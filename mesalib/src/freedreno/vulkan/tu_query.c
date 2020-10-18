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

#include "adreno_pm4.xml.h"
#include "adreno_common.xml.h"
#include "a6xx.xml.h"

#include "nir/nir_builder.h"
#include "util/os_time.h"

#include "tu_cs.h"

#define NSEC_PER_SEC 1000000000ull
#define WAIT_TIMEOUT 5
#define STAT_COUNT ((REG_A6XX_RBBM_PRIMCTR_10_LO - REG_A6XX_RBBM_PRIMCTR_0_LO) / 2 + 1)

struct PACKED query_slot {
   uint64_t available;
};

struct PACKED occlusion_slot_value {
   /* Seems sample counters are placed to be 16-byte aligned
    * even though this query needs an 8-byte slot. */
   uint64_t value;
   uint64_t _padding;
};

struct PACKED occlusion_query_slot {
   struct query_slot common;
   uint64_t result;

   struct occlusion_slot_value begin;
   struct occlusion_slot_value end;
};

struct PACKED timestamp_query_slot {
   struct query_slot common;
   uint64_t result;
};

struct PACKED primitive_slot_value {
   uint64_t values[2];
};

struct PACKED pipeline_stat_query_slot {
   struct query_slot common;
   uint64_t results[STAT_COUNT];

   uint64_t begin[STAT_COUNT];
   uint64_t end[STAT_COUNT];
};

struct PACKED primitive_query_slot {
   struct query_slot common;
   /* The result of transform feedback queries is two integer values:
    *   results[0] is the count of primitives written,
    *   results[1] is the count of primitives generated.
    * Also a result for each stream is stored at 4 slots respectively.
    */
   uint64_t results[2];

   /* Primitive counters also need to be 16-byte aligned. */
   uint64_t _padding;

   struct primitive_slot_value begin[4];
   struct primitive_slot_value end[4];
};

/* Returns the IOVA of a given uint64_t field in a given slot of a query
 * pool. */
#define query_iova(type, pool, query, field)                         \
   pool->bo.iova + pool->stride * (query) + offsetof(type, field)

#define occlusion_query_iova(pool, query, field)                     \
   query_iova(struct occlusion_query_slot, pool, query, field)

#define pipeline_stat_query_iova(pool, query, field)                 \
   pool->bo.iova + pool->stride * query +                            \
   offsetof(struct pipeline_stat_query_slot, field)

#define primitive_query_iova(pool, query, field, i)                  \
   query_iova(struct primitive_query_slot, pool, query, field) +     \
   offsetof(struct primitive_slot_value, values[i])

#define query_available_iova(pool, query)                            \
   query_iova(struct query_slot, pool, query, available)

#define query_result_iova(pool, query, i)                            \
   pool->bo.iova + pool->stride * (query) +                          \
   sizeof(struct query_slot) + sizeof(uint64_t) * i

#define query_result_addr(pool, query, i)                            \
   pool->bo.map + pool->stride * query +                             \
   sizeof(struct query_slot) + sizeof(uint64_t) * i

#define query_is_available(slot) slot->available

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
   case VK_QUERY_TYPE_TIMESTAMP:
      slot_size = sizeof(struct timestamp_query_slot);
      break;
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      slot_size = sizeof(struct primitive_query_slot);
      break;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      slot_size = sizeof(struct pipeline_stat_query_slot);
      break;
   default:
      unreachable("Invalid query type");
   }

   struct tu_query_pool *pool =
         vk_object_alloc(&device->vk, pAllocator, sizeof(*pool),
                         VK_OBJECT_TYPE_QUERY_POOL);
   if (!pool)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = tu_bo_init_new(device, &pool->bo,
         pCreateInfo->queryCount * slot_size, false);
   if (result != VK_SUCCESS) {
      vk_object_free(&device->vk, pAllocator, pool);
      return result;
   }

   result = tu_bo_map(device, &pool->bo);
   if (result != VK_SUCCESS) {
      tu_bo_finish(device, &pool->bo);
      vk_object_free(&device->vk, pAllocator, pool);
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
   vk_object_free(&device->vk, pAllocator, pool);
}

