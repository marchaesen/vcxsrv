/*
 * Copyright © 2016 Dave Airlie
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


#include <assert.h>
#include <stdbool.h>

#include "radv_meta.h"
#include "radv_private.h"
#include "nir/nir_builder.h"
#include "sid.h"
#include "vk_format.h"

static nir_ssa_def *radv_meta_build_resolve_srgb_conversion(nir_builder *b,
							    nir_ssa_def *input)
{
	unsigned i;

	nir_ssa_def *cmp[3];
	for (i = 0; i < 3; i++)
		cmp[i] = nir_flt(b, nir_channel(b, input, i),
				 nir_imm_int(b, 0x3b4d2e1c));

	nir_ssa_def *ltvals[3];
	for (i = 0; i < 3; i++)
		ltvals[i] = nir_fmul(b, nir_channel(b, input, i),
				     nir_imm_float(b, 12.92));

	nir_ssa_def *gtvals[3];

	for (i = 0; i < 3; i++) {
		gtvals[i] = nir_fpow(b, nir_channel(b, input, i),
				     nir_imm_float(b, 1.0/2.4));
		gtvals[i] = nir_fmul(b, gtvals[i],
				     nir_imm_float(b, 1.055));
		gtvals[i] = nir_fsub(b, gtvals[i],
				     nir_imm_float(b, 0.055));
	}

	nir_ssa_def *comp[4];
	for (i = 0; i < 3; i++)
		comp[i] = nir_bcsel(b, cmp[i], ltvals[i], gtvals[i]);
	comp[3] = nir_channels(b, input, 1 << 3);
	return nir_vec(b, comp, 4);
}

static nir_shader *
build_resolve_compute_shader(struct radv_device *dev, bool is_integer, bool is_srgb, int samples)
{
	nir_builder b;
	char name[64];
	const struct glsl_type *sampler_type = glsl_sampler_type(GLSL_SAMPLER_DIM_MS,
								 false,
								 false,
								 GLSL_TYPE_FLOAT);
	const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_2D,
							   false,
							   GLSL_TYPE_FLOAT);
	snprintf(name, 64, "meta_resolve_cs-%d-%s", samples, is_integer ? "int" : (is_srgb ? "srgb" : "float"));
	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info.name = ralloc_strdup(b.shader, name);
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
	nir_ssa_def *invoc_id = nir_load_local_invocation_id(&b);
	nir_ssa_def *wg_id = nir_load_work_group_id(&b, 32);
	nir_ssa_def *block_size = nir_imm_ivec4(&b,
						b.shader->info.cs.local_size[0],
						b.shader->info.cs.local_size[1],
						b.shader->info.cs.local_size[2], 0);

	nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

	nir_intrinsic_instr *src_offset = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	nir_intrinsic_set_base(src_offset, 0);
	nir_intrinsic_set_range(src_offset, 16);
	src_offset->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	src_offset->num_components = 2;
	nir_ssa_dest_init(&src_offset->instr, &src_offset->dest, 2, 32, "src_offset");
	nir_builder_instr_insert(&b, &src_offset->instr);

	nir_intrinsic_instr *dst_offset = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	nir_intrinsic_set_base(dst_offset, 0);
	nir_intrinsic_set_range(dst_offset, 16);
	dst_offset->src[0] = nir_src_for_ssa(nir_imm_int(&b, 8));
	dst_offset->num_components = 2;
	nir_ssa_dest_init(&dst_offset->instr, &dst_offset->dest, 2, 32, "dst_offset");
	nir_builder_instr_insert(&b, &dst_offset->instr);

	nir_ssa_def *img_coord = nir_channels(&b, nir_iadd(&b, global_id, &src_offset->dest.ssa), 0x3);
	nir_variable *color = nir_local_variable_create(b.impl, glsl_vec4_type(), "color");

	radv_meta_build_resolve_shader_core(&b, is_integer, samples, input_img,
	                                    color, img_coord);

	nir_ssa_def *outval = nir_load_var(&b, color);
	if (is_srgb)
		outval = radv_meta_build_resolve_srgb_conversion(&b, outval);

	nir_ssa_def *coord = nir_iadd(&b, global_id, &dst_offset->dest.ssa);
	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_image_deref_store);
	store->num_components = 4;
	store->src[0] = nir_src_for_ssa(&nir_build_deref_var(&b, output_img)->dest.ssa);
	store->src[1] = nir_src_for_ssa(coord);
	store->src[2] = nir_src_for_ssa(nir_ssa_undef(&b, 1, 32));
	store->src[3] = nir_src_for_ssa(outval);
	store->src[4] = nir_src_for_ssa(nir_imm_int(&b, 0));
	nir_builder_instr_insert(&b, &store->instr);
	return b.shader;
}

enum {
	DEPTH_RESOLVE,
	STENCIL_RESOLVE,
};

static const char *
get_resolve_mode_str(VkResolveModeFlagBits resolve_mode)
{
	switch (resolve_mode) {
	case VK_RESOLVE_MODE_SAMPLE_ZERO_BIT_KHR:
		return "zero";
	case VK_RESOLVE_MODE_AVERAGE_BIT_KHR:
		return "average";
	case VK_RESOLVE_MODE_MIN_BIT_KHR:
		return "min";
	case VK_RESOLVE_MODE_MAX_BIT_KHR:
		return "max";
	default:
		unreachable("invalid resolve mode");
	}
}

static nir_shader *
build_depth_stencil_resolve_compute_shader(struct radv_device *dev, int samples,
					   int index,
					   VkResolveModeFlagBits resolve_mode)
{
	nir_builder b;
	char name[64];
	const struct glsl_type *sampler_type = glsl_sampler_type(GLSL_SAMPLER_DIM_MS,
								 false,
								 false,
								 GLSL_TYPE_FLOAT);
	const struct glsl_type *img_type = glsl_image_type(GLSL_SAMPLER_DIM_2D,
							   false,
							   GLSL_TYPE_FLOAT);
	snprintf(name, 64, "meta_resolve_cs_%s-%s-%d",
		 index == DEPTH_RESOLVE ? "depth" : "stencil",
		 get_resolve_mode_str(resolve_mode), samples);

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info.name = ralloc_strdup(b.shader, name);
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
	nir_ssa_def *invoc_id = nir_load_local_invocation_id(&b);
	nir_ssa_def *wg_id = nir_load_work_group_id(&b, 32);
	nir_ssa_def *block_size = nir_imm_ivec4(&b,
						b.shader->info.cs.local_size[0],
						b.shader->info.cs.local_size[1],
						b.shader->info.cs.local_size[2], 0);

	nir_ssa_def *global_id = nir_iadd(&b, nir_imul(&b, wg_id, block_size), invoc_id);

	nir_intrinsic_instr *src_offset = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	nir_intrinsic_set_base(src_offset, 0);
	nir_intrinsic_set_range(src_offset, 16);
	src_offset->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	src_offset->num_components = 2;
	nir_ssa_dest_init(&src_offset->instr, &src_offset->dest, 2, 32, "src_offset");
	nir_builder_instr_insert(&b, &src_offset->instr);

	nir_intrinsic_instr *dst_offset = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	nir_intrinsic_set_base(dst_offset, 0);
	nir_intrinsic_set_range(dst_offset, 16);
	dst_offset->src[0] = nir_src_for_ssa(nir_imm_int(&b, 8));
	dst_offset->num_components = 2;
	nir_ssa_dest_init(&dst_offset->instr, &dst_offset->dest, 2, 32, "dst_offset");
	nir_builder_instr_insert(&b, &dst_offset->instr);

	nir_ssa_def *img_coord = nir_channels(&b, nir_iadd(&b, global_id, &src_offset->dest.ssa), 0x3);

	nir_ssa_def *input_img_deref = &nir_build_deref_var(&b, input_img)->dest.ssa;

	nir_alu_type type = index == DEPTH_RESOLVE ? nir_type_float : nir_type_uint;

	nir_tex_instr *tex = nir_tex_instr_create(b.shader, 3);
	tex->sampler_dim = GLSL_SAMPLER_DIM_MS;
	tex->op = nir_texop_txf_ms;
	tex->src[0].src_type = nir_tex_src_coord;
	tex->src[0].src = nir_src_for_ssa(img_coord);
	tex->src[1].src_type = nir_tex_src_ms_index;
	tex->src[1].src = nir_src_for_ssa(nir_imm_int(&b, 0));
	tex->src[2].src_type = nir_tex_src_texture_deref;
	tex->src[2].src = nir_src_for_ssa(input_img_deref);
	tex->dest_type = type;
	tex->is_array = false;
	tex->coord_components = 2;

	nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
	nir_builder_instr_insert(&b, &tex->instr);

	nir_ssa_def *outval = &tex->dest.ssa;

	if (resolve_mode != VK_RESOLVE_MODE_SAMPLE_ZERO_BIT_KHR) {
		for (int i = 1; i < samples; i++) {
			nir_tex_instr *tex_add = nir_tex_instr_create(b.shader, 3);
			tex_add->sampler_dim = GLSL_SAMPLER_DIM_MS;
			tex_add->op = nir_texop_txf_ms;
			tex_add->src[0].src_type = nir_tex_src_coord;
			tex_add->src[0].src = nir_src_for_ssa(img_coord);
			tex_add->src[1].src_type = nir_tex_src_ms_index;
			tex_add->src[1].src = nir_src_for_ssa(nir_imm_int(&b, i));
			tex_add->src[2].src_type = nir_tex_src_texture_deref;
			tex_add->src[2].src = nir_src_for_ssa(input_img_deref);
			tex_add->dest_type = type;
			tex_add->is_array = false;
			tex_add->coord_components = 2;

			nir_ssa_dest_init(&tex_add->instr, &tex_add->dest, 4, 32, "tex");
			nir_builder_instr_insert(&b, &tex_add->instr);

			switch (resolve_mode) {
			case VK_RESOLVE_MODE_AVERAGE_BIT_KHR:
				assert(index == DEPTH_RESOLVE);
				outval = nir_fadd(&b, outval, &tex_add->dest.ssa);
				break;
			case VK_RESOLVE_MODE_MIN_BIT_KHR:
				if (index == DEPTH_RESOLVE)
					outval = nir_fmin(&b, outval, &tex_add->dest.ssa);
				else
					outval = nir_umin(&b, outval, &tex_add->dest.ssa);
				break;
			case VK_RESOLVE_MODE_MAX_BIT_KHR:
				if (index == DEPTH_RESOLVE)
					outval = nir_fmax(&b, outval, &tex_add->dest.ssa);
				else
					outval = nir_umax(&b, outval, &tex_add->dest.ssa);
				break;
			default:
				unreachable("invalid resolve mode");
			}
		}

		if (resolve_mode == VK_RESOLVE_MODE_AVERAGE_BIT_KHR)
			outval = nir_fdiv(&b, outval, nir_imm_float(&b, samples));
	}

	nir_ssa_def *coord = nir_iadd(&b, global_id, &dst_offset->dest.ssa);
	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_image_deref_store);
	store->num_components = 4;
	store->src[0] = nir_src_for_ssa(&nir_build_deref_var(&b, output_img)->dest.ssa);
	store->src[1] = nir_src_for_ssa(coord);
	store->src[2] = nir_src_for_ssa(nir_ssa_undef(&b, 1, 32));
	store->src[3] = nir_src_for_ssa(outval);
	store->src[4] = nir_src_for_ssa(nir_imm_int(&b, 0));
	nir_builder_instr_insert(&b, &store->instr);
	return b.shader;
}

static VkResult
create_layout(struct radv_device *device)
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
						&device->meta_state.resolve_compute.ds_layout);
	if (result != VK_SUCCESS)
		goto fail;


	VkPipelineLayoutCreateInfo pl_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &device->meta_state.resolve_compute.ds_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 16},
	};

	result = radv_CreatePipelineLayout(radv_device_to_handle(device),
					  &pl_create_info,
					  &device->meta_state.alloc,
					  &device->meta_state.resolve_compute.p_layout);
	if (result != VK_SUCCESS)
		goto fail;
	return VK_SUCCESS;
fail:
	return result;
}

static VkResult
create_resolve_pipeline(struct radv_device *device,
			int samples,
			bool is_integer,
			bool is_srgb,
			VkPipeline *pipeline)
{
	VkResult result;
	struct radv_shader_module cs = { .nir = NULL };

	mtx_lock(&device->meta_state.mtx);
	if (*pipeline) {
		mtx_unlock(&device->meta_state.mtx);
		return VK_SUCCESS;
	}

	cs.nir = build_resolve_compute_shader(device, is_integer, is_srgb, samples);

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
		.layout = device->meta_state.resolve_compute.p_layout,
	};

	result = radv_CreateComputePipelines(radv_device_to_handle(device),
					     radv_pipeline_cache_to_handle(&device->meta_state.cache),
					     1, &vk_pipeline_info, NULL,
					     pipeline);
	if (result != VK_SUCCESS)
		goto fail;

	ralloc_free(cs.nir);
	mtx_unlock(&device->meta_state.mtx);
	return VK_SUCCESS;
fail:
	ralloc_free(cs.nir);
	mtx_unlock(&device->meta_state.mtx);
	return result;
}

static VkResult
create_depth_stencil_resolve_pipeline(struct radv_device *device,
				      int samples,
				      int index,
				      VkResolveModeFlagBits resolve_mode,
				      VkPipeline *pipeline)
{
	VkResult result;
	struct radv_shader_module cs = { .nir = NULL };

	mtx_lock(&device->meta_state.mtx);
	if (*pipeline) {
		mtx_unlock(&device->meta_state.mtx);
		return VK_SUCCESS;
	}

	cs.nir = build_depth_stencil_resolve_compute_shader(device, samples,
							    index, resolve_mode);

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
		.layout = device->meta_state.resolve_compute.p_layout,
	};

	result = radv_CreateComputePipelines(radv_device_to_handle(device),
					     radv_pipeline_cache_to_handle(&device->meta_state.cache),
					     1, &vk_pipeline_info, NULL,
					     pipeline);
	if (result != VK_SUCCESS)
		goto fail;

	ralloc_free(cs.nir);
	mtx_unlock(&device->meta_state.mtx);
	return VK_SUCCESS;
fail:
	ralloc_free(cs.nir);
	mtx_unlock(&device->meta_state.mtx);
	return result;
}

VkResult
radv_device_init_meta_resolve_compute_state(struct radv_device *device, bool on_demand)
{
	struct radv_meta_state *state = &device->meta_state;
	VkResult res;

	res = create_layout(device);
	if (res != VK_SUCCESS)
		goto fail;

	if (on_demand)
		return VK_SUCCESS;

	for (uint32_t i = 0; i < MAX_SAMPLES_LOG2; ++i) {
		uint32_t samples = 1 << i;

		res = create_resolve_pipeline(device, samples, false, false,
					      &state->resolve_compute.rc[i].pipeline);
		if (res != VK_SUCCESS)
			goto fail;

		res = create_resolve_pipeline(device, samples, true, false,
					      &state->resolve_compute.rc[i].i_pipeline);
		if (res != VK_SUCCESS)
			goto fail;

		res = create_resolve_pipeline(device, samples, false, true,
					      &state->resolve_compute.rc[i].srgb_pipeline);
		if (res != VK_SUCCESS)
			goto fail;

		res = create_depth_stencil_resolve_pipeline(device, samples,
							    DEPTH_RESOLVE,
							    VK_RESOLVE_MODE_AVERAGE_BIT_KHR,
							    &state->resolve_compute.depth[i].average_pipeline);
		if (res != VK_SUCCESS)
			goto fail;

		res = create_depth_stencil_resolve_pipeline(device, samples,
							    DEPTH_RESOLVE,
							    VK_RESOLVE_MODE_MAX_BIT_KHR,
							    &state->resolve_compute.depth[i].max_pipeline);
		if (res != VK_SUCCESS)
			goto fail;

		res = create_depth_stencil_resolve_pipeline(device, samples,
							    DEPTH_RESOLVE,
							    VK_RESOLVE_MODE_MIN_BIT_KHR,
							    &state->resolve_compute.depth[i].min_pipeline);
		if (res != VK_SUCCESS)
			goto fail;

		res = create_depth_stencil_resolve_pipeline(device, samples,
							    STENCIL_RESOLVE,
							    VK_RESOLVE_MODE_MAX_BIT_KHR,
							    &state->resolve_compute.stencil[i].max_pipeline);
		if (res != VK_SUCCESS)
			goto fail;

		res = create_depth_stencil_resolve_pipeline(device, samples,
							    STENCIL_RESOLVE,
							    VK_RESOLVE_MODE_MIN_BIT_KHR,
							    &state->resolve_compute.stencil[i].min_pipeline);
		if (res != VK_SUCCESS)
			goto fail;
	}

	res = create_depth_stencil_resolve_pipeline(device, 0,
						    DEPTH_RESOLVE,
						    VK_RESOLVE_MODE_SAMPLE_ZERO_BIT_KHR,
						    &state->resolve_compute.depth_zero_pipeline);
	if (res != VK_SUCCESS)
		goto fail;

	res = create_depth_stencil_resolve_pipeline(device, 0,
						    STENCIL_RESOLVE,
						    VK_RESOLVE_MODE_SAMPLE_ZERO_BIT_KHR,
						    &state->resolve_compute.stencil_zero_pipeline);
	if (res != VK_SUCCESS)
		goto fail;

	return VK_SUCCESS;
fail:
	radv_device_finish_meta_resolve_compute_state(device);
	return res;
}

void
radv_device_finish_meta_resolve_compute_state(struct radv_device *device)
{
	struct radv_meta_state *state = &device->meta_state;
	for (uint32_t i = 0; i < MAX_SAMPLES_LOG2; ++i) {
		radv_DestroyPipeline(radv_device_to_handle(device),
				     state->resolve_compute.rc[i].pipeline,
				     &state->alloc);

		radv_DestroyPipeline(radv_device_to_handle(device),
				     state->resolve_compute.rc[i].i_pipeline,
				     &state->alloc);

		radv_DestroyPipeline(radv_device_to_handle(device),
				     state->resolve_compute.rc[i].srgb_pipeline,
				     &state->alloc);

		radv_DestroyPipeline(radv_device_to_handle(device),
				     state->resolve_compute.depth[i].average_pipeline,
				     &state->alloc);

		radv_DestroyPipeline(radv_device_to_handle(device),
				     state->resolve_compute.depth[i].max_pipeline,
				     &state->alloc);

		radv_DestroyPipeline(radv_device_to_handle(device),
				     state->resolve_compute.depth[i].min_pipeline,
				     &state->alloc);

		radv_DestroyPipeline(radv_device_to_handle(device),
				     state->resolve_compute.stencil[i].max_pipeline,
				     &state->alloc);

		radv_DestroyPipeline(radv_device_to_handle(device),
				     state->resolve_compute.stencil[i].min_pipeline,
				     &state->alloc);
	}

	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->resolve_compute.depth_zero_pipeline,
			     &state->alloc);

	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->resolve_compute.stencil_zero_pipeline,
			     &state->alloc);

	radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
					state->resolve_compute.ds_layout,
					&state->alloc);
	radv_DestroyPipelineLayout(radv_device_to_handle(device),
				   state->resolve_compute.p_layout,
				   &state->alloc);
}

static VkPipeline *
radv_get_resolve_pipeline(struct radv_cmd_buffer *cmd_buffer,
			  struct radv_image_view *src_iview)
{
	struct radv_device *device = cmd_buffer->device;
	struct radv_meta_state *state = &device->meta_state;
	uint32_t samples = src_iview->image->info.samples;
	uint32_t samples_log2 = ffs(samples) - 1;
	VkPipeline *pipeline;

	if (vk_format_is_int(src_iview->vk_format))
		pipeline = &state->resolve_compute.rc[samples_log2].i_pipeline;
	else if (vk_format_is_srgb(src_iview->vk_format))
		pipeline = &state->resolve_compute.rc[samples_log2].srgb_pipeline;
	else
		pipeline = &state->resolve_compute.rc[samples_log2].pipeline;

	if (!*pipeline) {
		VkResult ret;

		ret = create_resolve_pipeline(device, samples,
					      vk_format_is_int(src_iview->vk_format),
					      vk_format_is_srgb(src_iview->vk_format),
					      pipeline);
		if (ret != VK_SUCCESS) {
			cmd_buffer->record_result = ret;
			return NULL;
		}
	}

	return pipeline;
}

static void
emit_resolve(struct radv_cmd_buffer *cmd_buffer,
	     struct radv_image_view *src_iview,
	     struct radv_image_view *dest_iview,
	     const VkOffset2D *src_offset,
             const VkOffset2D *dest_offset,
             const VkExtent2D *resolve_extent)
{
	struct radv_device *device = cmd_buffer->device;
	VkPipeline *pipeline;

	radv_meta_push_descriptor_set(cmd_buffer,
				      VK_PIPELINE_BIND_POINT_COMPUTE,
				      device->meta_state.resolve_compute.p_layout,
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
					      .imageView = radv_image_view_to_handle(src_iview),
	                                      .imageLayout = VK_IMAGE_LAYOUT_GENERAL	                              },
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
                                     .imageView = radv_image_view_to_handle(dest_iview),
                                     .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                              },
                      }
			      }
				      });

	pipeline = radv_get_resolve_pipeline(cmd_buffer, src_iview);

	radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
			     VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline);

	unsigned push_constants[4] = {
		src_offset->x,
		src_offset->y,
		dest_offset->x,
		dest_offset->y,
	};
	radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
			      device->meta_state.resolve_compute.p_layout,
			      VK_SHADER_STAGE_COMPUTE_BIT, 0, 16,
			      push_constants);
	radv_unaligned_dispatch(cmd_buffer, resolve_extent->width, resolve_extent->height, 1);

}

static void
emit_depth_stencil_resolve(struct radv_cmd_buffer *cmd_buffer,
			   struct radv_image_view *src_iview,
			   struct radv_image_view *dest_iview,
			   const VkOffset2D *src_offset,
			   const VkOffset2D *dest_offset,
			   const VkExtent2D *resolve_extent,
			   VkImageAspectFlags aspects,
			   VkResolveModeFlagBits resolve_mode)
{
	struct radv_device *device = cmd_buffer->device;
	const uint32_t samples = src_iview->image->info.samples;
	const uint32_t samples_log2 = ffs(samples) - 1;
	VkPipeline *pipeline;

	radv_meta_push_descriptor_set(cmd_buffer,
				      VK_PIPELINE_BIND_POINT_COMPUTE,
				      device->meta_state.resolve_compute.p_layout,
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
					      .imageView = radv_image_view_to_handle(src_iview),
	                                      .imageLayout = VK_IMAGE_LAYOUT_GENERAL	                              },
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
                                     .imageView = radv_image_view_to_handle(dest_iview),
                                     .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
                              },
                      }
			      }
				      });

	switch (resolve_mode) {
	case VK_RESOLVE_MODE_SAMPLE_ZERO_BIT_KHR:
		if (aspects == VK_IMAGE_ASPECT_DEPTH_BIT)
			pipeline = &device->meta_state.resolve_compute.depth_zero_pipeline;
		else
			pipeline = &device->meta_state.resolve_compute.stencil_zero_pipeline;
		break;
	case VK_RESOLVE_MODE_AVERAGE_BIT_KHR:
		assert(aspects == VK_IMAGE_ASPECT_DEPTH_BIT);
		pipeline = &device->meta_state.resolve_compute.depth[samples_log2].average_pipeline;
		break;
	case VK_RESOLVE_MODE_MIN_BIT_KHR:
		if (aspects == VK_IMAGE_ASPECT_DEPTH_BIT)
			pipeline = &device->meta_state.resolve_compute.depth[samples_log2].min_pipeline;
		else
			pipeline = &device->meta_state.resolve_compute.stencil[samples_log2].min_pipeline;
		break;
	case VK_RESOLVE_MODE_MAX_BIT_KHR:
		if (aspects == VK_IMAGE_ASPECT_DEPTH_BIT)
			pipeline = &device->meta_state.resolve_compute.depth[samples_log2].max_pipeline;
		else
			pipeline = &device->meta_state.resolve_compute.stencil[samples_log2].max_pipeline;
		break;
	default:
		unreachable("invalid resolve mode");
	}

	if (!*pipeline) {
		int index = aspects == VK_IMAGE_ASPECT_DEPTH_BIT ? DEPTH_RESOLVE : STENCIL_RESOLVE;
		VkResult ret;

		ret = create_depth_stencil_resolve_pipeline(device, samples,
							    index, resolve_mode,
							    pipeline);
		if (ret != VK_SUCCESS) {
			cmd_buffer->record_result = ret;
			return;
		}
	}

	radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
			     VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline);

	unsigned push_constants[4] = {
		src_offset->x,
		src_offset->y,
		dest_offset->x,
		dest_offset->y,
	};
	radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
			      device->meta_state.resolve_compute.p_layout,
			      VK_SHADER_STAGE_COMPUTE_BIT, 0, 16,
			      push_constants);
	radv_unaligned_dispatch(cmd_buffer, resolve_extent->width, resolve_extent->height, 1);

}

void radv_meta_resolve_compute_image(struct radv_cmd_buffer *cmd_buffer,
				     struct radv_image *src_image,
				     VkFormat src_format,
				     VkImageLayout src_image_layout,
				     struct radv_image *dest_image,
				     VkFormat dest_format,
				     VkImageLayout dest_image_layout,
				     const VkImageResolve2KHR *region)
{
	struct radv_meta_saved_state saved_state;

	radv_decompress_resolve_src(cmd_buffer, src_image, src_image_layout,
				    region);

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_COMPUTE_PIPELINE |
		       RADV_META_SAVE_CONSTANTS |
		       RADV_META_SAVE_DESCRIPTORS);

	assert(region->srcSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
	assert(region->dstSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
	assert(region->srcSubresource.layerCount == region->dstSubresource.layerCount);

	const uint32_t src_base_layer =
		radv_meta_get_iview_layer(src_image, &region->srcSubresource,
					  &region->srcOffset);

	const uint32_t dest_base_layer =
		radv_meta_get_iview_layer(dest_image, &region->dstSubresource,
					  &region->dstOffset);

	const struct VkExtent3D extent =
		radv_sanitize_image_extent(src_image->type, region->extent);
	const struct VkOffset3D srcOffset =
		radv_sanitize_image_offset(src_image->type, region->srcOffset);
	const struct VkOffset3D dstOffset =
		radv_sanitize_image_offset(dest_image->type, region->dstOffset);

	for (uint32_t layer = 0; layer < region->srcSubresource.layerCount;
	     ++layer) {

		struct radv_image_view src_iview;
		radv_image_view_init(&src_iview, cmd_buffer->device,
				     &(VkImageViewCreateInfo) {
					     .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
						     .image = radv_image_to_handle(src_image),
						     .viewType = radv_meta_get_view_type(src_image),
						     .format = src_format,
						     .subresourceRange = {
						     .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						     .baseMipLevel = region->srcSubresource.mipLevel,
						     .levelCount = 1,
						     .baseArrayLayer = src_base_layer + layer,
						     .layerCount = 1,
					     },
				     }, NULL);

		struct radv_image_view dest_iview;
		radv_image_view_init(&dest_iview, cmd_buffer->device,
				     &(VkImageViewCreateInfo) {
					     .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
						     .image = radv_image_to_handle(dest_image),
						     .viewType = radv_meta_get_view_type(dest_image),
						     .format = vk_to_non_srgb_format(dest_format),
						     .subresourceRange = {
						     .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						     .baseMipLevel = region->dstSubresource.mipLevel,
						     .levelCount = 1,
						     .baseArrayLayer = dest_base_layer + layer,
						     .layerCount = 1,
					     },
				     }, NULL);

		emit_resolve(cmd_buffer,
			     &src_iview,
			     &dest_iview,
			     &(VkOffset2D) {srcOffset.x, srcOffset.y },
			     &(VkOffset2D) {dstOffset.x, dstOffset.y },
			     &(VkExtent2D) {extent.width, extent.height });
	}

	radv_meta_restore(&saved_state, cmd_buffer);
}

/**
 * Emit any needed resolves for the current subpass.
 */
