/*
 * Copyright © 2019 Raspberry Pi Ltd
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
#include "util/libsync.h"
#include "util/os_time.h"
#include "vk_drm_syncobj.h"

#include <errno.h>
#include <time.h>

static void
v3dv_clif_dump(struct v3dv_device *device,
               struct v3dv_job *job,
               struct drm_v3d_submit_cl *submit)
{
   if (!(V3D_DBG(CL) ||
         V3D_DBG(CL_NO_BIN) ||
         V3D_DBG(CLIF)))
      return;

   struct clif_dump *clif = clif_dump_init(&device->devinfo,
                                           stderr,
                                           V3D_DBG(CL) ||
                                           V3D_DBG(CL_NO_BIN),
                                           V3D_DBG(CL_NO_BIN));

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

static VkResult
queue_wait_idle(struct v3dv_queue *queue,
                struct v3dv_submit_sync_info *sync_info)
{
   if (queue->device->pdevice->caps.multisync) {
      int ret = drmSyncobjWait(queue->device->pdevice->render_fd,
                               queue->last_job_syncs.syncs, 3,
                               INT64_MAX, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL,
                               NULL);
      if (ret) {
         return vk_errorf(queue, VK_ERROR_DEVICE_LOST,
                          "syncobj wait failed: %m");
      }

      bool first = true;
      for (int i = 0; i < 3; i++) {
         if (!queue->last_job_syncs.first[i])
            first = false;
      }

      /* If we're not the first job, that means we're waiting on some
       * per-queue-type syncobj which transitively waited on the semaphores
       * so we can skip the semaphore wait.
       */
      if (first) {
         VkResult result = vk_sync_wait_many(&queue->device->vk,
                                             sync_info->wait_count,
                                             sync_info->waits,
                                             VK_SYNC_WAIT_COMPLETE,
                                             UINT64_MAX);
         if (result != VK_SUCCESS)
            return result;
      }
   } else {
      /* Without multisync, all the semaphores are baked into the one syncobj
       * at the start of each submit so we only need to wait on the one.
       */
      int ret = drmSyncobjWait(queue->device->pdevice->render_fd,
                               &queue->last_job_syncs.syncs[V3DV_QUEUE_ANY], 1,
                               INT64_MAX, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL,
                               NULL);
      if (ret) {
         return vk_errorf(queue, VK_ERROR_DEVICE_LOST,
                          "syncobj wait failed: %m");
      }
   }

   for (int i = 0; i < 3; i++)
      queue->last_job_syncs.first[i] = false;

   return VK_SUCCESS;
}

static VkResult
handle_reset_query_cpu_job(struct v3dv_queue *queue, struct v3dv_job *job,
                           struct v3dv_submit_sync_info *sync_info)
{
   struct v3dv_reset_query_cpu_job_info *info = &job->cpu.query_reset;
   assert(info->pool);

   /* We are about to reset query counters so we need to make sure that
    * The GPU is not using them. The exception is timestamp queries, since
    * we handle those in the CPU.
    */
   if (info->pool->query_type == VK_QUERY_TYPE_OCCLUSION)
      v3dv_bo_wait(job->device, info->pool->bo, PIPE_TIMEOUT_INFINITE);

   if (info->pool->query_type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR) {
      struct vk_sync_wait waits[info->count];
      unsigned wait_count = 0;
      for (int i = 0; i < info->count; i++) {
         struct v3dv_query *query = &info->pool->queries[i];
         /* Only wait for a query if we've used it otherwise we will be
          * waiting forever for the fence to become signaled.
          */
         if (query->maybe_available) {
            waits[wait_count] = (struct vk_sync_wait){
               .sync = info->pool->queries[i].perf.last_job_sync
            };
            wait_count++;
         };
      }

      VkResult result = vk_sync_wait_many(&job->device->vk, wait_count, waits,
                                          VK_SYNC_WAIT_COMPLETE, UINT64_MAX);

      if (result != VK_SUCCESS)
         return result;
   }

