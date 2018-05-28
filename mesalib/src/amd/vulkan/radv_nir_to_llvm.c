/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
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

#include "radv_private.h"
#include "radv_shader.h"
#include "nir/nir.h"

#include <llvm-c/Core.h>
#include <llvm-c/TargetMachine.h>
#include <llvm-c/Transforms/Scalar.h>
#if HAVE_LLVM >= 0x0700
#include <llvm-c/Transforms/Utils.h>
#endif

#include "sid.h"
#include "gfx9d.h"
#include "ac_binary.h"
#include "ac_llvm_util.h"
#include "ac_llvm_build.h"
#include "ac_shader_abi.h"
#include "ac_shader_util.h"
#include "ac_exp_param.h"

#define RADEON_LLVM_MAX_INPUTS (VARYING_SLOT_VAR31 + 1)

struct radv_shader_context {
	struct ac_llvm_context ac;
	const struct radv_nir_compiler_options *options;
	struct radv_shader_variant_info *shader_info;
	struct ac_shader_abi abi;

	unsigned max_workgroup_size;
	LLVMContextRef context;
	LLVMValueRef main_function;

	LLVMValueRef descriptor_sets[RADV_UD_MAX_SETS];
	LLVMValueRef ring_offsets;

	LLVMValueRef vertex_buffers;
	LLVMValueRef rel_auto_id;
	LLVMValueRef vs_prim_id;
	LLVMValueRef es2gs_offset;

	LLVMValueRef oc_lds;
	LLVMValueRef merged_wave_info;
	LLVMValueRef tess_factor_offset;
	LLVMValueRef tes_rel_patch_id;
	LLVMValueRef tes_u;
	LLVMValueRef tes_v;

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

	uint32_t tcs_patch_outputs_read;
	uint64_t tcs_outputs_read;
	uint32_t tcs_vertices_per_patch;
	uint32_t tcs_num_inputs;
	uint32_t tcs_num_patches;
	uint32_t max_gsvs_emit_size;
	uint32_t gsvs_vertex_size;
};

enum radeon_llvm_calling_convention {
	RADEON_LLVM_AMDGPU_VS = 87,
	RADEON_LLVM_AMDGPU_GS = 88,
	RADEON_LLVM_AMDGPU_PS = 89,
	RADEON_LLVM_AMDGPU_CS = 90,
	RADEON_LLVM_AMDGPU_HS = 93,
};

static inline struct radv_shader_context *
radv_shader_context_from_abi(struct ac_shader_abi *abi)
{
	struct radv_shader_context *ctx = NULL;
	return container_of(abi, ctx, abi);
}

struct ac_build_if_state
{
	struct radv_shader_context *ctx;
	LLVMValueRef condition;
	LLVMBasicBlockRef entry_block;
	LLVMBasicBlockRef true_block;
	LLVMBasicBlockRef false_block;
	LLVMBasicBlockRef merge_block;
};

static LLVMBasicBlockRef
ac_build_insert_new_block(struct radv_shader_context *ctx, const char *name)
{
	LLVMBasicBlockRef current_block;
	LLVMBasicBlockRef next_block;
	LLVMBasicBlockRef new_block;

	/* get current basic block */
	current_block = LLVMGetInsertBlock(ctx->ac.builder);

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
		struct radv_shader_context *ctx,
		LLVMValueRef condition)
{
	LLVMBasicBlockRef block = LLVMGetInsertBlock(ctx->ac.builder);

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
	LLVMPositionBuilderAtEnd(ctx->ac.builder, ifthen->true_block);
}

/**
 * End a conditional.
 */