static uint32_t
get_result_count(struct tu_query_pool *pool)
{
   switch (pool->type) {
   /* Occulusion and timestamp queries write one integer value */
   case VK_QUERY_TYPE_OCCLUSION:
   case VK_QUERY_TYPE_TIMESTAMP:
      return 1;
   /* Transform feedback queries write two integer values */
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      return 2;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      return util_bitcount(pool->pipeline_statistics);
   default:
      assert(!"Invalid query type");
      return 0;
   }
}

static uint32_t
statistics_index(uint32_t *statistics)
{
   uint32_t stat;
   stat = u_bit_scan(statistics);

   switch (1 << stat) {
   case VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_VERTICES_BIT:
   case VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT:
      return 0;
   case VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT:
      return 1;
   case VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_CONTROL_SHADER_PATCHES_BIT:
      return 2;
   case VK_QUERY_PIPELINE_STATISTIC_TESSELLATION_EVALUATION_SHADER_INVOCATIONS_BIT:
      return 4;
   case VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT:
      return 5;
   case VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT:
      return 6;
   case VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT:
      return 7;
   case VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT:
      return 8;
   case VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT:
      return 9;
   case VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT:
      return 10;
   default:
      return 0;
   }
}

/* Wait on the the availability status of a query up until a timeout. */
static VkResult
wait_for_available(struct tu_device *device, struct tu_query_pool *pool,
                   uint32_t query)
{
   /* TODO: Use the MSM_IOVA_WAIT ioctl to wait on the available bit in a
    * scheduler friendly way instead of busy polling once the patch has landed
    * upstream. */
   struct query_slot *slot = slot_address(pool, query);
   uint64_t abs_timeout = os_time_get_absolute_timeout(
         WAIT_TIMEOUT * NSEC_PER_SEC);
   while(os_time_get_nano() < abs_timeout) {
      if (query_is_available(slot))
         return VK_SUCCESS;
   }
   return vk_error(device->instance, VK_TIMEOUT);
}

/* Writes a query value to a buffer from the CPU. */
static void
write_query_value_cpu(char* base,
                      uint32_t offset,
                      uint64_t value,
                      VkQueryResultFlags flags)
{
   if (flags & VK_QUERY_RESULT_64_BIT) {
      *(uint64_t*)(base + (offset * sizeof(uint64_t))) = value;
   } else {
      *(uint32_t*)(base + (offset * sizeof(uint32_t))) = value;
   }
}

static VkResult
get_query_pool_results(struct tu_device *device,
                       struct tu_query_pool *pool,
                       uint32_t firstQuery,
                       uint32_t queryCount,
                       size_t dataSize,
                       void *pData,
                       VkDeviceSize stride,
                       VkQueryResultFlags flags)
{
   assert(dataSize >= stride * queryCount);

   char *result_base = pData;
   VkResult result = VK_SUCCESS;
   for (uint32_t i = 0; i < queryCount; i++) {
      uint32_t query = firstQuery + i;
      struct query_slot *slot = slot_address(pool, query);
      bool available = query_is_available(slot);
      uint32_t result_count = get_result_count(pool);
      uint32_t statistics = pool->pipeline_statistics;

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
            result_base += stride;
            continue;
         }
      }

      for (uint32_t k = 0; k < result_count; k++) {
         if (available) {
            uint64_t *result;

            if (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS) {
               uint32_t stat_idx = statistics_index(&statistics);
               result = query_result_addr(pool, query, stat_idx);
            } else {
               result = query_result_addr(pool, query, k);
            }

            write_query_value_cpu(result_base, k, *result, flags);
         } else if (flags & VK_QUERY_RESULT_PARTIAL_BIT)
             /* From the Vulkan 1.1.130 spec:
              *
              *   If VK_QUERY_RESULT_PARTIAL_BIT is set, VK_QUERY_RESULT_WAIT_BIT
              *   is not set, and the query’s status is unavailable, an
              *   intermediate result value between zero and the final result
              *   value is written to pData for that query.
              *
              * Just return 0 here for simplicity since it's a valid result.
              */
            write_query_value_cpu(result_base, k, 0, flags);
      }

      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
         /* From the Vulkan 1.1.130 spec:
          *
          *    If VK_QUERY_RESULT_WITH_AVAILABILITY_BIT is set, the final
          *    integer value written for each query is non-zero if the query’s
          *    status was available or zero if the status was unavailable.
          */
         write_query_value_cpu(result_base, result_count, available, flags);

      result_base += stride;
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

   if (tu_device_is_lost(device))
      return VK_ERROR_DEVICE_LOST;

   switch (pool->type) {
   case VK_QUERY_TYPE_OCCLUSION:
   case VK_QUERY_TYPE_TIMESTAMP:
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      return get_query_pool_results(device, pool, firstQuery, queryCount,
                                    dataSize, pData, stride, flags);
   default:
      assert(!"Invalid query type");
   }
   return VK_SUCCESS;
}

