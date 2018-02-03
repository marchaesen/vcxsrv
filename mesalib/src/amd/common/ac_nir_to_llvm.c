/*
 * Copyright © 2016 Bas Nieuwenhuizen
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "ac_nir_to_llvm.h"
#include "ac_llvm_build.h"
#include "ac_llvm_util.h"
#include "ac_binary.h"
#include "sid.h"
#include "nir/nir.h"
#include "../vulkan/radv_descriptor_set.h"
#include "util/bitscan.h"
#include <llvm-c/Transforms/Scalar.h>
#include "ac_shader_abi.h"
#include "ac_shader_info.h"
#include "ac_shader_util.h"
#include "ac_exp_param.h"

enum radeon_llvm_calling_convention {
	RADEON_LLVM_AMDGPU_VS = 87,
	RADEON_LLVM_AMDGPU_GS = 88,
	RADEON_LLVM_AMDGPU_PS = 89,
	RADEON_LLVM_AMDGPU_CS = 90,
	RADEON_LLVM_AMDGPU_HS = 93,
};

#define RADEON_LLVM_MAX_INPUTS (VARYING_SLOT_VAR31 + 1)
#define RADEON_LLVM_MAX_OUTPUTS (VARYING_SLOT_VAR31 + 1)

struct nir_to_llvm_context;

struct ac_nir_context {
	struct ac_llvm_context ac;
	struct ac_shader_abi *abi;

	gl_shader_stage stage;

	struct hash_table *defs;
	struct hash_table *phis;
	struct hash_table *vars;

	LLVMValueRef main_function;
	LLVMBasicBlockRef continue_block;
	LLVMBasicBlockRef break_block;

	LLVMValueRef outputs[RADEON_LLVM_MAX_OUTPUTS * 4];

	int num_locals;
	LLVMValueRef *locals;

	struct nir_to_llvm_context *nctx; /* TODO get rid of this */
};

struct nir_to_llvm_context {
	struct ac_llvm_context ac;
	const struct ac_nir_compiler_options *options;
	struct ac_shader_variant_info *shader_info;
	struct ac_shader_abi abi;
	struct ac_nir_context *nir;

	unsigned max_workgroup_size;
	LLVMContextRef context;
	LLVMModuleRef module;
	LLVMBuilderRef builder;
	LLVMValueRef main_function;

	struct hash_table *defs;
	struct hash_table *phis;

	LLVMValueRef descriptor_sets[AC_UD_MAX_SETS];
	LLVMValueRef ring_offsets;
	LLVMValueRef push_constants;
	LLVMValueRef view_index;
	LLVMValueRef num_work_groups;
	LLVMValueRef workgroup_ids[3];
	LLVMValueRef local_invocation_ids;
	LLVMValueRef tg_size;

	LLVMValueRef vertex_buffers;
	LLVMValueRef rel_auto_id;
	LLVMValueRef vs_prim_id;
	LLVMValueRef ls_out_layout;
	LLVMValueRef es2gs_offset;

	LLVMValueRef tcs_offchip_layout;
	LLVMValueRef tcs_out_offsets;
	LLVMValueRef tcs_out_layout;
	LLVMValueRef tcs_in_layout;
	LLVMValueRef oc_lds;
	LLVMValueRef merged_wave_info;
	LLVMValueRef tess_factor_offset;
	LLVMValueRef tes_rel_patch_id;
	LLVMValueRef tes_u;
	LLVMValueRef tes_v;

	LLVMValueRef gsvs_ring_stride;
	LLVMValueRef gsvs_num_entries;
	LLVMValueRef gs2vs_offset;
	LLVMValueRef gs_wave_id;
	LLVMValueRef gs_vtx_offset[6];

	LLVMValueRef esgs_ring;
	LLVMValueRef gsvs_ring;
	LLVMValueRef hs_ring_tess_offchip;
	LLVMValueRef hs_ring_tess_factor;

	LLVMValueRef sample_pos_offset;
	LLVMValueRef persp_sample, persp_center, persp_centroid;
	LLVMValueRef linear_sample, linear_center, linear_centroid;

	gl_shader_stage stage;

	LLVMValueRef inputs[RADEON_LLVM_MAX_INPUTS * 4];

	uint64_t input_mask;
	uint64_t output_mask;
	uint8_t num_output_clips;
	uint8_t num_output_culls;

	bool is_gs_copy_shader;
	LLVMValueRef gs_next_vertex;
	unsigned gs_max_out_vertices;

	unsigned tes_primitive_mode;
	uint64_t tess_outputs_written;
	uint64_t tess_patch_outputs_written;

	uint32_t tcs_patch_outputs_read;
	uint64_t tcs_outputs_read;
};

static inline struct nir_to_llvm_context *
nir_to_llvm_context_from_abi(struct ac_shader_abi *abi)
{
	struct nir_to_llvm_context *ctx = NULL;
	return container_of(abi, ctx, abi);
}

static LLVMValueRef get_sampler_desc(struct ac_nir_context *ctx,
				     const nir_deref_var *deref,
				     enum ac_descriptor_type desc_type,
				     const nir_tex_instr *instr,
				     bool image, bool write);

static unsigned radeon_llvm_reg_index_soa(unsigned index, unsigned chan)
{
	return (index * 4) + chan;
}

static unsigned shader_io_get_unique_index(gl_varying_slot slot)
{
	/* handle patch indices separate */
	if (slot == VARYING_SLOT_TESS_LEVEL_OUTER)
		return 0;
	if (slot == VARYING_SLOT_TESS_LEVEL_INNER)
		return 1;
	if (slot >= VARYING_SLOT_PATCH0 && slot <= VARYING_SLOT_TESS_MAX)
		return 2 + (slot - VARYING_SLOT_PATCH0);

	if (slot == VARYING_SLOT_POS)
		return 0;
	if (slot == VARYING_SLOT_PSIZ)
		return 1;
	if (slot == VARYING_SLOT_CLIP_DIST0)
		return 2;
	/* 3 is reserved for clip dist as well */
	if (slot >= VARYING_SLOT_VAR0 && slot <= VARYING_SLOT_VAR31)
		return 4 + (slot - VARYING_SLOT_VAR0);
	unreachable("illegal slot in get unique index\n");
}

static void set_llvm_calling_convention(LLVMValueRef func,
                                        gl_shader_stage stage)
{
	enum radeon_llvm_calling_convention calling_conv;

	switch (stage) {
	case MESA_SHADER_VERTEX:
	case MESA_SHADER_TESS_EVAL:
		calling_conv = RADEON_LLVM_AMDGPU_VS;
		break;
	case MESA_SHADER_GEOMETRY:
		calling_conv = RADEON_LLVM_AMDGPU_GS;
		break;
	case MESA_SHADER_TESS_CTRL:
		calling_conv = HAVE_LLVM >= 0x0500 ? RADEON_LLVM_AMDGPU_HS : RADEON_LLVM_AMDGPU_VS;
		break;
	case MESA_SHADER_FRAGMENT:
		calling_conv = RADEON_LLVM_AMDGPU_PS;
		break;
	case MESA_SHADER_COMPUTE:
		calling_conv = RADEON_LLVM_AMDGPU_CS;
		break;
	default:
		unreachable("Unhandle shader type");
	}

	LLVMSetFunctionCallConv(func, calling_conv);
}

#define MAX_ARGS 23
struct arg_info {
	LLVMTypeRef types[MAX_ARGS];
	LLVMValueRef *assign[MAX_ARGS];
	unsigned array_params_mask;
	uint8_t count;
	uint8_t sgpr_count;
	uint8_t num_sgprs_used;
	uint8_t num_vgprs_used;
};

enum ac_arg_regfile {
	ARG_SGPR,
	ARG_VGPR,
};

static void
add_arg(struct arg_info *info, enum ac_arg_regfile regfile, LLVMTypeRef type,
	LLVMValueRef *param_ptr)
{
	assert(info->count < MAX_ARGS);

	info->assign[info->count] = param_ptr;
	info->types[info->count] = type;
	info->count++;

	if (regfile == ARG_SGPR) {
		info->num_sgprs_used += ac_get_type_size(type) / 4;
		info->sgpr_count++;
	} else {
		assert(regfile == ARG_VGPR);
		info->num_vgprs_used += ac_get_type_size(type) / 4;
	}
}

static inline void
add_array_arg(struct arg_info *info, LLVMTypeRef type, LLVMValueRef *param_ptr)
{
	info->array_params_mask |= (1 << info->count);
	add_arg(info, ARG_SGPR, type, param_ptr);
}

static void assign_arguments(LLVMValueRef main_function,
			     struct arg_info *info)
{
	unsigned i;
	for (i = 0; i < info->count; i++) {
		if (info->assign[i])
			*info->assign[i] = LLVMGetParam(main_function, i);
	}
}

static LLVMValueRef
create_llvm_function(LLVMContextRef ctx, LLVMModuleRef module,
                     LLVMBuilderRef builder, LLVMTypeRef *return_types,
                     unsigned num_return_elems,
		     struct arg_info *args,
		     unsigned max_workgroup_size,
		     bool unsafe_math)
{
	LLVMTypeRef main_function_type, ret_type;
	LLVMBasicBlockRef main_function_body;

	if (num_return_elems)
		ret_type = LLVMStructTypeInContext(ctx, return_types,
		                                   num_return_elems, true);
	else
		ret_type = LLVMVoidTypeInContext(ctx);

	/* Setup the function */
	main_function_type =
	    LLVMFunctionType(ret_type, args->types, args->count, 0);
	LLVMValueRef main_function =
	    LLVMAddFunction(module, "main", main_function_type);
	main_function_body =
	    LLVMAppendBasicBlockInContext(ctx, main_function, "main_body");
	LLVMPositionBuilderAtEnd(builder, main_function_body);

	LLVMSetFunctionCallConv(main_function, RADEON_LLVM_AMDGPU_CS);
	for (unsigned i = 0; i < args->sgpr_count; ++i) {
		ac_add_function_attr(ctx, main_function, i + 1, AC_FUNC_ATTR_INREG);

		if (args->array_params_mask & (1 << i)) {
			LLVMValueRef P = LLVMGetParam(main_function, i);
			ac_add_function_attr(ctx, main_function, i + 1, AC_FUNC_ATTR_NOALIAS);
			ac_add_attr_dereferenceable(P, UINT64_MAX);
		}
	}

	if (max_workgroup_size) {
		ac_llvm_add_target_dep_function_attr(main_function,
						     "amdgpu-max-work-group-size",
						     max_workgroup_size);
	}
	if (unsafe_math) {
		/* These were copied from some LLVM test. */
		LLVMAddTargetDependentFunctionAttr(main_function,
						   "less-precise-fpmad",
						   "true");
		LLVMAddTargetDependentFunctionAttr(main_function,
						   "no-infs-fp-math",
						   "true");
		LLVMAddTargetDependentFunctionAttr(main_function,
						   "no-nans-fp-math",
						   "true");
		LLVMAddTargetDependentFunctionAttr(main_function,
						   "unsafe-fp-math",
						   "true");
		LLVMAddTargetDependentFunctionAttr(main_function,
					   "no-signed-zeros-fp-math",
					   "true");
	}
	return main_function;
}

static int get_elem_bits(struct ac_llvm_context *ctx, LLVMTypeRef type)
{
	if (LLVMGetTypeKind(type) == LLVMVectorTypeKind)
		type = LLVMGetElementType(type);

	if (LLVMGetTypeKind(type) == LLVMIntegerTypeKind)
		return LLVMGetIntTypeWidth(type);

	if (type == ctx->f16)
		return 16;
	if (type == ctx->f32)
		return 32;
	if (type == ctx->f64)
		return 64;

	unreachable("Unhandled type kind in get_elem_bits");
}

static LLVMValueRef unpack_param(struct ac_llvm_context *ctx,
				 LLVMValueRef param, unsigned rshift,
				 unsigned bitwidth)
{
	LLVMValueRef value = param;
	if (rshift)
		value = LLVMBuildLShr(ctx->builder, value,
				      LLVMConstInt(ctx->i32, rshift, false), "");

	if (rshift + bitwidth < 32) {
		unsigned mask = (1 << bitwidth) - 1;
		value = LLVMBuildAnd(ctx->builder, value,
				     LLVMConstInt(ctx->i32, mask, false), "");
	}
	return value;
}

static LLVMValueRef get_rel_patch_id(struct nir_to_llvm_context *ctx)
{
	switch (ctx->stage) {
	case MESA_SHADER_TESS_CTRL:
		return unpack_param(&ctx->ac, ctx->abi.tcs_rel_ids, 0, 8);
	case MESA_SHADER_TESS_EVAL:
		return ctx->tes_rel_patch_id;
		break;
	default:
		unreachable("Illegal stage");
	}
}

/* Tessellation shaders pass outputs to the next shader using LDS.
 *
 * LS outputs = TCS inputs
 * TCS outputs = TES inputs
 *
 * The LDS layout is:
 * - TCS inputs for patch 0
 * - TCS inputs for patch 1
 * - TCS inputs for patch 2		= get_tcs_in_current_patch_offset (if RelPatchID==2)
 * - ...
 * - TCS outputs for patch 0            = get_tcs_out_patch0_offset
 * - Per-patch TCS outputs for patch 0  = get_tcs_out_patch0_patch_data_offset
 * - TCS outputs for patch 1
 * - Per-patch TCS outputs for patch 1
 * - TCS outputs for patch 2            = get_tcs_out_current_patch_offset (if RelPatchID==2)
 * - Per-patch TCS outputs for patch 2  = get_tcs_out_current_patch_data_offset (if RelPatchID==2)
 * - ...
 *
 * All three shaders VS(LS), TCS, TES share the same LDS space.
 */
static LLVMValueRef
get_tcs_in_patch_stride(struct nir_to_llvm_context *ctx)
{
	if (ctx->stage == MESA_SHADER_VERTEX)
		return unpack_param(&ctx->ac, ctx->ls_out_layout, 0, 13);
	else if (ctx->stage == MESA_SHADER_TESS_CTRL)
		return unpack_param(&ctx->ac, ctx->tcs_in_layout, 0, 13);
	else {
		assert(0);
		return NULL;
	}
}

static LLVMValueRef
get_tcs_out_patch_stride(struct nir_to_llvm_context *ctx)
{
	return unpack_param(&ctx->ac, ctx->tcs_out_layout, 0, 13);
}

static LLVMValueRef
get_tcs_out_patch0_offset(struct nir_to_llvm_context *ctx)
{
	return LLVMBuildMul(ctx->builder,
			    unpack_param(&ctx->ac, ctx->tcs_out_offsets, 0, 16),
			    LLVMConstInt(ctx->ac.i32, 4, false), "");
}

static LLVMValueRef
get_tcs_out_patch0_patch_data_offset(struct nir_to_llvm_context *ctx)
{
	return LLVMBuildMul(ctx->builder,
			    unpack_param(&ctx->ac, ctx->tcs_out_offsets, 16, 16),
			    LLVMConstInt(ctx->ac.i32, 4, false), "");
}

static LLVMValueRef
get_tcs_in_current_patch_offset(struct nir_to_llvm_context *ctx)
{
	LLVMValueRef patch_stride = get_tcs_in_patch_stride(ctx);
	LLVMValueRef rel_patch_id = get_rel_patch_id(ctx);

	return LLVMBuildMul(ctx->builder, patch_stride, rel_patch_id, "");
}

static LLVMValueRef
get_tcs_out_current_patch_offset(struct nir_to_llvm_context *ctx)
{
	LLVMValueRef patch0_offset = get_tcs_out_patch0_offset(ctx);
	LLVMValueRef patch_stride = get_tcs_out_patch_stride(ctx);
	LLVMValueRef rel_patch_id = get_rel_patch_id(ctx);

	return LLVMBuildAdd(ctx->builder, patch0_offset,
			    LLVMBuildMul(ctx->builder, patch_stride,
					 rel_patch_id, ""),
			    "");
}

static LLVMValueRef
get_tcs_out_current_patch_data_offset(struct nir_to_llvm_context *ctx)
{
	LLVMValueRef patch0_patch_data_offset =
		get_tcs_out_patch0_patch_data_offset(ctx);
	LLVMValueRef patch_stride = get_tcs_out_patch_stride(ctx);
	LLVMValueRef rel_patch_id = get_rel_patch_id(ctx);

	return LLVMBuildAdd(ctx->builder, patch0_patch_data_offset,
			    LLVMBuildMul(ctx->builder, patch_stride,
					 rel_patch_id, ""),
			    "");
}

static void
set_loc(struct ac_userdata_info *ud_info, uint8_t *sgpr_idx, uint8_t num_sgprs,
	uint32_t indirect_offset)
{
	ud_info->sgpr_idx = *sgpr_idx;
	ud_info->num_sgprs = num_sgprs;
	ud_info->indirect = indirect_offset > 0;
	ud_info->indirect_offset = indirect_offset;
	*sgpr_idx += num_sgprs;
}

static void
set_loc_shader(struct nir_to_llvm_context *ctx, int idx, uint8_t *sgpr_idx,
	       uint8_t num_sgprs)
{
	struct ac_userdata_info *ud_info =
		&ctx->shader_info->user_sgprs_locs.shader_data[idx];
	assert(ud_info);

	set_loc(ud_info, sgpr_idx, num_sgprs, 0);
}

static void
set_loc_desc(struct nir_to_llvm_context *ctx, int idx,  uint8_t *sgpr_idx,
	     uint32_t indirect_offset)
{
	struct ac_userdata_info *ud_info =
		&ctx->shader_info->user_sgprs_locs.descriptor_sets[idx];
	assert(ud_info);

	set_loc(ud_info, sgpr_idx, 2, indirect_offset);
}

struct user_sgpr_info {
	bool need_ring_offsets;
	uint8_t sgpr_count;
	bool indirect_all_descriptor_sets;
};

static bool needs_view_index_sgpr(struct nir_to_llvm_context *ctx,
				  gl_shader_stage stage)
{
	switch (stage) {
	case MESA_SHADER_VERTEX:
		if (ctx->shader_info->info.needs_multiview_view_index ||
		    (!ctx->options->key.vs.as_es && !ctx->options->key.vs.as_ls && ctx->options->key.has_multiview_view_index))
			return true;
		break;
	case MESA_SHADER_TESS_EVAL:
		if (ctx->shader_info->info.needs_multiview_view_index || (!ctx->options->key.tes.as_es && ctx->options->key.has_multiview_view_index))
			return true;
		break;
	case MESA_SHADER_GEOMETRY:
	case MESA_SHADER_TESS_CTRL:
		if (ctx->shader_info->info.needs_multiview_view_index)
			return true;
		break;
	default:
		break;
	}
	return false;
}

static void allocate_user_sgprs(struct nir_to_llvm_context *ctx,
				gl_shader_stage stage,
				bool needs_view_index,
				struct user_sgpr_info *user_sgpr_info)
{
	memset(user_sgpr_info, 0, sizeof(struct user_sgpr_info));

	/* until we sort out scratch/global buffers always assign ring offsets for gs/vs/es */
	if (stage == MESA_SHADER_GEOMETRY ||
	    stage == MESA_SHADER_VERTEX ||
	    stage == MESA_SHADER_TESS_CTRL ||
	    stage == MESA_SHADER_TESS_EVAL ||
	    ctx->is_gs_copy_shader)
		user_sgpr_info->need_ring_offsets = true;

	if (stage == MESA_SHADER_FRAGMENT &&
	    ctx->shader_info->info.ps.needs_sample_positions)
		user_sgpr_info->need_ring_offsets = true;

	/* 2 user sgprs will nearly always be allocated for scratch/rings */
	if (ctx->options->supports_spill || user_sgpr_info->need_ring_offsets) {
		user_sgpr_info->sgpr_count += 2;
	}

	/* FIXME: fix the number of user sgprs for merged shaders on GFX9 */
	switch (stage) {
	case MESA_SHADER_COMPUTE:
		if (ctx->shader_info->info.cs.uses_grid_size)
			user_sgpr_info->sgpr_count += 3;
		break;
	case MESA_SHADER_FRAGMENT:
		user_sgpr_info->sgpr_count += ctx->shader_info->info.ps.needs_sample_positions;
		break;
	case MESA_SHADER_VERTEX:
		if (!ctx->is_gs_copy_shader) {
			user_sgpr_info->sgpr_count += ctx->shader_info->info.vs.has_vertex_buffers ? 2 : 0;
			if (ctx->shader_info->info.vs.needs_draw_id) {
				user_sgpr_info->sgpr_count += 3;
			} else {
				user_sgpr_info->sgpr_count += 2;
			}
		}
		if (ctx->options->key.vs.as_ls)
			user_sgpr_info->sgpr_count++;
		break;
	case MESA_SHADER_TESS_CTRL:
		user_sgpr_info->sgpr_count += 4;
		break;
	case MESA_SHADER_TESS_EVAL:
		user_sgpr_info->sgpr_count += 1;
		break;
	case MESA_SHADER_GEOMETRY:
		user_sgpr_info->sgpr_count += 2;
		break;
	default:
		break;
	}

	if (needs_view_index)
		user_sgpr_info->sgpr_count++;

	if (ctx->shader_info->info.loads_push_constants)
		user_sgpr_info->sgpr_count += 2;

	uint32_t available_sgprs = ctx->options->chip_class >= GFX9 ? 32 : 16;
	uint32_t remaining_sgprs = available_sgprs - user_sgpr_info->sgpr_count;

	if (remaining_sgprs / 2 < util_bitcount(ctx->shader_info->info.desc_set_used_mask)) {
		user_sgpr_info->sgpr_count += 2;
		user_sgpr_info->indirect_all_descriptor_sets = true;
	} else {
		user_sgpr_info->sgpr_count += util_bitcount(ctx->shader_info->info.desc_set_used_mask) * 2;
	}
}

static void
declare_global_input_sgprs(struct nir_to_llvm_context *ctx,
			   gl_shader_stage stage,
			   bool has_previous_stage,
			   gl_shader_stage previous_stage,
			   const struct user_sgpr_info *user_sgpr_info,
			   struct arg_info *args,
			   LLVMValueRef *desc_sets)
{
	LLVMTypeRef type = ac_array_in_const_addr_space(ctx->ac.i8);
	unsigned num_sets = ctx->options->layout ?
			    ctx->options->layout->num_sets : 0;
	unsigned stage_mask = 1 << stage;

	if (has_previous_stage)
		stage_mask |= 1 << previous_stage;

	/* 1 for each descriptor set */
	if (!user_sgpr_info->indirect_all_descriptor_sets) {
		for (unsigned i = 0; i < num_sets; ++i) {
			if (ctx->options->layout->set[i].layout->shader_stages & stage_mask) {
				add_array_arg(args, type,
					      &ctx->descriptor_sets[i]);
			}
		}
	} else {
		add_array_arg(args, ac_array_in_const_addr_space(type), desc_sets);
	}

	if (ctx->shader_info->info.loads_push_constants) {
		/* 1 for push constants and dynamic descriptors */
		add_array_arg(args, type, &ctx->push_constants);
	}
}

static void
declare_vs_specific_input_sgprs(struct nir_to_llvm_context *ctx,
				gl_shader_stage stage,
				bool has_previous_stage,
				gl_shader_stage previous_stage,
				struct arg_info *args)
{
	if (!ctx->is_gs_copy_shader &&
	    (stage == MESA_SHADER_VERTEX ||
	     (has_previous_stage && previous_stage == MESA_SHADER_VERTEX))) {
		if (ctx->shader_info->info.vs.has_vertex_buffers) {
			add_arg(args, ARG_SGPR, ac_array_in_const_addr_space(ctx->ac.v4i32),
				&ctx->vertex_buffers);
		}
		add_arg(args, ARG_SGPR, ctx->ac.i32, &ctx->abi.base_vertex);
		add_arg(args, ARG_SGPR, ctx->ac.i32, &ctx->abi.start_instance);
		if (ctx->shader_info->info.vs.needs_draw_id) {
			add_arg(args, ARG_SGPR, ctx->ac.i32, &ctx->abi.draw_id);
		}
	}
}

static void
declare_vs_input_vgprs(struct nir_to_llvm_context *ctx, struct arg_info *args)
{
	add_arg(args, ARG_VGPR, ctx->ac.i32, &ctx->abi.vertex_id);
	if (!ctx->is_gs_copy_shader) {
		if (ctx->options->key.vs.as_ls) {
			add_arg(args, ARG_VGPR, ctx->ac.i32, &ctx->rel_auto_id);
			add_arg(args, ARG_VGPR, ctx->ac.i32, &ctx->abi.instance_id);
		} else {
			add_arg(args, ARG_VGPR, ctx->ac.i32, &ctx->abi.instance_id);
			add_arg(args, ARG_VGPR, ctx->ac.i32, &ctx->vs_prim_id);
		}
		add_arg(args, ARG_VGPR, ctx->ac.i32, NULL); /* unused */
	}
}

static void
declare_tes_input_vgprs(struct nir_to_llvm_context *ctx, struct arg_info *args)
{
	add_arg(args, ARG_VGPR, ctx->ac.f32, &ctx->tes_u);
	add_arg(args, ARG_VGPR, ctx->ac.f32, &ctx->tes_v);
	add_arg(args, ARG_VGPR, ctx->ac.i32, &ctx->tes_rel_patch_id);
	add_arg(args, ARG_VGPR, ctx->ac.i32, &ctx->abi.tes_patch_id);
}

static void
set_global_input_locs(struct nir_to_llvm_context *ctx, gl_shader_stage stage,
		      bool has_previous_stage, gl_shader_stage previous_stage,
		      const struct user_sgpr_info *user_sgpr_info,
		      LLVMValueRef desc_sets, uint8_t *user_sgpr_idx)
{
	unsigned num_sets = ctx->options->layout ?
			    ctx->options->layout->num_sets : 0;
	unsigned stage_mask = 1 << stage;

	if (has_previous_stage)
		stage_mask |= 1 << previous_stage;

	if (!user_sgpr_info->indirect_all_descriptor_sets) {
		for (unsigned i = 0; i < num_sets; ++i) {
			if (ctx->options->layout->set[i].layout->shader_stages & stage_mask) {
				set_loc_desc(ctx, i, user_sgpr_idx, 0);
			} else
				ctx->descriptor_sets[i] = NULL;
		}
	} else {
		set_loc_shader(ctx, AC_UD_INDIRECT_DESCRIPTOR_SETS,
			       user_sgpr_idx, 2);

		for (unsigned i = 0; i < num_sets; ++i) {
			if (ctx->options->layout->set[i].layout->shader_stages & stage_mask) {
				set_loc_desc(ctx, i, user_sgpr_idx, i * 8);
				ctx->descriptor_sets[i] =
					ac_build_load_to_sgpr(&ctx->ac,
							      desc_sets,
							      LLVMConstInt(ctx->ac.i32, i, false));

			} else
				ctx->descriptor_sets[i] = NULL;
		}
		ctx->shader_info->need_indirect_descriptor_sets = true;
	}

	if (ctx->shader_info->info.loads_push_constants) {
		set_loc_shader(ctx, AC_UD_PUSH_CONSTANTS, user_sgpr_idx, 2);
	}
}

static void
set_vs_specific_input_locs(struct nir_to_llvm_context *ctx,
			   gl_shader_stage stage, bool has_previous_stage,
			   gl_shader_stage previous_stage,
			   uint8_t *user_sgpr_idx)
{
	if (!ctx->is_gs_copy_shader &&
	    (stage == MESA_SHADER_VERTEX ||
	     (has_previous_stage && previous_stage == MESA_SHADER_VERTEX))) {
		if (ctx->shader_info->info.vs.has_vertex_buffers) {
			set_loc_shader(ctx, AC_UD_VS_VERTEX_BUFFERS,
				       user_sgpr_idx, 2);
		}

		unsigned vs_num = 2;
		if (ctx->shader_info->info.vs.needs_draw_id)
			vs_num++;

		set_loc_shader(ctx, AC_UD_VS_BASE_VERTEX_START_INSTANCE,
			       user_sgpr_idx, vs_num);
	}
}

static void create_function(struct nir_to_llvm_context *ctx,
                            gl_shader_stage stage,
                            bool has_previous_stage,
                            gl_shader_stage previous_stage)
{
	uint8_t user_sgpr_idx;
	struct user_sgpr_info user_sgpr_info;
	struct arg_info args = {};
	LLVMValueRef desc_sets;
	bool needs_view_index = needs_view_index_sgpr(ctx, stage);
	allocate_user_sgprs(ctx, stage, needs_view_index, &user_sgpr_info);

	if (user_sgpr_info.need_ring_offsets && !ctx->options->supports_spill) {
		add_arg(&args, ARG_SGPR, ac_array_in_const_addr_space(ctx->ac.v4i32),
			&ctx->ring_offsets);
	}

	switch (stage) {
	case MESA_SHADER_COMPUTE:
		declare_global_input_sgprs(ctx, stage, has_previous_stage,
					   previous_stage, &user_sgpr_info,
					   &args, &desc_sets);

		if (ctx->shader_info->info.cs.uses_grid_size) {
			add_arg(&args, ARG_SGPR, ctx->ac.v3i32,
				&ctx->num_work_groups);
		}

		for (int i = 0; i < 3; i++) {
			ctx->workgroup_ids[i] = NULL;
			if (ctx->shader_info->info.cs.uses_block_id[i]) {
				add_arg(&args, ARG_SGPR, ctx->ac.i32,
					&ctx->workgroup_ids[i]);
			}
		}

		if (ctx->shader_info->info.cs.uses_local_invocation_idx)
			add_arg(&args, ARG_SGPR, ctx->ac.i32, &ctx->tg_size);
		add_arg(&args, ARG_VGPR, ctx->ac.v3i32,
			&ctx->local_invocation_ids);
		break;
	case MESA_SHADER_VERTEX:
		declare_global_input_sgprs(ctx, stage, has_previous_stage,
					   previous_stage, &user_sgpr_info,
					   &args, &desc_sets);
		declare_vs_specific_input_sgprs(ctx, stage, has_previous_stage,
						previous_stage, &args);

		if (needs_view_index)
			add_arg(&args, ARG_SGPR, ctx->ac.i32, &ctx->view_index);
		if (ctx->options->key.vs.as_es)
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->es2gs_offset);
		else if (ctx->options->key.vs.as_ls)
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->ls_out_layout);

		declare_vs_input_vgprs(ctx, &args);
		break;
	case MESA_SHADER_TESS_CTRL:
		if (has_previous_stage) {
			// First 6 system regs
			add_arg(&args, ARG_SGPR, ctx->ac.i32, &ctx->oc_lds);
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->merged_wave_info);
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->tess_factor_offset);

			add_arg(&args, ARG_SGPR, ctx->ac.i32, NULL); // scratch offset
			add_arg(&args, ARG_SGPR, ctx->ac.i32, NULL); // unknown
			add_arg(&args, ARG_SGPR, ctx->ac.i32, NULL); // unknown

			declare_global_input_sgprs(ctx, stage,
						   has_previous_stage,
						   previous_stage,
						   &user_sgpr_info, &args,
						   &desc_sets);
			declare_vs_specific_input_sgprs(ctx, stage,
							has_previous_stage,
							previous_stage, &args);

			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->ls_out_layout);

			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->tcs_offchip_layout);
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->tcs_out_offsets);
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->tcs_out_layout);
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->tcs_in_layout);
			if (needs_view_index)
				add_arg(&args, ARG_SGPR, ctx->ac.i32,
					&ctx->view_index);

			add_arg(&args, ARG_VGPR, ctx->ac.i32,
				&ctx->abi.tcs_patch_id);
			add_arg(&args, ARG_VGPR, ctx->ac.i32,
				&ctx->abi.tcs_rel_ids);

			declare_vs_input_vgprs(ctx, &args);
		} else {
			declare_global_input_sgprs(ctx, stage,
						   has_previous_stage,
						   previous_stage,
						   &user_sgpr_info, &args,
						   &desc_sets);

			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->tcs_offchip_layout);
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->tcs_out_offsets);
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->tcs_out_layout);
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->tcs_in_layout);
			if (needs_view_index)
				add_arg(&args, ARG_SGPR, ctx->ac.i32,
					&ctx->view_index);

			add_arg(&args, ARG_SGPR, ctx->ac.i32, &ctx->oc_lds);
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->tess_factor_offset);
			add_arg(&args, ARG_VGPR, ctx->ac.i32,
				&ctx->abi.tcs_patch_id);
			add_arg(&args, ARG_VGPR, ctx->ac.i32,
				&ctx->abi.tcs_rel_ids);
		}
		break;
	case MESA_SHADER_TESS_EVAL:
		declare_global_input_sgprs(ctx, stage, has_previous_stage,
					   previous_stage, &user_sgpr_info,
					   &args, &desc_sets);

		add_arg(&args, ARG_SGPR, ctx->ac.i32, &ctx->tcs_offchip_layout);
		if (needs_view_index)
			add_arg(&args, ARG_SGPR, ctx->ac.i32, &ctx->view_index);

		if (ctx->options->key.tes.as_es) {
			add_arg(&args, ARG_SGPR, ctx->ac.i32, &ctx->oc_lds);
			add_arg(&args, ARG_SGPR, ctx->ac.i32, NULL);
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->es2gs_offset);
		} else {
			add_arg(&args, ARG_SGPR, ctx->ac.i32, NULL);
			add_arg(&args, ARG_SGPR, ctx->ac.i32, &ctx->oc_lds);
		}
		declare_tes_input_vgprs(ctx, &args);
		break;
	case MESA_SHADER_GEOMETRY:
		if (has_previous_stage) {
			// First 6 system regs
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->gs2vs_offset);
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->merged_wave_info);
			add_arg(&args, ARG_SGPR, ctx->ac.i32, &ctx->oc_lds);

			add_arg(&args, ARG_SGPR, ctx->ac.i32, NULL); // scratch offset
			add_arg(&args, ARG_SGPR, ctx->ac.i32, NULL); // unknown
			add_arg(&args, ARG_SGPR, ctx->ac.i32, NULL); // unknown

			declare_global_input_sgprs(ctx, stage,
						   has_previous_stage,
						   previous_stage,
						   &user_sgpr_info, &args,
						   &desc_sets);

			if (previous_stage == MESA_SHADER_TESS_EVAL) {
				add_arg(&args, ARG_SGPR, ctx->ac.i32,
					&ctx->tcs_offchip_layout);
			} else {
				declare_vs_specific_input_sgprs(ctx, stage,
								has_previous_stage,
								previous_stage,
								&args);
			}

			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->gsvs_ring_stride);
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->gsvs_num_entries);
			if (needs_view_index)
				add_arg(&args, ARG_SGPR, ctx->ac.i32,
					&ctx->view_index);

			add_arg(&args, ARG_VGPR, ctx->ac.i32,
				&ctx->gs_vtx_offset[0]);
			add_arg(&args, ARG_VGPR, ctx->ac.i32,
				&ctx->gs_vtx_offset[2]);
			add_arg(&args, ARG_VGPR, ctx->ac.i32,
				&ctx->abi.gs_prim_id);
			add_arg(&args, ARG_VGPR, ctx->ac.i32,
				&ctx->abi.gs_invocation_id);
			add_arg(&args, ARG_VGPR, ctx->ac.i32,
				&ctx->gs_vtx_offset[4]);

			if (previous_stage == MESA_SHADER_VERTEX) {
				declare_vs_input_vgprs(ctx, &args);
			} else {
				declare_tes_input_vgprs(ctx, &args);
			}
		} else {
			declare_global_input_sgprs(ctx, stage,
						   has_previous_stage,
						   previous_stage,
						   &user_sgpr_info, &args,
						   &desc_sets);

			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->gsvs_ring_stride);
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->gsvs_num_entries);
			if (needs_view_index)
				add_arg(&args, ARG_SGPR, ctx->ac.i32,
					&ctx->view_index);

			add_arg(&args, ARG_SGPR, ctx->ac.i32, &ctx->gs2vs_offset);
			add_arg(&args, ARG_SGPR, ctx->ac.i32, &ctx->gs_wave_id);
			add_arg(&args, ARG_VGPR, ctx->ac.i32,
				&ctx->gs_vtx_offset[0]);
			add_arg(&args, ARG_VGPR, ctx->ac.i32,
				&ctx->gs_vtx_offset[1]);
			add_arg(&args, ARG_VGPR, ctx->ac.i32,
				&ctx->abi.gs_prim_id);
			add_arg(&args, ARG_VGPR, ctx->ac.i32,
				&ctx->gs_vtx_offset[2]);
			add_arg(&args, ARG_VGPR, ctx->ac.i32,
				&ctx->gs_vtx_offset[3]);
			add_arg(&args, ARG_VGPR, ctx->ac.i32,
				&ctx->gs_vtx_offset[4]);
			add_arg(&args, ARG_VGPR, ctx->ac.i32,
				&ctx->gs_vtx_offset[5]);
			add_arg(&args, ARG_VGPR, ctx->ac.i32,
				&ctx->abi.gs_invocation_id);
		}
		break;
	case MESA_SHADER_FRAGMENT:
		declare_global_input_sgprs(ctx, stage, has_previous_stage,
					   previous_stage, &user_sgpr_info,
					   &args, &desc_sets);

		if (ctx->shader_info->info.ps.needs_sample_positions)
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->sample_pos_offset);

		add_arg(&args, ARG_SGPR, ctx->ac.i32, &ctx->abi.prim_mask);
		add_arg(&args, ARG_VGPR, ctx->ac.v2i32, &ctx->persp_sample);
		add_arg(&args, ARG_VGPR, ctx->ac.v2i32, &ctx->persp_center);
		add_arg(&args, ARG_VGPR, ctx->ac.v2i32, &ctx->persp_centroid);
		add_arg(&args, ARG_VGPR, ctx->ac.v3i32, NULL); /* persp pull model */
		add_arg(&args, ARG_VGPR, ctx->ac.v2i32, &ctx->linear_sample);
		add_arg(&args, ARG_VGPR, ctx->ac.v2i32, &ctx->linear_center);
		add_arg(&args, ARG_VGPR, ctx->ac.v2i32, &ctx->linear_centroid);
		add_arg(&args, ARG_VGPR, ctx->ac.f32, NULL);  /* line stipple tex */
		add_arg(&args, ARG_VGPR, ctx->ac.f32, &ctx->abi.frag_pos[0]);
		add_arg(&args, ARG_VGPR, ctx->ac.f32, &ctx->abi.frag_pos[1]);
		add_arg(&args, ARG_VGPR, ctx->ac.f32, &ctx->abi.frag_pos[2]);
		add_arg(&args, ARG_VGPR, ctx->ac.f32, &ctx->abi.frag_pos[3]);
		add_arg(&args, ARG_VGPR, ctx->ac.i32, &ctx->abi.front_face);
		add_arg(&args, ARG_VGPR, ctx->ac.i32, &ctx->abi.ancillary);
		add_arg(&args, ARG_VGPR, ctx->ac.i32, &ctx->abi.sample_coverage);
		add_arg(&args, ARG_VGPR, ctx->ac.i32, NULL);  /* fixed pt */
		break;
	default:
		unreachable("Shader stage not implemented");
	}

	ctx->main_function = create_llvm_function(
	    ctx->context, ctx->module, ctx->builder, NULL, 0, &args,
	    ctx->max_workgroup_size,
	    ctx->options->unsafe_math);
	set_llvm_calling_convention(ctx->main_function, stage);


	ctx->shader_info->num_input_vgprs = 0;
	ctx->shader_info->num_input_sgprs = ctx->options->supports_spill ? 2 : 0;

	ctx->shader_info->num_input_sgprs += args.num_sgprs_used;

	if (ctx->stage != MESA_SHADER_FRAGMENT)
		ctx->shader_info->num_input_vgprs = args.num_vgprs_used;

	assign_arguments(ctx->main_function, &args);

	user_sgpr_idx = 0;

	if (ctx->options->supports_spill || user_sgpr_info.need_ring_offsets) {
		set_loc_shader(ctx, AC_UD_SCRATCH_RING_OFFSETS,
			       &user_sgpr_idx, 2);
		if (ctx->options->supports_spill) {
			ctx->ring_offsets = ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.implicit.buffer.ptr",
							       LLVMPointerType(ctx->ac.i8, AC_CONST_ADDR_SPACE),
							       NULL, 0, AC_FUNC_ATTR_READNONE);
			ctx->ring_offsets = LLVMBuildBitCast(ctx->builder, ctx->ring_offsets,
							     ac_array_in_const_addr_space(ctx->ac.v4i32), "");
		}
	}
	
	/* For merged shaders the user SGPRs start at 8, with 8 system SGPRs in front (including
	 * the rw_buffers at s0/s1. With user SGPR0 = s8, lets restart the count from 0 */
	if (has_previous_stage)
		user_sgpr_idx = 0;

	set_global_input_locs(ctx, stage, has_previous_stage, previous_stage,
			      &user_sgpr_info, desc_sets, &user_sgpr_idx);

	switch (stage) {
	case MESA_SHADER_COMPUTE:
		if (ctx->shader_info->info.cs.uses_grid_size) {
			set_loc_shader(ctx, AC_UD_CS_GRID_SIZE,
				       &user_sgpr_idx, 3);
		}
		break;
	case MESA_SHADER_VERTEX:
		set_vs_specific_input_locs(ctx, stage, has_previous_stage,
					   previous_stage, &user_sgpr_idx);
		if (ctx->view_index)
			set_loc_shader(ctx, AC_UD_VIEW_INDEX, &user_sgpr_idx, 1);
		if (ctx->options->key.vs.as_ls) {
			set_loc_shader(ctx, AC_UD_VS_LS_TCS_IN_LAYOUT,
				       &user_sgpr_idx, 1);
		}
		if (ctx->options->key.vs.as_ls)
			ac_declare_lds_as_pointer(&ctx->ac);
		break;
	case MESA_SHADER_TESS_CTRL:
		set_vs_specific_input_locs(ctx, stage, has_previous_stage,
					   previous_stage, &user_sgpr_idx);
		if (has_previous_stage)
			set_loc_shader(ctx, AC_UD_VS_LS_TCS_IN_LAYOUT,
				       &user_sgpr_idx, 1);
		set_loc_shader(ctx, AC_UD_TCS_OFFCHIP_LAYOUT, &user_sgpr_idx, 4);
		if (ctx->view_index)
			set_loc_shader(ctx, AC_UD_VIEW_INDEX, &user_sgpr_idx, 1);
		ac_declare_lds_as_pointer(&ctx->ac);
		break;
	case MESA_SHADER_TESS_EVAL:
		set_loc_shader(ctx, AC_UD_TES_OFFCHIP_LAYOUT, &user_sgpr_idx, 1);
		if (ctx->view_index)
			set_loc_shader(ctx, AC_UD_VIEW_INDEX, &user_sgpr_idx, 1);
		break;
	case MESA_SHADER_GEOMETRY:
		if (has_previous_stage) {
			if (previous_stage == MESA_SHADER_VERTEX)
				set_vs_specific_input_locs(ctx, stage,
							   has_previous_stage,
							   previous_stage,
							   &user_sgpr_idx);
			else
				set_loc_shader(ctx, AC_UD_TES_OFFCHIP_LAYOUT,
					       &user_sgpr_idx, 1);
		}
		set_loc_shader(ctx, AC_UD_GS_VS_RING_STRIDE_ENTRIES,
			       &user_sgpr_idx, 2);
		if (ctx->view_index)
			set_loc_shader(ctx, AC_UD_VIEW_INDEX, &user_sgpr_idx, 1);
		if (has_previous_stage)
			ac_declare_lds_as_pointer(&ctx->ac);
		break;
	case MESA_SHADER_FRAGMENT:
		if (ctx->shader_info->info.ps.needs_sample_positions) {
			set_loc_shader(ctx, AC_UD_PS_SAMPLE_POS_OFFSET,
				       &user_sgpr_idx, 1);
		}
		break;
	default:
		unreachable("Shader stage not implemented");
	}

	ctx->shader_info->num_user_sgprs = user_sgpr_idx;
}

