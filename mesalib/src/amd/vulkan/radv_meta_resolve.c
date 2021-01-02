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
#include "vk_format.h"
#include "nir/nir_builder.h"
#include "sid.h"

/* emit 0, 0, 0, 1 */
static nir_shader *
build_nir_fs(void)
{
	const struct glsl_type *vec4 = glsl_vec4_type();
	nir_variable *f_color; /* vec4, fragment output color */

	nir_builder b = nir_builder_init_simple_shader(MESA_SHADER_FRAGMENT, NULL, "meta_resolve_fs");

	f_color = nir_variable_create(b.shader, nir_var_shader_out, vec4,
				      "f_color");
	f_color->data.location = FRAG_RESULT_DATA0;
	nir_store_var(&b, f_color, nir_imm_vec4(&b, 0.0, 0.0, 0.0, 1.0), 0xf);

	return b.shader;
}

static VkResult
create_pass(struct radv_device *device, VkFormat vk_format, VkRenderPass *pass)
{
	VkResult result;
	VkDevice device_h = radv_device_to_handle(device);
	const VkAllocationCallbacks *alloc = &device->meta_state.alloc;
	VkAttachmentDescription attachments[2];
	int i;

	for (i = 0; i < 2; i++) {
		attachments[i].format = vk_format;
		attachments[i].samples = 1;
		attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	}
	attachments[0].initialLayout = VK_IMAGE_LAYOUT_GENERAL;
	attachments[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
	attachments[1].initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	attachments[1].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	result = radv_CreateRenderPass(device_h,
				       &(VkRenderPassCreateInfo) {
					       .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
						       .attachmentCount = 2,
						       .pAttachments = attachments,
						       .subpassCount = 1,
								.pSubpasses = &(VkSubpassDescription) {
						       .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
						       .inputAttachmentCount = 0,
						       .colorAttachmentCount = 2,
						       .pColorAttachments = (VkAttachmentReference[]) {
							       {
								       .attachment = 0,
								       .layout = VK_IMAGE_LAYOUT_GENERAL,
							       },
							       {
								       .attachment = 1,
								       .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
							       },
						       },
						       .pResolveAttachments = NULL,
						       .pDepthStencilAttachment = &(VkAttachmentReference) {
							       .attachment = VK_ATTACHMENT_UNUSED,
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
create_pipeline(struct radv_device *device,
		 VkShaderModule vs_module_h,
		 VkPipeline *pipeline,
		 VkRenderPass pass)
{
	VkResult result;
	VkDevice device_h = radv_device_to_handle(device);

	struct radv_shader_module fs_module = {
		.nir = build_nir_fs(),
	};

	if (!fs_module.nir) {
		/* XXX: Need more accurate error */
		result = VK_ERROR_OUT_OF_HOST_MEMORY;
		goto cleanup;
	}

	VkPipelineLayoutCreateInfo pl_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 0,
		.pSetLayouts = NULL,
		.pushConstantRangeCount = 0,
		.pPushConstantRanges = NULL,
	};

	if (!device->meta_state.resolve.p_layout) {
		result = radv_CreatePipelineLayout(radv_device_to_handle(device),
						   &pl_create_info,
						   &device->meta_state.alloc,
						   &device->meta_state.resolve.p_layout);
		if (result != VK_SUCCESS)
			goto cleanup;
	}

	result = radv_graphics_pipeline_create(device_h,
					       radv_pipeline_cache_to_handle(&device->meta_state.cache),
					       &(VkGraphicsPipelineCreateInfo) {
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
						       .rasterizationSamples = 1,
						       .sampleShadingEnable = false,
						       .pSampleMask = NULL,
						       .alphaToCoverageEnable = false,
						       .alphaToOneEnable = false,
					       },
					       .pColorBlendState = &(VkPipelineColorBlendStateCreateInfo) {
						       .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
						       .logicOpEnable = false,
						       .attachmentCount = 2,
						       .pAttachments = (VkPipelineColorBlendAttachmentState []) {
							       {
							       .colorWriteMask = VK_COLOR_COMPONENT_R_BIT |
									       VK_COLOR_COMPONENT_G_BIT |
									       VK_COLOR_COMPONENT_B_BIT |
									       VK_COLOR_COMPONENT_A_BIT,
							       },
							       {
							       .colorWriteMask = 0,

							       }
						       },
						},
						.pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
							.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
							.dynamicStateCount = 2,
							.pDynamicStates = (VkDynamicState[]) {
								VK_DYNAMIC_STATE_VIEWPORT,
								VK_DYNAMIC_STATE_SCISSOR,
							},
						},
						.layout = device->meta_state.resolve.p_layout,
						.renderPass = pass,
																       .subpass = 0,
																       },
					       &(struct radv_graphics_pipeline_create_info) {
						       .use_rectlist = true,
						       .custom_blend_mode = V_028808_CB_RESOLVE,
							       },
					       &device->meta_state.alloc, pipeline);
	if (result != VK_SUCCESS)
		goto cleanup;

	goto cleanup;

cleanup:
	ralloc_free(fs_module.nir);
	return result;
}

void
radv_device_finish_meta_resolve_state(struct radv_device *device)
{
	struct radv_meta_state *state = &device->meta_state;

	for (uint32_t j = 0; j < NUM_META_FS_KEYS; j++) {
		radv_DestroyRenderPass(radv_device_to_handle(device),
				       state->resolve.pass[j], &state->alloc);
		radv_DestroyPipeline(radv_device_to_handle(device),
				     state->resolve.pipeline[j], &state->alloc);
	}
	radv_DestroyPipelineLayout(radv_device_to_handle(device),
				   state->resolve.p_layout, &state->alloc);

}

VkResult
radv_device_init_meta_resolve_state(struct radv_device *device, bool on_demand)
{
	if (on_demand)
		return VK_SUCCESS;

	VkResult res = VK_SUCCESS;
	struct radv_meta_state *state = &device->meta_state;
	struct radv_shader_module vs_module = { .nir = radv_meta_build_nir_vs_generate_vertices() };
	if (!vs_module.nir) {
		/* XXX: Need more accurate error */
		res = VK_ERROR_OUT_OF_HOST_MEMORY;
		goto fail;
	}

	for (uint32_t i = 0; i < NUM_META_FS_KEYS; ++i) {
		VkFormat format = radv_fs_key_format_exemplars[i];
		unsigned fs_key = radv_format_meta_fs_key(device, format);
		res = create_pass(device, format, &state->resolve.pass[fs_key]);
		if (res != VK_SUCCESS)
			goto fail;

		VkShaderModule vs_module_h = radv_shader_module_to_handle(&vs_module);
		res = create_pipeline(device, vs_module_h,
				      &state->resolve.pipeline[fs_key], state->resolve.pass[fs_key]);
		if (res != VK_SUCCESS)
			goto fail;
	}

	goto cleanup;

fail:
	radv_device_finish_meta_resolve_state(device);

cleanup:
	ralloc_free(vs_module.nir);

	return res;
}

static void
emit_resolve(struct radv_cmd_buffer *cmd_buffer,
	     VkFormat vk_format,
             const VkOffset2D *dest_offset,
             const VkExtent2D *resolve_extent)
{
	struct radv_device *device = cmd_buffer->device;
	VkCommandBuffer cmd_buffer_h = radv_cmd_buffer_to_handle(cmd_buffer);
	unsigned fs_key = radv_format_meta_fs_key(device, vk_format);

	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB;

	radv_CmdBindPipeline(cmd_buffer_h, VK_PIPELINE_BIND_POINT_GRAPHICS,
			     device->meta_state.resolve.pipeline[fs_key]);

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

enum radv_resolve_method {
	RESOLVE_HW,
	RESOLVE_COMPUTE,
	RESOLVE_FRAGMENT,
};

static void radv_pick_resolve_method_images(struct radv_device *device,
					    struct radv_image *src_image,
					    VkFormat src_format,
					    struct radv_image *dest_image,
					    VkImageLayout dest_image_layout,
					    bool dest_render_loop,
					    struct radv_cmd_buffer *cmd_buffer,
					    enum radv_resolve_method *method)

{
	uint32_t queue_mask = radv_image_queue_family_mask(dest_image,
	                                                   cmd_buffer->queue_family_index,
	                                                   cmd_buffer->queue_family_index);

	if (vk_format_is_color(src_format)) {
		if (src_format == VK_FORMAT_R16G16_UNORM ||
		    src_format == VK_FORMAT_R16G16_SNORM)
			*method = RESOLVE_COMPUTE;
		else if (vk_format_is_int(src_format))
			*method = RESOLVE_COMPUTE;
		else if (src_image->info.array_size > 1 ||
			 dest_image->info.array_size > 1)
			*method = RESOLVE_COMPUTE;

		if (radv_layout_dcc_compressed(device, dest_image, dest_image_layout,
		                               dest_render_loop, queue_mask)) {
			*method = RESOLVE_FRAGMENT;
		} else if (dest_image->planes[0].surface.micro_tile_mode !=
		           src_image->planes[0].surface.micro_tile_mode) {
			*method = RESOLVE_COMPUTE;
		}
	} else {
		if (src_image->info.array_size > 1 ||
		    dest_image->info.array_size > 1)
			*method = RESOLVE_COMPUTE;
		else
			*method = RESOLVE_FRAGMENT;
	}
}

static VkResult
build_resolve_pipeline(struct radv_device *device,
                       unsigned fs_key)
{
	VkResult result = VK_SUCCESS;

	if (device->meta_state.resolve.pipeline[fs_key])
		return result;

	mtx_lock(&device->meta_state.mtx);
	if (device->meta_state.resolve.pipeline[fs_key]) {
		mtx_unlock(&device->meta_state.mtx);
		return result;
	}

	struct radv_shader_module vs_module = { .nir = radv_meta_build_nir_vs_generate_vertices() };

	result = create_pass(device, radv_fs_key_format_exemplars[fs_key], &device->meta_state.resolve.pass[fs_key]);
	if (result != VK_SUCCESS)
		goto fail;

	VkShaderModule vs_module_h = radv_shader_module_to_handle(&vs_module);
	result = create_pipeline(device, vs_module_h, &device->meta_state.resolve.pipeline[fs_key], device->meta_state.resolve.pass[fs_key]);

fail:
	ralloc_free(vs_module.nir);
	mtx_unlock(&device->meta_state.mtx);
	return result;
}

static void
radv_meta_resolve_hardware_image(struct radv_cmd_buffer *cmd_buffer,
				 struct radv_image *src_image,
				 VkImageLayout src_image_layout,
				 struct radv_image *dst_image,
				 VkImageLayout dst_image_layout,
				 const VkImageResolve2KHR *region)
{
	struct radv_device *device = cmd_buffer->device;
	struct radv_meta_saved_state saved_state;

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_GRAPHICS_PIPELINE);

	assert(src_image->info.samples > 1);
	if (src_image->info.samples <= 1) {
		/* this causes GPU hangs if we get past here */
		fprintf(stderr, "radv: Illegal resolve operation (src not multisampled), will hang GPU.");
		return;
	}
	assert(dst_image->info.samples == 1);

	if (src_image->info.array_size > 1)
		radv_finishme("vkCmdResolveImage: multisample array images");

	unsigned fs_key = radv_format_meta_fs_key(device, dst_image->vk_format);

	/* From the Vulkan 1.0 spec:
	 *
	 *    - The aspectMask member of srcSubresource and dstSubresource must
	 *      only contain VK_IMAGE_ASPECT_COLOR_BIT
	 *
	 *    - The layerCount member of srcSubresource and dstSubresource must
	 *      match
	 */
	assert(region->srcSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
	assert(region->dstSubresource.aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
	assert(region->srcSubresource.layerCount ==
	       region->dstSubresource.layerCount);

	const uint32_t src_base_layer =
		radv_meta_get_iview_layer(src_image, &region->srcSubresource,
					  &region->srcOffset);

	const uint32_t dst_base_layer =
		radv_meta_get_iview_layer(dst_image, &region->dstSubresource,
					  &region->dstOffset);

	/**
	 * From Vulkan 1.0.6 spec: 18.6 Resolving Multisample Images
	 *
	 *    extent is the size in texels of the source image to resolve in width,
	 *    height and depth. 1D images use only x and width. 2D images use x, y,
	 *    width and height. 3D images use x, y, z, width, height and depth.
	 *
	 *    srcOffset and dstOffset select the initial x, y, and z offsets in
	 *    texels of the sub-regions of the source and destination image data.
	 *    extent is the size in texels of the source image to resolve in width,
	 *    height and depth. 1D images use only x and width. 2D images use x, y,
	 *    width and height. 3D images use x, y, z, width, height and depth.
	 */
	const struct VkExtent3D extent =
		radv_sanitize_image_extent(src_image->type, region->extent);
	const struct VkOffset3D dstOffset =
		radv_sanitize_image_offset(dst_image->type, region->dstOffset);

	if (radv_dcc_enabled(dst_image, region->dstSubresource.mipLevel)) {
		VkImageSubresourceRange range = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.baseMipLevel = region->dstSubresource.mipLevel,
			.levelCount = 1,
			.baseArrayLayer = dst_base_layer,
			.layerCount = region->dstSubresource.layerCount,
		};

		radv_initialize_dcc(cmd_buffer, dst_image, &range, 0xffffffff);
	}

	for (uint32_t layer = 0; layer < region->srcSubresource.layerCount;
	     ++layer) {

		VkResult ret = build_resolve_pipeline(device, fs_key);
		if (ret != VK_SUCCESS) {
			cmd_buffer->record_result = ret;
			break;
		}

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
				     }, NULL);

		struct radv_image_view dst_iview;
		radv_image_view_init(&dst_iview, cmd_buffer->device,
				     &(VkImageViewCreateInfo) {
					     .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
						     .image = radv_image_to_handle(dst_image),
						     .viewType = radv_meta_get_view_type(dst_image),
						     .format = dst_image->vk_format,
						     .subresourceRange = {
						     .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						     .baseMipLevel = region->dstSubresource.mipLevel,
						     .levelCount = 1,
						     .baseArrayLayer = dst_base_layer + layer,
						     .layerCount = 1,
					     },
				      }, NULL);

		VkFramebuffer fb_h;
		radv_CreateFramebuffer(radv_device_to_handle(device),
				       &(VkFramebufferCreateInfo) {
					       .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
						       .attachmentCount = 2,
						       .pAttachments = (VkImageView[]) {
						       radv_image_view_to_handle(&src_iview),
						       radv_image_view_to_handle(&dst_iview),
					       },
					       .width = radv_minify(dst_image->info.width,
								    region->dstSubresource.mipLevel),
					       .height = radv_minify(dst_image->info.height,
								      region->dstSubresource.mipLevel),
					       .layers = 1
				       },
				       &cmd_buffer->pool->alloc,
				       &fb_h);

		radv_cmd_buffer_begin_render_pass(cmd_buffer,
						  &(VkRenderPassBeginInfo) {
							.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
							.renderPass = device->meta_state.resolve.pass[fs_key],
							.framebuffer = fb_h,
							.renderArea = {
								.offset = {
									dstOffset.x,
									dstOffset.y,
								},
								.extent = {
									extent.width,
									extent.height,
							      }
							},
							.clearValueCount = 0,
							.pClearValues = NULL,
					      }, NULL);

		radv_cmd_buffer_set_subpass(cmd_buffer,
					    &cmd_buffer->state.pass->subpasses[0]);

		emit_resolve(cmd_buffer,
			     dst_iview.vk_format,
			     &(VkOffset2D) {
				     .x = dstOffset.x,
				     .y = dstOffset.y,
			     },
			     &(VkExtent2D) {
				     .width = extent.width,
				     .height = extent.height,
			     });

		radv_cmd_buffer_end_render_pass(cmd_buffer);

		radv_DestroyFramebuffer(radv_device_to_handle(device),
					fb_h, &cmd_buffer->pool->alloc);
	}

	radv_meta_restore(&saved_state, cmd_buffer);
}

static void
resolve_image(struct radv_cmd_buffer *cmd_buffer,
	      struct radv_image *src_image,
	      VkImageLayout src_image_layout,
	      struct radv_image *dst_image,
	      VkImageLayout dst_image_layout,
	      const VkImageResolve2KHR *region,
	      enum radv_resolve_method resolve_method)
{
	switch (resolve_method) {
	case RESOLVE_HW:
		radv_meta_resolve_hardware_image(cmd_buffer,
						 src_image,
						 src_image_layout,
						 dst_image,
						 dst_image_layout,
						 region);
		break;
	case RESOLVE_FRAGMENT:
		radv_meta_resolve_fragment_image(cmd_buffer,
						 src_image,
						 src_image_layout,
						 dst_image,
						 dst_image_layout,
						 region);
		break;
	case RESOLVE_COMPUTE:
		radv_meta_resolve_compute_image(cmd_buffer,
						src_image,
						src_image->vk_format,
						src_image_layout,
						dst_image,
						dst_image->vk_format,
						dst_image_layout,
						region);
		break;
	default:
		assert(!"Invalid resolve method selected");
	}
}

void radv_CmdResolveImage(
	VkCommandBuffer                             cmd_buffer_h,
	VkImage                                     src_image_h,
	VkImageLayout                               src_image_layout,
	VkImage                                     dest_image_h,
	VkImageLayout                               dest_image_layout,
	uint32_t                                    region_count,
	const VkImageResolve*                       regions)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, cmd_buffer_h);
	RADV_FROM_HANDLE(radv_image, src_image, src_image_h);
	RADV_FROM_HANDLE(radv_image, dest_image, dest_image_h);
	enum radv_resolve_method resolve_method = RESOLVE_HW;
	/* we can use the hw resolve only for single full resolves */
	if (region_count == 1) {
		if (regions[0].srcOffset.x ||
		    regions[0].srcOffset.y ||
		    regions[0].srcOffset.z)
			resolve_method = RESOLVE_COMPUTE;
		if (regions[0].dstOffset.x ||
		    regions[0].dstOffset.y ||
		    regions[0].dstOffset.z)
			resolve_method = RESOLVE_COMPUTE;

		if (regions[0].extent.width != src_image->info.width ||
		    regions[0].extent.height != src_image->info.height ||
		    regions[0].extent.depth != src_image->info.depth)
			resolve_method = RESOLVE_COMPUTE;
	} else
		resolve_method = RESOLVE_COMPUTE;

	radv_pick_resolve_method_images(cmd_buffer->device, src_image,
					src_image->vk_format, dest_image,
					dest_image_layout, false, cmd_buffer,
					&resolve_method);

	for (uint32_t r = 0; r < region_count; r++) {
		VkImageResolve2KHR region = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2_KHR,
			.srcSubresource = regions[r].srcSubresource,
			.srcOffset      = regions[r].srcOffset,
			.dstSubresource = regions[r].dstSubresource,
			.dstOffset      = regions[r].dstOffset,
			.extent         = regions[r].extent,
		};

		resolve_image(cmd_buffer, src_image, src_image_layout,
			      dest_image, dest_image_layout,
			      &region, resolve_method);
	}
}

