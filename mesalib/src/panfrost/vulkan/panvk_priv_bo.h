/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_PRIV_BO_H
#define PANVK_PRIV_BO_H

#include <vulkan/vulkan_core.h>

#include "util/list.h"
#include "util/u_atomic.h"

#include "panfrost-job.h"

struct panvk_kmod_bo;

/* Used for internal object allocation. */
struct panvk_priv_bo {
   struct list_head node;
   uint64_t refcnt;
   struct panvk_device *dev;
   struct pan_kmod_bo *bo;
   struct {
      mali_ptr dev;
      void *host;
   } addr;
};

VkResult panvk_priv_bo_create(struct panvk_device *dev, size_t size,
                              uint32_t flags, VkSystemAllocationScope scope,
                              struct panvk_priv_bo **out);

static inline struct panvk_priv_bo *
panvk_priv_bo_ref(struct panvk_priv_bo *bo)
{
   assert(p_atomic_read(&bo->refcnt) > 0);
   p_atomic_inc(&bo->refcnt);
   return bo;
}

void panvk_priv_bo_unref(struct panvk_priv_bo *bo);

#endif
