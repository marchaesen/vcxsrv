/*
 * Copyright © 2020 Google, Inc.
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

#include "tu_private.h"

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "msm_kgsl.h"

struct tu_syncobj {
   struct vk_object_base base;
   uint32_t timestamp;
   bool timestamp_valid;
};

static int
safe_ioctl(int fd, unsigned long request, void *arg)
{
   int ret;

   do {
      ret = ioctl(fd, request, arg);
   } while (ret == -1 && (errno == EINTR || errno == EAGAIN));

   return ret;
}

int
tu_drm_submitqueue_new(const struct tu_device *dev,
                       int priority,
                       uint32_t *queue_id)
{
   struct kgsl_drawctxt_create req = {
      .flags = KGSL_CONTEXT_SAVE_GMEM |
              KGSL_CONTEXT_NO_GMEM_ALLOC |
              KGSL_CONTEXT_PREAMBLE,
   };

   int ret = safe_ioctl(dev->physical_device->local_fd, IOCTL_KGSL_DRAWCTXT_CREATE, &req);
   if (ret)
      return ret;

   *queue_id = req.drawctxt_id;

   return 0;
}

void
tu_drm_submitqueue_close(const struct tu_device *dev, uint32_t queue_id)
{
   struct kgsl_drawctxt_destroy req = {
      .drawctxt_id = queue_id,
   };

   safe_ioctl(dev->physical_device->local_fd, IOCTL_KGSL_DRAWCTXT_DESTROY, &req);
}

VkResult
tu_bo_init_new(struct tu_device *dev, struct tu_bo *bo, uint64_t size, bool dump)
{
   struct kgsl_gpumem_alloc_id req = {
      .size = size,
   };
   int ret;

   ret = safe_ioctl(dev->physical_device->local_fd,
                    IOCTL_KGSL_GPUMEM_ALLOC_ID, &req);
   if (ret) {
      return vk_errorf(dev->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                       "GPUMEM_ALLOC_ID failed (%s)", strerror(errno));
   }

   *bo = (struct tu_bo) {
      .gem_handle = req.id,
      .size = req.mmapsize,
      .iova = req.gpuaddr,
   };

   return VK_SUCCESS;
}

VkResult
tu_bo_init_dmabuf(struct tu_device *dev,
                  struct tu_bo *bo,
                  uint64_t size,
                  int fd)
{
   struct kgsl_gpuobj_import_dma_buf import_dmabuf = {
      .fd = fd,
   };
   struct kgsl_gpuobj_import req = {
      .priv = (uintptr_t)&import_dmabuf,
      .priv_len = sizeof(import_dmabuf),
      .flags = 0,
      .type = KGSL_USER_MEM_TYPE_DMABUF,
   };
   int ret;

   ret = safe_ioctl(dev->physical_device->local_fd,
                    IOCTL_KGSL_GPUOBJ_IMPORT, &req);
   if (ret)
      return vk_errorf(dev->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                       "Failed to import dma-buf (%s)\n", strerror(errno));

   struct kgsl_gpuobj_info info_req = {
      .id = req.id,
   };

   ret = safe_ioctl(dev->physical_device->local_fd,
                    IOCTL_KGSL_GPUOBJ_INFO, &info_req);
   if (ret)
      return vk_errorf(dev->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                       "Failed to get dma-buf info (%s)\n", strerror(errno));

   *bo = (struct tu_bo) {
      .gem_handle = req.id,
      .size = info_req.size,
      .iova = info_req.gpuaddr,
   };

   return VK_SUCCESS;
}

int
tu_bo_export_dmabuf(struct tu_device *dev, struct tu_bo *bo)
{
   tu_stub();

   return -1;
}

VkResult
tu_bo_map(struct tu_device *dev, struct tu_bo *bo)
{
   if (bo->map)
      return VK_SUCCESS;

   uint64_t offset = bo->gem_handle << 12;
   void *map = mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                    dev->physical_device->local_fd, offset);
   if (map == MAP_FAILED)
      return vk_error(dev->instance, VK_ERROR_MEMORY_MAP_FAILED);

   bo->map = map;

   return VK_SUCCESS;
}

void
tu_bo_finish(struct tu_device *dev, struct tu_bo *bo)
{
   assert(bo->gem_handle);

   if (bo->map)
      munmap(bo->map, bo->size);

   struct kgsl_gpumem_free_id req = {
      .id = bo->gem_handle
   };

   safe_ioctl(dev->physical_device->local_fd, IOCTL_KGSL_GPUMEM_FREE_ID, &req);
}

static VkResult
get_kgsl_prop(int fd, unsigned int type, void *value, size_t size)
{
   struct kgsl_device_getproperty getprop = {
      .type = type,
      .value = value,
      .sizebytes = size,
   };

   return safe_ioctl(fd, IOCTL_KGSL_DEVICE_GETPROPERTY, &getprop);
}

VkResult
tu_enumerate_devices(struct tu_instance *instance)
{
   static const char path[] = "/dev/kgsl-3d0";
   int fd;

   struct tu_physical_device *device = &instance->physical_devices[0];

   if (instance->enabled_extensions.KHR_display)
      return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                       "I can't KHR_display");

   fd = open(path, O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      instance->physical_device_count = 0;
      return vk_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                       "failed to open device %s", path);
   }

   struct kgsl_devinfo info;
   if (get_kgsl_prop(fd, KGSL_PROP_DEVICE_INFO, &info, sizeof(info)))
      goto fail;

   uint64_t gmem_iova;
   if (get_kgsl_prop(fd, KGSL_PROP_UCHE_GMEM_VADDR, &gmem_iova, sizeof(gmem_iova)))
      goto fail;

   /* kgsl version check? */

   if (instance->debug_flags & TU_DEBUG_STARTUP)
      mesa_logi("Found compatible device '%s'.", path);

   vk_object_base_init(NULL, &device->base, VK_OBJECT_TYPE_PHYSICAL_DEVICE);
   device->instance = instance;
   device->master_fd = -1;
   device->local_fd = fd;

   device->gpu_id =
      ((info.chip_id >> 24) & 0xff) * 100 +
      ((info.chip_id >> 16) & 0xff) * 10 +
      ((info.chip_id >>  8) & 0xff);
   device->gmem_size = info.gmem_sizebytes;
   device->gmem_base = gmem_iova;

   if (tu_physical_device_init(device, instance) != VK_SUCCESS)
      goto fail;

   instance->physical_device_count = 1;

   return VK_SUCCESS;

