/*
 * Copyright © 2016 Intel Corporation
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
#include "sid.h"

enum radv_depth_op {
	DEPTH_DECOMPRESS,
	DEPTH_RESUMMARIZE,
};

enum radv_depth_decompress {
	DECOMPRESS_DEPTH_STENCIL,
	DECOMPRESS_DEPTH,
	DECOMPRESS_STENCIL,
};

static VkResult
create_pass(struct radv_device *device,
	    uint32_t samples,
	    VkRenderPass *pass)
{
	VkResult result;
	VkDevice device_h = radv_device_to_handle(device);
	const VkAllocationCallbacks *alloc = &device->meta_state.alloc;
	VkAttachmentDescription attachment;

	attachment.flags = 0;
	attachment.format = VK_FORMAT_D32_SFLOAT_S8_UINT;
	attachment.samples = samples;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
	attachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

	result = radv_CreateRenderPass(device_h,
				       &(VkRenderPassCreateInfo) {
					       .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
						       .attachmentCount = 1,
						       .pAttachments = &attachment,
						       .subpassCount = 1,
							.pSubpasses = &(VkSubpassDescription) {
						       .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
						       .inputAttachmentCount = 0,
						       .colorAttachmentCount = 0,
						       .pColorAttachments = NULL,
						       .pResolveAttachments = NULL,
						       .pDepthStencilAttachment = &(VkAttachmentReference) {
							       .attachment = 0,
							       .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
						       },
						       .preserveAttachmentCount = 0,
						       .pPreserveAttachments = NULL,
					       },
							.dependencyCount = 2,
							.pDependencies = (VkSubpassDependency[]) {
								{
									.srcSubpass = VK_SUBPASS_EXTERNAL,
									.dstSubpass = 0,
									.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
									.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
									.srcAccessMask = 0,
									.dstAccessMask = 0,
									.dependencyFlags = 0
								},
								{
									.srcSubpass = 0,
									.dstSubpass = VK_SUBPASS_EXTERNAL,
									.srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
									.dstStageMask = VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
									.srcAccessMask = 0,
									.dstAccessMask = 0,
									.dependencyFlags = 0
								}
							},
								   },
				       alloc,
				       pass);

	return result;
}

static VkResult
create_pipeline_layout(struct radv_device *device, VkPipelineLayout *layout)
{
	VkPipelineLayoutCreateInfo pl_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 0,
		.pSetLayouts = NULL,
		.pushConstantRangeCount = 0,
		.pPushConstantRanges = NULL,
	};

	return radv_CreatePipelineLayout(radv_device_to_handle(device),
					 &pl_create_info,
					 &device->meta_state.alloc,
					 layout);
}

static VkResult
create_pipeline(struct radv_device *device,
		uint32_t samples,
		VkRenderPass pass,
		VkPipelineLayout layout,
		enum radv_depth_op op,
		enum radv_depth_decompress decompress,
		VkPipeline *pipeline)
{
	VkResult result;
	VkDevice device_h = radv_device_to_handle(device);

	mtx_lock(&device->meta_state.mtx);
	if (*pipeline) {
		mtx_unlock(&device->meta_state.mtx);
		return VK_SUCCESS;
	}

	struct radv_shader_module vs_module = {
		.nir = radv_meta_build_nir_vs_generate_vertices()
	};

	if (!vs_module.nir) {
		/* XXX: Need more accurate error */
		result = VK_ERROR_OUT_OF_HOST_MEMORY;
		goto cleanup;
	}

	struct radv_shader_module fs_module = {
		.nir = radv_meta_build_nir_fs_noop(),
	};

	if (!fs_module.nir) {
		/* XXX: Need more accurate error */
		result = VK_ERROR_OUT_OF_HOST_MEMORY;
		goto cleanup;
	}

	const VkPipelineSampleLocationsStateCreateInfoEXT sample_locs_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SAMPLE_LOCATIONS_STATE_CREATE_INFO_EXT,
		.sampleLocationsEnable = false,
	};

	const VkGraphicsPipelineCreateInfo pipeline_create_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = (VkPipelineShaderStageCreateInfo[]) {
		       {
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_VERTEX_BIT,
				.module = radv_shader_module_to_handle(&vs_module),
				.pName = "main",
			},
			{
				.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
				.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
				.module = radv_shader_module_to_handle(&fs_module),
				.pName = "main",
			},
		},
		.pVertexInputState = &(VkPipelineVertexInputStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
			.vertexBindingDescriptionCount = 0,
			.vertexAttributeDescriptionCount = 0,
		},
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
			.depthClampEnable = false,
			.rasterizerDiscardEnable = false,
			.polygonMode = VK_POLYGON_MODE_FILL,
			.cullMode = VK_CULL_MODE_NONE,
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
		},
		.pMultisampleState = &(VkPipelineMultisampleStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.pNext = &sample_locs_create_info,
			.rasterizationSamples = samples,
			.sampleShadingEnable = false,
			.pSampleMask = NULL,
			.alphaToCoverageEnable = false,
			.alphaToOneEnable = false,
		},
		.pColorBlendState = &(VkPipelineColorBlendStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.logicOpEnable = false,
			.attachmentCount = 0,
			.pAttachments = NULL,
		},
		.pDepthStencilState = &(VkPipelineDepthStencilStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = false,
			.depthWriteEnable = false,
			.depthBoundsTestEnable = false,
			.stencilTestEnable = false,
		},
		.pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = 3,
			.pDynamicStates = (VkDynamicState[]) {
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR,
				VK_DYNAMIC_STATE_SAMPLE_LOCATIONS_EXT,
			},
		},
		.layout = layout,
		.renderPass = pass,
		.subpass = 0,
	};

	struct radv_graphics_pipeline_create_info extra = {
		.use_rectlist = true,
		.depth_compress_disable = decompress == DECOMPRESS_DEPTH_STENCIL ||
					  decompress == DECOMPRESS_DEPTH,
		.stencil_compress_disable = decompress == DECOMPRESS_DEPTH_STENCIL ||
					    decompress == DECOMPRESS_STENCIL,
		.resummarize_enable = op == DEPTH_RESUMMARIZE,
	};

	result = radv_graphics_pipeline_create(device_h,
					       radv_pipeline_cache_to_handle(&device->meta_state.cache),
					       &pipeline_create_info, &extra,
					       &device->meta_state.alloc,
					       pipeline);

