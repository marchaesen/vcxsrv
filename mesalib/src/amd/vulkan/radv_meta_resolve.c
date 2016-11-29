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
#include "nir/nir_builder.h"
#include "sid.h"
/**
 * Vertex attributes used by all pipelines.
 */
struct vertex_attrs {
	float position[2]; /**< 3DPRIM_RECTLIST */
};

/* passthrough vertex shader */
static nir_shader *
build_nir_vs(void)
{
	const struct glsl_type *vec4 = glsl_vec4_type();

	nir_builder b;
	nir_variable *a_position;
	nir_variable *v_position;

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_VERTEX, NULL);
	b.shader->info->name = ralloc_strdup(b.shader, "meta_resolve_vs");

	a_position = nir_variable_create(b.shader, nir_var_shader_in, vec4,
					 "a_position");
	a_position->data.location = VERT_ATTRIB_GENERIC0;

	v_position = nir_variable_create(b.shader, nir_var_shader_out, vec4,
					 "gl_Position");
	v_position->data.location = VARYING_SLOT_POS;

	nir_copy_var(&b, v_position, a_position);

	return b.shader;
}

/* simple passthrough shader */
static nir_shader *
build_nir_fs(void)
{
	const struct glsl_type *vec4 = glsl_vec4_type();
	nir_builder b;
	nir_variable *f_color; /* vec4, fragment output color */

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_FRAGMENT, NULL);
	b.shader->info->name = ralloc_asprintf(b.shader,
					       "meta_resolve_fs");

	f_color = nir_variable_create(b.shader, nir_var_shader_out, vec4,
				      "f_color");
	f_color->data.location = FRAG_RESULT_DATA0;
	nir_store_var(&b, f_color, nir_imm_vec4(&b, 0.0, 0.0, 0.0, 1.0), 0xf);

	return b.shader;
}

static VkResult
create_pass(struct radv_device *device)
{
	VkResult result;
	VkDevice device_h = radv_device_to_handle(device);
	const VkAllocationCallbacks *alloc = &device->meta_state.alloc;
	VkAttachmentDescription attachments[2];
	int i;

	for (i = 0; i < 2; i++) {
		attachments[i].format = VK_FORMAT_UNDEFINED;
		attachments[i].samples = 1;
		attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
		attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		attachments[i].initialLayout = VK_IMAGE_LAYOUT_GENERAL;
		attachments[i].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
	}

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
				       &device->meta_state.resolve.pass);

	return result;
}

static VkResult
create_pipeline(struct radv_device *device,
                VkShaderModule vs_module_h)
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
						       .vertexBindingDescriptionCount = 1,
						       .pVertexBindingDescriptions = (VkVertexInputBindingDescription[]) {
							       {
								       .binding = 0,
								       .stride = sizeof(struct vertex_attrs),
								       .inputRate = VK_VERTEX_INPUT_RATE_VERTEX
							       },
						       },
						       .vertexAttributeDescriptionCount = 1,
						       .pVertexAttributeDescriptions = (VkVertexInputAttributeDescription[]) {
							       {
								       /* Position */
								       .location = 0,
								       .binding = 0,
								       .format = VK_FORMAT_R32G32_SFLOAT,
								       .offset = offsetof(struct vertex_attrs, position),
							       },
						       },
					       },
					       .pInputAssemblyState = &(VkPipelineInputAssemblyStateCreateInfo) {
						       .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
						       .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
						       .primitiveRestartEnable = false,
					       },
					       .pViewportState = &(VkPipelineViewportStateCreateInfo) {
						       .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
						       .viewportCount = 0,
						       .scissorCount = 0,
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
						  .pDynamicState = NULL,
																       .renderPass = device->meta_state.resolve.pass,
																       .subpass = 0,
																       },
					       &(struct radv_graphics_pipeline_create_info) {
						       .use_rectlist = true,
						       .custom_blend_mode = V_028808_CB_RESOLVE,
							       },
					       &device->meta_state.alloc,
					       &device->meta_state.resolve.pipeline);
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
	VkDevice device_h = radv_device_to_handle(device);
	VkRenderPass pass_h = device->meta_state.resolve.pass;
	const VkAllocationCallbacks *alloc = &device->meta_state.alloc;

	if (pass_h)
		radv_DestroyRenderPass(device_h, pass_h,
					     &device->meta_state.alloc);

	VkPipeline pipeline_h = state->resolve.pipeline;
	if (pipeline_h) {
		radv_DestroyPipeline(device_h, pipeline_h, alloc);
	}
}

