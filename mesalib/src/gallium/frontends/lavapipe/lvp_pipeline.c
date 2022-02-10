/*
 * Copyright © 2019 Red Hat.
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

#include "lvp_private.h"
#include "vk_util.h"
#include "glsl_types.h"
#include "spirv/nir_spirv.h"
#include "nir/nir_builder.h"
#include "lvp_lower_vulkan_resource.h"
#include "pipe/p_state.h"
#include "pipe/p_context.h"
#include "nir/nir_xfb_info.h"

#define SPIR_V_MAGIC_NUMBER 0x07230203

#define LVP_PIPELINE_DUP(dst, src, type, count) do {             \
      type *temp = ralloc_array(mem_ctx, type, count);           \
      if (!temp) return VK_ERROR_OUT_OF_HOST_MEMORY;             \
      memcpy(temp, (src), sizeof(type) * count);                 \
      dst = temp;                                                \
   } while(0)

VKAPI_ATTR void VKAPI_CALL lvp_DestroyPipeline(
   VkDevice                                    _device,
   VkPipeline                                  _pipeline,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_pipeline, pipeline, _pipeline);

   if (!_pipeline)
      return;

   if (pipeline->shader_cso[PIPE_SHADER_VERTEX])
      device->queue.ctx->delete_vs_state(device->queue.ctx, pipeline->shader_cso[PIPE_SHADER_VERTEX]);
   if (pipeline->shader_cso[PIPE_SHADER_FRAGMENT])
      device->queue.ctx->delete_fs_state(device->queue.ctx, pipeline->shader_cso[PIPE_SHADER_FRAGMENT]);
   if (pipeline->shader_cso[PIPE_SHADER_GEOMETRY])
      device->queue.ctx->delete_gs_state(device->queue.ctx, pipeline->shader_cso[PIPE_SHADER_GEOMETRY]);
   if (pipeline->shader_cso[PIPE_SHADER_TESS_CTRL])
      device->queue.ctx->delete_tcs_state(device->queue.ctx, pipeline->shader_cso[PIPE_SHADER_TESS_CTRL]);
   if (pipeline->shader_cso[PIPE_SHADER_TESS_EVAL])
      device->queue.ctx->delete_tes_state(device->queue.ctx, pipeline->shader_cso[PIPE_SHADER_TESS_EVAL]);
   if (pipeline->shader_cso[PIPE_SHADER_COMPUTE])
      device->queue.ctx->delete_compute_state(device->queue.ctx, pipeline->shader_cso[PIPE_SHADER_COMPUTE]);

   ralloc_free(pipeline->mem_ctx);
   vk_object_base_finish(&pipeline->base);
   vk_free2(&device->vk.alloc, pAllocator, pipeline);
}

static VkResult
deep_copy_shader_stage(void *mem_ctx,
                       struct VkPipelineShaderStageCreateInfo *dst,
                       const struct VkPipelineShaderStageCreateInfo *src)
{
   dst->sType = src->sType;
   dst->pNext = NULL;
   dst->flags = src->flags;
   dst->stage = src->stage;
   dst->module = src->module;
   dst->pName = src->pName;
   dst->pSpecializationInfo = NULL;
   if (src->pSpecializationInfo) {
      const VkSpecializationInfo *src_spec = src->pSpecializationInfo;
      VkSpecializationInfo *dst_spec = ralloc_size(mem_ctx, sizeof(VkSpecializationInfo) +
                                                   src_spec->mapEntryCount * sizeof(VkSpecializationMapEntry) +
                                                   src_spec->dataSize);
      VkSpecializationMapEntry *maps = (VkSpecializationMapEntry *)(dst_spec + 1);
      dst_spec->pMapEntries = maps;
      void *pdata = (void *)(dst_spec->pMapEntries + src_spec->mapEntryCount);
      dst_spec->pData = pdata;


      dst_spec->mapEntryCount = src_spec->mapEntryCount;
      dst_spec->dataSize = src_spec->dataSize;
      memcpy(pdata, src_spec->pData, src->pSpecializationInfo->dataSize);
      memcpy(maps, src_spec->pMapEntries, src_spec->mapEntryCount * sizeof(VkSpecializationMapEntry));
      dst->pSpecializationInfo = dst_spec;
   }
   return VK_SUCCESS;
}

static VkResult
deep_copy_vertex_input_state(void *mem_ctx,
                             struct VkPipelineVertexInputStateCreateInfo *dst,
                             const struct VkPipelineVertexInputStateCreateInfo *src)
{
   dst->sType = src->sType;
   dst->pNext = NULL;
   dst->flags = src->flags;
   dst->vertexBindingDescriptionCount = src->vertexBindingDescriptionCount;

   LVP_PIPELINE_DUP(dst->pVertexBindingDescriptions,
                    src->pVertexBindingDescriptions,
                    VkVertexInputBindingDescription,
                    src->vertexBindingDescriptionCount);

   dst->vertexAttributeDescriptionCount = src->vertexAttributeDescriptionCount;

   LVP_PIPELINE_DUP(dst->pVertexAttributeDescriptions,
                    src->pVertexAttributeDescriptions,
                    VkVertexInputAttributeDescription,
                    src->vertexAttributeDescriptionCount);

   if (src->pNext) {
      vk_foreach_struct(ext, src->pNext) {
         switch (ext->sType) {
         case VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_DIVISOR_STATE_CREATE_INFO_EXT: {
            VkPipelineVertexInputDivisorStateCreateInfoEXT *ext_src = (VkPipelineVertexInputDivisorStateCreateInfoEXT *)ext;
            VkPipelineVertexInputDivisorStateCreateInfoEXT *ext_dst = ralloc(mem_ctx, VkPipelineVertexInputDivisorStateCreateInfoEXT);

            ext_dst->sType = ext_src->sType;
            ext_dst->vertexBindingDivisorCount = ext_src->vertexBindingDivisorCount;

            LVP_PIPELINE_DUP(ext_dst->pVertexBindingDivisors,
                             ext_src->pVertexBindingDivisors,
                             VkVertexInputBindingDivisorDescriptionEXT,
                             ext_src->vertexBindingDivisorCount);

            dst->pNext = ext_dst;
            break;
         }
         default:
            break;
         }
      }
   }
   return VK_SUCCESS;
}

static bool
dynamic_state_contains(const VkPipelineDynamicStateCreateInfo *src, VkDynamicState state)
{
   if (!src)
      return false;

   for (unsigned i = 0; i < src->dynamicStateCount; i++)
      if (src->pDynamicStates[i] == state)
         return true;
   return false;
}

static VkResult
deep_copy_viewport_state(void *mem_ctx,
                         const VkPipelineDynamicStateCreateInfo *dyn_state,
                         VkPipelineViewportStateCreateInfo *dst,
                         const VkPipelineViewportStateCreateInfo *src)
{
   dst->sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
   dst->pNext = NULL;
   dst->pViewports = NULL;
   dst->pScissors = NULL;

   if (!dynamic_state_contains(dyn_state, VK_DYNAMIC_STATE_VIEWPORT) &&
       !dynamic_state_contains(dyn_state, VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT_EXT)) {
      LVP_PIPELINE_DUP(dst->pViewports,
                       src->pViewports,
                       VkViewport,
                       src->viewportCount);
   }
   if (!dynamic_state_contains(dyn_state, VK_DYNAMIC_STATE_VIEWPORT_WITH_COUNT_EXT))
      dst->viewportCount = src->viewportCount;
   else
      dst->viewportCount = 0;

   if (!dynamic_state_contains(dyn_state, VK_DYNAMIC_STATE_SCISSOR) &&
       !dynamic_state_contains(dyn_state, VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT_EXT)) {
      if (src->pScissors)
         LVP_PIPELINE_DUP(dst->pScissors,
                          src->pScissors,
                          VkRect2D,
                          src->scissorCount);
   }
   if (!dynamic_state_contains(dyn_state, VK_DYNAMIC_STATE_SCISSOR_WITH_COUNT_EXT))
      dst->scissorCount = src->scissorCount;
   else
      dst->scissorCount = 0;

   return VK_SUCCESS;
}

static VkResult
deep_copy_color_blend_state(void *mem_ctx,
                            VkPipelineColorBlendStateCreateInfo *dst,
                            const VkPipelineColorBlendStateCreateInfo *src)
{
   dst->sType = src->sType;
   dst->pNext = NULL;
   dst->flags = src->flags;
   dst->logicOpEnable = src->logicOpEnable;
   dst->logicOp = src->logicOp;

   LVP_PIPELINE_DUP(dst->pAttachments,
                    src->pAttachments,
                    VkPipelineColorBlendAttachmentState,
                    src->attachmentCount);
   dst->attachmentCount = src->attachmentCount;

   memcpy(&dst->blendConstants, &src->blendConstants, sizeof(float) * 4);

   return VK_SUCCESS;
}

static VkResult
deep_copy_dynamic_state(void *mem_ctx,
                        VkPipelineDynamicStateCreateInfo *dst,
                        const VkPipelineDynamicStateCreateInfo *src)
{
   dst->sType = src->sType;
   dst->pNext = NULL;
   dst->flags = src->flags;

   LVP_PIPELINE_DUP(dst->pDynamicStates,
                    src->pDynamicStates,
                    VkDynamicState,
                    src->dynamicStateCount);
   dst->dynamicStateCount = src->dynamicStateCount;
   return VK_SUCCESS;
}


static VkResult
deep_copy_rasterization_state(void *mem_ctx,
                              VkPipelineRasterizationStateCreateInfo *dst,
                              const VkPipelineRasterizationStateCreateInfo *src)
{
   memcpy(dst, src, sizeof(VkPipelineRasterizationStateCreateInfo));
   dst->pNext = NULL;

   if (src->pNext) {
      vk_foreach_struct(ext, src->pNext) {
         switch (ext->sType) {
         case VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_DEPTH_CLIP_STATE_CREATE_INFO_EXT: {
            VkPipelineRasterizationDepthClipStateCreateInfoEXT *ext_src = (VkPipelineRasterizationDepthClipStateCreateInfoEXT *)ext;
            VkPipelineRasterizationDepthClipStateCreateInfoEXT *ext_dst = ralloc(mem_ctx, VkPipelineRasterizationDepthClipStateCreateInfoEXT);
            ext_dst->sType = ext_src->sType;
            ext_dst->flags = ext_src->flags;
            ext_dst->depthClipEnable = ext_src->depthClipEnable;
            dst->pNext = ext_dst;
            break;
         }
         default:
            break;
         }
      }
   }
   return VK_SUCCESS;
}

static VkResult
deep_copy_graphics_create_info(void *mem_ctx,
                               VkGraphicsPipelineCreateInfo *dst,
                               const VkGraphicsPipelineCreateInfo *src)
{
   int i;
   VkResult result;
   VkPipelineShaderStageCreateInfo *stages;
   VkPipelineVertexInputStateCreateInfo *vertex_input;
   VkPipelineRasterizationStateCreateInfo *rasterization_state;
   LVP_FROM_HANDLE(lvp_render_pass, pass, src->renderPass);
   const VkPipelineRenderingCreateInfoKHR *rp_info = vk_find_struct_const(src->pNext, PIPELINE_RENDERING_CREATE_INFO_KHR);

   dst->sType = src->sType;
   dst->pNext = NULL;
   dst->flags = src->flags;
   dst->layout = src->layout;
   dst->renderPass = src->renderPass;
   dst->subpass = src->subpass;
   dst->basePipelineHandle = src->basePipelineHandle;
   dst->basePipelineIndex = src->basePipelineIndex;

   /* pStages */
   VkShaderStageFlags stages_present = 0;
   dst->stageCount = src->stageCount;
   stages = ralloc_array(mem_ctx, VkPipelineShaderStageCreateInfo, dst->stageCount);
   for (i = 0 ; i < dst->stageCount; i++) {
      result = deep_copy_shader_stage(mem_ctx, &stages[i], &src->pStages[i]);
      if (result != VK_SUCCESS)
         return result;
      stages_present |= src->pStages[i].stage;
   }
   dst->pStages = stages;

   /* pVertexInputState */
   if (!dynamic_state_contains(src->pDynamicState, VK_DYNAMIC_STATE_VERTEX_INPUT_EXT)) {
      vertex_input = ralloc(mem_ctx, VkPipelineVertexInputStateCreateInfo);
      result = deep_copy_vertex_input_state(mem_ctx, vertex_input,
                                            src->pVertexInputState);
      if (result != VK_SUCCESS)
         return result;
      dst->pVertexInputState = vertex_input;
   } else
      dst->pVertexInputState = NULL;

   /* pInputAssemblyState */
   LVP_PIPELINE_DUP(dst->pInputAssemblyState,
                    src->pInputAssemblyState,
                    VkPipelineInputAssemblyStateCreateInfo,
                    1);

   /* pTessellationState */
   if (src->pTessellationState &&
      (stages_present & (VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)) ==
                        (VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT | VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT)) {
      LVP_PIPELINE_DUP(dst->pTessellationState,
                       src->pTessellationState,
                       VkPipelineTessellationStateCreateInfo,
                       1);
   }

   /* pViewportState */
   bool rasterization_disabled = !dynamic_state_contains(src->pDynamicState, VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE_EXT) &&
                                 src->pRasterizationState->rasterizerDiscardEnable;
   if (src->pViewportState && !rasterization_disabled) {
      VkPipelineViewportStateCreateInfo *viewport_state;
      viewport_state = ralloc(mem_ctx, VkPipelineViewportStateCreateInfo);
      if (!viewport_state)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      deep_copy_viewport_state(mem_ctx, src->pDynamicState,
			       viewport_state, src->pViewportState);
      dst->pViewportState = viewport_state;
   } else
      dst->pViewportState = NULL;

   /* pRasterizationState */
   rasterization_state = ralloc(mem_ctx, VkPipelineRasterizationStateCreateInfo);
   if (!rasterization_state)
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   deep_copy_rasterization_state(mem_ctx, rasterization_state, src->pRasterizationState);
   dst->pRasterizationState = rasterization_state;

   /* pMultisampleState */
   if (src->pMultisampleState && !rasterization_disabled) {
      VkPipelineMultisampleStateCreateInfo*   ms_state;
      ms_state = ralloc_size(mem_ctx, sizeof(VkPipelineMultisampleStateCreateInfo) + sizeof(VkSampleMask));
      if (!ms_state)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      /* does samplemask need deep copy? */
      memcpy(ms_state, src->pMultisampleState, sizeof(VkPipelineMultisampleStateCreateInfo));
      if (src->pMultisampleState->pSampleMask) {
         VkSampleMask *sample_mask = (VkSampleMask *)(ms_state + 1);
         sample_mask[0] = src->pMultisampleState->pSampleMask[0];
         ms_state->pSampleMask = sample_mask;
      }
      dst->pMultisampleState = ms_state;
   } else
      dst->pMultisampleState = NULL;

   /* pDepthStencilState */
   if (src->pDepthStencilState && !rasterization_disabled &&
       (pass ? pass->has_zs_attachment : (rp_info->depthAttachmentFormat || rp_info->stencilAttachmentFormat))) {
      LVP_PIPELINE_DUP(dst->pDepthStencilState,
                       src->pDepthStencilState,
                       VkPipelineDepthStencilStateCreateInfo,
                       1);
   } else
      dst->pDepthStencilState = NULL;

   /* pColorBlendState */
   if (src->pColorBlendState && !rasterization_disabled &&
       (pass ? pass->has_color_attachment : rp_info->colorAttachmentCount)) {
      VkPipelineColorBlendStateCreateInfo*    cb_state;

      cb_state = ralloc(mem_ctx, VkPipelineColorBlendStateCreateInfo);
      if (!cb_state)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      deep_copy_color_blend_state(mem_ctx, cb_state, src->pColorBlendState);
      dst->pColorBlendState = cb_state;
   } else
      dst->pColorBlendState = NULL;

   if (src->pDynamicState) {
      VkPipelineDynamicStateCreateInfo*       dyn_state;

      /* pDynamicState */
      dyn_state = ralloc(mem_ctx, VkPipelineDynamicStateCreateInfo);
      if (!dyn_state)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      deep_copy_dynamic_state(mem_ctx, dyn_state, src->pDynamicState);
      dst->pDynamicState = dyn_state;
   } else
      dst->pDynamicState = NULL;

   return VK_SUCCESS;
}

