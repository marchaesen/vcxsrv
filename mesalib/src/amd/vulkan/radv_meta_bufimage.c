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

/* GFX9 needs to use a 3D sampler to access 3D resources, so the shader has the options
 * for that.
 */
static nir_shader *
build_nir_itob_compute_shader(struct radv_device *dev, bool is_3d)
{
	nir_builder b;
	enum glsl_sampler_dim dim = is_3d ? GLSL_SAMPLER_DIM_3D : GLSL_SAMPLER_DIM_2D;
	const struct glsl_type *sampler_type = glsl_sampler_type(dim,
								 false,
								 false,
								 GLSL_TYPE_FLOAT);
	const struct glsl_type *img_type = glsl_sampler_type(GLSL_SAMPLER_DIM_BUF,
							     false,
							     false,
							     GLSL_TYPE_FLOAT);
	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info.name = ralloc_strdup(b.shader, is_3d ? "meta_itob_cs_3d" : "meta_itob_cs");
	b.shader->info.cs.local_size[0] = 16;
	b.shader->info.cs.local_size[1] = 16;
	b.shader->info.cs.local_size[2] = 1;
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
						b.shader->info.cs.local_size[0],
						b.shader->info.cs.local_size[1],
						b.shader->info.cs.local_size[2], 0);

	nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);



	nir_intrinsic_instr *offset = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	nir_intrinsic_set_base(offset, 0);
	nir_intrinsic_set_range(offset, 16);
	offset->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	offset->num_components = is_3d ? 3 : 2;
	nir_ssa_dest_init(&offset->instr, &offset->dest, is_3d ? 3 : 2, 32, "offset");
	nir_builder_instr_insert(&b, &offset->instr);

	nir_intrinsic_instr *stride = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	nir_intrinsic_set_base(stride, 0);
	nir_intrinsic_set_range(stride, 16);
	stride->src[0] = nir_src_for_ssa(nir_imm_int(&b, 12));
	stride->num_components = 1;
	nir_ssa_dest_init(&stride->instr, &stride->dest, 1, 32, "stride");
	nir_builder_instr_insert(&b, &stride->instr);

	nir_ssa_def *img_coord = nir_iadd(&b, global_id, &offset->dest.ssa);
	nir_tex_instr *tex = nir_tex_instr_create(b.shader, 2);
	tex->sampler_dim = dim;
	tex->op = nir_texop_txf;
	tex->src[0].src_type = nir_tex_src_coord;
	tex->src[0].src = nir_src_for_ssa(nir_channels(&b, img_coord, is_3d ? 0x7 : 0x3));
	tex->src[1].src_type = nir_tex_src_lod;
	tex->src[1].src = nir_src_for_ssa(nir_imm_int(&b, 0));
	tex->dest_type = nir_type_float;
	tex->is_array = false;
	tex->coord_components = is_3d ? 3 : 2;
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
	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_image_var_store);
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
	struct radv_shader_module cs_3d = { .nir = NULL };

	cs.nir = build_nir_itob_compute_shader(device, false);
	if (device->physical_device->rad_info.chip_class >= GFX9)
		cs_3d.nir = build_nir_itob_compute_shader(device, true);

	/*
	 * two descriptors one for the image being sampled
	 * one for the buffer being written.
	 */
	VkDescriptorSetLayoutCreateInfo ds_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
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
		.pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 16},
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

	if (device->physical_device->rad_info.chip_class >= GFX9) {
		VkPipelineShaderStageCreateInfo pipeline_shader_stage_3d = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = radv_shader_module_to_handle(&cs_3d),
			.pName = "main",
			.pSpecializationInfo = NULL,
		};

		VkComputePipelineCreateInfo vk_pipeline_info_3d = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = pipeline_shader_stage_3d,
			.flags = 0,
			.layout = device->meta_state.itob.img_p_layout,
		};

		result = radv_CreateComputePipelines(radv_device_to_handle(device),
						     radv_pipeline_cache_to_handle(&device->meta_state.cache),
						     1, &vk_pipeline_info_3d, NULL,
						     &device->meta_state.itob.pipeline_3d);
		if (result != VK_SUCCESS)
			goto fail;
		ralloc_free(cs_3d.nir);
	}
	ralloc_free(cs.nir);

	return VK_SUCCESS;
fail:
	ralloc_free(cs.nir);
	ralloc_free(cs_3d.nir);
	return result;
}