/* Copies a query value from one buffer to another from the GPU. */
static void
copy_query_value_gpu(struct tu_cmd_buffer *cmdbuf,
                     struct tu_cs *cs,
                     uint64_t src_iova,
                     uint64_t base_write_iova,
                     uint32_t offset,
                     VkQueryResultFlags flags) {
   uint32_t element_size = flags & VK_QUERY_RESULT_64_BIT ?
         sizeof(uint64_t) : sizeof(uint32_t);
   uint64_t write_iova = base_write_iova + (offset * element_size);

   tu_cs_emit_pkt7(cs, CP_MEM_TO_MEM, 5);
   uint32_t mem_to_mem_flags = flags & VK_QUERY_RESULT_64_BIT ?
         CP_MEM_TO_MEM_0_DOUBLE : 0;
   tu_cs_emit(cs, mem_to_mem_flags);
   tu_cs_emit_qw(cs, write_iova);
   tu_cs_emit_qw(cs, src_iova);
}

static void
emit_copy_query_pool_results(struct tu_cmd_buffer *cmdbuf,
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
   tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);

   for (uint32_t i = 0; i < queryCount; i++) {
      uint32_t query = firstQuery + i;
      uint64_t available_iova = query_available_iova(pool, query);
      uint64_t buffer_iova = tu_buffer_iova(buffer) + dstOffset + i * stride;
      uint32_t result_count = get_result_count(pool);
      uint32_t statistics = pool->pipeline_statistics;

      /* Wait for the available bit to be set if executed with the
       * VK_QUERY_RESULT_WAIT_BIT flag. */
      if (flags & VK_QUERY_RESULT_WAIT_BIT) {
         tu_cs_emit_pkt7(cs, CP_WAIT_REG_MEM, 6);
         tu_cs_emit(cs, CP_WAIT_REG_MEM_0_FUNCTION(WRITE_EQ) |
                        CP_WAIT_REG_MEM_0_POLL_MEMORY);
         tu_cs_emit_qw(cs, available_iova);
         tu_cs_emit(cs, CP_WAIT_REG_MEM_3_REF(0x1));
         tu_cs_emit(cs, CP_WAIT_REG_MEM_4_MASK(~0));
         tu_cs_emit(cs, CP_WAIT_REG_MEM_5_DELAY_LOOP_CYCLES(16));
      }

      for (uint32_t k = 0; k < result_count; k++) {
         uint64_t result_iova;

         if (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS) {
            uint32_t stat_idx = statistics_index(&statistics);
            result_iova = query_result_iova(pool, query, stat_idx);
         } else {
            result_iova = query_result_iova(pool, query, k);
         }

         if (flags & VK_QUERY_RESULT_PARTIAL_BIT) {
            /* Unconditionally copying the bo->result into the buffer here is
             * valid because we only set bo->result on vkCmdEndQuery. Thus, even
             * if the query is unavailable, this will copy the correct partial
             * value of 0.
             */
            copy_query_value_gpu(cmdbuf, cs, result_iova, buffer_iova,
                                 k /* offset */, flags);
         } else {
            /* Conditionally copy bo->result into the buffer based on whether the
             * query is available.
             *
             * NOTE: For the conditional packets to be executed, CP_COND_EXEC
             * tests that ADDR0 != 0 and ADDR1 < REF. The packet here simply tests
             * that 0 < available < 2, aka available == 1.
             */
            tu_cs_reserve(cs, 7 + 6);
            tu_cs_emit_pkt7(cs, CP_COND_EXEC, 6);
            tu_cs_emit_qw(cs, available_iova);
            tu_cs_emit_qw(cs, available_iova);
            tu_cs_emit(cs, CP_COND_EXEC_4_REF(0x2));
            tu_cs_emit(cs, 6); /* Cond execute the next 6 DWORDS */

            /* Start of conditional execution */
            copy_query_value_gpu(cmdbuf, cs, result_iova, buffer_iova,
                              k /* offset */, flags);
            /* End of conditional execution */
         }
      }

      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
         copy_query_value_gpu(cmdbuf, cs, available_iova, buffer_iova,
                              result_count /* offset */, flags);
      }
   }
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
   case VK_QUERY_TYPE_OCCLUSION:
   case VK_QUERY_TYPE_TIMESTAMP:
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      return emit_copy_query_pool_results(cmdbuf, cs, pool, firstQuery,
               queryCount, buffer, dstOffset, stride, flags);
   default:
      assert(!"Invalid query type");
   }
}

