//
// Copyright 2016 Francisco Jerez
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
// OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
// ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
// OTHER DEALINGS IN THE SOFTWARE.
//

///
/// \file
/// Some thin wrappers around the Clang/LLVM API used to preserve
/// compatibility with older API versions while keeping the ifdef clutter low
/// in the rest of the clover::llvm subtree.  In case of an API break please
/// consider whether it's possible to preserve backwards compatibility by
/// introducing a new one-liner inline function or typedef here under the
/// compat namespace in order to keep the running code free from preprocessor
/// conditionals.
///

#ifndef CLOVER_LLVM_COMPAT_HPP
#define CLOVER_LLVM_COMPAT_HPP

#include "util/algorithm.hpp"

#include <llvm/Config/llvm-config.h>

#include <llvm/IR/LLVMContext.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Target/TargetMachine.h>

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Analysis/TargetLibraryInfo.h>

#include <clang/Basic/TargetInfo.h>
#include <clang/Frontend/CompilerInstance.h>

#if LLVM_VERSION_MAJOR >= 10
#include <llvm/Support/CodeGen.h>
#endif

namespace clover {
   namespace llvm {
      namespace compat {

#if LLVM_VERSION_MAJOR >= 10
         const auto CGFT_ObjectFile = ::llvm::CGFT_ObjectFile;
         const auto CGFT_AssemblyFile = ::llvm::CGFT_AssemblyFile;
         typedef ::llvm::CodeGenFileType CodeGenFileType;
#else
         const auto CGFT_ObjectFile = ::llvm::TargetMachine::CGFT_ObjectFile;
         const auto CGFT_AssemblyFile =
            ::llvm::TargetMachine::CGFT_AssemblyFile;
         typedef ::llvm::TargetMachine::CodeGenFileType CodeGenFileType;
#endif

#if LLVM_VERSION_MAJOR >= 10
         const clang::InputKind ik_opencl = clang::Language::OpenCL;
#else
         const clang::InputKind ik_opencl = clang::InputKind::OpenCL;
#endif

         template<typename T> inline bool
         create_compiler_invocation_from_args(clang::CompilerInvocation &cinv,
                                              T copts,
                                              clang::DiagnosticsEngine &diag)
         {
#if LLVM_VERSION_MAJOR >= 10
            return clang::CompilerInvocation::CreateFromArgs(
               cinv, copts, diag);
#else
            return clang::CompilerInvocation::CreateFromArgs(
               cinv, copts.data(), copts.data() + copts.size(), diag);
#endif
         }
      }
   }
}

#endif
