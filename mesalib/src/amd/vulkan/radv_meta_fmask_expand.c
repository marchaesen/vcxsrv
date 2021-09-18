/*
 * Copyright © 2019 Valve Corporation
 * Copyright © 2018 Red Hat
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

#include "radv_meta.h"
#include "radv_private.h"
#include "vk_format.h"

static nir_shader *
build_fmask_expand_compute_shader(struct radv_device *device, int samples)
{
   const struct glsl_type *type =
      glsl_sampler_type(GLSL_SAMPLER_DIM_MS, false, true, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_MS, true, GLSL_TYPE_FLOAT);

   nir_builder b =
      nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL, "meta_fmask_expand_cs-%d", samples);
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;

   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_uniform, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;
   output_img->data.access = ACCESS_NON_READABLE;

   nir_ssa_def *invoc_id = nir_load_local_invocation_id(&b);
   nir_ssa_def *wg_id = nir_load_workgroup_id(&b, 32);
   nir_ssa_def *block_size =
      nir_imm_ivec4(&b, b.shader->info.workgroup_size[0], b.shader->info.workgroup_size[1],
                    b.shader->info.workgroup_size[2], 0);

   nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);
   nir_ssa_def *layer_id = nir_channel(&b, wg_id, 2);

   nir_ssa_def *input_img_deref = &nir_build_deref_var(&b, input_img)->dest.ssa;
   nir_ssa_def *output_img_deref = &nir_build_deref_var(&b, output_img)->dest.ssa;

   nir_ssa_def *tex_coord =
      nir_vec3(&b, nir_channel(&b, global_id, 0), nir_channel(&b, global_id, 1), layer_id);

   nir_tex_instr *tex_instr[8];
   for (uint32_t i = 0; i < samples; i++) {
      tex_instr[i] = nir_tex_instr_create(b.shader, 3);

      nir_tex_instr *tex = tex_instr[i];
      tex->sampler_dim = GLSL_SAMPLER_DIM_MS;
      tex->op = nir_texop_txf_ms;
      tex->src[0].src_type = nir_tex_src_coord;
      tex->src[0].src = nir_src_for_ssa(tex_coord);
      tex->src[1].src_type = nir_tex_src_ms_index;
      tex->src[1].src = nir_src_for_ssa(nir_imm_int(&b, i));
      tex->src[2].src_type = nir_tex_src_texture_deref;
      tex->src[2].src = nir_src_for_ssa(input_img_deref);
      tex->dest_type = nir_type_float32;
      tex->is_array = true;
      tex->coord_components = 3;

      nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
      nir_builder_instr_insert(&b, &tex->instr);
   }

   nir_ssa_def *img_coord =
      nir_vec4(&b, nir_channel(&b, tex_coord, 0), nir_channel(&b, tex_coord, 1),
               nir_channel(&b, tex_coord, 2), nir_imm_int(&b, 0));

   for (uint32_t i = 0; i < samples; i++) {
      nir_ssa_def *outval = &tex_instr[i]->dest.ssa;

      nir_image_deref_store(&b, output_img_deref, img_coord, nir_imm_int(&b, i), outval,
                            nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_MS, .image_array = true);
   }

   return b.shader;
}

void
radv_expand_fmask_image_inplace(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image,
                                const VkImageSubresourceRange *subresourceRange)
{
   struct radv_device *device = cmd_buffer->device;
   struct radv_meta_saved_state saved_state;
   const uint32_t samples = image->info.samples;
   const uint32_t samples_log2 = ffs(samples) - 1;
   unsigned layer_count = radv_get_layerCount(image, subresourceRange);
   struct radv_image_view iview;

   radv_meta_save(&saved_state, cmd_buffer,
                  RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_DESCRIPTORS);

   VkPipeline pipeline = device->meta_state.fmask_expand.pipeline[samples_log2];

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        pipeline);

   cmd_buffer->state.flush_bits |= radv_dst_access_flush(
      cmd_buffer, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT, image);

   radv_image_view_init(&iview, device,
                        &(VkImageViewCreateInfo){
                           .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                           .image = radv_image_to_handle(image),
                           .viewType = radv_meta_get_view_type(image),
                           .format = vk_format_no_srgb(image->vk_format),
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

   radv_meta_push_descriptor_set(
      cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
      cmd_buffer->device->meta_state.fmask_expand.p_layout, 0, /* set */
      2,                                                       /* descriptorWriteCount */
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

   radv_unaligned_dispatch(cmd_buffer, image->info.width, image->info.height, layer_count);

   radv_meta_restore(&saved_state, cmd_buffer);

   cmd_buffer->state.flush_bits |=
      RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
      radv_src_access_flush(cmd_buffer, VK_ACCESS_SHADER_WRITE_BIT, image);

   /* Re-initialize FMASK in fully expanded mode. */
   cmd_buffer->state.flush_bits |= radv_init_fmask(cmd_buffer, image, subresourceRange);
}

