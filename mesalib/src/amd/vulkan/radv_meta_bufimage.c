/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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
#include "nir/nir_builder.h"
#include "radv_meta.h"

/*
 * GFX queue: Compute shader implementation of image->buffer copy
 * Compute queue: implementation also of buffer->image, image->image, and image clear.
 */

/* GFX9 needs to use a 3D sampler to access 3D resources, so the shader has the options
 * for that.
 */
static nir_shader *
build_nir_itob_compute_shader(struct radv_device *dev, bool is_3d)
{
   enum glsl_sampler_dim dim = is_3d ? GLSL_SAMPLER_DIM_3D : GLSL_SAMPLER_DIM_2D;
   const struct glsl_type *sampler_type = glsl_sampler_type(dim, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_BUF, false, GLSL_TYPE_FLOAT);
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL,
                                                  is_3d ? "meta_itob_cs_3d" : "meta_itob_cs");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, sampler_type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_uniform, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_ssa_def *invoc_id = nir_load_local_invocation_id(&b);
   nir_ssa_def *wg_id = nir_load_workgroup_id(&b, 32);
   nir_ssa_def *block_size =
      nir_imm_ivec4(&b, b.shader->info.workgroup_size[0], b.shader->info.workgroup_size[1],
                    b.shader->info.workgroup_size[2], 0);

   nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

   nir_ssa_def *offset =
      nir_load_push_constant(&b, is_3d ? 3 : 2, 32, nir_imm_int(&b, 0), .range = 16);
   nir_ssa_def *stride = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 12), .range = 16);

   nir_ssa_def *img_coord = nir_iadd(&b, global_id, offset);
   nir_ssa_def *input_img_deref = &nir_build_deref_var(&b, input_img)->dest.ssa;

   nir_tex_instr *tex = nir_tex_instr_create(b.shader, 3);
   tex->sampler_dim = dim;
   tex->op = nir_texop_txf;
   tex->src[0].src_type = nir_tex_src_coord;
   tex->src[0].src = nir_src_for_ssa(nir_channels(&b, img_coord, is_3d ? 0x7 : 0x3));
   tex->src[1].src_type = nir_tex_src_lod;
   tex->src[1].src = nir_src_for_ssa(nir_imm_int(&b, 0));
   tex->src[2].src_type = nir_tex_src_texture_deref;
   tex->src[2].src = nir_src_for_ssa(input_img_deref);
   tex->dest_type = nir_type_float32;
   tex->is_array = false;
   tex->coord_components = is_3d ? 3 : 2;

   nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
   nir_builder_instr_insert(&b, &tex->instr);

   nir_ssa_def *pos_x = nir_channel(&b, global_id, 0);
   nir_ssa_def *pos_y = nir_channel(&b, global_id, 1);

   nir_ssa_def *tmp = nir_imul(&b, pos_y, stride);
   tmp = nir_iadd(&b, tmp, pos_x);

   nir_ssa_def *coord = nir_vec4(&b, tmp, tmp, tmp, tmp);

   nir_ssa_def *outval = &tex->dest.ssa;
   nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->dest.ssa, coord,
                         nir_ssa_undef(&b, 1, 32), outval, nir_imm_int(&b, 0),
                         .image_dim = GLSL_SAMPLER_DIM_BUF);

   return b.shader;
}

/* Image to buffer - don't write use image accessors */
static VkResult
radv_device_init_meta_itob_state(struct radv_device *device)
{
   VkResult result;
   nir_shader *cs = build_nir_itob_compute_shader(device, false);
   nir_shader *cs_3d = NULL;

   if (device->physical_device->rad_info.chip_class >= GFX9)
      cs_3d = build_nir_itob_compute_shader(device, true);

   /*
    * two descriptors one for the image being sampled
    * one for the buffer being written.
    */
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
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
      }};

   result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device), &ds_create_info,
                                           &device->meta_state.alloc,
                                           &device->meta_state.itob.img_ds_layout);
   if (result != VK_SUCCESS)
      goto fail;

   VkPipelineLayoutCreateInfo pl_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &device->meta_state.itob.img_ds_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 16},
   };

   result =
      radv_CreatePipelineLayout(radv_device_to_handle(device), &pl_create_info,
                                &device->meta_state.alloc, &device->meta_state.itob.img_p_layout);
   if (result != VK_SUCCESS)
      goto fail;

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
      .layout = device->meta_state.itob.img_p_layout,
   };

   result = radv_CreateComputePipelines(radv_device_to_handle(device),
                                        radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
                                        &vk_pipeline_info, NULL, &device->meta_state.itob.pipeline);
   if (result != VK_SUCCESS)
      goto fail;

   if (device->physical_device->rad_info.chip_class >= GFX9) {
      VkPipelineShaderStageCreateInfo pipeline_shader_stage_3d = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_COMPUTE_BIT,
         .module = vk_shader_module_handle_from_nir(cs_3d),
         .pName = "main",
         .pSpecializationInfo = NULL,
      };

      VkComputePipelineCreateInfo vk_pipeline_info_3d = {
         .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
         .stage = pipeline_shader_stage_3d,
         .flags = 0,
         .layout = device->meta_state.itob.img_p_layout,
      };

      result = radv_CreateComputePipelines(
         radv_device_to_handle(device), radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
         &vk_pipeline_info_3d, NULL, &device->meta_state.itob.pipeline_3d);
      if (result != VK_SUCCESS)
         goto fail;
      ralloc_free(cs_3d);
   }
   ralloc_free(cs);

   return VK_SUCCESS;
fail:
   ralloc_free(cs);
   ralloc_free(cs_3d);
   return result;
}

static void
radv_device_finish_meta_itob_state(struct radv_device *device)
{
   struct radv_meta_state *state = &device->meta_state;

   radv_DestroyPipelineLayout(radv_device_to_handle(device), state->itob.img_p_layout,
                              &state->alloc);
   radv_DestroyDescriptorSetLayout(radv_device_to_handle(device), state->itob.img_ds_layout,
                                   &state->alloc);
   radv_DestroyPipeline(radv_device_to_handle(device), state->itob.pipeline, &state->alloc);
   if (device->physical_device->rad_info.chip_class >= GFX9)
      radv_DestroyPipeline(radv_device_to_handle(device), state->itob.pipeline_3d, &state->alloc);
}

static nir_shader *
build_nir_btoi_compute_shader(struct radv_device *dev, bool is_3d)
{
   enum glsl_sampler_dim dim = is_3d ? GLSL_SAMPLER_DIM_3D : GLSL_SAMPLER_DIM_2D;
   const struct glsl_type *buf_type =
      glsl_sampler_type(GLSL_SAMPLER_DIM_BUF, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(dim, false, GLSL_TYPE_FLOAT);
   nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL,
                                                  is_3d ? "meta_btoi_cs_3d" : "meta_btoi_cs");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, buf_type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_uniform, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_ssa_def *invoc_id = nir_load_local_invocation_id(&b);
   nir_ssa_def *wg_id = nir_load_workgroup_id(&b, 32);
   nir_ssa_def *block_size =
      nir_imm_ivec4(&b, b.shader->info.workgroup_size[0], b.shader->info.workgroup_size[1],
                    b.shader->info.workgroup_size[2], 0);

   nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

   nir_ssa_def *offset =
      nir_load_push_constant(&b, is_3d ? 3 : 2, 32, nir_imm_int(&b, 0), .range = 16);
   nir_ssa_def *stride = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 12), .range = 16);

   nir_ssa_def *pos_x = nir_channel(&b, global_id, 0);
   nir_ssa_def *pos_y = nir_channel(&b, global_id, 1);

   nir_ssa_def *tmp = nir_imul(&b, pos_y, stride);
   tmp = nir_iadd(&b, tmp, pos_x);

   nir_ssa_def *buf_coord = nir_vec4(&b, tmp, tmp, tmp, tmp);

   nir_ssa_def *img_coord = nir_iadd(&b, global_id, offset);
   nir_ssa_def *input_img_deref = &nir_build_deref_var(&b, input_img)->dest.ssa;

   nir_tex_instr *tex = nir_tex_instr_create(b.shader, 3);
   tex->sampler_dim = GLSL_SAMPLER_DIM_BUF;
   tex->op = nir_texop_txf;
   tex->src[0].src_type = nir_tex_src_coord;
   tex->src[0].src = nir_src_for_ssa(nir_channels(&b, buf_coord, 1));
   tex->src[1].src_type = nir_tex_src_lod;
   tex->src[1].src = nir_src_for_ssa(nir_imm_int(&b, 0));
   tex->src[2].src_type = nir_tex_src_texture_deref;
   tex->src[2].src = nir_src_for_ssa(input_img_deref);
   tex->dest_type = nir_type_float32;
   tex->is_array = false;
   tex->coord_components = 1;

   nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
   nir_builder_instr_insert(&b, &tex->instr);

   nir_ssa_def *outval = &tex->dest.ssa;
   nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->dest.ssa, img_coord,
                         nir_ssa_undef(&b, 1, 32), outval, nir_imm_int(&b, 0), .image_dim = dim);

   return b.shader;
}