static void
emit_reset_query_pool(struct tu_cmd_buffer *cmdbuf,
                      struct tu_query_pool *pool,
                      uint32_t firstQuery,
                      uint32_t queryCount)
{
   struct tu_cs *cs = &cmdbuf->cs;

   for (uint32_t i = 0; i < queryCount; i++) {
      uint32_t query = firstQuery + i;
      uint32_t statistics = pool->pipeline_statistics;

      tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 4);
      tu_cs_emit_qw(cs, query_available_iova(pool, query));
      tu_cs_emit_qw(cs, 0x0);

      for (uint32_t k = 0; k < get_result_count(pool); k++) {
         uint64_t result_iova;

         if (pool->type == VK_QUERY_TYPE_PIPELINE_STATISTICS) {
            uint32_t stat_idx = statistics_index(&statistics);
            result_iova = query_result_iova(pool, query, stat_idx);
         } else {
            result_iova = query_result_iova(pool, query, k);
         }

         tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 4);
         tu_cs_emit_qw(cs, result_iova);
         tu_cs_emit_qw(cs, 0x0);
      }
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
   case VK_QUERY_TYPE_TIMESTAMP:
   case VK_QUERY_TYPE_OCCLUSION:
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      emit_reset_query_pool(cmdbuf, pool, firstQuery, queryCount);
      break;
   default:
      assert(!"Invalid query type");
   }
}

void
tu_ResetQueryPool(VkDevice device,
                  VkQueryPool queryPool,
                  uint32_t firstQuery,
                  uint32_t queryCount)
{
   TU_FROM_HANDLE(tu_query_pool, pool, queryPool);

   for (uint32_t i = 0; i < queryCount; i++) {
      struct query_slot *slot = slot_address(pool, i + firstQuery);
      slot->available = 0;

      for (uint32_t k = 0; k < get_result_count(pool); k++) {
         uint64_t *res = query_result_addr(pool, i + firstQuery, k);
         *res = 0;
      }
   }
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

   tu_cs_emit_regs(cs,
                   A6XX_RB_SAMPLE_COUNT_CONTROL(.copy = true));

   tu_cs_emit_regs(cs,
                   A6XX_RB_SAMPLE_COUNT_ADDR(.qword = begin_iova));

   tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 1);
   tu_cs_emit(cs, ZPASS_DONE);
}

static void
emit_begin_stat_query(struct tu_cmd_buffer *cmdbuf,
                      struct tu_query_pool *pool,
                      uint32_t query)
{
   struct tu_cs *cs = cmdbuf->state.pass ? &cmdbuf->draw_cs : &cmdbuf->cs;
   uint64_t begin_iova = pipeline_stat_query_iova(pool, query, begin);

   tu6_emit_event_write(cmdbuf, cs, START_PRIMITIVE_CTRS);
   tu6_emit_event_write(cmdbuf, cs, RST_PIX_CNT);
   tu6_emit_event_write(cmdbuf, cs, TILE_FLUSH);

   tu_cs_emit_wfi(cs);

   tu_cs_emit_pkt7(cs, CP_REG_TO_MEM, 3);
   tu_cs_emit(cs, CP_REG_TO_MEM_0_REG(REG_A6XX_RBBM_PRIMCTR_0_LO) |
                  CP_REG_TO_MEM_0_CNT(STAT_COUNT * 2) |
                  CP_REG_TO_MEM_0_64B);
   tu_cs_emit_qw(cs, begin_iova);
}

