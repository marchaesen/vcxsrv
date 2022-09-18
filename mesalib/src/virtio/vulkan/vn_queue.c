/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_queue.h"

#include "util/libsync.h"
#include "venus-protocol/vn_protocol_driver_event.h"
#include "venus-protocol/vn_protocol_driver_fence.h"
#include "venus-protocol/vn_protocol_driver_queue.h"
#include "venus-protocol/vn_protocol_driver_semaphore.h"

#include "vn_device.h"
#include "vn_device_memory.h"
#include "vn_physical_device.h"
#include "vn_renderer.h"
#include "vn_wsi.h"

/* queue commands */

void
vn_GetDeviceQueue2(VkDevice device,
                   const VkDeviceQueueInfo2 *pQueueInfo,
                   VkQueue *pQueue)
{
   struct vn_device *dev = vn_device_from_handle(device);

   for (uint32_t i = 0; i < dev->queue_count; i++) {
      struct vn_queue *queue = &dev->queues[i];
      if (queue->family == pQueueInfo->queueFamilyIndex &&
          queue->index == pQueueInfo->queueIndex &&
          queue->flags == pQueueInfo->flags) {
         *pQueue = vn_queue_to_handle(queue);
         return;
      }
   }
   unreachable("bad queue family/index");
}

static bool
vn_semaphore_wait_external(struct vn_device *dev, struct vn_semaphore *sem);

struct vn_queue_submission {
   VkStructureType batch_type;
   VkQueue queue;
   uint32_t batch_count;
   union {
      const void *batches;
      const VkSubmitInfo *submit_batches;
      const VkBindSparseInfo *bind_sparse_batches;
   };
   VkFence fence;

   bool synchronous;
   bool has_feedback_fence;
   const struct vn_device_memory *wsi_mem;
   uint32_t wait_semaphore_count;
   uint32_t wait_external_count;

   struct {
      void *storage;

      union {
         void *batches;
         VkSubmitInfo *submit_batches;
         VkBindSparseInfo *bind_sparse_batches;
      };
      VkSemaphore *semaphores;
   } temp;
};

static VkResult
vn_queue_submission_count_batch_semaphores(struct vn_queue_submission *submit,
                                           uint32_t batch_index)
{
   union {
      const VkSubmitInfo *submit_batch;
      const VkBindSparseInfo *bind_sparse_batch;
   } u;
   uint32_t wait_count;
   uint32_t signal_count;
   const VkSemaphore *wait_sems;
   const VkSemaphore *signal_sems;

   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO:
      u.submit_batch = &submit->submit_batches[batch_index];
      wait_count = u.submit_batch->waitSemaphoreCount;
      wait_sems = u.submit_batch->pWaitSemaphores;
      signal_count = u.submit_batch->signalSemaphoreCount;
      signal_sems = u.submit_batch->pSignalSemaphores;
      break;
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
      u.bind_sparse_batch = &submit->bind_sparse_batches[batch_index];
      wait_count = u.bind_sparse_batch->waitSemaphoreCount;
      wait_sems = u.bind_sparse_batch->pWaitSemaphores;
      signal_count = u.bind_sparse_batch->signalSemaphoreCount;
      signal_sems = u.bind_sparse_batch->pSignalSemaphores;
      break;
   default:
      unreachable("unexpected batch type");
      break;
   }

   submit->wait_semaphore_count += wait_count;
   for (uint32_t i = 0; i < wait_count; i++) {
      struct vn_semaphore *sem = vn_semaphore_from_handle(wait_sems[i]);
      const struct vn_sync_payload *payload = sem->payload;

      if (payload->type != VN_SYNC_TYPE_IMPORTED_SYNC_FD)
         continue;

      struct vn_queue *queue = vn_queue_from_handle(submit->queue);
      struct vn_device *dev = queue->device;
      if (dev->physical_device->renderer_sync_fd_semaphore_features &
          VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT) {
         if (!vn_semaphore_wait_external(dev, sem))
            return VK_ERROR_DEVICE_LOST;

         const VkImportSemaphoreResourceInfo100000MESA res_info = {
            .sType =
               VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_RESOURCE_INFO_100000_MESA,
            .semaphore = wait_sems[i],
            .resourceId = 0,
         };
         vn_async_vkImportSemaphoreResource100000MESA(
            dev->instance, vn_device_to_handle(dev), &res_info);
      } else {
         submit->wait_external_count++;
      }
   }

   for (uint32_t i = 0; i < signal_count; i++) {
      struct vn_semaphore *sem = vn_semaphore_from_handle(signal_sems[i]);

      /* see vn_queue_submission_prepare */
      submit->synchronous |= sem->is_external;
   }

   return VK_SUCCESS;
}

