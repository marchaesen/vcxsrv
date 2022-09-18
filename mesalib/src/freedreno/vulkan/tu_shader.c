/*
 * Copyright © 2019 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "tu_shader.h"

#include "spirv/nir_spirv.h"
#include "util/mesa-sha1.h"
#include "nir/nir_xfb_info.h"
#include "nir/nir_vulkan.h"
#include "vk_util.h"

#include "ir3/ir3_nir.h"

#include "tu_device.h"
#include "tu_descriptor_set.h"
#include "tu_pipeline.h"

nir_shader *
tu_spirv_to_nir(struct tu_device *dev,
                void *mem_ctx,
                const VkPipelineShaderStageCreateInfo *stage_info,
                gl_shader_stage stage)
{
   /* TODO these are made-up */
   const struct spirv_to_nir_options spirv_options = {
      .ubo_addr_format = nir_address_format_vec2_index_32bit_offset,
      .ssbo_addr_format = nir_address_format_vec2_index_32bit_offset,

      /* Accessed via stg/ldg */
      .phys_ssbo_addr_format = nir_address_format_64bit_global,

      /* Accessed via the const register file */
      .push_const_addr_format = nir_address_format_logical,

      /* Accessed via ldl/stl */
      .shared_addr_format = nir_address_format_32bit_offset,

      /* Accessed via stg/ldg (not used with Vulkan?) */
      .global_addr_format = nir_address_format_64bit_global,

      /* Use 16-bit math for RelaxedPrecision ALU ops */
      .mediump_16bit_alu = true,

      /* ViewID is a sysval in geometry stages and an input in the FS */
      .view_index_is_input = stage == MESA_SHADER_FRAGMENT,
      .caps = {
         .transform_feedback = true,
         .tessellation = true,
         .draw_parameters = true,
         .image_read_without_format = true,
         .image_write_without_format = true,
         .variable_pointers = true,
         .stencil_export = true,
         .multiview = true,
         .shader_viewport_index_layer = true,
         .geometry_streams = true,
         .device_group = true,
         .descriptor_indexing = true,
         .descriptor_array_dynamic_indexing = true,
         .descriptor_array_non_uniform_indexing = true,
         .runtime_descriptor_array = true,
         .float_controls = true,
         .float16 = true,
         .int16 = true,
         .storage_16bit = dev->physical_device->info->a6xx.storage_16bit,
         .demote_to_helper_invocation = true,
         .vk_memory_model = true,
         .vk_memory_model_device_scope = true,
         .subgroup_basic = true,
         .subgroup_ballot = true,
         .subgroup_vote = true,
         .subgroup_quad = true,
         .subgroup_shuffle = true,
         .subgroup_arithmetic = true,
         .physical_storage_buffer_address = true,
      },
   };

   const nir_shader_compiler_options *nir_options =
      ir3_get_compiler_options(dev->compiler);

   struct vk_shader_module *module =
      vk_shader_module_from_handle(stage_info->module);

   nir_shader *nir;
   VkResult result = vk_shader_module_to_nir(&dev->vk, module,
                                             stage, stage_info->pName,
                                             stage_info->pSpecializationInfo,
                                             &spirv_options, nir_options,
                                             mem_ctx, &nir);
   if (result != VK_SUCCESS)
      return NULL;

   if (unlikely(dev->physical_device->instance->debug_flags & TU_DEBUG_NIR)) {
      fprintf(stderr, "translated nir:\n");
      nir_print_shader(nir, stderr);
   }

   const struct nir_lower_sysvals_to_varyings_options sysvals_to_varyings = {
      .point_coord = true,
   };
   NIR_PASS_V(nir, nir_lower_sysvals_to_varyings, &sysvals_to_varyings);

   NIR_PASS_V(nir, nir_lower_global_vars_to_local);

   /* Older glslang missing bf6efd0316d8 ("SPV: Fix #2293: keep relaxed
    * precision on arg passed to relaxed param") will pass function args through
    * a highp temporary, so we need the nir_opt_find_array_copies() and a copy
    * prop before we lower mediump vars, or you'll be unable to optimize out
    * array copies after lowering.  We do this before splitting copies, since
    * that works against nir_opt_find_array_copies().
    * */
   NIR_PASS_V(nir, nir_opt_find_array_copies);
   NIR_PASS_V(nir, nir_opt_copy_prop_vars);
   NIR_PASS_V(nir, nir_opt_dce);

   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_var_copies);

   NIR_PASS_V(nir, nir_lower_mediump_vars, nir_var_function_temp | nir_var_shader_temp | nir_var_mem_shared);
   NIR_PASS_V(nir, nir_opt_copy_prop_vars);
   NIR_PASS_V(nir, nir_opt_combine_stores, nir_var_all);

   NIR_PASS_V(nir, nir_lower_is_helper_invocation);

   NIR_PASS_V(nir, nir_lower_system_values);

   NIR_PASS_V(nir, nir_lower_frexp);

   ir3_optimize_loop(dev->compiler, nir);

   NIR_PASS_V(nir, nir_opt_conditional_discard);

   return nir;
}

