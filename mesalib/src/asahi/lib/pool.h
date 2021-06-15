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

#ifndef __AGX_POOL_H__
#define __AGX_POOL_H__

#include <stddef.h>
#include "asahi/lib/agx_pack.h"
#include "agx_bo.h"

#include "util/u_dynarray.h"

/* Represents a pool of memory that can only grow, used to allocate objects
 * with the same lifetime as the pool itself. In OpenGL, a pool is owned by the
 * batch for transient structures. In Vulkan, it may be owned by e.g. the
 * command pool */

struct agx_pool {
   /* Parent device for allocation */
   struct agx_device *dev;

   /* BOs allocated by this pool */
   struct util_dynarray bos;

   /* Current transient BO */
   struct agx_bo *transient_bo;

   /* Within the topmost transient BO, how much has been used? */
   unsigned transient_offset;

   /* BO flags to use in the pool */
   unsigned create_flags;
};

void
agx_pool_init(struct agx_pool *pool, struct agx_device *dev,
      unsigned create_flags, bool prealloc);

void
agx_pool_cleanup(struct agx_pool *pool);

static inline unsigned
agx_pool_num_bos(struct agx_pool *pool)
{
   return util_dynarray_num_elements(&pool->bos, struct agx_bo *);
}

void
agx_pool_get_bo_handles(struct agx_pool *pool, uint32_t *handles);

/* Represents a fat pointer for GPU-mapped memory, returned from the transient
 * allocator and not used for much else */

struct agx_ptr
agx_pool_alloc_aligned(struct agx_pool *pool, size_t sz, unsigned alignment);

uint64_t
agx_pool_upload(struct agx_pool *pool, const void *data, size_t sz);

uint64_t
agx_pool_upload_aligned(struct agx_pool *pool, const void *data, size_t sz, unsigned alignment);

struct agx_desc_alloc_info {
   unsigned size;
   unsigned align;
   unsigned nelems;
};

#define AGX_DESC_ARRAY(count, name) \
        { \
                .size = MALI_ ## name ## _LENGTH, \
                .align = MALI_ ## name ## _ALIGN, \
                .nelems = count, \
        }

#define AGX_DESC(name) AGX_DESC_ARRAY(1, name)

#define AGX_DESC_AGGREGATE(...) \
        (struct agx_desc_alloc_info[]) { \
                __VA_ARGS__, \
                { 0 }, \
        }

static inline struct agx_ptr
agx_pool_alloc_descs(struct agx_pool *pool,
                          const struct agx_desc_alloc_info *descs)
{
   unsigned size = 0;
   unsigned align = descs[0].align;

   for (unsigned i = 0; descs[i].size; i++) {
      assert(!(size & (descs[i].align - 1)));
      size += descs[i].size * descs[i].nelems;
   }

   return agx_pool_alloc_aligned(pool, size, align);
}

#define agx_pool_alloc_desc(pool, name) \
        agx_pool_alloc_descs(pool, AGX_DESC_AGGREGATE(AGX_DESC(name)))

#define agx_pool_alloc_desc_array(pool, count, name) \
        agx_pool_alloc_descs(pool, AGX_DESC_AGGREGATE(AGX_DESC_ARRAY(count, name)))

#define agx_pool_alloc_desc_aggregate(pool, ...) \
        agx_pool_alloc_descs(pool, AGX_DESC_AGGREGATE(__VA_ARGS__))

#endif
