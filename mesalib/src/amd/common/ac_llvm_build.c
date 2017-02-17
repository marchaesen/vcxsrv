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
#include "ac_llvm_build.h"

#include <llvm-c/Core.h>

#include "c11/threads.h"

#include <assert.h>
#include <stdio.h>

#include "ac_llvm_util.h"

#include "util/bitscan.h"
#include "util/macros.h"
#include "sid.h"

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

	ctx->voidt = LLVMVoidTypeInContext(ctx->context);
	ctx->i1 = LLVMInt1TypeInContext(ctx->context);
	ctx->i8 = LLVMInt8TypeInContext(ctx->context);
	ctx->i32 = LLVMIntTypeInContext(ctx->context, 32);
	ctx->f32 = LLVMFloatTypeInContext(ctx->context);
	ctx->v4i32 = LLVMVectorType(ctx->i32, 4);
	ctx->v4f32 = LLVMVectorType(ctx->f32, 4);
	ctx->v16i8 = LLVMVectorType(ctx->i8, 16);

	ctx->range_md_kind = LLVMGetMDKindIDInContext(ctx->context,
						     "range", 5);

	ctx->invariant_load_md_kind = LLVMGetMDKindIDInContext(ctx->context,
							       "invariant.load", 14);

	ctx->fpmath_md_kind = LLVMGetMDKindIDInContext(ctx->context, "fpmath", 6);

	args[0] = LLVMConstReal(ctx->f32, 2.5);
	ctx->fpmath_md_2p5_ulp = LLVMMDNodeInContext(ctx->context, args, 1);

	ctx->uniform_md_kind = LLVMGetMDKindIDInContext(ctx->context,
							"amdgpu.uniform", 14);

	ctx->empty_md = LLVMMDNodeInContext(ctx->context, NULL, 0);
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


LLVMValueRef
ac_build_fs_interp(struct ac_llvm_context *ctx,
		   LLVMValueRef llvm_chan,
		   LLVMValueRef attr_number,
		   LLVMValueRef params,
		   LLVMValueRef i,
		   LLVMValueRef j)
{
	LLVMValueRef args[5];
	LLVMValueRef p1;
	
	if (HAVE_LLVM < 0x0400) {
		LLVMValueRef ij[2];
		ij[0] = LLVMBuildBitCast(ctx->builder, i, ctx->i32, "");
		ij[1] = LLVMBuildBitCast(ctx->builder, j, ctx->i32, "");

		args[0] = llvm_chan;
		args[1] = attr_number;
		args[2] = params;
		args[3] = ac_build_gather_values(ctx, ij, 2);
		return ac_emit_llvm_intrinsic(ctx, "llvm.SI.fs.interp",
					      ctx->f32, args, 4,
					      AC_FUNC_ATTR_READNONE);
	}

	args[0] = i;
	args[1] = llvm_chan;
	args[2] = attr_number;
	args[3] = params;

	p1 = ac_emit_llvm_intrinsic(ctx, "llvm.amdgcn.interp.p1",
				    ctx->f32, args, 4, AC_FUNC_ATTR_READNONE);

	args[0] = p1;
	args[1] = j;
	args[2] = llvm_chan;
	args[3] = attr_number;
	args[4] = params;

	return ac_emit_llvm_intrinsic(ctx, "llvm.amdgcn.interp.p2",
				      ctx->f32, args, 5, AC_FUNC_ATTR_READNONE);
}

LLVMValueRef
ac_build_fs_interp_mov(struct ac_llvm_context *ctx,
		       LLVMValueRef parameter,
		       LLVMValueRef llvm_chan,
		       LLVMValueRef attr_number,
		       LLVMValueRef params)
{
	LLVMValueRef args[4];
	if (HAVE_LLVM < 0x0400) {
		args[0] = llvm_chan;
		args[1] = attr_number;
		args[2] = params;

		return ac_emit_llvm_intrinsic(ctx,
					      "llvm.SI.fs.constant",
					      ctx->f32, args, 3,
					      AC_FUNC_ATTR_READNONE);
	}

	args[0] = parameter;
	args[1] = llvm_chan;
	args[2] = attr_number;
	args[3] = params;

	return ac_emit_llvm_intrinsic(ctx, "llvm.amdgcn.interp.mov",
				      ctx->f32, args, 4, AC_FUNC_ATTR_READNONE);
}

LLVMValueRef
ac_build_gep0(struct ac_llvm_context *ctx,
	      LLVMValueRef base_ptr,
	      LLVMValueRef index)
{
	LLVMValueRef indices[2] = {
		LLVMConstInt(ctx->i32, 0, 0),
		index,
	};
	return LLVMBuildGEP(ctx->builder, base_ptr,
			    indices, 2, "");
}

