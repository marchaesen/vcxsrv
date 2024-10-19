/*
 * Copyright © 2016 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <stdbool.h>

#include "radv_meta.h"
#include "sid.h"

static nir_shader *
build_expand_depth_stencil_compute_shader(struct radv_device *dev)
{
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_2D, false, GLSL_TYPE_FLOAT);

   nir_builder b = radv_meta_init_shader(dev, MESA_SHADER_COMPUTE, "expand_depth_stencil_compute");

   /* We need at least 8/8/1 to cover an entire HTILE block in a single workgroup. */
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_image, img_type, "in_img");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_def *invoc_id = nir_load_local_invocation_id(&b);
   nir_def *wg_id = nir_load_workgroup_id(&b);
   nir_def *block_size = nir_imm_ivec4(&b, b.shader->info.workgroup_size[0], b.shader->info.workgroup_size[1],
                                       b.shader->info.workgroup_size[2], 0);

   nir_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

   nir_def *data = nir_image_deref_load(&b, 4, 32, &nir_build_deref_var(&b, input_img)->def, global_id,
                                        nir_undef(&b, 1, 32), nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_2D);

   /* We need a SCOPE_DEVICE memory_scope because ACO will avoid
    * creating a vmcnt(0) because it expects the L1 cache to keep memory
    * operations in-order for the same workgroup. The vmcnt(0) seems
    * necessary however. */
   nir_barrier(&b, .execution_scope = SCOPE_WORKGROUP, .memory_scope = SCOPE_DEVICE,
               .memory_semantics = NIR_MEMORY_ACQ_REL, .memory_modes = nir_var_mem_ssbo);

   nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, global_id, nir_undef(&b, 1, 32), data,
                         nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_2D);
   return b.shader;
}

static VkResult
create_pipeline_cs(struct radv_device *device, VkPipeline *pipeline)
{
   VkResult result = VK_SUCCESS;

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

   result = radv_meta_create_descriptor_set_layout(device, 2, bindings,
                                                   &device->meta_state.expand_depth_stencil_compute_ds_layout);
   if (result != VK_SUCCESS)
       return result;

   result = radv_meta_create_pipeline_layout(device, &device->meta_state.expand_depth_stencil_compute_ds_layout, 0,
                                             NULL, &device->meta_state.expand_depth_stencil_compute_p_layout);
   if (result != VK_SUCCESS)
       return result;

   nir_shader *cs = build_expand_depth_stencil_compute_shader(device);

   result =
      radv_meta_create_compute_pipeline(device, cs, device->meta_state.expand_depth_stencil_compute_p_layout, pipeline);

   ralloc_free(cs);
   return result;
}

static VkResult
create_pipeline_gfx(struct radv_device *device, uint32_t samples, VkPipelineLayout layout, VkPipeline *pipeline)
{
   VkResult result;
   VkDevice device_h = radv_device_to_handle(device);

   if (!device->meta_state.depth_decomp.p_layout) {
      result = radv_meta_create_pipeline_layout(device, NULL, 0, NULL, &device->meta_state.depth_decomp.p_layout);
      if (result != VK_SUCCESS)
         return result;
   }

   nir_shader *vs_module = radv_meta_build_nir_vs_generate_vertices(device);
   nir_shader *fs_module = radv_meta_build_nir_fs_noop(device);

   const VkPipelineSampleLocationsStateCreateInfoEXT sample_locs_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT,
      .sampleLocationsEnable = false,
   };

   const VkPipelineRenderingCreateInfo rendering_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
      .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT_S8_UINT,
      .stencilAttachmentFormat = VK_FORMAT_D32_SFLOAT_S8_UINT,
   };

   const VkGraphicsPipelineCreateInfo pipeline_create_info = {
      .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
      .pNext = &rendering_create_info,
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
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
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
            .pNext = &sample_locs_create_info,
            .rasterizationSamples = samples,
            .sampleShadingEnable = false,
            .pSampleMask = NULL,
            .alphaToCoverageEnable = false,
            .alphaToOneEnable = false,
         },
      .pColorBlendState =
         &(VkPipelineColorBlendStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .logicOpEnable = false,
            .attachmentCount = 0,
            .pAttachments = NULL,
         },
      .pDepthStencilState =
         &(VkPipelineDepthStencilStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = false,
            .depthWriteEnable = false,
            .depthBoundsTestEnable = false,
            .stencilTestEnable = false,
         },
      .pDynamicState =
         &(VkPipelineDynamicStateCreateInfo){
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
            .dynamicStateCount = 3,
            .pDynamicStates =
               (VkDynamicState[]){
                  VK_DYNAMIC_STATE_VIEWPORT,
                  VK_DYNAMIC_STATE_SCISSOR,
                  VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_EXT,
               },
         },
      .layout = layout,
      .renderPass = VK_NULL_HANDLE,
      .subpass = 0,
   };

   struct radv_graphics_pipeline_create_info extra = {
      .use_rectlist = true,
      .depth_compress_disable = true,
      .stencil_compress_disable = true,
   };

   result = radv_graphics_pipeline_create(device_h, device->meta_state.cache, &pipeline_create_info, &extra,
                                          &device->meta_state.alloc, pipeline);

   ralloc_free(fs_module);
   ralloc_free(vs_module);
   return result;
}

