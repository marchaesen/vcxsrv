/*
 * Copyright © 2020 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include <inttypes.h>

#include "radv_buffer.h"
#include "radv_cs.h"
#include "radv_debug.h"
#include "radv_entrypoints.h"
#include "radv_perfcounter.h"
#include "radv_spm.h"
#include "radv_sqtt.h"
#include "sid.h"

#include "ac_pm4.h"

#include "vk_command_pool.h"
#include "vk_common_entrypoints.h"

#define SQTT_BUFFER_ALIGN_SHIFT 12

bool
radv_is_instruction_timing_enabled(void)
{
   return debug_get_bool_option("RADV_THREAD_TRACE_INSTRUCTION_TIMING", true);
}

bool
radv_sqtt_queue_events_enabled(void)
{
   return debug_get_bool_option("RADV_THREAD_TRACE_QUEUE_EVENTS", true);
}

static enum radv_queue_family
radv_ip_to_queue_family(enum amd_ip_type t)
{
   switch (t) {
   case AMD_IP_GFX:
      return RADV_QUEUE_GENERAL;
   case AMD_IP_COMPUTE:
      return RADV_QUEUE_COMPUTE;
   case AMD_IP_SDMA:
      return RADV_QUEUE_TRANSFER;
   default:
      unreachable("Unknown IP type");
   }
}

static void
radv_emit_wait_for_idle(const struct radv_device *device, struct radeon_cmdbuf *cs, int family)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const enum radv_queue_family qf = radv_ip_to_queue_family(family);
   enum rgp_flush_bits sqtt_flush_bits = 0;
   radv_cs_emit_cache_flush(
      device->ws, cs, pdev->info.gfx_level, NULL, 0, qf,
      (family == RADV_QUEUE_COMPUTE ? RADV_CMD_FLAG_CS_PARTIAL_FLUSH
                                    : (RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_PS_PARTIAL_FLUSH)) |
         RADV_CMD_FLAG_INV_ICACHE | RADV_CMD_FLAG_INV_SCACHE | RADV_CMD_FLAG_INV_VCACHE | RADV_CMD_FLAG_INV_L2,
      &sqtt_flush_bits, 0);
}

static void
radv_emit_sqtt_start(const struct radv_device *device, struct radeon_cmdbuf *cs, enum radv_queue_family qf)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const bool is_compute_queue = qf == RADV_QUEUE_COMPUTE;
   struct ac_pm4_state *pm4;

   pm4 = ac_pm4_create_sized(&pdev->info, false, 512, is_compute_queue);
   if (!pm4)
      return;

   ac_sqtt_emit_start(&pdev->info, pm4, &device->sqtt, is_compute_queue);
   ac_pm4_finalize(pm4);

   radeon_check_space(device->ws, cs, pm4->ndw);
   radeon_emit_array(cs, pm4->pm4, pm4->ndw);

   ac_pm4_free_state(pm4);
}

static void
radv_emit_sqtt_stop(const struct radv_device *device, struct radeon_cmdbuf *cs, enum radv_queue_family qf)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const bool is_compute_queue = qf == RADV_QUEUE_COMPUTE;
   struct ac_pm4_state *pm4;

   pm4 = ac_pm4_create_sized(&pdev->info, false, 512, is_compute_queue);
   if (!pm4)
      return;

   ac_sqtt_emit_stop(&pdev->info, pm4, is_compute_queue);
   ac_pm4_finalize(pm4);

   radeon_check_space(device->ws, cs, pm4->ndw);
   radeon_emit_array(cs, pm4->pm4, pm4->ndw);

   ac_pm4_clear_state(pm4, &pdev->info, false, is_compute_queue);

   if (pdev->info.has_sqtt_rb_harvest_bug) {
      /* Some chips with disabled RBs should wait for idle because FINISH_DONE doesn't work. */
      radv_emit_wait_for_idle(device, cs, qf);
   }

   ac_sqtt_emit_wait(&pdev->info, pm4, &device->sqtt, is_compute_queue);
   ac_pm4_finalize(pm4);

   radeon_check_space(device->ws, cs, pm4->ndw);
   radeon_emit_array(cs, pm4->pm4, pm4->ndw);

   ac_pm4_free_state(pm4);
}

