/*
 * Copyright © 2018 Google, Inc.
 * Copyright © 2015 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "tu_knl.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <xf86drm.h>

#include "vk_util.h"

#include "drm-uapi/msm_drm.h"
#include "util/u_debug.h"
#include "util/hash_table.h"

#include "tu_cmd_buffer.h"
#include "tu_cs.h"
#include "tu_device.h"
#include "tu_dynamic_rendering.h"
#include "tu_knl_drm.h"
#include "tu_queue.h"
#include "tu_rmv.h"
#include "redump.h"

static int
tu_drm_get_param(int fd, uint32_t param, uint64_t *value)
{
   /* Technically this requires a pipe, but the kernel only supports one pipe
    * anyway at the time of writing and most of these are clearly pipe
    * independent. */
   struct drm_msm_param req = {
      .pipe = MSM_PIPE_3D0,
      .param = param,
   };

   int ret = drmCommandWriteRead(fd, DRM_MSM_GET_PARAM, &req, sizeof(req));
   if (ret)
      return ret;

   *value = req.value;

   return 0;
}

static int
tu_drm_get_gpu_id(const struct tu_physical_device *dev, uint32_t *id)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev->local_fd, MSM_PARAM_GPU_ID, &value);
   if (ret)
      return ret;

   *id = value;
   return 0;
}

static int
tu_drm_get_gmem_size(const struct tu_physical_device *dev, uint32_t *size)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev->local_fd, MSM_PARAM_GMEM_SIZE, &value);
   if (ret)
      return ret;

   *size = value;
   return 0;
}

static int
tu_drm_get_gmem_base(const struct tu_physical_device *dev, uint64_t *base)
{
   return tu_drm_get_param(dev->local_fd, MSM_PARAM_GMEM_BASE, base);
}

static bool
tu_drm_get_raytracing(const struct tu_physical_device *dev)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev->local_fd, MSM_PARAM_RAYTRACING, &value);
   if (ret)
      return false;

   return value;
}

static int
tu_drm_get_va_prop(const struct tu_physical_device *dev,
                   uint64_t *va_start, uint64_t *va_size)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev->local_fd, MSM_PARAM_VA_START, &value);
   if (ret)
      return ret;

   *va_start = value;

   ret = tu_drm_get_param(dev->local_fd, MSM_PARAM_VA_SIZE, &value);
   if (ret)
      return ret;

   *va_size = value;

   return 0;
}

static bool
tu_drm_has_preemption(const struct tu_physical_device *dev)
{
   struct drm_msm_submitqueue req = {
      .flags = MSM_SUBMITQUEUE_ALLOW_PREEMPT,
      .prio = dev->submitqueue_priority_count / 2,
   };

   int ret = drmCommandWriteRead(dev->local_fd,
                                 DRM_MSM_SUBMITQUEUE_NEW, &req, sizeof(req));
   if (ret)
      return false;

   drmCommandWrite(dev->local_fd, DRM_MSM_SUBMITQUEUE_CLOSE, &req.id,
                   sizeof(req.id));
   return true;
}

static uint32_t
tu_drm_get_priorities(const struct tu_physical_device *dev)
{
   uint64_t val = 1;
   tu_drm_get_param(dev->local_fd, MSM_PARAM_PRIORITIES, &val);
   assert(val >= 1);

   return val;
}

static uint32_t
tu_drm_get_highest_bank_bit(const struct tu_physical_device *dev)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev->local_fd, MSM_PARAM_HIGHEST_BANK_BIT, &value);
   if (ret)
      return 0;

   return value;
}

static enum fdl_macrotile_mode
tu_drm_get_macrotile_mode(const struct tu_physical_device *dev)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev->local_fd, MSM_PARAM_MACROTILE_MODE, &value);
   if (ret)
      return FDL_MACROTILE_INVALID;

   return (enum fdl_macrotile_mode) value;
}

static uint32_t
tu_drm_get_ubwc_swizzle(const struct tu_physical_device *dev)
{
   uint64_t value;
   int ret = tu_drm_get_param(dev->local_fd, MSM_PARAM_UBWC_SWIZZLE, &value);
   if (ret)
      return ~0;

   return value;
}