void radv_CmdResolveImage2KHR(
	VkCommandBuffer                             commandBuffer,
	const VkResolveImageInfo2KHR*               pResolveImageInfo)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_image, src_image, pResolveImageInfo->srcImage);
	RADV_FROM_HANDLE(radv_image, dst_image, pResolveImageInfo->dstImage);
	VkImageLayout src_image_layout = pResolveImageInfo->srcImageLayout;
	VkImageLayout dst_image_layout = pResolveImageInfo->dstImageLayout;
	enum radv_resolve_method resolve_method = RESOLVE_HW;
	/* we can use the hw resolve only for single full resolves */
	if (pResolveImageInfo->regionCount == 1) {
		if (pResolveImageInfo->pRegions[0].srcOffset.x ||
		    pResolveImageInfo->pRegions[0].srcOffset.y ||
		    pResolveImageInfo->pRegions[0].srcOffset.z)
			resolve_method = RESOLVE_COMPUTE;
		if (pResolveImageInfo->pRegions[0].dstOffset.x ||
		    pResolveImageInfo->pRegions[0].dstOffset.y ||
		    pResolveImageInfo->pRegions[0].dstOffset.z)
			resolve_method = RESOLVE_COMPUTE;

		if (pResolveImageInfo->pRegions[0].extent.width != src_image->info.width ||
		    pResolveImageInfo->pRegions[0].extent.height != src_image->info.height ||
		    pResolveImageInfo->pRegions[0].extent.depth != src_image->info.depth)
			resolve_method = RESOLVE_COMPUTE;
	} else
		resolve_method = RESOLVE_COMPUTE;

	radv_pick_resolve_method_images(cmd_buffer->device, src_image,
					src_image->vk_format, dst_image,
					dst_image_layout, false, cmd_buffer,
					&resolve_method);

	for (uint32_t r = 0; r < pResolveImageInfo->regionCount; r++) {
		resolve_image(cmd_buffer, src_image, src_image_layout,
			      dst_image, dst_image_layout,
			      &pResolveImageInfo->pRegions[r], resolve_method);
	}
}

