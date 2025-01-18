/*
 * © Copyright 2018 Alyssa Rosenzweig
 * Copyright (C) 2019 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "panvk_mempool.h"
#include "panvk_priv_bo.h"

#include "kmod/pan_kmod.h"

void
panvk_bo_pool_cleanup(struct panvk_bo_pool *bo_pool)
{
   list_for_each_entry_safe(struct panvk_priv_bo, bo, &bo_pool->free_bos,
                            node) {
      list_del(&bo->node);
      panvk_priv_bo_unref(bo);
   }
}

/* Knockoff u_upload_mgr. Uploads wherever we left off, allocating new entries
 * when needed.
 *
 * In "owned" mode, a single parent owns the entire pool, and the pool owns all
 * created BOs. All BOs are tracked and addable as
 * panvk_pool_get_bo_handles. Freeing occurs at the level of an entire pool.
 * This is useful for streaming uploads, where the batch owns the pool.
 *
 * In "unowned" mode, the pool is freestanding. It does not track created BOs
 * or hold references. Instead, the consumer must manage the created BOs. This
 * is more flexible, enabling non-transient CSO state or shader code to be
 * packed with conservative lifetime handling.
 */

static struct panvk_priv_bo *
panvk_pool_alloc_backing(struct panvk_pool *pool, size_t sz)
{
   size_t bo_sz = ALIGN_POT(MAX2(pool->base.slab_size, sz), 4096);
   struct panvk_priv_bo *bo = NULL;

   /* If there's a free BO in our BO pool, let's pick it. */
   if (pool->bo_pool && bo_sz == pool->base.slab_size &&
       !list_is_empty(&pool->bo_pool->free_bos)) {
      bo =
         list_first_entry(&pool->bo_pool->free_bos, struct panvk_priv_bo, node);
      list_del(&bo->node);
   } else {
      /* We don't know what the BO will be used for, so let's flag it
       * RW and attach it to both the fragment and vertex/tiler jobs.
       * TODO: if we want fine grained BO assignment we should pass
       * flags to this function and keep the read/write,
       * fragment/vertex+tiler pools separate.
       */
      VkResult result =
         panvk_priv_bo_create(pool->dev, bo_sz, pool->props.create_flags,
                              VK_SYSTEM_ALLOCATION_SCOPE_DEVICE, &bo);

      /* Pool allocations are indirect, meaning there's no VkResult returned
       * and no way for the caller to know why the device memory allocation
       * failed. We want to propagate host allocation failures, so set
       * errno to -ENOMEM if panvk_priv_bo_create() returns
       * VK_ERROR_OUT_OF_HOST_MEMORY.
       * We expect the caller to check the returned pointer and catch the
       * host allocation failure with a call to panvk_error(). */
      if (result == VK_ERROR_OUT_OF_HOST_MEMORY)
         errno = -ENOMEM;
   }

   if (bo == NULL)
      return NULL;

   if (pool->props.owns_bos) {
      if (pan_kmod_bo_size(bo->bo) == pool->base.slab_size)
         list_addtail(&bo->node, &pool->bos);
      else
         list_addtail(&bo->node, &pool->big_bos);
      pool->bo_count++;
   }

   size_t new_remaining_size = pan_kmod_bo_size(bo->bo) - sz;
   size_t prev_remaining_size =
      pool->transient_bo
         ? pan_kmod_bo_size(pool->transient_bo->bo) - pool->transient_offset
         : 0;

   /* If there's less room in the new BO after the allocation, we stick to the
    * previous one. We also don't hold on BOs that are bigger than the pool
    * allocation granularity, to avoid memory fragmentation (retaining a big
    * BO which has just one tiny allocation active is not great). */
   if (prev_remaining_size < new_remaining_size &&
       (pool->props.owns_bos || bo_sz <= pool->base.slab_size)) {
      if (!pool->props.owns_bos)
         panvk_priv_bo_unref(pool->transient_bo);

      pool->transient_bo = bo;
      pool->transient_offset = 0;
   }

   return bo;
}

