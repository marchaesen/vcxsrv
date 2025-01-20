/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include "util/os_time.h"

#include "vk_log.h"
#include "vk_synchronization.h"

#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_meta.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_macros.h"
#include "panvk_query_pool.h"

/* At the API level, a query consists of a status and a result.  Both are
 * uninitialized initially.  There are these query operations:
 *
 *  - Reset op sets the status to unavailable and leaves the result undefined.
 *  - Begin/End pair or Write op sets the status to available and the result
 *    to the final query value.  Because of VK_QUERY_RESULT_PARTIAL_BIT, the
 *    result must hold valid intermediate query values while the query is
 *    active.
 *  - Copy op copies the result and optionally the status to a buffer.
 *
 * All query operations define execution dependencies among themselves when
 * they reference the same queries.  The only exception is the Copy op when
 * VK_QUERY_RESULT_WAIT_BIT is not set.
 *
 * We use a panvk_cs_sync32 to store the status of a query:
 *
 *  - Reset op waits on all prior query operations affecting the query before
 *    setting the seqno to 0 synchronously.
 *  - Begin op does not access the seqno.
 *  - End or Write op sets the seqno to 1 asynchronously.
 *  - Copy op waits on the seqno only when VK_QUERY_RESULT_WAIT_BIT is set.
 *
 * Because Reset op acts as a full barrier, End or Write op knows the seqno is
 * 0 and does not need to wait.
 */

static void
reset_oq_batch(struct cs_builder *b, struct cs_index addr,
               struct cs_index zero_regs, uint32_t query_count)
{
   const uint32_t regs_per_query = 2;
   const uint32_t queries_per_batch = zero_regs.size / regs_per_query;
   uint32_t remaining_queries = query_count;

   assert(zero_regs.size > 2 && ALIGN_POT(zero_regs.size, 2) == zero_regs.size);

   if (query_count > queries_per_batch * 4) {
      struct cs_index counter = cs_reg32(b, zero_regs.reg + zero_regs.size - 1);
      struct cs_index new_zero_regs =
         cs_reg_tuple(b, zero_regs.reg, zero_regs.size - 2);
      const uint32_t adjusted_queries_per_batch =
         new_zero_regs.size / regs_per_query;
      uint32_t full_batches = query_count / adjusted_queries_per_batch;

      cs_move32_to(b, counter, full_batches);
      cs_while(b, MALI_CS_CONDITION_GREATER, counter) {
         cs_store(b, new_zero_regs, addr, BITFIELD_MASK(new_zero_regs.size), 0);
         cs_add64(b, addr, addr, new_zero_regs.size * sizeof(uint32_t));
         cs_add32(b, counter, counter, -1);
      }

      remaining_queries =
         query_count - (full_batches * adjusted_queries_per_batch);
   }

   for (uint32_t i = 0; i < remaining_queries; i += queries_per_batch) {
      struct cs_index new_zero_regs = cs_reg_tuple(
         b, zero_regs.reg,
         MIN2(remaining_queries - i, queries_per_batch) * regs_per_query);

      cs_store(b, new_zero_regs, addr, BITFIELD_MASK(new_zero_regs.size),
               i * sizeof(uint32_t));
   }
}