static void
lower_load_push_constant(struct tu_device *dev,
                         nir_builder *b,
                         nir_intrinsic_instr *instr,
                         struct tu_shader *shader,
                         const struct tu_pipeline_layout *layout)
{
   uint32_t base = nir_intrinsic_base(instr);
   assert(base % 4 == 0);

   if (tu6_shared_constants_enable(layout, dev->compiler)) {
      /* All stages share the same range.  We could potentially add
       * push_constant_offset to layout and apply it, but this is good for
       * now.
       */
      base += dev->compiler->shared_consts_base_offset * 4;
   } else {
      assert(base >= shader->push_consts.lo * 4);
      base -= shader->push_consts.lo * 4;
   }

   nir_ssa_def *load =
      nir_load_uniform(b, instr->num_components,
            instr->dest.ssa.bit_size,
            nir_ushr(b, instr->src[0].ssa, nir_imm_int(b, 2)),
            .base = base);

   nir_ssa_def_rewrite_uses(&instr->dest.ssa, load);

   nir_instr_remove(&instr->instr);
}

static void
lower_vulkan_resource_index(nir_builder *b, nir_intrinsic_instr *instr,
                            struct tu_shader *shader,
                            const struct tu_pipeline_layout *layout)
{
   nir_ssa_def *vulkan_idx = instr->src[0].ssa;

   unsigned set = nir_intrinsic_desc_set(instr);
   unsigned binding = nir_intrinsic_binding(instr);
   struct tu_descriptor_set_layout *set_layout = layout->set[set].layout;
   struct tu_descriptor_set_binding_layout *binding_layout =
      &set_layout->binding[binding];
   uint32_t base;

   shader->active_desc_sets |= 1u << set;

   switch (binding_layout->type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      base = (layout->set[set].dynamic_offset_start +
         binding_layout->dynamic_offset_offset) / (4 * A6XX_TEX_CONST_DWORDS);
      set = MAX_SETS;
      break;
   default:
      base = binding_layout->offset / (4 * A6XX_TEX_CONST_DWORDS);
      break;
   }

   nir_ssa_def *shift;

   if (binding_layout->type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
      /* Inline uniform blocks cannot have arrays so the stride is unused */
      shift = nir_imm_int(b, 0);
   } else {
      unsigned stride = binding_layout->size / (4 * A6XX_TEX_CONST_DWORDS);
      assert(util_is_power_of_two_nonzero(stride));
      shift = nir_imm_int(b, util_logbase2(stride));
   }

   nir_ssa_def *def = nir_vec3(b, nir_imm_int(b, set),
                               nir_iadd(b, nir_imm_int(b, base),
                                        nir_ishl(b, vulkan_idx, shift)),
                               shift);

   nir_ssa_def_rewrite_uses(&instr->dest.ssa, def);
   nir_instr_remove(&instr->instr);
}

