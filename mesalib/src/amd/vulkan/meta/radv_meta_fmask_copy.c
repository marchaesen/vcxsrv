/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */
#include "nir/nir_builder.h"
#include "radv_formats.h"
#include "radv_meta.h"

static nir_shader *
build_fmask_copy_compute_shader(struct radv_device *dev, int samples)
{
   const struct glsl_type *sampler_type = glsl_sampler_type(GLSL_SAMPLER_DIM_MS, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_MS, false, GLSL_TYPE_FLOAT);

   nir_builder b = radv_meta_init_shader(dev, MESA_SHADER_COMPUTE, "meta_fmask_copy_cs_-%d", samples);

   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;

   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, sampler_type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_uniform, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_def *invoc_id = nir_load_local_invocation_id(&b);
   nir_def *wg_id = nir_load_workgroup_id(&b);
   nir_def *block_size = nir_imm_ivec3(&b, b.shader->info.workgroup_size[0], b.shader->info.workgroup_size[1],
                                       b.shader->info.workgroup_size[2]);

   nir_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

   /* Get coordinates. */
   nir_def *src_coord = nir_trim_vector(&b, global_id, 2);
   nir_def *dst_coord = nir_vec4(&b, nir_channel(&b, src_coord, 0), nir_channel(&b, src_coord, 1), nir_undef(&b, 1, 32),
                                 nir_undef(&b, 1, 32));

   nir_tex_src frag_mask_srcs[] = {{
      .src_type = nir_tex_src_coord,
      .src = nir_src_for_ssa(src_coord),
   }};
   nir_def *frag_mask =
      nir_build_tex_deref_instr(&b, nir_texop_fragment_mask_fetch_amd, nir_build_deref_var(&b, input_img), NULL,
                                ARRAY_SIZE(frag_mask_srcs), frag_mask_srcs);

   /* Get the maximum sample used in this fragment. */
   nir_def *max_sample_index = nir_imm_int(&b, 0);
   for (uint32_t s = 0; s < samples; s++) {
      /* max_sample_index = MAX2(max_sample_index, (frag_mask >> (s * 4)) & 0xf) */
      max_sample_index = nir_umax(&b, max_sample_index,
                                  nir_ubitfield_extract(&b, frag_mask, nir_imm_int(&b, 4 * s), nir_imm_int(&b, 4)));
   }

   nir_variable *counter = nir_local_variable_create(b.impl, glsl_int_type(), "counter");
   nir_store_var(&b, counter, nir_imm_int(&b, 0), 0x1);

   nir_loop *loop = nir_push_loop(&b);
   {
      nir_def *sample_id = nir_load_var(&b, counter);

      nir_tex_src frag_fetch_srcs[] = {{
                                          .src_type = nir_tex_src_coord,
                                          .src = nir_src_for_ssa(src_coord),
                                       },
                                       {
                                          .src_type = nir_tex_src_ms_index,
                                          .src = nir_src_for_ssa(sample_id),
                                       }};
      nir_def *outval = nir_build_tex_deref_instr(&b, nir_texop_fragment_fetch_amd, nir_build_deref_var(&b, input_img),
                                                  NULL, ARRAY_SIZE(frag_fetch_srcs), frag_fetch_srcs);

      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, dst_coord, sample_id, outval,
                            nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_MS);

      radv_break_on_count(&b, counter, max_sample_index);
   }
   nir_pop_loop(&b, loop);

   return b.shader;
}

static VkResult
create_pipeline(struct radv_device *device, int samples, VkPipeline *pipeline)
{
   struct radv_meta_state *state = &device->meta_state;
   VkResult result;

   if (!state->fmask_copy.ds_layout) {
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

      result = radv_meta_create_descriptor_set_layout(device, 2, bindings, &state->fmask_copy.ds_layout);
      if (result != VK_SUCCESS)
         return result;
   }

   if (!state->fmask_copy.p_layout) {
      result =
         radv_meta_create_pipeline_layout(device, &state->fmask_copy.ds_layout, 0, NULL, &state->fmask_copy.p_layout);
      if (result != VK_SUCCESS)
         return result;
   }

   nir_shader *cs = build_fmask_copy_compute_shader(device, samples);

   result = radv_meta_create_compute_pipeline(device, cs, state->fmask_copy.p_layout, pipeline);

   ralloc_free(cs);
   return result;
}

