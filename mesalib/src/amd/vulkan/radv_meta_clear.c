/*
 * Copyright © 2015 Intel Corporation
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

#include "radv_meta.h"
#include "radv_private.h"
#include "nir/nir_builder.h"

#include "util/format_rgb9e5.h"
#include "vk_format.h"
/** Vertex attributes for color clears.  */
struct color_clear_vattrs {
	float position[2];
	VkClearColorValue color;
};

/** Vertex attributes for depthstencil clears.  */
struct depthstencil_clear_vattrs {
	float position[2];
	float depth_clear;
};

enum {
	DEPTH_CLEAR_SLOW,
	DEPTH_CLEAR_FAST_EXPCLEAR,
	DEPTH_CLEAR_FAST_NO_EXPCLEAR
};

static void
build_color_shaders(struct nir_shader **out_vs,
                    struct nir_shader **out_fs,
                    uint32_t frag_output)
{
	nir_builder vs_b;
	nir_builder fs_b;

	nir_builder_init_simple_shader(&vs_b, NULL, MESA_SHADER_VERTEX, NULL);
	nir_builder_init_simple_shader(&fs_b, NULL, MESA_SHADER_FRAGMENT, NULL);

	vs_b.shader->info->name = ralloc_strdup(vs_b.shader, "meta_clear_color_vs");
	fs_b.shader->info->name = ralloc_strdup(fs_b.shader, "meta_clear_color_fs");

	const struct glsl_type *position_type = glsl_vec4_type();
	const struct glsl_type *color_type = glsl_vec4_type();

	nir_variable *vs_in_pos =
		nir_variable_create(vs_b.shader, nir_var_shader_in, position_type,
				    "a_position");
	vs_in_pos->data.location = VERT_ATTRIB_GENERIC0;

	nir_variable *vs_out_pos =
		nir_variable_create(vs_b.shader, nir_var_shader_out, position_type,
				    "gl_Position");
	vs_out_pos->data.location = VARYING_SLOT_POS;

	nir_variable *vs_in_color =
		nir_variable_create(vs_b.shader, nir_var_shader_in, color_type,
				    "a_color");
	vs_in_color->data.location = VERT_ATTRIB_GENERIC1;

	nir_variable *vs_out_color =
		nir_variable_create(vs_b.shader, nir_var_shader_out, color_type,
				    "v_color");
	vs_out_color->data.location = VARYING_SLOT_VAR0;
	vs_out_color->data.interpolation = INTERP_MODE_FLAT;

	nir_variable *fs_in_color =
		nir_variable_create(fs_b.shader, nir_var_shader_in, color_type,
				    "v_color");
	fs_in_color->data.location = vs_out_color->data.location;
	fs_in_color->data.interpolation = vs_out_color->data.interpolation;

	nir_variable *fs_out_color =
		nir_variable_create(fs_b.shader, nir_var_shader_out, color_type,
				    "f_color");
	fs_out_color->data.location = FRAG_RESULT_DATA0 + frag_output;

	nir_copy_var(&vs_b, vs_out_pos, vs_in_pos);
	nir_copy_var(&vs_b, vs_out_color, vs_in_color);
	nir_copy_var(&fs_b, fs_out_color, fs_in_color);

	const struct glsl_type *layer_type = glsl_int_type();
	nir_variable *vs_out_layer =
		nir_variable_create(vs_b.shader, nir_var_shader_out, layer_type,
				    "v_layer");
	vs_out_layer->data.location = VARYING_SLOT_LAYER;
	vs_out_layer->data.interpolation = INTERP_MODE_FLAT;
	nir_ssa_def *inst_id = nir_load_system_value(&vs_b, nir_intrinsic_load_instance_id, 0);

	nir_store_var(&vs_b, vs_out_layer, inst_id, 0x1);

	*out_vs = vs_b.shader;
	*out_fs = fs_b.shader;
}

static VkResult
create_pipeline(struct radv_device *device,
		struct radv_render_pass *render_pass,
		uint32_t samples,
                struct nir_shader *vs_nir,
                struct nir_shader *fs_nir,
                const VkPipelineVertexInputStateCreateInfo *vi_state,
                const VkPipelineDepthStencilStateCreateInfo *ds_state,
                const VkPipelineColorBlendStateCreateInfo *cb_state,
		const struct radv_graphics_pipeline_create_info *extra,
                const VkAllocationCallbacks *alloc,
                struct radv_pipeline **pipeline)
{
	VkDevice device_h = radv_device_to_handle(device);
	VkResult result;

	struct radv_shader_module vs_m = { .nir = vs_nir };
	struct radv_shader_module fs_m = { .nir = fs_nir };

