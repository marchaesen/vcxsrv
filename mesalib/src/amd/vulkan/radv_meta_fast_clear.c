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

static VkResult
create_pass(struct radv_device *device)
{
	VkResult result;
	VkDevice device_h = radv_device_to_handle(device);
	const VkAllocationCallbacks *alloc = &device->meta_state.alloc;
	VkAttachmentDescription attachment;

	attachment.format = VK_FORMAT_UNDEFINED;
	attachment.samples = 1;
	attachment.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
	attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment.initialLayout = VK_IMAGE_LAYOUT_GENERAL;
	attachment.finalLayout = VK_IMAGE_LAYOUT_GENERAL;

	result = radv_CreateRenderPass(device_h,
				       &(VkRenderPassCreateInfo) {
					       .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
						       .attachmentCount = 1,
						       .pAttachments = &attachment,
						       .subpassCount = 1,
						       .pSubpasses = &(VkSubpassDescription) {
						       .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
						       .inputAttachmentCount = 0,
						       .colorAttachmentCount = 1,
						       .pColorAttachments = (VkAttachmentReference[]) {
							       {
								       .attachment = 0,
								       .layout = VK_IMAGE_LAYOUT_GENERAL,
							       },
						       },
						       .pResolveAttachments = NULL,
						       .pDepthStencilAttachment = &(VkAttachmentReference) {
							       .attachment = VK_ATTACHMENT_UNUSED,
						       },
						       .preserveAttachmentCount = 0,
						       .pPreserveAttachments = NULL,
					       },
								.dependencyCount = 0,
				       },
				       alloc,
				       &device->meta_state.fast_clear_flush.pass);

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
		VkShaderModule vs_module_h,
		VkPipelineLayout layout)
{
	VkResult result;
	VkDevice device_h = radv_device_to_handle(device);

	struct radv_shader_module fs_module = {
		.nir = radv_meta_build_nir_fs_noop(),
	};

	if (!fs_module.nir) {
		/* XXX: Need more accurate error */
		result = VK_ERROR_OUT_OF_HOST_MEMORY;
		goto cleanup;
	}

	const VkPipelineShaderStageCreateInfo stages[2] = {
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_VERTEX_BIT,
			.module = vs_module_h,
			.pName = "main",
		},
		{
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
			.module = radv_shader_module_to_handle(&fs_module),
			.pName = "main",
		},
	};

	const VkPipelineVertexInputStateCreateInfo vi_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 0,
		.vertexAttributeDescriptionCount = 0,
	};

	const VkPipelineInputAssemblyStateCreateInfo ia_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
		.primitiveRestartEnable = false,
	};

	const VkPipelineColorBlendStateCreateInfo blend_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = false,
		.attachmentCount = 1,
		.pAttachments = (VkPipelineColorBlendAttachmentState []) {
			{
				.colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
				VK_COLOR_COMPONENT_G_BIT |
				VK_COLOR_COMPONENT_B_BIT |
				VK_COLOR_COMPONENT_A_BIT,
			},
		}
	};
	const VkPipelineRasterizationStateCreateInfo rs_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = false,
		.rasterizerDiscardEnable = false,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
	};

	result = radv_graphics_pipeline_create(device_h,
					       radv_pipeline_cache_to_handle(&device->meta_state.cache),
					       &(VkGraphicsPipelineCreateInfo) {
						       .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
						       .stageCount = 2,
						       .pStages = stages,

						       .pVertexInputState = &vi_state,
						       .pInputAssemblyState = &ia_state,

					       .pViewportState = &(VkPipelineViewportStateCreateInfo) {
						       .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
						       .viewportCount = 1,
						       .scissorCount = 1,
					       },
						       .pRasterizationState = &rs_state,
					       .pMultisampleState = &(VkPipelineMultisampleStateCreateInfo) {
						       .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
						       .rasterizationSamples = 1,
						       .sampleShadingEnable = false,
						       .pSampleMask = NULL,
						       .alphaToCoverageEnable = false,
						       .alphaToOneEnable = false,
					       },
						.pColorBlendState = &blend_state,
						.pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
							.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
							.dynamicStateCount = 2,
							.pDynamicStates = (VkDynamicState[]) {
								VK_DYNAMIC_STATE_VIEWPORT,
								VK_DYNAMIC_STATE_SCISSOR,
							},
						},
					        .layout = layout,
						.renderPass = device->meta_state.fast_clear_flush.pass,
						.subpass = 0,
					       },
					       &(struct radv_graphics_pipeline_create_info) {
						       .use_rectlist = true,
						       .custom_blend_mode = V_028808_CB_ELIMINATE_FAST_CLEAR,
					       },
					       &device->meta_state.alloc,
					       &device->meta_state.fast_clear_flush.cmask_eliminate_pipeline);
	if (result != VK_SUCCESS)
		goto cleanup;

	result = radv_graphics_pipeline_create(device_h,
					       radv_pipeline_cache_to_handle(&device->meta_state.cache),
					       &(VkGraphicsPipelineCreateInfo) {
						       .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
						       .stageCount = 2,
						       .pStages = stages,

						       .pVertexInputState = &vi_state,
						       .pInputAssemblyState = &ia_state,

					       .pViewportState = &(VkPipelineViewportStateCreateInfo) {
						       .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
						       .viewportCount = 1,
						       .scissorCount = 1,
					       },
						       .pRasterizationState = &rs_state,
					       .pMultisampleState = &(VkPipelineMultisampleStateCreateInfo) {
						       .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
						       .rasterizationSamples = 1,
						       .sampleShadingEnable = false,
						       .pSampleMask = NULL,
						       .alphaToCoverageEnable = false,
						       .alphaToOneEnable = false,
					       },
						.pColorBlendState = &blend_state,
						.pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
							.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
							.dynamicStateCount = 2,
							.pDynamicStates = (VkDynamicState[]) {
								VK_DYNAMIC_STATE_VIEWPORT,
								VK_DYNAMIC_STATE_SCISSOR,
							},
						},
						.layout = layout,
						.renderPass = device->meta_state.fast_clear_flush.pass,
						.subpass = 0,
					       },
					       &(struct radv_graphics_pipeline_create_info) {
						       .use_rectlist = true,
						       .custom_blend_mode = V_028808_CB_FMASK_DECOMPRESS,
					       },
					       &device->meta_state.alloc,
					       &device->meta_state.fast_clear_flush.fmask_decompress_pipeline);
	if (result != VK_SUCCESS)
		goto cleanup_cmask;

	goto cleanup;
