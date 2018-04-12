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
	nir_builder b;
	nir_variable *f_color; /* vec4, fragment output color */

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_FRAGMENT, NULL);
	b.shader->info.name = ralloc_asprintf(b.shader,
					       "meta_resolve_fs");

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
								.dependencyCount = 0,
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

static VkFormat pipeline_formats[] = {
	VK_FORMAT_R8G8B8A8_UNORM,
	VK_FORMAT_R8G8B8A8_UINT,
	VK_FORMAT_R8G8B8A8_SINT,
	VK_FORMAT_A2R10G10B10_UINT_PACK32,
	VK_FORMAT_A2R10G10B10_SINT_PACK32,
	VK_FORMAT_R16G16B16A16_UNORM,
	VK_FORMAT_R16G16B16A16_SNORM,
	VK_FORMAT_R16G16B16A16_UINT,
	VK_FORMAT_R16G16B16A16_SINT,
	VK_FORMAT_R32_SFLOAT,
	VK_FORMAT_R32G32_SFLOAT,
	VK_FORMAT_R32G32B32A32_SFLOAT
};

VkResult
radv_device_init_meta_resolve_state(struct radv_device *device)
{
	VkResult res = VK_SUCCESS;
	struct radv_meta_state *state = &device->meta_state;
	struct radv_shader_module vs_module = { .nir = radv_meta_build_nir_vs_generate_vertices() };
	if (!vs_module.nir) {
		/* XXX: Need more accurate error */
		res = VK_ERROR_OUT_OF_HOST_MEMORY;
		goto fail;
	}

	for (uint32_t i = 0; i < ARRAY_SIZE(pipeline_formats); ++i) {
		VkFormat format = pipeline_formats[i];
		unsigned fs_key = radv_format_meta_fs_key(format);
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
	unsigned fs_key = radv_format_meta_fs_key(vk_format);

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

static void radv_pick_resolve_method_images(struct radv_image *src_image,
					    struct radv_image *dest_image,
					    VkImageLayout dest_image_layout,
					    struct radv_cmd_buffer *cmd_buffer,
					    enum radv_resolve_method *method)

{
	uint32_t queue_mask = radv_image_queue_family_mask(dest_image,
	                                                   cmd_buffer->queue_family_index,
	                                                   cmd_buffer->queue_family_index);

	if (src_image->vk_format == VK_FORMAT_R16G16_UNORM ||
	    src_image->vk_format == VK_FORMAT_R16G16_SNORM)
		*method = RESOLVE_COMPUTE;
	else if (vk_format_is_int(src_image->vk_format))
		*method = RESOLVE_COMPUTE;
	
	if (radv_layout_dcc_compressed(dest_image, dest_image_layout, queue_mask)) {
		*method = RESOLVE_FRAGMENT;
	} else if (dest_image->surface.micro_tile_mode != src_image->surface.micro_tile_mode) {
		*method = RESOLVE_COMPUTE;
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
	struct radv_device *device = cmd_buffer->device;
	struct radv_meta_saved_state saved_state;
	VkDevice device_h = radv_device_to_handle(device);
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

	radv_pick_resolve_method_images(src_image, dest_image,
					dest_image_layout, cmd_buffer,
					&resolve_method);

	if (resolve_method == RESOLVE_FRAGMENT) {
		radv_meta_resolve_fragment_image(cmd_buffer,
						 src_image,
						 src_image_layout,
						 dest_image,
						 dest_image_layout,
						 region_count, regions);
		return;
	}

	if (resolve_method == RESOLVE_COMPUTE) {
		radv_meta_resolve_compute_image(cmd_buffer,
						src_image,
						src_image_layout,
						dest_image,
						dest_image_layout,
						region_count, regions);
		return;
	}

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_GRAPHICS_PIPELINE);

	assert(src_image->info.samples > 1);
	if (src_image->info.samples <= 1) {
		/* this causes GPU hangs if we get past here */
		fprintf(stderr, "radv: Illegal resolve operation (src not multisampled), will hang GPU.");
		return;
	}
	assert(dest_image->info.samples == 1);

	if (src_image->info.samples >= 16) {
		/* See commit aa3f9aaf31e9056a255f9e0472ebdfdaa60abe54 for the
		 * glBlitFramebuffer workaround for samples >= 16.
		 */
		radv_finishme("vkCmdResolveImage: need interpolation workaround when "
			      "samples >= 16");
	}

	if (src_image->info.array_size > 1)
		radv_finishme("vkCmdResolveImage: multisample array images");

	if (radv_image_has_dcc(dest_image)) {
		radv_initialize_dcc(cmd_buffer, dest_image, 0xffffffff);
	}
	unsigned fs_key = radv_format_meta_fs_key(dest_image->vk_format);
	for (uint32_t r = 0; r < region_count; ++r) {
		const VkImageResolve *region = &regions[r];

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

		const uint32_t dest_base_layer =
			radv_meta_get_iview_layer(dest_image, &region->dstSubresource,
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
			radv_sanitize_image_offset(dest_image->type, region->dstOffset);


		for (uint32_t layer = 0; layer < region->srcSubresource.layerCount;
		     ++layer) {

			struct radv_image_view src_iview;
			radv_image_view_init(&src_iview, cmd_buffer->device,
					     &(VkImageViewCreateInfo) {
						     .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
							     .image = src_image_h,
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
							     .image = dest_image_h,
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

			VkFramebuffer fb_h;
			radv_CreateFramebuffer(device_h,
					       &(VkFramebufferCreateInfo) {
						       .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
							       .attachmentCount = 2,
							       .pAttachments = (VkImageView[]) {
							       radv_image_view_to_handle(&src_iview),
							       radv_image_view_to_handle(&dest_iview),
						       },
						       .width = radv_minify(dest_image->info.width,
									    region->dstSubresource.mipLevel),
						       .height = radv_minify(dest_image->info.height,
									      region->dstSubresource.mipLevel),
						       .layers = 1
					       },
					       &cmd_buffer->pool->alloc,
					       &fb_h);

			radv_CmdBeginRenderPass(cmd_buffer_h,
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
						      },
						      VK_SUBPASS_CONTENTS_INLINE);

			emit_resolve(cmd_buffer,
				     dest_iview.vk_format,
				     &(VkOffset2D) {
					     .x = dstOffset.x,
					     .y = dstOffset.y,
				     },
				     &(VkExtent2D) {
					     .width = extent.width,
					     .height = extent.height,
				     });

			radv_CmdEndRenderPass(cmd_buffer_h);

			radv_DestroyFramebuffer(device_h, fb_h,
						&cmd_buffer->pool->alloc);
		}
	}

	radv_meta_restore(&saved_state, cmd_buffer);
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

	for (uint32_t i = 0; i < subpass->color_count; ++i) {
		VkAttachmentReference src_att = subpass->color_attachments[i];
		VkAttachmentReference dest_att = subpass->resolve_attachments[i];

		if (src_att.attachment == VK_ATTACHMENT_UNUSED ||
		    dest_att.attachment == VK_ATTACHMENT_UNUSED)
			continue;

		struct radv_image *dst_img = cmd_buffer->state.framebuffer->attachments[dest_att.attachment].attachment->image;
		struct radv_image *src_img = cmd_buffer->state.framebuffer->attachments[src_att.attachment].attachment->image;

		radv_pick_resolve_method_images(src_img, dst_img, dest_att.layout, cmd_buffer, &resolve_method);
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
		VkAttachmentReference src_att = subpass->color_attachments[i];
		VkAttachmentReference dest_att = subpass->resolve_attachments[i];

		if (src_att.attachment == VK_ATTACHMENT_UNUSED ||
		    dest_att.attachment == VK_ATTACHMENT_UNUSED)
			continue;

		struct radv_image *dst_img = cmd_buffer->state.framebuffer->attachments[dest_att.attachment].attachment->image;

		if (radv_image_has_dcc(dst_img)) {
			radv_initialize_dcc(cmd_buffer, dst_img, 0xffffffff);
			cmd_buffer->state.attachments[dest_att.attachment].current_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		struct radv_subpass resolve_subpass = {
			.color_count = 2,
			.color_attachments = (VkAttachmentReference[]) { src_att, dest_att },
			.depth_stencil_attachment = { .attachment = VK_ATTACHMENT_UNUSED },
		};

		radv_cmd_buffer_set_subpass(cmd_buffer, &resolve_subpass, false);

		emit_resolve(cmd_buffer,
			     dst_img->vk_format,
			     &(VkOffset2D) { 0, 0 },
			     &(VkExtent2D) { fb->width, fb->height });
	}

	cmd_buffer->state.subpass = subpass;
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

	for (uint32_t i = 0; i < subpass->color_count; ++i) {
		VkAttachmentReference src_att = subpass->color_attachments[i];
		VkAttachmentReference dest_att = subpass->resolve_attachments[i];

		if (src_att.attachment == VK_ATTACHMENT_UNUSED ||
		    dest_att.attachment == VK_ATTACHMENT_UNUSED)
			continue;

		struct radv_image_view *src_iview =
			fb->attachments[src_att.attachment].attachment;

		VkImageSubresourceRange range;
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.baseMipLevel = 0;
		range.levelCount = 1;
		range.baseArrayLayer = 0;
		range.layerCount = 1;

		radv_fast_clear_flush_image_inplace(cmd_buffer,
						    src_iview->image, &range);
	}
}

/**
 * Decompress CMask/FMask before resolving a multisampled source image.
 */
void
radv_decompress_resolve_src(struct radv_cmd_buffer *cmd_buffer,
			    struct radv_image *src_image,
			    uint32_t region_count,
			    const VkImageResolve *regions)
{
	for (uint32_t r = 0; r < region_count; ++r) {
		const VkImageResolve *region = &regions[r];
		const uint32_t src_base_layer =
			radv_meta_get_iview_layer(src_image, &region->srcSubresource,
						  &region->srcOffset);
		VkImageSubresourceRange range;
		range.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		range.baseMipLevel = region->srcSubresource.mipLevel;
		range.levelCount = 1;
		range.baseArrayLayer = src_base_layer;
		range.layerCount = region->srcSubresource.layerCount;

		radv_fast_clear_flush_image_inplace(cmd_buffer, src_image, &range);
	}
}