static void
panvk_cmd_reset_occlusion_queries(struct panvk_cmd_buffer *cmd,
                                  struct panvk_query_pool *pool,
                                  uint32_t first_query, uint32_t query_count)
{
   struct cs_builder *b = panvk_get_cs_builder(cmd, PANVK_SUBQUEUE_FRAGMENT);

   /* Wait on deferred sync to ensure all prior query operations have
    * completed
    */
   cs_wait_slot(b, SB_ID(DEFERRED_SYNC), false);

   struct cs_index addr = cs_scratch_reg64(b, 16);
   struct cs_index zero_regs = cs_scratch_reg_tuple(b, 0, 16);

   for (uint32_t i = 0; i < zero_regs.size; i += 2)
      cs_move64_to(b, cs_scratch_reg64(b, i), 0);

   /* Zero all query syncobj so it reports non-available. We don't use
    * cs_sync32_set() because no-one is waiting on this syncobj with
    * cs_sync32_wait(). The only reason we use a syncobj is so we can
    * defer the signalling in the issue_fragmnent_jobs() path. */
   cs_move64_to(b, addr, panvk_query_available_dev_addr(pool, first_query));
   reset_oq_batch(b, addr, zero_regs, query_count);

   cs_move64_to(b, addr, panvk_query_report_dev_addr(pool, first_query));
   reset_oq_batch(b, addr, zero_regs, query_count);

   /* reset_oq_batch() only does the stores, we need to flush those explicitly
    * here. */
   cs_wait_slot(b, SB_ID(LS), false);

   /* We flush the caches to make the new value visible to the CPU. */
   struct cs_index flush_id = cs_scratch_reg32(b, 0);

   cs_flush_caches(b, MALI_CS_FLUSH_MODE_CLEAN, MALI_CS_FLUSH_MODE_CLEAN, false,
                   flush_id,
                   cs_defer(SB_IMM_MASK, SB_ID(IMM_FLUSH)));
   cs_wait_slot(b, SB_ID(IMM_FLUSH), false);
}

static void
panvk_cmd_begin_occlusion_query(struct panvk_cmd_buffer *cmd,
                                struct panvk_query_pool *pool, uint32_t query,
                                VkQueryControlFlags flags)
{
   uint64_t report_addr = panvk_query_report_dev_addr(pool, query);

   cmd->state.gfx.occlusion_query.ptr = report_addr;
   cmd->state.gfx.occlusion_query.syncobj =
      panvk_query_available_dev_addr(pool, query);
   cmd->state.gfx.occlusion_query.mode = flags & VK_QUERY_CONTROL_PRECISE_BIT
                                            ? MALI_OCCLUSION_MODE_COUNTER
                                            : MALI_OCCLUSION_MODE_PREDICATE;
   gfx_state_set_dirty(cmd, OQ);

   /* From the Vulkan spec:
    *
    *   "When an occlusion query begins, the count of passing samples
    *    always starts at zero."
    *
    */
   struct cs_builder *b = panvk_get_cs_builder(cmd, PANVK_SUBQUEUE_FRAGMENT);

   struct cs_index report_addr_gpu = cs_scratch_reg64(b, 0);
   struct cs_index clear_value = cs_scratch_reg64(b, 2);
   cs_move64_to(b, report_addr_gpu, report_addr);
   cs_move64_to(b, clear_value, 0);
   cs_store64(b, clear_value, report_addr_gpu, 0);
   cs_wait_slot(b, SB_ID(LS), false);
}

static void
panvk_cmd_end_occlusion_query(struct panvk_cmd_buffer *cmd,
                              struct panvk_query_pool *pool, uint32_t query)
{
   uint64_t syncobj_addr = panvk_query_available_dev_addr(pool, query);

   cmd->state.gfx.occlusion_query.ptr = 0;
   cmd->state.gfx.occlusion_query.syncobj = 0;
   cmd->state.gfx.occlusion_query.mode = MALI_OCCLUSION_MODE_DISABLED;
   gfx_state_set_dirty(cmd, OQ);

   /* If the render pass is active, we let EndRendering take care of the
    * occlusion query end when the fragment job is issued. */
   if (cmd->state.gfx.render.oq.last == syncobj_addr)
      return;

   struct cs_builder *b = panvk_get_cs_builder(cmd, PANVK_SUBQUEUE_FRAGMENT);
   struct cs_index oq_syncobj = cs_scratch_reg64(b, 0);
   struct cs_index val = cs_scratch_reg32(b, 2);

   /* OQ accumulates sample counts to the report which is on a cached memory.
    * Wait for the accumulation and flush the caches.
    */
   cs_move32_to(b, val, 0);
   cs_flush_caches(b, MALI_CS_FLUSH_MODE_CLEAN, MALI_CS_FLUSH_MODE_CLEAN, false,
                   val, cs_defer(SB_ALL_ITERS_MASK, SB_ID(DEFERRED_FLUSH)));

