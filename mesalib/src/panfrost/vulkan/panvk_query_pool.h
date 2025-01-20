/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_QUERY_POOL_H
#define PANVK_QUERY_POOL_H

#ifndef PAN_ARCH
#error "PAN_ARCH must be defined"
#endif

#include <stdint.h>

#include "panvk_mempool.h"
#include "vk_query_pool.h"

#if PAN_ARCH >= 10
#include "panvk_cmd_buffer.h"
#endif

struct panvk_query_report {
   uint64_t value;
};

struct panvk_query_available_obj {
#if PAN_ARCH >= 10
   struct panvk_cs_sync32 sync_obj;
#else
   uint32_t value;
#endif
};

static_assert(sizeof(struct panvk_query_report) == 8,
              "panvk_query_report size is expected to be 8");

struct panvk_query_pool {
   struct vk_query_pool vk;

   uint32_t query_stride;
   uint32_t reports_per_query;

   struct panvk_priv_mem mem;
   struct panvk_priv_mem available_mem;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(panvk_query_pool, vk.base, VkQueryPool,
                               VK_OBJECT_TYPE_QUERY_POOL)

static uint64_t
panvk_query_available_dev_addr(struct panvk_query_pool *pool, uint32_t query)
{
   assert(query < pool->vk.query_count);
   return panvk_priv_mem_dev_addr(pool->available_mem) + query * sizeof(struct panvk_query_available_obj);
}

static struct panvk_query_available_obj *
panvk_query_available_host_addr(struct panvk_query_pool *pool, uint32_t query)
{
   assert(query < pool->vk.query_count);
   return (struct panvk_query_available_obj *)panvk_priv_mem_host_addr(pool->available_mem) + query;
}

static uint64_t
panvk_query_offset(struct panvk_query_pool *pool, uint32_t query)
{
   assert(query < pool->vk.query_count);
   return query * (uint64_t)pool->query_stride;
}

static uint64_t
panvk_query_report_dev_addr(struct panvk_query_pool *pool, uint32_t query)
{
   return panvk_priv_mem_dev_addr(pool->mem) + panvk_query_offset(pool, query);
}

static struct panvk_query_report *
panvk_query_report_host_addr(struct panvk_query_pool *pool, uint32_t query)
{
   return (void *)((char *)panvk_priv_mem_host_addr(pool->mem) +
                   panvk_query_offset(pool, query));
}

#endif
