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

VkResult lvp_CreateShaderModule(
   VkDevice                                    _device,
   const VkShaderModuleCreateInfo*             pCreateInfo,
   const VkAllocationCallbacks*                pAllocator,
   VkShaderModule*                             pShaderModule)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   struct lvp_shader_module *module;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
   assert(pCreateInfo->flags == 0);

   module = vk_alloc2(&device->vk.alloc, pAllocator,
                      sizeof(*module) + pCreateInfo->codeSize, 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (module == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &module->base,
                       VK_OBJECT_TYPE_SHADER_MODULE);
   module->size = pCreateInfo->codeSize;
   memcpy(module->data, pCreateInfo->pCode, module->size);

   *pShaderModule = lvp_shader_module_to_handle(module);

   return VK_SUCCESS;

}

void lvp_DestroyShaderModule(
   VkDevice                                    _device,
   VkShaderModule                              _module,
   const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_shader_module, module, _module);

   if (!_module)
      return;
   vk_object_base_finish(&module->base);
   vk_free2(&device->vk.alloc, pAllocator, module);
}

void lvp_DestroyPipeline(
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
            VkPipelineVertexInputDivisorStateCreateInfoEXT *ext_src = (VkPipelineVertexInputDivisorStateCreateInfoEXT *)ext;;
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

static VkResult
deep_copy_viewport_state(void *mem_ctx,
                         VkPipelineViewportStateCreateInfo *dst,
                         const VkPipelineViewportStateCreateInfo *src)
{
   dst->sType = src->sType;
   dst->pNext = NULL;
   dst->flags = src->flags;

   if (src->pViewports) {
      LVP_PIPELINE_DUP(dst->pViewports,
                       src->pViewports,
                       VkViewport,
                       src->viewportCount);
   } else
      dst->pViewports = NULL;
   dst->viewportCount = src->viewportCount;

   if (src->pScissors) {
      LVP_PIPELINE_DUP(dst->pScissors,
                       src->pScissors,
                       VkRect2D,
                       src->viewportCount);
   } else
      dst->pScissors = NULL;
   dst->scissorCount = src->scissorCount;

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
deep_copy_graphics_create_info(void *mem_ctx,
                               VkGraphicsPipelineCreateInfo *dst,
                               const VkGraphicsPipelineCreateInfo *src)
{
   int i;
   VkResult result;
   VkPipelineShaderStageCreateInfo *stages;
   VkPipelineVertexInputStateCreateInfo *vertex_input;

   dst->sType = src->sType;
   dst->pNext = NULL;
   dst->flags = src->flags;
   dst->layout = src->layout;
   dst->renderPass = src->renderPass;
   dst->subpass = src->subpass;
   dst->basePipelineHandle = src->basePipelineHandle;
   dst->basePipelineIndex = src->basePipelineIndex;

   /* pStages */
   dst->stageCount = src->stageCount;
   stages = ralloc_array(mem_ctx, VkPipelineShaderStageCreateInfo, dst->stageCount);
   for (i = 0 ; i < dst->stageCount; i++) {
      result = deep_copy_shader_stage(mem_ctx, &stages[i], &src->pStages[i]);
      if (result != VK_SUCCESS)
         return result;
   }
   dst->pStages = stages;

   /* pVertexInputState */
   vertex_input = ralloc(mem_ctx, VkPipelineVertexInputStateCreateInfo);
   result = deep_copy_vertex_input_state(mem_ctx, vertex_input,
                                         src->pVertexInputState);
   if (result != VK_SUCCESS)
      return result;
   dst->pVertexInputState = vertex_input;

   /* pInputAssemblyState */
   LVP_PIPELINE_DUP(dst->pInputAssemblyState,
                    src->pInputAssemblyState,
                    VkPipelineInputAssemblyStateCreateInfo,
                    1);

   /* pTessellationState */
   if (src->pTessellationState) {
      LVP_PIPELINE_DUP(dst->pTessellationState,
                       src->pTessellationState,
                       VkPipelineTessellationStateCreateInfo,
                       1);
   }

   /* pViewportState */
   if (src->pViewportState) {
      VkPipelineViewportStateCreateInfo *viewport_state;
      viewport_state = ralloc(mem_ctx, VkPipelineViewportStateCreateInfo);
      if (!viewport_state)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      deep_copy_viewport_state(mem_ctx, viewport_state, src->pViewportState);
      dst->pViewportState = viewport_state;
   } else
      dst->pViewportState = NULL;

   /* pRasterizationState */
   LVP_PIPELINE_DUP(dst->pRasterizationState,
                    src->pRasterizationState,
                    VkPipelineRasterizationStateCreateInfo,
                    1);

   /* pMultisampleState */
   if (src->pMultisampleState) {
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
   if (src->pDepthStencilState) {
      LVP_PIPELINE_DUP(dst->pDepthStencilState,
                       src->pDepthStencilState,
                       VkPipelineDepthStencilStateCreateInfo,
                       1);
   } else
      dst->pDepthStencilState = NULL;

   /* pColorBlendState */
   if (src->pColorBlendState) {
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

#define OPT(pass, ...) ({                                       \
         bool this_progress = false;                            \
         NIR_PASS(this_progress, nir, pass, ##__VA_ARGS__);     \
         if (this_progress)                                     \
            progress = true;                                    \
         this_progress;                                         \
      })

static void
lvp_shader_compile_to_ir(struct lvp_pipeline *pipeline,
                         struct lvp_shader_module *module,
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
   struct nir_spirv_specialization *spec_entries = NULL;
   if (spec_info && spec_info->mapEntryCount > 0) {
      num_spec_entries = spec_info->mapEntryCount;
      spec_entries = calloc(num_spec_entries, sizeof(*spec_entries));
      for (uint32_t i = 0; i < num_spec_entries; i++) {
         VkSpecializationMapEntry entry = spec_info->pMapEntries[i];
         const void *data =
            spec_info->pData + entry.offset;
         assert((const void *)(data + entry.size) <=
                spec_info->pData + spec_info->dataSize);

         spec_entries[i].id = entry.constantID;
         switch (entry.size) {
         case 8:
            spec_entries[i].value.u64 = *(const uint64_t *)data;
            break;
         case 4:
            spec_entries[i].value.u32 = *(const uint32_t *)data;
            break;
         case 2:
            spec_entries[i].value.u16 = *(const uint16_t *)data;
            break;
         case 1:
            spec_entries[i].value.u8 = *(const uint8_t *)data;
            break;
         default:
            assert(!"Invalid spec constant size");
            break;
         }
      }
   }
   struct lvp_device *pdevice = pipeline->device;
   const struct spirv_to_nir_options spirv_options = {
      .environment = NIR_SPIRV_VULKAN,
      .caps = {
         .float64 = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_DOUBLES) == 1),
         .int16 = true,
         .int64 = (pdevice->pscreen->get_param(pdevice->pscreen, PIPE_CAP_INT64) == 1),
         .tessellation = true,
         .image_ms_array = true,
         .image_read_without_format = true,
         .image_write_without_format = true,
         .storage_image_ms = true,
         .geometry_streams = true,
         .storage_16bit = true,
         .variable_pointers = true,
         .stencil_export = true,
         .post_depth_coverage = true,
         .transform_feedback = true,
         .geometry_streams = true,
         .device_group = true,
      },
      .ubo_addr_format = nir_address_format_32bit_index_offset,
      .ssbo_addr_format = nir_address_format_32bit_index_offset,
      .phys_ssbo_addr_format = nir_address_format_64bit_global,
      .push_const_addr_format = nir_address_format_logical,
      .shared_addr_format = nir_address_format_32bit_offset,
      .frag_coord_is_sysval = false,
   };

   nir = spirv_to_nir(spirv, module->size / 4,
                      spec_entries, num_spec_entries,
                      stage, entrypoint_name, &spirv_options, drv_options);

   nir_validate_shader(nir, NULL);

   free(spec_entries);

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
   nir_remove_dead_variables(nir, nir_var_uniform, NULL);

   lvp_lower_pipeline_layout(pipeline->device, pipeline->layout, nir);

   NIR_PASS_V(nir, nir_lower_io_to_temporaries, nir_shader_get_entrypoint(nir), true, true);
   NIR_PASS_V(nir, nir_split_var_copies);
   NIR_PASS_V(nir, nir_lower_global_vars_to_local);

   NIR_PASS_V(nir, nir_lower_explicit_io, nir_var_mem_push_const,
              nir_address_format_32bit_offset);

   NIR_PASS_V(nir, nir_lower_explicit_io,
              nir_var_mem_ubo | nir_var_mem_ssbo,
              nir_address_format_32bit_index_offset);

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

      progress |= OPT(nir_lower_flrp, 32|64, true);
      progress |= OPT(nir_split_array_vars, nir_var_function_temp);
      progress |= OPT(nir_shrink_vec_array_vars, nir_var_function_temp);
      progress |= OPT(nir_opt_deref);
      progress |= OPT(nir_lower_vars_to_ssa);

      progress |= nir_copy_prop(nir);
      progress |= nir_opt_dce(nir);
      progress |= nir_opt_dead_cf(nir);
      progress |= nir_opt_cse(nir);
      progress |= nir_opt_algebraic(nir);
      progress |= nir_opt_constant_folding(nir);
      progress |= nir_opt_undef(nir);

      progress |= nir_opt_deref(nir);
      progress |= nir_lower_alu_to_scalar(nir, NULL, NULL);
   } while (progress);

   nir_lower_var_copies(nir);
   nir_remove_dead_variables(nir, nir_var_function_temp, NULL);

   nir_validate_shader(nir, NULL);
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

   assert(tcs_info->tess.primitive_mode == 0 ||
          tes_info->tess.primitive_mode == 0 ||
          tcs_info->tess.primitive_mode == tes_info->tess.primitive_mode);
   tes_info->tess.primitive_mode |= tcs_info->tess.primitive_mode;
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
   device->physical_device->pscreen->finalize_nir(device->physical_device->pscreen, pipeline->pipeline_nir[stage], true);
   if (stage == MESA_SHADER_COMPUTE) {
      struct pipe_compute_state shstate = {};
      shstate.prog = (void *)pipeline->pipeline_nir[MESA_SHADER_COMPUTE];
      shstate.ir_type = PIPE_SHADER_IR_NIR;
      shstate.req_local_mem = pipeline->pipeline_nir[MESA_SHADER_COMPUTE]->info.cs.shared_size;
      pipeline->shader_cso[PIPE_SHADER_COMPUTE] = device->queue.ctx->create_compute_state(device->queue.ctx, &shstate);
   } else {
      struct pipe_shader_state shstate = {};
      fill_shader_prog(&shstate, stage, pipeline);

      nir_xfb_info *xfb_info = NULL;
      if (stage == MESA_SHADER_VERTEX ||
          stage == MESA_SHADER_GEOMETRY ||
          stage == MESA_SHADER_TESS_EVAL) {
         xfb_info = nir_gather_xfb_info(pipeline->pipeline_nir[stage], NULL);
         if (xfb_info) {
            unsigned num_outputs = 0;
            uint8_t output_mapping[VARYING_SLOT_TESS_MAX];
            memset(output_mapping, 0, sizeof(output_mapping));

            for (unsigned attr = 0; attr < VARYING_SLOT_MAX; attr++) {
               if (pipeline->pipeline_nir[stage]->info.outputs_written & BITFIELD64_BIT(attr))
                  output_mapping[attr] = num_outputs++;
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

   for (uint32_t i = 0; i < pCreateInfo->stageCount; i++) {
      LVP_FROM_HANDLE(lvp_shader_module, module,
                      pCreateInfo->pStages[i].module);
      gl_shader_stage stage = lvp_shader_stage(pCreateInfo->pStages[i].stage);
      lvp_shader_compile_to_ir(pipeline, module,
                               pCreateInfo->pStages[i].pName,
                               stage,
                               pCreateInfo->pStages[i].pSpecializationInfo);
   }

   if (pipeline->pipeline_nir[MESA_SHADER_FRAGMENT]) {
      if (pipeline->pipeline_nir[MESA_SHADER_FRAGMENT]->info.fs.uses_sample_qualifier ||
          pipeline->pipeline_nir[MESA_SHADER_FRAGMENT]->info.system_values_read & (SYSTEM_BIT_SAMPLE_ID |
                                                                                   SYSTEM_BIT_SAMPLE_POS))
         pipeline->force_min_sample = true;
   }
   if (pipeline->pipeline_nir[MESA_SHADER_TESS_CTRL]) {
      nir_lower_patch_vertices(pipeline->pipeline_nir[MESA_SHADER_TESS_EVAL], pipeline->pipeline_nir[MESA_SHADER_TESS_CTRL]->info.tess.tcs_vertices_out, NULL);
      merge_tess_info(&pipeline->pipeline_nir[MESA_SHADER_TESS_EVAL]->info, &pipeline->pipeline_nir[MESA_SHADER_TESS_CTRL]->info);
      pipeline->pipeline_nir[MESA_SHADER_TESS_EVAL]->info.tess.ccw = !pipeline->pipeline_nir[MESA_SHADER_TESS_EVAL]->info.tess.ccw;
   }


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
      struct pipe_shader_state shstate = {};
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
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

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

VkResult lvp_CreateGraphicsPipelines(
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
   LVP_FROM_HANDLE(lvp_shader_module, module,
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
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

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

VkResult lvp_CreateComputePipelines(
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
