/*
 * Copyright © 2019 Google LLC
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "tu_private.h"

#include "spirv/nir_spirv.h"
#include "util/mesa-sha1.h"
#include "nir/nir_xfb_info.h"
#include "nir/nir_vulkan.h"
#include "vk_util.h"

#include "ir3/ir3_nir.h"

nir_shader *
tu_spirv_to_nir(struct tu_device *dev,
                const VkPipelineShaderStageCreateInfo *stage_info,
                gl_shader_stage stage)
{
   /* TODO these are made-up */
   const struct spirv_to_nir_options spirv_options = {
      .frag_coord_is_sysval = true,

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
      },
   };
   const nir_shader_compiler_options *nir_options =
      ir3_get_compiler_options(dev->compiler);

   /* convert VkSpecializationInfo */
   const VkSpecializationInfo *spec_info = stage_info->pSpecializationInfo;
   struct nir_spirv_specialization *spec = NULL;
   uint32_t num_spec = 0;
   if (spec_info && spec_info->mapEntryCount) {
      spec = calloc(spec_info->mapEntryCount, sizeof(*spec));
      if (!spec)
         return NULL;

      for (uint32_t i = 0; i < spec_info->mapEntryCount; i++) {
         const VkSpecializationMapEntry *entry = &spec_info->pMapEntries[i];
         const void *data = spec_info->pData + entry->offset;
         assert(data + entry->size <= spec_info->pData + spec_info->dataSize);
         spec[i].id = entry->constantID;
         switch (entry->size) {
         case 8:
            spec[i].value.u64 = *(const uint64_t *)data;
            break;
         case 4:
            spec[i].value.u32 = *(const uint32_t *)data;
            break;
         case 2:
            spec[i].value.u16 = *(const uint16_t *)data;
            break;
         case 1:
            spec[i].value.u8 = *(const uint8_t *)data;
            break;
         default:
            assert(!"Invalid spec constant size");
            break;
         }
         spec[i].defined_on_module = false;
      }

      num_spec = spec_info->mapEntryCount;
   }

   struct tu_shader_module *module =
      tu_shader_module_from_handle(stage_info->module);
   assert(module->code_size % 4 == 0);
   nir_shader *nir =
      spirv_to_nir(module->code, module->code_size / 4,
                   spec, num_spec, stage, stage_info->pName,
                   &spirv_options, nir_options);

   free(spec);

   assert(nir->info.stage == stage);
   nir_validate_shader(nir, "after spirv_to_nir");

   if (unlikely(dev->physical_device->instance->debug_flags & TU_DEBUG_NIR)) {
      fprintf(stderr, "translated nir:\n");
      nir_print_shader(nir, stderr);
   }

   /* multi step inlining procedure */
   NIR_PASS_V(nir, nir_lower_variable_initializers, nir_var_function_temp);
   NIR_PASS_V(nir, nir_lower_returns);
   NIR_PASS_V(nir, nir_inline_functions);
   NIR_PASS_V(nir, nir_copy_prop);
   NIR_PASS_V(nir, nir_opt_deref);
   foreach_list_typed_safe(nir_function, func, node, &nir->functions) {
      if (!func->is_entrypoint)
         exec_node_remove(&func->node);
   }
   assert(exec_list_length(&nir->functions) == 1);
   NIR_PASS_V(nir, nir_lower_variable_initializers, ~nir_var_function_temp);

   /* Split member structs.  We do this before lower_io_to_temporaries so that
    * it doesn't lower system values to temporaries by accident.
    */
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_split_per_member_structs);

   NIR_PASS_V(nir, nir_remove_dead_variables,
              nir_var_shader_in | nir_var_shader_out | nir_var_system_value | nir_var_mem_shared,
              NULL);

   NIR_PASS_V(nir, nir_propagate_invariant);

   NIR_PASS_V(nir, nir_lower_io_to_temporaries, nir_shader_get_entrypoint(nir), true, true);

   NIR_PASS_V(nir, nir_lower_global_vars_to_local);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_var_copies);

   NIR_PASS_V(nir, nir_opt_copy_prop_vars);
   NIR_PASS_V(nir, nir_opt_combine_stores, nir_var_all);

   /* ir3 doesn't support indirect input/output */
   /* TODO: We shouldn't perform this lowering pass on gl_TessLevelInner
    * and gl_TessLevelOuter. Since the tess levels are actually stored in
    * a global BO, they can be directly accessed via stg and ldg.
    * nir_lower_indirect_derefs will instead generate a big if-ladder which
    * isn't *incorrect* but is much less efficient. */
   NIR_PASS_V(nir, nir_lower_indirect_derefs, nir_var_shader_in | nir_var_shader_out, UINT32_MAX);

   NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, false);

   NIR_PASS_V(nir, nir_lower_system_values);
   NIR_PASS_V(nir, nir_lower_compute_system_values, NULL);

   NIR_PASS_V(nir, nir_lower_clip_cull_distance_arrays);

   NIR_PASS_V(nir, nir_lower_frexp);

   ir3_optimize_loop(nir);

   return nir;
}

