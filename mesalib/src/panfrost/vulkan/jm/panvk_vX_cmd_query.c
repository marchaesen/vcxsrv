/*
 * Copyright © 2024 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#include "util/os_time.h"

#include "nir_builder.h"

#include "vk_log.h"
#include "vk_meta.h"
#include "vk_pipeline.h"

#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_cmd_buffer.h"
#include "panvk_cmd_meta.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_macros.h"
#include "panvk_query_pool.h"

static nir_def *
panvk_nir_query_report_dev_addr(nir_builder *b, nir_def *pool_addr,
                                nir_def *query_stride, nir_def *query)
{
   return nir_iadd(b, pool_addr, nir_umul_2x32_64(b, query, query_stride));
}

static nir_def *
panvk_nir_available_dev_addr(nir_builder *b, nir_def *available_addr,
                             nir_def *query)
{
   nir_def *offset = nir_imul_imm(b, query, sizeof(uint32_t));
   return nir_iadd(b, available_addr, nir_u2u64(b, offset));
}

static void
panvk_emit_write_job(struct panvk_cmd_buffer *cmd, struct panvk_batch *batch,
                     enum mali_write_value_type type, uint64_t addr,
                     uint64_t value)
{
   struct panfrost_ptr job =
      pan_pool_alloc_desc(&cmd->desc_pool.base, WRITE_VALUE_JOB);

   pan_section_pack(job.cpu, WRITE_VALUE_JOB, PAYLOAD, payload) {
      payload.type = type;
      payload.address = addr;
      payload.immediate_value = value;
   };

   pan_jc_add_job(&batch->vtc_jc, MALI_JOB_TYPE_WRITE_VALUE, true, false, 0, 0,
                  &job, false);
}

static struct panvk_batch *
open_batch(struct panvk_cmd_buffer *cmd, bool *had_batch)
{
   bool res = cmd->cur_batch != NULL;

   if (!res)
      panvk_per_arch(cmd_open_batch)(cmd);

   *had_batch = res;

   return cmd->cur_batch;
}

static void
close_batch(struct panvk_cmd_buffer *cmd, bool had_batch)
{
   if (!had_batch)
      panvk_per_arch(cmd_close_batch)(cmd);
}

#define load_info(__b, __type, __field_name)                                   \
   nir_load_push_constant((__b), 1,                                            \
                          sizeof(((__type *)NULL)->__field_name) * 8,          \
                          nir_imm_int(b, offsetof(__type, __field_name)))

struct panvk_clear_query_push {
   uint64_t pool_addr;
   uint64_t available_addr;
   uint32_t query_stride;
   uint32_t first_query;
   uint32_t query_count;
   uint32_t reports_per_query;
   uint32_t availaible_value;
};

static void
panvk_nir_clear_query(nir_builder *b, nir_def *i)
{
   nir_def *pool_addr = load_info(b, struct panvk_clear_query_push, pool_addr);
   nir_def *available_addr =
      nir_u2u64(b, load_info(b, struct panvk_clear_query_push, available_addr));
   nir_def *query_stride =
      load_info(b, struct panvk_clear_query_push, query_stride);
   nir_def *first_query =
      load_info(b, struct panvk_clear_query_push, first_query);
   nir_def *reports_per_query =
      load_info(b, struct panvk_clear_query_push, reports_per_query);
   nir_def *avail_value =
      load_info(b, struct panvk_clear_query_push, availaible_value);

   nir_def *query = nir_iadd(b, first_query, i);

   nir_def *avail_addr = panvk_nir_available_dev_addr(b, available_addr, query);
   nir_def *report_addr =
      panvk_nir_query_report_dev_addr(b, pool_addr, query_stride, query);

   nir_store_global(b, avail_addr, 4, avail_value, 0x1);

   nir_def *zero = nir_imm_int64(b, 0);
   nir_variable *r = nir_local_variable_create(b->impl, glsl_uint_type(), "r");
   nir_store_var(b, r, nir_imm_int(b, 0), 0x1);

   uint32_t qwords_per_report =
      DIV_ROUND_UP(sizeof(struct panvk_query_report), sizeof(uint64_t));

   nir_push_loop(b);
   {
      nir_def *report_idx = nir_load_var(b, r);
      nir_break_if(b, nir_ige(b, report_idx, reports_per_query));

      nir_def *base_addr = nir_iadd(
         b, report_addr,
         nir_i2i64(
            b, nir_imul_imm(b, report_idx, sizeof(struct panvk_query_report))));

      for (uint32_t y = 0; y < qwords_per_report; y++) {
         nir_def *addr = nir_iadd_imm(b, base_addr, y * sizeof(uint64_t));
         nir_store_global(b, addr, 8, zero, 0x1);
      }

      nir_store_var(b, r, nir_iadd_imm(b, report_idx, 1), 0x1);
   }
   nir_pop_loop(b, NULL);
}

static nir_shader *
build_clear_queries_shader(uint32_t max_threads_per_wg)
{
   nir_builder build = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, NULL, "panvk-meta-clear-queries");
   nir_builder *b = &build;

   b->shader->info.workgroup_size[0] = max_threads_per_wg;
   nir_def *wg_id = nir_load_workgroup_id(b);
   nir_def *i =
      nir_iadd(b, nir_load_subgroup_invocation(b),
               nir_imul_imm(b, nir_channel(b, wg_id, 0), max_threads_per_wg));

   nir_def *query_count =
      load_info(b, struct panvk_clear_query_push, query_count);
   nir_push_if(b, nir_ilt(b, i, query_count));
   {
      panvk_nir_clear_query(b, i);
   }
   nir_pop_if(b, NULL);

   return build.shader;
}

static VkResult
get_clear_queries_pipeline(struct panvk_device *dev, const void *key_data,
                           size_t key_size, VkPipelineLayout layout,
                           VkPipeline *pipeline_out)
{
   const struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);

   const VkPipelineShaderStageNirCreateInfoMESA nir_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_NIR_CREATE_INFO_MESA,
      .nir =
         build_clear_queries_shader(phys_dev->kmod.props.max_threads_per_wg),
   };
   const VkComputePipelineCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage =
         {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = &nir_info,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .pName = "main",
         },
      .layout = layout,
   };

   return vk_meta_create_compute_pipeline(&dev->vk, &dev->meta, &info, key_data,
                                          key_size, pipeline_out);
}

static void
panvk_emit_clear_queries(struct panvk_cmd_buffer *cmd,
                         struct panvk_query_pool *pool, bool availaible,
                         uint32_t first_query, uint32_t query_count)
{
   struct panvk_device *dev = to_panvk_device(cmd->vk.base.device);
   const struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   VkResult result;

   const struct panvk_clear_query_push push = {
      .pool_addr = panvk_priv_mem_dev_addr(pool->mem),
      .available_addr = panvk_priv_mem_dev_addr(pool->available_mem),
      .query_stride = pool->query_stride,
      .first_query = first_query,
      .query_count = query_count,
      .reports_per_query = pool->reports_per_query,
      .availaible_value = availaible,
   };

   const enum panvk_meta_object_key_type key =
      PANVK_META_OBJECT_KEY_CLEAR_QUERY_POOL_PIPELINE;
   const VkPushConstantRange push_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = sizeof(push),
   };
   VkPipelineLayout layout;
   result = vk_meta_get_pipeline_layout(&dev->vk, &dev->meta, NULL, &push_range,
                                        &key, sizeof(key), &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   VkPipeline pipeline = vk_meta_lookup_pipeline(&dev->meta, &key, sizeof(key));

   if (pipeline == VK_NULL_HANDLE) {
      result =
         get_clear_queries_pipeline(dev, &key, sizeof(key), layout, &pipeline);

      if (result != VK_SUCCESS) {
         vk_command_buffer_set_error(&cmd->vk, result);
         return;
      }
   }

   /* Save previous cmd state */
   struct panvk_cmd_meta_compute_save_ctx save = {0};
   panvk_per_arch(cmd_meta_compute_start)(cmd, &save);

   dev->vk.dispatch_table.CmdBindPipeline(panvk_cmd_buffer_to_handle(cmd),
                                          VK_PIPELINE_BIND_POINT_COMPUTE,
                                          pipeline);

   dev->vk.dispatch_table.CmdPushConstants(panvk_cmd_buffer_to_handle(cmd),
                                           layout, VK_SHADER_STAGE_COMPUTE_BIT,
                                           0, sizeof(push), &push);

   dev->vk.dispatch_table.CmdDispatchBase(
      panvk_cmd_buffer_to_handle(cmd), 0, 0, 0,
      DIV_ROUND_UP(query_count, phys_dev->kmod.props.max_threads_per_wg), 1, 1);

   /* Restore previous cmd state */
   panvk_per_arch(cmd_meta_compute_end)(cmd, &save);
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

   panvk_emit_clear_queries(cmd, pool, false, firstQuery, queryCount);
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
panvk_per_arch(CmdBeginQueryIndexedEXT)(VkCommandBuffer commandBuffer,
                                        VkQueryPool queryPool, uint32_t query,
                                        VkQueryControlFlags flags,
                                        uint32_t index)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmd, commandBuffer);
   VK_FROM_HANDLE(panvk_query_pool, pool, queryPool);

   /* TODO: transform feedback */
   assert(index == 0);

   bool had_batch;
   struct panvk_batch *batch = open_batch(cmd, &had_batch);
   uint64_t report_addr = panvk_query_report_dev_addr(pool, query);

   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_OCCLUSION: {
      cmd->state.gfx.occlusion_query.ptr = report_addr;
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
      for (unsigned i = 0; i < pool->reports_per_query; i++) {
         panvk_emit_write_job(
            cmd, batch, MALI_WRITE_VALUE_TYPE_IMMEDIATE_64,
            report_addr + i * sizeof(struct panvk_query_report), 0);
      }
      break;
   }
   default:
      unreachable("Unsupported query type");
   }

   close_batch(cmd, had_batch);
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

   bool end_sync = cmd->cur_batch != NULL;

   /* Close to ensure we are sync and flush caches */
   if (end_sync)
      panvk_per_arch(cmd_close_batch)(cmd);

   bool had_batch;
   struct panvk_batch *batch = open_batch(cmd, &had_batch);
   had_batch |= end_sync;

   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_OCCLUSION: {
      cmd->state.gfx.occlusion_query.ptr = 0;
      cmd->state.gfx.occlusion_query.mode = MALI_OCCLUSION_MODE_DISABLED;
      gfx_state_set_dirty(cmd, OQ);
      break;
   }
   default:
      unreachable("Unsupported query type");
   }

   uint64_t available_addr = panvk_query_available_dev_addr(pool, query);
   panvk_emit_write_job(cmd, batch, MALI_WRITE_VALUE_TYPE_IMMEDIATE_32,
                        available_addr, 1);

   close_batch(cmd, had_batch);
}