   v3dv_reset_query_pools(job->device, info->pool, info->first, info->count);

   return VK_SUCCESS;
}

static VkResult
export_perfmon_last_job_sync(struct v3dv_queue *queue, struct v3dv_job *job, int *fd)
{
   int err;
   if (job->device->pdevice->caps.multisync) {
      static const enum v3dv_queue_type queues_to_sync[] = {
         V3DV_QUEUE_CL,
         V3DV_QUEUE_CSD,
      };

      for (uint32_t i = 0; i < ARRAY_SIZE(queues_to_sync); i++) {
         enum v3dv_queue_type queue_type = queues_to_sync[i];
         int tmp_fd = -1;

         err = drmSyncobjExportSyncFile(job->device->pdevice->render_fd,
                                        queue->last_job_syncs.syncs[queue_type],
                                        &tmp_fd);

         if (err) {
            close(*fd);
            return vk_errorf(&job->device->queue, VK_ERROR_UNKNOWN,
                             "sync file export failed: %m");
         }

         err = sync_accumulate("v3dv", fd, tmp_fd);

         if (err) {
            close(tmp_fd);
            close(*fd);
            return vk_errorf(&job->device->queue, VK_ERROR_UNKNOWN,
                             "failed to accumulate sync files: %m");
         }
      }
   } else {
      err = drmSyncobjExportSyncFile(job->device->pdevice->render_fd,
                                     queue->last_job_syncs.syncs[V3DV_QUEUE_ANY],
                                     fd);

      if (err) {
         return vk_errorf(&job->device->queue, VK_ERROR_UNKNOWN,
                          "sync file export failed: %m");
      }
   }
   return VK_SUCCESS;
}

static VkResult
handle_end_query_cpu_job(struct v3dv_job *job, uint32_t counter_pass_idx)
{
   VkResult result = VK_SUCCESS;

   mtx_lock(&job->device->query_mutex);

   struct v3dv_end_query_cpu_job_info *info = &job->cpu.query_end;
   struct v3dv_queue *queue = &job->device->queue;

   int err = 0;
   int fd = -1;

   if (info->pool->query_type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR) {
      result = export_perfmon_last_job_sync(queue, job, &fd);

      if (result != VK_SUCCESS)
         goto fail;

      assert(fd >= 0);
   }

   for (uint32_t i = 0; i < info->count; i++) {
      assert(info->query + i < info->pool->query_count);
      struct v3dv_query *query = &info->pool->queries[info->query + i];

      if (info->pool->query_type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR) {
         uint32_t syncobj = vk_sync_as_drm_syncobj(query->perf.last_job_sync)->syncobj;
         err = drmSyncobjImportSyncFile(job->device->pdevice->render_fd,
                                        syncobj, fd);

         if (err) {
            result = vk_errorf(queue, VK_ERROR_UNKNOWN,
                               "sync file import failed: %m");
            goto fail;
         }
      }

      query->maybe_available = true;
   }

fail:
   if (info->pool->query_type == VK_QUERY_TYPE_PERFORMANCE_QUERY_KHR)
      close(fd);

   cnd_broadcast(&job->device->query_ended);
   mtx_unlock(&job->device->query_mutex);

   return result;
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

   uint8_t *offset = ((uint8_t *) bo->map) +
                     info->offset + info->dst->mem_offset;
   v3dv_get_query_pool_results(job->device,
                               info->pool,
                               info->first,
                               info->count,
                               offset,
                               info->stride,
                               info->flags);

   return VK_SUCCESS;
}

static VkResult
handle_set_event_cpu_job(struct v3dv_queue *queue, struct v3dv_job *job,
                         struct v3dv_submit_sync_info *sync_info)
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
    */

   VkResult result = queue_wait_idle(queue, sync_info);
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

