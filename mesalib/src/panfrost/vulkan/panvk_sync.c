/*
 * Copyright (C) 2021 Collabora Ltd.
 *
 * Derived from tu_drm.c which is:
 * Copyright © 2018 Google, Inc.
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

#include <xf86drm.h>

#include "panvk_private.h"

static VkResult
sync_create(struct panvk_device *device,
            struct panvk_syncobj *sync,
            bool signaled)
{
   const struct panfrost_device *pdev = &device->physical_device->pdev;

   struct drm_syncobj_create create = {
      .flags = signaled ? DRM_SYNCOBJ_CREATE_SIGNALED : 0,
   };

   int ret = drmIoctl(pdev->fd, DRM_IOCTL_SYNCOBJ_CREATE, &create);
   if (ret)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   sync->permanent = create.handle;

   return VK_SUCCESS;
}

static void
sync_set_temporary(struct panvk_device *device, struct panvk_syncobj *sync,
                   uint32_t syncobj)
{
   const struct panfrost_device *pdev = &device->physical_device->pdev;

   if (sync->temporary) {
      struct drm_syncobj_destroy destroy = { .handle = sync->temporary };
      drmIoctl(pdev->fd, DRM_IOCTL_SYNCOBJ_DESTROY, &destroy);
   }

   sync->temporary = syncobj;
}

static void
sync_destroy(struct panvk_device *device, struct panvk_syncobj *sync)
{
   const struct panfrost_device *pdev = &device->physical_device->pdev;

   if (!sync)
      return;

   sync_set_temporary(device, sync, 0);
   struct drm_syncobj_destroy destroy = { .handle = sync->permanent };
   drmIoctl(pdev->fd, DRM_IOCTL_SYNCOBJ_DESTROY, &destroy);
}

static VkResult
sync_import(struct panvk_device *device, struct panvk_syncobj *sync,
            bool temporary, bool sync_fd, int fd)
{
   const struct panfrost_device *pdev = &device->physical_device->pdev;
   int ret;

   if (!sync_fd) {
      uint32_t *dst = temporary ? &sync->temporary : &sync->permanent;

      struct drm_syncobj_handle handle = { .fd = fd };
      ret = drmIoctl(pdev->fd, DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE, &handle);
      if (ret)
         return VK_ERROR_INVALID_EXTERNAL_HANDLE;

      if (*dst) {
         struct drm_syncobj_destroy destroy = { .handle = *dst };
         drmIoctl(pdev->fd, DRM_IOCTL_SYNCOBJ_DESTROY, &destroy);
      }
      *dst = handle.handle;
      close(fd);
   } else {
      assert(temporary);

      struct drm_syncobj_create create = {};

      if (fd == -1)
         create.flags |= DRM_SYNCOBJ_CREATE_SIGNALED;

      ret = drmIoctl(pdev->fd, DRM_IOCTL_SYNCOBJ_CREATE, &create);
      if (ret)
         return VK_ERROR_INVALID_EXTERNAL_HANDLE;

      if (fd != -1) {
         struct drm_syncobj_handle handle = {
            .fd = fd,
            .handle = create.handle,
            .flags = DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE,
         };

         ret = drmIoctl(pdev->fd, DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE, &handle);
         if (ret) {
            struct drm_syncobj_destroy destroy = { .handle = create.handle };
            drmIoctl(pdev->fd, DRM_IOCTL_SYNCOBJ_DESTROY, &destroy);
            return VK_ERROR_INVALID_EXTERNAL_HANDLE;
         }
         close(fd);
      }

      sync_set_temporary(device, sync, create.handle);
   }

   return VK_SUCCESS;
}

static VkResult
sync_export(struct panvk_device *device, struct panvk_syncobj *sync,
            bool sync_fd, int *p_fd)
{
   const struct panfrost_device *pdev = &device->physical_device->pdev;

   struct drm_syncobj_handle handle = {
      .handle = sync->temporary ? : sync->permanent,
      .flags = sync_fd ? DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE : 0,
      .fd = -1,
   };
   int ret = drmIoctl(pdev->fd, DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, &handle);
   if (ret)
      return vk_error(device->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);

   /* restore permanent payload on export */
   sync_set_temporary(device, sync, 0);

   *p_fd = handle.fd;
   return VK_SUCCESS;
}