static void
radv_device_finish_meta_itob_state(struct radv_device *device)
{
	struct radv_meta_state *state = &device->meta_state;

	radv_DestroyPipelineLayout(radv_device_to_handle(device),
				   state->itob.img_p_layout, &state->alloc);
	radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
				        state->itob.img_ds_layout,
					&state->alloc);
	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->itob.pipeline, &state->alloc);
	if (device->physical_device->rad_info.chip_class >= GFX9)
		radv_DestroyPipeline(radv_device_to_handle(device),
				     state->itob.pipeline_3d, &state->alloc);
}

static nir_shader *
build_nir_btoi_compute_shader(struct radv_device *dev, bool is_3d)
{
	nir_builder b;
	enum glsl_sampler_dim dim = is_3d ? GLSL_SAMPLER_DIM_3D : GLSL_SAMPLER_DIM_2D;
	const struct glsl_type *buf_type = glsl_sampler_type(GLSL_SAMPLER_DIM_BUF,
							     false,
							     false,
							     GLSL_TYPE_FLOAT);
	const struct glsl_type *img_type = glsl_sampler_type(dim,
							     false,
							     false,
							     GLSL_TYPE_FLOAT);
	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info.name = ralloc_strdup(b.shader, is_3d ? "meta_btoi_cs_3d" : "meta_btoi_cs");
	b.shader->info.cs.local_size[0] = 16;
	b.shader->info.cs.local_size[1] = 16;
	b.shader->info.cs.local_size[2] = 1;
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
						b.shader->info.cs.local_size[0],
						b.shader->info.cs.local_size[1],
						b.shader->info.cs.local_size[2], 0);

	nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

	nir_intrinsic_instr *offset = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	nir_intrinsic_set_base(offset, 0);
	nir_intrinsic_set_range(offset, 16);
	offset->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	offset->num_components = is_3d ? 3 : 2;
	nir_ssa_dest_init(&offset->instr, &offset->dest, is_3d ? 3 : 2, 32, "offset");
	nir_builder_instr_insert(&b, &offset->instr);

	nir_intrinsic_instr *stride = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	nir_intrinsic_set_base(stride, 0);
	nir_intrinsic_set_range(stride, 16);
	stride->src[0] = nir_src_for_ssa(nir_imm_int(&b, 12));
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
	tex->src[0].src = nir_src_for_ssa(nir_channels(&b, buf_coord, 1));
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
	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_image_var_store);
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
	struct radv_shader_module cs_3d = { .nir = NULL };
	cs.nir = build_nir_btoi_compute_shader(device, false);
	if (device->physical_device->rad_info.chip_class >= GFX9)
		cs_3d.nir = build_nir_btoi_compute_shader(device, true);
	/*
	 * two descriptors one for the image being sampled
	 * one for the buffer being written.
	 */
	VkDescriptorSetLayoutCreateInfo ds_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
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
		.pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 16},
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

	if (device->physical_device->rad_info.chip_class >= GFX9) {
		VkPipelineShaderStageCreateInfo pipeline_shader_stage_3d = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = radv_shader_module_to_handle(&cs_3d),
			.pName = "main",
			.pSpecializationInfo = NULL,
		};

		VkComputePipelineCreateInfo vk_pipeline_info_3d = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = pipeline_shader_stage_3d,
			.flags = 0,
			.layout = device->meta_state.btoi.img_p_layout,
		};

		result = radv_CreateComputePipelines(radv_device_to_handle(device),
						     radv_pipeline_cache_to_handle(&device->meta_state.cache),
						     1, &vk_pipeline_info_3d, NULL,
						     &device->meta_state.btoi.pipeline_3d);
		ralloc_free(cs_3d.nir);
	}
	ralloc_free(cs.nir);

	return VK_SUCCESS;
fail:
	ralloc_free(cs_3d.nir);
	ralloc_free(cs.nir);
	return result;
}

static void
radv_device_finish_meta_btoi_state(struct radv_device *device)
{
	struct radv_meta_state *state = &device->meta_state;

	radv_DestroyPipelineLayout(radv_device_to_handle(device),
				   state->btoi.img_p_layout, &state->alloc);
	radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
				        state->btoi.img_ds_layout,
					&state->alloc);
	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->btoi.pipeline, &state->alloc);
	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->btoi.pipeline_3d, &state->alloc);
}

