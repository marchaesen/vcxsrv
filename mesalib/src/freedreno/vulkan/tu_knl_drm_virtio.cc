/*
 * Copyright © 2018 Google, Inc.
 * Copyright © 2015 Intel Corporation
 * SPDX-License-Identifier: MIT
 *
 * Kernel interface layer for turnip running on virtio_gpu (aka virtgpu)
 */

#include "tu_knl.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <xf86drm.h>

#include "vk_util.h"

#include "drm-uapi/msm_drm.h"
#include "drm-uapi/virtgpu_drm.h"
#include "util/u_debug.h"
#include "util/hash_table.h"
#include "util/libsync.h"
#include "util/u_process.h"

#include "tu_cmd_buffer.h"
#include "tu_cs.h"
#include "tu_device.h"
#include "tu_dynamic_rendering.h"
#include "tu_knl_drm.h"
#include "tu_queue.h"

#include "virglrenderer_hw.h"
#include "msm_proto.h"

#include "vdrm.h"

struct tu_userspace_fence_cmd {
   uint32_t pkt[4];    /* first 4 dwords of packet */
   uint32_t fence;     /* fifth dword is fence value which is plugged in at runtime */
   uint32_t _pad[11];
};

struct tu_userspace_fence_cmds {
   struct tu_userspace_fence_cmd cmds[64];
};

struct tu_virtio_device {
   struct vdrm_device *vdrm;
   struct msm_shmem *shmem;
   uint32_t next_blob_id;

   struct tu_userspace_fence_cmds *fence_cmds;
   struct tu_bo *fence_cmds_mem;

   /**
    * Processing zombie VMAs is a two step process, first we clear the iova
    * and then we close the handles.  But to minimize waste of virtqueue
    * space (and associated stalling and ping-ponging between guest and host)
    * we want to batch up all the GEM_SET_IOVA ccmds before we flush them to
    * the host and start closing handles.
    *
    * This gives us a place to stash the VMAs between the two steps.
    */
   struct u_vector zombie_vmas_stage_2;
};

static int tu_drm_get_param(struct vdrm_device *vdrm, uint32_t param, uint64_t *value);

/**
 * Helper for simple pass-thru ioctls
 */
static int
virtio_simple_ioctl(struct vdrm_device *vdrm, unsigned cmd, void *_req)
{
   MESA_TRACE_FUNC();
   unsigned req_len = sizeof(struct msm_ccmd_ioctl_simple_req);
   unsigned rsp_len = sizeof(struct msm_ccmd_ioctl_simple_rsp);

   req_len += _IOC_SIZE(cmd);
   if (cmd & IOC_OUT)
      rsp_len += _IOC_SIZE(cmd);

   uint8_t buf[req_len];
   struct msm_ccmd_ioctl_simple_req *req = (struct msm_ccmd_ioctl_simple_req *)buf;
   struct msm_ccmd_ioctl_simple_rsp *rsp;

   req->hdr = MSM_CCMD(IOCTL_SIMPLE, req_len);
   req->cmd = cmd;
   memcpy(req->payload, _req, _IOC_SIZE(cmd));

   rsp = (struct msm_ccmd_ioctl_simple_rsp *)
         vdrm_alloc_rsp(vdrm, &req->hdr, rsp_len);

   int ret = vdrm_send_req(vdrm, &req->hdr, true);

   if (cmd & IOC_OUT)
      memcpy(_req, rsp->payload, _IOC_SIZE(cmd));

   ret = rsp->ret;

   return ret;
}

static int
set_iova(struct tu_device *device, uint32_t res_id, uint64_t iova)
{
   struct msm_ccmd_gem_set_iova_req req = {
         .hdr = MSM_CCMD(GEM_SET_IOVA, sizeof(req)),
         .iova = iova,
         .res_id = res_id,
   };

   return vdrm_send_req(device->vdev->vdrm, &req.hdr, false);
}

static int
query_faults(struct tu_device *dev, uint64_t *value)
{
   struct tu_virtio_device *vdev = dev->vdev;
   uint32_t async_error = 0;
   uint64_t global_faults;

   if (vdrm_shmem_has_field(vdev->shmem, async_error))
      async_error = vdev->shmem->async_error;

   if (vdrm_shmem_has_field(vdev->shmem, global_faults)) {
      global_faults = vdev->shmem->global_faults;
   } else {
      int ret = tu_drm_get_param(vdev->vdrm, MSM_PARAM_FAULTS, &global_faults);
      if (ret)
         return ret;
   }

   *value = global_faults + async_error;

   return 0;
}

