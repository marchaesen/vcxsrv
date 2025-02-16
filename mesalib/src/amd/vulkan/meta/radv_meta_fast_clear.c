/*
 * Copyright © 2016 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdbool.h>

#include "nir/radv_meta_nir.h"
#include "radv_meta.h"

enum radv_color_op {
   FAST_CLEAR_ELIMINATE,
   FMASK_DECOMPRESS,
   DCC_DECOMPRESS,
};

static VkResult
get_dcc_decompress_compute_pipeline(struct radv_device *device, VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   enum radv_meta_object_key_type key = RADV_META_OBJECT_KEY_DCC_DECOMPRESS;
   VkResult result;

   const VkDescriptorSetLayoutBinding bindings[] = {
      {
         .binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
      {
         .binding = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
   };

   const VkDescriptorSetLayoutCreateInfo desc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
      .bindingCount = 2,
      .pBindings = bindings,
   };

   result = vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, NULL, &key, sizeof(key),
                                        layout_out);
   if (result != VK_SUCCESS)
      return result;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = radv_meta_nir_build_dcc_decompress_compute_shader(device);

   const VkPipelineShaderStageCreateInfo stage_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(cs),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   const VkComputePipelineCreateInfo pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = stage_info,
      .flags = 0,
      .layout = *layout_out,
   };

   result = vk_meta_create_compute_pipeline(&device->vk, &device->meta_state.device, &pipeline_info, &key, sizeof(key),
                                            pipeline_out);

   ralloc_free(cs);
   return result;
}

static VkResult
get_pipeline(struct radv_device *device, enum radv_color_op op, VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   enum radv_meta_object_key_type key = 0;
   VkResult result;

   switch (op) {
   case FAST_CLEAR_ELIMINATE:
      key = RADV_META_OBJECT_KEY_FAST_CLEAR_ELIMINATE;
      break;
   case FMASK_DECOMPRESS:
      key = RADV_META_OBJECT_KEY_FMASK_DECOMPRESS;
      break;
   case DCC_DECOMPRESS:
      key = RADV_META_OBJECT_KEY_DCC_DECOMPRESS;
      break;
   }

   result = radv_meta_get_noop_pipeline_layout(device, layout_out);
   if (result != VK_SUCCESS)
      return result;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *vs_module = radv_meta_nir_build_vs_generate_vertices(device);
   nir_shader *fs_module = radv_meta_nir_build_fs_noop(device);

   VkGraphicsPipelineCreateInfoRADV radv_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO_RADV,
   };

   switch (op) {
   case FAST_CLEAR_ELIMINATE:
      radv_info.custom_blend_mode = V_028808_CB_ELIMINATE_FAST_CLEAR;
      break;
   case FMASK_DECOMPRESS:
      radv_info.custom_blend_mode = V_028808_CB_FMASK_DECOMPRESS;
      break;
   case DCC_DECOMPRESS:
      radv_info.custom_blend_mode =
         pdev->info.gfx_level >= GFX11 ? V_028808_CB_DCC_DECOMPRESS_GFX11 : V_028808_CB_DCC_DECOMPRESS_GFX8;
      break;
   default:
      unreachable("Invalid color op");
   }

   const VkGraphicsPipelineCreateInfo pipeline_create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &radv_info,
      .stageCount = 2,
      .pStages =
         (VkPipelineShaderStageCreateInfo[]){
            {
               .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
               .stage = VK_SHADER_STAGE_VERTEX_BIT,
               .module = vk_shader_module_handle_from_nir(vs_module),
               .pName = "main",
            },
            {
               .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
               .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
               .module = vk_shader_module_handle_from_nir(fs_module),
               .pName = "main",
            },
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
         &(VkPipelineRasterizationStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .depthClampEnable = false,
            .rasterizerDiscardEnable = false,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_NONE,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
         },
      .pMultisampleState =
         &(VkPipelineMultisampleStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = 1,
            .sampleShadingEnable = false,
            .pSampleMask = NULL,
            .alphaToCoverageEnable = false,
            .alphaToOneEnable = false,
         },
      .pColorBlendState =
         &(VkPipelineColorBlendStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = false,
            .attachmentCount = 1,
            .pAttachments =
               (VkPipelineColorBlendAttachmentState[]){
                  {
                     .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                                       VK_COLOR_COMPONENT_A_BIT,
                  },
               },
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

   struct vk_meta_rendering_info render = {
      .color_attachment_count = 1,
      .color_attachment_formats = {VK_FORMAT_R8_UNORM},
   };

   result = vk_meta_create_graphics_pipeline(&device->vk, &device->meta_state.device, &pipeline_create_info, &render,
                                             &key, sizeof(key), pipeline_out);

   ralloc_free(vs_module);
   ralloc_free(fs_module);
   return result;
}

static void
radv_emit_set_predication_state_from_image(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                                           uint64_t pred_offset, bool value)
{
   uint64_t va = 0;

   if (value)
      va = radv_image_get_va(image, 0) + pred_offset;

   radv_emit_set_predication_state(cmd_buffer, true, PREDICATION_OP_BOOL64, va);
}

static void
radv_process_color_image_layer(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                               const VkImageSubresourceRange *range, int level, int layer, bool flush_cb)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_image_view iview;
   uint32_t width, height;

   width = u_minify(image->vk.extent.width, range->baseMipLevel + level);
   height = u_minify(image->vk.extent.height, range->baseMipLevel + level);

   radv_image_view_init(&iview, device,
                        &(VkImageViewCreateInfo){
                           .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                           .image = radv_image_to_handle(image),
                           .viewType = radv_meta_get_view_type(image),
                           .format = image->vk.format,
                           .subresourceRange =
                              {
                                 .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                 .baseMipLevel = range->baseMipLevel + level,
                                 .levelCount = 1,
                                 .baseArrayLayer = range->baseArrayLayer + layer,
                                 .layerCount = 1,
                              },
                        },
                        NULL);

   const VkRenderingAttachmentInfo color_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = radv_image_view_to_handle(&iview),
      .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
   };

   const VkRenderingInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .flags = VK_RENDERING_INPUT_ATTACHMENT_NO_CONCURRENT_WRITES_BIT_MESA,
      .renderArea = {.offset = {0, 0}, .extent = {width, height}},
      .layerCount = 1,
      .colorAttachmentCount = 1,
      .pColorAttachments = &color_att,
   };

   radv_CmdBeginRendering(radv_cmd_buffer_to_handle(cmd_buffer), &rendering_info);

   if (flush_cb)
      cmd_buffer->state.flush_bits |= radv_dst_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                                            VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT, 0, image, range);

   radv_CmdDraw(radv_cmd_buffer_to_handle(cmd_buffer), 3, 1, 0, 0);

   if (flush_cb)
      cmd_buffer->state.flush_bits |= radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                                            VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, 0, image, range);

   radv_CmdEndRendering(radv_cmd_buffer_to_handle(cmd_buffer));

   radv_image_view_finish(&iview);
}

static void
radv_process_color_image(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                         const VkImageSubresourceRange *subresourceRange, enum radv_color_op op)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_meta_saved_state saved_state;
   bool old_predicating = false;
   bool flush_cb = false;
   uint64_t pred_offset;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_pipeline(device, op, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   switch (op) {
   case FAST_CLEAR_ELIMINATE:
      pred_offset = image->fce_pred_offset;
      break;
   case FMASK_DECOMPRESS:
      pred_offset = 0; /* FMASK_DECOMPRESS is never predicated. */

      /* Flushing CB is required before and after FMASK_DECOMPRESS. */
      flush_cb = true;
      break;
   case DCC_DECOMPRESS:
      pred_offset = image->dcc_pred_offset;

      /* Flushing CB is required before and after DCC_DECOMPRESS. */
      flush_cb = true;
      break;
   default:
      unreachable("Invalid color op");
   }

   if (radv_dcc_enabled(image, subresourceRange->baseMipLevel) &&
       (image->vk.array_layers != vk_image_subresource_layer_count(&image->vk, subresourceRange) ||
        subresourceRange->baseArrayLayer != 0)) {
      /* Only use predication if the image has DCC with mipmaps or
       * if the range of layers covers the whole image because the
       * predication is based on mip level.
       */
      pred_offset = 0;
   }

   radv_meta_save(&saved_state, cmd_buffer, RADV_META_SAVE_GRAPHICS_PIPELINE | RADV_META_SAVE_RENDER);

   if (pred_offset) {
      pred_offset += 8 * subresourceRange->baseMipLevel;

      old_predicating = cmd_buffer->state.predicating;

      radv_emit_set_predication_state_from_image(cmd_buffer, image, pred_offset, true);
      cmd_buffer->state.predicating = true;
   }

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

   for (uint32_t l = 0; l < vk_image_subresource_level_count(&image->vk, subresourceRange); ++l) {
      uint32_t width, height;

      /* Do not decompress levels without DCC. */
      if (op == DCC_DECOMPRESS && !radv_dcc_enabled(image, subresourceRange->baseMipLevel + l))
         continue;

      width = u_minify(image->vk.extent.width, subresourceRange->baseMipLevel + l);
      height = u_minify(image->vk.extent.height, subresourceRange->baseMipLevel + l);

      radv_CmdSetViewport(
         radv_cmd_buffer_to_handle(cmd_buffer), 0, 1,
         &(VkViewport){.x = 0, .y = 0, .width = width, .height = height, .minDepth = 0.0f, .maxDepth = 1.0f});

      radv_CmdSetScissor(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1,
                         &(VkRect2D){
                            .offset = {0, 0},
                            .extent = {width, height},
                         });

      for (uint32_t s = 0; s < vk_image_subresource_layer_count(&image->vk, subresourceRange); s++) {
         radv_process_color_image_layer(cmd_buffer, image, subresourceRange, l, s, flush_cb);
      }
   }

   cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB | RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;

   if (pred_offset) {
      pred_offset += 8 * subresourceRange->baseMipLevel;

      cmd_buffer->state.predicating = old_predicating;

      radv_emit_set_predication_state_from_image(cmd_buffer, image, pred_offset, false);

      if (cmd_buffer->state.predication_type != -1) {
         /* Restore previous conditional rendering user state. */
         radv_emit_set_predication_state(cmd_buffer, cmd_buffer->state.predication_type,
                                         cmd_buffer->state.predication_op, cmd_buffer->state.predication_va);
      }
   }

   radv_meta_restore(&saved_state, cmd_buffer);

   /* Clear the image's fast-clear eliminate predicate because FMASK_DECOMPRESS and DCC_DECOMPRESS
    * also perform a fast-clear eliminate.
    */
   radv_update_fce_metadata(cmd_buffer, image, subresourceRange, false);

   /* Mark the image as being decompressed. */
   if (op == DCC_DECOMPRESS)
      radv_update_dcc_metadata(cmd_buffer, image, subresourceRange, false);
}