/**
 * Emit any needed resolves for the current subpass.
 */
void
radv_cmd_buffer_resolve_subpass(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_framebuffer *fb = cmd_buffer->state.framebuffer;
	const struct radv_subpass *subpass = cmd_buffer->state.subpass;
	struct radv_meta_saved_state saved_state;
	enum radv_resolve_method resolve_method = RESOLVE_HW;

	if (subpass->ds_resolve_attachment) {
		struct radv_subpass_attachment src_att = *subpass->depth_stencil_attachment;
		struct radv_subpass_attachment dst_att = *subpass->ds_resolve_attachment;
		struct radv_image_view *src_iview =
			cmd_buffer->state.attachments[src_att.attachment].iview;
		struct radv_image_view *dst_iview =
			cmd_buffer->state.attachments[dst_att.attachment].iview;

		/* Make sure to not clear the depth/stencil attachment after resolves. */
		cmd_buffer->state.attachments[dst_att.attachment].pending_clear_aspects = 0;

		radv_pick_resolve_method_images(cmd_buffer->device,
						src_iview->image,
						src_iview->vk_format,
						dst_iview->image,
						dst_att.layout,
						dst_att.in_render_loop,
						cmd_buffer,
						&resolve_method);

		if ((src_iview->aspect_mask & VK_IMAGE_ASPECT_DEPTH_BIT) &&
		    subpass->depth_resolve_mode != VK_RESOLVE_MODE_NONE_KHR) {
			if (resolve_method == RESOLVE_FRAGMENT) {
				radv_depth_stencil_resolve_subpass_fs(cmd_buffer,
								      VK_IMAGE_ASPECT_DEPTH_BIT,
								      subpass->depth_resolve_mode);
			} else {
				assert(resolve_method == RESOLVE_COMPUTE);
				radv_depth_stencil_resolve_subpass_cs(cmd_buffer,
								      VK_IMAGE_ASPECT_DEPTH_BIT,
								      subpass->depth_resolve_mode);
			}
		}

		if ((src_iview->aspect_mask & VK_IMAGE_ASPECT_STENCIL_BIT) &&
		    subpass->stencil_resolve_mode != VK_RESOLVE_MODE_NONE_KHR) {
			if (resolve_method == RESOLVE_FRAGMENT) {
				radv_depth_stencil_resolve_subpass_fs(cmd_buffer,
								      VK_IMAGE_ASPECT_STENCIL_BIT,
								      subpass->stencil_resolve_mode);
			} else {
				assert(resolve_method == RESOLVE_COMPUTE);
				radv_depth_stencil_resolve_subpass_cs(cmd_buffer,
								      VK_IMAGE_ASPECT_STENCIL_BIT,
								      subpass->stencil_resolve_mode);
			}
		}

		/* From the Vulkan spec 1.2.165:
		 *
		 * "VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT specifies
		 *  write access to a color, resolve, or depth/stencil
		 *  resolve attachment during a render pass or via
		 *  certain subpass load and store operations."
		 *
		 * Yes, it's counterintuitive but it makes sense because ds
		 * resolve operations happen late at the end of the subpass.
		 *
		 * That said, RADV is wrong because it executes the subpass
		 * end barrier *before* any subpass resolves instead of after.
		 *
		 * TODO: Fix this properly by executing subpass end barriers
		 * after subpass resolves.
		 */
		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB;
		if (radv_image_has_htile(dst_iview->image))
			cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB_META;
	}

	if (!subpass->has_color_resolve)
		return;

	for (uint32_t i = 0; i < subpass->color_count; ++i) {
		struct radv_subpass_attachment src_att = subpass->color_attachments[i];
		struct radv_subpass_attachment dest_att = subpass->resolve_attachments[i];

		if (dest_att.attachment == VK_ATTACHMENT_UNUSED)
			continue;

		/* Make sure to not clear color attachments after resolves. */
		cmd_buffer->state.attachments[dest_att.attachment].pending_clear_aspects = 0;

		struct radv_image *dst_img = cmd_buffer->state.attachments[dest_att.attachment].iview->image;
		struct radv_image_view *src_iview= cmd_buffer->state.attachments[src_att.attachment].iview;
		struct radv_image *src_img = src_iview->image;

		radv_pick_resolve_method_images(cmd_buffer->device, src_img,
						src_iview->vk_format, dst_img,
						dest_att.layout,
						dest_att.in_render_loop,
						cmd_buffer, &resolve_method);

		if (resolve_method == RESOLVE_FRAGMENT) {
			break;
		}
	}

	if (resolve_method == RESOLVE_COMPUTE) {
		radv_cmd_buffer_resolve_subpass_cs(cmd_buffer);
		return;
	} else if (resolve_method == RESOLVE_FRAGMENT) {
		radv_cmd_buffer_resolve_subpass_fs(cmd_buffer);
		return;
	}

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_GRAPHICS_PIPELINE);

	for (uint32_t i = 0; i < subpass->color_count; ++i) {
		struct radv_subpass_attachment src_att = subpass->color_attachments[i];
		struct radv_subpass_attachment dest_att = subpass->resolve_attachments[i];

		if (dest_att.attachment == VK_ATTACHMENT_UNUSED)
			continue;

		struct radv_image_view *dest_iview = cmd_buffer->state.attachments[dest_att.attachment].iview;
		struct radv_image *dst_img = dest_iview->image;

		if (radv_dcc_enabled(dst_img, dest_iview->base_mip)) {
			VkImageSubresourceRange range = {
				.aspectMask = dest_iview->aspect_mask,
				.baseMipLevel = dest_iview->base_mip,
				.levelCount = dest_iview->level_count,
				.baseArrayLayer = dest_iview->base_layer,
				.layerCount = dest_iview->layer_count,
			};

			radv_initialize_dcc(cmd_buffer, dst_img, &range, 0xffffffff);
			cmd_buffer->state.attachments[dest_att.attachment].current_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		struct radv_subpass resolve_subpass = {
			.color_count = 2,
			.color_attachments = (struct radv_subpass_attachment[]) { src_att, dest_att },
			.depth_stencil_attachment = NULL,
		};

		radv_cmd_buffer_set_subpass(cmd_buffer, &resolve_subpass);

		VkResult ret = build_resolve_pipeline(cmd_buffer->device, radv_format_meta_fs_key(cmd_buffer->device, dest_iview->vk_format));
		if (ret != VK_SUCCESS) {
			cmd_buffer->record_result = ret;
			continue;
		}

		emit_resolve(cmd_buffer,
			     dest_iview->vk_format,
			     &(VkOffset2D) { 0, 0 },
			     &(VkExtent2D) { fb->width, fb->height });
	}

	radv_cmd_buffer_set_subpass(cmd_buffer, subpass);

	radv_meta_restore(&saved_state, cmd_buffer);
}

