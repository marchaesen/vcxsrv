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
#include "compiler/nir/nir_builder.h"
#include "util/u_debug.h"

#include "disassemble.h"
#include "bifrost_compile.h"
#include "bifrost_nir.h"
#include "compiler.h"
#include "bi_quirks.h"
#include "bi_print.h"

static const struct debug_named_value debug_options[] = {
        {"msgs",      BIFROST_DBG_MSGS,		"Print debug messages"},
        {"shaders",   BIFROST_DBG_SHADERS,	"Dump shaders in NIR and MIR"},
        DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(bifrost_debug, "BIFROST_MESA_DEBUG", debug_options, 0)

int bifrost_debug = 0;

#define DBG(fmt, ...) \
		do { if (bifrost_debug & BIFROST_DBG_MSGS) \
			fprintf(stderr, "%s:%d: "fmt, \
				__FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)

static bi_block *emit_cf_list(bi_context *ctx, struct exec_list *list);
static bi_instruction *bi_emit_branch(bi_context *ctx);

static void
emit_jump(bi_context *ctx, nir_jump_instr *instr)
{
        bi_instruction *branch = bi_emit_branch(ctx);

        switch (instr->type) {
        case nir_jump_break:
                branch->branch_target = ctx->break_block;
                break;
        case nir_jump_continue:
                branch->branch_target = ctx->continue_block;
                break;
        default:
                unreachable("Unhandled jump type");
        }

        pan_block_add_successor(&ctx->current_block->base, &branch->branch_target->base);
        ctx->current_block->base.unconditional_jumps = true;
}

static bi_instruction
bi_load(enum bi_class T, nir_intrinsic_instr *instr)
{
        bi_instruction load = {
                .type = T,
                .vector_channels = instr->num_components,
                .src = { BIR_INDEX_CONSTANT },
                .src_types = { nir_type_uint32 },
                .constant = { .u64 = nir_intrinsic_base(instr) },
        };

        const nir_intrinsic_info *info = &nir_intrinsic_infos[instr->intrinsic];

        if (info->has_dest)
                load.dest = pan_dest_index(&instr->dest);

        if (info->has_dest && nir_intrinsic_has_dest_type(instr))
                load.dest_type = nir_intrinsic_dest_type(instr);

        nir_src *offset = nir_get_io_offset_src(instr);

        if (nir_src_is_const(*offset))
                load.constant.u64 += nir_src_as_uint(*offset);
        else
                load.src[0] = pan_src_index(offset);

        return load;
}

static void
bi_emit_ld_output(bi_context *ctx, nir_intrinsic_instr *instr)
{
        assert(ctx->is_blend);

        bi_instruction ins = {
                .type = BI_LOAD_TILE,
                .vector_channels = instr->num_components,
                .dest = pan_dest_index(&instr->dest),
                .dest_type = nir_type_float16,
                .src = {
                        /* PixelIndices */
                        BIR_INDEX_CONSTANT,
                        /* PixelCoverage: we simply pass r60 which contains the cumulative
                         * coverage bitmap
                         */
                        BIR_INDEX_REGISTER | 60,
                        /* InternalConversionDescriptor (see src/panfrost/lib/midgard.xml for more
                         * details)
			 */
                        BIR_INDEX_CONSTANT | 32
                },
                .src_types = { nir_type_uint32, nir_type_uint32, nir_type_uint32 },
        };

        /* We want to load the current pixel.
         * FIXME: The sample to load is currently hardcoded to 0. This should
         * be addressed for multi-sample FBs.
         */
        struct bifrost_pixel_indices pix = {
                .y = BIFROST_CURRENT_PIXEL,
        };
        memcpy(&ins.constant.u64, &pix, sizeof(pix));

        /* Only keep the conversion part of the blend descriptor. */
        ins.constant.u64 |= ctx->blend_desc & 0xffffffff00000000ULL;

        bi_emit(ctx, ins);
}

static void
bi_emit_ld_vary(bi_context *ctx, nir_intrinsic_instr *instr)
{
        bi_instruction ins = bi_load(BI_LOAD_VAR, instr);
        ins.load_vary.interp_mode = BIFROST_INTERP_DEFAULT; /* TODO */
        ins.load_vary.reuse = false; /* TODO */
        ins.load_vary.flat = instr->intrinsic != nir_intrinsic_load_interpolated_input;
        ins.dest_type = nir_type_float | nir_dest_bit_size(instr->dest);
        ins.format = ins.dest_type;

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
bi_emit_ld_blend_input(bi_context *ctx, nir_intrinsic_instr *instr)
{
        ASSERTED nir_io_semantics sem = nir_intrinsic_io_semantics(instr);

        /* We don't support dual-source blending yet. */
        assert(sem.location == VARYING_SLOT_COL0);

        bi_instruction ins = {
                .type = BI_COMBINE,
                .dest_type = nir_type_uint32,
                .dest = pan_dest_index(&instr->dest),
                .src_types = {
                        nir_type_uint32, nir_type_uint32,
                        nir_type_uint32, nir_type_uint32,
                },

                /* Source color is passed through r0-r3.
                 * TODO: We should probably find a way to avoid this
                 * combine/mov and use r0-r3 directly.
                 */
                .src = {
                        BIR_INDEX_REGISTER | 0,
                        BIR_INDEX_REGISTER | 1,
                        BIR_INDEX_REGISTER | 2,
                        BIR_INDEX_REGISTER | 3,
                },
        };

        bi_emit(ctx, ins);
}

static void
bi_emit_frag_out(bi_context *ctx, nir_intrinsic_instr *instr)
{
        if (!ctx->emitted_atest && !ctx->is_blend) {
                bi_instruction ins = {
                        .type = BI_ATEST,
                        .src = {
                                BIR_INDEX_REGISTER | 60 /* TODO: RA */,
                                pan_src_index(&instr->src[0])
                        },
                        .src_types = {
                                nir_type_uint32,
                                nir_intrinsic_src_type(instr)
                        },
                        .swizzle = {
                                { 0 },
                                { 3, 0 } /* swizzle out the alpha */
                        },
                        .dest = BIR_INDEX_REGISTER | 60 /* TODO: RA */,
                        .dest_type = nir_type_uint32,
                };

                bi_emit(ctx, ins);
                ctx->emitted_atest = true;
        }

        bi_instruction blend = {
                .type = BI_BLEND,
                .blend_location = nir_intrinsic_base(instr),
                .src = {
                        pan_src_index(&instr->src[0]),
                        BIR_INDEX_REGISTER | 60 /* Can this be arbitrary? */,
                },
                .src_types = {
                        nir_intrinsic_src_type(instr),
                        nir_type_uint32,
                        nir_type_uint32,
                        nir_type_uint32,
                },
                .swizzle = {
                        { 0, 1, 2, 3 },
                        { 0 }
                },
                .dest_type = nir_type_uint32,
                .vector_channels = 4
        };

        if (ctx->is_blend) {
                /* Blend descriptor comes from the compile inputs */
                blend.src[2] = BIR_INDEX_CONSTANT | 0;
                blend.src[3] = BIR_INDEX_CONSTANT | 32;
                blend.constant.u64 = ctx->blend_desc;

                /* Put the result in r0 */
                blend.dest = BIR_INDEX_REGISTER | 0;
        } else {
                /* Blend descriptor comes from the FAU RAM */
                blend.src[2] = BIR_INDEX_BLEND | BIFROST_SRC_FAU_LO;
                blend.src[3] = BIR_INDEX_BLEND | BIFROST_SRC_FAU_HI;

                /* By convention, the return address is stored in r48 and will
                 * be used by the blend shader to jump back to the fragment
                 * shader when it's done.
                 */
                blend.dest = BIR_INDEX_REGISTER | 48;
        }

        assert(blend.blend_location < 8);
        assert(ctx->blend_types);
        assert(blend.src_types[0]);
        ctx->blend_types[blend.blend_location] = blend.src_types[0];

        bi_emit(ctx, blend);

        if (ctx->is_blend) {
                /* Jump back to the fragment shader, return address is stored
                 * in r48 (see above).
                 */
                bi_instruction *ret = bi_emit_branch(ctx);
                ret->src[2] = BIR_INDEX_REGISTER | 48;
        }
}

static bi_instruction
bi_load_with_r61(enum bi_class T, nir_intrinsic_instr *instr)
{
        bi_instruction ld = bi_load(T, instr);
        ld.src[1] = BIR_INDEX_REGISTER | 61; /* TODO: RA */
        ld.src[2] = BIR_INDEX_REGISTER | 62;
        ld.src_types[1] = nir_type_uint32;
        ld.src_types[2] = nir_type_uint32;
        ld.format = instr->intrinsic == nir_intrinsic_store_output ?
                nir_intrinsic_src_type(instr) :
                nir_intrinsic_dest_type(instr);
        return ld;
}

static void
bi_emit_st_vary(bi_context *ctx, nir_intrinsic_instr *instr)
{
        bi_instruction address = bi_load_with_r61(BI_LOAD_VAR_ADDRESS, instr);
        address.dest = bi_make_temp(ctx);
        address.dest_type = nir_type_uint32;
        address.vector_channels = 3;

        unsigned nr = nir_intrinsic_src_components(instr, 0);
        assert(nir_intrinsic_write_mask(instr) == ((1 << nr) - 1));

        bi_instruction st = {
                .type = BI_STORE_VAR,
                .src = {
                        pan_src_index(&instr->src[0]),
                        address.dest, address.dest, address.dest,
                },
                .src_types = {
                        nir_type_uint32,
                        nir_type_uint32, nir_type_uint32, nir_type_uint32,
                },
                .swizzle = {
                        { 0 },
                        { 0 }, { 1 }, { 2}
                },
                .vector_channels = nr,
        };

        for (unsigned i = 0; i < nr; ++i)
                st.swizzle[0][i] = i;

        bi_emit(ctx, address);
        bi_emit(ctx, st);
}

static void
bi_emit_ld_uniform(bi_context *ctx, nir_intrinsic_instr *instr)
{
        bi_instruction ld = bi_load(BI_LOAD_UNIFORM, instr);
        ld.src[1] = BIR_INDEX_ZERO; /* TODO: UBO index */
        ld.segment = BI_SEGMENT_UBO;

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
                .segment = BI_SEGMENT_UBO,
                .vector_channels = nr_components,
                .src = { BIR_INDEX_CONSTANT, BIR_INDEX_ZERO },
                .src_types = { nir_type_uint32, nir_type_uint32 },
                .constant = { (uniform * 16) + offset },
                .dest = pan_dest_index(&nir_dest),
                .dest_type = nir_type_uint32, /* TODO */
        };

        bi_emit(ctx, load);
}

/* gl_FragCoord.xy = u16_to_f32(R59.xy) + 0.5
 * gl_FragCoord.z = ld_vary(fragz)
 * gl_FragCoord.w = ld_vary(fragw)
 */

static void
bi_emit_ld_frag_coord(bi_context *ctx, nir_intrinsic_instr *instr)
{
        /* Future proofing for mediump fragcoord at some point.. */
        nir_alu_type T = nir_type_float32;

        /* First, sketch a combine */
        bi_instruction combine = {
                .type = BI_COMBINE,
                .dest_type = nir_type_uint32,
                .dest = pan_dest_index(&instr->dest),
                .src_types = { T, T, T, T },
        };

        /* Second, handle xy */
        for (unsigned i = 0; i < 2; ++i) {
                bi_instruction conv = {
                        .type = BI_CONVERT,
                        .dest_type = T,
                        .dest = bi_make_temp(ctx),
                        .src = {
                                /* TODO: RA XXX */
                                BIR_INDEX_REGISTER | 59
                        },
                        .src_types = { nir_type_uint16 },
                        .swizzle = { { i } }
                };

                bi_instruction add = {
                        .type = BI_ADD,
                        .dest_type = T,
                        .dest = bi_make_temp(ctx),
                        .src = { conv.dest, BIR_INDEX_CONSTANT },
                        .src_types = { T, T },
                };

                float half = 0.5;
                memcpy(&add.constant.u32, &half, sizeof(float));

                bi_emit(ctx, conv);
                bi_emit(ctx, add);

                combine.src[i] = add.dest;
        }

        /* Third, zw */
        for (unsigned i = 0; i < 2; ++i) {
                bi_instruction load = {
                        .type = BI_LOAD_VAR,
                        .load_vary = {
                                .interp_mode = BIFROST_INTERP_DEFAULT,
                                .reuse = false,
                                .flat = true
                        },
                        .vector_channels = 1,
                        .dest_type = nir_type_float32,
                        .format = nir_type_float32,
                        .dest = bi_make_temp(ctx),
                        .src = {
                                BIR_INDEX_CONSTANT,
                                BIR_INDEX_PASS | BIFROST_SRC_FAU_LO
                        },
                        .src_types = { nir_type_uint32, nir_type_uint32 },
                        .constant = {
                                .u32 = (i == 0) ? BIFROST_FRAGZ : BIFROST_FRAGW
                        }
                };

                bi_emit(ctx, load);

                combine.src[i + 2] = load.dest;
        }

        /* Finally, emit the combine */
        bi_emit(ctx, combine);
}

static void
bi_emit_discard(bi_context *ctx, nir_intrinsic_instr *instr)
{
        /* Goofy lowering */
        bi_instruction discard = {
                .type = BI_DISCARD,
                .cond = BI_COND_EQ,
                .src_types = { nir_type_uint32, nir_type_uint32 },
                .src = { BIR_INDEX_ZERO, BIR_INDEX_ZERO },
        };

        bi_emit(ctx, discard);
}

static void
bi_fuse_cond(bi_instruction *csel, nir_alu_src cond,
                unsigned *constants_left, unsigned *constant_shift,
                unsigned comps, bool float_only);

static void
bi_emit_discard_if(bi_context *ctx, nir_intrinsic_instr *instr)
{
        nir_src cond = instr->src[0];
        nir_alu_type T = nir_type_uint | nir_src_bit_size(cond);

        bi_instruction discard = {
                .type = BI_DISCARD,
                .cond = BI_COND_NE,
                .src_types = { T, T },
                .src = {
                        pan_src_index(&cond),
                        BIR_INDEX_ZERO
                },
        };

        /* Try to fuse in the condition */
        unsigned constants_left = 1, constant_shift = 0;

        /* Scalar so no swizzle */
        nir_alu_src wrap = {
                .src = instr->src[0]
        };

        /* May or may not succeed but we're optimistic */
        bi_fuse_cond(&discard, wrap, &constants_left, &constant_shift, 1, true);

        bi_emit(ctx, discard);
}

static void
bi_emit_blend_const(bi_context *ctx, nir_intrinsic_instr *instr)
{
        assert(ctx->is_blend);

        unsigned comp;
        switch (instr->intrinsic) {
        case nir_intrinsic_load_blend_const_color_r_float: comp = 0; break;
        case nir_intrinsic_load_blend_const_color_g_float: comp = 1; break;
        case nir_intrinsic_load_blend_const_color_b_float: comp = 2; break;
        case nir_intrinsic_load_blend_const_color_a_float: comp = 3; break;
        default: unreachable("Invalid load blend constant intrinsic");
        }

        bi_instruction move = {
                .type = BI_MOV,
                .dest = pan_dest_index(&instr->dest),
                .dest_type = nir_type_uint32,
                .src = { BIR_INDEX_CONSTANT },
                .src_types = { nir_type_uint32 },
        };

        memcpy(&move.constant.u32, &ctx->blend_constants[comp], sizeof(float));

        bi_emit(ctx, move);
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
                if (ctx->is_blend)
                        bi_emit_ld_blend_input(ctx, instr);
                else if (ctx->stage == MESA_SHADER_FRAGMENT)
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

        case nir_intrinsic_load_frag_coord:
                bi_emit_ld_frag_coord(ctx, instr);
                break;

        case nir_intrinsic_discard:
                bi_emit_discard(ctx, instr);
                break;

        case nir_intrinsic_discard_if:
                bi_emit_discard_if(ctx, instr);
                break;

        case nir_intrinsic_load_ssbo_address:
                bi_emit_sysval(ctx, &instr->instr, 1, 0);
                break;

        case nir_intrinsic_get_ssbo_size:
                bi_emit_sysval(ctx, &instr->instr, 1, 8);
                break;

        case nir_intrinsic_load_output:
                bi_emit_ld_output(ctx, instr);
                break;

        case nir_intrinsic_load_viewport_scale:
        case nir_intrinsic_load_viewport_offset:
        case nir_intrinsic_load_num_work_groups:
        case nir_intrinsic_load_sampler_lod_parameters_pan:
                bi_emit_sysval(ctx, &instr->instr, 3, 0);
                break;

        case nir_intrinsic_load_blend_const_color_r_float:
        case nir_intrinsic_load_blend_const_color_g_float:
        case nir_intrinsic_load_blend_const_color_b_float:
        case nir_intrinsic_load_blend_const_color_a_float:
                bi_emit_blend_const(ctx, instr);
                break;

        default:
                unreachable("Unknown intrinsic");
                break;
        }
}

static void
emit_load_const(bi_context *ctx, nir_load_const_instr *instr)
{
        /* Make sure we've been lowered */
        assert(instr->def.num_components <= (32 / instr->def.bit_size));

        /* Accumulate all the channels of the constant, as if we did an
         * implicit SEL over them */
        uint32_t acc = 0;

        for (unsigned i = 0; i < instr->def.num_components; ++i) {
                unsigned v = nir_const_value_as_uint(instr->value[i], instr->def.bit_size);
                acc |= (v << (i * instr->def.bit_size));
        }

        bi_instruction move = {
                .type = BI_MOV,
                .dest = pan_ssa_index(&instr->def),
                .dest_type = nir_type_uint32,
                .src = {
                        BIR_INDEX_CONSTANT
                },
                .src_types = {
                        nir_type_uint32,
                },
                .constant = {
                        .u32 = acc
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
        case nir_op_fadd:
        case nir_op_fsub:
                return BI_ADD;

        case nir_op_iadd:
        case nir_op_isub:
                return BI_IMATH;

        case nir_op_imul:
                return BI_IMUL;

        case nir_op_iand:
        case nir_op_ior:
        case nir_op_ixor:
        case nir_op_inot:
        case nir_op_ishl:
                return BI_BITWISE;

        BI_CASE_CMP(nir_op_flt)
        BI_CASE_CMP(nir_op_fge)
        BI_CASE_CMP(nir_op_feq)
        BI_CASE_CMP(nir_op_fneu)
        BI_CASE_CMP(nir_op_ilt)
        BI_CASE_CMP(nir_op_ige)
        BI_CASE_CMP(nir_op_ieq)
        BI_CASE_CMP(nir_op_ine)
        BI_CASE_CMP(nir_op_uge)
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
        case nir_op_iabs:
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
        BI_CASE_CMP(nir_op_uge)
                return BI_COND_GE;

        BI_CASE_CMP(nir_op_feq)
        BI_CASE_CMP(nir_op_ieq)
                return BI_COND_EQ;

        BI_CASE_CMP(nir_op_fneu)
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
                unsigned *constants_left, unsigned *constant_shift, unsigned comps)
{
        unsigned bits = nir_src_bit_size(instr->src[i].src);
        unsigned dest_bits = nir_dest_bit_size(instr->dest.dest);

        alu->src_types[to] = nir_op_infos[instr->op].input_types[i]
                | bits;

        /* Try to inline a constant */
        if (nir_src_is_const(instr->src[i].src) && *constants_left && (dest_bits == bits)) {
                uint64_t mask = (1ull << dest_bits) - 1;
                uint64_t cons = nir_src_as_uint(instr->src[i].src);

                /* Try to reuse a constant */
                for (unsigned i = 0; i < (*constant_shift); i += dest_bits) {
                        if (((alu->constant.u64 >> i) & mask) == cons) {
                                alu->src[to] = BIR_INDEX_CONSTANT | i;
                                return;
                        }
                }

                alu->constant.u64 |= cons << *constant_shift;
                alu->src[to] = BIR_INDEX_CONSTANT | (*constant_shift);
                --(*constants_left);
                (*constant_shift) += MAX2(dest_bits, 32); /* lo/hi */
                return;
        }

        alu->src[to] = pan_src_index(&instr->src[i].src);

        /* Copy swizzle for all vectored components, replicating last component
         * to fill undersized */

        unsigned vec = alu->type == BI_COMBINE ? 1 :
                MAX2(1, 32 / dest_bits);

        for (unsigned j = 0; j < vec; ++j)
                alu->swizzle[to][j] = instr->src[i].swizzle[MIN2(j, comps - 1)];
}

static void
bi_fuse_cond(bi_instruction *csel, nir_alu_src cond,
                unsigned *constants_left, unsigned *constant_shift,
                unsigned comps, bool float_only)
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

        /* Some instructions can't compare ints */
        if (float_only) {
                nir_alu_type T = nir_op_infos[alu->op].input_types[0];
                T = nir_alu_type_get_base_type(T);

                if (T != nir_type_float)
                        return;
        }

        /* We found one, let's fuse it in */
        csel->cond = bcond;
        bi_copy_src(csel, alu, 0, 0, constants_left, constant_shift, comps);
        bi_copy_src(csel, alu, 1, 1, constants_left, constant_shift, comps);
}

static void
emit_alu(bi_context *ctx, nir_alu_instr *instr)
{
        /* Try some special functions */
        switch (instr->op) {
        case nir_op_fexp2:
                bi_emit_fexp2(ctx, instr);
                return;
        case nir_op_flog2:
                bi_emit_flog2(ctx, instr);
                return;
        default:
                break;
        }

        /* Otherwise, assume it's something we can handle normally */
        bi_instruction alu = {
                .type = bi_class_for_nir_alu(instr->op),
                .dest = pan_dest_index(&instr->dest.dest),
                .dest_type = nir_op_infos[instr->op].output_type
                        | nir_dest_bit_size(instr->dest.dest),
        };

        /* TODO: Implement lowering of special functions for older Bifrost */
        assert((alu.type != BI_SPECIAL) || !(ctx->quirks & BIFROST_NO_FAST_OP));

        unsigned comps = nir_dest_num_components(instr->dest.dest);
        bool vector = comps > MAX2(1, 32 / nir_dest_bit_size(instr->dest.dest));
        assert(!vector || alu.type == BI_COMBINE || alu.type == BI_MOV);

        if (!instr->dest.dest.is_ssa) {
                for (unsigned i = 0; i < comps; ++i)
                        assert(instr->dest.write_mask);
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

                bi_copy_src(&alu, instr, i, i + f, &constants_left, &constant_shift, comps);
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
        case nir_op_iadd:
                alu.op.imath = BI_IMATH_ADD;
                /* Carry */
                alu.src[2] = BIR_INDEX_ZERO;
                break;
        case nir_op_isub:
                alu.op.imath = BI_IMATH_SUB;
                /* Borrow */
                alu.src[2] = BIR_INDEX_ZERO;
                break;
        case nir_op_iabs:
                alu.op.special = BI_SPECIAL_IABS;
                break;
        case nir_op_inot:
                /* no dedicated bitwise not, but we can invert sources. convert to ~(a | 0) */
                alu.op.bitwise = BI_BITWISE_OR;
                alu.bitwise.dest_invert = true;
                alu.src[1] = BIR_INDEX_ZERO;
                /* zero shift */
                alu.src[2] = BIR_INDEX_ZERO;
                alu.src_types[2] = nir_type_uint8;
                break;
        case nir_op_ishl:
                alu.op.bitwise = BI_BITWISE_OR;
                /* move src1 to src2 and replace with zero. underlying op is (src0 << src2) | src1 */
                alu.src[2] = alu.src[1];
                alu.src_types[2] = nir_type_uint8;
                alu.src[1] = BIR_INDEX_ZERO;
                break;
        case nir_op_imul:
                alu.op.imul = BI_IMUL_IMUL;
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
        BI_CASE_CMP(nir_op_fneu)
        BI_CASE_CMP(nir_op_ine)
        BI_CASE_CMP(nir_op_uge)
                alu.cond = bi_cond_for_nir(instr->op, false);
                break;
        case nir_op_fround_even:
                alu.roundmode = BIFROST_RTE;
                break;
        case nir_op_fceil:
                alu.roundmode = BIFROST_RTP;
                break;
        case nir_op_ffloor:
                alu.roundmode = BIFROST_RTN;
                break;
        case nir_op_ftrunc:
                alu.roundmode = BIFROST_RTZ;
                break;
        case nir_op_iand:
                alu.op.bitwise = BI_BITWISE_AND;
                /* zero shift */
                alu.src[2] = BIR_INDEX_ZERO;
                alu.src_types[2] = nir_type_uint8;
                break;
        case nir_op_ior:
                alu.op.bitwise = BI_BITWISE_OR;
                /* zero shift */
                alu.src[2] = BIR_INDEX_ZERO;
                alu.src_types[2] = nir_type_uint8;
                break;
        case nir_op_ixor:
                alu.op.bitwise = BI_BITWISE_XOR;
                /* zero shift */
                alu.src[2] = BIR_INDEX_ZERO;
                alu.src_types[2] = nir_type_uint8;
                break;
        case nir_op_f2i32:
                alu.roundmode = BIFROST_RTZ;
                break;

        case nir_op_f2f16:
        case nir_op_i2i16:
        case nir_op_u2u16: {
                if (nir_src_bit_size(instr->src[0].src) != 32)
                        break;

                /* Should have been const folded */
                assert(!nir_src_is_const(instr->src[0].src));

                alu.src_types[1] = alu.src_types[0];
                alu.src[1] = alu.src[0];

                unsigned last = nir_dest_num_components(instr->dest.dest) - 1;
                assert(last <= 1);

                alu.swizzle[1][0] = instr->src[0].swizzle[last];
                break;
        }

        default:
                break;
        }

        if (alu.type == BI_MOV && vector) {
                alu.type = BI_COMBINE;

                for (unsigned i = 0; i < comps; ++i) {
                        alu.src[i] = alu.src[0];
                        alu.swizzle[i][0] = instr->src[0].swizzle[i];
                }
        }

        if (alu.type == BI_CSEL) {
                /* Default to csel3 */
                alu.cond = BI_COND_NE;
                alu.src[1] = BIR_INDEX_ZERO;
                alu.src_types[1] = alu.src_types[0];

                /* TODO: Reenable cond fusing when we can split up registers
                 * when scheduling */
#if 0
                bi_fuse_cond(&alu, instr->src[0],
                                &constants_left, &constant_shift, comps, false);
#endif
        }

        bi_emit(ctx, alu);
}

/* TEXS instructions assume normal 2D f32 operation but are more
 * space-efficient and with simpler RA/scheduling requirements*/

static void
emit_texs(bi_context *ctx, nir_tex_instr *instr)
{
        bi_instruction tex = {
                .type = BI_TEXS,
                .texture = {
                        .texture_index = instr->texture_index,
                        .sampler_index = instr->sampler_index,
                        .compute_lod = instr->op == nir_texop_tex,
                },
                .dest = pan_dest_index(&instr->dest),
                .dest_type = instr->dest_type,
                .src_types = { nir_type_float32, nir_type_float32 },
                .vector_channels = 4
        };

        for (unsigned i = 0; i < instr->num_srcs; ++i) {
                int index = pan_src_index(&instr->src[i].src);

                /* We were checked ahead-of-time */
                if (instr->src[i].src_type == nir_tex_src_lod)
                        continue;

                assert (instr->src[i].src_type == nir_tex_src_coord);

                tex.src[0] = index;
                tex.src[1] = index;
                tex.swizzle[0][0] = 0;
                tex.swizzle[1][0] = 1;
        }

        bi_emit(ctx, tex);
}

/* Returns dimension with 0 special casing cubemaps. Shamelessly copied from Midgard */
static unsigned
bifrost_tex_format(enum glsl_sampler_dim dim)
{
        switch (dim) {
        case GLSL_SAMPLER_DIM_1D:
        case GLSL_SAMPLER_DIM_BUF:
                return 1;

        case GLSL_SAMPLER_DIM_2D:
        case GLSL_SAMPLER_DIM_MS:
        case GLSL_SAMPLER_DIM_EXTERNAL:
        case GLSL_SAMPLER_DIM_RECT:
                return 2;

        case GLSL_SAMPLER_DIM_3D:
                return 3;

        case GLSL_SAMPLER_DIM_CUBE:
                return 0;

        default:
                DBG("Unknown sampler dim type\n");
                assert(0);
                return 0;
        }
}

static enum bifrost_texture_format_full
bi_texture_format(nir_alu_type T, enum bifrost_outmod outmod)
{
        switch (T) {
        case nir_type_float16: return BIFROST_TEXTURE_FORMAT_F16 + outmod;
        case nir_type_float32: return BIFROST_TEXTURE_FORMAT_F32 + outmod;
        case nir_type_uint16:  return BIFROST_TEXTURE_FORMAT_U16;
        case nir_type_int16:   return BIFROST_TEXTURE_FORMAT_S16;
        case nir_type_uint32:  return BIFROST_TEXTURE_FORMAT_U32;
        case nir_type_int32:   return BIFROST_TEXTURE_FORMAT_S32;
        default:              unreachable("Invalid type for texturing");
        }
}

/* TEXC's explicit and bias LOD modes requires the LOD to be transformed to a
 * 16-bit 8:8 fixed-point format. We lower as:
 *
 * F32_TO_S32(clamp(x, -16.0, +16.0) * 256.0) & 0xFFFF =
 * MKVEC(F32_TO_S32(clamp(x * 1.0/16.0, -1.0, 1.0) * (16.0 * 256.0)), #0)
 */

static unsigned
bi_emit_lod_88(bi_context *ctx, unsigned lod, bool fp16)
{
        nir_alu_type T = fp16 ? nir_type_float16 : nir_type_float32;

        /* Sort of arbitrary. Must be less than 128.0, greater than or equal to
         * the max LOD (16 since we cap at 2^16 texture dimensions), and
         * preferably small to minimize precision loss */
        const float max_lod = 16.0;

        /* FMA.f16/f32.sat_signed, saturated, lod, #1.0/max_lod, #0 */
        bi_instruction fsat = {
                .type = BI_FMA,
                .dest = bi_make_temp(ctx),
                .dest_type = nir_type_float32,
                .src = { lod, BIR_INDEX_CONSTANT, BIR_INDEX_ZERO },
                .src_types = { T, nir_type_float32, nir_type_float32 },
                .outmod = BIFROST_SAT_SIGNED,
                .roundmode = BIFROST_RTE,
                .constant = {
                        .u64 = fui(1.0 / max_lod)
                },
        };

        /* FMA.f32 scaled, saturated, lod, #(max_lod * 256.0), #0 */
        bi_instruction fmul = {
                .type = BI_FMA,
                .dest = bi_make_temp(ctx),
                .dest_type = T,
                .src = { fsat.dest, BIR_INDEX_CONSTANT, BIR_INDEX_ZERO },
                .src_types = { nir_type_float32, nir_type_float32, nir_type_float32 },
                .roundmode = BIFROST_RTE,
                .constant = {
                        .u64 = fui(max_lod * 256.0)
                },
        };

        /* F32_TO_S32 s32, scaled */
        bi_instruction f2i = {
                .type = BI_CONVERT,
                .dest = bi_make_temp(ctx),
                .dest_type = nir_type_int32,
                .src = { fmul.dest },
                .src_types = { T },
                .roundmode = BIFROST_RTZ
        };

        /* MKVEC.v2i16 s32.h0, #0 */
        bi_instruction mkvec = {
                .type = BI_SELECT,
                .dest = bi_make_temp(ctx),
                .dest_type = nir_type_int16,
                .src = { f2i.dest, BIR_INDEX_ZERO },
                .src_types = { nir_type_int16, nir_type_int16 },
        };

        bi_emit(ctx, fsat);
        bi_emit(ctx, fmul);
        bi_emit(ctx, f2i);
        bi_emit(ctx, mkvec);

        return mkvec.dest;
}

/* FETCH takes a 32-bit staging register containing the LOD as an integer in
 * the bottom 16-bits and (if present) the cube face index in the top 16-bits.
 * TODO: Cube face.
 */

static unsigned
bi_emit_lod_cube(bi_context *ctx, unsigned lod)
{
        /* MKVEC.v2i16 out, lod.h0, #0 */
        bi_instruction mkvec = {
                .type = BI_SELECT,
                .dest = bi_make_temp(ctx),
                .dest_type = nir_type_int16,
                .src = { lod, BIR_INDEX_ZERO },
                .src_types = { nir_type_int16, nir_type_int16 },
        };

        bi_emit(ctx, mkvec);

        return mkvec.dest;
}

/* Map to the main texture op used. Some of these (txd in particular) will
 * lower to multiple texture ops with different opcodes (GRDESC_DER + TEX in
 * sequence). We assume that lowering is handled elsewhere.
 */

static enum bifrost_tex_op
bi_tex_op(nir_texop op)
{
        switch (op) {
        case nir_texop_tex:
        case nir_texop_txb:
        case nir_texop_txl:
        case nir_texop_txd:
        case nir_texop_tex_prefetch:
                return BIFROST_TEX_OP_TEX;
        case nir_texop_txf:
        case nir_texop_txf_ms:
        case nir_texop_txf_ms_fb:
        case nir_texop_txf_ms_mcs:
        case nir_texop_tg4:
                return BIFROST_TEX_OP_FETCH;
        case nir_texop_txs:
        case nir_texop_lod:
        case nir_texop_query_levels:
        case nir_texop_texture_samples:
        case nir_texop_samples_identical:
                unreachable("should've been lowered");
        default:
                unreachable("unsupported tex op");
        }
}

/* Data registers required by texturing in the order they appear. All are
 * optional, the texture operation descriptor determines which are present.
 * Note since 3D arrays are not permitted at an API level, Z_COORD and
 * ARRAY/SHADOW are exlusive, so TEXC in practice reads at most 8 registers */

enum bifrost_tex_dreg {
        BIFROST_TEX_DREG_Z_COORD = 0,
        BIFROST_TEX_DREG_Y_DELTAS = 1,
        BIFROST_TEX_DREG_LOD = 2,
        BIFROST_TEX_DREG_GRDESC_HI = 3,
        BIFROST_TEX_DREG_SHADOW = 4,
        BIFROST_TEX_DREG_ARRAY = 5,
        BIFROST_TEX_DREG_OFFSETMS = 6,
        BIFROST_TEX_DREG_SAMPLER = 7,
        BIFROST_TEX_DREG_TEXTURE = 8,
        BIFROST_TEX_DREG_COUNT,
};

static void
emit_texc(bi_context *ctx, nir_tex_instr *instr)
{
        /* TODO: support more with other encodings */
        assert(instr->sampler_index < 16);

        /* TODO: support more ops */
        switch (instr->op) {
        case nir_texop_tex:
        case nir_texop_txl:
        case nir_texop_txb:
        case nir_texop_txf:
                break;
        default:
                unreachable("Unsupported texture op");
        }

        bi_instruction tex = {
                .type = BI_TEXC,
                .dest = pan_dest_index(&instr->dest),
                .dest_type = instr->dest_type,
                .src_types = {
                        /* Staging registers */
                        nir_type_uint32,
                        nir_type_float32, nir_type_float32,
                        nir_type_uint32
                },
                .vector_channels = 4
        };

        struct bifrost_texture_operation desc = {
                .sampler_index_or_mode = instr->sampler_index,
                .index = instr->texture_index,
                .immediate_indices = 1, /* TODO */
                .op = bi_tex_op(instr->op),
                .offset_or_bias_disable = false, /* TODO */
                .shadow_or_clamp_disable = instr->is_shadow,
                .array = false, /* TODO */
                .dimension = bifrost_tex_format(instr->sampler_dim),
                .format = bi_texture_format(instr->dest_type, BIFROST_NONE), /* TODO */
                .mask = (1 << tex.vector_channels) - 1
        };

        switch (desc.op) {
        case BIFROST_TEX_OP_TEX:
                desc.lod_or_fetch = BIFROST_LOD_MODE_COMPUTE;
                break;
        case BIFROST_TEX_OP_FETCH:
                /* TODO: gathers */
                desc.lod_or_fetch = BIFROST_TEXTURE_FETCH_TEXEL;
                break;
        default:
                unreachable("texture op unsupported");
        }

        /* 32-bit indices to be allocated as consecutive data registers. */
        unsigned dregs[BIFROST_TEX_DREG_COUNT] = { 0 };

        for (unsigned i = 0; i < instr->num_srcs; ++i) {
                unsigned index = pan_src_index(&instr->src[i].src);
                unsigned sz = nir_src_bit_size(instr->src[i].src);
                ASSERTED nir_alu_type base = nir_tex_instr_src_type(instr, i);

                switch (instr->src[i].src_type) {
                case nir_tex_src_coord:
                        /* TODO: cube map descriptor */
                        tex.src[1] = index;
                        tex.src[2] = index;
                        tex.swizzle[1][0] = 0;
                        tex.swizzle[2][0] = 1;
                        break;

                case nir_tex_src_lod:
                        if (nir_src_is_const(instr->src[i].src) && nir_src_as_uint(instr->src[i].src) == 0) {
                                desc.lod_or_fetch = BIFROST_LOD_MODE_ZERO;
                        } else if (desc.op == BIFROST_TEX_OP_TEX) {
                                assert(base == nir_type_float);

                                assert(sz == 16 || sz == 32);
                                dregs[BIFROST_TEX_DREG_LOD] =
                                        bi_emit_lod_88(ctx, index, sz == 16);
                                desc.lod_or_fetch = BIFROST_LOD_MODE_EXPLICIT;
                        } else {
                                assert(desc.op == BIFROST_TEX_OP_FETCH);
                                assert(base == nir_type_uint || base == nir_type_int);
                                assert(sz == 16 || sz == 32);

                                dregs[BIFROST_TEX_DREG_LOD] =
                                        bi_emit_lod_cube(ctx, index);
                        }

                        break;

                case nir_tex_src_bias:
                        /* Upper 16-bits interpreted as a clamp, leave zero */
                        assert(desc.op == BIFROST_TEX_OP_TEX);
                        assert(base == nir_type_float);
                        assert(sz == 16 || sz == 32);
                        dregs[BIFROST_TEX_DREG_LOD] =
                                bi_emit_lod_88(ctx, index, sz == 16);
                        desc.lod_or_fetch = BIFROST_LOD_MODE_BIAS;
                        break;

                default:
                        unreachable("Unhandled src type in texc emit");
                }
        }

        /* Allocate data registers contiguously */
        bi_instruction combine = {
                .type = BI_COMBINE,
                .dest_type = nir_type_uint32,
                .dest = bi_make_temp(ctx),
                .src_types = {
                        nir_type_uint32, nir_type_uint32,
                        nir_type_uint32, nir_type_uint32,
                },
        };

        unsigned dreg_index = 0;

        for (unsigned i = 0; i < ARRAY_SIZE(dregs); ++i) {
                assert(dreg_index < 4);

                if (dregs[i])
                        combine.src[dreg_index++] = dregs[i];
        }

        /* Pass combined data registers together */
        if (dreg_index > 0) {
                tex.src[0] = combine.dest;
                bi_emit(ctx, combine);

                for (unsigned i = 0; i < dreg_index; ++i)
                        tex.swizzle[0][i] = i;
        }

        /* Pass the texture operation descriptor in src2 */
        tex.src[3] = BIR_INDEX_CONSTANT;
        memcpy(&tex.constant.u64, &desc, sizeof(desc));

        bi_emit(ctx, tex);
}

/* Simple textures ops correspond to NIR tex or txl with LOD = 0. Anything else
 * needs a complete texture op. */

static bool
bi_is_normal_tex(gl_shader_stage stage, nir_tex_instr *instr)
{
        if (instr->op == nir_texop_tex)
                return true;

        if (instr->op != nir_texop_txl)
                return false;

        int lod_idx = nir_tex_instr_src_index(instr, nir_tex_src_lod);
        if (lod_idx < 0)
                return true;

        nir_src lod = instr->src[lod_idx].src;
        return nir_src_is_const(lod) && nir_src_as_uint(lod) == 0;
}

static void
emit_tex(bi_context *ctx, nir_tex_instr *instr)
{
        nir_alu_type base = nir_alu_type_get_base_type(instr->dest_type);
        unsigned sz =  nir_dest_bit_size(instr->dest);
        instr->dest_type = base | sz;

        bool is_normal = bi_is_normal_tex(ctx->stage, instr);
        bool is_2d = instr->sampler_dim == GLSL_SAMPLER_DIM_2D ||
                instr->sampler_dim == GLSL_SAMPLER_DIM_EXTERNAL;
        bool is_f = base == nir_type_float && (sz == 16 || sz == 32);

        if (is_normal && is_2d && is_f && !instr->is_shadow)
                emit_texs(ctx, instr);
        else
                emit_texc(ctx, instr);
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

        case nir_instr_type_tex:
                emit_tex(ctx, nir_instr_as_tex(instr));
                break;

        case nir_instr_type_jump:
                emit_jump(ctx, nir_instr_as_jump(instr));
                break;

        case nir_instr_type_ssa_undef:
                /* Spurious */
                break;

        default:
                unreachable("Unhandled instruction type");
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

        return blk;
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
                .cond = BI_COND_ALWAYS
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
        branch->src[0] = pan_src_index(cond);
        branch->src[1] = BIR_INDEX_ZERO;
        branch->src_types[0] = branch->src_types[1] = nir_type_uint |
                nir_src_bit_size(*cond);
        branch->cond = invert ? BI_COND_EQ : BI_COND_NE;
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
                then_branch->branch_target = ctx->after_block;
                pan_block_add_successor(&end_then_block->base, &ctx->after_block->base); /* fallthrough */
        } else {
                then_branch->branch_target = else_block;
                then_exit->branch_target = ctx->after_block;
                pan_block_add_successor(&end_then_block->base, &then_exit->branch_target->base);
                pan_block_add_successor(&end_else_block->base, &ctx->after_block->base); /* fallthrough */
        }

        pan_block_add_successor(&before_block->base, &then_branch->branch_target->base); /* then_branch */
        pan_block_add_successor(&before_block->base, &then_block->base); /* fallthrough */
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
        br_back->branch_target = ctx->continue_block;
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
                                 false /* always_precise */);
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
bifrost_compile_shader_nir(nir_shader *nir, panfrost_program *program,
                           const struct panfrost_compile_inputs *inputs)
{
        bifrost_debug = debug_get_option_bifrost_debug();

        bi_context *ctx = rzalloc(NULL, bi_context);
        ctx->nir = nir;
        ctx->stage = nir->info.stage;
        ctx->quirks = bifrost_get_quirks(inputs->gpu_id);
        ctx->is_blend = inputs->is_blend;
        ctx->blend_desc = inputs->blend.bifrost_blend_desc;
        memcpy(ctx->blend_constants, inputs->blend.constants, sizeof(ctx->blend_constants));
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
        NIR_PASS_V(nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
                        glsl_type_size, 0);
        NIR_PASS_V(nir, nir_lower_ssbo);
        // TODO: re-enable when fp16 is flipped on
        // NIR_PASS_V(nir, nir_lower_mediump_outputs);

        bi_optimize_nir(nir);

        if (bifrost_debug & BIFROST_DBG_SHADERS) {
                nir_print_shader(nir, stdout);
        }

        panfrost_nir_assign_sysvals(&ctx->sysvals, ctx, nir);
        program->sysval_count = ctx->sysvals.sysval_count;
        memcpy(program->sysvals, ctx->sysvals.sysvals, sizeof(ctx->sysvals.sysvals[0]) * ctx->sysvals.sysval_count);
        ctx->blend_types = program->blend_types;

        nir_foreach_function(func, nir) {
                if (!func->impl)
                        continue;

                ctx->impl = func->impl;
                emit_cf_list(ctx, &func->impl->body);
                break; /* TODO: Multi-function shaders */
        }

        unsigned block_source_count = 0;

        bi_foreach_block(ctx, _block) {
                bi_block *block = (bi_block *) _block;

                /* Name blocks now that we're done emitting so the order is
                 * consistent */
                block->base.name = block_source_count++;

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

        if (bifrost_debug & BIFROST_DBG_SHADERS)
                bi_print_shader(ctx, stdout);
        bi_schedule(ctx);
        bi_register_allocate(ctx);
        if (bifrost_debug & BIFROST_DBG_SHADERS)
                bi_print_shader(ctx, stdout);
        bi_pack(ctx, &program->compiled);

        memcpy(program->blend_ret_offsets, ctx->blend_ret_offsets, sizeof(program->blend_ret_offsets));

        if (bifrost_debug & BIFROST_DBG_SHADERS)
                disassemble_bifrost(stdout, program->compiled.data, program->compiled.size, true);

        ralloc_free(ctx);
}