static void
lower_vulkan_resource_reindex(nir_builder *b, nir_intrinsic_instr *instr)
{
   nir_ssa_def *old_index = instr->src[0].ssa;
   nir_ssa_def *delta = instr->src[1].ssa;
   nir_ssa_def *shift = nir_channel(b, old_index, 2);

   nir_ssa_def *new_index =
      nir_vec3(b, nir_channel(b, old_index, 0),
               nir_iadd(b, nir_channel(b, old_index, 1),
                        nir_ishl(b, delta, shift)),
               shift);

   nir_ssa_def_rewrite_uses(&instr->dest.ssa, new_index);
   nir_instr_remove(&instr->instr);
}

static void
lower_load_vulkan_descriptor(nir_builder *b, nir_intrinsic_instr *intrin)
{
   nir_ssa_def *old_index = intrin->src[0].ssa;
   /* Loading the descriptor happens as part of the load/store instruction so
    * this is a no-op. We just need to turn the shift into an offset of 0.
    */
   nir_ssa_def *new_index =
      nir_vec3(b, nir_channel(b, old_index, 0),
               nir_channel(b, old_index, 1),
               nir_imm_int(b, 0));
   nir_ssa_def_rewrite_uses(&intrin->dest.ssa, new_index);
   nir_instr_remove(&intrin->instr);
}

static void
lower_ssbo_ubo_intrinsic(struct tu_device *dev,
                         nir_builder *b, nir_intrinsic_instr *intrin)
{
   const nir_intrinsic_info *info = &nir_intrinsic_infos[intrin->intrinsic];

   /* The bindless base is part of the instruction, which means that part of
    * the "pointer" has to be constant. We solve this in the same way the blob
    * does, by generating a bunch of if-statements. In the usual case where
    * the descriptor set is constant we can skip that, though).
    */

   unsigned buffer_src;
   if (intrin->intrinsic == nir_intrinsic_store_ssbo) {
      /* This has the value first */
      buffer_src = 1;
   } else {
      buffer_src = 0;
   }

   nir_ssa_scalar scalar_idx = nir_ssa_scalar_resolved(intrin->src[buffer_src].ssa, 0);
   nir_ssa_def *descriptor_idx = nir_channel(b, intrin->src[buffer_src].ssa, 1);

   /* For isam, we need to use the appropriate descriptor if 16-bit storage is
    * enabled. Descriptor 0 is the 16-bit one, descriptor 1 is the 32-bit one.
    */
   if (dev->physical_device->info->a6xx.storage_16bit &&
       intrin->intrinsic == nir_intrinsic_load_ssbo &&
       (nir_intrinsic_access(intrin) & ACCESS_CAN_REORDER) &&
       intrin->dest.ssa.bit_size > 16) {
      descriptor_idx = nir_iadd(b, descriptor_idx, nir_imm_int(b, 1));
   }

   nir_ssa_def *results[MAX_SETS + 1] = { NULL };

   if (nir_ssa_scalar_is_const(scalar_idx)) {
      nir_ssa_def *bindless =
         nir_bindless_resource_ir3(b, 32, descriptor_idx, .desc_set = nir_ssa_scalar_as_uint(scalar_idx));
      nir_instr_rewrite_src_ssa(&intrin->instr, &intrin->src[buffer_src], bindless);
      return;
   }

   nir_ssa_def *base_idx = nir_channel(b, scalar_idx.def, scalar_idx.comp);
   for (unsigned i = 0; i < MAX_SETS + 1; i++) {
      /* if (base_idx == i) { ... */
      nir_if *nif = nir_push_if(b, nir_ieq_imm(b, base_idx, i));

      nir_ssa_def *bindless =
         nir_bindless_resource_ir3(b, 32, descriptor_idx, .desc_set = i);

      nir_intrinsic_instr *copy =
         nir_intrinsic_instr_create(b->shader, intrin->intrinsic);

      copy->num_components = intrin->num_components;

      for (unsigned src = 0; src < info->num_srcs; src++) {
         if (src == buffer_src)
            copy->src[src] = nir_src_for_ssa(bindless);
         else
            copy->src[src] = nir_src_for_ssa(intrin->src[src].ssa);
      }

      for (unsigned idx = 0; idx < info->num_indices; idx++) {
         copy->const_index[idx] = intrin->const_index[idx];
      }

      if (info->has_dest) {
         nir_ssa_dest_init(&copy->instr, &copy->dest,
                           intrin->dest.ssa.num_components,
                           intrin->dest.ssa.bit_size,
                           NULL);
         results[i] = &copy->dest.ssa;
      }

      nir_builder_instr_insert(b, &copy->instr);

      /* } else { ... */
      nir_push_else(b, nif);
   }

   nir_ssa_def *result =
      nir_ssa_undef(b, intrin->dest.ssa.num_components, intrin->dest.ssa.bit_size);
   for (int i = MAX_SETS; i >= 0; i--) {
      nir_pop_if(b, NULL);
      if (info->has_dest)
         result = nir_if_phi(b, results[i], result);
   }

   if (info->has_dest)
      nir_ssa_def_rewrite_uses(&intrin->dest.ssa, result);
   nir_instr_remove(&intrin->instr);
}