void
radv_device_finish_meta_fmask_expand_state(struct radv_device *device)
{
   struct radv_meta_state *state = &device->meta_state;

   for (uint32_t i = 0; i < MAX_SAMPLES_LOG2; ++i) {
      radv_DestroyPipeline(radv_device_to_handle(device), state->fmask_expand.pipeline[i],
                           &state->alloc);
   }
   radv_DestroyPipelineLayout(radv_device_to_handle(device), state->fmask_expand.p_layout,
                              &state->alloc);

   radv_DestroyDescriptorSetLayout(radv_device_to_handle(device), state->fmask_expand.ds_layout,
                                   &state->alloc);
}

static VkResult
create_fmask_expand_pipeline(struct radv_device *device, int samples, VkPipeline *pipeline)
{
   struct radv_meta_state *state = &device->meta_state;
   VkResult result;
   nir_shader *cs = build_fmask_expand_compute_shader(device, samples);
   ;

   VkPipelineShaderStageCreateInfo pipeline_shader_stage = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
      .stage = VK_SHADER_STAGE_COMPUTE_BIT,
      .module = vk_shader_module_handle_from_nir(cs),
      .pName = "main",
      .pSpecializationInfo = NULL,
   };

   VkComputePipelineCreateInfo vk_pipeline_info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage = pipeline_shader_stage,
      .flags = 0,
      .layout = state->fmask_expand.p_layout,
   };

   result = radv_CreateComputePipelines(radv_device_to_handle(device),
                                        radv_pipeline_cache_to_handle(&state->cache), 1,
                                        &vk_pipeline_info, NULL, pipeline);

   ralloc_free(cs);
   return result;
}

VkResult
radv_device_init_meta_fmask_expand_state(struct radv_device *device)
{
   struct radv_meta_state *state = &device->meta_state;
   VkResult result;

   VkDescriptorSetLayoutCreateInfo ds_create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
      .bindingCount = 2,
      .pBindings = (VkDescriptorSetLayoutBinding[]){
         {.binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
         {.binding = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
      }};

   result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device), &ds_create_info,
                                           &state->alloc, &state->fmask_expand.ds_layout);
   if (result != VK_SUCCESS)
      goto fail;

   VkPipelineLayoutCreateInfo color_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &state->fmask_expand.ds_layout,
      .pushConstantRangeCount = 0,
      .pPushConstantRanges = NULL,
   };

   result = radv_CreatePipelineLayout(radv_device_to_handle(device), &color_create_info,
                                      &state->alloc, &state->fmask_expand.p_layout);
   if (result != VK_SUCCESS)
      goto fail;

   for (uint32_t i = 0; i < MAX_SAMPLES_LOG2; i++) {
      uint32_t samples = 1 << i;
      result = create_fmask_expand_pipeline(device, samples, &state->fmask_expand.pipeline[i]);
      if (result != VK_SUCCESS)
         goto fail;
   }

   return VK_SUCCESS;
fail:
   radv_device_finish_meta_fmask_expand_state(device);
   return result;
}
