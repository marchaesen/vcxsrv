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

static void
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
                .segment = BI_SEGMENT_UBO,
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
                .vector_channels = 4,
        };

        bi_instruction ldva = {
                .type = BI_LOAD_VAR_ADDRESS,
                .vector_channels = 3,
                .dest = BIR_INDEX_REGISTER | 32,
                .dest_type = nir_type_uint32,
                .format = nir_type_uint32,
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
                .vector_channels = 4
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
                        clauses[i]->staging_barrier = true;
                }
        }

        clauses[0]->bundles[0].add = &ldubo;
        clauses[0]->message_type = BIFROST_MESSAGE_ATTRIBUTE;

        if (fma)
                clauses[1]->bundles[0].fma = ins;
        else
                clauses[1]->bundles[0].add = ins;

        clauses[0]->constant_count = 1;
        clauses[1]->constant_count = 1;
        clauses[1]->constants[0] = ins->constant.u64;

        clauses[2]->bundles[0].add = &ldva;
        clauses[3]->bundles[0].add = &st;

        clauses[2]->message_type = BIFROST_MESSAGE_ATTRIBUTE;
        clauses[3]->message_type = BIFROST_MESSAGE_STORE;

        panfrost_program prog = { 0 };
        util_dynarray_init(&prog.compiled, NULL);
        bi_pack(ctx, &prog.compiled);

        bool succ = bit_vertex(dev, &prog, input, 16, NULL, 0,
                        s.r, 16, debug);

        if (debug >= BIT_DEBUG_ALL || (!succ && debug >= BIT_DEBUG_FAIL)) {
                bi_print_shader(ctx, stderr);
                disassemble_bifrost(stderr, prog.compiled.data, prog.compiled.size, true);
        }

        if (!succ)
                fprintf(stderr, "FAIL\n");
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

#define BIT_FOREACH_SWIZZLE(swz, args, sz) \
        for (unsigned swz = 0; swz < ((sz == 16) ? (1 << (2 * args)) : 1); ++swz)

static void
bit_apply_swizzle(bi_instruction *ins, unsigned swz, unsigned args, unsigned sz)
{
        unsigned slots_per_arg = (sz == 16) ? 4 : 1;
        unsigned slots_per_chan = (sz == 16) ? 1 : 0;
        unsigned mask = (sz == 16) ? 1 : 0;

        for (unsigned i = 0; i < args; ++i) {
                for (unsigned j = 0; j < (32 / sz); ++j) {
                        ins->swizzle[i][j] = ((swz >> (slots_per_arg * i)) >> (slots_per_chan * j)) & mask;
                }
        }
}

/* Tests all 64 combinations of floating point modifiers for a given
 * instruction / floating-type / test type */

