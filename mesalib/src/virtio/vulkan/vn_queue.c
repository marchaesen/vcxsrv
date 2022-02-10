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
#include "vn_renderer.h"
#include "vn_wsi.h"

/* queue commands */

void
vn_GetDeviceQueue(VkDevice device,
                  uint32_t queueFamilyIndex,
                  uint32_t queueIndex,
                  VkQueue *pQueue)
{
   struct vn_device *dev = vn_device_from_handle(device);

   for (uint32_t i = 0; i < dev->queue_count; i++) {
      struct vn_queue *queue = &dev->queues[i];
      if (queue->family == queueFamilyIndex && queue->index == queueIndex) {
         assert(!queue->flags);
         *pQueue = vn_queue_to_handle(queue);
         return;
      }
   }
   unreachable("bad queue family/index");
}

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

static void
vn_semaphore_reset_wsi(struct vn_device *dev, struct vn_semaphore *sem);

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

   uint32_t wait_semaphore_count;
   uint32_t wait_wsi_count;

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

static void
vn_queue_submission_count_batch_semaphores(struct vn_queue_submission *submit,
                                           uint32_t batch_index)
{
   union {
      const VkSubmitInfo *submit_batch;
      const VkBindSparseInfo *bind_sparse_batch;
   } u;
   const VkSemaphore *wait_sems;
   uint32_t wait_count;
   switch (submit->batch_type) {
   case VK_STRUCTURE_TYPE_SUBMIT_INFO:
      u.submit_batch = &submit->submit_batches[batch_index];
      wait_sems = u.submit_batch->pWaitSemaphores;
      wait_count = u.submit_batch->waitSemaphoreCount;
      break;
   case VK_STRUCTURE_TYPE_BIND_SPARSE_INFO:
      u.bind_sparse_batch = &submit->bind_sparse_batches[batch_index];
      wait_sems = u.bind_sparse_batch->pWaitSemaphores;
      wait_count = u.bind_sparse_batch->waitSemaphoreCount;
      break;
   default:
      unreachable("unexpected batch type");
      break;
   }

   submit->wait_semaphore_count += wait_count;
   for (uint32_t i = 0; i < wait_count; i++) {
      struct vn_semaphore *sem = vn_semaphore_from_handle(wait_sems[i]);
      const struct vn_sync_payload *payload = sem->payload;

      if (payload->type == VN_SYNC_TYPE_WSI_SIGNALED)
         submit->wait_wsi_count++;
   }
}

static void
vn_queue_submission_count_semaphores(struct vn_queue_submission *submit)
{
   submit->wait_semaphore_count = 0;
   submit->wait_wsi_count = 0;

   for (uint32_t i = 0; i < submit->batch_count; i++)
      vn_queue_submission_count_batch_semaphores(submit, i);
}