static void
set_debuginfo(struct tu_device *dev)
{
   const char *comm = util_get_process_name();
   static char cmdline[0x1000+1];
   int fd = open("/proc/self/cmdline", O_RDONLY);
   if (fd < 0)
      return;

   int n = read(fd, cmdline, sizeof(cmdline) - 1);
   if (n < 0)
      return;

   /* arguments are separated by NULL, convert to spaces: */
   for (int i = 0; i < n; i++) {
      if (cmdline[i] == '\0') {
         cmdline[i] = ' ';
      }
   }

   cmdline[n] = '\0';

   unsigned comm_len = strlen(comm) + 1;
   unsigned cmdline_len = strlen(cmdline) + 1;

   struct msm_ccmd_set_debuginfo_req *req;

   unsigned req_len = align(sizeof(*req) + comm_len + cmdline_len, 4);

   req = (struct msm_ccmd_set_debuginfo_req *)malloc(req_len);

   req->hdr         = MSM_CCMD(SET_DEBUGINFO, req_len);
   req->comm_len    = comm_len;
   req->cmdline_len = cmdline_len;

   memcpy(&req->payload[0], comm, comm_len);
   memcpy(&req->payload[comm_len], cmdline, cmdline_len);

   vdrm_send_req(dev->vdev->vdrm, &req->hdr, false);

   free(req);
}

static VkResult
virtio_device_init(struct tu_device *dev)
{
   struct tu_instance *instance = dev->physical_device->instance;
   int fd;

   fd = open(dev->physical_device->fd_path, O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      return vk_startup_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                               "failed to open device %s", dev->physical_device->fd_path);
   }

   struct tu_virtio_device *vdev = (struct tu_virtio_device *)
            vk_zalloc(&instance->vk.alloc, sizeof(*vdev), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!vdev) {
      close(fd);
      return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   };

   u_vector_init(&vdev->zombie_vmas_stage_2, 64, sizeof(struct tu_zombie_vma));

   dev->vdev = vdev;
   dev->fd = fd;

   vdev->vdrm = vdrm_device_connect(fd, VIRTGPU_DRM_CONTEXT_MSM);

   p_atomic_set(&vdev->next_blob_id, 1);
   vdev->shmem = to_msm_shmem(vdev->vdrm->shmem);

   query_faults(dev, &dev->fault_count);

   set_debuginfo(dev);

   return VK_SUCCESS;
}

static void
virtio_device_finish(struct tu_device *dev)
{
   struct tu_instance *instance = dev->physical_device->instance;
   struct tu_virtio_device *vdev = dev->vdev;

   u_vector_finish(&vdev->zombie_vmas_stage_2);

   vdrm_device_close(vdev->vdrm);

   vk_free(&instance->vk.alloc, vdev);
   dev->vdev = NULL;

   close(dev->fd);
}

static int
tu_drm_get_param(struct vdrm_device *vdrm, uint32_t param, uint64_t *value)
{
   /* Technically this requires a pipe, but the kernel only supports one pipe
    * anyway at the time of writing and most of these are clearly pipe
    * independent. */
   struct drm_msm_param req = {
      .pipe = MSM_PIPE_3D0,
      .param = param,
   };

   int ret = virtio_simple_ioctl(vdrm, DRM_IOCTL_MSM_GET_PARAM, &req);
   if (ret)
      return ret;

   *value = req.value;

   return 0;
}

static uint32_t
tu_drm_get_highest_bank_bit(struct vdrm_device *vdrm)
{
   uint64_t value;
   int ret = tu_drm_get_param(vdrm, MSM_PARAM_HIGHEST_BANK_BIT, &value);
   if (ret)
      return 0;

   return value;
}

static enum fdl_macrotile_mode
tu_drm_get_macrotile_mode(struct vdrm_device *vdrm)
{
   if (vdrm->caps.u.msm.macrotile_mode)
      return (enum fdl_macrotile_mode)vdrm->caps.u.msm.macrotile_mode;
   uint64_t value;
   int ret = tu_drm_get_param(vdrm, MSM_PARAM_MACROTILE_MODE, &value);
   if (ret)
      return FDL_MACROTILE_INVALID;

   return (enum fdl_macrotile_mode) value;
}

static uint32_t
tu_drm_get_ubwc_swizzle(struct vdrm_device *vdrm)
{
   if (vdrm->caps.u.msm.ubwc_swizzle)
      return (uint32_t)vdrm->caps.u.msm.ubwc_swizzle;
   uint64_t value;
   int ret = tu_drm_get_param(vdrm, MSM_PARAM_UBWC_SWIZZLE, &value);
   if (ret)
      return ~0;

   return value;
}

static int
virtio_device_get_gpu_timestamp(struct tu_device *dev, uint64_t *ts)
{
   return tu_drm_get_param(dev->vdev->vdrm, MSM_PARAM_TIMESTAMP, ts);
}

