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
#include "radv_meta.h"
#include "nir/nir_builder.h"

/*
 * GFX queue: Compute shader implementation of image->buffer copy
 * Compute queue: implementation also of buffer->image, image->image, and image clear.
 */

static nir_shader *
build_nir_itob_compute_shader(struct radv_device *dev)
{
	nir_builder b;
	const struct glsl_type *sampler_type = glsl_sampler_type(GLSL_SAMPLER_DIM_2D,
								 false,
								 false,
								 GLSL_TYPE_FLOAT);
	const struct glsl_type *img_type = glsl_sampler_type(GLSL_SAMPLER_DIM_BUF,
							     false,
							     false,
							     GLSL_TYPE_FLOAT);
	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info->name = ralloc_strdup(b.shader, "meta_itob_cs");
	b.shader->info->cs.local_size[0] = 16;
	b.shader->info->cs.local_size[1] = 16;
	b.shader->info->cs.local_size[2] = 1;
	nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform,
						      sampler_type, "s_tex");
	input_img->data.descriptor_set = 0;
	input_img->data.binding = 0;

	nir_variable *output_img = nir_variable_create(b.shader, nir_var_uniform,
						       img_type, "out_img");
	output_img->data.descriptor_set = 0;
	output_img->data.binding = 1;

	nir_ssa_def *invoc_id = nir_load_system_value(&b, nir_intrinsic_load_local_invocation_id, 0);
	nir_ssa_def *wg_id = nir_load_system_value(&b, nir_intrinsic_load_work_group_id, 0);
	nir_ssa_def *block_size = nir_imm_ivec4(&b,
						b.shader->info->cs.local_size[0],
						b.shader->info->cs.local_size[1],
						b.shader->info->cs.local_size[2], 0);

	nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);



	nir_intrinsic_instr *offset = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	offset->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	offset->num_components = 2;
	nir_ssa_dest_init(&offset->instr, &offset->dest, 2, 32, "offset");
	nir_builder_instr_insert(&b, &offset->instr);

	nir_intrinsic_instr *stride = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	stride->src[0] = nir_src_for_ssa(nir_imm_int(&b, 8));
	stride->num_components = 1;
	nir_ssa_dest_init(&stride->instr, &stride->dest, 1, 32, "stride");
	nir_builder_instr_insert(&b, &stride->instr);

	nir_ssa_def *img_coord = nir_iadd(&b, global_id, &offset->dest.ssa);

	nir_tex_instr *tex = nir_tex_instr_create(b.shader, 2);
	tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
	tex->op = nir_texop_txf;
	tex->src[0].src_type = nir_tex_src_coord;
	tex->src[0].src = nir_src_for_ssa(img_coord);
	tex->src[1].src_type = nir_tex_src_lod;
	tex->src[1].src = nir_src_for_ssa(nir_imm_int(&b, 0));
	tex->dest_type = nir_type_float;
	tex->is_array = false;
	tex->coord_components = 2;
	tex->texture = nir_deref_var_create(tex, input_img);
	tex->sampler = NULL;

	nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
	nir_builder_instr_insert(&b, &tex->instr);

	nir_ssa_def *pos_x = nir_channel(&b, global_id, 0);
	nir_ssa_def *pos_y = nir_channel(&b, global_id, 1);

	nir_ssa_def *tmp = nir_imul(&b, pos_y, &stride->dest.ssa);
	tmp = nir_iadd(&b, tmp, pos_x);

	nir_ssa_def *coord = nir_vec4(&b, tmp, tmp, tmp, tmp);

	nir_ssa_def *outval = &tex->dest.ssa;
	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_image_store);
	store->src[0] = nir_src_for_ssa(coord);
	store->src[1] = nir_src_for_ssa(nir_ssa_undef(&b, 1, 32));
	store->src[2] = nir_src_for_ssa(outval);
	store->variables[0] = nir_deref_var_create(store, output_img);

	nir_builder_instr_insert(&b, &store->instr);
	return b.shader;
}

/* Image to buffer - don't write use image accessors */
static VkResult
radv_device_init_meta_itob_state(struct radv_device *device)
{
	VkResult result;
	struct radv_shader_module cs = { .nir = NULL };

	zero(device->meta_state.itob);

	cs.nir = build_nir_itob_compute_shader(device);

	/*
	 * two descriptors one for the image being sampled
	 * one for the buffer being written.
	 */
	VkDescriptorSetLayoutCreateInfo ds_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 2,
		.pBindings = (VkDescriptorSetLayoutBinding[]) {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = NULL
			},
			{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = NULL
			},
		}
	};

	result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device),
						&ds_create_info,
						&device->meta_state.alloc,
						&device->meta_state.itob.img_ds_layout);
	if (result != VK_SUCCESS)
		goto fail;


	VkPipelineLayoutCreateInfo pl_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &device->meta_state.itob.img_ds_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 12},
	};

	result = radv_CreatePipelineLayout(radv_device_to_handle(device),
					  &pl_create_info,
					  &device->meta_state.alloc,
					  &device->meta_state.itob.img_p_layout);
	if (result != VK_SUCCESS)
		goto fail;

	/* compute shader */

	VkPipelineShaderStageCreateInfo pipeline_shader_stage = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = radv_shader_module_to_handle(&cs),
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
					     radv_pipeline_cache_to_handle(&device->meta_state.cache),
					     1, &vk_pipeline_info, NULL,
					     &device->meta_state.itob.pipeline);
	if (result != VK_SUCCESS)
		goto fail;

	ralloc_free(cs.nir);
	return VK_SUCCESS;
