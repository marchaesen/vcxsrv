/*
 * Copyright © 2023 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef FD6_BARRIER_H_
#define FD6_BARRIER_H_

#include "freedreno_context.h"

/**
 * Various flush operations that could be needed
 */
enum fd6_flush {
   FD6_FLUSH_CCU_COLOR      = BIT(0),
   FD6_FLUSH_CCU_DEPTH      = BIT(1),
   FD6_INVALIDATE_CCU_COLOR = BIT(2),
   FD6_INVALIDATE_CCU_DEPTH = BIT(3),
   FD6_FLUSH_CACHE          = BIT(4),
   FD6_INVALIDATE_CACHE     = BIT(5),
   FD6_WAIT_MEM_WRITES      = BIT(6),
   FD6_WAIT_FOR_IDLE        = BIT(7),
   FD6_WAIT_FOR_ME          = BIT(8),
};

template <chip CHIP>
void fd6_emit_flushes(struct fd_context *ctx, struct fd_ringbuffer *ring,
                      unsigned flushes);

template <chip CHIP>
void fd6_barrier_flush(struct fd_batch *batch) assert_dt;

void fd6_barrier_init(struct pipe_context *pctx);

#endif /* FD6_BARRIER_H_ */
