/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */
#include "nir/nir_builder.h"
#include "radv_entrypoints.h"
#include "radv_meta.h"
#include "vk_common_entrypoints.h"
#include "vk_shader_module.h"

/*
 * GFX queue: Compute shader implementation of image->buffer copy
 * Compute queue: implementation also of buffer->image, image->image, and image clear.
 */

static nir_shader *
build_nir_itob_compute_shader(struct radv_device *dev, bool is_3d)
{
   enum glsl_sampler_dim dim = is_3d ? GLSL_SAMPLER_DIM_3D : GLSL_SAMPLER_DIM_2D;
   const struct glsl_type *sampler_type = glsl_sampler_type(dim, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_BUF, false, GLSL_TYPE_FLOAT);
   nir_builder b = radv_meta_init_shader(dev, MESA_SHADER_COMPUTE, is_3d ? "meta_itob_cs_3d" : "meta_itob_cs");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, sampler_type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_def *global_id = get_global_ids(&b, is_3d ? 3 : 2);

   nir_def *offset = nir_load_push_constant(&b, is_3d ? 3 : 2, 32, nir_imm_int(&b, 0), .range = is_3d ? 12 : 8);
   nir_def *stride = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 12), .range = 16);

   nir_def *img_coord = nir_iadd(&b, global_id, offset);
   nir_def *outval =
      nir_txf_deref(&b, nir_build_deref_var(&b, input_img), nir_trim_vector(&b, img_coord, 2 + is_3d), NULL);

   nir_def *pos_x = nir_channel(&b, global_id, 0);
   nir_def *pos_y = nir_channel(&b, global_id, 1);

   nir_def *tmp = nir_imul(&b, pos_y, stride);
   tmp = nir_iadd(&b, tmp, pos_x);

   nir_def *coord = nir_replicate(&b, tmp, 4);

   nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, coord, nir_undef(&b, 1, 32), outval,
                         nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_BUF);

   return b.shader;
}

static VkResult
get_itob_pipeline_layout(struct radv_device *device, VkPipelineLayout *layout_out)
{
   const char *key_data = "radv-itob";

   const VkDescriptorSetLayoutBinding bindings[] = {
      {
         .binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
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

   return vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, &pc_range, key_data,
                                      strlen(key_data), layout_out);
}

static VkResult
get_itob_pipeline(struct radv_device *device, const struct radv_image *image, VkPipeline *pipeline_out,
                  VkPipelineLayout *layout_out)
{
   const bool is_3d = image->vk.image_type == VK_IMAGE_TYPE_3D;
   char key_data[64];
   VkResult result;

   result = get_itob_pipeline_layout(device, layout_out);
   if (result != VK_SUCCESS)
      return result;

   snprintf(key_data, sizeof(key_data), "radv-itob-%d", is_3d);

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, key_data, strlen(key_data));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = build_nir_itob_compute_shader(device, is_3d);

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

   result = vk_meta_create_compute_pipeline(&device->vk, &device->meta_state.device, &pipeline_info, key_data,
                                            strlen(key_data), pipeline_out);

   ralloc_free(cs);
   return result;
}

static nir_shader *
build_nir_btoi_compute_shader(struct radv_device *dev, bool is_3d)
{
   enum glsl_sampler_dim dim = is_3d ? GLSL_SAMPLER_DIM_3D : GLSL_SAMPLER_DIM_2D;
   const struct glsl_type *buf_type = glsl_sampler_type(GLSL_SAMPLER_DIM_BUF, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(dim, false, GLSL_TYPE_FLOAT);
   nir_builder b = radv_meta_init_shader(dev, MESA_SHADER_COMPUTE, is_3d ? "meta_btoi_cs_3d" : "meta_btoi_cs");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, buf_type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_def *global_id = get_global_ids(&b, is_3d ? 3 : 2);

   nir_def *offset = nir_load_push_constant(&b, is_3d ? 3 : 2, 32, nir_imm_int(&b, 0), .range = is_3d ? 12 : 8);
   nir_def *stride = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 12), .range = 16);

   nir_def *pos_x = nir_channel(&b, global_id, 0);
   nir_def *pos_y = nir_channel(&b, global_id, 1);

   nir_def *buf_coord = nir_imul(&b, pos_y, stride);
   buf_coord = nir_iadd(&b, buf_coord, pos_x);

   nir_def *coord = nir_iadd(&b, global_id, offset);
   nir_def *outval = nir_txf_deref(&b, nir_build_deref_var(&b, input_img), buf_coord, NULL);

   nir_def *img_coord = nir_vec4(&b, nir_channel(&b, coord, 0), nir_channel(&b, coord, 1),
                                 is_3d ? nir_channel(&b, coord, 2) : nir_undef(&b, 1, 32), nir_undef(&b, 1, 32));

   nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, img_coord, nir_undef(&b, 1, 32), outval,
                         nir_imm_int(&b, 0), .image_dim = dim);

   return b.shader;
}

static VkResult
get_btoi_pipeline_layout(struct radv_device *device, VkPipelineLayout *layout_out)
{
   const char *key_data = "radv-btoi";

   const VkDescriptorSetLayoutBinding bindings[] = {
      {
         .binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
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

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = 16,
   };

   return vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, &pc_range, key_data,
                                      strlen(key_data), layout_out);
}

