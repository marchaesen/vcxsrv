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
#include "util/bitscan.h"
#include "util/u_math.h"
#include "ac_shader_abi.h"
#include "ac_shader_util.h"

struct ac_nir_context {
	struct ac_llvm_context ac;
	struct ac_shader_abi *abi;

	gl_shader_stage stage;

	LLVMValueRef *ssa_defs;

	struct hash_table *defs;
	struct hash_table *phis;
	struct hash_table *vars;

	LLVMValueRef main_function;
	LLVMBasicBlockRef continue_block;
	LLVMBasicBlockRef break_block;

	int num_locals;
	LLVMValueRef *locals;
};

static LLVMValueRef get_sampler_desc(struct ac_nir_context *ctx,
				     const nir_deref_var *deref,
				     enum ac_descriptor_type desc_type,
				     const nir_tex_instr *instr,
				     bool image, bool write);

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

static enum ac_image_dim
get_ac_sampler_dim(const struct ac_llvm_context *ctx, enum glsl_sampler_dim dim,
		   bool is_array)
{
	switch (dim) {
	case GLSL_SAMPLER_DIM_1D:
		if (ctx->chip_class >= GFX9)
			return is_array ? ac_image_2darray : ac_image_2d;
		return is_array ? ac_image_1darray : ac_image_1d;
	case GLSL_SAMPLER_DIM_2D:
	case GLSL_SAMPLER_DIM_RECT:
	case GLSL_SAMPLER_DIM_SUBPASS:
	case GLSL_SAMPLER_DIM_EXTERNAL:
		return is_array ? ac_image_2darray : ac_image_2d;
	case GLSL_SAMPLER_DIM_3D:
		return ac_image_3d;
	case GLSL_SAMPLER_DIM_CUBE:
		return ac_image_cube;
	case GLSL_SAMPLER_DIM_MS:
	case GLSL_SAMPLER_DIM_SUBPASS_MS:
		return is_array ? ac_image_2darraymsaa : ac_image_2dmsaa;
	default:
		unreachable("bad sampler dim");
	}
}

static enum ac_image_dim
get_ac_image_dim(const struct ac_llvm_context *ctx, enum glsl_sampler_dim sdim,
		 bool is_array)
{
	enum ac_image_dim dim = get_ac_sampler_dim(ctx, sdim, is_array);

	if (dim == ac_image_cube ||
	    (ctx->chip_class <= VI && dim == ac_image_3d))
		dim = ac_image_2darray;

	return dim;
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
	return nir->ssa_defs[src.ssa->index];
}

static LLVMValueRef
get_memory_ptr(struct ac_nir_context *ctx, nir_src src)
{
	LLVMValueRef ptr = get_src(ctx, src);
	ptr = LLVMBuildGEP(ctx->ac.builder, ctx->ac.lds, &ptr, 1, "");
	int addr_space = LLVMGetPointerAddressSpace(LLVMTypeOf(ptr));

	return LLVMBuildBitCast(ctx->ac.builder, ptr,
				LLVMPointerType(ctx->ac.i32, addr_space), "");
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
	unsigned src_components = ac_get_llvm_num_components(value);
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
						 ac_get_elem_bits(ctx, result_type));
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
						 ac_get_elem_bits(ctx, result_type));
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
						 ac_get_elem_bits(ctx, result_type));
	assert(length < sizeof(name));
	return ac_build_intrinsic(ctx, name, result_type, params, 3, AC_FUNC_ATTR_READNONE);
}

