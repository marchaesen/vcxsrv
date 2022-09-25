/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <unistd.h>
#include <poll.h>
#include <vulkan/vulkan.h>

#include "pvr_private.h"
#include "pvr_srv.h"
#include "pvr_srv_sync.h"
#include "util/libsync.h"
#include "util/macros.h"
#include "util/timespec.h"
#include "vk_alloc.h"
#include "vk_log.h"
#include "vk_sync.h"
#include "vk_util.h"

static VkResult pvr_srv_sync_init(struct vk_device *device,
                                  struct vk_sync *sync,
                                  uint64_t initial_value)
{
   struct pvr_srv_sync *srv_sync = to_srv_sync(sync);

   srv_sync->signaled = initial_value ? true : false;
   srv_sync->fd = -1;

   return VK_SUCCESS;
}

void pvr_srv_sync_finish(struct vk_device *device, struct vk_sync *sync)
{
   struct pvr_srv_sync *srv_sync = to_srv_sync(sync);

   if (srv_sync->fd != -1)
      close(srv_sync->fd);
}

/* Note: function closes the fd. */
static void pvr_set_sync_state(struct pvr_srv_sync *srv_sync, bool signaled)
{
   if (srv_sync->fd != -1) {
      close(srv_sync->fd);
      srv_sync->fd = -1;
   }

   srv_sync->signaled = signaled;
}

void pvr_srv_set_sync_payload(struct pvr_srv_sync *srv_sync, int payload)
{
   if (srv_sync->fd != -1)
      close(srv_sync->fd);

   srv_sync->fd = payload;
   srv_sync->signaled = (payload == -1);
}

static VkResult pvr_srv_sync_signal(struct vk_device *device,
                                    struct vk_sync *sync,
                                    UNUSED uint64_t value)
{
   struct pvr_srv_sync *srv_sync = to_srv_sync(sync);

   pvr_set_sync_state(srv_sync, true);

   return VK_SUCCESS;
}

static VkResult pvr_srv_sync_reset(struct vk_device *device,
                                   struct vk_sync *sync)
{
   struct pvr_srv_sync *srv_sync = to_srv_sync(sync);

   pvr_set_sync_state(srv_sync, false);

   return VK_SUCCESS;
}

/* Careful, timeout might overflow. */
static inline void pvr_start_timeout(struct timespec *timeout,
                                     uint64_t timeout_ns)
{
   clock_gettime(CLOCK_MONOTONIC, timeout);
   timespec_add_nsec(timeout, timeout, timeout_ns);
}

/* Careful, a negative value might be returned. */
static inline struct timespec
pvr_get_remaining_time(const struct timespec *timeout)
{
   struct timespec time;

   clock_gettime(CLOCK_MONOTONIC, &time);
   timespec_sub(&time, timeout, &time);

   return time;
}

/* abs_timeout_ns == 0 -> Get status without waiting.
 * abs_timeout_ns == ~0 -> Wait infinitely.
 * else wait for the given abs_timeout_ns in nanoseconds. */
