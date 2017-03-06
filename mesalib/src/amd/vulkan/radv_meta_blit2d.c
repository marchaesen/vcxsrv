/*
 * Copyright © 2016 Red Hat
 *
 * based on anv driver:
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

#include "radv_meta.h"
#include "nir/nir_builder.h"
#include "vk_format.h"

enum blit2d_dst_type {
	/* We can bind this destination as a "normal" render target and render
	 * to it just like you would anywhere else.
	 */
	BLIT2D_DST_TYPE_NORMAL,

	/* The destination has a 3-channel RGB format.  Since we can't render to
	 * non-power-of-two textures, we have to bind it as a red texture and
	 * select the correct component for the given red pixel in the shader.
	 */
	BLIT2D_DST_TYPE_RGB,

	BLIT2D_NUM_DST_TYPES,
};


enum blit2d_src_type {
	BLIT2D_SRC_TYPE_IMAGE,
	BLIT2D_SRC_TYPE_BUFFER,
	BLIT2D_NUM_SRC_TYPES,
};

static void
create_iview(struct radv_cmd_buffer *cmd_buffer,
             struct radv_meta_blit2d_surf *surf,
             VkImageUsageFlags usage,
             struct radv_image_view *iview, VkFormat depth_format)
{
	VkFormat format;

	if (depth_format)
		format = depth_format;
	else
		format = surf->format;

	radv_image_view_init(iview, cmd_buffer->device,
			     &(VkImageViewCreateInfo) {
				     .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
					     .image = radv_image_to_handle(surf->image),
					     .viewType = VK_IMAGE_VIEW_TYPE_2D,
					     .format = format,
					     .subresourceRange = {
					     .aspectMask = surf->aspect_mask,
					     .baseMipLevel = surf->level,
					     .levelCount = 1,
					     .baseArrayLayer = surf->layer,
					     .layerCount = 1
				     },
					     }, cmd_buffer, usage);
}

static void
create_bview(struct radv_cmd_buffer *cmd_buffer,
	     struct radv_meta_blit2d_buffer *src,
	     struct radv_buffer_view *bview, VkFormat depth_format)
{
	VkFormat format;

	if (depth_format)
		format = depth_format;
	else
		format = src->format;
	radv_buffer_view_init(bview, cmd_buffer->device,
			      &(VkBufferViewCreateInfo) {
				      .sType = VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO,
				      .flags = 0,
				      .buffer = radv_buffer_to_handle(src->buffer),
				      .format = format,
				      .offset = src->offset,
				      .range = VK_WHOLE_SIZE,
			      }, cmd_buffer);

}

struct blit2d_src_temps {
	struct radv_image_view iview;

	VkDescriptorSet set;
	struct radv_buffer_view bview;
};

static void
blit2d_bind_src(struct radv_cmd_buffer *cmd_buffer,
                struct radv_meta_blit2d_surf *src_img,
                struct radv_meta_blit2d_buffer *src_buf,
                struct blit2d_src_temps *tmp,
                enum blit2d_src_type src_type, VkFormat depth_format)
{
	struct radv_device *device = cmd_buffer->device;
	VkDevice vk_device = radv_device_to_handle(cmd_buffer->device);

	if (src_type == BLIT2D_SRC_TYPE_BUFFER) {
		create_bview(cmd_buffer, src_buf, &tmp->bview, depth_format);

		radv_temp_descriptor_set_create(cmd_buffer->device, cmd_buffer,
					        device->meta_state.blit2d.ds_layouts[src_type],
					        &tmp->set);

		radv_UpdateDescriptorSets(vk_device,
					  1, /* writeCount */
					  (VkWriteDescriptorSet[]) {
						  {
							  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
							  .dstSet = tmp->set,
							  .dstBinding = 0,
							  .dstArrayElement = 0,
							  .descriptorCount = 1,
							  .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
							  .pTexelBufferView = (VkBufferView[])  { radv_buffer_view_to_handle(&tmp->bview) }
						  }
					  }, 0, NULL);

		radv_CmdPushConstants(radv_cmd_buffer_to_handle(cmd_buffer),
				      device->meta_state.blit2d.p_layouts[src_type],
				      VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4,
				      &src_buf->pitch);
	} else {
		create_iview(cmd_buffer, src_img, VK_IMAGE_USAGE_SAMPLED_BIT, &tmp->iview,
			     depth_format);

		radv_temp_descriptor_set_create(cmd_buffer->device, cmd_buffer,
					        device->meta_state.blit2d.ds_layouts[src_type],
					        &tmp->set);

		radv_UpdateDescriptorSets(vk_device,
					  1, /* writeCount */
					  (VkWriteDescriptorSet[]) {
						  {
							  .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
								  .dstSet = tmp->set,
								  .dstBinding = 0,
								  .dstArrayElement = 0,
								  .descriptorCount = 1,
								  .descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
								  .pImageInfo = (VkDescriptorImageInfo[]) {
								  {
									  .sampler = VK_NULL_HANDLE,
									  .imageView = radv_image_view_to_handle(&tmp->iview),
									  .imageLayout = VK_IMAGE_LAYOUT_GENERAL,
								  },
							  }
						  }
					  }, 0, NULL);

	}