	VkPipeline pipeline_h = VK_NULL_HANDLE;
	result = radv_graphics_pipeline_create(device_h,
					       radv_pipeline_cache_to_handle(&device->meta_state.cache),
					       &(VkGraphicsPipelineCreateInfo) {
						       .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
							       .stageCount = fs_nir ? 2 : 1,
							       .pStages = (VkPipelineShaderStageCreateInfo[]) {
							       {
								       .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
								       .stage = VK_SHADER_STAGE_VERTEX_BIT,
								       .module = radv_shader_module_to_handle(&vs_m),
								       .pName = "main",
							       },
							       {
								       .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
								       .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
								       .module = radv_shader_module_to_handle(&fs_m),
								       .pName = "main",
							       },
						       },
							       .pVertexInputState = vi_state,
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
							       .rasterizerDiscardEnable = false,
							       .polygonMode = VK_POLYGON_MODE_FILL,
							       .cullMode = VK_CULL_MODE_NONE,
							       .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
							       .depthBiasEnable = false,
						       },
											  .pMultisampleState = &(VkPipelineMultisampleStateCreateInfo) {
							       .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
							       .rasterizationSamples = samples,
							       .sampleShadingEnable = false,
							       .pSampleMask = NULL,
							       .alphaToCoverageEnable = false,
							       .alphaToOneEnable = false,
						       },
												   .pDepthStencilState = ds_state,
													    .pColorBlendState = cb_state,
													    .pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
							       /* The meta clear pipeline declares all state as dynamic.
								* As a consequence, vkCmdBindPipeline writes no dynamic state
								* to the cmd buffer. Therefore, at the end of the meta clear,
								* we need only restore dynamic state was vkCmdSet.
								*/
							       .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
							       .dynamicStateCount = 6,
							       .pDynamicStates = (VkDynamicState[]) {
								       /* Everything except stencil write mask */
								       VK_DYNAMIC_STATE_LINE_WIDTH,
								       VK_DYNAMIC_STATE_DEPTH_BIAS,
								       VK_DYNAMIC_STATE_BLEND_CONSTANTS,
								       VK_DYNAMIC_STATE_DEPTH_BOUNDS,
								       VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
								       VK_DYNAMIC_STATE_STENCIL_REFERENCE,
							       },
						       },
													    .flags = 0,
														     .renderPass = radv_render_pass_to_handle(render_pass),
														     .subpass = 0,
														     },
					       extra,
					       alloc,
					       &pipeline_h);

	ralloc_free(vs_nir);
	ralloc_free(fs_nir);

	*pipeline = radv_pipeline_from_handle(pipeline_h);

	return result;
}

static VkResult
create_color_renderpass(struct radv_device *device,
			VkFormat vk_format,
			uint32_t samples,
			VkRenderPass *pass)
{
	return radv_CreateRenderPass(radv_device_to_handle(device),
				       &(VkRenderPassCreateInfo) {
					       .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
						       .attachmentCount = 1,
						       .pAttachments = &(VkAttachmentDescription) {
						       .format = vk_format,
						       .samples = samples,
						       .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
						       .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
						       .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
						       .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
					       },
						       .subpassCount = 1,
								.pSubpasses = &(VkSubpassDescription) {
						       .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
						       .inputAttachmentCount = 0,
						       .colorAttachmentCount = 1,
						       .pColorAttachments = &(VkAttachmentReference) {
							       .attachment = 0,
							       .layout = VK_IMAGE_LAYOUT_GENERAL,
						       },
						       .pResolveAttachments = NULL,
						       .pDepthStencilAttachment = &(VkAttachmentReference) {
							       .attachment = VK_ATTACHMENT_UNUSED,
							       .layout = VK_IMAGE_LAYOUT_GENERAL,
						       },
						       .preserveAttachmentCount = 1,
						       .pPreserveAttachments = (uint32_t[]) { 0 },
					       },
								.dependencyCount = 0,
									 }, &device->meta_state.alloc, pass);
}

static VkResult
create_color_pipeline(struct radv_device *device,
		      uint32_t samples,
                      uint32_t frag_output,
                      struct radv_pipeline **pipeline,
		      VkRenderPass pass)
{
	struct nir_shader *vs_nir;
	struct nir_shader *fs_nir;
	VkResult result;
	build_color_shaders(&vs_nir, &fs_nir, frag_output);

	const VkPipelineVertexInputStateCreateInfo vi_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = (VkVertexInputBindingDescription[]) {
			{
				.binding = 0,
				.stride = sizeof(struct color_clear_vattrs),
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
			},
		},
		.vertexAttributeDescriptionCount = 2,
		.pVertexAttributeDescriptions = (VkVertexInputAttributeDescription[]) {
			{
				/* Position */
				.location = 0,
				.binding = 0,
				.format = VK_FORMAT_R32G32_SFLOAT,
				.offset = offsetof(struct color_clear_vattrs, position),
			},
			{
				/* Color */
				.location = 1,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32A32_SFLOAT,
				.offset = offsetof(struct color_clear_vattrs, color),
			},
		},
	};

	const VkPipelineDepthStencilStateCreateInfo ds_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = false,
		.depthWriteEnable = false,
		.depthBoundsTestEnable = false,
		.stencilTestEnable = false,
	};

	VkPipelineColorBlendAttachmentState blend_attachment_state[MAX_RTS] = { 0 };
	blend_attachment_state[frag_output] = (VkPipelineColorBlendAttachmentState) {
		.blendEnable = false,
		.colorWriteMask = VK_COLOR_COMPONENT_A_BIT |
		VK_COLOR_COMPONENT_R_BIT |
		VK_COLOR_COMPONENT_G_BIT |
		VK_COLOR_COMPONENT_B_BIT,
	};

	const VkPipelineColorBlendStateCreateInfo cb_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = false,
		.attachmentCount = MAX_RTS,
		.pAttachments = blend_attachment_state
	};


	struct radv_graphics_pipeline_create_info extra = {
		.use_rectlist = true,
	};
	result = create_pipeline(device, radv_render_pass_from_handle(pass),
				 samples, vs_nir, fs_nir, &vi_state, &ds_state, &cb_state,
				 &extra, &device->meta_state.alloc, pipeline);

	return result;
}

static void
destroy_pipeline(struct radv_device *device, struct radv_pipeline *pipeline)
{
	if (!pipeline)
		return;

	radv_DestroyPipeline(radv_device_to_handle(device),
				   radv_pipeline_to_handle(pipeline),
				   &device->meta_state.alloc);

}

static void
destroy_render_pass(struct radv_device *device, VkRenderPass renderpass)
{
	radv_DestroyRenderPass(radv_device_to_handle(device), renderpass,
				     &device->meta_state.alloc);
}

