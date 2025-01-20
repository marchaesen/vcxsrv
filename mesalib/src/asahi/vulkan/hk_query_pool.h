/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "hk_private.h"
#include "vk_query_pool.h"

struct agx_bo;

struct hk_query_pool {
   struct vk_query_pool vk;

   uint32_t query_start;
   uint32_t query_stride;

   struct agx_bo *bo;
   void *bo_map;

   /* For timestamp queries, the kernel-assigned timestamp buffer handle. Unused
    * for all other query types
    */
   uint32_t handle;

   unsigned oq_queries;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(hk_query_pool, vk.base, VkQueryPool,
                               VK_OBJECT_TYPE_QUERY_POOL)
