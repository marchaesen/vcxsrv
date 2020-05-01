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
#include "vk_util.h"

#include "ir3/ir3_nir.h"

static nir_shader *
tu_spirv_to_nir(struct ir3_compiler *compiler,
                const uint32_t *words,
                size_t word_count,
                gl_shader_stage stage,
                const char *entry_point_name,
                const VkSpecializationInfo *spec_info)
{
   /* TODO these are made-up */
   const struct spirv_to_nir_options spirv_options = {
      .frag_coord_is_sysval = true,
      .lower_ubo_ssbo_access_to_offsets = true,
      .caps = {
         .transform_feedback = compiler->gpu_id >= 600,
      },
   };
   const nir_shader_compiler_options *nir_options =
      ir3_get_compiler_options(compiler);

   /* convert VkSpecializationInfo */
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

   nir_shader *nir =
      spirv_to_nir(words, word_count, spec, num_spec, stage, entry_point_name,
                   &spirv_options, nir_options);

   free(spec);

   assert(nir->info.stage == stage);
   nir_validate_shader(nir, "after spirv_to_nir");

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

   switch (binding_layout->type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
      base = layout->set[set].dynamic_offset_start +
         binding_layout->dynamic_offset_offset +
         layout->input_attachment_count;
      set = MAX_SETS;
      break;
   default:
      base = binding_layout->offset / (4 * A6XX_TEX_CONST_DWORDS);
      break;
   }

   nir_intrinsic_instr *bindless =
      nir_intrinsic_instr_create(b->shader,
                                 nir_intrinsic_bindless_resource_ir3);
   bindless->num_components = 1;
   nir_ssa_dest_init(&bindless->instr, &bindless->dest,
                     1, 32, NULL);
   nir_intrinsic_set_desc_set(bindless, set);
   bindless->src[0] = nir_src_for_ssa(nir_iadd(b, nir_imm_int(b, base), vulkan_idx));
   nir_builder_instr_insert(b, &bindless->instr);

   nir_ssa_def_rewrite_uses(&instr->dest.ssa,
                            nir_src_for_ssa(&bindless->dest.ssa));
   nir_instr_remove(&instr->instr);
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

   nir_ssa_def *desc_offset;
   unsigned descriptor_stride;
   if (bind_layout->type == VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT) {
      unsigned offset =
         layout->set[set].input_attachment_start +
         bind_layout->input_attachment_offset;
      desc_offset = nir_imm_int(b, offset);
      set = MAX_SETS;
      descriptor_stride = 1;
   } else {
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
   }

   if (deref->deref_type != nir_deref_type_var) {
      assert(deref->deref_type == nir_deref_type_array);

      nir_ssa_def *arr_index = nir_ssa_for_src(b, deref->arr.index, 1);
      desc_offset = nir_iadd(b, desc_offset,
                             nir_imul_imm(b, arr_index, descriptor_stride));
   }

   nir_intrinsic_instr *bindless =
      nir_intrinsic_instr_create(b->shader,
                                 nir_intrinsic_bindless_resource_ir3);
   bindless->num_components = 1;
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
   case nir_intrinsic_load_layer_id:
      /* TODO: remove this when layered rendering is implemented */
      nir_ssa_def_rewrite_uses(&instr->dest.ssa,
                               nir_src_for_ssa(nir_imm_int(b, 0)));
      nir_instr_remove(&instr->instr);
      return true;

   case nir_intrinsic_load_push_constant:
      lower_load_push_constant(b, instr, shader);
      return true;

   case nir_intrinsic_vulkan_resource_index:
      lower_vulkan_resource_index(b, instr, shader, layout);
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

static bool
lower_tex(nir_builder *b, nir_tex_instr *tex,
          struct tu_shader *shader, const struct tu_pipeline_layout *layout)
{
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
      tu_shader->ir3_shader.const_state.num_reserved_user_consts = 0;
      return;
   }

   /* CP_LOAD_STATE OFFSET and NUM_UNIT are in units of vec4 (4 dwords),
    * however there's an alignment requirement of 4 on OFFSET. Expand the
    * range and change units accordingly.
    */
   tu_shader->push_consts.lo = (min / 16) / 4 * 4;
   tu_shader->push_consts.count =
      align(max, 16) / 16 - tu_shader->push_consts.lo;
   tu_shader->ir3_shader.const_state.num_reserved_user_consts = 
      align(tu_shader->push_consts.count, 4);
}

