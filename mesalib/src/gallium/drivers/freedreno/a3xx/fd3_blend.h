/*
 * Copyright © 2013 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD3_BLEND_H_
#define FD3_BLEND_H_

#include "pipe/p_context.h"
#include "pipe/p_state.h"

#include "freedreno_util.h"

struct fd3_blend_stateobj {
   struct pipe_blend_state base;
   uint32_t rb_render_control;
   struct {
      uint32_t blend_control;
      uint32_t control;
   } rb_mrt[A3XX_MAX_RENDER_TARGETS];
};

static inline struct fd3_blend_stateobj *
fd3_blend_stateobj(struct pipe_blend_state *blend)
{
   return (struct fd3_blend_stateobj *)blend;
}

void *fd3_blend_state_create(struct pipe_context *pctx,
                             const struct pipe_blend_state *cso);

#endif /* FD3_BLEND_H_ */