VkResult
panvk_CreateSemaphore(VkDevice _device,
                      const VkSemaphoreCreateInfo *pCreateInfo,
                      const VkAllocationCallbacks *pAllocator,
                      VkSemaphore *pSemaphore)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk_semaphore *sem =
         vk_object_zalloc(&device->vk, pAllocator, sizeof(*sem),
                          VK_OBJECT_TYPE_SEMAPHORE);
   if (!sem)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult ret = sync_create(device, &sem->syncobj, false);
   if (ret != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, sync);
      return ret;
   }

   *pSemaphore = panvk_semaphore_to_handle(sem);
   return VK_SUCCESS;
}

void
panvk_DestroySemaphore(VkDevice _device, VkSemaphore _sem, const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_semaphore, sem, _sem);

   sync_destroy(device, &sem->syncobj);
   vk_object_free(&device->vk, pAllocator, sem);
}

VkResult
panvk_ImportSemaphoreFdKHR(VkDevice _device, const VkImportSemaphoreFdInfoKHR *info)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_semaphore, sem, info->semaphore);
   bool temp = info->flags & VK_SEMAPHORE_IMPORT_TEMPORARY_BIT;
   bool sync_fd = info->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;

   return sync_import(device, &sem->syncobj, temp, sync_fd, info->fd);
}

VkResult
panvk_GetSemaphoreFdKHR(VkDevice _device, const VkSemaphoreGetFdInfoKHR *info, int *pFd)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_semaphore, sem, info->semaphore);
   bool sync_fd = info->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;

   return sync_export(device, &sem->syncobj, sync_fd, pFd);
}

VkResult
panvk_CreateFence(VkDevice _device,
                  const VkFenceCreateInfo *info,
                  const VkAllocationCallbacks *pAllocator,
                  VkFence *pFence)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk_fence *fence =
         vk_object_zalloc(&device->vk, pAllocator, sizeof(*fence),
                          VK_OBJECT_TYPE_FENCE);
   if (!fence)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult ret = sync_create(device, &fence->syncobj,
                              info->flags & VK_FENCE_CREATE_SIGNALED_BIT);
   if (ret != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, pAllocator, fence);
      return ret;
   }

   *pFence = panvk_fence_to_handle(fence);
   return VK_SUCCESS;
}

void
panvk_DestroyFence(VkDevice _device, VkFence _fence,
                   const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_fence, fence, _fence);

   sync_destroy(device, &fence->syncobj);
   vk_object_free(&device->vk, pAllocator, fence);
}

VkResult
panvk_ImportFenceFdKHR(VkDevice _device, const VkImportFenceFdInfoKHR *info)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_fence, fence, info->fence);
   bool sync_fd = info->handleType == VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;
   bool temp = info->flags & VK_FENCE_IMPORT_TEMPORARY_BIT;

   return sync_import(device, &fence->syncobj, temp, sync_fd, info->fd);
}

VkResult
panvk_GetFenceFdKHR(VkDevice _device, const VkFenceGetFdInfoKHR *info, int *pFd)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_fence, fence, info->fence);
   bool sync_fd = info->handleType == VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT;

   return sync_export(device, &fence->syncobj, sync_fd, pFd);
}

static VkResult
drm_syncobj_wait(struct panvk_device *device,
                 const uint32_t *handles, uint32_t count_handles,
                 int64_t timeout_nsec, bool wait_all)
{
   const struct panfrost_device *pdev = &device->physical_device->pdev;
   struct drm_syncobj_wait wait = {
      .handles = (uint64_t) (uintptr_t) handles,
      .count_handles = count_handles,
      .timeout_nsec = timeout_nsec,
      .flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT |
               (wait_all ? DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL : 0)
   };

   int ret = drmIoctl(pdev->fd, DRM_IOCTL_SYNCOBJ_WAIT, &wait);
   if (ret) {
      if (errno == ETIME)
         return VK_TIMEOUT;

      assert(0);
      return VK_ERROR_DEVICE_LOST; /* TODO */
   }
   return VK_SUCCESS;
}