static VkResult
get_btoi_pipeline(struct radv_device *device, const struct radv_image *image, VkPipeline *pipeline_out,
                  VkPipelineLayout *layout_out)
{
   const bool is_3d = image->vk.image_type == VK_IMAGE_TYPE_3D;
   char key_data[64];
   VkResult result;

   result = get_btoi_pipeline_layout(device, layout_out);
   if (result != VK_SUCCESS)
      return result;

   snprintf(key_data, sizeof(key_data), "radv-btoi-%d", is_3d);

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, key_data, strlen(key_data));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = build_nir_btoi_compute_shader(device, is_3d);

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

   result = vk_meta_create_compute_pipeline(&device->vk, &device->meta_state.device, &pipeline_info, key_data,
                                            strlen(key_data), pipeline_out);

   ralloc_free(cs);
   return result;
}

/* Buffer to image - special path for R32G32B32 */
static nir_shader *
build_nir_btoi_r32g32b32_compute_shader(struct radv_device *dev)
{
   const struct glsl_type *buf_type = glsl_sampler_type(GLSL_SAMPLER_DIM_BUF, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_BUF, false, GLSL_TYPE_FLOAT);
   nir_builder b = radv_meta_init_shader(dev, MESA_SHADER_COMPUTE, "meta_btoi_r32g32b32_cs");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, buf_type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_def *global_id = get_global_ids(&b, 2);

   nir_def *offset = nir_load_push_constant(&b, 2, 32, nir_imm_int(&b, 0), .range = 8);
   nir_def *pitch = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 8), .range = 12);
   nir_def *stride = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 12), .range = 16);

   nir_def *pos_x = nir_channel(&b, global_id, 0);
   nir_def *pos_y = nir_channel(&b, global_id, 1);

   nir_def *buf_coord = nir_imul(&b, pos_y, stride);
   buf_coord = nir_iadd(&b, buf_coord, pos_x);

   nir_def *img_coord = nir_iadd(&b, global_id, offset);

   nir_def *global_pos = nir_iadd(&b, nir_imul(&b, nir_channel(&b, img_coord, 1), pitch),
                                  nir_imul_imm(&b, nir_channel(&b, img_coord, 0), 3));

   nir_def *outval = nir_txf_deref(&b, nir_build_deref_var(&b, input_img), buf_coord, NULL);

   for (int chan = 0; chan < 3; chan++) {
      nir_def *local_pos = nir_iadd_imm(&b, global_pos, chan);

      nir_def *coord = nir_replicate(&b, local_pos, 4);

      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, coord, nir_undef(&b, 1, 32),
                            nir_channel(&b, outval, chan), nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_BUF);
   }

   return b.shader;
}

static VkResult
get_btoi_r32g32b32_pipeline(struct radv_device *device, VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   const char *key_data = "radv-btoi-r32g32b32";
   VkResult result;

   const VkDescriptorSetLayoutBinding bindings[] = {
      {
         .binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
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

   result = vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, &pc_range, key_data,
                                        strlen(key_data), layout_out);
   if (result != VK_SUCCESS)
      return result;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, key_data, strlen(key_data));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = build_nir_btoi_r32g32b32_compute_shader(device);

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

   result = vk_meta_create_compute_pipeline(&device->vk, &device->meta_state.device, &pipeline_info, key_data,
                                            strlen(key_data), pipeline_out);

   ralloc_free(cs);
   return result;
}

static nir_shader *
build_nir_itoi_compute_shader(struct radv_device *dev, bool src_3d, bool dst_3d, int samples)
{
   bool is_multisampled = samples > 1;
   enum glsl_sampler_dim src_dim = src_3d            ? GLSL_SAMPLER_DIM_3D
                                   : is_multisampled ? GLSL_SAMPLER_DIM_MS
                                                     : GLSL_SAMPLER_DIM_2D;
   enum glsl_sampler_dim dst_dim = dst_3d            ? GLSL_SAMPLER_DIM_3D
                                   : is_multisampled ? GLSL_SAMPLER_DIM_MS
                                                     : GLSL_SAMPLER_DIM_2D;
   const struct glsl_type *buf_type = glsl_sampler_type(src_dim, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(dst_dim, false, GLSL_TYPE_FLOAT);
   nir_builder b = radv_meta_init_shader(dev, MESA_SHADER_COMPUTE, "meta_itoi_cs-%dd-%dd-%d", src_3d ? 3 : 2,
                                         dst_3d ? 3 : 2, samples);
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, buf_type, "s_tex");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_def *global_id = get_global_ids(&b, (src_3d || dst_3d) ? 3 : 2);

   nir_def *src_offset = nir_load_push_constant(&b, src_3d ? 3 : 2, 32, nir_imm_int(&b, 0), .range = src_3d ? 12 : 8);
   nir_def *dst_offset = nir_load_push_constant(&b, dst_3d ? 3 : 2, 32, nir_imm_int(&b, 12), .range = dst_3d ? 24 : 20);

   nir_def *src_coord = nir_iadd(&b, global_id, src_offset);
   nir_deref_instr *input_img_deref = nir_build_deref_var(&b, input_img);

   nir_def *dst_coord = nir_iadd(&b, global_id, dst_offset);

   nir_def *tex_vals[8];
   if (is_multisampled) {
      for (uint32_t i = 0; i < samples; i++) {
         tex_vals[i] = nir_txf_ms_deref(&b, input_img_deref, nir_trim_vector(&b, src_coord, 2), nir_imm_int(&b, i));
      }
   } else {
      tex_vals[0] = nir_txf_deref(&b, input_img_deref, nir_trim_vector(&b, src_coord, 2 + src_3d), nir_imm_int(&b, 0));
   }

   nir_def *img_coord = nir_vec4(&b, nir_channel(&b, dst_coord, 0), nir_channel(&b, dst_coord, 1),
                                 dst_3d ? nir_channel(&b, dst_coord, 2) : nir_undef(&b, 1, 32), nir_undef(&b, 1, 32));

   for (uint32_t i = 0; i < samples; i++) {
      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, img_coord, nir_imm_int(&b, i), tex_vals[i],
                            nir_imm_int(&b, 0), .image_dim = dst_dim);
   }

   return b.shader;
}