static LLVMValueRef trim_vector(struct ac_llvm_context *ctx,
                                LLVMValueRef value, unsigned count)
{
	unsigned num_components = ac_get_llvm_num_components(value);
	if (count == num_components)
		return value;

	LLVMValueRef masks[] = {
	    LLVMConstInt(ctx->i32, 0, false), LLVMConstInt(ctx->i32, 1, false),
	    LLVMConstInt(ctx->i32, 2, false), LLVMConstInt(ctx->i32, 3, false)};

	if (count == 1)
		return LLVMBuildExtractElement(ctx->builder, value, masks[0],
		                               "");

	LLVMValueRef swizzle = LLVMConstVector(masks, count);
	return LLVMBuildShuffleVector(ctx->builder, value, value, swizzle, "");
}

static void
build_store_values_extended(struct ac_llvm_context *ac,
			     LLVMValueRef *values,
			     unsigned value_count,
			     unsigned value_stride,
			     LLVMValueRef vec)
{
	LLVMBuilderRef builder = ac->builder;
	unsigned i;

	for (i = 0; i < value_count; i++) {
		LLVMValueRef ptr = values[i * value_stride];
		LLVMValueRef index = LLVMConstInt(ac->i32, i, false);
		LLVMValueRef value = LLVMBuildExtractElement(builder, vec, index, "");
		LLVMBuildStore(builder, value, ptr);
	}
}

static LLVMTypeRef get_def_type(struct ac_nir_context *ctx,
                                const nir_ssa_def *def)
{
	LLVMTypeRef type = LLVMIntTypeInContext(ctx->ac.context, def->bit_size);
	if (def->num_components > 1) {
		type = LLVMVectorType(type, def->num_components);
	}
	return type;
}

static LLVMValueRef get_src(struct ac_nir_context *nir, nir_src src)
{
	assert(src.is_ssa);
	struct hash_entry *entry = _mesa_hash_table_search(nir->defs, src.ssa);
	return (LLVMValueRef)entry->data;
}


static LLVMBasicBlockRef get_block(struct ac_nir_context *nir,
                                   const struct nir_block *b)
{
	struct hash_entry *entry = _mesa_hash_table_search(nir->defs, b);
	return (LLVMBasicBlockRef)entry->data;
}

static LLVMValueRef get_alu_src(struct ac_nir_context *ctx,
                                nir_alu_src src,
                                unsigned num_components)
{
	LLVMValueRef value = get_src(ctx, src.src);
	bool need_swizzle = false;

	assert(value);
	LLVMTypeRef type = LLVMTypeOf(value);
	unsigned src_components = LLVMGetTypeKind(type) == LLVMVectorTypeKind
	                              ? LLVMGetVectorSize(type)
	                              : 1;

	for (unsigned i = 0; i < num_components; ++i) {
		assert(src.swizzle[i] < src_components);
		if (src.swizzle[i] != i)
			need_swizzle = true;
	}

	if (need_swizzle || num_components != src_components) {
		LLVMValueRef masks[] = {
		    LLVMConstInt(ctx->ac.i32, src.swizzle[0], false),
		    LLVMConstInt(ctx->ac.i32, src.swizzle[1], false),
		    LLVMConstInt(ctx->ac.i32, src.swizzle[2], false),
		    LLVMConstInt(ctx->ac.i32, src.swizzle[3], false)};

		if (src_components > 1 && num_components == 1) {
			value = LLVMBuildExtractElement(ctx->ac.builder, value,
			                                masks[0], "");
		} else if (src_components == 1 && num_components > 1) {
			LLVMValueRef values[] = {value, value, value, value};
			value = ac_build_gather_values(&ctx->ac, values, num_components);
		} else {
			LLVMValueRef swizzle = LLVMConstVector(masks, num_components);
			value = LLVMBuildShuffleVector(ctx->ac.builder, value, value,
		                                       swizzle, "");
		}
	}
	assert(!src.negate);
	assert(!src.abs);
	return value;
}

static LLVMValueRef emit_int_cmp(struct ac_llvm_context *ctx,
                                 LLVMIntPredicate pred, LLVMValueRef src0,
                                 LLVMValueRef src1)
{
	LLVMValueRef result = LLVMBuildICmp(ctx->builder, pred, src0, src1, "");
	return LLVMBuildSelect(ctx->builder, result,
	                       LLVMConstInt(ctx->i32, 0xFFFFFFFF, false),
	                       ctx->i32_0, "");
}

static LLVMValueRef emit_float_cmp(struct ac_llvm_context *ctx,
                                   LLVMRealPredicate pred, LLVMValueRef src0,
                                   LLVMValueRef src1)
{
	LLVMValueRef result;
	src0 = ac_to_float(ctx, src0);
	src1 = ac_to_float(ctx, src1);
	result = LLVMBuildFCmp(ctx->builder, pred, src0, src1, "");
	return LLVMBuildSelect(ctx->builder, result,
	                       LLVMConstInt(ctx->i32, 0xFFFFFFFF, false),
			       ctx->i32_0, "");
}

static LLVMValueRef emit_intrin_1f_param(struct ac_llvm_context *ctx,
					 const char *intrin,
					 LLVMTypeRef result_type,
					 LLVMValueRef src0)
{
	char name[64];
	LLVMValueRef params[] = {
		ac_to_float(ctx, src0),
	};

	MAYBE_UNUSED const int length = snprintf(name, sizeof(name), "%s.f%d", intrin,
						 get_elem_bits(ctx, result_type));
	assert(length < sizeof(name));
	return ac_build_intrinsic(ctx, name, result_type, params, 1, AC_FUNC_ATTR_READNONE);
}

static LLVMValueRef emit_intrin_2f_param(struct ac_llvm_context *ctx,
				       const char *intrin,
				       LLVMTypeRef result_type,
				       LLVMValueRef src0, LLVMValueRef src1)
{
	char name[64];
	LLVMValueRef params[] = {
		ac_to_float(ctx, src0),
		ac_to_float(ctx, src1),
	};

	MAYBE_UNUSED const int length = snprintf(name, sizeof(name), "%s.f%d", intrin,
						 get_elem_bits(ctx, result_type));
	assert(length < sizeof(name));
	return ac_build_intrinsic(ctx, name, result_type, params, 2, AC_FUNC_ATTR_READNONE);
}

static LLVMValueRef emit_intrin_3f_param(struct ac_llvm_context *ctx,
					 const char *intrin,
					 LLVMTypeRef result_type,
					 LLVMValueRef src0, LLVMValueRef src1, LLVMValueRef src2)
{
	char name[64];
	LLVMValueRef params[] = {
		ac_to_float(ctx, src0),
		ac_to_float(ctx, src1),
		ac_to_float(ctx, src2),
	};

	MAYBE_UNUSED const int length = snprintf(name, sizeof(name), "%s.f%d", intrin,
						 get_elem_bits(ctx, result_type));
	assert(length < sizeof(name));
	return ac_build_intrinsic(ctx, name, result_type, params, 3, AC_FUNC_ATTR_READNONE);
}

static LLVMValueRef emit_bcsel(struct ac_llvm_context *ctx,
			       LLVMValueRef src0, LLVMValueRef src1, LLVMValueRef src2)
{
	LLVMValueRef v = LLVMBuildICmp(ctx->builder, LLVMIntNE, src0,
				       ctx->i32_0, "");
	return LLVMBuildSelect(ctx->builder, v, src1, src2, "");
}

static LLVMValueRef emit_minmax_int(struct ac_llvm_context *ctx,
				    LLVMIntPredicate pred,
				    LLVMValueRef src0, LLVMValueRef src1)
{
	return LLVMBuildSelect(ctx->builder,
			       LLVMBuildICmp(ctx->builder, pred, src0, src1, ""),
			       src0,
			       src1, "");

}
static LLVMValueRef emit_iabs(struct ac_llvm_context *ctx,
			      LLVMValueRef src0)
{
	return emit_minmax_int(ctx, LLVMIntSGT, src0,
			       LLVMBuildNeg(ctx->builder, src0, ""));
}

static LLVMValueRef emit_fsign(struct ac_llvm_context *ctx,
			       LLVMValueRef src0,
			       unsigned bitsize)
{
	LLVMValueRef cmp, val, zero, one;
	LLVMTypeRef type;

	if (bitsize == 32) {
		type = ctx->f32;
		zero = ctx->f32_0;
		one = ctx->f32_1;
	} else {
		type = ctx->f64;
		zero = ctx->f64_0;
		one = ctx->f64_1;
	}

	cmp = LLVMBuildFCmp(ctx->builder, LLVMRealOGT, src0, zero, "");
	val = LLVMBuildSelect(ctx->builder, cmp, one, src0, "");
	cmp = LLVMBuildFCmp(ctx->builder, LLVMRealOGE, val, zero, "");
	val = LLVMBuildSelect(ctx->builder, cmp, val, LLVMConstReal(type, -1.0), "");
	return val;
}

static LLVMValueRef emit_isign(struct ac_llvm_context *ctx,
			       LLVMValueRef src0, unsigned bitsize)
{
	LLVMValueRef cmp, val, zero, one;
	LLVMTypeRef type;

	if (bitsize == 32) {
		type = ctx->i32;
		zero = ctx->i32_0;
		one = ctx->i32_1;
	} else {
		type = ctx->i64;
		zero = ctx->i64_0;
		one = ctx->i64_1;
	}

	cmp = LLVMBuildICmp(ctx->builder, LLVMIntSGT, src0, zero, "");
	val = LLVMBuildSelect(ctx->builder, cmp, one, src0, "");
	cmp = LLVMBuildICmp(ctx->builder, LLVMIntSGE, val, zero, "");
	val = LLVMBuildSelect(ctx->builder, cmp, val, LLVMConstInt(type, -1, true), "");
	return val;
}

static LLVMValueRef emit_ffract(struct ac_llvm_context *ctx,
				LLVMValueRef src0, unsigned bitsize)
{
	LLVMTypeRef type;
	char *intr;

	if (bitsize == 32) {
		intr = "llvm.floor.f32";
		type = ctx->f32;
	} else {
		intr = "llvm.floor.f64";
		type = ctx->f64;
	}

	LLVMValueRef fsrc0 = ac_to_float(ctx, src0);
	LLVMValueRef params[] = {
		fsrc0,
	};
	LLVMValueRef floor = ac_build_intrinsic(ctx, intr, type, params, 1,
						AC_FUNC_ATTR_READNONE);
	return LLVMBuildFSub(ctx->builder, fsrc0, floor, "");
}

static LLVMValueRef emit_uint_carry(struct ac_llvm_context *ctx,
				    const char *intrin,
				    LLVMValueRef src0, LLVMValueRef src1)
{
	LLVMTypeRef ret_type;
	LLVMTypeRef types[] = { ctx->i32, ctx->i1 };
	LLVMValueRef res;
	LLVMValueRef params[] = { src0, src1 };
	ret_type = LLVMStructTypeInContext(ctx->context, types,
					   2, true);

	res = ac_build_intrinsic(ctx, intrin, ret_type,
				 params, 2, AC_FUNC_ATTR_READNONE);

	res = LLVMBuildExtractValue(ctx->builder, res, 1, "");
	res = LLVMBuildZExt(ctx->builder, res, ctx->i32, "");
	return res;
}

static LLVMValueRef emit_b2f(struct ac_llvm_context *ctx,
			     LLVMValueRef src0)
{
	return LLVMBuildAnd(ctx->builder, src0, LLVMBuildBitCast(ctx->builder, LLVMConstReal(ctx->f32, 1.0), ctx->i32, ""), "");
}

static LLVMValueRef emit_f2b(struct ac_llvm_context *ctx,
			     LLVMValueRef src0)
{
	src0 = ac_to_float(ctx, src0);
	return LLVMBuildSExt(ctx->builder,
			     LLVMBuildFCmp(ctx->builder, LLVMRealUNE, src0, ctx->f32_0, ""),
			     ctx->i32, "");
}

static LLVMValueRef emit_b2i(struct ac_llvm_context *ctx,
			     LLVMValueRef src0,
			     unsigned bitsize)
{
	LLVMValueRef result = LLVMBuildAnd(ctx->builder, src0, ctx->i32_1, "");

	if (bitsize == 32)
		return result;

	return LLVMBuildZExt(ctx->builder, result, ctx->i64, "");
}

static LLVMValueRef emit_i2b(struct ac_llvm_context *ctx,
			     LLVMValueRef src0)
{
	return LLVMBuildSExt(ctx->builder,
			     LLVMBuildICmp(ctx->builder, LLVMIntNE, src0, ctx->i32_0, ""),
			     ctx->i32, "");
}

static LLVMValueRef emit_f2f16(struct nir_to_llvm_context *ctx,
			       LLVMValueRef src0)
{
	LLVMValueRef result;
	LLVMValueRef cond = NULL;

	src0 = ac_to_float(&ctx->ac, src0);
	result = LLVMBuildFPTrunc(ctx->builder, src0, ctx->ac.f16, "");

	if (ctx->options->chip_class >= VI) {
		LLVMValueRef args[2];
		/* Check if the result is a denormal - and flush to 0 if so. */
		args[0] = result;
		args[1] = LLVMConstInt(ctx->ac.i32, N_SUBNORMAL | P_SUBNORMAL, false);
		cond = ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.class.f16", ctx->ac.i1, args, 2, AC_FUNC_ATTR_READNONE);
	}

	/* need to convert back up to f32 */
	result = LLVMBuildFPExt(ctx->builder, result, ctx->ac.f32, "");

	if (ctx->options->chip_class >= VI)
		result = LLVMBuildSelect(ctx->builder, cond, ctx->ac.f32_0, result, "");
	else {
		/* for SI/CIK */
		/* 0x38800000 is smallest half float value (2^-14) in 32-bit float,
		 * so compare the result and flush to 0 if it's smaller.
		 */
		LLVMValueRef temp, cond2;
		temp = emit_intrin_1f_param(&ctx->ac, "llvm.fabs",
					    ctx->ac.f32, result);
		cond = LLVMBuildFCmp(ctx->builder, LLVMRealUGT,
				     LLVMBuildBitCast(ctx->builder, LLVMConstInt(ctx->ac.i32, 0x38800000, false), ctx->ac.f32, ""),
				     temp, "");
		cond2 = LLVMBuildFCmp(ctx->builder, LLVMRealUNE,
				      temp, ctx->ac.f32_0, "");
		cond = LLVMBuildAnd(ctx->builder, cond, cond2, "");
		result = LLVMBuildSelect(ctx->builder, cond, ctx->ac.f32_0, result, "");
	}
	return result;
}

static LLVMValueRef emit_umul_high(struct ac_llvm_context *ctx,
				   LLVMValueRef src0, LLVMValueRef src1)
{
	LLVMValueRef dst64, result;
	src0 = LLVMBuildZExt(ctx->builder, src0, ctx->i64, "");
	src1 = LLVMBuildZExt(ctx->builder, src1, ctx->i64, "");

	dst64 = LLVMBuildMul(ctx->builder, src0, src1, "");
	dst64 = LLVMBuildLShr(ctx->builder, dst64, LLVMConstInt(ctx->i64, 32, false), "");
	result = LLVMBuildTrunc(ctx->builder, dst64, ctx->i32, "");
	return result;
}

static LLVMValueRef emit_imul_high(struct ac_llvm_context *ctx,
				   LLVMValueRef src0, LLVMValueRef src1)
{
	LLVMValueRef dst64, result;
	src0 = LLVMBuildSExt(ctx->builder, src0, ctx->i64, "");
	src1 = LLVMBuildSExt(ctx->builder, src1, ctx->i64, "");

	dst64 = LLVMBuildMul(ctx->builder, src0, src1, "");
	dst64 = LLVMBuildAShr(ctx->builder, dst64, LLVMConstInt(ctx->i64, 32, false), "");
	result = LLVMBuildTrunc(ctx->builder, dst64, ctx->i32, "");
	return result;
}

static LLVMValueRef emit_bitfield_extract(struct ac_llvm_context *ctx,
					  bool is_signed,
					  const LLVMValueRef srcs[3])
{
	LLVMValueRef result;
	LLVMValueRef icond = LLVMBuildICmp(ctx->builder, LLVMIntEQ, srcs[2], LLVMConstInt(ctx->i32, 32, false), "");

	result = ac_build_bfe(ctx, srcs[0], srcs[1], srcs[2], is_signed);
	result = LLVMBuildSelect(ctx->builder, icond, srcs[0], result, "");
	return result;
}

static LLVMValueRef emit_bitfield_insert(struct ac_llvm_context *ctx,
					 LLVMValueRef src0, LLVMValueRef src1,
					 LLVMValueRef src2, LLVMValueRef src3)
{
	LLVMValueRef bfi_args[3], result;

	bfi_args[0] = LLVMBuildShl(ctx->builder,
				   LLVMBuildSub(ctx->builder,
						LLVMBuildShl(ctx->builder,
							     ctx->i32_1,
							     src3, ""),
						ctx->i32_1, ""),
				   src2, "");
	bfi_args[1] = LLVMBuildShl(ctx->builder, src1, src2, "");
	bfi_args[2] = src0;

	LLVMValueRef icond = LLVMBuildICmp(ctx->builder, LLVMIntEQ, src3, LLVMConstInt(ctx->i32, 32, false), "");

	/* Calculate:
	 *   (arg0 & arg1) | (~arg0 & arg2) = arg2 ^ (arg0 & (arg1 ^ arg2)
	 * Use the right-hand side, which the LLVM backend can convert to V_BFI.
	 */
	result = LLVMBuildXor(ctx->builder, bfi_args[2],
			      LLVMBuildAnd(ctx->builder, bfi_args[0],
					   LLVMBuildXor(ctx->builder, bfi_args[1], bfi_args[2], ""), ""), "");

	result = LLVMBuildSelect(ctx->builder, icond, src1, result, "");
	return result;
}

static LLVMValueRef emit_pack_half_2x16(struct ac_llvm_context *ctx,
					LLVMValueRef src0)
{
	LLVMValueRef comp[2];

	src0 = ac_to_float(ctx, src0);
	comp[0] = LLVMBuildExtractElement(ctx->builder, src0, ctx->i32_0, "");
	comp[1] = LLVMBuildExtractElement(ctx->builder, src0, ctx->i32_1, "");

	return ac_build_cvt_pkrtz_f16(ctx, comp);
}

static LLVMValueRef emit_unpack_half_2x16(struct ac_llvm_context *ctx,
					  LLVMValueRef src0)
{
	LLVMValueRef const16 = LLVMConstInt(ctx->i32, 16, false);
	LLVMValueRef temps[2], result, val;
	int i;

	for (i = 0; i < 2; i++) {
		val = i == 1 ? LLVMBuildLShr(ctx->builder, src0, const16, "") : src0;
		val = LLVMBuildTrunc(ctx->builder, val, ctx->i16, "");
		val = LLVMBuildBitCast(ctx->builder, val, ctx->f16, "");
		temps[i] = LLVMBuildFPExt(ctx->builder, val, ctx->f32, "");
	}

	result = LLVMBuildInsertElement(ctx->builder, LLVMGetUndef(ctx->v2f32), temps[0],
					ctx->i32_0, "");
	result = LLVMBuildInsertElement(ctx->builder, result, temps[1],
					ctx->i32_1, "");
	return result;
}

static LLVMValueRef emit_ddxy(struct ac_nir_context *ctx,
			      nir_op op,
			      LLVMValueRef src0)
{
	unsigned mask;
	int idx;
	LLVMValueRef result;

	if (op == nir_op_fddx_fine || op == nir_op_fddx)
		mask = AC_TID_MASK_LEFT;
	else if (op == nir_op_fddy_fine || op == nir_op_fddy)
		mask = AC_TID_MASK_TOP;
	else
		mask = AC_TID_MASK_TOP_LEFT;

	/* for DDX we want to next X pixel, DDY next Y pixel. */
	if (op == nir_op_fddx_fine ||
	    op == nir_op_fddx_coarse ||
	    op == nir_op_fddx)
		idx = 1;
	else
		idx = 2;

	result = ac_build_ddxy(&ctx->ac, mask, idx, src0);
	return result;
}

/*
 * this takes an I,J coordinate pair,
 * and works out the X and Y derivatives.
 * it returns DDX(I), DDX(J), DDY(I), DDY(J).
 */
static LLVMValueRef emit_ddxy_interp(
	struct ac_nir_context *ctx,
	LLVMValueRef interp_ij)
{
	LLVMValueRef result[4], a;
	unsigned i;

	for (i = 0; i < 2; i++) {
		a = LLVMBuildExtractElement(ctx->ac.builder, interp_ij,
					    LLVMConstInt(ctx->ac.i32, i, false), "");
		result[i] = emit_ddxy(ctx, nir_op_fddx, a);
		result[2+i] = emit_ddxy(ctx, nir_op_fddy, a);
	}
	return ac_build_gather_values(&ctx->ac, result, 4);
}

