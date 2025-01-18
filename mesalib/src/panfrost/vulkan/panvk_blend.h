/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_BLEND_H
#define PANVK_BLEND_H

#include <stdbool.h>

#include "util/hash_table.h"
#include "util/simple_mtx.h"

#include "pan_blend.h"

#include "panvk_macros.h"
#include "panvk_mempool.h"

struct vk_color_blend_state;
struct vk_dynamic_graphics_state;
struct panvk_device;

#ifdef PAN_ARCH

struct panvk_blend_info {
   bool any_dest_read;
   bool needs_shader;
   bool shader_loads_blend_const;
};

VkResult panvk_per_arch(blend_emit_descs)(
   struct panvk_device *dev, const struct vk_dynamic_graphics_state *dy,
   const VkFormat *color_attachment_formats, uint8_t *color_attachment_samples,
   const struct pan_shader_info *fs_info, mali_ptr fs_code,
   struct mali_blend_packed *bds, struct panvk_blend_info *blend_info);

#endif

#endif
