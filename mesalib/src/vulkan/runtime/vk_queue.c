/*
 * Copyright © 2021 Intel Corporation
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

#include "vk_queue.h"

#include "util/debug.h"
#include <inttypes.h>

#include "vk_alloc.h"
#include "vk_command_buffer.h"
#include "vk_common_entrypoints.h"
#include "vk_device.h"
#include "vk_fence.h"
#include "vk_log.h"
#include "vk_physical_device.h"
#include "vk_semaphore.h"
#include "vk_sync.h"
#include "vk_sync_binary.h"
#include "vk_sync_dummy.h"
#include "vk_sync_timeline.h"
#include "vk_util.h"

#include "vulkan/wsi/wsi_common.h"

VkResult
vk_queue_init(struct vk_queue *queue, struct vk_device *device,
              const VkDeviceQueueCreateInfo *pCreateInfo,
              uint32_t index_in_family)
{
   VkResult result = VK_SUCCESS;
   int ret;

   memset(queue, 0, sizeof(*queue));
   vk_object_base_init(device, &queue->base, VK_OBJECT_TYPE_QUEUE);

   list_addtail(&queue->link, &device->queues);

   queue->flags = pCreateInfo->flags;
   queue->queue_family_index = pCreateInfo->queueFamilyIndex;

   assert(index_in_family < pCreateInfo->queueCount);
   queue->index_in_family = index_in_family;

   list_inithead(&queue->submit.submits);

   ret = mtx_init(&queue->submit.mutex, mtx_plain);
   if (ret == thrd_error) {
      result = vk_errorf(queue, VK_ERROR_UNKNOWN, "mtx_init failed");
      goto fail_mutex;
   }

   ret = cnd_init(&queue->submit.push);
   if (ret == thrd_error) {
      result = vk_errorf(queue, VK_ERROR_UNKNOWN, "cnd_init failed");
      goto fail_push;
   }

   ret = cnd_init(&queue->submit.pop);
   if (ret == thrd_error) {
      result = vk_errorf(queue, VK_ERROR_UNKNOWN, "cnd_init failed");
      goto fail_pop;
   }

   util_dynarray_init(&queue->labels, NULL);
   queue->region_begin = true;

   return VK_SUCCESS;

fail_pop:
   cnd_destroy(&queue->submit.push);
fail_push:
   mtx_destroy(&queue->submit.mutex);
fail_mutex:
   return result;
}

static bool
vk_queue_has_submit_thread(struct vk_queue *queue)
{
   return queue->submit.has_thread;
}

VkResult
_vk_queue_set_lost(struct vk_queue *queue,
                   const char *file, int line,
                   const char *msg, ...)
{
   if (queue->_lost.lost)
      return VK_ERROR_DEVICE_LOST;

   queue->_lost.lost = true;
   queue->_lost.error_file = file;
   queue->_lost.error_line = line;

   va_list ap;
   va_start(ap, msg);
   vsnprintf(queue->_lost.error_msg, sizeof(queue->_lost.error_msg), msg, ap);
   va_end(ap);

   p_atomic_inc(&queue->base.device->_lost.lost);

   if (env_var_as_boolean("MESA_VK_ABORT_ON_DEVICE_LOSS", false)) {
      _vk_device_report_lost(queue->base.device);
      abort();
   }

   return VK_ERROR_DEVICE_LOST;
}

static struct vk_queue_submit *
vk_queue_submit_alloc(struct vk_queue *queue,
                      uint32_t wait_count,
                      uint32_t command_buffer_count,
                      uint32_t signal_count)
{
   VK_MULTIALLOC(ma);
   VK_MULTIALLOC_DECL(&ma, struct vk_queue_submit, submit, 1);
   VK_MULTIALLOC_DECL(&ma, struct vk_sync_wait, waits, wait_count);
   VK_MULTIALLOC_DECL(&ma, struct vk_command_buffer *, command_buffers,
                      command_buffer_count);
   VK_MULTIALLOC_DECL(&ma, struct vk_sync_signal, signals, signal_count);
   VK_MULTIALLOC_DECL(&ma, struct vk_sync *, wait_temps, wait_count);

   struct vk_sync_timeline_point **wait_points = NULL, **signal_points = NULL;
   if (queue->base.device->timeline_mode == VK_DEVICE_TIMELINE_MODE_EMULATED) {
      vk_multialloc_add(&ma, &wait_points,
                        struct vk_sync_timeline_point *, wait_count);
      vk_multialloc_add(&ma, &signal_points,
                        struct vk_sync_timeline_point *, signal_count);
   }

   if (!vk_multialloc_zalloc(&ma, &queue->base.device->alloc,
                             VK_SYSTEM_ALLOCATION_SCOPE_DEVICE))
      return NULL;

   submit->wait_count            = wait_count;
   submit->command_buffer_count  = command_buffer_count;
   submit->signal_count          = signal_count;

   submit->waits           = waits;
   submit->command_buffers = command_buffers;
   submit->signals         = signals;
   submit->_wait_temps     = wait_temps;
   submit->_wait_points    = wait_points;
   submit->_signal_points  = signal_points;

   return submit;
}

static void
vk_queue_submit_cleanup(struct vk_queue *queue,
                        struct vk_queue_submit *submit)
{
   for (uint32_t i = 0; i < submit->wait_count; i++) {
      if (submit->_wait_temps[i] != NULL)
         vk_sync_destroy(queue->base.device, submit->_wait_temps[i]);
   }

   if (submit->_mem_signal_temp != NULL)
      vk_sync_destroy(queue->base.device, submit->_mem_signal_temp);

   if (submit->_wait_points != NULL) {
      for (uint32_t i = 0; i < submit->wait_count; i++) {
         if (unlikely(submit->_wait_points[i] != NULL)) {
            vk_sync_timeline_point_release(queue->base.device,
                                           submit->_wait_points[i]);
         }
      }
   }

   if (submit->_signal_points != NULL) {
      for (uint32_t i = 0; i < submit->signal_count; i++) {
         if (unlikely(submit->_signal_points[i] != NULL)) {
            vk_sync_timeline_point_free(queue->base.device,
                                        submit->_signal_points[i]);
         }
      }
   }
}

static void
vk_queue_submit_free(struct vk_queue *queue,
                     struct vk_queue_submit *submit)
{
   vk_free(&queue->base.device->alloc, submit);
}

static void
vk_queue_submit_destroy(struct vk_queue *queue,
                        struct vk_queue_submit *submit)
{
   vk_queue_submit_cleanup(queue, submit);
   vk_queue_submit_free(queue, submit);
}

static void
vk_queue_push_submit(struct vk_queue *queue,
                     struct vk_queue_submit *submit)
{
   mtx_lock(&queue->submit.mutex);
   list_addtail(&submit->link, &queue->submit.submits);
   cnd_signal(&queue->submit.push);
   mtx_unlock(&queue->submit.mutex);
}

static VkResult
vk_queue_drain(struct vk_queue *queue)
{
   VkResult result = VK_SUCCESS;

   mtx_lock(&queue->submit.mutex);
   while (!list_is_empty(&queue->submit.submits)) {
      if (vk_device_is_lost(queue->base.device)) {
         result = VK_ERROR_DEVICE_LOST;
         break;
      }

      int ret = cnd_wait(&queue->submit.pop, &queue->submit.mutex);
      if (ret == thrd_error) {
         result = vk_queue_set_lost(queue, "cnd_wait failed");
         break;
      }
   }
   mtx_unlock(&queue->submit.mutex);

   return result;
}

static VkResult
vk_queue_submit_final(struct vk_queue *queue,
                      struct vk_queue_submit *submit)
{
   VkResult result;

   /* Now that we know all our time points exist, fetch the time point syncs
    * from any vk_sync_timelines.  While we're here, also compact down the
    * list of waits to get rid of any trivial timeline waits.
    */
   uint32_t wait_count = 0;
   for (uint32_t i = 0; i < submit->wait_count; i++) {
      /* A timeline wait on 0 is always a no-op */
      if ((submit->waits[i].sync->flags & VK_SYNC_IS_TIMELINE) &&
          submit->waits[i].wait_value == 0)
         continue;

      /* Waits on dummy vk_syncs are no-ops */
      if (vk_sync_type_is_dummy(submit->waits[i].sync->type))
         continue;

      /* For emulated timelines, we have a binary vk_sync associated with
       * each time point and pass the binary vk_sync to the driver.
       */
      struct vk_sync_timeline *timeline =
         vk_sync_as_timeline(submit->waits[i].sync);
      if (timeline) {
         assert(queue->base.device->timeline_mode ==
                VK_DEVICE_TIMELINE_MODE_EMULATED);
         result = vk_sync_timeline_get_point(queue->base.device, timeline,
                                             submit->waits[i].wait_value,
                                             &submit->_wait_points[i]);
         if (unlikely(result != VK_SUCCESS)) {
            result = vk_queue_set_lost(queue,
                                       "Time point >= %"PRIu64" not found",
                                       submit->waits[i].wait_value);
         }

         /* This can happen if the point is long past */
         if (submit->_wait_points[i] == NULL)
            continue;

         submit->waits[i].sync = &submit->_wait_points[i]->sync;
         submit->waits[i].wait_value = 0;
      }

      struct vk_sync_binary *binary =
         vk_sync_as_binary(submit->waits[i].sync);
      if (binary) {
         submit->waits[i].sync = &binary->timeline;
         submit->waits[i].wait_value = binary->next_point;
      }

      assert((submit->waits[i].sync->flags & VK_SYNC_IS_TIMELINE) ||
             submit->waits[i].wait_value == 0);

      assert(wait_count <= i);
      if (wait_count < i) {
         submit->waits[wait_count] = submit->waits[i];
         submit->_wait_temps[wait_count] = submit->_wait_temps[i];
         if (submit->_wait_points)
            submit->_wait_points[wait_count] = submit->_wait_points[i];
      }
      wait_count++;
   }

   assert(wait_count <= submit->wait_count);
   submit->wait_count = wait_count;

   for (uint32_t i = 0; i < submit->signal_count; i++) {
      assert((submit->signals[i].sync->flags & VK_SYNC_IS_TIMELINE) ||
             submit->signals[i].signal_value == 0);

      struct vk_sync_binary *binary =
         vk_sync_as_binary(submit->signals[i].sync);
      if (binary) {
         submit->signals[i].sync = &binary->timeline;
         submit->signals[i].signal_value = ++binary->next_point;
      }
   }

   result = queue->driver_submit(queue, submit);
   if (unlikely(result != VK_SUCCESS))
      return result;

   if (submit->_signal_points) {
      for (uint32_t i = 0; i < submit->signal_count; i++) {
         if (submit->_signal_points[i] == NULL)
            continue;

         vk_sync_timeline_point_install(queue->base.device,
                                        submit->_signal_points[i]);
         submit->_signal_points[i] = NULL;
      }
   }

   return VK_SUCCESS;
}