static uint64_t
gettime_ns(void)
{
   struct timespec current;
   clock_gettime(CLOCK_MONOTONIC, &current);
   return (uint64_t)current.tv_sec * 1000000000 + current.tv_nsec;
}

/* and the kernel converts it right back to relative timeout - very smart UAPI */
static uint64_t
absolute_timeout(uint64_t timeout)
{
   if (timeout == 0)
      return 0;
   uint64_t current_time = gettime_ns();
   uint64_t max_timeout = (uint64_t) INT64_MAX - current_time;

   timeout = MIN2(max_timeout, timeout);

   return (current_time + timeout);
}

VkResult
panvk_WaitForFences(VkDevice _device,
                    uint32_t fenceCount,
                    const VkFence *pFences,
                    VkBool32 waitAll,
                    uint64_t timeout)
{
   VK_FROM_HANDLE(panvk_device, device, _device);

   if (panvk_device_is_lost(device))
      return VK_ERROR_DEVICE_LOST;

   uint32_t handles[fenceCount];
   for (unsigned i = 0; i < fenceCount; ++i) {
      VK_FROM_HANDLE(panvk_fence, fence, pFences[i]);

      if (fence->syncobj.temporary) {
         handles[i] = fence->syncobj.temporary;
      } else {
         handles[i] = fence->syncobj.permanent;
      }
   }

   return drm_syncobj_wait(device, handles, fenceCount, absolute_timeout(timeout), waitAll);
}

VkResult
panvk_ResetFences(VkDevice _device, uint32_t fenceCount, const VkFence *pFences)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   const struct panfrost_device *pdev = &device->physical_device->pdev;
   int ret;

   uint32_t handles[fenceCount];
   for (unsigned i = 0; i < fenceCount; ++i) {
      VK_FROM_HANDLE(panvk_fence, fence, pFences[i]);

      sync_set_temporary(device, &fence->syncobj, 0);
      handles[i] = fence->syncobj.permanent;
   }

   struct drm_syncobj_array objs = {
      .handles = (uint64_t) (uintptr_t) handles,
      .count_handles = fenceCount,
   };

   ret = drmIoctl(pdev->fd, DRM_IOCTL_SYNCOBJ_RESET, &objs);
   if (ret) {
      panvk_device_set_lost(device, "DRM_IOCTL_SYNCOBJ_RESET failure: %s",
                         strerror(errno));
   }

   return VK_SUCCESS;
}

VkResult
panvk_GetFenceStatus(VkDevice _device, VkFence _fence)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_fence, fence, _fence);
   uint32_t handle = fence->syncobj.temporary ? : fence->syncobj.permanent;
   VkResult result;

   result = drm_syncobj_wait(device, &handle, 1, 0, false);
   if (result == VK_TIMEOUT)
      result = VK_NOT_READY;
   return result;
}

int
panvk_signal_syncobjs(struct panvk_device *device,
                      struct panvk_syncobj *syncobj1,
                      struct panvk_syncobj *syncobj2)
{
   const struct panfrost_device *pdev = &device->physical_device->pdev;
   uint32_t handles[2], count = 0;

   if (syncobj1)
      handles[count++] = syncobj1->temporary ?: syncobj1->permanent;

   if (syncobj2)
      handles[count++] = syncobj2->temporary ?: syncobj2->permanent;

   if (!count)
      return 0;

   struct drm_syncobj_array objs = {
      .handles = (uintptr_t) handles,
      .count_handles = count
   };

   return drmIoctl(pdev->fd, DRM_IOCTL_SYNCOBJ_SIGNAL, &objs);
}

int
panvk_syncobj_to_fd(struct panvk_device *device, struct panvk_syncobj *sync)
{
   const struct panfrost_device *pdev = &device->physical_device->pdev;
   struct drm_syncobj_handle handle = { .handle = sync->permanent };
   int ret;

   ret = drmIoctl(pdev->fd, DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, &handle);

   return ret ? -1 : handle.fd;
}
