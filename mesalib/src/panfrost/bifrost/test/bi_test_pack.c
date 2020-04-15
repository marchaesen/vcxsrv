/*
 * Copyright (C) 2020 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors (Collabora):
 *      Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include "bit.h"
#include "bi_print.h"
#include "util/half_float.h"
#include "bifrost/disassemble.h"

/* Instruction packing tests */

static bool
bit_test_single(struct panfrost_device *dev,
                bi_instruction *ins, 
                uint32_t input[4],
                bool fma, enum bit_debug debug)
{
        /* First, simulate the instruction */
        struct bit_state s = { 0 };
        memcpy(s.r, input, 16);
        bit_step(&s, ins, fma);

        /* Next, wrap it up and pack it */

        bi_instruction ldubo = {
                .type = BI_LOAD_UNIFORM,
                .src = {
                        BIR_INDEX_CONSTANT,
                        BIR_INDEX_ZERO
                },
                .src_types = {
                        nir_type_uint32,
                        nir_type_uint32,
                },
                .dest = BIR_INDEX_REGISTER | 0,
                .dest_type = nir_type_uint32,
                .writemask = 0xFFFF
        };

        bi_instruction ldva = {
                .type = BI_LOAD_VAR_ADDRESS,
                .writemask = (1 << 12) - 1,
                .dest = BIR_INDEX_REGISTER | 32,
                .dest_type = nir_type_uint32,
                .src = {
                        BIR_INDEX_CONSTANT,
                        BIR_INDEX_REGISTER | 61,
                        BIR_INDEX_REGISTER | 62,
                        0,
                },
                .src_types = {
                        nir_type_uint32,
                        nir_type_uint32,
                        nir_type_uint32,
                        nir_type_uint32,
                }
        };

        bi_instruction st = {
                .type = BI_STORE_VAR,
                .src = {
                        BIR_INDEX_REGISTER | 0,
                        ldva.dest, ldva.dest + 1, ldva.dest + 2,
                },
                .src_types = {
                        nir_type_uint32,
                        nir_type_uint32, nir_type_uint32, nir_type_uint32,
                },
                .store_channels = 4
        };

        bi_context *ctx = rzalloc(NULL, bi_context);
        ctx->stage = MESA_SHADER_VERTEX;

        bi_block *blk = rzalloc(ctx, bi_block);
        blk->scheduled = true;

        blk->base.predecessors = _mesa_set_create(blk,
                        _mesa_hash_pointer,
                        _mesa_key_pointer_equal);

        list_inithead(&ctx->blocks);
        list_addtail(&blk->base.link, &ctx->blocks);
        list_inithead(&blk->clauses);

        bi_clause *clauses[4] = {
                rzalloc(ctx, bi_clause),
                rzalloc(ctx, bi_clause),
                rzalloc(ctx, bi_clause),
                rzalloc(ctx, bi_clause)
        };

        for (unsigned i = 0; i < 4; ++i) {
                clauses[i]->bundle_count = 1;
                list_addtail(&clauses[i]->link, &blk->clauses);
                clauses[i]->scoreboard_id = (i & 1);

                if (i) {
                        clauses[i]->dependencies = 1 << (~i & 1);
                        clauses[i]->data_register_write_barrier = true;
                }
        }

        clauses[0]->bundles[0].add = &ldubo;
        clauses[0]->clause_type = BIFROST_CLAUSE_UBO;

        if (fma)
                clauses[1]->bundles[0].fma = ins;
        else
                clauses[1]->bundles[0].add = ins;

        clauses[0]->constant_count = 1;
        clauses[1]->constant_count = 1;
        clauses[1]->constants[0] = ins->constant.u64;

        clauses[2]->bundles[0].add = &ldva;
        clauses[3]->bundles[0].add = &st;

        clauses[2]->clause_type = BIFROST_CLAUSE_UBO;
        clauses[3]->clause_type = BIFROST_CLAUSE_SSBO_STORE;

        panfrost_program prog;
        bi_pack(ctx, &prog.compiled);

        bool succ = bit_vertex(dev, prog, input, 16, NULL, 0,
                        s.r, 16, debug);

        if (debug >= BIT_DEBUG_ALL || (!succ && debug >= BIT_DEBUG_FAIL)) {
                bi_print_shader(ctx, stderr);
                disassemble_bifrost(stderr, prog.compiled.data, prog.compiled.size, true);
        }

        return succ;
}

