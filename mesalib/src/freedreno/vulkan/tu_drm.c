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
#include "util/timespec.h"
#include "util/os_time.h"
#include "util/perf/u_trace.h"

#include "tu_private.h"

#include "tu_cs.h"

struct tu_binary_syncobj {
   uint32_t permanent, temporary;
};

struct tu_timeline_point {
   struct list_head link;

   uint64_t value;
   uint32_t syncobj;
   uint32_t wait_count;
};

struct tu_timeline {
   uint64_t highest_submitted;
   uint64_t highest_signaled;

   /* A timeline can have multiple timeline points */
   struct list_head points;

   /* A list containing points that has been already submited.
    * A point will be moved to 'points' when new point is required
    * at submit time.
    */
   struct list_head free_points;
};

typedef enum {
   TU_SEMAPHORE_BINARY,
   TU_SEMAPHORE_TIMELINE,
} tu_semaphore_type;


struct tu_syncobj {
   struct vk_object_base base;

   tu_semaphore_type type;
   union {
      struct tu_binary_syncobj binary;
      struct tu_timeline timeline;
   };
};

struct tu_queue_submit
{
   struct   list_head link;

   VkCommandBuffer *cmd_buffers;
   struct tu_u_trace_cmd_data *cmd_buffer_trace_data;
   uint32_t cmd_buffer_count;

   struct   tu_syncobj **wait_semaphores;
   uint32_t wait_semaphore_count;
   struct   tu_syncobj **signal_semaphores;
   uint32_t signal_semaphore_count;

   struct   tu_syncobj **wait_timelines;
   uint64_t *wait_timeline_values;
   uint32_t wait_timeline_count;
   uint32_t wait_timeline_array_length;

   struct   tu_syncobj **signal_timelines;
   uint64_t *signal_timeline_values;
   uint32_t signal_timeline_count;
   uint32_t signal_timeline_array_length;

   struct   drm_msm_gem_submit_cmd *cmds;
   struct   drm_msm_gem_submit_syncobj *in_syncobjs;
   uint32_t nr_in_syncobjs;
   struct   drm_msm_gem_submit_syncobj *out_syncobjs;
   uint32_t nr_out_syncobjs;

   bool     last_submit;
   uint32_t entry_count;
   uint32_t counter_pass_index;
};

struct tu_u_trace_syncobj
{
   uint32_t msm_queue_id;
   uint32_t fence;
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
tu_drm_get_timestamp(struct tu_physical_device *device, uint64_t *ts)
{
   return tu_drm_get_param(device, MSM_PARAM_TIMESTAMP, ts);
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
      if (!new_ptr)
         goto fail_bo_list;

      dev->bo_list = new_ptr;
      dev->bo_list_size = new_len;
   }

   /* grow the "bo idx" list (maps gem handles to index in the bo list) */
   if (bo->gem_handle >= dev->bo_idx_size) {
      uint32_t new_len = bo->gem_handle + 256;
      uint32_t *new_ptr =
         vk_realloc(&dev->vk.alloc, dev->bo_idx, new_len * sizeof(*dev->bo_idx),
                    8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
      if (!new_ptr)
         goto fail_bo_idx;

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

fail_bo_idx:
   vk_free(&dev->vk.alloc, dev->bo_list);
fail_bo_list:
   tu_gem_close(dev, gem_handle);
   return VK_ERROR_OUT_OF_HOST_MEMORY;
}

VkResult
tu_bo_init_new(struct tu_device *dev, struct tu_bo *bo, uint64_t size,
               enum tu_bo_alloc_flags flags)
{
   /* TODO: Choose better flags. As of 2018-11-12, freedreno/drm/msm_bo.c
    * always sets `flags = MSM_BO_WC`, and we copy that behavior here.
    */
   struct drm_msm_gem_new req = {
      .size = size,
      .flags = MSM_BO_WC
   };

   if (flags & TU_BO_ALLOC_GPU_READ_ONLY)
      req.flags |= MSM_BO_GPU_READONLY;

   int ret = drmCommandWriteRead(dev->fd,
                                 DRM_MSM_GEM_NEW, &req, sizeof(req));
   if (ret)
      return vk_error(dev->instance, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   return tu_bo_init(dev, bo, req.handle, size, flags & TU_BO_ALLOC_ALLOW_DUMP);
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

   device->instance = instance;

   if (instance->vk.enabled_extensions.KHR_display) {
      master_fd =
         open(drm_device->nodes[DRM_NODE_PRIMARY], O_RDWR | O_CLOEXEC);
      if (master_fd >= 0) {
         /* TODO: free master_fd is accel is not working? */
      }
   }

   device->master_fd = master_fd;
   device->local_fd = fd;

   if (tu_drm_get_gpu_id(device, &device->dev_id.gpu_id)) {
      result = vk_startup_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                                 "could not get GPU ID");
      goto fail;
   }

   if (tu_drm_get_param(device, MSM_PARAM_CHIP_ID, &device->dev_id.chip_id)) {
      result = vk_startup_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                                 "could not get CHIP ID");
      goto fail;
   }

   if (tu_drm_get_gmem_size(device, &device->gmem_size)) {
      result = vk_startup_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                                "could not get GMEM size");
      goto fail;
   }

   if (tu_drm_get_gmem_base(device, &device->gmem_base)) {
      result = vk_startup_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                                 "could not get GMEM size");
      goto fail;
   }

   device->heap.size = tu_get_system_heap_size();
   device->heap.used = 0u;
   device->heap.flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

   result = tu_physical_device_init(device, instance);
   if (result == VK_SUCCESS)
       return result;

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

static void
tu_timeline_finish(struct tu_device *device,
                    struct tu_timeline *timeline)
{
   list_for_each_entry_safe(struct tu_timeline_point, point,
                            &timeline->free_points, link) {
      list_del(&point->link);
      drmIoctl(device->fd, DRM_IOCTL_SYNCOBJ_DESTROY,
            &(struct drm_syncobj_destroy) { .handle = point->syncobj });

      vk_free(&device->vk.alloc, point);
   }
   list_for_each_entry_safe(struct tu_timeline_point, point,
                            &timeline->points, link) {
      list_del(&point->link);
      drmIoctl(device->fd, DRM_IOCTL_SYNCOBJ_DESTROY,
            &(struct drm_syncobj_destroy) { .handle = point->syncobj });
      vk_free(&device->vk.alloc, point);
   }
}