static LLVMValueRef emit_bcsel(struct ac_llvm_context *ctx,
			       LLVMValueRef src0, LLVMValueRef src1, LLVMValueRef src2)
{
	LLVMValueRef v = LLVMBuildICmp(ctx->builder, LLVMIntNE, src0,
				       ctx->i32_0, "");
	return LLVMBuildSelect(ctx->builder, v, ac_to_integer(ctx, src1),
			       ac_to_integer(ctx, src2), "");
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
	LLVMValueRef zero = LLVMConstNull(LLVMTypeOf(src0));
	return LLVMBuildSExt(ctx->builder,
			     LLVMBuildFCmp(ctx->builder, LLVMRealUNE, src0, zero, ""),
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
	LLVMValueRef zero = LLVMConstNull(LLVMTypeOf(src0));
	return LLVMBuildSExt(ctx->builder,
			     LLVMBuildICmp(ctx->builder, LLVMIntNE, src0, zero, ""),
			     ctx->i32, "");
}

static LLVMValueRef emit_f2f16(struct ac_llvm_context *ctx,
			       LLVMValueRef src0)
{
	LLVMValueRef result;
	LLVMValueRef cond = NULL;

	src0 = ac_to_float(ctx, src0);
	result = LLVMBuildFPTrunc(ctx->builder, src0, ctx->f16, "");

	if (ctx->chip_class >= VI) {
		LLVMValueRef args[2];
		/* Check if the result is a denormal - and flush to 0 if so. */
		args[0] = result;
		args[1] = LLVMConstInt(ctx->i32, N_SUBNORMAL | P_SUBNORMAL, false);
		cond = ac_build_intrinsic(ctx, "llvm.amdgcn.class.f16", ctx->i1, args, 2, AC_FUNC_ATTR_READNONE);
	}

	/* need to convert back up to f32 */
	result = LLVMBuildFPExt(ctx->builder, result, ctx->f32, "");

	if (ctx->chip_class >= VI)
		result = LLVMBuildSelect(ctx->builder, cond, ctx->f32_0, result, "");
	else {
		/* for SI/CIK */
		/* 0x38800000 is smallest half float value (2^-14) in 32-bit float,
		 * so compare the result and flush to 0 if it's smaller.
		 */
		LLVMValueRef temp, cond2;
		temp = emit_intrin_1f_param(ctx, "llvm.fabs", ctx->f32, result);
		cond = LLVMBuildFCmp(ctx->builder, LLVMRealUGT,
				     LLVMBuildBitCast(ctx->builder, LLVMConstInt(ctx->i32, 0x38800000, false), ctx->f32, ""),
				     temp, "");
		cond2 = LLVMBuildFCmp(ctx->builder, LLVMRealUNE,
				      temp, ctx->f32_0, "");
		cond = LLVMBuildAnd(ctx->builder, cond, cond2, "");
		result = LLVMBuildSelect(ctx->builder, cond, ctx->f32_0, result, "");
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

	if (op == nir_op_fddx_fine)
		mask = AC_TID_MASK_LEFT;
	else if (op == nir_op_fddy_fine)
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
	case nir_op_cube_face_coord:
	case nir_op_cube_face_index:
		src_components = 3;
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
		result = emit_float_cmp(&ctx->ac, LLVMRealOEQ, src[0], src[1]);
		break;
	case nir_op_fne:
		result = emit_float_cmp(&ctx->ac, LLVMRealUNE, src[0], src[1]);
		break;
	case nir_op_flt:
		result = emit_float_cmp(&ctx->ac, LLVMRealOLT, src[0], src[1]);
		break;
	case nir_op_fge:
		result = emit_float_cmp(&ctx->ac, LLVMRealOGE, src[0], src[1]);
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
		result = ac_build_isign(&ctx->ac, src[0],
					instr->dest.dest.ssa.bit_size);
		break;
	case nir_op_fsign:
		src[0] = ac_to_float(&ctx->ac, src[0]);
		result = ac_build_fsign(&ctx->ac, src[0],
					instr->dest.dest.ssa.bit_size);
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
		src[0] = ac_to_float(&ctx->ac, src[0]);
		result = ac_build_fract(&ctx->ac, src[0],
					instr->dest.dest.ssa.bit_size);
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
	case nir_op_frexp_exp:
		src[0] = ac_to_float(&ctx->ac, src[0]);
		result = ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.frexp.exp.i32.f64",
					    ctx->ac.i32, src, 1, AC_FUNC_ATTR_READNONE);

		break;
	case nir_op_frexp_sig:
		src[0] = ac_to_float(&ctx->ac, src[0]);
		result = ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.frexp.mant.f64",
					    ctx->ac.f64, src, 1, AC_FUNC_ATTR_READNONE);
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
	case nir_op_ldexp:
		src[0] = ac_to_float(&ctx->ac, src[0]);
		if (ac_get_elem_bits(&ctx->ac, LLVMTypeOf(src[0])) == 32)
			result = ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.ldexp.f32", ctx->ac.f32, src, 2, AC_FUNC_ATTR_READNONE);
		else
			result = ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.ldexp.f64", ctx->ac.f64, src, 2, AC_FUNC_ATTR_READNONE);
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
		if (ac_get_elem_bits(&ctx->ac, LLVMTypeOf(src[0])) == 32)
			result = ac_build_intrinsic(&ctx->ac, "llvm.ctpop.i32", ctx->ac.i32, src, 1, AC_FUNC_ATTR_READNONE);
		else {
			result = ac_build_intrinsic(&ctx->ac, "llvm.ctpop.i64", ctx->ac.i64, src, 1, AC_FUNC_ATTR_READNONE);
			result = LLVMBuildTrunc(ctx->ac.builder, result, ctx->ac.i32, "");
		}
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
		src[0] = ac_to_float(&ctx->ac, src[0]);
		result = LLVMBuildFPTrunc(ctx->ac.builder, src[0], ac_to_float_type(&ctx->ac, def_type), "");
		break;
	case nir_op_u2u32:
	case nir_op_u2u64:
		src[0] = ac_to_integer(&ctx->ac, src[0]);
		if (ac_get_elem_bits(&ctx->ac, LLVMTypeOf(src[0])) < ac_get_elem_bits(&ctx->ac, def_type))
			result = LLVMBuildZExt(ctx->ac.builder, src[0], def_type, "");
		else
			result = LLVMBuildTrunc(ctx->ac.builder, src[0], def_type, "");
		break;
	case nir_op_i2i32:
	case nir_op_i2i64:
		src[0] = ac_to_integer(&ctx->ac, src[0]);
		if (ac_get_elem_bits(&ctx->ac, LLVMTypeOf(src[0])) < ac_get_elem_bits(&ctx->ac, def_type))
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
		result = emit_f2f16(&ctx->ac, src[0]);
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
		assert(ac_get_llvm_num_components(src[0]) == 1);
		LLVMValueRef tmp = LLVMBuildBitCast(ctx->ac.builder, src[0],
						    ctx->ac.v2i32,
						    "");
		result = LLVMBuildExtractElement(ctx->ac.builder, tmp,
						 ctx->ac.i32_0, "");
		break;
	}

	case nir_op_unpack_64_2x32_split_y: {
		assert(ac_get_llvm_num_components(src[0]) == 1);
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

	case nir_op_cube_face_coord: {
		src[0] = ac_to_float(&ctx->ac, src[0]);
		LLVMValueRef results[2];
		LLVMValueRef in[3];
		for (unsigned chan = 0; chan < 3; chan++)
			in[chan] = ac_llvm_extract_elem(&ctx->ac, src[0], chan);
		results[0] = ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.cubetc",
						ctx->ac.f32, in, 3, AC_FUNC_ATTR_READNONE);
		results[1] = ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.cubesc",
						ctx->ac.f32, in, 3, AC_FUNC_ATTR_READNONE);
		result = ac_build_gather_values(&ctx->ac, results, 2);
		break;
	}

	case nir_op_cube_face_index: {
		src[0] = ac_to_float(&ctx->ac, src[0]);
		LLVMValueRef in[3];
		for (unsigned chan = 0; chan < 3; chan++)
			in[chan] = ac_llvm_extract_elem(&ctx->ac, src[0], chan);
		result = ac_build_intrinsic(&ctx->ac,  "llvm.amdgcn.cubeid",
						ctx->ac.f32, in, 3, AC_FUNC_ATTR_READNONE);
		break;
	}

	case nir_op_fmin3:
		result = emit_intrin_2f_param(&ctx->ac, "llvm.minnum",
						ac_to_float_type(&ctx->ac, def_type), src[0], src[1]);
		result = emit_intrin_2f_param(&ctx->ac, "llvm.minnum",
						ac_to_float_type(&ctx->ac, def_type), result, src[2]);
		break;
	case nir_op_umin3:
		result = emit_minmax_int(&ctx->ac, LLVMIntULT, src[0], src[1]);
		result = emit_minmax_int(&ctx->ac, LLVMIntULT, result, src[2]);
		break;
	case nir_op_imin3:
		result = emit_minmax_int(&ctx->ac, LLVMIntSLT, src[0], src[1]);
		result = emit_minmax_int(&ctx->ac, LLVMIntSLT, result, src[2]);
		break;
	case nir_op_fmax3:
		result = emit_intrin_2f_param(&ctx->ac, "llvm.maxnum",
						ac_to_float_type(&ctx->ac, def_type), src[0], src[1]);
		result = emit_intrin_2f_param(&ctx->ac, "llvm.maxnum",
						ac_to_float_type(&ctx->ac, def_type), result, src[2]);
		break;
	case nir_op_umax3:
		result = emit_minmax_int(&ctx->ac, LLVMIntUGT, src[0], src[1]);
		result = emit_minmax_int(&ctx->ac, LLVMIntUGT, result, src[2]);
		break;
	case nir_op_imax3:
		result = emit_minmax_int(&ctx->ac, LLVMIntSGT, src[0], src[1]);
		result = emit_minmax_int(&ctx->ac, LLVMIntSGT, result, src[2]);
		break;
	case nir_op_fmed3: {
		LLVMValueRef tmp1 = emit_intrin_2f_param(&ctx->ac, "llvm.minnum",
						ac_to_float_type(&ctx->ac, def_type), src[0], src[1]);
		LLVMValueRef tmp2 = emit_intrin_2f_param(&ctx->ac, "llvm.maxnum",
						ac_to_float_type(&ctx->ac, def_type), src[0], src[1]);
		tmp2 = emit_intrin_2f_param(&ctx->ac, "llvm.minnum",
						ac_to_float_type(&ctx->ac, def_type), tmp2, src[2]);
		result = emit_intrin_2f_param(&ctx->ac, "llvm.maxnum",
						ac_to_float_type(&ctx->ac, def_type), tmp1, tmp2);
		break;
	}
	case nir_op_imed3: {
		LLVMValueRef tmp1 = emit_minmax_int(&ctx->ac, LLVMIntSLT, src[0], src[1]);
		LLVMValueRef tmp2 = emit_minmax_int(&ctx->ac, LLVMIntSGT, src[0], src[1]);
		tmp2 = emit_minmax_int(&ctx->ac, LLVMIntSLT, tmp2, src[2]);
		result = emit_minmax_int(&ctx->ac, LLVMIntSGT, tmp1, tmp2);
		break;
	}
	case nir_op_umed3: {
		LLVMValueRef tmp1 = emit_minmax_int(&ctx->ac, LLVMIntULT, src[0], src[1]);
		LLVMValueRef tmp2 = emit_minmax_int(&ctx->ac, LLVMIntUGT, src[0], src[1]);
		tmp2 = emit_minmax_int(&ctx->ac, LLVMIntULT, tmp2, src[2]);
		result = emit_minmax_int(&ctx->ac, LLVMIntUGT, tmp1, tmp2);
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
		ctx->ssa_defs[instr->dest.dest.ssa.index] = result;
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

	ctx->ssa_defs[instr->def.index] = value;
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

static LLVMValueRef lower_gather4_integer(struct ac_llvm_context *ctx,
					  struct ac_image_args *args,
					  const nir_tex_instr *instr)
{
	enum glsl_base_type stype = glsl_get_sampler_result_type(instr->texture->var->type);
	LLVMValueRef half_texel[2];
	LLVMValueRef compare_cube_wa = NULL;
	LLVMValueRef result;

	//TODO Rect
	{
		struct ac_image_args txq_args = { 0 };

		txq_args.dim = get_ac_sampler_dim(ctx, instr->sampler_dim, instr->is_array);
		txq_args.opcode = ac_image_get_resinfo;
		txq_args.dmask = 0xf;
		txq_args.lod = ctx->i32_0;
		txq_args.resource = args->resource;
		txq_args.attributes = AC_FUNC_ATTR_READNONE;
		LLVMValueRef size = ac_build_image_opcode(ctx, &txq_args);

		for (unsigned c = 0; c < 2; c++) {
			half_texel[c] = LLVMBuildExtractElement(ctx->builder, size,
								LLVMConstInt(ctx->i32, c, false), "");
			half_texel[c] = LLVMBuildUIToFP(ctx->builder, half_texel[c], ctx->f32, "");
			half_texel[c] = ac_build_fdiv(ctx, ctx->f32_1, half_texel[c]);
			half_texel[c] = LLVMBuildFMul(ctx->builder, half_texel[c],
						      LLVMConstReal(ctx->f32, -0.5), "");
		}
	}

	LLVMValueRef orig_coords[2] = { args->coords[0], args->coords[1] };

	for (unsigned c = 0; c < 2; c++) {
		LLVMValueRef tmp;
		tmp = LLVMBuildBitCast(ctx->builder, args->coords[c], ctx->f32, "");
		args->coords[c] = LLVMBuildFAdd(ctx->builder, tmp, half_texel[c], "");
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
		for (unsigned c = 0; c < 2; ++c)
			args->coords[c] = LLVMBuildSelect(
				ctx->builder, compare_cube_wa,
				orig_coords[c], args->coords[c], "");
	}

	args->attributes = AC_FUNC_ATTR_READNONE;
	result = ac_build_image_opcode(ctx, args);

	if (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE) {
		LLVMValueRef tmp, tmp2;

		/* if the cube workaround is in place, f2i the result. */
		for (unsigned c = 0; c < 4; c++) {
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
					struct ac_image_args *args)
{
	if (instr->sampler_dim == GLSL_SAMPLER_DIM_BUF) {
		unsigned mask = nir_ssa_def_components_read(&instr->dest.ssa);

		if (ctx->abi->gfx9_stride_size_workaround) {
			return ac_build_buffer_load_format_gfx9_safe(&ctx->ac,
			                                             args->resource,
			                                             args->coords[0],
			                                             ctx->ac.i32_0,
			                                             util_last_bit(mask),
			                                             false, true);
		} else {
			return ac_build_buffer_load_format(&ctx->ac,
			                                   args->resource,
			                                   args->coords[0],
			                                   ctx->ac.i32_0,
			                                   util_last_bit(mask),
			                                   false, true);
		}
	}

	args->opcode = ac_image_sample;

	switch (instr->op) {
	case nir_texop_txf:
	case nir_texop_txf_ms:
	case nir_texop_samples_identical:
		args->opcode = args->level_zero ||
			       instr->sampler_dim == GLSL_SAMPLER_DIM_MS ?
					ac_image_load : ac_image_load_mip;
		args->level_zero = false;
		break;
	case nir_texop_txs:
	case nir_texop_query_levels:
		args->opcode = ac_image_get_resinfo;
		if (!args->lod)
			args->lod = ctx->ac.i32_0;
		args->level_zero = false;
		break;
	case nir_texop_tex:
		if (ctx->stage != MESA_SHADER_FRAGMENT) {
			assert(!args->lod);
			args->level_zero = true;
		}
		break;
	case nir_texop_tg4:
		args->opcode = ac_image_gather4;
		args->level_zero = true;
		break;
	case nir_texop_lod:
		args->opcode = ac_image_get_lod;
		break;
	default:
		break;
	}

	if (instr->op == nir_texop_tg4 && ctx->ac.chip_class <= VI) {
		enum glsl_base_type stype = glsl_get_sampler_result_type(instr->texture->var->type);
		if (stype == GLSL_TYPE_UINT || stype == GLSL_TYPE_INT) {
			return lower_gather4_integer(&ctx->ac, args, instr);
		}
	}

	args->attributes = AC_FUNC_ATTR_READNONE;
	return ac_build_image_opcode(&ctx->ac, args);
}

static LLVMValueRef visit_vulkan_resource_reindex(struct ac_nir_context *ctx,
                                                  nir_intrinsic_instr *instr)
{
	LLVMValueRef ptr = get_src(ctx, instr->src[0]);
	LLVMValueRef index = get_src(ctx, instr->src[1]);

	LLVMValueRef result = LLVMBuildGEP(ctx->ac.builder, ptr, &index, 1, "");
	LLVMSetMetadata(result, ctx->ac.uniform_md_kind, ctx->ac.empty_md);
	return result;
}

static LLVMValueRef visit_load_push_constant(struct ac_nir_context *ctx,
                                             nir_intrinsic_instr *instr)
{
	LLVMValueRef ptr, addr;

	addr = LLVMConstInt(ctx->ac.i32, nir_intrinsic_base(instr), 0);
	addr = LLVMBuildAdd(ctx->ac.builder, addr,
			    get_src(ctx, instr->src[0]), "");

	ptr = ac_build_gep0(&ctx->ac, ctx->abi->push_constants, addr);
	ptr = ac_cast_ptr(&ctx->ac, ptr, get_def_type(ctx, &instr->dest.ssa));

	return LLVMBuildLoad(ctx->ac.builder, ptr, "");
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
	int elem_size_mult = ac_get_elem_bits(&ctx->ac, LLVMTypeOf(src_data)) / 32;
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
	base_data = ac_trim_vector(&ctx->ac, base_data, instr->num_components);
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
	ret = ac_trim_vector(&ctx->ac, ret, num_components);
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

	LLVMTypeRef dest_type = get_def_type(ctx, &instr->dest.ssa);

	LLVMTypeRef src_component_type;
	if (LLVMGetTypeKind(dest_type) == LLVMVectorTypeKind)
		src_component_type = LLVMGetElementType(dest_type);
	else
		src_component_type = dest_type;

	result = ctx->abi->load_tess_varyings(ctx->abi, src_component_type,
					      vertex_index, indir_index,
					      const_index, location, driver_location,
					      instr->variables[0]->var->data.location_frac,
					      instr->num_components,
					      is_patch, is_compact, load_inputs);
	return LLVMBuildBitCast(ctx->ac.builder, result, dest_type, "");
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
						     instr->variables[0]->var->data.location_frac,
						     instr->num_components, vertex_index, const_index, type);
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
						&ctx->ac, ctx->abi->outputs + idx + chan, count,
						stride, true, true);

				values[chan] = LLVMBuildExtractElement(ctx->ac.builder,
								       tmp_vec,
								       indir_index, "");
			} else {
				values[chan] = LLVMBuildLoad(ctx->ac.builder,
						     ctx->abi->outputs[idx + chan + const_index * stride],
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
	int writemask = instr->const_index[0];
	LLVMValueRef indir_index;
	unsigned const_index;
	get_deref_offset(ctx, instr->variables[0], false,
		         NULL, NULL, &const_index, &indir_index);

	if (ac_get_elem_bits(&ctx->ac, LLVMTypeOf(src)) == 64) {

		src = LLVMBuildBitCast(ctx->ac.builder, src,
		                       LLVMVectorType(ctx->ac.f32, ac_get_llvm_num_components(src) * 2),
		                       "");

		writemask = widen_mask(writemask, 2);
	}

	writemask = writemask << comp;

	switch (instr->variables[0]->var->data.mode) {
	case nir_var_shader_out:

		if (ctx->stage == MESA_SHADER_TESS_CTRL) {
			LLVMValueRef vertex_index = NULL;
			LLVMValueRef indir_index = NULL;
			unsigned const_index = 0;
			const bool is_patch = instr->variables[0]->var->data.patch;

			get_deref_offset(ctx, instr->variables[0],
					 false, NULL, is_patch ? NULL : &vertex_index,
					 &const_index, &indir_index);

			ctx->abi->store_tcs_outputs(ctx->abi, instr->variables[0]->var,
						    vertex_index, indir_index,
						    const_index, src, writemask);
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
						&ctx->ac, ctx->abi->outputs + idx + chan, count,
						stride, true, true);

				tmp_vec = LLVMBuildInsertElement(ctx->ac.builder, tmp_vec,
							         value, indir_index, "");
				build_store_values_extended(&ctx->ac, ctx->abi->outputs + idx + chan,
							    count, stride, tmp_vec);

			} else {
				temp_ptr = ctx->abi->outputs[idx + chan + const_index * stride];

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
	struct ac_image_args args = {0};
	LLVMValueRef res;

	args.coords[0] = coord_x;
	args.coords[1] = coord_y;
	if (coord_z)
		args.coords[2] = coord_z;

	args.opcode = ac_image_load;
	args.dim = coord_z ? ac_image_2darray : ac_image_2d;
	args.resource = fmask_desc_ptr;
	args.dmask = 0xf;
	args.attributes = AC_FUNC_ATTR_READNONE;

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

static bool
glsl_is_array_image(const struct glsl_type *type)
{
	const enum glsl_sampler_dim dim = glsl_get_sampler_dim(type);

	if (glsl_sampler_type_is_array(type))
		return true;

	return dim == GLSL_SAMPLER_DIM_SUBPASS ||
	       dim == GLSL_SAMPLER_DIM_SUBPASS_MS;
}

static void get_image_coords(struct ac_nir_context *ctx,
			     const nir_intrinsic_instr *instr,
			     struct ac_image_args *args)
{
	const struct glsl_type *type = glsl_without_array(instr->variables[0]->var->type);

	LLVMValueRef src0 = get_src(ctx, instr->src[0]);
	LLVMValueRef masks[] = {
		LLVMConstInt(ctx->ac.i32, 0, false), LLVMConstInt(ctx->ac.i32, 1, false),
		LLVMConstInt(ctx->ac.i32, 2, false), LLVMConstInt(ctx->ac.i32, 3, false),
	};
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
			fmask_load_address[2] = ac_to_integer(&ctx->ac, ctx->abi->inputs[ac_llvm_reg_index_soa(VARYING_SLOT_LAYER, 0)]);
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
			args->coords[0] = LLVMBuildExtractElement(ctx->ac.builder, src0, masks[0], "");
		else
			args->coords[0] = src0;
	} else {
		int chan;
		if (is_ms)
			count--;
		for (chan = 0; chan < count; ++chan) {
			args->coords[chan] = ac_llvm_extract_elem(&ctx->ac, src0, chan);
		}
		if (add_frag_pos) {
			for (chan = 0; chan < 2; ++chan) {
				args->coords[chan] = LLVMBuildAdd(
					ctx->ac.builder, args->coords[chan],
					LLVMBuildFPToUI(
						ctx->ac.builder, ctx->abi->frag_pos[chan],
						ctx->ac.i32, ""), "");
			}
			args->coords[2] = ac_to_integer(&ctx->ac,
				ctx->abi->inputs[ac_llvm_reg_index_soa(VARYING_SLOT_LAYER, 0)]);
			count++;
		}

		if (gfx9_1d) {
			if (is_array) {
				args->coords[2] = args->coords[1];
				args->coords[1] = ctx->ac.i32_0;
			} else
				args->coords[1] = ctx->ac.i32_0;
			count++;
		}

		if (is_ms) {
			args->coords[count] = sample_index;
			count++;
		}
	}
}

static LLVMValueRef get_image_buffer_descriptor(struct ac_nir_context *ctx,
                                                const nir_intrinsic_instr *instr, bool write)
{
	LLVMValueRef rsrc = get_sampler_desc(ctx, instr->variables[0], AC_DESC_BUFFER, NULL, true, write);
	if (ctx->abi->gfx9_stride_size_workaround) {
		LLVMValueRef elem_count = LLVMBuildExtractElement(ctx->ac.builder, rsrc, LLVMConstInt(ctx->ac.i32, 2, 0), "");
		LLVMValueRef stride = LLVMBuildExtractElement(ctx->ac.builder, rsrc, LLVMConstInt(ctx->ac.i32, 1, 0), "");
		stride = LLVMBuildLShr(ctx->ac.builder, stride, LLVMConstInt(ctx->ac.i32, 16, 0), "");

		LLVMValueRef new_elem_count = LLVMBuildSelect(ctx->ac.builder,
		                                              LLVMBuildICmp(ctx->ac.builder, LLVMIntUGT, elem_count, stride, ""),
		                                              elem_count, stride, "");

		rsrc = LLVMBuildInsertElement(ctx->ac.builder, rsrc, new_elem_count,
		                              LLVMConstInt(ctx->ac.i32, 2, 0), "");
	}
	return rsrc;
}

static LLVMValueRef visit_image_load(struct ac_nir_context *ctx,
				     const nir_intrinsic_instr *instr)
{
	LLVMValueRef res;
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

		rsrc = get_image_buffer_descriptor(ctx, instr, false);
		vindex = LLVMBuildExtractElement(ctx->ac.builder, get_src(ctx, instr->src[0]),
						 ctx->ac.i32_0, "");

		/* TODO: set "glc" and "can_speculate" when OpenGL needs it. */
		res = ac_build_buffer_load_format(&ctx->ac, rsrc, vindex,
						  ctx->ac.i32_0, num_channels,
						  false, false);
		res = ac_build_expand_to_vec4(&ctx->ac, res, num_channels);

		res = ac_trim_vector(&ctx->ac, res, instr->dest.ssa.num_components);
		res = ac_to_integer(&ctx->ac, res);
	} else {
		struct ac_image_args args = {};
		args.opcode = ac_image_load;
		get_image_coords(ctx, instr, &args);
		args.resource = get_sampler_desc(ctx, instr->variables[0],
						 AC_DESC_IMAGE, NULL, true, false);
		args.dim = get_ac_image_dim(&ctx->ac, glsl_get_sampler_dim(type),
					    glsl_is_array_image(type));
		args.dmask = 15;
		args.attributes = AC_FUNC_ATTR_READONLY;
		if (var->data.image._volatile || var->data.image.coherent)
			args.cache_policy |= ac_glc;

		res = ac_build_image_opcode(&ctx->ac, &args);
	}
	return ac_to_integer(&ctx->ac, res);
}

static void visit_image_store(struct ac_nir_context *ctx,
			      nir_intrinsic_instr *instr)
{
	LLVMValueRef params[8];
	const nir_variable *var = instr->variables[0]->var;
	const struct glsl_type *type = glsl_without_array(var->type);
	const enum glsl_sampler_dim dim = glsl_get_sampler_dim(type);
	LLVMValueRef glc = ctx->ac.i1false;
	bool force_glc = ctx->ac.chip_class == SI;
	if (force_glc)
		glc = ctx->ac.i1true;

	if (dim == GLSL_SAMPLER_DIM_BUF) {
		LLVMValueRef rsrc = get_image_buffer_descriptor(ctx, instr, true);

		params[0] = ac_to_float(&ctx->ac, get_src(ctx, instr->src[2])); /* data */
		params[1] = rsrc;
		params[2] = LLVMBuildExtractElement(ctx->ac.builder, get_src(ctx, instr->src[0]),
						    ctx->ac.i32_0, ""); /* vindex */
		params[3] = ctx->ac.i32_0; /* voffset */
		params[4] = glc;  /* glc */
		params[5] = ctx->ac.i1false;  /* slc */
		ac_build_intrinsic(&ctx->ac, "llvm.amdgcn.buffer.store.format.v4f32", ctx->ac.voidt,
				   params, 6, 0);
	} else {
		struct ac_image_args args = {};
		args.opcode = ac_image_store;
		args.data[0] = ac_to_float(&ctx->ac, get_src(ctx, instr->src[2]));
		get_image_coords(ctx, instr, &args);
		args.resource = get_sampler_desc(ctx, instr->variables[0],
						 AC_DESC_IMAGE, NULL, true, false);
		args.dim = get_ac_image_dim(&ctx->ac, glsl_get_sampler_dim(type),
					    glsl_is_array_image(type));
		args.dmask = 15;
		if (force_glc || var->data.image._volatile || var->data.image.coherent)
			args.cache_policy |= ac_glc;

		ac_build_image_opcode(&ctx->ac, &args);
	}

}

static LLVMValueRef visit_image_atomic(struct ac_nir_context *ctx,
                                       const nir_intrinsic_instr *instr)
{
	LLVMValueRef params[7];
	int param_count = 0;
	const nir_variable *var = instr->variables[0]->var;

	bool cmpswap = instr->intrinsic == nir_intrinsic_image_var_atomic_comp_swap;
	const char *atomic_name;
	char intrinsic_name[41];
	enum ac_atomic_op atomic_subop;
	const struct glsl_type *type = glsl_without_array(var->type);
	MAYBE_UNUSED int length;

	bool is_unsigned = glsl_get_sampler_result_type(type) == GLSL_TYPE_UINT;

	switch (instr->intrinsic) {
	case nir_intrinsic_image_var_atomic_add:
		atomic_name = "add";
		atomic_subop = ac_atomic_add;
		break;
	case nir_intrinsic_image_var_atomic_min:
		atomic_name = is_unsigned ? "umin" : "smin";
		atomic_subop = is_unsigned ? ac_atomic_umin : ac_atomic_smin;
		break;
	case nir_intrinsic_image_var_atomic_max:
		atomic_name = is_unsigned ? "umax" : "smax";
		atomic_subop = is_unsigned ? ac_atomic_umax : ac_atomic_smax;
		break;
	case nir_intrinsic_image_var_atomic_and:
		atomic_name = "and";
		atomic_subop = ac_atomic_and;
		break;
	case nir_intrinsic_image_var_atomic_or:
		atomic_name = "or";
		atomic_subop = ac_atomic_or;
		break;
	case nir_intrinsic_image_var_atomic_xor:
		atomic_name = "xor";
		atomic_subop = ac_atomic_xor;
		break;
	case nir_intrinsic_image_var_atomic_exchange:
		atomic_name = "swap";
		atomic_subop = ac_atomic_swap;
		break;
	case nir_intrinsic_image_var_atomic_comp_swap:
		atomic_name = "cmpswap";
		atomic_subop = 0; /* not used */
		break;
	default:
		abort();
	}

	if (cmpswap)
		params[param_count++] = get_src(ctx, instr->src[3]);
	params[param_count++] = get_src(ctx, instr->src[2]);

	if (glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_BUF) {
		params[param_count++] = get_image_buffer_descriptor(ctx, instr, true);
		params[param_count++] = LLVMBuildExtractElement(ctx->ac.builder, get_src(ctx, instr->src[0]),
								ctx->ac.i32_0, ""); /* vindex */
		params[param_count++] = ctx->ac.i32_0; /* voffset */
		params[param_count++] = ctx->ac.i1false;  /* slc */

		length = snprintf(intrinsic_name, sizeof(intrinsic_name),
				  "llvm.amdgcn.buffer.atomic.%s", atomic_name);

		assert(length < sizeof(intrinsic_name));
		return ac_build_intrinsic(&ctx->ac, intrinsic_name, ctx->ac.i32,
					  params, param_count, 0);
	} else {
		struct ac_image_args args = {};
		args.opcode = cmpswap ? ac_image_atomic_cmpswap : ac_image_atomic;
		args.atomic = atomic_subop;
		args.data[0] = params[0];
		if (cmpswap)
			args.data[1] = params[1];
		get_image_coords(ctx, instr, &args);
		args.resource = get_sampler_desc(ctx, instr->variables[0],
						 AC_DESC_IMAGE, NULL, true, false);
		args.dim = get_ac_image_dim(&ctx->ac, glsl_get_sampler_dim(type),
					    glsl_is_array_image(type));

		return ac_build_image_opcode(&ctx->ac, &args);
	}
}

static LLVMValueRef visit_image_samples(struct ac_nir_context *ctx,
					const nir_intrinsic_instr *instr)
{
	const nir_variable *var = instr->variables[0]->var;
	const struct glsl_type *type = glsl_without_array(var->type);

	struct ac_image_args args = { 0 };
	args.dim = get_ac_sampler_dim(&ctx->ac, glsl_get_sampler_dim(type),
				      glsl_sampler_type_is_array(type));
	args.dmask = 0xf;
	args.resource = get_sampler_desc(ctx, instr->variables[0],
					 AC_DESC_IMAGE, NULL, true, false);
	args.opcode = ac_image_get_resinfo;
	args.lod = ctx->ac.i32_0;
	args.attributes = AC_FUNC_ATTR_READNONE;

	return ac_build_image_opcode(&ctx->ac, &args);
}

static LLVMValueRef visit_image_size(struct ac_nir_context *ctx,
				     const nir_intrinsic_instr *instr)
{
	LLVMValueRef res;
	const nir_variable *var = instr->variables[0]->var;
	const struct glsl_type *type = glsl_without_array(var->type);

	if (glsl_get_sampler_dim(type) == GLSL_SAMPLER_DIM_BUF)
		return get_buffer_size(ctx,
			get_sampler_desc(ctx, instr->variables[0],
					 AC_DESC_BUFFER, NULL, true, false), true);

	struct ac_image_args args = { 0 };

	args.dim = get_ac_image_dim(&ctx->ac, glsl_get_sampler_dim(type),
				    glsl_sampler_type_is_array(type));
	args.dmask = 0xf;
	args.resource = get_sampler_desc(ctx, instr->variables[0], AC_DESC_IMAGE, NULL, true, false);
	args.opcode = ac_image_get_resinfo;
	args.lod = ctx->ac.i32_0;
	args.attributes = AC_FUNC_ATTR_READNONE;

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

static void emit_membar(struct ac_llvm_context *ac,
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
		ac_build_waitcnt(ac, waitcnt);
}

void ac_emit_barrier(struct ac_llvm_context *ac, gl_shader_stage stage)
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

	ctx->abi->emit_kill(ctx->abi, cond);
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
visit_load_local_invocation_index(struct ac_nir_context *ctx)
{
	LLVMValueRef result;
	LLVMValueRef thread_id = ac_get_thread_id(&ctx->ac);
	result = LLVMBuildAnd(ctx->ac.builder, ctx->abi->tg_size,
			      LLVMConstInt(ctx->ac.i32, 0xfc0, false), "");

	return LLVMBuildAdd(ctx->ac.builder, result, thread_id, "");
}

static LLVMValueRef
visit_load_subgroup_id(struct ac_nir_context *ctx)
{
	if (ctx->stage == MESA_SHADER_COMPUTE) {
		LLVMValueRef result;
		result = LLVMBuildAnd(ctx->ac.builder, ctx->abi->tg_size,
				LLVMConstInt(ctx->ac.i32, 0xfc0, false), "");
		return LLVMBuildLShr(ctx->ac.builder, result,  LLVMConstInt(ctx->ac.i32, 6, false), "");
	} else {
		return LLVMConstInt(ctx->ac.i32, 0, false);
	}
}

static LLVMValueRef
visit_load_num_subgroups(struct ac_nir_context *ctx)
{
	if (ctx->stage == MESA_SHADER_COMPUTE) {
		return LLVMBuildAnd(ctx->ac.builder, ctx->abi->tg_size,
		                    LLVMConstInt(ctx->ac.i32, 0x3f, false), "");
	} else {
		return LLVMConstInt(ctx->ac.i32, 1, false);
	}
}

static LLVMValueRef
visit_first_invocation(struct ac_nir_context *ctx)
{
	LLVMValueRef active_set = ac_build_ballot(&ctx->ac, ctx->ac.i32_1);

	/* The second argument is whether cttz(0) should be defined, but we do not care. */
	LLVMValueRef args[] = {active_set, LLVMConstInt(ctx->ac.i1, 0, false)};
	LLVMValueRef result =  ac_build_intrinsic(&ctx->ac,
	                                          "llvm.cttz.i64",
	                                          ctx->ac.i64, args, 2,
	                                          AC_FUNC_ATTR_NOUNWIND |
	                                          AC_FUNC_ATTR_READNONE);

	return LLVMBuildTrunc(ctx->ac.builder, result, ctx->ac.i32, "");
}

static LLVMValueRef
visit_load_shared(struct ac_nir_context *ctx,
		   const nir_intrinsic_instr *instr)
{
	LLVMValueRef values[4], derived_ptr, index, ret;

	LLVMValueRef ptr = get_memory_ptr(ctx, instr->src[0]);

	for (int chan = 0; chan < instr->num_components; chan++) {
		index = LLVMConstInt(ctx->ac.i32, chan, 0);
		derived_ptr = LLVMBuildGEP(ctx->ac.builder, ptr, &index, 1, "");
		values[chan] = LLVMBuildLoad(ctx->ac.builder, derived_ptr, "");
	}

	ret = ac_build_gather_values(&ctx->ac, values, instr->num_components);
	return LLVMBuildBitCast(ctx->ac.builder, ret, get_def_type(ctx, &instr->dest.ssa), "");
}

static void
visit_store_shared(struct ac_nir_context *ctx,
		   const nir_intrinsic_instr *instr)
{
	LLVMValueRef derived_ptr, data,index;
	LLVMBuilderRef builder = ctx->ac.builder;

	LLVMValueRef ptr = get_memory_ptr(ctx, instr->src[1]);
	LLVMValueRef src = get_src(ctx, instr->src[0]);

	int writemask = nir_intrinsic_write_mask(instr);
	for (int chan = 0; chan < 4; chan++) {
		if (!(writemask & (1 << chan))) {
			continue;
		}
		data = ac_llvm_extract_elem(&ctx->ac, src, chan);
		index = LLVMConstInt(ctx->ac.i32, chan, 0);
		derived_ptr = LLVMBuildGEP(builder, ptr, &index, 1, "");
		LLVMBuildStore(builder, data, derived_ptr);
	}
}

static LLVMValueRef visit_var_atomic(struct ac_nir_context *ctx,
				     const nir_intrinsic_instr *instr,
				     LLVMValueRef ptr, int src_idx)
{
	LLVMValueRef result;
	LLVMValueRef src = get_src(ctx, instr->src[src_idx]);

	if (instr->intrinsic == nir_intrinsic_var_atomic_comp_swap ||
	    instr->intrinsic == nir_intrinsic_shared_atomic_comp_swap) {
		LLVMValueRef src1 = get_src(ctx, instr->src[src_idx + 1]);
		result = LLVMBuildAtomicCmpXchg(ctx->ac.builder,
						ptr, src, src1,
						LLVMAtomicOrderingSequentiallyConsistent,
						LLVMAtomicOrderingSequentiallyConsistent,
						false);
		result = LLVMBuildExtractValue(ctx->ac.builder, result, 0, "");
	} else {
		LLVMAtomicRMWBinOp op;
		switch (instr->intrinsic) {
		case nir_intrinsic_var_atomic_add:
		case nir_intrinsic_shared_atomic_add:
			op = LLVMAtomicRMWBinOpAdd;
			break;
		case nir_intrinsic_var_atomic_umin:
		case nir_intrinsic_shared_atomic_umin:
			op = LLVMAtomicRMWBinOpUMin;
			break;
		case nir_intrinsic_var_atomic_umax:
		case nir_intrinsic_shared_atomic_umax:
			op = LLVMAtomicRMWBinOpUMax;
			break;
		case nir_intrinsic_var_atomic_imin:
		case nir_intrinsic_shared_atomic_imin:
			op = LLVMAtomicRMWBinOpMin;
			break;
		case nir_intrinsic_var_atomic_imax:
		case nir_intrinsic_shared_atomic_imax:
			op = LLVMAtomicRMWBinOpMax;
			break;
		case nir_intrinsic_var_atomic_and:
		case nir_intrinsic_shared_atomic_and:
			op = LLVMAtomicRMWBinOpAnd;
			break;
		case nir_intrinsic_var_atomic_or:
		case nir_intrinsic_shared_atomic_or:
			op = LLVMAtomicRMWBinOpOr;
			break;
		case nir_intrinsic_var_atomic_xor:
		case nir_intrinsic_shared_atomic_xor:
			op = LLVMAtomicRMWBinOpXor;
			break;
		case nir_intrinsic_var_atomic_exchange:
		case nir_intrinsic_shared_atomic_exchange:
			op = LLVMAtomicRMWBinOpXchg;
			break;
		default:
			return NULL;
		}

		result = LLVMBuildAtomicRMW(ctx->ac.builder, op, ptr, ac_to_integer(&ctx->ac, src),
					    LLVMAtomicOrderingSequentiallyConsistent,
					    false);
	}
	return result;
}

static LLVMValueRef load_sample_pos(struct ac_nir_context *ctx)
{
	LLVMValueRef values[2];
	LLVMValueRef pos[2];

	pos[0] = ac_to_float(&ctx->ac, ctx->abi->frag_pos[0]);
	pos[1] = ac_to_float(&ctx->ac, ctx->abi->frag_pos[1]);

	values[0] = ac_build_fract(&ctx->ac, pos[0], 32);
	values[1] = ac_build_fract(&ctx->ac, pos[1], 32);
	return ac_build_gather_values(&ctx->ac, values, 2);
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

static void visit_intrinsic(struct ac_nir_context *ctx,
                            nir_intrinsic_instr *instr)
{
	LLVMValueRef result = NULL;

	switch (instr->intrinsic) {
	case nir_intrinsic_ballot:
		result = ac_build_ballot(&ctx->ac, get_src(ctx, instr->src[0]));
		break;
	case nir_intrinsic_read_invocation:
		result = ac_build_readlane(&ctx->ac, get_src(ctx, instr->src[0]),
				get_src(ctx, instr->src[1]));
		break;
	case nir_intrinsic_read_first_invocation:
		result = ac_build_readlane(&ctx->ac, get_src(ctx, instr->src[0]), NULL);
		break;
	case nir_intrinsic_load_subgroup_invocation:
		result = ac_get_thread_id(&ctx->ac);
		break;
	case nir_intrinsic_load_work_group_id: {
		LLVMValueRef values[3];

		for (int i = 0; i < 3; i++) {
			values[i] = ctx->abi->workgroup_ids[i] ?
				    ctx->abi->workgroup_ids[i] : ctx->ac.i32_0;
		}

		result = ac_build_gather_values(&ctx->ac, values, 3);
		break;
	}
	case nir_intrinsic_load_base_vertex:
	case nir_intrinsic_load_first_vertex:
		result = ctx->abi->load_base_vertex(ctx->abi);
		break;
	case nir_intrinsic_load_local_group_size:
		result = ctx->abi->load_local_group_size(ctx->abi);
		break;
	case nir_intrinsic_load_vertex_id:
		result = LLVMBuildAdd(ctx->ac.builder, ctx->abi->vertex_id,
				      ctx->abi->base_vertex, "");
		break;
	case nir_intrinsic_load_vertex_id_zero_base: {
		result = ctx->abi->vertex_id;
		break;
	}
	case nir_intrinsic_load_local_invocation_id: {
		result = ctx->abi->local_invocation_ids;
		break;
	}
	case nir_intrinsic_load_base_instance:
		result = ctx->abi->start_instance;
		break;
	case nir_intrinsic_load_draw_id:
		result = ctx->abi->draw_id;
		break;
	case nir_intrinsic_load_view_index:
		result = ctx->abi->view_index;
		break;
	case nir_intrinsic_load_invocation_id:
		if (ctx->stage == MESA_SHADER_TESS_CTRL)
			result = ac_unpack_param(&ctx->ac, ctx->abi->tcs_rel_ids, 8, 5);
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
		result = ac_unpack_param(&ctx->ac, ctx->abi->ancillary, 8, 4);
		break;
	case nir_intrinsic_load_sample_pos:
		result = load_sample_pos(ctx);
		break;
	case nir_intrinsic_load_sample_mask_in:
		result = ctx->abi->load_sample_mask_in(ctx->abi);
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
		result = ctx->abi->num_work_groups;
		break;
	case nir_intrinsic_load_local_invocation_index:
		result = visit_load_local_invocation_index(ctx);
		break;
	case nir_intrinsic_load_subgroup_id:
		result = visit_load_subgroup_id(ctx);
		break;
	case nir_intrinsic_load_num_subgroups:
		result = visit_load_num_subgroups(ctx);
		break;
	case nir_intrinsic_first_invocation:
		result = visit_first_invocation(ctx);
		break;
	case nir_intrinsic_load_push_constant:
		result = visit_load_push_constant(ctx, instr);
		break;
	case nir_intrinsic_vulkan_resource_index: {
		LLVMValueRef index = get_src(ctx, instr->src[0]);
		unsigned desc_set = nir_intrinsic_desc_set(instr);
		unsigned binding = nir_intrinsic_binding(instr);

		result = ctx->abi->load_resource(ctx->abi, index, desc_set,
						 binding);
		break;
	}
	case nir_intrinsic_vulkan_resource_reindex:
		result = visit_vulkan_resource_reindex(ctx, instr);
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
	case nir_intrinsic_load_shared:
		result = visit_load_shared(ctx, instr);
		break;
	case nir_intrinsic_store_shared:
		visit_store_shared(ctx, instr);
		break;
	case nir_intrinsic_image_var_samples:
		result = visit_image_samples(ctx, instr);
		break;
	case nir_intrinsic_image_var_load:
		result = visit_image_load(ctx, instr);
		break;
	case nir_intrinsic_image_var_store:
		visit_image_store(ctx, instr);
		break;
	case nir_intrinsic_image_var_atomic_add:
	case nir_intrinsic_image_var_atomic_min:
	case nir_intrinsic_image_var_atomic_max:
	case nir_intrinsic_image_var_atomic_and:
	case nir_intrinsic_image_var_atomic_or:
	case nir_intrinsic_image_var_atomic_xor:
	case nir_intrinsic_image_var_atomic_exchange:
	case nir_intrinsic_image_var_atomic_comp_swap:
		result = visit_image_atomic(ctx, instr);
		break;
	case nir_intrinsic_image_var_size:
		result = visit_image_size(ctx, instr);
		break;
	case nir_intrinsic_shader_clock:
		result = ac_build_shader_clock(&ctx->ac);
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
		emit_membar(&ctx->ac, instr);
		break;
	case nir_intrinsic_barrier:
		ac_emit_barrier(&ctx->ac, ctx->stage);
		break;
	case nir_intrinsic_shared_atomic_add:
	case nir_intrinsic_shared_atomic_imin:
	case nir_intrinsic_shared_atomic_umin:
	case nir_intrinsic_shared_atomic_imax:
	case nir_intrinsic_shared_atomic_umax:
	case nir_intrinsic_shared_atomic_and:
	case nir_intrinsic_shared_atomic_or:
	case nir_intrinsic_shared_atomic_xor:
	case nir_intrinsic_shared_atomic_exchange:
	case nir_intrinsic_shared_atomic_comp_swap: {
		LLVMValueRef ptr = get_memory_ptr(ctx, instr->src[0]);
		result = visit_var_atomic(ctx, instr, ptr, 1);
		break;
	}
	case nir_intrinsic_var_atomic_add:
	case nir_intrinsic_var_atomic_imin:
	case nir_intrinsic_var_atomic_umin:
	case nir_intrinsic_var_atomic_imax:
	case nir_intrinsic_var_atomic_umax:
	case nir_intrinsic_var_atomic_and:
	case nir_intrinsic_var_atomic_or:
	case nir_intrinsic_var_atomic_xor:
	case nir_intrinsic_var_atomic_exchange:
	case nir_intrinsic_var_atomic_comp_swap: {
		LLVMValueRef ptr = build_gep_for_deref(ctx, instr->variables[0]);
		result = visit_var_atomic(ctx, instr, ptr, 0);
		break;
	}
	case nir_intrinsic_interp_var_at_centroid:
	case nir_intrinsic_interp_var_at_sample:
	case nir_intrinsic_interp_var_at_offset:
		result = visit_interp(ctx, instr);
		break;
	case nir_intrinsic_emit_vertex:
		ctx->abi->emit_vertex(ctx->abi, nir_intrinsic_stream_id(instr), ctx->abi->outputs);
		break;
	case nir_intrinsic_end_primitive:
		ctx->abi->emit_primitive(ctx->abi, nir_intrinsic_stream_id(instr));
		break;
	case nir_intrinsic_load_tess_coord:
		result = ctx->abi->load_tess_coord(ctx->abi);
		break;
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
	case nir_intrinsic_shuffle:
		result = ac_build_shuffle(&ctx->ac, get_src(ctx, instr->src[0]),
				get_src(ctx, instr->src[1]));
		break;
	case nir_intrinsic_reduce:
		result = ac_build_reduce(&ctx->ac,
				get_src(ctx, instr->src[0]),
				instr->const_index[0],
				instr->const_index[1]);
		break;
	case nir_intrinsic_inclusive_scan:
		result = ac_build_inclusive_scan(&ctx->ac,
				get_src(ctx, instr->src[0]),
				instr->const_index[0]);
		break;
	case nir_intrinsic_exclusive_scan:
		result = ac_build_exclusive_scan(&ctx->ac,
				get_src(ctx, instr->src[0]),
				instr->const_index[0]);
		break;
	case nir_intrinsic_quad_broadcast: {
		unsigned lane = nir_src_as_const_value(instr->src[1])->u32[0];
		result = ac_build_quad_swizzle(&ctx->ac, get_src(ctx, instr->src[0]),
				lane, lane, lane, lane);
		break;
	}
	case nir_intrinsic_quad_swap_horizontal:
		result = ac_build_quad_swizzle(&ctx->ac, get_src(ctx, instr->src[0]), 1, 0, 3 ,2);
		break;
	case nir_intrinsic_quad_swap_vertical:
		result = ac_build_quad_swizzle(&ctx->ac, get_src(ctx, instr->src[0]), 2, 3, 0 ,1);
		break;
	case nir_intrinsic_quad_swap_diagonal:
		result = ac_build_quad_swizzle(&ctx->ac, get_src(ctx, instr->src[0]), 3, 2, 1 ,0);
		break;
	default:
		fprintf(stderr, "Unknown intrinsic: ");
		nir_print_instr(&instr->instr, stderr);
		fprintf(stderr, "\n");
		break;
	}
	if (result) {
		ctx->ssa_defs[instr->dest.ssa.index] = result;
	}
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
	bool bindless = false;

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

		if (deref->var->data.bindless) {
			bindless = deref->var->data.bindless;
			base_index = deref->var->data.driver_location;
		} else {
			base_index = deref->var->data.binding;
		}
	}

	return ctx->abi->load_sampler_desc(ctx->abi,
					  descriptor_set,
					  base_index,
					  constant_index, index,
					  desc_type, image, write, bindless);
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
	LLVMValueRef fmask_ptr = NULL, sample_index = NULL;
	LLVMValueRef ddx = NULL, ddy = NULL;
	unsigned offset_src = 0;

	tex_fetch_ptrs(ctx, instr, &args.resource, &args.sampler, &fmask_ptr);

	for (unsigned i = 0; i < instr->num_srcs; i++) {
		switch (instr->src[i].src_type) {
		case nir_tex_src_coord: {
			LLVMValueRef coord = get_src(ctx, instr->src[i].src);
			for (unsigned chan = 0; chan < instr->coord_components; ++chan)
				args.coords[chan] = ac_llvm_extract_elem(&ctx->ac, coord, chan);
			break;
		}
		case nir_tex_src_projector:
			break;
		case nir_tex_src_comparator:
			if (instr->is_shadow)
				args.compare = get_src(ctx, instr->src[i].src);
			break;
		case nir_tex_src_offset:
			args.offset = get_src(ctx, instr->src[i].src);
			offset_src = i;
			break;
		case nir_tex_src_bias:
			if (instr->op == nir_texop_txb)
				args.bias = get_src(ctx, instr->src[i].src);
			break;
		case nir_tex_src_lod: {
			nir_const_value *val = nir_src_as_const_value(instr->src[i].src);

			if (val && val->i32[0] == 0)
				args.level_zero = true;
			else
				args.lod = get_src(ctx, instr->src[i].src);
			break;
		}
		case nir_tex_src_ms_index:
			sample_index = get_src(ctx, instr->src[i].src);
			break;
		case nir_tex_src_ms_mcs:
			break;
		case nir_tex_src_ddx:
			ddx = get_src(ctx, instr->src[i].src);
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
		result = get_buffer_size(ctx, args.resource, true);
		goto write_result;
	}

	if (instr->op == nir_texop_texture_samples) {
		LLVMValueRef res, samples, is_msaa;
		res = LLVMBuildBitCast(ctx->ac.builder, args.resource, ctx->ac.v8i32, "");
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

	if (args.offset && instr->op != nir_texop_txf) {
		LLVMValueRef offset[3], pack;
		for (unsigned chan = 0; chan < 3; ++chan)
			offset[chan] = ctx->ac.i32_0;

		unsigned num_components = ac_get_llvm_num_components(args.offset);
		for (unsigned chan = 0; chan < num_components; chan++) {
			offset[chan] = ac_llvm_extract_elem(&ctx->ac, args.offset, chan);
			offset[chan] = LLVMBuildAnd(ctx->ac.builder, offset[chan],
						    LLVMConstInt(ctx->ac.i32, 0x3f, false), "");
			if (chan)
				offset[chan] = LLVMBuildShl(ctx->ac.builder, offset[chan],
							    LLVMConstInt(ctx->ac.i32, chan * 8, false), "");
		}
		pack = LLVMBuildOr(ctx->ac.builder, offset[0], offset[1], "");
		pack = LLVMBuildOr(ctx->ac.builder, pack, offset[2], "");
		args.offset = pack;
	}

	/* TC-compatible HTILE on radeonsi promotes Z16 and Z24 to Z32_FLOAT,
	 * so the depth comparison value isn't clamped for Z16 and
	 * Z24 anymore. Do it manually here.
	 *
	 * It's unnecessary if the original texture format was
	 * Z32_FLOAT, but we don't know that here.
	 */
	if (args.compare && ctx->ac.chip_class == VI && ctx->abi->clamp_shadow_reference)
		args.compare = ac_build_clamp(&ctx->ac, ac_to_float(&ctx->ac, args.compare));

	/* pack derivatives */
	if (ddx || ddy) {
		int num_src_deriv_channels, num_dest_deriv_channels;
		switch (instr->sampler_dim) {
		case GLSL_SAMPLER_DIM_3D:
		case GLSL_SAMPLER_DIM_CUBE:
			num_src_deriv_channels = 3;
			num_dest_deriv_channels = 3;
			break;
		case GLSL_SAMPLER_DIM_2D:
		default:
			num_src_deriv_channels = 2;
			num_dest_deriv_channels = 2;
			break;
		case GLSL_SAMPLER_DIM_1D:
			num_src_deriv_channels = 1;
			if (ctx->ac.chip_class >= GFX9) {
				num_dest_deriv_channels = 2;
			} else {
				num_dest_deriv_channels = 1;
			}
			break;
		}

		for (unsigned i = 0; i < num_src_deriv_channels; i++) {
			args.derivs[i] = ac_to_float(&ctx->ac,
				ac_llvm_extract_elem(&ctx->ac, ddx, i));
			args.derivs[num_dest_deriv_channels + i] = ac_to_float(&ctx->ac,
				ac_llvm_extract_elem(&ctx->ac, ddy, i));
		}
		for (unsigned i = num_src_deriv_channels; i < num_dest_deriv_channels; i++) {
			args.derivs[i] = ctx->ac.f32_0;
			args.derivs[num_dest_deriv_channels + i] = ctx->ac.f32_0;
		}
	}

	if (instr->sampler_dim == GLSL_SAMPLER_DIM_CUBE && args.coords[0]) {
		for (unsigned chan = 0; chan < instr->coord_components; chan++)
			args.coords[chan] = ac_to_float(&ctx->ac, args.coords[chan]);
		if (instr->coord_components == 3)
			args.coords[3] = LLVMGetUndef(ctx->ac.f32);
		ac_prepare_cube_coords(&ctx->ac,
			instr->op == nir_texop_txd, instr->is_array,
			instr->op == nir_texop_lod, args.coords, args.derivs);
	}

	/* Texture coordinates fixups */
	if (instr->coord_components > 2 &&
	    (instr->sampler_dim == GLSL_SAMPLER_DIM_2D ||
	     instr->sampler_dim == GLSL_SAMPLER_DIM_MS ||
	     instr->sampler_dim == GLSL_SAMPLER_DIM_SUBPASS ||
	     instr->sampler_dim == GLSL_SAMPLER_DIM_SUBPASS_MS) &&
	    instr->is_array &&
	    instr->op != nir_texop_txf && instr->op != nir_texop_txf_ms) {
		args.coords[2] = apply_round_slice(&ctx->ac, args.coords[2]);
	}

	if (ctx->ac.chip_class >= GFX9 &&
	    instr->sampler_dim == GLSL_SAMPLER_DIM_1D &&
	    instr->op != nir_texop_lod) {
		LLVMValueRef filler;
		if (instr->op == nir_texop_txf)
			filler = ctx->ac.i32_0;
		else
			filler = LLVMConstReal(ctx->ac.f32, 0.5);

		if (instr->is_array)
			args.coords[2] = args.coords[1];
		args.coords[1] = filler;
	}

	/* Pack sample index */
	if (instr->op == nir_texop_txf_ms && sample_index)
		args.coords[instr->coord_components] = sample_index;

	if (instr->op == nir_texop_samples_identical) {
		struct ac_image_args txf_args = { 0 };
		memcpy(txf_args.coords, args.coords, sizeof(txf_args.coords));

		txf_args.dmask = 0xf;
		txf_args.resource = fmask_ptr;
		txf_args.dim = instr->is_array ? ac_image_2darray : ac_image_2d;
		result = build_tex_intrinsic(ctx, instr, &txf_args);

		result = LLVMBuildExtractElement(ctx->ac.builder, result, ctx->ac.i32_0, "");
		result = emit_int_cmp(&ctx->ac, LLVMIntEQ, result, ctx->ac.i32_0);
		goto write_result;
	}

	if (instr->sampler_dim == GLSL_SAMPLER_DIM_MS &&
	    instr->op != nir_texop_txs) {
		unsigned sample_chan = instr->is_array ? 3 : 2;
		args.coords[sample_chan] = adjust_sample_index_using_fmask(
			&ctx->ac, args.coords[0], args.coords[1],
			instr->is_array ? args.coords[2] : NULL,
			args.coords[sample_chan], fmask_ptr);
	}

	if (args.offset && instr->op == nir_texop_txf) {
		nir_const_value *const_offset =
			nir_src_as_const_value(instr->src[offset_src].src);
		int num_offsets = instr->src[offset_src].src.ssa->num_components;
		assert(const_offset);
		num_offsets = MIN2(num_offsets, instr->coord_components);
		for (unsigned i = 0; i < num_offsets; ++i) {
			args.coords[i] = LLVMBuildAdd(
				ctx->ac.builder, args.coords[i],
				LLVMConstInt(ctx->ac.i32, const_offset->i32[i], false), "");
		}
		args.offset = NULL;
	}

	/* TODO TG4 support */
	args.dmask = 0xf;
	if (instr->op == nir_texop_tg4) {
		if (instr->is_shadow)
			args.dmask = 1;
		else
			args.dmask = 1 << instr->component;
	}

	if (instr->sampler_dim != GLSL_SAMPLER_DIM_BUF)
		args.dim = get_ac_sampler_dim(&ctx->ac, instr->sampler_dim, instr->is_array);
	result = build_tex_intrinsic(ctx, instr, &args);

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
		result = ac_trim_vector(&ctx->ac, result, instr->dest.ssa.num_components);

write_result:
	if (result) {
		assert(instr->dest.is_ssa);
		result = ac_to_integer(&ctx->ac, result);
		ctx->ssa_defs[instr->dest.ssa.index] = result;
	}
}


static void visit_phi(struct ac_nir_context *ctx, nir_phi_instr *instr)
{
	LLVMTypeRef type = get_def_type(ctx, &instr->dest.ssa);
	LLVMValueRef result = LLVMBuildPhi(ctx->ac.builder, type, "");

	ctx->ssa_defs[instr->dest.ssa.index] = result;
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
	ctx->ssa_defs[instr->def.index] = undef;
}

static void visit_jump(struct ac_llvm_context *ctx,
		       const nir_jump_instr *instr)
{
	switch (instr->type) {
	case nir_jump_break:
		ac_build_break(ctx);
		break;
	case nir_jump_continue:
		ac_build_continue(ctx);
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
			visit_jump(&ctx->ac, nir_instr_as_jump(instr));
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

	nir_block *then_block =
		(nir_block *) exec_list_get_head(&if_stmt->then_list);

	ac_build_uif(&ctx->ac, value, then_block->index);

	visit_cf_list(ctx, &if_stmt->then_list);

	if (!exec_list_is_empty(&if_stmt->else_list)) {
		nir_block *else_block =
			(nir_block *) exec_list_get_head(&if_stmt->else_list);

		ac_build_else(&ctx->ac, else_block->index);
		visit_cf_list(ctx, &if_stmt->else_list);
	}

	ac_build_endif(&ctx->ac, then_block->index);
}

static void visit_loop(struct ac_nir_context *ctx, nir_loop *loop)
{
	nir_block *first_loop_block =
		(nir_block *) exec_list_get_head(&loop->body);

	ac_build_bgnloop(&ctx->ac, first_loop_block->index);

	visit_cf_list(ctx, &loop->body);

	ac_build_endloop(&ctx->ac, first_loop_block->index);
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

void
ac_handle_shader_output_decl(struct ac_llvm_context *ctx,
			     struct ac_shader_abi *abi,
			     struct nir_shader *nir,
			     struct nir_variable *variable,
			     gl_shader_stage stage)
{
	unsigned output_loc = variable->data.driver_location / 4;
	unsigned attrib_count = glsl_count_attribute_slots(variable->type, false);

	/* tess ctrl has it's own load/store paths for outputs */
	if (stage == MESA_SHADER_TESS_CTRL)
		return;

	if (stage == MESA_SHADER_VERTEX ||
	    stage == MESA_SHADER_TESS_EVAL ||
	    stage == MESA_SHADER_GEOMETRY) {
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
			abi->outputs[ac_llvm_reg_index_soa(output_loc + i, chan)] =
		                       ac_build_alloca_undef(ctx, ctx->f32, "");
		}
	}
}

static LLVMTypeRef
glsl_base_to_llvm_type(struct ac_llvm_context *ac,
		       enum glsl_base_type type)
{
	switch (type) {
	case GLSL_TYPE_INT:
	case GLSL_TYPE_UINT:
	case GLSL_TYPE_BOOL:
	case GLSL_TYPE_SUBROUTINE:
		return ac->i32;
	case GLSL_TYPE_FLOAT: /* TODO handle mediump */
		return ac->f32;
	case GLSL_TYPE_INT64:
	case GLSL_TYPE_UINT64:
		return ac->i64;
	case GLSL_TYPE_DOUBLE:
		return ac->f64;
	default:
		unreachable("unknown GLSL type");
	}
}

static LLVMTypeRef
glsl_to_llvm_type(struct ac_llvm_context *ac,
		  const struct glsl_type *type)
{
	if (glsl_type_is_scalar(type)) {
		return glsl_base_to_llvm_type(ac, glsl_get_base_type(type));
	}

	if (glsl_type_is_vector(type)) {
		return LLVMVectorType(
		   glsl_base_to_llvm_type(ac, glsl_get_base_type(type)),
		   glsl_get_vector_elements(type));
	}

	if (glsl_type_is_matrix(type)) {
		return LLVMArrayType(
		   glsl_to_llvm_type(ac, glsl_get_column_type(type)),
		   glsl_get_matrix_columns(type));
	}

	if (glsl_type_is_array(type)) {
		return LLVMArrayType(
		   glsl_to_llvm_type(ac, glsl_get_array_element(type)),
		   glsl_get_length(type));
	}

	assert(glsl_type_is_struct(type));

	LLVMTypeRef member_types[glsl_get_length(type)];

	for (unsigned i = 0; i < glsl_get_length(type); i++) {
		member_types[i] =
			glsl_to_llvm_type(ac,
					  glsl_get_struct_field(type, i));
	}

	return LLVMStructTypeInContext(ac->context, member_types,
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
				ac_build_alloca_undef(&ctx->ac, ctx->ac.f32, "temp");
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
			   ctx->ac.module, glsl_to_llvm_type(&ctx->ac, variable->type),
			   variable->name ? variable->name : "",
			   AC_LOCAL_ADDR_SPACE);
		_mesa_hash_table_insert(ctx->vars, variable, shared);
	}
}

void ac_nir_translate(struct ac_llvm_context *ac, struct ac_shader_abi *abi,
		      struct nir_shader *nir)
{
	struct ac_nir_context ctx = {};
	struct nir_function *func;

	ctx.ac = *ac;
	ctx.abi = abi;

	ctx.stage = nir->info.stage;

	ctx.main_function = LLVMGetBasicBlockParent(LLVMGetInsertBlock(ctx.ac.builder));

	nir_foreach_variable(variable, &nir->outputs)
		ac_handle_shader_output_decl(&ctx.ac, ctx.abi, nir, variable,
					     ctx.stage);

	ctx.defs = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
	                                   _mesa_key_pointer_equal);
	ctx.phis = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
	                                   _mesa_key_pointer_equal);
	ctx.vars = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
	                                   _mesa_key_pointer_equal);

	func = (struct nir_function *)exec_list_get_head(&nir->functions);

	nir_index_ssa_defs(func->impl);
	ctx.ssa_defs = calloc(func->impl->ssa_alloc, sizeof(LLVMValueRef));

	setup_locals(&ctx, func);

	if (nir->info.stage == MESA_SHADER_COMPUTE)
		setup_shared(&ctx, nir);

	visit_cf_list(&ctx, &func->impl->body);
	phi_post_pass(&ctx);

	if (nir->info.stage != MESA_SHADER_COMPUTE)
		ctx.abi->emit_outputs(ctx.abi, AC_LLVM_MAX_OUTPUTS,
				      ctx.abi->outputs);

	free(ctx.locals);
	free(ctx.ssa_defs);
	ralloc_free(ctx.defs);
	ralloc_free(ctx.phis);
	ralloc_free(ctx.vars);
}

void
ac_lower_indirect_derefs(struct nir_shader *nir, enum chip_class chip_class)
{
	/* While it would be nice not to have this flag, we are constrained
	 * by the reality that LLVM 5.0 doesn't have working VGPR indexing
	 * on GFX9.
	 */
	bool llvm_has_working_vgpr_indexing = chip_class <= VI;

	/* TODO: Indirect indexing of GS inputs is unimplemented.
	 *
	 * TCS and TES load inputs directly from LDS or offchip memory, so
	 * indirect indexing is trivial.
	 */
	nir_variable_mode indirect_mask = 0;
	if (nir->info.stage == MESA_SHADER_GEOMETRY ||
	    (nir->info.stage != MESA_SHADER_TESS_CTRL &&
	     nir->info.stage != MESA_SHADER_TESS_EVAL &&
	     !llvm_has_working_vgpr_indexing)) {
		indirect_mask |= nir_var_shader_in;
	}
	if (!llvm_has_working_vgpr_indexing &&
	    nir->info.stage != MESA_SHADER_TESS_CTRL)
		indirect_mask |= nir_var_shader_out;

	/* TODO: We shouldn't need to do this, however LLVM isn't currently
	 * smart enough to handle indirects without causing excess spilling
	 * causing the gpu to hang.
	 *
	 * See the following thread for more details of the problem:
	 * https://lists.freedesktop.org/archives/mesa-dev/2017-July/162106.html
	 */
	indirect_mask |= nir_var_local;

	nir_lower_indirect_derefs(nir, indirect_mask);
}
