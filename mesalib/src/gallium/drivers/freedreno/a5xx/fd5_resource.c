/*
 * Copyright © 2018 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "fd5_resource.h"

static void
setup_lrz(struct fd_resource *rsc)
{
   struct fd_screen *screen = fd_screen(rsc->b.b.screen);
   fdl5_lrz_layout_init(&rsc->lrz_layout, rsc->b.b.width0, rsc->b.b.height0,
                        rsc->b.b.nr_samples);
   rsc->lrz = fd_bo_new(screen->dev, rsc->lrz_layout.lrz_total_size,
                        FD_BO_NOMAP, "lrz");
}

uint32_t
fd5_setup_slices(struct fd_resource *rsc)
{
   struct pipe_resource *prsc = &rsc->b.b;

   if (FD_DBG(LRZ) && has_depth(prsc->format) && !is_z32(prsc->format))
      setup_lrz(rsc);

   fdl5_layout(&rsc->layout, prsc->format, fd_resource_nr_samples(prsc),
               prsc->width0, prsc->height0, prsc->depth0, prsc->last_level + 1,
               prsc->array_size, prsc->target == PIPE_TEXTURE_3D);

   return rsc->layout.size;
}
