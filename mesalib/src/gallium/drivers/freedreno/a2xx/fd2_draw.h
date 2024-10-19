/*
 * Copyright © 2012-2013 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD2_DRAW_H_
#define FD2_DRAW_H_

#include "pipe/p_context.h"

#include "freedreno_draw.h"

void fd2_draw_init(struct pipe_context *pctx);

enum {
   GMEM_PATCH_FASTCLEAR_COLOR,
   GMEM_PATCH_FASTCLEAR_DEPTH,
   GMEM_PATCH_FASTCLEAR_COLOR_DEPTH,
   GMEM_PATCH_RESTORE_INFO,
};

#endif /* FD2_DRAW_H_ */