void
ac_build_indexed_store(struct ac_llvm_context *ctx,
		       LLVMValueRef base_ptr, LLVMValueRef index,
		       LLVMValueRef value)
{
	LLVMBuildStore(ctx->builder, value,
		       ac_build_gep0(ctx, base_ptr, index));
}

/**
 * Build an LLVM bytecode indexed load using LLVMBuildGEP + LLVMBuildLoad.
 * It's equivalent to doing a load from &base_ptr[index].
 *
 * \param base_ptr  Where the array starts.
 * \param index     The element index into the array.
 * \param uniform   Whether the base_ptr and index can be assumed to be
 *                  dynamically uniform
 */
LLVMValueRef
ac_build_indexed_load(struct ac_llvm_context *ctx,
		      LLVMValueRef base_ptr, LLVMValueRef index,
		      bool uniform)
{
	LLVMValueRef pointer;

	pointer = ac_build_gep0(ctx, base_ptr, index);
	if (uniform)
		LLVMSetMetadata(pointer, ctx->uniform_md_kind, ctx->empty_md);
	return LLVMBuildLoad(ctx->builder, pointer, "");
}

/**
 * Do a load from &base_ptr[index], but also add a flag that it's loading
 * a constant from a dynamically uniform index.
 */
LLVMValueRef
ac_build_indexed_load_const(struct ac_llvm_context *ctx,
			    LLVMValueRef base_ptr, LLVMValueRef index)
{
	LLVMValueRef result = ac_build_indexed_load(ctx, base_ptr, index, true);
	LLVMSetMetadata(result, ctx->invariant_load_md_kind, ctx->empty_md);
	return result;
}

/* TBUFFER_STORE_FORMAT_{X,XY,XYZ,XYZW} <- the suffix is selected by num_channels=1..4.
 * The type of vdata must be one of i32 (num_channels=1), v2i32 (num_channels=2),
 * or v4i32 (num_channels=3,4).
 */
void
ac_build_tbuffer_store(struct ac_llvm_context *ctx,
		       LLVMValueRef rsrc,
		       LLVMValueRef vdata,
		       unsigned num_channels,
		       LLVMValueRef vaddr,
		       LLVMValueRef soffset,
		       unsigned inst_offset,
		       unsigned dfmt,
		       unsigned nfmt,
		       unsigned offen,
		       unsigned idxen,
		       unsigned glc,
		       unsigned slc,
		       unsigned tfe)
{
	LLVMValueRef args[] = {
		rsrc,
		vdata,
		LLVMConstInt(ctx->i32, num_channels, 0),
		vaddr,
		soffset,
		LLVMConstInt(ctx->i32, inst_offset, 0),
		LLVMConstInt(ctx->i32, dfmt, 0),
		LLVMConstInt(ctx->i32, nfmt, 0),
		LLVMConstInt(ctx->i32, offen, 0),
		LLVMConstInt(ctx->i32, idxen, 0),
		LLVMConstInt(ctx->i32, glc, 0),
		LLVMConstInt(ctx->i32, slc, 0),
		LLVMConstInt(ctx->i32, tfe, 0)
	};

	/* The instruction offset field has 12 bits */
	assert(offen || inst_offset < (1 << 12));

	/* The intrinsic is overloaded, we need to add a type suffix for overloading to work. */
	unsigned func = CLAMP(num_channels, 1, 3) - 1;
	const char *types[] = {"i32", "v2i32", "v4i32"};
	char name[256];
	snprintf(name, sizeof(name), "llvm.SI.tbuffer.store.%s", types[func]);

	ac_emit_llvm_intrinsic(ctx, name, ctx->voidt,
			       args, ARRAY_SIZE(args), 0);
}

void
ac_build_tbuffer_store_dwords(struct ac_llvm_context *ctx,
			      LLVMValueRef rsrc,
			      LLVMValueRef vdata,
			      unsigned num_channels,
			      LLVMValueRef vaddr,
			      LLVMValueRef soffset,
			      unsigned inst_offset)
{
	static unsigned dfmt[] = {
		V_008F0C_BUF_DATA_FORMAT_32,
		V_008F0C_BUF_DATA_FORMAT_32_32,
		V_008F0C_BUF_DATA_FORMAT_32_32_32,
		V_008F0C_BUF_DATA_FORMAT_32_32_32_32
	};
	assert(num_channels >= 1 && num_channels <= 4);

	ac_build_tbuffer_store(ctx, rsrc, vdata, num_channels, vaddr, soffset,
			       inst_offset, dfmt[num_channels - 1],
			       V_008F0C_BUF_NUM_FORMAT_UINT, 1, 0, 1, 1, 0);
}