static void
ac_nir_build_endif(struct ac_build_if_state *ifthen)
{
	LLVMBuilderRef builder = ifthen->ctx->ac.builder;

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


static LLVMValueRef get_rel_patch_id(struct radv_shader_context *ctx)
{
	switch (ctx->stage) {
	case MESA_SHADER_TESS_CTRL:
		return ac_unpack_param(&ctx->ac, ctx->abi.tcs_rel_ids, 0, 8);
	case MESA_SHADER_TESS_EVAL:
		return ctx->tes_rel_patch_id;
		break;
	default:
		unreachable("Illegal stage");
	}
}

static unsigned
get_tcs_num_patches(struct radv_shader_context *ctx)
{
	unsigned num_tcs_input_cp = ctx->options->key.tcs.input_vertices;
	unsigned num_tcs_output_cp = ctx->tcs_vertices_per_patch;
	uint32_t input_vertex_size = ctx->tcs_num_inputs * 16;
	uint32_t input_patch_size = ctx->options->key.tcs.input_vertices * input_vertex_size;
	uint32_t num_tcs_outputs = util_last_bit64(ctx->shader_info->info.tcs.outputs_written);
	uint32_t num_tcs_patch_outputs = util_last_bit64(ctx->shader_info->info.tcs.patch_outputs_written);
	uint32_t output_vertex_size = num_tcs_outputs * 16;
	uint32_t pervertex_output_patch_size = ctx->tcs_vertices_per_patch * output_vertex_size;
	uint32_t output_patch_size = pervertex_output_patch_size + num_tcs_patch_outputs * 16;
	unsigned num_patches;
	unsigned hardware_lds_size;

	/* Ensure that we only need one wave per SIMD so we don't need to check
	 * resource usage. Also ensures that the number of tcs in and out
	 * vertices per threadgroup are at most 256.
	 */
	num_patches = 64 / MAX2(num_tcs_input_cp, num_tcs_output_cp) * 4;
	/* Make sure that the data fits in LDS. This assumes the shaders only
	 * use LDS for the inputs and outputs.
	 */
	hardware_lds_size = ctx->options->chip_class >= CIK ? 65536 : 32768;
	num_patches = MIN2(num_patches, hardware_lds_size / (input_patch_size + output_patch_size));
	/* Make sure the output data fits in the offchip buffer */
	num_patches = MIN2(num_patches, (ctx->options->tess_offchip_block_dw_size * 4) / output_patch_size);
	/* Not necessary for correctness, but improves performance. The
	 * specific value is taken from the proprietary driver.
	 */
	num_patches = MIN2(num_patches, 40);

	/* SI bug workaround - limit LS-HS threadgroups to only one wave. */
	if (ctx->options->chip_class == SI) {
		unsigned one_wave = 64 / MAX2(num_tcs_input_cp, num_tcs_output_cp);
		num_patches = MIN2(num_patches, one_wave);
	}
	return num_patches;
}

static unsigned
calculate_tess_lds_size(struct radv_shader_context *ctx)
{
	unsigned num_tcs_input_cp = ctx->options->key.tcs.input_vertices;
	unsigned num_tcs_output_cp;
	unsigned num_tcs_outputs, num_tcs_patch_outputs;
	unsigned input_vertex_size, output_vertex_size;
	unsigned input_patch_size, output_patch_size;
	unsigned pervertex_output_patch_size;
	unsigned output_patch0_offset;
	unsigned num_patches;
	unsigned lds_size;

	num_tcs_output_cp = ctx->tcs_vertices_per_patch;
	num_tcs_outputs = util_last_bit64(ctx->shader_info->info.tcs.outputs_written);
	num_tcs_patch_outputs = util_last_bit64(ctx->shader_info->info.tcs.patch_outputs_written);

	input_vertex_size = ctx->tcs_num_inputs * 16;
	output_vertex_size = num_tcs_outputs * 16;

	input_patch_size = num_tcs_input_cp * input_vertex_size;

	pervertex_output_patch_size = num_tcs_output_cp * output_vertex_size;
	output_patch_size = pervertex_output_patch_size + num_tcs_patch_outputs * 16;

	num_patches = ctx->tcs_num_patches;
	output_patch0_offset = input_patch_size * num_patches;

	lds_size = output_patch0_offset + output_patch_size * num_patches;
	return lds_size;
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
get_tcs_in_patch_stride(struct radv_shader_context *ctx)
{
	assert (ctx->stage == MESA_SHADER_TESS_CTRL);
	uint32_t input_vertex_size = ctx->tcs_num_inputs * 16;
	uint32_t input_patch_size = ctx->options->key.tcs.input_vertices * input_vertex_size;

	input_patch_size /= 4;
	return LLVMConstInt(ctx->ac.i32, input_patch_size, false);
}

static LLVMValueRef
get_tcs_out_patch_stride(struct radv_shader_context *ctx)
{
	uint32_t num_tcs_outputs = util_last_bit64(ctx->shader_info->info.tcs.outputs_written);
	uint32_t num_tcs_patch_outputs = util_last_bit64(ctx->shader_info->info.tcs.patch_outputs_written);
	uint32_t output_vertex_size = num_tcs_outputs * 16;
	uint32_t pervertex_output_patch_size = ctx->tcs_vertices_per_patch * output_vertex_size;
	uint32_t output_patch_size = pervertex_output_patch_size + num_tcs_patch_outputs * 16;
	output_patch_size /= 4;
	return LLVMConstInt(ctx->ac.i32, output_patch_size, false);
}

static LLVMValueRef
get_tcs_out_vertex_stride(struct radv_shader_context *ctx)
{
	uint32_t num_tcs_outputs = util_last_bit64(ctx->shader_info->info.tcs.outputs_written);
	uint32_t output_vertex_size = num_tcs_outputs * 16;
	output_vertex_size /= 4;
	return LLVMConstInt(ctx->ac.i32, output_vertex_size, false);
}

static LLVMValueRef
get_tcs_out_patch0_offset(struct radv_shader_context *ctx)
{
	assert (ctx->stage == MESA_SHADER_TESS_CTRL);
	uint32_t input_vertex_size = ctx->tcs_num_inputs * 16;
	uint32_t input_patch_size = ctx->options->key.tcs.input_vertices * input_vertex_size;
	uint32_t output_patch0_offset = input_patch_size;
	unsigned num_patches = ctx->tcs_num_patches;

	output_patch0_offset *= num_patches;
	output_patch0_offset /= 4;
	return LLVMConstInt(ctx->ac.i32, output_patch0_offset, false);
}

static LLVMValueRef
get_tcs_out_patch0_patch_data_offset(struct radv_shader_context *ctx)
{
	assert (ctx->stage == MESA_SHADER_TESS_CTRL);
	uint32_t input_vertex_size = ctx->tcs_num_inputs * 16;
	uint32_t input_patch_size = ctx->options->key.tcs.input_vertices * input_vertex_size;
	uint32_t output_patch0_offset = input_patch_size;

	uint32_t num_tcs_outputs = util_last_bit64(ctx->shader_info->info.tcs.outputs_written);
	uint32_t output_vertex_size = num_tcs_outputs * 16;
	uint32_t pervertex_output_patch_size = ctx->tcs_vertices_per_patch * output_vertex_size;
	unsigned num_patches = ctx->tcs_num_patches;

	output_patch0_offset *= num_patches;
	output_patch0_offset += pervertex_output_patch_size;
	output_patch0_offset /= 4;
	return LLVMConstInt(ctx->ac.i32, output_patch0_offset, false);
}

static LLVMValueRef
get_tcs_in_current_patch_offset(struct radv_shader_context *ctx)
{
	LLVMValueRef patch_stride = get_tcs_in_patch_stride(ctx);
	LLVMValueRef rel_patch_id = get_rel_patch_id(ctx);

	return LLVMBuildMul(ctx->ac.builder, patch_stride, rel_patch_id, "");
}

static LLVMValueRef
get_tcs_out_current_patch_offset(struct radv_shader_context *ctx)
{
	LLVMValueRef patch0_offset = get_tcs_out_patch0_offset(ctx);
	LLVMValueRef patch_stride = get_tcs_out_patch_stride(ctx);
	LLVMValueRef rel_patch_id = get_rel_patch_id(ctx);

	return LLVMBuildAdd(ctx->ac.builder, patch0_offset,
			    LLVMBuildMul(ctx->ac.builder, patch_stride,
					 rel_patch_id, ""),
			    "");
}

static LLVMValueRef
get_tcs_out_current_patch_data_offset(struct radv_shader_context *ctx)
{
	LLVMValueRef patch0_patch_data_offset =
		get_tcs_out_patch0_patch_data_offset(ctx);
	LLVMValueRef patch_stride = get_tcs_out_patch_stride(ctx);
	LLVMValueRef rel_patch_id = get_rel_patch_id(ctx);

	return LLVMBuildAdd(ctx->ac.builder, patch0_patch_data_offset,
			    LLVMBuildMul(ctx->ac.builder, patch_stride,
					 rel_patch_id, ""),
			    "");
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
		     const struct radv_nir_compiler_options *options)
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

	if (options->address32_hi) {
		ac_llvm_add_target_dep_function_attr(main_function,
						     "amdgpu-32bit-address-high-bits",
						     options->address32_hi);
	}

	if (max_workgroup_size) {
		ac_llvm_add_target_dep_function_attr(main_function,
						     "amdgpu-max-work-group-size",
						     max_workgroup_size);
	}
	if (options->unsafe_math) {
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


static void
set_loc(struct radv_userdata_info *ud_info, uint8_t *sgpr_idx, uint8_t num_sgprs,
	uint32_t indirect_offset)
{
	ud_info->sgpr_idx = *sgpr_idx;
	ud_info->num_sgprs = num_sgprs;
	ud_info->indirect = indirect_offset > 0;
	ud_info->indirect_offset = indirect_offset;
	*sgpr_idx += num_sgprs;
}

static void
set_loc_shader(struct radv_shader_context *ctx, int idx, uint8_t *sgpr_idx,
	       uint8_t num_sgprs)
{
	struct radv_userdata_info *ud_info =
		&ctx->shader_info->user_sgprs_locs.shader_data[idx];
	assert(ud_info);

	set_loc(ud_info, sgpr_idx, num_sgprs, 0);
}

static void
set_loc_shader_ptr(struct radv_shader_context *ctx, int idx, uint8_t *sgpr_idx)
{
	bool use_32bit_pointers = HAVE_32BIT_POINTERS &&
				  idx != AC_UD_SCRATCH_RING_OFFSETS;

	set_loc_shader(ctx, idx, sgpr_idx, use_32bit_pointers ? 1 : 2);
}

static void
set_loc_desc(struct radv_shader_context *ctx, int idx,  uint8_t *sgpr_idx,
	     uint32_t indirect_offset)
{
	struct radv_userdata_info *ud_info =
		&ctx->shader_info->user_sgprs_locs.descriptor_sets[idx];
	assert(ud_info);

	set_loc(ud_info, sgpr_idx, HAVE_32BIT_POINTERS ? 1 : 2, indirect_offset);
}

struct user_sgpr_info {
	bool need_ring_offsets;
	bool indirect_all_descriptor_sets;
};

static bool needs_view_index_sgpr(struct radv_shader_context *ctx,
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

static uint8_t
count_vs_user_sgprs(struct radv_shader_context *ctx)
{
	uint8_t count = 0;

	if (ctx->shader_info->info.vs.has_vertex_buffers)
		count += HAVE_32BIT_POINTERS ? 1 : 2;
	count += ctx->shader_info->info.vs.needs_draw_id ? 3 : 2;

	return count;
}

static void allocate_user_sgprs(struct radv_shader_context *ctx,
				gl_shader_stage stage,
				bool has_previous_stage,
				gl_shader_stage previous_stage,
				bool needs_view_index,
				struct user_sgpr_info *user_sgpr_info)
{
	uint8_t user_sgpr_count = 0;

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
		user_sgpr_count += 2;
	}

	switch (stage) {
	case MESA_SHADER_COMPUTE:
		if (ctx->shader_info->info.cs.uses_grid_size)
			user_sgpr_count += 3;
		break;
	case MESA_SHADER_FRAGMENT:
		user_sgpr_count += ctx->shader_info->info.ps.needs_sample_positions;
		break;
	case MESA_SHADER_VERTEX:
		if (!ctx->is_gs_copy_shader)
			user_sgpr_count += count_vs_user_sgprs(ctx);
		break;
	case MESA_SHADER_TESS_CTRL:
		if (has_previous_stage) {
			if (previous_stage == MESA_SHADER_VERTEX)
				user_sgpr_count += count_vs_user_sgprs(ctx);
		}
		break;
	case MESA_SHADER_TESS_EVAL:
		break;
	case MESA_SHADER_GEOMETRY:
		if (has_previous_stage) {
			if (previous_stage == MESA_SHADER_VERTEX) {
				user_sgpr_count += count_vs_user_sgprs(ctx);
			}
		}
		break;
	default:
		break;
	}

	if (needs_view_index)
		user_sgpr_count++;

	if (ctx->shader_info->info.loads_push_constants)
		user_sgpr_count += HAVE_32BIT_POINTERS ? 1 : 2;

	uint32_t available_sgprs = ctx->options->chip_class >= GFX9 ? 32 : 16;
	uint32_t remaining_sgprs = available_sgprs - user_sgpr_count;
	uint32_t num_desc_set =
		util_bitcount(ctx->shader_info->info.desc_set_used_mask);

	if (remaining_sgprs / (HAVE_32BIT_POINTERS ? 1 : 2) < num_desc_set) {
		user_sgpr_info->indirect_all_descriptor_sets = true;
	}
}

static void
declare_global_input_sgprs(struct radv_shader_context *ctx,
			   gl_shader_stage stage,
			   bool has_previous_stage,
			   gl_shader_stage previous_stage,
			   const struct user_sgpr_info *user_sgpr_info,
			   struct arg_info *args,
			   LLVMValueRef *desc_sets)
{
	LLVMTypeRef type = ac_array_in_const32_addr_space(ctx->ac.i8);
	unsigned num_sets = ctx->options->layout ?
			    ctx->options->layout->num_sets : 0;
	unsigned stage_mask = 1 << stage;

	if (has_previous_stage)
		stage_mask |= 1 << previous_stage;

	/* 1 for each descriptor set */
	if (!user_sgpr_info->indirect_all_descriptor_sets) {
		for (unsigned i = 0; i < num_sets; ++i) {
			if ((ctx->shader_info->info.desc_set_used_mask & (1 << i)) &&
			    ctx->options->layout->set[i].layout->shader_stages & stage_mask) {
				add_array_arg(args, type,
					      &ctx->descriptor_sets[i]);
			}
		}
	} else {
		add_array_arg(args, ac_array_in_const32_addr_space(type), desc_sets);
	}

	if (ctx->shader_info->info.loads_push_constants) {
		/* 1 for push constants and dynamic descriptors */
		add_array_arg(args, type, &ctx->abi.push_constants);
	}
}

static void
declare_vs_specific_input_sgprs(struct radv_shader_context *ctx,
				gl_shader_stage stage,
				bool has_previous_stage,
				gl_shader_stage previous_stage,
				struct arg_info *args)
{
	if (!ctx->is_gs_copy_shader &&
	    (stage == MESA_SHADER_VERTEX ||
	     (has_previous_stage && previous_stage == MESA_SHADER_VERTEX))) {
		if (ctx->shader_info->info.vs.has_vertex_buffers) {
			add_arg(args, ARG_SGPR,
				ac_array_in_const32_addr_space(ctx->ac.v4i32),
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
declare_vs_input_vgprs(struct radv_shader_context *ctx, struct arg_info *args)
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
declare_tes_input_vgprs(struct radv_shader_context *ctx, struct arg_info *args)
{
	add_arg(args, ARG_VGPR, ctx->ac.f32, &ctx->tes_u);
	add_arg(args, ARG_VGPR, ctx->ac.f32, &ctx->tes_v);
	add_arg(args, ARG_VGPR, ctx->ac.i32, &ctx->tes_rel_patch_id);
	add_arg(args, ARG_VGPR, ctx->ac.i32, &ctx->abi.tes_patch_id);
}

static void
set_global_input_locs(struct radv_shader_context *ctx, gl_shader_stage stage,
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
			if ((ctx->shader_info->info.desc_set_used_mask & (1 << i)) &&
			    ctx->options->layout->set[i].layout->shader_stages & stage_mask) {
				set_loc_desc(ctx, i, user_sgpr_idx, 0);
			} else
				ctx->descriptor_sets[i] = NULL;
		}
	} else {
		set_loc_shader_ptr(ctx, AC_UD_INDIRECT_DESCRIPTOR_SETS,
			           user_sgpr_idx);

		for (unsigned i = 0; i < num_sets; ++i) {
			if ((ctx->shader_info->info.desc_set_used_mask & (1 << i)) &&
			    ctx->options->layout->set[i].layout->shader_stages & stage_mask) {
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
		set_loc_shader_ptr(ctx, AC_UD_PUSH_CONSTANTS, user_sgpr_idx);
	}
}

static void
set_vs_specific_input_locs(struct radv_shader_context *ctx,
			   gl_shader_stage stage, bool has_previous_stage,
			   gl_shader_stage previous_stage,
			   uint8_t *user_sgpr_idx)
{
	if (!ctx->is_gs_copy_shader &&
	    (stage == MESA_SHADER_VERTEX ||
	     (has_previous_stage && previous_stage == MESA_SHADER_VERTEX))) {
		if (ctx->shader_info->info.vs.has_vertex_buffers) {
			set_loc_shader_ptr(ctx, AC_UD_VS_VERTEX_BUFFERS,
					   user_sgpr_idx);
		}

		unsigned vs_num = 2;
		if (ctx->shader_info->info.vs.needs_draw_id)
			vs_num++;

		set_loc_shader(ctx, AC_UD_VS_BASE_VERTEX_START_INSTANCE,
			       user_sgpr_idx, vs_num);
	}
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
		calling_conv = RADEON_LLVM_AMDGPU_HS;
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

static void create_function(struct radv_shader_context *ctx,
                            gl_shader_stage stage,
                            bool has_previous_stage,
                            gl_shader_stage previous_stage)
{
	uint8_t user_sgpr_idx;
	struct user_sgpr_info user_sgpr_info;
	struct arg_info args = {};
	LLVMValueRef desc_sets;
	bool needs_view_index = needs_view_index_sgpr(ctx, stage);
	allocate_user_sgprs(ctx, stage, has_previous_stage,
			    previous_stage, needs_view_index, &user_sgpr_info);

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
				&ctx->abi.num_work_groups);
		}

		for (int i = 0; i < 3; i++) {
			ctx->abi.workgroup_ids[i] = NULL;
			if (ctx->shader_info->info.cs.uses_block_id[i]) {
				add_arg(&args, ARG_SGPR, ctx->ac.i32,
					&ctx->abi.workgroup_ids[i]);
			}
		}

		if (ctx->shader_info->info.cs.uses_local_invocation_idx)
			add_arg(&args, ARG_SGPR, ctx->ac.i32, &ctx->abi.tg_size);
		add_arg(&args, ARG_VGPR, ctx->ac.v3i32,
			&ctx->abi.local_invocation_ids);
		break;
	case MESA_SHADER_VERTEX:
		declare_global_input_sgprs(ctx, stage, has_previous_stage,
					   previous_stage, &user_sgpr_info,
					   &args, &desc_sets);
		declare_vs_specific_input_sgprs(ctx, stage, has_previous_stage,
						previous_stage, &args);

		if (needs_view_index)
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->abi.view_index);
		if (ctx->options->key.vs.as_es)
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->es2gs_offset);

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

			if (needs_view_index)
				add_arg(&args, ARG_SGPR, ctx->ac.i32,
					&ctx->abi.view_index);

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

			if (needs_view_index)
				add_arg(&args, ARG_SGPR, ctx->ac.i32,
					&ctx->abi.view_index);

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

		if (needs_view_index)
			add_arg(&args, ARG_SGPR, ctx->ac.i32,
				&ctx->abi.view_index);

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

			if (previous_stage != MESA_SHADER_TESS_EVAL) {
				declare_vs_specific_input_sgprs(ctx, stage,
								has_previous_stage,
								previous_stage,
								&args);
			}

			if (needs_view_index)
				add_arg(&args, ARG_SGPR, ctx->ac.i32,
					&ctx->abi.view_index);

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

			if (needs_view_index)
				add_arg(&args, ARG_SGPR, ctx->ac.i32,
					&ctx->abi.view_index);

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
	    ctx->context, ctx->ac.module, ctx->ac.builder, NULL, 0, &args,
	    ctx->max_workgroup_size, ctx->options);
	set_llvm_calling_convention(ctx->main_function, stage);


	ctx->shader_info->num_input_vgprs = 0;
	ctx->shader_info->num_input_sgprs = ctx->options->supports_spill ? 2 : 0;

	ctx->shader_info->num_input_sgprs += args.num_sgprs_used;

	if (ctx->stage != MESA_SHADER_FRAGMENT)
		ctx->shader_info->num_input_vgprs = args.num_vgprs_used;

	assign_arguments(ctx->main_function, &args);

	user_sgpr_idx = 0;

	if (ctx->options->supports_spill || user_sgpr_info.need_ring_offsets) {
		set_loc_shader_ptr(ctx, AC_UD_SCRATCH_RING_OFFSETS,
				   &user_sgpr_idx);
		if (ctx->options->supports_spill) {
			ctx->ring_offsets = ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.implicit.buffer.ptr",
							       LLVMPointerType(ctx->ac.i8, AC_CONST_ADDR_SPACE),
							       NULL, 0, AC_FUNC_ATTR_READNONE);
			ctx->ring_offsets = LLVMBuildBitCast(ctx->ac.builder, ctx->ring_offsets,
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
		if (ctx->abi.view_index)
			set_loc_shader(ctx, AC_UD_VIEW_INDEX, &user_sgpr_idx, 1);
		break;
	case MESA_SHADER_TESS_CTRL:
		set_vs_specific_input_locs(ctx, stage, has_previous_stage,
					   previous_stage, &user_sgpr_idx);
		if (ctx->abi.view_index)
			set_loc_shader(ctx, AC_UD_VIEW_INDEX, &user_sgpr_idx, 1);
		break;
	case MESA_SHADER_TESS_EVAL:
		if (ctx->abi.view_index)
			set_loc_shader(ctx, AC_UD_VIEW_INDEX, &user_sgpr_idx, 1);
		break;
	case MESA_SHADER_GEOMETRY:
		if (has_previous_stage) {
			if (previous_stage == MESA_SHADER_VERTEX)
				set_vs_specific_input_locs(ctx, stage,
							   has_previous_stage,
							   previous_stage,
							   &user_sgpr_idx);
		}
		if (ctx->abi.view_index)
			set_loc_shader(ctx, AC_UD_VIEW_INDEX, &user_sgpr_idx, 1);
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

	if (stage == MESA_SHADER_TESS_CTRL ||
	    (stage == MESA_SHADER_VERTEX && ctx->options->key.vs.as_ls) ||
	    /* GFX9 has the ESGS ring buffer in LDS. */
	    (stage == MESA_SHADER_GEOMETRY && has_previous_stage)) {
		ac_declare_lds_as_pointer(&ctx->ac);
	}

	ctx->shader_info->num_user_sgprs = user_sgpr_idx;
}


static LLVMValueRef
radv_load_resource(struct ac_shader_abi *abi, LLVMValueRef index,
		   unsigned desc_set, unsigned binding)
{
	struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
	LLVMValueRef desc_ptr = ctx->descriptor_sets[desc_set];
	struct radv_pipeline_layout *pipeline_layout = ctx->options->layout;
	struct radv_descriptor_set_layout *layout = pipeline_layout->set[desc_set].layout;
	unsigned base_offset = layout->binding[binding].offset;
	LLVMValueRef offset, stride;

	if (layout->binding[binding].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
	    layout->binding[binding].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
		unsigned idx = pipeline_layout->set[desc_set].dynamic_offset_start +
			layout->binding[binding].dynamic_offset_offset;
		desc_ptr = ctx->abi.push_constants;
		base_offset = pipeline_layout->push_constant_size + 16 * idx;
		stride = LLVMConstInt(ctx->ac.i32, 16, false);
	} else
		stride = LLVMConstInt(ctx->ac.i32, layout->binding[binding].size, false);

	offset = LLVMConstInt(ctx->ac.i32, base_offset, false);
	index = LLVMBuildMul(ctx->ac.builder, index, stride, "");
	offset = LLVMBuildAdd(ctx->ac.builder, offset, index, "");

	desc_ptr = ac_build_gep0(&ctx->ac, desc_ptr, offset);
	desc_ptr = ac_cast_ptr(&ctx->ac, desc_ptr, ctx->ac.v4i32);
	LLVMSetMetadata(desc_ptr, ctx->ac.uniform_md_kind, ctx->ac.empty_md);

	return desc_ptr;
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
static LLVMValueRef get_non_vertex_index_offset(struct radv_shader_context *ctx)
{
	uint32_t num_patches = ctx->tcs_num_patches;
	uint32_t num_tcs_outputs;
	if (ctx->stage == MESA_SHADER_TESS_CTRL)
		num_tcs_outputs = util_last_bit64(ctx->shader_info->info.tcs.outputs_written);
	else
		num_tcs_outputs = ctx->options->key.tes.tcs_num_outputs;

	uint32_t output_vertex_size = num_tcs_outputs * 16;
	uint32_t pervertex_output_patch_size = ctx->tcs_vertices_per_patch * output_vertex_size;

	return LLVMConstInt(ctx->ac.i32, pervertex_output_patch_size * num_patches, false);
}

static LLVMValueRef calc_param_stride(struct radv_shader_context *ctx,
				      LLVMValueRef vertex_index)
{
	LLVMValueRef param_stride;
	if (vertex_index)
		param_stride = LLVMConstInt(ctx->ac.i32, ctx->tcs_vertices_per_patch * ctx->tcs_num_patches, false);
	else
		param_stride = LLVMConstInt(ctx->ac.i32, ctx->tcs_num_patches, false);
	return param_stride;
}

static LLVMValueRef get_tcs_tes_buffer_address(struct radv_shader_context *ctx,
                                               LLVMValueRef vertex_index,
                                               LLVMValueRef param_index)
{
	LLVMValueRef base_addr;
	LLVMValueRef param_stride, constant16;
	LLVMValueRef rel_patch_id = get_rel_patch_id(ctx);
	LLVMValueRef vertices_per_patch = LLVMConstInt(ctx->ac.i32, ctx->tcs_vertices_per_patch, false);
	constant16 = LLVMConstInt(ctx->ac.i32, 16, false);
	param_stride = calc_param_stride(ctx, vertex_index);
	if (vertex_index) {
		base_addr = LLVMBuildMul(ctx->ac.builder, rel_patch_id,
		                         vertices_per_patch, "");

		base_addr = LLVMBuildAdd(ctx->ac.builder, base_addr,
		                         vertex_index, "");
	} else {
		base_addr = rel_patch_id;
	}

	base_addr = LLVMBuildAdd(ctx->ac.builder, base_addr,
	                         LLVMBuildMul(ctx->ac.builder, param_index,
	                                      param_stride, ""), "");

	base_addr = LLVMBuildMul(ctx->ac.builder, base_addr, constant16, "");

	if (!vertex_index) {
		LLVMValueRef patch_data_offset = get_non_vertex_index_offset(ctx);

		base_addr = LLVMBuildAdd(ctx->ac.builder, base_addr,
		                         patch_data_offset, "");
	}
	return base_addr;
}

static LLVMValueRef get_tcs_tes_buffer_address_params(struct radv_shader_context *ctx,
						      unsigned param,
						      unsigned const_index,
						      bool is_compact,
						      LLVMValueRef vertex_index,
						      LLVMValueRef indir_index)
{
	LLVMValueRef param_index;

	if (indir_index)
		param_index = LLVMBuildAdd(ctx->ac.builder, LLVMConstInt(ctx->ac.i32, param, false),
					   indir_index, "");
	else {
		if (const_index && !is_compact)
			param += const_index;
		param_index = LLVMConstInt(ctx->ac.i32, param, false);
	}
	return get_tcs_tes_buffer_address(ctx, vertex_index, param_index);
}

static LLVMValueRef
get_dw_address(struct radv_shader_context *ctx,
	       LLVMValueRef dw_addr,
	       unsigned param,
	       unsigned const_index,
	       bool compact_const_index,
	       LLVMValueRef vertex_index,
	       LLVMValueRef stride,
	       LLVMValueRef indir_index)

{

	if (vertex_index) {
		dw_addr = LLVMBuildAdd(ctx->ac.builder, dw_addr,
				       LLVMBuildMul(ctx->ac.builder,
						    vertex_index,
						    stride, ""), "");
	}

	if (indir_index)
		dw_addr = LLVMBuildAdd(ctx->ac.builder, dw_addr,
				       LLVMBuildMul(ctx->ac.builder, indir_index,
						    LLVMConstInt(ctx->ac.i32, 4, false), ""), "");
	else if (const_index && !compact_const_index)
		dw_addr = LLVMBuildAdd(ctx->ac.builder, dw_addr,
				       LLVMConstInt(ctx->ac.i32, const_index * 4, false), "");

	dw_addr = LLVMBuildAdd(ctx->ac.builder, dw_addr,
			       LLVMConstInt(ctx->ac.i32, param * 4, false), "");

	if (const_index && compact_const_index)
		dw_addr = LLVMBuildAdd(ctx->ac.builder, dw_addr,
				       LLVMConstInt(ctx->ac.i32, const_index, false), "");
	return dw_addr;
}

static LLVMValueRef
load_tcs_varyings(struct ac_shader_abi *abi,
		  LLVMTypeRef type,
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
	struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
	LLVMValueRef dw_addr, stride;
	LLVMValueRef value[4], result;
	unsigned param = shader_io_get_unique_index(location);

	if (load_input) {
		uint32_t input_vertex_size = (ctx->tcs_num_inputs * 16) / 4;
		stride = LLVMConstInt(ctx->ac.i32, input_vertex_size, false);
		dw_addr = get_tcs_in_current_patch_offset(ctx);
	} else {
		if (!is_patch) {
			stride = get_tcs_out_vertex_stride(ctx);
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
		dw_addr = LLVMBuildAdd(ctx->ac.builder, dw_addr,
				       ctx->ac.i32_1, "");
	}
	result = ac_build_varying_gather_values(&ctx->ac, value, num_components, component);
	return result;
}

static void
store_tcs_output(struct ac_shader_abi *abi,
		 const nir_variable *var,
		 LLVMValueRef vertex_index,
		 LLVMValueRef param_index,
		 unsigned const_index,
		 LLVMValueRef src,
		 unsigned writemask)
{
	struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
	const unsigned location = var->data.location;
	const unsigned component = var->data.location_frac;
	const bool is_patch = var->data.patch;
	const bool is_compact = var->data.compact;
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
		stride = get_tcs_out_vertex_stride(ctx);
		dw_addr = get_tcs_out_current_patch_offset(ctx);
	} else {
		dw_addr = get_tcs_out_current_patch_data_offset(ctx);
	}

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
				LLVMBuildAdd(ctx->ac.builder, dw_addr,
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
	       LLVMTypeRef type,
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
	struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
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
	buf_addr = LLVMBuildAdd(ctx->ac.builder, buf_addr, comp_offset, "");

	result = ac_build_buffer_load(&ctx->ac, ctx->hs_ring_tess_offchip, num_components, NULL,
				      buf_addr, ctx->oc_lds, is_compact ? (4 * const_index) : 0, 1, 0, true, false);
	result = ac_trim_vector(&ctx->ac, result, num_components);
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
	struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
	LLVMValueRef vtx_offset;
	unsigned param, vtx_offset_param;
	LLVMValueRef value[4], result;

	vtx_offset_param = vertex_index;
	assert(vtx_offset_param < 6);
	vtx_offset = LLVMBuildMul(ctx->ac.builder, ctx->gs_vtx_offset[vtx_offset_param],
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

			value[i] = LLVMBuildBitCast(ctx->ac.builder, value[i],
						    type, "");
		}
	}
	result = ac_build_varying_gather_values(&ctx->ac, value, num_components, component);
	result = ac_to_integer(&ctx->ac, result);
	return result;
}


static void radv_emit_kill(struct ac_shader_abi *abi, LLVMValueRef visible)
{
	struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
	ac_build_kill_if_false(&ctx->ac, visible);
}

static LLVMValueRef lookup_interp_param(struct ac_shader_abi *abi,
					enum glsl_interp_mode interp, unsigned location)
{
	struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);

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
	struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);

	LLVMValueRef result;
	LLVMValueRef ptr = ac_build_gep0(&ctx->ac, ctx->ring_offsets, LLVMConstInt(ctx->ac.i32, RING_PS_SAMPLE_POSITIONS, false));

	ptr = LLVMBuildBitCast(ctx->ac.builder, ptr,
			       ac_array_in_const_addr_space(ctx->ac.v2f32), "");

	sample_id = LLVMBuildAdd(ctx->ac.builder, sample_id, ctx->sample_pos_offset, "");
	result = ac_build_load_invariant(&ctx->ac, ptr, sample_id);

	return result;
}


static LLVMValueRef load_sample_mask_in(struct ac_shader_abi *abi)
{
	struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
	uint8_t log2_ps_iter_samples = ctx->shader_info->info.ps.force_persample ?
		ctx->options->key.fs.log2_num_samples :
		ctx->options->key.fs.log2_ps_iter_samples;

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
	sample_id = ac_unpack_param(&ctx->ac, abi->ancillary, 8, 4);
	sample_id = LLVMBuildShl(ctx->ac.builder, LLVMConstInt(ctx->ac.i32, ps_iter_mask, false), sample_id, "");
	result = LLVMBuildAnd(ctx->ac.builder, sample_id, abi->sample_coverage, "");
	return result;
}


static void
visit_emit_vertex(struct ac_shader_abi *abi, unsigned stream, LLVMValueRef *addrs)
{
	LLVMValueRef gs_next_vertex;
	LLVMValueRef can_emit;
	int idx;
	struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);

	assert(stream == 0);

	/* Write vertex attribute values to GSVS ring */
	gs_next_vertex = LLVMBuildLoad(ctx->ac.builder,
				       ctx->gs_next_vertex,
				       "");

	/* If this thread has already emitted the declared maximum number of
	 * vertices, kill it: excessive vertex emissions are not supposed to
	 * have any effect, and GS threads have no externally observable
	 * effects other than emitting vertices.
	 */
	can_emit = LLVMBuildICmp(ctx->ac.builder, LLVMIntULT, gs_next_vertex,
				 LLVMConstInt(ctx->ac.i32, ctx->gs_max_out_vertices, false), "");
	ac_build_kill_if_false(&ctx->ac, can_emit);

	/* loop num outputs */
	idx = 0;
	for (unsigned i = 0; i < AC_LLVM_MAX_OUTPUTS; ++i) {
		unsigned output_usage_mask =
			ctx->shader_info->info.gs.output_usage_mask[i];
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
			output_usage_mask = (1 << length) - 1;
		}

		for (unsigned j = 0; j < length; j++) {
			if (!(output_usage_mask & (1 << j)))
				continue;

			LLVMValueRef out_val = LLVMBuildLoad(ctx->ac.builder,
							     out_ptr[j], "");
			LLVMValueRef voffset = LLVMConstInt(ctx->ac.i32, (slot * 4 + j) * ctx->gs_max_out_vertices, false);
			voffset = LLVMBuildAdd(ctx->ac.builder, voffset, gs_next_vertex, "");
			voffset = LLVMBuildMul(ctx->ac.builder, voffset, LLVMConstInt(ctx->ac.i32, 4, false), "");

			out_val = LLVMBuildBitCast(ctx->ac.builder, out_val, ctx->ac.i32, "");

			ac_build_buffer_store_dword(&ctx->ac, ctx->gsvs_ring,
						    out_val, 1,
						    voffset, ctx->gs2vs_offset, 0,
						    1, 1, true, true);
		}
		idx += slot_inc;
	}

	gs_next_vertex = LLVMBuildAdd(ctx->ac.builder, gs_next_vertex,
				      ctx->ac.i32_1, "");
	LLVMBuildStore(ctx->ac.builder, gs_next_vertex, ctx->gs_next_vertex);

	ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_EMIT | AC_SENDMSG_GS | (0 << 8), ctx->gs_wave_id);
}

static void
visit_end_primitive(struct ac_shader_abi *abi, unsigned stream)
{
	struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
	ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_CUT | AC_SENDMSG_GS | (stream << 8), ctx->gs_wave_id);
}