void
radv_device_finish_meta_clear_state(struct radv_device *device)
{
	struct radv_meta_state *state = &device->meta_state;

	for (uint32_t i = 0; i < ARRAY_SIZE(state->clear); ++i) {
		for (uint32_t j = 0; j < ARRAY_SIZE(state->clear[i].color_pipelines); ++j) {
			destroy_pipeline(device, state->clear[i].color_pipelines[j]);
			destroy_render_pass(device, state->clear[i].render_pass[j]);
		}

		for (uint32_t j = 0; j < NUM_DEPTH_CLEAR_PIPELINES; j++) {
			destroy_pipeline(device, state->clear[i].depth_only_pipeline[j]);
			destroy_pipeline(device, state->clear[i].stencil_only_pipeline[j]);
			destroy_pipeline(device, state->clear[i].depthstencil_pipeline[j]);
		}
		destroy_render_pass(device, state->clear[i].depthstencil_rp);
	}

}

static void
emit_color_clear(struct radv_cmd_buffer *cmd_buffer,
                 const VkClearAttachment *clear_att,
                 const VkClearRect *clear_rect)
{
	struct radv_device *device = cmd_buffer->device;
	const struct radv_subpass *subpass = cmd_buffer->state.subpass;
	const struct radv_framebuffer *fb = cmd_buffer->state.framebuffer;
	const uint32_t subpass_att = clear_att->colorAttachment;
	const uint32_t pass_att = subpass->color_attachments[subpass_att].attachment;
	const struct radv_image_view *iview = fb->attachments[pass_att].attachment;
	const uint32_t samples = iview->image->samples;
	const uint32_t samples_log2 = ffs(samples) - 1;
	unsigned fs_key = radv_format_meta_fs_key(iview->vk_format);
	struct radv_pipeline *pipeline;
	VkClearColorValue clear_value = clear_att->clearValue.color;
	VkCommandBuffer cmd_buffer_h = radv_cmd_buffer_to_handle(cmd_buffer);
	VkPipeline pipeline_h;
	uint32_t offset;

	if (fs_key == -1) {
		radv_finishme("color clears incomplete");
		return;
	}
	pipeline = device->meta_state.clear[samples_log2].color_pipelines[fs_key];
	pipeline_h = radv_pipeline_to_handle(pipeline);

	if (!pipeline) {
		radv_finishme("color clears incomplete");
		return;
	}
	assert(samples_log2 < ARRAY_SIZE(device->meta_state.clear));
	assert(pipeline);
	assert(clear_att->aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
	assert(clear_att->colorAttachment < subpass->color_count);

	const struct color_clear_vattrs vertex_data[3] = {
		{
			.position = {
				clear_rect->rect.offset.x,
				clear_rect->rect.offset.y,
			},
			.color = clear_value,
		},
		{
			.position = {
				clear_rect->rect.offset.x,
				clear_rect->rect.offset.y + clear_rect->rect.extent.height,
			},
			.color = clear_value,
		},
		{
			.position = {
				clear_rect->rect.offset.x + clear_rect->rect.extent.width,
				clear_rect->rect.offset.y,
			},
			.color = clear_value,
		},
	};

	struct radv_subpass clear_subpass = {
		.color_count = 1,
		.color_attachments = (VkAttachmentReference[]) {
			subpass->color_attachments[clear_att->colorAttachment]
		},
		.depth_stencil_attachment = (VkAttachmentReference) { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED }
	};

	radv_cmd_buffer_set_subpass(cmd_buffer, &clear_subpass, false);

	radv_cmd_buffer_upload_data(cmd_buffer, sizeof(vertex_data), 16, vertex_data, &offset);
	struct radv_buffer vertex_buffer = {
		.device = device,
		.size = sizeof(vertex_data),
		.bo = cmd_buffer->upload.upload_bo,
		.offset = offset,
	};


	radv_CmdBindVertexBuffers(cmd_buffer_h, 0, 1,
					(VkBuffer[]) { radv_buffer_to_handle(&vertex_buffer) },
					(VkDeviceSize[]) { 0 });

	if (cmd_buffer->state.pipeline != pipeline) {
		radv_CmdBindPipeline(cmd_buffer_h, VK_PIPELINE_BIND_POINT_GRAPHICS,
					   pipeline_h);
	}

	radv_CmdDraw(cmd_buffer_h, 3, clear_rect->layerCount, 0, 0);

	radv_cmd_buffer_set_subpass(cmd_buffer, subpass, false);
}


static void
build_depthstencil_shader(struct nir_shader **out_vs, struct nir_shader **out_fs)
{
	nir_builder vs_b, fs_b;

	nir_builder_init_simple_shader(&vs_b, NULL, MESA_SHADER_VERTEX, NULL);
	nir_builder_init_simple_shader(&fs_b, NULL, MESA_SHADER_FRAGMENT, NULL);

	vs_b.shader->info->name = ralloc_strdup(vs_b.shader, "meta_clear_depthstencil_vs");
	fs_b.shader->info->name = ralloc_strdup(fs_b.shader, "meta_clear_depthstencil_fs");
	const struct glsl_type *position_type = glsl_vec4_type();

	nir_variable *vs_in_pos =
		nir_variable_create(vs_b.shader, nir_var_shader_in, position_type,
				    "a_position");
	vs_in_pos->data.location = VERT_ATTRIB_GENERIC0;

	nir_variable *vs_out_pos =
		nir_variable_create(vs_b.shader, nir_var_shader_out, position_type,
				    "gl_Position");
	vs_out_pos->data.location = VARYING_SLOT_POS;

	nir_copy_var(&vs_b, vs_out_pos, vs_in_pos);

	const struct glsl_type *layer_type = glsl_int_type();
	nir_variable *vs_out_layer =
		nir_variable_create(vs_b.shader, nir_var_shader_out, layer_type,
				    "v_layer");
	vs_out_layer->data.location = VARYING_SLOT_LAYER;
	vs_out_layer->data.interpolation = INTERP_MODE_FLAT;
	nir_ssa_def *inst_id = nir_load_system_value(&vs_b, nir_intrinsic_load_instance_id, 0);
	nir_store_var(&vs_b, vs_out_layer, inst_id, 0x1);

	*out_vs = vs_b.shader;
	*out_fs = fs_b.shader;
}

static VkResult
create_depthstencil_renderpass(struct radv_device *device,
			       uint32_t samples,
			       VkRenderPass *render_pass)
{
	return radv_CreateRenderPass(radv_device_to_handle(device),
				       &(VkRenderPassCreateInfo) {
					       .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
						       .attachmentCount = 1,
						       .pAttachments = &(VkAttachmentDescription) {
						       .format = VK_FORMAT_UNDEFINED,
						       .samples = samples,
						       .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
						       .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
						       .initialLayout = VK_IMAGE_LAYOUT_GENERAL,
						       .finalLayout = VK_IMAGE_LAYOUT_GENERAL,
					       },
						       .subpassCount = 1,
								.pSubpasses = &(VkSubpassDescription) {
						       .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
						       .inputAttachmentCount = 0,
						       .colorAttachmentCount = 0,
						       .pColorAttachments = NULL,
						       .pResolveAttachments = NULL,
						       .pDepthStencilAttachment = &(VkAttachmentReference) {
							       .attachment = 0,
							       .layout = VK_IMAGE_LAYOUT_GENERAL,
						       },
						       .preserveAttachmentCount = 1,
						       .pPreserveAttachments = (uint32_t[]) { 0 },
					       },
								.dependencyCount = 0,
									 }, &device->meta_state.alloc, render_pass);
}

static VkResult
create_depthstencil_pipeline(struct radv_device *device,
                             VkImageAspectFlags aspects,
			     uint32_t samples,
			     int index,
                             struct radv_pipeline **pipeline,
			     VkRenderPass render_pass)
{
	struct nir_shader *vs_nir, *fs_nir;
	VkResult result;
	build_depthstencil_shader(&vs_nir, &fs_nir);

	const VkPipelineVertexInputStateCreateInfo vi_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = (VkVertexInputBindingDescription[]) {
			{
				.binding = 0,
				.stride = sizeof(struct depthstencil_clear_vattrs),
				.inputRate = VK_VERTEX_INPUT_RATE_VERTEX
			},
		},
		.vertexAttributeDescriptionCount = 1,
		.pVertexAttributeDescriptions = (VkVertexInputAttributeDescription[]) {
			{
				/* Position */
				.location = 0,
				.binding = 0,
				.format = VK_FORMAT_R32G32B32_SFLOAT,
				.offset = offsetof(struct depthstencil_clear_vattrs, position),
			},
		},
	};

	const VkPipelineDepthStencilStateCreateInfo ds_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = (aspects & VK_IMAGE_ASPECT_DEPTH_BIT),
		.depthCompareOp = VK_COMPARE_OP_ALWAYS,
		.depthWriteEnable = (aspects & VK_IMAGE_ASPECT_DEPTH_BIT),
		.depthBoundsTestEnable = false,
		.stencilTestEnable = (aspects & VK_IMAGE_ASPECT_STENCIL_BIT),
		.front = {
			.passOp = VK_STENCIL_OP_REPLACE,
			.compareOp = VK_COMPARE_OP_ALWAYS,
			.writeMask = UINT32_MAX,
			.reference = 0, /* dynamic */
		},
		.back = { 0 /* dont care */ },
	};

	const VkPipelineColorBlendStateCreateInfo cb_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = false,
		.attachmentCount = 0,
		.pAttachments = NULL,
	};

	struct radv_graphics_pipeline_create_info extra = {
		.use_rectlist = true,
	};

	if (aspects & VK_IMAGE_ASPECT_DEPTH_BIT) {
		extra.db_depth_clear = index == DEPTH_CLEAR_SLOW ? false : true;
		extra.db_depth_disable_expclear = index == DEPTH_CLEAR_FAST_NO_EXPCLEAR ? true : false;
	}
	if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
		extra.db_stencil_clear = index == DEPTH_CLEAR_SLOW ? false : true;
		extra.db_stencil_disable_expclear = index == DEPTH_CLEAR_FAST_NO_EXPCLEAR ? true : false;
	}
	result = create_pipeline(device, radv_render_pass_from_handle(render_pass),
				 samples, vs_nir, fs_nir, &vi_state, &ds_state, &cb_state,
				 &extra, &device->meta_state.alloc, pipeline);
	return result;
}

