/*
 * Copyright © 2020 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "tu_drm.h"

#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#include "msm_kgsl.h"
#include "vk_util.h"

#include "util/debug.h"

#include "tu_cmd_buffer.h"
#include "tu_cs.h"
#include "tu_device.h"
#include "tu_dynamic_rendering.h"

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
tu_bo_init_new_explicit_iova(struct tu_device *dev,
                             struct tu_bo **out_bo,
                             uint64_t size,
                             uint64_t client_iova,
                             enum tu_bo_alloc_flags flags)
{
   assert(client_iova == 0);

   struct kgsl_gpumem_alloc_id req = {
      .size = size,
   };

   if (flags & TU_BO_ALLOC_GPU_READ_ONLY)
      req.flags |= KGSL_MEMFLAGS_GPUREADONLY;

   int ret;

   ret = safe_ioctl(dev->physical_device->local_fd,
                    IOCTL_KGSL_GPUMEM_ALLOC_ID, &req);
   if (ret) {
      return vk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                       "GPUMEM_ALLOC_ID failed (%s)", strerror(errno));
   }

   struct tu_bo* bo = tu_device_lookup_bo(dev, req.id);
   assert(bo && bo->gem_handle == 0);

   *bo = (struct tu_bo) {
      .gem_handle = req.id,
      .size = req.mmapsize,
      .iova = req.gpuaddr,
      .refcnt = 1,
   };

   *out_bo = bo;

   return VK_SUCCESS;
}

VkResult
tu_bo_init_dmabuf(struct tu_device *dev,
                  struct tu_bo **out_bo,
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
      return vk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                       "Failed to import dma-buf (%s)\n", strerror(errno));

   struct kgsl_gpuobj_info info_req = {
      .id = req.id,
   };

   ret = safe_ioctl(dev->physical_device->local_fd,
                    IOCTL_KGSL_GPUOBJ_INFO, &info_req);
   if (ret)
      return vk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                       "Failed to get dma-buf info (%s)\n", strerror(errno));

   struct tu_bo* bo = tu_device_lookup_bo(dev, req.id);
   assert(bo && bo->gem_handle == 0);

   *bo = (struct tu_bo) {
      .gem_handle = req.id,
      .size = info_req.size,
      .iova = info_req.gpuaddr,
      .refcnt = 1,
   };

   *out_bo = bo;

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
      return vk_error(dev, VK_ERROR_MEMORY_MAP_FAILED);

   bo->map = map;

   return VK_SUCCESS;
}

void
tu_bo_finish(struct tu_device *dev, struct tu_bo *bo)
{
   assert(bo->gem_handle);

   if (!p_atomic_dec_zero(&bo->refcnt))
      return;

   if (bo->map)
      munmap(bo->map, bo->size);

   struct kgsl_gpumem_free_id req = {
      .id = bo->gem_handle
   };

   /* Tell sparse array that entry is free */
   memset(bo, 0, sizeof(*bo));

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
tu_enumerate_devices(struct vk_instance *vk_instance)
{
   struct tu_instance *instance =
      container_of(vk_instance, struct tu_instance, vk);

   static const char path[] = "/dev/kgsl-3d0";
   int fd;

   if (instance->vk.enabled_extensions.KHR_display) {
      return vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                       "I can't KHR_display");
   }

   fd = open(path, O_RDWR | O_CLOEXEC);
   if (fd < 0) {
      if (errno == ENOENT)
         return VK_SUCCESS;

      return vk_errorf(instance, VK_ERROR_INITIALIZATION_FAILED,
                       "failed to open device %s", path);
   }

   struct tu_physical_device *device =
      vk_zalloc(&instance->vk.alloc, sizeof(*device), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_INSTANCE);
   if (!device) {
      close(fd);
      return vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
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

   device->instance = instance;
   device->master_fd = -1;
   device->local_fd = fd;

   device->dev_id.gpu_id =
      ((info.chip_id >> 24) & 0xff) * 100 +
      ((info.chip_id >> 16) & 0xff) * 10 +
      ((info.chip_id >>  8) & 0xff);
   device->dev_id.chip_id = info.chip_id;
   device->gmem_size = env_var_as_unsigned("TU_GMEM", info.gmem_sizebytes);
   device->gmem_base = gmem_iova;

   device->submitqueue_priority_count = 1;

   device->heap.size = tu_get_system_heap_size();
   device->heap.used = 0u;
   device->heap.flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;

   if (tu_physical_device_init(device, instance) != VK_SUCCESS)
      goto fail;

   list_addtail(&device->vk.link, &instance->vk.physical_devices.list);

   return VK_SUCCESS;

fail:
   vk_free(&instance->vk.alloc, device);
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

VKAPI_ATTR VkResult VKAPI_CALL
tu_QueueSubmit2(VkQueue _queue,
                uint32_t submitCount,
                const VkSubmitInfo2 *pSubmits,
                VkFence _fence)
{
   MESA_TRACE_FUNC();
   TU_FROM_HANDLE(tu_queue, queue, _queue);
   TU_FROM_HANDLE(tu_syncobj, fence, _fence);
   VkResult result = VK_SUCCESS;

   if (unlikely(queue->device->physical_device->instance->debug_flags &
                 TU_DEBUG_LOG_SKIP_GMEM_OPS)) {
      tu_dbg_log_gmem_load_store_skips(queue->device);
   }

   struct tu_cmd_buffer **submit_cmd_buffers[submitCount];
   uint32_t submit_cmd_buffer_count[submitCount];

   uint32_t max_entry_count = 0;
   for (uint32_t i = 0; i < submitCount; ++i) {
      const VkSubmitInfo2 *submit = pSubmits + i;

      const VkPerformanceQuerySubmitInfoKHR *perf_info =
         vk_find_struct_const(pSubmits[i].pNext,
                              PERFORMANCE_QUERY_SUBMIT_INFO_KHR);

      struct tu_cmd_buffer *old_cmd_buffers[submit->commandBufferInfoCount];
      uint32_t cmdbuf_count = submit->commandBufferInfoCount;
      for (uint32_t j = 0; j < cmdbuf_count; ++j) {
         TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, submit->pCommandBufferInfos[j].commandBuffer);
         old_cmd_buffers[j] = cmdbuf;
      }

      struct tu_cmd_buffer **cmd_buffers = old_cmd_buffers;
      tu_insert_dynamic_cmdbufs(queue->device, &cmd_buffers, &cmdbuf_count);
      if (cmd_buffers == old_cmd_buffers) {
         cmd_buffers =
            vk_alloc(&queue->device->vk.alloc,
                     sizeof(*cmd_buffers) * cmdbuf_count, 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
         memcpy(cmd_buffers, old_cmd_buffers,
                sizeof(*cmd_buffers) * cmdbuf_count);
      }
      submit_cmd_buffers[i] = cmd_buffers;
      submit_cmd_buffer_count[i] = cmdbuf_count;

      uint32_t entry_count = 0;
      for (uint32_t j = 0; j < cmdbuf_count; ++j) {
         entry_count += cmd_buffers[i]->cs.entry_count;
         if (perf_info)
            entry_count++;
      }

      if (tu_autotune_submit_requires_fence(cmd_buffers, cmdbuf_count))
         entry_count++;

      max_entry_count = MAX2(max_entry_count, entry_count);
   }

   struct kgsl_command_object *cmds =
      vk_alloc(&queue->device->vk.alloc,
               sizeof(cmds[0]) * max_entry_count, 8,
               VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (cmds == NULL)
      return vk_error(queue, VK_ERROR_OUT_OF_HOST_MEMORY);

   for (uint32_t i = 0; i < submitCount; ++i) {
      const VkSubmitInfo2 *submit = pSubmits + i;
      uint32_t entry_idx = 0;
      const VkPerformanceQuerySubmitInfoKHR *perf_info =
         vk_find_struct_const(pSubmits[i].pNext,
                              PERFORMANCE_QUERY_SUBMIT_INFO_KHR);


      struct tu_cmd_buffer **cmd_buffers = submit_cmd_buffers[i];
      uint32_t cmdbuf_count = submit_cmd_buffer_count[i];
      for (uint32_t j = 0; j < cmdbuf_count; j++) {
         struct tu_cmd_buffer *cmdbuf = cmd_buffers[j];
         struct tu_cs *cs = &cmdbuf->cs;

         if (perf_info) {
            struct tu_cs_entry *perf_cs_entry =
               &cmdbuf->device->perfcntrs_pass_cs_entries[perf_info->counterPassIndex];

            cmds[entry_idx++] = (struct kgsl_command_object) {
               .offset = perf_cs_entry->offset,
               .gpuaddr = perf_cs_entry->bo->iova,
               .size = perf_cs_entry->size,
               .flags = KGSL_CMDLIST_IB,
               .id = perf_cs_entry->bo->gem_handle,
            };
         }

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

      if (tu_autotune_submit_requires_fence(cmd_buffers, cmdbuf_count)) {
         struct tu_cs *autotune_cs =
            tu_autotune_on_submit(queue->device,
                                  &queue->device->autotune,
                                  cmd_buffers,
                                  cmdbuf_count);
         cmds[entry_idx++] = (struct kgsl_command_object) {
            .offset = autotune_cs->entries[0].offset,
            .gpuaddr = autotune_cs->entries[0].bo->iova,
            .size = autotune_cs->entries[0].size,
            .flags = KGSL_CMDLIST_IB,
            .id = autotune_cs->entries[0].bo->gem_handle,
         };
      }

      VkSemaphore wait_semaphores[submit->waitSemaphoreInfoCount];
      for (uint32_t j = 0; j < submit->waitSemaphoreInfoCount; j++) {
         wait_semaphores[j] = submit->pWaitSemaphoreInfos[j].semaphore;
      }

      struct tu_syncobj s = sync_merge(wait_semaphores,
                                       submit->waitSemaphoreInfoCount,
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
         result = vk_device_set_lost(&queue->device->vk,
                                     "submit failed: %s\n", strerror(errno));
         goto fail;
      }

      for (uint32_t i = 0; i < submit->signalSemaphoreInfoCount; i++) {
         TU_FROM_HANDLE(tu_syncobj, sem, submit->pSignalSemaphoreInfos[i].semaphore);
         sem->timestamp = req.timestamp;
         sem->timestamp_valid = true;
      }

      /* no need to merge fences as queue execution is serialized */
      if (i == submitCount - 1) {
         int fd = timestamp_to_fd(queue, req.timestamp);
         if (fd < 0) {
            result = vk_device_set_lost(&queue->device->vk,
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
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (signaled)
      tu_finishme("CREATE FENCE SIGNALED");

   sync->timestamp_valid = false;
   *p_sync = sync;

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_ImportSemaphoreFdKHR(VkDevice _device,
                        const VkImportSemaphoreFdInfoKHR *pImportSemaphoreFdInfo)
{
   tu_finishme("ImportSemaphoreFdKHR");
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetSemaphoreFdKHR(VkDevice _device,
                     const VkSemaphoreGetFdInfoKHR *pGetFdInfo,
                     int *pFd)
{
   tu_finishme("GetSemaphoreFdKHR");
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateSemaphore(VkDevice device,
                   const VkSemaphoreCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkSemaphore *pSemaphore)
{
   return sync_create(device, false, false, pAllocator, (void**) pSemaphore);
}

VKAPI_ATTR void VKAPI_CALL
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

VKAPI_ATTR VkResult VKAPI_CALL
tu_ImportFenceFdKHR(VkDevice _device,
                    const VkImportFenceFdInfoKHR *pImportFenceFdInfo)
{
   tu_stub();

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetFenceFdKHR(VkDevice _device,
                 const VkFenceGetFdInfoKHR *pGetFdInfo,
                 int *pFd)
{
   tu_stub();

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_CreateFence(VkDevice device,
               const VkFenceCreateInfo *info,
               const VkAllocationCallbacks *pAllocator,
               VkFence *pFence)
{
   return sync_create(device, info->flags & VK_FENCE_CREATE_SIGNALED_BIT, true,
                      pAllocator, (void**) pFence);
}

VKAPI_ATTR void VKAPI_CALL
tu_DestroyFence(VkDevice _device, VkFence fence, const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_syncobj, sync, fence);

   if (!sync)
      return;

   vk_object_free(&device->vk, pAllocator, sync);
}

/* safe_ioctl is not enough as restarted waits would not adjust the timeout
 * which could lead to waiting substantially longer than requested
 */
static int
wait_timestamp_safe(int fd,
                    unsigned int context_id,
                    unsigned int timestamp,
                    int64_t timeout_ms)
{
   int64_t start_time = os_time_get_nano();
   struct kgsl_device_waittimestamp_ctxtid wait = {
      .context_id = context_id,
      .timestamp = timestamp,
      .timeout = timeout_ms,
   };

   while (true) {
      int ret = ioctl(fd, IOCTL_KGSL_DEVICE_WAITTIMESTAMP_CTXTID, &wait);

      if (ret == -1 && (errno == EINTR || errno == EAGAIN)) {
         int64_t current_time = os_time_get_nano();

         /* update timeout to consider time that has passed since the start */
         timeout_ms -= (current_time - start_time) / 1000000;
         if (timeout_ms <= 0) {
            errno = ETIME;
            return -1;
         }

         wait.timeout = (unsigned int) timeout_ms;
         start_time = current_time;
      } else {
         return ret;
      }
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
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

   int ret = wait_timestamp_safe(device->fd, 
                                 device->queues[0]->msm_queue_id, 
                                 s.timestamp, 
                                 timeout / 1000000);
   if (ret) {
      assert(errno == ETIME);
      return VK_TIMEOUT;
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_ResetFences(VkDevice _device, uint32_t count, const VkFence *pFences)
{
   for (uint32_t i = 0; i < count; i++) {
      TU_FROM_HANDLE(tu_syncobj, sync, pFences[i]);
      sync->timestamp_valid = false;
   }
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetFenceStatus(VkDevice _device, VkFence _fence)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_syncobj, sync, _fence);

   if (!sync->timestamp_valid)
      return VK_NOT_READY;


   int ret = wait_timestamp_safe(device->fd, 
                                 device->queues[0]->msm_queue_id, 
                                 sync->timestamp, 
                                 0);
   if (ret) {
      assert(errno == ETIME);
      return VK_NOT_READY;
   }

   return VK_SUCCESS;
}

VkResult
tu_device_wait_u_trace(struct tu_device *dev, struct tu_u_trace_syncobj *syncobj)
{
   tu_finishme("tu_device_wait_u_trace");
   return VK_SUCCESS;
}

int
tu_device_get_gpu_timestamp(struct tu_device *dev, uint64_t *ts)
{
   tu_finishme("tu_device_get_gpu_timestamp");
   return 0;
}

int
tu_device_get_suspend_count(struct tu_device *dev, uint64_t *suspend_count)
{
   /* kgsl doesn't have a way to get it */
   *suspend_count = 0;
   return 0;
}

VkResult
tu_device_check_status(struct vk_device *vk_device)
{
   struct tu_device *device = container_of(vk_device, struct tu_device, vk);

   for (unsigned i = 0; i < TU_MAX_QUEUE_FAMILIES; i++) {
      for (unsigned q = 0; q < device->queue_count[i]; q++) {
         /* KGSL's KGSL_PROP_GPU_RESET_STAT takes the u32 msm_queue_id and returns a
         * KGSL_CTX_STAT_* for the worst reset that happened since the last time it
         * was queried on that queue.
         */
         uint32_t value = device->queues[i][q].msm_queue_id;
         VkResult status = get_kgsl_prop(device->fd, KGSL_PROP_GPU_RESET_STAT,
                                       &value, sizeof(value));
         if (status != VK_SUCCESS)
            return vk_device_set_lost(&device->vk, "Failed to get GPU reset status");

         if (value != KGSL_CTX_STAT_NO_ERROR &&
            value != KGSL_CTX_STAT_INNOCENT_CONTEXT_RESET_EXT) {
            return vk_device_set_lost(&device->vk, "GPU faulted or hung");
         }
      }
   }

   return VK_SUCCESS;
}

#ifdef ANDROID
VKAPI_ATTR VkResult VKAPI_CALL
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