static VkResult
handle_wait_events_cpu_job(struct v3dv_job *job)
{
   assert(job->type == V3DV_JOB_TYPE_CPU_WAIT_EVENTS);

   /* Wait for events to be signaled */
   const useconds_t wait_interval_ms = 1;
   while (!check_wait_events_complete(job))
      usleep(wait_interval_ms * 1000);

   return VK_SUCCESS;
}

static VkResult
handle_copy_buffer_to_image_cpu_job(struct v3dv_queue *queue,
                                    struct v3dv_job *job,
                                    struct v3dv_submit_sync_info *sync_info)
{
   assert(job->type == V3DV_JOB_TYPE_CPU_COPY_BUFFER_TO_IMAGE);
   struct v3dv_copy_buffer_to_image_cpu_job_info *info =
      &job->cpu.copy_buffer_to_image;

   /* Wait for all GPU work to finish first, since we may be accessing
    * the BOs involved in the operation.
    */
   VkResult result = queue_wait_idle(queue, sync_info);
   if (result != VK_SUCCESS)
      return result;

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
handle_timestamp_query_cpu_job(struct v3dv_queue *queue, struct v3dv_job *job,
                               struct v3dv_submit_sync_info *sync_info)
{
   assert(job->type == V3DV_JOB_TYPE_CPU_TIMESTAMP_QUERY);
   struct v3dv_timestamp_query_cpu_job_info *info = &job->cpu.query_timestamp;

   /* Wait for completion of all work queued before the timestamp query */
   VkResult result = queue_wait_idle(queue, sync_info);
   if (result != VK_SUCCESS)
      return result;

   mtx_lock(&job->device->query_mutex);

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

   cnd_broadcast(&job->device->query_ended);
   mtx_unlock(&job->device->query_mutex);

   return VK_SUCCESS;
}

static VkResult
handle_csd_indirect_cpu_job(struct v3dv_queue *queue,
                            struct v3dv_job *job,
                            struct v3dv_submit_sync_info *sync_info)
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

   return VK_SUCCESS;
}

static VkResult
process_waits(struct v3dv_queue *queue,
              uint32_t count, struct vk_sync_wait *waits)
{
   struct v3dv_device *device = queue->device;
   VkResult result = VK_SUCCESS;
   int err = 0;

   if (count == 0)
      return VK_SUCCESS;

   /* If multisync is supported, we wait on semaphores in the first job
    * submitted to each of the individual queues.  We don't need to
    * pre-populate the syncobjs.
    */
   if (queue->device->pdevice->caps.multisync)
      return VK_SUCCESS;

   int fd = -1;
   err = drmSyncobjExportSyncFile(device->pdevice->render_fd,
                                  queue->last_job_syncs.syncs[V3DV_QUEUE_ANY],
                                  &fd);
   if (err) {
      result = vk_errorf(queue, VK_ERROR_UNKNOWN,
                         "sync file export failed: %m");
      goto fail;
   }

   for (uint32_t i = 0; i < count; i++) {
      uint32_t syncobj = vk_sync_as_drm_syncobj(waits[i].sync)->syncobj;
      int wait_fd = -1;

      err = drmSyncobjExportSyncFile(device->pdevice->render_fd,
                                     syncobj, &wait_fd);
      if (err) {
         result = vk_errorf(queue, VK_ERROR_UNKNOWN,
                            "sync file export failed: %m");
         goto fail;
      }

      err = sync_accumulate("v3dv", &fd, wait_fd);
      close(wait_fd);
      if (err) {
         result = vk_errorf(queue, VK_ERROR_UNKNOWN,
                            "sync file merge failed: %m");
         goto fail;
      }
   }

   err = drmSyncobjImportSyncFile(device->pdevice->render_fd,
                                  queue->last_job_syncs.syncs[V3DV_QUEUE_ANY],
                                  fd);
   if (err) {
      result = vk_errorf(queue, VK_ERROR_UNKNOWN,
                         "sync file import failed: %m");
   }

fail:
   close(fd);
   return result;
}