static void visit_alu(struct ac_nir_context *ctx, const nir_alu_instr *instr)
{
	LLVMValueRef src[4], result = NULL;
	unsigned num_components = instr->dest.dest.ssa.num_components;
	unsigned src_components;
	LLVMTypeRef def_type = get_def_type(ctx, &instr->dest.dest.ssa);

	assert(nir_op_infos[instr->op].num_inputs <= ARRAY_SIZE(src));
	switch (instr->op) {
	case nir_op_vec2:
	case nir_op_vec3:
	case nir_op_vec4:
		src_components = 1;
		break;
	case nir_op_pack_half_2x16:
		src_components = 2;
		break;
	case nir_op_unpack_half_2x16:
		src_components = 1;
		break;
	default:
		src_components = num_components;
		break;
	}
	for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++)
		src[i] = get_alu_src(ctx, instr->src[i], src_components);

	switch (instr->op) {
	case nir_op_fmov:
	case nir_op_imov:
		result = src[0];
		break;
	case nir_op_fneg:
	        src[0] = ac_to_float(&ctx->ac, src[0]);
		result = LLVMBuildFNeg(ctx->ac.builder, src[0], "");
		break;
	case nir_op_ineg:
		result = LLVMBuildNeg(ctx->ac.builder, src[0], "");
		break;
	case nir_op_inot:
		result = LLVMBuildNot(ctx->ac.builder, src[0], "");
		break;
	case nir_op_iadd:
		result = LLVMBuildAdd(ctx->ac.builder, src[0], src[1], "");
		break;
	case nir_op_fadd:
		src[0] = ac_to_float(&ctx->ac, src[0]);
		src[1] = ac_to_float(&ctx->ac, src[1]);
		result = LLVMBuildFAdd(ctx->ac.builder, src[0], src[1], "");
		break;
	case nir_op_fsub:
		src[0] = ac_to_float(&ctx->ac, src[0]);
		src[1] = ac_to_float(&ctx->ac, src[1]);
		result = LLVMBuildFSub(ctx->ac.builder, src[0], src[1], "");
		break;
	case nir_op_isub:
		result = LLVMBuildSub(ctx->ac.builder, src[0], src[1], "");
		break;
	case nir_op_imul:
		result = LLVMBuildMul(ctx->ac.builder, src[0], src[1], "");
		break;
	case nir_op_imod:
		result = LLVMBuildSRem(ctx->ac.builder, src[0], src[1], "");
		break;
	case nir_op_umod:
		result = LLVMBuildURem(ctx->ac.builder, src[0], src[1], "");
		break;
	case nir_op_fmod:
		src[0] = ac_to_float(&ctx->ac, src[0]);
		src[1] = ac_to_float(&ctx->ac, src[1]);
		result = ac_build_fdiv(&ctx->ac, src[0], src[1]);
		result = emit_intrin_1f_param(&ctx->ac, "llvm.floor",
		                              ac_to_float_type(&ctx->ac, def_type), result);
		result = LLVMBuildFMul(ctx->ac.builder, src[1] , result, "");
		result = LLVMBuildFSub(ctx->ac.builder, src[0], result, "");
		break;
	case nir_op_frem:
		src[0] = ac_to_float(&ctx->ac, src[0]);
		src[1] = ac_to_float(&ctx->ac, src[1]);
		result = LLVMBuildFRem(ctx->ac.builder, src[0], src[1], "");
		break;
	case nir_op_irem:
		result = LLVMBuildSRem(ctx->ac.builder, src[0], src[1], "");
		break;
	case nir_op_idiv:
		result = LLVMBuildSDiv(ctx->ac.builder, src[0], src[1], "");
		break;
	case nir_op_udiv:
		result = LLVMBuildUDiv(ctx->ac.builder, src[0], src[1], "");
		break;
	case nir_op_fmul:
		src[0] = ac_to_float(&ctx->ac, src[0]);
		src[1] = ac_to_float(&ctx->ac, src[1]);
		result = LLVMBuildFMul(ctx->ac.builder, src[0], src[1], "");
		break;
	case nir_op_fdiv:
		src[0] = ac_to_float(&ctx->ac, src[0]);
		src[1] = ac_to_float(&ctx->ac, src[1]);
		result = ac_build_fdiv(&ctx->ac, src[0], src[1]);
		break;
	case nir_op_frcp:
		src[0] = ac_to_float(&ctx->ac, src[0]);
		result = ac_build_fdiv(&ctx->ac, instr->dest.dest.ssa.bit_size == 32 ? ctx->ac.f32_1 : ctx->ac.f64_1,
				       src[0]);
		break;
	case nir_op_iand:
		result = LLVMBuildAnd(ctx->ac.builder, src[0], src[1], "");
		break;
	case nir_op_ior:
		result = LLVMBuildOr(ctx->ac.builder, src[0], src[1], "");
		break;
	case nir_op_ixor:
		result = LLVMBuildXor(ctx->ac.builder, src[0], src[1], "");
		break;
	case nir_op_ishl:
		result = LLVMBuildShl(ctx->ac.builder, src[0],
				      LLVMBuildZExt(ctx->ac.builder, src[1],
						    LLVMTypeOf(src[0]), ""),
				      "");
		break;
	case nir_op_ishr:
		result = LLVMBuildAShr(ctx->ac.builder, src[0],
				       LLVMBuildZExt(ctx->ac.builder, src[1],
						     LLVMTypeOf(src[0]), ""),
				       "");
		break;
	case nir_op_ushr:
		result = LLVMBuildLShr(ctx->ac.builder, src[0],
				       LLVMBuildZExt(ctx->ac.builder, src[1],
						     LLVMTypeOf(src[0]), ""),
				       "");
		break;
	case nir_op_ilt:
		result = emit_int_cmp(&ctx->ac, LLVMIntSLT, src[0], src[1]);
		break;
	case nir_op_ine:
		result = emit_int_cmp(&ctx->ac, LLVMIntNE, src[0], src[1]);
		break;
	case nir_op_ieq:
		result = emit_int_cmp(&ctx->ac, LLVMIntEQ, src[0], src[1]);
		break;
	case nir_op_ige:
		result = emit_int_cmp(&ctx->ac, LLVMIntSGE, src[0], src[1]);
		break;
	case nir_op_ult:
		result = emit_int_cmp(&ctx->ac, LLVMIntULT, src[0], src[1]);
		break;
	case nir_op_uge:
		result = emit_int_cmp(&ctx->ac, LLVMIntUGE, src[0], src[1]);
		break;
	case nir_op_feq:
		result = emit_float_cmp(&ctx->ac, LLVMRealUEQ, src[0], src[1]);
		break;
	case nir_op_fne:
		result = emit_float_cmp(&ctx->ac, LLVMRealUNE, src[0], src[1]);
		break;
	case nir_op_flt:
		result = emit_float_cmp(&ctx->ac, LLVMRealULT, src[0], src[1]);
		break;
	case nir_op_fge:
		result = emit_float_cmp(&ctx->ac, LLVMRealUGE, src[0], src[1]);
		break;
	case nir_op_fabs:
		result = emit_intrin_1f_param(&ctx->ac, "llvm.fabs",
		                              ac_to_float_type(&ctx->ac, def_type), src[0]);
		break;
	case nir_op_iabs:
		result = emit_iabs(&ctx->ac, src[0]);
		break;
	case nir_op_imax:
		result = emit_minmax_int(&ctx->ac, LLVMIntSGT, src[0], src[1]);
		break;
	case nir_op_imin:
		result = emit_minmax_int(&ctx->ac, LLVMIntSLT, src[0], src[1]);
		break;
	case nir_op_umax:
		result = emit_minmax_int(&ctx->ac, LLVMIntUGT, src[0], src[1]);
		break;
	case nir_op_umin:
		result = emit_minmax_int(&ctx->ac, LLVMIntULT, src[0], src[1]);
		break;
	case nir_op_isign:
		result = emit_isign(&ctx->ac, src[0], instr->dest.dest.ssa.bit_size);
		break;
	case nir_op_fsign:
		src[0] = ac_to_float(&ctx->ac, src[0]);
		result = emit_fsign(&ctx->ac, src[0], instr->dest.dest.ssa.bit_size);
		break;
	case nir_op_ffloor:
		result = emit_intrin_1f_param(&ctx->ac, "llvm.floor",
		                              ac_to_float_type(&ctx->ac, def_type), src[0]);
		break;
	case nir_op_ftrunc:
		result = emit_intrin_1f_param(&ctx->ac, "llvm.trunc",
		                              ac_to_float_type(&ctx->ac, def_type), src[0]);
		break;
	case nir_op_fceil:
		result = emit_intrin_1f_param(&ctx->ac, "llvm.ceil",
		                              ac_to_float_type(&ctx->ac, def_type), src[0]);
		break;
	case nir_op_fround_even:
		result = emit_intrin_1f_param(&ctx->ac, "llvm.rint",
		                              ac_to_float_type(&ctx->ac, def_type),src[0]);
		break;
	case nir_op_ffract:
		result = emit_ffract(&ctx->ac, src[0], instr->dest.dest.ssa.bit_size);
		break;
	case nir_op_fsin:
		result = emit_intrin_1f_param(&ctx->ac, "llvm.sin",
		                              ac_to_float_type(&ctx->ac, def_type), src[0]);
		break;
	case nir_op_fcos:
		result = emit_intrin_1f_param(&ctx->ac, "llvm.cos",
		                              ac_to_float_type(&ctx->ac, def_type), src[0]);
		break;
	case nir_op_fsqrt:
		result = emit_intrin_1f_param(&ctx->ac, "llvm.sqrt",
		                              ac_to_float_type(&ctx->ac, def_type), src[0]);
		break;
	case nir_op_fexp2:
		result = emit_intrin_1f_param(&ctx->ac, "llvm.exp2",
		                              ac_to_float_type(&ctx->ac, def_type), src[0]);
		break;
	case nir_op_flog2:
		result = emit_intrin_1f_param(&ctx->ac, "llvm.log2",
		                              ac_to_float_type(&ctx->ac, def_type), src[0]);
		break;
	case nir_op_frsq:
		result = emit_intrin_1f_param(&ctx->ac, "llvm.sqrt",
		                              ac_to_float_type(&ctx->ac, def_type), src[0]);
		result = ac_build_fdiv(&ctx->ac, instr->dest.dest.ssa.bit_size == 32 ? ctx->ac.f32_1 : ctx->ac.f64_1,
				       result);
		break;
	case nir_op_fpow:
		result = emit_intrin_2f_param(&ctx->ac, "llvm.pow",
		                              ac_to_float_type(&ctx->ac, def_type), src[0], src[1]);
		break;
	case nir_op_fmax:
		result = emit_intrin_2f_param(&ctx->ac, "llvm.maxnum",
		                              ac_to_float_type(&ctx->ac, def_type), src[0], src[1]);
		if (ctx->ac.chip_class < GFX9 &&
		    instr->dest.dest.ssa.bit_size == 32) {
			/* Only pre-GFX9 chips do not flush denorms. */
			result = emit_intrin_1f_param(&ctx->ac, "llvm.canonicalize",
						      ac_to_float_type(&ctx->ac, def_type),
						      result);
		}
		break;
	case nir_op_fmin:
		result = emit_intrin_2f_param(&ctx->ac, "llvm.minnum",
		                              ac_to_float_type(&ctx->ac, def_type), src[0], src[1]);
		if (ctx->ac.chip_class < GFX9 &&
		    instr->dest.dest.ssa.bit_size == 32) {
			/* Only pre-GFX9 chips do not flush denorms. */
			result = emit_intrin_1f_param(&ctx->ac, "llvm.canonicalize",
						      ac_to_float_type(&ctx->ac, def_type),
						      result);
		}
		break;
	case nir_op_ffma:
		result = emit_intrin_3f_param(&ctx->ac, "llvm.fmuladd",
		                              ac_to_float_type(&ctx->ac, def_type), src[0], src[1], src[2]);
		break;
	case nir_op_ibitfield_extract:
		result = emit_bitfield_extract(&ctx->ac, true, src);
		break;
	case nir_op_ubitfield_extract:
		result = emit_bitfield_extract(&ctx->ac, false, src);
		break;
	case nir_op_bitfield_insert:
		result = emit_bitfield_insert(&ctx->ac, src[0], src[1], src[2], src[3]);
		break;
	case nir_op_bitfield_reverse:
		result = ac_build_intrinsic(&ctx->ac, "llvm.bitreverse.i32", ctx->ac.i32, src, 1, AC_FUNC_ATTR_READNONE);
		break;
	case nir_op_bit_count:
		result = ac_build_intrinsic(&ctx->ac, "llvm.ctpop.i32", ctx->ac.i32, src, 1, AC_FUNC_ATTR_READNONE);
		break;
	case nir_op_vec2:
	case nir_op_vec3:
	case nir_op_vec4:
		for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++)
			src[i] = ac_to_integer(&ctx->ac, src[i]);
		result = ac_build_gather_values(&ctx->ac, src, num_components);
		break;
	case nir_op_f2i32:
	case nir_op_f2i64:
		src[0] = ac_to_float(&ctx->ac, src[0]);
		result = LLVMBuildFPToSI(ctx->ac.builder, src[0], def_type, "");
		break;
	case nir_op_f2u32:
	case nir_op_f2u64:
		src[0] = ac_to_float(&ctx->ac, src[0]);
		result = LLVMBuildFPToUI(ctx->ac.builder, src[0], def_type, "");
		break;
	case nir_op_i2f32:
	case nir_op_i2f64:
		src[0] = ac_to_integer(&ctx->ac, src[0]);
		result = LLVMBuildSIToFP(ctx->ac.builder, src[0], ac_to_float_type(&ctx->ac, def_type), "");
		break;
	case nir_op_u2f32:
	case nir_op_u2f64:
		src[0] = ac_to_integer(&ctx->ac, src[0]);
		result = LLVMBuildUIToFP(ctx->ac.builder, src[0], ac_to_float_type(&ctx->ac, def_type), "");
		break;
	case nir_op_f2f64:
		src[0] = ac_to_float(&ctx->ac, src[0]);
		result = LLVMBuildFPExt(ctx->ac.builder, src[0], ac_to_float_type(&ctx->ac, def_type), "");
		break;
	case nir_op_f2f32:
		result = LLVMBuildFPTrunc(ctx->ac.builder, src[0], ac_to_float_type(&ctx->ac, def_type), "");
		break;
	case nir_op_u2u32:
	case nir_op_u2u64:
		src[0] = ac_to_integer(&ctx->ac, src[0]);
		if (get_elem_bits(&ctx->ac, LLVMTypeOf(src[0])) < get_elem_bits(&ctx->ac, def_type))
			result = LLVMBuildZExt(ctx->ac.builder, src[0], def_type, "");
		else
			result = LLVMBuildTrunc(ctx->ac.builder, src[0], def_type, "");
		break;
	case nir_op_i2i32:
	case nir_op_i2i64:
		src[0] = ac_to_integer(&ctx->ac, src[0]);
		if (get_elem_bits(&ctx->ac, LLVMTypeOf(src[0])) < get_elem_bits(&ctx->ac, def_type))
			result = LLVMBuildSExt(ctx->ac.builder, src[0], def_type, "");
		else
			result = LLVMBuildTrunc(ctx->ac.builder, src[0], def_type, "");
		break;
	case nir_op_bcsel:
		result = emit_bcsel(&ctx->ac, src[0], src[1], src[2]);
		break;
	case nir_op_find_lsb:
		src[0] = ac_to_integer(&ctx->ac, src[0]);
		result = ac_find_lsb(&ctx->ac, ctx->ac.i32, src[0]);
		break;
	case nir_op_ufind_msb:
		src[0] = ac_to_integer(&ctx->ac, src[0]);
		result = ac_build_umsb(&ctx->ac, src[0], ctx->ac.i32);
		break;
	case nir_op_ifind_msb:
		src[0] = ac_to_integer(&ctx->ac, src[0]);
		result = ac_build_imsb(&ctx->ac, src[0], ctx->ac.i32);
		break;
	case nir_op_uadd_carry:
		src[0] = ac_to_integer(&ctx->ac, src[0]);
		src[1] = ac_to_integer(&ctx->ac, src[1]);
		result = emit_uint_carry(&ctx->ac, "llvm.uadd.with.overflow.i32", src[0], src[1]);
		break;
	case nir_op_usub_borrow:
		src[0] = ac_to_integer(&ctx->ac, src[0]);
		src[1] = ac_to_integer(&ctx->ac, src[1]);
		result = emit_uint_carry(&ctx->ac, "llvm.usub.with.overflow.i32", src[0], src[1]);
		break;
	case nir_op_b2f:
		result = emit_b2f(&ctx->ac, src[0]);
		break;
	case nir_op_f2b:
		result = emit_f2b(&ctx->ac, src[0]);
		break;
	case nir_op_b2i:
		result = emit_b2i(&ctx->ac, src[0], instr->dest.dest.ssa.bit_size);
		break;
	case nir_op_i2b:
		src[0] = ac_to_integer(&ctx->ac, src[0]);
		result = emit_i2b(&ctx->ac, src[0]);
		break;
	case nir_op_fquantize2f16:
		result = emit_f2f16(ctx->nctx, src[0]);
		break;
	case nir_op_umul_high:
		src[0] = ac_to_integer(&ctx->ac, src[0]);
		src[1] = ac_to_integer(&ctx->ac, src[1]);
		result = emit_umul_high(&ctx->ac, src[0], src[1]);
		break;
	case nir_op_imul_high:
		src[0] = ac_to_integer(&ctx->ac, src[0]);
		src[1] = ac_to_integer(&ctx->ac, src[1]);
		result = emit_imul_high(&ctx->ac, src[0], src[1]);
		break;
	case nir_op_pack_half_2x16:
		result = emit_pack_half_2x16(&ctx->ac, src[0]);
		break;
	case nir_op_unpack_half_2x16:
		result = emit_unpack_half_2x16(&ctx->ac, src[0]);
		break;
	case nir_op_fddx:
	case nir_op_fddy:
	case nir_op_fddx_fine:
	case nir_op_fddy_fine:
	case nir_op_fddx_coarse:
	case nir_op_fddy_coarse:
		result = emit_ddxy(ctx, instr->op, src[0]);
		break;

	case nir_op_unpack_64_2x32_split_x: {
		assert(instr->src[0].src.ssa->num_components == 1);
		LLVMValueRef tmp = LLVMBuildBitCast(ctx->ac.builder, src[0],
						    ctx->ac.v2i32,
						    "");
		result = LLVMBuildExtractElement(ctx->ac.builder, tmp,
						 ctx->ac.i32_0, "");
		break;
	}

	case nir_op_unpack_64_2x32_split_y: {
		assert(instr->src[0].src.ssa->num_components == 1);
		LLVMValueRef tmp = LLVMBuildBitCast(ctx->ac.builder, src[0],
						    ctx->ac.v2i32,
						    "");
		result = LLVMBuildExtractElement(ctx->ac.builder, tmp,
						 ctx->ac.i32_1, "");
		break;
	}

	case nir_op_pack_64_2x32_split: {
		LLVMValueRef tmp = LLVMGetUndef(ctx->ac.v2i32);
		tmp = LLVMBuildInsertElement(ctx->ac.builder, tmp,
					     src[0], ctx->ac.i32_0, "");
		tmp = LLVMBuildInsertElement(ctx->ac.builder, tmp,
					     src[1], ctx->ac.i32_1, "");
		result = LLVMBuildBitCast(ctx->ac.builder, tmp, ctx->ac.i64, "");
		break;
	}

	default:
		fprintf(stderr, "Unknown NIR alu instr: ");
		nir_print_instr(&instr->instr, stderr);
		fprintf(stderr, "\n");
		abort();
	}

	if (result) {
		assert(instr->dest.dest.is_ssa);
		result = ac_to_integer(&ctx->ac, result);
		_mesa_hash_table_insert(ctx->defs, &instr->dest.dest.ssa,
		                        result);
	}
}

static void visit_load_const(struct ac_nir_context *ctx,
                             const nir_load_const_instr *instr)
{
	LLVMValueRef values[4], value = NULL;
	LLVMTypeRef element_type =
	    LLVMIntTypeInContext(ctx->ac.context, instr->def.bit_size);

	for (unsigned i = 0; i < instr->def.num_components; ++i) {
		switch (instr->def.bit_size) {
		case 32:
			values[i] = LLVMConstInt(element_type,
			                         instr->value.u32[i], false);
			break;
		case 64:
			values[i] = LLVMConstInt(element_type,
			                         instr->value.u64[i], false);
			break;
		default:
			fprintf(stderr,
			        "unsupported nir load_const bit_size: %d\n",
			        instr->def.bit_size);
			abort();
		}
	}
	if (instr->def.num_components > 1) {
		value = LLVMConstVector(values, instr->def.num_components);
	} else
		value = values[0];

	_mesa_hash_table_insert(ctx->defs, &instr->def, value);
}

static LLVMValueRef cast_ptr(struct nir_to_llvm_context *ctx, LLVMValueRef ptr,
                             LLVMTypeRef type)
{
	int addr_space = LLVMGetPointerAddressSpace(LLVMTypeOf(ptr));
	return LLVMBuildBitCast(ctx->builder, ptr,
	                        LLVMPointerType(type, addr_space), "");
}

static LLVMValueRef
get_buffer_size(struct ac_nir_context *ctx, LLVMValueRef descriptor, bool in_elements)
{
	LLVMValueRef size =
		LLVMBuildExtractElement(ctx->ac.builder, descriptor,
					LLVMConstInt(ctx->ac.i32, 2, false), "");

	/* VI only */
	if (ctx->ac.chip_class == VI && in_elements) {
		/* On VI, the descriptor contains the size in bytes,
		 * but TXQ must return the size in elements.
		 * The stride is always non-zero for resources using TXQ.
		 */
		LLVMValueRef stride =
			LLVMBuildExtractElement(ctx->ac.builder, descriptor,
						ctx->ac.i32_1, "");
		stride = LLVMBuildLShr(ctx->ac.builder, stride,
				       LLVMConstInt(ctx->ac.i32, 16, false), "");
		stride = LLVMBuildAnd(ctx->ac.builder, stride,
				      LLVMConstInt(ctx->ac.i32, 0x3fff, false), "");

		size = LLVMBuildUDiv(ctx->ac.builder, size, stride, "");
	}
	return size;
}

/**
 * Given the i32 or vNi32 \p type, generate the textual name (e.g. for use with
 * intrinsic names).
 */
static void build_int_type_name(
	LLVMTypeRef type,
	char *buf, unsigned bufsize)
{
	assert(bufsize >= 6);

	if (LLVMGetTypeKind(type) == LLVMVectorTypeKind)
		snprintf(buf, bufsize, "v%ui32",
			 LLVMGetVectorSize(type));
	else
		strcpy(buf, "i32");
}

static LLVMValueRef radv_lower_gather4_integer(struct ac_llvm_context *ctx,
					       struct ac_image_args *args,
					       const nir_tex_instr *instr)
{
	enum glsl_base_type stype = glsl_get_sampler_result_type(instr->texture->var->type);
	LLVMValueRef coord = args->addr;
	LLVMValueRef half_texel[2];
	LLVMValueRef compare_cube_wa = NULL;
	LLVMValueRef result;
	int c;
	unsigned coord_vgpr_index = (unsigned)args->offset + (unsigned)args->compare;

	//TODO Rect
	{
		struct ac_image_args txq_args = { 0 };

		txq_args.da = instr->is_array || instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE;
		txq_args.opcode = ac_image_get_resinfo;
		txq_args.dmask = 0xf;
		txq_args.addr = ctx->i32_0;
		txq_args.resource = args->resource;
		LLVMValueRef size = ac_build_image_opcode(ctx, &txq_args);

		for (c = 0; c < 2; c++) {
			half_texel[c] = LLVMBuildExtractElement(ctx->builder, size,
								LLVMConstInt(ctx->i32, c, false), "");
			half_texel[c] = LLVMBuildUIToFP(ctx->builder, half_texel[c], ctx->f32, "");
			half_texel[c] = ac_build_fdiv(ctx, ctx->f32_1, half_texel[c]);
			half_texel[c] = LLVMBuildFMul(ctx->builder, half_texel[c],
						      LLVMConstReal(ctx->f32, -0.5), "");
		}
	}

	LLVMValueRef orig_coords = args->addr;

	for (c = 0; c < 2; c++) {
		LLVMValueRef tmp;
		LLVMValueRef index = LLVMConstInt(ctx->i32, coord_vgpr_index + c, 0);
		tmp = LLVMBuildExtractElement(ctx->builder, coord, index, "");
		tmp = LLVMBuildBitCast(ctx->builder, tmp, ctx->f32, "");
		tmp = LLVMBuildFAdd(ctx->builder, tmp, half_texel[c], "");
		tmp = LLVMBuildBitCast(ctx->builder, tmp, ctx->i32, "");
		coord = LLVMBuildInsertElement(ctx->builder, coord, tmp, index, "");
	}


	/*
	 * Apparantly cube has issue with integer types that the workaround doesn't solve,
	 * so this tests if the format is 8_8_8_8 and an integer type do an alternate
	 * workaround by sampling using a scaled type and converting.
	 * This is taken from amdgpu-pro shaders.
	 */
	/* NOTE this produces some ugly code compared to amdgpu-pro,
	 * LLVM ends up dumping SGPRs into VGPRs to deal with the compare/select,
	 * and then reads them back. -pro generates two selects,
	 * one s_cmp for the descriptor rewriting
	 * one v_cmp for the coordinate and result changes.
	 */
	if (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE) {
		LLVMValueRef tmp, tmp2;

		/* workaround 8/8/8/8 uint/sint cube gather bug */
		/* first detect it then change to a scaled read and f2i */
		tmp = LLVMBuildExtractElement(ctx->builder, args->resource, ctx->i32_1, "");
		tmp2 = tmp;

		/* extract the DATA_FORMAT */
		tmp = ac_build_bfe(ctx, tmp, LLVMConstInt(ctx->i32, 20, false),
				   LLVMConstInt(ctx->i32, 6, false), false);

		/* is the DATA_FORMAT == 8_8_8_8 */
		compare_cube_wa = LLVMBuildICmp(ctx->builder, LLVMIntEQ, tmp, LLVMConstInt(ctx->i32, V_008F14_IMG_DATA_FORMAT_8_8_8_8, false), "");

		if (stype == GLSL_TYPE_UINT)
			/* Create a NUM FORMAT - 0x2 or 0x4 - USCALED or UINT */
			tmp = LLVMBuildSelect(ctx->builder, compare_cube_wa, LLVMConstInt(ctx->i32, 0x8000000, false),
					      LLVMConstInt(ctx->i32, 0x10000000, false), "");
		else
			/* Create a NUM FORMAT - 0x3 or 0x5 - SSCALED or SINT */
			tmp = LLVMBuildSelect(ctx->builder, compare_cube_wa, LLVMConstInt(ctx->i32, 0xc000000, false),
					      LLVMConstInt(ctx->i32, 0x14000000, false), "");

		/* replace the NUM FORMAT in the descriptor */
		tmp2 = LLVMBuildAnd(ctx->builder, tmp2, LLVMConstInt(ctx->i32, C_008F14_NUM_FORMAT_GFX6, false), "");
		tmp2 = LLVMBuildOr(ctx->builder, tmp2, tmp, "");

		args->resource = LLVMBuildInsertElement(ctx->builder, args->resource, tmp2, ctx->i32_1, "");

		/* don't modify the coordinates for this case */
		coord = LLVMBuildSelect(ctx->builder, compare_cube_wa, orig_coords, coord, "");
	}
	args->addr = coord;
	result = ac_build_image_opcode(ctx, args);

	if (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE) {
		LLVMValueRef tmp, tmp2;

		/* if the cube workaround is in place, f2i the result. */
		for (c = 0; c < 4; c++) {
			tmp = LLVMBuildExtractElement(ctx->builder, result, LLVMConstInt(ctx->i32, c, false), "");
			if (stype == GLSL_TYPE_UINT)
				tmp2 = LLVMBuildFPToUI(ctx->builder, tmp, ctx->i32, "");
			else
				tmp2 = LLVMBuildFPToSI(ctx->builder, tmp, ctx->i32, "");
			tmp = LLVMBuildBitCast(ctx->builder, tmp, ctx->i32, "");
			tmp2 = LLVMBuildBitCast(ctx->builder, tmp2, ctx->i32, "");
			tmp = LLVMBuildSelect(ctx->builder, compare_cube_wa, tmp2, tmp, "");
			tmp = LLVMBuildBitCast(ctx->builder, tmp, ctx->f32, "");
			result = LLVMBuildInsertElement(ctx->builder, result, tmp, LLVMConstInt(ctx->i32, c, false), "");
		}
	}
	return result;
}

static LLVMValueRef build_tex_intrinsic(struct ac_nir_context *ctx,
					const nir_tex_instr *instr,
					bool lod_is_zero,
					struct ac_image_args *args)
{
	if (instr->sampler_dim == GLSL_SAMPLER_DIM_BUF) {
		unsigned mask = nir_ssa_def_components_read(&instr->dest.ssa);

		return ac_build_buffer_load_format(&ctx->ac,
						   args->resource,
						   args->addr,
						   ctx->ac.i32_0,
						   util_last_bit(mask),
						   false, true);
	}

	args->opcode = ac_image_sample;
	args->compare = instr->is_shadow;

	switch (instr->op) {
	case nir_texop_txf:
	case nir_texop_txf_ms:
	case nir_texop_samples_identical:
		args->opcode = lod_is_zero ||
			       instr->sampler_dim == GLSL_SAMPLER_DIM_MS ?
					ac_image_load : ac_image_load_mip;
		args->compare = false;
		args->offset = false;
		break;
	case nir_texop_txb:
		args->bias = true;
		break;
	case nir_texop_txl:
		if (lod_is_zero)
			args->level_zero = true;
		else
			args->lod = true;
		break;
	case nir_texop_txs:
	case nir_texop_query_levels:
		args->opcode = ac_image_get_resinfo;
		break;
	case nir_texop_tex:
		if (ctx->stage != MESA_SHADER_FRAGMENT)
			args->level_zero = true;
		break;
	case nir_texop_txd:
		args->deriv = true;
		break;
	case nir_texop_tg4:
		args->opcode = ac_image_gather4;
		args->level_zero = true;
		break;
	case nir_texop_lod:
		args->opcode = ac_image_get_lod;
		args->compare = false;
		args->offset = false;
		break;
	default:
		break;
	}

	if (instr->op == nir_texop_tg4 && ctx->ac.chip_class <= VI) {
		enum glsl_base_type stype = glsl_get_sampler_result_type(instr->texture->var->type);
		if (stype == GLSL_TYPE_UINT || stype == GLSL_TYPE_INT) {
			return radv_lower_gather4_integer(&ctx->ac, args, instr);
		}
	}
	return ac_build_image_opcode(&ctx->ac, args);
}

static LLVMValueRef visit_vulkan_resource_index(struct nir_to_llvm_context *ctx,
                                                nir_intrinsic_instr *instr)
{
	LLVMValueRef index = get_src(ctx->nir, instr->src[0]);
	unsigned desc_set = nir_intrinsic_desc_set(instr);
	unsigned binding = nir_intrinsic_binding(instr);
	LLVMValueRef desc_ptr = ctx->descriptor_sets[desc_set];
	struct radv_pipeline_layout *pipeline_layout = ctx->options->layout;
	struct radv_descriptor_set_layout *layout = pipeline_layout->set[desc_set].layout;
	unsigned base_offset = layout->binding[binding].offset;
	LLVMValueRef offset, stride;

	if (layout->binding[binding].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
	    layout->binding[binding].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
		unsigned idx = pipeline_layout->set[desc_set].dynamic_offset_start +
			layout->binding[binding].dynamic_offset_offset;
		desc_ptr = ctx->push_constants;
		base_offset = pipeline_layout->push_constant_size + 16 * idx;
		stride = LLVMConstInt(ctx->ac.i32, 16, false);
	} else
		stride = LLVMConstInt(ctx->ac.i32, layout->binding[binding].size, false);

	offset = LLVMConstInt(ctx->ac.i32, base_offset, false);
	index = LLVMBuildMul(ctx->builder, index, stride, "");
	offset = LLVMBuildAdd(ctx->builder, offset, index, "");
	
	desc_ptr = ac_build_gep0(&ctx->ac, desc_ptr, offset);
	desc_ptr = cast_ptr(ctx, desc_ptr, ctx->ac.v4i32);
	LLVMSetMetadata(desc_ptr, ctx->ac.uniform_md_kind, ctx->ac.empty_md);

	return desc_ptr;
}

static LLVMValueRef visit_vulkan_resource_reindex(struct nir_to_llvm_context *ctx,
                                                  nir_intrinsic_instr *instr)
{
	LLVMValueRef ptr = get_src(ctx->nir, instr->src[0]);
	LLVMValueRef index = get_src(ctx->nir, instr->src[1]);

	LLVMValueRef result = LLVMBuildGEP(ctx->builder, ptr, &index, 1, "");
	LLVMSetMetadata(result, ctx->ac.uniform_md_kind, ctx->ac.empty_md);
	return result;
}

static LLVMValueRef visit_load_push_constant(struct nir_to_llvm_context *ctx,
                                             nir_intrinsic_instr *instr)
{
	LLVMValueRef ptr, addr;

	addr = LLVMConstInt(ctx->ac.i32, nir_intrinsic_base(instr), 0);
	addr = LLVMBuildAdd(ctx->builder, addr, get_src(ctx->nir, instr->src[0]), "");

	ptr = ac_build_gep0(&ctx->ac, ctx->push_constants, addr);
	ptr = cast_ptr(ctx, ptr, get_def_type(ctx->nir, &instr->dest.ssa));

	return LLVMBuildLoad(ctx->builder, ptr, "");
}

static LLVMValueRef visit_get_buffer_size(struct ac_nir_context *ctx,
                                          const nir_intrinsic_instr *instr)
{
	LLVMValueRef index = get_src(ctx, instr->src[0]);

	return get_buffer_size(ctx, ctx->abi->load_ssbo(ctx->abi, index, false), false);
}

static uint32_t widen_mask(uint32_t mask, unsigned multiplier)
{
	uint32_t new_mask = 0;
	for(unsigned i = 0; i < 32 && (1u << i) <= mask; ++i)
		if (mask & (1u << i))
			new_mask |= ((1u << multiplier) - 1u) << (i * multiplier);
	return new_mask;
}

static LLVMValueRef extract_vector_range(struct ac_llvm_context *ctx, LLVMValueRef src,
                                         unsigned start, unsigned count)
{
	LLVMTypeRef type = LLVMTypeOf(src);

	if (LLVMGetTypeKind(type) != LLVMVectorTypeKind) {
		assert(start == 0);
		assert(count == 1);
		return src;
	}

	unsigned src_elements = LLVMGetVectorSize(type);
	assert(start < src_elements);
	assert(start + count <= src_elements);

	if (start == 0 && count == src_elements)
		return src;

	if (count == 1)
		return LLVMBuildExtractElement(ctx->builder, src, LLVMConstInt(ctx->i32, start, false), "");

	assert(count <= 8);
	LLVMValueRef indices[8];
	for (unsigned i = 0; i < count; ++i)
		indices[i] = LLVMConstInt(ctx->i32, start + i, false);

	LLVMValueRef swizzle = LLVMConstVector(indices, count);
	return LLVMBuildShuffleVector(ctx->builder, src, src, swizzle, "");
}

static void visit_store_ssbo(struct ac_nir_context *ctx,
                             nir_intrinsic_instr *instr)
{
	const char *store_name;
	LLVMValueRef src_data = get_src(ctx, instr->src[0]);
	LLVMTypeRef data_type = ctx->ac.f32;
	int elem_size_mult = get_elem_bits(&ctx->ac, LLVMTypeOf(src_data)) / 32;
	int components_32bit = elem_size_mult * instr->num_components;
	unsigned writemask = nir_intrinsic_write_mask(instr);
	LLVMValueRef base_data, base_offset;
	LLVMValueRef params[6];

	params[1] = ctx->abi->load_ssbo(ctx->abi,
				        get_src(ctx, instr->src[1]), true);
	params[2] = ctx->ac.i32_0; /* vindex */
	params[4] = ctx->ac.i1false;  /* glc */
	params[5] = ctx->ac.i1false;  /* slc */

	if (components_32bit > 1)
		data_type = LLVMVectorType(ctx->ac.f32, components_32bit);

	writemask = widen_mask(writemask, elem_size_mult);

	base_data = ac_to_float(&ctx->ac, src_data);
	base_data = trim_vector(&ctx->ac, base_data, instr->num_components);
	base_data = LLVMBuildBitCast(ctx->ac.builder, base_data,
				     data_type, "");
	base_offset = get_src(ctx, instr->src[2]);      /* voffset */
	while (writemask) {
		int start, count;
		LLVMValueRef data;
		LLVMValueRef offset;

		u_bit_scan_consecutive_range(&writemask, &start, &count);

		/* Due to an LLVM limitation, split 3-element writes
		 * into a 2-element and a 1-element write. */
		if (count == 3) {
			writemask |= 1 << (start + 2);
			count = 2;
		}

		if (count > 4) {
			writemask |= ((1u << (count - 4)) - 1u) << (start + 4);
			count = 4;
		}

		if (count == 4) {
			store_name = "llvm.amdgcn.buffer.store.v4f32";
		} else if (count == 2) {
			store_name = "llvm.amdgcn.buffer.store.v2f32";

		} else {
			assert(count == 1);
			store_name = "llvm.amdgcn.buffer.store.f32";
		}
		data = extract_vector_range(&ctx->ac, base_data, start, count);

		offset = base_offset;
		if (start != 0) {
			offset = LLVMBuildAdd(ctx->ac.builder, offset, LLVMConstInt(ctx->ac.i32, start * 4, false), "");
		}
		params[0] = data;
		params[3] = offset;
		ac_build_intrinsic(&ctx->ac, store_name,
				   ctx->ac.voidt, params, 6, 0);
	}
}

static LLVMValueRef visit_atomic_ssbo(struct ac_nir_context *ctx,
                                      const nir_intrinsic_instr *instr)
{
	const char *name;
	LLVMValueRef params[6];
	int arg_count = 0;

	if (instr->intrinsic == nir_intrinsic_ssbo_atomic_comp_swap) {
		params[arg_count++] = ac_llvm_extract_elem(&ctx->ac, get_src(ctx, instr->src[3]), 0);
	}
	params[arg_count++] = ac_llvm_extract_elem(&ctx->ac, get_src(ctx, instr->src[2]), 0);
	params[arg_count++] = ctx->abi->load_ssbo(ctx->abi,
						 get_src(ctx, instr->src[0]),
						 true);
	params[arg_count++] = ctx->ac.i32_0; /* vindex */
	params[arg_count++] = get_src(ctx, instr->src[1]);      /* voffset */
	params[arg_count++] = LLVMConstInt(ctx->ac.i1, 0, false);  /* slc */

	switch (instr->intrinsic) {
	case nir_intrinsic_ssbo_atomic_add:
		name = "llvm.amdgcn.buffer.atomic.add";
		break;
	case nir_intrinsic_ssbo_atomic_imin:
		name = "llvm.amdgcn.buffer.atomic.smin";
		break;
	case nir_intrinsic_ssbo_atomic_umin:
		name = "llvm.amdgcn.buffer.atomic.umin";
		break;
	case nir_intrinsic_ssbo_atomic_imax:
		name = "llvm.amdgcn.buffer.atomic.smax";
		break;
	case nir_intrinsic_ssbo_atomic_umax:
		name = "llvm.amdgcn.buffer.atomic.umax";
		break;
	case nir_intrinsic_ssbo_atomic_and:
		name = "llvm.amdgcn.buffer.atomic.and";
		break;
	case nir_intrinsic_ssbo_atomic_or:
		name = "llvm.amdgcn.buffer.atomic.or";
		break;
	case nir_intrinsic_ssbo_atomic_xor:
		name = "llvm.amdgcn.buffer.atomic.xor";
		break;
	case nir_intrinsic_ssbo_atomic_exchange:
		name = "llvm.amdgcn.buffer.atomic.swap";
		break;
	case nir_intrinsic_ssbo_atomic_comp_swap:
		name = "llvm.amdgcn.buffer.atomic.cmpswap";
		break;
	default:
		abort();
	}

	return ac_build_intrinsic(&ctx->ac, name, ctx->ac.i32, params, arg_count, 0);
}

static LLVMValueRef visit_load_buffer(struct ac_nir_context *ctx,
                                      const nir_intrinsic_instr *instr)
{
	LLVMValueRef results[2];
	int load_components;
	int num_components = instr->num_components;
	if (instr->dest.ssa.bit_size == 64)
		num_components *= 2;

	for (int i = 0; i < num_components; i += load_components) {
		load_components = MIN2(num_components - i, 4);
		const char *load_name;
		LLVMTypeRef data_type = ctx->ac.f32;
		LLVMValueRef offset = LLVMConstInt(ctx->ac.i32, i * 4, false);
		offset = LLVMBuildAdd(ctx->ac.builder, get_src(ctx, instr->src[1]), offset, "");

		if (load_components == 3)
			data_type = LLVMVectorType(ctx->ac.f32, 4);
		else if (load_components > 1)
			data_type = LLVMVectorType(ctx->ac.f32, load_components);

		if (load_components >= 3)
			load_name = "llvm.amdgcn.buffer.load.v4f32";
		else if (load_components == 2)
			load_name = "llvm.amdgcn.buffer.load.v2f32";
		else if (load_components == 1)
			load_name = "llvm.amdgcn.buffer.load.f32";
		else
			unreachable("unhandled number of components");

		LLVMValueRef params[] = {
			ctx->abi->load_ssbo(ctx->abi,
					    get_src(ctx, instr->src[0]),
					    false),
			ctx->ac.i32_0,
			offset,
			ctx->ac.i1false,
			ctx->ac.i1false,
		};

		results[i > 0 ? 1 : 0] = ac_build_intrinsic(&ctx->ac, load_name, data_type, params, 5, 0);
	}

	assume(results[0]);
	LLVMValueRef ret = results[0];
	if (num_components > 4 || num_components == 3) {
		LLVMValueRef masks[] = {
		        LLVMConstInt(ctx->ac.i32, 0, false), LLVMConstInt(ctx->ac.i32, 1, false),
		        LLVMConstInt(ctx->ac.i32, 2, false), LLVMConstInt(ctx->ac.i32, 3, false),
			LLVMConstInt(ctx->ac.i32, 4, false), LLVMConstInt(ctx->ac.i32, 5, false),
		        LLVMConstInt(ctx->ac.i32, 6, false), LLVMConstInt(ctx->ac.i32, 7, false)
		};

		LLVMValueRef swizzle = LLVMConstVector(masks, num_components);
		ret = LLVMBuildShuffleVector(ctx->ac.builder, results[0],
					     results[num_components > 4 ? 1 : 0], swizzle, "");
	}

	return LLVMBuildBitCast(ctx->ac.builder, ret,
	                        get_def_type(ctx, &instr->dest.ssa), "");
}

static LLVMValueRef visit_load_ubo_buffer(struct ac_nir_context *ctx,
                                          const nir_intrinsic_instr *instr)
{
	LLVMValueRef ret;
	LLVMValueRef rsrc = get_src(ctx, instr->src[0]);
	LLVMValueRef offset = get_src(ctx, instr->src[1]);
	int num_components = instr->num_components;

	if (ctx->abi->load_ubo)
		rsrc = ctx->abi->load_ubo(ctx->abi, rsrc);

	if (instr->dest.ssa.bit_size == 64)
		num_components *= 2;

	ret = ac_build_buffer_load(&ctx->ac, rsrc, num_components, NULL, offset,
				   NULL, 0, false, false, true, true);
	ret = trim_vector(&ctx->ac, ret, num_components);
	return LLVMBuildBitCast(ctx->ac.builder, ret,
	                        get_def_type(ctx, &instr->dest.ssa), "");
}

static void
get_deref_offset(struct ac_nir_context *ctx, nir_deref_var *deref,
		 bool vs_in, unsigned *vertex_index_out,
		 LLVMValueRef *vertex_index_ref,
		 unsigned *const_out, LLVMValueRef *indir_out)
{
	unsigned const_offset = 0;
	nir_deref *tail = &deref->deref;
	LLVMValueRef offset = NULL;

	if (vertex_index_out != NULL || vertex_index_ref != NULL) {
		tail = tail->child;
		nir_deref_array *deref_array = nir_deref_as_array(tail);
		if (vertex_index_out)
			*vertex_index_out = deref_array->base_offset;

		if (vertex_index_ref) {
			LLVMValueRef vtx = LLVMConstInt(ctx->ac.i32, deref_array->base_offset, false);
			if (deref_array->deref_array_type == nir_deref_array_type_indirect) {
				vtx = LLVMBuildAdd(ctx->ac.builder, vtx, get_src(ctx, deref_array->indirect), "");
			}
			*vertex_index_ref = vtx;
		}
	}

	if (deref->var->data.compact) {
		assert(tail->child->deref_type == nir_deref_type_array);
		assert(glsl_type_is_scalar(glsl_without_array(deref->var->type)));
		nir_deref_array *deref_array = nir_deref_as_array(tail->child);
		/* We always lower indirect dereferences for "compact" array vars. */
		assert(deref_array->deref_array_type == nir_deref_array_type_direct);

		const_offset = deref_array->base_offset;
		goto out;
	}

	while (tail->child != NULL) {
		const struct glsl_type *parent_type = tail->type;
		tail = tail->child;

		if (tail->deref_type == nir_deref_type_array) {
			nir_deref_array *deref_array = nir_deref_as_array(tail);
			LLVMValueRef index, stride, local_offset;
			unsigned size = glsl_count_attribute_slots(tail->type, vs_in);

			const_offset += size * deref_array->base_offset;
			if (deref_array->deref_array_type == nir_deref_array_type_direct)
				continue;

			assert(deref_array->deref_array_type == nir_deref_array_type_indirect);
			index = get_src(ctx, deref_array->indirect);
			stride = LLVMConstInt(ctx->ac.i32, size, 0);
			local_offset = LLVMBuildMul(ctx->ac.builder, stride, index, "");

			if (offset)
				offset = LLVMBuildAdd(ctx->ac.builder, offset, local_offset, "");
			else
				offset = local_offset;
		} else if (tail->deref_type == nir_deref_type_struct) {
			nir_deref_struct *deref_struct = nir_deref_as_struct(tail);

			for (unsigned i = 0; i < deref_struct->index; i++) {
				const struct glsl_type *ft = glsl_get_struct_field(parent_type, i);
				const_offset += glsl_count_attribute_slots(ft, vs_in);
			}
		} else
			unreachable("unsupported deref type");

	}
out:
	if (const_offset && offset)
		offset = LLVMBuildAdd(ctx->ac.builder, offset,
				      LLVMConstInt(ctx->ac.i32, const_offset, 0),
				      "");

	*const_out = const_offset;
	*indir_out = offset;
}