	radv_CmdBindDescriptorSets(radv_cmd_buffer_to_handle(cmd_buffer),
				   VK_PIPELINE_BIND_POINT_GRAPHICS,
				   device->meta_state.blit2d.p_layouts[src_type], 0, 1,
				   &tmp->set, 0, NULL);
}

static void
blit2d_unbind_src(struct radv_cmd_buffer *cmd_buffer,
                  struct blit2d_src_temps *tmp,
                  enum blit2d_src_type src_type)
{
	radv_temp_descriptor_set_destroy(cmd_buffer->device, tmp->set);
}

struct blit2d_dst_temps {
	VkImage image;
	struct radv_image_view iview;
	VkFramebuffer fb;
};

static void
blit2d_bind_dst(struct radv_cmd_buffer *cmd_buffer,
                struct radv_meta_blit2d_surf *dst,
                uint32_t width,
                uint32_t height,
		VkFormat depth_format,
                struct blit2d_dst_temps *tmp)
{
	VkImageUsageFlagBits bits;

	if (dst->aspect_mask == VK_IMAGE_ASPECT_COLOR_BIT)
		bits = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	else
		bits = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

	create_iview(cmd_buffer, dst, bits,
		     &tmp->iview, depth_format);

	radv_CreateFramebuffer(radv_device_to_handle(cmd_buffer->device),
			       &(VkFramebufferCreateInfo) {
				       .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
					       .attachmentCount = 1,
					       .pAttachments = (VkImageView[]) {
					       radv_image_view_to_handle(&tmp->iview),
				       },
				       .width = width,
				       .height = height,
				       .layers = 1
				}, &cmd_buffer->pool->alloc, &tmp->fb);
}

static void
blit2d_unbind_dst(struct radv_cmd_buffer *cmd_buffer,
                  struct blit2d_dst_temps *tmp)
{
	VkDevice vk_device = radv_device_to_handle(cmd_buffer->device);
	radv_DestroyFramebuffer(vk_device, tmp->fb, &cmd_buffer->pool->alloc);
}

static void
bind_pipeline(struct radv_cmd_buffer *cmd_buffer,
              enum blit2d_src_type src_type, unsigned fs_key)
{
	VkPipeline pipeline =
		cmd_buffer->device->meta_state.blit2d.pipelines[src_type][fs_key];

	if (cmd_buffer->state.pipeline != radv_pipeline_from_handle(pipeline)) {
		radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
				     VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	}
}

static void
bind_depth_pipeline(struct radv_cmd_buffer *cmd_buffer,
		    enum blit2d_src_type src_type)
{
	VkPipeline pipeline =
		cmd_buffer->device->meta_state.blit2d.depth_only_pipeline[src_type];

	if (cmd_buffer->state.pipeline != radv_pipeline_from_handle(pipeline)) {
		radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
				     VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	}
}

static void
bind_stencil_pipeline(struct radv_cmd_buffer *cmd_buffer,
		      enum blit2d_src_type src_type)
{
	VkPipeline pipeline =
		cmd_buffer->device->meta_state.blit2d.stencil_only_pipeline[src_type];

	if (cmd_buffer->state.pipeline != radv_pipeline_from_handle(pipeline)) {
		radv_CmdBindPipeline(radv_cmd_buffer_to_handle(cmd_buffer),
				     VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	}
}

static void
radv_meta_blit2d_normal_dst(struct radv_cmd_buffer *cmd_buffer,
			    struct radv_meta_blit2d_surf *src_img,
			    struct radv_meta_blit2d_buffer *src_buf,
			    struct radv_meta_blit2d_surf *dst,
			    unsigned num_rects,
			    struct radv_meta_blit2d_rect *rects, enum blit2d_src_type src_type)
{
	struct radv_device *device = cmd_buffer->device;

