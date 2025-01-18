/*
 * Copyright © 2014 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD4_PROGRAM_H_
#define FD4_PROGRAM_H_

#include "pipe/p_context.h"
#include "freedreno_context.h"

#include "ir3/ir3_cache.h"
#include "ir3/ir3_shader.h"

struct fd4_emit;

struct fd4_program_state {
   struct ir3_program_state base;
   const struct ir3_shader_variant *bs; /* VS for when emit->binning */
   const struct ir3_shader_variant *vs;
   const struct ir3_shader_variant *fs; /* FS for when !emit->binning */
};

static inline struct fd4_program_state *
fd4_program_state(struct ir3_program_state *state)
{
   return (struct fd4_program_state *)state;
}

void fd4_emit_shader(struct fd_ringbuffer *ring,
                     const struct ir3_shader_variant *so);

void fd4_program_emit(struct fd_ringbuffer *ring, struct fd4_emit *emit, int nr,
                      struct pipe_surface **bufs);

void fd4_prog_init(struct pipe_context *pctx);

#endif /* FD4_PROGRAM_H_ */