/* The offchip buffer layout for TCS->TES is
 *
 * - attribute 0 of patch 0 vertex 0
 * - attribute 0 of patch 0 vertex 1
 * - attribute 0 of patch 0 vertex 2
 *   ...
 * - attribute 0 of patch 1 vertex 0
 * - attribute 0 of patch 1 vertex 1
 *   ...
 * - attribute 1 of patch 0 vertex 0
 * - attribute 1 of patch 0 vertex 1
 *   ...
 * - per patch attribute 0 of patch 0
 * - per patch attribute 0 of patch 1
 *   ...
 *
 * Note that every attribute has 4 components.
 */
static LLVMValueRef get_tcs_tes_buffer_address(struct nir_to_llvm_context *ctx,
                                               LLVMValueRef vertex_index,
                                               LLVMValueRef param_index)
{
	LLVMValueRef base_addr, vertices_per_patch, num_patches, total_vertices;
	LLVMValueRef param_stride, constant16;
	LLVMValueRef rel_patch_id = get_rel_patch_id(ctx);

	vertices_per_patch = unpack_param(&ctx->ac, ctx->tcs_offchip_layout, 9, 6);
	num_patches = unpack_param(&ctx->ac, ctx->tcs_offchip_layout, 0, 9);
	total_vertices = LLVMBuildMul(ctx->builder, vertices_per_patch,
	                              num_patches, "");

	constant16 = LLVMConstInt(ctx->ac.i32, 16, false);
	if (vertex_index) {
		base_addr = LLVMBuildMul(ctx->builder, rel_patch_id,
		                         vertices_per_patch, "");

		base_addr = LLVMBuildAdd(ctx->builder, base_addr,
		                         vertex_index, "");

		param_stride = total_vertices;
	} else {
		base_addr = rel_patch_id;
		param_stride = num_patches;
	}

	base_addr = LLVMBuildAdd(ctx->builder, base_addr,
	                         LLVMBuildMul(ctx->builder, param_index,
	                                      param_stride, ""), "");

	base_addr = LLVMBuildMul(ctx->builder, base_addr, constant16, "");

	if (!vertex_index) {
		LLVMValueRef patch_data_offset =
		           unpack_param(&ctx->ac, ctx->tcs_offchip_layout, 16, 16);

		base_addr = LLVMBuildAdd(ctx->builder, base_addr,
		                         patch_data_offset, "");
	}
	return base_addr;
}

static LLVMValueRef get_tcs_tes_buffer_address_params(struct nir_to_llvm_context *ctx,
						      unsigned param,
						      unsigned const_index,
						      bool is_compact,
						      LLVMValueRef vertex_index,
						      LLVMValueRef indir_index)
{
	LLVMValueRef param_index;

	if (indir_index)
		param_index = LLVMBuildAdd(ctx->builder, LLVMConstInt(ctx->ac.i32, param, false),
					   indir_index, "");
	else {
		if (const_index && !is_compact)
			param += const_index;
		param_index = LLVMConstInt(ctx->ac.i32, param, false);
	}
	return get_tcs_tes_buffer_address(ctx, vertex_index, param_index);
}

static void
mark_tess_output(struct nir_to_llvm_context *ctx,
		 bool is_patch, uint32_t param)

{
	if (is_patch) {
		ctx->tess_patch_outputs_written |= (1ull << param);
	} else
		ctx->tess_outputs_written |= (1ull << param);
}

static LLVMValueRef
get_dw_address(struct nir_to_llvm_context *ctx,
	       LLVMValueRef dw_addr,
	       unsigned param,
	       unsigned const_index,
	       bool compact_const_index,
	       LLVMValueRef vertex_index,
	       LLVMValueRef stride,
	       LLVMValueRef indir_index)

{

	if (vertex_index) {
		dw_addr = LLVMBuildAdd(ctx->builder, dw_addr,
				       LLVMBuildMul(ctx->builder,
						    vertex_index,
						    stride, ""), "");
	}

	if (indir_index)
		dw_addr = LLVMBuildAdd(ctx->builder, dw_addr,
				       LLVMBuildMul(ctx->builder, indir_index,
						    LLVMConstInt(ctx->ac.i32, 4, false), ""), "");
	else if (const_index && !compact_const_index)
		dw_addr = LLVMBuildAdd(ctx->builder, dw_addr,
				       LLVMConstInt(ctx->ac.i32, const_index, false), "");

	dw_addr = LLVMBuildAdd(ctx->builder, dw_addr,
			       LLVMConstInt(ctx->ac.i32, param * 4, false), "");

	if (const_index && compact_const_index)
		dw_addr = LLVMBuildAdd(ctx->builder, dw_addr,
				       LLVMConstInt(ctx->ac.i32, const_index, false), "");
	return dw_addr;
}

static LLVMValueRef
load_tcs_varyings(struct ac_shader_abi *abi,
		  LLVMValueRef vertex_index,
		  LLVMValueRef indir_index,
		  unsigned const_index,
		  unsigned location,
		  unsigned driver_location,
		  unsigned component,
		  unsigned num_components,
		  bool is_patch,
		  bool is_compact,
		  bool load_input)
{
	struct nir_to_llvm_context *ctx = nir_to_llvm_context_from_abi(abi);
	LLVMValueRef dw_addr, stride;
	LLVMValueRef value[4], result;
	unsigned param = shader_io_get_unique_index(location);

	if (load_input) {
		stride = unpack_param(&ctx->ac, ctx->tcs_in_layout, 13, 8);
		dw_addr = get_tcs_in_current_patch_offset(ctx);
	} else {
		if (!is_patch) {
			stride = unpack_param(&ctx->ac, ctx->tcs_out_layout, 13, 8);
			dw_addr = get_tcs_out_current_patch_offset(ctx);
		} else {
			dw_addr = get_tcs_out_current_patch_data_offset(ctx);
			stride = NULL;
		}
	}

	dw_addr = get_dw_address(ctx, dw_addr, param, const_index, is_compact, vertex_index, stride,
				 indir_index);

	for (unsigned i = 0; i < num_components + component; i++) {
		value[i] = ac_lds_load(&ctx->ac, dw_addr);
		dw_addr = LLVMBuildAdd(ctx->builder, dw_addr,
				       ctx->ac.i32_1, "");
	}
	result = ac_build_varying_gather_values(&ctx->ac, value, num_components, component);
	return result;
}

static void
store_tcs_output(struct ac_shader_abi *abi,
		 LLVMValueRef vertex_index,
		 LLVMValueRef param_index,
		 unsigned const_index,
		 unsigned location,
		 unsigned driver_location,
		 LLVMValueRef src,
		 unsigned component,
		 bool is_patch,
		 bool is_compact,
		 unsigned writemask)
{
	struct nir_to_llvm_context *ctx = nir_to_llvm_context_from_abi(abi);
	LLVMValueRef dw_addr;
	LLVMValueRef stride = NULL;
	LLVMValueRef buf_addr = NULL;
	unsigned param;
	bool store_lds = true;

	if (is_patch) {
		if (!(ctx->tcs_patch_outputs_read & (1U << (location - VARYING_SLOT_PATCH0))))
			store_lds = false;
	} else {
		if (!(ctx->tcs_outputs_read & (1ULL << location)))
			store_lds = false;
	}

	param = shader_io_get_unique_index(location);
	if (location == VARYING_SLOT_CLIP_DIST0 &&
	    is_compact && const_index > 3) {
		const_index -= 3;
		param++;
	}

	if (!is_patch) {
		stride = unpack_param(&ctx->ac, ctx->tcs_out_layout, 13, 8);
		dw_addr = get_tcs_out_current_patch_offset(ctx);
	} else {
		dw_addr = get_tcs_out_current_patch_data_offset(ctx);
	}

	mark_tess_output(ctx, is_patch, param);

	dw_addr = get_dw_address(ctx, dw_addr, param, const_index, is_compact, vertex_index, stride,
				 param_index);
	buf_addr = get_tcs_tes_buffer_address_params(ctx, param, const_index, is_compact,
						     vertex_index, param_index);

	bool is_tess_factor = false;
	if (location == VARYING_SLOT_TESS_LEVEL_INNER ||
	    location == VARYING_SLOT_TESS_LEVEL_OUTER)
		is_tess_factor = true;

	unsigned base = is_compact ? const_index : 0;
	for (unsigned chan = 0; chan < 8; chan++) {
		if (!(writemask & (1 << chan)))
			continue;
		LLVMValueRef value = ac_llvm_extract_elem(&ctx->ac, src, chan - component);

		if (store_lds || is_tess_factor) {
			LLVMValueRef dw_addr_chan =
				LLVMBuildAdd(ctx->builder, dw_addr,
				                           LLVMConstInt(ctx->ac.i32, chan, false), "");
			ac_lds_store(&ctx->ac, dw_addr_chan, value);
		}

		if (!is_tess_factor && writemask != 0xF)
			ac_build_buffer_store_dword(&ctx->ac, ctx->hs_ring_tess_offchip, value, 1,
						    buf_addr, ctx->oc_lds,
						    4 * (base + chan), 1, 0, true, false);
	}

	if (writemask == 0xF) {
		ac_build_buffer_store_dword(&ctx->ac, ctx->hs_ring_tess_offchip, src, 4,
					    buf_addr, ctx->oc_lds,
					    (base * 4), 1, 0, true, false);
	}
}

static LLVMValueRef
load_tes_input(struct ac_shader_abi *abi,
	       LLVMValueRef vertex_index,
	       LLVMValueRef param_index,
	       unsigned const_index,
	       unsigned location,
	       unsigned driver_location,
	       unsigned component,
	       unsigned num_components,
	       bool is_patch,
	       bool is_compact,
	       bool load_input)
{
	struct nir_to_llvm_context *ctx = nir_to_llvm_context_from_abi(abi);
	LLVMValueRef buf_addr;
	LLVMValueRef result;
	unsigned param = shader_io_get_unique_index(location);

	if (location == VARYING_SLOT_CLIP_DIST0 && is_compact && const_index > 3) {
		const_index -= 3;
		param++;
	}

	buf_addr = get_tcs_tes_buffer_address_params(ctx, param, const_index,
						     is_compact, vertex_index, param_index);

	LLVMValueRef comp_offset = LLVMConstInt(ctx->ac.i32, component * 4, false);
	buf_addr = LLVMBuildAdd(ctx->builder, buf_addr, comp_offset, "");

	result = ac_build_buffer_load(&ctx->ac, ctx->hs_ring_tess_offchip, num_components, NULL,
				      buf_addr, ctx->oc_lds, is_compact ? (4 * const_index) : 0, 1, 0, true, false);
	result = trim_vector(&ctx->ac, result, num_components);
	return result;
}

static LLVMValueRef
load_gs_input(struct ac_shader_abi *abi,
	      unsigned location,
	      unsigned driver_location,
	      unsigned component,
	      unsigned num_components,
	      unsigned vertex_index,
	      unsigned const_index,
	      LLVMTypeRef type)
{
	struct nir_to_llvm_context *ctx = nir_to_llvm_context_from_abi(abi);
	LLVMValueRef vtx_offset;
	unsigned param, vtx_offset_param;
	LLVMValueRef value[4], result;

	vtx_offset_param = vertex_index;
	assert(vtx_offset_param < 6);
	vtx_offset = LLVMBuildMul(ctx->builder, ctx->gs_vtx_offset[vtx_offset_param],
				  LLVMConstInt(ctx->ac.i32, 4, false), "");

	param = shader_io_get_unique_index(location);

	for (unsigned i = component; i < num_components + component; i++) {
		if (ctx->ac.chip_class >= GFX9) {
			LLVMValueRef dw_addr = ctx->gs_vtx_offset[vtx_offset_param];
			dw_addr = LLVMBuildAdd(ctx->ac.builder, dw_addr,
			                       LLVMConstInt(ctx->ac.i32, param * 4 + i + const_index, 0), "");
			value[i] = ac_lds_load(&ctx->ac, dw_addr);
		} else {
			LLVMValueRef soffset =
				LLVMConstInt(ctx->ac.i32,
					     (param * 4 + i + const_index) * 256,
					     false);

			value[i] = ac_build_buffer_load(&ctx->ac,
							ctx->esgs_ring, 1,
							ctx->ac.i32_0,
							vtx_offset, soffset,
							0, 1, 0, true, false);
		}
	}
	result = ac_build_varying_gather_values(&ctx->ac, value, num_components, component);

	return result;
}

static LLVMValueRef
build_gep_for_deref(struct ac_nir_context *ctx,
		    nir_deref_var *deref)
{
	struct hash_entry *entry = _mesa_hash_table_search(ctx->vars, deref->var);
	assert(entry->data);
	LLVMValueRef val = entry->data;
	nir_deref *tail = deref->deref.child;
	while (tail != NULL) {
		LLVMValueRef offset;
		switch (tail->deref_type) {
		case nir_deref_type_array: {
			nir_deref_array *array = nir_deref_as_array(tail);
			offset = LLVMConstInt(ctx->ac.i32, array->base_offset, 0);
			if (array->deref_array_type ==
			    nir_deref_array_type_indirect) {
				offset = LLVMBuildAdd(ctx->ac.builder, offset,
						      get_src(ctx,
							      array->indirect),
						      "");
			}
			break;
		}
		case nir_deref_type_struct: {
			nir_deref_struct *deref_struct =
				nir_deref_as_struct(tail);
			offset = LLVMConstInt(ctx->ac.i32,
					      deref_struct->index, 0);
			break;
		}
		default:
			unreachable("bad deref type");
		}
		val = ac_build_gep0(&ctx->ac, val, offset);
		tail = tail->child;
	}
	return val;
}

static LLVMValueRef load_tess_varyings(struct ac_nir_context *ctx,
				       nir_intrinsic_instr *instr,
				       bool load_inputs)
{
	LLVMValueRef result;
	LLVMValueRef vertex_index = NULL;
	LLVMValueRef indir_index = NULL;
	unsigned const_index = 0;
	unsigned location = instr->variables[0]->var->data.location;
	unsigned driver_location = instr->variables[0]->var->data.driver_location;
	const bool is_patch =  instr->variables[0]->var->data.patch;
	const bool is_compact = instr->variables[0]->var->data.compact;

	get_deref_offset(ctx, instr->variables[0],
			 false, NULL, is_patch ? NULL : &vertex_index,
			 &const_index, &indir_index);

	result = ctx->abi->load_tess_varyings(ctx->abi, vertex_index, indir_index,
					      const_index, location, driver_location,
					      instr->variables[0]->var->data.location_frac,
					      instr->num_components,
					      is_patch, is_compact, load_inputs);
	return LLVMBuildBitCast(ctx->ac.builder, result, get_def_type(ctx, &instr->dest.ssa), "");
}

static LLVMValueRef visit_load_var(struct ac_nir_context *ctx,
				   nir_intrinsic_instr *instr)
{
	LLVMValueRef values[8];
	int idx = instr->variables[0]->var->data.driver_location;
	int ve = instr->dest.ssa.num_components;
	unsigned comp = instr->variables[0]->var->data.location_frac;
	LLVMValueRef indir_index;
	LLVMValueRef ret;
	unsigned const_index;
	unsigned stride = instr->variables[0]->var->data.compact ? 1 : 4;
	bool vs_in = ctx->stage == MESA_SHADER_VERTEX &&
	             instr->variables[0]->var->data.mode == nir_var_shader_in;
	get_deref_offset(ctx, instr->variables[0], vs_in, NULL, NULL,
				      &const_index, &indir_index);

	if (instr->dest.ssa.bit_size == 64)
		ve *= 2;

	switch (instr->variables[0]->var->data.mode) {
	case nir_var_shader_in:
		if (ctx->stage == MESA_SHADER_TESS_CTRL ||
		    ctx->stage == MESA_SHADER_TESS_EVAL) {
			return load_tess_varyings(ctx, instr, true);
		}

		if (ctx->stage == MESA_SHADER_GEOMETRY) {
			LLVMTypeRef type = LLVMIntTypeInContext(ctx->ac.context, instr->dest.ssa.bit_size);
			LLVMValueRef indir_index;
			unsigned const_index, vertex_index;
			get_deref_offset(ctx, instr->variables[0],
					 false, &vertex_index, NULL,
					 &const_index, &indir_index);

			return ctx->abi->load_inputs(ctx->abi, instr->variables[0]->var->data.location,
						     instr->variables[0]->var->data.driver_location,
						     instr->variables[0]->var->data.location_frac, ve,
						     vertex_index, const_index, type);
		}

		for (unsigned chan = comp; chan < ve + comp; chan++) {
			if (indir_index) {
				unsigned count = glsl_count_attribute_slots(
						instr->variables[0]->var->type,
						ctx->stage == MESA_SHADER_VERTEX);
				count -= chan / 4;
				LLVMValueRef tmp_vec = ac_build_gather_values_extended(
						&ctx->ac, ctx->abi->inputs + idx + chan, count,
						stride, false, true);

				values[chan] = LLVMBuildExtractElement(ctx->ac.builder,
								       tmp_vec,
								       indir_index, "");
			} else
				values[chan] = ctx->abi->inputs[idx + chan + const_index * stride];
		}
		break;
	case nir_var_local:
		for (unsigned chan = 0; chan < ve; chan++) {
			if (indir_index) {
				unsigned count = glsl_count_attribute_slots(
					instr->variables[0]->var->type, false);
				count -= chan / 4;
				LLVMValueRef tmp_vec = ac_build_gather_values_extended(
						&ctx->ac, ctx->locals + idx + chan, count,
						stride, true, true);

				values[chan] = LLVMBuildExtractElement(ctx->ac.builder,
								       tmp_vec,
								       indir_index, "");
			} else {
				values[chan] = LLVMBuildLoad(ctx->ac.builder, ctx->locals[idx + chan + const_index * stride], "");
			}
		}
		break;
	case nir_var_shared: {
		LLVMValueRef address = build_gep_for_deref(ctx,
							   instr->variables[0]);
		LLVMValueRef val = LLVMBuildLoad(ctx->ac.builder, address, "");
		return LLVMBuildBitCast(ctx->ac.builder, val,
					get_def_type(ctx, &instr->dest.ssa),
					"");
	}
	case nir_var_shader_out:
		if (ctx->stage == MESA_SHADER_TESS_CTRL) {
			return load_tess_varyings(ctx, instr, false);
		}

		for (unsigned chan = comp; chan < ve + comp; chan++) {
			if (indir_index) {
				unsigned count = glsl_count_attribute_slots(
						instr->variables[0]->var->type, false);
				count -= chan / 4;
				LLVMValueRef tmp_vec = ac_build_gather_values_extended(
						&ctx->ac, ctx->outputs + idx + chan, count,
						stride, true, true);

				values[chan] = LLVMBuildExtractElement(ctx->ac.builder,
								       tmp_vec,
								       indir_index, "");
			} else {
				values[chan] = LLVMBuildLoad(ctx->ac.builder,
						     ctx->outputs[idx + chan + const_index * stride],
						     "");
			}
		}
		break;
	default:
		unreachable("unhandle variable mode");
	}
	ret = ac_build_varying_gather_values(&ctx->ac, values, ve, comp);
	return LLVMBuildBitCast(ctx->ac.builder, ret, get_def_type(ctx, &instr->dest.ssa), "");
}

static void
visit_store_var(struct ac_nir_context *ctx,
		nir_intrinsic_instr *instr)
{
	LLVMValueRef temp_ptr, value;
	int idx = instr->variables[0]->var->data.driver_location;
	unsigned comp = instr->variables[0]->var->data.location_frac;
	LLVMValueRef src = ac_to_float(&ctx->ac, get_src(ctx, instr->src[0]));
	int writemask = instr->const_index[0] << comp;
	LLVMValueRef indir_index;
	unsigned const_index;
	get_deref_offset(ctx, instr->variables[0], false,
		         NULL, NULL, &const_index, &indir_index);

	if (get_elem_bits(&ctx->ac, LLVMTypeOf(src)) == 64) {

		src = LLVMBuildBitCast(ctx->ac.builder, src,
		                       LLVMVectorType(ctx->ac.f32, ac_get_llvm_num_components(src) * 2),
		                       "");

		writemask = widen_mask(writemask, 2);
	}

	switch (instr->variables[0]->var->data.mode) {
	case nir_var_shader_out:

		if (ctx->stage == MESA_SHADER_TESS_CTRL) {
			LLVMValueRef vertex_index = NULL;
			LLVMValueRef indir_index = NULL;
			unsigned const_index = 0;
			const unsigned location = instr->variables[0]->var->data.location;
			const unsigned driver_location = instr->variables[0]->var->data.driver_location;
			const unsigned comp = instr->variables[0]->var->data.location_frac;
			const bool is_patch = instr->variables[0]->var->data.patch;
			const bool is_compact = instr->variables[0]->var->data.compact;

			get_deref_offset(ctx, instr->variables[0],
					 false, NULL, is_patch ? NULL : &vertex_index,
					 &const_index, &indir_index);

			ctx->abi->store_tcs_outputs(ctx->abi, vertex_index, indir_index,
						    const_index, location, driver_location,
						    src, comp, is_patch, is_compact, writemask);
			return;
		}

		for (unsigned chan = 0; chan < 8; chan++) {
			int stride = 4;
			if (!(writemask & (1 << chan)))
				continue;

			value = ac_llvm_extract_elem(&ctx->ac, src, chan - comp);

			if (instr->variables[0]->var->data.compact)
				stride = 1;
			if (indir_index) {
				unsigned count = glsl_count_attribute_slots(
						instr->variables[0]->var->type, false);
				count -= chan / 4;
				LLVMValueRef tmp_vec = ac_build_gather_values_extended(
						&ctx->ac, ctx->outputs + idx + chan, count,
						stride, true, true);

				tmp_vec = LLVMBuildInsertElement(ctx->ac.builder, tmp_vec,
							         value, indir_index, "");
				build_store_values_extended(&ctx->ac, ctx->outputs + idx + chan,
							    count, stride, tmp_vec);

			} else {
				temp_ptr = ctx->outputs[idx + chan + const_index * stride];

				LLVMBuildStore(ctx->ac.builder, value, temp_ptr);
			}
		}
		break;
	case nir_var_local:
		for (unsigned chan = 0; chan < 8; chan++) {
			if (!(writemask & (1 << chan)))
				continue;

			value = ac_llvm_extract_elem(&ctx->ac, src, chan);
			if (indir_index) {
				unsigned count = glsl_count_attribute_slots(
					instr->variables[0]->var->type, false);
				count -= chan / 4;
				LLVMValueRef tmp_vec = ac_build_gather_values_extended(
					&ctx->ac, ctx->locals + idx + chan, count,
					4, true, true);

				tmp_vec = LLVMBuildInsertElement(ctx->ac.builder, tmp_vec,
								 value, indir_index, "");
				build_store_values_extended(&ctx->ac, ctx->locals + idx + chan,
							    count, 4, tmp_vec);
			} else {
				temp_ptr = ctx->locals[idx + chan + const_index * 4];

				LLVMBuildStore(ctx->ac.builder, value, temp_ptr);
			}
		}
		break;
	case nir_var_shared: {
		int writemask = instr->const_index[0];
		LLVMValueRef address = build_gep_for_deref(ctx,
							   instr->variables[0]);
		LLVMValueRef val = get_src(ctx, instr->src[0]);
		unsigned components =
			glsl_get_vector_elements(
			   nir_deref_tail(&instr->variables[0]->deref)->type);
		if (writemask == (1 << components) - 1) {
			val = LLVMBuildBitCast(
			   ctx->ac.builder, val,
			   LLVMGetElementType(LLVMTypeOf(address)), "");
			LLVMBuildStore(ctx->ac.builder, val, address);
		} else {
			for (unsigned chan = 0; chan < 4; chan++) {
				if (!(writemask & (1 << chan)))
					continue;
				LLVMValueRef ptr =
					LLVMBuildStructGEP(ctx->ac.builder,
							   address, chan, "");
				LLVMValueRef src = ac_llvm_extract_elem(&ctx->ac, val,
									chan);
				src = LLVMBuildBitCast(
				   ctx->ac.builder, src,
				   LLVMGetElementType(LLVMTypeOf(ptr)), "");
				LLVMBuildStore(ctx->ac.builder, src, ptr);
			}
		}
		break;
	}
	default:
		break;
	}
}

static int image_type_to_components_count(enum glsl_sampler_dim dim, bool array)
{
	switch (dim) {
	case GLSL_SAMPLER_DIM_BUF:
		return 1;
	case GLSL_SAMPLER_DIM_1D:
		return array ? 2 : 1;
	case GLSL_SAMPLER_DIM_2D:
		return array ? 3 : 2;
	case GLSL_SAMPLER_DIM_MS:
		return array ? 4 : 3;
	case GLSL_SAMPLER_DIM_3D:
	case GLSL_SAMPLER_DIM_CUBE:
		return 3;
	case GLSL_SAMPLER_DIM_RECT:
	case GLSL_SAMPLER_DIM_SUBPASS:
		return 2;
	case GLSL_SAMPLER_DIM_SUBPASS_MS:
		return 3;
	default:
		break;
	}
	return 0;
}



/* Adjust the sample index according to FMASK.
 *
 * For uncompressed MSAA surfaces, FMASK should return 0x76543210,
 * which is the identity mapping. Each nibble says which physical sample
 * should be fetched to get that sample.
 *
 * For example, 0x11111100 means there are only 2 samples stored and
 * the second sample covers 3/4 of the pixel. When reading samples 0
 * and 1, return physical sample 0 (determined by the first two 0s
 * in FMASK), otherwise return physical sample 1.
 *
 * The sample index should be adjusted as follows:
 *   sample_index = (fmask >> (sample_index * 4)) & 0xF;
 */
static LLVMValueRef adjust_sample_index_using_fmask(struct ac_llvm_context *ctx,
						    LLVMValueRef coord_x, LLVMValueRef coord_y,
						    LLVMValueRef coord_z,
						    LLVMValueRef sample_index,
						    LLVMValueRef fmask_desc_ptr)
{
	LLVMValueRef fmask_load_address[4];
	LLVMValueRef res;

	fmask_load_address[0] = coord_x;
	fmask_load_address[1] = coord_y;
	if (coord_z) {
		fmask_load_address[2] = coord_z;
		fmask_load_address[3] = LLVMGetUndef(ctx->i32);
	}

	struct ac_image_args args = {0};

	args.opcode = ac_image_load;
	args.da = coord_z ? true : false;
	args.resource = fmask_desc_ptr;
	args.dmask = 0xf;
	args.addr = ac_build_gather_values(ctx, fmask_load_address, coord_z ? 4 : 2);

	res = ac_build_image_opcode(ctx, &args);

	res = ac_to_integer(ctx, res);
	LLVMValueRef four = LLVMConstInt(ctx->i32, 4, false);
	LLVMValueRef F = LLVMConstInt(ctx->i32, 0xf, false);

	LLVMValueRef fmask = LLVMBuildExtractElement(ctx->builder,
						     res,
						     ctx->i32_0, "");

	LLVMValueRef sample_index4 =
		LLVMBuildMul(ctx->builder, sample_index, four, "");
	LLVMValueRef shifted_fmask =
		LLVMBuildLShr(ctx->builder, fmask, sample_index4, "");
	LLVMValueRef final_sample =
		LLVMBuildAnd(ctx->builder, shifted_fmask, F, "");

	/* Don't rewrite the sample index if WORD1.DATA_FORMAT of the FMASK
	 * resource descriptor is 0 (invalid),
	 */
	LLVMValueRef fmask_desc =
		LLVMBuildBitCast(ctx->builder, fmask_desc_ptr,
				 ctx->v8i32, "");

	LLVMValueRef fmask_word1 =
		LLVMBuildExtractElement(ctx->builder, fmask_desc,
					ctx->i32_1, "");

	LLVMValueRef word1_is_nonzero =
		LLVMBuildICmp(ctx->builder, LLVMIntNE,
			      fmask_word1, ctx->i32_0, "");

	/* Replace the MSAA sample index. */
	sample_index =
		LLVMBuildSelect(ctx->builder, word1_is_nonzero,
				final_sample, sample_index, "");
	return sample_index;
}

static LLVMValueRef get_image_coords(struct ac_nir_context *ctx,
				     const nir_intrinsic_instr *instr)
{
	const struct glsl_type *type = glsl_without_array(instr->variables[0]->var->type);

	LLVMValueRef src0 = get_src(ctx, instr->src[0]);
	LLVMValueRef coords[4];
	LLVMValueRef masks[] = {
		LLVMConstInt(ctx->ac.i32, 0, false), LLVMConstInt(ctx->ac.i32, 1, false),
		LLVMConstInt(ctx->ac.i32, 2, false), LLVMConstInt(ctx->ac.i32, 3, false),
	};
	LLVMValueRef res;
	LLVMValueRef sample_index = ac_llvm_extract_elem(&ctx->ac, get_src(ctx, instr->src[1]), 0);

	int count;
	enum glsl_sampler_dim dim = glsl_get_sampler_dim(type);
	bool is_array = glsl_sampler_type_is_array(type);
	bool add_frag_pos = (dim == GLSL_SAMPLER_DIM_SUBPASS ||
			     dim == GLSL_SAMPLER_DIM_SUBPASS_MS);
	bool is_ms = (dim == GLSL_SAMPLER_DIM_MS ||
		      dim == GLSL_SAMPLER_DIM_SUBPASS_MS);
	bool gfx9_1d = ctx->ac.chip_class >= GFX9 && dim == GLSL_SAMPLER_DIM_1D;
	count = image_type_to_components_count(dim, is_array);

	if (is_ms) {
		LLVMValueRef fmask_load_address[3];
		int chan;

		fmask_load_address[0] = LLVMBuildExtractElement(ctx->ac.builder, src0, masks[0], "");
		fmask_load_address[1] = LLVMBuildExtractElement(ctx->ac.builder, src0, masks[1], "");
		if (is_array)
			fmask_load_address[2] = LLVMBuildExtractElement(ctx->ac.builder, src0, masks[2], "");
		else
			fmask_load_address[2] = NULL;
		if (add_frag_pos) {
			for (chan = 0; chan < 2; ++chan)
				fmask_load_address[chan] =
					LLVMBuildAdd(ctx->ac.builder, fmask_load_address[chan],
						LLVMBuildFPToUI(ctx->ac.builder, ctx->abi->frag_pos[chan],
								ctx->ac.i32, ""), "");
			fmask_load_address[2] = ac_to_integer(&ctx->ac, ctx->abi->inputs[radeon_llvm_reg_index_soa(VARYING_SLOT_LAYER, 0)]);
		}
		sample_index = adjust_sample_index_using_fmask(&ctx->ac,
							       fmask_load_address[0],
							       fmask_load_address[1],
							       fmask_load_address[2],
							       sample_index,
							       get_sampler_desc(ctx, instr->variables[0], AC_DESC_FMASK, NULL, true, false));
	}
	if (count == 1 && !gfx9_1d) {
		if (instr->src[0].ssa->num_components)
			res = LLVMBuildExtractElement(ctx->ac.builder, src0, masks[0], "");
		else
			res = src0;
	} else {
		int chan;
		if (is_ms)
			count--;
		for (chan = 0; chan < count; ++chan) {
			coords[chan] = ac_llvm_extract_elem(&ctx->ac, src0, chan);
		}
		if (add_frag_pos) {
			for (chan = 0; chan < 2; ++chan)
				coords[chan] = LLVMBuildAdd(ctx->ac.builder, coords[chan], LLVMBuildFPToUI(ctx->ac.builder, ctx->abi->frag_pos[chan],
						ctx->ac.i32, ""), "");
			coords[2] = ac_to_integer(&ctx->ac, ctx->abi->inputs[radeon_llvm_reg_index_soa(VARYING_SLOT_LAYER, 0)]);
			count++;
		}

		if (gfx9_1d) {
			if (is_array) {
				coords[2] = coords[1];
				coords[1] = ctx->ac.i32_0;
			} else
				coords[1] = ctx->ac.i32_0;
			count++;
		}

		if (is_ms) {
			coords[count] = sample_index;
			count++;
		}

		if (count == 3) {
			coords[3] = LLVMGetUndef(ctx->ac.i32);
			count = 4;
		}
		res = ac_build_gather_values(&ctx->ac, coords, count);
	}
	return res;
}

static LLVMValueRef visit_image_load(struct ac_nir_context *ctx,
				     const nir_intrinsic_instr *instr)
{
	LLVMValueRef params[7];
	LLVMValueRef res;
	char intrinsic_name[64];
	const nir_variable *var = instr->variables[0]->var;
	const struct glsl_type *type = var->type;

	if(instr->variables[0]->deref.child)
		type = instr->variables[0]->deref.child->type;

	type = glsl_without_array(type);

	const enum glsl_sampler_dim dim = glsl_get_sampler_dim(type);
	if (dim == GLSL_SAMPLER_DIM_BUF) {
		unsigned mask = nir_ssa_def_components_read(&instr->dest.ssa);
		unsigned num_channels = util_last_bit(mask);
		LLVMValueRef rsrc, vindex;

		rsrc = get_sampler_desc(ctx, instr->variables[0], AC_DESC_BUFFER, NULL, true, false);
		vindex = LLVMBuildExtractElement(ctx->ac.builder, get_src(ctx, instr->src[0]),
						 ctx->ac.i32_0, "");

		/* TODO: set "glc" and "can_speculate" when OpenGL needs it. */
		res = ac_build_buffer_load_format(&ctx->ac, rsrc, vindex,
						  ctx->ac.i32_0, num_channels,
						  false, false);
		res = ac_build_expand_to_vec4(&ctx->ac, res, num_channels);

		res = trim_vector(&ctx->ac, res, instr->dest.ssa.num_components);
		res = ac_to_integer(&ctx->ac, res);
	} else {
		bool is_da = glsl_sampler_type_is_array(type) ||
			     dim == GLSL_SAMPLER_DIM_CUBE ||
			     dim == GLSL_SAMPLER_DIM_3D ||
			     dim == GLSL_SAMPLER_DIM_SUBPASS ||
			     dim == GLSL_SAMPLER_DIM_SUBPASS_MS;
		LLVMValueRef da = is_da ? ctx->ac.i1true : ctx->ac.i1false;
		LLVMValueRef glc = ctx->ac.i1false;
		LLVMValueRef slc = ctx->ac.i1false;

		params[0] = get_image_coords(ctx, instr);
		params[1] = get_sampler_desc(ctx, instr->variables[0], AC_DESC_IMAGE, NULL, true, false);
		params[2] = LLVMConstInt(ctx->ac.i32, 15, false); /* dmask */
		params[3] = glc;
		params[4] = slc;
		params[5] = ctx->ac.i1false;
		params[6] = da;

		ac_get_image_intr_name("llvm.amdgcn.image.load",
				       ctx->ac.v4f32, /* vdata */
				       LLVMTypeOf(params[0]), /* coords */
				       LLVMTypeOf(params[1]), /* rsrc */
				       intrinsic_name, sizeof(intrinsic_name));

		res = ac_build_intrinsic(&ctx->ac, intrinsic_name, ctx->ac.v4f32,
					 params, 7, AC_FUNC_ATTR_READONLY);
	}
	return ac_to_integer(&ctx->ac, res);
}

static void visit_image_store(struct ac_nir_context *ctx,
			      nir_intrinsic_instr *instr)
{
	LLVMValueRef params[8];
	char intrinsic_name[64];
	const nir_variable *var = instr->variables[0]->var;
	const struct glsl_type *type = glsl_without_array(var->type);
	const enum glsl_sampler_dim dim = glsl_get_sampler_dim(type);
	LLVMValueRef glc = ctx->ac.i1false;
	bool force_glc = ctx->ac.chip_class == SI;
	if (force_glc)
		glc = ctx->ac.i1true;

	if (dim == GLSL_SAMPLER_DIM_BUF) {
		params[0] = ac_to_float(&ctx->ac, get_src(ctx, instr->src[2])); /* data */
		params[1] = get_sampler_desc(ctx, instr->variables[0], AC_DESC_BUFFER, NULL, true, true);
		params[2] = LLVMBuildExtractElement(ctx->ac.builder, get_src(ctx, instr->src[0]),
						    ctx->ac.i32_0, ""); /* vindex */
		params[3] = ctx->ac.i32_0; /* voffset */
		params[4] = glc;  /* glc */
		params[5] = ctx->ac.i1false;  /* slc */
		ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.buffer.store.format.v4f32", ctx->ac.voidt,
				   params, 6, 0);
	} else {
		bool is_da = glsl_sampler_type_is_array(type) ||
			     dim == GLSL_SAMPLER_DIM_CUBE ||
			     dim == GLSL_SAMPLER_DIM_3D;
		LLVMValueRef da = is_da ? ctx->ac.i1true : ctx->ac.i1false;
		LLVMValueRef slc = ctx->ac.i1false;

		params[0] = ac_to_float(&ctx->ac, get_src(ctx, instr->src[2]));
		params[1] = get_image_coords(ctx, instr); /* coords */
		params[2] = get_sampler_desc(ctx, instr->variables[0], AC_DESC_IMAGE, NULL, true, true);
		params[3] = LLVMConstInt(ctx->ac.i32, 15, false); /* dmask */
		params[4] = glc;
		params[5] = slc;
		params[6] = ctx->ac.i1false;
		params[7] = da;

		ac_get_image_intr_name("llvm.amdgcn.image.store",
				       LLVMTypeOf(params[0]), /* vdata */
				       LLVMTypeOf(params[1]), /* coords */
				       LLVMTypeOf(params[2]), /* rsrc */
				       intrinsic_name, sizeof(intrinsic_name));

		ac_build_intrinsic(&ctx->ac, intrinsic_name, ctx->ac.voidt,
				   params, 8, 0);
	}

}

