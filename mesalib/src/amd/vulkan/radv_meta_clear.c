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

#include "radv_debug.h"
#include "radv_meta.h"
#include "radv_private.h"
#include "nir/nir_builder.h"

#include "util/format_rgb9e5.h"
#include "vk_format.h"

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

	vs_b.shader->info.name = ralloc_strdup(vs_b.shader, "meta_clear_color_vs");
	fs_b.shader->info.name = ralloc_strdup(fs_b.shader, "meta_clear_color_fs");

	const struct glsl_type *position_type = glsl_vec4_type();
	const struct glsl_type *color_type = glsl_vec4_type();

	nir_variable *vs_out_pos =
		nir_variable_create(vs_b.shader, nir_var_shader_out, position_type,
				    "gl_Position");
	vs_out_pos->data.location = VARYING_SLOT_POS;

	nir_intrinsic_instr *in_color_load = nir_intrinsic_instr_create(fs_b.shader, nir_intrinsic_load_push_constant);
	nir_intrinsic_set_base(in_color_load, 0);
	nir_intrinsic_set_range(in_color_load, 16);
	in_color_load->src[0] = nir_src_for_ssa(nir_imm_int(&fs_b, 0));
	in_color_load->num_components = 4;
	nir_ssa_dest_init(&in_color_load->instr, &in_color_load->dest, 4, 32, "clear color");
	nir_builder_instr_insert(&fs_b, &in_color_load->instr);

	nir_variable *fs_out_color =
		nir_variable_create(fs_b.shader, nir_var_shader_out, color_type,
				    "f_color");
	fs_out_color->data.location = FRAG_RESULT_DATA0 + frag_output;

	nir_store_var(&fs_b, fs_out_color, &in_color_load->dest.ssa, 0xf);

	nir_ssa_def *outvec = radv_meta_gen_rect_vertices(&vs_b);
	nir_store_var(&vs_b, vs_out_pos, outvec, 0xf);

	const struct glsl_type *layer_type = glsl_int_type();
	nir_variable *vs_out_layer =
		nir_variable_create(vs_b.shader, nir_var_shader_out, layer_type,
				    "v_layer");
	vs_out_layer->data.location = VARYING_SLOT_LAYER;
	vs_out_layer->data.interpolation = INTERP_MODE_FLAT;
	nir_ssa_def *inst_id = nir_load_system_value(&vs_b, nir_intrinsic_load_instance_id, 0);
	nir_ssa_def *base_instance = nir_load_system_value(&vs_b, nir_intrinsic_load_base_instance, 0);

	nir_ssa_def *layer_id = nir_iadd(&vs_b, inst_id, base_instance);
	nir_store_var(&vs_b, vs_out_layer, layer_id, 0x1);

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
		const VkPipelineLayout layout,
		const struct radv_graphics_pipeline_create_info *extra,
                const VkAllocationCallbacks *alloc,
		VkPipeline *pipeline)
{
	VkDevice device_h = radv_device_to_handle(device);
	VkResult result;

	struct radv_shader_module vs_m = { .nir = vs_nir };
	struct radv_shader_module fs_m = { .nir = fs_nir };

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
							       .viewportCount = 1,
							       .scissorCount = 1,
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
							       .dynamicStateCount = 8,
							       .pDynamicStates = (VkDynamicState[]) {
								       /* Everything except stencil write mask */
								       VK_DYNAMIC_STATE_VIEWPORT,
								       VK_DYNAMIC_STATE_SCISSOR,
								       VK_DYNAMIC_STATE_LINE_WIDTH,
								       VK_DYNAMIC_STATE_DEPTH_BIAS,
								       VK_DYNAMIC_STATE_BLEND_CONSTANTS,
								       VK_DYNAMIC_STATE_DEPTH_BOUNDS,
								       VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
								       VK_DYNAMIC_STATE_STENCIL_REFERENCE,
							       },
						       },
						    .layout = layout,
						    .flags = 0,
						    .renderPass = radv_render_pass_to_handle(render_pass),
						    .subpass = 0,
						},
					       extra,
					       alloc,
					       pipeline);

	ralloc_free(vs_nir);
	ralloc_free(fs_nir);

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
		      VkPipeline *pipeline,
		      VkRenderPass pass)
{
	struct nir_shader *vs_nir;
	struct nir_shader *fs_nir;
	VkResult result;
	build_color_shaders(&vs_nir, &fs_nir, frag_output);

	const VkPipelineVertexInputStateCreateInfo vi_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 0,
		.vertexAttributeDescriptionCount = 0,
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
				 device->meta_state.clear_color_p_layout,
				 &extra, &device->meta_state.alloc, pipeline);

	return result;
}

