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
#include "midgard_quirks.h"

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

        blk->predecessors = _mesa_set_create(blk,
                        _mesa_hash_pointer,
                        _mesa_key_pointer_equal);

        blk->source_id = ctx->block_source_count++;

        return blk;
}

static void
midgard_block_add_successor(midgard_block *block, midgard_block *successor)
{
        assert(block);
        assert(successor);

        /* Deduplicate */
        for (unsigned i = 0; i < block->nr_successors; ++i) {
                if (block->successors[i] == successor)
                        return;
        }

        block->successors[block->nr_successors++] = successor;
        assert(block->nr_successors <= ARRAY_SIZE(block->successors));

        /* Note the predecessor in the other direction */
        _mesa_set_add(successor->predecessors, block);
}

static void
schedule_barrier(compiler_context *ctx)
{
        midgard_block *temp = ctx->after_block;
        ctx->after_block = create_empty_block(ctx);
        ctx->block_count++;
        list_addtail(&ctx->after_block->link, &ctx->blocks);
        list_inithead(&ctx->after_block->instructions);
        midgard_block_add_successor(ctx->current_block, ctx->after_block);
        ctx->current_block = ctx->after_block;
        ctx->after_block = temp;
}

/* Helpers to generate midgard_instruction's using macro magic, since every
 * driver seems to do it that way */

#define EMIT(op, ...) emit_mir_instruction(ctx, v_##op(__VA_ARGS__));

#define M_LOAD_STORE(name, store) \
	static midgard_instruction m_##name(unsigned ssa, unsigned address) { \
		midgard_instruction i = { \
			.type = TAG_LOAD_STORE_4, \
                        .mask = 0xF, \
                        .dest = ~0, \
                        .src = { ~0, ~0, ~0, ~0 }, \
                        .swizzle = SWIZZLE_IDENTITY_4, \
			.load_store = { \
				.op = midgard_op_##name, \
				.address = address \
			} \
		}; \
                \
                if (store) \
                        i.src[0] = ssa; \
                else \
                        i.dest = ssa; \
		\
		return i; \
	}

#define M_LOAD(name) M_LOAD_STORE(name, false)
#define M_STORE(name) M_LOAD_STORE(name, true)

/* Inputs a NIR ALU source, with modifiers attached if necessary, and outputs
 * the corresponding Midgard source */

