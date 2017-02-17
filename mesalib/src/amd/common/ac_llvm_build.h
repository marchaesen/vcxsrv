/*
 * Copyright 2016 Bas Nieuwenhuizen
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
#ifndef AC_LLVM_BUILD_H
#define AC_LLVM_BUILD_H

#include <stdbool.h>
#include <llvm-c/TargetMachine.h>

#ifdef __cplusplus
extern "C" {
#endif

struct ac_llvm_context {
	LLVMContextRef context;
	LLVMModuleRef module;
	LLVMBuilderRef builder;

	LLVMTypeRef voidt;
	LLVMTypeRef i1;
	LLVMTypeRef i8;
	LLVMTypeRef i32;
	LLVMTypeRef f32;
	LLVMTypeRef v4i32;
	LLVMTypeRef v4f32;
	LLVMTypeRef v16i8;

	unsigned range_md_kind;
	unsigned invariant_load_md_kind;
	unsigned uniform_md_kind;
	unsigned fpmath_md_kind;
	LLVMValueRef fpmath_md_2p5_ulp;
	LLVMValueRef empty_md;
};

void
ac_llvm_context_init(struct ac_llvm_context *ctx, LLVMContextRef context);

LLVMValueRef
ac_emit_llvm_intrinsic(struct ac_llvm_context *ctx, const char *name,
		       LLVMTypeRef return_type, LLVMValueRef *params,
		       unsigned param_count, unsigned attrib_mask);

LLVMValueRef
ac_build_gather_values_extended(struct ac_llvm_context *ctx,
				LLVMValueRef *values,
				unsigned value_count,
				unsigned value_stride,
				bool load);
LLVMValueRef
ac_build_gather_values(struct ac_llvm_context *ctx,
		       LLVMValueRef *values,
		       unsigned value_count);

LLVMValueRef
ac_emit_fdiv(struct ac_llvm_context *ctx,
	     LLVMValueRef num,
	     LLVMValueRef den);

void
ac_prepare_cube_coords(struct ac_llvm_context *ctx,
		       bool is_deriv, bool is_array,
		       LLVMValueRef *coords_arg,
		       LLVMValueRef *derivs_arg);


LLVMValueRef
ac_build_fs_interp(struct ac_llvm_context *ctx,
		   LLVMValueRef llvm_chan,
		   LLVMValueRef attr_number,
		   LLVMValueRef params,
		   LLVMValueRef i,
		   LLVMValueRef j);

LLVMValueRef
ac_build_fs_interp_mov(struct ac_llvm_context *ctx,
		       LLVMValueRef parameter,
		       LLVMValueRef llvm_chan,
		       LLVMValueRef attr_number,
		       LLVMValueRef params);

LLVMValueRef
ac_build_gep0(struct ac_llvm_context *ctx,
	      LLVMValueRef base_ptr,
	      LLVMValueRef index);

void
ac_build_indexed_store(struct ac_llvm_context *ctx,
		       LLVMValueRef base_ptr, LLVMValueRef index,
		       LLVMValueRef value);

LLVMValueRef
ac_build_indexed_load(struct ac_llvm_context *ctx,
		      LLVMValueRef base_ptr, LLVMValueRef index,
		      bool uniform);

LLVMValueRef
ac_build_indexed_load_const(struct ac_llvm_context *ctx,
			    LLVMValueRef base_ptr, LLVMValueRef index);

void
ac_build_tbuffer_store_dwords(struct ac_llvm_context *ctx,
			      LLVMValueRef rsrc,
			      LLVMValueRef vdata,
			      unsigned num_channels,
			      LLVMValueRef vaddr,
			      LLVMValueRef soffset,
			      unsigned inst_offset);

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
		       unsigned tfe);

LLVMValueRef
ac_build_buffer_load(struct ac_llvm_context *ctx,
		     LLVMValueRef rsrc,
		     int num_channels,
		     LLVMValueRef vindex,
		     LLVMValueRef voffset,
		     LLVMValueRef soffset,
		     unsigned inst_offset,
		     unsigned glc,
		     unsigned slc);

LLVMValueRef
ac_get_thread_id(struct ac_llvm_context *ctx);

#define AC_TID_MASK_TOP_LEFT 0xfffffffc
#define AC_TID_MASK_TOP      0xfffffffd
#define AC_TID_MASK_LEFT     0xfffffffe

LLVMValueRef
ac_emit_ddxy(struct ac_llvm_context *ctx,
	     bool has_ds_bpermute,
	     uint32_t mask,
	     int idx,
	     LLVMValueRef lds,
	     LLVMValueRef val);

#define AC_SENDMSG_GS 2
#define AC_SENDMSG_GS_DONE 3

#define AC_SENDMSG_GS_OP_NOP      (0 << 4)
#define AC_SENDMSG_GS_OP_CUT      (1 << 4)
#define AC_SENDMSG_GS_OP_EMIT     (2 << 4)
#define AC_SENDMSG_GS_OP_EMIT_CUT (3 << 4)

void ac_emit_sendmsg(struct ac_llvm_context *ctx,
		     uint32_t msg,
		     LLVMValueRef wave_id);

LLVMValueRef ac_emit_imsb(struct ac_llvm_context *ctx,
			  LLVMValueRef arg,
			  LLVMTypeRef dst_type);

LLVMValueRef ac_emit_umsb(struct ac_llvm_context *ctx,
			  LLVMValueRef arg,
			  LLVMTypeRef dst_type);

#ifdef __cplusplus
}
#endif

#endif