static nir_ssa_def *
build_bindless(struct tu_device *dev, nir_builder *b,
               nir_deref_instr *deref, bool is_sampler,
               struct tu_shader *shader,
               const struct tu_pipeline_layout *layout)
{
   nir_variable *var = nir_deref_instr_get_variable(deref);

   unsigned set = var->data.descriptor_set;
   unsigned binding = var->data.binding;
   const struct tu_descriptor_set_binding_layout *bind_layout =
      &layout->set[set].layout->binding[binding];

   /* input attachments use non bindless workaround */
   if (bind_layout->type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT &&
       likely(!(dev->instance->debug_flags & TU_DEBUG_DYNAMIC))) {
      const struct glsl_type *glsl_type = glsl_without_array(var->type);
      uint32_t idx = var->data.index * 2;

      BITSET_SET_RANGE_INSIDE_WORD(b->shader->info.textures_used, idx * 2, ((idx * 2) + (bind_layout->array_size * 2)) - 1);

      /* D24S8 workaround: stencil of D24S8 will be sampled as uint */
      if (glsl_get_sampler_result_type(glsl_type) == GLSL_TYPE_UINT)
         idx += 1;

      if (deref->deref_type == nir_deref_type_var)
         return nir_imm_int(b, idx);

      nir_ssa_def *arr_index = nir_ssa_for_src(b, deref->arr.index, 1);
      return nir_iadd(b, nir_imm_int(b, idx),
                      nir_imul_imm(b, arr_index, 2));
   }

   shader->active_desc_sets |= 1u << set;

   nir_ssa_def *desc_offset;
   unsigned descriptor_stride;
   unsigned offset = 0;
   /* Samplers come second in combined image/sampler descriptors, see
      * write_combined_image_sampler_descriptor().
      */
   if (is_sampler && bind_layout->type ==
         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
      offset = 1;
   }
   desc_offset =
      nir_imm_int(b, (bind_layout->offset / (4 * A6XX_TEX_CONST_DWORDS)) +
                  offset);
   descriptor_stride = bind_layout->size / (4 * A6XX_TEX_CONST_DWORDS);

   if (deref->deref_type != nir_deref_type_var) {
      assert(deref->deref_type == nir_deref_type_array);

      nir_ssa_def *arr_index = nir_ssa_for_src(b, deref->arr.index, 1);
      desc_offset = nir_iadd(b, desc_offset,
                             nir_imul_imm(b, arr_index, descriptor_stride));
   }

   return nir_bindless_resource_ir3(b, 32, desc_offset, .desc_set = set);
}

static void
lower_image_deref(struct tu_device *dev, nir_builder *b,
                  nir_intrinsic_instr *instr, struct tu_shader *shader,
                  const struct tu_pipeline_layout *layout)
{
   nir_deref_instr *deref = nir_src_as_deref(instr->src[0]);
   nir_ssa_def *bindless = build_bindless(dev, b, deref, false, shader, layout);
   nir_rewrite_image_intrinsic(instr, bindless, true);
}