/* Gather the InputAttachmentIndex for each input attachment from the NIR
 * shader and organize the info in a way so that draw-time patching is easy.
 */
static void
gather_input_attachments(nir_shader *shader, struct tu_shader *tu_shader,
                         const struct tu_pipeline_layout *layout)
{
   nir_foreach_variable(var, &shader->uniforms) {
      const struct glsl_type *glsl_type = glsl_without_array(var->type);

      if (!glsl_type_is_image(glsl_type))
         continue;

      enum glsl_sampler_dim dim = glsl_get_sampler_dim(glsl_type);

      const uint32_t set = var->data.descriptor_set;
      const uint32_t binding = var->data.binding;
      const struct tu_descriptor_set_binding_layout *bind_layout =
            &layout->set[set].layout->binding[binding];
      const uint32_t array_size = bind_layout->array_size;

      if (dim == GLSL_SAMPLER_DIM_SUBPASS ||
          dim == GLSL_SAMPLER_DIM_SUBPASS_MS) {
         unsigned offset =
            layout->set[set].input_attachment_start +
            bind_layout->input_attachment_offset;
         for (unsigned i = 0; i < array_size; i++)
            tu_shader->attachment_idx[offset + i] = var->data.index + i;
      }
   }
}

static bool
tu_lower_io(nir_shader *shader, struct tu_shader *tu_shader,
            const struct tu_pipeline_layout *layout)
{
   bool progress = false;

   gather_push_constants(shader, tu_shader);
   gather_input_attachments(shader, tu_shader, layout);

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= lower_impl(function->impl, tu_shader, layout);
   }

   return progress;
}

static void
tu_gather_xfb_info(nir_shader *nir, struct tu_shader *shader)
{
   struct ir3_stream_output_info *info = &shader->ir3_shader.stream_output;
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

   for (int i = 0; i < IR3_MAX_SO_BUFFERS; i++)
      info->stride[i] = xfb->buffers[i].stride / 4;

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
                 gl_shader_stage stage,
                 const VkPipelineShaderStageCreateInfo *stage_info,
                 struct tu_pipeline_layout *layout,
                 const VkAllocationCallbacks *alloc)
{
   const struct tu_shader_module *module =
      tu_shader_module_from_handle(stage_info->module);
   struct tu_shader *shader;

   const uint32_t max_variant_count = (stage == MESA_SHADER_VERTEX) ? 2 : 1;
   shader = vk_zalloc2(
      &dev->alloc, alloc,
      sizeof(*shader) + sizeof(struct ir3_shader_variant) * max_variant_count,
      8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!shader)
      return NULL;

   /* translate SPIR-V to NIR */
   assert(module->code_size % 4 == 0);
   nir_shader *nir = tu_spirv_to_nir(
      dev->compiler, (const uint32_t *) module->code, module->code_size / 4,
      stage, stage_info->pName, stage_info->pSpecializationInfo);
   if (!nir) {
      vk_free2(&dev->alloc, alloc, shader);
      return NULL;
   }

   if (unlikely(dev->physical_device->instance->debug_flags & TU_DEBUG_NIR)) {
      fprintf(stderr, "translated nir:\n");
      nir_print_shader(nir, stderr);
   }

   /* multi step inlining procedure */
   NIR_PASS_V(nir, nir_lower_variable_initializers, nir_var_function_temp);
   NIR_PASS_V(nir, nir_lower_returns);
   NIR_PASS_V(nir, nir_inline_functions);
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
              nir_var_shader_in | nir_var_shader_out | nir_var_system_value | nir_var_mem_shared);

   /* Gather information for transform feedback.
    * This should be called after nir_split_per_member_structs.
    * Also needs to be called after nir_remove_dead_variables with varyings,
    * so that we could align stream outputs correctly.
    */
   if (nir->info.stage == MESA_SHADER_VERTEX ||
         nir->info.stage == MESA_SHADER_TESS_EVAL ||
         nir->info.stage == MESA_SHADER_GEOMETRY)
      tu_gather_xfb_info(nir, shader);

   NIR_PASS_V(nir, nir_propagate_invariant);

   NIR_PASS_V(nir, nir_lower_io_to_temporaries, nir_shader_get_entrypoint(nir), true, true);

   NIR_PASS_V(nir, nir_lower_global_vars_to_local);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_var_copies);

   NIR_PASS_V(nir, nir_opt_copy_prop_vars);
   NIR_PASS_V(nir, nir_opt_combine_stores, nir_var_all);

   /* ir3 doesn't support indirect input/output */
   NIR_PASS_V(nir, nir_lower_indirect_derefs, nir_var_shader_in | nir_var_shader_out);

   NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, false);

   nir_assign_io_var_locations(&nir->inputs, &nir->num_inputs, stage);
   nir_assign_io_var_locations(&nir->outputs, &nir->num_outputs, stage);

   NIR_PASS_V(nir, nir_lower_system_values);
   NIR_PASS_V(nir, nir_lower_frexp);

   if (stage == MESA_SHADER_FRAGMENT)
      NIR_PASS_V(nir, nir_lower_input_attachments, true);

   NIR_PASS_V(nir, tu_lower_io, shader, layout);

   NIR_PASS_V(nir, nir_lower_io, nir_var_all, ir3_glsl_type_size, 0);

   if (stage == MESA_SHADER_FRAGMENT) {
      /* NOTE: lower load_barycentric_at_sample first, since it
       * produces load_barycentric_at_offset:
       */
      NIR_PASS_V(nir, ir3_nir_lower_load_barycentric_at_sample);
      NIR_PASS_V(nir, ir3_nir_lower_load_barycentric_at_offset);

      NIR_PASS_V(nir, ir3_nir_move_varying_inputs);
   }

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   /* num_uniforms only used by ir3 for size of ubo 0 (push constants) */
   nir->num_uniforms = MAX_PUSH_CONSTANTS_SIZE / 16;

   shader->ir3_shader.compiler = dev->compiler;
   shader->ir3_shader.type = stage;
   shader->ir3_shader.nir = nir;

   return shader;
}