static void
nir_write_query_result(nir_builder *b, nir_def *dst_addr, nir_def *idx,
                       nir_def *flags, nir_def *result)
{
   assert(result->num_components == 1);
   assert(result->bit_size == 64);

   nir_push_if(b, nir_test_mask(b, flags, VK_QUERY_RESULT_64_BIT));
   {
      nir_def *offset = nir_i2i64(b, nir_imul_imm(b, idx, 8));
      nir_store_global(b, nir_iadd(b, dst_addr, offset), 8, result, 0x1);
   }
   nir_push_else(b, NULL);
   {
      nir_def *result32 = nir_u2u32(b, result);
      nir_def *offset = nir_i2i64(b, nir_imul_imm(b, idx, 4));
      nir_store_global(b, nir_iadd(b, dst_addr, offset), 4, result32, 0x1);
   }
   nir_pop_if(b, NULL);
}

static void
nir_write_occlusion_query_result(nir_builder *b, nir_def *dst_addr,
                                 nir_def *idx, nir_def *flags,
                                 nir_def *report_addr, unsigned core_count)
{
   nir_def *value = nir_imm_int64(b, 0);

   for (unsigned core_idx = 0; core_idx < core_count; core_idx++) {
      /* Start values start at the second entry */
      unsigned report_offset = core_idx * sizeof(struct panvk_query_report);

      value = nir_iadd(
         b, value,
         nir_load_global(
            b, nir_iadd(b, report_addr, nir_imm_int64(b, report_offset)), 8, 1,
            64));
   }

   nir_write_query_result(b, dst_addr, idx, flags, value);
}

