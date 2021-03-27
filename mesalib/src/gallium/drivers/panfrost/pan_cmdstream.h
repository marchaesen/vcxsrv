/*
 * Copyright (C) 2018 Alyssa Rosenzweig
 * Copyright (C) 2020 Collabora Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __PAN_CMDSTREAM_H__
#define __PAN_CMDSTREAM_H__

#include "pipe/p_defines.h"
#include "pipe/p_state.h"

#include "midgard_pack.h"

#include "pan_job.h"

void panfrost_sampler_desc_init(const struct pipe_sampler_state *cso, struct mali_midgard_sampler_packed *hw);
void panfrost_sampler_desc_init_bifrost(const struct pipe_sampler_state *cso, struct mali_bifrost_sampler_packed *hw);

mali_ptr
panfrost_emit_compute_shader_meta(struct panfrost_batch *batch, enum pipe_shader_type stage);

mali_ptr
panfrost_emit_frag_shader_meta(struct panfrost_batch *batch);

mali_ptr
panfrost_emit_viewport(struct panfrost_batch *batch);

mali_ptr
panfrost_emit_const_buf(struct panfrost_batch *batch,
                        enum pipe_shader_type stage,
                        mali_ptr *push_constants);

mali_ptr
panfrost_emit_shared_memory(struct panfrost_batch *batch,
                            const struct pipe_grid_info *info);

mali_ptr
panfrost_emit_texture_descriptors(struct panfrost_batch *batch,
                                  enum pipe_shader_type stage);

mali_ptr
panfrost_emit_sampler_descriptors(struct panfrost_batch *batch,
                                  enum pipe_shader_type stage);

mali_ptr
panfrost_emit_image_attribs(struct panfrost_batch *batch,
                            mali_ptr *buffers,
                            enum pipe_shader_type type);

mali_ptr
panfrost_emit_vertex_data(struct panfrost_batch *batch,
                          mali_ptr *buffers);

mali_ptr
panfrost_get_index_buffer_bounded(struct panfrost_context *ctx,
                                  const struct pipe_draw_info *info,
                                  const struct pipe_draw_start_count *draw,
                                  unsigned *min_index, unsigned *max_index);

void
panfrost_emit_varying_descriptor(struct panfrost_batch *batch,
                                 unsigned vertex_count,
                                 mali_ptr *vs_attribs,
                                 mali_ptr *fs_attribs,
                                 mali_ptr *buffers,
                                 mali_ptr *position,
                                 mali_ptr *psiz);

void
panfrost_emit_vertex_tiler_jobs(struct panfrost_batch *batch,
                                const struct panfrost_ptr *vertex_job,
                                const struct panfrost_ptr *tiler_job);

static inline unsigned
panfrost_translate_compare_func(enum pipe_compare_func in)
{
        switch (in) {
        case PIPE_FUNC_NEVER: return MALI_FUNC_NEVER;
        case PIPE_FUNC_LESS: return MALI_FUNC_LESS;
        case PIPE_FUNC_EQUAL: return MALI_FUNC_EQUAL;
        case PIPE_FUNC_LEQUAL: return MALI_FUNC_LEQUAL;
        case PIPE_FUNC_GREATER: return MALI_FUNC_GREATER;
        case PIPE_FUNC_NOTEQUAL: return MALI_FUNC_NOT_EQUAL;
        case PIPE_FUNC_GEQUAL: return MALI_FUNC_GEQUAL;
        case PIPE_FUNC_ALWAYS: return MALI_FUNC_ALWAYS;
        default: unreachable("Invalid func");
        }
}

static inline enum mali_sample_pattern
panfrost_sample_pattern(unsigned samples)
{
        switch (samples) {
        case 1:  return MALI_SAMPLE_PATTERN_SINGLE_SAMPLED;
        case 4:  return MALI_SAMPLE_PATTERN_ROTATED_4X_GRID;
        case 8:  return MALI_SAMPLE_PATTERN_D3D_8X_GRID;
        case 16: return MALI_SAMPLE_PATTERN_D3D_16X_GRID;
        default: unreachable("Unsupported sample count");
        }
}

#endif /* __PAN_CMDSTREAM_H__ */
