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
#include "nir/nir_builder.h"

struct blit_region {
	VkOffset3D src_offset;
	VkExtent3D src_extent;
	VkOffset3D dest_offset;
	VkExtent3D dest_extent;
};

static VkResult
build_pipeline(struct radv_device *device,
               VkImageAspectFlagBits aspect,
               enum glsl_sampler_dim tex_dim,
               unsigned fs_key,
               VkPipeline *pipeline);

static nir_shader *
build_nir_vertex_shader(void)
{
	const struct glsl_type *vec4 = glsl_vec4_type();
	nir_builder b;

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_VERTEX, NULL);
	b.shader->info.name = ralloc_strdup(b.shader, "meta_blit_vs");

	nir_variable *pos_out = nir_variable_create(b.shader, nir_var_shader_out,
						    vec4, "gl_Position");
	pos_out->data.location = VARYING_SLOT_POS;

	nir_variable *tex_pos_out = nir_variable_create(b.shader, nir_var_shader_out,
							vec4, "v_tex_pos");
	tex_pos_out->data.location = VARYING_SLOT_VAR0;
	tex_pos_out->data.interpolation = INTERP_MODE_SMOOTH;

	nir_ssa_def *outvec = radv_meta_gen_rect_vertices(&b);

	nir_store_var(&b, pos_out, outvec, 0xf);

	nir_intrinsic_instr *src_box = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	src_box->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	nir_intrinsic_set_base(src_box, 0);
	nir_intrinsic_set_range(src_box, 16);
	src_box->num_components = 4;
	nir_ssa_dest_init(&src_box->instr, &src_box->dest, 4, 32, "src_box");
	nir_builder_instr_insert(&b, &src_box->instr);

	nir_intrinsic_instr *src0_z = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_push_constant);
	src0_z->src[0] = nir_src_for_ssa(nir_imm_int(&b, 0));
	nir_intrinsic_set_base(src0_z, 16);
	nir_intrinsic_set_range(src0_z, 4);
	src0_z->num_components = 1;
	nir_ssa_dest_init(&src0_z->instr, &src0_z->dest, 1, 32, "src0_z");
	nir_builder_instr_insert(&b, &src0_z->instr);

	nir_intrinsic_instr *vertex_id = nir_intrinsic_instr_create(b.shader, nir_intrinsic_load_vertex_id_zero_base);
	nir_ssa_dest_init(&vertex_id->instr, &vertex_id->dest, 1, 32, "vertexid");
	nir_builder_instr_insert(&b, &vertex_id->instr);

	/* vertex 0 - src0_x, src0_y, src0_z */
	/* vertex 1 - src0_x, src1_y, src0_z*/
	/* vertex 2 - src1_x, src0_y, src0_z */
	/* so channel 0 is vertex_id != 2 ? src_x : src_x + w
	   channel 1 is vertex id != 1 ? src_y : src_y + w */

	nir_ssa_def *c0cmp = nir_ine(&b, &vertex_id->dest.ssa,
				     nir_imm_int(&b, 2));
	nir_ssa_def *c1cmp = nir_ine(&b, &vertex_id->dest.ssa,
				     nir_imm_int(&b, 1));

	nir_ssa_def *comp[4];
	comp[0] = nir_bcsel(&b, c0cmp,
			    nir_channel(&b, &src_box->dest.ssa, 0),
			    nir_channel(&b, &src_box->dest.ssa, 2));

	comp[1] = nir_bcsel(&b, c1cmp,
			    nir_channel(&b, &src_box->dest.ssa, 1),
			    nir_channel(&b, &src_box->dest.ssa, 3));
	comp[2] = &src0_z->dest.ssa;
	comp[3] = nir_imm_float(&b, 1.0);
	nir_ssa_def *out_tex_vec = nir_vec(&b, comp, 4);
	nir_store_var(&b, tex_pos_out, out_tex_vec, 0xf);
	return b.shader;
}

static nir_shader *
build_nir_copy_fragment_shader(enum glsl_sampler_dim tex_dim)
{
	char shader_name[64];
	const struct glsl_type *vec4 = glsl_vec4_type();
	nir_builder b;

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_FRAGMENT, NULL);

	sprintf(shader_name, "meta_blit_fs.%d", tex_dim);
	b.shader->info.name = ralloc_strdup(b.shader, shader_name);

	nir_variable *tex_pos_in = nir_variable_create(b.shader, nir_var_shader_in,
						       vec4, "v_tex_pos");
	tex_pos_in->data.location = VARYING_SLOT_VAR0;

	/* Swizzle the array index which comes in as Z coordinate into the right
	 * position.
	 */
	unsigned swz[] = { 0, (tex_dim == GLSL_SAMPLER_DIM_1D ? 2 : 1), 2 };
	nir_ssa_def *const tex_pos =
		nir_swizzle(&b, nir_load_var(&b, tex_pos_in), swz,
			    (tex_dim == GLSL_SAMPLER_DIM_1D ? 2 : 3));

	const struct glsl_type *sampler_type =
		glsl_sampler_type(tex_dim, false, tex_dim != GLSL_SAMPLER_DIM_3D,
				  glsl_get_base_type(vec4));
	nir_variable *sampler = nir_variable_create(b.shader, nir_var_uniform,
						    sampler_type, "s_tex");
	sampler->data.descriptor_set = 0;
	sampler->data.binding = 0;

	nir_ssa_def *tex_deref = &nir_build_deref_var(&b, sampler)->dest.ssa;

	nir_tex_instr *tex = nir_tex_instr_create(b.shader, 3);
	tex->sampler_dim = tex_dim;
	tex->op = nir_texop_tex;
	tex->src[0].src_type = nir_tex_src_coord;
	tex->src[0].src = nir_src_for_ssa(tex_pos);
	tex->src[1].src_type = nir_tex_src_texture_deref;
	tex->src[1].src = nir_src_for_ssa(tex_deref);
	tex->src[2].src_type = nir_tex_src_sampler_deref;
	tex->src[2].src = nir_src_for_ssa(tex_deref);
	tex->dest_type = nir_type_float; /* TODO */
	tex->is_array = glsl_sampler_type_is_array(sampler_type);
	tex->coord_components = tex_pos->num_components;

	nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
	nir_builder_instr_insert(&b, &tex->instr);

	nir_variable *color_out = nir_variable_create(b.shader, nir_var_shader_out,
						      vec4, "f_color");
	color_out->data.location = FRAG_RESULT_DATA0;
	nir_store_var(&b, color_out, &tex->dest.ssa, 0xf);

	return b.shader;
}

