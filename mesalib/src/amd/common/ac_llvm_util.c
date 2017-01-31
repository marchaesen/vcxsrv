/*
 * Copyright 2014 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS, AUTHORS AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 */
/* based on pieces from si_pipe.c and radeon_llvm_emit.c */
#include "ac_llvm_util.h"

#include <llvm-c/Core.h>

#include "c11/threads.h"

#include <assert.h>
#include <stdio.h>

#include "util/bitscan.h"
#include "util/macros.h"

static void ac_init_llvm_target()
{
#if HAVE_LLVM < 0x0307
	LLVMInitializeR600TargetInfo();
	LLVMInitializeR600Target();
	LLVMInitializeR600TargetMC();
	LLVMInitializeR600AsmPrinter();
#else
	LLVMInitializeAMDGPUTargetInfo();
	LLVMInitializeAMDGPUTarget();
	LLVMInitializeAMDGPUTargetMC();
	LLVMInitializeAMDGPUAsmPrinter();
#endif
}

static once_flag ac_init_llvm_target_once_flag = ONCE_FLAG_INIT;

static LLVMTargetRef ac_get_llvm_target(const char *triple)
{
	LLVMTargetRef target = NULL;
	char *err_message = NULL;

	call_once(&ac_init_llvm_target_once_flag, ac_init_llvm_target);

	if (LLVMGetTargetFromTriple(triple, &target, &err_message)) {
		fprintf(stderr, "Cannot find target for triple %s ", triple);
		if (err_message) {
			fprintf(stderr, "%s\n", err_message);
		}
		LLVMDisposeMessage(err_message);
		return NULL;
	}
	return target;
}

static const char *ac_get_llvm_processor_name(enum radeon_family family)
{
	switch (family) {
	case CHIP_TAHITI:
		return "tahiti";
	case CHIP_PITCAIRN:
		return "pitcairn";
	case CHIP_VERDE:
		return "verde";
	case CHIP_OLAND:
		return "oland";
	case CHIP_HAINAN:
		return "hainan";
	case CHIP_BONAIRE:
		return "bonaire";
	case CHIP_KABINI:
		return "kabini";
	case CHIP_KAVERI:
		return "kaveri";
	case CHIP_HAWAII:
		return "hawaii";
	case CHIP_MULLINS:
		return "mullins";
	case CHIP_TONGA:
		return "tonga";
	case CHIP_ICELAND:
		return "iceland";
	case CHIP_CARRIZO:
		return "carrizo";
#if HAVE_LLVM <= 0x0307
	case CHIP_FIJI:
		return "tonga";
	case CHIP_STONEY:
		return "carrizo";
#else
	case CHIP_FIJI:
		return "fiji";
	case CHIP_STONEY:
		return "stoney";
#endif
#if HAVE_LLVM <= 0x0308
	case CHIP_POLARIS10:
		return "tonga";
	case CHIP_POLARIS11:
		return "tonga";
#else
	case CHIP_POLARIS10:
		return "polaris10";
	case CHIP_POLARIS11:
		return "polaris11";
#endif
	default:
		return "";
	}
}

LLVMTargetMachineRef ac_create_target_machine(enum radeon_family family, bool supports_spill)
{
	assert(family >= CHIP_TAHITI);

	const char *triple = supports_spill ? "amdgcn-mesa-mesa3d" : "amdgcn--";
	LLVMTargetRef target = ac_get_llvm_target(triple);
	LLVMTargetMachineRef tm = LLVMCreateTargetMachine(
	                             target,
	                             triple,
	                             ac_get_llvm_processor_name(family),
	                             "+DumpCode,+vgpr-spilling",
	                             LLVMCodeGenLevelDefault,
	                             LLVMRelocDefault,
	                             LLVMCodeModelDefault);

	return tm;
}

/* Initialize module-independent parts of the context.
 *
 * The caller is responsible for initializing ctx::module and ctx::builder.
 */
void
ac_llvm_context_init(struct ac_llvm_context *ctx, LLVMContextRef context)
{
	LLVMValueRef args[1];

	ctx->context = context;
	ctx->module = NULL;
	ctx->builder = NULL;

	ctx->i32 = LLVMIntTypeInContext(ctx->context, 32);
	ctx->f32 = LLVMFloatTypeInContext(ctx->context);

	ctx->fpmath_md_kind = LLVMGetMDKindIDInContext(ctx->context, "fpmath", 6);

	args[0] = LLVMConstReal(ctx->f32, 2.5);
	ctx->fpmath_md_2p5_ulp = LLVMMDNodeInContext(ctx->context, args, 1);
}

