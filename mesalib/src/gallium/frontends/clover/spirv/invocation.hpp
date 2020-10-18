//
// Copyright 2018 Pierre Moreau
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

#ifndef CLOVER_SPIRV_INVOCATION_HPP
#define CLOVER_SPIRV_INVOCATION_HPP

#include <unordered_set>

#include "core/context.hpp"
#include "core/module.hpp"
#include "core/program.hpp"

namespace clover {
   namespace spirv {
      // Returns whether the given binary is considered valid for the given
      // OpenCL version.
      //
      // It uses SPIRV-Tools validator to do the validation, and potential
      // warnings and errors are appended to |r_log|.
      bool is_valid_spirv(const std::vector<char> &binary,
                          const std::string &opencl_version,
                          std::string &r_log);

      // Creates a clover module out of the given SPIR-V binary.
      module compile_program(const std::vector<char> &binary,
                             const device &dev, std::string &r_log,
                             bool validate = true);

      // Combines multiple clover modules into a single one, resolving
      // link dependencies between them.
      module link_program(const std::vector<module> &modules, const device &dev,
                          const std::string &opts, std::string &r_log);

      // Returns a textual representation of the given binary.
      std::string print_module(const std::vector<char> &binary,
                               const std::string &opencl_version);

      // Returns a set of supported SPIR-V extensions.
      std::unordered_set<std::string> supported_extensions();

      // Returns a vector (sorted in increasing order) of supported SPIR-V
      // versions.
      std::vector<uint32_t> supported_versions();
   }
}

#endif
