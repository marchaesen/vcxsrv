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
#include "zink_types.h"
#include "zink_batch.h"

#define VK_VIS_VRAM (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
#define VK_LAZY_VRAM (VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT | VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)


static inline enum zink_alloc_flag
zink_alloc_flags_from_heap(enum zink_heap heap)
{
   switch (heap) {
   case ZINK_HEAP_DEVICE_LOCAL_SPARSE:
      return ZINK_ALLOC_SPARSE;
      break;
   default:
      break;
   }
   return (enum zink_alloc_flag)0;
}

static inline VkMemoryPropertyFlags
vk_domain_from_heap(enum zink_heap heap)
{
   VkMemoryPropertyFlags domains = (VkMemoryPropertyFlags)0;

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

bool
zink_bo_get_kms_handle(struct zink_screen *screen, struct zink_bo *bo, int fd, uint32_t *handle);

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
zink_bo_commit(struct zink_screen *screen, struct zink_resource *res, unsigned level, struct pipe_box *box, bool commit, VkSemaphore *sem);

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
