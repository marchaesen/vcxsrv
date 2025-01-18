/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD5_RASTERIZER_H_
#define FD5_RASTERIZER_H_

#include "pipe/p_context.h"
#include "pipe/p_state.h"

struct fd5_rasterizer_stateobj {
   struct pipe_rasterizer_state base;

   uint32_t gras_su_point_minmax;
   uint32_t gras_su_point_size;
   uint32_t gras_su_poly_offset_scale;
   uint32_t gras_su_poly_offset_offset;
   uint32_t gras_su_poly_offset_clamp;

   uint32_t gras_su_cntl;
   uint32_t gras_cl_clip_cntl;
   uint32_t pc_primitive_cntl;
   uint32_t pc_raster_cntl;
};

static inline struct fd5_rasterizer_stateobj *
fd5_rasterizer_stateobj(struct pipe_rasterizer_state *rast)
{
   return (struct fd5_rasterizer_stateobj *)rast;
}

void *fd5_rasterizer_state_create(struct pipe_context *pctx,
                                  const struct pipe_rasterizer_state *cso);

#endif /* FD5_RASTERIZER_H_ */