LLVMValueRef
ac_build_buffer_load(struct ac_llvm_context *ctx,
		     LLVMValueRef rsrc,
		     int num_channels,
		     LLVMValueRef vindex,
		     LLVMValueRef voffset,
		     LLVMValueRef soffset,
		     unsigned inst_offset,
		     unsigned glc,
		     unsigned slc)
{
	unsigned func = CLAMP(num_channels, 1, 3) - 1;

	if (HAVE_LLVM >= 0x309) {
		LLVMValueRef args[] = {
			LLVMBuildBitCast(ctx->builder, rsrc, ctx->v4i32, ""),
			vindex ? vindex : LLVMConstInt(ctx->i32, 0, 0),
			LLVMConstInt(ctx->i32, inst_offset, 0),
			LLVMConstInt(ctx->i1, glc, 0),
			LLVMConstInt(ctx->i1, slc, 0)
		};

		LLVMTypeRef types[] = {ctx->f32, LLVMVectorType(ctx->f32, 2),
		                       ctx->v4f32};
		const char *type_names[] = {"f32", "v2f32", "v4f32"};
		char name[256];

		if (voffset) {
			args[2] = LLVMBuildAdd(ctx->builder, args[2], voffset,
			                       "");
		}

		if (soffset) {
			args[2] = LLVMBuildAdd(ctx->builder, args[2], soffset,
			                       "");
		}

		snprintf(name, sizeof(name), "llvm.amdgcn.buffer.load.%s",
		         type_names[func]);

		return ac_emit_llvm_intrinsic(ctx, name, types[func], args,
					      ARRAY_SIZE(args), AC_FUNC_ATTR_READONLY);
	} else {
		LLVMValueRef args[] = {
			LLVMBuildBitCast(ctx->builder, rsrc, ctx->v16i8, ""),
			voffset ? voffset : vindex,
			soffset,
			LLVMConstInt(ctx->i32, inst_offset, 0),
			LLVMConstInt(ctx->i32, voffset ? 1 : 0, 0), // offen
			LLVMConstInt(ctx->i32, vindex ? 1 : 0, 0), //idxen
			LLVMConstInt(ctx->i32, glc, 0),
			LLVMConstInt(ctx->i32, slc, 0),
			LLVMConstInt(ctx->i32, 0, 0), // TFE
		};

		LLVMTypeRef types[] = {ctx->i32, LLVMVectorType(ctx->i32, 2),
		                       ctx->v4i32};
		const char *type_names[] = {"i32", "v2i32", "v4i32"};
		const char *arg_type = "i32";
		char name[256];

		if (voffset && vindex) {
			LLVMValueRef vaddr[] = {vindex, voffset};

			arg_type = "v2i32";
			args[1] = ac_build_gather_values(ctx, vaddr, 2);
		}

		snprintf(name, sizeof(name), "llvm.SI.buffer.load.dword.%s.%s",
		         type_names[func], arg_type);

		return ac_emit_llvm_intrinsic(ctx, name, types[func], args,
					       ARRAY_SIZE(args), AC_FUNC_ATTR_READONLY);
	}
}

/**
 * Set range metadata on an instruction.  This can only be used on load and
 * call instructions.  If you know an instruction can only produce the values
 * 0, 1, 2, you would do set_range_metadata(value, 0, 3);
 * \p lo is the minimum value inclusive.
 * \p hi is the maximum value exclusive.
 */
