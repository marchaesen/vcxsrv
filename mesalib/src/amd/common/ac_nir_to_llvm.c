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

enum radeon_llvm_calling_convention {
	RADEON_LLVM_AMDGPU_VS = 87,
	RADEON_LLVM_AMDGPU_GS = 88,
	RADEON_LLVM_AMDGPU_PS = 89,
	RADEON_LLVM_AMDGPU_CS = 90,
};

#define CONST_ADDR_SPACE 2
#define LOCAL_ADDR_SPACE 3

#define RADEON_LLVM_MAX_INPUTS (VARYING_SLOT_VAR31 + 1)
#define RADEON_LLVM_MAX_OUTPUTS (VARYING_SLOT_VAR31 + 1)

enum desc_type {
	DESC_IMAGE,
	DESC_FMASK,
	DESC_SAMPLER,
	DESC_BUFFER,
};

struct nir_to_llvm_context {
	struct ac_llvm_context ac;
	const struct ac_nir_compiler_options *options;
	struct ac_shader_variant_info *shader_info;

	LLVMContextRef context;
	LLVMModuleRef module;
	LLVMBuilderRef builder;
	LLVMValueRef main_function;

	struct hash_table *defs;
	struct hash_table *phis;

	LLVMValueRef descriptor_sets[AC_UD_MAX_SETS];
	LLVMValueRef ring_offsets;
	LLVMValueRef push_constants;
	LLVMValueRef num_work_groups;
	LLVMValueRef workgroup_ids;
	LLVMValueRef local_invocation_ids;
	LLVMValueRef tg_size;

	LLVMValueRef vertex_buffers;
	LLVMValueRef base_vertex;
	LLVMValueRef start_instance;
	LLVMValueRef draw_index;
	LLVMValueRef vertex_id;
	LLVMValueRef rel_auto_id;
	LLVMValueRef vs_prim_id;
	LLVMValueRef instance_id;

	LLVMValueRef es2gs_offset;

	LLVMValueRef gsvs_ring_stride;
	LLVMValueRef gsvs_num_entries;
	LLVMValueRef gs2vs_offset;
	LLVMValueRef gs_wave_id;
	LLVMValueRef gs_vtx_offset[6];
	LLVMValueRef gs_prim_id, gs_invocation_id;

	LLVMValueRef esgs_ring;
	LLVMValueRef gsvs_ring;

	LLVMValueRef prim_mask;
	LLVMValueRef sample_positions;
	LLVMValueRef persp_sample, persp_center, persp_centroid;
	LLVMValueRef linear_sample, linear_center, linear_centroid;
	LLVMValueRef front_face;
	LLVMValueRef ancillary;
	LLVMValueRef sample_coverage;
	LLVMValueRef frag_pos[4];

	LLVMBasicBlockRef continue_block;
	LLVMBasicBlockRef break_block;

	LLVMTypeRef i1;
	LLVMTypeRef i8;
	LLVMTypeRef i16;
	LLVMTypeRef i32;
	LLVMTypeRef i64;
	LLVMTypeRef v2i32;
	LLVMTypeRef v3i32;
	LLVMTypeRef v4i32;
	LLVMTypeRef v8i32;
	LLVMTypeRef f64;
	LLVMTypeRef f32;
	LLVMTypeRef f16;
	LLVMTypeRef v2f32;
	LLVMTypeRef v4f32;
	LLVMTypeRef v16i8;
	LLVMTypeRef voidt;

	LLVMValueRef i32zero;
	LLVMValueRef i32one;
	LLVMValueRef f32zero;
	LLVMValueRef f32one;
	LLVMValueRef v4f32empty;

	unsigned uniform_md_kind;
	LLVMValueRef empty_md;
	gl_shader_stage stage;

	LLVMValueRef lds;
	LLVMValueRef inputs[RADEON_LLVM_MAX_INPUTS * 4];
	LLVMValueRef outputs[RADEON_LLVM_MAX_OUTPUTS * 4];

	LLVMValueRef shared_memory;
	uint64_t input_mask;
	uint64_t output_mask;
	int num_locals;
	LLVMValueRef *locals;
	bool has_ddxy;
	uint8_t num_input_clips;
	uint8_t num_input_culls;
	uint8_t num_output_clips;
	uint8_t num_output_culls;

	bool has_ds_bpermute;

	bool is_gs_copy_shader;
	LLVMValueRef gs_next_vertex;
	unsigned gs_max_out_vertices;
};

struct ac_tex_info {
	LLVMValueRef args[12];
	int arg_count;
	LLVMTypeRef dst_type;
	bool has_offset;
};

static LLVMValueRef get_sampler_desc(struct nir_to_llvm_context *ctx,
				     nir_deref_var *deref,
				     enum desc_type desc_type);
static unsigned radeon_llvm_reg_index_soa(unsigned index, unsigned chan)
{
	return (index * 4) + chan;
}

static unsigned shader_io_get_unique_index(gl_varying_slot slot)
{
	if (slot == VARYING_SLOT_POS)
		return 0;
	if (slot == VARYING_SLOT_PSIZ)
		return 1;
	if (slot == VARYING_SLOT_CLIP_DIST0 ||
	    slot == VARYING_SLOT_CULL_DIST0)
		return 2;
	if (slot == VARYING_SLOT_CLIP_DIST1 ||
	    slot == VARYING_SLOT_CULL_DIST1)
		return 3;
	if (slot >= VARYING_SLOT_VAR0 && slot <= VARYING_SLOT_VAR31)
		return 4 + (slot - VARYING_SLOT_VAR0);
	unreachable("illegal slot in get unique index\n");
}

static unsigned llvm_get_type_size(LLVMTypeRef type)
{
	LLVMTypeKind kind = LLVMGetTypeKind(type);

	switch (kind) {
	case LLVMIntegerTypeKind:
		return LLVMGetIntTypeWidth(type) / 8;
	case LLVMFloatTypeKind:
		return 4;
	case LLVMPointerTypeKind:
		return 8;
	case LLVMVectorTypeKind:
		return LLVMGetVectorSize(type) *
		       llvm_get_type_size(LLVMGetElementType(type));
	default:
		assert(0);
		return 0;
	}
}

static void set_llvm_calling_convention(LLVMValueRef func,
                                        gl_shader_stage stage)
{
	enum radeon_llvm_calling_convention calling_conv;

	switch (stage) {
	case MESA_SHADER_VERTEX:
	case MESA_SHADER_TESS_CTRL:
	case MESA_SHADER_TESS_EVAL:
		calling_conv = RADEON_LLVM_AMDGPU_VS;
		break;
	case MESA_SHADER_GEOMETRY:
		calling_conv = RADEON_LLVM_AMDGPU_GS;
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

static LLVMValueRef
create_llvm_function(LLVMContextRef ctx, LLVMModuleRef module,
                     LLVMBuilderRef builder, LLVMTypeRef *return_types,
                     unsigned num_return_elems, LLVMTypeRef *param_types,
                     unsigned param_count, unsigned array_params_mask,
                     unsigned sgpr_params, bool unsafe_math)
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
	    LLVMFunctionType(ret_type, param_types, param_count, 0);
	LLVMValueRef main_function =
	    LLVMAddFunction(module, "main", main_function_type);
	main_function_body =
	    LLVMAppendBasicBlockInContext(ctx, main_function, "main_body");
	LLVMPositionBuilderAtEnd(builder, main_function_body);

	LLVMSetFunctionCallConv(main_function, RADEON_LLVM_AMDGPU_CS);
	for (unsigned i = 0; i < sgpr_params; ++i) {
		if (array_params_mask & (1 << i)) {
			LLVMValueRef P = LLVMGetParam(main_function, i);
			ac_add_function_attr(ctx, main_function, i + 1, AC_FUNC_ATTR_BYVAL);
			ac_add_attr_dereferenceable(P, UINT64_MAX);
		}
		else {
			ac_add_function_attr(ctx, main_function, i + 1, AC_FUNC_ATTR_INREG);
		}
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
	}
	return main_function;
}

static LLVMTypeRef const_array(LLVMTypeRef elem_type, int num_elements)
{
	return LLVMPointerType(LLVMArrayType(elem_type, num_elements),
	                       CONST_ADDR_SPACE);
}

static LLVMValueRef get_shared_memory_ptr(struct nir_to_llvm_context *ctx,
					  int idx,
					  LLVMTypeRef type)
{
	LLVMValueRef offset;
	LLVMValueRef ptr;
	int addr_space;

	offset = LLVMConstInt(ctx->i32, idx, false);

	ptr = ctx->shared_memory;
	ptr = LLVMBuildGEP(ctx->builder, ptr, &offset, 1, "");
	addr_space = LLVMGetPointerAddressSpace(LLVMTypeOf(ptr));
	ptr = LLVMBuildBitCast(ctx->builder, ptr, LLVMPointerType(type, addr_space), "");
	return ptr;
}

static LLVMTypeRef to_integer_type_scalar(struct nir_to_llvm_context *ctx, LLVMTypeRef t)
{
	if (t == ctx->f16 || t == ctx->i16)
		return ctx->i16;
	else if (t == ctx->f32 || t == ctx->i32)
		return ctx->i32;
	else if (t == ctx->f64 || t == ctx->i64)
		return ctx->i64;
	else
		unreachable("Unhandled integer size");
}

static LLVMTypeRef to_integer_type(struct nir_to_llvm_context *ctx, LLVMTypeRef t)
{
	if (LLVMGetTypeKind(t) == LLVMVectorTypeKind) {
		LLVMTypeRef elem_type = LLVMGetElementType(t);
		return LLVMVectorType(to_integer_type_scalar(ctx, elem_type),
		                      LLVMGetVectorSize(t));
	}
	return to_integer_type_scalar(ctx, t);
}

static LLVMValueRef to_integer(struct nir_to_llvm_context *ctx, LLVMValueRef v)
{
	LLVMTypeRef type = LLVMTypeOf(v);
	return LLVMBuildBitCast(ctx->builder, v, to_integer_type(ctx, type), "");
}

static LLVMTypeRef to_float_type_scalar(struct nir_to_llvm_context *ctx, LLVMTypeRef t)
{
	if (t == ctx->i16 || t == ctx->f16)
		return ctx->f16;
	else if (t == ctx->i32 || t == ctx->f32)
		return ctx->f32;
	else if (t == ctx->i64 || t == ctx->f64)
		return ctx->f64;
	else
		unreachable("Unhandled float size");
}

static LLVMTypeRef to_float_type(struct nir_to_llvm_context *ctx, LLVMTypeRef t)
{
	if (LLVMGetTypeKind(t) == LLVMVectorTypeKind) {
		LLVMTypeRef elem_type = LLVMGetElementType(t);
		return LLVMVectorType(to_float_type_scalar(ctx, elem_type),
		                      LLVMGetVectorSize(t));
	}
	return to_float_type_scalar(ctx, t);
}

static LLVMValueRef to_float(struct nir_to_llvm_context *ctx, LLVMValueRef v)
{
	LLVMTypeRef type = LLVMTypeOf(v);
	return LLVMBuildBitCast(ctx->builder, v, to_float_type(ctx, type), "");
}

static int get_elem_bits(struct nir_to_llvm_context *ctx, LLVMTypeRef type)
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

static LLVMValueRef unpack_param(struct nir_to_llvm_context *ctx,
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

static void set_userdata_location(struct ac_userdata_info *ud_info, uint8_t sgpr_idx, uint8_t num_sgprs)
{
	ud_info->sgpr_idx = sgpr_idx;
	ud_info->num_sgprs = num_sgprs;
	ud_info->indirect = false;
	ud_info->indirect_offset = 0;
}

static void set_userdata_location_shader(struct nir_to_llvm_context *ctx,
					 int idx, uint8_t sgpr_idx, uint8_t num_sgprs)
{
	set_userdata_location(&ctx->shader_info->user_sgprs_locs.shader_data[idx], sgpr_idx, num_sgprs);
}

#if 0
static void set_userdata_location_indirect(struct ac_userdata_info *ud_info, uint8_t sgpr_idx, uint8_t num_sgprs,
					   uint32_t indirect_offset)
{
	ud_info->sgpr_idx = sgpr_idx;
	ud_info->num_sgprs = num_sgprs;
	ud_info->indirect = true;
	ud_info->indirect_offset = indirect_offset;
}
#endif

static void create_function(struct nir_to_llvm_context *ctx)
{
	LLVMTypeRef arg_types[23];
	unsigned arg_idx = 0;
	unsigned array_params_mask = 0;
	unsigned sgpr_count = 0, user_sgpr_count;
	unsigned i;
	unsigned num_sets = ctx->options->layout ? ctx->options->layout->num_sets : 0;
	unsigned user_sgpr_idx;
	bool need_push_constants;
	bool need_ring_offsets = false;

	/* until we sort out scratch/global buffers always assign ring offsets for gs/vs/es */
	if (ctx->stage == MESA_SHADER_GEOMETRY ||
	    ctx->stage == MESA_SHADER_VERTEX ||
	    ctx->is_gs_copy_shader)
		need_ring_offsets = true;

	need_push_constants = true;
	if (!ctx->options->layout)
		need_push_constants = false;
	else if (!ctx->options->layout->push_constant_size &&
		 !ctx->options->layout->dynamic_offset_count)
		need_push_constants = false;

	if (need_ring_offsets && !ctx->options->supports_spill) {
		arg_types[arg_idx++] = const_array(ctx->v16i8, 8); /* address of rings */
	}

	/* 1 for each descriptor set */
	for (unsigned i = 0; i < num_sets; ++i) {
		if (ctx->options->layout->set[i].layout->shader_stages & (1 << ctx->stage)) {
			array_params_mask |= (1 << arg_idx);
			arg_types[arg_idx++] = const_array(ctx->i8, 1024 * 1024);
		}
	}

	if (need_push_constants) {
		/* 1 for push constants and dynamic descriptors */
		array_params_mask |= (1 << arg_idx);
		arg_types[arg_idx++] = const_array(ctx->i8, 1024 * 1024);
	}

	switch (ctx->stage) {
	case MESA_SHADER_COMPUTE:
		arg_types[arg_idx++] = LLVMVectorType(ctx->i32, 3); /* grid size */
		user_sgpr_count = arg_idx;
		arg_types[arg_idx++] = LLVMVectorType(ctx->i32, 3);
		arg_types[arg_idx++] = ctx->i32;
		sgpr_count = arg_idx;

		arg_types[arg_idx++] = LLVMVectorType(ctx->i32, 3);
		break;
	case MESA_SHADER_VERTEX:
		if (!ctx->is_gs_copy_shader) {
			arg_types[arg_idx++] = const_array(ctx->v16i8, 16); /* vertex buffers */
			arg_types[arg_idx++] = ctx->i32; // base vertex
			arg_types[arg_idx++] = ctx->i32; // start instance
			arg_types[arg_idx++] = ctx->i32; // draw index
		}
		user_sgpr_count = arg_idx;
		if (ctx->options->key.vs.as_es)
			arg_types[arg_idx++] = ctx->i32; //es2gs offset
		sgpr_count = arg_idx;
		arg_types[arg_idx++] = ctx->i32; // vertex id
		if (!ctx->is_gs_copy_shader) {
			arg_types[arg_idx++] = ctx->i32; // rel auto id
			arg_types[arg_idx++] = ctx->i32; // vs prim id
			arg_types[arg_idx++] = ctx->i32; // instance id
		}
		break;
	case MESA_SHADER_GEOMETRY:
		arg_types[arg_idx++] = ctx->i32; // gsvs stride
		arg_types[arg_idx++] = ctx->i32; // gsvs num entires
		user_sgpr_count = arg_idx;
		arg_types[arg_idx++] = ctx->i32; // gs2vs offset
	        arg_types[arg_idx++] = ctx->i32; // wave id
		sgpr_count = arg_idx;
		arg_types[arg_idx++] = ctx->i32; // vtx0
		arg_types[arg_idx++] = ctx->i32; // vtx1
		arg_types[arg_idx++] = ctx->i32; // prim id
		arg_types[arg_idx++] = ctx->i32; // vtx2
		arg_types[arg_idx++] = ctx->i32; // vtx3
		arg_types[arg_idx++] = ctx->i32; // vtx4
		arg_types[arg_idx++] = ctx->i32; // vtx5
		arg_types[arg_idx++] = ctx->i32; // GS instance id
		break;
	case MESA_SHADER_FRAGMENT:
		arg_types[arg_idx++] = const_array(ctx->f32, 32); /* sample positions */
		user_sgpr_count = arg_idx;
		arg_types[arg_idx++] = ctx->i32; /* prim mask */
		sgpr_count = arg_idx;
		arg_types[arg_idx++] = ctx->v2i32; /* persp sample */
		arg_types[arg_idx++] = ctx->v2i32; /* persp center */
		arg_types[arg_idx++] = ctx->v2i32; /* persp centroid */
		arg_types[arg_idx++] = ctx->v3i32; /* persp pull model */
		arg_types[arg_idx++] = ctx->v2i32; /* linear sample */
		arg_types[arg_idx++] = ctx->v2i32; /* linear center */
		arg_types[arg_idx++] = ctx->v2i32; /* linear centroid */
		arg_types[arg_idx++] = ctx->f32;  /* line stipple tex */
		arg_types[arg_idx++] = ctx->f32;  /* pos x float */
		arg_types[arg_idx++] = ctx->f32;  /* pos y float */
		arg_types[arg_idx++] = ctx->f32;  /* pos z float */
		arg_types[arg_idx++] = ctx->f32;  /* pos w float */
		arg_types[arg_idx++] = ctx->i32;  /* front face */
		arg_types[arg_idx++] = ctx->i32;  /* ancillary */
		arg_types[arg_idx++] = ctx->i32;  /* sample coverage */
		arg_types[arg_idx++] = ctx->i32;  /* fixed pt */
		break;
	default:
		unreachable("Shader stage not implemented");
	}

	ctx->main_function = create_llvm_function(
	    ctx->context, ctx->module, ctx->builder, NULL, 0, arg_types,
	    arg_idx, array_params_mask, sgpr_count, ctx->options->unsafe_math);
	set_llvm_calling_convention(ctx->main_function, ctx->stage);

	ctx->shader_info->num_input_sgprs = 0;
	ctx->shader_info->num_input_vgprs = 0;

	ctx->shader_info->num_user_sgprs = ctx->options->supports_spill ? 2 : 0;
	for (i = 0; i < user_sgpr_count; i++)
		ctx->shader_info->num_user_sgprs += llvm_get_type_size(arg_types[i]) / 4;

	ctx->shader_info->num_input_sgprs = ctx->shader_info->num_user_sgprs;
	for (; i < sgpr_count; i++)
		ctx->shader_info->num_input_sgprs += llvm_get_type_size(arg_types[i]) / 4;

	if (ctx->stage != MESA_SHADER_FRAGMENT)
		for (; i < arg_idx; ++i)
			ctx->shader_info->num_input_vgprs += llvm_get_type_size(arg_types[i]) / 4;

	arg_idx = 0;
	user_sgpr_idx = 0;

	if (ctx->options->supports_spill || need_ring_offsets) {
		set_userdata_location_shader(ctx, AC_UD_SCRATCH_RING_OFFSETS, user_sgpr_idx, 2);
		user_sgpr_idx += 2;
		if (ctx->options->supports_spill) {
			ctx->ring_offsets = ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.implicit.buffer.ptr",
							       LLVMPointerType(ctx->i8, CONST_ADDR_SPACE),
							       NULL, 0, AC_FUNC_ATTR_READNONE);
			ctx->ring_offsets = LLVMBuildBitCast(ctx->builder, ctx->ring_offsets,
							     const_array(ctx->v16i8, 8), "");
		} else
			ctx->ring_offsets = LLVMGetParam(ctx->main_function, arg_idx++);
	}

	for (unsigned i = 0; i < num_sets; ++i) {
		if (ctx->options->layout->set[i].layout->shader_stages & (1 << ctx->stage)) {
			set_userdata_location(&ctx->shader_info->user_sgprs_locs.descriptor_sets[i], user_sgpr_idx, 2);
			user_sgpr_idx += 2;
			ctx->descriptor_sets[i] =
				LLVMGetParam(ctx->main_function, arg_idx++);
		} else
			ctx->descriptor_sets[i] = NULL;
	}

	if (need_push_constants) {
		ctx->push_constants = LLVMGetParam(ctx->main_function, arg_idx++);
		set_userdata_location_shader(ctx, AC_UD_PUSH_CONSTANTS, user_sgpr_idx, 2);
		user_sgpr_idx += 2;
	}

	switch (ctx->stage) {
	case MESA_SHADER_COMPUTE:
		set_userdata_location_shader(ctx, AC_UD_CS_GRID_SIZE, user_sgpr_idx, 3);
		user_sgpr_idx += 3;
		ctx->num_work_groups =
		    LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->workgroup_ids =
		    LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->tg_size =
		    LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->local_invocation_ids =
		    LLVMGetParam(ctx->main_function, arg_idx++);
		break;
	case MESA_SHADER_VERTEX:
		if (!ctx->is_gs_copy_shader) {
			set_userdata_location_shader(ctx, AC_UD_VS_VERTEX_BUFFERS, user_sgpr_idx, 2);
			user_sgpr_idx += 2;
			ctx->vertex_buffers = LLVMGetParam(ctx->main_function, arg_idx++);
			set_userdata_location_shader(ctx, AC_UD_VS_BASE_VERTEX_START_INSTANCE, user_sgpr_idx, 3);
			user_sgpr_idx += 3;
			ctx->base_vertex = LLVMGetParam(ctx->main_function, arg_idx++);
			ctx->start_instance = LLVMGetParam(ctx->main_function, arg_idx++);
			ctx->draw_index = LLVMGetParam(ctx->main_function, arg_idx++);
		}
		if (ctx->options->key.vs.as_es)
			ctx->es2gs_offset = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->vertex_id = LLVMGetParam(ctx->main_function, arg_idx++);
		if (!ctx->is_gs_copy_shader) {
			ctx->rel_auto_id = LLVMGetParam(ctx->main_function, arg_idx++);
			ctx->vs_prim_id = LLVMGetParam(ctx->main_function, arg_idx++);
			ctx->instance_id = LLVMGetParam(ctx->main_function, arg_idx++);
		}
		break;
	case MESA_SHADER_GEOMETRY:
		set_userdata_location_shader(ctx, AC_UD_GS_VS_RING_STRIDE_ENTRIES, user_sgpr_idx, 2);
		user_sgpr_idx += 2;
		ctx->gsvs_ring_stride = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->gsvs_num_entries = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->gs2vs_offset = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->gs_wave_id = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->gs_vtx_offset[0] = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->gs_vtx_offset[1] = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->gs_prim_id = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->gs_vtx_offset[2] = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->gs_vtx_offset[3] = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->gs_vtx_offset[4] = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->gs_vtx_offset[5] = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->gs_invocation_id = LLVMGetParam(ctx->main_function, arg_idx++);
		break;
	case MESA_SHADER_FRAGMENT:
		set_userdata_location_shader(ctx, AC_UD_PS_SAMPLE_POS, user_sgpr_idx, 2);
		user_sgpr_idx += 2;
		ctx->sample_positions = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->prim_mask = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->persp_sample = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->persp_center = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->persp_centroid = LLVMGetParam(ctx->main_function, arg_idx++);
		arg_idx++;
		ctx->linear_sample = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->linear_center = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->linear_centroid = LLVMGetParam(ctx->main_function, arg_idx++);
		arg_idx++; /* line stipple */
		ctx->frag_pos[0] = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->frag_pos[1] = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->frag_pos[2] = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->frag_pos[3] = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->front_face = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->ancillary = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->sample_coverage = LLVMGetParam(ctx->main_function, arg_idx++);
		break;
	default:
		unreachable("Shader stage not implemented");
	}
}

static void setup_types(struct nir_to_llvm_context *ctx)
{
	LLVMValueRef args[4];

	ctx->voidt = LLVMVoidTypeInContext(ctx->context);
	ctx->i1 = LLVMIntTypeInContext(ctx->context, 1);
	ctx->i8 = LLVMIntTypeInContext(ctx->context, 8);
	ctx->i16 = LLVMIntTypeInContext(ctx->context, 16);
	ctx->i32 = LLVMIntTypeInContext(ctx->context, 32);
	ctx->i64 = LLVMIntTypeInContext(ctx->context, 64);
	ctx->v2i32 = LLVMVectorType(ctx->i32, 2);
	ctx->v3i32 = LLVMVectorType(ctx->i32, 3);
	ctx->v4i32 = LLVMVectorType(ctx->i32, 4);
	ctx->v8i32 = LLVMVectorType(ctx->i32, 8);
	ctx->f32 = LLVMFloatTypeInContext(ctx->context);
	ctx->f16 = LLVMHalfTypeInContext(ctx->context);
	ctx->f64 = LLVMDoubleTypeInContext(ctx->context);
	ctx->v2f32 = LLVMVectorType(ctx->f32, 2);
	ctx->v4f32 = LLVMVectorType(ctx->f32, 4);
	ctx->v16i8 = LLVMVectorType(ctx->i8, 16);

	ctx->i32zero = LLVMConstInt(ctx->i32, 0, false);
	ctx->i32one = LLVMConstInt(ctx->i32, 1, false);
	ctx->f32zero = LLVMConstReal(ctx->f32, 0.0);
	ctx->f32one = LLVMConstReal(ctx->f32, 1.0);

	args[0] = ctx->f32zero;
	args[1] = ctx->f32zero;
	args[2] = ctx->f32zero;
	args[3] = ctx->f32one;
	ctx->v4f32empty = LLVMConstVector(args, 4);

	ctx->uniform_md_kind =
	    LLVMGetMDKindIDInContext(ctx->context, "amdgpu.uniform", 14);
	ctx->empty_md = LLVMMDNodeInContext(ctx->context, NULL, 0);

	args[0] = LLVMConstReal(ctx->f32, 2.5);
}

static int get_llvm_num_components(LLVMValueRef value)
{
	LLVMTypeRef type = LLVMTypeOf(value);
	unsigned num_components = LLVMGetTypeKind(type) == LLVMVectorTypeKind
	                              ? LLVMGetVectorSize(type)
	                              : 1;
	return num_components;
}

static LLVMValueRef llvm_extract_elem(struct nir_to_llvm_context *ctx,
				      LLVMValueRef value,
				      int index)
{
	int count = get_llvm_num_components(value);

	assert(index < count);
	if (count == 1)
		return value;

	return LLVMBuildExtractElement(ctx->builder, value,
				       LLVMConstInt(ctx->i32, index, false), "");
}

static LLVMValueRef trim_vector(struct nir_to_llvm_context *ctx,
                                LLVMValueRef value, unsigned count)
{
	unsigned num_components = get_llvm_num_components(value);
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
build_store_values_extended(struct nir_to_llvm_context *ctx,
			     LLVMValueRef *values,
			     unsigned value_count,
			     unsigned value_stride,
			     LLVMValueRef vec)
{
	LLVMBuilderRef builder = ctx->builder;
	unsigned i;

	if (value_count == 1) {
		LLVMBuildStore(builder, vec, values[0]);
		return;
	}

	for (i = 0; i < value_count; i++) {
		LLVMValueRef ptr = values[i * value_stride];
		LLVMValueRef index = LLVMConstInt(ctx->i32, i, false);
		LLVMValueRef value = LLVMBuildExtractElement(builder, vec, index, "");
		LLVMBuildStore(builder, value, ptr);
	}
}

static LLVMTypeRef get_def_type(struct nir_to_llvm_context *ctx,
                                nir_ssa_def *def)
{
	LLVMTypeRef type = LLVMIntTypeInContext(ctx->context, def->bit_size);
	if (def->num_components > 1) {
		type = LLVMVectorType(type, def->num_components);
	}
	return type;
}

static LLVMValueRef get_src(struct nir_to_llvm_context *ctx, nir_src src)
{
	assert(src.is_ssa);
	struct hash_entry *entry = _mesa_hash_table_search(ctx->defs, src.ssa);
	return (LLVMValueRef)entry->data;
}


static LLVMBasicBlockRef get_block(struct nir_to_llvm_context *ctx,
                                   struct nir_block *b)
{
	struct hash_entry *entry = _mesa_hash_table_search(ctx->defs, b);
	return (LLVMBasicBlockRef)entry->data;
}

static LLVMValueRef get_alu_src(struct nir_to_llvm_context *ctx,
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
		    LLVMConstInt(ctx->i32, src.swizzle[0], false),
		    LLVMConstInt(ctx->i32, src.swizzle[1], false),
		    LLVMConstInt(ctx->i32, src.swizzle[2], false),
		    LLVMConstInt(ctx->i32, src.swizzle[3], false)};

		if (src_components > 1 && num_components == 1) {
			value = LLVMBuildExtractElement(ctx->builder, value,
			                                masks[0], "");
		} else if (src_components == 1 && num_components > 1) {
			LLVMValueRef values[] = {value, value, value, value};
			value = ac_build_gather_values(&ctx->ac, values, num_components);
		} else {
			LLVMValueRef swizzle = LLVMConstVector(masks, num_components);
			value = LLVMBuildShuffleVector(ctx->builder, value, value,
		                                       swizzle, "");
		}
	}
	assert(!src.negate);
	assert(!src.abs);
	return value;
}

