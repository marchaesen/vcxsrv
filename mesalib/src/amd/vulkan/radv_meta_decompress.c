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
								.dependencyCount = 0,
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
                VkShaderModule vs_module_h,
		uint32_t samples,
		VkRenderPass pass,
		VkPipelineLayout layout,
		VkPipeline *decompress_pipeline,
		VkPipeline *resummarize_pipeline)
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

	const VkGraphicsPipelineCreateInfo pipeline_create_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = (VkPipelineShaderStageCreateInfo[]) {
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
			.dynamicStateCount = 2,
			.pDynamicStates = (VkDynamicState[]) {
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR,
			},
		},
		.layout = layout,
		.renderPass = pass,
		.subpass = 0,
	};

	result = radv_graphics_pipeline_create(device_h,
					       radv_pipeline_cache_to_handle(&device->meta_state.cache),
					       &pipeline_create_info,
					       &(struct radv_graphics_pipeline_create_info) {
							.use_rectlist = true,
							.db_flush_depth_inplace = true,
							.db_flush_stencil_inplace = true,
					       },
					       &device->meta_state.alloc,
					       decompress_pipeline);
	if (result != VK_SUCCESS)
		goto cleanup;

	result = radv_graphics_pipeline_create(device_h,
					       radv_pipeline_cache_to_handle(&device->meta_state.cache),
					       &pipeline_create_info,
					       &(struct radv_graphics_pipeline_create_info) {
							.use_rectlist = true,
							.db_flush_depth_inplace = true,
							.db_flush_stencil_inplace = true,
							.db_resummarize = true,
					       },
					       &device->meta_state.alloc,
					       resummarize_pipeline);
	if (result != VK_SUCCESS)
		goto cleanup;

	goto cleanup;

cleanup:
	ralloc_free(fs_module.nir);
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
		radv_DestroyPipeline(radv_device_to_handle(device),
				     state->depth_decomp[i].decompress_pipeline,
				     &state->alloc);
		radv_DestroyPipeline(radv_device_to_handle(device),
				     state->depth_decomp[i].resummarize_pipeline,
				     &state->alloc);
	}
}

VkResult
radv_device_init_meta_depth_decomp_state(struct radv_device *device)
{
	struct radv_meta_state *state = &device->meta_state;
	VkResult res = VK_SUCCESS;

	struct radv_shader_module vs_module = { .nir = radv_meta_build_nir_vs_generate_vertices() };
	if (!vs_module.nir) {
		/* XXX: Need more accurate error */
		res = VK_ERROR_OUT_OF_HOST_MEMORY;
		goto fail;
	}

	VkShaderModule vs_module_h = radv_shader_module_to_handle(&vs_module);

	for (uint32_t i = 0; i < ARRAY_SIZE(state->depth_decomp); ++i) {
		uint32_t samples = 1 << i;

		res = create_pass(device, samples, &state->depth_decomp[i].pass);
		if (res != VK_SUCCESS)
			goto fail;

		res = create_pipeline_layout(device,
					     &state->depth_decomp[i].p_layout);
		if (res != VK_SUCCESS)
			goto fail;

		res = create_pipeline(device, vs_module_h, samples,
				      state->depth_decomp[i].pass,
				      state->depth_decomp[i].p_layout,
				      &state->depth_decomp[i].decompress_pipeline,
				      &state->depth_decomp[i].resummarize_pipeline);
		if (res != VK_SUCCESS)
			goto fail;
	}

	goto cleanup;

fail:
	radv_device_finish_meta_depth_decomp_state(device);

cleanup:
	ralloc_free(vs_module.nir);

	return res;
}

static void
emit_depth_decomp(struct radv_cmd_buffer *cmd_buffer,
		  const VkExtent2D *depth_decomp_extent,
		  VkPipeline pipeline_h)
{
	VkCommandBuffer cmd_buffer_h = radv_cmd_buffer_to_handle(cmd_buffer);

	radv_CmdBindPipeline(cmd_buffer_h, VK_PIPELINE_BIND_POINT_GRAPHICS,
			     pipeline_h);

	radv_CmdSetViewport(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1, &(VkViewport) {
		.x = 0,
		.y = 0,
		.width = depth_decomp_extent->width,
		.height = depth_decomp_extent->height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	});

	radv_CmdSetScissor(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1, &(VkRect2D) {
		.offset = { 0, 0 },
		.extent = *depth_decomp_extent,
	});

	radv_CmdDraw(cmd_buffer_h, 3, 1, 0, 0);
}