/* Buffer to image - don't write use image accessors */
static VkResult
radv_device_init_meta_btoi_state(struct radv_device *device)
{
   VkResult result;
   nir_shader *cs = build_nir_btoi_compute_shader(device, false);
   nir_shader *cs_3d = NULL;
   if (device->physical_device->rad_info.chip_class >= GFX9)
      cs_3d = build_nir_btoi_compute_shader(device, true);
   /*
    * two descriptors one for the image being sampled
    * one for the buffer being written.
    */
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
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
      }};

   result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device), &ds_create_info,
                                           &device->meta_state.alloc,
                                           &device->meta_state.btoi.img_ds_layout);
   if (result != VK_SUCCESS)
      goto fail;

   VkPipelineLayoutCreateInfo pl_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &device->meta_state.btoi.img_ds_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 16},
   };

   result =
      radv_CreatePipelineLayout(radv_device_to_handle(device), &pl_create_info,
                                &device->meta_state.alloc, &device->meta_state.btoi.img_p_layout);
   if (result != VK_SUCCESS)
      goto fail;

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
      .layout = device->meta_state.btoi.img_p_layout,
   };

   result = radv_CreateComputePipelines(radv_device_to_handle(device),
                                        radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
                                        &vk_pipeline_info, NULL, &device->meta_state.btoi.pipeline);
   if (result != VK_SUCCESS)
      goto fail;

   if (device->physical_device->rad_info.chip_class >= GFX9) {
      VkPipelineShaderStageCreateInfo pipeline_shader_stage_3d = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_COMPUTE_BIT,
         .module = vk_shader_module_handle_from_nir(cs_3d),
         .pName = "main",
         .pSpecializationInfo = NULL,
      };

      VkComputePipelineCreateInfo vk_pipeline_info_3d = {
         .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
         .stage = pipeline_shader_stage_3d,
         .flags = 0,
         .layout = device->meta_state.btoi.img_p_layout,
      };

      result = radv_CreateComputePipelines(
         radv_device_to_handle(device), radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
         &vk_pipeline_info_3d, NULL, &device->meta_state.btoi.pipeline_3d);
      ralloc_free(cs_3d);
   }
   ralloc_free(cs);

   return VK_SUCCESS;
fail:
   ralloc_free(cs_3d);
   ralloc_free(cs);
   return result;
}

static void
radv_device_finish_meta_btoi_state(struct radv_device *device)
{
   struct radv_meta_state *state = &device->meta_state;

   radv_DestroyPipelineLayout(radv_device_to_handle(device), state->btoi.img_p_layout,
                              &state->alloc);
   radv_DestroyDescriptorSetLayout(radv_device_to_handle(device), state->btoi.img_ds_layout,
                                   &state->alloc);
   radv_DestroyPipeline(radv_device_to_handle(device), state->btoi.pipeline, &state->alloc);
   radv_DestroyPipeline(radv_device_to_handle(device), state->btoi.pipeline_3d, &state->alloc);
}

/* Buffer to image - special path for R32G32B32 */
static nir_shader *
build_nir_btoi_r32g32b32_compute_shader(struct radv_device *dev)
{
   const struct glsl_type *buf_type =
      glsl_sampler_type(GLSL_SAMPLER_DIM_BUF, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_BUF, false, GLSL_TYPE_FLOAT);
   nir_builder b =
      nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL, "meta_btoi_r32g32b32_cs");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, buf_type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_uniform, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_ssa_def *invoc_id = nir_load_local_invocation_id(&b);
   nir_ssa_def *wg_id = nir_load_workgroup_id(&b, 32);
   nir_ssa_def *block_size =
      nir_imm_ivec4(&b, b.shader->info.workgroup_size[0], b.shader->info.workgroup_size[1],
                    b.shader->info.workgroup_size[2], 0);

   nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

   nir_ssa_def *offset = nir_load_push_constant(&b, 2, 32, nir_imm_int(&b, 0), .range = 16);
   nir_ssa_def *pitch = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 8), .range = 16);
   nir_ssa_def *stride = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 12), .range = 16);

   nir_ssa_def *pos_x = nir_channel(&b, global_id, 0);
   nir_ssa_def *pos_y = nir_channel(&b, global_id, 1);

   nir_ssa_def *tmp = nir_imul(&b, pos_y, stride);
   tmp = nir_iadd(&b, tmp, pos_x);

   nir_ssa_def *buf_coord = nir_vec4(&b, tmp, tmp, tmp, tmp);

   nir_ssa_def *img_coord = nir_iadd(&b, global_id, offset);

   nir_ssa_def *global_pos =
      nir_iadd(&b, nir_imul(&b, nir_channel(&b, img_coord, 1), pitch),
               nir_imul(&b, nir_channel(&b, img_coord, 0), nir_imm_int(&b, 3)));

   nir_ssa_def *input_img_deref = &nir_build_deref_var(&b, input_img)->dest.ssa;

   nir_tex_instr *tex = nir_tex_instr_create(b.shader, 3);
   tex->sampler_dim = GLSL_SAMPLER_DIM_BUF;
   tex->op = nir_texop_txf;
   tex->src[0].src_type = nir_tex_src_coord;
   tex->src[0].src = nir_src_for_ssa(nir_channels(&b, buf_coord, 1));
   tex->src[1].src_type = nir_tex_src_lod;
   tex->src[1].src = nir_src_for_ssa(nir_imm_int(&b, 0));
   tex->src[2].src_type = nir_tex_src_texture_deref;
   tex->src[2].src = nir_src_for_ssa(input_img_deref);
   tex->dest_type = nir_type_float32;
   tex->is_array = false;
   tex->coord_components = 1;
   nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
   nir_builder_instr_insert(&b, &tex->instr);

   nir_ssa_def *outval = &tex->dest.ssa;

   for (int chan = 0; chan < 3; chan++) {
      nir_ssa_def *local_pos = nir_iadd(&b, global_pos, nir_imm_int(&b, chan));

      nir_ssa_def *coord = nir_vec4(&b, local_pos, local_pos, local_pos, local_pos);

      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->dest.ssa, coord,
                            nir_ssa_undef(&b, 1, 32), nir_channel(&b, outval, chan),
                            nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_BUF);
   }

   return b.shader;
}