static VkResult
sync_create(VkDevice _device,
            bool signaled,
            bool fence,
            bool binary,
            uint64_t timeline_value,
            const VkAllocationCallbacks *pAllocator,
            void **p_sync)
{
   TU_FROM_HANDLE(tu_device, device, _device);

   struct tu_syncobj *sync =
         vk_object_alloc(&device->vk, pAllocator, sizeof(*sync),
                         fence ? VK_OBJECT_TYPE_FENCE : VK_OBJECT_TYPE_SEMAPHORE);
   if (!sync)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (binary) {
      struct drm_syncobj_create create = {};
      if (signaled)
         create.flags |= DRM_SYNCOBJ_CREATE_SIGNALED;

      int ret = drmIoctl(device->fd, DRM_IOCTL_SYNCOBJ_CREATE, &create);
      if (ret) {
         vk_free2(&device->vk.alloc, pAllocator, sync);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      sync->binary.permanent = create.handle;
      sync->binary.temporary = 0;
      sync->type = TU_SEMAPHORE_BINARY;
   } else {
      sync->type = TU_SEMAPHORE_TIMELINE;
      sync->timeline.highest_signaled = sync->timeline.highest_submitted =
             timeline_value;
      list_inithead(&sync->timeline.points);
      list_inithead(&sync->timeline.free_points);
   }

   *p_sync = sync;

   return VK_SUCCESS;
}

static void
sync_set_temporary(struct tu_device *device, struct tu_syncobj *sync, uint32_t syncobj)
{
   if (sync->binary.temporary) {
      drmIoctl(device->fd, DRM_IOCTL_SYNCOBJ_DESTROY,
            &(struct drm_syncobj_destroy) { .handle = sync->binary.temporary });
   }
   sync->binary.temporary = syncobj;
}

static void
sync_destroy(VkDevice _device, struct tu_syncobj *sync, const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);

   if (!sync)
      return;

   if (sync->type == TU_SEMAPHORE_BINARY) {
      sync_set_temporary(device, sync, 0);
      drmIoctl(device->fd, DRM_IOCTL_SYNCOBJ_DESTROY,
            &(struct drm_syncobj_destroy) { .handle = sync->binary.permanent });
   } else {
      tu_timeline_finish(device, &sync->timeline);
   }

   vk_object_free(&device->vk, pAllocator, sync);
}