static void
lower_load_push_constant(nir_builder *b, nir_intrinsic_instr *instr,
                         struct tu_shader *shader)
{
   nir_intrinsic_instr *load =
      nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_uniform);
   load->num_components = instr->num_components;
   uint32_t base = nir_intrinsic_base(instr);
   assert(base % 4 == 0);
   assert(base >= shader->push_consts.lo * 16);
   base -= shader->push_consts.lo * 16;
   nir_intrinsic_set_base(load, base / 4);
   load->src[0] =
      nir_src_for_ssa(nir_ushr(b, instr->src[0].ssa, nir_imm_int(b, 2)));
   nir_ssa_dest_init(&load->instr, &load->dest,
                     load->num_components, instr->dest.ssa.bit_size,
                     instr->dest.ssa.name);
   nir_builder_instr_insert(b, &load->instr);
   nir_ssa_def_rewrite_uses(&instr->dest.ssa, nir_src_for_ssa(&load->dest.ssa));

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
      base = layout->set[set].dynamic_offset_start +
         binding_layout->dynamic_offset_offset;
      set = MAX_SETS;
      break;
   default:
      base = binding_layout->offset / (4 * A6XX_TEX_CONST_DWORDS);
      break;
   }

   nir_ssa_def *def = nir_vec3(b, nir_imm_int(b, set),
                               nir_iadd(b, nir_imm_int(b, base), vulkan_idx),
                               nir_imm_int(b, 0));

   nir_ssa_def_rewrite_uses(&instr->dest.ssa, nir_src_for_ssa(def));
   nir_instr_remove(&instr->instr);
}

static void
lower_load_vulkan_descriptor(nir_intrinsic_instr *intrin)
{
   /* Loading the descriptor happens as part of the load/store instruction so
    * this is a no-op.
    */
   nir_ssa_def_rewrite_uses(&intrin->dest.ssa, intrin->src[0]);
   nir_instr_remove(&intrin->instr);
}