#if HAVE_LLVM < 0x0400
static LLVMAttribute ac_attr_to_llvm_attr(enum ac_func_attr attr)
{
   switch (attr) {
   case AC_FUNC_ATTR_ALWAYSINLINE: return LLVMAlwaysInlineAttribute;
   case AC_FUNC_ATTR_BYVAL: return LLVMByValAttribute;
   case AC_FUNC_ATTR_INREG: return LLVMInRegAttribute;
   case AC_FUNC_ATTR_NOALIAS: return LLVMNoAliasAttribute;
   case AC_FUNC_ATTR_NOUNWIND: return LLVMNoUnwindAttribute;
   case AC_FUNC_ATTR_READNONE: return LLVMReadNoneAttribute;
   case AC_FUNC_ATTR_READONLY: return LLVMReadOnlyAttribute;
   default:
	   fprintf(stderr, "Unhandled function attribute: %x\n", attr);
	   return 0;
   }
}

#else

static const char *attr_to_str(enum ac_func_attr attr)
{
   switch (attr) {
   case AC_FUNC_ATTR_ALWAYSINLINE: return "alwaysinline";
   case AC_FUNC_ATTR_BYVAL: return "byval";
   case AC_FUNC_ATTR_INREG: return "inreg";
   case AC_FUNC_ATTR_NOALIAS: return "noalias";
   case AC_FUNC_ATTR_NOUNWIND: return "nounwind";
   case AC_FUNC_ATTR_READNONE: return "readnone";
   case AC_FUNC_ATTR_READONLY: return "readonly";
   default:
	   fprintf(stderr, "Unhandled function attribute: %x\n", attr);
	   return 0;
   }
}

#endif

void
ac_add_function_attr(LLVMValueRef function,
                     int attr_idx,
                     enum ac_func_attr attr)
{

#if HAVE_LLVM < 0x0400
   LLVMAttribute llvm_attr = ac_attr_to_llvm_attr(attr);
   if (attr_idx == -1) {
      LLVMAddFunctionAttr(function, llvm_attr);
   } else {
      LLVMAddAttribute(LLVMGetParam(function, attr_idx - 1), llvm_attr);
   }
#else
   LLVMContextRef context = LLVMGetModuleContext(LLVMGetGlobalParent(function));
   const char *attr_name = attr_to_str(attr);
   unsigned kind_id = LLVMGetEnumAttributeKindForName(attr_name,
                                                      strlen(attr_name));
   LLVMAttributeRef llvm_attr = LLVMCreateEnumAttribute(context, kind_id, 0);
   LLVMAddAttributeAtIndex(function, attr_idx, llvm_attr);
#endif
}

LLVMValueRef
ac_emit_llvm_intrinsic(struct ac_llvm_context *ctx, const char *name,
		       LLVMTypeRef return_type, LLVMValueRef *params,
		       unsigned param_count, unsigned attrib_mask)
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

		attrib_mask |= AC_FUNC_ATTR_NOUNWIND;
		while (attrib_mask) {
			enum ac_func_attr attr = 1u << u_bit_scan(&attrib_mask);
			ac_add_function_attr(function, -1, attr);
		}
	}
	return LLVMBuildCall(ctx->builder, function, params, param_count, "");
}

LLVMValueRef
ac_build_gather_values_extended(struct ac_llvm_context *ctx,
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
	} else if (!value_count)
		unreachable("value_count is 0");

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

LLVMValueRef
ac_build_gather_values(struct ac_llvm_context *ctx,
		       LLVMValueRef *values,
		       unsigned value_count)
{
	return ac_build_gather_values_extended(ctx, values, value_count, 1, false);
}

LLVMValueRef
ac_emit_fdiv(struct ac_llvm_context *ctx,
	     LLVMValueRef num,
	     LLVMValueRef den)
{
	LLVMValueRef ret = LLVMBuildFDiv(ctx->builder, num, den, "");

	if (!LLVMIsConstant(ret))
		LLVMSetMetadata(ret, ctx->fpmath_md_kind, ctx->fpmath_md_2p5_ulp);
	return ret;
}

/* Coordinates for cube map selection. sc, tc, and ma are as in Table 8.27
 * of the OpenGL 4.5 (Compatibility Profile) specification, except ma is
 * already multiplied by two. id is the cube face number.
 */
struct cube_selection_coords {
	LLVMValueRef stc[2];
	LLVMValueRef ma;
	LLVMValueRef id;
};

