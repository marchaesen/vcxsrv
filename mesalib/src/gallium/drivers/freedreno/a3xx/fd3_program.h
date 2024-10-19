/*
 * Copyright © 2013 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD3_PROGRAM_H_
#define FD3_PROGRAM_H_

#include "pipe/p_context.h"
#include "freedreno_context.h"

#include "ir3/ir3_cache.h"
#include "ir3/ir3_shader.h"

struct fd3_emit;

struct fd3_program_state {
   struct ir3_program_state base;
   const struct ir3_shader_variant *bs; /* VS for when emit->binning */
   const struct ir3_shader_variant *vs;
   const struct ir3_shader_variant *fs; /* FS for when !emit->binning */
};

static inline struct fd3_program_state *
fd3_program_state(struct ir3_program_state *state)
{
   return (struct fd3_program_state *)state;
}

void fd3_program_emit(struct fd_ringbuffer *ring, struct fd3_emit *emit, int nr,
                      struct pipe_surface **bufs);

void fd3_prog_init(struct pipe_context *pctx);

bool fd3_needs_manual_clipping(const struct ir3_shader *,
                               const struct pipe_rasterizer_state *);

#endif /* FD3_PROGRAM_H_ */