static bool depth_view_can_fast_clear(const struct radv_image_view *iview,
				      VkImageLayout layout,
				      const VkClearRect *clear_rect)
{
	if (clear_rect->rect.offset.x || clear_rect->rect.offset.y ||
	    clear_rect->rect.extent.width != iview->extent.width ||
	    clear_rect->rect.extent.height != iview->extent.height)
		return false;
	if (iview->image->htile.size &&
	    iview->base_mip == 0 &&
	    iview->base_layer == 0 &&
	    radv_layout_can_expclear(iview->image, layout) &&
	    memcmp(&iview->extent, &iview->image->extent, sizeof(iview->extent)) == 0)
		return true;
	return false;
}

static struct radv_pipeline *
pick_depthstencil_pipeline(struct radv_meta_state *meta_state,
			   const struct radv_image_view *iview,
			   int samples_log2,
			   VkImageAspectFlags aspects,
			   VkImageLayout layout,
			   const VkClearRect *clear_rect,
			   VkClearDepthStencilValue clear_value)
{
	bool fast = depth_view_can_fast_clear(iview, layout, clear_rect);
	int index = DEPTH_CLEAR_SLOW;

	if (fast) {
		/* we don't know the previous clear values, so we always have
		 * the NO_EXPCLEAR path */
		index = DEPTH_CLEAR_FAST_NO_EXPCLEAR;
	}

	switch (aspects) {
	case VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT:
		return meta_state->clear[samples_log2].depthstencil_pipeline[index];
	case VK_IMAGE_ASPECT_DEPTH_BIT:
		return meta_state->clear[samples_log2].depth_only_pipeline[index];
	case VK_IMAGE_ASPECT_STENCIL_BIT:
		return meta_state->clear[samples_log2].stencil_only_pipeline[index];
	}
	unreachable("expected depth or stencil aspect");
}