static VkResult
get_itoi_pipeline_layout(struct radv_device *device, VkPipelineLayout *layout_out)
{
   const char *key_data = "radv-itoi";

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

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = 24,
   };

   return vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, &pc_range, key_data,
                                      strlen(key_data), layout_out);
}

static VkResult
get_itoi_pipeline(struct radv_device *device, const struct radv_image *src_image, const struct radv_image *dst_image,
                  int samples, VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   const bool src_3d = src_image->vk.image_type == VK_IMAGE_TYPE_3D;
   const bool dst_3d = dst_image->vk.image_type == VK_IMAGE_TYPE_3D;
   const uint32_t samples_log2 = ffs(samples) - 1;
   VkResult result;
   char key_data[64];

   result = get_itoi_pipeline_layout(device, layout_out);
   if (result != VK_SUCCESS)
      return result;

   snprintf(key_data, sizeof(key_data), "radv-itoi-%d-%d-%d", src_3d, dst_3d, samples_log2);

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, key_data, strlen(key_data));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = build_nir_itoi_compute_shader(device, src_3d, dst_3d, samples);

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

   result = vk_meta_create_compute_pipeline(&device->vk, &device->meta_state.device, &pipeline_info, key_data,
                                            strlen(key_data), pipeline_out);

   ralloc_free(cs);
   return result;
}

static nir_shader *
build_nir_itoi_r32g32b32_compute_shader(struct radv_device *dev)
{
   const struct glsl_type *type = glsl_sampler_type(GLSL_SAMPLER_DIM_BUF, false, false, GLSL_TYPE_FLOAT);
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_BUF, false, GLSL_TYPE_FLOAT);
   nir_builder b = radv_meta_init_shader(dev, MESA_SHADER_COMPUTE, "meta_itoi_r32g32b32_cs");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;
   nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform, type, "input_img");
   input_img->data.descriptor_set = 0;
   input_img->data.binding = 0;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "output_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 1;

   nir_def *global_id = get_global_ids(&b, 2);

   nir_def *src_offset = nir_load_push_constant(&b, 3, 32, nir_imm_int(&b, 0), .range = 12);
   nir_def *dst_offset = nir_load_push_constant(&b, 3, 32, nir_imm_int(&b, 12), .range = 24);

   nir_def *src_stride = nir_channel(&b, src_offset, 2);
   nir_def *dst_stride = nir_channel(&b, dst_offset, 2);

   nir_def *src_img_coord = nir_iadd(&b, global_id, src_offset);
   nir_def *dst_img_coord = nir_iadd(&b, global_id, dst_offset);

   nir_def *src_global_pos = nir_iadd(&b, nir_imul(&b, nir_channel(&b, src_img_coord, 1), src_stride),
                                      nir_imul_imm(&b, nir_channel(&b, src_img_coord, 0), 3));

   nir_def *dst_global_pos = nir_iadd(&b, nir_imul(&b, nir_channel(&b, dst_img_coord, 1), dst_stride),
                                      nir_imul_imm(&b, nir_channel(&b, dst_img_coord, 0), 3));

   for (int chan = 0; chan < 3; chan++) {
      /* src */
      nir_def *src_local_pos = nir_iadd_imm(&b, src_global_pos, chan);
      nir_def *outval = nir_txf_deref(&b, nir_build_deref_var(&b, input_img), src_local_pos, NULL);

      /* dst */
      nir_def *dst_local_pos = nir_iadd_imm(&b, dst_global_pos, chan);

      nir_def *dst_coord = nir_replicate(&b, dst_local_pos, 4);

      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, dst_coord, nir_undef(&b, 1, 32),
                            nir_channel(&b, outval, 0), nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_BUF);
   }

   return b.shader;
}

static VkResult
get_itoi_r32g32b32_pipeline(struct radv_device *device, VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   const char *key_data = "radv-itoi-r32g32b32";
   VkResult result;

   const VkDescriptorSetLayoutBinding bindings[] = {
      {
         .binding = 0,
         .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
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
      .size = 24,
   };

   result = vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, &pc_range, key_data,
                                        strlen(key_data), layout_out);
   if (result != VK_SUCCESS)
      return result;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, key_data, strlen(key_data));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = build_nir_itoi_r32g32b32_compute_shader(device);

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

   result = vk_meta_create_compute_pipeline(&device->vk, &device->meta_state.device, &pipeline_info, key_data,
                                            strlen(key_data), pipeline_out);

   ralloc_free(cs);
   return result;
}

