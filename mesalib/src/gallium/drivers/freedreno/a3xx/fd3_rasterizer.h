/*
 * Copyright © 2013 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD3_RASTERIZER_H_
#define FD3_RASTERIZER_H_

#include "pipe/p_context.h"
#include "pipe/p_state.h"

struct fd3_rasterizer_stateobj {
   struct pipe_rasterizer_state base;
   uint32_t gras_su_point_minmax;
   uint32_t gras_su_point_size;
   uint32_t gras_su_poly_offset_scale;
   uint32_t gras_su_poly_offset_offset;

   uint32_t gras_su_mode_control;
   uint32_t gras_cl_clip_cntl;
   uint32_t pc_prim_vtx_cntl;
};

static inline struct fd3_rasterizer_stateobj *
fd3_rasterizer_stateobj(struct pipe_rasterizer_state *rast)
{
   return (struct fd3_rasterizer_stateobj *)rast;
}

void *fd3_rasterizer_state_create(struct pipe_context *pctx,
                                  const struct pipe_rasterizer_state *cso);

#endif /* FD3_RASTERIZER_H_ */