static VkResult
sync_import(VkDevice _device, struct tu_syncobj *sync, bool temporary, bool sync_fd, int fd)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   int ret;

   if (!sync_fd) {
      uint32_t *dst = temporary ? &sync->binary.temporary : &sync->binary.permanent;

      struct drm_syncobj_handle handle = { .fd = fd };
      ret = drmIoctl(device->fd, DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE, &handle);
      if (ret)
         return VK_ERROR_INVALID_EXTERNAL_HANDLE;

      if (*dst) {
         drmIoctl(device->fd, DRM_IOCTL_SYNCOBJ_DESTROY,
               &(struct drm_syncobj_destroy) { .handle = *dst });
      }
      *dst = handle.handle;
      close(fd);
   } else {
      assert(temporary);

      struct drm_syncobj_create create = {};

      if (fd == -1)
         create.flags |= DRM_SYNCOBJ_CREATE_SIGNALED;

      ret = drmIoctl(device->fd, DRM_IOCTL_SYNCOBJ_CREATE, &create);
      if (ret)
         return VK_ERROR_INVALID_EXTERNAL_HANDLE;

      if (fd != -1) {
         ret = drmIoctl(device->fd, DRM_IOCTL_SYNCOBJ_FD_TO_HANDLE, &(struct drm_syncobj_handle) {
            .fd = fd,
            .handle = create.handle,
            .flags = DRM_SYNCOBJ_FD_TO_HANDLE_FLAGS_IMPORT_SYNC_FILE,
         });
         if (ret) {
            drmIoctl(device->fd, DRM_IOCTL_SYNCOBJ_DESTROY,
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
      .handle = sync->binary.temporary ?: sync->binary.permanent,
      .flags = COND(sync_fd, DRM_SYNCOBJ_HANDLE_TO_FD_FLAGS_EXPORT_SYNC_FILE),
      .fd = -1,
   };
   int ret = drmIoctl(device->fd, DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, &handle);
   if (ret)
      return vk_error(device->instance, VK_ERROR_INVALID_EXTERNAL_HANDLE);

   /* restore permanent payload on export */
   sync_set_temporary(device, sync, 0);

   *p_fd = handle.fd;
   return VK_SUCCESS;
}

static VkSemaphoreTypeKHR
get_semaphore_type(const void *pNext, uint64_t *initial_value)
{
   const VkSemaphoreTypeCreateInfoKHR *type_info =
      vk_find_struct_const(pNext, SEMAPHORE_TYPE_CREATE_INFO_KHR);

   if (!type_info)
      return VK_SEMAPHORE_TYPE_BINARY_KHR;

   if (initial_value)
      *initial_value = type_info->initialValue;
   return type_info->semaphoreType;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateSemaphore(VkDevice device,
                   const VkSemaphoreCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkSemaphore *pSemaphore)
{
   uint64_t timeline_value = 0;
   VkSemaphoreTypeKHR sem_type = get_semaphore_type(pCreateInfo->pNext, &timeline_value);

   return sync_create(device, false, false, (sem_type == VK_SEMAPHORE_TYPE_BINARY_KHR),
                      timeline_value, pAllocator, (void**) pSemaphore);
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroySemaphore(VkDevice device, VkSemaphore sem, const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_syncobj, sync, sem);
   sync_destroy(device, sync, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_ImportSemaphoreFdKHR(VkDevice device, const VkImportSemaphoreFdInfoKHR *info)
{
   TU_FROM_HANDLE(tu_syncobj, sync, info->semaphore);
   return sync_import(device, sync, info->flags & VK_SEMAPHORE_IMPORT_TEMPORARY_BIT,
         info->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT, info->fd);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetSemaphoreFdKHR(VkDevice device, const VkSemaphoreGetFdInfoKHR *info, int *pFd)
{
   TU_FROM_HANDLE(tu_syncobj, sync, info->semaphore);
   return sync_export(device, sync,
         info->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT, pFd);
}

VKAPI_ATTR void VKAPI_CALL
tu_GetPhysicalDeviceExternalSemaphoreProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalSemaphoreInfo *pExternalSemaphoreInfo,
   VkExternalSemaphoreProperties *pExternalSemaphoreProperties)
{
   VkSemaphoreTypeKHR type = get_semaphore_type(pExternalSemaphoreInfo->pNext, NULL);

   if (type != VK_SEMAPHORE_TYPE_TIMELINE &&
       (pExternalSemaphoreInfo->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_OPAQUE_FD_BIT ||
       pExternalSemaphoreInfo->handleType == VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT )) {
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

static VkResult
tu_queue_submit_add_timeline_wait_locked(struct tu_queue_submit* submit,
                                         struct tu_device *device,
                                         struct tu_syncobj *timeline,
                                         uint64_t value)
{
   if (submit->wait_timeline_count >= submit->wait_timeline_array_length) {
      uint32_t new_len = MAX2(submit->wait_timeline_array_length * 2, 64);

      submit->wait_timelines = vk_realloc(&device->vk.alloc,
            submit->wait_timelines,
            new_len * sizeof(*submit->wait_timelines),
            8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

      if (submit->wait_timelines == NULL)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      submit->wait_timeline_values = vk_realloc(&device->vk.alloc,
            submit->wait_timeline_values,
            new_len * sizeof(*submit->wait_timeline_values),
            8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

      if (submit->wait_timeline_values == NULL) {
         vk_free(&device->vk.alloc, submit->wait_timelines);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      submit->wait_timeline_array_length = new_len;
   }

   submit->wait_timelines[submit->wait_timeline_count] = timeline;
   submit->wait_timeline_values[submit->wait_timeline_count] = value;

   submit->wait_timeline_count++;

   return VK_SUCCESS;
}

static VkResult
tu_queue_submit_add_timeline_signal_locked(struct tu_queue_submit* submit,
                                           struct tu_device *device,
                                           struct tu_syncobj *timeline,
                                           uint64_t value)
{
   if (submit->signal_timeline_count >= submit->signal_timeline_array_length) {
      uint32_t new_len = MAX2(submit->signal_timeline_array_length * 2, 32);

      submit->signal_timelines = vk_realloc(&device->vk.alloc,
            submit->signal_timelines,
            new_len * sizeof(*submit->signal_timelines),
            8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

      if (submit->signal_timelines == NULL)
         return VK_ERROR_OUT_OF_HOST_MEMORY;

      submit->signal_timeline_values = vk_realloc(&device->vk.alloc,
            submit->signal_timeline_values,
            new_len * sizeof(*submit->signal_timeline_values),
            8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

      if (submit->signal_timeline_values == NULL) {
         vk_free(&device->vk.alloc, submit->signal_timelines);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      submit->signal_timeline_array_length = new_len;
   }

   submit->signal_timelines[submit->signal_timeline_count] = timeline;
   submit->signal_timeline_values[submit->signal_timeline_count] = value;

   submit->signal_timeline_count++;

   return VK_SUCCESS;
}

static VkResult
tu_queue_submit_create_locked(struct tu_queue *queue,
                              const VkSubmitInfo *submit_info,
                              const uint32_t nr_in_syncobjs,
                              const uint32_t nr_out_syncobjs,
                              const bool last_submit,
                              const VkPerformanceQuerySubmitInfoKHR *perf_info,
                              struct tu_queue_submit **submit)
{
   VkResult result;

   const VkTimelineSemaphoreSubmitInfoKHR *timeline_info =
         vk_find_struct_const(submit_info->pNext,
                              TIMELINE_SEMAPHORE_SUBMIT_INFO_KHR);

   const uint32_t wait_values_count =
         timeline_info ? timeline_info->waitSemaphoreValueCount : 0;
   const uint32_t signal_values_count =
         timeline_info ? timeline_info->signalSemaphoreValueCount : 0;

   const uint64_t *wait_values =
         wait_values_count ? timeline_info->pWaitSemaphoreValues : NULL;
   const uint64_t *signal_values =
         signal_values_count ?  timeline_info->pSignalSemaphoreValues : NULL;

   struct tu_queue_submit *new_submit = vk_zalloc(&queue->device->vk.alloc,
               sizeof(*new_submit), 8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   new_submit->cmd_buffer_count = submit_info->commandBufferCount;
   new_submit->cmd_buffers = vk_zalloc(&queue->device->vk.alloc,
         new_submit->cmd_buffer_count * sizeof(*new_submit->cmd_buffers), 8,
         VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (new_submit->cmd_buffers == NULL) {
      result = vk_error(queue->device->instance, VK_ERROR_OUT_OF_HOST_MEMORY)
      goto fail_cmd_buffers;
   }

   memcpy(new_submit->cmd_buffers, submit_info->pCommandBuffers,
          new_submit->cmd_buffer_count * sizeof(*new_submit->cmd_buffers));

   new_submit->wait_semaphores = vk_zalloc(&queue->device->vk.alloc,
         submit_info->waitSemaphoreCount * sizeof(*new_submit->wait_semaphores),
         8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (new_submit->wait_semaphores == NULL) {
      result = vk_error(queue->device->instance, VK_ERROR_OUT_OF_HOST_MEMORY)
      goto fail_wait_semaphores;
   }
   new_submit->wait_semaphore_count = submit_info->waitSemaphoreCount;

   new_submit->signal_semaphores = vk_zalloc(&queue->device->vk.alloc,
         submit_info->signalSemaphoreCount *sizeof(*new_submit->signal_semaphores),
         8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (new_submit->signal_semaphores == NULL) {
      result = vk_error(queue->device->instance, VK_ERROR_OUT_OF_HOST_MEMORY)
      goto fail_signal_semaphores;
   }
   new_submit->signal_semaphore_count = submit_info->signalSemaphoreCount;

   for (uint32_t i = 0; i < submit_info->waitSemaphoreCount; i++) {
      TU_FROM_HANDLE(tu_syncobj, sem, submit_info->pWaitSemaphores[i]);
      new_submit->wait_semaphores[i] = sem;

      if (sem->type == TU_SEMAPHORE_TIMELINE) {
         result = tu_queue_submit_add_timeline_wait_locked(new_submit,
               queue->device, sem, wait_values[i]);
         if (result != VK_SUCCESS)
            goto fail_wait_timelines;
      }
   }

   for (uint32_t i = 0; i < submit_info->signalSemaphoreCount; i++) {
      TU_FROM_HANDLE(tu_syncobj, sem, submit_info->pSignalSemaphores[i]);
      new_submit->signal_semaphores[i] = sem;

      if (sem->type == TU_SEMAPHORE_TIMELINE) {
         result = tu_queue_submit_add_timeline_signal_locked(new_submit,
               queue->device, sem, signal_values[i]);
         if (result != VK_SUCCESS)
            goto fail_signal_timelines;
      }
   }

   bool u_trace_enabled = u_trace_context_tracing(&queue->device->trace_context);
   bool has_trace_points = false;

   uint32_t entry_count = 0;
   for (uint32_t j = 0; j < new_submit->cmd_buffer_count; ++j) {
      TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, new_submit->cmd_buffers[j]);

      if (perf_info)
         entry_count++;

      entry_count += cmdbuf->cs.entry_count;

      if (u_trace_enabled && u_trace_has_points(&cmdbuf->trace)) {
         if (!(cmdbuf->usage_flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT))
            entry_count++;

         has_trace_points = true;
      }
   }

   new_submit->cmds = vk_zalloc(&queue->device->vk.alloc,
         entry_count * sizeof(*new_submit->cmds), 8,
         VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (new_submit->cmds == NULL) {
      result = vk_error(queue->device->instance, VK_ERROR_OUT_OF_HOST_MEMORY)
      goto fail_cmds;
   }

   if (has_trace_points) {
      new_submit->cmd_buffer_trace_data = vk_zalloc(&queue->device->vk.alloc,
            new_submit->cmd_buffer_count * sizeof(struct tu_u_trace_cmd_data), 8,
            VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

      if (new_submit->cmd_buffer_trace_data == NULL) {
         result = vk_error(queue->device->instance, VK_ERROR_OUT_OF_HOST_MEMORY)
         goto fail_cmd_trace_data;
      }

      for (uint32_t i = 0; i < new_submit->cmd_buffer_count; ++i) {
         TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, new_submit->cmd_buffers[i]);

         if (!(cmdbuf->usage_flags & VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT) &&
             u_trace_has_points(&cmdbuf->trace)) {
            /* A single command buffer could be submitted several times, but we
             * already backed timestamp iova addresses and trace points are
             * single-use. Therefor we have to copy trace points and create
             * a new timestamp buffer on every submit of reusable command buffer.
             */
            if (tu_create_copy_timestamp_cs(cmdbuf,
                  &new_submit->cmd_buffer_trace_data[i].timestamp_copy_cs,
                  &new_submit->cmd_buffer_trace_data[i].trace) != VK_SUCCESS) {
               result = vk_error(queue->device->instance, VK_ERROR_OUT_OF_HOST_MEMORY)
               goto fail_copy_timestamp_cs;
            }
            assert(new_submit->cmd_buffer_trace_data[i].timestamp_copy_cs->entry_count == 1);
         } else {
            new_submit->cmd_buffer_trace_data[i].trace = &cmdbuf->trace;
         }
      }
   }

   /* Allocate without wait timeline semaphores */
   new_submit->in_syncobjs = vk_zalloc(&queue->device->vk.alloc,
         (nr_in_syncobjs - new_submit->wait_timeline_count) *
         sizeof(*new_submit->in_syncobjs), 8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (new_submit->in_syncobjs == NULL) {
      result = vk_error(queue->device->instance, VK_ERROR_OUT_OF_HOST_MEMORY)
      goto fail_in_syncobjs;
   }

   /* Allocate with signal timeline semaphores considered */
   new_submit->out_syncobjs = vk_zalloc(&queue->device->vk.alloc,
         nr_out_syncobjs * sizeof(*new_submit->out_syncobjs), 8,
         VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (new_submit->out_syncobjs == NULL) {
      result = vk_error(queue->device->instance, VK_ERROR_OUT_OF_HOST_MEMORY)
      goto fail_out_syncobjs;
   }

   new_submit->entry_count = entry_count;
   new_submit->nr_in_syncobjs = nr_in_syncobjs;
   new_submit->nr_out_syncobjs = nr_out_syncobjs;
   new_submit->last_submit = last_submit;
   new_submit->counter_pass_index = perf_info ? perf_info->counterPassIndex : ~0;

   list_inithead(&new_submit->link);

   *submit = new_submit;

   return VK_SUCCESS;

fail_out_syncobjs:
   vk_free(&queue->device->vk.alloc, new_submit->in_syncobjs);
fail_in_syncobjs:
   if (new_submit->cmd_buffer_trace_data)
      tu_u_trace_cmd_data_finish(queue->device, new_submit->cmd_buffer_trace_data,
                                 new_submit->cmd_buffer_count);
fail_copy_timestamp_cs:
   vk_free(&queue->device->vk.alloc, new_submit->cmd_buffer_trace_data);
fail_cmd_trace_data:
   vk_free(&queue->device->vk.alloc, new_submit->cmds);
fail_cmds:
fail_signal_timelines:
fail_wait_timelines:
   vk_free(&queue->device->vk.alloc, new_submit->signal_semaphores);
fail_signal_semaphores:
   vk_free(&queue->device->vk.alloc, new_submit->wait_semaphores);
fail_wait_semaphores:
   vk_free(&queue->device->vk.alloc, new_submit->cmd_buffers);
fail_cmd_buffers:
   return result;
}

static void
tu_queue_submit_free(struct tu_queue *queue, struct tu_queue_submit *submit)
{
   vk_free(&queue->device->vk.alloc, submit->wait_semaphores);
   vk_free(&queue->device->vk.alloc, submit->signal_semaphores);

   vk_free(&queue->device->vk.alloc, submit->wait_timelines);
   vk_free(&queue->device->vk.alloc, submit->wait_timeline_values);
   vk_free(&queue->device->vk.alloc, submit->signal_timelines);
   vk_free(&queue->device->vk.alloc, submit->signal_timeline_values);

   vk_free(&queue->device->vk.alloc, submit->cmds);
   vk_free(&queue->device->vk.alloc, submit->in_syncobjs);
   vk_free(&queue->device->vk.alloc, submit->out_syncobjs);
   vk_free(&queue->device->vk.alloc, submit->cmd_buffers);
   vk_free(&queue->device->vk.alloc, submit);
}

static void
tu_queue_build_msm_gem_submit_cmds(struct tu_queue *queue,
                                   struct tu_queue_submit *submit)
{
   struct drm_msm_gem_submit_cmd *cmds = submit->cmds;

   uint32_t entry_idx = 0;
   for (uint32_t j = 0; j < submit->cmd_buffer_count; ++j) {
      TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, submit->cmd_buffers[j]);
      struct tu_cs *cs = &cmdbuf->cs;
      struct tu_device *dev = queue->device;

      if (submit->counter_pass_index != ~0) {
         struct tu_cs_entry *perf_cs_entry =
            &dev->perfcntrs_pass_cs_entries[submit->counter_pass_index];

         cmds[entry_idx].type = MSM_SUBMIT_CMD_BUF;
         cmds[entry_idx].submit_idx =
            dev->bo_idx[perf_cs_entry->bo->gem_handle];
         cmds[entry_idx].submit_offset = perf_cs_entry->offset;
         cmds[entry_idx].size = perf_cs_entry->size;
         cmds[entry_idx].pad = 0;
         cmds[entry_idx].nr_relocs = 0;
         cmds[entry_idx++].relocs = 0;
      }

      for (unsigned i = 0; i < cs->entry_count; ++i, ++entry_idx) {
         cmds[entry_idx].type = MSM_SUBMIT_CMD_BUF;
         cmds[entry_idx].submit_idx =
            dev->bo_idx[cs->entries[i].bo->gem_handle];
         cmds[entry_idx].submit_offset = cs->entries[i].offset;
         cmds[entry_idx].size = cs->entries[i].size;
         cmds[entry_idx].pad = 0;
         cmds[entry_idx].nr_relocs = 0;
         cmds[entry_idx].relocs = 0;
      }

      if (submit->cmd_buffer_trace_data) {
         struct tu_cs *ts_cs = submit->cmd_buffer_trace_data[j].timestamp_copy_cs;
         if (ts_cs) {
            cmds[entry_idx].type = MSM_SUBMIT_CMD_BUF;
            cmds[entry_idx].submit_idx =
               queue->device->bo_idx[ts_cs->entries[0].bo->gem_handle];

            assert(cmds[entry_idx].submit_idx < queue->device->bo_count);

            cmds[entry_idx].submit_offset = ts_cs->entries[0].offset;
            cmds[entry_idx].size = ts_cs->entries[0].size;
            cmds[entry_idx].pad = 0;
            cmds[entry_idx].nr_relocs = 0;
            cmds[entry_idx++].relocs = 0;
         }
      }
   }
}

static VkResult
tu_queue_submit_locked(struct tu_queue *queue, struct tu_queue_submit *submit)
{
   queue->device->submit_count++;

#if HAVE_PERFETTO
   tu_perfetto_submit(queue->device, queue->device->submit_count);
#endif

   uint32_t flags = MSM_PIPE_3D0;

   if (submit->nr_in_syncobjs)
      flags |= MSM_SUBMIT_SYNCOBJ_IN;

   if (submit->nr_out_syncobjs)
      flags |= MSM_SUBMIT_SYNCOBJ_OUT;

   if (submit->last_submit)
      flags |= MSM_SUBMIT_FENCE_FD_OUT;

   mtx_lock(&queue->device->bo_mutex);

   /* drm_msm_gem_submit_cmd requires index of bo which could change at any
    * time when bo_mutex is not locked. So we build submit cmds here the real
    * place to submit.
    */
   tu_queue_build_msm_gem_submit_cmds(queue, submit);

   struct drm_msm_gem_submit req = {
      .flags = flags,
      .queueid = queue->msm_queue_id,
      .bos = (uint64_t)(uintptr_t) queue->device->bo_list,
      .nr_bos = queue->device->bo_count,
      .cmds = (uint64_t)(uintptr_t)submit->cmds,
      .nr_cmds = submit->entry_count,
      .in_syncobjs = (uint64_t)(uintptr_t)submit->in_syncobjs,
      .out_syncobjs = (uint64_t)(uintptr_t)submit->out_syncobjs,
      .nr_in_syncobjs = submit->nr_in_syncobjs - submit->wait_timeline_count,
      .nr_out_syncobjs = submit->nr_out_syncobjs,
      .syncobj_stride = sizeof(struct drm_msm_gem_submit_syncobj),
   };

   int ret = drmCommandWriteRead(queue->device->fd,
                                 DRM_MSM_GEM_SUBMIT,
                                 &req, sizeof(req));

   mtx_unlock(&queue->device->bo_mutex);

   if (ret)
      return tu_device_set_lost(queue->device, "submit failed: %s\n",
                                strerror(errno));

   /* restore permanent payload on wait */
   for (uint32_t i = 0; i < submit->wait_semaphore_count; i++) {
      TU_FROM_HANDLE(tu_syncobj, sem, submit->wait_semaphores[i]);
      if(sem->type == TU_SEMAPHORE_BINARY)
         sync_set_temporary(queue->device, sem, 0);
   }

   if (submit->last_submit) {
      if (queue->fence >= 0)
         close(queue->fence);
      queue->fence = req.fence_fd;
   }

   /* Update highest_submitted values in the timeline. */
   for (uint32_t i = 0; i < submit->signal_timeline_count; i++) {
      struct tu_syncobj *sem = submit->signal_timelines[i];
      uint64_t signal_value = submit->signal_timeline_values[i];

      assert(signal_value > sem->timeline.highest_submitted);

      sem->timeline.highest_submitted = signal_value;
   }

   if (submit->cmd_buffer_trace_data) {
      struct tu_u_trace_flush_data *flush_data =
         vk_alloc(&queue->device->vk.alloc, sizeof(struct tu_u_trace_flush_data),
               8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
      flush_data->submission_id = queue->device->submit_count;
      flush_data->syncobj =
         vk_alloc(&queue->device->vk.alloc, sizeof(struct tu_u_trace_syncobj),
               8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
      flush_data->syncobj->fence = req.fence;
      flush_data->syncobj->msm_queue_id = queue->msm_queue_id;

      flush_data->cmd_trace_data = submit->cmd_buffer_trace_data;
      flush_data->trace_count = submit->cmd_buffer_count;
      submit->cmd_buffer_trace_data = NULL;

      for (uint32_t i = 0; i < submit->cmd_buffer_count; i++) {
         bool free_data = i == (submit->cmd_buffer_count - 1);
         u_trace_flush(flush_data->cmd_trace_data[i].trace, flush_data, free_data);
      }
   }

   pthread_cond_broadcast(&queue->device->timeline_cond);

   return VK_SUCCESS;
}


static bool
tu_queue_submit_ready_locked(struct tu_queue_submit *submit)
{
   for (uint32_t i = 0; i < submit->wait_timeline_count; i++) {
      if (submit->wait_timeline_values[i] >
            submit->wait_timelines[i]->timeline.highest_submitted) {
         return false;
      }
   }

   return true;
}

static VkResult
tu_timeline_add_point_locked(struct tu_device *device,
                             struct tu_timeline *timeline,
                             uint64_t value,
                             struct tu_timeline_point **point)
{

   if (list_is_empty(&timeline->free_points)) {
      *point = vk_zalloc(&device->vk.alloc, sizeof(**point), 8,
            VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

      if (!(*point))
         return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

      struct drm_syncobj_create create = {};

      int ret = drmIoctl(device->fd, DRM_IOCTL_SYNCOBJ_CREATE, &create);
      if (ret) {
         vk_free(&device->vk.alloc, *point);
         return vk_error(device->instance, VK_ERROR_DEVICE_LOST);
      }

      (*point)->syncobj = create.handle;

   } else {
      *point = list_first_entry(&timeline->free_points,
                                struct tu_timeline_point, link);
      list_del(&(*point)->link);
   }

   (*point)->value = value;
   list_addtail(&(*point)->link, &timeline->points);

   return VK_SUCCESS;
}

static VkResult
tu_queue_submit_timeline_locked(struct tu_queue *queue,
                                struct tu_queue_submit *submit)
{
   VkResult result;
   uint32_t timeline_idx =
         submit->nr_out_syncobjs - submit->signal_timeline_count;

   for (uint32_t i = 0; i < submit->signal_timeline_count; i++) {
      struct tu_timeline *timeline = &submit->signal_timelines[i]->timeline;
      uint64_t signal_value = submit->signal_timeline_values[i];
      struct tu_timeline_point *point;

      result = tu_timeline_add_point_locked(queue->device, timeline,
            signal_value, &point);
      if (result != VK_SUCCESS)
         return result;

      submit->out_syncobjs[timeline_idx + i] =
         (struct drm_msm_gem_submit_syncobj) {
            .handle = point->syncobj,
            .flags = 0,
         };
   }

   return tu_queue_submit_locked(queue, submit);
}

static VkResult
tu_queue_submit_deferred_locked(struct tu_queue *queue, uint32_t *advance)
{
   VkResult result = VK_SUCCESS;

   list_for_each_entry_safe(struct tu_queue_submit, submit,
                            &queue->queued_submits, link) {
      if (!tu_queue_submit_ready_locked(submit))
         break;

      (*advance)++;

      result = tu_queue_submit_timeline_locked(queue, submit);

      list_del(&submit->link);
      tu_queue_submit_free(queue, submit);

      if (result != VK_SUCCESS)
         break;
   }

   return result;
}

VkResult
tu_device_submit_deferred_locked(struct tu_device *dev)
{
    VkResult result = VK_SUCCESS;

    uint32_t advance = 0;
    do {
       advance = 0;
       for (uint32_t i = 0; i < dev->queue_count[0]; i++) {
          /* Try again if there's signaled submission. */
          result = tu_queue_submit_deferred_locked(&dev->queues[0][i],
                &advance);
          if (result != VK_SUCCESS)
             return result;
       }

    } while(advance);

    return result;
}

static inline void
get_abs_timeout(struct drm_msm_timespec *tv, uint64_t ns)
{
   struct timespec t;
   clock_gettime(CLOCK_MONOTONIC, &t);
   tv->tv_sec = t.tv_sec + ns / 1000000000;
   tv->tv_nsec = t.tv_nsec + ns % 1000000000;
}

VkResult
tu_device_wait_u_trace(struct tu_device *dev, struct tu_u_trace_syncobj *syncobj)
{
   struct drm_msm_wait_fence req = {
      .fence = syncobj->fence,
      .queueid = syncobj->msm_queue_id,
   };
   int ret;

   get_abs_timeout(&req.timeout, 1000000000);

   ret = drmCommandWrite(dev->fd, DRM_MSM_WAIT_FENCE, &req, sizeof(req));
   if (ret && (ret != -ETIMEDOUT)) {
      fprintf(stderr, "wait-fence failed! %d (%s)", ret, strerror(errno));
      return VK_TIMEOUT;
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
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

      const VkPerformanceQuerySubmitInfoKHR *perf_info =
         vk_find_struct_const(pSubmits[i].pNext,
                              PERFORMANCE_QUERY_SUBMIT_INFO_KHR);

      if (last_submit && fence)
         out_syncobjs_size += 1;

      pthread_mutex_lock(&queue->device->submit_mutex);
      struct tu_queue_submit *submit_req = NULL;

      VkResult ret = tu_queue_submit_create_locked(queue, submit,
            submit->waitSemaphoreCount, out_syncobjs_size,
            last_submit, perf_info, &submit_req);

      if (ret != VK_SUCCESS) {
         pthread_mutex_unlock(&queue->device->submit_mutex);
         return ret;
      }

      /* note: assuming there won't be any very large semaphore counts */
      struct drm_msm_gem_submit_syncobj *in_syncobjs = submit_req->in_syncobjs;
      struct drm_msm_gem_submit_syncobj *out_syncobjs = submit_req->out_syncobjs;
      uint32_t nr_in_syncobjs = 0, nr_out_syncobjs = 0;

      for (uint32_t i = 0; i < submit->waitSemaphoreCount; i++) {
         TU_FROM_HANDLE(tu_syncobj, sem, submit->pWaitSemaphores[i]);
         if (sem->type == TU_SEMAPHORE_TIMELINE)
            continue;

         in_syncobjs[nr_in_syncobjs++] = (struct drm_msm_gem_submit_syncobj) {
            .handle = sem->binary.temporary ?: sem->binary.permanent,
            .flags = MSM_SUBMIT_SYNCOBJ_RESET,
         };
      }

      for (uint32_t i = 0; i < submit->signalSemaphoreCount; i++) {
         TU_FROM_HANDLE(tu_syncobj, sem, submit->pSignalSemaphores[i]);

         /* In case of timeline semaphores, we can defer the creation of syncobj
          * and adding it at real submit time.
          */
         if (sem->type == TU_SEMAPHORE_TIMELINE)
            continue;

         out_syncobjs[nr_out_syncobjs++] = (struct drm_msm_gem_submit_syncobj) {
            .handle = sem->binary.temporary ?: sem->binary.permanent,
            .flags = 0,
         };
      }

      if (last_submit && fence) {
         out_syncobjs[nr_out_syncobjs++] = (struct drm_msm_gem_submit_syncobj) {
            .handle = fence->binary.temporary ?: fence->binary.permanent,
            .flags = 0,
         };
      }

      /* Queue the current submit */
      list_addtail(&submit_req->link, &queue->queued_submits);
      ret = tu_device_submit_deferred_locked(queue->device);

      pthread_mutex_unlock(&queue->device->submit_mutex);
      if (ret != VK_SUCCESS)
          return ret;
   }

   if (!submitCount && fence) {
      /* signal fence imemediately since we don't have a submit to do it */
      drmIoctl(queue->device->fd, DRM_IOCTL_SYNCOBJ_SIGNAL, &(struct drm_syncobj_array) {
         .handles = (uintptr_t) (uint32_t[]) { fence->binary.temporary ?: fence->binary.permanent },
         .count_handles = 1,
      });
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateFence(VkDevice device,
               const VkFenceCreateInfo *info,
               const VkAllocationCallbacks *pAllocator,
               VkFence *pFence)
{
   return sync_create(device, info->flags & VK_FENCE_CREATE_SIGNALED_BIT, true, true, 0,
                      pAllocator, (void**) pFence);
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyFence(VkDevice device, VkFence fence, const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_syncobj, sync, fence);
   sync_destroy(device, sync, pAllocator);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_ImportFenceFdKHR(VkDevice device, const VkImportFenceFdInfoKHR *info)
{
   TU_FROM_HANDLE(tu_syncobj, sync, info->fence);
   return sync_import(device, sync, info->flags & VK_FENCE_IMPORT_TEMPORARY_BIT,
         info->handleType == VK_EXTERNAL_FENCE_HANDLE_TYPE_SYNC_FD_BIT, info->fd);
}

VKAPI_ATTR VkResult VKAPI_CALL
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
   int ret = drmIoctl(device->fd, DRM_IOCTL_SYNCOBJ_WAIT, &(struct drm_syncobj_wait) {
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

VKAPI_ATTR VkResult VKAPI_CALL
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
      handles[i] = fence->binary.temporary ?: fence->binary.permanent;
   }

   return drm_syncobj_wait(device, handles, fenceCount, absolute_timeout(timeout), waitAll);
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_ResetFences(VkDevice _device, uint32_t fenceCount, const VkFence *pFences)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   int ret;

   uint32_t handles[fenceCount];
   for (unsigned i = 0; i < fenceCount; ++i) {
      TU_FROM_HANDLE(tu_syncobj, fence, pFences[i]);
      sync_set_temporary(device, fence, 0);
      handles[i] = fence->binary.permanent;
   }

   ret = drmIoctl(device->fd, DRM_IOCTL_SYNCOBJ_RESET, &(struct drm_syncobj_array) {
      .handles = (uint64_t) (uintptr_t) handles,
      .count_handles = fenceCount,
   });
   if (ret) {
      tu_device_set_lost(device, "DRM_IOCTL_SYNCOBJ_RESET failure: %s",
                         strerror(errno));
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetFenceStatus(VkDevice _device, VkFence _fence)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_syncobj, fence, _fence);
   VkResult result;

   result = drm_syncobj_wait(device, (uint32_t[]){fence->binary.temporary ?: fence->binary.permanent}, 1, 0, false);
   if (result == VK_TIMEOUT)
      result = VK_NOT_READY;
   return result;
}

int
tu_signal_fences(struct tu_device *device, struct tu_syncobj *fence1, struct tu_syncobj *fence2)
{
   uint32_t handles[2], count = 0;
   if (fence1)
      handles[count++] = fence1->binary.temporary ?: fence1->binary.permanent;

   if (fence2)
      handles[count++] = fence2->binary.temporary ?: fence2->binary.permanent;

   if (!count)
      return 0;

   return drmIoctl(device->fd, DRM_IOCTL_SYNCOBJ_SIGNAL, &(struct drm_syncobj_array) {
      .handles = (uintptr_t) handles,
      .count_handles = count
   });
}

int
tu_syncobj_to_fd(struct tu_device *device, struct tu_syncobj *sync)
{
   struct drm_syncobj_handle handle = { .handle = sync->binary.permanent };
   int ret;

   ret = drmIoctl(device->fd, DRM_IOCTL_SYNCOBJ_HANDLE_TO_FD, &handle);

   return ret ? -1 : handle.fd;
}

static VkResult
tu_timeline_gc_locked(struct tu_device *dev, struct tu_timeline *timeline)
{
   VkResult result = VK_SUCCESS;

   /* Go through every point in the timeline and check if any signaled point */
   list_for_each_entry_safe(struct tu_timeline_point, point,
                            &timeline->points, link) {

      /* If the value of the point is higher than highest_submitted,
       * the point has not been submited yet.
       */
      if (point->wait_count || point->value > timeline->highest_submitted)
         return VK_SUCCESS;

      result = drm_syncobj_wait(dev, (uint32_t[]){point->syncobj}, 1, 0, true);

      if (result == VK_TIMEOUT) {
         /* This means the syncobj is still busy and it should wait
          * with timeout specified by users via vkWaitSemaphores.
          */
         result = VK_SUCCESS;
      } else {
         timeline->highest_signaled =
               MAX2(timeline->highest_signaled, point->value);
         list_del(&point->link);
         list_add(&point->link, &timeline->free_points);
      }
   }

   return result;
}


static VkResult
tu_timeline_wait_locked(struct tu_device *device,
                        struct tu_timeline *timeline,
                        uint64_t value,
                        uint64_t abs_timeout)
{
   VkResult result;

   while(timeline->highest_submitted < value) {
      struct timespec abstime;
      timespec_from_nsec(&abstime, abs_timeout);

      pthread_cond_timedwait(&device->timeline_cond, &device->submit_mutex,
            &abstime);

      if (os_time_get_nano() >= abs_timeout &&
            timeline->highest_submitted < value)
         return VK_TIMEOUT;
   }

   /* Visit every point in the timeline and wait until
    * the highest_signaled reaches the value.
    */
   while (1) {
      result = tu_timeline_gc_locked(device, timeline);
      if (result != VK_SUCCESS)
         return result;

      if (timeline->highest_signaled >= value)
          return VK_SUCCESS;

      struct tu_timeline_point *point =
            list_first_entry(&timeline->points,
                             struct tu_timeline_point, link);

      point->wait_count++;
      pthread_mutex_unlock(&device->submit_mutex);
      result = drm_syncobj_wait(device, (uint32_t[]){point->syncobj}, 1,
                                abs_timeout, true);

      pthread_mutex_lock(&device->submit_mutex);
      point->wait_count--;

      if (result != VK_SUCCESS)
         return result;
   }

   return result;
}

static VkResult
tu_wait_timelines(struct tu_device *device,
                  const VkSemaphoreWaitInfoKHR* pWaitInfo,
                  uint64_t abs_timeout)
{
   if ((pWaitInfo->flags & VK_SEMAPHORE_WAIT_ANY_BIT_KHR) &&
         pWaitInfo->semaphoreCount > 1) {
      pthread_mutex_lock(&device->submit_mutex);

      /* Visit every timline semaphore in the queue until timeout */
      while (1) {
         for(uint32_t i = 0; i < pWaitInfo->semaphoreCount; ++i) {
            TU_FROM_HANDLE(tu_syncobj, semaphore, pWaitInfo->pSemaphores[i]);
            VkResult result = tu_timeline_wait_locked(device,
                  &semaphore->timeline, pWaitInfo->pValues[i], 0);

            /* Returns result values including VK_SUCCESS except for VK_TIMEOUT */
            if (result != VK_TIMEOUT) {
               pthread_mutex_unlock(&device->submit_mutex);
               return result;
            }
         }

         if (os_time_get_nano() > abs_timeout) {
            pthread_mutex_unlock(&device->submit_mutex);
            return VK_TIMEOUT;
         }
      }
   } else {
      VkResult result = VK_SUCCESS;

      pthread_mutex_lock(&device->submit_mutex);
      for(uint32_t i = 0; i < pWaitInfo->semaphoreCount; ++i) {
         TU_FROM_HANDLE(tu_syncobj, semaphore, pWaitInfo->pSemaphores[i]);
         assert(semaphore->type == TU_SEMAPHORE_TIMELINE);

         result = tu_timeline_wait_locked(device, &semaphore->timeline,
               pWaitInfo->pValues[i], abs_timeout);
         if (result != VK_SUCCESS)
            break;
      }
      pthread_mutex_unlock(&device->submit_mutex);

      return result;
   }
}


VKAPI_ATTR VkResult VKAPI_CALL
tu_GetSemaphoreCounterValue(VkDevice _device,
                            VkSemaphore _semaphore,
                            uint64_t* pValue)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_syncobj, semaphore, _semaphore);

   assert(semaphore->type == TU_SEMAPHORE_TIMELINE);

   VkResult result;

   pthread_mutex_lock(&device->submit_mutex);

   result = tu_timeline_gc_locked(device, &semaphore->timeline);
   *pValue = semaphore->timeline.highest_signaled;

   pthread_mutex_unlock(&device->submit_mutex);

   return result;
}


VKAPI_ATTR VkResult VKAPI_CALL
tu_WaitSemaphores(VkDevice _device,
                  const VkSemaphoreWaitInfoKHR* pWaitInfo,
                  uint64_t timeout)
{
   TU_FROM_HANDLE(tu_device, device, _device);

   return tu_wait_timelines(device, pWaitInfo, absolute_timeout(timeout));
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_SignalSemaphore(VkDevice _device,
                   const VkSemaphoreSignalInfoKHR* pSignalInfo)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_syncobj, semaphore, pSignalInfo->semaphore);
   VkResult result;

   assert(semaphore->type == TU_SEMAPHORE_TIMELINE);

   pthread_mutex_lock(&device->submit_mutex);

   result = tu_timeline_gc_locked(device, &semaphore->timeline);
   if (result != VK_SUCCESS) {
      pthread_mutex_unlock(&device->submit_mutex);
      return result;
   }

   semaphore->timeline.highest_submitted = pSignalInfo->value;
   semaphore->timeline.highest_signaled = pSignalInfo->value;

   result = tu_device_submit_deferred_locked(device);

   pthread_cond_broadcast(&device->timeline_cond);
   pthread_mutex_unlock(&device->submit_mutex);

   return result;
}

#ifdef ANDROID
#include <libsync.h>

VKAPI_ATTR VkResult VKAPI_CALL
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
