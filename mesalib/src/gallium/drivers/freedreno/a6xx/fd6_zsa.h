/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD6_ZSA_H_
#define FD6_ZSA_H_

#include "pipe/p_context.h"
#include "pipe/p_state.h"

#include "freedreno_util.h"

#include "fd6_context.h"

#define FD6_ZSA_NO_ALPHA    (1 << 0)
#define FD6_ZSA_DEPTH_CLAMP (1 << 1)

struct fd6_zsa_stateobj {
   struct pipe_depth_stencil_alpha_state base;

   uint32_t rb_alpha_control;
   uint32_t rb_depth_cntl;
   uint32_t rb_stencil_control;
   uint32_t rb_stencilmask;
   uint32_t rb_stencilwrmask;

   struct fd6_lrz_state lrz;
   bool writes_zs : 1; /* writes depth and/or stencil */
   bool writes_z : 1;  /* writes depth */
   bool invalidate_lrz : 1;
   bool alpha_test : 1;

   /* Track whether we've alread generated perf warns so that
    * we don't flood the user with LRZ disable warns which can
    * only be detected at draw time.
    */
   bool perf_warn_blend : 1;
   bool perf_warn_zdir : 1;

   struct fd_ringbuffer *stateobj[4];
};

static inline struct fd6_zsa_stateobj *
fd6_zsa_stateobj(struct pipe_depth_stencil_alpha_state *zsa)
{
   return (struct fd6_zsa_stateobj *)zsa;
}

static inline struct fd_ringbuffer *
fd6_zsa_state(struct fd_context *ctx, bool no_alpha, bool depth_clamp) assert_dt
{
   int variant = 0;
   if (no_alpha)
      variant |= FD6_ZSA_NO_ALPHA;
   if (depth_clamp)
      variant |= FD6_ZSA_DEPTH_CLAMP;
   return fd6_zsa_stateobj(ctx->zsa)->stateobj[variant];
}

template <chip CHIP>
void *fd6_zsa_state_create(struct pipe_context *pctx,
                           const struct pipe_depth_stencil_alpha_state *cso);

void fd6_zsa_state_delete(struct pipe_context *pctx, void *hwcso);

#endif /* FD6_ZSA_H_ */