/* Utilities for generating tests */

static void
bit_generate_float4(float *mem)
{
        for (unsigned i = 0; i < 4; ++i)
                mem[i] = (float) ((rand() & 255) - 127) / 16.0;
}

static void
bit_generate_half8(uint16_t *mem)
{
        for (unsigned i = 0; i < 8; ++i)
                mem[i] = _mesa_float_to_half(((float) (rand() & 255) - 127) / 16.0);
}

static bi_instruction
bit_ins(enum bi_class C, unsigned argc, nir_alu_type base, unsigned size)
{
        nir_alu_type T = base | size;

        bi_instruction ins = {
                .type = C,
                .dest = BIR_INDEX_REGISTER | 0,
                .dest_type = T,
        };

        for (unsigned i = 0; i < argc; ++i) {
                ins.src[i] = BIR_INDEX_REGISTER | i;
                ins.src_types[i] = T;
        }

        return ins;
}

/* Tests all 64 combinations of floating point modifiers for a given
 * instruction / floating-type / test type */

static void
bit_fmod_helper(struct panfrost_device *dev,
                enum bi_class c, unsigned size, bool fma,
                uint32_t *input, enum bit_debug debug, unsigned op)
{
        bi_instruction ins = bit_ins(c, 2, nir_type_float, size);

        for (unsigned outmod = 0; outmod < 4; ++outmod) {
                for (unsigned inmod = 0; inmod < 16; ++inmod) {
                        ins.outmod = outmod;
                        ins.op.minmax = op;
                        ins.src_abs[0] = (inmod & 0x1);
                        ins.src_abs[1] = (inmod & 0x2);
                        ins.src_neg[0] = (inmod & 0x4);
                        ins.src_neg[1] = (inmod & 0x8);

                        /* Skip over tests that cannot run on FMA */
                        if (fma && (size == 16) && ins.src_abs[0] && ins.src_abs[1])
                                continue;

                        if (!bit_test_single(dev, &ins, input, fma, debug)) {
                                fprintf(stderr, "FAIL: fmod.%s%u.%s%s.%u\n",
                                                bi_class_name(c),
                                                size,
                                                fma ? "fma" : "add",
                                                outmod ? bi_output_mod_name(outmod) : ".none",
                                                inmod);
                        }
                }
        }
}

static void
bit_fma_helper(struct panfrost_device *dev,
                unsigned size, uint32_t *input, enum bit_debug debug)
{
        bi_instruction ins = bit_ins(BI_FMA, 3, nir_type_float, size);

        for (unsigned outmod = 0; outmod < 4; ++outmod) {
                for (unsigned inmod = 0; inmod < 8; ++inmod) {
                        ins.outmod = outmod;
                        ins.src_neg[0] = (inmod & 0x1);
                        ins.src_neg[1] = (inmod & 0x2);
                        ins.src_neg[2] = (inmod & 0x4);

                        if (!bit_test_single(dev, &ins, input, true, debug)) {
                                fprintf(stderr, "FAIL: fma%u%s.%u\n",
                                                size,
                                                outmod ? bi_output_mod_name(outmod) : ".none",
                                                inmod);
                        }
                }
        }
}

static void
bit_csel_helper(struct panfrost_device *dev,
                unsigned size, uint32_t *input, enum bit_debug debug)
{
        bi_instruction ins = bit_ins(BI_CSEL, 4, nir_type_uint, size);
        
        /* SCHEDULER: We can only read 3 registers at once. */
        ins.src[2] = ins.src[0];

        for (enum bi_cond cond = BI_COND_LT; cond <= BI_COND_NE; ++cond) {
                ins.csel_cond = cond;

                if (!bit_test_single(dev, &ins, input, true, debug)) {
                        fprintf(stderr, "FAIL: csel%u.%s\n",
                                        size, bi_cond_name(cond));
                }
        }
}

