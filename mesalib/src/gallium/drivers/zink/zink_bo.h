/*
 * Copyright © 2021 Valve Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 * 
 * Authors:
 *    Mike Blumenkrantz <michael.blumenkrantz@gmail.com>
 */

#ifndef ZINK_BO_H
#define ZINK_BO_H
#include <vulkan/vulkan.h>
#include "pipebuffer/pb_cache.h"
#include "pipebuffer/pb_slab.h"
#include "zink_batch.h"

#define VK_VIS_VRAM (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
#define VK_LAZY_VRAM (VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
enum zink_resource_access {
   ZINK_RESOURCE_ACCESS_READ = 1,
   ZINK_RESOURCE_ACCESS_WRITE = 32,
   ZINK_RESOURCE_ACCESS_RW = ZINK_RESOURCE_ACCESS_READ | ZINK_RESOURCE_ACCESS_WRITE,
};


enum zink_heap {
   ZINK_HEAP_DEVICE_LOCAL,
   ZINK_HEAP_DEVICE_LOCAL_SPARSE,
   ZINK_HEAP_DEVICE_LOCAL_LAZY,
   ZINK_HEAP_DEVICE_LOCAL_VISIBLE,
   ZINK_HEAP_HOST_VISIBLE_COHERENT,
   ZINK_HEAP_HOST_VISIBLE_CACHED,
   ZINK_HEAP_MAX,
};

enum zink_alloc_flag {
   ZINK_ALLOC_SPARSE = 1<<0,
   ZINK_ALLOC_NO_SUBALLOC = 1<<1,
};


struct zink_bo {
   struct pb_buffer base;

   union {
      struct {
         void *cpu_ptr; /* for user_ptr and permanent maps */
         int map_count;

         bool is_user_ptr;
         bool use_reusable_pool;

         /* Whether buffer_get_handle or buffer_from_handle has been called,
          * it can only transition from false to true. Protected by lock.
          */
         bool is_shared;
      } real;
      struct {
         struct pb_slab_entry entry;
         struct zink_bo *real;
      } slab;
      struct {
         uint32_t num_va_pages;
         uint32_t num_backing_pages;

         struct list_head backing;

         /* Commitment information for each page of the virtual memory area. */
         struct zink_sparse_commitment *commitments;
      } sparse;
   } u;

   VkDeviceMemory mem;
   uint64_t offset;

   uint32_t unique_id;

   simple_mtx_t lock;

   struct zink_batch_usage *reads;
   struct zink_batch_usage *writes;

   struct pb_cache_entry cache_entry[];
};

static inline struct zink_bo *
zink_bo(struct pb_buffer *pbuf)
{
   return (struct zink_bo*)pbuf;
}

static inline enum zink_alloc_flag
zink_alloc_flags_from_heap(enum zink_heap heap)
{
   enum zink_alloc_flag flags = 0;
   switch (heap) {
   case ZINK_HEAP_DEVICE_LOCAL_SPARSE:
      flags |= ZINK_ALLOC_SPARSE;
      break;
   default:
      break;
   }
   return flags;
}

static inline VkMemoryPropertyFlags
vk_domain_from_heap(enum zink_heap heap)
{
   VkMemoryPropertyFlags domains = 0;

   switch (heap) {
   case ZINK_HEAP_DEVICE_LOCAL:
   case ZINK_HEAP_DEVICE_LOCAL_SPARSE:
      domains = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      break;
   case ZINK_HEAP_DEVICE_LOCAL_LAZY:
      domains = VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      break;
   case ZINK_HEAP_DEVICE_LOCAL_VISIBLE:
      domains = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
      break;
   case ZINK_HEAP_HOST_VISIBLE_COHERENT:
      domains = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
      break;
   case ZINK_HEAP_HOST_VISIBLE_CACHED:
      domains = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
      break;
   default:
      break;
   }
   return domains;
}