static VkResult
process_signals(struct v3dv_queue *queue,
                uint32_t count, struct vk_sync_signal *signals)
{
   struct v3dv_device *device = queue->device;

   if (count == 0)
      return VK_SUCCESS;

   /* If multisync is supported, we are signalling semaphores in the last job
    * of the last command buffer and, therefore, we do not need to process any
    * semaphores here.
    */
   if (device->pdevice->caps.multisync)
      return VK_SUCCESS;

   int fd;
   drmSyncobjExportSyncFile(device->pdevice->render_fd,
                            queue->last_job_syncs.syncs[V3DV_QUEUE_ANY],
                            &fd);
   if (fd == -1) {
      return vk_errorf(queue, VK_ERROR_UNKNOWN,
                       "sync file export failed: %m");
   }

   VkResult result = VK_SUCCESS;
   for (uint32_t i = 0; i < count; i++) {
      uint32_t syncobj = vk_sync_as_drm_syncobj(signals[i].sync)->syncobj;
      int err = drmSyncobjImportSyncFile(device->pdevice->render_fd,
                                         syncobj, fd);
      if (err) {
         result = vk_errorf(queue, VK_ERROR_UNKNOWN,
                            "sync file import failed: %m");
         break;
      }
   }

   assert(fd >= 0);
   close(fd);

   return result;
}

static void
multisync_free(struct v3dv_device *device,
               struct drm_v3d_multi_sync *ms)
{
   vk_free(&device->vk.alloc, (void *)(uintptr_t)ms->out_syncs);
   vk_free(&device->vk.alloc, (void *)(uintptr_t)ms->in_syncs);
}

static struct drm_v3d_sem *
set_in_syncs(struct v3dv_queue *queue,
             struct v3dv_job *job,
             enum v3dv_queue_type queue_sync,
             uint32_t *count,
             struct v3dv_submit_sync_info *sync_info)
{
   struct v3dv_device *device = queue->device;
   uint32_t n_syncs = 0;

   /* If this is the first job submitted to a given GPU queue in this cmd buf
    * batch, it has to wait on wait semaphores (if any) before running.
    */
   if (queue->last_job_syncs.first[queue_sync])
      n_syncs = sync_info->wait_count;

   /* If the serialize flag is set the job needs to be serialized in the
    * corresponding queues. Notice that we may implement transfer operations
    * as both CL or TFU jobs.
    *
    * FIXME: maybe we could track more precisely if the source of a transfer
    * barrier is a CL and/or a TFU job.
    */
   bool sync_csd  = job->serialize & V3DV_BARRIER_COMPUTE_BIT;
   bool sync_tfu  = job->serialize & V3DV_BARRIER_TRANSFER_BIT;
   bool sync_cl   = job->serialize & (V3DV_BARRIER_GRAPHICS_BIT |
                                      V3DV_BARRIER_TRANSFER_BIT);
   *count = n_syncs;
   if (sync_cl)
      (*count)++;
   if (sync_tfu)
      (*count)++;
   if (sync_csd)
      (*count)++;

   if (!*count)
      return NULL;

   struct drm_v3d_sem *syncs =
      vk_zalloc(&device->vk.alloc, *count * sizeof(struct drm_v3d_sem),
                8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);

   if (!syncs)
      return NULL;

   for (int i = 0; i < n_syncs; i++) {
      syncs[i].handle =
         vk_sync_as_drm_syncobj(sync_info->waits[i].sync)->syncobj;
   }

   if (sync_cl)
      syncs[n_syncs++].handle = queue->last_job_syncs.syncs[V3DV_QUEUE_CL];

   if (sync_csd)
      syncs[n_syncs++].handle = queue->last_job_syncs.syncs[V3DV_QUEUE_CSD];

