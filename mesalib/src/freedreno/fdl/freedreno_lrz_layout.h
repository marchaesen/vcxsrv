/*
 * Copyright © 2025 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#ifndef FREEDRENO_LRZ_LAYOUT_H_
#define FREEDRENO_LRZ_LAYOUT_H_

#include <stdint.h>

#include "freedreno_layout.h"

BEGINC;

struct fdl_lrz_layout {
   uint32_t lrz_offset;
   uint32_t lrz_pitch;
   uint32_t lrz_height;
   uint32_t lrz_layer_size;
   uint32_t lrz_fc_offset;
   uint32_t lrz_fc_size;
   uint32_t lrz_total_size;
};

void
fdl5_lrz_layout_init(struct fdl_lrz_layout *lrz_layout, uint32_t width,
                     uint32_t height, uint32_t nr_samples);
ENDC;

#ifdef __cplusplus
#include "common/freedreno_lrz.h"

template <chip CHIP>
static void
fdl6_lrz_layout_init(struct fdl_lrz_layout *lrz_layout,
                     struct fdl_layout *layout,
                     const struct fd_dev_info *dev_info, uint32_t lrz_offset,
                     uint32_t array_layers)
{
   unsigned width = layout->width0;
   unsigned height = layout->height0;

   /* LRZ buffer is super-sampled */
   switch (layout->nr_samples) {
   case 8:
      height *= 2;
      FALLTHROUGH;
   case 4:
      width *= 2;
      FALLTHROUGH;
   case 2:
      height *= 2;
      break;
   default:
      break;
   }

   unsigned lrz_pitch = align(DIV_ROUND_UP(width, 8), 32);
   unsigned lrz_height = align(DIV_ROUND_UP(height, 8), 32);

   lrz_layout->lrz_offset = lrz_offset;
   lrz_layout->lrz_height = lrz_height;
   lrz_layout->lrz_pitch = lrz_pitch;
   lrz_layout->lrz_layer_size = lrz_pitch * lrz_height * sizeof(uint16_t);

   unsigned nblocksx = DIV_ROUND_UP(DIV_ROUND_UP(width, 8), 16);
   unsigned nblocksy = DIV_ROUND_UP(DIV_ROUND_UP(height, 8), 4);

   /* Fast-clear buffer is 1bit/block */
   lrz_layout->lrz_fc_size =
      DIV_ROUND_UP(nblocksx * nblocksy, 8) * array_layers;

   /* Fast-clear buffer cannot be larger than 512 bytes on A6XX and 1024 bytes
    * on A7XX (HW limitation) */
   if (!dev_info->a6xx.enable_lrz_fast_clear ||
       lrz_layout->lrz_fc_size > fd_lrzfc_layout<CHIP>::FC_SIZE) {
      lrz_layout->lrz_fc_size = 0;
   }

   uint32_t lrz_size = lrz_layout->lrz_layer_size * array_layers;
   if (dev_info->a6xx.enable_lrz_fast_clear ||
       dev_info->a6xx.has_lrz_dir_tracking) {
      lrz_layout->lrz_fc_offset =
         lrz_layout->lrz_offset + lrz_size;
      lrz_size += sizeof(fd_lrzfc_layout<CHIP>);
   }

   lrz_layout->lrz_total_size = lrz_size;

   uint32_t lrz_clear_height = lrz_layout->lrz_height * array_layers;
   if (((lrz_clear_height - 1) >> 14) > 0) {
      /* For simplicity bail out if LRZ cannot be cleared in one go. */
      lrz_layout->lrz_height = 0;
      lrz_layout->lrz_total_size = 0;
   }
}
#endif

#endif