static VkResult
vn_queue_submission_prepare(struct vn_queue_submission *submit)
{
   struct vn_fence *fence = vn_fence_from_handle(submit->fence);
   const bool has_external_fence = fence && fence->is_external;

   submit->has_feedback_fence = fence && fence->feedback.slot;
   assert(!has_external_fence || !submit->has_feedback_fence);

   submit->wsi_mem = NULL;
   if (submit->batch_count == 1) {
      const struct wsi_memory_signal_submit_info *info = vk_find_struct_const(
         submit->submit_batches[0].pNext, WSI_MEMORY_SIGNAL_SUBMIT_INFO_MESA);
      if (info) {
         submit->wsi_mem = vn_device_memory_from_handle(info->memory);
         assert(!submit->wsi_mem->base_memory && submit->wsi_mem->base_bo);
      }
   }

   /* To ensure external components waiting on the correct fence payload,
    * below sync primitives must be installed after the submission:
    * - explicit fencing: sync file export
    * - implicit fencing: dma-fence attached to the wsi bo
    *
    * Under globalFencing, we enforce above via a synchronous submission if
    * any of the below applies:
    * - struct wsi_memory_signal_submit_info
    * - fence is an external fence
    * - has an external signal semaphore
    */
   submit->synchronous = has_external_fence || submit->wsi_mem;

   submit->wait_semaphore_count = 0;
   submit->wait_external_count = 0;

   for (uint32_t i = 0; i < submit->batch_count; i++) {
      VkResult result = vn_queue_submission_count_batch_semaphores(submit, i);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

static VkResult
vn_queue_submission_alloc_storage(struct vn_queue_submission *submit)
{
   struct vn_queue *queue = vn_queue_from_handle(submit->queue);
   const VkAllocationCallbacks *alloc = &queue->device->base.base.alloc;
   size_t alloc_size = 0;
   size_t semaphores_offset = 0;

   /* we want to filter out VN_SYNC_TYPE_IMPORTED_SYNC_FD wait semaphores */
   if (submit->wait_external_count) {
      switch (submit->batch_type) {
      case VK_STRUCTURE_TYPE_SUBMIT_INFO:
         alloc_size += sizeof(VkSubmitInfo) * submit->batch_count;
         break;
      case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
         alloc_size += sizeof(VkBindSparseInfo) * submit->batch_count;
         break;
      default:
         unreachable("unexpected batch type");
         break;
      }

      semaphores_offset = alloc_size;
      alloc_size +=
         sizeof(*submit->temp.semaphores) *
         (submit->wait_semaphore_count - submit->wait_external_count);
   }

   if (!alloc_size) {
      submit->temp.storage = NULL;
      return VK_SUCCESS;
   }

   submit->temp.storage = vk_alloc(alloc, alloc_size, VN_DEFAULT_ALIGN,
                                   VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!submit->temp.storage)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   submit->temp.batches = submit->temp.storage;
   submit->temp.semaphores = submit->temp.storage + semaphores_offset;

   return VK_SUCCESS;
}

static VkResult
vn_queue_submission_filter_batch_external_semaphores(
   struct vn_queue_submission *submit,
   uint32_t batch_index,
   uint32_t sem_base,
   uint32_t *out_count)
{
   struct vn_queue *queue = vn_queue_from_handle(submit->queue);

   union {
      VkSubmitInfo *submit_batch;
      VkBindSparseInfo *bind_sparse_batch;
   } u;
   const VkSemaphore *src_sems;
   uint32_t src_count;
   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO:
      u.submit_batch = &submit->temp.submit_batches[batch_index];
      src_sems = u.submit_batch->pWaitSemaphores;
      src_count = u.submit_batch->waitSemaphoreCount;
      break;
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
      u.bind_sparse_batch = &submit->temp.bind_sparse_batches[batch_index];
      src_sems = u.bind_sparse_batch->pWaitSemaphores;
      src_count = u.bind_sparse_batch->waitSemaphoreCount;
      break;
   default:
      unreachable("unexpected batch type");
      break;
   }

   VkSemaphore *dst_sems = &submit->temp.semaphores[sem_base];
   uint32_t dst_count = 0;

   /* filter out VN_SYNC_TYPE_IMPORTED_SYNC_FD wait semaphores */
   for (uint32_t i = 0; i < src_count; i++) {
      struct vn_semaphore *sem = vn_semaphore_from_handle(src_sems[i]);
      const struct vn_sync_payload *payload = sem->payload;

      if (payload->type == VN_SYNC_TYPE_IMPORTED_SYNC_FD) {
         if (!vn_semaphore_wait_external(queue->device, sem))
            return VK_ERROR_DEVICE_LOST;
      } else {
         dst_sems[dst_count++] = src_sems[i];
      }
   }

   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO:
      u.submit_batch->pWaitSemaphores = dst_sems;
      u.submit_batch->waitSemaphoreCount = dst_count;
      break;
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
      u.bind_sparse_batch->pWaitSemaphores = dst_sems;
      u.bind_sparse_batch->waitSemaphoreCount = dst_count;
      break;
   default:
      break;
   }

   *out_count = dst_count;
   return VK_SUCCESS;
}

static VkResult
vn_queue_submission_setup_batches(struct vn_queue_submission *submit)
{
   if (!submit->temp.storage)
      return VK_SUCCESS;

   /* make a copy because we need to filter out external semaphores */
   if (submit->wait_external_count) {
      switch (submit->batch_type) {
      case VK_STRUCTURE_TYPE_SUBMIT_INFO:
         memcpy(submit->temp.submit_batches, submit->submit_batches,
                sizeof(submit->submit_batches[0]) * submit->batch_count);
         submit->submit_batches = submit->temp.submit_batches;
         break;
      case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
         memcpy(submit->temp.bind_sparse_batches, submit->bind_sparse_batches,
                sizeof(submit->bind_sparse_batches[0]) * submit->batch_count);
         submit->bind_sparse_batches = submit->temp.bind_sparse_batches;
         break;
      default:
         unreachable("unexpected batch type");
         break;
      }
   }

   VkResult result;
   uint32_t wait_sem_base = 0;
   for (uint32_t i = 0; i < submit->batch_count; i++) {
      if (submit->wait_external_count) {
         uint32_t wait_sem_count = 0;
         result = vn_queue_submission_filter_batch_external_semaphores(
            submit, i, wait_sem_base, &wait_sem_count);
         if (result != VK_SUCCESS)
            return result;

         wait_sem_base += wait_sem_count;
      }
   }

   return VK_SUCCESS;
}

static void
vn_queue_submission_cleanup(struct vn_queue_submission *submit)
{
   struct vn_queue *queue = vn_queue_from_handle(submit->queue);
   const VkAllocationCallbacks *alloc = &queue->device->base.base.alloc;

   vk_free(alloc, submit->temp.storage);
}

static VkResult
vn_queue_submission_prepare_submit(struct vn_queue_submission *submit,
                                   VkQueue queue,
                                   uint32_t batch_count,
                                   const VkSubmitInfo *submit_batches,
                                   VkFence fence)
{
   submit->batch_type = VK_STRUCTURE_TYPE_SUBMIT_INFO;
   submit->queue = queue;
   submit->batch_count = batch_count;
   submit->submit_batches = submit_batches;
   submit->fence = fence;

   VkResult result = vn_queue_submission_prepare(submit);
   if (result != VK_SUCCESS)
      return result;

   result = vn_queue_submission_alloc_storage(submit);
   if (result != VK_SUCCESS)
      return result;

   result = vn_queue_submission_setup_batches(submit);
   if (result != VK_SUCCESS)
      vn_queue_submission_cleanup(submit);

   return result;
}