static LLVMValueRef
load_tess_coord(struct ac_shader_abi *abi)
{
	struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);

	LLVMValueRef coord[4] = {
		ctx->tes_u,
		ctx->tes_v,
		ctx->ac.f32_0,
		ctx->ac.f32_0,
	};

	if (ctx->tes_primitive_mode == GL_TRIANGLES)
		coord[2] = LLVMBuildFSub(ctx->ac.builder, ctx->ac.f32_1,
					LLVMBuildFAdd(ctx->ac.builder, coord[0], coord[1], ""), "");

	return ac_build_gather_values(&ctx->ac, coord, 3);
}

static LLVMValueRef
load_patch_vertices_in(struct ac_shader_abi *abi)
{
	struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
	return LLVMConstInt(ctx->ac.i32, ctx->options->key.tcs.input_vertices, false);
}


static LLVMValueRef radv_load_base_vertex(struct ac_shader_abi *abi)
{
	return abi->base_vertex;
}

static LLVMValueRef radv_load_ssbo(struct ac_shader_abi *abi,
				   LLVMValueRef buffer_ptr, bool write)
{
	struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
	LLVMValueRef result;

	LLVMSetMetadata(buffer_ptr, ctx->ac.uniform_md_kind, ctx->ac.empty_md);

	result = LLVMBuildLoad(ctx->ac.builder, buffer_ptr, "");
	LLVMSetMetadata(result, ctx->ac.invariant_load_md_kind, ctx->ac.empty_md);

	return result;
}

