/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef VN_RENDERER_H
#define VN_RENDERER_H

#include "vn_common.h"

struct vn_renderer_shmem {
   atomic_int refcount;
   uint32_t res_id;
   size_t mmap_size; /* for internal use only (i.e., munmap) */
   void *mmap_ptr;
};

struct vn_renderer_bo {
   atomic_int refcount;
   uint32_t res_id;
   /* for internal use only */
   size_t mmap_size;
   void *mmap_ptr;
};

enum vn_renderer_sync_flags {
   VN_RENDERER_SYNC_SHAREABLE = 1u << 0,
   VN_RENDERER_SYNC_BINARY = 1u << 1,
};

struct vn_renderer_sync_ops {
   void (*destroy)(struct vn_renderer_sync *sync);

   /* a sync can be initialized/released multiple times */
   VkResult (*init)(struct vn_renderer_sync *sync,
                    uint64_t initial_val,
                    uint32_t flags);
   VkResult (*init_syncobj)(struct vn_renderer_sync *sync,
                            int fd,
                            bool sync_file);
   void (*release)(struct vn_renderer_sync *sync);

   int (*export_syncobj)(struct vn_renderer_sync *sync, bool sync_file);

   /* reset the counter */
   VkResult (*reset)(struct vn_renderer_sync *sync, uint64_t initial_val);

   /* read the current value from the counter */
   VkResult (*read)(struct vn_renderer_sync *sync, uint64_t *val);

   /* write a new value (larger than the current one) to the counter */
   VkResult (*write)(struct vn_renderer_sync *sync, uint64_t val);
};

/*
 * A sync consists of a uint64_t counter.  The counter can be updated by CPU
 * or by GPU.  It can also be waited on by CPU or by GPU until it reaches
 * certain values.
 *
 * This models after timeline VkSemaphore rather than timeline drm_syncobj.
 * The main difference is that drm_syncobj can have unsignaled value 0.
 */
struct vn_renderer_sync {
   uint32_t sync_id;

   struct vn_renderer_sync_ops ops;
};

struct vn_renderer_info {
   struct {
      uint16_t vendor_id;
      uint16_t device_id;

      bool has_bus_info;
      uint16_t domain;
      uint8_t bus;
      uint8_t device;
      uint8_t function;
   } pci;

   bool has_dmabuf_import;
   bool has_cache_management;
   bool has_external_sync;
   bool has_implicit_fencing;

   uint32_t max_sync_queue_count;

   /* hw capset */
   uint32_t wire_format_version;
   uint32_t vk_xml_version;
   uint32_t vk_ext_command_serialization_spec_version;
   uint32_t vk_mesa_venus_protocol_spec_version;
};

struct vn_renderer_submit_batch {
   const void *cs_data;
   size_t cs_size;

   /*
    * Submit cs to the virtual sync queue identified by sync_queue_index.  The
    * virtual queue is assumed to be associated with the physical VkQueue
    * identified by vk_queue_id.  After the execution completes on the
    * VkQueue, the virtual sync queue is signaled.
    *
    * sync_queue_index must be less than max_sync_queue_count.
    *
    * vk_queue_id specifies the object id of a VkQueue.
    *
    * When sync_queue_cpu is true, it specifies the special CPU sync queue,
    * and sync_queue_index/vk_queue_id are ignored.  TODO revisit this later
    */
   uint32_t sync_queue_index;
   bool sync_queue_cpu;
   vn_object_id vk_queue_id;

   /* syncs to update when the virtual sync queue is signaled */
   struct vn_renderer_sync *const *syncs;
   /* TODO allow NULL when syncs are all binary? */
   const uint64_t *sync_values;
   uint32_t sync_count;
};

struct vn_renderer_submit {
   /* BOs to pin and to fence implicitly
    *
    * TODO track all bos and automatically pin them.  We don't do it yet
    * because each vn_command_buffer owns a bo.  We can probably make do by
    * returning the bos to a bo cache and exclude bo cache from pinning.
    */
   struct vn_renderer_bo *const *bos;
   uint32_t bo_count;

   const struct vn_renderer_submit_batch *batches;
   uint32_t batch_count;
};

struct vn_renderer_wait {
   bool wait_any;
   uint64_t timeout;

   struct vn_renderer_sync *const *syncs;
   /* TODO allow NULL when syncs are all binary? */
   const uint64_t *sync_values;
   uint32_t sync_count;
};