void
radv_device_finish_meta_clear_state(struct radv_device *device)
{
	struct radv_meta_state *state = &device->meta_state;

	for (uint32_t i = 0; i < ARRAY_SIZE(state->clear); ++i) {
		for (uint32_t j = 0; j < ARRAY_SIZE(state->clear[i].color_pipelines); ++j) {
			radv_DestroyPipeline(radv_device_to_handle(device),
					     state->clear[i].color_pipelines[j],
					     &state->alloc);
			radv_DestroyRenderPass(radv_device_to_handle(device),
					       state->clear[i].render_pass[j],
					       &state->alloc);
		}

		for (uint32_t j = 0; j < NUM_DEPTH_CLEAR_PIPELINES; j++) {
			radv_DestroyPipeline(radv_device_to_handle(device),
					     state->clear[i].depth_only_pipeline[j],
					     &state->alloc);
			radv_DestroyPipeline(radv_device_to_handle(device),
					     state->clear[i].stencil_only_pipeline[j],
					     &state->alloc);
			radv_DestroyPipeline(radv_device_to_handle(device),
					     state->clear[i].depthstencil_pipeline[j],
					     &state->alloc);
		}
		radv_DestroyRenderPass(radv_device_to_handle(device),
				      state->clear[i].depthstencil_rp,
				      &state->alloc);
	}
	radv_DestroyPipelineLayout(radv_device_to_handle(device),
				   state->clear_color_p_layout,
				   &state->alloc);
	radv_DestroyPipelineLayout(radv_device_to_handle(device),
				   state->clear_depth_p_layout,
				   &state->alloc);
}

static void
emit_color_clear(struct radv_cmd_buffer *cmd_buffer,
                 const VkClearAttachment *clear_att,
                 const VkClearRect *clear_rect,
                 uint32_t view_mask)
{
	struct radv_device *device = cmd_buffer->device;
	const struct radv_subpass *subpass = cmd_buffer->state.subpass;
	const struct radv_framebuffer *fb = cmd_buffer->state.framebuffer;
	const uint32_t subpass_att = clear_att->colorAttachment;
	const uint32_t pass_att = subpass->color_attachments[subpass_att].attachment;
	const struct radv_image_view *iview = fb->attachments[pass_att].attachment;
	const uint32_t samples = iview->image->info.samples;
	const uint32_t samples_log2 = ffs(samples) - 1;
	unsigned fs_key = radv_format_meta_fs_key(iview->vk_format);
	VkClearColorValue clear_value = clear_att->clearValue.color;
	VkCommandBuffer cmd_buffer_h = radv_cmd_buffer_to_handle(cmd_buffer);
	VkPipeline pipeline;

	if (fs_key == -1) {
		radv_finishme("color clears incomplete");
		return;
	}

	pipeline = device->meta_state.clear[samples_log2].color_pipelines[fs_key];
	if (!pipeline) {
		radv_finishme("color clears incomplete");
		return;
	}
	assert(samples_log2 < ARRAY_SIZE(device->meta_state.clear));
	assert(pipeline);
	assert(clear_att->aspectMask == VK_IMAGE_ASPECT_COLOR_BIT);
	assert(clear_att->colorAttachment < subpass->color_count);

	radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
			      device->meta_state.clear_color_p_layout,
			      VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16,
			      &clear_value);

	struct radv_subpass clear_subpass = {
		.color_count = 1,
		.color_attachments = (VkAttachmentReference[]) {
			subpass->color_attachments[clear_att->colorAttachment]
		},
		.depth_stencil_attachment = (VkAttachmentReference) { VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_UNDEFINED }
	};

	radv_cmd_buffer_set_subpass(cmd_buffer, &clear_subpass, false);

	radv_CmdBindPipeline(cmd_buffer_h, VK_PIPELINE_BIND_POINT_GRAPHICS,
			     pipeline);

	radv_CmdSetViewport(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1, &(VkViewport) {
			.x = clear_rect->rect.offset.x,
			.y = clear_rect->rect.offset.y,
			.width = clear_rect->rect.extent.width,
			.height = clear_rect->rect.extent.height,
			.minDepth = 0.0f,
			.maxDepth = 1.0f
		});

	radv_CmdSetScissor(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1, &clear_rect->rect);

	if (view_mask) {
		unsigned i;
		for_each_bit(i, view_mask)
			radv_CmdDraw(cmd_buffer_h, 3, 1, 0, i);
	} else {
		radv_CmdDraw(cmd_buffer_h, 3, clear_rect->layerCount, 0, clear_rect->baseArrayLayer);
	}

	radv_cmd_buffer_set_subpass(cmd_buffer, subpass, false);
}


