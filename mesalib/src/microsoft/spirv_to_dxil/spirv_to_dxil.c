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

#include "git_sha1.h"
#include "vulkan/vulkan.h"

static void
shared_var_info(const struct glsl_type* type, unsigned* size, unsigned* align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size = glsl_type_is_boolean(type) ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length;
   *align = comp_size;
}

static nir_variable *
add_runtime_data_var(nir_shader *nir, unsigned desc_set, unsigned binding)
{
   unsigned runtime_data_size =
      nir->info.stage == MESA_SHADER_COMPUTE
         ? sizeof(struct dxil_spirv_compute_runtime_data)
         : sizeof(struct dxil_spirv_vertex_runtime_data);

   const struct glsl_type *array_type =
      glsl_array_type(glsl_uint_type(), runtime_data_size / sizeof(unsigned),
                      sizeof(unsigned));
   const struct glsl_struct_field field = {array_type, "arr"};
   nir_variable *var = nir_variable_create(
      nir, nir_var_mem_ubo,
      glsl_struct_type(&field, 1, "runtime_data", false), "runtime_data");
   var->data.descriptor_set = desc_set;
   // Check that desc_set fits on descriptor_set
   assert(var->data.descriptor_set == desc_set);
   var->data.binding = binding;
   var->data.how_declared = nir_var_hidden;
   return var;
}

struct lower_system_values_data {
   nir_address_format ubo_format;
   unsigned desc_set;
   unsigned binding;
};