static void
emit_depthstencil_clear(struct radv_cmd_buffer *cmd_buffer,
                        const VkClearAttachment *clear_att,
                        const VkClearRect *clear_rect)
{
	struct radv_device *device = cmd_buffer->device;
	struct radv_meta_state *meta_state = &device->meta_state;
	const struct radv_subpass *subpass = cmd_buffer->state.subpass;
	const struct radv_framebuffer *fb = cmd_buffer->state.framebuffer;
	const uint32_t pass_att = subpass->depth_stencil_attachment.attachment;
	VkClearDepthStencilValue clear_value = clear_att->clearValue.depthStencil;
	VkImageAspectFlags aspects = clear_att->aspectMask;
	const struct radv_image_view *iview = fb->attachments[pass_att].attachment;
	const uint32_t samples = iview->image->samples;
	const uint32_t samples_log2 = ffs(samples) - 1;
	VkCommandBuffer cmd_buffer_h = radv_cmd_buffer_to_handle(cmd_buffer);
	uint32_t offset;

	assert(aspects == VK_IMAGE_ASPECT_DEPTH_BIT ||
	       aspects == VK_IMAGE_ASPECT_STENCIL_BIT ||
	       aspects == (VK_IMAGE_ASPECT_DEPTH_BIT |
			   VK_IMAGE_ASPECT_STENCIL_BIT));
	assert(pass_att != VK_ATTACHMENT_UNUSED);

	const struct depthstencil_clear_vattrs vertex_data[3] = {
		{
			.position = {
				clear_rect->rect.offset.x,
				clear_rect->rect.offset.y,
			},
			.depth_clear = clear_value.depth,
		},
		{
			.position = {
				clear_rect->rect.offset.x,
				clear_rect->rect.offset.y + clear_rect->rect.extent.height,
			},
			.depth_clear = clear_value.depth,
		},
		{
			.position = {
				clear_rect->rect.offset.x + clear_rect->rect.extent.width,
				clear_rect->rect.offset.y,
			},
			.depth_clear = clear_value.depth,
		},
	};

	radv_cmd_buffer_upload_data(cmd_buffer, sizeof(vertex_data), 16, vertex_data, &offset);
	struct radv_buffer vertex_buffer = {
		.device = device,
		.size = sizeof(vertex_data),
		.bo = cmd_buffer->upload.upload_bo,
		.offset = offset,
	};

	if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
		radv_CmdSetStencilReference(cmd_buffer_h, VK_STENCIL_FACE_FRONT_BIT,
						  clear_value.stencil);
	}

	radv_CmdBindVertexBuffers(cmd_buffer_h, 0, 1,
					(VkBuffer[]) { radv_buffer_to_handle(&vertex_buffer) },
					(VkDeviceSize[]) { 0 });

	struct radv_pipeline *pipeline = pick_depthstencil_pipeline(meta_state,
								    iview,
								    samples_log2,
								    aspects,
								    subpass->depth_stencil_attachment.layout,
								    clear_rect,
								    clear_value);
	if (cmd_buffer->state.pipeline != pipeline) {
		radv_CmdBindPipeline(cmd_buffer_h, VK_PIPELINE_BIND_POINT_GRAPHICS,
					   radv_pipeline_to_handle(pipeline));
	}

	if (depth_view_can_fast_clear(iview, subpass->depth_stencil_attachment.layout, clear_rect))
		radv_set_depth_clear_regs(cmd_buffer, iview->image, clear_value, aspects);

	radv_CmdDraw(cmd_buffer_h, 3, clear_rect->layerCount, 0, 0);
}


static VkFormat pipeline_formats[] = {
	VK_FORMAT_R8G8B8A8_UNORM,
	VK_FORMAT_R8G8B8A8_UINT,
	VK_FORMAT_R8G8B8A8_SINT,
	VK_FORMAT_R16G16B16A16_UNORM,
	VK_FORMAT_R16G16B16A16_SNORM,
	VK_FORMAT_R16G16B16A16_UINT,
	VK_FORMAT_R16G16B16A16_SINT,
	VK_FORMAT_R32_SFLOAT,
	VK_FORMAT_R32G32_SFLOAT,
	VK_FORMAT_R32G32B32A32_SFLOAT
};

