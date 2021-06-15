/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "spirv_to_dxil.h"
#include "nir_to_dxil.h"
#include "dxil_nir.h"
#include "shader_enums.h"
#include "spirv/nir_spirv.h"
#include "util/blob.h"

bool
spirv_to_dxil(const uint32_t *words, size_t word_count,
              struct dxil_spirv_specialization *specializations,
              unsigned int num_specializations, dxil_spirv_shader_stage stage,
              const char *entry_point_name, void **buffer, size_t *size)
{
   if (stage == MESA_SHADER_NONE || stage == MESA_SHADER_KERNEL)
      return false;

   struct spirv_to_nir_options spirv_opts = {0};
   spirv_opts.ubo_addr_format = nir_address_format_32bit_index_offset;
   spirv_opts.ssbo_addr_format = nir_address_format_32bit_index_offset;

   glsl_type_singleton_init_or_ref();

   struct nir_shader_compiler_options nir_options = *dxil_get_nir_compiler_options();
   // We will manually handle base_vertex
   nir_options.lower_base_vertex = false;

   nir_shader *nir = spirv_to_nir(
      words, word_count, (struct nir_spirv_specialization *)specializations,
      num_specializations, (gl_shader_stage)stage, entry_point_name,
      &spirv_opts, &nir_options);
   if (!nir) {
      glsl_type_singleton_decref();
      return false;
   }

   nir_validate_shader(nir,
                       "Validate before feeding NIR to the DXIL compiler");

   NIR_PASS_V(nir, nir_lower_system_values);

   // vertex_id and instance_id should have already been transformed to base
   //  zero before spirv_to_dxil was called. Also, WebGPU does not support
   //  base/firstVertex/Instance.
   gl_system_value system_values[] = {
      SYSTEM_VALUE_FIRST_VERTEX,
      SYSTEM_VALUE_BASE_VERTEX,
      SYSTEM_VALUE_BASE_INSTANCE
   };
   NIR_PASS_V(nir, dxil_nir_lower_system_values_to_zero, system_values, ARRAY_SIZE(system_values));

   NIR_PASS_V(nir, nir_split_per_member_structs);

   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_ubo | nir_var_mem_ssbo,
              nir_address_format_32bit_index_offset);

   nir_variable_mode nir_var_function_temp =
      nir_var_shader_in | nir_var_shader_out;
   NIR_PASS_V(nir, nir_lower_variable_initializers,
              nir_var_function_temp);
   NIR_PASS_V(nir, nir_opt_deref);
   NIR_PASS_V(nir, nir_lower_returns);
   NIR_PASS_V(nir, nir_inline_functions);
   NIR_PASS_V(nir, nir_lower_variable_initializers,
              ~nir_var_function_temp);

   // Pick off the single entrypoint that we want.
   nir_function *entrypoint;
   foreach_list_typed_safe(nir_function, func, node, &nir->functions) {
      if (func->is_entrypoint)
         entrypoint = func;
      else
         exec_node_remove(&func->node);
   }
   assert(exec_list_length(&nir->functions) == 1);

   NIR_PASS_V(nir, nir_lower_clip_cull_distance_arrays);
   NIR_PASS_V(nir, nir_lower_io_to_temporaries, entrypoint->impl, true, true);
   NIR_PASS_V(nir, nir_lower_global_vars_to_local);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_var_copies);
   NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, false);

   NIR_PASS_V(nir, nir_lower_alu_to_scalar, NULL, NULL);
   NIR_PASS_V(nir, nir_opt_dce);
   NIR_PASS_V(nir, dxil_nir_lower_double_math);

   {
      bool progress;
      do
      {
         progress = false;
         NIR_PASS(progress, nir, nir_copy_prop);
         NIR_PASS(progress, nir, nir_opt_copy_prop_vars);
         NIR_PASS(progress, nir, nir_opt_deref);
         NIR_PASS(progress, nir, nir_opt_dce);
         NIR_PASS(progress, nir, nir_opt_undef);
         NIR_PASS(progress, nir, nir_opt_constant_folding);
         NIR_PASS(progress, nir, nir_opt_cse);
         NIR_PASS(progress, nir, nir_lower_vars_to_ssa);
         NIR_PASS(progress, nir, nir_opt_algebraic);
      } while (progress);
   }

   NIR_PASS_V(nir, nir_lower_readonly_images_to_tex, true);
   NIR_PASS_V(nir, dxil_nir_split_clip_cull_distance);
   NIR_PASS_V(nir, dxil_nir_lower_loads_stores_to_dxil);
   NIR_PASS_V(nir, dxil_nir_create_bare_samplers);

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   struct nir_to_dxil_options opts = {.vulkan_environment = true};

   struct blob dxil_blob;
   if (!nir_to_dxil(nir, &opts, &dxil_blob)) {
      if (dxil_blob.allocated)
         blob_finish(&dxil_blob);
      glsl_type_singleton_decref();
      return false;
   }

   blob_finish_get_buffer(&dxil_blob, buffer, size);

   glsl_type_singleton_decref();
   return true;
}

void
spirv_to_dxil_free(void* buffer)
{
   free(buffer);
}
