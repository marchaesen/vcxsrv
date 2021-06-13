/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#ifndef VN_QUERY_POOL_H
#define VN_QUERY_POOL_H

#include "vn_common.h"

struct vn_query_pool {
   struct vn_object_base base;

   VkAllocationCallbacks allocator;
   uint32_t result_array_size;
};
VK_DEFINE_NONDISP_HANDLE_CASTS(vn_query_pool,
                               base.base,
                               VkQueryPool,
                               VK_OBJECT_TYPE_QUERY_POOL)

#endif /* VN_QUERY_POOL_H */