struct vn_renderer_ops {
   void (*destroy)(struct vn_renderer *renderer,
                   const VkAllocationCallbacks *alloc);

   void (*get_info)(struct vn_renderer *renderer,
                    struct vn_renderer_info *info);

   VkResult (*submit)(struct vn_renderer *renderer,
                      const struct vn_renderer_submit *submit);

   /*
    * On success, returns VK_SUCCESS or VK_TIMEOUT.  On failure, returns
    * VK_ERROR_DEVICE_LOST or out of device/host memory.
    */
   VkResult (*wait)(struct vn_renderer *renderer,
                    const struct vn_renderer_wait *wait);

   struct vn_renderer_sync *(*sync_create)(struct vn_renderer *renderer);
};

struct vn_renderer_shmem_ops {
   struct vn_renderer_shmem *(*create)(struct vn_renderer *renderer,
                                       size_t size);
   void (*destroy)(struct vn_renderer *renderer,
                   struct vn_renderer_shmem *shmem);
};

struct vn_renderer_bo_ops {
   VkResult (*create_from_device_memory)(
      struct vn_renderer *renderer,
      VkDeviceSize size,
      vn_object_id mem_id,
      VkMemoryPropertyFlags flags,
      VkExternalMemoryHandleTypeFlags external_handles,
      struct vn_renderer_bo **out_bo);

   VkResult (*create_from_dmabuf)(
      struct vn_renderer *renderer,
      VkDeviceSize size,
      int fd,
      VkMemoryPropertyFlags flags,
      VkExternalMemoryHandleTypeFlags external_handles,
      struct vn_renderer_bo **out_bo);

   bool (*destroy)(struct vn_renderer *renderer, struct vn_renderer_bo *bo);

   int (*export_dmabuf)(struct vn_renderer *renderer,
                        struct vn_renderer_bo *bo);

   /* map is not thread-safe */
   void *(*map)(struct vn_renderer *renderer, struct vn_renderer_bo *bo);

   void (*flush)(struct vn_renderer *renderer,
                 struct vn_renderer_bo *bo,
                 VkDeviceSize offset,
                 VkDeviceSize size);
   void (*invalidate)(struct vn_renderer *renderer,
                      struct vn_renderer_bo *bo,
                      VkDeviceSize offset,
                      VkDeviceSize size);
};

struct vn_renderer {
   struct vn_renderer_ops ops;
   struct vn_renderer_shmem_ops shmem_ops;
   struct vn_renderer_bo_ops bo_ops;
};

VkResult
vn_renderer_create_virtgpu(struct vn_instance *instance,
                           const VkAllocationCallbacks *alloc,
                           struct vn_renderer **renderer);

VkResult
vn_renderer_create_vtest(struct vn_instance *instance,
                         const VkAllocationCallbacks *alloc,
                         struct vn_renderer **renderer);

static inline VkResult
vn_renderer_create(struct vn_instance *instance,
                   const VkAllocationCallbacks *alloc,
                   struct vn_renderer **renderer)
{
   if (VN_DEBUG(VTEST)) {
      VkResult result = vn_renderer_create_vtest(instance, alloc, renderer);
      if (result == VK_SUCCESS)
         return VK_SUCCESS;
   }

   return vn_renderer_create_virtgpu(instance, alloc, renderer);
}

static inline void
vn_renderer_destroy(struct vn_renderer *renderer,
                    const VkAllocationCallbacks *alloc)
{
   renderer->ops.destroy(renderer, alloc);
}

static inline void
vn_renderer_get_info(struct vn_renderer *renderer,
                     struct vn_renderer_info *info)
{
   renderer->ops.get_info(renderer, info);
}

static inline VkResult
vn_renderer_submit(struct vn_renderer *renderer,
                   const struct vn_renderer_submit *submit)
{
   return renderer->ops.submit(renderer, submit);
}

static inline VkResult
vn_renderer_submit_simple(struct vn_renderer *renderer,
                          const void *cs_data,
                          size_t cs_size)
{
   const struct vn_renderer_submit submit = {
      .batches =
         &(const struct vn_renderer_submit_batch){
            .cs_data = cs_data,
            .cs_size = cs_size,
         },
      .batch_count = 1,
   };
   return vn_renderer_submit(renderer, &submit);
}

