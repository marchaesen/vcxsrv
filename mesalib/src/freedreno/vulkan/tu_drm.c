/*
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

#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <xf86drm.h>

#include "vk_util.h"

#include "drm-uapi/msm_drm.h"

#include "tu_private.h"

struct tu_syncobj {
   struct vk_object_base base;
   uint32_t permanent, temporary;
};

static int
tu_drm_get_param(const struct tu_physical_device *dev,
                 uint32_t param,
                 uint64_t *value)
{
   /* Technically this requires a pipe, but the kernel only supports one pipe
    * anyway at the time of writing and most of these are clearly pipe
    * independent. */
   struct drm_msm_param req = {
      .pipe = MSM_PIPE_3D0,
      .param = param,
   };

   int ret = drmCommandWriteRead(dev->local_fd, DRM_MSM_GET_PARAM, &req,
                                 sizeof(req));
   if (ret)
      return ret;

   *value = req.value;

   return 0;
}

static int
tu_drm_get_gpu_id(const struct tu_physical_device *dev, uint32_t *id)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev, MSM_PARAM_GPU_ID, &value);
   if (ret)
      return ret;

   *id = value;
   return 0;
}

static int
tu_drm_get_gmem_size(const struct tu_physical_device *dev, uint32_t *size)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev, MSM_PARAM_GMEM_SIZE, &value);
   if (ret)
      return ret;

   *size = value;
   return 0;
}

static int
tu_drm_get_gmem_base(const struct tu_physical_device *dev, uint64_t *base)
{
   return tu_drm_get_param(dev, MSM_PARAM_GMEM_BASE, base);
}

int
tu_drm_submitqueue_new(const struct tu_device *dev,
                       int priority,
                       uint32_t *queue_id)
{
   struct drm_msm_submitqueue req = {
      .flags = 0,
      .prio = priority,
   };

   int ret = drmCommandWriteRead(dev->fd,
                                 DRM_MSM_SUBMITQUEUE_NEW, &req, sizeof(req));
   if (ret)
      return ret;

   *queue_id = req.id;
   return 0;
}

void
tu_drm_submitqueue_close(const struct tu_device *dev, uint32_t queue_id)
{
   drmCommandWrite(dev->fd, DRM_MSM_SUBMITQUEUE_CLOSE,
                   &queue_id, sizeof(uint32_t));
}

static void
tu_gem_close(const struct tu_device *dev, uint32_t gem_handle)
{
   struct drm_gem_close req = {
      .handle = gem_handle,
   };

   drmIoctl(dev->fd, DRM_IOCTL_GEM_CLOSE, &req);
}

/** Helper for DRM_MSM_GEM_INFO, returns 0 on error. */
static uint64_t
tu_gem_info(const struct tu_device *dev, uint32_t gem_handle, uint32_t info)
{
   struct drm_msm_gem_info req = {
      .handle = gem_handle,
      .info = info,
   };

   int ret = drmCommandWriteRead(dev->fd,
                                 DRM_MSM_GEM_INFO, &req, sizeof(req));
   if (ret < 0)
      return 0;

   return req.value;
}