static void
build_depthstencil_shader(struct nir_shader **out_vs, struct nir_shader **out_fs)
{
	nir_builder vs_b, fs_b;

	nir_builder_init_simple_shader(&vs_b, NULL, MESA_SHADER_VERTEX, NULL);
	nir_builder_init_simple_shader(&fs_b, NULL, MESA_SHADER_FRAGMENT, NULL);

	vs_b.shader->info.name = ralloc_strdup(vs_b.shader, "meta_clear_depthstencil_vs");
	fs_b.shader->info.name = ralloc_strdup(fs_b.shader, "meta_clear_depthstencil_fs");
	const struct glsl_type *position_out_type = glsl_vec4_type();

	nir_variable *vs_out_pos =
		nir_variable_create(vs_b.shader, nir_var_shader_out, position_out_type,
				    "gl_Position");
	vs_out_pos->data.location = VARYING_SLOT_POS;

	nir_intrinsic_instr *in_color_load = nir_intrinsic_instr_create(vs_b.shader, nir_intrinsic_load_push_constant);
	nir_intrinsic_set_base(in_color_load, 0);
	nir_intrinsic_set_range(in_color_load, 4);
	in_color_load->src[0] = nir_src_for_ssa(nir_imm_int(&vs_b, 0));
	in_color_load->num_components = 1;
	nir_ssa_dest_init(&in_color_load->instr, &in_color_load->dest, 1, 32, "depth value");
	nir_builder_instr_insert(&vs_b, &in_color_load->instr);

	nir_ssa_def *outvec = radv_meta_gen_rect_vertices_comp2(&vs_b, &in_color_load->dest.ssa);
	nir_store_var(&vs_b, vs_out_pos, outvec, 0xf);

	const struct glsl_type *layer_type = glsl_int_type();
	nir_variable *vs_out_layer =
		nir_variable_create(vs_b.shader, nir_var_shader_out, layer_type,
				    "v_layer");
	vs_out_layer->data.location = VARYING_SLOT_LAYER;
	vs_out_layer->data.interpolation = INTERP_MODE_FLAT;
	nir_ssa_def *inst_id = nir_load_system_value(&vs_b, nir_intrinsic_load_instance_id, 0);
	nir_ssa_def *base_instance = nir_load_system_value(&vs_b, nir_intrinsic_load_base_instance, 0);

	nir_ssa_def *layer_id = nir_iadd(&vs_b, inst_id, base_instance);
	nir_store_var(&vs_b, vs_out_layer, layer_id, 0x1);

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
						       .format = VK_FORMAT_D32_SFLOAT_S8_UINT,
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
			     VkPipeline *pipeline,
			     VkRenderPass render_pass)
{
	struct nir_shader *vs_nir, *fs_nir;
	VkResult result;
	build_depthstencil_shader(&vs_nir, &fs_nir);

	const VkPipelineVertexInputStateCreateInfo vi_state = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 0,
		.vertexAttributeDescriptionCount = 0,
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
				 device->meta_state.clear_depth_p_layout,
				 &extra, &device->meta_state.alloc, pipeline);
	return result;
}

static bool depth_view_can_fast_clear(struct radv_cmd_buffer *cmd_buffer,
				      const struct radv_image_view *iview,
				      VkImageAspectFlags aspects,
				      VkImageLayout layout,
				      const VkClearRect *clear_rect,
				      VkClearDepthStencilValue clear_value)
{
	uint32_t queue_mask = radv_image_queue_family_mask(iview->image,
	                                                   cmd_buffer->queue_family_index,
	                                                   cmd_buffer->queue_family_index);
	if (clear_rect->rect.offset.x || clear_rect->rect.offset.y ||
	    clear_rect->rect.extent.width != iview->extent.width ||
	    clear_rect->rect.extent.height != iview->extent.height)
		return false;
	if (radv_image_is_tc_compat_htile(iview->image) &&
	    (((aspects & VK_IMAGE_ASPECT_DEPTH_BIT) && clear_value.depth != 0.0 &&
	      clear_value.depth != 1.0) ||
	     ((aspects & VK_IMAGE_ASPECT_STENCIL_BIT) && clear_value.stencil != 0)))
		return false;
	if (radv_image_has_htile(iview->image) &&
	    iview->base_mip == 0 &&
	    iview->base_layer == 0 &&
	    radv_layout_is_htile_compressed(iview->image, layout, queue_mask) &&
	    !radv_image_extent_compare(iview->image, &iview->extent))
		return true;
	return false;
}