VkResult
vk_queue_flush(struct vk_queue *queue, uint32_t *submit_count_out)
{
   VkResult result = VK_SUCCESS;

   assert(queue->base.device->timeline_mode ==
          VK_DEVICE_TIMELINE_MODE_EMULATED);

   mtx_lock(&queue->submit.mutex);

   uint32_t submit_count = 0;
   while (!list_is_empty(&queue->submit.submits)) {
      struct vk_queue_submit *submit =
         list_first_entry(&queue->submit.submits,
                          struct vk_queue_submit, link);

      for (uint32_t i = 0; i < submit->wait_count; i++) {
         /* In emulated timeline mode, only emulated timelines are allowed */
         if (!vk_sync_type_is_vk_sync_timeline(submit->waits[i].sync->type)) {
            assert(!(submit->waits[i].sync->flags & VK_SYNC_IS_TIMELINE));
            continue;
         }

         result = vk_sync_wait(queue->base.device,
                               submit->waits[i].sync,
                               submit->waits[i].wait_value,
                               VK_SYNC_WAIT_PENDING, 0);
         if (result == VK_TIMEOUT) {
            /* This one's not ready yet */
            result = VK_SUCCESS;
            goto done;
         } else if (result != VK_SUCCESS) {
            result = vk_queue_set_lost(queue, "Wait for time points failed");
            goto done;
         }
      }

      result = vk_queue_submit_final(queue, submit);
      if (unlikely(result != VK_SUCCESS)) {
         result = vk_queue_set_lost(queue, "queue::driver_submit failed");
         goto done;
      }

      submit_count++;

      list_del(&submit->link);

      vk_queue_submit_destroy(queue, submit);
   }

done:
   if (submit_count)
      cnd_broadcast(&queue->submit.pop);

   mtx_unlock(&queue->submit.mutex);

   if (submit_count_out)
      *submit_count_out = submit_count;

   return result;
}

