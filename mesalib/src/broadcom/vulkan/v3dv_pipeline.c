/*
 * Copyright © 2019 Raspberry Pi
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "vk_util.h"

#include "v3dv_debug.h"
#include "v3dv_private.h"

#include "vk_format_info.h"

#include "common/v3d_debug.h"

#include "compiler/nir/nir_builder.h"
#include "nir/nir_serialize.h"

#include "util/u_atomic.h"
#include "util/u_prim.h"
#include "util/os_time.h"

#include "vulkan/util/vk_format.h"

static VkResult
compute_vpm_config(struct v3dv_pipeline *pipeline);

void
v3dv_print_v3d_key(struct v3d_key *key,
                   uint32_t v3d_key_size)
{
   struct mesa_sha1 ctx;
   unsigned char sha1[20];
   char sha1buf[41];

   _mesa_sha1_init(&ctx);

   _mesa_sha1_update(&ctx, key, v3d_key_size);

   _mesa_sha1_final(&ctx, sha1);
   _mesa_sha1_format(sha1buf, sha1);

   fprintf(stderr, "key %p: %s\n", key, sha1buf);
}

static void
pipeline_compute_sha1_from_nir(nir_shader *nir,
                               unsigned char sha1[20])
{
   assert(nir);
   struct blob blob;
   blob_init(&blob);

   nir_serialize(&blob, nir, false);
   if (!blob.out_of_memory)
      _mesa_sha1_compute(blob.data, blob.size, sha1);

   blob_finish(&blob);
}

void
v3dv_shader_module_internal_init(struct v3dv_device *device,
                                 struct vk_shader_module *module,
                                 nir_shader *nir)
{
   vk_object_base_init(&device->vk, &module->base,
                       VK_OBJECT_TYPE_SHADER_MODULE);
   module->nir = nir;
   module->size = 0;

   pipeline_compute_sha1_from_nir(nir, module->sha1);
}

void
v3dv_shader_variant_destroy(struct v3dv_device *device,
                            struct v3dv_shader_variant *variant)
{
   /* The assembly BO is shared by all variants in the pipeline, so it can't
    * be freed here and should be freed with the pipeline
    */
   ralloc_free(variant->prog_data.base);
   vk_free(&device->vk.alloc, variant);
}

static void
destroy_pipeline_stage(struct v3dv_device *device,
                       struct v3dv_pipeline_stage *p_stage,
                       const VkAllocationCallbacks *pAllocator)
{
   if (!p_stage)
      return;

   ralloc_free(p_stage->nir);
   vk_free2(&device->vk.alloc, pAllocator, p_stage);
}

static void
pipeline_free_stages(struct v3dv_device *device,
                     struct v3dv_pipeline *pipeline,
                     const VkAllocationCallbacks *pAllocator)
{
   assert(pipeline);

   /* FIXME: we can't just use a loop over mesa stage due the bin, would be
    * good to find an alternative.
    */
   destroy_pipeline_stage(device, pipeline->vs, pAllocator);
   destroy_pipeline_stage(device, pipeline->vs_bin, pAllocator);
   destroy_pipeline_stage(device, pipeline->gs, pAllocator);
   destroy_pipeline_stage(device, pipeline->gs_bin, pAllocator);
   destroy_pipeline_stage(device, pipeline->fs, pAllocator);
   destroy_pipeline_stage(device, pipeline->cs, pAllocator);

   pipeline->vs = NULL;
   pipeline->vs_bin = NULL;
   pipeline->gs = NULL;
   pipeline->gs_bin = NULL;
   pipeline->fs = NULL;
   pipeline->cs = NULL;
}

static void
v3dv_destroy_pipeline(struct v3dv_pipeline *pipeline,
                      struct v3dv_device *device,
                      const VkAllocationCallbacks *pAllocator)
{
   if (!pipeline)
      return;

   pipeline_free_stages(device, pipeline, pAllocator);

   if (pipeline->shared_data) {
      v3dv_pipeline_shared_data_unref(device, pipeline->shared_data);
      pipeline->shared_data = NULL;
   }

   if (pipeline->spill.bo) {
      assert(pipeline->spill.size_per_thread > 0);
      v3dv_bo_free(device, pipeline->spill.bo);
   }

   if (pipeline->default_attribute_values) {
      v3dv_bo_free(device, pipeline->default_attribute_values);
      pipeline->default_attribute_values = NULL;
   }

   vk_object_free(&device->vk, pAllocator, pipeline);
}

VKAPI_ATTR void VKAPI_CALL
v3dv_DestroyPipeline(VkDevice _device,
                     VkPipeline _pipeline,
                     const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_pipeline, pipeline, _pipeline);

   if (!pipeline)
      return;

   v3dv_destroy_pipeline(pipeline, device, pAllocator);
}

static const struct spirv_to_nir_options default_spirv_options =  {
   .caps = {
      .device_group = true,
      .multiview = true,
      .subgroup_basic = true,
      .variable_pointers = true,
    },
   .ubo_addr_format = nir_address_format_32bit_index_offset,
   .ssbo_addr_format = nir_address_format_32bit_index_offset,
   .phys_ssbo_addr_format = nir_address_format_64bit_global,
   .push_const_addr_format = nir_address_format_logical,
   .shared_addr_format = nir_address_format_32bit_offset,
};

const nir_shader_compiler_options v3dv_nir_options = {
   .lower_uadd_sat = true,
   .lower_iadd_sat = true,
   .lower_all_io_to_temps = true,
   .lower_extract_byte = true,
   .lower_extract_word = true,
   .lower_insert_byte = true,
   .lower_insert_word = true,
   .lower_bitfield_insert_to_shifts = true,
   .lower_bitfield_extract_to_shifts = true,
   .lower_bitfield_reverse = true,
   .lower_bit_count = true,
   .lower_cs_local_id_from_index = true,
   .lower_ffract = true,
   .lower_fmod = true,
   .lower_pack_unorm_2x16 = true,
   .lower_pack_snorm_2x16 = true,
   .lower_unpack_unorm_2x16 = true,
   .lower_unpack_snorm_2x16 = true,
   .lower_pack_unorm_4x8 = true,
   .lower_pack_snorm_4x8 = true,
   .lower_unpack_unorm_4x8 = true,
   .lower_unpack_snorm_4x8 = true,
   .lower_pack_half_2x16 = true,
   .lower_unpack_half_2x16 = true,
   /* FIXME: see if we can avoid the uadd_carry and usub_borrow lowering and
    * get the tests to pass since it might produce slightly better code.
    */
   .lower_uadd_carry = true,
   .lower_usub_borrow = true,
   /* FIXME: check if we can use multop + umul24 to implement mul2x32_64
    * without lowering.
    */
   .lower_mul_2x32_64 = true,
   .lower_fdiv = true,
   .lower_find_lsb = true,
   .lower_ffma16 = true,
   .lower_ffma32 = true,
   .lower_ffma64 = true,
   .lower_flrp32 = true,
   .lower_fpow = true,
   .lower_fsat = true,
   .lower_fsqrt = true,
   .lower_ifind_msb = true,
   .lower_isign = true,
   .lower_ldexp = true,
   .lower_mul_high = true,
   .lower_wpos_pntc = true,
   .lower_rotate = true,
   .lower_to_scalar = true,
   .lower_device_index_to_zero = true,
   .has_fsub = true,
   .has_isub = true,
   .vertex_id_zero_based = false, /* FIXME: to set this to true, the intrinsic
                                   * needs to be supported */
   .lower_interpolate_at = true,
   .max_unroll_iterations = 16,
   .force_indirect_unrolling = (nir_var_shader_in | nir_var_function_temp),
   .divergence_analysis_options =
      nir_divergence_multiple_workgroup_per_compute_subgroup
};

const nir_shader_compiler_options *
v3dv_pipeline_get_nir_options(void)
{
   return &v3dv_nir_options;
}

#define OPT(pass, ...) ({                                  \
   bool this_progress = false;                             \
   NIR_PASS(this_progress, nir, pass, ##__VA_ARGS__);      \
   if (this_progress)                                      \
      progress = true;                                     \
   this_progress;                                          \
})

static void
nir_optimize(nir_shader *nir, bool allow_copies)
{
   bool progress;

   do {
      progress = false;
      OPT(nir_split_array_vars, nir_var_function_temp);
      OPT(nir_shrink_vec_array_vars, nir_var_function_temp);
      OPT(nir_opt_deref);
      OPT(nir_lower_vars_to_ssa);
      if (allow_copies) {
         /* Only run this pass in the first call to nir_optimize.  Later calls
          * assume that we've lowered away any copy_deref instructions and we
          * don't want to introduce any more.
          */
         OPT(nir_opt_find_array_copies);
      }
      OPT(nir_opt_copy_prop_vars);
      OPT(nir_opt_dead_write_vars);
      OPT(nir_opt_combine_stores, nir_var_all);

      OPT(nir_lower_alu_to_scalar, NULL, NULL);

      OPT(nir_copy_prop);
      OPT(nir_lower_phis_to_scalar, false);

      OPT(nir_copy_prop);
      OPT(nir_opt_dce);
      OPT(nir_opt_cse);
      OPT(nir_opt_combine_stores, nir_var_all);

      /* Passing 0 to the peephole select pass causes it to convert
       * if-statements that contain only move instructions in the branches
       * regardless of the count.
       *
       * Passing 1 to the peephole select pass causes it to convert
       * if-statements that contain at most a single ALU instruction (total)
       * in both branches.
       */
      OPT(nir_opt_peephole_select, 0, false, false);
      OPT(nir_opt_peephole_select, 8, false, true);

      OPT(nir_opt_intrinsics);
      OPT(nir_opt_idiv_const, 32);
      OPT(nir_opt_algebraic);
      OPT(nir_opt_constant_folding);

      OPT(nir_opt_dead_cf);

      OPT(nir_opt_if, false);
      OPT(nir_opt_conditional_discard);

      OPT(nir_opt_remove_phis);
      OPT(nir_opt_undef);
      OPT(nir_lower_pack);
   } while (progress);

   OPT(nir_remove_dead_variables, nir_var_function_temp, NULL);
}

static void
preprocess_nir(nir_shader *nir)
{
   /* We have to lower away local variable initializers right before we
    * inline functions.  That way they get properly initialized at the top
    * of the function and not at the top of its caller.
    */
   NIR_PASS_V(nir, nir_lower_variable_initializers, nir_var_function_temp);
   NIR_PASS_V(nir, nir_lower_returns);
   NIR_PASS_V(nir, nir_inline_functions);
   NIR_PASS_V(nir, nir_opt_deref);

   /* Pick off the single entrypoint that we want */
   foreach_list_typed_safe(nir_function, func, node, &nir->functions) {
      if (func->is_entrypoint)
         func->name = ralloc_strdup(func, "main");
      else
         exec_node_remove(&func->node);
   }
   assert(exec_list_length(&nir->functions) == 1);

   /* Vulkan uses the separate-shader linking model */
   nir->info.separate_shader = true;

   /* Make sure we lower variable initializers on output variables so that
    * nir_remove_dead_variables below sees the corresponding stores
    */
   NIR_PASS_V(nir, nir_lower_variable_initializers, nir_var_shader_out);

   /* Now that we've deleted all but the main function, we can go ahead and
    * lower the rest of the variable initializers.
    */
   NIR_PASS_V(nir, nir_lower_variable_initializers, ~0);

   /* Split member structs.  We do this before lower_io_to_temporaries so that
    * it doesn't lower system values to temporaries by accident.
    */
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_split_per_member_structs);

   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS_V(nir, nir_lower_io_to_vector, nir_var_shader_out);
   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS_V(nir, nir_lower_input_attachments,
                 &(nir_input_attachment_options) {
                    .use_fragcoord_sysval = false,
                       });
   }

   NIR_PASS_V(nir, nir_lower_explicit_io,
              nir_var_mem_push_const,
              nir_address_format_32bit_offset);

   NIR_PASS_V(nir, nir_lower_explicit_io,
              nir_var_mem_ubo | nir_var_mem_ssbo,
              nir_address_format_32bit_index_offset);

   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_shader_in |
              nir_var_shader_out | nir_var_system_value | nir_var_mem_shared,
              NULL);

   NIR_PASS_V(nir, nir_propagate_invariant, false);
   NIR_PASS_V(nir, nir_lower_io_to_temporaries,
              nir_shader_get_entrypoint(nir), true, false);

   NIR_PASS_V(nir, nir_lower_system_values);
   NIR_PASS_V(nir, nir_lower_clip_cull_distance_arrays);

   NIR_PASS_V(nir, nir_lower_alu_to_scalar, NULL, NULL);

   NIR_PASS_V(nir, nir_normalize_cubemap_coords);

   NIR_PASS_V(nir, nir_lower_global_vars_to_local);

   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_split_struct_vars, nir_var_function_temp);

   nir_optimize(nir, true);

   NIR_PASS_V(nir, nir_lower_load_const_to_scalar);

   /* Lower a bunch of stuff */
   NIR_PASS_V(nir, nir_lower_var_copies);

   NIR_PASS_V(nir, nir_lower_indirect_derefs, nir_var_shader_in, UINT32_MAX);

   NIR_PASS_V(nir, nir_lower_indirect_derefs,
              nir_var_function_temp, 2);

   NIR_PASS_V(nir, nir_lower_array_deref_of_vec,
              nir_var_mem_ubo | nir_var_mem_ssbo,
              nir_lower_direct_array_deref_of_vec_load);

   NIR_PASS_V(nir, nir_lower_frexp);

   /* Get rid of split copies */
   nir_optimize(nir, false);
}

static nir_shader *
shader_module_compile_to_nir(struct v3dv_device *device,
                             struct v3dv_pipeline_stage *stage)
{
   nir_shader *nir;
   const nir_shader_compiler_options *nir_options = &v3dv_nir_options;

   if (!stage->module->nir) {
      uint32_t *spirv = (uint32_t *) stage->module->data;
      assert(stage->module->size % 4 == 0);

      if (unlikely(V3D_DEBUG & V3D_DEBUG_DUMP_SPIRV))
         v3dv_print_spirv(stage->module->data, stage->module->size, stderr);

      uint32_t num_spec_entries = 0;
      struct nir_spirv_specialization *spec_entries =
         vk_spec_info_to_nir_spirv(stage->spec_info, &num_spec_entries);
      const struct spirv_to_nir_options spirv_options = default_spirv_options;
      nir = spirv_to_nir(spirv, stage->module->size / 4,
                         spec_entries, num_spec_entries,
                         broadcom_shader_stage_to_gl(stage->stage),
                         stage->entrypoint,
                         &spirv_options, nir_options);
      assert(nir);
      nir_validate_shader(nir, "after spirv_to_nir");
      free(spec_entries);
   } else {
      /* For NIR modules created by the driver we can't consume the NIR
       * directly, we need to clone it first, since ownership of the NIR code
       * (as with SPIR-V code for SPIR-V shaders), belongs to the creator
       * of the module and modules can be destroyed immediately after been used
       * to create pipelines.
       */
      nir = nir_shader_clone(NULL, stage->module->nir);
      nir_validate_shader(nir, "nir module");
   }
   assert(nir->info.stage == broadcom_shader_stage_to_gl(stage->stage));

