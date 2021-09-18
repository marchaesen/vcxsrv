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

#include "agx_bo.h"
#include "agx_device.h"
#include "pool.h"

/* Transient command stream pooling: command stream uploads try to simply copy
 * into whereever we left off. If there isn't space, we allocate a new entry
 * into the pool and copy there */

#define POOL_SLAB_SIZE (256 * 1024)

static struct agx_bo *
agx_pool_alloc_backing(struct agx_pool *pool, size_t bo_sz)
{
   struct agx_bo *bo = agx_bo_create(pool->dev, bo_sz,
                            pool->create_flags);

   util_dynarray_append(&pool->bos, struct agx_bo *, bo);
   pool->transient_bo = bo;
   pool->transient_offset = 0;

   return bo;
}

void
agx_pool_init(struct agx_pool *pool, struct agx_device *dev,
                   unsigned create_flags, bool prealloc)
{
   memset(pool, 0, sizeof(*pool));
   pool->dev = dev;
   pool->create_flags = create_flags;
   util_dynarray_init(&pool->bos, dev->memctx);

   if (prealloc)
      agx_pool_alloc_backing(pool, POOL_SLAB_SIZE);
}

void
agx_pool_cleanup(struct agx_pool *pool)
{
   util_dynarray_foreach(&pool->bos, struct agx_bo *, bo) {
	   agx_bo_unreference(*bo);
   }

   util_dynarray_fini(&pool->bos);
}

void
agx_pool_get_bo_handles(struct agx_pool *pool, uint32_t *handles)
{
   unsigned idx = 0;
   util_dynarray_foreach(&pool->bos, struct agx_bo *, bo) {
      handles[idx++] = (*bo)->handle;
   }
}

struct agx_ptr
agx_pool_alloc_aligned(struct agx_pool *pool, size_t sz, unsigned alignment)
{
	alignment = MAX2(alignment, 4096);
   assert(alignment == util_next_power_of_two(alignment));

   /* Find or create a suitable BO */
   struct agx_bo *bo = pool->transient_bo;
   unsigned offset = ALIGN_POT(pool->transient_offset, alignment);

   /* If we don't fit, allocate a new backing */
   if (unlikely(bo == NULL || (offset + sz) >= POOL_SLAB_SIZE)) {
      bo = agx_pool_alloc_backing(pool,
            ALIGN_POT(MAX2(POOL_SLAB_SIZE, sz), 4096));
      offset = 0;
   }

   pool->transient_offset = offset + sz;

   struct agx_ptr ret = {
      .cpu = bo->ptr.cpu + offset,
      .gpu = bo->ptr.gpu + offset,
   };

   return ret;
}

uint64_t
agx_pool_upload(struct agx_pool *pool, const void *data, size_t sz)
{
   return agx_pool_upload_aligned(pool, data, sz, util_next_power_of_two(sz));
}

uint64_t
agx_pool_upload_aligned(struct agx_pool *pool, const void *data, size_t sz, unsigned alignment)
{
	alignment = MAX2(alignment, 4096);
   struct agx_ptr transfer = agx_pool_alloc_aligned(pool, sz, alignment);
   memcpy(transfer.cpu, data, sz);
   return transfer.gpu;
}