fail:
   close(fd);
   return VK_ERROR_INITIALIZATION_FAILED;
}

static int
timestamp_to_fd(struct tu_queue *queue, uint32_t timestamp)
{
   int fd;
   struct kgsl_timestamp_event event = {
      .type = KGSL_TIMESTAMP_EVENT_FENCE,
      .context_id = queue->msm_queue_id,
      .timestamp = timestamp,
      .priv = &fd,
      .len = sizeof(fd),
   };

   int ret = safe_ioctl(queue->device->fd, IOCTL_KGSL_TIMESTAMP_EVENT, &event);
   if (ret)
      return -1;

   return fd;
}

/* return true if timestamp a is greater (more recent) then b
 * this relies on timestamps never having a difference > (1<<31)
 */
static inline bool
timestamp_cmp(uint32_t a, uint32_t b)
{
   return (int32_t) (a - b) >= 0;
}

static uint32_t
max_ts(uint32_t a, uint32_t b)
{
   return timestamp_cmp(a, b) ? a : b;
}

static uint32_t
min_ts(uint32_t a, uint32_t b)
{
   return timestamp_cmp(a, b) ? b : a;
}

static struct tu_syncobj
sync_merge(const VkSemaphore *syncobjs, uint32_t count, bool wait_all, bool reset)
{
   struct tu_syncobj ret;

   ret.timestamp_valid = false;

   for (uint32_t i = 0; i < count; ++i) {
      TU_FROM_HANDLE(tu_syncobj, sync, syncobjs[i]);

      /* TODO: this means the fence is unsignaled and will never become signaled */
      if (!sync->timestamp_valid)
         continue;

      if (!ret.timestamp_valid)
         ret.timestamp = sync->timestamp;
      else if (wait_all)
         ret.timestamp = max_ts(ret.timestamp, sync->timestamp);
      else
         ret.timestamp = min_ts(ret.timestamp, sync->timestamp);

      ret.timestamp_valid = true;
      if (reset)
         sync->timestamp_valid = false;

   }
   return ret;
}

