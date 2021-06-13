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
   if (!(V3D_DEBUG & (V3D_DEBUG_CL | V3D_DEBUG_CLIF)))
      return;

   struct clif_dump *clif = clif_dump_init(&device->devinfo,
                                           stderr,
                                           V3D_DEBUG & V3D_DEBUG_CL);

   set_foreach(job->bos, entry) {
      struct v3dv_bo *bo = (void *)entry->key;
      char *name = ralloc_asprintf(NULL, "%s_0x%x",
                                   bo->name, bo->offset);

      v3dv_bo_map(device, bo, bo->size);
      clif_dump_add_bo(clif, name, bo->offset, bo->size, bo->map);

      ralloc_free(name);
   }

   clif_dump(clif, submit);

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
                 bool do_sem_wait,
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

   mtx_lock(&device->mutex);
   uint32_t last_job_sync = device->last_job_sync;
   mtx_unlock(&device->mutex);

   int ret = drmSyncobjWait(device->pdevice->render_fd,
                            &last_job_sync, 1, INT64_MAX, 0, NULL);
   if (ret)
      return VK_ERROR_DEVICE_LOST;

   return VK_SUCCESS;
}

VkResult
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
   for (uint32_t i = info->first; i < info->first + info->count; i++) {
      assert(i < info->pool->query_count);
      struct v3dv_query *query = &info->pool->queries[i];
      query->maybe_available = false;
      switch (info->pool->query_type) {
      case VK_QUERY_TYPE_OCCLUSION: {
         v3dv_bo_wait(job->device, query->bo, PIPE_TIMEOUT_INFINITE);
         uint32_t *counter = (uint32_t *) query->bo->map;
         *counter = 0;
         break;
      }
      case VK_QUERY_TYPE_TIMESTAMP:
         query->value = 0;
         break;
      default:
         unreachable("Unsupported query type");
      }
   }

   return VK_SUCCESS;
}