static nir_shader *
build_nir_copy_fragment_shader_depth(enum glsl_sampler_dim tex_dim)
{
	char shader_name[64];
	const struct glsl_type *vec4 = glsl_vec4_type();
	nir_builder b;

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_FRAGMENT, NULL);

	sprintf(shader_name, "meta_blit_depth_fs.%d", tex_dim);
	b.shader->info.name = ralloc_strdup(b.shader, shader_name);

	nir_variable *tex_pos_in = nir_variable_create(b.shader, nir_var_shader_in,
						       vec4, "v_tex_pos");
	tex_pos_in->data.location = VARYING_SLOT_VAR0;

	/* Swizzle the array index which comes in as Z coordinate into the right
	 * position.
	 */
	unsigned swz[] = { 0, (tex_dim == GLSL_SAMPLER_DIM_1D ? 2 : 1), 2 };
	nir_ssa_def *const tex_pos =
		nir_swizzle(&b, nir_load_var(&b, tex_pos_in), swz,
			    (tex_dim == GLSL_SAMPLER_DIM_1D ? 2 : 3));

	const struct glsl_type *sampler_type =
		glsl_sampler_type(tex_dim, false, tex_dim != GLSL_SAMPLER_DIM_3D,
				  glsl_get_base_type(vec4));
	nir_variable *sampler = nir_variable_create(b.shader, nir_var_uniform,
						    sampler_type, "s_tex");
	sampler->data.descriptor_set = 0;
	sampler->data.binding = 0;

	nir_ssa_def *tex_deref = &nir_build_deref_var(&b, sampler)->dest.ssa;

	nir_tex_instr *tex = nir_tex_instr_create(b.shader, 3);
	tex->sampler_dim = tex_dim;
	tex->op = nir_texop_tex;
	tex->src[0].src_type = nir_tex_src_coord;
	tex->src[0].src = nir_src_for_ssa(tex_pos);
	tex->src[1].src_type = nir_tex_src_texture_deref;
	tex->src[1].src = nir_src_for_ssa(tex_deref);
	tex->src[2].src_type = nir_tex_src_sampler_deref;
	tex->src[2].src = nir_src_for_ssa(tex_deref);
	tex->dest_type = nir_type_float; /* TODO */
	tex->is_array = glsl_sampler_type_is_array(sampler_type);
	tex->coord_components = tex_pos->num_components;

	nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
	nir_builder_instr_insert(&b, &tex->instr);

	nir_variable *color_out = nir_variable_create(b.shader, nir_var_shader_out,
						      vec4, "f_color");
	color_out->data.location = FRAG_RESULT_DEPTH;
	nir_store_var(&b, color_out, &tex->dest.ssa, 0x1);

	return b.shader;
}

static nir_shader *
build_nir_copy_fragment_shader_stencil(enum glsl_sampler_dim tex_dim)
{
	char shader_name[64];
	const struct glsl_type *vec4 = glsl_vec4_type();
	nir_builder b;

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_FRAGMENT, NULL);

	sprintf(shader_name, "meta_blit_stencil_fs.%d", tex_dim);
	b.shader->info.name = ralloc_strdup(b.shader, shader_name);

	nir_variable *tex_pos_in = nir_variable_create(b.shader, nir_var_shader_in,
						       vec4, "v_tex_pos");
	tex_pos_in->data.location = VARYING_SLOT_VAR0;

	/* Swizzle the array index which comes in as Z coordinate into the right
	 * position.
	 */
	unsigned swz[] = { 0, (tex_dim == GLSL_SAMPLER_DIM_1D ? 2 : 1), 2 };
	nir_ssa_def *const tex_pos =
		nir_swizzle(&b, nir_load_var(&b, tex_pos_in), swz,
			    (tex_dim == GLSL_SAMPLER_DIM_1D ? 2 : 3));

	const struct glsl_type *sampler_type =
		glsl_sampler_type(tex_dim, false, tex_dim != GLSL_SAMPLER_DIM_3D,
				  glsl_get_base_type(vec4));
	nir_variable *sampler = nir_variable_create(b.shader, nir_var_uniform,
						    sampler_type, "s_tex");
	sampler->data.descriptor_set = 0;
	sampler->data.binding = 0;

	nir_ssa_def *tex_deref = &nir_build_deref_var(&b, sampler)->dest.ssa;

	nir_tex_instr *tex = nir_tex_instr_create(b.shader, 3);
	tex->sampler_dim = tex_dim;
	tex->op = nir_texop_tex;
	tex->src[0].src_type = nir_tex_src_coord;
	tex->src[0].src = nir_src_for_ssa(tex_pos);
	tex->src[1].src_type = nir_tex_src_texture_deref;
	tex->src[1].src = nir_src_for_ssa(tex_deref);
	tex->src[2].src_type = nir_tex_src_sampler_deref;
	tex->src[2].src = nir_src_for_ssa(tex_deref);
	tex->dest_type = nir_type_float; /* TODO */
	tex->is_array = glsl_sampler_type_is_array(sampler_type);
	tex->coord_components = tex_pos->num_components;

	nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
	nir_builder_instr_insert(&b, &tex->instr);

	nir_variable *color_out = nir_variable_create(b.shader, nir_var_shader_out,
						      vec4, "f_color");
	color_out->data.location = FRAG_RESULT_STENCIL;
	nir_store_var(&b, color_out, &tex->dest.ssa, 0x1);

	return b.shader;
}