static VkResult
get_pipeline(struct radv_device *device, uint32_t samples_log2, VkPipeline *pipeline_out)
{
   struct radv_meta_state *state = &device->meta_state;
   VkResult result = VK_SUCCESS;

   mtx_lock(&state->mtx);
   if (!state->fmask_copy.pipeline[samples_log2]) {
      result = create_pipeline(device, 1 << samples_log2, &state->fmask_copy.pipeline[samples_log2]);
      if (result != VK_SUCCESS)
         goto fail;
   }

   *pipeline_out = state->fmask_copy.pipeline[samples_log2];

fail:
   mtx_unlock(&state->mtx);
   return result;
}

void
radv_device_finish_meta_fmask_copy_state(struct radv_device *device)
{
   struct radv_meta_state *state = &device->meta_state;

   radv_DestroyPipelineLayout(radv_device_to_handle(device), state->fmask_copy.p_layout, &state->alloc);
   device->vk.dispatch_table.DestroyDescriptorSetLayout(radv_device_to_handle(device), state->fmask_copy.ds_layout,
                                                        &state->alloc);

   for (uint32_t i = 0; i < MAX_SAMPLES_LOG2; ++i) {
      radv_DestroyPipeline(radv_device_to_handle(device), state->fmask_copy.pipeline[i], &state->alloc);
   }
}

VkResult
radv_device_init_meta_fmask_copy_state(struct radv_device *device, bool on_demand)
{
   VkResult result;

   if (on_demand)
      return VK_SUCCESS;

   for (uint32_t i = 0; i < MAX_SAMPLES_LOG2; i++) {
      result = create_pipeline(device, 1u << i, &device->meta_state.fmask_copy.pipeline[i]);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

static void
radv_fixup_copy_dst_metadata(struct radv_cmd_buffer *cmd_buffer, const struct radv_image *src_image,
                             const struct radv_image *dst_image)
{
   uint64_t src_offset, dst_offset, size;

   assert(src_image->planes[0].surface.cmask_size == dst_image->planes[0].surface.cmask_size &&
          src_image->planes[0].surface.fmask_size == dst_image->planes[0].surface.fmask_size);
   assert(src_image->planes[0].surface.fmask_offset + src_image->planes[0].surface.fmask_size ==
             src_image->planes[0].surface.cmask_offset &&
          dst_image->planes[0].surface.fmask_offset + dst_image->planes[0].surface.fmask_size ==
             dst_image->planes[0].surface.cmask_offset);

   /* Copy CMASK+FMASK. */
   size = src_image->planes[0].surface.cmask_size + src_image->planes[0].surface.fmask_size;
   src_offset = src_image->bindings[0].offset + src_image->planes[0].surface.fmask_offset;
   dst_offset = dst_image->bindings[0].offset + dst_image->planes[0].surface.fmask_offset;

   radv_copy_buffer(cmd_buffer, src_image->bindings[0].bo, dst_image->bindings[0].bo, src_offset, dst_offset, size);
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
   VkPipeline pipeline;
   VkResult result;

   result = get_pipeline(device, samples_log2, &pipeline);
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

   radv_meta_push_descriptor_set(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, device->meta_state.fmask_copy.p_layout, 0,
                                 2,
                                 (VkWriteDescriptorSet[]){{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                                           .dstBinding = 0,
                                                           .dstArrayElement = 0,
                                                           .descriptorCount = 1,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                                           .pImageInfo =
                                                              (VkDescriptorImageInfo[]){
                                                                 {.sampler = VK_NULL_HANDLE,
                                                                  .imageView = radv_image_view_to_handle(&src_iview),
                                                                  .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                                                              }},
                                                          {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                                           .dstBinding = 1,
                                                           .dstArrayElement = 0,
                                                           .descriptorCount = 1,
                                                           .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                                           .pImageInfo = (VkDescriptorImageInfo[]){
                                                              {.sampler = VK_NULL_HANDLE,
                                                               .imageView = radv_image_view_to_handle(&dst_iview),
                                                               .imageLayout = VK_IMAGE_LAYOUT_GENERAL},
                                                           }}});

   radv_unaligned_dispatch(cmd_buffer, src->image->vk.extent.width, src->image->vk.extent.height, 1);

   /* Fixup destination image metadata by copying CMASK/FMASK from the source image. */
   radv_fixup_copy_dst_metadata(cmd_buffer, src->image, dst->image);
}