static bool
lower_intrinsic(nir_builder *b, nir_intrinsic_instr *instr,
                struct tu_device *dev,
                struct tu_shader *shader,
                const struct tu_pipeline_layout *layout)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_push_constant:
      lower_load_push_constant(dev, b, instr, shader, layout);
      return true;

   case nir_intrinsic_load_vulkan_descriptor:
      lower_load_vulkan_descriptor(b, instr);
      return true;

   case nir_intrinsic_vulkan_resource_index:
      lower_vulkan_resource_index(b, instr, shader, layout);
      return true;
   case nir_intrinsic_vulkan_resource_reindex:
      lower_vulkan_resource_reindex(b, instr);
      return true;

   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_store_ssbo:
   case nir_intrinsic_ssbo_atomic_add:
   case nir_intrinsic_ssbo_atomic_imin:
   case nir_intrinsic_ssbo_atomic_umin:
   case nir_intrinsic_ssbo_atomic_imax:
   case nir_intrinsic_ssbo_atomic_umax:
   case nir_intrinsic_ssbo_atomic_and:
   case nir_intrinsic_ssbo_atomic_or:
   case nir_intrinsic_ssbo_atomic_xor:
   case nir_intrinsic_ssbo_atomic_exchange:
   case nir_intrinsic_ssbo_atomic_comp_swap:
   case nir_intrinsic_ssbo_atomic_fadd:
   case nir_intrinsic_ssbo_atomic_fmin:
   case nir_intrinsic_ssbo_atomic_fmax:
   case nir_intrinsic_ssbo_atomic_fcomp_swap:
   case nir_intrinsic_get_ssbo_size:
      lower_ssbo_ubo_intrinsic(dev, b, instr);
      return true;

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
      lower_image_deref(dev, b, instr, shader, layout);
      return true;

   default:
      return false;
   }
}

static void
lower_tex_ycbcr(const struct tu_pipeline_layout *layout,
                nir_builder *builder,
                nir_tex_instr *tex)
{
   int deref_src_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   assert(deref_src_idx >= 0);
   nir_deref_instr *deref = nir_src_as_deref(tex->src[deref_src_idx].src);

   nir_variable *var = nir_deref_instr_get_variable(deref);
   const struct tu_descriptor_set_layout *set_layout =
      layout->set[var->data.descriptor_set].layout;
   const struct tu_descriptor_set_binding_layout *binding =
      &set_layout->binding[var->data.binding];
   const struct tu_sampler_ycbcr_conversion *ycbcr_samplers =
      tu_immutable_ycbcr_samplers(set_layout, binding);

   if (!ycbcr_samplers)
      return;

   /* For the following instructions, we don't apply any change */
   if (tex->op == nir_texop_txs ||
       tex->op == nir_texop_query_levels ||
       tex->op == nir_texop_lod)
      return;

   assert(tex->texture_index == 0);
   unsigned array_index = 0;
   if (deref->deref_type != nir_deref_type_var) {
      assert(deref->deref_type == nir_deref_type_array);
      if (!nir_src_is_const(deref->arr.index))
         return;
      array_index = nir_src_as_uint(deref->arr.index);
      array_index = MIN2(array_index, binding->array_size - 1);
   }
   const struct tu_sampler_ycbcr_conversion *ycbcr_sampler = ycbcr_samplers + array_index;

   if (ycbcr_sampler->ycbcr_model == VK_SAMPLER_YCBCR_MODEL_CONVERSION_RGB_IDENTITY)
      return;

   builder->cursor = nir_after_instr(&tex->instr);

   uint8_t bits = vk_format_get_component_bits(ycbcr_sampler->format,
                                               UTIL_FORMAT_COLORSPACE_RGB,
                                               PIPE_SWIZZLE_X);

   switch (ycbcr_sampler->format) {
   case VK_FORMAT_G8B8G8R8_422_UNORM:
   case VK_FORMAT_B8G8R8G8_422_UNORM:
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
   case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
      /* util_format_get_component_bits doesn't return what we want */
      bits = 8;
      break;
   default:
      break;
   }

   uint32_t bpcs[3] = {bits, bits, bits}; /* TODO: use right bpc for each channel ? */
   nir_ssa_def *result = nir_convert_ycbcr_to_rgb(builder,
                                                  ycbcr_sampler->ycbcr_model,
                                                  ycbcr_sampler->ycbcr_range,
                                                  &tex->dest.ssa,
                                                  bpcs);
   nir_ssa_def_rewrite_uses_after(&tex->dest.ssa, result,
                                  result->parent_instr);

   builder->cursor = nir_before_instr(&tex->instr);
}

