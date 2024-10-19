/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD6_BLEND_H_
#define FD6_BLEND_H_

#include "pipe/p_context.h"
#include "pipe/p_state.h"

#include "freedreno_context.h"
#include "freedreno_util.h"

/**
 * Since the sample-mask is part of the hw blend state, we need to have state
 * variants per sample-mask value.  But we don't expect the sample-mask state
 * to change frequently.
 */
struct fd6_blend_variant {
   unsigned sample_mask;
   struct fd_ringbuffer *stateobj;
};

struct fd6_blend_stateobj {
   struct pipe_blend_state base;

   bool use_dual_src_blend;

   struct fd_context *ctx;
   bool reads_dest;
   uint32_t all_mrt_write_mask;
   struct util_dynarray variants;
};

static inline struct fd6_blend_stateobj *
fd6_blend_stateobj(struct pipe_blend_state *blend)
{
   return (struct fd6_blend_stateobj *)blend;
}

template <chip CHIP>
struct fd6_blend_variant *
__fd6_setup_blend_variant(struct fd6_blend_stateobj *blend,
                          unsigned sample_mask);

template <chip CHIP>
static inline struct fd6_blend_variant *
fd6_blend_variant(struct pipe_blend_state *cso, unsigned nr_samples,
                  unsigned sample_mask)
{
   struct fd6_blend_stateobj *blend = fd6_blend_stateobj(cso);
   unsigned mask = BITFIELD_MASK(nr_samples);

   util_dynarray_foreach (&blend->variants, struct fd6_blend_variant *, vp) {
      struct fd6_blend_variant *v = *vp;

      /* mask out sample-mask bits that we don't care about to avoid
       * creating unnecessary variants
       */
      if ((mask & v->sample_mask) == (mask & sample_mask)) {
         return v;
      }
   }

   return __fd6_setup_blend_variant<CHIP>(blend, sample_mask);
}

void *fd6_blend_state_create(struct pipe_context *pctx,
                             const struct pipe_blend_state *cso);
void fd6_blend_state_delete(struct pipe_context *, void *hwcso);

#endif /* FD6_BLEND_H_ */