VkResult
tu_QueueSubmit(VkQueue _queue,
               uint32_t submitCount,
               const VkSubmitInfo *pSubmits,
               VkFence _fence)
{
   TU_FROM_HANDLE(tu_queue, queue, _queue);
   TU_FROM_HANDLE(tu_syncobj, fence, _fence);
   VkResult result = VK_SUCCESS;

   uint32_t max_entry_count = 0;
   for (uint32_t i = 0; i < submitCount; ++i) {
      const VkSubmitInfo *submit = pSubmits + i;

      uint32_t entry_count = 0;
      for (uint32_t j = 0; j < submit->commandBufferCount; ++j) {
         TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, submit->pCommandBuffers[j]);
         entry_count += cmdbuf->cs.entry_count;
      }

      max_entry_count = MAX2(max_entry_count, entry_count);
   }

   struct kgsl_command_object *cmds =
      vk_alloc(&queue->device->vk.alloc,
               sizeof(cmds[0]) * max_entry_count, 8,
               VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (cmds == NULL)
      return vk_error(queue->device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   for (uint32_t i = 0; i < submitCount; ++i) {
      const VkSubmitInfo *submit = pSubmits + i;
      uint32_t entry_idx = 0;

      for (uint32_t j = 0; j < submit->commandBufferCount; j++) {
         TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, submit->pCommandBuffers[j]);
         struct tu_cs *cs = &cmdbuf->cs;
         for (unsigned k = 0; k < cs->entry_count; k++) {
            cmds[entry_idx++] = (struct kgsl_command_object) {
               .offset = cs->entries[k].offset,
               .gpuaddr = cs->entries[k].bo->iova,
               .size = cs->entries[k].size,
               .flags = KGSL_CMDLIST_IB,
               .id = cs->entries[k].bo->gem_handle,
            };
         }
      }

      struct tu_syncobj s = sync_merge(submit->pWaitSemaphores,
                                       submit->waitSemaphoreCount,
                                       true, true);

      struct kgsl_cmd_syncpoint_timestamp ts = {
         .context_id = queue->msm_queue_id,
         .timestamp = s.timestamp,
      };
      struct kgsl_command_syncpoint sync = {
         .type = KGSL_CMD_SYNCPOINT_TYPE_TIMESTAMP,
         .size = sizeof(ts),
         .priv = (uintptr_t) &ts,
      };

      struct kgsl_gpu_command req = {
         .flags = KGSL_CMDBATCH_SUBMIT_IB_LIST,
         .context_id = queue->msm_queue_id,
         .cmdlist = (uint64_t) (uintptr_t) cmds,
         .numcmds = entry_idx,
         .cmdsize = sizeof(struct kgsl_command_object),
         .synclist = (uintptr_t) &sync,
         .syncsize = sizeof(struct kgsl_command_syncpoint),
         .numsyncs = s.timestamp_valid ? 1 : 0,
      };

      int ret = safe_ioctl(queue->device->physical_device->local_fd,
                           IOCTL_KGSL_GPU_COMMAND, &req);
      if (ret) {
         result = tu_device_set_lost(queue->device,
                                     "submit failed: %s\n", strerror(errno));
         goto fail;
      }

      for (uint32_t i = 0; i < submit->signalSemaphoreCount; i++) {
         TU_FROM_HANDLE(tu_syncobj, sem, submit->pSignalSemaphores[i]);
         sem->timestamp = req.timestamp;
         sem->timestamp_valid = true;
      }

      /* no need to merge fences as queue execution is serialized */
      if (i == submitCount - 1) {
         int fd = timestamp_to_fd(queue, req.timestamp);
         if (fd < 0) {
            result = tu_device_set_lost(queue->device,
                                        "Failed to create sync file for timestamp: %s\n",
                                        strerror(errno));
            goto fail;
         }

         if (queue->fence >= 0)
            close(queue->fence);
         queue->fence = fd;

         if (fence) {
            fence->timestamp = req.timestamp;
            fence->timestamp_valid = true;
         }
      }
   }
fail:
   vk_free(&queue->device->vk.alloc, cmds);

   return result;
}

static VkResult
sync_create(VkDevice _device,
            bool signaled,
            bool fence,
            const VkAllocationCallbacks *pAllocator,
            void **p_sync)
{
   TU_FROM_HANDLE(tu_device, device, _device);

   struct tu_syncobj *sync =
         vk_object_alloc(&device->vk, pAllocator, sizeof(*sync),
                         fence ? VK_OBJECT_TYPE_FENCE : VK_OBJECT_TYPE_SEMAPHORE);
   if (!sync)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (signaled)
      tu_finishme("CREATE FENCE SIGNALED");

   sync->timestamp_valid = false;
   *p_sync = sync;

   return VK_SUCCESS;
}

VkResult
tu_ImportSemaphoreFdKHR(VkDevice _device,
                        const VkImportSemaphoreFdInfoKHR *pImportSemaphoreFdInfo)
{
   tu_finishme("ImportSemaphoreFdKHR");
   return VK_SUCCESS;
}