static bool
lower_tex(nir_builder *b, nir_tex_instr *tex, struct tu_device *dev,
          struct tu_shader *shader, const struct tu_pipeline_layout *layout)
{
   lower_tex_ycbcr(layout, b, tex);

   int sampler_src_idx = nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref);
   if (sampler_src_idx >= 0) {
      nir_deref_instr *deref = nir_src_as_deref(tex->src[sampler_src_idx].src);
      nir_ssa_def *bindless = build_bindless(dev, b, deref, true, shader, layout);
      nir_instr_rewrite_src(&tex->instr, &tex->src[sampler_src_idx].src,
                            nir_src_for_ssa(bindless));
      tex->src[sampler_src_idx].src_type = nir_tex_src_sampler_handle;
   }

   int tex_src_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   if (tex_src_idx >= 0) {
      nir_deref_instr *deref = nir_src_as_deref(tex->src[tex_src_idx].src);
      nir_ssa_def *bindless = build_bindless(dev, b, deref, false, shader, layout);
      nir_instr_rewrite_src(&tex->instr, &tex->src[tex_src_idx].src,
                            nir_src_for_ssa(bindless));
      tex->src[tex_src_idx].src_type = nir_tex_src_texture_handle;

      /* for the input attachment case: */
      if (bindless->parent_instr->type != nir_instr_type_intrinsic)
         tex->src[tex_src_idx].src_type = nir_tex_src_texture_offset;
   }

   return true;
}

struct lower_instr_params {
   struct tu_device *dev;
   struct tu_shader *shader;
   const struct tu_pipeline_layout *layout;
};

static bool
lower_instr(nir_builder *b, nir_instr *instr, void *cb_data)
{
   struct lower_instr_params *params = cb_data;
   b->cursor = nir_before_instr(instr);
   switch (instr->type) {
   case nir_instr_type_tex:
      return lower_tex(b, nir_instr_as_tex(instr), params->dev, params->shader, params->layout);
   case nir_instr_type_intrinsic:
      return lower_intrinsic(b, nir_instr_as_intrinsic(instr), params->dev, params->shader, params->layout);
   default:
      return false;
   }
}

/* Figure out the range of push constants that we're actually going to push to
 * the shader, and tell the backend to reserve this range when pushing UBO
 * constants.
 */

static void
gather_push_constants(nir_shader *shader, struct tu_shader *tu_shader)
{
   uint32_t min = UINT32_MAX, max = 0;
   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;

      nir_foreach_block(block, function->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_load_push_constant)
               continue;

            uint32_t base = nir_intrinsic_base(intrin);
            uint32_t range = nir_intrinsic_range(intrin);
            min = MIN2(min, base);
            max = MAX2(max, base + range);
            break;
         }
      }
   }

   if (min >= max) {
      tu_shader->push_consts.lo = 0;
      tu_shader->push_consts.dwords = 0;
      return;
   }

   /* CP_LOAD_STATE OFFSET and NUM_UNIT for SHARED_CONSTS are in units of
    * dwords while loading regular consts is in units of vec4's.
    * So we unify the unit here as dwords for tu_push_constant_range, then
    * we should consider correct unit when emitting.
    *
    * Note there's an alignment requirement of 16 dwords on OFFSET. Expand
    * the range and change units accordingly.
    */
   tu_shader->push_consts.lo = (min / 4) / 4 * 4;
   tu_shader->push_consts.dwords =
      align(max, 16) / 4 - tu_shader->push_consts.lo;
}