static nir_shader *
build_nir_cleari_compute_shader(struct radv_device *dev, bool is_3d, int samples)
{
   bool is_multisampled = samples > 1;
   enum glsl_sampler_dim dim = is_3d             ? GLSL_SAMPLER_DIM_3D
                               : is_multisampled ? GLSL_SAMPLER_DIM_MS
                                                 : GLSL_SAMPLER_DIM_2D;
   const struct glsl_type *img_type = glsl_image_type(dim, false, GLSL_TYPE_FLOAT);
   nir_builder b =
      radv_meta_init_shader(dev, MESA_SHADER_COMPUTE, is_3d ? "meta_cleari_cs_3d-%d" : "meta_cleari_cs-%d", samples);
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 0;

   nir_def *global_id = get_global_ids(&b, 2);

   nir_def *clear_val = nir_load_push_constant(&b, 4, 32, nir_imm_int(&b, 0), .range = 16);
   nir_def *layer = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 16), .range = 20);

   nir_def *comps[4];
   comps[0] = nir_channel(&b, global_id, 0);
   comps[1] = nir_channel(&b, global_id, 1);
   comps[2] = layer;
   comps[3] = nir_undef(&b, 1, 32);
   global_id = nir_vec(&b, comps, 4);

   for (uint32_t i = 0; i < samples; i++) {
      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, global_id, nir_imm_int(&b, i), clear_val,
                            nir_imm_int(&b, 0), .image_dim = dim);
   }

   return b.shader;
}

static VkResult
get_cleari_pipeline_layout(struct radv_device *device, VkPipelineLayout *layout_out)
{
   const char *key_data = "radv-cleari";

   const VkDescriptorSetLayoutBinding binding = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
   };

   const VkDescriptorSetLayoutCreateInfo desc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
      .bindingCount = 1,
      .pBindings = &binding,
   };

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = 20,
   };

   return vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, &pc_range, key_data,
                                      strlen(key_data), layout_out);
}

static VkResult
get_cleari_pipeline(struct radv_device *device, const struct radv_image *image, VkPipeline *pipeline_out,
                    VkPipelineLayout *layout_out)
{
   const bool is_3d = image->vk.image_type == VK_IMAGE_TYPE_3D;
   const uint32_t samples = image->vk.samples;
   const uint32_t samples_log2 = ffs(samples) - 1;
   char key_data[64];
   VkResult result;

   result = get_cleari_pipeline_layout(device, layout_out);
   if (result != VK_SUCCESS)
      return result;

   snprintf(key_data, sizeof(key_data), "radv-cleari-%d-%d", is_3d, samples_log2);

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, key_data, strlen(key_data));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = build_nir_cleari_compute_shader(device, is_3d, samples);

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

   result = vk_meta_create_compute_pipeline(&device->vk, &device->meta_state.device, &pipeline_info, key_data,
                                            strlen(key_data), pipeline_out);

   ralloc_free(cs);
   return result;
}

/* Special path for clearing R32G32B32 images using a compute shader. */
static nir_shader *
build_nir_cleari_r32g32b32_compute_shader(struct radv_device *dev)
{
   const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_BUF, false, GLSL_TYPE_FLOAT);
   nir_builder b = radv_meta_init_shader(dev, MESA_SHADER_COMPUTE, "meta_cleari_r32g32b32_cs");
   b.shader->info.workgroup_size[0] = 8;
   b.shader->info.workgroup_size[1] = 8;

   nir_variable *output_img = nir_variable_create(b.shader, nir_var_image, img_type, "out_img");
   output_img->data.descriptor_set = 0;
   output_img->data.binding = 0;

   nir_def *global_id = get_global_ids(&b, 2);

   nir_def *clear_val = nir_load_push_constant(&b, 3, 32, nir_imm_int(&b, 0), .range = 12);
   nir_def *stride = nir_load_push_constant(&b, 1, 32, nir_imm_int(&b, 12), .range = 16);

   nir_def *global_x = nir_channel(&b, global_id, 0);
   nir_def *global_y = nir_channel(&b, global_id, 1);

   nir_def *global_pos = nir_iadd(&b, nir_imul(&b, global_y, stride), nir_imul_imm(&b, global_x, 3));

   for (unsigned chan = 0; chan < 3; chan++) {
      nir_def *local_pos = nir_iadd_imm(&b, global_pos, chan);

      nir_def *coord = nir_replicate(&b, local_pos, 4);

      nir_image_deref_store(&b, &nir_build_deref_var(&b, output_img)->def, coord, nir_undef(&b, 1, 32),
                            nir_channel(&b, clear_val, chan), nir_imm_int(&b, 0), .image_dim = GLSL_SAMPLER_DIM_BUF);
   }

   return b.shader;
}

static VkResult
get_cleari_r32g32b32_pipeline(struct radv_device *device, VkPipeline *pipeline_out, VkPipelineLayout *layout_out)
{
   const char *key_data = "radv-cleari-r32g32b32";
   VkResult result;

   const VkDescriptorSetLayoutBinding binding = {
      .binding = 0,
      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
      .descriptorCount = 1,
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
   };

   const VkDescriptorSetLayoutCreateInfo desc_info = {
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
      .flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT,
      .bindingCount = 1,
      .pBindings = &binding,
   };

   const VkPushConstantRange pc_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = 16,
   };

   result = vk_meta_get_pipeline_layout(&device->vk, &device->meta_state.device, &desc_info, &pc_range, key_data,
                                        strlen(key_data), layout_out);
   if (result != VK_SUCCESS)
      return result;

   VkPipeline pipeline_from_cache = vk_meta_lookup_pipeline(&device->meta_state.device, key_data, strlen(key_data));
   if (pipeline_from_cache != VK_NULL_HANDLE) {
      *pipeline_out = pipeline_from_cache;
      return VK_SUCCESS;
   }

   nir_shader *cs = build_nir_cleari_r32g32b32_compute_shader(device);

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

   result = vk_meta_create_compute_pipeline(&device->vk, &device->meta_state.device, &pipeline_info, key_data,
                                            strlen(key_data), pipeline_out);

   ralloc_free(cs);
   return result;
}

