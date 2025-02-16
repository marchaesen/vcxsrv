/*
 * Copyright © 2019 Valve Corporation
 * Copyright © 2018 Red Hat
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/radv_meta_nir.h"
#include "radv_formats.h"
#include "radv_meta.h"
#include "vk_format.h"

static VkResult
get_pipeline_layout(struct radv_device *device, VkPipelineLayout *layout_out)
{
   enum radv_meta_object_key_type key = RADV_META_OBJECT_KEY_FMASK_EXPAND;

   const VkDescriptorSetLayoutBinding bindings[] = {
      {
         .binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
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

   return vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, NULL, &key, sizeof(key),
                                      layout_out);
}

struct radv_fmask_expand_key {
   enum radv_meta_object_key_type type;
   uint32_t samples;
};

static VkResult
get_pipeline(struct radv_device *device, uint32_t samples_log2, VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   const uint32_t samples = 1 << samples_log2;
   struct radv_fmask_expand_key key;
   VkResult result;

   result = get_pipeline_layout(device, layout_out);
   if (result != VK_SUCCESS)
      return result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_FMASK_EXPAND;
   key.samples = samples;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = radv_meta_nir_build_fmask_expand_compute_shader(device, samples);

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

void
radv_expand_fmask_image_inplace(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                                const VkImageSubresourceRange *subresourceRange)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_meta_saved_state saved_state;
   const uint32_t samples = image->vk.samples;
   const uint32_t samples_log2 = ffs(samples) - 1;
   unsigned layer_count = vk_image_subresource_layer_count(&image->vk, subresourceRange);
   struct radv_image_view iview;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_pipeline(device, samples_log2, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_meta_save(&saved_state, cmd_buffer, RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   radv_image_view_init(&iview, device,
                        &(VkImageViewCreateInfo){
                           .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                           .image = radv_image_to_handle(image),
                           .viewType = radv_meta_get_view_type(image),
                           .format = vk_format_no_srgb(image->vk.format),
                           .subresourceRange =
                              {
                                 .aspectMask = subresourceRange->aspectMask,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = subresourceRange->baseArrayLayer,
                                 .layerCount = layer_count,
                              },
                        },
                        NULL);

   const VkImageSubresourceRange range = vk_image_view_subresource_range(&iview.vk);

   cmd_buffer->state.flush_bits |= radv_dst_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                                         VK_ACCESS_2_SHADER_READ_BIT, 0, image, &range);

   radv_meta_push_descriptor_set(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 2,
                                 (VkWriteDescriptorSet[]){{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                                           .dstBinding = 0,
                                                           .dstArrayElement = 0,
                                                           .descriptorCount = 1,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                                           .pImageInfo =
                                                              (VkDescriptorImageInfo[]){
                                                                 {.sampler = VK_NULL_HANDLE,
                                                                  .imageView = radv_image_view_to_handle(&iview),
                                                                  .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                                                              }},
                                                          {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                                           .dstBinding = 1,
                                                           .dstArrayElement = 0,
                                                           .descriptorCount = 1,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                           .pImageInfo = (VkDescriptorImageInfo[]){
                                                              {.sampler = VK_NULL_HANDLE,
                                                               .imageView = radv_image_view_to_handle(&iview),
                                                               .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                                                           }}});

   radv_unaligned_dispatch(cmd_buffer, image->vk.extent.width, image->vk.extent.height, layer_count);

   radv_image_view_finish(&iview);

   radv_meta_restore(&saved_state, cmd_buffer);

   cmd_buffer->state.flush_bits |=
      RADV_CMD_FLAG_CS_PARTIAL_FLUSH | radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                             VK_ACCESS_2_SHADER_WRITE_BIT, 0, image, &range);

   /* Re-initialize FMASK in fully expanded mode. */
   cmd_buffer->state.flush_bits |= radv_init_fmask(cmd_buffer, image, subresourceRange);
}