static bool
tu_lower_io(nir_shader *shader, struct tu_device *dev,
            struct tu_shader *tu_shader,
            const struct tu_pipeline_layout *layout)
{
   if (!tu6_shared_constants_enable(layout, dev->compiler))
      gather_push_constants(shader, tu_shader);

   struct lower_instr_params params = {
      .dev = dev,
      .shader = tu_shader,
      .layout = layout,
   };

   bool progress = nir_shader_instructions_pass(shader,
                                                lower_instr,
                                                nir_metadata_none,
                                                &params);

   /* Remove now-unused variables so that when we gather the shader info later
    * they won't be counted.
    */

   if (progress)
      nir_opt_dce(shader);

   progress |=
      nir_remove_dead_variables(shader,
                                nir_var_uniform | nir_var_mem_ubo | nir_var_mem_ssbo,
                                NULL);

   return progress;
}

static void
shared_type_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   unsigned comp_size =
      glsl_type_is_boolean(type) ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length;
   *align = comp_size;
}

static void
tu_gather_xfb_info(nir_shader *nir, struct ir3_stream_output_info *info)
{
   nir_shader_gather_xfb_info(nir);

   if (!nir->xfb_info)
      return;

   nir_xfb_info *xfb = nir->xfb_info;

   uint8_t output_map[VARYING_SLOT_TESS_MAX];
   memset(output_map, 0, sizeof(output_map));

   nir_foreach_shader_out_variable(var, nir) {
      unsigned slots =
         var->data.compact ? DIV_ROUND_UP(glsl_get_length(var->type), 4)
                           : glsl_count_attribute_slots(var->type, false);
      for (unsigned i = 0; i < slots; i++)
         output_map[var->data.location + i] = var->data.driver_location + i;
   }

   assert(xfb->output_count <= IR3_MAX_SO_OUTPUTS);
   info->num_outputs = xfb->output_count;

   for (int i = 0; i < IR3_MAX_SO_BUFFERS; i++) {
      info->stride[i] = xfb->buffers[i].stride / 4;
      info->buffer_to_stream[i] = xfb->buffer_to_stream[i];
   }

   info->streams_written = xfb->streams_written;

   for (int i = 0; i < xfb->output_count; i++) {
      info->output[i].register_index = output_map[xfb->outputs[i].location];
      info->output[i].start_component = xfb->outputs[i].component_offset;
      info->output[i].num_components =
                           util_bitcount(xfb->outputs[i].component_mask);
      info->output[i].output_buffer  = xfb->outputs[i].buffer;
      info->output[i].dst_offset = xfb->outputs[i].offset / 4;
      info->output[i].stream = xfb->buffer_to_stream[xfb->outputs[i].buffer];
   }
}