static inline VkResult
vn_renderer_wait(struct vn_renderer *renderer,
                 const struct vn_renderer_wait *wait)
{
   return renderer->ops.wait(renderer, wait);
}

static inline struct vn_renderer_shmem *
vn_renderer_shmem_create(struct vn_renderer *renderer, size_t size)
{
   struct vn_renderer_shmem *shmem =
      renderer->shmem_ops.create(renderer, size);
   if (shmem) {
      assert(atomic_load(&shmem->refcount) == 1);
      assert(shmem->res_id);
      assert(shmem->mmap_size >= size);
      assert(shmem->mmap_ptr);
   }

   return shmem;
}

static inline struct vn_renderer_shmem *
vn_renderer_shmem_ref(struct vn_renderer *renderer,
                      struct vn_renderer_shmem *shmem)
{
   const int old =
      atomic_fetch_add_explicit(&shmem->refcount, 1, memory_order_relaxed);
   assert(old >= 1);

   return shmem;
}

static inline void
vn_renderer_shmem_unref(struct vn_renderer *renderer,
                        struct vn_renderer_shmem *shmem)
{
   const int old =
      atomic_fetch_sub_explicit(&shmem->refcount, 1, memory_order_release);
   assert(old >= 1);

   if (old == 1) {
      atomic_thread_fence(memory_order_acquire);
      renderer->shmem_ops.destroy(renderer, shmem);
   }
}

static inline VkResult
vn_renderer_bo_create_from_device_memory(
   struct vn_renderer *renderer,
   VkDeviceSize size,
   vn_object_id mem_id,
   VkMemoryPropertyFlags flags,
   VkExternalMemoryHandleTypeFlags external_handles,
   struct vn_renderer_bo **out_bo)
{
   struct vn_renderer_bo *bo;
   VkResult result = renderer->bo_ops.create_from_device_memory(
      renderer, size, mem_id, flags, external_handles, &bo);
   if (result != VK_SUCCESS)
      return result;

   assert(atomic_load(&bo->refcount) == 1);
   assert(bo->res_id);
   assert(!bo->mmap_size || bo->mmap_size >= size);

   *out_bo = bo;
   return VK_SUCCESS;
}

static inline VkResult
vn_renderer_bo_create_from_dmabuf(
   struct vn_renderer *renderer,
   VkDeviceSize size,
   int fd,
   VkMemoryPropertyFlags flags,
   VkExternalMemoryHandleTypeFlags external_handles,
   struct vn_renderer_bo **out_bo)
{
   struct vn_renderer_bo *bo;
   VkResult result = renderer->bo_ops.create_from_dmabuf(
      renderer, size, fd, flags, external_handles, &bo);
   if (result != VK_SUCCESS)
      return result;

   assert(atomic_load(&bo->refcount) >= 1);
   assert(bo->res_id);
   assert(!bo->mmap_size || bo->mmap_size >= size);

   *out_bo = bo;
   return VK_SUCCESS;
}

static inline struct vn_renderer_bo *
vn_renderer_bo_ref(struct vn_renderer *renderer, struct vn_renderer_bo *bo)
{
   const int old =
      atomic_fetch_add_explicit(&bo->refcount, 1, memory_order_relaxed);
   assert(old >= 1);

   return bo;
}

static inline bool
vn_renderer_bo_unref(struct vn_renderer *renderer, struct vn_renderer_bo *bo)
{
   const int old =
      atomic_fetch_sub_explicit(&bo->refcount, 1, memory_order_release);
   assert(old >= 1);

   if (old == 1) {
      atomic_thread_fence(memory_order_acquire);
      return renderer->bo_ops.destroy(renderer, bo);
   }

   return false;
}

static inline int
vn_renderer_bo_export_dmabuf(struct vn_renderer *renderer,
                             struct vn_renderer_bo *bo)
{
   return renderer->bo_ops.export_dmabuf(renderer, bo);
}

static inline void *
vn_renderer_bo_map(struct vn_renderer *renderer, struct vn_renderer_bo *bo)
{
   return renderer->bo_ops.map(renderer, bo);
}

static inline void
vn_renderer_bo_flush(struct vn_renderer *renderer,
                     struct vn_renderer_bo *bo,
                     VkDeviceSize offset,
                     VkDeviceSize end)
{
   renderer->bo_ops.flush(renderer, bo, offset, end);
}