static void
create_iview(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *surf, struct radv_image_view *iview,
             VkFormat format, VkImageAspectFlagBits aspects)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   if (format == VK_FORMAT_UNDEFINED)
      format = surf->format;

   radv_image_view_init(iview, device,
                        &(VkImageViewCreateInfo){
                           .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
                           .image = radv_image_to_handle(surf->image),
                           .viewType = radv_meta_get_view_type(surf->image),
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
create_bview(struct radv_cmd_buffer *cmd_buffer, struct radv_buffer *buffer, unsigned offset, VkFormat format,
             struct radv_buffer_view *bview)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);

   radv_buffer_view_init(bview, device,
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
                         VkBufferUsageFlagBits2 usage, VkBuffer *buffer)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_device_memory mem;

   radv_device_memory_init(&mem, device, surf->image->bindings[0].bo);

   radv_create_buffer(device,
                      &(VkBufferCreateInfo){
                         .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
                         .pNext =
                            &(VkBufferUsageFlags2CreateInfo){
                               .sType = VK_STRUCTURE_TYPE_BUFFER_USAGE_FLAGS_2_CREATE_INFO,
                               .usage = usage,
                            },
                         .flags = 0,
                         .size = surf->image->size,
                         .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                      },
                      NULL, buffer, true);

   radv_BindBufferMemory2(radv_device_to_handle(device), 1,
                          (VkBindBufferMemoryInfo[]){{
                             .sType = VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO,
                             .buffer = *buffer,
                             .memory = radv_device_memory_to_handle(&mem),
                             .memoryOffset = surf->image->bindings[0].offset,
                          }});

   radv_device_memory_finish(&mem);
}

static void
create_bview_for_r32g32b32(struct radv_cmd_buffer *cmd_buffer, struct radv_buffer *buffer, unsigned offset,
                           VkFormat src_format, struct radv_buffer_view *bview)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
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

   radv_buffer_view_init(bview, device,
                         &(VkBufferViewCreateInfo){
                            .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
                            .flags = 0,
                            .buffer = radv_buffer_to_handle(buffer),
                            .format = format,
                            .offset = offset,
                            .range = VK_WHOLE_SIZE,
                         });
}

/* GFX9+ has an issue where the HW does not calculate mipmap degradations
 * for block-compressed images correctly (see the comment in
 * radv_image_view_init). Some texels are unaddressable and cannot be copied
 * to/from by a compute shader. Here we will perform a buffer copy to copy the
 * texels that the hardware missed.
 *
 * GFX10 will not use this workaround because it can be fixed by adjusting its
 * image view descriptors instead.
 */
static void
fixup_gfx9_cs_copy(struct radv_cmd_buffer *cmd_buffer, const struct radv_meta_blit2d_buffer *buf_bsurf,
                   const struct radv_meta_blit2d_surf *img_bsurf, const struct radv_meta_blit2d_rect *rect,
                   bool to_image)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const unsigned mip_level = img_bsurf->level;
   const struct radv_image *image = img_bsurf->image;
   const struct radeon_surf *surf = &image->planes[0].surface;
   const struct radeon_info *gpu_info = &pdev->info;
   struct ac_surf_info surf_info = radv_get_ac_surf_info(device, image);

   /* GFX10 will use a different workaround unless this is not a 2D image */
   if (gpu_info->gfx_level < GFX9 || (gpu_info->gfx_level >= GFX10 && image->vk.image_type == VK_IMAGE_TYPE_2D) ||
       image->vk.mip_levels == 1 || !vk_format_is_block_compressed(image->vk.format))
      return;

   /* The physical extent of the base mip */
   VkExtent2D hw_base_extent = {surf->u.gfx9.base_mip_width, surf->u.gfx9.base_mip_height};

   /* The hardware-calculated extent of the selected mip
    * (naive divide-by-two integer math)
    */
   VkExtent2D hw_mip_extent = {u_minify(hw_base_extent.width, mip_level), u_minify(hw_base_extent.height, mip_level)};

   /* The actual extent we want to copy */
   VkExtent2D mip_extent = {rect->width, rect->height};

   VkOffset2D mip_offset = {to_image ? rect->dst_x : rect->src_x, to_image ? rect->dst_y : rect->src_y};

   if (hw_mip_extent.width >= mip_offset.x + mip_extent.width &&
       hw_mip_extent.height >= mip_offset.y + mip_extent.height)
      return;

   if (!to_image) {
      /* If we are writing to a buffer, then we need to wait for the compute
       * shader to finish because it may write over the unaddressable texels
       * while we're fixing them. If we're writing to an image, we do not need
       * to wait because the compute shader cannot write to those texels
       */
      cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_INV_L2 | RADV_CMD_FLAG_INV_VCACHE;
   }

   for (uint32_t y = 0; y < mip_extent.height; y++) {
      uint32_t coordY = y + mip_offset.y;
      /* If the default copy algorithm (done previously) has already seen this
       * scanline, then we can bias the starting X coordinate over to skip the
       * region already copied by the default copy.
       */
      uint32_t x = (coordY < hw_mip_extent.height) ? hw_mip_extent.width : 0;
      for (; x < mip_extent.width; x++) {
         uint32_t coordX = x + mip_offset.x;
         uint64_t addr = ac_surface_addr_from_coord(pdev->addrlib, gpu_info, surf, &surf_info, mip_level, coordX,
                                                    coordY, img_bsurf->layer, image->vk.image_type == VK_IMAGE_TYPE_3D);
         struct radeon_winsys_bo *img_bo = image->bindings[0].bo;
         struct radeon_winsys_bo *mem_bo = buf_bsurf->buffer->bo;
         const uint64_t img_offset = image->bindings[0].offset + addr;
         /* buf_bsurf->offset already includes the layer offset */
         const uint64_t mem_offset =
            buf_bsurf->buffer->offset + buf_bsurf->offset + y * buf_bsurf->pitch * surf->bpe + x * surf->bpe;
         if (to_image) {
            radv_copy_buffer(cmd_buffer, mem_bo, img_bo, mem_offset, img_offset, surf->bpe);
         } else {
            radv_copy_buffer(cmd_buffer, img_bo, mem_bo, img_offset, mem_offset, surf->bpe);
         }
      }
   }
}