static bool
tu_drm_is_memory_type_supported(int fd, uint32_t flags)
{
   struct drm_msm_gem_new req_alloc = { .size = 0x1000, .flags = flags };

   int ret =
      drmCommandWriteRead(fd, DRM_MSM_GEM_NEW, &req_alloc, sizeof(req_alloc));
   if (ret) {
      return false;
   }

   struct drm_gem_close req_close = {
      .handle = req_alloc.handle,
   };
   drmIoctl(fd, DRM_IOCTL_GEM_CLOSE, &req_close);

   return true;
}

static VkResult
msm_device_init(struct tu_device *dev)
{
   int fd = open(dev->physical_device->fd_path, O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      return vk_startup_errorf(
            dev->physical_device->instance, VK_ERROR_INITIALIZATION_FAILED,
            "failed to open device %s", dev->physical_device->fd_path);
   }

   int ret = tu_drm_get_param(fd, MSM_PARAM_FAULTS, &dev->fault_count);
   if (ret != 0) {
      close(fd);
      return vk_startup_errorf(dev->physical_device->instance,
                               VK_ERROR_INITIALIZATION_FAILED,
                               "Failed to get initial fault count: %d", ret);
   }

   dev->fd = fd;

   return VK_SUCCESS;
}

static void
msm_device_finish(struct tu_device *dev)
{
   close(dev->fd);
}

static int
msm_device_get_gpu_timestamp(struct tu_device *dev, uint64_t *ts)
{
   return tu_drm_get_param(dev->fd, MSM_PARAM_TIMESTAMP, ts);
}

static int
msm_device_get_suspend_count(struct tu_device *dev, uint64_t *suspend_count)
{
   int ret = tu_drm_get_param(dev->fd, MSM_PARAM_SUSPENDS, suspend_count);
   return ret;
}

static VkResult
msm_device_check_status(struct tu_device *device)
{
   uint64_t last_fault_count = device->fault_count;
   int ret = tu_drm_get_param(device->fd, MSM_PARAM_FAULTS, &device->fault_count);
   if (ret != 0)
      return vk_device_set_lost(&device->vk, "error getting GPU fault count: %d", ret);

   if (last_fault_count != device->fault_count)
      return vk_device_set_lost(&device->vk, "GPU faulted or hung");

   return VK_SUCCESS;
}

static int
msm_submitqueue_new(struct tu_device *dev,
                    int priority,
                    uint32_t *queue_id)
{
   assert(priority >= 0 &&
          priority < dev->physical_device->submitqueue_priority_count);
   struct drm_msm_submitqueue req = {
      .flags = dev->physical_device->info->chip >= 7 &&
         dev->physical_device->has_preemption ?
         MSM_SUBMITQUEUE_ALLOW_PREEMPT : 0,
      .prio = priority,
   };

   int ret = drmCommandWriteRead(dev->fd,
                                 DRM_MSM_SUBMITQUEUE_NEW, &req, sizeof(req));
   if (ret)
      return ret;

   *queue_id = req.id;
   return 0;
}

static void
msm_submitqueue_close(struct tu_device *dev, uint32_t queue_id)
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
tu_wait_fence(struct tu_device *dev,
              uint32_t queue_id,
              int fence,
              uint64_t timeout_ns)
{
   /* fence was created when no work was yet submitted */
   if (fence < 0)
      return VK_SUCCESS;

   struct drm_msm_wait_fence req = {
      .fence = fence,
      .queueid = queue_id,
   };
   int ret;

   get_abs_timeout(&req.timeout, timeout_ns);

   ret = drmCommandWrite(dev->fd, DRM_MSM_WAIT_FENCE, &req, sizeof(req));
   if (ret) {
      if (ret == -ETIMEDOUT) {
         return VK_TIMEOUT;
      } else {
         mesa_loge("tu_wait_fence failed! %d (%s)", ret, strerror(errno));
         return VK_ERROR_UNKNOWN;
      }
   }

   return VK_SUCCESS;
}

VkResult
msm_queue_wait_fence(struct tu_queue *queue, uint32_t fence,
                     uint64_t timeout_ns)
{
   return tu_wait_fence(queue->device, queue->msm_queue_id, fence,
                        timeout_ns);
}

