/*
 * Copyright (C) 2018-2019 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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
#include "main/imports.h"
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

static bool
midgard_is_branch_unit(unsigned unit)
{
        return (unit == ALU_ENAB_BRANCH) || (unit == ALU_ENAB_BR_COMPACT);
}

static void
midgard_block_add_successor(midgard_block *block, midgard_block *successor)
{
        block->successors[block->nr_successors++] = successor;
        assert(block->nr_successors <= ARRAY_SIZE(block->successors));
}

/* Helpers to generate midgard_instruction's using macro magic, since every
 * driver seems to do it that way */

#define EMIT(op, ...) emit_mir_instruction(ctx, v_##op(__VA_ARGS__));

#define M_LOAD_STORE(name, rname, uname) \
	static midgard_instruction m_##name(unsigned ssa, unsigned address) { \
		midgard_instruction i = { \
			.type = TAG_LOAD_STORE_4, \
                        .mask = 0xF, \
			.ssa_args = { \
				.rname = ssa, \
				.uname = -1, \
				.src1 = -1 \
			}, \
			.load_store = { \
				.op = midgard_op_##name, \
				.swizzle = SWIZZLE_XYZW, \
				.address = address \
			} \
		}; \
		\
		return i; \
	}

#define M_LOAD(name) M_LOAD_STORE(name, dest, src0)
#define M_STORE(name) M_LOAD_STORE(name, src0, dest)

/* Inputs a NIR ALU source, with modifiers attached if necessary, and outputs
 * the corresponding Midgard source */

static midgard_vector_alu_src
vector_alu_modifiers(nir_alu_src *src, bool is_int, unsigned broadcast_count,
                     bool half, bool sext)
{
        if (!src) return blank_alu_src;

        /* Figure out how many components there are so we can adjust the
         * swizzle.  Specifically we want to broadcast the last channel so
         * things like ball2/3 work
         */

        if (broadcast_count) {
                uint8_t last_component = src->swizzle[broadcast_count - 1];

                for (unsigned c = broadcast_count; c < NIR_MAX_VEC_COMPONENTS; ++c) {
                        src->swizzle[c] = last_component;
                }
        }

        midgard_vector_alu_src alu_src = {
                .rep_low = 0,
                .rep_high = 0,
                .half = half,
                .swizzle = SWIZZLE_FROM_ARRAY(src->swizzle)
        };

        if (is_int) {
                alu_src.mod = midgard_int_normal;

                /* Sign/zero-extend if needed */

                if (half) {
                        alu_src.mod = sext ?
                                      midgard_int_sign_extend
                                      : midgard_int_zero_extend;
                }

                /* These should have been lowered away */
                assert(!(src->abs || src->negate));
        } else {
                alu_src.mod = (src->abs << 0) | (src->negate << 1);
        }

        return alu_src;
}

/* load/store instructions have both 32-bit and 16-bit variants, depending on
 * whether we are using vectors composed of highp or mediump. At the moment, we
 * don't support half-floats -- this requires changes in other parts of the
 * compiler -- therefore the 16-bit versions are commented out. */

//M_LOAD(ld_attr_16);
M_LOAD(ld_attr_32);
//M_LOAD(ld_vary_16);
M_LOAD(ld_vary_32);
//M_LOAD(ld_uniform_16);
M_LOAD(ld_uniform_32);
M_LOAD(ld_color_buffer_8);
//M_STORE(st_vary_16);
M_STORE(st_vary_32);
M_STORE(st_cubemap_coords);

static midgard_instruction
v_alu_br_compact_cond(midgard_jmp_writeout_op op, unsigned tag, signed offset, unsigned cond)
{
        midgard_branch_cond branch = {
                .op = op,
                .dest_tag = tag,
                .offset = offset,
                .cond = cond
        };

        uint16_t compact;
        memcpy(&compact, &branch, sizeof(branch));

        midgard_instruction ins = {
                .type = TAG_ALU_4,
                .unit = ALU_ENAB_BR_COMPACT,
                .prepacked_branch = true,
                .compact_branch = true,
                .br_compact = compact
        };

        if (op == midgard_jmp_writeout_op_writeout)
                ins.writeout = true;

        return ins;
}

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
                }
        };

        return ins;
}

