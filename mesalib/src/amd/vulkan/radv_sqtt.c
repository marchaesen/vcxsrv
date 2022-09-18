/*
 * Copyright © 2020 Valve Corporation
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

#include <inttypes.h>

#include "radv_cs.h"
#include "radv_private.h"
#include "sid.h"

#define SQTT_BUFFER_ALIGN_SHIFT 12

bool
radv_is_instruction_timing_enabled(void)
{
   return debug_get_bool_option("RADV_THREAD_TRACE_INSTRUCTION_TIMING", true);
}

static bool
radv_se_is_disabled(struct radv_device *device, unsigned se)
{
   /* No active CU on the SE means it is disabled. */
   return device->physical_device->rad_info.cu_mask[se][0] == 0;
}

static uint32_t
gfx10_get_thread_trace_ctrl(struct radv_device *device, bool enable)
{
   uint32_t thread_trace_ctrl = S_008D1C_MODE(enable) | S_008D1C_HIWATER(5) |
                                S_008D1C_UTIL_TIMER(1) | S_008D1C_RT_FREQ(2) | /* 4096 clk */
                                S_008D1C_DRAW_EVENT_EN(1) | S_008D1C_REG_STALL_EN(1) |
                                S_008D1C_SPI_STALL_EN(1) | S_008D1C_SQ_STALL_EN(1) |
                                S_008D1C_REG_DROP_ON_STALL(0);

   if (device->physical_device->rad_info.gfx_level == GFX10_3)
      thread_trace_ctrl |= S_008D1C_LOWATER_OFFSET(4);

   if (device->physical_device->rad_info.has_sqtt_auto_flush_mode_bug)
      thread_trace_ctrl |= S_008D1C_AUTO_FLUSH_MODE(1);

   return thread_trace_ctrl;
}

static void
radv_emit_wait_for_idle(struct radv_device *device, struct radeon_cmdbuf *cs, int family)
{
   enum rgp_flush_bits sqtt_flush_bits = 0;
   si_cs_emit_cache_flush(
      cs, device->physical_device->rad_info.gfx_level, NULL, 0,
      family == AMD_IP_COMPUTE && device->physical_device->rad_info.gfx_level >= GFX7,
      (family == RADV_QUEUE_COMPUTE
          ? RADV_CMD_FLAG_CS_PARTIAL_FLUSH
          : (RADV_CMD_FLAG_CS_PARTIAL_FLUSH | RADV_CMD_FLAG_PS_PARTIAL_FLUSH)) |
         RADV_CMD_FLAG_INV_ICACHE | RADV_CMD_FLAG_INV_SCACHE | RADV_CMD_FLAG_INV_VCACHE |
         RADV_CMD_FLAG_INV_L2,
      &sqtt_flush_bits, 0);
}

static void
radv_emit_thread_trace_start(struct radv_device *device, struct radeon_cmdbuf *cs,
                             enum radv_queue_family qf)
{
   uint32_t shifted_size = device->thread_trace.buffer_size >> SQTT_BUFFER_ALIGN_SHIFT;
   struct radeon_info *rad_info = &device->physical_device->rad_info;
   unsigned max_se = rad_info->max_se;