struct panvk_copy_query_push {
   uint64_t pool_addr;
   uint32_t available_addr;
   uint32_t query_stride;
   uint32_t first_query;
   uint32_t query_count;
   uint64_t dst_addr;
   uint64_t dst_stride;
   uint32_t flags;
};

static void
panvk_nir_copy_query(nir_builder *b, VkQueryType query_type,
                     unsigned core_count, nir_def *i)
{
   nir_def *pool_addr = load_info(b, struct panvk_copy_query_push, pool_addr);
   nir_def *available_addr =
      nir_u2u64(b, load_info(b, struct panvk_copy_query_push, available_addr));
   nir_def *query_stride =
      load_info(b, struct panvk_copy_query_push, query_stride);
   nir_def *first_query =
      load_info(b, struct panvk_copy_query_push, first_query);
   nir_def *dst_addr = load_info(b, struct panvk_copy_query_push, dst_addr);
   nir_def *dst_stride = load_info(b, struct panvk_copy_query_push, dst_stride);
   nir_def *flags = load_info(b, struct panvk_copy_query_push, flags);

   nir_def *query = nir_iadd(b, first_query, i);

   nir_def *avail_addr = panvk_nir_available_dev_addr(b, available_addr, query);
   nir_def *available = nir_i2b(b, nir_load_global(b, avail_addr, 4, 1, 32));

   nir_def *partial = nir_test_mask(b, flags, VK_QUERY_RESULT_PARTIAL_BIT);
   nir_def *write_results = nir_ior(b, available, partial);

   nir_def *report_addr =
      panvk_nir_query_report_dev_addr(b, pool_addr, query_stride, query);
   nir_def *dst_offset = nir_imul(b, nir_u2u64(b, i), dst_stride);

   nir_push_if(b, write_results);
   {
      switch (query_type) {
      case VK_QUERY_TYPE_OCCLUSION: {
         nir_write_occlusion_query_result(b, nir_iadd(b, dst_addr, dst_offset),
                                          nir_imm_int(b, 0), flags, report_addr,
                                          core_count);
         break;
      }
      default:
         unreachable("Unsupported query type");
      }
   }
   nir_pop_if(b, NULL);

   nir_push_if(b,
               nir_test_mask(b, flags, VK_QUERY_RESULT_WITH_AVAILABILITY_BIT));
   {
      nir_write_query_result(b, nir_iadd(b, dst_addr, dst_offset),
                             nir_imm_int(b, 1), flags, nir_b2i64(b, available));
   }
   nir_pop_if(b, NULL);
}