/**
 * Decompress CMask/FMask before resolving a multisampled source image inside a
 * subpass.
 */
void
radv_decompress_resolve_subpass_src(struct radv_cmd_buffer *cmd_buffer)
{
	const struct radv_subpass *subpass = cmd_buffer->state.subpass;
	struct radv_framebuffer *fb = cmd_buffer->state.framebuffer;
	uint32_t layer_count = fb->layers;

	if (subpass->view_mask)
		layer_count = util_last_bit(subpass->view_mask);

	for (uint32_t i = 0; i < subpass->color_count; ++i) {
		struct radv_subpass_attachment src_att = subpass->color_attachments[i];
		struct radv_subpass_attachment dest_att = subpass->resolve_attachments[i];

		if (dest_att.attachment == VK_ATTACHMENT_UNUSED)
			continue;

		struct radv_image_view *src_iview = cmd_buffer->state.attachments[src_att.attachment].iview;
		struct radv_image *src_image = src_iview->image;

		VkImageResolve2KHR region = {0};
		region.sType = VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2_KHR;
		region.srcSubresource.aspectMask = src_iview->aspect_mask;
		region.srcSubresource.mipLevel = 0;
		region.srcSubresource.baseArrayLayer = src_iview->base_layer;
		region.srcSubresource.layerCount = layer_count;

		radv_decompress_resolve_src(cmd_buffer, src_image,
					    src_att.layout, &region);
	}

	if (subpass->ds_resolve_attachment) {
		struct radv_subpass_attachment src_att = *subpass->depth_stencil_attachment;
		struct radv_image_view *src_iview = fb->attachments[src_att.attachment];
		struct radv_image *src_image = src_iview->image;

		VkImageResolve2KHR region = {0};
		region.sType = VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2_KHR;
		region.srcSubresource.aspectMask = src_iview->aspect_mask;
		region.srcSubresource.mipLevel = 0;
		region.srcSubresource.baseArrayLayer = src_iview->base_layer;
		region.srcSubresource.layerCount = layer_count;

		radv_decompress_resolve_src(cmd_buffer, src_image,
					    src_att.layout, &region);
	}
}