static LLVMValueRef emit_int_cmp(struct nir_to_llvm_context *ctx,
                                 LLVMIntPredicate pred, LLVMValueRef src0,
                                 LLVMValueRef src1)
{
	LLVMValueRef result = LLVMBuildICmp(ctx->builder, pred, src0, src1, "");
	return LLVMBuildSelect(ctx->builder, result,
	                       LLVMConstInt(ctx->i32, 0xFFFFFFFF, false),
	                       LLVMConstInt(ctx->i32, 0, false), "");
}

static LLVMValueRef emit_float_cmp(struct nir_to_llvm_context *ctx,
                                   LLVMRealPredicate pred, LLVMValueRef src0,
                                   LLVMValueRef src1)
{
	LLVMValueRef result;
	src0 = to_float(ctx, src0);
	src1 = to_float(ctx, src1);
	result = LLVMBuildFCmp(ctx->builder, pred, src0, src1, "");
	return LLVMBuildSelect(ctx->builder, result,
	                       LLVMConstInt(ctx->i32, 0xFFFFFFFF, false),
	                       LLVMConstInt(ctx->i32, 0, false), "");
}

static LLVMValueRef emit_intrin_1f_param(struct nir_to_llvm_context *ctx,
					 const char *intrin,
					 LLVMTypeRef result_type,
					 LLVMValueRef src0)
{
	char name[64];
	LLVMValueRef params[] = {
		to_float(ctx, src0),
	};

	sprintf(name, "%s.f%d", intrin, get_elem_bits(ctx, result_type));
	return ac_build_intrinsic(&ctx->ac, name, result_type, params, 1, AC_FUNC_ATTR_READNONE);
}

static LLVMValueRef emit_intrin_2f_param(struct nir_to_llvm_context *ctx,
				       const char *intrin,
				       LLVMTypeRef result_type,
				       LLVMValueRef src0, LLVMValueRef src1)
{
	char name[64];
	LLVMValueRef params[] = {
		to_float(ctx, src0),
		to_float(ctx, src1),
	};

	sprintf(name, "%s.f%d", intrin, get_elem_bits(ctx, result_type));
	return ac_build_intrinsic(&ctx->ac, name, result_type, params, 2, AC_FUNC_ATTR_READNONE);
}

static LLVMValueRef emit_intrin_3f_param(struct nir_to_llvm_context *ctx,
					 const char *intrin,
					 LLVMTypeRef result_type,
					 LLVMValueRef src0, LLVMValueRef src1, LLVMValueRef src2)
{
	char name[64];
	LLVMValueRef params[] = {
		to_float(ctx, src0),
		to_float(ctx, src1),
		to_float(ctx, src2),
	};

	sprintf(name, "%s.f%d", intrin, get_elem_bits(ctx, result_type));
	return ac_build_intrinsic(&ctx->ac, name, result_type, params, 3, AC_FUNC_ATTR_READNONE);
}

static LLVMValueRef emit_bcsel(struct nir_to_llvm_context *ctx,
			       LLVMValueRef src0, LLVMValueRef src1, LLVMValueRef src2)
{
	LLVMValueRef v = LLVMBuildICmp(ctx->builder, LLVMIntNE, src0,
				       ctx->i32zero, "");
	return LLVMBuildSelect(ctx->builder, v, src1, src2, "");
}

static LLVMValueRef emit_find_lsb(struct nir_to_llvm_context *ctx,
				  LLVMValueRef src0)
{
	LLVMValueRef params[2] = {
		src0,

		/* The value of 1 means that ffs(x=0) = undef, so LLVM won't
		 * add special code to check for x=0. The reason is that
		 * the LLVM behavior for x=0 is different from what we
		 * need here.
		 *
		 * The hardware already implements the correct behavior.
		 */
		LLVMConstInt(ctx->i32, 1, false),
	};
	return ac_build_intrinsic(&ctx->ac, "llvm.cttz.i32", ctx->i32, params, 2, AC_FUNC_ATTR_READNONE);
}

static LLVMValueRef emit_ifind_msb(struct nir_to_llvm_context *ctx,
				   LLVMValueRef src0)
{
	return ac_build_imsb(&ctx->ac, src0, ctx->i32);
}

static LLVMValueRef emit_ufind_msb(struct nir_to_llvm_context *ctx,
				   LLVMValueRef src0)
{
	return ac_build_umsb(&ctx->ac, src0, ctx->i32);
}

static LLVMValueRef emit_minmax_int(struct nir_to_llvm_context *ctx,
				    LLVMIntPredicate pred,
				    LLVMValueRef src0, LLVMValueRef src1)
{
	return LLVMBuildSelect(ctx->builder,
			       LLVMBuildICmp(ctx->builder, pred, src0, src1, ""),
			       src0,
			       src1, "");

}
static LLVMValueRef emit_iabs(struct nir_to_llvm_context *ctx,
			      LLVMValueRef src0)
{
	return emit_minmax_int(ctx, LLVMIntSGT, src0,
			       LLVMBuildNeg(ctx->builder, src0, ""));
}

static LLVMValueRef emit_fsign(struct nir_to_llvm_context *ctx,
			       LLVMValueRef src0)
{
	LLVMValueRef cmp, val;

	cmp = LLVMBuildFCmp(ctx->builder, LLVMRealOGT, src0, ctx->f32zero, "");
	val = LLVMBuildSelect(ctx->builder, cmp, ctx->f32one, src0, "");
	cmp = LLVMBuildFCmp(ctx->builder, LLVMRealOGE, val, ctx->f32zero, "");
	val = LLVMBuildSelect(ctx->builder, cmp, val, LLVMConstReal(ctx->f32, -1.0), "");
	return val;
}

static LLVMValueRef emit_isign(struct nir_to_llvm_context *ctx,
			       LLVMValueRef src0)
{
	LLVMValueRef cmp, val;

	cmp = LLVMBuildICmp(ctx->builder, LLVMIntSGT, src0, ctx->i32zero, "");
	val = LLVMBuildSelect(ctx->builder, cmp, ctx->i32one, src0, "");
	cmp = LLVMBuildICmp(ctx->builder, LLVMIntSGE, val, ctx->i32zero, "");
	val = LLVMBuildSelect(ctx->builder, cmp, val, LLVMConstInt(ctx->i32, -1, true), "");
	return val;
}

static LLVMValueRef emit_ffract(struct nir_to_llvm_context *ctx,
				LLVMValueRef src0)
{
	const char *intr = "llvm.floor.f32";
	LLVMValueRef fsrc0 = to_float(ctx, src0);
	LLVMValueRef params[] = {
		fsrc0,
	};
	LLVMValueRef floor = ac_build_intrinsic(&ctx->ac, intr,
						ctx->f32, params, 1,
						AC_FUNC_ATTR_READNONE);
	return LLVMBuildFSub(ctx->builder, fsrc0, floor, "");
}

static LLVMValueRef emit_uint_carry(struct nir_to_llvm_context *ctx,
				    const char *intrin,
				    LLVMValueRef src0, LLVMValueRef src1)
{
	LLVMTypeRef ret_type;
	LLVMTypeRef types[] = { ctx->i32, ctx->i1 };
	LLVMValueRef res;
	LLVMValueRef params[] = { src0, src1 };
	ret_type = LLVMStructTypeInContext(ctx->context, types,
					   2, true);

	res = ac_build_intrinsic(&ctx->ac, intrin, ret_type,
				 params, 2, AC_FUNC_ATTR_READNONE);

	res = LLVMBuildExtractValue(ctx->builder, res, 1, "");
	res = LLVMBuildZExt(ctx->builder, res, ctx->i32, "");
	return res;
}

static LLVMValueRef emit_b2f(struct nir_to_llvm_context *ctx,
			     LLVMValueRef src0)
{
	return LLVMBuildAnd(ctx->builder, src0, LLVMBuildBitCast(ctx->builder, LLVMConstReal(ctx->f32, 1.0), ctx->i32, ""), "");
}