static int
vk_queue_submit_thread_func(void *_data)
{
   struct vk_queue *queue = _data;
   VkResult result;

   assert(queue->base.device->timeline_mode ==
          VK_DEVICE_TIMELINE_MODE_ASSISTED);

   mtx_lock(&queue->submit.mutex);

   while (queue->submit.thread_run) {
      if (list_is_empty(&queue->submit.submits)) {
         int ret = cnd_wait(&queue->submit.push, &queue->submit.mutex);
         if (ret == thrd_error) {
            mtx_unlock(&queue->submit.mutex);
            vk_queue_set_lost(queue, "cnd_wait failed");
            return 1;
         }
         continue;
      }

      struct vk_queue_submit *submit =
         list_first_entry(&queue->submit.submits,
                          struct vk_queue_submit, link);

      /* Drop the lock while we wait */
      mtx_unlock(&queue->submit.mutex);

      result = vk_sync_wait_many(queue->base.device,
                                 submit->wait_count, submit->waits,
                                 VK_SYNC_WAIT_PENDING, UINT64_MAX);
      if (unlikely(result != VK_SUCCESS)) {
         vk_queue_set_lost(queue, "Wait for time points failed");
         return 1;
      }

      result = vk_queue_submit_final(queue, submit);
      if (unlikely(result != VK_SUCCESS)) {
         vk_queue_set_lost(queue, "queue::driver_submit failed");
         return 1;
      }

      /* Do all our cleanup of individual fences etc. outside the lock.
       * We can't actually remove it from the list yet.  We have to do
       * that under the lock.
       */
      vk_queue_submit_cleanup(queue, submit);

      mtx_lock(&queue->submit.mutex);

      /* Only remove the submit from from the list and free it after
       * queue->submit() has completed.  This ensures that, when
       * vk_queue_drain() completes, there are no more pending jobs.
       */
      list_del(&submit->link);
      vk_queue_submit_free(queue, submit);

      cnd_broadcast(&queue->submit.pop);
   }

   mtx_unlock(&queue->submit.mutex);
   return 0;
}

