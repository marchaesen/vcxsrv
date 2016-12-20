/*
 * Copyright © 2016 Red Hat
 * based on intel anv code:
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

#ifndef RADV_META_H
#define RADV_META_H

#include "radv_private.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RADV_META_VERTEX_BINDING_COUNT 2

struct radv_meta_saved_state {
	struct radv_vertex_binding old_vertex_bindings[RADV_META_VERTEX_BINDING_COUNT];
	struct radv_descriptor_set *old_descriptor_set0;
	struct radv_pipeline *old_pipeline;

	/**
	 * Bitmask of (1 << VK_DYNAMIC_STATE_*). Defines the set of saved dynamic
	 * state.
	 */
	uint32_t dynamic_mask;
	struct radv_dynamic_state dynamic;

	char push_constants[128];
};

struct radv_meta_saved_pass_state {
	struct radv_render_pass *pass;
	const struct radv_subpass *subpass;
	struct radv_attachment_state *attachments;
	struct radv_framebuffer *framebuffer;
	VkRect2D render_area;
};

struct radv_meta_saved_compute_state {
	struct radv_descriptor_set *old_descriptor_set0;
	struct radv_pipeline *old_pipeline;

	char push_constants[128];
};

VkResult radv_device_init_meta_clear_state(struct radv_device *device);
void radv_device_finish_meta_clear_state(struct radv_device *device);

VkResult radv_device_init_meta_resolve_state(struct radv_device *device);
void radv_device_finish_meta_resolve_state(struct radv_device *device);

VkResult radv_device_init_meta_depth_decomp_state(struct radv_device *device);
void radv_device_finish_meta_depth_decomp_state(struct radv_device *device);

VkResult radv_device_init_meta_fast_clear_flush_state(struct radv_device *device);
void radv_device_finish_meta_fast_clear_flush_state(struct radv_device *device);

VkResult radv_device_init_meta_blit_state(struct radv_device *device);
void radv_device_finish_meta_blit_state(struct radv_device *device);

VkResult radv_device_init_meta_blit2d_state(struct radv_device *device);
void radv_device_finish_meta_blit2d_state(struct radv_device *device);

VkResult radv_device_init_meta_buffer_state(struct radv_device *device);
void radv_device_finish_meta_buffer_state(struct radv_device *device);

VkResult radv_device_init_meta_resolve_compute_state(struct radv_device *device);
void radv_device_finish_meta_resolve_compute_state(struct radv_device *device);
void radv_meta_save(struct radv_meta_saved_state *state,
		    const struct radv_cmd_buffer *cmd_buffer,
		    uint32_t dynamic_mask);

void radv_meta_restore(const struct radv_meta_saved_state *state,
		       struct radv_cmd_buffer *cmd_buffer);

void radv_meta_save_pass(struct radv_meta_saved_pass_state *state,
			 const struct radv_cmd_buffer *cmd_buffer);

void radv_meta_restore_pass(const struct radv_meta_saved_pass_state *state,
			    struct radv_cmd_buffer *cmd_buffer);

void radv_meta_save_compute(struct radv_meta_saved_compute_state *state,
			    const struct radv_cmd_buffer *cmd_buffer,
			    unsigned push_constant_size);

void radv_meta_restore_compute(const struct radv_meta_saved_compute_state *state,
			       struct radv_cmd_buffer *cmd_buffer,
			       unsigned push_constant_size);

VkImageViewType radv_meta_get_view_type(const struct radv_image *image);

uint32_t radv_meta_get_iview_layer(const struct radv_image *dest_image,
				   const VkImageSubresourceLayers *dest_subresource,
				   const VkOffset3D *dest_offset);

struct radv_meta_blit2d_surf {
	/** The size of an element in bytes. */
	uint8_t bs;
	VkFormat format;

	struct radv_image *image;
	unsigned level;
	unsigned layer;
	VkImageAspectFlags aspect_mask;
};

struct radv_meta_blit2d_buffer {
	struct radv_buffer *buffer;
	uint32_t offset;
	uint32_t pitch;
	uint8_t bs;
	VkFormat format;
};

