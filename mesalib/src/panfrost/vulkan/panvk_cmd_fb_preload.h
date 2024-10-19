/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_FB_PRELOAD_H
#define PANVK_FB_PRELOAD_H

#include "panvk_cmd_buffer.h"

VkResult panvk_per_arch(cmd_fb_preload)(struct panvk_cmd_buffer *cmdbuf);

#endif