static void
bit_special_helper(struct panfrost_device *dev,
                unsigned size, uint32_t *input, enum bit_debug debug)
{
        bi_instruction ins = bit_ins(BI_SPECIAL, 1, nir_type_float, size);

        for (enum bi_special_op op = BI_SPECIAL_FRCP; op <= BI_SPECIAL_FRSQ; ++op) {
                for (unsigned c = 0; c < ((size == 16) ? 2 : 1); ++c) {
                        ins.op.special = op;
                        ins.swizzle[0][0] = c;

                        if (!bit_test_single(dev, &ins, input, false, debug)) {
                                fprintf(stderr, "FAIL: special%u.%s\n",
                                                size, bi_special_op_name(op));
                        }
                }
        }
}

static void
bit_convert_helper(struct panfrost_device *dev, unsigned from_size,
                unsigned to_size, unsigned cx, unsigned cy, bool FMA,
                enum bifrost_roundmode roundmode,
                uint32_t *input, enum bit_debug debug)
{
        bi_instruction ins = {
                .type = BI_CONVERT,
                .dest = BIR_INDEX_REGISTER | 0,
                .writemask = 0xF,
                .src = { BIR_INDEX_REGISTER | 0 }
        };

        nir_alu_type Ts[3] = { nir_type_float, nir_type_uint, nir_type_int };

        for (unsigned from_base = 0; from_base < 3; ++from_base) {
                for (unsigned to_base = 0; to_base < 3; ++to_base) {
                        /* Discard invalid combinations.. */
                        if ((from_size == to_size) && (from_base == to_base))
                                continue;

                        /* Can't switch signedness */
                        if (from_base && to_base)
                                continue;

                        /* No F16_TO_I32, etc */
                        if (from_size != to_size && from_base == 0 && to_base)
                                continue;

                        if (from_size != to_size && from_base && to_base == 0)
                                continue;

                        /* No need, just ignore the upper half */
                        if (from_size > to_size && from_base == to_base && from_base)
                                continue;

                        ins.dest_type = Ts[to_base] | to_size;
                        ins.src_types[0] = Ts[from_base] | from_size;
                        ins.roundmode = roundmode;
                        ins.swizzle[0][0] = cx;
                        ins.swizzle[0][1] = cy;

                        if (!bit_test_single(dev, &ins, input, FMA, debug)) {
                                fprintf(stderr, "FAIL: convert.%u-%u.%u-%u.%u%u\n",
                                                from_base, from_size,
                                                to_base, to_size,
                                                cx, cy);
                        }
                }
        }
}

void
bit_packing(struct panfrost_device *dev, enum bit_debug debug)
{
        float input32[4];
        uint16_t input16[8];

        bit_generate_float4(input32);
        bit_generate_half8(input16);

        for (unsigned sz = 16; sz <= 32; sz *= 2) {
                uint32_t *input =
                        (sz == 16) ? (uint32_t *) input16 :
                        (uint32_t *) input32;

                bit_fmod_helper(dev, BI_ADD, sz, true, input, debug, 0);

                if (sz == 32) {
                        bit_fmod_helper(dev, BI_ADD, sz, false, input, debug, 0);
                        bit_fmod_helper(dev, BI_MINMAX, sz, false, input, debug, BI_MINMAX_MIN);
                        bit_fmod_helper(dev, BI_MINMAX, sz, false, input, debug, BI_MINMAX_MAX);
                }

                bit_fma_helper(dev, sz, input, debug);
        }

        for (unsigned sz = 32; sz <= 32; sz *= 2)
                bit_csel_helper(dev, sz, (uint32_t *) input32, debug);

        float special[4] = { 0.9 };
        uint32_t special16[4] = { _mesa_float_to_half(special[0]) | (_mesa_float_to_half(0.2) << 16) };

        for (unsigned sz = 16; sz <= 32; sz *= 2) {
                uint32_t *input =
                        (sz == 16) ? special16 :
                        (uint32_t *) special;

                bit_special_helper(dev, sz, input, debug);
        }

        for (unsigned rm = 0; rm < 4; ++rm) {
                bit_convert_helper(dev, 32, 32, 0, 0, false, rm, (uint32_t *) input32, debug);

                for (unsigned c = 0; c < 2; ++c)
                        bit_convert_helper(dev, 32, 16, c, 0, false, rm, (uint32_t *) input32, debug);

                bit_convert_helper(dev, 16, 32, 0, 0, false, rm, (uint32_t *) input16, debug);

                for (unsigned c = 0; c < 4; ++c)
                        bit_convert_helper(dev, 16, 16, c & 1, c >> 1, false, rm, (uint32_t *) input16, debug);
        }


}