static VkResult
tu_bo_init(struct tu_device *dev,
           struct tu_bo *bo,
           uint32_t gem_handle,
           uint64_t size,
           bool dump)
{
   uint64_t iova = tu_gem_info(dev, gem_handle, MSM_INFO_GET_IOVA);
   if (!iova) {
      tu_gem_close(dev, gem_handle);
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;
   }

   *bo = (struct tu_bo) {
      .gem_handle = gem_handle,
      .size = size,
      .iova = iova,
   };

   mtx_lock(&dev->bo_mutex);
   uint32_t idx = dev->bo_count++;

   /* grow the bo list if needed */
   if (idx >= dev->bo_list_size) {
      uint32_t new_len = idx + 64;
      struct drm_msm_gem_submit_bo *new_ptr =
         vk_realloc(&dev->vk.alloc, dev->bo_list, new_len * sizeof(*dev->bo_list),
                    8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
      if (!new_ptr) {
         tu_gem_close(dev, gem_handle);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      dev->bo_list = new_ptr;
      dev->bo_list_size = new_len;
   }

   /* grow the "bo idx" list (maps gem handles to index in the bo list) */
   if (bo->gem_handle >= dev->bo_idx_size) {
      uint32_t new_len = bo->gem_handle + 256;
      uint32_t *new_ptr =
         vk_realloc(&dev->vk.alloc, dev->bo_idx, new_len * sizeof(*dev->bo_idx),
                    8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
      if (!new_ptr) {
         tu_gem_close(dev, gem_handle);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      dev->bo_idx = new_ptr;
      dev->bo_idx_size = new_len;
   }

   dev->bo_idx[bo->gem_handle] = idx;
   dev->bo_list[idx] = (struct drm_msm_gem_submit_bo) {
      .flags = MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE |
               COND(dump, MSM_SUBMIT_BO_DUMP),
      .handle = gem_handle,
      .presumed = iova,
   };
   mtx_unlock(&dev->bo_mutex);

   return VK_SUCCESS;
}

VkResult
tu_bo_init_new(struct tu_device *dev, struct tu_bo *bo, uint64_t size, bool dump)
{
   /* TODO: Choose better flags. As of 2018-11-12, freedreno/drm/msm_bo.c
    * always sets `flags = MSM_BO_WC`, and we copy that behavior here.
    */
   struct drm_msm_gem_new req = {
      .size = size,
      .flags = MSM_BO_WC
   };

   int ret = drmCommandWriteRead(dev->fd,
                                 DRM_MSM_GEM_NEW, &req, sizeof(req));
   if (ret)
      return vk_error(dev->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   return tu_bo_init(dev, bo, req.handle, size, dump);
}

VkResult
tu_bo_init_dmabuf(struct tu_device *dev,
                  struct tu_bo *bo,
                  uint64_t size,
                  int prime_fd)
{
   /* lseek() to get the real size */
   off_t real_size = lseek(prime_fd, 0, SEEK_END);
   lseek(prime_fd, 0, SEEK_SET);
   if (real_size < 0 || (uint64_t) real_size < size)
      return vk_error(dev->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);

   uint32_t gem_handle;
   int ret = drmPrimeFDToHandle(dev->fd, prime_fd,
                                &gem_handle);
   if (ret)
      return vk_error(dev->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);

   return tu_bo_init(dev, bo, gem_handle, size, false);
}

int
tu_bo_export_dmabuf(struct tu_device *dev, struct tu_bo *bo)
{
   int prime_fd;
   int ret = drmPrimeHandleToFD(dev->fd, bo->gem_handle,
                                DRM_CLOEXEC, &prime_fd);

   return ret == 0 ? prime_fd : -1;
}

VkResult
tu_bo_map(struct tu_device *dev, struct tu_bo *bo)
{
   if (bo->map)
      return VK_SUCCESS;

   uint64_t offset = tu_gem_info(dev, bo->gem_handle, MSM_INFO_GET_OFFSET);
   if (!offset)
      return vk_error(dev->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   /* TODO: Should we use the wrapper os_mmap() like Freedreno does? */
   void *map = mmap(0, bo->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                    dev->fd, offset);
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

   mtx_lock(&dev->bo_mutex);
   uint32_t idx = dev->bo_idx[bo->gem_handle];
   dev->bo_count--;
   dev->bo_list[idx] = dev->bo_list[dev->bo_count];
   dev->bo_idx[dev->bo_list[idx].handle] = idx;
   mtx_unlock(&dev->bo_mutex);

   tu_gem_close(dev, bo->gem_handle);
}

static VkResult
tu_drm_device_init(struct tu_physical_device *device,
                   struct tu_instance *instance,
                   drmDevicePtr drm_device)
{
   const char *path = drm_device->nodes[DRM_NODE_RENDER];
   VkResult result = VK_SUCCESS;
   drmVersionPtr version;
   int fd;
   int master_fd = -1;

   fd = open(path, O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      return vk_startup_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                               "failed to open device %s", path);
   }

   /* Version 1.6 added SYNCOBJ support. */
   const int min_version_major = 1;
   const int min_version_minor = 6;

   version = drmGetVersion(fd);
   if (!version) {
      close(fd);
      return vk_startup_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                               "failed to query kernel driver version for device %s",
                               path);
   }

   if (strcmp(version->name, "msm")) {
      drmFreeVersion(version);
      close(fd);
      return vk_startup_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                               "device %s does not use the msm kernel driver",
                               path);
   }

   if (version->version_major != min_version_major ||
       version->version_minor < min_version_minor) {
      result = vk_startup_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                                 "kernel driver for device %s has version %d.%d, "
                                 "but Vulkan requires version >= %d.%d",
                                 path,
                                 version->version_major, version->version_minor,
                                 min_version_major, min_version_minor);
      drmFreeVersion(version);
      close(fd);
      return result;
   }

   device->msm_major_version = version->version_major;
   device->msm_minor_version = version->version_minor;

   drmFreeVersion(version);

   if (instance->debug_flags & TU_DEBUG_STARTUP)
      mesa_logi("Found compatible device '%s'.", path);

   vk_object_base_init(NULL, &device->base, VK_OBJECT_TYPE_PHYSICAL_DEVICE);
   device->instance = instance;

   if (instance->enabled_extensions.KHR_display) {
      master_fd =
         open(drm_device->nodes[DRM_NODE_PRIMARY], O_RDWR | O_CLOEXEC);
      if (master_fd >= 0) {
         /* TODO: free master_fd is accel is not working? */
      }
   }

   device->master_fd = master_fd;
   device->local_fd = fd;

   if (tu_drm_get_gpu_id(device, &device->gpu_id)) {
      if (instance->debug_flags & TU_DEBUG_STARTUP)
         mesa_logi("Could not query the GPU ID");
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                         "could not get GPU ID");
      goto fail;
   }

   if (tu_drm_get_gmem_size(device, &device->gmem_size)) {
      if (instance->debug_flags & TU_DEBUG_STARTUP)
         mesa_logi("Could not query the GMEM size");
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                         "could not get GMEM size");
      goto fail;
   }

   if (tu_drm_get_gmem_base(device, &device->gmem_base)) {
      if (instance->debug_flags & TU_DEBUG_STARTUP)
         mesa_logi("Could not query the GMEM size");
      result = vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                         "could not get GMEM size");
      goto fail;
   }

   return tu_physical_device_init(device, instance);