static int
virtio_device_get_suspend_count(struct tu_device *dev, uint64_t *suspend_count)
{
   int ret = tu_drm_get_param(dev->vdev->vdrm, MSM_PARAM_SUSPENDS, suspend_count);
   return ret;
}

static bool
opt_cap_bool(uint32_t val)
{
   if (val == VIRTGPU_CAP_BOOL_TRUE)  return true;
   if (val == VIRTGPU_CAP_BOOL_FALSE) return false;
   unreachable("invalid val");
}

static bool
tu_drm_get_raytracing(struct vdrm_device *vdrm)
{
   if (vdrm->caps.u.msm.has_raytracing)
      return opt_cap_bool(vdrm->caps.u.msm.has_raytracing);
   uint64_t value;
   int ret = tu_drm_get_param(vdrm, MSM_PARAM_RAYTRACING, &value);
   if (ret)
      return false;

   return value;
}


static VkResult
virtio_device_check_status(struct tu_device *device)
{
   uint64_t last_fault_count = device->fault_count;

   query_faults(device, &device->fault_count);

   if (last_fault_count != device->fault_count)
      return vk_device_set_lost(&device->vk, "GPU faulted or hung");

   return VK_SUCCESS;
}

static int
virtio_submitqueue_new(struct tu_device *dev,
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

   int ret = virtio_simple_ioctl(dev->vdev->vdrm, DRM_IOCTL_MSM_SUBMITQUEUE_NEW, &req);
   if (ret)
      return ret;

   *queue_id = req.id;
   return 0;
}

static void
virtio_submitqueue_close(struct tu_device *dev, uint32_t queue_id)
{
   virtio_simple_ioctl(dev->vdev->vdrm, DRM_IOCTL_MSM_SUBMITQUEUE_CLOSE, &queue_id);
}

static bool
virtio_has_preemption(struct vdrm_device *vdrm)
{
   if (vdrm->caps.u.msm.has_preemption)
      return opt_cap_bool(vdrm->caps.u.msm.has_preemption);
   struct drm_msm_submitqueue req = {
      .flags = MSM_SUBMITQUEUE_ALLOW_PREEMPT,
      .prio = vdrm->caps.u.msm.priorities / 2,
   };

   int ret = virtio_simple_ioctl(vdrm, DRM_IOCTL_MSM_SUBMITQUEUE_NEW, &req);
   if (ret)
      return false;

   virtio_simple_ioctl(vdrm, DRM_IOCTL_MSM_SUBMITQUEUE_CLOSE, &req.id);
   return true;
}

static VkResult
tu_wait_fence(struct tu_device *dev,
              uint32_t queue_id,
              int fence,
              uint64_t timeout_ns)
{
   struct vdrm_device *vdrm = dev->vdev->vdrm;

   if (!fence_before(dev->global_bo_map->userspace_fence, fence))
      return VK_SUCCESS;

   if (!timeout_ns)
      return VK_TIMEOUT;

   MESA_TRACE_FUNC();

   struct msm_ccmd_wait_fence_req req = {
         .hdr = MSM_CCMD(WAIT_FENCE, sizeof(req)),
         .queue_id = queue_id,
         .fence = fence,
   };
   struct msm_ccmd_submitqueue_query_rsp *rsp;
   int64_t end_time = os_time_get_nano() + timeout_ns;
   int ret;

   do {
      rsp = (struct msm_ccmd_submitqueue_query_rsp *)
            vdrm_alloc_rsp(vdrm, &req.hdr, sizeof(*rsp));

      ret = vdrm_send_req(vdrm, &req.hdr, true);
      if (ret)
         goto out;

      if (os_time_get_nano() >= end_time)
         break;

      ret = rsp->ret;
   } while (ret == -ETIMEDOUT);

out:
   if (!ret) return VK_SUCCESS;
   if (ret == -ETIMEDOUT) return VK_TIMEOUT;
   return VK_ERROR_UNKNOWN;
}

VkResult
virtio_queue_wait_fence(struct tu_queue *queue, uint32_t fence,
                        uint64_t timeout_ns)
{
   return tu_wait_fence(queue->device, queue->msm_queue_id, fence,
                        timeout_ns);
}

