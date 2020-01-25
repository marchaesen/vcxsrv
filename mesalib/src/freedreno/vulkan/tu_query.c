/*
 * Copyrigh 2016 Red Hat Inc.
 * Based on anv:
 * Copyright © 2015 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "tu_private.h"

#include <assert.h>
#include <fcntl.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "registers/adreno_pm4.xml.h"
#include "registers/adreno_common.xml.h"
#include "registers/a6xx.xml.h"

#include "nir/nir_builder.h"
#include "util/os_time.h"

#include "tu_cs.h"

#define NSEC_PER_SEC 1000000000ull
#define WAIT_TIMEOUT 5

/* It seems like sample counts need to be copied over to 16-byte aligned
 * memory. */
struct PACKED slot_value {
   uint64_t value;
   uint64_t __padding;
};

struct PACKED occlusion_query_slot {
   struct slot_value available; /* 0 when unavailable, 1 when available */
   struct slot_value begin;
   struct slot_value end;
   struct slot_value result;
};

/* Returns the IOVA of a given uint64_t field in a given slot of a query
 * pool. */
#define query_iova(type, pool, query, field)                         \
   pool->bo.iova + pool->stride * query + offsetof(type, field) +    \
         offsetof(struct slot_value, value)

#define occlusion_query_iova(pool, query, field)                     \
   query_iova(struct occlusion_query_slot, pool, query, field)

#define query_is_available(type, slot)                               \
   ((type*)slot)->available.value

#define occlusion_query_is_available(slot)                           \
   query_is_available(struct occlusion_query_slot, slot)

/*
 * Returns a pointer to a given slot in a query pool.
 */
static void* slot_address(struct tu_query_pool *pool, uint32_t query)
{
   return (char*)pool->bo.map + query * pool->stride;
}

VkResult
tu_CreateQueryPool(VkDevice _device,
                   const VkQueryPoolCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkQueryPool *pQueryPool)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO);
   assert(pCreateInfo->queryCount > 0);

   uint32_t slot_size;
   switch (pCreateInfo->queryType) {
   case VK_QUERY_TYPE_OCCLUSION:
      slot_size = sizeof(struct occlusion_query_slot);
      break;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
   case VK_QUERY_TYPE_TIMESTAMP:
      unreachable("Unimplemented query type");
   default:
      assert(!"Invalid query type");
   }

   struct tu_query_pool *pool =
      vk_alloc2(&device->alloc, pAllocator, sizeof(*pool), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (!pool)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = tu_bo_init_new(device, &pool->bo,
         pCreateInfo->queryCount * slot_size);
   if (result != VK_SUCCESS) {
      vk_free2(&device->alloc, pAllocator, pool);
      return result;
   }

   result = tu_bo_map(device, &pool->bo);
   if (result != VK_SUCCESS) {
      tu_bo_finish(device, &pool->bo);
      vk_free2(&device->alloc, pAllocator, pool);
      return result;
   }

   /* Initialize all query statuses to unavailable */
   memset(pool->bo.map, 0, pool->bo.size);

   pool->type = pCreateInfo->queryType;
   pool->stride = slot_size;
   pool->size = pCreateInfo->queryCount;
   pool->pipeline_statistics = pCreateInfo->pipelineStatistics;
   *pQueryPool = tu_query_pool_to_handle(pool);

   return VK_SUCCESS;
}

void
tu_DestroyQueryPool(VkDevice _device,
                    VkQueryPool _pool,
                    const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_query_pool, pool, _pool);

   if (!pool)
      return;

   tu_bo_finish(device, &pool->bo);
   vk_free2(&device->alloc, pAllocator, pool);
}

/* Wait on the the availability status of a query up until a timeout. */
static VkResult
wait_for_available(struct tu_device *device, struct tu_query_pool *pool,
                   uint32_t query)
{
   /* TODO: Use the MSM_IOVA_WAIT ioctl to wait on the available bit in a
    * scheduler friendly way instead of busy polling once the patch has landed
    * upstream. */
   struct occlusion_query_slot *slot = slot_address(pool, query);
   uint64_t abs_timeout = os_time_get_absolute_timeout(
         WAIT_TIMEOUT * NSEC_PER_SEC);
   while(os_time_get_nano() < abs_timeout) {
      if (occlusion_query_is_available(slot))
         return VK_SUCCESS;
   }
   return vk_error(device->instance, VK_TIMEOUT);
}