static VkPipeline
pick_depthstencil_pipeline(struct radv_cmd_buffer *cmd_buffer,
			   struct radv_meta_state *meta_state,
			   const struct radv_image_view *iview,
			   int samples_log2,
			   VkImageAspectFlags aspects,
			   VkImageLayout layout,
			   const VkClearRect *clear_rect,
			   VkClearDepthStencilValue clear_value)
{
	bool fast = depth_view_can_fast_clear(cmd_buffer, iview, aspects, layout, clear_rect, clear_value);
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
	const uint32_t samples = iview->image->info.samples;
	const uint32_t samples_log2 = ffs(samples) - 1;
	VkCommandBuffer cmd_buffer_h = radv_cmd_buffer_to_handle(cmd_buffer);

	assert(pass_att != VK_ATTACHMENT_UNUSED);

	if (!(aspects & VK_IMAGE_ASPECT_DEPTH_BIT))
		clear_value.depth = 1.0f;

	radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
			      device->meta_state.clear_depth_p_layout,
			      VK_SHADER_STAGE_VERTEX_BIT, 0, 4,
			      &clear_value.depth);

	uint32_t prev_reference = cmd_buffer->state.dynamic.stencil_reference.front;
	if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
		radv_CmdSetStencilReference(cmd_buffer_h, VK_STENCIL_FACE_FRONT_BIT,
						  clear_value.stencil);
	}

	VkPipeline pipeline = pick_depthstencil_pipeline(cmd_buffer,
							 meta_state,
							 iview,
							 samples_log2,
							 aspects,
							 subpass->depth_stencil_attachment.layout,
							 clear_rect,
							 clear_value);

	radv_CmdBindPipeline(cmd_buffer_h, VK_PIPELINE_BIND_POINT_GRAPHICS,
			     pipeline);

	if (depth_view_can_fast_clear(cmd_buffer, iview, aspects,
	                              subpass->depth_stencil_attachment.layout,
	                              clear_rect, clear_value))
		radv_set_depth_clear_regs(cmd_buffer, iview->image, clear_value, aspects);

	radv_CmdSetViewport(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1, &(VkViewport) {
			.x = clear_rect->rect.offset.x,
			.y = clear_rect->rect.offset.y,
			.width = clear_rect->rect.extent.width,
			.height = clear_rect->rect.extent.height,
			.minDepth = 0.0f,
			.maxDepth = 1.0f
		});

	radv_CmdSetScissor(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1, &clear_rect->rect);

	radv_CmdDraw(cmd_buffer_h, 3, clear_rect->layerCount, 0, clear_rect->baseArrayLayer);

	if (aspects & VK_IMAGE_ASPECT_STENCIL_BIT) {
		radv_CmdSetStencilReference(cmd_buffer_h, VK_STENCIL_FACE_FRONT_BIT,
						  prev_reference);
	}
}

static bool
emit_fast_htile_clear(struct radv_cmd_buffer *cmd_buffer,
		      const VkClearAttachment *clear_att,
		      const VkClearRect *clear_rect,
		      enum radv_cmd_flush_bits *pre_flush,
		      enum radv_cmd_flush_bits *post_flush)
{
	const struct radv_subpass *subpass = cmd_buffer->state.subpass;
	const uint32_t pass_att = subpass->depth_stencil_attachment.attachment;
	VkImageLayout image_layout = subpass->depth_stencil_attachment.layout;
	const struct radv_framebuffer *fb = cmd_buffer->state.framebuffer;
	const struct radv_image_view *iview = fb->attachments[pass_att].attachment;
	VkClearDepthStencilValue clear_value = clear_att->clearValue.depthStencil;
	VkImageAspectFlags aspects = clear_att->aspectMask;
	uint32_t clear_word, flush_bits;

	if (!radv_image_has_htile(iview->image))
		return false;

	if (cmd_buffer->device->instance->debug_flags & RADV_DEBUG_NO_FAST_CLEARS)
		return false;

	if (!radv_layout_is_htile_compressed(iview->image, image_layout, radv_image_queue_family_mask(iview->image, cmd_buffer->queue_family_index, cmd_buffer->queue_family_index)))
		goto fail;

	/* don't fast clear 3D */
	if (iview->image->type == VK_IMAGE_TYPE_3D)
		goto fail;

	/* all layers are bound */
	if (iview->base_layer > 0)
		goto fail;
	if (iview->image->info.array_size != iview->layer_count)
		goto fail;

	if (!radv_image_extent_compare(iview->image, &iview->extent))
		goto fail;

	if (clear_rect->rect.offset.x || clear_rect->rect.offset.y ||
	    clear_rect->rect.extent.width != iview->image->info.width ||
	    clear_rect->rect.extent.height != iview->image->info.height)
		goto fail;

	if (clear_rect->baseArrayLayer != 0)
		goto fail;
	if (clear_rect->layerCount != iview->image->info.array_size)
		goto fail;

	if ((clear_value.depth != 0.0 && clear_value.depth != 1.0) || !(aspects & VK_IMAGE_ASPECT_DEPTH_BIT))
		goto fail;