static unsigned
get_image_stride_for_r32g32b32(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *surf)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   unsigned stride;

   if (pdev->info.gfx_level >= GFX9) {
      stride = surf->image->planes[0].surface.u.gfx9.surf_pitch;
   } else {
      stride = surf->image->planes[0].surface.u.legacy.level[0].nblk_x * 3;
   }

   return stride;
}

void
radv_meta_image_to_buffer(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *src,
                          struct radv_meta_blit2d_buffer *dst, struct radv_meta_blit2d_rect *rect)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_image_view src_view;
   struct radv_buffer_view dst_view;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_itob_pipeline(device, src->image, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   create_iview(cmd_buffer, src, &src_view, VK_FORMAT_UNDEFINED, src->aspect_mask);
   create_bview(cmd_buffer, dst->buffer, dst->offset, dst->format, &dst_view);

   radv_meta_push_descriptor_set(
      cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 2,
      (VkWriteDescriptorSet[]){{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstBinding = 0,
                                .dstArrayElement = 0,
                                .descriptorCount = 1,
                                .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                .pImageInfo =
                                   (VkDescriptorImageInfo[]){
                                      {
                                         .sampler = VK_NULL_HANDLE,
                                         .imageView = radv_image_view_to_handle(&src_view),
                                         .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                      },
                                   }},
                               {
                                  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                  .dstBinding = 1,
                                  .dstArrayElement = 0,
                                  .descriptorCount = 1,
                                  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                                  .pTexelBufferView = (VkBufferView[]){radv_buffer_view_to_handle(&dst_view)},
                               }});

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   unsigned push_constants[4] = {rect->src_x, rect->src_y, src->layer, dst->pitch};
   vk_common_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer), layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16,
                              push_constants);

   radv_unaligned_dispatch(cmd_buffer, rect->width, rect->height, 1);
   fixup_gfx9_cs_copy(cmd_buffer, dst, src, rect, false);

   radv_image_view_finish(&src_view);
   radv_buffer_view_finish(&dst_view);
}

static void
radv_meta_buffer_to_image_cs_r32g32b32(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_buffer *src,
                                       struct radv_meta_blit2d_surf *dst, struct radv_meta_blit2d_rect *rect)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_buffer_view src_view, dst_view;
   unsigned dst_offset = 0;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   unsigned stride;
   VkBuffer buffer;
   VkResult result;

   result = get_btoi_r32g32b32_pipeline(device, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   /* This special btoi path for R32G32B32 formats will write the linear
    * image as a buffer with the same underlying memory. The compute
    * shader will copy all components separately using a R32 format.
    */
   create_buffer_from_image(cmd_buffer, dst, VK_BUFFER_USAGE_2_STORAGE_TEXEL_BUFFER_BIT, &buffer);

   create_bview(cmd_buffer, src->buffer, src->offset, src->format, &src_view);
   create_bview_for_r32g32b32(cmd_buffer, radv_buffer_from_handle(buffer), dst_offset, dst->format, &dst_view);

   radv_meta_push_descriptor_set(
      cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 2,
      (VkWriteDescriptorSet[]){{
                                  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                  .dstBinding = 0,
                                  .dstArrayElement = 0,
                                  .descriptorCount = 1,
                                  .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
                                  .pTexelBufferView = (VkBufferView[]){radv_buffer_view_to_handle(&src_view)},
                               },
                               {
                                  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                  .dstBinding = 1,
                                  .dstArrayElement = 0,
                                  .descriptorCount = 1,
                                  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                                  .pTexelBufferView = (VkBufferView[]){radv_buffer_view_to_handle(&dst_view)},
                               }});

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   stride = get_image_stride_for_r32g32b32(cmd_buffer, dst);

   unsigned push_constants[4] = {
      rect->dst_x,
      rect->dst_y,
      stride,
      src->pitch,
   };

   vk_common_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer), layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16,
                              push_constants);

   radv_unaligned_dispatch(cmd_buffer, rect->width, rect->height, 1);

   radv_buffer_view_finish(&src_view);
   radv_buffer_view_finish(&dst_view);
   radv_DestroyBuffer(radv_device_to_handle(device), buffer, NULL);
}