cleanup_cmask:
	radv_DestroyPipeline(device_h, device->meta_state.fast_clear_flush.cmask_eliminate_pipeline, &device->meta_state.alloc);
cleanup:
	ralloc_free(fs_module.nir);
	return result;
}

void
radv_device_finish_meta_fast_clear_flush_state(struct radv_device *device)
{
	struct radv_meta_state *state = &device->meta_state;

	radv_DestroyRenderPass(radv_device_to_handle(device),
			       state->fast_clear_flush.pass, &state->alloc);
	radv_DestroyPipelineLayout(radv_device_to_handle(device),
				   state->fast_clear_flush.p_layout,
				   &state->alloc);
	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->fast_clear_flush.cmask_eliminate_pipeline,
			     &state->alloc);
	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->fast_clear_flush.fmask_decompress_pipeline,
			     &state->alloc);
}

VkResult
radv_device_init_meta_fast_clear_flush_state(struct radv_device *device)
{
	VkResult res = VK_SUCCESS;

	struct radv_shader_module vs_module = { .nir = radv_meta_build_nir_vs_generate_vertices() };
	if (!vs_module.nir) {
		/* XXX: Need more accurate error */
		res = VK_ERROR_OUT_OF_HOST_MEMORY;
		goto fail;
	}

	res = create_pass(device);
	if (res != VK_SUCCESS)
		goto fail;

	res = create_pipeline_layout(device,
				     &device->meta_state.fast_clear_flush.p_layout);
	if (res != VK_SUCCESS)
		goto fail;

	VkShaderModule vs_module_h = radv_shader_module_to_handle(&vs_module);
	res = create_pipeline(device, vs_module_h,
			      device->meta_state.fast_clear_flush.p_layout);
	if (res != VK_SUCCESS)
		goto fail;

	goto cleanup;

fail:
	radv_device_finish_meta_fast_clear_flush_state(device);

cleanup:
	ralloc_free(vs_module.nir);

	return res;
}