static VkResult
radv_device_init_meta_btoi_r32g32b32_state(struct radv_device *device)
{
   VkResult result;
   nir_shader *cs = build_nir_btoi_r32g32b32_compute_shader(device);

   VkDescriptorSetLayoutCreateInfo ds_create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
      .bindingCount = 2,
      .pBindings = (VkDescriptorSetLayoutBinding[]){
         {.binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
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
                                           &device->meta_state.btoi_r32g32b32.img_ds_layout);
   if (result != VK_SUCCESS)
      goto fail;

   VkPipelineLayoutCreateInfo pl_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &device->meta_state.btoi_r32g32b32.img_ds_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 16},
   };

   result = radv_CreatePipelineLayout(radv_device_to_handle(device), &pl_create_info,
                                      &device->meta_state.alloc,
                                      &device->meta_state.btoi_r32g32b32.img_p_layout);
   if (result != VK_SUCCESS)
      goto fail;

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
      .layout = device->meta_state.btoi_r32g32b32.img_p_layout,
   };

   result = radv_CreateComputePipelines(
      radv_device_to_handle(device), radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
      &vk_pipeline_info, NULL, &device->meta_state.btoi_r32g32b32.pipeline);

fail:
   ralloc_free(cs);
   return result;
}

static void
radv_device_finish_meta_btoi_r32g32b32_state(struct radv_device *device)
{
   struct radv_meta_state *state = &device->meta_state;

   radv_DestroyPipelineLayout(radv_device_to_handle(device), state->btoi_r32g32b32.img_p_layout,
                              &state->alloc);
   radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
                                   state->btoi_r32g32b32.img_ds_layout, &state->alloc);
   radv_DestroyPipeline(radv_device_to_handle(device), state->btoi_r32g32b32.pipeline,
                        &state->alloc);
}

static nir_shader *
build_nir_itoi_compute_shader(struct radv_device *dev, bool is_3d, int samples)
{
   bool is_multisampled = samples > 1;
   enum glsl_sampler_dim dim = is_3d             ? GLSL_SAMPLER_DIM_3D
                               : is_multisampled ? GLSL_SAMPLER_DIM_MS
                                                 : GLSL_SAMPLER_DIM_2D;
   const struct glsl_type *buf_type = glsl_sampler_type(dim, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(dim, false, GLSL_TYPE_FLOAT);
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, NULL, is_3d ? "meta_itoi_cs_3d-%d" : "meta_itoi_cs-%d", samples);
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, buf_type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_uniform, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_ssa_def *invoc_id = nir_load_local_invocation_id(&b);
   nir_ssa_def *wg_id = nir_load_workgroup_id(&b, 32);
   nir_ssa_def *block_size =
      nir_imm_ivec4(&b, b.shader->info.workgroup_size[0], b.shader->info.workgroup_size[1],
                    b.shader->info.workgroup_size[2], 0);

   nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

   nir_ssa_def *src_offset =
      nir_load_push_constant(&b, is_3d ? 3 : 2, 32, nir_imm_int(&b, 0), .range = 24);
   nir_ssa_def *dst_offset =
      nir_load_push_constant(&b, is_3d ? 3 : 2, 32, nir_imm_int(&b, 12), .range = 24);

   nir_ssa_def *src_coord = nir_iadd(&b, global_id, src_offset);
   nir_ssa_def *input_img_deref = &nir_build_deref_var(&b, input_img)->dest.ssa;

   nir_ssa_def *dst_coord = nir_iadd(&b, global_id, dst_offset);

   nir_tex_instr *tex_instr[8];
   for (uint32_t i = 0; i < samples; i++) {
      tex_instr[i] = nir_tex_instr_create(b.shader, is_multisampled ? 4 : 3);

      nir_tex_instr *tex = tex_instr[i];
      tex->sampler_dim = dim;
      tex->op = is_multisampled ? nir_texop_txf_ms : nir_texop_txf;
      tex->src[0].src_type = nir_tex_src_coord;
      tex->src[0].src = nir_src_for_ssa(nir_channels(&b, src_coord, is_3d ? 0x7 : 0x3));
      tex->src[1].src_type = nir_tex_src_lod;
      tex->src[1].src = nir_src_for_ssa(nir_imm_int(&b, 0));
      tex->src[2].src_type = nir_tex_src_texture_deref;
      tex->src[2].src = nir_src_for_ssa(input_img_deref);
      if (is_multisampled) {
         tex->src[3].src_type = nir_tex_src_ms_index;
         tex->src[3].src = nir_src_for_ssa(nir_imm_int(&b, i));
      }
      tex->dest_type = nir_type_float32;
      tex->is_array = false;
      tex->coord_components = is_3d ? 3 : 2;

      nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
      nir_builder_instr_insert(&b, &tex->instr);
   }

   for (uint32_t i = 0; i < samples; i++) {
      nir_ssa_def *outval = &tex_instr[i]->dest.ssa;
      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->dest.ssa, dst_coord,
                            nir_imm_int(&b, i), outval, nir_imm_int(&b, 0), .image_dim = dim);
   }

   return b.shader;
}

static VkResult
create_itoi_pipeline(struct radv_device *device, int samples, VkPipeline *pipeline)
{
   struct radv_meta_state *state = &device->meta_state;
   nir_shader *cs = build_nir_itoi_compute_shader(device, false, samples);
   VkResult result;

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
      .layout = state->itoi.img_p_layout,
   };

   result = radv_CreateComputePipelines(radv_device_to_handle(device),
                                        radv_pipeline_cache_to_handle(&state->cache), 1,
                                        &vk_pipeline_info, NULL, pipeline);
   ralloc_free(cs);
   return result;
}

/* image to image - don't write use image accessors */
static VkResult
radv_device_init_meta_itoi_state(struct radv_device *device)
{
   VkResult result;

   /*
    * two descriptors one for the image being sampled
    * one for the buffer being written.
    */
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
                                           &device->meta_state.alloc,
                                           &device->meta_state.itoi.img_ds_layout);
   if (result != VK_SUCCESS)
      goto fail;

   VkPipelineLayoutCreateInfo pl_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &device->meta_state.itoi.img_ds_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 24},
   };

   result =
      radv_CreatePipelineLayout(radv_device_to_handle(device), &pl_create_info,
                                &device->meta_state.alloc, &device->meta_state.itoi.img_p_layout);
   if (result != VK_SUCCESS)
      goto fail;

   for (uint32_t i = 0; i < MAX_SAMPLES_LOG2; i++) {
      uint32_t samples = 1 << i;
      result = create_itoi_pipeline(device, samples, &device->meta_state.itoi.pipeline[i]);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (device->physical_device->rad_info.chip_class >= GFX9) {
      nir_shader *cs_3d = build_nir_itoi_compute_shader(device, true, 1);

      VkPipelineShaderStageCreateInfo pipeline_shader_stage_3d = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_COMPUTE_BIT,
         .module = vk_shader_module_handle_from_nir(cs_3d),
         .pName = "main",
         .pSpecializationInfo = NULL,
      };

      VkComputePipelineCreateInfo vk_pipeline_info_3d = {
         .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
         .stage = pipeline_shader_stage_3d,
         .flags = 0,
         .layout = device->meta_state.itoi.img_p_layout,
      };

      result = radv_CreateComputePipelines(
         radv_device_to_handle(device), radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
         &vk_pipeline_info_3d, NULL, &device->meta_state.itoi.pipeline_3d);
      ralloc_free(cs_3d);
   }

   return VK_SUCCESS;
fail:
   return result;
}

static void
radv_device_finish_meta_itoi_state(struct radv_device *device)
{
   struct radv_meta_state *state = &device->meta_state;

   radv_DestroyPipelineLayout(radv_device_to_handle(device), state->itoi.img_p_layout,
                              &state->alloc);
   radv_DestroyDescriptorSetLayout(radv_device_to_handle(device), state->itoi.img_ds_layout,
                                   &state->alloc);

   for (uint32_t i = 0; i < MAX_SAMPLES_LOG2; ++i) {
      radv_DestroyPipeline(radv_device_to_handle(device), state->itoi.pipeline[i], &state->alloc);
   }

   if (device->physical_device->rad_info.chip_class >= GFX9)
      radv_DestroyPipeline(radv_device_to_handle(device), state->itoi.pipeline_3d, &state->alloc);
}

