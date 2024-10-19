/*
 * Copyright © 2013 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD3_ZSA_H_
#define FD3_ZSA_H_

#include "pipe/p_context.h"
#include "pipe/p_state.h"

#include "freedreno_util.h"

struct fd3_zsa_stateobj {
   struct pipe_depth_stencil_alpha_state base;
   uint32_t rb_render_control;
   uint32_t rb_alpha_ref;
   uint32_t rb_depth_control;
   uint32_t rb_stencil_control;
   uint32_t rb_stencilrefmask;
   uint32_t rb_stencilrefmask_bf;
};

static inline struct fd3_zsa_stateobj *
fd3_zsa_stateobj(struct pipe_depth_stencil_alpha_state *zsa)
{
   return (struct fd3_zsa_stateobj *)zsa;
}

void *fd3_zsa_state_create(struct pipe_context *pctx,
                           const struct pipe_depth_stencil_alpha_state *cso);

#endif /* FD3_ZSA_H_ */
