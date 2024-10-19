/*
 * Copyright © 2014 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD4_TEXTURE_H_
#define FD4_TEXTURE_H_

#include "pipe/p_context.h"

#include "freedreno_resource.h"
#include "freedreno_texture.h"

#include "fd4_context.h"
#include "fd4_format.h"

struct fd4_sampler_stateobj {
   struct pipe_sampler_state base;
   uint32_t texsamp0, texsamp1;
   bool needs_border;
};

static inline struct fd4_sampler_stateobj *
fd4_sampler_stateobj(struct pipe_sampler_state *samp)
{
   return (struct fd4_sampler_stateobj *)samp;
}

struct fd4_pipe_sampler_view {
   struct pipe_sampler_view base;
   uint32_t texconst0, texconst1, texconst2, texconst3, texconst4;
   uint32_t offset;
   bool astc_srgb;
   uint32_t swizzle;
};

static inline struct fd4_pipe_sampler_view *
fd4_pipe_sampler_view(struct pipe_sampler_view *pview)
{
   return (struct fd4_pipe_sampler_view *)pview;
}

unsigned fd4_get_const_idx(struct fd_context *ctx,
                           struct fd_texture_stateobj *tex, unsigned samp_id);

void fd4_texture_init(struct pipe_context *pctx);

static inline enum a4xx_tex_type
fd4_tex_type(unsigned target)
{
   switch (target) {
   default:
      unreachable("Unsupported target");
   case PIPE_BUFFER:
      return A4XX_TEX_BUFFER;
   case PIPE_TEXTURE_1D:
   case PIPE_TEXTURE_1D_ARRAY:
      return A4XX_TEX_1D;
   case PIPE_TEXTURE_RECT:
   case PIPE_TEXTURE_2D:
   case PIPE_TEXTURE_2D_ARRAY:
      return A4XX_TEX_2D;
   case PIPE_TEXTURE_3D:
      return A4XX_TEX_3D;
   case PIPE_TEXTURE_CUBE:
   case PIPE_TEXTURE_CUBE_ARRAY:
      return A4XX_TEX_CUBE;
   }
}

#endif /* FD4_TEXTURE_H_ */