fail:
	ralloc_free(cs.nir);
	return result;
}

static void
radv_device_finish_meta_itob_state(struct radv_device *device)
{
	if (device->meta_state.itob.img_p_layout) {
		radv_DestroyPipelineLayout(radv_device_to_handle(device),
					   device->meta_state.itob.img_p_layout,
					   &device->meta_state.alloc);
	}
	if (device->meta_state.itob.img_ds_layout) {
		radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
						device->meta_state.itob.img_ds_layout,
						&device->meta_state.alloc);
	}
	if (device->meta_state.itob.pipeline) {
		radv_DestroyPipeline(radv_device_to_handle(device),
				     device->meta_state.itob.pipeline,
				     &device->meta_state.alloc);
	}
}

static nir_shader *
build_nir_btoi_compute_shader(struct radv_device *dev)
{
	nir_builder b;
	const struct glsl_type *buf_type = glsl_sampler_type(GLSL_SAMPLER_DIM_BUF,
							     false,
							     false,
							     GLSL_TYPE_FLOAT);
	const struct glsl_type *img_type = glsl_sampler_type(GLSL_SAMPLER_DIM_2D,
							     false,
							     false,
							     GLSL_TYPE_FLOAT);
	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info->name = ralloc_strdup(b.shader, "meta_btoi_cs");
	b.shader->info->cs.local_size[0] = 16;
	b.shader->info->cs.local_size[1] = 16;
	b.shader->info->cs.local_size[2] = 1;
	nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform,
						      buf_type, "s_tex");
	input_img->data.descriptor_set = 0;
	input_img->data.binding = 0;

	nir_variable *output_img = nir_variable_create(b.shader, nir_var_uniform,
						       img_type, "out_img");
	output_img->data.descriptor_set = 0;
	output_img->data.binding = 1;

	nir_ssa_def *invoc_id = nir_load_system_value(&b, nir_intrinsic_load_local_invocation_id, 0);
	nir_ssa_def *wg_id = nir_load_system_value(&b, nir_intrinsic_load_work_group_id, 0);
	nir_ssa_def *block_size = nir_imm_ivec4(&b,
						b.shader->info->cs.local_size[0],
						b.shader->info->cs.local_size[1],
						b.shader->info->cs.local_size[2], 0);

	nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

	nir_intrinsic_instr *offset = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	offset->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	offset->num_components = 2;
	nir_ssa_dest_init(&offset->instr, &offset->dest, 2, 32, "offset");
	nir_builder_instr_insert(&b, &offset->instr);

	nir_intrinsic_instr *stride = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	stride->src[0] = nir_src_for_ssa(nir_imm_int(&b, 8));
	stride->num_components = 1;
	nir_ssa_dest_init(&stride->instr, &stride->dest, 1, 32, "stride");
	nir_builder_instr_insert(&b, &stride->instr);

	nir_ssa_def *pos_x = nir_channel(&b, global_id, 0);
	nir_ssa_def *pos_y = nir_channel(&b, global_id, 1);

	nir_ssa_def *tmp = nir_imul(&b, pos_y, &stride->dest.ssa);
	tmp = nir_iadd(&b, tmp, pos_x);

	nir_ssa_def *buf_coord = nir_vec4(&b, tmp, tmp, tmp, tmp);

	nir_ssa_def *img_coord = nir_iadd(&b, global_id, &offset->dest.ssa);

	nir_tex_instr *tex = nir_tex_instr_create(b.shader, 2);
	tex->sampler_dim = GLSL_SAMPLER_DIM_BUF;
	tex->op = nir_texop_txf;
	tex->src[0].src_type = nir_tex_src_coord;
	tex->src[0].src = nir_src_for_ssa(buf_coord);
	tex->src[1].src_type = nir_tex_src_lod;
	tex->src[1].src = nir_src_for_ssa(nir_imm_int(&b, 0));
	tex->dest_type = nir_type_float;
	tex->is_array = false;
	tex->coord_components = 1;
	tex->texture = nir_deref_var_create(tex, input_img);
	tex->sampler = NULL;

	nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
	nir_builder_instr_insert(&b, &tex->instr);

	nir_ssa_def *outval = &tex->dest.ssa;
	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_image_store);
	store->src[0] = nir_src_for_ssa(img_coord);
	store->src[1] = nir_src_for_ssa(nir_ssa_undef(&b, 1, 32));
	store->src[2] = nir_src_for_ssa(outval);
	store->variables[0] = nir_deref_var_create(store, output_img);

	nir_builder_instr_insert(&b, &store->instr);
	return b.shader;
}