VkResult
radv_device_init_meta_clear_state(struct radv_device *device)
{
	VkResult res;
	struct radv_meta_state *state = &device->meta_state;

	memset(&device->meta_state.clear, 0, sizeof(device->meta_state.clear));

	for (uint32_t i = 0; i < ARRAY_SIZE(state->clear); ++i) {
		uint32_t samples = 1 << i;
		for (uint32_t j = 0; j < ARRAY_SIZE(pipeline_formats); ++j) {
			VkFormat format = pipeline_formats[j];
			unsigned fs_key = radv_format_meta_fs_key(format);
			assert(!state->clear[i].color_pipelines[fs_key]);

			res = create_color_renderpass(device, format, samples,
						      &state->clear[i].render_pass[fs_key]);
			if (res != VK_SUCCESS)
				goto fail;

			res = create_color_pipeline(device, samples, 0, &state->clear[i].color_pipelines[fs_key],
						    state->clear[i].render_pass[fs_key]);
			if (res != VK_SUCCESS)
				goto fail;

		}

		res = create_depthstencil_renderpass(device,
						     samples,
						     &state->clear[i].depthstencil_rp);
		if (res != VK_SUCCESS)
			goto fail;

		for (uint32_t j = 0; j < NUM_DEPTH_CLEAR_PIPELINES; j++) {
			res = create_depthstencil_pipeline(device,
							   VK_IMAGE_ASPECT_DEPTH_BIT,
							   samples,
							   j,
							   &state->clear[i].depth_only_pipeline[j],
							   state->clear[i].depthstencil_rp);
			if (res != VK_SUCCESS)
				goto fail;

			res = create_depthstencil_pipeline(device,
							   VK_IMAGE_ASPECT_STENCIL_BIT,
							   samples,
							   j,
							   &state->clear[i].stencil_only_pipeline[j],
							   state->clear[i].depthstencil_rp);
			if (res != VK_SUCCESS)
				goto fail;

			res = create_depthstencil_pipeline(device,
							   VK_IMAGE_ASPECT_DEPTH_BIT |
							   VK_IMAGE_ASPECT_STENCIL_BIT,
							   samples,
							   j,
							   &state->clear[i].depthstencil_pipeline[j],
							   state->clear[i].depthstencil_rp);
			if (res != VK_SUCCESS)
				goto fail;
		}
	}
	return VK_SUCCESS;

fail:
	radv_device_finish_meta_clear_state(device);
	return res;
}

static bool
emit_fast_color_clear(struct radv_cmd_buffer *cmd_buffer,
		      const VkClearAttachment *clear_att,
		      const VkClearRect *clear_rect)
{
	const struct radv_subpass *subpass = cmd_buffer->state.subpass;
	const uint32_t subpass_att = clear_att->colorAttachment;
	const uint32_t pass_att = subpass->color_attachments[subpass_att].attachment;
	VkImageLayout image_layout = subpass->color_attachments[subpass_att].layout;
	const struct radv_framebuffer *fb = cmd_buffer->state.framebuffer;
	const struct radv_image_view *iview = fb->attachments[pass_att].attachment;
	VkClearColorValue clear_value = clear_att->clearValue.color;
	uint32_t clear_color[2];
	bool ret;

	if (!iview->image->cmask.size && !iview->image->surface.dcc_size)
		return false;

	if (!(cmd_buffer->device->debug_flags & RADV_DEBUG_FAST_CLEARS))
		return false;

	if (!radv_layout_can_fast_clear(iview->image, image_layout, radv_image_queue_family_mask(iview->image, cmd_buffer->queue_family_index)))
		goto fail;
	if (vk_format_get_blocksizebits(iview->image->vk_format) > 64)
		goto fail;

	/* don't fast clear 3D */
	if (iview->image->type == VK_IMAGE_TYPE_3D)
		goto fail;

	/* all layers are bound */
	if (iview->base_layer > 0)
		goto fail;
	if (iview->image->array_size != iview->layer_count)
		goto fail;

	if (iview->image->levels > 1)
		goto fail;

	if (iview->image->surface.level[0].mode < RADEON_SURF_MODE_1D)
		goto fail;

	if (memcmp(&iview->extent, &iview->image->extent, sizeof(iview->extent)))
		goto fail;

	if (clear_rect->rect.offset.x || clear_rect->rect.offset.y ||
	    clear_rect->rect.extent.width != iview->image->extent.width ||
	    clear_rect->rect.extent.height != iview->image->extent.height)
		goto fail;

	if (clear_rect->baseArrayLayer != 0)
		goto fail;
	if (clear_rect->layerCount != iview->image->array_size)
		goto fail;

	/* DCC */
	ret = radv_format_pack_clear_color(iview->image->vk_format,
					   clear_color, &clear_value);
	if (ret == false)
		goto fail;

	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
	                                RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;
	si_emit_cache_flush(cmd_buffer);
	/* clear cmask buffer */
	if (iview->image->surface.dcc_size) {
		radv_fill_buffer(cmd_buffer, iview->image->bo,
				 iview->image->offset + iview->image->dcc_offset,
				 iview->image->surface.dcc_size, 0x20202020);
	} else {
		radv_fill_buffer(cmd_buffer, iview->image->bo,
				 iview->image->offset + iview->image->cmask.offset,
				 iview->image->cmask.size, 0);
	}
	cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
	                                RADV_CMD_FLAG_INV_VMEM_L1 |
	                                RADV_CMD_FLAG_INV_GLOBAL_L2;

	radv_set_color_clear_regs(cmd_buffer, iview->image, subpass_att, clear_color);

	return true;
fail:
	return false;
}

/**
 * The parameters mean that same as those in vkCmdClearAttachments.
 */
static void
emit_clear(struct radv_cmd_buffer *cmd_buffer,
           const VkClearAttachment *clear_att,
           const VkClearRect *clear_rect)
{
	if (clear_att->aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {

		if (!emit_fast_color_clear(cmd_buffer, clear_att, clear_rect))
			emit_color_clear(cmd_buffer, clear_att, clear_rect);
	} else {
		assert(clear_att->aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT |
						VK_IMAGE_ASPECT_STENCIL_BIT));
		emit_depthstencil_clear(cmd_buffer, clear_att, clear_rect);
	}
}

