/*
 * Copyright © 2025 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "freedreno_lrz_layout.h"
#include "util/compiler.h"

void
fdl5_lrz_layout_init(struct fdl_lrz_layout *lrz_layout, uint32_t width,
                     uint32_t height, uint32_t nr_samples)
{
   uint32_t lrz_pitch = align(DIV_ROUND_UP(width, 8), 64);
   uint32_t lrz_height = DIV_ROUND_UP(height, 8);

   /* LRZ buffer is super-sampled: */
   switch (nr_samples) {
   case 4:
      lrz_pitch *= 2;
      FALLTHROUGH;
   case 2:
      lrz_height *= 2;
   }

   uint32_t lrz_size = lrz_pitch * lrz_height * 2;
   lrz_size += 0x1000; /* for GRAS_LRZ_FAST_CLEAR_BUFFER */

   lrz_layout->lrz_offset = 0;
   lrz_layout->lrz_pitch = lrz_pitch;
   lrz_layout->lrz_height = lrz_height;
   lrz_layout->lrz_layer_size = 0;
   lrz_layout->lrz_fc_offset = 0;
   lrz_layout->lrz_fc_size = 0;
   lrz_layout->lrz_total_size = lrz_size;
}
