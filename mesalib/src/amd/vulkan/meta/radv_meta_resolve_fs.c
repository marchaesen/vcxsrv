/*
 * Copyright © 2016 Dave Airlie
 *
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdbool.h>

#include "nir/radv_meta_nir.h"
#include "radv_entrypoints.h"
#include "radv_meta.h"
#include "vk_common_entrypoints.h"
#include "vk_format.h"

static VkResult
create_layout(struct radv_device *device, VkPipelineLayout *layout_out)
{
   enum radv_meta_object_key_type key = RADV_META_OBJECT_KEY_RESOLVE_FS;

   const VkDescriptorSetLayoutBinding binding = {.binding = 0,
                                                 .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                                 .descriptorCount = 1,
                                                 .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT};

   const VkDescriptorSetLayoutCreateInfo desc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
      .bindingCount = 1,
      .pBindings = &binding,
   };

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = 8,
   };

   return vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, &pc_range, &key, sizeof(key),
                                      layout_out);
}

struct radv_resolve_ds_fs_key {
   enum radv_meta_object_key_type type;
   uint32_t samples;
   VkImageAspectFlags aspects;
   VkResolveModeFlagBits resolve_mode;
};

static VkResult
get_depth_stencil_resolve_pipeline(struct radv_device *device, int samples, VkImageAspectFlags aspects,
                                   VkResolveModeFlagBits resolve_mode, VkPipeline *pipeline_out,
                                   VkPipelineLayout *layout_out)
{
   const enum radv_meta_resolve_type index =
      aspects == VK_IMAGE_ASPECT_DEPTH_BIT ? RADV_META_DEPTH_RESOLVE : RADV_META_STENCIL_RESOLVE;
   struct radv_resolve_ds_fs_key key;
   VkResult result;

   result = create_layout(device, layout_out);
   if (result != VK_SUCCESS)
      return result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_RESOLVE_DS_FS;
   key.samples = samples;
   key.aspects = aspects;
   key.resolve_mode = resolve_mode;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *fs_module =
      radv_meta_nir_build_depth_stencil_resolve_fragment_shader(device, samples, index, resolve_mode);
   nir_shader *vs_module = radv_meta_nir_build_vs_generate_vertices(device);

   const VkStencilOp stencil_op = index == RADV_META_DEPTH_RESOLVE ? VK_STENCIL_OP_KEEP : VK_STENCIL_OP_REPLACE;

   const VkGraphicsPipelineCreateInfo pipeline_create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = 2,
      .pStages =
         (VkPipelineShaderStageCreateInfo[]){
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = VK_SHADER_STAGE_VERTEX_BIT,
             .module = vk_shader_module_handle_from_nir(vs_module),
             .pName = "main",
             .pSpecializationInfo = NULL},
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
             .module = vk_shader_module_handle_from_nir(fs_module),
             .pName = "main",
             .pSpecializationInfo = NULL},
         },
      .pVertexInputState =
         &(VkPipelineVertexInputStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 0,
            .vertexAttributeDescriptionCount = 0,

         },
      .pInputAssemblyState =
         &(VkPipelineInputAssemblyStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_META_RECT_LIST_MESA,
            .primitiveRestartEnable = false,
         },
      .pViewportState =
         &(VkPipelineViewportStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
         },
      .pDepthStencilState =
         &(VkPipelineDepthStencilStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
                                                  .depthTestEnable = true,
                                                  .depthWriteEnable = index == RADV_META_DEPTH_RESOLVE,
                                                  .stencilTestEnable = index == RADV_META_STENCIL_RESOLVE,
                                                  .depthCompareOp = VK_COMPARE_OP_ALWAYS,
                                                  .front =
                                                     {
                                                        .failOp = stencil_op,
                                                        .passOp = stencil_op,
                                                        .depthFailOp = stencil_op,
                                                        .compareOp = VK_COMPARE_OP_ALWAYS,
                                                        .compareMask = UINT32_MAX,
                                                        .writeMask = UINT32_MAX,
                                                        .reference = 0u,
                                                     },
                                                  .back =
                                                     {
                                                        .failOp = stencil_op,
                                                        .passOp = stencil_op,
                                                        .depthFailOp = stencil_op,
                                                        .compareOp = VK_COMPARE_OP_ALWAYS,
                                                        .compareMask = UINT32_MAX,
                                                        .writeMask = UINT32_MAX,
                                                        .reference = 0u,
                                                     },
                                                  .minDepthBounds = 0.0f,
                                                  .maxDepthBounds = 1.0f},
      .pRasterizationState =
         &(VkPipelineRasterizationStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                   .rasterizerDiscardEnable = false,
                                                   .polygonMode = VK_POLYGON_MODE_FILL,
                                                   .cullMode = VK_CULL_MODE_NONE,
                                                   .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                                   .depthBiasConstantFactor = 0.0f,
                                                   .depthBiasClamp = 0.0f,
                                                   .depthBiasSlopeFactor = 0.0f,
                                                   .lineWidth = 1.0f},
      .pMultisampleState =
         &(VkPipelineMultisampleStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = 1,
            .sampleShadingEnable = false,
            .pSampleMask = (VkSampleMask[]){UINT32_MAX},
         },
      .pColorBlendState =
         &(VkPipelineColorBlendStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 0,
            .pAttachments =
               (VkPipelineColorBlendAttachmentState[]){
                  {.colorWriteMask = VK_COLOR_COMPONENT_A_BIT | VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT},
               },
            .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}},
      .pDynamicState =
         &(VkPipelineDynamicStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates =
               (VkDynamicState[]){
                  VK_DYNAMIC_STATE_VIEWPORT,
                  VK_DYNAMIC_STATE_SCISSOR,
               },
         },
      .layout = *layout_out,
   };

   struct vk_meta_rendering_info render = {
      .depth_attachment_format = index == RADV_META_DEPTH_RESOLVE ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_UNDEFINED,
      .stencil_attachment_format = index == RADV_META_STENCIL_RESOLVE ? VK_FORMAT_S8_UINT : VK_FORMAT_UNDEFINED,
   };

   result = vk_meta_create_graphics_pipeline(&device->vk, &device->meta_state.device, &pipeline_create_info, &render,
                                             &key, sizeof(key), pipeline_out);

   ralloc_free(vs_module);
   ralloc_free(fs_module);
   return result;
}

struct radv_resolve_color_fs_key {
   enum radv_meta_object_key_type type;
   uint32_t samples;
   uint32_t fs_key;
};

static VkResult
get_color_resolve_pipeline(struct radv_device *device, struct radv_image_view *src_iview,
                           struct radv_image_view *dst_iview, VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   const unsigned fs_key = radv_format_meta_fs_key(device, dst_iview->vk.format);
   const uint32_t samples = src_iview->image->vk.samples;
   const VkFormat format = radv_fs_key_format_exemplars[fs_key];
   const bool is_integer = vk_format_is_int(format);
   struct radv_resolve_color_fs_key key;
   VkResult result;

   result = create_layout(device, layout_out);
   if (result != VK_SUCCESS)
      return result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_RESOLVE_COLOR_FS;
   key.samples = samples;
   key.fs_key = fs_key;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *vs_module = radv_meta_nir_build_vs_generate_vertices(device);
   nir_shader *fs_module = radv_meta_nir_build_resolve_fragment_shader(device, is_integer, samples);

   const VkGraphicsPipelineCreateInfo pipeline_create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = 2,
      .pStages =
         (VkPipelineShaderStageCreateInfo[]){
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = VK_SHADER_STAGE_VERTEX_BIT,
             .module = vk_shader_module_handle_from_nir(vs_module),
             .pName = "main",
             .pSpecializationInfo = NULL},
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
             .module = vk_shader_module_handle_from_nir(fs_module),
             .pName = "main",
             .pSpecializationInfo = NULL},

         },
      .pVertexInputState =
         &(VkPipelineVertexInputStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 0,
            .vertexAttributeDescriptionCount = 0,
         },
      .pInputAssemblyState =
         &(VkPipelineInputAssemblyStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_META_RECT_LIST_MESA,
            .primitiveRestartEnable = false,
         },
      .pViewportState =
         &(VkPipelineViewportStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
         },
      .pRasterizationState =
         &(VkPipelineRasterizationStateCreateInfo){.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
                                                   .rasterizerDiscardEnable = false,
                                                   .polygonMode = VK_POLYGON_MODE_FILL,
                                                   .cullMode = VK_CULL_MODE_NONE,
                                                   .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
                                                   .depthBiasConstantFactor = 0.0f,
                                                   .depthBiasClamp = 0.0f,
                                                   .depthBiasSlopeFactor = 0.0f,
                                                   .lineWidth = 1.0f},
      .pMultisampleState =
         &(VkPipelineMultisampleStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = 1,
            .sampleShadingEnable = false,
            .pSampleMask = (VkSampleMask[]){UINT32_MAX},
         },
      .pColorBlendState =
         &(VkPipelineColorBlendStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments =
               (VkPipelineColorBlendAttachmentState[]){
                  {.colorWriteMask = VK_COLOR_COMPONENT_A_BIT | VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                     VK_COLOR_COMPONENT_B_BIT},
               },
            .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}},
      .pDynamicState =
         &(VkPipelineDynamicStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 2,
            .pDynamicStates =
               (VkDynamicState[]){
                  VK_DYNAMIC_STATE_VIEWPORT,
                  VK_DYNAMIC_STATE_SCISSOR,
               },
         },
      .layout = *layout_out,
   };

   struct vk_meta_rendering_info render = {
      .color_attachment_count = 1,
      .color_attachment_formats = {format},
   };

   result = vk_meta_create_graphics_pipeline(&device->vk, &device->meta_state.device, &pipeline_create_info, &render,
                                             &key, sizeof(key), pipeline_out);

   ralloc_free(vs_module);
   ralloc_free(fs_module);
   return result;
}

static void
emit_resolve(struct radv_cmd_buffer *cmd_buffer, struct radv_image_view *src_iview, struct radv_image_view *dst_iview,
             const VkOffset2D *src_offset, const VkOffset2D *dst_offset)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   VkCommandBuffer cmd_buffer_h = radv_cmd_buffer_to_handle(cmd_buffer);
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_color_resolve_pipeline(device, src_iview, dst_iview, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_meta_bind_descriptors(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1,
                              (VkDescriptorGetInfoEXT[]){
                                 {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                  .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                  .data.pSampledImage =
                                     (VkDescriptorImageInfo[]){
                                        {
                                           .sampler = VK_NULL_HANDLE,
                                           .imageView = radv_image_view_to_handle(src_iview),
                                           .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                        },
                                     }},
                              });

   const VkImageSubresourceRange src_range = vk_image_view_subresource_range(&src_iview->vk);
   const VkImageSubresourceRange dst_range = vk_image_view_subresource_range(&dst_iview->vk);

   cmd_buffer->state.flush_bits |=
      radv_dst_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_SHADER_READ_BIT, 0,
                            src_iview->image, &src_range) |
      radv_dst_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT, 0, dst_iview->image, &dst_range);

   unsigned push_constants[2] = {
      src_offset->x - dst_offset->x,
      src_offset->y - dst_offset->y,
   };
   vk_common_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer), layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8,
                              push_constants);

   radv_CmdBindPipeline(cmd_buffer_h, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

   radv_CmdDraw(cmd_buffer_h, 3, 1, 0, 0);
   cmd_buffer->state.flush_bits |=
      radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0, dst_iview->image, &dst_range);
}

static void
emit_depth_stencil_resolve(struct radv_cmd_buffer *cmd_buffer, struct radv_image_view *src_iview,
                           struct radv_image_view *dst_iview, const VkOffset2D *resolve_offset,
                           const VkExtent2D *resolve_extent, VkImageAspectFlags aspects,
                           VkResolveModeFlagBits resolve_mode)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const uint32_t samples = src_iview->image->vk.samples;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_depth_stencil_resolve_pipeline(device, samples, aspects, resolve_mode, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_meta_bind_descriptors(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 1,
                              (VkDescriptorGetInfoEXT[]){
                                 {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                  .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                  .data.pSampledImage =
                                     (VkDescriptorImageInfo[]){
                                        {
                                           .sampler = VK_NULL_HANDLE,
                                           .imageView = radv_image_view_to_handle(src_iview),
                                           .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                        },
                                     }},
                              });

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

   radv_CmdSetViewport(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1,
                       &(VkViewport){.x = resolve_offset->x,
                                     .y = resolve_offset->y,
                                     .width = resolve_extent->width,
                                     .height = resolve_extent->height,
                                     .minDepth = 0.0f,
                                     .maxDepth = 1.0f});

   radv_CmdSetScissor(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1,
                      &(VkRect2D){
                         .offset = *resolve_offset,
                         .extent = *resolve_extent,
                      });

   radv_CmdDraw(radv_cmd_buffer_to_handle(cmd_buffer), 3, 1, 0, 0);
}

void
radv_meta_resolve_fragment_image(struct radv_cmd_buffer *cmd_buffer, struct radv_image *src_image,
                                 VkImageLayout src_image_layout, struct radv_image *dst_image,
                                 VkImageLayout dst_image_layout, const VkImageResolve2 *region)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_meta_saved_state saved_state;
   unsigned dst_layout = radv_meta_dst_layout_from_layout(dst_image_layout);
   VkImageLayout layout = radv_meta_dst_layout_to_layout(dst_layout);

   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_GRAPHICS_PIPELINE | RADV_META_SAVE_CONSTANTS | RADV_META_SAVE_DESCRIPTORS);

   assert(region->srcSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
   assert(region->dstSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
   /* Multi-layer resolves are handled by compute */
   assert(vk_image_subresource_layer_count(&src_image->vk, &region->srcSubresource) == 1 &&
          vk_image_subresource_layer_count(&dst_image->vk, &region->dstSubresource) == 1);

   const struct VkExtent3D extent = vk_image_sanitize_extent(&src_image->vk, region->extent);
   const struct VkOffset3D srcOffset = vk_image_sanitize_offset(&src_image->vk, region->srcOffset);
   const struct VkOffset3D dstOffset = vk_image_sanitize_offset(&dst_image->vk, region->dstOffset);

   VkRect2D resolve_area = {
      .offset = {dstOffset.x, dstOffset.y},
      .extent = {extent.width, extent.height},
   };

   radv_CmdSetViewport(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1,
                       &(VkViewport){.x = resolve_area.offset.x,
                                     .y = resolve_area.offset.y,
                                     .width = resolve_area.extent.width,
                                     .height = resolve_area.extent.height,
                                     .minDepth = 0.0f,
                                     .maxDepth = 1.0f});

   radv_CmdSetScissor(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1, &resolve_area);

   struct radv_image_view src_iview;
   radv_image_view_init(&src_iview, device,
                        &(VkImageViewCreateInfo){
                           .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                           .image = radv_image_to_handle(src_image),
                           .viewType = VK_IMAGE_VIEW_TYPE_2D,
                           .format = src_image->vk.format,
                           .subresourceRange =
                              {
                                 .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1,
                              },
                        },
                        NULL);

   struct radv_image_view dst_iview;
   radv_image_view_init(&dst_iview, device,
                        &(VkImageViewCreateInfo){
                           .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                           .image = radv_image_to_handle(dst_image),
                           .viewType = radv_meta_get_view_type(dst_image),
                           .format = dst_image->vk.format,
                           .subresourceRange =
                              {
                                 .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = region->dstSubresource.mipLevel,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1,
                              },
                        },
                        NULL);

   const VkRenderingAttachmentInfo color_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = radv_image_view_to_handle(&dst_iview),
      .imageLayout = layout,
      .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
   };

   const VkRenderingInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .flags = VK_RENDERING_INPUT_ATTACHMENT_NO_CONCURRENT_WRITES_BIT_MESA,
      .renderArea = resolve_area,
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_att,
   };

   radv_CmdBeginRendering(radv_cmd_buffer_to_handle(cmd_buffer), &rendering_info);

   emit_resolve(cmd_buffer, &src_iview, &dst_iview, &(VkOffset2D){srcOffset.x, srcOffset.y},
                &(VkOffset2D){dstOffset.x, dstOffset.y});

   radv_CmdEndRendering(radv_cmd_buffer_to_handle(cmd_buffer));

   radv_image_view_finish(&src_iview);
   radv_image_view_finish(&dst_iview);

   radv_meta_restore(&saved_state, cmd_buffer);
}