static nir_shader *
build_nir_itoi_r32g32b32_compute_shader(struct radv_device *dev)
{
   const struct glsl_type *type =
      glsl_sampler_type(GLSL_SAMPLER_DIM_BUF, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_BUF, false, GLSL_TYPE_FLOAT);
   nir_builder b =
      nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL, "meta_itoi_r32g32b32_cs");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, type, "input_img");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img =
      nir_variable_create(b.shader, nir_var_uniform, img_type, "output_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_ssa_def *invoc_id = nir_load_local_invocation_id(&b);
   nir_ssa_def *wg_id = nir_load_workgroup_id(&b, 32);
   nir_ssa_def *block_size =
      nir_imm_ivec4(&b, b.shader->info.workgroup_size[0], b.shader->info.workgroup_size[1],
                    b.shader->info.workgroup_size[2], 0);

   nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

   nir_ssa_def *src_offset = nir_load_push_constant(&b, 3, 32, nir_imm_int(&b, 0), .range = 24);
   nir_ssa_def *dst_offset = nir_load_push_constant(&b, 3, 32, nir_imm_int(&b, 12), .range = 24);

   nir_ssa_def *src_stride = nir_channel(&b, src_offset, 2);
   nir_ssa_def *dst_stride = nir_channel(&b, dst_offset, 2);

   nir_ssa_def *src_img_coord = nir_iadd(&b, global_id, src_offset);
   nir_ssa_def *dst_img_coord = nir_iadd(&b, global_id, dst_offset);

   nir_ssa_def *src_global_pos =
      nir_iadd(&b, nir_imul(&b, nir_channel(&b, src_img_coord, 1), src_stride),
               nir_imul(&b, nir_channel(&b, src_img_coord, 0), nir_imm_int(&b, 3)));

   nir_ssa_def *dst_global_pos =
      nir_iadd(&b, nir_imul(&b, nir_channel(&b, dst_img_coord, 1), dst_stride),
               nir_imul(&b, nir_channel(&b, dst_img_coord, 0), nir_imm_int(&b, 3)));

   for (int chan = 0; chan < 3; chan++) {
      /* src */
      nir_ssa_def *src_local_pos = nir_iadd(&b, src_global_pos, nir_imm_int(&b, chan));

      nir_ssa_def *src_coord =
         nir_vec4(&b, src_local_pos, src_local_pos, src_local_pos, src_local_pos);

      nir_ssa_def *input_img_deref = &nir_build_deref_var(&b, input_img)->dest.ssa;

      nir_tex_instr *tex = nir_tex_instr_create(b.shader, 3);
      tex->sampler_dim = GLSL_SAMPLER_DIM_BUF;
      tex->op = nir_texop_txf;
      tex->src[0].src_type = nir_tex_src_coord;
      tex->src[0].src = nir_src_for_ssa(nir_channels(&b, src_coord, 1));
      tex->src[1].src_type = nir_tex_src_lod;
      tex->src[1].src = nir_src_for_ssa(nir_imm_int(&b, 0));
      tex->src[2].src_type = nir_tex_src_texture_deref;
      tex->src[2].src = nir_src_for_ssa(input_img_deref);
      tex->dest_type = nir_type_float32;
      tex->is_array = false;
      tex->coord_components = 1;
      nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
      nir_builder_instr_insert(&b, &tex->instr);

      nir_ssa_def *outval = &tex->dest.ssa;

      /* dst */
      nir_ssa_def *dst_local_pos = nir_iadd(&b, dst_global_pos, nir_imm_int(&b, chan));

      nir_ssa_def *dst_coord =
         nir_vec4(&b, dst_local_pos, dst_local_pos, dst_local_pos, dst_local_pos);

      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->dest.ssa, dst_coord,
                            nir_ssa_undef(&b, 1, 32), nir_channel(&b, outval, 0),
                            nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_BUF);
   }

   return b.shader;
}

/* Image to image - special path for R32G32B32 */
static VkResult
radv_device_init_meta_itoi_r32g32b32_state(struct radv_device *device)
{
   VkResult result;
   nir_shader *cs = build_nir_itoi_r32g32b32_compute_shader(device);

   VkDescriptorSetLayoutCreateInfo ds_create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
      .bindingCount = 2,
      .pBindings = (VkDescriptorSetLayoutBinding[]){
         {.binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
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
                                           &device->meta_state.itoi_r32g32b32.img_ds_layout);
   if (result != VK_SUCCESS)
      goto fail;

   VkPipelineLayoutCreateInfo pl_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &device->meta_state.itoi_r32g32b32.img_ds_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 24},
   };

   result = radv_CreatePipelineLayout(radv_device_to_handle(device), &pl_create_info,
                                      &device->meta_state.alloc,
                                      &device->meta_state.itoi_r32g32b32.img_p_layout);
   if (result != VK_SUCCESS)
      goto fail;

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
      .layout = device->meta_state.itoi_r32g32b32.img_p_layout,
   };

   result = radv_CreateComputePipelines(
      radv_device_to_handle(device), radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
      &vk_pipeline_info, NULL, &device->meta_state.itoi_r32g32b32.pipeline);

fail:
   ralloc_free(cs);
   return result;
}

static void
radv_device_finish_meta_itoi_r32g32b32_state(struct radv_device *device)
{
   struct radv_meta_state *state = &device->meta_state;

   radv_DestroyPipelineLayout(radv_device_to_handle(device), state->itoi_r32g32b32.img_p_layout,
                              &state->alloc);
   radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
                                   state->itoi_r32g32b32.img_ds_layout, &state->alloc);
   radv_DestroyPipeline(radv_device_to_handle(device), state->itoi_r32g32b32.pipeline,
                        &state->alloc);
}

static nir_shader *
build_nir_cleari_compute_shader(struct radv_device *dev, bool is_3d, int samples)
{
   bool is_multisampled = samples > 1;
   enum glsl_sampler_dim dim = is_3d             ? GLSL_SAMPLER_DIM_3D
                               : is_multisampled ? GLSL_SAMPLER_DIM_MS
                                                 : GLSL_SAMPLER_DIM_2D;
   const struct glsl_type *img_type = glsl_image_type(dim, false, GLSL_TYPE_FLOAT);
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, NULL, is_3d ? "meta_cleari_cs_3d-%d" : "meta_cleari_cs-%d", samples);
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_uniform, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 0;

   nir_ssa_def *invoc_id = nir_load_local_invocation_id(&b);
   nir_ssa_def *wg_id = nir_load_workgroup_id(&b, 32);
   nir_ssa_def *block_size =
      nir_imm_ivec4(&b, b.shader->info.workgroup_size[0], b.shader->info.workgroup_size[1],
                    b.shader->info.workgroup_size[2], 0);

   nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

   nir_ssa_def *clear_val = nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .range = 20);
   nir_ssa_def *layer = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 16), .range = 20);

   nir_ssa_def *global_z = nir_iadd(&b, nir_channel(&b, global_id, 2), layer);

   nir_ssa_def *comps[4];
   comps[0] = nir_channel(&b, global_id, 0);
   comps[1] = nir_channel(&b, global_id, 1);
   comps[2] = global_z;
   comps[3] = nir_imm_int(&b, 0);
   global_id = nir_vec(&b, comps, 4);

   for (uint32_t i = 0; i < samples; i++) {
      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->dest.ssa, global_id,
                            nir_imm_int(&b, i), clear_val, nir_imm_int(&b, 0), .image_dim = dim);
   }

   return b.shader;
}

