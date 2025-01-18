/*
 * Copyright © 2012-2013 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD2_RASTERIZER_H_
#define FD2_RASTERIZER_H_

#include "pipe/p_context.h"
#include "pipe/p_state.h"

struct fd2_rasterizer_stateobj {
   struct pipe_rasterizer_state base;
   uint32_t pa_sc_line_stipple;
   uint32_t pa_cl_clip_cntl;
   uint32_t pa_su_vtx_cntl;
   uint32_t pa_su_point_size;
   uint32_t pa_su_point_minmax;
   uint32_t pa_su_line_cntl;
   uint32_t pa_su_sc_mode_cntl;
};

static inline struct fd2_rasterizer_stateobj *
fd2_rasterizer_stateobj(struct pipe_rasterizer_state *rast)
{
   return (struct fd2_rasterizer_stateobj *)rast;
}

void *fd2_rasterizer_state_create(struct pipe_context *pctx,
                                  const struct pipe_rasterizer_state *cso);

#endif /* FD2_RASTERIZER_H_ */
