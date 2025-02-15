/*
 * Copyright © Microsoft Corporation
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */


#ifndef D3D12_VIDEO_BUFFER_H
#define D3D12_VIDEO_BUFFER_H

#include "pipe/p_context.h"
#include "pipe/p_video_codec.h"
#include <vector>
#include "d3d12_video_types.h"

///
/// Pipe video buffer interface starts
///

/**
 * creates a video buffer
 */
struct pipe_video_buffer *
d3d12_video_buffer_create(struct pipe_context *pipe, const struct pipe_video_buffer *tmpl);

/**
 * creates a video buffer from a handle
 */
struct pipe_video_buffer *
d3d12_video_buffer_from_handle( struct pipe_context *pipe,
                           const struct pipe_video_buffer *tmpl,
                           struct winsys_handle *handle,
                           unsigned usage);
/**
 * destroy this video buffer
 */
void
d3d12_video_buffer_destroy(struct pipe_video_buffer *buffer);

/**
 * get an individual resource for each plane,
 * only returns existing resources by reference
 */
void
d3d12_video_buffer_resources(struct pipe_video_buffer *buffer,
                             struct pipe_resource **resources);

/**
 * get an individual sampler view for each plane
 */
struct pipe_sampler_view **
d3d12_video_buffer_get_sampler_view_planes(struct pipe_video_buffer *buffer);

/**
 * get an individual sampler view for each component
 */
struct pipe_sampler_view **
d3d12_video_buffer_get_sampler_view_components(struct pipe_video_buffer *buffer);

/**
 * get an individual surfaces for each plane
 */
struct pipe_surface **
d3d12_video_buffer_get_surfaces(struct pipe_video_buffer *buffer);

/*
 * destroy the associated data
 */
void
d3d12_video_buffer_destroy_associated_data(void *associated_data);

/**
 * output for decoding / input for displaying
 */
struct d3d12_video_buffer
{
   pipe_video_buffer                       base;
   struct d3d12_resource *                 texture = nullptr;
   uint                                    num_planes = 0;
   std::vector<pipe_surface *>      surfaces;
   std::vector<pipe_sampler_view *> sampler_view_planes;
   std::vector<pipe_sampler_view *> sampler_view_components;

   // Indicates the subresource index into the texture.array_size
   // that corresponds to this video buffer object
   uint                             idx_texarray_slots = 0;

   // Used by d3d12_video_buffer_destroy() when using texture array mode
   // in the function d3d12_video_enc::d3d12_video_create_dpb_buffer()
   // Points to the same address as d3d12_video_encoder::m_spVideoTexArrayDPBPoolInUse
   std::shared_ptr<uint32_t> m_spVideoTexArrayDPBPoolInUse;
};

///
/// Pipe video buffer interface ends
///

/**
 * creates a video dpb buffer
 */

enum class d3d12_video_buffer_creation_mode
{
   create_resource = 0,
   place_on_resource = 1,
   open_shared_resource = 2,
};

struct pipe_video_buffer*
d3d12_video_create_dpb_buffer(struct pipe_video_codec *codec,
                              struct pipe_picture_desc *picture,
                              const struct pipe_video_buffer *templat);

struct pipe_video_buffer*
d3d12_video_create_dpb_buffer_aot(struct pipe_video_codec *codec,
                                  struct pipe_picture_desc *picture,
                                  const struct pipe_video_buffer *templat);

struct pipe_video_buffer*
d3d12_video_create_dpb_buffer_texarray(struct pipe_video_codec *codec,
                                       struct pipe_picture_desc *picture,
                                       const struct pipe_video_buffer *templat);

#endif