static void
emit_begin_xfb_query(struct tu_cmd_buffer *cmdbuf,
                     struct tu_query_pool *pool,
                     uint32_t query,
                     uint32_t stream_id)
{
   struct tu_cs *cs = cmdbuf->state.pass ? &cmdbuf->draw_cs : &cmdbuf->cs;
   uint64_t begin_iova = primitive_query_iova(pool, query, begin[0], 0);

   tu_cs_emit_regs(cs, A6XX_VPC_SO_STREAM_COUNTS(.qword = begin_iova));
   tu6_emit_event_write(cmdbuf, cs, WRITE_PRIMITIVE_COUNTS);
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
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      emit_begin_xfb_query(cmdbuf, pool, query, 0);
      break;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      emit_begin_stat_query(cmdbuf, pool, query);
      break;
   case VK_QUERY_TYPE_TIMESTAMP:
      unreachable("Unimplemented query type");
   default:
      assert(!"Invalid query type");
   }
}

void
tu_CmdBeginQueryIndexedEXT(VkCommandBuffer commandBuffer,
                           VkQueryPool queryPool,
                           uint32_t query,
                           VkQueryControlFlags flags,
                           uint32_t index)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   TU_FROM_HANDLE(tu_query_pool, pool, queryPool);
   assert(query < pool->size);

   switch (pool->type) {
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      emit_begin_xfb_query(cmdbuf, pool, query, index);
      break;
   default:
      assert(!"Invalid query type");
   }
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

   uint64_t available_iova = query_available_iova(pool, query);
   uint64_t begin_iova = occlusion_query_iova(pool, query, begin);
   uint64_t end_iova = occlusion_query_iova(pool, query, end);
   uint64_t result_iova = query_result_iova(pool, query, 0);
   tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 4);
   tu_cs_emit_qw(cs, end_iova);
   tu_cs_emit_qw(cs, 0xffffffffffffffffull);

   tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);

   tu_cs_emit_regs(cs,
                   A6XX_RB_SAMPLE_COUNT_CONTROL(.copy = true));

   tu_cs_emit_regs(cs,
                   A6XX_RB_SAMPLE_COUNT_ADDR(.qword = end_iova));

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

   tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 4);
   tu_cs_emit_qw(cs, available_iova);
   tu_cs_emit_qw(cs, 0x1);
}

static void
emit_end_stat_query(struct tu_cmd_buffer *cmdbuf,
                    struct tu_query_pool *pool,
                    uint32_t query)
{
   struct tu_cs *cs = cmdbuf->state.pass ? &cmdbuf->draw_cs : &cmdbuf->cs;
   uint64_t end_iova = pipeline_stat_query_iova(pool, query, end);
   uint64_t available_iova = query_available_iova(pool, query);
   uint64_t result_iova;
   uint64_t stat_start_iova;
   uint64_t stat_stop_iova;

   tu6_emit_event_write(cmdbuf, cs, STOP_PRIMITIVE_CTRS);
   tu6_emit_event_write(cmdbuf, cs, RST_VTX_CNT);
   tu6_emit_event_write(cmdbuf, cs, STAT_EVENT);

   tu_cs_emit_wfi(cs);

   tu_cs_emit_pkt7(cs, CP_REG_TO_MEM, 3);
   tu_cs_emit(cs, CP_REG_TO_MEM_0_REG(REG_A6XX_RBBM_PRIMCTR_0_LO) |
                  CP_REG_TO_MEM_0_CNT(STAT_COUNT * 2) |
                  CP_REG_TO_MEM_0_64B);
   tu_cs_emit_qw(cs, end_iova);

   for (int i = 0; i < STAT_COUNT; i++) {
      result_iova = query_result_iova(pool, query, i);
      stat_start_iova = pipeline_stat_query_iova(pool, query, begin[i]);
      stat_stop_iova = pipeline_stat_query_iova(pool, query, end[i]);

      tu_cs_emit_pkt7(cs, CP_MEM_TO_MEM, 9);
      tu_cs_emit(cs, CP_MEM_TO_MEM_0_WAIT_FOR_MEM_WRITES |
                     CP_MEM_TO_MEM_0_DOUBLE |
                     CP_MEM_TO_MEM_0_NEG_C);

      tu_cs_emit_qw(cs, result_iova);
      tu_cs_emit_qw(cs, result_iova);
      tu_cs_emit_qw(cs, stat_stop_iova);
      tu_cs_emit_qw(cs, stat_start_iova);
   }

   tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);

   if (cmdbuf->state.pass)
      cs = &cmdbuf->draw_epilogue_cs;

   /* Set the availability to 1 */
   tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 4);
   tu_cs_emit_qw(cs, available_iova);
   tu_cs_emit_qw(cs, 0x1);
}