static VkResult
get_occlusion_query_pool_results(struct tu_device *device,
                                 struct tu_query_pool *pool,
                                 uint32_t firstQuery,
                                 uint32_t queryCount,
                                 size_t dataSize,
                                 void *pData,
                                 VkDeviceSize stride,
                                 VkQueryResultFlags flags)
{
   assert(dataSize >= stride * queryCount);

   char *query_result = pData;
   VkResult result = VK_SUCCESS;
   for (uint32_t i = 0; i < queryCount; i++) {
      uint32_t query = firstQuery + i;
      struct occlusion_query_slot *slot = slot_address(pool, query);
      bool available = occlusion_query_is_available(slot);
      if ((flags & VK_QUERY_RESULT_WAIT_BIT) && !available) {
         VkResult wait_result = wait_for_available(device, pool, query);
         if (wait_result != VK_SUCCESS)
            return wait_result;
         available = true;
      } else if (!(flags & VK_QUERY_RESULT_PARTIAL_BIT) && !available) {
         /* From the Vulkan 1.1.130 spec:
          *
          *    If VK_QUERY_RESULT_WAIT_BIT and VK_QUERY_RESULT_PARTIAL_BIT are
          *    both not set then no result values are written to pData for
          *    queries that are in the unavailable state at the time of the
          *    call, and vkGetQueryPoolResults returns VK_NOT_READY. However,
          *    availability state is still written to pData for those queries
          *    if VK_QUERY_RESULT_WITH_AVAILABILITY_BIT is set.
          */
         result = VK_NOT_READY;
         if (!(flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)) {
            query_result += stride;
            continue;
         }
      }

      uint64_t value;
      if (available) {
         value = slot->result.value;
      } else if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
         /* From the Vulkan 1.1.130 spec:
          *
          *    If VK_QUERY_RESULT_WITH_AVAILABILITY_BIT is set, the final
          *    integer value written for each query is non-zero if the query’s
          *    status was available or zero if the status was unavailable.
          */
         value = 0;
      } else if (flags & VK_QUERY_RESULT_PARTIAL_BIT) {
          /* From the Vulkan 1.1.130 spec:
           *
           *   If VK_QUERY_RESULT_PARTIAL_BIT is set, VK_QUERY_RESULT_WAIT_BIT
           *   is not set, and the query’s status is unavailable, an
           *   intermediate result value between zero and the final result
           *   value is written to pData for that query.
           *
           * Just return 0 here for simplicity since it's a valid result.
           */
         value = 0;
      }

      if (flags & VK_QUERY_RESULT_64_BIT) {
         *(uint64_t*)query_result = value;
      } else {
         *(uint32_t*)query_result = value;
      }
      query_result += stride;
   }
   return result;
}

VkResult
tu_GetQueryPoolResults(VkDevice _device,
                       VkQueryPool queryPool,
                       uint32_t firstQuery,
                       uint32_t queryCount,
                       size_t dataSize,
                       void *pData,
                       VkDeviceSize stride,
                       VkQueryResultFlags flags)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_query_pool, pool, queryPool);
   assert(firstQuery + queryCount <= pool->size);

   switch (pool->type) {
   case VK_QUERY_TYPE_OCCLUSION: {
      return get_occlusion_query_pool_results(device, pool, firstQuery,
            queryCount, dataSize, pData, stride, flags);
   }
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
   case VK_QUERY_TYPE_TIMESTAMP:
      unreachable("Unimplemented query type");
   default:
      assert(!"Invalid query type");
   }
   return VK_SUCCESS;
}