   const struct nir_lower_sysvals_to_varyings_options sysvals_to_varyings = {
      .frag_coord = true,
      .point_coord = true,
   };
   NIR_PASS_V(nir, nir_lower_sysvals_to_varyings, &sysvals_to_varyings);

   if (unlikely(V3D_DEBUG & (V3D_DEBUG_NIR |
                             v3d_debug_flag_for_shader_stage(
                                broadcom_shader_stage_to_gl(stage->stage))))) {
      fprintf(stderr, "Initial form: %s prog %d NIR:\n",
              broadcom_shader_stage_name(stage->stage),
              stage->program_id);
      nir_print_shader(nir, stderr);
      fprintf(stderr, "\n");
   }

   preprocess_nir(nir);

   return nir;
}

static int
type_size_vec4(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

/* FIXME: the number of parameters for this method is somewhat big. Perhaps
 * rethink.
 */
static unsigned
descriptor_map_add(struct v3dv_descriptor_map *map,
                   int set,
                   int binding,
                   int array_index,
                   int array_size,
                   uint8_t return_size)
{
   assert(array_index < array_size);
   assert(return_size == 16 || return_size == 32);

   unsigned index = 0;
   for (unsigned i = 0; i < map->num_desc; i++) {
      if (set == map->set[i] &&
          binding == map->binding[i] &&
          array_index == map->array_index[i]) {
         assert(array_size == map->array_size[i]);
         if (return_size != map->return_size[index]) {
            /* It the return_size is different it means that the same sampler
             * was used for operations with different precision
             * requirement. In this case we need to ensure that we use the
             * larger one.
             */
            map->return_size[index] = 32;
         }
         return index;
      }
      index++;
   }

   assert(index == map->num_desc);

   map->set[map->num_desc] = set;
   map->binding[map->num_desc] = binding;
   map->array_index[map->num_desc] = array_index;
   map->array_size[map->num_desc] = array_size;
   map->return_size[map->num_desc] = return_size;
   map->num_desc++;

   return index;
}


static void
lower_load_push_constant(nir_builder *b, nir_intrinsic_instr *instr,
                         struct v3dv_pipeline *pipeline)
{
   assert(instr->intrinsic == nir_intrinsic_load_push_constant);
   instr->intrinsic = nir_intrinsic_load_uniform;
}

static struct v3dv_descriptor_map*
pipeline_get_descriptor_map(struct v3dv_pipeline *pipeline,
                            VkDescriptorType desc_type,
                            gl_shader_stage gl_stage,
                            bool is_sampler)
{
   enum broadcom_shader_stage broadcom_stage =
      gl_shader_stage_to_broadcom(gl_stage);

   assert(pipeline->shared_data &&
          pipeline->shared_data->maps[broadcom_stage]);

   switch(desc_type) {
   case VK_DESCRIPTOR_TYPE_SAMPLER:
      return &pipeline->shared_data->maps[broadcom_stage]->sampler_map;
   case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
   case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      return &pipeline->shared_data->maps[broadcom_stage]->texture_map;
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      return is_sampler ?
         &pipeline->shared_data->maps[broadcom_stage]->sampler_map :
         &pipeline->shared_data->maps[broadcom_stage]->texture_map;
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
      return &pipeline->shared_data->maps[broadcom_stage]->ubo_map;
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      return &pipeline->shared_data->maps[broadcom_stage]->ssbo_map;
   default:
      unreachable("Descriptor type unknown or not having a descriptor map");
   }
}

/* Gathers info from the intrinsic (set and binding) and then lowers it so it
 * could be used by the v3d_compiler */
static void
lower_vulkan_resource_index(nir_builder *b,
                            nir_intrinsic_instr *instr,
                            nir_shader *shader,
                            struct v3dv_pipeline *pipeline,
                            const struct v3dv_pipeline_layout *layout)
{
   assert(instr->intrinsic == nir_intrinsic_vulkan_resource_index);

   nir_const_value *const_val = nir_src_as_const_value(instr->src[0]);

   unsigned set = nir_intrinsic_desc_set(instr);
   unsigned binding = nir_intrinsic_binding(instr);
   struct v3dv_descriptor_set_layout *set_layout = layout->set[set].layout;
   struct v3dv_descriptor_set_binding_layout *binding_layout =
      &set_layout->binding[binding];
   unsigned index = 0;
   const VkDescriptorType desc_type = nir_intrinsic_desc_type(instr);

   switch (desc_type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: {
      struct v3dv_descriptor_map *descriptor_map =
         pipeline_get_descriptor_map(pipeline, desc_type, shader->info.stage, false);

      if (!const_val)
         unreachable("non-constant vulkan_resource_index array index");

      index = descriptor_map_add(descriptor_map, set, binding,
                                 const_val->u32,
                                 binding_layout->array_size,
                                 32 /* return_size: doesn't really apply for this case */);

      if (desc_type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
         /* skip index 0 which is used for push constants */
         index++;
      }
      break;
   }

   default:
      unreachable("unsupported desc_type for vulkan_resource_index");
      break;
   }

   /* Since we use the deref pass, both vulkan_resource_index and
    * vulkan_load_descriptor return a vec2 providing an index and
    * offset. Our backend compiler only cares about the index part.
    */
   nir_ssa_def_rewrite_uses(&instr->dest.ssa,
                            nir_imm_ivec2(b, index, 0));
   nir_instr_remove(&instr->instr);
}

/* Returns return_size, so it could be used for the case of not having a
 * sampler object
 */
static uint8_t
lower_tex_src_to_offset(nir_builder *b, nir_tex_instr *instr, unsigned src_idx,
                        nir_shader *shader,
                        struct v3dv_pipeline *pipeline,
                        const struct v3dv_pipeline_layout *layout)
{
   nir_ssa_def *index = NULL;
   unsigned base_index = 0;
   unsigned array_elements = 1;
   nir_tex_src *src = &instr->src[src_idx];
   bool is_sampler = src->src_type == nir_tex_src_sampler_deref;

   /* We compute first the offsets */
   nir_deref_instr *deref = nir_instr_as_deref(src->src.ssa->parent_instr);
   while (deref->deref_type != nir_deref_type_var) {
      assert(deref->parent.is_ssa);
      nir_deref_instr *parent =
         nir_instr_as_deref(deref->parent.ssa->parent_instr);

      assert(deref->deref_type == nir_deref_type_array);

      if (nir_src_is_const(deref->arr.index) && index == NULL) {
         /* We're still building a direct index */
         base_index += nir_src_as_uint(deref->arr.index) * array_elements;
      } else {
         if (index == NULL) {
            /* We used to be direct but not anymore */
            index = nir_imm_int(b, base_index);
            base_index = 0;
         }

         index = nir_iadd(b, index,
                          nir_imul(b, nir_imm_int(b, array_elements),
                                   nir_ssa_for_src(b, deref->arr.index, 1)));
      }

      array_elements *= glsl_get_length(parent->type);

      deref = parent;
   }

   if (index)
      index = nir_umin(b, index, nir_imm_int(b, array_elements - 1));

   /* We have the offsets, we apply them, rewriting the source or removing
    * instr if needed
    */
   if (index) {
      nir_instr_rewrite_src(&instr->instr, &src->src,
                            nir_src_for_ssa(index));

      src->src_type = is_sampler ?
         nir_tex_src_sampler_offset :
         nir_tex_src_texture_offset;
   } else {
      nir_tex_instr_remove_src(instr, src_idx);
   }

   uint32_t set = deref->var->data.descriptor_set;
   uint32_t binding = deref->var->data.binding;
   /* FIXME: this is a really simplified check for the precision to be used
    * for the sampling. Right now we are ony checking for the variables used
    * on the operation itself, but there are other cases that we could use to
    * infer the precision requirement.
    */
   bool relaxed_precision = deref->var->data.precision == GLSL_PRECISION_MEDIUM ||
                            deref->var->data.precision == GLSL_PRECISION_LOW;
   struct v3dv_descriptor_set_layout *set_layout = layout->set[set].layout;
   struct v3dv_descriptor_set_binding_layout *binding_layout =
      &set_layout->binding[binding];

   /* For input attachments, the shader includes the attachment_idx. As we are
    * treating them as a texture, we only want the base_index
    */
   uint32_t array_index = binding_layout->type != VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT ?
      deref->var->data.index + base_index :
      base_index;

   uint8_t return_size;
   if (unlikely(V3D_DEBUG & V3D_DEBUG_TMU_16BIT))
      return_size = 16;
   else  if (unlikely(V3D_DEBUG & V3D_DEBUG_TMU_32BIT))
      return_size = 32;
   else
      return_size = relaxed_precision || instr->is_shadow ? 16 : 32;

   struct v3dv_descriptor_map *map =
      pipeline_get_descriptor_map(pipeline, binding_layout->type,
                                  shader->info.stage, is_sampler);
   int desc_index =
      descriptor_map_add(map,
                         deref->var->data.descriptor_set,
                         deref->var->data.binding,
                         array_index,
                         binding_layout->array_size,
                         return_size);

   if (is_sampler)
      instr->sampler_index = desc_index;
   else
      instr->texture_index = desc_index;

   return return_size;
}

static bool
lower_sampler(nir_builder *b, nir_tex_instr *instr,
              nir_shader *shader,
              struct v3dv_pipeline *pipeline,
              const struct v3dv_pipeline_layout *layout)
{
   uint8_t return_size = 0;

   int texture_idx =
      nir_tex_instr_src_index(instr, nir_tex_src_texture_deref);

   if (texture_idx >= 0)
      return_size = lower_tex_src_to_offset(b, instr, texture_idx, shader,
                                            pipeline, layout);

   int sampler_idx =
      nir_tex_instr_src_index(instr, nir_tex_src_sampler_deref);

   if (sampler_idx >= 0)
      lower_tex_src_to_offset(b, instr, sampler_idx, shader, pipeline, layout);

   if (texture_idx < 0 && sampler_idx < 0)
      return false;

   /* If we don't have a sampler, we assign it the idx we reserve for this
    * case, and we ensure that it is using the correct return size.
    */
   if (sampler_idx < 0) {
      instr->sampler_index = return_size == 16 ?
         V3DV_NO_SAMPLER_16BIT_IDX : V3DV_NO_SAMPLER_32BIT_IDX;
   }

   return true;
}

/* FIXME: really similar to lower_tex_src_to_offset, perhaps refactor? */
static void
lower_image_deref(nir_builder *b,
                  nir_intrinsic_instr *instr,
                  nir_shader *shader,
                  struct v3dv_pipeline *pipeline,
                  const struct v3dv_pipeline_layout *layout)
{
   nir_deref_instr *deref = nir_src_as_deref(instr->src[0]);
   nir_ssa_def *index = NULL;
   unsigned array_elements = 1;
   unsigned base_index = 0;

   while (deref->deref_type != nir_deref_type_var) {
      assert(deref->parent.is_ssa);
      nir_deref_instr *parent =
         nir_instr_as_deref(deref->parent.ssa->parent_instr);

      assert(deref->deref_type == nir_deref_type_array);

      if (nir_src_is_const(deref->arr.index) && index == NULL) {
         /* We're still building a direct index */
         base_index += nir_src_as_uint(deref->arr.index) * array_elements;
      } else {
         if (index == NULL) {
            /* We used to be direct but not anymore */
            index = nir_imm_int(b, base_index);
            base_index = 0;
         }

         index = nir_iadd(b, index,
                          nir_imul(b, nir_imm_int(b, array_elements),
                                   nir_ssa_for_src(b, deref->arr.index, 1)));
      }

      array_elements *= glsl_get_length(parent->type);

      deref = parent;
   }

   if (index)
      index = nir_umin(b, index, nir_imm_int(b, array_elements - 1));

   uint32_t set = deref->var->data.descriptor_set;
   uint32_t binding = deref->var->data.binding;
   struct v3dv_descriptor_set_layout *set_layout = layout->set[set].layout;
   struct v3dv_descriptor_set_binding_layout *binding_layout =
      &set_layout->binding[binding];

   uint32_t array_index = deref->var->data.index + base_index;

   assert(binding_layout->type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
          binding_layout->type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER);

   struct v3dv_descriptor_map *map =
      pipeline_get_descriptor_map(pipeline, binding_layout->type,
                                  shader->info.stage, false);

   int desc_index =
      descriptor_map_add(map,
                         deref->var->data.descriptor_set,
                         deref->var->data.binding,
                         array_index,
                         binding_layout->array_size,
                         32 /* return_size: doesn't apply for textures */);

   /* Note: we don't need to do anything here in relation to the precision and
    * the output size because for images we can infer that info from the image
    * intrinsic, that includes the image format (see
    * NIR_INTRINSIC_FORMAT). That is done by the v3d compiler.
    */

   index = nir_imm_int(b, desc_index);

   nir_rewrite_image_intrinsic(instr, index, false);
}

static bool
lower_intrinsic(nir_builder *b, nir_intrinsic_instr *instr,
                nir_shader *shader,
                struct v3dv_pipeline *pipeline,
                const struct v3dv_pipeline_layout *layout)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_layer_id:
      /* FIXME: if layered rendering gets supported, this would need a real
       * lowering
       */
      nir_ssa_def_rewrite_uses(&instr->dest.ssa,
                               nir_imm_int(b, 0));
      nir_instr_remove(&instr->instr);
      return true;

   case nir_intrinsic_load_push_constant:
      lower_load_push_constant(b, instr, pipeline);
      return true;

   case nir_intrinsic_vulkan_resource_index:
      lower_vulkan_resource_index(b, instr, shader, pipeline, layout);
      return true;

   case nir_intrinsic_load_vulkan_descriptor: {
      /* Loading the descriptor happens as part of load/store instructions,
       * so for us this is a no-op.
       */
      nir_ssa_def_rewrite_uses(&instr->dest.ssa, instr->src[0].ssa);
      nir_instr_remove(&instr->instr);
      return true;
   }

   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_store:
   case nir_intrinsic_image_deref_atomic_add:
   case nir_intrinsic_image_deref_atomic_imin:
   case nir_intrinsic_image_deref_atomic_umin:
   case nir_intrinsic_image_deref_atomic_imax:
   case nir_intrinsic_image_deref_atomic_umax:
   case nir_intrinsic_image_deref_atomic_and:
   case nir_intrinsic_image_deref_atomic_or:
   case nir_intrinsic_image_deref_atomic_xor:
   case nir_intrinsic_image_deref_atomic_exchange:
   case nir_intrinsic_image_deref_atomic_comp_swap:
   case nir_intrinsic_image_deref_size:
   case nir_intrinsic_image_deref_samples:
      lower_image_deref(b, instr, shader, pipeline, layout);
      return true;

   default:
      return false;
   }
}

static bool
lower_impl(nir_function_impl *impl,
           nir_shader *shader,
           struct v3dv_pipeline *pipeline,
           const struct v3dv_pipeline_layout *layout)
{
   nir_builder b;
   nir_builder_init(&b, impl);
   bool progress = false;

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         b.cursor = nir_before_instr(instr);
         switch (instr->type) {
         case nir_instr_type_tex:
            progress |=
               lower_sampler(&b, nir_instr_as_tex(instr), shader, pipeline, layout);
            break;
         case nir_instr_type_intrinsic:
            progress |=
               lower_intrinsic(&b, nir_instr_as_intrinsic(instr), shader,
                               pipeline, layout);
            break;
         default:
            break;
         }
      }
   }

   return progress;
}