static void set_range_metadata(struct ac_llvm_context *ctx,
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

LLVMValueRef
ac_get_thread_id(struct ac_llvm_context *ctx)
{
	LLVMValueRef tid;

	if (HAVE_LLVM < 0x0308) {
		tid = ac_emit_llvm_intrinsic(ctx, "llvm.SI.tid",
					     ctx->i32,
					     NULL, 0, AC_FUNC_ATTR_READNONE);
	} else {
		LLVMValueRef tid_args[2];
		tid_args[0] = LLVMConstInt(ctx->i32, 0xffffffff, false);
		tid_args[1] = LLVMConstInt(ctx->i32, 0, false);
		tid_args[1] = ac_emit_llvm_intrinsic(ctx,
						     "llvm.amdgcn.mbcnt.lo", ctx->i32,
						     tid_args, 2, AC_FUNC_ATTR_READNONE);

		tid = ac_emit_llvm_intrinsic(ctx, "llvm.amdgcn.mbcnt.hi",
					     ctx->i32, tid_args,
					     2, AC_FUNC_ATTR_READNONE);
	}
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
LLVMValueRef
ac_emit_ddxy(struct ac_llvm_context *ctx,
	     bool has_ds_bpermute,
	     uint32_t mask,
	     int idx,
	     LLVMValueRef lds,
	     LLVMValueRef val)
{
	LLVMValueRef thread_id, tl, trbl, tl_tid, trbl_tid, args[2];
	LLVMValueRef result;

	thread_id = ac_get_thread_id(ctx);

	tl_tid = LLVMBuildAnd(ctx->builder, thread_id,
			      LLVMConstInt(ctx->i32, mask, false), "");

	trbl_tid = LLVMBuildAdd(ctx->builder, tl_tid,
				LLVMConstInt(ctx->i32, idx, false), "");

	if (has_ds_bpermute) {
		args[0] = LLVMBuildMul(ctx->builder, tl_tid,
				       LLVMConstInt(ctx->i32, 4, false), "");
		args[1] = val;
		tl = ac_emit_llvm_intrinsic(ctx,
					    "llvm.amdgcn.ds.bpermute", ctx->i32,
					    args, 2, AC_FUNC_ATTR_READNONE);

		args[0] = LLVMBuildMul(ctx->builder, trbl_tid,
				       LLVMConstInt(ctx->i32, 4, false), "");
		trbl = ac_emit_llvm_intrinsic(ctx,
					      "llvm.amdgcn.ds.bpermute", ctx->i32,
					      args, 2, AC_FUNC_ATTR_READNONE);
	} else {
		LLVMValueRef store_ptr, load_ptr0, load_ptr1;

		store_ptr = ac_build_gep0(ctx, lds, thread_id);
		load_ptr0 = ac_build_gep0(ctx, lds, tl_tid);
		load_ptr1 = ac_build_gep0(ctx, lds, trbl_tid);

		LLVMBuildStore(ctx->builder, val, store_ptr);
		tl = LLVMBuildLoad(ctx->builder, load_ptr0, "");
		trbl = LLVMBuildLoad(ctx->builder, load_ptr1, "");
	}

	tl = LLVMBuildBitCast(ctx->builder, tl, ctx->f32, "");
	trbl = LLVMBuildBitCast(ctx->builder, trbl, ctx->f32, "");
	result = LLVMBuildFSub(ctx->builder, trbl, tl, "");
	return result;
}

void
ac_emit_sendmsg(struct ac_llvm_context *ctx,
		uint32_t msg,
		LLVMValueRef wave_id)
{
	LLVMValueRef args[2];
	const char *intr_name = (HAVE_LLVM < 0x0400) ? "llvm.SI.sendmsg" : "llvm.amdgcn.s.sendmsg";
	args[0] = LLVMConstInt(ctx->i32, msg, false);
	args[1] = wave_id;
	ac_emit_llvm_intrinsic(ctx, intr_name, ctx->voidt,
			       args, 2, 0);
}

LLVMValueRef
ac_emit_imsb(struct ac_llvm_context *ctx,
	     LLVMValueRef arg,
	     LLVMTypeRef dst_type)
{
	const char *intr_name = (HAVE_LLVM < 0x0400) ? "llvm.AMDGPU.flbit.i32" : "llvm.amdgcn.sffbh";
	LLVMValueRef msb = ac_emit_llvm_intrinsic(ctx, intr_name,
						  dst_type, &arg, 1,
						  AC_FUNC_ATTR_READNONE);

	/* The HW returns the last bit index from MSB, but NIR/TGSI wants
	 * the index from LSB. Invert it by doing "31 - msb". */
	msb = LLVMBuildSub(ctx->builder, LLVMConstInt(ctx->i32, 31, false),
			   msb, "");

	LLVMValueRef all_ones = LLVMConstInt(ctx->i32, -1, true);
	LLVMValueRef cond = LLVMBuildOr(ctx->builder,
					LLVMBuildICmp(ctx->builder, LLVMIntEQ,
						      arg, LLVMConstInt(ctx->i32, 0, 0), ""),
					LLVMBuildICmp(ctx->builder, LLVMIntEQ,
						      arg, all_ones, ""), "");

	return LLVMBuildSelect(ctx->builder, cond, all_ones, msb, "");
}

LLVMValueRef
ac_emit_umsb(struct ac_llvm_context *ctx,
	     LLVMValueRef arg,
	     LLVMTypeRef dst_type)
{
	LLVMValueRef args[2] = {
		arg,
		LLVMConstInt(ctx->i32, 1, 0),
	};
	LLVMValueRef msb = ac_emit_llvm_intrinsic(ctx, "llvm.ctlz.i32",
						  dst_type, args, ARRAY_SIZE(args),
						  AC_FUNC_ATTR_READNONE);

	/* The HW returns the last bit index from MSB, but TGSI/NIR wants
	 * the index from LSB. Invert it by doing "31 - msb". */
	msb = LLVMBuildSub(ctx->builder, LLVMConstInt(ctx->i32, 31, false),
			   msb, "");

	/* check for zero */
	return LLVMBuildSelect(ctx->builder,
			       LLVMBuildICmp(ctx->builder, LLVMIntEQ, arg,
					     LLVMConstInt(ctx->i32, 0, 0), ""),
			       LLVMConstInt(ctx->i32, -1, true), msb, "");
}