void
radv_cmd_buffer_resolve_rendering_fs(struct radv_cmd_buffer *cmd_buffer, struct radv_image_view *src_iview,
                                     VkImageLayout src_layout, struct radv_image_view *dst_iview,
                                     VkImageLayout dst_layout)
{
   const struct radv_rendering_state *render = &cmd_buffer->state.render;
   struct radv_meta_saved_state saved_state;
   VkRect2D resolve_area = render->area;

   radv_meta_save(
      &saved_state, cmd_buffer,
      RADV_META_SAVE_GRAPHICS_PIPELINE | RADV_META_SAVE_CONSTANTS | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_RENDER);

   radv_CmdSetViewport(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1,
                       &(VkViewport){.x = resolve_area.offset.x,
                                     .y = resolve_area.offset.y,
                                     .width = resolve_area.extent.width,
                                     .height = resolve_area.extent.height,
                                     .minDepth = 0.0f,
                                     .maxDepth = 1.0f});

   radv_CmdSetScissor(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1, &resolve_area);

   const VkRenderingAttachmentInfo color_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = radv_image_view_to_handle(dst_iview),
      .imageLayout = dst_layout,
      .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
   };

   const VkRenderingInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .flags = VK_RENDERING_INPUT_ATTACHMENT_NO_CONCURRENT_WRITES_BIT_MESA,
      .renderArea = saved_state.render.area,
      .layerCount = 1,
      .viewMask = saved_state.render.view_mask,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_att,
   };

   radv_CmdBeginRendering(radv_cmd_buffer_to_handle(cmd_buffer), &rendering_info);

   emit_resolve(cmd_buffer, src_iview, dst_iview, &resolve_area.offset, &resolve_area.offset);

   radv_CmdEndRendering(radv_cmd_buffer_to_handle(cmd_buffer));

   radv_meta_restore(&saved_state, cmd_buffer);
}