static LLVMValueRef visit_image_atomic(struct ac_nir_context *ctx,
                                       const nir_intrinsic_instr *instr)
{
	LLVMValueRef params[7];
	int param_count = 0;
	const nir_variable *var = instr->variables[0]->var;

	const char *atomic_name;
	char intrinsic_name[41];
	const struct glsl_type *type = glsl_without_array(var->type);
	MAYBE_UNUSED int length;

	bool is_unsigned = glsl_get_sampler_result_type(type) == GLSL_TYPE_UINT;

	switch (instr->intrinsic) {
	case nir_intrinsic_image_atomic_add:
		atomic_name = "add";
		break;
	case nir_intrinsic_image_atomic_min:
		atomic_name = is_unsigned ? "umin" : "smin";
		break;
	case nir_intrinsic_image_atomic_max:
		atomic_name = is_unsigned ? "umax" : "smax";
		break;
	case nir_intrinsic_image_atomic_and:
		atomic_name = "and";
		break;
	case nir_intrinsic_image_atomic_or:
		atomic_name = "or";
		break;
	case nir_intrinsic_image_atomic_xor:
		atomic_name = "xor";
		break;
	case nir_intrinsic_image_atomic_exchange:
		atomic_name = "swap";
		break;
	case nir_intrinsic_image_atomic_comp_swap:
		atomic_name = "cmpswap";
		break;
	default:
		abort();
	}

	if (instr->intrinsic == nir_intrinsic_image_atomic_comp_swap)
		params[param_count++] = get_src(ctx, instr->src[3]);
	params[param_count++] = get_src(ctx, instr->src[2]);

	if (glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_BUF) {
		params[param_count++] = get_sampler_desc(ctx, instr->variables[0], AC_DESC_BUFFER,
							 NULL, true, true);
		params[param_count++] = LLVMBuildExtractElement(ctx->ac.builder, get_src(ctx, instr->src[0]),
								ctx->ac.i32_0, ""); /* vindex */
		params[param_count++] = ctx->ac.i32_0; /* voffset */
		params[param_count++] = ctx->ac.i1false;  /* slc */

		length = snprintf(intrinsic_name, sizeof(intrinsic_name),
				  "llvm.amdgcn.buffer.atomic.%s", atomic_name);
	} else {
		char coords_type[8];

		bool da = glsl_sampler_type_is_array(type) ||
		          glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_CUBE;

		LLVMValueRef coords = params[param_count++] = get_image_coords(ctx, instr);
		params[param_count++] = get_sampler_desc(ctx, instr->variables[0], AC_DESC_IMAGE,
							 NULL, true, true);
		params[param_count++] = ctx->ac.i1false; /* r128 */
		params[param_count++] = da ? ctx->ac.i1true : ctx->ac.i1false;      /* da */
		params[param_count++] = ctx->ac.i1false;  /* slc */

		build_int_type_name(LLVMTypeOf(coords),
				    coords_type, sizeof(coords_type));

		length = snprintf(intrinsic_name, sizeof(intrinsic_name),
				  "llvm.amdgcn.image.atomic.%s.%s", atomic_name, coords_type);
	}

	assert(length < sizeof(intrinsic_name));
	return ac_build_intrinsic(&ctx->ac, intrinsic_name, ctx->ac.i32, params, param_count, 0);
}

static LLVMValueRef visit_image_size(struct ac_nir_context *ctx,
				     const nir_intrinsic_instr *instr)
{
	LLVMValueRef res;
	const nir_variable *var = instr->variables[0]->var;
	const struct glsl_type *type = instr->variables[0]->var->type;
	bool da = glsl_sampler_type_is_array(var->type) ||
		  glsl_get_sampler_dim(var->type) == GLSL_SAMPLER_DIM_CUBE ||
		  glsl_get_sampler_dim(var->type) == GLSL_SAMPLER_DIM_3D;
	if(instr->variables[0]->deref.child)
		type = instr->variables[0]->deref.child->type;

	if (glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_BUF)
		return get_buffer_size(ctx,
			get_sampler_desc(ctx, instr->variables[0],
					 AC_DESC_BUFFER, NULL, true, false), true);

	struct ac_image_args args = { 0 };

	args.da = da;
	args.dmask = 0xf;
	args.resource = get_sampler_desc(ctx, instr->variables[0], AC_DESC_IMAGE, NULL, true, false);
	args.opcode = ac_image_get_resinfo;
	args.addr = ctx->ac.i32_0;

	res = ac_build_image_opcode(&ctx->ac, &args);

	LLVMValueRef two = LLVMConstInt(ctx->ac.i32, 2, false);

	if (glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_CUBE &&
	    glsl_sampler_type_is_array(type)) {
		LLVMValueRef six = LLVMConstInt(ctx->ac.i32, 6, false);
		LLVMValueRef z = LLVMBuildExtractElement(ctx->ac.builder, res, two, "");
		z = LLVMBuildSDiv(ctx->ac.builder, z, six, "");
		res = LLVMBuildInsertElement(ctx->ac.builder, res, z, two, "");
	}
	if (ctx->ac.chip_class >= GFX9 &&
	    glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_1D &&
	    glsl_sampler_type_is_array(type)) {
		LLVMValueRef layers = LLVMBuildExtractElement(ctx->ac.builder, res, two, "");
		res = LLVMBuildInsertElement(ctx->ac.builder, res, layers,
						ctx->ac.i32_1, "");

	}
	return res;
}

#define NOOP_WAITCNT 0xf7f
#define LGKM_CNT 0x07f
#define VM_CNT 0xf70

static void emit_membar(struct nir_to_llvm_context *ctx,
			const nir_intrinsic_instr *instr)
{
	unsigned waitcnt = NOOP_WAITCNT;

	switch (instr->intrinsic) {
	case nir_intrinsic_memory_barrier:
	case nir_intrinsic_group_memory_barrier:
		waitcnt &= VM_CNT & LGKM_CNT;
		break;
	case nir_intrinsic_memory_barrier_atomic_counter:
	case nir_intrinsic_memory_barrier_buffer:
	case nir_intrinsic_memory_barrier_image:
		waitcnt &= VM_CNT;
		break;
	case nir_intrinsic_memory_barrier_shared:
		waitcnt &= LGKM_CNT;
		break;
	default:
		break;
	}
	if (waitcnt != NOOP_WAITCNT)
		ac_build_waitcnt(&ctx->ac, waitcnt);
}

static void emit_barrier(struct ac_llvm_context *ac, gl_shader_stage stage)
{
	/* SI only (thanks to a hw bug workaround):
	 * The real barrier instruction isn’t needed, because an entire patch
	 * always fits into a single wave.
	 */
	if (ac->chip_class == SI && stage == MESA_SHADER_TESS_CTRL) {
		ac_build_waitcnt(ac, LGKM_CNT & VM_CNT);
		return;
	}
	ac_build_intrinsic(ac, "llvm.amdgcn.s.barrier",
			   ac->voidt, NULL, 0, AC_FUNC_ATTR_CONVERGENT);
}

static void emit_discard(struct ac_nir_context *ctx,
			 const nir_intrinsic_instr *instr)
{
	LLVMValueRef cond;

	if (instr->intrinsic == nir_intrinsic_discard_if) {
		cond = LLVMBuildICmp(ctx->ac.builder, LLVMIntEQ,
				     get_src(ctx, instr->src[0]),
				     ctx->ac.i32_0, "");
	} else {
		assert(instr->intrinsic == nir_intrinsic_discard);
		cond = LLVMConstInt(ctx->ac.i1, false, 0);
	}

	ac_build_kill_if_false(&ctx->ac, cond);
}

static LLVMValueRef
visit_load_helper_invocation(struct ac_nir_context *ctx)
{
	LLVMValueRef result = ac_build_intrinsic(&ctx->ac,
						 "llvm.amdgcn.ps.live",
						 ctx->ac.i1, NULL, 0,
						 AC_FUNC_ATTR_READNONE);
	result = LLVMBuildNot(ctx->ac.builder, result, "");
	return LLVMBuildSExt(ctx->ac.builder, result, ctx->ac.i32, "");
}

static LLVMValueRef
visit_load_local_invocation_index(struct nir_to_llvm_context *ctx)
{
	LLVMValueRef result;
	LLVMValueRef thread_id = ac_get_thread_id(&ctx->ac);
	result = LLVMBuildAnd(ctx->builder, ctx->tg_size,
			      LLVMConstInt(ctx->ac.i32, 0xfc0, false), "");

	return LLVMBuildAdd(ctx->builder, result, thread_id, "");
}

static LLVMValueRef visit_var_atomic(struct nir_to_llvm_context *ctx,
				     const nir_intrinsic_instr *instr)
{
	LLVMValueRef ptr, result;
	LLVMValueRef src = get_src(ctx->nir, instr->src[0]);
	ptr = build_gep_for_deref(ctx->nir, instr->variables[0]);

	if (instr->intrinsic == nir_intrinsic_var_atomic_comp_swap) {
		LLVMValueRef src1 = get_src(ctx->nir, instr->src[1]);
		result = LLVMBuildAtomicCmpXchg(ctx->builder,
						ptr, src, src1,
						LLVMAtomicOrderingSequentiallyConsistent,
						LLVMAtomicOrderingSequentiallyConsistent,
						false);
	} else {
		LLVMAtomicRMWBinOp op;
		switch (instr->intrinsic) {
		case nir_intrinsic_var_atomic_add:
			op = LLVMAtomicRMWBinOpAdd;
			break;
		case nir_intrinsic_var_atomic_umin:
			op = LLVMAtomicRMWBinOpUMin;
			break;
		case nir_intrinsic_var_atomic_umax:
			op = LLVMAtomicRMWBinOpUMax;
			break;
		case nir_intrinsic_var_atomic_imin:
			op = LLVMAtomicRMWBinOpMin;
			break;
		case nir_intrinsic_var_atomic_imax:
			op = LLVMAtomicRMWBinOpMax;
			break;
		case nir_intrinsic_var_atomic_and:
			op = LLVMAtomicRMWBinOpAnd;
			break;
		case nir_intrinsic_var_atomic_or:
			op = LLVMAtomicRMWBinOpOr;
			break;
		case nir_intrinsic_var_atomic_xor:
			op = LLVMAtomicRMWBinOpXor;
			break;
		case nir_intrinsic_var_atomic_exchange:
			op = LLVMAtomicRMWBinOpXchg;
			break;
		default:
			return NULL;
		}

		result = LLVMBuildAtomicRMW(ctx->builder, op, ptr, ac_to_integer(&ctx->ac, src),
					    LLVMAtomicOrderingSequentiallyConsistent,
					    false);
	}
	return result;
}

static LLVMValueRef lookup_interp_param(struct ac_shader_abi *abi,
					enum glsl_interp_mode interp, unsigned location)
{
	struct nir_to_llvm_context *ctx = nir_to_llvm_context_from_abi(abi);

	switch (interp) {
	case INTERP_MODE_FLAT:
	default:
		return NULL;
	case INTERP_MODE_SMOOTH:
	case INTERP_MODE_NONE:
		if (location == INTERP_CENTER)
			return ctx->persp_center;
		else if (location == INTERP_CENTROID)
			return ctx->persp_centroid;
		else if (location == INTERP_SAMPLE)
			return ctx->persp_sample;
		break;
	case INTERP_MODE_NOPERSPECTIVE:
		if (location == INTERP_CENTER)
			return ctx->linear_center;
		else if (location == INTERP_CENTROID)
			return ctx->linear_centroid;
		else if (location == INTERP_SAMPLE)
			return ctx->linear_sample;
		break;
	}
	return NULL;
}

static LLVMValueRef load_sample_position(struct ac_shader_abi *abi,
					 LLVMValueRef sample_id)
{
	struct nir_to_llvm_context *ctx = nir_to_llvm_context_from_abi(abi);

	LLVMValueRef result;
	LLVMValueRef ptr = ac_build_gep0(&ctx->ac, ctx->ring_offsets, LLVMConstInt(ctx->ac.i32, RING_PS_SAMPLE_POSITIONS, false));

	ptr = LLVMBuildBitCast(ctx->builder, ptr,
			       ac_array_in_const_addr_space(ctx->ac.v2f32), "");

	sample_id = LLVMBuildAdd(ctx->builder, sample_id, ctx->sample_pos_offset, "");
	result = ac_build_load_invariant(&ctx->ac, ptr, sample_id);

	return result;
}

static LLVMValueRef load_sample_pos(struct ac_nir_context *ctx)
{
	LLVMValueRef values[2];

	values[0] = emit_ffract(&ctx->ac, ctx->abi->frag_pos[0], 32);
	values[1] = emit_ffract(&ctx->ac, ctx->abi->frag_pos[1], 32);
	return ac_build_gather_values(&ctx->ac, values, 2);
}

static LLVMValueRef load_sample_mask_in(struct ac_nir_context *ctx)
{
	uint8_t log2_ps_iter_samples = ctx->nctx->shader_info->info.ps.force_persample ? ctx->nctx->options->key.fs.log2_num_samples : ctx->nctx->options->key.fs.log2_ps_iter_samples;

	/* The bit pattern matches that used by fixed function fragment
	 * processing. */
	static const uint16_t ps_iter_masks[] = {
		0xffff, /* not used */
		0x5555,
		0x1111,
		0x0101,
		0x0001,
	};
	assert(log2_ps_iter_samples < ARRAY_SIZE(ps_iter_masks));

	uint32_t ps_iter_mask = ps_iter_masks[log2_ps_iter_samples];

	LLVMValueRef result, sample_id;
	sample_id = unpack_param(&ctx->ac, ctx->abi->ancillary, 8, 4);
	sample_id = LLVMBuildShl(ctx->ac.builder, LLVMConstInt(ctx->ac.i32, ps_iter_mask, false), sample_id, "");
	result = LLVMBuildAnd(ctx->ac.builder, sample_id, ctx->abi->sample_coverage, "");
	return result;
}

static LLVMValueRef visit_interp(struct ac_nir_context *ctx,
				 const nir_intrinsic_instr *instr)
{
	LLVMValueRef result[4];
	LLVMValueRef interp_param, attr_number;
	unsigned location;
	unsigned chan;
	LLVMValueRef src_c0 = NULL;
	LLVMValueRef src_c1 = NULL;
	LLVMValueRef src0 = NULL;
	int input_index = instr->variables[0]->var->data.location - VARYING_SLOT_VAR0;
	switch (instr->intrinsic) {
	case nir_intrinsic_interp_var_at_centroid:
		location = INTERP_CENTROID;
		break;
	case nir_intrinsic_interp_var_at_sample:
	case nir_intrinsic_interp_var_at_offset:
		location = INTERP_CENTER;
		src0 = get_src(ctx, instr->src[0]);
		break;
	default:
		break;
	}

	if (instr->intrinsic == nir_intrinsic_interp_var_at_offset) {
		src_c0 = ac_to_float(&ctx->ac, LLVMBuildExtractElement(ctx->ac.builder, src0, ctx->ac.i32_0, ""));
		src_c1 = ac_to_float(&ctx->ac, LLVMBuildExtractElement(ctx->ac.builder, src0, ctx->ac.i32_1, ""));
	} else if (instr->intrinsic == nir_intrinsic_interp_var_at_sample) {
		LLVMValueRef sample_position;
		LLVMValueRef halfval = LLVMConstReal(ctx->ac.f32, 0.5f);

		/* fetch sample ID */
		sample_position = ctx->abi->load_sample_position(ctx->abi, src0);

		src_c0 = LLVMBuildExtractElement(ctx->ac.builder, sample_position, ctx->ac.i32_0, "");
		src_c0 = LLVMBuildFSub(ctx->ac.builder, src_c0, halfval, "");
		src_c1 = LLVMBuildExtractElement(ctx->ac.builder, sample_position, ctx->ac.i32_1, "");
		src_c1 = LLVMBuildFSub(ctx->ac.builder, src_c1, halfval, "");
	}
	interp_param = ctx->abi->lookup_interp_param(ctx->abi, instr->variables[0]->var->data.interpolation, location);
	attr_number = LLVMConstInt(ctx->ac.i32, input_index, false);

	if (location == INTERP_CENTER) {
		LLVMValueRef ij_out[2];
		LLVMValueRef ddxy_out = emit_ddxy_interp(ctx, interp_param);

		/*
		 * take the I then J parameters, and the DDX/Y for it, and
		 * calculate the IJ inputs for the interpolator.
		 * temp1 = ddx * offset/sample.x + I;
		 * interp_param.I = ddy * offset/sample.y + temp1;
		 * temp1 = ddx * offset/sample.x + J;
		 * interp_param.J = ddy * offset/sample.y + temp1;
		 */
		for (unsigned i = 0; i < 2; i++) {
			LLVMValueRef ix_ll = LLVMConstInt(ctx->ac.i32, i, false);
			LLVMValueRef iy_ll = LLVMConstInt(ctx->ac.i32, i + 2, false);
			LLVMValueRef ddx_el = LLVMBuildExtractElement(ctx->ac.builder,
								      ddxy_out, ix_ll, "");
			LLVMValueRef ddy_el = LLVMBuildExtractElement(ctx->ac.builder,
								      ddxy_out, iy_ll, "");
			LLVMValueRef interp_el = LLVMBuildExtractElement(ctx->ac.builder,
									 interp_param, ix_ll, "");
			LLVMValueRef temp1, temp2;

			interp_el = LLVMBuildBitCast(ctx->ac.builder, interp_el,
						     ctx->ac.f32, "");

			temp1 = LLVMBuildFMul(ctx->ac.builder, ddx_el, src_c0, "");
			temp1 = LLVMBuildFAdd(ctx->ac.builder, temp1, interp_el, "");

			temp2 = LLVMBuildFMul(ctx->ac.builder, ddy_el, src_c1, "");
			temp2 = LLVMBuildFAdd(ctx->ac.builder, temp2, temp1, "");

			ij_out[i] = LLVMBuildBitCast(ctx->ac.builder,
						     temp2, ctx->ac.i32, "");
		}
		interp_param = ac_build_gather_values(&ctx->ac, ij_out, 2);

	}

	for (chan = 0; chan < 4; chan++) {
		LLVMValueRef llvm_chan = LLVMConstInt(ctx->ac.i32, chan, false);

		if (interp_param) {
			interp_param = LLVMBuildBitCast(ctx->ac.builder,
							interp_param, ctx->ac.v2f32, "");
			LLVMValueRef i = LLVMBuildExtractElement(
				ctx->ac.builder, interp_param, ctx->ac.i32_0, "");
			LLVMValueRef j = LLVMBuildExtractElement(
				ctx->ac.builder, interp_param, ctx->ac.i32_1, "");

			result[chan] = ac_build_fs_interp(&ctx->ac,
							  llvm_chan, attr_number,
							  ctx->abi->prim_mask, i, j);
		} else {
			result[chan] = ac_build_fs_interp_mov(&ctx->ac,
							      LLVMConstInt(ctx->ac.i32, 2, false),
							      llvm_chan, attr_number,
							      ctx->abi->prim_mask);
		}
	}
	return ac_build_varying_gather_values(&ctx->ac, result, instr->num_components,
					      instr->variables[0]->var->data.location_frac);
}

static void
visit_emit_vertex(struct ac_shader_abi *abi, unsigned stream, LLVMValueRef *addrs)
{
	LLVMValueRef gs_next_vertex;
	LLVMValueRef can_emit;
	int idx;
	struct nir_to_llvm_context *ctx = nir_to_llvm_context_from_abi(abi);

	assert(stream == 0);

	/* Write vertex attribute values to GSVS ring */
	gs_next_vertex = LLVMBuildLoad(ctx->builder,
				       ctx->gs_next_vertex,
				       "");

	/* If this thread has already emitted the declared maximum number of
	 * vertices, kill it: excessive vertex emissions are not supposed to
	 * have any effect, and GS threads have no externally observable
	 * effects other than emitting vertices.
	 */
	can_emit = LLVMBuildICmp(ctx->builder, LLVMIntULT, gs_next_vertex,
				 LLVMConstInt(ctx->ac.i32, ctx->gs_max_out_vertices, false), "");
	ac_build_kill_if_false(&ctx->ac, can_emit);

	/* loop num outputs */
	idx = 0;
	for (unsigned i = 0; i < RADEON_LLVM_MAX_OUTPUTS; ++i) {
		LLVMValueRef *out_ptr = &addrs[i * 4];
		int length = 4;
		int slot = idx;
		int slot_inc = 1;

		if (!(ctx->output_mask & (1ull << i)))
			continue;

		if (i == VARYING_SLOT_CLIP_DIST0) {
			/* pack clip and cull into a single set of slots */
			length = ctx->num_output_clips + ctx->num_output_culls;
			if (length > 4)
				slot_inc = 2;
		}
		for (unsigned j = 0; j < length; j++) {
			LLVMValueRef out_val = LLVMBuildLoad(ctx->builder,
							     out_ptr[j], "");
			LLVMValueRef voffset = LLVMConstInt(ctx->ac.i32, (slot * 4 + j) * ctx->gs_max_out_vertices, false);
			voffset = LLVMBuildAdd(ctx->builder, voffset, gs_next_vertex, "");
			voffset = LLVMBuildMul(ctx->builder, voffset, LLVMConstInt(ctx->ac.i32, 4, false), "");

			out_val = LLVMBuildBitCast(ctx->builder, out_val, ctx->ac.i32, "");

			ac_build_buffer_store_dword(&ctx->ac, ctx->gsvs_ring,
						    out_val, 1,
						    voffset, ctx->gs2vs_offset, 0,
						    1, 1, true, true);
		}
		idx += slot_inc;
	}

	gs_next_vertex = LLVMBuildAdd(ctx->builder, gs_next_vertex,
				      ctx->ac.i32_1, "");
	LLVMBuildStore(ctx->builder, gs_next_vertex, ctx->gs_next_vertex);

	ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_EMIT | AC_SENDMSG_GS | (0 << 8), ctx->gs_wave_id);
}

static void
visit_end_primitive(struct ac_shader_abi *abi, unsigned stream)
{
	struct nir_to_llvm_context *ctx = nir_to_llvm_context_from_abi(abi);
	ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_CUT | AC_SENDMSG_GS | (stream << 8), ctx->gs_wave_id);
}

static LLVMValueRef
load_tess_coord(struct ac_shader_abi *abi, LLVMTypeRef type,
		unsigned num_components)
{
	struct nir_to_llvm_context *ctx = nir_to_llvm_context_from_abi(abi);

	LLVMValueRef coord[4] = {
		ctx->tes_u,
		ctx->tes_v,
		ctx->ac.f32_0,
		ctx->ac.f32_0,
	};

	if (ctx->tes_primitive_mode == GL_TRIANGLES)
		coord[2] = LLVMBuildFSub(ctx->builder, ctx->ac.f32_1,
					LLVMBuildFAdd(ctx->builder, coord[0], coord[1], ""), "");

	LLVMValueRef result = ac_build_gather_values(&ctx->ac, coord, num_components);
	return LLVMBuildBitCast(ctx->builder, result, type, "");
}

static LLVMValueRef
load_patch_vertices_in(struct ac_shader_abi *abi)
{
	struct nir_to_llvm_context *ctx = nir_to_llvm_context_from_abi(abi);
	return LLVMConstInt(ctx->ac.i32, ctx->options->key.tcs.input_vertices, false);
}

static void visit_intrinsic(struct ac_nir_context *ctx,
                            nir_intrinsic_instr *instr)
{
	LLVMValueRef result = NULL;

	switch (instr->intrinsic) {
	case nir_intrinsic_ballot:
		result = ac_build_ballot(&ctx->ac, get_src(ctx, instr->src[0]));
		break;
	case nir_intrinsic_read_invocation:
	case nir_intrinsic_read_first_invocation: {
		LLVMValueRef args[2];

		/* Value */
		args[0] = get_src(ctx, instr->src[0]);

		unsigned num_args;
		const char *intr_name;
		if (instr->intrinsic == nir_intrinsic_read_invocation) {
			num_args = 2;
			intr_name = "llvm.amdgcn.readlane";

			/* Invocation */
			args[1] = get_src(ctx, instr->src[1]);
		} else {
			num_args = 1;
			intr_name = "llvm.amdgcn.readfirstlane";
		}

		/* We currently have no other way to prevent LLVM from lifting the icmp
		 * calls to a dominating basic block.
		 */
		ac_build_optimization_barrier(&ctx->ac, &args[0]);

		result = ac_build_intrinsic(&ctx->ac, intr_name,
					    ctx->ac.i32, args, num_args,
					    AC_FUNC_ATTR_READNONE |
					    AC_FUNC_ATTR_CONVERGENT);
		break;
	}
	case nir_intrinsic_load_subgroup_invocation:
		result = ac_get_thread_id(&ctx->ac);
		break;
	case nir_intrinsic_load_work_group_id: {
		LLVMValueRef values[3];

		for (int i = 0; i < 3; i++) {
			values[i] = ctx->nctx->workgroup_ids[i] ?
				    ctx->nctx->workgroup_ids[i] : ctx->ac.i32_0;
		}

		result = ac_build_gather_values(&ctx->ac, values, 3);
		break;
	}
	case nir_intrinsic_load_base_vertex: {
		result = ctx->abi->base_vertex;
		break;
	}
	case nir_intrinsic_load_vertex_id_zero_base: {
		result = ctx->abi->vertex_id;
		break;
	}
	case nir_intrinsic_load_local_invocation_id: {
		result = ctx->nctx->local_invocation_ids;
		break;
	}
	case nir_intrinsic_load_base_instance:
		result = ctx->abi->start_instance;
		break;
	case nir_intrinsic_load_draw_id:
		result = ctx->abi->draw_id;
		break;
	case nir_intrinsic_load_view_index:
		result = ctx->nctx->view_index ? ctx->nctx->view_index : ctx->ac.i32_0;
		break;
	case nir_intrinsic_load_invocation_id:
		if (ctx->stage == MESA_SHADER_TESS_CTRL)
			result = unpack_param(&ctx->ac, ctx->abi->tcs_rel_ids, 8, 5);
		else
			result = ctx->abi->gs_invocation_id;
		break;
	case nir_intrinsic_load_primitive_id:
		if (ctx->stage == MESA_SHADER_GEOMETRY) {
			result = ctx->abi->gs_prim_id;
		} else if (ctx->stage == MESA_SHADER_TESS_CTRL) {
			result = ctx->abi->tcs_patch_id;
		} else if (ctx->stage == MESA_SHADER_TESS_EVAL) {
			result = ctx->abi->tes_patch_id;
		} else
			fprintf(stderr, "Unknown primitive id intrinsic: %d", ctx->stage);
		break;
	case nir_intrinsic_load_sample_id:
		result = unpack_param(&ctx->ac, ctx->abi->ancillary, 8, 4);
		break;
	case nir_intrinsic_load_sample_pos:
		result = load_sample_pos(ctx);
		break;
	case nir_intrinsic_load_sample_mask_in:
		if (ctx->nctx)
			result = load_sample_mask_in(ctx);
		else
			result = ctx->abi->sample_coverage;
		break;
	case nir_intrinsic_load_frag_coord: {
		LLVMValueRef values[4] = {
			ctx->abi->frag_pos[0],
			ctx->abi->frag_pos[1],
			ctx->abi->frag_pos[2],
			ac_build_fdiv(&ctx->ac, ctx->ac.f32_1, ctx->abi->frag_pos[3])
		};
		result = ac_build_gather_values(&ctx->ac, values, 4);
		break;
	}
	case nir_intrinsic_load_front_face:
		result = ctx->abi->front_face;
		break;
	case nir_intrinsic_load_helper_invocation:
		result = visit_load_helper_invocation(ctx);
		break;
	case nir_intrinsic_load_instance_id:
		result = ctx->abi->instance_id;
		break;
	case nir_intrinsic_load_num_work_groups:
		result = ctx->nctx->num_work_groups;
		break;
	case nir_intrinsic_load_local_invocation_index:
		result = visit_load_local_invocation_index(ctx->nctx);
		break;
	case nir_intrinsic_load_push_constant:
		result = visit_load_push_constant(ctx->nctx, instr);
		break;
	case nir_intrinsic_vulkan_resource_index:
		result = visit_vulkan_resource_index(ctx->nctx, instr);
		break;
	case nir_intrinsic_vulkan_resource_reindex:
		result = visit_vulkan_resource_reindex(ctx->nctx, instr);
		break;
	case nir_intrinsic_store_ssbo:
		visit_store_ssbo(ctx, instr);
		break;
	case nir_intrinsic_load_ssbo:
		result = visit_load_buffer(ctx, instr);
		break;
	case nir_intrinsic_ssbo_atomic_add:
	case nir_intrinsic_ssbo_atomic_imin:
	case nir_intrinsic_ssbo_atomic_umin:
	case nir_intrinsic_ssbo_atomic_imax:
	case nir_intrinsic_ssbo_atomic_umax:
	case nir_intrinsic_ssbo_atomic_and:
	case nir_intrinsic_ssbo_atomic_or:
	case nir_intrinsic_ssbo_atomic_xor:
	case nir_intrinsic_ssbo_atomic_exchange:
	case nir_intrinsic_ssbo_atomic_comp_swap:
		result = visit_atomic_ssbo(ctx, instr);
		break;
	case nir_intrinsic_load_ubo:
		result = visit_load_ubo_buffer(ctx, instr);
		break;
	case nir_intrinsic_get_buffer_size:
		result = visit_get_buffer_size(ctx, instr);
		break;
	case nir_intrinsic_load_var:
		result = visit_load_var(ctx, instr);
		break;
	case nir_intrinsic_store_var:
		visit_store_var(ctx, instr);
		break;
	case nir_intrinsic_image_load:
		result = visit_image_load(ctx, instr);
		break;
	case nir_intrinsic_image_store:
		visit_image_store(ctx, instr);
		break;
	case nir_intrinsic_image_atomic_add:
	case nir_intrinsic_image_atomic_min:
	case nir_intrinsic_image_atomic_max:
	case nir_intrinsic_image_atomic_and:
	case nir_intrinsic_image_atomic_or:
	case nir_intrinsic_image_atomic_xor:
	case nir_intrinsic_image_atomic_exchange:
	case nir_intrinsic_image_atomic_comp_swap:
		result = visit_image_atomic(ctx, instr);
		break;
	case nir_intrinsic_image_size:
		result = visit_image_size(ctx, instr);
		break;
	case nir_intrinsic_discard:
	case nir_intrinsic_discard_if:
		emit_discard(ctx, instr);
		break;
	case nir_intrinsic_memory_barrier:
	case nir_intrinsic_group_memory_barrier:
	case nir_intrinsic_memory_barrier_atomic_counter:
	case nir_intrinsic_memory_barrier_buffer:
	case nir_intrinsic_memory_barrier_image:
	case nir_intrinsic_memory_barrier_shared:
		emit_membar(ctx->nctx, instr);
		break;
	case nir_intrinsic_barrier:
		emit_barrier(&ctx->ac, ctx->stage);
		break;
	case nir_intrinsic_var_atomic_add:
	case nir_intrinsic_var_atomic_imin:
	case nir_intrinsic_var_atomic_umin:
	case nir_intrinsic_var_atomic_imax:
	case nir_intrinsic_var_atomic_umax:
	case nir_intrinsic_var_atomic_and:
	case nir_intrinsic_var_atomic_or:
	case nir_intrinsic_var_atomic_xor:
	case nir_intrinsic_var_atomic_exchange:
	case nir_intrinsic_var_atomic_comp_swap:
		result = visit_var_atomic(ctx->nctx, instr);
		break;
	case nir_intrinsic_interp_var_at_centroid:
	case nir_intrinsic_interp_var_at_sample:
	case nir_intrinsic_interp_var_at_offset:
		result = visit_interp(ctx, instr);
		break;
	case nir_intrinsic_emit_vertex:
		ctx->abi->emit_vertex(ctx->abi, nir_intrinsic_stream_id(instr), ctx->outputs);
		break;
	case nir_intrinsic_end_primitive:
		ctx->abi->emit_primitive(ctx->abi, nir_intrinsic_stream_id(instr));
		break;
	case nir_intrinsic_load_tess_coord: {
		LLVMTypeRef type = ctx->nctx ?
			get_def_type(ctx->nctx->nir, &instr->dest.ssa) :
			NULL;
		result = ctx->abi->load_tess_coord(ctx->abi, type, instr->num_components);
		break;
	}
	case nir_intrinsic_load_tess_level_outer:
		result = ctx->abi->load_tess_level(ctx->abi, VARYING_SLOT_TESS_LEVEL_OUTER);
		break;
	case nir_intrinsic_load_tess_level_inner:
		result = ctx->abi->load_tess_level(ctx->abi, VARYING_SLOT_TESS_LEVEL_INNER);
		break;
	case nir_intrinsic_load_patch_vertices_in:
		result = ctx->abi->load_patch_vertices_in(ctx->abi);
		break;
	case nir_intrinsic_vote_all: {
		LLVMValueRef tmp = ac_build_vote_all(&ctx->ac, get_src(ctx, instr->src[0]));
		result = LLVMBuildSExt(ctx->ac.builder, tmp, ctx->ac.i32, "");
		break;
	}
	case nir_intrinsic_vote_any: {
		LLVMValueRef tmp = ac_build_vote_any(&ctx->ac, get_src(ctx, instr->src[0]));
		result = LLVMBuildSExt(ctx->ac.builder, tmp, ctx->ac.i32, "");
		break;
	}
	case nir_intrinsic_vote_eq: {
		LLVMValueRef tmp = ac_build_vote_eq(&ctx->ac, get_src(ctx, instr->src[0]));
		result = LLVMBuildSExt(ctx->ac.builder, tmp, ctx->ac.i32, "");
		break;
	}
	default:
		fprintf(stderr, "Unknown intrinsic: ");
		nir_print_instr(&instr->instr, stderr);
		fprintf(stderr, "\n");
		break;
	}
	if (result) {
		_mesa_hash_table_insert(ctx->defs, &instr->dest.ssa, result);
	}
}

static LLVMValueRef radv_load_ssbo(struct ac_shader_abi *abi,
				   LLVMValueRef buffer_ptr, bool write)
{
	struct nir_to_llvm_context *ctx = nir_to_llvm_context_from_abi(abi);
	LLVMValueRef result;

	LLVMSetMetadata(buffer_ptr, ctx->ac.uniform_md_kind, ctx->ac.empty_md);

	result = LLVMBuildLoad(ctx->builder, buffer_ptr, "");
	LLVMSetMetadata(result, ctx->ac.invariant_load_md_kind, ctx->ac.empty_md);

	return result;
}

static LLVMValueRef radv_load_ubo(struct ac_shader_abi *abi, LLVMValueRef buffer_ptr)
{
	struct nir_to_llvm_context *ctx = nir_to_llvm_context_from_abi(abi);
	LLVMValueRef result;

	LLVMSetMetadata(buffer_ptr, ctx->ac.uniform_md_kind, ctx->ac.empty_md);

	result = LLVMBuildLoad(ctx->builder, buffer_ptr, "");
	LLVMSetMetadata(result, ctx->ac.invariant_load_md_kind, ctx->ac.empty_md);

	return result;
}

