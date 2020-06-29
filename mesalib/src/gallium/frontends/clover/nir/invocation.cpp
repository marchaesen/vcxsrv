//
// Copyright 2019 Karol Herbst
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

#include "invocation.hpp"

#include <tuple>

#include "core/device.hpp"
#include "core/error.hpp"
#include "pipe/p_state.h"
#include "util/algorithm.hpp"
#include "util/functional.hpp"

#include <compiler/glsl_types.h>
#include <compiler/nir/nir_serialize.h>
#include <compiler/spirv/nir_spirv.h>
#include <util/u_math.h>

using namespace clover;

#ifdef HAVE_CLOVER_SPIRV

// Refs and unrefs the glsl_type_singleton.
static class glsl_type_ref {
public:
   glsl_type_ref() {
      glsl_type_singleton_init_or_ref();
   }

   ~glsl_type_ref() {
      glsl_type_singleton_decref();
   }
} glsl_type_ref;

static const nir_shader_compiler_options *
dev_get_nir_compiler_options(const device &dev)
{
   const void *co = dev.get_compiler_options(PIPE_SHADER_IR_NIR);
   return static_cast<const nir_shader_compiler_options*>(co);
}

module clover::nir::spirv_to_nir(const module &mod, const device &dev,
                                 std::string &r_log)
{
   struct spirv_to_nir_options spirv_options = {};
   spirv_options.environment = NIR_SPIRV_OPENCL;
   spirv_options.caps.address = true;
   spirv_options.caps.float64 = true;
   spirv_options.caps.int8 = true;
   spirv_options.caps.int16 = true;
   spirv_options.caps.int64 = true;
   spirv_options.caps.kernel = true;
   spirv_options.constant_as_global = true;

   module m;
   // We only insert one section.
   assert(mod.secs.size() == 1);
   auto &section = mod.secs[0];

   module::resource_id section_id = 0;
   for (const auto &sym : mod.syms) {
      assert(sym.section == 0);

      const auto *binary =
         reinterpret_cast<const pipe_binary_program_header *>(section.data.data());
      const uint32_t *data = reinterpret_cast<const uint32_t *>(binary->blob);
      const size_t num_words = binary->num_bytes / 4;
      const char *name = sym.name.c_str();
      auto *compiler_options = dev_get_nir_compiler_options(dev);

      nir_shader *nir = spirv_to_nir(data, num_words, nullptr, 0,
                                     MESA_SHADER_KERNEL, name,
                                     &spirv_options, compiler_options);
      if (!nir) {
         r_log += "Translation from SPIR-V to NIR for kernel \"" + sym.name +
                  "\" failed.\n";
         throw build_error();
      }

      nir->info.cs.local_size_variable = true;
      nir_validate_shader(nir, "clover");

      // Calculate input offsets.
      unsigned offset = 0;
      nir_foreach_variable_safe(var, &nir->inputs) {
         offset = align(offset, glsl_get_cl_alignment(var->type));
         var->data.driver_location = offset;
         offset += glsl_get_cl_size(var->type);
      }

      // Inline all functions first.
      // according to the comment on nir_inline_functions
      NIR_PASS_V(nir, nir_lower_variable_initializers, nir_var_function_temp);
      NIR_PASS_V(nir, nir_lower_returns);
      NIR_PASS_V(nir, nir_inline_functions);
      NIR_PASS_V(nir, nir_opt_deref);

      // Pick off the single entrypoint that we want.
      foreach_list_typed_safe(nir_function, func, node, &nir->functions) {
         if (!func->is_entrypoint)
            exec_node_remove(&func->node);
      }
      assert(exec_list_length(&nir->functions) == 1);

      nir_validate_shader(nir, "clover after function inlining");

      NIR_PASS_V(nir, nir_lower_variable_initializers,
                 static_cast<nir_variable_mode>(~nir_var_function_temp));

      // copy propagate to prepare for lower_explicit_io
      NIR_PASS_V(nir, nir_split_var_copies);
      NIR_PASS_V(nir, nir_opt_copy_prop_vars);
      NIR_PASS_V(nir, nir_lower_var_copies);
      NIR_PASS_V(nir, nir_lower_vars_to_ssa);
      NIR_PASS_V(nir, nir_opt_dce);

      nir_variable_mode modes = (nir_variable_mode)(
         nir_var_shader_in |
         nir_var_mem_global |
         nir_var_mem_shared);
      nir_address_format format = nir->info.cs.ptr_size == 64 ?
         nir_address_format_64bit_global : nir_address_format_32bit_global;
      NIR_PASS_V(nir, nir_lower_explicit_io, modes, format);

      NIR_PASS_V(nir, nir_lower_system_values);
      if (compiler_options->lower_int64_options)
         NIR_PASS_V(nir, nir_lower_int64,
                    compiler_options->lower_int64_options);

      NIR_PASS_V(nir, nir_opt_dce);

      struct blob blob;
      blob_init(&blob);
      nir_serialize(&blob, nir, false);

      const pipe_binary_program_header header { uint32_t(blob.size) };
      module::section text { section_id, module::section::text_executable, header.num_bytes, {} };
      text.data.insert(text.data.end(), reinterpret_cast<const char *>(&header),
                       reinterpret_cast<const char *>(&header) + sizeof(header));
      text.data.insert(text.data.end(), blob.data, blob.data + blob.size);

      m.syms.emplace_back(sym.name, section_id, 0, sym.args);
      m.secs.push_back(text);
      section_id++;
   }
   return m;
}
#else
module clover::nir::spirv_to_nir(const module &mod, const device &dev, std::string &r_log)
{
   r_log += "SPIR-V support in clover is not enabled.\n";
   throw error(CL_LINKER_NOT_AVAILABLE);
}
#endif