static nir_shader *
build_nir_itoi_compute_shader(struct radv_device *dev, bool is_3d)
{
	nir_builder b;
	enum glsl_sampler_dim dim = is_3d ? GLSL_SAMPLER_DIM_3D : GLSL_SAMPLER_DIM_2D;
	const struct glsl_type *buf_type = glsl_sampler_type(dim,
							     false,
							     false,
							     GLSL_TYPE_FLOAT);
	const struct glsl_type *img_type = glsl_sampler_type(dim,
							     false,
							     false,
							     GLSL_TYPE_FLOAT);
	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info.name = ralloc_strdup(b.shader, is_3d ? "meta_itoi_cs_3d" : "meta_itoi_cs");
	b.shader->info.cs.local_size[0] = 16;
	b.shader->info.cs.local_size[1] = 16;
	b.shader->info.cs.local_size[2] = 1;
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
						b.shader->info.cs.local_size[0],
						b.shader->info.cs.local_size[1],
						b.shader->info.cs.local_size[2], 0);

	nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

	nir_intrinsic_instr *src_offset = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	nir_intrinsic_set_base(src_offset, 0);
	nir_intrinsic_set_range(src_offset, 24);
	src_offset->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	src_offset->num_components = is_3d ? 3 : 2;
	nir_ssa_dest_init(&src_offset->instr, &src_offset->dest, is_3d ? 3 : 2, 32, "src_offset");
	nir_builder_instr_insert(&b, &src_offset->instr);

	nir_intrinsic_instr *dst_offset = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	nir_intrinsic_set_base(dst_offset, 0);
	nir_intrinsic_set_range(dst_offset, 24);
	dst_offset->src[0] = nir_src_for_ssa(nir_imm_int(&b, 12));
	dst_offset->num_components = is_3d ? 3 : 2;
	nir_ssa_dest_init(&dst_offset->instr, &dst_offset->dest, is_3d ? 3 : 2, 32, "dst_offset");
	nir_builder_instr_insert(&b, &dst_offset->instr);

	nir_ssa_def *src_coord = nir_iadd(&b, global_id, &src_offset->dest.ssa);

	nir_ssa_def *dst_coord = nir_iadd(&b, global_id, &dst_offset->dest.ssa);

	nir_tex_instr *tex = nir_tex_instr_create(b.shader, 2);
	tex->sampler_dim = dim;
	tex->op = nir_texop_txf;
	tex->src[0].src_type = nir_tex_src_coord;
	tex->src[0].src = nir_src_for_ssa(nir_channels(&b, src_coord, is_3d ? 0x7 : 0x3));
	tex->src[1].src_type = nir_tex_src_lod;
	tex->src[1].src = nir_src_for_ssa(nir_imm_int(&b, 0));
	tex->dest_type = nir_type_float;
	tex->is_array = false;
	tex->coord_components = is_3d ? 3 : 2;
	tex->texture = nir_deref_var_create(tex, input_img);
	tex->sampler = NULL;

	nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
	nir_builder_instr_insert(&b, &tex->instr);

	nir_ssa_def *outval = &tex->dest.ssa;
	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_image_var_store);
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
	struct radv_shader_module cs_3d = { .nir = NULL };
	cs.nir = build_nir_itoi_compute_shader(device, false);
	if (device->physical_device->rad_info.chip_class >= GFX9)
		cs_3d.nir = build_nir_itoi_compute_shader(device, true);
	/*
	 * two descriptors one for the image being sampled
	 * one for the buffer being written.
	 */
	VkDescriptorSetLayoutCreateInfo ds_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
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
		.pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 24},
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

	if (device->physical_device->rad_info.chip_class >= GFX9) {
		VkPipelineShaderStageCreateInfo pipeline_shader_stage_3d = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = radv_shader_module_to_handle(&cs_3d),
			.pName = "main",
			.pSpecializationInfo = NULL,
		};

		VkComputePipelineCreateInfo vk_pipeline_info_3d = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = pipeline_shader_stage_3d,
			.flags = 0,
			.layout = device->meta_state.itoi.img_p_layout,
		};

		result = radv_CreateComputePipelines(radv_device_to_handle(device),
						     radv_pipeline_cache_to_handle(&device->meta_state.cache),
						     1, &vk_pipeline_info_3d, NULL,
						     &device->meta_state.itoi.pipeline_3d);

		ralloc_free(cs_3d.nir);
	}
	ralloc_free(cs.nir);

	return VK_SUCCESS;
