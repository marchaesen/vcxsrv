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

#include "main/mtypes.h"
#include "compiler/glsl/glsl_to_nir.h"
#include "compiler/nir_types.h"
#include "util/imports.h"
#include "compiler/nir/nir_builder.h"

#include "disassemble.h"
#include "bifrost_compile.h"
#include "bifrost_nir.h"
#include "compiler.h"
#include "bi_quirks.h"
#include "bi_print.h"

static bi_block *emit_cf_list(bi_context *ctx, struct exec_list *list);
static bi_instruction *bi_emit_branch(bi_context *ctx);
static void bi_schedule_barrier(bi_context *ctx);

static void
emit_jump(bi_context *ctx, nir_jump_instr *instr)
{
        bi_instruction *branch = bi_emit_branch(ctx);

        switch (instr->type) {
        case nir_jump_break:
                branch->branch.target = ctx->break_block;
                break;
        case nir_jump_continue:
                branch->branch.target = ctx->continue_block;
                break;
        default:
                unreachable("Unhandled jump type");
        }

        pan_block_add_successor(&ctx->current_block->base, &branch->branch.target->base);
}

/* Gets a bytemask for a complete vecN write */
static unsigned
bi_mask_for_channels_32(unsigned i)
{
        return (1 << (4 * i)) - 1;
}

static bi_instruction
bi_load(enum bi_class T, nir_intrinsic_instr *instr)
{
        bi_instruction load = {
                .type = T,
                .writemask = bi_mask_for_channels_32(instr->num_components),
                .src = { BIR_INDEX_CONSTANT },
                .constant = { .u64 = nir_intrinsic_base(instr) },
        };

        const nir_intrinsic_info *info = &nir_intrinsic_infos[instr->intrinsic];

        if (info->has_dest)
                load.dest = bir_dest_index(&instr->dest);

        if (info->has_dest && info->index_map[NIR_INTRINSIC_TYPE] > 0)
                load.dest_type = nir_intrinsic_type(instr);

        nir_src *offset = nir_get_io_offset_src(instr);

        if (nir_src_is_const(*offset))
                load.constant.u64 += nir_src_as_uint(*offset);
        else
                load.src[0] = bir_src_index(offset);

        return load;
}

static void
bi_emit_ld_vary(bi_context *ctx, nir_intrinsic_instr *instr)
{
        bi_instruction ins = bi_load(BI_LOAD_VAR, instr);
        ins.load_vary.interp_mode = BIFROST_INTERP_DEFAULT; /* TODO */
        ins.load_vary.reuse = false; /* TODO */
        ins.load_vary.flat = instr->intrinsic != nir_intrinsic_load_interpolated_input;
        ins.dest_type = nir_type_float | nir_dest_bit_size(instr->dest);

        if (nir_src_is_const(*nir_get_io_offset_src(instr))) {
                /* Zero it out for direct */
                ins.src[1] = BIR_INDEX_ZERO;
        } else {
                /* R61 contains sample mask stuff, TODO RA XXX */
                ins.src[1] = BIR_INDEX_REGISTER | 61;
        }

        bi_emit(ctx, ins);
}

static void
bi_emit_frag_out(bi_context *ctx, nir_intrinsic_instr *instr)
{
        if (!ctx->emitted_atest) {
                bi_instruction ins = {
                        .type = BI_ATEST,
                        .src = {
                                BIR_INDEX_REGISTER | 60 /* TODO: RA */,
                                bir_src_index(&instr->src[0])
                        },
                        .src_types = {
                                nir_type_uint32,
                                nir_type_float32
                        },
                        .swizzle = {
                                { 0 },
                                { 3, 0 } /* swizzle out the alpha */
                        },
                        .dest = BIR_INDEX_REGISTER | 60 /* TODO: RA */,
                        .dest_type = nir_type_uint32,
                        .writemask = 0xF
                };

                bi_emit(ctx, ins);
                bi_schedule_barrier(ctx);
                ctx->emitted_atest = true;
        }

        bi_instruction blend = {
                .type = BI_BLEND,
                .blend_location = nir_intrinsic_base(instr),
                .src = {
                        bir_src_index(&instr->src[0]),
                        BIR_INDEX_REGISTER | 60 /* Can this be arbitrary? */,
                },
                .src_types = {
                        nir_type_float32,
                        nir_type_uint32
                },
                .swizzle = {
                        { 0, 1, 2, 3 },
                        { 0 }
                },
                .dest = BIR_INDEX_REGISTER | 48 /* Looks like magic */,
                .dest_type = nir_type_uint32,
                .writemask = 0xF
        };

        bi_emit(ctx, blend);
        bi_schedule_barrier(ctx);
}

