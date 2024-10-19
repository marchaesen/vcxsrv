/*
 * Copyright © 2012 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FREEDRENO_STATE_H_
#define FREEDRENO_STATE_H_

#include "pipe/p_context.h"
#include "freedreno_context.h"

BEGINC;

static inline bool
fd_depth_enabled(struct fd_context *ctx) assert_dt
{
   return ctx->zsa && ctx->zsa->depth_enabled;
}

static inline bool
fd_depth_write_enabled(struct fd_context *ctx) assert_dt
{
   return ctx->zsa && ctx->zsa->depth_writemask;
}

static inline bool
fd_stencil_enabled(struct fd_context *ctx) assert_dt
{
   return ctx->zsa && ctx->zsa->stencil[0].enabled;
}

static inline bool
fd_blend_enabled(struct fd_context *ctx, unsigned n) assert_dt
{
   return ctx->blend && ctx->blend->rt[n].blend_enable;
}

static inline bool
fd_rast_depth_clamp_enabled(const struct pipe_rasterizer_state *cso)
{
   return !(cso->depth_clip_near && cso->depth_clip_far);
}

static inline bool
fd_depth_clamp_enabled(struct fd_context *ctx) assert_dt
{
   return fd_rast_depth_clamp_enabled(ctx->rasterizer);
}

void fd_set_shader_buffers(struct pipe_context *pctx,
                           enum pipe_shader_type shader,
                           unsigned start, unsigned count,
                           const struct pipe_shader_buffer *buffers,
                           unsigned writable_bitmask) in_dt;

void fd_set_shader_images(struct pipe_context *pctx,
                          enum pipe_shader_type shader, unsigned start,
                          unsigned count, unsigned unbind_num_trailing_slots,
                          const struct pipe_image_view *images) in_dt;

void fd_set_framebuffer_state(struct pipe_context *pctx,
                         const struct pipe_framebuffer_state *framebuffer) in_dt;

void fd_state_init(struct pipe_context *pctx);

ENDC;

#endif /* FREEDRENO_STATE_H_ */