fail:
   close(fd);
   if (master_fd != -1)
      close(master_fd);
   return result;
}

VkResult
tu_enumerate_devices(struct tu_instance *instance)
{
   /* TODO: Check for more devices ? */
   drmDevicePtr devices[8];
   VkResult result = VK_ERROR_INCOMPATIBLE_DRIVER;
   int max_devices;

   instance->physical_device_count = 0;

   max_devices = drmGetDevices2(0, devices, ARRAY_SIZE(devices));

   if (instance->debug_flags & TU_DEBUG_STARTUP) {
      if (max_devices < 0)
         mesa_logi("drmGetDevices2 returned error: %s\n", strerror(max_devices));
      else
         mesa_logi("Found %d drm nodes", max_devices);
   }

   if (max_devices < 1)
      return vk_startup_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                               "No DRM devices found");

   for (unsigned i = 0; i < (unsigned) max_devices; i++) {
      if (devices[i]->available_nodes & 1 << DRM_NODE_RENDER &&
          devices[i]->bustype == DRM_BUS_PLATFORM) {

         result = tu_drm_device_init(
            instance->physical_devices + instance->physical_device_count,
            instance, devices[i]);
         if (result == VK_SUCCESS)
            ++instance->physical_device_count;
         else if (result != VK_ERROR_INCOMPATIBLE_DRIVER)
            break;
      }
   }
   drmFreeDevices(devices, max_devices);

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

   struct drm_syncobj_create create = {};
   if (signaled)
      create.flags |= DRM_SYNCOBJ_CREATE_SIGNALED;

   int ret = ioctl(device->fd, DRM_IOCTL_SYNCOBJ_CREATE, &create);
   if (ret) {
      vk_free2(&device->vk.alloc, pAllocator, sync);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   sync->permanent = create.handle;
   sync->temporary = 0;
   *p_sync = sync;

   return VK_SUCCESS;
}