static bool
lower_pipeline_layout_info(nir_shader *shader,
                           struct v3dv_pipeline *pipeline,
                           const struct v3dv_pipeline_layout *layout)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= lower_impl(function->impl, shader, pipeline, layout);
   }

   return progress;
}


static void
lower_fs_io(nir_shader *nir)
{
   /* Our backend doesn't handle array fragment shader outputs */
   NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, false);
   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_shader_out, NULL);

   nir_assign_io_var_locations(nir, nir_var_shader_in, &nir->num_inputs,
                               MESA_SHADER_FRAGMENT);

   nir_assign_io_var_locations(nir, nir_var_shader_out, &nir->num_outputs,
                               MESA_SHADER_FRAGMENT);

   NIR_PASS_V(nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
              type_size_vec4, 0);
}

static void
lower_gs_io(struct nir_shader *nir)
{
   NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, false);

   nir_assign_io_var_locations(nir, nir_var_shader_in, &nir->num_inputs,
                               MESA_SHADER_GEOMETRY);

   nir_assign_io_var_locations(nir, nir_var_shader_out, &nir->num_outputs,
                               MESA_SHADER_GEOMETRY);
}

static void
lower_vs_io(struct nir_shader *nir)
{
   NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, false);

   nir_assign_io_var_locations(nir, nir_var_shader_in, &nir->num_inputs,
                               MESA_SHADER_VERTEX);

   nir_assign_io_var_locations(nir, nir_var_shader_out, &nir->num_outputs,
                               MESA_SHADER_VERTEX);

   /* FIXME: if we call nir_lower_io, we get a crash later. Likely because it
    * overlaps with v3d_nir_lower_io. Need further research though.
    */
}

static void
shader_debug_output(const char *message, void *data)
{
   /* FIXME: We probably don't want to debug anything extra here, and in fact
    * the compiler is not using this callback too much, only as an alternative
    * way to debug out the shaderdb stats, that you can already get using
    * V3D_DEBUG=shaderdb. Perhaps it would make sense to revisit the v3d
    * compiler to remove that callback.
    */
}

static void
pipeline_populate_v3d_key(struct v3d_key *key,
                          const struct v3dv_pipeline_stage *p_stage,
                          uint32_t ucp_enables,
                          bool robust_buffer_access)
{
   assert(p_stage->pipeline->shared_data &&
          p_stage->pipeline->shared_data->maps[p_stage->stage]);

   /* The following values are default values used at pipeline create. We use
    * there 32 bit as default return size.
    */
   struct v3dv_descriptor_map *sampler_map =
      &p_stage->pipeline->shared_data->maps[p_stage->stage]->sampler_map;
   struct v3dv_descriptor_map *texture_map =
      &p_stage->pipeline->shared_data->maps[p_stage->stage]->texture_map;

   key->num_tex_used = texture_map->num_desc;
   assert(key->num_tex_used <= V3D_MAX_TEXTURE_SAMPLERS);
   for (uint32_t tex_idx = 0; tex_idx < texture_map->num_desc; tex_idx++) {
      key->tex[tex_idx].swizzle[0] = PIPE_SWIZZLE_X;
      key->tex[tex_idx].swizzle[1] = PIPE_SWIZZLE_Y;
      key->tex[tex_idx].swizzle[2] = PIPE_SWIZZLE_Z;
      key->tex[tex_idx].swizzle[3] = PIPE_SWIZZLE_W;
   }

   key->num_samplers_used = sampler_map->num_desc;
   assert(key->num_samplers_used <= V3D_MAX_TEXTURE_SAMPLERS);
   for (uint32_t sampler_idx = 0; sampler_idx < sampler_map->num_desc;
        sampler_idx++) {
      key->sampler[sampler_idx].return_size =
         sampler_map->return_size[sampler_idx];

      key->sampler[sampler_idx].return_channels =
         key->sampler[sampler_idx].return_size == 32 ? 4 : 2;
   }

   switch (p_stage->stage) {
   case BROADCOM_SHADER_VERTEX:
   case BROADCOM_SHADER_VERTEX_BIN:
      key->is_last_geometry_stage = p_stage->pipeline->gs == NULL;
      break;
   case BROADCOM_SHADER_GEOMETRY:
   case BROADCOM_SHADER_GEOMETRY_BIN:
      /* FIXME: while we don't implement tessellation shaders */
      key->is_last_geometry_stage = true;
      break;
   case BROADCOM_SHADER_FRAGMENT:
   case BROADCOM_SHADER_COMPUTE:
      key->is_last_geometry_stage = false;
      break;
   default:
      unreachable("unsupported shader stage");
   }

   /* Vulkan doesn't have fixed function state for user clip planes. Instead,
    * shaders can write to gl_ClipDistance[], in which case the SPIR-V compiler
    * takes care of adding a single compact array variable at
    * VARYING_SLOT_CLIP_DIST0, so we don't need any user clip plane lowering.
    *
    * The only lowering we are interested is specific to the fragment shader,
    * where we want to emit discards to honor writes to gl_ClipDistance[] in
    * previous stages. This is done via nir_lower_clip_fs() so we only set up
    * the ucp enable mask for that stage.
    */
   key->ucp_enables = ucp_enables;

   key->robust_buffer_access = robust_buffer_access;

   key->environment = V3D_ENVIRONMENT_VULKAN;
}

/* FIXME: anv maps to hw primitive type. Perhaps eventually we would do the
 * same. For not using prim_mode that is the one already used on v3d
 */
static const enum pipe_prim_type vk_to_pipe_prim_type[] = {
   [VK_PRIMITIVE_TOPOLOGY_POINT_LIST] = PIPE_PRIM_POINTS,
   [VK_PRIMITIVE_TOPOLOGY_LINE_LIST] = PIPE_PRIM_LINES,
   [VK_PRIMITIVE_TOPOLOGY_LINE_STRIP] = PIPE_PRIM_LINE_STRIP,
   [VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST] = PIPE_PRIM_TRIANGLES,
   [VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP] = PIPE_PRIM_TRIANGLE_STRIP,
   [VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN] = PIPE_PRIM_TRIANGLE_FAN,
   [VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY] = PIPE_PRIM_LINES_ADJACENCY,
   [VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY] = PIPE_PRIM_LINE_STRIP_ADJACENCY,
   [VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY] = PIPE_PRIM_TRIANGLES_ADJACENCY,
   [VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY] = PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY,
};

static const enum pipe_logicop vk_to_pipe_logicop[] = {
   [VK_LOGIC_OP_CLEAR] = PIPE_LOGICOP_CLEAR,
   [VK_LOGIC_OP_AND] = PIPE_LOGICOP_AND,
   [VK_LOGIC_OP_AND_REVERSE] = PIPE_LOGICOP_AND_REVERSE,
   [VK_LOGIC_OP_COPY] = PIPE_LOGICOP_COPY,
   [VK_LOGIC_OP_AND_INVERTED] = PIPE_LOGICOP_AND_INVERTED,
   [VK_LOGIC_OP_NO_OP] = PIPE_LOGICOP_NOOP,
   [VK_LOGIC_OP_XOR] = PIPE_LOGICOP_XOR,
   [VK_LOGIC_OP_OR] = PIPE_LOGICOP_OR,
   [VK_LOGIC_OP_NOR] = PIPE_LOGICOP_NOR,
   [VK_LOGIC_OP_EQUIVALENT] = PIPE_LOGICOP_EQUIV,
   [VK_LOGIC_OP_INVERT] = PIPE_LOGICOP_INVERT,
   [VK_LOGIC_OP_OR_REVERSE] = PIPE_LOGICOP_OR_REVERSE,
   [VK_LOGIC_OP_COPY_INVERTED] = PIPE_LOGICOP_COPY_INVERTED,
   [VK_LOGIC_OP_OR_INVERTED] = PIPE_LOGICOP_OR_INVERTED,
   [VK_LOGIC_OP_NAND] = PIPE_LOGICOP_NAND,
   [VK_LOGIC_OP_SET] = PIPE_LOGICOP_SET,
};

static void
pipeline_populate_v3d_fs_key(struct v3d_fs_key *key,
                             const VkGraphicsPipelineCreateInfo *pCreateInfo,
                             const struct v3dv_pipeline_stage *p_stage,
                             bool has_geometry_shader,
                             uint32_t ucp_enables)
{
   assert(p_stage->stage == BROADCOM_SHADER_FRAGMENT);

   memset(key, 0, sizeof(*key));

   const bool rba = p_stage->pipeline->device->features.robustBufferAccess;
   pipeline_populate_v3d_key(&key->base, p_stage, ucp_enables, rba);

   const VkPipelineInputAssemblyStateCreateInfo *ia_info =
      pCreateInfo->pInputAssemblyState;
   uint8_t topology = vk_to_pipe_prim_type[ia_info->topology];

   key->is_points = (topology == PIPE_PRIM_POINTS);
   key->is_lines = (topology >= PIPE_PRIM_LINES &&
                    topology <= PIPE_PRIM_LINE_STRIP);
   key->has_gs = has_geometry_shader;

   const VkPipelineColorBlendStateCreateInfo *cb_info =
      !pCreateInfo->pRasterizationState->rasterizerDiscardEnable ?
      pCreateInfo->pColorBlendState : NULL;

   key->logicop_func = cb_info && cb_info->logicOpEnable == VK_TRUE ?
                       vk_to_pipe_logicop[cb_info->logicOp] :
                       PIPE_LOGICOP_COPY;

   const bool raster_enabled =
      !pCreateInfo->pRasterizationState->rasterizerDiscardEnable;

   /* Multisample rasterization state must be ignored if rasterization
    * is disabled.
    */
   const VkPipelineMultisampleStateCreateInfo *ms_info =
      raster_enabled ? pCreateInfo->pMultisampleState : NULL;
   if (ms_info) {
      assert(ms_info->rasterizationSamples == VK_SAMPLE_COUNT_1_BIT ||
             ms_info->rasterizationSamples == VK_SAMPLE_COUNT_4_BIT);
      key->msaa = ms_info->rasterizationSamples > VK_SAMPLE_COUNT_1_BIT;

      if (key->msaa) {
         key->sample_coverage =
            p_stage->pipeline->sample_mask != (1 << V3D_MAX_SAMPLES) - 1;
         key->sample_alpha_to_coverage = ms_info->alphaToCoverageEnable;
         key->sample_alpha_to_one = ms_info->alphaToOneEnable;
      }
   }

   /* This is intended for V3D versions before 4.1, otherwise we just use the
    * tile buffer load/store swap R/B bit.
    */
   key->swap_color_rb = 0;

   const struct v3dv_render_pass *pass =
      v3dv_render_pass_from_handle(pCreateInfo->renderPass);
   const struct v3dv_subpass *subpass = p_stage->pipeline->subpass;
   for (uint32_t i = 0; i < subpass->color_count; i++) {
      const uint32_t att_idx = subpass->color_attachments[i].attachment;
      if (att_idx == VK_ATTACHMENT_UNUSED)
         continue;

      key->cbufs |= 1 << i;

      VkFormat fb_format = pass->attachments[att_idx].desc.format;
      enum pipe_format fb_pipe_format = vk_format_to_pipe_format(fb_format);

      /* If logic operations are enabled then we might emit color reads and we
       * need to know the color buffer format and swizzle for that
       */
      if (key->logicop_func != PIPE_LOGICOP_COPY) {
         key->color_fmt[i].format = fb_pipe_format;
         key->color_fmt[i].swizzle =
            v3dv_get_format_swizzle(p_stage->pipeline->device, fb_format);
      }

      const struct util_format_description *desc =
         vk_format_description(fb_format);

      if (desc->channel[0].type == UTIL_FORMAT_TYPE_FLOAT &&
          desc->channel[0].size == 32) {
         key->f32_color_rb |= 1 << i;
      }

      if (p_stage->nir->info.fs.untyped_color_outputs) {
         if (util_format_is_pure_uint(fb_pipe_format))
            key->uint_color_rb |= 1 << i;
         else if (util_format_is_pure_sint(fb_pipe_format))
            key->int_color_rb |= 1 << i;
      }

      if (key->is_points) {
         /* FIXME: The mask would need to be computed based on the shader
          * inputs. On gallium it is done at st_atom_rasterizer
          * (sprite_coord_enable). anv seems (need to confirm) to do that on
          * genX_pipeline (PointSpriteTextureCoordinateEnable). Would be also
          * better to have tests to guide filling the mask.
          */
         key->point_sprite_mask = 0;

         /* Vulkan mandates upper left. */
         key->point_coord_upper_left = true;
      }
   }
}

static void
setup_stage_outputs_from_next_stage_inputs(
   uint8_t next_stage_num_inputs,
   struct v3d_varying_slot *next_stage_input_slots,
   uint8_t *num_used_outputs,
   struct v3d_varying_slot *used_output_slots,
   uint32_t size_of_used_output_slots)
{
   *num_used_outputs = next_stage_num_inputs;
   memcpy(used_output_slots, next_stage_input_slots, size_of_used_output_slots);
}

static void
pipeline_populate_v3d_gs_key(struct v3d_gs_key *key,
                             const VkGraphicsPipelineCreateInfo *pCreateInfo,
                             const struct v3dv_pipeline_stage *p_stage)
{
   assert(p_stage->stage == BROADCOM_SHADER_GEOMETRY ||
          p_stage->stage == BROADCOM_SHADER_GEOMETRY_BIN);

   memset(key, 0, sizeof(*key));

   const bool rba = p_stage->pipeline->device->features.robustBufferAccess;
   pipeline_populate_v3d_key(&key->base, p_stage, 0, rba);

   struct v3dv_pipeline *pipeline = p_stage->pipeline;

   key->per_vertex_point_size =
      p_stage->nir->info.outputs_written & (1ull << VARYING_SLOT_PSIZ);

   key->is_coord = broadcom_shader_stage_is_binning(p_stage->stage);

   assert(key->base.is_last_geometry_stage);
   if (key->is_coord) {
      /* Output varyings in the last binning shader are only used for transform
       * feedback. Set to 0 as VK_EXT_transform_feedback is not supported.
       */
      key->num_used_outputs = 0;
   } else {
      struct v3dv_shader_variant *fs_variant =
         pipeline->shared_data->variants[BROADCOM_SHADER_FRAGMENT];

      STATIC_ASSERT(sizeof(key->used_outputs) ==
                    sizeof(fs_variant->prog_data.fs->input_slots));

      setup_stage_outputs_from_next_stage_inputs(
         fs_variant->prog_data.fs->num_inputs,
         fs_variant->prog_data.fs->input_slots,
         &key->num_used_outputs,
         key->used_outputs,
         sizeof(key->used_outputs));
   }
}