	for (unsigned r = 0; r < num_rects; ++r) {
		VkFormat depth_format = 0;
		if (dst->aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT)
			depth_format = vk_format_stencil_only(dst->image->vk_format);
		else if (dst->aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT)
			depth_format = vk_format_depth_only(dst->image->vk_format);
		struct blit2d_src_temps src_temps;
		blit2d_bind_src(cmd_buffer, src_img, src_buf, &src_temps, src_type, depth_format);

		uint32_t offset = 0;
		struct blit2d_dst_temps dst_temps;
		blit2d_bind_dst(cmd_buffer, dst, rects[r].dst_x + rects[r].width,
				rects[r].dst_y + rects[r].height, depth_format, &dst_temps);

		struct blit_vb_data {
			float pos[2];
			float tex_coord[2];
		} vb_data[3];

		unsigned vb_size = 3 * sizeof(*vb_data);

		vb_data[0] = (struct blit_vb_data) {
			.pos = {
				rects[r].dst_x,
				rects[r].dst_y,
			},
			.tex_coord = {
				rects[r].src_x,
				rects[r].src_y,
			},
		};

		vb_data[1] = (struct blit_vb_data) {
			.pos = {
				rects[r].dst_x,
				rects[r].dst_y + rects[r].height,
			},
			.tex_coord = {
				rects[r].src_x,
				rects[r].src_y + rects[r].height,
			},
		};

		vb_data[2] = (struct blit_vb_data) {
			.pos = {
				rects[r].dst_x + rects[r].width,
				rects[r].dst_y,
			},
			.tex_coord = {
				rects[r].src_x + rects[r].width,
				rects[r].src_y,
			},
		};


		radv_cmd_buffer_upload_data(cmd_buffer, vb_size, 16, vb_data, &offset);

		struct radv_buffer vertex_buffer = {
			.device = device,
			.size = vb_size,
			.bo = cmd_buffer->upload.upload_bo,
			.offset = offset,
		};

		radv_CmdBindVertexBuffers(radv_cmd_buffer_to_handle(cmd_buffer), 0, 1,
					  (VkBuffer[]) {
						  radv_buffer_to_handle(&vertex_buffer),
							  },
					  (VkDeviceSize[]) {
						  0,
							  });


		if (dst->aspect_mask == VK_IMAGE_ASPECT_COLOR_BIT) {
			unsigned fs_key = radv_format_meta_fs_key(dst_temps.iview.vk_format);

			radv_CmdBeginRenderPass(radv_cmd_buffer_to_handle(cmd_buffer),
						      &(VkRenderPassBeginInfo) {
							      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
								      .renderPass = device->meta_state.blit2d.render_passes[fs_key],
								      .framebuffer = dst_temps.fb,
								      .renderArea = {
								      .offset = { rects[r].dst_x, rects[r].dst_y, },
								      .extent = { rects[r].width, rects[r].height },
							      },
								      .clearValueCount = 0,
									       .pClearValues = NULL,
									       }, VK_SUBPASS_CONTENTS_INLINE);


			bind_pipeline(cmd_buffer, src_type, fs_key);
		} else if (dst->aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT) {
			radv_CmdBeginRenderPass(radv_cmd_buffer_to_handle(cmd_buffer),
						      &(VkRenderPassBeginInfo) {
							      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
								      .renderPass = device->meta_state.blit2d.depth_only_rp,
								      .framebuffer = dst_temps.fb,
								      .renderArea = {
								      .offset = { rects[r].dst_x, rects[r].dst_y, },
								      .extent = { rects[r].width, rects[r].height },
							      },
								      .clearValueCount = 0,
									       .pClearValues = NULL,
									       }, VK_SUBPASS_CONTENTS_INLINE);


			bind_depth_pipeline(cmd_buffer, src_type);

		} else if (dst->aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT) {
			radv_CmdBeginRenderPass(radv_cmd_buffer_to_handle(cmd_buffer),
						      &(VkRenderPassBeginInfo) {
							      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
								      .renderPass = device->meta_state.blit2d.stencil_only_rp,
								      .framebuffer = dst_temps.fb,
								      .renderArea = {
								      .offset = { rects[r].dst_x, rects[r].dst_y, },
								      .extent = { rects[r].width, rects[r].height },
							      },
								      .clearValueCount = 0,
									       .pClearValues = NULL,
									       }, VK_SUBPASS_CONTENTS_INLINE);


			bind_stencil_pipeline(cmd_buffer, src_type);
		}

		radv_CmdDraw(radv_cmd_buffer_to_handle(cmd_buffer), 3, 1, 0, 0);
		radv_CmdEndRenderPass(radv_cmd_buffer_to_handle(cmd_buffer));

		/* At the point where we emit the draw call, all data from the
		 * descriptor sets, etc. has been used.  We are free to delete it.
		 */
		blit2d_unbind_src(cmd_buffer, &src_temps, src_type);
		blit2d_unbind_dst(cmd_buffer, &dst_temps);
	}
}

void
radv_meta_blit2d(struct radv_cmd_buffer *cmd_buffer,
		 struct radv_meta_blit2d_surf *src_img,
		 struct radv_meta_blit2d_buffer *src_buf,
		 struct radv_meta_blit2d_surf *dst,
		 unsigned num_rects,
		 struct radv_meta_blit2d_rect *rects)
{
	enum blit2d_src_type src_type = src_buf ? BLIT2D_SRC_TYPE_BUFFER :
						  BLIT2D_SRC_TYPE_IMAGE;
	radv_meta_blit2d_normal_dst(cmd_buffer, src_img, src_buf, dst,
				    num_rects, rects, src_type);
}

static nir_shader *
build_nir_vertex_shader(void)
{
	const struct glsl_type *vec4 = glsl_vec4_type();
	const struct glsl_type *vec2 = glsl_vector_type(GLSL_TYPE_FLOAT, 2);
	nir_builder b;

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_VERTEX, NULL);
	b.shader->info->name = ralloc_strdup(b.shader, "meta_blit_vs");

