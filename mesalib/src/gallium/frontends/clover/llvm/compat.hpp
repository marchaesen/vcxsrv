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
#if LLVM_VERSION_MAJOR < 4
#include <llvm/Bitcode/ReaderWriter.h>
#else
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#endif

#include <llvm/IR/LLVMContext.h>
#include <llvm/Linker/Linker.h>
#include <llvm/Transforms/IPO.h>
#include <llvm/Transforms/Utils/Cloning.h>
#include <llvm/Target/TargetMachine.h>
#if LLVM_VERSION_MAJOR >= 4
#include <llvm/Support/Error.h>
#else
#include <llvm/Support/ErrorOr.h>
#endif

#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Analysis/TargetLibraryInfo.h>

#include <clang/Basic/TargetInfo.h>
#include <clang/Frontend/CompilerInstance.h>

#if LLVM_VERSION_MAJOR >= 8
#include <clang/Basic/CodeGenOptions.h>
#else
#include <clang/Frontend/CodeGenOptions.h>
#endif

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

         template<typename T, typename AS>
         unsigned target_address_space(const T &target, const AS lang_as) {
            const auto &map = target.getAddressSpaceMap();
#if LLVM_VERSION_MAJOR >= 5
            return map[static_cast<unsigned>(lang_as)];
#else
            return map[lang_as - clang::LangAS::Offset];
#endif
         }

#if LLVM_VERSION_MAJOR >= 10
         const clang::InputKind ik_opencl = clang::Language::OpenCL;
#elif LLVM_VERSION_MAJOR >= 5
         const clang::InputKind ik_opencl = clang::InputKind::OpenCL;
#else
         const clang::InputKind ik_opencl = clang::IK_OpenCL;
#endif

#if LLVM_VERSION_MAJOR >= 5
         const clang::LangStandard::Kind lang_opencl10 = clang::LangStandard::lang_opencl10;
#else
         const clang::LangStandard::Kind lang_opencl10 = clang::LangStandard::lang_opencl;
#endif

         inline void
         add_link_bitcode_file(clang::CodeGenOptions &opts,
                               const std::string &path) {
#if LLVM_VERSION_MAJOR >= 5
            clang::CodeGenOptions::BitcodeFileToLink F;

            F.Filename = path;
            F.PropagateAttrs = true;
            F.LinkFlags = ::llvm::Linker::Flags::None;
            opts.LinkBitcodeFiles.emplace_back(F);
#else
            opts.LinkBitcodeFiles.emplace_back(::llvm::Linker::Flags::None, path);
#endif
         }

#if LLVM_VERSION_MAJOR >= 6
         const auto default_code_model = ::llvm::None;
#else
         const auto default_code_model = ::llvm::CodeModel::Default;
#endif

         template<typename M, typename F> void
         handle_module_error(M &mod, const F &f) {
#if LLVM_VERSION_MAJOR >= 4
            if (::llvm::Error err = mod.takeError())
               ::llvm::handleAllErrors(std::move(err), [&](::llvm::ErrorInfoBase &eib) {
                     f(eib.message());
                  });
#else
            if (!mod)
               f(mod.getError().message());
#endif
         }

         template<typename T> void
         set_diagnostic_handler(::llvm::LLVMContext &ctx,
                                T *diagnostic_handler, void *data) {
#if LLVM_VERSION_MAJOR >= 6
            ctx.setDiagnosticHandlerCallBack(diagnostic_handler, data);
#else
            ctx.setDiagnosticHandler(diagnostic_handler, data);
#endif
         }

         inline std::unique_ptr< ::llvm::Module>
         clone_module(const ::llvm::Module &mod)
         {
#if LLVM_VERSION_MAJOR >= 7
            return ::llvm::CloneModule(mod);
#else
            return ::llvm::CloneModule(&mod);
#endif
         }

         template<typename T> void
         write_bitcode_to_file(const ::llvm::Module &mod, T &os)
         {
#if LLVM_VERSION_MAJOR >= 7
            ::llvm::WriteBitcodeToFile(mod, os);
#else
            ::llvm::WriteBitcodeToFile(&mod, os);
#endif
         }

         template<typename TM, typename PM, typename OS, typename FT>
         bool add_passes_to_emit_file(TM &tm, PM &pm, OS &os, FT &ft)
         {
#if LLVM_VERSION_MAJOR >= 7
            return tm.addPassesToEmitFile(pm, os, nullptr, ft);
#else
            return tm.addPassesToEmitFile(pm, os, ft);
#endif
         }

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

         template<typename T, typename M>
         T get_abi_type(const T &arg_type, const M &mod) {
#if LLVM_VERSION_MAJOR >= 7
            return arg_type;
#else
            ::llvm::DataLayout dl(&mod);
            const unsigned arg_store_size = dl.getTypeStoreSize(arg_type);
            return !arg_type->isIntegerTy() ? arg_type :
               dl.getSmallestLegalIntType(mod.getContext(), arg_store_size * 8);
#endif
         }
      }
   }
}

#endif