static VkResult
tu_free_zombie_vma_locked(struct tu_device *dev, bool wait)
{
   struct tu_virtio_device *vdev = dev->vdev;

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

   /* Clear the iova of all finished objects in first pass so the SET_IOVA
    * ccmd's can be buffered and sent together to the host.  *Then* delete
    * the handles.  This avoids filling up the virtqueue with tiny messages,
    * since each execbuf ends up needing to be page aligned.
    */
   int last_signaled_fence = -1;
   while (u_vector_length(&dev->zombie_vmas) > 0) {
      struct tu_zombie_vma *vma = (struct tu_zombie_vma *)
            u_vector_tail(&dev->zombie_vmas);
      if (vma->fence > last_signaled_fence) {
         VkResult ret =
            tu_wait_fence(dev, dev->queues[0]->msm_queue_id, vma->fence, 0);
         if (ret != VK_SUCCESS)
            break;

         last_signaled_fence = vma->fence;
      }

      u_vector_remove(&dev->zombie_vmas);

      if (vma->gem_handle) {
         set_iova(dev, vma->res_id, 0);

         struct tu_zombie_vma *vma2 =
            (struct tu_zombie_vma *) u_vector_add(&vdev->zombie_vmas_stage_2);

         *vma2 = *vma;
      }
   }

   /* And _then_ close the GEM handles: */
   while (u_vector_length(&vdev->zombie_vmas_stage_2) > 0) {
      struct tu_zombie_vma *vma = (struct tu_zombie_vma *)
            u_vector_remove(&vdev->zombie_vmas_stage_2);

      util_vma_heap_free(&dev->vma, vma->iova, vma->size);
      vdrm_bo_close(dev->vdev->vdrm, vma->gem_handle);
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

         /* mark to skip later vdrm bo and iova cleanup */
         vma->gem_handle = 0;
         return true;
      }
   }

   return false;
}

static VkResult
virtio_allocate_userspace_iova_locked(struct tu_device *dev,
                                      uint32_t gem_handle,
                                      uint64_t size,
                                      uint64_t client_iova,
                                      enum tu_bo_alloc_flags flags,
                                      uint64_t *iova)
{
   VkResult result;

   *iova = 0;

   if (flags & TU_BO_ALLOC_DMABUF) {
      assert(gem_handle);

      if (tu_restore_from_zombie_vma_locked(dev, gem_handle, iova))
         return VK_SUCCESS;
   }

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

   return result;
}

static VkResult
tu_bo_init(struct tu_device *dev,
           struct vk_object_base *base,
           struct tu_bo *bo,
           uint32_t gem_handle,
           uint64_t size,
           uint64_t iova,
           enum tu_bo_alloc_flags flags,
           const char *name)
{
   assert(dev->physical_device->has_set_iova);

   set_iova(dev, bo->res_id, iova);

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
         vdrm_bo_close(dev->vdev->vdrm, bo->gem_handle);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }

      dev->submit_bo_list = new_ptr;
      dev->submit_bo_list_size = new_len;
   }

   bool dump = flags & TU_BO_ALLOC_ALLOW_DUMP;
   dev->submit_bo_list[idx] = (struct drm_msm_gem_submit_bo) {
      .flags = MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE |
               COND(dump, MSM_SUBMIT_BO_DUMP),
      .handle = bo->res_id,
      .presumed = iova,
   };

   *bo = (struct tu_bo) {
      .gem_handle = gem_handle,
      .res_id = bo->res_id,
      .size = size,
      .iova = iova,
      .name = name,
      .refcnt = 1,
      .submit_bo_list_idx = idx,
      .base = base,
   };

   mtx_unlock(&dev->bo_mutex);

   tu_dump_bo_init(dev, bo);

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

   size_t sz = strlen(name);

   unsigned req_len = sizeof(struct msm_ccmd_gem_set_name_req) + align(sz, 4);

   uint8_t buf[req_len];
   struct msm_ccmd_gem_set_name_req *req = (struct msm_ccmd_gem_set_name_req *)buf;

   req->hdr = MSM_CCMD(GEM_SET_NAME, req_len);
   req->res_id = bo->res_id;
   req->len = sz;

   memcpy(req->payload, name, sz);

   vdrm_send_req(dev->vdev->vdrm, &req->hdr, false);
}

