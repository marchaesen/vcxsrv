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


static nir_shader *
build_dcc_decompress_compute_shader(struct radv_device *dev)
{
	nir_builder b;
	const struct glsl_type *buf_type = glsl_sampler_type(GLSL_SAMPLER_DIM_2D,
							     false,
							     false,
							     GLSL_TYPE_FLOAT);
	const struct glsl_type *img_type = glsl_sampler_type(GLSL_SAMPLER_DIM_2D,
							     false,
							     false,
							     GLSL_TYPE_FLOAT);
	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_COMPUTE, NULL);
	b.shader->info.name = ralloc_strdup(b.shader, "dcc_decompress_compute");

	/* We need at least 16/16/1 to cover an entire DCC block in a single workgroup. */
	b.shader->info.cs.local_size[0] = 16;
	b.shader->info.cs.local_size[1] = 16;
	b.shader->info.cs.local_size[2] = 1;
	nir_variable *input_img = nir_variable_create(b.shader, nir_var_uniform,
						      buf_type, "s_tex");
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

	nir_tex_instr *tex = nir_tex_instr_create(b.shader, 2);
	tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
	tex->op = nir_texop_txf;
	tex->src[0].src_type = nir_tex_src_coord;
	tex->src[0].src = nir_src_for_ssa(nir_channels(&b, global_id, 3));
	tex->src[1].src_type = nir_tex_src_lod;
	tex->src[1].src = nir_src_for_ssa(nir_imm_int(&b, 0));
	tex->dest_type = nir_type_float;
	tex->is_array = false;
	tex->coord_components = 2;
	tex->texture = nir_deref_var_create(tex, input_img);
	tex->sampler = NULL;

	nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
	nir_builder_instr_insert(&b, &tex->instr);

	nir_intrinsic_instr *membar = nir_intrinsic_instr_create(b.shader, nir_intrinsic_memory_barrier);
	nir_builder_instr_insert(&b, &membar->instr);

	nir_intrinsic_instr *bar = nir_intrinsic_instr_create(b.shader, nir_intrinsic_barrier);
	nir_builder_instr_insert(&b, &bar->instr);

	nir_ssa_def *outval = &tex->dest.ssa;
	nir_intrinsic_instr *store = nir_intrinsic_instr_create(b.shader, nir_intrinsic_image_var_store);
	store->src[0] = nir_src_for_ssa(global_id);
	store->src[1] = nir_src_for_ssa(nir_ssa_undef(&b, 1, 32));
	store->src[2] = nir_src_for_ssa(outval);
	store->variables[0] = nir_deref_var_create(store, output_img);

	nir_builder_instr_insert(&b, &store->instr);
	return b.shader;
}

static VkResult
create_dcc_compress_compute(struct radv_device *device)
{
	VkResult result = VK_SUCCESS;
	struct radv_shader_module cs = { .nir = NULL };

	cs.nir = build_dcc_decompress_compute_shader(device);

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
						&device->meta_state.fast_clear_flush.dcc_decompress_compute_ds_layout);
	if (result != VK_SUCCESS)
		goto cleanup;


	VkPipelineLayoutCreateInfo pl_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &device->meta_state.fast_clear_flush.dcc_decompress_compute_ds_layout,
		.pushConstantRangeCount = 1,
		.pPushConstantRanges = &(VkPushConstantRange){VK_SHADER_STAGE_COMPUTE_BIT, 0, 8},
	};

	result = radv_CreatePipelineLayout(radv_device_to_handle(device),
					  &pl_create_info,
					  &device->meta_state.alloc,
					  &device->meta_state.fast_clear_flush.dcc_decompress_compute_p_layout);
	if (result != VK_SUCCESS)
		goto cleanup;

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
		.layout = device->meta_state.fast_clear_flush.dcc_decompress_compute_p_layout,
	};

	result = radv_CreateComputePipelines(radv_device_to_handle(device),
					     radv_pipeline_cache_to_handle(&device->meta_state.cache),
					     1, &vk_pipeline_info, NULL,
					     &device->meta_state.fast_clear_flush.dcc_decompress_compute_pipeline);
	if (result != VK_SUCCESS)
		goto cleanup;

cleanup:
	ralloc_free(cs.nir);
	return result;
}

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
	attachment.initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
	attachment.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;

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
								       .layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
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
						       .custom_blend_mode = V_028808_CB_DCC_DECOMPRESS,
					       },
					       &device->meta_state.alloc,
					       &device->meta_state.fast_clear_flush.dcc_decompress_pipeline);
	if (result != VK_SUCCESS)
		goto cleanup;

	goto cleanup;

cleanup:
	ralloc_free(fs_module.nir);
	return result;
}