static void
pipeline_populate_v3d_vs_key(struct v3d_vs_key *key,
                             const VkGraphicsPipelineCreateInfo *pCreateInfo,
                             const struct v3dv_pipeline_stage *p_stage)
{
   assert(p_stage->stage == BROADCOM_SHADER_VERTEX ||
          p_stage->stage == BROADCOM_SHADER_VERTEX_BIN);

   memset(key, 0, sizeof(*key));

   const bool rba = p_stage->pipeline->device->features.robustBufferAccess;
   pipeline_populate_v3d_key(&key->base, p_stage, 0, rba);

   struct v3dv_pipeline *pipeline = p_stage->pipeline;

   /* Vulkan specifies a point size per vertex, so true for if the prim are
    * points, like on ES2)
    */
   const VkPipelineInputAssemblyStateCreateInfo *ia_info =
      pCreateInfo->pInputAssemblyState;
   uint8_t topology = vk_to_pipe_prim_type[ia_info->topology];

   /* FIXME: PRIM_POINTS is not enough, in gallium the full check is
    * PIPE_PRIM_POINTS && v3d->rasterizer->base.point_size_per_vertex */
   key->per_vertex_point_size = (topology == PIPE_PRIM_POINTS);

   key->is_coord = broadcom_shader_stage_is_binning(p_stage->stage);

   if (key->is_coord) { /* Binning VS*/
      if (key->base.is_last_geometry_stage) {
         /* Output varyings in the last binning shader are only used for
          * transform feedback. Set to 0 as VK_EXT_transform_feedback is not
          * supported.
          */
         key->num_used_outputs = 0;
      } else {
         /* Linking against GS binning program */
         assert(pipeline->gs);
         struct v3dv_shader_variant *gs_bin_variant =
            pipeline->shared_data->variants[BROADCOM_SHADER_GEOMETRY_BIN];

         STATIC_ASSERT(sizeof(key->used_outputs) ==
                       sizeof(gs_bin_variant->prog_data.gs->input_slots));

         setup_stage_outputs_from_next_stage_inputs(
            gs_bin_variant->prog_data.gs->num_inputs,
            gs_bin_variant->prog_data.gs->input_slots,
            &key->num_used_outputs,
            key->used_outputs,
            sizeof(key->used_outputs));
      }
   } else { /* Render VS */
      if (pipeline->gs) {
         /* Linking against GS render program */
         struct v3dv_shader_variant *gs_variant =
            pipeline->shared_data->variants[BROADCOM_SHADER_GEOMETRY];

         STATIC_ASSERT(sizeof(key->used_outputs) ==
                       sizeof(gs_variant->prog_data.gs->input_slots));

         setup_stage_outputs_from_next_stage_inputs(
            gs_variant->prog_data.gs->num_inputs,
            gs_variant->prog_data.gs->input_slots,
            &key->num_used_outputs,
            key->used_outputs,
            sizeof(key->used_outputs));
      } else {
         /* Linking against FS program */
         struct v3dv_shader_variant *fs_variant =
            pipeline->shared_data->variants[BROADCOM_SHADER_FRAGMENT];

         STATIC_ASSERT(sizeof(key->used_outputs) ==
                       sizeof(fs_variant->prog_data.fs->input_slots));

         setup_stage_outputs_from_next_stage_inputs(
            fs_variant->prog_data.fs->num_inputs,
            fs_variant->prog_data.fs->input_slots,
            &key->num_used_outputs,
            key->used_outputs,
            sizeof(key->used_outputs));
      }
   }

   const VkPipelineVertexInputStateCreateInfo *vi_info =
      pCreateInfo->pVertexInputState;
   for (uint32_t i = 0; i < vi_info->vertexAttributeDescriptionCount; i++) {
      const VkVertexInputAttributeDescription *desc =
         &vi_info->pVertexAttributeDescriptions[i];
      assert(desc->location < MAX_VERTEX_ATTRIBS);
      if (desc->format == VK_FORMAT_B8G8R8A8_UNORM)
         key->va_swap_rb_mask |= 1 << (VERT_ATTRIB_GENERIC0 + desc->location);
   }
}

/**
 * Creates the initial form of the pipeline stage for a binning shader by
 * cloning the render shader and flagging it as a coordinate shader.
 *
 * Returns NULL if it was not able to allocate the object, so it should be
 * handled as a VK_ERROR_OUT_OF_HOST_MEMORY error.
 */
static struct v3dv_pipeline_stage *
pipeline_stage_create_binning(const struct v3dv_pipeline_stage *src,
                              const VkAllocationCallbacks *pAllocator)
{
   struct v3dv_device *device = src->pipeline->device;

   struct v3dv_pipeline_stage *p_stage =
      vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*p_stage), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (p_stage == NULL)
      return NULL;

   assert(src->stage == BROADCOM_SHADER_VERTEX ||
          src->stage == BROADCOM_SHADER_GEOMETRY);

   enum broadcom_shader_stage bin_stage =
      src->stage == BROADCOM_SHADER_VERTEX ?
         BROADCOM_SHADER_VERTEX_BIN :
         BROADCOM_SHADER_GEOMETRY_BIN;

   p_stage->pipeline = src->pipeline;
   p_stage->stage = bin_stage;
   p_stage->entrypoint = src->entrypoint;
   p_stage->module = src->module;
   /* For binning shaders we will clone the NIR code from the corresponding
    * render shader later, when we call pipeline_compile_xxx_shader. This way
    * we only have to run the relevant NIR lowerings once for render shaders
    */
   p_stage->nir = NULL;
   p_stage->spec_info = src->spec_info;
   p_stage->feedback = (VkPipelineCreationFeedbackEXT) { 0 };
   memcpy(p_stage->shader_sha1, src->shader_sha1, 20);

   return p_stage;
}

/**
 * Returns false if it was not able to allocate or map the assembly bo memory.
 */
static bool
upload_assembly(struct v3dv_pipeline *pipeline)
{
   uint32_t total_size = 0;
   for (uint8_t stage = 0; stage < BROADCOM_SHADER_STAGES; stage++) {
      struct v3dv_shader_variant *variant =
         pipeline->shared_data->variants[stage];

      if (variant != NULL)
         total_size += variant->qpu_insts_size;
   }

   struct v3dv_bo *bo = v3dv_bo_alloc(pipeline->device, total_size,
                                      "pipeline shader assembly", true);
   if (!bo) {
      fprintf(stderr, "failed to allocate memory for shader\n");
      return false;
   }

   bool ok = v3dv_bo_map(pipeline->device, bo, total_size);
   if (!ok) {
      fprintf(stderr, "failed to map source shader buffer\n");
      return false;
   }

   uint32_t offset = 0;
   for (uint8_t stage = 0; stage < BROADCOM_SHADER_STAGES; stage++) {
      struct v3dv_shader_variant *variant =
         pipeline->shared_data->variants[stage];

      if (variant != NULL) {
         variant->assembly_offset = offset;

         memcpy(bo->map + offset, variant->qpu_insts, variant->qpu_insts_size);
         offset += variant->qpu_insts_size;

         /* We dont need qpu_insts anymore. */
         free(variant->qpu_insts);
         variant->qpu_insts = NULL;
      }
   }
   assert(total_size == offset);

   pipeline->shared_data->assembly_bo = bo;

   return true;
}

static void
pipeline_hash_graphics(const struct v3dv_pipeline *pipeline,
                       struct v3dv_pipeline_key *key,
                       unsigned char *sha1_out)
{
   struct mesa_sha1 ctx;
   _mesa_sha1_init(&ctx);

   /* We need to include all shader stages in the sha1 key as linking may modify
    * the shader code in any stage. An alternative would be to use the
    * serialized NIR, but that seems like an overkill.
    */
   _mesa_sha1_update(&ctx, pipeline->vs->shader_sha1,
                     sizeof(pipeline->vs->shader_sha1));

   if (pipeline->gs) {
      _mesa_sha1_update(&ctx, pipeline->gs->shader_sha1,
                        sizeof(pipeline->gs->shader_sha1));
   }

   _mesa_sha1_update(&ctx, pipeline->fs->shader_sha1,
                     sizeof(pipeline->fs->shader_sha1));

   _mesa_sha1_update(&ctx, key, sizeof(struct v3dv_pipeline_key));

   _mesa_sha1_final(&ctx, sha1_out);
}

static void
pipeline_hash_compute(const struct v3dv_pipeline *pipeline,
                      struct v3dv_pipeline_key *key,
                      unsigned char *sha1_out)
{
   struct mesa_sha1 ctx;
   _mesa_sha1_init(&ctx);

   _mesa_sha1_update(&ctx, pipeline->cs->shader_sha1,
                     sizeof(pipeline->cs->shader_sha1));

   _mesa_sha1_update(&ctx, key, sizeof(struct v3dv_pipeline_key));

   _mesa_sha1_final(&ctx, sha1_out);
}

/* Checks that the pipeline has enough spill size to use for any of their
 * variants
 */
static void
pipeline_check_spill_size(struct v3dv_pipeline *pipeline)
{
   uint32_t max_spill_size = 0;

   for(uint8_t stage = 0; stage < BROADCOM_SHADER_STAGES; stage++) {
      struct v3dv_shader_variant *variant =
         pipeline->shared_data->variants[stage];

      if (variant != NULL) {
         max_spill_size = MAX2(variant->prog_data.base->spill_size,
                               max_spill_size);
      }
   }

   if (max_spill_size > 0) {
      struct v3dv_device *device = pipeline->device;

      /* The TIDX register we use for choosing the area to access
       * for scratch space is: (core << 6) | (qpu << 2) | thread.
       * Even at minimum threadcount in a particular shader, that
       * means we still multiply by qpus by 4.
       */
      const uint32_t total_spill_size =
         4 * device->devinfo.qpu_count * max_spill_size;
      if (pipeline->spill.bo) {
         assert(pipeline->spill.size_per_thread > 0);
         v3dv_bo_free(device, pipeline->spill.bo);
      }
      pipeline->spill.bo =
         v3dv_bo_alloc(device, total_spill_size, "spill", true);
      pipeline->spill.size_per_thread = max_spill_size;
   }
}

/**
 * Creates a new shader_variant_create. Note that for prog_data is not const,
 * so it is assumed that the caller will prove a pointer that the
 * shader_variant will own.
 *
 * Creation doesn't include allocate a BD to store the content of qpu_insts,
 * as we will try to share the same bo for several shader variants. Also note
 * that qpu_ints being NULL is valid, for example if we are creating the
 * shader_variants from the cache, so we can just upload the assembly of all
 * the shader stages at once.
 */
struct v3dv_shader_variant *
v3dv_shader_variant_create(struct v3dv_device *device,
                           enum broadcom_shader_stage stage,
                           struct v3d_prog_data *prog_data,
                           uint32_t prog_data_size,
                           uint32_t assembly_offset,
                           uint64_t *qpu_insts,
                           uint32_t qpu_insts_size,
                           VkResult *out_vk_result)
{
   struct v3dv_shader_variant *variant =
      vk_zalloc(&device->vk.alloc, sizeof(*variant), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (variant == NULL) {
      *out_vk_result = VK_ERROR_OUT_OF_HOST_MEMORY;
      return NULL;
   }

   variant->stage = stage;
   variant->prog_data_size = prog_data_size;
   variant->prog_data.base = prog_data;

   variant->assembly_offset = assembly_offset;
   variant->qpu_insts_size = qpu_insts_size;
   variant->qpu_insts = qpu_insts;

   *out_vk_result = VK_SUCCESS;

   return variant;
}

/* For a given key, it returns the compiled version of the shader.  Returns a
 * new reference to the shader_variant to the caller, or NULL.
 *
 * If the method returns NULL it means that something wrong happened:
 *   * Not enough memory: this is one of the possible outcomes defined by
 *     vkCreateXXXPipelines. out_vk_result will return the proper oom error.
 *   * Compilation error: hypothetically this shouldn't happen, as the spec
 *     states that vkShaderModule needs to be created with a valid SPIR-V, so
 *     any compilation failure is a driver bug. In the practice, something as
 *     common as failing to register allocate can lead to a compilation
 *     failure. In that case the only option (for any driver) is
 *     VK_ERROR_UNKNOWN, even if we know that the problem was a compiler
 *     error.
 */
static struct v3dv_shader_variant *
pipeline_compile_shader_variant(struct v3dv_pipeline_stage *p_stage,
                                struct v3d_key *key,
                                size_t key_size,
                                const VkAllocationCallbacks *pAllocator,
                                VkResult *out_vk_result)
{
   int64_t stage_start = os_time_get_nano();

   struct v3dv_pipeline *pipeline = p_stage->pipeline;
   struct v3dv_physical_device *physical_device =
      &pipeline->device->instance->physicalDevice;
   const struct v3d_compiler *compiler = physical_device->compiler;

   if (unlikely(V3D_DEBUG & (V3D_DEBUG_NIR |
                             v3d_debug_flag_for_shader_stage
                             (broadcom_shader_stage_to_gl(p_stage->stage))))) {
      fprintf(stderr, "Just before v3d_compile: %s prog %d NIR:\n",
              broadcom_shader_stage_name(p_stage->stage),
              p_stage->program_id);
      nir_print_shader(p_stage->nir, stderr);
      fprintf(stderr, "\n");
   }

   uint64_t *qpu_insts;
   uint32_t qpu_insts_size;
   struct v3d_prog_data *prog_data;
   uint32_t prog_data_size =
      v3d_prog_data_size(broadcom_shader_stage_to_gl(p_stage->stage));

   qpu_insts = v3d_compile(compiler,
                           key, &prog_data,
                           p_stage->nir,
                           shader_debug_output, NULL,
                           p_stage->program_id, 0,
                           &qpu_insts_size);

   struct v3dv_shader_variant *variant = NULL;

   if (!qpu_insts) {
      fprintf(stderr, "Failed to compile %s prog %d NIR to VIR\n",
              gl_shader_stage_name(p_stage->stage),
              p_stage->program_id);
      *out_vk_result = VK_ERROR_UNKNOWN;
   } else {
      variant =
         v3dv_shader_variant_create(pipeline->device, p_stage->stage,
                                    prog_data, prog_data_size,
                                    0, /* assembly_offset, no final value yet */
                                    qpu_insts, qpu_insts_size,
                                    out_vk_result);
   }
   /* At this point we don't need anymore the nir shader, but we are freeing
    * all the temporary p_stage structs used during the pipeline creation when
    * we finish it, so let's not worry about freeing the nir here.
    */

   p_stage->feedback.duration += os_time_get_nano() - stage_start;

   return variant;
}

/* FIXME: C&P from st, common place? */
static void
st_nir_opts(nir_shader *nir)
{
   bool progress;

   do {
      progress = false;

      NIR_PASS_V(nir, nir_lower_vars_to_ssa);

      /* Linking deals with unused inputs/outputs, but here we can remove
       * things local to the shader in the hopes that we can cleanup other
       * things. This pass will also remove variables with only stores, so we
       * might be able to make progress after it.
       */
      NIR_PASS(progress, nir, nir_remove_dead_variables,
               (nir_variable_mode)(nir_var_function_temp |
                                   nir_var_shader_temp |
                                   nir_var_mem_shared),
               NULL);

      NIR_PASS(progress, nir, nir_opt_copy_prop_vars);
      NIR_PASS(progress, nir, nir_opt_dead_write_vars);

      if (nir->options->lower_to_scalar) {
         NIR_PASS_V(nir, nir_lower_alu_to_scalar, NULL, NULL);
         NIR_PASS_V(nir, nir_lower_phis_to_scalar, false);
      }

      NIR_PASS_V(nir, nir_lower_alu);
      NIR_PASS_V(nir, nir_lower_pack);
      NIR_PASS(progress, nir, nir_copy_prop);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_dce);
      if (nir_opt_trivial_continues(nir)) {
         progress = true;
         NIR_PASS(progress, nir, nir_copy_prop);
         NIR_PASS(progress, nir, nir_opt_dce);
      }
      NIR_PASS(progress, nir, nir_opt_if, false);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_peephole_select, 8, true, true);

      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);

      NIR_PASS(progress, nir, nir_opt_undef);
      NIR_PASS(progress, nir, nir_opt_conditional_discard);
   } while (progress);
}

