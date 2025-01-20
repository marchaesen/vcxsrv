/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "vk_log.h"

#include "pan_props.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_query_pool.h"

#define PANVK_QUERY_TIMEOUT 2000000000ull

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(CreateQueryPool)(VkDevice _device,
                                const VkQueryPoolCreateInfo *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkQueryPool *pQueryPool)
{
   VK_FROM_HANDLE(panvk_device, device, _device);

   struct panvk_query_pool *pool;

   pool =
      vk_query_pool_create(&device->vk, pCreateInfo, pAllocator, sizeof(*pool));
   if (!pool)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   uint32_t reports_per_query;
   switch (pCreateInfo->queryType) {
   case VK_QUERY_TYPE_OCCLUSION: {
      /* The counter is per core on Bifrost */
#if PAN_ARCH < 9
      const struct panvk_physical_device *phys_dev =
         to_panvk_physical_device(device->vk.physical);

      panfrost_query_core_count(&phys_dev->kmod.props, &reports_per_query);
#else
      reports_per_query = 1;
#endif
      break;
   }
   default:
      unreachable("Unsupported query type");
   }

   pool->reports_per_query = reports_per_query;
   pool->query_stride = reports_per_query * sizeof(struct panvk_query_report);

   assert(pool->vk.query_count > 0);

   struct panvk_pool_alloc_info alloc_info = {
      .size = pool->reports_per_query * sizeof(struct panvk_query_report) *
              pool->vk.query_count,
      .alignment = sizeof(struct panvk_query_report),
   };
   pool->mem = panvk_pool_alloc_mem(&device->mempools.rw, alloc_info);
   if (!pool->mem.bo) {
      vk_query_pool_destroy(&device->vk, pAllocator, &pool->vk);
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   struct panvk_pool_alloc_info syncobjs_alloc_info = {
      .size = sizeof(struct panvk_query_available_obj) * pool->vk.query_count,
      .alignment = 64,
   };
   pool->available_mem =
      panvk_pool_alloc_mem(&device->mempools.rw_nc, syncobjs_alloc_info);
   if (!pool->available_mem.bo) {
      panvk_pool_free_mem(&pool->mem);
      vk_query_pool_destroy(&device->vk, pAllocator, &pool->vk);
      return vk_error(device, VK_ERROR_OUT_OF_DEVICE_MEMORY);
   }

   *pQueryPool = panvk_query_pool_to_handle(pool);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(DestroyQueryPool)(VkDevice _device, VkQueryPool queryPool,
                                 const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);

   if (!pool)
      return;

   panvk_pool_free_mem(&pool->mem);
   panvk_pool_free_mem(&pool->available_mem);
   vk_query_pool_destroy(&device->vk, pAllocator, &pool->vk);
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(ResetQueryPool)(VkDevice device, VkQueryPool queryPool,
                               uint32_t firstQuery, uint32_t queryCount)
{
   VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);

   struct panvk_query_available_obj *available =
      panvk_query_available_host_addr(pool, firstQuery);
   memset(available, 0, queryCount * sizeof(*available));
}

static bool
panvk_query_is_available(struct panvk_query_pool *pool, uint32_t query)
{
   struct panvk_query_available_obj *available =
      panvk_query_available_host_addr(pool, query);

#if PAN_ARCH >= 10
   return p_atomic_read(&available->sync_obj.seqno) != 0;
#else
   return p_atomic_read(&available->value) != 0;
#endif
}

static VkResult
panvk_query_wait_for_available(struct panvk_device *dev,
                               struct panvk_query_pool *pool, uint32_t query)
{
   int64_t abs_timeout_ns = os_time_get_absolute_timeout(PANVK_QUERY_TIMEOUT);

   while (os_time_get_nano() < abs_timeout_ns) {
      if (panvk_query_is_available(pool, query))
         return VK_SUCCESS;

      VkResult status = vk_device_check_status(&dev->vk);
      if (status != VK_SUCCESS)
         return status;
   }

   return vk_device_set_lost(&dev->vk, "query timeout");
}

static void
cpu_write_query_result(void *dst, uint32_t idx, VkQueryResultFlags flags,
                       uint64_t result)
{
   if (flags & VK_QUERY_RESULT_64_BIT) {
      uint64_t *dst64 = dst;
      dst64[idx] = result;
   } else {
      uint32_t *dst32 = dst;
      dst32[idx] = result;
   }
}

static void
cpu_write_occlusion_query_result(void *dst, uint32_t idx,
                                 VkQueryResultFlags flags,
                                 const struct panvk_query_report *src,
                                 unsigned core_count)
{
   uint64_t result = 0;

   for (uint32_t core_idx = 0; core_idx < core_count; core_idx++)
      result += src[core_idx].value;

   cpu_write_query_result(dst, idx, flags, result);
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(GetQueryPoolResults)(VkDevice _device, VkQueryPool queryPool,
                                    uint32_t firstQuery, uint32_t queryCount,
                                    size_t dataSize, void *pData,
                                    VkDeviceSize stride,
                                    VkQueryResultFlags flags)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);

   if (vk_device_is_lost(&device->vk))
      return VK_ERROR_DEVICE_LOST;

   VkResult status = VK_SUCCESS;
   for (uint32_t i = 0; i < queryCount; i++) {
      const uint32_t query = firstQuery + i;

      bool available = panvk_query_is_available(pool, query);

      if (!available && (flags & VK_QUERY_RESULT_WAIT_BIT)) {
         status = panvk_query_wait_for_available(device, pool, query);
         if (status != VK_SUCCESS)
            return status;

         available = true;
      }

      bool write_results = available || (flags & VK_QUERY_RESULT_PARTIAL_BIT);

      const struct panvk_query_report *src =
         panvk_query_report_host_addr(pool, query);
      assert(i * stride < dataSize);
      void *dst = (char *)pData + i * stride;

      switch (pool->vk.query_type) {
      case VK_QUERY_TYPE_OCCLUSION: {
         if (write_results)
            cpu_write_occlusion_query_result(dst, 0, flags, src,
                                             pool->reports_per_query);
         break;
      }
      default:
         unreachable("Unsupported query type");
      }

      if (!write_results)
         status = VK_NOT_READY;

      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
         cpu_write_query_result(dst, 1, flags, available);
   }

   return status;
}