VkResult
radv_device_init_meta_resolve_state(struct radv_device *device)
{
	VkResult res = VK_SUCCESS;

	zero(device->meta_state.resolve);

	struct radv_shader_module vs_module = { .nir = build_nir_vs() };
	if (!vs_module.nir) {
		/* XXX: Need more accurate error */
		res = VK_ERROR_OUT_OF_HOST_MEMORY;
		goto fail;
	}

	res = create_pass(device);
	if (res != VK_SUCCESS)
		goto fail;

	VkShaderModule vs_module_h = radv_shader_module_to_handle(&vs_module);
	res = create_pipeline(device, vs_module_h);
	if (res != VK_SUCCESS)
		goto fail;

	goto cleanup;

fail:
	radv_device_finish_meta_resolve_state(device);

cleanup:
	ralloc_free(vs_module.nir);

	return res;
}

static void
emit_resolve(struct radv_cmd_buffer *cmd_buffer,
             const VkOffset2D *dest_offset,
             const VkExtent2D *resolve_extent)
{
	struct radv_device *device = cmd_buffer->device;
	VkCommandBuffer cmd_buffer_h = radv_cmd_buffer_to_handle(cmd_buffer);
	uint32_t offset;
	const struct vertex_attrs vertex_data[3] = {
		{
			.position = {
				dest_offset->x,
				dest_offset->y,
			},
		},
		{
			.position = {
				dest_offset->x,
				dest_offset->y + resolve_extent->height,
			},
		},
		{
			.position = {
				dest_offset->x + resolve_extent->width,
				dest_offset->y,
			},
		},
	};

	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB;
	radv_cmd_buffer_upload_data(cmd_buffer, sizeof(vertex_data), 16, vertex_data, &offset);
	struct radv_buffer vertex_buffer = {
		.device = device,
		.size = sizeof(vertex_data),
		.bo = cmd_buffer->upload.upload_bo,
		.offset = offset,
	};

	VkBuffer vertex_buffer_h = radv_buffer_to_handle(&vertex_buffer);

	radv_CmdBindVertexBuffers(cmd_buffer_h,
				  /*firstBinding*/ 0,
				  /*bindingCount*/ 1,
				  (VkBuffer[]) { vertex_buffer_h },
				  (VkDeviceSize[]) { 0 });

	VkPipeline pipeline_h = device->meta_state.resolve.pipeline;
	RADV_FROM_HANDLE(radv_pipeline, pipeline, pipeline_h);

	if (cmd_buffer->state.pipeline != pipeline) {
		radv_CmdBindPipeline(cmd_buffer_h, VK_PIPELINE_BIND_POINT_GRAPHICS,
				     pipeline_h);
	}

	radv_CmdDraw(cmd_buffer_h, 3, 1, 0, 0);
	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB;
	si_emit_cache_flush(cmd_buffer);
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
	bool use_compute_resolve = false;

	/* we can use the hw resolve only for single full resolves */
	if (region_count == 1) {
		if (regions[0].srcOffset.x ||
		    regions[0].srcOffset.y ||
		    regions[0].srcOffset.z)
			use_compute_resolve = true;
		if (regions[0].dstOffset.x ||
		    regions[0].dstOffset.y ||
		    regions[0].dstOffset.z)
			use_compute_resolve = true;

		if (regions[0].extent.width != src_image->extent.width ||
		    regions[0].extent.height != src_image->extent.height ||
		    regions[0].extent.depth != src_image->extent.depth)
			use_compute_resolve = true;
	} else
		use_compute_resolve = true;

	if (use_compute_resolve) {

		radv_fast_clear_flush_image_inplace(cmd_buffer, src_image);
		radv_meta_resolve_compute_image(cmd_buffer,
						src_image,
						src_image_layout,
						dest_image,
						dest_image_layout,
						region_count, regions);
		return;
	}

	radv_meta_save_graphics_reset_vport_scissor(&saved_state, cmd_buffer);

	assert(src_image->samples > 1);
	assert(dest_image->samples == 1);

	if (src_image->samples >= 16) {
		/* See commit aa3f9aaf31e9056a255f9e0472ebdfdaa60abe54 for the
		 * glBlitFramebuffer workaround for samples >= 16.
		 */
		radv_finishme("vkCmdResolveImage: need interpolation workaround when "
			      "samples >= 16");
	}

	if (src_image->array_size > 1)
		radv_finishme("vkCmdResolveImage: multisample array images");

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
							     },
					     cmd_buffer, VK_IMAGE_USAGE_SAMPLED_BIT);

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
							     },
					     cmd_buffer, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

			VkFramebuffer fb_h;
			radv_CreateFramebuffer(device_h,
					       &(VkFramebufferCreateInfo) {
						       .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
							       .attachmentCount = 2,
							       .pAttachments = (VkImageView[]) {
							       radv_image_view_to_handle(&src_iview),
							       radv_image_view_to_handle(&dest_iview),
						       },
						       .width = radv_minify(dest_image->extent.width,
									    region->dstSubresource.mipLevel),
						       .height = radv_minify(dest_image->extent.height,
									      region->dstSubresource.mipLevel),
						       .layers = 1
					       },
					       &cmd_buffer->pool->alloc,
					       &fb_h);

			radv_CmdBeginRenderPass(cmd_buffer_h,
						      &(VkRenderPassBeginInfo) {
							      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
								      .renderPass = device->meta_state.resolve.pass,
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

	radv_meta_save_graphics_reset_vport_scissor(&saved_state, cmd_buffer);

	for (uint32_t i = 0; i < subpass->color_count; ++i) {
		VkAttachmentReference src_att = subpass->color_attachments[i];
		VkAttachmentReference dest_att = subpass->resolve_attachments[i];
		struct radv_image *dst_img = cmd_buffer->state.framebuffer->attachments[dest_att.attachment].attachment->image;
		if (dest_att.attachment == VK_ATTACHMENT_UNUSED)
			continue;

		if (dst_img->surface.dcc_size) {
			radv_initialize_dcc(cmd_buffer, dst_img, 0xffffffff);
			cmd_buffer->state.attachments[dest_att.attachment].current_layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		}

		struct radv_subpass resolve_subpass = {
			.color_count = 2,
			.color_attachments = (VkAttachmentReference[]) { src_att, dest_att },
			.depth_stencil_attachment = { .attachment = VK_ATTACHMENT_UNUSED },
		};

		radv_cmd_buffer_set_subpass(cmd_buffer, &resolve_subpass, false);

		/* Subpass resolves must respect the render area. We can ignore the
		 * render area here because vkCmdBeginRenderPass set the render area
		 * with 3DSTATE_DRAWING_RECTANGLE.
		 *
		 * XXX(chadv): Does the hardware really respect
		 * 3DSTATE_DRAWING_RECTANGLE when draing a 3DPRIM_RECTLIST?
		 */
		emit_resolve(cmd_buffer,
			     &(VkOffset2D) { 0, 0 },
			     &(VkExtent2D) { fb->width, fb->height });
	}

	cmd_buffer->state.subpass = subpass;
	radv_meta_restore(&saved_state, cmd_buffer);
}