/* Buffer to image - don't write use image accessors */
static VkResult
radv_device_init_meta_btoi_state(struct radv_device *device)
{
	VkResult result;
	struct radv_shader_module cs = { .nir = NULL };

	zero(device->meta_state.btoi);

	cs.nir = build_nir_btoi_compute_shader(device);

	/*
	 * two descriptors one for the image being sampled
	 * one for the buffer being written.
	 */
	VkDescriptorSetLayoutCreateInfo ds_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 2,
		.pBindings = (VkDescriptorSetLayoutBinding[]) {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = NULL
			},
			{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = NULL
			},
		}
	};

	result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device),
						&ds_create_info,
						&device->meta_state.alloc,
						&device->meta_state.btoi.img_ds_layout);
	if (result != VK_SUCCESS)
		goto fail;


	VkPipelineLayoutCreateInfo pl_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &device->meta_state.btoi.img_ds_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 12},
	};

	result = radv_CreatePipelineLayout(radv_device_to_handle(device),
					  &pl_create_info,
					  &device->meta_state.alloc,
					  &device->meta_state.btoi.img_p_layout);
	if (result != VK_SUCCESS)
		goto fail;

	/* compute shader */

	VkPipelineShaderStageCreateInfo pipeline_shader_stage = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = radv_shader_module_to_handle(&cs),
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
					     radv_pipeline_cache_to_handle(&device->meta_state.cache),
					     1, &vk_pipeline_info, NULL,
					     &device->meta_state.btoi.pipeline);
	if (result != VK_SUCCESS)
		goto fail;

	ralloc_free(cs.nir);
	return VK_SUCCESS;
fail:
	ralloc_free(cs.nir);
	return result;
}

static void
radv_device_finish_meta_btoi_state(struct radv_device *device)
{
	if (device->meta_state.btoi.img_p_layout) {
		radv_DestroyPipelineLayout(radv_device_to_handle(device),
					   device->meta_state.btoi.img_p_layout,
					   &device->meta_state.alloc);
	}
	if (device->meta_state.btoi.img_ds_layout) {
		radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
						device->meta_state.btoi.img_ds_layout,
						&device->meta_state.alloc);
	}
	if (device->meta_state.btoi.pipeline) {
		radv_DestroyPipeline(radv_device_to_handle(device),
				     device->meta_state.btoi.pipeline,
				     &device->meta_state.alloc);
	}
}

static nir_shader *
build_nir_itoi_compute_shader(struct radv_device *dev)
{
	nir_builder b;
	const struct glsl_type *buf_type = glsl_sampler_type(GLSL_SAMPLER_DIM_2D,
							     false,
							     false,
							     GLSL_TYPE_FLOAT);
	const struct glsl_type *img_type = glsl_sampler_type(GLSL_SAMPLER_DIM_2D,
							     false,
							     false,
							     GLSL_TYPE_FLOAT);
	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info->name = ralloc_strdup(b.shader, "meta_itoi_cs");
	b.shader->info->cs.local_size[0] = 16;
	b.shader->info->cs.local_size[1] = 16;
	b.shader->info->cs.local_size[2] = 1;
	nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform,
						      buf_type, "s_tex");
	input_img->data.descriptor_set = 0;
	input_img->data.binding = 0;

	nir_variable *output_img = nir_variable_create(b.shader, nir_var_uniform,
						       img_type, "out_img");
	output_img->data.descriptor_set = 0;
	output_img->data.binding = 1;

	nir_ssa_def *invoc_id = nir_load_system_value(&b, nir_intrinsic_load_local_invocation_id, 0);
	nir_ssa_def *wg_id = nir_load_system_value(&b, nir_intrinsic_load_work_group_id, 0);
	nir_ssa_def *block_size = nir_imm_ivec4(&b,
						b.shader->info->cs.local_size[0],
						b.shader->info->cs.local_size[1],
						b.shader->info->cs.local_size[2], 0);

	nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

	nir_intrinsic_instr *src_offset = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	src_offset->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	src_offset->num_components = 2;
	nir_ssa_dest_init(&src_offset->instr, &src_offset->dest, 2, 32, "src_offset");
	nir_builder_instr_insert(&b, &src_offset->instr);

	nir_intrinsic_instr *dst_offset = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	dst_offset->src[0] = nir_src_for_ssa(nir_imm_int(&b, 8));
	dst_offset->num_components = 2;
	nir_ssa_dest_init(&dst_offset->instr, &dst_offset->dest, 2, 32, "dst_offset");
	nir_builder_instr_insert(&b, &dst_offset->instr);

	nir_ssa_def *src_coord = nir_iadd(&b, global_id, &src_offset->dest.ssa);

	nir_ssa_def *dst_coord = nir_iadd(&b, global_id, &dst_offset->dest.ssa);

	nir_tex_instr *tex = nir_tex_instr_create(b.shader, 2);
	tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
	tex->op = nir_texop_txf;
	tex->src[0].src_type = nir_tex_src_coord;
	tex->src[0].src = nir_src_for_ssa(src_coord);
	tex->src[1].src_type = nir_tex_src_lod;
	tex->src[1].src = nir_src_for_ssa(nir_imm_int(&b, 0));
	tex->dest_type = nir_type_float;
	tex->is_array = false;
	tex->coord_components = 2;
	tex->texture = nir_deref_var_create(tex, input_img);
	tex->sampler = NULL;

	nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
	nir_builder_instr_insert(&b, &tex->instr);

	nir_ssa_def *outval = &tex->dest.ssa;
	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_image_store);
	store->src[0] = nir_src_for_ssa(dst_coord);
	store->src[1] = nir_src_for_ssa(nir_ssa_undef(&b, 1, 32));
	store->src[2] = nir_src_for_ssa(outval);
	store->variables[0] = nir_deref_var_create(store, output_img);

	nir_builder_instr_insert(&b, &store->instr);
	return b.shader;
}