	if (vk_format_aspects(iview->image->vk_format) & VK_IMAGE_ASPECT_STENCIL_BIT) {
		if (clear_value.stencil != 0 || !(aspects & VK_IMAGE_ASPECT_STENCIL_BIT))
			goto fail;
		clear_word = clear_value.depth ? 0xfffc0000 : 0;
	} else
		clear_word = clear_value.depth ? 0xfffffff0 : 0;

	if (pre_flush) {
		cmd_buffer->state.flush_bits |= (RADV_CMD_FLAG_FLUSH_AND_INV_DB |
						 RADV_CMD_FLAG_FLUSH_AND_INV_DB_META) & ~ *pre_flush;
		*pre_flush |= cmd_buffer->state.flush_bits;
	} else
		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_DB |
		                                RADV_CMD_FLAG_FLUSH_AND_INV_DB_META;

	flush_bits = radv_fill_buffer(cmd_buffer, iview->image->bo,
				      iview->image->offset + iview->image->htile_offset,
				      iview->image->surface.htile_size, clear_word);

	radv_set_depth_clear_regs(cmd_buffer, iview->image, clear_value, aspects);
	if (post_flush) {
		*post_flush |= flush_bits;
	} else {
		cmd_buffer->state.flush_bits |= flush_bits;
	}

	return true;
fail:
	return false;
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
radv_device_init_meta_clear_state(struct radv_device *device)
{
	VkResult res;
	struct radv_meta_state *state = &device->meta_state;

	VkPipelineLayoutCreateInfo pl_color_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 0,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16},
	};

	res = radv_CreatePipelineLayout(radv_device_to_handle(device),
					&pl_color_create_info,
					&device->meta_state.alloc,
					&device->meta_state.clear_color_p_layout);
	if (res != VK_SUCCESS)
		goto fail;

	VkPipelineLayoutCreateInfo pl_depth_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 0,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_VERTEX_BIT, 0, 4},
	};

	res = radv_CreatePipelineLayout(radv_device_to_handle(device),
					&pl_depth_create_info,
					&device->meta_state.alloc,
					&device->meta_state.clear_depth_p_layout);
	if (res != VK_SUCCESS)
		goto fail;

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

static uint32_t
radv_get_cmask_fast_clear_value(const struct radv_image *image)
{
	uint32_t value = 0; /* Default value when no DCC. */

	/* The fast-clear value is different for images that have both DCC and
	 * CMASK metadata.
	 */
	if (radv_image_has_dcc(image)) {
		/* DCC fast clear with MSAA should clear CMASK to 0xC. */
		return image->info.samples > 1 ? 0xcccccccc : 0xffffffff;
	}

	return value;
}

uint32_t
radv_clear_cmask(struct radv_cmd_buffer *cmd_buffer,
		 struct radv_image *image, uint32_t value)
{
	return radv_fill_buffer(cmd_buffer, image->bo,
				image->offset + image->cmask.offset,
				image->cmask.size, value);
}

uint32_t
radv_clear_dcc(struct radv_cmd_buffer *cmd_buffer,
	       struct radv_image *image, uint32_t value)
{
	return radv_fill_buffer(cmd_buffer, image->bo,
				image->offset + image->dcc_offset,
				image->surface.dcc_size, value);
}

static void vi_get_fast_clear_parameters(VkFormat format,
					 const VkClearColorValue *clear_value,
					 uint32_t* reset_value,
					 bool *can_avoid_fast_clear_elim)
{
	bool values[4] = {};
	int extra_channel;
	bool main_value = false;
	bool extra_value = false;
	int i;
	*can_avoid_fast_clear_elim = false;

	*reset_value = 0x20202020U;

	const struct vk_format_description *desc = vk_format_description(format);
	if (format == VK_FORMAT_B10G11R11_UFLOAT_PACK32 ||
	    format == VK_FORMAT_R5G6B5_UNORM_PACK16 ||
	    format == VK_FORMAT_B5G6R5_UNORM_PACK16)
		extra_channel = -1;
	else if (desc->layout == VK_FORMAT_LAYOUT_PLAIN) {
		if (radv_translate_colorswap(format, false) <= 1)
			extra_channel = desc->nr_channels - 1;
		else
			extra_channel = 0;
	} else
		return;

	for (i = 0; i < 4; i++) {
		int index = desc->swizzle[i] - VK_SWIZZLE_X;
		if (desc->swizzle[i] < VK_SWIZZLE_X ||
		    desc->swizzle[i] > VK_SWIZZLE_W)
			continue;

		if (desc->channel[i].pure_integer &&
		    desc->channel[i].type == VK_FORMAT_TYPE_SIGNED) {
			/* Use the maximum value for clamping the clear color. */
			int max = u_bit_consecutive(0, desc->channel[i].size - 1);

			values[i] = clear_value->int32[i] != 0;
			if (clear_value->int32[i] != 0 && MIN2(clear_value->int32[i], max) != max)
				return;
		} else if (desc->channel[i].pure_integer &&
			   desc->channel[i].type == VK_FORMAT_TYPE_UNSIGNED) {
			/* Use the maximum value for clamping the clear color. */
			unsigned max = u_bit_consecutive(0, desc->channel[i].size);

			values[i] = clear_value->uint32[i] != 0U;
			if (clear_value->uint32[i] != 0U && MIN2(clear_value->uint32[i], max) != max)
				return;
		} else {
			values[i] = clear_value->float32[i] != 0.0F;
			if (clear_value->float32[i] != 0.0F && clear_value->float32[i] != 1.0F)
				return;
		}

		if (index == extra_channel)
			extra_value = values[i];
		else
			main_value = values[i];
	}

	for (int i = 0; i < 4; ++i)
		if (values[i] != main_value &&
		    desc->swizzle[i] - VK_SWIZZLE_X != extra_channel &&
		    desc->swizzle[i] >= VK_SWIZZLE_X &&
		    desc->swizzle[i] <= VK_SWIZZLE_W)
			return;

	*can_avoid_fast_clear_elim = true;
	if (main_value)
		*reset_value |= 0x80808080U;

	if (extra_value)
		*reset_value |= 0x40404040U;
	return;
}

