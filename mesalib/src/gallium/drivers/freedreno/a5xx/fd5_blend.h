/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD5_BLEND_H_
#define FD5_BLEND_H_

#include "pipe/p_context.h"
#include "pipe/p_state.h"

#include "freedreno_util.h"

struct fd5_blend_stateobj {
   struct pipe_blend_state base;

   struct {
      uint32_t control;
      uint32_t buf_info;
      uint32_t blend_control;
   } rb_mrt[A5XX_MAX_RENDER_TARGETS];
   uint32_t rb_blend_cntl;
   uint32_t sp_blend_cntl;
   bool lrz_write;
};

static inline struct fd5_blend_stateobj *
fd5_blend_stateobj(struct pipe_blend_state *blend)
{
   return (struct fd5_blend_stateobj *)blend;
}

void *fd5_blend_state_create(struct pipe_context *pctx,
                             const struct pipe_blend_state *cso);

#endif /* FD5_BLEND_H_ */