static void
lower_ssbo_ubo_intrinsic(nir_builder *b, nir_intrinsic_instr *intrin)
{
   const nir_intrinsic_info *info = &nir_intrinsic_infos[intrin->intrinsic];

   /* The bindless base is part of the instruction, which means that part of
    * the "pointer" has to be constant. We solve this in the same way the blob
    * does, by generating a bunch of if-statements. In the usual case where
    * the descriptor set is constant this will get optimized out.
    */

   unsigned buffer_src;
   if (intrin->intrinsic == nir_intrinsic_store_ssbo) {
      /* This has the value first */
      buffer_src = 1;
   } else {
      buffer_src = 0;
   }

   nir_ssa_def *base_idx = nir_channel(b, intrin->src[buffer_src].ssa, 0);
   nir_ssa_def *descriptor_idx = nir_channel(b, intrin->src[buffer_src].ssa, 1);

   nir_ssa_def *results[MAX_SETS + 1] = { NULL };

   for (unsigned i = 0; i < MAX_SETS + 1; i++) {
      /* if (base_idx == i) { ... */
      nir_if *nif = nir_push_if(b, nir_ieq_imm(b, base_idx, i));

      nir_intrinsic_instr *bindless =
         nir_intrinsic_instr_create(b->shader,
                                    nir_intrinsic_bindless_resource_ir3);
      bindless->num_components = 0;
      nir_ssa_dest_init(&bindless->instr, &bindless->dest,
                        1, 32, NULL);
      nir_intrinsic_set_desc_set(bindless, i);
      bindless->src[0] = nir_src_for_ssa(descriptor_idx);
      nir_builder_instr_insert(b, &bindless->instr);

      nir_intrinsic_instr *copy =
         nir_intrinsic_instr_create(b->shader, intrin->intrinsic);

      copy->num_components = intrin->num_components;

      for (unsigned src = 0; src < info->num_srcs; src++) {
         if (src == buffer_src)
            copy->src[src] = nir_src_for_ssa(&bindless->dest.ssa);
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
                           intrin->dest.ssa.name);
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
      nir_ssa_def_rewrite_uses(&intrin->dest.ssa, nir_src_for_ssa(result));
   nir_instr_remove(&intrin->instr);
}

static nir_ssa_def *
build_bindless(nir_builder *b, nir_deref_instr *deref, bool is_sampler,
               struct tu_shader *shader,
               const struct tu_pipeline_layout *layout)
{
   nir_variable *var = nir_deref_instr_get_variable(deref);

   unsigned set = var->data.descriptor_set;
   unsigned binding = var->data.binding;
   const struct tu_descriptor_set_binding_layout *bind_layout =
      &layout->set[set].layout->binding[binding];

   /* input attachments use non bindless workaround */
   if (bind_layout->type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT) {
      const struct glsl_type *glsl_type = glsl_without_array(var->type);
      uint32_t idx = var->data.index * 2;

      b->shader->info.textures_used |=
         ((1ull << (bind_layout->array_size * 2)) - 1) << (idx * 2);

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

   nir_intrinsic_instr *bindless =
      nir_intrinsic_instr_create(b->shader,
                                 nir_intrinsic_bindless_resource_ir3);
   bindless->num_components = 0;
   nir_ssa_dest_init(&bindless->instr, &bindless->dest,
                     1, 32, NULL);
   nir_intrinsic_set_desc_set(bindless, set);
   bindless->src[0] = nir_src_for_ssa(desc_offset);
   nir_builder_instr_insert(b, &bindless->instr);

   return &bindless->dest.ssa;
}

static void
lower_image_deref(nir_builder *b,
                  nir_intrinsic_instr *instr, struct tu_shader *shader,
                  const struct tu_pipeline_layout *layout)
{
   nir_deref_instr *deref = nir_src_as_deref(instr->src[0]);
   nir_ssa_def *bindless = build_bindless(b, deref, false, shader, layout);
   nir_rewrite_image_intrinsic(instr, bindless, true);
}

static bool
lower_intrinsic(nir_builder *b, nir_intrinsic_instr *instr,
                struct tu_shader *shader,
                const struct tu_pipeline_layout *layout)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_push_constant:
      lower_load_push_constant(b, instr, shader);
      return true;

   case nir_intrinsic_load_vulkan_descriptor:
      lower_load_vulkan_descriptor(instr);
      return true;

   case nir_intrinsic_vulkan_resource_index:
      lower_vulkan_resource_index(b, instr, shader, layout);
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
      lower_ssbo_ubo_intrinsic(b, instr);
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
      lower_image_deref(b, instr, shader, layout);
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
   uint32_t bpcs[3] = {bits, bits, bits}; /* TODO: use right bpc for each channel ? */
   nir_ssa_def *result = nir_convert_ycbcr_to_rgb(builder,
                                                  ycbcr_sampler->ycbcr_model,
                                                  ycbcr_sampler->ycbcr_range,
                                                  &tex->dest.ssa,
                                                  bpcs);
   nir_ssa_def_rewrite_uses_after(&tex->dest.ssa, nir_src_for_ssa(result),
                                  result->parent_instr);

   builder->cursor = nir_before_instr(&tex->instr);
}

static bool
lower_tex(nir_builder *b, nir_tex_instr *tex,
          struct tu_shader *shader, const struct tu_pipeline_layout *layout)
{
   lower_tex_ycbcr(layout, b, tex);

   int sampler_src_idx = nir_tex_instr_src_index(tex, nir_tex_src_sampler_deref);
   if (sampler_src_idx >= 0) {
      nir_deref_instr *deref = nir_src_as_deref(tex->src[sampler_src_idx].src);
      nir_ssa_def *bindless = build_bindless(b, deref, true, shader, layout);
      nir_instr_rewrite_src(&tex->instr, &tex->src[sampler_src_idx].src,
                            nir_src_for_ssa(bindless));
      tex->src[sampler_src_idx].src_type = nir_tex_src_sampler_handle;
   }

   int tex_src_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   if (tex_src_idx >= 0) {
      nir_deref_instr *deref = nir_src_as_deref(tex->src[tex_src_idx].src);
      nir_ssa_def *bindless = build_bindless(b, deref, false, shader, layout);
      nir_instr_rewrite_src(&tex->instr, &tex->src[tex_src_idx].src,
                            nir_src_for_ssa(bindless));
      tex->src[tex_src_idx].src_type = nir_tex_src_texture_handle;

      /* for the input attachment case: */
      if (bindless->parent_instr->type != nir_instr_type_intrinsic)
         tex->src[tex_src_idx].src_type = nir_tex_src_texture_offset;
   }

   return true;
}

static bool
lower_impl(nir_function_impl *impl, struct tu_shader *shader,
            const struct tu_pipeline_layout *layout)
{
   nir_builder b;
   nir_builder_init(&b, impl);
   bool progress = false;

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         b.cursor = nir_before_instr(instr);
         switch (instr->type) {
         case nir_instr_type_tex:
            progress |= lower_tex(&b, nir_instr_as_tex(instr), shader, layout);
            break;
         case nir_instr_type_intrinsic:
            progress |= lower_intrinsic(&b, nir_instr_as_intrinsic(instr), shader, layout);
            break;
         default:
            break;
         }
      }
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_none);
   else
      nir_metadata_preserve(impl, nir_metadata_all);

   return progress;
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
      tu_shader->push_consts.count = 0;
      return;
   }

   /* CP_LOAD_STATE OFFSET and NUM_UNIT are in units of vec4 (4 dwords),
    * however there's an alignment requirement of 4 on OFFSET. Expand the
    * range and change units accordingly.
    */
   tu_shader->push_consts.lo = (min / 16) / 4 * 4;
   tu_shader->push_consts.count =
      align(max, 16) / 16 - tu_shader->push_consts.lo;
}