fail:
	ralloc_free(cs.nir);
	ralloc_free(cs_3d.nir);
	return result;
}

static void
radv_device_finish_meta_itoi_state(struct radv_device *device)
{
	struct radv_meta_state *state = &device->meta_state;

	radv_DestroyPipelineLayout(radv_device_to_handle(device),
				   state->itoi.img_p_layout, &state->alloc);
	radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
				        state->itoi.img_ds_layout,
					&state->alloc);
	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->itoi.pipeline, &state->alloc);
	if (device->physical_device->rad_info.chip_class >= GFX9)
		radv_DestroyPipeline(radv_device_to_handle(device),
				     state->itoi.pipeline_3d, &state->alloc);
}

static nir_shader *
build_nir_cleari_compute_shader(struct radv_device *dev, bool is_3d)
{
	nir_builder b;
	enum glsl_sampler_dim dim = is_3d ? GLSL_SAMPLER_DIM_3D : GLSL_SAMPLER_DIM_2D;
	const struct glsl_type *img_type = glsl_sampler_type(dim,
							     false,
							     false,
							     GLSL_TYPE_FLOAT);
	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info.name = ralloc_strdup(b.shader, is_3d ? "meta_cleari_cs_3d" : "meta_cleari_cs");
	b.shader->info.cs.local_size[0] = 16;
	b.shader->info.cs.local_size[1] = 16;
	b.shader->info.cs.local_size[2] = 1;

	nir_variable *output_img = nir_variable_create(b.shader, nir_var_uniform,
						       img_type, "out_img");
	output_img->data.descriptor_set = 0;
	output_img->data.binding = 0;

	nir_ssa_def *invoc_id = nir_load_system_value(&b, nir_intrinsic_load_local_invocation_id, 0);
	nir_ssa_def *wg_id = nir_load_system_value(&b, nir_intrinsic_load_work_group_id, 0);
	nir_ssa_def *block_size = nir_imm_ivec4(&b,
						b.shader->info.cs.local_size[0],
						b.shader->info.cs.local_size[1],
						b.shader->info.cs.local_size[2], 0);

	nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

	nir_intrinsic_instr *clear_val = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	nir_intrinsic_set_base(clear_val, 0);
	nir_intrinsic_set_range(clear_val, 20);
	clear_val->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	clear_val->num_components = 4;
	nir_ssa_dest_init(&clear_val->instr, &clear_val->dest, 4, 32, "clear_value");
	nir_builder_instr_insert(&b, &clear_val->instr);

	nir_intrinsic_instr *layer = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	nir_intrinsic_set_base(layer, 0);
	nir_intrinsic_set_range(layer, 20);
	layer->src[0] = nir_src_for_ssa(nir_imm_int(&b, 16));
	layer->num_components = 1;
	nir_ssa_dest_init(&layer->instr, &layer->dest, 1, 32, "layer");
	nir_builder_instr_insert(&b, &layer->instr);

	nir_ssa_def *global_z = nir_iadd(&b, nir_channel(&b, global_id, 2), &layer->dest.ssa);

	nir_ssa_def *comps[4];
	comps[0] = nir_channel(&b, global_id, 0);
	comps[1] = nir_channel(&b, global_id, 1);
	comps[2] = global_z;
	comps[3] = nir_imm_int(&b, 0);
	global_id = nir_vec(&b, comps, 4);

	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_image_var_store);
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
	struct radv_shader_module cs_3d = { .nir = NULL };
	cs.nir = build_nir_cleari_compute_shader(device, false);
	if (device->physical_device->rad_info.chip_class >= GFX9)
		cs_3d.nir = build_nir_cleari_compute_shader(device, true);

	/*
	 * two descriptors one for the image being sampled
	 * one for the buffer being written.
	 */
	VkDescriptorSetLayoutCreateInfo ds_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
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
		.pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 20},
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


	if (device->physical_device->rad_info.chip_class >= GFX9) {
		/* compute shader */
		VkPipelineShaderStageCreateInfo pipeline_shader_stage_3d = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = radv_shader_module_to_handle(&cs_3d),
			.pName = "main",
			.pSpecializationInfo = NULL,
		};

		VkComputePipelineCreateInfo vk_pipeline_info_3d = {
			.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
			.stage = pipeline_shader_stage_3d,
			.flags = 0,
			.layout = device->meta_state.cleari.img_p_layout,
		};

		result = radv_CreateComputePipelines(radv_device_to_handle(device),
						     radv_pipeline_cache_to_handle(&device->meta_state.cache),
						     1, &vk_pipeline_info_3d, NULL,
						     &device->meta_state.cleari.pipeline_3d);
		if (result != VK_SUCCESS)
			goto fail;

		ralloc_free(cs_3d.nir);
	}
	ralloc_free(cs.nir);
	return VK_SUCCESS;