static void
sync_set_temporary(struct tu_device *device, struct tu_syncobj *sync, uint32_t syncobj)
{
   if (sync->temporary) {
      ioctl(device->fd, DRM_IOCTL_SYNCOBJ_DESTROY,
            &(struct drm_syncobj_destroy) { .handle = sync->temporary });
   }
   sync->temporary = syncobj;
}

static void
sync_destroy(VkDevice _device, struct tu_syncobj *sync, const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);

   if (!sync)
      return;

   sync_set_temporary(device, sync, 0);
   ioctl(device->fd, DRM_IOCTL_SYNCOBJ_DESTROY,
         &(struct drm_syncobj_destroy) { .handle = sync->permanent });

   vk_object_free(&device->vk, pAllocator, sync);
}

static VkResult
sync_import(VkDevice _device, struct tu_syncobj *sync, bool temporary, bool sync_fd, int fd)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   int ret;

   if (!sync_fd) {
      uint32_t *dst = temporary ? &sync->temporary : &sync->permanent;

      struct drm_syncobj_handle handle = { .fd = fd };
      ret = ioctl(device->fd, DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE, &handle);
      if (ret)
         return VK_ERROR_INVALID_EXTERNAL_HANDLE;

      if (*dst) {
         ioctl(device->fd, DRM_IOCTL_SYNCOBJ_DESTROY,
               &(struct drm_syncobj_destroy) { .handle = *dst });
      }
      *dst = handle.handle;
      close(fd);
   } else {
      assert(temporary);

      struct drm_syncobj_create create = {};

      if (fd == -1)
         create.flags |= DRM_SYNCOBJ_CREATE_SIGNALED;

      ret = ioctl(device->fd, DRM_IOCTL_SYNCOBJ_CREATE, &create);
      if (ret)
         return VK_ERROR_INVALID_EXTERNAL_HANDLE;

      if (fd != -1) {
         ret = ioctl(device->fd, DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE, &(struct drm_syncobj_handle) {
            .fd = fd,
            .handle = create.handle,
            .flags = DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE,
         });
         if (ret) {
            ioctl(device->fd, DRM_IOCTL_SYNCOBJ_DESTROY,
                  &(struct drm_syncobj_destroy) { .handle = create.handle });
            return VK_ERROR_INVALID_EXTERNAL_HANDLE;
         }
         close(fd);
      }

      sync_set_temporary(device, sync, create.handle);
   }

   return VK_SUCCESS;
}

static VkResult
sync_export(VkDevice _device, struct tu_syncobj *sync, bool sync_fd, int *p_fd)
{
   TU_FROM_HANDLE(tu_device, device, _device);

   struct drm_syncobj_handle handle = {
      .handle = sync->temporary ?: sync->permanent,
      .flags = COND(sync_fd, DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE),
      .fd = -1,
   };
   int ret = ioctl(device->fd, DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, &handle);
   if (ret)
      return vk_error(device->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);

   /* restore permanent payload on export */
   sync_set_temporary(device, sync, 0);

   *p_fd = handle.fd;
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
tu_DestroySemaphore(VkDevice device, VkSemaphore sem, const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_syncobj, sync, sem);
   sync_destroy(device, sync, pAllocator);
}

VkResult
tu_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR *info)
{
   TU_FROM_HANDLE(tu_syncobj, sync, info->semaphore);
   return sync_import(device, sync, info->flags & VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
         info->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT, info->fd);
}

VkResult
tu_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR *info, int *pFd)
{
   TU_FROM_HANDLE(tu_syncobj, sync, info->semaphore);
   return sync_export(device, sync,
         info->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT, pFd);
}

