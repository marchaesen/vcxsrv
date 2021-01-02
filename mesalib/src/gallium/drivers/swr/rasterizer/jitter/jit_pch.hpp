/****************************************************************************
 * Copyright (C) 2017-2020 Intel Corporation.   All Rights Reserved.
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
 *
 * @file jit_pch.hpp
 *
 * @brief Pre-compiled header for jitter
 *
 * Notes:
 *
 ******************************************************************************/

#pragma once

#if defined(_MSC_VER)
#pragma warning(disable : 4146 4244 4267 4800 4996)
#endif

#include <llvm/Config/llvm-config.h>

#if LLVM_VERSION_MAJOR < 7
// llvm 3.7+ reuses "DEBUG" as an enum value
#pragma push_macro("DEBUG")
#undef DEBUG
#endif

#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/IntrinsicInst.h"
#if LLVM_VERSION_MAJOR >= 10
#include "llvm/IR/IntrinsicsX86.h"
#endif
#include "llvm/ExecutionEngine/ObjectCache.h"

#include "llvm/IR/Verifier.h"
#include "llvm/ExecutionEngine/MCJIT.h"
#include "llvm/Support/FileSystem.h"
#define LLVM_F_NONE sys::fs::F_None

#include "llvm/Analysis/Passes.h"

#include "llvm/IR/LegacyPassManager.h"
using FunctionPassManager = llvm::legacy::FunctionPassManager;
using PassManager         = llvm::legacy::PassManager;

#include "llvm/CodeGen/Passes.h"
#include "llvm/ExecutionEngine/ExecutionEngine.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Support/TargetSelect.h"
#include "llvm/Support/DynamicLibrary.h"
#include "llvm/Transforms/IPO.h"
#include "llvm/Transforms/Scalar.h"
#if LLVM_VERSION_MAJOR >= 7
#include "llvm/Transforms/Utils.h"
#include "llvm/Transforms/InstCombine/InstCombine.h"
#endif
#include "llvm/Support/Host.h"
#include "llvm/Support/DynamicLibrary.h"

#include "llvm/IR/DIBuilder.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Value.h"
#include "llvm/IR/Instructions.h"
#include "llvm/Pass.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
#include "llvm/Transforms/Utils/Cloning.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/SCCIterator.h"
#include "llvm/IR/Dominators.h"
#include "llvm/Analysis/PostDominators.h"
#include "llvm/Analysis/LoopInfo.h"

#include "llvm/Transforms/Utils/Cloning.h"

#if defined(_WIN32)
#include "llvm/ADT/Triple.h"
#endif
#include "llvm/IR/Function.h"

#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/SourceMgr.h"

#include "llvm/Analysis/CFGPrinter.h"
#include "llvm/IRReader/IRReader.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Support/FormattedStream.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Config/llvm-config.h"

#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/Bitcode/BitcodeReader.h"

#if LLVM_USE_INTEL_JITEVENTS
#include "llvm/ExecutionEngine/JITEventListener.h"
#endif

#if LLVM_VERSION_MAJOR >= 5
static const auto                Sync_CrossThread     = llvm::SyncScope::System;
static const auto                Attrib_FunctionIndex = llvm::AttributeList::FunctionIndex;
static inline llvm::AttributeSet GetFuncAttribSet(llvm::LLVMContext&       ctx,
                                                  const llvm::AttrBuilder& b)
{
    return llvm::AttributeSet::get(ctx, b);
}
#else
static const auto                Sync_CrossThread     = llvm::SynchronizationScope::CrossThread;
static const auto                Attrib_FunctionIndex = llvm::AttributeSet::FunctionIndex;
static inline llvm::AttributeSet GetFuncAttribSet(llvm::LLVMContext&       ctx,
                                                  const llvm::AttrBuilder& b)
{
    return llvm::AttributeSet::get(ctx, Attrib_FunctionIndex, b);
}
#endif

#if LLVM_VERSION_MAJOR >= 12
static inline llvm::VectorType* getVectorType(llvm::Type *ElementType, unsigned NumElements)
{
    return llvm::VectorType::get(ElementType, NumElements, false);
}
#else
static inline llvm::VectorType* getVectorType(llvm::Type *ElementType, unsigned NumElements)
{
    return llvm::VectorType::get(ElementType, NumElements);
}
#endif

#if LLVM_VERSION_MAJOR < 7
#pragma pop_macro("DEBUG")
#endif

#if LLVM_VERSION_MAJOR > 10
    typedef unsigned            IntrinsicID;
    typedef llvm::Align         AlignType;
#else
    typedef llvm::Intrinsic::ID IntrinsicID;
    typedef unsigned            AlignType;
#endif

#include <deque>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <iostream>
#include <sstream>
#include <type_traits>
#include <cstdint>
#include <vector>
#include <tuple>
#include <mutex>

#include "common/os.h"

#if defined(_WIN32)
#define JIT_OBJ_EXT ".obj"
#else
#define JIT_OBJ_EXT ".o"
#endif // _WIN32
