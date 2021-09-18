/*
 * Copyright © 2021 Google
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

#define AC_SURFACE_INCLUDE_NIR
#include "ac_surface.h"

#include "radv_meta.h"
#include "radv_private.h"

static nir_ssa_def *
get_global_ids(nir_builder *b, unsigned num_components)
{
   unsigned mask = BITFIELD_MASK(num_components);

   nir_ssa_def *local_ids = nir_channels(b, nir_load_local_invocation_id(b), mask);
   nir_ssa_def *block_ids = nir_channels(b, nir_load_workgroup_id(b, 32), mask);
   nir_ssa_def *block_size = nir_channels(
      b,
      nir_imm_ivec4(b, b->shader->info.workgroup_size[0], b->shader->info.workgroup_size[1],
                    b->shader->info.workgroup_size[2], 0),
      mask);

   return nir_iadd(b, nir_imul(b, block_ids, block_size), local_ids);
}

static nir_shader *
build_dcc_retile_compute_shader(struct radv_device *dev, struct radeon_surf *surf)
{
   const struct glsl_type *buf_type = glsl_image_type(GLSL_SAMPLER_DIM_BUF, false, GLSL_TYPE_UINT);
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL, "dcc_retile_compute");

   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;

   nir_ssa_def *src_dcc_size = nir_load_push_constant(&b, 2, 32, nir_imm_int(&b, 0), .range = 8);
   nir_ssa_def *src_dcc_pitch = nir_channels(&b, src_dcc_size, 1);
   nir_ssa_def *src_dcc_height = nir_channels(&b, src_dcc_size, 2);

   nir_ssa_def *dst_dcc_size = nir_load_push_constant(&b, 2, 32, nir_imm_int(&b, 8), .range = 8);
   nir_ssa_def *dst_dcc_pitch = nir_channels(&b, dst_dcc_size, 1);
   nir_ssa_def *dst_dcc_height = nir_channels(&b, dst_dcc_size, 2);
   nir_variable *input_dcc = nir_variable_create(b.shader, nir_var_uniform, buf_type, "dcc_in");
   input_dcc->data.descriptor_set = 0;
   input_dcc->data.binding = 0;
   nir_variable *output_dcc = nir_variable_create(b.shader, nir_var_uniform, buf_type, "dcc_out");
   output_dcc->data.descriptor_set = 0;
   output_dcc->data.binding = 1;

   nir_ssa_def *input_dcc_ref = &nir_build_deref_var(&b, input_dcc)->dest.ssa;
   nir_ssa_def *output_dcc_ref = &nir_build_deref_var(&b, output_dcc)->dest.ssa;

   nir_ssa_def *coord = get_global_ids(&b, 2);
   nir_ssa_def *zero = nir_imm_int(&b, 0);
   coord = nir_imul(
      &b, coord,
      nir_imm_ivec2(&b, surf->u.gfx9.color.dcc_block_width, surf->u.gfx9.color.dcc_block_height));

   nir_ssa_def *src = ac_nir_dcc_addr_from_coord(&b, &dev->physical_device->rad_info, surf->bpe,
                                                 &surf->u.gfx9.color.dcc_equation, src_dcc_pitch,
                                                 src_dcc_height, zero, nir_channel(&b, coord, 0),
                                                 nir_channel(&b, coord, 1), zero, zero, zero);
   nir_ssa_def *dst = ac_nir_dcc_addr_from_coord(
      &b, &dev->physical_device->rad_info, surf->bpe, &surf->u.gfx9.color.display_dcc_equation,
      dst_dcc_pitch, dst_dcc_height, zero, nir_channel(&b, coord, 0), nir_channel(&b, coord, 1),
      zero, zero, zero);

   nir_intrinsic_instr *dcc_val =
      nir_intrinsic_instr_create(b.shader, nir_intrinsic_image_deref_load);
   dcc_val->num_components = 1;
   dcc_val->src[0] = nir_src_for_ssa(input_dcc_ref);
   dcc_val->src[1] = nir_src_for_ssa(nir_vec4(&b, src, src, src, src));
   dcc_val->src[2] = nir_src_for_ssa(nir_ssa_undef(&b, 1, 32));
   dcc_val->src[3] = nir_src_for_ssa(nir_imm_int(&b, 0));
   nir_ssa_dest_init(&dcc_val->instr, &dcc_val->dest, 1, 32, "dcc_val");
   nir_intrinsic_set_image_dim(dcc_val, GLSL_SAMPLER_DIM_BUF);
   nir_builder_instr_insert(&b, &dcc_val->instr);

   nir_intrinsic_instr *store =
      nir_intrinsic_instr_create(b.shader, nir_intrinsic_image_deref_store);
   store->num_components = 1;
   store->src[0] = nir_src_for_ssa(output_dcc_ref);
   store->src[1] = nir_src_for_ssa(nir_vec4(&b, dst, dst, dst, dst));
   store->src[2] = nir_src_for_ssa(nir_ssa_undef(&b, 1, 32));
   store->src[3] = nir_src_for_ssa(&dcc_val->dest.ssa);
   store->src[4] = nir_src_for_ssa(nir_imm_int(&b, 0));
   nir_intrinsic_set_image_dim(store, GLSL_SAMPLER_DIM_BUF);

   nir_builder_instr_insert(&b, &store->instr);
   return b.shader;
}

void
radv_device_finish_meta_dcc_retile_state(struct radv_device *device)
{
   struct radv_meta_state *state = &device->meta_state;

   for (unsigned i = 0; i < ARRAY_SIZE(state->dcc_retile.pipeline); i++) {
      radv_DestroyPipeline(radv_device_to_handle(device), state->dcc_retile.pipeline[i],
                           &state->alloc);
   }
   radv_DestroyPipelineLayout(radv_device_to_handle(device), state->dcc_retile.p_layout,
                              &state->alloc);
   radv_DestroyDescriptorSetLayout(radv_device_to_handle(device), state->dcc_retile.ds_layout,
                                   &state->alloc);

   /* Reset for next finish. */
   memset(&state->dcc_retile, 0, sizeof(state->dcc_retile));
}