static VkResult
vn_queue_submission_prepare_bind_sparse(
   struct vn_queue_submission *submit,
   VkQueue queue,
   uint32_t batch_count,
   const VkBindSparseInfo *bind_sparse_batches,
   VkFence fence)
{
   submit->batch_type = VK_STRUCTURE_TYPE_BIND_SPARSE_INFO;
   submit->queue = queue;
   submit->batch_count = batch_count;
   submit->bind_sparse_batches = bind_sparse_batches;
   submit->fence = fence;

   VkResult result = vn_queue_submission_prepare(submit);
   if (result != VK_SUCCESS)
      return result;

   result = vn_queue_submission_alloc_storage(submit);
   if (result != VK_SUCCESS)
      return result;

   result = vn_queue_submission_setup_batches(submit);
   if (result != VK_SUCCESS)
      vn_queue_submission_cleanup(submit);

   return VK_SUCCESS;
}

static const VkCommandBuffer
vn_get_fence_feedback_cmd(struct vn_queue_submission *submit)
{
   struct vn_queue *queue = vn_queue_from_handle(submit->queue);
   struct vn_fence *fence = vn_fence_from_handle(submit->fence);

   assert(submit->has_feedback_fence);

   for (uint32_t i = 0; i < queue->device->queue_family_count; i++) {
      if (queue->device->queue_families[i] == queue->family)
         return fence->feedback.commands[i];
   }

   unreachable("invalid vn_queue_submission");
}

static VkResult
vn_queue_submit(struct vn_instance *instance,
                VkQueue queue_handle,
                uint32_t batch_count,
                const VkSubmitInfo *batches,
                VkFence fence_handle,
                bool sync_submit)
{
   /* skip no-op submit */
   if (!batch_count && fence_handle == VK_NULL_HANDLE)
      return VK_SUCCESS;

   if (sync_submit || VN_PERF(NO_ASYNC_QUEUE_SUBMIT)) {
      return vn_call_vkQueueSubmit(instance, queue_handle, batch_count,
                                   batches, fence_handle);
   }

   vn_async_vkQueueSubmit(instance, queue_handle, batch_count, batches,
                          fence_handle);
   return VK_SUCCESS;
}

VkResult
vn_QueueSubmit(VkQueue queue,
               uint32_t submitCount,
               const VkSubmitInfo *pSubmits,
               VkFence fence)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_queue_from_handle(queue)->device;
   struct vn_queue_submission submit;

   VkResult result = vn_queue_submission_prepare_submit(
      &submit, queue, submitCount, pSubmits, fence);
   if (result != VK_SUCCESS)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* if the original submission involves a feedback fence:
    * - defer the feedback fence to another submit to avoid deep copy
    * - defer the potential sync_submit to the feedback fence submission
    */
   result = vn_queue_submit(
      dev->instance, submit.queue, submit.batch_count, submit.submit_batches,
      submit.has_feedback_fence ? VK_NULL_HANDLE : submit.fence,
      !submit.has_feedback_fence && submit.synchronous);
   if (result != VK_SUCCESS) {
      vn_queue_submission_cleanup(&submit);
      return vn_error(dev->instance, result);
   }

   /* TODO intercept original submit batches to append the fence feedback cmd
    * with a per-queue cached submission builder to avoid transient allocs.
    *
    * vn_queue_submission bits must be fixed for VkTimelineSemaphoreSubmitInfo
    * before adding timeline semaphore feedback.
    */
   if (submit.has_feedback_fence) {
      const VkCommandBuffer cmd_handle = vn_get_fence_feedback_cmd(&submit);
      const VkSubmitInfo info = {
         .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
         .commandBufferCount = 1,
         .pCommandBuffers = &cmd_handle,
      };
      result = vn_queue_submit(dev->instance, submit.queue, 1, &info,
                               submit.fence, submit.synchronous);
      if (result != VK_SUCCESS) {
         vn_queue_submission_cleanup(&submit);
         return vn_error(dev->instance, result);
      }
   }

   if (submit.wsi_mem) {
      /* XXX this is always false and kills the performance */
      if (dev->instance->renderer->info.has_implicit_fencing) {
         vn_renderer_submit(dev->renderer, &(const struct vn_renderer_submit){
                                              .bos = &submit.wsi_mem->base_bo,
                                              .bo_count = 1,
                                           });
      } else {
         if (VN_DEBUG(WSI)) {
            static uint32_t ratelimit;
            if (ratelimit < 10) {
               vn_log(dev->instance,
                      "forcing vkQueueWaitIdle before presenting");
               ratelimit++;
            }
         }

         vn_QueueWaitIdle(submit.queue);
      }
   }

   vn_queue_submission_cleanup(&submit);

   return VK_SUCCESS;
}

VkResult
vn_QueueBindSparse(VkQueue _queue,
                   uint32_t bindInfoCount,
                   const VkBindSparseInfo *pBindInfo,
                   VkFence fence)
{
   VN_TRACE_FUNC();
   struct vn_queue *queue = vn_queue_from_handle(_queue);
   struct vn_device *dev = queue->device;

   /* TODO allow sparse resource along with sync feedback */
   assert(VN_PERF(NO_FENCE_FEEDBACK));

   struct vn_queue_submission submit;
   VkResult result = vn_queue_submission_prepare_bind_sparse(
      &submit, _queue, bindInfoCount, pBindInfo, fence);
   if (result != VK_SUCCESS)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = vn_call_vkQueueBindSparse(
      dev->instance, submit.queue, submit.batch_count,
      submit.bind_sparse_batches, submit.fence);
   if (result != VK_SUCCESS) {
      vn_queue_submission_cleanup(&submit);
      return vn_error(dev->instance, result);
   }

   vn_queue_submission_cleanup(&submit);

   return VK_SUCCESS;
}