static enum glsl_sampler_dim
translate_sampler_dim(VkImageType type) {
	switch(type) {
	case VK_IMAGE_TYPE_1D:
		return GLSL_SAMPLER_DIM_1D;
	case VK_IMAGE_TYPE_2D:
		return GLSL_SAMPLER_DIM_2D;
	case VK_IMAGE_TYPE_3D:
		return GLSL_SAMPLER_DIM_3D;
	default:
		unreachable("Unhandled image type");
	}
}

static void
meta_emit_blit(struct radv_cmd_buffer *cmd_buffer,
               struct radv_image *src_image,
               struct radv_image_view *src_iview,
	       VkImageLayout src_image_layout,
               float src_offset_0[3],
               float src_offset_1[3],
               struct radv_image *dest_image,
               struct radv_image_view *dest_iview,
	       VkImageLayout dest_image_layout,
               VkOffset2D dest_offset_0,
               VkOffset2D dest_offset_1,
               VkRect2D dest_box,
               VkSampler sampler)
{
	struct radv_device *device = cmd_buffer->device;
	uint32_t src_width = radv_minify(src_iview->image->info.width, src_iview->base_mip);
	uint32_t src_height = radv_minify(src_iview->image->info.height, src_iview->base_mip);
	uint32_t src_depth = radv_minify(src_iview->image->info.depth, src_iview->base_mip);
	uint32_t dst_width = radv_minify(dest_iview->image->info.width, dest_iview->base_mip);
	uint32_t dst_height = radv_minify(dest_iview->image->info.height, dest_iview->base_mip);

	assert(src_image->info.samples == dest_image->info.samples);

	float vertex_push_constants[5] = {
		src_offset_0[0] / (float)src_width,
		src_offset_0[1] / (float)src_height,
		src_offset_1[0] / (float)src_width,
		src_offset_1[1] / (float)src_height,
		src_offset_0[2] / (float)src_depth,
	};

	radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
			      device->meta_state.blit.pipeline_layout,
			      VK_SHADER_STAGE_VERTEX_BIT, 0, 20,
			      vertex_push_constants);

	VkFramebuffer fb;
	radv_CreateFramebuffer(radv_device_to_handle(device),
			       &(VkFramebufferCreateInfo) {
				       .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
					       .attachmentCount = 1,
					       .pAttachments = (VkImageView[]) {
					       radv_image_view_to_handle(dest_iview),
				       },
				       .width = dst_width,
				       .height = dst_height,
				       .layers = 1,
				}, &cmd_buffer->pool->alloc, &fb);
	VkPipeline* pipeline = NULL;
	unsigned fs_key = 0;
	switch (src_iview->aspect_mask) {
	case VK_IMAGE_ASPECT_COLOR_BIT: {
		unsigned dst_layout = radv_meta_dst_layout_from_layout(dest_image_layout);
		fs_key = radv_format_meta_fs_key(dest_image->vk_format);

		radv_cmd_buffer_begin_render_pass(cmd_buffer,
						  &(VkRenderPassBeginInfo) {
							.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
								.renderPass = device->meta_state.blit.render_pass[fs_key][dst_layout],
								.framebuffer = fb,
								.renderArea = {
									.offset = { dest_box.offset.x, dest_box.offset.y },
									.extent = { dest_box.extent.width, dest_box.extent.height },
								},
							.clearValueCount = 0,
							.pClearValues = NULL,
						});
		switch (src_image->type) {
		case VK_IMAGE_TYPE_1D:
			pipeline = &device->meta_state.blit.pipeline_1d_src[fs_key];
			break;
		case VK_IMAGE_TYPE_2D:
			pipeline = &device->meta_state.blit.pipeline_2d_src[fs_key];
			break;
		case VK_IMAGE_TYPE_3D:
			pipeline = &device->meta_state.blit.pipeline_3d_src[fs_key];
			break;
		default:
			unreachable("bad VkImageType");
		}
		break;
	}
	case VK_IMAGE_ASPECT_DEPTH_BIT: {
		enum radv_blit_ds_layout ds_layout = radv_meta_blit_ds_to_type(dest_image_layout);
		radv_cmd_buffer_begin_render_pass(cmd_buffer,
						  &(VkRenderPassBeginInfo) {
							.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
							.renderPass = device->meta_state.blit.depth_only_rp[ds_layout],
							.framebuffer = fb,
							.renderArea = {
								.offset = { dest_box.offset.x, dest_box.offset.y },
								.extent = { dest_box.extent.width, dest_box.extent.height },
							},
							.clearValueCount = 0,
							.pClearValues = NULL,
						  });
		switch (src_image->type) {
		case VK_IMAGE_TYPE_1D:
			pipeline = &device->meta_state.blit.depth_only_1d_pipeline;
			break;
		case VK_IMAGE_TYPE_2D:
			pipeline = &device->meta_state.blit.depth_only_2d_pipeline;
			break;
		case VK_IMAGE_TYPE_3D:
			pipeline = &device->meta_state.blit.depth_only_3d_pipeline;
			break;
		default:
			unreachable("bad VkImageType");
		}
		break;
	}
	case VK_IMAGE_ASPECT_STENCIL_BIT: {
		enum radv_blit_ds_layout ds_layout = radv_meta_blit_ds_to_type(dest_image_layout);
		radv_cmd_buffer_begin_render_pass(cmd_buffer,
						  &(VkRenderPassBeginInfo) {
							.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
							.renderPass = device->meta_state.blit.stencil_only_rp[ds_layout],
							.framebuffer = fb,
							.renderArea = {
								.offset = { dest_box.offset.x, dest_box.offset.y },
								.extent = { dest_box.extent.width, dest_box.extent.height },
						        },
							.clearValueCount = 0,
							.pClearValues = NULL,
						  });
		switch (src_image->type) {
		case VK_IMAGE_TYPE_1D:
			pipeline = &device->meta_state.blit.stencil_only_1d_pipeline;
			break;
		case VK_IMAGE_TYPE_2D:
			pipeline = &device->meta_state.blit.stencil_only_2d_pipeline;
			break;
		case VK_IMAGE_TYPE_3D:
			pipeline = &device->meta_state.blit.stencil_only_3d_pipeline;
			break;
		default:
			unreachable("bad VkImageType");
		}
		break;
	}
	default:
		unreachable("bad VkImageType");
	}

	radv_cmd_buffer_set_subpass(cmd_buffer,
				    &cmd_buffer->state.pass->subpasses[0]);

	if (!*pipeline) {
		VkResult ret = build_pipeline(device, src_iview->aspect_mask, translate_sampler_dim(src_image->type), fs_key, pipeline);
		if (ret != VK_SUCCESS) {
			cmd_buffer->record_result = ret;
			goto fail_pipeline;
		}
	}

	radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
			     VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline);

	radv_meta_push_descriptor_set(cmd_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
			              device->meta_state.blit.pipeline_layout,
				      0, /* set */
				      1, /* descriptorWriteCount */
				      (VkWriteDescriptorSet[]) {
				              {
				                      .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
				                      .dstBinding = 0,
				                      .dstArrayElement = 0,
				                      .descriptorCount = 1,
				                      .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				                      .pImageInfo = (VkDescriptorImageInfo[]) {
				                              {
				                                      .sampler = sampler,
				                                      .imageView = radv_image_view_to_handle(src_iview),
				                                      .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
				                              },
				                      }
				              }
				      });

	radv_CmdSetViewport(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1, &(VkViewport) {
		.x = dest_offset_0.x,
		.y = dest_offset_0.y,
		.width = dest_offset_1.x - dest_offset_0.x,
		.height = dest_offset_1.y - dest_offset_0.y,
		.minDepth = 0.0f,
		.maxDepth = 1.0f
	});

	radv_CmdSetScissor(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1, &(VkRect2D) {
		.offset = (VkOffset2D) { MIN2(dest_offset_0.x, dest_offset_1.x), MIN2(dest_offset_0.y, dest_offset_1.y) },
		.extent = (VkExtent2D) {
			abs(dest_offset_1.x - dest_offset_0.x),
			abs(dest_offset_1.y - dest_offset_0.y)
		},
	});

	radv_CmdDraw(radv_cmd_buffer_to_handle(cmd_buffer), 3, 1, 0, 0);