void
radv_emit_sqtt_userdata(const struct radv_cmd_buffer *cmd_buffer, const void *data, uint32_t num_dwords)
{
   struct radv_device *device = radv_cmd_buffer_device(cmd_buffer);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const enum amd_gfx_level gfx_level = pdev->info.gfx_level;
   const enum radv_queue_family qf = cmd_buffer->qf;
   struct radeon_cmdbuf *cs = cmd_buffer->cs;
   const uint32_t *dwords = (uint32_t *)data;

   /* SQTT user data packets aren't supported on SDMA queues. */
   if (cmd_buffer->qf == RADV_QUEUE_TRANSFER)
      return;

   while (num_dwords > 0) {
      uint32_t count = MIN2(num_dwords, 2);

      radeon_check_space(device->ws, cs, 2 + count);

      /* Without the perfctr bit the CP might not always pass the
       * write on correctly. */
      if (pdev->info.gfx_level >= GFX10)
         radeon_set_uconfig_perfctr_reg_seq(gfx_level, qf, cs, R_030D08_SQ_THREAD_TRACE_USERDATA_2, count);
      else
         radeon_set_uconfig_reg_seq(cs, R_030D08_SQ_THREAD_TRACE_USERDATA_2, count);
      radeon_emit_array(cs, dwords, count);

      dwords += count;
      num_dwords -= count;
   }
}

void
radv_emit_spi_config_cntl(const struct radv_device *device, struct radeon_cmdbuf *cs, bool enable)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   if (pdev->info.gfx_level >= GFX9) {
      uint32_t spi_config_cntl = S_031100_GPR_WRITE_PRIORITY(0x2c688) | S_031100_EXP_PRIORITY_ORDER(3) |
                                 S_031100_ENABLE_SQG_TOP_EVENTS(enable) | S_031100_ENABLE_SQG_BOP_EVENTS(enable);

      if (pdev->info.gfx_level >= GFX10)
         spi_config_cntl |= S_031100_PS_PKR_PRIORITY_CNTL(3);

      radeon_set_uconfig_reg(cs, R_031100_SPI_CONFIG_CNTL, spi_config_cntl);
   } else {
      /* SPI_CONFIG_CNTL is a protected register on GFX6-GFX8. */
      radeon_set_privileged_config_reg(cs, R_009100_SPI_CONFIG_CNTL,
                                       S_009100_ENABLE_SQG_TOP_EVENTS(enable) | S_009100_ENABLE_SQG_BOP_EVENTS(enable));
   }
}

void
radv_emit_inhibit_clockgating(const struct radv_device *device, struct radeon_cmdbuf *cs, bool inhibit)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);

   if (pdev->info.gfx_level >= GFX11)
      return; /* not needed */

   if (pdev->info.gfx_level >= GFX10) {
      radeon_set_uconfig_reg(cs, R_037390_RLC_PERFMON_CLK_CNTL, S_037390_PERFMON_CLOCK_STATE(inhibit));
   } else if (pdev->info.gfx_level >= GFX8) {
      radeon_set_uconfig_reg(cs, R_0372FC_RLC_PERFMON_CLK_CNTL, S_0372FC_PERFMON_CLOCK_STATE(inhibit));
   }
}