void
tu_shader_destroy(struct tu_device *dev,
                  struct tu_shader *shader,
                  const VkAllocationCallbacks *alloc)
{
   if (shader->ir3_shader.nir)
      ralloc_free(shader->ir3_shader.nir);

   for (uint32_t i = 0; i < 1 + shader->has_binning_pass; i++) {
      if (shader->variants[i].ir)
         ir3_destroy(shader->variants[i].ir);
   }

   if (shader->ir3_shader.const_state.immediates)
	   free(shader->ir3_shader.const_state.immediates);
   if (shader->binary)
      free(shader->binary);
   if (shader->binning_binary)
      free(shader->binning_binary);

   vk_free2(&dev->alloc, alloc, shader);
}

void
tu_shader_compile_options_init(
   struct tu_shader_compile_options *options,
   const VkGraphicsPipelineCreateInfo *pipeline_info)
{
   bool has_gs = false;
   bool msaa = false;
   if (pipeline_info) {
      for (uint32_t i = 0; i < pipeline_info->stageCount; i++) {
         if (pipeline_info->pStages[i].stage == VK_SHADER_STAGE_GEOMETRY_BIT) {
            has_gs = true;
            break;
         }
      }

      const VkPipelineMultisampleStateCreateInfo *msaa_info = pipeline_info->pMultisampleState;
      const struct VkPipelineSampleLocationsStateCreateInfoEXT *sample_locations =
         vk_find_struct_const(msaa_info->pNext, PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT);
      if (!pipeline_info->pRasterizationState->rasterizerDiscardEnable &&
          (msaa_info->rasterizationSamples > 1 ||
          /* also set msaa key when sample location is not the default
           * since this affects varying interpolation */
           (sample_locations && sample_locations->sampleLocationsEnable))) {
         msaa = true;
      }
   }

   *options = (struct tu_shader_compile_options) {
      /* TODO: Populate the remaining fields of ir3_shader_key. */
      .key = {
         .has_gs = has_gs,
         .msaa = msaa,
      },
      /* TODO: VK_PIPELINE_CREATE_DISABLE_OPTIMIZATION_BIT
       * some optimizations need to happen otherwise shader might not compile
       */
      .optimize = true,
      .include_binning_pass = true,
   };
}