static VkResult
deep_copy_compute_create_info(void *mem_ctx,
                              VkComputePipelineCreateInfo *dst,
                              const VkComputePipelineCreateInfo *src)
{
   VkResult result;
   dst->sType = src->sType;
   dst->pNext = NULL;
   dst->flags = src->flags;
   dst->layout = src->layout;
   dst->basePipelineHandle = src->basePipelineHandle;
   dst->basePipelineIndex = src->basePipelineIndex;

   result = deep_copy_shader_stage(mem_ctx, &dst->stage, &src->stage);
   if (result != VK_SUCCESS)
      return result;
   return VK_SUCCESS;
}

static inline unsigned
st_shader_stage_to_ptarget(gl_shader_stage stage)
{
   switch (stage) {
   case MESA_SHADER_VERTEX:
      return PIPE_SHADER_VERTEX;
   case MESA_SHADER_FRAGMENT:
      return PIPE_SHADER_FRAGMENT;
   case MESA_SHADER_GEOMETRY:
      return PIPE_SHADER_GEOMETRY;
   case MESA_SHADER_TESS_CTRL:
      return PIPE_SHADER_TESS_CTRL;
   case MESA_SHADER_TESS_EVAL:
      return PIPE_SHADER_TESS_EVAL;
   case MESA_SHADER_COMPUTE:
      return PIPE_SHADER_COMPUTE;
   default:
      break;
   }

   assert(!"should not be reached");
   return PIPE_SHADER_VERTEX;
}