static bi_instruction
bi_load_with_r61(enum bi_class T, nir_intrinsic_instr *instr)
{
        bi_instruction ld = bi_load(T, instr);
        ld.src[1] = BIR_INDEX_REGISTER | 61; /* TODO: RA */
        ld.src[2] = BIR_INDEX_REGISTER | 62;
        ld.src[3] = 0;
        ld.src_types[1] = nir_type_uint32;
        ld.src_types[2] = nir_type_uint32;
        ld.src_types[3] = nir_intrinsic_type(instr);
        return ld;
}

static void
bi_emit_st_vary(bi_context *ctx, nir_intrinsic_instr *instr)
{
        bi_instruction address = bi_load_with_r61(BI_LOAD_VAR_ADDRESS, instr);
        address.dest = bi_make_temp(ctx);
        address.dest_type = nir_type_uint32;
        address.writemask = (1 << 12) - 1;

        bi_instruction st = {
                .type = BI_STORE_VAR,
                .src = {
                        bir_src_index(&instr->src[0]),
                        address.dest, address.dest, address.dest,
                },
                .src_types = {
                        nir_type_uint32,
                        nir_type_uint32, nir_type_uint32, nir_type_uint32,
                },
                .swizzle = {
                        { 0, 1, 2, 3 },
                        { 0 }, { 1 }, { 2}
                },
                .store_channels = 4, /* TODO: WRITEMASK */
        };

        bi_emit(ctx, address);
        bi_emit(ctx, st);
}

static void
bi_emit_ld_uniform(bi_context *ctx, nir_intrinsic_instr *instr)
{
        bi_instruction ld = bi_load(BI_LOAD_UNIFORM, instr);
        ld.src[1] = BIR_INDEX_ZERO; /* TODO: UBO index */

        /* TODO: Indirect access, since we need to multiply by the element
         * size. I believe we can get this lowering automatically via
         * nir_lower_io (as mul instructions) with the proper options, but this
         * is TODO */
        assert(ld.src[0] & BIR_INDEX_CONSTANT);
        ld.constant.u64 += ctx->sysvals.sysval_count;
        ld.constant.u64 *= 16;

        bi_emit(ctx, ld);
}

static void
bi_emit_sysval(bi_context *ctx, nir_instr *instr,
                unsigned nr_components, unsigned offset)
{
        nir_dest nir_dest;

        /* Figure out which uniform this is */
        int sysval = panfrost_sysval_for_instr(instr, &nir_dest);
        void *val = _mesa_hash_table_u64_search(ctx->sysvals.sysval_to_id, sysval);

        /* Sysvals are prefix uniforms */
        unsigned uniform = ((uintptr_t) val) - 1;

        /* Emit the read itself -- this is never indirect */

        bi_instruction load = {
                .type = BI_LOAD_UNIFORM,
                .writemask = (1 << (nr_components * 4)) - 1,
                .src = { BIR_INDEX_CONSTANT, BIR_INDEX_ZERO },
                .constant = { (uniform * 16) + offset },
                .dest = bir_dest_index(&nir_dest),
                .dest_type = nir_type_uint32, /* TODO */
        };

        bi_emit(ctx, load);
}