VkResult
radv_sqtt_acquire_gpu_timestamp(struct radv_device *device, struct radeon_winsys_bo **gpu_timestamp_bo,
                                uint32_t *gpu_timestamp_offset, void **gpu_timestamp_ptr)
{
   simple_mtx_lock(&device->sqtt_timestamp_mtx);

   if (device->sqtt_timestamp.offset + 8 > device->sqtt_timestamp.size) {
      struct radeon_winsys_bo *bo;
      uint64_t new_size;
      VkResult result;
      uint8_t *map;

      new_size = MAX2(4096, 2 * device->sqtt_timestamp.size);

      result = radv_bo_create(device, NULL, new_size, 8, RADEON_DOMAIN_GTT,
                              RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING, RADV_BO_PRIORITY_SCRATCH, 0,
                              true, &bo);
      if (result != VK_SUCCESS) {
         simple_mtx_unlock(&device->sqtt_timestamp_mtx);
         return result;
      }

      map = radv_buffer_map(device->ws, bo);
      if (!map) {
         radv_bo_destroy(device, NULL, bo);
         simple_mtx_unlock(&device->sqtt_timestamp_mtx);
         return VK_ERROR_OUT_OF_DEVICE_MEMORY;
      }

      if (device->sqtt_timestamp.bo) {
         struct radv_sqtt_timestamp *new_timestamp;

         new_timestamp = malloc(sizeof(*new_timestamp));
         if (!new_timestamp) {
            radv_bo_destroy(device, NULL, bo);
            simple_mtx_unlock(&device->sqtt_timestamp_mtx);
            return VK_ERROR_OUT_OF_HOST_MEMORY;
         }

         memcpy(new_timestamp, &device->sqtt_timestamp, sizeof(*new_timestamp));
         list_add(&new_timestamp->list, &device->sqtt_timestamp.list);
      }

      device->sqtt_timestamp.bo = bo;
      device->sqtt_timestamp.size = new_size;
      device->sqtt_timestamp.offset = 0;
      device->sqtt_timestamp.map = map;
   }

   *gpu_timestamp_bo = device->sqtt_timestamp.bo;
   *gpu_timestamp_offset = device->sqtt_timestamp.offset;
   *gpu_timestamp_ptr = device->sqtt_timestamp.map + device->sqtt_timestamp.offset;

   device->sqtt_timestamp.offset += 8;

   simple_mtx_unlock(&device->sqtt_timestamp_mtx);

   return VK_SUCCESS;
}

static void
radv_sqtt_reset_timestamp(struct radv_device *device)
{
   simple_mtx_lock(&device->sqtt_timestamp_mtx);

   list_for_each_entry_safe (struct radv_sqtt_timestamp, ts, &device->sqtt_timestamp.list, list) {
      radv_bo_destroy(device, NULL, ts->bo);
      list_del(&ts->list);
      free(ts);
   }

   device->sqtt_timestamp.offset = 0;

   simple_mtx_unlock(&device->sqtt_timestamp_mtx);
}

static bool
radv_sqtt_init_queue_event(struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radv_instance *instance = radv_physical_device_instance(pdev);
   VkCommandPool cmd_pool;
   VkResult result;

   const VkCommandPoolCreateInfo create_gfx_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
      .queueFamilyIndex = RADV_QUEUE_GENERAL, /* Graphics queue is always the first queue. */
   };

   result = vk_common_CreateCommandPool(radv_device_to_handle(device), &create_gfx_info, NULL, &cmd_pool);
   if (result != VK_SUCCESS)
      return false;

   device->sqtt_command_pool[0] = vk_command_pool_from_handle(cmd_pool);

   if (!(instance->debug_flags & RADV_DEBUG_NO_COMPUTE_QUEUE)) {
      const VkCommandPoolCreateInfo create_comp_info = {
         .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
         .queueFamilyIndex = RADV_QUEUE_COMPUTE,
      };

      result = vk_common_CreateCommandPool(radv_device_to_handle(device), &create_comp_info, NULL, &cmd_pool);
      if (result != VK_SUCCESS)
         return false;

      device->sqtt_command_pool[1] = vk_command_pool_from_handle(cmd_pool);
   }

   simple_mtx_init(&device->sqtt_command_pool_mtx, mtx_plain);

   simple_mtx_init(&device->sqtt_timestamp_mtx, mtx_plain);
   list_inithead(&device->sqtt_timestamp.list);

   return true;
}

static void
radv_sqtt_finish_queue_event(struct radv_device *device)
{
   if (device->sqtt_timestamp.bo)
      radv_bo_destroy(device, NULL, device->sqtt_timestamp.bo);

   simple_mtx_destroy(&device->sqtt_timestamp_mtx);

   for (unsigned i = 0; i < ARRAY_SIZE(device->sqtt_command_pool); i++)
      vk_common_DestroyCommandPool(radv_device_to_handle(device),
                                   vk_command_pool_to_handle(device->sqtt_command_pool[i]), NULL);

   simple_mtx_destroy(&device->sqtt_command_pool_mtx);
}

