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

/* based on Marek's patch to lp_bld_misc.cpp */

// Workaround http://llvm.org/PR23628
#pragma push_macro("DEBUG")
#undef DEBUG

#include "ac_llvm_util.h"
#include <llvm-c/Core.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/Attributes.h>
#include <llvm/IR/CallSite.h>
#include <llvm/IR/IRBuilder.h>

void ac_add_attr_dereferenceable(LLVMValueRef val, uint64_t bytes)
{
   llvm::Argument *A = llvm::unwrap<llvm::Argument>(val);
   A->addAttr(llvm::Attribute::getWithDereferenceableBytes(A->getContext(), bytes));
}

bool ac_is_sgpr_param(LLVMValueRef arg)
{
	llvm::Argument *A = llvm::unwrap<llvm::Argument>(arg);
	llvm::AttributeList AS = A->getParent()->getAttributes();
	unsigned ArgNo = A->getArgNo();
	return AS.hasAttribute(ArgNo + 1, llvm::Attribute::InReg);
}

LLVMValueRef ac_llvm_get_called_value(LLVMValueRef call)
{
	return LLVMGetCalledValue(call);
}

bool ac_llvm_is_function(LLVMValueRef v)
{
	return LLVMGetValueKind(v) == LLVMFunctionValueKind;
}

LLVMBuilderRef ac_create_builder(LLVMContextRef ctx,
				 enum ac_float_mode float_mode)
{
	LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx);

	llvm::FastMathFlags flags;

	switch (float_mode) {
	case AC_FLOAT_MODE_DEFAULT:
		break;
	case AC_FLOAT_MODE_NO_SIGNED_ZEROS_FP_MATH:
		flags.setNoSignedZeros();
		llvm::unwrap(builder)->setFastMathFlags(flags);
		break;
	case AC_FLOAT_MODE_UNSAFE_FP_MATH:
#if HAVE_LLVM >= 0x0600
		flags.setFast();
#else
		flags.setUnsafeAlgebra();
#endif
		llvm::unwrap(builder)->setFastMathFlags(flags);
		break;
	}

	return builder;
}