static void
shared_var_info(const struct glsl_type *type, unsigned *size, unsigned *align)
{
   assert(glsl_type_is_vector_or_scalar(type));

   uint32_t comp_size = glsl_type_is_boolean(type)
      ? 4 : glsl_get_bit_size(type) / 8;
   unsigned length = glsl_get_vector_elements(type);
   *size = comp_size * length,
      *align = comp_size;
}

static void
lvp_shader_compile_to_ir(struct lvp_pipeline *pipeline,
                         struct vk_shader_module *module,
                         const char *entrypoint_name,
                         gl_shader_stage stage,
                         const VkSpecializationInfo *spec_info)
{
   nir_shader *nir;
   const nir_shader_compiler_options *drv_options = pipeline->device->pscreen->get_compiler_options(pipeline->device->pscreen, PIPE_SHADER_IR_NIR, st_shader_stage_to_ptarget(stage));
   bool progress;
   uint32_t *spirv = (uint32_t *) module->data;
   assert(spirv[0] == SPIR_V_MAGIC_NUMBER);
   assert(module->size % 4 == 0);

   uint32_t num_spec_entries = 0;
   struct nir_spirv_specialization *spec_entries =
      vk_spec_info_to_nir_spirv(spec_info, &num_spec_entries);

   struct lvp_device *pdevice = pipeline->device;
   const struct spirv_to_nir_options spirv_options = {
      .environment = NIR_SPIRV_VULKAN,
      .caps = {
         .float64 = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_DOUBLES) == 1),
         .int16 = true,
         .int64 = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_INT64) == 1),
         .tessellation = true,
         .float_controls = true,
         .image_ms_array = true,
         .image_read_without_format = true,
         .image_write_without_format = true,
         .storage_image_ms = true,
         .geometry_streams = true,
         .storage_8bit = true,
         .storage_16bit = true,
         .variable_pointers = true,
         .stencil_export = true,
         .post_depth_coverage = true,
         .transform_feedback = true,
         .device_group = true,
         .draw_parameters = true,
         .shader_viewport_index_layer = true,
         .multiview = true,
         .physical_storage_buffer_address = true,
         .int64_atomics = true,
         .subgroup_arithmetic = true,
         .subgroup_basic = true,
         .subgroup_ballot = true,
         .subgroup_quad = true,
         .subgroup_vote = true,
         .int8 = true,
         .float16 = true,
      },
      .ubo_addr_format = nir_address_format_32bit_index_offset,
      .ssbo_addr_format = nir_address_format_32bit_index_offset,
      .phys_ssbo_addr_format = nir_address_format_64bit_global,
      .push_const_addr_format = nir_address_format_logical,
      .shared_addr_format = nir_address_format_32bit_offset,
   };

   nir = spirv_to_nir(spirv, module->size / 4,
                      spec_entries, num_spec_entries,
                      stage, entrypoint_name, &spirv_options, drv_options);

   if (!nir) {
      free(spec_entries);
      return;
   }
   nir_validate_shader(nir, NULL);

   free(spec_entries);

   const struct nir_lower_sysvals_to_varyings_options sysvals_to_varyings = {
      .frag_coord = true,
      .point_coord = true,
   };
   NIR_PASS_V(nir, nir_lower_sysvals_to_varyings, &sysvals_to_varyings);

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

   NIR_PASS_V(nir, nir_lower_variable_initializers, ~0);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_split_per_member_structs);

   NIR_PASS_V(nir, nir_remove_dead_variables,
              nir_var_shader_in | nir_var_shader_out | nir_var_system_value, NULL);

   if (stage == MESA_SHADER_FRAGMENT)
      lvp_lower_input_attachments(nir, false);
   NIR_PASS_V(nir, nir_lower_system_values);
   NIR_PASS_V(nir, nir_lower_compute_system_values, NULL);

   NIR_PASS_V(nir, nir_lower_clip_cull_distance_arrays);
   NIR_PASS_V(nir, nir_remove_dead_variables,
              nir_var_uniform | nir_var_image, NULL);

   lvp_lower_pipeline_layout(pipeline->device, pipeline->layout, nir);

   NIR_PASS_V(nir, nir_lower_io_to_temporaries, nir_shader_get_entrypoint(nir), true, true);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_global_vars_to_local);

   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_push_const,
              nir_address_format_32bit_offset);

   NIR_PASS_V(nir, nir_lower_explicit_io,
              nir_var_mem_ubo | nir_var_mem_ssbo,
              nir_address_format_32bit_index_offset);

   NIR_PASS_V(nir, nir_lower_explicit_io,
              nir_var_mem_global,
              nir_address_format_64bit_global);

   if (nir->info.stage == MESA_SHADER_COMPUTE) {
      NIR_PASS_V(nir, nir_lower_vars_to_explicit_types, nir_var_mem_shared, shared_var_info);
      NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_shared, nir_address_format_32bit_offset);
   }

   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_shader_temp, NULL);

   if (nir->info.stage == MESA_SHADER_VERTEX ||
       nir->info.stage == MESA_SHADER_GEOMETRY) {
      NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, false);
   } else if (nir->info.stage == MESA_SHADER_FRAGMENT) {
      NIR_PASS_V(nir, nir_lower_io_arrays_to_elements_no_indirects, true);
   }

   do {
      progress = false;

      NIR_PASS(progress, nir, nir_lower_flrp, 32|64, true);
      NIR_PASS(progress, nir, nir_split_array_vars, nir_var_function_temp);
      NIR_PASS(progress, nir, nir_shrink_vec_array_vars, nir_var_function_temp);
      NIR_PASS(progress, nir, nir_opt_deref);
      NIR_PASS(progress, nir, nir_lower_vars_to_ssa);

      NIR_PASS(progress, nir, nir_copy_prop);
      NIR_PASS(progress, nir, nir_opt_dce);
      NIR_PASS(progress, nir, nir_opt_peephole_select, 8, true, true);

      NIR_PASS(progress, nir, nir_opt_algebraic);
      NIR_PASS(progress, nir, nir_opt_constant_folding);

      NIR_PASS(progress, nir, nir_opt_remove_phis);
      bool trivial_continues = false;
      NIR_PASS(trivial_continues, nir, nir_opt_trivial_continues);
      progress |= trivial_continues;
      if (trivial_continues) {
         /* If nir_opt_trivial_continues makes progress, then we need to clean
          * things up if we want any hope of nir_opt_if or nir_opt_loop_unroll
          * to make progress.
          */
         NIR_PASS(progress, nir, nir_copy_prop);
         NIR_PASS(progress, nir, nir_opt_dce);
         NIR_PASS(progress, nir, nir_opt_remove_phis);
      }
      NIR_PASS(progress, nir, nir_opt_if, true);
      NIR_PASS(progress, nir, nir_opt_dead_cf);
      NIR_PASS(progress, nir, nir_opt_conditional_discard);
      NIR_PASS(progress, nir, nir_opt_remove_phis);
      NIR_PASS(progress, nir, nir_opt_cse);
      NIR_PASS(progress, nir, nir_opt_undef);

      NIR_PASS(progress, nir, nir_opt_deref);
      NIR_PASS(progress, nir, nir_lower_alu_to_scalar, NULL, NULL);
   } while (progress);

   NIR_PASS_V(nir, nir_lower_var_copies);
   NIR_PASS_V(nir, nir_remove_dead_variables, nir_var_function_temp, NULL);
   NIR_PASS_V(nir, nir_opt_dce);
   nir_sweep(nir);

   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));

   if (nir->info.stage != MESA_SHADER_VERTEX)
      nir_assign_io_var_locations(nir, nir_var_shader_in, &nir->num_inputs, nir->info.stage);
   else {
      nir->num_inputs = util_last_bit64(nir->info.inputs_read);
      nir_foreach_shader_in_variable(var, nir) {
         var->data.driver_location = var->data.location - VERT_ATTRIB_GENERIC0;
      }
   }
   nir_assign_io_var_locations(nir, nir_var_shader_out, &nir->num_outputs,
                               nir->info.stage);
   pipeline->pipeline_nir[stage] = nir;
}