fail_pipeline:
	radv_cmd_buffer_end_render_pass(cmd_buffer);

	/* At the point where we emit the draw call, all data from the
	 * descriptor sets, etc. has been used.  We are free to delete it.
	 */
	/* TODO: above comment is not valid for at least descriptor sets/pools,
	 * as we may not free them till after execution finishes. Check others. */

	radv_DestroyFramebuffer(radv_device_to_handle(device), fb,
				&cmd_buffer->pool->alloc);
}

static bool
flip_coords(unsigned *src0, unsigned *src1, unsigned *dst0, unsigned *dst1)
{
	bool flip = false;
	if (*src0 > *src1) {
		unsigned tmp = *src0;
		*src0 = *src1;
		*src1 = tmp;
		flip = !flip;
	}

	if (*dst0 > *dst1) {
		unsigned tmp = *dst0;
		*dst0 = *dst1;
		*dst1 = tmp;
		flip = !flip;
	}
	return flip;
}

static void
blit_image(struct radv_cmd_buffer *cmd_buffer,
	   struct radv_image *src_image,
	   VkImageLayout src_image_layout,
	   struct radv_image *dst_image,
	   VkImageLayout dst_image_layout,
	   const VkImageBlit2KHR *region,
	   VkFilter filter)
{
	const VkImageSubresourceLayers *src_res = &region->srcSubresource;
	const VkImageSubresourceLayers *dst_res = &region->dstSubresource;
	struct radv_device *device = cmd_buffer->device;
	struct radv_meta_saved_state saved_state;
	bool old_predicating;
	VkSampler sampler;

	/* From the Vulkan 1.0 spec:
	 *
	 *    vkCmdBlitImage must not be used for multisampled source or
	 *    destination images. Use vkCmdResolveImage for this purpose.
	 */
	assert(src_image->info.samples == 1);
	assert(dst_image->info.samples == 1);

	radv_CreateSampler(radv_device_to_handle(device),
			   &(VkSamplerCreateInfo) {
				.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
				.magFilter = filter,
				.minFilter = filter,
				.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
				.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
			   }, &cmd_buffer->pool->alloc, &sampler);

	radv_meta_save(&saved_state, cmd_buffer,
		       RADV_META_SAVE_GRAPHICS_PIPELINE |
		       RADV_META_SAVE_CONSTANTS |
		       RADV_META_SAVE_DESCRIPTORS);

	/* VK_EXT_conditional_rendering says that blit commands should not be
	 * affected by conditional rendering.
	 */
	old_predicating = cmd_buffer->state.predicating;
	cmd_buffer->state.predicating = false;

	unsigned dst_start, dst_end;
	if (dst_image->type == VK_IMAGE_TYPE_3D) {
		assert(dst_res->baseArrayLayer == 0);
		dst_start = region->dstOffsets[0].z;
		dst_end = region->dstOffsets[1].z;
	} else {
		dst_start = dst_res->baseArrayLayer;
		dst_end = dst_start + dst_res->layerCount;
	}

	unsigned src_start, src_end;
	if (src_image->type == VK_IMAGE_TYPE_3D) {
		assert(src_res->baseArrayLayer == 0);
		src_start = region->srcOffsets[0].z;
		src_end = region->srcOffsets[1].z;
	} else {
		src_start = src_res->baseArrayLayer;
		src_end = src_start + src_res->layerCount;
	}

	bool flip_z = flip_coords(&src_start, &src_end, &dst_start, &dst_end);
	float src_z_step = (float)(src_end - src_start) /
		(float)(dst_end - dst_start);

	/* There is no interpolation to the pixel center during
	 * rendering, so add the 0.5 offset ourselves here. */
	float depth_center_offset = 0;
	if (src_image->type == VK_IMAGE_TYPE_3D)
		depth_center_offset = 0.5 / (dst_end - dst_start) * (src_end - src_start);

	if (flip_z) {
		src_start = src_end;
		src_z_step *= -1;
		depth_center_offset *= -1;
	}

	unsigned src_x0 = region->srcOffsets[0].x;
	unsigned src_x1 = region->srcOffsets[1].x;
	unsigned dst_x0 = region->dstOffsets[0].x;
	unsigned dst_x1 = region->dstOffsets[1].x;

	unsigned src_y0 = region->srcOffsets[0].y;
	unsigned src_y1 = region->srcOffsets[1].y;
	unsigned dst_y0 = region->dstOffsets[0].y;
	unsigned dst_y1 = region->dstOffsets[1].y;

	VkRect2D dst_box;
	dst_box.offset.x = MIN2(dst_x0, dst_x1);
	dst_box.offset.y = MIN2(dst_y0, dst_y1);
	dst_box.extent.width = dst_x1 - dst_x0;
	dst_box.extent.height = dst_y1 - dst_y0;

	const unsigned num_layers = dst_end - dst_start;
	for (unsigned i = 0; i < num_layers; i++) {
		struct radv_image_view dst_iview, src_iview;

		const VkOffset2D dst_offset_0 = {
			.x = dst_x0,
			.y = dst_y0,
		};
		const VkOffset2D dst_offset_1 = {
			.x = dst_x1,
			.y = dst_y1,
		};

		float src_offset_0[3] = {
			src_x0,
			src_y0,
			src_start + i * src_z_step + depth_center_offset,
		};
		float src_offset_1[3] = {
			src_x1,
			src_y1,
			src_start + i * src_z_step + depth_center_offset,
		};
		const uint32_t dst_array_slice = dst_start + i;

		/* 3D images have just 1 layer */
		const uint32_t src_array_slice = src_image->type == VK_IMAGE_TYPE_3D ? 0 : src_start + i;

		radv_image_view_init(&dst_iview, cmd_buffer->device,
				     &(VkImageViewCreateInfo) {
					     .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
						     .image = radv_image_to_handle(dst_image),
						     .viewType = radv_meta_get_view_type(dst_image),
						     .format = dst_image->vk_format,
						     .subresourceRange = {
						     .aspectMask = dst_res->aspectMask,
						     .baseMipLevel = dst_res->mipLevel,
						     .levelCount = 1,
						     .baseArrayLayer = dst_array_slice,
						     .layerCount = 1
					     },
				     }, NULL);
		radv_image_view_init(&src_iview, cmd_buffer->device,
				     &(VkImageViewCreateInfo) {
					.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
						.image = radv_image_to_handle(src_image),
						.viewType = radv_meta_get_view_type(src_image),
						.format = src_image->vk_format,
						.subresourceRange = {
						.aspectMask = src_res->aspectMask,
						.baseMipLevel = src_res->mipLevel,
						.levelCount = 1,
						.baseArrayLayer = src_array_slice,
						.layerCount = 1
					},
				}, NULL);
		meta_emit_blit(cmd_buffer,
			       src_image, &src_iview, src_image_layout,
			       src_offset_0, src_offset_1,
			       dst_image, &dst_iview, dst_image_layout,
			       dst_offset_0, dst_offset_1,
			       dst_box,
			       sampler);
	}

	/* Restore conditional rendering. */
	cmd_buffer->state.predicating = old_predicating;

	radv_meta_restore(&saved_state, cmd_buffer);

	radv_DestroySampler(radv_device_to_handle(device), sampler,
			    &cmd_buffer->pool->alloc);
}