/* image to image - don't write use image accessors */
static VkResult
radv_device_init_meta_itoi_state(struct radv_device *device)
{
	VkResult result;
	struct radv_shader_module cs = { .nir = NULL };

	zero(device->meta_state.itoi);

	cs.nir = build_nir_itoi_compute_shader(device);

	/*
	 * two descriptors one for the image being sampled
	 * one for the buffer being written.
	 */
	VkDescriptorSetLayoutCreateInfo ds_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 2,
		.pBindings = (VkDescriptorSetLayoutBinding[]) {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = NULL
			},
			{
				.binding = 1,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = NULL
			},
		}
	};

	result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device),
						&ds_create_info,
						&device->meta_state.alloc,
						&device->meta_state.itoi.img_ds_layout);
	if (result != VK_SUCCESS)
		goto fail;


	VkPipelineLayoutCreateInfo pl_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &device->meta_state.itoi.img_ds_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 16},
	};

	result = radv_CreatePipelineLayout(radv_device_to_handle(device),
					  &pl_create_info,
					  &device->meta_state.alloc,
					  &device->meta_state.itoi.img_p_layout);
	if (result != VK_SUCCESS)
		goto fail;

	/* compute shader */

	VkPipelineShaderStageCreateInfo pipeline_shader_stage = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = radv_shader_module_to_handle(&cs),
		.pName = "main",
		.pSpecializationInfo = NULL,
	};

	VkComputePipelineCreateInfo vk_pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = pipeline_shader_stage,
		.flags = 0,
		.layout = device->meta_state.itoi.img_p_layout,
	};

	result = radv_CreateComputePipelines(radv_device_to_handle(device),
					     radv_pipeline_cache_to_handle(&device->meta_state.cache),
					     1, &vk_pipeline_info, NULL,
					     &device->meta_state.itoi.pipeline);
	if (result != VK_SUCCESS)
		goto fail;

	ralloc_free(cs.nir);
	return VK_SUCCESS;
fail:
	ralloc_free(cs.nir);
	return result;
}

static void
radv_device_finish_meta_itoi_state(struct radv_device *device)
{
	if (device->meta_state.itoi.img_p_layout) {
		radv_DestroyPipelineLayout(radv_device_to_handle(device),
					   device->meta_state.itoi.img_p_layout,
					   &device->meta_state.alloc);
	}
	if (device->meta_state.itoi.img_ds_layout) {
		radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
						device->meta_state.itoi.img_ds_layout,
						&device->meta_state.alloc);
	}
	if (device->meta_state.itoi.pipeline) {
		radv_DestroyPipeline(radv_device_to_handle(device),
				     device->meta_state.itoi.pipeline,
				     &device->meta_state.alloc);
	}
}

static nir_shader *
build_nir_cleari_compute_shader(struct radv_device *dev)
{
	nir_builder b;
	const struct glsl_type *img_type = glsl_sampler_type(GLSL_SAMPLER_DIM_2D,
							     false,
							     false,
							     GLSL_TYPE_FLOAT);
	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info->name = ralloc_strdup(b.shader, "meta_cleari_cs");
	b.shader->info->cs.local_size[0] = 16;
	b.shader->info->cs.local_size[1] = 16;
	b.shader->info->cs.local_size[2] = 1;

	nir_variable *output_img = nir_variable_create(b.shader, nir_var_uniform,
						       img_type, "out_img");
	output_img->data.descriptor_set = 0;
	output_img->data.binding = 0;

	nir_ssa_def *invoc_id = nir_load_system_value(&b, nir_intrinsic_load_local_invocation_id, 0);
	nir_ssa_def *wg_id = nir_load_system_value(&b, nir_intrinsic_load_work_group_id, 0);
	nir_ssa_def *block_size = nir_imm_ivec4(&b,
						b.shader->info->cs.local_size[0],
						b.shader->info->cs.local_size[1],
						b.shader->info->cs.local_size[2], 0);

	nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

	nir_intrinsic_instr *clear_val = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	clear_val->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	clear_val->num_components = 4;
	nir_ssa_dest_init(&clear_val->instr, &clear_val->dest, 4, 32, "clear_value");
	nir_builder_instr_insert(&b, &clear_val->instr);

	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_image_store);
	store->src[0] = nir_src_for_ssa(global_id);
	store->src[1] = nir_src_for_ssa(nir_ssa_undef(&b, 1, 32));
	store->src[2] = nir_src_for_ssa(&clear_val->dest.ssa);
	store->variables[0] = nir_deref_var_create(store, output_img);

	nir_builder_instr_insert(&b, &store->instr);
	return b.shader;
}