   /* Signal the query syncobj after the flush is effective. */
   cs_move32_to(b, val, 1);
   cs_move64_to(b, oq_syncobj, panvk_query_available_dev_addr(pool, query));
   cs_sync32_set(b, true, MALI_CS_SYNC_SCOPE_CSG, val, oq_syncobj,
                 cs_defer(SB_MASK(DEFERRED_FLUSH), SB_ID(DEFERRED_SYNC)));
}

static void
copy_oq_result_batch(struct cs_builder *b,
                     VkQueryResultFlags flags,
                     struct cs_index dst_addr,
                     VkDeviceSize dst_stride,
                     struct cs_index res_addr,
                     struct cs_index avail_addr,
                     struct cs_index scratch_regs,
                     uint32_t query_count)
{
   uint32_t res_size = (flags & VK_QUERY_RESULT_64_BIT) ? 2 : 1;
   uint32_t regs_per_copy =
      res_size + ((flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) ? 1 : 0);

   assert(query_count <= scratch_regs.size / regs_per_copy);

   for (uint32_t i = 0; i < query_count; i++) {
      struct cs_index res =
         cs_reg_tuple(b, scratch_regs.reg + (i * regs_per_copy), res_size);
      struct cs_index avail = cs_reg32(b, res.reg + res_size);

      cs_load_to(b, res, res_addr, BITFIELD_MASK(res.size),
                 i * sizeof(uint64_t));

      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
         cs_load32_to(b, avail, avail_addr, i * sizeof(struct panvk_cs_sync32));
   }

   /* Flush the loads. */
   cs_wait_slot(b, SB_ID(LS), false);

   for (uint32_t i = 0; i < query_count; i++) {
      struct cs_index store_src =
         cs_reg_tuple(b, scratch_regs.reg + (i * regs_per_copy), regs_per_copy);

      cs_store(b, store_src, dst_addr, BITFIELD_MASK(regs_per_copy),
               i * dst_stride);
   }

   /* Flush the stores. */
   cs_wait_slot(b, SB_ID(LS), false);
}