	nir_variable *pos_in = nir_variable_create(b.shader, nir_var_shader_in,
						   vec4, "a_pos");
	pos_in->data.location = VERT_ATTRIB_GENERIC0;
	nir_variable *pos_out = nir_variable_create(b.shader, nir_var_shader_out,
						    vec4, "gl_Position");
	pos_out->data.location = VARYING_SLOT_POS;
	nir_copy_var(&b, pos_out, pos_in);

	nir_variable *tex_pos_in = nir_variable_create(b.shader, nir_var_shader_in,
						       vec2, "a_tex_pos");
	tex_pos_in->data.location = VERT_ATTRIB_GENERIC1;
	nir_variable *tex_pos_out = nir_variable_create(b.shader, nir_var_shader_out,
							vec2, "v_tex_pos");
	tex_pos_out->data.location = VARYING_SLOT_VAR0;
	tex_pos_out->data.interpolation = INTERP_MODE_SMOOTH;
	nir_copy_var(&b, tex_pos_out, tex_pos_in);

	return b.shader;
}

typedef nir_ssa_def* (*texel_fetch_build_func)(struct nir_builder *,
                                               struct radv_device *,
                                               nir_ssa_def *);

static nir_ssa_def *
build_nir_texel_fetch(struct nir_builder *b, struct radv_device *device,
                      nir_ssa_def *tex_pos)
{
	const struct glsl_type *sampler_type =
		glsl_sampler_type(GLSL_SAMPLER_DIM_2D, false, false, GLSL_TYPE_UINT);
	nir_variable *sampler = nir_variable_create(b->shader, nir_var_uniform,
						    sampler_type, "s_tex");
	sampler->data.descriptor_set = 0;
	sampler->data.binding = 0;

	nir_tex_instr *tex = nir_tex_instr_create(b->shader, 2);
	tex->sampler_dim = GLSL_SAMPLER_DIM_2D;
	tex->op = nir_texop_txf;
	tex->src[0].src_type = nir_tex_src_coord;
	tex->src[0].src = nir_src_for_ssa(tex_pos);
	tex->src[1].src_type = nir_tex_src_lod;
	tex->src[1].src = nir_src_for_ssa(nir_imm_int(b, 0));
	tex->dest_type = nir_type_uint;
	tex->is_array = false;
	tex->coord_components = 2;
	tex->texture = nir_deref_var_create(tex, sampler);
	tex->sampler = NULL;

	nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
	nir_builder_instr_insert(b, &tex->instr);

	return &tex->dest.ssa;
}


static nir_ssa_def *
build_nir_buffer_fetch(struct nir_builder *b, struct radv_device *device,
                      nir_ssa_def *tex_pos)
{
	const struct glsl_type *sampler_type =
		glsl_sampler_type(GLSL_SAMPLER_DIM_BUF, false, false, GLSL_TYPE_UINT);
	nir_variable *sampler = nir_variable_create(b->shader, nir_var_uniform,
						    sampler_type, "s_tex");
	sampler->data.descriptor_set = 0;
	sampler->data.binding = 0;

	nir_intrinsic_instr *width = nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_push_constant);
	width->src[0] = nir_src_for_ssa(nir_imm_int(b, 0));
	width->num_components = 1;
	nir_ssa_dest_init(&width->instr, &width->dest, 1, 32, "width");
	nir_builder_instr_insert(b, &width->instr);

	nir_ssa_def *pos_x = nir_channel(b, tex_pos, 0);
	nir_ssa_def *pos_y = nir_channel(b, tex_pos, 1);
	pos_y = nir_imul(b, pos_y, &width->dest.ssa);
	pos_x = nir_iadd(b, pos_x, pos_y);
	//pos_x = nir_iadd(b, pos_x, nir_imm_int(b, 100000));

	nir_tex_instr *tex = nir_tex_instr_create(b->shader, 1);
	tex->sampler_dim = GLSL_SAMPLER_DIM_BUF;
	tex->op = nir_texop_txf;
	tex->src[0].src_type = nir_tex_src_coord;
	tex->src[0].src = nir_src_for_ssa(pos_x);
	tex->dest_type = nir_type_uint;
	tex->is_array = false;
	tex->coord_components = 1;
	tex->texture = nir_deref_var_create(tex, sampler);
	tex->sampler = NULL;

	nir_ssa_dest_init(&tex->instr, &tex->dest, 4, 32, "tex");
	nir_builder_instr_insert(b, &tex->instr);

	return &tex->dest.ssa;
}

static const VkPipelineVertexInputStateCreateInfo normal_vi_create_info = {
	.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
	.vertexBindingDescriptionCount = 1,
	.pVertexBindingDescriptions = (VkVertexInputBindingDescription[]) {
		{
			.binding = 0,
			.stride = 4 * sizeof(float),
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
			.offset = 0
		},
		{
			/* Texture Coordinate */
			.location = 1,
			.binding = 0,
			.format = VK_FORMAT_R32G32_SFLOAT,
			.offset = 8
		},
	},
};

