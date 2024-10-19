/*
 * Copyright © 2013 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD3_TEXTURE_H_
#define FD3_TEXTURE_H_

#include "pipe/p_context.h"

#include "freedreno_resource.h"
#include "freedreno_texture.h"

#include "fd3_context.h"
#include "fd3_format.h"

struct fd3_sampler_stateobj {
   struct pipe_sampler_state base;
   uint32_t texsamp0, texsamp1;
   bool needs_border;
};

static inline struct fd3_sampler_stateobj *
fd3_sampler_stateobj(struct pipe_sampler_state *samp)
{
   return (struct fd3_sampler_stateobj *)samp;
}

struct fd3_pipe_sampler_view {
   struct pipe_sampler_view base;
   uint32_t texconst0, texconst1, texconst2, texconst3;
};

static inline struct fd3_pipe_sampler_view *
fd3_pipe_sampler_view(struct pipe_sampler_view *pview)
{
   return (struct fd3_pipe_sampler_view *)pview;
}

unsigned fd3_get_const_idx(struct fd_context *ctx,
                           struct fd_texture_stateobj *tex, unsigned samp_id);

void fd3_texture_init(struct pipe_context *pctx);

#endif /* FD3_TEXTURE_H_ */