static void
link_shaders(nir_shader *producer, nir_shader *consumer)
{
   assert(producer);
   assert(consumer);

   if (producer->options->lower_to_scalar) {
      NIR_PASS_V(producer, nir_lower_io_to_scalar_early, nir_var_shader_out);
      NIR_PASS_V(consumer, nir_lower_io_to_scalar_early, nir_var_shader_in);
   }

   nir_lower_io_arrays_to_elements(producer, consumer);

   st_nir_opts(producer);
   st_nir_opts(consumer);

   if (nir_link_opt_varyings(producer, consumer))
      st_nir_opts(consumer);

   NIR_PASS_V(producer, nir_remove_dead_variables, nir_var_shader_out, NULL);
   NIR_PASS_V(consumer, nir_remove_dead_variables, nir_var_shader_in, NULL);

   if (nir_remove_unused_varyings(producer, consumer)) {
      NIR_PASS_V(producer, nir_lower_global_vars_to_local);
      NIR_PASS_V(consumer, nir_lower_global_vars_to_local);

      st_nir_opts(producer);
      st_nir_opts(consumer);

      /* Optimizations can cause varyings to become unused.
       * nir_compact_varyings() depends on all dead varyings being removed so
       * we need to call nir_remove_dead_variables() again here.
       */
      NIR_PASS_V(producer, nir_remove_dead_variables, nir_var_shader_out, NULL);
      NIR_PASS_V(consumer, nir_remove_dead_variables, nir_var_shader_in, NULL);
   }
}

static void
pipeline_lower_nir(struct v3dv_pipeline *pipeline,
                   struct v3dv_pipeline_stage *p_stage,
                   struct v3dv_pipeline_layout *layout)
{
   int64_t stage_start = os_time_get_nano();

   assert(pipeline->shared_data &&
          pipeline->shared_data->maps[p_stage->stage]);

   nir_shader_gather_info(p_stage->nir, nir_shader_get_entrypoint(p_stage->nir));

   /* We add this because we need a valid sampler for nir_lower_tex to do
    * unpacking of the texture operation result, even for the case where there
    * is no sampler state.
    *
    * We add two of those, one for the case we need a 16bit return_size, and
    * another for the case we need a 32bit return size.
    */
   UNUSED unsigned index =
      descriptor_map_add(&pipeline->shared_data->maps[p_stage->stage]->sampler_map,
                         -1, -1, -1, 0, 16);
   assert(index == V3DV_NO_SAMPLER_16BIT_IDX);

   index =
      descriptor_map_add(&pipeline->shared_data->maps[p_stage->stage]->sampler_map,
                         -2, -2, -2, 0, 32);
   assert(index == V3DV_NO_SAMPLER_32BIT_IDX);

   /* Apply the actual pipeline layout to UBOs, SSBOs, and textures */
   NIR_PASS_V(p_stage->nir, lower_pipeline_layout_info, pipeline, layout);

   p_stage->feedback.duration += os_time_get_nano() - stage_start;
}

/**
 * The SPIR-V compiler will insert a sized compact array for
 * VARYING_SLOT_CLIP_DIST0 if the vertex shader writes to gl_ClipDistance[],
 * where the size of the array determines the number of active clip planes.
 */
static uint32_t
get_ucp_enable_mask(struct v3dv_pipeline_stage *p_stage)
{
   assert(p_stage->stage == BROADCOM_SHADER_VERTEX);
   const nir_shader *shader = p_stage->nir;
   assert(shader);

   nir_foreach_variable_with_modes(var, shader, nir_var_shader_out) {
      if (var->data.location == VARYING_SLOT_CLIP_DIST0) {
         assert(var->data.compact);
         return (1 << glsl_get_length(var->type)) - 1;
      }
   }
   return 0;
}

static nir_shader *
pipeline_stage_get_nir(struct v3dv_pipeline_stage *p_stage,
                       struct v3dv_pipeline *pipeline,
                       struct v3dv_pipeline_cache *cache)
{
   int64_t stage_start = os_time_get_nano();

   nir_shader *nir = NULL;

   nir = v3dv_pipeline_cache_search_for_nir(pipeline, cache,
                                            &v3dv_nir_options,
                                            p_stage->shader_sha1);

   if (nir) {
      assert(nir->info.stage == broadcom_shader_stage_to_gl(p_stage->stage));

      /* A NIR cach hit doesn't avoid the large majority of pipeline stage
       * creation so the cache hit is not recorded in the pipeline feedback
       * flags
       */

      p_stage->feedback.duration += os_time_get_nano() - stage_start;

      return nir;
   }

   nir = shader_module_compile_to_nir(pipeline->device, p_stage);

   if (nir) {
      struct v3dv_pipeline_cache *default_cache =
         &pipeline->device->default_pipeline_cache;

      v3dv_pipeline_cache_upload_nir(pipeline, cache, nir,
                                     p_stage->shader_sha1);

      /* Ensure that the variant is on the default cache, as cmd_buffer could
       * need to change the current variant
       */
      if (default_cache != cache) {
         v3dv_pipeline_cache_upload_nir(pipeline, default_cache, nir,
                                        p_stage->shader_sha1);
      }

      p_stage->feedback.duration += os_time_get_nano() - stage_start;

      return nir;
   }

   /* FIXME: this shouldn't happen, raise error? */
   return NULL;
}

static void
pipeline_hash_shader(const struct vk_shader_module *module,
                     const char *entrypoint,
                     gl_shader_stage stage,
                     const VkSpecializationInfo *spec_info,
                     unsigned char *sha1_out)
{
   struct mesa_sha1 ctx;
   _mesa_sha1_init(&ctx);

   _mesa_sha1_update(&ctx, module->sha1, sizeof(module->sha1));
   _mesa_sha1_update(&ctx, entrypoint, strlen(entrypoint));
   _mesa_sha1_update(&ctx, &stage, sizeof(stage));
   if (spec_info) {
      _mesa_sha1_update(&ctx, spec_info->pMapEntries,
                        spec_info->mapEntryCount *
                        sizeof(*spec_info->pMapEntries));
      _mesa_sha1_update(&ctx, spec_info->pData,
                        spec_info->dataSize);
   }

   _mesa_sha1_final(&ctx, sha1_out);
}

static VkResult
pipeline_compile_vertex_shader(struct v3dv_pipeline *pipeline,
                               const VkAllocationCallbacks *pAllocator,
                               const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   assert(pipeline->vs_bin != NULL);
   if (pipeline->vs_bin->nir == NULL) {
      assert(pipeline->vs->nir);
      pipeline->vs_bin->nir = nir_shader_clone(NULL, pipeline->vs->nir);
   }

   VkResult vk_result;
   struct v3d_vs_key key;
   pipeline_populate_v3d_vs_key(&key, pCreateInfo, pipeline->vs);
   pipeline->shared_data->variants[BROADCOM_SHADER_VERTEX] =
      pipeline_compile_shader_variant(pipeline->vs, &key.base, sizeof(key),
                                      pAllocator, &vk_result);
   if (vk_result != VK_SUCCESS)
      return vk_result;

   pipeline_populate_v3d_vs_key(&key, pCreateInfo, pipeline->vs_bin);
   pipeline->shared_data->variants[BROADCOM_SHADER_VERTEX_BIN] =
      pipeline_compile_shader_variant(pipeline->vs_bin, &key.base, sizeof(key),
                                      pAllocator, &vk_result);

   return vk_result;
}

static VkResult
pipeline_compile_geometry_shader(struct v3dv_pipeline *pipeline,
                                 const VkAllocationCallbacks *pAllocator,
                                 const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   assert(pipeline->gs);

   assert(pipeline->gs_bin != NULL);
   if (pipeline->gs_bin->nir == NULL) {
      assert(pipeline->gs->nir);
      pipeline->gs_bin->nir = nir_shader_clone(NULL, pipeline->gs->nir);
   }

   VkResult vk_result;
   struct v3d_gs_key key;
   pipeline_populate_v3d_gs_key(&key, pCreateInfo, pipeline->gs);
   pipeline->shared_data->variants[BROADCOM_SHADER_GEOMETRY] =
      pipeline_compile_shader_variant(pipeline->gs, &key.base, sizeof(key),
                                      pAllocator, &vk_result);
   if (vk_result != VK_SUCCESS)
      return vk_result;

   pipeline_populate_v3d_gs_key(&key, pCreateInfo, pipeline->gs_bin);
   pipeline->shared_data->variants[BROADCOM_SHADER_GEOMETRY_BIN] =
      pipeline_compile_shader_variant(pipeline->gs_bin, &key.base, sizeof(key),
                                      pAllocator, &vk_result);

   return vk_result;
}

static VkResult
pipeline_compile_fragment_shader(struct v3dv_pipeline *pipeline,
                                 const VkAllocationCallbacks *pAllocator,
                                 const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   struct v3dv_pipeline_stage *p_stage = pipeline->vs;

   p_stage = pipeline->fs;

   struct v3d_fs_key key;

   pipeline_populate_v3d_fs_key(&key, pCreateInfo, p_stage,
                                pipeline->gs != NULL,
                                get_ucp_enable_mask(pipeline->vs));

   VkResult vk_result;
   pipeline->shared_data->variants[BROADCOM_SHADER_FRAGMENT] =
      pipeline_compile_shader_variant(p_stage, &key.base, sizeof(key),
                                      pAllocator, &vk_result);

   return vk_result;
}

static void
pipeline_populate_graphics_key(struct v3dv_pipeline *pipeline,
                               struct v3dv_pipeline_key *key,
                               const VkGraphicsPipelineCreateInfo *pCreateInfo)
{
   memset(key, 0, sizeof(*key));
   key->robust_buffer_access =
      pipeline->device->features.robustBufferAccess;

   const bool raster_enabled =
      !pCreateInfo->pRasterizationState->rasterizerDiscardEnable;

   const VkPipelineInputAssemblyStateCreateInfo *ia_info =
      pCreateInfo->pInputAssemblyState;
   key->topology = vk_to_pipe_prim_type[ia_info->topology];

   const VkPipelineColorBlendStateCreateInfo *cb_info =
      raster_enabled ? pCreateInfo->pColorBlendState : NULL;

   key->logicop_func = cb_info && cb_info->logicOpEnable == VK_TRUE ?
      vk_to_pipe_logicop[cb_info->logicOp] :
      PIPE_LOGICOP_COPY;

   /* Multisample rasterization state must be ignored if rasterization
    * is disabled.
    */
   const VkPipelineMultisampleStateCreateInfo *ms_info =
      raster_enabled ? pCreateInfo->pMultisampleState : NULL;
   if (ms_info) {
      assert(ms_info->rasterizationSamples == VK_SAMPLE_COUNT_1_BIT ||
             ms_info->rasterizationSamples == VK_SAMPLE_COUNT_4_BIT);
      key->msaa = ms_info->rasterizationSamples > VK_SAMPLE_COUNT_1_BIT;

      if (key->msaa) {
         key->sample_coverage =
            pipeline->sample_mask != (1 << V3D_MAX_SAMPLES) - 1;
         key->sample_alpha_to_coverage = ms_info->alphaToCoverageEnable;
         key->sample_alpha_to_one = ms_info->alphaToOneEnable;
      }
   }

   const struct v3dv_render_pass *pass =
      v3dv_render_pass_from_handle(pCreateInfo->renderPass);
   const struct v3dv_subpass *subpass = pipeline->subpass;
   for (uint32_t i = 0; i < subpass->color_count; i++) {
      const uint32_t att_idx = subpass->color_attachments[i].attachment;
      if (att_idx == VK_ATTACHMENT_UNUSED)
         continue;

      key->cbufs |= 1 << i;

      VkFormat fb_format = pass->attachments[att_idx].desc.format;
      enum pipe_format fb_pipe_format = vk_format_to_pipe_format(fb_format);

      /* If logic operations are enabled then we might emit color reads and we
       * need to know the color buffer format and swizzle for that
       */
      if (key->logicop_func != PIPE_LOGICOP_COPY) {
         key->color_fmt[i].format = fb_pipe_format;
         key->color_fmt[i].swizzle = v3dv_get_format_swizzle(pipeline->device,
                                                             fb_format);
      }

      const struct util_format_description *desc =
         vk_format_description(fb_format);

      if (desc->channel[0].type == UTIL_FORMAT_TYPE_FLOAT &&
          desc->channel[0].size == 32) {
         key->f32_color_rb |= 1 << i;
      }
   }

   const VkPipelineVertexInputStateCreateInfo *vi_info =
      pCreateInfo->pVertexInputState;
   for (uint32_t i = 0; i < vi_info->vertexAttributeDescriptionCount; i++) {
      const VkVertexInputAttributeDescription *desc =
         &vi_info->pVertexAttributeDescriptions[i];
      assert(desc->location < MAX_VERTEX_ATTRIBS);
      if (desc->format == VK_FORMAT_B8G8R8A8_UNORM)
         key->va_swap_rb_mask |= 1 << (VERT_ATTRIB_GENERIC0 + desc->location);
   }

   assert(pipeline->subpass);
   key->has_multiview = pipeline->subpass->view_mask != 0;
}

static void
pipeline_populate_compute_key(struct v3dv_pipeline *pipeline,
                              struct v3dv_pipeline_key *key,
                              const VkComputePipelineCreateInfo *pCreateInfo)
{
   /* We use the same pipeline key for graphics and compute, but we don't need
    * to add a field to flag compute keys because this key is not used alone
    * to search in the cache, we also use the SPIR-V or the serialized NIR for
    * example, which already flags compute shaders.
    */
   memset(key, 0, sizeof(*key));
   key->robust_buffer_access =
      pipeline->device->features.robustBufferAccess;
}

