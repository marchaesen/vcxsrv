/*
 * Copyright © 2019 Raspberry Pi
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

#include "v3dv_private.h"
#include "drm-uapi/v3d_drm.h"

#include "broadcom/clif/clif_dump.h"

#include <errno.h>
#include <time.h>

static void
v3dv_clif_dump(struct v3dv_device *device,
               struct v3dv_job *job,
               struct drm_v3d_submit_cl *submit)
{
   if (!(unlikely(V3D_DEBUG & (V3D_DEBUG_CL |
                               V3D_DEBUG_CL_NO_BIN |
                               V3D_DEBUG_CLIF))))
      return;

   struct clif_dump *clif = clif_dump_init(&device->devinfo,
                                           stderr,
                                           V3D_DEBUG & (V3D_DEBUG_CL |
                                                        V3D_DEBUG_CL_NO_BIN),
                                           V3D_DEBUG & V3D_DEBUG_CL_NO_BIN);

   set_foreach(job->bos, entry) {
      struct v3dv_bo *bo = (void *)entry->key;
      char *name = ralloc_asprintf(NULL, "%s_0x%x",
                                   bo->name, bo->offset);

      bool ok = v3dv_bo_map(device, bo, bo->size);
      if (!ok) {
         fprintf(stderr, "failed to map BO for clif_dump.\n");
         ralloc_free(name);
         goto free_clif;
      }
      clif_dump_add_bo(clif, name, bo->offset, bo->size, bo->map);

      ralloc_free(name);
   }

   clif_dump(clif, submit);

 free_clif:
   clif_dump_destroy(clif);
}

static uint64_t
gettime_ns()
{
   struct timespec current;
   clock_gettime(CLOCK_MONOTONIC, &current);
   return (uint64_t)current.tv_sec * NSEC_PER_SEC + current.tv_nsec;
}

static uint64_t
get_absolute_timeout(uint64_t timeout)
{
   uint64_t current_time = gettime_ns();
   uint64_t max_timeout = (uint64_t) INT64_MAX - current_time;

   timeout = MIN2(max_timeout, timeout);

   return (current_time + timeout);
}

static VkResult
queue_submit_job(struct v3dv_queue *queue,
                 struct v3dv_job *job,
                 struct v3dv_submit_info_semaphores *sems_info,
                 pthread_t *wait_thread);

/* Waits for active CPU wait threads spawned before the current thread to
 * complete and submit all their GPU jobs.
 */
static void
cpu_queue_wait_idle(struct v3dv_queue *queue)
{
   const pthread_t this_thread = pthread_self();

retry:
   mtx_lock(&queue->mutex);
   list_for_each_entry(struct v3dv_queue_submit_wait_info, info,
                       &queue->submit_wait_list, list_link) {
      for (uint32_t  i = 0; i < info->wait_thread_count; i++) {
         if (info->wait_threads[i].finished)
            continue;

         /* Because we are testing this against the list of spawned threads
          * it will never match for the main thread, so when we call this from
          * the main thread we are effectively waiting for all active threads
          * to complete, and otherwise we are only waiting for work submitted
          * before the wait thread that called this (a wait thread should never
          * be waiting for work submitted after it).
          */
         if (info->wait_threads[i].thread == this_thread)
            goto done;

         /* Wait and try again */
         mtx_unlock(&queue->mutex);
         usleep(500); /* 0.5 ms */
         goto retry;
      }
   }

done:
   mtx_unlock(&queue->mutex);
}