static void
emit_intrinsic(bi_context *ctx, nir_intrinsic_instr *instr)
{

        switch (instr->intrinsic) {
        case nir_intrinsic_load_barycentric_pixel:
                /* stub */
                break;
        case nir_intrinsic_load_interpolated_input:
        case nir_intrinsic_load_input:
                if (ctx->stage == MESA_SHADER_FRAGMENT)
                        bi_emit_ld_vary(ctx, instr);
                else if (ctx->stage == MESA_SHADER_VERTEX)
                        bi_emit(ctx, bi_load_with_r61(BI_LOAD_ATTR, instr));
                else {
                        unreachable("Unsupported shader stage");
                }
                break;

        case nir_intrinsic_store_output:
                if (ctx->stage == MESA_SHADER_FRAGMENT)
                        bi_emit_frag_out(ctx, instr);
                else if (ctx->stage == MESA_SHADER_VERTEX)
                        bi_emit_st_vary(ctx, instr);
                else
                        unreachable("Unsupported shader stage");
                break;

        case nir_intrinsic_load_uniform:
                bi_emit_ld_uniform(ctx, instr);
                break;

        case nir_intrinsic_load_ssbo_address:
                bi_emit_sysval(ctx, &instr->instr, 1, 0);
                break;

        case nir_intrinsic_get_buffer_size:
                bi_emit_sysval(ctx, &instr->instr, 1, 8);
                break;

        case nir_intrinsic_load_viewport_scale:
        case nir_intrinsic_load_viewport_offset:
        case nir_intrinsic_load_num_work_groups:
        case nir_intrinsic_load_sampler_lod_parameters_pan:
                bi_emit_sysval(ctx, &instr->instr, 3, 0);
                break;

        default:
                /* todo */
                break;
        }
}

static void
emit_load_const(bi_context *ctx, nir_load_const_instr *instr)
{
        /* Make sure we've been lowered */
        assert(instr->def.num_components == 1);

        bi_instruction move = {
                .type = BI_MOV,
                .dest = bir_ssa_index(&instr->def),
                .dest_type = instr->def.bit_size | nir_type_uint,
                .writemask = (1 << (instr->def.bit_size / 8)) - 1,
                .src = {
                        BIR_INDEX_CONSTANT
                },
                .src_types = {
                        instr->def.bit_size | nir_type_uint,
                },
                .constant = {
                        .u64 = nir_const_value_as_uint(instr->value[0], instr->def.bit_size)
                }
        };

        bi_emit(ctx, move);
}

#define BI_CASE_CMP(op) \
        case op##8: \
        case op##16: \
        case op##32: \

static enum bi_class
bi_class_for_nir_alu(nir_op op)
{
        switch (op) {
        case nir_op_iadd:
        case nir_op_fadd:
        case nir_op_fsub:
                return BI_ADD;
        case nir_op_isub:
                return BI_ISUB;

        BI_CASE_CMP(nir_op_flt)
        BI_CASE_CMP(nir_op_fge)
        BI_CASE_CMP(nir_op_feq)
        BI_CASE_CMP(nir_op_fne)
        BI_CASE_CMP(nir_op_ilt)
        BI_CASE_CMP(nir_op_ige)
        BI_CASE_CMP(nir_op_ieq)
        BI_CASE_CMP(nir_op_ine)
                return BI_CMP;

        case nir_op_b8csel:
        case nir_op_b16csel:
        case nir_op_b32csel:
                return BI_CSEL;

        case nir_op_i2i8:
        case nir_op_i2i16:
        case nir_op_i2i32:
        case nir_op_i2i64:
        case nir_op_u2u8:
        case nir_op_u2u16:
        case nir_op_u2u32:
        case nir_op_u2u64:
        case nir_op_f2i16:
        case nir_op_f2i32:
        case nir_op_f2i64:
        case nir_op_f2u16:
        case nir_op_f2u32:
        case nir_op_f2u64:
        case nir_op_i2f16:
        case nir_op_i2f32:
        case nir_op_i2f64:
        case nir_op_u2f16:
        case nir_op_u2f32:
        case nir_op_u2f64:
        case nir_op_f2f16:
        case nir_op_f2f32:
        case nir_op_f2f64:
        case nir_op_f2fmp:
                return BI_CONVERT;

        case nir_op_vec2:
        case nir_op_vec3:
        case nir_op_vec4:
                return BI_COMBINE;

        case nir_op_vec8:
        case nir_op_vec16:
                unreachable("should've been lowered");

        case nir_op_ffma:
        case nir_op_fmul:
                return BI_FMA;

        case nir_op_imin:
        case nir_op_imax:
        case nir_op_umin:
        case nir_op_umax:
        case nir_op_fmin:
        case nir_op_fmax:
                return BI_MINMAX;

        case nir_op_fsat:
        case nir_op_fneg:
        case nir_op_fabs:
                return BI_FMOV;
        case nir_op_mov:
                return BI_MOV;

        case nir_op_fround_even:
        case nir_op_fceil:
        case nir_op_ffloor:
        case nir_op_ftrunc:
                return BI_ROUND;

        case nir_op_frcp:
        case nir_op_frsq:
                return BI_SPECIAL;

        default:
                unreachable("Unknown ALU op");
        }
}