static LLVMValueRef radv_load_ubo(struct ac_shader_abi *abi, LLVMValueRef buffer_ptr)
{
	struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
	LLVMValueRef result;

	LLVMSetMetadata(buffer_ptr, ctx->ac.uniform_md_kind, ctx->ac.empty_md);

	result = LLVMBuildLoad(ctx->ac.builder, buffer_ptr, "");
	LLVMSetMetadata(result, ctx->ac.invariant_load_md_kind, ctx->ac.empty_md);

	return result;
}

static LLVMValueRef radv_get_sampler_desc(struct ac_shader_abi *abi,
					  unsigned descriptor_set,
					  unsigned base_index,
					  unsigned constant_index,
					  LLVMValueRef index,
					  enum ac_descriptor_type desc_type,
					  bool image, bool write,
					  bool bindless)
{
	struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);
	LLVMValueRef list = ctx->descriptor_sets[descriptor_set];
	struct radv_descriptor_set_layout *layout = ctx->options->layout->set[descriptor_set].layout;
	struct radv_descriptor_set_binding_layout *binding = layout->binding + base_index;
	unsigned offset = binding->offset;
	unsigned stride = binding->size;
	unsigned type_size;
	LLVMBuilderRef builder = ctx->ac.builder;
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
	list = LLVMBuildPointerCast(builder, list,
				    ac_array_in_const32_addr_space(type), "");

	return ac_build_load_to_sgpr(&ctx->ac, list, index);
}