static VkResult
radv_device_init_meta_cleari_state(struct radv_device *device)
{
	VkResult result;
	struct radv_shader_module cs = { .nir = NULL };

	zero(device->meta_state.cleari);

	cs.nir = build_nir_cleari_compute_shader(device);

	/*
	 * two descriptors one for the image being sampled
	 * one for the buffer being written.
	 */
	VkDescriptorSetLayoutCreateInfo ds_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 1,
		.pBindings = (VkDescriptorSetLayoutBinding[]) {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
				.pImmutableSamplers = NULL
			},
		}
	};

	result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device),
						&ds_create_info,
						&device->meta_state.alloc,
						&device->meta_state.cleari.img_ds_layout);
	if (result != VK_SUCCESS)
		goto fail;


	VkPipelineLayoutCreateInfo pl_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &device->meta_state.cleari.img_ds_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 16},
	};

	result = radv_CreatePipelineLayout(radv_device_to_handle(device),
					  &pl_create_info,
					  &device->meta_state.alloc,
					  &device->meta_state.cleari.img_p_layout);
	if (result != VK_SUCCESS)
		goto fail;

	/* compute shader */

	VkPipelineShaderStageCreateInfo pipeline_shader_stage = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_COMPUTE_BIT,
		.module = radv_shader_module_to_handle(&cs),
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
					     radv_pipeline_cache_to_handle(&device->meta_state.cache),
					     1, &vk_pipeline_info, NULL,
					     &device->meta_state.cleari.pipeline);
	if (result != VK_SUCCESS)
		goto fail;

	ralloc_free(cs.nir);
	return VK_SUCCESS;
fail:
	ralloc_free(cs.nir);
	return result;
}

static void
radv_device_finish_meta_cleari_state(struct radv_device *device)
{
	if (device->meta_state.cleari.img_p_layout) {
		radv_DestroyPipelineLayout(radv_device_to_handle(device),
					   device->meta_state.cleari.img_p_layout,
					   &device->meta_state.alloc);
	}
	if (device->meta_state.cleari.img_ds_layout) {
		radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
						device->meta_state.cleari.img_ds_layout,
						&device->meta_state.alloc);
	}
	if (device->meta_state.cleari.pipeline) {
		radv_DestroyPipeline(radv_device_to_handle(device),
				     device->meta_state.cleari.pipeline,
				     &device->meta_state.alloc);
	}
}

void
radv_device_finish_meta_bufimage_state(struct radv_device *device)
{
	radv_device_finish_meta_itob_state(device);
	radv_device_finish_meta_btoi_state(device);
	radv_device_finish_meta_itoi_state(device);
	radv_device_finish_meta_cleari_state(device);
}

VkResult
radv_device_init_meta_bufimage_state(struct radv_device *device)
{
	VkResult result;

	result = radv_device_init_meta_itob_state(device);
	if (result != VK_SUCCESS)
		return result;

	result = radv_device_init_meta_btoi_state(device);
	if (result != VK_SUCCESS)
		goto fail_itob;

	result = radv_device_init_meta_itoi_state(device);
	if (result != VK_SUCCESS)
		goto fail_btoi;

	result = radv_device_init_meta_cleari_state(device);
	if (result != VK_SUCCESS)
		goto fail_itoi;

	return VK_SUCCESS;
fail_itoi:
	radv_device_finish_meta_itoi_state(device);
fail_btoi:
	radv_device_finish_meta_btoi_state(device);
fail_itob:
	radv_device_finish_meta_itob_state(device);
	return result;
}

void
radv_meta_begin_itoi(struct radv_cmd_buffer *cmd_buffer,
		     struct radv_meta_saved_compute_state *save)
{
	radv_meta_save_compute(save, cmd_buffer, 16);
}

void
radv_meta_end_itoi(struct radv_cmd_buffer *cmd_buffer,
		   struct radv_meta_saved_compute_state *save)
{
	radv_meta_restore_compute(save, cmd_buffer, 16);
}

void
radv_meta_begin_bufimage(struct radv_cmd_buffer *cmd_buffer,
			 struct radv_meta_saved_compute_state *save)
{
	radv_meta_save_compute(save, cmd_buffer, 12);
}

