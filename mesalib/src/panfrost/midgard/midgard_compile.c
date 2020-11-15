/*
 * Copyright (C) 2018-2019 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright (C) 2019-2020 Collabora, Ltd.
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
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <err.h>

#include "main/mtypes.h"
#include "compiler/glsl/glsl_to_nir.h"
#include "compiler/nir_types.h"
#include "compiler/nir/nir_builder.h"
#include "util/half_float.h"
#include "util/u_math.h"
#include "util/u_debug.h"
#include "util/u_dynarray.h"
#include "util/list.h"
#include "main/mtypes.h"

#include "midgard.h"
#include "midgard_nir.h"
#include "midgard_compile.h"
#include "midgard_ops.h"
#include "helpers.h"
#include "compiler.h"
#include "midgard_quirks.h"
#include "panfrost-quirks.h"
#include "panfrost/util/pan_lower_framebuffer.h"

#include "disassemble.h"

static const struct debug_named_value debug_options[] = {
        {"msgs",      MIDGARD_DBG_MSGS,		"Print debug messages"},
        {"shaders",   MIDGARD_DBG_SHADERS,	"Dump shaders in NIR and MIR"},
        {"shaderdb",  MIDGARD_DBG_SHADERDB,     "Prints shader-db statistics"},
        DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(midgard_debug, "MIDGARD_MESA_DEBUG", debug_options, 0)

unsigned SHADER_DB_COUNT = 0;

int midgard_debug = 0;

#define DBG(fmt, ...) \
		do { if (midgard_debug & MIDGARD_DBG_MSGS) \
			fprintf(stderr, "%s:%d: "fmt, \
				__FUNCTION__, __LINE__, ##__VA_ARGS__); } while (0)
static midgard_block *
create_empty_block(compiler_context *ctx)
{
        midgard_block *blk = rzalloc(ctx, midgard_block);

        blk->base.predecessors = _mesa_set_create(blk,
                        _mesa_hash_pointer,
                        _mesa_key_pointer_equal);

        blk->base.name = ctx->block_source_count++;

        return blk;
}

static void
schedule_barrier(compiler_context *ctx)
{
        midgard_block *temp = ctx->after_block;
        ctx->after_block = create_empty_block(ctx);
        ctx->block_count++;
        list_addtail(&ctx->after_block->base.link, &ctx->blocks);
        list_inithead(&ctx->after_block->base.instructions);
        pan_block_add_successor(&ctx->current_block->base, &ctx->after_block->base);
        ctx->current_block = ctx->after_block;
        ctx->after_block = temp;
}

/* Helpers to generate midgard_instruction's using macro magic, since every
 * driver seems to do it that way */

#define EMIT(op, ...) emit_mir_instruction(ctx, v_##op(__VA_ARGS__));

#define M_LOAD_STORE(name, store, T) \
	static midgard_instruction m_##name(unsigned ssa, unsigned address) { \
		midgard_instruction i = { \
			.type = TAG_LOAD_STORE_4, \
                        .mask = 0xF, \
                        .dest = ~0, \
                        .src = { ~0, ~0, ~0, ~0 }, \
                        .swizzle = SWIZZLE_IDENTITY_4, \
                        .op = midgard_op_##name, \
			.load_store = { \
				.address = address \
			} \
		}; \
                \
                if (store) { \
                        i.src[0] = ssa; \
                        i.src_types[0] = T; \
                        i.dest_type = T; \
                } else { \
                        i.dest = ssa; \
                        i.dest_type = T; \
                } \
		return i; \
	}

#define M_LOAD(name, T) M_LOAD_STORE(name, false, T)
#define M_STORE(name, T) M_LOAD_STORE(name, true, T)

M_LOAD(ld_attr_32, nir_type_uint32);
M_LOAD(ld_vary_32, nir_type_uint32);
M_LOAD(ld_ubo_int4, nir_type_uint32);
M_LOAD(ld_int4, nir_type_uint32);
M_STORE(st_int4, nir_type_uint32);
M_LOAD(ld_color_buffer_32u, nir_type_uint32);
M_LOAD(ld_color_buffer_as_fp16, nir_type_float16);
M_LOAD(ld_color_buffer_as_fp32, nir_type_float32);
M_STORE(st_vary_32, nir_type_uint32);
M_LOAD(ld_cubemap_coords, nir_type_uint32);
M_LOAD(ld_compute_id, nir_type_uint32);

static midgard_instruction
v_branch(bool conditional, bool invert)
{
        midgard_instruction ins = {
                .type = TAG_ALU_4,
                .unit = ALU_ENAB_BRANCH,
                .compact_branch = true,
                .branch = {
                        .conditional = conditional,
                        .invert_conditional = invert
                },
                .dest = ~0,
                .src = { ~0, ~0, ~0, ~0 },
        };

        return ins;
}

static void
attach_constants(compiler_context *ctx, midgard_instruction *ins, void *constants, int name)
{
        ins->has_constants = true;
        memcpy(&ins->constants, constants, 16);
}

static int
glsl_type_size(const struct glsl_type *type, bool bindless)
{
        return glsl_count_attribute_slots(type, false);
}

/* Lower fdot2 to a vector multiplication followed by channel addition  */
static bool
midgard_nir_lower_fdot2_instr(nir_builder *b, nir_instr *instr, void *data)
{
        if (instr->type != nir_instr_type_alu)
                return false;

        nir_alu_instr *alu = nir_instr_as_alu(instr);
        if (alu->op != nir_op_fdot2)
                return false;

        b->cursor = nir_before_instr(&alu->instr);

        nir_ssa_def *src0 = nir_ssa_for_alu_src(b, alu, 0);
        nir_ssa_def *src1 = nir_ssa_for_alu_src(b, alu, 1);

        nir_ssa_def *product = nir_fmul(b, src0, src1);

        nir_ssa_def *sum = nir_fadd(b,
                                    nir_channel(b, product, 0),
                                    nir_channel(b, product, 1));

        /* Replace the fdot2 with this sum */
        nir_ssa_def_rewrite_uses(&alu->dest.dest.ssa, nir_src_for_ssa(sum));

        return true;
}

static bool
midgard_nir_lower_fdot2(nir_shader *shader)
{
        return nir_shader_instructions_pass(shader,
                                            midgard_nir_lower_fdot2_instr,
                                            nir_metadata_block_index | nir_metadata_dominance,
                                            NULL);
}

static bool
mdg_is_64(const nir_instr *instr, const void *_unused)
{
        const nir_alu_instr *alu = nir_instr_as_alu(instr);

        if (nir_dest_bit_size(alu->dest.dest) == 64)
                return true;

        switch (alu->op) {
        case nir_op_umul_high:
        case nir_op_imul_high:
                return true;
        default:
                return false;
        }
}

/* Flushes undefined values to zero */

static void
optimise_nir(nir_shader *nir, unsigned quirks, bool is_blend)
{
        bool progress;
        unsigned lower_flrp =
                (nir->options->lower_flrp16 ? 16 : 0) |
                (nir->options->lower_flrp32 ? 32 : 0) |
                (nir->options->lower_flrp64 ? 64 : 0);

        NIR_PASS(progress, nir, nir_lower_regs_to_ssa);
        NIR_PASS(progress, nir, nir_lower_idiv, nir_lower_idiv_fast);

        nir_lower_tex_options lower_tex_options = {
                .lower_txs_lod = true,
                .lower_txp = ~0,
                .lower_tex_without_implicit_lod =
                        (quirks & MIDGARD_EXPLICIT_LOD),
                .lower_tg4_broadcom_swizzle = true,

                /* TODO: we have native gradient.. */
                .lower_txd = true,
        };

        NIR_PASS(progress, nir, nir_lower_tex, &lower_tex_options);

        /* Must lower fdot2 after tex is lowered */
        NIR_PASS(progress, nir, midgard_nir_lower_fdot2);

        /* T720 is broken. */

        if (quirks & MIDGARD_BROKEN_LOD)
                NIR_PASS_V(nir, midgard_nir_lod_errata);

        NIR_PASS(progress, nir, midgard_nir_lower_algebraic_early);

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
                NIR_PASS(progress, nir, nir_undef_to_zero);

                NIR_PASS(progress, nir, nir_opt_loop_unroll,
                         nir_var_shader_in |
                         nir_var_shader_out |
                         nir_var_function_temp);

                NIR_PASS(progress, nir, nir_opt_vectorize, NULL, NULL);
        } while (progress);

        NIR_PASS_V(nir, nir_lower_alu_to_scalar, mdg_is_64, NULL);

        /* Run after opts so it can hit more */
        if (!is_blend)
                NIR_PASS(progress, nir, nir_fuse_io_16);

        /* Must be run at the end to prevent creation of fsin/fcos ops */
        NIR_PASS(progress, nir, midgard_nir_scale_trig);

        do {
                progress = false;

                NIR_PASS(progress, nir, nir_opt_dce);
                NIR_PASS(progress, nir, nir_opt_algebraic);
                NIR_PASS(progress, nir, nir_opt_constant_folding);
                NIR_PASS(progress, nir, nir_copy_prop);
        } while (progress);

        NIR_PASS(progress, nir, nir_opt_algebraic_late);
        NIR_PASS(progress, nir, nir_opt_algebraic_distribute_src_mods);

        /* We implement booleans as 32-bit 0/~0 */
        NIR_PASS(progress, nir, nir_lower_bool_to_int32);

        /* Now that booleans are lowered, we can run out late opts */
        NIR_PASS(progress, nir, midgard_nir_lower_algebraic_late);
        NIR_PASS(progress, nir, midgard_nir_cancel_inot);

        NIR_PASS(progress, nir, nir_copy_prop);
        NIR_PASS(progress, nir, nir_opt_dce);

        /* Take us out of SSA */
        NIR_PASS(progress, nir, nir_lower_locals_to_regs);
        NIR_PASS(progress, nir, nir_convert_from_ssa, true);

        /* We are a vector architecture; write combine where possible */
        NIR_PASS(progress, nir, nir_move_vec_src_uses_to_dest);
        NIR_PASS(progress, nir, nir_lower_vec_to_movs);

        NIR_PASS(progress, nir, nir_opt_dce);
}

/* Do not actually emit a load; instead, cache the constant for inlining */

