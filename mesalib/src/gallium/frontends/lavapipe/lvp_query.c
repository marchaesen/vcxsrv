/*
 * Copyright © 2019 Red Hat.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "lvp_private.h"
#include "pipe/p_context.h"

VkResult lvp_CreateQueryPool(
    VkDevice                                    _device,
    const VkQueryPoolCreateInfo*                pCreateInfo,
    const VkAllocationCallbacks*                pAllocator,
    VkQueryPool*                                pQueryPool)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);

   enum pipe_query_type pipeq;
   switch (pCreateInfo->queryType) {
   case VK_QUERY_TYPE_OCCLUSION:
      pipeq = PIPE_QUERY_OCCLUSION_COUNTER;
      break;
   case VK_QUERY_TYPE_TIMESTAMP:
      pipeq = PIPE_QUERY_TIMESTAMP;
      break;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      pipeq = PIPE_QUERY_PIPELINE_STATISTICS;
      break;
   default:
      return VK_ERROR_FEATURE_NOT_PRESENT;
   }
   struct lvp_query_pool *pool;
   uint32_t pool_size = sizeof(*pool) + pCreateInfo->queryCount * sizeof(struct pipe_query *);

   pool = vk_zalloc2(&device->vk.alloc, pAllocator,
                    pool_size, 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!pool)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk, &pool->base,
                       VK_OBJECT_TYPE_QUERY_POOL);
   pool->type = pCreateInfo->queryType;
   pool->count = pCreateInfo->queryCount;
   pool->base_type = pipeq;
   pool->pipeline_stats = pCreateInfo->pipelineStatistics;

   *pQueryPool = lvp_query_pool_to_handle(pool);
   return VK_SUCCESS;
}

void lvp_DestroyQueryPool(
    VkDevice                                    _device,
    VkQueryPool                                 _pool,
    const VkAllocationCallbacks*                pAllocator)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_query_pool, pool, _pool);

   if (!pool)
      return;

   for (unsigned i = 0; i < pool->count; i++)
      if (pool->queries[i])
         device->queue.ctx->destroy_query(device->queue.ctx, pool->queries[i]);
   vk_object_base_finish(&pool->base);
   vk_free2(&device->vk.alloc, pAllocator, pool);
}

VkResult lvp_GetQueryPoolResults(
   VkDevice                                    _device,
   VkQueryPool                                 queryPool,
   uint32_t                                    firstQuery,
   uint32_t                                    queryCount,
   size_t                                      dataSize,
   void*                                       pData,
   VkDeviceSize                                stride,
   VkQueryResultFlags                          flags)
{
   LVP_FROM_HANDLE(lvp_device, device, _device);
   LVP_FROM_HANDLE(lvp_query_pool, pool, queryPool);
   VkResult vk_result = VK_SUCCESS;

   lvp_DeviceWaitIdle(_device);

   for (unsigned i = firstQuery; i < firstQuery + queryCount; i++) {
      uint8_t *dptr = (uint8_t *)((char *)pData + (stride * (i - firstQuery)));
      union pipe_query_result result;
      bool ready = false;
      if (pool->queries[i]) {
        ready = device->queue.ctx->get_query_result(device->queue.ctx,
                                                    pool->queries[i],
                                                    (flags & VK_QUERY_RESULT_WAIT_BIT),
                                                    &result);
      } else {
        result.u64 = 0;
      }

      if (!ready && !(flags & VK_QUERY_RESULT_PARTIAL_BIT))
          vk_result = VK_NOT_READY;
      if (flags & VK_QUERY_RESULT_64_BIT) {
         if (ready || (flags & VK_QUERY_RESULT_PARTIAL_BIT)) {
            if (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS) {
               uint32_t mask = pool->pipeline_stats;
               uint64_t *pstats = (uint64_t *)&result.pipeline_statistics;
               while (mask) {
                  uint32_t i = u_bit_scan(&mask);

                  *(uint64_t *)dptr = pstats[i];
                  dptr += 8;
               }
            } else {
               *(uint64_t *)dptr = result.u64;
               dptr += 8;
            }
         } else
            dptr += stride;

      } else {
         if (ready || (flags & VK_QUERY_RESULT_PARTIAL_BIT)) {
            if (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS) {
               uint32_t mask = pool->pipeline_stats;
               uint64_t *pstats = (uint64_t *)&result.pipeline_statistics;
               while (mask) {
                  uint32_t i = u_bit_scan(&mask);

                  if (pstats[i] > UINT32_MAX)
                     *(uint32_t *)dptr = UINT32_MAX;
                  else
                     *(uint32_t *)dptr = pstats[i];
                  dptr += 4;
               }
            } else {
               if (result.u64 > UINT32_MAX)
                  *(uint32_t *)dptr = UINT32_MAX;
               else
                  *(uint32_t *)dptr = result.u32;
               dptr += 4;
            }
         } else
            dptr += stride;
      }

      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
        if (flags & VK_QUERY_RESULT_64_BIT)
           *(uint64_t *)dptr = ready;
        else
           *(uint32_t *)dptr = ready;
      }
   }
   return vk_result;
}