void
radv_meta_end_bufimage(struct radv_cmd_buffer *cmd_buffer,
		       struct radv_meta_saved_compute_state *save)
{
	radv_meta_restore_compute(save, cmd_buffer, 12);
}

void
radv_meta_begin_cleari(struct radv_cmd_buffer *cmd_buffer,
		       struct radv_meta_saved_compute_state *save)
{
	radv_meta_save_compute(save, cmd_buffer, 16);
}

void
radv_meta_end_cleari(struct radv_cmd_buffer *cmd_buffer,
		     struct radv_meta_saved_compute_state *save)
{
	radv_meta_restore_compute(save, cmd_buffer, 16);
}

static void
create_iview(struct radv_cmd_buffer *cmd_buffer,
             struct radv_meta_blit2d_surf *surf,
             VkImageUsageFlags usage,
             struct radv_image_view *iview)
{

	radv_image_view_init(iview, cmd_buffer->device,
			     &(VkImageViewCreateInfo) {
				     .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					     .image = radv_image_to_handle(surf->image),
					     .viewType = VK_IMAGE_VIEW_TYPE_2D,
					     .format = surf->format,
					     .subresourceRange = {
					     .aspectMask = surf->aspect_mask,
					     .baseMipLevel = surf->level,
					     .levelCount = 1,
					     .baseArrayLayer = surf->layer,
					     .layerCount = 1
				     },
					     }, cmd_buffer, usage);
}

static void
create_bview(struct radv_cmd_buffer *cmd_buffer,
	     struct radv_buffer *buffer,
	     unsigned offset,
	     VkFormat format,
	     struct radv_buffer_view *bview)
{
	radv_buffer_view_init(bview, cmd_buffer->device,
			      &(VkBufferViewCreateInfo) {
				      .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
				      .flags = 0,
				      .buffer = radv_buffer_to_handle(buffer),
				      .format = format,
				      .offset = offset,
				      .range = VK_WHOLE_SIZE,
			      }, cmd_buffer);

}

struct itob_temps {
	struct radv_image_view src_iview;
	struct radv_buffer_view dst_bview;
	VkDescriptorSet set;
};

static void
itob_bind_descriptors(struct radv_cmd_buffer *cmd_buffer,
		      struct itob_temps *tmp)
{
	struct radv_device *device = cmd_buffer->device;
	VkDevice vk_device = radv_device_to_handle(cmd_buffer->device);

	radv_temp_descriptor_set_create(device, cmd_buffer,
					device->meta_state.itob.img_ds_layout,
					&tmp->set);

	radv_UpdateDescriptorSets(vk_device,
				  2, /* writeCount */
				  (VkWriteDescriptorSet[]) {
					  {
						  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						  .dstSet = tmp->set,
						  .dstBinding = 0,
						  .dstArrayElement = 0,
						  .descriptorCount = 1,
						  .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
						  .pImageInfo = (VkDescriptorImageInfo[]) {
							  {
								  .sampler = VK_NULL_HANDLE,
								  .imageView = radv_image_view_to_handle(&tmp->src_iview),
								  .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
							  },
						  }
					  },
					  {
						  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						  .dstSet = tmp->set,
						  .dstBinding = 1,
						  .dstArrayElement = 0,
						  .descriptorCount = 1,
						  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
						  .pTexelBufferView = (VkBufferView[])  { radv_buffer_view_to_handle(&tmp->dst_bview) },
					  }
				  }, 0, NULL);

	radv_CmdBindDescriptorSets(radv_cmd_buffer_to_handle(cmd_buffer),
				   VK_PIPELINE_BIND_POINT_COMPUTE,
				   device->meta_state.itob.img_p_layout, 0, 1,
				   &tmp->set, 0, NULL);
}

static void
itob_bind_pipeline(struct radv_cmd_buffer *cmd_buffer)
{
	VkPipeline pipeline =
		cmd_buffer->device->meta_state.itob.pipeline;

	if (cmd_buffer->state.compute_pipeline != radv_pipeline_from_handle(pipeline)) {
		radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
				     VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
	}
}

void
radv_meta_image_to_buffer(struct radv_cmd_buffer *cmd_buffer,
			  struct radv_meta_blit2d_surf *src,
			  struct radv_meta_blit2d_buffer *dst,
			  unsigned num_rects,
			  struct radv_meta_blit2d_rect *rects)
{
	struct radv_device *device = cmd_buffer->device;
	struct itob_temps temps;

	create_iview(cmd_buffer, src, VK_IMAGE_USAGE_SAMPLED_BIT, &temps.src_iview);
	create_bview(cmd_buffer, dst->buffer, dst->offset, dst->format, &temps.dst_bview);
	itob_bind_descriptors(cmd_buffer, &temps);

	itob_bind_pipeline(cmd_buffer);

	for (unsigned r = 0; r < num_rects; ++r) {
		unsigned push_constants[3] = {
			rects[r].src_x,
			rects[r].src_y,
			dst->pitch
		};
		radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
				      device->meta_state.itob.img_p_layout,
				      VK_SHADER_STAGE_COMPUTE_BIT, 0, 12,
				      push_constants);