void
radv_cmd_buffer_resolve_subpass_cs(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_framebuffer *fb = cmd_buffer->state.framebuffer;
	const struct radv_subpass *subpass = cmd_buffer->state.subpass;
	struct radv_subpass_barrier barrier;
	uint32_t layer_count = fb->layers;

	if (subpass->view_mask)
		layer_count = util_last_bit(subpass->view_mask);

	/* Resolves happen before the end-of-subpass barriers get executed, so
	 * we have to make the attachment shader-readable.
	 */
	barrier.src_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	barrier.src_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dst_access_mask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
	radv_subpass_barrier(cmd_buffer, &barrier);

	for (uint32_t i = 0; i < subpass->color_count; ++i) {
		struct radv_subpass_attachment src_att = subpass->color_attachments[i];
		struct radv_subpass_attachment dst_att = subpass->resolve_attachments[i];

		if (dst_att.attachment == VK_ATTACHMENT_UNUSED)
			continue;

		struct radv_image_view *src_iview = cmd_buffer->state.attachments[src_att.attachment].iview;
		struct radv_image_view *dst_iview = cmd_buffer->state.attachments[dst_att.attachment].iview;

		VkImageResolve2KHR region = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2_KHR,
			.extent = (VkExtent3D){ fb->width, fb->height, 0 },
			.srcSubresource = (VkImageSubresourceLayers) {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = src_iview->base_mip,
				.baseArrayLayer = src_iview->base_layer,
				.layerCount = layer_count,
			},
			.dstSubresource = (VkImageSubresourceLayers) {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.mipLevel = dst_iview->base_mip,
				.baseArrayLayer = dst_iview->base_layer,
				.layerCount = layer_count,
			},
			.srcOffset = (VkOffset3D){ 0, 0, 0 },
			.dstOffset = (VkOffset3D){ 0, 0, 0 },
		};

		radv_meta_resolve_compute_image(cmd_buffer,
						src_iview->image,
						src_iview->vk_format,
						src_att.layout,
						dst_iview->image,
						dst_iview->vk_format,
						dst_att.layout,
						&region);
	}

	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
	                                RADV_CMD_FLAG_INV_VCACHE;
}

