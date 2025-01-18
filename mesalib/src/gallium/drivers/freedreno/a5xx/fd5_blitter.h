/*
 * Copyright © 2017 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD5_BLIT_H_
#define FD5_BLIT_H_

#include "pipe/p_state.h"

#include "freedreno_context.h"

bool fd5_blitter_blit(struct fd_context *ctx,
                      const struct pipe_blit_info *info);
unsigned fd5_tile_mode(const struct pipe_resource *tmpl);

#endif /* FD5_BLIT_H_ */
