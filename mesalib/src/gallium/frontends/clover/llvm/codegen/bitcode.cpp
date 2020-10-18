//
// Copyright 2012-2016 Francisco Jerez
// Copyright 2012-2016 Advanced Micro Devices, Inc.
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
/// Trivial codegen back-end that simply passes through the existing LLVM IR
/// and either formats it so it can be consumed by pipe drivers (if
/// build_module_bitcode() is used) or serializes so it can be deserialized at
/// a later point and passed to the actual codegen back-end (if
/// build_module_library() / parse_module_library() is used), potentially
/// after linking against other bitcode object files.
///

#include "llvm/codegen.hpp"
#include "llvm/compat.hpp"
#include "llvm/metadata.hpp"
#include "core/error.hpp"
#include "util/algorithm.hpp"

#include <map>
#include <llvm/Config/llvm-config.h>
#if LLVM_VERSION_MAJOR < 4
#include <llvm/Bitcode/ReaderWriter.h>
#else
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Bitcode/BitcodeWriter.h>
#endif
#include <llvm/Support/raw_ostream.h>

using namespace clover;
using namespace clover::llvm;

namespace {
   std::vector<char>
   emit_code(const ::llvm::Module &mod) {
      ::llvm::SmallVector<char, 1024> data;
      ::llvm::raw_svector_ostream os { data };
      ::llvm::WriteBitcodeToFile(mod, os);
      return { os.str().begin(), os.str().end() };
   }
}

std::string
clover::llvm::print_module_bitcode(const ::llvm::Module &mod) {
   std::string s;
   ::llvm::raw_string_ostream os { s };
   mod.print(os, NULL);
   return os.str();
}

module
clover::llvm::build_module_library(const ::llvm::Module &mod,
                                   enum module::section::type section_type) {
   module m;
   const auto code = emit_code(mod);
   m.secs.emplace_back(0, section_type, code.size(), code);
   return m;
}

std::unique_ptr< ::llvm::Module>
clover::llvm::parse_module_library(const module &m, ::llvm::LLVMContext &ctx,
                                   std::string &r_log) {
   auto mod = ::llvm::parseBitcodeFile(::llvm::MemoryBufferRef(
                                        as_string(m.secs[0].data), " "), ctx);

   if (::llvm::Error err = mod.takeError()) {
      ::llvm::handleAllErrors(std::move(err), [&](::llvm::ErrorInfoBase &eib) {
         fail(r_log, error(CL_INVALID_PROGRAM), eib.message());
      });
   }

   return std::unique_ptr< ::llvm::Module>(std::move(*mod));
}