static LLVMValueRef radv_get_sampler_desc(struct ac_shader_abi *abi,
					  unsigned descriptor_set,
					  unsigned base_index,
					  unsigned constant_index,
					  LLVMValueRef index,
					  enum ac_descriptor_type desc_type,
					  bool image, bool write)
{
	struct nir_to_llvm_context *ctx = nir_to_llvm_context_from_abi(abi);
	LLVMValueRef list = ctx->descriptor_sets[descriptor_set];
	struct radv_descriptor_set_layout *layout = ctx->options->layout->set[descriptor_set].layout;
	struct radv_descriptor_set_binding_layout *binding = layout->binding + base_index;
	unsigned offset = binding->offset;
	unsigned stride = binding->size;
	unsigned type_size;
	LLVMBuilderRef builder = ctx->builder;
	LLVMTypeRef type;

	assert(base_index < layout->binding_count);

	switch (desc_type) {
	case AC_DESC_IMAGE:
		type = ctx->ac.v8i32;
		type_size = 32;
		break;
	case AC_DESC_FMASK:
		type = ctx->ac.v8i32;
		offset += 32;
		type_size = 32;
		break;
	case AC_DESC_SAMPLER:
		type = ctx->ac.v4i32;
		if (binding->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			offset += 64;

		type_size = 16;
		break;
	case AC_DESC_BUFFER:
		type = ctx->ac.v4i32;
		type_size = 16;
		break;
	default:
		unreachable("invalid desc_type\n");
	}

	offset += constant_index * stride;

	if (desc_type == AC_DESC_SAMPLER && binding->immutable_samplers_offset &&
	    (!index || binding->immutable_samplers_equal)) {
		if (binding->immutable_samplers_equal)
			constant_index = 0;

		const uint32_t *samplers = radv_immutable_samplers(layout, binding);

		LLVMValueRef constants[] = {
			LLVMConstInt(ctx->ac.i32, samplers[constant_index * 4 + 0], 0),
			LLVMConstInt(ctx->ac.i32, samplers[constant_index * 4 + 1], 0),
			LLVMConstInt(ctx->ac.i32, samplers[constant_index * 4 + 2], 0),
			LLVMConstInt(ctx->ac.i32, samplers[constant_index * 4 + 3], 0),
		};
		return ac_build_gather_values(&ctx->ac, constants, 4);
	}

	assert(stride % type_size == 0);

	if (!index)
		index = ctx->ac.i32_0;

	index = LLVMBuildMul(builder, index, LLVMConstInt(ctx->ac.i32, stride / type_size, 0), "");

	list = ac_build_gep0(&ctx->ac, list, LLVMConstInt(ctx->ac.i32, offset, 0));
	list = LLVMBuildPointerCast(builder, list, ac_array_in_const_addr_space(type), "");

	return ac_build_load_to_sgpr(&ctx->ac, list, index);
}

static LLVMValueRef get_sampler_desc(struct ac_nir_context *ctx,
				     const nir_deref_var *deref,
				     enum ac_descriptor_type desc_type,
				     const nir_tex_instr *tex_instr,
				     bool image, bool write)
{
	LLVMValueRef index = NULL;
	unsigned constant_index = 0;
	unsigned descriptor_set;
	unsigned base_index;

	if (!deref) {
		assert(tex_instr && !image);
		descriptor_set = 0;
		base_index = tex_instr->sampler_index;
	} else {
		const nir_deref *tail = &deref->deref;
		while (tail->child) {
			const nir_deref_array *child = nir_deref_as_array(tail->child);
			unsigned array_size = glsl_get_aoa_size(tail->child->type);

			if (!array_size)
				array_size = 1;

			assert(child->deref_array_type != nir_deref_array_type_wildcard);

			if (child->deref_array_type == nir_deref_array_type_indirect) {
				LLVMValueRef indirect = get_src(ctx, child->indirect);

				indirect = LLVMBuildMul(ctx->ac.builder, indirect,
					LLVMConstInt(ctx->ac.i32, array_size, false), "");

				if (!index)
					index = indirect;
				else
					index = LLVMBuildAdd(ctx->ac.builder, index, indirect, "");
			}

			constant_index += child->base_offset * array_size;

			tail = &child->deref;
		}
		descriptor_set = deref->var->data.descriptor_set;
		base_index = deref->var->data.binding;
	}

	return ctx->abi->load_sampler_desc(ctx->abi,
					  descriptor_set,
					  base_index,
					  constant_index, index,
					  desc_type, image, write);
}

static void set_tex_fetch_args(struct ac_llvm_context *ctx,
			       struct ac_image_args *args,
			       const nir_tex_instr *instr,
			       nir_texop op,
			       LLVMValueRef res_ptr, LLVMValueRef samp_ptr,
			       LLVMValueRef *param, unsigned count,
			       unsigned dmask)
{
	unsigned is_rect = 0;
	bool da = instr->is_array || instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE;

	if (op == nir_texop_lod)
		da = false;
	/* Pad to power of two vector */
	while (count < util_next_power_of_two(count))
		param[count++] = LLVMGetUndef(ctx->i32);

	if (count > 1)
		args->addr = ac_build_gather_values(ctx, param, count);
	else
		args->addr = param[0];

	args->resource = res_ptr;
	args->sampler = samp_ptr;

	if (instr->sampler_dim == GLSL_SAMPLER_DIM_BUF && op == nir_texop_txf) {
		args->addr = param[0];
		return;
	}

	args->dmask = dmask;
	args->unorm = is_rect;
	args->da = da;
}

/* Disable anisotropic filtering if BASE_LEVEL == LAST_LEVEL.
 *
 * SI-CI:
 *   If BASE_LEVEL == LAST_LEVEL, the shader must disable anisotropic
 *   filtering manually. The driver sets img7 to a mask clearing
 *   MAX_ANISO_RATIO if BASE_LEVEL == LAST_LEVEL. The shader must do:
 *     s_and_b32 samp0, samp0, img7
 *
 * VI:
 *   The ANISO_OVERRIDE sampler field enables this fix in TA.
 */
static LLVMValueRef sici_fix_sampler_aniso(struct ac_nir_context *ctx,
                                           LLVMValueRef res, LLVMValueRef samp)
{
	LLVMBuilderRef builder = ctx->ac.builder;
	LLVMValueRef img7, samp0;

	if (ctx->ac.chip_class >= VI)
		return samp;

	img7 = LLVMBuildExtractElement(builder, res,
	                               LLVMConstInt(ctx->ac.i32, 7, 0), "");
	samp0 = LLVMBuildExtractElement(builder, samp,
	                                LLVMConstInt(ctx->ac.i32, 0, 0), "");
	samp0 = LLVMBuildAnd(builder, samp0, img7, "");
	return LLVMBuildInsertElement(builder, samp, samp0,
	                              LLVMConstInt(ctx->ac.i32, 0, 0), "");
}

static void tex_fetch_ptrs(struct ac_nir_context *ctx,
			   nir_tex_instr *instr,
			   LLVMValueRef *res_ptr, LLVMValueRef *samp_ptr,
			   LLVMValueRef *fmask_ptr)
{
	if (instr->sampler_dim  == GLSL_SAMPLER_DIM_BUF)
		*res_ptr = get_sampler_desc(ctx, instr->texture, AC_DESC_BUFFER, instr, false, false);
	else
		*res_ptr = get_sampler_desc(ctx, instr->texture, AC_DESC_IMAGE, instr, false, false);
	if (samp_ptr) {
		if (instr->sampler)
			*samp_ptr = get_sampler_desc(ctx, instr->sampler, AC_DESC_SAMPLER, instr, false, false);
		else
			*samp_ptr = get_sampler_desc(ctx, instr->texture, AC_DESC_SAMPLER, instr, false, false);
		if (instr->sampler_dim < GLSL_SAMPLER_DIM_RECT)
			*samp_ptr = sici_fix_sampler_aniso(ctx, *res_ptr, *samp_ptr);
	}
	if (fmask_ptr && !instr->sampler && (instr->op == nir_texop_txf_ms ||
					     instr->op == nir_texop_samples_identical))
		*fmask_ptr = get_sampler_desc(ctx, instr->texture, AC_DESC_FMASK, instr, false, false);
}

static LLVMValueRef apply_round_slice(struct ac_llvm_context *ctx,
				      LLVMValueRef coord)
{
	coord = ac_to_float(ctx, coord);
	coord = ac_build_intrinsic(ctx, "llvm.rint.f32", ctx->f32, &coord, 1, 0);
	coord = ac_to_integer(ctx, coord);
	return coord;
}

static void visit_tex(struct ac_nir_context *ctx, nir_tex_instr *instr)
{
	LLVMValueRef result = NULL;
	struct ac_image_args args = { 0 };
	unsigned dmask = 0xf;
	LLVMValueRef address[16];
	LLVMValueRef coords[5];
	LLVMValueRef coord = NULL, lod = NULL, comparator = NULL;
	LLVMValueRef bias = NULL, offsets = NULL;
	LLVMValueRef res_ptr, samp_ptr, fmask_ptr = NULL, sample_index = NULL;
	LLVMValueRef ddx = NULL, ddy = NULL;
	LLVMValueRef derivs[6];
	unsigned chan, count = 0;
	unsigned const_src = 0, num_deriv_comp = 0;
	bool lod_is_zero = false;

	tex_fetch_ptrs(ctx, instr, &res_ptr, &samp_ptr, &fmask_ptr);

	for (unsigned i = 0; i < instr->num_srcs; i++) {
		switch (instr->src[i].src_type) {
		case nir_tex_src_coord:
			coord = get_src(ctx, instr->src[i].src);
			break;
		case nir_tex_src_projector:
			break;
		case nir_tex_src_comparator:
			comparator = get_src(ctx, instr->src[i].src);
			break;
		case nir_tex_src_offset:
			offsets = get_src(ctx, instr->src[i].src);
			const_src = i;
			break;
		case nir_tex_src_bias:
			bias = get_src(ctx, instr->src[i].src);
			break;
		case nir_tex_src_lod: {
			nir_const_value *val = nir_src_as_const_value(instr->src[i].src);

			if (val && val->i32[0] == 0)
				lod_is_zero = true;
			lod = get_src(ctx, instr->src[i].src);
			break;
		}
		case nir_tex_src_ms_index:
			sample_index = get_src(ctx, instr->src[i].src);
			break;
		case nir_tex_src_ms_mcs:
			break;
		case nir_tex_src_ddx:
			ddx = get_src(ctx, instr->src[i].src);
			num_deriv_comp = instr->src[i].src.ssa->num_components;
			break;
		case nir_tex_src_ddy:
			ddy = get_src(ctx, instr->src[i].src);
			break;
		case nir_tex_src_texture_offset:
		case nir_tex_src_sampler_offset:
		case nir_tex_src_plane:
		default:
			break;
		}
	}

	if (instr->op == nir_texop_txs && instr->sampler_dim == GLSL_SAMPLER_DIM_BUF) {
		result = get_buffer_size(ctx, res_ptr, true);
		goto write_result;
	}

	if (instr->op == nir_texop_texture_samples) {
		LLVMValueRef res, samples, is_msaa;
		res = LLVMBuildBitCast(ctx->ac.builder, res_ptr, ctx->ac.v8i32, "");
		samples = LLVMBuildExtractElement(ctx->ac.builder, res,
						  LLVMConstInt(ctx->ac.i32, 3, false), "");
		is_msaa = LLVMBuildLShr(ctx->ac.builder, samples,
					LLVMConstInt(ctx->ac.i32, 28, false), "");
		is_msaa = LLVMBuildAnd(ctx->ac.builder, is_msaa,
				       LLVMConstInt(ctx->ac.i32, 0xe, false), "");
		is_msaa = LLVMBuildICmp(ctx->ac.builder, LLVMIntEQ, is_msaa,
					LLVMConstInt(ctx->ac.i32, 0xe, false), "");

		samples = LLVMBuildLShr(ctx->ac.builder, samples,
					LLVMConstInt(ctx->ac.i32, 16, false), "");
		samples = LLVMBuildAnd(ctx->ac.builder, samples,
				       LLVMConstInt(ctx->ac.i32, 0xf, false), "");
		samples = LLVMBuildShl(ctx->ac.builder, ctx->ac.i32_1,
				       samples, "");
		samples = LLVMBuildSelect(ctx->ac.builder, is_msaa, samples,
					  ctx->ac.i32_1, "");
		result = samples;
		goto write_result;
	}

	if (coord)
		for (chan = 0; chan < instr->coord_components; chan++)
			coords[chan] = ac_llvm_extract_elem(&ctx->ac, coord, chan);

	if (offsets && instr->op != nir_texop_txf) {
		LLVMValueRef offset[3], pack;
		for (chan = 0; chan < 3; ++chan)
			offset[chan] = ctx->ac.i32_0;

		args.offset = true;
		for (chan = 0; chan < ac_get_llvm_num_components(offsets); chan++) {
			offset[chan] = ac_llvm_extract_elem(&ctx->ac, offsets, chan);
			offset[chan] = LLVMBuildAnd(ctx->ac.builder, offset[chan],
						    LLVMConstInt(ctx->ac.i32, 0x3f, false), "");
			if (chan)
				offset[chan] = LLVMBuildShl(ctx->ac.builder, offset[chan],
							    LLVMConstInt(ctx->ac.i32, chan * 8, false), "");
		}
		pack = LLVMBuildOr(ctx->ac.builder, offset[0], offset[1], "");
		pack = LLVMBuildOr(ctx->ac.builder, pack, offset[2], "");
		address[count++] = pack;

	}
	/* pack LOD bias value */
	if (instr->op == nir_texop_txb && bias) {
		address[count++] = bias;
	}

	/* Pack depth comparison value */
	if (instr->is_shadow && comparator) {
		LLVMValueRef z = ac_to_float(&ctx->ac,
		                             ac_llvm_extract_elem(&ctx->ac, comparator, 0));

		/* TC-compatible HTILE on radeonsi promotes Z16 and Z24 to Z32_FLOAT,
		 * so the depth comparison value isn't clamped for Z16 and
		 * Z24 anymore. Do it manually here.
		 *
		 * It's unnecessary if the original texture format was
		 * Z32_FLOAT, but we don't know that here.
		 */
		if (ctx->ac.chip_class == VI && ctx->abi->clamp_shadow_reference)
			z = ac_build_clamp(&ctx->ac, z);

		address[count++] = z;
	}

	/* pack derivatives */
	if (ddx || ddy) {
		int num_src_deriv_channels, num_dest_deriv_channels;
		switch (instr->sampler_dim) {
		case GLSL_SAMPLER_DIM_3D:
		case GLSL_SAMPLER_DIM_CUBE:
			num_deriv_comp = 3;
			num_src_deriv_channels = 3;
			num_dest_deriv_channels = 3;
			break;
		case GLSL_SAMPLER_DIM_2D:
		default:
			num_src_deriv_channels = 2;
			num_dest_deriv_channels = 2;
			num_deriv_comp = 2;
			break;
		case GLSL_SAMPLER_DIM_1D:
			num_src_deriv_channels = 1;
			if (ctx->ac.chip_class >= GFX9) {
				num_dest_deriv_channels = 2;
				num_deriv_comp = 2;
			} else {
				num_dest_deriv_channels = 1;
				num_deriv_comp = 1;
			}
			break;
		}

		for (unsigned i = 0; i < num_src_deriv_channels; i++) {
			derivs[i] = ac_to_float(&ctx->ac, ac_llvm_extract_elem(&ctx->ac, ddx, i));
			derivs[num_dest_deriv_channels + i] = ac_to_float(&ctx->ac, ac_llvm_extract_elem(&ctx->ac, ddy, i));
		}
		for (unsigned i = num_src_deriv_channels; i < num_dest_deriv_channels; i++) {
			derivs[i] = ctx->ac.f32_0;
			derivs[num_dest_deriv_channels + i] = ctx->ac.f32_0;
		}
	}

	if (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE && coord) {
		for (chan = 0; chan < instr->coord_components; chan++)
			coords[chan] = ac_to_float(&ctx->ac, coords[chan]);
		if (instr->coord_components == 3)
			coords[3] = LLVMGetUndef(ctx->ac.f32);
		ac_prepare_cube_coords(&ctx->ac,
			instr->op == nir_texop_txd, instr->is_array,
			instr->op == nir_texop_lod, coords, derivs);
		if (num_deriv_comp)
			num_deriv_comp--;
	}

	if (ddx || ddy) {
		for (unsigned i = 0; i < num_deriv_comp * 2; i++)
			address[count++] = derivs[i];
	}

	/* Pack texture coordinates */
	if (coord) {
		address[count++] = coords[0];
		if (instr->coord_components > 1) {
			if (instr->sampler_dim == GLSL_SAMPLER_DIM_1D && instr->is_array && instr->op != nir_texop_txf) {
				coords[1] = apply_round_slice(&ctx->ac, coords[1]);
			}
			address[count++] = coords[1];
		}
		if (instr->coord_components > 2) {
			/* This seems like a bit of a hack - but it passes Vulkan CTS with it */
			if (instr->sampler_dim != GLSL_SAMPLER_DIM_3D &&
			    instr->sampler_dim != GLSL_SAMPLER_DIM_CUBE &&
			    instr->op != nir_texop_txf) {
				coords[2] = apply_round_slice(&ctx->ac, coords[2]);
			}
			address[count++] = coords[2];
		}

		if (ctx->ac.chip_class >= GFX9) {
			LLVMValueRef filler;
			if (instr->op == nir_texop_txf)
				filler = ctx->ac.i32_0;
			else
				filler = LLVMConstReal(ctx->ac.f32, 0.5);

			if (instr->sampler_dim == GLSL_SAMPLER_DIM_1D) {
				/* No nir_texop_lod, because it does not take a slice
				 * even with array textures. */
				if (instr->is_array && instr->op != nir_texop_lod ) {
					address[count] = address[count - 1];
					address[count - 1] = filler;
					count++;
				} else
					address[count++] = filler;
			}
		}
	}

	/* Pack LOD */
	if (lod && ((instr->op == nir_texop_txl && !lod_is_zero) ||
		    instr->op == nir_texop_txf)) {
		address[count++] = lod;
	} else if (instr->op == nir_texop_txf_ms && sample_index) {
		address[count++] = sample_index;
	} else if(instr->op == nir_texop_txs) {
		count = 0;
		if (lod)
			address[count++] = lod;
		else
			address[count++] = ctx->ac.i32_0;
	}

	for (chan = 0; chan < count; chan++) {
		address[chan] = LLVMBuildBitCast(ctx->ac.builder,
						 address[chan], ctx->ac.i32, "");
	}

	if (instr->op == nir_texop_samples_identical) {
		LLVMValueRef txf_address[4];
		struct ac_image_args txf_args = { 0 };
		unsigned txf_count = count;
		memcpy(txf_address, address, sizeof(txf_address));

		if (!instr->is_array)
			txf_address[2] = ctx->ac.i32_0;
		txf_address[3] = ctx->ac.i32_0;

		set_tex_fetch_args(&ctx->ac, &txf_args, instr, nir_texop_txf,
				   fmask_ptr, NULL,
				   txf_address, txf_count, 0xf);

		result = build_tex_intrinsic(ctx, instr, false, &txf_args);

		result = LLVMBuildExtractElement(ctx->ac.builder, result, ctx->ac.i32_0, "");
		result = emit_int_cmp(&ctx->ac, LLVMIntEQ, result, ctx->ac.i32_0);
		goto write_result;
	}

	if (instr->sampler_dim == GLSL_SAMPLER_DIM_MS &&
	    instr->op != nir_texop_txs) {
		unsigned sample_chan = instr->is_array ? 3 : 2;
		address[sample_chan] = adjust_sample_index_using_fmask(&ctx->ac,
								       address[0],
								       address[1],
								       instr->is_array ? address[2] : NULL,
								       address[sample_chan],
								       fmask_ptr);
	}

	if (offsets && instr->op == nir_texop_txf) {
		nir_const_value *const_offset =
			nir_src_as_const_value(instr->src[const_src].src);
		int num_offsets = instr->src[const_src].src.ssa->num_components;
		assert(const_offset);
		num_offsets = MIN2(num_offsets, instr->coord_components);
		if (num_offsets > 2)
			address[2] = LLVMBuildAdd(ctx->ac.builder,
						  address[2], LLVMConstInt(ctx->ac.i32, const_offset->i32[2], false), "");
		if (num_offsets > 1)
			address[1] = LLVMBuildAdd(ctx->ac.builder,
						  address[1], LLVMConstInt(ctx->ac.i32, const_offset->i32[1], false), "");
		address[0] = LLVMBuildAdd(ctx->ac.builder,
					  address[0], LLVMConstInt(ctx->ac.i32, const_offset->i32[0], false), "");

	}

	/* TODO TG4 support */
	if (instr->op == nir_texop_tg4) {
		if (instr->is_shadow)
			dmask = 1;
		else
			dmask = 1 << instr->component;
	}
	set_tex_fetch_args(&ctx->ac, &args, instr, instr->op,
			   res_ptr, samp_ptr, address, count, dmask);

	result = build_tex_intrinsic(ctx, instr, lod_is_zero, &args);

	if (instr->op == nir_texop_query_levels)
		result = LLVMBuildExtractElement(ctx->ac.builder, result, LLVMConstInt(ctx->ac.i32, 3, false), "");
	else if (instr->is_shadow && instr->is_new_style_shadow &&
		 instr->op != nir_texop_txs && instr->op != nir_texop_lod &&
		 instr->op != nir_texop_tg4)
		result = LLVMBuildExtractElement(ctx->ac.builder, result, ctx->ac.i32_0, "");
	else if (instr->op == nir_texop_txs &&
		 instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE &&
		 instr->is_array) {
		LLVMValueRef two = LLVMConstInt(ctx->ac.i32, 2, false);
		LLVMValueRef six = LLVMConstInt(ctx->ac.i32, 6, false);
		LLVMValueRef z = LLVMBuildExtractElement(ctx->ac.builder, result, two, "");
		z = LLVMBuildSDiv(ctx->ac.builder, z, six, "");
		result = LLVMBuildInsertElement(ctx->ac.builder, result, z, two, "");
	} else if (ctx->ac.chip_class >= GFX9 &&
		   instr->op == nir_texop_txs &&
		   instr->sampler_dim == GLSL_SAMPLER_DIM_1D &&
		   instr->is_array) {
		LLVMValueRef two = LLVMConstInt(ctx->ac.i32, 2, false);
		LLVMValueRef layers = LLVMBuildExtractElement(ctx->ac.builder, result, two, "");
		result = LLVMBuildInsertElement(ctx->ac.builder, result, layers,
						ctx->ac.i32_1, "");
	} else if (instr->dest.ssa.num_components != 4)
		result = trim_vector(&ctx->ac, result, instr->dest.ssa.num_components);

write_result:
	if (result) {
		assert(instr->dest.is_ssa);
		result = ac_to_integer(&ctx->ac, result);
		_mesa_hash_table_insert(ctx->defs, &instr->dest.ssa, result);
	}
}


static void visit_phi(struct ac_nir_context *ctx, nir_phi_instr *instr)
{
	LLVMTypeRef type = get_def_type(ctx, &instr->dest.ssa);
	LLVMValueRef result = LLVMBuildPhi(ctx->ac.builder, type, "");

	_mesa_hash_table_insert(ctx->defs, &instr->dest.ssa, result);
	_mesa_hash_table_insert(ctx->phis, instr, result);
}

static void visit_post_phi(struct ac_nir_context *ctx,
                           nir_phi_instr *instr,
                           LLVMValueRef llvm_phi)
{
	nir_foreach_phi_src(src, instr) {
		LLVMBasicBlockRef block = get_block(ctx, src->pred);
		LLVMValueRef llvm_src = get_src(ctx, src->src);

		LLVMAddIncoming(llvm_phi, &llvm_src, &block, 1);
	}
}

static void phi_post_pass(struct ac_nir_context *ctx)
{
	struct hash_entry *entry;
	hash_table_foreach(ctx->phis, entry) {
		visit_post_phi(ctx, (nir_phi_instr*)entry->key,
		               (LLVMValueRef)entry->data);
	}
}


static void visit_ssa_undef(struct ac_nir_context *ctx,
			    const nir_ssa_undef_instr *instr)
{
	unsigned num_components = instr->def.num_components;
	LLVMTypeRef type = LLVMIntTypeInContext(ctx->ac.context, instr->def.bit_size);
	LLVMValueRef undef;

	if (num_components == 1)
		undef = LLVMGetUndef(type);
	else {
		undef = LLVMGetUndef(LLVMVectorType(type, num_components));
	}
	_mesa_hash_table_insert(ctx->defs, &instr->def, undef);
}

static void visit_jump(struct ac_nir_context *ctx,
		       const nir_jump_instr *instr)
{
	switch (instr->type) {
	case nir_jump_break:
		LLVMBuildBr(ctx->ac.builder, ctx->break_block);
		LLVMClearInsertionPosition(ctx->ac.builder);
		break;
	case nir_jump_continue:
		LLVMBuildBr(ctx->ac.builder, ctx->continue_block);
		LLVMClearInsertionPosition(ctx->ac.builder);
		break;
	default:
		fprintf(stderr, "Unknown NIR jump instr: ");
		nir_print_instr(&instr->instr, stderr);
		fprintf(stderr, "\n");
		abort();
	}
}

static void visit_cf_list(struct ac_nir_context *ctx,
                          struct exec_list *list);

static void visit_block(struct ac_nir_context *ctx, nir_block *block)
{
	LLVMBasicBlockRef llvm_block = LLVMGetInsertBlock(ctx->ac.builder);
	nir_foreach_instr(instr, block)
	{
		switch (instr->type) {
		case nir_instr_type_alu:
			visit_alu(ctx, nir_instr_as_alu(instr));
			break;
		case nir_instr_type_load_const:
			visit_load_const(ctx, nir_instr_as_load_const(instr));
			break;
		case nir_instr_type_intrinsic:
			visit_intrinsic(ctx, nir_instr_as_intrinsic(instr));
			break;
		case nir_instr_type_tex:
			visit_tex(ctx, nir_instr_as_tex(instr));
			break;
		case nir_instr_type_phi:
			visit_phi(ctx, nir_instr_as_phi(instr));
			break;
		case nir_instr_type_ssa_undef:
			visit_ssa_undef(ctx, nir_instr_as_ssa_undef(instr));
			break;
		case nir_instr_type_jump:
			visit_jump(ctx, nir_instr_as_jump(instr));
			break;
		default:
			fprintf(stderr, "Unknown NIR instr type: ");
			nir_print_instr(instr, stderr);
			fprintf(stderr, "\n");
			abort();
		}
	}

	_mesa_hash_table_insert(ctx->defs, block, llvm_block);
}

static void visit_if(struct ac_nir_context *ctx, nir_if *if_stmt)
{
	LLVMValueRef value = get_src(ctx, if_stmt->condition);

	LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->ac.builder));
	LLVMBasicBlockRef merge_block =
	    LLVMAppendBasicBlockInContext(ctx->ac.context, fn, "");
	LLVMBasicBlockRef if_block =
	    LLVMAppendBasicBlockInContext(ctx->ac.context, fn, "");
	LLVMBasicBlockRef else_block = merge_block;
	if (!exec_list_is_empty(&if_stmt->else_list))
		else_block = LLVMAppendBasicBlockInContext(
		    ctx->ac.context, fn, "");

	LLVMValueRef cond = LLVMBuildICmp(ctx->ac.builder, LLVMIntNE, value,
	                                  ctx->ac.i32_0, "");
	LLVMBuildCondBr(ctx->ac.builder, cond, if_block, else_block);

	LLVMPositionBuilderAtEnd(ctx->ac.builder, if_block);
	visit_cf_list(ctx, &if_stmt->then_list);
	if (LLVMGetInsertBlock(ctx->ac.builder))
		LLVMBuildBr(ctx->ac.builder, merge_block);

	if (!exec_list_is_empty(&if_stmt->else_list)) {
		LLVMPositionBuilderAtEnd(ctx->ac.builder, else_block);
		visit_cf_list(ctx, &if_stmt->else_list);
		if (LLVMGetInsertBlock(ctx->ac.builder))
			LLVMBuildBr(ctx->ac.builder, merge_block);
	}

	LLVMPositionBuilderAtEnd(ctx->ac.builder, merge_block);
}

static void visit_loop(struct ac_nir_context *ctx, nir_loop *loop)
{
	LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx->ac.builder));
	LLVMBasicBlockRef continue_parent = ctx->continue_block;
	LLVMBasicBlockRef break_parent = ctx->break_block;

	ctx->continue_block =
	    LLVMAppendBasicBlockInContext(ctx->ac.context, fn, "");
	ctx->break_block =
	    LLVMAppendBasicBlockInContext(ctx->ac.context, fn, "");

	LLVMBuildBr(ctx->ac.builder, ctx->continue_block);
	LLVMPositionBuilderAtEnd(ctx->ac.builder, ctx->continue_block);
	visit_cf_list(ctx, &loop->body);

	if (LLVMGetInsertBlock(ctx->ac.builder))
		LLVMBuildBr(ctx->ac.builder, ctx->continue_block);
	LLVMPositionBuilderAtEnd(ctx->ac.builder, ctx->break_block);

	ctx->continue_block = continue_parent;
	ctx->break_block = break_parent;
}

static void visit_cf_list(struct ac_nir_context *ctx,
                          struct exec_list *list)
{
	foreach_list_typed(nir_cf_node, node, node, list)
	{
		switch (node->type) {
		case nir_cf_node_block:
			visit_block(ctx, nir_cf_node_as_block(node));
			break;

		case nir_cf_node_if:
			visit_if(ctx, nir_cf_node_as_if(node));
			break;

		case nir_cf_node_loop:
			visit_loop(ctx, nir_cf_node_as_loop(node));
			break;

		default:
			assert(0);
		}
	}
}

static void
handle_vs_input_decl(struct nir_to_llvm_context *ctx,
		     struct nir_variable *variable)
{
	LLVMValueRef t_list_ptr = ctx->vertex_buffers;
	LLVMValueRef t_offset;
	LLVMValueRef t_list;
	LLVMValueRef input;
	LLVMValueRef buffer_index;
	int index = variable->data.location - VERT_ATTRIB_GENERIC0;
	int idx = variable->data.location;
	unsigned attrib_count = glsl_count_attribute_slots(variable->type, true);

	variable->data.driver_location = idx * 4;

	for (unsigned i = 0; i < attrib_count; ++i, ++idx) {
		if (ctx->options->key.vs.instance_rate_inputs & (1u << (index + i))) {
			buffer_index = LLVMBuildAdd(ctx->builder, ctx->abi.instance_id,
			                            ctx->abi.start_instance, "");
			if (ctx->options->key.vs.as_ls) {
				ctx->shader_info->vs.vgpr_comp_cnt =
					MAX2(2, ctx->shader_info->vs.vgpr_comp_cnt);
			} else {
				ctx->shader_info->vs.vgpr_comp_cnt =
					MAX2(1, ctx->shader_info->vs.vgpr_comp_cnt);
			}
		} else
			buffer_index = LLVMBuildAdd(ctx->builder, ctx->abi.vertex_id,
			                            ctx->abi.base_vertex, "");
		t_offset = LLVMConstInt(ctx->ac.i32, index + i, false);

		t_list = ac_build_load_to_sgpr(&ctx->ac, t_list_ptr, t_offset);

		input = ac_build_buffer_load_format(&ctx->ac, t_list,
						    buffer_index,
						    ctx->ac.i32_0,
						    4, false, true);

		for (unsigned chan = 0; chan < 4; chan++) {
			LLVMValueRef llvm_chan = LLVMConstInt(ctx->ac.i32, chan, false);
			ctx->inputs[radeon_llvm_reg_index_soa(idx, chan)] =
				ac_to_integer(&ctx->ac, LLVMBuildExtractElement(ctx->builder,
							input, llvm_chan, ""));
		}
	}
}

static void interp_fs_input(struct nir_to_llvm_context *ctx,
			    unsigned attr,
			    LLVMValueRef interp_param,
			    LLVMValueRef prim_mask,
			    LLVMValueRef result[4])
{
	LLVMValueRef attr_number;
	unsigned chan;
	LLVMValueRef i, j;
	bool interp = interp_param != NULL;

	attr_number = LLVMConstInt(ctx->ac.i32, attr, false);

	/* fs.constant returns the param from the middle vertex, so it's not
	 * really useful for flat shading. It's meant to be used for custom
	 * interpolation (but the intrinsic can't fetch from the other two
	 * vertices).
	 *
	 * Luckily, it doesn't matter, because we rely on the FLAT_SHADE state
	 * to do the right thing. The only reason we use fs.constant is that
	 * fs.interp cannot be used on integers, because they can be equal
	 * to NaN.
	 */
	if (interp) {
		interp_param = LLVMBuildBitCast(ctx->builder, interp_param,
						ctx->ac.v2f32, "");

		i = LLVMBuildExtractElement(ctx->builder, interp_param,
						ctx->ac.i32_0, "");
		j = LLVMBuildExtractElement(ctx->builder, interp_param,
						ctx->ac.i32_1, "");
	}

	for (chan = 0; chan < 4; chan++) {
		LLVMValueRef llvm_chan = LLVMConstInt(ctx->ac.i32, chan, false);

		if (interp) {
			result[chan] = ac_build_fs_interp(&ctx->ac,
							  llvm_chan,
							  attr_number,
							  prim_mask, i, j);
		} else {
			result[chan] = ac_build_fs_interp_mov(&ctx->ac,
							      LLVMConstInt(ctx->ac.i32, 2, false),
							      llvm_chan,
							      attr_number,
							      prim_mask);
		}
	}
}

static void
handle_fs_input_decl(struct nir_to_llvm_context *ctx,
		     struct nir_variable *variable)
{
	int idx = variable->data.location;
	unsigned attrib_count = glsl_count_attribute_slots(variable->type, false);
	LLVMValueRef interp;

	variable->data.driver_location = idx * 4;
	ctx->input_mask |= ((1ull << attrib_count) - 1) << variable->data.location;

	if (glsl_get_base_type(glsl_without_array(variable->type)) == GLSL_TYPE_FLOAT) {
		unsigned interp_type;
		if (variable->data.sample) {
			interp_type = INTERP_SAMPLE;
			ctx->shader_info->info.ps.force_persample = true;
		} else if (variable->data.centroid)
			interp_type = INTERP_CENTROID;
		else
			interp_type = INTERP_CENTER;

		interp = lookup_interp_param(&ctx->abi, variable->data.interpolation, interp_type);
	} else
		interp = NULL;

	for (unsigned i = 0; i < attrib_count; ++i)
		ctx->inputs[radeon_llvm_reg_index_soa(idx + i, 0)] = interp;

}

static void
handle_vs_inputs(struct nir_to_llvm_context *ctx,
                 struct nir_shader *nir) {
	nir_foreach_variable(variable, &nir->inputs)
		handle_vs_input_decl(ctx, variable);
}

static void
prepare_interp_optimize(struct nir_to_llvm_context *ctx,
                        struct nir_shader *nir)
{
	if (!ctx->options->key.fs.multisample)
		return;

	bool uses_center = false;
	bool uses_centroid = false;
	nir_foreach_variable(variable, &nir->inputs) {
		if (glsl_get_base_type(glsl_without_array(variable->type)) != GLSL_TYPE_FLOAT ||
		    variable->data.sample)
			continue;

		if (variable->data.centroid)
			uses_centroid = true;
		else
			uses_center = true;
	}

	if (uses_center && uses_centroid) {
		LLVMValueRef sel = LLVMBuildICmp(ctx->builder, LLVMIntSLT, ctx->abi.prim_mask, ctx->ac.i32_0, "");
		ctx->persp_centroid = LLVMBuildSelect(ctx->builder, sel, ctx->persp_center, ctx->persp_centroid, "");
		ctx->linear_centroid = LLVMBuildSelect(ctx->builder, sel, ctx->linear_center, ctx->linear_centroid, "");
	}
}

static void
handle_fs_inputs(struct nir_to_llvm_context *ctx,
                 struct nir_shader *nir)
{
	prepare_interp_optimize(ctx, nir);

	nir_foreach_variable(variable, &nir->inputs)
		handle_fs_input_decl(ctx, variable);

	unsigned index = 0;

	if (ctx->shader_info->info.ps.uses_input_attachments ||
	    ctx->shader_info->info.needs_multiview_view_index)
		ctx->input_mask |= 1ull << VARYING_SLOT_LAYER;

	for (unsigned i = 0; i < RADEON_LLVM_MAX_INPUTS; ++i) {
		LLVMValueRef interp_param;
		LLVMValueRef *inputs = ctx->inputs +radeon_llvm_reg_index_soa(i, 0);

		if (!(ctx->input_mask & (1ull << i)))
			continue;

		if (i >= VARYING_SLOT_VAR0 || i == VARYING_SLOT_PNTC ||
		    i == VARYING_SLOT_PRIMITIVE_ID || i == VARYING_SLOT_LAYER) {
			interp_param = *inputs;
			interp_fs_input(ctx, index, interp_param, ctx->abi.prim_mask,
					inputs);

			if (!interp_param)
				ctx->shader_info->fs.flat_shaded_mask |= 1u << index;
			++index;
		} else if (i == VARYING_SLOT_POS) {
			for(int i = 0; i < 3; ++i)
				inputs[i] = ctx->abi.frag_pos[i];

			inputs[3] = ac_build_fdiv(&ctx->ac, ctx->ac.f32_1,
						  ctx->abi.frag_pos[3]);
		}
	}
	ctx->shader_info->fs.num_interp = index;
	if (ctx->input_mask & (1 << VARYING_SLOT_PNTC))
		ctx->shader_info->fs.has_pcoord = true;
	if (ctx->input_mask & (1 << VARYING_SLOT_PRIMITIVE_ID))
		ctx->shader_info->fs.prim_id_input = true;
	if (ctx->input_mask & (1 << VARYING_SLOT_LAYER))
		ctx->shader_info->fs.layer_input = true;
	ctx->shader_info->fs.input_mask = ctx->input_mask >> VARYING_SLOT_VAR0;

