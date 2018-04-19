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
	nir_const_value v;
	unsigned i;
	v.u32[0] = 0x3b4d2e1c; // 0.00313080009

	nir_ssa_def *cmp[3];
	for (i = 0; i < 3; i++)
		cmp[i] = nir_flt(b, nir_channel(b, input, i),
				 nir_build_imm(b, 1, 32, v));

	nir_ssa_def *ltvals[3];
	v.f32[0] = 12.92;
	for (i = 0; i < 3; i++)
		ltvals[i] = nir_fmul(b, nir_channel(b, input, i),
				     nir_build_imm(b, 1, 32, v));

	nir_ssa_def *gtvals[3];

	for (i = 0; i < 3; i++) {
		v.f32[0] = 1.0/2.4;
		gtvals[i] = nir_fpow(b, nir_channel(b, input, i),
				     nir_build_imm(b, 1, 32, v));
		v.f32[0] = 1.055;
		gtvals[i] = nir_fmul(b, gtvals[i],
				     nir_build_imm(b, 1, 32, v));
		v.f32[0] = 0.055;
		gtvals[i] = nir_fsub(b, gtvals[i],
				     nir_build_imm(b, 1, 32, v));
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
	const struct glsl_type *img_type = glsl_sampler_type(GLSL_SAMPLER_DIM_2D,
							     false,
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
	nir_ssa_def *invoc_id = nir_load_system_value(&b, nir_intrinsic_load_local_invocation_id, 0);
	nir_ssa_def *wg_id = nir_load_system_value(&b, nir_intrinsic_load_work_group_id, 0);
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
	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_image_var_store);
	store->src[0] = nir_src_for_ssa(coord);
	store->src[1] = nir_src_for_ssa(nir_ssa_undef(&b, 1, 32));
	store->src[2] = nir_src_for_ssa(outval);
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

	res = create_layout(device);
	if (res != VK_SUCCESS)
		goto fail;

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

	}

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
	}

	radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
					state->resolve_compute.ds_layout,
					&state->alloc);
	radv_DestroyPipelineLayout(radv_device_to_handle(device),
				   state->resolve_compute.p_layout,
				   &state->alloc);
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
	const uint32_t samples = src_iview->image->info.samples;
	const uint32_t samples_log2 = ffs(samples) - 1;
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

	VkPipeline pipeline;
	if (vk_format_is_int(src_iview->image->vk_format))
		pipeline = device->meta_state.resolve_compute.rc[samples_log2].i_pipeline;
	else if (vk_format_is_srgb(src_iview->image->vk_format))
		pipeline = device->meta_state.resolve_compute.rc[samples_log2].srgb_pipeline;
	else
		pipeline = device->meta_state.resolve_compute.rc[samples_log2].pipeline;

	radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
			     VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);

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
				     VkImageLayout src_image_layout,
				     struct radv_image *dest_image,
				     VkImageLayout dest_image_layout,
				     uint32_t region_count,
				     const VkImageResolve *regions)
{
	struct radv_meta_saved_state saved_state;

	radv_decompress_resolve_src(cmd_buffer, src_image, src_image_layout,
				    region_count, regions);

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_COMPUTE_PIPELINE |
		       RADV_META_SAVE_CONSTANTS |
		       RADV_META_SAVE_DESCRIPTORS);

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
					     });

			struct radv_image_view dest_iview;
			radv_image_view_init(&dest_iview, cmd_buffer->device,
					     &(VkImageViewCreateInfo) {
						     .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
							     .image = radv_image_to_handle(dest_image),
							     .viewType = radv_meta_get_view_type(dest_image),
							     .format = vk_to_non_srgb_format(dest_image->vk_format),
							     .subresourceRange = {
							     .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							     .baseMipLevel = region->dstSubresource.mipLevel,
							     .levelCount = 1,
							     .baseArrayLayer = dest_base_layer + layer,
							     .layerCount = 1,
						     },
					     });

			emit_resolve(cmd_buffer,
				     &src_iview,
				     &dest_iview,
				     &(VkOffset2D) {srcOffset.x, srcOffset.y },
				     &(VkOffset2D) {dstOffset.x, dstOffset.y },
				     &(VkExtent2D) {extent.width, extent.height });
		}
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
	struct radv_meta_saved_state saved_state;
	/* FINISHME(perf): Skip clears for resolve attachments.
	 *
	 * From the Vulkan 1.0 spec:
	 *
	 *    If the first use of an attachment in a render pass is as a resolve
	 *    attachment, then the loadOp is effectively ignored as the resolve is
	 *    guaranteed to overwrite all pixels in the render area.
	 */

	if (!subpass->has_resolve)
		return;

	/* Resolves happen before the end-of-subpass barriers get executed,
	 * so we have to make the attachment shader-readable */
	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_PS_PARTIAL_FLUSH |
	                                RADV_CMD_FLAG_FLUSH_AND_INV_CB |
	                                RADV_CMD_FLAG_FLUSH_AND_INV_CB_META |
	                                RADV_CMD_FLAG_INV_GLOBAL_L2 |
	                                RADV_CMD_FLAG_INV_VMEM_L1;

	radv_decompress_resolve_subpass_src(cmd_buffer);

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_COMPUTE_PIPELINE |
		       RADV_META_SAVE_CONSTANTS |
		       RADV_META_SAVE_DESCRIPTORS);

	for (uint32_t i = 0; i < subpass->color_count; ++i) {
		VkAttachmentReference src_att = subpass->color_attachments[i];
		VkAttachmentReference dest_att = subpass->resolve_attachments[i];
		struct radv_image_view *src_iview = cmd_buffer->state.framebuffer->attachments[src_att.attachment].attachment;
		struct radv_image_view *dst_iview = cmd_buffer->state.framebuffer->attachments[dest_att.attachment].attachment;
		if (dest_att.attachment == VK_ATTACHMENT_UNUSED)
			continue;

		emit_resolve(cmd_buffer,
			     src_iview,
			     dst_iview,
			     &(VkOffset2D) { 0, 0 },
			     &(VkOffset2D) { 0, 0 },
			     &(VkExtent2D) { fb->width, fb->height });
	}

	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
	                                RADV_CMD_FLAG_INV_VMEM_L1;

	radv_meta_restore(&saved_state, cmd_buffer);
}