VkResult
vn_QueueWaitIdle(VkQueue _queue)
{
   VN_TRACE_FUNC();
   struct vn_queue *queue = vn_queue_from_handle(_queue);
   VkDevice dev_handle = vn_device_to_handle(queue->device);
   VkResult result;

   /* lazily create queue wait fence for queue idle waiting */
   if (queue->wait_fence == VK_NULL_HANDLE) {
      const VkFenceCreateInfo create_info = {
         .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
         .flags = 0,
      };
      result =
         vn_CreateFence(dev_handle, &create_info, NULL, &queue->wait_fence);
      if (result != VK_SUCCESS)
         return result;
   }

   result = vn_QueueSubmit(_queue, 0, NULL, queue->wait_fence);
   if (result != VK_SUCCESS)
      return result;

   result =
      vn_WaitForFences(dev_handle, 1, &queue->wait_fence, true, UINT64_MAX);
   vn_ResetFences(dev_handle, 1, &queue->wait_fence);

   return vn_result(queue->device->instance, result);
}

/* fence commands */

static void
vn_sync_payload_release(UNUSED struct vn_device *dev,
                        struct vn_sync_payload *payload)
{
   if (payload->type == VN_SYNC_TYPE_IMPORTED_SYNC_FD && payload->fd >= 0)
      close(payload->fd);

   payload->type = VN_SYNC_TYPE_INVALID;
}

static VkResult
vn_fence_init_payloads(struct vn_device *dev,
                       struct vn_fence *fence,
                       bool signaled,
                       const VkAllocationCallbacks *alloc)
{
   fence->permanent.type = VN_SYNC_TYPE_DEVICE_ONLY;
   fence->temporary.type = VN_SYNC_TYPE_INVALID;
   fence->payload = &fence->permanent;

   return VK_SUCCESS;
}

void
vn_fence_signal_wsi(struct vn_device *dev, struct vn_fence *fence)
{
   struct vn_sync_payload *temp = &fence->temporary;

   vn_sync_payload_release(dev, temp);
   temp->type = VN_SYNC_TYPE_IMPORTED_SYNC_FD;
   temp->fd = -1;
   fence->payload = temp;
}

static VkResult
vn_fence_feedback_init(struct vn_device *dev,
                       struct vn_fence *fence,
                       bool signaled,
                       const VkAllocationCallbacks *alloc)
{
   VkDevice dev_handle = vn_device_to_handle(dev);
   struct vn_feedback_slot *slot;
   VkCommandBuffer *cmd_handles;
   VkResult result;

   if (fence->is_external)
      return VK_SUCCESS;

   /* Fence feedback implementation relies on vkWaitForFences to cover the gap
    * between feedback slot signaling and the actual fence signal operation.
    */
   if (unlikely(!dev->instance->renderer->info.allow_vk_wait_syncs))
      return VK_SUCCESS;

   if (VN_PERF(NO_FENCE_FEEDBACK))
      return VK_SUCCESS;

   slot = vn_feedback_pool_alloc(&dev->feedback_pool, VN_FEEDBACK_TYPE_FENCE);
   if (!slot)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   vn_feedback_set_status(slot, signaled ? VK_SUCCESS : VK_NOT_READY);

   cmd_handles =
      vk_zalloc(alloc, sizeof(*cmd_handles) * dev->queue_family_count,
                VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!cmd_handles) {
      vn_feedback_pool_free(&dev->feedback_pool, slot);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   for (uint32_t i = 0; i < dev->queue_family_count; i++) {
      result = vn_feedback_fence_cmd_alloc(dev_handle, &dev->cmd_pools[i],
                                           slot, &cmd_handles[i]);
      if (result != VK_SUCCESS) {
         for (uint32_t j = 0; j < i; j++) {
            vn_feedback_fence_cmd_free(dev_handle, &dev->cmd_pools[j],
                                       cmd_handles[j]);
         }
         break;
      }
   }

   if (result != VK_SUCCESS) {
      vk_free(alloc, cmd_handles);
      vn_feedback_pool_free(&dev->feedback_pool, slot);
      return result;
   }

   fence->feedback.slot = slot;
   fence->feedback.commands = cmd_handles;

   return VK_SUCCESS;
}

static void
vn_fence_feedback_fini(struct vn_device *dev,
                       struct vn_fence *fence,
                       const VkAllocationCallbacks *alloc)
{
   VkDevice dev_handle = vn_device_to_handle(dev);

   if (!fence->feedback.slot)
      return;

   for (uint32_t i = 0; i < dev->queue_family_count; i++) {
      vn_feedback_fence_cmd_free(dev_handle, &dev->cmd_pools[i],
                                 fence->feedback.commands[i]);
   }

   vn_feedback_pool_free(&dev->feedback_pool, fence->feedback.slot);

   vk_free(alloc, fence->feedback.commands);
}

VkResult
vn_CreateFence(VkDevice device,
               const VkFenceCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator,
               VkFence *pFence)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;
   const bool signaled = pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT;
   VkResult result;

   struct vn_fence *fence = vk_zalloc(alloc, sizeof(*fence), VN_DEFAULT_ALIGN,
                                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!fence)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&fence->base, VK_OBJECT_TYPE_FENCE, &dev->base);

   const struct VkExportFenceCreateInfo *export_info =
      vk_find_struct_const(pCreateInfo->pNext, EXPORT_FENCE_CREATE_INFO);
   fence->is_external = export_info && export_info->handleTypes;

   result = vn_fence_init_payloads(dev, fence, signaled, alloc);
   if (result != VK_SUCCESS)
      goto out_object_base_fini;

   result = vn_fence_feedback_init(dev, fence, signaled, alloc);
   if (result != VK_SUCCESS)
      goto out_payloads_fini;

   *pFence = vn_fence_to_handle(fence);
   vn_async_vkCreateFence(dev->instance, device, pCreateInfo, NULL, pFence);

   return VK_SUCCESS;