static bool
emit_fast_color_clear(struct radv_cmd_buffer *cmd_buffer,
		      const VkClearAttachment *clear_att,
		      const VkClearRect *clear_rect,
		      enum radv_cmd_flush_bits *pre_flush,
		      enum radv_cmd_flush_bits *post_flush,
                      uint32_t view_mask)
{
	const struct radv_subpass *subpass = cmd_buffer->state.subpass;
	const uint32_t subpass_att = clear_att->colorAttachment;
	const uint32_t pass_att = subpass->color_attachments[subpass_att].attachment;
	VkImageLayout image_layout = subpass->color_attachments[subpass_att].layout;
	const struct radv_framebuffer *fb = cmd_buffer->state.framebuffer;
	const struct radv_image_view *iview = fb->attachments[pass_att].attachment;
	VkClearColorValue clear_value = clear_att->clearValue.color;
	uint32_t clear_color[2], flush_bits;
	uint32_t cmask_clear_value;
	bool ret;

	if (!radv_image_has_cmask(iview->image) && !radv_image_has_dcc(iview->image))
		return false;

	if (cmd_buffer->device->instance->debug_flags & RADV_DEBUG_NO_FAST_CLEARS)
		return false;

	if (!radv_layout_can_fast_clear(iview->image, image_layout, radv_image_queue_family_mask(iview->image, cmd_buffer->queue_family_index, cmd_buffer->queue_family_index)))
		goto fail;

	/* don't fast clear 3D */
	if (iview->image->type == VK_IMAGE_TYPE_3D)
		goto fail;

	/* all layers are bound */
	if (iview->base_layer > 0)
		goto fail;
	if (iview->image->info.array_size != iview->layer_count)
		goto fail;

	if (iview->image->info.levels > 1)
		goto fail;

	if (iview->image->surface.is_linear)
		goto fail;
	if (!radv_image_extent_compare(iview->image, &iview->extent))
		goto fail;

	if (clear_rect->rect.offset.x || clear_rect->rect.offset.y ||
	    clear_rect->rect.extent.width != iview->image->info.width ||
	    clear_rect->rect.extent.height != iview->image->info.height)
		goto fail;

	if (view_mask && (iview->image->info.array_size >= 32 ||
	                 (1u << iview->image->info.array_size) - 1u != view_mask))
		goto fail;
	if (!view_mask && clear_rect->baseArrayLayer != 0)
		goto fail;
	if (!view_mask && clear_rect->layerCount != iview->image->info.array_size)
		goto fail;

	/* RB+ doesn't work with CMASK fast clear on Stoney. */
	if (!radv_image_has_dcc(iview->image) &&
	    cmd_buffer->device->physical_device->rad_info.family == CHIP_STONEY)
		goto fail;

	/* DCC */
	ret = radv_format_pack_clear_color(iview->vk_format,
					   clear_color, &clear_value);
	if (ret == false)
		goto fail;

	if (pre_flush) {
		cmd_buffer->state.flush_bits |= (RADV_CMD_FLAG_FLUSH_AND_INV_CB |
						 RADV_CMD_FLAG_FLUSH_AND_INV_CB_META) & ~ *pre_flush;
		*pre_flush |= cmd_buffer->state.flush_bits;
	} else
		cmd_buffer->state.flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
		                                RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;

	cmask_clear_value = radv_get_cmask_fast_clear_value(iview->image);

	/* clear cmask buffer */
	if (radv_image_has_dcc(iview->image)) {
		uint32_t reset_value;
		bool can_avoid_fast_clear_elim;
		bool need_decompress_pass = false;

		vi_get_fast_clear_parameters(iview->vk_format,
					     &clear_value, &reset_value,
					     &can_avoid_fast_clear_elim);

		if (iview->image->info.samples > 1) {
			/* DCC fast clear with MSAA should clear CMASK. */
			/* FIXME: This doesn't work for now. There is a
			 * hardware bug with fast clears and DCC for MSAA
			 * textures. AMDVLK has a workaround but it doesn't
			 * seem to work here. Note that we might emit useless
			 * CB flushes but that shouldn't matter.
			 */
			if (!can_avoid_fast_clear_elim)
				goto fail;

			assert(radv_image_has_cmask(iview->image));

			flush_bits = radv_clear_cmask(cmd_buffer, iview->image,
						      cmask_clear_value);

			need_decompress_pass = true;
		}

		if (!can_avoid_fast_clear_elim)
			need_decompress_pass = true;

		flush_bits = radv_clear_dcc(cmd_buffer, iview->image, reset_value);

		radv_set_dcc_need_cmask_elim_pred(cmd_buffer, iview->image,
						  need_decompress_pass);
	} else {
		flush_bits = radv_clear_cmask(cmd_buffer, iview->image,
					      cmask_clear_value);
	}

	if (post_flush) {
		*post_flush |= flush_bits;
	} else {
		cmd_buffer->state.flush_bits |= flush_bits;
	}

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
           const VkClearRect *clear_rect,
           enum radv_cmd_flush_bits *pre_flush,
           enum radv_cmd_flush_bits *post_flush,
           uint32_t view_mask)
{
	if (clear_att->aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) {
		if (!emit_fast_color_clear(cmd_buffer, clear_att, clear_rect,
		                           pre_flush, post_flush, view_mask))
			emit_color_clear(cmd_buffer, clear_att, clear_rect, view_mask);
	} else {
		assert(clear_att->aspectMask & (VK_IMAGE_ASPECT_DEPTH_BIT |
						VK_IMAGE_ASPECT_STENCIL_BIT));
		if (!emit_fast_htile_clear(cmd_buffer, clear_att, clear_rect,
		                           pre_flush, post_flush))
			emit_depthstencil_clear(cmd_buffer, clear_att, clear_rect);
	}
}