void radv_CmdBlitImage(
	VkCommandBuffer                             commandBuffer,
	VkImage                                     srcImage,
	VkImageLayout                               srcImageLayout,
	VkImage                                     dstImage,
	VkImageLayout                               dstImageLayout,
	uint32_t                                    regionCount,
	const VkImageBlit*                          pRegions,
	VkFilter                                    filter)

{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_image, src_image, srcImage);
	RADV_FROM_HANDLE(radv_image, dst_image, dstImage);

	for (unsigned r = 0; r < regionCount; r++) {
		VkImageBlit2KHR blit = {
			.sType          = VK_STRUCTURE_TYPE_IMAGE_BLIT_2_KHR,
			.srcSubresource = pRegions[r].srcSubresource,
			.srcOffsets     = {
				pRegions[r].srcOffsets[0],
				pRegions[r].srcOffsets[1],
			},
			.dstSubresource = pRegions[r].dstSubresource,
			.dstOffsets     = {
				pRegions[r].dstOffsets[0],
				pRegions[r].dstOffsets[1],
			},
		};

		blit_image(cmd_buffer,
			   src_image, srcImageLayout,
			   dst_image, dstImageLayout,
			   &blit, filter);
	}
}

void radv_CmdBlitImage2KHR(
	VkCommandBuffer                             commandBuffer,
	const VkBlitImageInfo2KHR*                  pBlitImageInfo)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_image, src_image, pBlitImageInfo->srcImage);
	RADV_FROM_HANDLE(radv_image, dst_image, pBlitImageInfo->dstImage);

	for (unsigned r = 0; r < pBlitImageInfo->regionCount; r++) {
		blit_image(cmd_buffer,
			   src_image, pBlitImageInfo->srcImageLayout,
			   dst_image, pBlitImageInfo->dstImageLayout,
			   &pBlitImageInfo->pRegions[r],
			   pBlitImageInfo->filter);
	}
}