   for (unsigned se = 0; se < max_se; se++) {
      uint64_t va = radv_buffer_get_va(device->thread_trace.bo);
      uint64_t data_va = ac_thread_trace_get_data_va(rad_info, &device->thread_trace, va, se);
      uint64_t shifted_va = data_va >> SQTT_BUFFER_ALIGN_SHIFT;
      int first_active_cu = ffs(device->physical_device->rad_info.cu_mask[se][0]);

      if (radv_se_is_disabled(device, se))
         continue;

      /* Target SEx and SH0. */
      radeon_set_uconfig_reg(
         cs, R_030800_GRBM_GFX_INDEX,
         S_030800_SE_INDEX(se) | S_030800_SH_INDEX(0) | S_030800_INSTANCE_BROADCAST_WRITES(1));

      if (device->physical_device->rad_info.gfx_level >= GFX10) {
         /* Order seems important for the following 2 registers. */
         radeon_set_privileged_config_reg(
            cs, R_008D04_SQ_THREAD_TRACE_BUF0_SIZE,
            S_008D04_SIZE(shifted_size) | S_008D04_BASE_HI(shifted_va >> 32));

         radeon_set_privileged_config_reg(cs, R_008D00_SQ_THREAD_TRACE_BUF0_BASE, shifted_va);

         radeon_set_privileged_config_reg(
            cs, R_008D14_SQ_THREAD_TRACE_MASK,
            S_008D14_WTYPE_INCLUDE(0x7f) | /* all shader stages */
               S_008D14_SA_SEL(0) | S_008D14_WGP_SEL(first_active_cu / 2) | S_008D14_SIMD_SEL(0));

         uint32_t thread_trace_token_mask = S_008D18_REG_INCLUDE(
            V_008D18_REG_INCLUDE_SQDEC | V_008D18_REG_INCLUDE_SHDEC | V_008D18_REG_INCLUDE_GFXUDEC |
            V_008D18_REG_INCLUDE_COMP | V_008D18_REG_INCLUDE_CONTEXT | V_008D18_REG_INCLUDE_CONFIG);

         /* Performance counters with SQTT are considered deprecated. */
         uint32_t token_exclude = V_008D18_TOKEN_EXCLUDE_PERF;

         if (!radv_is_instruction_timing_enabled()) {
            /* Reduce SQTT traffic when instruction timing isn't enabled. */
            token_exclude |= V_008D18_TOKEN_EXCLUDE_VMEMEXEC |
                             V_008D18_TOKEN_EXCLUDE_ALUEXEC |
                             V_008D18_TOKEN_EXCLUDE_VALUINST |
                             V_008D18_TOKEN_EXCLUDE_IMMEDIATE |
                             V_008D18_TOKEN_EXCLUDE_INST;
         }
         thread_trace_token_mask |= S_008D18_TOKEN_EXCLUDE(token_exclude);

         radeon_set_privileged_config_reg(cs, R_008D18_SQ_THREAD_TRACE_TOKEN_MASK,
                                          thread_trace_token_mask);

         /* Should be emitted last (it enables thread traces). */
         radeon_set_privileged_config_reg(cs, R_008D1C_SQ_THREAD_TRACE_CTRL,
                                          gfx10_get_thread_trace_ctrl(device, true));
      } else {
         /* Order seems important for the following 4 registers. */
         radeon_set_uconfig_reg(cs, R_030CDC_SQ_THREAD_TRACE_BASE2,
                                S_030CDC_ADDR_HI(shifted_va >> 32));

         radeon_set_uconfig_reg(cs, R_030CC0_SQ_THREAD_TRACE_BASE, shifted_va);

         radeon_set_uconfig_reg(cs, R_030CC4_SQ_THREAD_TRACE_SIZE, S_030CC4_SIZE(shifted_size));

         radeon_set_uconfig_reg(cs, R_030CD4_SQ_THREAD_TRACE_CTRL, S_030CD4_RESET_BUFFER(1));

         uint32_t thread_trace_mask = S_030CC8_CU_SEL(first_active_cu) | S_030CC8_SH_SEL(0) |
                                      S_030CC8_SIMD_EN(0xf) | S_030CC8_VM_ID_MASK(0) |
                                      S_030CC8_REG_STALL_EN(1) | S_030CC8_SPI_STALL_EN(1) |
                                      S_030CC8_SQ_STALL_EN(1);

         if (device->physical_device->rad_info.gfx_level < GFX9) {
            thread_trace_mask |= S_030CC8_RANDOM_SEED(0xffff);
         }

         radeon_set_uconfig_reg(cs, R_030CC8_SQ_THREAD_TRACE_MASK, thread_trace_mask);

         /* Trace all tokens and registers. */
         radeon_set_uconfig_reg(
            cs, R_030CCC_SQ_THREAD_TRACE_TOKEN_MASK,
            S_030CCC_TOKEN_MASK(0xbfff) | S_030CCC_REG_MASK(0xff) | S_030CCC_REG_DROP_ON_STALL(0));

         /* Enable SQTT perf counters for all CUs. */
         radeon_set_uconfig_reg(cs, R_030CD0_SQ_THREAD_TRACE_PERF_MASK,
                                S_030CD0_SH0_MASK(0xffff) | S_030CD0_SH1_MASK(0xffff));

         radeon_set_uconfig_reg(cs, R_030CE0_SQ_THREAD_TRACE_TOKEN_MASK2, 0xffffffff);

         radeon_set_uconfig_reg(cs, R_030CEC_SQ_THREAD_TRACE_HIWATER, S_030CEC_HIWATER(4));

         if (device->physical_device->rad_info.gfx_level == GFX9) {
            /* Reset thread trace status errors. */
            radeon_set_uconfig_reg(cs, R_030CE8_SQ_THREAD_TRACE_STATUS, S_030CE8_UTC_ERROR(0));
         }

         /* Enable the thread trace mode. */
         uint32_t thread_trace_mode =
            S_030CD8_MASK_PS(1) | S_030CD8_MASK_VS(1) | S_030CD8_MASK_GS(1) | S_030CD8_MASK_ES(1) |
            S_030CD8_MASK_HS(1) | S_030CD8_MASK_LS(1) | S_030CD8_MASK_CS(1) |
            S_030CD8_AUTOFLUSH_EN(1) | /* periodically flush SQTT data to memory */
            S_030CD8_MODE(1);

         if (device->physical_device->rad_info.gfx_level == GFX9) {
            /* Count SQTT traffic in TCC perf counters. */
            thread_trace_mode |= S_030CD8_TC_PERF_EN(1);
         }

         radeon_set_uconfig_reg(cs, R_030CD8_SQ_THREAD_TRACE_MODE, thread_trace_mode);
      }
   }