static inline bool
radv_attachment_needs_clear(struct radv_cmd_state *cmd_state, uint32_t a)
{
	uint32_t view_mask = cmd_state->subpass->view_mask;
	return (a != VK_ATTACHMENT_UNUSED &&
		cmd_state->attachments[a].pending_clear_aspects &&
		(!view_mask || (view_mask & ~cmd_state->attachments[a].cleared_views)));
}

static bool
radv_subpass_needs_clear(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_cmd_state *cmd_state = &cmd_buffer->state;
	uint32_t a;

	if (!cmd_state->subpass)
		return false;

	for (uint32_t i = 0; i < cmd_state->subpass->color_count; ++i) {
		a = cmd_state->subpass->color_attachments[i].attachment;
		if (radv_attachment_needs_clear(cmd_state, a))
			return true;
	}

	a = cmd_state->subpass->depth_stencil_attachment.attachment;
	return radv_attachment_needs_clear(cmd_state, a);
}

static void
radv_subpass_clear_attachment(struct radv_cmd_buffer *cmd_buffer,
			      struct radv_attachment_state *attachment,
			      const VkClearAttachment *clear_att,
			      enum radv_cmd_flush_bits *pre_flush,
			      enum radv_cmd_flush_bits *post_flush)
{
	struct radv_cmd_state *cmd_state = &cmd_buffer->state;
	uint32_t view_mask = cmd_state->subpass->view_mask;

	VkClearRect clear_rect = {
		.rect = cmd_state->render_area,
		.baseArrayLayer = 0,
		.layerCount = cmd_state->framebuffer->layers,
	};

	emit_clear(cmd_buffer, clear_att, &clear_rect, pre_flush, post_flush,
		   view_mask & ~attachment->cleared_views);
	if (view_mask)
		attachment->cleared_views |= view_mask;
	else
		attachment->pending_clear_aspects = 0;
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
	enum radv_cmd_flush_bits pre_flush = 0;
	enum radv_cmd_flush_bits post_flush = 0;

