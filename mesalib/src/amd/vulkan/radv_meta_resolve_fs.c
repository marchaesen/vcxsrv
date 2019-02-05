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
build_nir_vertex_shader(void)
{
	const struct glsl_type *vec4 = glsl_vec4_type();
	nir_builder b;

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_VERTEX, NULL);
	b.shader->info.name = ralloc_strdup(b.shader, "meta_resolve_vs");

	nir_variable *pos_out = nir_variable_create(b.shader, nir_var_shader_out,
						    vec4, "gl_Position");
	pos_out->data.location = VARYING_SLOT_POS;

	nir_ssa_def *outvec = radv_meta_gen_rect_vertices(&b);

	nir_store_var(&b, pos_out, outvec, 0xf);
	return b.shader;
}

static nir_shader *
build_resolve_fragment_shader(struct radv_device *dev, bool is_integer, int samples)
{
	nir_builder b;
	char name[64];
	const struct glsl_type *vec2 = glsl_vector_type(GLSL_TYPE_FLOAT, 2);
	const struct glsl_type *vec4 = glsl_vec4_type();
	const struct glsl_type *sampler_type = glsl_sampler_type(GLSL_SAMPLER_DIM_MS,
								 false,
								 false,
								 GLSL_TYPE_FLOAT);

	snprintf(name, 64, "meta_resolve_fs-%d-%s", samples, is_integer ? "int" : "float");
	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_FRAGMENT, NULL);
	b.shader->info.name = ralloc_strdup(b.shader, name);

	nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform,
						      sampler_type, "s_tex");
	input_img->data.descriptor_set = 0;
	input_img->data.binding = 0;

	nir_variable *fs_pos_in = nir_variable_create(b.shader, nir_var_shader_in, vec2, "fs_pos_in");
	fs_pos_in->data.location = VARYING_SLOT_POS;

	nir_variable *color_out = nir_variable_create(b.shader, nir_var_shader_out,
						      vec4, "f_color");
	color_out->data.location = FRAG_RESULT_DATA0;

	nir_ssa_def *pos_in = nir_load_var(&b, fs_pos_in);
	nir_intrinsic_instr *src_offset = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	nir_intrinsic_set_base(src_offset, 0);
	nir_intrinsic_set_range(src_offset, 8);
	src_offset->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	src_offset->num_components = 2;
	nir_ssa_dest_init(&src_offset->instr, &src_offset->dest, 2, 32, "src_offset");
	nir_builder_instr_insert(&b, &src_offset->instr);

	nir_ssa_def *pos_int = nir_f2i32(&b, pos_in);

	nir_ssa_def *img_coord = nir_channels(&b, nir_iadd(&b, pos_int, &src_offset->dest.ssa), 0x3);
	nir_variable *color = nir_local_variable_create(b.impl, glsl_vec4_type(), "color");

	radv_meta_build_resolve_shader_core(&b, is_integer, samples, input_img,
	                                    color, img_coord);

	nir_ssa_def *outval = nir_load_var(&b, color);
	nir_store_var(&b, color_out, outval, 0xf);
	return b.shader;
}


static VkResult
create_layout(struct radv_device *device)
{
	VkResult result;
	/*
	 * one descriptors for the image being sampled
	 */
	VkDescriptorSetLayoutCreateInfo ds_create_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
		.bindingCount = 1,
		.pBindings = (VkDescriptorSetLayoutBinding[]) {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				.pImmutableSamplers = NULL
			},
		}
	};

	result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device),
						&ds_create_info,
						&device->meta_state.alloc,
						&device->meta_state.resolve_fragment.ds_layout);
	if (result != VK_SUCCESS)
		goto fail;


	VkPipelineLayoutCreateInfo pl_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &device->meta_state.resolve_fragment.ds_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8},
	};

	result = radv_CreatePipelineLayout(radv_device_to_handle(device),
					  &pl_create_info,
					  &device->meta_state.alloc,
					  &device->meta_state.resolve_fragment.p_layout);
	if (result != VK_SUCCESS)
		goto fail;
	return VK_SUCCESS;
fail:
	return result;
}

static const VkPipelineVertexInputStateCreateInfo normal_vi_create_info = {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	.vertexBindingDescriptionCount = 0,
	.vertexAttributeDescriptionCount = 0,
};