static VkResult
create_cleari_pipeline(struct radv_device *device, int samples, VkPipeline *pipeline)
{
   nir_shader *cs = build_nir_cleari_compute_shader(device, false, samples);
   VkResult result;

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
      .layout = device->meta_state.cleari.img_p_layout,
   };

   result = radv_CreateComputePipelines(radv_device_to_handle(device),
                                        radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
                                        &vk_pipeline_info, NULL, pipeline);
   ralloc_free(cs);
   return result;
}

static VkResult
radv_device_init_meta_cleari_state(struct radv_device *device)
{
   VkResult result;

   /*
    * two descriptors one for the image being sampled
    * one for the buffer being written.
    */
   VkDescriptorSetLayoutCreateInfo ds_create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
      .bindingCount = 1,
      .pBindings = (VkDescriptorSetLayoutBinding[]){
         {.binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
      }};

   result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device), &ds_create_info,
                                           &device->meta_state.alloc,
                                           &device->meta_state.cleari.img_ds_layout);
   if (result != VK_SUCCESS)
      goto fail;

   VkPipelineLayoutCreateInfo pl_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &device->meta_state.cleari.img_ds_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 20},
   };

   result =
      radv_CreatePipelineLayout(radv_device_to_handle(device), &pl_create_info,
                                &device->meta_state.alloc, &device->meta_state.cleari.img_p_layout);
   if (result != VK_SUCCESS)
      goto fail;

   for (uint32_t i = 0; i < MAX_SAMPLES_LOG2; i++) {
      uint32_t samples = 1 << i;
      result = create_cleari_pipeline(device, samples, &device->meta_state.cleari.pipeline[i]);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (device->physical_device->rad_info.chip_class >= GFX9) {
      nir_shader *cs_3d = build_nir_cleari_compute_shader(device, true, 1);

      /* compute shader */
      VkPipelineShaderStageCreateInfo pipeline_shader_stage_3d = {
         .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
         .stage = VK_SHADER_STAGE_COMPUTE_BIT,
         .module = vk_shader_module_handle_from_nir(cs_3d),
         .pName = "main",
         .pSpecializationInfo = NULL,
      };

      VkComputePipelineCreateInfo vk_pipeline_info_3d = {
         .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
         .stage = pipeline_shader_stage_3d,
         .flags = 0,
         .layout = device->meta_state.cleari.img_p_layout,
      };

      result = radv_CreateComputePipelines(
         radv_device_to_handle(device), radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
         &vk_pipeline_info_3d, NULL, &device->meta_state.cleari.pipeline_3d);
      ralloc_free(cs_3d);
   }

   return VK_SUCCESS;
fail:
   return result;
}

static void
radv_device_finish_meta_cleari_state(struct radv_device *device)
{
   struct radv_meta_state *state = &device->meta_state;

   radv_DestroyPipelineLayout(radv_device_to_handle(device), state->cleari.img_p_layout,
                              &state->alloc);
   radv_DestroyDescriptorSetLayout(radv_device_to_handle(device), state->cleari.img_ds_layout,
                                   &state->alloc);

   for (uint32_t i = 0; i < MAX_SAMPLES_LOG2; ++i) {
      radv_DestroyPipeline(radv_device_to_handle(device), state->cleari.pipeline[i], &state->alloc);
   }

   radv_DestroyPipeline(radv_device_to_handle(device), state->cleari.pipeline_3d, &state->alloc);
}

/* Special path for clearing R32G32B32 images using a compute shader. */
static nir_shader *
build_nir_cleari_r32g32b32_compute_shader(struct radv_device *dev)
{
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_BUF, false, GLSL_TYPE_FLOAT);
   nir_builder b =
      nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, NULL, "meta_cleari_r32g32b32_cs");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   b.shader->info.workgroup_size[2] = 1;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_uniform, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 0;

   nir_ssa_def *invoc_id = nir_load_local_invocation_id(&b);
   nir_ssa_def *wg_id = nir_load_workgroup_id(&b, 32);
   nir_ssa_def *block_size =
      nir_imm_ivec4(&b, b.shader->info.workgroup_size[0], b.shader->info.workgroup_size[1],
                    b.shader->info.workgroup_size[2], 0);

   nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

   nir_ssa_def *clear_val = nir_load_push_constant(&b, 3, 32, nir_imm_int(&b, 0), .range = 16);
   nir_ssa_def *stride = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 12), .range = 16);

   nir_ssa_def *global_x = nir_channel(&b, global_id, 0);
   nir_ssa_def *global_y = nir_channel(&b, global_id, 1);

   nir_ssa_def *global_pos =
      nir_iadd(&b, nir_imul(&b, global_y, stride), nir_imul(&b, global_x, nir_imm_int(&b, 3)));

   for (unsigned chan = 0; chan < 3; chan++) {
      nir_ssa_def *local_pos = nir_iadd(&b, global_pos, nir_imm_int(&b, chan));

      nir_ssa_def *coord = nir_vec4(&b, local_pos, local_pos, local_pos, local_pos);

      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->dest.ssa, coord,
                            nir_ssa_undef(&b, 1, 32), nir_channel(&b, clear_val, chan),
                            nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_BUF);
   }

   return b.shader;
}

static VkResult
radv_device_init_meta_cleari_r32g32b32_state(struct radv_device *device)
{
   VkResult result;
   nir_shader *cs = build_nir_cleari_r32g32b32_compute_shader(device);

   VkDescriptorSetLayoutCreateInfo ds_create_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
      .bindingCount = 1,
      .pBindings = (VkDescriptorSetLayoutBinding[]){
         {.binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
          .pImmutableSamplers = NULL},
      }};

   result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device), &ds_create_info,
                                           &device->meta_state.alloc,
                                           &device->meta_state.cleari_r32g32b32.img_ds_layout);
   if (result != VK_SUCCESS)
      goto fail;

   VkPipelineLayoutCreateInfo pl_create_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
      .setLayoutCount = 1,
      .pSetLayouts = &device->meta_state.cleari_r32g32b32.img_ds_layout,
      .pushConstantRangeCount = 1,
      .pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 16},
   };

   result = radv_CreatePipelineLayout(radv_device_to_handle(device), &pl_create_info,
                                      &device->meta_state.alloc,
                                      &device->meta_state.cleari_r32g32b32.img_p_layout);
   if (result != VK_SUCCESS)
      goto fail;

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
      .layout = device->meta_state.cleari_r32g32b32.img_p_layout,
   };

   result = radv_CreateComputePipelines(
      radv_device_to_handle(device), radv_pipeline_cache_to_handle(&device->meta_state.cache), 1,
      &vk_pipeline_info, NULL, &device->meta_state.cleari_r32g32b32.pipeline);

fail:
   ralloc_free(cs);
   return result;
}

static void
radv_device_finish_meta_cleari_r32g32b32_state(struct radv_device *device)
{
   struct radv_meta_state *state = &device->meta_state;

   radv_DestroyPipelineLayout(radv_device_to_handle(device), state->cleari_r32g32b32.img_p_layout,
                              &state->alloc);
   radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
                                   state->cleari_r32g32b32.img_ds_layout, &state->alloc);
   radv_DestroyPipeline(radv_device_to_handle(device), state->cleari_r32g32b32.pipeline,
                        &state->alloc);
}

void
radv_device_finish_meta_bufimage_state(struct radv_device *device)
{
   radv_device_finish_meta_itob_state(device);
   radv_device_finish_meta_btoi_state(device);
   radv_device_finish_meta_btoi_r32g32b32_state(device);
   radv_device_finish_meta_itoi_state(device);
   radv_device_finish_meta_itoi_r32g32b32_state(device);
   radv_device_finish_meta_cleari_state(device);
   radv_device_finish_meta_cleari_r32g32b32_state(device);
}