static VkResult
handle_end_query_cpu_job(struct v3dv_job *job)
{
   struct v3dv_end_query_cpu_job_info *info = &job->cpu.query_end;
   assert(info->query < info->pool->query_count);
   struct v3dv_query *query = &info->pool->queries[info->query];
   query->maybe_available = true;

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
      return vk_error(job->device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* FIXME: if flags includes VK_QUERY_RESULT_WAIT_BIT this could trigger a
    * sync wait on the CPU for the corresponding GPU jobs to finish. We might
    * want to use a submission thread to avoid blocking on the main thread.
    */
   v3dv_get_query_pool_results_cpu(job->device,
                                   info->pool,
                                   info->first,
                                   info->count,
                                   bo->map + info->dst->mem_offset,
                                   info->stride,
                                   info->flags);

   return VK_SUCCESS;
}

static VkResult
handle_set_event_cpu_job(struct v3dv_job *job, bool is_wait_thread)
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
event_wait_thread_func(void *_job)
{
   struct v3dv_job *job = (struct v3dv_job *) _job;
   assert(job->type == V3DV_JOB_TYPE_CPU_WAIT_EVENTS);
   struct v3dv_event_wait_cpu_job_info *info = &job->cpu.event_wait;

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
      VkResult result = queue_submit_job(queue, pjob, info->sem_wait, NULL);
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
   return NULL;
}

static VkResult
spawn_event_wait_thread(struct v3dv_job *job, pthread_t *wait_thread)

{
   assert(job->type == V3DV_JOB_TYPE_CPU_WAIT_EVENTS);
   assert(job->cmd_buffer);
   assert(wait_thread != NULL);

   if (pthread_create(wait_thread, NULL, event_wait_thread_func, job))
      return vk_error(job->device->instance, VK_ERROR_DEVICE_LOST);

   return VK_NOT_READY;
}

static VkResult
handle_wait_events_cpu_job(struct v3dv_job *job,
                           bool sem_wait,
                           pthread_t *wait_thread)
{
   assert(job->type == V3DV_JOB_TYPE_CPU_WAIT_EVENTS);
   struct v3dv_event_wait_cpu_job_info *info = &job->cpu.event_wait;

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
   info->sem_wait = sem_wait;
   if (wait_thread)
      return spawn_event_wait_thread(job, wait_thread);
   else
      return VK_NOT_READY;
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
      return vk_error(job->device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   void *dst_ptr = dst_bo->map;

   struct v3dv_bo *src_bo = info->buffer->mem->bo;
   assert(!src_bo->map || src_bo->map_size == src_bo->size);
   if (!src_bo->map && !v3dv_bo_map(job->device, src_bo, src_bo->size))
      return vk_error(job->device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
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
   assert(info->query < info->pool->query_count);
   struct v3dv_query *query = &info->pool->queries[info->query];
   query->maybe_available = true;
   query->value = t.tv_sec * 1000000000ull + t.tv_nsec;

   return VK_SUCCESS;
}

static VkResult
handle_csd_job(struct v3dv_queue *queue,
               struct v3dv_job *job,
               bool do_sem_wait);

static VkResult
handle_csd_indirect_cpu_job(struct v3dv_queue *queue,
                            struct v3dv_job *job,
                            bool do_sem_wait)
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
      return vk_error(job->device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   assert(bo->map);

   const uint32_t offset = info->buffer->mem_offset + info->offset;
   const uint32_t *group_counts = (uint32_t *) (bo->map + offset);
   if (group_counts[0] == 0 || group_counts[1] == 0|| group_counts[2] == 0)
      return VK_SUCCESS;

   if (memcmp(group_counts, info->csd_job->csd.wg_count,
              sizeof(info->csd_job->csd.wg_count)) != 0) {
      v3dv_cmd_buffer_rewrite_indirect_csd_job(info, group_counts);
   }

   handle_csd_job(queue, info->csd_job, do_sem_wait);

   return VK_SUCCESS;
}

static VkResult
process_semaphores_to_signal(struct v3dv_device *device,
                             uint32_t count, const VkSemaphore *sems)
{
   if (count == 0)
      return VK_SUCCESS;

   int render_fd = device->pdevice->render_fd;

   int fd;
   mtx_lock(&device->mutex);
   drmSyncobjExportSyncFile(render_fd, device->last_job_sync, &fd);
   mtx_unlock(&device->mutex);
   if (fd == -1)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   for (uint32_t i = 0; i < count; i++) {
      struct v3dv_semaphore *sem = v3dv_semaphore_from_handle(sems[i]);

      if (sem->fd >= 0)
         close(sem->fd);
      sem->fd = -1;

      int ret = drmSyncobjImportSyncFile(render_fd, sem->sync, fd);
      if (ret)
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

      sem->fd = fd;
   }

   return VK_SUCCESS;
}

static VkResult
process_fence_to_signal(struct v3dv_device *device, VkFence _fence)
{
   if (_fence == VK_NULL_HANDLE)
      return VK_SUCCESS;

   struct v3dv_fence *fence = v3dv_fence_from_handle(_fence);

   if (fence->fd >= 0)
      close(fence->fd);
   fence->fd = -1;

   int render_fd = device->pdevice->render_fd;

   int fd;
   mtx_lock(&device->mutex);
   drmSyncobjExportSyncFile(render_fd, device->last_job_sync, &fd);
   mtx_unlock(&device->mutex);
   if (fd == -1)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   int ret = drmSyncobjImportSyncFile(render_fd, fence->sync, fd);
   if (ret)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   fence->fd = fd;

   return VK_SUCCESS;
}

static VkResult
handle_cl_job(struct v3dv_queue *queue,
              struct v3dv_job *job,
              bool do_sem_wait)
{
   struct v3dv_device *device = queue->device;

   struct drm_v3d_submit_cl submit;

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

   /* We need a binning sync if we are waiting on a sempahore (do_sem_wait) or
    * if the job comes after a pipeline barrier than involves geometry stages
    * (needs_bcl_sync).
    *
    * We need a render sync if the job doesn't need a binning sync but has
    * still been flagged for serialization. It should be noted that RCL jobs
    * don't start until the previous RCL job has finished so we don't really
    * need to add a fence for those, however, we might need to wait on a CSD or
    * TFU job, which are not automatically serialized with CL jobs.
    *
    * FIXME: for now, if we are asked to wait on any semaphores, we just wait
    * on the last job we submitted. In the future we might want to pass the
    * actual syncobj of the wait semaphores so we don't block on the last RCL
    * if we only need to wait for a previous CSD or TFU, for example, but
    * we would have to extend our kernel interface to support the case where
    * we have more than one semaphore to wait on.
    */
   const bool needs_bcl_sync = do_sem_wait || job->needs_bcl_sync;
   const bool needs_rcl_sync = job->serialize && !needs_bcl_sync;

   mtx_lock(&queue->device->mutex);
   submit.in_sync_bcl = needs_bcl_sync ? device->last_job_sync : 0;
   submit.in_sync_rcl = needs_rcl_sync ? device->last_job_sync : 0;
   submit.out_sync = device->last_job_sync;
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

   if (ret)
      return vk_error(device->instance, VK_ERROR_DEVICE_LOST);

   return VK_SUCCESS;
}

static VkResult
handle_tfu_job(struct v3dv_queue *queue,
               struct v3dv_job *job,
               bool do_sem_wait)
{
   struct v3dv_device *device = queue->device;

   const bool needs_sync = do_sem_wait || job->serialize;

   mtx_lock(&device->mutex);
   job->tfu.in_sync = needs_sync ? device->last_job_sync : 0;
   job->tfu.out_sync = device->last_job_sync;
   int ret = v3dv_ioctl(device->pdevice->render_fd,
                        DRM_IOCTL_V3D_SUBMIT_TFU, &job->tfu);
   mtx_unlock(&device->mutex);

   if (ret != 0) {
      fprintf(stderr, "Failed to submit TFU job: %d\n", ret);
      return vk_error(device->instance, VK_ERROR_DEVICE_LOST);
   }

   return VK_SUCCESS;
}

static VkResult
handle_csd_job(struct v3dv_queue *queue,
               struct v3dv_job *job,
               bool do_sem_wait)
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

   const bool needs_sync = do_sem_wait || job->serialize;

   mtx_lock(&queue->device->mutex);
   submit->in_sync = needs_sync ? device->last_job_sync : 0;
   submit->out_sync = device->last_job_sync;
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

   if (ret)
      return vk_error(device->instance, VK_ERROR_DEVICE_LOST);

   return VK_SUCCESS;
}

static VkResult
queue_submit_job(struct v3dv_queue *queue,
                 struct v3dv_job *job,
                 bool do_sem_wait,
                 pthread_t *wait_thread)
{
   assert(job);

   switch (job->type) {
   case V3DV_JOB_TYPE_GPU_CL:
      return handle_cl_job(queue, job, do_sem_wait);
   case V3DV_JOB_TYPE_GPU_TFU:
      return handle_tfu_job(queue, job, do_sem_wait);
   case V3DV_JOB_TYPE_GPU_CSD:
      return handle_csd_job(queue, job, do_sem_wait);
   case V3DV_JOB_TYPE_CPU_RESET_QUERIES:
      return handle_reset_query_cpu_job(job);
   case V3DV_JOB_TYPE_CPU_END_QUERY:
      return handle_end_query_cpu_job(job);
   case V3DV_JOB_TYPE_CPU_COPY_QUERY_RESULTS:
      return handle_copy_query_results_cpu_job(job);
   case V3DV_JOB_TYPE_CPU_SET_EVENT:
      return handle_set_event_cpu_job(job, wait_thread != NULL);
   case V3DV_JOB_TYPE_CPU_WAIT_EVENTS:
      return handle_wait_events_cpu_job(job, do_sem_wait, wait_thread);
   case V3DV_JOB_TYPE_CPU_COPY_BUFFER_TO_IMAGE:
      return handle_copy_buffer_to_image_cpu_job(job);
   case V3DV_JOB_TYPE_CPU_CSD_INDIRECT:
      return handle_csd_indirect_cpu_job(queue, job, do_sem_wait);
   case V3DV_JOB_TYPE_CPU_TIMESTAMP_QUERY:
      return handle_timestamp_query_cpu_job(job);
   default:
      unreachable("Unhandled job type");
   }
}

static void
emit_noop_bin(struct v3dv_job *job)
{
   v3dv_job_start_frame(job, 1, 1, 1, 1, V3D_INTERNAL_BPP_32, false);
   v3dv_job_emit_binning_flush(job);
}

static void
emit_noop_render(struct v3dv_job *job)
{
   struct v3dv_cl *rcl = &job->rcl;
   v3dv_cl_ensure_space_with_branch(rcl, 200 + 1 * 256 *
                                    cl_packet_length(SUPERTILE_COORDINATES));

   cl_emit(rcl, TILE_RENDERING_MODE_CFG_COMMON, config) {
      config.early_z_disable = true;
      config.image_width_pixels = 1;
      config.image_height_pixels = 1;
      config.number_of_render_targets = 1;
      config.multisample_mode_4x = false;
      config.maximum_bpp_of_all_render_targets = V3D_INTERNAL_BPP_32;
   }

   cl_emit(rcl, TILE_RENDERING_MODE_CFG_COLOR, rt) {
      rt.render_target_0_internal_bpp = V3D_INTERNAL_BPP_32;
      rt.render_target_0_internal_type = V3D_INTERNAL_TYPE_8;
      rt.render_target_0_clamp = V3D_RENDER_TARGET_CLAMP_NONE;
   }

   cl_emit(rcl, TILE_RENDERING_MODE_CFG_ZS_CLEAR_VALUES, clear) {
      clear.z_clear_value = 1.0f;
      clear.stencil_clear_value = 0;
   };

   cl_emit(rcl, TILE_LIST_INITIAL_BLOCK_SIZE, init) {
      init.use_auto_chained_tile_lists = true;
      init.size_of_first_block_in_chained_tile_lists =
         TILE_ALLOCATION_BLOCK_SIZE_64B;
   }

   cl_emit(rcl, MULTICORE_RENDERING_TILE_LIST_SET_BASE, list) {
      list.address = v3dv_cl_address(job->tile_alloc, 0);
   }

   cl_emit(rcl, MULTICORE_RENDERING_SUPERTILE_CFG, config) {
      config.number_of_bin_tile_lists = 1;
      config.total_frame_width_in_tiles = 1;
      config.total_frame_height_in_tiles = 1;
      config.supertile_width_in_tiles = 1;
      config.supertile_height_in_tiles = 1;
      config.total_frame_width_in_supertiles = 1;
      config.total_frame_height_in_supertiles = 1;
   }

   struct v3dv_cl *icl = &job->indirect;
   v3dv_cl_ensure_space(icl, 200, 1);
   struct v3dv_cl_reloc tile_list_start = v3dv_cl_get_address(icl);

   cl_emit(icl, TILE_COORDINATES_IMPLICIT, coords);

   cl_emit(icl, END_OF_LOADS, end);

   cl_emit(icl, BRANCH_TO_IMPLICIT_TILE_LIST, branch);

   cl_emit(icl, STORE_TILE_BUFFER_GENERAL, store) {
      store.buffer_to_store = NONE;
   }

   cl_emit(icl, END_OF_TILE_MARKER, end);

   cl_emit(icl, RETURN_FROM_SUB_LIST, ret);

   cl_emit(rcl, START_ADDRESS_OF_GENERIC_TILE_LIST, branch) {
      branch.start = tile_list_start;
      branch.end = v3dv_cl_get_address(icl);
   }

   cl_emit(rcl, SUPERTILE_COORDINATES, coords) {
      coords.column_number_in_supertiles = 0;
      coords.row_number_in_supertiles = 0;
   }

   cl_emit(rcl, END_OF_RENDERING, end);
}

static VkResult
queue_create_noop_job(struct v3dv_queue *queue)
{
   struct v3dv_device *device = queue->device;
   queue->noop_job = vk_zalloc(&device->vk.alloc, sizeof(struct v3dv_job), 8,
                               VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!queue->noop_job)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   v3dv_job_init(queue->noop_job, V3DV_JOB_TYPE_GPU_CL, device, NULL, -1);

   emit_noop_bin(queue->noop_job);
   emit_noop_render(queue->noop_job);

   return VK_SUCCESS;
}

static VkResult
queue_submit_noop_job(struct v3dv_queue *queue, const VkSubmitInfo *pSubmit)
{
   /* VkQueue host access is externally synchronized so we don't need to lock
    * here for the static variable.
    */
   if (!queue->noop_job) {
      VkResult result = queue_create_noop_job(queue);
      if (result != VK_SUCCESS)
         return result;
   }

   return queue_submit_job(queue, queue->noop_job,
                           pSubmit->waitSemaphoreCount > 0, NULL);
}

static VkResult
queue_submit_cmd_buffer(struct v3dv_queue *queue,
                        struct v3dv_cmd_buffer *cmd_buffer,
                        const VkSubmitInfo *pSubmit,
                        pthread_t *wait_thread)
{
   assert(cmd_buffer);
   assert(cmd_buffer->status == V3DV_CMD_BUFFER_STATUS_EXECUTABLE);

   if (list_is_empty(&cmd_buffer->jobs))
      return queue_submit_noop_job(queue, pSubmit);

   list_for_each_entry_safe(struct v3dv_job, job,
                            &cmd_buffer->jobs, list_link) {
      VkResult result = queue_submit_job(queue, job,
                                         pSubmit->waitSemaphoreCount > 0,
                                         wait_thread);
      if (result != VK_SUCCESS)
         return result;
   }

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

   /* FIXME: We put all the semaphores in a list and we signal all of them
    * together from the submit master thread when the last wait thread in the
    * submit completes. We could do better though: group the semaphores per
    * submit and signal them as soon as all wait threads for a particular
    * submit completes. Not sure if the extra work would be worth it though,
    * since we only spawn waith threads for event waits and only when the
    * event if set from the host after the queue submission.
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

   /* Even if we don't have any actual work to submit we still need to wait
    * on the wait semaphores and signal the signal semaphores and fence, so
    * in this scenario we just submit a trivial no-op job so we don't have
    * to do anything special, it should not be a common case anyway.
    */
   if (pSubmit->commandBufferCount == 0) {
      result = queue_submit_noop_job(queue, pSubmit);
   } else {
      for (uint32_t i = 0; i < pSubmit->commandBufferCount; i++) {
         pthread_t wait_thread;
         struct v3dv_cmd_buffer *cmd_buffer =
            v3dv_cmd_buffer_from_handle(pSubmit->pCommandBuffers[i]);
         result = queue_submit_cmd_buffer(queue, cmd_buffer, pSubmit,
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
      result = vk_error(queue->device->instance, VK_ERROR_DEVICE_LOST);
      goto done;
   }

   list_addtail(&wait_info->list_link, &queue->submit_wait_list);

done:
   mtx_unlock(&queue->mutex);
   return result;
}

VkResult
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

VkResult
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
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   sem->fd = -1;

   int ret = drmSyncobjCreate(device->pdevice->render_fd, 0, &sem->sync);
   if (ret) {
      vk_object_free(&device->vk, pAllocator, sem);
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   *pSemaphore = v3dv_semaphore_to_handle(sem);

   return VK_SUCCESS;
}

void
v3dv_DestroySemaphore(VkDevice _device,
                      VkSemaphore semaphore,
                      const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_semaphore, sem, semaphore);

   if (sem == NULL)
      return;

   drmSyncobjDestroy(device->pdevice->render_fd, sem->sync);

   if (sem->fd != -1)
      close(sem->fd);

   vk_object_free(&device->vk, pAllocator, sem);
}

VkResult
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
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   unsigned flags = 0;
   if (pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT)
      flags |= DRM_SYNCOBJ_CREATE_SIGNALED;
   int ret = drmSyncobjCreate(device->pdevice->render_fd, flags, &fence->sync);
   if (ret) {
      vk_object_free(&device->vk, pAllocator, fence);
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   fence->fd = -1;

   *pFence = v3dv_fence_to_handle(fence);

   return VK_SUCCESS;
}

void
v3dv_DestroyFence(VkDevice _device,
                  VkFence _fence,
                  const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_fence, fence, _fence);

   if (fence == NULL)
      return;

   drmSyncobjDestroy(device->pdevice->render_fd, fence->sync);

   if (fence->fd != -1)
      close(fence->fd);

   vk_object_free(&device->vk, pAllocator, fence);
}

VkResult
v3dv_GetFenceStatus(VkDevice _device, VkFence _fence)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_fence, fence, _fence);

   int ret = drmSyncobjWait(device->pdevice->render_fd, &fence->sync, 1,
                            0, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT, NULL);
   if (ret == -ETIME)
      return VK_NOT_READY;
   else if (ret)
      return vk_error(device->instance, VK_ERROR_DEVICE_LOST);
   return VK_SUCCESS;
}