static VkResult
create_resolve_pipeline(struct radv_device *device,
			int samples_log2,
			VkFormat format)
{
	mtx_lock(&device->meta_state.mtx);

	unsigned fs_key = radv_format_meta_fs_key(format);
	VkPipeline *pipeline = &device->meta_state.resolve_fragment.rc[samples_log2].pipeline[fs_key];
	if (*pipeline) {
		mtx_unlock(&device->meta_state.mtx);
		return VK_SUCCESS;
	}

	VkResult result;
	bool is_integer = false;
	uint32_t samples = 1 << samples_log2;
	const VkPipelineVertexInputStateCreateInfo *vi_create_info;
	vi_create_info = &normal_vi_create_info;
	if (vk_format_is_int(format))
		is_integer = true;

	struct radv_shader_module fs = { .nir = NULL };
	fs.nir = build_resolve_fragment_shader(device, is_integer, samples);
	struct radv_shader_module vs = {
		.nir = build_nir_vertex_shader(),
	};

	VkRenderPass *rp = &device->meta_state.resolve_fragment.rc[samples_log2].render_pass[fs_key][0];

	assert(!*rp);

	VkPipelineShaderStageCreateInfo pipeline_shader_stages[] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = radv_shader_module_to_handle(&vs),
			.pName = "main",
			.pSpecializationInfo = NULL
		}, {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = radv_shader_module_to_handle(&fs),
			.pName = "main",
			.pSpecializationInfo = NULL
		},
	};


	for (unsigned dst_layout = 0; dst_layout < RADV_META_DST_LAYOUT_COUNT; ++dst_layout) {
		VkImageLayout layout = radv_meta_dst_layout_to_layout(dst_layout);
		result = radv_CreateRenderPass(radv_device_to_handle(device),
					&(VkRenderPassCreateInfo) {
						.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
						.attachmentCount = 1,
						.pAttachments = &(VkAttachmentDescription) {
							.format = format,
							.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
							.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
							.initialLayout = layout,
							.finalLayout = layout,
						},
						.subpassCount = 1,
						.pSubpasses = &(VkSubpassDescription) {
							.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
							.inputAttachmentCount = 0,
							.colorAttachmentCount = 1,
							.pColorAttachments = &(VkAttachmentReference) {
								.attachment = 0,
								.layout = layout,
							},
						.pResolveAttachments = NULL,
						.pDepthStencilAttachment = &(VkAttachmentReference) {
							.attachment = VK_ATTACHMENT_UNUSED,
							.layout = VK_IMAGE_LAYOUT_GENERAL,
						},
						.preserveAttachmentCount = 0,
						.pPreserveAttachments = NULL,
					},
					.dependencyCount = 0,
				}, &device->meta_state.alloc, rp + dst_layout);
	}


	const VkGraphicsPipelineCreateInfo vk_pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = ARRAY_SIZE(pipeline_shader_stages),
		.pStages = pipeline_shader_stages,
		.pVertexInputState = vi_create_info,
		.pInputAssemblyState = &(VkPipelineInputAssemblyStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
			.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
			.primitiveRestartEnable = false,
		},
		.pViewportState = &(VkPipelineViewportStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
			.viewportCount = 1,
			.scissorCount = 1,
		},
		.pRasterizationState = &(VkPipelineRasterizationStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
			.rasterizerDiscardEnable = false,
			.polygonMode = VK_POLYGON_MODE_FILL,
			.cullMode = VK_CULL_MODE_NONE,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE
		},
		.pMultisampleState = &(VkPipelineMultisampleStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = 1,
			.sampleShadingEnable = false,
			.pSampleMask = (VkSampleMask[]) { UINT32_MAX },
		},
		.pColorBlendState = &(VkPipelineColorBlendStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.attachmentCount = 1,
			.pAttachments = (VkPipelineColorBlendAttachmentState []) {
				{ .colorWriteMask =
				  VK_COLOR_COMPONENT_A_BIT |
				  VK_COLOR_COMPONENT_R_BIT |
				  VK_COLOR_COMPONENT_G_BIT |
				  VK_COLOR_COMPONENT_B_BIT },
			}
		},
		.pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = 9,
			.pDynamicStates = (VkDynamicState[]) {
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR,
				VK_DYNAMIC_STATE_LINE_WIDTH,
				VK_DYNAMIC_STATE_DEPTH_BIAS,
				VK_DYNAMIC_STATE_BLEND_CONSTANTS,
				VK_DYNAMIC_STATE_DEPTH_BOUNDS,
				VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
				VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
				VK_DYNAMIC_STATE_STENCIL_REFERENCE,
			},
		},
		.flags = 0,
		.layout = device->meta_state.resolve_fragment.p_layout,
		.renderPass = *rp,
		.subpass = 0,
	};

	const struct radv_graphics_pipeline_create_info radv_pipeline_info = {
		.use_rectlist = true
	};

	result = radv_graphics_pipeline_create(radv_device_to_handle(device),
					       radv_pipeline_cache_to_handle(&device->meta_state.cache),
					       &vk_pipeline_info, &radv_pipeline_info,
					       &device->meta_state.alloc,
					       pipeline);
	ralloc_free(vs.nir);
	ralloc_free(fs.nir);

	mtx_unlock(&device->meta_state.mtx);
	return result;
}

