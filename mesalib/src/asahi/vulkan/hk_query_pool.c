/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "hk_query_pool.h"

#include "agx_compile.h"
#include "agx_pack.h"
#include "hk_buffer.h"
#include "hk_cmd_buffer.h"
#include "hk_device.h"
#include "hk_entrypoints.h"
#include "hk_shader.h"

#include "libagx_shaders.h"
#include "vk_common_entrypoints.h"

#include "asahi/lib/agx_bo.h"
#include "asahi/libagx/libagx.h"
#include "asahi/libagx/query.h"
#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"

#include "util/os_time.h"
#include "util/u_dynarray.h"
#include "vulkan/vulkan_core.h"

struct hk_query_report {
   uint64_t value;
};

static inline bool
hk_has_available(const struct hk_query_pool *pool)
{
   return pool->vk.query_type != VK_QUERY_TYPE_TIMESTAMP;
}

static uint16_t *
hk_pool_oq_index_ptr(const struct hk_query_pool *pool)
{
   return agx_bo_map(pool->bo) + pool->query_start;
}

static uint32_t
hk_reports_per_query(struct hk_query_pool *pool)
{
   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_OCCLUSION:
   case VK_QUERY_TYPE_TIMESTAMP:
   case VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT:
      return 1;
   case VK_QUERY_TYPE_PIPELINE_STATISTICS:
      return util_bitcount(pool->vk.pipeline_statistics);
   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT:
      // Primitives succeeded and primitives needed
      return 2;
   default:
      unreachable("Unsupported query type");
   }
}

