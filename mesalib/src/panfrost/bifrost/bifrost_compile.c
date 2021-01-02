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
#include "bi_builder.h"

static const struct debug_named_value debug_options[] = {
        {"msgs",      BIFROST_DBG_MSGS,		"Print debug messages"},
        {"shaders",   BIFROST_DBG_SHADERS,	"Dump shaders in NIR and MIR"},
        {"shaderdb",  BIFROST_DBG_SHADERDB,	"Print statistics"},
        {"verbose",   BIFROST_DBG_VERBOSE,	"Disassemble verbosely"},
        DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(bifrost_debug, "BIFROST_MESA_DEBUG", debug_options, 0)

/* TODO: This is not thread safe!! */
static unsigned SHADER_DB_COUNT = 0;

int bifrost_debug = 0;

#define DBG(fmt, ...) \
		do { if (bifrost_debug & BIFROST_DBG_MSGS) \
			fprintf(stderr, "%s:%d: "fmt, \
				__FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)

static inline bi_builder
bi_init_builder(bi_context *ctx)
{
        return (bi_builder) {
                .shader = ctx,
                .cursor = bi_after_block(ctx->current_block)
        };
}

static bi_block *emit_cf_list(bi_context *ctx, struct exec_list *list);

static void
bi_emit_jump(bi_builder *b, nir_jump_instr *instr)
{
        bi_instr *branch = bi_jump_to(b, bi_null(), bi_zero());

        switch (instr->type) {
        case nir_jump_break:
                branch->branch_target = b->shader->break_block;
                break;
        case nir_jump_continue:
                branch->branch_target = b->shader->continue_block;
                break;
        default:
                unreachable("Unhandled jump type");
        }

        pan_block_add_successor(&b->shader->current_block->base, &branch->branch_target->base);
        b->shader->current_block->base.unconditional_jumps = true;
}

static void
bi_emit_ld_tile(bi_builder *b, nir_intrinsic_instr *instr)
{
        assert(b->shader->is_blend);

        /* We want to load the current pixel.
         * FIXME: The sample to load is currently hardcoded to 0. This should
         * be addressed for multi-sample FBs.
         */
        struct bifrost_pixel_indices pix = {
                .y = BIFROST_CURRENT_PIXEL,
        };

        uint32_t indices = 0;
        memcpy(&indices, &pix, sizeof(indices));

        bi_ld_tile_to(b, bi_dest_index(&instr->dest), bi_imm_u32(indices),
                bi_register(60), /* coverage bitmap, TODO ra */
                /* Only keep the conversion part of the blend descriptor. */
                bi_imm_u32(b->shader->blend_desc >> 32),
                (instr->num_components - 1));

}

static enum bi_sample
bi_interp_for_intrinsic(nir_intrinsic_op op)
{
        switch (op) {
        case nir_intrinsic_load_barycentric_centroid:
                return BI_SAMPLE_CENTROID;
        case nir_intrinsic_load_barycentric_sample:
                return BI_SAMPLE_SAMPLE;
        case nir_intrinsic_load_barycentric_pixel:
        default:
                return BI_SAMPLE_CENTER;
        }
}

/* auto, 64-bit omitted */
static enum bi_register_format
bi_reg_fmt_for_nir(nir_alu_type T)
{
        switch (T) {
        case nir_type_float16: return BI_REGISTER_FORMAT_F16;
        case nir_type_float32: return BI_REGISTER_FORMAT_F32;
        case nir_type_int16:   return BI_REGISTER_FORMAT_S16;
        case nir_type_uint16:  return BI_REGISTER_FORMAT_U16;
        case nir_type_int32:   return BI_REGISTER_FORMAT_S32;
        case nir_type_uint32:  return BI_REGISTER_FORMAT_U32;
        default: unreachable("Invalid type for register format");
        }
}

/* Checks if the _IMM variant of an intrinsic can be used, returning in imm the
 * immediate to be used (which applies even if _IMM can't be used) */

static bool
bi_is_intr_immediate(nir_intrinsic_instr *instr, unsigned *immediate)
{
        nir_src *offset = nir_get_io_offset_src(instr);

        if (!nir_src_is_const(*offset))
                return false;

        *immediate = nir_intrinsic_base(instr) + nir_src_as_uint(*offset);
        return (*immediate) < 20;
}

static void
bi_emit_load_attr(bi_builder *b, nir_intrinsic_instr *instr)
{
        nir_alu_type T = nir_intrinsic_dest_type(instr);
        enum bi_register_format regfmt = bi_reg_fmt_for_nir(T);
        nir_src *offset = nir_get_io_offset_src(instr);
        unsigned imm_index = 0;
        unsigned base = nir_intrinsic_base(instr);
        bool constant = nir_src_is_const(*offset);
        bool immediate = bi_is_intr_immediate(instr, &imm_index);

        if (immediate) {
                bi_ld_attr_imm_to(b, bi_dest_index(&instr->dest),
                                bi_register(61), /* TODO RA */
                                bi_register(62), /* TODO RA */
                                regfmt, instr->num_components - 1, imm_index);
        } else {
                bi_index idx = bi_src_index(&instr->src[0]);

                if (constant)
                        idx = bi_imm_u32(imm_index);
                else if (base != 0)
                        idx = bi_iadd_u32(b, idx, bi_imm_u32(base), false);

                bi_ld_attr_to(b, bi_dest_index(&instr->dest),
                                bi_register(61), /* TODO RA */
                                bi_register(62), /* TODO RA */
                                idx, regfmt, instr->num_components - 1);
        }
}

static void
bi_emit_load_vary(bi_builder *b, nir_intrinsic_instr *instr)
{
        enum bi_sample sample = BI_SAMPLE_CENTER;
        enum bi_update update = BI_UPDATE_STORE;
        enum bi_register_format regfmt = BI_REGISTER_FORMAT_AUTO;
        enum bi_vecsize vecsize = instr->num_components - 1;
        bool smooth = instr->intrinsic == nir_intrinsic_load_interpolated_input;

        if (smooth) {
                nir_intrinsic_instr *parent = nir_src_as_intrinsic(instr->src[0]);
                assert(parent);

                sample = bi_interp_for_intrinsic(parent->intrinsic);
        } else {
                regfmt = bi_reg_fmt_for_nir(nir_intrinsic_dest_type(instr));
        }

        /* Ignored for non-conditional center and retrieve modes (use an
         * efficient encoding), otherwise R61 for sample mask XXX RA */

        bi_index src0 = (sample == BI_SAMPLE_CENTER) ? bi_passthrough(BIFROST_SRC_FAU_HI) :
                bi_register(61);

        nir_src *offset = nir_get_io_offset_src(instr);
        unsigned imm_index = 0;
        bool immediate = bi_is_intr_immediate(instr, &imm_index);

        if (immediate && smooth) {
                bi_ld_var_imm_to(b, bi_dest_index(&instr->dest),
                                src0, regfmt, sample, update, vecsize,
                                imm_index);
        } else if (immediate && !smooth) {
                bi_ld_var_flat_imm_to(b, bi_dest_index(&instr->dest),
                                BI_FUNCTION_NONE, regfmt, vecsize, imm_index);
        } else {
                bi_index idx = bi_src_index(offset);
                unsigned base = nir_intrinsic_base(instr);

                if (base != 0)
                        idx = bi_iadd_u32(b, idx, bi_imm_u32(base), false);

                if (smooth) {
                        bi_ld_var_to(b, bi_dest_index(&instr->dest),
                                        src0, idx, regfmt, sample, update,
                                        vecsize);
                } else {
                        bi_ld_var_flat_to(b, bi_dest_index(&instr->dest),
                                        idx, BI_FUNCTION_NONE, regfmt,
                                        vecsize);
                }
        }
}

static void
bi_make_vec_to(bi_builder *b, bi_index final_dst,
                bi_index *src,
                unsigned *channel,
                unsigned count,
                unsigned bitsize)
{
        /* If we reads our own output, we need a temporary move to allow for
         * swapping. TODO: Could do a bit better for pairwise swaps of 16-bit
         * vectors */
        bool reads_self = false;

        for (unsigned i = 0; i < count; ++i)
                reads_self |= bi_is_equiv(final_dst, src[i]);

        /* SSA can't read itself */
        assert(!reads_self || final_dst.reg);

        bi_index dst = reads_self ? bi_temp(b->shader) : final_dst;

        if (bitsize == 32) {
                for (unsigned i = 0; i < count; ++i) {
                        bi_mov_i32_to(b, bi_word(dst, i),
                                        bi_word(src[i], channel ? channel[i] : 0));
                }
        } else if (bitsize == 16) {
                for (unsigned i = 0; i < count; i += 2) {
                        unsigned chan = channel ? channel[i] : 0;

                        bi_index w0 = bi_half(bi_word(src[i], chan >> 1), chan & 1);
                        bi_index w1 = bi_imm_u16(0);

                        /* Don't read out of bound for vec3 */
                        if ((i + 1) < count) {
                                unsigned nextc = channel ? channel[i + 1] : 0;
                                w1 = bi_half(bi_word(src[i + 1], nextc >> 1), nextc & 1);
                        }

                        bi_mkvec_v2i16_to(b, bi_word(dst, i >> 1), w0, w1);
                }
        } else {
                unreachable("8-bit mkvec not yet supported");
        }

        /* Emit an explicit copy if needed */
        if (!bi_is_equiv(dst, final_dst)) {
                unsigned shift = (bitsize == 8) ? 2 : (bitsize == 16) ? 1 : 0;
                unsigned vec = (1 << shift);

                for (unsigned i = 0; i < count; i += vec) {
                        bi_mov_i32_to(b, bi_word(final_dst, i >> shift),
                                        bi_word(dst, i >> shift));
                }
        }
}

static void
bi_emit_load_blend_input(bi_builder *b, nir_intrinsic_instr *instr)
{
        ASSERTED nir_io_semantics sem = nir_intrinsic_io_semantics(instr);

        /* We don't support dual-source blending yet. */
        assert(sem.location == VARYING_SLOT_COL0);

        /* Source color is passed through r0-r3.  TODO: Precolour instead */
        bi_index srcs[] = {
                bi_register(0), bi_register(1), bi_register(2), bi_register(3)
        };

        bi_make_vec_to(b, bi_dest_index(&instr->dest), srcs, NULL, 4, 32);
}

static void
bi_emit_blend_op(bi_builder *b, bi_index rgba, nir_alu_type T, unsigned rt)
{
        if (b->shader->is_blend) {
                /* Blend descriptor comes from the compile inputs */
                /* Put the result in r0 */
                bi_blend_to(b, bi_register(0), rgba,
                                bi_register(60) /* TODO RA */,
                                bi_imm_u32(b->shader->blend_desc & 0xffffffff),
                                bi_imm_u32(b->shader->blend_desc >> 32));
        } else {
                /* Blend descriptor comes from the FAU RAM. By convention, the
                 * return address is stored in r48 and will be used by the
                 * blend shader to jump back to the fragment shader after */
                bi_blend_to(b, bi_register(48), rgba,
                                bi_register(60) /* TODO RA */,
                                bi_fau(BIR_FAU_BLEND_0 + rt, false),
                                bi_fau(BIR_FAU_BLEND_0 + rt, true));
        }

        assert(rt < 8);
        assert(b->shader->blend_types);
        b->shader->blend_types[rt] = T;
}

static void
bi_emit_fragment_out(bi_builder *b, nir_intrinsic_instr *instr)
{
        bool combined = instr->intrinsic ==
                nir_intrinsic_store_combined_output_pan;

        unsigned writeout = combined ? nir_intrinsic_component(instr) :
                PAN_WRITEOUT_C;

        bool emit_blend = writeout & (PAN_WRITEOUT_C);
        bool emit_zs = writeout & (PAN_WRITEOUT_Z | PAN_WRITEOUT_S);

        const nir_variable *var =
                nir_find_variable_with_driver_location(b->shader->nir,
                                nir_var_shader_out, nir_intrinsic_base(instr));
        assert(var);

        if (!b->shader->emitted_atest && !b->shader->is_blend) {
                nir_alu_type T = nir_intrinsic_src_type(instr);
                assert(T == nir_type_float16 || T == nir_type_float32);

                bi_index rgba = bi_src_index(&instr->src[0]);
                bi_index alpha = (T == nir_type_float32) ? bi_word(rgba, 3) :
                        bi_half(bi_word(rgba, 1), true) ;

                bi_atest_to(b, bi_register(60), bi_register(60), alpha);
                b->shader->emitted_atest = true;
        }

        if (emit_zs) {
                bi_index z = { 0 }, s = { 0 };

                if (writeout & PAN_WRITEOUT_Z)
                        z = bi_src_index(&instr->src[2]);

                if (writeout & PAN_WRITEOUT_S)
                        s = bi_src_index(&instr->src[3]);

                bi_zs_emit_to(b, bi_register(60), z, s,
                                bi_register(60) /* TODO RA */,
                                writeout & PAN_WRITEOUT_S,
                                writeout & PAN_WRITEOUT_Z);
        }

        if (emit_blend) {
                unsigned loc = var->data.location;
                assert(loc == FRAG_RESULT_COLOR || loc >= FRAG_RESULT_DATA0);

                unsigned rt = loc == FRAG_RESULT_COLOR ? 0 :
                        (loc - FRAG_RESULT_DATA0);

                bi_emit_blend_op(b, bi_src_index(&instr->src[0]),
                                nir_intrinsic_src_type(instr), rt);
        }

        if (b->shader->is_blend) {
                /* Jump back to the fragment shader, return address is stored
                 * in r48 (see above).
                 */
                bi_jump_to(b, bi_null(), bi_register(48));
        }
}

static void
bi_emit_store_vary(bi_builder *b, nir_intrinsic_instr *instr)
{
        nir_alu_type T = nir_intrinsic_src_type(instr);
        enum bi_register_format regfmt = bi_reg_fmt_for_nir(T);

        nir_src *offset = nir_get_io_offset_src(instr);
        unsigned imm_index = 0;
        bool immediate = bi_is_intr_immediate(instr, &imm_index);

        bi_index address = immediate ?
                bi_lea_attr_imm(b,
                                bi_register(61), /* TODO RA */
                                bi_register(62), /* TODO RA */
                                regfmt, imm_index) :
                bi_lea_attr(b,
                                bi_register(61), /* TODO RA */
                                bi_register(62), /* TODO RA */
                                bi_src_index(offset), regfmt);

        /* Only look at the total components needed. In effect, we fill in all
         * the intermediate "holes" in the write mask, since we can't mask off
         * stores. Since nir_lower_io_to_temporaries ensures each varying is
         * written at most once, anything that's masked out is undefined, so it
         * doesn't matter what we write there. So we may as well do the
         * simplest thing possible. */
        unsigned nr = util_last_bit(nir_intrinsic_write_mask(instr));
        assert(nr > 0 && nr <= nir_intrinsic_src_components(instr, 0));

        bi_st_cvt_to(b, bi_null(), bi_src_index(&instr->src[0]),
                        address, bi_word(address, 1), bi_word(address, 2),
                        regfmt, nr - 1);
}

static void
bi_emit_load_ubo(bi_builder *b, nir_intrinsic_instr *instr)
{
        /* nir_lower_uniforms_to_ubo() should have been called, reserving
         * UBO #0 for uniforms even if the shaders doesn't have uniforms.
         */
        assert(b->shader->nir->info.first_ubo_is_default_ubo);

        bool offset_is_const = nir_src_is_const(instr->src[1]);
        bi_index dyn_offset = bi_src_index(&instr->src[1]);
        uint32_t const_offset = 0;

        /* We may need to offset UBO loads by however many sysvals we have */
        unsigned sysval_offset = 16 * b->shader->sysvals.sysval_count;

        if (nir_src_is_const(instr->src[1]))
                const_offset = nir_src_as_uint(instr->src[1]);

        if (nir_src_is_const(instr->src[0]) &&
            nir_src_as_uint(instr->src[0]) == 0 &&
            b->shader->sysvals.sysval_count) {
                if (offset_is_const) {
                        const_offset += sysval_offset;
                } else {
                        dyn_offset = bi_iadd_u32(b, dyn_offset,
                                        bi_imm_u32(sysval_offset), false);
                }
        }

        bi_load_to(b, instr->num_components * 32,
                        bi_dest_index(&instr->dest), offset_is_const ?
                        bi_imm_u32(const_offset) : dyn_offset,
                        bi_src_index(&instr->src[0]),
                        BI_SEG_UBO);
}

static void
bi_load_sysval(bi_builder *b, nir_instr *instr,
                unsigned nr_components, unsigned offset)
{
        nir_dest nir_dest;

        /* Figure out which uniform this is */
        int sysval = panfrost_sysval_for_instr(instr, &nir_dest);
        void *val = _mesa_hash_table_u64_search(b->shader->sysvals.sysval_to_id, sysval);

        /* Sysvals are prefix uniforms */
        unsigned uniform = ((uintptr_t) val) - 1;
        unsigned idx = (uniform * 16) + offset;

        bi_load_to(b, nr_components * 32, bi_dest_index(&nir_dest),
                        bi_imm_u32(idx), bi_zero(), BI_SEG_UBO);
}

/* gl_FragCoord.xy = u16_to_f32(R59.xy) + 0.5
 * gl_FragCoord.z = ld_vary(fragz)
 * gl_FragCoord.w = ld_vary(fragw)
 */

static void
bi_emit_load_frag_coord(bi_builder *b, nir_intrinsic_instr *instr)
{
        bi_index src[4] = {};

        for (unsigned i = 0; i < 2; ++i) {
                src[i] = bi_fadd_f32(b,
                                bi_u16_to_f32(b, bi_half(bi_register(59), i)),
                                bi_imm_f32(0.5f), BI_ROUND_NONE);
        }

        for (unsigned i = 0; i < 2; ++i) {
                src[2 + i] = bi_ld_var_special(b, bi_zero(),
                                BI_REGISTER_FORMAT_F32, BI_SAMPLE_CENTER,
                                BI_UPDATE_CLOBBER,
                                (i == 0) ? BI_VARYING_NAME_FRAG_Z :
                                        BI_VARYING_NAME_FRAG_W,
                                BI_VECSIZE_NONE);
        }

        bi_make_vec_to(b, bi_dest_index(&instr->dest), src, NULL, 4, 32);
}

static void
bi_emit_intrinsic(bi_builder *b, nir_intrinsic_instr *instr)
{
        bi_index dst = nir_intrinsic_infos[instr->intrinsic].has_dest ?
                bi_dest_index(&instr->dest) : bi_null();
        gl_shader_stage stage = b->shader->stage;

        switch (instr->intrinsic) {
        case nir_intrinsic_load_barycentric_pixel:
        case nir_intrinsic_load_barycentric_centroid:
        case nir_intrinsic_load_barycentric_sample:
                /* handled later via load_vary */
                break;
        case nir_intrinsic_load_interpolated_input:
        case nir_intrinsic_load_input:
                if (b->shader->is_blend)
                        bi_emit_load_blend_input(b, instr);
                else if (stage == MESA_SHADER_FRAGMENT)
                        bi_emit_load_vary(b, instr);
                else if (stage == MESA_SHADER_VERTEX)
                        bi_emit_load_attr(b, instr);
                else
                        unreachable("Unsupported shader stage");
                break;

        case nir_intrinsic_store_output:
                if (stage == MESA_SHADER_FRAGMENT)
                        bi_emit_fragment_out(b, instr);
                else if (stage == MESA_SHADER_VERTEX)
                        bi_emit_store_vary(b, instr);
                else
                        unreachable("Unsupported shader stage");
                break;

        case nir_intrinsic_store_combined_output_pan:
                assert(stage == MESA_SHADER_FRAGMENT);
                bi_emit_fragment_out(b, instr);
                break;

        case nir_intrinsic_load_ubo:
                bi_emit_load_ubo(b, instr);
                break;

        case nir_intrinsic_load_frag_coord:
                bi_emit_load_frag_coord(b, instr);
                break;

        case nir_intrinsic_load_output:
                bi_emit_ld_tile(b, instr);
                break;

        case nir_intrinsic_discard_if: {
                bi_index src = bi_src_index(&instr->src[0]);

                unsigned sz = nir_src_bit_size(instr->src[0]);
                assert(sz == 16 || sz == 32);

                if (sz == 16)
                        src = bi_half(src, false);

                bi_discard_f32_to(b, bi_null(), src, bi_zero(), BI_CMPF_NE);
                break;
        }

        case nir_intrinsic_discard:
                bi_discard_f32_to(b, bi_null(), bi_zero(), bi_zero(),
                                BI_CMPF_EQ);
                break;

        case nir_intrinsic_load_ssbo_address:
                bi_load_sysval(b, &instr->instr, 1, 0);
                break;

        case nir_intrinsic_get_ssbo_size:
                bi_load_sysval(b, &instr->instr, 1, 8);
                break;

        case nir_intrinsic_load_viewport_scale:
        case nir_intrinsic_load_viewport_offset:
        case nir_intrinsic_load_num_work_groups:
        case nir_intrinsic_load_sampler_lod_parameters_pan:
                bi_load_sysval(b, &instr->instr, 3, 0);
                break;
        case nir_intrinsic_load_blend_const_color_r_float:
                bi_mov_i32_to(b, dst,
                                bi_imm_f32(b->shader->blend_constants[0]));
                break;

        case nir_intrinsic_load_blend_const_color_g_float:
                bi_mov_i32_to(b, dst,
                                bi_imm_f32(b->shader->blend_constants[1]));
                break;

        case nir_intrinsic_load_blend_const_color_b_float:
                bi_mov_i32_to(b, dst,
                                bi_imm_f32(b->shader->blend_constants[2]));
                break;

        case nir_intrinsic_load_blend_const_color_a_float:
                bi_mov_i32_to(b, dst,
                                bi_imm_f32(b->shader->blend_constants[3]));
                break;

	case nir_intrinsic_load_sample_id: {
                /* r61[16:23] contains the sampleID, mask it out */

                bi_rshift_and_i32_to(b, dst, bi_register(61), bi_imm_u32(0xff),
                                bi_imm_u8(16));
                break;
        }

	case nir_intrinsic_load_front_face:
                /* r58 == 0 means primitive is front facing */
                bi_icmp_i32_to(b, dst, bi_register(58), bi_zero(), BI_CMPF_EQ,
                                BI_RESULT_TYPE_M1);
                break;

        case nir_intrinsic_load_point_coord:
                bi_ld_var_special_to(b, dst, bi_zero(), BI_REGISTER_FORMAT_F32,
                                BI_SAMPLE_CENTER, BI_UPDATE_CLOBBER,
                                BI_VARYING_NAME_POINT, BI_VECSIZE_V2);
                break;

        case nir_intrinsic_load_vertex_id:
                bi_mov_i32_to(b, dst, bi_register(61));
                break;

        case nir_intrinsic_load_instance_id:
                bi_mov_i32_to(b, dst, bi_register(62));
                break;

        default:
                unreachable("Unknown intrinsic");
        }
}

static void
bi_emit_load_const(bi_builder *b, nir_load_const_instr *instr)
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

        bi_mov_i32_to(b, bi_get_index(instr->def.index, false, 0), bi_imm_u32(acc));
}

static bi_index
bi_alu_src_index(nir_alu_src src, unsigned comps)
{
        /* we don't lower modifiers until the backend */
        assert(!(src.negate || src.abs));

        unsigned bitsize = nir_src_bit_size(src.src);

        /* the bi_index carries the 32-bit (word) offset separate from the
         * subword swizzle, first handle the offset */

        unsigned offset = 0;

        assert(bitsize == 8 || bitsize == 16 || bitsize == 32);
        unsigned subword_shift = (bitsize == 32) ? 0 : (bitsize == 16) ? 1 : 2;

        for (unsigned i = 0; i < comps; ++i) {
                unsigned new_offset = (src.swizzle[i] >> subword_shift);

                if (i > 0)
                        assert(offset == new_offset);

                offset = new_offset;
        }

        bi_index idx = bi_word(bi_src_index(&src.src), offset);

        /* Compose the subword swizzle with existing (identity) swizzle */
        assert(idx.swizzle == BI_SWIZZLE_H01);

        /* Bigger vectors should have been lowered */
        assert(comps <= (1 << bitsize));

        if (bitsize == 16) {
                unsigned c0 = src.swizzle[0] & 1;
                unsigned c1 = (comps > 1) ? src.swizzle[1] & 1 : c0;
                idx.swizzle = BI_SWIZZLE_H00 + c1 + (c0 << 1);
        } else if (bitsize == 8) {
                unreachable("8-bit handling todo");
        }

        return idx;
}

static enum bi_round
bi_nir_round(nir_op op)
{
        switch (op) {
        case nir_op_fround_even: return BI_ROUND_NONE;
        case nir_op_ftrunc: return BI_ROUND_RTZ;
        case nir_op_fceil: return BI_ROUND_RTP;
        case nir_op_ffloor: return BI_ROUND_RTN;
        default: unreachable("invalid nir round op");
        }
}

static enum bi_cmpf
bi_cmpf_nir(nir_op op)
{
        switch (op) {
        case nir_op_flt32:
        case nir_op_ilt32:
        case nir_op_ult32:
                return BI_CMPF_LT;

        case nir_op_fge32:
        case nir_op_ige32:
        case nir_op_uge32:
                return BI_CMPF_GE;

        case nir_op_feq32:
        case nir_op_ieq32:
                return BI_CMPF_EQ;

        case nir_op_fneu32:
        case nir_op_ine32:
                return BI_CMPF_NE;

        default:
                unreachable("Invalid compare");
        }
}

static void
bi_emit_alu(bi_builder *b, nir_alu_instr *instr)
{
        bi_index dst = bi_dest_index(&instr->dest.dest);
        unsigned sz = nir_dest_bit_size(instr->dest.dest);

        unsigned srcs = nir_op_infos[instr->op].num_inputs;
        unsigned comps = nir_dest_num_components(instr->dest.dest);

        if (!instr->dest.dest.is_ssa) {
                for (unsigned i = 0; i < comps; ++i)
                        assert(instr->dest.write_mask);
        }

        /* First, match against the various moves in NIR. These are
         * special-cased because they can operate on vectors even after
         * lowering ALU to scalar. For Bifrost, bi_alu_src_index assumes the
         * instruction is no "bigger" than SIMD-within-a-register. These moves
         * are the exceptions that need to handle swizzles specially. */

        switch (instr->op) {
        case nir_op_vec2:
        case nir_op_vec3:
        case nir_op_vec4: {
                bi_index unoffset_srcs[4] = {
                        srcs > 0 ? bi_src_index(&instr->src[0].src) : bi_null(),
                        srcs > 1 ? bi_src_index(&instr->src[1].src) : bi_null(),
                        srcs > 2 ? bi_src_index(&instr->src[2].src) : bi_null(),
                        srcs > 3 ? bi_src_index(&instr->src[3].src) : bi_null(),
                };

                unsigned channels[4] = {
                        instr->src[0].swizzle[0],
                        instr->src[1].swizzle[0],
                        srcs > 2 ? instr->src[2].swizzle[0] : 0,
                        srcs > 3 ? instr->src[3].swizzle[0] : 0,
                };

                bi_make_vec_to(b, dst, unoffset_srcs, channels, srcs, sz);
                return;
        }

        case nir_op_vec8:
        case nir_op_vec16:
                unreachable("should've been lowered");

        case nir_op_mov: {
                bi_index idx = bi_src_index(&instr->src[0].src);
                bi_index unoffset_srcs[4] = { idx, idx, idx, idx };

                unsigned channels[4] = {
                        comps > 0 ? instr->src[0].swizzle[0] : 0,
                        comps > 1 ? instr->src[0].swizzle[1] : 0,
                        comps > 2 ? instr->src[0].swizzle[2] : 0,
                        comps > 3 ? instr->src[0].swizzle[3] : 0,
                };

                bi_make_vec_to(b, dst, unoffset_srcs, channels, comps, sz);
                return;
        }

        default:
                break;
        }

        bi_index s0 = srcs > 0 ? bi_alu_src_index(instr->src[0], comps) : bi_null();
        bi_index s1 = srcs > 1 ? bi_alu_src_index(instr->src[1], comps) : bi_null();
        bi_index s2 = srcs > 2 ? bi_alu_src_index(instr->src[2], comps) : bi_null();

        unsigned src_sz = srcs > 0 ? nir_src_bit_size(instr->src[0].src) : 0;

        switch (instr->op) {
        case nir_op_ffma:
                bi_fma_to(b, sz, dst, s0, s1, s2, BI_ROUND_NONE);
                break;

        case nir_op_fmul:
                bi_fma_to(b, sz, dst, s0, s1, bi_zero(), BI_ROUND_NONE);
                break;

        case nir_op_fsub:
                s1 = bi_neg(s1);
                /* fallthrough */
        case nir_op_fadd:
                bi_fadd_to(b, sz, dst, s0, s1, BI_ROUND_NONE);
                break;

        case nir_op_fsat: {
                bi_instr *I = (sz == 32) ?
                        bi_fadd_f32_to(b, dst, s0, bi_zero(), BI_ROUND_NONE) :
                        bi_fma_v2f16_to(b, dst, s0, bi_zero(), bi_zero(),
                                        BI_ROUND_NONE);

                I->clamp = BI_CLAMP_CLAMP_0_1;
                break;
        }

        case nir_op_fneg:
                bi_fadd_to(b, sz, dst, bi_neg(s0), bi_zero(), BI_ROUND_NONE);
                break;

        case nir_op_fabs:
                bi_fadd_to(b, sz, dst, bi_abs(s0), bi_zero(), BI_ROUND_NONE);
                break;

        case nir_op_fexp2: {
                /* TODO G71 */
                assert(sz == 32); /* should've been lowered */

                /* multiply by 1.0 * 2*24 */
                bi_index scale = bi_fma_rscale_f32(b, s0, bi_imm_f32(1.0f),
                                bi_zero(), bi_imm_u32(24), BI_ROUND_NONE,
                                BI_SPECIAL_NONE);

                bi_fexp_f32_to(b, dst, bi_f32_to_s32(b, scale, BI_ROUND_NONE), s0);
                break;
        }

        case nir_op_flog2: {
                /* TODO G71 */
                assert(sz == 32); /* should've been lowered */
                bi_index frexp = bi_frexpe_f32(b, s0, true, false);
                bi_index frexpi = bi_s32_to_f32(b, frexp, BI_ROUND_RTZ);
                bi_index add = bi_fadd_lscale_f32(b, bi_imm_f32(-1.0f), s0);
                bi_fma_f32_to(b, dst, bi_flogd_f32(b, s0), add, frexpi,
                                BI_ROUND_NONE);
                break;
        }

        case nir_op_b8csel:
        case nir_op_b16csel:
        case nir_op_b32csel:
                if (sz == 8)
                        bi_mux_v4i8_to(b, dst, s2, s1, s0, BI_MUX_INT_ZERO);
                else
                        bi_csel_to(b, sz, dst, s0, bi_zero(), s1, s2, BI_CMPF_NE);
                break;

        case nir_op_ishl:
                bi_lshift_or_to(b, sz, dst, s0, bi_zero(), bi_byte(s1, 0));
                break;
        case nir_op_ushr:
                bi_rshift_or_to(b, sz, dst, s0, bi_zero(), bi_byte(s1, 0));
                break;

        case nir_op_ishr:
                bi_arshift_to(b, sz, dst, s0, bi_null(), bi_byte(s1, 0));
                break;

        case nir_op_flt32:
        case nir_op_fge32:
        case nir_op_feq32:
        case nir_op_fneu32:
                bi_fcmp_to(b, sz, dst, s0, s1, bi_cmpf_nir(instr->op),
                                BI_RESULT_TYPE_M1);
                break;

        case nir_op_ieq32:
        case nir_op_ine32:
                if (sz == 32) {
                        bi_icmp_i32_to(b, dst, s0, s1, bi_cmpf_nir(instr->op),
                                        BI_RESULT_TYPE_M1);
                } else if (sz == 16) {
                        bi_icmp_v2i16_to(b, dst, s0, s1, bi_cmpf_nir(instr->op),
                                        BI_RESULT_TYPE_M1);
                } else {
                        bi_icmp_v4i8_to(b, dst, s0, s1, bi_cmpf_nir(instr->op),
                                        BI_RESULT_TYPE_M1);
                }
                break;

        case nir_op_ilt32:
        case nir_op_ige32:
                if (sz == 32) {
                        bi_icmp_s32_to(b, dst, s0, s1, bi_cmpf_nir(instr->op),
                                        BI_RESULT_TYPE_M1);
                } else if (sz == 16) {
                        bi_icmp_v2s16_to(b, dst, s0, s1, bi_cmpf_nir(instr->op),
                                        BI_RESULT_TYPE_M1);
                } else {
                        bi_icmp_v4s8_to(b, dst, s0, s1, bi_cmpf_nir(instr->op),
                                        BI_RESULT_TYPE_M1);
                }
                break;

        case nir_op_ult32:
        case nir_op_uge32:
                if (sz == 32) {
                        bi_icmp_u32_to(b, dst, s0, s1, bi_cmpf_nir(instr->op),
                                        BI_RESULT_TYPE_M1);
                } else if (sz == 16) {
                        bi_icmp_v2u16_to(b, dst, s0, s1, bi_cmpf_nir(instr->op),
                                        BI_RESULT_TYPE_M1);
                } else {
                        bi_icmp_v4u8_to(b, dst, s0, s1, bi_cmpf_nir(instr->op),
                                        BI_RESULT_TYPE_M1);
                }
                break;

        case nir_op_fddx:
        case nir_op_fddy: {
                bi_index cur_lane = bi_mov_i32(b, bi_fau(BIR_FAU_LANE_ID, false));

                bi_index lane1 = bi_lshift_and_i32(b, cur_lane,
                                bi_imm_u32(instr->op == nir_op_fddx ? 2 : 1),
                                bi_byte(bi_zero(), 0));

                bi_index lane2 = bi_iadd_u32(b, lane1,
                                bi_imm_u32(instr->op == nir_op_fddx ? 1 : 2),
                                false);

                bi_index left, right;

                if (b->shader->arch == 6) {
                        left = bi_clper_v6_i32(b, s0, lane1);
                        right = bi_clper_v6_i32(b, s0, lane2);
                } else {
                        left = bi_clper_v7_i32(b, s0, lane1,
                                        BI_INACTIVE_RESULT_ZERO, BI_LANE_OP_NONE,
                                        BI_SUBGROUP_SUBGROUP4);

                        right = bi_clper_v7_i32(b, s0, lane2,
                                        BI_INACTIVE_RESULT_ZERO, BI_LANE_OP_NONE,
                                        BI_SUBGROUP_SUBGROUP4);
                }

                bi_fadd_to(b, sz, dst, right, bi_neg(left), BI_ROUND_NONE);
                break;
        }

        case nir_op_f2f16:
                bi_v2f32_to_v2f16_to(b, dst, s0, s0, BI_ROUND_NONE);
                break;

        case nir_op_f2f32:
                bi_f16_to_f32_to(b, dst, s0);
                break;

        case nir_op_f2i32:
                if (src_sz == 32)
                        bi_f32_to_s32_to(b, dst, s0, BI_ROUND_RTZ);
                else
                        bi_f16_to_s32_to(b, dst, s0, BI_ROUND_RTZ);
                break;

        case nir_op_f2u16:
                if (src_sz == 32)
                        unreachable("should've been lowered");
                else
                        bi_v2f16_to_v2u16_to(b, dst, s0, BI_ROUND_RTZ);
                break;

        case nir_op_f2i16:
                if (src_sz == 32)
                        unreachable("should've been lowered");
                else
                        bi_v2f16_to_v2s16_to(b, dst, s0, BI_ROUND_RTZ);
                break;

        case nir_op_f2u32:
                if (src_sz == 32)
                        bi_f32_to_u32_to(b, dst, s0, BI_ROUND_RTZ);
                else
                        bi_f16_to_u32_to(b, dst, s0, BI_ROUND_RTZ);
                break;

        case nir_op_u2f16:
                if (src_sz == 32)
                        unreachable("should've been lowered");
                else if (src_sz == 16)
                        bi_v2u16_to_v2f16_to(b, dst, s0, BI_ROUND_RTZ);
                else if (src_sz == 8)
                        bi_v2u8_to_v2f16_to(b, dst, s0);
                break;

        case nir_op_u2f32:
                if (src_sz == 32)
                        bi_u32_to_f32_to(b, dst, s0, BI_ROUND_RTZ);
                else if (src_sz == 16)
                        bi_u16_to_f32_to(b, dst, s0);
                else
                        bi_u8_to_f32_to(b, dst, bi_byte(s0, 0));
                break;

        case nir_op_i2f16:
                if (src_sz == 32)
                        unreachable("should've been lowered");
                else
                        bi_v2s16_to_v2f16_to(b, dst, s0, BI_ROUND_RTZ);
                break;

        case nir_op_i2f32:
                if (src_sz == 32)
                        bi_s32_to_f32_to(b, dst, s0, BI_ROUND_RTZ);
                else
                        bi_s16_to_f32_to(b, dst, s0);
                break;

        case nir_op_i2i32:
                if (src_sz == 16)
                        bi_s16_to_s32_to(b, dst, s0);
                else
                        bi_s8_to_s32_to(b, dst, s0);
                break;

        case nir_op_u2u32:
                if (src_sz == 16)
                        bi_u16_to_u32_to(b, dst, s0);
                else
                        bi_u8_to_u32_to(b, dst, s0);
                break;

        /* todo optimize out downcasts */
        case nir_op_i2i16:
                assert(src_sz == 8 || src_sz == 32);

                if (src_sz == 8)
                        bi_v2s8_to_v2s16_to(b, dst, s0);
                else
                        bi_mkvec_v2i16_to(b, dst, bi_half(s0, false), bi_imm_u16(0));
                break;

        case nir_op_u2u16:
                assert(src_sz == 8 || src_sz == 32);

                if (src_sz == 8)
                        bi_v2u8_to_v2u16_to(b, dst, s0);
                else
                        bi_mkvec_v2i16_to(b, dst, bi_half(s0, false), bi_imm_u16(0));
                break;

        case nir_op_i2i8:
        case nir_op_u2u8:
                unreachable("should've been lowered");

        case nir_op_fround_even:
        case nir_op_fceil:
        case nir_op_ffloor:
        case nir_op_ftrunc:
                bi_fround_to(b, sz, dst, s0, bi_nir_round(instr->op));
                break;

        case nir_op_fmin:
                bi_fmin_to(b, sz, dst, s0, s1);
                break;

        case nir_op_fmax:
                bi_fmax_to(b, sz, dst, s0, s1);
                break;

        case nir_op_iadd:
                bi_iadd_to(b, sz, dst, s0, s1, false);
                break;

        case nir_op_isub:
                bi_isub_to(b, sz, dst, s0, s1, false);
                break;

        case nir_op_imul:
                bi_imul_to(b, sz, dst, s0, s1);
                break;

        case nir_op_iabs:
                bi_iabs_to(b, sz, dst, s0);
                break;

        case nir_op_iand:
                bi_lshift_and_to(b, sz, dst, s0, s1, bi_imm_u8(0));
                break;

        case nir_op_ior:
                bi_lshift_or_to(b, sz, dst, s0, s1, bi_imm_u8(0));
                break;

        case nir_op_ixor:
                bi_lshift_xor_to(b, sz, dst, s0, s1, bi_imm_u8(0));
                break;

        case nir_op_inot:
                bi_lshift_or_to(b, sz, dst, bi_zero(), bi_not(s0), bi_imm_u8(0));
                break;

        case nir_op_frsq:
                bi_frsq_to(b, sz, dst, s0);
                break;

        case nir_op_frcp:
                bi_frcp_to(b, sz, dst, s0);
                break;

        default:
                fprintf(stderr, "Unhandled ALU op %s\n", nir_op_infos[instr->op].name);
                unreachable("Unknown ALU op");
        }
}

/* TEXS instructions assume normal 2D f32 operation but are more
 * space-efficient and with simpler RA/scheduling requirements*/

static void
bi_emit_texs(bi_builder *b, nir_tex_instr *instr)
{
        int coord_idx = nir_tex_instr_src_index(instr, nir_tex_src_coord);
        assert(coord_idx >= 0);
        bi_index coords = bi_src_index(&instr->src[coord_idx].src);

        bi_texs_2d_to(b, nir_dest_bit_size(instr->dest),
                        bi_dest_index(&instr->dest),
                        coords, bi_word(coords, 1),
                        instr->op != nir_texop_tex, /* zero LOD */
                        instr->sampler_index, instr->texture_index);
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
bi_texture_format(nir_alu_type T, enum bi_clamp clamp)
{
        switch (T) {
        case nir_type_float16: return BIFROST_TEXTURE_FORMAT_F16 + clamp;
        case nir_type_float32: return BIFROST_TEXTURE_FORMAT_F32 + clamp;
        case nir_type_uint16:  return BIFROST_TEXTURE_FORMAT_U16;
        case nir_type_int16:   return BIFROST_TEXTURE_FORMAT_S16;
        case nir_type_uint32:  return BIFROST_TEXTURE_FORMAT_U32;
        case nir_type_int32:   return BIFROST_TEXTURE_FORMAT_S32;
        default:              unreachable("Invalid type for texturing");
        }
}

/* Array indices are specified as 32-bit uints, need to convert. In .z component from NIR */
static bi_index
bi_emit_texc_array_index(bi_builder *b, bi_index idx, nir_alu_type T)
{
        /* For (u)int we can just passthrough */
        nir_alu_type base = nir_alu_type_get_base_type(T);
        if (base == nir_type_int || base == nir_type_uint)
                return idx;

        /* Otherwise we convert */
        assert(T == nir_type_float32);

        /* OpenGL ES 3.2 specification section 8.14.2 ("Coordinate Wrapping and
         * Texel Selection") defines the layer to be taken from clamp(RNE(r),
         * 0, dt - 1). So we use round RTE, clamping is handled at the data
         * structure level */

        return bi_f32_to_u32(b, idx, BI_ROUND_NONE);
}

/* TEXC's explicit and bias LOD modes requires the LOD to be transformed to a
 * 16-bit 8:8 fixed-point format. We lower as:
 *
 * F32_TO_S32(clamp(x, -16.0, +16.0) * 256.0) & 0xFFFF =
 * MKVEC(F32_TO_S32(clamp(x * 1.0/16.0, -1.0, 1.0) * (16.0 * 256.0)), #0)
 */

static bi_index
bi_emit_texc_lod_88(bi_builder *b, bi_index lod, bool fp16)
{
        /* Sort of arbitrary. Must be less than 128.0, greater than or equal to
         * the max LOD (16 since we cap at 2^16 texture dimensions), and
         * preferably small to minimize precision loss */
        const float max_lod = 16.0;

        bi_instr *fsat = bi_fma_f32_to(b, bi_temp(b->shader),
                        fp16 ? bi_half(lod, false) : lod,
                        bi_imm_f32(1.0f / max_lod), bi_zero(), BI_ROUND_NONE);

        fsat->clamp = BI_CLAMP_CLAMP_M1_1;

        bi_index fmul = bi_fma_f32(b, fsat->dest[0], bi_imm_f32(max_lod * 256.0f),
                        bi_zero(), BI_ROUND_NONE);

        return bi_mkvec_v2i16(b,
                        bi_half(bi_f32_to_s32(b, fmul, BI_ROUND_RTZ), false),
                        bi_imm_u16(0));
}

/* FETCH takes a 32-bit staging register containing the LOD as an integer in
 * the bottom 16-bits and (if present) the cube face index in the top 16-bits.
 * TODO: Cube face.
 */

static bi_index
bi_emit_texc_lod_cube(bi_builder *b, bi_index lod)
{
        return bi_lshift_or_i32(b, lod, bi_zero(), bi_imm_u8(8));
}

/* The hardware specifies texel offsets and multisample indices together as a
 * u8vec4 <offset, ms index>. By default all are zero, so if have either a
 * nonzero texel offset or a nonzero multisample index, we build a u8vec4 with
 * the bits we need and return that to be passed as a staging register. Else we
 * return 0 to avoid allocating a data register when everything is zero. */

static bi_index
bi_emit_texc_offset_ms_index(bi_builder *b, nir_tex_instr *instr)
{
        bi_index dest = bi_zero();

        int offs_idx = nir_tex_instr_src_index(instr, nir_tex_src_offset);
        if (offs_idx >= 0 &&
            (!nir_src_is_const(instr->src[offs_idx].src) ||
             nir_src_as_uint(instr->src[offs_idx].src) != 0)) {
                unsigned nr = nir_src_num_components(instr->src[offs_idx].src);
                bi_index idx = bi_src_index(&instr->src[offs_idx].src);
                dest = bi_mkvec_v4i8(b, 
                                (nr > 0) ? bi_byte(bi_word(idx, 0), 0) : bi_imm_u8(0),
                                (nr > 1) ? bi_byte(bi_word(idx, 1), 0) : bi_imm_u8(0),
                                (nr > 2) ? bi_byte(bi_word(idx, 2), 0) : bi_imm_u8(0),
                                bi_imm_u8(0));
        }

        int ms_idx = nir_tex_instr_src_index(instr, nir_tex_src_ms_index);
        if (ms_idx >= 0 &&
            (!nir_src_is_const(instr->src[ms_idx].src) ||
             nir_src_as_uint(instr->src[ms_idx].src) != 0)) {
                dest = bi_lshift_or_i32(b,
                                bi_src_index(&instr->src[ms_idx].src), dest,
                                bi_imm_u8(24));
        }

        return dest;
}

static void
bi_emit_cube_coord(bi_builder *b, bi_index coord,
                    bi_index *face, bi_index *s, bi_index *t)
{
        /* Compute max { |x|, |y|, |z| } */
        bi_index cubeface1 = bi_cubeface1(b, coord,
                        bi_word(coord, 1), bi_word(coord, 2));

        /* Calculate packed exponent / face / infinity. In reality this reads
         * the destination from cubeface1 but that's handled by lowering */
        bi_instr *cubeface2 = bi_cubeface1_to(b, bi_temp(b->shader), coord,
                        bi_word(coord, 1), bi_word(coord, 2));
        cubeface2->op = BI_OPCODE_CUBEFACE2; /* XXX: DEEP VOODOO */

        /* Select coordinates */

        bi_index ssel = bi_cube_ssel(b, bi_word(coord, 2), coord,
                        cubeface2->dest[0]);

        bi_index tsel = bi_cube_tsel(b, bi_word(coord, 1), bi_word(coord, 2),
                        cubeface2->dest[0]);

        /* The OpenGL ES specification requires us to transform an input vector
         * (x, y, z) to the coordinate, given the selected S/T:
         *
         * (1/2 ((s / max{x,y,z}) + 1), 1/2 ((t / max{x, y, z}) + 1))
         *
         * We implement (s shown, t similar) in a form friendlier to FMA
         * instructions, and clamp coordinates at the end for correct
         * NaN/infinity handling:
         *
         * fsat(s * (0.5 * (1 / max{x, y, z})) + 0.5)
         *
         * Take the reciprocal of max{x, y, z}
         */

        bi_index rcp = bi_frcp_f32(b, cubeface1);

        /* Calculate 0.5 * (1.0 / max{x, y, z}) */
        bi_index fma1 = bi_fma_f32(b, rcp, bi_imm_f32(0.5f), bi_zero(),
                        BI_ROUND_NONE);

        /* Transform the coordinates */
        *s = bi_temp(b->shader);
        *t = bi_temp(b->shader);

        bi_instr *S = bi_fma_f32_to(b, *s, fma1, ssel, bi_imm_f32(0.5f),
                        BI_ROUND_NONE);
        bi_instr *T = bi_fma_f32_to(b, *t, fma1, tsel, bi_imm_f32(0.5f),
                        BI_ROUND_NONE);

        S->clamp = BI_CLAMP_CLAMP_0_1;
        T->clamp = BI_CLAMP_CLAMP_0_1;

        /* Cube face is stored in bit[29:31], we don't apply the shift here
         * because the TEXS_CUBE and TEXC instructions expect the face index to
         * be at this position.
         */
        *face = cubeface2->dest[0];
}

/* Emits a cube map descriptor, returning lower 32-bits and putting upper
 * 32-bits in passed pointer t */

static bi_index
bi_emit_texc_cube_coord(bi_builder *b, bi_index coord, bi_index *t)
{
        bi_index face, s;
        bi_emit_cube_coord(b, coord, &face, &s, t);

        bi_index and1 = bi_lshift_and_i32(b, face, bi_imm_u32(0xe0000000),
                        bi_imm_u8(0));

        bi_index and2 = bi_lshift_and_i32(b, s, bi_imm_u32(0x1fffffff),
                        bi_imm_u8(0));

        return bi_lshift_or_i32(b, and1, and2, bi_imm_u8(0));
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
bi_emit_texc(bi_builder *b, nir_tex_instr *instr)
{
        /* TODO: support more with other encodings */
        assert(instr->sampler_index < 16);

        /* TODO: support more ops */
        switch (instr->op) {
        case nir_texop_tex:
        case nir_texop_txl:
        case nir_texop_txb:
        case nir_texop_txf:
        case nir_texop_txf_ms:
                break;
        default:
                unreachable("Unsupported texture op");
        }

        struct bifrost_texture_operation desc = {
                .sampler_index_or_mode = instr->sampler_index,
                .index = instr->texture_index,
                .immediate_indices = 1, /* TODO */
                .op = bi_tex_op(instr->op),
                .offset_or_bias_disable = false, /* TODO */
                .shadow_or_clamp_disable = instr->is_shadow,
                .array = instr->is_array,
                .dimension = bifrost_tex_format(instr->sampler_dim),
                .format = bi_texture_format(instr->dest_type | nir_dest_bit_size(instr->dest), BI_CLAMP_NONE), /* TODO */
                .mask = 0xF,
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

        /* 32-bit indices to be allocated as consecutive staging registers */
        bi_index dregs[BIFROST_TEX_DREG_COUNT] = { };
        bi_index cx = bi_null(), cy = bi_null();

        for (unsigned i = 0; i < instr->num_srcs; ++i) {
                bi_index index = bi_src_index(&instr->src[i].src);
                unsigned sz = nir_src_bit_size(instr->src[i].src);
                ASSERTED nir_alu_type base = nir_tex_instr_src_type(instr, i);
                nir_alu_type T = base | sz;

                switch (instr->src[i].src_type) {
                case nir_tex_src_coord:
                        if (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE) {
                                cx = bi_emit_texc_cube_coord(b, index, &cy);
			} else {
                                unsigned components = nir_src_num_components(instr->src[i].src);

                                /* Copy XY (for 2D+) or XX (for 1D) */
                                cx = index;
                                cy = bi_word(index, MIN2(1, components - 1));

                                assert(components >= 1 && components <= 3);

                                if (components < 3) {
                                        /* nothing to do */
                                } else if (desc.array) {
                                        /* 2D array */
                                        dregs[BIFROST_TEX_DREG_ARRAY] =
                                                bi_emit_texc_array_index(b,
                                                                bi_word(index, 2), T);
                                } else {
                                        /* 3D */
                                        dregs[BIFROST_TEX_DREG_Z_COORD] =
                                                bi_word(index, 2);
                                }
                        }
                        break;

                case nir_tex_src_lod:
                        if (desc.op == BIFROST_TEX_OP_TEX &&
                            nir_src_is_const(instr->src[i].src) &&
                            nir_src_as_uint(instr->src[i].src) == 0) {
                                desc.lod_or_fetch = BIFROST_LOD_MODE_ZERO;
                        } else if (desc.op == BIFROST_TEX_OP_TEX) {
                                assert(base == nir_type_float);

                                assert(sz == 16 || sz == 32);
                                dregs[BIFROST_TEX_DREG_LOD] =
                                        bi_emit_texc_lod_88(b, index, sz == 16);
                                desc.lod_or_fetch = BIFROST_LOD_MODE_EXPLICIT;
                        } else {
                                assert(desc.op == BIFROST_TEX_OP_FETCH);
                                assert(base == nir_type_uint || base == nir_type_int);
                                assert(sz == 16 || sz == 32);

                                dregs[BIFROST_TEX_DREG_LOD] =
                                        bi_emit_texc_lod_cube(b, index);
                        }

                        break;

                case nir_tex_src_bias:
                        /* Upper 16-bits interpreted as a clamp, leave zero */
                        assert(desc.op == BIFROST_TEX_OP_TEX);
                        assert(base == nir_type_float);
                        assert(sz == 16 || sz == 32);
                        dregs[BIFROST_TEX_DREG_LOD] =
                                bi_emit_texc_lod_88(b, index, sz == 16);
                        desc.lod_or_fetch = BIFROST_LOD_MODE_BIAS;
                        break;

                case nir_tex_src_ms_index:
                case nir_tex_src_offset:
                        if (desc.offset_or_bias_disable)
                                break;

                        dregs[BIFROST_TEX_DREG_OFFSETMS] =
	                        bi_emit_texc_offset_ms_index(b, instr);
                        if (!bi_is_equiv(dregs[BIFROST_TEX_DREG_OFFSETMS], bi_zero()))
                                desc.offset_or_bias_disable = true;
                        break;

                case nir_tex_src_comparator:
                        dregs[BIFROST_TEX_DREG_SHADOW] = index;
                        break;

                default:
                        unreachable("Unhandled src type in texc emit");
                }
        }

        if (desc.op == BIFROST_TEX_OP_FETCH && bi_is_null(dregs[BIFROST_TEX_DREG_LOD])) {
                dregs[BIFROST_TEX_DREG_LOD] =
                        bi_emit_texc_lod_cube(b, bi_zero());
        }

        /* Allocate staging registers contiguously by compacting the array.
         * Index is not SSA (tied operands) */

        bi_index idx = bi_temp_reg(b->shader);
        unsigned sr_count = 0;

        for (unsigned i = 0; i < ARRAY_SIZE(dregs); ++i) {
                if (!bi_is_null(dregs[i]))
                        dregs[sr_count++] = dregs[i];
        }

        if (sr_count)
                bi_make_vec_to(b, idx, dregs, NULL, sr_count, 32);
        else
                bi_mov_i32_to(b, idx, bi_zero()); /* XXX: shouldn't be necessary */

        uint32_t desc_u = 0;
        memcpy(&desc_u, &desc, sizeof(desc_u));
        bi_texc_to(b, idx, idx, cx, cy, bi_imm_u32(desc_u), sr_count);

        /* Explicit copy to facilitate tied operands */
        bi_index srcs[4] = { idx, idx, idx, idx };
        unsigned channels[4] = { 0, 1, 2, 3 };
        bi_make_vec_to(b, bi_dest_index(&instr->dest), srcs, channels, 4, 32);
}

/* Simple textures ops correspond to NIR tex or txl with LOD = 0 on 2D (or cube
 * map, TODO) textures. Anything else needs a complete texture op. */

static bool
bi_is_simple_tex(nir_tex_instr *instr)
{
        if (instr->op != nir_texop_tex && instr->op != nir_texop_txl)
                return false;

        for (unsigned i = 0; i < instr->num_srcs; ++i) {
                if (instr->src[i].src_type != nir_tex_src_lod &&
                    instr->src[i].src_type != nir_tex_src_coord)
                        return false;
        }

        int lod_idx = nir_tex_instr_src_index(instr, nir_tex_src_lod);
        if (lod_idx < 0)
                return true;

        nir_src lod = instr->src[lod_idx].src;
        return nir_src_is_const(lod) && nir_src_as_uint(lod) == 0;
}

static void
bi_emit_tex(bi_builder *b, nir_tex_instr *instr)
{
        switch (instr->op) {
        case nir_texop_txs:
                bi_load_sysval(b, &instr->instr, 4, 0);
                return;
        case nir_texop_tex:
        case nir_texop_txl:
        case nir_texop_txb:
        case nir_texop_txf:
        case nir_texop_txf_ms:
                break;
        default:
                unreachable("Invalid texture operation");
        }

        nir_alu_type base = nir_alu_type_get_base_type(instr->dest_type);
        unsigned sz = nir_dest_bit_size(instr->dest);

        bool is_simple = bi_is_simple_tex(instr);
        bool is_2d = instr->sampler_dim == GLSL_SAMPLER_DIM_2D ||
                instr->sampler_dim == GLSL_SAMPLER_DIM_EXTERNAL;
        bool is_f = base == nir_type_float && (sz == 16 || sz == 32);

        if (is_simple && is_2d && is_f && !instr->is_shadow && !instr->is_array)
                bi_emit_texs(b, instr);
        else
                bi_emit_texc(b, instr);
}

static void
bi_emit_instr(bi_builder *b, struct nir_instr *instr)
{
        switch (instr->type) {
        case nir_instr_type_load_const:
                bi_emit_load_const(b, nir_instr_as_load_const(instr));
                break;

        case nir_instr_type_intrinsic:
                bi_emit_intrinsic(b, nir_instr_as_intrinsic(instr));
                break;

        case nir_instr_type_alu:
                bi_emit_alu(b, nir_instr_as_alu(instr));
                break;

        case nir_instr_type_tex:
                bi_emit_tex(b, nir_instr_as_tex(instr));
                break;

        case nir_instr_type_jump:
                bi_emit_jump(b, nir_instr_as_jump(instr));
                break;

        default:
                unreachable("should've been lowered");
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

        bi_builder _b = bi_init_builder(ctx);

        nir_foreach_instr(instr, block) {
                bi_emit_instr(&_b, instr);
                ++ctx->instruction_count;
        }

        return ctx->current_block;
}

/* Emits a direct branch based on a given condition. TODO: try to unwrap the
 * condition to optimize */

static bi_instr *
bi_branch(bi_builder *b, nir_src *condition, bool invert)
{
        return bi_branchz_i32_to(b, bi_null(), bi_src_index(condition),
                        bi_zero(), invert ? BI_CMPF_EQ : BI_CMPF_NE);
}

static bi_instr *
bi_jump(bi_builder *b, bi_block *target)
{
        bi_instr *I = bi_jump_to(b, bi_null(), bi_zero());
        I->branch_target = target;
        return I;
}

static void
emit_if(bi_context *ctx, nir_if *nif)
{
        bi_block *before_block = ctx->current_block;

        /* Speculatively emit the branch, but we can't fill it in until later */
        bi_builder _b = bi_init_builder(ctx);
        bi_instr *then_branch = bi_branch(&_b, &nif->condition, true);

        /* Emit the two subblocks. */
        bi_block *then_block = emit_cf_list(ctx, &nif->then_list);
        bi_block *end_then_block = ctx->current_block;

        /* Emit second block, and check if it's empty */

        int count_in = ctx->instruction_count;
        bi_block *else_block = emit_cf_list(ctx, &nif->else_list);
        bi_block *end_else_block = ctx->current_block;
        ctx->after_block = create_empty_block(ctx);

        /* Now that we have the subblocks emitted, fix up the branches */

        assert(then_block);
        assert(else_block);

        if (ctx->instruction_count == count_in) {
                then_branch->branch_target = ctx->after_block;
                pan_block_add_successor(&end_then_block->base, &ctx->after_block->base); /* fallthrough */
        } else {
                then_branch->branch_target = else_block;

                /* Emit a jump from the end of the then block to the end of the else */
                _b.cursor = bi_after_block(end_then_block);
                bi_instr *then_exit = bi_jump(&_b, ctx->after_block);

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
        bi_builder _b = bi_init_builder(ctx);
        bi_jump(&_b, ctx->continue_block);
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

/* shader-db stuff */

static void
bi_print_stats(bi_context *ctx, FILE *fp)
{
        unsigned nr_clauses = 0, nr_tuples = 0, nr_ins = 0;

        /* Count instructions, clauses, and tuples */
        bi_foreach_block(ctx, _block) {
                bi_block *block = (bi_block *) _block;

                bi_foreach_clause_in_block(block, clause) {
                        nr_clauses++;
                        nr_tuples += clause->bundle_count;

                        for (unsigned i = 0; i < clause->bundle_count; ++i) {
                                if (clause->bundles[i].fma)
                                        nr_ins++;

                                if (clause->bundles[i].add)
                                        nr_ins++;
                        }
                }
        }

        /* tuples = ((# of instructions) + (# of nops)) / 2 */
        unsigned nr_nops = (2 * nr_tuples) - nr_ins;

        /* In the future, we'll calculate thread count for v7. For now we
         * always use fewer threads than we should (v6 style) due to missing
         * piping, TODO: fix that for a nice perf win */
        unsigned nr_threads = 1;

        /* Dump stats */

        fprintf(stderr, "shader%d:%s - %s shader: "
                        "%u inst, %u nops, %u clauses, "
                        "%u threads, %u loops, "
                        "%u:%u spills:fills\n",
                        SHADER_DB_COUNT++,
                        ctx->nir->info.label ?: "",
                        ctx->is_blend ? "PAN_SHADER_BLEND" :
                        gl_shader_stage_name(ctx->stage),
                        nr_ins, nr_nops, nr_clauses,
                        nr_threads,
                        ctx->loop_count,
                        ctx->spills, ctx->fills);
}

static int
glsl_type_size(const struct glsl_type *type, bool bindless)
{
        return glsl_count_attribute_slots(type, false);
}

static unsigned
bi_lower_constant(bi_builder *b, bi_instr *ins, unsigned s, uint32_t *accum, unsigned cwords, bool allow_constant)
{
        uint32_t value = ins->src[s].value;

        /* Staging registers can't have constants */
        allow_constant &= !(s == 0 && bi_opcode_props[ins->op].sr_read);

        /* If we're allowed any inline constants, see if this one works */
        if (allow_constant) {
                for (unsigned i = 0; i < cwords; ++i) {
                        if (value == accum[i])
                                return cwords;
                }

                if (value == 0 && !bi_opcode_props[ins->op].add)
                        return cwords;

                if (cwords < 2) {
                        accum[cwords] = value;
                        return cwords + 1;
                }
        }

        /* should be const folded */
        assert(!ins->src[s].abs && !ins->src[s].neg);
        enum bi_swizzle old_swizzle = ins->src[s].swizzle;

        b->cursor = bi_before_instr(ins);
        ins->src[s] = bi_mov_i32(b, bi_imm_u32(value));
        ins->src[s].swizzle = old_swizzle;
        return cwords;
}

static void
bi_lower_fau(bi_context *ctx, bi_block *block)
{
        bi_builder b = bi_init_builder(ctx);

        bi_foreach_instr_in_block_safe(block, _ins) {
                bi_instr *ins = (bi_instr *) _ins;
                uint32_t constants[2];
                unsigned cwords = 0;

                bi_foreach_src(ins, s) {
                        if (ins->src[s].type == BI_INDEX_CONSTANT)
                                cwords = bi_lower_constant(&b, ins, s, constants, cwords, true);
                }
        }
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
                NIR_PASS(progress, nir, nir_lower_undef_to_zero);

                NIR_PASS(progress, nir, nir_opt_loop_unroll,
                         nir_var_shader_in |
                         nir_var_shader_out |
                         nir_var_function_temp);
        } while (progress);

        /* We need to cleanup after each iteration of late algebraic
         * optimizations, since otherwise NIR can produce weird edge cases
         * (like fneg of a constant) which we don't handle */
        bool late_algebraic = true;
        while (late_algebraic) {
                late_algebraic = false;
                NIR_PASS(late_algebraic, nir, nir_opt_algebraic_late);
                NIR_PASS(progress, nir, nir_opt_constant_folding);
                NIR_PASS(progress, nir, nir_copy_prop);
                NIR_PASS(progress, nir, nir_opt_dce);
                NIR_PASS(progress, nir, nir_opt_cse);
        }

        NIR_PASS(progress, nir, nir_lower_bool_to_int32);
        NIR_PASS(progress, nir, bifrost_nir_lower_algebraic_late);
        NIR_PASS(progress, nir, nir_lower_alu_to_scalar, NULL, NULL);
        NIR_PASS(progress, nir, nir_lower_load_const_to_scalar);

        /* Take us out of SSA */
        NIR_PASS(progress, nir, nir_lower_locals_to_regs);
        NIR_PASS(progress, nir, nir_move_vec_src_uses_to_dest);
        NIR_PASS(progress, nir, nir_convert_from_ssa, true);
}

panfrost_program *
bifrost_compile_shader_nir(void *mem_ctx, nir_shader *nir,
                           const struct panfrost_compile_inputs *inputs)
{
        panfrost_program *program = rzalloc(mem_ctx, panfrost_program);

        bifrost_debug = debug_get_option_bifrost_debug();

        bi_context *ctx = rzalloc(NULL, bi_context);
        ctx->nir = nir;
        ctx->stage = nir->info.stage;
        ctx->quirks = bifrost_get_quirks(inputs->gpu_id);
        ctx->arch = inputs->gpu_id >> 12;
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
        NIR_PASS_V(nir, pan_nir_lower_zs_store);
        // TODO: re-enable when fp16 is flipped on
        // NIR_PASS_V(nir, nir_lower_mediump_outputs);

        bi_optimize_nir(nir);

        NIR_PASS_V(nir, pan_nir_reorder_writeout);

        if (bifrost_debug & BIFROST_DBG_SHADERS && !nir->info.internal) {
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
        }

        bool progress = false;

        do {
                progress = false;

                bi_foreach_block(ctx, _block) {
                        bi_block *block = (bi_block *) _block;
                        progress |= bi_opt_dead_code_eliminate(ctx, block);
                }
        } while(progress);

        bi_foreach_block(ctx, _block) {
                bi_block *block = (bi_block *) _block;
                bi_lower_fau(ctx, block);
        }

        if (bifrost_debug & BIFROST_DBG_SHADERS && !nir->info.internal)
                bi_print_shader(ctx, stdout);
        bi_schedule(ctx);
        bi_register_allocate(ctx);
        if (bifrost_debug & BIFROST_DBG_SHADERS && !nir->info.internal)
                bi_print_shader(ctx, stdout);

        util_dynarray_init(&program->compiled, NULL);
        bi_pack(ctx, &program->compiled);

        memcpy(program->blend_ret_offsets, ctx->blend_ret_offsets, sizeof(program->blend_ret_offsets));

        if (bifrost_debug & BIFROST_DBG_SHADERS && !nir->info.internal) {
                disassemble_bifrost(stdout, program->compiled.data,
                                program->compiled.size,
                                bifrost_debug & BIFROST_DBG_VERBOSE);
        }

        program->tls_size = ctx->tls_size;

        if ((bifrost_debug & BIFROST_DBG_SHADERDB || inputs->shaderdb) &&
            !nir->info.internal) {
                bi_print_stats(ctx, stderr);
        }

        ralloc_free(ctx);

        return program;
}