   if (sync_tfu)
      syncs[n_syncs++].handle = queue->last_job_syncs.syncs[V3DV_QUEUE_TFU];

   assert(n_syncs == *count);
   return syncs;
}

static struct drm_v3d_sem *
set_out_syncs(struct v3dv_queue *queue,
              struct v3dv_job *job,
              enum v3dv_queue_type queue_sync,
              uint32_t *count,
              struct v3dv_submit_sync_info *sync_info,
              bool signal_syncs)
{
   struct v3dv_device *device = queue->device;

   uint32_t n_vk_syncs = signal_syncs ? sync_info->signal_count : 0;

   /* We always signal the syncobj from `device->last_job_syncs` related to
    * this v3dv_queue_type to track the last job submitted to this queue.
    */
   (*count) = n_vk_syncs + 1;

   struct drm_v3d_sem *syncs =
      vk_zalloc(&device->vk.alloc, *count * sizeof(struct drm_v3d_sem),
                8, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);

   if (!syncs)
      return NULL;

   if (n_vk_syncs) {
      for (unsigned i = 0; i < n_vk_syncs; i++) {
         syncs[i].handle =
            vk_sync_as_drm_syncobj(sync_info->signals[i].sync)->syncobj;
      }
   }

   syncs[n_vk_syncs].handle = queue->last_job_syncs.syncs[queue_sync];

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
              struct v3dv_submit_sync_info *sync_info,
              struct drm_v3d_extension *next,
              struct v3dv_device *device,
              struct v3dv_job *job,
              enum v3dv_queue_type queue_sync,
              enum v3d_queue wait_stage,
              bool signal_syncs)
{
   struct v3dv_queue *queue = &device->queue;
   uint32_t out_sync_count = 0, in_sync_count = 0;
   struct drm_v3d_sem *out_syncs = NULL, *in_syncs = NULL;

   in_syncs = set_in_syncs(queue, job, queue_sync,
                           &in_sync_count, sync_info);
   if (!in_syncs && in_sync_count)
      goto fail;

   out_syncs = set_out_syncs(queue, job, queue_sync,
                             &out_sync_count, sync_info, signal_syncs);

   assert(out_sync_count > 0);

   if (!out_syncs)
      goto fail;

   set_ext(&ms->base, next, DRM_V3D_EXT_ID_MULTI_SYNC, 0);
   ms->wait_stage = wait_stage;
   ms->out_sync_count = out_sync_count;
   ms->out_syncs = (uintptr_t)(void *)out_syncs;
   ms->in_sync_count = in_sync_count;
   ms->in_syncs = (uintptr_t)(void *)in_syncs;

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
              uint32_t counter_pass_idx,
              struct v3dv_submit_sync_info *sync_info,
              bool signal_syncs)
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

   /* If the job uses VK_KHR_buffer_device_addess we need to ensure all
    * buffers flagged with VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR
    * are included.
    */
   if (job->uses_buffer_device_address) {
      util_dynarray_foreach(&queue->device->device_address_bo_list,
                            struct v3dv_bo *, bo) {
         v3dv_job_add_bo(job, *bo);
      }
   }

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

   submit.perfmon_id = job->perf ?
      job->perf->kperfmon_ids[counter_pass_idx] : 0;
   const bool needs_perf_sync = queue->last_perfmon_id != submit.perfmon_id;
   queue->last_perfmon_id = submit.perfmon_id;

   /* We need a binning sync if we are the first CL job waiting on a semaphore
    * with a wait stage that involves the geometry pipeline, or if the job
    * comes after a pipeline barrier that involves geometry stages
    * (needs_bcl_sync) or when performance queries are in use.
    *
    * We need a render sync if the job doesn't need a binning sync but has
    * still been flagged for serialization. It should be noted that RCL jobs
    * don't start until the previous RCL job has finished so we don't really
    * need to add a fence for those, however, we might need to wait on a CSD or
    * TFU job, which are not automatically serialized with CL jobs.
    */
   bool needs_bcl_sync = job->needs_bcl_sync || needs_perf_sync;
   if (queue->last_job_syncs.first[V3DV_QUEUE_CL]) {
      for (int i = 0; !needs_bcl_sync && i < sync_info->wait_count; i++) {
         needs_bcl_sync = sync_info->waits[i].stage_mask &
             (VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT |
              VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT |
              VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT |
              VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
              VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
              VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
              VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT |
              VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
              VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT |
              VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT |
              VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT |
              VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT);
      }
   }

   bool needs_rcl_sync = job->serialize && !needs_bcl_sync;

   /* Replace single semaphore settings whenever our kernel-driver supports
    * multiple semaphores extension.
    */
   struct drm_v3d_multi_sync ms = { 0 };
   if (device->pdevice->caps.multisync) {
      enum v3d_queue wait_stage = needs_rcl_sync ? V3D_RENDER : V3D_BIN;
      set_multisync(&ms, sync_info, NULL, device, job,
                    V3DV_QUEUE_CL, wait_stage, signal_syncs);
      if (!ms.base.id)
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

      submit.flags |= DRM_V3D_SUBMIT_EXTENSION;
      submit.extensions = (uintptr_t)(void *)&ms;
      /* Disable legacy sync interface when multisync extension is used */
      submit.in_sync_rcl = 0;
      submit.in_sync_bcl = 0;
      submit.out_sync = 0;
   } else {
      uint32_t last_job_sync = queue->last_job_syncs.syncs[V3DV_QUEUE_ANY];
      submit.in_sync_bcl = needs_bcl_sync ? last_job_sync : 0;
      submit.in_sync_rcl = needs_rcl_sync ? last_job_sync : 0;
      submit.out_sync = last_job_sync;
   }

   v3dv_clif_dump(device, job, &submit);
   int ret = v3dv_ioctl(device->pdevice->render_fd,
                        DRM_IOCTL_V3D_SUBMIT_CL, &submit);

   static bool warned = false;
   if (ret && !warned) {
      fprintf(stderr, "Draw call returned %s. Expect corruption.\n",
              strerror(errno));
      warned = true;
   }

   free(bo_handles);
   multisync_free(device, &ms);

   queue->last_job_syncs.first[V3DV_QUEUE_CL] = false;

   if (ret)
      return vk_queue_set_lost(&queue->vk, "V3D_SUBMIT_CL failed: %m");

   return VK_SUCCESS;
}