void
radv_device_finish_meta_blit_state(struct radv_device *device)
{
	struct radv_meta_state *state = &device->meta_state;

	for (unsigned i = 0; i < NUM_META_FS_KEYS; ++i) {
		for (unsigned j = 0; j < RADV_META_DST_LAYOUT_COUNT; ++j) {
			radv_DestroyRenderPass(radv_device_to_handle(device),
			                       state->blit.render_pass[i][j],
			                       &state->alloc);
		}
		radv_DestroyPipeline(radv_device_to_handle(device),
				     state->blit.pipeline_1d_src[i],
				     &state->alloc);
		radv_DestroyPipeline(radv_device_to_handle(device),
				     state->blit.pipeline_2d_src[i],
				     &state->alloc);
		radv_DestroyPipeline(radv_device_to_handle(device),
				     state->blit.pipeline_3d_src[i],
				     &state->alloc);
	}

	for (enum radv_blit_ds_layout i = RADV_BLIT_DS_LAYOUT_TILE_ENABLE; i < RADV_BLIT_DS_LAYOUT_COUNT; i++) {
		radv_DestroyRenderPass(radv_device_to_handle(device),
				       state->blit.depth_only_rp[i], &state->alloc);
		radv_DestroyRenderPass(radv_device_to_handle(device),
				       state->blit.stencil_only_rp[i], &state->alloc);
	}

	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->blit.depth_only_1d_pipeline, &state->alloc);
	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->blit.depth_only_2d_pipeline, &state->alloc);
	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->blit.depth_only_3d_pipeline, &state->alloc);

	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->blit.stencil_only_1d_pipeline,
			     &state->alloc);
	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->blit.stencil_only_2d_pipeline,
			     &state->alloc);
	radv_DestroyPipeline(radv_device_to_handle(device),
			     state->blit.stencil_only_3d_pipeline,
			     &state->alloc);


	radv_DestroyPipelineLayout(radv_device_to_handle(device),
				   state->blit.pipeline_layout, &state->alloc);
	radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
					state->blit.ds_layout, &state->alloc);
}

