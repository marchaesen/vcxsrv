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
	const struct ac_nir_compiler_options *options;
	struct ac_shader_variant_info *shader_info;

	LLVMContextRef context;
	LLVMModuleRef module;
	LLVMBuilderRef builder;
	LLVMValueRef main_function;

	struct hash_table *defs;
	struct hash_table *phis;

	LLVMValueRef descriptor_sets[4];
	LLVMValueRef push_constants;
	LLVMValueRef num_work_groups;
	LLVMValueRef workgroup_ids;
	LLVMValueRef local_invocation_ids;
	LLVMValueRef tg_size;

	LLVMValueRef vertex_buffers;
	LLVMValueRef base_vertex;
	LLVMValueRef start_instance;
	LLVMValueRef vertex_id;
	LLVMValueRef rel_auto_id;
	LLVMValueRef vs_prim_id;
	LLVMValueRef instance_id;

	LLVMValueRef prim_mask;
	LLVMValueRef sample_positions;
	LLVMValueRef persp_sample, persp_center, persp_centroid;
	LLVMValueRef linear_sample, linear_center, linear_centroid;
	LLVMValueRef front_face;
	LLVMValueRef ancillary;
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

	unsigned range_md_kind;
	unsigned uniform_md_kind;
	unsigned fpmath_md_kind;
	unsigned invariant_load_md_kind;
	LLVMValueRef empty_md;
	LLVMValueRef fpmath_md_2p5_ulp;
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
	unsigned num_clips;
	unsigned num_culls;
};

struct ac_tex_info {
	LLVMValueRef args[12];
	int arg_count;
	LLVMTypeRef dst_type;
	bool has_offset;
};

static LLVMValueRef
emit_llvm_intrinsic(struct nir_to_llvm_context *ctx, const char *name,
                    LLVMTypeRef return_type, LLVMValueRef *params,
                    unsigned param_count, LLVMAttribute attribs);
static LLVMValueRef get_sampler_desc(struct nir_to_llvm_context *ctx,
				     nir_deref_var *deref,
				     enum desc_type desc_type);