struct tu_shader *
tu_shader_create(struct tu_device *dev,
                 nir_shader *nir,
                 const struct tu_shader_key *key,
                 struct tu_pipeline_layout *layout,
                 const VkAllocationCallbacks *alloc)
{
   struct tu_shader *shader;

   shader = vk_zalloc2(
      &dev->vk.alloc, alloc,
      sizeof(*shader),
      8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!shader)
      return NULL;

   NIR_PASS_V(nir, nir_opt_access, &(nir_opt_access_options) {
               .is_vulkan = true,
               .infer_non_readable = true,
             });

   if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS_V(nir, nir_lower_input_attachments,
                 &(nir_input_attachment_options) {
                     .use_fragcoord_sysval = true,
                     .use_layer_id_sysval = false,
                     /* When using multiview rendering, we must use
                      * gl_ViewIndex as the layer id to pass to the texture
                      * sampling function. gl_Layer doesn't work when
                      * multiview is enabled.
                      */
                     .use_view_id_for_layer = key->multiview_mask != 0,
                 });
   }

   /* This needs to happen before multiview lowering which rewrites store
    * instructions of the position variable, so that we can just rewrite one
    * store at the end instead of having to rewrite every store specified by
    * the user.
    */
   ir3_nir_lower_io_to_temporaries(nir);

   if (nir->info.stage == MESA_SHADER_VERTEX && key->multiview_mask) {
      tu_nir_lower_multiview(nir, key->multiview_mask,
                             &shader->multi_pos_output, dev);
   }

   if (nir->info.stage == MESA_SHADER_FRAGMENT && key->force_sample_interp) {
      nir_foreach_shader_in_variable(var, nir) {
         if (!var->data.centroid)
            var->data.sample = true;
      }
   }

   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_push_const,
              nir_address_format_32bit_offset);

   NIR_PASS_V(nir, nir_lower_explicit_io,
              nir_var_mem_ubo | nir_var_mem_ssbo,
              nir_address_format_vec2_index_32bit_offset);

   NIR_PASS_V(nir, nir_lower_explicit_io,
              nir_var_mem_global,
              nir_address_format_64bit_global);

   if (nir->info.stage == MESA_SHADER_COMPUTE) {
      NIR_PASS_V(nir, nir_lower_vars_to_explicit_types,
                 nir_var_mem_shared, shared_type_info);
      NIR_PASS_V(nir, nir_lower_explicit_io,
                 nir_var_mem_shared,
                 nir_address_format_32bit_offset);

      if (nir->info.zero_initialize_shared_memory && nir->info.shared_size > 0) {
         const unsigned chunk_size = 16; /* max single store size */
         /* Shared memory is allocated in 1024b chunks in HW, but the zero-init
          * extension only requires us to initialize the memory that the shader
          * is allocated at the API level, and it's up to the user to ensure
          * that accesses are limited to those bounds.
          */
         const unsigned shared_size = ALIGN(nir->info.shared_size, chunk_size);
         NIR_PASS_V(nir, nir_zero_initialize_shared_memory, shared_size, chunk_size);
      }

      const struct nir_lower_compute_system_values_options compute_sysval_options = {
         .has_base_workgroup_id = true,
      };
      NIR_PASS_V(nir, nir_lower_compute_system_values, &compute_sysval_options);
   }

   nir_assign_io_var_locations(nir, nir_var_shader_in, &nir->num_inputs, nir->info.stage);
   nir_assign_io_var_locations(nir, nir_var_shader_out, &nir->num_outputs, nir->info.stage);

  /* Gather information for transform feedback. This should be called after:
    * - nir_split_per_member_structs.
    * - nir_remove_dead_variables with varyings, so that we could align
    *   stream outputs correctly.
    * - nir_assign_io_var_locations - to have valid driver_location
    */
   struct ir3_stream_output_info so_info = {};
   if (nir->info.stage == MESA_SHADER_VERTEX ||
         nir->info.stage == MESA_SHADER_TESS_EVAL ||
         nir->info.stage == MESA_SHADER_GEOMETRY)
      tu_gather_xfb_info(nir, &so_info);

   NIR_PASS_V(nir, tu_lower_io, dev, shader, layout);

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   ir3_finalize_nir(dev->compiler, nir);

   uint32_t reserved_consts_vec4 = align(shader->push_consts.dwords, 16) / 4;
   bool shared_consts_enable = tu6_shared_constants_enable(layout, dev->compiler);
   if (shared_consts_enable)
      assert(!shader->push_consts.dwords);

   shader->ir3_shader =
      ir3_shader_from_nir(dev->compiler, nir, &(struct ir3_shader_options) {
                           .reserved_user_consts = reserved_consts_vec4,
                           .shared_consts_enable = shared_consts_enable,
                           .api_wavesize = key->api_wavesize,
                           .real_wavesize = key->real_wavesize,
                          }, &so_info);

   return shader;
}

void
tu_shader_destroy(struct tu_device *dev,
                  struct tu_shader *shader,
                  const VkAllocationCallbacks *alloc)
{
   ir3_shader_destroy(shader->ir3_shader);

   vk_free2(&dev->vk.alloc, alloc, shader);
}