static VkResult
handle_tfu_job(struct v3dv_queue *queue,
               struct v3dv_job *job,
               struct v3dv_submit_sync_info *sync_info,
               bool signal_syncs)
{
   struct v3dv_device *device = queue->device;

   const bool needs_sync = sync_info->wait_count || job->serialize;

   /* Replace single semaphore settings whenever our kernel-driver supports
    * multiple semaphore extension.
    */
   struct drm_v3d_multi_sync ms = { 0 };
   if (device->pdevice->caps.multisync) {
      set_multisync(&ms, sync_info, NULL, device, job,
                    V3DV_QUEUE_TFU, V3D_TFU, signal_syncs);
      if (!ms.base.id)
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

      job->tfu.flags |= DRM_V3D_SUBMIT_EXTENSION;
      job->tfu.extensions = (uintptr_t)(void *)&ms;
      /* Disable legacy sync interface when multisync extension is used */
      job->tfu.in_sync = 0;
      job->tfu.out_sync = 0;
   } else {
      uint32_t last_job_sync = queue->last_job_syncs.syncs[V3DV_QUEUE_ANY];
      job->tfu.in_sync = needs_sync ? last_job_sync : 0;
      job->tfu.out_sync = last_job_sync;
   }
   int ret = v3dv_ioctl(device->pdevice->render_fd,
                        DRM_IOCTL_V3D_SUBMIT_TFU, &job->tfu);

   multisync_free(device, &ms);
   queue->last_job_syncs.first[V3DV_QUEUE_TFU] = false;

   if (ret != 0)
      return vk_queue_set_lost(&queue->vk, "V3D_SUBMIT_TFU failed: %m");

   return VK_SUCCESS;
}

