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

static nir_shader *
build_resolve_compute_shader(struct radv_device *dev, bool is_integer, int samples)
{
	nir_builder b;
	char name[64];
	nir_if *outer_if = NULL;
	const struct glsl_type *sampler_type = glsl_sampler_type(GLSL_SAMPLER_DIM_MS,
								 false,
								 false,
								 GLSL_TYPE_FLOAT);
	const struct glsl_type *img_type = glsl_sampler_type(GLSL_SAMPLER_DIM_2D,
							     false,
							     false,
							     GLSL_TYPE_FLOAT);
	snprintf(name, 64, "meta_resolve_cs-%d-%s", samples, is_integer ? "int" : "float");
	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info->name = ralloc_strdup(b.shader, name);
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

	nir_ssa_def *img_coord = nir_iadd(&b, global_id, &src_offset->dest.ssa);
	/* do a txf_ms on each sample */
	nir_ssa_def *tmp;

	nir_tex_instr *tex = nir_tex_instr_create(b.shader, 2);
	tex->sampler_dim = GLSL_SAMPLER_DIM_MS;
	tex->op = nir_texop_txf_ms;
	tex->src[0].src_type = nir_tex_src_coord;
	tex->src[0].src = nir_src_for_ssa(img_coord);
	tex->src[1].src_type = nir_tex_src_ms_index;
	tex->src[1].src = nir_src_for_ssa(nir_imm_int(&b, 0));
	tex->dest_type = nir_type_float;
	tex->is_array = false;
	tex->coord_components = 2;
	tex->texture = nir_deref_var_create(tex, input_img);
	tex->sampler = NULL;

	nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
	nir_builder_instr_insert(&b, &tex->instr);

	tmp = &tex->dest.ssa;
	nir_variable *color =
		nir_local_variable_create(b.impl, glsl_vec4_type(), "color");

	if (!is_integer && samples > 1) {
		nir_tex_instr *tex_all_same = nir_tex_instr_create(b.shader, 1);
		tex_all_same->sampler_dim = GLSL_SAMPLER_DIM_MS;
		tex_all_same->op = nir_texop_samples_identical;
		tex_all_same->src[0].src_type = nir_tex_src_coord;
		tex_all_same->src[0].src = nir_src_for_ssa(img_coord);
		tex_all_same->dest_type = nir_type_float;
		tex_all_same->is_array = false;
		tex_all_same->coord_components = 2;
		tex_all_same->texture = nir_deref_var_create(tex_all_same, input_img);
		tex_all_same->sampler = NULL;

		nir_ssa_dest_init(&tex_all_same->instr, &tex_all_same->dest, 1, 32, "tex");
		nir_builder_instr_insert(&b, &tex_all_same->instr);

		nir_ssa_def *all_same = nir_ine(&b, &tex_all_same->dest.ssa, nir_imm_int(&b, 0));
		nir_if *if_stmt = nir_if_create(b.shader);
		if_stmt->condition = nir_src_for_ssa(all_same);
		nir_cf_node_insert(b.cursor, &if_stmt->cf_node);

		b.cursor = nir_after_cf_list(&if_stmt->then_list);
		for (int i = 1; i < samples; i++) {
			nir_tex_instr *tex_add = nir_tex_instr_create(b.shader, 2);
			tex_add->sampler_dim = GLSL_SAMPLER_DIM_MS;
			tex_add->op = nir_texop_txf_ms;
			tex_add->src[0].src_type = nir_tex_src_coord;
			tex_add->src[0].src = nir_src_for_ssa(img_coord);
			tex_add->src[1].src_type = nir_tex_src_ms_index;
			tex_add->src[1].src = nir_src_for_ssa(nir_imm_int(&b, i));
			tex_add->dest_type = nir_type_float;
			tex_add->is_array = false;
			tex_add->coord_components = 2;
			tex_add->texture = nir_deref_var_create(tex_add, input_img);
			tex_add->sampler = NULL;

			nir_ssa_dest_init(&tex_add->instr, &tex_add->dest, 4, 32, "tex");
			nir_builder_instr_insert(&b, &tex_add->instr);

			tmp = nir_fadd(&b, tmp, &tex_add->dest.ssa);
		}

		tmp = nir_fdiv(&b, tmp, nir_imm_float(&b, samples));
		nir_store_var(&b, color, tmp, 0xf);
		b.cursor = nir_after_cf_list(&if_stmt->else_list);
		outer_if = if_stmt;
	}
	nir_store_var(&b, color, &tex->dest.ssa, 0xf);

	if (outer_if)
		b.cursor = nir_after_cf_node(&outer_if->cf_node);

	nir_ssa_def *newv = nir_load_var(&b, color);
	nir_ssa_def *coord = nir_iadd(&b, global_id, &dst_offset->dest.ssa);
	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_image_store);
	store->src[0] = nir_src_for_ssa(coord);
	store->src[1] = nir_src_for_ssa(nir_ssa_undef(&b, 1, 32));
	store->src[2] = nir_src_for_ssa(newv);
	store->variables[0] = nir_deref_var_create(store, output_img);
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
			VkPipeline *pipeline)
{
	VkResult result;
	struct radv_shader_module cs = { .nir = NULL };

	cs.nir = build_resolve_compute_shader(device, is_integer, samples);

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
	return VK_SUCCESS;
fail:
	ralloc_free(cs.nir);
	return result;
}