static midgard_vector_alu_src
vector_alu_modifiers(nir_alu_src *src, bool is_int, unsigned broadcast_count,
                     bool half, bool sext)
{
        /* Figure out how many components there are so we can adjust.
         * Specifically we want to broadcast the last channel so things like
         * ball2/3 work.
         */

        if (broadcast_count && src) {
                uint8_t last_component = src->swizzle[broadcast_count - 1];

                for (unsigned c = broadcast_count; c < NIR_MAX_VEC_COMPONENTS; ++c) {
                        src->swizzle[c] = last_component;
                }
        }

        midgard_vector_alu_src alu_src = {
                .rep_low = 0,
                .rep_high = 0,
                .half = half
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
                if (src)
                        assert(!(src->abs || src->negate));
        } else {
                if (src)
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
M_LOAD(ld_ubo_int4);
M_LOAD(ld_int4);
M_STORE(st_int4);
M_LOAD(ld_color_buffer_32u);
//M_STORE(st_vary_16);
M_STORE(st_vary_32);
M_LOAD(ld_cubemap_coords);
M_LOAD(ld_compute_id);

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

static midgard_branch_extended
midgard_create_branch_extended( midgard_condition cond,
                                midgard_jmp_writeout_op op,
                                unsigned dest_tag,
                                signed quadword_offset)
{
        /* The condition code is actually a LUT describing a function to
         * combine multiple condition codes. However, we only support a single
         * condition code at the moment, so we just duplicate over a bunch of
         * times. */

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
midgard_sysval_for_ssbo(nir_intrinsic_instr *instr)
{
        /* This is way too meta */
        bool is_store = instr->intrinsic == nir_intrinsic_store_ssbo;
        unsigned idx_idx = is_store ? 1 : 0;

        nir_src index = instr->src[idx_idx];
        assert(nir_src_is_const(index));
        uint32_t uindex = nir_src_as_uint(index);

        return PAN_SYSVAL(SSBO, uindex);
}

static int
midgard_sysval_for_sampler(nir_intrinsic_instr *instr)
{
        /* TODO: indirect samplers !!! */
        nir_src index = instr->src[0];
        assert(nir_src_is_const(index));
        uint32_t uindex = nir_src_as_uint(index);

        return PAN_SYSVAL(SAMPLER, uindex);
}

static int
midgard_nir_sysval_for_intrinsic(nir_intrinsic_instr *instr)
{
        switch (instr->intrinsic) {
        case nir_intrinsic_load_viewport_scale:
                return PAN_SYSVAL_VIEWPORT_SCALE;
        case nir_intrinsic_load_viewport_offset:
                return PAN_SYSVAL_VIEWPORT_OFFSET;
        case nir_intrinsic_load_num_work_groups:
                return PAN_SYSVAL_NUM_WORK_GROUPS;
        case nir_intrinsic_load_ssbo: 
        case nir_intrinsic_store_ssbo: 
                return midgard_sysval_for_ssbo(instr);
        case nir_intrinsic_load_sampler_lod_parameters_pan:
                return midgard_sysval_for_sampler(instr);
        default:
                return ~0;
        }
}

static int sysval_for_instr(compiler_context *ctx, nir_instr *instr,
                            unsigned *dest)
{
        nir_intrinsic_instr *intr;
        nir_dest *dst = NULL;
        nir_tex_instr *tex;
        int sysval = -1;

        bool is_store = false;

        switch (instr->type) {
        case nir_instr_type_intrinsic:
                intr = nir_instr_as_intrinsic(instr);
                sysval = midgard_nir_sysval_for_intrinsic(intr);
                dst = &intr->dest;
                is_store |= intr->intrinsic == nir_intrinsic_store_ssbo;
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

        if (dest && dst && !is_store)
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
optimise_nir(nir_shader *nir, unsigned quirks)
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

                /* TODO: we have native gradient.. */
                .lower_txd = true,
        };

        NIR_PASS(progress, nir, nir_lower_tex, &lower_tex_options);

        /* Must lower fdot2 after tex is lowered */
        NIR_PASS(progress, nir, midgard_nir_lower_fdot2);

        /* T720 is broken. */

        if (quirks & MIDGARD_BROKEN_LOD)
                NIR_PASS_V(nir, midgard_nir_lod_errata);

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
        /* Derivatives end up emitted on the texture pipe, not the ALUs. This
         * is handled elsewhere */

        if (instr->op == nir_op_fddx || instr->op == nir_op_fddy) {
                midgard_emit_derivatives(ctx, instr);
                return;
        }

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

                ALU_CASE(f2i64, f2i_rtz);
                ALU_CASE(f2u64, f2u_rtz);
                ALU_CASE(i2f64, i2f_rtz);
                ALU_CASE(u2f64, u2f_rtz);

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

                /* We'll set invert */
                ALU_CASE(inot, imov);
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
        case nir_op_i2i64:
                /* If we end up upscale, we'll need a sign-extend on the
                 * operand (the second argument) */

                sext_2 = true;
                /* fallthrough */
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

                /* The condition is the first argument; move the other
                 * arguments up one to be a binary instruction for
                 * Midgard with the condition last */

                nir_alu_src temp = instr->src[2];

                instr->src[2] = instr->src[0];
                instr->src[0] = instr->src[1];
                instr->src[1] = temp;

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
        unsigned src1 = nr_inputs >= 2 ? nir_alu_src_index(ctx, &instr->src[1]) : ~0;
        unsigned src2 = nr_inputs == 3 ? nir_alu_src_index(ctx, &instr->src[2]) : ~0;
        assert(nr_inputs <= 3);

        /* Rather than use the instruction generation helpers, we do it
         * ourselves here to avoid the mess */

        midgard_instruction ins = {
                .type = TAG_ALU_4,
                .src = {
                        quirk_flipped_r24 ? ~0 : src0,
                        quirk_flipped_r24 ? src0       : src1,
                        src2,
                        ~0
                },
                .dest = dest,
        };

        nir_alu_src *nirmods[3] = { NULL };

        if (nr_inputs >= 2) {
                nirmods[0] = &instr->src[0];
                nirmods[1] = &instr->src[1];
        } else if (nr_inputs == 1) {
                nirmods[quirk_flipped_r24] = &instr->src[0];
        } else {
                assert(0);
        }

        if (nr_inputs == 3)
                nirmods[2] = &instr->src[2];

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

        for (unsigned m = 0; m < 3; ++m) {
                if (!nirmods[m])
                        continue;

                for (unsigned c = 0; c < NIR_MAX_VEC_COMPONENTS; ++c)
                        ins.swizzle[m][c] = nirmods[m]->swizzle[c];

                /* Replicate. TODO: remove when vec16 lands */
                for (unsigned c = NIR_MAX_VEC_COMPONENTS; c < MIR_VEC_COMPONENTS; ++c)
                        ins.swizzle[m][c] = nirmods[m]->swizzle[NIR_MAX_VEC_COMPONENTS - 1];
        }

        if (nr_inputs == 3) {
                /* Conditions can't have mods */
                assert(!nirmods[2]->abs);
                assert(!nirmods[2]->negate);
        }

        ins.alu = alu;

        /* Late fixup for emulated instructions */

        if (instr->op == nir_op_b2f32 || instr->op == nir_op_b2i32) {
                /* Presently, our second argument is an inline #0 constant.
                 * Switch over to an embedded 1.0 constant (that can't fit
                 * inline, since we're 32-bit, not 16-bit like the inline
                 * constants) */

                ins.has_inline_constant = false;
                ins.src[1] = SSA_FIXED_REGISTER(REGISTER_CONSTANT);
                ins.has_constants = true;

                if (instr->op == nir_op_b2f32)
                        ins.constants.f32[0] = 1.0f;
                else
                        ins.constants.i32[0] = 1;

                for (unsigned c = 0; c < 16; ++c)
                        ins.swizzle[1][c] = 0;
        } else if (nr_inputs == 1 && !quirk_flipped_r24) {
                /* Lots of instructions need a 0 plonked in */
                ins.has_inline_constant = false;
                ins.src[1] = SSA_FIXED_REGISTER(REGISTER_CONSTANT);
                ins.has_constants = true;
                ins.constants.u32[0] = 0;

                for (unsigned c = 0; c < 16; ++c)
                        ins.swizzle[1][c] = 0;
        } else if (instr->op == nir_op_inot) {
                ins.invert = true;
        }

        if ((opcode_props & UNITS_ALL) == UNIT_VLUT) {
                /* To avoid duplicating the lookup tables (probably), true LUT
                 * instructions can only operate as if they were scalars. Lower
                 * them here by changing the component. */

                unsigned orig_mask = ins.mask;

                for (int i = 0; i < nr_components; ++i) {
                        /* Mask the associated component, dropping the
                         * instruction if needed */

                        ins.mask = 1 << i;
                        ins.mask &= orig_mask;

                        if (!ins.mask)
                                continue;

                        for (unsigned j = 0; j < MIR_VEC_COMPONENTS; ++j)
                                ins.swizzle[0][j] = nirmods[0]->swizzle[i]; /* Pull from the correct component */

                        emit_mir_instruction(ctx, ins);
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
        unsigned bytemask = mir_to_bytemask(mir_mode_for_destsize(dsize), nir_mask);
        mir_set_bytemask(ins, bytemask);

        if (dsize == 64)
                ins->load_64 = true;
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
                ins.load_store.arg_2 = (indirect_shift << 5);
        } else {
                ins.load_store.arg_2 = 0x1E;
        }

        ins.load_store.arg_1 = index;

        return emit_mir_instruction(ctx, ins);
}

/* SSBO reads are like UBO reads if you squint */

static void
emit_ssbo_access(
        compiler_context *ctx,
        nir_instr *instr,
        bool is_read,
        unsigned srcdest,
        unsigned offset,
        nir_src *indirect_offset,
        unsigned index)
{
        /* TODO: types */

        midgard_instruction ins; 

        if (is_read)
                ins = m_ld_int4(srcdest, offset);
        else
                ins = m_st_int4(srcdest, offset);

        /* SSBO reads use a generic memory read interface, so we need the
         * address of the SSBO as the first argument. This is a sysval. */

        unsigned addr = make_compiler_temp(ctx);
        emit_sysval_read(ctx, instr, addr, 2);

        /* The source array:
         *
         *  src[0] = store ? value : unused
         *  src[1] = arg_1
         *  src[2] = arg_2
         *
         * We would like arg_1 = the address and
         * arg_2 = the offset.
         */

        ins.src[1] = addr;

        /* TODO: What is this? It looks superficially like a shift << 5, but
         * arg_1 doesn't take a shift Should it be E0 or A0? We also need the
         * indirect offset. */

        if (indirect_offset) {
                ins.load_store.arg_1 |= 0xE0;
                ins.src[2] = nir_src_index(ctx, indirect_offset);
        } else {
                ins.load_store.arg_2 = 0x7E;
        }

        /* TODO: Bounds check */

        /* Finally, we emit the direct offset */

        ins.load_store.varying_parameters = (offset & 0x1FF) << 1;
        ins.load_store.address = (offset >> 9);
        mir_set_intr_mask(instr, &ins, is_read);

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

        if (indirect_offset)
                ins.src[2] = nir_src_index(ctx, indirect_offset);
        else
                ins.load_store.arg_2 = 0x1E;

        ins.load_store.arg_1 = 0x9E;

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
}

void
emit_sysval_read(compiler_context *ctx, nir_instr *instr, signed dest_override,
                unsigned nr_components)
{
        unsigned dest = 0;

        /* Figure out which uniform this is */
        int sysval = sysval_for_instr(ctx, instr, &dest);
        void *val = _mesa_hash_table_u64_search(ctx->sysval_to_id, sysval);

        if (dest_override >= 0)
                dest = dest_override;

        /* Sysvals are prefix uniforms */
        unsigned uniform = ((uintptr_t) val) - 1;

        /* Emit the read itself -- this is never indirect */
        midgard_instruction *ins =
                emit_ubo_read(ctx, instr, dest, uniform * 16, NULL, 0, 0);

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
emit_fragment_store(compiler_context *ctx, unsigned src, unsigned rt)
{
        emit_explicit_constant(ctx, src, src);

        struct midgard_instruction ins =
                v_branch(false, false);

        ins.writeout = true;

        /* Add dependencies */
        ins.src[0] = src;
        ins.constants.u32[0] = rt * 0x100;

        /* Emit the branch */
        midgard_instruction *br = emit_mir_instruction(ctx, ins);
        schedule_barrier(ctx);

        assert(rt < ARRAY_SIZE(ctx->writeout_branch));
        assert(!ctx->writeout_branch[rt]);
        ctx->writeout_branch[rt] = br;

        /* Push our current location = current block count - 1 = where we'll
         * jump to. Maybe a bit too clever for my own good */

        br->branch.target_block = ctx->block_count - 1;
}

static void
emit_compute_builtin(compiler_context *ctx, nir_intrinsic_instr *instr)
{
        unsigned reg = nir_dest_index(ctx, &instr->dest);
        midgard_instruction ins = m_ld_compute_id(reg, 0);
        ins.mask = mask_of(3);
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
        unsigned reg = nir_dest_index(ctx, &instr->dest);
        emit_attr_read(ctx, reg, vertex_builtin_arg(instr->intrinsic), 1, nir_type_int);
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

                if (conditional)
                        discard.src[0] = nir_src_index(ctx, &instr->src[0]);

                emit_mir_instruction(ctx, discard);
                schedule_barrier(ctx);

                break;
        }

        case nir_intrinsic_load_uniform:
        case nir_intrinsic_load_ubo:
        case nir_intrinsic_load_ssbo:
        case nir_intrinsic_load_input:
        case nir_intrinsic_load_interpolated_input: {
                bool is_uniform = instr->intrinsic == nir_intrinsic_load_uniform;
                bool is_ubo = instr->intrinsic == nir_intrinsic_load_ubo;
                bool is_ssbo = instr->intrinsic == nir_intrinsic_load_ssbo;
                bool is_flat = instr->intrinsic == nir_intrinsic_load_input;
                bool is_interp = instr->intrinsic == nir_intrinsic_load_interpolated_input;

                /* Get the base type of the intrinsic */
                /* TODO: Infer type? Does it matter? */
                nir_alu_type t =
                        (is_ubo || is_ssbo) ? nir_type_uint :
                        (is_interp) ? nir_type_float :
                        nir_intrinsic_type(instr);

                t = nir_alu_type_get_base_type(t);

                if (!(is_ubo || is_ssbo)) {
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
                reg = nir_dest_index(ctx, &instr->dest);

                if (is_uniform && !ctx->is_blend) {
                        emit_ubo_read(ctx, &instr->instr, reg, (ctx->sysval_count + offset) * 16, indirect_offset, 4, 0);
                } else if (is_ubo) {
                        nir_src index = instr->src[0];

                        /* TODO: Is indirect block number possible? */
                        assert(nir_src_is_const(index));

                        uint32_t uindex = nir_src_as_uint(index) + 1;
                        emit_ubo_read(ctx, &instr->instr, reg, offset, indirect_offset, 0, uindex);
                } else if (is_ssbo) {
                        nir_src index = instr->src[0];
                        assert(nir_src_is_const(index));
                        uint32_t uindex = nir_src_as_uint(index);

                        emit_ssbo_access(ctx, &instr->instr, true, reg, offset, indirect_offset, uindex);
                } else if (ctx->stage == MESA_SHADER_FRAGMENT && !ctx->is_blend) {
                        emit_varying_read(ctx, reg, offset, nr_comp, component, indirect_offset, t, is_flat);
                } else if (ctx->is_blend) {
                        /* For blend shaders, load the input color, which is
                         * preloaded to r0 */

                        midgard_instruction move = v_mov(SSA_FIXED_REGISTER(0), reg);
                        emit_mir_instruction(ctx, move);
                        schedule_barrier(ctx);
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
                break;

        /* Reads 128-bit value raw off the tilebuffer during blending, tasty */

        case nir_intrinsic_load_raw_output_pan:
        case nir_intrinsic_load_output_u8_as_fp16_pan:
                reg = nir_dest_index(ctx, &instr->dest);
                assert(ctx->is_blend);

                /* T720 and below use different blend opcodes with slightly
                 * different semantics than T760 and up */

                midgard_instruction ld = m_ld_color_buffer_32u(reg, 0);
                bool old_blend = ctx->quirks & MIDGARD_OLD_BLEND;

                if (instr->intrinsic == nir_intrinsic_load_output_u8_as_fp16_pan) {
                        ld.load_store.op = old_blend ?
                                midgard_op_ld_color_buffer_u8_as_fp16_old :
                                midgard_op_ld_color_buffer_u8_as_fp16;

                        if (old_blend) {
                                ld.load_store.address = 1;
                                ld.load_store.arg_2 = 0x1E;
                        }

                        for (unsigned c = 2; c < 16; ++c)
                                ld.swizzle[0][c] = 0;
                }

                emit_mir_instruction(ctx, ld);
                break;

        case nir_intrinsic_load_blend_const_color_rgba: {
                assert(ctx->is_blend);
                reg = nir_dest_index(ctx, &instr->dest);

                /* Blend constants are embedded directly in the shader and
                 * patched in, so we use some magic routing */

                midgard_instruction ins = v_mov(SSA_FIXED_REGISTER(REGISTER_CONSTANT), reg);
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
                        emit_fragment_store(ctx, reg, offset);
                } else if (ctx->stage == MESA_SHADER_VERTEX) {
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

                        switch (nir_alu_type_get_base_type(nir_intrinsic_type(instr))) {
                        case nir_type_uint:
                        case nir_type_bool:
                                st.load_store.op = midgard_op_st_vary_32u;
                                break;
                        case nir_type_int:
                                st.load_store.op = midgard_op_st_vary_32i;
                                break;
                        case nir_type_float:
                                st.load_store.op = midgard_op_st_vary_32;
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

                if (ctx->quirks & MIDGARD_OLD_BLEND) {
                        /* Suppose reg = qr0.xyzw. That means 4 8-bit ---> 1 32-bit. So
                         * reg = r0.x. We want to splatter. So we can do a 32-bit move
                         * of:
                         *
                         * imov r0.xyzw, r0.xxxx
                         */

                        unsigned expanded = make_compiler_temp(ctx);

                        midgard_instruction splatter = v_mov(reg, expanded);

                        for (unsigned c = 0; c < 16; ++c)
                                splatter.swizzle[1][c] = 0;

                        emit_mir_instruction(ctx, splatter);
                        emit_fragment_store(ctx, expanded, ctx->blend_rt);
                } else
                        emit_fragment_store(ctx, reg, ctx->blend_rt);

                break;

        case nir_intrinsic_store_ssbo:
                assert(nir_src_is_const(instr->src[1]));

                bool direct_offset = nir_src_is_const(instr->src[2]);
                offset = direct_offset ? nir_src_as_uint(instr->src[2]) : 0;
                nir_src *indirect_offset = direct_offset ? NULL : &instr->src[2];
                reg = nir_src_index(ctx, &instr->src[0]);

                uint32_t uindex = nir_src_as_uint(instr->src[1]);

                emit_explicit_constant(ctx, reg, reg);
                emit_ssbo_access(ctx, &instr->instr, false, reg, offset, indirect_offset, uindex);
                break;

        case nir_intrinsic_load_viewport_scale:
        case nir_intrinsic_load_viewport_offset:
        case nir_intrinsic_load_num_work_groups:
        case nir_intrinsic_load_sampler_lod_parameters_pan:
                emit_sysval_read(ctx, &instr->instr, ~0, 3);
                break;

        case nir_intrinsic_load_work_group_id:
        case nir_intrinsic_load_local_invocation_id:
                emit_compute_builtin(ctx, instr);
                break;

        case nir_intrinsic_load_vertex_id:
        case nir_intrinsic_load_instance_id:
                emit_vertex_builtin(ctx, instr);
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
        case GLSL_SAMPLER_DIM_RECT:
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

        int texture_index = instr->texture_index;
        int sampler_index = texture_index;

        /* No helper to build texture words -- we do it all here */
        midgard_instruction ins = {
                .type = TAG_TEXTURE_4,
                .mask = 0xF,
                .dest = nir_dest_index(ctx, &instr->dest),
                .src = { ~0, ~0, ~0, ~0 },
                .swizzle = SWIZZLE_IDENTITY_4,
                .texture = {
                        .op = midgard_texop,
                        .format = midgard_tex_format(instr->sampler_dim),
                        .texture_handle = texture_index,
                        .sampler_handle = sampler_index,

                        /* TODO: half */
                        .in_reg_full = 1,
                        .out_full = 1,

                        .sampler_type = midgard_sampler_type(instr->dest_type),
                        .shadow = instr->is_shadow,
                }
        };

        /* We may need a temporary for the coordinate */

        bool needs_temp_coord =
                (midgard_texop == TEXTURE_OP_TEXEL_FETCH) ||
                (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE) ||
                (instr->is_shadow);

        unsigned coords = needs_temp_coord ? make_compiler_temp_reg(ctx) : 0;

        for (unsigned i = 0; i < instr->num_srcs; ++i) {
                int index = nir_src_index(ctx, &instr->src[i].src);
                unsigned nr_components = nir_src_num_components(instr->src[i].src);

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

                        for (unsigned c = 0; c < MIR_VEC_COMPONENTS; ++c)
                                ins.swizzle[2][c] = COMPONENT_X;

                        emit_explicit_constant(ctx, index, index);

                        break;
                };

                case nir_tex_src_offset: {
                        ins.texture.offset_register = true;
                        ins.src[3] = index;

                        for (unsigned c = 0; c < MIR_VEC_COMPONENTS; ++c)
                                ins.swizzle[3][c] = (c > COMPONENT_Z) ? 0 : c;

                        emit_explicit_constant(ctx, index, index);
                        break;
                };

                case nir_tex_src_comparator: {
                        unsigned comp = COMPONENT_Z;

                        /* mov coord_temp.foo, coords */
                        midgard_instruction mov = v_mov(index, coords);
                        mov.mask = 1 << comp;

                        for (unsigned i = 0; i < MIR_VEC_COMPONENTS; ++i)
                                mov.swizzle[1][i] = COMPONENT_X;

                        emit_mir_instruction(ctx, mov);
                        break;
                }

                default:
                        unreachable("Unknown texture source type\n");
                }
        }

        emit_mir_instruction(ctx, ins);

        /* Used for .cont and .last hinting */
        ctx->texture_op_count++;
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
                emit_texop_native(ctx, instr, TEXTURE_OP_LOD);
                break;
        case nir_texop_txf:
                emit_texop_native(ctx, instr, TEXTURE_OP_TEXEL_FETCH);
                break;
        case nir_texop_txs:
                emit_sysval_read(ctx, &instr->instr, ~0, 4);
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
                         * other. Note, if both arguments are constants, then
                         * necessarily neither argument depends on the value of
                         * any particular register. As the destination register
                         * will be wiped, that means we can spill the constant
                         * to the destination register.
                         */

                        void *entry = _mesa_hash_table_u64_search(ctx->ssa_constants, alu->src[1] + 1);
                        unsigned scratch = alu->dest;

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

/* Being a little silly with the names, but returns the op that is the bitwise
 * inverse of the op with the argument switched. I.e. (f and g are
 * contrapositives):
 *
 * f(a, b) = ~g(b, a)
 *
 * Corollary: if g is the contrapositve of f, f is the contrapositive of g:
 *
 *      f(a, b) = ~g(b, a)
 *      ~f(a, b) = g(b, a)
 *      ~f(a, b) = ~h(a, b) where h is the contrapositive of g
 *      f(a, b) = h(a, b)
 *
 * Thus we define this function in pairs.
 */

static inline midgard_alu_op
mir_contrapositive(midgard_alu_op op)
{
        switch (op) {
        case midgard_alu_op_flt:
                return midgard_alu_op_fle;
        case midgard_alu_op_fle:
                return midgard_alu_op_flt;

        case midgard_alu_op_ilt:
                return midgard_alu_op_ile;
        case midgard_alu_op_ile:
                return midgard_alu_op_ilt;

        default:
                unreachable("No known contrapositive");
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

                if (ins->src[0] == SSA_FIXED_REGISTER(REGISTER_CONSTANT)) {
                        bool flip = alu_opcode_props[op].props & OP_COMMUTES;

                        switch (op) {
                        /* Conditionals can be inverted */
                        case midgard_alu_op_flt:
                        case midgard_alu_op_ilt:
                        case midgard_alu_op_fle:
                        case midgard_alu_op_ile:
                                ins->alu.op = mir_contrapositive(ins->alu.op);
                                ins->invert = true;
                                flip = true;
                                break;

                        case midgard_alu_op_fcsel:
                        case midgard_alu_op_icsel:
                                DBG("Missed non-commutative flip (%s)\n", alu_opcode_props[op].name);
                        default:
                                break;
                        }

                        if (flip)
                                mir_flip(ins);
                }

                if (ins->src[1] == SSA_FIXED_REGISTER(REGISTER_CONSTANT)) {
                        /* Extract the source information */

                        midgard_vector_alu_src *src;
                        int q = ins->alu.src2;
                        midgard_vector_alu_src *m = (midgard_vector_alu_src *) &q;
                        src = m;

                        /* Component is from the swizzle. Take a nonzero component */
                        assert(ins->mask);
                        unsigned first_comp = ffs(ins->mask) - 1;
                        unsigned component = ins->swizzle[1][first_comp];

                        /* Scale constant appropriately, if we can legally */
                        uint16_t scaled_constant = 0;

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

                        /* We don't know how to handle these with a constant */

                        if (mir_nontrivial_source2_mod_simple(ins) || src->rep_low || src->rep_high) {
                                DBG("Bailing inline constant...\n");
                                continue;
                        }

                        /* Make sure that the constant is not itself a vector
                         * by checking if all accessed values are the same. */

                        const midgard_constants *cons = &ins->constants;
                        uint32_t value = is_16 ? cons->u16[component] : cons->u32[component];

                        bool is_vector = false;
                        unsigned mask = effective_writemask(&ins->alu, ins->mask);

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
midgard_opt_cull_dead_branch(compiler_context *ctx, midgard_block *block)
{
        bool branched = false;

        mir_foreach_instr_in_block_safe(block, ins) {
                if (!midgard_is_branch_unit(ins->unit)) continue;

                if (branched)
                        mir_remove_instruction(ins);

                branched = true;
        }
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
                unsigned src = ins->src[1];
                if (src & IS_REG) continue;

                /* There might be a source modifier, too */
                if (mir_nontrivial_source2_mod(ins)) continue;

                /* Backpropagate the modifier */
                mir_foreach_instr_in_block_from_rev(block, v, mir_prev_op(ins)) {
                        if (v->type != TAG_ALU_4) continue;
                        if (v->dest != src) continue;

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

static unsigned
emit_fragment_epilogue(compiler_context *ctx, unsigned rt)
{
        /* Loop to ourselves */

        struct midgard_instruction ins = v_branch(false, false);
        ins.writeout = true;
        ins.branch.target_block = ctx->block_count - 1;
        ins.constants.u32[0] = rt * 0x100;
        emit_mir_instruction(ctx, ins);

        ctx->current_block->epilogue = true;
        schedule_barrier(ctx);
        return ins.branch.target_block;
}

static midgard_block *
emit_block(compiler_context *ctx, nir_block *block)
{
        midgard_block *this_block = ctx->after_block;
        ctx->after_block = NULL;

        if (!this_block)
                this_block = create_empty_block(ctx);

        list_addtail(&this_block->link, &ctx->blocks);

        this_block->is_scheduled = false;
        ++ctx->block_count;

        /* Set up current block */
        list_inithead(&this_block->instructions);
        ctx->current_block = this_block;

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
        EMIT(branch, true, true);
        midgard_instruction *then_branch = mir_last_in_block(ctx->current_block);
        then_branch->src[0] = nir_src_index(ctx, &nif->condition);

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

        midgard_block_add_successor(before_block, then_block);
        midgard_block_add_successor(before_block, else_block);

        midgard_block_add_successor(end_then_block, ctx->after_block);
        midgard_block_add_successor(end_else_block, ctx->after_block);
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
        midgard_block_add_successor(start_block, loop_block);
        midgard_block_add_successor(ctx->current_block, loop_block);

        /* Find the index of the block about to follow us (note: we don't add
         * one; blocks are 0-indexed so we get a fencepost problem) */
        int break_block_idx = ctx->block_count;

        /* Fix up the break statements we emitted to point to the right place,
         * now that we can allocate a block number for them */
        ctx->after_block = create_empty_block(ctx);

        list_for_each_entry_from(struct midgard_block, block, start_block, &ctx->blocks, link) {
                mir_foreach_instr_in_block(block, ins) {
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

                        midgard_block_add_successor(block, ctx->after_block);
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

        mir_foreach_block_from(ctx, initial_block, v) {
                if (v->quadword_count) {
                        midgard_bundle *initial_bundle =
                                util_dynarray_element(&v->bundles, midgard_bundle, 0);

                        first_tag = initial_bundle->tag;
                        break;
                }
        }

        return first_tag;
}

static unsigned
pan_format_from_nir_base(nir_alu_type base)
{
        switch (base) {
        case nir_type_int:
                return MALI_FORMAT_SINT;
        case nir_type_uint:
        case nir_type_bool:
                return MALI_FORMAT_UINT;
        case nir_type_float:
                return MALI_CHANNEL_FLOAT;
        default:
                unreachable("Invalid base");
        }
}

static unsigned
pan_format_from_nir_size(nir_alu_type base, unsigned size)
{
        if (base == nir_type_float) {
                switch (size) {
                case 16: return MALI_FORMAT_SINT;
                case 32: return MALI_FORMAT_UNORM;
                default:
                        unreachable("Invalid float size for format");
                }
        } else {
                switch (size) {
                case 1:
                case 8:  return MALI_CHANNEL_8;
                case 16: return MALI_CHANNEL_16;
                case 32: return MALI_CHANNEL_32;
                default:
                         unreachable("Invalid int size for format");
                }
        }
}

static enum mali_format
pan_format_from_glsl(const struct glsl_type *type)
{
        enum glsl_base_type glsl_base = glsl_get_base_type(glsl_without_array(type));
        nir_alu_type t = nir_get_nir_type_for_glsl_base_type(glsl_base);

        unsigned base = nir_alu_type_get_base_type(t);
        unsigned size = nir_alu_type_get_type_size(t);

        return pan_format_from_nir_base(base) |
                pan_format_from_nir_size(base, size) |
                MALI_NR_CHANNELS(4);
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
                midgard_block_add_successor(mir_get_block(ctx, popped - 1), ctx->current_block);
                br->branch.target_block = emit_fragment_epilogue(ctx, rt);

                /* If we have more RTs, we'll need to restore back after our
                 * loop terminates */

                if ((rt + 1) < ARRAY_SIZE(ctx->writeout_branch) && ctx->writeout_branch[rt + 1]) {
                        midgard_instruction uncond = v_branch(false, false);
                        uncond.branch.target_block = popped;
                        emit_mir_instruction(ctx, uncond);
                        midgard_block_add_successor(ctx->current_block, mir_get_block(ctx, popped));
                        schedule_barrier(ctx);
                } else {
                        /* We're last, so we can terminate here */
                        br->last_writeout = true;
                }
        }
}

int
midgard_compile_shader_nir(nir_shader *nir, midgard_program *program, bool is_blend, unsigned blend_rt, unsigned gpu_id, bool shaderdb)
{
        struct util_dynarray *compiled = &program->compiled;

        midgard_debug = debug_get_option_midgard_debug();

        /* TODO: Bound against what? */
        compiler_context *ctx = rzalloc(NULL, compiler_context);

        ctx->nir = nir;
        ctx->stage = nir->info.stage;
        ctx->is_blend = is_blend;
        ctx->alpha_ref = program->alpha_ref;
        ctx->blend_rt = blend_rt;
        ctx->quirks = midgard_get_quirks(gpu_id);

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
                        program->varying_type[loc + c] = pan_format_from_glsl(var->type);
                        max_varying = MAX2(max_varying, loc + c);
                }
        }

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

        NIR_PASS_V(nir, nir_lower_io, nir_var_all, glsl_type_size, 0);

        /* Optimisation passes */

        optimise_nir(nir, ctx->quirks);

        if (midgard_debug & MIDGARD_DBG_SHADERS) {
                nir_print_shader(nir, stdout);
        }

        /* Assign sysvals and counts, now that we're sure
         * (post-optimisation) */

        midgard_nir_assign_sysvals(ctx, nir);

        program->uniform_count = nir->num_uniforms;
        program->sysval_count = ctx->sysval_count;
        memcpy(program->sysvals, ctx->sysvals, sizeof(ctx->sysvals[0]) * ctx->sysval_count);

        nir_foreach_function(func, nir) {
                if (!func->impl)
                        continue;

                list_inithead(&ctx->blocks);
                ctx->block_count = 0;
                ctx->func = func;

                emit_cf_list(ctx, &func->impl->body);
                break; /* TODO: Multi-function shaders */
        }

        util_dynarray_init(compiled, NULL);

        /* Per-block lowering before opts */

        mir_foreach_block(ctx, block) {
                inline_alu_constants(ctx, block);
                midgard_opt_promote_fmov(ctx, block);
                embedded_to_inline_constant(ctx, block);
        }
        /* MIR-level optimizations */

        bool progress = false;

        do {
                progress = false;

                mir_foreach_block(ctx, block) {
                        progress |= midgard_opt_pos_propagate(ctx, block);
                        progress |= midgard_opt_copy_prop(ctx, block);
                        progress |= midgard_opt_dead_code_eliminate(ctx, block);
                        progress |= midgard_opt_combine_projection(ctx, block);
                        progress |= midgard_opt_varying_projection(ctx, block);
                        progress |= midgard_opt_not_propagate(ctx, block);
                        progress |= midgard_opt_fuse_src_invert(ctx, block);
                        progress |= midgard_opt_fuse_dest_invert(ctx, block);
                        progress |= midgard_opt_csel_invert(ctx, block);
                        progress |= midgard_opt_drop_cmp_invert(ctx, block);
                        progress |= midgard_opt_invert_branch(ctx, block);
                }
        } while (progress);

        mir_foreach_block(ctx, block) {
                midgard_lower_invert(ctx, block);
                midgard_lower_derivatives(ctx, block);
        }

        /* Nested control-flow can result in dead branches at the end of the
         * block. This messes with our analysis and is just dead code, so cull
         * them */
        mir_foreach_block(ctx, block) {
                midgard_opt_cull_dead_branch(ctx, block);
        }

        /* Ensure we were lowered */
        mir_foreach_instr_global(ctx, ins) {
                assert(!ins->invert);
        }

        if (ctx->stage == MESA_SHADER_FRAGMENT)
                mir_add_writeout_loops(ctx);

        /* Schedule! */
        midgard_schedule_program(ctx);
        mir_ra(ctx);

        /* Now that all the bundles are scheduled and we can calculate block
         * sizes, emit actual branch instructions rather than placeholders */

        int br_block_idx = 0;

        mir_foreach_block(ctx, block) {
                util_dynarray_foreach(&block->bundles, midgard_bundle, bundle) {
                        for (int c = 0; c < bundle->instruction_count; ++c) {
                                midgard_instruction *ins = bundle->instructions[c];

                                if (!midgard_is_branch_unit(ins->unit)) continue;

                                /* Parse some basic branch info */
                                bool is_compact = ins->unit == ALU_ENAB_BR_COMPACT;
                                bool is_conditional = ins->branch.conditional;
                                bool is_inverted = ins->branch.invert_conditional;
                                bool is_discard = ins->branch.target_type == TARGET_DISCARD;
                                bool is_writeout = ins->writeout;

                                /* Determine the block we're jumping to */
                                int target_number = ins->branch.target_block;

                                /* Report the destination tag */
                                int dest_tag = is_discard ? 0 : midgard_get_first_tag_from_block(ctx, target_number);

                                /* Count up the number of quadwords we're
                                 * jumping over = number of quadwords until
                                 * (br_block_idx, target_number) */

                                int quadword_offset = 0;

                                if (is_discard) {
                                        /* Ignored */
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
                                        is_writeout ? midgard_jmp_writeout_op_writeout :
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
         * which we return 1. */

        mir_foreach_block(ctx, block) {
                mir_foreach_bundle_in_block(block, bundle) {
                        int lookahead = 1;

                        if (!bundle->last_writeout && (current_bundle + 1 < bundle_count))
                                lookahead = source_order_bundles[current_bundle + 1]->tag;

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
        program->uniform_cutoff = ctx->uniform_cutoff;

        program->blend_patch_offset = ctx->blend_constant_offset;
        program->tls_size = ctx->tls_size;

        if (midgard_debug & MIDGARD_DBG_SHADERS)
                disassemble_midgard(stdout, program->compiled.data, program->compiled.size, gpu_id, ctx->stage);

        if (midgard_debug & MIDGARD_DBG_SHADERDB || shaderdb) {
                unsigned nr_bundles = 0, nr_ins = 0;

                /* Count instructions and bundles */

                mir_foreach_block(ctx, block) {
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
                        gl_shader_stage_name(ctx->stage),
                        nr_ins, nr_bundles, ctx->quadword_count,
                        nr_registers, nr_threads,
                        ctx->loop_count,
                        ctx->spills, ctx->fills);
        }

        ralloc_free(ctx);

        return 0;
}
