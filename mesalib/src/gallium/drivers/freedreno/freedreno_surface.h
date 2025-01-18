/*
 * Copyright © 2012 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FREEDRENO_SURFACE_H_
#define FREEDRENO_SURFACE_H_

#include "pipe/p_state.h"

struct fd_surface {
   struct pipe_surface base;
};

static inline struct fd_surface *
fd_surface(struct pipe_surface *psurf)
{
   return (struct fd_surface *)psurf;
}

struct pipe_surface *fd_create_surface(struct pipe_context *pctx,
                                       struct pipe_resource *ptex,
                                       const struct pipe_surface *surf_tmpl);
void fd_surface_destroy(struct pipe_context *pctx, struct pipe_surface *psurf);

#endif /* FREEDRENO_SURFACE_H_ */