void
radv_device_finish_meta_fast_clear_flush_state(struct radv_device *device)
{
	struct radv_meta_state *state = &device->meta_state;

	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->fast_clear_flush.dcc_decompress_pipeline,
			     &state->alloc);
	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->fast_clear_flush.fmask_decompress_pipeline,
			     &state->alloc);
	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->fast_clear_flush.cmask_eliminate_pipeline,
			     &state->alloc);
	radv_DestroyRenderPass(radv_device_to_handle(device),
			       state->fast_clear_flush.pass, &state->alloc);
	radv_DestroyPipelineLayout(radv_device_to_handle(device),
				   state->fast_clear_flush.p_layout,
				   &state->alloc);

	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->fast_clear_flush.dcc_decompress_compute_pipeline,
			     &state->alloc);
	radv_DestroyPipelineLayout(radv_device_to_handle(device),
				   state->fast_clear_flush.dcc_decompress_compute_p_layout,
				   &state->alloc);
	radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
	                                state->fast_clear_flush.dcc_decompress_compute_ds_layout,
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

	res = create_dcc_compress_compute(device);
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
static void
radv_emit_color_decompress(struct radv_cmd_buffer *cmd_buffer,
                           struct radv_image *image,
                           const VkImageSubresourceRange *subresourceRange,
                           bool decompress_dcc)
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

	if (decompress_dcc && image->surface.dcc_size) {
		pipeline = cmd_buffer->device->meta_state.fast_clear_flush.dcc_decompress_pipeline;
	} else if (image->fmask.size > 0) {
               pipeline = cmd_buffer->device->meta_state.fast_clear_flush.fmask_decompress_pipeline;
	} else {
               pipeline = cmd_buffer->device->meta_state.fast_clear_flush.cmask_eliminate_pipeline;
	}

	if (!decompress_dcc && image->surface.dcc_size) {
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

void
radv_fast_clear_flush_image_inplace(struct radv_cmd_buffer *cmd_buffer,
                                    struct radv_image *image,
                                    const VkImageSubresourceRange *subresourceRange)
{
	radv_emit_color_decompress(cmd_buffer, image, subresourceRange, false);
}

static void
radv_decompress_dcc_gfx(struct radv_cmd_buffer *cmd_buffer,
                        struct radv_image *image,
                        const VkImageSubresourceRange *subresourceRange)
{
	radv_emit_color_decompress(cmd_buffer, image, subresourceRange, true);
}

static void
radv_decompress_dcc_compute(struct radv_cmd_buffer *cmd_buffer,
                            struct radv_image *image,
                            const VkImageSubresourceRange *subresourceRange)
{
	struct radv_meta_saved_state saved_state;
	struct radv_image_view iview = {0};
	struct radv_device *device = cmd_buffer->device;

	/* This assumes the image is 2d with 1 layer and 1 mipmap level */
	struct radv_cmd_state *state = &cmd_buffer->state;

	state->flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
			     RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;

	radv_meta_save(&saved_state, cmd_buffer, RADV_META_SAVE_DESCRIPTORS |
	                                         RADV_META_SAVE_COMPUTE_PIPELINE);

	radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
	                     VK_PIPELINE_BIND_POINT_COMPUTE,
	                     device->meta_state.fast_clear_flush.dcc_decompress_compute_pipeline);

	radv_image_view_init(&iview, cmd_buffer->device,
			     &(VkImageViewCreateInfo) {
				     .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					     .image = radv_image_to_handle(image),
					     .viewType = VK_IMAGE_VIEW_TYPE_2D,
					     .format = image->vk_format,
					     .subresourceRange = {
						.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
						.baseMipLevel = 0,
						.levelCount = 1,
						.baseArrayLayer = 0,
						.layerCount = 1
					     },
			     });

	radv_meta_push_descriptor_set(cmd_buffer,
				      VK_PIPELINE_BIND_POINT_COMPUTE,
				      device->meta_state.fast_clear_flush.dcc_decompress_compute_p_layout,
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
				                                       .imageView = radv_image_view_to_handle(&iview),
				                                       .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				                               },
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
				                                       .imageView = radv_image_view_to_handle(&iview),
				                                       .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				                               },
				                       }
				              }
				      });

	radv_unaligned_dispatch(cmd_buffer, image->info.width, image->info.height, 1);

	/* The fill buffer below does its own saving */
	radv_meta_restore(&saved_state, cmd_buffer);

	state->flush_bits |= RADV_CMD_FLAG_CS_PARTIAL_FLUSH |
			     RADV_CMD_FLAG_INV_VMEM_L1;

	state->flush_bits |= radv_fill_buffer(cmd_buffer, image->bo,
					      image->offset + image->dcc_offset,
					      image->surface.dcc_size, 0xffffffff);

	state->flush_bits |= RADV_CMD_FLAG_FLUSH_AND_INV_CB |
			     RADV_CMD_FLAG_FLUSH_AND_INV_CB_META;
}

void
radv_decompress_dcc(struct radv_cmd_buffer *cmd_buffer,
                    struct radv_image *image,
                    const VkImageSubresourceRange *subresourceRange)
{
	if (cmd_buffer->queue_family_index == RADV_QUEUE_GENERAL)
		radv_decompress_dcc_gfx(cmd_buffer, image, subresourceRange);
	else
		radv_decompress_dcc_compute(cmd_buffer, image, subresourceRange);
}