static VkResult
handle_csd_job(struct v3dv_queue *queue,
               struct v3dv_job *job,
               uint32_t counter_pass_idx,
               struct v3dv_submit_sync_info *sync_info,
               bool signal_syncs)
{
   struct v3dv_device *device = queue->device;

   struct drm_v3d_submit_csd *submit = &job->csd.submit;

   /* If the job uses VK_KHR_buffer_device_addess we need to ensure all
    * buffers flagged with VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT_KHR
    * are included.
    */
   if (job->uses_buffer_device_address) {
      util_dynarray_foreach(&queue->device->device_address_bo_list,
                            struct v3dv_bo *, bo) {
         v3dv_job_add_bo(job, *bo);
      }
   }

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

   const bool needs_sync = sync_info->wait_count || job->serialize;

   /* Replace single semaphore settings whenever our kernel-driver supports
    * multiple semaphore extension.
    */
   struct drm_v3d_multi_sync ms = { 0 };
   if (device->pdevice->caps.multisync) {
      set_multisync(&ms, sync_info, NULL, device, job,
                    V3DV_QUEUE_CSD, V3D_CSD, signal_syncs);
      if (!ms.base.id)
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

      submit->flags |= DRM_V3D_SUBMIT_EXTENSION;
      submit->extensions = (uintptr_t)(void *)&ms;
      /* Disable legacy sync interface when multisync extension is used */
      submit->in_sync = 0;
      submit->out_sync = 0;
   } else {
      uint32_t last_job_sync = queue->last_job_syncs.syncs[V3DV_QUEUE_ANY];
      submit->in_sync = needs_sync ? last_job_sync : 0;
      submit->out_sync = last_job_sync;
   }
   submit->perfmon_id = job->perf ?
      job->perf->kperfmon_ids[counter_pass_idx] : 0;
   queue->last_perfmon_id = submit->perfmon_id;
   int ret = v3dv_ioctl(device->pdevice->render_fd,
                        DRM_IOCTL_V3D_SUBMIT_CSD, submit);

   static bool warned = false;
   if (ret && !warned) {
      fprintf(stderr, "Compute dispatch returned %s. Expect corruption.\n",
              strerror(errno));
      warned = true;
   }

   free(bo_handles);

   multisync_free(device, &ms);
   queue->last_job_syncs.first[V3DV_QUEUE_CSD] = false;

   if (ret)
      return vk_queue_set_lost(&queue->vk, "V3D_SUBMIT_CSD failed: %m");

   return VK_SUCCESS;
}