static struct radv_sample_locations_state *
radv_get_resolve_sample_locations(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_cmd_state *state = &cmd_buffer->state;
	uint32_t subpass_id = radv_get_subpass_id(cmd_buffer);

	for (uint32_t i = 0; i < state->num_subpass_sample_locs; i++) {
		if (state->subpass_sample_locs[i].subpass_idx == subpass_id)
			return &state->subpass_sample_locs[i].sample_location;
	}

	return NULL;
}

/**
 * Decompress CMask/FMask before resolving a multisampled source image.
 */
void
radv_decompress_resolve_src(struct radv_cmd_buffer *cmd_buffer,
			    struct radv_image *src_image,
			    VkImageLayout src_image_layout,
			     const VkImageResolve2KHR *region)
{
	const uint32_t src_base_layer =
		radv_meta_get_iview_layer(src_image, &region->srcSubresource,
					  &region->srcOffset);

	VkImageMemoryBarrier barrier = {0};
	barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
	barrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
	barrier.oldLayout = src_image_layout;
	barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
	barrier.image = radv_image_to_handle(src_image);
	barrier.subresourceRange = (VkImageSubresourceRange) {
		.aspectMask = region->srcSubresource.aspectMask,
		.baseMipLevel = region->srcSubresource.mipLevel,
		.levelCount = 1,
		.baseArrayLayer = src_base_layer,
		.layerCount = region->srcSubresource.layerCount,
	};

	if (src_image->flags & VK_IMAGE_CREATE_SAMPLE_LOCATIONS_COMPATIBLE_DEPTH_BIT_EXT) {
		/* If the depth/stencil image uses different sample
		 * locations, we need them during HTILE decompressions.
		 */
		struct radv_sample_locations_state *sample_locs =
			radv_get_resolve_sample_locations(cmd_buffer);

		barrier.pNext = &(VkSampleLocationsInfoEXT) {
			.sType = VK_STRUCTURE_TYPE_SAMPLE_LOCATIONS_INFO_EXT,
			.sampleLocationsPerPixel = sample_locs->per_pixel,
			.sampleLocationGridSize = sample_locs->grid_size,
			.sampleLocationsCount = sample_locs->count,
			.pSampleLocations = sample_locs->locations,
		};
	}

	radv_CmdPipelineBarrier(radv_cmd_buffer_to_handle(cmd_buffer),
				VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
				VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
				false, 0, NULL, 0, NULL, 1, &barrier);
}
