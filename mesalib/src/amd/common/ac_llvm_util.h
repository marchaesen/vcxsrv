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
#pragma once

#include <stdbool.h>
#include <llvm-c/TargetMachine.h>

#include "amd_family.h"

#ifdef __cplusplus
extern "C" {
#endif

enum ac_func_attr {
	AC_FUNC_ATTR_ALWAYSINLINE = (1 << 0),
	AC_FUNC_ATTR_BYVAL        = (1 << 1),
	AC_FUNC_ATTR_INREG        = (1 << 2),
	AC_FUNC_ATTR_NOALIAS      = (1 << 3),
	AC_FUNC_ATTR_NOUNWIND     = (1 << 4),
	AC_FUNC_ATTR_READNONE     = (1 << 5),
	AC_FUNC_ATTR_READONLY     = (1 << 6),
	AC_FUNC_ATTR_LAST         = (1 << 7)
};

struct ac_llvm_context {
	LLVMContextRef context;
	LLVMModuleRef module;
	LLVMBuilderRef builder;

	LLVMTypeRef i32;
	LLVMTypeRef f32;

	unsigned fpmath_md_kind;
	LLVMValueRef fpmath_md_2p5_ulp;
};

LLVMTargetMachineRef ac_create_target_machine(enum radeon_family family, bool supports_spill);

void ac_add_attr_dereferenceable(LLVMValueRef val, uint64_t bytes);
bool ac_is_sgpr_param(LLVMValueRef param);

void
ac_llvm_context_init(struct ac_llvm_context *ctx, LLVMContextRef context);

void
ac_add_function_attr(LLVMValueRef function,
                     int attr_idx,
                     enum ac_func_attr attr);
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

void
ac_dump_module(LLVMModuleRef module);

#ifdef __cplusplus
}
#endif
