/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD6_DRAW_H_
#define FD6_DRAW_H_

#include "pipe/p_context.h"

#include "freedreno_draw.h"

#include "fd6_context.h"

template <chip CHIP>
void fd6_draw_init(struct pipe_context *pctx);

#endif /* FD6_DRAW_H_ */
