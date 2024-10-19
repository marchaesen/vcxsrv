/*
 * Copyright © 2012-2013 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD2_ZSA_H_
#define FD2_ZSA_H_

#include "pipe/p_context.h"
#include "pipe/p_state.h"

#include "freedreno_util.h"

struct fd2_zsa_stateobj {
   struct pipe_depth_stencil_alpha_state base;
   uint32_t rb_depthcontrol;
   uint32_t rb_colorcontrol; /* must be OR'd w/ blend->rb_colorcontrol */
   uint32_t rb_alpha_ref;
   uint32_t rb_stencilrefmask;
   uint32_t rb_stencilrefmask_bf;
};

static inline struct fd2_zsa_stateobj *
fd2_zsa_stateobj(struct pipe_depth_stencil_alpha_state *zsa)
{
   return (struct fd2_zsa_stateobj *)zsa;
}

void *fd2_zsa_state_create(struct pipe_context *pctx,
                           const struct pipe_depth_stencil_alpha_state *cso);

#endif /* FD2_ZSA_H_ */
