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
#include "util/u_debug.h"
#include "venus-protocol/vn_protocol_driver_info.h"
#include "vk_enum_to_str.h"

#define VN_RELAX_MIN_BASE_SLEEP_US (160)

static const struct debug_control vn_debug_options[] = {
   /* clang-format off */
   { "init", VN_DEBUG_INIT },
   { "result", VN_DEBUG_RESULT },
   { "vtest", VN_DEBUG_VTEST },
   { "wsi", VN_DEBUG_WSI },
   { "no_abort", VN_DEBUG_NO_ABORT },
   { NULL, 0 },
   /* clang-format on */
};

static const struct debug_control vn_perf_options[] = {
   /* clang-format off */
   { "no_async_set_alloc", VN_PERF_NO_ASYNC_SET_ALLOC },
   { "no_async_buffer_create", VN_PERF_NO_ASYNC_BUFFER_CREATE },
   { "no_async_queue_submit", VN_PERF_NO_ASYNC_QUEUE_SUBMIT },
   { "no_event_feedback", VN_PERF_NO_EVENT_FEEDBACK },
   { "no_fence_feedback", VN_PERF_NO_FENCE_FEEDBACK },
   { NULL, 0 },
   /* clang-format on */
};

struct vn_env vn_env;

static void
vn_env_init_once(void)
{
   vn_env.debug =
      parse_debug_string(os_get_option("VN_DEBUG"), vn_debug_options);
   vn_env.perf =
      parse_debug_string(os_get_option("VN_PERF"), vn_perf_options);
   vn_env.draw_cmd_batch_limit =
      debug_get_num_option("VN_DRAW_CMD_BATCH_LIMIT", UINT32_MAX);
   if (!vn_env.draw_cmd_batch_limit)
      vn_env.draw_cmd_batch_limit = UINT32_MAX;
   vn_env.relax_base_sleep_us = debug_get_num_option(
      "VN_RELAX_BASE_SLEEP_US", VN_RELAX_MIN_BASE_SLEEP_US);
}

void
vn_env_init(void)
{
   static once_flag once = ONCE_FLAG_INIT;
   call_once(&once, vn_env_init_once);

   /* log per VkInstance creation */
   if (VN_DEBUG(INIT)) {
      vn_log(NULL,
             "vn_env is as below:"
             "\n\tdebug = 0x%" PRIx64 ""
             "\n\tperf = 0x%" PRIx64 ""
             "\n\tdraw_cmd_batch_limit = %u"
             "\n\trelax_base_sleep_us = %u",
             vn_env.debug, vn_env.perf, vn_env.draw_cmd_batch_limit,
             vn_env.relax_base_sleep_us);
   }
}

void
vn_trace_init(void)
{
#ifdef ANDROID
   atrace_init();
#endif
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

uint32_t
vn_extension_get_spec_version(const char *name)
{
   const int32_t index = vn_info_extension_index(name);
   return index >= 0 ? vn_info_extension_get(index)->spec_version : 0;
}

void
vn_relax(uint32_t *iter, const char *reason)
{
   /* Yield for the first 2^busy_wait_order times and then sleep for
    * base_sleep_us microseconds for the same number of times.  After that,
    * keep doubling both sleep length and count.
    */
   const uint32_t busy_wait_order = 10;
   const uint32_t base_sleep_us = vn_env.relax_base_sleep_us;
   const uint32_t warn_order = 14;
   const uint32_t abort_order = 16;

   (*iter)++;
   if (*iter < (1 << busy_wait_order)) {
      thrd_yield();
      return;
   }

   /* warn occasionally if we have slept at least 1.28ms for 8192 times (plus
    * another 8191 shorter sleeps)
    */
   if (unlikely(*iter % (1 << warn_order) == 0)) {
      vn_log(NULL, "stuck in %s wait with iter at %d", reason, *iter);

      if (*iter >= (1 << abort_order) && !VN_DEBUG(NO_ABORT)) {
         vn_log(NULL, "aborting");
         abort();
      }
   }

   const uint32_t shift = util_last_bit(*iter) - busy_wait_order - 1;
   os_time_sleep(base_sleep_us << shift);
}