VkResult
tu_GetSemaphoreFdKHR(VkDevice _device,
                     const VkSemaphoreGetFdInfoKHR *pGetFdInfo,
                     int *pFd)
{
   tu_finishme("GetSemaphoreFdKHR");
   return VK_SUCCESS;
}

VkResult
tu_CreateSemaphore(VkDevice device,
                   const VkSemaphoreCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkSemaphore *pSemaphore)
{
   return sync_create(device, false, false, pAllocator, (void**) pSemaphore);
}

void
tu_DestroySemaphore(VkDevice _device,
                    VkSemaphore semaphore,
                    const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_syncobj, sync, semaphore);

   if (!sync)
      return;

   vk_object_free(&device->vk, pAllocator, sync);
}

VkResult
tu_ImportFenceFdKHR(VkDevice _device,
                    const VkImportFenceFdInfoKHR *pImportFenceFdInfo)
{
   tu_stub();

   return VK_SUCCESS;
}

VkResult
tu_GetFenceFdKHR(VkDevice _device,
                 const VkFenceGetFdInfoKHR *pGetFdInfo,
                 int *pFd)
{
   tu_stub();

   return VK_SUCCESS;
}

VkResult
tu_CreateFence(VkDevice device,
               const VkFenceCreateInfo *info,
               const VkAllocationCallbacks *pAllocator,
               VkFence *pFence)
{
   return sync_create(device, info->flags & VK_FENCE_CREATE_SIGNALED_BIT, true,
                      pAllocator, (void**) pFence);
}

void
tu_DestroyFence(VkDevice _device, VkFence fence, const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_syncobj, sync, fence);

   if (!sync)
      return;

   vk_object_free(&device->vk, pAllocator, sync);
}

VkResult
tu_WaitForFences(VkDevice _device,
                 uint32_t count,
                 const VkFence *pFences,
                 VkBool32 waitAll,
                 uint64_t timeout)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   struct tu_syncobj s = sync_merge((const VkSemaphore*) pFences, count, waitAll, false);

   if (!s.timestamp_valid)
      return VK_SUCCESS;

   int ret = ioctl(device->fd, IOCTL_KGSL_DEVICE_WAITTIMESTAMP_CTXTID,
                   &(struct kgsl_device_waittimestamp_ctxtid) {
      .context_id = device->queues[0]->msm_queue_id,
      .timestamp = s.timestamp,
      .timeout = timeout / 1000000,
   });
   if (ret) {
      assert(errno == ETIME);
      return VK_TIMEOUT;
   }

   return VK_SUCCESS;
}

VkResult
tu_ResetFences(VkDevice _device, uint32_t count, const VkFence *pFences)
{
   for (uint32_t i = 0; i < count; i++) {
      TU_FROM_HANDLE(tu_syncobj, sync, pFences[i]);
      sync->timestamp_valid = false;
   }
   return VK_SUCCESS;
}

VkResult
tu_GetFenceStatus(VkDevice _device, VkFence _fence)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_syncobj, sync, _fence);

   if (!sync->timestamp_valid)
      return VK_NOT_READY;

   int ret = ioctl(device->fd, IOCTL_KGSL_DEVICE_WAITTIMESTAMP_CTXTID,
               &(struct kgsl_device_waittimestamp_ctxtid) {
      .context_id = device->queues[0]->msm_queue_id,
      .timestamp = sync->timestamp,
      .timeout = 0,
   });
   if (ret) {
      assert(errno == ETIME);
      return VK_NOT_READY;
   }

   return VK_SUCCESS;
}

int
tu_signal_fences(struct tu_device *device, struct tu_syncobj *fence1, struct tu_syncobj *fence2)
{
   tu_finishme("tu_signal_fences");
   return 0;
}

int
tu_syncobj_to_fd(struct tu_device *device, struct tu_syncobj *sync)
{
   tu_finishme("tu_syncobj_to_fd");
   return -1;
}

#ifdef ANDROID
VkResult
tu_QueueSignalReleaseImageANDROID(VkQueue _queue,
                                  uint32_t waitSemaphoreCount,
                                  const VkSemaphore *pWaitSemaphores,
                                  VkImage image,
                                  int *pNativeFenceFd)
{
   TU_FROM_HANDLE(tu_queue, queue, _queue);
   if (!pNativeFenceFd)
      return VK_SUCCESS;

   struct tu_syncobj s = sync_merge(pWaitSemaphores, waitSemaphoreCount, true, true);

   if (!s.timestamp_valid) {
      *pNativeFenceFd = -1;
      return VK_SUCCESS;
   }

   *pNativeFenceFd = timestamp_to_fd(queue, s.timestamp);

   return VK_SUCCESS;
}
#endif