VkResult
radv_device_init_meta_resolve_fragment_state(struct radv_device *device, bool on_demand)
{
	VkResult res;

	res = create_layout(device);
	if (res != VK_SUCCESS)
		goto fail;

	if (on_demand)
		return VK_SUCCESS;

	for (uint32_t i = 0; i < MAX_SAMPLES_LOG2; ++i) {
		for (unsigned j = 0; j < NUM_META_FS_KEYS; ++j) {
			res = create_resolve_pipeline(device, i, radv_fs_key_format_exemplars[j]);
			if (res != VK_SUCCESS)
				goto fail;
		}
	}

	return VK_SUCCESS;
fail:
	radv_device_finish_meta_resolve_fragment_state(device);
	return res;
}

void
radv_device_finish_meta_resolve_fragment_state(struct radv_device *device)
{
	struct radv_meta_state *state = &device->meta_state;
	for (uint32_t i = 0; i < MAX_SAMPLES_LOG2; ++i) {
		for (unsigned j = 0; j < NUM_META_FS_KEYS; ++j) {
			for(unsigned k =0; k < RADV_META_DST_LAYOUT_COUNT; ++k) {
				radv_DestroyRenderPass(radv_device_to_handle(device),
				                       state->resolve_fragment.rc[i].render_pass[j][k],
				                       &state->alloc);
			}
			radv_DestroyPipeline(radv_device_to_handle(device),
					     state->resolve_fragment.rc[i].pipeline[j],
					     &state->alloc);
		}
	}

	radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
					state->resolve_fragment.ds_layout,
					&state->alloc);
	radv_DestroyPipelineLayout(radv_device_to_handle(device),
				   state->resolve_fragment.p_layout,
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
	VkCommandBuffer cmd_buffer_h = radv_cmd_buffer_to_handle(cmd_buffer);
	const uint32_t samples = src_iview->image->info.samples;
	const uint32_t samples_log2 = ffs(samples) - 1;
	radv_meta_push_descriptor_set(cmd_buffer,
				      VK_PIPELINE_BIND_POINT_GRAPHICS,
				      cmd_buffer->device->meta_state.resolve_fragment.p_layout,
				      0, /* set */
				      1, /* descriptorWriteCount */
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
						      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
						      },
						      }
					      },
				      });

	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB;

	unsigned push_constants[2] = {
		src_offset->x - dest_offset->x,
		src_offset->y - dest_offset->y,
	};
	radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
			      device->meta_state.resolve_fragment.p_layout,
			      VK_SHADER_STAGE_FRAGMENT_BIT, 0, 8,
			      push_constants);

	unsigned fs_key = radv_format_meta_fs_key(dest_iview->vk_format);
	VkPipeline* pipeline = &device->meta_state.resolve_fragment.rc[samples_log2].pipeline[fs_key];

	if (*pipeline == VK_NULL_HANDLE) {
		VkResult ret = create_resolve_pipeline(device, samples_log2, radv_fs_key_format_exemplars[fs_key]);
		if (ret != VK_SUCCESS) {
			cmd_buffer->record_result = ret;
			return;
		}
	}

	radv_CmdBindPipeline(cmd_buffer_h, VK_PIPELINE_BIND_POINT_GRAPHICS,
			     *pipeline);

	radv_CmdSetViewport(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1, &(VkViewport) {
		.x = dest_offset->x,
		.y = dest_offset->y,
		.width = resolve_extent->width,
		.height = resolve_extent->height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	});

	radv_CmdSetScissor(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1, &(VkRect2D) {
		.offset = *dest_offset,
		.extent = *resolve_extent,
	});

	radv_CmdDraw(cmd_buffer_h, 3, 1, 0, 0);
	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB;
}

