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

void
vn_relax(uint32_t *iter, const char *reason)
{
   /* Yield for the first 2^busy_wait_order times and then sleep for
    * base_sleep_us microseconds for the same number of times.  After that,
    * keep doubling both sleep length and count.
    */
   const uint32_t busy_wait_order = 4;
   const uint32_t base_sleep_us = 10;
   const uint32_t warn_order = 12;

   (*iter)++;
   if (*iter < (1 << busy_wait_order)) {
      thrd_yield();
      return;
   }

   /* warn occasionally if we have slept at least 1.28ms for 2048 times (plus
    * another 2047 shorter sleeps)
    */
   if (unlikely(*iter % (1 << warn_order) == 0))
      vn_log(NULL, "stuck in %s wait with iter at %d", reason, *iter);

   const uint32_t shift = util_last_bit(*iter) - busy_wait_order - 1;
   os_time_sleep(base_sleep_us << shift);
}
