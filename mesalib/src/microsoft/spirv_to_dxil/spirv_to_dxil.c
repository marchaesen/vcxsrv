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

#include "dxil_spirv_nir.h"
#include "spirv_to_dxil.h"
#include "dxil_nir.h"
#include "nir_to_dxil.h"
#include "shader_enums.h"
#include "spirv/nir_spirv.h"
#include "util/blob.h"

#include "git_sha1.h"
#include "vulkan/vulkan.h"

static_assert(DXIL_SPIRV_SHADER_NONE == (int)MESA_SHADER_NONE, "must match");
static_assert(DXIL_SPIRV_SHADER_VERTEX == (int)MESA_SHADER_VERTEX, "must match");
static_assert(DXIL_SPIRV_SHADER_TESS_CTRL == (int)MESA_SHADER_TESS_CTRL, "must match");
static_assert(DXIL_SPIRV_SHADER_TESS_EVAL == (int)MESA_SHADER_TESS_EVAL, "must match");
static_assert(DXIL_SPIRV_SHADER_GEOMETRY == (int)MESA_SHADER_GEOMETRY, "must match");
static_assert(DXIL_SPIRV_SHADER_FRAGMENT == (int)MESA_SHADER_FRAGMENT, "must match");
static_assert(DXIL_SPIRV_SHADER_COMPUTE == (int)MESA_SHADER_COMPUTE, "must match");
static_assert(DXIL_SPIRV_SHADER_KERNEL == (int)MESA_SHADER_KERNEL, "must match");

/* Logic extracted from vk_spirv_to_nir() so we have the same preparation
 * steps for both the vulkan driver and the lib used by the WebGPU
 * implementation.
 * Maybe we should move those steps out of vk_spirv_to_nir() and make
 * them vk agnosting (right, the only vk specific thing is the vk_device
 * object that's used for the debug callback passed to spirv_to_nir()).
 */
static void
spirv_to_dxil_nir_prep(nir_shader *nir)
{
   /* We have to lower away local constant initializers right before we
    * inline functions.  That way they get properly initialized at the top
    * of the function and not at the top of its caller.
    */
   NIR_PASS_V(nir, nir_lower_variable_initializers, nir_var_function_temp);
   NIR_PASS_V(nir, nir_lower_returns);
   NIR_PASS_V(nir, nir_inline_functions);
   NIR_PASS_V(nir, nir_copy_prop);
   NIR_PASS_V(nir, nir_opt_deref);

   /* Pick off the single entrypoint that we want */
   foreach_list_typed_safe(nir_function, func, node, &nir->functions) {
      if (!func->is_entrypoint)
         exec_node_remove(&func->node);
   }
   assert(exec_list_length(&nir->functions) == 1);

   /* Now that we've deleted all but the main function, we can go ahead and
    * lower the rest of the constant initializers.  We do this here so that
    * nir_remove_dead_variables and split_per_member_structs below see the
    * corresponding stores.
    */
   NIR_PASS_V(nir, nir_lower_variable_initializers, ~0);

   /* Split member structs.  We do this before lower_io_to_temporaries so that
    * it doesn't lower system values to temporaries by accident.
    */
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_split_per_member_structs);

   NIR_PASS_V(nir, nir_remove_dead_variables,
              nir_var_shader_in | nir_var_shader_out | nir_var_system_value |
              nir_var_shader_call_data | nir_var_ray_hit_attrib,
              NULL);

   NIR_PASS_V(nir, nir_propagate_invariant, false);
}

bool
spirv_to_dxil(const uint32_t *words, size_t word_count,
              struct dxil_spirv_specialization *specializations,
              unsigned int num_specializations, dxil_spirv_shader_stage stage,
              const char *entry_point_name,
              const struct dxil_spirv_debug_options *dgb_opts,
              const struct dxil_spirv_runtime_conf *conf,
              const struct dxil_spirv_logger *logger,
              struct dxil_spirv_object *out_dxil)
{
   if (stage == DXIL_SPIRV_SHADER_NONE || stage == DXIL_SPIRV_SHADER_KERNEL)
      return false;

   struct spirv_to_nir_options spirv_opts = {
      .caps = {
         .draw_parameters = true,
      },
      .ubo_addr_format = nir_address_format_32bit_index_offset,
      .ssbo_addr_format = nir_address_format_32bit_index_offset,
      .shared_addr_format = nir_address_format_32bit_offset_as_64bit,

      // use_deref_buffer_array_length + nir_lower_explicit_io force
      //  get_ssbo_size to take in the return from load_vulkan_descriptor
      //  instead of vulkan_resource_index. This makes it much easier to
      //  get the DXIL handle for the SSBO.
      .use_deref_buffer_array_length = true
   };

   glsl_type_singleton_init_or_ref();

   struct nir_shader_compiler_options nir_options = *dxil_get_nir_compiler_options();
   // We will manually handle base_vertex when vertex_id and instance_id have
   // have been already converted to zero-base.
   nir_options.lower_base_vertex = !conf->zero_based_vertex_instance_id;

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

   spirv_to_dxil_nir_prep(nir);

   bool requires_runtime_data;
   dxil_spirv_nir_passes(nir, conf, &requires_runtime_data);

   if (dgb_opts->dump_nir)
      nir_print_shader(nir, stderr);

   struct nir_to_dxil_options opts = {
      .environment = DXIL_ENVIRONMENT_VULKAN,
      .shader_model_max = SHADER_MODEL_6_2,
      .validator_version_max = DXIL_VALIDATOR_1_4,
   };

   struct dxil_logger logger_inner = {.priv = logger->priv,
                                      .log = logger->log};

   struct blob dxil_blob;
   if (!nir_to_dxil(nir, &opts, &logger_inner, &dxil_blob)) {
      if (dxil_blob.allocated)
         blob_finish(&dxil_blob);
      ralloc_free(nir);
      glsl_type_singleton_decref();
      return false;
   }

   ralloc_free(nir);
   out_dxil->metadata.requires_runtime_data = requires_runtime_data;
   blob_finish_get_buffer(&dxil_blob, &out_dxil->binary.buffer,
                          &out_dxil->binary.size);

   glsl_type_singleton_decref();
   return true;
}

void
spirv_to_dxil_free(struct dxil_spirv_object *dxil)
{
   free(dxil->binary.buffer);
}

uint64_t
spirv_to_dxil_get_version()
{
   const char sha1[] = MESA_GIT_SHA1;
   const char* dash = strchr(sha1, '-');
   if (dash) {
      return strtoull(dash + 1, NULL, 16);
   }
   return 0;
}
