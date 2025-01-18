/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD5_SCREEN_H_
#define FD5_SCREEN_H_

#include "pipe/p_screen.h"

#include "freedreno_util.h"

#include "a5xx.xml.h"

void fd5_screen_init(struct pipe_screen *pscreen);

static inline void
emit_marker5(struct fd_ringbuffer *ring, int scratch_idx)
{
   extern int32_t marker_cnt;
   unsigned reg = REG_A5XX_CP_SCRATCH_REG(scratch_idx);
   if (__EMIT_MARKER) {
      OUT_WFI5(ring);
      OUT_PKT4(ring, reg, 1);
      OUT_RING(ring, p_atomic_inc_return(&marker_cnt));
   }
}

#endif /* FD5_SCREEN_H_ */