static VkResult
queue_handle_job(struct v3dv_queue *queue,
                 struct v3dv_job *job,
                 uint32_t counter_pass_idx,
                 struct v3dv_submit_sync_info *sync_info,
                 bool signal_syncs)
{
   switch (job->type) {
   case V3DV_JOB_TYPE_GPU_CL:
      return handle_cl_job(queue, job, counter_pass_idx, sync_info, signal_syncs);
   case V3DV_JOB_TYPE_GPU_TFU:
      return handle_tfu_job(queue, job, sync_info, signal_syncs);
   case V3DV_JOB_TYPE_GPU_CSD:
      return handle_csd_job(queue, job, counter_pass_idx, sync_info, signal_syncs);
   case V3DV_JOB_TYPE_CPU_RESET_QUERIES:
      return handle_reset_query_cpu_job(queue, job, sync_info);
   case V3DV_JOB_TYPE_CPU_END_QUERY:
      return handle_end_query_cpu_job(job, counter_pass_idx);
   case V3DV_JOB_TYPE_CPU_COPY_QUERY_RESULTS:
      return handle_copy_query_results_cpu_job(job);
   case V3DV_JOB_TYPE_CPU_SET_EVENT:
      return handle_set_event_cpu_job(queue, job, sync_info);
   case V3DV_JOB_TYPE_CPU_WAIT_EVENTS:
      return handle_wait_events_cpu_job(job);
   case V3DV_JOB_TYPE_CPU_COPY_BUFFER_TO_IMAGE:
      return handle_copy_buffer_to_image_cpu_job(queue, job, sync_info);
   case V3DV_JOB_TYPE_CPU_CSD_INDIRECT:
      return handle_csd_indirect_cpu_job(queue, job, sync_info);
   case V3DV_JOB_TYPE_CPU_TIMESTAMP_QUERY:
      return handle_timestamp_query_cpu_job(queue, job, sync_info);
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

   /* We use no-op jobs to signal semaphores/fences. These jobs needs to be
    * serialized across all hw queues to comply with Vulkan's signal operation
    * order requirements, which basically require that signal operations occur
    * in submission order.
    */
   queue->noop_job->serialize = V3DV_BARRIER_ALL;

   return VK_SUCCESS;
}

static VkResult
queue_submit_noop_job(struct v3dv_queue *queue,
                      uint32_t counter_pass_idx,
                      struct v3dv_submit_sync_info *sync_info,
                      bool signal_syncs)
{
   if (!queue->noop_job) {
      VkResult result = queue_create_noop_job(queue);
      if (result != VK_SUCCESS)
         return result;
   }

   assert(queue->noop_job);
   return queue_handle_job(queue, queue->noop_job, counter_pass_idx,
                           sync_info, signal_syncs);
}

VkResult
v3dv_queue_driver_submit(struct vk_queue *vk_queue,
                         struct vk_queue_submit *submit)
{
   struct v3dv_queue *queue = container_of(vk_queue, struct v3dv_queue, vk);
   VkResult result;

   struct v3dv_submit_sync_info sync_info = {
      .wait_count = submit->wait_count,
      .waits = submit->waits,
      .signal_count = submit->signal_count,
      .signals = submit->signals,
   };

   for (int i = 0; i < V3DV_QUEUE_COUNT; i++)
      queue->last_job_syncs.first[i] = true;

   result = process_waits(queue, sync_info.wait_count, sync_info.waits);
   if (result != VK_SUCCESS)
      return result;

   for (uint32_t i = 0; i < submit->command_buffer_count; i++) {
      struct v3dv_cmd_buffer *cmd_buffer =
         container_of(submit->command_buffers[i], struct v3dv_cmd_buffer, vk);
      list_for_each_entry_safe(struct v3dv_job, job,
                               &cmd_buffer->jobs, list_link) {

         result = queue_handle_job(queue, job, submit->perf_pass_index,
                                   &sync_info, false);
         if (result != VK_SUCCESS)
            return result;
      }

      /* If the command buffer ends with a barrier we need to consume it now.
       *
       * FIXME: this will drain all hw queues. Instead, we could use the pending
       * barrier state to limit the queues we serialize against.
       */
      if (cmd_buffer->state.barrier.dst_mask) {
         result = queue_submit_noop_job(queue, submit->perf_pass_index,
                                        &sync_info, false);
         if (result != VK_SUCCESS)
            return result;
      }
   }

   /* Finish by submitting a no-op job that synchronizes across all queues.
    * This will ensure that the signal semaphores don't get triggered until
    * all work on any queue completes. See Vulkan's signal operation order
    * requirements.
    */
   if (submit->signal_count > 0) {
      result = queue_submit_noop_job(queue, submit->perf_pass_index,
                                     &sync_info, true);
      if (result != VK_SUCCESS)
         return result;
   }

   process_signals(queue, sync_info.signal_count, sync_info.signals);

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
