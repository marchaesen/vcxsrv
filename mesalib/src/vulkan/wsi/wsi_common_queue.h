/*
 * Copyright © 2016 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef VULKAN_WSI_COMMON_QUEUE_H
#define VULKAN_WSI_COMMON_QUEUE_H

#include <time.h>
#include "util/cnd_monotonic.h"
#include "util/u_vector.h"

struct wsi_queue {
   struct u_vector vector;
   mtx_t mutex;
   struct u_cnd_monotonic cond;
};

static inline int
wsi_queue_init(struct wsi_queue *queue, int length)
{
   int ret;

   if (length < 4)
      length = 4;

   ret = u_vector_init(&queue->vector, length, sizeof(uint32_t));
   if (!ret)
      return ENOMEM;

   ret = u_cnd_monotonic_init(&queue->cond);
   if (ret != thrd_success)
      goto fail_vector;

   ret = mtx_init(&queue->mutex, mtx_plain);
   if (ret != thrd_success)
      goto fail_cond;

   return 0;

fail_cond:
   u_cnd_monotonic_destroy(&queue->cond);
fail_vector:
   u_vector_finish(&queue->vector);

   return ret;
}

static inline void
wsi_queue_destroy(struct wsi_queue *queue)
{
   u_vector_finish(&queue->vector);
   mtx_destroy(&queue->mutex);
   u_cnd_monotonic_destroy(&queue->cond);
}

static inline void
wsi_queue_push(struct wsi_queue *queue, uint32_t index)
{
   uint32_t *elem;

   mtx_lock(&queue->mutex);

   if (u_vector_length(&queue->vector) == 0)
      u_cnd_monotonic_signal(&queue->cond);

   elem = u_vector_add(&queue->vector);
   *elem = index;

   mtx_unlock(&queue->mutex);
}

static inline VkResult
wsi_queue_pull(struct wsi_queue *queue, uint32_t *index, uint64_t timeout)
{
   VkResult result;
   int32_t ret;

   mtx_lock(&queue->mutex);

   struct timespec abs_timeout_ts;
   timespec_from_nsec(&abs_timeout_ts, os_time_get_absolute_timeout(timeout));

   while (u_vector_length(&queue->vector) == 0) {
      ret = u_cnd_monotonic_timedwait(&queue->cond, &queue->mutex,
                                      &abs_timeout_ts);
      if (ret == thrd_success) {
         continue;
      } else if (ret == thrd_timedout) {
         result = VK_TIMEOUT;
         goto end;
      } else {
         /* Something went badly wrong */
         result = VK_ERROR_OUT_OF_DATE_KHR;
         goto end;
      }
   }

   uint32_t *elem = u_vector_remove(&queue->vector);
   *index = *elem;
   result = VK_SUCCESS;

end:
   mtx_unlock(&queue->mutex);

   return result;
}

#endif /* VULKAN_WSI_COMMON_QUEUE_H */