   /* Restore global broadcasting. */
   radeon_set_uconfig_reg(cs, R_030800_GRBM_GFX_INDEX,
                          S_030800_SE_BROADCAST_WRITES(1) | S_030800_SH_BROADCAST_WRITES(1) |
                             S_030800_INSTANCE_BROADCAST_WRITES(1));

   /* Start the thread trace with a different event based on the queue. */
   if (qf == RADV_QUEUE_COMPUTE) {
      radeon_set_sh_reg(cs, R_00B878_COMPUTE_THREAD_TRACE_ENABLE, S_00B878_THREAD_TRACE_ENABLE(1));
   } else {
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_THREAD_TRACE_START) | EVENT_INDEX(0));
   }
}

static const uint32_t gfx8_thread_trace_info_regs[] = {
   R_030CE4_SQ_THREAD_TRACE_WPTR,
   R_030CE8_SQ_THREAD_TRACE_STATUS,
   R_008E40_SQ_THREAD_TRACE_CNTR,
};

static const uint32_t gfx9_thread_trace_info_regs[] = {
   R_030CE4_SQ_THREAD_TRACE_WPTR,
   R_030CE8_SQ_THREAD_TRACE_STATUS,
   R_030CF0_SQ_THREAD_TRACE_CNTR,
};

static const uint32_t gfx10_thread_trace_info_regs[] = {
   R_008D10_SQ_THREAD_TRACE_WPTR,
   R_008D20_SQ_THREAD_TRACE_STATUS,
   R_008D24_SQ_THREAD_TRACE_DROPPED_CNTR,
};

static void
radv_copy_thread_trace_info_regs(struct radv_device *device, struct radeon_cmdbuf *cs,
                                 unsigned se_index)
{
   const uint32_t *thread_trace_info_regs = NULL;

   if (device->physical_device->rad_info.gfx_level >= GFX10) {
      thread_trace_info_regs = gfx10_thread_trace_info_regs;
   } else if (device->physical_device->rad_info.gfx_level == GFX9) {
      thread_trace_info_regs = gfx9_thread_trace_info_regs;
   } else {
      assert(device->physical_device->rad_info.gfx_level == GFX8);
      thread_trace_info_regs = gfx8_thread_trace_info_regs;
   }

   /* Get the VA where the info struct is stored for this SE. */
   uint64_t va = radv_buffer_get_va(device->thread_trace.bo);
   uint64_t info_va = ac_thread_trace_get_info_va(va, se_index);

   /* Copy back the info struct one DWORD at a time. */
   for (unsigned i = 0; i < 3; i++) {
      radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
      radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_PERF) | COPY_DATA_DST_SEL(COPY_DATA_TC_L2) |
                         COPY_DATA_WR_CONFIRM);
      radeon_emit(cs, thread_trace_info_regs[i] >> 2);
      radeon_emit(cs, 0); /* unused */
      radeon_emit(cs, (info_va + i * 4));
      radeon_emit(cs, (info_va + i * 4) >> 32);
   }
}

