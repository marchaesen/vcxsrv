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
#if HAVE_LLVM >= 0x0307
#  pragma push_macro("DEBUG")
#  undef DEBUG
#endif

#include "ac_llvm_util.h"
#include <llvm-c/Core.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/ExecutionEngine/ExecutionEngine.h>
#include <llvm/IR/Attributes.h>

void ac_add_attr_dereferenceable(LLVMValueRef val, uint64_t bytes)
{
   llvm::Argument *A = llvm::unwrap<llvm::Argument>(val);
   llvm::AttrBuilder B;
   B.addDereferenceableAttr(bytes);
   A->addAttr(llvm::AttributeSet::get(A->getContext(), A->getArgNo() + 1,  B));
}

bool ac_is_sgpr_param(LLVMValueRef arg)
{
	llvm::Argument *A = llvm::unwrap<llvm::Argument>(arg);
	llvm::AttributeSet AS = A->getParent()->getAttributes();
	unsigned ArgNo = A->getArgNo();
	return AS.hasAttribute(ArgNo + 1, llvm::Attribute::ByVal) ||
	       AS.hasAttribute(ArgNo + 1, llvm::Attribute::InReg);
}