void
radv_depth_stencil_resolve_subpass_cs(struct radv_cmd_buffer *cmd_buffer,
				      VkImageAspectFlags aspects,
				      VkResolveModeFlagBits resolve_mode)
{
	struct radv_framebuffer *fb = cmd_buffer->state.framebuffer;
	const struct radv_subpass *subpass = cmd_buffer->state.subpass;
	struct radv_meta_saved_state saved_state;
	struct radv_subpass_barrier barrier;
	uint32_t layer_count = fb->layers;

	if (subpass->view_mask)
		layer_count = util_last_bit(subpass->view_mask);

	/* Resolves happen before the end-of-subpass barriers get executed, so
	 * we have to make the attachment shader-readable.
	 */
	barrier.src_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	barrier.src_access_mask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
	barrier.dst_access_mask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
	radv_subpass_barrier(cmd_buffer, &barrier);

	radv_decompress_resolve_subpass_src(cmd_buffer);

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_COMPUTE_PIPELINE |
		       RADV_META_SAVE_CONSTANTS |
		       RADV_META_SAVE_DESCRIPTORS);

	struct radv_subpass_attachment src_att = *subpass->depth_stencil_attachment;
	struct radv_subpass_attachment dest_att = *subpass->ds_resolve_attachment;

	struct radv_image_view *src_iview =
		cmd_buffer->state.attachments[src_att.attachment].iview;
	struct radv_image_view *dst_iview =
		cmd_buffer->state.attachments[dest_att.attachment].iview;

	struct radv_image *src_image = src_iview->image;
	struct radv_image *dst_image = dst_iview->image;

	for (uint32_t layer = 0; layer < layer_count; layer++) {
		struct radv_image_view tsrc_iview;
		radv_image_view_init(&tsrc_iview, cmd_buffer->device,
				     &(VkImageViewCreateInfo) {
					.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					.image = radv_image_to_handle(src_image),
					.viewType = radv_meta_get_view_type(src_image),
					.format = src_iview->vk_format,
					.subresourceRange = {
						.aspectMask = aspects,
						.baseMipLevel = src_iview->base_mip,
						.levelCount = 1,
						.baseArrayLayer = src_iview->base_layer + layer,
						.layerCount = 1,
					},
				     }, NULL);

		struct radv_image_view tdst_iview;
		radv_image_view_init(&tdst_iview, cmd_buffer->device,
				     &(VkImageViewCreateInfo) {
					.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					.image = radv_image_to_handle(dst_image),
					.viewType = radv_meta_get_view_type(dst_image),
					.format = dst_iview->vk_format,
					.subresourceRange = {
						.aspectMask = aspects,
						.baseMipLevel = dst_iview->base_mip,
						.levelCount = 1,
						.baseArrayLayer = dst_iview->base_layer + layer,
						.layerCount = 1,
					},
				     }, NULL);

		emit_depth_stencil_resolve(cmd_buffer, &tsrc_iview, &tdst_iview,
					   &(VkOffset2D) { 0, 0 },
					   &(VkOffset2D) { 0, 0 },
					   &(VkExtent2D) { fb->width, fb->height },
					   aspects,
					   resolve_mode);
	}

	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
	                                RADV_CMD_FLAG_INV_VCACHE;

	if (radv_image_has_htile(dst_image)) {
		if (aspects == VK_IMAGE_ASPECT_DEPTH_BIT) {
			VkImageSubresourceRange range = {0};
			range.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
			range.baseMipLevel = dst_iview->base_mip;
			range.levelCount = 1;
			range.baseArrayLayer = dst_iview->base_layer;
			range.layerCount = layer_count;

			uint32_t clear_value = 0xfffc000f;

			if (vk_format_is_stencil(dst_image->vk_format) &&
			    subpass->stencil_resolve_mode != VK_RESOLVE_MODE_NONE_KHR) {
				/* Only clear the stencil part of the HTILE
				 * buffer if it's resolved, otherwise this
				 * might break if the stencil has been cleared.
				 */
				clear_value = 0xfffff30f;
			}

			cmd_buffer->state.flush_bits |=
				radv_clear_htile(cmd_buffer, dst_image, &range,
						 clear_value);
		}
	}

	radv_meta_restore(&saved_state, cmd_buffer);
}