cleanup:
	ralloc_free(fs_module.nir);
	ralloc_free(vs_module.nir);
	mtx_unlock(&device->meta_state.mtx);
	return result;
}

void
radv_device_finish_meta_depth_decomp_state(struct radv_device *device)
{
	struct radv_meta_state *state = &device->meta_state;

	for (uint32_t i = 0; i < ARRAY_SIZE(state->depth_decomp); ++i) {
		radv_DestroyRenderPass(radv_device_to_handle(device),
				       state->depth_decomp[i].pass,
				       &state->alloc);
		radv_DestroyPipelineLayout(radv_device_to_handle(device),
					   state->depth_decomp[i].p_layout,
					   &state->alloc);

		for (uint32_t j = 0; j < NUM_DEPTH_DECOMPRESS_PIPELINES; j++) {
			radv_DestroyPipeline(radv_device_to_handle(device),
					     state->depth_decomp[i].decompress_pipeline[j],
					     &state->alloc);
		}
		radv_DestroyPipeline(radv_device_to_handle(device),
				     state->depth_decomp[i].resummarize_pipeline,
				     &state->alloc);
	}
}

VkResult
radv_device_init_meta_depth_decomp_state(struct radv_device *device, bool on_demand)
{
	struct radv_meta_state *state = &device->meta_state;
	VkResult res = VK_SUCCESS;

	for (uint32_t i = 0; i < ARRAY_SIZE(state->depth_decomp); ++i) {
		uint32_t samples = 1 << i;

		res = create_pass(device, samples, &state->depth_decomp[i].pass);
		if (res != VK_SUCCESS)
			goto fail;

		res = create_pipeline_layout(device,
					     &state->depth_decomp[i].p_layout);
		if (res != VK_SUCCESS)
			goto fail;

		if (on_demand)
			continue;

		for (uint32_t j = 0; j < NUM_DEPTH_DECOMPRESS_PIPELINES; j++) {
			res = create_pipeline(device, samples,
					      state->depth_decomp[i].pass,
					      state->depth_decomp[i].p_layout,
					      DEPTH_DECOMPRESS,
					      j,
					      &state->depth_decomp[i].decompress_pipeline[j]);
			if (res != VK_SUCCESS)
				goto fail;
		}

		res = create_pipeline(device, samples,
				      state->depth_decomp[i].pass,
				      state->depth_decomp[i].p_layout,
				      DEPTH_RESUMMARIZE,
				      0, /* unused */
				      &state->depth_decomp[i].resummarize_pipeline);
		if (res != VK_SUCCESS)
			goto fail;
	}

	return VK_SUCCESS;

fail:
	radv_device_finish_meta_depth_decomp_state(device);
	return res;
}