/* Gets a bi_cond for a given NIR comparison opcode. In soft mode, it will
 * return BI_COND_ALWAYS as a sentinel if it fails to do so (when used for
 * optimizations). Otherwise it will bail (when used for primary code
 * generation). */

static enum bi_cond
bi_cond_for_nir(nir_op op, bool soft)
{
        switch (op) {
        BI_CASE_CMP(nir_op_flt)
        BI_CASE_CMP(nir_op_ilt)
                return BI_COND_LT;

        BI_CASE_CMP(nir_op_fge)
        BI_CASE_CMP(nir_op_ige)
                return BI_COND_GE;

        BI_CASE_CMP(nir_op_feq)
        BI_CASE_CMP(nir_op_ieq)
                return BI_COND_EQ;

        BI_CASE_CMP(nir_op_fne)
        BI_CASE_CMP(nir_op_ine)
                return BI_COND_NE;
        default:
                if (soft)
                        return BI_COND_ALWAYS;
                else
                        unreachable("Invalid compare");
        }
}

static void
bi_copy_src(bi_instruction *alu, nir_alu_instr *instr, unsigned i, unsigned to,
                unsigned *constants_left, unsigned *constant_shift)
{
        unsigned bits = nir_src_bit_size(instr->src[i].src);
        unsigned dest_bits = nir_dest_bit_size(instr->dest.dest);

        alu->src_types[to] = nir_op_infos[instr->op].input_types[i]
                | bits;

        /* Try to inline a constant */
        if (nir_src_is_const(instr->src[i].src) && *constants_left && (dest_bits == bits)) {
                alu->constant.u64 |=
                        (nir_src_as_uint(instr->src[i].src)) << *constant_shift;

                alu->src[to] = BIR_INDEX_CONSTANT | (*constant_shift);
                --(*constants_left);
                (*constant_shift) += dest_bits;
                return;
        }

        alu->src[to] = bir_src_index(&instr->src[i].src);

        /* We assert scalarization above */
        alu->swizzle[to][0] = instr->src[i].swizzle[0];
}

static void
bi_fuse_csel_cond(bi_instruction *csel, nir_alu_src cond,
                unsigned *constants_left, unsigned *constant_shift)
{
        /* Bail for vector weirdness */
        if (cond.swizzle[0] != 0)
                return;

        if (!cond.src.is_ssa)
                return;

        nir_ssa_def *def = cond.src.ssa;
        nir_instr *parent = def->parent_instr;

        if (parent->type != nir_instr_type_alu)
                return;

        nir_alu_instr *alu = nir_instr_as_alu(parent);

        /* Try to match a condition */
        enum bi_cond bcond = bi_cond_for_nir(alu->op, true);

        if (bcond == BI_COND_ALWAYS)
                return;

        /* We found one, let's fuse it in */
        csel->csel_cond = bcond;
        bi_copy_src(csel, alu, 0, 0, constants_left, constant_shift);
        bi_copy_src(csel, alu, 1, 1, constants_left, constant_shift);
}

