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

#include "pan_bo.h"
#include "pan_pool.h"

/* Transient command stream pooling: command stream uploads try to simply copy
 * into whereever we left off. If there isn't space, we allocate a new entry
 * into the pool and copy there */

static struct panfrost_bo *
panfrost_pool_alloc_backing(struct pan_pool *pool, size_t bo_sz)
{
        /* We don't know what the BO will be used for, so let's flag it
         * RW and attach it to both the fragment and vertex/tiler jobs.
         * TODO: if we want fine grained BO assignment we should pass
         * flags to this function and keep the read/write,
         * fragment/vertex+tiler pools separate.
         */
        struct panfrost_bo *bo = panfrost_bo_create(pool->dev, bo_sz,
                        pool->create_flags);

        util_dynarray_append(&pool->bos, struct panfrost_bo *, bo);
        pool->transient_bo = bo;
        pool->transient_offset = 0;

        return bo;
}

void
panfrost_pool_init(struct pan_pool *pool, void *memctx,
                   struct panfrost_device *dev,
                   unsigned create_flags, bool prealloc)
{
        memset(pool, 0, sizeof(*pool));
        pool->dev = dev;
        pool->create_flags = create_flags;
        util_dynarray_init(&pool->bos, memctx);

        if (prealloc)
                panfrost_pool_alloc_backing(pool, TRANSIENT_SLAB_SIZE);
}

void
panfrost_pool_cleanup(struct pan_pool *pool)
{
        util_dynarray_foreach(&pool->bos, struct panfrost_bo *, bo)
                panfrost_bo_unreference(*bo);

        util_dynarray_fini(&pool->bos);
}

void
panfrost_pool_get_bo_handles(struct pan_pool *pool, uint32_t *handles)
{
        unsigned idx = 0;
        util_dynarray_foreach(&pool->bos, struct panfrost_bo *, bo) {
                assert((*bo)->gem_handle > 0);
                handles[idx++] = (*bo)->gem_handle;

               /* Update the BO access flags so that panfrost_bo_wait() knows
                * about all pending accesses.
                * We only keep the READ/WRITE info since this is all the BO
                * wait logic cares about.
                * We also preserve existing flags as this batch might not
                * be the first one to access the BO.
                */
                (*bo)->gpu_access |= PAN_BO_ACCESS_RW;
        }
}

struct panfrost_ptr
panfrost_pool_alloc_aligned(struct pan_pool *pool, size_t sz, unsigned alignment)
{
        assert(alignment == util_next_power_of_two(alignment));

        /* Find or create a suitable BO */
        struct panfrost_bo *bo = pool->transient_bo;
        unsigned offset = ALIGN_POT(pool->transient_offset, alignment);

        /* If we don't fit, allocate a new backing */
        if (unlikely(bo == NULL || (offset + sz) >= TRANSIENT_SLAB_SIZE)) {
                bo = panfrost_pool_alloc_backing(pool,
                                ALIGN_POT(MAX2(TRANSIENT_SLAB_SIZE, sz), 4096));
                offset = 0;
        }

        pool->transient_offset = offset + sz;

        struct panfrost_ptr ret = {
                .cpu = bo->ptr.cpu + offset,
                .gpu = bo->ptr.gpu + offset,
        };

        return ret;
}

mali_ptr
panfrost_pool_upload(struct pan_pool *pool, const void *data, size_t sz)
{
        return panfrost_pool_upload_aligned(pool, data, sz, sz);
}

mali_ptr
panfrost_pool_upload_aligned(struct pan_pool *pool, const void *data, size_t sz, unsigned alignment)
{
        struct panfrost_ptr transfer = panfrost_pool_alloc_aligned(pool, sz, alignment);
        memcpy(transfer.cpu, data, sz);
        return transfer.gpu;
}