static nir_shader *
build_nir_copy_fragment_shader(struct radv_device *device,
                               texel_fetch_build_func txf_func, const char* name)
{
	const struct glsl_type *vec4 = glsl_vec4_type();
	const struct glsl_type *vec2 = glsl_vector_type(GLSL_TYPE_FLOAT, 2);
	nir_builder b;

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_FRAGMENT, NULL);
	b.shader->info->name = ralloc_strdup(b.shader, name);

	nir_variable *tex_pos_in = nir_variable_create(b.shader, nir_var_shader_in,
						       vec2, "v_tex_pos");
	tex_pos_in->data.location = VARYING_SLOT_VAR0;

	nir_variable *color_out = nir_variable_create(b.shader, nir_var_shader_out,
						      vec4, "f_color");
	color_out->data.location = FRAG_RESULT_DATA0;

	nir_ssa_def *pos_int = nir_f2i(&b, nir_load_var(&b, tex_pos_in));
	unsigned swiz[4] = { 0, 1 };
	nir_ssa_def *tex_pos = nir_swizzle(&b, pos_int, swiz, 2, false);

	nir_ssa_def *color = txf_func(&b, device, tex_pos);
	nir_store_var(&b, color_out, color, 0xf);

	return b.shader;
}

static nir_shader *
build_nir_copy_fragment_shader_depth(struct radv_device *device,
				     texel_fetch_build_func txf_func, const char* name)
{
	const struct glsl_type *vec4 = glsl_vec4_type();
	const struct glsl_type *vec2 = glsl_vector_type(GLSL_TYPE_FLOAT, 2);
	nir_builder b;

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_FRAGMENT, NULL);
	b.shader->info->name = ralloc_strdup(b.shader, name);

	nir_variable *tex_pos_in = nir_variable_create(b.shader, nir_var_shader_in,
						       vec2, "v_tex_pos");
	tex_pos_in->data.location = VARYING_SLOT_VAR0;

	nir_variable *color_out = nir_variable_create(b.shader, nir_var_shader_out,
						      vec4, "f_color");
	color_out->data.location = FRAG_RESULT_DEPTH;

	nir_ssa_def *pos_int = nir_f2i(&b, nir_load_var(&b, tex_pos_in));
	unsigned swiz[4] = { 0, 1 };
	nir_ssa_def *tex_pos = nir_swizzle(&b, pos_int, swiz, 2, false);

	nir_ssa_def *color = txf_func(&b, device, tex_pos);
	nir_store_var(&b, color_out, color, 0x1);

	return b.shader;
}

static nir_shader *
build_nir_copy_fragment_shader_stencil(struct radv_device *device,
				       texel_fetch_build_func txf_func, const char* name)
{
	const struct glsl_type *vec4 = glsl_vec4_type();
	const struct glsl_type *vec2 = glsl_vector_type(GLSL_TYPE_FLOAT, 2);
	nir_builder b;

	nir_builder_init_simple_shader(&b, NULL, MESA_SHADER_FRAGMENT, NULL);
	b.shader->info->name = ralloc_strdup(b.shader, name);

	nir_variable *tex_pos_in = nir_variable_create(b.shader, nir_var_shader_in,
						       vec2, "v_tex_pos");
	tex_pos_in->data.location = VARYING_SLOT_VAR0;

	nir_variable *color_out = nir_variable_create(b.shader, nir_var_shader_out,
						      vec4, "f_color");
	color_out->data.location = FRAG_RESULT_STENCIL;

	nir_ssa_def *pos_int = nir_f2i(&b, nir_load_var(&b, tex_pos_in));
	unsigned swiz[4] = { 0, 1 };
	nir_ssa_def *tex_pos = nir_swizzle(&b, pos_int, swiz, 2, false);

	nir_ssa_def *color = txf_func(&b, device, tex_pos);
	nir_store_var(&b, color_out, color, 0x1);

	return b.shader;
}