static bool
subpass_needs_clear(const struct radv_cmd_buffer *cmd_buffer)
{
	const struct radv_cmd_state *cmd_state = &cmd_buffer->state;
	uint32_t ds;

	if (!cmd_state->subpass)
		return false;
	ds = cmd_state->subpass->depth_stencil_attachment.attachment;
	for (uint32_t i = 0; i < cmd_state->subpass->color_count; ++i) {
		uint32_t a = cmd_state->subpass->color_attachments[i].attachment;
		if (cmd_state->attachments[a].pending_clear_aspects) {
			return true;
		}
	}

	if (ds != VK_ATTACHMENT_UNUSED &&
	    cmd_state->attachments[ds].pending_clear_aspects) {
		return true;
	}

	return false;
}

/**
 * Emit any pending attachment clears for the current subpass.
 *
 * @see radv_attachment_state::pending_clear_aspects
 */
void
radv_cmd_buffer_clear_subpass(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_cmd_state *cmd_state = &cmd_buffer->state;
	struct radv_meta_saved_state saved_state;

	if (!subpass_needs_clear(cmd_buffer))
		return;

	radv_meta_save_graphics_reset_vport_scissor(&saved_state, cmd_buffer);

	VkClearRect clear_rect = {
		.rect = cmd_state->render_area,
		.baseArrayLayer = 0,
		.layerCount = cmd_state->framebuffer->layers,
	};

	for (uint32_t i = 0; i < cmd_state->subpass->color_count; ++i) {
		uint32_t a = cmd_state->subpass->color_attachments[i].attachment;

		if (!cmd_state->attachments[a].pending_clear_aspects)
			continue;

		assert(cmd_state->attachments[a].pending_clear_aspects ==
		       VK_IMAGE_ASPECT_COLOR_BIT);

		VkClearAttachment clear_att = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.colorAttachment = i, /* Use attachment index relative to subpass */
			.clearValue = cmd_state->attachments[a].clear_value,
		};

		emit_clear(cmd_buffer, &clear_att, &clear_rect);
		cmd_state->attachments[a].pending_clear_aspects = 0;
	}

	uint32_t ds = cmd_state->subpass->depth_stencil_attachment.attachment;

	if (ds != VK_ATTACHMENT_UNUSED) {

		if (cmd_state->attachments[ds].pending_clear_aspects) {

			VkClearAttachment clear_att = {
				.aspectMask = cmd_state->attachments[ds].pending_clear_aspects,
				.clearValue = cmd_state->attachments[ds].clear_value,
			};

			emit_clear(cmd_buffer, &clear_att, &clear_rect);
			cmd_state->attachments[ds].pending_clear_aspects = 0;
		}
	}

	radv_meta_restore(&saved_state, cmd_buffer);
}