static void fill_shader_prog(struct pipe_shader_state *state, gl_shader_stage stage, struct lvp_pipeline *pipeline)
{
   state->type = PIPE_SHADER_IR_NIR;
   state->ir.nir = pipeline->pipeline_nir[stage];
}

static void
merge_tess_info(struct shader_info *tes_info,
                const struct shader_info *tcs_info)
{
   /* The Vulkan 1.0.38 spec, section 21.1 Tessellator says:
    *
    *    "PointMode. Controls generation of points rather than triangles
    *     or lines. This functionality defaults to disabled, and is
    *     enabled if either shader stage includes the execution mode.
    *
    * and about Triangles, Quads, IsoLines, VertexOrderCw, VertexOrderCcw,
    * PointMode, SpacingEqual, SpacingFractionalEven, SpacingFractionalOdd,
    * and OutputVertices, it says:
    *
    *    "One mode must be set in at least one of the tessellation
    *     shader stages."
    *
    * So, the fields can be set in either the TCS or TES, but they must
    * agree if set in both.  Our backend looks at TES, so bitwise-or in
    * the values from the TCS.
    */
   assert(tcs_info->tess.tcs_vertices_out == 0 ||
          tes_info->tess.tcs_vertices_out == 0 ||
          tcs_info->tess.tcs_vertices_out == tes_info->tess.tcs_vertices_out);
   tes_info->tess.tcs_vertices_out |= tcs_info->tess.tcs_vertices_out;

   assert(tcs_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
          tes_info->tess.spacing == TESS_SPACING_UNSPECIFIED ||
          tcs_info->tess.spacing == tes_info->tess.spacing);
   tes_info->tess.spacing |= tcs_info->tess.spacing;

   assert(tcs_info->tess._primitive_mode == 0 ||
          tes_info->tess._primitive_mode == 0 ||
          tcs_info->tess._primitive_mode == tes_info->tess._primitive_mode);
   tes_info->tess._primitive_mode |= tcs_info->tess._primitive_mode;
   tes_info->tess.ccw |= tcs_info->tess.ccw;
   tes_info->tess.point_mode |= tcs_info->tess.point_mode;
}

