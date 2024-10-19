/*
 * Copyright © 2012-2013 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD2_EMIT_H
#define FD2_EMIT_H

#include "pipe/p_context.h"

#include "freedreno_context.h"

struct fd_ringbuffer;

struct fd2_vertex_buf {
   unsigned offset, size;
   struct pipe_resource *prsc;
};

void fd2_emit_vertex_bufs(struct fd_ringbuffer *ring, uint32_t val,
                          struct fd2_vertex_buf *vbufs, uint32_t n);
void fd2_emit_state_binning(struct fd_context *ctx,
                            const enum fd_dirty_3d_state dirty) assert_dt;
void fd2_emit_state(struct fd_context *ctx,
                    const enum fd_dirty_3d_state dirty) assert_dt;
void fd2_emit_restore(struct fd_context *ctx, struct fd_ringbuffer *ring);

void fd2_emit_init_screen(struct pipe_screen *pscreen);
void fd2_emit_init(struct pipe_context *pctx);

static inline void
fd2_emit_ib(struct fd_ringbuffer *ring, struct fd_ringbuffer *target)
{
   __OUT_IB(ring, false, target);
}

#endif /* FD2_EMIT_H */