static void
emit_copy_occlusion_query_pool_results(struct tu_cmd_buffer *cmdbuf,
                                       struct tu_cs *cs,
                                       struct tu_query_pool *pool,
                                       uint32_t firstQuery,
                                       uint32_t queryCount,
                                       struct tu_buffer *buffer,
                                       VkDeviceSize dstOffset,
                                       VkDeviceSize stride,
                                       VkQueryResultFlags flags)
{
   /* From the Vulkan 1.1.130 spec:
    *
    *    vkCmdCopyQueryPoolResults is guaranteed to see the effect of previous
    *    uses of vkCmdResetQueryPool in the same queue, without any additional
    *    synchronization.
    *
    * To ensure that previous writes to the available bit are coherent, first
    * wait for all writes to complete.
    */
   tu_cs_reserve_space(cmdbuf->device, cs, 1);
   tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);

   for (uint32_t i = 0; i < queryCount; i++) {
      uint32_t query = firstQuery + i;
      uint64_t available_iova = occlusion_query_iova(pool, query, available);
      uint64_t result_iova = occlusion_query_iova(pool, query, result);
      uint64_t buffer_iova = tu_buffer_iova(buffer) + dstOffset + i * stride;
      /* Wait for the available bit to be set if executed with the
       * VK_QUERY_RESULT_WAIT_BIT flag. */
      if (flags & VK_QUERY_RESULT_WAIT_BIT) {
         tu_cs_reserve_space(cmdbuf->device, cs, 7);
         tu_cs_emit_pkt7(cs, CP_WAIT_REG_MEM, 6);
         tu_cs_emit(cs, CP_WAIT_REG_MEM_0_FUNCTION(WRITE_EQ) |
                        CP_WAIT_REG_MEM_0_POLL_MEMORY);
         tu_cs_emit_qw(cs, available_iova);
         tu_cs_emit(cs, CP_WAIT_REG_MEM_3_REF(0x1));
         tu_cs_emit(cs, CP_WAIT_REG_MEM_4_MASK(~0));
         tu_cs_emit(cs, CP_WAIT_REG_MEM_5_DELAY_LOOP_CYCLES(16));
      }

      /* If the query result is available, conditionally emit a packet to copy
       * the result (bo->result) into the buffer.
       *
       * NOTE: For the conditional packet to be executed, CP_COND_EXEC tests
       * that ADDR0 != 0 and ADDR1 < REF. The packet here simply tests that
       * 0 < available < 2, aka available == 1.
       */
      tu_cs_reserve_space(cmdbuf->device, cs, 13);
      tu_cs_emit_pkt7(cs, CP_COND_EXEC, 6);
      tu_cs_emit_qw(cs, available_iova);
      tu_cs_emit_qw(cs, available_iova);
      tu_cs_emit(cs, CP_COND_EXEC_4_REF(0x2));
      tu_cs_emit(cs, 6); /* Conditionally execute the next 6 DWORDS */

      /* Start of conditional execution */
      tu_cs_emit_pkt7(cs, CP_MEM_TO_MEM, 5);
      uint32_t mem_to_mem_flags = flags & VK_QUERY_RESULT_64_BIT ?
            CP_MEM_TO_MEM_0_DOUBLE : 0;
      tu_cs_emit(cs, mem_to_mem_flags);
      tu_cs_emit_qw(cs, buffer_iova);
      tu_cs_emit_qw(cs, result_iova);
      /* End of conditional execution */

      /* Like in the case of vkGetQueryPoolResults, copying the results of an
       * unavailable query with the VK_QUERY_RESULT_WITH_AVAILABILITY_BIT or
       * VK_QUERY_RESULT_PARTIAL_BIT flags will return 0. */
      if (flags & (VK_QUERY_RESULT_WITH_AVAILABILITY_BIT |
                   VK_QUERY_RESULT_PARTIAL_BIT)) {
         if (flags & VK_QUERY_RESULT_64_BIT) {
            tu_cs_reserve_space(cmdbuf->device, cs, 10);
            tu_cs_emit_pkt7(cs, CP_COND_WRITE5, 9);
         } else {
            tu_cs_reserve_space(cmdbuf->device, cs, 9);
            tu_cs_emit_pkt7(cs, CP_COND_WRITE5, 8);
         }
         tu_cs_emit(cs, CP_COND_WRITE5_0_FUNCTION(WRITE_EQ) |
                        CP_COND_WRITE5_0_POLL_MEMORY |
                        CP_COND_WRITE5_0_WRITE_MEMORY);
         tu_cs_emit_qw(cs, available_iova);
         tu_cs_emit(cs, CP_COND_WRITE5_3_REF(0));
         tu_cs_emit(cs, CP_COND_WRITE5_4_MASK(~0));
         tu_cs_emit_qw(cs, buffer_iova);
         if (flags & VK_QUERY_RESULT_64_BIT) {
            tu_cs_emit_qw(cs, 0);
         } else {
            tu_cs_emit(cs, 0);
         }
      }
   }

   tu_bo_list_add(&cmdbuf->bo_list, buffer->bo, MSM_SUBMIT_BO_WRITE);
}

void
tu_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer,
                           VkQueryPool queryPool,
                           uint32_t firstQuery,
                           uint32_t queryCount,
                           VkBuffer dstBuffer,
                           VkDeviceSize dstOffset,
                           VkDeviceSize stride,
                           VkQueryResultFlags flags)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   TU_FROM_HANDLE(tu_query_pool, pool, queryPool);
   TU_FROM_HANDLE(tu_buffer, buffer, dstBuffer);
   struct tu_cs *cs = &cmdbuf->cs;
   assert(firstQuery + queryCount <= pool->size);

   switch (pool->type) {
   case VK_QUERY_TYPE_OCCLUSION: {
      return emit_copy_occlusion_query_pool_results(cmdbuf, cs, pool,
            firstQuery, queryCount, buffer, dstOffset, stride, flags);
   }
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
   case VK_QUERY_TYPE_TIMESTAMP:
      unreachable("Unimplemented query type");
   default:
      assert(!"Invalid query type");
   }
}