static void
emit_alu(bi_context *ctx, nir_alu_instr *instr)
{
        /* Assume it's something we can handle normally */
        bi_instruction alu = {
                .type = bi_class_for_nir_alu(instr->op),
                .dest = bir_dest_index(&instr->dest.dest),
                .dest_type = nir_op_infos[instr->op].output_type
                        | nir_dest_bit_size(instr->dest.dest),
        };

        /* TODO: Implement lowering of special functions for older Bifrost */
        assert((alu.type != BI_SPECIAL) || !(ctx->quirks & BIFROST_NO_FAST_OP));

        if (instr->dest.dest.is_ssa) {
                /* Construct a writemask */
                unsigned bits_per_comp = instr->dest.dest.ssa.bit_size;
                unsigned comps = instr->dest.dest.ssa.num_components;

                if (alu.type != BI_COMBINE)
                        assert(comps == 1);

                unsigned bits = bits_per_comp * comps;
                unsigned bytes = bits / 8;
                alu.writemask = (1 << bytes) - 1;
        } else {
                unsigned comp_mask = instr->dest.write_mask;

                alu.writemask = pan_to_bytemask(nir_dest_bit_size(instr->dest.dest),
                                comp_mask);
        }

        /* We inline constants as we go. This tracks how many constants have
         * been inlined, since we're limited to 64-bits of constants per
         * instruction */

        unsigned dest_bits = nir_dest_bit_size(instr->dest.dest);
        unsigned constants_left = (64 / dest_bits);
        unsigned constant_shift = 0;

        if (alu.type == BI_COMBINE)
                constants_left = 0;

        /* Copy sources */

        unsigned num_inputs = nir_op_infos[instr->op].num_inputs;
        assert(num_inputs <= ARRAY_SIZE(alu.src));

        for (unsigned i = 0; i < num_inputs; ++i) {
                unsigned f = 0;

                if (i && alu.type == BI_CSEL)
                        f++;

                bi_copy_src(&alu, instr, i, i + f, &constants_left, &constant_shift);
        }

        /* Op-specific fixup */
        switch (instr->op) {
        case nir_op_fmul:
                alu.src[2] = BIR_INDEX_ZERO; /* FMA */
                alu.src_types[2] = alu.src_types[1];
                break;
        case nir_op_fsat:
                alu.outmod = BIFROST_SAT; /* FMOV */
                break;
        case nir_op_fneg:
                alu.src_neg[0] = true; /* FMOV */
                break;
        case nir_op_fabs:
                alu.src_abs[0] = true; /* FMOV */
                break;
        case nir_op_fsub:
                alu.src_neg[1] = true; /* FADD */
                break;
        case nir_op_fmax:
        case nir_op_imax:
        case nir_op_umax:
                alu.op.minmax = BI_MINMAX_MAX; /* MINMAX */
                break;
        case nir_op_frcp:
                alu.op.special = BI_SPECIAL_FRCP;
                break;
        case nir_op_frsq:
                alu.op.special = BI_SPECIAL_FRSQ;
                break;
        BI_CASE_CMP(nir_op_flt)
        BI_CASE_CMP(nir_op_ilt)
        BI_CASE_CMP(nir_op_fge)
        BI_CASE_CMP(nir_op_ige)
        BI_CASE_CMP(nir_op_feq)
        BI_CASE_CMP(nir_op_ieq)
        BI_CASE_CMP(nir_op_fne)
        BI_CASE_CMP(nir_op_ine)
                alu.op.compare = bi_cond_for_nir(instr->op, false);
                break;
        case nir_op_fround_even:
                alu.op.round = BI_ROUND_MODE;
                alu.roundmode = BIFROST_RTE;
                break;
        case nir_op_fceil:
                alu.op.round = BI_ROUND_MODE;
                alu.roundmode = BIFROST_RTP;
                break;
        case nir_op_ffloor:
                alu.op.round = BI_ROUND_MODE;
                alu.roundmode = BIFROST_RTN;
                break;
        case nir_op_ftrunc:
                alu.op.round = BI_ROUND_MODE;
                alu.roundmode = BIFROST_RTZ;
                break;
        default:
                break;
        }

        if (alu.type == BI_CSEL) {
                /* Default to csel3 */
                alu.csel_cond = BI_COND_NE;
                alu.src[1] = BIR_INDEX_ZERO;
                alu.src_types[1] = alu.src_types[0];

                bi_fuse_csel_cond(&alu, instr->src[0],
                                &constants_left, &constant_shift);
        }

        bi_emit(ctx, alu);
}