	if (ctx->shader_info->info.needs_multiview_view_index)
		ctx->view_index = ctx->inputs[radeon_llvm_reg_index_soa(VARYING_SLOT_LAYER, 0)];
}

static LLVMValueRef
ac_build_alloca(struct ac_llvm_context *ac,
                LLVMTypeRef type,
                const char *name)
{
	LLVMBuilderRef builder = ac->builder;
	LLVMBasicBlockRef current_block = LLVMGetInsertBlock(builder);
	LLVMValueRef function = LLVMGetBasicBlockParent(current_block);
	LLVMBasicBlockRef first_block = LLVMGetEntryBasicBlock(function);
	LLVMValueRef first_instr = LLVMGetFirstInstruction(first_block);
	LLVMBuilderRef first_builder = LLVMCreateBuilderInContext(ac->context);
	LLVMValueRef res;

	if (first_instr) {
		LLVMPositionBuilderBefore(first_builder, first_instr);
	} else {
		LLVMPositionBuilderAtEnd(first_builder, first_block);
	}

	res = LLVMBuildAlloca(first_builder, type, name);
	LLVMBuildStore(builder, LLVMConstNull(type), res);

	LLVMDisposeBuilder(first_builder);

	return res;
}

static LLVMValueRef si_build_alloca_undef(struct ac_llvm_context *ac,
					  LLVMTypeRef type,
					  const char *name)
{
	LLVMValueRef ptr = ac_build_alloca(ac, type, name);
	LLVMBuildStore(ac->builder, LLVMGetUndef(type), ptr);
	return ptr;
}

static void
scan_shader_output_decl(struct nir_to_llvm_context *ctx,
			struct nir_variable *variable,
			struct nir_shader *shader,
			gl_shader_stage stage)
{
	int idx = variable->data.location + variable->data.index;
	unsigned attrib_count = glsl_count_attribute_slots(variable->type, false);
	uint64_t mask_attribs;

	variable->data.driver_location = idx * 4;

	/* tess ctrl has it's own load/store paths for outputs */
	if (stage == MESA_SHADER_TESS_CTRL)
		return;

	mask_attribs = ((1ull << attrib_count) - 1) << idx;
	if (stage == MESA_SHADER_VERTEX ||
	    stage == MESA_SHADER_TESS_EVAL ||
	    stage == MESA_SHADER_GEOMETRY) {
		if (idx == VARYING_SLOT_CLIP_DIST0) {
			int length = shader->info.clip_distance_array_size +
			             shader->info.cull_distance_array_size;
			if (stage == MESA_SHADER_VERTEX) {
				ctx->shader_info->vs.outinfo.clip_dist_mask = (1 << shader->info.clip_distance_array_size) - 1;
				ctx->shader_info->vs.outinfo.cull_dist_mask = (1 << shader->info.cull_distance_array_size) - 1;
			}
			if (stage == MESA_SHADER_TESS_EVAL) {
				ctx->shader_info->tes.outinfo.clip_dist_mask = (1 << shader->info.clip_distance_array_size) - 1;
				ctx->shader_info->tes.outinfo.cull_dist_mask = (1 << shader->info.cull_distance_array_size) - 1;
			}

			if (length > 4)
				attrib_count = 2;
			else
				attrib_count = 1;
			mask_attribs = 1ull << idx;
		}
	}

	ctx->output_mask |= mask_attribs;
}

static void
handle_shader_output_decl(struct ac_nir_context *ctx,
			  struct nir_shader *nir,
			  struct nir_variable *variable)
{
	unsigned output_loc = variable->data.driver_location / 4;
	unsigned attrib_count = glsl_count_attribute_slots(variable->type, false);

	/* tess ctrl has it's own load/store paths for outputs */
	if (ctx->stage == MESA_SHADER_TESS_CTRL)
		return;

	if (ctx->stage == MESA_SHADER_VERTEX ||
	    ctx->stage == MESA_SHADER_TESS_EVAL ||
	    ctx->stage == MESA_SHADER_GEOMETRY) {
		int idx = variable->data.location + variable->data.index;
		if (idx == VARYING_SLOT_CLIP_DIST0) {
			int length = nir->info.clip_distance_array_size +
				     nir->info.cull_distance_array_size;

			if (length > 4)
				attrib_count = 2;
			else
				attrib_count = 1;
		}
	}

	for (unsigned i = 0; i < attrib_count; ++i) {
		for (unsigned chan = 0; chan < 4; chan++) {
			ctx->outputs[radeon_llvm_reg_index_soa(output_loc + i, chan)] =
		                       si_build_alloca_undef(&ctx->ac, ctx->ac.f32, "");
		}
	}
}

static LLVMTypeRef
glsl_base_to_llvm_type(struct nir_to_llvm_context *ctx,
		       enum glsl_base_type type)
{
	switch (type) {
	case GLSL_TYPE_INT:
	case GLSL_TYPE_UINT:
	case GLSL_TYPE_BOOL:
	case GLSL_TYPE_SUBROUTINE:
		return ctx->ac.i32;
	case GLSL_TYPE_FLOAT: /* TODO handle mediump */
		return ctx->ac.f32;
	case GLSL_TYPE_INT64:
	case GLSL_TYPE_UINT64:
		return ctx->ac.i64;
	case GLSL_TYPE_DOUBLE:
		return ctx->ac.f64;
	default:
		unreachable("unknown GLSL type");
	}
}

static LLVMTypeRef
glsl_to_llvm_type(struct nir_to_llvm_context *ctx,
		  const struct glsl_type *type)
{
	if (glsl_type_is_scalar(type)) {
		return glsl_base_to_llvm_type(ctx, glsl_get_base_type(type));
	}

	if (glsl_type_is_vector(type)) {
		return LLVMVectorType(
		   glsl_base_to_llvm_type(ctx, glsl_get_base_type(type)),
		   glsl_get_vector_elements(type));
	}

	if (glsl_type_is_matrix(type)) {
		return LLVMArrayType(
		   glsl_to_llvm_type(ctx, glsl_get_column_type(type)),
		   glsl_get_matrix_columns(type));
	}

	if (glsl_type_is_array(type)) {
		return LLVMArrayType(
		   glsl_to_llvm_type(ctx, glsl_get_array_element(type)),
		   glsl_get_length(type));
	}

	assert(glsl_type_is_struct(type));

	LLVMTypeRef member_types[glsl_get_length(type)];

	for (unsigned i = 0; i < glsl_get_length(type); i++) {
		member_types[i] =
			glsl_to_llvm_type(ctx,
					  glsl_get_struct_field(type, i));
	}

	return LLVMStructTypeInContext(ctx->context, member_types,
				       glsl_get_length(type), false);
}

static void
setup_locals(struct ac_nir_context *ctx,
	     struct nir_function *func)
{
	int i, j;
	ctx->num_locals = 0;
	nir_foreach_variable(variable, &func->impl->locals) {
		unsigned attrib_count = glsl_count_attribute_slots(variable->type, false);
		variable->data.driver_location = ctx->num_locals * 4;
		variable->data.location_frac = 0;
		ctx->num_locals += attrib_count;
	}
	ctx->locals = malloc(4 * ctx->num_locals * sizeof(LLVMValueRef));
	if (!ctx->locals)
	    return;

	for (i = 0; i < ctx->num_locals; i++) {
		for (j = 0; j < 4; j++) {
			ctx->locals[i * 4 + j] =
				si_build_alloca_undef(&ctx->ac, ctx->ac.f32, "temp");
		}
	}
}

static void
setup_shared(struct ac_nir_context *ctx,
	     struct nir_shader *nir)
{
	nir_foreach_variable(variable, &nir->shared) {
		LLVMValueRef shared =
			LLVMAddGlobalInAddressSpace(
			   ctx->ac.module, glsl_to_llvm_type(ctx->nctx, variable->type),
			   variable->name ? variable->name : "",
			   AC_LOCAL_ADDR_SPACE);
		_mesa_hash_table_insert(ctx->vars, variable, shared);
	}
}

static LLVMValueRef
emit_float_saturate(struct ac_llvm_context *ctx, LLVMValueRef v, float lo, float hi)
{
	v = ac_to_float(ctx, v);
	v = emit_intrin_2f_param(ctx, "llvm.maxnum", ctx->f32, v, LLVMConstReal(ctx->f32, lo));
	return emit_intrin_2f_param(ctx, "llvm.minnum", ctx->f32, v, LLVMConstReal(ctx->f32, hi));
}


static LLVMValueRef emit_pack_int16(struct nir_to_llvm_context *ctx,
					LLVMValueRef src0, LLVMValueRef src1)
{
	LLVMValueRef const16 = LLVMConstInt(ctx->ac.i32, 16, false);
	LLVMValueRef comp[2];

	comp[0] = LLVMBuildAnd(ctx->builder, src0, LLVMConstInt(ctx->ac.i32, 65535, 0), "");
	comp[1] = LLVMBuildAnd(ctx->builder, src1, LLVMConstInt(ctx->ac.i32, 65535, 0), "");
	comp[1] = LLVMBuildShl(ctx->builder, comp[1], const16, "");
	return LLVMBuildOr(ctx->builder, comp[0], comp[1], "");
}

/* Initialize arguments for the shader export intrinsic */
static void
si_llvm_init_export_args(struct nir_to_llvm_context *ctx,
			 LLVMValueRef *values,
			 unsigned target,
			 struct ac_export_args *args)
{
	/* Default is 0xf. Adjusted below depending on the format. */
	args->enabled_channels = 0xf;

	/* Specify whether the EXEC mask represents the valid mask */
	args->valid_mask = 0;

	/* Specify whether this is the last export */
	args->done = 0;

	/* Specify the target we are exporting */
	args->target = target;

	args->compr = false;
	args->out[0] = LLVMGetUndef(ctx->ac.f32);
	args->out[1] = LLVMGetUndef(ctx->ac.f32);
	args->out[2] = LLVMGetUndef(ctx->ac.f32);
	args->out[3] = LLVMGetUndef(ctx->ac.f32);

	if (!values)
		return;

	if (ctx->stage == MESA_SHADER_FRAGMENT && target >= V_008DFC_SQ_EXP_MRT) {
		LLVMValueRef val[4];
		unsigned index = target - V_008DFC_SQ_EXP_MRT;
		unsigned col_format = (ctx->options->key.fs.col_format >> (4 * index)) & 0xf;
		bool is_int8 = (ctx->options->key.fs.is_int8 >> index) & 1;
		bool is_int10 = (ctx->options->key.fs.is_int10 >> index) & 1;

		switch(col_format) {
		case V_028714_SPI_SHADER_ZERO:
			args->enabled_channels = 0; /* writemask */
			args->target = V_008DFC_SQ_EXP_NULL;
			break;

		case V_028714_SPI_SHADER_32_R:
			args->enabled_channels = 1;
			args->out[0] = values[0];
			break;

		case V_028714_SPI_SHADER_32_GR:
			args->enabled_channels = 0x3;
			args->out[0] = values[0];
			args->out[1] = values[1];
			break;

		case V_028714_SPI_SHADER_32_AR:
			args->enabled_channels = 0x9;
			args->out[0] = values[0];
			args->out[3] = values[3];
			break;

		case V_028714_SPI_SHADER_FP16_ABGR:
			args->compr = 1;

			for (unsigned chan = 0; chan < 2; chan++) {
				LLVMValueRef pack_args[2] = {
					values[2 * chan],
					values[2 * chan + 1]
				};
				LLVMValueRef packed;

				packed = ac_build_cvt_pkrtz_f16(&ctx->ac, pack_args);
				args->out[chan] = packed;
			}
			break;

		case V_028714_SPI_SHADER_UNORM16_ABGR:
			for (unsigned chan = 0; chan < 4; chan++) {
				val[chan] = ac_build_clamp(&ctx->ac, values[chan]);
				val[chan] = LLVMBuildFMul(ctx->builder, val[chan],
							LLVMConstReal(ctx->ac.f32, 65535), "");
				val[chan] = LLVMBuildFAdd(ctx->builder, val[chan],
							LLVMConstReal(ctx->ac.f32, 0.5), "");
				val[chan] = LLVMBuildFPToUI(ctx->builder, val[chan],
							ctx->ac.i32, "");
			}

			args->compr = 1;
			args->out[0] = emit_pack_int16(ctx, val[0], val[1]);
			args->out[1] = emit_pack_int16(ctx, val[2], val[3]);
			break;

		case V_028714_SPI_SHADER_SNORM16_ABGR:
			for (unsigned chan = 0; chan < 4; chan++) {
				val[chan] = emit_float_saturate(&ctx->ac, values[chan], -1, 1);
				val[chan] = LLVMBuildFMul(ctx->builder, val[chan],
							LLVMConstReal(ctx->ac.f32, 32767), "");

				/* If positive, add 0.5, else add -0.5. */
				val[chan] = LLVMBuildFAdd(ctx->builder, val[chan],
						LLVMBuildSelect(ctx->builder,
							LLVMBuildFCmp(ctx->builder, LLVMRealOGE,
								val[chan], ctx->ac.f32_0, ""),
							LLVMConstReal(ctx->ac.f32, 0.5),
							LLVMConstReal(ctx->ac.f32, -0.5), ""), "");
				val[chan] = LLVMBuildFPToSI(ctx->builder, val[chan], ctx->ac.i32, "");
			}

			args->compr = 1;
			args->out[0] = emit_pack_int16(ctx, val[0], val[1]);
			args->out[1] = emit_pack_int16(ctx, val[2], val[3]);
			break;

		case V_028714_SPI_SHADER_UINT16_ABGR: {
			LLVMValueRef max_rgb = LLVMConstInt(ctx->ac.i32,
							    is_int8 ? 255 : is_int10 ? 1023 : 65535, 0);
			LLVMValueRef max_alpha = !is_int10 ? max_rgb : LLVMConstInt(ctx->ac.i32, 3, 0);

			for (unsigned chan = 0; chan < 4; chan++) {
				val[chan] = ac_to_integer(&ctx->ac, values[chan]);
				val[chan] = emit_minmax_int(&ctx->ac, LLVMIntULT, val[chan], chan == 3 ? max_alpha : max_rgb);
			}

			args->compr = 1;
			args->out[0] = emit_pack_int16(ctx, val[0], val[1]);
			args->out[1] = emit_pack_int16(ctx, val[2], val[3]);
			break;
		}

		case V_028714_SPI_SHADER_SINT16_ABGR: {
			LLVMValueRef max_rgb = LLVMConstInt(ctx->ac.i32,
							    is_int8 ? 127 : is_int10 ? 511 : 32767, 0);
			LLVMValueRef min_rgb = LLVMConstInt(ctx->ac.i32,
							    is_int8 ? -128 : is_int10 ? -512 : -32768, 0);
			LLVMValueRef max_alpha = !is_int10 ? max_rgb : ctx->ac.i32_1;
			LLVMValueRef min_alpha = !is_int10 ? min_rgb : LLVMConstInt(ctx->ac.i32, -2, 0);

			/* Clamp. */
			for (unsigned chan = 0; chan < 4; chan++) {
				val[chan] = ac_to_integer(&ctx->ac, values[chan]);
				val[chan] = emit_minmax_int(&ctx->ac, LLVMIntSLT, val[chan], chan == 3 ? max_alpha : max_rgb);
				val[chan] = emit_minmax_int(&ctx->ac, LLVMIntSGT, val[chan], chan == 3 ? min_alpha : min_rgb);
			}

			args->compr = 1;
			args->out[0] = emit_pack_int16(ctx, val[0], val[1]);
			args->out[1] = emit_pack_int16(ctx, val[2], val[3]);
			break;
		}

		default:
		case V_028714_SPI_SHADER_32_ABGR:
			memcpy(&args->out[0], values, sizeof(values[0]) * 4);
			break;
		}
	} else
		memcpy(&args->out[0], values, sizeof(values[0]) * 4);

	for (unsigned i = 0; i < 4; ++i)
		args->out[i] = ac_to_float(&ctx->ac, args->out[i]);
}

static void
handle_vs_outputs_post(struct nir_to_llvm_context *ctx,
		       bool export_prim_id,
		       struct ac_vs_output_info *outinfo)
{
	uint32_t param_count = 0;
	unsigned target;
	unsigned pos_idx, num_pos_exports = 0;
	struct ac_export_args args, pos_args[4] = {};
	LLVMValueRef psize_value = NULL, layer_value = NULL, viewport_index_value = NULL;
	int i;

	if (ctx->options->key.has_multiview_view_index) {
		LLVMValueRef* tmp_out = &ctx->nir->outputs[radeon_llvm_reg_index_soa(VARYING_SLOT_LAYER, 0)];
		if(!*tmp_out) {
			for(unsigned i = 0; i < 4; ++i)
				ctx->nir->outputs[radeon_llvm_reg_index_soa(VARYING_SLOT_LAYER, i)] =
				            si_build_alloca_undef(&ctx->ac, ctx->ac.f32, "");
		}

		LLVMBuildStore(ctx->builder, ac_to_float(&ctx->ac, ctx->view_index),  *tmp_out);
		ctx->output_mask |= 1ull << VARYING_SLOT_LAYER;
	}

	memset(outinfo->vs_output_param_offset, AC_EXP_PARAM_UNDEFINED,
	       sizeof(outinfo->vs_output_param_offset));

	if (ctx->output_mask & (1ull << VARYING_SLOT_CLIP_DIST0)) {
		LLVMValueRef slots[8];
		unsigned j;

		if (outinfo->cull_dist_mask)
			outinfo->cull_dist_mask <<= ctx->num_output_clips;

		i = VARYING_SLOT_CLIP_DIST0;
		for (j = 0; j < ctx->num_output_clips + ctx->num_output_culls; j++)
			slots[j] = ac_to_float(&ctx->ac, LLVMBuildLoad(ctx->builder,
							       ctx->nir->outputs[radeon_llvm_reg_index_soa(i, j)], ""));

		for (i = ctx->num_output_clips + ctx->num_output_culls; i < 8; i++)
			slots[i] = LLVMGetUndef(ctx->ac.f32);

		if (ctx->num_output_clips + ctx->num_output_culls > 4) {
			target = V_008DFC_SQ_EXP_POS + 3;
			si_llvm_init_export_args(ctx, &slots[4], target, &args);
			memcpy(&pos_args[target - V_008DFC_SQ_EXP_POS],
			       &args, sizeof(args));
		}

		target = V_008DFC_SQ_EXP_POS + 2;
		si_llvm_init_export_args(ctx, &slots[0], target, &args);
		memcpy(&pos_args[target - V_008DFC_SQ_EXP_POS],
		       &args, sizeof(args));

	}

	LLVMValueRef pos_values[4] = {ctx->ac.f32_0, ctx->ac.f32_0, ctx->ac.f32_0, ctx->ac.f32_1};
	if (ctx->output_mask & (1ull << VARYING_SLOT_POS)) {
		for (unsigned j = 0; j < 4; j++)
			pos_values[j] = LLVMBuildLoad(ctx->builder,
			                         ctx->nir->outputs[radeon_llvm_reg_index_soa(VARYING_SLOT_POS, j)], "");
	}
	si_llvm_init_export_args(ctx, pos_values, V_008DFC_SQ_EXP_POS, &pos_args[0]);

	if (ctx->output_mask & (1ull << VARYING_SLOT_PSIZ)) {
		outinfo->writes_pointsize = true;
		psize_value = LLVMBuildLoad(ctx->builder,
		                            ctx->nir->outputs[radeon_llvm_reg_index_soa(VARYING_SLOT_PSIZ, 0)], "");
	}

	if (ctx->output_mask & (1ull << VARYING_SLOT_LAYER)) {
		outinfo->writes_layer = true;
		layer_value = LLVMBuildLoad(ctx->builder,
		                            ctx->nir->outputs[radeon_llvm_reg_index_soa(VARYING_SLOT_LAYER, 0)], "");
	}

	if (ctx->output_mask & (1ull << VARYING_SLOT_VIEWPORT)) {
		outinfo->writes_viewport_index = true;
		viewport_index_value = LLVMBuildLoad(ctx->builder,
		                                     ctx->nir->outputs[radeon_llvm_reg_index_soa(VARYING_SLOT_VIEWPORT, 0)], "");
	}

	if (outinfo->writes_pointsize ||
	    outinfo->writes_layer ||
	    outinfo->writes_viewport_index) {
		pos_args[1].enabled_channels = ((outinfo->writes_pointsize == true ? 1 : 0) |
						(outinfo->writes_layer == true ? 4 : 0));
		pos_args[1].valid_mask = 0;
		pos_args[1].done = 0;
		pos_args[1].target = V_008DFC_SQ_EXP_POS + 1;
		pos_args[1].compr = 0;
		pos_args[1].out[0] = ctx->ac.f32_0; /* X */
		pos_args[1].out[1] = ctx->ac.f32_0; /* Y */
		pos_args[1].out[2] = ctx->ac.f32_0; /* Z */
		pos_args[1].out[3] = ctx->ac.f32_0;  /* W */

		if (outinfo->writes_pointsize == true)
			pos_args[1].out[0] = psize_value;
		if (outinfo->writes_layer == true)
			pos_args[1].out[2] = layer_value;
		if (outinfo->writes_viewport_index == true) {
			if (ctx->options->chip_class >= GFX9) {
				/* GFX9 has the layer in out.z[10:0] and the viewport
				 * index in out.z[19:16].
				 */
				LLVMValueRef v = viewport_index_value;
				v = ac_to_integer(&ctx->ac, v);
				v = LLVMBuildShl(ctx->builder, v,
						 LLVMConstInt(ctx->ac.i32, 16, false),
						 "");
				v = LLVMBuildOr(ctx->builder, v,
						ac_to_integer(&ctx->ac, pos_args[1].out[2]), "");

				pos_args[1].out[2] = ac_to_float(&ctx->ac, v);
				pos_args[1].enabled_channels |= 1 << 2;
			} else {
				pos_args[1].out[3] = viewport_index_value;
				pos_args[1].enabled_channels |= 1 << 3;
			}
		}
	}
	for (i = 0; i < 4; i++) {
		if (pos_args[i].out[0])
			num_pos_exports++;
	}

	pos_idx = 0;
	for (i = 0; i < 4; i++) {
		if (!pos_args[i].out[0])
			continue;

		/* Specify the target we are exporting */
		pos_args[i].target = V_008DFC_SQ_EXP_POS + pos_idx++;
		if (pos_idx == num_pos_exports)
			pos_args[i].done = 1;
		ac_build_export(&ctx->ac, &pos_args[i]);
	}

	for (unsigned i = 0; i < RADEON_LLVM_MAX_OUTPUTS; ++i) {
		LLVMValueRef values[4];
		if (!(ctx->output_mask & (1ull << i)))
			continue;

		for (unsigned j = 0; j < 4; j++)
			values[j] = ac_to_float(&ctx->ac, LLVMBuildLoad(ctx->builder,
					        ctx->nir->outputs[radeon_llvm_reg_index_soa(i, j)], ""));

		if (i == VARYING_SLOT_LAYER) {
			target = V_008DFC_SQ_EXP_PARAM + param_count;
			outinfo->vs_output_param_offset[VARYING_SLOT_LAYER] = param_count;
			param_count++;
		} else if (i == VARYING_SLOT_PRIMITIVE_ID) {
			target = V_008DFC_SQ_EXP_PARAM + param_count;
			outinfo->vs_output_param_offset[VARYING_SLOT_PRIMITIVE_ID] = param_count;
			param_count++;
		} else if (i >= VARYING_SLOT_VAR0) {
			outinfo->export_mask |= 1u << (i - VARYING_SLOT_VAR0);
			target = V_008DFC_SQ_EXP_PARAM + param_count;
			outinfo->vs_output_param_offset[i] = param_count;
			param_count++;
		} else
			continue;

		si_llvm_init_export_args(ctx, values, target, &args);

		if (target >= V_008DFC_SQ_EXP_POS &&
		    target <= (V_008DFC_SQ_EXP_POS + 3)) {
			memcpy(&pos_args[target - V_008DFC_SQ_EXP_POS],
			       &args, sizeof(args));
		} else {
			ac_build_export(&ctx->ac, &args);
		}
	}

	if (export_prim_id) {
		LLVMValueRef values[4];
		target = V_008DFC_SQ_EXP_PARAM + param_count;
		outinfo->vs_output_param_offset[VARYING_SLOT_PRIMITIVE_ID] = param_count;
		param_count++;

		values[0] = ctx->vs_prim_id;
		ctx->shader_info->vs.vgpr_comp_cnt = MAX2(2,
							  ctx->shader_info->vs.vgpr_comp_cnt);
		for (unsigned j = 1; j < 4; j++)
			values[j] = ctx->ac.f32_0;
		si_llvm_init_export_args(ctx, values, target, &args);
		ac_build_export(&ctx->ac, &args);
		outinfo->export_prim_id = true;
	}

	outinfo->pos_exports = num_pos_exports;
	outinfo->param_exports = param_count;
}

static void
handle_es_outputs_post(struct nir_to_llvm_context *ctx,
		       struct ac_es_output_info *outinfo)
{
	int j;
	uint64_t max_output_written = 0;
	LLVMValueRef lds_base = NULL;

	for (unsigned i = 0; i < RADEON_LLVM_MAX_OUTPUTS; ++i) {
		int param_index;
		int length = 4;

		if (!(ctx->output_mask & (1ull << i)))
			continue;

		if (i == VARYING_SLOT_CLIP_DIST0)
			length = ctx->num_output_clips + ctx->num_output_culls;

		param_index = shader_io_get_unique_index(i);

		max_output_written = MAX2(param_index + (length > 4), max_output_written);
	}

	outinfo->esgs_itemsize = (max_output_written + 1) * 16;

	if (ctx->ac.chip_class  >= GFX9) {
		unsigned itemsize_dw = outinfo->esgs_itemsize / 4;
		LLVMValueRef vertex_idx = ac_get_thread_id(&ctx->ac);
		LLVMValueRef wave_idx = ac_build_bfe(&ctx->ac, ctx->merged_wave_info,
		                                     LLVMConstInt(ctx->ac.i32, 24, false),
		                                     LLVMConstInt(ctx->ac.i32, 4, false), false);
		vertex_idx = LLVMBuildOr(ctx->ac.builder, vertex_idx,
					 LLVMBuildMul(ctx->ac.builder, wave_idx,
						      LLVMConstInt(ctx->ac.i32, 64, false), ""), "");
		lds_base = LLVMBuildMul(ctx->ac.builder, vertex_idx,
					LLVMConstInt(ctx->ac.i32, itemsize_dw, 0), "");
	}

	for (unsigned i = 0; i < RADEON_LLVM_MAX_OUTPUTS; ++i) {
		LLVMValueRef dw_addr;
		LLVMValueRef *out_ptr = &ctx->nir->outputs[i * 4];
		int param_index;
		int length = 4;

		if (!(ctx->output_mask & (1ull << i)))
			continue;

		if (i == VARYING_SLOT_CLIP_DIST0)
			length = ctx->num_output_clips + ctx->num_output_culls;

		param_index = shader_io_get_unique_index(i);

		if (lds_base) {
			dw_addr = LLVMBuildAdd(ctx->builder, lds_base,
			                       LLVMConstInt(ctx->ac.i32, param_index * 4, false),
			                       "");
		}
		for (j = 0; j < length; j++) {
			LLVMValueRef out_val = LLVMBuildLoad(ctx->builder, out_ptr[j], "");
			out_val = LLVMBuildBitCast(ctx->builder, out_val, ctx->ac.i32, "");

			if (ctx->ac.chip_class  >= GFX9) {
				ac_lds_store(&ctx->ac, dw_addr,
					     LLVMBuildLoad(ctx->builder, out_ptr[j], ""));
				dw_addr = LLVMBuildAdd(ctx->builder, dw_addr, ctx->ac.i32_1, "");
			} else {
				ac_build_buffer_store_dword(&ctx->ac,
				                            ctx->esgs_ring,
				                            out_val, 1,
				                            NULL, ctx->es2gs_offset,
				                            (4 * param_index + j) * 4,
				                            1, 1, true, true);
			}
		}
	}
}

static void
handle_ls_outputs_post(struct nir_to_llvm_context *ctx)
{
	LLVMValueRef vertex_id = ctx->rel_auto_id;
	LLVMValueRef vertex_dw_stride = unpack_param(&ctx->ac, ctx->ls_out_layout, 13, 8);
	LLVMValueRef base_dw_addr = LLVMBuildMul(ctx->builder, vertex_id,
						 vertex_dw_stride, "");

	for (unsigned i = 0; i < RADEON_LLVM_MAX_OUTPUTS; ++i) {
		LLVMValueRef *out_ptr = &ctx->nir->outputs[i * 4];
		int length = 4;

		if (!(ctx->output_mask & (1ull << i)))
			continue;

		if (i == VARYING_SLOT_CLIP_DIST0)
			length = ctx->num_output_clips + ctx->num_output_culls;
		int param = shader_io_get_unique_index(i);
		mark_tess_output(ctx, false, param);
		if (length > 4)
			mark_tess_output(ctx, false, param + 1);
		LLVMValueRef dw_addr = LLVMBuildAdd(ctx->builder, base_dw_addr,
						    LLVMConstInt(ctx->ac.i32, param * 4, false),
						    "");
		for (unsigned j = 0; j < length; j++) {
			ac_lds_store(&ctx->ac, dw_addr,
				     LLVMBuildLoad(ctx->builder, out_ptr[j], ""));
			dw_addr = LLVMBuildAdd(ctx->builder, dw_addr, ctx->ac.i32_1, "");
		}
	}
}

struct ac_build_if_state
{
	struct nir_to_llvm_context *ctx;
	LLVMValueRef condition;
	LLVMBasicBlockRef entry_block;
	LLVMBasicBlockRef true_block;
	LLVMBasicBlockRef false_block;
	LLVMBasicBlockRef merge_block;
};

static LLVMBasicBlockRef
ac_build_insert_new_block(struct nir_to_llvm_context *ctx, const char *name)
{
	LLVMBasicBlockRef current_block;
	LLVMBasicBlockRef next_block;
	LLVMBasicBlockRef new_block;

	/* get current basic block */
	current_block = LLVMGetInsertBlock(ctx->builder);

	/* chqeck if there's another block after this one */
	next_block = LLVMGetNextBasicBlock(current_block);
	if (next_block) {
		/* insert the new block before the next block */
		new_block = LLVMInsertBasicBlockInContext(ctx->context, next_block, name);
	}
	else {
		/* append new block after current block */
		LLVMValueRef function = LLVMGetBasicBlockParent(current_block);
		new_block = LLVMAppendBasicBlockInContext(ctx->context, function, name);
	}
	return new_block;
}

static void
ac_nir_build_if(struct ac_build_if_state *ifthen,
		struct nir_to_llvm_context *ctx,
		LLVMValueRef condition)
{
	LLVMBasicBlockRef block = LLVMGetInsertBlock(ctx->builder);

	memset(ifthen, 0, sizeof *ifthen);
	ifthen->ctx = ctx;
	ifthen->condition = condition;
	ifthen->entry_block = block;

	/* create endif/merge basic block for the phi functions */
	ifthen->merge_block = ac_build_insert_new_block(ctx, "endif-block");

	/* create/insert true_block before merge_block */
	ifthen->true_block =
		LLVMInsertBasicBlockInContext(ctx->context,
					      ifthen->merge_block,
					      "if-true-block");

	/* successive code goes into the true block */
	LLVMPositionBuilderAtEnd(ctx->builder, ifthen->true_block);
}

/**
 * End a conditional.
 */
static void
ac_nir_build_endif(struct ac_build_if_state *ifthen)
{
	LLVMBuilderRef builder = ifthen->ctx->builder;

	/* Insert branch to the merge block from current block */
	LLVMBuildBr(builder, ifthen->merge_block);

	/*
	 * Now patch in the various branch instructions.
	 */

	/* Insert the conditional branch instruction at the end of entry_block */
	LLVMPositionBuilderAtEnd(builder, ifthen->entry_block);
	if (ifthen->false_block) {
		/* we have an else clause */
		LLVMBuildCondBr(builder, ifthen->condition,
				ifthen->true_block, ifthen->false_block);
	}
	else {
		/* no else clause */
		LLVMBuildCondBr(builder, ifthen->condition,
				ifthen->true_block, ifthen->merge_block);
	}

	/* Resume building code at end of the ifthen->merge_block */
	LLVMPositionBuilderAtEnd(builder, ifthen->merge_block);
}

static void
write_tess_factors(struct nir_to_llvm_context *ctx)
{
	unsigned stride, outer_comps, inner_comps;
	struct ac_build_if_state if_ctx, inner_if_ctx;
	LLVMValueRef invocation_id = unpack_param(&ctx->ac, ctx->abi.tcs_rel_ids, 8, 5);
	LLVMValueRef rel_patch_id = unpack_param(&ctx->ac, ctx->abi.tcs_rel_ids, 0, 8);
	unsigned tess_inner_index, tess_outer_index;
	LLVMValueRef lds_base, lds_inner, lds_outer, byteoffset, buffer;
	LLVMValueRef out[6], vec0, vec1, tf_base, inner[4], outer[4];
	int i;
	emit_barrier(&ctx->ac, ctx->stage);

	switch (ctx->options->key.tcs.primitive_mode) {
	case GL_ISOLINES:
		stride = 2;
		outer_comps = 2;
		inner_comps = 0;
		break;
	case GL_TRIANGLES:
		stride = 4;
		outer_comps = 3;
		inner_comps = 1;
		break;
	case GL_QUADS:
		stride = 6;
		outer_comps = 4;
		inner_comps = 2;
		break;
	default:
		return;
	}

	ac_nir_build_if(&if_ctx, ctx,
			LLVMBuildICmp(ctx->builder, LLVMIntEQ,
				      invocation_id, ctx->ac.i32_0, ""));

	tess_inner_index = shader_io_get_unique_index(VARYING_SLOT_TESS_LEVEL_INNER);
	tess_outer_index = shader_io_get_unique_index(VARYING_SLOT_TESS_LEVEL_OUTER);

	mark_tess_output(ctx, true, tess_inner_index);
	mark_tess_output(ctx, true, tess_outer_index);
	lds_base = get_tcs_out_current_patch_data_offset(ctx);
	lds_inner = LLVMBuildAdd(ctx->builder, lds_base,
				 LLVMConstInt(ctx->ac.i32, tess_inner_index * 4, false), "");
	lds_outer = LLVMBuildAdd(ctx->builder, lds_base,
				 LLVMConstInt(ctx->ac.i32, tess_outer_index * 4, false), "");

	for (i = 0; i < 4; i++) {
		inner[i] = LLVMGetUndef(ctx->ac.i32);
		outer[i] = LLVMGetUndef(ctx->ac.i32);
	}

	// LINES reverseal
	if (ctx->options->key.tcs.primitive_mode == GL_ISOLINES) {
		outer[0] = out[1] = ac_lds_load(&ctx->ac, lds_outer);
		lds_outer = LLVMBuildAdd(ctx->builder, lds_outer,
					 ctx->ac.i32_1, "");
		outer[1] = out[0] = ac_lds_load(&ctx->ac, lds_outer);
	} else {
		for (i = 0; i < outer_comps; i++) {
			outer[i] = out[i] =
				ac_lds_load(&ctx->ac, lds_outer);
			lds_outer = LLVMBuildAdd(ctx->builder, lds_outer,
						 ctx->ac.i32_1, "");
		}
		for (i = 0; i < inner_comps; i++) {
			inner[i] = out[outer_comps+i] =
				ac_lds_load(&ctx->ac, lds_inner);
			lds_inner = LLVMBuildAdd(ctx->builder, lds_inner,
						 ctx->ac.i32_1, "");
		}
	}

	/* Convert the outputs to vectors for stores. */
	vec0 = ac_build_gather_values(&ctx->ac, out, MIN2(stride, 4));
	vec1 = NULL;

	if (stride > 4)
		vec1 = ac_build_gather_values(&ctx->ac, out + 4, stride - 4);


	buffer = ctx->hs_ring_tess_factor;
	tf_base = ctx->tess_factor_offset;
	byteoffset = LLVMBuildMul(ctx->builder, rel_patch_id,
				  LLVMConstInt(ctx->ac.i32, 4 * stride, false), "");
	unsigned tf_offset = 0;

	if (ctx->options->chip_class <= VI) {
		ac_nir_build_if(&inner_if_ctx, ctx,
		                LLVMBuildICmp(ctx->builder, LLVMIntEQ,
		                              rel_patch_id, ctx->ac.i32_0, ""));

		/* Store the dynamic HS control word. */
		ac_build_buffer_store_dword(&ctx->ac, buffer,
					    LLVMConstInt(ctx->ac.i32, 0x80000000, false),
					    1, ctx->ac.i32_0, tf_base,
					    0, 1, 0, true, false);
		tf_offset += 4;

		ac_nir_build_endif(&inner_if_ctx);
	}

	/* Store the tessellation factors. */
	ac_build_buffer_store_dword(&ctx->ac, buffer, vec0,
				    MIN2(stride, 4), byteoffset, tf_base,
				    tf_offset, 1, 0, true, false);
	if (vec1)
		ac_build_buffer_store_dword(&ctx->ac, buffer, vec1,
					    stride - 4, byteoffset, tf_base,
					    16 + tf_offset, 1, 0, true, false);

	//store to offchip for TES to read - only if TES reads them
	if (ctx->options->key.tcs.tes_reads_tess_factors) {
		LLVMValueRef inner_vec, outer_vec, tf_outer_offset;
		LLVMValueRef tf_inner_offset;
		unsigned param_outer, param_inner;

		param_outer = shader_io_get_unique_index(VARYING_SLOT_TESS_LEVEL_OUTER);
		tf_outer_offset = get_tcs_tes_buffer_address(ctx, NULL,
							     LLVMConstInt(ctx->ac.i32, param_outer, 0));

		outer_vec = ac_build_gather_values(&ctx->ac, outer,
						   util_next_power_of_two(outer_comps));

		ac_build_buffer_store_dword(&ctx->ac, ctx->hs_ring_tess_offchip, outer_vec,
					    outer_comps, tf_outer_offset,
					    ctx->oc_lds, 0, 1, 0, true, false);
		if (inner_comps) {
			param_inner = shader_io_get_unique_index(VARYING_SLOT_TESS_LEVEL_INNER);
			tf_inner_offset = get_tcs_tes_buffer_address(ctx, NULL,
								     LLVMConstInt(ctx->ac.i32, param_inner, 0));

			inner_vec = inner_comps == 1 ? inner[0] :
				ac_build_gather_values(&ctx->ac, inner, inner_comps);
			ac_build_buffer_store_dword(&ctx->ac, ctx->hs_ring_tess_offchip, inner_vec,
						    inner_comps, tf_inner_offset,
						    ctx->oc_lds, 0, 1, 0, true, false);
		}
	}
	ac_nir_build_endif(&if_ctx);
}

