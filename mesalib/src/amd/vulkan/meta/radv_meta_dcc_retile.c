/*
 * Copyright © 2021 Google
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/radv_meta_nir.h"
#include "ac_surface.h"

#include "radv_meta.h"
#include "vk_common_entrypoints.h"

static VkResult
get_pipeline_layout(struct radv_device *device, VkPipelineLayout *layout_out)
{
   enum radv_meta_object_key_type key = RADV_META_OBJECT_KEY_DCC_RETILE;

   const VkDescriptorSetLayoutBinding bindings[] = {
      {
         .binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
         .descriptorCount = 1,
         .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
      {
         .binding = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
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

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = 16,
   };

   return vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, &pc_range, &key, sizeof(key),
                                      layout_out);
}

struct radv_dcc_retile_key {
   enum radv_meta_object_key_type type;
   uint32_t swizzle;
};

/*
 * This take a surface, but the only things used are:
 * - BPE
 * - DCC equations
 * - DCC block size
 *
 * BPE is always 4 at the moment and the rest is derived from the tilemode.
 */
static VkResult
get_pipeline(struct radv_device *device, struct radv_image *image, VkPipeline *pipeline_out,
             VkPipelineLayout *layout_out)
{
   const unsigned swizzle_mode = image->planes[0].surface.u.gfx9.swizzle_mode;
   struct radv_dcc_retile_key key;
   VkResult result;

   result = get_pipeline_layout(device, layout_out);
   if (result != VK_SUCCESS)
      return result;

   memset(&key, 0, sizeof(key));
   key.type = RADV_META_OBJECT_KEY_DCC_RETILE;
   key.swizzle = swizzle_mode;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, &key, sizeof(key));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = radv_meta_nir_build_dcc_retile_compute_shader(device, &image->planes[0].surface);

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
radv_retile_dcc(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image)
{
   struct radv_meta_saved_state saved_state;
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_buffer buffer;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   assert(image->vk.image_type == VK_IMAGE_TYPE_2D);
   assert(image->vk.array_layers == 1 && image->vk.mip_levels == 1);

   struct radv_cmd_state *state = &cmd_buffer->state;

   result = get_pipeline(device, image, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   state->flush_bits |= radv_dst_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
                                              VK_ACCESS_2_SHADER_READ_BIT, 0, image, NULL);

   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_CONSTANTS);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   radv_buffer_init(&buffer, device, image->bindings[0].bo, image->size, image->bindings[0].offset);

   struct radv_buffer_view views[2];
   VkBufferView view_handles[2];
   radv_buffer_view_init(views, device,
                         &(VkBufferViewCreateInfo){
                            .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
                            .buffer = radv_buffer_to_handle(&buffer),
                            .offset = image->planes[0].surface.meta_offset,
                            .range = image->planes[0].surface.meta_size,
                            .format = VK_FORMAT_R8_UINT,
                         });
   radv_buffer_view_init(views + 1, device,
                         &(VkBufferViewCreateInfo){
                            .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
                            .buffer = radv_buffer_to_handle(&buffer),
                            .offset = image->planes[0].surface.display_dcc_offset,
                            .range = image->planes[0].surface.u.gfx9.color.display_dcc_size,
                            .format = VK_FORMAT_R8_UINT,
                         });
   for (unsigned i = 0; i < 2; ++i)
      view_handles[i] = radv_buffer_view_to_handle(&views[i]);

   radv_meta_push_descriptor_set(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 2,
                                 (VkWriteDescriptorSet[]){
                                    {
                                       .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                       .dstBinding = 0,
                                       .dstArrayElement = 0,
                                       .descriptorCount = 1,
                                       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                                       .pTexelBufferView = &view_handles[0],
                                    },
                                    {
                                       .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                       .dstBinding = 1,
                                       .dstArrayElement = 0,
                                       .descriptorCount = 1,
                                       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                                       .pTexelBufferView = &view_handles[1],
                                    },
                                 });

   unsigned width = DIV_ROUND_UP(image->vk.extent.width, vk_format_get_blockwidth(image->vk.format));
   unsigned height = DIV_ROUND_UP(image->vk.extent.height, vk_format_get_blockheight(image->vk.format));

   unsigned dcc_width = DIV_ROUND_UP(width, image->planes[0].surface.u.gfx9.color.dcc_block_width);
   unsigned dcc_height = DIV_ROUND_UP(height, image->planes[0].surface.u.gfx9.color.dcc_block_height);

   uint32_t constants[] = {
      image->planes[0].surface.u.gfx9.color.dcc_pitch_max + 1,
      image->planes[0].surface.u.gfx9.color.dcc_height,
      image->planes[0].surface.u.gfx9.color.display_dcc_pitch_max + 1,
      image->planes[0].surface.u.gfx9.color.display_dcc_height,
   };
   vk_common_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer), layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16,
                              constants);

   radv_unaligned_dispatch(cmd_buffer, dcc_width, dcc_height, 1);

   radv_buffer_view_finish(views);
   radv_buffer_view_finish(views + 1);
   radv_buffer_finish(&buffer);

   radv_meta_restore(&saved_state, cmd_buffer);

   state->flush_bits |=
      RADV_CMD_FLAG_CS_PARTIAL_FLUSH | radv_src_access_flush(cmd_buffer, VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                                                             VK_ACCESS_2_SHADER_WRITE_BIT, 0, image, NULL);
}
