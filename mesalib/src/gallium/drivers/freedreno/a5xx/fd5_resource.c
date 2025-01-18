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
   unsigned lrz_pitch = align(DIV_ROUND_UP(rsc->b.b.width0, 8), 64);
   unsigned lrz_height = DIV_ROUND_UP(rsc->b.b.height0, 8);

   /* LRZ buffer is super-sampled: */
   switch (rsc->b.b.nr_samples) {
   case 4:
      lrz_pitch *= 2;
      FALLTHROUGH;
   case 2:
      lrz_height *= 2;
   }

   unsigned size = lrz_pitch * lrz_height * 2;

   size += 0x1000; /* for GRAS_LRZ_FAST_CLEAR_BUFFER */

   rsc->lrz_height = lrz_height;
   rsc->lrz_width = lrz_pitch;
   rsc->lrz_pitch = lrz_pitch;
   rsc->lrz = fd_bo_new(screen->dev, size, FD_BO_NOMAP, "lrz");
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