static bool
radv_sqtt_init_bo(struct radv_device *device)
{
   const struct radv_physical_device *pdev = radv_device_physical(device);
   unsigned max_se = pdev->info.max_se;
   struct radeon_winsys *ws = device->ws;
   VkResult result;
   uint64_t size;

   /* The buffer size and address need to be aligned in HW regs. Align the
    * size as early as possible so that we do all the allocation & addressing
    * correctly. */
   device->sqtt.buffer_size = align64(device->sqtt.buffer_size, 1u << SQTT_BUFFER_ALIGN_SHIFT);

   /* Compute total size of the thread trace BO for all SEs. */
   size = align64(sizeof(struct ac_sqtt_data_info) * max_se, 1 << SQTT_BUFFER_ALIGN_SHIFT);
   size += device->sqtt.buffer_size * (uint64_t)max_se;

   struct radeon_winsys_bo *bo = NULL;
   result = radv_bo_create(device, NULL, size, 4096, RADEON_DOMAIN_VRAM,
                           RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING | RADEON_FLAG_ZERO_VRAM,
                           RADV_BO_PRIORITY_SCRATCH, 0, true, &bo);
   device->sqtt.bo = bo;
   if (result != VK_SUCCESS)
      return false;

   result = ws->buffer_make_resident(ws, device->sqtt.bo, true);
   if (result != VK_SUCCESS)
      return false;

   device->sqtt.ptr = radv_buffer_map(ws, device->sqtt.bo);
   if (!device->sqtt.ptr)
      return false;

   device->sqtt.buffer_va = radv_buffer_get_va(device->sqtt.bo);

   return true;
}

static void
radv_sqtt_finish_bo(struct radv_device *device)
{
   struct radeon_winsys *ws = device->ws;

   if (unlikely(device->sqtt.bo)) {
      ws->buffer_make_resident(ws, device->sqtt.bo, false);
      radv_bo_destroy(device, NULL, device->sqtt.bo);
   }
}

static VkResult
radv_register_queue(struct radv_device *device, struct radv_queue *queue)
{
   struct ac_sqtt *sqtt = &device->sqtt;
   struct rgp_queue_info *queue_info = &sqtt->rgp_queue_info;
   struct rgp_queue_info_record *record;

   record = malloc(sizeof(struct rgp_queue_info_record));
   if (!record)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   record->queue_id = (uintptr_t)queue;
   record->queue_context = (uintptr_t)queue->hw_ctx;
   if (queue->vk.queue_family_index == RADV_QUEUE_GENERAL) {
      record->hardware_info.queue_type = SQTT_QUEUE_TYPE_UNIVERSAL;
      record->hardware_info.engine_type = SQTT_ENGINE_TYPE_UNIVERSAL;
   } else {
      record->hardware_info.queue_type = SQTT_QUEUE_TYPE_COMPUTE;
      record->hardware_info.engine_type = SQTT_ENGINE_TYPE_COMPUTE;
   }

   simple_mtx_lock(&queue_info->lock);
   list_addtail(&record->list, &queue_info->record);
   queue_info->record_count++;
   simple_mtx_unlock(&queue_info->lock);

   return VK_SUCCESS;
}

static void
radv_unregister_queue(struct radv_device *device, struct radv_queue *queue)
{
   struct ac_sqtt *sqtt = &device->sqtt;
   struct rgp_queue_info *queue_info = &sqtt->rgp_queue_info;

   /* Destroy queue info record. */
   simple_mtx_lock(&queue_info->lock);
   if (queue_info->record_count > 0) {
      list_for_each_entry_safe (struct rgp_queue_info_record, record, &queue_info->record, list) {
         if (record->queue_id == (uintptr_t)queue) {
            queue_info->record_count--;
            list_del(&record->list);
            free(record);
            break;
         }
      }
   }
   simple_mtx_unlock(&queue_info->lock);
}

static void
radv_register_queues(struct radv_device *device, struct ac_sqtt *sqtt)
{
   if (device->queue_count[RADV_QUEUE_GENERAL] == 1)
      radv_register_queue(device, &device->queues[RADV_QUEUE_GENERAL][0]);

   for (uint32_t i = 0; i < device->queue_count[RADV_QUEUE_COMPUTE]; i++)
      radv_register_queue(device, &device->queues[RADV_QUEUE_COMPUTE][i]);
}

