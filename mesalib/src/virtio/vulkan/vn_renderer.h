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

   bool has_dma_buf_import;
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

   VkResult (*create_from_dma_buf)(struct vn_renderer *renderer,
                                   VkDeviceSize size,
                                   int fd,
                                   VkMemoryPropertyFlags flags,
                                   struct vn_renderer_bo **out_bo);

   bool (*destroy)(struct vn_renderer *renderer, struct vn_renderer_bo *bo);

   int (*export_dma_buf)(struct vn_renderer *renderer,
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

enum vn_renderer_sync_flags {
   VN_RENDERER_SYNC_SHAREABLE = 1u << 0,
   VN_RENDERER_SYNC_BINARY = 1u << 1,
};

struct vn_renderer_sync_ops {
   VkResult (*create)(struct vn_renderer *renderer,
                      uint64_t initial_val,
                      uint32_t flags,
                      struct vn_renderer_sync **out_sync);

   VkResult (*create_from_syncobj)(struct vn_renderer *renderer,
                                   int fd,
                                   bool sync_file,
                                   struct vn_renderer_sync **out_sync);
   void (*destroy)(struct vn_renderer *renderer,
                   struct vn_renderer_sync *sync);

   int (*export_syncobj)(struct vn_renderer *renderer,
                         struct vn_renderer_sync *sync,
                         bool sync_file);

   /* reset the counter */
   VkResult (*reset)(struct vn_renderer *renderer,
                     struct vn_renderer_sync *sync,
                     uint64_t initial_val);

   /* read the current value from the counter */
   VkResult (*read)(struct vn_renderer *renderer,
                    struct vn_renderer_sync *sync,
                    uint64_t *val);

   /* write a new value (larger than the current one) to the counter */
   VkResult (*write)(struct vn_renderer *renderer,
                     struct vn_renderer_sync *sync,
                     uint64_t val);
};

struct vn_renderer {
   struct vn_renderer_ops ops;
   struct vn_renderer_shmem_ops shmem_ops;
   struct vn_renderer_bo_ops bo_ops;
   struct vn_renderer_sync_ops sync_ops;
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
   ASSERTED const int old =
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
vn_renderer_bo_create_from_dma_buf(struct vn_renderer *renderer,
                                   VkDeviceSize size,
                                   int fd,
                                   VkMemoryPropertyFlags flags,
                                   struct vn_renderer_bo **out_bo)
{
   struct vn_renderer_bo *bo;
   VkResult result =
      renderer->bo_ops.create_from_dma_buf(renderer, size, fd, flags, &bo);
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
   ASSERTED const int old =
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
vn_renderer_bo_export_dma_buf(struct vn_renderer *renderer,
                              struct vn_renderer_bo *bo)
{
   return renderer->bo_ops.export_dma_buf(renderer, bo);
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
vn_renderer_sync_create(struct vn_renderer *renderer,
                        uint64_t initial_val,
                        uint32_t flags,
                        struct vn_renderer_sync **out_sync)
{
   return renderer->sync_ops.create(renderer, initial_val, flags, out_sync);
}

static inline VkResult
vn_renderer_sync_create_from_syncobj(struct vn_renderer *renderer,
                                     int fd,
                                     bool sync_file,
                                     struct vn_renderer_sync **out_sync)
{
   return renderer->sync_ops.create_from_syncobj(renderer, fd, sync_file,
                                                 out_sync);
}

static inline void
vn_renderer_sync_destroy(struct vn_renderer *renderer,
                         struct vn_renderer_sync *sync)
{
   renderer->sync_ops.destroy(renderer, sync);
}

static inline int
vn_renderer_sync_export_syncobj(struct vn_renderer *renderer,
                                struct vn_renderer_sync *sync,
                                bool sync_file)
{
   return renderer->sync_ops.export_syncobj(renderer, sync, sync_file);
}

static inline VkResult
vn_renderer_sync_reset(struct vn_renderer *renderer,
                       struct vn_renderer_sync *sync,
                       uint64_t initial_val)
{
   return renderer->sync_ops.reset(renderer, sync, initial_val);
}

static inline VkResult
vn_renderer_sync_read(struct vn_renderer *renderer,
                      struct vn_renderer_sync *sync,
                      uint64_t *val)
{
   return renderer->sync_ops.read(renderer, sync, val);
}

static inline VkResult
vn_renderer_sync_write(struct vn_renderer *renderer,
                       struct vn_renderer_sync *sync,
                       uint64_t val)
{
   return renderer->sync_ops.write(renderer, sync, val);
}

static inline VkResult
vn_renderer_submit_simple_sync(struct vn_renderer *renderer,
                               const void *cs_data,
                               size_t cs_size)
{
   struct vn_renderer_sync *sync;
   VkResult result =
      vn_renderer_sync_create(renderer, 0, VN_RENDERER_SYNC_BINARY, &sync);
   if (result != VK_SUCCESS)
      return result;

   const struct vn_renderer_submit submit = {
      .batches =
         &(const struct vn_renderer_submit_batch){
            .cs_data = cs_data,
            .cs_size = cs_size,
            .sync_queue_cpu = true,
            .syncs = &sync,
            .sync_values = &(const uint64_t){ 1 },
            .sync_count = 1,
         },
      .batch_count = 1,
   };
   const struct vn_renderer_wait wait = {
      .timeout = UINT64_MAX,
      .syncs = &sync,
      .sync_values = &(const uint64_t){ 1 },
      .sync_count = 1,
   };

   result = vn_renderer_submit(renderer, &submit);
   if (result == VK_SUCCESS)
      result = vn_renderer_wait(renderer, &wait);

   vn_renderer_sync_destroy(renderer, sync);

   return result;
}

#endif /* VN_RENDERER_H */