static VkResult
build_pipeline(struct radv_device *device,
               VkImageAspectFlagBits aspect,
               enum glsl_sampler_dim tex_dim,
               unsigned fs_key,
               VkPipeline *pipeline)
{
	VkResult result = VK_SUCCESS;

	mtx_lock(&device->meta_state.mtx);

	if (*pipeline) {
		mtx_unlock(&device->meta_state.mtx);
		return VK_SUCCESS;
	}

	struct radv_shader_module fs = {0};
	struct radv_shader_module vs = {.nir = build_nir_vertex_shader()};
	VkRenderPass rp;

	switch(aspect) {
	case VK_IMAGE_ASPECT_COLOR_BIT:
		fs.nir = build_nir_copy_fragment_shader(tex_dim);
		rp = device->meta_state.blit.render_pass[fs_key][0];
		break;
	case VK_IMAGE_ASPECT_DEPTH_BIT:
		fs.nir = build_nir_copy_fragment_shader_depth(tex_dim);
		rp = device->meta_state.blit.depth_only_rp[0];
		break;
	case VK_IMAGE_ASPECT_STENCIL_BIT:
		fs.nir = build_nir_copy_fragment_shader_stencil(tex_dim);
		rp = device->meta_state.blit.stencil_only_rp[0];
		break;
	default:
		unreachable("Unhandled aspect");
	}
	VkPipelineVertexInputStateCreateInfo vi_create_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 0,
		.vertexAttributeDescriptionCount = 0,
	};

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

	VkGraphicsPipelineCreateInfo vk_pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = ARRAY_SIZE(pipeline_shader_stages),
		.pStages = pipeline_shader_stages,
		.pVertexInputState = &vi_create_info,
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
		.pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = 4,
			.pDynamicStates = (VkDynamicState[]) {
				VK_DYNAMIC_STATE_VIEWPORT,
				VK_DYNAMIC_STATE_SCISSOR,
				VK_DYNAMIC_STATE_LINE_WIDTH,
				VK_DYNAMIC_STATE_BLEND_CONSTANTS,
			},
		},
		.flags = 0,
		.layout = device->meta_state.blit.pipeline_layout,
		.renderPass = rp,
		.subpass = 0,
	};

	VkPipelineColorBlendStateCreateInfo color_blend_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = (VkPipelineColorBlendAttachmentState []) {
			{
				.colorWriteMask = VK_COLOR_COMPONENT_A_BIT |
						  VK_COLOR_COMPONENT_R_BIT |
						  VK_COLOR_COMPONENT_G_BIT |
						  VK_COLOR_COMPONENT_B_BIT },
			}
		};

	VkPipelineDepthStencilStateCreateInfo depth_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = true,
		.depthWriteEnable = true,
		.depthCompareOp = VK_COMPARE_OP_ALWAYS,
	};

	VkPipelineDepthStencilStateCreateInfo stencil_info = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
		.depthTestEnable = false,
		.depthWriteEnable = false,
		.stencilTestEnable = true,
		.front = {
			.failOp = VK_STENCIL_OP_REPLACE,
			.passOp = VK_STENCIL_OP_REPLACE,
			.depthFailOp = VK_STENCIL_OP_REPLACE,
			.compareOp = VK_COMPARE_OP_ALWAYS,
			.compareMask = 0xff,
			.writeMask = 0xff,
			.reference = 0
		},
		.back = {
			.failOp = VK_STENCIL_OP_REPLACE,
			.passOp = VK_STENCIL_OP_REPLACE,
			.depthFailOp = VK_STENCIL_OP_REPLACE,
			.compareOp = VK_COMPARE_OP_ALWAYS,
			.compareMask = 0xff,
			.writeMask = 0xff,
			.reference = 0
		},
		.depthCompareOp = VK_COMPARE_OP_ALWAYS,
	};

	switch(aspect) {
	case VK_IMAGE_ASPECT_COLOR_BIT:
		vk_pipeline_info.pColorBlendState = &color_blend_info;
		break;
	case VK_IMAGE_ASPECT_DEPTH_BIT:
		vk_pipeline_info.pDepthStencilState = &depth_info;
		break;
	case VK_IMAGE_ASPECT_STENCIL_BIT:
		vk_pipeline_info.pDepthStencilState = &stencil_info;
		break;
	default:
		unreachable("Unhandled aspect");
	}

	const struct radv_graphics_pipeline_create_info radv_pipeline_info = {
		.use_rectlist = true
	};

	result = radv_graphics_pipeline_create(radv_device_to_handle(device),
	                                       radv_pipeline_cache_to_handle(&device->meta_state.cache),
	                                       &vk_pipeline_info, &radv_pipeline_info,
	                                       &device->meta_state.alloc, pipeline);
	ralloc_free(vs.nir);
	ralloc_free(fs.nir);
	mtx_unlock(&device->meta_state.mtx);
	return result;
}