static VkResult
vk_queue_enable_submit_thread(struct vk_queue *queue)
{
   int ret;

   queue->submit.thread_run = true;

   ret = thrd_create(&queue->submit.thread,
                     vk_queue_submit_thread_func,
                     queue);
   if (ret == thrd_error)
      return vk_errorf(queue, VK_ERROR_UNKNOWN, "thrd_create failed");

   queue->submit.has_thread = true;

   return VK_SUCCESS;
}

static void
vk_queue_disable_submit_thread(struct vk_queue *queue)
{
   vk_queue_drain(queue);

   /* Kick the thread to disable it */
   mtx_lock(&queue->submit.mutex);
   queue->submit.thread_run = false;
   cnd_signal(&queue->submit.push);
   mtx_unlock(&queue->submit.mutex);

   thrd_join(queue->submit.thread, NULL);

   queue->submit.has_thread = false;
}

static VkResult
vk_queue_submit(struct vk_queue *queue,
                const VkSubmitInfo2KHR *info,
                struct vk_fence *fence)
{
   VkResult result;

   const struct wsi_memory_signal_submit_info *mem_signal =
      vk_find_struct_const(info->pNext, WSI_MEMORY_SIGNAL_SUBMIT_INFO_MESA);
   bool signal_mem_sync = mem_signal != NULL &&
                          mem_signal->memory != VK_NULL_HANDLE &&
                          queue->base.device->create_sync_for_memory != NULL;