static void
radv_unregister_queues(struct radv_device *device, struct ac_sqtt *sqtt)
{
   if (device->queue_count[RADV_QUEUE_GENERAL] == 1)
      radv_unregister_queue(device, &device->queues[RADV_QUEUE_GENERAL][0]);

   for (uint32_t i = 0; i < device->queue_count[RADV_QUEUE_COMPUTE]; i++)
      radv_unregister_queue(device, &device->queues[RADV_QUEUE_COMPUTE][i]);
}

bool
radv_sqtt_init(struct radv_device *device)
{
   struct ac_sqtt *sqtt = &device->sqtt;

   /* Default buffer size set to 32MB per SE. */
   device->sqtt.buffer_size = (uint32_t)debug_get_num_option("RADV_THREAD_TRACE_BUFFER_SIZE", 32 * 1024 * 1024);
   device->sqtt.instruction_timing_enabled = radv_is_instruction_timing_enabled();

   if (!radv_sqtt_init_bo(device))
      return false;

   if (!radv_sqtt_init_queue_event(device))
      return false;

   if (!radv_device_acquire_performance_counters(device))
      return false;

   ac_sqtt_init(sqtt);

   radv_register_queues(device, sqtt);

   return true;
}

void
radv_sqtt_finish(struct radv_device *device)
{
   struct ac_sqtt *sqtt = &device->sqtt;
   struct radeon_winsys *ws = device->ws;

   radv_sqtt_finish_bo(device);
   radv_sqtt_finish_queue_event(device);

   for (unsigned i = 0; i < 2; i++) {
      if (device->sqtt.start_cs[i])
         ws->cs_destroy(device->sqtt.start_cs[i]);
      if (device->sqtt.stop_cs[i])
         ws->cs_destroy(device->sqtt.stop_cs[i]);
   }

   radv_unregister_queues(device, sqtt);

   ac_sqtt_finish(sqtt);
}

static bool
radv_sqtt_resize_bo(struct radv_device *device)
{
   /* Destroy the previous thread trace BO. */
   radv_sqtt_finish_bo(device);

   /* Double the size of the thread trace buffer per SE. */
   device->sqtt.buffer_size *= 2;

   fprintf(stderr,
           "Failed to get the thread trace because the buffer "
           "was too small, resizing to %d KB\n",
           device->sqtt.buffer_size / 1024);

   /* Re-create the thread trace BO. */
   return radv_sqtt_init_bo(device);
}