/* For 2_10_10_10 formats the alpha is handled as unsigned by pre-vega HW.
 * so we may need to fix it up. */
static LLVMValueRef
adjust_vertex_fetch_alpha(struct radv_shader_context *ctx,
                          unsigned adjustment,
                          LLVMValueRef alpha)
{
	if (adjustment == RADV_ALPHA_ADJUST_NONE)
		return alpha;

	LLVMValueRef c30 = LLVMConstInt(ctx->ac.i32, 30, 0);

	if (adjustment == RADV_ALPHA_ADJUST_SSCALED)
		alpha = LLVMBuildFPToUI(ctx->ac.builder, alpha, ctx->ac.i32, "");
	else
		alpha = ac_to_integer(&ctx->ac, alpha);

	/* For the integer-like cases, do a natural sign extension.
	 *
	 * For the SNORM case, the values are 0.0, 0.333, 0.666, 1.0
	 * and happen to contain 0, 1, 2, 3 as the two LSBs of the
	 * exponent.
	 */
	alpha = LLVMBuildShl(ctx->ac.builder, alpha,
	                     adjustment == RADV_ALPHA_ADJUST_SNORM ?
	                     LLVMConstInt(ctx->ac.i32, 7, 0) : c30, "");
	alpha = LLVMBuildAShr(ctx->ac.builder, alpha, c30, "");

	/* Convert back to the right type. */
	if (adjustment == RADV_ALPHA_ADJUST_SNORM) {
		LLVMValueRef clamp;
		LLVMValueRef neg_one = LLVMConstReal(ctx->ac.f32, -1.0);
		alpha = LLVMBuildSIToFP(ctx->ac.builder, alpha, ctx->ac.f32, "");
		clamp = LLVMBuildFCmp(ctx->ac.builder, LLVMRealULT, alpha, neg_one, "");
		alpha = LLVMBuildSelect(ctx->ac.builder, clamp, neg_one, alpha, "");
	} else if (adjustment == RADV_ALPHA_ADJUST_SSCALED) {
		alpha = LLVMBuildSIToFP(ctx->ac.builder, alpha, ctx->ac.f32, "");
	}

	return alpha;
}

static void
handle_vs_input_decl(struct radv_shader_context *ctx,
		     struct nir_variable *variable)
{
	LLVMValueRef t_list_ptr = ctx->vertex_buffers;
	LLVMValueRef t_offset;
	LLVMValueRef t_list;
	LLVMValueRef input;
	LLVMValueRef buffer_index;
	unsigned attrib_count = glsl_count_attribute_slots(variable->type, true);
	uint8_t input_usage_mask =
		ctx->shader_info->info.vs.input_usage_mask[variable->data.location];
	unsigned num_channels = util_last_bit(input_usage_mask);

	variable->data.driver_location = variable->data.location * 4;

	for (unsigned i = 0; i < attrib_count; ++i) {
		LLVMValueRef output[4];
		unsigned attrib_index = variable->data.location + i - VERT_ATTRIB_GENERIC0;

		if (ctx->options->key.vs.instance_rate_inputs & (1u << attrib_index)) {
			uint32_t divisor = ctx->options->key.vs.instance_rate_divisors[attrib_index];

			if (divisor) {
				buffer_index = LLVMBuildAdd(ctx->ac.builder, ctx->abi.instance_id,
				                            ctx->abi.start_instance, "");

				if (divisor != 1) {
					buffer_index = LLVMBuildUDiv(ctx->ac.builder, buffer_index,
					                             LLVMConstInt(ctx->ac.i32, divisor, 0), "");
				}

				if (ctx->options->key.vs.as_ls) {
					ctx->shader_info->vs.vgpr_comp_cnt =
						MAX2(2, ctx->shader_info->vs.vgpr_comp_cnt);
				} else {
					ctx->shader_info->vs.vgpr_comp_cnt =
						MAX2(1, ctx->shader_info->vs.vgpr_comp_cnt);
				}
			} else {
				buffer_index = ctx->ac.i32_0;
			}
		} else
			buffer_index = LLVMBuildAdd(ctx->ac.builder, ctx->abi.vertex_id,
			                            ctx->abi.base_vertex, "");
		t_offset = LLVMConstInt(ctx->ac.i32, attrib_index, false);

		t_list = ac_build_load_to_sgpr(&ctx->ac, t_list_ptr, t_offset);

		input = ac_build_buffer_load_format(&ctx->ac, t_list,
						    buffer_index,
						    ctx->ac.i32_0,
						    num_channels, false, true);

		input = ac_build_expand_to_vec4(&ctx->ac, input, num_channels);

		for (unsigned chan = 0; chan < 4; chan++) {
			LLVMValueRef llvm_chan = LLVMConstInt(ctx->ac.i32, chan, false);
			output[chan] = LLVMBuildExtractElement(ctx->ac.builder, input, llvm_chan, "");
		}

		unsigned alpha_adjust = (ctx->options->key.vs.alpha_adjust >> (attrib_index * 2)) & 3;
		output[3] = adjust_vertex_fetch_alpha(ctx, alpha_adjust, output[3]);

		for (unsigned chan = 0; chan < 4; chan++) {
			ctx->inputs[ac_llvm_reg_index_soa(variable->data.location + i, chan)] =
				ac_to_integer(&ctx->ac, output[chan]);
		}
	}
}

