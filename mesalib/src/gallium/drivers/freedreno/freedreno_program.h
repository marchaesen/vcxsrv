/*
 * Copyright © 2014 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FREEDRENO_PROGRAM_H_
#define FREEDRENO_PROGRAM_H_

#include "pipe/p_context.h"

#include "common/freedreno_common.h"

BEGINC;

void fd_prog_init(struct pipe_context *pctx);
void fd_prog_fini(struct pipe_context *pctx);

ENDC;

#endif /* FREEDRENO_PROGRAM_H_ */
