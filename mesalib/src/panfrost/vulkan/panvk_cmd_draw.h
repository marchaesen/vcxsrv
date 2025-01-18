/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_CMD_DRAW_H
#define PANVK_CMD_DRAW_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include "panvk_cmd_buffer.h"
#include "panvk_physical_device.h"

#include "pan_props.h"

static inline uint32_t
panvk_select_tiler_hierarchy_mask(struct panvk_cmd_buffer *cmdbuf)
{
   struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(cmdbuf->vk.base.device->physical);
   struct panfrost_tiler_features tiler_features =
      panfrost_query_tiler_features(&phys_dev->kmod.props);
   uint32_t max_fb_wh = MAX2(cmdbuf->state.gfx.render.fb.info.width,
                             cmdbuf->state.gfx.render.fb.info.height);
   uint32_t last_hierarchy_bit = util_last_bit(DIV_ROUND_UP(max_fb_wh, 16));
   uint32_t hierarchy_mask = BITFIELD_MASK(tiler_features.max_levels);

   /* Always enable the level covering the whole FB, and disable the finest
    * levels if we don't have enough to cover everything.
    * This is suboptimal for small primitives, since it might force
    * primitives to be walked multiple times even if they don't cover the
    * the tile being processed. On the other hand, it's hard to guess
    * the draw pattern, so it's probably good enough for now.
    */
   if (last_hierarchy_bit > tiler_features.max_levels)
      hierarchy_mask <<= last_hierarchy_bit - tiler_features.max_levels;

   return hierarchy_mask;
}

#endif
