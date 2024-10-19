/*
 * Copyright © 2012-2013 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD2_TEXTURE_H_
#define FD2_TEXTURE_H_

#include "pipe/p_context.h"

#include "freedreno_resource.h"
#include "freedreno_texture.h"

#include "fd2_context.h"
#include "fd2_util.h"

struct fd2_sampler_stateobj {
   struct pipe_sampler_state base;
   uint32_t tex0, tex3, tex4;
};

static inline struct fd2_sampler_stateobj *
fd2_sampler_stateobj(struct pipe_sampler_state *samp)
{
   return (struct fd2_sampler_stateobj *)samp;
}

struct fd2_pipe_sampler_view {
   struct pipe_sampler_view base;
   uint32_t tex0, tex1, tex2, tex3, tex4, tex5;
};

static inline struct fd2_pipe_sampler_view *
fd2_pipe_sampler_view(struct pipe_sampler_view *pview)
{
   return (struct fd2_pipe_sampler_view *)pview;
}

unsigned fd2_get_const_idx(struct fd_context *ctx,
                           struct fd_texture_stateobj *tex, unsigned samp_id);

void fd2_texture_init(struct pipe_context *pctx);

#endif /* FD2_TEXTURE_H_ */