struct radv_meta_blit2d_rect {
	uint32_t src_x, src_y;
	uint32_t dst_x, dst_y;
	uint32_t width, height;
};

void radv_meta_begin_blit2d(struct radv_cmd_buffer *cmd_buffer,
			    struct radv_meta_saved_state *save);

void radv_meta_blit2d(struct radv_cmd_buffer *cmd_buffer,
		      struct radv_meta_blit2d_surf *src_img,
		      struct radv_meta_blit2d_buffer *src_buf,
		      struct radv_meta_blit2d_surf *dst,
		      unsigned num_rects,
		      struct radv_meta_blit2d_rect *rects);

void radv_meta_end_blit2d(struct radv_cmd_buffer *cmd_buffer,
			  struct radv_meta_saved_state *save);


VkResult radv_device_init_meta_bufimage_state(struct radv_device *device);
void radv_device_finish_meta_bufimage_state(struct radv_device *device);
void radv_meta_begin_bufimage(struct radv_cmd_buffer *cmd_buffer,
			      struct radv_meta_saved_compute_state *save);
void radv_meta_end_bufimage(struct radv_cmd_buffer *cmd_buffer,
			    struct radv_meta_saved_compute_state *save);
void radv_meta_begin_itoi(struct radv_cmd_buffer *cmd_buffer,
			  struct radv_meta_saved_compute_state *save);
void radv_meta_end_itoi(struct radv_cmd_buffer *cmd_buffer,
			struct radv_meta_saved_compute_state *save);
void radv_meta_begin_cleari(struct radv_cmd_buffer *cmd_buffer,
			    struct radv_meta_saved_compute_state *save);
void radv_meta_end_cleari(struct radv_cmd_buffer *cmd_buffer,
			  struct radv_meta_saved_compute_state *save);
void radv_meta_image_to_buffer(struct radv_cmd_buffer *cmd_buffer,
			       struct radv_meta_blit2d_surf *src,
			       struct radv_meta_blit2d_buffer *dst,
			       unsigned num_rects,
			       struct radv_meta_blit2d_rect *rects);

void radv_meta_buffer_to_image_cs(struct radv_cmd_buffer *cmd_buffer,
				  struct radv_meta_blit2d_buffer *src,
				  struct radv_meta_blit2d_surf *dst,
				  unsigned num_rects,
				  struct radv_meta_blit2d_rect *rects);
void radv_meta_image_to_image_cs(struct radv_cmd_buffer *cmd_buffer,
				 struct radv_meta_blit2d_surf *src,
				 struct radv_meta_blit2d_surf *dst,
				 unsigned num_rects,
				 struct radv_meta_blit2d_rect *rects);
void radv_meta_clear_image_cs(struct radv_cmd_buffer *cmd_buffer,
			      struct radv_meta_blit2d_surf *dst,
			      const VkClearColorValue *clear_color);

void radv_decompress_depth_image_inplace(struct radv_cmd_buffer *cmd_buffer,
					 struct radv_image *image,
					 VkImageSubresourceRange *subresourceRange);
void radv_resummarize_depth_image_inplace(struct radv_cmd_buffer *cmd_buffer,
					  struct radv_image *image,
					  VkImageSubresourceRange *subresourceRange);
void radv_fast_clear_flush_image_inplace(struct radv_cmd_buffer *cmd_buffer,
					 struct radv_image *image);

void radv_meta_save_graphics_reset_vport_scissor(struct radv_meta_saved_state *saved_state,
						 struct radv_cmd_buffer *cmd_buffer);

void radv_meta_resolve_compute_image(struct radv_cmd_buffer *cmd_buffer,
				     struct radv_image *src_image,
				     VkImageLayout src_image_layout,
				     struct radv_image *dest_image,
				     VkImageLayout dest_image_layout,
				     uint32_t region_count,
				     const VkImageResolve *regions);

#ifdef __cplusplus
}
#endif

#endif /* RADV_META_H */