static VkResult
vn_queue_submission_alloc_storage(struct vn_queue_submission *submit)
{
   struct vn_queue *queue = vn_queue_from_handle(submit->queue);
   const VkAllocationCallbacks *alloc = &queue->device->base.base.alloc;
   size_t alloc_size = 0;
   size_t semaphores_offset = 0;

   /* we want to filter out VN_SYNC_TYPE_WSI_SIGNALED wait semaphores */
   if (submit->wait_wsi_count) {
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
      alloc_size += sizeof(*submit->temp.semaphores) *
                    (submit->wait_semaphore_count - submit->wait_wsi_count);
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

static uint32_t
vn_queue_submission_filter_batch_wsi_semaphores(
   struct vn_queue_submission *submit,
   uint32_t batch_index,
   uint32_t sem_base)
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

   /* filter out VN_SYNC_TYPE_WSI_SIGNALED wait semaphores */
   for (uint32_t i = 0; i < src_count; i++) {
      struct vn_semaphore *sem = vn_semaphore_from_handle(src_sems[i]);
      const struct vn_sync_payload *payload = sem->payload;

      if (payload->type == VN_SYNC_TYPE_WSI_SIGNALED)
         vn_semaphore_reset_wsi(queue->device, sem);
      else
         dst_sems[dst_count++] = src_sems[i];
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

   return dst_count;
}

static void
vn_queue_submission_setup_batches(struct vn_queue_submission *submit)
{
   if (!submit->temp.storage)
      return;

   /* make a copy because we need to filter out WSI semaphores */
   if (submit->wait_wsi_count) {
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

   uint32_t wait_sem_base = 0;
   for (uint32_t i = 0; i < submit->batch_count; i++) {
      if (submit->wait_wsi_count) {
         wait_sem_base += vn_queue_submission_filter_batch_wsi_semaphores(
            submit, i, wait_sem_base);
      }
   }
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

   vn_queue_submission_count_semaphores(submit);

   VkResult result = vn_queue_submission_alloc_storage(submit);
   if (result != VK_SUCCESS)
      return result;

   vn_queue_submission_setup_batches(submit);

   return VK_SUCCESS;
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

   vn_queue_submission_count_semaphores(submit);

   VkResult result = vn_queue_submission_alloc_storage(submit);
   if (result != VK_SUCCESS)
      return result;

   vn_queue_submission_setup_batches(submit);

   return VK_SUCCESS;
}

static void
vn_queue_submission_cleanup(struct vn_queue_submission *submit)
{
   struct vn_queue *queue = vn_queue_from_handle(submit->queue);
   const VkAllocationCallbacks *alloc = &queue->device->base.base.alloc;

   vk_free(alloc, submit->temp.storage);
}

VkResult
vn_QueueSubmit(VkQueue _queue,
               uint32_t submitCount,
               const VkSubmitInfo *pSubmits,
               VkFence _fence)
{
   VN_TRACE_FUNC();
   struct vn_queue *queue = vn_queue_from_handle(_queue);
   struct vn_device *dev = queue->device;
   struct vn_fence *fence = vn_fence_from_handle(_fence);
   const bool is_fence_external = fence && fence->is_external;

   struct vn_queue_submission submit;
   VkResult result = vn_queue_submission_prepare_submit(
      &submit, _queue, submitCount, pSubmits, _fence);
   if (result != VK_SUCCESS)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   const struct vn_device_memory *wsi_mem = NULL;
   if (submit.batch_count == 1) {
      const struct wsi_memory_signal_submit_info *info = vk_find_struct_const(
         submit.submit_batches[0].pNext, WSI_MEMORY_SIGNAL_SUBMIT_INFO_MESA);
      if (info) {
         wsi_mem = vn_device_memory_from_handle(info->memory);
         assert(!wsi_mem->base_memory && wsi_mem->base_bo);
      }
   }

   /* TODO defer roundtrip for external fence until the next sync operation */
   if (!wsi_mem && !is_fence_external) {
      vn_async_vkQueueSubmit(dev->instance, submit.queue, submit.batch_count,
                             submit.submit_batches, submit.fence);
      vn_queue_submission_cleanup(&submit);
      return VK_SUCCESS;
   }

   result =
      vn_call_vkQueueSubmit(dev->instance, submit.queue, submit.batch_count,
                            submit.submit_batches, submit.fence);
   if (result != VK_SUCCESS) {
      vn_queue_submission_cleanup(&submit);
      return vn_error(dev->instance, result);
   }

   if (wsi_mem) {
      /* XXX this is always false and kills the performance */
      if (dev->instance->renderer_info.has_implicit_fencing) {
         vn_renderer_submit(dev->renderer, &(const struct vn_renderer_submit){
                                              .bos = &wsi_mem->base_bo,
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
   VkDevice device = vn_device_to_handle(queue->device);

   VkResult result = vn_QueueSubmit(_queue, 0, NULL, queue->wait_fence);
   if (result != VK_SUCCESS)
      return result;

   result = vn_WaitForFences(device, 1, &queue->wait_fence, true, UINT64_MAX);
   vn_ResetFences(device, 1, &queue->wait_fence);

   return vn_result(queue->device->instance, result);
}

/* fence commands */

static void
vn_sync_payload_release(struct vn_device *dev,
                        struct vn_sync_payload *payload)
{
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
   temp->type = VN_SYNC_TYPE_WSI_SIGNALED;
   fence->payload = temp;
}

VkResult
vn_CreateFence(VkDevice device,
               const VkFenceCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator,
               VkFence *pFence)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_fence *fence = vk_zalloc(alloc, sizeof(*fence), VN_DEFAULT_ALIGN,
                                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!fence)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&fence->base, VK_OBJECT_TYPE_FENCE, &dev->base);

   const struct VkExportFenceCreateInfo *export_info =
      vk_find_struct_const(pCreateInfo->pNext, EXPORT_FENCE_CREATE_INFO);
   VkFenceCreateInfo local_create_info;
   if (export_info) {
      local_create_info = *pCreateInfo;
      local_create_info.pNext = NULL;
      pCreateInfo = &local_create_info;

      fence->is_external = !!export_info->handleTypes;
   }

   VkResult result = vn_fence_init_payloads(
      dev, fence, pCreateInfo->flags & VK_FENCE_CREATE_SIGNALED_BIT, alloc);
   if (result != VK_SUCCESS) {
      vn_object_base_fini(&fence->base);
      vk_free(alloc, fence);
      return vn_error(dev->instance, result);
   }

   VkFence fence_handle = vn_fence_to_handle(fence);
   vn_async_vkCreateFence(dev->instance, device, pCreateInfo, NULL,
                          &fence_handle);

   *pFence = fence_handle;

   return VK_SUCCESS;
}

void
vn_DestroyFence(VkDevice device,
                VkFence _fence,
                const VkAllocationCallbacks *pAllocator)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_fence *fence = vn_fence_from_handle(_fence);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!fence)
      return;

   vn_async_vkDestroyFence(dev->instance, device, _fence, NULL);

   vn_sync_payload_release(dev, &fence->permanent);
   vn_sync_payload_release(dev, &fence->temporary);

   vn_object_base_fini(&fence->base);
   vk_free(alloc, fence);
}

VkResult
vn_ResetFences(VkDevice device, uint32_t fenceCount, const VkFence *pFences)
{
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
      result = vn_call_vkGetFenceStatus(dev->instance, device, _fence);
      break;
   case VN_SYNC_TYPE_WSI_SIGNALED:
      result = VK_SUCCESS;
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

VkResult
vn_ImportFenceFdKHR(VkDevice device,
                    const VkImportFenceFdInfoKHR *pImportFenceFdInfo)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_fence *fence = vn_fence_from_handle(pImportFenceFdInfo->fence);
   ASSERTED const bool sync_file = pImportFenceFdInfo->handleType ==
                                   VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
   const int fd = pImportFenceFdInfo->fd;

   /* TODO update fence->is_external after we support opaque fd import */
   assert(dev->instance->experimental.globalFencing);
   assert(sync_file);
   if (fd >= 0) {
      if (sync_wait(fd, -1))
         return vn_error(dev->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);

      close(fd);
   }

   /* abuse VN_SYNC_TYPE_WSI_SIGNALED */
   vn_fence_signal_wsi(dev, fence);

   return VK_SUCCESS;
}

VkResult
vn_GetFenceFdKHR(VkDevice device,
                 const VkFenceGetFdInfoKHR *pGetFdInfo,
                 int *pFd)
{
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_fence *fence = vn_fence_from_handle(pGetFdInfo->fence);
   const bool sync_file =
      pGetFdInfo->handleType == VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
   struct vn_sync_payload *payload = fence->payload;

   assert(dev->instance->experimental.globalFencing);
   assert(sync_file);
   int fd = -1;
   if (payload->type == VN_SYNC_TYPE_DEVICE_ONLY) {
      VkResult result = vn_create_sync_file(dev, &fd);
      if (result != VK_SUCCESS)
         return vn_error(dev->instance, result);
   }

   if (sync_file) {
      vn_sync_payload_release(dev, &fence->temporary);
      fence->payload = &fence->permanent;

      /* XXX implies reset operation on the host fence */
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

static void
vn_semaphore_reset_wsi(struct vn_device *dev, struct vn_semaphore *sem)
{
   struct vn_sync_payload *perm = &sem->permanent;

   vn_sync_payload_release(dev, &sem->temporary);

   sem->payload = perm;
}

void
vn_semaphore_signal_wsi(struct vn_device *dev, struct vn_semaphore *sem)
{
   struct vn_sync_payload *temp = &sem->temporary;

   vn_sync_payload_release(dev, temp);
   temp->type = VN_SYNC_TYPE_WSI_SIGNALED;
   sem->payload = temp;
}

VkResult
vn_CreateSemaphore(VkDevice device,
                   const VkSemaphoreCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkSemaphore *pSemaphore)
{
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
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_semaphore *sem =
      vn_semaphore_from_handle(pImportSemaphoreFdInfo->semaphore);
   ASSERTED const bool sync_file =
      pImportSemaphoreFdInfo->handleType ==
      VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
   const int fd = pImportSemaphoreFdInfo->fd;

   assert(dev->instance->experimental.globalFencing);
   assert(sync_file);
   if (fd >= 0) {
      if (sync_wait(fd, -1))
         return vn_error(dev->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);

      close(fd);
   }

   /* abuse VN_SYNC_TYPE_WSI_SIGNALED */
   vn_semaphore_signal_wsi(dev, sem);

   return VK_SUCCESS;
}

VkResult
vn_GetSemaphoreFdKHR(VkDevice device,
                     const VkSemaphoreGetFdInfoKHR *pGetFdInfo,
                     int *pFd)
{
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
   }

   if (sync_file) {
      vn_sync_payload_release(dev, &sem->temporary);
      sem->payload = &sem->permanent;

      /* XXX implies wait operation on the host semaphore */
   }

   *pFd = fd;
   return VK_SUCCESS;
}

/* event commands */

VkResult
vn_CreateEvent(VkDevice device,
               const VkEventCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator,
               VkEvent *pEvent)
{
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_event *ev = vk_zalloc(alloc, sizeof(*ev), VN_DEFAULT_ALIGN,
                                   VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!ev)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&ev->base, VK_OBJECT_TYPE_EVENT, &dev->base);

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
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_event *ev = vn_event_from_handle(event);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   if (!ev)
      return;

   vn_async_vkDestroyEvent(dev->instance, device, event, NULL);

   vn_object_base_fini(&ev->base);
   vk_free(alloc, ev);
}

VkResult
vn_GetEventStatus(VkDevice device, VkEvent event)
{
   struct vn_device *dev = vn_device_from_handle(device);

   /* TODO When the renderer supports it (requires a new vk extension), there
    * should be a coherent memory backing the event.
    */
   VkResult result = vn_call_vkGetEventStatus(dev->instance, device, event);

   return vn_result(dev->instance, result);
}

VkResult
vn_SetEvent(VkDevice device, VkEvent event)
{
   struct vn_device *dev = vn_device_from_handle(device);

   VkResult result = vn_call_vkSetEvent(dev->instance, device, event);

   return vn_result(dev->instance, result);
}

VkResult
vn_ResetEvent(VkDevice device, VkEvent event)
{
   struct vn_device *dev = vn_device_from_handle(device);

   VkResult result = vn_call_vkResetEvent(dev->instance, device, event);

   return vn_result(dev->instance, result);
}