VkResult
radv_device_init_meta_resolve_compute_state(struct radv_device *device)
{
	struct radv_meta_state *state = &device->meta_state;
	VkResult res;
	memset(&device->meta_state.resolve_compute, 0, sizeof(device->meta_state.resolve_compute));

	res = create_layout(device);
	if (res != VK_SUCCESS)
		return res;

	for (uint32_t i = 0; i < MAX_SAMPLES_LOG2; ++i) {
		uint32_t samples = 1 << i;

		res = create_resolve_pipeline(device, samples, false,
					      &state->resolve_compute.rc[i].pipeline);

		res = create_resolve_pipeline(device, samples, true,
					      &state->resolve_compute.rc[i].i_pipeline);

	}

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
	}

	radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
					state->resolve_compute.ds_layout,
					&state->alloc);
	radv_DestroyPipelineLayout(radv_device_to_handle(device),
				   state->resolve_compute.p_layout,
				   &state->alloc);
}

void radv_meta_resolve_compute_image(struct radv_cmd_buffer *cmd_buffer,
				     struct radv_image *src_image,
				     VkImageLayout src_image_layout,
				     struct radv_image *dest_image,
				     VkImageLayout dest_image_layout,
				     uint32_t region_count,
				     const VkImageResolve *regions)
{
	struct radv_device *device = cmd_buffer->device;
	struct radv_meta_saved_compute_state saved_state;
	const uint32_t samples = src_image->samples;
	const uint32_t samples_log2 = ffs(samples) - 1;
	radv_meta_save_compute(&saved_state, cmd_buffer, 16);

	for (uint32_t r = 0; r < region_count; ++r) {
		const VkImageResolve *region = &regions[r];

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
			VkDescriptorSet set;
			radv_image_view_init(&src_iview, cmd_buffer->device,
					     &(VkImageViewCreateInfo) {
						     .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
							     .image = radv_image_to_handle(src_image),
							     .viewType = radv_meta_get_view_type(src_image),
							     .format = src_image->vk_format,
							     .subresourceRange = {
							     .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							     .baseMipLevel = region->srcSubresource.mipLevel,
							     .levelCount = 1,
							     .baseArrayLayer = src_base_layer + layer,
							     .layerCount = 1,
						     },
					     },
					     cmd_buffer, VK_IMAGE_USAGE_SAMPLED_BIT);

			struct radv_image_view dest_iview;
			radv_image_view_init(&dest_iview, cmd_buffer->device,
					     &(VkImageViewCreateInfo) {
						     .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
							     .image = radv_image_to_handle(dest_image),
							     .viewType = radv_meta_get_view_type(dest_image),
							     .format = dest_image->vk_format,
							     .subresourceRange = {
							     .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							     .baseMipLevel = region->dstSubresource.mipLevel,
							     .levelCount = 1,
							     .baseArrayLayer = dest_base_layer + layer,
							     .layerCount = 1,
						     },
							     },
					     cmd_buffer, VK_IMAGE_USAGE_STORAGE_BIT);


			radv_temp_descriptor_set_create(device, cmd_buffer,
							device->meta_state.resolve_compute.ds_layout,
							&set);

			radv_UpdateDescriptorSets(radv_device_to_handle(device),
						  2, /* writeCount */
						  (VkWriteDescriptorSet[]) {
						  {
						  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						  .dstSet = set,
						  .dstBinding = 0,
						  .dstArrayElement = 0,
						  .descriptorCount = 1,
						  .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
						  .pImageInfo = (VkDescriptorImageInfo[]) {
							  {
								  .sampler = VK_NULL_HANDLE,
								  .imageView = radv_image_view_to_handle(&src_iview),
								  .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
							  },
						  }
					  },
					  {
						  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
						  .dstSet = set,
						  .dstBinding = 1,
						  .dstArrayElement = 0,
						  .descriptorCount = 1,
						  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
						  .pImageInfo = (VkDescriptorImageInfo[]) {
							  {
								  .sampler = VK_NULL_HANDLE,
								  .imageView = radv_image_view_to_handle(&dest_iview),
								  .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
							  },
						  }
					  }
				  }, 0, NULL);

			radv_CmdBindDescriptorSets(radv_cmd_buffer_to_handle(cmd_buffer),
						   VK_PIPELINE_BIND_POINT_COMPUTE,
						   device->meta_state.resolve_compute.p_layout, 0, 1,
						   &set, 0, NULL);

			VkPipeline pipeline;
			if (vk_format_is_int(src_image->vk_format))
				pipeline = device->meta_state.resolve_compute.rc[samples_log2].i_pipeline;
			else
				pipeline = device->meta_state.resolve_compute.rc[samples_log2].pipeline;
			if (cmd_buffer->state.compute_pipeline != radv_pipeline_from_handle(pipeline)) {
				radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
						     VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
			}

			unsigned push_constants[4] = {
				srcOffset.x,
				srcOffset.y,
				dstOffset.x,
				dstOffset.y,
			};
			radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
					      device->meta_state.resolve_compute.p_layout,
					      VK_SHADER_STAGE_COMPUTE_BIT, 0, 16,
					      push_constants);
			radv_unaligned_dispatch(cmd_buffer, extent.width, extent.height, 1);
			radv_temp_descriptor_set_destroy(cmd_buffer->device, set);
		}
	}
	radv_meta_restore_compute(&saved_state, cmd_buffer, 16);
}