static VkResult
radv_device_init_meta_blit_color(struct radv_device *device, bool on_demand)
{
	VkResult result;

	for (unsigned i = 0; i < NUM_META_FS_KEYS; ++i) {
		unsigned key = radv_format_meta_fs_key(radv_fs_key_format_exemplars[i]);
		for(unsigned j = 0; j < RADV_META_DST_LAYOUT_COUNT; ++j) {
			VkImageLayout layout = radv_meta_dst_layout_to_layout(j);
			result = radv_CreateRenderPass(radv_device_to_handle(device),
						&(VkRenderPassCreateInfo) {
							.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
								.attachmentCount = 1,
								.pAttachments = &(VkAttachmentDescription) {
								.format = radv_fs_key_format_exemplars[i],
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
						}, &device->meta_state.alloc, &device->meta_state.blit.render_pass[key][j]);
			if (result != VK_SUCCESS)
				goto fail;
		}

		if (on_demand)
			continue;

		result = build_pipeline(device, VK_IMAGE_ASPECT_COLOR_BIT, GLSL_SAMPLER_DIM_1D, key, &device->meta_state.blit.pipeline_1d_src[key]);
		if (result != VK_SUCCESS)
			goto fail;

		result = build_pipeline(device, VK_IMAGE_ASPECT_COLOR_BIT, GLSL_SAMPLER_DIM_2D, key, &device->meta_state.blit.pipeline_2d_src[key]);
		if (result != VK_SUCCESS)
			goto fail;

		result = build_pipeline(device, VK_IMAGE_ASPECT_COLOR_BIT, GLSL_SAMPLER_DIM_3D, key, &device->meta_state.blit.pipeline_3d_src[key]);
		if (result != VK_SUCCESS)
			goto fail;

	}

	result = VK_SUCCESS;
fail:
	return result;
}

static VkResult
radv_device_init_meta_blit_depth(struct radv_device *device, bool on_demand)
{
	VkResult result;

	for (enum radv_blit_ds_layout ds_layout = RADV_BLIT_DS_LAYOUT_TILE_ENABLE; ds_layout < RADV_BLIT_DS_LAYOUT_COUNT; ds_layout++) {
		VkImageLayout layout = radv_meta_blit_ds_to_layout(ds_layout);
		result = radv_CreateRenderPass(radv_device_to_handle(device),
					       &(VkRenderPassCreateInfo) {
						       .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
						       .attachmentCount = 1,
						       .pAttachments = &(VkAttachmentDescription) {
							       .format = VK_FORMAT_D32_SFLOAT,
							       .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
							       .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
							       .initialLayout = layout,
							       .finalLayout = layout,
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
								       .layout = layout,
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
						}, &device->meta_state.alloc, &device->meta_state.blit.depth_only_rp[ds_layout]);
		if (result != VK_SUCCESS)
			goto fail;
	}

	if (on_demand)
		return VK_SUCCESS;

	result = build_pipeline(device, VK_IMAGE_ASPECT_DEPTH_BIT, GLSL_SAMPLER_DIM_1D, 0, &device->meta_state.blit.depth_only_1d_pipeline);
	if (result != VK_SUCCESS)
		goto fail;

	result = build_pipeline(device, VK_IMAGE_ASPECT_DEPTH_BIT, GLSL_SAMPLER_DIM_2D, 0, &device->meta_state.blit.depth_only_2d_pipeline);
	if (result != VK_SUCCESS)
		goto fail;

	result = build_pipeline(device, VK_IMAGE_ASPECT_DEPTH_BIT, GLSL_SAMPLER_DIM_3D, 0, &device->meta_state.blit.depth_only_3d_pipeline);
	if (result != VK_SUCCESS)
		goto fail;

fail:
	return result;
}

static VkResult
radv_device_init_meta_blit_stencil(struct radv_device *device, bool on_demand)
{
	VkResult result;

	for (enum radv_blit_ds_layout ds_layout = RADV_BLIT_DS_LAYOUT_TILE_ENABLE; ds_layout < RADV_BLIT_DS_LAYOUT_COUNT; ds_layout++) {
		VkImageLayout layout = radv_meta_blit_ds_to_layout(ds_layout);
		result = radv_CreateRenderPass(radv_device_to_handle(device),
					       &(VkRenderPassCreateInfo) {
						       .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
						       .attachmentCount = 1,
						       .pAttachments = &(VkAttachmentDescription) {
							       .format = VK_FORMAT_S8_UINT,
							       .loadOp = VK_ATTACHMENT_LOAD_OP_LOAD,
							       .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
							       .initialLayout = layout,
							       .finalLayout = layout,
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
								       .layout = layout,
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

					 }, &device->meta_state.alloc, &device->meta_state.blit.stencil_only_rp[ds_layout]);
	}
	if (result != VK_SUCCESS)
		goto fail;

	if (on_demand)
		return VK_SUCCESS;

	result = build_pipeline(device, VK_IMAGE_ASPECT_STENCIL_BIT, GLSL_SAMPLER_DIM_1D, 0, &device->meta_state.blit.stencil_only_1d_pipeline);
	if (result != VK_SUCCESS)
		goto fail;

	result = build_pipeline(device, VK_IMAGE_ASPECT_STENCIL_BIT, GLSL_SAMPLER_DIM_2D, 0, &device->meta_state.blit.stencil_only_2d_pipeline);
	if (result != VK_SUCCESS)
		goto fail;

	result = build_pipeline(device, VK_IMAGE_ASPECT_STENCIL_BIT, GLSL_SAMPLER_DIM_3D, 0, &device->meta_state.blit.stencil_only_3d_pipeline);
	if (result != VK_SUCCESS)
		goto fail;


fail:
	return result;
}

VkResult
radv_device_init_meta_blit_state(struct radv_device *device, bool on_demand)
{
	VkResult result;

	VkDescriptorSetLayoutCreateInfo ds_layout_info = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR,
		.bindingCount = 1,
		.pBindings = (VkDescriptorSetLayoutBinding[]) {
			{
				.binding = 0,
				.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				.descriptorCount = 1,
				.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
				.pImmutableSamplers = NULL
			},
		}
	};
	result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device),
						&ds_layout_info,
						&device->meta_state.alloc,
						&device->meta_state.blit.ds_layout);
	if (result != VK_SUCCESS)
		goto fail;

	const VkPushConstantRange push_constant_range = {VK_SHADER_STAGE_VERTEX_BIT, 0, 20};

	result = radv_CreatePipelineLayout(radv_device_to_handle(device),
					   &(VkPipelineLayoutCreateInfo) {
						   .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
							   .setLayoutCount = 1,
							   .pSetLayouts = &device->meta_state.blit.ds_layout,
							   .pushConstantRangeCount = 1,
							   .pPushConstantRanges = &push_constant_range,
							   },
					   &device->meta_state.alloc, &device->meta_state.blit.pipeline_layout);
	if (result != VK_SUCCESS)
		goto fail;

	result = radv_device_init_meta_blit_color(device, on_demand);
	if (result != VK_SUCCESS)
		goto fail;

	result = radv_device_init_meta_blit_depth(device, on_demand);
	if (result != VK_SUCCESS)
		goto fail;

	result = radv_device_init_meta_blit_stencil(device, on_demand);

fail:
	if (result != VK_SUCCESS)
		radv_device_finish_meta_blit_state(device);
	return result;
}