static VkResult
tu_free_zombie_vma_locked(struct tu_device *dev, bool wait)
{
   if (!u_vector_length(&dev->zombie_vmas))
      return VK_SUCCESS;

   if (wait) {
      struct tu_zombie_vma *vma = (struct tu_zombie_vma *)
            u_vector_head(&dev->zombie_vmas);
      /* Wait for 3s (arbitrary timeout) */
      VkResult ret = tu_wait_fence(dev, dev->queues[0]->msm_queue_id,
                                   vma->fence, 3000000000);

      if (ret != VK_SUCCESS)
         return ret;
   }

   int last_signaled_fence = -1;
   while (u_vector_length(&dev->zombie_vmas) > 0) {
      struct tu_zombie_vma *vma = (struct tu_zombie_vma *)
            u_vector_tail(&dev->zombie_vmas);
      if (vma->fence > last_signaled_fence) {
         VkResult ret =
            tu_wait_fence(dev, dev->queues[0]->msm_queue_id, vma->fence, 0);
         if (ret != VK_SUCCESS)
            return ret;

         last_signaled_fence = vma->fence;
      }

      if (vma->gem_handle) {
         /* Ensure that internal kernel's vma is freed. */
         struct drm_msm_gem_info req = {
            .handle = vma->gem_handle,
            .info = MSM_INFO_SET_IOVA,
            .value = 0,
         };

         int ret =
            drmCommandWriteRead(dev->fd, DRM_MSM_GEM_INFO, &req, sizeof(req));
         if (ret < 0) {
            mesa_loge("MSM_INFO_SET_IOVA(0) failed! %d (%s)", ret,
                      strerror(errno));
            return VK_ERROR_UNKNOWN;
         }

         tu_gem_close(dev, vma->gem_handle);

         util_vma_heap_free(&dev->vma, vma->iova, vma->size);
      }

      u_vector_remove(&dev->zombie_vmas);
   }

   return VK_SUCCESS;
}

static bool
tu_restore_from_zombie_vma_locked(struct tu_device *dev,
                                  uint32_t gem_handle,
                                  uint64_t *iova)
{
   struct tu_zombie_vma *vma;
   u_vector_foreach (vma, &dev->zombie_vmas) {
      if (vma->gem_handle == gem_handle) {
         *iova = vma->iova;

         /* mark to skip later gem and iova cleanup */
         vma->gem_handle = 0;
         return true;
      }
   }

   return false;
}