/**
 * Depth/stencil resolves for the current rendering.
 */
void
radv_depth_stencil_resolve_rendering_fs(struct radv_cmd_buffer *cmd_buffer, VkImageAspectFlags aspects,
                                        VkResolveModeFlagBits resolve_mode)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_rendering_state *render = &cmd_buffer->state.render;
   VkRect2D resolve_area = render->area;
   struct radv_meta_saved_state saved_state;
   struct radv_resolve_barrier barrier;

   /* Resolves happen before rendering ends, so we have to make the attachment shader-readable */
   barrier.src_stage_mask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
   barrier.dst_stage_mask = VK_PIPELINE_STAGE_2_RESOLVE_BIT;
   barrier.src_access_mask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
   barrier.dst_access_mask = VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT;
   radv_emit_resolve_barrier(cmd_buffer, &barrier);

   struct radv_image_view *src_iview = cmd_buffer->state.render.ds_att.iview;
   VkImageLayout src_layout =
      aspects & VK_IMAGE_ASPECT_DEPTH_BIT ? render->ds_att.layout : render->ds_att.stencil_layout;
   struct radv_image *src_image = src_iview->image;

   VkImageResolve2 region = {0};
   region.sType = VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2;
   region.srcSubresource.aspectMask = aspects;
   region.srcSubresource.mipLevel = 0;
   region.srcSubresource.baseArrayLayer = 0;
   region.srcSubresource.layerCount = 1;

   radv_decompress_resolve_src(cmd_buffer, src_image, src_layout, &region);

   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_GRAPHICS_PIPELINE | RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_RENDER);

   struct radv_image_view *dst_iview = saved_state.render.ds_att.resolve_iview;

   const VkRenderingAttachmentInfo depth_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = radv_image_view_to_handle(dst_iview),
      .imageLayout = saved_state.render.ds_att.resolve_layout,
      .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
   };

   const VkRenderingAttachmentInfo stencil_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = radv_image_view_to_handle(dst_iview),
      .imageLayout = saved_state.render.ds_att.stencil_resolve_layout,
      .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
   };

   const VkRenderingInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .flags = VK_RENDERING_INPUT_ATTACHMENT_NO_CONCURRENT_WRITES_BIT_MESA,
      .renderArea = saved_state.render.area,
      .layerCount = 1,
      .viewMask = saved_state.render.view_mask,
      .pDepthAttachment = (dst_iview->image->vk.aspects & VK_IMAGE_ASPECT_DEPTH_BIT) ? &depth_att : NULL,
      .pStencilAttachment = (dst_iview->image->vk.aspects & VK_IMAGE_ASPECT_STENCIL_BIT) ? &stencil_att : NULL,
   };

   radv_CmdBeginRendering(radv_cmd_buffer_to_handle(cmd_buffer), &rendering_info);

   struct radv_image_view tsrc_iview;
   radv_image_view_init(&tsrc_iview, device,
                        &(VkImageViewCreateInfo){
                           .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                           .image = radv_image_to_handle(src_image),
                           .viewType = VK_IMAGE_VIEW_TYPE_2D,
                           .format = src_iview->vk.format,
                           .subresourceRange =
                              {
                                 .aspectMask = aspects,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1,
                              },
                        },
                        NULL);

   emit_depth_stencil_resolve(cmd_buffer, &tsrc_iview, dst_iview, &resolve_area.offset, &resolve_area.extent, aspects,
                              resolve_mode);

   radv_CmdEndRendering(radv_cmd_buffer_to_handle(cmd_buffer));

   radv_image_view_finish(&tsrc_iview);

   radv_meta_restore(&saved_state, cmd_buffer);
}