void
radv_device_finish_meta_depth_decomp_state(struct radv_device *device)
{
   struct radv_meta_state *state = &device->meta_state;

   radv_DestroyPipelineLayout(radv_device_to_handle(device), state->depth_decomp.p_layout, &state->alloc);
   for (uint32_t i = 0; i < ARRAY_SIZE(state->depth_decomp.decompress_pipeline); ++i) {
      radv_DestroyPipeline(radv_device_to_handle(device), state->depth_decomp.decompress_pipeline[i], &state->alloc);
   }

   radv_DestroyPipeline(radv_device_to_handle(device), state->expand_depth_stencil_compute_pipeline, &state->alloc);
   radv_DestroyPipelineLayout(radv_device_to_handle(device), state->expand_depth_stencil_compute_p_layout,
                              &state->alloc);
   device->vk.dispatch_table.DestroyDescriptorSetLayout(radv_device_to_handle(device),
                                                        state->expand_depth_stencil_compute_ds_layout, &state->alloc);
}

VkResult
radv_device_init_meta_depth_decomp_state(struct radv_device *device, bool on_demand)
{
   struct radv_meta_state *state = &device->meta_state;
   VkResult res = VK_SUCCESS;

   if (on_demand)
      return res;

   for (uint32_t i = 0; i < ARRAY_SIZE(state->depth_decomp.decompress_pipeline); ++i) {
      uint32_t samples = 1 << i;

      res = create_pipeline_gfx(device, samples, state->depth_decomp.p_layout,
                                &state->depth_decomp.decompress_pipeline[i]);
      if (res != VK_SUCCESS)
         return res;
   }

   return create_pipeline_cs(device, &state->expand_depth_stencil_compute_pipeline);
}

static VkResult
get_pipeline_gfx(struct radv_device *device, struct radv_image *image, VkPipeline *pipeline_out)
{
   struct radv_meta_state *state = &device->meta_state;
   uint32_t samples = image->vk.samples;
   uint32_t samples_log2 = ffs(samples) - 1;
   VkResult result = VK_SUCCESS;

   mtx_lock(&state->mtx);
   if (!state->depth_decomp.decompress_pipeline[samples_log2]) {
      result = create_pipeline_gfx(device, samples, state->depth_decomp.p_layout,
                                   &state->depth_decomp.decompress_pipeline[samples_log2]);
      if (result != VK_SUCCESS)
         goto fail;
   }

   *pipeline_out = state->depth_decomp.decompress_pipeline[samples_log2];

fail:
   mtx_unlock(&state->mtx);
   return result;
}

static void
radv_process_depth_image_layer(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                               const VkImageSubresourceRange *range, int level, int layer)
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
                                 .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
                                 .baseMipLevel = range->baseMipLevel + level,
                                 .levelCount = 1,
                                 .baseArrayLayer = range->baseArrayLayer + layer,
                                 .layerCount = 1,
                              },
                        },
                        NULL);

   const VkRenderingAttachmentInfo depth_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = radv_image_view_to_handle(&iview),
      .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
   };

   const VkRenderingAttachmentInfo stencil_att = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
      .imageView = radv_image_view_to_handle(&iview),
      .imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
      .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
      .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
   };

   const VkRenderingInfo rendering_info = {
      .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
      .flags = VK_RENDERING_INPUT_ATTACHMENT_NO_CONCURRENT_WRITES_BIT_MESA,
      .renderArea = {.offset = {0, 0}, .extent = {width, height}},
      .layerCount = 1,
      .pDepthAttachment = &depth_att,
      .pStencilAttachment = &stencil_att,
   };

   radv_CmdBeginRendering(radv_cmd_buffer_to_handle(cmd_buffer), &rendering_info);

   radv_CmdDraw(radv_cmd_buffer_to_handle(cmd_buffer), 3, 1, 0, 0);

   radv_CmdEndRendering(radv_cmd_buffer_to_handle(cmd_buffer));

   radv_image_view_finish(&iview);
}