out_payloads_fini:
   vn_sync_payload_release(dev, &fence->permanent);
   vn_sync_payload_release(dev, &fence->temporary);

out_object_base_fini:
   vn_object_base_fini(&fence->base);
   vk_free(alloc, fence);
   return vn_error(dev->instance, result);
}

void
vn_DestroyFence(VkDevice device,
                VkFence _fence,
                const VkAllocationCallbacks *pAllocator)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_fence *fence = vn_fence_from_handle(_fence);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!fence)
      return;

   vn_async_vkDestroyFence(dev->instance, device, _fence, NULL);

   vn_fence_feedback_fini(dev, fence, alloc);

   vn_sync_payload_release(dev, &fence->permanent);
   vn_sync_payload_release(dev, &fence->temporary);

   vn_object_base_fini(&fence->base);
   vk_free(alloc, fence);
}

VkResult
vn_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);

   /* TODO if the fence is shared-by-ref, this needs to be synchronous */
   if (false)
      vn_call_vkResetFences(dev->instance, device, fenceCount, pFences);
   else
      vn_async_vkResetFences(dev->instance, device, fenceCount, pFences);

   for (uint32_t i = 0; i < fenceCount; i++) {
      struct vn_fence *fence = vn_fence_from_handle(pFences[i]);
      struct vn_sync_payload *perm = &fence->permanent;

      vn_sync_payload_release(dev, &fence->temporary);

      assert(perm->type == VN_SYNC_TYPE_DEVICE_ONLY);
      fence->payload = perm;

      if (fence->feedback.slot)
         vn_feedback_reset_status(fence->feedback.slot);
   }

   return VK_SUCCESS;
}

VkResult
vn_GetFenceStatus(VkDevice device, VkFence _fence)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_fence *fence = vn_fence_from_handle(_fence);
   struct vn_sync_payload *payload = fence->payload;

   VkResult result;
   switch (payload->type) {
   case VN_SYNC_TYPE_DEVICE_ONLY:
      if (fence->feedback.slot) {
         result = vn_feedback_get_status(fence->feedback.slot);
         if (result == VK_SUCCESS) {
            /* When fence feedback slot gets signaled, the real fence
             * signal operation follows after but the signaling isr can be
             * deferred or preempted. To avoid theoretical racing, we let
             * the renderer wait for the fence. This also helps resolve
             * synchronization validation errors, because the layer no
             * longer sees any fence status checks and falsely believes the
             * caller does not sync.
             */
            vn_async_vkWaitForFences(dev->instance, device, 1, &_fence,
                                     VK_TRUE, UINT64_MAX);
         }
      } else {
         result = vn_call_vkGetFenceStatus(dev->instance, device, _fence);
      }
      break;
   case VN_SYNC_TYPE_IMPORTED_SYNC_FD:
      if (payload->fd < 0 || sync_wait(payload->fd, 0) == 0)
         result = VK_SUCCESS;
      else
         result = errno == ETIME ? VK_NOT_READY : VK_ERROR_DEVICE_LOST;
      break;
   default:
      unreachable("unexpected fence payload type");
      break;
   }

   return vn_result(dev->instance, result);
}

static VkResult
vn_find_first_signaled_fence(VkDevice device,
                             const VkFence *fences,
                             uint32_t count)
{
   for (uint32_t i = 0; i < count; i++) {
      VkResult result = vn_GetFenceStatus(device, fences[i]);
      if (result == VK_SUCCESS || result < 0)
         return result;
   }
   return VK_NOT_READY;
}

static VkResult
vn_remove_signaled_fences(VkDevice device, VkFence *fences, uint32_t *count)
{
   uint32_t cur = 0;
   for (uint32_t i = 0; i < *count; i++) {
      VkResult result = vn_GetFenceStatus(device, fences[i]);
      if (result != VK_SUCCESS) {
         if (result < 0)
            return result;
         fences[cur++] = fences[i];
      }
   }

   *count = cur;
   return cur ? VK_NOT_READY : VK_SUCCESS;
}

static VkResult
vn_update_sync_result(VkResult result, int64_t abs_timeout, uint32_t *iter)
{
   switch (result) {
   case VK_NOT_READY:
      if (abs_timeout != OS_TIMEOUT_INFINITE &&
          os_time_get_nano() >= abs_timeout)
         result = VK_TIMEOUT;
      else
         vn_relax(iter, "client");
      break;
   default:
      assert(result == VK_SUCCESS || result < 0);
      break;
   }

   return result;
}

VkResult
vn_WaitForFences(VkDevice device,
                 uint32_t fenceCount,
                 const VkFence *pFences,
                 VkBool32 waitAll,
                 uint64_t timeout)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;

   const int64_t abs_timeout = os_time_get_absolute_timeout(timeout);
   VkResult result = VK_NOT_READY;
   uint32_t iter = 0;
   if (fenceCount > 1 && waitAll) {
      VkFence local_fences[8];
      VkFence *fences = local_fences;
      if (fenceCount > ARRAY_SIZE(local_fences)) {
         fences =
            vk_alloc(alloc, sizeof(*fences) * fenceCount, VN_DEFAULT_ALIGN,
                     VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
         if (!fences)
            return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      }
      memcpy(fences, pFences, sizeof(*fences) * fenceCount);

      while (result == VK_NOT_READY) {
         result = vn_remove_signaled_fences(device, fences, &fenceCount);
         result = vn_update_sync_result(result, abs_timeout, &iter);
      }

      if (fences != local_fences)
         vk_free(alloc, fences);
   } else {
      while (result == VK_NOT_READY) {
         result = vn_find_first_signaled_fence(device, pFences, fenceCount);
         result = vn_update_sync_result(result, abs_timeout, &iter);
      }
   }

   return vn_result(dev->instance, result);
}