static VkResult pvr_srv_sync_wait_many(struct vk_device *device,
                                       uint32_t wait_count,
                                       const struct vk_sync_wait *waits,
                                       enum vk_sync_wait_flags wait_flags,
                                       uint64_t abs_timeout_ns)
{
   uint32_t unsignaled_count = 0U;
   struct timespec end_time;
   VkResult result;
   int ppoll_ret;

   STACK_ARRAY(struct pollfd, poll_fds, wait_count);
   if (!poll_fds)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (abs_timeout_ns != 0UL && abs_timeout_ns != ~0) {
      /* Syncobj timeouts are signed. */
      abs_timeout_ns = MIN2(abs_timeout_ns, (uint64_t)INT64_MAX);
      pvr_start_timeout(&end_time, abs_timeout_ns);
   }

   for (uint32_t i = 0U; i < wait_count; i++) {
      struct pvr_srv_sync *srv_sync = to_srv_sync(waits[i].sync);

      /* -1 in case if fence is signaled or uninitialized, ppoll will skip the
       * fence.
       */
      /* FIXME: We don't currently support wait-for-fd path, so the caller
       * should make sure all the sync have been assigned before calling this
       * function.
       */
      if (srv_sync->signaled || srv_sync->fd == -1) {
         poll_fds[i].fd = -1;
      } else {
         poll_fds[i].fd = srv_sync->fd;
         unsignaled_count++;
      }

      poll_fds[i].events = POLLIN;
      poll_fds[i].revents = 0U;
   }

   if (unsignaled_count == 0U) {
      result = VK_SUCCESS;
      goto end_wait_for_fences;
   }

   /* FIXME: Fix device loss handling. */
   do {
      if (abs_timeout_ns == ~0) {
         ppoll_ret = ppoll(poll_fds, wait_count, NULL, NULL);
      } else {
         struct timespec remaining_time;

         if (abs_timeout_ns == 0U) {
            remaining_time = (struct timespec){ 0UL, 0UL };
         } else {
            /* ppoll() returns EINVAL on negative timeout. Nothing to worry.
             */
            remaining_time = pvr_get_remaining_time(&end_time);
         }

         ppoll_ret = ppoll(poll_fds, wait_count, &remaining_time, NULL);
      }

      if (ppoll_ret > 0U) {
         /* ppoll_ret contains the amount of structs updated by poll(). */
         unsignaled_count -= ppoll_ret;

         /* ppoll_ret > 0 is for early loop termination. */
         for (uint32_t i = 0; ppoll_ret > 0 && i < wait_count; i++) {
            if (poll_fds[i].revents == 0)
               continue;

            if (poll_fds[i].revents & (POLLNVAL | POLLERR)) {
               result = vk_error(NULL, VK_ERROR_DEVICE_LOST);
               goto end_wait_for_fences;
            }

            pvr_srv_sync_signal(device, waits[i].sync, 0U);

            if (wait_flags & VK_SYNC_WAIT_ANY) {
               result = VK_SUCCESS;
               goto end_wait_for_fences;
            }

            /* -1 makes ppoll ignore it and set revents to 0. */
            poll_fds[i].fd = -1;
            ppoll_ret--;
         }

         /* For zero timeout, just return even if we still have unsignaled
          * syncs.
          */
         if (abs_timeout_ns == 0U && unsignaled_count != 0U) {
            result = VK_TIMEOUT;
            goto end_wait_for_fences;
         }
      } else if (ppoll_ret == 0) {
         result = VK_TIMEOUT;
         goto end_wait_for_fences;
      }

      /* Careful as we might have decremented ppoll_ret to 0. */
   } while ((ppoll_ret != -1 && unsignaled_count != 0) ||
            (ppoll_ret == -1 && (errno == EINTR || errno == EAGAIN)));

   /* We assume device loss in case of an unknown error or invalid fd. */
   if (ppoll_ret != -1)
      result = VK_SUCCESS;
   else if (errno == EINVAL)
      result = VK_TIMEOUT;
   else if (errno == ENOMEM)
      result = vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);
   else
      result = vk_error(NULL, VK_ERROR_DEVICE_LOST);

end_wait_for_fences:
   STACK_ARRAY_FINISH(poll_fds);

   return result;
}

static VkResult pvr_srv_sync_move(struct vk_device *device,
                                  struct vk_sync *dst,
                                  struct vk_sync *src)
{
   struct pvr_srv_sync *srv_dst_sync = to_srv_sync(dst);
   struct pvr_srv_sync *srv_src_sync = to_srv_sync(src);

   if (!(dst->flags & VK_SYNC_IS_SHARED) && !(src->flags & VK_SYNC_IS_SHARED)) {
      pvr_srv_set_sync_payload(srv_dst_sync, srv_src_sync->fd);
      srv_src_sync->fd = -1;
      pvr_srv_sync_reset(device, src);
      return VK_SUCCESS;
   }

   unreachable("srv_sync doesn't support move for shared sync objects.");
   return VK_ERROR_UNKNOWN;
}

static VkResult pvr_srv_sync_import_sync_file(struct vk_device *device,
                                              struct vk_sync *sync,
                                              int sync_file)
{
   struct pvr_srv_sync *srv_sync = to_srv_sync(sync);
   int fd = -1;

   if (sync_file >= 0) {
      fd = dup(sync_file);
      if (fd < 0)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   pvr_srv_set_sync_payload(srv_sync, fd);

   return VK_SUCCESS;
}

static VkResult pvr_srv_sync_export_sync_file(struct vk_device *device,
                                              struct vk_sync *sync,
                                              int *sync_file)
{
   struct pvr_srv_sync *srv_sync = to_srv_sync(sync);
   int fd;

   if (srv_sync->fd < 0) {
      *sync_file = -1;
      return VK_SUCCESS;
   }

   fd = dup(srv_sync->fd);
   if (fd < 0)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   *sync_file = fd;

   return VK_SUCCESS;
}

const struct vk_sync_type pvr_srv_sync_type = {
   .size = sizeof(struct pvr_srv_sync),
   /* clang-format off */
   .features = VK_SYNC_FEATURE_BINARY |
               VK_SYNC_FEATURE_GPU_WAIT |
               VK_SYNC_FEATURE_GPU_MULTI_WAIT |
               VK_SYNC_FEATURE_CPU_WAIT |
               VK_SYNC_FEATURE_CPU_RESET |
               VK_SYNC_FEATURE_CPU_SIGNAL |
               VK_SYNC_FEATURE_WAIT_ANY,
   /* clang-format on */
   .init = pvr_srv_sync_init,
   .finish = pvr_srv_sync_finish,
   .signal = pvr_srv_sync_signal,
   .reset = pvr_srv_sync_reset,
   .wait_many = pvr_srv_sync_wait_many,
   .move = pvr_srv_sync_move,
   .import_sync_file = pvr_srv_sync_import_sync_file,
   .export_sync_file = pvr_srv_sync_export_sync_file,
};