	if (!radv_subpass_needs_clear(cmd_buffer))
		return;

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_GRAPHICS_PIPELINE |
		       RADV_META_SAVE_CONSTANTS);

	for (uint32_t i = 0; i < cmd_state->subpass->color_count; ++i) {
		uint32_t a = cmd_state->subpass->color_attachments[i].attachment;

		if (!radv_attachment_needs_clear(cmd_state, a))
			continue;

		assert(cmd_state->attachments[a].pending_clear_aspects ==
		       VK_IMAGE_ASPECT_COLOR_BIT);

		VkClearAttachment clear_att = {
			.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
			.colorAttachment = i, /* Use attachment index relative to subpass */
			.clearValue = cmd_state->attachments[a].clear_value,
		};

		radv_subpass_clear_attachment(cmd_buffer,
					      &cmd_state->attachments[a],
					      &clear_att, &pre_flush,
					      &post_flush);
	}

	uint32_t ds = cmd_state->subpass->depth_stencil_attachment.attachment;
	if (radv_attachment_needs_clear(cmd_state, ds)) {
		VkClearAttachment clear_att = {
			.aspectMask = cmd_state->attachments[ds].pending_clear_aspects,
			.clearValue = cmd_state->attachments[ds].clear_value,
		};

		radv_subpass_clear_attachment(cmd_buffer,
					      &cmd_state->attachments[ds],
					      &clear_att, &pre_flush,
					      &post_flush);
	}

	radv_meta_restore(&saved_state, cmd_buffer);
	cmd_buffer->state.flush_bits |= post_flush;
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
	uint32_t width = radv_minify(image->info.width, range->baseMipLevel + level);
	uint32_t height = radv_minify(image->info.height, range->baseMipLevel + level);

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
			     });

	VkFramebuffer fb;
	radv_CreateFramebuffer(device_h,
			       &(VkFramebufferCreateInfo) {
				       .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
					       .attachmentCount = 1,
					       .pAttachments = (VkImageView[]) {
					       radv_image_view_to_handle(&iview),
				       },
					       .width = width,
					       .height = height,
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
							.width = width,
							.height = height,
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
			.extent = { width, height },
		},
		.baseArrayLayer = range->baseArrayLayer,
		.layerCount = 1, /* FINISHME: clear multi-layer framebuffer */
	};

	emit_clear(cmd_buffer, &clear_att, &clear_rect, NULL, NULL, 0);

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

	if (format == VK_FORMAT_R4G4_UNORM_PACK8) {
		uint8_t r, g;
		format = VK_FORMAT_R8_UINT;
		r = float_to_ubyte(clear_value->color.float32[0]) >> 4;
		g = float_to_ubyte(clear_value->color.float32[1]) >> 4;
		internal_clear_value.color.uint32[0] = (r << 4) | (g & 0xf);
	}

	for (uint32_t r = 0; r < range_count; r++) {
		const VkImageSubresourceRange *range = &ranges[r];
		for (uint32_t l = 0; l < radv_get_levelCount(image, range); ++l) {
			const uint32_t layer_count = image->type == VK_IMAGE_TYPE_3D ?
				radv_minify(image->info.depth, range->baseMipLevel + l) :
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
	struct radv_meta_saved_state saved_state;
	bool cs = cmd_buffer->queue_family_index == RADV_QUEUE_COMPUTE;

	if (cs) {
		radv_meta_save(&saved_state, cmd_buffer,
			       RADV_META_SAVE_COMPUTE_PIPELINE |
			       RADV_META_SAVE_CONSTANTS |
			       RADV_META_SAVE_DESCRIPTORS);
	} else {
		radv_meta_save(&saved_state, cmd_buffer,
			       RADV_META_SAVE_GRAPHICS_PIPELINE |
			       RADV_META_SAVE_CONSTANTS);
	}

	radv_cmd_clear_image(cmd_buffer, image, imageLayout,
			     (const VkClearValue *) pColor,
			     rangeCount, pRanges, cs);

	radv_meta_restore(&saved_state, cmd_buffer);
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

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_GRAPHICS_PIPELINE |
		       RADV_META_SAVE_CONSTANTS);

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
	enum radv_cmd_flush_bits pre_flush = 0;
	enum radv_cmd_flush_bits post_flush = 0;

	if (!cmd_buffer->state.subpass)
		return;

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_GRAPHICS_PIPELINE |
		       RADV_META_SAVE_CONSTANTS);

	/* FINISHME: We can do better than this dumb loop. It thrashes too much
	 * state.
	 */
	for (uint32_t a = 0; a < attachmentCount; ++a) {
		for (uint32_t r = 0; r < rectCount; ++r) {
			emit_clear(cmd_buffer, &pAttachments[a], &pRects[r], &pre_flush, &post_flush,
			           cmd_buffer->state.subpass->view_mask);
		}
	}

	radv_meta_restore(&saved_state, cmd_buffer);
	cmd_buffer->state.flush_bits |= post_flush;
}