VkResult
radv_device_init_meta_bufimage_state(struct radv_device *device)
{
   VkResult result;

   result = radv_device_init_meta_itob_state(device);
   if (result != VK_SUCCESS)
      goto fail_itob;

   result = radv_device_init_meta_btoi_state(device);
   if (result != VK_SUCCESS)
      goto fail_btoi;

   result = radv_device_init_meta_btoi_r32g32b32_state(device);
   if (result != VK_SUCCESS)
      goto fail_btoi_r32g32b32;

   result = radv_device_init_meta_itoi_state(device);
   if (result != VK_SUCCESS)
      goto fail_itoi;

   result = radv_device_init_meta_itoi_r32g32b32_state(device);
   if (result != VK_SUCCESS)
      goto fail_itoi_r32g32b32;

   result = radv_device_init_meta_cleari_state(device);
   if (result != VK_SUCCESS)
      goto fail_cleari;

   result = radv_device_init_meta_cleari_r32g32b32_state(device);
   if (result != VK_SUCCESS)
      goto fail_cleari_r32g32b32;

   return VK_SUCCESS;
fail_cleari_r32g32b32:
   radv_device_finish_meta_cleari_r32g32b32_state(device);
fail_cleari:
   radv_device_finish_meta_cleari_state(device);
fail_itoi_r32g32b32:
   radv_device_finish_meta_itoi_r32g32b32_state(device);
fail_itoi:
   radv_device_finish_meta_itoi_state(device);
fail_btoi_r32g32b32:
   radv_device_finish_meta_btoi_r32g32b32_state(device);
fail_btoi:
   radv_device_finish_meta_btoi_state(device);
fail_itob:
   radv_device_finish_meta_itob_state(device);
   return result;
}

static void
create_iview(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *surf,
             struct radv_image_view *iview, VkFormat format, VkImageAspectFlagBits aspects)
{
   VkImageViewType view_type = cmd_buffer->device->physical_device->rad_info.chip_class < GFX9
                                  ? VK_IMAGE_VIEW_TYPE_2D
                                  : radv_meta_get_view_type(surf->image);

   if (format == VK_FORMAT_UNDEFINED)
      format = surf->format;

   radv_image_view_init(iview, cmd_buffer->device,
                        &(VkImageViewCreateInfo){
                           .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                           .image = radv_image_to_handle(surf->image),
                           .viewType = view_type,
                           .format = format,
                           .subresourceRange = {.aspectMask = aspects,
                                                .baseMipLevel = surf->level,
                                                .levelCount = 1,
                                                .baseArrayLayer = surf->layer,
                                                .layerCount = 1},
                        },
                        &(struct radv_image_view_extra_create_info){
                           .disable_compression = surf->disable_compression,
                        });
}

static void
create_bview(struct radv_cmd_buffer *cmd_buffer, struct radv_buffer *buffer, unsigned offset,
             VkFormat format, struct radv_buffer_view *bview)
{
   radv_buffer_view_init(bview, cmd_buffer->device,
                         &(VkBufferViewCreateInfo){
                            .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
                            .flags = 0,
                            .buffer = radv_buffer_to_handle(buffer),
                            .format = format,
                            .offset = offset,
                            .range = VK_WHOLE_SIZE,
                         });
}

static void
create_buffer_from_image(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *surf,
                         VkBufferUsageFlagBits usage, VkBuffer *buffer)
{
   struct radv_device *device = cmd_buffer->device;
   struct radv_device_memory mem = {.bo = surf->image->bo};

   radv_CreateBuffer(radv_device_to_handle(device),
                     &(VkBufferCreateInfo){
                        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                        .flags = 0,
                        .size = surf->image->size,
                        .usage = usage,
                        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                     },
                     NULL, buffer);

   radv_BindBufferMemory2(radv_device_to_handle(device), 1,
                          (VkBindBufferMemoryInfo[]){{
                             .sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,
                             .buffer = *buffer,
                             .memory = radv_device_memory_to_handle(&mem),
                             .memoryOffset = surf->image->offset,
                          }});
}

static void
create_bview_for_r32g32b32(struct radv_cmd_buffer *cmd_buffer, struct radv_buffer *buffer,
                           unsigned offset, VkFormat src_format, struct radv_buffer_view *bview)
{
   VkFormat format;

   switch (src_format) {
   case VK_FORMAT_R32G32B32_UINT:
      format = VK_FORMAT_R32_UINT;
      break;
   case VK_FORMAT_R32G32B32_SINT:
      format = VK_FORMAT_R32_SINT;
      break;
   case VK_FORMAT_R32G32B32_SFLOAT:
      format = VK_FORMAT_R32_SFLOAT;
      break;
   default:
      unreachable("invalid R32G32B32 format");
   }

   radv_buffer_view_init(bview, cmd_buffer->device,
                         &(VkBufferViewCreateInfo){
                            .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
                            .flags = 0,
                            .buffer = radv_buffer_to_handle(buffer),
                            .format = format,
                            .offset = offset,
                            .range = VK_WHOLE_SIZE,
                         });
}

static unsigned
get_image_stride_for_r32g32b32(struct radv_cmd_buffer *cmd_buffer,
                               struct radv_meta_blit2d_surf *surf)
{
   unsigned stride;

   if (cmd_buffer->device->physical_device->rad_info.chip_class >= GFX9) {
      stride = surf->image->planes[0].surface.u.gfx9.surf_pitch;
   } else {
      stride = surf->image->planes[0].surface.u.legacy.level[0].nblk_x * 3;
   }

   return stride;
}

static void
itob_bind_descriptors(struct radv_cmd_buffer *cmd_buffer, struct radv_image_view *src,
                      struct radv_buffer_view *dst)
{
   struct radv_device *device = cmd_buffer->device;

   radv_meta_push_descriptor_set(
      cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, device->meta_state.itob.img_p_layout, 0, /* set */
      2, /* descriptorWriteCount */
      (VkWriteDescriptorSet[]){
         {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstBinding = 0,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
          .pImageInfo =
             (VkDescriptorImageInfo[]){
                {
                   .sampler = VK_NULL_HANDLE,
                   .imageView = radv_image_view_to_handle(src),
                   .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                },
             }},
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
            .pTexelBufferView = (VkBufferView[]){radv_buffer_view_to_handle(dst)},
         }});
}

void
radv_meta_image_to_buffer(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *src,
                          struct radv_meta_blit2d_buffer *dst, unsigned num_rects,
                          struct radv_meta_blit2d_rect *rects)
{
   VkPipeline pipeline = cmd_buffer->device->meta_state.itob.pipeline;
   struct radv_device *device = cmd_buffer->device;
   struct radv_image_view src_view;
   struct radv_buffer_view dst_view;

   create_iview(cmd_buffer, src, &src_view, VK_FORMAT_UNDEFINED, src->aspect_mask);
   create_bview(cmd_buffer, dst->buffer, dst->offset, dst->format, &dst_view);
   itob_bind_descriptors(cmd_buffer, &src_view, &dst_view);

   if (device->physical_device->rad_info.chip_class >= GFX9 && src->image->type == VK_IMAGE_TYPE_3D)
      pipeline = cmd_buffer->device->meta_state.itob.pipeline_3d;

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        pipeline);

   for (unsigned r = 0; r < num_rects; ++r) {
      unsigned push_constants[4] = {rects[r].src_x, rects[r].src_y, src->layer, dst->pitch};
      radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                            device->meta_state.itob.img_p_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                            16, push_constants);

      radv_unaligned_dispatch(cmd_buffer, rects[r].width, rects[r].height, 1);
   }
}

static void
btoi_r32g32b32_bind_descriptors(struct radv_cmd_buffer *cmd_buffer, struct radv_buffer_view *src,
                                struct radv_buffer_view *dst)
{
   struct radv_device *device = cmd_buffer->device;