static VkResult
vn_create_sync_file(struct vn_device *dev, int *out_fd)
{
   struct vn_renderer_sync *sync;
   VkResult result = vn_renderer_sync_create(dev->renderer, 0,
                                             VN_RENDERER_SYNC_BINARY, &sync);
   if (result != VK_SUCCESS)
      return vn_error(dev->instance, result);

   const struct vn_renderer_submit submit = {
      .batches =
         &(const struct vn_renderer_submit_batch){
            .syncs = &sync,
            .sync_values = &(const uint64_t){ 1 },
            .sync_count = 1,
         },
      .batch_count = 1,
   };
   result = vn_renderer_submit(dev->renderer, &submit);
   if (result != VK_SUCCESS) {
      vn_renderer_sync_destroy(dev->renderer, sync);
      return vn_error(dev->instance, result);
   }

   *out_fd = vn_renderer_sync_export_syncobj(dev->renderer, sync, true);
   vn_renderer_sync_destroy(dev->renderer, sync);

   return *out_fd >= 0 ? VK_SUCCESS : VK_ERROR_TOO_MANY_OBJECTS;
}

static inline bool
vn_sync_valid_fd(int fd)
{
   /* the special value -1 for fd is treated like a valid sync file descriptor
    * referring to an object that has already signaled
    */
   return (fd >= 0 && sync_valid_fd(fd)) || fd == -1;
}

VkResult
vn_ImportFenceFdKHR(VkDevice device,
                    const VkImportFenceFdInfoKHR *pImportFenceFdInfo)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_fence *fence = vn_fence_from_handle(pImportFenceFdInfo->fence);
   ASSERTED const bool sync_file = pImportFenceFdInfo->handleType ==
                                   VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
   const int fd = pImportFenceFdInfo->fd;

   assert(dev->instance->experimental.globalFencing);
   assert(sync_file);

   if (!vn_sync_valid_fd(fd))
      return vn_error(dev->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);

   struct vn_sync_payload *temp = &fence->temporary;
   vn_sync_payload_release(dev, temp);
   temp->type = VN_SYNC_TYPE_IMPORTED_SYNC_FD;
   temp->fd = fd;
   fence->payload = temp;

   return VK_SUCCESS;
}

VkResult
vn_GetFenceFdKHR(VkDevice device,
                 const VkFenceGetFdInfoKHR *pGetFdInfo,
                 int *pFd)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_fence *fence = vn_fence_from_handle(pGetFdInfo->fence);
   const bool sync_file =
      pGetFdInfo->handleType == VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
   struct vn_sync_payload *payload = fence->payload;
   VkResult result;

   assert(dev->instance->experimental.globalFencing);
   assert(sync_file);
   int fd = -1;
   if (payload->type == VN_SYNC_TYPE_DEVICE_ONLY) {
      result = vn_create_sync_file(dev, &fd);
      if (result != VK_SUCCESS)
         return vn_error(dev->instance, result);

      /* perform reset operation on the host fence */
      if (dev->physical_device->renderer_sync_fd_fence_features &
          VK_EXTERNAL_FENCE_FEATURE_EXPORTABLE_BIT) {
         vn_async_vkResetFenceResource100000MESA(dev->instance, device,
                                                 pGetFdInfo->fence);
      }

      vn_sync_payload_release(dev, &fence->temporary);
      fence->payload = &fence->permanent;
   } else {
      assert(payload->type == VN_SYNC_TYPE_IMPORTED_SYNC_FD);

      /* transfer ownership of imported sync fd to save a dup */
      fd = payload->fd;
      payload->fd = -1;

      /* reset host fence in case in signaled state before import */
      result = vn_ResetFences(device, 1, &pGetFdInfo->fence);
      if (result != VK_SUCCESS) {
         /* transfer sync fd ownership back on error */
         payload->fd = fd;
         return result;
      }
   }

   *pFd = fd;
   return VK_SUCCESS;
}

/* semaphore commands */

static VkResult
vn_semaphore_init_payloads(struct vn_device *dev,
                           struct vn_semaphore *sem,
                           uint64_t initial_val,
                           const VkAllocationCallbacks *alloc)
{
   sem->permanent.type = VN_SYNC_TYPE_DEVICE_ONLY;
   sem->temporary.type = VN_SYNC_TYPE_INVALID;
   sem->payload = &sem->permanent;

   return VK_SUCCESS;
}

static bool
vn_semaphore_wait_external(struct vn_device *dev, struct vn_semaphore *sem)
{
   struct vn_sync_payload *temp = &sem->temporary;

   assert(temp->type == VN_SYNC_TYPE_IMPORTED_SYNC_FD);

   if (temp->fd >= 0) {
      if (sync_wait(temp->fd, -1))
         return false;
   }

   vn_sync_payload_release(dev, &sem->temporary);
   sem->payload = &sem->permanent;

   return true;
}

void
vn_semaphore_signal_wsi(struct vn_device *dev, struct vn_semaphore *sem)
{
   struct vn_sync_payload *temp = &sem->temporary;

   vn_sync_payload_release(dev, temp);
   temp->type = VN_SYNC_TYPE_IMPORTED_SYNC_FD;
   temp->fd = -1;
   sem->payload = temp;
}

VkResult
vn_CreateSemaphore(VkDevice device,
                   const VkSemaphoreCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkSemaphore *pSemaphore)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_semaphore *sem = vk_zalloc(alloc, sizeof(*sem), VN_DEFAULT_ALIGN,
                                        VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!sem)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&sem->base, VK_OBJECT_TYPE_SEMAPHORE, &dev->base);

   const VkSemaphoreTypeCreateInfo *type_info =
      vk_find_struct_const(pCreateInfo->pNext, SEMAPHORE_TYPE_CREATE_INFO);
   uint64_t initial_val = 0;
   if (type_info && type_info->semaphoreType == VK_SEMAPHORE_TYPE_TIMELINE) {
      sem->type = VK_SEMAPHORE_TYPE_TIMELINE;
      initial_val = type_info->initialValue;
   } else {
      sem->type = VK_SEMAPHORE_TYPE_BINARY;
   }

   const struct VkExportSemaphoreCreateInfo *export_info =
      vk_find_struct_const(pCreateInfo->pNext, EXPORT_SEMAPHORE_CREATE_INFO);
   sem->is_external = export_info && export_info->handleTypes;

   VkResult result = vn_semaphore_init_payloads(dev, sem, initial_val, alloc);
   if (result != VK_SUCCESS) {
      vn_object_base_fini(&sem->base);
      vk_free(alloc, sem);
      return vn_error(dev->instance, result);
   }

   VkSemaphore sem_handle = vn_semaphore_to_handle(sem);
   vn_async_vkCreateSemaphore(dev->instance, device, pCreateInfo, NULL,
                              &sem_handle);

   *pSemaphore = sem_handle;

   return VK_SUCCESS;
}