static uint32_t *
tu_compile_shader_variant(struct ir3_shader *shader,
                          const struct ir3_shader_key *key,
                          struct ir3_shader_variant *nonbinning,
                          struct ir3_shader_variant *variant)
{
   variant->shader = shader;
   variant->type = shader->type;
   variant->key = *key;
   variant->binning_pass = !!nonbinning;
   variant->nonbinning = nonbinning;

   int ret = ir3_compile_shader_nir(shader->compiler, variant);
   if (ret)
      return NULL;

   /* when assemble fails, we rely on tu_shader_destroy to clean up the
    * variant
    */
   return ir3_shader_assemble(variant, shader->compiler->gpu_id);
}

VkResult
tu_shader_compile(struct tu_device *dev,
                  struct tu_shader *shader,
                  const struct tu_shader *next_stage,
                  const struct tu_shader_compile_options *options,
                  const VkAllocationCallbacks *alloc)
{
   if (options->optimize) {
      /* ignore the key for the first pass of optimization */
      ir3_optimize_nir(&shader->ir3_shader, shader->ir3_shader.nir, NULL);

      if (unlikely(dev->physical_device->instance->debug_flags &
                   TU_DEBUG_NIR)) {
         fprintf(stderr, "optimized nir:\n");
         nir_print_shader(shader->ir3_shader.nir, stderr);
      }
   }

   shader->binary = tu_compile_shader_variant(
      &shader->ir3_shader, &options->key, NULL, &shader->variants[0]);
   if (!shader->binary)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   if (shader_debug_enabled(shader->ir3_shader.type)) {
      fprintf(stdout, "Native code for unnamed %s shader %s:\n",
              ir3_shader_stage(&shader->variants[0]), shader->ir3_shader.nir->info.name);
       if (shader->ir3_shader.type == MESA_SHADER_FRAGMENT)
               fprintf(stdout, "SIMD0\n");
       ir3_shader_disasm(&shader->variants[0], shader->binary, stdout);
   }

   /* compile another variant for the binning pass */
   if (options->include_binning_pass &&
       shader->ir3_shader.type == MESA_SHADER_VERTEX) {
      shader->binning_binary = tu_compile_shader_variant(
         &shader->ir3_shader, &options->key, &shader->variants[0],
         &shader->variants[1]);
      if (!shader->binning_binary)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      shader->has_binning_pass = true;

      if (shader_debug_enabled(MESA_SHADER_VERTEX)) {
         fprintf(stdout, "Native code for unnamed binning shader %s:\n",
                 shader->ir3_shader.nir->info.name);
          ir3_shader_disasm(&shader->variants[1], shader->binary, stdout);
      }
   }

   if (unlikely(dev->physical_device->instance->debug_flags & TU_DEBUG_IR3)) {
      fprintf(stderr, "disassembled ir3:\n");
      fprintf(stderr, "shader: %s\n",
              gl_shader_stage_name(shader->ir3_shader.type));
      ir3_shader_disasm(&shader->variants[0], shader->binary, stderr);

      if (shader->has_binning_pass) {
         fprintf(stderr, "disassembled ir3:\n");
         fprintf(stderr, "shader: %s (binning)\n",
                 gl_shader_stage_name(shader->ir3_shader.type));
         ir3_shader_disasm(&shader->variants[1], shader->binning_binary,
                           stderr);
      }
   }

   return VK_SUCCESS;
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

   module = vk_alloc2(&device->alloc, pAllocator,
                      sizeof(*module) + pCreateInfo->codeSize, 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (module == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   module->code_size = pCreateInfo->codeSize;
   memcpy(module->code, pCreateInfo->pCode, pCreateInfo->codeSize);

   _mesa_sha1_compute(module->code, module->code_size, module->sha1);

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

   vk_free2(&device->alloc, pAllocator, module);
}
