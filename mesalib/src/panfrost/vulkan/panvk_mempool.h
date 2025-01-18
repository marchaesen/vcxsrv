/*
 * © Copyright 2017-2018 Alyssa Rosenzweig
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

#ifndef __PANVK_POOL_H__
#define __PANVK_POOL_H__

#include "panvk_priv_bo.h"

#include "pan_pool.h"

#include "util/list.h"
#include "util/simple_mtx.h"

struct panvk_bo_pool {
   struct list_head free_bos;
};

static inline void
panvk_bo_pool_init(struct panvk_bo_pool *bo_pool)
{
   list_inithead(&bo_pool->free_bos);
}

void panvk_bo_pool_cleanup(struct panvk_bo_pool *bo_pool);

struct panvk_pool_properties {
   /* BO flags to use in the pool */
   unsigned create_flags;

   /* Allocation granularity. */
   size_t slab_size;

   /* Label for created BOs */
   const char *label;

   /* When false, BOs allocated by the pool are not retained by the pool
    * when they leave the transient_bo field. */
   bool owns_bos;

   /* If pool is shared and not externally protected, this should be true. */
   bool needs_locking;

   bool prealloc;
};

/* Represents grow-only memory. It may be owned by the batch (OpenGL), or may
   be unowned for persistent uploads. */

struct panvk_pool {
   /* Inherit from pan_pool */
   struct pan_pool base;

   /* Parent device for allocation */
   struct panvk_device *dev;

   /* Pool properties. */
   struct panvk_pool_properties props;

   /* Before allocating a new BO, check if the BO pool has free BOs.
    * When returning BOs, if bo_pool != NULL, return them to this bo_pool.
    */
   struct panvk_bo_pool *bo_pool;

   /* BOs allocated by this pool */
   struct list_head bos;
   struct list_head big_bos;
   unsigned bo_count;

   /* Lock used to protect allocation when the pool is shared. */
   simple_mtx_t lock;

   /* Current transient BO */
   struct panvk_priv_bo *transient_bo;

   /* Within the topmost transient BO, how much has been used? */
   unsigned transient_offset;
};

static inline struct panvk_pool *
to_panvk_pool(struct pan_pool *pool)
{
   return container_of(pool, struct panvk_pool, base);
}

void panvk_pool_init(struct panvk_pool *pool, struct panvk_device *dev,
                     struct panvk_bo_pool *bo_pool,
                     const struct panvk_pool_properties *props);

void panvk_pool_reset(struct panvk_pool *pool);

void panvk_pool_cleanup(struct panvk_pool *pool);

static inline unsigned
panvk_pool_num_bos(struct panvk_pool *pool)
{
   return pool->bo_count;
}

void panvk_pool_get_bo_handles(struct panvk_pool *pool, uint32_t *handles);

enum panvk_priv_mem_flags {
   PANVK_PRIV_MEM_OWNED_BY_POOL = BITFIELD_BIT(0),
};

struct panvk_priv_mem {
   uintptr_t bo;
   unsigned offset;
};

static struct panvk_priv_bo *
panvk_priv_mem_bo(struct panvk_priv_mem mem)
{
   return (void *)(mem.bo & ~7ull);
}

static uint32_t
panvk_priv_mem_flags(struct panvk_priv_mem mem)
{
   return mem.bo & 7ull;
}

static inline uint64_t
panvk_priv_mem_dev_addr(struct panvk_priv_mem mem)
{
   struct panvk_priv_bo *bo = panvk_priv_mem_bo(mem);

   return bo ? bo->addr.dev + mem.offset : 0;
}

static inline void *
panvk_priv_mem_host_addr(struct panvk_priv_mem mem)
{
   struct panvk_priv_bo *bo = panvk_priv_mem_bo(mem);

   return bo && bo->addr.host ? (uint8_t *)bo->addr.host + mem.offset : NULL;
}

struct panvk_pool_alloc_info {
   size_t size;
   unsigned alignment;
};

static inline struct panvk_pool_alloc_info
panvk_pool_descs_to_alloc_info(const struct pan_desc_alloc_info *descs)
{
   struct panvk_pool_alloc_info alloc_info = {
      .alignment = descs[0].align,
   };

   for (unsigned i = 0; descs[i].size; i++)
      alloc_info.size += descs[i].size * descs[i].nelems;

   return alloc_info;
}

struct panvk_priv_mem panvk_pool_alloc_mem(struct panvk_pool *pool,
                                           struct panvk_pool_alloc_info info);

static inline void
panvk_pool_free_mem(struct panvk_priv_mem *mem)
{
   struct panvk_priv_bo *bo = panvk_priv_mem_bo(*mem);
   uint32_t flags = panvk_priv_mem_flags(*mem);

   if (bo) {
      if (likely(!(flags & PANVK_PRIV_MEM_OWNED_BY_POOL)))
         panvk_priv_bo_unref(bo);

      memset(mem, 0, sizeof(*mem));
   }
}

static inline struct panvk_priv_mem
panvk_pool_upload_aligned(struct panvk_pool *pool, const void *data, size_t sz,
                          unsigned alignment)
{
   struct panvk_pool_alloc_info info = {
      .size = sz,
      .alignment = alignment,
   };

   struct panvk_priv_mem mem = panvk_pool_alloc_mem(pool, info);
   memcpy(panvk_priv_mem_host_addr(mem), data, sz);
   return mem;
}

static inline struct panvk_priv_mem
panvk_pool_upload(struct panvk_pool *pool, const void *data, size_t sz)
{
   return panvk_pool_upload_aligned(pool, data, sz, sz);
}

#define panvk_pool_alloc_desc(pool, name)                                      \
   panvk_pool_alloc_mem(pool, panvk_pool_descs_to_alloc_info(                  \
                                 PAN_DESC_AGGREGATE(PAN_DESC(name))))

#define panvk_pool_alloc_desc_array(pool, count, name)                         \
   panvk_pool_alloc_mem(pool,                                                  \
                        panvk_pool_descs_to_alloc_info(                        \
                           PAN_DESC_AGGREGATE(PAN_DESC_ARRAY(count, name))))

#define panvk_pool_alloc_desc_aggregate(pool, ...)                             \
   panvk_pool_alloc_mem(                                                       \
      pool, panvk_pool_descs_to_alloc_info(PAN_DESC_AGGREGATE(__VA_ARGS__)))

#endif