void
vn_DestroySemaphore(VkDevice device,
                    VkSemaphore semaphore,
                    const VkAllocationCallbacks *pAllocator)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_semaphore *sem = vn_semaphore_from_handle(semaphore);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!sem)
      return;

   vn_async_vkDestroySemaphore(dev->instance, device, semaphore, NULL);

   vn_sync_payload_release(dev, &sem->permanent);
   vn_sync_payload_release(dev, &sem->temporary);

   vn_object_base_fini(&sem->base);
   vk_free(alloc, sem);
}

VkResult
vn_GetSemaphoreCounterValue(VkDevice device,
                            VkSemaphore semaphore,
                            uint64_t *pValue)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_semaphore *sem = vn_semaphore_from_handle(semaphore);
   ASSERTED struct vn_sync_payload *payload = sem->payload;

   assert(payload->type == VN_SYNC_TYPE_DEVICE_ONLY);
   return vn_call_vkGetSemaphoreCounterValue(dev->instance, device, semaphore,
                                             pValue);
}

VkResult
vn_SignalSemaphore(VkDevice device, const VkSemaphoreSignalInfo *pSignalInfo)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);

   /* TODO if the semaphore is shared-by-ref, this needs to be synchronous */
   if (false)
      vn_call_vkSignalSemaphore(dev->instance, device, pSignalInfo);
   else
      vn_async_vkSignalSemaphore(dev->instance, device, pSignalInfo);

   return VK_SUCCESS;
}

static VkResult
vn_find_first_signaled_semaphore(VkDevice device,
                                 const VkSemaphore *semaphores,
                                 const uint64_t *values,
                                 uint32_t count)
{
   for (uint32_t i = 0; i < count; i++) {
      uint64_t val = 0;
      VkResult result =
         vn_GetSemaphoreCounterValue(device, semaphores[i], &val);
      if (result != VK_SUCCESS || val >= values[i])
         return result;
   }
   return VK_NOT_READY;
}

static VkResult
vn_remove_signaled_semaphores(VkDevice device,
                              VkSemaphore *semaphores,
                              uint64_t *values,
                              uint32_t *count)
{
   uint32_t cur = 0;
   for (uint32_t i = 0; i < *count; i++) {
      uint64_t val = 0;
      VkResult result =
         vn_GetSemaphoreCounterValue(device, semaphores[i], &val);
      if (result != VK_SUCCESS)
         return result;
      if (val < values[i])
         semaphores[cur++] = semaphores[i];
   }

   *count = cur;
   return cur ? VK_NOT_READY : VK_SUCCESS;
}

VkResult
vn_WaitSemaphores(VkDevice device,
                  const VkSemaphoreWaitInfo *pWaitInfo,
                  uint64_t timeout)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc = &dev->base.base.alloc;

   const int64_t abs_timeout = os_time_get_absolute_timeout(timeout);
   VkResult result = VK_NOT_READY;
   uint32_t iter = 0;
   if (pWaitInfo->semaphoreCount > 1 &&
       !(pWaitInfo->flags & VK_SEMAPHORE_WAIT_ANY_BIT)) {
      uint32_t semaphore_count = pWaitInfo->semaphoreCount;
      VkSemaphore local_semaphores[8];
      uint64_t local_values[8];
      VkSemaphore *semaphores = local_semaphores;
      uint64_t *values = local_values;
      if (semaphore_count > ARRAY_SIZE(local_semaphores)) {
         semaphores = vk_alloc(
            alloc, (sizeof(*semaphores) + sizeof(*values)) * semaphore_count,
            VN_DEFAULT_ALIGN, VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
         if (!semaphores)
            return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

         values = (uint64_t *)&semaphores[semaphore_count];
      }
      memcpy(semaphores, pWaitInfo->pSemaphores,
             sizeof(*semaphores) * semaphore_count);
      memcpy(values, pWaitInfo->pValues, sizeof(*values) * semaphore_count);

      while (result == VK_NOT_READY) {
         result = vn_remove_signaled_semaphores(device, semaphores, values,
                                                &semaphore_count);
         result = vn_update_sync_result(result, abs_timeout, &iter);
      }

      if (semaphores != local_semaphores)
         vk_free(alloc, semaphores);
   } else {
      while (result == VK_NOT_READY) {
         result = vn_find_first_signaled_semaphore(
            device, pWaitInfo->pSemaphores, pWaitInfo->pValues,
            pWaitInfo->semaphoreCount);
         result = vn_update_sync_result(result, abs_timeout, &iter);
      }
   }

   return vn_result(dev->instance, result);
}

VkResult
vn_ImportSemaphoreFdKHR(
   VkDevice device, const VkImportSemaphoreFdInfoKHR *pImportSemaphoreFdInfo)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_semaphore *sem =
      vn_semaphore_from_handle(pImportSemaphoreFdInfo->semaphore);
   ASSERTED const bool sync_file =
      pImportSemaphoreFdInfo->handleType ==
      VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
   const int fd = pImportSemaphoreFdInfo->fd;

   assert(dev->instance->experimental.globalFencing);
   assert(sync_file);

   if (!vn_sync_valid_fd(fd))
      return vn_error(dev->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);

   struct vn_sync_payload *temp = &sem->temporary;
   vn_sync_payload_release(dev, temp);
   temp->type = VN_SYNC_TYPE_IMPORTED_SYNC_FD;
   temp->fd = fd;
   sem->payload = temp;

   return VK_SUCCESS;
}

