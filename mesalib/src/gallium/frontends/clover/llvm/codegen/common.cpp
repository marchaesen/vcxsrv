//
// Copyright 2012-2016 Francisco Jerez
// Copyright 2012-2016 Advanced Micro Devices, Inc.
// Copyright 2015 Zoltan Gilian
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
/// Codegen back-end-independent part of the construction of an executable
/// clover::module, including kernel argument metadata extraction and
/// formatting of the pre-generated binary code in a form that can be
/// understood by pipe drivers.
///

#include "llvm/codegen.hpp"
#include "llvm/metadata.hpp"

#include "CL/cl.h"

#include "pipe/p_state.h"
#include "util/u_math.h"

#include <clang/Basic/TargetInfo.h>

using namespace clover;
using namespace clover::llvm;

using ::llvm::Module;
using ::llvm::Function;
using ::llvm::Type;
using ::llvm::isa;
using ::llvm::cast;
using ::llvm::dyn_cast;

namespace {
   enum module::argument::type
   get_image_type(const std::string &type,
                  const std::string &qual) {
      if (type == "image2d_t" && qual == "read_only")
         return module::argument::image2d_rd;
      else if (type == "image2d_t" && qual == "write_only")
         return module::argument::image2d_wr;
      else if (type == "image3d_t" && qual == "read_only")
         return module::argument::image3d_rd;
      else if (type == "image3d_t" && qual == "write_only")
         return module::argument::image3d_wr;
      else
         unreachable("Unknown image type");
   }

   std::vector<module::argument>
   make_kernel_args(const Module &mod, const std::string &kernel_name,
                    const clang::CompilerInstance &c) {
      std::vector<module::argument> args;
      const Function &f = *mod.getFunction(kernel_name);
      ::llvm::DataLayout dl(&mod);
      const auto size_type =
         dl.getSmallestLegalIntType(mod.getContext(), sizeof(cl_uint) * 8);

      for (const auto &arg : f.args()) {
         const auto arg_type = arg.getType();

         // OpenCL 1.2 specification, Ch. 6.1.5: "A built-in data
         // type that is not a power of two bytes in size must be
         // aligned to the next larger power of two.
         // This rule applies to built-in types only, not structs or unions."
         const unsigned arg_store_size = dl.getTypeStoreSize(arg_type);
         const unsigned arg_api_size = dl.getTypeAllocSize(arg_type);

         const auto target_type = compat::get_abi_type(arg_type, mod);
         const unsigned target_size = dl.getTypeStoreSize(target_type);
         const unsigned target_align = dl.getABITypeAlignment(target_type);

         const auto type_name = get_argument_metadata(f, arg,
                                                      "kernel_arg_type");

         if (type_name == "image2d_t" || type_name == "image3d_t") {
            // Image.
            const auto access_qual = get_argument_metadata(
               f, arg, "kernel_arg_access_qual");
            args.emplace_back(get_image_type(type_name, access_qual),
                              arg_store_size, target_size,
                              target_align, module::argument::zero_ext);

         } else if (type_name == "__llvm_image_size") {
            // Image size implicit argument.
            args.emplace_back(module::argument::scalar, sizeof(cl_uint),
                              dl.getTypeStoreSize(size_type),
                              dl.getABITypeAlignment(size_type),
                              module::argument::zero_ext,
                              module::argument::image_size);

         } else if (type_name == "__llvm_image_format") {
            // Image format implicit argument.
            args.emplace_back(module::argument::scalar, sizeof(cl_uint),
                              dl.getTypeStoreSize(size_type),
                              dl.getABITypeAlignment(size_type),
                              module::argument::zero_ext,
                              module::argument::image_format);

         } else {
            // Other types.
            const auto actual_type =
               isa< ::llvm::PointerType>(arg_type) && arg.hasByValAttr() ?
               cast< ::llvm::PointerType>(arg_type)->getElementType() : arg_type;

            if (actual_type->isPointerTy()) {
               const unsigned address_space =
                  cast< ::llvm::PointerType>(actual_type)->getAddressSpace();

               if (address_space == compat::target_address_space(
                                  c.getTarget(), clang::LangAS::opencl_local)) {
                  args.emplace_back(module::argument::local, arg_api_size,
                                    target_size, target_align,
                                    module::argument::zero_ext);
               } else {
                  // XXX: Correctly handle constant address space.  There is no
                  // way for r600g to pass a handle for constant buffers back
                  // to clover like it can for global buffers, so
                  // creating constant arguments will break r600g.  For now,
                  // continue treating constant buffers as global buffers
                  // until we can come up with a way to create handles for
                  // constant buffers.
                  args.emplace_back(module::argument::global, arg_api_size,
                                    target_size, target_align,
                                    module::argument::zero_ext);
               }

            } else {
               const bool needs_sign_ext = f.getAttributes().hasAttribute(
                  arg.getArgNo() + 1, ::llvm::Attribute::SExt);

               args.emplace_back(module::argument::scalar, arg_api_size,
                                 target_size, target_align,
                                 (needs_sign_ext ? module::argument::sign_ext :
                                  module::argument::zero_ext));
            }
         }
      }

      // Append implicit arguments.  XXX - The types, ordering and
      // vector size of the implicit arguments should depend on the
      // target according to the selected calling convention.
      args.emplace_back(module::argument::scalar, sizeof(cl_uint),
                        dl.getTypeStoreSize(size_type),
                        dl.getABITypeAlignment(size_type),
                        module::argument::zero_ext,
                        module::argument::grid_dimension);

      args.emplace_back(module::argument::scalar, sizeof(cl_uint),
                        dl.getTypeStoreSize(size_type),
                        dl.getABITypeAlignment(size_type),
                        module::argument::zero_ext,
                        module::argument::grid_offset);

      return args;
   }

   module::section
   make_text_section(const std::vector<char> &code) {
      const pipe_binary_program_header header { uint32_t(code.size()) };
      module::section text { 0, module::section::text_executable,
                             header.num_bytes, {} };

      text.data.insert(text.data.end(), reinterpret_cast<const char *>(&header),
                       reinterpret_cast<const char *>(&header) + sizeof(header));
      text.data.insert(text.data.end(), code.begin(), code.end());

      return text;
   }
}

module
clover::llvm::build_module_common(const Module &mod,
                                  const std::vector<char> &code,
                                  const std::map<std::string,
                                                 unsigned> &offsets,
                                  const clang::CompilerInstance &c) {
   module m;

   for (const auto &llvm_name : map(std::mem_fn(&Function::getName),
                               get_kernels(mod))) {
      const ::std::string name(llvm_name);
      if (offsets.count(name))
         m.syms.emplace_back(name, 0, offsets.at(name),
                             make_kernel_args(mod, name, c));
   }

   m.secs.push_back(make_text_section(code));
   return m;
}