fail:
	ralloc_free(cs.nir);
	ralloc_free(cs_3d.nir);
	return result;
}

static void
radv_device_finish_meta_cleari_state(struct radv_device *device)
{
	struct radv_meta_state *state = &device->meta_state;

	radv_DestroyPipelineLayout(radv_device_to_handle(device),
				   state->cleari.img_p_layout, &state->alloc);
	radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
				        state->cleari.img_ds_layout,
					&state->alloc);
	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->cleari.pipeline, &state->alloc);
	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->cleari.pipeline_3d, &state->alloc);
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
		goto fail_itob;

	result = radv_device_init_meta_btoi_state(device);
	if (result != VK_SUCCESS)
		goto fail_btoi;

	result = radv_device_init_meta_itoi_state(device);
	if (result != VK_SUCCESS)
		goto fail_itoi;

	result = radv_device_init_meta_cleari_state(device);
	if (result != VK_SUCCESS)
		goto fail_cleari;

	return VK_SUCCESS;
fail_cleari:
	radv_device_finish_meta_cleari_state(device);
fail_itoi:
	radv_device_finish_meta_itoi_state(device);
fail_btoi:
	radv_device_finish_meta_btoi_state(device);
fail_itob:
	radv_device_finish_meta_itob_state(device);
	return result;
}

static void
create_iview(struct radv_cmd_buffer *cmd_buffer,
             struct radv_meta_blit2d_surf *surf,
             struct radv_image_view *iview)
{
	VkImageViewType view_type = cmd_buffer->device->physical_device->rad_info.chip_class < GFX9 ? VK_IMAGE_VIEW_TYPE_2D :
		radv_meta_get_view_type(surf->image);
	radv_image_view_init(iview, cmd_buffer->device,
			     &(VkImageViewCreateInfo) {
				     .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					     .image = radv_image_to_handle(surf->image),
					     .viewType = view_type,
					     .format = surf->format,
					     .subresourceRange = {
					     .aspectMask = surf->aspect_mask,
					     .baseMipLevel = surf->level,
					     .levelCount = 1,
					     .baseArrayLayer = surf->layer,
					     .layerCount = 1
				     },
			     });
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
			      });

}

static void
itob_bind_descriptors(struct radv_cmd_buffer *cmd_buffer,
		      struct radv_image_view *src,
		      struct radv_buffer_view *dst)
{
	struct radv_device *device = cmd_buffer->device;

	radv_meta_push_descriptor_set(cmd_buffer,
				      VK_PIPELINE_BIND_POINT_COMPUTE,
				      device->meta_state.itob.img_p_layout,
				      0, /* set */
				      2, /* descriptorWriteCount */
				      (VkWriteDescriptorSet[]) {
				              {
				                      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				                      .dstBinding = 0,
				                      .dstArrayElement = 0,
				                      .descriptorCount = 1,
				                      .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
				                      .pImageInfo = (VkDescriptorImageInfo[]) {
				                              {
				                                      .sampler = VK_NULL_HANDLE,
				                                      .imageView = radv_image_view_to_handle(src),
				                                      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				                              },
				                      }
				              },
				              {
				                      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				                      .dstBinding = 1,
				                      .dstArrayElement = 0,
				                      .descriptorCount = 1,
				                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
				                      .pTexelBufferView = (VkBufferView[])  { radv_buffer_view_to_handle(dst) },
				              }
				      });
}

void
radv_meta_image_to_buffer(struct radv_cmd_buffer *cmd_buffer,
			  struct radv_meta_blit2d_surf *src,
			  struct radv_meta_blit2d_buffer *dst,
			  unsigned num_rects,
			  struct radv_meta_blit2d_rect *rects)
{
	VkPipeline pipeline = cmd_buffer->device->meta_state.itob.pipeline;
	struct radv_device *device = cmd_buffer->device;
	struct radv_image_view src_view;
	struct radv_buffer_view dst_view;