static bool
lower_shader_system_values(struct nir_builder *builder, nir_instr *instr,
                           void *cb_data)
{
   if (instr->type != nir_instr_type_intrinsic) {
      return false;
   }

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   /* All the intrinsics we care about are loads */
   if (!nir_intrinsic_infos[intrin->intrinsic].has_dest)
      return false;

   assert(intrin->dest.is_ssa);

   int offset = 0;
   switch (intrin->intrinsic) {
   case nir_intrinsic_load_num_workgroups:
      offset =
         offsetof(struct dxil_spirv_compute_runtime_data, group_count_x);
      break;
   case nir_intrinsic_load_first_vertex:
      offset = offsetof(struct dxil_spirv_vertex_runtime_data, first_vertex);
      break;
   case nir_intrinsic_load_is_indexed_draw:
      offset =
         offsetof(struct dxil_spirv_vertex_runtime_data, is_indexed_draw);
      break;
   case nir_intrinsic_load_base_instance:
      offset = offsetof(struct dxil_spirv_vertex_runtime_data, base_instance);
      break;
   default:
      return false;
   }

   struct lower_system_values_data *data =
      (struct lower_system_values_data *)cb_data;

   builder->cursor = nir_after_instr(instr);
   nir_address_format ubo_format = data->ubo_format;

   nir_ssa_def *index = nir_vulkan_resource_index(
      builder, nir_address_format_num_components(ubo_format),
      nir_address_format_bit_size(ubo_format),
      nir_imm_int(builder, 0), 
      .desc_set = data->desc_set, .binding = data->binding,
      .desc_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

   nir_ssa_def *load_desc = nir_load_vulkan_descriptor(
      builder, nir_address_format_num_components(ubo_format),
      nir_address_format_bit_size(ubo_format),
      index, .desc_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

   nir_ssa_def *load_data = build_load_ubo_dxil(
      builder, nir_channel(builder, load_desc, 0),
      nir_imm_int(builder, offset),
      nir_dest_num_components(intrin->dest), nir_dest_bit_size(intrin->dest));

   nir_ssa_def_rewrite_uses(&intrin->dest.ssa, load_data);
   nir_instr_remove(instr);
   return true;
}

static bool
dxil_spirv_nir_lower_shader_system_values(nir_shader *shader,
                                          nir_address_format ubo_format,
                                          unsigned desc_set, unsigned binding)
{
   struct lower_system_values_data data = {
      .ubo_format = ubo_format,
      .desc_set = desc_set,
      .binding = binding,
   };
   return nir_shader_instructions_pass(shader, lower_shader_system_values,
                                       nir_metadata_block_index |
                                          nir_metadata_dominance |
                                          nir_metadata_loop_analysis,
                                       &data);
}

bool
spirv_to_dxil(const uint32_t *words, size_t word_count,
              struct dxil_spirv_specialization *specializations,
              unsigned int num_specializations, dxil_spirv_shader_stage stage,
              const char *entry_point_name,
              const struct dxil_spirv_runtime_conf *conf,
              struct dxil_spirv_object *out_dxil)
{
   if (stage == MESA_SHADER_NONE || stage == MESA_SHADER_KERNEL)
      return false;

   struct spirv_to_nir_options spirv_opts = {
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

   const struct nir_lower_sysvals_to_varyings_options sysvals_to_varyings = {
      .frag_coord = true,
      .point_coord = true,
   };
   NIR_PASS_V(nir, nir_lower_sysvals_to_varyings, &sysvals_to_varyings);

   NIR_PASS_V(nir, nir_lower_system_values);

   if (conf->zero_based_vertex_instance_id) {
      // vertex_id and instance_id should have already been transformed to
      // base zero before spirv_to_dxil was called. Therefore, we can zero out
      // base/firstVertex/Instance.
      gl_system_value system_values[] = {SYSTEM_VALUE_FIRST_VERTEX,
                                         SYSTEM_VALUE_BASE_VERTEX,
                                         SYSTEM_VALUE_BASE_INSTANCE};
      NIR_PASS_V(nir, dxil_nir_lower_system_values_to_zero, system_values,
                 ARRAY_SIZE(system_values));
   }

   bool requires_runtime_data = false;
   NIR_PASS(requires_runtime_data, nir,
            dxil_spirv_nir_lower_shader_system_values,
            spirv_opts.ubo_addr_format,
            conf->runtime_data_cbv.register_space,
            conf->runtime_data_cbv.base_shader_register);
   if (requires_runtime_data) {
      add_runtime_data_var(nir, conf->runtime_data_cbv.register_space,
                           conf->runtime_data_cbv.base_shader_register);
   }

   NIR_PASS_V(nir, nir_split_per_member_structs);

   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_ubo | nir_var_mem_ssbo,
              nir_address_format_32bit_index_offset);

   if (!nir->info.shared_memory_explicit_layout) {
      NIR_PASS_V(nir, nir_lower_vars_to_explicit_types, nir_var_mem_shared,
                 shared_var_info);
   }
   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_shared,
      nir_address_format_32bit_offset_as_64bit);

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
         if (nir_opt_trivial_continues(nir)) {
            progress = true;
            NIR_PASS(progress, nir, nir_copy_prop);
            NIR_PASS(progress, nir, nir_opt_dce);
         }
         NIR_PASS(progress, nir, nir_lower_vars_to_ssa);
         NIR_PASS(progress, nir, nir_opt_algebraic);
      } while (progress);
   }

   NIR_PASS_V(nir, nir_lower_readonly_images_to_tex, true);
   nir_lower_tex_options lower_tex_options = {0};
   NIR_PASS_V(nir, nir_lower_tex, &lower_tex_options);

   NIR_PASS_V(nir, dxil_nir_split_clip_cull_distance);
   NIR_PASS_V(nir, dxil_nir_lower_loads_stores_to_dxil);
   NIR_PASS_V(nir, dxil_nir_create_bare_samplers);
   NIR_PASS_V(nir, dxil_nir_lower_bool_input);

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   nir->info.inputs_read =
      dxil_reassign_driver_locations(nir, nir_var_shader_in, 0);

   if (stage != MESA_SHADER_FRAGMENT) {
      nir->info.outputs_written =
         dxil_reassign_driver_locations(nir, nir_var_shader_out, 0);
   } else {
      dxil_sort_ps_outputs(nir);
   }

   struct nir_to_dxil_options opts = {.vulkan_environment = true};

   struct blob dxil_blob;
   if (!nir_to_dxil(nir, &opts, &dxil_blob)) {
      if (dxil_blob.allocated)
         blob_finish(&dxil_blob);
      glsl_type_singleton_decref();
      return false;
   }

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