static struct v3dv_pipeline_shared_data *
v3dv_pipeline_shared_data_new_empty(const unsigned char sha1_key[20],
                                    struct v3dv_pipeline *pipeline,
                                    bool is_graphics_pipeline)
{
   /* We create new_entry using the device alloc. Right now shared_data is ref
    * and unref by both the pipeline and the pipeline cache, so we can't
    * ensure that the cache or pipeline alloc will be available on the last
    * unref.
    */
   struct v3dv_pipeline_shared_data *new_entry =
      vk_zalloc2(&pipeline->device->vk.alloc, NULL,
                 sizeof(struct v3dv_pipeline_shared_data), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (new_entry == NULL)
      return NULL;

   for (uint8_t stage = 0; stage < BROADCOM_SHADER_STAGES; stage++) {
      /* We don't need specific descriptor maps for binning stages we use the
       * map for the render stage.
       */
      if (broadcom_shader_stage_is_binning(stage))
         continue;

      if ((is_graphics_pipeline && stage == BROADCOM_SHADER_COMPUTE) ||
          (!is_graphics_pipeline && stage != BROADCOM_SHADER_COMPUTE)) {
         continue;
      }

      if (stage == BROADCOM_SHADER_GEOMETRY && !pipeline->gs) {
         /* We always inject a custom GS if we have multiview */
         if (!pipeline->subpass->view_mask)
            continue;
      }

      struct v3dv_descriptor_maps *new_maps =
         vk_zalloc2(&pipeline->device->vk.alloc, NULL,
                    sizeof(struct v3dv_descriptor_maps), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

      if (new_maps == NULL)
         goto fail;

      new_entry->maps[stage] = new_maps;
   }

   new_entry->maps[BROADCOM_SHADER_VERTEX_BIN] =
      new_entry->maps[BROADCOM_SHADER_VERTEX];

   new_entry->maps[BROADCOM_SHADER_GEOMETRY_BIN] =
      new_entry->maps[BROADCOM_SHADER_GEOMETRY];

   new_entry->ref_cnt = 1;
   memcpy(new_entry->sha1_key, sha1_key, 20);

   return new_entry;

fail:
   if (new_entry != NULL) {
      for (uint8_t stage = 0; stage < BROADCOM_SHADER_STAGES; stage++) {
         if (new_entry->maps[stage] != NULL)
            vk_free(&pipeline->device->vk.alloc, new_entry->maps[stage]);
      }
   }

   vk_free(&pipeline->device->vk.alloc, new_entry);

   return NULL;
}

static void
write_creation_feedback(struct v3dv_pipeline *pipeline,
                        const void *next,
                        const VkPipelineCreationFeedbackEXT *pipeline_feedback,
                        uint32_t stage_count,
                        const VkPipelineShaderStageCreateInfo *stages)
{
   const VkPipelineCreationFeedbackCreateInfoEXT *create_feedback =
      vk_find_struct_const(next, PIPELINE_CREATION_FEEDBACK_CREATE_INFO_EXT);

   if (create_feedback) {
      typed_memcpy(create_feedback->pPipelineCreationFeedback,
             pipeline_feedback,
             1);

      assert(stage_count == create_feedback->pipelineStageCreationFeedbackCount);

      for (uint32_t i = 0; i < stage_count; i++) {
         gl_shader_stage s = vk_to_mesa_shader_stage(stages[i].stage);
         switch (s) {
         case MESA_SHADER_VERTEX:
            create_feedback->pPipelineStageCreationFeedbacks[i] =
               pipeline->vs->feedback;

            create_feedback->pPipelineStageCreationFeedbacks[i].duration +=
               pipeline->vs_bin->feedback.duration;
            break;

         case MESA_SHADER_GEOMETRY:
            create_feedback->pPipelineStageCreationFeedbacks[i] =
               pipeline->gs->feedback;

            create_feedback->pPipelineStageCreationFeedbacks[i].duration +=
               pipeline->gs_bin->feedback.duration;
            break;

         case MESA_SHADER_FRAGMENT:
            create_feedback->pPipelineStageCreationFeedbacks[i] =
               pipeline->fs->feedback;
            break;

         case MESA_SHADER_COMPUTE:
            create_feedback->pPipelineStageCreationFeedbacks[i] =
               pipeline->cs->feedback;
            break;

         default:
            unreachable("not supported shader stage");
         }
      }
   }
}

static uint32_t
multiview_gs_input_primitive_from_pipeline(struct v3dv_pipeline *pipeline)
{
   switch (pipeline->topology) {
   case PIPE_PRIM_POINTS:
      return GL_POINTS;
   case PIPE_PRIM_LINES:
   case PIPE_PRIM_LINE_STRIP:
      return GL_LINES;
   case PIPE_PRIM_TRIANGLES:
   case PIPE_PRIM_TRIANGLE_STRIP:
   case PIPE_PRIM_TRIANGLE_FAN:
      return GL_TRIANGLES;
   default:
      /* Since we don't allow GS with multiview, we can only see non-adjacency
       * primitives.
       */
      unreachable("Unexpected pipeline primitive type");
   }
}

static uint32_t
multiview_gs_output_primitive_from_pipeline(struct v3dv_pipeline *pipeline)
{
   switch (pipeline->topology) {
   case PIPE_PRIM_POINTS:
      return GL_POINTS;
   case PIPE_PRIM_LINES:
   case PIPE_PRIM_LINE_STRIP:
      return GL_LINE_STRIP;
   case PIPE_PRIM_TRIANGLES:
   case PIPE_PRIM_TRIANGLE_STRIP:
   case PIPE_PRIM_TRIANGLE_FAN:
      return GL_TRIANGLE_STRIP;
   default:
      /* Since we don't allow GS with multiview, we can only see non-adjacency
       * primitives.
       */
      unreachable("Unexpected pipeline primitive type");
   }
}

static bool
pipeline_add_multiview_gs(struct v3dv_pipeline *pipeline,
                          struct v3dv_pipeline_cache *cache,
                          const VkAllocationCallbacks *pAllocator)
{
   /* Create the passthrough GS from the VS output interface */
   pipeline->vs->nir = pipeline_stage_get_nir(pipeline->vs, pipeline, cache);
   nir_shader *vs_nir = pipeline->vs->nir;

   const nir_shader_compiler_options *options = v3dv_pipeline_get_nir_options();
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_GEOMETRY, options,
                                                  "multiview broadcast gs");
   nir_shader *nir = b.shader;
   nir->info.inputs_read = vs_nir->info.outputs_written;
   nir->info.outputs_written = vs_nir->info.outputs_written |
                               (1ull << VARYING_SLOT_LAYER);

   uint32_t vertex_count = u_vertices_per_prim(pipeline->topology);
   nir->info.gs.input_primitive =
      multiview_gs_input_primitive_from_pipeline(pipeline);
   nir->info.gs.output_primitive =
      multiview_gs_output_primitive_from_pipeline(pipeline);
   nir->info.gs.vertices_in = vertex_count;
   nir->info.gs.vertices_out = nir->info.gs.vertices_in;
   nir->info.gs.invocations = 1;
   nir->info.gs.active_stream_mask = 0x1;

   /* Make a list of GS input/output variables from the VS outputs */
   nir_variable *in_vars[100];
   nir_variable *out_vars[100];
   uint32_t var_count = 0;
   nir_foreach_shader_out_variable(out_vs_var, vs_nir) {
      char name[8];
      snprintf(name, ARRAY_SIZE(name), "in_%d", var_count);

      in_vars[var_count] =
         nir_variable_create(nir, nir_var_shader_in,
                             glsl_array_type(out_vs_var->type, vertex_count, 0),
                             name);
      in_vars[var_count]->data.location = out_vs_var->data.location;
      in_vars[var_count]->data.location_frac = out_vs_var->data.location_frac;
      in_vars[var_count]->data.interpolation = out_vs_var->data.interpolation;

      snprintf(name, ARRAY_SIZE(name), "out_%d", var_count);
      out_vars[var_count] =
         nir_variable_create(nir, nir_var_shader_out, out_vs_var->type, name);
      out_vars[var_count]->data.location = out_vs_var->data.location;
      out_vars[var_count]->data.interpolation = out_vs_var->data.interpolation;

      var_count++;
   }

   /* Add the gl_Layer output variable */
   nir_variable *out_layer =
      nir_variable_create(nir, nir_var_shader_out, glsl_int_type(),
                          "out_Layer");
   out_layer->data.location = VARYING_SLOT_LAYER;

   /* Get the view index value that we will write to gl_Layer */
   nir_ssa_def *layer =
      nir_load_system_value(&b, nir_intrinsic_load_view_index, 0, 1, 32);

   /* Emit all output vertices */
   for (uint32_t vi = 0; vi < vertex_count; vi++) {
      /* Emit all output varyings */
      for (uint32_t i = 0; i < var_count; i++) {
         nir_deref_instr *in_value =
            nir_build_deref_array_imm(&b, nir_build_deref_var(&b, in_vars[i]), vi);
         nir_copy_deref(&b, nir_build_deref_var(&b, out_vars[i]), in_value);
      }

      /* Emit gl_Layer write */
      nir_store_var(&b, out_layer, layer, 0x1);

      nir_emit_vertex(&b, 0);
   }
   nir_end_primitive(&b, 0);

   /* Make sure we run our pre-process NIR passes so we produce NIR compatible
    * with what we expect from SPIR-V modules.
    */
   preprocess_nir(nir);

   /* Attach the geometry shader to the  pipeline */
   struct v3dv_device *device = pipeline->device;
   struct v3dv_physical_device *physical_device =
      &device->instance->physicalDevice;

   struct v3dv_pipeline_stage *p_stage =
      vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*p_stage), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (p_stage == NULL) {
      ralloc_free(nir);
      return false;
   }

   p_stage->pipeline = pipeline;
   p_stage->stage = BROADCOM_SHADER_GEOMETRY;
   p_stage->entrypoint = "main";
   p_stage->module = 0;
   p_stage->nir = nir;
   pipeline_compute_sha1_from_nir(p_stage->nir, p_stage->shader_sha1);
   p_stage->program_id = p_atomic_inc_return(&physical_device->next_program_id);

   pipeline->has_gs = true;
   pipeline->gs = p_stage;
   pipeline->active_stages |= MESA_SHADER_GEOMETRY;

   pipeline->gs_bin =
      pipeline_stage_create_binning(pipeline->gs, pAllocator);
      if (pipeline->gs_bin == NULL)
         return false;

   return true;
}

/*
 * It compiles a pipeline. Note that it also allocate internal object, but if
 * some allocations success, but other fails, the method is not freeing the
 * successful ones.
 *
 * This is done to simplify the code, as what we do in this case is just call
 * the pipeline destroy method, and this would handle freeing the internal
 * objects allocated. We just need to be careful setting to NULL the objects
 * not allocated.
 */
static VkResult
pipeline_compile_graphics(struct v3dv_pipeline *pipeline,
                          struct v3dv_pipeline_cache *cache,
                          const VkGraphicsPipelineCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator)
{
   VkPipelineCreationFeedbackEXT pipeline_feedback = {
      .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT,
   };
   int64_t pipeline_start = os_time_get_nano();

   struct v3dv_device *device = pipeline->device;
   struct v3dv_physical_device *physical_device =
      &device->instance->physicalDevice;

   /* First pass to get some common info from the shader, and create the
    * individual pipeline_stage objects
    */
   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      const VkPipelineShaderStageCreateInfo *sinfo = &pCreateInfo->pStages[i];
      gl_shader_stage stage = vk_to_mesa_shader_stage(sinfo->stage);

      struct v3dv_pipeline_stage *p_stage =
         vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*p_stage), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

      if (p_stage == NULL)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      /* Note that we are assigning program_id slightly differently that
       * v3d. Here we are assigning one per pipeline stage, so vs and vs_bin
       * would have a different program_id, while v3d would have the same for
       * both. For the case of v3dv, it is more natural to have an id this way,
       * as right now we are using it for debugging, not for shader-db.
       */
      p_stage->program_id =
         p_atomic_inc_return(&physical_device->next_program_id);

      p_stage->pipeline = pipeline;
      p_stage->stage = gl_shader_stage_to_broadcom(stage);
      p_stage->entrypoint = sinfo->pName;
      p_stage->module = vk_shader_module_from_handle(sinfo->module);
      p_stage->spec_info = sinfo->pSpecializationInfo;

      pipeline_hash_shader(p_stage->module,
                           p_stage->entrypoint,
                           stage,
                           p_stage->spec_info,
                           p_stage->shader_sha1);

      pipeline->active_stages |= sinfo->stage;

      /* We will try to get directly the compiled shader variant, so let's not
       * worry about getting the nir shader for now.
       */
      p_stage->nir = NULL;

      switch(stage) {
      case MESA_SHADER_VERTEX:
         pipeline->vs = p_stage;
         pipeline->vs_bin =
            pipeline_stage_create_binning(pipeline->vs, pAllocator);
         if (pipeline->vs_bin == NULL)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         break;

      case MESA_SHADER_GEOMETRY:
         pipeline->has_gs = true;
         pipeline->gs = p_stage;
         pipeline->gs_bin =
            pipeline_stage_create_binning(pipeline->gs, pAllocator);
         if (pipeline->gs_bin == NULL)
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         break;

      case MESA_SHADER_FRAGMENT:
         pipeline->fs = p_stage;
         break;

      default:
         unreachable("not supported shader stage");
      }
   }

   /* Add a no-op fragment shader if needed */
   if (!pipeline->fs) {
      nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT,
                                                     &v3dv_nir_options,
                                                     "noop_fs");

      struct v3dv_pipeline_stage *p_stage =
         vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*p_stage), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

      if (p_stage == NULL)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      p_stage->pipeline = pipeline;
      p_stage->stage = BROADCOM_SHADER_FRAGMENT;
      p_stage->entrypoint = "main";
      p_stage->module = 0;
      p_stage->nir = b.shader;
      pipeline_compute_sha1_from_nir(p_stage->nir, p_stage->shader_sha1);
      p_stage->program_id =
         p_atomic_inc_return(&physical_device->next_program_id);

      pipeline->fs = p_stage;
      pipeline->active_stages |= MESA_SHADER_FRAGMENT;
   }

   /* If multiview is enabled, we inject a custom passthrough geometry shader
    * to broadcast draw calls to the appropriate views.
    */
   assert(!pipeline->subpass->view_mask || (!pipeline->has_gs && !pipeline->gs));
   if (pipeline->subpass->view_mask) {
      if (!pipeline_add_multiview_gs(pipeline, cache, pAllocator))
         return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   /* First we try to get the variants from the pipeline cache */
   struct v3dv_pipeline_key pipeline_key;
   pipeline_populate_graphics_key(pipeline, &pipeline_key, pCreateInfo);
   unsigned char pipeline_sha1[20];
   pipeline_hash_graphics(pipeline, &pipeline_key, pipeline_sha1);

   bool cache_hit = false;

   pipeline->shared_data =
      v3dv_pipeline_cache_search_for_pipeline(cache,
                                              pipeline_sha1,
                                              &cache_hit);

   if (pipeline->shared_data != NULL) {
      /* A correct pipeline must have at least a VS and FS */
      assert(pipeline->shared_data->variants[BROADCOM_SHADER_VERTEX]);
      assert(pipeline->shared_data->variants[BROADCOM_SHADER_VERTEX_BIN]);
      assert(pipeline->shared_data->variants[BROADCOM_SHADER_FRAGMENT]);
      assert(!pipeline->gs ||
             pipeline->shared_data->variants[BROADCOM_SHADER_GEOMETRY]);
      assert(!pipeline->gs ||
             pipeline->shared_data->variants[BROADCOM_SHADER_GEOMETRY_BIN]);

      if (cache_hit && cache != &pipeline->device->default_pipeline_cache)
         pipeline_feedback.flags |=
            VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT_EXT;

      goto success;
   }

   if (pCreateInfo->flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT_EXT)
      return VK_PIPELINE_COMPILE_REQUIRED_EXT;

   /* Otherwise we try to get the NIR shaders (either from the original SPIR-V
    * shader or the pipeline cache) and compile.
    */
   pipeline->shared_data =
      v3dv_pipeline_shared_data_new_empty(pipeline_sha1, pipeline, true);

   pipeline->vs->feedback.flags |=
      VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT;
   if (pipeline->gs)
      pipeline->gs->feedback.flags |=
         VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT;
   pipeline->fs->feedback.flags |=
      VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT;

   if (!pipeline->vs->nir)
      pipeline->vs->nir = pipeline_stage_get_nir(pipeline->vs, pipeline, cache);
   if (pipeline->gs && !pipeline->gs->nir)
      pipeline->gs->nir = pipeline_stage_get_nir(pipeline->gs, pipeline, cache);
   if (!pipeline->fs->nir)
      pipeline->fs->nir = pipeline_stage_get_nir(pipeline->fs, pipeline, cache);

   /* Linking + pipeline lowerings */
   if (pipeline->gs) {
      link_shaders(pipeline->gs->nir, pipeline->fs->nir);
      link_shaders(pipeline->vs->nir, pipeline->gs->nir);
   } else {
      link_shaders(pipeline->vs->nir, pipeline->fs->nir);
   }

   pipeline_lower_nir(pipeline, pipeline->fs, pipeline->layout);
   lower_fs_io(pipeline->fs->nir);

   if (pipeline->gs) {
      pipeline_lower_nir(pipeline, pipeline->gs, pipeline->layout);
      lower_gs_io(pipeline->gs->nir);
   }

   pipeline_lower_nir(pipeline, pipeline->vs, pipeline->layout);
   lower_vs_io(pipeline->vs->nir);

   /* Compiling to vir */
   VkResult vk_result;

   /* We should have got all the variants or no variants from the cache */
   assert(!pipeline->shared_data->variants[BROADCOM_SHADER_FRAGMENT]);
   vk_result = pipeline_compile_fragment_shader(pipeline, pAllocator, pCreateInfo);
   if (vk_result != VK_SUCCESS)
      return vk_result;

   assert(!pipeline->shared_data->variants[BROADCOM_SHADER_GEOMETRY] &&
          !pipeline->shared_data->variants[BROADCOM_SHADER_GEOMETRY_BIN]);

   if (pipeline->gs) {
      vk_result =
         pipeline_compile_geometry_shader(pipeline, pAllocator, pCreateInfo);
      if (vk_result != VK_SUCCESS)
         return vk_result;
   }

   assert(!pipeline->shared_data->variants[BROADCOM_SHADER_VERTEX] &&
          !pipeline->shared_data->variants[BROADCOM_SHADER_VERTEX_BIN]);

   vk_result = pipeline_compile_vertex_shader(pipeline, pAllocator, pCreateInfo);
   if (vk_result != VK_SUCCESS)
      return vk_result;

   if (!upload_assembly(pipeline))
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   v3dv_pipeline_cache_upload_pipeline(pipeline, cache);

 success:

   pipeline_feedback.duration = os_time_get_nano() - pipeline_start;
   write_creation_feedback(pipeline,
                           pCreateInfo->pNext,
                           &pipeline_feedback,
                           pCreateInfo->stageCount,
                           pCreateInfo->pStages);

   /* Since we have the variants in the pipeline shared data we can now free
    * the pipeline stages.
    */
   pipeline_free_stages(device, pipeline, pAllocator);

   pipeline_check_spill_size(pipeline);

   return compute_vpm_config(pipeline);
}

