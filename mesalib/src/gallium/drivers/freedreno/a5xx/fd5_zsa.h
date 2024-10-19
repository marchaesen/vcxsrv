/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD5_ZSA_H_
#define FD5_ZSA_H_

#include "pipe/p_context.h"
#include "pipe/p_state.h"

#include "freedreno_util.h"

struct fd5_zsa_stateobj {
   struct pipe_depth_stencil_alpha_state base;

   uint32_t rb_alpha_control;
   uint32_t rb_depth_cntl;
   uint32_t rb_stencil_control;
   uint32_t rb_stencilrefmask;
   uint32_t rb_stencilrefmask_bf;
   uint32_t gras_lrz_cntl;
   bool lrz_write;
};

static inline struct fd5_zsa_stateobj *
fd5_zsa_stateobj(struct pipe_depth_stencil_alpha_state *zsa)
{
   return (struct fd5_zsa_stateobj *)zsa;
}

void *fd5_zsa_state_create(struct pipe_context *pctx,
                           const struct pipe_depth_stencil_alpha_state *cso);

#endif /* FD5_ZSA_H_ */