static gl_shader_stage
lvp_shader_stage(VkShaderStageFlagBits stage)
{
   switch (stage) {
   case VK_SHADER_STAGE_VERTEX_BIT:
      return MESA_SHADER_VERTEX;
   case VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
      return MESA_SHADER_TESS_CTRL;
   case VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
      return MESA_SHADER_TESS_EVAL;
   case VK_SHADER_STAGE_GEOMETRY_BIT:
      return MESA_SHADER_GEOMETRY;
   case VK_SHADER_STAGE_FRAGMENT_BIT:
      return MESA_SHADER_FRAGMENT;
   case VK_SHADER_STAGE_COMPUTE_BIT:
      return MESA_SHADER_COMPUTE;
   default:
      unreachable("invalid VkShaderStageFlagBits");
      return MESA_SHADER_NONE;
   }
}

static VkResult
lvp_pipeline_compile(struct lvp_pipeline *pipeline,
                     gl_shader_stage stage)
{
   struct lvp_device *device = pipeline->device;
   device->physical_device->pscreen->finalize_nir(device->physical_device->pscreen, pipeline->pipeline_nir[stage]);
   if (stage == MESA_SHADER_COMPUTE) {
      struct pipe_compute_state shstate = {0};
      shstate.prog = (void *)pipeline->pipeline_nir[MESA_SHADER_COMPUTE];
      shstate.ir_type = PIPE_SHADER_IR_NIR;
      shstate.req_local_mem = pipeline->pipeline_nir[MESA_SHADER_COMPUTE]->info.shared_size;
      pipeline->shader_cso[PIPE_SHADER_COMPUTE] = device->queue.ctx->create_compute_state(device->queue.ctx, &shstate);
   } else {
      struct pipe_shader_state shstate = {0};
      fill_shader_prog(&shstate, stage, pipeline);

      if (stage == MESA_SHADER_VERTEX ||
          stage == MESA_SHADER_GEOMETRY ||
          stage == MESA_SHADER_TESS_EVAL) {
         nir_xfb_info *xfb_info = nir_gather_xfb_info(pipeline->pipeline_nir[stage], NULL);
         if (xfb_info) {
            uint8_t output_mapping[VARYING_SLOT_TESS_MAX];
            memset(output_mapping, 0, sizeof(output_mapping));

            nir_foreach_shader_out_variable(var, pipeline->pipeline_nir[stage]) {
               unsigned slots = var->data.compact ? DIV_ROUND_UP(glsl_get_length(var->type), 4)
                                                  : glsl_count_attribute_slots(var->type, false);
               for (unsigned i = 0; i < slots; i++)
                  output_mapping[var->data.location + i] = var->data.driver_location + i;
            }

            shstate.stream_output.num_outputs = xfb_info->output_count;
            for (unsigned i = 0; i < PIPE_MAX_SO_BUFFERS; i++) {
               if (xfb_info->buffers_written & (1 << i)) {
                  shstate.stream_output.stride[i] = xfb_info->buffers[i].stride / 4;
               }
            }
            for (unsigned i = 0; i < xfb_info->output_count; i++) {
               shstate.stream_output.output[i].output_buffer = xfb_info->outputs[i].buffer;
               shstate.stream_output.output[i].dst_offset = xfb_info->outputs[i].offset / 4;
               shstate.stream_output.output[i].register_index = output_mapping[xfb_info->outputs[i].location];
               shstate.stream_output.output[i].num_components = util_bitcount(xfb_info->outputs[i].component_mask);
               shstate.stream_output.output[i].start_component = ffs(xfb_info->outputs[i].component_mask) - 1;
               shstate.stream_output.output[i].stream = xfb_info->buffer_to_stream[xfb_info->outputs[i].buffer];
            }

            ralloc_free(xfb_info);
         }
      }

      switch (stage) {
      case MESA_SHADER_FRAGMENT:
         pipeline->shader_cso[PIPE_SHADER_FRAGMENT] = device->queue.ctx->create_fs_state(device->queue.ctx, &shstate);
         break;
      case MESA_SHADER_VERTEX:
         pipeline->shader_cso[PIPE_SHADER_VERTEX] = device->queue.ctx->create_vs_state(device->queue.ctx, &shstate);
         break;
      case MESA_SHADER_GEOMETRY:
         pipeline->shader_cso[PIPE_SHADER_GEOMETRY] = device->queue.ctx->create_gs_state(device->queue.ctx, &shstate);
         break;
      case MESA_SHADER_TESS_CTRL:
         pipeline->shader_cso[PIPE_SHADER_TESS_CTRL] = device->queue.ctx->create_tcs_state(device->queue.ctx, &shstate);
         break;
      case MESA_SHADER_TESS_EVAL:
         pipeline->shader_cso[PIPE_SHADER_TESS_EVAL] = device->queue.ctx->create_tes_state(device->queue.ctx, &shstate);
         break;
      default:
         unreachable("illegal shader");
         break;
      }
   }
   return VK_SUCCESS;
}