struct panvk_priv_mem
panvk_pool_alloc_mem(struct panvk_pool *pool, struct panvk_pool_alloc_info info)
{
   assert(info.alignment == util_next_power_of_two(info.alignment));

   if (pool->props.needs_locking)
      simple_mtx_lock(&pool->lock);

   /* Find or create a suitable BO */
   struct panvk_priv_bo *bo = pool->transient_bo;
   unsigned offset = ALIGN_POT(pool->transient_offset, info.alignment);

   /* If we don't fit, allocate a new backing */
   if (unlikely(bo == NULL || (offset + info.size) >= pool->base.slab_size)) {
      bo = panvk_pool_alloc_backing(pool, info.size);
      offset = 0;
   }

   if (bo != NULL && pool->transient_bo == bo) {
      pool->transient_offset = offset + info.size;
      if (!pool->props.owns_bos)
         panvk_priv_bo_ref(bo);
   }

   uint32_t flags = 0;

   if (pool->props.owns_bos)
      flags |= PANVK_PRIV_MEM_OWNED_BY_POOL;

   assert(!((uintptr_t)bo & 7));
   assert(!(flags & ~7));

   struct panvk_priv_mem ret = {
      .bo = (uintptr_t)bo | flags,
      .offset = offset,
   };

   if (pool->props.needs_locking)
      simple_mtx_unlock(&pool->lock);

   return ret;
}

static struct panfrost_ptr
panvk_pool_alloc_aligned(struct panvk_pool *pool, size_t sz, unsigned alignment)
{
   /* We just return the host/dev address, so callers can't
    * release the BO ref they acquired. */
   assert(pool->props.owns_bos);

   struct panvk_pool_alloc_info info = {
      .size = sz,
      .alignment = alignment,
   };
   struct panvk_priv_mem mem = panvk_pool_alloc_mem(pool, info);

   return (struct panfrost_ptr){
      .cpu = panvk_priv_mem_host_addr(mem),
      .gpu = panvk_priv_mem_dev_addr(mem),
   };
}
PAN_POOL_ALLOCATOR(struct panvk_pool, panvk_pool_alloc_aligned)

void
panvk_pool_init(struct panvk_pool *pool, struct panvk_device *dev,
                struct panvk_bo_pool *bo_pool,
                const struct panvk_pool_properties *props)
{
   memset(pool, 0, sizeof(*pool));
   pool->props = *props;
   simple_mtx_init(&pool->lock, mtx_plain);
   pan_pool_init(&pool->base, pool->props.slab_size);
   pool->dev = dev;
   pool->bo_pool = bo_pool;

   list_inithead(&pool->bos);
   list_inithead(&pool->big_bos);

   if (props->prealloc)
      panvk_pool_alloc_backing(pool, pool->base.slab_size);
}

void
panvk_pool_reset(struct panvk_pool *pool)
{
   if (pool->bo_pool) {
      list_splicetail(&pool->bos, &pool->bo_pool->free_bos);
      list_inithead(&pool->bos);
   } else {
      list_for_each_entry_safe(struct panvk_priv_bo, bo, &pool->bos, node) {
         list_del(&bo->node);
         panvk_priv_bo_unref(bo);
      }
   }

   list_for_each_entry_safe(struct panvk_priv_bo, bo, &pool->big_bos, node) {
      list_del(&bo->node);
      panvk_priv_bo_unref(bo);
   }

   if (!pool->props.owns_bos)
      panvk_priv_bo_unref(pool->transient_bo);

   pool->bo_count = 0;
   pool->transient_bo = NULL;
}

void
panvk_pool_cleanup(struct panvk_pool *pool)
{
   panvk_pool_reset(pool);
}

void
panvk_pool_get_bo_handles(struct panvk_pool *pool, uint32_t *handles)
{
   unsigned idx = 0;

   list_for_each_entry(struct panvk_priv_bo, bo, &pool->bos, node)
      handles[idx++] = pan_kmod_bo_handle(bo->bo);

   list_for_each_entry(struct panvk_priv_bo, bo, &pool->big_bos, node)
      handles[idx++] = pan_kmod_bo_handle(bo->bo);
}