static void
radv_fast_clear_eliminate(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                          const VkImageSubresourceRange *subresourceRange)
{
   struct radv_barrier_data barrier = {0};

   barrier.layout_transitions.fast_clear_eliminate = 1;
   radv_describe_layout_transition(cmd_buffer, &barrier);

   radv_process_color_image(cmd_buffer, image, subresourceRange, FAST_CLEAR_ELIMINATE);
}

static void
radv_fmask_decompress(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                      const VkImageSubresourceRange *subresourceRange)
{
   struct radv_barrier_data barrier = {0};

   barrier.layout_transitions.fmask_decompress = 1;
   radv_describe_layout_transition(cmd_buffer, &barrier);

   radv_process_color_image(cmd_buffer, image, subresourceRange, FMASK_DECOMPRESS);
}

void
radv_fast_clear_flush_image_inplace(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                                    const VkImageSubresourceRange *subresourceRange)
{
   if (radv_image_has_fmask(image) && !image->tc_compatible_cmask) {
      if (radv_image_has_dcc(image) && radv_image_has_cmask(image)) {
         /* MSAA images with DCC and CMASK might have been fast-cleared and might require a FCE but
          * FMASK_DECOMPRESS can't eliminate DCC fast clears.
          */
         radv_fast_clear_eliminate(cmd_buffer, image, subresourceRange);
      }

      radv_fmask_decompress(cmd_buffer, image, subresourceRange);
   } else {
      /* Skip fast clear eliminate for images that support comp-to-single fast clears. */
      if (image->support_comp_to_single)
         return;

      radv_fast_clear_eliminate(cmd_buffer, image, subresourceRange);
   }
}