static VkResult
lvp_graphics_pipeline_init(struct lvp_pipeline *pipeline,
                           struct lvp_device *device,
                           struct lvp_pipeline_cache *cache,
                           const VkGraphicsPipelineCreateInfo *pCreateInfo,
                           const VkAllocationCallbacks *alloc)
{
   if (alloc == NULL)
      alloc = &device->vk.alloc;
   pipeline->device = device;
   pipeline->layout = lvp_pipeline_layout_from_handle(pCreateInfo->layout);
   pipeline->force_min_sample = false;

   pipeline->mem_ctx = ralloc_context(NULL);
   /* recreate createinfo */
   deep_copy_graphics_create_info(pipeline->mem_ctx, &pipeline->graphics_create_info, pCreateInfo);
   pipeline->is_compute_pipeline = false;

   const VkPipelineRasterizationProvokingVertexStateCreateInfoEXT *pv_state =
      vk_find_struct_const(pCreateInfo->pRasterizationState,
                           PIPELINE_RASTERIZATION_PROVOKING_VERTEX_STATE_CREATE_INFO_EXT);
   pipeline->provoking_vertex_last = pv_state && pv_state->provokingVertexMode == VK_PROVOKING_VERTEX_MODE_LAST_VERTEX_EXT;

   const VkPipelineRasterizationLineStateCreateInfoEXT *line_state =
      vk_find_struct_const(pCreateInfo->pRasterizationState,
                           PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT);
   if (line_state) {
      /* always draw bresenham if !smooth */
      pipeline->line_stipple_enable = line_state->stippledLineEnable;
      pipeline->line_smooth = line_state->lineRasterizationMode == VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT;
      pipeline->disable_multisample = line_state->lineRasterizationMode == VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT ||
                                      line_state->lineRasterizationMode == VK_LINE_RASTERIZATION_MODE_RECTANGULAR_SMOOTH_EXT;
      pipeline->line_rectangular = line_state->lineRasterizationMode != VK_LINE_RASTERIZATION_MODE_BRESENHAM_EXT;
      if (pipeline->line_stipple_enable) {
         if (!dynamic_state_contains(pipeline->graphics_create_info.pDynamicState, VK_DYNAMIC_STATE_LINE_STIPPLE_EXT)) {
            pipeline->line_stipple_factor = line_state->lineStippleFactor - 1;
            pipeline->line_stipple_pattern = line_state->lineStipplePattern;
         } else {
            pipeline->line_stipple_factor = 0;
            pipeline->line_stipple_pattern = UINT16_MAX;
         }
      }
   } else
      pipeline->line_rectangular = true;

   bool rasterization_disabled = !dynamic_state_contains(pipeline->graphics_create_info.pDynamicState, VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE_EXT) &&
      pipeline->graphics_create_info.pRasterizationState->rasterizerDiscardEnable;
   if (!dynamic_state_contains(pipeline->graphics_create_info.pDynamicState, VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT) &&
       !rasterization_disabled) {
      const VkPipelineColorWriteCreateInfoEXT *cw_state =
         vk_find_struct_const(pCreateInfo->pColorBlendState, PIPELINE_COLOR_WRITE_CREATE_INFO_EXT);
      if (cw_state) {
         assert(cw_state->attachmentCount <= pipeline->graphics_create_info.pColorBlendState->attachmentCount);
         for (unsigned i = 0; i < cw_state->attachmentCount; i++)
            if (!cw_state->pColorWriteEnables[i]) {
               VkPipelineColorBlendAttachmentState *att = (void*)&pipeline->graphics_create_info.pColorBlendState->pAttachments[i];
               att->colorWriteMask = 0;
            }
      }
   }


   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      VK_FROM_HANDLE(vk_shader_module, module,
                      pCreateInfo->pStages[i].module);
      gl_shader_stage stage = lvp_shader_stage(pCreateInfo->pStages[i].stage);
      lvp_shader_compile_to_ir(pipeline, module,
                               pCreateInfo->pStages[i].pName,
                               stage,
                               pCreateInfo->pStages[i].pSpecializationInfo);
      if (!pipeline->pipeline_nir[stage])
         return VK_ERROR_FEATURE_NOT_PRESENT;
   }

   if (pipeline->pipeline_nir[MESA_SHADER_FRAGMENT]) {
      if (pipeline->pipeline_nir[MESA_SHADER_FRAGMENT]->info.fs.uses_sample_qualifier ||
          BITSET_TEST(pipeline->pipeline_nir[MESA_SHADER_FRAGMENT]->info.system_values_read, SYSTEM_VALUE_SAMPLE_ID) ||
          BITSET_TEST(pipeline->pipeline_nir[MESA_SHADER_FRAGMENT]->info.system_values_read, SYSTEM_VALUE_SAMPLE_POS))
         pipeline->force_min_sample = true;
   }
   if (pipeline->pipeline_nir[MESA_SHADER_TESS_CTRL]) {
      nir_lower_patch_vertices(pipeline->pipeline_nir[MESA_SHADER_TESS_EVAL], pipeline->pipeline_nir[MESA_SHADER_TESS_CTRL]->info.tess.tcs_vertices_out, NULL);
      merge_tess_info(&pipeline->pipeline_nir[MESA_SHADER_TESS_EVAL]->info, &pipeline->pipeline_nir[MESA_SHADER_TESS_CTRL]->info);
      const VkPipelineTessellationDomainOriginStateCreateInfo *domain_origin_state =
         vk_find_struct_const(pCreateInfo->pTessellationState,
                              PIPELINE_TESSELLATION_DOMAIN_ORIGIN_STATE_CREATE_INFO);
      if (!domain_origin_state || domain_origin_state->domainOrigin == VK_TESSELLATION_DOMAIN_ORIGIN_UPPER_LEFT)
         pipeline->pipeline_nir[MESA_SHADER_TESS_EVAL]->info.tess.ccw = !pipeline->pipeline_nir[MESA_SHADER_TESS_EVAL]->info.tess.ccw;
   }

   pipeline->gs_output_lines = pipeline->pipeline_nir[MESA_SHADER_GEOMETRY] &&
                               pipeline->pipeline_nir[MESA_SHADER_GEOMETRY]->info.gs.output_primitive == SHADER_PRIM_LINES;


   bool has_fragment_shader = false;
   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      gl_shader_stage stage = lvp_shader_stage(pCreateInfo->pStages[i].stage);
      lvp_pipeline_compile(pipeline, stage);
      if (stage == MESA_SHADER_FRAGMENT)
         has_fragment_shader = true;
   }

   if (has_fragment_shader == false) {
      /* create a dummy fragment shader for this pipeline. */
      nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, NULL,
                                                     "dummy_frag");

      pipeline->pipeline_nir[MESA_SHADER_FRAGMENT] = b.shader;
      struct pipe_shader_state shstate = {0};
      shstate.type = PIPE_SHADER_IR_NIR;
      shstate.ir.nir = pipeline->pipeline_nir[MESA_SHADER_FRAGMENT];
      pipeline->shader_cso[PIPE_SHADER_FRAGMENT] = device->queue.ctx->create_fs_state(device->queue.ctx, &shstate);
   }
   return VK_SUCCESS;
}

