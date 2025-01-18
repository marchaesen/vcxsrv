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

struct panvk_cmd_buffer;

#ifdef PAN_ARCH

struct panvk_blend_info {
   bool any_dest_read;
   bool needs_shader;
   bool shader_loads_blend_const;
};

VkResult panvk_per_arch(blend_emit_descs)(struct panvk_cmd_buffer *cmdbuf,
                                          struct mali_blend_packed *bds);

#endif

#endif
