/*
 * Copyright 2017-2018 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 *
 */

#pragma once

#include <stddef.h>
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

   /* Label for pool allocations */
   const char *label;
};

void agx_pool_init(struct agx_pool *pool, struct agx_device *dev,
                   const char *label, unsigned create_flags, bool prealloc);

void agx_pool_cleanup(struct agx_pool *pool);

struct agx_ptr agx_pool_alloc_aligned_with_bo(struct agx_pool *pool, size_t sz,
                                              unsigned alignment,
                                              struct agx_bo **bo);

static inline struct agx_ptr
agx_pool_alloc_aligned(struct agx_pool *pool, size_t sz, unsigned alignment)
{
   return agx_pool_alloc_aligned_with_bo(pool, sz, alignment, NULL);
}

uint64_t agx_pool_upload(struct agx_pool *pool, const void *data, size_t sz);

uint64_t agx_pool_upload_aligned_with_bo(struct agx_pool *pool,
                                         const void *data, size_t sz,
                                         unsigned alignment,
                                         struct agx_bo **bo);

static inline uint64_t
agx_pool_upload_aligned(struct agx_pool *pool, const void *data, size_t sz,
                        unsigned alignment)
{
   return agx_pool_upload_aligned_with_bo(pool, data, sz, alignment, NULL);
}