static void
emit_instr(bi_context *ctx, struct nir_instr *instr)
{
        switch (instr->type) {
        case nir_instr_type_load_const:
                emit_load_const(ctx, nir_instr_as_load_const(instr));
                break;

        case nir_instr_type_intrinsic:
                emit_intrinsic(ctx, nir_instr_as_intrinsic(instr));
                break;

        case nir_instr_type_alu:
                emit_alu(ctx, nir_instr_as_alu(instr));
                break;

#if 0
        case nir_instr_type_tex:
                emit_tex(ctx, nir_instr_as_tex(instr));
                break;
#endif

        case nir_instr_type_jump:
                emit_jump(ctx, nir_instr_as_jump(instr));
                break;

        case nir_instr_type_ssa_undef:
                /* Spurious */
                break;

        default:
                //unreachable("Unhandled instruction type");
                break;
        }
}



static bi_block *
create_empty_block(bi_context *ctx)
{
        bi_block *blk = rzalloc(ctx, bi_block);

        blk->base.predecessors = _mesa_set_create(blk,
                        _mesa_hash_pointer,
                        _mesa_key_pointer_equal);

        blk->base.name = ctx->block_name_count++;

        return blk;
}

static void
bi_schedule_barrier(bi_context *ctx)
{
        bi_block *temp = ctx->after_block;
        ctx->after_block = create_empty_block(ctx);
        list_addtail(&ctx->after_block->base.link, &ctx->blocks);
        list_inithead(&ctx->after_block->base.instructions);
        pan_block_add_successor(&ctx->current_block->base, &ctx->after_block->base);
        ctx->current_block = ctx->after_block;
        ctx->after_block = temp;
}

static bi_block *
emit_block(bi_context *ctx, nir_block *block)
{
        if (ctx->after_block) {
                ctx->current_block = ctx->after_block;
                ctx->after_block = NULL;
        } else {
                ctx->current_block = create_empty_block(ctx);
        }

        list_addtail(&ctx->current_block->base.link, &ctx->blocks);
        list_inithead(&ctx->current_block->base.instructions);

        nir_foreach_instr(instr, block) {
                emit_instr(ctx, instr);
                ++ctx->instruction_count;
        }

        return ctx->current_block;
}

/* Emits an unconditional branch to the end of the current block, returning a
 * pointer so the user can fill in details */

static bi_instruction *
bi_emit_branch(bi_context *ctx)
{
        bi_instruction branch = {
                .type = BI_BRANCH,
                .branch = {
                        .cond = BI_COND_ALWAYS
                }
        };

        return bi_emit(ctx, branch);
}

/* Sets a condition for a branch by examing the NIR condition. If we're
 * familiar with the condition, we unwrap it to fold it into the branch
 * instruction. Otherwise, we consume the condition directly. We
 * generally use 1-bit booleans which allows us to use small types for
 * the conditions.
 */

static void
bi_set_branch_cond(bi_instruction *branch, nir_src *cond, bool invert)
{
        /* TODO: Try to unwrap instead of always bailing */
        branch->src[0] = bir_src_index(cond);
        branch->src[1] = BIR_INDEX_ZERO;
        branch->src_types[0] = branch->src_types[1] = nir_type_uint16;
        branch->branch.cond = invert ? BI_COND_EQ : BI_COND_NE;
}

static void
emit_if(bi_context *ctx, nir_if *nif)
{
        bi_block *before_block = ctx->current_block;

        /* Speculatively emit the branch, but we can't fill it in until later */
        bi_instruction *then_branch = bi_emit_branch(ctx);
        bi_set_branch_cond(then_branch, &nif->condition, true);

        /* Emit the two subblocks. */
        bi_block *then_block = emit_cf_list(ctx, &nif->then_list);
        bi_block *end_then_block = ctx->current_block;

        /* Emit a jump from the end of the then block to the end of the else */
        bi_instruction *then_exit = bi_emit_branch(ctx);

        /* Emit second block, and check if it's empty */

        int count_in = ctx->instruction_count;
        bi_block *else_block = emit_cf_list(ctx, &nif->else_list);
        bi_block *end_else_block = ctx->current_block;
        ctx->after_block = create_empty_block(ctx);

        /* Now that we have the subblocks emitted, fix up the branches */

        assert(then_block);
        assert(else_block);

        if (ctx->instruction_count == count_in) {
                /* The else block is empty, so don't emit an exit jump */
                bi_remove_instruction(then_exit);
                then_branch->branch.target = ctx->after_block;
        } else {
                then_branch->branch.target = else_block;
                then_exit->branch.target = ctx->after_block;
                pan_block_add_successor(&end_then_block->base, &then_exit->branch.target->base);
        }

        /* Wire up the successors */

        pan_block_add_successor(&before_block->base, &then_branch->branch.target->base); /* then_branch */

        pan_block_add_successor(&before_block->base, &then_block->base); /* fallthrough */
        pan_block_add_successor(&end_else_block->base, &ctx->after_block->base); /* fallthrough */
}

