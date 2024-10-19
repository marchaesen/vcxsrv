/*
 * Copyright © 2014 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD4_BLEND_H_
#define FD4_BLEND_H_

#include "pipe/p_context.h"
#include "pipe/p_state.h"

#include "freedreno_util.h"

struct fd4_blend_stateobj {
   struct pipe_blend_state base;
   struct {
      uint32_t control;
      uint32_t buf_info;
      uint32_t blend_control;
   } rb_mrt[A4XX_MAX_RENDER_TARGETS];
   uint32_t rb_fs_output;
};

static inline struct fd4_blend_stateobj *
fd4_blend_stateobj(struct pipe_blend_state *blend)
{
   return (struct fd4_blend_stateobj *)blend;
}

void *fd4_blend_state_create(struct pipe_context *pctx,
                             const struct pipe_blend_state *cso);

#endif /* FD4_BLEND_H_ */