static VkPipeline *
radv_get_depth_pipeline(struct radv_cmd_buffer *cmd_buffer,
			struct radv_image *image,
			const VkImageSubresourceRange *subresourceRange,
			enum radv_depth_op op)
{
	struct radv_meta_state *state = &cmd_buffer->device->meta_state;
	uint32_t samples = image->info.samples;
	uint32_t samples_log2 = ffs(samples) - 1;
	enum radv_depth_decompress decompress;
	VkPipeline *pipeline;

	if (subresourceRange->aspectMask == VK_IMAGE_ASPECT_DEPTH_BIT) {
		decompress = DECOMPRESS_DEPTH;
	} else if (subresourceRange->aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT) {
		decompress = DECOMPRESS_STENCIL;
	} else {
		decompress = DECOMPRESS_DEPTH_STENCIL;
	}

	if (!state->depth_decomp[samples_log2].decompress_pipeline[decompress]) {
		VkResult ret;

		for (uint32_t i = 0; i < NUM_DEPTH_DECOMPRESS_PIPELINES; i++) {
			ret = create_pipeline(cmd_buffer->device, samples,
					      state->depth_decomp[samples_log2].pass,
					      state->depth_decomp[samples_log2].p_layout,
					      DEPTH_DECOMPRESS,
					      i,
					      &state->depth_decomp[samples_log2].decompress_pipeline[i]);
			if (ret != VK_SUCCESS) {
				cmd_buffer->record_result = ret;
				return NULL;
			}
		}

		ret = create_pipeline(cmd_buffer->device, samples,
				      state->depth_decomp[samples_log2].pass,
				      state->depth_decomp[samples_log2].p_layout,
				      DEPTH_RESUMMARIZE,
				      0, /* unused */
				      &state->depth_decomp[samples_log2].resummarize_pipeline);
		if (ret != VK_SUCCESS) {
			cmd_buffer->record_result = ret;
			return NULL;
		}
       }

	switch (op) {
	case DEPTH_DECOMPRESS:
		pipeline = &state->depth_decomp[samples_log2].decompress_pipeline[decompress];
		break;
	case DEPTH_RESUMMARIZE:
		pipeline = &state->depth_decomp[samples_log2].resummarize_pipeline;
		break;
	default:
		unreachable("unknown operation");
	}

	return pipeline;
}

static void
radv_process_depth_image_layer(struct radv_cmd_buffer *cmd_buffer,
			       struct radv_image *image,
			       const VkImageSubresourceRange *range,
			       int level, int layer)
{
	struct radv_device *device = cmd_buffer->device;
	struct radv_meta_state *state = &device->meta_state;
	uint32_t samples_log2 = ffs(image->info.samples) - 1;
	struct radv_image_view iview;
	uint32_t width, height;

	width = radv_minify(image->info.width, range->baseMipLevel + level);
	height = radv_minify(image->info.height, range->baseMipLevel + level);

	radv_image_view_init(&iview, device,
			     &(VkImageViewCreateInfo) {
					.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					.image = radv_image_to_handle(image),
					.viewType = radv_meta_get_view_type(image),
					.format = image->vk_format,
					.subresourceRange = {
						.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
						.baseMipLevel = range->baseMipLevel + level,
						.levelCount = 1,
						.baseArrayLayer = range->baseArrayLayer + layer,
						.layerCount = 1,
					},
			     }, NULL);


	VkFramebuffer fb_h;
	radv_CreateFramebuffer(radv_device_to_handle(device),
			       &(VkFramebufferCreateInfo) {
					.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
					.attachmentCount = 1,
						.pAttachments = (VkImageView[]) {
							radv_image_view_to_handle(&iview)
					},
					.width = width,
					.height = height,
					.layers = 1
			       }, &cmd_buffer->pool->alloc, &fb_h);

	radv_cmd_buffer_begin_render_pass(cmd_buffer,
					  &(VkRenderPassBeginInfo) {
						.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
						.renderPass = state->depth_decomp[samples_log2].pass,
						.framebuffer = fb_h,
						.renderArea = {
							.offset = {
								0,
								0,
							},
							.extent = {
								width,
								height,
							}
						},
						.clearValueCount = 0,
						.pClearValues = NULL,
					});
	radv_cmd_buffer_set_subpass(cmd_buffer,
				    &cmd_buffer->state.pass->subpasses[0]);

	radv_CmdDraw(radv_cmd_buffer_to_handle(cmd_buffer), 3, 1, 0, 0);
	radv_cmd_buffer_end_render_pass(cmd_buffer);

	radv_DestroyFramebuffer(radv_device_to_handle(device), fb_h,
				&cmd_buffer->pool->alloc);
}