static unsigned radeon_llvm_reg_index_soa(unsigned index, unsigned chan)
{
	return (index * 4) + chan;
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
                     unsigned param_count, unsigned array_params,
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
		LLVMValueRef P = LLVMGetParam(main_function, i);

		if (i < array_params) {
			LLVMAddAttribute(P, LLVMByValAttribute);
			ac_add_attr_dereferenceable(P, UINT64_MAX);
		}
		else
			LLVMAddAttribute(P, LLVMInRegAttribute);
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

static LLVMValueRef to_integer(struct nir_to_llvm_context *ctx, LLVMValueRef v)
{
	LLVMTypeRef type = LLVMTypeOf(v);
	if (type == ctx->f32) {
		return LLVMBuildBitCast(ctx->builder, v, ctx->i32, "");
	} else if (LLVMGetTypeKind(type) == LLVMVectorTypeKind) {
		LLVMTypeRef elem_type = LLVMGetElementType(type);
		if (elem_type == ctx->f32) {
			LLVMTypeRef nt = LLVMVectorType(ctx->i32, LLVMGetVectorSize(type));
			return LLVMBuildBitCast(ctx->builder, v, nt, "");
		}
	}
	return v;
}

static LLVMValueRef to_float(struct nir_to_llvm_context *ctx, LLVMValueRef v)
{
	LLVMTypeRef type = LLVMTypeOf(v);
	if (type == ctx->i32) {
		return LLVMBuildBitCast(ctx->builder, v, ctx->f32, "");
	} else if (LLVMGetTypeKind(type) == LLVMVectorTypeKind) {
		LLVMTypeRef elem_type = LLVMGetElementType(type);
		if (elem_type == ctx->i32) {
			LLVMTypeRef nt = LLVMVectorType(ctx->f32, LLVMGetVectorSize(type));
			return LLVMBuildBitCast(ctx->builder, v, nt, "");
		}
	}
	return v;
}

static LLVMValueRef build_indexed_load(struct nir_to_llvm_context *ctx,
				       LLVMValueRef base_ptr, LLVMValueRef index,
				       bool uniform)
{
	LLVMValueRef pointer;
	LLVMValueRef indices[] = {ctx->i32zero, index};

	pointer = LLVMBuildGEP(ctx->builder, base_ptr, indices, 2, "");
	if (uniform)
		LLVMSetMetadata(pointer, ctx->uniform_md_kind, ctx->empty_md);
	return LLVMBuildLoad(ctx->builder, pointer, "");
}

static LLVMValueRef build_indexed_load_const(struct nir_to_llvm_context *ctx,
					     LLVMValueRef base_ptr, LLVMValueRef index)
{
	LLVMValueRef result = build_indexed_load(ctx, base_ptr, index, true);
	LLVMSetMetadata(result, ctx->invariant_load_md_kind, ctx->empty_md);
	return result;
}

static void create_function(struct nir_to_llvm_context *ctx,
                            struct nir_shader *nir)
{
	LLVMTypeRef arg_types[23];
	unsigned arg_idx = 0;
	unsigned array_count = 0;
	unsigned sgpr_count = 0, user_sgpr_count;
	unsigned i;

	/* 1 for each descriptor set */
	for (unsigned i = 0; i < 4; ++i)
		arg_types[arg_idx++] = const_array(ctx->i8, 1024 * 1024);

	/* 1 for push constants and dynamic descriptors */
	arg_types[arg_idx++] = const_array(ctx->i8, 1024 * 1024);

	array_count = arg_idx;
	switch (nir->stage) {
	case MESA_SHADER_COMPUTE:
		arg_types[arg_idx++] = LLVMVectorType(ctx->i32, 3); /* grid size */
		user_sgpr_count = arg_idx;
		arg_types[arg_idx++] = LLVMVectorType(ctx->i32, 3);
		arg_types[arg_idx++] = ctx->i32;
		sgpr_count = arg_idx;

		arg_types[arg_idx++] = LLVMVectorType(ctx->i32, 3);
		break;
	case MESA_SHADER_VERTEX:
		arg_types[arg_idx++] = const_array(ctx->v16i8, 16); /* vertex buffers */
		arg_types[arg_idx++] = ctx->i32; // base vertex
		arg_types[arg_idx++] = ctx->i32; // start instance
		user_sgpr_count = sgpr_count = arg_idx;
		arg_types[arg_idx++] = ctx->i32; // vertex id
		arg_types[arg_idx++] = ctx->i32; // rel auto id
		arg_types[arg_idx++] = ctx->i32; // vs prim id
		arg_types[arg_idx++] = ctx->i32; // instance id
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
		arg_types[arg_idx++] = ctx->f32;  /* sample coverage */
		arg_types[arg_idx++] = ctx->i32;  /* fixed pt */
		break;
	default:
		unreachable("Shader stage not implemented");
	}

	ctx->main_function = create_llvm_function(
	    ctx->context, ctx->module, ctx->builder, NULL, 0, arg_types,
	    arg_idx, array_count, sgpr_count, ctx->options->unsafe_math);
	set_llvm_calling_convention(ctx->main_function, nir->stage);


	ctx->shader_info->num_input_sgprs = 0;
	ctx->shader_info->num_input_vgprs = 0;

	for (i = 0; i < user_sgpr_count; i++)
		ctx->shader_info->num_user_sgprs += llvm_get_type_size(arg_types[i]) / 4;

	ctx->shader_info->num_input_sgprs = ctx->shader_info->num_user_sgprs;
	for (; i < sgpr_count; i++)
		ctx->shader_info->num_input_sgprs += llvm_get_type_size(arg_types[i]) / 4;

	if (nir->stage != MESA_SHADER_FRAGMENT)
		for (; i < arg_idx; ++i)
			ctx->shader_info->num_input_vgprs += llvm_get_type_size(arg_types[i]) / 4;

	arg_idx = 0;
	for (unsigned i = 0; i < 4; ++i)
		ctx->descriptor_sets[i] =
		    LLVMGetParam(ctx->main_function, arg_idx++);

	ctx->push_constants = LLVMGetParam(ctx->main_function, arg_idx++);

	switch (nir->stage) {
	case MESA_SHADER_COMPUTE:
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
		ctx->vertex_buffers = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->base_vertex = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->start_instance = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->vertex_id = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->rel_auto_id = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->vs_prim_id = LLVMGetParam(ctx->main_function, arg_idx++);
		ctx->instance_id = LLVMGetParam(ctx->main_function, arg_idx++);
		break;
	case MESA_SHADER_FRAGMENT:
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

	ctx->range_md_kind = LLVMGetMDKindIDInContext(ctx->context,
						      "range", 5);
	ctx->invariant_load_md_kind = LLVMGetMDKindIDInContext(ctx->context,
							       "invariant.load", 14);
	ctx->uniform_md_kind =
	    LLVMGetMDKindIDInContext(ctx->context, "amdgpu.uniform", 14);
	ctx->empty_md = LLVMMDNodeInContext(ctx->context, NULL, 0);

	ctx->fpmath_md_kind = LLVMGetMDKindIDInContext(ctx->context, "fpmath", 6);

	args[0] = LLVMConstReal(ctx->f32, 2.5);
	ctx->fpmath_md_2p5_ulp = LLVMMDNodeInContext(ctx->context, args, 1);
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

static LLVMValueRef
build_gather_values_extended(struct nir_to_llvm_context *ctx,
			     LLVMValueRef *values,
			     unsigned value_count,
			     unsigned value_stride,
			     bool load)
{
	LLVMBuilderRef builder = ctx->builder;
	LLVMValueRef vec;
	unsigned i;


	if (value_count == 1) {
		if (load)
			return LLVMBuildLoad(builder, values[0], "");
		return values[0];
	}

	for (i = 0; i < value_count; i++) {
		LLVMValueRef value = values[i * value_stride];
		if (load)
			value = LLVMBuildLoad(builder, value, "");

		if (!i)
			vec = LLVMGetUndef( LLVMVectorType(LLVMTypeOf(value), value_count));
		LLVMValueRef index = LLVMConstInt(ctx->i32, i, false);
		vec = LLVMBuildInsertElement(builder, vec, value, index, "");
	}
	return vec;
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

static LLVMValueRef
build_gather_values(struct nir_to_llvm_context *ctx,
		    LLVMValueRef *values,
		    unsigned value_count)
{
	return build_gather_values_extended(ctx, values, value_count, 1, false);
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
			value = build_gather_values(ctx, values, num_components);
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
					 LLVMValueRef src0)
{
	LLVMValueRef params[] = {
		to_float(ctx, src0),
	};
	return emit_llvm_intrinsic(ctx, intrin, ctx->f32, params, 1, LLVMReadNoneAttribute);
}

static LLVMValueRef emit_intrin_2f_param(struct nir_to_llvm_context *ctx,
				       const char *intrin,
				       LLVMValueRef src0, LLVMValueRef src1)
{
	LLVMValueRef params[] = {
		to_float(ctx, src0),
		to_float(ctx, src1),
	};
	return emit_llvm_intrinsic(ctx, intrin, ctx->f32, params, 2, LLVMReadNoneAttribute);
}

static LLVMValueRef emit_intrin_3f_param(struct nir_to_llvm_context *ctx,
					 const char *intrin,
					 LLVMValueRef src0, LLVMValueRef src1, LLVMValueRef src2)
{
	LLVMValueRef params[] = {
		to_float(ctx, src0),
		to_float(ctx, src1),
		to_float(ctx, src2),
	};
	return emit_llvm_intrinsic(ctx, intrin, ctx->f32, params, 3, LLVMReadNoneAttribute);
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
	return emit_llvm_intrinsic(ctx, "llvm.cttz.i32", ctx->i32, params, 2, LLVMReadNoneAttribute);
}

static LLVMValueRef emit_ifind_msb(struct nir_to_llvm_context *ctx,
				   LLVMValueRef src0)
{
	LLVMValueRef msb = emit_llvm_intrinsic(ctx, "llvm.AMDGPU.flbit.i32",
					       ctx->i32, &src0, 1,
					       LLVMReadNoneAttribute);

	/* The HW returns the last bit index from MSB, but NIR wants
	 * the index from LSB. Invert it by doing "31 - msb". */
	msb = LLVMBuildSub(ctx->builder, LLVMConstInt(ctx->i32, 31, false),
			   msb, "");

	LLVMValueRef all_ones = LLVMConstInt(ctx->i32, -1, true);
	LLVMValueRef cond = LLVMBuildOr(ctx->builder,
					LLVMBuildICmp(ctx->builder, LLVMIntEQ,
						      src0, ctx->i32zero, ""),
					LLVMBuildICmp(ctx->builder, LLVMIntEQ,
						      src0, all_ones, ""), "");

	return LLVMBuildSelect(ctx->builder, cond, all_ones, msb, "");
}

static LLVMValueRef emit_ufind_msb(struct nir_to_llvm_context *ctx,
				   LLVMValueRef src0)
{
	LLVMValueRef args[2] = {
		src0,
		ctx->i32one,
	};
	LLVMValueRef msb = emit_llvm_intrinsic(ctx, "llvm.ctlz.i32",
					       ctx->i32, args, ARRAY_SIZE(args),
					       LLVMReadNoneAttribute);

	/* The HW returns the last bit index from MSB, but NIR wants
	 * the index from LSB. Invert it by doing "31 - msb". */
	msb = LLVMBuildSub(ctx->builder, LLVMConstInt(ctx->i32, 31, false),
			   msb, "");

	return LLVMBuildSelect(ctx->builder,
			       LLVMBuildICmp(ctx->builder, LLVMIntEQ, src0,
					     ctx->i32zero, ""),
			       LLVMConstInt(ctx->i32, -1, true), msb, "");
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
	LLVMValueRef floor = emit_llvm_intrinsic(ctx, intr,
						 ctx->f32, params, 1,
						 LLVMReadNoneAttribute);
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

	res = emit_llvm_intrinsic(ctx, intrin, ret_type,
				  params, 2, LLVMReadNoneAttribute);

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
					  const char *intrin,
					  LLVMValueRef srcs[3])
{
	LLVMValueRef result;
	LLVMValueRef icond = LLVMBuildICmp(ctx->builder, LLVMIntEQ, srcs[2], LLVMConstInt(ctx->i32, 32, false), "");
	result = emit_llvm_intrinsic(ctx, intrin, ctx->i32, srcs, 3, LLVMReadNoneAttribute);

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

/**
 * Set range metadata on an instruction.  This can only be used on load and
 * call instructions.  If you know an instruction can only produce the values
 * 0, 1, 2, you would do set_range_metadata(value, 0, 3);
 * \p lo is the minimum value inclusive.
 * \p hi is the maximum value exclusive.
 */
static void set_range_metadata(struct nir_to_llvm_context *ctx,
			       LLVMValueRef value, unsigned lo, unsigned hi)
{
	LLVMValueRef range_md, md_args[2];
	LLVMTypeRef type = LLVMTypeOf(value);
	LLVMContextRef context = LLVMGetTypeContext(type);

	md_args[0] = LLVMConstInt(type, lo, false);
	md_args[1] = LLVMConstInt(type, hi, false);
	range_md = LLVMMDNodeInContext(context, md_args, 2);
	LLVMSetMetadata(value, ctx->range_md_kind, range_md);
}

static LLVMValueRef get_thread_id(struct nir_to_llvm_context *ctx)
{
	LLVMValueRef tid;
	LLVMValueRef tid_args[2];
	tid_args[0] = LLVMConstInt(ctx->i32, 0xffffffff, false);
	tid_args[1] = ctx->i32zero;
	tid_args[1] = emit_llvm_intrinsic(ctx,
					  "llvm.amdgcn.mbcnt.lo", ctx->i32,
					  tid_args, 2, LLVMReadNoneAttribute);

	tid = emit_llvm_intrinsic(ctx,
				  "llvm.amdgcn.mbcnt.hi", ctx->i32,
				  tid_args, 2, LLVMReadNoneAttribute);
	set_range_metadata(ctx, tid, 0, 64);
	return tid;
}

/*
 * SI implements derivatives using the local data store (LDS)
 * All writes to the LDS happen in all executing threads at
 * the same time. TID is the Thread ID for the current
 * thread and is a value between 0 and 63, representing
 * the thread's position in the wavefront.
 *
 * For the pixel shader threads are grouped into quads of four pixels.
 * The TIDs of the pixels of a quad are:
 *
 *  +------+------+
 *  |4n + 0|4n + 1|
 *  +------+------+
 *  |4n + 2|4n + 3|
 *  +------+------+
 *
 * So, masking the TID with 0xfffffffc yields the TID of the top left pixel
 * of the quad, masking with 0xfffffffd yields the TID of the top pixel of
 * the current pixel's column, and masking with 0xfffffffe yields the TID
 * of the left pixel of the current pixel's row.
 *
 * Adding 1 yields the TID of the pixel to the right of the left pixel, and
 * adding 2 yields the TID of the pixel below the top pixel.
 */
/* masks for thread ID. */
#define TID_MASK_TOP_LEFT 0xfffffffc
#define TID_MASK_TOP      0xfffffffd
#define TID_MASK_LEFT     0xfffffffe
static LLVMValueRef emit_ddxy(struct nir_to_llvm_context *ctx,
			      nir_alu_instr *instr,
			      LLVMValueRef src0)
{
	LLVMValueRef indices[2];
	LLVMValueRef store_ptr, load_ptr0, load_ptr1;
	LLVMValueRef tl, trbl, result;
	LLVMValueRef tl_tid, trbl_tid;
	LLVMValueRef args[2];
	unsigned mask;
	int idx;
	ctx->has_ddxy = true;
	if (!ctx->lds)
		ctx->lds = LLVMAddGlobalInAddressSpace(ctx->module,
						       LLVMArrayType(ctx->i32, 64),
						       "ddxy_lds", LOCAL_ADDR_SPACE);

	indices[0] = ctx->i32zero;
	indices[1] = get_thread_id(ctx);
	store_ptr = LLVMBuildGEP(ctx->builder, ctx->lds,
				 indices, 2, "");

	if (instr->op == nir_op_fddx_fine || instr->op == nir_op_fddx)
		mask = TID_MASK_LEFT;
	else if (instr->op == nir_op_fddy_fine || instr->op == nir_op_fddy)
		mask = TID_MASK_TOP;
	else
		mask = TID_MASK_TOP_LEFT;

	tl_tid = LLVMBuildAnd(ctx->builder, indices[1],
			      LLVMConstInt(ctx->i32, mask, false), "");
	indices[1] = tl_tid;
	load_ptr0 = LLVMBuildGEP(ctx->builder, ctx->lds,
				 indices, 2, "");

	/* for DDX we want to next X pixel, DDY next Y pixel. */
	if (instr->op == nir_op_fddx_fine ||
	    instr->op == nir_op_fddx_coarse ||
	    instr->op == nir_op_fddx)
		idx = 1;
	else
		idx = 2;

	trbl_tid = LLVMBuildAdd(ctx->builder, indices[1],
				LLVMConstInt(ctx->i32, idx, false), "");
	indices[1] = trbl_tid;
	load_ptr1 = LLVMBuildGEP(ctx->builder, ctx->lds,
				 indices, 2, "");

	if (ctx->options->family >= CHIP_TONGA) {
		args[0] = LLVMBuildMul(ctx->builder, tl_tid,
				       LLVMConstInt(ctx->i32, 4, false), "");
		args[1] = src0;
		tl = emit_llvm_intrinsic(ctx, "llvm.amdgcn.ds.bpermute",
					 ctx->i32, args, 2,
					 LLVMReadNoneAttribute);

		args[0] = LLVMBuildMul(ctx->builder, trbl_tid,
				       LLVMConstInt(ctx->i32, 4, false), "");
		trbl = emit_llvm_intrinsic(ctx, "llvm.amdgcn.ds.bpermute",
					   ctx->i32, args, 2,
					   LLVMReadNoneAttribute);
	} else {
		LLVMBuildStore(ctx->builder, src0, store_ptr);

		tl = LLVMBuildLoad(ctx->builder, load_ptr0, "");
		trbl = LLVMBuildLoad(ctx->builder, load_ptr1, "");
	}
	tl = LLVMBuildBitCast(ctx->builder, tl, ctx->f32, "");
	trbl = LLVMBuildBitCast(ctx->builder, trbl, ctx->f32, "");
	result = LLVMBuildFSub(ctx->builder, trbl, tl, "");
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
	LLVMValueRef indices[2];
	LLVMValueRef store_ptr, load_ptr_x, load_ptr_y, load_ptr_ddx, load_ptr_ddy, temp, temp2;
	LLVMValueRef tl, tr, bl, result[4];
	unsigned c;

	if (!ctx->lds)
		ctx->lds = LLVMAddGlobalInAddressSpace(ctx->module,
						       LLVMArrayType(ctx->i32, 64),
						       "ddxy_lds", LOCAL_ADDR_SPACE);

	indices[0] = ctx->i32zero;
	indices[1] = get_thread_id(ctx);
	store_ptr = LLVMBuildGEP(ctx->builder, ctx->lds,
				 indices, 2, "");

	temp = LLVMBuildAnd(ctx->builder, indices[1],
			    LLVMConstInt(ctx->i32, TID_MASK_LEFT, false), "");

	temp2 = LLVMBuildAnd(ctx->builder, indices[1],
			     LLVMConstInt(ctx->i32, TID_MASK_TOP, false), "");

	indices[1] = temp;
	load_ptr_x = LLVMBuildGEP(ctx->builder, ctx->lds,
				  indices, 2, "");

	indices[1] = temp2;
	load_ptr_y = LLVMBuildGEP(ctx->builder, ctx->lds,
				  indices, 2, "");

	indices[1] = LLVMBuildAdd(ctx->builder, temp,
				  LLVMConstInt(ctx->i32, 1, false), "");
	load_ptr_ddx = LLVMBuildGEP(ctx->builder, ctx->lds,
				   indices, 2, "");

	indices[1] = LLVMBuildAdd(ctx->builder, temp2,
				  LLVMConstInt(ctx->i32, 2, false), "");
	load_ptr_ddy = LLVMBuildGEP(ctx->builder, ctx->lds,
				   indices, 2, "");

	for (c = 0; c < 2; ++c) {
		LLVMValueRef store_val;
		LLVMValueRef c_ll = LLVMConstInt(ctx->i32, c, false);

		store_val = LLVMBuildExtractElement(ctx->builder,
						    interp_ij, c_ll, "");
		LLVMBuildStore(ctx->builder,
			       store_val,
			       store_ptr);

		tl = LLVMBuildLoad(ctx->builder, load_ptr_x, "");
		tl = LLVMBuildBitCast(ctx->builder, tl, ctx->f32, "");

		tr = LLVMBuildLoad(ctx->builder, load_ptr_ddx, "");
		tr = LLVMBuildBitCast(ctx->builder, tr, ctx->f32, "");

		result[c] = LLVMBuildFSub(ctx->builder, tr, tl, "");

		tl = LLVMBuildLoad(ctx->builder, load_ptr_y, "");
		tl = LLVMBuildBitCast(ctx->builder, tl, ctx->f32, "");

		bl = LLVMBuildLoad(ctx->builder, load_ptr_ddy, "");
		bl = LLVMBuildBitCast(ctx->builder, bl, ctx->f32, "");

		result[c + 2] = LLVMBuildFSub(ctx->builder, bl, tl, "");
	}

	return build_gather_values(ctx, result, 4);
}

static LLVMValueRef emit_fdiv(struct nir_to_llvm_context *ctx,
			      LLVMValueRef num,
			      LLVMValueRef den)
{
	LLVMValueRef ret = LLVMBuildFDiv(ctx->builder, num, den, "");

	if (!LLVMIsConstant(ret))
		LLVMSetMetadata(ret, ctx->fpmath_md_kind, ctx->fpmath_md_2p5_ulp);
	return ret;
}

static void visit_alu(struct nir_to_llvm_context *ctx, nir_alu_instr *instr)
{
	LLVMValueRef src[4], result = NULL;
	unsigned num_components = instr->dest.dest.ssa.num_components;
	unsigned src_components;

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
		result = emit_fdiv(ctx, src[0], src[1]);
		result = emit_intrin_1f_param(ctx, "llvm.floor.f32", result);
		result = LLVMBuildFMul(ctx->builder, src[1] , result, "");
		result = LLVMBuildFSub(ctx->builder, src[0], result, "");
		break;
	case nir_op_frem:
		src[0] = to_float(ctx, src[0]);
		src[1] = to_float(ctx, src[1]);
		result = LLVMBuildFRem(ctx->builder, src[0], src[1], "");
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
		result = emit_fdiv(ctx, src[0], src[1]);
		break;
	case nir_op_frcp:
		src[0] = to_float(ctx, src[0]);
		result = emit_fdiv(ctx, ctx->f32one, src[0]);
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
		result = emit_intrin_1f_param(ctx, "llvm.fabs.f32", src[0]);
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
		result = emit_intrin_1f_param(ctx, "llvm.floor.f32", src[0]);
		break;
	case nir_op_ftrunc:
		result = emit_intrin_1f_param(ctx, "llvm.trunc.f32", src[0]);
		break;
	case nir_op_fceil:
		result = emit_intrin_1f_param(ctx, "llvm.ceil.f32", src[0]);
		break;
	case nir_op_fround_even:
		result = emit_intrin_1f_param(ctx, "llvm.rint.f32", src[0]);
		break;
	case nir_op_ffract:
		result = emit_ffract(ctx, src[0]);
		break;
	case nir_op_fsin:
		result = emit_intrin_1f_param(ctx, "llvm.sin.f32", src[0]);
		break;
	case nir_op_fcos:
		result = emit_intrin_1f_param(ctx, "llvm.cos.f32", src[0]);
		break;
	case nir_op_fsqrt:
		result = emit_intrin_1f_param(ctx, "llvm.sqrt.f32", src[0]);
		break;
	case nir_op_fexp2:
		result = emit_intrin_1f_param(ctx, "llvm.exp2.f32", src[0]);
		break;
	case nir_op_flog2:
		result = emit_intrin_1f_param(ctx, "llvm.log2.f32", src[0]);
		break;
	case nir_op_frsq:
		result = emit_intrin_1f_param(ctx, "llvm.sqrt.f32", src[0]);
		result = emit_fdiv(ctx, ctx->f32one, result);
		break;
	case nir_op_fpow:
		result = emit_intrin_2f_param(ctx, "llvm.pow.f32", src[0], src[1]);
		break;
	case nir_op_fmax:
		result = emit_intrin_2f_param(ctx, "llvm.maxnum.f32", src[0], src[1]);
		break;
	case nir_op_fmin:
		result = emit_intrin_2f_param(ctx, "llvm.minnum.f32", src[0], src[1]);
		break;
	case nir_op_ffma:
		result = emit_intrin_3f_param(ctx, "llvm.fma.f32", src[0], src[1], src[2]);
		break;
	case nir_op_ibitfield_extract:
		result = emit_bitfield_extract(ctx, "llvm.AMDGPU.bfe.i32", src);
		break;
	case nir_op_ubitfield_extract:
		result = emit_bitfield_extract(ctx, "llvm.AMDGPU.bfe.u32", src);
		break;
	case nir_op_bitfield_insert:
		result = emit_bitfield_insert(ctx, src[0], src[1], src[2], src[3]);
		break;
	case nir_op_bitfield_reverse:
		result = emit_llvm_intrinsic(ctx, "llvm.bitreverse.i32", ctx->i32, src, 1, LLVMReadNoneAttribute);
		break;
	case nir_op_bit_count:
		result = emit_llvm_intrinsic(ctx, "llvm.ctpop.i32", ctx->i32, src, 1, LLVMReadNoneAttribute);
		break;
	case nir_op_vec2:
	case nir_op_vec3:
	case nir_op_vec4:
		for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++)
			src[i] = to_integer(ctx, src[i]);
		result = build_gather_values(ctx, src, num_components);
		break;
	case nir_op_f2i:
		src[0] = to_float(ctx, src[0]);
		result = LLVMBuildFPToSI(ctx->builder, src[0], ctx->i32, "");
		break;
	case nir_op_f2u:
		src[0] = to_float(ctx, src[0]);
		result = LLVMBuildFPToUI(ctx->builder, src[0], ctx->i32, "");
		break;
	case nir_op_i2f:
		result = LLVMBuildSIToFP(ctx->builder, src[0], ctx->f32, "");
		break;
	case nir_op_u2f:
		result = LLVMBuildUIToFP(ctx->builder, src[0], ctx->f32, "");
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
		result = emit_ddxy(ctx, instr, src[0]);
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
emit_llvm_intrinsic(struct nir_to_llvm_context *ctx, const char *name,
                    LLVMTypeRef return_type, LLVMValueRef *params,
                    unsigned param_count, LLVMAttribute attribs)
{
	LLVMValueRef function;

	function = LLVMGetNamedFunction(ctx->module, name);
	if (!function) {
		LLVMTypeRef param_types[32], function_type;
		unsigned i;

		assert(param_count <= 32);

		for (i = 0; i < param_count; ++i) {
			assert(params[i]);
			param_types[i] = LLVMTypeOf(params[i]);
		}
		function_type =
		    LLVMFunctionType(return_type, param_types, param_count, 0);
		function = LLVMAddFunction(ctx->module, name, function_type);

		LLVMSetFunctionCallConv(function, LLVMCCallConv);
		LLVMSetLinkage(function, LLVMExternalLinkage);

		LLVMAddFunctionAttr(function, attribs | LLVMNoUnwindAttribute);
	}
	return LLVMBuildCall(ctx->builder, function, params, param_count, "");
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
		size = emit_llvm_intrinsic(ctx, "llvm.SI.getresinfo.i32", ctx->v4i32,
					   txq_args, txq_arg_count,
					   LLVMReadNoneAttribute);

		for (c = 0; c < 2; c++) {
			half_texel[c] = LLVMBuildExtractElement(ctx->builder, size,
								ctx->i32zero, "");
			half_texel[c] = LLVMBuildUIToFP(ctx->builder, half_texel[c], ctx->f32, "");
			half_texel[c] = emit_fdiv(ctx, ctx->f32one, half_texel[c]);
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
	return emit_llvm_intrinsic(ctx, intr_name, tinfo->dst_type, tinfo->args, tinfo->arg_count,
				   LLVMReadNoneAttribute | LLVMNoUnwindAttribute);

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
	return emit_llvm_intrinsic(ctx, intr_name, tinfo->dst_type, tinfo->args, tinfo->arg_count,
				   LLVMReadNoneAttribute | LLVMNoUnwindAttribute);

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

	LLVMValueRef indices[] = {ctx->i32zero, offset};
	desc_ptr = LLVMBuildGEP(ctx->builder, desc_ptr, indices, 2, "");
	desc_ptr = cast_ptr(ctx, desc_ptr, ctx->v4i32);
	LLVMSetMetadata(desc_ptr, ctx->uniform_md_kind, ctx->empty_md);

	return LLVMBuildLoad(ctx->builder, desc_ptr, "");
}

static LLVMValueRef visit_load_push_constant(struct nir_to_llvm_context *ctx,
                                             nir_intrinsic_instr *instr)
{
	LLVMValueRef ptr;

	LLVMValueRef indices[] = {ctx->i32zero, get_src(ctx, instr->src[0])};
	ptr = LLVMBuildGEP(ctx->builder, ctx->push_constants, indices, 2, "");
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
	LLVMTypeRef data_type = ctx->f32;
	unsigned writemask = nir_intrinsic_write_mask(instr);
	LLVMValueRef base_data, base_offset;
	LLVMValueRef params[6];

	if (ctx->stage == MESA_SHADER_FRAGMENT)
		ctx->shader_info->fs.writes_memory = true;

	params[1] = get_src(ctx, instr->src[1]);
	params[2] = LLVMConstInt(ctx->i32, 0, false); /* vindex */
	params[4] = LLVMConstInt(ctx->i1, 0, false);  /* glc */
	params[5] = LLVMConstInt(ctx->i1, 0, false);  /* slc */

	if (instr->num_components > 1)
		data_type = LLVMVectorType(ctx->f32, instr->num_components);

	base_data = to_float(ctx, get_src(ctx, instr->src[0]));
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
		emit_llvm_intrinsic(ctx, store_name,
				    LLVMVoidTypeInContext(ctx->context), params, 6, 0);
	}
}

static LLVMValueRef visit_atomic_ssbo(struct nir_to_llvm_context *ctx,
                                      nir_intrinsic_instr *instr)
{
	const char *name;
	LLVMValueRef params[5];
	int arg_count = 0;
	if (ctx->stage == MESA_SHADER_FRAGMENT)
		ctx->shader_info->fs.writes_memory = true;

	if (instr->intrinsic == nir_intrinsic_ssbo_atomic_comp_swap) {
		params[arg_count++] = get_src(ctx, instr->src[3]);
	}
	params[arg_count++] = get_src(ctx, instr->src[2]);
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

	return emit_llvm_intrinsic(ctx, name, ctx->i32, params, arg_count, 0);
}

static LLVMValueRef visit_load_buffer(struct nir_to_llvm_context *ctx,
                                      nir_intrinsic_instr *instr)
{
	const char *load_name;
	LLVMTypeRef data_type = ctx->f32;
	if (instr->num_components == 3)
		data_type = LLVMVectorType(ctx->f32, 4);
	else if (instr->num_components > 1)
		data_type = LLVMVectorType(ctx->f32, instr->num_components);

	if (instr->num_components == 4 || instr->num_components == 3)
		load_name = "llvm.amdgcn.buffer.load.v4f32";
	else if (instr->num_components == 2)
		load_name = "llvm.amdgcn.buffer.load.v2f32";
	else if (instr->num_components == 1)
		load_name = "llvm.amdgcn.buffer.load.f32";
	else
		abort();

	LLVMValueRef params[] = {
	    get_src(ctx, instr->src[0]),
	    LLVMConstInt(ctx->i32, 0, false),
	    get_src(ctx, instr->src[1]),
	    LLVMConstInt(ctx->i1, 0, false),
	    LLVMConstInt(ctx->i1, 0, false),
	};

	LLVMValueRef ret =
	    emit_llvm_intrinsic(ctx, load_name, data_type, params, 5, 0);

	if (instr->num_components == 3)
		ret = trim_vector(ctx, ret, 3);

	return LLVMBuildBitCast(ctx->builder, ret,
	                        get_def_type(ctx, &instr->dest.ssa), "");
}

static void
radv_get_deref_offset(struct nir_to_llvm_context *ctx, nir_deref *tail,
                      bool vs_in, unsigned *const_out, LLVMValueRef *indir_out)
{
	unsigned const_offset = 0;
	LLVMValueRef offset = NULL;


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

static LLVMValueRef visit_load_var(struct nir_to_llvm_context *ctx,
				   nir_intrinsic_instr *instr)
{
	LLVMValueRef values[4];
	int idx = instr->variables[0]->var->data.driver_location;
	int ve = instr->dest.ssa.num_components;
	LLVMValueRef indir_index;
	unsigned const_index;
	switch (instr->variables[0]->var->data.mode) {
	case nir_var_shader_in:
		radv_get_deref_offset(ctx, &instr->variables[0]->deref,
				      ctx->stage == MESA_SHADER_VERTEX,
				      &const_index, &indir_index);
		for (unsigned chan = 0; chan < ve; chan++) {
			if (indir_index) {
				unsigned count = glsl_count_attribute_slots(
						instr->variables[0]->var->type,
						ctx->stage == MESA_SHADER_VERTEX);
				LLVMValueRef tmp_vec = build_gather_values_extended(
						ctx, ctx->inputs + idx + chan, count,
						4, false);

				values[chan] = LLVMBuildExtractElement(ctx->builder,
								       tmp_vec,
								       indir_index, "");
			} else
				values[chan] = ctx->inputs[idx + chan + const_index * 4];
		}
		return to_integer(ctx, build_gather_values(ctx, values, ve));
		break;
	case nir_var_local:
		radv_get_deref_offset(ctx, &instr->variables[0]->deref, false,
				      &const_index, &indir_index);
		for (unsigned chan = 0; chan < ve; chan++) {
			if (indir_index) {
				unsigned count = glsl_count_attribute_slots(
					instr->variables[0]->var->type, false);
				LLVMValueRef tmp_vec = build_gather_values_extended(
						ctx, ctx->locals + idx + chan, count,
						4, true);

				values[chan] = LLVMBuildExtractElement(ctx->builder,
								       tmp_vec,
								       indir_index, "");
			} else {
				values[chan] = LLVMBuildLoad(ctx->builder, ctx->locals[idx + chan + const_index * 4], "");
			}
		}
		return to_integer(ctx, build_gather_values(ctx, values, ve));
	case nir_var_shader_out:
		radv_get_deref_offset(ctx, &instr->variables[0]->deref, false,
				      &const_index, &indir_index);
		for (unsigned chan = 0; chan < ve; chan++) {
			if (indir_index) {
				unsigned count = glsl_count_attribute_slots(
						instr->variables[0]->var->type, false);
				LLVMValueRef tmp_vec = build_gather_values_extended(
						ctx, ctx->outputs + idx + chan, count,
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
		return to_integer(ctx, build_gather_values(ctx, values, ve));
	case nir_var_shared: {
		radv_get_deref_offset(ctx, &instr->variables[0]->deref, false,
				      &const_index, &indir_index);
		LLVMValueRef ptr = get_shared_memory_ptr(ctx, idx, ctx->i32);
		LLVMValueRef derived_ptr;
		LLVMValueRef index = ctx->i32zero;
		if (indir_index)
			index = LLVMBuildAdd(ctx->builder, index, indir_index, "");
		derived_ptr = LLVMBuildGEP(ctx->builder, ptr, &index, 1, "");

		return to_integer(ctx, LLVMBuildLoad(ctx->builder, derived_ptr, ""));
		break;
	}
	default:
		break;
	}
	return NULL;
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
	switch (instr->variables[0]->var->data.mode) {
	case nir_var_shader_out:
		radv_get_deref_offset(ctx, &instr->variables[0]->deref, false,
				      &const_index, &indir_index);
		for (unsigned chan = 0; chan < 4; chan++) {
			int stride = 4;
			if (!(writemask & (1 << chan)))
				continue;
			if (get_llvm_num_components(src) == 1)
				value = src;
			else
				value = LLVMBuildExtractElement(ctx->builder, src,
								LLVMConstInt(ctx->i32,
									     chan, false),
								"");

			if (instr->variables[0]->var->data.location == VARYING_SLOT_CLIP_DIST0 ||
			    instr->variables[0]->var->data.location == VARYING_SLOT_CULL_DIST0)
				stride = 1;
			if (indir_index) {
				unsigned count = glsl_count_attribute_slots(
						instr->variables[0]->var->type, false);
				LLVMValueRef tmp_vec = build_gather_values_extended(
						ctx, ctx->outputs + idx + chan, count,
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
		radv_get_deref_offset(ctx, &instr->variables[0]->deref, false,
				      &const_index, &indir_index);
		for (unsigned chan = 0; chan < 4; chan++) {
			if (!(writemask & (1 << chan)))
				continue;

			if (get_llvm_num_components(src) == 1)
				value = src;
			else
				value = LLVMBuildExtractElement(ctx->builder, src,
								LLVMConstInt(ctx->i32, chan, false), "");
			if (indir_index) {
				unsigned count = glsl_count_attribute_slots(
					instr->variables[0]->var->type, false);
				LLVMValueRef tmp_vec = build_gather_values_extended(
					ctx, ctx->locals + idx + chan, count,
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
		LLVMValueRef ptr;
		radv_get_deref_offset(ctx, &instr->variables[0]->deref, false,
				      &const_index, &indir_index);

		ptr = get_shared_memory_ptr(ctx, idx, ctx->i32);
		LLVMValueRef index = ctx->i32zero;
		LLVMValueRef derived_ptr;

		if (indir_index)
			index = LLVMBuildAdd(ctx->builder, index, indir_index, "");
		derived_ptr = LLVMBuildGEP(ctx->builder, ptr, &index, 1, "");
		LLVMBuildStore(ctx->builder,
			       to_integer(ctx, src), derived_ptr);
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
	case GLSL_SAMPLER_DIM_3D:
	case GLSL_SAMPLER_DIM_CUBE:
		return 3;
	case GLSL_SAMPLER_DIM_RECT:
	case GLSL_SAMPLER_DIM_SUBPASS:
		return 2;
	default:
		break;
	}
	return 0;
}

static LLVMValueRef get_image_coords(struct nir_to_llvm_context *ctx,
				     nir_intrinsic_instr *instr, bool add_frag_pos)
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
	int count;
	count = image_type_to_components_count(glsl_get_sampler_dim(type),
					       glsl_sampler_type_is_array(type));

	if (count == 1) {
		if (instr->src[0].ssa->num_components)
			res = LLVMBuildExtractElement(ctx->builder, src0, masks[0], "");
		else
			res = src0;
	} else {
		int chan;
		for (chan = 0; chan < count; ++chan) {
			coords[chan] = LLVMBuildExtractElement(ctx->builder, src0, masks[chan], "");
		}

		if (add_frag_pos) {
			for (chan = 0; chan < count; ++chan)
				coords[chan] = LLVMBuildAdd(ctx->builder, coords[chan], LLVMBuildFPToUI(ctx->builder, ctx->frag_pos[chan], ctx->i32, ""), "");
		}
		if (count == 3) {
			coords[3] = LLVMGetUndef(ctx->i32);
			count = 4;
		}
		res = build_gather_values(ctx, coords, count);
	}
	return res;
}

static void build_type_name_for_intr(
        LLVMTypeRef type,
        char *buf, unsigned bufsize)
{
        LLVMTypeRef elem_type = type;

        assert(bufsize >= 8);

        if (LLVMGetTypeKind(type) == LLVMVectorTypeKind) {
                int ret = snprintf(buf, bufsize, "v%u",
                                        LLVMGetVectorSize(type));
                if (ret < 0) {
                        char *type_name = LLVMPrintTypeToString(type);
                        fprintf(stderr, "Error building type name for: %s\n",
                                type_name);
                        return;
                }
                elem_type = LLVMGetElementType(type);
                buf += ret;
                bufsize -= ret;
        }
        switch (LLVMGetTypeKind(elem_type)) {
        default: break;
        case LLVMIntegerTypeKind:
                snprintf(buf, bufsize, "i%d", LLVMGetIntTypeWidth(elem_type));
                break;
        case LLVMFloatTypeKind:
                snprintf(buf, bufsize, "f32");
                break;
        case LLVMDoubleTypeKind:
                snprintf(buf, bufsize, "f64");
                break;
        }
}

static void get_image_intr_name(const char *base_name,
                                LLVMTypeRef data_type,
                                LLVMTypeRef coords_type,
                                LLVMTypeRef rsrc_type,
                                char *out_name, unsigned out_len)
{
        char coords_type_name[8];

        build_type_name_for_intr(coords_type, coords_type_name,
                            sizeof(coords_type_name));

        if (HAVE_LLVM <= 0x0309) {
                snprintf(out_name, out_len, "%s.%s", base_name, coords_type_name);
        } else {
                char data_type_name[8];
                char rsrc_type_name[8];

                build_type_name_for_intr(data_type, data_type_name,
                                        sizeof(data_type_name));
                build_type_name_for_intr(rsrc_type, rsrc_type_name,
                                        sizeof(rsrc_type_name));
                snprintf(out_name, out_len, "%s.%s.%s.%s", base_name,
                         data_type_name, coords_type_name, rsrc_type_name);
        }
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
		res = emit_llvm_intrinsic(ctx, "llvm.amdgcn.buffer.load.format.v4f32", ctx->v4f32,
					  params, 5, 0);

		res = trim_vector(ctx, res, instr->dest.ssa.num_components);
		res = to_integer(ctx, res);
	} else {
		bool is_da = glsl_sampler_type_is_array(type) ||
			     glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_CUBE;
		bool add_frag_pos = glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_SUBPASS;
		LLVMValueRef da = is_da ? ctx->i32one : ctx->i32zero;
		LLVMValueRef glc = LLVMConstInt(ctx->i1, 0, false);
		LLVMValueRef slc = LLVMConstInt(ctx->i1, 0, false);

		params[0] = get_image_coords(ctx, instr, add_frag_pos);
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

		res = emit_llvm_intrinsic(ctx, intrinsic_name, ctx->v4f32,
					params, 7, LLVMReadOnlyAttribute);
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
		emit_llvm_intrinsic(ctx, "llvm.amdgcn.buffer.store.format.v4f32", ctx->voidt,
				    params, 6, 0);
	} else {
		bool is_da = glsl_sampler_type_is_array(type) ||
			     glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_CUBE;
		LLVMValueRef da = is_da ? i1true : i1false;
		LLVMValueRef glc = i1false;
		LLVMValueRef slc = i1false;

		params[0] = to_float(ctx, get_src(ctx, instr->src[2]));
		params[1] = get_image_coords(ctx, instr, false); /* coords */
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

		emit_llvm_intrinsic(ctx, intrinsic_name, ctx->voidt,
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

		coords = params[param_count++] = get_image_coords(ctx, instr, false);
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
	return emit_llvm_intrinsic(ctx, intrinsic_name, ctx->i32, params, param_count, 0);
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

	res = emit_llvm_intrinsic(ctx, "llvm.SI.getresinfo.i32", ctx->v4i32,
				  params, 10, LLVMReadNoneAttribute);

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
	emit_llvm_intrinsic(ctx, "llvm.amdgcn.s.waitcnt",
			    ctx->voidt, args, 1, 0);
}

static void emit_barrier(struct nir_to_llvm_context *ctx)
{
	// TODO tess
	emit_llvm_intrinsic(ctx, "llvm.amdgcn.s.barrier",
			    ctx->voidt, NULL, 0, 0);
}

static LLVMValueRef
visit_load_local_invocation_index(struct nir_to_llvm_context *ctx)
{
	LLVMValueRef result;
	LLVMValueRef thread_id = get_thread_id(ctx);
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

	result[0] = build_indexed_load_const(ctx, ctx->sample_positions, offset0);
	result[1] = build_indexed_load_const(ctx, ctx->sample_positions, offset1);

	return build_gather_values(ctx, result, 2);
}

static LLVMValueRef visit_interp(struct nir_to_llvm_context *ctx,
				 nir_intrinsic_instr *instr)
{
	LLVMValueRef result[2];
	LLVMValueRef interp_param, attr_number;
	unsigned location;
	unsigned chan;
	LLVMValueRef src_c0, src_c1;
	const char *intr_name;
	LLVMValueRef src0;
	int input_index = instr->variables[0]->var->data.location - VARYING_SLOT_VAR0;
	switch (instr->intrinsic) {
	case nir_intrinsic_interp_var_at_centroid:
		location = INTERP_CENTROID;
		break;
	case nir_intrinsic_interp_var_at_sample:
	case nir_intrinsic_interp_var_at_offset:
		location = INTERP_SAMPLE;
		src0 = get_src(ctx, instr->src[0]);
		break;
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

	if (location == INTERP_SAMPLE) {
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
		interp_param = build_gather_values(ctx, ij_out, 2);

	}
	intr_name = interp_param ? "llvm.SI.fs.interp" : "llvm.SI.fs.constant";
	for (chan = 0; chan < 2; chan++) {
		LLVMValueRef args[4];
		LLVMValueRef llvm_chan = LLVMConstInt(ctx->i32, chan, false);

		args[0] = llvm_chan;
		args[1] = attr_number;
		args[2] = ctx->prim_mask;
		args[3] = interp_param;
		result[chan] = emit_llvm_intrinsic(ctx, intr_name,
						   ctx->f32, args, args[3] ? 4 : 3,
						   LLVMReadNoneAttribute);
	}
	return build_gather_values(ctx, result, 2);
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
	case nir_intrinsic_load_sample_id:
		result = ctx->ancillary;
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
		result = visit_load_buffer(ctx, instr);
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
		emit_llvm_intrinsic(ctx, "llvm.AMDGPU.kilp",
				    LLVMVoidTypeInContext(ctx->context),
				    NULL, 0, 0);
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
	LLVMValueRef indices[2];
	LLVMValueRef index = NULL;

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
	}

	if (deref->deref.child) {
		nir_deref_array *child = (nir_deref_array*)deref->deref.child;

		assert(child->deref_array_type != nir_deref_array_type_wildcard);
		offset += child->base_offset * stride;
		if (child->deref_array_type == nir_deref_array_type_indirect) {
			index = get_src(ctx, child->indirect);
		}
	}

	assert(stride % type_size == 0);

	if (!index)
		index = ctx->i32zero;

	index = LLVMBuildMul(builder, index, LLVMConstInt(ctx->i32, stride / type_size, 0), "");
	indices[0] = ctx->i32zero;
	indices[1] = LLVMConstInt(ctx->i32, offset, 0);
	list = LLVMBuildGEP(builder, list, indices, 2, "");
	list = LLVMBuildPointerCast(builder, list, const_array(type, 0), "");

	return build_indexed_load_const(ctx, list, index);
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
		tinfo->args[0] = build_gather_values(ctx, param, count);
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
	}
	if (fmask_ptr && !instr->sampler && (instr->op == nir_texop_txf_ms ||
					     instr->op == nir_texop_samples_identical))
		*fmask_ptr = get_sampler_desc(ctx, instr->texture, DESC_FMASK);
}

static LLVMValueRef build_cube_intrinsic(struct nir_to_llvm_context *ctx,
					 LLVMValueRef *in)
{

	LLVMValueRef v, cube_vec;

	if (1) {
		LLVMTypeRef f32 = LLVMTypeOf(in[0]);
		LLVMValueRef out[4];

		out[0] = emit_llvm_intrinsic(ctx, "llvm.amdgcn.cubetc",
					     f32, in, 3, LLVMReadNoneAttribute);
		out[1] = emit_llvm_intrinsic(ctx, "llvm.amdgcn.cubesc",
					     f32, in, 3, LLVMReadNoneAttribute);
		out[2] = emit_llvm_intrinsic(ctx, "llvm.amdgcn.cubema",
					     f32, in, 3, LLVMReadNoneAttribute);
		out[3] = emit_llvm_intrinsic(ctx, "llvm.amdgcn.cubeid",
					     f32, in, 3, LLVMReadNoneAttribute);

		return build_gather_values(ctx, out, 4);
	} else {
		LLVMValueRef c[4];
		c[0] = in[0];
		c[1] = in[1];
		c[2] = in[2];
		c[3] = LLVMGetUndef(LLVMTypeOf(in[0]));
		cube_vec = build_gather_values(ctx, c, 4);
		v = emit_llvm_intrinsic(ctx, "llvm.AMDGPU.cube", LLVMTypeOf(cube_vec),
					&cube_vec, 1, LLVMReadNoneAttribute);
	}
	return v;
}

static void cube_to_2d_coords(struct nir_to_llvm_context *ctx,
			      LLVMValueRef *in, LLVMValueRef *out)
{
	LLVMValueRef coords[4];
	LLVMValueRef mad_args[3];
	LLVMValueRef v;
	LLVMValueRef tmp;
	int i;

	v = build_cube_intrinsic(ctx, in);
	for (i = 0; i < 4; i++)
		coords[i] = LLVMBuildExtractElement(ctx->builder, v,
						    LLVMConstInt(ctx->i32, i, false), "");

	coords[2] = emit_llvm_intrinsic(ctx, "llvm.fabs.f32", ctx->f32,
					&coords[2], 1, LLVMReadNoneAttribute);
	coords[2] = emit_fdiv(ctx, ctx->f32one, coords[2]);

	mad_args[1] = coords[2];
	mad_args[2] = LLVMConstReal(ctx->f32, 1.5);
	mad_args[0] = coords[0];

	/* emit MAD */
	tmp = LLVMBuildFMul(ctx->builder, mad_args[0], mad_args[1], "");
	coords[0] = LLVMBuildFAdd(ctx->builder, tmp, mad_args[2], "");

	mad_args[0] = coords[1];

	/* emit MAD */
	tmp = LLVMBuildFMul(ctx->builder, mad_args[0], mad_args[1], "");
	coords[1] = LLVMBuildFAdd(ctx->builder, tmp, mad_args[2], "");

	/* apply xyz = yxw swizzle to cooords */
	out[0] = coords[1];
	out[1] = coords[0];
	out[2] = coords[3];
}

static void emit_prepare_cube_coords(struct nir_to_llvm_context *ctx,
				     LLVMValueRef *coords_arg, int num_coords,
				     bool is_deriv,
				     bool is_array, LLVMValueRef *derivs_arg)
{
	LLVMValueRef coords[4];
	int i;
	cube_to_2d_coords(ctx, coords_arg, coords);

	if (is_deriv && derivs_arg) {
		LLVMValueRef derivs[4];
		int axis;

		/* Convert cube derivatives to 2D derivatives. */
		for (axis = 0; axis < 2; axis++) {
			LLVMValueRef shifted_cube_coords[4], shifted_coords[4];

			/* Shift the cube coordinates by the derivatives to get
			 * the cube coordinates of the "neighboring pixel".
			 */
			for (i = 0; i < 3; i++)
				shifted_cube_coords[i] =
					LLVMBuildFAdd(ctx->builder, coords_arg[i],
						      derivs_arg[axis*3+i], "");
			shifted_cube_coords[3] = LLVMGetUndef(ctx->f32);

			/* Project the shifted cube coordinates onto the face. */
			cube_to_2d_coords(ctx, shifted_cube_coords,
					  shifted_coords);

			/* Subtract both sets of 2D coordinates to get 2D derivatives.
			 * This won't work if the shifted coordinates ended up
			 * in a different face.
			 */
			for (i = 0; i < 2; i++)
				derivs[axis * 2 + i] =
					LLVMBuildFSub(ctx->builder, shifted_coords[i],
						      coords[i], "");
		}

		memcpy(derivs_arg, derivs, sizeof(derivs));
	}

	if (is_array) {
		/* for cube arrays coord.z = coord.w(array_index) * 8 + face */
		/* coords_arg.w component - array_index for cube arrays */
		LLVMValueRef tmp = LLVMBuildFMul(ctx->builder, coords_arg[3], LLVMConstReal(ctx->f32, 8.0), "");
		coords[2] = LLVMBuildFAdd(ctx->builder, tmp, coords[2], "");
	}

	memcpy(coords_arg, coords, sizeof(coords));
}

static void visit_tex(struct nir_to_llvm_context *ctx, nir_tex_instr *instr)
{
	LLVMValueRef result = NULL;
	struct ac_tex_info tinfo = { 0 };
	unsigned dmask = 0xf;
	LLVMValueRef address[16];
	LLVMValueRef coords[5];
	LLVMValueRef coord = NULL, lod = NULL, comparitor = NULL, bias, offsets = NULL;
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
		case nir_tex_src_comparitor:
			comparitor = get_src(ctx, instr->src[i].src);
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

	if (instr->op == nir_texop_texture_samples) {
		LLVMValueRef res, samples;
		res = LLVMBuildBitCast(ctx->builder, res_ptr, ctx->v8i32, "");
		samples = LLVMBuildExtractElement(ctx->builder, res,
						  LLVMConstInt(ctx->i32, 3, false), "");
		samples = LLVMBuildLShr(ctx->builder, samples,
					LLVMConstInt(ctx->i32, 16, false), "");
		samples = LLVMBuildAnd(ctx->builder, samples,
				       LLVMConstInt(ctx->i32, 0xf, false), "");
		samples = LLVMBuildShl(ctx->builder, ctx->i32one,
				       samples, "");

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
	if (instr->is_shadow && comparitor) {
		address[count++] = llvm_extract_elem(ctx, comparitor, 0);
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
		emit_prepare_cube_coords(ctx, coords, instr->coord_components, instr->op == nir_texop_txd, instr->is_array, derivs);
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
		if (instr->coord_components > 1)
			address[count++] = coords[1];
		if (instr->coord_components > 2) {
			/* This seems like a bit of a hack - but it passes Vulkan CTS with it */
			if (instr->sampler_dim != GLSL_SAMPLER_DIM_3D && instr->op != nir_texop_txf) {
				coords[2] = to_float(ctx, coords[2]);
				coords[2] = emit_llvm_intrinsic(ctx, "llvm.rint.f32", ctx->f32, &coords[2],
								1, 0);
				coords[2] = to_integer(ctx, coords[2]);
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
		address[count++] = lod;
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
	if (instr->sampler_dim == GLSL_SAMPLER_DIM_MS) {
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
		LLVMValueRef four = LLVMConstInt(ctx->i32, 4, false);
		LLVMValueRef F = LLVMConstInt(ctx->i32, 0xf, false);

		LLVMValueRef fmask = LLVMBuildExtractElement(ctx->builder,
							     result,
							     ctx->i32zero, "");

		unsigned sample_chan = instr->is_array ? 3 : 2;

		LLVMValueRef sample_index4 =
			LLVMBuildMul(ctx->builder, address[sample_chan], four, "");
		LLVMValueRef shifted_fmask =
			LLVMBuildLShr(ctx->builder, fmask, sample_index4, "");
		LLVMValueRef final_sample =
			LLVMBuildAnd(ctx->builder, shifted_fmask, F, "");

		/* Don't rewrite the sample index if WORD1.DATA_FORMAT of the FMASK
		 * resource descriptor is 0 (invalid),
		 */
		LLVMValueRef fmask_desc =
			LLVMBuildBitCast(ctx->builder, fmask_ptr,
					 ctx->v8i32, "");

		LLVMValueRef fmask_word1 =
			LLVMBuildExtractElement(ctx->builder, fmask_desc,
						ctx->i32one, "");

		LLVMValueRef word1_is_nonzero =
			LLVMBuildICmp(ctx->builder, LLVMIntNE,
				      fmask_word1, ctx->i32zero, "");

		/* Replace the MSAA sample index. */
		address[sample_chan] =
			LLVMBuildSelect(ctx->builder, word1_is_nonzero,
					final_sample, address[sample_chan], "");
	}

	if (offsets && instr->op == nir_texop_txf) {
		nir_const_value *const_offset =
			nir_src_as_const_value(instr->src[const_src].src);

		assert(const_offset);
		if (instr->coord_components > 2)
			address[2] = LLVMBuildAdd(ctx->builder,
						  address[2], LLVMConstInt(ctx->i32, const_offset->i32[2], false), "");
		if (instr->coord_components > 1)
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
	else if (instr->op == nir_texop_txs &&
		 instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE &&
		 instr->is_array) {
		LLVMValueRef two = LLVMConstInt(ctx->i32, 2, false);
		LLVMValueRef six = LLVMConstInt(ctx->i32, 6, false);
		LLVMValueRef z = LLVMBuildExtractElement(ctx->builder, result, two, "");
		z = LLVMBuildSDiv(ctx->builder, z, six, "");
		result = LLVMBuildInsertElement(ctx->builder, result, z, two, "");
	}

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

		t_list = build_indexed_load_const(ctx, t_list_ptr, t_offset);
		args[0] = t_list;
		args[1] = LLVMConstInt(ctx->i32, 0, false);
		args[2] = buffer_index;
		input = emit_llvm_intrinsic(ctx,
			"llvm.SI.vs.load.input", ctx->v4f32, args, 3,
			LLVMReadNoneAttribute | LLVMNoUnwindAttribute);

		for (unsigned chan = 0; chan < 4; chan++) {
			LLVMValueRef llvm_chan = LLVMConstInt(ctx->i32, chan, false);
			ctx->inputs[radeon_llvm_reg_index_soa(idx, chan)] =
				to_integer(ctx, LLVMBuildExtractElement(ctx->builder,
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
	const char *intr_name;
	LLVMValueRef attr_number;
	unsigned chan;

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
	intr_name = interp_param ? "llvm.SI.fs.interp" : "llvm.SI.fs.constant";

	for (chan = 0; chan < 4; chan++) {
		LLVMValueRef args[4];
		LLVMValueRef llvm_chan = LLVMConstInt(ctx->i32, chan, false);

		args[0] = llvm_chan;
		args[1] = attr_number;
		args[2] = prim_mask;
		args[3] = interp_param;
		result[chan] = emit_llvm_intrinsic(ctx, intr_name,
						   ctx->f32, args, args[3] ? 4 : 3,
						  LLVMReadNoneAttribute | LLVMNoUnwindAttribute);
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

	if (glsl_get_base_type(glsl_without_array(variable->type)) == GLSL_TYPE_FLOAT)
		interp = lookup_interp_param(ctx, variable->data.interpolation, INTERP_CENTER);
	else
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

		if (i >= VARYING_SLOT_VAR0 || i == VARYING_SLOT_PNTC) {
			interp_param = *inputs;
			interp_fs_input(ctx, index, interp_param, ctx->prim_mask,
					inputs);

			if (!interp_param)
				ctx->shader_info->fs.flat_shaded_mask |= 1u << index;
			++index;
		} else if (i == VARYING_SLOT_POS) {
			for(int i = 0; i < 3; ++i)
				inputs[i] = ctx->frag_pos[i];

			inputs[3] = emit_fdiv(ctx, ctx->f32one, ctx->frag_pos[3]);
		}
	}
	ctx->shader_info->fs.num_interp = index;
	if (ctx->input_mask & (1 << VARYING_SLOT_PNTC))
		ctx->shader_info->fs.has_pcoord = true;
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
	int idx = variable->data.location;
	unsigned attrib_count = glsl_count_attribute_slots(variable->type, false);

	variable->data.driver_location = idx * 4;

	if (ctx->stage == MESA_SHADER_VERTEX) {

		if (idx == VARYING_SLOT_CLIP_DIST0 ||
		    idx == VARYING_SLOT_CULL_DIST0) {
			int length = glsl_get_length(variable->type);
			if (idx == VARYING_SLOT_CLIP_DIST0) {
				ctx->shader_info->vs.clip_dist_mask = (1 << length) - 1;
				ctx->num_clips = length;
			} else if (idx == VARYING_SLOT_CULL_DIST0) {
				ctx->shader_info->vs.cull_dist_mask = (1 << length) - 1;
				ctx->num_culls = length;
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
	ctx->output_mask |= ((1ull << attrib_count) - 1) << variable->data.location;
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
	v = emit_intrin_2f_param(ctx, "llvm.maxnum.f32", v, LLVMConstReal(ctx->f32, lo));
	return emit_intrin_2f_param(ctx, "llvm.minnum.f32", v, LLVMConstReal(ctx->f32, hi));
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
			 LLVMValueRef *args)
{
	/* Default is 0xf. Adjusted below depending on the format. */
	args[0] = LLVMConstInt(ctx->i32, target != V_008DFC_SQ_EXP_NULL ? 0xf : 0, false);
	/* Specify whether the EXEC mask represents the valid mask */
	args[1] = LLVMConstInt(ctx->i32, 0, false);

	/* Specify whether this is the last export */
	args[2] = LLVMConstInt(ctx->i32, 0, false);
	/* Specify the target we are exporting */
	args[3] = LLVMConstInt(ctx->i32, target, false);

	args[4] = LLVMConstInt(ctx->i32, 0, false); /* COMPR flag */
	args[5] = LLVMGetUndef(ctx->f32);
	args[6] = LLVMGetUndef(ctx->f32);
	args[7] = LLVMGetUndef(ctx->f32);
	args[8] = LLVMGetUndef(ctx->f32);

	if (!values)
		return;

	if (ctx->stage == MESA_SHADER_FRAGMENT && target >= V_008DFC_SQ_EXP_MRT) {
		LLVMValueRef val[4];
		unsigned index = target - V_008DFC_SQ_EXP_MRT;
		unsigned col_format = (ctx->options->key.fs.col_format >> (4 * index)) & 0xf;
		bool is_int8 = (ctx->options->key.fs.is_int8 >> index) & 1;

		switch(col_format) {
		case V_028714_SPI_SHADER_ZERO:
			args[0] = LLVMConstInt(ctx->i32, 0x0, 0);
			args[3] = LLVMConstInt(ctx->i32, V_008DFC_SQ_EXP_NULL, 0);
			break;

		case V_028714_SPI_SHADER_32_R:
			args[0] = LLVMConstInt(ctx->i32, 0x1, 0);
			args[5] = values[0];
			break;

		case V_028714_SPI_SHADER_32_GR:
			args[0] = LLVMConstInt(ctx->i32, 0x3, 0);
			args[5] = values[0];
			args[6] = values[1];
			break;

		case V_028714_SPI_SHADER_32_AR:
			args[0] = LLVMConstInt(ctx->i32, 0x9, 0);
			args[5] = values[0];
			args[8] = values[3];
			break;

		case V_028714_SPI_SHADER_FP16_ABGR:
			args[4] = ctx->i32one;

			for (unsigned chan = 0; chan < 2; chan++) {
				LLVMValueRef pack_args[2] = {
					values[2 * chan],
					values[2 * chan + 1]
				};
				LLVMValueRef packed;

				packed = emit_llvm_intrinsic(ctx, "llvm.SI.packf16",
							     ctx->i32, pack_args, 2,
							     LLVMReadNoneAttribute);
				args[chan + 5] = packed;
			}
			break;

		case V_028714_SPI_SHADER_UNORM16_ABGR:
			for (unsigned chan = 0; chan < 4; chan++) {
				val[chan] = emit_float_saturate(ctx, values[chan], 0, 1);
				val[chan] = LLVMBuildFMul(ctx->builder, val[chan],
							LLVMConstReal(ctx->f32, 65535), "");
				val[chan] = LLVMBuildFAdd(ctx->builder, val[chan],
							LLVMConstReal(ctx->f32, 0.5), "");
				val[chan] = LLVMBuildFPToUI(ctx->builder, val[chan],
							ctx->i32, "");
			}

			args[4] = ctx->i32one;
			args[5] = emit_pack_int16(ctx, val[0], val[1]);
			args[6] = emit_pack_int16(ctx, val[2], val[3]);
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

			args[4] = ctx->i32one;
			args[5] = emit_pack_int16(ctx, val[0], val[1]);
			args[6] = emit_pack_int16(ctx, val[2], val[3]);
			break;

		case V_028714_SPI_SHADER_UINT16_ABGR: {
			LLVMValueRef max = LLVMConstInt(ctx->i32, is_int8 ? 255 : 65535, 0);

			for (unsigned chan = 0; chan < 4; chan++) {
				val[chan] = to_integer(ctx, values[chan]);
				val[chan] = emit_minmax_int(ctx, LLVMIntULT, val[chan], max);
			}

			args[4] = ctx->i32one;
			args[5] = emit_pack_int16(ctx, val[0], val[1]);
			args[6] = emit_pack_int16(ctx, val[2], val[3]);
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

			args[4] = ctx->i32one;
			args[5] = emit_pack_int16(ctx, val[0], val[1]);
			args[6] = emit_pack_int16(ctx, val[2], val[3]);
			break;
		}

		default:
		case V_028714_SPI_SHADER_32_ABGR:
			memcpy(&args[5], values, sizeof(values[0]) * 4);
			break;
		}
	} else
		memcpy(&args[5], values, sizeof(values[0]) * 4);

	for (unsigned i = 5; i < 9; ++i)
		args[i] = to_float(ctx, args[i]);
}

static void
handle_vs_outputs_post(struct nir_to_llvm_context *ctx,
		      struct nir_shader *nir)
{
	uint32_t param_count = 0;
	unsigned target;
	unsigned pos_idx, num_pos_exports = 0;
	LLVMValueRef args[9];
	LLVMValueRef pos_args[4][9] = { { 0 } };
	LLVMValueRef psize_value = 0;
	int i;
	const uint64_t clip_mask = ctx->output_mask & ((1ull << VARYING_SLOT_CLIP_DIST0) |
						       (1ull << VARYING_SLOT_CLIP_DIST1) |
						       (1ull << VARYING_SLOT_CULL_DIST0) |
						       (1ull << VARYING_SLOT_CULL_DIST1));

	if (clip_mask) {
		LLVMValueRef slots[8];
		unsigned j;

		if (ctx->shader_info->vs.cull_dist_mask)
			ctx->shader_info->vs.cull_dist_mask <<= ctx->num_clips;

		i = VARYING_SLOT_CLIP_DIST0;
		for (j = 0; j < ctx->num_clips; j++)
			slots[j] = to_float(ctx, LLVMBuildLoad(ctx->builder,
							       ctx->outputs[radeon_llvm_reg_index_soa(i, j)], ""));
		i = VARYING_SLOT_CULL_DIST0;
		for (j = 0; j < ctx->num_culls; j++)
			slots[ctx->num_clips + j] = to_float(ctx, LLVMBuildLoad(ctx->builder,
									   ctx->outputs[radeon_llvm_reg_index_soa(i, j)], ""));

		for (i = ctx->num_clips + ctx->num_culls; i < 8; i++)
			slots[i] = LLVMGetUndef(ctx->f32);

		if (ctx->num_clips + ctx->num_culls > 4) {
			target = V_008DFC_SQ_EXP_POS + 3;
			si_llvm_init_export_args(ctx, &slots[4], target, args);
			memcpy(pos_args[target - V_008DFC_SQ_EXP_POS],
			       args, sizeof(args));
		}

		target = V_008DFC_SQ_EXP_POS + 2;
		si_llvm_init_export_args(ctx, &slots[0], target, args);
		memcpy(pos_args[target - V_008DFC_SQ_EXP_POS],
		       args, sizeof(args));

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
		} else if (i >= VARYING_SLOT_VAR0) {
			ctx->shader_info->vs.export_mask |= 1u << (i - VARYING_SLOT_VAR0);
			target = V_008DFC_SQ_EXP_PARAM + param_count;
			param_count++;
		}

		si_llvm_init_export_args(ctx, values, target, args);

		if (target >= V_008DFC_SQ_EXP_POS &&
		    target <= (V_008DFC_SQ_EXP_POS + 3)) {
			memcpy(pos_args[target - V_008DFC_SQ_EXP_POS],
			       args, sizeof(args));
		} else {
			emit_llvm_intrinsic(ctx,
					    "llvm.SI.export",
					    LLVMVoidTypeInContext(ctx->context),
					    args, 9, 0);
		}
	}

	/* We need to add the position output manually if it's missing. */
	if (!pos_args[0][0]) {
		pos_args[0][0] = LLVMConstInt(ctx->i32, 0xf, false);
		pos_args[0][1] = ctx->i32zero; /* EXEC mask */
		pos_args[0][2] = ctx->i32zero; /* last export? */
		pos_args[0][3] = LLVMConstInt(ctx->i32, V_008DFC_SQ_EXP_POS, false);
		pos_args[0][4] = ctx->i32zero; /* COMPR flag */
		pos_args[0][5] = ctx->f32zero; /* X */
		pos_args[0][6] = ctx->f32zero; /* Y */
		pos_args[0][7] = ctx->f32zero; /* Z */
		pos_args[0][8] = ctx->f32one;  /* W */
	}

	if (ctx->shader_info->vs.writes_pointsize == true) {
		pos_args[1][0] = LLVMConstInt(ctx->i32, (ctx->shader_info->vs.writes_pointsize == true), false); /* writemask */
		pos_args[1][1] = ctx->i32zero;  /* EXEC mask */
		pos_args[1][2] = ctx->i32zero;  /* last export? */
		pos_args[1][3] = LLVMConstInt(ctx->i32, V_008DFC_SQ_EXP_POS + 1, false);
		pos_args[1][4] = ctx->i32zero;  /* COMPR flag */
		pos_args[1][5] = ctx->f32zero; /* X */
		pos_args[1][6] = ctx->f32zero; /* Y */
		pos_args[1][7] = ctx->f32zero; /* Z */
		pos_args[1][8] = ctx->f32zero;  /* W */

		if (ctx->shader_info->vs.writes_pointsize == true)
			pos_args[1][5] = psize_value;
	}
	for (i = 0; i < 4; i++) {
		if (pos_args[i][0])
			num_pos_exports++;
	}

	pos_idx = 0;
	for (i = 0; i < 4; i++) {
		if (!pos_args[i][0])
			continue;

		/* Specify the target we are exporting */
		pos_args[i][3] = LLVMConstInt(ctx->i32, V_008DFC_SQ_EXP_POS + pos_idx++, false);
		if (pos_idx == num_pos_exports)
			pos_args[i][2] = ctx->i32one;
		emit_llvm_intrinsic(ctx,
				    "llvm.SI.export",
				    LLVMVoidTypeInContext(ctx->context),
				    pos_args[i], 9, 0);
	}

	ctx->shader_info->vs.pos_exports = num_pos_exports;
	ctx->shader_info->vs.param_exports = param_count;
}

static void
si_export_mrt_color(struct nir_to_llvm_context *ctx,
		    LLVMValueRef *color, unsigned param, bool is_last)
{
	LLVMValueRef args[9];
	/* Export */
	si_llvm_init_export_args(ctx, color, param,
				 args);

	if (is_last) {
		args[1] = ctx->i32one; /* whether the EXEC mask is valid */
		args[2] = ctx->i32one; /* DONE bit */
	} else if (args[0] == ctx->i32zero)
		return; /* unnecessary NULL export */

	emit_llvm_intrinsic(ctx, "llvm.SI.export",
			    ctx->voidt, args, 9, 0);
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
	emit_llvm_intrinsic(ctx, "llvm.SI.export",
			    ctx->voidt, args, 9, 0);
}

static void
handle_fs_outputs_post(struct nir_to_llvm_context *ctx,
		       struct nir_shader *nir)
{
	unsigned index = 0;
	LLVMValueRef depth = NULL, stencil = NULL, samplemask = NULL;

	for (unsigned i = 0; i < RADEON_LLVM_MAX_OUTPUTS; ++i) {
		LLVMValueRef values[4];
		bool last;
		if (!(ctx->output_mask & (1ull << i)))
			continue;

		last = ctx->output_mask <= ((1ull << (i + 1)) - 1);

		if (i == FRAG_RESULT_DEPTH) {
			ctx->shader_info->fs.writes_z = true;
			depth = to_float(ctx, LLVMBuildLoad(ctx->builder,
							    ctx->outputs[radeon_llvm_reg_index_soa(i, 0)], ""));
		} else if (i == FRAG_RESULT_STENCIL) {
			ctx->shader_info->fs.writes_stencil = true;
			stencil = to_float(ctx, LLVMBuildLoad(ctx->builder,
							      ctx->outputs[radeon_llvm_reg_index_soa(i, 0)], ""));
		} else {
			for (unsigned j = 0; j < 4; j++)
				values[j] = to_float(ctx, LLVMBuildLoad(ctx->builder,
									ctx->outputs[radeon_llvm_reg_index_soa(i, j)], ""));

			si_export_mrt_color(ctx, values, V_008DFC_SQ_EXP_MRT + index, last);
			index++;
		}
	}

	if (depth || stencil)
		si_export_mrt_z(ctx, depth, stencil, samplemask);
	else if (!index)
		si_export_mrt_color(ctx, NULL, V_008DFC_SQ_EXP_NULL, true);

	ctx->shader_info->fs.output_mask = index ? ((1ull << index) - 1) : 0;
}

static void
handle_shader_outputs_post(struct nir_to_llvm_context *ctx,
			   struct nir_shader *nir)
{
	switch (ctx->stage) {
	case MESA_SHADER_VERTEX:
		handle_vs_outputs_post(ctx, nir);
		break;
	case MESA_SHADER_FRAGMENT:
		handle_fs_outputs_post(ctx, nir);
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

static
LLVMModuleRef ac_translate_nir_to_llvm(LLVMTargetMachineRef tm,
                                       struct nir_shader *nir,
                                       struct ac_shader_variant_info *shader_info,
                                       const struct ac_nir_compiler_options *options)
{
	struct nir_to_llvm_context ctx = {0};
	struct nir_function *func;
	ctx.options = options;
	ctx.shader_info = shader_info;
	ctx.context = LLVMContextCreate();
	ctx.module = LLVMModuleCreateWithNameInContext("shader", ctx.context);

	memset(shader_info, 0, sizeof(*shader_info));

	LLVMSetTarget(ctx.module, "amdgcn--");
	setup_types(&ctx);

	ctx.builder = LLVMCreateBuilderInContext(ctx.context);
	ctx.stage = nir->stage;

	create_function(&ctx, nir);

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

			shared_size *= 4;
			var = LLVMAddGlobalInAddressSpace(ctx.module,
							  LLVMArrayType(ctx.i8, shared_size),
							  "compute_lds",
							  LOCAL_ADDR_SPACE);
			LLVMSetAlignment(var, 4);
			ctx.shared_memory = LLVMBuildBitCast(ctx.builder, var, i8p, "");
		}
	}

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

	handle_shader_outputs_post(&ctx, nir);
	LLVMBuildRetVoid(ctx.builder);

	ac_llvm_finalize_module(&ctx);
	free(ctx.locals);
	ralloc_free(ctx.defs);
	ralloc_free(ctx.phis);

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
	if (dump_shader)
		LLVMDumpModule(llvm_module);

	memset(binary, 0, sizeof(*binary));
	int v = ac_llvm_compile(llvm_module, binary, tm);
	if (v) {
		fprintf(stderr, "compile failed\n");
	}

	if (dump_shader)
		fprintf(stderr, "disasm:\n%s\n", binary->disasm_string);

	ac_shader_binary_read_config(binary, config, 0);

	LLVMContextRef ctx = LLVMGetModuleContext(llvm_module);
	LLVMDisposeModule(llvm_module);
	LLVMContextDispose(ctx);

	if (nir->stage == MESA_SHADER_FRAGMENT) {
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
	if (nir->stage == MESA_SHADER_COMPUTE) {
		for (int i = 0; i < 3; ++i)
			shader_info->cs.block_size[i] = nir->info.cs.local_size[i];
	}

	if (nir->stage == MESA_SHADER_FRAGMENT)
		shader_info->fs.early_fragment_test = nir->info.fs.early_fragment_tests;
}
