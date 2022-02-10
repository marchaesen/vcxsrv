/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_device.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "genxml/gen_macros.h"

#include "decode.h"

#include "panvk_private.h"
#include "panvk_cs.h"

static void
panvk_queue_submit_batch(struct panvk_queue *queue,
                         struct panvk_batch *batch,
                         uint32_t *bos, unsigned nr_bos,
                         uint32_t *in_fences,
                         unsigned nr_in_fences)
{
   const struct panvk_device *dev = queue->device;
   unsigned debug = dev->physical_device->instance->debug_flags;
   const struct panfrost_device *pdev = &dev->physical_device->pdev;
   int ret;

   /* Reset the batch if it's already been issued */
   if (batch->issued) {
      util_dynarray_foreach(&batch->jobs, void *, job)
         memset((*job), 0, 4 * 4);

      /* Reset the tiler before re-issuing the batch */
#if PAN_ARCH >= 6
      if (batch->tiler.descs.cpu) {
         memcpy(batch->tiler.descs.cpu, batch->tiler.templ,
                pan_size(TILER_CONTEXT) + pan_size(TILER_HEAP));
      }
#else
      if (batch->fb.desc.cpu) {
         void *tiler = pan_section_ptr(batch->fb.desc.cpu, FRAMEBUFFER, TILER);
         memcpy(tiler, batch->tiler.templ, pan_size(TILER_CONTEXT));
         /* All weights set to 0, nothing to do here */
         pan_section_pack(batch->fb.desc.cpu, FRAMEBUFFER, TILER_WEIGHTS, w);
      }
#endif
   }

   if (batch->scoreboard.first_job) {
      struct drm_panfrost_submit submit = {
         .bo_handles = (uintptr_t)bos,
         .bo_handle_count = nr_bos,
         .in_syncs = (uintptr_t)in_fences,
         .in_sync_count = nr_in_fences,
         .out_sync = queue->sync,
         .jc = batch->scoreboard.first_job,
      };

      ret = drmIoctl(pdev->fd, DRM_IOCTL_PANFROST_SUBMIT, &submit);
      assert(!ret);

      if (debug & (PANVK_DEBUG_TRACE | PANVK_DEBUG_SYNC)) {
         ret = drmSyncobjWait(pdev->fd, &submit.out_sync, 1, INT64_MAX, 0, NULL);
         assert(!ret);
      }

      if (debug & PANVK_DEBUG_TRACE)
         GENX(pandecode_jc)(batch->scoreboard.first_job, pdev->gpu_id);
   }

   if (batch->fragment_job) {
      struct drm_panfrost_submit submit = {
         .bo_handles = (uintptr_t)bos,
         .bo_handle_count = nr_bos,
         .out_sync = queue->sync,
         .jc = batch->fragment_job,
         .requirements = PANFROST_JD_REQ_FS,
      };

      if (batch->scoreboard.first_job) {
         submit.in_syncs = (uintptr_t)(&queue->sync);
         submit.in_sync_count = 1;
      } else {
         submit.in_syncs = (uintptr_t)in_fences;
         submit.in_sync_count = nr_in_fences;
      }

      ret = drmIoctl(pdev->fd, DRM_IOCTL_PANFROST_SUBMIT, &submit);
      assert(!ret);
      if (debug & (PANVK_DEBUG_TRACE | PANVK_DEBUG_SYNC)) {
         ret = drmSyncobjWait(pdev->fd, &submit.out_sync, 1, INT64_MAX, 0, NULL);
         assert(!ret);
      }

      if (debug & PANVK_DEBUG_TRACE)
         GENX(pandecode_jc)(batch->fragment_job, pdev->gpu_id);
   }

   if (debug & PANVK_DEBUG_TRACE)
      pandecode_next_frame();

   batch->issued = true;
}

static void
panvk_queue_transfer_sync(struct panvk_queue *queue, uint32_t syncobj)
{
   const struct panfrost_device *pdev = &queue->device->physical_device->pdev;
   int ret;

   struct drm_syncobj_handle handle = {
      .handle = queue->sync,
      .flags = DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE,
      .fd = -1,
   };

   ret = drmIoctl(pdev->fd, DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, &handle);
   assert(!ret);
   assert(handle.fd >= 0);

   handle.handle = syncobj;
   ret = drmIoctl(pdev->fd, DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE, &handle);
   assert(!ret);

   close(handle.fd);
}

static void
panvk_add_wait_event_syncobjs(struct panvk_batch *batch, uint32_t *in_fences, unsigned *nr_in_fences)
{
   util_dynarray_foreach(&batch->event_ops, struct panvk_event_op, op) {
      switch (op->type) {
      case PANVK_EVENT_OP_SET:
         /* Nothing to do yet */
         break;
      case PANVK_EVENT_OP_RESET:
         /* Nothing to do yet */
         break;
      case PANVK_EVENT_OP_WAIT:
         in_fences[(*nr_in_fences)++] = op->event->syncobj;
         break;
      default:
         unreachable("bad panvk_event_op type\n");
      }
   }
}

static void
panvk_signal_event_syncobjs(struct panvk_queue *queue, struct panvk_batch *batch)
{
   const struct panfrost_device *pdev = &queue->device->physical_device->pdev;

   util_dynarray_foreach(&batch->event_ops, struct panvk_event_op, op) {
      switch (op->type) {
      case PANVK_EVENT_OP_SET: {
         panvk_queue_transfer_sync(queue, op->event->syncobj);
         break;
      }
      case PANVK_EVENT_OP_RESET: {
         struct panvk_event *event = op->event;

         struct drm_syncobj_array objs = {
            .handles = (uint64_t) (uintptr_t) &event->syncobj,
            .count_handles = 1
         };

         int ret = drmIoctl(pdev->fd, DRM_IOCTL_SYNCOBJ_RESET, &objs);
         assert(!ret);
         break;
      }
      case PANVK_EVENT_OP_WAIT:
         /* Nothing left to do */
         break;
      default:
         unreachable("bad panvk_event_op type\n");
      }
   }
}