static void radv_process_depth_stencil(struct radv_cmd_buffer *cmd_buffer,
				       struct radv_image *image,
				       const VkImageSubresourceRange *subresourceRange,
				       struct radv_sample_locations_state *sample_locs,
				       enum radv_depth_op op)
{
	struct radv_meta_saved_state saved_state;
	VkCommandBuffer cmd_buffer_h = radv_cmd_buffer_to_handle(cmd_buffer);
	VkPipeline *pipeline;

	if (!radv_image_has_htile(image))
		return;

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_GRAPHICS_PIPELINE |
		       RADV_META_SAVE_SAMPLE_LOCATIONS |
		       RADV_META_SAVE_PASS);

	pipeline = radv_get_depth_pipeline(cmd_buffer, image,
					   subresourceRange, op);

	radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
			     VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline);

	if (sample_locs) {
		assert(image->flags & VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT);

		/* Set the sample locations specified during explicit or
		 * automatic layout transitions, otherwise the depth decompress
		 * pass uses the default HW locations.
		 */
		radv_CmdSetSampleLocationsEXT(cmd_buffer_h, &(VkSampleLocationsInfoEXT) {
			.sampleLocationsPerPixel = sample_locs->per_pixel,
			.sampleLocationGridSize = sample_locs->grid_size,
			.sampleLocationsCount = sample_locs->count,
			.pSampleLocations = sample_locs->locations,
		});
	}

	for (uint32_t l = 0; l < radv_get_levelCount(image, subresourceRange); ++l) {
		uint32_t width =
			radv_minify(image->info.width,
				    subresourceRange->baseMipLevel + l);
		uint32_t height =
			radv_minify(image->info.height,
				    subresourceRange->baseMipLevel + l);

		radv_CmdSetViewport(cmd_buffer_h, 0, 1,
				    &(VkViewport) {
					.x = 0,
					.y = 0,
					.width = width,
					.height = height,
					.minDepth = 0.0f,
					.maxDepth = 1.0f
				    });

		radv_CmdSetScissor(cmd_buffer_h, 0, 1,
				   &(VkRect2D) {
					.offset = { 0, 0 },
					.extent = { width, height },
				   });

		for (uint32_t s = 0; s < radv_get_layerCount(image, subresourceRange); s++) {
			radv_process_depth_image_layer(cmd_buffer, image,
						       subresourceRange, l, s);
		}
	}

	radv_meta_restore(&saved_state, cmd_buffer);
}

void radv_decompress_depth_stencil(struct radv_cmd_buffer *cmd_buffer,
				   struct radv_image *image,
				   const VkImageSubresourceRange *subresourceRange,
				   struct radv_sample_locations_state *sample_locs)
{
	struct radv_barrier_data barrier = {0};

	barrier.layout_transitions.depth_stencil_expand = 1;
	radv_describe_layout_transition(cmd_buffer, &barrier);

	assert(cmd_buffer->queue_family_index == RADV_QUEUE_GENERAL);
	radv_process_depth_stencil(cmd_buffer, image, subresourceRange,
				   sample_locs, DEPTH_DECOMPRESS);
}

void radv_resummarize_depth_stencil(struct radv_cmd_buffer *cmd_buffer,
				    struct radv_image *image,
				    const VkImageSubresourceRange *subresourceRange,
				    struct radv_sample_locations_state *sample_locs)
{
	struct radv_barrier_data barrier = {0};

	barrier.layout_transitions.depth_stencil_resummarize = 1;
	radv_describe_layout_transition(cmd_buffer, &barrier);

	assert(cmd_buffer->queue_family_index == RADV_QUEUE_GENERAL);
	radv_process_depth_stencil(cmd_buffer, image, subresourceRange,
				   sample_locs, DEPTH_RESUMMARIZE);
}