static void
build_cube_intrinsic(struct ac_llvm_context *ctx,
		     LLVMValueRef in[3],
		     struct cube_selection_coords *out)
{
	LLVMBuilderRef builder = ctx->builder;

	if (HAVE_LLVM >= 0x0309) {
		LLVMTypeRef f32 = ctx->f32;

		out->stc[1] = ac_emit_llvm_intrinsic(ctx, "llvm.amdgcn.cubetc",
					f32, in, 3, AC_FUNC_ATTR_READNONE);
		out->stc[0] = ac_emit_llvm_intrinsic(ctx, "llvm.amdgcn.cubesc",
					f32, in, 3, AC_FUNC_ATTR_READNONE);
		out->ma = ac_emit_llvm_intrinsic(ctx, "llvm.amdgcn.cubema",
					f32, in, 3, AC_FUNC_ATTR_READNONE);
		out->id = ac_emit_llvm_intrinsic(ctx, "llvm.amdgcn.cubeid",
					f32, in, 3, AC_FUNC_ATTR_READNONE);
	} else {
		LLVMValueRef c[4] = {
			in[0],
			in[1],
			in[2],
			LLVMGetUndef(LLVMTypeOf(in[0]))
		};
		LLVMValueRef vec = ac_build_gather_values(ctx, c, 4);

		LLVMValueRef tmp =
			ac_emit_llvm_intrinsic(ctx, "llvm.AMDGPU.cube",
					  LLVMTypeOf(vec), &vec, 1,
					  AC_FUNC_ATTR_READNONE);

		out->stc[1] = LLVMBuildExtractElement(builder, tmp,
				LLVMConstInt(ctx->i32, 0, 0), "");
		out->stc[0] = LLVMBuildExtractElement(builder, tmp,
				LLVMConstInt(ctx->i32, 1, 0), "");
		out->ma = LLVMBuildExtractElement(builder, tmp,
				LLVMConstInt(ctx->i32, 2, 0), "");
		out->id = LLVMBuildExtractElement(builder, tmp,
				LLVMConstInt(ctx->i32, 3, 0), "");
	}
}

/**
 * Build a manual selection sequence for cube face sc/tc coordinates and
 * major axis vector (multiplied by 2 for consistency) for the given
 * vec3 \p coords, for the face implied by \p selcoords.
 *
 * For the major axis, we always adjust the sign to be in the direction of
 * selcoords.ma; i.e., a positive out_ma means that coords is pointed towards
 * the selcoords major axis.
 */
static void build_cube_select(LLVMBuilderRef builder,
			      const struct cube_selection_coords *selcoords,
			      const LLVMValueRef *coords,
			      LLVMValueRef *out_st,
			      LLVMValueRef *out_ma)
{
	LLVMTypeRef f32 = LLVMTypeOf(coords[0]);
	LLVMValueRef is_ma_positive;
	LLVMValueRef sgn_ma;
	LLVMValueRef is_ma_z, is_not_ma_z;
	LLVMValueRef is_ma_y;
	LLVMValueRef is_ma_x;
	LLVMValueRef sgn;
	LLVMValueRef tmp;

	is_ma_positive = LLVMBuildFCmp(builder, LLVMRealUGE,
		selcoords->ma, LLVMConstReal(f32, 0.0), "");
	sgn_ma = LLVMBuildSelect(builder, is_ma_positive,
		LLVMConstReal(f32, 1.0), LLVMConstReal(f32, -1.0), "");

	is_ma_z = LLVMBuildFCmp(builder, LLVMRealUGE, selcoords->id, LLVMConstReal(f32, 4.0), "");
	is_not_ma_z = LLVMBuildNot(builder, is_ma_z, "");
	is_ma_y = LLVMBuildAnd(builder, is_not_ma_z,
		LLVMBuildFCmp(builder, LLVMRealUGE, selcoords->id, LLVMConstReal(f32, 2.0), ""), "");
	is_ma_x = LLVMBuildAnd(builder, is_not_ma_z, LLVMBuildNot(builder, is_ma_y, ""), "");

	/* Select sc */
	tmp = LLVMBuildSelect(builder, is_ma_z, coords[2], coords[0], "");
	sgn = LLVMBuildSelect(builder, is_ma_y, LLVMConstReal(f32, 1.0),
		LLVMBuildSelect(builder, is_ma_x, sgn_ma,
			LLVMBuildFNeg(builder, sgn_ma, ""), ""), "");
	out_st[0] = LLVMBuildFMul(builder, tmp, sgn, "");

	/* Select tc */
	tmp = LLVMBuildSelect(builder, is_ma_y, coords[2], coords[1], "");
	sgn = LLVMBuildSelect(builder, is_ma_y, LLVMBuildFNeg(builder, sgn_ma, ""),
		LLVMConstReal(f32, -1.0), "");
	out_st[1] = LLVMBuildFMul(builder, tmp, sgn, "");

	/* Select ma */
	tmp = LLVMBuildSelect(builder, is_ma_z, coords[2],
		LLVMBuildSelect(builder, is_ma_y, coords[1], coords[0], ""), "");
	sgn = LLVMBuildSelect(builder, is_ma_positive,
		LLVMConstReal(f32, 2.0), LLVMConstReal(f32, -2.0), "");
	*out_ma = LLVMBuildFMul(builder, tmp, sgn, "");
}