void
tu_GetPhysicalDeviceExternalSemaphoreProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalSemaphoreInfo *pExternalSemaphoreInfo,
   VkExternalSemaphoreProperties *pExternalSemaphoreProperties)
{
   if (pExternalSemaphoreInfo->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT ||
       pExternalSemaphoreInfo->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT) {
      pExternalSemaphoreProperties->exportFromImportedHandleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT | VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
      pExternalSemaphoreProperties->compatibleHandleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT | VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
      pExternalSemaphoreProperties->externalSemaphoreFeatures = VK_EXTERNAL_SEMAPHORE_FEATURE_EXPORTABLE_BIT |
         VK_EXTERNAL_SEMAPHORE_FEATURE_IMPORTABLE_BIT;
   } else {
      pExternalSemaphoreProperties->exportFromImportedHandleTypes = 0;
      pExternalSemaphoreProperties->compatibleHandleTypes = 0;
      pExternalSemaphoreProperties->externalSemaphoreFeatures = 0;
   }
}

VkResult
tu_QueueSubmit(VkQueue _queue,
               uint32_t submitCount,
               const VkSubmitInfo *pSubmits,
               VkFence _fence)
{
   TU_FROM_HANDLE(tu_queue, queue, _queue);
   TU_FROM_HANDLE(tu_syncobj, fence, _fence);

   for (uint32_t i = 0; i < submitCount; ++i) {
      const VkSubmitInfo *submit = pSubmits + i;
      const bool last_submit = (i == submitCount - 1);
      uint32_t out_syncobjs_size = submit->signalSemaphoreCount;
      if (last_submit && fence)
         out_syncobjs_size += 1;
      /* note: assuming there won't be any very large semaphore counts */
      struct drm_msm_gem_submit_syncobj in_syncobjs[submit->waitSemaphoreCount];
      struct drm_msm_gem_submit_syncobj out_syncobjs[out_syncobjs_size];
      uint32_t nr_in_syncobjs = 0, nr_out_syncobjs = 0;

      for (uint32_t i = 0; i < submit->waitSemaphoreCount; i++) {
         TU_FROM_HANDLE(tu_syncobj, sem, submit->pWaitSemaphores[i]);
         in_syncobjs[nr_in_syncobjs++] = (struct drm_msm_gem_submit_syncobj) {
            .handle = sem->temporary ?: sem->permanent,
            .flags = MSM_SUBMIT_SYNCOBJ_RESET,
         };
      }

      for (uint32_t i = 0; i < submit->signalSemaphoreCount; i++) {
         TU_FROM_HANDLE(tu_syncobj, sem, submit->pSignalSemaphores[i]);
         out_syncobjs[nr_out_syncobjs++] = (struct drm_msm_gem_submit_syncobj) {
            .handle = sem->temporary ?: sem->permanent,
            .flags = 0,
         };
      }

      if (last_submit && fence) {
         out_syncobjs[nr_out_syncobjs++] = (struct drm_msm_gem_submit_syncobj) {
            .handle = fence->temporary ?: fence->permanent,
            .flags = 0,
         };
      }

      uint32_t entry_count = 0;
      for (uint32_t j = 0; j < submit->commandBufferCount; ++j) {
         TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, submit->pCommandBuffers[j]);
         entry_count += cmdbuf->cs.entry_count;
      }

      mtx_lock(&queue->device->bo_mutex);

      struct drm_msm_gem_submit_cmd cmds[entry_count];
      uint32_t entry_idx = 0;
      for (uint32_t j = 0; j < submit->commandBufferCount; ++j) {
         TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, submit->pCommandBuffers[j]);
         struct tu_cs *cs = &cmdbuf->cs;
         for (unsigned i = 0; i < cs->entry_count; ++i, ++entry_idx) {
            cmds[entry_idx].type = MSM_SUBMIT_CMD_BUF;
            cmds[entry_idx].submit_idx =
               queue->device->bo_idx[cs->entries[i].bo->gem_handle];
            cmds[entry_idx].submit_offset = cs->entries[i].offset;
            cmds[entry_idx].size = cs->entries[i].size;
            cmds[entry_idx].pad = 0;
            cmds[entry_idx].nr_relocs = 0;
            cmds[entry_idx].relocs = 0;
         }
      }

      uint32_t flags = MSM_PIPE_3D0;
      if (nr_in_syncobjs) {
         flags |= MSM_SUBMIT_SYNCOBJ_IN;
      }
      if (nr_out_syncobjs) {
         flags |= MSM_SUBMIT_SYNCOBJ_OUT;
      }
      if (last_submit) {
         flags |= MSM_SUBMIT_FENCE_FD_OUT;
      }

      struct drm_msm_gem_submit req = {
         .flags = flags,
         .queueid = queue->msm_queue_id,
         .bos = (uint64_t)(uintptr_t) queue->device->bo_list,
         .nr_bos = queue->device->bo_count,
         .cmds = (uint64_t)(uintptr_t)cmds,
         .nr_cmds = entry_count,
         .in_syncobjs = (uint64_t)(uintptr_t)in_syncobjs,
         .out_syncobjs = (uint64_t)(uintptr_t)out_syncobjs,
         .nr_in_syncobjs = nr_in_syncobjs,
         .nr_out_syncobjs = nr_out_syncobjs,
         .syncobj_stride = sizeof(struct drm_msm_gem_submit_syncobj),
      };

      int ret = drmCommandWriteRead(queue->device->fd,
                                    DRM_MSM_GEM_SUBMIT,
                                    &req, sizeof(req));
      mtx_unlock(&queue->device->bo_mutex);
      if (ret) {
         return tu_device_set_lost(queue->device, "submit failed: %s\n",
                                   strerror(errno));
      }

      /* restore permanent payload on wait */
      for (uint32_t i = 0; i < submit->waitSemaphoreCount; i++) {
         TU_FROM_HANDLE(tu_syncobj, sem, submit->pWaitSemaphores[i]);
         sync_set_temporary(queue->device, sem, 0);
      }

      if (last_submit) {
         if (queue->fence >= 0)
            close(queue->fence);
         queue->fence = req.fence_fd;
      }
   }

   if (!submitCount && fence) {
      /* signal fence imemediately since we don't have a submit to do it */
      ioctl(queue->device->fd, DRM_IOCTL_SYNCOBJ_SIGNAL, &(struct drm_syncobj_array) {
         .handles = (uintptr_t) (uint32_t[]) { fence->temporary ?: fence->permanent },
         .count_handles = 1,
      });
   }

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
tu_DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_syncobj, sync, fence);
   sync_destroy(device, sync, pAllocator);
}

