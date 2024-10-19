/*
 * Copyright © 2017 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD6_BLIT_H_
#define FD6_BLIT_H_

#include "pipe/p_state.h"

#include "freedreno_context.h"


template <chip CHIP>
void fd6_blitter_init(struct pipe_context *pctx);
unsigned fd6_tile_mode_for_format(enum pipe_format pfmt);
unsigned fd6_tile_mode(const struct pipe_resource *tmpl);

/*
 * Blitter APIs used by gmem for cases that need CP_BLIT's (r2d)
 * instead of CP_EVENT_WRITE::BLITs
 */

template <chip CHIP>
void fd6_clear_lrz(struct fd_batch *batch, struct fd_resource *zsbuf,
                   struct fd_bo *lrz, double depth) assert_dt;
template <chip CHIP>
void fd6_clear_surface(struct fd_context *ctx, struct fd_ringbuffer *ring,
                       struct pipe_surface *psurf, const struct pipe_box *box2d,
                       union pipe_color_union *color, uint32_t unknown_8c01) assert_dt;
template <chip CHIP>
void fd6_resolve_tile(struct fd_batch *batch, struct fd_ringbuffer *ring,
                      uint32_t base, struct pipe_surface *psurf, uint32_t unknown_8c01) assert_dt;

#endif /* FD6_BLIT_H_ */