static void
radv_emit_thread_trace_stop(struct radv_device *device, struct radeon_cmdbuf *cs,
                            enum radv_queue_family qf)
{
   unsigned max_se = device->physical_device->rad_info.max_se;

   /* Stop the thread trace with a different event based on the queue. */
   if (qf == RADV_QUEUE_COMPUTE) {
      radeon_set_sh_reg(cs, R_00B878_COMPUTE_THREAD_TRACE_ENABLE, S_00B878_THREAD_TRACE_ENABLE(0));
   } else {
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_THREAD_TRACE_STOP) | EVENT_INDEX(0));
   }

   radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
   radeon_emit(cs, EVENT_TYPE(V_028A90_THREAD_TRACE_FINISH) | EVENT_INDEX(0));

   if (device->physical_device->rad_info.has_sqtt_rb_harvest_bug) {
      /* Some chips with disabled RBs should wait for idle because FINISH_DONE doesn't work. */
      radv_emit_wait_for_idle(device, cs, qf);
   }

   for (unsigned se = 0; se < max_se; se++) {
      if (radv_se_is_disabled(device, se))
         continue;

      /* Target SEi and SH0. */
      radeon_set_uconfig_reg(
         cs, R_030800_GRBM_GFX_INDEX,
         S_030800_SE_INDEX(se) | S_030800_SH_INDEX(0) | S_030800_INSTANCE_BROADCAST_WRITES(1));

      if (device->physical_device->rad_info.gfx_level >= GFX10) {
         if (!device->physical_device->rad_info.has_sqtt_rb_harvest_bug) {
            /* Make sure to wait for the trace buffer. */
            radeon_emit(cs, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
            radeon_emit(
               cs,
               WAIT_REG_MEM_NOT_EQUAL); /* wait until the register is equal to the reference value */
            radeon_emit(cs, R_008D20_SQ_THREAD_TRACE_STATUS >> 2); /* register */
            radeon_emit(cs, 0);
            radeon_emit(cs, 0);                       /* reference value */
            radeon_emit(cs, ~C_008D20_FINISH_DONE);
            radeon_emit(cs, 4);                       /* poll interval */
         }

         /* Disable the thread trace mode. */
         radeon_set_privileged_config_reg(cs, R_008D1C_SQ_THREAD_TRACE_CTRL,
                                          gfx10_get_thread_trace_ctrl(device, false));

         /* Wait for thread trace completion. */
         radeon_emit(cs, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
         radeon_emit(
            cs, WAIT_REG_MEM_EQUAL); /* wait until the register is equal to the reference value */
         radeon_emit(cs, R_008D20_SQ_THREAD_TRACE_STATUS >> 2); /* register */
         radeon_emit(cs, 0);
         radeon_emit(cs, 0);                /* reference value */
         radeon_emit(cs, ~C_008D20_BUSY); /* mask */
         radeon_emit(cs, 4);                /* poll interval */
      } else {
         /* Disable the thread trace mode. */
         radeon_set_uconfig_reg(cs, R_030CD8_SQ_THREAD_TRACE_MODE, S_030CD8_MODE(0));

         /* Wait for thread trace completion. */
         radeon_emit(cs, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
         radeon_emit(
            cs, WAIT_REG_MEM_EQUAL); /* wait until the register is equal to the reference value */
         radeon_emit(cs, R_030CE8_SQ_THREAD_TRACE_STATUS >> 2); /* register */
         radeon_emit(cs, 0);
         radeon_emit(cs, 0);                /* reference value */
         radeon_emit(cs, ~C_030CE8_BUSY); /* mask */
         radeon_emit(cs, 4);                /* poll interval */
      }

      radv_copy_thread_trace_info_regs(device, cs, se);
   }

   /* Restore global broadcasting. */
   radeon_set_uconfig_reg(cs, R_030800_GRBM_GFX_INDEX,
                          S_030800_SE_BROADCAST_WRITES(1) | S_030800_SH_BROADCAST_WRITES(1) |
                             S_030800_INSTANCE_BROADCAST_WRITES(1));
}

void
radv_emit_thread_trace_userdata(struct radv_cmd_buffer *cmd_buffer, const void *data,
                                uint32_t num_dwords)
{
   struct radv_device *device = cmd_buffer->device;
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
      if (device->physical_device->rad_info.gfx_level >= GFX10)
         radeon_set_uconfig_reg_seq_perfctr(cs, R_030D08_SQ_THREAD_TRACE_USERDATA_2, count);
      else
         radeon_set_uconfig_reg_seq(cs, R_030D08_SQ_THREAD_TRACE_USERDATA_2, count);
      radeon_emit_array(cs, dwords, count);

      dwords += count;
      num_dwords -= count;
   }
}

void
radv_emit_spi_config_cntl(struct radv_device *device, struct radeon_cmdbuf *cs, bool enable)
{
   if (device->physical_device->rad_info.gfx_level >= GFX9) {
      uint32_t spi_config_cntl =
         S_031100_GPR_WRITE_PRIORITY(0x2c688) | S_031100_EXP_PRIORITY_ORDER(3) |
         S_031100_ENABLE_SQG_TOP_EVENTS(enable) | S_031100_ENABLE_SQG_BOP_EVENTS(enable);

      if (device->physical_device->rad_info.gfx_level >= GFX10)
         spi_config_cntl |= S_031100_PS_PKR_PRIORITY_CNTL(3);

      radeon_set_uconfig_reg(cs, R_031100_SPI_CONFIG_CNTL, spi_config_cntl);
   } else {
      /* SPI_CONFIG_CNTL is a protected register on GFX6-GFX8. */
      radeon_set_privileged_config_reg(
         cs, R_009100_SPI_CONFIG_CNTL,
         S_009100_ENABLE_SQG_TOP_EVENTS(enable) | S_009100_ENABLE_SQG_BOP_EVENTS(enable));
   }
}

void
radv_emit_inhibit_clockgating(struct radv_device *device, struct radeon_cmdbuf *cs, bool inhibit)
{
   if (device->physical_device->rad_info.gfx_level >= GFX11)
      return; /* not needed */

   if (device->physical_device->rad_info.gfx_level >= GFX10) {
      radeon_set_uconfig_reg(cs, R_037390_RLC_PERFMON_CLK_CNTL,
                             S_037390_PERFMON_CLOCK_STATE(inhibit));
   } else if (device->physical_device->rad_info.gfx_level >= GFX8) {
      radeon_set_uconfig_reg(cs, R_0372FC_RLC_PERFMON_CLK_CNTL,
                             S_0372FC_PERFMON_CLOCK_STATE(inhibit));
   }
}

static bool
radv_thread_trace_init_bo(struct radv_device *device)
{
   unsigned max_se = device->physical_device->rad_info.max_se;
   struct radeon_winsys *ws = device->ws;
   VkResult result;
   uint64_t size;

   /* The buffer size and address need to be aligned in HW regs. Align the
    * size as early as possible so that we do all the allocation & addressing
    * correctly. */
   device->thread_trace.buffer_size =
      align64(device->thread_trace.buffer_size, 1u << SQTT_BUFFER_ALIGN_SHIFT);

   /* Compute total size of the thread trace BO for all SEs. */
   size = align64(sizeof(struct ac_thread_trace_info) * max_se, 1 << SQTT_BUFFER_ALIGN_SHIFT);
   size += device->thread_trace.buffer_size * (uint64_t)max_se;

   struct radeon_winsys_bo *bo = NULL;
   result = ws->buffer_create(
      ws, size, 4096, RADEON_DOMAIN_VRAM,
      RADEON_FLAG_CPU_ACCESS | RADEON_FLAG_NO_INTERPROCESS_SHARING | RADEON_FLAG_ZERO_VRAM,
      RADV_BO_PRIORITY_SCRATCH, 0, &bo);
   device->thread_trace.bo = bo;
   if (result != VK_SUCCESS)
      return false;

   result = ws->buffer_make_resident(ws, device->thread_trace.bo, true);
   if (result != VK_SUCCESS)
      return false;

   device->thread_trace.ptr = ws->buffer_map(device->thread_trace.bo);
   if (!device->thread_trace.ptr)
      return false;

   return true;
}

static void
radv_thread_trace_finish_bo(struct radv_device *device)
{
   struct radeon_winsys *ws = device->ws;

   if (unlikely(device->thread_trace.bo)) {
      ws->buffer_make_resident(ws, device->thread_trace.bo, false);
      ws->buffer_destroy(ws, device->thread_trace.bo);
   }
}

bool
radv_thread_trace_init(struct radv_device *device)
{
   struct ac_thread_trace_data *thread_trace_data = &device->thread_trace;

   /* Default buffer size set to 32MB per SE. */
   device->thread_trace.buffer_size =
      radv_get_int_debug_option("RADV_THREAD_TRACE_BUFFER_SIZE", 32 * 1024 * 1024);
   device->thread_trace.start_frame = radv_get_int_debug_option("RADV_THREAD_TRACE", -1);

   const char *trigger_file = getenv("RADV_THREAD_TRACE_TRIGGER");
   if (trigger_file)
      device->thread_trace.trigger_file = strdup(trigger_file);

   if (!radv_thread_trace_init_bo(device))
      return false;

   if (!radv_device_acquire_performance_counters(device))
      return false;

   list_inithead(&thread_trace_data->rgp_pso_correlation.record);
   simple_mtx_init(&thread_trace_data->rgp_pso_correlation.lock, mtx_plain);

   list_inithead(&thread_trace_data->rgp_loader_events.record);
   simple_mtx_init(&thread_trace_data->rgp_loader_events.lock, mtx_plain);

   list_inithead(&thread_trace_data->rgp_code_object.record);
   simple_mtx_init(&thread_trace_data->rgp_code_object.lock, mtx_plain);

   return true;
}

void
radv_thread_trace_finish(struct radv_device *device)
{
   struct ac_thread_trace_data *thread_trace_data = &device->thread_trace;
   struct radeon_winsys *ws = device->ws;

   free(device->thread_trace.trigger_file);

   radv_thread_trace_finish_bo(device);

   for (unsigned i = 0; i < 2; i++) {
      if (device->thread_trace.start_cs[i])
         ws->cs_destroy(device->thread_trace.start_cs[i]);
      if (device->thread_trace.stop_cs[i])
         ws->cs_destroy(device->thread_trace.stop_cs[i]);
   }

   assert(thread_trace_data->rgp_pso_correlation.record_count == 0);
   simple_mtx_destroy(&thread_trace_data->rgp_pso_correlation.lock);

   assert(thread_trace_data->rgp_loader_events.record_count == 0);
   simple_mtx_destroy(&thread_trace_data->rgp_loader_events.lock);

   assert(thread_trace_data->rgp_code_object.record_count == 0);
   simple_mtx_destroy(&thread_trace_data->rgp_code_object.lock);
}

static bool
radv_thread_trace_resize_bo(struct radv_device *device)
{
   /* Destroy the previous thread trace BO. */
   radv_thread_trace_finish_bo(device);

   /* Double the size of the thread trace buffer per SE. */
   device->thread_trace.buffer_size *= 2;

   fprintf(stderr,
           "Failed to get the thread trace because the buffer "
           "was too small, resizing to %d KB\n",
           device->thread_trace.buffer_size / 1024);

   /* Re-create the thread trace BO. */
   return radv_thread_trace_init_bo(device);
}

bool
radv_begin_thread_trace(struct radv_queue *queue)
{
   struct radv_device *device = queue->device;
   enum radv_queue_family family = queue->state.qf;
   struct radeon_winsys *ws = device->ws;
   struct radeon_cmdbuf *cs;
   VkResult result;

   /* Destroy the previous start CS and create a new one. */
   if (device->thread_trace.start_cs[family]) {
      ws->cs_destroy(device->thread_trace.start_cs[family]);
      device->thread_trace.start_cs[family] = NULL;
   }

   cs = ws->cs_create(ws, radv_queue_ring(queue));
   if (!cs)
      return false;

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

   if (device->spm_trace.bo) {
      /* Enable all shader stages by default. */
      radv_perfcounter_emit_shaders(cs, 0x7f);

      radv_emit_spm_setup(device, cs);
   }

   /* Start SQTT. */
   radv_emit_thread_trace_start(device, cs, family);

   if (device->spm_trace.bo)
      radv_perfcounter_emit_spm_start(device, cs, family);

   result = ws->cs_finalize(cs);
   if (result != VK_SUCCESS) {
      ws->cs_destroy(cs);
      return false;
   }

   device->thread_trace.start_cs[family] = cs;

   return radv_queue_internal_submit(queue, cs);
}

bool
radv_end_thread_trace(struct radv_queue *queue)
{
   struct radv_device *device = queue->device;
   enum radv_queue_family family = queue->state.qf;
   struct radeon_winsys *ws = device->ws;
   struct radeon_cmdbuf *cs;
   VkResult result;

   /* Destroy the previous stop CS and create a new one. */
   if (queue->device->thread_trace.stop_cs[family]) {
      ws->cs_destroy(device->thread_trace.stop_cs[family]);
      device->thread_trace.stop_cs[family] = NULL;
   }

   cs = ws->cs_create(ws, radv_queue_ring(queue));
   if (!cs)
      return false;

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

   if (device->spm_trace.bo)
      radv_perfcounter_emit_spm_stop(device, cs, family);

   /* Stop SQTT. */
   radv_emit_thread_trace_stop(device, cs, family);

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

   device->thread_trace.stop_cs[family] = cs;

   return radv_queue_internal_submit(queue, cs);
}

bool
radv_get_thread_trace(struct radv_queue *queue, struct ac_thread_trace *thread_trace)
{
   struct radv_device *device = queue->device;
   struct radeon_info *rad_info = &device->physical_device->rad_info;
   unsigned max_se = rad_info->max_se;
   void *thread_trace_ptr = device->thread_trace.ptr;

   memset(thread_trace, 0, sizeof(*thread_trace));

   for (unsigned se = 0; se < max_se; se++) {
      uint64_t info_offset = ac_thread_trace_get_info_offset(se);
      uint64_t data_offset = ac_thread_trace_get_data_offset(rad_info, &device->thread_trace, se);
      void *info_ptr = (uint8_t *)thread_trace_ptr + info_offset;
      void *data_ptr = (uint8_t *)thread_trace_ptr + data_offset;
      struct ac_thread_trace_info *info = (struct ac_thread_trace_info *)info_ptr;
      struct ac_thread_trace_se thread_trace_se = {0};
      int first_active_cu = ffs(device->physical_device->rad_info.cu_mask[se][0]);

      if (radv_se_is_disabled(device, se))
         continue;

      if (!ac_is_thread_trace_complete(&device->physical_device->rad_info, &device->thread_trace,
                                       info)) {
         if (!radv_thread_trace_resize_bo(device)) {
            fprintf(stderr, "Failed to resize the thread "
                            "trace buffer.\n");
            abort();
         }
         return false;
      }

      thread_trace_se.data_ptr = data_ptr;
      thread_trace_se.info = *info;
      thread_trace_se.shader_engine = se;

      /* RGP seems to expect units of WGP on GFX10+. */
      thread_trace_se.compute_unit = device->physical_device->rad_info.gfx_level >= GFX10
                                        ? (first_active_cu / 2)
                                        : first_active_cu;

      thread_trace->traces[thread_trace->num_traces] = thread_trace_se;
      thread_trace->num_traces++;
   }

   thread_trace->data = &device->thread_trace;
   return true;
}