		radv_unaligned_dispatch(cmd_buffer, rects[r].width, rects[r].height, 1);
	}
	radv_temp_descriptor_set_destroy(cmd_buffer->device, temps.set);
}

struct btoi_temps {
	struct radv_buffer_view src_bview;
	struct radv_image_view dst_iview;
	VkDescriptorSet set;
};

static void
btoi_bind_descriptors(struct radv_cmd_buffer *cmd_buffer,
		      struct btoi_temps *tmp)
{
	struct radv_device *device = cmd_buffer->device;
	VkDevice vk_device = radv_device_to_handle(cmd_buffer->device);

	radv_temp_descriptor_set_create(device, cmd_buffer,
					device->meta_state.btoi.img_ds_layout,
					&tmp->set);

	radv_UpdateDescriptorSets(vk_device,
				  2, /* writeCount */
				  (VkWriteDescriptorSet[]) {
					  {
						  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						  .dstSet = tmp->set,
						  .dstBinding = 0,
						  .dstArrayElement = 0,
						  .descriptorCount = 1,
						  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
						  .pTexelBufferView = (VkBufferView[])  { radv_buffer_view_to_handle(&tmp->src_bview) },
					  },
					  {
						  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						  .dstSet = tmp->set,
						  .dstBinding = 1,
						  .dstArrayElement = 0,
						  .descriptorCount = 1,
						  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
						  .pImageInfo = (VkDescriptorImageInfo[]) {
							  {
								  .sampler = NULL,
								  .imageView = radv_image_view_to_handle(&tmp->dst_iview),
								  .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
							  },
						  }
					  }
				  }, 0, NULL);

	radv_CmdBindDescriptorSets(radv_cmd_buffer_to_handle(cmd_buffer),
				   VK_PIPELINE_BIND_POINT_COMPUTE,
				   device->meta_state.btoi.img_p_layout, 0, 1,
				   &tmp->set, 0, NULL);
}

static void
btoi_bind_pipeline(struct radv_cmd_buffer *cmd_buffer)
{
	VkPipeline pipeline =
		cmd_buffer->device->meta_state.btoi.pipeline;

	if (cmd_buffer->state.compute_pipeline != radv_pipeline_from_handle(pipeline)) {
		radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
				     VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
	}
}

void
radv_meta_buffer_to_image_cs(struct radv_cmd_buffer *cmd_buffer,
			     struct radv_meta_blit2d_buffer *src,
			     struct radv_meta_blit2d_surf *dst,
			     unsigned num_rects,
			     struct radv_meta_blit2d_rect *rects)
{
	struct radv_device *device = cmd_buffer->device;
	struct btoi_temps temps;

	create_bview(cmd_buffer, src->buffer, src->offset, src->format, &temps.src_bview);
	create_iview(cmd_buffer, dst, VK_IMAGE_USAGE_STORAGE_BIT, &temps.dst_iview);
	btoi_bind_descriptors(cmd_buffer, &temps);

	btoi_bind_pipeline(cmd_buffer);

	for (unsigned r = 0; r < num_rects; ++r) {
		unsigned push_constants[3] = {
			rects[r].dst_x,
			rects[r].dst_y,
			src->pitch
		};
		radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
				      device->meta_state.btoi.img_p_layout,
				      VK_SHADER_STAGE_COMPUTE_BIT, 0, 12,
				      push_constants);

		radv_unaligned_dispatch(cmd_buffer, rects[r].width, rects[r].height, 1);
	}
	radv_temp_descriptor_set_destroy(cmd_buffer->device, temps.set);
}

struct itoi_temps {
	struct radv_image_view src_iview;
	struct radv_image_view dst_iview;
	VkDescriptorSet set;
};

static void
itoi_bind_descriptors(struct radv_cmd_buffer *cmd_buffer,
		      struct itoi_temps *tmp)
{
	struct radv_device *device = cmd_buffer->device;
	VkDevice vk_device = radv_device_to_handle(cmd_buffer->device);

	radv_temp_descriptor_set_create(device, cmd_buffer,
					device->meta_state.itoi.img_ds_layout,
					&tmp->set);

	radv_UpdateDescriptorSets(vk_device,
				  2, /* writeCount */
				  (VkWriteDescriptorSet[]) {
					  {
						  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						  .dstSet = tmp->set,
						  .dstBinding = 0,
						  .dstArrayElement = 0,
						  .descriptorCount = 1,
						  .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
						  .pImageInfo = (VkDescriptorImageInfo[]) {
							  {
								  .sampler = NULL,
								  .imageView = radv_image_view_to_handle(&tmp->src_iview),
								  .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
							  },
						  }
					  },
					  {
						  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						  .dstSet = tmp->set,
						  .dstBinding = 1,
						  .dstArrayElement = 0,
						  .descriptorCount = 1,
						  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
						  .pImageInfo = (VkDescriptorImageInfo[]) {
							  {
								  .sampler = NULL,
								  .imageView = radv_image_view_to_handle(&tmp->dst_iview),
								  .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
							  },
						  }
					  }
				  }, 0, NULL);

