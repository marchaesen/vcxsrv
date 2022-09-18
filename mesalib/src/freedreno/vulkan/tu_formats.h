/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#ifndef TU_FORMATS_H
#define TU_FORMATS_H

#include "tu_common.h"

struct tu_native_format
{
   enum a6xx_format fmt : 8;
   enum a3xx_color_swap swap : 8;
   enum a6xx_tile_mode tile_mode : 8;
};

enum pipe_format tu_vk_format_to_pipe_format(VkFormat vk_format);
bool tu6_format_vtx_supported(VkFormat format);
struct tu_native_format tu6_format_vtx(VkFormat format);
bool tu6_format_color_supported(enum pipe_format format);
struct tu_native_format tu6_format_color(enum pipe_format format, enum a6xx_tile_mode tile_mode);
bool tu6_format_texture_supported(enum pipe_format format);
struct tu_native_format tu6_format_texture(enum pipe_format format, enum a6xx_tile_mode tile_mode);

static inline enum a6xx_format
tu6_base_format(enum pipe_format format)
{
   /* note: tu6_format_color doesn't care about tiling for .fmt field */
   return tu6_format_color(format, TILE6_LINEAR).fmt;
}

bool tu6_mutable_format_list_ubwc_compatible(const VkImageFormatListCreateInfo *fmt_list);

#endif /* TU_FORMATS_H */