static void
emit_reset_occlusion_query_pool(struct tu_cmd_buffer *cmdbuf,
                                struct tu_query_pool *pool,
                                uint32_t firstQuery,
                                uint32_t queryCount)
{
   struct tu_cs *cs = &cmdbuf->cs;

   for (uint32_t i = 0; i < queryCount; i++) {
      uint32_t query = firstQuery + i;
      uint64_t available_iova = occlusion_query_iova(pool, query, available);
      uint64_t result_iova = occlusion_query_iova(pool, query, result);
      tu_cs_reserve_space(cmdbuf->device, cs, 11);
      tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 4);
      tu_cs_emit_qw(cs, available_iova);
      tu_cs_emit_qw(cs, 0x0);

      tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 4);
      tu_cs_emit_qw(cs, result_iova);
      tu_cs_emit_qw(cs, 0x0);
   }
}

void
tu_CmdResetQueryPool(VkCommandBuffer commandBuffer,
                     VkQueryPool queryPool,
                     uint32_t firstQuery,
                     uint32_t queryCount)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   TU_FROM_HANDLE(tu_query_pool, pool, queryPool);

   switch (pool->type) {
   case VK_QUERY_TYPE_OCCLUSION:
      emit_reset_occlusion_query_pool(cmdbuf, pool, firstQuery, queryCount);
      break;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
   case VK_QUERY_TYPE_TIMESTAMP:
      unreachable("Unimplemented query type");
   default:
      assert(!"Invalid query type");
   }

   tu_bo_list_add(&cmdbuf->bo_list, &pool->bo, MSM_SUBMIT_BO_WRITE);
}

static void
emit_begin_occlusion_query(struct tu_cmd_buffer *cmdbuf,
                           struct tu_query_pool *pool,
                           uint32_t query)
{
   /* From the Vulkan 1.1.130 spec:
    *
    *    A query must begin and end inside the same subpass of a render pass
    *    instance, or must both begin and end outside of a render pass
    *    instance.
    *
    * Unlike on an immediate-mode renderer, Turnip renders all tiles on
    * vkCmdEndRenderPass, not individually on each vkCmdDraw*. As such, if a
    * query begins/ends inside the same subpass of a render pass, we need to
    * record the packets on the secondary draw command stream. cmdbuf->draw_cs
    * is then run on every tile during render, so we just need to accumulate
    * sample counts in slot->result to compute the query result.
    */
   struct tu_cs *cs = cmdbuf->state.pass ? &cmdbuf->draw_cs : &cmdbuf->cs;

   uint64_t begin_iova = occlusion_query_iova(pool, query, begin);

   tu_cs_reserve_space(cmdbuf->device, cs, 7);
   tu_cs_emit_regs(cs,
                   A6XX_RB_SAMPLE_COUNT_CONTROL(.copy = true));

   tu_cs_emit_regs(cs,
                   A6XX_RB_SAMPLE_COUNT_ADDR_LO(begin_iova));

   tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 1);
   tu_cs_emit(cs, ZPASS_DONE);
}

void
tu_CmdBeginQuery(VkCommandBuffer commandBuffer,
                 VkQueryPool queryPool,
                 uint32_t query,
                 VkQueryControlFlags flags)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   TU_FROM_HANDLE(tu_query_pool, pool, queryPool);
   assert(query < pool->size);

   switch (pool->type) {
   case VK_QUERY_TYPE_OCCLUSION:
      /* In freedreno, there is no implementation difference between
       * GL_SAMPLES_PASSED and GL_ANY_SAMPLES_PASSED, so we can similarly
       * ignore the VK_QUERY_CONTROL_PRECISE_BIT flag here.
       */
      emit_begin_occlusion_query(cmdbuf, pool, query);
      break;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
   case VK_QUERY_TYPE_TIMESTAMP:
      unreachable("Unimplemented query type");
   default:
      assert(!"Invalid query type");
   }

   tu_bo_list_add(&cmdbuf->bo_list, &pool->bo, MSM_SUBMIT_BO_WRITE);
}