static VkResult
lvp_graphics_pipeline_create(
   VkDevice _device,
   VkPipelineCache _cache,
   const VkGraphicsPipelineCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator,
   VkPipeline *pPipeline)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_pipeline_cache, cache, _cache);
   struct lvp_pipeline *pipeline;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);

   pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &pipeline->base,
                       VK_OBJECT_TYPE_PIPELINE);
   result = lvp_graphics_pipeline_init(pipeline, device, cache, pCreateInfo,
                                       pAllocator);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, pipeline);
      return result;
   }

   *pPipeline = lvp_pipeline_to_handle(pipeline);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateGraphicsPipelines(
   VkDevice                                    _device,
   VkPipelineCache                             pipelineCache,
   uint32_t                                    count,
   const VkGraphicsPipelineCreateInfo*         pCreateInfos,
   const VkAllocationCallbacks*                pAllocator,
   VkPipeline*                                 pPipelines)
{
   VkResult result = VK_SUCCESS;
   unsigned i = 0;

   for (; i < count; i++) {
      VkResult r;
      r = lvp_graphics_pipeline_create(_device,
                                       pipelineCache,
                                       &pCreateInfos[i],
                                       pAllocator, &pPipelines[i]);
      if (r != VK_SUCCESS) {
         result = r;
         pPipelines[i] = VK_NULL_HANDLE;
      }
   }

   return result;
}