void
radv_device_finish_meta_blit2d_state(struct radv_device *device)
{
	for(unsigned j = 0; j < NUM_META_FS_KEYS; ++j) {
		if (device->meta_state.blit2d.render_passes[j]) {
			radv_DestroyRenderPass(radv_device_to_handle(device),
					       device->meta_state.blit2d.render_passes[j],
					       &device->meta_state.alloc);
		}
	}

	radv_DestroyRenderPass(radv_device_to_handle(device),
			       device->meta_state.blit2d.depth_only_rp,
			       &device->meta_state.alloc);
	radv_DestroyRenderPass(radv_device_to_handle(device),
			       device->meta_state.blit2d.stencil_only_rp,
			       &device->meta_state.alloc);

	for (unsigned src = 0; src < BLIT2D_NUM_SRC_TYPES; src++) {
		if (device->meta_state.blit2d.p_layouts[src]) {
			radv_DestroyPipelineLayout(radv_device_to_handle(device),
						device->meta_state.blit2d.p_layouts[src],
						&device->meta_state.alloc);
		}

		if (device->meta_state.blit2d.ds_layouts[src]) {
			radv_DestroyDescriptorSetLayout(radv_device_to_handle(device),
							device->meta_state.blit2d.ds_layouts[src],
							&device->meta_state.alloc);
		}

		for (unsigned j = 0; j < NUM_META_FS_KEYS; ++j) {
			if (device->meta_state.blit2d.pipelines[src][j]) {
				radv_DestroyPipeline(radv_device_to_handle(device),
						     device->meta_state.blit2d.pipelines[src][j],
						     &device->meta_state.alloc);
			}
		}

		radv_DestroyPipeline(radv_device_to_handle(device),
				     device->meta_state.blit2d.depth_only_pipeline[src],
				     &device->meta_state.alloc);
		radv_DestroyPipeline(radv_device_to_handle(device),
				     device->meta_state.blit2d.stencil_only_pipeline[src],
				     &device->meta_state.alloc);
	}
}

static VkResult
blit2d_init_color_pipeline(struct radv_device *device,
			   enum blit2d_src_type src_type,
			   VkFormat format)
{
	VkResult result;
	unsigned fs_key = radv_format_meta_fs_key(format);
	const char *name;

	texel_fetch_build_func src_func;
	switch(src_type) {
	case BLIT2D_SRC_TYPE_IMAGE:
		src_func = build_nir_texel_fetch;
		name = "meta_blit2d_image_fs";
		break;
	case BLIT2D_SRC_TYPE_BUFFER:
		src_func = build_nir_buffer_fetch;
		name = "meta_blit2d_buffer_fs";
		break;
	default:
		unreachable("unknown blit src type\n");
		break;
	}

	const VkPipelineVertexInputStateCreateInfo *vi_create_info;
	struct radv_shader_module fs = { .nir = NULL };


	fs.nir = build_nir_copy_fragment_shader(device, src_func, name);
	vi_create_info = &normal_vi_create_info;

	struct radv_shader_module vs = {
		.nir = build_nir_vertex_shader(),
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

	if (!device->meta_state.blit2d.render_passes[fs_key]) {
		result = radv_CreateRenderPass(radv_device_to_handle(device),
					       &(VkRenderPassCreateInfo) {
						       .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
						       .attachmentCount = 1,
						       .pAttachments = &(VkAttachmentDescription) {
						       .format = format,
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
				 }, &device->meta_state.alloc, &device->meta_state.blit2d.render_passes[fs_key]);
	}

	const VkGraphicsPipelineCreateInfo vk_pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = ARRAY_SIZE(pipeline_shader_stages),
		.pStages = pipeline_shader_stages,
		.pVertexInputState = vi_create_info,
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
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE
		},
		.pMultisampleState = &(VkPipelineMultisampleStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = 1,
			.sampleShadingEnable = false,
			.pSampleMask = (VkSampleMask[]) { UINT32_MAX },
		},
		.pColorBlendState = &(VkPipelineColorBlendStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.attachmentCount = 1,
			.pAttachments = (VkPipelineColorBlendAttachmentState []) {
				{ .colorWriteMask =
				  VK_COLOR_COMPONENT_A_BIT |
				  VK_COLOR_COMPONENT_R_BIT |
				  VK_COLOR_COMPONENT_G_BIT |
				  VK_COLOR_COMPONENT_B_BIT },
			}
		},
		.pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = 7,
			.pDynamicStates = (VkDynamicState[]) {
				VK_DYNAMIC_STATE_LINE_WIDTH,
				VK_DYNAMIC_STATE_DEPTH_BIAS,
				VK_DYNAMIC_STATE_BLEND_CONSTANTS,
				VK_DYNAMIC_STATE_DEPTH_BOUNDS,
				VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
				VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
				VK_DYNAMIC_STATE_STENCIL_REFERENCE,
			},
		},
		.flags = 0,
		.layout = device->meta_state.blit2d.p_layouts[src_type],
		.renderPass = device->meta_state.blit2d.render_passes[fs_key],
		.subpass = 0,
	};

	const struct radv_graphics_pipeline_create_info radv_pipeline_info = {
		.use_rectlist = true
	};

	result = radv_graphics_pipeline_create(radv_device_to_handle(device),
					       radv_pipeline_cache_to_handle(&device->meta_state.cache),
					       &vk_pipeline_info, &radv_pipeline_info,
					       &device->meta_state.alloc,
					       &device->meta_state.blit2d.pipelines[src_type][fs_key]);


	ralloc_free(vs.nir);
	ralloc_free(fs.nir);

	return result;
}

static VkResult
blit2d_init_depth_only_pipeline(struct radv_device *device,
				enum blit2d_src_type src_type)
{
	VkResult result;
	const char *name;