static inline enum zink_heap
zink_heap_from_domain_flags(VkMemoryPropertyFlags domains, enum zink_alloc_flag flags)
{
   if (flags & ZINK_ALLOC_SPARSE)
      return ZINK_HEAP_DEVICE_LOCAL_SPARSE;

   if ((domains & VK_VIS_VRAM) == VK_VIS_VRAM)
      return ZINK_HEAP_DEVICE_LOCAL_VISIBLE;

   if (domains & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
      return ZINK_HEAP_DEVICE_LOCAL;

   if (domains & VK_MEMORY_PROPERTY_HOST_CACHED_BIT)
      return ZINK_HEAP_HOST_VISIBLE_CACHED;

   return ZINK_HEAP_HOST_VISIBLE_COHERENT;
}

bool
zink_bo_init(struct zink_screen *screen);

void
zink_bo_deinit(struct zink_screen *screen);

struct pb_buffer *
zink_bo_create(struct zink_screen *screen, uint64_t size, unsigned alignment, enum zink_heap heap, enum zink_alloc_flag flags, const void *pNext);

static inline uint64_t
zink_bo_get_offset(const struct zink_bo *bo)
{
   return bo->offset;
}

static inline VkDeviceMemory
zink_bo_get_mem(const struct zink_bo *bo)
{
   return bo->mem ? bo->mem : bo->u.slab.real->mem;
}

static inline VkDeviceSize
zink_bo_get_size(const struct zink_bo *bo)
{
   return bo->mem ? bo->base.size : bo->u.slab.real->base.size;
}

void *
zink_bo_map(struct zink_screen *screen, struct zink_bo *bo);
void
zink_bo_unmap(struct zink_screen *screen, struct zink_bo *bo);

bool
zink_bo_commit(struct zink_screen *screen, struct zink_resource *res, unsigned level, struct pipe_box *box, bool commit);

static inline bool
zink_bo_has_unflushed_usage(const struct zink_bo *bo)
{
   return zink_batch_usage_is_unflushed(bo->reads) ||
          zink_batch_usage_is_unflushed(bo->writes);
}

static inline bool
zink_bo_has_usage(const struct zink_bo *bo)
{
   return zink_batch_usage_exists(bo->reads) ||
          zink_batch_usage_exists(bo->writes);
}

static inline bool
zink_bo_usage_matches(const struct zink_bo *bo, const struct zink_batch_state *bs)
{
   return zink_batch_usage_matches(bo->reads, bs) ||
          zink_batch_usage_matches(bo->writes, bs);
}

static inline bool
zink_bo_usage_check_completion(struct zink_screen *screen, struct zink_bo *bo, enum zink_resource_access access)
{
   if (access & ZINK_RESOURCE_ACCESS_READ && !zink_screen_usage_check_completion(screen, bo->reads))
      return false;
   if (access & ZINK_RESOURCE_ACCESS_WRITE && !zink_screen_usage_check_completion(screen, bo->writes))
      return false;
   return true;
}

static inline void
zink_bo_usage_wait(struct zink_context *ctx, struct zink_bo *bo, enum zink_resource_access access)
{
   if (access & ZINK_RESOURCE_ACCESS_READ)
      zink_batch_usage_wait(ctx, bo->reads);
   if (access & ZINK_RESOURCE_ACCESS_WRITE)
      zink_batch_usage_wait(ctx, bo->writes);
}

static inline void
zink_bo_usage_set(struct zink_bo *bo, struct zink_batch_state *bs, bool write)
{
   if (write)
      zink_batch_usage_set(&bo->writes, bs);
   else
      zink_batch_usage_set(&bo->reads, bs);
}

static inline bool
zink_bo_usage_unset(struct zink_bo *bo, struct zink_batch_state *bs)
{
   zink_batch_usage_unset(&bo->reads, bs);
   zink_batch_usage_unset(&bo->writes, bs);
   return bo->reads || bo->writes;
}


static inline void
zink_bo_unref(struct zink_screen *screen, struct zink_bo *bo)
{
   struct pb_buffer *pbuf = &bo->base;
   pb_reference_with_winsys(screen, &pbuf, NULL);
}

#endif