static void
panvk_copy_occlusion_query_results(struct panvk_cmd_buffer *cmd,
                                   struct panvk_query_pool *pool,
                                   uint32_t first_query, uint32_t query_count,
                                   uint64_t dst_buffer_addr,
                                   VkDeviceSize stride,
                                   VkQueryResultFlags flags)
{
   struct cs_builder *b = panvk_get_cs_builder(cmd, PANVK_SUBQUEUE_FRAGMENT);

   /* Wait for occlusion query syncobjs to be signalled. */
   if (flags & VK_QUERY_RESULT_WAIT_BIT)
      cs_wait_slot(b, SB_ID(DEFERRED_SYNC), false);

   uint32_t res_size = (flags & VK_QUERY_RESULT_64_BIT) ? 2 : 1;
   uint32_t regs_per_copy =
      res_size + ((flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) ? 1 : 0);

   struct cs_index dst_addr = cs_scratch_reg64(b, 16);
   struct cs_index res_addr = cs_scratch_reg64(b, 14);
   struct cs_index avail_addr = cs_scratch_reg64(b, 12);
   struct cs_index counter = cs_scratch_reg32(b, 11);
   struct cs_index scratch_regs = cs_scratch_reg_tuple(b, 0, 11);
   uint32_t queries_per_batch = scratch_regs.size / regs_per_copy;

   /* Store offset is a 16-bit signed integer, so we might be limited by the
    * stride here. */
   queries_per_batch = MIN2(((1u << 15) / stride) + 1, queries_per_batch);

   /* Stop unrolling the loop when it takes more than 2 steps to copy the
    * queries. */
   if (query_count > 2 * queries_per_batch) {
      uint32_t copied_query_count =
         query_count - (query_count % queries_per_batch);

      cs_move32_to(b, counter, copied_query_count);
      cs_move64_to(b, dst_addr, dst_buffer_addr);
      cs_move64_to(b, res_addr, panvk_query_report_dev_addr(pool, first_query));
      cs_move64_to(b, avail_addr,
                   panvk_query_available_dev_addr(pool, first_query));
      cs_while(b, MALI_CS_CONDITION_GREATER, counter) {
         copy_oq_result_batch(b, flags, dst_addr, stride, res_addr, avail_addr,
                              scratch_regs, queries_per_batch);

         cs_add32(b, counter, counter, -queries_per_batch);
         cs_add64(b, dst_addr, dst_addr, queries_per_batch * stride);
         cs_add64(b, res_addr, res_addr, queries_per_batch * sizeof(uint64_t));
         cs_add64(b, avail_addr, avail_addr,
                  queries_per_batch * sizeof(uint64_t));
      }

      dst_buffer_addr += stride * copied_query_count;
      first_query += copied_query_count;
      query_count -= copied_query_count;
   }

   for (uint32_t i = 0; i < query_count; i += queries_per_batch) {
      cs_move64_to(b, dst_addr, dst_buffer_addr + (i * stride));
      cs_move64_to(b, res_addr,
                   panvk_query_report_dev_addr(pool, i + first_query));
      cs_move64_to(b, avail_addr,
                   panvk_query_available_dev_addr(pool, i + first_query));
      copy_oq_result_batch(b, flags, dst_addr, stride, res_addr, avail_addr,
                           scratch_regs,
                           MIN2(queries_per_batch, query_count - i));
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdResetQueryPool)(VkCommandBuffer commandBuffer,
                                  VkQueryPool queryPool, uint32_t firstQuery,
                                  uint32_t queryCount)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);

   if (queryCount == 0)
      return;

   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_OCCLUSION: {
      panvk_cmd_reset_occlusion_queries(cmd, pool, firstQuery, queryCount);
      break;
   }
   default:
      unreachable("Unsupported query type");
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBeginQueryIndexedEXT)(VkCommandBuffer commandBuffer,
                                        VkQueryPool queryPool, uint32_t query,
                                        VkQueryControlFlags flags,
                                        uint32_t index)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);

   /* TODO: transform feedback */
   assert(index == 0);

   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_OCCLUSION: {
      panvk_cmd_begin_occlusion_query(cmd, pool, query, flags);
      break;
   }
   default:
      unreachable("Unsupported query type");
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdEndQueryIndexedEXT)(VkCommandBuffer commandBuffer,
                                      VkQueryPool queryPool, uint32_t query,
                                      uint32_t index)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);

   /* TODO: transform feedback */
   assert(index == 0);

   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_OCCLUSION: {
      panvk_cmd_end_occlusion_query(cmd, pool, query);
      break;
   }
   default:
      unreachable("Unsupported query type");
   }
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdWriteTimestamp2)(VkCommandBuffer commandBuffer,
                                   VkPipelineStageFlags2 stage,
                                   VkQueryPool queryPool, uint32_t query)
{
   UNUSED VK_FROM_HANDLE(panvk_cmd_buffer, cmd, commandBuffer);
   UNUSED VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);

   panvk_stub();
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdCopyQueryPoolResults)(
   VkCommandBuffer commandBuffer, VkQueryPool queryPool, uint32_t firstQuery,
   uint32_t queryCount, VkBuffer dstBuffer, VkDeviceSize dstOffset,
   VkDeviceSize stride, VkQueryResultFlags flags)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);
   VK_FROM_HANDLE(panvk_buffer, dst_buffer, dstBuffer);

   uint64_t dst_buffer_addr = panvk_buffer_gpu_ptr(dst_buffer, dstOffset);

   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_OCCLUSION: {
      panvk_copy_occlusion_query_results(cmd, pool, firstQuery, queryCount,
                                         dst_buffer_addr, stride, flags);
      break;
   }
   default:
      unreachable("Unsupported query type");
   }
}
