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
#include "radv_shader.h"

#ifdef __cplusplus
extern "C" {
#endif

enum radv_meta_save_flags {
	RADV_META_SAVE_PASS		 = (1 << 0),
	RADV_META_SAVE_CONSTANTS         = (1 << 1),
	RADV_META_SAVE_DESCRIPTORS       = (1 << 2),
	RADV_META_SAVE_GRAPHICS_PIPELINE = (1 << 3),
	RADV_META_SAVE_COMPUTE_PIPELINE  = (1 << 4),
	RADV_META_SAVE_SAMPLE_LOCATIONS  = (1 << 5),
};

struct radv_meta_saved_state {
	uint32_t flags;

	struct radv_descriptor_set *old_descriptor_set0;
	struct radv_pipeline *old_pipeline;
	struct radv_viewport_state viewport;
	struct radv_scissor_state scissor;
	struct radv_sample_locations_state sample_location;

	char push_constants[128];

	struct radv_render_pass *pass;
	const struct radv_subpass *subpass;
	struct radv_attachment_state *attachments;
	struct radv_framebuffer *framebuffer;
	VkRect2D render_area;

	VkCullModeFlags cull_mode;
	VkFrontFace front_face;

	unsigned primitive_topology;

	bool depth_test_enable;
	bool depth_write_enable;
	unsigned depth_compare_op;
	bool depth_bounds_test_enable;
	bool stencil_test_enable;

	struct {
		struct {
			VkStencilOp fail_op;
			VkStencilOp pass_op;
			VkStencilOp depth_fail_op;
			VkCompareOp compare_op;
		} front;

		struct {
			VkStencilOp fail_op;
			VkStencilOp pass_op;
			VkStencilOp depth_fail_op;
			VkCompareOp compare_op;
		} back;
	} stencil_op;
};

VkResult radv_device_init_meta_clear_state(struct radv_device *device, bool on_demand);
void radv_device_finish_meta_clear_state(struct radv_device *device);

VkResult radv_device_init_meta_resolve_state(struct radv_device *device, bool on_demand);
void radv_device_finish_meta_resolve_state(struct radv_device *device);

VkResult radv_device_init_meta_depth_decomp_state(struct radv_device *device, bool on_demand);
void radv_device_finish_meta_depth_decomp_state(struct radv_device *device);

VkResult radv_device_init_meta_fast_clear_flush_state(struct radv_device *device, bool on_demand);
void radv_device_finish_meta_fast_clear_flush_state(struct radv_device *device);

VkResult radv_device_init_meta_blit_state(struct radv_device *device, bool on_demand);
void radv_device_finish_meta_blit_state(struct radv_device *device);

VkResult radv_device_init_meta_blit2d_state(struct radv_device *device, bool on_demand);
void radv_device_finish_meta_blit2d_state(struct radv_device *device);

VkResult radv_device_init_meta_buffer_state(struct radv_device *device);
void radv_device_finish_meta_buffer_state(struct radv_device *device);

VkResult radv_device_init_meta_query_state(struct radv_device *device, bool on_demand);
void radv_device_finish_meta_query_state(struct radv_device *device);

VkResult radv_device_init_meta_resolve_compute_state(struct radv_device *device, bool on_demand);
void radv_device_finish_meta_resolve_compute_state(struct radv_device *device);

VkResult radv_device_init_meta_resolve_fragment_state(struct radv_device *device, bool on_demand);
void radv_device_finish_meta_resolve_fragment_state(struct radv_device *device);

VkResult radv_device_init_meta_fmask_expand_state(struct radv_device *device);
void radv_device_finish_meta_fmask_expand_state(struct radv_device *device);

void radv_meta_save(struct radv_meta_saved_state *saved_state,
		    struct radv_cmd_buffer *cmd_buffer, uint32_t flags);

void radv_meta_restore(const struct radv_meta_saved_state *state,
		       struct radv_cmd_buffer *cmd_buffer);

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
	VkImageLayout current_layout;
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

void radv_decompress_depth_stencil(struct radv_cmd_buffer *cmd_buffer,
				   struct radv_image *image,
				   const VkImageSubresourceRange *subresourceRange,
				   struct radv_sample_locations_state *sample_locs);
void radv_resummarize_depth_stencil(struct radv_cmd_buffer *cmd_buffer,
				    struct radv_image *image,
				    const VkImageSubresourceRange *subresourceRange,
				    struct radv_sample_locations_state *sample_locs);
void radv_fast_clear_flush_image_inplace(struct radv_cmd_buffer *cmd_buffer,
					 struct radv_image *image,
					 const VkImageSubresourceRange *subresourceRange);
void radv_decompress_dcc(struct radv_cmd_buffer *cmd_buffer,
			struct radv_image *image,
                        const VkImageSubresourceRange *subresourceRange);
void radv_expand_fmask_image_inplace(struct radv_cmd_buffer *cmd_buffer,
				     struct radv_image *image,
				     const VkImageSubresourceRange *subresourceRange);

void radv_meta_resolve_compute_image(struct radv_cmd_buffer *cmd_buffer,
				     struct radv_image *src_image,
				     VkFormat src_format,
				     VkImageLayout src_image_layout,
				     struct radv_image *dest_image,
				     VkFormat dest_format,
				     VkImageLayout dest_image_layout,
				     const VkImageResolve2KHR *region);

void radv_meta_resolve_fragment_image(struct radv_cmd_buffer *cmd_buffer,
				      struct radv_image *src_image,
				      VkImageLayout src_image_layout,
				      struct radv_image *dest_image,
				      VkImageLayout dest_image_layout,
				     const VkImageResolve2KHR *region);

void radv_decompress_resolve_subpass_src(struct radv_cmd_buffer *cmd_buffer);

void radv_decompress_resolve_src(struct radv_cmd_buffer *cmd_buffer,
				 struct radv_image *src_image,
				 VkImageLayout src_image_layout,
				 const VkImageResolve2KHR *region);

uint32_t radv_clear_cmask(struct radv_cmd_buffer *cmd_buffer,
			  struct radv_image *image,
			  const VkImageSubresourceRange *range, uint32_t value);
uint32_t radv_clear_fmask(struct radv_cmd_buffer *cmd_buffer,
			  struct radv_image *image,
			  const VkImageSubresourceRange *range, uint32_t value);
uint32_t radv_clear_dcc(struct radv_cmd_buffer *cmd_buffer,
			struct radv_image *image,
			const VkImageSubresourceRange *range, uint32_t value);
uint32_t radv_clear_htile(struct radv_cmd_buffer *cmd_buffer,
			  const struct radv_image *image,
			  const VkImageSubresourceRange *range, uint32_t value);

/**
 * Return whether the bound pipeline is the FMASK decompress pass.
 */
static inline bool
radv_is_fmask_decompress_pipeline(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_meta_state *meta_state = &cmd_buffer->device->meta_state;
	struct radv_pipeline *pipeline = cmd_buffer->state.pipeline;

	return radv_pipeline_to_handle(pipeline) ==
	       meta_state->fast_clear_flush.fmask_decompress_pipeline;
}

/**
 * Return whether the bound pipeline is the DCC decompress pass.
 */
static inline bool
radv_is_dcc_decompress_pipeline(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_meta_state *meta_state = &cmd_buffer->device->meta_state;
	struct radv_pipeline *pipeline = cmd_buffer->state.pipeline;

	return radv_pipeline_to_handle(pipeline) ==
	       meta_state->fast_clear_flush.dcc_decompress_pipeline;
}

/**
 * Return whether the bound pipeline is the hardware resolve path.
 */
static inline bool
radv_is_hw_resolve_pipeline(struct radv_cmd_buffer *cmd_buffer)
{
	struct radv_meta_state *meta_state = &cmd_buffer->device->meta_state;
	struct radv_pipeline *pipeline = cmd_buffer->state.pipeline;

	if (!pipeline)
		return false;

	for (uint32_t i = 0; i < NUM_META_FS_KEYS; ++i) {
		if (radv_pipeline_to_handle(pipeline) == meta_state->resolve.pipeline[i])
			return true;
	}
	return false;
}

/* common nir builder helpers */
#include "nir/nir_builder.h"

nir_ssa_def *radv_meta_gen_rect_vertices(nir_builder *vs_b);
nir_ssa_def *radv_meta_gen_rect_vertices_comp2(nir_builder *vs_b, nir_ssa_def *comp2);
nir_shader *radv_meta_build_nir_vs_generate_vertices(void);
nir_shader *radv_meta_build_nir_fs_noop(void);

void radv_meta_build_resolve_shader_core(nir_builder *b,
					 bool is_integer,
					 int samples,
					 nir_variable *input_img,
					 nir_variable *color,
					 nir_ssa_def *img_coord);

nir_ssa_def *radv_meta_load_descriptor(nir_builder *b, unsigned desc_set,
				       unsigned binding);

#ifdef __cplusplus
}
#endif

#endif /* RADV_META_H */