static void
radv_process_depth_stencil(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                           const VkImageSubresourceRange *subresourceRange,
                           struct radv_sample_locations_state *sample_locs)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_meta_saved_state saved_state;
   VkCommandBuffer cmd_buffer_h = radv_cmd_buffer_to_handle(cmd_buffer);
   VkPipeline pipeline;
   VkResult result;

   result = get_pipeline_gfx(device, image, &pipeline);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_meta_save(&saved_state, cmd_buffer, RADV_META_SAVE_GRAPHICS_PIPELINE | RADV_META_SAVE_RENDER);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);

   if (sample_locs) {
      assert(image->vk.create_flags & VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT);

      /* Set the sample locations specified during explicit or
       * automatic layout transitions, otherwise the depth decompress
       * pass uses the default HW locations.
       */
      radv_CmdSetSampleLocationsEXT(cmd_buffer_h, &(VkSampleLocationsInfoEXT){
                                                     .sampleLocationsPerPixel = sample_locs->per_pixel,
                                                     .sampleLocationGridSize = sample_locs->grid_size,
                                                     .sampleLocationsCount = sample_locs->count,
                                                     .pSampleLocations = sample_locs->locations,
                                                  });
   }

   for (uint32_t l = 0; l < vk_image_subresource_level_count(&image->vk, subresourceRange); ++l) {

      /* Do not decompress levels without HTILE. */
      if (!radv_htile_enabled(image, subresourceRange->baseMipLevel + l))
         continue;

      uint32_t width = u_minify(image->vk.extent.width, subresourceRange->baseMipLevel + l);
      uint32_t height = u_minify(image->vk.extent.height, subresourceRange->baseMipLevel + l);

      radv_CmdSetViewport(
         cmd_buffer_h, 0, 1,
         &(VkViewport){.x = 0, .y = 0, .width = width, .height = height, .minDepth = 0.0f, .maxDepth = 1.0f});

      radv_CmdSetScissor(cmd_buffer_h, 0, 1,
                         &(VkRect2D){
                            .offset = {0, 0},
                            .extent = {width, height},
                         });

      for (uint32_t s = 0; s < vk_image_subresource_layer_count(&image->vk, subresourceRange); s++) {
         radv_process_depth_image_layer(cmd_buffer, image, subresourceRange, l, s);
      }
   }

   radv_meta_restore(&saved_state, cmd_buffer);
}

static VkResult
get_pipeline_cs(struct radv_device *device, VkPipeline *pipeline_out)
{
   struct radv_meta_state *state = &device->meta_state;
   VkResult result = VK_SUCCESS;

   mtx_lock(&state->mtx);
   if (!state->expand_depth_stencil_compute_pipeline) {
      result = create_pipeline_cs(device, &state->expand_depth_stencil_compute_pipeline);
      if (result != VK_SUCCESS)
         goto fail;
   }

   *pipeline_out = state->expand_depth_stencil_compute_pipeline;

fail:
   mtx_unlock(&state->mtx);
   return result;
}

static void
radv_expand_depth_stencil_compute(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                                  const VkImageSubresourceRange *subresourceRange)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_meta_saved_state saved_state;
   struct radv_image_view load_iview = {0};
   struct radv_image_view store_iview = {0};
   VkPipeline pipeline;
   VkResult result;

   assert(radv_image_is_tc_compat_htile(image));

   result = get_pipeline_cs(device, &pipeline);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_meta_save(&saved_state, cmd_buffer, RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_COMPUTE_PIPELINE);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   for (uint32_t l = 0; l < vk_image_subresource_level_count(&image->vk, subresourceRange); l++) {
      uint32_t width, height;

      /* Do not decompress levels without HTILE. */
      if (!radv_htile_enabled(image, subresourceRange->baseMipLevel + l))
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
                                 .subresourceRange = {.aspectMask = subresourceRange->aspectMask,
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
                                 .subresourceRange = {.aspectMask = subresourceRange->aspectMask,
                                                      .baseMipLevel = subresourceRange->baseMipLevel + l,
                                                      .levelCount = 1,
                                                      .baseArrayLayer = subresourceRange->baseArrayLayer + s,
                                                      .layerCount = 1},
                              },
                              &(struct radv_image_view_extra_create_info){.disable_compression = true});

         radv_meta_push_descriptor_set(
            cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, device->meta_state.expand_depth_stencil_compute_p_layout, 0, 2,
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

   radv_meta_restore(&saved_state, cmd_buffer);

   cmd_buffer->state.flush_bits |=
      RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_VCACHE |
      radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_WRITE_BIT, image);

   /* Initialize the HTILE metadata as "fully expanded". */
   uint32_t htile_value = radv_get_htile_initial_value(device, image);

   cmd_buffer->state.flush_bits |= radv_clear_htile(cmd_buffer, image, subresourceRange, htile_value);
}

void
radv_expand_depth_stencil(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                          const VkImageSubresourceRange *subresourceRange,
                          struct radv_sample_locations_state *sample_locs)
{
   struct radv_barrier_data barrier = {0};

   barrier.layout_transitions.depth_stencil_expand = 1;
   radv_describe_layout_transition(cmd_buffer, &barrier);

   if (cmd_buffer->qf == RADV_QUEUE_GENERAL) {
      radv_process_depth_stencil(cmd_buffer, image, subresourceRange, sample_locs);
   } else {
      radv_expand_depth_stencil_compute(cmd_buffer, image, subresourceRange);
   }
}