   radv_meta_push_descriptor_set(
      cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, device->meta_state.btoi_r32g32b32.img_p_layout,
      0, /* set */
      2, /* descriptorWriteCount */
      (VkWriteDescriptorSet[]){
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
            .pTexelBufferView = (VkBufferView[]){radv_buffer_view_to_handle(src)},
         },
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
            .pTexelBufferView = (VkBufferView[]){radv_buffer_view_to_handle(dst)},
         }});
}

static void
radv_meta_buffer_to_image_cs_r32g32b32(struct radv_cmd_buffer *cmd_buffer,
                                       struct radv_meta_blit2d_buffer *src,
                                       struct radv_meta_blit2d_surf *dst, unsigned num_rects,
                                       struct radv_meta_blit2d_rect *rects)
{
   VkPipeline pipeline = cmd_buffer->device->meta_state.btoi_r32g32b32.pipeline;
   struct radv_device *device = cmd_buffer->device;
   struct radv_buffer_view src_view, dst_view;
   unsigned dst_offset = 0;
   unsigned stride;
   VkBuffer buffer;

   /* This special btoi path for R32G32B32 formats will write the linear
    * image as a buffer with the same underlying memory. The compute
    * shader will copy all components separately using a R32 format.
    */
   create_buffer_from_image(cmd_buffer, dst, VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT, &buffer);

   create_bview(cmd_buffer, src->buffer, src->offset, src->format, &src_view);
   create_bview_for_r32g32b32(cmd_buffer, radv_buffer_from_handle(buffer), dst_offset, dst->format,
                              &dst_view);
   btoi_r32g32b32_bind_descriptors(cmd_buffer, &src_view, &dst_view);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        pipeline);

   stride = get_image_stride_for_r32g32b32(cmd_buffer, dst);

   for (unsigned r = 0; r < num_rects; ++r) {
      unsigned push_constants[4] = {
         rects[r].dst_x,
         rects[r].dst_y,
         stride,
         src->pitch,
      };

      radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                            device->meta_state.btoi_r32g32b32.img_p_layout,
                            VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, push_constants);

      radv_unaligned_dispatch(cmd_buffer, rects[r].width, rects[r].height, 1);
   }

   radv_DestroyBuffer(radv_device_to_handle(device), buffer, NULL);
}

static void
btoi_bind_descriptors(struct radv_cmd_buffer *cmd_buffer, struct radv_buffer_view *src,
                      struct radv_image_view *dst)
{
   struct radv_device *device = cmd_buffer->device;

   radv_meta_push_descriptor_set(
      cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, device->meta_state.btoi.img_p_layout, 0, /* set */
      2, /* descriptorWriteCount */
      (VkWriteDescriptorSet[]){
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
            .pTexelBufferView = (VkBufferView[]){radv_buffer_view_to_handle(src)},
         },
         {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
          .dstBinding = 1,
          .dstArrayElement = 0,
          .descriptorCount = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .pImageInfo = (VkDescriptorImageInfo[]){
             {
                .sampler = VK_NULL_HANDLE,
                .imageView = radv_image_view_to_handle(dst),
                .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
             },
          }}});
}

void
radv_meta_buffer_to_image_cs(struct radv_cmd_buffer *cmd_buffer,
                             struct radv_meta_blit2d_buffer *src, struct radv_meta_blit2d_surf *dst,
                             unsigned num_rects, struct radv_meta_blit2d_rect *rects)
{
   VkPipeline pipeline = cmd_buffer->device->meta_state.btoi.pipeline;
   struct radv_device *device = cmd_buffer->device;
   struct radv_buffer_view src_view;
   struct radv_image_view dst_view;

   if (dst->image->vk_format == VK_FORMAT_R32G32B32_UINT ||
       dst->image->vk_format == VK_FORMAT_R32G32B32_SINT ||
       dst->image->vk_format == VK_FORMAT_R32G32B32_SFLOAT) {
      radv_meta_buffer_to_image_cs_r32g32b32(cmd_buffer, src, dst, num_rects, rects);
      return;
   }

   create_bview(cmd_buffer, src->buffer, src->offset, src->format, &src_view);
   create_iview(cmd_buffer, dst, &dst_view, VK_FORMAT_UNDEFINED, dst->aspect_mask);
   btoi_bind_descriptors(cmd_buffer, &src_view, &dst_view);

   if (device->physical_device->rad_info.chip_class >= GFX9 && dst->image->type == VK_IMAGE_TYPE_3D)
      pipeline = cmd_buffer->device->meta_state.btoi.pipeline_3d;
   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        pipeline);

   for (unsigned r = 0; r < num_rects; ++r) {
      unsigned push_constants[4] = {
         rects[r].dst_x,
         rects[r].dst_y,
         dst->layer,
         src->pitch,
      };
      radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                            device->meta_state.btoi.img_p_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                            16, push_constants);

      radv_unaligned_dispatch(cmd_buffer, rects[r].width, rects[r].height, 1);
   }
}

static void
itoi_r32g32b32_bind_descriptors(struct radv_cmd_buffer *cmd_buffer, struct radv_buffer_view *src,
                                struct radv_buffer_view *dst)
{
   struct radv_device *device = cmd_buffer->device;

   radv_meta_push_descriptor_set(
      cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, device->meta_state.itoi_r32g32b32.img_p_layout,
      0, /* set */
      2, /* descriptorWriteCount */
      (VkWriteDescriptorSet[]){
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 0,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
            .pTexelBufferView = (VkBufferView[]){radv_buffer_view_to_handle(src)},
         },
         {
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstBinding = 1,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
            .pTexelBufferView = (VkBufferView[]){radv_buffer_view_to_handle(dst)},
         }});
}

static void
radv_meta_image_to_image_cs_r32g32b32(struct radv_cmd_buffer *cmd_buffer,
                                      struct radv_meta_blit2d_surf *src,
                                      struct radv_meta_blit2d_surf *dst, unsigned num_rects,
                                      struct radv_meta_blit2d_rect *rects)
{
   VkPipeline pipeline = cmd_buffer->device->meta_state.itoi_r32g32b32.pipeline;
   struct radv_device *device = cmd_buffer->device;
   struct radv_buffer_view src_view, dst_view;
   unsigned src_offset = 0, dst_offset = 0;
   unsigned src_stride, dst_stride;
   VkBuffer src_buffer, dst_buffer;

   /* 96-bit formats are only compatible to themselves. */
   assert(dst->format == VK_FORMAT_R32G32B32_UINT || dst->format == VK_FORMAT_R32G32B32_SINT ||
          dst->format == VK_FORMAT_R32G32B32_SFLOAT);

   /* This special itoi path for R32G32B32 formats will write the linear
    * image as a buffer with the same underlying memory. The compute
    * shader will copy all components separately using a R32 format.
    */
   create_buffer_from_image(cmd_buffer, src, VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT, &src_buffer);
   create_buffer_from_image(cmd_buffer, dst, VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT, &dst_buffer);

   create_bview_for_r32g32b32(cmd_buffer, radv_buffer_from_handle(src_buffer), src_offset,
                              src->format, &src_view);
   create_bview_for_r32g32b32(cmd_buffer, radv_buffer_from_handle(dst_buffer), dst_offset,
                              dst->format, &dst_view);
   itoi_r32g32b32_bind_descriptors(cmd_buffer, &src_view, &dst_view);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        pipeline);

   src_stride = get_image_stride_for_r32g32b32(cmd_buffer, src);
   dst_stride = get_image_stride_for_r32g32b32(cmd_buffer, dst);

   for (unsigned r = 0; r < num_rects; ++r) {
      unsigned push_constants[6] = {
         rects[r].src_x, rects[r].src_y, src_stride, rects[r].dst_x, rects[r].dst_y, dst_stride,
      };
      radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                            device->meta_state.itoi_r32g32b32.img_p_layout,
                            VK_SHADER_STAGE_COMPUTE_BIT, 0, 24, push_constants);

      radv_unaligned_dispatch(cmd_buffer, rects[r].width, rects[r].height, 1);
   }

   radv_DestroyBuffer(radv_device_to_handle(device), src_buffer, NULL);
   radv_DestroyBuffer(radv_device_to_handle(device), dst_buffer, NULL);
}

