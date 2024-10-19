/*
 * Copyright © 2012-2013 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD2_BLEND_H_
#define FD2_BLEND_H_

#include "pipe/p_context.h"
#include "pipe/p_state.h"

struct fd2_blend_stateobj {
   struct pipe_blend_state base;
   uint32_t rb_blendcontrol;
   uint32_t rb_colorcontrol; /* must be OR'd w/ zsa->rb_colorcontrol */
   uint32_t rb_colormask;
};

static inline struct fd2_blend_stateobj *
fd2_blend_stateobj(struct pipe_blend_state *blend)
{
   return (struct fd2_blend_stateobj *)blend;
}

void *fd2_blend_state_create(struct pipe_context *pctx,
                             const struct pipe_blend_state *cso);

#endif /* FD2_BLEND_H_ */