VkResult
vn_GetSemaphoreFdKHR(VkDevice device,
                     const VkSemaphoreGetFdInfoKHR *pGetFdInfo,
                     int *pFd)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_semaphore *sem = vn_semaphore_from_handle(pGetFdInfo->semaphore);
   const bool sync_file =
      pGetFdInfo->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
   struct vn_sync_payload *payload = sem->payload;

   assert(dev->instance->experimental.globalFencing);
   assert(sync_file);
   int fd = -1;
   if (payload->type == VN_SYNC_TYPE_DEVICE_ONLY) {
      VkResult result = vn_create_sync_file(dev, &fd);
      if (result != VK_SUCCESS)
         return vn_error(dev->instance, result);

   } else {
      assert(payload->type == VN_SYNC_TYPE_IMPORTED_SYNC_FD);

      /* transfer ownership of imported sync fd to save a dup */
      fd = payload->fd;
      payload->fd = -1;
   }

   /* required sync_fd features for fixing the host semaphore payload */
   static const VkExternalSemaphoreFeatureFlags req_sync_fd_feats =
      VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT |
      VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
   if ((dev->physical_device->renderer_sync_fd_semaphore_features &
        req_sync_fd_feats) == req_sync_fd_feats) {

      /* When payload->type is VN_SYNC_TYPE_IMPORTED_SYNC_FD, the current
       * payload is from a prior temporary sync_fd import. The permanent
       * payload of the sempahore might be in signaled state. So we do an
       * import here to ensure later wait operation is legit. With resourceId
       * 0, renderer does a signaled sync_fd -1 payload import on the host
       * semaphore.
       */
      if (payload->type == VN_SYNC_TYPE_IMPORTED_SYNC_FD) {
         const VkImportSemaphoreResourceInfo100000MESA res_info = {
            .sType =
               VK_STRUCTURE_TYPE_IMPORT_SEMAPHORE_RESOURCE_INFO_100000_MESA,
            .semaphore = pGetFdInfo->semaphore,
            .resourceId = 0,
         };
         vn_async_vkImportSemaphoreResource100000MESA(dev->instance, device,
                                                      &res_info);
      }

      /* perform wait operation on the host semaphore */
      vn_async_vkWaitSemaphoreResource100000MESA(dev->instance, device,
                                                 pGetFdInfo->semaphore);
   }

   vn_sync_payload_release(dev, &sem->temporary);
   sem->payload = &sem->permanent;

   *pFd = fd;
   return VK_SUCCESS;
}

/* event commands */

static VkResult
vn_event_feedback_init(struct vn_device *dev, struct vn_event *ev)
{
   struct vn_feedback_slot *slot;

   if (VN_PERF(NO_EVENT_FEEDBACK))
      return VK_SUCCESS;

   slot = vn_feedback_pool_alloc(&dev->feedback_pool, VN_FEEDBACK_TYPE_EVENT);
   if (!slot)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   /* newly created event object is in the unsignaled state */
   vn_feedback_set_status(slot, VK_EVENT_RESET);

   ev->feedback_slot = slot;

   return VK_SUCCESS;
}

static inline void
vn_event_feedback_fini(struct vn_device *dev, struct vn_event *ev)
{
   if (ev->feedback_slot)
      vn_feedback_pool_free(&dev->feedback_pool, ev->feedback_slot);
}

VkResult
vn_CreateEvent(VkDevice device,
               const VkEventCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator,
               VkEvent *pEvent)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_event *ev = vk_zalloc(alloc, sizeof(*ev), VN_DEFAULT_ALIGN,
                                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!ev)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&ev->base, VK_OBJECT_TYPE_EVENT, &dev->base);

   /* feedback is only needed to speed up host operations */
   if (!(pCreateInfo->flags & VK_EVENT_CREATE_DEVICE_ONLY_BIT)) {
      VkResult result = vn_event_feedback_init(dev, ev);
      if (result != VK_SUCCESS)
         return vn_error(dev->instance, result);
   }

   VkEvent ev_handle = vn_event_to_handle(ev);
   vn_async_vkCreateEvent(dev->instance, device, pCreateInfo, NULL,
                          &ev_handle);

   *pEvent = ev_handle;

   return VK_SUCCESS;
}

void
vn_DestroyEvent(VkDevice device,
                VkEvent event,
                const VkAllocationCallbacks *pAllocator)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_event *ev = vn_event_from_handle(event);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!ev)
      return;

   vn_async_vkDestroyEvent(dev->instance, device, event, NULL);

   vn_event_feedback_fini(dev, ev);

   vn_object_base_fini(&ev->base);
   vk_free(alloc, ev);
}

VkResult
vn_GetEventStatus(VkDevice device, VkEvent event)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_event *ev = vn_event_from_handle(event);
   VkResult result;

   if (ev->feedback_slot)
      result = vn_feedback_get_status(ev->feedback_slot);
   else
      result = vn_call_vkGetEventStatus(dev->instance, device, event);

   return vn_result(dev->instance, result);
}

VkResult
vn_SetEvent(VkDevice device, VkEvent event)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_event *ev = vn_event_from_handle(event);

   if (ev->feedback_slot) {
      vn_feedback_set_status(ev->feedback_slot, VK_EVENT_SET);
      vn_async_vkSetEvent(dev->instance, device, event);
   } else {
      VkResult result = vn_call_vkSetEvent(dev->instance, device, event);
      if (result != VK_SUCCESS)
         return vn_error(dev->instance, result);
   }

   return VK_SUCCESS;
}

VkResult
vn_ResetEvent(VkDevice device, VkEvent event)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_event *ev = vn_event_from_handle(event);

   if (ev->feedback_slot) {
      vn_feedback_reset_status(ev->feedback_slot);
      vn_async_vkResetEvent(dev->instance, device, event);
   } else {
      VkResult result = vn_call_vkResetEvent(dev->instance, device, event);
      if (result != VK_SUCCESS)
         return vn_error(dev->instance, result);
   }

   return VK_SUCCESS;
}