static void
emit_end_xfb_query(struct tu_cmd_buffer *cmdbuf,
                   struct tu_query_pool *pool,
                   uint32_t query,
                   uint32_t stream_id)
{
   struct tu_cs *cs = cmdbuf->state.pass ? &cmdbuf->draw_cs : &cmdbuf->cs;

   uint64_t end_iova = primitive_query_iova(pool, query, end[0], 0);
   uint64_t result_written_iova = query_result_iova(pool, query, 0);
   uint64_t result_generated_iova = query_result_iova(pool, query, 1);
   uint64_t begin_written_iova = primitive_query_iova(pool, query, begin[stream_id], 0);
   uint64_t begin_generated_iova = primitive_query_iova(pool, query, begin[stream_id], 1);
   uint64_t end_written_iova = primitive_query_iova(pool, query, end[stream_id], 0);
   uint64_t end_generated_iova = primitive_query_iova(pool, query, end[stream_id], 1);
   uint64_t available_iova = query_available_iova(pool, query);

   tu_cs_emit_regs(cs, A6XX_VPC_SO_STREAM_COUNTS(.qword = end_iova));
   tu6_emit_event_write(cmdbuf, cs, WRITE_PRIMITIVE_COUNTS);

   tu_cs_emit_wfi(cs);
   tu6_emit_event_write(cmdbuf, cs, CACHE_FLUSH_TS);

   /* Set the count of written primitives */
   tu_cs_emit_pkt7(cs, CP_MEM_TO_MEM, 9);
   tu_cs_emit(cs, CP_MEM_TO_MEM_0_DOUBLE | CP_MEM_TO_MEM_0_NEG_C |
                  CP_MEM_TO_MEM_0_WAIT_FOR_MEM_WRITES | 0x80000000);
   tu_cs_emit_qw(cs, result_written_iova);
   tu_cs_emit_qw(cs, result_written_iova);
   tu_cs_emit_qw(cs, end_written_iova);
   tu_cs_emit_qw(cs, begin_written_iova);

   tu6_emit_event_write(cmdbuf, cs, CACHE_FLUSH_TS);

   /* Set the count of generated primitives */
   tu_cs_emit_pkt7(cs, CP_MEM_TO_MEM, 9);
   tu_cs_emit(cs, CP_MEM_TO_MEM_0_DOUBLE | CP_MEM_TO_MEM_0_NEG_C |
                  CP_MEM_TO_MEM_0_WAIT_FOR_MEM_WRITES | 0x80000000);
   tu_cs_emit_qw(cs, result_generated_iova);
   tu_cs_emit_qw(cs, result_generated_iova);
   tu_cs_emit_qw(cs, end_generated_iova);
   tu_cs_emit_qw(cs, begin_generated_iova);

   /* Set the availability to 1 */
   tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 4);
   tu_cs_emit_qw(cs, available_iova);
   tu_cs_emit_qw(cs, 0x1);
}

/* Implement this bit of spec text from section 17.2 "Query Operation":
 *
 *     If queries are used while executing a render pass instance that has
 *     multiview enabled, the query uses N consecutive query indices in the
 *     query pool (starting at query) where N is the number of bits set in the
 *     view mask in the subpass the query is used in. How the numerical
 *     results of the query are distributed among the queries is
 *     implementation-dependent. For example, some implementations may write
 *     each view’s results to a distinct query, while other implementations
 *     may write the total result to the first query and write zero to the
 *     other queries. However, the sum of the results in all the queries must
 *     accurately reflect the total result of the query summed over all views.
 *     Applications can sum the results from all the queries to compute the
 *     total result.
 *
 * Since we execute all views at once, we write zero to the other queries.
 * Furthermore, because queries must be reset before use, and we set the
 * result to 0 in vkCmdResetQueryPool(), we just need to mark it as available.
 */