static nir_shader *
build_copy_queries_shader(VkQueryType query_type, uint32_t max_threads_per_wg,
                          unsigned core_count)
{
   nir_builder build = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, NULL,
      "panvk-meta-copy-queries(query_type=%d,core_count=%u)", query_type,
      core_count);
   nir_builder *b = &build;

   b->shader->info.workgroup_size[0] = max_threads_per_wg;
   nir_def *wg_id = nir_load_workgroup_id(b);
   nir_def *i =
      nir_iadd(b, nir_load_subgroup_invocation(b),
               nir_imul_imm(b, nir_channel(b, wg_id, 0), max_threads_per_wg));

   nir_def *query_count =
      load_info(b, struct panvk_copy_query_push, query_count);
   nir_push_if(b, nir_ilt(b, i, query_count));
   {
      panvk_nir_copy_query(b, query_type, core_count, i);
   }
   nir_pop_if(b, NULL);

   return build.shader;
}

static VkResult
get_copy_queries_pipeline(struct panvk_device *dev, VkQueryType query_type,
                          const void *key_data, size_t key_size,
                          VkPipelineLayout layout, VkPipeline *pipeline_out)
{
   const struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);

   unsigned core_count;
   panfrost_query_core_count(&phys_dev->kmod.props, &core_count);
   const VkPipelineShaderStageNirCreateInfoMESA nir_info = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_NIR_CREATE_INFO_MESA,
      .nir = build_copy_queries_shader(
         query_type, phys_dev->kmod.props.max_threads_per_wg, core_count),
   };
   const VkComputePipelineCreateInfo info = {
      .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
      .stage =
         {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .pNext = &nir_info,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .pName = "main",
         },
      .layout = layout,
   };

   return vk_meta_create_compute_pipeline(&dev->vk, &dev->meta, &info, key_data,
                                          key_size, pipeline_out);
}

