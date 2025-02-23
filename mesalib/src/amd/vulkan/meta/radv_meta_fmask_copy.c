/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/radv_meta_nir.h"
#include "radv_formats.h"
#include "radv_meta.h"

static VkResult
get_pipeline_layout(struct radv_device *device, VkPipelineLayout *layout_out)
{
   enum radv_meta_object_key_type key = RADV_META_OBJECT_KEY_FMASK_COPY;

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

struct radv_fmask_copy_key {
   enum radv_meta_object_key_type type;
   uint32_t samples;
};

static VkResult
get_pipeline(struct radv_device *device, uint32_t samples_log2, VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   const uint32_t samples = 1 << samples_log2;
   struct radv_fmask_copy_key key;
   VkResult result;

   result = get_pipeline_layout(device, layout_out);
   if (result != VK_SUCCESS)
      return result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_FMASK_COPY;
   key.samples = samples;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = radv_meta_nir_build_fmask_copy_compute_shader(device, samples);

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

static void
radv_fixup_copy_dst_metadata(struct radv_cmd_buffer *cmd_buffer, const struct radv_image *src_image,
                             const struct radv_image *dst_image)
{
   uint64_t src_va, dst_va, size;

   assert(src_image->planes[0].surface.cmask_size == dst_image->planes[0].surface.cmask_size &&
          src_image->planes[0].surface.fmask_size == dst_image->planes[0].surface.fmask_size);
   assert(src_image->planes[0].surface.fmask_offset + src_image->planes[0].surface.fmask_size ==
             src_image->planes[0].surface.cmask_offset &&
          dst_image->planes[0].surface.fmask_offset + dst_image->planes[0].surface.fmask_size ==
             dst_image->planes[0].surface.cmask_offset);

   /* Copy CMASK+FMASK. */
   size = src_image->planes[0].surface.cmask_size + src_image->planes[0].surface.fmask_size;
   src_va = src_image->bindings[0].addr + src_image->planes[0].surface.fmask_offset;
   dst_va = dst_image->bindings[0].addr + dst_image->planes[0].surface.fmask_offset;

   radv_copy_memory(cmd_buffer, src_va, dst_va, size);
}

bool
radv_can_use_fmask_copy(struct radv_cmd_buffer *cmd_buffer, const struct radv_image *src_image,
                        const struct radv_image *dst_image, const struct radv_meta_blit2d_rect *rect)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);

   /* TODO: Test on pre GFX10 chips. */
   if (pdev->info.gfx_level < GFX10)
      return false;

   /* TODO: Add support for layers. */
   if (src_image->vk.array_layers != 1 || dst_image->vk.array_layers != 1)
      return false;

   /* Source/destination images must have FMASK. */
   if (!radv_image_has_fmask(src_image) || !radv_image_has_fmask(dst_image))
      return false;

   /* Source/destination images must have identical TC-compat mode. */
   if (radv_image_is_tc_compat_cmask(src_image) != radv_image_is_tc_compat_cmask(dst_image))
      return false;

   /* The region must be a whole image copy. */
   if (rect->src_x || rect->src_y || rect->dst_x || rect->dst_y || rect->width != src_image->vk.extent.width ||
       rect->height != src_image->vk.extent.height)
      return false;

   /* Source/destination images must have identical size. */
   if (src_image->vk.extent.width != dst_image->vk.extent.width ||
       src_image->vk.extent.height != dst_image->vk.extent.height)
      return false;

   /* Source/destination images must have identical swizzle. */
   if (src_image->planes[0].surface.fmask_tile_swizzle != dst_image->planes[0].surface.fmask_tile_swizzle ||
       src_image->planes[0].surface.u.gfx9.color.fmask_swizzle_mode !=
          dst_image->planes[0].surface.u.gfx9.color.fmask_swizzle_mode)
      return false;

   return true;
}

void
radv_fmask_copy(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *src,
                struct radv_meta_blit2d_surf *dst)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_image_view src_iview, dst_iview;
   uint32_t samples = src->image->vk.samples;
   uint32_t samples_log2 = ffs(samples) - 1;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_pipeline(device, samples_log2, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   radv_image_view_init(&src_iview, device,
                        &(VkImageViewCreateInfo){
                           .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                           .image = radv_image_to_handle(src->image),
                           .viewType = radv_meta_get_view_type(src->image),
                           .format = vk_format_no_srgb(src->image->vk.format),
                           .subresourceRange =
                              {
                                 .aspectMask = src->aspect_mask,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1,
                              },
                        },
                        NULL);

   radv_image_view_init(&dst_iview, device,
                        &(VkImageViewCreateInfo){
                           .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                           .image = radv_image_to_handle(dst->image),
                           .viewType = radv_meta_get_view_type(dst->image),
                           .format = vk_format_no_srgb(dst->image->vk.format),
                           .subresourceRange =
                              {
                                 .aspectMask = dst->aspect_mask,
                                 .baseMipLevel = 0,
                                 .levelCount = 1,
                                 .baseArrayLayer = 0,
                                 .layerCount = 1,
                              },
                        },
                        NULL);

   radv_meta_bind_descriptors(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 2,
                              (VkDescriptorGetInfoEXT[]){{.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                                          .type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                                          .data.pSampledImage =
                                                             (VkDescriptorImageInfo[]){
                                                                {.sampler = VK_NULL_HANDLE,
                                                                 .imageView = radv_image_view_to_handle(&src_iview),
                                                                 .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                                                             }},
                                                         {.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_GET_INFO_EXT,
                                                          .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                          .data.pStorageImage = (VkDescriptorImageInfo[]){
                                                             {.sampler = VK_NULL_HANDLE,
                                                              .imageView = radv_image_view_to_handle(&dst_iview),
                                                              .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                                                          }}});

   radv_unaligned_dispatch(cmd_buffer, src->image->vk.extent.width, src->image->vk.extent.height, 1);

   /* Fixup destination image metadata by copying CMASK/FMASK from the source image. */
   radv_fixup_copy_dst_metadata(cmd_buffer, src->image, dst->image);
}
