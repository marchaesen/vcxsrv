/*
 * Copyright 2019 Google LLC
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv and radv which are:
 * Copyright © 2015 Intel Corporation
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 */

#include "vn_query_pool.h"

#include "venus-protocol/vn_protocol_driver_query_pool.h"

#include "vn_device.h"

/* query pool commands */

VkResult
vn_CreateQueryPool(VkDevice device,
                   const VkQueryPoolCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkQueryPool *pQueryPool)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   const VkAllocationCallbacks *alloc =
      pAllocator ? pAllocator : &dev->base.base.alloc;

   struct vn_query_pool *pool =
      vk_zalloc(alloc, sizeof(*pool), VN_DEFAULT_ALIGN,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pool)
      return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vn_object_base_init(&pool->base, VK_OBJECT_TYPE_QUERY_POOL, &dev->base);

   pool->allocator = *alloc;

   switch (pCreateInfo->queryType) {
   case VK_QUERY_TYPE_OCCLUSION:
      pool->result_array_size = 1;
      break;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      pool->result_array_size =
         util_bitcount(pCreateInfo->pipelineStatistics);
      break;
   case VK_QUERY_TYPE_TIMESTAMP:
      pool->result_array_size = 1;
      break;
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      pool->result_array_size = 2;
      break;
   default:
      unreachable("bad query type");
      break;
   }

   VkQueryPool pool_handle = vn_query_pool_to_handle(pool);
   vn_async_vkCreateQueryPool(dev->instance, device, pCreateInfo, NULL,
                              &pool_handle);

   *pQueryPool = pool_handle;

   return VK_SUCCESS;
}

void
vn_DestroyQueryPool(VkDevice device,
                    VkQueryPool queryPool,
                    const VkAllocationCallbacks *pAllocator)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_query_pool *pool = vn_query_pool_from_handle(queryPool);
   const VkAllocationCallbacks *alloc;

   if (!pool)
      return;

   alloc = pAllocator ? pAllocator : &pool->allocator;

   vn_async_vkDestroyQueryPool(dev->instance, device, queryPool, NULL);

   vn_object_base_fini(&pool->base);
   vk_free(alloc, pool);
}

void
vn_ResetQueryPool(VkDevice device,
                  VkQueryPool queryPool,
                  uint32_t firstQuery,
                  uint32_t queryCount)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);

   vn_async_vkResetQueryPool(dev->instance, device, queryPool, firstQuery,
                             queryCount);
}

VkResult
vn_GetQueryPoolResults(VkDevice device,
                       VkQueryPool queryPool,
                       uint32_t firstQuery,
                       uint32_t queryCount,
                       size_t dataSize,
                       void *pData,
                       VkDeviceSize stride,
                       VkQueryResultFlags flags)
{
   VN_TRACE_FUNC();
   struct vn_device *dev = vn_device_from_handle(device);
   struct vn_query_pool *pool = vn_query_pool_from_handle(queryPool);
   const VkAllocationCallbacks *alloc = &pool->allocator;

   const size_t result_width = flags & VK_QUERY_RESULT_64_BIT ? 8 : 4;
   const size_t result_size = pool->result_array_size * result_width;
   const bool result_always_written =
      flags & (VK_QUERY_RESULT_WAIT_BIT | VK_QUERY_RESULT_PARTIAL_BIT);

   VkQueryResultFlags packed_flags = flags;
   size_t packed_stride = result_size;
   if (!result_always_written)
      packed_flags |= VK_QUERY_RESULT_WITH_AVAILABILITY_BIT;
   if (packed_flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
      packed_stride += result_width;

   const size_t packed_size = packed_stride * queryCount;
   void *packed_data;
   if (result_always_written && packed_stride == stride) {
      packed_data = pData;
   } else {
      packed_data = vk_alloc(alloc, packed_size, VN_DEFAULT_ALIGN,
                             VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!packed_data)
         return vn_error(dev->instance, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   /* TODO the renderer should transparently vkCmdCopyQueryPoolResults to a
    * coherent memory such that we can memcpy from the coherent memory to
    * avoid this serialized round trip.
    */
   VkResult result = vn_call_vkGetQueryPoolResults(
      dev->instance, device, queryPool, firstQuery, queryCount, packed_size,
      packed_data, packed_stride, packed_flags);

   if (packed_data == pData)
      return vn_result(dev->instance, result);

   const size_t copy_size =
      result_size +
      (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT ? result_width : 0);
   const void *src = packed_data;
   void *dst = pData;
   if (result == VK_SUCCESS) {
      for (uint32_t i = 0; i < queryCount; i++) {
         memcpy(dst, src, copy_size);
         src += packed_stride;
         dst += stride;
      }
   } else if (result == VK_NOT_READY) {
      assert(!result_always_written &&
             (packed_flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
      if (flags & VK_QUERY_RESULT_64_BIT) {
         for (uint32_t i = 0; i < queryCount; i++) {
            const bool avail = *(const uint64_t *)(src + result_size);
            if (avail)
               memcpy(dst, src, copy_size);
            else if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
               *(uint64_t *)(dst + result_size) = 0;

            src += packed_stride;
            dst += stride;
         }
      } else {
         for (uint32_t i = 0; i < queryCount; i++) {
            const bool avail = *(const uint32_t *)(src + result_size);
            if (avail)
               memcpy(dst, src, copy_size);
            else if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
               *(uint32_t *)(dst + result_size) = 0;

            src += packed_stride;
            dst += stride;
         }
      }
   }

   vk_free(alloc, packed_data);
   return vn_result(dev->instance, result);
}