static inline void
vn_renderer_bo_invalidate(struct vn_renderer *renderer,
                          struct vn_renderer_bo *bo,
                          VkDeviceSize offset,
                          VkDeviceSize size)
{
   renderer->bo_ops.invalidate(renderer, bo, offset, size);
}

static inline VkResult
vn_renderer_sync_create_cpu(struct vn_renderer *renderer,
                            struct vn_renderer_sync **_sync)
{
   struct vn_renderer_sync *sync = renderer->ops.sync_create(renderer);
   if (!sync)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   const uint64_t initial_val = 0;
   const uint32_t flags = 0;
   VkResult result = sync->ops.init(sync, initial_val, flags);
   if (result != VK_SUCCESS) {
      sync->ops.destroy(sync);
      return result;
   }

   *_sync = sync;
   return VK_SUCCESS;
}

static inline VkResult
vn_renderer_sync_create_fence(struct vn_renderer *renderer,
                              bool signaled,
                              VkExternalFenceHandleTypeFlags external_handles,
                              struct vn_renderer_sync **_sync)
{
   struct vn_renderer_sync *sync = renderer->ops.sync_create(renderer);
   if (!sync)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   const uint64_t initial_val = signaled;
   const uint32_t flags = VN_RENDERER_SYNC_BINARY |
                          (external_handles ? VN_RENDERER_SYNC_SHAREABLE : 0);
   VkResult result = sync->ops.init(sync, initial_val, flags);
   if (result != VK_SUCCESS) {
      sync->ops.destroy(sync);
      return result;
   }

   *_sync = sync;
   return VK_SUCCESS;
}

static inline VkResult
vn_renderer_sync_create_semaphore(
   struct vn_renderer *renderer,
   VkSemaphoreType type,
   uint64_t initial_val,
   VkExternalSemaphoreHandleTypeFlags external_handles,
   struct vn_renderer_sync **_sync)
{
   struct vn_renderer_sync *sync = renderer->ops.sync_create(renderer);
   if (!sync)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   const uint32_t flags =
      (external_handles ? VN_RENDERER_SYNC_SHAREABLE : 0) |
      (type == VK_SEMAPHORE_TYPE_BINARY ? VN_RENDERER_SYNC_BINARY : 0);
   VkResult result = sync->ops.init(sync, initial_val, flags);
   if (result != VK_SUCCESS) {
      sync->ops.destroy(sync);
      return result;
   }

   *_sync = sync;
   return VK_SUCCESS;
}

static inline VkResult
vn_renderer_sync_create_empty(struct vn_renderer *renderer,
                              struct vn_renderer_sync **_sync)
{
   struct vn_renderer_sync *sync = renderer->ops.sync_create(renderer);
   if (!sync)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   /* no init */

   *_sync = sync;
   return VK_SUCCESS;
}

static inline void
vn_renderer_sync_destroy(struct vn_renderer_sync *sync)
{
   sync->ops.destroy(sync);
}

static inline VkResult
vn_renderer_sync_init_signaled(struct vn_renderer_sync *sync)
{
   const uint64_t initial_val = 1;
   const uint32_t flags = VN_RENDERER_SYNC_BINARY;
   return sync->ops.init(sync, initial_val, flags);
}

static inline VkResult
vn_renderer_sync_init_syncobj(struct vn_renderer_sync *sync,
                              int fd,
                              bool sync_file)
{
   return sync->ops.init_syncobj(sync, fd, sync_file);
}

static inline void
vn_renderer_sync_release(struct vn_renderer_sync *sync)
{
   sync->ops.release(sync);
}

static inline int
vn_renderer_sync_export_syncobj(struct vn_renderer_sync *sync, bool sync_file)
{
   return sync->ops.export_syncobj(sync, sync_file);
}

static inline VkResult
vn_renderer_sync_reset(struct vn_renderer_sync *sync, uint64_t initial_val)
{
   return sync->ops.reset(sync, initial_val);
}

static inline VkResult
vn_renderer_sync_read(struct vn_renderer_sync *sync, uint64_t *val)
{
   return sync->ops.read(sync, val);
}

static inline VkResult
vn_renderer_sync_write(struct vn_renderer_sync *sync, uint64_t val)
{
   return sync->ops.write(sync, val);
}

#endif /* VN_RENDERER_H */