void
radv_meta_buffer_to_image_cs(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_buffer *src,
                             struct radv_meta_blit2d_surf *dst, struct radv_meta_blit2d_rect *rect)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_buffer_view src_view;
   struct radv_image_view dst_view;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   if (dst->image->vk.format == VK_FORMAT_R32G32B32_UINT || dst->image->vk.format == VK_FORMAT_R32G32B32_SINT ||
       dst->image->vk.format == VK_FORMAT_R32G32B32_SFLOAT) {
      radv_meta_buffer_to_image_cs_r32g32b32(cmd_buffer, src, dst, rect);
      return;
   }

   result = get_btoi_pipeline(device, dst->image, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   create_bview(cmd_buffer, src->buffer, src->offset, src->format, &src_view);
   create_iview(cmd_buffer, dst, &dst_view, VK_FORMAT_UNDEFINED, dst->aspect_mask);

   radv_meta_push_descriptor_set(
      cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 2,
      (VkWriteDescriptorSet[]){{
                                  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                  .dstBinding = 0,
                                  .dstArrayElement = 0,
                                  .descriptorCount = 1,
                                  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                                  .pTexelBufferView = (VkBufferView[]){radv_buffer_view_to_handle(&src_view)},
                               },
                               {.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                .dstBinding = 1,
                                .dstArrayElement = 0,
                                .descriptorCount = 1,
                                .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                                .pImageInfo = (VkDescriptorImageInfo[]){
                                   {
                                      .sampler = VK_NULL_HANDLE,
                                      .imageView = radv_image_view_to_handle(&dst_view),
                                      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                   },
                                }}});

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   unsigned push_constants[4] = {
      rect->dst_x,
      rect->dst_y,
      dst->layer,
      src->pitch,
   };
   vk_common_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer), layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16,
                              push_constants);

   radv_unaligned_dispatch(cmd_buffer, rect->width, rect->height, 1);
   fixup_gfx9_cs_copy(cmd_buffer, src, dst, rect, true);

   radv_image_view_finish(&dst_view);
   radv_buffer_view_finish(&src_view);
}

static void
radv_meta_image_to_image_cs_r32g32b32(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *src,
                                      struct radv_meta_blit2d_surf *dst, struct radv_meta_blit2d_rect *rect)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_buffer_view src_view, dst_view;
   unsigned src_offset = 0, dst_offset = 0;
   unsigned src_stride, dst_stride;
   VkBuffer src_buffer, dst_buffer;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   result = get_itoi_r32g32b32_pipeline(device, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   /* 96-bit formats are only compatible to themselves. */
   assert(dst->format == VK_FORMAT_R32G32B32_UINT || dst->format == VK_FORMAT_R32G32B32_SINT ||
          dst->format == VK_FORMAT_R32G32B32_SFLOAT);

   /* This special itoi path for R32G32B32 formats will write the linear
    * image as a buffer with the same underlying memory. The compute
    * shader will copy all components separately using a R32 format.
    */
   create_buffer_from_image(cmd_buffer, src, VK_BUFFER_USAGE_2_UNIFORM_TEXEL_BUFFER_BIT, &src_buffer);
   create_buffer_from_image(cmd_buffer, dst, VK_BUFFER_USAGE_2_STORAGE_TEXEL_BUFFER_BIT, &dst_buffer);

   create_bview_for_r32g32b32(cmd_buffer, radv_buffer_from_handle(src_buffer), src_offset, src->format, &src_view);
   create_bview_for_r32g32b32(cmd_buffer, radv_buffer_from_handle(dst_buffer), dst_offset, dst->format, &dst_view);

   radv_meta_push_descriptor_set(
      cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 2,
      (VkWriteDescriptorSet[]){{
                                  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                  .dstBinding = 0,
                                  .dstArrayElement = 0,
                                  .descriptorCount = 1,
                                  .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
                                  .pTexelBufferView = (VkBufferView[]){radv_buffer_view_to_handle(&src_view)},
                               },
                               {
                                  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                  .dstBinding = 1,
                                  .dstArrayElement = 0,
                                  .descriptorCount = 1,
                                  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                                  .pTexelBufferView = (VkBufferView[]){radv_buffer_view_to_handle(&dst_view)},
                               }});

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   src_stride = get_image_stride_for_r32g32b32(cmd_buffer, src);
   dst_stride = get_image_stride_for_r32g32b32(cmd_buffer, dst);

   unsigned push_constants[6] = {
      rect->src_x, rect->src_y, src_stride, rect->dst_x, rect->dst_y, dst_stride,
   };
   vk_common_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer), layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 24,
                              push_constants);

   radv_unaligned_dispatch(cmd_buffer, rect->width, rect->height, 1);

   radv_buffer_view_finish(&src_view);
   radv_buffer_view_finish(&dst_view);
   radv_DestroyBuffer(radv_device_to_handle(device), src_buffer, NULL);
   radv_DestroyBuffer(radv_device_to_handle(device), dst_buffer, NULL);
}