static VkResult
lvp_compute_pipeline_init(struct lvp_pipeline *pipeline,
                          struct lvp_device *device,
                          struct lvp_pipeline_cache *cache,
                          const VkComputePipelineCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *alloc)
{
   VK_FROM_HANDLE(vk_shader_module, module,
                   pCreateInfo->stage.module);
   if (alloc == NULL)
      alloc = &device->vk.alloc;
   pipeline->device = device;
   pipeline->layout = lvp_pipeline_layout_from_handle(pCreateInfo->layout);
   pipeline->force_min_sample = false;

   pipeline->mem_ctx = ralloc_context(NULL);
   deep_copy_compute_create_info(pipeline->mem_ctx,
                                 &pipeline->compute_create_info, pCreateInfo);
   pipeline->is_compute_pipeline = true;

   lvp_shader_compile_to_ir(pipeline, module,
                            pCreateInfo->stage.pName,
                            MESA_SHADER_COMPUTE,
                            pCreateInfo->stage.pSpecializationInfo);
   if (!pipeline->pipeline_nir[MESA_SHADER_COMPUTE])
      return VK_ERROR_FEATURE_NOT_PRESENT;
   lvp_pipeline_compile(pipeline, MESA_SHADER_COMPUTE);
   return VK_SUCCESS;
}

static VkResult
lvp_compute_pipeline_create(
   VkDevice _device,
   VkPipelineCache _cache,
   const VkComputePipelineCreateInfo *pCreateInfo,
   const VkAllocationCallbacks *pAllocator,
   VkPipeline *pPipeline)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_pipeline_cache, cache, _cache);
   struct lvp_pipeline *pipeline;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);

   pipeline = vk_zalloc2(&device->vk.alloc, pAllocator, sizeof(*pipeline), 8,
                         VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pipeline == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &pipeline->base,
                       VK_OBJECT_TYPE_PIPELINE);
   result = lvp_compute_pipeline_init(pipeline, device, cache, pCreateInfo,
                                      pAllocator);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, pipeline);
      return result;
   }

   *pPipeline = lvp_pipeline_to_handle(pipeline);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL lvp_CreateComputePipelines(
   VkDevice                                    _device,
   VkPipelineCache                             pipelineCache,
   uint32_t                                    count,
   const VkComputePipelineCreateInfo*          pCreateInfos,
   const VkAllocationCallbacks*                pAllocator,
   VkPipeline*                                 pPipelines)
{
   VkResult result = VK_SUCCESS;
   unsigned i = 0;

   for (; i < count; i++) {
      VkResult r;
      r = lvp_compute_pipeline_create(_device,
                                      pipelineCache,
                                      &pCreateInfos[i],
                                      pAllocator, &pPipelines[i]);
      if (r != VK_SUCCESS) {
         result = r;
         pPipelines[i] = VK_NULL_HANDLE;
      }
   }

   return result;
}
