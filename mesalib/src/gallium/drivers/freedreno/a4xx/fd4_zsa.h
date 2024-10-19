/*
 * Copyright © 2014 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD4_ZSA_H_
#define FD4_ZSA_H_

#include "pipe/p_context.h"
#include "pipe/p_state.h"

#include "freedreno_util.h"

struct fd4_zsa_stateobj {
   struct pipe_depth_stencil_alpha_state base;
   uint32_t gras_alpha_control;
   uint32_t rb_alpha_control;
   uint32_t rb_depth_control;
   uint32_t rb_stencil_control;
   uint32_t rb_stencil_control2;
   uint32_t rb_stencilrefmask;
   uint32_t rb_stencilrefmask_bf;
};

static inline struct fd4_zsa_stateobj *
fd4_zsa_stateobj(struct pipe_depth_stencil_alpha_state *zsa)
{
   return (struct fd4_zsa_stateobj *)zsa;
}

void *fd4_zsa_state_create(struct pipe_context *pctx,
                           const struct pipe_depth_stencil_alpha_state *cso);

#endif /* FD4_ZSA_H_ */