VkResult
tu_ImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR *info)
{
   TU_FROM_HANDLE(tu_syncobj, sync, info->fence);
   return sync_import(device, sync, info->flags & VK_FENCE_IMPORT_TEMPORARY_BIT,
         info->handleType == VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT, info->fd);
}

VkResult
tu_GetFenceFdKHR(VkDevice device, const VkFenceGetFdInfoKHR *info, int *pFd)
{
   TU_FROM_HANDLE(tu_syncobj, sync, info->fence);
   return sync_export(device, sync,
         info->handleType == VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT, pFd);
}

static VkResult
drm_syncobj_wait(struct tu_device *device,
                 const uint32_t *handles, uint32_t count_handles,
                 int64_t timeout_nsec, bool wait_all)
{
   int ret = ioctl(device->fd, DRM_IOCTL_SYNCOBJ_WAIT, &(struct drm_syncobj_wait) {
      .handles = (uint64_t) (uintptr_t) handles,
      .count_handles = count_handles,
      .timeout_nsec = timeout_nsec,
      .flags = DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT |
               COND(wait_all, DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL)
   });
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
tu_WaitForFences(VkDevice _device,
                 uint32_t fenceCount,
                 const VkFence *pFences,
                 VkBool32 waitAll,
                 uint64_t timeout)
{
   TU_FROM_HANDLE(tu_device, device, _device);

   if (tu_device_is_lost(device))
      return VK_ERROR_DEVICE_LOST;

   uint32_t handles[fenceCount];
   for (unsigned i = 0; i < fenceCount; ++i) {
      TU_FROM_HANDLE(tu_syncobj, fence, pFences[i]);
      handles[i] = fence->temporary ?: fence->permanent;
   }

   return drm_syncobj_wait(device, handles, fenceCount, absolute_timeout(timeout), waitAll);
}

VkResult
tu_ResetFences(VkDevice _device, uint32_t fenceCount, const VkFence *pFences)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   int ret;

   uint32_t handles[fenceCount];
   for (unsigned i = 0; i < fenceCount; ++i) {
      TU_FROM_HANDLE(tu_syncobj, fence, pFences[i]);
      sync_set_temporary(device, fence, 0);
      handles[i] = fence->permanent;
   }

   ret = ioctl(device->fd, DRM_IOCTL_SYNCOBJ_RESET, &(struct drm_syncobj_array) {
      .handles = (uint64_t) (uintptr_t) handles,
      .count_handles = fenceCount,
   });
   if (ret) {
      tu_device_set_lost(device, "DRM_IOCTL_SYNCOBJ_RESET failure: %s",
                         strerror(errno));
   }

   return VK_SUCCESS;
}