static void interp_fs_input(struct radv_shader_context *ctx,
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
		interp_param = LLVMBuildBitCast(ctx->ac.builder, interp_param,
						ctx->ac.v2f32, "");

		i = LLVMBuildExtractElement(ctx->ac.builder, interp_param,
						ctx->ac.i32_0, "");
		j = LLVMBuildExtractElement(ctx->ac.builder, interp_param,
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
handle_fs_input_decl(struct radv_shader_context *ctx,
		     struct nir_variable *variable)
{
	int idx = variable->data.location;
	unsigned attrib_count = glsl_count_attribute_slots(variable->type, false);
	LLVMValueRef interp;

	variable->data.driver_location = idx * 4;
	ctx->input_mask |= ((1ull << attrib_count) - 1) << variable->data.location;

	if (glsl_get_base_type(glsl_without_array(variable->type)) == GLSL_TYPE_FLOAT) {
		unsigned interp_type;
		if (variable->data.sample)
			interp_type = INTERP_SAMPLE;
		else if (variable->data.centroid)
			interp_type = INTERP_CENTROID;
		else
			interp_type = INTERP_CENTER;

		interp = lookup_interp_param(&ctx->abi, variable->data.interpolation, interp_type);
	} else
		interp = NULL;

	for (unsigned i = 0; i < attrib_count; ++i)
		ctx->inputs[ac_llvm_reg_index_soa(idx + i, 0)] = interp;

}

static void
handle_vs_inputs(struct radv_shader_context *ctx,
                 struct nir_shader *nir) {
	nir_foreach_variable(variable, &nir->inputs)
		handle_vs_input_decl(ctx, variable);
}

static void
prepare_interp_optimize(struct radv_shader_context *ctx,
                        struct nir_shader *nir)
{
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
		LLVMValueRef sel = LLVMBuildICmp(ctx->ac.builder, LLVMIntSLT, ctx->abi.prim_mask, ctx->ac.i32_0, "");
		ctx->persp_centroid = LLVMBuildSelect(ctx->ac.builder, sel, ctx->persp_center, ctx->persp_centroid, "");
		ctx->linear_centroid = LLVMBuildSelect(ctx->ac.builder, sel, ctx->linear_center, ctx->linear_centroid, "");
	}
}

static void
handle_fs_inputs(struct radv_shader_context *ctx,
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
		LLVMValueRef *inputs = ctx->inputs +ac_llvm_reg_index_soa(i, 0);

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
	ctx->shader_info->fs.input_mask = ctx->input_mask >> VARYING_SLOT_VAR0;

	if (ctx->shader_info->info.needs_multiview_view_index)
		ctx->abi.view_index = ctx->inputs[ac_llvm_reg_index_soa(VARYING_SLOT_LAYER, 0)];
}

static void
scan_shader_output_decl(struct radv_shader_context *ctx,
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


/* Initialize arguments for the shader export intrinsic */
static void
si_llvm_init_export_args(struct radv_shader_context *ctx,
			 LLVMValueRef *values,
			 unsigned enabled_channels,
			 unsigned target,
			 struct ac_export_args *args)
{
	/* Specify the channels that are enabled. */
	args->enabled_channels = enabled_channels;

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

	if (ctx->stage == MESA_SHADER_FRAGMENT && target >= V_008DFC_SQ_EXP_MRT) {
		unsigned index = target - V_008DFC_SQ_EXP_MRT;
		unsigned col_format = (ctx->options->key.fs.col_format >> (4 * index)) & 0xf;
		bool is_int8 = (ctx->options->key.fs.is_int8 >> index) & 1;
		bool is_int10 = (ctx->options->key.fs.is_int10 >> index) & 1;
		unsigned chan;

		LLVMValueRef (*packf)(struct ac_llvm_context *ctx, LLVMValueRef args[2]) = NULL;
		LLVMValueRef (*packi)(struct ac_llvm_context *ctx, LLVMValueRef args[2],
				      unsigned bits, bool hi) = NULL;

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
			args->enabled_channels = 0x5;
			packf = ac_build_cvt_pkrtz_f16;
			break;

		case V_028714_SPI_SHADER_UNORM16_ABGR:
			args->enabled_channels = 0x5;
			packf = ac_build_cvt_pknorm_u16;
			break;

		case V_028714_SPI_SHADER_SNORM16_ABGR:
			args->enabled_channels = 0x5;
			packf = ac_build_cvt_pknorm_i16;
			break;

		case V_028714_SPI_SHADER_UINT16_ABGR:
			args->enabled_channels = 0x5;
			packi = ac_build_cvt_pk_u16;
			break;

		case V_028714_SPI_SHADER_SINT16_ABGR:
			args->enabled_channels = 0x5;
			packi = ac_build_cvt_pk_i16;
			break;

		default:
		case V_028714_SPI_SHADER_32_ABGR:
			memcpy(&args->out[0], values, sizeof(values[0]) * 4);
			break;
		}

		/* Pack f16 or norm_i16/u16. */
		if (packf) {
			for (chan = 0; chan < 2; chan++) {
				LLVMValueRef pack_args[2] = {
					values[2 * chan],
					values[2 * chan + 1]
				};
				LLVMValueRef packed;

				packed = packf(&ctx->ac, pack_args);
				args->out[chan] = ac_to_float(&ctx->ac, packed);
			}
			args->compr = 1; /* COMPR flag */
		}

		/* Pack i16/u16. */
		if (packi) {
			for (chan = 0; chan < 2; chan++) {
				LLVMValueRef pack_args[2] = {
					ac_to_integer(&ctx->ac, values[2 * chan]),
					ac_to_integer(&ctx->ac, values[2 * chan + 1])
				};
				LLVMValueRef packed;

				packed = packi(&ctx->ac, pack_args,
					       is_int8 ? 8 : is_int10 ? 10 : 16,
					       chan == 1);
				args->out[chan] = ac_to_float(&ctx->ac, packed);
			}
			args->compr = 1; /* COMPR flag */
		}
		return;
	}

	memcpy(&args->out[0], values, sizeof(values[0]) * 4);

	for (unsigned i = 0; i < 4; ++i) {
		if (!(args->enabled_channels & (1 << i)))
			continue;

		args->out[i] = ac_to_float(&ctx->ac, args->out[i]);
	}
}

static void
radv_export_param(struct radv_shader_context *ctx, unsigned index,
		  LLVMValueRef *values, unsigned enabled_channels)
{
	struct ac_export_args args;

	si_llvm_init_export_args(ctx, values, enabled_channels,
				 V_008DFC_SQ_EXP_PARAM + index, &args);
	ac_build_export(&ctx->ac, &args);
}

static LLVMValueRef
radv_load_output(struct radv_shader_context *ctx, unsigned index, unsigned chan)
{
	LLVMValueRef output =
		ctx->abi.outputs[ac_llvm_reg_index_soa(index, chan)];

	return LLVMBuildLoad(ctx->ac.builder, output, "");
}

static void
handle_vs_outputs_post(struct radv_shader_context *ctx,
		       bool export_prim_id, bool export_layer_id,
		       struct radv_vs_output_info *outinfo)
{
	uint32_t param_count = 0;
	unsigned target;
	unsigned pos_idx, num_pos_exports = 0;
	struct ac_export_args args, pos_args[4] = {};
	LLVMValueRef psize_value = NULL, layer_value = NULL, viewport_index_value = NULL;
	int i;

	if (ctx->options->key.has_multiview_view_index) {
		LLVMValueRef* tmp_out = &ctx->abi.outputs[ac_llvm_reg_index_soa(VARYING_SLOT_LAYER, 0)];
		if(!*tmp_out) {
			for(unsigned i = 0; i < 4; ++i)
				ctx->abi.outputs[ac_llvm_reg_index_soa(VARYING_SLOT_LAYER, i)] =
				            ac_build_alloca_undef(&ctx->ac, ctx->ac.f32, "");
		}

		LLVMBuildStore(ctx->ac.builder, ac_to_float(&ctx->ac, ctx->abi.view_index),  *tmp_out);
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
			slots[j] = ac_to_float(&ctx->ac, radv_load_output(ctx, i, j));

		for (i = ctx->num_output_clips + ctx->num_output_culls; i < 8; i++)
			slots[i] = LLVMGetUndef(ctx->ac.f32);

		if (ctx->num_output_clips + ctx->num_output_culls > 4) {
			target = V_008DFC_SQ_EXP_POS + 3;
			si_llvm_init_export_args(ctx, &slots[4], 0xf, target, &args);
			memcpy(&pos_args[target - V_008DFC_SQ_EXP_POS],
			       &args, sizeof(args));
		}

		target = V_008DFC_SQ_EXP_POS + 2;
		si_llvm_init_export_args(ctx, &slots[0], 0xf, target, &args);
		memcpy(&pos_args[target - V_008DFC_SQ_EXP_POS],
		       &args, sizeof(args));

	}

	LLVMValueRef pos_values[4] = {ctx->ac.f32_0, ctx->ac.f32_0, ctx->ac.f32_0, ctx->ac.f32_1};
	if (ctx->output_mask & (1ull << VARYING_SLOT_POS)) {
		for (unsigned j = 0; j < 4; j++)
			pos_values[j] = radv_load_output(ctx, VARYING_SLOT_POS, j);
	}
	si_llvm_init_export_args(ctx, pos_values, 0xf, V_008DFC_SQ_EXP_POS, &pos_args[0]);

	if (ctx->output_mask & (1ull << VARYING_SLOT_PSIZ)) {
		outinfo->writes_pointsize = true;
		psize_value = radv_load_output(ctx, VARYING_SLOT_PSIZ, 0);
	}

	if (ctx->output_mask & (1ull << VARYING_SLOT_LAYER)) {
		outinfo->writes_layer = true;
		layer_value = radv_load_output(ctx, VARYING_SLOT_LAYER, 0);
	}

	if (ctx->output_mask & (1ull << VARYING_SLOT_VIEWPORT)) {
		outinfo->writes_viewport_index = true;
		viewport_index_value = radv_load_output(ctx, VARYING_SLOT_VIEWPORT, 0);
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
				v = LLVMBuildShl(ctx->ac.builder, v,
						 LLVMConstInt(ctx->ac.i32, 16, false),
						 "");
				v = LLVMBuildOr(ctx->ac.builder, v,
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

	for (unsigned i = 0; i < AC_LLVM_MAX_OUTPUTS; ++i) {
		LLVMValueRef values[4];
		if (!(ctx->output_mask & (1ull << i)))
			continue;

		if (i != VARYING_SLOT_LAYER &&
		    i != VARYING_SLOT_PRIMITIVE_ID &&
		    i < VARYING_SLOT_VAR0)
			continue;

		for (unsigned j = 0; j < 4; j++)
			values[j] = ac_to_float(&ctx->ac, radv_load_output(ctx, i, j));

		unsigned output_usage_mask;

		if (ctx->stage == MESA_SHADER_VERTEX &&
		    !ctx->is_gs_copy_shader) {
			output_usage_mask =
				ctx->shader_info->info.vs.output_usage_mask[i];
		} else if (ctx->stage == MESA_SHADER_TESS_EVAL) {
			output_usage_mask =
				ctx->shader_info->info.tes.output_usage_mask[i];
		} else {
			assert(ctx->is_gs_copy_shader);
			output_usage_mask =
				ctx->shader_info->info.gs.output_usage_mask[i];
		}

		radv_export_param(ctx, param_count, values, output_usage_mask);

		outinfo->vs_output_param_offset[i] = param_count++;
	}

	if (export_prim_id) {
		LLVMValueRef values[4];

		values[0] = ctx->vs_prim_id;
		ctx->shader_info->vs.vgpr_comp_cnt = MAX2(2,
							  ctx->shader_info->vs.vgpr_comp_cnt);
		for (unsigned j = 1; j < 4; j++)
			values[j] = ctx->ac.f32_0;

		radv_export_param(ctx, param_count, values, 0x1);

		outinfo->vs_output_param_offset[VARYING_SLOT_PRIMITIVE_ID] = param_count++;
		outinfo->export_prim_id = true;
	}

	if (export_layer_id && layer_value) {
		LLVMValueRef values[4];

		values[0] = layer_value;
		for (unsigned j = 1; j < 4; j++)
			values[j] = ctx->ac.f32_0;

		radv_export_param(ctx, param_count, values, 0x1);

		outinfo->vs_output_param_offset[VARYING_SLOT_LAYER] = param_count++;
	}

	outinfo->pos_exports = num_pos_exports;
	outinfo->param_exports = param_count;
}

static void
handle_es_outputs_post(struct radv_shader_context *ctx,
		       struct radv_es_output_info *outinfo)
{
	int j;
	uint64_t max_output_written = 0;
	LLVMValueRef lds_base = NULL;

	for (unsigned i = 0; i < AC_LLVM_MAX_OUTPUTS; ++i) {
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

	for (unsigned i = 0; i < AC_LLVM_MAX_OUTPUTS; ++i) {
		LLVMValueRef dw_addr = NULL;
		LLVMValueRef *out_ptr = &ctx->abi.outputs[i * 4];
		unsigned output_usage_mask;
		int param_index;
		int length = 4;

		if (!(ctx->output_mask & (1ull << i)))
			continue;

		if (ctx->stage == MESA_SHADER_VERTEX) {
			output_usage_mask =
				ctx->shader_info->info.vs.output_usage_mask[i];
		} else {
			assert(ctx->stage == MESA_SHADER_TESS_EVAL);
			output_usage_mask =
				ctx->shader_info->info.tes.output_usage_mask[i];
		}

		if (i == VARYING_SLOT_CLIP_DIST0) {
			length = ctx->num_output_clips + ctx->num_output_culls;
			output_usage_mask = (1 << length) - 1;
		}

		param_index = shader_io_get_unique_index(i);

		if (lds_base) {
			dw_addr = LLVMBuildAdd(ctx->ac.builder, lds_base,
			                       LLVMConstInt(ctx->ac.i32, param_index * 4, false),
			                       "");
		}

		for (j = 0; j < length; j++) {
			if (!(output_usage_mask & (1 << j)))
				continue;

			LLVMValueRef out_val = LLVMBuildLoad(ctx->ac.builder, out_ptr[j], "");
			out_val = LLVMBuildBitCast(ctx->ac.builder, out_val, ctx->ac.i32, "");

			if (ctx->ac.chip_class  >= GFX9) {
				LLVMValueRef dw_addr_offset =
					LLVMBuildAdd(ctx->ac.builder, dw_addr,
						     LLVMConstInt(ctx->ac.i32,
								  j, false), "");

				ac_lds_store(&ctx->ac, dw_addr_offset,
					     LLVMBuildLoad(ctx->ac.builder, out_ptr[j], ""));
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
handle_ls_outputs_post(struct radv_shader_context *ctx)
{
	LLVMValueRef vertex_id = ctx->rel_auto_id;
	uint32_t num_tcs_inputs = util_last_bit64(ctx->shader_info->info.vs.ls_outputs_written);
	LLVMValueRef vertex_dw_stride = LLVMConstInt(ctx->ac.i32, num_tcs_inputs * 4, false);
	LLVMValueRef base_dw_addr = LLVMBuildMul(ctx->ac.builder, vertex_id,
						 vertex_dw_stride, "");

	for (unsigned i = 0; i < AC_LLVM_MAX_OUTPUTS; ++i) {
		LLVMValueRef *out_ptr = &ctx->abi.outputs[i * 4];
		int length = 4;

		if (!(ctx->output_mask & (1ull << i)))
			continue;

		if (i == VARYING_SLOT_CLIP_DIST0)
			length = ctx->num_output_clips + ctx->num_output_culls;
		int param = shader_io_get_unique_index(i);
		LLVMValueRef dw_addr = LLVMBuildAdd(ctx->ac.builder, base_dw_addr,
						    LLVMConstInt(ctx->ac.i32, param * 4, false),
						    "");
		for (unsigned j = 0; j < length; j++) {
			ac_lds_store(&ctx->ac, dw_addr,
				     LLVMBuildLoad(ctx->ac.builder, out_ptr[j], ""));
			dw_addr = LLVMBuildAdd(ctx->ac.builder, dw_addr, ctx->ac.i32_1, "");
		}
	}
}

static void
write_tess_factors(struct radv_shader_context *ctx)
{
	unsigned stride, outer_comps, inner_comps;
	struct ac_build_if_state if_ctx, inner_if_ctx;
	LLVMValueRef invocation_id = ac_unpack_param(&ctx->ac, ctx->abi.tcs_rel_ids, 8, 5);
	LLVMValueRef rel_patch_id = ac_unpack_param(&ctx->ac, ctx->abi.tcs_rel_ids, 0, 8);
	unsigned tess_inner_index = 0, tess_outer_index;
	LLVMValueRef lds_base, lds_inner = NULL, lds_outer, byteoffset, buffer;
	LLVMValueRef out[6], vec0, vec1, tf_base, inner[4], outer[4];
	int i;
	ac_emit_barrier(&ctx->ac, ctx->stage);

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
			LLVMBuildICmp(ctx->ac.builder, LLVMIntEQ,
				      invocation_id, ctx->ac.i32_0, ""));

	lds_base = get_tcs_out_current_patch_data_offset(ctx);

	if (inner_comps) {
		tess_inner_index = shader_io_get_unique_index(VARYING_SLOT_TESS_LEVEL_INNER);
		lds_inner = LLVMBuildAdd(ctx->ac.builder, lds_base,
					 LLVMConstInt(ctx->ac.i32, tess_inner_index * 4, false), "");
	}

	tess_outer_index = shader_io_get_unique_index(VARYING_SLOT_TESS_LEVEL_OUTER);
	lds_outer = LLVMBuildAdd(ctx->ac.builder, lds_base,
				 LLVMConstInt(ctx->ac.i32, tess_outer_index * 4, false), "");

	for (i = 0; i < 4; i++) {
		inner[i] = LLVMGetUndef(ctx->ac.i32);
		outer[i] = LLVMGetUndef(ctx->ac.i32);
	}

	// LINES reversal
	if (ctx->options->key.tcs.primitive_mode == GL_ISOLINES) {
		outer[0] = out[1] = ac_lds_load(&ctx->ac, lds_outer);
		lds_outer = LLVMBuildAdd(ctx->ac.builder, lds_outer,
					 ctx->ac.i32_1, "");
		outer[1] = out[0] = ac_lds_load(&ctx->ac, lds_outer);
	} else {
		for (i = 0; i < outer_comps; i++) {
			outer[i] = out[i] =
				ac_lds_load(&ctx->ac, lds_outer);
			lds_outer = LLVMBuildAdd(ctx->ac.builder, lds_outer,
						 ctx->ac.i32_1, "");
		}
		for (i = 0; i < inner_comps; i++) {
			inner[i] = out[outer_comps+i] =
				ac_lds_load(&ctx->ac, lds_inner);
			lds_inner = LLVMBuildAdd(ctx->ac.builder, lds_inner,
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
	byteoffset = LLVMBuildMul(ctx->ac.builder, rel_patch_id,
				  LLVMConstInt(ctx->ac.i32, 4 * stride, false), "");
	unsigned tf_offset = 0;

	if (ctx->options->chip_class <= VI) {
		ac_nir_build_if(&inner_if_ctx, ctx,
		                LLVMBuildICmp(ctx->ac.builder, LLVMIntEQ,
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
handle_tcs_outputs_post(struct radv_shader_context *ctx)
{
	write_tess_factors(ctx);
}

static bool
si_export_mrt_color(struct radv_shader_context *ctx,
		    LLVMValueRef *color, unsigned index,
		    struct ac_export_args *args)
{
	/* Export */
	si_llvm_init_export_args(ctx, color, 0xf,
				 V_008DFC_SQ_EXP_MRT + index, args);
	if (!args->enabled_channels)
		return false; /* unnecessary NULL export */

	return true;
}

static void
radv_export_mrt_z(struct radv_shader_context *ctx,
		  LLVMValueRef depth, LLVMValueRef stencil,
		  LLVMValueRef samplemask)
{
	struct ac_export_args args;

	ac_export_mrt_z(&ctx->ac, depth, stencil, samplemask, &args);

	ac_build_export(&ctx->ac, &args);
}

static void
handle_fs_outputs_post(struct radv_shader_context *ctx)
{
	unsigned index = 0;
	LLVMValueRef depth = NULL, stencil = NULL, samplemask = NULL;
	struct ac_export_args color_args[8];

	for (unsigned i = 0; i < AC_LLVM_MAX_OUTPUTS; ++i) {
		LLVMValueRef values[4];

		if (!(ctx->output_mask & (1ull << i)))
			continue;

		if (i < FRAG_RESULT_DATA0)
			continue;

		for (unsigned j = 0; j < 4; j++)
			values[j] = ac_to_float(&ctx->ac,
						radv_load_output(ctx, i, j));

		bool ret = si_export_mrt_color(ctx, values,
					       i - FRAG_RESULT_DATA0,
					       &color_args[index]);
		if (ret)
			index++;
	}

	/* Process depth, stencil, samplemask. */
	if (ctx->shader_info->info.ps.writes_z) {
		depth = ac_to_float(&ctx->ac,
				    radv_load_output(ctx, FRAG_RESULT_DEPTH, 0));
	}
	if (ctx->shader_info->info.ps.writes_stencil) {
		stencil = ac_to_float(&ctx->ac,
				      radv_load_output(ctx, FRAG_RESULT_STENCIL, 0));
	}
	if (ctx->shader_info->info.ps.writes_sample_mask) {
		samplemask = ac_to_float(&ctx->ac,
					 radv_load_output(ctx, FRAG_RESULT_SAMPLE_MASK, 0));
	}

	/* Set the DONE bit on last non-null color export only if Z isn't
	 * exported.
	 */
	if (index > 0 &&
	    !ctx->shader_info->info.ps.writes_z &&
	    !ctx->shader_info->info.ps.writes_stencil &&
	    !ctx->shader_info->info.ps.writes_sample_mask) {
		unsigned last = index - 1;

               color_args[last].valid_mask = 1; /* whether the EXEC mask is valid */
               color_args[last].done = 1; /* DONE bit */
	}

	/* Export PS outputs. */
	for (unsigned i = 0; i < index; i++)
		ac_build_export(&ctx->ac, &color_args[i]);

	if (depth || stencil || samplemask)
		radv_export_mrt_z(ctx, depth, stencil, samplemask);
	else if (!index)
		ac_build_export_null(&ctx->ac);
}

static void
emit_gs_epilogue(struct radv_shader_context *ctx)
{
	ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_NOP | AC_SENDMSG_GS_DONE, ctx->gs_wave_id);
}

static void
handle_shader_outputs_post(struct ac_shader_abi *abi, unsigned max_outputs,
			   LLVMValueRef *addrs)
{
	struct radv_shader_context *ctx = radv_shader_context_from_abi(abi);

	switch (ctx->stage) {
	case MESA_SHADER_VERTEX:
		if (ctx->options->key.vs.as_ls)
			handle_ls_outputs_post(ctx);
		else if (ctx->options->key.vs.as_es)
			handle_es_outputs_post(ctx, &ctx->shader_info->vs.es_info);
		else
			handle_vs_outputs_post(ctx, ctx->options->key.vs.export_prim_id,
					       ctx->options->key.vs.export_layer_id,
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
					       ctx->options->key.tes.export_layer_id,
					       &ctx->shader_info->tes.outinfo);
		break;
	default:
		break;
	}
}

static void ac_llvm_finalize_module(struct radv_shader_context *ctx)
{
	LLVMPassManagerRef passmgr;
	/* Create the pass manager */
	passmgr = LLVMCreateFunctionPassManagerForModule(
							ctx->ac.module);

	/* This pass should eliminate all the load and store instructions */
	LLVMAddPromoteMemoryToRegisterPass(passmgr);

	/* Add some optimization passes */
	LLVMAddScalarReplAggregatesPass(passmgr);
	LLVMAddLICMPass(passmgr);
	LLVMAddAggressiveDCEPass(passmgr);
	LLVMAddCFGSimplificationPass(passmgr);
	/* This is recommended by the instruction combining pass. */
	LLVMAddEarlyCSEMemSSAPass(passmgr);
	LLVMAddInstructionCombiningPass(passmgr);

	/* Run the pass */
	LLVMInitializeFunctionPassManager(passmgr);
	LLVMRunFunctionPassManager(passmgr, ctx->main_function);
	LLVMFinalizeFunctionPassManager(passmgr);

	LLVMDisposeBuilder(ctx->ac.builder);
	LLVMDisposePassManager(passmgr);

	ac_llvm_context_dispose(&ctx->ac);
}

static void
ac_nir_eliminate_const_vs_outputs(struct radv_shader_context *ctx)
{
	struct radv_vs_output_info *outinfo;

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
ac_setup_rings(struct radv_shader_context *ctx)
{
	if (ctx->options->chip_class <= VI &&
	    (ctx->stage == MESA_SHADER_GEOMETRY ||
	     ctx->options->key.vs.as_es || ctx->options->key.tes.as_es)) {
		unsigned ring = ctx->stage == MESA_SHADER_GEOMETRY ? RING_ESGS_GS
								   : RING_ESGS_VS;
		LLVMValueRef offset = LLVMConstInt(ctx->ac.i32, ring, false);

		ctx->esgs_ring = ac_build_load_to_sgpr(&ctx->ac,
						       ctx->ring_offsets,
						       offset);
	}

	if (ctx->is_gs_copy_shader) {
		ctx->gsvs_ring = ac_build_load_to_sgpr(&ctx->ac, ctx->ring_offsets, LLVMConstInt(ctx->ac.i32, RING_GSVS_VS, false));
	}
	if (ctx->stage == MESA_SHADER_GEOMETRY) {
		LLVMValueRef tmp;
		uint32_t num_entries = 64;
		LLVMValueRef gsvs_ring_stride = LLVMConstInt(ctx->ac.i32, ctx->max_gsvs_emit_size, false);
		LLVMValueRef gsvs_ring_desc = LLVMConstInt(ctx->ac.i32, ctx->max_gsvs_emit_size << 16, false);
		ctx->gsvs_ring = ac_build_load_to_sgpr(&ctx->ac, ctx->ring_offsets, LLVMConstInt(ctx->ac.i32, RING_GSVS_GS, false));

		ctx->gsvs_ring = LLVMBuildBitCast(ctx->ac.builder, ctx->gsvs_ring, ctx->ac.v4i32, "");

		tmp = LLVMConstInt(ctx->ac.i32, num_entries, false);
		if (ctx->options->chip_class >= VI)
			tmp = LLVMBuildMul(ctx->ac.builder, gsvs_ring_stride, tmp, "");
		ctx->gsvs_ring = LLVMBuildInsertElement(ctx->ac.builder, ctx->gsvs_ring, tmp, LLVMConstInt(ctx->ac.i32, 2, false), "");
		tmp = LLVMBuildExtractElement(ctx->ac.builder, ctx->gsvs_ring, ctx->ac.i32_1, "");
		tmp = LLVMBuildOr(ctx->ac.builder, tmp, gsvs_ring_desc, "");
		ctx->gsvs_ring = LLVMBuildInsertElement(ctx->ac.builder, ctx->gsvs_ring, tmp, ctx->ac.i32_1, "");
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
static void ac_nir_fixup_ls_hs_input_vgprs(struct radv_shader_context *ctx)
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

static void prepare_gs_input_vgprs(struct radv_shader_context *ctx)
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


static
LLVMModuleRef ac_translate_nir_to_llvm(LLVMTargetMachineRef tm,
                                       struct nir_shader *const *shaders,
                                       int shader_count,
                                       struct radv_shader_variant_info *shader_info,
                                       const struct radv_nir_compiler_options *options)
{
	struct radv_shader_context ctx = {0};
	unsigned i;
	ctx.options = options;
	ctx.shader_info = shader_info;
	ctx.context = LLVMContextCreate();

	ac_llvm_context_init(&ctx.ac, ctx.context, options->chip_class,
			     options->family);
	ctx.ac.module = LLVMModuleCreateWithNameInContext("shader", ctx.context);
	LLVMSetTarget(ctx.ac.module, options->supports_spill ? "amdgcn-mesa-mesa3d" : "amdgcn--");

	LLVMTargetDataRef data_layout = LLVMCreateTargetDataLayout(tm);
	char *data_layout_str = LLVMCopyStringRepOfTargetData(data_layout);
	LLVMSetDataLayout(ctx.ac.module, data_layout_str);
	LLVMDisposeTargetData(data_layout);
	LLVMDisposeMessage(data_layout_str);

	enum ac_float_mode float_mode =
		options->unsafe_math ? AC_FLOAT_MODE_UNSAFE_FP_MATH :
				       AC_FLOAT_MODE_DEFAULT;

	ctx.ac.builder = ac_create_builder(ctx.context, float_mode);

	memset(shader_info, 0, sizeof(*shader_info));

	for(int i = 0; i < shader_count; ++i)
		radv_nir_shader_info_pass(shaders[i], options, &shader_info->info);

	for (i = 0; i < RADV_UD_MAX_SETS; i++)
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
	ctx.abi.load_resource = radv_load_resource;
	ctx.abi.clamp_shadow_reference = false;
	ctx.abi.gfx9_stride_size_workaround = ctx.ac.chip_class == GFX9;

	if (shader_count >= 2)
		ac_init_exec_full_mask(&ctx.ac);

	if (ctx.ac.chip_class == GFX9 &&
	    shaders[shader_count - 1]->info.stage == MESA_SHADER_TESS_CTRL)
		ac_nir_fixup_ls_hs_input_vgprs(&ctx);

	for(int i = 0; i < shader_count; ++i) {
		ctx.stage = shaders[i]->info.stage;
		ctx.output_mask = 0;
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
			ctx.tcs_vertices_per_patch = shaders[i]->info.tess.tcs_vertices_out;
			if (shader_count == 1)
				ctx.tcs_num_inputs = ctx.options->key.tcs.num_inputs;
			else
				ctx.tcs_num_inputs = util_last_bit64(shader_info->info.vs.ls_outputs_written);
			ctx.tcs_num_patches = get_tcs_num_patches(&ctx);
		} else if (shaders[i]->info.stage == MESA_SHADER_TESS_EVAL) {
			ctx.tes_primitive_mode = shaders[i]->info.tess.primitive_mode;
			ctx.abi.load_tess_varyings = load_tes_input;
			ctx.abi.load_tess_coord = load_tess_coord;
			ctx.abi.load_patch_vertices_in = load_patch_vertices_in;
			ctx.tcs_vertices_per_patch = shaders[i]->info.tess.tcs_vertices_out;
			ctx.tcs_num_patches = ctx.options->key.tes.num_patches;
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
			ctx.abi.load_base_vertex = radv_load_base_vertex;
		} else if (shaders[i]->info.stage == MESA_SHADER_FRAGMENT) {
			shader_info->fs.can_discard = shaders[i]->info.fs.uses_discard;
			ctx.abi.lookup_interp_param = lookup_interp_param;
			ctx.abi.load_sample_position = load_sample_position;
			ctx.abi.load_sample_mask_in = load_sample_mask_in;
			ctx.abi.emit_kill = radv_emit_kill;
		}

		if (i)
			ac_emit_barrier(&ctx.ac, ctx.stage);

		nir_foreach_variable(variable, &shaders[i]->outputs)
			scan_shader_output_decl(&ctx, variable, shaders[i], shaders[i]->info.stage);

		if (shaders[i]->info.stage == MESA_SHADER_GEOMETRY) {
			unsigned addclip = shaders[i]->info.clip_distance_array_size +
					shaders[i]->info.cull_distance_array_size > 4;
			ctx.gsvs_vertex_size = (util_bitcount64(ctx.output_mask) + addclip) * 16;
			ctx.max_gsvs_emit_size = ctx.gsvs_vertex_size *
				shaders[i]->info.gs.vertices_out;
		}

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

		ac_nir_translate(&ctx.ac, &ctx.abi, shaders[i]);

		if (shader_count >= 2) {
			LLVMBuildBr(ctx.ac.builder, merge_block);
			LLVMPositionBuilderAtEnd(ctx.ac.builder, merge_block);
		}

		if (shaders[i]->info.stage == MESA_SHADER_GEOMETRY) {
			shader_info->gs.gsvs_vertex_size = ctx.gsvs_vertex_size;
			shader_info->gs.max_gsvs_emit_size = ctx.max_gsvs_emit_size;
		} else if (shaders[i]->info.stage == MESA_SHADER_TESS_CTRL) {
			shader_info->tcs.num_patches = ctx.tcs_num_patches;
			shader_info->tcs.lds_size = calculate_tess_lds_size(&ctx);
		}
	}

	LLVMBuildRetVoid(ctx.ac.builder);

	if (options->dump_preoptir)
		ac_dump_module(ctx.ac.module);

	ac_llvm_finalize_module(&ctx);

	if (shader_count == 1)
		ac_nir_eliminate_const_vs_outputs(&ctx);

	if (options->dump_shader) {
		ctx.shader_info->private_mem_vgprs =
			ac_count_scratch_private_memory(ctx.main_function);
	}

	return ctx.ac.module;
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
				   struct radv_shader_variant_info *shader_info,
				   gl_shader_stage stage,
				   const struct radv_nir_compiler_options *options)
{
	if (options->dump_shader)
		ac_dump_module(llvm_module);

	memset(binary, 0, sizeof(*binary));

	if (options->record_llvm_ir) {
		char *llvm_ir = LLVMPrintModuleToString(llvm_module);
		binary->llvm_ir_string = strdup(llvm_ir);
		LLVMDisposeMessage(llvm_ir);
	}

	int v = ac_llvm_compile(llvm_module, binary, tm);
	if (v) {
		fprintf(stderr, "compile failed\n");
	}

	if (options->dump_shader)
		fprintf(stderr, "disasm:\n%s\n", binary->disasm_string);

	ac_shader_binary_read_config(binary, config, 0, options->supports_spill);

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
ac_fill_shader_info(struct radv_shader_variant_info *shader_info, struct nir_shader *nir, const struct radv_nir_compiler_options *options)
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

void
radv_compile_nir_shader(LLVMTargetMachineRef tm,
			struct ac_shader_binary *binary,
			struct ac_shader_config *config,
			struct radv_shader_variant_info *shader_info,
			struct nir_shader *const *nir,
			int nir_count,
			const struct radv_nir_compiler_options *options)
{

	LLVMModuleRef llvm_module;

	llvm_module = ac_translate_nir_to_llvm(tm, nir, nir_count, shader_info,
	                                       options);

	ac_compile_llvm_module(tm, llvm_module, binary, config, shader_info,
			       nir[0]->info.stage, options);

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
ac_gs_copy_shader_emit(struct radv_shader_context *ctx)
{
	LLVMValueRef vtx_offset =
		LLVMBuildMul(ctx->ac.builder, ctx->abi.vertex_id,
			     LLVMConstInt(ctx->ac.i32, 4, false), "");
	int idx = 0;

	for (unsigned i = 0; i < AC_LLVM_MAX_OUTPUTS; ++i) {
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

			LLVMBuildStore(ctx->ac.builder,
				       ac_to_float(&ctx->ac, value), ctx->abi.outputs[ac_llvm_reg_index_soa(i, j)]);
		}
		idx += slot_inc;
	}
	handle_vs_outputs_post(ctx, false, false, &ctx->shader_info->vs.outinfo);
}

void
radv_compile_gs_copy_shader(LLVMTargetMachineRef tm,
			    struct nir_shader *geom_shader,
			    struct ac_shader_binary *binary,
			    struct ac_shader_config *config,
			    struct radv_shader_variant_info *shader_info,
			    const struct radv_nir_compiler_options *options)
{
	struct radv_shader_context ctx = {0};
	ctx.context = LLVMContextCreate();
	ctx.options = options;
	ctx.shader_info = shader_info;

	ac_llvm_context_init(&ctx.ac, ctx.context, options->chip_class,
			     options->family);
	ctx.ac.module = LLVMModuleCreateWithNameInContext("shader", ctx.context);

	ctx.is_gs_copy_shader = true;
	LLVMSetTarget(ctx.ac.module, "amdgcn--");

	enum ac_float_mode float_mode =
		options->unsafe_math ? AC_FLOAT_MODE_UNSAFE_FP_MATH :
				       AC_FLOAT_MODE_DEFAULT;

	ctx.ac.builder = ac_create_builder(ctx.context, float_mode);
	ctx.stage = MESA_SHADER_VERTEX;

	radv_nir_shader_info_pass(geom_shader, options, &shader_info->info);

	create_function(&ctx, MESA_SHADER_VERTEX, false, MESA_SHADER_VERTEX);

	ctx.gs_max_out_vertices = geom_shader->info.gs.vertices_out;
	ac_setup_rings(&ctx);

	ctx.num_output_clips = geom_shader->info.clip_distance_array_size;
	ctx.num_output_culls = geom_shader->info.cull_distance_array_size;

	nir_foreach_variable(variable, &geom_shader->outputs) {
		scan_shader_output_decl(&ctx, variable, geom_shader, MESA_SHADER_VERTEX);
		ac_handle_shader_output_decl(&ctx.ac, &ctx.abi, geom_shader,
					     variable, MESA_SHADER_VERTEX);
	}

	ac_gs_copy_shader_emit(&ctx);

	LLVMBuildRetVoid(ctx.ac.builder);

	ac_llvm_finalize_module(&ctx);

	ac_compile_llvm_module(tm, ctx.ac.module, binary, config, shader_info,
			       MESA_SHADER_VERTEX, options);
}