void
radv_meta_image_to_image_cs(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *src,
                            struct radv_meta_blit2d_surf *dst, struct radv_meta_blit2d_rect *rect)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_image_view src_view, dst_view;
   uint32_t samples = src->image->vk.samples;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   if (src->format == VK_FORMAT_R32G32B32_UINT || src->format == VK_FORMAT_R32G32B32_SINT ||
       src->format == VK_FORMAT_R32G32B32_SFLOAT) {
      radv_meta_image_to_image_cs_r32g32b32(cmd_buffer, src, dst, rect);
      return;
   }

   result = get_itoi_pipeline(device, src->image, dst->image, samples, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   u_foreach_bit (i, dst->aspect_mask) {
      unsigned dst_aspect_mask = 1u << i;
      unsigned src_aspect_mask = dst_aspect_mask;
      VkFormat depth_format = 0;
      if (dst_aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT)
         depth_format = vk_format_stencil_only(dst->image->vk.format);
      else if (dst_aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT)
         depth_format = vk_format_depth_only(dst->image->vk.format);
      else {
         /*
          * "Multi-planar images can only be copied on a per-plane basis, and the subresources used in each region when
          * copying to or from such images must specify only one plane, though different regions can specify different
          * planes."
          */
         assert((dst->aspect_mask & (dst->aspect_mask - 1)) == 0);
         assert((src->aspect_mask & (src->aspect_mask - 1)) == 0);
         src_aspect_mask = src->aspect_mask;
      }

      /* Adjust the aspect for color to depth/stencil image copies. */
      if (vk_format_is_color(src->image->vk.format) && vk_format_is_depth_or_stencil(dst->image->vk.format)) {
         assert(src->aspect_mask == VK_IMAGE_ASPECT_COLOR_BIT);
         src_aspect_mask = src->aspect_mask;
      }

      create_iview(cmd_buffer, src, &src_view,
                   (src_aspect_mask & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) ? depth_format : 0,
                   src_aspect_mask);
      create_iview(cmd_buffer, dst, &dst_view, depth_format, dst_aspect_mask);

      radv_meta_push_descriptor_set(
         cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 2,
         (VkWriteDescriptorSet[]){{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                   .dstBinding = 0,
                                   .dstArrayElement = 0,
                                   .descriptorCount = 1,
                                   .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
                                   .pImageInfo =
                                      (VkDescriptorImageInfo[]){
                                         {
                                            .sampler = VK_NULL_HANDLE,
                                            .imageView = radv_image_view_to_handle(&src_view),
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
                                         .imageView = radv_image_view_to_handle(&dst_view),
                                         .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                      },
                                   }}});

      radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

      unsigned push_constants[6] = {
         rect->src_x, rect->src_y, src->layer, rect->dst_x, rect->dst_y, dst->layer,
      };
      vk_common_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer), layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 24,
                                 push_constants);

      radv_unaligned_dispatch(cmd_buffer, rect->width, rect->height, 1);

      radv_image_view_finish(&src_view);
      radv_image_view_finish(&dst_view);
   }
}

static void
radv_meta_clear_image_cs_r32g32b32(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *dst,
                                   const VkClearColorValue *clear_color)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_buffer_view dst_view;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   unsigned stride;
   VkBuffer buffer;
   VkResult result;

   result = get_cleari_r32g32b32_pipeline(device, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   /* This special clear path for R32G32B32 formats will write the linear
    * image as a buffer with the same underlying memory. The compute
    * shader will clear all components separately using a R32 format.
    */
   create_buffer_from_image(cmd_buffer, dst, VK_BUFFER_USAGE_2_STORAGE_TEXEL_BUFFER_BIT, &buffer);

   create_bview_for_r32g32b32(cmd_buffer, radv_buffer_from_handle(buffer), 0, dst->format, &dst_view);

   radv_meta_push_descriptor_set(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1,
                                 (VkWriteDescriptorSet[]){{
                                    .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                                    .dstBinding = 0,
                                    .dstArrayElement = 0,
                                    .descriptorCount = 1,
                                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
                                    .pTexelBufferView = (VkBufferView[]){radv_buffer_view_to_handle(&dst_view)},
                                 }});

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   stride = get_image_stride_for_r32g32b32(cmd_buffer, dst);

   unsigned push_constants[4] = {
      clear_color->uint32[0],
      clear_color->uint32[1],
      clear_color->uint32[2],
      stride,
   };

   vk_common_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer), layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 16,
                              push_constants);

   radv_unaligned_dispatch(cmd_buffer, dst->image->vk.extent.width, dst->image->vk.extent.height, 1);

   radv_buffer_view_finish(&dst_view);
   radv_DestroyBuffer(radv_device_to_handle(device), buffer, NULL);
}

void
radv_meta_clear_image_cs(struct radv_cmd_buffer *cmd_buffer, struct radv_meta_blit2d_surf *dst,
                         const VkClearColorValue *clear_color)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   struct radv_image_view dst_iview;
   VkPipelineLayout layout;
   VkPipeline pipeline;
   VkResult result;

   if (dst->format == VK_FORMAT_R32G32B32_UINT || dst->format == VK_FORMAT_R32G32B32_SINT ||
       dst->format == VK_FORMAT_R32G32B32_SFLOAT) {
      radv_meta_clear_image_cs_r32g32b32(cmd_buffer, dst, clear_color);
      return;
   }

   result = get_cleari_pipeline(device, dst->image, &pipeline, &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd_buffer->vk, result);
      return;
   }

   create_iview(cmd_buffer, dst, &dst_iview, VK_FORMAT_UNDEFINED, dst->aspect_mask);

   radv_meta_push_descriptor_set(cmd_buffer, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1,
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
                                              .imageView = radv_image_view_to_handle(&dst_iview),
                                              .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                                           },
                                        }},
                                 });

   radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer), VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

   unsigned push_constants[5] = {
      clear_color->uint32[0], clear_color->uint32[1], clear_color->uint32[2], clear_color->uint32[3], dst->layer,
   };

   vk_common_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer), layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, 20,
                              push_constants);

   radv_unaligned_dispatch(cmd_buffer, dst->image->vk.extent.width, dst->image->vk.extent.height, 1);

   radv_image_view_finish(&dst_iview);
}
