/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "pvr_bo.h"
#include "pvr_csb.h"
#include "pvr_device_info.h"
#include "pvr_private.h"
#include "util/macros.h"
#include "vk_log.h"
#include "vk_object.h"

VkResult pvr_CreateQueryPool(VkDevice _device,
                             const VkQueryPoolCreateInfo *pCreateInfo,
                             const VkAllocationCallbacks *pAllocator,
                             VkQueryPool *pQueryPool)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   const uint32_t core_count = device->pdevice->dev_runtime_info.core_count;
   const uint32_t query_size = pCreateInfo->queryCount * sizeof(uint32_t);
   struct pvr_query_pool *pool;
   uint64_t alloc_size;
   VkResult result;

   /* Vulkan 1.0 supports only occlusion, timestamp, and pipeline statistics
    * query.
    * We don't currently support timestamp queries.
    * VkQueueFamilyProperties->timestampValidBits = 0.
    * We don't currently support pipeline statistics queries.
    * VkPhysicalDeviceFeatures->pipelineStatisticsQuery = false.
    */
   assert(!device->features.pipelineStatisticsQuery);
   assert(pCreateInfo->queryType == VK_QUERY_TYPE_OCCLUSION);

   pool = vk_object_alloc(&device->vk,
                          pAllocator,
                          sizeof(*pool),
                          VK_OBJECT_TYPE_QUERY_POOL);
   if (!pool)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   pool->result_stride =
      ALIGN_POT(query_size, PVRX(CR_ISP_OCLQRY_BASE_ADDR_ALIGNMENT));

   /* Each Phantom writes to a separate offset within the vis test heap so
    * allocate space for the total number of Phantoms.
    */
   alloc_size = pool->result_stride * core_count;

   result = pvr_bo_alloc(device,
                         device->heaps.vis_test_heap,
                         alloc_size,
                         PVRX(CR_ISP_OCLQRY_BASE_ADDR_ALIGNMENT),
                         PVR_BO_ALLOC_FLAG_CPU_MAPPED,
                         &pool->result_buffer);
   if (result != VK_SUCCESS)
      goto err_free_pool;

   result = pvr_bo_alloc(device,
                         device->heaps.vis_test_heap,
                         query_size,
                         sizeof(uint32_t),
                         PVR_BO_ALLOC_FLAG_CPU_MAPPED,
                         &pool->availability_buffer);
   if (result != VK_SUCCESS)
      goto err_free_result_buffer;

   *pQueryPool = pvr_query_pool_to_handle(pool);

   return VK_SUCCESS;

err_free_result_buffer:
   pvr_bo_free(device, pool->result_buffer);

err_free_pool:
   vk_object_free(&device->vk, pAllocator, pool);

   return result;
}

void pvr_DestroyQueryPool(VkDevice _device,
                          VkQueryPool queryPool,
                          const VkAllocationCallbacks *pAllocator)
{
   PVR_FROM_HANDLE(pvr_query_pool, pool, queryPool);
   PVR_FROM_HANDLE(pvr_device, device, _device);

   pvr_bo_free(device, pool->availability_buffer);
   pvr_bo_free(device, pool->result_buffer);

   vk_object_free(&device->vk, pAllocator, pool);
}

VkResult pvr_GetQueryPoolResults(VkDevice _device,
                                 VkQueryPool queryPool,
                                 uint32_t firstQuery,
                                 uint32_t queryCount,
                                 size_t dataSize,
                                 void *pData,
                                 VkDeviceSize stride,
                                 VkQueryResultFlags flags)
{
   assert(!"Unimplemented");
   return VK_SUCCESS;
}

void pvr_CmdResetQueryPool(VkCommandBuffer commandBuffer,
                           VkQueryPool queryPool,
                           uint32_t firstQuery,
                           uint32_t queryCount)
{
   assert(!"Unimplemented");
}

void pvr_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer,
                                 VkQueryPool queryPool,
                                 uint32_t firstQuery,
                                 uint32_t queryCount,
                                 VkBuffer dstBuffer,
                                 VkDeviceSize dstOffset,
                                 VkDeviceSize stride,
                                 VkQueryResultFlags flags)
{
   assert(!"Unimplemented");
}

void pvr_CmdBeginQuery(VkCommandBuffer commandBuffer,
                       VkQueryPool queryPool,
                       uint32_t query,
                       VkQueryControlFlags flags)
{
   assert(!"Unimplemented");
}

void pvr_CmdEndQuery(VkCommandBuffer commandBuffer,
                     VkQueryPool queryPool,
                     uint32_t query)
{
   assert(!"Unimplemented");
}