static VkResult
virtio_bo_init(struct tu_device *dev,
               struct vk_object_base *base,
               struct tu_bo **out_bo,
               uint64_t size,
               uint64_t client_iova,
               VkMemoryPropertyFlags mem_property,
               enum tu_bo_alloc_flags flags,
               const char *name)
{
   struct tu_virtio_device *vdev = dev->vdev;
   struct msm_ccmd_gem_new_req req = {
         .hdr = MSM_CCMD(GEM_NEW, sizeof(req)),
         .size = size,
   };
   VkResult result;
   uint32_t res_id;
   struct tu_bo *bo;

   if (mem_property & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) {
      if (mem_property & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) {
         req.flags |= MSM_BO_CACHED_COHERENT;
      } else {
         req.flags |= MSM_BO_CACHED;
      }
   } else {
      req.flags |= MSM_BO_WC;
   }

   uint32_t blob_flags = 0;
   if (mem_property & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) {
      blob_flags |= VIRTGPU_BLOB_FLAG_USE_MAPPABLE;
   }

   if (!(mem_property & VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT)) {
      blob_flags |= VIRTGPU_BLOB_FLAG_USE_SHAREABLE;
      if (vdev->vdrm->supports_cross_device)
         blob_flags |= VIRTGPU_BLOB_FLAG_USE_CROSS_DEVICE;
   }

   if (flags & TU_BO_ALLOC_GPU_READ_ONLY)
      req.flags |= MSM_BO_GPU_READONLY;

   assert(!(flags & TU_BO_ALLOC_DMABUF));

   mtx_lock(&dev->vma_mutex);
   result = virtio_allocate_userspace_iova_locked(dev, 0, size, client_iova,
                                                  flags, &req.iova);
   mtx_unlock(&dev->vma_mutex);

   if (result != VK_SUCCESS)
      return result;

   /* tunneled cmds are processed separately on host side,
    * before the renderer->get_blob() callback.. the blob_id
    * is used to link the created bo to the get_blob() call
    */
   req.blob_id = p_atomic_inc_return(&vdev->next_blob_id);;

   uint32_t handle =
      vdrm_bo_create(vdev->vdrm, size, blob_flags, req.blob_id, &req.hdr);

   if (!handle) {
      result = VK_ERROR_OUT_OF_DEVICE_MEMORY;
      goto fail;
   }

   res_id = vdrm_handle_to_res_id(vdev->vdrm, handle);
   bo = tu_device_lookup_bo(dev, res_id);
   assert(bo && bo->gem_handle == 0);

   bo->res_id = res_id;

   result = tu_bo_init(dev, base, bo, handle, size, req.iova, flags, name);
   if (result != VK_SUCCESS) {
      memset(bo, 0, sizeof(*bo));
      goto fail;
   }

   *out_bo = bo;

   /* We don't use bo->name here because for the !TU_DEBUG=bo case bo->name is NULL. */
   tu_bo_set_kernel_name(dev, bo, name);

   if ((mem_property & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) &&
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

   return VK_SUCCESS;

fail:
   mtx_lock(&dev->vma_mutex);
   util_vma_heap_free(&dev->vma, req.iova, size);
   mtx_unlock(&dev->vma_mutex);
   return result;
}

static VkResult
virtio_bo_init_dmabuf(struct tu_device *dev,
                   struct tu_bo **out_bo,
                   uint64_t size,
                   int prime_fd)
{
   struct vdrm_device *vdrm = dev->vdev->vdrm;
   VkResult result;
   struct tu_bo* bo = NULL;

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
   mtx_lock(&dev->vma_mutex);

   uint32_t handle, res_id;
   uint64_t iova;

   handle = vdrm_dmabuf_to_handle(vdrm, prime_fd);
   if (!handle) {
      result = vk_error(dev, VK_ERROR_INVALID_EXTERNAL_HANDLE);
      goto out_unlock;
   }

   res_id = vdrm_handle_to_res_id(vdrm, handle);
   if (!res_id) {
      /* XXX gem_handle potentially leaked here since no refcnt */
      result = vk_error(dev, VK_ERROR_INVALID_EXTERNAL_HANDLE);
      goto out_unlock;
   }

   bo = tu_device_lookup_bo(dev, res_id);

   if (bo->refcnt != 0) {
      p_atomic_inc(&bo->refcnt);
      assert(bo->res_id == res_id);
      *out_bo = bo;
      result = VK_SUCCESS;
      goto out_unlock;
   }

   bo->res_id = res_id;

   result = virtio_allocate_userspace_iova_locked(dev, handle, size, 0,
                                                  TU_BO_ALLOC_DMABUF, &iova);
   if (result != VK_SUCCESS) {
      vdrm_bo_close(dev->vdev->vdrm, handle);
      goto out_unlock;
   }

   result =
      tu_bo_init(dev, NULL, bo, handle, size, iova, TU_BO_ALLOC_NO_FLAGS, "dmabuf");
   if (result != VK_SUCCESS) {
      util_vma_heap_free(&dev->vma, iova, size);
      memset(bo, 0, sizeof(*bo));
   } else {
      *out_bo = bo;
   }

out_unlock:
   mtx_unlock(&dev->vma_mutex);
   u_rwlock_wrunlock(&dev->dma_bo_lock);
   return result;
}

static VkResult
virtio_bo_map(struct tu_device *dev, struct tu_bo *bo, void *placed_addr)
{
   bo->map = vdrm_bo_map(dev->vdev->vdrm, bo->gem_handle, bo->size, placed_addr);
   if (bo->map == MAP_FAILED)
      return vk_error(dev, VK_ERROR_MEMORY_MAP_FAILED);

   return VK_SUCCESS;
}

static void
virtio_bo_allow_dump(struct tu_device *dev, struct tu_bo *bo)
{
   mtx_lock(&dev->bo_mutex);
   dev->submit_bo_list[bo->submit_bo_list_idx].flags |= MSM_SUBMIT_BO_DUMP;
   mtx_unlock(&dev->bo_mutex);
}

static VkResult
setup_fence_cmds(struct tu_device *dev)
{
   struct tu_virtio_device *vdev = dev->vdev;
   VkResult result;

   result = tu_bo_init_new(dev, NULL, &vdev->fence_cmds_mem,
                           sizeof(*vdev->fence_cmds), (enum tu_bo_alloc_flags)
                              (TU_BO_ALLOC_ALLOW_DUMP | TU_BO_ALLOC_GPU_READ_ONLY),
                           "fence_cmds");
   if (result != VK_SUCCESS)
      return result;

   result = tu_bo_map(dev, vdev->fence_cmds_mem, NULL);
   if (result != VK_SUCCESS)
      return result;

   vdev->fence_cmds = (struct tu_userspace_fence_cmds *)vdev->fence_cmds_mem->map;

   uint64_t fence_iova = dev->global_bo->iova + gb_offset(userspace_fence);
   for (int i = 0; i < ARRAY_SIZE(vdev->fence_cmds->cmds); i++) {
      struct tu_userspace_fence_cmd *c = &vdev->fence_cmds->cmds[i];

      memset(c, 0, sizeof(*c));

      if (fd_dev_gen(&dev->physical_device->dev_id) >= A7XX) {
         c->pkt[0] = pm4_pkt7_hdr((uint8_t)CP_EVENT_WRITE7, 4);
         c->pkt[1] = CP_EVENT_WRITE7_0(.event = CACHE_FLUSH_TS,
                           .write_src = EV_WRITE_USER_32B,
                           .write_dst = EV_DST_RAM,
                           .write_enabled = true).value;
      } else {
         c->pkt[0] = pm4_pkt7_hdr((uint8_t)CP_EVENT_WRITE, 4);
         c->pkt[1] = CP_EVENT_WRITE_0_EVENT(CACHE_FLUSH_TS);
      }
      c->pkt[2] = fence_iova;
      c->pkt[3] = fence_iova >> 32;
   }

   return result;
}

static VkResult
virtio_queue_submit(struct tu_queue *queue, void *_submit,
                    struct vk_sync_wait *waits, uint32_t wait_count,
                    struct vk_sync_signal *signals, uint32_t signal_count,
                    struct tu_u_trace_submission_data *u_trace_submission_data)
{
   VkResult result = VK_SUCCESS;
   int ret;
   struct tu_msm_queue_submit *submit =
      (struct tu_msm_queue_submit *)_submit;
   struct tu_virtio_device *vdev = queue->device->vdev;
   struct drm_virtgpu_execbuffer_syncobj *in_syncobjs, *out_syncobjs;
   uint64_t gpu_offset = 0;
   int ring_idx = queue->priority + 1;
   struct vdrm_execbuf_params params;
#if HAVE_PERFETTO
   struct tu_perfetto_clocks clocks;
   uint64_t start_ts = tu_perfetto_begin_submit();
#endif

   /* It would be nice to not need to defer this, but virtio_device_init()
    * happens before the device is initialized enough to allocate normal
    * GEM buffers
    */
   if (!vdev->fence_cmds) {
      VkResult result = setup_fence_cmds(queue->device);
      if (result != VK_SUCCESS)
         return result;
   }

   /* Add the userspace fence cmd: */
   struct tu_userspace_fence_cmds *fcmds = vdev->fence_cmds;
   if (queue->fence <= 0)
      queue->fence = 0;
   uint32_t fence = ++queue->fence;
   int idx = fence % ARRAY_SIZE(fcmds->cmds);
   struct tu_cs_entry fence_cs = {
      .bo = vdev->fence_cmds_mem,
      .size = 5 * 4,
      .offset = ((intptr_t)&fcmds->cmds[idx]) - (intptr_t)fcmds,
   };
   msm_submit_add_entries(queue->device, _submit, &fence_cs, 1);

   uint32_t entry_count =
      util_dynarray_num_elements(&submit->commands, struct drm_msm_gem_submit_cmd);
   unsigned nr_bos = entry_count ? queue->device->submit_bo_count : 0;
   unsigned bos_len = nr_bos * sizeof(struct drm_msm_gem_submit_bo);
   unsigned cmd_len = entry_count * sizeof(struct drm_msm_gem_submit_cmd);
   unsigned req_len = sizeof(struct msm_ccmd_gem_submit_req) + bos_len + cmd_len;
   struct msm_ccmd_gem_submit_req *req;
   uint32_t flags = MSM_PIPE_3D0;

   /* Allocate without wait timeline semaphores */
   in_syncobjs = (struct drm_virtgpu_execbuffer_syncobj *) vk_zalloc(
      &queue->device->vk.alloc,
      wait_count * sizeof(*in_syncobjs), 8,
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (in_syncobjs == NULL) {
      result = vk_error(queue, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_in_syncobjs;
   }

   /* Allocate with signal timeline semaphores considered */
   out_syncobjs = (struct drm_virtgpu_execbuffer_syncobj *) vk_zalloc(
      &queue->device->vk.alloc,
      signal_count * sizeof(*out_syncobjs), 8,
      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (out_syncobjs == NULL) {
      result = vk_error(queue, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_out_syncobjs;
   }

   for (uint32_t i = 0; i < wait_count; i++) {
      struct vk_sync *sync = waits[i].sync;

      in_syncobjs[i] = (struct drm_virtgpu_execbuffer_syncobj) {
         .handle = tu_syncobj_from_vk_sync(sync),
         .flags = 0,
         .point = waits[i].wait_value,
      };
   }

   for (uint32_t i = 0; i < signal_count; i++) {
      struct vk_sync *sync = signals[i].sync;

      out_syncobjs[i] = (struct drm_virtgpu_execbuffer_syncobj) {
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

   req = (struct msm_ccmd_gem_submit_req *)vk_alloc(
         &queue->device->vk.alloc, req_len, 8, VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (!req) {
      mtx_unlock(&queue->device->bo_mutex);
      result = vk_error(queue, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail_alloc_req;
   }

   req->hdr      = MSM_CCMD(GEM_SUBMIT, req_len);
   req->flags    = flags;
   req->queue_id = queue->msm_queue_id;
   req->nr_bos   = nr_bos;
   req->nr_cmds  = entry_count;

   /* Use same kernel fence and userspace fence seqno to avoid having
    * to track both:
    */
   req->fence    = queue->fence;

   memcpy(req->payload, queue->device->submit_bo_list, bos_len);
   memcpy(req->payload + bos_len, submit->commands.data, cmd_len);

   params = (struct vdrm_execbuf_params) {
      .ring_idx = ring_idx,
      .req = &req->hdr,
      .in_syncobjs = in_syncobjs,
      .out_syncobjs = out_syncobjs,
      .num_in_syncobjs = wait_count,
      .num_out_syncobjs = signal_count,
   };

   ret = vdrm_execbuf(vdev->vdrm, &params);

   mtx_unlock(&queue->device->bo_mutex);

   if (ret) {
      result = vk_device_set_lost(&queue->device->vk, "submit failed: %m");
      goto fail_submit;
   }

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
   vk_free(&queue->device->vk.alloc, req);
fail_alloc_req:
   vk_free(&queue->device->vk.alloc, out_syncobjs);
fail_out_syncobjs:
   vk_free(&queue->device->vk.alloc, in_syncobjs);
fail_in_syncobjs:
   return result;
}

static const struct tu_knl virtio_knl_funcs = {
      .name = "virtgpu",

      .device_init = virtio_device_init,
      .device_finish = virtio_device_finish,
      .device_get_gpu_timestamp = virtio_device_get_gpu_timestamp,
      .device_get_suspend_count = virtio_device_get_suspend_count,
      .device_check_status = virtio_device_check_status,
      .submitqueue_new = virtio_submitqueue_new,
      .submitqueue_close = virtio_submitqueue_close,
      .bo_init = virtio_bo_init,
      .bo_init_dmabuf = virtio_bo_init_dmabuf,
      .bo_export_dmabuf = tu_drm_export_dmabuf,
      .bo_map = virtio_bo_map,
      .bo_allow_dump = virtio_bo_allow_dump,
      .bo_finish = tu_drm_bo_finish,
      .submit_create = msm_submit_create,
      .submit_finish = msm_submit_finish,
      .submit_add_entries = msm_submit_add_entries,
      .queue_submit = virtio_queue_submit,
      .queue_wait_fence = virtio_queue_wait_fence,
};

VkResult
tu_knl_drm_virtio_load(struct tu_instance *instance,
                       int fd, struct _drmVersion *version,
                       struct tu_physical_device **out)
{
   struct virgl_renderer_capset_drm caps;
   struct vdrm_device *vdrm;
   VkResult result = VK_SUCCESS;
   uint64_t val;

   /* Debug option to force fallback to venus: */
   if (debug_get_bool_option("TU_NO_VIRTIO", false))
      return VK_ERROR_INCOMPATIBLE_DRIVER;

   if (drmGetCap(fd, DRM_CAP_SYNCOBJ, &val) || !val) {
      return vk_startup_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                               "kernel driver for device %s does not support DRM_CAP_SYNC_OBJ",
                               version->name);
   }

   /* Try to connect. If this doesn't work, it's probably because we're running
    * in a non-Adreno VM. Unless startup debug info is specifically requested,
    * we should silently exit and let another Vulkan driver try probing instead.
    */
   vdrm = vdrm_device_connect(fd, VIRTGPU_DRM_CONTEXT_MSM);
   if (!vdrm) {
      if (TU_DEBUG(STARTUP)) {
         return vk_startup_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                                  "could not get connect vdrm: %s", strerror(errno));
      } else {
         return VK_ERROR_INCOMPATIBLE_DRIVER;
      }
   }

   caps = vdrm->caps;

   bool has_preemption = virtio_has_preemption(vdrm);

   /* If virglrenderer is too old, we may need another round-trip to get this.
    */
   if (caps.u.msm.highest_bank_bit == 0)
      caps.u.msm.highest_bank_bit = tu_drm_get_highest_bank_bit(vdrm);

   uint32_t bank_swizzle_levels = tu_drm_get_ubwc_swizzle(vdrm);
   enum fdl_macrotile_mode macrotile_mode = tu_drm_get_macrotile_mode(vdrm);

   bool has_raytracing = tu_drm_get_raytracing(vdrm);

   vdrm_device_close(vdrm);

   mesa_logd("wire_format_version: %u", caps.wire_format_version);
   mesa_logd("version_major:       %u", caps.version_major);
   mesa_logd("version_minor:       %u", caps.version_minor);
   mesa_logd("version_patchlevel:  %u", caps.version_patchlevel);
   mesa_logd("has_cached_coherent: %u", caps.u.msm.has_cached_coherent);
   mesa_logd("va_start:            0x%0" PRIx64, caps.u.msm.va_start);
   mesa_logd("va_size:             0x%0" PRIx64, caps.u.msm.va_size);
   mesa_logd("gpu_id:              %u", caps.u.msm.gpu_id);
   mesa_logd("gmem_size:           %u", caps.u.msm.gmem_size);
   mesa_logd("gmem_base:           0x%0" PRIx64, caps.u.msm.gmem_base);
   mesa_logd("chip_id:             0x%0" PRIx64, caps.u.msm.chip_id);
   mesa_logd("max_freq:            %u", caps.u.msm.max_freq);
   mesa_logd("highest_bank_bit:    %u", caps.u.msm.highest_bank_bit);
   mesa_logd("ubwc_swizzle:        0x%" PRIx64, caps.u.msm.ubwc_swizzle);
   mesa_logd("macrotile_mode:      %" PRIu64, caps.u.msm.macrotile_mode);
   mesa_logd("has_raytracing:      %x", caps.u.msm.has_raytracing);
   mesa_logd("has_preemption:      %d", caps.u.msm.has_preemption);
   mesa_logd("uche_trap_base:      0x%" PRIx64, caps.u.msm.uche_trap_base);

   if (caps.wire_format_version != 2) {
      return vk_startup_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                               "Unsupported protocol version: %u",
                               caps.wire_format_version);
   }

   if ((caps.version_major != 1) || (caps.version_minor < 9)) {
      return vk_startup_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                               "unsupported version: %u.%u.%u",
                               caps.version_major,
                               caps.version_minor,
                               caps.version_patchlevel);
   }

   if (!caps.u.msm.va_size) {
      return vk_startup_errorf(instance, VK_ERROR_INCOMPATIBLE_DRIVER,
                               "No address space");
   }

   struct tu_physical_device *device = (struct tu_physical_device *)
      vk_zalloc(&instance->vk.alloc, sizeof(*device), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!device) {
      result = vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto fail;
   }

   device->msm_major_version = caps.version_major;
   device->msm_minor_version = caps.version_minor;

   device->instance = instance;
   device->local_fd = fd;

   device->dev_id.gpu_id  = caps.u.msm.gpu_id;
   device->dev_id.chip_id = caps.u.msm.chip_id;
   device->gmem_size      = caps.u.msm.gmem_size;
   device->gmem_base      = caps.u.msm.gmem_base;
   device->va_start       = caps.u.msm.va_start;
   device->va_size        = caps.u.msm.va_size;
   device->ubwc_config.highest_bank_bit = caps.u.msm.highest_bank_bit;
   device->has_set_iova   = true;
   device->has_preemption = has_preemption;

   device->ubwc_config.bank_swizzle_levels = bank_swizzle_levels;
   device->ubwc_config.macrotile_mode = macrotile_mode;

   device->gmem_size = debug_get_num_option("TU_GMEM", device->gmem_size);

   device->has_cached_coherent_memory = caps.u.msm.has_cached_coherent;

   device->submitqueue_priority_count = caps.u.msm.priorities;

   device->has_raytracing = has_raytracing;

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

   instance->knl = &virtio_knl_funcs;

   *out = device;

   return VK_SUCCESS;

fail:
   vk_free(&instance->vk.alloc, device);
   return result;
}