static void
emit_loop(bi_context *ctx, nir_loop *nloop)
{
        /* Remember where we are */
        bi_block *start_block = ctx->current_block;

        bi_block *saved_break = ctx->break_block;
        bi_block *saved_continue = ctx->continue_block;

        ctx->continue_block = create_empty_block(ctx);
        ctx->break_block = create_empty_block(ctx);
        ctx->after_block = ctx->continue_block;

        /* Emit the body itself */
        emit_cf_list(ctx, &nloop->body);

        /* Branch back to loop back */
        bi_instruction *br_back = bi_emit_branch(ctx);
        br_back->branch.target = ctx->continue_block;
        pan_block_add_successor(&start_block->base, &ctx->continue_block->base);
        pan_block_add_successor(&ctx->current_block->base, &ctx->continue_block->base);

        ctx->after_block = ctx->break_block;

        /* Pop off */
        ctx->break_block = saved_break;
        ctx->continue_block = saved_continue;
        ++ctx->loop_count;
}

static bi_block *
emit_cf_list(bi_context *ctx, struct exec_list *list)
{
        bi_block *start_block = NULL;

        foreach_list_typed(nir_cf_node, node, node, list) {
                switch (node->type) {
                case nir_cf_node_block: {
                        bi_block *block = emit_block(ctx, nir_cf_node_as_block(node));

                        if (!start_block)
                                start_block = block;

                        break;
                }

                case nir_cf_node_if:
                        emit_if(ctx, nir_cf_node_as_if(node));
                        break;

                case nir_cf_node_loop:
                        emit_loop(ctx, nir_cf_node_as_loop(node));
                        break;

                default:
                        unreachable("Unknown control flow");
                }
        }

        return start_block;
}

static int
glsl_type_size(const struct glsl_type *type, bool bindless)
{
        return glsl_count_attribute_slots(type, false);
}

static void
bi_optimize_nir(nir_shader *nir)
{
        bool progress;
        unsigned lower_flrp = 16 | 32 | 64;

        NIR_PASS(progress, nir, nir_lower_regs_to_ssa);
        NIR_PASS(progress, nir, nir_lower_idiv, nir_lower_idiv_fast);

        nir_lower_tex_options lower_tex_options = {
                .lower_txs_lod = true,
                .lower_txp = ~0,
                .lower_tex_without_implicit_lod = true,
                .lower_txd = true,
        };

        NIR_PASS(progress, nir, nir_lower_tex, &lower_tex_options);
        NIR_PASS(progress, nir, nir_lower_alu_to_scalar, NULL, NULL);
        NIR_PASS(progress, nir, nir_lower_load_const_to_scalar);

        do {
                progress = false;

                NIR_PASS(progress, nir, nir_lower_var_copies);
                NIR_PASS(progress, nir, nir_lower_vars_to_ssa);

                NIR_PASS(progress, nir, nir_copy_prop);
                NIR_PASS(progress, nir, nir_opt_remove_phis);
                NIR_PASS(progress, nir, nir_opt_dce);
                NIR_PASS(progress, nir, nir_opt_dead_cf);
                NIR_PASS(progress, nir, nir_opt_cse);
                NIR_PASS(progress, nir, nir_opt_peephole_select, 64, false, true);
                NIR_PASS(progress, nir, nir_opt_algebraic);
                NIR_PASS(progress, nir, nir_opt_constant_folding);

                if (lower_flrp != 0) {
                        bool lower_flrp_progress = false;
                        NIR_PASS(lower_flrp_progress,
                                 nir,
                                 nir_lower_flrp,
                                 lower_flrp,
                                 false /* always_precise */,
                                 nir->options->lower_ffma);
                        if (lower_flrp_progress) {
                                NIR_PASS(progress, nir,
                                         nir_opt_constant_folding);
                                progress = true;
                        }

                        /* Nothing should rematerialize any flrps, so we only
                         * need to do this lowering once.
                         */
                        lower_flrp = 0;
                }

                NIR_PASS(progress, nir, nir_opt_undef);
                NIR_PASS(progress, nir, nir_opt_loop_unroll,
                         nir_var_shader_in |
                         nir_var_shader_out |
                         nir_var_function_temp);
        } while (progress);

        NIR_PASS(progress, nir, nir_opt_algebraic_late);
        NIR_PASS(progress, nir, nir_lower_bool_to_int32);
        NIR_PASS(progress, nir, bifrost_nir_lower_algebraic_late);
        NIR_PASS(progress, nir, nir_lower_alu_to_scalar, NULL, NULL);
        NIR_PASS(progress, nir, nir_lower_load_const_to_scalar);

        /* Take us out of SSA */
        NIR_PASS(progress, nir, nir_lower_locals_to_regs);
        NIR_PASS(progress, nir, nir_move_vec_src_uses_to_dest);
        NIR_PASS(progress, nir, nir_convert_from_ssa, true);
}

