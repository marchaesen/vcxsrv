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
/// Utility functions for LLVM IR metadata introspection.
///

#ifndef CLOVER_LLVM_METADATA_HPP
#define CLOVER_LLVM_METADATA_HPP

#include "llvm/compat.hpp"
#include "util/algorithm.hpp"

#include <vector>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Metadata.h>

namespace clover {
   namespace llvm {
      namespace detail {
         inline bool
         is_kernel(const ::llvm::Function &f) {
            return f.getMetadata("kernel_arg_type");
         }

         inline iterator_range< ::llvm::MDNode::op_iterator>
         get_kernel_metadata_operands(const ::llvm::Function &f,
                                      const std::string &name) {
            const auto data_node = f.getMetadata(name);
            return range(data_node->op_begin(), data_node->op_end());
         }
      }

      ///
      /// Extract the string metadata node \p name corresponding to the kernel
      /// argument given by \p arg.
      ///
      inline std::string
      get_argument_metadata(const ::llvm::Function &f,
                            const ::llvm::Argument &arg,
                            const std::string &name) {
         return ::llvm::cast< ::llvm::MDString>(
               detail::get_kernel_metadata_operands(f, name)[arg.getArgNo()])
            ->getString().str();
      }

      ///
      /// Return a vector with all CL kernel functions found in the LLVM
      /// module \p mod.
      ///
      inline std::vector<const ::llvm::Function *>
      get_kernels(const ::llvm::Module &mod) {
         std::vector<const ::llvm::Function *> fs;

         for (auto &f : mod.getFunctionList()) {
            if (detail::is_kernel(f))
               fs.push_back(&f);
         }

         return fs;
      }
   }
}

#endif