static void
emit_load_const(compiler_context *ctx, nir_load_const_instr *instr)
{
        nir_ssa_def def = instr->def;

        midgard_constants *consts = rzalloc(NULL, midgard_constants);

        assert(instr->def.num_components * instr->def.bit_size <= sizeof(*consts) * 8);

#define RAW_CONST_COPY(bits)                                         \
        nir_const_value_to_array(consts->u##bits, instr->value,      \
                                 instr->def.num_components, u##bits)

        switch (instr->def.bit_size) {
        case 64:
                RAW_CONST_COPY(64);
                break;
        case 32:
                RAW_CONST_COPY(32);
                break;
        case 16:
                RAW_CONST_COPY(16);
                break;
        case 8:
                RAW_CONST_COPY(8);
                break;
        default:
                unreachable("Invalid bit_size for load_const instruction\n");
        }

        /* Shifted for SSA, +1 for off-by-one */
        _mesa_hash_table_u64_insert(ctx->ssa_constants, (def.index << 1) + 1, consts);
}

/* Normally constants are embedded implicitly, but for I/O and such we have to
 * explicitly emit a move with the constant source */

static void
emit_explicit_constant(compiler_context *ctx, unsigned node, unsigned to)
{
        void *constant_value = _mesa_hash_table_u64_search(ctx->ssa_constants, node + 1);

        if (constant_value) {
                midgard_instruction ins = v_mov(SSA_FIXED_REGISTER(REGISTER_CONSTANT), to);
                attach_constants(ctx, &ins, constant_value, node + 1);
                emit_mir_instruction(ctx, ins);
        }
}

static bool
nir_is_non_scalar_swizzle(nir_alu_src *src, unsigned nr_components)
{
        unsigned comp = src->swizzle[0];

        for (unsigned c = 1; c < nr_components; ++c) {
                if (src->swizzle[c] != comp)
                        return true;
        }

        return false;
}

#define ATOMIC_CASE_IMPL(ctx, instr, nir, op, is_shared) \
        case nir_intrinsic_##nir: \
                emit_atomic(ctx, instr, is_shared, midgard_op_##op); \
                break;

#define ATOMIC_CASE(ctx, instr, nir, op) \
        ATOMIC_CASE_IMPL(ctx, instr, shared_atomic_##nir, atomic_##op, true); \
        ATOMIC_CASE_IMPL(ctx, instr, global_atomic_##nir, atomic_##op, false);

#define ALU_CASE(nir, _op) \
	case nir_op_##nir: \
		op = midgard_alu_op_##_op; \
                assert(src_bitsize == dst_bitsize); \
		break;

#define ALU_CASE_RTZ(nir, _op) \
	case nir_op_##nir: \
		op = midgard_alu_op_##_op; \
                roundmode = MIDGARD_RTZ; \
		break;

#define ALU_CHECK_CMP() \
                assert(src_bitsize == 16 || src_bitsize == 32); \
                assert(dst_bitsize == 16 || dst_bitsize == 32); \

#define ALU_CASE_BCAST(nir, _op, count) \
        case nir_op_##nir: \
                op = midgard_alu_op_##_op; \
                broadcast_swizzle = count; \
                ALU_CHECK_CMP(); \
                break;

#define ALU_CASE_CMP(nir, _op) \
	case nir_op_##nir: \
		op = midgard_alu_op_##_op; \
                ALU_CHECK_CMP(); \
                break;

/* Compare mir_lower_invert */
static bool
nir_accepts_inot(nir_op op, unsigned src)
{
        switch (op) {
        case nir_op_ior:
        case nir_op_iand: /* TODO: b2f16 */
        case nir_op_ixor:
                return true;
        case nir_op_b32csel:
                /* Only the condition */
                return (src == 0);
        default:
                return false;
        }
}

static bool
mir_accept_dest_mod(compiler_context *ctx, nir_dest **dest, nir_op op)
{
        if (pan_has_dest_mod(dest, op)) {
                assert((*dest)->is_ssa);
                BITSET_SET(ctx->already_emitted, (*dest)->ssa.index);
                return true;
        }

        return false;
}

/* Look for floating point mods. We have the mods fsat, fsat_signed,
 * and fpos. We also have the relations (note 3 * 2 = 6 cases):
 *
 * fsat_signed(fpos(x)) = fsat(x)
 * fsat_signed(fsat(x)) = fsat(x)
 * fpos(fsat_signed(x)) = fsat(x)
 * fpos(fsat(x)) = fsat(x)
 * fsat(fsat_signed(x)) = fsat(x)
 * fsat(fpos(x)) = fsat(x)
 *
 * So by cases any composition of output modifiers is equivalent to
 * fsat alone.
 */
static unsigned
mir_determine_float_outmod(compiler_context *ctx, nir_dest **dest, unsigned prior_outmod)
{
        bool fpos = mir_accept_dest_mod(ctx, dest, nir_op_fclamp_pos);
        bool fsat = mir_accept_dest_mod(ctx, dest, nir_op_fsat);
        bool ssat = mir_accept_dest_mod(ctx, dest, nir_op_fsat_signed);
        bool prior = (prior_outmod != midgard_outmod_none);
        int count = (int) prior + (int) fpos + (int) ssat + (int) fsat;

        return ((count > 1) || fsat) ? midgard_outmod_sat :
                                fpos ? midgard_outmod_pos :
                                ssat ? midgard_outmod_sat_signed :
                                prior_outmod;
}

static void
mir_copy_src(midgard_instruction *ins, nir_alu_instr *instr, unsigned i, unsigned to, bool *abs, bool *neg, bool *not, enum midgard_roundmode *roundmode, bool is_int, unsigned bcast_count)
{
        nir_alu_src src = instr->src[i];

        if (!is_int) {
                if (pan_has_source_mod(&src, nir_op_fneg))
                        *neg = !(*neg);

                if (pan_has_source_mod(&src, nir_op_fabs))
                        *abs = true;
        }

        if (nir_accepts_inot(instr->op, i) && pan_has_source_mod(&src, nir_op_inot))
                *not = true;

        if (roundmode) {
                if (pan_has_source_mod(&src, nir_op_fround_even))
                        *roundmode = MIDGARD_RTE;

                if (pan_has_source_mod(&src, nir_op_ftrunc))
                        *roundmode = MIDGARD_RTZ;

                if (pan_has_source_mod(&src, nir_op_ffloor))
                        *roundmode = MIDGARD_RTN;

                if (pan_has_source_mod(&src, nir_op_fceil))
                        *roundmode = MIDGARD_RTP;
        }

        unsigned bits = nir_src_bit_size(src.src);

        ins->src[to] = nir_src_index(NULL, &src.src);
        ins->src_types[to] = nir_op_infos[instr->op].input_types[i] | bits;

        for (unsigned c = 0; c < NIR_MAX_VEC_COMPONENTS; ++c) {
                ins->swizzle[to][c] = src.swizzle[
                        (!bcast_count || c < bcast_count) ? c :
                                (bcast_count - 1)];
        }
}

/* Midgard features both fcsel and icsel, depending on whether you want int or
 * float modifiers. NIR's csel is typeless, so we want a heuristic to guess if
 * we should emit an int or float csel depending on what modifiers could be
 * placed. In the absense of modifiers, this is probably arbitrary. */

static bool
mir_is_bcsel_float(nir_alu_instr *instr)
{
        nir_op intmods[] = {
                nir_op_i2i8, nir_op_i2i16,
                nir_op_i2i32, nir_op_i2i64
        };

        nir_op floatmods[] = {
                nir_op_fabs, nir_op_fneg,
                nir_op_f2f16, nir_op_f2f32,
                nir_op_f2f64
        };

        nir_op floatdestmods[] = {
                nir_op_fsat, nir_op_fsat_signed, nir_op_fclamp_pos,
                nir_op_f2f16, nir_op_f2f32
        };

        signed score = 0;

        for (unsigned i = 1; i < 3; ++i) {
                nir_alu_src s = instr->src[i];
                for (unsigned q = 0; q < ARRAY_SIZE(intmods); ++q) {
                        if (pan_has_source_mod(&s, intmods[q]))
                                score--;
                }
        }

        for (unsigned i = 1; i < 3; ++i) {
                nir_alu_src s = instr->src[i];
                for (unsigned q = 0; q < ARRAY_SIZE(floatmods); ++q) {
                        if (pan_has_source_mod(&s, floatmods[q]))
                                score++;
                }
        }

        for (unsigned q = 0; q < ARRAY_SIZE(floatdestmods); ++q) {
                nir_dest *dest = &instr->dest.dest;
                if (pan_has_dest_mod(&dest, floatdestmods[q]))
                        score++;
        }

        return (score > 0);
}

static void
emit_alu(compiler_context *ctx, nir_alu_instr *instr)
{
        nir_dest *dest = &instr->dest.dest;

        if (dest->is_ssa && BITSET_TEST(ctx->already_emitted, dest->ssa.index))
                return;

        /* Derivatives end up emitted on the texture pipe, not the ALUs. This
         * is handled elsewhere */

        if (instr->op == nir_op_fddx || instr->op == nir_op_fddy) {
                midgard_emit_derivatives(ctx, instr);
                return;
        }

        bool is_ssa = dest->is_ssa;

        unsigned nr_components = nir_dest_num_components(*dest);
        unsigned nr_inputs = nir_op_infos[instr->op].num_inputs;
        unsigned op = 0;

        /* Number of components valid to check for the instruction (the rest
         * will be forced to the last), or 0 to use as-is. Relevant as
         * ball-type instructions have a channel count in NIR but are all vec4
         * in Midgard */

        unsigned broadcast_swizzle = 0;

        /* Should we swap arguments? */
        bool flip_src12 = false;

        ASSERTED unsigned src_bitsize = nir_src_bit_size(instr->src[0].src);
        ASSERTED unsigned dst_bitsize = nir_dest_bit_size(*dest);

        enum midgard_roundmode roundmode = MIDGARD_RTE;

        switch (instr->op) {
                ALU_CASE(fadd, fadd);
                ALU_CASE(fmul, fmul);
                ALU_CASE(fmin, fmin);
                ALU_CASE(fmax, fmax);
                ALU_CASE(imin, imin);
                ALU_CASE(imax, imax);
                ALU_CASE(umin, umin);
                ALU_CASE(umax, umax);
                ALU_CASE(ffloor, ffloor);
                ALU_CASE(fround_even, froundeven);
                ALU_CASE(ftrunc, ftrunc);
                ALU_CASE(fceil, fceil);
                ALU_CASE(fdot3, fdot3);
                ALU_CASE(fdot4, fdot4);
                ALU_CASE(iadd, iadd);
                ALU_CASE(isub, isub);
                ALU_CASE(imul, imul);
                ALU_CASE(imul_high, imul);
                ALU_CASE(umul_high, imul);

                /* Zero shoved as second-arg */
                ALU_CASE(iabs, iabsdiff);

                ALU_CASE(uabs_isub, iabsdiff);
                ALU_CASE(uabs_usub, uabsdiff);

                ALU_CASE(mov, imov);

                ALU_CASE_CMP(feq32, feq);
                ALU_CASE_CMP(fneu32, fne);
                ALU_CASE_CMP(flt32, flt);
                ALU_CASE_CMP(ieq32, ieq);
                ALU_CASE_CMP(ine32, ine);
                ALU_CASE_CMP(ilt32, ilt);
                ALU_CASE_CMP(ult32, ult);

                /* We don't have a native b2f32 instruction. Instead, like many
                 * GPUs, we exploit booleans as 0/~0 for false/true, and
                 * correspondingly AND
                 * by 1.0 to do the type conversion. For the moment, prime us
                 * to emit:
                 *
                 * iand [whatever], #0
                 *
                 * At the end of emit_alu (as MIR), we'll fix-up the constant
                 */

                ALU_CASE_CMP(b2f32, iand);
                ALU_CASE_CMP(b2f16, iand);
                ALU_CASE_CMP(b2i32, iand);

                /* Likewise, we don't have a dedicated f2b32 instruction, but
                 * we can do a "not equal to 0.0" test. */

                ALU_CASE_CMP(f2b32, fne);
                ALU_CASE_CMP(i2b32, ine);

                ALU_CASE(frcp, frcp);
                ALU_CASE(frsq, frsqrt);
                ALU_CASE(fsqrt, fsqrt);
                ALU_CASE(fexp2, fexp2);
                ALU_CASE(flog2, flog2);

                ALU_CASE_RTZ(f2i64, f2i_rte);
                ALU_CASE_RTZ(f2u64, f2u_rte);
                ALU_CASE_RTZ(i2f64, i2f_rte);
                ALU_CASE_RTZ(u2f64, u2f_rte);

                ALU_CASE_RTZ(f2i32, f2i_rte);
                ALU_CASE_RTZ(f2u32, f2u_rte);
                ALU_CASE_RTZ(i2f32, i2f_rte);
                ALU_CASE_RTZ(u2f32, u2f_rte);

                ALU_CASE_RTZ(f2i8, f2i_rte);
                ALU_CASE_RTZ(f2u8, f2u_rte);

                ALU_CASE_RTZ(f2i16, f2i_rte);
                ALU_CASE_RTZ(f2u16, f2u_rte);
                ALU_CASE_RTZ(i2f16, i2f_rte);
                ALU_CASE_RTZ(u2f16, u2f_rte);

                ALU_CASE(fsin, fsin);
                ALU_CASE(fcos, fcos);

                /* We'll get 0 in the second arg, so:
                 * ~a = ~(a | 0) = nor(a, 0) */
                ALU_CASE(inot, inor);
                ALU_CASE(iand, iand);
                ALU_CASE(ior, ior);
                ALU_CASE(ixor, ixor);
                ALU_CASE(ishl, ishl);
                ALU_CASE(ishr, iasr);
                ALU_CASE(ushr, ilsr);

                ALU_CASE_BCAST(b32all_fequal2, fball_eq, 2);
                ALU_CASE_BCAST(b32all_fequal3, fball_eq, 3);
                ALU_CASE_CMP(b32all_fequal4, fball_eq);

                ALU_CASE_BCAST(b32any_fnequal2, fbany_neq, 2);
                ALU_CASE_BCAST(b32any_fnequal3, fbany_neq, 3);
                ALU_CASE_CMP(b32any_fnequal4, fbany_neq);

                ALU_CASE_BCAST(b32all_iequal2, iball_eq, 2);
                ALU_CASE_BCAST(b32all_iequal3, iball_eq, 3);
                ALU_CASE_CMP(b32all_iequal4, iball_eq);

                ALU_CASE_BCAST(b32any_inequal2, ibany_neq, 2);
                ALU_CASE_BCAST(b32any_inequal3, ibany_neq, 3);
                ALU_CASE_CMP(b32any_inequal4, ibany_neq);

                /* Source mods will be shoved in later */
                ALU_CASE(fabs, fmov);
                ALU_CASE(fneg, fmov);
                ALU_CASE(fsat, fmov);
                ALU_CASE(fsat_signed, fmov);
                ALU_CASE(fclamp_pos, fmov);

        /* For size conversion, we use a move. Ideally though we would squash
         * these ops together; maybe that has to happen after in NIR as part of
         * propagation...? An earlier algebraic pass ensured we step down by
         * only / exactly one size. If stepping down, we use a dest override to
         * reduce the size; if stepping up, we use a larger-sized move with a
         * half source and a sign/zero-extension modifier */

        case nir_op_i2i8:
        case nir_op_i2i16:
        case nir_op_i2i32:
        case nir_op_i2i64:
        case nir_op_u2u8:
        case nir_op_u2u16:
        case nir_op_u2u32:
        case nir_op_u2u64:
        case nir_op_f2f16:
        case nir_op_f2f32:
        case nir_op_f2f64: {
                if (instr->op == nir_op_f2f16 || instr->op == nir_op_f2f32 ||
                    instr->op == nir_op_f2f64)
                        op = midgard_alu_op_fmov;
                else
                        op = midgard_alu_op_imov;

                break;
        }

        /* For greater-or-equal, we lower to less-or-equal and flip the
         * arguments */

        case nir_op_fge:
        case nir_op_fge32:
        case nir_op_ige32:
        case nir_op_uge32: {
                op =
                        instr->op == nir_op_fge   ? midgard_alu_op_fle :
                        instr->op == nir_op_fge32 ? midgard_alu_op_fle :
                        instr->op == nir_op_ige32 ? midgard_alu_op_ile :
                        instr->op == nir_op_uge32 ? midgard_alu_op_ule :
                        0;

                flip_src12 = true;
                ALU_CHECK_CMP();
                break;
        }

        case nir_op_b32csel: {
                bool mixed = nir_is_non_scalar_swizzle(&instr->src[0], nr_components);
                bool is_float = mir_is_bcsel_float(instr);
                op = is_float ?
                        (mixed ? midgard_alu_op_fcsel_v : midgard_alu_op_fcsel) :
                        (mixed ? midgard_alu_op_icsel_v : midgard_alu_op_icsel);

                break;
        }

        case nir_op_unpack_32_2x16:
        case nir_op_unpack_32_4x8:
        case nir_op_pack_32_2x16:
        case nir_op_pack_32_4x8: {
                op = midgard_alu_op_imov;
                break;
        }

        default:
                DBG("Unhandled ALU op %s\n", nir_op_infos[instr->op].name);
                assert(0);
                return;
        }

        /* Promote imov to fmov if it might help inline a constant */
        if (op == midgard_alu_op_imov && nir_src_is_const(instr->src[0].src)
                        && nir_src_bit_size(instr->src[0].src) == 32
                        && nir_is_same_comp_swizzle(instr->src[0].swizzle,
                                nir_src_num_components(instr->src[0].src))) {
                op = midgard_alu_op_fmov;
        }

        /* Midgard can perform certain modifiers on output of an ALU op */

        unsigned outmod = 0;
        bool is_int = midgard_is_integer_op(op);

        if (instr->op == nir_op_umul_high || instr->op == nir_op_imul_high) {
                outmod = midgard_outmod_int_high;
        } else if (midgard_is_integer_out_op(op)) {
                outmod = midgard_outmod_int_wrap;
        } else if (instr->op == nir_op_fsat) {
                outmod = midgard_outmod_sat;
        } else if (instr->op == nir_op_fsat_signed) {
                outmod = midgard_outmod_sat_signed;
        } else if (instr->op == nir_op_fclamp_pos) {
                outmod = midgard_outmod_pos;
        }

        /* Fetch unit, quirks, etc information */
        unsigned opcode_props = alu_opcode_props[op].props;
        bool quirk_flipped_r24 = opcode_props & QUIRK_FLIPPED_R24;

        if (!midgard_is_integer_out_op(op)) {
                outmod = mir_determine_float_outmod(ctx, &dest, outmod);
        }

        midgard_instruction ins = {
                .type = TAG_ALU_4,
                .dest = nir_dest_index(dest),
                .dest_type = nir_op_infos[instr->op].output_type
                        | nir_dest_bit_size(*dest),
                .roundmode = roundmode,
        };

        enum midgard_roundmode *roundptr = (opcode_props & MIDGARD_ROUNDS) ?
                &ins.roundmode : NULL;

        for (unsigned i = nr_inputs; i < ARRAY_SIZE(ins.src); ++i)
                ins.src[i] = ~0;

        if (quirk_flipped_r24) {
                ins.src[0] = ~0;
                mir_copy_src(&ins, instr, 0, 1, &ins.src_abs[1], &ins.src_neg[1], &ins.src_invert[1], roundptr, is_int, broadcast_swizzle);
        } else {
                for (unsigned i = 0; i < nr_inputs; ++i) {
                        unsigned to = i;

                        if (instr->op == nir_op_b32csel) {
                                /* The condition is the first argument; move
                                 * the other arguments up one to be a binary
                                 * instruction for Midgard with the condition
                                 * last */

                                if (i == 0)
                                        to = 2;
                                else if (flip_src12)
                                        to = 2 - i;
                                else
                                        to = i - 1;
                        } else if (flip_src12) {
                                to = 1 - to;
                        }

                        mir_copy_src(&ins, instr, i, to, &ins.src_abs[to], &ins.src_neg[to], &ins.src_invert[to], roundptr, is_int, broadcast_swizzle);

                        /* (!c) ? a : b = c ? b : a */
                        if (instr->op == nir_op_b32csel && ins.src_invert[2]) {
                                ins.src_invert[2] = false;
                                flip_src12 ^= true;
                        }
                }
        }

        if (instr->op == nir_op_fneg || instr->op == nir_op_fabs) {
                /* Lowered to move */
                if (instr->op == nir_op_fneg)
                        ins.src_neg[1] ^= true;

                if (instr->op == nir_op_fabs)
                        ins.src_abs[1] = true;
        }

        ins.mask = mask_of(nr_components);

        /* Apply writemask if non-SSA, keeping in mind that we can't write to
         * components that don't exist. Note modifier => SSA => !reg => no
         * writemask, so we don't have to worry about writemasks here.*/

        if (!is_ssa)
                ins.mask &= instr->dest.write_mask;

        ins.op = op;
        ins.outmod = outmod;

        /* Late fixup for emulated instructions */

        if (instr->op == nir_op_b2f32 || instr->op == nir_op_b2i32) {
                /* Presently, our second argument is an inline #0 constant.
                 * Switch over to an embedded 1.0 constant (that can't fit
                 * inline, since we're 32-bit, not 16-bit like the inline
                 * constants) */

                ins.has_inline_constant = false;
                ins.src[1] = SSA_FIXED_REGISTER(REGISTER_CONSTANT);
                ins.src_types[1] = nir_type_float32;
                ins.has_constants = true;

                if (instr->op == nir_op_b2f32)
                        ins.constants.f32[0] = 1.0f;
                else
                        ins.constants.i32[0] = 1;

                for (unsigned c = 0; c < 16; ++c)
                        ins.swizzle[1][c] = 0;
        } else if (instr->op == nir_op_b2f16) {
                ins.src[1] = SSA_FIXED_REGISTER(REGISTER_CONSTANT);
                ins.src_types[1] = nir_type_float16;
                ins.has_constants = true;
                ins.constants.i16[0] = _mesa_float_to_half(1.0);

                for (unsigned c = 0; c < 16; ++c)
                        ins.swizzle[1][c] = 0;
        } else if (nr_inputs == 1 && !quirk_flipped_r24) {
                /* Lots of instructions need a 0 plonked in */
                ins.has_inline_constant = false;
                ins.src[1] = SSA_FIXED_REGISTER(REGISTER_CONSTANT);
                ins.src_types[1] = ins.src_types[0];
                ins.has_constants = true;
                ins.constants.u32[0] = 0;

                for (unsigned c = 0; c < 16; ++c)
                        ins.swizzle[1][c] = 0;
        } else if (instr->op == nir_op_pack_32_2x16) {
                ins.dest_type = nir_type_uint16;
                ins.mask = mask_of(nr_components * 2);
                ins.is_pack = true;
        } else if (instr->op == nir_op_pack_32_4x8) {
                ins.dest_type = nir_type_uint8;
                ins.mask = mask_of(nr_components * 4);
                ins.is_pack = true;
        } else if (instr->op == nir_op_unpack_32_2x16) {
                ins.dest_type = nir_type_uint32;
                ins.mask = mask_of(nr_components >> 1);
                ins.is_pack = true;
        } else if (instr->op == nir_op_unpack_32_4x8) {
                ins.dest_type = nir_type_uint32;
                ins.mask = mask_of(nr_components >> 2);
                ins.is_pack = true;
        }

        if ((opcode_props & UNITS_ALL) == UNIT_VLUT) {
                /* To avoid duplicating the lookup tables (probably), true LUT
                 * instructions can only operate as if they were scalars. Lower
                 * them here by changing the component. */

                unsigned orig_mask = ins.mask;

                unsigned swizzle_back[MIR_VEC_COMPONENTS];
                memcpy(&swizzle_back, ins.swizzle[0], sizeof(swizzle_back));

                midgard_instruction ins_split[MIR_VEC_COMPONENTS];
                unsigned ins_count = 0;

                for (int i = 0; i < nr_components; ++i) {
                        /* Mask the associated component, dropping the
                         * instruction if needed */

                        ins.mask = 1 << i;
                        ins.mask &= orig_mask;

                        for (unsigned j = 0; j < ins_count; ++j) {
                                if (swizzle_back[i] == ins_split[j].swizzle[0][0]) {
                                        ins_split[j].mask |= ins.mask;
                                        ins.mask = 0;
                                        break;
                                }
                        }

                        if (!ins.mask)
                                continue;

                        for (unsigned j = 0; j < MIR_VEC_COMPONENTS; ++j)
                                ins.swizzle[0][j] = swizzle_back[i]; /* Pull from the correct component */

                        ins_split[ins_count] = ins;

                        ++ins_count;
                }

                for (unsigned i = 0; i < ins_count; ++i) {
                        emit_mir_instruction(ctx, ins_split[i]);
                }
        } else {
                emit_mir_instruction(ctx, ins);
        }
}

#undef ALU_CASE

static void
mir_set_intr_mask(nir_instr *instr, midgard_instruction *ins, bool is_read)
{
        nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
        unsigned nir_mask = 0;
        unsigned dsize = 0;

        if (is_read) {
                nir_mask = mask_of(nir_intrinsic_dest_components(intr));
                dsize = nir_dest_bit_size(intr->dest);
        } else {
                nir_mask = nir_intrinsic_write_mask(intr);
                dsize = 32;
        }

        /* Once we have the NIR mask, we need to normalize to work in 32-bit space */
        unsigned bytemask = pan_to_bytemask(dsize, nir_mask);
        ins->dest_type = nir_type_uint | dsize;
        mir_set_bytemask(ins, bytemask);
}

/* Uniforms and UBOs use a shared code path, as uniforms are just (slightly
 * optimized) versions of UBO #0 */

static midgard_instruction *
emit_ubo_read(
        compiler_context *ctx,
        nir_instr *instr,
        unsigned dest,
        unsigned offset,
        nir_src *indirect_offset,
        unsigned indirect_shift,
        unsigned index)
{
        /* TODO: half-floats */

        midgard_instruction ins = m_ld_ubo_int4(dest, 0);
        ins.constants.u32[0] = offset;

        if (instr->type == nir_instr_type_intrinsic)
                mir_set_intr_mask(instr, &ins, true);

        if (indirect_offset) {
                ins.src[2] = nir_src_index(ctx, indirect_offset);
                ins.src_types[2] = nir_type_uint32;
                ins.load_store.arg_2 = (indirect_shift << 5);

                /* X component for the whole swizzle to prevent register
                 * pressure from ballooning from the extra components */
                for (unsigned i = 0; i < ARRAY_SIZE(ins.swizzle[2]); ++i)
                        ins.swizzle[2][i] = 0;
        } else {
                ins.load_store.arg_2 = 0x1E;
        }

        ins.load_store.arg_1 = index;

        return emit_mir_instruction(ctx, ins);
}

/* Globals are like UBOs if you squint. And shared memory is like globals if
 * you squint even harder */

static void
emit_global(
        compiler_context *ctx,
        nir_instr *instr,
        bool is_read,
        unsigned srcdest,
        nir_src *offset,
        bool is_shared)
{
        /* TODO: types */

        midgard_instruction ins;

        if (is_read)
                ins = m_ld_int4(srcdest, 0);
        else
                ins = m_st_int4(srcdest, 0);

        mir_set_offset(ctx, &ins, offset, is_shared);
        mir_set_intr_mask(instr, &ins, is_read);

        /* Set a valid swizzle for masked out components */
        assert(ins.mask);
        unsigned first_component = __builtin_ffs(ins.mask) - 1;

        for (unsigned i = 0; i < ARRAY_SIZE(ins.swizzle[0]); ++i) {
                if (!(ins.mask & (1 << i)))
                        ins.swizzle[0][i] = first_component;
        }

        emit_mir_instruction(ctx, ins);
}

/* If is_shared is off, the only other possible value are globals, since
 * SSBO's are being lowered to globals through a NIR pass. */
static void
emit_atomic(
        compiler_context *ctx,
        nir_intrinsic_instr *instr,
        bool is_shared,
        midgard_load_store_op op)
{
        unsigned bitsize = nir_src_bit_size(instr->src[1]);
        nir_alu_type type =
                (op == midgard_op_atomic_imin || op == midgard_op_atomic_imax) ?
                nir_type_int : nir_type_uint;

        unsigned dest = nir_dest_index(&instr->dest);
        unsigned val = nir_src_index(ctx, &instr->src[1]);
        emit_explicit_constant(ctx, val, val);

        midgard_instruction ins = {
                .type = TAG_LOAD_STORE_4,
                .mask = 0xF,
                .dest = dest,
                .src = { ~0, ~0, ~0, val },
                .src_types = { 0, 0, 0, type | bitsize },
                .op = op
        };

        nir_src *src_offset = nir_get_io_offset_src(instr);

        /* cmpxchg takes an extra value in arg_2, so we don't use it for the offset */
        if (op == midgard_op_atomic_cmpxchg) {
                unsigned addr = nir_src_index(ctx, src_offset);

                ins.src[1] = addr;
                ins.src_types[1] = nir_type_uint | nir_src_bit_size(*src_offset);

                unsigned xchg_val = nir_src_index(ctx, &instr->src[2]);
                emit_explicit_constant(ctx, xchg_val, xchg_val);

                ins.src[2] = val;
                ins.src_types[2] = type | bitsize;
                ins.src[3] = xchg_val;

                if (is_shared)
                        ins.load_store.arg_1 |= 0x6E;
        } else {
                mir_set_offset(ctx, &ins, src_offset, is_shared);
        }

        mir_set_intr_mask(&instr->instr, &ins, true);

        emit_mir_instruction(ctx, ins);
}

static void
emit_varying_read(
        compiler_context *ctx,
        unsigned dest, unsigned offset,
        unsigned nr_comp, unsigned component,
        nir_src *indirect_offset, nir_alu_type type, bool flat)
{
        /* XXX: Half-floats? */
        /* TODO: swizzle, mask */

        midgard_instruction ins = m_ld_vary_32(dest, offset);
        ins.mask = mask_of(nr_comp);
        ins.dest_type = type;

        if (type == nir_type_float16) {
                /* Ensure we are aligned so we can pack it later */
                ins.mask = mask_of(ALIGN_POT(nr_comp, 2));
        }

        for (unsigned i = 0; i < ARRAY_SIZE(ins.swizzle[0]); ++i)
                ins.swizzle[0][i] = MIN2(i + component, COMPONENT_W);

        midgard_varying_parameter p = {
                .is_varying = 1,
                .interpolation = midgard_interp_default,
                .flat = flat,
        };

        unsigned u;
        memcpy(&u, &p, sizeof(p));
        ins.load_store.varying_parameters = u;

        if (indirect_offset) {
                ins.src[2] = nir_src_index(ctx, indirect_offset);
                ins.src_types[2] = nir_type_uint32;
        } else
                ins.load_store.arg_2 = 0x1E;

        ins.load_store.arg_1 = 0x9E;

        /* Use the type appropriate load */
        switch (type) {
        case nir_type_uint32:
        case nir_type_bool32:
                ins.op = midgard_op_ld_vary_32u;
                break;
        case nir_type_int32:
                ins.op = midgard_op_ld_vary_32i;
                break;
        case nir_type_float32:
                ins.op = midgard_op_ld_vary_32;
                break;
        case nir_type_float16:
                ins.op = midgard_op_ld_vary_16;
                break;
        default:
                unreachable("Attempted to load unknown type");
                break;
        }

        emit_mir_instruction(ctx, ins);
}

static void
emit_attr_read(
        compiler_context *ctx,
        unsigned dest, unsigned offset,
        unsigned nr_comp, nir_alu_type t)
{
        midgard_instruction ins = m_ld_attr_32(dest, offset);
        ins.load_store.arg_1 = 0x1E;
        ins.load_store.arg_2 = 0x1E;
        ins.mask = mask_of(nr_comp);

        /* Use the type appropriate load */
        switch (t) {
        case nir_type_uint:
        case nir_type_bool:
                ins.op = midgard_op_ld_attr_32u;
                break;
        case nir_type_int:
                ins.op = midgard_op_ld_attr_32i;
                break;
        case nir_type_float:
                ins.op = midgard_op_ld_attr_32;
                break;
        default:
                unreachable("Attempted to load unknown type");
                break;
        }

        emit_mir_instruction(ctx, ins);
}

static void
emit_sysval_read(compiler_context *ctx, nir_instr *instr,
                unsigned nr_components, unsigned offset)
{
        nir_dest nir_dest;

        /* Figure out which uniform this is */
        int sysval = panfrost_sysval_for_instr(instr, &nir_dest);
        void *val = _mesa_hash_table_u64_search(ctx->sysvals.sysval_to_id, sysval);

        unsigned dest = nir_dest_index(&nir_dest);

        /* Sysvals are prefix uniforms */
        unsigned uniform = ((uintptr_t) val) - 1;

        /* Emit the read itself -- this is never indirect */
        midgard_instruction *ins =
                emit_ubo_read(ctx, instr, dest, (uniform * 16) + offset, NULL, 0, 0);

        ins->mask = mask_of(nr_components);
}

static unsigned
compute_builtin_arg(nir_op op)
{
        switch (op) {
        case nir_intrinsic_load_work_group_id:
                return 0x14;
        case nir_intrinsic_load_local_invocation_id:
                return 0x10;
        default:
                unreachable("Invalid compute paramater loaded");
        }
}

static void
emit_fragment_store(compiler_context *ctx, unsigned src, unsigned src_z, unsigned src_s, enum midgard_rt_id rt)
{
        assert(rt < ARRAY_SIZE(ctx->writeout_branch));

        midgard_instruction *br = ctx->writeout_branch[rt];

        assert(!br);

        emit_explicit_constant(ctx, src, src);

        struct midgard_instruction ins =
                v_branch(false, false);

        bool depth_only = (rt == MIDGARD_ZS_RT);

        ins.writeout = depth_only ? 0 : PAN_WRITEOUT_C;

        /* Add dependencies */
        ins.src[0] = src;
        ins.src_types[0] = nir_type_uint32;
        ins.constants.u32[0] = depth_only ? 0xFF : (rt - MIDGARD_COLOR_RT0) * 0x100;
        for (int i = 0; i < 4; ++i)
                ins.swizzle[0][i] = i;

        if (~src_z) {
                emit_explicit_constant(ctx, src_z, src_z);
                ins.src[2] = src_z;
                ins.src_types[2] = nir_type_uint32;
                ins.writeout |= PAN_WRITEOUT_Z;
        }
        if (~src_s) {
                emit_explicit_constant(ctx, src_s, src_s);
                ins.src[3] = src_s;
                ins.src_types[3] = nir_type_uint32;
                ins.writeout |= PAN_WRITEOUT_S;
        }

        /* Emit the branch */
        br = emit_mir_instruction(ctx, ins);
        schedule_barrier(ctx);
        ctx->writeout_branch[rt] = br;

        /* Push our current location = current block count - 1 = where we'll
         * jump to. Maybe a bit too clever for my own good */

        br->branch.target_block = ctx->block_count - 1;
}

static void
emit_compute_builtin(compiler_context *ctx, nir_intrinsic_instr *instr)
{
        unsigned reg = nir_dest_index(&instr->dest);
        midgard_instruction ins = m_ld_compute_id(reg, 0);
        ins.mask = mask_of(3);
        ins.swizzle[0][3] = COMPONENT_X; /* xyzx */
        ins.load_store.arg_1 = compute_builtin_arg(instr->intrinsic);
        emit_mir_instruction(ctx, ins);
}

static unsigned
vertex_builtin_arg(nir_op op)
{
        switch (op) {
        case nir_intrinsic_load_vertex_id:
                return PAN_VERTEX_ID;
        case nir_intrinsic_load_instance_id:
                return PAN_INSTANCE_ID;
        default:
                unreachable("Invalid vertex builtin");
        }
}

static void
emit_vertex_builtin(compiler_context *ctx, nir_intrinsic_instr *instr)
{
        unsigned reg = nir_dest_index(&instr->dest);
        emit_attr_read(ctx, reg, vertex_builtin_arg(instr->intrinsic), 1, nir_type_int);
}

static void
emit_special(compiler_context *ctx, nir_intrinsic_instr *instr, unsigned idx)
{
        unsigned reg = nir_dest_index(&instr->dest);

        midgard_instruction ld = m_ld_color_buffer_32u(reg, 0);
        ld.op = midgard_op_ld_color_buffer_32u_old;
        ld.load_store.address = idx;
        ld.load_store.arg_2 = 0x1E;

        for (int i = 0; i < 4; ++i)
                ld.swizzle[0][i] = COMPONENT_X;

        emit_mir_instruction(ctx, ld);
}

static void
emit_control_barrier(compiler_context *ctx)
{
        midgard_instruction ins = {
                .type = TAG_TEXTURE_4,
                .dest = ~0,
                .src = { ~0, ~0, ~0, ~0 },
                .op = TEXTURE_OP_BARRIER,
        };

        emit_mir_instruction(ctx, ins);
}

static unsigned
mir_get_branch_cond(nir_src *src, bool *invert)
{
        /* Wrap it. No swizzle since it's a scalar */

        nir_alu_src alu = {
                .src = *src
        };

        *invert = pan_has_source_mod(&alu, nir_op_inot);
        return nir_src_index(NULL, &alu.src);
}

static uint8_t
output_load_rt_addr(compiler_context *ctx, nir_intrinsic_instr *instr)
{
        if (ctx->is_blend)
                return ctx->blend_rt;

        const nir_variable *var;
        var = nir_find_variable_with_driver_location(ctx->nir, nir_var_shader_out, nir_intrinsic_base(instr));
        assert(var);

        unsigned loc = var->data.location;

        if (loc == FRAG_RESULT_COLOR)
                loc = FRAG_RESULT_DATA0;

        if (loc >= FRAG_RESULT_DATA0)
                return loc - FRAG_RESULT_DATA0;

        if (loc == FRAG_RESULT_DEPTH)
                return 0x1F;
        if (loc == FRAG_RESULT_STENCIL)
                return 0x1E;

        unreachable("Invalid RT to load from");
}

static void
emit_intrinsic(compiler_context *ctx, nir_intrinsic_instr *instr)
{
        unsigned offset = 0, reg;

        switch (instr->intrinsic) {
        case nir_intrinsic_discard_if:
        case nir_intrinsic_discard: {
                bool conditional = instr->intrinsic == nir_intrinsic_discard_if;
                struct midgard_instruction discard = v_branch(conditional, false);
                discard.branch.target_type = TARGET_DISCARD;

                if (conditional) {
                        discard.src[0] = mir_get_branch_cond(&instr->src[0],
                                        &discard.branch.invert_conditional);
                        discard.src_types[0] = nir_type_uint32;
                }

                emit_mir_instruction(ctx, discard);
                schedule_barrier(ctx);

                break;
        }

        case nir_intrinsic_load_uniform:
        case nir_intrinsic_load_ubo:
        case nir_intrinsic_load_global:
        case nir_intrinsic_load_shared:
        case nir_intrinsic_load_input:
        case nir_intrinsic_load_interpolated_input: {
                bool is_uniform = instr->intrinsic == nir_intrinsic_load_uniform;
                bool is_ubo = instr->intrinsic == nir_intrinsic_load_ubo;
                bool is_global = instr->intrinsic == nir_intrinsic_load_global;
                bool is_shared = instr->intrinsic == nir_intrinsic_load_shared;
                bool is_flat = instr->intrinsic == nir_intrinsic_load_input;
                bool is_interp = instr->intrinsic == nir_intrinsic_load_interpolated_input;

                /* Get the base type of the intrinsic */
                /* TODO: Infer type? Does it matter? */
                nir_alu_type t =
                        (is_ubo || is_global || is_shared) ? nir_type_uint :
                        (is_interp) ? nir_type_float :
                        nir_intrinsic_dest_type(instr);

                t = nir_alu_type_get_base_type(t);

                if (!(is_ubo || is_global)) {
                        offset = nir_intrinsic_base(instr);
                }

                unsigned nr_comp = nir_intrinsic_dest_components(instr);

                nir_src *src_offset = nir_get_io_offset_src(instr);

                bool direct = nir_src_is_const(*src_offset);
                nir_src *indirect_offset = direct ? NULL : src_offset;

                if (direct)
                        offset += nir_src_as_uint(*src_offset);

                /* We may need to apply a fractional offset */
                int component = (is_flat || is_interp) ?
                                nir_intrinsic_component(instr) : 0;
                reg = nir_dest_index(&instr->dest);

                if (is_uniform && !ctx->is_blend) {
                        emit_ubo_read(ctx, &instr->instr, reg, (ctx->sysvals.sysval_count + offset) * 16, indirect_offset, 4, 0);
                } else if (is_ubo) {
                        nir_src index = instr->src[0];

                        /* TODO: Is indirect block number possible? */
                        assert(nir_src_is_const(index));

                        uint32_t uindex = nir_src_as_uint(index) + 1;
                        emit_ubo_read(ctx, &instr->instr, reg, offset, indirect_offset, 0, uindex);
                } else if (is_global || is_shared) {
                        emit_global(ctx, &instr->instr, true, reg, src_offset, is_shared);
                } else if (ctx->stage == MESA_SHADER_FRAGMENT && !ctx->is_blend) {
                        emit_varying_read(ctx, reg, offset, nr_comp, component, indirect_offset, t | nir_dest_bit_size(instr->dest), is_flat);
                } else if (ctx->is_blend) {
                        /* ctx->blend_input will be precoloured to r0/r2, where
                         * the input is preloaded */

                        unsigned *input = offset ? &ctx->blend_src1 : &ctx->blend_input;

                        if (*input == ~0)
                                *input = reg;
                        else
                                emit_mir_instruction(ctx, v_mov(*input, reg));
                } else if (ctx->stage == MESA_SHADER_VERTEX) {
                        emit_attr_read(ctx, reg, offset, nr_comp, t);
                } else {
                        DBG("Unknown load\n");
                        assert(0);
                }

                break;
        }

        /* Artefact of load_interpolated_input. TODO: other barycentric modes */
        case nir_intrinsic_load_barycentric_pixel:
        case nir_intrinsic_load_barycentric_centroid:
                break;

        /* Reads 128-bit value raw off the tilebuffer during blending, tasty */

        case nir_intrinsic_load_raw_output_pan: {
                reg = nir_dest_index(&instr->dest);

                /* T720 and below use different blend opcodes with slightly
                 * different semantics than T760 and up */

                midgard_instruction ld = m_ld_color_buffer_32u(reg, 0);

                ld.load_store.arg_2 = output_load_rt_addr(ctx, instr);

                if (nir_src_is_const(instr->src[0])) {
                        ld.load_store.arg_1 = nir_src_as_uint(instr->src[0]);
                } else {
                        ld.load_store.varying_parameters = 2;
                        ld.src[1] = nir_src_index(ctx, &instr->src[0]);
                        ld.src_types[1] = nir_type_int32;
                }

                if (ctx->quirks & MIDGARD_OLD_BLEND) {
                        ld.op = midgard_op_ld_color_buffer_32u_old;
                        ld.load_store.address = 16;
                        ld.load_store.arg_2 = 0x1E;
                }

                emit_mir_instruction(ctx, ld);
                break;
        }

        case nir_intrinsic_load_output: {
                reg = nir_dest_index(&instr->dest);

                unsigned bits = nir_dest_bit_size(instr->dest);

                midgard_instruction ld;
                if (bits == 16)
                        ld = m_ld_color_buffer_as_fp16(reg, 0);
                else
                        ld = m_ld_color_buffer_as_fp32(reg, 0);

                ld.load_store.arg_2 = output_load_rt_addr(ctx, instr);

                for (unsigned c = 4; c < 16; ++c)
                        ld.swizzle[0][c] = 0;

                if (ctx->quirks & MIDGARD_OLD_BLEND) {
                        if (bits == 16)
                                ld.op = midgard_op_ld_color_buffer_as_fp16_old;
                        else
                                ld.op = midgard_op_ld_color_buffer_as_fp32_old;
                        ld.load_store.address = 1;
                        ld.load_store.arg_2 = 0x1E;
                }

                emit_mir_instruction(ctx, ld);
                break;
        }

        case nir_intrinsic_load_blend_const_color_rgba: {
                assert(ctx->is_blend);
                reg = nir_dest_index(&instr->dest);

                midgard_instruction ins = v_mov(SSA_FIXED_REGISTER(REGISTER_CONSTANT), reg);
                ins.has_constants = true;
                memcpy(ins.constants.f32, ctx->blend_constants, sizeof(ctx->blend_constants));
                emit_mir_instruction(ctx, ins);
                break;
        }

        case nir_intrinsic_store_output:
        case nir_intrinsic_store_combined_output_pan:
                assert(nir_src_is_const(instr->src[1]) && "no indirect outputs");

                offset = nir_intrinsic_base(instr) + nir_src_as_uint(instr->src[1]);

                reg = nir_src_index(ctx, &instr->src[0]);

                if (ctx->stage == MESA_SHADER_FRAGMENT) {
                        bool combined = instr->intrinsic ==
                                nir_intrinsic_store_combined_output_pan;

                        const nir_variable *var;
                        var = nir_find_variable_with_driver_location(ctx->nir, nir_var_shader_out,
                                         nir_intrinsic_base(instr));
                        assert(var);

                        /* Dual-source blend writeout is done by leaving the
                         * value in r2 for the blend shader to use. */
                        if (var->data.index) {
                                if (instr->src[0].is_ssa) {
                                        emit_explicit_constant(ctx, reg, reg);

                                        unsigned out = make_compiler_temp(ctx);

                                        midgard_instruction ins = v_mov(reg, out);
                                        emit_mir_instruction(ctx, ins);

                                        ctx->blend_src1 = out;
                                } else {
                                        ctx->blend_src1 = reg;
                                }

                                break;
                        }

                        enum midgard_rt_id rt;
                        if (var->data.location == FRAG_RESULT_COLOR)
                                rt = MIDGARD_COLOR_RT0;
                        else if (var->data.location >= FRAG_RESULT_DATA0)
                                rt = MIDGARD_COLOR_RT0 + var->data.location -
                                     FRAG_RESULT_DATA0;
                        else if (combined)
                                rt = MIDGARD_ZS_RT;
                        else
                                unreachable("bad rt");

                        unsigned reg_z = ~0, reg_s = ~0;
                        if (combined) {
                                unsigned writeout = nir_intrinsic_component(instr);
                                if (writeout & PAN_WRITEOUT_Z)
                                        reg_z = nir_src_index(ctx, &instr->src[2]);
                                if (writeout & PAN_WRITEOUT_S)
                                        reg_s = nir_src_index(ctx, &instr->src[3]);
                        }

                        emit_fragment_store(ctx, reg, reg_z, reg_s, rt);
                } else if (ctx->stage == MESA_SHADER_VERTEX) {
                        assert(instr->intrinsic == nir_intrinsic_store_output);

                        /* We should have been vectorized, though we don't
                         * currently check that st_vary is emitted only once
                         * per slot (this is relevant, since there's not a mask
                         * parameter available on the store [set to 0 by the
                         * blob]). We do respect the component by adjusting the
                         * swizzle. If this is a constant source, we'll need to
                         * emit that explicitly. */

                        emit_explicit_constant(ctx, reg, reg);

                        unsigned dst_component = nir_intrinsic_component(instr);
                        unsigned nr_comp = nir_src_num_components(instr->src[0]);

                        midgard_instruction st = m_st_vary_32(reg, offset);
                        st.load_store.arg_1 = 0x9E;
                        st.load_store.arg_2 = 0x1E;

                        switch (nir_alu_type_get_base_type(nir_intrinsic_src_type(instr))) {
                        case nir_type_uint:
                        case nir_type_bool:
                                st.op = midgard_op_st_vary_32u;
                                break;
                        case nir_type_int:
                                st.op = midgard_op_st_vary_32i;
                                break;
                        case nir_type_float:
                                st.op = midgard_op_st_vary_32;
                                break;
                        default:
                                unreachable("Attempted to store unknown type");
                                break;
                        }

                        /* nir_intrinsic_component(store_intr) encodes the
                         * destination component start. Source component offset
                         * adjustment is taken care of in
                         * install_registers_instr(), when offset_swizzle() is
                         * called.
                         */
                        unsigned src_component = COMPONENT_X;

                        assert(nr_comp > 0);
                        for (unsigned i = 0; i < ARRAY_SIZE(st.swizzle); ++i) {
                                st.swizzle[0][i] = src_component;
                                if (i >= dst_component && i < dst_component + nr_comp - 1)
                                        src_component++;
                        }

                        emit_mir_instruction(ctx, st);
                } else {
                        DBG("Unknown store\n");
                        assert(0);
                }

                break;

        /* Special case of store_output for lowered blend shaders */
        case nir_intrinsic_store_raw_output_pan:
                assert (ctx->stage == MESA_SHADER_FRAGMENT);
                reg = nir_src_index(ctx, &instr->src[0]);
                emit_fragment_store(ctx, reg, ~0, ~0, ctx->blend_rt);
                break;

        case nir_intrinsic_store_global:
        case nir_intrinsic_store_shared:
                reg = nir_src_index(ctx, &instr->src[0]);
                emit_explicit_constant(ctx, reg, reg);

                emit_global(ctx, &instr->instr, false, reg, &instr->src[1], instr->intrinsic == nir_intrinsic_store_shared);
                break;

        case nir_intrinsic_load_ssbo_address:
                emit_sysval_read(ctx, &instr->instr, 1, 0);
                break;

        case nir_intrinsic_get_ssbo_size:
                emit_sysval_read(ctx, &instr->instr, 1, 8);
                break;

        case nir_intrinsic_load_viewport_scale:
        case nir_intrinsic_load_viewport_offset:
        case nir_intrinsic_load_num_work_groups:
        case nir_intrinsic_load_sampler_lod_parameters_pan:
                emit_sysval_read(ctx, &instr->instr, 3, 0);
                break;

        case nir_intrinsic_load_work_group_id:
        case nir_intrinsic_load_local_invocation_id:
                emit_compute_builtin(ctx, instr);
                break;

        case nir_intrinsic_load_vertex_id:
        case nir_intrinsic_load_instance_id:
                emit_vertex_builtin(ctx, instr);
                break;

        case nir_intrinsic_load_sample_mask_in:
                emit_special(ctx, instr, 96);
                break;

        case nir_intrinsic_load_sample_id:
                emit_special(ctx, instr, 97);
                break;

        case nir_intrinsic_memory_barrier_buffer:
        case nir_intrinsic_memory_barrier_shared:
                break;

        case nir_intrinsic_control_barrier:
                schedule_barrier(ctx);
                emit_control_barrier(ctx);
                schedule_barrier(ctx);
                break;

        ATOMIC_CASE(ctx, instr, add, add);
        ATOMIC_CASE(ctx, instr, and, and);
        ATOMIC_CASE(ctx, instr, comp_swap, cmpxchg);
        ATOMIC_CASE(ctx, instr, exchange, xchg);
        ATOMIC_CASE(ctx, instr, imax, imax);
        ATOMIC_CASE(ctx, instr, imin, imin);
        ATOMIC_CASE(ctx, instr, or, or);
        ATOMIC_CASE(ctx, instr, umax, umax);
        ATOMIC_CASE(ctx, instr, umin, umin);
        ATOMIC_CASE(ctx, instr, xor, xor);

        default:
                fprintf(stderr, "Unhandled intrinsic %s\n", nir_intrinsic_infos[instr->intrinsic].name);
                assert(0);
                break;
        }
}

/* Returns dimension with 0 special casing cubemaps */
static unsigned
midgard_tex_format(enum glsl_sampler_dim dim)
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

/* Tries to attach an explicit LOD or bias as a constant. Returns whether this
 * was successful */

static bool
pan_attach_constant_bias(
        compiler_context *ctx,
        nir_src lod,
        midgard_texture_word *word)
{
        /* To attach as constant, it has to *be* constant */

        if (!nir_src_is_const(lod))
                return false;

        float f = nir_src_as_float(lod);

        /* Break into fixed-point */
        signed lod_int = f;
        float lod_frac = f - lod_int;

        /* Carry over negative fractions */
        if (lod_frac < 0.0) {
                lod_int--;
                lod_frac += 1.0;
        }

        /* Encode */
        word->bias = float_to_ubyte(lod_frac);
        word->bias_int = lod_int;

        return true;
}

static enum mali_texture_mode
mdg_texture_mode(nir_tex_instr *instr)
{
        if (instr->op == nir_texop_tg4 && instr->is_shadow)
                return TEXTURE_GATHER_SHADOW;
        else if (instr->op == nir_texop_tg4)
                return TEXTURE_GATHER_X + instr->component;
        else if (instr->is_shadow)
                return TEXTURE_SHADOW;
        else
                return TEXTURE_NORMAL;
}

static void
emit_texop_native(compiler_context *ctx, nir_tex_instr *instr,
                  unsigned midgard_texop)
{
        /* TODO */
        //assert (!instr->sampler);

        nir_dest *dest = &instr->dest;

        int texture_index = instr->texture_index;
        int sampler_index = texture_index;

        nir_alu_type dest_base = nir_alu_type_get_base_type(instr->dest_type);
        nir_alu_type dest_type = dest_base | nir_dest_bit_size(*dest);

        /* texture instructions support float outmods */
        unsigned outmod = midgard_outmod_none;
        if (dest_base == nir_type_float) {
                outmod = mir_determine_float_outmod(ctx, &dest, 0);
        }

        midgard_instruction ins = {
                .type = TAG_TEXTURE_4,
                .mask = 0xF,
                .dest = nir_dest_index(dest),
                .src = { ~0, ~0, ~0, ~0 },
                .dest_type = dest_type,
                .swizzle = SWIZZLE_IDENTITY_4,
                .outmod = outmod,
                .op = midgard_texop,
                .texture = {
                        .format = midgard_tex_format(instr->sampler_dim),
                        .texture_handle = texture_index,
                        .sampler_handle = sampler_index,
                        .mode = mdg_texture_mode(instr)
                }
        };

        if (instr->is_shadow && !instr->is_new_style_shadow && instr->op != nir_texop_tg4)
           for (int i = 0; i < 4; ++i)
              ins.swizzle[0][i] = COMPONENT_X;

        /* We may need a temporary for the coordinate */

        bool needs_temp_coord =
                (midgard_texop == TEXTURE_OP_TEXEL_FETCH) ||
                (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE) ||
                (instr->is_shadow);

        unsigned coords = needs_temp_coord ? make_compiler_temp_reg(ctx) : 0;

        for (unsigned i = 0; i < instr->num_srcs; ++i) {
                int index = nir_src_index(ctx, &instr->src[i].src);
                unsigned nr_components = nir_src_num_components(instr->src[i].src);
                unsigned sz = nir_src_bit_size(instr->src[i].src);
                nir_alu_type T = nir_tex_instr_src_type(instr, i) | sz;

                switch (instr->src[i].src_type) {
                case nir_tex_src_coord: {
                        emit_explicit_constant(ctx, index, index);

                        unsigned coord_mask = mask_of(instr->coord_components);

                        bool flip_zw = (instr->sampler_dim == GLSL_SAMPLER_DIM_2D) && (coord_mask & (1 << COMPONENT_Z));

                        if (flip_zw)
                                coord_mask ^= ((1 << COMPONENT_Z) | (1 << COMPONENT_W));

                        if (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE) {
                                /* texelFetch is undefined on samplerCube */
                                assert(midgard_texop != TEXTURE_OP_TEXEL_FETCH);

                                /* For cubemaps, we use a special ld/st op to
                                 * select the face and copy the xy into the
                                 * texture register */

                                midgard_instruction ld = m_ld_cubemap_coords(coords, 0);
                                ld.src[1] = index;
                                ld.src_types[1] = T;
                                ld.mask = 0x3; /* xy */
                                ld.load_store.arg_1 = 0x20;
                                ld.swizzle[1][3] = COMPONENT_X;
                                emit_mir_instruction(ctx, ld);

                                /* xyzw -> xyxx */
                                ins.swizzle[1][2] = instr->is_shadow ? COMPONENT_Z : COMPONENT_X;
                                ins.swizzle[1][3] = COMPONENT_X;
                        } else if (needs_temp_coord) {
                                /* mov coord_temp, coords */
                                midgard_instruction mov = v_mov(index, coords);
                                mov.mask = coord_mask;

                                if (flip_zw)
                                        mov.swizzle[1][COMPONENT_W] = COMPONENT_Z;

                                emit_mir_instruction(ctx, mov);
                        } else {
                                coords = index;
                        }

                        ins.src[1] = coords;
                        ins.src_types[1] = T;

                        /* Texelfetch coordinates uses all four elements
                         * (xyz/index) regardless of texture dimensionality,
                         * which means it's necessary to zero the unused
                         * components to keep everything happy */

                        if (midgard_texop == TEXTURE_OP_TEXEL_FETCH) {
                                /* mov index.zw, #0, or generalized */
                                midgard_instruction mov =
                                        v_mov(SSA_FIXED_REGISTER(REGISTER_CONSTANT), coords);
                                mov.has_constants = true;
                                mov.mask = coord_mask ^ 0xF;
                                emit_mir_instruction(ctx, mov);
                        }

                        if (instr->sampler_dim == GLSL_SAMPLER_DIM_2D) {
                                /* Array component in w but NIR wants it in z,
                                 * but if we have a temp coord we already fixed
                                 * that up */

                                if (nr_components == 3) {
                                        ins.swizzle[1][2] = COMPONENT_Z;
                                        ins.swizzle[1][3] = needs_temp_coord ? COMPONENT_W : COMPONENT_Z;
                                } else if (nr_components == 2) {
                                        ins.swizzle[1][2] =
                                                instr->is_shadow ? COMPONENT_Z : COMPONENT_X;
                                        ins.swizzle[1][3] = COMPONENT_X;
                                } else
                                        unreachable("Invalid texture 2D components");
                        }

                        if (midgard_texop == TEXTURE_OP_TEXEL_FETCH) {
                                /* We zeroed */
                                ins.swizzle[1][2] = COMPONENT_Z;
                                ins.swizzle[1][3] = COMPONENT_W;
                        }

                        break;
                }

                case nir_tex_src_bias:
                case nir_tex_src_lod: {
                        /* Try as a constant if we can */

                        bool is_txf = midgard_texop == TEXTURE_OP_TEXEL_FETCH;
                        if (!is_txf && pan_attach_constant_bias(ctx, instr->src[i].src, &ins.texture))
                                break;

                        ins.texture.lod_register = true;
                        ins.src[2] = index;
                        ins.src_types[2] = T;

                        for (unsigned c = 0; c < MIR_VEC_COMPONENTS; ++c)
                                ins.swizzle[2][c] = COMPONENT_X;

                        emit_explicit_constant(ctx, index, index);

                        break;
                };

                case nir_tex_src_offset: {
                        ins.texture.offset_register = true;
                        ins.src[3] = index;
                        ins.src_types[3] = T;

                        for (unsigned c = 0; c < MIR_VEC_COMPONENTS; ++c)
                                ins.swizzle[3][c] = (c > COMPONENT_Z) ? 0 : c;

                        emit_explicit_constant(ctx, index, index);
                        break;
                };

                case nir_tex_src_comparator:
                case nir_tex_src_ms_index: {
                        unsigned comp = COMPONENT_Z;

                        /* mov coord_temp.foo, coords */
                        midgard_instruction mov = v_mov(index, coords);
                        mov.mask = 1 << comp;

                        for (unsigned i = 0; i < MIR_VEC_COMPONENTS; ++i)
                                mov.swizzle[1][i] = COMPONENT_X;

                        emit_mir_instruction(ctx, mov);
                        break;
                }

                default: {
                        fprintf(stderr, "Unknown texture source type: %d\n", instr->src[i].src_type);
                        assert(0);
                }
                }
        }

        emit_mir_instruction(ctx, ins);
}

static void
emit_tex(compiler_context *ctx, nir_tex_instr *instr)
{
        switch (instr->op) {
        case nir_texop_tex:
        case nir_texop_txb:
                emit_texop_native(ctx, instr, TEXTURE_OP_NORMAL);
                break;
        case nir_texop_txl:
        case nir_texop_tg4:
                emit_texop_native(ctx, instr, TEXTURE_OP_LOD);
                break;
        case nir_texop_txf:
        case nir_texop_txf_ms:
                emit_texop_native(ctx, instr, TEXTURE_OP_TEXEL_FETCH);
                break;
        case nir_texop_txs:
                emit_sysval_read(ctx, &instr->instr, 4, 0);
                break;
        default: {
                fprintf(stderr, "Unhandled texture op: %d\n", instr->op);
                assert(0);
        }
        }
}

static void
emit_jump(compiler_context *ctx, nir_jump_instr *instr)
{
        switch (instr->type) {
        case nir_jump_break: {
                /* Emit a branch out of the loop */
                struct midgard_instruction br = v_branch(false, false);
                br.branch.target_type = TARGET_BREAK;
                br.branch.target_break = ctx->current_loop_depth;
                emit_mir_instruction(ctx, br);
                break;
        }

        default:
                DBG("Unknown jump type %d\n", instr->type);
                break;
        }
}

static void
emit_instr(compiler_context *ctx, struct nir_instr *instr)
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
                DBG("Unhandled instruction type\n");
                break;
        }
}


/* ALU instructions can inline or embed constants, which decreases register
 * pressure and saves space. */

#define CONDITIONAL_ATTACH(idx) { \
	void *entry = _mesa_hash_table_u64_search(ctx->ssa_constants, alu->src[idx] + 1); \
\
	if (entry) { \
		attach_constants(ctx, alu, entry, alu->src[idx] + 1); \
		alu->src[idx] = SSA_FIXED_REGISTER(REGISTER_CONSTANT); \
	} \
}

static void
inline_alu_constants(compiler_context *ctx, midgard_block *block)
{
        mir_foreach_instr_in_block(block, alu) {
                /* Other instructions cannot inline constants */
                if (alu->type != TAG_ALU_4) continue;
                if (alu->compact_branch) continue;

                /* If there is already a constant here, we can do nothing */
                if (alu->has_constants) continue;

                CONDITIONAL_ATTACH(0);

                if (!alu->has_constants) {
                        CONDITIONAL_ATTACH(1)
                } else if (!alu->inline_constant) {
                        /* Corner case: _two_ vec4 constants, for instance with a
                         * csel. For this case, we can only use a constant
                         * register for one, we'll have to emit a move for the
                         * other. */

                        void *entry = _mesa_hash_table_u64_search(ctx->ssa_constants, alu->src[1] + 1);
                        unsigned scratch = make_compiler_temp(ctx);

                        if (entry) {
                                midgard_instruction ins = v_mov(SSA_FIXED_REGISTER(REGISTER_CONSTANT), scratch);
                                attach_constants(ctx, &ins, entry, alu->src[1] + 1);

                                /* Set the source */
                                alu->src[1] = scratch;

                                /* Inject us -before- the last instruction which set r31 */
                                mir_insert_instruction_before(ctx, mir_prev_op(alu), ins);
                        }
                }
        }
}

unsigned
max_bitsize_for_alu(midgard_instruction *ins)
{
        unsigned max_bitsize = 0;
        for (int i = 0; i < MIR_SRC_COUNT; i++) {
                if (ins->src[i] == ~0) continue;
                unsigned src_bitsize = nir_alu_type_get_type_size(ins->src_types[i]);
                max_bitsize = MAX2(src_bitsize, max_bitsize);
        }
        unsigned dst_bitsize = nir_alu_type_get_type_size(ins->dest_type);
        max_bitsize = MAX2(dst_bitsize, max_bitsize);

        /* We don't have fp16 LUTs, so we'll want to emit code like:
         *
         *      vlut.fsinr hr0, hr0
         *
         * where both input and output are 16-bit but the operation is carried
         * out in 32-bit
         */

        switch (ins->op) {
        case midgard_alu_op_fsqrt:
        case midgard_alu_op_frcp:
        case midgard_alu_op_frsqrt:
        case midgard_alu_op_fsin:
        case midgard_alu_op_fcos:
        case midgard_alu_op_fexp2:
        case midgard_alu_op_flog2:
                max_bitsize = MAX2(max_bitsize, 32);
                break;

        default:
                break;
        }

        /* High implies computing at a higher bitsize, e.g umul_high of 32-bit
         * requires computing at 64-bit */
        if (midgard_is_integer_out_op(ins->op) && ins->outmod == midgard_outmod_int_high) {
                max_bitsize *= 2;
                assert(max_bitsize <= 64);
        }

        return max_bitsize;
}

midgard_reg_mode
reg_mode_for_bitsize(unsigned bitsize)
{
        switch (bitsize) {
                /* use 16 pipe for 8 since we don't support vec16 yet */
        case 8:
        case 16:
                return midgard_reg_mode_16;
        case 32:
                return midgard_reg_mode_32;
        case 64:
                return midgard_reg_mode_64;
        default:
                unreachable("invalid bit size");
        }
}

/* Midgard supports two types of constants, embedded constants (128-bit) and
 * inline constants (16-bit). Sometimes, especially with scalar ops, embedded
 * constants can be demoted to inline constants, for space savings and
 * sometimes a performance boost */

static void
embedded_to_inline_constant(compiler_context *ctx, midgard_block *block)
{
        mir_foreach_instr_in_block(block, ins) {
                if (!ins->has_constants) continue;
                if (ins->has_inline_constant) continue;

                unsigned max_bitsize = max_bitsize_for_alu(ins);

                /* We can inline 32-bit (sometimes) or 16-bit (usually) */
                bool is_16 = max_bitsize == 16;
                bool is_32 = max_bitsize == 32;

                if (!(is_16 || is_32))
                        continue;

                /* src1 cannot be an inline constant due to encoding
                 * restrictions. So, if possible we try to flip the arguments
                 * in that case */

                int op = ins->op;

                if (ins->src[0] == SSA_FIXED_REGISTER(REGISTER_CONSTANT) &&
                                alu_opcode_props[op].props & OP_COMMUTES) {
                        mir_flip(ins);
                }

                if (ins->src[1] == SSA_FIXED_REGISTER(REGISTER_CONSTANT)) {
                        /* Component is from the swizzle. Take a nonzero component */
                        assert(ins->mask);
                        unsigned first_comp = ffs(ins->mask) - 1;
                        unsigned component = ins->swizzle[1][first_comp];

                        /* Scale constant appropriately, if we can legally */
                        int16_t scaled_constant = 0;

                        if (is_16) {
                                scaled_constant = ins->constants.u16[component];
                        } else if (midgard_is_integer_op(op)) {
                                scaled_constant = ins->constants.u32[component];

                                /* Constant overflow after resize */
                                if (scaled_constant != ins->constants.u32[component])
                                        continue;
                        } else {
                                float original = ins->constants.f32[component];
                                scaled_constant = _mesa_float_to_half(original);

                                /* Check for loss of precision. If this is
                                 * mediump, we don't care, but for a highp
                                 * shader, we need to pay attention. NIR
                                 * doesn't yet tell us which mode we're in!
                                 * Practically this prevents most constants
                                 * from being inlined, sadly. */

                                float fp32 = _mesa_half_to_float(scaled_constant);

                                if (fp32 != original)
                                        continue;
                        }

                        /* Should've been const folded */
                        if (ins->src_abs[1] || ins->src_neg[1])
                                continue;

                        /* Make sure that the constant is not itself a vector
                         * by checking if all accessed values are the same. */

                        const midgard_constants *cons = &ins->constants;
                        uint32_t value = is_16 ? cons->u16[component] : cons->u32[component];

                        bool is_vector = false;
                        unsigned mask = effective_writemask(ins->op, ins->mask);

                        for (unsigned c = 0; c < MIR_VEC_COMPONENTS; ++c) {
                                /* We only care if this component is actually used */
                                if (!(mask & (1 << c)))
                                        continue;

                                uint32_t test = is_16 ?
                                                cons->u16[ins->swizzle[1][c]] :
                                                cons->u32[ins->swizzle[1][c]];

                                if (test != value) {
                                        is_vector = true;
                                        break;
                                }
                        }

                        if (is_vector)
                                continue;

                        /* Get rid of the embedded constant */
                        ins->has_constants = false;
                        ins->src[1] = ~0;
                        ins->has_inline_constant = true;
                        ins->inline_constant = scaled_constant;
                }
        }
}

/* Dead code elimination for branches at the end of a block - only one branch
 * per block is legal semantically */

static void
midgard_cull_dead_branch(compiler_context *ctx, midgard_block *block)
{
        bool branched = false;

        mir_foreach_instr_in_block_safe(block, ins) {
                if (!midgard_is_branch_unit(ins->unit)) continue;

                if (branched)
                        mir_remove_instruction(ins);

                branched = true;
        }
}

/* We want to force the invert on AND/OR to the second slot to legalize into
 * iandnot/iornot. The relevant patterns are for AND (and OR respectively)
 *
 *   ~a & #b = ~a & ~(#~b)
 *   ~a & b = b & ~a
 */

static void
midgard_legalize_invert(compiler_context *ctx, midgard_block *block)
{
        mir_foreach_instr_in_block(block, ins) {
                if (ins->type != TAG_ALU_4) continue;

                if (ins->op != midgard_alu_op_iand &&
                    ins->op != midgard_alu_op_ior) continue;

                if (ins->src_invert[1] || !ins->src_invert[0]) continue;

                if (ins->has_inline_constant) {
                        /* ~(#~a) = ~(~#a) = a, so valid, and forces both
                         * inverts on */
                        ins->inline_constant = ~ins->inline_constant;
                        ins->src_invert[1] = true;
                } else {
                        /* Flip to the right invert order. Note
                         * has_inline_constant false by assumption on the
                         * branch, so flipping makes sense. */
                        mir_flip(ins);
                }
        }
}

static unsigned
emit_fragment_epilogue(compiler_context *ctx, unsigned rt)
{
        /* Loop to ourselves */
        midgard_instruction *br = ctx->writeout_branch[rt];
        struct midgard_instruction ins = v_branch(false, false);
        ins.writeout = br->writeout;
        ins.branch.target_block = ctx->block_count - 1;
        ins.constants.u32[0] = br->constants.u32[0];
        memcpy(&ins.src_types, &br->src_types, sizeof(ins.src_types));
        emit_mir_instruction(ctx, ins);

        ctx->current_block->epilogue = true;
        schedule_barrier(ctx);
        return ins.branch.target_block;
}

static midgard_block *
emit_block_init(compiler_context *ctx)
{
        midgard_block *this_block = ctx->after_block;
        ctx->after_block = NULL;

        if (!this_block)
                this_block = create_empty_block(ctx);

        list_addtail(&this_block->base.link, &ctx->blocks);

        this_block->scheduled = false;
        ++ctx->block_count;

        /* Set up current block */
        list_inithead(&this_block->base.instructions);
        ctx->current_block = this_block;

        return this_block;
}

static midgard_block *
emit_block(compiler_context *ctx, nir_block *block)
{
        midgard_block *this_block = emit_block_init(ctx);

        nir_foreach_instr(instr, block) {
                emit_instr(ctx, instr);
                ++ctx->instruction_count;
        }

        return this_block;
}

static midgard_block *emit_cf_list(struct compiler_context *ctx, struct exec_list *list);

static void
emit_if(struct compiler_context *ctx, nir_if *nif)
{
        midgard_block *before_block = ctx->current_block;

        /* Speculatively emit the branch, but we can't fill it in until later */
        bool inv = false;
        EMIT(branch, true, true);
        midgard_instruction *then_branch = mir_last_in_block(ctx->current_block);
        then_branch->src[0] = mir_get_branch_cond(&nif->condition, &inv);
        then_branch->src_types[0] = nir_type_uint32;
        then_branch->branch.invert_conditional = !inv;

        /* Emit the two subblocks. */
        midgard_block *then_block = emit_cf_list(ctx, &nif->then_list);
        midgard_block *end_then_block = ctx->current_block;

        /* Emit a jump from the end of the then block to the end of the else */
        EMIT(branch, false, false);
        midgard_instruction *then_exit = mir_last_in_block(ctx->current_block);

        /* Emit second block, and check if it's empty */

        int else_idx = ctx->block_count;
        int count_in = ctx->instruction_count;
        midgard_block *else_block = emit_cf_list(ctx, &nif->else_list);
        midgard_block *end_else_block = ctx->current_block;
        int after_else_idx = ctx->block_count;

        /* Now that we have the subblocks emitted, fix up the branches */

        assert(then_block);
        assert(else_block);

        if (ctx->instruction_count == count_in) {
                /* The else block is empty, so don't emit an exit jump */
                mir_remove_instruction(then_exit);
                then_branch->branch.target_block = after_else_idx;
        } else {
                then_branch->branch.target_block = else_idx;
                then_exit->branch.target_block = after_else_idx;
        }

        /* Wire up the successors */

        ctx->after_block = create_empty_block(ctx);

        pan_block_add_successor(&before_block->base, &then_block->base);
        pan_block_add_successor(&before_block->base, &else_block->base);

        pan_block_add_successor(&end_then_block->base, &ctx->after_block->base);
        pan_block_add_successor(&end_else_block->base, &ctx->after_block->base);
}

static void
emit_loop(struct compiler_context *ctx, nir_loop *nloop)
{
        /* Remember where we are */
        midgard_block *start_block = ctx->current_block;

        /* Allocate a loop number, growing the current inner loop depth */
        int loop_idx = ++ctx->current_loop_depth;

        /* Get index from before the body so we can loop back later */
        int start_idx = ctx->block_count;

        /* Emit the body itself */
        midgard_block *loop_block = emit_cf_list(ctx, &nloop->body);

        /* Branch back to loop back */
        struct midgard_instruction br_back = v_branch(false, false);
        br_back.branch.target_block = start_idx;
        emit_mir_instruction(ctx, br_back);

        /* Mark down that branch in the graph. */
        pan_block_add_successor(&start_block->base, &loop_block->base);
        pan_block_add_successor(&ctx->current_block->base, &loop_block->base);

        /* Find the index of the block about to follow us (note: we don't add
         * one; blocks are 0-indexed so we get a fencepost problem) */
        int break_block_idx = ctx->block_count;

        /* Fix up the break statements we emitted to point to the right place,
         * now that we can allocate a block number for them */
        ctx->after_block = create_empty_block(ctx);

        mir_foreach_block_from(ctx, start_block, _block) {
                mir_foreach_instr_in_block(((midgard_block *) _block), ins) {
                        if (ins->type != TAG_ALU_4) continue;
                        if (!ins->compact_branch) continue;

                        /* We found a branch -- check the type to see if we need to do anything */
                        if (ins->branch.target_type != TARGET_BREAK) continue;

                        /* It's a break! Check if it's our break */
                        if (ins->branch.target_break != loop_idx) continue;

                        /* Okay, cool, we're breaking out of this loop.
                         * Rewrite from a break to a goto */

                        ins->branch.target_type = TARGET_GOTO;
                        ins->branch.target_block = break_block_idx;

                        pan_block_add_successor(_block, &ctx->after_block->base);
                }
        }

        /* Now that we've finished emitting the loop, free up the depth again
         * so we play nice with recursion amid nested loops */
        --ctx->current_loop_depth;

        /* Dump loop stats */
        ++ctx->loop_count;
}

static midgard_block *
emit_cf_list(struct compiler_context *ctx, struct exec_list *list)
{
        midgard_block *start_block = NULL;

        foreach_list_typed(nir_cf_node, node, node, list) {
                switch (node->type) {
                case nir_cf_node_block: {
                        midgard_block *block = emit_block(ctx, nir_cf_node_as_block(node));

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

                case nir_cf_node_function:
                        assert(0);
                        break;
                }
        }

        return start_block;
}

/* Due to lookahead, we need to report the first tag executed in the command
 * stream and in branch targets. An initial block might be empty, so iterate
 * until we find one that 'works' */

unsigned
midgard_get_first_tag_from_block(compiler_context *ctx, unsigned block_idx)
{
        midgard_block *initial_block = mir_get_block(ctx, block_idx);

        mir_foreach_block_from(ctx, initial_block, _v) {
                midgard_block *v = (midgard_block *) _v;
                if (v->quadword_count) {
                        midgard_bundle *initial_bundle =
                                util_dynarray_element(&v->bundles, midgard_bundle, 0);

                        return initial_bundle->tag;
                }
        }

        /* Default to a tag 1 which will break from the shader, in case we jump
         * to the exit block (i.e. `return` in a compute shader) */

        return 1;
}

/* For each fragment writeout instruction, generate a writeout loop to
 * associate with it */

static void
mir_add_writeout_loops(compiler_context *ctx)
{
        for (unsigned rt = 0; rt < ARRAY_SIZE(ctx->writeout_branch); ++rt) {
                midgard_instruction *br = ctx->writeout_branch[rt];
                if (!br) continue;

                unsigned popped = br->branch.target_block;
                pan_block_add_successor(&(mir_get_block(ctx, popped - 1)->base), &ctx->current_block->base);
                br->branch.target_block = emit_fragment_epilogue(ctx, rt);
                br->branch.target_type = TARGET_GOTO;

                /* If we have more RTs, we'll need to restore back after our
                 * loop terminates */

                if ((rt + 1) < ARRAY_SIZE(ctx->writeout_branch) && ctx->writeout_branch[rt + 1]) {
                        midgard_instruction uncond = v_branch(false, false);
                        uncond.branch.target_block = popped;
                        uncond.branch.target_type = TARGET_GOTO;
                        emit_mir_instruction(ctx, uncond);
                        pan_block_add_successor(&ctx->current_block->base, &(mir_get_block(ctx, popped)->base));
                        schedule_barrier(ctx);
                } else {
                        /* We're last, so we can terminate here */
                        br->last_writeout = true;
                }
        }
}

panfrost_program *
midgard_compile_shader_nir(void *mem_ctx, nir_shader *nir,
                           const struct panfrost_compile_inputs *inputs)
{
        panfrost_program *program = rzalloc(mem_ctx, panfrost_program);

        struct util_dynarray *compiled = &program->compiled;

        midgard_debug = debug_get_option_midgard_debug();

        /* TODO: Bound against what? */
        compiler_context *ctx = rzalloc(NULL, compiler_context);

        ctx->nir = nir;
        ctx->stage = nir->info.stage;
        ctx->is_blend = inputs->is_blend;
        ctx->blend_rt = MIDGARD_COLOR_RT0 + inputs->blend.rt;
        memcpy(ctx->blend_constants, inputs->blend.constants, sizeof(ctx->blend_constants));
        ctx->blend_input = ~0;
        ctx->blend_src1 = ~0;
        ctx->quirks = midgard_get_quirks(inputs->gpu_id);

        /* Start off with a safe cutoff, allowing usage of all 16 work
         * registers. Later, we'll promote uniform reads to uniform registers
         * if we determine it is beneficial to do so */
        ctx->uniform_cutoff = 8;

        /* Initialize at a global (not block) level hash tables */

        ctx->ssa_constants = _mesa_hash_table_u64_create(NULL);

        /* Lower gl_Position pre-optimisation, but after lowering vars to ssa
         * (so we don't accidentally duplicate the epilogue since mesa/st has
         * messed with our I/O quite a bit already) */

        NIR_PASS_V(nir, nir_lower_vars_to_ssa);

        if (ctx->stage == MESA_SHADER_VERTEX) {
                NIR_PASS_V(nir, nir_lower_viewport_transform);
                NIR_PASS_V(nir, nir_lower_point_size, 1.0, 1024.0);
        }

        NIR_PASS_V(nir, nir_lower_var_copies);
        NIR_PASS_V(nir, nir_lower_vars_to_ssa);
        NIR_PASS_V(nir, nir_split_var_copies);
        NIR_PASS_V(nir, nir_lower_var_copies);
        NIR_PASS_V(nir, nir_lower_global_vars_to_local);
        NIR_PASS_V(nir, nir_lower_var_copies);
        NIR_PASS_V(nir, nir_lower_vars_to_ssa);

        unsigned pan_quirks = panfrost_get_quirks(inputs->gpu_id);
        NIR_PASS_V(nir, pan_lower_framebuffer,
                   inputs->rt_formats, inputs->is_blend, pan_quirks);

        NIR_PASS_V(nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
                        glsl_type_size, 0);
        NIR_PASS_V(nir, nir_lower_ssbo);
        NIR_PASS_V(nir, pan_nir_lower_zs_store);

        /* Optimisation passes */

        optimise_nir(nir, ctx->quirks, inputs->is_blend);

        NIR_PASS_V(nir, pan_nir_reorder_writeout);

        if ((midgard_debug & MIDGARD_DBG_SHADERS) && !nir->info.internal) {
                nir_print_shader(nir, stdout);
        }

        /* Assign sysvals and counts, now that we're sure
         * (post-optimisation) */

        panfrost_nir_assign_sysvals(&ctx->sysvals, ctx, nir);
        program->sysval_count = ctx->sysvals.sysval_count;
        memcpy(program->sysvals, ctx->sysvals.sysvals, sizeof(ctx->sysvals.sysvals[0]) * ctx->sysvals.sysval_count);

        nir_foreach_function(func, nir) {
                if (!func->impl)
                        continue;

                list_inithead(&ctx->blocks);
                ctx->block_count = 0;
                ctx->func = func;
                ctx->already_emitted = calloc(BITSET_WORDS(func->impl->ssa_alloc), sizeof(BITSET_WORD));

                if (nir->info.outputs_read && !inputs->is_blend) {
                        emit_block_init(ctx);

                        struct midgard_instruction wait = v_branch(false, false);
                        wait.branch.target_type = TARGET_TILEBUF_WAIT;

                        emit_mir_instruction(ctx, wait);

                        ++ctx->instruction_count;
                }

                emit_cf_list(ctx, &func->impl->body);
                free(ctx->already_emitted);
                break; /* TODO: Multi-function shaders */
        }

        util_dynarray_init(compiled, program);

        /* Per-block lowering before opts */

        mir_foreach_block(ctx, _block) {
                midgard_block *block = (midgard_block *) _block;
                inline_alu_constants(ctx, block);
                embedded_to_inline_constant(ctx, block);
        }
        /* MIR-level optimizations */

        bool progress = false;

        do {
                progress = false;
                progress |= midgard_opt_dead_code_eliminate(ctx);

                mir_foreach_block(ctx, _block) {
                        midgard_block *block = (midgard_block *) _block;
                        progress |= midgard_opt_copy_prop(ctx, block);
                        progress |= midgard_opt_combine_projection(ctx, block);
                        progress |= midgard_opt_varying_projection(ctx, block);
                }
        } while (progress);

        mir_foreach_block(ctx, _block) {
                midgard_block *block = (midgard_block *) _block;
                midgard_lower_derivatives(ctx, block);
                midgard_legalize_invert(ctx, block);
                midgard_cull_dead_branch(ctx, block);
        }

        if (ctx->stage == MESA_SHADER_FRAGMENT)
                mir_add_writeout_loops(ctx);

        /* Analyze now that the code is known but before scheduling creates
         * pipeline registers which are harder to track */
        mir_analyze_helper_terminate(ctx);
        mir_analyze_helper_requirements(ctx);

        /* Schedule! */
        midgard_schedule_program(ctx);
        mir_ra(ctx);

        /* Emit flat binary from the instruction arrays. Iterate each block in
         * sequence. Save instruction boundaries such that lookahead tags can
         * be assigned easily */

        /* Cache _all_ bundles in source order for lookahead across failed branches */

        int bundle_count = 0;
        mir_foreach_block(ctx, _block) {
                midgard_block *block = (midgard_block *) _block;
                bundle_count += block->bundles.size / sizeof(midgard_bundle);
        }
        midgard_bundle **source_order_bundles = malloc(sizeof(midgard_bundle *) * bundle_count);
        int bundle_idx = 0;
        mir_foreach_block(ctx, _block) {
                midgard_block *block = (midgard_block *) _block;
                util_dynarray_foreach(&block->bundles, midgard_bundle, bundle) {
                        source_order_bundles[bundle_idx++] = bundle;
                }
        }

        int current_bundle = 0;

        /* Midgard prefetches instruction types, so during emission we
         * need to lookahead. Unless this is the last instruction, in
         * which we return 1. */

        mir_foreach_block(ctx, _block) {
                midgard_block *block = (midgard_block *) _block;
                mir_foreach_bundle_in_block(block, bundle) {
                        int lookahead = 1;

                        if (!bundle->last_writeout && (current_bundle + 1 < bundle_count))
                                lookahead = source_order_bundles[current_bundle + 1]->tag;

                        emit_binary_bundle(ctx, block, bundle, compiled, lookahead);
                        ++current_bundle;
                }

                /* TODO: Free deeper */
                //util_dynarray_fini(&block->instructions);
        }

        free(source_order_bundles);

        /* Report the very first tag executed */
        program->first_tag = midgard_get_first_tag_from_block(ctx, 0);

        /* Deal with off-by-one related to the fencepost problem */
        program->work_register_count = ctx->work_registers + 1;
        program->uniform_cutoff = ctx->uniform_cutoff;

        program->tls_size = ctx->tls_size;

        if ((midgard_debug & MIDGARD_DBG_SHADERS) && !nir->info.internal) {
                disassemble_midgard(stdout,
                                    program->compiled.data,
                                    program->compiled.size,
                                    inputs->gpu_id, ctx->stage);
        }

        if ((midgard_debug & MIDGARD_DBG_SHADERDB || inputs->shaderdb) &&
            !nir->info.internal) {
                unsigned nr_bundles = 0, nr_ins = 0;

                /* Count instructions and bundles */

                mir_foreach_block(ctx, _block) {
                        midgard_block *block = (midgard_block *) _block;
                        nr_bundles += util_dynarray_num_elements(
                                              &block->bundles, midgard_bundle);

                        mir_foreach_bundle_in_block(block, bun)
                                nr_ins += bun->instruction_count;
                }

                /* Calculate thread count. There are certain cutoffs by
                 * register count for thread count */

                unsigned nr_registers = program->work_register_count;

                unsigned nr_threads =
                        (nr_registers <= 4) ? 4 :
                        (nr_registers <= 8) ? 2 :
                        1;

                /* Dump stats */

                fprintf(stderr, "shader%d - %s shader: "
                        "%u inst, %u bundles, %u quadwords, "
                        "%u registers, %u threads, %u loops, "
                        "%u:%u spills:fills\n",
                        SHADER_DB_COUNT++,
                        ctx->is_blend ? "PAN_SHADER_BLEND" :
                        gl_shader_stage_name(ctx->stage),
                        nr_ins, nr_bundles, ctx->quadword_count,
                        nr_registers, nr_threads,
                        ctx->loop_count,
                        ctx->spills, ctx->fills);
        }

        ralloc_free(ctx);

        return program;
}