static void
bit_fmod_helper(struct panfrost_device *dev,
                enum bi_class c, unsigned size, bool fma,
                uint32_t *input, enum bit_debug debug, unsigned op)
{
        bi_instruction ins = bit_ins(c, 2, nir_type_float, size);

        bool fp16 = (size == 16);
        bool has_outmods = fma || !fp16;

        for (unsigned outmod = 0; outmod < (has_outmods ? 4 : 1); ++outmod) {
        BIT_FOREACH_SWIZZLE(swz, 2, size) {
                for (unsigned inmod = 0; inmod < 16; ++inmod) {
                        ins.outmod = outmod;
                        ins.op.minmax = op;
                        ins.src_abs[0] = (inmod & 0x1);
                        ins.src_abs[1] = (inmod & 0x2);
                        ins.src_neg[0] = (inmod & 0x4);
                        ins.src_neg[1] = (inmod & 0x8);
                        bit_apply_swizzle(&ins, swz, 2, size);
                        bit_test_single(dev, &ins, input, fma, debug);
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
                        bit_test_single(dev, &ins, input, true, debug);
                }
        }
}

static void
bit_fma_mscale_helper(struct panfrost_device *dev, uint32_t *input, enum bit_debug debug)
{
        bi_instruction ins = bit_ins(BI_FMA, 4, nir_type_float, 32);
        ins.op.mscale = true;
        ins.src_types[3] = nir_type_int32;
        ins.src[2] = ins.src[3]; /* Not enough ports! */

        for (unsigned outmod = 0; outmod < 4; ++outmod) {
                for (unsigned inmod = 0; inmod < 8; ++inmod) {
                        ins.outmod = outmod;
                        ins.src_abs[0] = (inmod & 0x1);
                        ins.src_neg[1] = (inmod & 0x2);
                        ins.src_neg[2] = (inmod & 0x4);
                        bit_test_single(dev, &ins, input, true, debug);
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
                ins.cond = cond;
                bit_test_single(dev, &ins, input, true, debug);
        }
}

static void
bit_special_helper(struct panfrost_device *dev,
                unsigned size, uint32_t *input, enum bit_debug debug)
{
        bi_instruction ins = bit_ins(BI_SPECIAL_ADD, 2, nir_type_float, size);
        uint32_t exp_input[4];

        for (enum bi_special_op op = BI_SPECIAL_FRCP; op <= BI_SPECIAL_EXP2_LOW; ++op) {
                if (op == BI_SPECIAL_EXP2_LOW) {
                        /* exp2 only supported in fp32 mode */
                        if (size != 32)
                                continue;

                        /* Give expected input */
                        exp_input[1] = input[0];
                        float *ff = (float *) input;
                        exp_input[0] = (int) (ff[0] * (1 << 24));
                }

                for (unsigned c = 0; c < ((size == 16) ? 2 : 1); ++c) {
                        ins.op.special = op;
                        ins.swizzle[0][0] = c;
                        bit_test_single(dev, &ins,
                                                op == BI_SPECIAL_EXP2_LOW ? exp_input : input,
                                                false, debug);
                }
        }
}

static void
bit_table_helper(struct panfrost_device *dev, uint32_t *input, enum bit_debug debug)
{
        bi_instruction ins = bit_ins(BI_TABLE, 1, nir_type_float, 32);

        for (enum bi_table_op op = 0; op <= BI_TABLE_LOG2_U_OVER_U_1_LOW; ++op) {
                ins.op.table = op;
                bit_test_single(dev, &ins, input, false, debug);
        }
}

static void
bit_frexp_helper(struct panfrost_device *dev, uint32_t *input, enum bit_debug debug)
{
        bi_instruction ins = bit_ins(BI_FREXP, 1, nir_type_float, 32);
        ins.dest_type = nir_type_int32;

        for (enum bi_frexp_op op = 0; op <= BI_FREXPE_LOG; ++op) {
                ins.op.frexp = op;
                bit_test_single(dev, &ins, input, true, debug);
        }
}

static void
bit_round_helper(struct panfrost_device *dev, uint32_t *input, unsigned sz, bool FMA, enum bit_debug debug)
{
        bi_instruction ins = bit_ins(BI_ROUND, 1, nir_type_float, sz);

        for (enum bifrost_roundmode mode = 0; mode <= 3; ++mode) {
        BIT_FOREACH_SWIZZLE(swz, 1, sz) {
                bit_apply_swizzle(&ins, swz, 1, sz);
                ins.roundmode = mode;
                bit_test_single(dev, &ins, input, FMA, debug);
        }
        }
}

static void
bit_reduce_helper(struct panfrost_device *dev, uint32_t *input, enum bit_debug debug)
{
        bi_instruction ins = bit_ins(BI_REDUCE_FMA, 2, nir_type_float, 32);

        for (enum bi_reduce_op op = 0; op <= BI_REDUCE_ADD_FREXPM; ++op) {
                ins.op.reduce = op;
                bit_test_single(dev, &ins, input, true, debug);
        }
}

static void
bit_select_helper(struct panfrost_device *dev, uint32_t *input, unsigned size, enum bit_debug debug)
{
        unsigned C = 32 / size;
        bi_instruction ins = bit_ins(BI_SELECT, C, nir_type_uint, 32);

        for (unsigned c = 0; c < C; ++c)
                ins.src_types[c] = nir_type_uint | size;

        if (size == 8) {
                /* SCHEDULER: We can only read 3 registers at once. */
                ins.src[2] = ins.src[0];
        }

        /* Each argument has swizzle {lo, hi} so 2^C options */
        unsigned hi = (size == 16) ? 1 : 2;

        for (unsigned add = 0; add < ((size == 16) ? 2 : 1); ++add) {
                for (unsigned swizzle = 0; swizzle < (1 << C); ++swizzle) {
                        for (unsigned i = 0; i < C; ++i)
                                ins.swizzle[i][0] = ((swizzle >> i) & 1) ? hi : 0;

                        bit_test_single(dev, &ins, input, !add, debug);
                }
        }
}

static void
bit_fcmp_helper(struct panfrost_device *dev, uint32_t *input, unsigned size, enum bit_debug debug, bool FMA)
{
        bi_instruction ins = bit_ins(BI_CMP, 2, nir_type_float, size);
        ins.dest_type = nir_type_uint | size;

        /* 16-bit has swizzles and abs. 32-bit has abs/neg mods. */
        unsigned max_mods = (size == 16) ? 64 : (size == 32) ? 16 : 1;

        for (enum bi_cond cond = BI_COND_LT; cond <= BI_COND_NE; ++cond) {
                for (unsigned mods = 0; mods < max_mods; ++mods) {
                        ins.cond = cond;

                        if (size == 16) {
                                for (unsigned i = 0; i < 2; ++i) {
                                        ins.swizzle[i][0] = ((mods >> (i * 2)) & 1) ? 1 : 0;
                                        ins.swizzle[i][1] = ((mods >> (i * 2)) & 2) ? 1 : 0;
                                }

                                ins.src_abs[0] = (mods & 16) ? true : false;
                                ins.src_abs[1] = (mods & 32) ? true : false;
                        } else if (size == 8) {
                                for (unsigned i = 0; i < 2; ++i) {
                                        for (unsigned j = 0; j < 4; ++j)
                                                ins.swizzle[i][j] = j;
                                }
                        } else if (size == 32) {
                                ins.src_abs[0] = (mods & 1) ? true : false;
                                ins.src_abs[1] = (mods & 2) ? true : false;
                                ins.src_neg[0] = (mods & 4) ? true : false;
                                ins.src_neg[1] = (mods & 8) ? true : false;
                        }

                        bit_test_single(dev, &ins, input, FMA, debug);
                }
        }
}

static void
bit_icmp_helper(struct panfrost_device *dev, uint32_t *input, unsigned size, nir_alu_type T, enum bit_debug debug)
{
        bi_instruction ins = bit_ins(BI_CMP, 2, T, size);
        ins.dest_type = nir_type_uint | size;

        for (enum bi_cond cond = BI_COND_LT; cond <= BI_COND_NE; ++cond) { 
        BIT_FOREACH_SWIZZLE(swz, 2, size) {
                ins.cond = cond;
                bit_apply_swizzle(&ins, swz, 2, size);
                bit_test_single(dev, &ins, input, false, debug);
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

                        if (to_size == 16 && from_size == 32) {
                                ins.src_types[1] = ins.src_types[0];
                                ins.src[1] = ins.src[0];
                        } else {
                                ins.src[1] = ins.src_types[1] = 0;
                        }

                        bit_test_single(dev, &ins, input, FMA, debug);
                }
        }
}

static void
bit_constant_helper(struct panfrost_device *dev,
                uint32_t *input, enum bit_debug debug)
{
        enum bi_class C[3] = { BI_MOV, BI_ADD, BI_FMA };

        for (unsigned doubled = 0; doubled < 2; ++doubled) {
                for (unsigned count = 1; count <= 3; ++count) {
                        bi_instruction ins = bit_ins(C[count - 1], count, nir_type_float, 32);

                        ins.src[0] = BIR_INDEX_CONSTANT | 0;
                        ins.src[1] = (count >= 2) ? BIR_INDEX_CONSTANT | (doubled ? 32 : 0) : 0;
                        ins.src[2] = (count >= 3) ? BIR_INDEX_ZERO : 0;

                        ins.constant.u64 = doubled ?
                                0x3f800000ull | (0x3f000000ull << 32ull) :
                                0x3f800000ull;

                        bit_test_single(dev, &ins, input, true, debug);
                }
        }
}

static void
bit_swizzle_identity(bi_instruction *ins, unsigned args, unsigned size)
{
        for (unsigned i = 0; i < 2; ++i) {
                for (unsigned j = 0; j < (32 / size); ++j)
                        ins->swizzle[i][j] = j;
        }
}

static void
bit_bitwise_helper(struct panfrost_device *dev, uint32_t *input, unsigned size, enum bit_debug debug)
{
        bi_instruction ins = bit_ins(BI_BITWISE, 3, nir_type_uint, size);
        bit_swizzle_identity(&ins, 2, size);

        /* TODO: shifts */
        ins.src[2] = BIR_INDEX_ZERO;
        ins.src_types[2] = nir_type_uint8;

        for (unsigned op = BI_BITWISE_AND; op <= BI_BITWISE_XOR; ++op) {
                ins.op.bitwise = op;

                for (unsigned mods = 0; mods < 4; ++mods) {
                        ins.bitwise.dest_invert = mods & 1;
                        ins.bitwise.src1_invert = mods & 2;

                        /* Skip out-of-spec combinations */
                        if (ins.bitwise.src1_invert && op == BI_BITWISE_XOR)
                                continue;

                        bit_test_single(dev, &ins, input, true, debug);
                }
        }
}

static void
bit_imath_helper(struct panfrost_device *dev, uint32_t *input, unsigned size, enum bit_debug debug, bool FMA)
{
        bi_instruction ins = bit_ins(BI_IMATH, 2, nir_type_uint, size);
        bit_swizzle_identity(&ins, 2, size);
        ins.src[2] = BIR_INDEX_ZERO; /* carry/borrow for FMA */

        for (unsigned op = BI_IMATH_ADD; op <= BI_IMATH_SUB; ++op) {
                ins.op.imath = op;
                bit_test_single(dev, &ins, input, FMA, debug);
        }
}

void
bit_packing(struct panfrost_device *dev, enum bit_debug debug)
{
        float input32[4];
        uint16_t input16[8];

        bit_generate_float4(input32);
        bit_generate_half8(input16);

        bit_constant_helper(dev, (uint32_t *) input32, debug);

        for (unsigned sz = 16; sz <= 32; sz *= 2) {
                uint32_t *input =
                        (sz == 16) ? (uint32_t *) input16 :
                        (uint32_t *) input32;

                bit_fmod_helper(dev, BI_ADD, sz, true, input, debug, 0);
                bit_fmod_helper(dev, BI_ADD, sz, false, input, debug, 0);
                bit_round_helper(dev, (uint32_t *) input32, sz, true, debug);

                bit_fmod_helper(dev, BI_MINMAX, sz, false, input, debug, BI_MINMAX_MIN);
                bit_fmod_helper(dev, BI_MINMAX, sz, false, input, debug, BI_MINMAX_MAX);

                bit_fma_helper(dev, sz, input, debug);
                bit_icmp_helper(dev, input, sz, nir_type_uint, debug);
                bit_icmp_helper(dev, input, sz, nir_type_int, debug);
        }

        for (unsigned sz = 16; sz <= 32; sz *= 2)
                bit_csel_helper(dev, sz, (uint32_t *) input32, debug);

        float special[4] = { 0.9 };
        uint32_t special16[4] = { _mesa_float_to_half(special[0]) | (_mesa_float_to_half(0.2) << 16) };

        bit_table_helper(dev, (uint32_t *) special, debug);

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

        bit_frexp_helper(dev, (uint32_t *) input32, debug);
        bit_reduce_helper(dev, (uint32_t *) input32, debug);

        uint32_t mscale_input[4];
        memcpy(mscale_input, input32, sizeof(input32));
        mscale_input[3] = 0x7;
        bit_fma_mscale_helper(dev, mscale_input, debug);

        for (unsigned sz = 8; sz <= 16; sz *= 2) {
                bit_select_helper(dev, (uint32_t *) input32, sz, debug);
        }

        bit_fcmp_helper(dev, (uint32_t *) input32, 32, debug, true);
        bit_fcmp_helper(dev, (uint32_t *) input32, 16, debug, true);

        for (unsigned sz = 8; sz <= 32; sz *= 2) {
                bit_bitwise_helper(dev, (uint32_t *) input32, sz, debug);
                bit_imath_helper(dev, (uint32_t *) input32, sz, debug, false);
        }

        bit_imath_helper(dev, (uint32_t *) input32, 32, debug, true);
}
