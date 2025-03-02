/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <assert.h>

#include "vk_log.h"
#include "vk_util.h"

#define HK_MAX_SETS                   8
#define HK_MAX_PUSH_SIZE              256
#define HK_MAX_DYNAMIC_BUFFERS        64
#define HK_MAX_RTS                    8
#define HK_MIN_SSBO_ALIGNMENT         16
#define HK_MIN_TEXEL_BUFFER_ALIGNMENT 16
#define HK_MIN_UBO_ALIGNMENT          64
#define HK_MAX_VIEWPORTS              16
#define HK_MAX_DESCRIPTOR_SIZE        32
#define HK_MAX_PUSH_DESCRIPTORS       32
#define HK_MAX_DESCRIPTOR_SET_SIZE    (1u << 30)
#define HK_MAX_DESCRIPTORS            (1 << 20)
#define HK_PUSH_DESCRIPTOR_SET_SIZE                                            \
   (HK_MAX_PUSH_DESCRIPTORS * HK_MAX_DESCRIPTOR_SIZE)
#define HK_SSBO_BOUNDS_CHECK_ALIGNMENT 4
#define HK_MAX_MULTIVIEW_VIEW_COUNT    32

#define HK_SPARSE_ADDR_SPACE_SIZE (1ull << 39)
#define HK_MAX_BUFFER_SIZE        (1ull << 37)
#define HK_MAX_SHARED_SIZE        (32 * 1024)

struct hk_addr_range {
   uint64_t addr;
   uint64_t range;
};

#define hk_cmd_buffer_device(cmd) ((struct hk_device *)(cmd)->vk.base.device)

#define perf_debug_dev(dev, fmt, ...)                                          \
   do {                                                                        \
      if ((dev)->debug & AGX_DBG_PERF)                                         \
         mesa_log(MESA_LOG_WARN, (MESA_LOG_TAG), (fmt), ##__VA_ARGS__);        \
   } while (0)

#define perf_debug(cmd, fmt, ...)                                              \
   do {                                                                        \
      if (hk_cmd_buffer_device(cmd)->dev.debug & AGX_DBG_PERF)                 \
         mesa_log(MESA_LOG_WARN, (MESA_LOG_TAG), (fmt), ##__VA_ARGS__);        \
   } while (0)