/*
 * This take a surface, but the only things used are:
 * - BPE
 * - DCC equations
 * - DCC block size
 *
 * BPE is always 4 at the moment and the rest is derived from the tilemode.
 */
static VkResult
radv_device_init_meta_dcc_retile_state(struct radv_device *device, struct radeon_surf *surf)
{
   VkResult result = VK_SUCCESS;
   nir_shader *cs = build_dcc_retile_compute_shader(device, surf);

   VkDescriptorSetLayoutCreateInfo ds_create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
      .bindingCount = 2,
      .pBindings = (VkDescriptorSetLayoutBinding[]){
         {.binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
         {.binding = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
      }};

   result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device), &ds_create_info,
                                           &device->meta_state.alloc,
                                           &device->meta_state.dcc_retile.ds_layout);
   if (result != VK_SUCCESS)
      goto cleanup;

   VkPipelineLayoutCreateInfo pl_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &device->meta_state.dcc_retile.ds_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 16},
   };

   result =
      radv_CreatePipelineLayout(radv_device_to_handle(device), &pl_create_info,
                                &device->meta_state.alloc, &device->meta_state.dcc_retile.p_layout);
   if (result != VK_SUCCESS)
      goto cleanup;

   /* compute shader */

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
      .layout = device->meta_state.dcc_retile.p_layout,
   };

   result = radv_CreateComputePipelines(
      radv_device_to_handle(device), radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
      &vk_pipeline_info, NULL, &device->meta_state.dcc_retile.pipeline[surf->u.gfx9.swizzle_mode]);
   if (result != VK_SUCCESS)
      goto cleanup;

cleanup:
   if (result != VK_SUCCESS)
      radv_device_finish_meta_dcc_retile_state(device);
   ralloc_free(cs);
   return result;
}

