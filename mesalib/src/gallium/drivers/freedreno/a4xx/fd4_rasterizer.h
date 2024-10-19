/*
 * Copyright © 2014 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD4_RASTERIZER_H_
#define FD4_RASTERIZER_H_

#include "pipe/p_context.h"
#include "pipe/p_state.h"

struct fd4_rasterizer_stateobj {
   struct pipe_rasterizer_state base;
   uint32_t gras_su_point_minmax;
   uint32_t gras_su_point_size;
   uint32_t gras_su_poly_offset_scale;
   uint32_t gras_su_poly_offset_offset;
   uint32_t gras_su_poly_offset_clamp;

   uint32_t gras_su_mode_control;
   uint32_t gras_cl_clip_cntl;
   uint32_t pc_prim_vtx_cntl;
   uint32_t pc_prim_vtx_cntl2;
};

static inline struct fd4_rasterizer_stateobj *
fd4_rasterizer_stateobj(struct pipe_rasterizer_state *rast)
{
   return (struct fd4_rasterizer_stateobj *)rast;
}

void *fd4_rasterizer_state_create(struct pipe_context *pctx,
                                  const struct pipe_rasterizer_state *cso);

#endif /* FD4_RASTERIZER_H_ */