static VkResult
msm_allocate_userspace_iova_locked(struct tu_device *dev,
                                   uint32_t gem_handle,
                                   uint64_t size,
                                   uint64_t client_iova,
                                   enum tu_bo_alloc_flags flags,
                                   uint64_t *iova)
{
   VkResult result;

   *iova = 0;

   if ((flags & TU_BO_ALLOC_DMABUF) &&
       tu_restore_from_zombie_vma_locked(dev, gem_handle, iova))
      return VK_SUCCESS;

   tu_free_zombie_vma_locked(dev, false);

   result = tu_allocate_userspace_iova(dev, size, client_iova, flags, iova);
   if (result == VK_ERROR_INVALID_OPAQUE_CAPTURE_ADDRESS) {
      /* Address may be already freed by us, but not considered as
       * freed by the kernel. We have to wait until all work that
       * may hold the address is done. Since addresses are meant to
       * be replayed only by debug tooling, it should be ok to wait.
       */
      tu_free_zombie_vma_locked(dev, true);
      result = tu_allocate_userspace_iova(dev, size, client_iova, flags, iova);
   }

   if (result != VK_SUCCESS)
      return result;

   struct drm_msm_gem_info req = {
      .handle = gem_handle,
      .info = MSM_INFO_SET_IOVA,
      .value = *iova,
   };

   int ret =
      drmCommandWriteRead(dev->fd, DRM_MSM_GEM_INFO, &req, sizeof(req));
   if (ret < 0) {
      util_vma_heap_free(&dev->vma, *iova, size);
      mesa_loge("MSM_INFO_SET_IOVA failed! %d (%s)", ret, strerror(errno));
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   return VK_SUCCESS;
}

static VkResult
tu_allocate_kernel_iova(struct tu_device *dev,
                        uint32_t gem_handle,
                        uint64_t *iova)
{
   *iova = tu_gem_info(dev, gem_handle, MSM_INFO_GET_IOVA);
   if (!*iova)
      return VK_ERROR_OUT_OF_DEVICE_MEMORY;

   return VK_SUCCESS;
}

static VkResult
tu_bo_init(struct tu_device *dev,
           struct vk_object_base *base,
           struct tu_bo *bo,
           uint32_t gem_handle,
           uint64_t size,
           uint64_t client_iova,
           enum tu_bo_alloc_flags flags,
           const char *name)
{
   VkResult result = VK_SUCCESS;
   uint64_t iova = 0;

   assert(!client_iova || dev->physical_device->has_set_iova);

   if (dev->physical_device->has_set_iova) {
      result = msm_allocate_userspace_iova_locked(dev, gem_handle, size,
                                                  client_iova, flags, &iova);
   } else {
      result = tu_allocate_kernel_iova(dev, gem_handle, &iova);
   }

   if (result != VK_SUCCESS) {
      tu_gem_close(dev, gem_handle);
      return result;
   }

   name = tu_debug_bos_add(dev, size, name);

   mtx_lock(&dev->bo_mutex);
   uint32_t idx = dev->submit_bo_count++;

   /* grow the bo list if needed */
   if (idx >= dev->submit_bo_list_size) {
      uint32_t new_len = idx + 64;
      struct drm_msm_gem_submit_bo *new_ptr = (struct drm_msm_gem_submit_bo *)
         vk_realloc(&dev->vk.alloc, dev->submit_bo_list, new_len * sizeof(*dev->submit_bo_list),
                    8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
      if (!new_ptr) {
         dev->submit_bo_count--;
         mtx_unlock(&dev->bo_mutex);
         if (dev->physical_device->has_set_iova)
            util_vma_heap_free(&dev->vma, iova, size);
         tu_gem_close(dev, gem_handle);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      dev->submit_bo_list = new_ptr;
      dev->submit_bo_list_size = new_len;
   }

   bool dump = flags & TU_BO_ALLOC_ALLOW_DUMP;
   dev->submit_bo_list[idx] = (struct drm_msm_gem_submit_bo) {
      .flags = MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE |
               COND(dump, MSM_SUBMIT_BO_DUMP),
      .handle = gem_handle,
      .presumed = iova,
   };

   *bo = (struct tu_bo) {
      .gem_handle = gem_handle,
      .size = size,
      .iova = iova,
      .name = name,
      .refcnt = 1,
      .submit_bo_list_idx = idx,
      .base = base,
   };

   mtx_unlock(&dev->bo_mutex);

   tu_dump_bo_init(dev, bo);

   TU_RMV(bo_allocate, dev, bo);

   return VK_SUCCESS;
}

/**
 * Sets the name in the kernel so that the contents of /debug/dri/0/gem are more
 * useful.
 *
 * We skip this on release builds (when we're also not doing BO debugging) to
 * reduce overhead.
 */
static void
tu_bo_set_kernel_name(struct tu_device *dev, struct tu_bo *bo, const char *name)
{
   bool kernel_bo_names = dev->bo_sizes != NULL;
#if MESA_DEBUG
   kernel_bo_names = true;
#endif
   if (!kernel_bo_names)
      return;

   struct drm_msm_gem_info req = {
      .handle = bo->gem_handle,
      .info = MSM_INFO_SET_NAME,
      .value = (uintptr_t)(void *)name,
      .len = strlen(name),
   };

   int ret = drmCommandWrite(dev->fd, DRM_MSM_GEM_INFO, &req, sizeof(req));
   if (ret) {
      mesa_logw_once("Failed to set BO name with DRM_MSM_GEM_INFO: %d",
                     ret);
   }
}

static inline void
msm_vma_lock(struct tu_device *dev)
{
   if (dev->physical_device->has_set_iova)
      mtx_lock(&dev->vma_mutex);
}

static inline void
msm_vma_unlock(struct tu_device *dev)
{
   if (dev->physical_device->has_set_iova)
      mtx_unlock(&dev->vma_mutex);
}

static VkResult
msm_bo_init(struct tu_device *dev,
            struct vk_object_base *base,
            struct tu_bo **out_bo,
            uint64_t size,
            uint64_t client_iova,
            VkMemoryPropertyFlags mem_property,
            enum tu_bo_alloc_flags flags,
            const char *name)
{
   struct drm_msm_gem_new req = {
      .size = size,
      .flags = 0
   };

   if (mem_property & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
      if (mem_property & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
         req.flags |= MSM_BO_CACHED_COHERENT;
      } else {
         req.flags |= MSM_BO_CACHED;
      }
   } else {
      req.flags |= MSM_BO_WC;
   }

   if (flags & TU_BO_ALLOC_GPU_READ_ONLY)
      req.flags |= MSM_BO_GPU_READONLY;

   int ret = drmCommandWriteRead(dev->fd,
                                 DRM_MSM_GEM_NEW, &req, sizeof(req));
   if (ret)
      return vk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   struct tu_bo* bo = tu_device_lookup_bo(dev, req.handle);
   assert(bo && bo->gem_handle == 0);

   assert(!(flags & TU_BO_ALLOC_DMABUF));

   msm_vma_lock(dev);

   VkResult result =
      tu_bo_init(dev, base, bo, req.handle, size, client_iova, flags, name);

   msm_vma_unlock(dev);

   if (result == VK_SUCCESS) {
      *out_bo = bo;
      if (flags & TU_BO_ALLOC_INTERNAL_RESOURCE) {
         TU_RMV(internal_resource_create, dev, bo);
         TU_RMV(resource_name, dev, bo, name);
      }
   } else
      memset(bo, 0, sizeof(*bo));

   /* We don't use bo->name here because for the !TU_DEBUG=bo case bo->name is NULL. */
   tu_bo_set_kernel_name(dev, bo, name);

   if (result == VK_SUCCESS &&
       (mem_property & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) &&
       !(mem_property & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
      tu_bo_map(dev, bo, NULL);

      /* Cached non-coherent memory may already have dirty cache lines,
       * we should clean the cache lines before GPU got the chance to
       * write into this memory.
       *
       * MSM already does this automatically for uncached (MSM_BO_WC) memory.
       */
      tu_bo_sync_cache(dev, bo, 0, VK_WHOLE_SIZE, TU_MEM_SYNC_CACHE_TO_GPU);
   }

   return result;
}

static VkResult
msm_bo_init_dmabuf(struct tu_device *dev,
                   struct tu_bo **out_bo,
                   uint64_t size,
                   int prime_fd)
{
   /* lseek() to get the real size */
   off_t real_size = lseek(prime_fd, 0, SEEK_END);
   lseek(prime_fd, 0, SEEK_SET);
   if (real_size < 0 || (uint64_t) real_size < size)
      return vk_error(dev, VK_ERROR_INVALID_EXTERNAL_HANDLE);

   /* iova allocation needs to consider the object's *real* size: */
   size = real_size;

   /* Importing the same dmabuf several times would yield the same
    * gem_handle. Thus there could be a race when destroying
    * BO and importing the same dmabuf from different threads.
    * We must not permit the creation of dmabuf BO and its release
    * to happen in parallel.
    */
   u_rwlock_wrlock(&dev->dma_bo_lock);
   msm_vma_lock(dev);

   uint32_t gem_handle;
   int ret = drmPrimeFDToHandle(dev->fd, prime_fd,
                                &gem_handle);
   if (ret) {
      msm_vma_unlock(dev);
      u_rwlock_wrunlock(&dev->dma_bo_lock);
      return vk_error(dev, VK_ERROR_INVALID_EXTERNAL_HANDLE);
   }

   struct tu_bo* bo = tu_device_lookup_bo(dev, gem_handle);

   if (bo->refcnt != 0) {
      p_atomic_inc(&bo->refcnt);
      msm_vma_unlock(dev);
      u_rwlock_wrunlock(&dev->dma_bo_lock);

      *out_bo = bo;
      return VK_SUCCESS;
   }

   VkResult result =
      tu_bo_init(dev, NULL, bo, gem_handle, size, 0, TU_BO_ALLOC_DMABUF, "dmabuf");

   if (result != VK_SUCCESS)
      memset(bo, 0, sizeof(*bo));
   else
      *out_bo = bo;

   msm_vma_unlock(dev);
   u_rwlock_wrunlock(&dev->dma_bo_lock);

   return result;
}

static VkResult
msm_bo_map(struct tu_device *dev, struct tu_bo *bo, void *placed_addr)
{
   uint64_t offset = tu_gem_info(dev, bo->gem_handle, MSM_INFO_GET_OFFSET);
   if (!offset)
      return vk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);

   /* TODO: Should we use the wrapper os_mmap() like Freedreno does? */
   void *map = mmap(placed_addr, bo->size, PROT_READ | PROT_WRITE,
                    MAP_SHARED | (placed_addr != NULL ? MAP_FIXED : 0),
                    dev->fd, offset);
   if (map == MAP_FAILED)
      return vk_error(dev, VK_ERROR_MEMORY_MAP_FAILED);

   bo->map = map;
   TU_RMV(bo_map, dev, bo);

   return VK_SUCCESS;
}

static void
msm_bo_allow_dump(struct tu_device *dev, struct tu_bo *bo)
{
   mtx_lock(&dev->bo_mutex);
   dev->submit_bo_list[bo->submit_bo_list_idx].flags |= MSM_SUBMIT_BO_DUMP;
   mtx_unlock(&dev->bo_mutex);
}


static void
msm_bo_set_metadata(struct tu_device *dev, struct tu_bo *bo,
                    void *metadata, uint32_t metadata_size)
{
   struct drm_msm_gem_info req = {
      .handle = bo->gem_handle,
      .info = MSM_INFO_SET_METADATA,
      .value = (uintptr_t)(void *)metadata,
      .len = metadata_size,
   };

   int ret = drmCommandWrite(dev->fd, DRM_MSM_GEM_INFO, &req, sizeof(req));
   if (ret) {
      mesa_logw_once("Failed to set BO metadata with DRM_MSM_GEM_INFO: %d",
                     ret);
   }
}

static int
msm_bo_get_metadata(struct tu_device *dev, struct tu_bo *bo,
                    void *metadata, uint32_t metadata_size)
{
   struct drm_msm_gem_info req = {
      .handle = bo->gem_handle,
      .info = MSM_INFO_GET_METADATA,
      .value = (uintptr_t)(void *)metadata,
      .len = metadata_size,
   };

   int ret = drmCommandWrite(dev->fd, DRM_MSM_GEM_INFO, &req, sizeof(req));
   if (ret) {
      mesa_logw_once("Failed to get BO metadata with DRM_MSM_GEM_INFO: %d",
                     ret);
   }

   return ret;
}

static VkResult
msm_queue_submit(struct tu_queue *queue, void *_submit,
                 struct vk_sync_wait *waits, uint32_t wait_count,
                 struct vk_sync_signal *signals, uint32_t signal_count,
                 struct tu_u_trace_submission_data *u_trace_submission_data)
{
   VkResult result = VK_SUCCESS;
   int ret;
   struct tu_msm_queue_submit *submit =
      (struct tu_msm_queue_submit *)_submit;
   struct drm_msm_gem_submit_syncobj *in_syncobjs, *out_syncobjs;
   struct drm_msm_gem_submit req;
   uint64_t gpu_offset = 0;
   uint32_t entry_count =
      util_dynarray_num_elements(&submit->commands, struct drm_msm_gem_submit_cmd);
#if HAVE_PERFETTO
   struct tu_perfetto_clocks clocks;
   uint64_t start_ts = tu_perfetto_begin_submit();
#endif

   uint32_t flags = MSM_PIPE_3D0;

   /* Allocate without wait timeline semaphores */
   in_syncobjs = (struct drm_msm_gem_submit_syncobj *) vk_zalloc(
      &queue->device->vk.alloc,
      wait_count * sizeof(*in_syncobjs), 8,
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (in_syncobjs == NULL) {
      result = vk_error(queue, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_in_syncobjs;
   }

   /* Allocate with signal timeline semaphores considered */
   out_syncobjs = (struct drm_msm_gem_submit_syncobj *) vk_zalloc(
      &queue->device->vk.alloc,
      signal_count * sizeof(*out_syncobjs), 8,
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (out_syncobjs == NULL) {
      result = vk_error(queue, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_out_syncobjs;
   }

   for (uint32_t i = 0; i < wait_count; i++) {
      struct vk_sync *sync = waits[i].sync;

      in_syncobjs[i] = (struct drm_msm_gem_submit_syncobj) {
         .handle = tu_syncobj_from_vk_sync(sync),
         .flags = 0,
         .point = waits[i].wait_value,
      };
   }

   for (uint32_t i = 0; i < signal_count; i++) {
      struct vk_sync *sync = signals[i].sync;

      out_syncobjs[i] = (struct drm_msm_gem_submit_syncobj) {
         .handle = tu_syncobj_from_vk_sync(sync),
         .flags = 0,
         .point = signals[i].signal_value,
      };
   }

   if (wait_count)
      flags |= MSM_SUBMIT_SYNCOBJ_IN;

   if (signal_count)
      flags |= MSM_SUBMIT_SYNCOBJ_OUT;

   mtx_lock(&queue->device->bo_mutex);

   if (queue->device->implicit_sync_bo_count == 0)
      flags |= MSM_SUBMIT_NO_IMPLICIT;

   /* drm_msm_gem_submit_cmd requires index of bo which could change at any
    * time when bo_mutex is not locked. So we update the index here under the
    * lock.
    */
   util_dynarray_foreach (&submit->commands, struct drm_msm_gem_submit_cmd,
                          cmd) {
      unsigned i = cmd -
         util_dynarray_element(&submit->commands,
                               struct drm_msm_gem_submit_cmd, 0);
      struct tu_bo **bo = util_dynarray_element(&submit->command_bos,
                                                struct tu_bo *, i);
      cmd->submit_idx = (*bo)->submit_bo_list_idx;
   }

   req = (struct drm_msm_gem_submit) {
      .flags = flags,
      .nr_bos = entry_count ? queue->device->submit_bo_count : 0,
      .nr_cmds = entry_count,
      .bos = (uint64_t)(uintptr_t) queue->device->submit_bo_list,
      .cmds = (uint64_t)(uintptr_t)submit->commands.data,
      .queueid = queue->msm_queue_id,
      .in_syncobjs = (uint64_t)(uintptr_t)in_syncobjs,
      .out_syncobjs = (uint64_t)(uintptr_t)out_syncobjs,
      .nr_in_syncobjs = wait_count,
      .nr_out_syncobjs = signal_count,
      .syncobj_stride = sizeof(struct drm_msm_gem_submit_syncobj),
   };

   ret = drmCommandWriteRead(queue->device->fd,
                             DRM_MSM_GEM_SUBMIT,
                             &req, sizeof(req));

   mtx_unlock(&queue->device->bo_mutex);

   if (ret) {
      result = vk_device_set_lost(&queue->device->vk, "submit failed: %m");
      goto fail_submit;
   }

   p_atomic_set(&queue->fence, req.fence);

#if HAVE_PERFETTO
   clocks = tu_perfetto_end_submit(queue, queue->device->submit_count,
                                   start_ts, NULL);
   gpu_offset = clocks.gpu_ts_offset;
#endif

   if (u_trace_submission_data) {
      u_trace_submission_data->gpu_ts_offset = gpu_offset;
   }

   for (uint32_t i = 0; i < wait_count; i++) {
      if (!vk_sync_is_tu_timeline_sync(waits[i].sync))
         continue;

      struct tu_timeline_sync *sync =
         container_of(waits[i].sync, struct tu_timeline_sync, base);

      assert(sync->state != TU_TIMELINE_SYNC_STATE_RESET);

      /* Set SIGNALED to the state of the wait timeline sync since this means the syncobj
       * is done and ready again so this can be garbage-collectioned later.
       */
      sync->state = TU_TIMELINE_SYNC_STATE_SIGNALED;
   }

   for (uint32_t i = 0; i < signal_count; i++) {
      if (!vk_sync_is_tu_timeline_sync(signals[i].sync))
         continue;

      struct tu_timeline_sync *sync =
         container_of(signals[i].sync, struct tu_timeline_sync, base);

      assert(sync->state == TU_TIMELINE_SYNC_STATE_RESET);
      /* Set SUBMITTED to the state of the signal timeline sync so we could wait for
       * this timeline sync until completed if necessary.
       */
      sync->state = TU_TIMELINE_SYNC_STATE_SUBMITTED;
   }

fail_submit:
   vk_free(&queue->device->vk.alloc, out_syncobjs);
fail_out_syncobjs:
   vk_free(&queue->device->vk.alloc, in_syncobjs);
fail_in_syncobjs:
   return result;
}

static const struct tu_knl msm_knl_funcs = {
      .name = "msm",

      .device_init = msm_device_init,
      .device_finish = msm_device_finish,
      .device_get_gpu_timestamp = msm_device_get_gpu_timestamp,
      .device_get_suspend_count = msm_device_get_suspend_count,
      .device_check_status = msm_device_check_status,
      .submitqueue_new = msm_submitqueue_new,
      .submitqueue_close = msm_submitqueue_close,
      .bo_init = msm_bo_init,
      .bo_init_dmabuf = msm_bo_init_dmabuf,
      .bo_export_dmabuf = tu_drm_export_dmabuf,
      .bo_map = msm_bo_map,
      .bo_allow_dump = msm_bo_allow_dump,
      .bo_finish = tu_drm_bo_finish,
      .bo_set_metadata = msm_bo_set_metadata,
      .bo_get_metadata = msm_bo_get_metadata,
      .submit_create = msm_submit_create,
      .submit_finish = msm_submit_finish,
      .submit_add_entries = msm_submit_add_entries,
      .queue_submit = msm_queue_submit,
      .queue_wait_fence = msm_queue_wait_fence,
};

VkResult
tu_knl_drm_msm_load(struct tu_instance *instance,
                    int fd, struct _drmVersion *version,
                    struct tu_physical_device **out)
{
   VkResult result = VK_SUCCESS;

   /* Version 1.6 added SYNCOBJ support. */
   const int min_version_major = 1;
   const int min_version_minor = 6;

   if (version->version_major != min_version_major ||
       version->version_minor < min_version_minor) {
      result = vk_startup_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                                 "kernel driver for device %s has version %d.%d, "
                                 "but Vulkan requires version >= %d.%d",
                                 version->name,
                                 version->version_major, version->version_minor,
                                 min_version_major, min_version_minor);
      return result;
   }

   struct tu_physical_device *device = (struct tu_physical_device *)
      vk_zalloc(&instance->vk.alloc, sizeof(*device), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!device) {
      result = vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail;
   }

   device->msm_major_version = version->version_major;
   device->msm_minor_version = version->version_minor;

   device->instance = instance;
   device->local_fd = fd;

   if (tu_drm_get_gpu_id(device, &device->dev_id.gpu_id)) {
      result = vk_startup_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                                 "could not get GPU ID");
      goto fail;
   }

   if (tu_drm_get_param(fd, MSM_PARAM_CHIP_ID, &device->dev_id.chip_id)) {
      result = vk_startup_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                                 "could not get CHIP ID");
      goto fail;
   }

   if (tu_drm_get_gmem_size(device, &device->gmem_size)) {
      result = vk_startup_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                                "could not get GMEM size");
      goto fail;
   }
   device->gmem_size = debug_get_num_option("TU_GMEM", device->gmem_size);

   if (tu_drm_get_gmem_base(device, &device->gmem_base)) {
      result = vk_startup_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                                 "could not get GMEM size");
      goto fail;
   }

   device->has_set_iova = !tu_drm_get_va_prop(device, &device->va_start,
                                              &device->va_size);
   device->has_raytracing = tu_drm_get_raytracing(device);

   device->has_preemption = tu_drm_has_preemption(device);

   /* Even if kernel is new enough, the GPU itself may not support it. */
   device->has_cached_coherent_memory =
      (device->msm_minor_version >= 8) &&
      tu_drm_is_memory_type_supported(fd, MSM_BO_CACHED_COHERENT);

   device->submitqueue_priority_count = tu_drm_get_priorities(device);

   device->ubwc_config.highest_bank_bit = tu_drm_get_highest_bank_bit(device);
   device->ubwc_config.bank_swizzle_levels = tu_drm_get_ubwc_swizzle(device);
   device->ubwc_config.macrotile_mode = tu_drm_get_macrotile_mode(device);

   device->syncobj_type = vk_drm_syncobj_get_type(fd);
   /* we don't support DRM_CAP_SYNCOBJ_TIMELINE, but drm-shim does */
   if (!(device->syncobj_type.features & VK_SYNC_FEATURE_TIMELINE))
      device->timeline_type = vk_sync_timeline_get_type(&tu_timeline_sync_type);

   device->sync_types[0] = &device->syncobj_type;
   device->sync_types[1] = &device->timeline_type.sync;
   device->sync_types[2] = NULL;

   device->heap.size = tu_get_system_heap_size(device);
   device->heap.used = 0u;
   device->heap.flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

   instance->knl = &msm_knl_funcs;

   *out = device;

   return VK_SUCCESS;

fail:
   vk_free(&instance->vk.alloc, device);
   return result;
}
