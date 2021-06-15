/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_common.h"

#include <stdarg.h>

#include "util/debug.h"
#include "util/log.h"
#include "util/os_misc.h"
#include "vk_enum_to_str.h"

#if __STDC_VERSION__ >= 201112L
#define VN_MAX_ALIGN _Alignof(max_align_t)
#else
#define VN_MAX_ALIGN VN_DEFAULT_ALIGN
#endif

static const struct debug_control vn_debug_options[] = {
   { "init", VN_DEBUG_INIT },
   { "result", VN_DEBUG_RESULT },
   { "vtest", VN_DEBUG_VTEST },
   { "wsi", VN_DEBUG_WSI },
   { NULL, 0 },
};

uint64_t vn_debug;

static void
vn_debug_init_once(void)
{
   vn_debug = parse_debug_string(os_get_option("VN_DEBUG"), vn_debug_options);
}

void
vn_debug_init(void)
{
   static once_flag once = ONCE_FLAG_INIT;
   call_once(&once, vn_debug_init_once);
}

void
vn_log(struct vn_instance *instance, const char *format, ...)
{
   va_list ap;

   va_start(ap, format);
   mesa_log_v(MESA_LOG_DEBUG, "MESA-VIRTIO", format, ap);
   va_end(ap);

   /* instance may be NULL or partially initialized */
}

VkResult
vn_log_result(struct vn_instance *instance,
              VkResult result,
              const char *where)
{
   vn_log(instance, "%s: %s", where, vk_Result_to_str(result));
   return result;
}

static void *
vn_default_alloc(void *pUserData,
                 size_t size,
                 size_t alignment,
                 VkSystemAllocationScope allocationScope)
{
   assert(VN_MAX_ALIGN % alignment == 0);
   return malloc(size);
}

static void *
vn_default_realloc(void *pUserData,
                   void *pOriginal,
                   size_t size,
                   size_t alignment,
                   VkSystemAllocationScope allocationScope)
{
   assert(VN_MAX_ALIGN % alignment == 0);
   return realloc(pOriginal, size);
}

static void
vn_default_free(void *pUserData, void *pMemory)
{
   free(pMemory);
}

const VkAllocationCallbacks *
vn_default_allocator(void)
{
   static const VkAllocationCallbacks allocator = {
      .pfnAllocation = vn_default_alloc,
      .pfnReallocation = vn_default_realloc,
      .pfnFree = vn_default_free,
   };
   return &allocator;
}

void
vn_relax(uint32_t *iter)
{
   const uint32_t busy_wait_order = 4;
   const uint32_t base_sleep_us = 10;

   (*iter)++;
   if (*iter < (1 << busy_wait_order)) {
      thrd_yield();
      return;
   }

   const uint32_t shift = util_last_bit(*iter) - busy_wait_order - 1;
   os_time_sleep(base_sleep_us << shift);
}
