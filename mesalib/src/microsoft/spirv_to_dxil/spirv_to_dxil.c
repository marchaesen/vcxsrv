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

static nir_variable *
add_push_constant_var(nir_shader *nir, unsigned size, unsigned desc_set, unsigned binding)
{
   /* Size must be a multiple of 16 as buffer load is loading 16 bytes at a time */
   size = ALIGN_POT(size, 16) / 16;

   const struct glsl_type *array_type = glsl_array_type(glsl_uint_type(), size, 4);
   const struct glsl_struct_field field = {array_type, "arr"};
   nir_variable *var = nir_variable_create(
      nir, nir_var_mem_ubo,
      glsl_struct_type(&field, 1, "block", false), "push_constants");
   var->data.descriptor_set = desc_set;
   var->data.binding = binding;
   var->data.how_declared = nir_var_hidden;
   return var;
}

struct lower_load_push_constant_data {
   nir_address_format ubo_format;
   unsigned desc_set;
   unsigned binding;
   uint32_t min;
   uint32_t max;
};

static bool
lower_load_push_constant(struct nir_builder *builder, nir_instr *instr,
                           void *cb_data)
{
   struct lower_load_push_constant_data *data =
      (struct lower_load_push_constant_data *)cb_data;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   /* All the intrinsics we care about are loads */
   if (intrin->intrinsic != nir_intrinsic_load_push_constant)
      return false;

   uint32_t base = nir_intrinsic_base(intrin);
   uint32_t range = nir_intrinsic_range(intrin);
   data->min = MIN2(data->min, base);
   data->max = MAX2(data->max, base + range);

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

   nir_ssa_def *offset = nir_ssa_for_src(builder, intrin->src[0], 1);
   nir_ssa_def *load_data = build_load_ubo_dxil(
      builder, nir_channel(builder, load_desc, 0),
      nir_iadd_imm(builder, offset, base),
      nir_dest_num_components(intrin->dest), nir_dest_bit_size(intrin->dest));

   nir_ssa_def_rewrite_uses(&intrin->dest.ssa, load_data);
   nir_instr_remove(instr);
   return true;
}

static bool
dxil_spirv_nir_lower_load_push_constant(nir_shader *shader,
                                        nir_address_format ubo_format,
                                        unsigned desc_set, unsigned binding,
                                        uint32_t *size)
{
   bool ret;
   struct lower_load_push_constant_data data = {
      .ubo_format = ubo_format,
      .desc_set = desc_set,
      .binding = binding,
      .min = UINT32_MAX,
      .max = 0,
   };
   ret = nir_shader_instructions_pass(shader, lower_load_push_constant,
                                      nir_metadata_block_index |
                                         nir_metadata_dominance |
                                         nir_metadata_loop_analysis,
                                      &data);

   if (data.min >= data.max)
      *size = 0;
   else
      *size = (data.max - data.min);

   assert(ret == (*size > 0));

   return ret;
}

struct lower_yz_flip_data {
   bool *reads_sysval_ubo;
   const struct dxil_spirv_runtime_conf *rt_conf;
};

