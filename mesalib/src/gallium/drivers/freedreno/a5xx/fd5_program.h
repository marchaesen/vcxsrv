/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD5_PROGRAM_H_
#define FD5_PROGRAM_H_

#include "pipe/p_context.h"
#include "freedreno_context.h"

#include "ir3/ir3_cache.h"
#include "ir3/ir3_shader.h"

struct fd5_emit;

struct fd5_program_state {
   struct ir3_program_state base;
   const struct ir3_shader_variant *bs; /* VS for when emit->binning */
   const struct ir3_shader_variant *vs;
   const struct ir3_shader_variant *fs; /* FS for when !emit->binning */
};

static inline struct fd5_program_state *
fd5_program_state(struct ir3_program_state *state)
{
   return (struct fd5_program_state *)state;
}

void fd5_emit_shader(struct fd_ringbuffer *ring,
                     const struct ir3_shader_variant *so);

void fd5_emit_shader_obj(struct fd_context *ctx, struct fd_ringbuffer *ring,
                         const struct ir3_shader_variant *so,
                         uint32_t shader_obj_reg) assert_dt;

void fd5_program_emit(struct fd_context *ctx, struct fd_ringbuffer *ring,
                      struct fd5_emit *emit) assert_dt;

void fd5_prog_init(struct pipe_context *pctx);

#endif /* FD5_PROGRAM_H_ */