static VkResult
compute_vpm_config(struct v3dv_pipeline *pipeline)
{
   struct v3dv_shader_variant *vs_variant =
      pipeline->shared_data->variants[BROADCOM_SHADER_VERTEX];
   struct v3dv_shader_variant *vs_bin_variant =
      pipeline->shared_data->variants[BROADCOM_SHADER_VERTEX];
   struct v3d_vs_prog_data *vs = vs_variant->prog_data.vs;
   struct v3d_vs_prog_data *vs_bin =vs_bin_variant->prog_data.vs;

   struct v3d_gs_prog_data *gs = NULL;
   struct v3d_gs_prog_data *gs_bin = NULL;
   if (pipeline->has_gs) {
      struct v3dv_shader_variant *gs_variant =
         pipeline->shared_data->variants[BROADCOM_SHADER_GEOMETRY];
      struct v3dv_shader_variant *gs_bin_variant =
         pipeline->shared_data->variants[BROADCOM_SHADER_GEOMETRY_BIN];
      gs = gs_variant->prog_data.gs;
      gs_bin = gs_bin_variant->prog_data.gs;
   }

   if (!v3d_compute_vpm_config(&pipeline->device->devinfo,
                               vs_bin, vs, gs_bin, gs,
                               &pipeline->vpm_cfg_bin,
                               &pipeline->vpm_cfg)) {
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }

   return VK_SUCCESS;
}

static unsigned
v3dv_dynamic_state_mask(VkDynamicState state)
{
   switch(state) {
   case VK_DYNAMIC_STATE_VIEWPORT:
      return V3DV_DYNAMIC_VIEWPORT;
   case VK_DYNAMIC_STATE_SCISSOR:
      return V3DV_DYNAMIC_SCISSOR;
   case VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK:
      return V3DV_DYNAMIC_STENCIL_COMPARE_MASK;
   case VK_DYNAMIC_STATE_STENCIL_WRITE_MASK:
      return V3DV_DYNAMIC_STENCIL_WRITE_MASK;
   case VK_DYNAMIC_STATE_STENCIL_REFERENCE:
      return V3DV_DYNAMIC_STENCIL_REFERENCE;
   case VK_DYNAMIC_STATE_BLEND_CONSTANTS:
      return V3DV_DYNAMIC_BLEND_CONSTANTS;
   case VK_DYNAMIC_STATE_DEPTH_BIAS:
      return V3DV_DYNAMIC_DEPTH_BIAS;
   case VK_DYNAMIC_STATE_LINE_WIDTH:
      return V3DV_DYNAMIC_LINE_WIDTH;
   case VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT:
      return V3DV_DYNAMIC_COLOR_WRITE_ENABLE;

   /* Depth bounds testing is not available in in V3D 4.2 so here we are just
    * ignoring this dynamic state. We are already asserting at pipeline creation
    * time that depth bounds testing is not enabled.
    */
   case VK_DYNAMIC_STATE_DEPTH_BOUNDS:
      return 0;

   default:
      unreachable("Unhandled dynamic state");
   }
}

static void
pipeline_init_dynamic_state(
   struct v3dv_pipeline *pipeline,
   const VkPipelineDynamicStateCreateInfo *pDynamicState,
   const VkPipelineViewportStateCreateInfo *pViewportState,
   const VkPipelineDepthStencilStateCreateInfo *pDepthStencilState,
   const VkPipelineColorBlendStateCreateInfo *pColorBlendState,
   const VkPipelineRasterizationStateCreateInfo *pRasterizationState,
   const VkPipelineColorWriteCreateInfoEXT *pColorWriteState)
{
   pipeline->dynamic_state = default_dynamic_state;
   struct v3dv_dynamic_state *dynamic = &pipeline->dynamic_state;

   /* Create a mask of enabled dynamic states */
   uint32_t dynamic_states = 0;
   if (pDynamicState) {
      uint32_t count = pDynamicState->dynamicStateCount;
      for (uint32_t s = 0; s < count; s++) {
         dynamic_states |=
            v3dv_dynamic_state_mask(pDynamicState->pDynamicStates[s]);
      }
   }

   /* For any pipeline states that are not dynamic, set the dynamic state
    * from the static pipeline state.
    */
   if (pViewportState) {
      if (!(dynamic_states & V3DV_DYNAMIC_VIEWPORT)) {
         dynamic->viewport.count = pViewportState->viewportCount;
         typed_memcpy(dynamic->viewport.viewports, pViewportState->pViewports,
                      pViewportState->viewportCount);

         for (uint32_t i = 0; i < dynamic->viewport.count; i++) {
            v3dv_viewport_compute_xform(&dynamic->viewport.viewports[i],
                                        dynamic->viewport.scale[i],
                                        dynamic->viewport.translate[i]);
         }
      }

      if (!(dynamic_states & V3DV_DYNAMIC_SCISSOR)) {
         dynamic->scissor.count = pViewportState->scissorCount;
         typed_memcpy(dynamic->scissor.scissors, pViewportState->pScissors,
                      pViewportState->scissorCount);
      }
   }

   if (pDepthStencilState) {
      if (!(dynamic_states & V3DV_DYNAMIC_STENCIL_COMPARE_MASK)) {
         dynamic->stencil_compare_mask.front =
            pDepthStencilState->front.compareMask;
         dynamic->stencil_compare_mask.back =
            pDepthStencilState->back.compareMask;
      }

      if (!(dynamic_states & V3DV_DYNAMIC_STENCIL_WRITE_MASK)) {
         dynamic->stencil_write_mask.front = pDepthStencilState->front.writeMask;
         dynamic->stencil_write_mask.back = pDepthStencilState->back.writeMask;
      }

      if (!(dynamic_states & V3DV_DYNAMIC_STENCIL_REFERENCE)) {
         dynamic->stencil_reference.front = pDepthStencilState->front.reference;
         dynamic->stencil_reference.back = pDepthStencilState->back.reference;
      }
   }

   if (pColorBlendState && !(dynamic_states & V3DV_DYNAMIC_BLEND_CONSTANTS)) {
      memcpy(dynamic->blend_constants, pColorBlendState->blendConstants,
             sizeof(dynamic->blend_constants));
   }

   if (pRasterizationState) {
      if (pRasterizationState->depthBiasEnable &&
          !(dynamic_states & V3DV_DYNAMIC_DEPTH_BIAS)) {
         dynamic->depth_bias.constant_factor =
            pRasterizationState->depthBiasConstantFactor;
         dynamic->depth_bias.depth_bias_clamp =
            pRasterizationState->depthBiasClamp;
         dynamic->depth_bias.slope_factor =
            pRasterizationState->depthBiasSlopeFactor;
      }
      if (!(dynamic_states & V3DV_DYNAMIC_LINE_WIDTH))
         dynamic->line_width = pRasterizationState->lineWidth;
   }

   if (pColorWriteState && !(dynamic_states & V3DV_DYNAMIC_COLOR_WRITE_ENABLE)) {
      dynamic->color_write_enable = 0;
      for (uint32_t i = 0; i < pColorWriteState->attachmentCount; i++)
         dynamic->color_write_enable |= pColorWriteState->pColorWriteEnables[i] ? (0xfu << (i * 4)) : 0;
   }

   pipeline->dynamic_state.mask = dynamic_states;
}

static bool
stencil_op_is_no_op(const VkStencilOpState *stencil)
{
   return stencil->depthFailOp == VK_STENCIL_OP_KEEP &&
          stencil->compareOp == VK_COMPARE_OP_ALWAYS;
}

static void
enable_depth_bias(struct v3dv_pipeline *pipeline,
                  const VkPipelineRasterizationStateCreateInfo *rs_info)
{
   pipeline->depth_bias.enabled = false;
   pipeline->depth_bias.is_z16 = false;

   if (!rs_info || !rs_info->depthBiasEnable)
      return;

   /* Check the depth/stencil attachment description for the subpass used with
    * this pipeline.
    */
   assert(pipeline->pass && pipeline->subpass);
   struct v3dv_render_pass *pass = pipeline->pass;
   struct v3dv_subpass *subpass = pipeline->subpass;

   if (subpass->ds_attachment.attachment == VK_ATTACHMENT_UNUSED)
      return;

   assert(subpass->ds_attachment.attachment < pass->attachment_count);
   struct v3dv_render_pass_attachment *att =
      &pass->attachments[subpass->ds_attachment.attachment];

   if (att->desc.format == VK_FORMAT_D16_UNORM)
      pipeline->depth_bias.is_z16 = true;

   pipeline->depth_bias.enabled = true;
}

static void
pipeline_set_ez_state(struct v3dv_pipeline *pipeline,
                      const VkPipelineDepthStencilStateCreateInfo *ds_info)
{
   if (!ds_info || !ds_info->depthTestEnable) {
      pipeline->ez_state = V3D_EZ_DISABLED;
      return;
   }

   switch (ds_info->depthCompareOp) {
   case VK_COMPARE_OP_LESS:
   case VK_COMPARE_OP_LESS_OR_EQUAL:
      pipeline->ez_state = V3D_EZ_LT_LE;
      break;
   case VK_COMPARE_OP_GREATER:
   case VK_COMPARE_OP_GREATER_OR_EQUAL:
      pipeline->ez_state = V3D_EZ_GT_GE;
      break;
   case VK_COMPARE_OP_NEVER:
   case VK_COMPARE_OP_EQUAL:
      pipeline->ez_state = V3D_EZ_UNDECIDED;
      break;
   default:
      pipeline->ez_state = V3D_EZ_DISABLED;
      break;
   }

   /* If stencil is enabled and is not a no-op, we need to disable EZ */
   if (ds_info->stencilTestEnable &&
       (!stencil_op_is_no_op(&ds_info->front) ||
        !stencil_op_is_no_op(&ds_info->back))) {
         pipeline->ez_state = V3D_EZ_DISABLED;
   }
}

static bool
pipeline_has_integer_vertex_attrib(struct v3dv_pipeline *pipeline)
{
   for (uint8_t i = 0; i < pipeline->va_count; i++) {
      if (vk_format_is_int(pipeline->va[i].vk_format))
         return true;
   }
   return false;
}

/* @pipeline can be NULL. We assume in that case that all the attributes have
 * a float format (we only create an all-float BO once and we reuse it with
 * all float pipelines), otherwise we look at the actual type of each
 * attribute used with the specific pipeline passed in.
 */
struct v3dv_bo *
v3dv_pipeline_create_default_attribute_values(struct v3dv_device *device,
                                              struct v3dv_pipeline *pipeline)
{
   uint32_t size = MAX_VERTEX_ATTRIBS * sizeof(float) * 4;
   struct v3dv_bo *bo;

   bo = v3dv_bo_alloc(device, size, "default_vi_attributes", true);

   if (!bo) {
      fprintf(stderr, "failed to allocate memory for the default "
              "attribute values\n");
      return NULL;
   }

   bool ok = v3dv_bo_map(device, bo, size);
   if (!ok) {
      fprintf(stderr, "failed to map default attribute values buffer\n");
      return false;
   }

   uint32_t *attrs = bo->map;
   uint8_t va_count = pipeline != NULL ? pipeline->va_count : 0;
   for (int i = 0; i < MAX_VERTEX_ATTRIBS; i++) {
      attrs[i * 4 + 0] = 0;
      attrs[i * 4 + 1] = 0;
      attrs[i * 4 + 2] = 0;
      VkFormat attr_format =
         pipeline != NULL ? pipeline->va[i].vk_format : VK_FORMAT_UNDEFINED;
      if (i < va_count && vk_format_is_int(attr_format)) {
         attrs[i * 4 + 3] = 1;
      } else {
         attrs[i * 4 + 3] = fui(1.0);
      }
   }

   v3dv_bo_unmap(device, bo);

   return bo;
}