   struct vk_queue_submit *submit =
      vk_queue_submit_alloc(queue, info->waitSemaphoreInfoCount,
                            info->commandBufferInfoCount,
                            info->signalSemaphoreInfoCount +
                            signal_mem_sync + (fence != NULL));
   if (unlikely(submit == NULL))
      return vk_error(queue, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* From the Vulkan 1.2.194 spec:
    *
    *    "If the VkSubmitInfo::pNext chain does not include this structure,
    *    the batch defaults to use counter pass index 0."
    */
   const VkPerformanceQuerySubmitInfoKHR *perf_info =
      vk_find_struct_const(info->pNext, PERFORMANCE_QUERY_SUBMIT_INFO_KHR);
   submit->perf_pass_index = perf_info ? perf_info->counterPassIndex : 0;

   bool has_binary_permanent_semaphore_wait = false;
   for (uint32_t i = 0; i < info->waitSemaphoreInfoCount; i++) {
      VK_FROM_HANDLE(vk_semaphore, semaphore,
                     info->pWaitSemaphoreInfos[i].semaphore);

      /* From the Vulkan 1.2.194 spec:
       *
       *    "Applications can import a semaphore payload into an existing
       *    semaphore using an external semaphore handle. The effects of the
       *    import operation will be either temporary or permanent, as
       *    specified by the application. If the import is temporary, the
       *    implementation must restore the semaphore to its prior permanent
       *    state after submitting the next semaphore wait operation."
       *
       * and
       *
       *    VUID-VkImportSemaphoreFdInfoKHR-flags-03323
       *
       *    "If flags contains VK_SEMAPHORE_IMPORT_TEMPORARY_BIT, the
       *    VkSemaphoreTypeCreateInfo::semaphoreType field of the semaphore
       *    from which handle or name was exported must not be
       *    VK_SEMAPHORE_TYPE_TIMELINE"
       */
      struct vk_sync *sync;
      if (semaphore->temporary) {
         assert(semaphore->type == VK_SEMAPHORE_TYPE_BINARY);
         sync = submit->_wait_temps[i] = semaphore->temporary;
         semaphore->temporary = NULL;
      } else {
         if (semaphore->type == VK_SEMAPHORE_TYPE_BINARY) {
            if (queue->base.device->timeline_mode ==
                VK_DEVICE_TIMELINE_MODE_ASSISTED)
               assert(semaphore->permanent.type->move);
            has_binary_permanent_semaphore_wait = true;
         }

         sync = &semaphore->permanent;
      }

      uint32_t wait_value = semaphore->type == VK_SEMAPHORE_TYPE_TIMELINE ?
                            info->pWaitSemaphoreInfos[i].value : 0;

      submit->waits[i] = (struct vk_sync_wait) {
         .sync = sync,
         .stage_mask = info->pWaitSemaphoreInfos[i].stageMask,
         .wait_value = wait_value,
      };
   }

   for (uint32_t i = 0; i < info->commandBufferInfoCount; i++) {
      VK_FROM_HANDLE(vk_command_buffer, cmd_buffer,
                     info->pCommandBufferInfos[i].commandBuffer);
      assert(info->pCommandBufferInfos[i].deviceMask == 0 ||
             info->pCommandBufferInfos[i].deviceMask == 1);
      submit->command_buffers[i] = cmd_buffer;
   }

   for (uint32_t i = 0; i < info->signalSemaphoreInfoCount; i++) {
      VK_FROM_HANDLE(vk_semaphore, semaphore,
                     info->pSignalSemaphoreInfos[i].semaphore);

      struct vk_sync *sync = vk_semaphore_get_active_sync(semaphore);
      uint32_t signal_value = info->pSignalSemaphoreInfos[i].value;
      if (semaphore->type == VK_SEMAPHORE_TYPE_TIMELINE) {
         if (signal_value == 0) {
            result = vk_queue_set_lost(queue,
               "Tried to signal a timeline with value 0");
            goto fail;
         }
      } else {
         signal_value = 0;
      }

      /* For emulated timelines, we need to associate a binary vk_sync with
       * each time point and pass the binary vk_sync to the driver.  We could
       * do this in vk_queue_submit_final but it might require doing memory
       * allocation and we don't want to to add extra failure paths there.
       * Instead, allocate and replace the driver-visible vk_sync now and
       * we'll insert it into the timeline in vk_queue_submit_final.  The
       * insert step is guaranteed to not fail.
       */
      struct vk_sync_timeline *timeline = vk_sync_as_timeline(sync);
      if (timeline) {
         assert(queue->base.device->timeline_mode ==
                VK_DEVICE_TIMELINE_MODE_EMULATED);
         result = vk_sync_timeline_alloc_point(queue->base.device, timeline,
                                               signal_value,
                                               &submit->_signal_points[i]);
         if (unlikely(result != VK_SUCCESS))
            goto fail;

         sync = &submit->_signal_points[i]->sync;
         signal_value = 0;
      }

      submit->signals[i] = (struct vk_sync_signal) {
         .sync = sync,
         .stage_mask = info->pSignalSemaphoreInfos[i].stageMask,
         .signal_value = signal_value,
      };
   }

   uint32_t signal_count = info->signalSemaphoreInfoCount;
   if (signal_mem_sync) {
      struct vk_sync *mem_sync;
      result = queue->base.device->create_sync_for_memory(queue->base.device,
                                                          mem_signal->memory,
                                                          true, &mem_sync);
      if (unlikely(result != VK_SUCCESS))
         goto fail;

      submit->_mem_signal_temp = mem_sync;

      assert(submit->signals[signal_count].sync == NULL);
      submit->signals[signal_count++] = (struct vk_sync_signal) {
         .sync = mem_sync,
         .stage_mask = ~(VkPipelineStageFlags2KHR)0,
      };
   }

   if (fence != NULL) {
      assert(submit->signals[signal_count].sync == NULL);
      submit->signals[signal_count++] = (struct vk_sync_signal) {
         .sync = vk_fence_get_active_sync(fence),
         .stage_mask = ~(VkPipelineStageFlags2KHR)0,
      };
   }

   assert(signal_count == submit->signal_count);

   switch (queue->base.device->timeline_mode) {
   case VK_DEVICE_TIMELINE_MODE_ASSISTED:
      if (!vk_queue_has_submit_thread(queue)) {
         static int force_submit_thread = -1;
         if (unlikely(force_submit_thread < 0)) {
            force_submit_thread =
               env_var_as_boolean("MESA_VK_ENABLE_SUBMIT_THREAD", false);
         }

         if (unlikely(force_submit_thread)) {
            result = vk_queue_enable_submit_thread(queue);
         } else {
            /* Otherwise, only enable the submit thread if we need it in order
             * to resolve timeline semaphore wait-before-signal issues.
             */
            result = vk_sync_wait_many(queue->base.device,
                                       submit->wait_count, submit->waits,
                                       VK_SYNC_WAIT_PENDING, 0);
            if (result == VK_TIMEOUT)
               result = vk_queue_enable_submit_thread(queue);
         }
         if (unlikely(result != VK_SUCCESS))
            goto fail;
      }

      if (vk_queue_has_submit_thread(queue)) {
         if (has_binary_permanent_semaphore_wait) {
            for (uint32_t i = 0; i < info->waitSemaphoreInfoCount; i++) {
               VK_FROM_HANDLE(vk_semaphore, semaphore,
                              info->pWaitSemaphoreInfos[i].semaphore);

               if (semaphore->type != VK_SEMAPHORE_TYPE_BINARY)
                  continue;

               /* From the Vulkan 1.2.194 spec:
                *
                *    "When a batch is submitted to a queue via a queue
                *    submission, and it includes semaphores to be waited on,
                *    it defines a memory dependency between prior semaphore
                *    signal operations and the batch, and defines semaphore
                *    wait operations.
                *
                *    Such semaphore wait operations set the semaphores
                *    created with a VkSemaphoreType of
                *    VK_SEMAPHORE_TYPE_BINARY to the unsignaled state."
                *
                * For threaded submit, we depend on tracking the unsignaled
                * state of binary semaphores to determine when we can safely
                * submit.  The VK_SYNC_WAIT_PENDING check above as well as the
                * one in the sumbit thread depend on all binary semaphores
                * being reset when they're not in active use from the point
                * of view of the client's CPU timeline.  This means we need to
                * reset them inside vkQueueSubmit and cannot wait until the
                * actual submit which happens later in the thread.
                *
                * We've already stolen temporary semaphore payloads above as
                * part of basic semaphore processing.  We steal permanent
                * semaphore payloads here by way of vk_sync_move.  For shared
                * semaphores, this can be a bit expensive (sync file import
                * and export) but, for non-shared semaphores, it can be made
                * fairly cheap.  Also, we only do this semaphore swapping in
                * the case where you have real timelines AND the client is
                * using timeline semaphores with wait-before-signal (that's
                * the only way to get a submit thread) AND mixing those with
                * waits on binary semaphores AND said binary semaphore is
                * using its permanent payload.  In other words, this code
                * should basically only ever get executed in CTS tests.
                */
               if (submit->_wait_temps[i] != NULL)
                  continue;

               assert(submit->waits[i].sync == &semaphore->permanent);

               /* From the Vulkan 1.2.194 spec:
                *
                *    VUID-vkQueueSubmit-pWaitSemaphores-03238
                *
                *    "All elements of the pWaitSemaphores member of all
                *    elements of pSubmits created with a VkSemaphoreType of
                *    VK_SEMAPHORE_TYPE_BINARY must reference a semaphore
                *    signal operation that has been submitted for execution
                *    and any semaphore signal operations on which it depends
                *    (if any) must have also been submitted for execution."
                *
                * Therefore, we can safely do a blocking wait here and it
                * won't actually block for long.  This ensures that the
                * vk_sync_move below will succeed.
                */
               result = vk_sync_wait(queue->base.device,
                                     submit->waits[i].sync, 0,
                                     VK_SYNC_WAIT_PENDING, UINT64_MAX);
               if (unlikely(result != VK_SUCCESS))
                  goto fail;

               result = vk_sync_create(queue->base.device,
                                       semaphore->permanent.type,
                                       0 /* flags */,
                                       0 /* initial value */,
                                       &submit->_wait_temps[i]);
               if (unlikely(result != VK_SUCCESS))
                  goto fail;

               result = vk_sync_move(queue->base.device,
                                     submit->_wait_temps[i],
                                     &semaphore->permanent);
               if (unlikely(result != VK_SUCCESS))
                  goto fail;

               submit->waits[i].sync = submit->_wait_temps[i];
            }
         }

         vk_queue_push_submit(queue, submit);

         if (signal_mem_sync) {
            /* If we're signaling a memory object, we have to ensure that
             * vkQueueSubmit does not return until the kernel submission has
             * happened.  Otherwise, we may get a race between this process
             * and whatever is going to wait on the object where the other
             * process may wait before we've submitted our work.  Drain the
             * queue now to avoid this.  It's the responsibility of the caller
             * to ensure that any vkQueueSubmit which signals a memory object
             * has fully resolved dependencies.
             */
            result = vk_queue_drain(queue);
            if (unlikely(result != VK_SUCCESS))
               return result;
         }

         return VK_SUCCESS;
      } else {
         result = vk_queue_submit_final(queue, submit);
         if (unlikely(result != VK_SUCCESS))
            goto fail;

         /* If we don't have a submit thread, we can more directly ensure
          * that binary semaphore payloads get reset.  If we also signal the
          * vk_sync, then we can consider it to have been both reset and
          * signaled.  A reset in this case would be wrong because it would
          * throw away our signal operation.  If we don't signal the vk_sync,
          * then we need to reset it.
          */
         if (has_binary_permanent_semaphore_wait) {
            for (uint32_t i = 0; i < submit->wait_count; i++) {
               if ((submit->waits[i].sync->flags & VK_SYNC_IS_TIMELINE) ||
                   submit->_wait_temps[i] != NULL)
                  continue;

               bool was_signaled = false;
               for (uint32_t j = 0; j < submit->signal_count; j++) {
                  if (submit->signals[j].sync == submit->waits[i].sync) {
                     was_signaled = true;
                     break;
                  }
               }

               if (!was_signaled) {
                  result = vk_sync_reset(queue->base.device,
                                         submit->waits[i].sync);
                  if (unlikely(result != VK_SUCCESS))
                     goto fail;
               }
            }
         }

         vk_queue_submit_destroy(queue, submit);
         return VK_SUCCESS;
      }
      unreachable("Should have returned");

   case VK_DEVICE_TIMELINE_MODE_EMULATED:
      vk_queue_push_submit(queue, submit);
      return vk_device_flush(queue->base.device);

   case VK_DEVICE_TIMELINE_MODE_NONE:
   case VK_DEVICE_TIMELINE_MODE_NATIVE:
      result = vk_queue_submit_final(queue, submit);
      vk_queue_submit_destroy(queue, submit);
      return result;
   }
   unreachable("Invalid timeline mode");

fail:
   vk_queue_submit_destroy(queue, submit);
   return result;
}

VkResult
vk_queue_wait_before_present(struct vk_queue *queue,
                             const VkPresentInfoKHR *pPresentInfo)
{
   if (vk_device_is_lost(queue->base.device))
      return VK_ERROR_DEVICE_LOST;

   /* From the Vulkan 1.2.194 spec:
    *
    *    VUID-vkQueuePresentKHR-pWaitSemaphores-03268
    *
    *    "All elements of the pWaitSemaphores member of pPresentInfo must
    *    reference a semaphore signal operation that has been submitted for
    *    execution and any semaphore signal operations on which it depends (if
    *    any) must have also been submitted for execution."
    *
    * As with vkQueueSubmit above, we need to ensure that any binary
    * semaphores we use in this present actually exist.  If we don't have
    * timeline semaphores, this is a non-issue.  If they're emulated, then
    * this is ensured for us by the vk_device_flush() at the end of every
    * vkQueueSubmit() and every vkSignalSemaphore().  For real timeline
    * semaphores, however, we need to do a wait.  Thanks to the above bit of
    * spec text, that wait should never block for long.
    */
   if (queue->base.device->timeline_mode != VK_DEVICE_TIMELINE_MODE_ASSISTED)
      return VK_SUCCESS;

   const uint32_t wait_count = pPresentInfo->waitSemaphoreCount;
   STACK_ARRAY(struct vk_sync_wait, waits, wait_count);

   for (uint32_t i = 0; i < wait_count; i++) {
      VK_FROM_HANDLE(vk_semaphore, semaphore,
                     pPresentInfo->pWaitSemaphores[i]);

      /* From the Vulkan 1.2.194 spec:
       *
       *    VUID-vkQueuePresentKHR-pWaitSemaphores-03267
       *
       *    "All elements of the pWaitSemaphores member of pPresentInfo must
       *    be created with a VkSemaphoreType of VK_SEMAPHORE_TYPE_BINARY."
       */
      assert(semaphore->type == VK_SEMAPHORE_TYPE_BINARY);

      waits[i] = (struct vk_sync_wait) {
         .sync = vk_semaphore_get_active_sync(semaphore),
         .stage_mask = ~(VkPipelineStageFlags2KHR)0,
      };
   }

   VkResult result = vk_sync_wait_many(queue->base.device, wait_count, waits,
                                       VK_SYNC_WAIT_PENDING, UINT64_MAX);

   STACK_ARRAY_FINISH(waits);

   /* Check again, just in case */
   if (vk_device_is_lost(queue->base.device))
      return VK_ERROR_DEVICE_LOST;

   return result;
}

static VkResult
vk_queue_signal_sync(struct vk_queue *queue,
                     struct vk_sync *sync,
                     uint32_t signal_value)
{
   struct vk_queue_submit *submit = vk_queue_submit_alloc(queue, 0, 0, 1);
   if (unlikely(submit == NULL))
      return vk_error(queue, VK_ERROR_OUT_OF_HOST_MEMORY);

   submit->signals[0] = (struct vk_sync_signal) {
      .sync = sync,
      .stage_mask = ~(VkPipelineStageFlags2KHR)0,
      .signal_value = signal_value,
   };

   VkResult result;
   switch (queue->base.device->timeline_mode) {
   case VK_DEVICE_TIMELINE_MODE_ASSISTED:
      if (vk_queue_has_submit_thread(queue)) {
         vk_queue_push_submit(queue, submit);
         return VK_SUCCESS;
      } else {
         result = vk_queue_submit_final(queue, submit);
         vk_queue_submit_destroy(queue, submit);
         return result;
      }

   case VK_DEVICE_TIMELINE_MODE_EMULATED:
      vk_queue_push_submit(queue, submit);
      return vk_device_flush(queue->base.device);

   case VK_DEVICE_TIMELINE_MODE_NONE:
   case VK_DEVICE_TIMELINE_MODE_NATIVE:
      result = vk_queue_submit_final(queue, submit);
      vk_queue_submit_destroy(queue, submit);
      return result;
   }
   unreachable("Invalid timeline mode");
}

void
vk_queue_finish(struct vk_queue *queue)
{
   if (vk_queue_has_submit_thread(queue))
      vk_queue_disable_submit_thread(queue);

   while (!list_is_empty(&queue->submit.submits)) {
      assert(vk_device_is_lost_no_report(queue->base.device));

      struct vk_queue_submit *submit =
         list_first_entry(&queue->submit.submits,
                          struct vk_queue_submit, link);

      list_del(&submit->link);
      vk_queue_submit_destroy(queue, submit);
   }

   cnd_destroy(&queue->submit.pop);
   cnd_destroy(&queue->submit.push);
   mtx_destroy(&queue->submit.mutex);

   util_dynarray_fini(&queue->labels);
   list_del(&queue->link);
   vk_object_base_finish(&queue->base);
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_QueueSubmit2KHR(VkQueue _queue,
                          uint32_t submitCount,
                          const VkSubmitInfo2KHR *pSubmits,
                          VkFence _fence)
{
   VK_FROM_HANDLE(vk_queue, queue, _queue);
   VK_FROM_HANDLE(vk_fence, fence, _fence);

   if (vk_device_is_lost(queue->base.device))
      return VK_ERROR_DEVICE_LOST;

   if (submitCount == 0) {
      if (fence == NULL) {
         return VK_SUCCESS;
      } else {
         return vk_queue_signal_sync(queue, vk_fence_get_active_sync(fence), 0);
      }
   }

   for (uint32_t i = 0; i < submitCount; i++) {
      VkResult result = vk_queue_submit(queue, &pSubmits[i],
                                        i == submitCount - 1 ? fence : NULL);
      if (unlikely(result != VK_SUCCESS))
         return result;
   }

   return VK_SUCCESS;
}

static const struct vk_sync_type *
get_cpu_wait_type(struct vk_physical_device *pdevice)
{
   for (const struct vk_sync_type *const *t =
        pdevice->supported_sync_types; *t; t++) {
      if (((*t)->features & VK_SYNC_FEATURE_BINARY) &&
          ((*t)->features & VK_SYNC_FEATURE_CPU_WAIT))
         return *t;
   }

   unreachable("You must have a non-timeline CPU wait sync type");
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_QueueWaitIdle(VkQueue _queue)
{
   VK_FROM_HANDLE(vk_queue, queue, _queue);
   VkResult result;

   if (vk_device_is_lost(queue->base.device))
      return VK_ERROR_DEVICE_LOST;

   const struct vk_sync_type *sync_type =
      get_cpu_wait_type(queue->base.device->physical);

   struct vk_sync *sync;
   result = vk_sync_create(queue->base.device, sync_type, 0, 0, &sync);
   if (unlikely(result != VK_SUCCESS))
      return result;

   result = vk_queue_signal_sync(queue, sync, 0);
   if (unlikely(result != VK_SUCCESS))
      return result;

   result = vk_sync_wait(queue->base.device, sync, 0,
                         VK_SYNC_WAIT_COMPLETE, UINT64_MAX);

   vk_sync_destroy(queue->base.device, sync);

   VkResult device_status = vk_device_check_status(queue->base.device);
   if (device_status != VK_SUCCESS)
      return device_status;

   return result;
}