VkResult
tu_GetFenceStatus(VkDevice _device, VkFence _fence)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_syncobj, fence, _fence);
   VkResult result;

   result = drm_syncobj_wait(device, (uint32_t[]){fence->temporary ?: fence->permanent}, 1, 0, false);
   if (result == VK_TIMEOUT)
      result = VK_NOT_READY;
   return result;
}

int
tu_signal_fences(struct tu_device *device, struct tu_syncobj *fence1, struct tu_syncobj *fence2)
{
   uint32_t handles[2], count = 0;
   if (fence1)
      handles[count++] = fence1->temporary ?: fence1->permanent;

   if (fence2)
      handles[count++] = fence2->temporary ?: fence2->permanent;

   if (!count)
      return 0;

   return ioctl(device->fd, DRM_IOCTL_SYNCOBJ_SIGNAL, &(struct drm_syncobj_array) {
      .handles = (uintptr_t) handles,
      .count_handles = count
   });
}

int
tu_syncobj_to_fd(struct tu_device *device, struct tu_syncobj *sync)
{
   struct drm_syncobj_handle handle = { .handle = sync->permanent };
   int ret;

   ret = ioctl(device->fd, DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, &handle);

   return ret ? -1 : handle.fd;
}

#ifdef ANDROID
#include <libsync.h>

VkResult
tu_QueueSignalReleaseImageANDROID(VkQueue _queue,
                                  uint32_t waitSemaphoreCount,
                                  const VkSemaphore *pWaitSemaphores,
                                  VkImage image,
                                  int *pNativeFenceFd)
{
   TU_FROM_HANDLE(tu_queue, queue, _queue);
   VkResult result = VK_SUCCESS;

   if (waitSemaphoreCount == 0) {
      if (pNativeFenceFd)
         *pNativeFenceFd = -1;
      return VK_SUCCESS;
   }

   int fd = -1;

   for (uint32_t i = 0; i < waitSemaphoreCount; ++i) {
      int tmp_fd;
      result = tu_GetSemaphoreFdKHR(
         tu_device_to_handle(queue->device),
         &(VkSemaphoreGetFdInfoKHR) {
            .sType = VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR,
            .handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT,
            .semaphore = pWaitSemaphores[i],
         },
         &tmp_fd);
      if (result != VK_SUCCESS) {
         if (fd >= 0)
            close(fd);
         return result;
      }

      if (fd < 0)
         fd = tmp_fd;
      else if (tmp_fd >= 0) {
         sync_accumulate("tu", &fd, tmp_fd);
         close(tmp_fd);
      }
   }

   if (pNativeFenceFd) {
      *pNativeFenceFd = fd;
   } else if (fd >= 0) {
      close(fd);
      /* We still need to do the exports, to reset the semaphores, but
       * otherwise we don't wait on them. */
   }
   return VK_SUCCESS;
}
#endif