void radv_meta_resolve_fragment_image(struct radv_cmd_buffer *cmd_buffer,
				      struct radv_image *src_image,
				      VkImageLayout src_image_layout,
				      struct radv_image *dest_image,
				      VkImageLayout dest_image_layout,
				      uint32_t region_count,
				      const VkImageResolve *regions)
{
	struct radv_device *device = cmd_buffer->device;
	struct radv_meta_saved_state saved_state;
	const uint32_t samples = src_image->info.samples;
	const uint32_t samples_log2 = ffs(samples) - 1;
	unsigned fs_key = radv_format_meta_fs_key(dest_image->vk_format);
	unsigned dst_layout = radv_meta_dst_layout_from_layout(dest_image_layout);
	VkRenderPass rp;

	radv_decompress_resolve_src(cmd_buffer, src_image, src_image_layout,
				    region_count, regions);

	if (!device->meta_state.resolve_fragment.rc[samples_log2].render_pass[fs_key][dst_layout]) {
		VkResult ret = create_resolve_pipeline(device, samples_log2, radv_fs_key_format_exemplars[fs_key]);
		if (ret != VK_SUCCESS) {
			cmd_buffer->record_result = ret;
			return;
		}
	}

	rp = device->meta_state.resolve_fragment.rc[samples_log2].render_pass[fs_key][dst_layout];

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_GRAPHICS_PIPELINE |
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
							     .format = dest_image->vk_format,
							     .subresourceRange = {
							     .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
							     .baseMipLevel = region->dstSubresource.mipLevel,
							     .levelCount = 1,
							     .baseArrayLayer = dest_base_layer + layer,
							     .layerCount = 1,
						     },
					     });


			VkFramebuffer fb;
			radv_CreateFramebuffer(radv_device_to_handle(cmd_buffer->device),
			       &(VkFramebufferCreateInfo) {
				       .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
					       .attachmentCount = 1,
					       .pAttachments = (VkImageView[]) {
					       radv_image_view_to_handle(&dest_iview),
				       },
				       .width = extent.width + dstOffset.x,
				       .height = extent.height + dstOffset.y,
				       .layers = 1
				}, &cmd_buffer->pool->alloc, &fb);

			radv_CmdBeginRenderPass(radv_cmd_buffer_to_handle(cmd_buffer),
						&(VkRenderPassBeginInfo) {
							.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
								.renderPass = rp,
								.framebuffer = fb,
								.renderArea = {
								.offset = { dstOffset.x, dstOffset.y, },
								.extent = { extent.width, extent.height },
							},
								.clearValueCount = 0,
								.pClearValues = NULL,
						}, VK_SUBPASS_CONTENTS_INLINE);



			emit_resolve(cmd_buffer,
				     &src_iview,
				     &dest_iview,
				     &(VkOffset2D) { srcOffset.x, srcOffset.y },
				     &(VkOffset2D) { dstOffset.x, dstOffset.y },
				     &(VkExtent2D) { extent.width, extent.height });

			radv_CmdEndRenderPass(radv_cmd_buffer_to_handle(cmd_buffer));

			radv_DestroyFramebuffer(radv_device_to_handle(cmd_buffer->device), fb, &cmd_buffer->pool->alloc);
		}
	}

	radv_meta_restore(&saved_state, cmd_buffer);
}


/**
 * Emit any needed resolves for the current subpass.
 */
void
radv_cmd_buffer_resolve_subpass_fs(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_framebuffer *fb = cmd_buffer->state.framebuffer;
	const struct radv_subpass *subpass = cmd_buffer->state.subpass;
	struct radv_meta_saved_state saved_state;
	struct radv_subpass_barrier barrier;

	/* Resolves happen before the end-of-subpass barriers get executed,
	 * so we have to make the attachment shader-readable */
	barrier.src_stage_mask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	barrier.src_access_mask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
	barrier.dst_access_mask = VK_ACCESS_INPUT_ATTACHMENT_READ_BIT;
	radv_subpass_barrier(cmd_buffer, &barrier);

	radv_decompress_resolve_subpass_src(cmd_buffer);

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_GRAPHICS_PIPELINE |
		       RADV_META_SAVE_CONSTANTS |
		       RADV_META_SAVE_DESCRIPTORS);

	for (uint32_t i = 0; i < subpass->color_count; ++i) {
		struct radv_subpass_attachment src_att = subpass->color_attachments[i];
		struct radv_subpass_attachment dest_att = subpass->resolve_attachments[i];

		if (dest_att.attachment == VK_ATTACHMENT_UNUSED)
			continue;

		struct radv_image_view *dest_iview = cmd_buffer->state.framebuffer->attachments[dest_att.attachment].attachment;
		struct radv_image_view *src_iview = cmd_buffer->state.framebuffer->attachments[src_att.attachment].attachment;

		struct radv_subpass resolve_subpass = {
			.color_count = 1,
			.color_attachments = (struct radv_subpass_attachment[]) { dest_att },
			.depth_stencil_attachment = NULL,
		};

		radv_cmd_buffer_set_subpass(cmd_buffer, &resolve_subpass);

		emit_resolve(cmd_buffer,
			     src_iview,
			     dest_iview,
			     &(VkOffset2D) { 0, 0 },
			     &(VkOffset2D) { 0, 0 },
			     &(VkExtent2D) { fb->width, fb->height });
	}

	cmd_buffer->state.subpass = subpass;
	radv_meta_restore(&saved_state, cmd_buffer);
}