static bool
lower_yz_flip(struct nir_builder *builder, nir_instr *instr,
              void *cb_data)
{
   struct lower_yz_flip_data *data =
      (struct lower_yz_flip_data *)cb_data;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   if (intrin->intrinsic != nir_intrinsic_store_deref)
      return false;

   nir_variable *var = nir_intrinsic_get_var(intrin, 0);
   if (var->data.mode != nir_var_shader_out ||
       var->data.location != VARYING_SLOT_POS)
      return false;

   builder->cursor = nir_before_instr(instr);

   const struct dxil_spirv_runtime_conf *rt_conf = data->rt_conf;

   nir_ssa_def *pos = nir_ssa_for_src(builder, intrin->src[1], 4);
   nir_ssa_def *y_pos = nir_channel(builder, pos, 1);
   nir_ssa_def *z_pos = nir_channel(builder, pos, 2);
   nir_ssa_def *y_flip_mask = NULL, *z_flip_mask = NULL, *dyn_yz_flip_mask;

   if (rt_conf->yz_flip.mode & DXIL_SPIRV_YZ_FLIP_CONDITIONAL) {
      // conditional YZ-flip. The flip bitmask is passed through the vertex
      // runtime data UBO.
      unsigned offset =
         offsetof(struct dxil_spirv_vertex_runtime_data, yz_flip_mask);
      nir_address_format ubo_format = nir_address_format_32bit_index_offset;

      nir_ssa_def *index = nir_vulkan_resource_index(
         builder, nir_address_format_num_components(ubo_format),
         nir_address_format_bit_size(ubo_format),
         nir_imm_int(builder, 0),
         .desc_set = rt_conf->runtime_data_cbv.register_space,
         .binding = rt_conf->runtime_data_cbv.base_shader_register,
         .desc_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

      nir_ssa_def *load_desc = nir_load_vulkan_descriptor(
         builder, nir_address_format_num_components(ubo_format),
         nir_address_format_bit_size(ubo_format),
         index, .desc_type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);

      dyn_yz_flip_mask =
         build_load_ubo_dxil(builder,
                             nir_channel(builder, load_desc, 0),
                             nir_imm_int(builder, offset), 1, 32);
      *data->reads_sysval_ubo = true;
   }

   if (rt_conf->yz_flip.mode & DXIL_SPIRV_Y_FLIP_UNCONDITIONAL)
      y_flip_mask = nir_imm_int(builder, rt_conf->yz_flip.y_mask);
   else if (rt_conf->yz_flip.mode & DXIL_SPIRV_Y_FLIP_CONDITIONAL)
      y_flip_mask = nir_iand_imm(builder, dyn_yz_flip_mask, DXIL_SPIRV_Y_FLIP_MASK);

   if (rt_conf->yz_flip.mode & DXIL_SPIRV_Z_FLIP_UNCONDITIONAL)
      z_flip_mask = nir_imm_int(builder, rt_conf->yz_flip.z_mask);
   else if (rt_conf->yz_flip.mode & DXIL_SPIRV_Z_FLIP_CONDITIONAL)
      z_flip_mask = nir_ushr_imm(builder, dyn_yz_flip_mask, DXIL_SPIRV_Z_FLIP_SHIFT);

   /* TODO: Multi-viewport */

   if (y_flip_mask) {
      nir_ssa_def *flip = nir_ieq_imm(builder, nir_iand_imm(builder, y_flip_mask, 1), 1);

      // Z-flip => pos.y = -pos.y
      y_pos = nir_bcsel(builder, flip, nir_fneg(builder, y_pos), y_pos);
   }

   if (z_flip_mask) {
      nir_ssa_def *flip = nir_ieq_imm(builder, nir_iand_imm(builder, z_flip_mask, 1), 1);

      // Z-flip => pos.z = -pos.z + 1.0f
      z_pos = nir_bcsel(builder, flip,
                        nir_fadd_imm(builder, nir_fneg(builder, z_pos), 1.0f),
                        z_pos);
   }

   nir_ssa_def *def = nir_vec4(builder,
                               nir_channel(builder, pos, 0),
                               y_pos,
                               z_pos,
                               nir_channel(builder, pos, 3));
   nir_instr_rewrite_src(&intrin->instr, &intrin->src[1], nir_src_for_ssa(def));
   return true;
}

static bool
dxil_spirv_nir_lower_yz_flip(nir_shader *shader,
                             const struct dxil_spirv_runtime_conf *rt_conf,
                             bool *reads_sysval_ubo)
{
   struct lower_yz_flip_data data = {
      .rt_conf = rt_conf,
      .reads_sysval_ubo = reads_sysval_ubo,
   };

   return nir_shader_instructions_pass(shader, lower_yz_flip,
                                       nir_metadata_block_index |
                                       nir_metadata_dominance |
                                       nir_metadata_loop_analysis,
                                       &data);
}

static bool
adjust_resource_index_binding(struct nir_builder *builder, nir_instr *instr,
                              void *cb_data)
{
   struct dxil_spirv_runtime_conf *conf =
      (struct dxil_spirv_runtime_conf *)cb_data;

   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   if (intrin->intrinsic != nir_intrinsic_vulkan_resource_index)
      return false;

   unsigned set = nir_intrinsic_desc_set(intrin);
   unsigned binding = nir_intrinsic_binding(intrin);

   if (set >= conf->descriptor_set_count)
      return false;

   binding = conf->descriptor_sets[set].bindings[binding].base_register;
   nir_intrinsic_set_binding(intrin, binding);

   return true;
}

static bool
dxil_spirv_nir_adjust_var_bindings(nir_shader *shader,
                                   const struct dxil_spirv_runtime_conf *conf)
{
   uint32_t modes = nir_var_image | nir_var_uniform | nir_var_mem_ubo | nir_var_mem_ssbo;

   nir_foreach_variable_with_modes(var, shader, modes) {
      if (var->data.mode == nir_var_uniform) {
         const struct glsl_type *type = glsl_without_array(var->type);

         if (!glsl_type_is_sampler(type) && !glsl_type_is_texture(type))
            continue;
      }

      unsigned s = var->data.descriptor_set, b = var->data.binding;
      var->data.binding = conf->descriptor_sets[s].bindings[b].base_register;
   }

   return nir_shader_instructions_pass(shader, adjust_resource_index_binding,
                                       nir_metadata_all, (void *)conf);
}

static bool
discard_psiz_access(struct nir_builder *builder, nir_instr *instr,
                    void *cb_data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

   if (intrin->intrinsic != nir_intrinsic_store_deref &&
       intrin->intrinsic != nir_intrinsic_load_deref)
      return false;

   nir_variable *var = nir_intrinsic_get_var(intrin, 0);
   if (!var || var->data.mode != nir_var_shader_out ||
       var->data.location != VARYING_SLOT_PSIZ)
      return false;

   builder->cursor = nir_before_instr(instr);

   if (intrin->intrinsic == nir_intrinsic_load_deref)
      nir_ssa_def_rewrite_uses(&intrin->dest.ssa, nir_imm_float(builder, 1.0));

   nir_instr_remove(instr);
   return true;
}

static bool
dxil_spirv_nir_discard_point_size_var(nir_shader *shader)
{
   if (shader->info.stage != MESA_SHADER_VERTEX &&
       shader->info.stage != MESA_SHADER_TESS_EVAL &&
       shader->info.stage != MESA_SHADER_GEOMETRY)
      return false;

   nir_variable *psiz = NULL;
   nir_foreach_shader_out_variable(var, shader) {
      if (var->data.location == VARYING_SLOT_PSIZ) {
         psiz = var;
         break;
      }
   }

   if (!psiz)
      return false;

   if (!nir_shader_instructions_pass(shader, discard_psiz_access,
                                     nir_metadata_block_index |
                                     nir_metadata_dominance |
                                     nir_metadata_loop_analysis,
                                     NULL))
      return false;

   nir_remove_dead_derefs(shader);
   return true;
}

static bool
fix_sample_mask_type(struct nir_builder *builder, nir_instr *instr,
                     void *cb_data)
{
   struct dxil_spirv_runtime_conf *conf =
      (struct dxil_spirv_runtime_conf *)cb_data;

   if (instr->type != nir_instr_type_deref)
      return false;

   nir_deref_instr *deref = nir_instr_as_deref(instr);
   nir_variable *var =
      deref->deref_type == nir_deref_type_var ? deref->var : NULL;

   if (!var || var->data.mode != nir_var_shader_out ||
       var->data.location != FRAG_RESULT_SAMPLE_MASK ||
       deref->type == glsl_uint_type())
      return false;

   assert(glsl_without_array(deref->type) == glsl_int_type());
   deref->type = glsl_uint_type();
   return true;
}

static bool
dxil_spirv_nir_fix_sample_mask_type(nir_shader *shader)
{
   nir_foreach_variable_with_modes(var, shader, nir_var_shader_out) {
      if (var->data.location == FRAG_RESULT_SAMPLE_MASK &&
          var->type != glsl_uint_type()) {
         var->type = glsl_uint_type();
      }
   }

   return nir_shader_instructions_pass(shader, fix_sample_mask_type,
                                       nir_metadata_all, NULL);
}


bool
spirv_to_dxil(const uint32_t *words, size_t word_count,
              struct dxil_spirv_specialization *specializations,
              unsigned int num_specializations, dxil_spirv_shader_stage stage,
              const char *entry_point_name,
              const struct dxil_spirv_debug_options *dgb_opts,
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

   if (conf->descriptor_set_count > 0)
      NIR_PASS_V(nir, dxil_spirv_nir_adjust_var_bindings, conf);

   bool requires_runtime_data = false;
   NIR_PASS(requires_runtime_data, nir,
            dxil_spirv_nir_lower_shader_system_values,
            spirv_opts.ubo_addr_format,
            conf->runtime_data_cbv.register_space,
            conf->runtime_data_cbv.base_shader_register);

   if (stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS_V(nir, nir_lower_input_attachments,
                 &(nir_input_attachment_options){
                     .use_fragcoord_sysval = false,
                     .use_layer_id_sysval = true,
                 });
   }

   NIR_PASS_V(nir, nir_opt_deref);

   if (conf->read_only_images_as_srvs) {
      const nir_opt_access_options opt_access_options = {
         .is_vulkan = true,
         .infer_non_readable = true,
      };
      NIR_PASS_V(nir, nir_opt_access, &opt_access_options);
   }

   NIR_PASS_V(nir, nir_split_per_member_structs);

   NIR_PASS_V(nir, dxil_spirv_nir_discard_point_size_var);

   NIR_PASS_V(nir, nir_remove_dead_variables,
              nir_var_shader_in | nir_var_shader_out |
              nir_var_system_value | nir_var_mem_shared,
              NULL);

   uint32_t push_constant_size = 0;
   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_push_const,
              nir_address_format_32bit_offset);
   NIR_PASS_V(nir, dxil_spirv_nir_lower_load_push_constant,
              spirv_opts.ubo_addr_format,
              conf->push_constant_cbv.register_space,
              conf->push_constant_cbv.base_shader_register,
              &push_constant_size);

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


   if (conf->yz_flip.mode != DXIL_SPIRV_YZ_FLIP_NONE) {
      assert(stage == MESA_SHADER_VERTEX || stage == MESA_SHADER_GEOMETRY);
      NIR_PASS_V(nir,
                 dxil_spirv_nir_lower_yz_flip,
                 conf, &requires_runtime_data);
   }

   if (requires_runtime_data) {
      add_runtime_data_var(nir, conf->runtime_data_cbv.register_space,
                           conf->runtime_data_cbv.base_shader_register);
   }

   if (push_constant_size > 0) {
      add_push_constant_var(nir, push_constant_size,
                            conf->push_constant_cbv.register_space,
                            conf->push_constant_cbv.base_shader_register);
   }

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

   NIR_PASS_V(nir, dxil_spirv_nir_fix_sample_mask_type);
   NIR_PASS_V(nir, dxil_nir_lower_atomics_to_dxil);
   NIR_PASS_V(nir, dxil_nir_split_clip_cull_distance);
   NIR_PASS_V(nir, dxil_nir_lower_loads_stores_to_dxil);
   NIR_PASS_V(nir, dxil_nir_split_typed_samplers);
   NIR_PASS_V(nir, dxil_nir_lower_bool_input);
   NIR_PASS_V(nir, nir_opt_dce);
   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_uniform, NULL);

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   nir->info.inputs_read =
      dxil_reassign_driver_locations(nir, nir_var_shader_in, 0);

   if (stage != MESA_SHADER_FRAGMENT) {
      nir->info.outputs_written =
         dxil_reassign_driver_locations(nir, nir_var_shader_out, 0);
   } else {
      dxil_sort_ps_outputs(nir);
   }

   struct nir_to_dxil_options opts = {.environment = DXIL_ENVIRONMENT_VULKAN};

   if (dgb_opts->dump_nir)
      nir_print_shader(nir, stderr);

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
