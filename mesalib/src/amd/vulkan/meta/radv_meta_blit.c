/*
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/radv_meta_nir.h"
#include "radv_meta.h"
#include "vk_command_pool.h"
#include "vk_common_entrypoints.h"

static enum glsl_sampler_dim
translate_sampler_dim(VkImageType type)
{
   switch (type) {
   case VK_IMAGE_TYPE_1D:
      return GLSL_SAMPLER_DIM_1D;
   case VK_IMAGE_TYPE_2D:
      return GLSL_SAMPLER_DIM_2D;
   case VK_IMAGE_TYPE_3D:
      return GLSL_SAMPLER_DIM_3D;
   default:
      unreachable("Unhandled image type");
   }
}

static VkResult
get_pipeline_layout(struct radv_device *device, VkPipelineLayout *layout_out)
{
   enum radv_meta_object_key_type key = RADV_META_OBJECT_KEY_BLIT;

   const VkDescriptorSetLayoutBinding binding = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
   };

   const VkDescriptorSetLayoutCreateInfo desc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
      .bindingCount = 1,
      .pBindings = &binding,
   };

   const VkPushConstantRange pc_range = {VK_SHADER_STAGE_VERTEX_BIT, 0, 20};

   return vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, &pc_range, &key, sizeof(key),
                                      layout_out);
}

struct radv_blit_key {
   enum radv_meta_object_key_type type;
   VkImageAspectFlags aspects;
   VkImageType image_type;
   uint32_t fs_key;
};

static VkResult
get_pipeline(struct radv_device *device, const struct radv_image_view *src_iview,
             const struct radv_image_view *dst_iview, VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   const VkImageAspectFlags aspect = src_iview->vk.aspects;
   const struct radv_image *src_image = src_iview->image;
   const struct radv_image *dst_image = dst_iview->image;
   const enum glsl_sampler_dim tex_dim = translate_sampler_dim(src_image->vk.image_type);
   struct radv_blit_key key;
   VkResult result;

   result = get_pipeline_layout(device, layout_out);
   if (result != VK_SUCCESS)
      return result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_BLIT;
   key.aspects = src_image->vk.aspects;
   key.image_type = src_image->vk.image_type;

   if (src_image->vk.aspects == VK_IMAGE_ASPECT_COLOR_BIT)
      key.fs_key = radv_format_meta_fs_key(device, dst_image->vk.format);

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *fs;
   nir_shader *vs = radv_meta_nir_build_blit_vertex_shader(device);

   switch (aspect) {
   case VK_IMAGE_ASPECT_COLOR_BIT:
      fs = radv_meta_nir_build_blit_copy_fragment_shader(device, tex_dim);
      break;
   case VK_IMAGE_ASPECT_DEPTH_BIT:
      fs = radv_meta_nir_build_blit_copy_fragment_shader_depth(device, tex_dim);
      break;
   case VK_IMAGE_ASPECT_STENCIL_BIT:
      fs = radv_meta_nir_build_blit_copy_fragment_shader_stencil(device, tex_dim);
      break;
   default:
      unreachable("Unhandled aspect");
   }

   VkGraphicsPipelineCreateInfo pipeline_create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .stageCount = 2,
      .pStages =
         (VkPipelineShaderStageCreateInfo[]){
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = VK_SHADER_STAGE_VERTEX_BIT,
             .module = vk_shader_module_handle_from_nir(vs),
             .pName = "main",
             .pSpecializationInfo = NULL},
            {.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
             .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
             .module = vk_shader_module_handle_from_nir(fs),
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
                                                   .lineWidth = 1.0f},
      .pMultisampleState =
         &(VkPipelineMultisampleStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = 1,
            .sampleShadingEnable = false,
            .pSampleMask = (VkSampleMask[]){UINT32_MAX},
         },
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

   VkPipelineColorBlendStateCreateInfo color_blend_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
      .attachmentCount = 1,
      .pAttachments =
         (VkPipelineColorBlendAttachmentState[]){
            {.colorWriteMask = VK_COLOR_COMPONENT_A_BIT | VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT},
         },
      .blendConstants = {0.0f, 0.0f, 0.0f, 0.0f}};

   VkPipelineDepthStencilStateCreateInfo depth_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = true,
      .depthWriteEnable = true,
      .depthCompareOp = VK_COMPARE_OP_ALWAYS,
   };

   VkPipelineDepthStencilStateCreateInfo stencil_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = false,
      .depthWriteEnable = false,
      .stencilTestEnable = true,
      .front = {.failOp = VK_STENCIL_OP_REPLACE,
                .passOp = VK_STENCIL_OP_REPLACE,
                .depthFailOp = VK_STENCIL_OP_REPLACE,
                .compareOp = VK_COMPARE_OP_ALWAYS,
                .compareMask = 0xff,
                .writeMask = 0xff,
                .reference = 0},
      .back = {.failOp = VK_STENCIL_OP_REPLACE,
               .passOp = VK_STENCIL_OP_REPLACE,
               .depthFailOp = VK_STENCIL_OP_REPLACE,
               .compareOp = VK_COMPARE_OP_ALWAYS,
               .compareMask = 0xff,
               .writeMask = 0xff,
               .reference = 0},
      .depthCompareOp = VK_COMPARE_OP_ALWAYS,
   };

   struct vk_meta_rendering_info render = {0};

   switch (aspect) {
   case VK_IMAGE_ASPECT_COLOR_BIT:
      pipeline_create_info.pColorBlendState = &color_blend_info;
      render.color_attachment_count = 1;
      render.color_attachment_formats[0] = radv_fs_key_format_exemplars[key.fs_key];
      break;
   case VK_IMAGE_ASPECT_DEPTH_BIT:
      pipeline_create_info.pDepthStencilState = &depth_info;
      render.depth_attachment_format = VK_FORMAT_D32_SFLOAT;
      break;
   case VK_IMAGE_ASPECT_STENCIL_BIT:
      pipeline_create_info.pDepthStencilState = &stencil_info;
      render.stencil_attachment_format = VK_FORMAT_S8_UINT;
      break;
   default:
      unreachable("Unhandled aspect");
   }

   result = vk_meta_create_graphics_pipeline(&device->vk, &device->meta_state.device, &pipeline_create_info, &render,
                                             &key, sizeof(key), pipeline_out);

   ralloc_free(vs);
   ralloc_free(fs);
   return result;
}

static void
meta_emit_blit(struct radv_cmd_buffer *cmd_buffer, struct radv_image_view *src_iview, VkImageLayout src_image_layout,
               float src_offset_0[3], float src_offset_1[3], struct radv_image_view *dst_iview,
               VkImageLayout dst_image_layout, VkRect2D dst_box, VkSampler sampler)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_image *src_image = src_iview->image;
   const struct radv_image *dst_image = dst_iview->image;
   uint32_t src_width = u_minify(src_image->vk.extent.width, src_iview->vk.base_mip_level);
   uint32_t src_height = u_minify(src_image->vk.extent.height, src_iview->vk.base_mip_level);
   uint32_t src_depth = u_minify(src_image->vk.extent.depth, src_iview->vk.base_mip_level);
   uint32_t dst_width = u_minify(dst_image->vk.extent.width, dst_iview->vk.base_mip_level);
   uint32_t dst_height = u_minify(dst_image->vk.extent.height, dst_iview->vk.base_mip_level);
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   assert(src_image->vk.samples == dst_image->vk.samples);

   result = get_pipeline(device, src_iview, dst_iview, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   float vertex_push_constants[5] = {
      src_offset_0[0] / (float)src_width,  src_offset_0[1] / (float)src_height, src_offset_1[0] / (float)src_width,
      src_offset_1[1] / (float)src_height, src_offset_0[2] / (float)src_depth,
   };

   vk_common_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer), layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 20,
                              vertex_push_constants);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

   radv_meta_push_descriptor_set(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1,
                                 (VkWriteDescriptorSet[]){{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                                           .dstBinding = 0,
                                                           .dstArrayElement = 0,
                                                           .descriptorCount = 1,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                                           .pImageInfo = (VkDescriptorImageInfo[]){
                                                              {
                                                                 .sampler = sampler,
                                                                 .imageView = radv_image_view_to_handle(src_iview),
                                                                 .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                                              },
                                                           }}});

   VkRenderingInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .flags = VK_RENDERING_INPUT_ATTACHMENT_NO_CONCURRENT_WRITES_BIT_MESA,
      .renderArea =
         {
            .offset = {0, 0},
            .extent = {dst_width, dst_height},
         },
      .layerCount = 1,
   };

   VkRenderingAttachmentInfo color_att;
   if (src_image->vk.aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
      unsigned dst_layout = radv_meta_dst_layout_from_layout(dst_image_layout);

      color_att = (VkRenderingAttachmentInfo){
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = radv_image_view_to_handle(dst_iview),
         .imageLayout = radv_meta_dst_layout_to_layout(dst_layout),
         .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      };
      rendering_info.colorAttachmentCount = 1;
      rendering_info.pColorAttachments = &color_att;
   }

   VkRenderingAttachmentInfo depth_att;
   if (src_image->vk.aspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
      enum radv_blit_ds_layout ds_layout = radv_meta_blit_ds_to_type(dst_image_layout);

      depth_att = (VkRenderingAttachmentInfo){
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = radv_image_view_to_handle(dst_iview),
         .imageLayout = radv_meta_blit_ds_to_layout(ds_layout),
         .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      };
      rendering_info.pDepthAttachment = &depth_att;
   }

   VkRenderingAttachmentInfo stencil_att;
   if (src_image->vk.aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
      enum radv_blit_ds_layout ds_layout = radv_meta_blit_ds_to_type(dst_image_layout);

      stencil_att = (VkRenderingAttachmentInfo){
         .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
         .imageView = radv_image_view_to_handle(dst_iview),
         .imageLayout = radv_meta_blit_ds_to_layout(ds_layout),
         .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
         .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
      };
      rendering_info.pStencilAttachment = &stencil_att;
   }

   radv_CmdBeginRendering(radv_cmd_buffer_to_handle(cmd_buffer), &rendering_info);

   radv_CmdDraw(radv_cmd_buffer_to_handle(cmd_buffer), 3, 1, 0, 0);

   radv_CmdEndRendering(radv_cmd_buffer_to_handle(cmd_buffer));
}

static bool
flip_coords(unsigned *src0, unsigned *src1, unsigned *dst0, unsigned *dst1)
{
   bool flip = false;
   if (*src0 > *src1) {
      unsigned tmp = *src0;
      *src0 = *src1;
      *src1 = tmp;
      flip = !flip;
   }

   if (*dst0 > *dst1) {
      unsigned tmp = *dst0;
      *dst0 = *dst1;
      *dst1 = tmp;
      flip = !flip;
   }
   return flip;
}

static void
blit_image(struct radv_cmd_buffer *cmd_buffer, struct radv_image *src_image, VkImageLayout src_image_layout,
           struct radv_image *dst_image, VkImageLayout dst_image_layout, const VkImageBlit2 *region, VkFilter filter)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const VkImageSubresourceLayers *src_res = &region->srcSubresource;
   const VkImageSubresourceLayers *dst_res = &region->dstSubresource;
   struct radv_meta_saved_state saved_state;
   VkSampler sampler;

   /* From the Vulkan 1.0 spec:
    *
    *    vkCmdBlitImage must not be used for multisampled source or
    *    destination images. Use vkCmdResolveImage for this purpose.
    */
   assert(src_image->vk.samples == 1);
   assert(dst_image->vk.samples == 1);

   radv_CreateSampler(radv_device_to_handle(device),
                      &(VkSamplerCreateInfo){
                         .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
                         .magFilter = filter,
                         .minFilter = filter,
                         .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                         .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                         .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
                      },
                      &cmd_buffer->vk.pool->alloc, &sampler);

   /* VK_EXT_conditional_rendering says that blit commands should not be
    * affected by conditional rendering.
    */
   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_GRAPHICS_PIPELINE | RADV_META_SAVE_CONSTANTS | RADV_META_SAVE_DESCRIPTORS |
                     RADV_META_SUSPEND_PREDICATING);

   unsigned dst_start, dst_end;
   if (dst_image->vk.image_type == VK_IMAGE_TYPE_3D) {
      assert(dst_res->baseArrayLayer == 0);
      dst_start = region->dstOffsets[0].z;
      dst_end = region->dstOffsets[1].z;
   } else {
      dst_start = dst_res->baseArrayLayer;
      dst_end = dst_start + vk_image_subresource_layer_count(&dst_image->vk, dst_res);
   }

   unsigned src_start, src_end;
   if (src_image->vk.image_type == VK_IMAGE_TYPE_3D) {
      assert(src_res->baseArrayLayer == 0);
      src_start = region->srcOffsets[0].z;
      src_end = region->srcOffsets[1].z;
   } else {
      src_start = src_res->baseArrayLayer;
      src_end = src_start + vk_image_subresource_layer_count(&src_image->vk, src_res);
   }

   bool flip_z = flip_coords(&src_start, &src_end, &dst_start, &dst_end);
   float src_z_step = (float)(src_end - src_start) / (float)(dst_end - dst_start);

   /* There is no interpolation to the pixel center during
    * rendering, so add the 0.5 offset ourselves here. */
   float depth_center_offset = 0;
   if (src_image->vk.image_type == VK_IMAGE_TYPE_3D)
      depth_center_offset = 0.5 / (dst_end - dst_start) * (src_end - src_start);

   if (flip_z) {
      src_start = src_end;
      src_z_step *= -1;
      depth_center_offset *= -1;
   }

   unsigned src_x0 = region->srcOffsets[0].x;
   unsigned src_x1 = region->srcOffsets[1].x;
   unsigned dst_x0 = region->dstOffsets[0].x;
   unsigned dst_x1 = region->dstOffsets[1].x;

   unsigned src_y0 = region->srcOffsets[0].y;
   unsigned src_y1 = region->srcOffsets[1].y;
   unsigned dst_y0 = region->dstOffsets[0].y;
   unsigned dst_y1 = region->dstOffsets[1].y;

   VkRect2D dst_box;
   dst_box.offset.x = MIN2(dst_x0, dst_x1);
   dst_box.offset.y = MIN2(dst_y0, dst_y1);
   dst_box.extent.width = dst_x1 - dst_x0;
   dst_box.extent.height = dst_y1 - dst_y0;

   const VkOffset2D dst_offset_0 = {
      .x = dst_x0,
      .y = dst_y0,
   };
   const VkOffset2D dst_offset_1 = {
      .x = dst_x1,
      .y = dst_y1,
   };

   radv_CmdSetViewport(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1,
                       &(VkViewport){.x = dst_offset_0.x,
                                     .y = dst_offset_0.y,
                                     .width = dst_offset_1.x - dst_offset_0.x,
                                     .height = dst_offset_1.y - dst_offset_0.y,
                                     .minDepth = 0.0f,
                                     .maxDepth = 1.0f});

   radv_CmdSetScissor(
      radv_cmd_buffer_to_handle(cmd_buffer), 0, 1,
      &(VkRect2D){
         .offset = (VkOffset2D){MIN2(dst_offset_0.x, dst_offset_1.x), MIN2(dst_offset_0.y, dst_offset_1.y)},
         .extent = (VkExtent2D){abs(dst_offset_1.x - dst_offset_0.x), abs(dst_offset_1.y - dst_offset_0.y)},
      });

   const unsigned num_layers = dst_end - dst_start;
   for (unsigned i = 0; i < num_layers; i++) {
      struct radv_image_view dst_iview, src_iview;

      float src_offset_0[3] = {
         src_x0,
         src_y0,
         src_start + i * src_z_step + depth_center_offset,
      };
      float src_offset_1[3] = {
         src_x1,
         src_y1,
         src_start + i * src_z_step + depth_center_offset,
      };
      const uint32_t dst_array_slice = dst_start + i;

      /* 3D images have just 1 layer */
      const uint32_t src_array_slice = src_image->vk.image_type == VK_IMAGE_TYPE_3D ? 0 : src_start + i;

      radv_image_view_init(&dst_iview, device,
                           &(VkImageViewCreateInfo){
                              .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                              .image = radv_image_to_handle(dst_image),
                              .viewType = radv_meta_get_view_type(dst_image),
                              .format = dst_image->vk.format,
                              .subresourceRange = {.aspectMask = dst_res->aspectMask,
                                                   .baseMipLevel = dst_res->mipLevel,
                                                   .levelCount = 1,
                                                   .baseArrayLayer = dst_array_slice,
                                                   .layerCount = 1},
                           },
                           NULL);
      radv_image_view_init(&src_iview, device,
                           &(VkImageViewCreateInfo){
                              .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                              .image = radv_image_to_handle(src_image),
                              .viewType = radv_meta_get_view_type(src_image),
                              .format = src_image->vk.format,
                              .subresourceRange = {.aspectMask = src_res->aspectMask,
                                                   .baseMipLevel = src_res->mipLevel,
                                                   .levelCount = 1,
                                                   .baseArrayLayer = src_array_slice,
                                                   .layerCount = 1},
                           },
                           NULL);
      meta_emit_blit(cmd_buffer, &src_iview, src_image_layout, src_offset_0, src_offset_1, &dst_iview, dst_image_layout,
                     dst_box, sampler);

      radv_image_view_finish(&dst_iview);
      radv_image_view_finish(&src_iview);
   }

   radv_meta_restore(&saved_state, cmd_buffer);

   radv_DestroySampler(radv_device_to_handle(device), sampler, &cmd_buffer->vk.pool->alloc);
}

VKAPI_ATTR void VKAPI_CALL
radv_CmdBlitImage2(VkCommandBuffer commandBuffer, const VkBlitImageInfo2 *pBlitImageInfo)
{
   VK_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
   VK_FROM_HANDLE(radv_image, src_image, pBlitImageInfo->srcImage);
   VK_FROM_HANDLE(radv_image, dst_image, pBlitImageInfo->dstImage);

   for (unsigned r = 0; r < pBlitImageInfo->regionCount; r++) {
      blit_image(cmd_buffer, src_image, pBlitImageInfo->srcImageLayout, dst_image, pBlitImageInfo->dstImageLayout,
                 &pBlitImageInfo->pRegions[r], pBlitImageInfo->filter);
   }
}