static midgard_branch_extended
midgard_create_branch_extended( midgard_condition cond,
                                midgard_jmp_writeout_op op,
                                unsigned dest_tag,
                                signed quadword_offset)
{
        /* For unclear reasons, the condition code is repeated 8 times */
        uint16_t duplicated_cond =
                (cond << 14) |
                (cond << 12) |
                (cond << 10) |
                (cond << 8) |
                (cond << 6) |
                (cond << 4) |
                (cond << 2) |
                (cond << 0);

        midgard_branch_extended branch = {
                .op = op,
                .dest_tag = dest_tag,
                .offset = quadword_offset,
                .cond = duplicated_cond
        };

        return branch;
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
static void
midgard_nir_lower_fdot2_body(nir_builder *b, nir_alu_instr *alu)
{
        if (alu->op != nir_op_fdot2)
                return;

        b->cursor = nir_before_instr(&alu->instr);

        nir_ssa_def *src0 = nir_ssa_for_alu_src(b, alu, 0);
        nir_ssa_def *src1 = nir_ssa_for_alu_src(b, alu, 1);

        nir_ssa_def *product = nir_fmul(b, src0, src1);

        nir_ssa_def *sum = nir_fadd(b,
                                    nir_channel(b, product, 0),
                                    nir_channel(b, product, 1));

        /* Replace the fdot2 with this sum */
        nir_ssa_def_rewrite_uses(&alu->dest.dest.ssa, nir_src_for_ssa(sum));
}

static int
midgard_nir_sysval_for_intrinsic(nir_intrinsic_instr *instr)
{
        switch (instr->intrinsic) {
        case nir_intrinsic_load_viewport_scale:
                return PAN_SYSVAL_VIEWPORT_SCALE;
        case nir_intrinsic_load_viewport_offset:
                return PAN_SYSVAL_VIEWPORT_OFFSET;
        default:
                return -1;
        }
}

static unsigned
nir_dest_index(compiler_context *ctx, nir_dest *dst)
{
        if (dst->is_ssa)
                return dst->ssa.index;
        else {
                assert(!dst->reg.indirect);
                return ctx->func->impl->ssa_alloc + dst->reg.reg->index;
        }
}

static int sysval_for_instr(compiler_context *ctx, nir_instr *instr,
                            unsigned *dest)
{
        nir_intrinsic_instr *intr;
        nir_dest *dst = NULL;
        nir_tex_instr *tex;
        int sysval = -1;

        switch (instr->type) {
        case nir_instr_type_intrinsic:
                intr = nir_instr_as_intrinsic(instr);
                sysval = midgard_nir_sysval_for_intrinsic(intr);
                dst = &intr->dest;
                break;
        case nir_instr_type_tex:
                tex = nir_instr_as_tex(instr);
                if (tex->op != nir_texop_txs)
                        break;

                sysval = PAN_SYSVAL(TEXTURE_SIZE,
                                    PAN_TXS_SYSVAL_ID(tex->texture_index,
                                                      nir_tex_instr_dest_size(tex) -
                                                      (tex->is_array ? 1 : 0),
                                                      tex->is_array));
                dst  = &tex->dest;
                break;
        default:
                break;
        }

        if (dest && dst)
                *dest = nir_dest_index(ctx, dst);

        return sysval;
}

static void
midgard_nir_assign_sysval_body(compiler_context *ctx, nir_instr *instr)
{
        int sysval;

        sysval = sysval_for_instr(ctx, instr, NULL);
        if (sysval < 0)
                return;

        /* We have a sysval load; check if it's already been assigned */

        if (_mesa_hash_table_u64_search(ctx->sysval_to_id, sysval))
                return;

        /* It hasn't -- so assign it now! */

        unsigned id = ctx->sysval_count++;
        _mesa_hash_table_u64_insert(ctx->sysval_to_id, sysval, (void *) ((uintptr_t) id + 1));
        ctx->sysvals[id] = sysval;
}

static void
midgard_nir_assign_sysvals(compiler_context *ctx, nir_shader *shader)
{
        ctx->sysval_count = 0;

        nir_foreach_function(function, shader) {
                if (!function->impl) continue;

                nir_foreach_block(block, function->impl) {
                        nir_foreach_instr_safe(instr, block) {
                                midgard_nir_assign_sysval_body(ctx, instr);
                        }
                }
        }
}

static bool
midgard_nir_lower_fdot2(nir_shader *shader)
{
        bool progress = false;

        nir_foreach_function(function, shader) {
                if (!function->impl) continue;

                nir_builder _b;
                nir_builder *b = &_b;
                nir_builder_init(b, function->impl);

                nir_foreach_block(block, function->impl) {
                        nir_foreach_instr_safe(instr, block) {
                                if (instr->type != nir_instr_type_alu) continue;

                                nir_alu_instr *alu = nir_instr_as_alu(instr);
                                midgard_nir_lower_fdot2_body(b, alu);

                                progress |= true;
                        }
                }

                nir_metadata_preserve(function->impl, nir_metadata_block_index | nir_metadata_dominance);

        }

        return progress;
}

/* Flushes undefined values to zero */

static void
optimise_nir(nir_shader *nir)
{
        bool progress;
        unsigned lower_flrp =
                (nir->options->lower_flrp16 ? 16 : 0) |
                (nir->options->lower_flrp32 ? 32 : 0) |
                (nir->options->lower_flrp64 ? 64 : 0);

        NIR_PASS(progress, nir, nir_lower_regs_to_ssa);
        NIR_PASS(progress, nir, midgard_nir_lower_fdot2);
        NIR_PASS(progress, nir, nir_lower_idiv);

        nir_lower_tex_options lower_tex_1st_pass_options = {
                .lower_rect = true,
                .lower_txp = ~0
        };

        nir_lower_tex_options lower_tex_2nd_pass_options = {
                .lower_txs_lod = true,
        };

        NIR_PASS(progress, nir, nir_lower_tex, &lower_tex_1st_pass_options);
        NIR_PASS(progress, nir, nir_lower_tex, &lower_tex_2nd_pass_options);

        do {
                progress = false;

                NIR_PASS(progress, nir, nir_lower_var_copies);
                NIR_PASS(progress, nir, nir_lower_vars_to_ssa);

                NIR_PASS(progress, nir, nir_copy_prop);
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
                NIR_PASS(progress, nir, nir_undef_to_zero);

                NIR_PASS(progress, nir, nir_opt_loop_unroll,
                         nir_var_shader_in |
                         nir_var_shader_out |
                         nir_var_function_temp);

                NIR_PASS(progress, nir, nir_opt_vectorize);
        } while (progress);

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

        /* We implement booleans as 32-bit 0/~0 */
        NIR_PASS(progress, nir, nir_lower_bool_to_int32);

        /* Now that booleans are lowered, we can run out late opts */
        NIR_PASS(progress, nir, midgard_nir_lower_algebraic_late);

        /* Lower mods for float ops only. Integer ops don't support modifiers
         * (saturate doesn't make sense on integers, neg/abs require dedicated
         * instructions) */

        NIR_PASS(progress, nir, nir_lower_to_source_mods, nir_lower_float_source_mods);
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

        float *v = rzalloc_array(NULL, float, 4);
        nir_const_load_to_arr(v, instr, f32);
        _mesa_hash_table_u64_insert(ctx->ssa_constants, def.index + 1, v);
}

static unsigned
nir_src_index(compiler_context *ctx, nir_src *src)
{
        if (src->is_ssa)
                return src->ssa->index;
        else {
                assert(!src->reg.indirect);
                return ctx->func->impl->ssa_alloc + src->reg.reg->index;
        }
}

static unsigned
nir_alu_src_index(compiler_context *ctx, nir_alu_src *src)
{
        return nir_src_index(ctx, &src->src);
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

/* Midgard puts scalar conditionals in r31.w; move an arbitrary source (the
 * output of a conditional test) into that register */

static void
emit_condition(compiler_context *ctx, nir_src *src, bool for_branch, unsigned component)
{
        int condition = nir_src_index(ctx, src);

        /* Source to swizzle the desired component into w */

        const midgard_vector_alu_src alu_src = {
                .swizzle = SWIZZLE(component, component, component, component),
        };

        /* There is no boolean move instruction. Instead, we simulate a move by
         * ANDing the condition with itself to get it into r31.w */

        midgard_instruction ins = {
                .type = TAG_ALU_4,

                /* We need to set the conditional as close as possible */
                .precede_break = true,
                .unit = for_branch ? UNIT_SMUL : UNIT_SADD,
                .mask = 1 << COMPONENT_W,

                .ssa_args = {
                        .src0 = condition,
                        .src1 = condition,
                        .dest = SSA_FIXED_REGISTER(31),
                },

                .alu = {
                        .op = midgard_alu_op_iand,
                        .outmod = midgard_outmod_int_wrap,
                        .reg_mode = midgard_reg_mode_32,
                        .dest_override = midgard_dest_override_none,
                        .src1 = vector_alu_srco_unsigned(alu_src),
                        .src2 = vector_alu_srco_unsigned(alu_src)
                },
        };

        emit_mir_instruction(ctx, ins);
}

/* Or, for mixed conditions (with csel_v), here's a vector version using all of
 * r31 instead */

static void
emit_condition_mixed(compiler_context *ctx, nir_alu_src *src, unsigned nr_comp)
{
        int condition = nir_src_index(ctx, &src->src);

        /* Source to swizzle the desired component into w */

        const midgard_vector_alu_src alu_src = {
                .swizzle = SWIZZLE_FROM_ARRAY(src->swizzle),
        };

        /* There is no boolean move instruction. Instead, we simulate a move by
         * ANDing the condition with itself to get it into r31.w */

        midgard_instruction ins = {
                .type = TAG_ALU_4,
                .precede_break = true,
                .mask = mask_of(nr_comp),
                .ssa_args = {
                        .src0 = condition,
                        .src1 = condition,
                        .dest = SSA_FIXED_REGISTER(31),
                },
                .alu = {
                        .op = midgard_alu_op_iand,
                        .outmod = midgard_outmod_int_wrap,
                        .reg_mode = midgard_reg_mode_32,
                        .dest_override = midgard_dest_override_none,
                        .src1 = vector_alu_srco_unsigned(alu_src),
                        .src2 = vector_alu_srco_unsigned(alu_src)
                },
        };

        emit_mir_instruction(ctx, ins);
}



/* Likewise, indirect offsets are put in r27.w. TODO: Allow componentwise
 * pinning to eliminate this move in all known cases */

static void
emit_indirect_offset(compiler_context *ctx, nir_src *src)
{
        int offset = nir_src_index(ctx, src);

        midgard_instruction ins = {
                .type = TAG_ALU_4,
                .mask = 1 << COMPONENT_W,
                .ssa_args = {
                        .src0 = SSA_UNUSED_1,
                        .src1 = offset,
                        .dest = SSA_FIXED_REGISTER(REGISTER_OFFSET),
                },
                .alu = {
                        .op = midgard_alu_op_imov,
                        .outmod = midgard_outmod_int_wrap,
                        .reg_mode = midgard_reg_mode_32,
                        .dest_override = midgard_dest_override_none,
                        .src1 = vector_alu_srco_unsigned(zero_alu_src),
                        .src2 = vector_alu_srco_unsigned(blank_alu_src_xxxx)
                },
        };

        emit_mir_instruction(ctx, ins);
}

#define ALU_CASE(nir, _op) \
	case nir_op_##nir: \
		op = midgard_alu_op_##_op; \
                assert(src_bitsize == dst_bitsize); \
		break;

#define ALU_CASE_BCAST(nir, _op, count) \
        case nir_op_##nir: \
                op = midgard_alu_op_##_op; \
                broadcast_swizzle = count; \
                assert(src_bitsize == dst_bitsize); \
                break;
static bool
nir_is_fzero_constant(nir_src src)
{
        if (!nir_src_is_const(src))
                return false;

        for (unsigned c = 0; c < nir_src_num_components(src); ++c) {
                if (nir_src_comp_as_float(src, c) != 0.0)
                        return false;
        }

        return true;
}

/* Analyze the sizes of the inputs to determine which reg mode. Ops needed
 * special treatment override this anyway. */

static midgard_reg_mode
reg_mode_for_nir(nir_alu_instr *instr)
{
        unsigned src_bitsize = nir_src_bit_size(instr->src[0].src);

        switch (src_bitsize) {
        case 8:
                return midgard_reg_mode_8;
        case 16:
                return midgard_reg_mode_16;
        case 32:
                return midgard_reg_mode_32;
        case 64:
                return midgard_reg_mode_64;
        default:
                unreachable("Invalid bit size");
        }
}

static void
emit_alu(compiler_context *ctx, nir_alu_instr *instr)
{
        bool is_ssa = instr->dest.dest.is_ssa;

        unsigned dest = nir_dest_index(ctx, &instr->dest.dest);
        unsigned nr_components = nir_dest_num_components(instr->dest.dest);
        unsigned nr_inputs = nir_op_infos[instr->op].num_inputs;

        /* Most Midgard ALU ops have a 1:1 correspondance to NIR ops; these are
         * supported. A few do not and are commented for now. Also, there are a
         * number of NIR ops which Midgard does not support and need to be
         * lowered, also TODO. This switch block emits the opcode and calling
         * convention of the Midgard instruction; actual packing is done in
         * emit_alu below */

        unsigned op;

        /* Number of components valid to check for the instruction (the rest
         * will be forced to the last), or 0 to use as-is. Relevant as
         * ball-type instructions have a channel count in NIR but are all vec4
         * in Midgard */

        unsigned broadcast_swizzle = 0;

        /* What register mode should we operate in? */
        midgard_reg_mode reg_mode =
                reg_mode_for_nir(instr);

        /* Do we need a destination override? Used for inline
         * type conversion */

        midgard_dest_override dest_override =
                midgard_dest_override_none;

        /* Should we use a smaller respective source and sign-extend?  */

        bool half_1 = false, sext_1 = false;
        bool half_2 = false, sext_2 = false;

        unsigned src_bitsize = nir_src_bit_size(instr->src[0].src);
        unsigned dst_bitsize = nir_dest_bit_size(instr->dest.dest);

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

                /* Zero shoved as second-arg */
                ALU_CASE(iabs, iabsdiff);

                ALU_CASE(mov, imov);

                ALU_CASE(feq32, feq);
                ALU_CASE(fne32, fne);
                ALU_CASE(flt32, flt);
                ALU_CASE(ieq32, ieq);
                ALU_CASE(ine32, ine);
                ALU_CASE(ilt32, ilt);
                ALU_CASE(ult32, ult);

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

                ALU_CASE(b2f32, iand);
                ALU_CASE(b2i32, iand);

                /* Likewise, we don't have a dedicated f2b32 instruction, but
                 * we can do a "not equal to 0.0" test. */

                ALU_CASE(f2b32, fne);
                ALU_CASE(i2b32, ine);

                ALU_CASE(frcp, frcp);
                ALU_CASE(frsq, frsqrt);
                ALU_CASE(fsqrt, fsqrt);
                ALU_CASE(fexp2, fexp2);
                ALU_CASE(flog2, flog2);

                ALU_CASE(f2i32, f2i_rtz);
                ALU_CASE(f2u32, f2u_rtz);
                ALU_CASE(i2f32, i2f_rtz);
                ALU_CASE(u2f32, u2f_rtz);

                ALU_CASE(f2i16, f2i_rtz);
                ALU_CASE(f2u16, f2u_rtz);
                ALU_CASE(i2f16, i2f_rtz);
                ALU_CASE(u2f16, u2f_rtz);

                ALU_CASE(fsin, fsin);
                ALU_CASE(fcos, fcos);

                /* Second op implicit #0 */
                ALU_CASE(inot, inor);
                ALU_CASE(iand, iand);
                ALU_CASE(ior, ior);
                ALU_CASE(ixor, ixor);
                ALU_CASE(ishl, ishl);
                ALU_CASE(ishr, iasr);
                ALU_CASE(ushr, ilsr);

                ALU_CASE_BCAST(b32all_fequal2, fball_eq, 2);
                ALU_CASE_BCAST(b32all_fequal3, fball_eq, 3);
                ALU_CASE(b32all_fequal4, fball_eq);

                ALU_CASE_BCAST(b32any_fnequal2, fbany_neq, 2);
                ALU_CASE_BCAST(b32any_fnequal3, fbany_neq, 3);
                ALU_CASE(b32any_fnequal4, fbany_neq);

                ALU_CASE_BCAST(b32all_iequal2, iball_eq, 2);
                ALU_CASE_BCAST(b32all_iequal3, iball_eq, 3);
                ALU_CASE(b32all_iequal4, iball_eq);

                ALU_CASE_BCAST(b32any_inequal2, ibany_neq, 2);
                ALU_CASE_BCAST(b32any_inequal3, ibany_neq, 3);
                ALU_CASE(b32any_inequal4, ibany_neq);

                /* Source mods will be shoved in later */
                ALU_CASE(fabs, fmov);
                ALU_CASE(fneg, fmov);
                ALU_CASE(fsat, fmov);

        /* For size conversion, we use a move. Ideally though we would squash
         * these ops together; maybe that has to happen after in NIR as part of
         * propagation...? An earlier algebraic pass ensured we step down by
         * only / exactly one size. If stepping down, we use a dest override to
         * reduce the size; if stepping up, we use a larger-sized move with a
         * half source and a sign/zero-extension modifier */

        case nir_op_i2i8:
        case nir_op_i2i16:
        case nir_op_i2i32:
                /* If we end up upscale, we'll need a sign-extend on the
                 * operand (the second argument) */

                sext_2 = true;
        case nir_op_u2u8:
        case nir_op_u2u16:
        case nir_op_u2u32: {
                op = midgard_alu_op_imov;

                if (dst_bitsize == (src_bitsize * 2)) {
                        /* Converting up */
                        half_2 = true;

                        /* Use a greater register mode */
                        reg_mode++;
                } else if (src_bitsize == (dst_bitsize * 2)) {
                        /* Converting down */
                        dest_override = midgard_dest_override_lower;
                }

                break;
        }

        case nir_op_f2f16: {
                assert(src_bitsize == 32);

                op = midgard_alu_op_fmov;
                dest_override = midgard_dest_override_lower;
                break;
        }

        case nir_op_f2f32: {
                assert(src_bitsize == 16);

                op = midgard_alu_op_fmov;
                half_2 = true;
                reg_mode++;
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

                /* Swap via temporary */
                nir_alu_src temp = instr->src[1];
                instr->src[1] = instr->src[0];
                instr->src[0] = temp;

                break;
        }

        case nir_op_b32csel: {
                /* Midgard features both fcsel and icsel, depending on
                 * the type of the arguments/output. However, as long
                 * as we're careful we can _always_ use icsel and
                 * _never_ need fcsel, since the latter does additional
                 * floating-point-specific processing whereas the
                 * former just moves bits on the wire. It's not obvious
                 * why these are separate opcodes, save for the ability
                 * to do things like sat/pos/abs/neg for free */

                bool mixed = nir_is_non_scalar_swizzle(&instr->src[0], nr_components);
                op = mixed ? midgard_alu_op_icsel_v : midgard_alu_op_icsel;

                /* csel works as a two-arg in Midgard, since the condition is hardcoded in r31.w */
                nr_inputs = 2;

                /* Emit the condition into r31 */

                if (mixed)
                        emit_condition_mixed(ctx, &instr->src[0], nr_components);
                else
                        emit_condition(ctx, &instr->src[0].src, false, instr->src[0].swizzle[0]);

                /* The condition is the first argument; move the other
                 * arguments up one to be a binary instruction for
                 * Midgard */

                memmove(instr->src, instr->src + 1, 2 * sizeof(nir_alu_src));
                break;
        }

        default:
                DBG("Unhandled ALU op %s\n", nir_op_infos[instr->op].name);
                assert(0);
                return;
        }

        /* Midgard can perform certain modifiers on output of an ALU op */
        unsigned outmod;

        if (midgard_is_integer_out_op(op)) {
                outmod = midgard_outmod_int_wrap;
        } else {
                bool sat = instr->dest.saturate || instr->op == nir_op_fsat;
                outmod = sat ? midgard_outmod_sat : midgard_outmod_none;
        }

        /* fmax(a, 0.0) can turn into a .pos modifier as an optimization */

        if (instr->op == nir_op_fmax) {
                if (nir_is_fzero_constant(instr->src[0].src)) {
                        op = midgard_alu_op_fmov;
                        nr_inputs = 1;
                        outmod = midgard_outmod_pos;
                        instr->src[0] = instr->src[1];
                } else if (nir_is_fzero_constant(instr->src[1].src)) {
                        op = midgard_alu_op_fmov;
                        nr_inputs = 1;
                        outmod = midgard_outmod_pos;
                }
        }

        /* Fetch unit, quirks, etc information */
        unsigned opcode_props = alu_opcode_props[op].props;
        bool quirk_flipped_r24 = opcode_props & QUIRK_FLIPPED_R24;

        /* src0 will always exist afaik, but src1 will not for 1-argument
         * instructions. The latter can only be fetched if the instruction
         * needs it, or else we may segfault. */

        unsigned src0 = nir_alu_src_index(ctx, &instr->src[0]);
        unsigned src1 = nr_inputs == 2 ? nir_alu_src_index(ctx, &instr->src[1]) : SSA_UNUSED_0;

        /* Rather than use the instruction generation helpers, we do it
         * ourselves here to avoid the mess */

        midgard_instruction ins = {
                .type = TAG_ALU_4,
                .ssa_args = {
                        .src0 = quirk_flipped_r24 ? SSA_UNUSED_1 : src0,
                        .src1 = quirk_flipped_r24 ? src0         : src1,
                        .dest = dest,
                }
        };

        nir_alu_src *nirmods[2] = { NULL };

        if (nr_inputs == 2) {
                nirmods[0] = &instr->src[0];
                nirmods[1] = &instr->src[1];
        } else if (nr_inputs == 1) {
                nirmods[quirk_flipped_r24] = &instr->src[0];
        } else {
                assert(0);
        }

        /* These were lowered to a move, so apply the corresponding mod */

        if (instr->op == nir_op_fneg || instr->op == nir_op_fabs) {
                nir_alu_src *s = nirmods[quirk_flipped_r24];

                if (instr->op == nir_op_fneg)
                        s->negate = !s->negate;

                if (instr->op == nir_op_fabs)
                        s->abs = !s->abs;
        }

        bool is_int = midgard_is_integer_op(op);

        ins.mask = mask_of(nr_components);

        midgard_vector_alu alu = {
                .op = op,
                .reg_mode = reg_mode,
                .dest_override = dest_override,
                .outmod = outmod,

                .src1 = vector_alu_srco_unsigned(vector_alu_modifiers(nirmods[0], is_int, broadcast_swizzle, half_1, sext_1)),
                .src2 = vector_alu_srco_unsigned(vector_alu_modifiers(nirmods[1], is_int, broadcast_swizzle, half_2, sext_2)),
        };

        /* Apply writemask if non-SSA, keeping in mind that we can't write to components that don't exist */

        if (!is_ssa)
                ins.mask &= instr->dest.write_mask;

        ins.alu = alu;

        /* Late fixup for emulated instructions */

        if (instr->op == nir_op_b2f32 || instr->op == nir_op_b2i32) {
                /* Presently, our second argument is an inline #0 constant.
                 * Switch over to an embedded 1.0 constant (that can't fit
                 * inline, since we're 32-bit, not 16-bit like the inline
                 * constants) */

                ins.ssa_args.inline_constant = false;
                ins.ssa_args.src1 = SSA_FIXED_REGISTER(REGISTER_CONSTANT);
                ins.has_constants = true;

                if (instr->op == nir_op_b2f32) {
                        ins.constants[0] = 1.0f;
                } else {
                        /* Type pun it into place */
                        uint32_t one = 0x1;
                        memcpy(&ins.constants[0], &one, sizeof(uint32_t));
                }

                ins.alu.src2 = vector_alu_srco_unsigned(blank_alu_src_xxxx);
        } else if (nr_inputs == 1 && !quirk_flipped_r24) {
                /* Lots of instructions need a 0 plonked in */
                ins.ssa_args.inline_constant = false;
                ins.ssa_args.src1 = SSA_FIXED_REGISTER(REGISTER_CONSTANT);
                ins.has_constants = true;
                ins.constants[0] = 0.0f;
                ins.alu.src2 = vector_alu_srco_unsigned(blank_alu_src_xxxx);
        } else if (instr->op == nir_op_inot) {
                /* ~b = ~(b & b), so duplicate the source */
                ins.ssa_args.src1 = ins.ssa_args.src0;
                ins.alu.src2 = ins.alu.src1;
        }

        if ((opcode_props & UNITS_ALL) == UNIT_VLUT) {
                /* To avoid duplicating the lookup tables (probably), true LUT
                 * instructions can only operate as if they were scalars. Lower
                 * them here by changing the component. */

                uint8_t original_swizzle[4];
                memcpy(original_swizzle, nirmods[0]->swizzle, sizeof(nirmods[0]->swizzle));
                unsigned orig_mask = ins.mask;

                for (int i = 0; i < nr_components; ++i) {
                        /* Mask the associated component, dropping the
                         * instruction if needed */

                        ins.mask = 1 << i;
                        ins.mask &= orig_mask;

                        if (!ins.mask)
                                continue;

                        for (int j = 0; j < 4; ++j)
                                nirmods[0]->swizzle[j] = original_swizzle[i]; /* Pull from the correct component */

                        ins.alu.src1 = vector_alu_srco_unsigned(vector_alu_modifiers(nirmods[0], is_int, broadcast_swizzle, half_1, false));
                        emit_mir_instruction(ctx, ins);
                }
        } else {
                emit_mir_instruction(ctx, ins);
        }
}

#undef ALU_CASE

/* Uniforms and UBOs use a shared code path, as uniforms are just (slightly
 * optimized) versions of UBO #0 */

void
emit_ubo_read(
        compiler_context *ctx,
        unsigned dest,
        unsigned offset,
        nir_src *indirect_offset,
        unsigned index)
{
        /* TODO: half-floats */

        midgard_instruction ins = m_ld_uniform_32(dest, offset);

        /* TODO: Don't split */
        ins.load_store.varying_parameters = (offset & 7) << 7;
        ins.load_store.address = offset >> 3;

        if (indirect_offset) {
                emit_indirect_offset(ctx, indirect_offset);
                ins.load_store.unknown = 0x8700 | index; /* xxx: what is this? */
        } else {
                ins.load_store.unknown = 0x1E00 | index; /* xxx: what is this? */
        }

        emit_mir_instruction(ctx, ins);
}

static void
emit_varying_read(
        compiler_context *ctx,
        unsigned dest, unsigned offset,
        unsigned nr_comp, unsigned component,
        nir_src *indirect_offset, nir_alu_type type)
{
        /* XXX: Half-floats? */
        /* TODO: swizzle, mask */

        midgard_instruction ins = m_ld_vary_32(dest, offset);
        ins.mask = mask_of(nr_comp);
        ins.load_store.swizzle = SWIZZLE_XYZW >> (2 * component);

        midgard_varying_parameter p = {
                .is_varying = 1,
                .interpolation = midgard_interp_default,
                .flat = /*var->data.interpolation == INTERP_MODE_FLAT*/ 0
        };

        unsigned u;
        memcpy(&u, &p, sizeof(p));
        ins.load_store.varying_parameters = u;

        if (indirect_offset) {
                /* We need to add in the dynamic index, moved to r27.w */
                emit_indirect_offset(ctx, indirect_offset);
                ins.load_store.unknown = 0x79e; /* xxx: what is this? */
        } else {
                /* Just a direct load */
                ins.load_store.unknown = 0x1e9e; /* xxx: what is this? */
        }

        /* Use the type appropriate load */
        switch (type) {
        case nir_type_uint:
        case nir_type_bool:
                ins.load_store.op = midgard_op_ld_vary_32u;
                break;
        case nir_type_int:
                ins.load_store.op = midgard_op_ld_vary_32i;
                break;
        case nir_type_float:
                ins.load_store.op = midgard_op_ld_vary_32;
                break;
        default:
                unreachable("Attempted to load unknown type");
                break;
        }

        emit_mir_instruction(ctx, ins);
}

static void
emit_sysval_read(compiler_context *ctx, nir_instr *instr)
{
        unsigned dest = 0;

        /* Figure out which uniform this is */
        int sysval = sysval_for_instr(ctx, instr, &dest);
        void *val = _mesa_hash_table_u64_search(ctx->sysval_to_id, sysval);

        /* Sysvals are prefix uniforms */
        unsigned uniform = ((uintptr_t) val) - 1;

        /* Emit the read itself -- this is never indirect */
        emit_ubo_read(ctx, dest, uniform, NULL, 0);
}

static void
emit_intrinsic(compiler_context *ctx, nir_intrinsic_instr *instr)
{
        unsigned offset = 0, reg;

        switch (instr->intrinsic) {
        case nir_intrinsic_discard_if:
                emit_condition(ctx, &instr->src[0], true, COMPONENT_X);

        /* fallthrough */

        case nir_intrinsic_discard: {
                bool conditional = instr->intrinsic == nir_intrinsic_discard_if;
                struct midgard_instruction discard = v_branch(conditional, false);
                discard.branch.target_type = TARGET_DISCARD;
                emit_mir_instruction(ctx, discard);

                ctx->can_discard = true;
                break;
        }

        case nir_intrinsic_load_uniform:
        case nir_intrinsic_load_ubo:
        case nir_intrinsic_load_input: {
                bool is_uniform = instr->intrinsic == nir_intrinsic_load_uniform;
                bool is_ubo = instr->intrinsic == nir_intrinsic_load_ubo;

                /* Get the base type of the intrinsic */
                /* TODO: Infer type? Does it matter? */
                nir_alu_type t =
                        is_ubo ? nir_type_uint : nir_intrinsic_type(instr);
                t = nir_alu_type_get_base_type(t);

                if (!is_ubo) {
                        offset = nir_intrinsic_base(instr);
                }

                unsigned nr_comp = nir_intrinsic_dest_components(instr);

                nir_src *src_offset = nir_get_io_offset_src(instr);

                bool direct = nir_src_is_const(*src_offset);

                if (direct)
                        offset += nir_src_as_uint(*src_offset);

                /* We may need to apply a fractional offset */
                int component = instr->intrinsic == nir_intrinsic_load_input ?
                                nir_intrinsic_component(instr) : 0;
                reg = nir_dest_index(ctx, &instr->dest);

                if (is_uniform && !ctx->is_blend) {
                        emit_ubo_read(ctx, reg, ctx->sysval_count + offset, !direct ? &instr->src[0] : NULL, 0);
                } else if (is_ubo) {
                        nir_src index = instr->src[0];

                        /* We don't yet support indirect UBOs. For indirect
                         * block numbers (if that's possible), we don't know
                         * enough about the hardware yet. For indirect sources,
                         * we know what we need but we need to add some NIR
                         * support for lowering correctly with respect to
                         * 128-bit reads */

                        assert(nir_src_is_const(index));
                        assert(nir_src_is_const(*src_offset));

                        /* TODO: Alignment */
                        assert((offset & 0xF) == 0);

                        uint32_t uindex = nir_src_as_uint(index) + 1;
                        emit_ubo_read(ctx, reg, offset / 16, NULL, uindex);
                } else if (ctx->stage == MESA_SHADER_FRAGMENT && !ctx->is_blend) {
                        emit_varying_read(ctx, reg, offset, nr_comp, component, !direct ? &instr->src[0] : NULL, t);
                } else if (ctx->is_blend) {
                        /* For blend shaders, load the input color, which is
                         * preloaded to r0 */

                        midgard_instruction move = v_mov(reg, blank_alu_src, SSA_FIXED_REGISTER(0));
                        emit_mir_instruction(ctx, move);
                }  else if (ctx->stage == MESA_SHADER_VERTEX) {
                        midgard_instruction ins = m_ld_attr_32(reg, offset);
                        ins.load_store.unknown = 0x1E1E; /* XXX: What is this? */
                        ins.mask = mask_of(nr_comp);

                        /* Use the type appropriate load */
                        switch (t) {
                        case nir_type_uint:
                        case nir_type_bool:
                                ins.load_store.op = midgard_op_ld_attr_32u;
                                break;
                        case nir_type_int:
                                ins.load_store.op = midgard_op_ld_attr_32i;
                                break;
                        case nir_type_float:
                                ins.load_store.op = midgard_op_ld_attr_32;
                                break;
                        default:
                                unreachable("Attempted to load unknown type");
                                break;
                        }

                        emit_mir_instruction(ctx, ins);
                } else {
                        DBG("Unknown load\n");
                        assert(0);
                }

                break;
        }

        /* Reads 128-bit value raw off the tilebuffer during blending, tasty */

        case nir_intrinsic_load_raw_output_pan:
                reg = nir_dest_index(ctx, &instr->dest);
                assert(ctx->is_blend);

                midgard_instruction ins = m_ld_color_buffer_8(reg, 0);
                emit_mir_instruction(ctx, ins);
                break;

        case nir_intrinsic_load_blend_const_color_rgba: {
                assert(ctx->is_blend);
                reg = nir_dest_index(ctx, &instr->dest);

                /* Blend constants are embedded directly in the shader and
                 * patched in, so we use some magic routing */

                midgard_instruction ins = v_mov(SSA_FIXED_REGISTER(REGISTER_CONSTANT), blank_alu_src, reg);
                ins.has_constants = true;
                ins.has_blend_constant = true;
                emit_mir_instruction(ctx, ins);
                break;
        }

        case nir_intrinsic_store_output:
                assert(nir_src_is_const(instr->src[1]) && "no indirect outputs");

                offset = nir_intrinsic_base(instr) + nir_src_as_uint(instr->src[1]);

                reg = nir_src_index(ctx, &instr->src[0]);

                if (ctx->stage == MESA_SHADER_FRAGMENT) {
                        /* gl_FragColor is not emitted with load/store
                         * instructions. Instead, it gets plonked into
                         * r0 at the end of the shader and we do the
                         * framebuffer writeout dance. TODO: Defer
                         * writes */

                        midgard_instruction move = v_mov(reg, blank_alu_src, SSA_FIXED_REGISTER(0));
                        emit_mir_instruction(ctx, move);

                        /* Save the index we're writing to for later reference
                         * in the epilogue */

                        ctx->fragment_output = reg;
                } else if (ctx->stage == MESA_SHADER_VERTEX) {
                        /* Varyings are written into one of two special
                         * varying register, r26 or r27. The register itself is
                         * selected as the register in the st_vary instruction,
                         * minus the base of 26. E.g. write into r27 and then
                         * call st_vary(1) */

                        midgard_instruction ins = v_mov(reg, blank_alu_src, SSA_FIXED_REGISTER(26));
                        emit_mir_instruction(ctx, ins);

                        /* We should have been vectorized, though we don't
                         * currently check that st_vary is emitted only once
                         * per slot (this is relevant, since there's not a mask
                         * parameter available on the store [set to 0 by the
                         * blob]). We do respect the component by adjusting the
                         * swizzle. */

                        unsigned component = nir_intrinsic_component(instr);

                        midgard_instruction st = m_st_vary_32(SSA_FIXED_REGISTER(0), offset);
                        st.load_store.unknown = 0x1E9E; /* XXX: What is this? */
                        st.load_store.swizzle = SWIZZLE_XYZW << (2*component);
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

                midgard_instruction move = v_mov(reg, blank_alu_src, SSA_FIXED_REGISTER(0));
                emit_mir_instruction(ctx, move);
                ctx->fragment_output = reg;

                break;

        case nir_intrinsic_load_alpha_ref_float:
                assert(instr->dest.is_ssa);

                float ref_value = ctx->alpha_ref;

                float *v = ralloc_array(NULL, float, 4);
                memcpy(v, &ref_value, sizeof(float));
                _mesa_hash_table_u64_insert(ctx->ssa_constants, instr->dest.ssa.index + 1, v);
                break;

        case nir_intrinsic_load_viewport_scale:
        case nir_intrinsic_load_viewport_offset:
                emit_sysval_read(ctx, &instr->instr);
                break;

        default:
                printf ("Unhandled intrinsic\n");
                assert(0);
                break;
        }
}

static unsigned
midgard_tex_format(enum glsl_sampler_dim dim)
{
        switch (dim) {
        case GLSL_SAMPLER_DIM_1D:
        case GLSL_SAMPLER_DIM_BUF:
                return MALI_TEX_1D;

        case GLSL_SAMPLER_DIM_2D:
        case GLSL_SAMPLER_DIM_EXTERNAL:
                return MALI_TEX_2D;

        case GLSL_SAMPLER_DIM_3D:
                return MALI_TEX_3D;

        case GLSL_SAMPLER_DIM_CUBE:
                return MALI_TEX_CUBE;

        default:
                DBG("Unknown sampler dim type\n");
                assert(0);
                return 0;
        }
}

/* Tries to attach an explicit LOD / bias as a constant. Returns whether this
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

static enum mali_sampler_type
midgard_sampler_type(nir_alu_type t) {
        switch (nir_alu_type_get_base_type(t))
        {
        case nir_type_float:
                                return MALI_SAMPLER_FLOAT;
        case nir_type_int:
                return MALI_SAMPLER_SIGNED;
        case nir_type_uint:
                return MALI_SAMPLER_UNSIGNED;
        default:
                unreachable("Unknown sampler type");
        }
}

static void
emit_texop_native(compiler_context *ctx, nir_tex_instr *instr,
                  unsigned midgard_texop)
{
        /* TODO */
        //assert (!instr->sampler);
        //assert (!instr->texture_array_size);

        /* Allocate registers via a round robin scheme to alternate between the two registers */
        int reg = ctx->texture_op_count & 1;
        int in_reg = reg, out_reg = reg;

        int texture_index = instr->texture_index;
        int sampler_index = texture_index;

        /* No helper to build texture words -- we do it all here */
        midgard_instruction ins = {
                .type = TAG_TEXTURE_4,
                .mask = 0xF,
                .texture = {
                        .op = midgard_texop,
                        .format = midgard_tex_format(instr->sampler_dim),
                        .texture_handle = texture_index,
                        .sampler_handle = sampler_index,

                        /* TODO: Regalloc it in */
                        .swizzle = SWIZZLE_XYZW,

                        /* TODO: half */
                        .in_reg_full = 1,
                        .out_full = 1,

                        .sampler_type = midgard_sampler_type(instr->dest_type),
                }
        };

        for (unsigned i = 0; i < instr->num_srcs; ++i) {
                int reg = SSA_FIXED_REGISTER(REGISTER_TEXTURE_BASE + in_reg);
                int index = nir_src_index(ctx, &instr->src[i].src);
                int nr_comp = nir_src_num_components(instr->src[i].src);
                midgard_vector_alu_src alu_src = blank_alu_src;

                switch (instr->src[i].src_type) {
                case nir_tex_src_coord: {
                        if (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE) {
                                /* texelFetch is undefined on samplerCube */
                                assert(midgard_texop != TEXTURE_OP_TEXEL_FETCH);

                                /* For cubemaps, we need to load coords into
                                 * special r27, and then use a special ld/st op
                                 * to select the face and copy the xy into the
                                 * texture register */

                                alu_src.swizzle = SWIZZLE(COMPONENT_X, COMPONENT_Y, COMPONENT_Z, COMPONENT_X);

                                midgard_instruction move = v_mov(index, alu_src, SSA_FIXED_REGISTER(27));
                                emit_mir_instruction(ctx, move);

                                midgard_instruction st = m_st_cubemap_coords(reg, 0);
                                st.load_store.unknown = 0x24; /* XXX: What is this? */
                                st.mask = 0x3; /* xy */
                                st.load_store.swizzle = alu_src.swizzle;
                                emit_mir_instruction(ctx, st);

                                ins.texture.in_reg_swizzle = swizzle_of(2);
                        } else {
                                ins.texture.in_reg_swizzle = alu_src.swizzle = swizzle_of(nr_comp);

                                midgard_instruction mov = v_mov(index, alu_src, reg);
                                mov.mask = mask_of(nr_comp);
                                emit_mir_instruction(ctx, mov);

                                if (midgard_texop == TEXTURE_OP_TEXEL_FETCH) {
                                        /* Texel fetch opcodes care about the
                                         * values of z and w, so we actually
                                         * need to spill into a second register
                                         * for a texel fetch with register bias
                                         * (for non-2D). TODO: Implement that
                                         */

                                        assert(instr->sampler_dim == GLSL_SAMPLER_DIM_2D);

                                        midgard_instruction zero = v_mov(index, alu_src, reg);
                                        zero.ssa_args.inline_constant = true;
                                        zero.ssa_args.src1 = SSA_FIXED_REGISTER(REGISTER_CONSTANT);
                                        zero.has_constants = true;
                                        zero.mask = ~mov.mask;
                                        emit_mir_instruction(ctx, zero);

                                        ins.texture.in_reg_swizzle = SWIZZLE_XYZZ;
                                } else {
                                        /* Non-texel fetch doesn't need that
                                         * nonsense. However we do use the Z
                                         * for array indexing */
                                        bool is_3d = instr->sampler_dim == GLSL_SAMPLER_DIM_3D;
                                        ins.texture.in_reg_swizzle = is_3d ? SWIZZLE_XYZZ : SWIZZLE_XYXZ;
                                }
                        }

                        break;
                }

                case nir_tex_src_bias:
                case nir_tex_src_lod: {
                        /* Try as a constant if we can */

                        bool is_txf = midgard_texop == TEXTURE_OP_TEXEL_FETCH;
                        if (!is_txf && pan_attach_constant_bias(ctx, instr->src[i].src, &ins.texture))
                                break;

                        /* Otherwise we use a register. To keep RA simple, we
                         * put the bias/LOD into the w component of the input
                         * source, which is otherwise in xy */

                        alu_src.swizzle = SWIZZLE_XXXX;

                        midgard_instruction mov = v_mov(index, alu_src, reg);
                        mov.mask = 1 << COMPONENT_W;
                        emit_mir_instruction(ctx, mov);

                        ins.texture.lod_register = true;

                        midgard_tex_register_select sel = {
                                .select = in_reg,
                                .full = 1,

                                /* w */
                                .component_lo = 1,
                                .component_hi = 1
                        };

                        uint8_t packed;
                        memcpy(&packed, &sel, sizeof(packed));
                        ins.texture.bias = packed;

                        break;
                };

                default:
                        unreachable("Unknown texture source type\n");
                }
        }

        /* Set registers to read and write from the same place */
        ins.texture.in_reg_select = in_reg;
        ins.texture.out_reg_select = out_reg;

        emit_mir_instruction(ctx, ins);

        int o_reg = REGISTER_TEXTURE_BASE + out_reg, o_index = nir_dest_index(ctx, &instr->dest);
        midgard_instruction ins2 = v_mov(SSA_FIXED_REGISTER(o_reg), blank_alu_src, o_index);
        emit_mir_instruction(ctx, ins2);

        /* Used for .cont and .last hinting */
        ctx->texture_op_count++;
}

static void
emit_tex(compiler_context *ctx, nir_tex_instr *instr)
{
        /* Fixup op, since only textureLod is permitted in VS but NIR can give
         * generic tex in some cases (which confuses the hardware) */

        bool is_vertex = ctx->stage == MESA_SHADER_VERTEX;

        if (is_vertex && instr->op == nir_texop_tex)
                instr->op = nir_texop_txl;

        switch (instr->op) {
        case nir_texop_tex:
        case nir_texop_txb:
                emit_texop_native(ctx, instr, TEXTURE_OP_NORMAL);
                break;
        case nir_texop_txl:
                emit_texop_native(ctx, instr, TEXTURE_OP_LOD);
                break;
        case nir_texop_txf:
                emit_texop_native(ctx, instr, TEXTURE_OP_TEXEL_FETCH);
                break;
        case nir_texop_txs:
                emit_sysval_read(ctx, &instr->instr);
                break;
        default:
                unreachable("Unhanlded texture op");
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

#define CONDITIONAL_ATTACH(src) { \
	void *entry = _mesa_hash_table_u64_search(ctx->ssa_constants, alu->ssa_args.src + 1); \
\
	if (entry) { \
		attach_constants(ctx, alu, entry, alu->ssa_args.src + 1); \
		alu->ssa_args.src = SSA_FIXED_REGISTER(REGISTER_CONSTANT); \
	} \
}

static void
inline_alu_constants(compiler_context *ctx)
{
        mir_foreach_instr(ctx, alu) {
                /* Other instructions cannot inline constants */
                if (alu->type != TAG_ALU_4) continue;

                /* If there is already a constant here, we can do nothing */
                if (alu->has_constants) continue;

                /* It makes no sense to inline constants on a branch */
                if (alu->compact_branch || alu->prepacked_branch) continue;

                CONDITIONAL_ATTACH(src0);

                if (!alu->has_constants) {
                        CONDITIONAL_ATTACH(src1)
                } else if (!alu->inline_constant) {
                        /* Corner case: _two_ vec4 constants, for instance with a
                         * csel. For this case, we can only use a constant
                         * register for one, we'll have to emit a move for the
                         * other. Note, if both arguments are constants, then
                         * necessarily neither argument depends on the value of
                         * any particular register. As the destination register
                         * will be wiped, that means we can spill the constant
                         * to the destination register.
                         */

                        void *entry = _mesa_hash_table_u64_search(ctx->ssa_constants, alu->ssa_args.src1 + 1);
                        unsigned scratch = alu->ssa_args.dest;

                        if (entry) {
                                midgard_instruction ins = v_mov(SSA_FIXED_REGISTER(REGISTER_CONSTANT), blank_alu_src, scratch);
                                attach_constants(ctx, &ins, entry, alu->ssa_args.src1 + 1);

                                /* Force a break XXX Defer r31 writes */
                                ins.unit = UNIT_VLUT;

                                /* Set the source */
                                alu->ssa_args.src1 = scratch;

                                /* Inject us -before- the last instruction which set r31 */
                                mir_insert_instruction_before(mir_prev_op(alu), ins);
                        }
                }
        }
}

/* Midgard supports two types of constants, embedded constants (128-bit) and
 * inline constants (16-bit). Sometimes, especially with scalar ops, embedded
 * constants can be demoted to inline constants, for space savings and
 * sometimes a performance boost */

static void
embedded_to_inline_constant(compiler_context *ctx)
{
        mir_foreach_instr(ctx, ins) {
                if (!ins->has_constants) continue;

                if (ins->ssa_args.inline_constant) continue;

                /* Blend constants must not be inlined by definition */
                if (ins->has_blend_constant) continue;

                /* We can inline 32-bit (sometimes) or 16-bit (usually) */
                bool is_16 = ins->alu.reg_mode == midgard_reg_mode_16;
                bool is_32 = ins->alu.reg_mode == midgard_reg_mode_32;

                if (!(is_16 || is_32))
                        continue;

                /* src1 cannot be an inline constant due to encoding
                 * restrictions. So, if possible we try to flip the arguments
                 * in that case */

                int op = ins->alu.op;

                if (ins->ssa_args.src0 == SSA_FIXED_REGISTER(REGISTER_CONSTANT)) {
                        switch (op) {
                        /* These ops require an operational change to flip
                         * their arguments TODO */
                        case midgard_alu_op_flt:
                        case midgard_alu_op_fle:
                        case midgard_alu_op_ilt:
                        case midgard_alu_op_ile:
                        case midgard_alu_op_fcsel:
                        case midgard_alu_op_icsel:
                                DBG("Missed non-commutative flip (%s)\n", alu_opcode_props[op].name);
                        default:
                                break;
                        }

                        if (alu_opcode_props[op].props & OP_COMMUTES) {
                                /* Flip the SSA numbers */
                                ins->ssa_args.src0 = ins->ssa_args.src1;
                                ins->ssa_args.src1 = SSA_FIXED_REGISTER(REGISTER_CONSTANT);

                                /* And flip the modifiers */

                                unsigned src_temp;

                                src_temp = ins->alu.src2;
                                ins->alu.src2 = ins->alu.src1;
                                ins->alu.src1 = src_temp;
                        }
                }

                if (ins->ssa_args.src1 == SSA_FIXED_REGISTER(REGISTER_CONSTANT)) {
                        /* Extract the source information */

                        midgard_vector_alu_src *src;
                        int q = ins->alu.src2;
                        midgard_vector_alu_src *m = (midgard_vector_alu_src *) &q;
                        src = m;

                        /* Component is from the swizzle, e.g. r26.w -> w component. TODO: What if x is masked out? */
                        int component = src->swizzle & 3;

                        /* Scale constant appropriately, if we can legally */
                        uint16_t scaled_constant = 0;

                        if (midgard_is_integer_op(op) || is_16) {
                                unsigned int *iconstants = (unsigned int *) ins->constants;
                                scaled_constant = (uint16_t) iconstants[component];

                                /* Constant overflow after resize */
                                if (scaled_constant != iconstants[component])
                                        continue;
                        } else {
                                float original = (float) ins->constants[component];
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

                        /* We don't know how to handle these with a constant */

                        if (src->mod || src->half || src->rep_low || src->rep_high) {
                                DBG("Bailing inline constant...\n");
                                continue;
                        }

                        /* Make sure that the constant is not itself a
                         * vector by checking if all accessed values
                         * (by the swizzle) are the same. */

                        uint32_t *cons = (uint32_t *) ins->constants;
                        uint32_t value = cons[component];

                        bool is_vector = false;
                        unsigned mask = effective_writemask(&ins->alu, ins->mask);

                        for (int c = 1; c < 4; ++c) {
                                /* We only care if this component is actually used */
                                if (!(mask & (1 << c)))
                                        continue;

                                uint32_t test = cons[(src->swizzle >> (2 * c)) & 3];

                                if (test != value) {
                                        is_vector = true;
                                        break;
                                }
                        }

                        if (is_vector)
                                continue;

                        /* Get rid of the embedded constant */
                        ins->has_constants = false;
                        ins->ssa_args.src1 = SSA_UNUSED_0;
                        ins->ssa_args.inline_constant = true;
                        ins->inline_constant = scaled_constant;
                }
        }
}

/* Basic dead code elimination on the MIR itself, which cleans up e.g. the
 * texture pipeline */

static bool
midgard_opt_dead_code_eliminate(compiler_context *ctx, midgard_block *block)
{
        bool progress = false;

        mir_foreach_instr_in_block_safe(block, ins) {
                if (ins->type != TAG_ALU_4) continue;
                if (ins->compact_branch) continue;

                if (ins->ssa_args.dest >= SSA_FIXED_MINIMUM) continue;
                if (mir_is_live_after(ctx, block, ins, ins->ssa_args.dest)) continue;

                mir_remove_instruction(ins);
                progress = true;
        }

        return progress;
}

/* Dead code elimination for branches at the end of a block - only one branch
 * per block is legal semantically */

static void
midgard_opt_cull_dead_branch(compiler_context *ctx, midgard_block *block)
{
        bool branched = false;

        mir_foreach_instr_in_block_safe(block, ins) {
                if (!midgard_is_branch_unit(ins->unit)) continue;

                /* We ignore prepacked branches since the fragment epilogue is
                 * just generally special */
                if (ins->prepacked_branch) continue;

                /* Discards are similarly special and may not correspond to the
                 * end of a block */

                if (ins->branch.target_type == TARGET_DISCARD) continue;

                if (branched) {
                        /* We already branched, so this is dead */
                        mir_remove_instruction(ins);
                }

                branched = true;
        }
}

static bool
mir_nontrivial_mod(midgard_vector_alu_src src, bool is_int, unsigned mask)
{
        /* abs or neg */
        if (!is_int && src.mod) return true;

        /* Other int mods don't matter in isolation */
        if (is_int && src.mod == midgard_int_shift) return true;

        /* size-conversion */
        if (src.half) return true;

        /* swizzle */
        for (unsigned c = 0; c < 4; ++c) {
                if (!(mask & (1 << c))) continue;
                if (((src.swizzle >> (2*c)) & 3) != c) return true;
        }

        return false;
}

static bool
mir_nontrivial_source2_mod(midgard_instruction *ins)
{
        bool is_int = midgard_is_integer_op(ins->alu.op);

        midgard_vector_alu_src src2 =
                vector_alu_from_unsigned(ins->alu.src2);

        return mir_nontrivial_mod(src2, is_int, ins->mask);
}

static bool
mir_nontrivial_outmod(midgard_instruction *ins)
{
        bool is_int = midgard_is_integer_op(ins->alu.op);
        unsigned mod = ins->alu.outmod;

        /* Type conversion is a sort of outmod */
        if (ins->alu.dest_override != midgard_dest_override_none)
                return true;

        if (is_int)
                return mod != midgard_outmod_int_wrap;
        else
                return mod != midgard_outmod_none;
}

static bool
midgard_opt_copy_prop(compiler_context *ctx, midgard_block *block)
{
        bool progress = false;

        mir_foreach_instr_in_block_safe(block, ins) {
                if (ins->type != TAG_ALU_4) continue;
                if (!OP_IS_MOVE(ins->alu.op)) continue;

                unsigned from = ins->ssa_args.src1;
                unsigned to = ins->ssa_args.dest;

                /* We only work on pure SSA */

                if (to >= SSA_FIXED_MINIMUM) continue;
                if (from >= SSA_FIXED_MINIMUM) continue;
                if (to >= ctx->func->impl->ssa_alloc) continue;
                if (from >= ctx->func->impl->ssa_alloc) continue;

                /* Constant propagation is not handled here, either */
                if (ins->ssa_args.inline_constant) continue;
                if (ins->has_constants) continue;

                if (mir_nontrivial_source2_mod(ins)) continue;
                if (mir_nontrivial_outmod(ins)) continue;

                /* We're clear -- rewrite */
                mir_rewrite_index_src(ctx, to, from);
                mir_remove_instruction(ins);
                progress |= true;
        }

        return progress;
}

/* fmov.pos is an idiom for fpos. Propoagate the .pos up to the source, so then
 * the move can be propagated away entirely */

static bool
mir_compose_float_outmod(midgard_outmod_float *outmod, midgard_outmod_float comp)
{
        /* Nothing to do */
        if (comp == midgard_outmod_none)
                return true;

        if (*outmod == midgard_outmod_none) {
                *outmod = comp;
                return true;
        }

        /* TODO: Compose rules */
        return false;
}

static bool
midgard_opt_pos_propagate(compiler_context *ctx, midgard_block *block)
{
        bool progress = false;

        mir_foreach_instr_in_block_safe(block, ins) {
                if (ins->type != TAG_ALU_4) continue;
                if (ins->alu.op != midgard_alu_op_fmov) continue;
                if (ins->alu.outmod != midgard_outmod_pos) continue;

                /* TODO: Registers? */
                unsigned src = ins->ssa_args.src1;
                if (src >= ctx->func->impl->ssa_alloc) continue;
                assert(!mir_has_multiple_writes(ctx, src));

                /* There might be a source modifier, too */
                if (mir_nontrivial_source2_mod(ins)) continue;

                /* Backpropagate the modifier */
                mir_foreach_instr_in_block_from_rev(block, v, mir_prev_op(ins)) {
                        if (v->type != TAG_ALU_4) continue;
                        if (v->ssa_args.dest != src) continue;

                        /* Can we even take a float outmod? */
                        if (midgard_is_integer_out_op(v->alu.op)) continue;

                        midgard_outmod_float temp = v->alu.outmod;
                        progress |= mir_compose_float_outmod(&temp, ins->alu.outmod);

                        /* Throw in the towel.. */
                        if (!progress) break;

                        /* Otherwise, transfer the modifier */
                        v->alu.outmod = temp;
                        ins->alu.outmod = midgard_outmod_none;

                        break;
                }
        }

        return progress;
}

static void
emit_fragment_epilogue(compiler_context *ctx)
{
        /* Special case: writing out constants requires us to include the move
         * explicitly now, so shove it into r0 */

        void *constant_value = _mesa_hash_table_u64_search(ctx->ssa_constants, ctx->fragment_output + 1);

        if (constant_value) {
                midgard_instruction ins = v_mov(SSA_FIXED_REGISTER(REGISTER_CONSTANT), blank_alu_src, SSA_FIXED_REGISTER(0));
                attach_constants(ctx, &ins, constant_value, ctx->fragment_output + 1);
                emit_mir_instruction(ctx, ins);
        }

        /* Perform the actual fragment writeout. We have two writeout/branch
         * instructions, forming a loop until writeout is successful as per the
         * docs. TODO: gl_FragDepth */

        EMIT(alu_br_compact_cond, midgard_jmp_writeout_op_writeout, TAG_ALU_4, 0, midgard_condition_always);
        EMIT(alu_br_compact_cond, midgard_jmp_writeout_op_writeout, TAG_ALU_4, -1, midgard_condition_always);
}

static midgard_block *
emit_block(compiler_context *ctx, nir_block *block)
{
        midgard_block *this_block = calloc(sizeof(midgard_block), 1);
        list_addtail(&this_block->link, &ctx->blocks);

        this_block->is_scheduled = false;
        ++ctx->block_count;

        ctx->texture_index[0] = -1;
        ctx->texture_index[1] = -1;

        /* Add us as a successor to the block we are following */
        if (ctx->current_block)
                midgard_block_add_successor(ctx->current_block, this_block);

        /* Set up current block */
        list_inithead(&this_block->instructions);
        ctx->current_block = this_block;

        nir_foreach_instr(instr, block) {
                emit_instr(ctx, instr);
                ++ctx->instruction_count;
        }

        inline_alu_constants(ctx);
        embedded_to_inline_constant(ctx);

        /* Append fragment shader epilogue (value writeout) */
        if (ctx->stage == MESA_SHADER_FRAGMENT) {
                if (block == nir_impl_last_block(ctx->func->impl)) {
                        emit_fragment_epilogue(ctx);
                }
        }

        if (block == nir_start_block(ctx->func->impl))
                ctx->initial_block = this_block;

        if (block == nir_impl_last_block(ctx->func->impl))
                ctx->final_block = this_block;

        /* Allow the next control flow to access us retroactively, for
         * branching etc */
        ctx->current_block = this_block;

        /* Document the fallthrough chain */
        ctx->previous_source_block = this_block;

        return this_block;
}

static midgard_block *emit_cf_list(struct compiler_context *ctx, struct exec_list *list);

static void
emit_if(struct compiler_context *ctx, nir_if *nif)
{
        /* Conditional branches expect the condition in r31.w; emit a move for
         * that in the _previous_ block (which is the current block). */
        emit_condition(ctx, &nif->condition, true, COMPONENT_X);

        /* Speculatively emit the branch, but we can't fill it in until later */
        EMIT(branch, true, true);
        midgard_instruction *then_branch = mir_last_in_block(ctx->current_block);

        /* Emit the two subblocks */
        midgard_block *then_block = emit_cf_list(ctx, &nif->then_list);

        /* Emit a jump from the end of the then block to the end of the else */
        EMIT(branch, false, false);
        midgard_instruction *then_exit = mir_last_in_block(ctx->current_block);

        /* Emit second block, and check if it's empty */

        int else_idx = ctx->block_count;
        int count_in = ctx->instruction_count;
        midgard_block *else_block = emit_cf_list(ctx, &nif->else_list);
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
        emit_cf_list(ctx, &nloop->body);

        /* Branch back to loop back */
        struct midgard_instruction br_back = v_branch(false, false);
        br_back.branch.target_block = start_idx;
        emit_mir_instruction(ctx, br_back);

        /* Mark down that branch in the graph. Note that we're really branching
         * to the block *after* we started in. TODO: Why doesn't the branch
         * itself have an off-by-one then...? */
        midgard_block_add_successor(ctx->current_block, start_block->successors[0]);

        /* Find the index of the block about to follow us (note: we don't add
         * one; blocks are 0-indexed so we get a fencepost problem) */
        int break_block_idx = ctx->block_count;

        /* Fix up the break statements we emitted to point to the right place,
         * now that we can allocate a block number for them */

        list_for_each_entry_from(struct midgard_block, block, start_block, &ctx->blocks, link) {
                mir_foreach_instr_in_block(block, ins) {
                        if (ins->type != TAG_ALU_4) continue;
                        if (!ins->compact_branch) continue;
                        if (ins->prepacked_branch) continue;

                        /* We found a branch -- check the type to see if we need to do anything */
                        if (ins->branch.target_type != TARGET_BREAK) continue;

                        /* It's a break! Check if it's our break */
                        if (ins->branch.target_break != loop_idx) continue;

                        /* Okay, cool, we're breaking out of this loop.
                         * Rewrite from a break to a goto */

                        ins->branch.target_type = TARGET_GOTO;
                        ins->branch.target_block = break_block_idx;
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

static unsigned
midgard_get_first_tag_from_block(compiler_context *ctx, unsigned block_idx)
{
        midgard_block *initial_block = mir_get_block(ctx, block_idx);

        unsigned first_tag = 0;

        do {
                midgard_bundle *initial_bundle = util_dynarray_element(&initial_block->bundles, midgard_bundle, 0);

                if (initial_bundle) {
                        first_tag = initial_bundle->tag;
                        break;
                }

                /* Initial block is empty, try the next block */
                initial_block = list_first_entry(&(initial_block->link), midgard_block, link);
        } while(initial_block != NULL);

        assert(first_tag);
        return first_tag;
}

int
midgard_compile_shader_nir(struct midgard_screen *screen, nir_shader *nir, midgard_program *program, bool is_blend)
{
        struct util_dynarray *compiled = &program->compiled;

        midgard_debug = debug_get_option_midgard_debug();

        compiler_context ictx = {
                .nir = nir,
                .screen = screen,
                .stage = nir->info.stage,

                .is_blend = is_blend,
                .blend_constant_offset = 0,

                .alpha_ref = program->alpha_ref
        };

        compiler_context *ctx = &ictx;

        /* Start off with a safe cutoff, allowing usage of all 16 work
         * registers. Later, we'll promote uniform reads to uniform registers
         * if we determine it is beneficial to do so */
        ctx->uniform_cutoff = 8;

        /* Initialize at a global (not block) level hash tables */

        ctx->ssa_constants = _mesa_hash_table_u64_create(NULL);
        ctx->hash_to_temp = _mesa_hash_table_u64_create(NULL);
        ctx->sysval_to_id = _mesa_hash_table_u64_create(NULL);

        /* Record the varying mapping for the command stream's bookkeeping */

        struct exec_list *varyings =
                        ctx->stage == MESA_SHADER_VERTEX ? &nir->outputs : &nir->inputs;

        unsigned max_varying = 0;
        nir_foreach_variable(var, varyings) {
                unsigned loc = var->data.driver_location;
                unsigned sz = glsl_type_size(var->type, FALSE);

                for (int c = 0; c < sz; ++c) {
                        program->varyings[loc + c] = var->data.location + c;
                        max_varying = MAX2(max_varying, loc + c);
                }
        }

        /* Lower gl_Position pre-optimisation, but after lowering vars to ssa
         * (so we don't accidentally duplicate the epilogue since mesa/st has
         * messed with our I/O quite a bit already) */

        NIR_PASS_V(nir, nir_lower_vars_to_ssa);

        if (ctx->stage == MESA_SHADER_VERTEX) {
                NIR_PASS_V(nir, nir_lower_viewport_transform);
                NIR_PASS_V(nir, nir_clamp_psiz, 1.0, 1024.0);
        }

        NIR_PASS_V(nir, nir_lower_var_copies);
        NIR_PASS_V(nir, nir_lower_vars_to_ssa);
        NIR_PASS_V(nir, nir_split_var_copies);
        NIR_PASS_V(nir, nir_lower_var_copies);
        NIR_PASS_V(nir, nir_lower_global_vars_to_local);
        NIR_PASS_V(nir, nir_lower_var_copies);
        NIR_PASS_V(nir, nir_lower_vars_to_ssa);

        NIR_PASS_V(nir, nir_lower_io, nir_var_all, glsl_type_size, 0);

        /* Optimisation passes */

        optimise_nir(nir);

        if (midgard_debug & MIDGARD_DBG_SHADERS) {
                nir_print_shader(nir, stdout);
        }

        /* Assign sysvals and counts, now that we're sure
         * (post-optimisation) */

        midgard_nir_assign_sysvals(ctx, nir);

        program->uniform_count = nir->num_uniforms;
        program->sysval_count = ctx->sysval_count;
        memcpy(program->sysvals, ctx->sysvals, sizeof(ctx->sysvals[0]) * ctx->sysval_count);

        program->attribute_count = (ctx->stage == MESA_SHADER_VERTEX) ? nir->num_inputs : 0;
        program->varying_count = max_varying + 1; /* Fencepost off-by-one */

        nir_foreach_function(func, nir) {
                if (!func->impl)
                        continue;

                list_inithead(&ctx->blocks);
                ctx->block_count = 0;
                ctx->func = func;

                emit_cf_list(ctx, &func->impl->body);
                emit_block(ctx, func->impl->end_block);

                break; /* TODO: Multi-function shaders */
        }

        util_dynarray_init(compiled, NULL);

        /* MIR-level optimizations */

        bool progress = false;

        do {
                progress = false;

                mir_foreach_block(ctx, block) {
                        progress |= midgard_opt_pos_propagate(ctx, block);
                        progress |= midgard_opt_copy_prop(ctx, block);
                        progress |= midgard_opt_dead_code_eliminate(ctx, block);
                }
        } while (progress);

        /* Nested control-flow can result in dead branches at the end of the
         * block. This messes with our analysis and is just dead code, so cull
         * them */
        mir_foreach_block(ctx, block) {
                midgard_opt_cull_dead_branch(ctx, block);
        }

        /* Schedule! */
        schedule_program(ctx);

        /* Now that all the bundles are scheduled and we can calculate block
         * sizes, emit actual branch instructions rather than placeholders */

        int br_block_idx = 0;

        mir_foreach_block(ctx, block) {
                util_dynarray_foreach(&block->bundles, midgard_bundle, bundle) {
                        for (int c = 0; c < bundle->instruction_count; ++c) {
                                midgard_instruction *ins = bundle->instructions[c];

                                if (!midgard_is_branch_unit(ins->unit)) continue;

                                if (ins->prepacked_branch) continue;

                                /* Parse some basic branch info */
                                bool is_compact = ins->unit == ALU_ENAB_BR_COMPACT;
                                bool is_conditional = ins->branch.conditional;
                                bool is_inverted = ins->branch.invert_conditional;
                                bool is_discard = ins->branch.target_type == TARGET_DISCARD;

                                /* Determine the block we're jumping to */
                                int target_number = ins->branch.target_block;

                                /* Report the destination tag */
                                int dest_tag = is_discard ? 0 : midgard_get_first_tag_from_block(ctx, target_number);

                                /* Count up the number of quadwords we're
                                 * jumping over = number of quadwords until
                                 * (br_block_idx, target_number) */

                                int quadword_offset = 0;

                                if (is_discard) {
                                        /* Jump to the end of the shader. We
                                         * need to include not only the
                                         * following blocks, but also the
                                         * contents of our current block (since
                                         * discard can come in the middle of
                                         * the block) */

                                        midgard_block *blk = mir_get_block(ctx, br_block_idx + 1);

                                        for (midgard_bundle *bun = bundle + 1; bun < (midgard_bundle *)((char*) block->bundles.data + block->bundles.size); ++bun) {
                                                quadword_offset += quadword_size(bun->tag);
                                        }

                                        mir_foreach_block_from(ctx, blk, b) {
                                                quadword_offset += b->quadword_count;
                                        }

                                } else if (target_number > br_block_idx) {
                                        /* Jump forward */

                                        for (int idx = br_block_idx + 1; idx < target_number; ++idx) {
                                                midgard_block *blk = mir_get_block(ctx, idx);
                                                assert(blk);

                                                quadword_offset += blk->quadword_count;
                                        }
                                } else {
                                        /* Jump backwards */

                                        for (int idx = br_block_idx; idx >= target_number; --idx) {
                                                midgard_block *blk = mir_get_block(ctx, idx);
                                                assert(blk);

                                                quadword_offset -= blk->quadword_count;
                                        }
                                }

                                /* Unconditional extended branches (far jumps)
                                 * have issues, so we always use a conditional
                                 * branch, setting the condition to always for
                                 * unconditional. For compact unconditional
                                 * branches, cond isn't used so it doesn't
                                 * matter what we pick. */

                                midgard_condition cond =
                                        !is_conditional ? midgard_condition_always :
                                        is_inverted ? midgard_condition_false :
                                        midgard_condition_true;

                                midgard_jmp_writeout_op op =
                                        is_discard ? midgard_jmp_writeout_op_discard :
                                        (is_compact && !is_conditional) ? midgard_jmp_writeout_op_branch_uncond :
                                        midgard_jmp_writeout_op_branch_cond;

                                if (!is_compact) {
                                        midgard_branch_extended branch =
                                                midgard_create_branch_extended(
                                                        cond, op,
                                                        dest_tag,
                                                        quadword_offset);

                                        memcpy(&ins->branch_extended, &branch, sizeof(branch));
                                } else if (is_conditional || is_discard) {
                                        midgard_branch_cond branch = {
                                                .op = op,
                                                .dest_tag = dest_tag,
                                                .offset = quadword_offset,
                                                .cond = cond
                                        };

                                        assert(branch.offset == quadword_offset);

                                        memcpy(&ins->br_compact, &branch, sizeof(branch));
                                } else {
                                        assert(op == midgard_jmp_writeout_op_branch_uncond);

                                        midgard_branch_uncond branch = {
                                                .op = op,
                                                .dest_tag = dest_tag,
                                                .offset = quadword_offset,
                                                .unknown = 1
                                        };

                                        assert(branch.offset == quadword_offset);

                                        memcpy(&ins->br_compact, &branch, sizeof(branch));
                                }
                        }
                }

                ++br_block_idx;
        }

        /* Emit flat binary from the instruction arrays. Iterate each block in
         * sequence. Save instruction boundaries such that lookahead tags can
         * be assigned easily */

        /* Cache _all_ bundles in source order for lookahead across failed branches */

        int bundle_count = 0;
        mir_foreach_block(ctx, block) {
                bundle_count += block->bundles.size / sizeof(midgard_bundle);
        }
        midgard_bundle **source_order_bundles = malloc(sizeof(midgard_bundle *) * bundle_count);
        int bundle_idx = 0;
        mir_foreach_block(ctx, block) {
                util_dynarray_foreach(&block->bundles, midgard_bundle, bundle) {
                        source_order_bundles[bundle_idx++] = bundle;
                }
        }

        int current_bundle = 0;

        /* Midgard prefetches instruction types, so during emission we
         * need to lookahead. Unless this is the last instruction, in
         * which we return 1. Or if this is the second to last and the
         * last is an ALU, then it's also 1... */

        mir_foreach_block(ctx, block) {
                mir_foreach_bundle_in_block(block, bundle) {
                        int lookahead = 1;

                        if (current_bundle + 1 < bundle_count) {
                                uint8_t next = source_order_bundles[current_bundle + 1]->tag;

                                if (!(current_bundle + 2 < bundle_count) && IS_ALU(next)) {
                                        lookahead = 1;
                                } else {
                                        lookahead = next;
                                }
                        }

                        emit_binary_bundle(ctx, bundle, compiled, lookahead);
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

        program->can_discard = ctx->can_discard;
        program->uniform_cutoff = ctx->uniform_cutoff;

        program->blend_patch_offset = ctx->blend_constant_offset;
        program->tls_size = ctx->tls_size;

        if (midgard_debug & MIDGARD_DBG_SHADERS)
                disassemble_midgard(program->compiled.data, program->compiled.size);

        if (midgard_debug & MIDGARD_DBG_SHADERDB) {
                unsigned nr_bundles = 0, nr_ins = 0, nr_quadwords = 0;

                /* Count instructions and bundles */

                mir_foreach_instr_global(ctx, ins) {
                        nr_ins++;
                }

                mir_foreach_block(ctx, block) {
                        nr_bundles += util_dynarray_num_elements(
                                              &block->bundles, midgard_bundle);

                        nr_quadwords += block->quadword_count;
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
                        "%d:%d spills:fills\n",
                        SHADER_DB_COUNT++,
                        gl_shader_stage_name(ctx->stage),
                        nr_ins, nr_bundles, nr_quadwords,
                        nr_registers, nr_threads,
                        ctx->loop_count,
                        ctx->spills, ctx->fills);
        }


        return 0;
}