static void
radv_decompress_dcc_compute(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                            const VkImageSubresourceRange *subresourceRange)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_meta_saved_state saved_state;
   struct radv_image_view load_iview = {0};
   struct radv_image_view store_iview = {0};
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_dcc_decompress_compute_pipeline(device, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   cmd_buffer->state.flush_bits |= radv_dst_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                         VK_ACCESS_2_SHADER_READ_BIT, 0, image, subresourceRange);

   radv_meta_save(&saved_state, cmd_buffer, RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_COMPUTE_PIPELINE);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   for (uint32_t l = 0; l < vk_image_subresource_level_count(&image->vk, subresourceRange); l++) {
      uint32_t width, height;

      /* Do not decompress levels without DCC. */
      if (!radv_dcc_enabled(image, subresourceRange->baseMipLevel + l))
         continue;

      width = u_minify(image->vk.extent.width, subresourceRange->baseMipLevel + l);
      height = u_minify(image->vk.extent.height, subresourceRange->baseMipLevel + l);

      for (uint32_t s = 0; s < vk_image_subresource_layer_count(&image->vk, subresourceRange); s++) {
         radv_image_view_init(&load_iview, device,
                              &(VkImageViewCreateInfo){
                                 .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                 .image = radv_image_to_handle(image),
                                 .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                 .format = image->vk.format,
                                 .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                      .baseMipLevel = subresourceRange->baseMipLevel + l,
                                                      .levelCount = 1,
                                                      .baseArrayLayer = subresourceRange->baseArrayLayer + s,
                                                      .layerCount = 1},
                              },
                              &(struct radv_image_view_extra_create_info){.enable_compression = true});
         radv_image_view_init(&store_iview, device,
                              &(VkImageViewCreateInfo){
                                 .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                                 .image = radv_image_to_handle(image),
                                 .viewType = VK_IMAGE_VIEW_TYPE_2D,
                                 .format = image->vk.format,
                                 .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                                      .baseMipLevel = subresourceRange->baseMipLevel + l,
                                                      .levelCount = 1,
                                                      .baseArrayLayer = subresourceRange->baseArrayLayer + s,
                                                      .layerCount = 1},
                              },
                              &(struct radv_image_view_extra_create_info){.disable_compression = true});

         radv_meta_push_descriptor_set(
            cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 2,
            (VkWriteDescriptorSet[]){{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                      .dstBinding = 0,
                                      .dstArrayElement = 0,
                                      .descriptorCount = 1,
                                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                      .pImageInfo =
                                         (VkDescriptorImageInfo[]){
                                            {
                                               .sampler = VK_NULL_HANDLE,
                                               .imageView = radv_image_view_to_handle(&load_iview),
                                               .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                            },
                                         }},
                                     {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                      .dstBinding = 1,
                                      .dstArrayElement = 0,
                                      .descriptorCount = 1,
                                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                      .pImageInfo = (VkDescriptorImageInfo[]){
                                         {
                                            .sampler = VK_NULL_HANDLE,
                                            .imageView = radv_image_view_to_handle(&store_iview),
                                            .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                         },
                                      }}});

         radv_unaligned_dispatch(cmd_buffer, width, height, 1);

         radv_image_view_finish(&load_iview);
         radv_image_view_finish(&store_iview);
      }
   }

   /* Mark this image as actually being decompressed. */
   radv_update_dcc_metadata(cmd_buffer, image, subresourceRange, false);

   radv_meta_restore(&saved_state, cmd_buffer);

   cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_VCACHE |
                                   radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                         VK_ACCESS_2_SHADER_WRITE_BIT, 0, image, subresourceRange);

   /* Initialize the DCC metadata as "fully expanded". */
   cmd_buffer->state.flush_bits |= radv_init_dcc(cmd_buffer, image, subresourceRange, 0xffffffff);
}

void
radv_decompress_dcc(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                    const VkImageSubresourceRange *subresourceRange)
{
   struct radv_barrier_data barrier = {0};

   barrier.layout_transitions.dcc_decompress = 1;
   radv_describe_layout_transition(cmd_buffer, &barrier);

   if (cmd_buffer->qf == RADV_QUEUE_GENERAL)
      radv_process_color_image(cmd_buffer, image, subresourceRange, DCC_DECOMPRESS);
   else
      radv_decompress_dcc_compute(cmd_buffer, image, subresourceRange);
}