VkResult
v3dv_ResetFences(VkDevice _device, uint32_t fenceCount, const VkFence *pFences)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);

   uint32_t *syncobjs = vk_alloc(&device->vk.alloc,
                                 sizeof(*syncobjs) * fenceCount, 8,
                                 VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!syncobjs)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   for (uint32_t i = 0; i < fenceCount; i++) {
      struct v3dv_fence *fence = v3dv_fence_from_handle(pFences[i]);
      syncobjs[i] = fence->sync;
   }

   int ret = drmSyncobjReset(device->pdevice->render_fd, syncobjs, fenceCount);

   vk_free(&device->vk.alloc, syncobjs);

   if (ret)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   return VK_SUCCESS;
}

VkResult
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
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   for (uint32_t i = 0; i < fenceCount; i++) {
      struct v3dv_fence *fence = v3dv_fence_from_handle(pFences[i]);
      syncobjs[i] = fence->sync;
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
      return vk_error(device->instance, VK_ERROR_DEVICE_LOST);
   return VK_SUCCESS;
}

VkResult
v3dv_QueueBindSparse(VkQueue _queue,
                     uint32_t bindInfoCount,
                     const VkBindSparseInfo *pBindInfo,
                     VkFence fence)
{
   V3DV_FROM_HANDLE(v3dv_queue, queue, _queue);
   return vk_error(queue->device->instance, VK_ERROR_FEATURE_NOT_PRESENT);
}