static LLVMValueRef emit_umul_high(struct nir_to_llvm_context *ctx,
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

static LLVMValueRef emit_imul_high(struct nir_to_llvm_context *ctx,
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

static LLVMValueRef emit_bitfield_extract(struct nir_to_llvm_context *ctx,
					  bool is_signed,
					  LLVMValueRef srcs[3])
{
	LLVMValueRef result;
	LLVMValueRef icond = LLVMBuildICmp(ctx->builder, LLVMIntEQ, srcs[2], LLVMConstInt(ctx->i32, 32, false), "");

	result = ac_build_bfe(&ctx->ac, srcs[0], srcs[1], srcs[2], is_signed);
	result = LLVMBuildSelect(ctx->builder, icond, srcs[0], result, "");
	return result;
}

static LLVMValueRef emit_bitfield_insert(struct nir_to_llvm_context *ctx,
					 LLVMValueRef src0, LLVMValueRef src1,
					 LLVMValueRef src2, LLVMValueRef src3)
{
	LLVMValueRef bfi_args[3], result;

	bfi_args[0] = LLVMBuildShl(ctx->builder,
				   LLVMBuildSub(ctx->builder,
						LLVMBuildShl(ctx->builder,
							     ctx->i32one,
							     src3, ""),
						ctx->i32one, ""),
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

static LLVMValueRef emit_pack_half_2x16(struct nir_to_llvm_context *ctx,
					LLVMValueRef src0)
{
	LLVMValueRef const16 = LLVMConstInt(ctx->i32, 16, false);
	int i;
	LLVMValueRef comp[2];

	src0 = to_float(ctx, src0);
	comp[0] = LLVMBuildExtractElement(ctx->builder, src0, ctx->i32zero, "");
	comp[1] = LLVMBuildExtractElement(ctx->builder, src0, ctx->i32one, "");
	for (i = 0; i < 2; i++) {
		comp[i] = LLVMBuildFPTrunc(ctx->builder, comp[i], ctx->f16, "");
		comp[i] = LLVMBuildBitCast(ctx->builder, comp[i], ctx->i16, "");
		comp[i] = LLVMBuildZExt(ctx->builder, comp[i], ctx->i32, "");
	}

	comp[1] = LLVMBuildShl(ctx->builder, comp[1], const16, "");
	comp[0] = LLVMBuildOr(ctx->builder, comp[0], comp[1], "");

	return comp[0];
}

static LLVMValueRef emit_unpack_half_2x16(struct nir_to_llvm_context *ctx,
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
					ctx->i32zero, "");
	result = LLVMBuildInsertElement(ctx->builder, result, temps[1],
					ctx->i32one, "");
	return result;
}

static LLVMValueRef emit_ddxy(struct nir_to_llvm_context *ctx,
			      nir_op op,
			      LLVMValueRef src0)
{
	unsigned mask;
	int idx;
	LLVMValueRef result;
	ctx->has_ddxy = true;

	if (!ctx->lds && !ctx->has_ds_bpermute)
		ctx->lds = LLVMAddGlobalInAddressSpace(ctx->module,
						       LLVMArrayType(ctx->i32, 64),
						       "ddxy_lds", LOCAL_ADDR_SPACE);

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

	result = ac_build_ddxy(&ctx->ac, ctx->has_ds_bpermute,
			      mask, idx, ctx->lds,
			      src0);
	return result;
}

/*
 * this takes an I,J coordinate pair,
 * and works out the X and Y derivatives.
 * it returns DDX(I), DDX(J), DDY(I), DDY(J).
 */
static LLVMValueRef emit_ddxy_interp(
	struct nir_to_llvm_context *ctx,
	LLVMValueRef interp_ij)
{
	LLVMValueRef result[4], a;
	unsigned i;

	for (i = 0; i < 2; i++) {
		a = LLVMBuildExtractElement(ctx->builder, interp_ij,
					    LLVMConstInt(ctx->i32, i, false), "");
		result[i] = emit_ddxy(ctx, nir_op_fddx, a);
		result[2+i] = emit_ddxy(ctx, nir_op_fddy, a);
	}
	return ac_build_gather_values(&ctx->ac, result, 4);
}

static void visit_alu(struct nir_to_llvm_context *ctx, nir_alu_instr *instr)
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
	        src[0] = to_float(ctx, src[0]);
		result = LLVMBuildFNeg(ctx->builder, src[0], "");
		break;
	case nir_op_ineg:
		result = LLVMBuildNeg(ctx->builder, src[0], "");
		break;
	case nir_op_inot:
		result = LLVMBuildNot(ctx->builder, src[0], "");
		break;
	case nir_op_iadd:
		result = LLVMBuildAdd(ctx->builder, src[0], src[1], "");
		break;
	case nir_op_fadd:
		src[0] = to_float(ctx, src[0]);
		src[1] = to_float(ctx, src[1]);
		result = LLVMBuildFAdd(ctx->builder, src[0], src[1], "");
		break;
	case nir_op_fsub:
		src[0] = to_float(ctx, src[0]);
		src[1] = to_float(ctx, src[1]);
		result = LLVMBuildFSub(ctx->builder, src[0], src[1], "");
		break;
	case nir_op_isub:
		result = LLVMBuildSub(ctx->builder, src[0], src[1], "");
		break;
	case nir_op_imul:
		result = LLVMBuildMul(ctx->builder, src[0], src[1], "");
		break;
	case nir_op_imod:
		result = LLVMBuildSRem(ctx->builder, src[0], src[1], "");
		break;
	case nir_op_umod:
		result = LLVMBuildURem(ctx->builder, src[0], src[1], "");
		break;
	case nir_op_fmod:
		src[0] = to_float(ctx, src[0]);
		src[1] = to_float(ctx, src[1]);
		result = ac_build_fdiv(&ctx->ac, src[0], src[1]);
		result = emit_intrin_1f_param(ctx, "llvm.floor",
		                              to_float_type(ctx, def_type), result);
		result = LLVMBuildFMul(ctx->builder, src[1] , result, "");
		result = LLVMBuildFSub(ctx->builder, src[0], result, "");
		break;
	case nir_op_frem:
		src[0] = to_float(ctx, src[0]);
		src[1] = to_float(ctx, src[1]);
		result = LLVMBuildFRem(ctx->builder, src[0], src[1], "");
		break;
	case nir_op_irem:
		result = LLVMBuildSRem(ctx->builder, src[0], src[1], "");
		break;
	case nir_op_idiv:
		result = LLVMBuildSDiv(ctx->builder, src[0], src[1], "");
		break;
	case nir_op_udiv:
		result = LLVMBuildUDiv(ctx->builder, src[0], src[1], "");
		break;
	case nir_op_fmul:
		src[0] = to_float(ctx, src[0]);
		src[1] = to_float(ctx, src[1]);
		result = LLVMBuildFMul(ctx->builder, src[0], src[1], "");
		break;
	case nir_op_fdiv:
		src[0] = to_float(ctx, src[0]);
		src[1] = to_float(ctx, src[1]);
		result = ac_build_fdiv(&ctx->ac, src[0], src[1]);
		break;
	case nir_op_frcp:
		src[0] = to_float(ctx, src[0]);
		result = ac_build_fdiv(&ctx->ac, ctx->f32one, src[0]);
		break;
	case nir_op_iand:
		result = LLVMBuildAnd(ctx->builder, src[0], src[1], "");
		break;
	case nir_op_ior:
		result = LLVMBuildOr(ctx->builder, src[0], src[1], "");
		break;
	case nir_op_ixor:
		result = LLVMBuildXor(ctx->builder, src[0], src[1], "");
		break;
	case nir_op_ishl:
		result = LLVMBuildShl(ctx->builder, src[0], src[1], "");
		break;
	case nir_op_ishr:
		result = LLVMBuildAShr(ctx->builder, src[0], src[1], "");
		break;
	case nir_op_ushr:
		result = LLVMBuildLShr(ctx->builder, src[0], src[1], "");
		break;
	case nir_op_ilt:
		result = emit_int_cmp(ctx, LLVMIntSLT, src[0], src[1]);
		break;
	case nir_op_ine:
		result = emit_int_cmp(ctx, LLVMIntNE, src[0], src[1]);
		break;
	case nir_op_ieq:
		result = emit_int_cmp(ctx, LLVMIntEQ, src[0], src[1]);
		break;
	case nir_op_ige:
		result = emit_int_cmp(ctx, LLVMIntSGE, src[0], src[1]);
		break;
	case nir_op_ult:
		result = emit_int_cmp(ctx, LLVMIntULT, src[0], src[1]);
		break;
	case nir_op_uge:
		result = emit_int_cmp(ctx, LLVMIntUGE, src[0], src[1]);
		break;
	case nir_op_feq:
		result = emit_float_cmp(ctx, LLVMRealUEQ, src[0], src[1]);
		break;
	case nir_op_fne:
		result = emit_float_cmp(ctx, LLVMRealUNE, src[0], src[1]);
		break;
	case nir_op_flt:
		result = emit_float_cmp(ctx, LLVMRealULT, src[0], src[1]);
		break;
	case nir_op_fge:
		result = emit_float_cmp(ctx, LLVMRealUGE, src[0], src[1]);
		break;
	case nir_op_fabs:
		result = emit_intrin_1f_param(ctx, "llvm.fabs",
		                              to_float_type(ctx, def_type), src[0]);
		break;
	case nir_op_iabs:
		result = emit_iabs(ctx, src[0]);
		break;
	case nir_op_imax:
		result = emit_minmax_int(ctx, LLVMIntSGT, src[0], src[1]);
		break;
	case nir_op_imin:
		result = emit_minmax_int(ctx, LLVMIntSLT, src[0], src[1]);
		break;
	case nir_op_umax:
		result = emit_minmax_int(ctx, LLVMIntUGT, src[0], src[1]);
		break;
	case nir_op_umin:
		result = emit_minmax_int(ctx, LLVMIntULT, src[0], src[1]);
		break;
	case nir_op_isign:
		result = emit_isign(ctx, src[0]);
		break;
	case nir_op_fsign:
		src[0] = to_float(ctx, src[0]);
		result = emit_fsign(ctx, src[0]);
		break;
	case nir_op_ffloor:
		result = emit_intrin_1f_param(ctx, "llvm.floor",
		                              to_float_type(ctx, def_type), src[0]);
		break;
	case nir_op_ftrunc:
		result = emit_intrin_1f_param(ctx, "llvm.trunc",
		                              to_float_type(ctx, def_type), src[0]);
		break;
	case nir_op_fceil:
		result = emit_intrin_1f_param(ctx, "llvm.ceil",
		                              to_float_type(ctx, def_type), src[0]);
		break;
	case nir_op_fround_even:
		result = emit_intrin_1f_param(ctx, "llvm.rint",
		                              to_float_type(ctx, def_type),src[0]);
		break;
	case nir_op_ffract:
		result = emit_ffract(ctx, src[0]);
		break;
	case nir_op_fsin:
		result = emit_intrin_1f_param(ctx, "llvm.sin",
		                              to_float_type(ctx, def_type), src[0]);
		break;
	case nir_op_fcos:
		result = emit_intrin_1f_param(ctx, "llvm.cos",
		                              to_float_type(ctx, def_type), src[0]);
		break;
	case nir_op_fsqrt:
		result = emit_intrin_1f_param(ctx, "llvm.sqrt",
		                              to_float_type(ctx, def_type), src[0]);
		break;
	case nir_op_fexp2:
		result = emit_intrin_1f_param(ctx, "llvm.exp2",
		                              to_float_type(ctx, def_type), src[0]);
		break;
	case nir_op_flog2:
		result = emit_intrin_1f_param(ctx, "llvm.log2",
		                              to_float_type(ctx, def_type), src[0]);
		break;
	case nir_op_frsq:
		result = emit_intrin_1f_param(ctx, "llvm.sqrt",
		                              to_float_type(ctx, def_type), src[0]);
		result = ac_build_fdiv(&ctx->ac, ctx->f32one, result);
		break;
	case nir_op_fpow:
		result = emit_intrin_2f_param(ctx, "llvm.pow",
		                              to_float_type(ctx, def_type), src[0], src[1]);
		break;
	case nir_op_fmax:
		result = emit_intrin_2f_param(ctx, "llvm.maxnum",
		                              to_float_type(ctx, def_type), src[0], src[1]);
		break;
	case nir_op_fmin:
		result = emit_intrin_2f_param(ctx, "llvm.minnum",
		                              to_float_type(ctx, def_type), src[0], src[1]);
		break;
	case nir_op_ffma:
		result = emit_intrin_3f_param(ctx, "llvm.fma",
		                              to_float_type(ctx, def_type), src[0], src[1], src[2]);
		break;
	case nir_op_ibitfield_extract:
		result = emit_bitfield_extract(ctx, true, src);
		break;
	case nir_op_ubitfield_extract:
		result = emit_bitfield_extract(ctx, false, src);
		break;
	case nir_op_bitfield_insert:
		result = emit_bitfield_insert(ctx, src[0], src[1], src[2], src[3]);
		break;
	case nir_op_bitfield_reverse:
		result = ac_build_intrinsic(&ctx->ac, "llvm.bitreverse.i32", ctx->i32, src, 1, AC_FUNC_ATTR_READNONE);
		break;
	case nir_op_bit_count:
		result = ac_build_intrinsic(&ctx->ac, "llvm.ctpop.i32", ctx->i32, src, 1, AC_FUNC_ATTR_READNONE);
		break;
	case nir_op_vec2:
	case nir_op_vec3:
	case nir_op_vec4:
		for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++)
			src[i] = to_integer(ctx, src[i]);
		result = ac_build_gather_values(&ctx->ac, src, num_components);
		break;
	case nir_op_d2i:
	case nir_op_f2i:
		src[0] = to_float(ctx, src[0]);
		result = LLVMBuildFPToSI(ctx->builder, src[0], def_type, "");
		break;
	case nir_op_d2u:
	case nir_op_f2u:
		src[0] = to_float(ctx, src[0]);
		result = LLVMBuildFPToUI(ctx->builder, src[0], def_type, "");
		break;
	case nir_op_i2d:
	case nir_op_i2f:
		result = LLVMBuildSIToFP(ctx->builder, src[0], to_float_type(ctx, def_type), "");
		break;
	case nir_op_u2d:
	case nir_op_u2f:
		result = LLVMBuildUIToFP(ctx->builder, src[0], to_float_type(ctx, def_type), "");
		break;
	case nir_op_f2d:
		result = LLVMBuildFPExt(ctx->builder, src[0], to_float_type(ctx, def_type), "");
		break;
	case nir_op_d2f:
		result = LLVMBuildFPTrunc(ctx->builder, src[0], to_float_type(ctx, def_type), "");
		break;
	case nir_op_u2u32:
	case nir_op_u2u64:
	case nir_op_u2i32:
	case nir_op_u2i64:
		if (get_elem_bits(ctx, LLVMTypeOf(src[0])) < get_elem_bits(ctx, def_type))
			result = LLVMBuildZExt(ctx->builder, src[0], def_type, "");
		else
			result = LLVMBuildTrunc(ctx->builder, src[0], def_type, "");
		break;
	case nir_op_i2u32:
	case nir_op_i2u64:
	case nir_op_i2i32:
	case nir_op_i2i64:
		if (get_elem_bits(ctx, LLVMTypeOf(src[0])) < get_elem_bits(ctx, def_type))
			result = LLVMBuildSExt(ctx->builder, src[0], def_type, "");
		else
			result = LLVMBuildTrunc(ctx->builder, src[0], def_type, "");
		break;
	case nir_op_bcsel:
		result = emit_bcsel(ctx, src[0], src[1], src[2]);
		break;
	case nir_op_find_lsb:
		result = emit_find_lsb(ctx, src[0]);
		break;
	case nir_op_ufind_msb:
		result = emit_ufind_msb(ctx, src[0]);
		break;
	case nir_op_ifind_msb:
		result = emit_ifind_msb(ctx, src[0]);
		break;
	case nir_op_uadd_carry:
		result = emit_uint_carry(ctx, "llvm.uadd.with.overflow.i32", src[0], src[1]);
		break;
	case nir_op_usub_borrow:
		result = emit_uint_carry(ctx, "llvm.usub.with.overflow.i32", src[0], src[1]);
		break;
	case nir_op_b2f:
		result = emit_b2f(ctx, src[0]);
		break;
	case nir_op_fquantize2f16:
		src[0] = to_float(ctx, src[0]);
		result = LLVMBuildFPTrunc(ctx->builder, src[0], ctx->f16, "");
		/* need to convert back up to f32 */
		result = LLVMBuildFPExt(ctx->builder, result, ctx->f32, "");
		break;
	case nir_op_umul_high:
		result = emit_umul_high(ctx, src[0], src[1]);
		break;
	case nir_op_imul_high:
		result = emit_imul_high(ctx, src[0], src[1]);
		break;
	case nir_op_pack_half_2x16:
		result = emit_pack_half_2x16(ctx, src[0]);
		break;
	case nir_op_unpack_half_2x16:
		result = emit_unpack_half_2x16(ctx, src[0]);
		break;
	case nir_op_fddx:
	case nir_op_fddy:
	case nir_op_fddx_fine:
	case nir_op_fddy_fine:
	case nir_op_fddx_coarse:
	case nir_op_fddy_coarse:
		result = emit_ddxy(ctx, instr->op, src[0]);
		break;
	default:
		fprintf(stderr, "Unknown NIR alu instr: ");
		nir_print_instr(&instr->instr, stderr);
		fprintf(stderr, "\n");
		abort();
	}

	if (result) {
		assert(instr->dest.dest.is_ssa);
		result = to_integer(ctx, result);
		_mesa_hash_table_insert(ctx->defs, &instr->dest.dest.ssa,
		                        result);
	}
}