	create_iview(cmd_buffer, src, &src_view);
	create_bview(cmd_buffer, dst->buffer, dst->offset, dst->format, &dst_view);
	itob_bind_descriptors(cmd_buffer, &src_view, &dst_view);

	if (device->physical_device->rad_info.chip_class >= GFX9 &&
	    src->image->type == VK_IMAGE_TYPE_3D)
		pipeline = cmd_buffer->device->meta_state.itob.pipeline_3d;

	radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
			     VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

	for (unsigned r = 0; r < num_rects; ++r) {
		unsigned push_constants[4] = {
			rects[r].src_x,
			rects[r].src_y,
			src->layer,
			dst->pitch
		};
		radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
				      device->meta_state.itob.img_p_layout,
				      VK_SHADER_STAGE_COMPUTE_BIT, 0, 16,
				      push_constants);

		radv_unaligned_dispatch(cmd_buffer, rects[r].width, rects[r].height, 1);
	}
}

static void
btoi_bind_descriptors(struct radv_cmd_buffer *cmd_buffer,
		      struct radv_buffer_view *src,
		      struct radv_image_view *dst)
{
	struct radv_device *device = cmd_buffer->device;

	radv_meta_push_descriptor_set(cmd_buffer,
				      VK_PIPELINE_BIND_POINT_COMPUTE,
				      device->meta_state.btoi.img_p_layout,
				      0, /* set */
				      2, /* descriptorWriteCount */
				      (VkWriteDescriptorSet[]) {
				              {
				                      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				                      .dstBinding = 0,
				                      .dstArrayElement = 0,
				                      .descriptorCount = 1,
				                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,
				                      .pTexelBufferView = (VkBufferView[])  { radv_buffer_view_to_handle(src) },
				              },
				              {
				                      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				                      .dstBinding = 1,
				                      .dstArrayElement = 0,
				                      .descriptorCount = 1,
				                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				                      .pImageInfo = (VkDescriptorImageInfo[]) {
				                              {
				                                      .sampler = VK_NULL_HANDLE,
				                                      .imageView = radv_image_view_to_handle(dst),
				                                      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				                              },
				                      }
				              }
				      });
}

void
radv_meta_buffer_to_image_cs(struct radv_cmd_buffer *cmd_buffer,
			     struct radv_meta_blit2d_buffer *src,
			     struct radv_meta_blit2d_surf *dst,
			     unsigned num_rects,
			     struct radv_meta_blit2d_rect *rects)
{
	VkPipeline pipeline = cmd_buffer->device->meta_state.btoi.pipeline;
	struct radv_device *device = cmd_buffer->device;
	struct radv_buffer_view src_view;
	struct radv_image_view dst_view;

	create_bview(cmd_buffer, src->buffer, src->offset, src->format, &src_view);
	create_iview(cmd_buffer, dst, &dst_view);
	btoi_bind_descriptors(cmd_buffer, &src_view, &dst_view);

	if (device->physical_device->rad_info.chip_class >= GFX9 &&
	    dst->image->type == VK_IMAGE_TYPE_3D)
		pipeline = cmd_buffer->device->meta_state.btoi.pipeline_3d;
	radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
			     VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

	for (unsigned r = 0; r < num_rects; ++r) {
		unsigned push_constants[4] = {
			rects[r].dst_x,
			rects[r].dst_y,
			dst->layer,
			src->pitch,
		};
		radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
				      device->meta_state.btoi.img_p_layout,
				      VK_SHADER_STAGE_COMPUTE_BIT, 0, 16,
				      push_constants);

		radv_unaligned_dispatch(cmd_buffer, rects[r].width, rects[r].height, 1);
	}
}

static void
itoi_bind_descriptors(struct radv_cmd_buffer *cmd_buffer,
		      struct radv_image_view *src,
		      struct radv_image_view *dst)
{
	struct radv_device *device = cmd_buffer->device;