static void
handle_multiview_queries(struct tu_cmd_buffer *cmd,
                         struct tu_query_pool *pool,
                         uint32_t query)
{
   if (!cmd->state.pass || !cmd->state.subpass->multiview_mask)
      return;

   unsigned views = util_bitcount(cmd->state.subpass->multiview_mask);
   struct tu_cs *cs = &cmd->draw_epilogue_cs;

   for (uint32_t i = 1; i < views; i++) {
      tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 4);
      tu_cs_emit_qw(cs, query_available_iova(pool, query + i));
      tu_cs_emit_qw(cs, 0x1);
   }
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
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      emit_end_xfb_query(cmdbuf, pool, query, 0);
      break;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      emit_end_stat_query(cmdbuf, pool, query);
      break;
   case VK_QUERY_TYPE_TIMESTAMP:
      unreachable("Unimplemented query type");
   default:
      assert(!"Invalid query type");
   }

   handle_multiview_queries(cmdbuf, pool, query);
}

void
tu_CmdEndQueryIndexedEXT(VkCommandBuffer commandBuffer,
                         VkQueryPool queryPool,
                         uint32_t query,
                         uint32_t index)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   TU_FROM_HANDLE(tu_query_pool, pool, queryPool);
   assert(query < pool->size);

   switch (pool->type) {
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      assert(index <= 4);
      emit_end_xfb_query(cmdbuf, pool, query, index);
      break;
   default:
      assert(!"Invalid query type");
   }
}

void
tu_CmdWriteTimestamp(VkCommandBuffer commandBuffer,
                     VkPipelineStageFlagBits pipelineStage,
                     VkQueryPool queryPool,
                     uint32_t query)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_query_pool, pool, queryPool);

   /* Inside a render pass, just write the timestamp multiple times so that
    * the user gets the last one if we use GMEM. There isn't really much
    * better we can do, and this seems to be what the blob does too.
    */
   struct tu_cs *cs = cmd->state.pass ? &cmd->draw_cs : &cmd->cs;

   /* Stages that will already have been executed by the time the CP executes
    * the REG_TO_MEM. DrawIndirect parameters are read by the CP, so the draw
    * indirect stage counts as top-of-pipe too.
    */
   VkPipelineStageFlags top_of_pipe_flags =
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT |
      VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;

   if (pipelineStage & ~top_of_pipe_flags) {
      /* Execute a WFI so that all commands complete. Note that CP_REG_TO_MEM
       * does CP_WAIT_FOR_ME internally, which will wait for the WFI to
       * complete.
       *
       * Stalling the CP like this is really unfortunate, but I don't think
       * there's a better solution that allows all 48 bits of precision
       * because CP_EVENT_WRITE doesn't support 64-bit timestamps.
       */
      tu_cs_emit_wfi(cs);
   }

   tu_cs_emit_pkt7(cs, CP_REG_TO_MEM, 3);
   tu_cs_emit(cs, CP_REG_TO_MEM_0_REG(REG_A6XX_CP_ALWAYS_ON_COUNTER_LO) |
                  CP_REG_TO_MEM_0_CNT(2) |
                  CP_REG_TO_MEM_0_64B);
   tu_cs_emit_qw(cs, query_result_iova(pool, query, 0));

   /* Only flag availability once the entire renderpass is done, similar to
    * the begin/end path.
    */
   cs = cmd->state.pass ? &cmd->draw_epilogue_cs : &cmd->cs;

   tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 4);
   tu_cs_emit_qw(cs, query_available_iova(pool, query));
   tu_cs_emit_qw(cs, 0x1);

   /* From the spec for vkCmdWriteTimestamp:
    *
    *    If vkCmdWriteTimestamp is called while executing a render pass
    *    instance that has multiview enabled, the timestamp uses N consecutive
    *    query indices in the query pool (starting at query) where N is the
    *    number of bits set in the view mask of the subpass the command is
    *    executed in. The resulting query values are determined by an
    *    implementation-dependent choice of one of the following behaviors:
    *
    *    -   The first query is a timestamp value and (if more than one bit is
    *        set in the view mask) zero is written to the remaining queries.
    *        If two timestamps are written in the same subpass, the sum of the
    *        execution time of all views between those commands is the
    *        difference between the first query written by each command.
    *
    *    -   All N queries are timestamp values. If two timestamps are written
    *        in the same subpass, the sum of the execution time of all views
    *        between those commands is the sum of the difference between
    *        corresponding queries written by each command. The difference
    *        between corresponding queries may be the execution time of a
    *        single view.
    *
    * We execute all views in the same draw call, so we implement the first
    * option, the same as regular queries.
    */
   handle_multiview_queries(cmd, pool, query);
}