static void
panvk_meta_copy_query_pool_results(struct panvk_cmd_buffer *cmd,
                                   struct panvk_query_pool *pool,
                                   uint32_t first_query, uint32_t query_count,
                                   uint64_t dst_addr, uint64_t dst_stride,
                                   VkQueryResultFlags flags)
{
   struct panvk_device *dev = to_panvk_device(cmd->vk.base.device);
   const struct panvk_physical_device *phys_dev =
      to_panvk_physical_device(dev->vk.physical);
   VkResult result;

   const struct panvk_copy_query_push push = {
      .pool_addr = panvk_priv_mem_dev_addr(pool->mem),
      .available_addr = panvk_priv_mem_dev_addr(pool->available_mem),
      .query_stride = pool->query_stride,
      .first_query = first_query,
      .query_count = query_count,
      .dst_addr = dst_addr,
      .dst_stride = dst_stride,
      .flags = flags,
   };

   enum panvk_meta_object_key_type key;

   switch (pool->vk.query_type) {
   case VK_QUERY_TYPE_OCCLUSION: {
      key = PANVK_META_OBJECT_KEY_COPY_QUERY_POOL_RESULTS_OQ_PIPELINE;
      break;
   }
   default:
      unreachable("Unsupported query type");
   }

   const VkPushConstantRange push_range = {
      .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      .size = sizeof(push),
   };
   VkPipelineLayout layout;
   result = vk_meta_get_pipeline_layout(&dev->vk, &dev->meta, NULL, &push_range,
                                        &key, sizeof(key), &layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   VkPipeline pipeline = vk_meta_lookup_pipeline(&dev->meta, &key, sizeof(key));

   if (pipeline == VK_NULL_HANDLE) {
      result = get_copy_queries_pipeline(dev, pool->vk.query_type, &key,
                                         sizeof(key), layout, &pipeline);

      if (result != VK_SUCCESS) {
         vk_command_buffer_set_error(&cmd->vk, result);
         return;
      }
   }

   /* Save previous cmd state */
   struct panvk_cmd_meta_compute_save_ctx save = {0};
   panvk_per_arch(cmd_meta_compute_start)(cmd, &save);

   dev->vk.dispatch_table.CmdBindPipeline(panvk_cmd_buffer_to_handle(cmd),
                                          VK_PIPELINE_BIND_POINT_COMPUTE,
                                          pipeline);

   dev->vk.dispatch_table.CmdPushConstants(panvk_cmd_buffer_to_handle(cmd),
                                           layout, VK_SHADER_STAGE_COMPUTE_BIT,
                                           0, sizeof(push), &push);

   dev->vk.dispatch_table.CmdDispatchBase(
      panvk_cmd_buffer_to_handle(cmd), 0, 0, 0,
      DIV_ROUND_UP(query_count, phys_dev->kmod.props.max_threads_per_wg), 1, 1);

   /* Restore previous cmd state */
   panvk_per_arch(cmd_meta_compute_end)(cmd, &save);
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

   /* XXX: Do we really need that barrier when EndQuery already handle it? */
   if ((flags & VK_QUERY_RESULT_WAIT_BIT) && cmd->cur_batch != NULL) {
      close_batch(cmd, true);
   }

   uint64_t dst_addr = panvk_buffer_gpu_ptr(dst_buffer, dstOffset);
   panvk_meta_copy_query_pool_results(cmd, pool, firstQuery, queryCount,
                                      dst_addr, stride, flags);
}