bool
radv_begin_sqtt(struct radv_queue *queue)
{
   struct radv_device *device = radv_queue_device(queue);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   enum radv_queue_family family = queue->state.qf;
   struct radeon_winsys *ws = device->ws;
   struct radeon_cmdbuf *cs;
   VkResult result;

   /* Destroy the previous start CS and create a new one. */
   if (device->sqtt.start_cs[family]) {
      ws->cs_destroy(device->sqtt.start_cs[family]);
      device->sqtt.start_cs[family] = NULL;
   }

   cs = ws->cs_create(ws, radv_queue_ring(queue), false);
   if (!cs)
      return false;

   radeon_check_space(ws, cs, 512);

   switch (family) {
   case RADV_QUEUE_GENERAL:
      radeon_emit(cs, PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
      radeon_emit(cs, CC0_UPDATE_LOAD_ENABLES(1));
      radeon_emit(cs, CC1_UPDATE_SHADOW_ENABLES(1));
      break;
   case RADV_QUEUE_COMPUTE:
      radeon_emit(cs, PKT3(PKT3_NOP, 0, 0));
      radeon_emit(cs, 0);
      break;
   default:
      unreachable("Incorrect queue family");
      break;
   }

   /* Make sure to wait-for-idle before starting SQTT. */
   radv_emit_wait_for_idle(device, cs, family);

   /* Disable clock gating before starting SQTT. */
   radv_emit_inhibit_clockgating(device, cs, true);

   /* Enable SQG events that collects thread trace data. */
   radv_emit_spi_config_cntl(device, cs, true);

   radv_perfcounter_emit_spm_reset(cs);

   if (device->spm.bo) {
      /* Enable all shader stages by default. */
      radv_perfcounter_emit_shaders(device, cs, ac_sqtt_get_shader_mask(&pdev->info));

      radv_emit_spm_setup(device, cs, family);
   }

   /* Start SQTT. */
   radv_emit_sqtt_start(device, cs, family);

   if (device->spm.bo) {
      radeon_check_space(ws, cs, 8);
      radv_perfcounter_emit_spm_start(device, cs, family);
   }

   result = ws->cs_finalize(cs);
   if (result != VK_SUCCESS) {
      ws->cs_destroy(cs);
      return false;
   }

   device->sqtt.start_cs[family] = cs;

   return radv_queue_internal_submit(queue, cs);
}

bool
radv_end_sqtt(struct radv_queue *queue)
{
   struct radv_device *device = radv_queue_device(queue);
   enum radv_queue_family family = queue->state.qf;
   struct radeon_winsys *ws = device->ws;
   struct radeon_cmdbuf *cs;
   VkResult result;

   /* Destroy the previous stop CS and create a new one. */
   if (device->sqtt.stop_cs[family]) {
      ws->cs_destroy(device->sqtt.stop_cs[family]);
      device->sqtt.stop_cs[family] = NULL;
   }

   cs = ws->cs_create(ws, radv_queue_ring(queue), false);
   if (!cs)
      return false;

   radeon_check_space(ws, cs, 512);

   switch (family) {
   case RADV_QUEUE_GENERAL:
      radeon_emit(cs, PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
      radeon_emit(cs, CC0_UPDATE_LOAD_ENABLES(1));
      radeon_emit(cs, CC1_UPDATE_SHADOW_ENABLES(1));
      break;
   case RADV_QUEUE_COMPUTE:
      radeon_emit(cs, PKT3(PKT3_NOP, 0, 0));
      radeon_emit(cs, 0);
      break;
   default:
      unreachable("Incorrect queue family");
      break;
   }

   /* Make sure to wait-for-idle before stopping SQTT. */
   radv_emit_wait_for_idle(device, cs, family);

   if (device->spm.bo) {
      radeon_check_space(ws, cs, 8);
      radv_perfcounter_emit_spm_stop(device, cs, family);
   }

   /* Stop SQTT. */
   radv_emit_sqtt_stop(device, cs, family);

   radv_perfcounter_emit_spm_reset(cs);

   /* Restore previous state by disabling SQG events. */
   radv_emit_spi_config_cntl(device, cs, false);

   /* Restore previous state by re-enabling clock gating. */
   radv_emit_inhibit_clockgating(device, cs, false);

   result = ws->cs_finalize(cs);
   if (result != VK_SUCCESS) {
      ws->cs_destroy(cs);
      return false;
   }

   device->sqtt.stop_cs[family] = cs;

   return radv_queue_internal_submit(queue, cs);
}

bool
radv_get_sqtt_trace(struct radv_queue *queue, struct ac_sqtt_trace *sqtt_trace)
{
   struct radv_device *device = radv_queue_device(queue);
   const struct radv_physical_device *pdev = radv_device_physical(device);
   const struct radeon_info *gpu_info = &pdev->info;

   if (!ac_sqtt_get_trace(&device->sqtt, gpu_info, sqtt_trace)) {
      if (!radv_sqtt_resize_bo(device))
         fprintf(stderr, "radv: Failed to resize the SQTT buffer.\n");
      return false;
   }

   return true;
}

void
radv_reset_sqtt_trace(struct radv_device *device)
{
   struct ac_sqtt *sqtt = &device->sqtt;
   struct rgp_clock_calibration *clock_calibration = &sqtt->rgp_clock_calibration;
   struct rgp_queue_event *queue_event = &sqtt->rgp_queue_event;

   /* Clear clock calibration records. */
   simple_mtx_lock(&clock_calibration->lock);
   list_for_each_entry_safe (struct rgp_clock_calibration_record, record, &clock_calibration->record, list) {
      clock_calibration->record_count--;
      list_del(&record->list);
      free(record);
   }
   simple_mtx_unlock(&clock_calibration->lock);

   /* Clear queue event records. */
   simple_mtx_lock(&queue_event->lock);
   list_for_each_entry_safe (struct rgp_queue_event_record, record, &queue_event->record, list) {
      list_del(&record->list);
      free(record);
   }
   queue_event->record_count = 0;
   simple_mtx_unlock(&queue_event->lock);

   /* Clear timestamps. */
   radv_sqtt_reset_timestamp(device);

   /* Clear timed cmdbufs. */
   simple_mtx_lock(&device->sqtt_command_pool_mtx);
   for (unsigned i = 0; i < ARRAY_SIZE(device->sqtt_command_pool); i++) {
      /* If RADV_DEBUG_NO_COMPUTE_QUEUE is used, there's no compute sqtt command pool */
      if (device->sqtt_command_pool[i])
         vk_common_TrimCommandPool(radv_device_to_handle(device), vk_command_pool_to_handle(device->sqtt_command_pool[i]),
                                0);
   }
   simple_mtx_unlock(&device->sqtt_command_pool_mtx);
}

static VkResult
radv_get_calibrated_timestamps(struct radv_device *device, uint64_t *cpu_timestamp, uint64_t *gpu_timestamp)
{
   uint64_t timestamps[2];
   uint64_t max_deviation;
   VkResult result;

   const VkCalibratedTimestampInfoKHR timestamp_infos[2] = {{
                                                               .sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
                                                               .timeDomain = VK_TIME_DOMAIN_CLOCK_MONOTONIC_KHR,
                                                            },
                                                            {
                                                               .sType = VK_STRUCTURE_TYPE_CALIBRATED_TIMESTAMP_INFO_KHR,
                                                               .timeDomain = VK_TIME_DOMAIN_DEVICE_KHR,
                                                            }};

   result =
      radv_GetCalibratedTimestampsKHR(radv_device_to_handle(device), 2, timestamp_infos, timestamps, &max_deviation);
   if (result != VK_SUCCESS)
      return result;

   *cpu_timestamp = timestamps[0];
   *gpu_timestamp = timestamps[1];

   return result;
}

bool
radv_sqtt_sample_clocks(struct radv_device *device)
{
   uint64_t cpu_timestamp = 0, gpu_timestamp = 0;
   VkResult result;

   result = radv_get_calibrated_timestamps(device, &cpu_timestamp, &gpu_timestamp);
   if (result != VK_SUCCESS)
      return false;

   return ac_sqtt_add_clock_calibration(&device->sqtt, cpu_timestamp, gpu_timestamp);
}

VkResult
radv_sqtt_get_timed_cmdbuf(struct radv_queue *queue, struct radeon_winsys_bo *timestamp_bo, uint32_t timestamp_offset,
                           VkPipelineStageFlags2 timestamp_stage, VkCommandBuffer *pcmdbuf)
{
   struct radv_device *device = radv_queue_device(queue);
   enum radv_queue_family queue_family = queue->state.qf;
   VkCommandBuffer cmdbuf;
   uint64_t timestamp_va;
   VkResult result;

   assert(queue_family == RADV_QUEUE_GENERAL || queue_family == RADV_QUEUE_COMPUTE);

   simple_mtx_lock(&device->sqtt_command_pool_mtx);

   const VkCommandBufferAllocateInfo alloc_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
      .commandPool = vk_command_pool_to_handle(device->sqtt_command_pool[queue_family]),
      .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
      .commandBufferCount = 1,
   };

   result = vk_common_AllocateCommandBuffers(radv_device_to_handle(device), &alloc_info, &cmdbuf);
   if (result != VK_SUCCESS)
      goto fail;

   const VkCommandBufferBeginInfo begin_info = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
      .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
   };

   result = radv_BeginCommandBuffer(cmdbuf, &begin_info);
   if (result != VK_SUCCESS)
      goto fail;

   radeon_check_space(device->ws, radv_cmd_buffer_from_handle(cmdbuf)->cs, 28);

   timestamp_va = radv_buffer_get_va(timestamp_bo) + timestamp_offset;

   radv_cs_add_buffer(device->ws, radv_cmd_buffer_from_handle(cmdbuf)->cs, timestamp_bo);

   radv_write_timestamp(radv_cmd_buffer_from_handle(cmdbuf), timestamp_va, timestamp_stage);

   result = radv_EndCommandBuffer(cmdbuf);
   if (result != VK_SUCCESS)
      goto fail;

   *pcmdbuf = cmdbuf;

fail:
   simple_mtx_unlock(&device->sqtt_command_pool_mtx);
   return result;
}