static void
pipeline_set_sample_mask(struct v3dv_pipeline *pipeline,
                         const VkPipelineMultisampleStateCreateInfo *ms_info)
{
   pipeline->sample_mask = (1 << V3D_MAX_SAMPLES) - 1;

   /* Ignore pSampleMask if we are not enabling multisampling. The hardware
    * requires this to be 0xf or 0x0 if using a single sample.
    */
   if (ms_info && ms_info->pSampleMask &&
       ms_info->rasterizationSamples > VK_SAMPLE_COUNT_1_BIT) {
      pipeline->sample_mask &= ms_info->pSampleMask[0];
   }
}

static void
pipeline_set_sample_rate_shading(struct v3dv_pipeline *pipeline,
                                 const VkPipelineMultisampleStateCreateInfo *ms_info)
{
   pipeline->sample_rate_shading =
      ms_info && ms_info->rasterizationSamples > VK_SAMPLE_COUNT_1_BIT &&
      ms_info->sampleShadingEnable;
}

static VkResult
pipeline_init(struct v3dv_pipeline *pipeline,
              struct v3dv_device *device,
              struct v3dv_pipeline_cache *cache,
              const VkGraphicsPipelineCreateInfo *pCreateInfo,
              const VkAllocationCallbacks *pAllocator)
{
   VkResult result = VK_SUCCESS;

   pipeline->device = device;

   V3DV_FROM_HANDLE(v3dv_pipeline_layout, layout, pCreateInfo->layout);
   pipeline->layout = layout;

   V3DV_FROM_HANDLE(v3dv_render_pass, render_pass, pCreateInfo->renderPass);
   assert(pCreateInfo->subpass < render_pass->subpass_count);
   pipeline->pass = render_pass;
   pipeline->subpass = &render_pass->subpasses[pCreateInfo->subpass];

   const VkPipelineInputAssemblyStateCreateInfo *ia_info =
      pCreateInfo->pInputAssemblyState;
   pipeline->topology = vk_to_pipe_prim_type[ia_info->topology];

   /* If rasterization is not enabled, various CreateInfo structs must be
    * ignored.
    */
   const bool raster_enabled =
      !pCreateInfo->pRasterizationState->rasterizerDiscardEnable;

   const VkPipelineViewportStateCreateInfo *vp_info =
      raster_enabled ? pCreateInfo->pViewportState : NULL;

   const VkPipelineDepthStencilStateCreateInfo *ds_info =
      raster_enabled ? pCreateInfo->pDepthStencilState : NULL;

   const VkPipelineRasterizationStateCreateInfo *rs_info =
      raster_enabled ? pCreateInfo->pRasterizationState : NULL;

   const VkPipelineRasterizationProvokingVertexStateCreateInfoEXT *pv_info =
      rs_info ? vk_find_struct_const(
         rs_info->pNext,
         PIPELINE_RASTERIZATION_PROVOKING_VERTEX_STATE_CREATE_INFO_EXT) :
            NULL;

   const VkPipelineColorBlendStateCreateInfo *cb_info =
      raster_enabled ? pCreateInfo->pColorBlendState : NULL;

   const VkPipelineMultisampleStateCreateInfo *ms_info =
      raster_enabled ? pCreateInfo->pMultisampleState : NULL;

   const VkPipelineColorWriteCreateInfoEXT *cw_info =
      cb_info ? vk_find_struct_const(cb_info->pNext,
                                     PIPELINE_COLOR_WRITE_CREATE_INFO_EXT) :
                NULL;

   pipeline_init_dynamic_state(pipeline,
                               pCreateInfo->pDynamicState,
                               vp_info, ds_info, cb_info, rs_info, cw_info);

   /* V3D 4.2 doesn't support depth bounds testing so we don't advertise that
    * feature and it shouldn't be used by any pipeline.
    */
   assert(!ds_info || !ds_info->depthBoundsTestEnable);

   v3dv_X(device, pipeline_pack_state)(pipeline, cb_info, ds_info,
                                       rs_info, pv_info, ms_info);

   pipeline_set_ez_state(pipeline, ds_info);
   enable_depth_bias(pipeline, rs_info);
   pipeline_set_sample_mask(pipeline, ms_info);
   pipeline_set_sample_rate_shading(pipeline, ms_info);

   pipeline->primitive_restart =
      pCreateInfo->pInputAssemblyState->primitiveRestartEnable;

   result = pipeline_compile_graphics(pipeline, cache, pCreateInfo, pAllocator);

   if (result != VK_SUCCESS) {
      /* Caller would already destroy the pipeline, and we didn't allocate any
       * extra info. We don't need to do anything else.
       */
      return result;
   }

   const VkPipelineVertexInputStateCreateInfo *vi_info =
      pCreateInfo->pVertexInputState;

   const VkPipelineVertexInputDivisorStateCreateInfoEXT *vd_info =
      vk_find_struct_const(vi_info->pNext,
                           PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT);

   v3dv_X(device, pipeline_pack_compile_state)(pipeline, vi_info, vd_info);

   if (pipeline_has_integer_vertex_attrib(pipeline)) {
      pipeline->default_attribute_values =
         v3dv_pipeline_create_default_attribute_values(pipeline->device, pipeline);
      if (!pipeline->default_attribute_values)
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   } else {
      pipeline->default_attribute_values = NULL;
   }

   return result;
}

static VkResult
graphics_pipeline_create(VkDevice _device,
                         VkPipelineCache _cache,
                         const VkGraphicsPipelineCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator,
                         VkPipeline *pPipeline)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_pipeline_cache, cache, _cache);

   struct v3dv_pipeline *pipeline;
   VkResult result;

   /* Use the default pipeline cache if none is specified */
   if (cache == NULL && device->instance->default_pipeline_cache_enabled)
      cache = &device->default_pipeline_cache;

   pipeline = vk_object_zalloc(&device->vk, pAllocator, sizeof(*pipeline),
                               VK_OBJECT_TYPE_PIPELINE);

   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = pipeline_init(pipeline, device, cache,
                          pCreateInfo,
                          pAllocator);

   if (result != VK_SUCCESS) {
      v3dv_destroy_pipeline(pipeline, device, pAllocator);
      if (result == VK_PIPELINE_COMPILE_REQUIRED_EXT)
         *pPipeline = VK_NULL_HANDLE;
      return result;
   }

   *pPipeline = v3dv_pipeline_to_handle(pipeline);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_CreateGraphicsPipelines(VkDevice _device,
                             VkPipelineCache pipelineCache,
                             uint32_t count,
                             const VkGraphicsPipelineCreateInfo *pCreateInfos,
                             const VkAllocationCallbacks *pAllocator,
                             VkPipeline *pPipelines)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   VkResult result = VK_SUCCESS;

   if (unlikely(V3D_DEBUG & V3D_DEBUG_SHADERS))
      mtx_lock(&device->pdevice->mutex);

   uint32_t i = 0;
   for (; i < count; i++) {
      VkResult local_result;

      local_result = graphics_pipeline_create(_device,
                                              pipelineCache,
                                              &pCreateInfos[i],
                                              pAllocator,
                                              &pPipelines[i]);

      if (local_result != VK_SUCCESS) {
         result = local_result;
         pPipelines[i] = VK_NULL_HANDLE;

         if (pCreateInfos[i].flags &
             VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT_EXT)
            break;
      }
   }

   for (; i < count; i++)
      pPipelines[i] = VK_NULL_HANDLE;

   if (unlikely(V3D_DEBUG & V3D_DEBUG_SHADERS))
      mtx_unlock(&device->pdevice->mutex);

   return result;
}

static void
shared_type_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size = glsl_type_is_boolean(type)
      ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length,
   *align = comp_size * (length == 3 ? 4 : length);
}

static void
lower_cs_shared(struct nir_shader *nir)
{
   NIR_PASS_V(nir, nir_lower_vars_to_explicit_types,
              nir_var_mem_shared, shared_type_info);
   NIR_PASS_V(nir, nir_lower_explicit_io,
              nir_var_mem_shared, nir_address_format_32bit_offset);
}

static VkResult
pipeline_compile_compute(struct v3dv_pipeline *pipeline,
                         struct v3dv_pipeline_cache *cache,
                         const VkComputePipelineCreateInfo *info,
                         const VkAllocationCallbacks *alloc)
{
   VkPipelineCreationFeedbackEXT pipeline_feedback = {
      .flags = VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT,
   };
   int64_t pipeline_start = os_time_get_nano();

   struct v3dv_device *device = pipeline->device;
   struct v3dv_physical_device *physical_device =
      &device->instance->physicalDevice;

   const VkPipelineShaderStageCreateInfo *sinfo = &info->stage;
   gl_shader_stage stage = vk_to_mesa_shader_stage(sinfo->stage);

   struct v3dv_pipeline_stage *p_stage =
      vk_zalloc2(&device->vk.alloc, alloc, sizeof(*p_stage), 8,
                 VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!p_stage)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   p_stage->program_id = p_atomic_inc_return(&physical_device->next_program_id);
   p_stage->pipeline = pipeline;
   p_stage->stage = gl_shader_stage_to_broadcom(stage);
   p_stage->entrypoint = sinfo->pName;
   p_stage->module = vk_shader_module_from_handle(sinfo->module);
   p_stage->spec_info = sinfo->pSpecializationInfo;
   p_stage->feedback = (VkPipelineCreationFeedbackEXT) { 0 };

   pipeline_hash_shader(p_stage->module,
                        p_stage->entrypoint,
                        stage,
                        p_stage->spec_info,
                        p_stage->shader_sha1);

   /* We try to get directly the variant first from the cache */
   p_stage->nir = NULL;

   pipeline->cs = p_stage;
   pipeline->active_stages |= sinfo->stage;

   struct v3dv_pipeline_key pipeline_key;
   pipeline_populate_compute_key(pipeline, &pipeline_key, info);
   unsigned char pipeline_sha1[20];
   pipeline_hash_compute(pipeline, &pipeline_key, pipeline_sha1);

   bool cache_hit = false;
   pipeline->shared_data =
      v3dv_pipeline_cache_search_for_pipeline(cache, pipeline_sha1, &cache_hit);

   if (pipeline->shared_data != NULL) {
      assert(pipeline->shared_data->variants[BROADCOM_SHADER_COMPUTE]);
      if (cache_hit && cache != &pipeline->device->default_pipeline_cache)
         pipeline_feedback.flags |=
            VK_PIPELINE_CREATION_FEEDBACK_APPLICATION_PIPELINE_CACHE_HIT_BIT_EXT;

      goto success;
   }

   if (info->flags & VK_PIPELINE_CREATE_FAIL_ON_PIPELINE_COMPILE_REQUIRED_BIT_EXT)
      return VK_PIPELINE_COMPILE_REQUIRED_EXT;

   pipeline->shared_data = v3dv_pipeline_shared_data_new_empty(pipeline_sha1,
                                                               pipeline,
                                                               false);

   p_stage->feedback.flags |= VK_PIPELINE_CREATION_FEEDBACK_VALID_BIT_EXT;

   /* If not found on cache, compile it */
   p_stage->nir = pipeline_stage_get_nir(p_stage, pipeline, cache);
   assert(p_stage->nir);

   st_nir_opts(p_stage->nir);
   pipeline_lower_nir(pipeline, p_stage, pipeline->layout);
   lower_cs_shared(p_stage->nir);

   VkResult result = VK_SUCCESS;

   struct v3d_key key;
   memset(&key, 0, sizeof(key));
   pipeline_populate_v3d_key(&key, p_stage, 0,
                             pipeline->device->features.robustBufferAccess);
   pipeline->shared_data->variants[BROADCOM_SHADER_COMPUTE] =
      pipeline_compile_shader_variant(p_stage, &key, sizeof(key),
                                      alloc, &result);

   if (result != VK_SUCCESS)
      return result;

   if (!upload_assembly(pipeline))
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   v3dv_pipeline_cache_upload_pipeline(pipeline, cache);

success:

   pipeline_feedback.duration = os_time_get_nano() - pipeline_start;
   write_creation_feedback(pipeline,
                           info->pNext,
                           &pipeline_feedback,
                           1,
                           &info->stage);

   /* As we got the variants in pipeline->shared_data, after compiling we
    * don't need the pipeline_stages
    */
   pipeline_free_stages(device, pipeline, alloc);

   pipeline_check_spill_size(pipeline);

   return VK_SUCCESS;
}

static VkResult
compute_pipeline_init(struct v3dv_pipeline *pipeline,
                      struct v3dv_device *device,
                      struct v3dv_pipeline_cache *cache,
                      const VkComputePipelineCreateInfo *info,
                      const VkAllocationCallbacks *alloc)
{
   V3DV_FROM_HANDLE(v3dv_pipeline_layout, layout, info->layout);

   pipeline->device = device;
   pipeline->layout = layout;

   VkResult result = pipeline_compile_compute(pipeline, cache, info, alloc);

   return result;
}

static VkResult
compute_pipeline_create(VkDevice _device,
                         VkPipelineCache _cache,
                         const VkComputePipelineCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator,
                         VkPipeline *pPipeline)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_pipeline_cache, cache, _cache);

   struct v3dv_pipeline *pipeline;
   VkResult result;

   /* Use the default pipeline cache if none is specified */
   if (cache == NULL && device->instance->default_pipeline_cache_enabled)
      cache = &device->default_pipeline_cache;

   pipeline = vk_object_zalloc(&device->vk, pAllocator, sizeof(*pipeline),
                               VK_OBJECT_TYPE_PIPELINE);
   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = compute_pipeline_init(pipeline, device, cache,
                                  pCreateInfo, pAllocator);
   if (result != VK_SUCCESS) {
      v3dv_destroy_pipeline(pipeline, device, pAllocator);
      if (result == VK_PIPELINE_COMPILE_REQUIRED_EXT)
         *pPipeline = VK_NULL_HANDLE;
      return result;
   }

   *pPipeline = v3dv_pipeline_to_handle(pipeline);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_CreateComputePipelines(VkDevice _device,
                            VkPipelineCache pipelineCache,
                            uint32_t createInfoCount,
                            const VkComputePipelineCreateInfo *pCreateInfos,
                            const VkAllocationCallbacks *pAllocator,
                            VkPipeline *pPipelines)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   VkResult result = VK_SUCCESS;

   if (unlikely(V3D_DEBUG & V3D_DEBUG_SHADERS))
      mtx_lock(&device->pdevice->mutex);

   uint32_t i = 0;
   for (; i < createInfoCount; i++) {
      VkResult local_result;
      local_result = compute_pipeline_create(_device,
                                              pipelineCache,
                                              &pCreateInfos[i],
                                              pAllocator,
                                              &pPipelines[i]);

      if (local_result != VK_SUCCESS) {
         result = local_result;
         pPipelines[i] = VK_NULL_HANDLE;

         if (pCreateInfos[i].flags &
             VK_PIPELINE_CREATE_EARLY_RETURN_ON_FAILURE_BIT_EXT)
            break;
      }
   }

   for (; i < createInfoCount; i++)
      pPipelines[i] = VK_NULL_HANDLE;

   if (unlikely(V3D_DEBUG & V3D_DEBUG_SHADERS))
      mtx_unlock(&device->pdevice->mutex);

   return result;
}
