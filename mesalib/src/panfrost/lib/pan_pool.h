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

#ifndef __PAN_POOL_H__
#define __PAN_POOL_H__

#include <stddef.h>
#include <midgard_pack.h>
#include "pan_bo.h"

#include "util/u_dynarray.h"

/* Represents a pool of memory that can only grow, used to allocate objects
 * with the same lifetime as the pool itself. In OpenGL, a pool is owned by the
 * batch for transient structures. In Vulkan, it may be owned by e.g. the
 * command pool */

struct pan_pool {
        /* Parent device for allocation */
        struct panfrost_device *dev;

        /* BOs allocated by this pool */
        struct util_dynarray bos;

        /* Current transient BO */
        struct panfrost_bo *transient_bo;

        /* Within the topmost transient BO, how much has been used? */
        unsigned transient_offset;

        /* BO flags to use in the pool */
        unsigned create_flags;
};

void
panfrost_pool_init(struct pan_pool *pool, void *memctx,
                   struct panfrost_device *dev, unsigned create_flags,
                   bool prealloc);

void
panfrost_pool_cleanup(struct pan_pool *pool);

static inline unsigned
panfrost_pool_num_bos(struct pan_pool *pool)
{
        return util_dynarray_num_elements(&pool->bos, struct panfrost_bo *);
}

void
panfrost_pool_get_bo_handles(struct pan_pool *pool, uint32_t *handles);

/* Represents a fat pointer for GPU-mapped memory, returned from the transient
 * allocator and not used for much else */

struct panfrost_ptr
panfrost_pool_alloc_aligned(struct pan_pool *pool, size_t sz, unsigned alignment);

/* Default to self-alignment */

static inline struct panfrost_ptr
panfrost_pool_alloc(struct pan_pool *pool, size_t sz)
{
        assert(sz == util_next_power_of_two(sz));
        return panfrost_pool_alloc_aligned(pool, sz, sz);
}

struct panfrost_ptr
panfrost_pool_alloc(struct pan_pool *pool, size_t sz);

mali_ptr
panfrost_pool_upload(struct pan_pool *pool, const void *data, size_t sz);

mali_ptr
panfrost_pool_upload_aligned(struct pan_pool *pool, const void *data, size_t sz, unsigned alignment);

#endif