	radv_meta_push_descriptor_set(cmd_buffer,
				      VK_PIPELINE_BIND_POINT_COMPUTE,
				      device->meta_state.itoi.img_p_layout,
				      0, /* set */
				      2, /* descriptorWriteCount */
				      (VkWriteDescriptorSet[]) {
				              {
				                       .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				                       .dstBinding = 0,
				                       .dstArrayElement = 0,
				                       .descriptorCount = 1,
				                       .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
				                       .pImageInfo = (VkDescriptorImageInfo[]) {
				                               {
				                                       .sampler = VK_NULL_HANDLE,
				                                       .imageView = radv_image_view_to_handle(src),
				                                       .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				                               },
				                       }
				              },
				              {
				                       .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				                       .dstBinding = 1,
				                       .dstArrayElement = 0,
				                       .descriptorCount = 1,
				                       .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				                       .pImageInfo = (VkDescriptorImageInfo[]) {
				                               {
				                                       .sampler = VK_NULL_HANDLE,
				                                       .imageView = radv_image_view_to_handle(dst),
				                                       .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				                               },
				                       }
				              }
				      });
}

void
radv_meta_image_to_image_cs(struct radv_cmd_buffer *cmd_buffer,
			    struct radv_meta_blit2d_surf *src,
			    struct radv_meta_blit2d_surf *dst,
			    unsigned num_rects,
			    struct radv_meta_blit2d_rect *rects)
{
	VkPipeline pipeline = cmd_buffer->device->meta_state.itoi.pipeline;
	struct radv_device *device = cmd_buffer->device;
	struct radv_image_view src_view, dst_view;

	create_iview(cmd_buffer, src, &src_view);
	create_iview(cmd_buffer, dst, &dst_view);

	itoi_bind_descriptors(cmd_buffer, &src_view, &dst_view);

	if (device->physical_device->rad_info.chip_class >= GFX9 &&
	    src->image->type == VK_IMAGE_TYPE_3D)
		pipeline = cmd_buffer->device->meta_state.itoi.pipeline_3d;
	radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
			     VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

	for (unsigned r = 0; r < num_rects; ++r) {
		unsigned push_constants[6] = {
			rects[r].src_x,
			rects[r].src_y,
			src->layer,
			rects[r].dst_x,
			rects[r].dst_y,
			dst->layer,
		};
		radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
				      device->meta_state.itoi.img_p_layout,
				      VK_SHADER_STAGE_COMPUTE_BIT, 0, 24,
				      push_constants);

		radv_unaligned_dispatch(cmd_buffer, rects[r].width, rects[r].height, 1);
	}
}

static void
cleari_bind_descriptors(struct radv_cmd_buffer *cmd_buffer,
	                struct radv_image_view *dst_iview)
{
	struct radv_device *device = cmd_buffer->device;

	radv_meta_push_descriptor_set(cmd_buffer,
				      VK_PIPELINE_BIND_POINT_COMPUTE,
				      device->meta_state.cleari.img_p_layout,
				      0, /* set */
				      1, /* descriptorWriteCount */
				      (VkWriteDescriptorSet[]) {
				              {
				                      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				                      .dstBinding = 0,
				                      .dstArrayElement = 0,
				                      .descriptorCount = 1,
				                      .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
				                      .pImageInfo = (VkDescriptorImageInfo[]) {
				                               {
				                                      .sampler = VK_NULL_HANDLE,
				                                      .imageView = radv_image_view_to_handle(dst_iview),
				                                      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				                               },
				                      }
				               },
				      });
}

void
radv_meta_clear_image_cs(struct radv_cmd_buffer *cmd_buffer,
			 struct radv_meta_blit2d_surf *dst,
			 const VkClearColorValue *clear_color)
{
	VkPipeline pipeline = cmd_buffer->device->meta_state.cleari.pipeline;
	struct radv_device *device = cmd_buffer->device;
	struct radv_image_view dst_iview;

	create_iview(cmd_buffer, dst, &dst_iview);
	cleari_bind_descriptors(cmd_buffer, &dst_iview);

	if (device->physical_device->rad_info.chip_class >= GFX9 &&
	    dst->image->type == VK_IMAGE_TYPE_3D)
		pipeline = cmd_buffer->device->meta_state.cleari.pipeline_3d;

	radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
			     VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

	unsigned push_constants[5] = {
		clear_color->uint32[0],
		clear_color->uint32[1],
		clear_color->uint32[2],
		clear_color->uint32[3],
		dst->layer,
	};

	radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
			      device->meta_state.cleari.img_p_layout,
			      VK_SHADER_STAGE_COMPUTE_BIT, 0, 20,
			      push_constants);

	radv_unaligned_dispatch(cmd_buffer, dst->image->info.width, dst->image->info.height, 1);
}