	radv_CmdBindDescriptorSets(radv_cmd_buffer_to_handle(cmd_buffer),
				   VK_PIPELINE_BIND_POINT_COMPUTE,
				   device->meta_state.itoi.img_p_layout, 0, 1,
				   &tmp->set, 0, NULL);
}

static void
itoi_bind_pipeline(struct radv_cmd_buffer *cmd_buffer)
{
	VkPipeline pipeline =
		cmd_buffer->device->meta_state.itoi.pipeline;

	if (cmd_buffer->state.compute_pipeline != radv_pipeline_from_handle(pipeline)) {
		radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
				     VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
	}
}

void
radv_meta_image_to_image_cs(struct radv_cmd_buffer *cmd_buffer,
			    struct radv_meta_blit2d_surf *src,
			    struct radv_meta_blit2d_surf *dst,
			    unsigned num_rects,
			    struct radv_meta_blit2d_rect *rects)
{
	struct radv_device *device = cmd_buffer->device;
	struct itoi_temps temps;

	create_iview(cmd_buffer, src, VK_IMAGE_USAGE_SAMPLED_BIT, &temps.src_iview);
	create_iview(cmd_buffer, dst, VK_IMAGE_USAGE_STORAGE_BIT, &temps.dst_iview);

	itoi_bind_descriptors(cmd_buffer, &temps);

	itoi_bind_pipeline(cmd_buffer);

	for (unsigned r = 0; r < num_rects; ++r) {
		unsigned push_constants[4] = {
			rects[r].src_x,
			rects[r].src_y,
			rects[r].dst_x,
			rects[r].dst_y,
		};
		radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
				      device->meta_state.itoi.img_p_layout,
				      VK_SHADER_STAGE_COMPUTE_BIT, 0, 16,
				      push_constants);

		radv_unaligned_dispatch(cmd_buffer, rects[r].width, rects[r].height, 1);
	}
	radv_temp_descriptor_set_destroy(cmd_buffer->device, temps.set);
}

struct cleari_temps {
	struct radv_image_view dst_iview;
	VkDescriptorSet set;
};

static void
cleari_bind_descriptors(struct radv_cmd_buffer *cmd_buffer,
			struct cleari_temps *tmp)
{
	struct radv_device *device = cmd_buffer->device;
	VkDevice vk_device = radv_device_to_handle(cmd_buffer->device);

	radv_temp_descriptor_set_create(device, cmd_buffer,
					device->meta_state.cleari.img_ds_layout,
					&tmp->set);

	radv_UpdateDescriptorSets(vk_device,
				  1, /* writeCount */
				  (VkWriteDescriptorSet[]) {
					  {
						  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						  .dstSet = tmp->set,
						  .dstBinding = 0,
						  .dstArrayElement = 0,
						  .descriptorCount = 1,
						  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
						  .pImageInfo = (VkDescriptorImageInfo[]) {
							  {
								  .sampler = NULL,
								  .imageView = radv_image_view_to_handle(&tmp->dst_iview),
								  .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
							  },
						  }
					  },
				  }, 0, NULL);

	radv_CmdBindDescriptorSets(radv_cmd_buffer_to_handle(cmd_buffer),
				   VK_PIPELINE_BIND_POINT_COMPUTE,
				   device->meta_state.cleari.img_p_layout, 0, 1,
				   &tmp->set, 0, NULL);
}

static void
cleari_bind_pipeline(struct radv_cmd_buffer *cmd_buffer)
{
	VkPipeline pipeline =
		cmd_buffer->device->meta_state.cleari.pipeline;

	if (cmd_buffer->state.compute_pipeline != radv_pipeline_from_handle(pipeline)) {
		radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
				     VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
	}
}

void
radv_meta_clear_image_cs(struct radv_cmd_buffer *cmd_buffer,
			 struct radv_meta_blit2d_surf *dst,
			 const VkClearColorValue *clear_color)
{
	struct radv_device *device = cmd_buffer->device;
	struct cleari_temps temps;

	create_iview(cmd_buffer, dst, VK_IMAGE_USAGE_STORAGE_BIT, &temps.dst_iview);
	cleari_bind_descriptors(cmd_buffer, &temps);

	cleari_bind_pipeline(cmd_buffer);

	unsigned push_constants[4] = {
		clear_color->uint32[0],
		clear_color->uint32[1],
		clear_color->uint32[2],
		clear_color->uint32[3],
	};

	radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
			      device->meta_state.cleari.img_p_layout,
			      VK_SHADER_STAGE_COMPUTE_BIT, 0, 16,
			      push_constants);

	radv_unaligned_dispatch(cmd_buffer, dst->image->extent.width, dst->image->extent.height, 1);
	radv_temp_descriptor_set_destroy(cmd_buffer->device, temps.set);
}