enum radv_depth_op {
	DEPTH_DECOMPRESS,
	DEPTH_RESUMMARIZE,
};

static void radv_process_depth_image_inplace(struct radv_cmd_buffer *cmd_buffer,
					     struct radv_image *image,
					     VkImageSubresourceRange *subresourceRange,
					     enum radv_depth_op op)
{
	struct radv_meta_saved_state saved_state;
	VkDevice device_h = radv_device_to_handle(cmd_buffer->device);
	VkCommandBuffer cmd_buffer_h = radv_cmd_buffer_to_handle(cmd_buffer);
	uint32_t width = radv_minify(image->info.width,
				     subresourceRange->baseMipLevel);
	uint32_t height = radv_minify(image->info.height,
				     subresourceRange->baseMipLevel);
	uint32_t samples = image->info.samples;
	uint32_t samples_log2 = ffs(samples) - 1;
	struct radv_meta_state *meta_state = &cmd_buffer->device->meta_state;
	VkPipeline pipeline_h;

	if (!radv_image_has_htile(image))
		return;

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_GRAPHICS_PIPELINE |
		       RADV_META_SAVE_PASS);

	switch (op) {
	case DEPTH_DECOMPRESS:
		pipeline_h = meta_state->depth_decomp[samples_log2].decompress_pipeline;
		break;
	case DEPTH_RESUMMARIZE:
		pipeline_h = meta_state->depth_decomp[samples_log2].resummarize_pipeline;
		break;
	default:
		unreachable("unknown operation");
	}

	for (uint32_t layer = 0; layer < radv_get_layerCount(image, subresourceRange); layer++) {
		struct radv_image_view iview;

		radv_image_view_init(&iview, cmd_buffer->device,
				     &(VkImageViewCreateInfo) {
					     .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					     .image = radv_image_to_handle(image),
					     .viewType = radv_meta_get_view_type(image),
					     .format = image->vk_format,
					     .subresourceRange = {
						     .aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT,
						     .baseMipLevel = subresourceRange->baseMipLevel,
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
					       .width = width,
						.height = height,
					       .layers = 1
				       },
				       &cmd_buffer->pool->alloc,
				       &fb_h);

		radv_CmdBeginRenderPass(cmd_buffer_h,
					      &(VkRenderPassBeginInfo) {
						      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
							      .renderPass = meta_state->depth_decomp[samples_log2].pass,
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
					   },
					   VK_SUBPASS_CONTENTS_INLINE);

		emit_depth_decomp(cmd_buffer, &(VkExtent2D){width, height}, pipeline_h);
		radv_CmdEndRenderPass(cmd_buffer_h);

		radv_DestroyFramebuffer(device_h, fb_h,
					&cmd_buffer->pool->alloc);
	}
	radv_meta_restore(&saved_state, cmd_buffer);
}

void radv_decompress_depth_image_inplace(struct radv_cmd_buffer *cmd_buffer,
					 struct radv_image *image,
					 VkImageSubresourceRange *subresourceRange)
{
	assert(cmd_buffer->queue_family_index == RADV_QUEUE_GENERAL);
	radv_process_depth_image_inplace(cmd_buffer, image, subresourceRange, DEPTH_DECOMPRESS);
}

void radv_resummarize_depth_image_inplace(struct radv_cmd_buffer *cmd_buffer,
					 struct radv_image *image,
					 VkImageSubresourceRange *subresourceRange)
{
	assert(cmd_buffer->queue_family_index == RADV_QUEUE_GENERAL);
	radv_process_depth_image_inplace(cmd_buffer, image, subresourceRange, DEPTH_RESUMMARIZE);
}