static void
handle_tcs_outputs_post(struct nir_to_llvm_context *ctx)
{
	write_tess_factors(ctx);
}

static bool
si_export_mrt_color(struct nir_to_llvm_context *ctx,
		    LLVMValueRef *color, unsigned param, bool is_last,
		    struct ac_export_args *args)
{
	/* Export */
	si_llvm_init_export_args(ctx, color, param,
				 args);

	if (is_last) {
		args->valid_mask = 1; /* whether the EXEC mask is valid */
		args->done = 1; /* DONE bit */
	} else if (!args->enabled_channels)
		return false; /* unnecessary NULL export */

	return true;
}

static void
radv_export_mrt_z(struct nir_to_llvm_context *ctx,
		  LLVMValueRef depth, LLVMValueRef stencil,
		  LLVMValueRef samplemask)
{
	struct ac_export_args args;

	ac_export_mrt_z(&ctx->ac, depth, stencil, samplemask, &args);

	ac_build_export(&ctx->ac, &args);
}

static void
handle_fs_outputs_post(struct nir_to_llvm_context *ctx)
{
	unsigned index = 0;
	LLVMValueRef depth = NULL, stencil = NULL, samplemask = NULL;
	struct ac_export_args color_args[8];

	for (unsigned i = 0; i < RADEON_LLVM_MAX_OUTPUTS; ++i) {
		LLVMValueRef values[4];

		if (!(ctx->output_mask & (1ull << i)))
			continue;

		if (i == FRAG_RESULT_DEPTH) {
			ctx->shader_info->fs.writes_z = true;
			depth = ac_to_float(&ctx->ac, LLVMBuildLoad(ctx->builder,
							    ctx->nir->outputs[radeon_llvm_reg_index_soa(i, 0)], ""));
		} else if (i == FRAG_RESULT_STENCIL) {
			ctx->shader_info->fs.writes_stencil = true;
			stencil = ac_to_float(&ctx->ac, LLVMBuildLoad(ctx->builder,
							      ctx->nir->outputs[radeon_llvm_reg_index_soa(i, 0)], ""));
		} else if (i == FRAG_RESULT_SAMPLE_MASK) {
			ctx->shader_info->fs.writes_sample_mask = true;
			samplemask = ac_to_float(&ctx->ac, LLVMBuildLoad(ctx->builder,
								  ctx->nir->outputs[radeon_llvm_reg_index_soa(i, 0)], ""));
		} else {
			bool last = false;
			for (unsigned j = 0; j < 4; j++)
				values[j] = ac_to_float(&ctx->ac, LLVMBuildLoad(ctx->builder,
									ctx->nir->outputs[radeon_llvm_reg_index_soa(i, j)], ""));

			if (!ctx->shader_info->fs.writes_z && !ctx->shader_info->fs.writes_stencil && !ctx->shader_info->fs.writes_sample_mask)
				last = ctx->output_mask <= ((1ull << (i + 1)) - 1);

			bool ret = si_export_mrt_color(ctx, values, V_008DFC_SQ_EXP_MRT + (i - FRAG_RESULT_DATA0), last, &color_args[index]);
			if (ret)
				index++;
		}
	}

	for (unsigned i = 0; i < index; i++)
		ac_build_export(&ctx->ac, &color_args[i]);
	if (depth || stencil || samplemask)
		radv_export_mrt_z(ctx, depth, stencil, samplemask);
	else if (!index) {
		si_export_mrt_color(ctx, NULL, V_008DFC_SQ_EXP_NULL, true, &color_args[0]);
		ac_build_export(&ctx->ac, &color_args[0]);
	}
}

static void
emit_gs_epilogue(struct nir_to_llvm_context *ctx)
{
	ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_NOP | AC_SENDMSG_GS_DONE, ctx->gs_wave_id);
}

static void
handle_shader_outputs_post(struct ac_shader_abi *abi, unsigned max_outputs,
			   LLVMValueRef *addrs)
{
	struct nir_to_llvm_context *ctx = nir_to_llvm_context_from_abi(abi);

	switch (ctx->stage) {
	case MESA_SHADER_VERTEX:
		if (ctx->options->key.vs.as_ls)
			handle_ls_outputs_post(ctx);
		else if (ctx->options->key.vs.as_es)
			handle_es_outputs_post(ctx, &ctx->shader_info->vs.es_info);
		else
			handle_vs_outputs_post(ctx, ctx->options->key.vs.export_prim_id,
					       &ctx->shader_info->vs.outinfo);
		break;
	case MESA_SHADER_FRAGMENT:
		handle_fs_outputs_post(ctx);
		break;
	case MESA_SHADER_GEOMETRY:
		emit_gs_epilogue(ctx);
		break;
	case MESA_SHADER_TESS_CTRL:
		handle_tcs_outputs_post(ctx);
		break;
	case MESA_SHADER_TESS_EVAL:
		if (ctx->options->key.tes.as_es)
			handle_es_outputs_post(ctx, &ctx->shader_info->tes.es_info);
		else
			handle_vs_outputs_post(ctx, ctx->options->key.tes.export_prim_id,
					       &ctx->shader_info->tes.outinfo);
		break;
	default:
		break;
	}
}

static void ac_llvm_finalize_module(struct nir_to_llvm_context * ctx)
{
	LLVMPassManagerRef passmgr;
	/* Create the pass manager */
	passmgr = LLVMCreateFunctionPassManagerForModule(
							ctx->module);

	/* This pass should eliminate all the load and store instructions */
	LLVMAddPromoteMemoryToRegisterPass(passmgr);

	/* Add some optimization passes */
	LLVMAddScalarReplAggregatesPass(passmgr);
	LLVMAddLICMPass(passmgr);
	LLVMAddAggressiveDCEPass(passmgr);
	LLVMAddCFGSimplificationPass(passmgr);
	LLVMAddInstructionCombiningPass(passmgr);

	/* Run the pass */
	LLVMInitializeFunctionPassManager(passmgr);
	LLVMRunFunctionPassManager(passmgr, ctx->main_function);
	LLVMFinalizeFunctionPassManager(passmgr);

	LLVMDisposeBuilder(ctx->builder);
	LLVMDisposePassManager(passmgr);
}

static void
ac_nir_eliminate_const_vs_outputs(struct nir_to_llvm_context *ctx)
{
	struct ac_vs_output_info *outinfo;

	switch (ctx->stage) {
	case MESA_SHADER_FRAGMENT:
	case MESA_SHADER_COMPUTE:
	case MESA_SHADER_TESS_CTRL:
	case MESA_SHADER_GEOMETRY:
		return;
	case MESA_SHADER_VERTEX:
		if (ctx->options->key.vs.as_ls ||
		    ctx->options->key.vs.as_es)
			return;
		outinfo = &ctx->shader_info->vs.outinfo;
		break;
	case MESA_SHADER_TESS_EVAL:
		if (ctx->options->key.vs.as_es)
			return;
		outinfo = &ctx->shader_info->tes.outinfo;
		break;
	default:
		unreachable("Unhandled shader type");
	}

	ac_optimize_vs_outputs(&ctx->ac,
			       ctx->main_function,
			       outinfo->vs_output_param_offset,
			       VARYING_SLOT_MAX,
			       &outinfo->param_exports);
}

static void
ac_setup_rings(struct nir_to_llvm_context *ctx)
{
	if ((ctx->stage == MESA_SHADER_VERTEX && ctx->options->key.vs.as_es) ||
	    (ctx->stage == MESA_SHADER_TESS_EVAL && ctx->options->key.tes.as_es)) {
		ctx->esgs_ring = ac_build_load_to_sgpr(&ctx->ac, ctx->ring_offsets, LLVMConstInt(ctx->ac.i32, RING_ESGS_VS, false));
	}

	if (ctx->is_gs_copy_shader) {
		ctx->gsvs_ring = ac_build_load_to_sgpr(&ctx->ac, ctx->ring_offsets, LLVMConstInt(ctx->ac.i32, RING_GSVS_VS, false));
	}
	if (ctx->stage == MESA_SHADER_GEOMETRY) {
		LLVMValueRef tmp;
		ctx->esgs_ring = ac_build_load_to_sgpr(&ctx->ac, ctx->ring_offsets, LLVMConstInt(ctx->ac.i32, RING_ESGS_GS, false));
		ctx->gsvs_ring = ac_build_load_to_sgpr(&ctx->ac, ctx->ring_offsets, LLVMConstInt(ctx->ac.i32, RING_GSVS_GS, false));

		ctx->gsvs_ring = LLVMBuildBitCast(ctx->builder, ctx->gsvs_ring, ctx->ac.v4i32, "");

		ctx->gsvs_ring = LLVMBuildInsertElement(ctx->builder, ctx->gsvs_ring, ctx->gsvs_num_entries, LLVMConstInt(ctx->ac.i32, 2, false), "");
		tmp = LLVMBuildExtractElement(ctx->builder, ctx->gsvs_ring, ctx->ac.i32_1, "");
		tmp = LLVMBuildOr(ctx->builder, tmp, ctx->gsvs_ring_stride, "");
		ctx->gsvs_ring = LLVMBuildInsertElement(ctx->builder, ctx->gsvs_ring, tmp, ctx->ac.i32_1, "");
	}

	if (ctx->stage == MESA_SHADER_TESS_CTRL ||
	    ctx->stage == MESA_SHADER_TESS_EVAL) {
		ctx->hs_ring_tess_offchip = ac_build_load_to_sgpr(&ctx->ac, ctx->ring_offsets, LLVMConstInt(ctx->ac.i32, RING_HS_TESS_OFFCHIP, false));
		ctx->hs_ring_tess_factor = ac_build_load_to_sgpr(&ctx->ac, ctx->ring_offsets, LLVMConstInt(ctx->ac.i32, RING_HS_TESS_FACTOR, false));
	}
}

static unsigned
ac_nir_get_max_workgroup_size(enum chip_class chip_class,
			      const struct nir_shader *nir)
{
	switch (nir->info.stage) {
	case MESA_SHADER_TESS_CTRL:
		return chip_class >= CIK ? 128 : 64;
	case MESA_SHADER_GEOMETRY:
		return chip_class >= GFX9 ? 128 : 64;
	case MESA_SHADER_COMPUTE:
		break;
	default:
		return 0;
	}

	unsigned max_workgroup_size = nir->info.cs.local_size[0] *
		nir->info.cs.local_size[1] *
		nir->info.cs.local_size[2];
	return max_workgroup_size;
}

/* Fixup the HW not emitting the TCS regs if there are no HS threads. */
static void ac_nir_fixup_ls_hs_input_vgprs(struct nir_to_llvm_context *ctx)
{
	LLVMValueRef count = ac_build_bfe(&ctx->ac, ctx->merged_wave_info,
	                                  LLVMConstInt(ctx->ac.i32, 8, false),
	                                  LLVMConstInt(ctx->ac.i32, 8, false), false);
	LLVMValueRef hs_empty = LLVMBuildICmp(ctx->ac.builder, LLVMIntEQ, count,
	                                      ctx->ac.i32_0, "");
	ctx->abi.instance_id = LLVMBuildSelect(ctx->ac.builder, hs_empty, ctx->rel_auto_id, ctx->abi.instance_id, "");
	ctx->vs_prim_id = LLVMBuildSelect(ctx->ac.builder, hs_empty, ctx->abi.vertex_id, ctx->vs_prim_id, "");
	ctx->rel_auto_id = LLVMBuildSelect(ctx->ac.builder, hs_empty, ctx->abi.tcs_rel_ids, ctx->rel_auto_id, "");
	ctx->abi.vertex_id = LLVMBuildSelect(ctx->ac.builder, hs_empty, ctx->abi.tcs_patch_id, ctx->abi.vertex_id, "");
}

static void prepare_gs_input_vgprs(struct nir_to_llvm_context *ctx)
{
	for(int i = 5; i >= 0; --i) {
		ctx->gs_vtx_offset[i] = ac_build_bfe(&ctx->ac, ctx->gs_vtx_offset[i & ~1],
		                                     LLVMConstInt(ctx->ac.i32, (i & 1) * 16, false),
		                                     LLVMConstInt(ctx->ac.i32, 16, false), false);
	}

	ctx->gs_wave_id = ac_build_bfe(&ctx->ac, ctx->merged_wave_info,
	                               LLVMConstInt(ctx->ac.i32, 16, false),
	                               LLVMConstInt(ctx->ac.i32, 8, false), false);
}

void ac_nir_translate(struct ac_llvm_context *ac, struct ac_shader_abi *abi,
		      struct nir_shader *nir, struct nir_to_llvm_context *nctx)
{
	struct ac_nir_context ctx = {};
	struct nir_function *func;

	ctx.ac = *ac;
	ctx.abi = abi;

	ctx.nctx = nctx;
	if (nctx)
		nctx->nir = &ctx;

	ctx.stage = nir->info.stage;

	ctx.main_function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx.ac.builder));

	nir_foreach_variable(variable, &nir->outputs)
		handle_shader_output_decl(&ctx, nir, variable);

	ctx.defs = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
	                                   _mesa_key_pointer_equal);
	ctx.phis = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
	                                   _mesa_key_pointer_equal);
	ctx.vars = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
	                                   _mesa_key_pointer_equal);

	func = (struct nir_function *)exec_list_get_head(&nir->functions);

	setup_locals(&ctx, func);

	if (nir->info.stage == MESA_SHADER_COMPUTE)
		setup_shared(&ctx, nir);

	visit_cf_list(&ctx, &func->impl->body);
	phi_post_pass(&ctx);

	ctx.abi->emit_outputs(ctx.abi, RADEON_LLVM_MAX_OUTPUTS,
			      ctx.outputs);

	free(ctx.locals);
	ralloc_free(ctx.defs);
	ralloc_free(ctx.phis);
	ralloc_free(ctx.vars);

	if (nctx)
		nctx->nir = NULL;
}

static
LLVMModuleRef ac_translate_nir_to_llvm(LLVMTargetMachineRef tm,
                                       struct nir_shader *const *shaders,
                                       int shader_count,
                                       struct ac_shader_variant_info *shader_info,
                                       const struct ac_nir_compiler_options *options)
{
	struct nir_to_llvm_context ctx = {0};
	unsigned i;
	ctx.options = options;
	ctx.shader_info = shader_info;
	ctx.context = LLVMContextCreate();
	ctx.module = LLVMModuleCreateWithNameInContext("shader", ctx.context);

	ac_llvm_context_init(&ctx.ac, ctx.context, options->chip_class,
			     options->family);
	ctx.ac.module = ctx.module;
	LLVMSetTarget(ctx.module, options->supports_spill ? "amdgcn-mesa-mesa3d" : "amdgcn--");

	LLVMTargetDataRef data_layout = LLVMCreateTargetDataLayout(tm);
	char *data_layout_str = LLVMCopyStringRepOfTargetData(data_layout);
	LLVMSetDataLayout(ctx.module, data_layout_str);
	LLVMDisposeTargetData(data_layout);
	LLVMDisposeMessage(data_layout_str);

	enum ac_float_mode float_mode =
		options->unsafe_math ? AC_FLOAT_MODE_UNSAFE_FP_MATH :
				       AC_FLOAT_MODE_DEFAULT;

	ctx.builder = ac_create_builder(ctx.context, float_mode);
	ctx.ac.builder = ctx.builder;

	memset(shader_info, 0, sizeof(*shader_info));

	for(int i = 0; i < shader_count; ++i)
		ac_nir_shader_info_pass(shaders[i], options, &shader_info->info);

	for (i = 0; i < AC_UD_MAX_SETS; i++)
		shader_info->user_sgprs_locs.descriptor_sets[i].sgpr_idx = -1;
	for (i = 0; i < AC_UD_MAX_UD; i++)
		shader_info->user_sgprs_locs.shader_data[i].sgpr_idx = -1;

	ctx.max_workgroup_size = 0;
	for (int i = 0; i < shader_count; ++i) {
		ctx.max_workgroup_size = MAX2(ctx.max_workgroup_size,
		                              ac_nir_get_max_workgroup_size(ctx.options->chip_class,
		                                                            shaders[i]));
	}

	create_function(&ctx, shaders[shader_count - 1]->info.stage, shader_count >= 2,
	                shader_count >= 2 ? shaders[shader_count - 2]->info.stage  : MESA_SHADER_VERTEX);

	ctx.abi.inputs = &ctx.inputs[0];
	ctx.abi.emit_outputs = handle_shader_outputs_post;
	ctx.abi.emit_vertex = visit_emit_vertex;
	ctx.abi.load_ubo = radv_load_ubo;
	ctx.abi.load_ssbo = radv_load_ssbo;
	ctx.abi.load_sampler_desc = radv_get_sampler_desc;
	ctx.abi.clamp_shadow_reference = false;

	if (shader_count >= 2)
		ac_init_exec_full_mask(&ctx.ac);

	if (ctx.ac.chip_class == GFX9 &&
	    shaders[shader_count - 1]->info.stage == MESA_SHADER_TESS_CTRL)
		ac_nir_fixup_ls_hs_input_vgprs(&ctx);

	for(int i = 0; i < shader_count; ++i) {
		ctx.stage = shaders[i]->info.stage;
		ctx.output_mask = 0;
		ctx.tess_outputs_written = 0;
		ctx.num_output_clips = shaders[i]->info.clip_distance_array_size;
		ctx.num_output_culls = shaders[i]->info.cull_distance_array_size;

		if (shaders[i]->info.stage == MESA_SHADER_GEOMETRY) {
			ctx.gs_next_vertex = ac_build_alloca(&ctx.ac, ctx.ac.i32, "gs_next_vertex");
			ctx.gs_max_out_vertices = shaders[i]->info.gs.vertices_out;
			ctx.abi.load_inputs = load_gs_input;
			ctx.abi.emit_primitive = visit_end_primitive;
		} else if (shaders[i]->info.stage == MESA_SHADER_TESS_CTRL) {
			ctx.tcs_outputs_read = shaders[i]->info.outputs_read;
			ctx.tcs_patch_outputs_read = shaders[i]->info.patch_outputs_read;
			ctx.abi.load_tess_varyings = load_tcs_varyings;
			ctx.abi.load_patch_vertices_in = load_patch_vertices_in;
			ctx.abi.store_tcs_outputs = store_tcs_output;
		} else if (shaders[i]->info.stage == MESA_SHADER_TESS_EVAL) {
			ctx.tes_primitive_mode = shaders[i]->info.tess.primitive_mode;
			ctx.abi.load_tess_varyings = load_tes_input;
			ctx.abi.load_tess_coord = load_tess_coord;
			ctx.abi.load_patch_vertices_in = load_patch_vertices_in;
		} else if (shaders[i]->info.stage == MESA_SHADER_VERTEX) {
			if (shader_info->info.vs.needs_instance_id) {
				if (ctx.options->key.vs.as_ls) {
					ctx.shader_info->vs.vgpr_comp_cnt =
						MAX2(2, ctx.shader_info->vs.vgpr_comp_cnt);
				} else {
					ctx.shader_info->vs.vgpr_comp_cnt =
						MAX2(1, ctx.shader_info->vs.vgpr_comp_cnt);
				}
			}
		} else if (shaders[i]->info.stage == MESA_SHADER_FRAGMENT) {
			shader_info->fs.can_discard = shaders[i]->info.fs.uses_discard;
			ctx.abi.lookup_interp_param = lookup_interp_param;
			ctx.abi.load_sample_position = load_sample_position;
		}

		if (i)
			emit_barrier(&ctx.ac, ctx.stage);

		ac_setup_rings(&ctx);

		LLVMBasicBlockRef merge_block;
		if (shader_count >= 2) {
			LLVMValueRef fn = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx.ac.builder));
			LLVMBasicBlockRef then_block = LLVMAppendBasicBlockInContext(ctx.ac.context, fn, "");
			merge_block = LLVMAppendBasicBlockInContext(ctx.ac.context, fn, "");

			LLVMValueRef count = ac_build_bfe(&ctx.ac, ctx.merged_wave_info,
			                                  LLVMConstInt(ctx.ac.i32, 8 * i, false),
			                                  LLVMConstInt(ctx.ac.i32, 8, false), false);
			LLVMValueRef thread_id = ac_get_thread_id(&ctx.ac);
			LLVMValueRef cond = LLVMBuildICmp(ctx.ac.builder, LLVMIntULT,
			                                  thread_id, count, "");
			LLVMBuildCondBr(ctx.ac.builder, cond, then_block, merge_block);

			LLVMPositionBuilderAtEnd(ctx.ac.builder, then_block);
		}

		if (shaders[i]->info.stage == MESA_SHADER_FRAGMENT)
			handle_fs_inputs(&ctx, shaders[i]);
		else if(shaders[i]->info.stage == MESA_SHADER_VERTEX)
			handle_vs_inputs(&ctx, shaders[i]);
		else if(shader_count >= 2 && shaders[i]->info.stage == MESA_SHADER_GEOMETRY)
			prepare_gs_input_vgprs(&ctx);

		nir_foreach_variable(variable, &shaders[i]->outputs)
			scan_shader_output_decl(&ctx, variable, shaders[i], shaders[i]->info.stage);

		ac_nir_translate(&ctx.ac, &ctx.abi, shaders[i], &ctx);

		if (shader_count >= 2) {
			LLVMBuildBr(ctx.ac.builder, merge_block);
			LLVMPositionBuilderAtEnd(ctx.ac.builder, merge_block);
		}

		if (shaders[i]->info.stage == MESA_SHADER_GEOMETRY) {
			unsigned addclip = shaders[i]->info.clip_distance_array_size +
					shaders[i]->info.cull_distance_array_size > 4;
			shader_info->gs.gsvs_vertex_size = (util_bitcount64(ctx.output_mask) + addclip) * 16;
			shader_info->gs.max_gsvs_emit_size = shader_info->gs.gsvs_vertex_size *
				shaders[i]->info.gs.vertices_out;
		} else if (shaders[i]->info.stage == MESA_SHADER_TESS_CTRL) {
			shader_info->tcs.outputs_written = ctx.tess_outputs_written;
			shader_info->tcs.patch_outputs_written = ctx.tess_patch_outputs_written;
		} else if (shaders[i]->info.stage == MESA_SHADER_VERTEX && ctx.options->key.vs.as_ls) {
			shader_info->vs.outputs_written = ctx.tess_outputs_written;
		}
	}

	LLVMBuildRetVoid(ctx.builder);

	if (options->dump_preoptir)
		ac_dump_module(ctx.module);

	ac_llvm_finalize_module(&ctx);

	if (shader_count == 1)
		ac_nir_eliminate_const_vs_outputs(&ctx);

	return ctx.module;
}

static void ac_diagnostic_handler(LLVMDiagnosticInfoRef di, void *context)
{
	unsigned *retval = (unsigned *)context;
	LLVMDiagnosticSeverity severity = LLVMGetDiagInfoSeverity(di);
	char *description = LLVMGetDiagInfoDescription(di);

	if (severity == LLVMDSError) {
		*retval = 1;
		fprintf(stderr, "LLVM triggered Diagnostic Handler: %s\n",
		        description);
	}

	LLVMDisposeMessage(description);
}

static unsigned ac_llvm_compile(LLVMModuleRef M,
                                struct ac_shader_binary *binary,
                                LLVMTargetMachineRef tm)
{
	unsigned retval = 0;
	char *err;
	LLVMContextRef llvm_ctx;
	LLVMMemoryBufferRef out_buffer;
	unsigned buffer_size;
	const char *buffer_data;
	LLVMBool mem_err;

	/* Setup Diagnostic Handler*/
	llvm_ctx = LLVMGetModuleContext(M);

	LLVMContextSetDiagnosticHandler(llvm_ctx, ac_diagnostic_handler,
	                                &retval);

	/* Compile IR*/
	mem_err = LLVMTargetMachineEmitToMemoryBuffer(tm, M, LLVMObjectFile,
	                                              &err, &out_buffer);

	/* Process Errors/Warnings */
	if (mem_err) {
		fprintf(stderr, "%s: %s", __FUNCTION__, err);
		free(err);
		retval = 1;
		goto out;
	}

	/* Extract Shader Code*/
	buffer_size = LLVMGetBufferSize(out_buffer);
	buffer_data = LLVMGetBufferStart(out_buffer);

	ac_elf_read(buffer_data, buffer_size, binary);

	/* Clean up */
	LLVMDisposeMemoryBuffer(out_buffer);

out:
	return retval;
}

static void ac_compile_llvm_module(LLVMTargetMachineRef tm,
				   LLVMModuleRef llvm_module,
				   struct ac_shader_binary *binary,
				   struct ac_shader_config *config,
				   struct ac_shader_variant_info *shader_info,
				   gl_shader_stage stage,
				   bool dump_shader, bool supports_spill)
{
	if (dump_shader)
		ac_dump_module(llvm_module);

	memset(binary, 0, sizeof(*binary));
	int v = ac_llvm_compile(llvm_module, binary, tm);
	if (v) {
		fprintf(stderr, "compile failed\n");
	}

	if (dump_shader)
		fprintf(stderr, "disasm:\n%s\n", binary->disasm_string);

	ac_shader_binary_read_config(binary, config, 0, supports_spill);

	LLVMContextRef ctx = LLVMGetModuleContext(llvm_module);
	LLVMDisposeModule(llvm_module);
	LLVMContextDispose(ctx);

	if (stage == MESA_SHADER_FRAGMENT) {
		shader_info->num_input_vgprs = 0;
		if (G_0286CC_PERSP_SAMPLE_ENA(config->spi_ps_input_addr))
			shader_info->num_input_vgprs += 2;
		if (G_0286CC_PERSP_CENTER_ENA(config->spi_ps_input_addr))
			shader_info->num_input_vgprs += 2;
		if (G_0286CC_PERSP_CENTROID_ENA(config->spi_ps_input_addr))
			shader_info->num_input_vgprs += 2;
		if (G_0286CC_PERSP_PULL_MODEL_ENA(config->spi_ps_input_addr))
			shader_info->num_input_vgprs += 3;
		if (G_0286CC_LINEAR_SAMPLE_ENA(config->spi_ps_input_addr))
			shader_info->num_input_vgprs += 2;
		if (G_0286CC_LINEAR_CENTER_ENA(config->spi_ps_input_addr))
			shader_info->num_input_vgprs += 2;
		if (G_0286CC_LINEAR_CENTROID_ENA(config->spi_ps_input_addr))
			shader_info->num_input_vgprs += 2;
		if (G_0286CC_LINE_STIPPLE_TEX_ENA(config->spi_ps_input_addr))
			shader_info->num_input_vgprs += 1;
		if (G_0286CC_POS_X_FLOAT_ENA(config->spi_ps_input_addr))
			shader_info->num_input_vgprs += 1;
		if (G_0286CC_POS_Y_FLOAT_ENA(config->spi_ps_input_addr))
			shader_info->num_input_vgprs += 1;
		if (G_0286CC_POS_Z_FLOAT_ENA(config->spi_ps_input_addr))
			shader_info->num_input_vgprs += 1;
		if (G_0286CC_POS_W_FLOAT_ENA(config->spi_ps_input_addr))
			shader_info->num_input_vgprs += 1;
		if (G_0286CC_FRONT_FACE_ENA(config->spi_ps_input_addr))
			shader_info->num_input_vgprs += 1;
		if (G_0286CC_ANCILLARY_ENA(config->spi_ps_input_addr))
			shader_info->num_input_vgprs += 1;
		if (G_0286CC_SAMPLE_COVERAGE_ENA(config->spi_ps_input_addr))
			shader_info->num_input_vgprs += 1;
		if (G_0286CC_POS_FIXED_PT_ENA(config->spi_ps_input_addr))
			shader_info->num_input_vgprs += 1;
	}
	config->num_vgprs = MAX2(config->num_vgprs, shader_info->num_input_vgprs);

	/* +3 for scratch wave offset and VCC */
	config->num_sgprs = MAX2(config->num_sgprs,
	                         shader_info->num_input_sgprs + 3);

	/* Enable 64-bit and 16-bit denormals, because there is no performance
	 * cost.
	 *
	 * If denormals are enabled, all floating-point output modifiers are
	 * ignored.
	 *
	 * Don't enable denormals for 32-bit floats, because:
	 * - Floating-point output modifiers would be ignored by the hw.
	 * - Some opcodes don't support denormals, such as v_mad_f32. We would
	 *   have to stop using those.
	 * - SI & CI would be very slow.
	 */
	config->float_mode |= V_00B028_FP_64_DENORMS;
}

static void
ac_fill_shader_info(struct ac_shader_variant_info *shader_info, struct nir_shader *nir, const struct ac_nir_compiler_options *options)
{
        switch (nir->info.stage) {
        case MESA_SHADER_COMPUTE:
                for (int i = 0; i < 3; ++i)
                        shader_info->cs.block_size[i] = nir->info.cs.local_size[i];
                break;
        case MESA_SHADER_FRAGMENT:
                shader_info->fs.early_fragment_test = nir->info.fs.early_fragment_tests;
                break;
        case MESA_SHADER_GEOMETRY:
                shader_info->gs.vertices_in = nir->info.gs.vertices_in;
                shader_info->gs.vertices_out = nir->info.gs.vertices_out;
                shader_info->gs.output_prim = nir->info.gs.output_primitive;
                shader_info->gs.invocations = nir->info.gs.invocations;
                break;
        case MESA_SHADER_TESS_EVAL:
                shader_info->tes.primitive_mode = nir->info.tess.primitive_mode;
                shader_info->tes.spacing = nir->info.tess.spacing;
                shader_info->tes.ccw = nir->info.tess.ccw;
                shader_info->tes.point_mode = nir->info.tess.point_mode;
                shader_info->tes.as_es = options->key.tes.as_es;
                break;
        case MESA_SHADER_TESS_CTRL:
                shader_info->tcs.tcs_vertices_out = nir->info.tess.tcs_vertices_out;
                break;
        case MESA_SHADER_VERTEX:
                shader_info->vs.as_es = options->key.vs.as_es;
                shader_info->vs.as_ls = options->key.vs.as_ls;
                /* in LS mode we need at least 1, invocation id needs 2, handled elsewhere */
                if (options->key.vs.as_ls)
                        shader_info->vs.vgpr_comp_cnt = MAX2(1, shader_info->vs.vgpr_comp_cnt);
                break;
        default:
                break;
        }
}

void ac_compile_nir_shader(LLVMTargetMachineRef tm,
                           struct ac_shader_binary *binary,
                           struct ac_shader_config *config,
                           struct ac_shader_variant_info *shader_info,
                           struct nir_shader *const *nir,
                           int nir_count,
                           const struct ac_nir_compiler_options *options,
			   bool dump_shader)
{

	LLVMModuleRef llvm_module = ac_translate_nir_to_llvm(tm, nir, nir_count, shader_info,
	                                                     options);

	ac_compile_llvm_module(tm, llvm_module, binary, config, shader_info, nir[0]->info.stage, dump_shader, options->supports_spill);
	for (int i = 0; i < nir_count; ++i)
		ac_fill_shader_info(shader_info, nir[i], options);

	/* Determine the ES type (VS or TES) for the GS on GFX9. */
	if (options->chip_class == GFX9) {
		if (nir_count == 2 &&
		    nir[1]->info.stage == MESA_SHADER_GEOMETRY) {
			shader_info->gs.es_type = nir[0]->info.stage;
		}
	}
}

static void
ac_gs_copy_shader_emit(struct nir_to_llvm_context *ctx)
{
	LLVMValueRef vtx_offset =
		LLVMBuildMul(ctx->builder, ctx->abi.vertex_id,
			     LLVMConstInt(ctx->ac.i32, 4, false), "");
	int idx = 0;

	for (unsigned i = 0; i < RADEON_LLVM_MAX_OUTPUTS; ++i) {
		int length = 4;
		int slot = idx;
		int slot_inc = 1;
		if (!(ctx->output_mask & (1ull << i)))
			continue;

		if (i == VARYING_SLOT_CLIP_DIST0) {
			/* unpack clip and cull from a single set of slots */
			length = ctx->num_output_clips + ctx->num_output_culls;
			if (length > 4)
				slot_inc = 2;
		}

		for (unsigned j = 0; j < length; j++) {
			LLVMValueRef value, soffset;

			soffset = LLVMConstInt(ctx->ac.i32,
					       (slot * 4 + j) *
					       ctx->gs_max_out_vertices * 16 * 4, false);

			value = ac_build_buffer_load(&ctx->ac, ctx->gsvs_ring,
						     1, ctx->ac.i32_0,
						     vtx_offset, soffset,
						     0, 1, 1, true, false);

			LLVMBuildStore(ctx->builder,
				       ac_to_float(&ctx->ac, value), ctx->nir->outputs[radeon_llvm_reg_index_soa(i, j)]);
		}
		idx += slot_inc;
	}
	handle_vs_outputs_post(ctx, false, &ctx->shader_info->vs.outinfo);
}

void ac_create_gs_copy_shader(LLVMTargetMachineRef tm,
			      struct nir_shader *geom_shader,
			      struct ac_shader_binary *binary,
			      struct ac_shader_config *config,
			      struct ac_shader_variant_info *shader_info,
			      const struct ac_nir_compiler_options *options,
			      bool dump_shader)
{
	struct nir_to_llvm_context ctx = {0};
	ctx.context = LLVMContextCreate();
	ctx.module = LLVMModuleCreateWithNameInContext("shader", ctx.context);
	ctx.options = options;
	ctx.shader_info = shader_info;

	ac_llvm_context_init(&ctx.ac, ctx.context, options->chip_class,
			     options->family);
	ctx.ac.module = ctx.module;

	ctx.is_gs_copy_shader = true;
	LLVMSetTarget(ctx.module, "amdgcn--");

	enum ac_float_mode float_mode =
		options->unsafe_math ? AC_FLOAT_MODE_UNSAFE_FP_MATH :
				       AC_FLOAT_MODE_DEFAULT;

	ctx.builder = ac_create_builder(ctx.context, float_mode);
	ctx.ac.builder = ctx.builder;
	ctx.stage = MESA_SHADER_VERTEX;

	create_function(&ctx, MESA_SHADER_VERTEX, false, MESA_SHADER_VERTEX);

	ctx.gs_max_out_vertices = geom_shader->info.gs.vertices_out;
	ac_setup_rings(&ctx);

	ctx.num_output_clips = geom_shader->info.clip_distance_array_size;
	ctx.num_output_culls = geom_shader->info.cull_distance_array_size;

	struct ac_nir_context nir_ctx = {};
	nir_ctx.ac = ctx.ac;
	nir_ctx.abi = &ctx.abi;

	nir_ctx.nctx = &ctx;
	ctx.nir = &nir_ctx;

	nir_foreach_variable(variable, &geom_shader->outputs) {
		scan_shader_output_decl(&ctx, variable, geom_shader, MESA_SHADER_VERTEX);
		handle_shader_output_decl(&nir_ctx, geom_shader, variable);
	}

	ac_gs_copy_shader_emit(&ctx);

	ctx.nir = NULL;

	LLVMBuildRetVoid(ctx.builder);

	ac_llvm_finalize_module(&ctx);

	ac_compile_llvm_module(tm, ctx.module, binary, config, shader_info,
			       MESA_SHADER_VERTEX,
			       dump_shader, options->supports_spill);
}