static void
itoi_bind_descriptors(struct radv_cmd_buffer *cmd_buffer, struct radv_image_view *src,
                      struct radv_image_view *dst)
{
   struct radv_device *device = cmd_buffer->device;

   radv_meta_push_descriptor_set(
      cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, device->meta_state.itoi.img_p_layout, 0, /* set */
      2, /* descriptorWriteCount */
      (VkWriteDescriptorSet[]){{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstBinding = 0,
                                .dstArrayElement = 0,
                                .descriptorCount = 1,
                                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                .pImageInfo =
                                   (VkDescriptorImageInfo[]){
                                      {
                                         .sampler = VK_NULL_HANDLE,
                                         .imageView = radv_image_view_to_handle(src),
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
                                      .imageView = radv_image_view_to_handle(dst),
                                      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                   },
                                }}});
}

void
radv_meta_image_to_image_cs(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *src,
                            struct radv_meta_blit2d_surf *dst, unsigned num_rects,
                            struct radv_meta_blit2d_rect *rects)
{
   struct radv_device *device = cmd_buffer->device;
   struct radv_image_view src_view, dst_view;
   uint32_t samples = src->image->info.samples;
   uint32_t samples_log2 = ffs(samples) - 1;

   if (src->format == VK_FORMAT_R32G32B32_UINT || src->format == VK_FORMAT_R32G32B32_SINT ||
       src->format == VK_FORMAT_R32G32B32_SFLOAT) {
      radv_meta_image_to_image_cs_r32g32b32(cmd_buffer, src, dst, num_rects, rects);
      return;
   }

   u_foreach_bit(i, dst->aspect_mask) {
      unsigned aspect_mask = 1u << i;
      VkFormat depth_format = 0;
      if (aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT)
         depth_format = vk_format_stencil_only(dst->image->vk_format);
      else if (aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT)
         depth_format = vk_format_depth_only(dst->image->vk_format);

      create_iview(cmd_buffer, src, &src_view, depth_format, aspect_mask);
      create_iview(cmd_buffer, dst, &dst_view, depth_format, aspect_mask);

      itoi_bind_descriptors(cmd_buffer, &src_view, &dst_view);

      VkPipeline pipeline = cmd_buffer->device->meta_state.itoi.pipeline[samples_log2];
      if (device->physical_device->rad_info.chip_class >= GFX9 &&
          (src->image->type == VK_IMAGE_TYPE_3D || dst->image->type == VK_IMAGE_TYPE_3D))
         pipeline = cmd_buffer->device->meta_state.itoi.pipeline_3d;
      radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                           pipeline);

      for (unsigned r = 0; r < num_rects; ++r) {
         unsigned push_constants[6] = {
            rects[r].src_x, rects[r].src_y, src->layer, rects[r].dst_x, rects[r].dst_y, dst->layer,
         };
         radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                               device->meta_state.itoi.img_p_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0,
                               24, push_constants);

         radv_unaligned_dispatch(cmd_buffer, rects[r].width, rects[r].height, 1);
      }
   }
}

static void
cleari_r32g32b32_bind_descriptors(struct radv_cmd_buffer *cmd_buffer, struct radv_buffer_view *view)
{
   struct radv_device *device = cmd_buffer->device;

   radv_meta_push_descriptor_set(
      cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, device->meta_state.cleari_r32g32b32.img_p_layout,
      0, /* set */
      1, /* descriptorWriteCount */
      (VkWriteDescriptorSet[]){{
         .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
         .dstBinding = 0,
         .dstArrayElement = 0,
         .descriptorCount = 1,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
         .pTexelBufferView = (VkBufferView[]){radv_buffer_view_to_handle(view)},
      }});
}

static void
radv_meta_clear_image_cs_r32g32b32(struct radv_cmd_buffer *cmd_buffer,
                                   struct radv_meta_blit2d_surf *dst,
                                   const VkClearColorValue *clear_color)
{
   VkPipeline pipeline = cmd_buffer->device->meta_state.cleari_r32g32b32.pipeline;
   struct radv_device *device = cmd_buffer->device;
   struct radv_buffer_view dst_view;
   unsigned stride;
   VkBuffer buffer;

   /* This special clear path for R32G32B32 formats will write the linear
    * image as a buffer with the same underlying memory. The compute
    * shader will clear all components separately using a R32 format.
    */
   create_buffer_from_image(cmd_buffer, dst, VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT, &buffer);

   create_bview_for_r32g32b32(cmd_buffer, radv_buffer_from_handle(buffer), 0, dst->format,
                              &dst_view);
   cleari_r32g32b32_bind_descriptors(cmd_buffer, &dst_view);

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        pipeline);

   stride = get_image_stride_for_r32g32b32(cmd_buffer, dst);

   unsigned push_constants[4] = {
      clear_color->uint32[0],
      clear_color->uint32[1],
      clear_color->uint32[2],
      stride,
   };

   radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                         device->meta_state.cleari_r32g32b32.img_p_layout,
                         VK_SHADER_STAGE_COMPUTE_BIT, 0, 16, push_constants);

   radv_unaligned_dispatch(cmd_buffer, dst->image->info.width, dst->image->info.height, 1);

   radv_DestroyBuffer(radv_device_to_handle(device), buffer, NULL);
}

static void
cleari_bind_descriptors(struct radv_cmd_buffer *cmd_buffer, struct radv_image_view *dst_iview)
{
   struct radv_device *device = cmd_buffer->device;

   radv_meta_push_descriptor_set(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                                 device->meta_state.cleari.img_p_layout, 0, /* set */
                                 1, /* descriptorWriteCount */
                                 (VkWriteDescriptorSet[]){
                                    {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                     .dstBinding = 0,
                                     .dstArrayElement = 0,
                                     .descriptorCount = 1,
                                     .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                     .pImageInfo =
                                        (VkDescriptorImageInfo[]){
                                           {
                                              .sampler = VK_NULL_HANDLE,
                                              .imageView = radv_image_view_to_handle(dst_iview),
                                              .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                           },
                                        }},
                                 });
}

void
radv_meta_clear_image_cs(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *dst,
                         const VkClearColorValue *clear_color)
{
   struct radv_device *device = cmd_buffer->device;
   struct radv_image_view dst_iview;
   uint32_t samples = dst->image->info.samples;
   uint32_t samples_log2 = ffs(samples) - 1;

   if (dst->format == VK_FORMAT_R32G32B32_UINT || dst->format == VK_FORMAT_R32G32B32_SINT ||
       dst->format == VK_FORMAT_R32G32B32_SFLOAT) {
      radv_meta_clear_image_cs_r32g32b32(cmd_buffer, dst, clear_color);
      return;
   }

   create_iview(cmd_buffer, dst, &dst_iview, VK_FORMAT_UNDEFINED, dst->aspect_mask);
   cleari_bind_descriptors(cmd_buffer, &dst_iview);

   VkPipeline pipeline = cmd_buffer->device->meta_state.cleari.pipeline[samples_log2];
   if (device->physical_device->rad_info.chip_class >= GFX9 && dst->image->type == VK_IMAGE_TYPE_3D)
      pipeline = cmd_buffer->device->meta_state.cleari.pipeline_3d;

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE,
                        pipeline);

   unsigned push_constants[5] = {
      clear_color->uint32[0],
      clear_color->uint32[1],
      clear_color->uint32[2],
      clear_color->uint32[3],
      dst->layer,
   };

   radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
                         device->meta_state.cleari.img_p_layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 20,
                         push_constants);

   radv_unaligned_dispatch(cmd_buffer, dst->image->info.width, dst->image->info.height, 1);
}