static void
emit_end_occlusion_query(struct tu_cmd_buffer *cmdbuf,
                         struct tu_query_pool *pool,
                         uint32_t query)
{
   /* Ending an occlusion query happens in a few steps:
    *    1) Set the slot->end to UINT64_MAX.
    *    2) Set up the SAMPLE_COUNT registers and trigger a CP_EVENT_WRITE to
    *       write the current sample count value into slot->end.
    *    3) Since (2) is asynchronous, wait until slot->end is not equal to
    *       UINT64_MAX before continuing via CP_WAIT_REG_MEM.
    *    4) Accumulate the results of the query (slot->end - slot->begin) into
    *       slot->result.
    *    5) If vkCmdEndQuery is *not* called from within the scope of a render
    *       pass, set the slot's available bit since the query is now done.
    *    6) If vkCmdEndQuery *is* called from within the scope of a render
    *       pass, we cannot mark as available yet since the commands in
    *       draw_cs are not run until vkCmdEndRenderPass.
    */
   const struct tu_render_pass *pass = cmdbuf->state.pass;
   struct tu_cs *cs = pass ? &cmdbuf->draw_cs : &cmdbuf->cs;

   uint64_t available_iova = occlusion_query_iova(pool, query, available);
   uint64_t begin_iova = occlusion_query_iova(pool, query, begin);
   uint64_t end_iova = occlusion_query_iova(pool, query, end);
   uint64_t result_iova = occlusion_query_iova(pool, query, result);
   tu_cs_reserve_space(cmdbuf->device, cs, 31);
   tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 4);
   tu_cs_emit_qw(cs, end_iova);
   tu_cs_emit_qw(cs, 0xffffffffffffffffull);

   tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);

   tu_cs_emit_regs(cs,
                   A6XX_RB_SAMPLE_COUNT_CONTROL(.copy = true));

   tu_cs_emit_regs(cs,
                   A6XX_RB_SAMPLE_COUNT_ADDR_LO(end_iova));

   tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 1);
   tu_cs_emit(cs, ZPASS_DONE);

   tu_cs_emit_pkt7(cs, CP_WAIT_REG_MEM, 6);
   tu_cs_emit(cs, CP_WAIT_REG_MEM_0_FUNCTION(WRITE_NE) |
                  CP_WAIT_REG_MEM_0_POLL_MEMORY);
   tu_cs_emit_qw(cs, end_iova);
   tu_cs_emit(cs, CP_WAIT_REG_MEM_3_REF(0xffffffff));
   tu_cs_emit(cs, CP_WAIT_REG_MEM_4_MASK(~0));
   tu_cs_emit(cs, CP_WAIT_REG_MEM_5_DELAY_LOOP_CYCLES(16));

   /* result (dst) = result (srcA) + end (srcB) - begin (srcC) */
   tu_cs_emit_pkt7(cs, CP_MEM_TO_MEM, 9);
   tu_cs_emit(cs, CP_MEM_TO_MEM_0_DOUBLE | CP_MEM_TO_MEM_0_NEG_C);
   tu_cs_emit_qw(cs, result_iova);
   tu_cs_emit_qw(cs, result_iova);
   tu_cs_emit_qw(cs, end_iova);
   tu_cs_emit_qw(cs, begin_iova);

   tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);

   if (pass)
      /* Technically, queries should be tracked per-subpass, but here we track
       * at the render pass level to simply the code a bit. This is safe
       * because the only commands that use the available bit are
       * vkCmdCopyQueryPoolResults and vkCmdResetQueryPool, both of which
       * cannot be invoked from inside a render pass scope.
       */
      cs = &cmdbuf->draw_epilogue_cs;

   tu_cs_reserve_space(cmdbuf->device, cs, 5);
   tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 4);
   tu_cs_emit_qw(cs, available_iova);
   tu_cs_emit_qw(cs, 0x1);
}

void
tu_CmdEndQuery(VkCommandBuffer commandBuffer,
               VkQueryPool queryPool,
               uint32_t query)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   TU_FROM_HANDLE(tu_query_pool, pool, queryPool);
   assert(query < pool->size);

   switch (pool->type) {
   case VK_QUERY_TYPE_OCCLUSION:
      emit_end_occlusion_query(cmdbuf, pool, query);
      break;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
   case VK_QUERY_TYPE_TIMESTAMP:
      unreachable("Unimplemented query type");
   default:
      assert(!"Invalid query type");
   }

   tu_bo_list_add(&cmdbuf->bo_list, &pool->bo, MSM_SUBMIT_BO_WRITE);
}

void
tu_CmdWriteTimestamp(VkCommandBuffer commandBuffer,
                     VkPipelineStageFlagBits pipelineStage,
                     VkQueryPool queryPool,
                     uint32_t query)
{
}