static void
radv_clear_image_layer(struct radv_cmd_buffer *cmd_buffer,
		       struct radv_image *image,
		       VkImageLayout image_layout,
		       const VkImageSubresourceRange *range,
		       VkFormat format, int level, int layer,
		       const VkClearValue *clear_val)
{
	VkDevice device_h = radv_device_to_handle(cmd_buffer->device);
	struct radv_image_view iview;
	radv_image_view_init(&iview, cmd_buffer->device,
			     &(VkImageViewCreateInfo) {
				     .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					     .image = radv_image_to_handle(image),
					     .viewType = radv_meta_get_view_type(image),
					     .format = format,
					     .subresourceRange = {
					     .aspectMask = range->aspectMask,
					     .baseMipLevel = range->baseMipLevel + level,
					     .levelCount = 1,
					     .baseArrayLayer = range->baseArrayLayer + layer,
					     .layerCount = 1
				     },
			     },
			     cmd_buffer, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT);

	VkFramebuffer fb;
	radv_CreateFramebuffer(device_h,
			       &(VkFramebufferCreateInfo) {
				       .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
					       .attachmentCount = 1,
					       .pAttachments = (VkImageView[]) {
					       radv_image_view_to_handle(&iview),
				       },
					       .width = iview.extent.width,
							.height = iview.extent.height,
							.layers = 1
			       },
			       &cmd_buffer->pool->alloc,
			       &fb);

	VkAttachmentDescription att_desc = {
		.format = iview.vk_format,
		.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE,
		.initialLayout = image_layout,
		.finalLayout = image_layout,
	};

	VkSubpassDescription subpass_desc = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.inputAttachmentCount = 0,
		.colorAttachmentCount = 0,
		.pColorAttachments = NULL,
		.pResolveAttachments = NULL,
		.pDepthStencilAttachment = NULL,
		.preserveAttachmentCount = 0,
		.pPreserveAttachments = NULL,
	};

	const VkAttachmentReference att_ref = {
		.attachment = 0,
		.layout = image_layout,
	};

	if (range->aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
		subpass_desc.colorAttachmentCount = 1;
		subpass_desc.pColorAttachments = &att_ref;
	} else {
		subpass_desc.pDepthStencilAttachment = &att_ref;
	}

	VkRenderPass pass;
	radv_CreateRenderPass(device_h,
			      &(VkRenderPassCreateInfo) {
				      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
					      .attachmentCount = 1,
					      .pAttachments = &att_desc,
					      .subpassCount = 1,
					      .pSubpasses = &subpass_desc,
					      },
			      &cmd_buffer->pool->alloc,
			      &pass);

	radv_CmdBeginRenderPass(radv_cmd_buffer_to_handle(cmd_buffer),
				&(VkRenderPassBeginInfo) {
					.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
						.renderArea = {
						.offset = { 0, 0, },
						.extent = {
							.width = iview.extent.width,
							.height = iview.extent.height,
						},
					},
						.renderPass = pass,
						.framebuffer = fb,
						.clearValueCount = 0,
						.pClearValues = NULL,
						},
				VK_SUBPASS_CONTENTS_INLINE);

	VkClearAttachment clear_att = {
		.aspectMask = range->aspectMask,
		.colorAttachment = 0,
		.clearValue = *clear_val,
	};

	VkClearRect clear_rect = {
		.rect = {
			.offset = { 0, 0 },
			.extent = { iview.extent.width, iview.extent.height },
		},
		.baseArrayLayer = range->baseArrayLayer,
		.layerCount = 1, /* FINISHME: clear multi-layer framebuffer */
	};

	emit_clear(cmd_buffer, &clear_att, &clear_rect);

	radv_CmdEndRenderPass(radv_cmd_buffer_to_handle(cmd_buffer));
	radv_DestroyRenderPass(device_h, pass,
			       &cmd_buffer->pool->alloc);
	radv_DestroyFramebuffer(device_h, fb,
				&cmd_buffer->pool->alloc);
}
static void
radv_cmd_clear_image(struct radv_cmd_buffer *cmd_buffer,
		     struct radv_image *image,
		     VkImageLayout image_layout,
		     const VkClearValue *clear_value,
		     uint32_t range_count,
		     const VkImageSubresourceRange *ranges,
		     bool cs)
{
	VkFormat format = image->vk_format;
	VkClearValue internal_clear_value = *clear_value;

	if (format == VK_FORMAT_E5B9G9R9_UFLOAT_PACK32) {
		uint32_t value;
		format = VK_FORMAT_R32_UINT;
		value = float3_to_rgb9e5(clear_value->color.float32);
		internal_clear_value.color.uint32[0] = value;
	}

	for (uint32_t r = 0; r < range_count; r++) {
		const VkImageSubresourceRange *range = &ranges[r];
		for (uint32_t l = 0; l < radv_get_levelCount(image, range); ++l) {
			const uint32_t layer_count = image->type == VK_IMAGE_TYPE_3D ?
				radv_minify(image->extent.depth, range->baseMipLevel + l) :
				radv_get_layerCount(image, range);
			for (uint32_t s = 0; s < layer_count; ++s) {

				if (cs) {
					struct radv_meta_blit2d_surf surf;
					surf.format = format;
					surf.image = image;
					surf.level = range->baseMipLevel + l;
					surf.layer = range->baseArrayLayer + s;
					surf.aspect_mask = range->aspectMask;
					radv_meta_clear_image_cs(cmd_buffer, &surf,
								 &internal_clear_value.color);
				} else {
					radv_clear_image_layer(cmd_buffer, image, image_layout,
							       range, format, l, s, &internal_clear_value);
				}
			}
		}
	}
}

union meta_saved_state {
	struct radv_meta_saved_state gfx;
	struct radv_meta_saved_compute_state compute;
};

void radv_CmdClearColorImage(
	VkCommandBuffer                             commandBuffer,
	VkImage                                     image_h,
	VkImageLayout                               imageLayout,
	const VkClearColorValue*                    pColor,
	uint32_t                                    rangeCount,
	const VkImageSubresourceRange*              pRanges)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_image, image, image_h);
	union meta_saved_state saved_state;
	bool cs = cmd_buffer->queue_family_index == RADV_QUEUE_COMPUTE;

	if (cs)
		radv_meta_begin_cleari(cmd_buffer, &saved_state.compute);
	else
		radv_meta_save_graphics_reset_vport_scissor(&saved_state.gfx, cmd_buffer);

	radv_cmd_clear_image(cmd_buffer, image, imageLayout,
			     (const VkClearValue *) pColor,
			     rangeCount, pRanges, cs);

	if (cs)
		radv_meta_end_cleari(cmd_buffer, &saved_state.compute);
	else
		radv_meta_restore(&saved_state.gfx, cmd_buffer);
}

void radv_CmdClearDepthStencilImage(
	VkCommandBuffer                             commandBuffer,
	VkImage                                     image_h,
	VkImageLayout                               imageLayout,
	const VkClearDepthStencilValue*             pDepthStencil,
	uint32_t                                    rangeCount,
	const VkImageSubresourceRange*              pRanges)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_image, image, image_h);
	struct radv_meta_saved_state saved_state;

	radv_meta_save_graphics_reset_vport_scissor(&saved_state, cmd_buffer);

	radv_cmd_clear_image(cmd_buffer, image, imageLayout,
			     (const VkClearValue *) pDepthStencil,
			     rangeCount, pRanges, false);

	radv_meta_restore(&saved_state, cmd_buffer);
}

void radv_CmdClearAttachments(
	VkCommandBuffer                             commandBuffer,
	uint32_t                                    attachmentCount,
	const VkClearAttachment*                    pAttachments,
	uint32_t                                    rectCount,
	const VkClearRect*                          pRects)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	struct radv_meta_saved_state saved_state;

	if (!cmd_buffer->state.subpass)
		return;

	radv_meta_save_graphics_reset_vport_scissor(&saved_state, cmd_buffer);

	/* FINISHME: We can do better than this dumb loop. It thrashes too much
	 * state.
	 */
	for (uint32_t a = 0; a < attachmentCount; ++a) {
		for (uint32_t r = 0; r < rectCount; ++r) {
			emit_clear(cmd_buffer, &pAttachments[a], &pRects[r]);
		}
	}

	radv_meta_restore(&saved_state, cmd_buffer);
}