	texel_fetch_build_func src_func;
	switch(src_type) {
	case BLIT2D_SRC_TYPE_IMAGE:
		src_func = build_nir_texel_fetch;
		name = "meta_blit2d_depth_image_fs";
		break;
	case BLIT2D_SRC_TYPE_BUFFER:
		src_func = build_nir_buffer_fetch;
		name = "meta_blit2d_depth_buffer_fs";
		break;
	default:
		unreachable("unknown blit src type\n");
		break;
	}

	const VkPipelineVertexInputStateCreateInfo *vi_create_info;
	struct radv_shader_module fs = { .nir = NULL };

	fs.nir = build_nir_copy_fragment_shader_depth(device, src_func, name);
	vi_create_info = &normal_vi_create_info;

	struct radv_shader_module vs = {
		.nir = build_nir_vertex_shader(),
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

	if (!device->meta_state.blit2d.depth_only_rp) {
		result = radv_CreateRenderPass(radv_device_to_handle(device),
					       &(VkRenderPassCreateInfo) {
						       .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
							       .attachmentCount = 1,
							       .pAttachments = &(VkAttachmentDescription) {
							       .format = 0,
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
						 }, &device->meta_state.alloc, &device->meta_state.blit2d.depth_only_rp);
	}

	const VkGraphicsPipelineCreateInfo vk_pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = ARRAY_SIZE(pipeline_shader_stages),
		.pStages = pipeline_shader_stages,
		.pVertexInputState = vi_create_info,
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
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE
		},
		.pMultisampleState = &(VkPipelineMultisampleStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = 1,
			.sampleShadingEnable = false,
			.pSampleMask = (VkSampleMask[]) { UINT32_MAX },
		},
		.pColorBlendState = &(VkPipelineColorBlendStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.attachmentCount = 0,
			.pAttachments = NULL,
		},
		.pDepthStencilState = &(VkPipelineDepthStencilStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
			.depthTestEnable = true,
			.depthWriteEnable = true,
			.depthCompareOp = VK_COMPARE_OP_ALWAYS,
		},
		.pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = 7,
			.pDynamicStates = (VkDynamicState[]) {
				VK_DYNAMIC_STATE_LINE_WIDTH,
				VK_DYNAMIC_STATE_DEPTH_BIAS,
				VK_DYNAMIC_STATE_BLEND_CONSTANTS,
				VK_DYNAMIC_STATE_DEPTH_BOUNDS,
				VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK,
				VK_DYNAMIC_STATE_STENCIL_WRITE_MASK,
				VK_DYNAMIC_STATE_STENCIL_REFERENCE,
			},
		},
		.flags = 0,
		.layout = device->meta_state.blit2d.p_layouts[src_type],
		.renderPass = device->meta_state.blit2d.depth_only_rp,
		.subpass = 0,
	};

	const struct radv_graphics_pipeline_create_info radv_pipeline_info = {
		.use_rectlist = true
	};

	result = radv_graphics_pipeline_create(radv_device_to_handle(device),
					       radv_pipeline_cache_to_handle(&device->meta_state.cache),
					       &vk_pipeline_info, &radv_pipeline_info,
					       &device->meta_state.alloc,
					       &device->meta_state.blit2d.depth_only_pipeline[src_type]);


	ralloc_free(vs.nir);
	ralloc_free(fs.nir);

	return result;
}

static VkResult
blit2d_init_stencil_only_pipeline(struct radv_device *device,
				  enum blit2d_src_type src_type)
{
	VkResult result;
	const char *name;

	texel_fetch_build_func src_func;
	switch(src_type) {
	case BLIT2D_SRC_TYPE_IMAGE:
		src_func = build_nir_texel_fetch;
		name = "meta_blit2d_stencil_image_fs";
		break;
	case BLIT2D_SRC_TYPE_BUFFER:
		src_func = build_nir_buffer_fetch;
		name = "meta_blit2d_stencil_buffer_fs";
		break;
	default:
		unreachable("unknown blit src type\n");
		break;
	}

	const VkPipelineVertexInputStateCreateInfo *vi_create_info;
	struct radv_shader_module fs = { .nir = NULL };

	fs.nir = build_nir_copy_fragment_shader_stencil(device, src_func, name);
	vi_create_info = &normal_vi_create_info;

	struct radv_shader_module vs = {
		.nir = build_nir_vertex_shader(),
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