VkResult
panvk_per_arch(QueueSubmit)(VkQueue _queue,
                            uint32_t submitCount,
                            const VkSubmitInfo *pSubmits,
                            VkFence _fence)
{
   VK_FROM_HANDLE(panvk_queue, queue, _queue);
   VK_FROM_HANDLE(panvk_fence, fence, _fence);
   const struct panfrost_device *pdev = &queue->device->physical_device->pdev;

   for (uint32_t i = 0; i < submitCount; ++i) {
      const VkSubmitInfo *submit = pSubmits + i;
      unsigned nr_semaphores = submit->waitSemaphoreCount + 1;
      uint32_t semaphores[nr_semaphores];
      
      semaphores[0] = queue->sync;
      for (unsigned i = 0; i < submit->waitSemaphoreCount; i++) {
         VK_FROM_HANDLE(panvk_semaphore, sem, submit->pWaitSemaphores[i]);

         semaphores[i + 1] = sem->syncobj.temporary ? : sem->syncobj.permanent;
      }

      for (uint32_t j = 0; j < submit->commandBufferCount; ++j) {
         VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, (submit->pCommandBuffers[j]));

         list_for_each_entry(struct panvk_batch, batch, &cmdbuf->batches, node) {
            /* FIXME: should be done at the batch level */
            unsigned nr_bos =
               panvk_pool_num_bos(&cmdbuf->desc_pool) +
               panvk_pool_num_bos(&cmdbuf->varying_pool) +
               panvk_pool_num_bos(&cmdbuf->tls_pool) +
               (batch->fb.info ? batch->fb.info->attachment_count : 0) +
               (batch->blit.src ? 1 : 0) +
               (batch->blit.dst ? 1 : 0) +
               (batch->scoreboard.first_tiler ? 1 : 0) + 1;
            unsigned bo_idx = 0;
            uint32_t bos[nr_bos];

            panvk_pool_get_bo_handles(&cmdbuf->desc_pool, &bos[bo_idx]);
            bo_idx += panvk_pool_num_bos(&cmdbuf->desc_pool);

            panvk_pool_get_bo_handles(&cmdbuf->varying_pool, &bos[bo_idx]);
            bo_idx += panvk_pool_num_bos(&cmdbuf->varying_pool);

            panvk_pool_get_bo_handles(&cmdbuf->tls_pool, &bos[bo_idx]);
            bo_idx += panvk_pool_num_bos(&cmdbuf->tls_pool);

            if (batch->fb.info) {
               for (unsigned i = 0; i < batch->fb.info->attachment_count; i++) {
                  bos[bo_idx++] = batch->fb.info->attachments[i].iview->pview.image->data.bo->gem_handle;
               }
            }

            if (batch->blit.src)
               bos[bo_idx++] = batch->blit.src->gem_handle;

            if (batch->blit.dst)
               bos[bo_idx++] = batch->blit.dst->gem_handle;

            if (batch->scoreboard.first_tiler)
               bos[bo_idx++] = pdev->tiler_heap->gem_handle;

            bos[bo_idx++] = pdev->sample_positions->gem_handle;
            assert(bo_idx == nr_bos);

            /* Merge identical BO entries. */
            for (unsigned x = 0; x < nr_bos; x++) {
               for (unsigned y = x + 1; y < nr_bos; ) {
                  if (bos[x] == bos[y])
                     bos[y] = bos[--nr_bos];
                  else
                     y++;
               }
            }

            unsigned nr_in_fences = 0;
            unsigned max_wait_event_syncobjs =
               util_dynarray_num_elements(&batch->event_ops,
                                          struct panvk_event_op);
            uint32_t in_fences[nr_semaphores + max_wait_event_syncobjs];
            memcpy(in_fences, semaphores, nr_semaphores * sizeof(*in_fences));
            nr_in_fences += nr_semaphores;

            panvk_add_wait_event_syncobjs(batch, in_fences, &nr_in_fences);

            panvk_queue_submit_batch(queue, batch, bos, nr_bos, in_fences, nr_in_fences);

            panvk_signal_event_syncobjs(queue, batch);
         }
      }

      /* Transfer the out fence to signal semaphores */
      for (unsigned i = 0; i < submit->signalSemaphoreCount; i++) {
         VK_FROM_HANDLE(panvk_semaphore, sem, submit->pSignalSemaphores[i]);
         panvk_queue_transfer_sync(queue, sem->syncobj.temporary ? : sem->syncobj.permanent);
      }
   }

   if (fence) {
      /* Transfer the last out fence to the fence object */
      panvk_queue_transfer_sync(queue, fence->syncobj.temporary ? : fence->syncobj.permanent);
   }

   return VK_SUCCESS;
}

VkResult
panvk_per_arch(CreateSampler)(VkDevice _device,
                              const VkSamplerCreateInfo *pCreateInfo,
                              const VkAllocationCallbacks *pAllocator,
                              VkSampler *pSampler)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk_sampler *sampler;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);

   sampler = vk_object_alloc(&device->vk, pAllocator, sizeof(*sampler),
                             VK_OBJECT_TYPE_SAMPLER);
   if (!sampler)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   STATIC_ASSERT(sizeof(sampler->desc) >= pan_size(SAMPLER));
   panvk_per_arch(emit_sampler)(pCreateInfo, &sampler->desc);
   *pSampler = panvk_sampler_to_handle(sampler);

   return VK_SUCCESS;
}