void
ac_prepare_cube_coords(struct ac_llvm_context *ctx,
		       bool is_deriv, bool is_array,
		       LLVMValueRef *coords_arg,
		       LLVMValueRef *derivs_arg)
{

	LLVMBuilderRef builder = ctx->builder;
	struct cube_selection_coords selcoords;
	LLVMValueRef coords[3];
	LLVMValueRef invma;

	build_cube_intrinsic(ctx, coords_arg, &selcoords);

	invma = ac_emit_llvm_intrinsic(ctx, "llvm.fabs.f32",
			ctx->f32, &selcoords.ma, 1, AC_FUNC_ATTR_READNONE);
	invma = ac_emit_fdiv(ctx, LLVMConstReal(ctx->f32, 1.0), invma);

	for (int i = 0; i < 2; ++i)
		coords[i] = LLVMBuildFMul(builder, selcoords.stc[i], invma, "");

	coords[2] = selcoords.id;

	if (is_deriv && derivs_arg) {
		LLVMValueRef derivs[4];
		int axis;

		/* Convert cube derivatives to 2D derivatives. */
		for (axis = 0; axis < 2; axis++) {
			LLVMValueRef deriv_st[2];
			LLVMValueRef deriv_ma;

			/* Transform the derivative alongside the texture
			 * coordinate. Mathematically, the correct formula is
			 * as follows. Assume we're projecting onto the +Z face
			 * and denote by dx/dh the derivative of the (original)
			 * X texture coordinate with respect to horizontal
			 * window coordinates. The projection onto the +Z face
			 * plane is:
			 *
			 *   f(x,z) = x/z
			 *
			 * Then df/dh = df/dx * dx/dh + df/dz * dz/dh
			 *            = 1/z * dx/dh - x/z * 1/z * dz/dh.
			 *
			 * This motivatives the implementation below.
			 *
			 * Whether this actually gives the expected results for
			 * apps that might feed in derivatives obtained via
			 * finite differences is anyone's guess. The OpenGL spec
			 * seems awfully quiet about how textureGrad for cube
			 * maps should be handled.
			 */
			build_cube_select(builder, &selcoords, &derivs_arg[axis * 3],
					  deriv_st, &deriv_ma);

			deriv_ma = LLVMBuildFMul(builder, deriv_ma, invma, "");

			for (int i = 0; i < 2; ++i)
				derivs[axis * 2 + i] =
					LLVMBuildFSub(builder,
						LLVMBuildFMul(builder, deriv_st[i], invma, ""),
						LLVMBuildFMul(builder, deriv_ma, coords[i], ""), "");
		}

		memcpy(derivs_arg, derivs, sizeof(derivs));
	}

	/* Shift the texture coordinate. This must be applied after the
	 * derivative calculation.
	 */
	for (int i = 0; i < 2; ++i)
		coords[i] = LLVMBuildFAdd(builder, coords[i], LLVMConstReal(ctx->f32, 1.5), "");

	if (is_array) {
		/* for cube arrays coord.z = coord.w(array_index) * 8 + face */
		/* coords_arg.w component - array_index for cube arrays */
		LLVMValueRef tmp = LLVMBuildFMul(ctx->builder, coords_arg[3], LLVMConstReal(ctx->f32, 8.0), "");
		coords[2] = LLVMBuildFAdd(ctx->builder, tmp, coords[2], "");
	}

	memcpy(coords_arg, coords, sizeof(coords));
}

void
ac_dump_module(LLVMModuleRef module)
{
	char *str = LLVMPrintModuleToString(module);
	fprintf(stderr, "%s", str);
	LLVMDisposeMessage(str);
}