	if (!device->meta_state.blit2d.stencil_only_rp) {
		result = radv_CreateRenderPass(radv_device_to_handle(device),
					       &(VkRenderPassCreateInfo) {
						       .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
							       .attachmentCount = 1,
							       .pAttachments = &(VkAttachmentDescription) {
							       .format = 0,
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
						 }, &device->meta_state.alloc, &device->meta_state.blit2d.stencil_only_rp);
	}

	const VkGraphicsPipelineCreateInfo vk_pipeline_info = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = ARRAY_SIZE(pipeline_shader_stages),
		.pStages = pipeline_shader_stages,
		.pVertexInputState = vi_create_info,
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
			.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE
		},
		.pMultisampleState = &(VkPipelineMultisampleStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
			.rasterizationSamples = 1,
			.sampleShadingEnable = false,
			.pSampleMask = (VkSampleMask[]) { UINT32_MAX },
		},
		.pColorBlendState = &(VkPipelineColorBlendStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
			.attachmentCount = 0,
			.pAttachments = NULL,
		},
		.pDepthStencilState = &(VkPipelineDepthStencilStateCreateInfo) {
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
		},
		.pDynamicState = &(VkPipelineDynamicStateCreateInfo) {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
			.dynamicStateCount = 4,
			.pDynamicStates = (VkDynamicState[]) {
				VK_DYNAMIC_STATE_LINE_WIDTH,
				VK_DYNAMIC_STATE_DEPTH_BIAS,
				VK_DYNAMIC_STATE_BLEND_CONSTANTS,
				VK_DYNAMIC_STATE_DEPTH_BOUNDS,
			},
		},
		.flags = 0,
		.layout = device->meta_state.blit2d.p_layouts[src_type],
		.renderPass = device->meta_state.blit2d.stencil_only_rp,
		.subpass = 0,
	};

	const struct radv_graphics_pipeline_create_info radv_pipeline_info = {
		.use_rectlist = true
	};

	result = radv_graphics_pipeline_create(radv_device_to_handle(device),
					       radv_pipeline_cache_to_handle(&device->meta_state.cache),
					       &vk_pipeline_info, &radv_pipeline_info,
					       &device->meta_state.alloc,
					       &device->meta_state.blit2d.stencil_only_pipeline[src_type]);


	ralloc_free(vs.nir);
	ralloc_free(fs.nir);

	return result;
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
radv_device_init_meta_blit2d_state(struct radv_device *device)
{
	VkResult result;

	zero(device->meta_state.blit2d);

	result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device),
						&(VkDescriptorSetLayoutCreateInfo) {
							.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
								.bindingCount = 1,
								.pBindings = (VkDescriptorSetLayoutBinding[]) {
								{
									.binding = 0,
									.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
									.descriptorCount = 1,
									.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
									.pImmutableSamplers = NULL
								},
							}
						}, &device->meta_state.alloc, &device->meta_state.blit2d.ds_layouts[BLIT2D_SRC_TYPE_IMAGE]);
	if (result != VK_SUCCESS)
		goto fail;

	result = radv_CreatePipelineLayout(radv_device_to_handle(device),
					   &(VkPipelineLayoutCreateInfo) {
						   .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
							   .setLayoutCount = 1,
							   .pSetLayouts = &device->meta_state.blit2d.ds_layouts[BLIT2D_SRC_TYPE_IMAGE],
							   },
					   &device->meta_state.alloc, &device->meta_state.blit2d.p_layouts[BLIT2D_SRC_TYPE_IMAGE]);
	if (result != VK_SUCCESS)
		goto fail;

	result = radv_CreateDescriptorSetLayout(radv_device_to_handle(device),
						&(VkDescriptorSetLayoutCreateInfo) {
							.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
								.bindingCount = 1,
								.pBindings = (VkDescriptorSetLayoutBinding[]) {
								{
									.binding = 0,
									.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,
									.descriptorCount = 1,
									.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
									.pImmutableSamplers = NULL
								},
							}
						}, &device->meta_state.alloc, &device->meta_state.blit2d.ds_layouts[BLIT2D_SRC_TYPE_BUFFER]);
	if (result != VK_SUCCESS)
		goto fail;

	const VkPushConstantRange push_constant_range = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4};
	result = radv_CreatePipelineLayout(radv_device_to_handle(device),
					   &(VkPipelineLayoutCreateInfo) {
						   .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
						   .setLayoutCount = 1,
						   .pSetLayouts = &device->meta_state.blit2d.ds_layouts[BLIT2D_SRC_TYPE_BUFFER],
						   .pushConstantRangeCount = 1,
						   .pPushConstantRanges = &push_constant_range,
					   },
					   &device->meta_state.alloc, &device->meta_state.blit2d.p_layouts[BLIT2D_SRC_TYPE_BUFFER]);
	if (result != VK_SUCCESS)
		goto fail;

	for (unsigned src = 0; src < BLIT2D_NUM_SRC_TYPES; src++) {
		for (unsigned j = 0; j < ARRAY_SIZE(pipeline_formats); ++j) {
			result = blit2d_init_color_pipeline(device, src, pipeline_formats[j]);
			if (result != VK_SUCCESS)
				goto fail;
		}

		result = blit2d_init_depth_only_pipeline(device, src);
		if (result != VK_SUCCESS)
			goto fail;

		result = blit2d_init_stencil_only_pipeline(device, src);
		if (result != VK_SUCCESS)
			goto fail;
	}

	return VK_SUCCESS;

fail:
	radv_device_finish_meta_blit2d_state(device);
	return result;
}