void
radv_retile_dcc(struct radv_cmd_buffer *cmd_buffer, struct radv_image *image)
{
   struct radv_meta_saved_state saved_state;
   struct radv_device *device = cmd_buffer->device;

   assert(image->type == VK_IMAGE_TYPE_2D);
   assert(image->info.array_size == 1 && image->info.levels == 1);

   struct radv_cmd_state *state = &cmd_buffer->state;

   state->flush_bits |= radv_dst_access_flush(cmd_buffer, VK_ACCESS_SHADER_READ_BIT, image) |
                        radv_dst_access_flush(cmd_buffer, VK_ACCESS_SHADER_WRITE_BIT, image);

   unsigned swizzle_mode = image->planes[0].surface.u.gfx9.swizzle_mode;

   /* Compile pipelines if not already done so. */
   if (!cmd_buffer->device->meta_state.dcc_retile.pipeline[swizzle_mode]) {
      VkResult ret =
         radv_device_init_meta_dcc_retile_state(cmd_buffer->device, &image->planes[0].surface);
      if (ret != VK_SUCCESS) {
         cmd_buffer->record_result = ret;
         return;
      }
   }

   radv_meta_save(
      &saved_state, cmd_buffer,
      RADV_META_SAVE_DESCRIPTORS | RADV_META_SAVE_COMPUTE_PIPELINE | RADV_META_SAVE_CONSTANTS);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        device->meta_state.dcc_retile.pipeline[swizzle_mode]);

   struct radv_buffer buffer = {.size = image->size, .bo = image->bo, .offset = image->offset};

   struct radv_buffer_view views[2];
   VkBufferView view_handles[2];
   radv_buffer_view_init(views, cmd_buffer->device,
                         &(VkBufferViewCreateInfo){
                            .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
                            .buffer = radv_buffer_to_handle(&buffer),
                            .offset = image->planes[0].surface.meta_offset,
                            .range = image->planes[0].surface.meta_size,
                            .format = VK_FORMAT_R8_UINT,
                         });
   radv_buffer_view_init(views + 1, cmd_buffer->device,
                         &(VkBufferViewCreateInfo){
                            .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
                            .buffer = radv_buffer_to_handle(&buffer),
                            .offset = image->planes[0].surface.display_dcc_offset,
                            .range = image->planes[0].surface.u.gfx9.color.display_dcc_size,
                            .format = VK_FORMAT_R8_UINT,
                         });
   for (unsigned i = 0; i < 2; ++i)
      view_handles[i] = radv_buffer_view_to_handle(&views[i]);

   radv_meta_push_descriptor_set(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                 device->meta_state.dcc_retile.p_layout, 0, /* set */
                                 2, /* descriptorWriteCount */
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

   unsigned width = DIV_ROUND_UP(image->info.width, vk_format_get_blockwidth(image->vk_format));
   unsigned height = DIV_ROUND_UP(image->info.height, vk_format_get_blockheight(image->vk_format));

   unsigned dcc_width = DIV_ROUND_UP(width, image->planes[0].surface.u.gfx9.color.dcc_block_width);
   unsigned dcc_height =
      DIV_ROUND_UP(height, image->planes[0].surface.u.gfx9.color.dcc_block_height);

   uint32_t constants[] = {
      image->planes[0].surface.u.gfx9.color.dcc_pitch_max + 1,
      image->planes[0].surface.u.gfx9.color.dcc_height,
      image->planes[0].surface.u.gfx9.color.display_dcc_pitch_max + 1,
      image->planes[0].surface.u.gfx9.color.display_dcc_height,
   };
   radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                         device->meta_state.dcc_retile.p_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16,
                         constants);

   radv_unaligned_dispatch(cmd_buffer, dcc_width, dcc_height, 1);

   radv_meta_restore(&saved_state, cmd_buffer);

   state->flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
                        radv_src_access_flush(cmd_buffer, VK_ACCESS_SHADER_WRITE_BIT, image);
}