static void visit_load_const(struct nir_to_llvm_context *ctx,
                             nir_load_const_instr *instr)
{
	LLVMValueRef values[4], value = NULL;
	LLVMTypeRef element_type =
	    LLVMIntTypeInContext(ctx->context, instr->def.bit_size);

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
get_buffer_size(struct nir_to_llvm_context *ctx, LLVMValueRef descriptor, bool in_elements)
{
	LLVMValueRef size =
		LLVMBuildExtractElement(ctx->builder, descriptor,
					LLVMConstInt(ctx->i32, 2, false), "");

	/* VI only */
	if (ctx->options->chip_class >= VI && in_elements) {
		/* On VI, the descriptor contains the size in bytes,
		 * but TXQ must return the size in elements.
		 * The stride is always non-zero for resources using TXQ.
		 */
		LLVMValueRef stride =
			LLVMBuildExtractElement(ctx->builder, descriptor,
						LLVMConstInt(ctx->i32, 1, false), "");
		stride = LLVMBuildLShr(ctx->builder, stride,
				       LLVMConstInt(ctx->i32, 16, false), "");
		stride = LLVMBuildAnd(ctx->builder, stride,
				      LLVMConstInt(ctx->i32, 0x3fff, false), "");

		size = LLVMBuildUDiv(ctx->builder, size, stride, "");
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

static LLVMValueRef radv_lower_gather4_integer(struct nir_to_llvm_context *ctx,
					       struct ac_tex_info *tinfo,
					       nir_tex_instr *instr,
					       const char *intr_name,
					       unsigned coord_vgpr_index)
{
	LLVMValueRef coord = tinfo->args[0];
	LLVMValueRef half_texel[2];
	int c;

	//TODO Rect
	{
		LLVMValueRef txq_args[10];
		int txq_arg_count = 0;
		LLVMValueRef size;
		bool da = instr->is_array || instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE;
		txq_args[txq_arg_count++] = LLVMConstInt(ctx->i32, 0, false);
		txq_args[txq_arg_count++] = tinfo->args[1];
		txq_args[txq_arg_count++] = LLVMConstInt(ctx->i32, 0xf, 0); /* dmask */
		txq_args[txq_arg_count++] = LLVMConstInt(ctx->i32, 0, 0); /* unorm */
		txq_args[txq_arg_count++] = LLVMConstInt(ctx->i32, 0, 0); /* r128 */
		txq_args[txq_arg_count++] = LLVMConstInt(ctx->i32, da ? 1 : 0, 0);
		txq_args[txq_arg_count++] = LLVMConstInt(ctx->i32, 0, 0); /* glc */
		txq_args[txq_arg_count++] = LLVMConstInt(ctx->i32, 0, 0); /* slc */
		txq_args[txq_arg_count++] = LLVMConstInt(ctx->i32, 0, 0); /* tfe */
		txq_args[txq_arg_count++] = LLVMConstInt(ctx->i32, 0, 0); /* lwe */
		size = ac_build_intrinsic(&ctx->ac, "llvm.SI.getresinfo.i32", ctx->v4i32,
					  txq_args, txq_arg_count,
					  AC_FUNC_ATTR_READNONE |
					  AC_FUNC_ATTR_LEGACY);

		for (c = 0; c < 2; c++) {
			half_texel[c] = LLVMBuildExtractElement(ctx->builder, size,
								LLVMConstInt(ctx->i32, c, false), "");
			half_texel[c] = LLVMBuildUIToFP(ctx->builder, half_texel[c], ctx->f32, "");
			half_texel[c] = ac_build_fdiv(&ctx->ac, ctx->f32one, half_texel[c]);
			half_texel[c] = LLVMBuildFMul(ctx->builder, half_texel[c],
						      LLVMConstReal(ctx->f32, -0.5), "");
		}
	}

	for (c = 0; c < 2; c++) {
		LLVMValueRef tmp;
		LLVMValueRef index = LLVMConstInt(ctx->i32, coord_vgpr_index + c, 0);
		tmp = LLVMBuildExtractElement(ctx->builder, coord, index, "");
		tmp = LLVMBuildBitCast(ctx->builder, tmp, ctx->f32, "");
		tmp = LLVMBuildFAdd(ctx->builder, tmp, half_texel[c], "");
		tmp = LLVMBuildBitCast(ctx->builder, tmp, ctx->i32, "");
		coord = LLVMBuildInsertElement(ctx->builder, coord, tmp, index, "");
	}

	tinfo->args[0] = coord;
	return ac_build_intrinsic(&ctx->ac, intr_name, tinfo->dst_type, tinfo->args, tinfo->arg_count,
				  AC_FUNC_ATTR_READNONE | AC_FUNC_ATTR_NOUNWIND |
				  AC_FUNC_ATTR_LEGACY);

}

static LLVMValueRef build_tex_intrinsic(struct nir_to_llvm_context *ctx,
					nir_tex_instr *instr,
					struct ac_tex_info *tinfo)
{
	const char *name = "llvm.SI.image.sample";
	const char *infix = "";
	char intr_name[127];
	char type[64];
	bool is_shadow = instr->is_shadow;
	bool has_offset = tinfo->has_offset;
	switch (instr->op) {
	case nir_texop_txf:
	case nir_texop_txf_ms:
	case nir_texop_samples_identical:
		name = instr->sampler_dim == GLSL_SAMPLER_DIM_MS ? "llvm.SI.image.load" :
		       instr->sampler_dim == GLSL_SAMPLER_DIM_BUF ? "llvm.SI.vs.load.input" :
			"llvm.SI.image.load.mip";
		is_shadow = false;
		has_offset = false;
		break;
	case nir_texop_txb:
		infix = ".b";
		break;
	case nir_texop_txl:
		infix = ".l";
		break;
	case nir_texop_txs:
		name = "llvm.SI.getresinfo";
		break;
	case nir_texop_query_levels:
		name = "llvm.SI.getresinfo";
		break;
	case nir_texop_tex:
		if (ctx->stage != MESA_SHADER_FRAGMENT)
			infix = ".lz";
		break;
	case nir_texop_txd:
		infix = ".d";
		break;
	case nir_texop_tg4:
		name = "llvm.SI.gather4";
		infix = ".lz";
		break;
	case nir_texop_lod:
		name = "llvm.SI.getlod";
		is_shadow = false;
		has_offset = false;
		break;
	default:
		break;
	}

	build_int_type_name(LLVMTypeOf(tinfo->args[0]), type, sizeof(type));
	sprintf(intr_name, "%s%s%s%s.%s", name, is_shadow ? ".c" : "", infix,
		has_offset ? ".o" : "", type);

	if (instr->op == nir_texop_tg4) {
		enum glsl_base_type stype = glsl_get_sampler_result_type(instr->texture->var->type);
		if (stype == GLSL_TYPE_UINT || stype == GLSL_TYPE_INT) {
			return radv_lower_gather4_integer(ctx, tinfo, instr, intr_name,
							  (int)has_offset + (int)is_shadow);
		}
	}
	return ac_build_intrinsic(&ctx->ac, intr_name, tinfo->dst_type, tinfo->args, tinfo->arg_count,
				  AC_FUNC_ATTR_READNONE | AC_FUNC_ATTR_NOUNWIND |
				  AC_FUNC_ATTR_LEGACY);

}

static LLVMValueRef visit_vulkan_resource_index(struct nir_to_llvm_context *ctx,
                                                nir_intrinsic_instr *instr)
{
	LLVMValueRef index = get_src(ctx, instr->src[0]);
	unsigned desc_set = nir_intrinsic_desc_set(instr);
	unsigned binding = nir_intrinsic_binding(instr);
	LLVMValueRef desc_ptr = ctx->descriptor_sets[desc_set];
	struct radv_descriptor_set_layout *layout = ctx->options->layout->set[desc_set].layout;
	unsigned base_offset = layout->binding[binding].offset;
	LLVMValueRef offset, stride;

	if (layout->binding[binding].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
	    layout->binding[binding].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
		desc_ptr = ctx->push_constants;
		base_offset = ctx->options->layout->push_constant_size;
		base_offset +=  16 * layout->binding[binding].dynamic_offset_offset;
		stride = LLVMConstInt(ctx->i32, 16, false);
	} else
		stride = LLVMConstInt(ctx->i32, layout->binding[binding].size, false);

	offset = LLVMConstInt(ctx->i32, base_offset, false);
	index = LLVMBuildMul(ctx->builder, index, stride, "");
	offset = LLVMBuildAdd(ctx->builder, offset, index, "");
	
	desc_ptr = ac_build_gep0(&ctx->ac, desc_ptr, offset);
	desc_ptr = cast_ptr(ctx, desc_ptr, ctx->v4i32);
	LLVMSetMetadata(desc_ptr, ctx->uniform_md_kind, ctx->empty_md);

	return LLVMBuildLoad(ctx->builder, desc_ptr, "");
}

static LLVMValueRef visit_load_push_constant(struct nir_to_llvm_context *ctx,
                                             nir_intrinsic_instr *instr)
{
	LLVMValueRef ptr, addr;

	addr = LLVMConstInt(ctx->i32, nir_intrinsic_base(instr), 0);
	addr = LLVMBuildAdd(ctx->builder, addr, get_src(ctx, instr->src[0]), "");

	ptr = ac_build_gep0(&ctx->ac, ctx->push_constants, addr);
	ptr = cast_ptr(ctx, ptr, get_def_type(ctx, &instr->dest.ssa));

	return LLVMBuildLoad(ctx->builder, ptr, "");
}

static LLVMValueRef visit_get_buffer_size(struct nir_to_llvm_context *ctx,
                                          nir_intrinsic_instr *instr)
{
	LLVMValueRef desc = get_src(ctx, instr->src[0]);

	return get_buffer_size(ctx, desc, false);
}
static void visit_store_ssbo(struct nir_to_llvm_context *ctx,
                             nir_intrinsic_instr *instr)
{
	const char *store_name;
	LLVMValueRef src_data = get_src(ctx, instr->src[0]);
	LLVMTypeRef data_type = ctx->f32;
	int elem_size_mult = get_elem_bits(ctx, LLVMTypeOf(src_data)) / 32;
	int components_32bit = elem_size_mult * instr->num_components;
	unsigned writemask = nir_intrinsic_write_mask(instr);
	LLVMValueRef base_data, base_offset;
	LLVMValueRef params[6];

	if (ctx->stage == MESA_SHADER_FRAGMENT)
		ctx->shader_info->fs.writes_memory = true;

	params[1] = get_src(ctx, instr->src[1]);
	params[2] = LLVMConstInt(ctx->i32, 0, false); /* vindex */
	params[4] = LLVMConstInt(ctx->i1, 0, false);  /* glc */
	params[5] = LLVMConstInt(ctx->i1, 0, false);  /* slc */

	if (components_32bit > 1)
		data_type = LLVMVectorType(ctx->f32, components_32bit);

	base_data = to_float(ctx, src_data);
	base_data = trim_vector(ctx, base_data, instr->num_components);
	base_data = LLVMBuildBitCast(ctx->builder, base_data,
				     data_type, "");
	base_offset = get_src(ctx, instr->src[2]);      /* voffset */
	while (writemask) {
		int start, count;
		LLVMValueRef data;
		LLVMValueRef offset;
		LLVMValueRef tmp;
		u_bit_scan_consecutive_range(&writemask, &start, &count);

		/* Due to an LLVM limitation, split 3-element writes
		 * into a 2-element and a 1-element write. */
		if (count == 3) {
			writemask |= 1 << (start + 2);
			count = 2;
		}

		start *= elem_size_mult;
		count *= elem_size_mult;

		if (count > 4) {
			writemask |= ((1u << (count - 4)) - 1u) << (start + 4);
			count = 4;
		}

		if (count == 4) {
			store_name = "llvm.amdgcn.buffer.store.v4f32";
			data = base_data;
		} else if (count == 2) {
			tmp = LLVMBuildExtractElement(ctx->builder,
						      base_data, LLVMConstInt(ctx->i32, start, false), "");
			data = LLVMBuildInsertElement(ctx->builder, LLVMGetUndef(ctx->v2f32), tmp,
						      ctx->i32zero, "");

			tmp = LLVMBuildExtractElement(ctx->builder,
						      base_data, LLVMConstInt(ctx->i32, start + 1, false), "");
			data = LLVMBuildInsertElement(ctx->builder, data, tmp,
						      ctx->i32one, "");
			store_name = "llvm.amdgcn.buffer.store.v2f32";

		} else {
			assert(count == 1);
			if (get_llvm_num_components(base_data) > 1)
				data = LLVMBuildExtractElement(ctx->builder, base_data,
							       LLVMConstInt(ctx->i32, start, false), "");
			else
				data = base_data;
			store_name = "llvm.amdgcn.buffer.store.f32";
		}

		offset = base_offset;
		if (start != 0) {
			offset = LLVMBuildAdd(ctx->builder, offset, LLVMConstInt(ctx->i32, start * 4, false), "");
		}
		params[0] = data;
		params[3] = offset;
		ac_build_intrinsic(&ctx->ac, store_name,
				   ctx->voidt, params, 6, 0);
	}
}

static LLVMValueRef visit_atomic_ssbo(struct nir_to_llvm_context *ctx,
                                      nir_intrinsic_instr *instr)
{
	const char *name;
	LLVMValueRef params[6];
	int arg_count = 0;
	if (ctx->stage == MESA_SHADER_FRAGMENT)
		ctx->shader_info->fs.writes_memory = true;

	if (instr->intrinsic == nir_intrinsic_ssbo_atomic_comp_swap) {
		params[arg_count++] = llvm_extract_elem(ctx, get_src(ctx, instr->src[3]), 0);
	}
	params[arg_count++] = llvm_extract_elem(ctx, get_src(ctx, instr->src[2]), 0);
	params[arg_count++] = get_src(ctx, instr->src[0]);
	params[arg_count++] = LLVMConstInt(ctx->i32, 0, false); /* vindex */
	params[arg_count++] = get_src(ctx, instr->src[1]);      /* voffset */
	params[arg_count++] = LLVMConstInt(ctx->i1, 0, false);  /* slc */

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

	return ac_build_intrinsic(&ctx->ac, name, ctx->i32, params, arg_count, 0);
}

static LLVMValueRef visit_load_buffer(struct nir_to_llvm_context *ctx,
                                      nir_intrinsic_instr *instr)
{
	LLVMValueRef results[2];
	int load_components;
	int num_components = instr->num_components;
	if (instr->dest.ssa.bit_size == 64)
		num_components *= 2;

	for (int i = 0; i < num_components; i += load_components) {
		load_components = MIN2(num_components - i, 4);
		const char *load_name;
		LLVMTypeRef data_type = ctx->f32;
		LLVMValueRef offset = LLVMConstInt(ctx->i32, i * 4, false);
		offset = LLVMBuildAdd(ctx->builder, get_src(ctx, instr->src[1]), offset, "");

		if (load_components == 3)
			data_type = LLVMVectorType(ctx->f32, 4);
		else if (load_components > 1)
			data_type = LLVMVectorType(ctx->f32, load_components);

		if (load_components >= 3)
			load_name = "llvm.amdgcn.buffer.load.v4f32";
		else if (load_components == 2)
			load_name = "llvm.amdgcn.buffer.load.v2f32";
		else if (load_components == 1)
			load_name = "llvm.amdgcn.buffer.load.f32";
		else
			unreachable("unhandled number of components");

		LLVMValueRef params[] = {
			get_src(ctx, instr->src[0]),
			LLVMConstInt(ctx->i32, 0, false),
			offset,
			LLVMConstInt(ctx->i1, 0, false),
			LLVMConstInt(ctx->i1, 0, false),
		};

		results[i] = ac_build_intrinsic(&ctx->ac, load_name, data_type, params, 5, 0);

	}

	LLVMValueRef ret = results[0];
	if (num_components > 4 || num_components == 3) {
		LLVMValueRef masks[] = {
		        LLVMConstInt(ctx->i32, 0, false), LLVMConstInt(ctx->i32, 1, false),
		        LLVMConstInt(ctx->i32, 2, false), LLVMConstInt(ctx->i32, 3, false),
			LLVMConstInt(ctx->i32, 4, false), LLVMConstInt(ctx->i32, 5, false),
		        LLVMConstInt(ctx->i32, 6, false), LLVMConstInt(ctx->i32, 7, false)
		};

		LLVMValueRef swizzle = LLVMConstVector(masks, num_components);
		ret = LLVMBuildShuffleVector(ctx->builder, results[0],
					     results[num_components > 4 ? 1 : 0], swizzle, "");
	}

	return LLVMBuildBitCast(ctx->builder, ret,
	                        get_def_type(ctx, &instr->dest.ssa), "");
}

static LLVMValueRef visit_load_ubo_buffer(struct nir_to_llvm_context *ctx,
                                          nir_intrinsic_instr *instr)
{
	LLVMValueRef results[8], ret;
	LLVMValueRef rsrc = get_src(ctx, instr->src[0]);
	LLVMValueRef offset = get_src(ctx, instr->src[1]);
	int num_components = instr->num_components;

	rsrc = LLVMBuildBitCast(ctx->builder, rsrc, LLVMVectorType(ctx->i8, 16), "");

	if (instr->dest.ssa.bit_size == 64)
		num_components *= 2;

	for (unsigned i = 0; i < num_components; ++i) {
		LLVMValueRef params[] = {
			rsrc,
			LLVMBuildAdd(ctx->builder, LLVMConstInt(ctx->i32, 4 * i, 0),
				     offset, "")
		};
		results[i] = ac_build_intrinsic(&ctx->ac, "llvm.SI.load.const", ctx->f32,
						params, 2,
						AC_FUNC_ATTR_READNONE |
						AC_FUNC_ATTR_LEGACY);
	}


	ret = ac_build_gather_values(&ctx->ac, results, instr->num_components);
	return LLVMBuildBitCast(ctx->builder, ret,
	                        get_def_type(ctx, &instr->dest.ssa), "");
}

static void
radv_get_deref_offset(struct nir_to_llvm_context *ctx, nir_deref *tail,
		      bool vs_in, unsigned *vertex_index_out,
		      unsigned *const_out, LLVMValueRef *indir_out)
{
	unsigned const_offset = 0;
	LLVMValueRef offset = NULL;

	if (vertex_index_out != NULL) {
		tail = tail->child;
		nir_deref_array *deref_array = nir_deref_as_array(tail);
		*vertex_index_out = deref_array->base_offset;
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
			stride = LLVMConstInt(ctx->i32, size, 0);
			local_offset = LLVMBuildMul(ctx->builder, stride, index, "");

			if (offset)
				offset = LLVMBuildAdd(ctx->builder, offset, local_offset, "");
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

	if (const_offset && offset)
		offset = LLVMBuildAdd(ctx->builder, offset,
				      LLVMConstInt(ctx->i32, const_offset, 0),
				      "");

	*const_out = const_offset;
	*indir_out = offset;
}

static LLVMValueRef
load_gs_input(struct nir_to_llvm_context *ctx,
	      nir_intrinsic_instr *instr)
{
	LLVMValueRef indir_index, vtx_offset;
	unsigned const_index;
	LLVMValueRef args[9];
	unsigned param, vtx_offset_param;
	LLVMValueRef value[4], result;
	unsigned vertex_index;
	unsigned cull_offset = 0;
	radv_get_deref_offset(ctx, &instr->variables[0]->deref,
			      false, &vertex_index,
			      &const_index, &indir_index);
	vtx_offset_param = vertex_index;
	assert(vtx_offset_param < 6);
	vtx_offset = LLVMBuildMul(ctx->builder, ctx->gs_vtx_offset[vtx_offset_param],
				  LLVMConstInt(ctx->i32, 4, false), "");

	param = shader_io_get_unique_index(instr->variables[0]->var->data.location);
	if (instr->variables[0]->var->data.location == VARYING_SLOT_CULL_DIST0)
		cull_offset += ctx->num_input_clips;
	for (unsigned i = 0; i < instr->num_components; i++) {

		args[0] = ctx->esgs_ring;
		args[1] = vtx_offset;
		args[2] = LLVMConstInt(ctx->i32, (param * 4 + i + const_index + cull_offset) * 256, false);
		args[3] = ctx->i32zero;
		args[4] = ctx->i32one; /* OFFEN */
		args[5] = ctx->i32zero; /* IDXEN */
		args[6] = ctx->i32one; /* GLC */
		args[7] = ctx->i32zero; /* SLC */
		args[8] = ctx->i32zero; /* TFE */

		value[i] = ac_build_intrinsic(&ctx->ac, "llvm.SI.buffer.load.dword.i32.i32",
					      ctx->i32, args, 9,
					      AC_FUNC_ATTR_READONLY |
					      AC_FUNC_ATTR_LEGACY);
	}
	result = ac_build_gather_values(&ctx->ac, value, instr->num_components);

	return result;
}

static LLVMValueRef visit_load_var(struct nir_to_llvm_context *ctx,
				   nir_intrinsic_instr *instr)
{
	LLVMValueRef values[8];
	int idx = instr->variables[0]->var->data.driver_location;
	int ve = instr->dest.ssa.num_components;
	LLVMValueRef indir_index;
	LLVMValueRef ret;
	unsigned const_index;
	bool vs_in = ctx->stage == MESA_SHADER_VERTEX &&
	             instr->variables[0]->var->data.mode == nir_var_shader_in;
	radv_get_deref_offset(ctx, &instr->variables[0]->deref, vs_in, NULL,
				      &const_index, &indir_index);

	if (instr->dest.ssa.bit_size == 64)
		ve *= 2;

	switch (instr->variables[0]->var->data.mode) {
	case nir_var_shader_in:
		if (ctx->stage == MESA_SHADER_GEOMETRY) {
			return load_gs_input(ctx, instr);
		}
		for (unsigned chan = 0; chan < ve; chan++) {
			if (indir_index) {
				unsigned count = glsl_count_attribute_slots(
						instr->variables[0]->var->type,
						ctx->stage == MESA_SHADER_VERTEX);
				count -= chan / 4;
				LLVMValueRef tmp_vec = ac_build_gather_values_extended(
						&ctx->ac, ctx->inputs + idx + chan, count,
						4, false);

				values[chan] = LLVMBuildExtractElement(ctx->builder,
								       tmp_vec,
								       indir_index, "");
			} else
				values[chan] = ctx->inputs[idx + chan + const_index * 4];
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
						4, true);

				values[chan] = LLVMBuildExtractElement(ctx->builder,
								       tmp_vec,
								       indir_index, "");
			} else {
				values[chan] = LLVMBuildLoad(ctx->builder, ctx->locals[idx + chan + const_index * 4], "");
			}
		}
		break;
	case nir_var_shader_out:
		for (unsigned chan = 0; chan < ve; chan++) {
			if (indir_index) {
				unsigned count = glsl_count_attribute_slots(
						instr->variables[0]->var->type, false);
				count -= chan / 4;
				LLVMValueRef tmp_vec = ac_build_gather_values_extended(
						&ctx->ac, ctx->outputs + idx + chan, count,
						4, true);

				values[chan] = LLVMBuildExtractElement(ctx->builder,
								       tmp_vec,
								       indir_index, "");
			} else {
			values[chan] = LLVMBuildLoad(ctx->builder,
						     ctx->outputs[idx + chan + const_index * 4],
						     "");
			}
		}
		break;
	case nir_var_shared: {
		LLVMValueRef ptr = get_shared_memory_ptr(ctx, idx, ctx->i32);
		LLVMValueRef derived_ptr;

		if (indir_index)
			indir_index = LLVMBuildMul(ctx->builder, indir_index, LLVMConstInt(ctx->i32, 4, false), "");

		for (unsigned chan = 0; chan < ve; chan++) {
			LLVMValueRef index = LLVMConstInt(ctx->i32, chan, false);
			if (indir_index)
				index = LLVMBuildAdd(ctx->builder, index, indir_index, "");
			derived_ptr = LLVMBuildGEP(ctx->builder, ptr, &index, 1, "");

			values[chan] = LLVMBuildLoad(ctx->builder, derived_ptr, "");
		}
		break;
	}
	default:
		unreachable("unhandle variable mode");
	}
	ret = ac_build_gather_values(&ctx->ac, values, ve);
	return LLVMBuildBitCast(ctx->builder, ret, get_def_type(ctx, &instr->dest.ssa), "");
}

static void
visit_store_var(struct nir_to_llvm_context *ctx,
				   nir_intrinsic_instr *instr)
{
	LLVMValueRef temp_ptr, value;
	int idx = instr->variables[0]->var->data.driver_location;
	LLVMValueRef src = to_float(ctx, get_src(ctx, instr->src[0]));
	int writemask = instr->const_index[0];
	LLVMValueRef indir_index;
	unsigned const_index;
	radv_get_deref_offset(ctx, &instr->variables[0]->deref, false,
	                      NULL, &const_index, &indir_index);

	if (get_elem_bits(ctx, LLVMTypeOf(src)) == 64) {
		int old_writemask = writemask;

		src = LLVMBuildBitCast(ctx->builder, src,
		                       LLVMVectorType(ctx->f32, get_llvm_num_components(src) * 2),
		                       "");

		writemask = 0;
		for (unsigned chan = 0; chan < 4; chan++) {
			if (old_writemask & (1 << chan))
				writemask |= 3u << (2 * chan);
		}
	}

	switch (instr->variables[0]->var->data.mode) {
	case nir_var_shader_out:
		for (unsigned chan = 0; chan < 8; chan++) {
			int stride = 4;
			if (!(writemask & (1 << chan)))
				continue;

			value = llvm_extract_elem(ctx, src, chan);

			if (instr->variables[0]->var->data.location == VARYING_SLOT_CLIP_DIST0 ||
			    instr->variables[0]->var->data.location == VARYING_SLOT_CULL_DIST0)
				stride = 1;
			if (indir_index) {
				unsigned count = glsl_count_attribute_slots(
						instr->variables[0]->var->type, false);
				count -= chan / 4;
				LLVMValueRef tmp_vec = ac_build_gather_values_extended(
						&ctx->ac, ctx->outputs + idx + chan, count,
						stride, true);

				if (get_llvm_num_components(tmp_vec) > 1) {
					tmp_vec = LLVMBuildInsertElement(ctx->builder, tmp_vec,
									 value, indir_index, "");
				} else
					tmp_vec = value;
				build_store_values_extended(ctx, ctx->outputs + idx + chan,
							    count, stride, tmp_vec);

			} else {
				temp_ptr = ctx->outputs[idx + chan + const_index * stride];

				LLVMBuildStore(ctx->builder, value, temp_ptr);
			}
		}
		break;
	case nir_var_local:
		for (unsigned chan = 0; chan < 8; chan++) {
			if (!(writemask & (1 << chan)))
				continue;

			value = llvm_extract_elem(ctx, src, chan);
			if (indir_index) {
				unsigned count = glsl_count_attribute_slots(
					instr->variables[0]->var->type, false);
				count -= chan / 4;
				LLVMValueRef tmp_vec = ac_build_gather_values_extended(
					&ctx->ac, ctx->locals + idx + chan, count,
					4, true);

				tmp_vec = LLVMBuildInsertElement(ctx->builder, tmp_vec,
								 value, indir_index, "");
				build_store_values_extended(ctx, ctx->locals + idx + chan,
							    count, 4, tmp_vec);
			} else {
				temp_ptr = ctx->locals[idx + chan + const_index * 4];

				LLVMBuildStore(ctx->builder, value, temp_ptr);
			}
		}
		break;
	case nir_var_shared: {
		LLVMValueRef ptr = get_shared_memory_ptr(ctx, idx, ctx->i32);

		if (indir_index)
			indir_index = LLVMBuildMul(ctx->builder, indir_index, LLVMConstInt(ctx->i32, 4, false), "");

		for (unsigned chan = 0; chan < 8; chan++) {
			if (!(writemask & (1 << chan)))
				continue;
			LLVMValueRef index = LLVMConstInt(ctx->i32, chan, false);
			LLVMValueRef derived_ptr;

			if (indir_index)
				index = LLVMBuildAdd(ctx->builder, index, indir_index, "");

			value = llvm_extract_elem(ctx, src, chan);
			derived_ptr = LLVMBuildGEP(ctx->builder, ptr, &index, 1, "");
			LLVMBuildStore(ctx->builder,
			               to_integer(ctx, value), derived_ptr);
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


static void get_image_intr_name(const char *base_name,
                                LLVMTypeRef data_type,
                                LLVMTypeRef coords_type,
                                LLVMTypeRef rsrc_type,
                                char *out_name, unsigned out_len)
{
        char coords_type_name[8];

        ac_build_type_name_for_intr(coords_type, coords_type_name,
                            sizeof(coords_type_name));

        if (HAVE_LLVM <= 0x0309) {
                snprintf(out_name, out_len, "%s.%s", base_name, coords_type_name);
        } else {
                char data_type_name[8];
                char rsrc_type_name[8];

                ac_build_type_name_for_intr(data_type, data_type_name,
                                        sizeof(data_type_name));
                ac_build_type_name_for_intr(rsrc_type, rsrc_type_name,
                                        sizeof(rsrc_type_name));
                snprintf(out_name, out_len, "%s.%s.%s.%s", base_name,
                         data_type_name, coords_type_name, rsrc_type_name);
        }
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
static LLVMValueRef adjust_sample_index_using_fmask(struct nir_to_llvm_context *ctx,
						    LLVMValueRef coord_x, LLVMValueRef coord_y,
						    LLVMValueRef coord_z,
						    LLVMValueRef sample_index,
						    LLVMValueRef fmask_desc_ptr)
{
	LLVMValueRef fmask_load_address[4], params[7];
	LLVMValueRef glc = LLVMConstInt(ctx->i1, 0, false);
	LLVMValueRef slc = LLVMConstInt(ctx->i1, 0, false);
	LLVMValueRef da = coord_z ? ctx->i32one : ctx->i32zero;
	LLVMValueRef res;
	char intrinsic_name[64];

	fmask_load_address[0] = coord_x;
	fmask_load_address[1] = coord_y;
	if (coord_z) {
		fmask_load_address[2] = coord_z;
		fmask_load_address[3] = LLVMGetUndef(ctx->i32);
	}

	params[0] = ac_build_gather_values(&ctx->ac, fmask_load_address, coord_z ? 4 : 2);
	params[1] = fmask_desc_ptr;
	params[2] = LLVMConstInt(ctx->i32, 15, false); /* dmask */
	LLVMValueRef lwe = LLVMConstInt(ctx->i1, 0, false);
	params[3] = glc;
	params[4] = slc;
	params[5] = lwe;
	params[6] = da;

	get_image_intr_name("llvm.amdgcn.image.load",
			    ctx->v4f32, /* vdata */
			    LLVMTypeOf(params[0]), /* coords */
			    LLVMTypeOf(params[1]), /* rsrc */
			    intrinsic_name, sizeof(intrinsic_name));

	res = ac_build_intrinsic(&ctx->ac, intrinsic_name, ctx->v4f32,
				 params, 7, AC_FUNC_ATTR_READONLY);

	res = to_integer(ctx, res);
	LLVMValueRef four = LLVMConstInt(ctx->i32, 4, false);
	LLVMValueRef F = LLVMConstInt(ctx->i32, 0xf, false);

	LLVMValueRef fmask = LLVMBuildExtractElement(ctx->builder,
						     res,
						     ctx->i32zero, "");

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
		LLVMBuildBitCast(ctx->builder, params[1],
				 ctx->v8i32, "");

	LLVMValueRef fmask_word1 =
		LLVMBuildExtractElement(ctx->builder, fmask_desc,
					ctx->i32one, "");

	LLVMValueRef word1_is_nonzero =
		LLVMBuildICmp(ctx->builder, LLVMIntNE,
			      fmask_word1, ctx->i32zero, "");

	/* Replace the MSAA sample index. */
	sample_index =
		LLVMBuildSelect(ctx->builder, word1_is_nonzero,
				final_sample, sample_index, "");
	return sample_index;
}

static LLVMValueRef get_image_coords(struct nir_to_llvm_context *ctx,
				     nir_intrinsic_instr *instr)
{
	const struct glsl_type *type = instr->variables[0]->var->type;
	if(instr->variables[0]->deref.child)
		type = instr->variables[0]->deref.child->type;

	LLVMValueRef src0 = get_src(ctx, instr->src[0]);
	LLVMValueRef coords[4];
	LLVMValueRef masks[] = {
		LLVMConstInt(ctx->i32, 0, false), LLVMConstInt(ctx->i32, 1, false),
		LLVMConstInt(ctx->i32, 2, false), LLVMConstInt(ctx->i32, 3, false),
	};
	LLVMValueRef res;
	LLVMValueRef sample_index = llvm_extract_elem(ctx, get_src(ctx, instr->src[1]), 0);

	int count;
	enum glsl_sampler_dim dim = glsl_get_sampler_dim(type);
	bool add_frag_pos = (dim == GLSL_SAMPLER_DIM_SUBPASS ||
			     dim == GLSL_SAMPLER_DIM_SUBPASS_MS);
	bool is_ms = (dim == GLSL_SAMPLER_DIM_MS ||
		      dim == GLSL_SAMPLER_DIM_SUBPASS_MS);

	count = image_type_to_components_count(dim,
					       glsl_sampler_type_is_array(type));

	if (is_ms) {
		LLVMValueRef fmask_load_address[3];
		int chan;

		fmask_load_address[0] = LLVMBuildExtractElement(ctx->builder, src0, masks[0], "");
		fmask_load_address[1] = LLVMBuildExtractElement(ctx->builder, src0, masks[1], "");
		if (glsl_sampler_type_is_array(type))
			fmask_load_address[2] = LLVMBuildExtractElement(ctx->builder, src0, masks[2], "");
		else
			fmask_load_address[2] = NULL;
		if (add_frag_pos) {
			for (chan = 0; chan < 2; ++chan)
				fmask_load_address[chan] = LLVMBuildAdd(ctx->builder, fmask_load_address[chan], LLVMBuildFPToUI(ctx->builder, ctx->frag_pos[chan], ctx->i32, ""), "");
		}
		sample_index = adjust_sample_index_using_fmask(ctx,
							       fmask_load_address[0],
							       fmask_load_address[1],
							       fmask_load_address[2],
							       sample_index,
							       get_sampler_desc(ctx, instr->variables[0], DESC_FMASK));
	}
	if (count == 1) {
		if (instr->src[0].ssa->num_components)
			res = LLVMBuildExtractElement(ctx->builder, src0, masks[0], "");
		else
			res = src0;
	} else {
		int chan;
		if (is_ms)
			count--;
		for (chan = 0; chan < count; ++chan) {
			coords[chan] = LLVMBuildExtractElement(ctx->builder, src0, masks[chan], "");
		}

		if (add_frag_pos) {
			for (chan = 0; chan < count; ++chan)
				coords[chan] = LLVMBuildAdd(ctx->builder, coords[chan], LLVMBuildFPToUI(ctx->builder, ctx->frag_pos[chan], ctx->i32, ""), "");
		}
		if (is_ms) {
			coords[count] = sample_index;
			count++;
		}

		if (count == 3) {
			coords[3] = LLVMGetUndef(ctx->i32);
			count = 4;
		}
		res = ac_build_gather_values(&ctx->ac, coords, count);
	}
	return res;
}

static LLVMValueRef visit_image_load(struct nir_to_llvm_context *ctx,
				     nir_intrinsic_instr *instr)
{
	LLVMValueRef params[7];
	LLVMValueRef res;
	char intrinsic_name[64];
	const nir_variable *var = instr->variables[0]->var;
	const struct glsl_type *type = var->type;
	if(instr->variables[0]->deref.child)
		type = instr->variables[0]->deref.child->type;

	type = glsl_without_array(type);
	if (glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_BUF) {
		params[0] = get_sampler_desc(ctx, instr->variables[0], DESC_BUFFER);
		params[1] = LLVMBuildExtractElement(ctx->builder, get_src(ctx, instr->src[0]),
						    LLVMConstInt(ctx->i32, 0, false), ""); /* vindex */
		params[2] = LLVMConstInt(ctx->i32, 0, false); /* voffset */
		params[3] = LLVMConstInt(ctx->i1, 0, false);  /* glc */
		params[4] = LLVMConstInt(ctx->i1, 0, false);  /* slc */
		res = ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.buffer.load.format.v4f32", ctx->v4f32,
					 params, 5, 0);

		res = trim_vector(ctx, res, instr->dest.ssa.num_components);
		res = to_integer(ctx, res);
	} else {
		bool is_da = glsl_sampler_type_is_array(type) ||
			     glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_CUBE;
		LLVMValueRef da = is_da ? ctx->i32one : ctx->i32zero;
		LLVMValueRef glc = LLVMConstInt(ctx->i1, 0, false);
		LLVMValueRef slc = LLVMConstInt(ctx->i1, 0, false);

		params[0] = get_image_coords(ctx, instr);
		params[1] = get_sampler_desc(ctx, instr->variables[0], DESC_IMAGE);
		params[2] = LLVMConstInt(ctx->i32, 15, false); /* dmask */
		if (HAVE_LLVM <= 0x0309) {
			params[3] = LLVMConstInt(ctx->i1, 0, false);  /* r128 */
			params[4] = da;
			params[5] = glc;
			params[6] = slc;
		} else {
			LLVMValueRef lwe = LLVMConstInt(ctx->i1, 0, false);
			params[3] = glc;
			params[4] = slc;
			params[5] = lwe;
			params[6] = da;
		}

		get_image_intr_name("llvm.amdgcn.image.load",
				    ctx->v4f32, /* vdata */
				    LLVMTypeOf(params[0]), /* coords */
				    LLVMTypeOf(params[1]), /* rsrc */
				    intrinsic_name, sizeof(intrinsic_name));

		res = ac_build_intrinsic(&ctx->ac, intrinsic_name, ctx->v4f32,
					 params, 7, AC_FUNC_ATTR_READONLY);
	}
	return to_integer(ctx, res);
}

static void visit_image_store(struct nir_to_llvm_context *ctx,
			      nir_intrinsic_instr *instr)
{
	LLVMValueRef params[8];
	char intrinsic_name[64];
	const nir_variable *var = instr->variables[0]->var;
	LLVMValueRef i1false = LLVMConstInt(ctx->i1, 0, 0);
	LLVMValueRef i1true = LLVMConstInt(ctx->i1, 1, 0);
	const struct glsl_type *type = glsl_without_array(var->type);

	if (ctx->stage == MESA_SHADER_FRAGMENT)
		ctx->shader_info->fs.writes_memory = true;

	if (glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_BUF) {
		params[0] = to_float(ctx, get_src(ctx, instr->src[2])); /* data */
		params[1] = get_sampler_desc(ctx, instr->variables[0], DESC_BUFFER);
		params[2] = LLVMBuildExtractElement(ctx->builder, get_src(ctx, instr->src[0]),
						    LLVMConstInt(ctx->i32, 0, false), ""); /* vindex */
		params[3] = LLVMConstInt(ctx->i32, 0, false); /* voffset */
		params[4] = i1false;  /* glc */
		params[5] = i1false;  /* slc */
		ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.buffer.store.format.v4f32", ctx->voidt,
				   params, 6, 0);
	} else {
		bool is_da = glsl_sampler_type_is_array(type) ||
			     glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_CUBE;
		LLVMValueRef da = is_da ? i1true : i1false;
		LLVMValueRef glc = i1false;
		LLVMValueRef slc = i1false;

		params[0] = to_float(ctx, get_src(ctx, instr->src[2]));
		params[1] = get_image_coords(ctx, instr); /* coords */
		params[2] = get_sampler_desc(ctx, instr->variables[0], DESC_IMAGE);
		params[3] = LLVMConstInt(ctx->i32, 15, false); /* dmask */
		if (HAVE_LLVM <= 0x0309) {
			params[4] = i1false;  /* r128 */
			params[5] = da;
			params[6] = glc;
			params[7] = slc;
		} else {
			LLVMValueRef lwe = i1false;
			params[4] = glc;
			params[5] = slc;
			params[6] = lwe;
			params[7] = da;
		}

		get_image_intr_name("llvm.amdgcn.image.store",
				    LLVMTypeOf(params[0]), /* vdata */
				    LLVMTypeOf(params[1]), /* coords */
				    LLVMTypeOf(params[2]), /* rsrc */
				    intrinsic_name, sizeof(intrinsic_name));

		ac_build_intrinsic(&ctx->ac, intrinsic_name, ctx->voidt,
				   params, 8, 0);
	}

}

static LLVMValueRef visit_image_atomic(struct nir_to_llvm_context *ctx,
                                       nir_intrinsic_instr *instr)
{
	LLVMValueRef params[6];
	int param_count = 0;
	const nir_variable *var = instr->variables[0]->var;
	LLVMValueRef i1false = LLVMConstInt(ctx->i1, 0, 0);
	LLVMValueRef i1true = LLVMConstInt(ctx->i1, 1, 0);
	const char *base_name = "llvm.amdgcn.image.atomic";
	const char *atomic_name;
	LLVMValueRef coords;
	char intrinsic_name[32], coords_type[8];
	const struct glsl_type *type = glsl_without_array(var->type);

	if (ctx->stage == MESA_SHADER_FRAGMENT)
		ctx->shader_info->fs.writes_memory = true;

	params[param_count++] = get_src(ctx, instr->src[2]);
	if (instr->intrinsic == nir_intrinsic_image_atomic_comp_swap)
		params[param_count++] = get_src(ctx, instr->src[3]);

	if (glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_BUF) {
		params[param_count++] = get_sampler_desc(ctx, instr->variables[0], DESC_BUFFER);
		coords = params[param_count++] = LLVMBuildExtractElement(ctx->builder, get_src(ctx, instr->src[0]),
									LLVMConstInt(ctx->i32, 0, false), ""); /* vindex */
		params[param_count++] = ctx->i32zero; /* voffset */
		params[param_count++] = i1false;  /* glc */
		params[param_count++] = i1false;  /* slc */
	} else {
		bool da = glsl_sampler_type_is_array(type) ||
		          glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_CUBE;

		coords = params[param_count++] = get_image_coords(ctx, instr);
		params[param_count++] = get_sampler_desc(ctx, instr->variables[0], DESC_IMAGE);
		params[param_count++] = i1false; /* r128 */
		params[param_count++] = da ? i1true : i1false;      /* da */
		params[param_count++] = i1false;  /* slc */
	}

	switch (instr->intrinsic) {
	case nir_intrinsic_image_atomic_add:
		atomic_name = "add";
		break;
	case nir_intrinsic_image_atomic_min:
		atomic_name = "smin";
		break;
	case nir_intrinsic_image_atomic_max:
		atomic_name = "smax";
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
	build_int_type_name(LLVMTypeOf(coords),
			    coords_type, sizeof(coords_type));

	snprintf(intrinsic_name, sizeof(intrinsic_name),
			 "%s.%s.%s", base_name, atomic_name, coords_type);
	return ac_build_intrinsic(&ctx->ac, intrinsic_name, ctx->i32, params, param_count, 0);
}

static LLVMValueRef visit_image_size(struct nir_to_llvm_context *ctx,
				     nir_intrinsic_instr *instr)
{
	LLVMValueRef res;
	LLVMValueRef params[10];
	const nir_variable *var = instr->variables[0]->var;
	const struct glsl_type *type = instr->variables[0]->var->type;
	bool da = glsl_sampler_type_is_array(var->type) ||
	          glsl_get_sampler_dim(var->type) == GLSL_SAMPLER_DIM_CUBE;
	if(instr->variables[0]->deref.child)
		type = instr->variables[0]->deref.child->type;

	if (glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_BUF)
		return get_buffer_size(ctx, get_sampler_desc(ctx, instr->variables[0], DESC_BUFFER), true);
	params[0] = ctx->i32zero;
	params[1] = get_sampler_desc(ctx, instr->variables[0], DESC_IMAGE);
	params[2] = LLVMConstInt(ctx->i32, 15, false);
	params[3] = ctx->i32zero;
	params[4] = ctx->i32zero;
	params[5] = da ? ctx->i32one : ctx->i32zero;
	params[6] = ctx->i32zero;
	params[7] = ctx->i32zero;
	params[8] = ctx->i32zero;
	params[9] = ctx->i32zero;

	res = ac_build_intrinsic(&ctx->ac, "llvm.SI.getresinfo.i32", ctx->v4i32,
				 params, 10,
				 AC_FUNC_ATTR_READNONE |
				 AC_FUNC_ATTR_LEGACY);

	if (glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_CUBE &&
	    glsl_sampler_type_is_array(type)) {
		LLVMValueRef two = LLVMConstInt(ctx->i32, 2, false);
		LLVMValueRef six = LLVMConstInt(ctx->i32, 6, false);
		LLVMValueRef z = LLVMBuildExtractElement(ctx->builder, res, two, "");
		z = LLVMBuildSDiv(ctx->builder, z, six, "");
		res = LLVMBuildInsertElement(ctx->builder, res, z, two, "");
	}
	return res;
}

static void emit_waitcnt(struct nir_to_llvm_context *ctx)
{
	LLVMValueRef args[1] = {
		LLVMConstInt(ctx->i32, 0xf70, false),
	};
	ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.s.waitcnt",
			   ctx->voidt, args, 1, 0);
}

static void emit_barrier(struct nir_to_llvm_context *ctx)
{
	// TODO tess
	ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.s.barrier",
			   ctx->voidt, NULL, 0, 0);
}

static void emit_discard_if(struct nir_to_llvm_context *ctx,
			    nir_intrinsic_instr *instr)
{
	LLVMValueRef cond;
	ctx->shader_info->fs.can_discard = true;

	cond = LLVMBuildICmp(ctx->builder, LLVMIntNE,
			     get_src(ctx, instr->src[0]),
			     ctx->i32zero, "");

	cond = LLVMBuildSelect(ctx->builder, cond,
			       LLVMConstReal(ctx->f32, -1.0f),
			       ctx->f32zero, "");
	ac_build_kill(&ctx->ac, cond);
}

static LLVMValueRef
visit_load_local_invocation_index(struct nir_to_llvm_context *ctx)
{
	LLVMValueRef result;
	LLVMValueRef thread_id = ac_get_thread_id(&ctx->ac);
	result = LLVMBuildAnd(ctx->builder, ctx->tg_size,
			      LLVMConstInt(ctx->i32, 0xfc0, false), "");

	return LLVMBuildAdd(ctx->builder, result, thread_id, "");
}

static LLVMValueRef visit_var_atomic(struct nir_to_llvm_context *ctx,
				     nir_intrinsic_instr *instr)
{
	LLVMValueRef ptr, result;
	int idx = instr->variables[0]->var->data.driver_location;
	LLVMValueRef src = get_src(ctx, instr->src[0]);
	ptr = get_shared_memory_ptr(ctx, idx, ctx->i32);

	if (instr->intrinsic == nir_intrinsic_var_atomic_comp_swap) {
		LLVMValueRef src1 = get_src(ctx, instr->src[1]);
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

		result = LLVMBuildAtomicRMW(ctx->builder, op, ptr, to_integer(ctx, src),
					    LLVMAtomicOrderingSequentiallyConsistent,
					    false);
	}
	return result;
}

#define INTERP_CENTER 0
#define INTERP_CENTROID 1
#define INTERP_SAMPLE 2

static LLVMValueRef lookup_interp_param(struct nir_to_llvm_context *ctx,
					enum glsl_interp_mode interp, unsigned location)
{
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

static LLVMValueRef load_sample_position(struct nir_to_llvm_context *ctx,
					 LLVMValueRef sample_id)
{
	/* offset = sample_id * 8  (8 = 2 floats containing samplepos.xy) */
	LLVMValueRef offset0 = LLVMBuildMul(ctx->builder, sample_id, LLVMConstInt(ctx->i32, 8, false), "");
	LLVMValueRef offset1 = LLVMBuildAdd(ctx->builder, offset0, LLVMConstInt(ctx->i32, 4, false), "");
	LLVMValueRef result[2];

	result[0] = ac_build_indexed_load_const(&ctx->ac, ctx->sample_positions, offset0);
	result[1] = ac_build_indexed_load_const(&ctx->ac, ctx->sample_positions, offset1);

	return ac_build_gather_values(&ctx->ac, result, 2);
}

static LLVMValueRef load_sample_pos(struct nir_to_llvm_context *ctx)
{
	LLVMValueRef values[2];

	values[0] = emit_ffract(ctx, ctx->frag_pos[0]);
	values[1] = emit_ffract(ctx, ctx->frag_pos[1]);
	return ac_build_gather_values(&ctx->ac, values, 2);
}

static LLVMValueRef visit_interp(struct nir_to_llvm_context *ctx,
				 nir_intrinsic_instr *instr)
{
	LLVMValueRef result[2];
	LLVMValueRef interp_param, attr_number;
	unsigned location;
	unsigned chan;
	LLVMValueRef src_c0, src_c1;
	LLVMValueRef src0;
	int input_index = instr->variables[0]->var->data.location - VARYING_SLOT_VAR0;
	switch (instr->intrinsic) {
	case nir_intrinsic_interp_var_at_centroid:
		location = INTERP_CENTROID;
		break;
	case nir_intrinsic_interp_var_at_sample:
		location = INTERP_SAMPLE;
		src0 = get_src(ctx, instr->src[0]);
		break;
	case nir_intrinsic_interp_var_at_offset:
		location = INTERP_CENTER;
		src0 = get_src(ctx, instr->src[0]);
	default:
		break;
	}

	if (instr->intrinsic == nir_intrinsic_interp_var_at_offset) {
		src_c0 = to_float(ctx, LLVMBuildExtractElement(ctx->builder, src0, ctx->i32zero, ""));
		src_c1 = to_float(ctx, LLVMBuildExtractElement(ctx->builder, src0, ctx->i32one, ""));
	} else if (instr->intrinsic == nir_intrinsic_interp_var_at_sample) {
		LLVMValueRef sample_position;
		LLVMValueRef halfval = LLVMConstReal(ctx->f32, 0.5f);

		/* fetch sample ID */
		sample_position = load_sample_position(ctx, src0);

		src_c0 = LLVMBuildExtractElement(ctx->builder, sample_position, ctx->i32zero, "");
		src_c0 = LLVMBuildFSub(ctx->builder, src_c0, halfval, "");
		src_c1 = LLVMBuildExtractElement(ctx->builder, sample_position, ctx->i32one, "");
		src_c1 = LLVMBuildFSub(ctx->builder, src_c1, halfval, "");
	}
	interp_param = lookup_interp_param(ctx, instr->variables[0]->var->data.interpolation, location);
	attr_number = LLVMConstInt(ctx->i32, input_index, false);

	if (location == INTERP_SAMPLE || location == INTERP_CENTER) {
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
			LLVMValueRef ix_ll = LLVMConstInt(ctx->i32, i, false);
			LLVMValueRef iy_ll = LLVMConstInt(ctx->i32, i + 2, false);
			LLVMValueRef ddx_el = LLVMBuildExtractElement(ctx->builder,
								      ddxy_out, ix_ll, "");
			LLVMValueRef ddy_el = LLVMBuildExtractElement(ctx->builder,
								      ddxy_out, iy_ll, "");
			LLVMValueRef interp_el = LLVMBuildExtractElement(ctx->builder,
									 interp_param, ix_ll, "");
			LLVMValueRef temp1, temp2;

			interp_el = LLVMBuildBitCast(ctx->builder, interp_el,
						     ctx->f32, "");

			temp1 = LLVMBuildFMul(ctx->builder, ddx_el, src_c0, "");
			temp1 = LLVMBuildFAdd(ctx->builder, temp1, interp_el, "");

			temp2 = LLVMBuildFMul(ctx->builder, ddy_el, src_c1, "");
			temp2 = LLVMBuildFAdd(ctx->builder, temp2, temp1, "");

			ij_out[i] = LLVMBuildBitCast(ctx->builder,
						     temp2, ctx->i32, "");
		}
		interp_param = ac_build_gather_values(&ctx->ac, ij_out, 2);

	}

	for (chan = 0; chan < 2; chan++) {
		LLVMValueRef llvm_chan = LLVMConstInt(ctx->i32, chan, false);

		if (interp_param) {
			interp_param = LLVMBuildBitCast(ctx->builder,
							interp_param, LLVMVectorType(ctx->f32, 2), "");
			LLVMValueRef i = LLVMBuildExtractElement(
				ctx->builder, interp_param, ctx->i32zero, "");
			LLVMValueRef j = LLVMBuildExtractElement(
				ctx->builder, interp_param, ctx->i32one, "");

			result[chan] = ac_build_fs_interp(&ctx->ac,
							  llvm_chan, attr_number,
							  ctx->prim_mask, i, j);
		} else {
			result[chan] = ac_build_fs_interp_mov(&ctx->ac,
							      LLVMConstInt(ctx->i32, 2, false),
							      llvm_chan, attr_number,
							      ctx->prim_mask);
		}
	}
	return ac_build_gather_values(&ctx->ac, result, 2);
}

static void
visit_emit_vertex(struct nir_to_llvm_context *ctx,
		  nir_intrinsic_instr *instr)
{
	LLVMValueRef gs_next_vertex;
	LLVMValueRef can_emit, kill;
	int idx;
	int clip_cull_slot = -1;
	assert(instr->const_index[0] == 0);
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
				 LLVMConstInt(ctx->i32, ctx->gs_max_out_vertices, false), "");

	kill = LLVMBuildSelect(ctx->builder, can_emit,
			       LLVMConstReal(ctx->f32, 1.0f),
			       LLVMConstReal(ctx->f32, -1.0f), "");
	ac_build_kill(&ctx->ac, kill);

	/* loop num outputs */
	idx = 0;
	for (unsigned i = 0; i < RADEON_LLVM_MAX_OUTPUTS; ++i) {
		LLVMValueRef *out_ptr = &ctx->outputs[i * 4];
		int length = 4;
		int start = 0;
		int slot = idx;
		int slot_inc = 1;

		if (!(ctx->output_mask & (1ull << i)))
			continue;

		if (i == VARYING_SLOT_CLIP_DIST1 ||
		    i == VARYING_SLOT_CULL_DIST1)
			continue;

		if (i == VARYING_SLOT_CLIP_DIST0 ||
		    i == VARYING_SLOT_CULL_DIST0) {
			/* pack clip and cull into a single set of slots */
			if (clip_cull_slot == -1) {
				clip_cull_slot = idx;
				if (ctx->num_output_clips + ctx->num_output_culls > 4)
					slot_inc = 2;
			} else {
				slot = clip_cull_slot;
				slot_inc = 0;
			}
			if (i == VARYING_SLOT_CLIP_DIST0)
				length = ctx->num_output_clips;
			if (i == VARYING_SLOT_CULL_DIST0) {
				start = ctx->num_output_clips;
				length = ctx->num_output_culls;
			}
		}
		for (unsigned j = 0; j < length; j++) {
			LLVMValueRef out_val = LLVMBuildLoad(ctx->builder,
							     out_ptr[j], "");
			LLVMValueRef voffset = LLVMConstInt(ctx->i32, (slot * 4 + j + start) * ctx->gs_max_out_vertices, false);
			voffset = LLVMBuildAdd(ctx->builder, voffset, gs_next_vertex, "");
			voffset = LLVMBuildMul(ctx->builder, voffset, LLVMConstInt(ctx->i32, 4, false), "");

			out_val = LLVMBuildBitCast(ctx->builder, out_val, ctx->i32, "");

			ac_build_buffer_store_dword(&ctx->ac, ctx->gsvs_ring,
						    out_val, 1,
						    voffset, ctx->gs2vs_offset, 0,
						    1, 1, true, true);
		}
		idx += slot_inc;
	}

	gs_next_vertex = LLVMBuildAdd(ctx->builder, gs_next_vertex,
				      ctx->i32one, "");
	LLVMBuildStore(ctx->builder, gs_next_vertex, ctx->gs_next_vertex);

	ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_EMIT | AC_SENDMSG_GS | (0 << 8), ctx->gs_wave_id);
}

static void
visit_end_primitive(struct nir_to_llvm_context *ctx,
		    nir_intrinsic_instr *instr)
{
	ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_CUT | AC_SENDMSG_GS | (0 << 8), ctx->gs_wave_id);
}

static void visit_intrinsic(struct nir_to_llvm_context *ctx,
                            nir_intrinsic_instr *instr)
{
	LLVMValueRef result = NULL;

	switch (instr->intrinsic) {
	case nir_intrinsic_load_work_group_id: {
		result = ctx->workgroup_ids;
		break;
	}
	case nir_intrinsic_load_base_vertex: {
		result = ctx->base_vertex;
		break;
	}
	case nir_intrinsic_load_vertex_id_zero_base: {
		result = ctx->vertex_id;
		break;
	}
	case nir_intrinsic_load_local_invocation_id: {
		result = ctx->local_invocation_ids;
		break;
	}
	case nir_intrinsic_load_base_instance:
		result = ctx->start_instance;
		break;
	case nir_intrinsic_load_draw_id:
		result = ctx->draw_index;
		break;
	case nir_intrinsic_load_invocation_id:
		result = ctx->gs_invocation_id;
		break;
	case nir_intrinsic_load_primitive_id:
		if (ctx->stage == MESA_SHADER_GEOMETRY)
			result = ctx->gs_prim_id;
		else
			fprintf(stderr, "Unknown primitive id intrinsic: %d", ctx->stage);
		break;
	case nir_intrinsic_load_sample_id:
		ctx->shader_info->fs.force_persample = true;
		result = unpack_param(ctx, ctx->ancillary, 8, 4);
		break;
	case nir_intrinsic_load_sample_pos:
		ctx->shader_info->fs.force_persample = true;
		result = load_sample_pos(ctx);
		break;
	case nir_intrinsic_load_sample_mask_in:
		result = ctx->sample_coverage;
		break;
	case nir_intrinsic_load_front_face:
		result = ctx->front_face;
		break;
	case nir_intrinsic_load_instance_id:
		result = ctx->instance_id;
		ctx->shader_info->vs.vgpr_comp_cnt = MAX2(3,
		                            ctx->shader_info->vs.vgpr_comp_cnt);
		break;
	case nir_intrinsic_load_num_work_groups:
		result = ctx->num_work_groups;
		break;
	case nir_intrinsic_load_local_invocation_index:
		result = visit_load_local_invocation_index(ctx);
		break;
	case nir_intrinsic_load_push_constant:
		result = visit_load_push_constant(ctx, instr);
		break;
	case nir_intrinsic_vulkan_resource_index:
		result = visit_vulkan_resource_index(ctx, instr);
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
		ctx->shader_info->fs.can_discard = true;
		ac_build_intrinsic(&ctx->ac, "llvm.AMDGPU.kilp",
				   ctx->voidt,
				   NULL, 0, AC_FUNC_ATTR_LEGACY);
		break;
	case nir_intrinsic_discard_if:
		emit_discard_if(ctx, instr);
		break;
	case nir_intrinsic_memory_barrier:
		emit_waitcnt(ctx);
		break;
	case nir_intrinsic_barrier:
		emit_barrier(ctx);
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
		result = visit_var_atomic(ctx, instr);
		break;
	case nir_intrinsic_interp_var_at_centroid:
	case nir_intrinsic_interp_var_at_sample:
	case nir_intrinsic_interp_var_at_offset:
		result = visit_interp(ctx, instr);
		break;
	case nir_intrinsic_emit_vertex:
		visit_emit_vertex(ctx, instr);
		break;
	case nir_intrinsic_end_primitive:
		visit_end_primitive(ctx, instr);
		break;
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

static LLVMValueRef get_sampler_desc(struct nir_to_llvm_context *ctx,
					  nir_deref_var *deref,
					  enum desc_type desc_type)
{
	unsigned desc_set = deref->var->data.descriptor_set;
	LLVMValueRef list = ctx->descriptor_sets[desc_set];
	struct radv_descriptor_set_layout *layout = ctx->options->layout->set[desc_set].layout;
	struct radv_descriptor_set_binding_layout *binding = layout->binding + deref->var->data.binding;
	unsigned offset = binding->offset;
	unsigned stride = binding->size;
	unsigned type_size;
	LLVMBuilderRef builder = ctx->builder;
	LLVMTypeRef type;
	LLVMValueRef index = NULL;
	unsigned constant_index = 0;

	assert(deref->var->data.binding < layout->binding_count);

	switch (desc_type) {
	case DESC_IMAGE:
		type = ctx->v8i32;
		type_size = 32;
		break;
	case DESC_FMASK:
		type = ctx->v8i32;
		offset += 32;
		type_size = 32;
		break;
	case DESC_SAMPLER:
		type = ctx->v4i32;
		if (binding->type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
			offset += 64;

		type_size = 16;
		break;
	case DESC_BUFFER:
		type = ctx->v4i32;
		type_size = 16;
		break;
	default:
		unreachable("invalid desc_type\n");
	}

	if (deref->deref.child) {
		nir_deref_array *child = (nir_deref_array*)deref->deref.child;

		assert(child->deref_array_type != nir_deref_array_type_wildcard);
		offset += child->base_offset * stride;
		if (child->deref_array_type == nir_deref_array_type_indirect) {
			index = get_src(ctx, child->indirect);
		}

		constant_index = child->base_offset;
	}
	if (desc_type == DESC_SAMPLER && binding->immutable_samplers &&
	    (!index || binding->immutable_samplers_equal)) {
		if (binding->immutable_samplers_equal)
			constant_index = 0;

		LLVMValueRef constants[] = {
			LLVMConstInt(ctx->i32, binding->immutable_samplers[constant_index * 4 + 0], 0),
			LLVMConstInt(ctx->i32, binding->immutable_samplers[constant_index * 4 + 1], 0),
			LLVMConstInt(ctx->i32, binding->immutable_samplers[constant_index * 4 + 2], 0),
			LLVMConstInt(ctx->i32, binding->immutable_samplers[constant_index * 4 + 3], 0),
		};
		return ac_build_gather_values(&ctx->ac, constants, 4);
	}

	assert(stride % type_size == 0);

	if (!index)
		index = ctx->i32zero;

	index = LLVMBuildMul(builder, index, LLVMConstInt(ctx->i32, stride / type_size, 0), "");

	list = ac_build_gep0(&ctx->ac, list, LLVMConstInt(ctx->i32, offset, 0));
	list = LLVMBuildPointerCast(builder, list, const_array(type, 0), "");

	return ac_build_indexed_load_const(&ctx->ac, list, index);
}

static void set_tex_fetch_args(struct nir_to_llvm_context *ctx,
			       struct ac_tex_info *tinfo,
			       nir_tex_instr *instr,
			       nir_texop op,
			       LLVMValueRef res_ptr, LLVMValueRef samp_ptr,
			       LLVMValueRef *param, unsigned count,
			       unsigned dmask)
{
	int num_args;
	unsigned is_rect = 0;
	bool da = instr->is_array || instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE;

	if (op == nir_texop_lod)
		da = false;
	/* Pad to power of two vector */
	while (count < util_next_power_of_two(count))
		param[count++] = LLVMGetUndef(ctx->i32);

	if (count > 1)
		tinfo->args[0] = ac_build_gather_values(&ctx->ac, param, count);
	else
		tinfo->args[0] = param[0];

	tinfo->args[1] = res_ptr;
	num_args = 2;

	if (op == nir_texop_txf ||
	    op == nir_texop_txf_ms ||
	    op == nir_texop_query_levels ||
	    op == nir_texop_texture_samples ||
	    op == nir_texop_txs)
		tinfo->dst_type = ctx->v4i32;
	else {
		tinfo->dst_type = ctx->v4f32;
		tinfo->args[num_args++] = samp_ptr;
	}

	if (instr->sampler_dim == GLSL_SAMPLER_DIM_BUF && op == nir_texop_txf) {
		tinfo->args[0] = res_ptr;
		tinfo->args[1] = LLVMConstInt(ctx->i32, 0, false);
		tinfo->args[2] = param[0];
		tinfo->arg_count = 3;
		return;
	}

	tinfo->args[num_args++] = LLVMConstInt(ctx->i32, dmask, 0);
	tinfo->args[num_args++] = LLVMConstInt(ctx->i32, is_rect, 0); /* unorm */
	tinfo->args[num_args++] = LLVMConstInt(ctx->i32, 0, 0); /* r128 */
	tinfo->args[num_args++] = LLVMConstInt(ctx->i32, da ? 1 : 0, 0);
	tinfo->args[num_args++] = LLVMConstInt(ctx->i32, 0, 0); /* glc */
	tinfo->args[num_args++] = LLVMConstInt(ctx->i32, 0, 0); /* slc */
	tinfo->args[num_args++] = LLVMConstInt(ctx->i32, 0, 0); /* tfe */
	tinfo->args[num_args++] = LLVMConstInt(ctx->i32, 0, 0); /* lwe */

	tinfo->arg_count = num_args;
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
static LLVMValueRef sici_fix_sampler_aniso(struct nir_to_llvm_context *ctx,
                                           LLVMValueRef res, LLVMValueRef samp)
{
	LLVMBuilderRef builder = ctx->builder;
	LLVMValueRef img7, samp0;

	if (ctx->options->chip_class >= VI)
		return samp;

	img7 = LLVMBuildExtractElement(builder, res,
	                               LLVMConstInt(ctx->i32, 7, 0), "");
	samp0 = LLVMBuildExtractElement(builder, samp,
	                                LLVMConstInt(ctx->i32, 0, 0), "");
	samp0 = LLVMBuildAnd(builder, samp0, img7, "");
	return LLVMBuildInsertElement(builder, samp, samp0,
	                              LLVMConstInt(ctx->i32, 0, 0), "");
}

static void tex_fetch_ptrs(struct nir_to_llvm_context *ctx,
			   nir_tex_instr *instr,
			   LLVMValueRef *res_ptr, LLVMValueRef *samp_ptr,
			   LLVMValueRef *fmask_ptr)
{
	if (instr->sampler_dim  == GLSL_SAMPLER_DIM_BUF)
		*res_ptr = get_sampler_desc(ctx, instr->texture, DESC_BUFFER);
	else
		*res_ptr = get_sampler_desc(ctx, instr->texture, DESC_IMAGE);
	if (samp_ptr) {
		if (instr->sampler)
			*samp_ptr = get_sampler_desc(ctx, instr->sampler, DESC_SAMPLER);
		else
			*samp_ptr = get_sampler_desc(ctx, instr->texture, DESC_SAMPLER);
		if (instr->sampler_dim < GLSL_SAMPLER_DIM_RECT)
			*samp_ptr = sici_fix_sampler_aniso(ctx, *res_ptr, *samp_ptr);
	}
	if (fmask_ptr && !instr->sampler && (instr->op == nir_texop_txf_ms ||
					     instr->op == nir_texop_samples_identical))
		*fmask_ptr = get_sampler_desc(ctx, instr->texture, DESC_FMASK);
}

static LLVMValueRef apply_round_slice(struct nir_to_llvm_context *ctx,
				      LLVMValueRef coord)
{
	coord = to_float(ctx, coord);
	coord = ac_build_intrinsic(&ctx->ac, "llvm.rint.f32", ctx->f32, &coord, 1, 0);
	coord = to_integer(ctx, coord);
	return coord;
}

static void visit_tex(struct nir_to_llvm_context *ctx, nir_tex_instr *instr)
{
	LLVMValueRef result = NULL;
	struct ac_tex_info tinfo = { 0 };
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
		case nir_tex_src_lod:
			lod = get_src(ctx, instr->src[i].src);
			break;
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
		res = LLVMBuildBitCast(ctx->builder, res_ptr, ctx->v8i32, "");
		samples = LLVMBuildExtractElement(ctx->builder, res,
						  LLVMConstInt(ctx->i32, 3, false), "");
		is_msaa = LLVMBuildLShr(ctx->builder, samples,
					LLVMConstInt(ctx->i32, 28, false), "");
		is_msaa = LLVMBuildAnd(ctx->builder, is_msaa,
				       LLVMConstInt(ctx->i32, 0xe, false), "");
		is_msaa = LLVMBuildICmp(ctx->builder, LLVMIntEQ, is_msaa,
					LLVMConstInt(ctx->i32, 0xe, false), "");

		samples = LLVMBuildLShr(ctx->builder, samples,
					LLVMConstInt(ctx->i32, 16, false), "");
		samples = LLVMBuildAnd(ctx->builder, samples,
				       LLVMConstInt(ctx->i32, 0xf, false), "");
		samples = LLVMBuildShl(ctx->builder, ctx->i32one,
				       samples, "");
		samples = LLVMBuildSelect(ctx->builder, is_msaa, samples,
					  ctx->i32one, "");
		result = samples;
		goto write_result;
	}

	if (coord)
		for (chan = 0; chan < instr->coord_components; chan++)
			coords[chan] = llvm_extract_elem(ctx, coord, chan);

	if (offsets && instr->op != nir_texop_txf) {
		LLVMValueRef offset[3], pack;
		for (chan = 0; chan < 3; ++chan)
			offset[chan] = ctx->i32zero;

		tinfo.has_offset = true;
		for (chan = 0; chan < get_llvm_num_components(offsets); chan++) {
			offset[chan] = llvm_extract_elem(ctx, offsets, chan);
			offset[chan] = LLVMBuildAnd(ctx->builder, offset[chan],
						    LLVMConstInt(ctx->i32, 0x3f, false), "");
			if (chan)
				offset[chan] = LLVMBuildShl(ctx->builder, offset[chan],
							    LLVMConstInt(ctx->i32, chan * 8, false), "");
		}
		pack = LLVMBuildOr(ctx->builder, offset[0], offset[1], "");
		pack = LLVMBuildOr(ctx->builder, pack, offset[2], "");
		address[count++] = pack;

	}
	/* pack LOD bias value */
	if (instr->op == nir_texop_txb && bias) {
		address[count++] = bias;
	}

	/* Pack depth comparison value */
	if (instr->is_shadow && comparator) {
		address[count++] = llvm_extract_elem(ctx, comparator, 0);
	}

	/* pack derivatives */
	if (ddx || ddy) {
		switch (instr->sampler_dim) {
		case GLSL_SAMPLER_DIM_3D:
		case GLSL_SAMPLER_DIM_CUBE:
			num_deriv_comp = 3;
			break;
		case GLSL_SAMPLER_DIM_2D:
		default:
			num_deriv_comp = 2;
			break;
		case GLSL_SAMPLER_DIM_1D:
			num_deriv_comp = 1;
			break;
		}

		for (unsigned i = 0; i < num_deriv_comp; i++) {
			derivs[i * 2] = to_float(ctx, llvm_extract_elem(ctx, ddx, i));
			derivs[i * 2 + 1] = to_float(ctx, llvm_extract_elem(ctx, ddy, i));
		}
	}

	if (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE && coord) {
		for (chan = 0; chan < instr->coord_components; chan++)
			coords[chan] = to_float(ctx, coords[chan]);
		if (instr->coord_components == 3)
			coords[3] = LLVMGetUndef(ctx->f32);
		ac_prepare_cube_coords(&ctx->ac,
			instr->op == nir_texop_txd, instr->is_array,
			coords, derivs);
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
				coords[1] = apply_round_slice(ctx, coords[1]);
			}
			address[count++] = coords[1];
		}
		if (instr->coord_components > 2) {
			/* This seems like a bit of a hack - but it passes Vulkan CTS with it */
			if (instr->sampler_dim != GLSL_SAMPLER_DIM_3D && instr->op != nir_texop_txf) {
				coords[2] = apply_round_slice(ctx, coords[2]);
			}
			address[count++] = coords[2];
		}
	}

	/* Pack LOD */
	if ((instr->op == nir_texop_txl || instr->op == nir_texop_txf) && lod) {
		address[count++] = lod;
	} else if (instr->op == nir_texop_txf_ms && sample_index) {
		address[count++] = sample_index;
	} else if(instr->op == nir_texop_txs) {
		count = 0;
		if (lod)
			address[count++] = lod;
		else
			address[count++] = ctx->i32zero;
	}

	for (chan = 0; chan < count; chan++) {
		address[chan] = LLVMBuildBitCast(ctx->builder,
						 address[chan], ctx->i32, "");
	}

	if (instr->op == nir_texop_samples_identical) {
		LLVMValueRef txf_address[4];
		struct ac_tex_info txf_info = { 0 };
		unsigned txf_count = count;
		memcpy(txf_address, address, sizeof(txf_address));

		if (!instr->is_array)
			txf_address[2] = ctx->i32zero;
		txf_address[3] = ctx->i32zero;

		set_tex_fetch_args(ctx, &txf_info, instr, nir_texop_txf,
				   fmask_ptr, NULL,
				   txf_address, txf_count, 0xf);

		result = build_tex_intrinsic(ctx, instr, &txf_info);

		result = LLVMBuildExtractElement(ctx->builder, result, ctx->i32zero, "");
		result = emit_int_cmp(ctx, LLVMIntEQ, result, ctx->i32zero);
		goto write_result;
	}

	if (instr->sampler_dim == GLSL_SAMPLER_DIM_MS &&
	    instr->op != nir_texop_txs) {
		unsigned sample_chan = instr->is_array ? 3 : 2;
		address[sample_chan] = adjust_sample_index_using_fmask(ctx,
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
			address[2] = LLVMBuildAdd(ctx->builder,
						  address[2], LLVMConstInt(ctx->i32, const_offset->i32[2], false), "");
		if (num_offsets > 1)
			address[1] = LLVMBuildAdd(ctx->builder,
						  address[1], LLVMConstInt(ctx->i32, const_offset->i32[1], false), "");
		address[0] = LLVMBuildAdd(ctx->builder,
					  address[0], LLVMConstInt(ctx->i32, const_offset->i32[0], false), "");

	}

	/* TODO TG4 support */
	if (instr->op == nir_texop_tg4) {
		if (instr->is_shadow)
			dmask = 1;
		else
			dmask = 1 << instr->component;
	}
	set_tex_fetch_args(ctx, &tinfo, instr, instr->op,
			   res_ptr, samp_ptr, address, count, dmask);

	result = build_tex_intrinsic(ctx, instr, &tinfo);

	if (instr->op == nir_texop_query_levels)
		result = LLVMBuildExtractElement(ctx->builder, result, LLVMConstInt(ctx->i32, 3, false), "");
	else if (instr->is_shadow && instr->op != nir_texop_txs && instr->op != nir_texop_lod && instr->op != nir_texop_tg4)
		result = LLVMBuildExtractElement(ctx->builder, result, ctx->i32zero, "");
	else if (instr->op == nir_texop_txs &&
		 instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE &&
		 instr->is_array) {
		LLVMValueRef two = LLVMConstInt(ctx->i32, 2, false);
		LLVMValueRef six = LLVMConstInt(ctx->i32, 6, false);
		LLVMValueRef z = LLVMBuildExtractElement(ctx->builder, result, two, "");
		z = LLVMBuildSDiv(ctx->builder, z, six, "");
		result = LLVMBuildInsertElement(ctx->builder, result, z, two, "");
	} else if (instr->dest.ssa.num_components != 4)
		result = trim_vector(ctx, result, instr->dest.ssa.num_components);

write_result:
	if (result) {
		assert(instr->dest.is_ssa);
		result = to_integer(ctx, result);
		_mesa_hash_table_insert(ctx->defs, &instr->dest.ssa, result);
	}
}


static void visit_phi(struct nir_to_llvm_context *ctx, nir_phi_instr *instr)
{
	LLVMTypeRef type = get_def_type(ctx, &instr->dest.ssa);
	LLVMValueRef result = LLVMBuildPhi(ctx->builder, type, "");

	_mesa_hash_table_insert(ctx->defs, &instr->dest.ssa, result);
	_mesa_hash_table_insert(ctx->phis, instr, result);
}

static void visit_post_phi(struct nir_to_llvm_context *ctx,
                           nir_phi_instr *instr,
                           LLVMValueRef llvm_phi)
{
	nir_foreach_phi_src(src, instr) {
		LLVMBasicBlockRef block = get_block(ctx, src->pred);
		LLVMValueRef llvm_src = get_src(ctx, src->src);

		LLVMAddIncoming(llvm_phi, &llvm_src, &block, 1);
	}
}

static void phi_post_pass(struct nir_to_llvm_context *ctx)
{
	struct hash_entry *entry;
	hash_table_foreach(ctx->phis, entry) {
		visit_post_phi(ctx, (nir_phi_instr*)entry->key,
		               (LLVMValueRef)entry->data);
	}
}


static void visit_ssa_undef(struct nir_to_llvm_context *ctx,
			    nir_ssa_undef_instr *instr)
{
	unsigned num_components = instr->def.num_components;
	LLVMValueRef undef;

	if (num_components == 1)
		undef = LLVMGetUndef(ctx->i32);
	else {
		undef = LLVMGetUndef(LLVMVectorType(ctx->i32, num_components));
	}
	_mesa_hash_table_insert(ctx->defs, &instr->def, undef);
}

static void visit_jump(struct nir_to_llvm_context *ctx,
		       nir_jump_instr *instr)
{
	switch (instr->type) {
	case nir_jump_break:
		LLVMBuildBr(ctx->builder, ctx->break_block);
		LLVMClearInsertionPosition(ctx->builder);
		break;
	case nir_jump_continue:
		LLVMBuildBr(ctx->builder, ctx->continue_block);
		LLVMClearInsertionPosition(ctx->builder);
		break;
	default:
		fprintf(stderr, "Unknown NIR jump instr: ");
		nir_print_instr(&instr->instr, stderr);
		fprintf(stderr, "\n");
		abort();
	}
}

static void visit_cf_list(struct nir_to_llvm_context *ctx,
                          struct exec_list *list);

static void visit_block(struct nir_to_llvm_context *ctx, nir_block *block)
{
	LLVMBasicBlockRef llvm_block = LLVMGetInsertBlock(ctx->builder);
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

static void visit_if(struct nir_to_llvm_context *ctx, nir_if *if_stmt)
{
	LLVMValueRef value = get_src(ctx, if_stmt->condition);

	LLVMBasicBlockRef merge_block =
	    LLVMAppendBasicBlockInContext(ctx->context, ctx->main_function, "");
	LLVMBasicBlockRef if_block =
	    LLVMAppendBasicBlockInContext(ctx->context, ctx->main_function, "");
	LLVMBasicBlockRef else_block = merge_block;
	if (!exec_list_is_empty(&if_stmt->else_list))
		else_block = LLVMAppendBasicBlockInContext(
		    ctx->context, ctx->main_function, "");

	LLVMValueRef cond = LLVMBuildICmp(ctx->builder, LLVMIntNE, value,
	                                  LLVMConstInt(ctx->i32, 0, false), "");
	LLVMBuildCondBr(ctx->builder, cond, if_block, else_block);

	LLVMPositionBuilderAtEnd(ctx->builder, if_block);
	visit_cf_list(ctx, &if_stmt->then_list);
	if (LLVMGetInsertBlock(ctx->builder))
		LLVMBuildBr(ctx->builder, merge_block);

	if (!exec_list_is_empty(&if_stmt->else_list)) {
		LLVMPositionBuilderAtEnd(ctx->builder, else_block);
		visit_cf_list(ctx, &if_stmt->else_list);
		if (LLVMGetInsertBlock(ctx->builder))
			LLVMBuildBr(ctx->builder, merge_block);
	}

	LLVMPositionBuilderAtEnd(ctx->builder, merge_block);
}

static void visit_loop(struct nir_to_llvm_context *ctx, nir_loop *loop)
{
	LLVMBasicBlockRef continue_parent = ctx->continue_block;
	LLVMBasicBlockRef break_parent = ctx->break_block;

	ctx->continue_block =
	    LLVMAppendBasicBlockInContext(ctx->context, ctx->main_function, "");
	ctx->break_block =
	    LLVMAppendBasicBlockInContext(ctx->context, ctx->main_function, "");

	LLVMBuildBr(ctx->builder, ctx->continue_block);
	LLVMPositionBuilderAtEnd(ctx->builder, ctx->continue_block);
	visit_cf_list(ctx, &loop->body);

	if (LLVMGetInsertBlock(ctx->builder))
		LLVMBuildBr(ctx->builder, ctx->continue_block);
	LLVMPositionBuilderAtEnd(ctx->builder, ctx->break_block);

	ctx->continue_block = continue_parent;
	ctx->break_block = break_parent;
}

static void visit_cf_list(struct nir_to_llvm_context *ctx,
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
	LLVMValueRef args[3];
	LLVMValueRef input;
	LLVMValueRef buffer_index;
	int index = variable->data.location - VERT_ATTRIB_GENERIC0;
	int idx = variable->data.location;
	unsigned attrib_count = glsl_count_attribute_slots(variable->type, true);

	variable->data.driver_location = idx * 4;

	if (ctx->options->key.vs.instance_rate_inputs & (1u << index)) {
		buffer_index = LLVMBuildAdd(ctx->builder, ctx->instance_id,
					    ctx->start_instance, "");
		ctx->shader_info->vs.vgpr_comp_cnt = MAX2(3,
		                            ctx->shader_info->vs.vgpr_comp_cnt);
	} else
		buffer_index = LLVMBuildAdd(ctx->builder, ctx->vertex_id,
					    ctx->base_vertex, "");

	for (unsigned i = 0; i < attrib_count; ++i, ++idx) {
		t_offset = LLVMConstInt(ctx->i32, index + i, false);

		t_list = ac_build_indexed_load_const(&ctx->ac, t_list_ptr, t_offset);
		args[0] = t_list;
		args[1] = LLVMConstInt(ctx->i32, 0, false);
		args[2] = buffer_index;
		input = ac_build_intrinsic(&ctx->ac,
			"llvm.SI.vs.load.input", ctx->v4f32, args, 3,
			AC_FUNC_ATTR_READNONE | AC_FUNC_ATTR_NOUNWIND |
			AC_FUNC_ATTR_LEGACY);

		for (unsigned chan = 0; chan < 4; chan++) {
			LLVMValueRef llvm_chan = LLVMConstInt(ctx->i32, chan, false);
			ctx->inputs[radeon_llvm_reg_index_soa(idx, chan)] =
				to_integer(ctx, LLVMBuildExtractElement(ctx->builder,
							input, llvm_chan, ""));
		}
	}
}

static void
handle_gs_input_decl(struct nir_to_llvm_context *ctx,
		     struct nir_variable *variable)
{
	int idx = variable->data.location;

	if (idx == VARYING_SLOT_CLIP_DIST0 ||
	    idx == VARYING_SLOT_CULL_DIST0) {
		int length = glsl_get_length(glsl_get_array_element(variable->type));
		if (idx == VARYING_SLOT_CLIP_DIST0)
			ctx->num_input_clips = length;
		else
			ctx->num_input_culls = length;
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

	attr_number = LLVMConstInt(ctx->i32, attr, false);

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
						LLVMVectorType(ctx->f32, 2), "");

		i = LLVMBuildExtractElement(ctx->builder, interp_param,
						ctx->i32zero, "");
		j = LLVMBuildExtractElement(ctx->builder, interp_param,
						ctx->i32one, "");
	}

	for (chan = 0; chan < 4; chan++) {
		LLVMValueRef llvm_chan = LLVMConstInt(ctx->i32, chan, false);

		if (interp) {
			result[chan] = ac_build_fs_interp(&ctx->ac,
							  llvm_chan,
							  attr_number,
							  prim_mask, i, j);
		} else {
			result[chan] = ac_build_fs_interp_mov(&ctx->ac,
							      LLVMConstInt(ctx->i32, 2, false),
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
			ctx->shader_info->fs.force_persample = true;
		} else if (variable->data.centroid)
			interp_type = INTERP_CENTROID;
		else
			interp_type = INTERP_CENTER;

		interp = lookup_interp_param(ctx, variable->data.interpolation, interp_type);
	} else
		interp = NULL;

	for (unsigned i = 0; i < attrib_count; ++i)
		ctx->inputs[radeon_llvm_reg_index_soa(idx + i, 0)] = interp;

}

static void
handle_shader_input_decl(struct nir_to_llvm_context *ctx,
			 struct nir_variable *variable)
{
	switch (ctx->stage) {
	case MESA_SHADER_VERTEX:
		handle_vs_input_decl(ctx, variable);
		break;
	case MESA_SHADER_FRAGMENT:
		handle_fs_input_decl(ctx, variable);
		break;
	case MESA_SHADER_GEOMETRY:
		handle_gs_input_decl(ctx, variable);
		break;
	default:
		break;
	}

}

static void
handle_fs_inputs_pre(struct nir_to_llvm_context *ctx,
		     struct nir_shader *nir)
{
	unsigned index = 0;
	for (unsigned i = 0; i < RADEON_LLVM_MAX_INPUTS; ++i) {
		LLVMValueRef interp_param;
		LLVMValueRef *inputs = ctx->inputs +radeon_llvm_reg_index_soa(i, 0);

		if (!(ctx->input_mask & (1ull << i)))
			continue;

		if (i >= VARYING_SLOT_VAR0 || i == VARYING_SLOT_PNTC ||
		    i == VARYING_SLOT_PRIMITIVE_ID || i == VARYING_SLOT_LAYER) {
			interp_param = *inputs;
			interp_fs_input(ctx, index, interp_param, ctx->prim_mask,
					inputs);

			if (!interp_param)
				ctx->shader_info->fs.flat_shaded_mask |= 1u << index;
			++index;
		} else if (i == VARYING_SLOT_POS) {
			for(int i = 0; i < 3; ++i)
				inputs[i] = ctx->frag_pos[i];

			inputs[3] = ac_build_fdiv(&ctx->ac, ctx->f32one, ctx->frag_pos[3]);
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
}

static LLVMValueRef
ac_build_alloca(struct nir_to_llvm_context *ctx,
                LLVMTypeRef type,
                const char *name)
{
	LLVMBuilderRef builder = ctx->builder;
	LLVMBasicBlockRef current_block = LLVMGetInsertBlock(builder);
	LLVMValueRef function = LLVMGetBasicBlockParent(current_block);
	LLVMBasicBlockRef first_block = LLVMGetEntryBasicBlock(function);
	LLVMValueRef first_instr = LLVMGetFirstInstruction(first_block);
	LLVMBuilderRef first_builder = LLVMCreateBuilderInContext(ctx->context);
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

static LLVMValueRef si_build_alloca_undef(struct nir_to_llvm_context *ctx,
					  LLVMTypeRef type,
					  const char *name)
{
	LLVMValueRef ptr = ac_build_alloca(ctx, type, name);
	LLVMBuildStore(ctx->builder, LLVMGetUndef(type), ptr);
	return ptr;
}

static void
handle_shader_output_decl(struct nir_to_llvm_context *ctx,
			  struct nir_variable *variable)
{
	int idx = variable->data.location + variable->data.index;
	unsigned attrib_count = glsl_count_attribute_slots(variable->type, false);

	variable->data.driver_location = idx * 4;

	if (ctx->stage == MESA_SHADER_VERTEX ||
	    ctx->stage == MESA_SHADER_GEOMETRY) {
		if (idx == VARYING_SLOT_CLIP_DIST0 ||
		    idx == VARYING_SLOT_CULL_DIST0) {
			int length = glsl_get_length(variable->type);
			if (idx == VARYING_SLOT_CLIP_DIST0) {
				if (ctx->stage == MESA_SHADER_VERTEX)
					ctx->shader_info->vs.clip_dist_mask = (1 << length) - 1;
				ctx->num_output_clips = length;
			} else if (idx == VARYING_SLOT_CULL_DIST0) {
				if (ctx->stage == MESA_SHADER_VERTEX)
					ctx->shader_info->vs.cull_dist_mask = (1 << length) - 1;
				ctx->num_output_culls = length;
			}
			if (length > 4)
				attrib_count = 2;
			else
				attrib_count = 1;
		}
	}

	for (unsigned i = 0; i < attrib_count; ++i) {
		for (unsigned chan = 0; chan < 4; chan++) {
			ctx->outputs[radeon_llvm_reg_index_soa(idx + i, chan)] =
		                       si_build_alloca_undef(ctx, ctx->f32, "");
		}
	}
	ctx->output_mask |= ((1ull << attrib_count) - 1) << idx;
}

static void
setup_locals(struct nir_to_llvm_context *ctx,
	     struct nir_function *func)
{
	int i, j;
	ctx->num_locals = 0;
	nir_foreach_variable(variable, &func->impl->locals) {
		unsigned attrib_count = glsl_count_attribute_slots(variable->type, false);
		variable->data.driver_location = ctx->num_locals * 4;
		ctx->num_locals += attrib_count;
	}
	ctx->locals = malloc(4 * ctx->num_locals * sizeof(LLVMValueRef));
	if (!ctx->locals)
	    return;

	for (i = 0; i < ctx->num_locals; i++) {
		for (j = 0; j < 4; j++) {
			ctx->locals[i * 4 + j] =
				si_build_alloca_undef(ctx, ctx->f32, "temp");
		}
	}
}

static LLVMValueRef
emit_float_saturate(struct nir_to_llvm_context *ctx, LLVMValueRef v, float lo, float hi)
{
	v = to_float(ctx, v);
	v = emit_intrin_2f_param(ctx, "llvm.maxnum.f32", ctx->f32, v, LLVMConstReal(ctx->f32, lo));
	return emit_intrin_2f_param(ctx, "llvm.minnum.f32", ctx->f32, v, LLVMConstReal(ctx->f32, hi));
}


static LLVMValueRef emit_pack_int16(struct nir_to_llvm_context *ctx,
					LLVMValueRef src0, LLVMValueRef src1)
{
	LLVMValueRef const16 = LLVMConstInt(ctx->i32, 16, false);
	LLVMValueRef comp[2];

	comp[0] = LLVMBuildAnd(ctx->builder, src0, LLVMConstInt(ctx-> i32, 65535, 0), "");
	comp[1] = LLVMBuildAnd(ctx->builder, src1, LLVMConstInt(ctx-> i32, 65535, 0), "");
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
	args->out[0] = LLVMGetUndef(ctx->f32);
	args->out[1] = LLVMGetUndef(ctx->f32);
	args->out[2] = LLVMGetUndef(ctx->f32);
	args->out[3] = LLVMGetUndef(ctx->f32);

	if (!values)
		return;

	if (ctx->stage == MESA_SHADER_FRAGMENT && target >= V_008DFC_SQ_EXP_MRT) {
		LLVMValueRef val[4];
		unsigned index = target - V_008DFC_SQ_EXP_MRT;
		unsigned col_format = (ctx->options->key.fs.col_format >> (4 * index)) & 0xf;
		bool is_int8 = (ctx->options->key.fs.is_int8 >> index) & 1;

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
							LLVMConstReal(ctx->f32, 65535), "");
				val[chan] = LLVMBuildFAdd(ctx->builder, val[chan],
							LLVMConstReal(ctx->f32, 0.5), "");
				val[chan] = LLVMBuildFPToUI(ctx->builder, val[chan],
							ctx->i32, "");
			}

			args->compr = 1;
			args->out[0] = emit_pack_int16(ctx, val[0], val[1]);
			args->out[1] = emit_pack_int16(ctx, val[2], val[3]);
			break;

		case V_028714_SPI_SHADER_SNORM16_ABGR:
			for (unsigned chan = 0; chan < 4; chan++) {
				val[chan] = emit_float_saturate(ctx, values[chan], -1, 1);
				val[chan] = LLVMBuildFMul(ctx->builder, val[chan],
							LLVMConstReal(ctx->f32, 32767), "");

				/* If positive, add 0.5, else add -0.5. */
				val[chan] = LLVMBuildFAdd(ctx->builder, val[chan],
						LLVMBuildSelect(ctx->builder,
							LLVMBuildFCmp(ctx->builder, LLVMRealOGE,
								val[chan], ctx->f32zero, ""),
							LLVMConstReal(ctx->f32, 0.5),
							LLVMConstReal(ctx->f32, -0.5), ""), "");
				val[chan] = LLVMBuildFPToSI(ctx->builder, val[chan], ctx->i32, "");
			}

			args->compr = 1;
			args->out[0] = emit_pack_int16(ctx, val[0], val[1]);
			args->out[1] = emit_pack_int16(ctx, val[2], val[3]);
			break;

		case V_028714_SPI_SHADER_UINT16_ABGR: {
			LLVMValueRef max = LLVMConstInt(ctx->i32, is_int8 ? 255 : 65535, 0);

			for (unsigned chan = 0; chan < 4; chan++) {
				val[chan] = to_integer(ctx, values[chan]);
				val[chan] = emit_minmax_int(ctx, LLVMIntULT, val[chan], max);
			}

			args->compr = 1;
			args->out[0] = emit_pack_int16(ctx, val[0], val[1]);
			args->out[1] = emit_pack_int16(ctx, val[2], val[3]);
			break;
		}

		case V_028714_SPI_SHADER_SINT16_ABGR: {
			LLVMValueRef max = LLVMConstInt(ctx->i32, is_int8 ? 127 : 32767, 0);
			LLVMValueRef min = LLVMConstInt(ctx->i32, is_int8 ? -128 : -32768, 0);

			/* Clamp. */
			for (unsigned chan = 0; chan < 4; chan++) {
				val[chan] = to_integer(ctx, values[chan]);
				val[chan] = emit_minmax_int(ctx, LLVMIntSLT, val[chan], max);
				val[chan] = emit_minmax_int(ctx, LLVMIntSGT, val[chan], min);
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
		args->out[i] = to_float(ctx, args->out[i]);
}

static void
handle_vs_outputs_post(struct nir_to_llvm_context *ctx)
{
	uint32_t param_count = 0;
	unsigned target;
	unsigned pos_idx, num_pos_exports = 0;
	struct ac_export_args args, pos_args[4] = {};
	LLVMValueRef psize_value = NULL, layer_value = NULL, viewport_index_value = NULL;
	int i;
	const uint64_t clip_mask = ctx->output_mask & ((1ull << VARYING_SLOT_CLIP_DIST0) |
						       (1ull << VARYING_SLOT_CLIP_DIST1) |
						       (1ull << VARYING_SLOT_CULL_DIST0) |
						       (1ull << VARYING_SLOT_CULL_DIST1));

	ctx->shader_info->vs.prim_id_output = 0xffffffff;
	ctx->shader_info->vs.layer_output = 0xffffffff;
	if (clip_mask) {
		LLVMValueRef slots[8];
		unsigned j;

		if (ctx->shader_info->vs.cull_dist_mask)
			ctx->shader_info->vs.cull_dist_mask <<= ctx->num_output_clips;

		i = VARYING_SLOT_CLIP_DIST0;
		for (j = 0; j < ctx->num_output_clips; j++)
			slots[j] = to_float(ctx, LLVMBuildLoad(ctx->builder,
							       ctx->outputs[radeon_llvm_reg_index_soa(i, j)], ""));
		i = VARYING_SLOT_CULL_DIST0;
		for (j = 0; j < ctx->num_output_culls; j++)
			slots[ctx->num_output_clips + j] = to_float(ctx, LLVMBuildLoad(ctx->builder,
									   ctx->outputs[radeon_llvm_reg_index_soa(i, j)], ""));

		for (i = ctx->num_output_clips + ctx->num_output_culls; i < 8; i++)
			slots[i] = LLVMGetUndef(ctx->f32);

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

	for (unsigned i = 0; i < RADEON_LLVM_MAX_OUTPUTS; ++i) {
		LLVMValueRef values[4];
		if (!(ctx->output_mask & (1ull << i)))
			continue;

		for (unsigned j = 0; j < 4; j++)
			values[j] = to_float(ctx, LLVMBuildLoad(ctx->builder,
					      ctx->outputs[radeon_llvm_reg_index_soa(i, j)], ""));

		if (i == VARYING_SLOT_POS) {
			target = V_008DFC_SQ_EXP_POS;
		} else if (i == VARYING_SLOT_CLIP_DIST0 ||
			   i == VARYING_SLOT_CLIP_DIST1 ||
			   i == VARYING_SLOT_CULL_DIST0 ||
			   i == VARYING_SLOT_CULL_DIST1) {
			continue;
		} else if (i == VARYING_SLOT_PSIZ) {
			ctx->shader_info->vs.writes_pointsize = true;
			psize_value = values[0];
			continue;
		} else if (i == VARYING_SLOT_LAYER) {
			ctx->shader_info->vs.writes_layer = true;
			layer_value = values[0];
			ctx->shader_info->vs.layer_output = param_count;
			target = V_008DFC_SQ_EXP_PARAM + param_count;
			param_count++;
		} else if (i == VARYING_SLOT_VIEWPORT) {
			ctx->shader_info->vs.writes_viewport_index = true;
			viewport_index_value = values[0];
			continue;
		} else if (i == VARYING_SLOT_PRIMITIVE_ID) {
			ctx->shader_info->vs.prim_id_output = param_count;
			target = V_008DFC_SQ_EXP_PARAM + param_count;
			param_count++;
		} else if (i >= VARYING_SLOT_VAR0) {
			ctx->shader_info->vs.export_mask |= 1u << (i - VARYING_SLOT_VAR0);
			target = V_008DFC_SQ_EXP_PARAM + param_count;
			param_count++;
		}

		si_llvm_init_export_args(ctx, values, target, &args);

		if (target >= V_008DFC_SQ_EXP_POS &&
		    target <= (V_008DFC_SQ_EXP_POS + 3)) {
			memcpy(&pos_args[target - V_008DFC_SQ_EXP_POS],
			       &args, sizeof(args));
		} else {
			ac_build_export(&ctx->ac, &args);
		}
	}

	/* We need to add the position output manually if it's missing. */
	if (!pos_args[0].out[0]) {
		pos_args[0].enabled_channels = 0xf;
		pos_args[0].valid_mask = 0;
		pos_args[0].done = 0;
		pos_args[0].target = V_008DFC_SQ_EXP_POS;
		pos_args[0].compr = 0;
		pos_args[0].out[0] = ctx->f32zero; /* X */
		pos_args[0].out[1] = ctx->f32zero; /* Y */
		pos_args[0].out[2] = ctx->f32zero; /* Z */
		pos_args[0].out[3] = ctx->f32one;  /* W */
	}

	uint32_t mask = ((ctx->shader_info->vs.writes_pointsize == true ? 1 : 0) |
			 (ctx->shader_info->vs.writes_layer == true ? 4 : 0) |
			 (ctx->shader_info->vs.writes_viewport_index == true ? 8 : 0));
	if (mask) {
		pos_args[1].enabled_channels = mask;
		pos_args[1].valid_mask = 0;
		pos_args[1].done = 0;
		pos_args[1].target = V_008DFC_SQ_EXP_POS + 1;
		pos_args[1].compr = 0;
		pos_args[1].out[0] = ctx->f32zero; /* X */
		pos_args[1].out[1] = ctx->f32zero; /* Y */
		pos_args[1].out[2] = ctx->f32zero; /* Z */
		pos_args[1].out[3] = ctx->f32zero;  /* W */

		if (ctx->shader_info->vs.writes_pointsize == true)
			pos_args[1].out[0] = psize_value;
		if (ctx->shader_info->vs.writes_layer == true)
			pos_args[1].out[2] = layer_value;
		if (ctx->shader_info->vs.writes_viewport_index == true)
			pos_args[1].out[3] = viewport_index_value;
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

	ctx->shader_info->vs.pos_exports = num_pos_exports;
	ctx->shader_info->vs.param_exports = param_count;
}

static void
handle_es_outputs_post(struct nir_to_llvm_context *ctx)
{
	int j;
	uint64_t max_output_written = 0;
	for (unsigned i = 0; i < RADEON_LLVM_MAX_OUTPUTS; ++i) {
		LLVMValueRef *out_ptr = &ctx->outputs[i * 4];
		int param_index;
		int length = 4;
		int start = 0;
		if (!(ctx->output_mask & (1ull << i)))
			continue;

		if (i == VARYING_SLOT_CLIP_DIST0) {
			length = ctx->num_output_clips;
		} else if (i == VARYING_SLOT_CULL_DIST0) {
			start = ctx->num_output_clips;
			length = ctx->num_output_culls;
		}
		param_index = shader_io_get_unique_index(i);

		if (param_index > max_output_written)
			max_output_written = param_index;

		for (j = 0; j < length; j++) {
			LLVMValueRef out_val = LLVMBuildLoad(ctx->builder, out_ptr[j], "");
			out_val = LLVMBuildBitCast(ctx->builder, out_val, ctx->i32, "");

			ac_build_buffer_store_dword(&ctx->ac,
					       ctx->esgs_ring,
					       out_val, 1,
					       NULL, ctx->es2gs_offset,
					       (4 * param_index + j + start) * 4,
					       1, 1, true, true);
		}
	}
	ctx->shader_info->vs.esgs_itemsize = (max_output_written + 1) * 16;
}

static void
si_export_mrt_color(struct nir_to_llvm_context *ctx,
		    LLVMValueRef *color, unsigned param, bool is_last)
{

	struct ac_export_args args;

	/* Export */
	si_llvm_init_export_args(ctx, color, param,
				 &args);

	if (is_last) {
		args.valid_mask = 1; /* whether the EXEC mask is valid */
		args.done = 1; /* DONE bit */
	} else if (!args.enabled_channels)
		return; /* unnecessary NULL export */

	ac_build_export(&ctx->ac, &args);
}

static void
si_export_mrt_z(struct nir_to_llvm_context *ctx,
		LLVMValueRef depth, LLVMValueRef stencil,
		LLVMValueRef samplemask)
{
	LLVMValueRef args[9];
	unsigned mask = 0;
	args[1] = ctx->i32one; /* whether the EXEC mask is valid */
	args[2] = ctx->i32one; /* DONE bit */
	/* Specify the target we are exporting */
	args[3] = LLVMConstInt(ctx->i32, V_008DFC_SQ_EXP_MRTZ, false);

	args[4] = ctx->i32zero; /* COMP flag */
	args[5] = LLVMGetUndef(ctx->f32); /* R, depth */
	args[6] = LLVMGetUndef(ctx->f32); /* G, stencil test val[0:7], stencil op val[8:15] */
	args[7] = LLVMGetUndef(ctx->f32); /* B, sample mask */
	args[8] = LLVMGetUndef(ctx->f32); /* A, alpha to mask */

	if (depth) {
		args[5] = depth;
		mask |= 0x1;
	}

	if (stencil) {
		args[6] = stencil;
		mask |= 0x2;
	}

	if (samplemask) {
		args[7] = samplemask;
		mask |= 0x04;
	}

	/* SI (except OLAND) has a bug that it only looks
	 * at the X writemask component. */
	if (ctx->options->chip_class == SI &&
	    ctx->options->family != CHIP_OLAND)
		mask |= 0x01;

	args[0] = LLVMConstInt(ctx->i32, mask, false);
	ac_build_intrinsic(&ctx->ac, "llvm.SI.export",
			   ctx->voidt, args, 9,
			   AC_FUNC_ATTR_LEGACY);
}

static void
handle_fs_outputs_post(struct nir_to_llvm_context *ctx)
{
	unsigned index = 0;
	LLVMValueRef depth = NULL, stencil = NULL, samplemask = NULL;

	for (unsigned i = 0; i < RADEON_LLVM_MAX_OUTPUTS; ++i) {
		LLVMValueRef values[4];

		if (!(ctx->output_mask & (1ull << i)))
			continue;

		if (i == FRAG_RESULT_DEPTH) {
			ctx->shader_info->fs.writes_z = true;
			depth = to_float(ctx, LLVMBuildLoad(ctx->builder,
							    ctx->outputs[radeon_llvm_reg_index_soa(i, 0)], ""));
		} else if (i == FRAG_RESULT_STENCIL) {
			ctx->shader_info->fs.writes_stencil = true;
			stencil = to_float(ctx, LLVMBuildLoad(ctx->builder,
							      ctx->outputs[radeon_llvm_reg_index_soa(i, 0)], ""));
		} else if (i == FRAG_RESULT_SAMPLE_MASK) {
			ctx->shader_info->fs.writes_sample_mask = true;
			samplemask = to_float(ctx, LLVMBuildLoad(ctx->builder,
								  ctx->outputs[radeon_llvm_reg_index_soa(i, 0)], ""));
		} else {
			bool last = false;
			for (unsigned j = 0; j < 4; j++)
				values[j] = to_float(ctx, LLVMBuildLoad(ctx->builder,
									ctx->outputs[radeon_llvm_reg_index_soa(i, j)], ""));

			if (!ctx->shader_info->fs.writes_z && !ctx->shader_info->fs.writes_stencil && !ctx->shader_info->fs.writes_sample_mask)
				last = ctx->output_mask <= ((1ull << (i + 1)) - 1);

			si_export_mrt_color(ctx, values, V_008DFC_SQ_EXP_MRT + index, last);
			index++;
		}
	}

	if (depth || stencil || samplemask)
		si_export_mrt_z(ctx, depth, stencil, samplemask);
	else if (!index)
		si_export_mrt_color(ctx, NULL, V_008DFC_SQ_EXP_NULL, true);

	ctx->shader_info->fs.output_mask = index ? ((1ull << index) - 1) : 0;
}

static void
emit_gs_epilogue(struct nir_to_llvm_context *ctx)
{
	ac_build_sendmsg(&ctx->ac, AC_SENDMSG_GS_OP_NOP | AC_SENDMSG_GS_DONE, ctx->gs_wave_id);
}

static void
handle_shader_outputs_post(struct nir_to_llvm_context *ctx)
{
	switch (ctx->stage) {
	case MESA_SHADER_VERTEX:
		if (ctx->options->key.vs.as_es)
			handle_es_outputs_post(ctx);
		else
			handle_vs_outputs_post(ctx);
		break;
	case MESA_SHADER_FRAGMENT:
		handle_fs_outputs_post(ctx);
		break;
	case MESA_SHADER_GEOMETRY:
		emit_gs_epilogue(ctx);
		break;
	default:
		break;
	}
}

static void
handle_shared_compute_var(struct nir_to_llvm_context *ctx,
			  struct nir_variable *variable, uint32_t *offset, int idx)
{
	unsigned size = glsl_count_attribute_slots(variable->type, false);
	variable->data.driver_location = *offset;
	*offset += size;
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
ac_setup_rings(struct nir_to_llvm_context *ctx)
{
	if (ctx->stage == MESA_SHADER_VERTEX && ctx->options->key.vs.as_es) {
		ctx->esgs_ring = ac_build_indexed_load_const(&ctx->ac, ctx->ring_offsets, ctx->i32one);
	}

	if (ctx->is_gs_copy_shader) {
		ctx->gsvs_ring = ac_build_indexed_load_const(&ctx->ac, ctx->ring_offsets, LLVMConstInt(ctx->i32, 3, false));
	}
	if (ctx->stage == MESA_SHADER_GEOMETRY) {
		LLVMValueRef tmp;
		ctx->esgs_ring = ac_build_indexed_load_const(&ctx->ac, ctx->ring_offsets, LLVMConstInt(ctx->i32, 2, false));
		ctx->gsvs_ring = ac_build_indexed_load_const(&ctx->ac, ctx->ring_offsets, LLVMConstInt(ctx->i32, 4, false));

		ctx->gsvs_ring = LLVMBuildBitCast(ctx->builder, ctx->gsvs_ring, ctx->v4i32, "");

		ctx->gsvs_ring = LLVMBuildInsertElement(ctx->builder, ctx->gsvs_ring, ctx->gsvs_num_entries, LLVMConstInt(ctx->i32, 2, false), "");
		tmp = LLVMBuildExtractElement(ctx->builder, ctx->gsvs_ring, ctx->i32one, "");
		tmp = LLVMBuildOr(ctx->builder, tmp, ctx->gsvs_ring_stride, "");
		ctx->gsvs_ring = LLVMBuildInsertElement(ctx->builder, ctx->gsvs_ring, tmp, ctx->i32one, "");

		ctx->gsvs_ring = LLVMBuildBitCast(ctx->builder, ctx->gsvs_ring, ctx->v16i8, "");
	}
}

static
LLVMModuleRef ac_translate_nir_to_llvm(LLVMTargetMachineRef tm,
                                       struct nir_shader *nir,
                                       struct ac_shader_variant_info *shader_info,
                                       const struct ac_nir_compiler_options *options)
{
	struct nir_to_llvm_context ctx = {0};
	struct nir_function *func;
	unsigned i;
	ctx.options = options;
	ctx.shader_info = shader_info;
	ctx.context = LLVMContextCreate();
	ctx.module = LLVMModuleCreateWithNameInContext("shader", ctx.context);

	ac_llvm_context_init(&ctx.ac, ctx.context);
	ctx.ac.module = ctx.module;

	ctx.has_ds_bpermute = ctx.options->chip_class >= VI;

	memset(shader_info, 0, sizeof(*shader_info));

	LLVMSetTarget(ctx.module, options->supports_spill ? "amdgcn-mesa-mesa3d" : "amdgcn--");
	setup_types(&ctx);

	ctx.builder = LLVMCreateBuilderInContext(ctx.context);
	ctx.ac.builder = ctx.builder;
	ctx.stage = nir->stage;

	for (i = 0; i < AC_UD_MAX_SETS; i++)
		shader_info->user_sgprs_locs.descriptor_sets[i].sgpr_idx = -1;
	for (i = 0; i < AC_UD_MAX_UD; i++)
		shader_info->user_sgprs_locs.shader_data[i].sgpr_idx = -1;

	create_function(&ctx);

	if (nir->stage == MESA_SHADER_COMPUTE) {
		int num_shared = 0;
		nir_foreach_variable(variable, &nir->shared)
			num_shared++;
		if (num_shared) {
			int idx = 0;
			uint32_t shared_size = 0;
			LLVMValueRef var;
			LLVMTypeRef i8p = LLVMPointerType(ctx.i8, LOCAL_ADDR_SPACE);
			nir_foreach_variable(variable, &nir->shared) {
				handle_shared_compute_var(&ctx, variable, &shared_size, idx);
				idx++;
			}

			shared_size *= 16;
			var = LLVMAddGlobalInAddressSpace(ctx.module,
							  LLVMArrayType(ctx.i8, shared_size),
							  "compute_lds",
							  LOCAL_ADDR_SPACE);
			LLVMSetAlignment(var, 4);
			ctx.shared_memory = LLVMBuildBitCast(ctx.builder, var, i8p, "");
		}
	} else if (nir->stage == MESA_SHADER_GEOMETRY) {
		ctx.gs_next_vertex = ac_build_alloca(&ctx, ctx.i32, "gs_next_vertex");

		ctx.gs_max_out_vertices = nir->info->gs.vertices_out;
	}

	ac_setup_rings(&ctx);

	nir_foreach_variable(variable, &nir->inputs)
		handle_shader_input_decl(&ctx, variable);

	if (nir->stage == MESA_SHADER_FRAGMENT)
		handle_fs_inputs_pre(&ctx, nir);

	nir_foreach_variable(variable, &nir->outputs)
		handle_shader_output_decl(&ctx, variable);

	ctx.defs = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
	                                   _mesa_key_pointer_equal);
	ctx.phis = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
	                                   _mesa_key_pointer_equal);

	func = (struct nir_function *)exec_list_get_head(&nir->functions);

	setup_locals(&ctx, func);

	visit_cf_list(&ctx, &func->impl->body);
	phi_post_pass(&ctx);

	handle_shader_outputs_post(&ctx);
	LLVMBuildRetVoid(ctx.builder);

	ac_llvm_finalize_module(&ctx);
	free(ctx.locals);
	ralloc_free(ctx.defs);
	ralloc_free(ctx.phis);

	if (nir->stage == MESA_SHADER_GEOMETRY) {
		shader_info->gs.gsvs_vertex_size = util_bitcount64(ctx.output_mask) * 16;
		shader_info->gs.max_gsvs_emit_size = shader_info->gs.gsvs_vertex_size *
			nir->info->gs.vertices_out;
	}
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
}

void ac_compile_nir_shader(LLVMTargetMachineRef tm,
                           struct ac_shader_binary *binary,
                           struct ac_shader_config *config,
                           struct ac_shader_variant_info *shader_info,
                           struct nir_shader *nir,
                           const struct ac_nir_compiler_options *options,
			   bool dump_shader)
{

	LLVMModuleRef llvm_module = ac_translate_nir_to_llvm(tm, nir, shader_info,
	                                                     options);

	ac_compile_llvm_module(tm, llvm_module, binary, config, shader_info, nir->stage, dump_shader, options->supports_spill);
	switch (nir->stage) {
	case MESA_SHADER_COMPUTE:
		for (int i = 0; i < 3; ++i)
			shader_info->cs.block_size[i] = nir->info->cs.local_size[i];
		break;
	case MESA_SHADER_FRAGMENT:
		shader_info->fs.early_fragment_test = nir->info->fs.early_fragment_tests;
		break;
	case MESA_SHADER_GEOMETRY:
		shader_info->gs.vertices_in = nir->info->gs.vertices_in;
		shader_info->gs.vertices_out = nir->info->gs.vertices_out;
		shader_info->gs.output_prim = nir->info->gs.output_primitive;
		shader_info->gs.invocations = nir->info->gs.invocations;
		break;
	case MESA_SHADER_VERTEX:
		shader_info->vs.as_es = options->key.vs.as_es;
		break;
	default:
		break;
	}
}

static void
ac_gs_copy_shader_emit(struct nir_to_llvm_context *ctx)
{
	LLVMValueRef args[9];
	args[0] = ctx->gsvs_ring;
	args[1] = LLVMBuildMul(ctx->builder, ctx->vertex_id, LLVMConstInt(ctx->i32, 4, false), "");
	args[3] = ctx->i32zero;
	args[4] = ctx->i32one;  /* OFFEN */
	args[5] = ctx->i32zero; /* IDXEN */
	args[6] = ctx->i32one;  /* GLC */
	args[7] = ctx->i32one;  /* SLC */
	args[8] = ctx->i32zero; /* TFE */

	int idx = 0;
	int clip_cull_slot = -1;
	for (unsigned i = 0; i < RADEON_LLVM_MAX_OUTPUTS; ++i) {
		int length = 4;
		int start = 0;
		int slot = idx;
		int slot_inc = 1;
		if (!(ctx->output_mask & (1ull << i)))
			continue;

		if (i == VARYING_SLOT_CLIP_DIST1 ||
		    i == VARYING_SLOT_CULL_DIST1)
			continue;

		if (i == VARYING_SLOT_CLIP_DIST0 ||
		    i == VARYING_SLOT_CULL_DIST0) {
			/* unpack clip and cull from a single set of slots */
			if (clip_cull_slot == -1) {
				clip_cull_slot = idx;
				if (ctx->num_output_clips + ctx->num_output_culls > 4)
					slot_inc = 2;
			} else {
				slot = clip_cull_slot;
				slot_inc = 0;
			}
			if (i == VARYING_SLOT_CLIP_DIST0)
				length = ctx->num_output_clips;
			if (i == VARYING_SLOT_CULL_DIST0) {
				start = ctx->num_output_clips;
				length = ctx->num_output_culls;
			}
		}

		for (unsigned j = 0; j < length; j++) {
			LLVMValueRef value;
			args[2] = LLVMConstInt(ctx->i32,
					       (slot * 4 + j + start) *
					       ctx->gs_max_out_vertices * 16 * 4, false);

			value = ac_build_intrinsic(&ctx->ac,
						   "llvm.SI.buffer.load.dword.i32.i32",
						   ctx->i32, args, 9,
						   AC_FUNC_ATTR_READONLY |
						   AC_FUNC_ATTR_LEGACY);

			LLVMBuildStore(ctx->builder,
				       to_float(ctx, value), ctx->outputs[radeon_llvm_reg_index_soa(i, j)]);
		}
		idx += slot_inc;
	}
	handle_vs_outputs_post(ctx);
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

	ac_llvm_context_init(&ctx.ac, ctx.context);
	ctx.ac.module = ctx.module;

	ctx.is_gs_copy_shader = true;
	LLVMSetTarget(ctx.module, "amdgcn--");
	setup_types(&ctx);

	ctx.builder = LLVMCreateBuilderInContext(ctx.context);
	ctx.ac.builder = ctx.builder;
	ctx.stage = MESA_SHADER_VERTEX;

	create_function(&ctx);

	ctx.gs_max_out_vertices = geom_shader->info->gs.vertices_out;
	ac_setup_rings(&ctx);

	nir_foreach_variable(variable, &geom_shader->outputs)
		handle_shader_output_decl(&ctx, variable);

	ac_gs_copy_shader_emit(&ctx);

	LLVMBuildRetVoid(ctx.builder);

	ac_llvm_finalize_module(&ctx);

	ac_compile_llvm_module(tm, ctx.module, binary, config, shader_info,
			       MESA_SHADER_VERTEX,
			       dump_shader, options->supports_spill);
}