static void
hk_flush_if_timestamp(struct hk_cmd_buffer *cmd, struct hk_query_pool *pool)
{
   struct hk_device *dev = hk_cmd_buffer_device(cmd);

   /* There might not be a barrier between the timestamp write and the copy
    * otherwise but we need one to give the CPU a chance to write the timestamp.
    * This could maybe optimized.
    */
   if (pool->vk.query_type == VK_QUERY_TYPE_TIMESTAMP) {
      perf_debug(dev, "Flushing for timestamp copy");
      hk_cmd_buffer_end_graphics(cmd);
      hk_cmd_buffer_end_compute(cmd);
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_CreateQueryPool(VkDevice device, const VkQueryPoolCreateInfo *pCreateInfo,
                   const VkAllocationCallbacks *pAllocator,
                   VkQueryPool *pQueryPool)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   struct hk_query_pool *pool;

   bool occlusion = pCreateInfo->queryType == VK_QUERY_TYPE_OCCLUSION;
   bool timestamp = pCreateInfo->queryType == VK_QUERY_TYPE_TIMESTAMP;
   unsigned occlusion_queries = occlusion ? pCreateInfo->queryCount : 0;

   /* Workaround for DXVK on old kernels */
   if (!agx_supports_timestamps(&dev->dev))
      timestamp = false;

   pool =
      vk_query_pool_create(&dev->vk, pCreateInfo, pAllocator, sizeof(*pool));
   if (!pool)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* We place the availability first and then data */
   pool->query_start = 0;
   if (hk_has_available(pool)) {
      pool->query_start = align(pool->vk.query_count * sizeof(uint32_t),
                                sizeof(struct hk_query_report));
   }

   uint32_t reports_per_query = hk_reports_per_query(pool);
   pool->query_stride = reports_per_query * sizeof(struct hk_query_report);

   if (pool->vk.query_count > 0) {
      uint32_t bo_size = pool->query_start;

      /* For occlusion queries, we stick the query index remapping here */
      if (occlusion_queries)
         bo_size += sizeof(uint16_t) * pool->vk.query_count;
      else
         bo_size += pool->query_stride * pool->vk.query_count;

      /* The kernel requires that timestamp buffers are SHARED */
      enum agx_bo_flags flags = AGX_BO_WRITEBACK;
      if (timestamp)
         flags |= AGX_BO_SHARED;

      pool->bo = agx_bo_create(&dev->dev, bo_size, 0, flags, "Query pool");
      if (!pool->bo) {
         hk_DestroyQueryPool(device, hk_query_pool_to_handle(pool), pAllocator);
         return vk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      }

      /* Timestamp buffers must be explicitly bound as such before we can use
       * them.
       */
      if (timestamp) {
         int ret = dev->dev.ops.bo_bind_object(
            &dev->dev, pool->bo, &pool->handle, pool->bo->size, 0,
            ASAHI_BIND_OBJECT_USAGE_TIMESTAMPS);

         if (ret) {
            hk_DestroyQueryPool(device, hk_query_pool_to_handle(pool),
                                pAllocator);
            return vk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
         }

         assert(pool->handle && "handles are nonzero");
      }
   }

   uint16_t *oq_index = hk_pool_oq_index_ptr(pool);

   for (unsigned i = 0; i < occlusion_queries; ++i) {
      uint64_t zero = 0;
      unsigned index;

      VkResult result = hk_descriptor_table_add(
         dev, &dev->occlusion_queries, &zero, sizeof(uint64_t), &index);

      if (result != VK_SUCCESS) {
         hk_DestroyQueryPool(device, hk_query_pool_to_handle(pool), pAllocator);
         return vk_error(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY);
      }

      /* We increment as we go so we can clean up properly if we run out */
      assert(pool->oq_queries < occlusion_queries);
      oq_index[pool->oq_queries++] = index;
   }

   *pQueryPool = hk_query_pool_to_handle(pool);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
hk_DestroyQueryPool(VkDevice device, VkQueryPool queryPool,
                    const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   VK_FROM_HANDLE(hk_query_pool, pool, queryPool);

   if (!pool)
      return;

   uint16_t *oq_index = hk_pool_oq_index_ptr(pool);

   for (unsigned i = 0; i < pool->oq_queries; ++i) {
      hk_descriptor_table_remove(dev, &dev->occlusion_queries, oq_index[i]);
   }

   if (pool->handle)
      dev->dev.ops.bo_unbind_object(&dev->dev, pool->handle, 0);

   agx_bo_unreference(&dev->dev, pool->bo);
   vk_query_pool_destroy(&dev->vk, pAllocator, &pool->vk);
}

static uint64_t
hk_query_available_addr(struct hk_query_pool *pool, uint32_t query)
{
   assert(hk_has_available(pool));
   assert(query < pool->vk.query_count);
   return pool->bo->va->addr + query * sizeof(uint32_t);
}

static uint32_t *
hk_query_available_map(struct hk_query_pool *pool, uint32_t query)
{
   assert(hk_has_available(pool));
   assert(query < pool->vk.query_count);
   return (uint32_t *)agx_bo_map(pool->bo) + query;
}

static uint64_t
hk_query_offset(struct hk_query_pool *pool, uint32_t query)
{
   assert(query < pool->vk.query_count);
   return pool->query_start + query * pool->query_stride;
}

static uint64_t
hk_query_report_addr(struct hk_device *dev, struct hk_query_pool *pool,
                     uint32_t query)
{
   if (pool->oq_queries) {
      uint16_t *oq_index = hk_pool_oq_index_ptr(pool);
      return dev->occlusion_queries.bo->va->addr +
             (oq_index[query] * sizeof(uint64_t));
   } else {
      return pool->bo->va->addr + hk_query_offset(pool, query);
   }
}

static struct hk_query_report *
hk_query_report_map(struct hk_device *dev, struct hk_query_pool *pool,
                    uint32_t query)
{
   if (pool->oq_queries) {
      uint64_t *queries = (uint64_t *)agx_bo_map(dev->occlusion_queries.bo);
      uint16_t *oq_index = hk_pool_oq_index_ptr(pool);

      return (struct hk_query_report *)&queries[oq_index[query]];
   } else {
      return (void *)((char *)agx_bo_map(pool->bo) +
                      hk_query_offset(pool, query));
   }
}

void
hk_dispatch_imm_writes(struct hk_cmd_buffer *cmd, struct hk_cs *cs)
{
   hk_ensure_cs_has_space(cmd, cs, 0x2000 /* TODO */);

   /* As soon as we mark a query available, it needs to be available system
    * wide, otherwise a CPU-side get result can query. As such, we cache flush
    * before and then let coherency works its magic. Without this barrier, we
    * get flakes in
    *
    * dEQP-VK.query_pool.occlusion_query.get_results_conservative_size_64_wait_query_without_availability_draw_triangles_discard
    */
   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   hk_cdm_cache_flush(dev, cs);

   perf_debug(dev, "Queued writes");

   uint64_t params =
      hk_pool_upload(cmd, cs->imm_writes.data, cs->imm_writes.size, 16);

   uint32_t count =
      util_dynarray_num_elements(&cs->imm_writes, struct libagx_imm_write);
   assert(count > 0);

   libagx_write_u32s(cs, agx_1d(count), AGX_BARRIER_ALL, params);
}

void
hk_queue_write(struct hk_cmd_buffer *cmd, uint64_t address, uint32_t value,
               bool after_gfx)
{
   struct hk_cs *cs = hk_cmd_buffer_get_cs_general(
      cmd, after_gfx ? &cmd->current_cs.post_gfx : &cmd->current_cs.cs, true);
   if (!cs)
      return;

   /* TODO: Generalize this mechanism suitably */
   if (after_gfx) {
      struct libagx_imm_write imm = {.address = address, .value = value};

      if (!cs->imm_writes.data) {
         util_dynarray_init(&cs->imm_writes, NULL);
      }

      util_dynarray_append(&cs->imm_writes, struct libagx_imm_write, imm);
      return;
   }

   hk_ensure_cs_has_space(cmd, cs, 0x2000 /* TODO */);

   /* As soon as we mark a query available, it needs to be available system
    * wide, otherwise a CPU-side get result can query. As such, we cache flush
    * before and then let coherency works its magic. Without this barrier, we
    * get flakes in
    *
    * dEQP-VK.query_pool.occlusion_query.get_results_conservative_size_64_wait_query_without_availability_draw_triangles_discard
    */
   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   hk_cdm_cache_flush(dev, cs);

   perf_debug(dev, "Queued write");
   libagx_write_u32(cs, agx_1d(1), AGX_BARRIER_ALL, address, value);
}

/**
 * Goes through a series of consecutive query indices in the given pool,
 * setting all element values to 0 and emitting them as available.
 */
static void
emit_zero_queries(struct hk_cmd_buffer *cmd, struct hk_query_pool *pool,
                  uint32_t first_index, uint32_t num_queries,
                  bool set_available)
{
   struct hk_device *dev = hk_cmd_buffer_device(cmd);

   for (uint32_t i = 0; i < num_queries; i++) {
      uint64_t report = hk_query_report_addr(dev, pool, first_index + i);

      uint64_t value = 0;
      if (hk_has_available(pool)) {
         uint64_t available = hk_query_available_addr(pool, first_index + i);
         hk_queue_write(cmd, available, set_available, false);
      } else {
         value = set_available ? 0 : LIBAGX_QUERY_UNAVAILABLE;
      }

      /* XXX: is this supposed to happen on the begin? */
      for (unsigned j = 0; j < hk_reports_per_query(pool); ++j) {
         hk_queue_write(cmd, report + (j * sizeof(struct hk_query_report)),
                        value, false);
         hk_queue_write(cmd, report + (j * sizeof(struct hk_query_report)) + 4,
                        value >> 32, false);
      }
   }
}

static void
host_zero_queries(struct hk_device *dev, struct hk_query_pool *pool,
                  uint32_t first_index, uint32_t num_queries,
                  bool set_available)
{
   for (uint32_t i = 0; i < num_queries; i++) {
      struct hk_query_report *reports =
         hk_query_report_map(dev, pool, first_index + i);

      uint64_t value = 0;
      if (hk_has_available(pool)) {
         uint32_t *available = hk_query_available_map(pool, first_index + i);
         *available = set_available;
      } else {
         value = set_available ? 0 : LIBAGX_QUERY_UNAVAILABLE;
      }

      for (unsigned j = 0; j < hk_reports_per_query(pool); ++j) {
         reports[j].value = value;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
hk_ResetQueryPool(VkDevice device, VkQueryPool queryPool, uint32_t firstQuery,
                  uint32_t queryCount)
{
   VK_FROM_HANDLE(hk_query_pool, pool, queryPool);
   VK_FROM_HANDLE(hk_device, dev, device);

   host_zero_queries(dev, pool, firstQuery, queryCount, false);
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdResetQueryPool(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                     uint32_t firstQuery, uint32_t queryCount)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(hk_query_pool, pool, queryPool);
   struct hk_device *dev = hk_cmd_buffer_device(cmd);

   hk_flush_if_timestamp(cmd, pool);

   perf_debug(dev, "Reset query pool");
   emit_zero_queries(cmd, pool, firstQuery, queryCount, false);
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdWriteTimestamp2(VkCommandBuffer commandBuffer,
                      VkPipelineStageFlags2 stage, VkQueryPool queryPool,
                      uint32_t query)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(hk_query_pool, pool, queryPool);
   struct hk_device *dev = hk_cmd_buffer_device(cmd);

   /* Workaround for DXVK on old kernels */
   if (!agx_supports_timestamps(&dev->dev))
      return;

   uint64_t report_addr = hk_query_report_addr(dev, pool, query);

   bool after_gfx = cmd->current_cs.gfx != NULL;

   /* When writing timestamps for compute, we split the control stream at each
    * write. This ensures we never need to copy compute timestamps, which would
    * require an extra control stream anyway. Unlike graphics, splitting compute
    * control streams is inexpensive so there's not a strong performance reason
    * to do otherwise. Finally, batching multiple timestamp writes (like we do
    * for graphics) would destroy the ability to profile individual compute
    * dispatches. While that's allowed by the Vulkan spec, it's pretty mean to
    * apps. So.. don't do that.
    */
   if (!after_gfx && cmd->current_cs.cs &&
       cmd->current_cs.cs->timestamp.end.addr) {

      perf_debug(dev, "Splitting for compute timestamp");
      hk_cmd_buffer_end_compute(cmd);
   }

   struct hk_cs *cs = hk_cmd_buffer_get_cs_general(
      cmd, after_gfx ? &cmd->current_cs.gfx : &cmd->current_cs.cs, true);
   if (!cs)
      return;

   if (cs->timestamp.end.addr) {
      assert(after_gfx && "compute is handled above");

      struct hk_cs *after =
         hk_cmd_buffer_get_cs_general(cmd, &cmd->current_cs.post_gfx, true);
      if (!after)
         return;

      libagx_copy_timestamp(after, agx_1d(1), AGX_BARRIER_ALL, report_addr,
                            cs->timestamp.end.addr);
   } else {
      cs->timestamp.end = (struct agx_timestamp_req){
         .addr = report_addr,
         .handle = pool->handle,
         .offset_B = hk_query_offset(pool, query),
      };
   }

   /* From the Vulkan spec:
    *
    *   "If vkCmdWriteTimestamp2 is called while executing a render pass
    *    instance that has multiview enabled, the timestamp uses N consecutive
    *    query indices in the query pool (starting at query) where N is the
    *    number of bits set in the view mask of the subpass the command is
    *    executed in. The resulting query values are determined by an
    *    implementation-dependent choice of one of the following behaviors:"
    *
    * In our case, only the first query is used, so we emit zeros for the
    * remaining queries, as described in the first behavior listed in the
    * Vulkan spec:
    *
    *   "The first query is a timestamp value and (if more than one bit is set
    *   in the view mask) zero is written to the remaining queries."
    */
   if (cmd->state.gfx.render.view_mask != 0) {
      const uint32_t num_queries =
         util_bitcount(cmd->state.gfx.render.view_mask);
      if (num_queries > 1)
         emit_zero_queries(cmd, pool, query + 1, num_queries - 1, true);
   }
}

static void
hk_cmd_begin_end_query(struct hk_cmd_buffer *cmd, struct hk_query_pool *pool,
                       uint32_t query, uint32_t index,
                       VkQueryControlFlags flags, bool end)
{
   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   bool graphics = false;

   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_OCCLUSION: {
      assert(query < pool->oq_queries);

      if (end) {
         cmd->state.gfx.occlusion.mode = AGX_VISIBILITY_MODE_NONE;
      } else {
         cmd->state.gfx.occlusion.mode = flags & VK_QUERY_CONTROL_PRECISE_BIT
                                            ? AGX_VISIBILITY_MODE_COUNTING
                                            : AGX_VISIBILITY_MODE_BOOLEAN;
      }

      uint16_t *oq_index = hk_pool_oq_index_ptr(pool);
      cmd->state.gfx.occlusion.index = oq_index[query];
      cmd->state.gfx.dirty |= HK_DIRTY_OCCLUSION;
      graphics = true;
      break;
   }

   case VK_QUERY_TYPE_TRANSFORM_FEEDBACK_STREAM_EXT: {
      uint64_t addr = hk_query_report_addr(dev, pool, query);
      cmd->state.gfx.xfb_query[index] = end ? 0 : addr;
      break;
   }

   case VK_QUERY_TYPE_PIPELINE_STATISTICS: {
      struct hk_root_descriptor_table *root = &cmd->state.gfx.descriptors.root;
      cmd->state.gfx.descriptors.root_dirty = true;

      root->draw.pipeline_stats = hk_query_report_addr(dev, pool, query);
      root->draw.pipeline_stats_flags = pool->vk.pipeline_statistics;

      /* XXX: I don't think is correct... when does the query become available
       * exactly?
       */
      graphics = pool->vk.pipeline_statistics &
                 ~VK_QUERY_PIPELINE_STATISTIC_COMPUTE_SHADER_INVOCATIONS_BIT;
      break;
   }

   default:
      unreachable("Unsupported query type");
   }

   /* We need to set available=1 after the graphics work finishes. */
   if (end) {
      perf_debug(dev, "Query ending, type %u", pool->vk.query_type);
      hk_queue_write(cmd, hk_query_available_addr(pool, query), 1, graphics);
   }
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdBeginQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                           uint32_t query, VkQueryControlFlags flags,
                           uint32_t index)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(hk_query_pool, pool, queryPool);

   hk_cmd_begin_end_query(cmd, pool, query, index, flags, false);
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdEndQueryIndexedEXT(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                         uint32_t query, uint32_t index)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(hk_query_pool, pool, queryPool);
   struct hk_device *dev = hk_cmd_buffer_device(cmd);

   hk_cmd_begin_end_query(cmd, pool, query, index, 0, true);

   /* From the Vulkan spec:
    *
    *   "If queries are used while executing a render pass instance that has
    *    multiview enabled, the query uses N consecutive query indices in
    *    the query pool (starting at query) where N is the number of bits set
    *    in the view mask in the subpass the query is used in. How the
    *    numerical results of the query are distributed among the queries is
    *    implementation-dependent."
    *
    * In our case, only the first query is used, so we emit zeros for the
    * remaining queries.
    */
   if (cmd->state.gfx.render.view_mask != 0) {
      const uint32_t num_queries =
         util_bitcount(cmd->state.gfx.render.view_mask);
      if (num_queries > 1) {
         perf_debug(dev, "Multiview query zeroing");
         emit_zero_queries(cmd, pool, query + 1, num_queries - 1, true);
      }
   }
}

static bool
hk_query_is_available(struct hk_device *dev, struct hk_query_pool *pool,
                      uint32_t query)
{
   if (hk_has_available(pool)) {
      uint32_t *available = hk_query_available_map(pool, query);
      return p_atomic_read(available) != 0;
   } else {
      const struct hk_query_report *report =
         hk_query_report_map(dev, pool, query);

      return report->value != LIBAGX_QUERY_UNAVAILABLE;
   }
}

#define HK_QUERY_TIMEOUT 2000000000ull

static VkResult
hk_query_wait_for_available(struct hk_device *dev, struct hk_query_pool *pool,
                            uint32_t query)
{
   uint64_t abs_timeout_ns = os_time_get_absolute_timeout(HK_QUERY_TIMEOUT);

   while (os_time_get_nano() < abs_timeout_ns) {
      if (hk_query_is_available(dev, pool, query))
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

VKAPI_ATTR VkResult VKAPI_CALL
hk_GetQueryPoolResults(VkDevice device, VkQueryPool queryPool,
                       uint32_t firstQuery, uint32_t queryCount,
                       size_t dataSize, void *pData, VkDeviceSize stride,
                       VkQueryResultFlags flags)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   VK_FROM_HANDLE(hk_query_pool, pool, queryPool);

   if (vk_device_is_lost(&dev->vk))
      return VK_ERROR_DEVICE_LOST;

   VkResult status = VK_SUCCESS;
   for (uint32_t i = 0; i < queryCount; i++) {
      const uint32_t query = firstQuery + i;

      bool available = hk_query_is_available(dev, pool, query);

      if (!available && (flags & VK_QUERY_RESULT_WAIT_BIT)) {
         status = hk_query_wait_for_available(dev, pool, query);
         if (status != VK_SUCCESS)
            return status;

         available = true;
      }

      bool write_results = available || (flags & VK_QUERY_RESULT_PARTIAL_BIT);

      const struct hk_query_report *src = hk_query_report_map(dev, pool, query);
      assert(i * stride < dataSize);
      void *dst = (char *)pData + i * stride;

      uint32_t reports = hk_reports_per_query(pool);
      if (write_results) {
         for (uint32_t j = 0; j < reports; j++) {
            cpu_write_query_result(dst, j, flags, src[j].value);
         }
      }

      if (!write_results)
         status = VK_NOT_READY;

      if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT)
         cpu_write_query_result(dst, reports, flags, available);
   }

   return status;
}

VKAPI_ATTR void VKAPI_CALL
hk_CmdCopyQueryPoolResults(VkCommandBuffer commandBuffer, VkQueryPool queryPool,
                           uint32_t firstQuery, uint32_t queryCount,
                           VkBuffer dstBuffer, VkDeviceSize dstOffset,
                           VkDeviceSize stride, VkQueryResultFlags flags)
{
   VK_FROM_HANDLE(hk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(hk_query_pool, pool, queryPool);
   VK_FROM_HANDLE(hk_buffer, dst_buffer, dstBuffer);

   struct hk_device *dev = hk_cmd_buffer_device(cmd);
   hk_flush_if_timestamp(cmd, pool);

   struct hk_cs *cs = hk_cmd_buffer_get_cs(cmd, true);
   if (!cs)
      return;

   perf_debug(dev, "Query pool copy");
   hk_ensure_cs_has_space(cmd, cs, 0x2000 /* TODO */);

   struct libagx_copy_query_args info = {
      .availability = hk_has_available(pool) ? pool->bo->va->addr : 0,
      .results = pool->oq_queries ? dev->occlusion_queries.bo->va->addr
                                  : pool->bo->va->addr + pool->query_start,
      .oq_index = pool->oq_queries ? pool->bo->va->addr + pool->query_start : 0,

      .first_query = firstQuery,
      .dst_addr = hk_buffer_address(dst_buffer, dstOffset),
      .dst_stride = stride,
      .reports_per_query = hk_reports_per_query(pool),

      .partial = flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT,
      ._64 = flags & VK_QUERY_RESULT_64_BIT,
      .with_availability = flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT,
   };

   libagx_copy_query_struct(cs, agx_1d(queryCount), AGX_BARRIER_ALL, info);
}