void
bifrost_compile_shader_nir(nir_shader *nir, panfrost_program *program, unsigned product_id)
{
        bi_context *ctx = rzalloc(NULL, bi_context);
        ctx->nir = nir;
        ctx->stage = nir->info.stage;
        ctx->quirks = bifrost_get_quirks(product_id);
        list_inithead(&ctx->blocks);

        /* Lower gl_Position pre-optimisation, but after lowering vars to ssa
         * (so we don't accidentally duplicate the epilogue since mesa/st has
         * messed with our I/O quite a bit already) */

        NIR_PASS_V(nir, nir_lower_vars_to_ssa);

        if (ctx->stage == MESA_SHADER_VERTEX) {
                NIR_PASS_V(nir, nir_lower_viewport_transform);
                NIR_PASS_V(nir, nir_lower_point_size, 1.0, 1024.0);
        }

        NIR_PASS_V(nir, nir_split_var_copies);
        NIR_PASS_V(nir, nir_lower_global_vars_to_local);
        NIR_PASS_V(nir, nir_lower_var_copies);
        NIR_PASS_V(nir, nir_lower_vars_to_ssa);
        NIR_PASS_V(nir, nir_lower_io, nir_var_all, glsl_type_size, 0);
        NIR_PASS_V(nir, nir_lower_ssbo);

        bi_optimize_nir(nir);
        nir_print_shader(nir, stdout);

        panfrost_nir_assign_sysvals(&ctx->sysvals, nir);
        program->sysval_count = ctx->sysvals.sysval_count;
        memcpy(program->sysvals, ctx->sysvals.sysvals, sizeof(ctx->sysvals.sysvals[0]) * ctx->sysvals.sysval_count);

        nir_foreach_function(func, nir) {
                if (!func->impl)
                        continue;

                ctx->impl = func->impl;
                emit_cf_list(ctx, &func->impl->body);
                break; /* TODO: Multi-function shaders */
        }

        bi_foreach_block(ctx, _block) {
                bi_block *block = (bi_block *) _block;
                bi_lower_combine(ctx, block);
        }

        bool progress = false;

        do {
                progress = false;

                bi_foreach_block(ctx, _block) {
                        bi_block *block = (bi_block *) _block;
                        progress |= bi_opt_dead_code_eliminate(ctx, block);
                }
        } while(progress);

        bi_print_shader(ctx, stdout);
        bi_schedule(ctx);
        bi_register_allocate(ctx);
        bi_print_shader(ctx, stdout);
        bi_pack(ctx, &program->compiled);
        disassemble_bifrost(stdout, program->compiled.data, program->compiled.size, true);

        ralloc_free(ctx);
}