static VkResult
gpu_queue_wait_idle(struct v3dv_queue *queue)
{
   struct v3dv_device *device = queue->device;
   int render_fd = device->pdevice->render_fd;
   struct v3dv_last_job_sync last_job_syncs;

   mtx_lock(&device->mutex);
   memcpy(&last_job_syncs, &device->last_job_syncs, sizeof(last_job_syncs));
   mtx_unlock(&device->mutex);

   if (device->pdevice->caps.multisync) {
      int ret = drmSyncobjWait(render_fd, (uint32_t *) &last_job_syncs.syncs,
                               3, INT64_MAX,
                               DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL, NULL);
      if (ret)
         return VK_ERROR_DEVICE_LOST;
   } else {
      int ret =
         drmSyncobjWait(render_fd, &last_job_syncs.syncs[V3DV_QUEUE_ANY], 1,
                        INT64_MAX, 0, NULL);
      if (ret)
         return VK_ERROR_DEVICE_LOST;
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_QueueWaitIdle(VkQueue _queue)
{
   V3DV_FROM_HANDLE(v3dv_queue, queue, _queue);

   /* Check that we don't have any wait threads running in the CPU first,
    * as these can spawn new GPU jobs.
    */
   cpu_queue_wait_idle(queue);

   /* Check we don't have any GPU jobs running */
   return gpu_queue_wait_idle(queue);
}

static VkResult
handle_reset_query_cpu_job(struct v3dv_job *job)
{
   struct v3dv_reset_query_cpu_job_info *info = &job->cpu.query_reset;
   assert(info->pool);

   /* We are about to reset query counters so we need to make sure that
    * The GPU is not using them. The exception is timestamp queries, since
    * we handle those in the CPU.
    *
    * FIXME: we could avoid blocking the main thread for this if we use
    *        submission thread.
    */
   if (info->pool->query_type == VK_QUERY_TYPE_OCCLUSION)
         v3dv_bo_wait(job->device, info->pool->bo, PIPE_TIMEOUT_INFINITE);

   v3dv_reset_query_pools(job->device, info->pool, info->first, info->count);

   return VK_SUCCESS;
}

static VkResult
handle_end_query_cpu_job(struct v3dv_job *job)
{
   struct v3dv_end_query_cpu_job_info *info = &job->cpu.query_end;
   for (uint32_t i = 0; i < info->count; i++) {
      assert(info->query + i < info->pool->query_count);
      struct v3dv_query *query = &info->pool->queries[info->query + i];
      query->maybe_available = true;
   }

   return VK_SUCCESS;
}

static VkResult
handle_copy_query_results_cpu_job(struct v3dv_job *job)
{
   struct v3dv_copy_query_results_cpu_job_info *info =
      &job->cpu.query_copy_results;

   assert(info->dst && info->dst->mem && info->dst->mem->bo);
   struct v3dv_bo *bo = info->dst->mem->bo;

   /* Map the entire dst buffer for the CPU copy if needed */
   assert(!bo->map || bo->map_size == bo->size);
   if (!bo->map && !v3dv_bo_map(job->device, bo, bo->size))
      return vk_error(job->device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* FIXME: if flags includes VK_QUERY_RESULT_WAIT_BIT this could trigger a
    * sync wait on the CPU for the corresponding GPU jobs to finish. We might
    * want to use a submission thread to avoid blocking on the main thread.
    */
   uint8_t *offset = ((uint8_t *) bo->map) +
                     info->offset + info->dst->mem_offset;
   v3dv_get_query_pool_results_cpu(job->device,
                                   info->pool,
                                   info->first,
                                   info->count,
                                   offset,
                                   info->stride,
                                   info->flags);

   return VK_SUCCESS;
}

static VkResult
handle_set_event_cpu_job(struct v3dv_job *job)
{
   /* From the Vulkan 1.0 spec:
    *
    *    "When vkCmdSetEvent is submitted to a queue, it defines an execution
    *     dependency on commands that were submitted before it, and defines an
    *     event signal operation which sets the event to the signaled state.
    *     The first synchronization scope includes every command previously
    *     submitted to the same queue, including those in the same command
    *     buffer and batch".
    *
    * So we should wait for all prior work to be completed before signaling
    * the event, this includes all active CPU wait threads spawned for any
    * command buffer submitted *before* this.
    *
    * FIXME: we could avoid blocking the main thread for this if we use a
    *        submission thread.
    */

   /* If we are calling this from a wait thread it will only wait
    * wait threads sspawned before it, otherwise it will wait for
    * all active threads to complete.
    */
   cpu_queue_wait_idle(&job->device->queue);

   VkResult result = gpu_queue_wait_idle(&job->device->queue);
   if (result != VK_SUCCESS)
      return result;

   struct v3dv_event_set_cpu_job_info *info = &job->cpu.event_set;
   p_atomic_set(&info->event->state, info->state);

   return VK_SUCCESS;
}

static VkResult
copy_semaphores(struct v3dv_device *device,
                VkSemaphore *sems_src, uint32_t sems_src_count,
                VkSemaphore **sems_dst, uint32_t *sems_dst_count)
{
   *sems_dst_count = sems_src_count;

   if (*sems_dst_count == 0) {
      *sems_dst = NULL;
      return VK_SUCCESS;
   }

   *sems_dst = vk_alloc(&device->vk.alloc,
                        *sems_dst_count * sizeof(VkSemaphore), 8,
		        VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!sems_dst) {
      *sems_dst_count = 0;
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   memcpy(*sems_dst, sems_src, *sems_dst_count * sizeof(VkSemaphore));

   return VK_SUCCESS;
}

static struct v3dv_submit_info_semaphores *
copy_semaphores_info(struct v3dv_device *device,
                     struct v3dv_submit_info_semaphores *info)
{
   VkResult result;
   struct v3dv_submit_info_semaphores *info_copy =
      vk_zalloc(&device->vk.alloc, sizeof(struct v3dv_submit_info_semaphores),
                8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!info_copy)
      return NULL;

   result = copy_semaphores(device, info->wait_sems, info->wait_sem_count,
                            &info_copy->wait_sems, &info_copy->wait_sem_count);
   if (result != VK_SUCCESS)
      goto fail;

   result = copy_semaphores(device, info->signal_sems, info->signal_sem_count,
                            &info_copy->signal_sems,
                            &info_copy->signal_sem_count);
   if (result != VK_SUCCESS)
      goto fail;

   return info_copy;

fail:
   if (info_copy->wait_sem_count > 0)
      vk_free(&device->vk.alloc, info_copy->wait_sems);
   vk_free(&device->vk.alloc, info_copy);

   return NULL;
}

static struct v3dv_wait_thread_info *
create_wait_thread_info(struct v3dv_job *job,
                        struct v3dv_submit_info_semaphores *sems_info)
{
   struct v3dv_wait_thread_info *info =
      vk_alloc(&job->device->vk.alloc, sizeof(*info), 8,
               VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!info)
      return NULL;

   info->job = job;
   info->sems_info = copy_semaphores_info(job->device, sems_info);
   if (!info->sems_info) {
      vk_free(&job->device->vk.alloc, info);
      return NULL;
   }

   return info;
}

static void
free_wait_thread_info(struct v3dv_device *device,
                      struct v3dv_wait_thread_info *info)
{
   assert(info != NULL);

   if (info->sems_info->wait_sem_count > 0)
      vk_free(&device->vk.alloc, info->sems_info->wait_sems);

   if (info->sems_info->signal_sem_count > 0)
      vk_free(&device->vk.alloc, info->sems_info->signal_sems);

   vk_free(&device->vk.alloc, info->sems_info);
   vk_free(&device->vk.alloc, info);
}

static bool
check_wait_events_complete(struct v3dv_job *job)
{
   assert(job->type == V3DV_JOB_TYPE_CPU_WAIT_EVENTS);

   struct v3dv_event_wait_cpu_job_info *info = &job->cpu.event_wait;
   for (uint32_t i = 0; i < info->event_count; i++) {
      if (!p_atomic_read(&info->events[i]->state))
         return false;
   }
   return true;
}

static void
wait_thread_finish(struct v3dv_queue *queue, pthread_t thread)
{
   mtx_lock(&queue->mutex);
   list_for_each_entry(struct v3dv_queue_submit_wait_info, info,
                       &queue->submit_wait_list, list_link) {
      for (uint32_t  i = 0; i < info->wait_thread_count; i++) {
         if (info->wait_threads[i].thread == thread) {
            info->wait_threads[i].finished = true;
            goto done;
         }
      }
   }

   unreachable(!"Failed to finish wait thread: not found");

done:
   mtx_unlock(&queue->mutex);
}

static void *
event_wait_thread_func(void *_info)
{
   struct v3dv_wait_thread_info *info = (struct v3dv_wait_thread_info *) _info;
   struct v3dv_job *job = info->job;
   assert(job->type == V3DV_JOB_TYPE_CPU_WAIT_EVENTS);

   /* Wait for events to be signaled */
   const useconds_t wait_interval_ms = 1;
   while (!check_wait_events_complete(job))
      usleep(wait_interval_ms * 1000);

   /* Now continue submitting pending jobs for the same command buffer after
    * the wait job.
    */
   struct v3dv_queue *queue = &job->device->queue;
   list_for_each_entry_from(struct v3dv_job, pjob, job->list_link.next,
                            &job->cmd_buffer->jobs, list_link) {
      /* We don't want to spawn more than one wait thread per command buffer.
       * If this job also requires a wait for events, we will do the wait here.
       */
      VkResult result = queue_submit_job(queue, pjob, info->sems_info, NULL);
      if (result == VK_NOT_READY) {
         while (!check_wait_events_complete(pjob)) {
            usleep(wait_interval_ms * 1000);
         }
         result = VK_SUCCESS;
      }

      if (result != VK_SUCCESS) {
         fprintf(stderr, "Wait thread job execution failed.\n");
         goto done;
      }
   }

done:
   wait_thread_finish(queue, pthread_self());
   free_wait_thread_info(job->device, info);
   return NULL;
}

static VkResult
spawn_event_wait_thread(struct v3dv_wait_thread_info *info, pthread_t *wait_thread)

{
   assert(info->job->type == V3DV_JOB_TYPE_CPU_WAIT_EVENTS);
   assert(info->job->cmd_buffer);
   assert(wait_thread != NULL);

   if (pthread_create(wait_thread, NULL, event_wait_thread_func, info))
      return vk_error(info->job->device, VK_ERROR_DEVICE_LOST);

   return VK_NOT_READY;
}

static VkResult
handle_wait_events_cpu_job(struct v3dv_job *job,
                           struct v3dv_submit_info_semaphores *sems_info,
                           pthread_t *wait_thread)
{
   assert(job->type == V3DV_JOB_TYPE_CPU_WAIT_EVENTS);

   /* If all events are signaled then we are done and can continue submitting
    * the rest of the command buffer normally.
    */
   if (check_wait_events_complete(job))
      return VK_SUCCESS;

   /* Otherwise, we put the rest of the command buffer on a wait thread until
    * all events are signaled. We only spawn a new thread on the first
    * wait job we see for a command buffer, any additional wait jobs in the
    * same command buffer will run in that same wait thread and will get here
    * with a NULL wait_thread pointer.
    *
    * Also, whether we spawn a wait thread or not, we always return
    * VK_NOT_READY (unless an error happened), so we stop trying to submit
    * any jobs in the same command buffer after the wait job. The wait thread
    * will attempt to submit them after the wait completes.
    */
   if (!wait_thread)
      return VK_NOT_READY;

   /* As events can be signaled by the host, jobs after the event wait must
    * still wait for semaphores, if any. So, whenever we spawn a wait thread,
    * we keep a copy of the semaphores (info->sems_info) to be used when
    * submitting pending jobs in the wait thread context.
    */
   struct v3dv_wait_thread_info *info =
      create_wait_thread_info(job, sems_info);
   if (!info)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   return spawn_event_wait_thread(info, wait_thread);
}

static VkResult
handle_copy_buffer_to_image_cpu_job(struct v3dv_job *job)
{
   assert(job->type == V3DV_JOB_TYPE_CPU_COPY_BUFFER_TO_IMAGE);
   struct v3dv_copy_buffer_to_image_cpu_job_info *info =
      &job->cpu.copy_buffer_to_image;

   /* Wait for all GPU work to finish first, since we may be accessing
    * the BOs involved in the operation.
    */
   v3dv_QueueWaitIdle(v3dv_queue_to_handle(&job->device->queue));

   /* Map BOs */
   struct v3dv_bo *dst_bo = info->image->mem->bo;
   assert(!dst_bo->map || dst_bo->map_size == dst_bo->size);
   if (!dst_bo->map && !v3dv_bo_map(job->device, dst_bo, dst_bo->size))
      return vk_error(job->device, VK_ERROR_OUT_OF_HOST_MEMORY);
   void *dst_ptr = dst_bo->map;

   struct v3dv_bo *src_bo = info->buffer->mem->bo;
   assert(!src_bo->map || src_bo->map_size == src_bo->size);
   if (!src_bo->map && !v3dv_bo_map(job->device, src_bo, src_bo->size))
      return vk_error(job->device, VK_ERROR_OUT_OF_HOST_MEMORY);
   void *src_ptr = src_bo->map;

   const struct v3d_resource_slice *slice =
      &info->image->slices[info->mip_level];

   const struct pipe_box box = {
      info->image_offset.x, info->image_offset.y, info->base_layer,
      info->image_extent.width, info->image_extent.height, info->layer_count,
   };

   /* Copy each layer */
   for (uint32_t i = 0; i < info->layer_count; i++) {
      const uint32_t dst_offset =
         v3dv_layer_offset(info->image, info->mip_level, info->base_layer + i);
      const uint32_t src_offset =
         info->buffer->mem_offset + info->buffer_offset +
         info->buffer_layer_stride * i;
      v3d_store_tiled_image(
         dst_ptr + dst_offset, slice->stride,
         src_ptr + src_offset, info->buffer_stride,
         slice->tiling, info->image->cpp, slice->padded_height, &box);
   }

   return VK_SUCCESS;
}

static VkResult
handle_timestamp_query_cpu_job(struct v3dv_job *job)
{
   assert(job->type == V3DV_JOB_TYPE_CPU_TIMESTAMP_QUERY);
   struct v3dv_timestamp_query_cpu_job_info *info = &job->cpu.query_timestamp;

   /* Wait for completion of all work queued before the timestamp query */
   v3dv_QueueWaitIdle(v3dv_queue_to_handle(&job->device->queue));

   /* Compute timestamp */
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);

   for (uint32_t i = 0; i < info->count; i++) {
      assert(info->query + i < info->pool->query_count);
      struct v3dv_query *query = &info->pool->queries[info->query + i];
      query->maybe_available = true;
      if (i == 0)
         query->value = t.tv_sec * 1000000000ull + t.tv_nsec;
   }

   return VK_SUCCESS;
}

static VkResult
handle_csd_job(struct v3dv_queue *queue,
               struct v3dv_job *job,
               struct v3dv_submit_info_semaphores *sems_info);

static VkResult
handle_csd_indirect_cpu_job(struct v3dv_queue *queue,
                            struct v3dv_job *job,
                            struct v3dv_submit_info_semaphores *sems_info)
{
   assert(job->type == V3DV_JOB_TYPE_CPU_CSD_INDIRECT);
   struct v3dv_csd_indirect_cpu_job_info *info = &job->cpu.csd_indirect;
   assert(info->csd_job);

   /* Make sure the GPU is no longer using the indirect buffer*/
   assert(info->buffer && info->buffer->mem && info->buffer->mem->bo);
   v3dv_bo_wait(queue->device, info->buffer->mem->bo, PIPE_TIMEOUT_INFINITE);

   /* Map the indirect buffer and read the dispatch parameters */
   assert(info->buffer && info->buffer->mem && info->buffer->mem->bo);
   struct v3dv_bo *bo = info->buffer->mem->bo;
   if (!bo->map && !v3dv_bo_map(job->device, bo, bo->size))
      return vk_error(job->device, VK_ERROR_OUT_OF_HOST_MEMORY);
   assert(bo->map);

   const uint32_t offset = info->buffer->mem_offset + info->offset;
   const uint32_t *group_counts = (uint32_t *) (bo->map + offset);
   if (group_counts[0] == 0 || group_counts[1] == 0|| group_counts[2] == 0)
      return VK_SUCCESS;

   if (memcmp(group_counts, info->csd_job->csd.wg_count,
              sizeof(info->csd_job->csd.wg_count)) != 0) {
      v3dv_cmd_buffer_rewrite_indirect_csd_job(info, group_counts);
   }

   handle_csd_job(queue, info->csd_job, sems_info);

   return VK_SUCCESS;
}

static VkResult
process_semaphores_to_signal(struct v3dv_device *device,
                             uint32_t count, const VkSemaphore *sems)
{
   if (count == 0)
      return VK_SUCCESS;

   /* If multisync is supported, we are signalling semaphores in the last job
    * of the last command buffer and, therefore, we do not need to process any
    * semaphores here.
    */
   if (device->pdevice->caps.multisync)
      return VK_SUCCESS;

   int render_fd = device->pdevice->render_fd;

   int fd;
   mtx_lock(&device->mutex);
   drmSyncobjExportSyncFile(render_fd,
                            device->last_job_syncs.syncs[V3DV_QUEUE_ANY],
                            &fd);
   mtx_unlock(&device->mutex);
   if (fd == -1)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = VK_SUCCESS;
   for (uint32_t i = 0; i < count; i++) {
      struct v3dv_semaphore *sem = v3dv_semaphore_from_handle(sems[i]);

      int ret;
      if (!sem->temp_sync)
         ret = drmSyncobjImportSyncFile(render_fd, sem->sync, fd);
      else
         ret = drmSyncobjImportSyncFile(render_fd, sem->temp_sync, fd);

      if (ret) {
         result = VK_ERROR_OUT_OF_HOST_MEMORY;
         break;
      }
   }

   assert(fd >= 0);
   close(fd);

   return result;
}

static VkResult
queue_submit_noop_job(struct v3dv_queue *queue,
                      struct v3dv_submit_info_semaphores *sems_info,
                      bool do_sem_signal, bool serialize);

static VkResult
process_fence_to_signal(struct v3dv_device *device, VkFence _fence)
{
   if (_fence == VK_NULL_HANDLE)
      return VK_SUCCESS;

   struct v3dv_fence *fence = v3dv_fence_from_handle(_fence);

   int render_fd = device->pdevice->render_fd;

   if (device->pdevice->caps.multisync) {
      struct v3dv_queue *queue = &device->queue;
      /* We signal the fence once all submitted command buffers have completed
       * execution. For this, we emit a noop job that waits on the completion
       * of all submitted jobs and signal the fence for this submission.
       * FIXME: In simpler cases (for instance, when all jobs were submitted to
       * the same queue), we can just import the last out sync produced into
       * the fence.
       */
      struct v3dv_submit_info_semaphores sems_info = {
         .wait_sem_count = 0,
         .wait_sems = NULL,
         .signal_sem_count = 0,
         .signal_sems = NULL,
         .fence = _fence,
      };

      return queue_submit_noop_job(queue, &sems_info, false, true);
   }

   int fd;
   mtx_lock(&device->mutex);
   drmSyncobjExportSyncFile(render_fd,
                            device->last_job_syncs.syncs[V3DV_QUEUE_ANY],
                            &fd);
   mtx_unlock(&device->mutex);
   if (fd == -1)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   int ret;
   if (!fence->temp_sync)
      ret = drmSyncobjImportSyncFile(render_fd, fence->sync, fd);
   else
      ret = drmSyncobjImportSyncFile(render_fd, fence->temp_sync, fd);

   assert(fd >= 0);
   close(fd);

   return ret ? VK_ERROR_OUT_OF_HOST_MEMORY : VK_SUCCESS;
}

static void
multisync_free(struct v3dv_device *device,
               struct drm_v3d_sem *out_syncs,
               struct drm_v3d_sem *in_syncs)
{
   vk_free(&device->vk.alloc, out_syncs);
   vk_free(&device->vk.alloc, in_syncs);
}

static struct drm_v3d_sem *
set_in_syncs(struct v3dv_device *device,
             struct v3dv_job *job,
             enum v3dv_queue_type queue,
             uint32_t *count,
             struct v3dv_submit_info_semaphores *sems_info)
{
   uint32_t n_sems = 0;

   /* If this is the first job submitted to a given GPU queue in this cmd buf
    * batch, it has to wait on wait semaphores (if any) before running.
    */
   if (device->last_job_syncs.first[queue])
      n_sems = sems_info->wait_sem_count;

   /* If we don't need to wait on wait semaphores but the serialize flag is
    * set, this job waits for completion of all GPU jobs submitted in any
    * queue V3DV_QUEUE_(CL/TFU/CSD) before running.
    */
   *count = n_sems == 0 && job->serialize ? 3 : n_sems;

   if (!*count)
      return NULL;

   struct drm_v3d_sem *syncs =
      vk_zalloc(&device->vk.alloc, *count * sizeof(struct drm_v3d_sem),
                8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);

   if (!syncs)
      return NULL;

   if (n_sems) {
      for (int i = 0; i < *count; i++) {
         struct v3dv_semaphore *sem =
            v3dv_semaphore_from_handle(sems_info->wait_sems[i]);
         syncs[i].handle = sem->sync;
      }
   } else {
      for (int i = 0; i < *count; i++)
         syncs[i].handle = device->last_job_syncs.syncs[i];
   }

   return syncs;
}

static struct drm_v3d_sem *
set_out_syncs(struct v3dv_device *device,
              struct v3dv_job *job,
              enum v3dv_queue_type queue,
              uint32_t *count,
              struct v3dv_submit_info_semaphores *sems_info)
{
   uint32_t n_sems = job->do_sem_signal ? sems_info->signal_sem_count : 0;

   /* We always signal the syncobj from `device->last_job_syncs` related to
    * this v3dv_queue_type to track the last job submitted to this queue.
    */
   (*count) = n_sems + 1;

   if (sems_info->fence)
      (*count)++;

   struct drm_v3d_sem *syncs =
      vk_zalloc(&device->vk.alloc, *count * sizeof(struct drm_v3d_sem),
                8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);

   if (!syncs)
      return NULL;

   if (n_sems) {
      for (unsigned i = 0; i < n_sems; i++) {
         struct v3dv_semaphore *sem =
            v3dv_semaphore_from_handle(sems_info->signal_sems[i]);
         syncs[i].handle = sem->sync;
      }
   }

   syncs[n_sems].handle = device->last_job_syncs.syncs[queue];

   if (sems_info->fence) {
      struct v3dv_fence *fence = v3dv_fence_from_handle(sems_info->fence);
      syncs[++n_sems].handle = fence->sync;
   }

   return syncs;
}

static void
set_ext(struct drm_v3d_extension *ext,
	struct drm_v3d_extension *next,
	uint32_t id,
	uintptr_t flags)
{
   ext->next = (uintptr_t)(void *)next;
   ext->id = id;
   ext->flags = flags;
}

/* This function sets the extension for multiple in/out syncobjs. When it is
 * successful, it sets the extension id to DRM_V3D_EXT_ID_MULTI_SYNC.
 * Otherwise, the extension id is 0, which means an out-of-memory error.
 */
static void
set_multisync(struct drm_v3d_multi_sync *ms,
              struct v3dv_submit_info_semaphores *sems_info,
              struct drm_v3d_extension *next,
              struct v3dv_device *device,
              struct v3dv_job *job,
              struct drm_v3d_sem *out_syncs,
              struct drm_v3d_sem *in_syncs,
              enum v3dv_queue_type queue_sync,
              enum v3d_queue wait_stage)
{
   uint32_t out_sync_count = 0, in_sync_count = 0;

   in_syncs = set_in_syncs(device, job, queue_sync,
                           &in_sync_count, sems_info);
   if (!in_syncs && in_sync_count)
      goto fail;

   out_syncs = set_out_syncs(device, job, queue_sync,
                             &out_sync_count, sems_info);

   assert(out_sync_count > 0);

   if (!out_syncs)
      goto fail;

   set_ext(&ms->base, next, DRM_V3D_EXT_ID_MULTI_SYNC, 0);
   ms->wait_stage = wait_stage;
   ms->out_sync_count = out_sync_count;
   ms->out_syncs = (uintptr_t)(void *)out_syncs;
   ms->in_sync_count = in_sync_count;
   ms->in_syncs = (uintptr_t)(void *)in_syncs;

   device->last_job_syncs.first[queue_sync] = false;

   return;

fail:
   if (in_syncs)
      vk_free(&device->vk.alloc, in_syncs);
   assert(!out_syncs);

   return;
}

static VkResult
handle_cl_job(struct v3dv_queue *queue,
              struct v3dv_job *job,
              struct v3dv_submit_info_semaphores *sems_info)
{
   struct v3dv_device *device = queue->device;

   struct drm_v3d_submit_cl submit = { 0 };

   /* Sanity check: we should only flag a bcl sync on a job that needs to be
    * serialized.
    */
   assert(job->serialize || !job->needs_bcl_sync);

   /* We expect to have just one RCL per job which should fit in just one BO.
    * Our BCL, could chain multiple BOS together though.
    */
   assert(list_length(&job->rcl.bo_list) == 1);
   assert(list_length(&job->bcl.bo_list) >= 1);
   struct v3dv_bo *bcl_fist_bo =
      list_first_entry(&job->bcl.bo_list, struct v3dv_bo, list_link);
   submit.bcl_start = bcl_fist_bo->offset;
   submit.bcl_end = job->bcl.bo->offset + v3dv_cl_offset(&job->bcl);
   submit.rcl_start = job->rcl.bo->offset;
   submit.rcl_end = job->rcl.bo->offset + v3dv_cl_offset(&job->rcl);

   submit.qma = job->tile_alloc->offset;
   submit.qms = job->tile_alloc->size;
   submit.qts = job->tile_state->offset;

   submit.flags = 0;
   if (job->tmu_dirty_rcl)
      submit.flags |= DRM_V3D_SUBMIT_CL_FLUSH_CACHE;

   submit.bo_handle_count = job->bo_count;
   uint32_t *bo_handles =
      (uint32_t *) malloc(sizeof(uint32_t) * submit.bo_handle_count);
   uint32_t bo_idx = 0;
   set_foreach(job->bos, entry) {
      struct v3dv_bo *bo = (struct v3dv_bo *)entry->key;
      bo_handles[bo_idx++] = bo->handle;
   }
   assert(bo_idx == submit.bo_handle_count);
   submit.bo_handles = (uintptr_t)(void *)bo_handles;

   /* We need a binning sync if we are waiting on a semaphore or if the job
    * comes after a pipeline barrier that involves geometry stages
    * (needs_bcl_sync).
    *
    * We need a render sync if the job doesn't need a binning sync but has
    * still been flagged for serialization. It should be noted that RCL jobs
    * don't start until the previous RCL job has finished so we don't really
    * need to add a fence for those, however, we might need to wait on a CSD or
    * TFU job, which are not automatically serialized with CL jobs.
    *
    * FIXME: see if we can do better and avoid bcl syncs for any jobs in the
    * command buffer after the first job where we should be able to track bcl
    * dependencies strictly through barriers.
    */
   const bool needs_bcl_sync =
      sems_info->wait_sem_count > 0 || job->needs_bcl_sync;
   const bool needs_rcl_sync = job->serialize && !needs_bcl_sync;
   struct drm_v3d_sem *out_syncs = NULL, *in_syncs = NULL;

   mtx_lock(&queue->device->mutex);

   /* Replace single semaphore settings whenever our kernel-driver supports
    * multiple semaphores extension.
    */
   if (device->pdevice->caps.multisync) {
      struct drm_v3d_multi_sync ms = { 0 };
      enum v3d_queue wait_stage = needs_rcl_sync ? V3D_RENDER : V3D_BIN;
      set_multisync(&ms, sems_info, NULL, device, job, out_syncs, in_syncs,
                    V3DV_QUEUE_CL, wait_stage);
      if (!ms.base.id) {
         mtx_unlock(&queue->device->mutex);
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      submit.flags |= DRM_V3D_SUBMIT_EXTENSION;
      submit.extensions = (uintptr_t)(void *)&ms;
      /* Disable legacy sync interface when multisync extension is used */
      submit.in_sync_rcl = 0;
      submit.in_sync_bcl = 0;
      submit.out_sync = 0;
   } else {
      uint32_t last_job_sync = device->last_job_syncs.syncs[V3DV_QUEUE_ANY];
      submit.in_sync_bcl = needs_bcl_sync ? last_job_sync : 0;
      submit.in_sync_rcl = needs_rcl_sync ? last_job_sync : 0;
      submit.out_sync = last_job_sync;
   }

   v3dv_clif_dump(device, job, &submit);
   int ret = v3dv_ioctl(device->pdevice->render_fd,
                        DRM_IOCTL_V3D_SUBMIT_CL, &submit);
   mtx_unlock(&queue->device->mutex);

   static bool warned = false;
   if (ret && !warned) {
      fprintf(stderr, "Draw call returned %s. Expect corruption.\n",
              strerror(errno));
      warned = true;
   }

   free(bo_handles);
   if (device->pdevice->caps.multisync)
      multisync_free(device, out_syncs, in_syncs);

   if (ret)
      return vk_error(device, VK_ERROR_DEVICE_LOST);

   return VK_SUCCESS;
}

static VkResult
handle_tfu_job(struct v3dv_queue *queue,
               struct v3dv_job *job,
               struct v3dv_submit_info_semaphores *sems_info)
{
   struct v3dv_device *device = queue->device;

   const bool needs_sync = sems_info->wait_sem_count || job->serialize;
   struct drm_v3d_sem *out_syncs = NULL, *in_syncs = NULL;

   mtx_lock(&device->mutex);

   /* Replace single semaphore settings whenever our kernel-driver supports
    * multiple semaphore extension.
    */
   if (device->pdevice->caps.multisync) {
      struct drm_v3d_multi_sync ms = { 0 };
      set_multisync(&ms, sems_info, NULL, device, job, out_syncs, in_syncs,
                    V3DV_QUEUE_TFU, V3D_TFU);
      if (!ms.base.id) {
         mtx_unlock(&device->mutex);
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      job->tfu.flags |= DRM_V3D_SUBMIT_EXTENSION;
      job->tfu.extensions = (uintptr_t)(void *)&ms;
      /* Disable legacy sync interface when multisync extension is used */
      job->tfu.in_sync = 0;
      job->tfu.out_sync = 0;
   } else {
      uint32_t last_job_sync = device->last_job_syncs.syncs[V3DV_QUEUE_ANY];
      job->tfu.in_sync = needs_sync ? last_job_sync : 0;
      job->tfu.out_sync = last_job_sync;
   }
   int ret = v3dv_ioctl(device->pdevice->render_fd,
                        DRM_IOCTL_V3D_SUBMIT_TFU, &job->tfu);
   mtx_unlock(&device->mutex);

   if (device->pdevice->caps.multisync)
      multisync_free(device, out_syncs, in_syncs);

   if (ret != 0) {
      fprintf(stderr, "Failed to submit TFU job: %d\n", ret);
      return vk_error(device, VK_ERROR_DEVICE_LOST);
   }

   return VK_SUCCESS;
}

static VkResult
handle_csd_job(struct v3dv_queue *queue,
               struct v3dv_job *job,
               struct v3dv_submit_info_semaphores *sems_info)
{
   struct v3dv_device *device = queue->device;

   struct drm_v3d_submit_csd *submit = &job->csd.submit;

   submit->bo_handle_count = job->bo_count;
   uint32_t *bo_handles =
      (uint32_t *) malloc(sizeof(uint32_t) * MAX2(4, submit->bo_handle_count * 2));
   uint32_t bo_idx = 0;
   set_foreach(job->bos, entry) {
      struct v3dv_bo *bo = (struct v3dv_bo *)entry->key;
      bo_handles[bo_idx++] = bo->handle;
   }
   assert(bo_idx == submit->bo_handle_count);
   submit->bo_handles = (uintptr_t)(void *)bo_handles;

   const bool needs_sync = sems_info->wait_sem_count || job->serialize;
   struct drm_v3d_sem *out_syncs = NULL, *in_syncs = NULL;

   mtx_lock(&queue->device->mutex);
   /* Replace single semaphore settings whenever our kernel-driver supports
    * multiple semaphore extension.
    */
   if (device->pdevice->caps.multisync) {
      struct drm_v3d_multi_sync ms = { 0 };
      set_multisync(&ms, sems_info, NULL, device, job, out_syncs, in_syncs,
                    V3DV_QUEUE_CSD, V3D_CSD);
      if (!ms.base.id) {
         mtx_unlock(&queue->device->mutex);
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      submit->flags |= DRM_V3D_SUBMIT_EXTENSION;
      submit->extensions = (uintptr_t)(void *)&ms;
      /* Disable legacy sync interface when multisync extension is used */
      submit->in_sync = 0;
      submit->out_sync = 0;
   } else {
      uint32_t last_job_sync = device->last_job_syncs.syncs[V3DV_QUEUE_ANY];
      submit->in_sync = needs_sync ? last_job_sync : 0;
      submit->out_sync = last_job_sync;
   }
   int ret = v3dv_ioctl(device->pdevice->render_fd,
                        DRM_IOCTL_V3D_SUBMIT_CSD, submit);
   mtx_unlock(&queue->device->mutex);

   static bool warned = false;
   if (ret && !warned) {
      fprintf(stderr, "Compute dispatch returned %s. Expect corruption.\n",
              strerror(errno));
      warned = true;
   }

   free(bo_handles);

   if (device->pdevice->caps.multisync)
      multisync_free(device, out_syncs, in_syncs);

   if (ret)
      return vk_error(device, VK_ERROR_DEVICE_LOST);

   return VK_SUCCESS;
}

static VkResult
queue_submit_job(struct v3dv_queue *queue,
                 struct v3dv_job *job,
                 struct v3dv_submit_info_semaphores *sems_info,
                 pthread_t *wait_thread)
{
   assert(job);

   /* CPU jobs typically execute explicit waits before they are processed. For
    * example, a query reset CPU job will explicitly wait for the queries
    * being unused before proceeding, etc. However, if we have any wait
    * semaphores, we need to honour that too for the first CPU job we process
    * in the command buffer batch. We do that by waiting for idle to ensure
    * that any previous work has been completed, at which point any wait
    * semaphores must be signalled, and we never need to do this again for the
    * same batch.
    */
   if (!v3dv_job_type_is_gpu(job) && sems_info->wait_sem_count) {
      v3dv_QueueWaitIdle(v3dv_queue_to_handle(&job->device->queue));
#ifdef DEBUG
      /* Loop through wait sems and check they are all signalled */
      for (int i = 0; i < sems_info->wait_sem_count; i++) {
         int render_fd = queue->device->pdevice->render_fd;
         struct v3dv_semaphore *sem =
            v3dv_semaphore_from_handle(sems_info->wait_sems[i]);
	 int ret = drmSyncobjWait(render_fd, &sem->sync, 1, 0, 0, NULL);
	 assert(ret == 0);
      }
#endif
      sems_info->wait_sem_count = 0;
   }

   switch (job->type) {
   case V3DV_JOB_TYPE_GPU_CL:
      return handle_cl_job(queue, job, sems_info);
   case V3DV_JOB_TYPE_GPU_TFU:
      return handle_tfu_job(queue, job, sems_info);
   case V3DV_JOB_TYPE_GPU_CSD:
      return handle_csd_job(queue, job, sems_info);
   case V3DV_JOB_TYPE_CPU_RESET_QUERIES:
      return handle_reset_query_cpu_job(job);
   case V3DV_JOB_TYPE_CPU_END_QUERY:
      return handle_end_query_cpu_job(job);
   case V3DV_JOB_TYPE_CPU_COPY_QUERY_RESULTS:
      return handle_copy_query_results_cpu_job(job);
   case V3DV_JOB_TYPE_CPU_SET_EVENT:
      return handle_set_event_cpu_job(job);
   case V3DV_JOB_TYPE_CPU_WAIT_EVENTS:
      return handle_wait_events_cpu_job(job, sems_info, wait_thread);
   case V3DV_JOB_TYPE_CPU_COPY_BUFFER_TO_IMAGE:
      return handle_copy_buffer_to_image_cpu_job(job);
   case V3DV_JOB_TYPE_CPU_CSD_INDIRECT:
      return handle_csd_indirect_cpu_job(queue, job, sems_info);
   case V3DV_JOB_TYPE_CPU_TIMESTAMP_QUERY:
      return handle_timestamp_query_cpu_job(job);
   default:
      unreachable("Unhandled job type");
   }
}

static VkResult
queue_create_noop_job(struct v3dv_queue *queue)
{
   struct v3dv_device *device = queue->device;
   queue->noop_job = vk_zalloc(&device->vk.alloc, sizeof(struct v3dv_job), 8,
                               VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!queue->noop_job)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   v3dv_job_init(queue->noop_job, V3DV_JOB_TYPE_GPU_CL, device, NULL, -1);

   v3dv_X(device, job_emit_noop)(queue->noop_job);

   return VK_SUCCESS;
}

static VkResult
queue_submit_noop_job(struct v3dv_queue *queue,
                      struct v3dv_submit_info_semaphores *sems_info,
                      bool do_sem_signal, bool serialize)
{
   if (!do_sem_signal && !serialize && !sems_info->wait_sem_count)
      return VK_SUCCESS;

   /* VkQueue host access is externally synchronized so we don't need to lock
    * here for the static variable.
    */
   if (!queue->noop_job) {
      VkResult result = queue_create_noop_job(queue);
      if (result != VK_SUCCESS)
         return result;
   }
   queue->noop_job->do_sem_signal = do_sem_signal;
   queue->noop_job->serialize = serialize;

   return queue_submit_job(queue, queue->noop_job, sems_info, NULL);
}

/* This function takes a job type and returns True if we have
 * previously submitted any jobs for the same command buffer batch
 * to a queue different to the one for this job type.
 */
static bool
cmd_buffer_batch_is_multi_queue(struct v3dv_device *device,
                                enum v3dv_job_type job_type)
{
   enum v3dv_queue_type queue_type = V3DV_QUEUE_ANY;
   struct v3dv_last_job_sync last_job_syncs;

   mtx_lock(&device->mutex);
   memcpy(&last_job_syncs, &device->last_job_syncs, sizeof(last_job_syncs));
   mtx_unlock(&device->mutex);

   switch (job_type) {
   case V3DV_JOB_TYPE_GPU_CL:
   case V3DV_JOB_TYPE_GPU_CL_SECONDARY:
      queue_type = V3DV_QUEUE_CL;
      break;
   case V3DV_JOB_TYPE_GPU_TFU:
      queue_type = V3DV_QUEUE_TFU;
      break;
   case V3DV_JOB_TYPE_GPU_CSD:
      queue_type = V3DV_QUEUE_CSD;
      break;
   default:
      unreachable("Queue type is undefined");
      break;
   }

   for (int i = 0; i < V3DV_QUEUE_ANY; i++) {
      if (i != queue_type && !last_job_syncs.first[i]) {
         return true;
      }
   }

   return false;
}

static VkResult
queue_submit_cmd_buffer(struct v3dv_queue *queue,
                        struct v3dv_cmd_buffer *cmd_buffer,
                        struct v3dv_submit_info_semaphores *sems_info,
                        bool is_last_cmd_buffer,
                        pthread_t *wait_thread)
{
   struct v3dv_job *last;
   bool do_sem_signal = is_last_cmd_buffer && sems_info->signal_sem_count > 0;

   assert(cmd_buffer);
   assert(cmd_buffer->status == V3DV_CMD_BUFFER_STATUS_EXECUTABLE);

   if (list_is_empty(&cmd_buffer->jobs))
      return queue_submit_noop_job(queue, sems_info, do_sem_signal, false);

   /* When we are in the last cmd buffer and there are semaphores to signal,
    * we process semaphores in the last job, following these conditions:
    * - CPU-job: we can't signal until all GPU work has completed, so we
    *   submit a serialized noop GPU job to handle signaling when all on-going
    *   GPU work on all queues has completed.
    * - GPU-job: can signal semaphores only if we have not submitted jobs to
    *   a queue other than the queue of this job. Otherwise, we submit a
    *   serialized noop job to handle signaling.
    */
   if (do_sem_signal) {
      last = list_last_entry(&cmd_buffer->jobs, struct v3dv_job, list_link);
      if (v3dv_job_type_is_gpu(last))
         last->do_sem_signal = true;
   }

   list_for_each_entry_safe(struct v3dv_job, job,
                            &cmd_buffer->jobs, list_link) {
      if (job->do_sem_signal &&
          cmd_buffer_batch_is_multi_queue(queue->device, job->type))
         job->do_sem_signal = false;
      VkResult result = queue_submit_job(queue, job, sems_info, wait_thread);
      if (result != VK_SUCCESS)
         return result;
   }

   /* If we are in the last cmd buffer batch, but the last job cannot handle
    * signal semaphores, we emit a serialized noop_job for signalling.
    */
   if (do_sem_signal && !(last && last->do_sem_signal))
      return queue_submit_noop_job(queue, sems_info, true, true);

   return VK_SUCCESS;
}

static void
add_wait_thread_to_list(struct v3dv_device *device,
                        pthread_t thread,
                        struct v3dv_queue_submit_wait_info **wait_info)
{
   /* If this is the first time we spawn a wait thread for this queue
    * submission create a v3dv_queue_submit_wait_info to track this and
    * any other threads in the same submission and add it to the global list
    * in the queue.
    */
   if (*wait_info == NULL) {
      *wait_info =
         vk_zalloc(&device->vk.alloc, sizeof(struct v3dv_queue_submit_wait_info), 8,
                   VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      (*wait_info)->device = device;
   }

   /* And add the thread to the list of wait threads for this submission */
   const uint32_t thread_idx = (*wait_info)->wait_thread_count;
   assert(thread_idx < 16);
   (*wait_info)->wait_threads[thread_idx].thread = thread;
   (*wait_info)->wait_threads[thread_idx].finished = false;
   (*wait_info)->wait_thread_count++;
}

static void
add_signal_semaphores_to_wait_list(struct v3dv_device *device,
                                   const VkSubmitInfo *pSubmit,
                                   struct v3dv_queue_submit_wait_info *wait_info)
{
   assert(wait_info);

   if (pSubmit->signalSemaphoreCount == 0)
      return;

   /* If multisync is supported, we just signal semaphores in the last job of
    * the last command buffer and, therefore, we do not need to add any
    * semaphores here.
    */
   if (device->pdevice->caps.multisync)
      return;

   /* Otherwise, we put all the semaphores in a list and we signal all of them
    * together from the submit master thread when the last wait thread in the
    * submit completes.
    */

   /* Check the size of the current semaphore list */
   const uint32_t prev_count = wait_info->signal_semaphore_count;
   const uint32_t prev_alloc_size = prev_count * sizeof(VkSemaphore);
   VkSemaphore *prev_list = wait_info->signal_semaphores;

   /* Resize the list to hold the additional semaphores */
   const uint32_t extra_alloc_size =
      pSubmit->signalSemaphoreCount * sizeof(VkSemaphore);
   wait_info->signal_semaphore_count += pSubmit->signalSemaphoreCount;
   wait_info->signal_semaphores =
      vk_alloc(&device->vk.alloc, prev_alloc_size + extra_alloc_size, 8,
               VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);

   /* Copy the old list to the new allocation and free the old list */
   if (prev_count > 0) {
      memcpy(wait_info->signal_semaphores, prev_list, prev_alloc_size);
      vk_free(&device->vk.alloc, prev_list);
   }

   /* Add the new semaphores to the list */
   memcpy(wait_info->signal_semaphores + prev_count,
          pSubmit->pSignalSemaphores, extra_alloc_size);
}

static VkResult
queue_submit_cmd_buffer_batch(struct v3dv_queue *queue,
                              const VkSubmitInfo *pSubmit,
                              struct v3dv_queue_submit_wait_info **wait_info)
{
   VkResult result = VK_SUCCESS;
   bool has_wait_threads = false;

   /* Wrap wait semaphores info from VkSubmitInfo to use it whenever we need
    * the data to submit all jobs in the same command buffer batch.
    */
   struct v3dv_submit_info_semaphores sems_info = {
      .wait_sem_count = pSubmit->waitSemaphoreCount,
      .wait_sems = (VkSemaphore *) pSubmit->pWaitSemaphores,
      .signal_sem_count = pSubmit->signalSemaphoreCount,
      .signal_sems = (VkSemaphore *) pSubmit->pSignalSemaphores,
      .fence = 0,
   };

   /* In the beginning of a cmd buffer batch, we set all last_job_syncs as
    * first. It helps to determine wait semaphores conditions.
    */
   for (unsigned i = 0; i < V3DV_QUEUE_COUNT; i++)
      queue->device->last_job_syncs.first[i] = true;

   /* Even if we don't have any actual work to submit we still need to wait
    * on the wait semaphores and signal the signal semaphores and fence, so
    * in this scenario we just submit a trivial no-op job so we don't have
    * to do anything special, it should not be a common case anyway.
    */
   if (pSubmit->commandBufferCount == 0) {
      result = queue_submit_noop_job(queue, &sems_info,
                                     sems_info.signal_sem_count > 0, false);
   } else {
      const uint32_t last_cmd_buffer_idx = pSubmit->commandBufferCount - 1;
      for (uint32_t i = 0; i < pSubmit->commandBufferCount; i++) {
         pthread_t wait_thread;
         struct v3dv_cmd_buffer *cmd_buffer =
            v3dv_cmd_buffer_from_handle(pSubmit->pCommandBuffers[i]);
         result = queue_submit_cmd_buffer(queue, cmd_buffer, &sems_info,
                                          (i == last_cmd_buffer_idx),
                                          &wait_thread);

         /* We get VK_NOT_READY if we had to spawn a wait thread for the
          * command buffer. In that scenario, we want to continue submitting
          * any pending command buffers in the batch, but we don't want to
          * process any signal semaphores for the batch until we know we have
          * submitted every job for every command buffer in the batch.
          */
         if (result == VK_NOT_READY) {
            result = VK_SUCCESS;
            add_wait_thread_to_list(queue->device, wait_thread, wait_info);
            has_wait_threads = true;
         }

         if (result != VK_SUCCESS)
            break;
      }
   }

   if (result != VK_SUCCESS)
      return result;

   /* If had to emit any wait threads in this submit we need to wait for all
    * of them to complete before we can signal any semaphores.
    */
   if (!has_wait_threads) {
      return process_semaphores_to_signal(queue->device,
                                          pSubmit->signalSemaphoreCount,
                                          pSubmit->pSignalSemaphores);
   } else {
      assert(*wait_info);
      add_signal_semaphores_to_wait_list(queue->device, pSubmit, *wait_info);
      return VK_NOT_READY;
   }
}

static void *
master_wait_thread_func(void *_wait_info)
{
   struct v3dv_queue_submit_wait_info *wait_info =
      (struct v3dv_queue_submit_wait_info *) _wait_info;

   struct v3dv_queue *queue = &wait_info->device->queue;

   /* Wait for all command buffer wait threads to complete */
   for (uint32_t i = 0; i < wait_info->wait_thread_count; i++) {
      int res = pthread_join(wait_info->wait_threads[i].thread, NULL);
      if (res != 0)
         fprintf(stderr, "Wait thread failed to join.\n");
   }

   /* Signal semaphores and fences */
   VkResult result;
   result = process_semaphores_to_signal(wait_info->device,
                                         wait_info->signal_semaphore_count,
                                         wait_info->signal_semaphores);
   if (result != VK_SUCCESS)
      fprintf(stderr, "Wait thread semaphore signaling failed.");

   result = process_fence_to_signal(wait_info->device, wait_info->fence);
   if (result != VK_SUCCESS)
      fprintf(stderr, "Wait thread fence signaling failed.");

   /* Release wait_info */
   mtx_lock(&queue->mutex);
   list_del(&wait_info->list_link);
   mtx_unlock(&queue->mutex);

   vk_free(&wait_info->device->vk.alloc, wait_info->signal_semaphores);
   vk_free(&wait_info->device->vk.alloc, wait_info);

   return NULL;
}


static VkResult
spawn_master_wait_thread(struct v3dv_queue *queue,
                         struct v3dv_queue_submit_wait_info *wait_info)

{
   VkResult result = VK_SUCCESS;

   mtx_lock(&queue->mutex);
   if (pthread_create(&wait_info->master_wait_thread, NULL,
                      master_wait_thread_func, wait_info)) {
      result = vk_error(queue, VK_ERROR_DEVICE_LOST);
      goto done;
   }

   list_addtail(&wait_info->list_link, &queue->submit_wait_list);

done:
   mtx_unlock(&queue->mutex);
   return result;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_QueueSubmit(VkQueue _queue,
                 uint32_t submitCount,
                 const VkSubmitInfo* pSubmits,
                 VkFence fence)
{
   V3DV_FROM_HANDLE(v3dv_queue, queue, _queue);

   struct v3dv_queue_submit_wait_info *wait_info = NULL;

   VkResult result = VK_SUCCESS;
   for (uint32_t i = 0; i < submitCount; i++) {
      result = queue_submit_cmd_buffer_batch(queue, &pSubmits[i], &wait_info);
      if (result != VK_SUCCESS && result != VK_NOT_READY)
         goto done;
   }

   if (!wait_info) {
      assert(result != VK_NOT_READY);
      result = process_fence_to_signal(queue->device, fence);
      goto done;
   }

   /* We emitted wait threads, so we have to spwan a master thread for this
    * queue submission that waits for all other threads to complete and then
    * will signal any semaphores and fences.
    */
   assert(wait_info);
   wait_info->fence = fence;
   result = spawn_master_wait_thread(queue, wait_info);

done:
   return result;
}

static void
destroy_syncobj(uint32_t device_fd, uint32_t *sync)
{
   assert(sync);
   drmSyncobjDestroy(device_fd, *sync);
   *sync = 0;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_CreateSemaphore(VkDevice _device,
                     const VkSemaphoreCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkSemaphore *pSemaphore)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);

   struct v3dv_semaphore *sem =
      vk_object_zalloc(&device->vk, pAllocator, sizeof(struct v3dv_semaphore),
                       VK_OBJECT_TYPE_SEMAPHORE);
   if (sem == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   int ret = drmSyncobjCreate(device->pdevice->render_fd, 0, &sem->sync);
   if (ret) {
      vk_object_free(&device->vk, pAllocator, sem);
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   *pSemaphore = v3dv_semaphore_to_handle(sem);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetPhysicalDeviceExternalSemaphoreProperties(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceExternalSemaphoreInfo *pExternalSemaphoreInfo,
    VkExternalSemaphoreProperties *pExternalSemaphoreProperties)
{
   switch (pExternalSemaphoreInfo->handleType) {
   case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT:
   case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT:
      pExternalSemaphoreProperties->exportFromImportedHandleTypes =
         VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT |
         VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
      pExternalSemaphoreProperties->compatibleHandleTypes =
         VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT |
         VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;

      /* FIXME: we can't import external semaphores until we improve the kernel
       * submit interface to handle multiple in syncobjs, because once we have
       * an imported semaphore in our list of semaphores to wait on, we can no
       * longer use the workaround of waiting on the last syncobj fence produced
       * from the device, since the imported semaphore may not (and in fact, it
       * would typically not) have been produced from same device.
       *
       * This behavior is exercised via dEQP-VK.synchronization.cross_instance.*.
       * Particularly, this test:
       * dEQP-VK.synchronization.cross_instance.dedicated.
       * write_ssbo_compute_read_vertex_input.buffer_16384_binary_semaphore_fd
       * fails consistently because of this, so it'll be a good reference to
       * verify the implementation when the kernel bits are in place.
       */
      pExternalSemaphoreProperties->externalSemaphoreFeatures = 0;

      /* FIXME: See comment in GetPhysicalDeviceExternalFenceProperties
       * for details on why we can't export to SYNC_FD.
       */
      if (pExternalSemaphoreInfo->handleType !=
          VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT) {
         pExternalSemaphoreProperties->externalSemaphoreFeatures |=
            VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT;
      }
      break;
   default:
      pExternalSemaphoreProperties->exportFromImportedHandleTypes = 0;
      pExternalSemaphoreProperties->compatibleHandleTypes = 0;
      pExternalSemaphoreProperties->externalSemaphoreFeatures = 0;
      break;
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_ImportSemaphoreFdKHR(
   VkDevice _device,
   const VkImportSemaphoreFdInfoKHR *pImportSemaphoreFdInfo)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_semaphore, sem, pImportSemaphoreFdInfo->semaphore);

   assert(pImportSemaphoreFdInfo->sType ==
          VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_FD_INFO_KHR);

   int fd = pImportSemaphoreFdInfo->fd;
   int render_fd = device->pdevice->render_fd;

   bool is_temporary =
      pImportSemaphoreFdInfo->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT ||
      (pImportSemaphoreFdInfo->flags & VK_SEMAPHORE_IMPORT_TEMPORARY_BIT);

   uint32_t new_sync;
   switch (pImportSemaphoreFdInfo->handleType) {
   case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT: {
      /* "If handleType is VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT, the
       *  special value -1 for fd is treated like a valid sync file descriptor
       *  referring to an object that has already signaled. The import
       *  operation will succeed and the VkSemaphore will have a temporarily
       *  imported payload as if a valid file descriptor had been provided."
       */
      unsigned flags = fd == -1 ? DRM_SYNCOBJ_CREATE_SIGNALED : 0;
      if (drmSyncobjCreate(render_fd, flags, &new_sync))
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

      if (fd != -1) {
         if (drmSyncobjImportSyncFile(render_fd, new_sync, fd)) {
            drmSyncobjDestroy(render_fd, new_sync);
            return vk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);
         }
      }
      break;
   }
   case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT: {
      if (drmSyncobjFDToHandle(render_fd, fd, &new_sync))
         return vk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);
      break;
   }
   default:
      return vk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }

   destroy_syncobj(render_fd, &sem->temp_sync);
   if (is_temporary) {
      sem->temp_sync = new_sync;
   } else {
      destroy_syncobj(render_fd, &sem->sync);
      sem->sync = new_sync;
   }

   /* From the Vulkan 1.0.53 spec:
    *
    *    "Importing a semaphore payload from a file descriptor transfers
    *     ownership of the file descriptor from the application to the
    *     Vulkan implementation. The application must not perform any
    *     operations on the file descriptor after a successful import."
    *
    * If the import fails, we leave the file descriptor open.
    */
   if (fd != -1)
      close(fd);

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetSemaphoreFdKHR(VkDevice _device,
                       const VkSemaphoreGetFdInfoKHR *pGetFdInfo,
                       int *pFd)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_semaphore, sem, pGetFdInfo->semaphore);

   assert(pGetFdInfo->sType == VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR);

   *pFd = -1;
   int render_fd = device->pdevice->render_fd;
   switch (pGetFdInfo->handleType) {
   case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT: {
      drmSyncobjExportSyncFile(render_fd, sem->sync, pFd);
      if (*pFd == -1)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      break;
   case VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT:
      drmSyncobjHandleToFD(render_fd, sem->sync, pFd);
      if (*pFd == -1)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      break;
   }
   default:
      unreachable("Unsupported external semaphore handle type");
   }

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_DestroySemaphore(VkDevice _device,
                      VkSemaphore semaphore,
                      const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_semaphore, sem, semaphore);

   if (sem == NULL)
      return;

   destroy_syncobj(device->pdevice->render_fd, &sem->sync);
   destroy_syncobj(device->pdevice->render_fd, &sem->temp_sync);

   vk_object_free(&device->vk, pAllocator, sem);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_CreateFence(VkDevice _device,
                 const VkFenceCreateInfo *pCreateInfo,
                 const VkAllocationCallbacks *pAllocator,
                 VkFence *pFence)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);

   struct v3dv_fence *fence =
      vk_object_zalloc(&device->vk, pAllocator, sizeof(struct v3dv_fence),
                       VK_OBJECT_TYPE_FENCE);
   if (fence == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   unsigned flags = 0;
   if (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT)
      flags |= DRM_SYNCOBJ_CREATE_SIGNALED;
   int ret = drmSyncobjCreate(device->pdevice->render_fd, flags, &fence->sync);
   if (ret) {
      vk_object_free(&device->vk, pAllocator, fence);
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   *pFence = v3dv_fence_to_handle(fence);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetPhysicalDeviceExternalFenceProperties(
    VkPhysicalDevice physicalDevice,
    const VkPhysicalDeviceExternalFenceInfo *pExternalFenceInfo,
    VkExternalFenceProperties *pExternalFenceProperties)

{
   switch (pExternalFenceInfo->handleType) {
   case VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT:
   case VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT:
      pExternalFenceProperties->exportFromImportedHandleTypes =
         VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT |
         VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
      pExternalFenceProperties->compatibleHandleTypes =
         VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT |
         VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
      pExternalFenceProperties->externalFenceFeatures =
         VK_EXTERNAL_FENCE_FEATURE_IMPORTABLE_BIT;

      /* FIXME: SYNC_FD exports the actual fence referenced by the syncobj, not
       * the syncobj itself, and that fence is only created after we have
       * submitted to the kernel and updated the syncobj for the fence to import
       * the actual DRM fence created with the submission. Unfortunately, if the
       * queue submission has a 'wait for events' we may hold any jobs after the
       * wait in a user-space thread until the events are signaled, and in that
       * case we don't update the out fence of the submit until the events are
       * signaled and we can submit all the jobs involved with the vkQueueSubmit
       * call. This means that if the applications submits with an out fence and
       * a wait for events, trying to export the out fence to a SYNC_FD rigth
       * after the submission and before the events are signaled will fail,
       * because the actual DRM fence won't exist yet. This is not a problem
       * with OPAQUE_FD because in this case we export the entire syncobj, not
       * the underlying DRM fence. To fix this we need to rework our kernel
       * interface to be more flexible and accept multiple in/out syncobjs so
       * we can implement event waits as regular fence waits on the kernel side,
       * until then, we can only reliably export OPAQUE_FD.
       */
      if (pExternalFenceInfo->handleType !=
          VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT) {
         pExternalFenceProperties->externalFenceFeatures |=
            VK_EXTERNAL_FENCE_FEATURE_EXPORTABLE_BIT;
      }
      break;
   default:
      pExternalFenceProperties->exportFromImportedHandleTypes = 0;
      pExternalFenceProperties->compatibleHandleTypes = 0;
      pExternalFenceProperties->externalFenceFeatures = 0;
      break;
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_ImportFenceFdKHR(VkDevice _device,
                      const VkImportFenceFdInfoKHR *pImportFenceFdInfo)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_fence, fence, pImportFenceFdInfo->fence);

   assert(pImportFenceFdInfo->sType ==
          VK_STRUCTURE_TYPE_IMPORT_FENCE_FD_INFO_KHR);

   int fd = pImportFenceFdInfo->fd;
   int render_fd = device->pdevice->render_fd;

   bool is_temporary =
      pImportFenceFdInfo->handleType == VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT ||
      (pImportFenceFdInfo->flags & VK_FENCE_IMPORT_TEMPORARY_BIT);

   uint32_t new_sync;
   switch (pImportFenceFdInfo->handleType) {
   case VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT: {
      /* "If handleType is VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT, the
       *  special value -1 for fd is treated like a valid sync file descriptor
       *  referring to an object that has already signaled. The import
       *  operation will succeed and the VkFence will have a temporarily
       *  imported payload as if a valid file descriptor had been provided."
       */
      unsigned flags = fd == -1 ? DRM_SYNCOBJ_CREATE_SIGNALED : 0;
      if (drmSyncobjCreate(render_fd, flags, &new_sync))
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

      if (fd != -1) {
         if (drmSyncobjImportSyncFile(render_fd, new_sync, fd)) {
            drmSyncobjDestroy(render_fd, new_sync);
            return vk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);
         }
      }
      break;
   }
   case VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT: {
      if (drmSyncobjFDToHandle(render_fd, fd, &new_sync))
         return vk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);
      break;
   }
   default:
      return vk_error(device, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }

   destroy_syncobj(render_fd, &fence->temp_sync);
   if (is_temporary) {
      fence->temp_sync = new_sync;
   } else {
      destroy_syncobj(render_fd, &fence->sync);
      fence->sync = new_sync;
   }

   /* From the Vulkan 1.0.53 spec:
    *
    *    "Importing a fence payload from a file descriptor transfers
    *     ownership of the file descriptor from the application to the
    *     Vulkan implementation. The application must not perform any
    *     operations on the file descriptor after a successful import."
    *
    * If the import fails, we leave the file descriptor open.
    */
   if (fd != -1)
      close(fd);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_DestroyFence(VkDevice _device,
                  VkFence _fence,
                  const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_fence, fence, _fence);

   if (fence == NULL)
      return;

   destroy_syncobj(device->pdevice->render_fd, &fence->sync);
   destroy_syncobj(device->pdevice->render_fd, &fence->temp_sync);

   vk_object_free(&device->vk, pAllocator, fence);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetFenceStatus(VkDevice _device, VkFence _fence)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_fence, fence, _fence);

   int ret = drmSyncobjWait(device->pdevice->render_fd, &fence->sync, 1,
                            0, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL);
   if (ret == -ETIME)
      return VK_NOT_READY;
   else if (ret)
      return vk_error(device, VK_ERROR_DEVICE_LOST);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetFenceFdKHR(VkDevice _device,
                   const VkFenceGetFdInfoKHR *pGetFdInfo,
                   int *pFd)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_fence, fence, pGetFdInfo->fence);

   assert(pGetFdInfo->sType == VK_STRUCTURE_TYPE_FENCE_GET_FD_INFO_KHR);

   *pFd = -1;
   int render_fd = device->pdevice->render_fd;
   switch (pGetFdInfo->handleType) {
   case VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT: {
      drmSyncobjExportSyncFile(render_fd, fence->sync, pFd);
      if (*pFd == -1)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      break;
   case VK_EXTERNAL_FENCE_HANDLE_TYPE_OPAQUE_FD_BIT:
      drmSyncobjHandleToFD(render_fd, fence->sync, pFd);
      if (*pFd == -1)
         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      break;
   }
   default:
      unreachable("Unsupported external fence handle type");
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_ResetFences(VkDevice _device, uint32_t fenceCount, const VkFence *pFences)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);

   uint32_t *syncobjs = vk_alloc(&device->vk.alloc,
                                 sizeof(*syncobjs) * fenceCount, 8,
                                 VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!syncobjs)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   int render_fd = device->pdevice->render_fd;
   uint32_t reset_count = 0;
   for (uint32_t i = 0; i < fenceCount; i++) {
      struct v3dv_fence *fence = v3dv_fence_from_handle(pFences[i]);
      /* From the Vulkan spec, section 'Importing Fence Payloads':
       *
       *    "If the import is temporary, the fence will be restored to its
       *     permanent state the next time that fence is passed to
       *     vkResetFences.
       *
       *     Note: Restoring a fence to its prior permanent payload is a
       *     distinct operation from resetting a fence payload."
       *
       * To restore the previous state, we just need to destroy the temporary.
       */
      if (fence->temp_sync)
         destroy_syncobj(render_fd, &fence->temp_sync);
      else
         syncobjs[reset_count++] = fence->sync;
   }

   int ret = 0;
   if (reset_count > 0)
      ret = drmSyncobjReset(render_fd, syncobjs, reset_count);

   vk_free(&device->vk.alloc, syncobjs);

   if (ret)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_WaitForFences(VkDevice _device,
                   uint32_t fenceCount,
                   const VkFence *pFences,
                   VkBool32 waitAll,
                   uint64_t timeout)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);

   const uint64_t abs_timeout = get_absolute_timeout(timeout);

   uint32_t *syncobjs = vk_alloc(&device->vk.alloc,
                                 sizeof(*syncobjs) * fenceCount, 8,
                                 VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!syncobjs)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   for (uint32_t i = 0; i < fenceCount; i++) {
      struct v3dv_fence *fence = v3dv_fence_from_handle(pFences[i]);
      syncobjs[i] = fence->temp_sync ? fence->temp_sync : fence->sync;
   }

   unsigned flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT;
   if (waitAll)
      flags |= DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL;

   int ret;
   do {
      ret = drmSyncobjWait(device->pdevice->render_fd, syncobjs, fenceCount,
                           timeout, flags, NULL);
   } while (ret == -ETIME && gettime_ns() < abs_timeout);

   vk_free(&device->vk.alloc, syncobjs);

   if (ret == -ETIME)
      return VK_TIMEOUT;
   else if (ret)
      return vk_error(device, VK_ERROR_DEVICE_LOST);
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_QueueBindSparse(VkQueue _queue,
                     uint32_t bindInfoCount,
                     const VkBindSparseInfo *pBindInfo,
                     VkFence fence)
{
   V3DV_FROM_HANDLE(v3dv_queue, queue, _queue);
   return vk_error(queue, VK_ERROR_FEATURE_NOT_PRESENT);
}