static bool
tu_lower_io(nir_shader *shader, struct tu_shader *tu_shader,
            const struct tu_pipeline_layout *layout)
{
   bool progress = false;

   gather_push_constants(shader, tu_shader);

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= lower_impl(function->impl, tu_shader, layout);
   }

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
   *align = 4;
}

static void
tu_gather_xfb_info(nir_shader *nir, struct ir3_stream_output_info *info)
{
   nir_xfb_info *xfb = nir_gather_xfb_info(nir, NULL);

   if (!xfb)
      return;

   /* creating a map from VARYING_SLOT_* enums to consecutive index */
   uint8_t num_outputs = 0;
   uint64_t outputs_written = 0;
   for (int i = 0; i < xfb->output_count; i++)
      outputs_written |= BITFIELD64_BIT(xfb->outputs[i].location);

   uint8_t output_map[VARYING_SLOT_TESS_MAX];
   memset(output_map, 0, sizeof(output_map));

   for (unsigned attr = 0; attr < VARYING_SLOT_MAX; attr++) {
      if (outputs_written & BITFIELD64_BIT(attr))
         output_map[attr] = num_outputs++;
   }

   assert(xfb->output_count < IR3_MAX_SO_OUTPUTS);
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

   ralloc_free(xfb);
}

struct tu_shader *
tu_shader_create(struct tu_device *dev,
                 nir_shader *nir,
                 unsigned multiview_mask,
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

   /* Gather information for transform feedback.
    * This should be called after nir_split_per_member_structs.
    * Also needs to be called after nir_remove_dead_variables with varyings,
    * so that we could align stream outputs correctly.
    */
   struct ir3_stream_output_info so_info = {};
   if (nir->info.stage == MESA_SHADER_VERTEX ||
         nir->info.stage == MESA_SHADER_TESS_EVAL ||
         nir->info.stage == MESA_SHADER_GEOMETRY)
      tu_gather_xfb_info(nir, &so_info);

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
                     .use_view_id_for_layer = multiview_mask != 0,
                 });
   }

   if (nir->info.stage == MESA_SHADER_VERTEX && multiview_mask) {
      tu_nir_lower_multiview(nir, multiview_mask,
                             &shader->multi_pos_output, dev);
   }

   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_push_const,
              nir_address_format_32bit_offset);

   NIR_PASS_V(nir, nir_lower_explicit_io,
              nir_var_mem_ubo | nir_var_mem_ssbo,
              nir_address_format_vec2_index_32bit_offset);

   if (nir->info.stage == MESA_SHADER_COMPUTE) {
      NIR_PASS_V(nir, nir_lower_vars_to_explicit_types,
                 nir_var_mem_shared, shared_type_info);
      NIR_PASS_V(nir, nir_lower_explicit_io,
                 nir_var_mem_shared,
                 nir_address_format_32bit_offset);
   }

   nir_assign_io_var_locations(nir, nir_var_shader_in, &nir->num_inputs, nir->info.stage);
   nir_assign_io_var_locations(nir, nir_var_shader_out, &nir->num_outputs, nir->info.stage);

   NIR_PASS_V(nir, tu_lower_io, shader, layout);

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   ir3_finalize_nir(dev->compiler, nir);

   shader->ir3_shader =
      ir3_shader_from_nir(dev->compiler, nir,
                          align(shader->push_consts.count, 4),
                          &so_info);

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

VkResult
tu_CreateShaderModule(VkDevice _device,
                      const VkShaderModuleCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkShaderModule *pShaderModule)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   struct tu_shader_module *module;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
   assert(pCreateInfo->flags == 0);
   assert(pCreateInfo->codeSize % 4 == 0);

   module = vk_object_alloc(&device->vk, pAllocator,
                            sizeof(*module) + pCreateInfo->codeSize,
                            VK_OBJECT_TYPE_SHADER_MODULE);
   if (module == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   module->code_size = pCreateInfo->codeSize;
   memcpy(module->code, pCreateInfo->pCode, pCreateInfo->codeSize);

   *pShaderModule = tu_shader_module_to_handle(module);

   return VK_SUCCESS;
}

void
tu_DestroyShaderModule(VkDevice _device,
                       VkShaderModule _module,
                       const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_shader_module, module, _module);

   if (!module)
      return;

   vk_object_free(&device->vk, pAllocator, module);
}