static void
emit_fast_clear_flush(struct radv_cmd_buffer *cmd_buffer,
		      const VkExtent2D *resolve_extent,
		      VkPipeline pipeline)
{
	VkCommandBuffer cmd_buffer_h = radv_cmd_buffer_to_handle(cmd_buffer);

	radv_CmdBindPipeline(cmd_buffer_h, VK_PIPELINE_BIND_POINT_GRAPHICS,
			     pipeline);

	radv_CmdSetViewport(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1, &(VkViewport) {
			.x = 0,
			.y = 0,
			.width = resolve_extent->width,
			.height = resolve_extent->height,
			.minDepth = 0.0f,
			.maxDepth = 1.0f
		});

		radv_CmdSetScissor(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1, &(VkRect2D) {
			.offset = (VkOffset2D) { 0, 0 },
			.extent = (VkExtent2D) { resolve_extent->width, resolve_extent->height },
		});

	radv_CmdDraw(cmd_buffer_h, 3, 1, 0, 0);
	cmd_buffer->state.flush_bits |= (RADV_CMD_FLAG_FLUSH_AND_INV_CB |
					 RADV_CMD_FLAG_FLUSH_AND_INV_CB_META);
}

static void
radv_emit_set_predication_state_from_image(struct radv_cmd_buffer *cmd_buffer,
				      struct radv_image *image, bool value)
{
	uint64_t va = 0;

	if (value) {
		va = radv_buffer_get_va(image->bo) + image->offset;
		va += image->dcc_pred_offset;
	}

	si_emit_set_predication_state(cmd_buffer, va);
}

/**
 */
void
radv_fast_clear_flush_image_inplace(struct radv_cmd_buffer *cmd_buffer,
				    struct radv_image *image,
				    const VkImageSubresourceRange *subresourceRange)
{
	struct radv_meta_saved_state saved_state;
	VkDevice device_h = radv_device_to_handle(cmd_buffer->device);
	VkCommandBuffer cmd_buffer_h = radv_cmd_buffer_to_handle(cmd_buffer);
	uint32_t layer_count = radv_get_layerCount(image, subresourceRange);
	VkPipeline pipeline;

	assert(cmd_buffer->queue_family_index == RADV_QUEUE_GENERAL);

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_GRAPHICS_PIPELINE |
		       RADV_META_SAVE_PASS);

	if (image->fmask.size > 0) {
               pipeline = cmd_buffer->device->meta_state.fast_clear_flush.fmask_decompress_pipeline;
	} else {
               pipeline = cmd_buffer->device->meta_state.fast_clear_flush.cmask_eliminate_pipeline;
	}

	if (image->surface.dcc_size) {
		radv_emit_set_predication_state_from_image(cmd_buffer, image, true);
		cmd_buffer->state.predicating = true;
	}
	for (uint32_t layer = 0; layer < layer_count; ++layer) {
		struct radv_image_view iview;

		radv_image_view_init(&iview, cmd_buffer->device,
				     &(VkImageViewCreateInfo) {
					     .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					     .image = radv_image_to_handle(image),
					     .viewType = radv_meta_get_view_type(image),
					     .format = image->vk_format,
					     .subresourceRange = {
						     .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						     .baseMipLevel = 0,
						     .levelCount = 1,
						     .baseArrayLayer = subresourceRange->baseArrayLayer + layer,
						     .layerCount = 1,
					      },
				     });

		VkFramebuffer fb_h;
		radv_CreateFramebuffer(device_h,
				&(VkFramebufferCreateInfo) {
					.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
					.attachmentCount = 1,
					.pAttachments = (VkImageView[]) {
						radv_image_view_to_handle(&iview)
					},
				       .width = image->info.width,
				       .height = image->info.height,
				       .layers = 1
				},
				&cmd_buffer->pool->alloc,
				&fb_h);

		radv_CmdBeginRenderPass(cmd_buffer_h,
				      &(VkRenderPassBeginInfo) {
					      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
						      .renderPass = cmd_buffer->device->meta_state.fast_clear_flush.pass,
						      .framebuffer = fb_h,
						      .renderArea = {
						      .offset = {
							      0,
							      0,
						      },
						      .extent = {
							      image->info.width,
							      image->info.height,
						      }
					      },
					      .clearValueCount = 0,
					      .pClearValues = NULL,
				     },
				     VK_SUBPASS_CONTENTS_INLINE);

		emit_fast_clear_flush(cmd_buffer,
				      &(VkExtent2D) { image->info.width, image->info.height },
				      pipeline);
		radv_CmdEndRenderPass(cmd_buffer_h);

		radv_DestroyFramebuffer(device_h, fb_h,
					&cmd_buffer->pool->alloc);

	}
	if (image->surface.dcc_size) {
		cmd_buffer->state.predicating = false;
		radv_emit_set_predication_state_from_image(cmd_buffer, image, false);
	}
	radv_meta_restore(&saved_state, cmd_buffer);
}
