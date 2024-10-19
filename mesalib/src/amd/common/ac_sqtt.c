/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 * Copyright 2020 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_pm4.h"
#include "ac_sqtt.h"

#include "sid.h"
#include "ac_gpu_info.h"
#include "util/u_math.h"
#include "util/os_time.h"

#include "sid.h"

uint64_t
ac_sqtt_get_info_offset(unsigned se)
{
   return sizeof(struct ac_sqtt_data_info) * se;
}

uint64_t
ac_sqtt_get_data_offset(const struct radeon_info *rad_info, const struct ac_sqtt *data, unsigned se)
{
   unsigned max_se = rad_info->max_se;
   uint64_t data_offset;

   data_offset = align64(sizeof(struct ac_sqtt_data_info) * max_se, 1 << SQTT_BUFFER_ALIGN_SHIFT);
   data_offset += data->buffer_size * se;

   return data_offset;
}

static uint64_t
ac_sqtt_get_info_va(uint64_t va, unsigned se)
{
   return va + ac_sqtt_get_info_offset(se);
}

static uint64_t
ac_sqtt_get_data_va(const struct radeon_info *rad_info, const struct ac_sqtt *data,
                    unsigned se)
{
   return data->buffer_va + ac_sqtt_get_data_offset(rad_info, data, se);
}

void
ac_sqtt_init(struct ac_sqtt *data)
{
   list_inithead(&data->rgp_pso_correlation.record);
   simple_mtx_init(&data->rgp_pso_correlation.lock, mtx_plain);

   list_inithead(&data->rgp_loader_events.record);
   simple_mtx_init(&data->rgp_loader_events.lock, mtx_plain);

   list_inithead(&data->rgp_code_object.record);
   simple_mtx_init(&data->rgp_code_object.lock, mtx_plain);

   list_inithead(&data->rgp_clock_calibration.record);
   simple_mtx_init(&data->rgp_clock_calibration.lock, mtx_plain);

   list_inithead(&data->rgp_queue_info.record);
   simple_mtx_init(&data->rgp_queue_info.lock, mtx_plain);

   list_inithead(&data->rgp_queue_event.record);
   simple_mtx_init(&data->rgp_queue_event.lock, mtx_plain);
}

void
ac_sqtt_finish(struct ac_sqtt *data)
{
   assert(data->rgp_pso_correlation.record_count == 0);
   simple_mtx_destroy(&data->rgp_pso_correlation.lock);

   assert(data->rgp_loader_events.record_count == 0);
   simple_mtx_destroy(&data->rgp_loader_events.lock);

   assert(data->rgp_code_object.record_count == 0);
   simple_mtx_destroy(&data->rgp_code_object.lock);

   assert(data->rgp_clock_calibration.record_count == 0);
   simple_mtx_destroy(&data->rgp_clock_calibration.lock);

   assert(data->rgp_queue_info.record_count == 0);
   simple_mtx_destroy(&data->rgp_queue_info.lock);

   assert(data->rgp_queue_event.record_count == 0);
   simple_mtx_destroy(&data->rgp_queue_event.lock);
}

bool
ac_is_sqtt_complete(const struct radeon_info *rad_info, const struct ac_sqtt *data,
                    const struct ac_sqtt_data_info *info)
{
   if (rad_info->gfx_level >= GFX10) {
      /* GFX10 doesn't have THREAD_TRACE_CNTR but it reports the number of
       * dropped bytes per SE via THREAD_TRACE_DROPPED_CNTR. Though, this
       * doesn't seem reliable because it might still report non-zero even if
       * the SQTT buffer isn't full.
       *
       * The solution here is to compare the number of bytes written by the hw
       * (in units of 32 bytes) to the SQTT buffer size. If it's equal, that
       * means that the buffer is full and should be resized.
       */
      return !(info->cur_offset * 32 == data->buffer_size - 32);
   }

   /* Otherwise, compare the current thread trace offset with the number
    * of written bytes.
    */
   return info->cur_offset == info->gfx9_write_counter;
}

uint32_t
ac_get_expected_buffer_size(struct radeon_info *rad_info, const struct ac_sqtt_data_info *info)
{
   if (rad_info->gfx_level >= GFX10) {
      uint32_t dropped_cntr_per_se = info->gfx10_dropped_cntr / rad_info->max_se;
      return ((info->cur_offset * 32) + dropped_cntr_per_se) / 1024;
   }

   return (info->gfx9_write_counter * 32) / 1024;
}

bool
ac_sqtt_add_pso_correlation(struct ac_sqtt *sqtt, uint64_t pipeline_hash, uint64_t api_hash)
{
   struct rgp_pso_correlation *pso_correlation = &sqtt->rgp_pso_correlation;
   struct rgp_pso_correlation_record *record;

   record = malloc(sizeof(struct rgp_pso_correlation_record));
   if (!record)
      return false;

   record->api_pso_hash = api_hash;
   record->pipeline_hash[0] = pipeline_hash;
   record->pipeline_hash[1] = pipeline_hash;
   memset(record->api_level_obj_name, 0, sizeof(record->api_level_obj_name));

   simple_mtx_lock(&pso_correlation->lock);
   list_addtail(&record->list, &pso_correlation->record);
   pso_correlation->record_count++;
   simple_mtx_unlock(&pso_correlation->lock);

   return true;
}

bool
ac_sqtt_add_code_object_loader_event(struct ac_sqtt *sqtt, uint64_t pipeline_hash,
                                     uint64_t base_address)
{
   struct rgp_loader_events *loader_events = &sqtt->rgp_loader_events;
   struct rgp_loader_events_record *record;

   record = malloc(sizeof(struct rgp_loader_events_record));
   if (!record)
      return false;

   record->loader_event_type = RGP_LOAD_TO_GPU_MEMORY;
   record->reserved = 0;
   record->base_address = base_address & 0xffffffffffff;
   record->code_object_hash[0] = pipeline_hash;
   record->code_object_hash[1] = pipeline_hash;
   record->time_stamp = os_time_get_nano();

   simple_mtx_lock(&loader_events->lock);
   list_addtail(&record->list, &loader_events->record);
   loader_events->record_count++;
   simple_mtx_unlock(&loader_events->lock);

   return true;
}

bool
ac_sqtt_add_clock_calibration(struct ac_sqtt *sqtt, uint64_t cpu_timestamp, uint64_t gpu_timestamp)
{
   struct rgp_clock_calibration *clock_calibration = &sqtt->rgp_clock_calibration;
   struct rgp_clock_calibration_record *record;

   record = malloc(sizeof(struct rgp_clock_calibration_record));
   if (!record)
      return false;

   record->cpu_timestamp = cpu_timestamp;
   record->gpu_timestamp = gpu_timestamp;

   simple_mtx_lock(&clock_calibration->lock);
   list_addtail(&record->list, &clock_calibration->record);
   clock_calibration->record_count++;
   simple_mtx_unlock(&clock_calibration->lock);

   return true;
}

/* See https://gitlab.freedesktop.org/mesa/mesa/-/issues/5260
 * On some HW SQTT can hang if we're not in one of the profiling pstates. */
bool
ac_check_profile_state(const struct radeon_info *info)
{
   char path[128];
   char data[128];
   int n;

   if (!info->pci.valid)
      return false; /* Unknown but optimistic. */

   snprintf(path, sizeof(path),
            "/sys/bus/pci/devices/%04x:%02x:%02x.%x/power_dpm_force_performance_level",
            info->pci.domain, info->pci.bus, info->pci.dev, info->pci.func);

   FILE *f = fopen(path, "r");
   if (!f)
      return false; /* Unknown but optimistic. */
   n = fread(data, 1, sizeof(data) - 1, f);
   fclose(f);
   data[n] = 0;
   return strstr(data, "profile") == NULL;
}

union rgp_sqtt_marker_cb_id
ac_sqtt_get_next_cmdbuf_id(struct ac_sqtt *data, enum amd_ip_type ip_type)
{
   union rgp_sqtt_marker_cb_id cb_id = {0};

   cb_id.global_cb_id.cb_index =
      p_atomic_inc_return(&data->cmdbuf_ids_per_queue[ip_type]);

   return cb_id;
}

static bool
ac_sqtt_se_is_disabled(const struct radeon_info *info, unsigned se)
{
   /* No active CU on the SE means it is disabled. */
   return info->cu_mask[se][0] == 0;
}

static uint32_t
ac_sqtt_get_active_cu(const struct radeon_info *info, unsigned se)
{
   uint32_t cu_index;

   if (info->gfx_level >= GFX11) {
      /* GFX11 seems to operate on the last active CU. */
      cu_index = util_last_bit(info->cu_mask[se][0]) - 1;
   } else {
      /* Default to the first active CU. */
      cu_index = ffs(info->cu_mask[se][0]);
   }

   return cu_index;
}

bool
ac_sqtt_get_trace(struct ac_sqtt *data, const struct radeon_info *info,
                  struct ac_sqtt_trace *sqtt_trace)
{
   unsigned max_se = info->max_se;
   void *ptr = data->ptr;

   memset(sqtt_trace, 0, sizeof(*sqtt_trace));

   for (unsigned se = 0; se < max_se; se++) {
      uint64_t info_offset = ac_sqtt_get_info_offset(se);
      uint64_t data_offset = ac_sqtt_get_data_offset(info, data, se);
      void *info_ptr = (uint8_t *)ptr + info_offset;
      void *data_ptr = (uint8_t *)ptr + data_offset;
      struct ac_sqtt_data_info *trace_info = (struct ac_sqtt_data_info *)info_ptr;
      struct ac_sqtt_data_se data_se = {0};
      int active_cu = ac_sqtt_get_active_cu(info, se);

      if (ac_sqtt_se_is_disabled(info, se))
         continue;

      if (!ac_is_sqtt_complete(info, data, trace_info))
         return false;

      data_se.data_ptr = data_ptr;
      data_se.info = *trace_info;
      data_se.shader_engine = se;

      /* RGP seems to expect units of WGP on GFX10+. */
      data_se.compute_unit = info->gfx_level >= GFX10 ? (active_cu / 2) : active_cu;

      sqtt_trace->traces[sqtt_trace->num_traces] = data_se;
      sqtt_trace->num_traces++;
   }

   sqtt_trace->rgp_code_object = &data->rgp_code_object;
   sqtt_trace->rgp_loader_events = &data->rgp_loader_events;
   sqtt_trace->rgp_pso_correlation = &data->rgp_pso_correlation;
   sqtt_trace->rgp_queue_info = &data->rgp_queue_info;
   sqtt_trace->rgp_queue_event = &data->rgp_queue_event;
   sqtt_trace->rgp_clock_calibration = &data->rgp_clock_calibration;

   return true;
}

uint32_t
ac_sqtt_get_ctrl(const struct radeon_info *info, bool enable)
{

   uint32_t ctrl;

   if (info->gfx_level >= GFX11) {
      ctrl = S_0367B0_MODE(enable) | S_0367B0_HIWATER(5) |
             S_0367B0_UTIL_TIMER_GFX11(1) | S_0367B0_RT_FREQ(2) | /* 4096 clk */
             S_0367B0_DRAW_EVENT_EN(1) | S_0367B0_SPI_STALL_EN(1) |
             S_0367B0_SQ_STALL_EN(1) | S_0367B0_REG_AT_HWM(2);
   } else {
      assert(info->gfx_level >= GFX10);

      ctrl = S_008D1C_MODE(enable) | S_008D1C_HIWATER(5) | S_008D1C_UTIL_TIMER(1) |
             S_008D1C_RT_FREQ(2) | /* 4096 clk */ S_008D1C_DRAW_EVENT_EN(1) |
             S_008D1C_REG_STALL_EN(1) | S_008D1C_SPI_STALL_EN(1) |
             S_008D1C_SQ_STALL_EN(1) | S_008D1C_REG_DROP_ON_STALL(0);

      if (info->gfx_level == GFX10_3)
         ctrl |= S_008D1C_LOWATER_OFFSET(4);

      if (info->has_sqtt_auto_flush_mode_bug)
         ctrl |= S_008D1C_AUTO_FLUSH_MODE(1);
   }

   return ctrl;
}

uint32_t
ac_sqtt_get_shader_mask(const struct radeon_info *info)
{
   unsigned shader_mask = 0x7f; /* all shader stages */

   if (info->gfx_level >= GFX11) {
      /* Disable unsupported hw shader stages */
      shader_mask &= ~(0x02 /* VS */ | 0x08 /* ES */ | 0x20 /* LS */);
   }

   return shader_mask;
}

void
ac_sqtt_emit_start(const struct radeon_info *info, struct ac_pm4_state *pm4,
                   const struct ac_sqtt *sqtt, bool is_compute_queue)
{
   const uint32_t shifted_size = sqtt->buffer_size >> SQTT_BUFFER_ALIGN_SHIFT;
   const unsigned shader_mask = ac_sqtt_get_shader_mask(info);
   const unsigned max_se = info->max_se;

   for (unsigned se = 0; se < max_se; se++) {
      uint64_t data_va = ac_sqtt_get_data_va(info, sqtt, se);
      uint64_t shifted_va = data_va >> SQTT_BUFFER_ALIGN_SHIFT;
      int active_cu = ac_sqtt_get_active_cu(info, se);

      if (ac_sqtt_se_is_disabled(info, se))
         continue;

      /* Target SEx and SH0. */
      ac_pm4_set_reg(pm4, R_030800_GRBM_GFX_INDEX, S_030800_SE_INDEX(se) |
                     S_030800_SH_INDEX(0) | S_030800_INSTANCE_BROADCAST_WRITES(1));

      if (info->gfx_level >= GFX11) {
         /* Order seems important for the following 2 registers. */
         ac_pm4_set_reg(pm4, R_0367A4_SQ_THREAD_TRACE_BUF0_SIZE,
                        S_0367A4_SIZE(shifted_size) | S_0367A4_BASE_HI(shifted_va >> 32));

         ac_pm4_set_reg(pm4, R_0367A0_SQ_THREAD_TRACE_BUF0_BASE, shifted_va);

         ac_pm4_set_reg(pm4, R_0367B4_SQ_THREAD_TRACE_MASK,
                        S_0367B4_WTYPE_INCLUDE(shader_mask) | S_0367B4_SA_SEL(0) |
                        S_0367B4_WGP_SEL(active_cu / 2) | S_0367B4_SIMD_SEL(0));

         uint32_t sqtt_token_mask = S_0367B8_REG_INCLUDE(V_0367B8_REG_INCLUDE_SQDEC | V_0367B8_REG_INCLUDE_SHDEC |
                                                         V_0367B8_REG_INCLUDE_GFXUDEC | V_0367B8_REG_INCLUDE_COMP |
                                                         V_0367B8_REG_INCLUDE_CONTEXT | V_0367B8_REG_INCLUDE_CONFIG);

         /* Performance counters with SQTT are considered deprecated. */
         uint32_t token_exclude = V_0367B8_TOKEN_EXCLUDE_PERF;

         if (!sqtt->instruction_timing_enabled) {
            /* Reduce SQTT traffic when instruction timing isn't enabled. */
            token_exclude |= V_0367B8_TOKEN_EXCLUDE_VMEMEXEC | V_0367B8_TOKEN_EXCLUDE_ALUEXEC |
                             V_0367B8_TOKEN_EXCLUDE_VALUINST | V_0367B8_TOKEN_EXCLUDE_IMMEDIATE |
                             V_0367B8_TOKEN_EXCLUDE_INST;
         }
         sqtt_token_mask |= S_0367B8_TOKEN_EXCLUDE_GFX11(token_exclude) | S_0367B8_BOP_EVENTS_TOKEN_INCLUDE_GFX11(1);

         ac_pm4_set_reg(pm4, R_0367B8_SQ_THREAD_TRACE_TOKEN_MASK, sqtt_token_mask);

         /* Should be emitted last (it enables thread traces). */
         ac_pm4_set_reg(pm4, R_0367B0_SQ_THREAD_TRACE_CTRL, ac_sqtt_get_ctrl(info, true));
      } else if (info->gfx_level >= GFX10) {
         /* Order seems important for the following 2 registers. */
         ac_pm4_set_reg(pm4, R_008D04_SQ_THREAD_TRACE_BUF0_SIZE,
                        S_008D04_SIZE(shifted_size) | S_008D04_BASE_HI(shifted_va >> 32));

         ac_pm4_set_reg(pm4, R_008D00_SQ_THREAD_TRACE_BUF0_BASE, shifted_va);

         ac_pm4_set_reg(pm4, R_008D14_SQ_THREAD_TRACE_MASK,
                        S_008D14_WTYPE_INCLUDE(shader_mask) | S_008D14_SA_SEL(0) |
                        S_008D14_WGP_SEL(active_cu / 2) | S_008D14_SIMD_SEL(0));

         uint32_t sqtt_token_mask = S_008D18_REG_INCLUDE(V_008D18_REG_INCLUDE_SQDEC | V_008D18_REG_INCLUDE_SHDEC |
                                                         V_008D18_REG_INCLUDE_GFXUDEC | V_008D18_REG_INCLUDE_COMP |
                                                         V_008D18_REG_INCLUDE_CONTEXT | V_008D18_REG_INCLUDE_CONFIG);

         /* Performance counters with SQTT are considered deprecated. */
         uint32_t token_exclude = V_008D18_TOKEN_EXCLUDE_PERF;

         if (!sqtt->instruction_timing_enabled) {
            /* Reduce SQTT traffic when instruction timing isn't enabled. */
            token_exclude |= V_008D18_TOKEN_EXCLUDE_VMEMEXEC | V_008D18_TOKEN_EXCLUDE_ALUEXEC |
                             V_008D18_TOKEN_EXCLUDE_VALUINST | V_008D18_TOKEN_EXCLUDE_IMMEDIATE |
                             V_008D18_TOKEN_EXCLUDE_INST;
         }
         sqtt_token_mask |=
            S_008D18_TOKEN_EXCLUDE(token_exclude) | S_008D18_BOP_EVENTS_TOKEN_INCLUDE(info->gfx_level == GFX10_3);

         ac_pm4_set_reg(pm4, R_008D18_SQ_THREAD_TRACE_TOKEN_MASK, sqtt_token_mask);

         /* Should be emitted last (it enables thread traces). */
         ac_pm4_set_reg(pm4, R_008D1C_SQ_THREAD_TRACE_CTRL, ac_sqtt_get_ctrl(info, true));
      } else {
         /* Order seems important for the following 4 registers. */
         ac_pm4_set_reg(pm4, R_030CDC_SQ_THREAD_TRACE_BASE2, S_030CDC_ADDR_HI(shifted_va >> 32));

         ac_pm4_set_reg(pm4, R_030CC0_SQ_THREAD_TRACE_BASE, shifted_va);

         ac_pm4_set_reg(pm4, R_030CC4_SQ_THREAD_TRACE_SIZE, S_030CC4_SIZE(shifted_size));

         ac_pm4_set_reg(pm4, R_030CD4_SQ_THREAD_TRACE_CTRL, S_030CD4_RESET_BUFFER(1));

         uint32_t sqtt_mask = S_030CC8_CU_SEL(active_cu) | S_030CC8_SH_SEL(0) | S_030CC8_SIMD_EN(0xf) |
                              S_030CC8_VM_ID_MASK(0) | S_030CC8_REG_STALL_EN(1) | S_030CC8_SPI_STALL_EN(1) |
                              S_030CC8_SQ_STALL_EN(1);

         if (info->gfx_level < GFX9) {
            sqtt_mask |= S_030CC8_RANDOM_SEED(0xffff);
         }

         ac_pm4_set_reg(pm4, R_030CC8_SQ_THREAD_TRACE_MASK, sqtt_mask);

         /* Trace all tokens and registers. */
         ac_pm4_set_reg(pm4, R_030CCC_SQ_THREAD_TRACE_TOKEN_MASK,
                        S_030CCC_TOKEN_MASK(0xbfff) | S_030CCC_REG_MASK(0xff) | S_030CCC_REG_DROP_ON_STALL(0));

         /* Enable SQTT perf counters for all CUs. */
         ac_pm4_set_reg(pm4, R_030CD0_SQ_THREAD_TRACE_PERF_MASK,
                        S_030CD0_SH0_MASK(0xffff) | S_030CD0_SH1_MASK(0xffff));

         ac_pm4_set_reg(pm4, R_030CE0_SQ_THREAD_TRACE_TOKEN_MASK2, 0xffffffff);

         ac_pm4_set_reg(pm4, R_030CEC_SQ_THREAD_TRACE_HIWATER, S_030CEC_HIWATER(4));

         if (info->gfx_level == GFX9) {
            /* Reset thread trace status errors. */
            ac_pm4_set_reg(pm4, R_030CE8_SQ_THREAD_TRACE_STATUS, S_030CE8_UTC_ERROR(0));
         }

         /* Enable the thread trace mode. */
         uint32_t sqtt_mode = S_030CD8_MASK_PS(1) | S_030CD8_MASK_VS(1) | S_030CD8_MASK_GS(1) | S_030CD8_MASK_ES(1) |
                              S_030CD8_MASK_HS(1) | S_030CD8_MASK_LS(1) | S_030CD8_MASK_CS(1) |
                              S_030CD8_AUTOFLUSH_EN(1) | /* periodically flush SQTT data to memory */
                              S_030CD8_MODE(1);

         if (info->gfx_level == GFX9) {
            /* Count SQTT traffic in TCC perf counters. */
            sqtt_mode |= S_030CD8_TC_PERF_EN(1);
         }

         ac_pm4_set_reg(pm4, R_030CD8_SQ_THREAD_TRACE_MODE, sqtt_mode);
      }
   }

   /* Restore global broadcasting. */
   ac_pm4_set_reg(pm4, R_030800_GRBM_GFX_INDEX,  S_030800_SE_BROADCAST_WRITES(1) |
                  S_030800_SH_BROADCAST_WRITES(1) | S_030800_INSTANCE_BROADCAST_WRITES(1));

   /* Start the thread trace with a different event based on the queue. */
   if (is_compute_queue) {
      ac_pm4_set_reg(pm4, R_00B878_COMPUTE_THREAD_TRACE_ENABLE, S_00B878_THREAD_TRACE_ENABLE(1));
   } else {
      ac_pm4_cmd_add(pm4, PKT3(PKT3_EVENT_WRITE, 0, 0));
      ac_pm4_cmd_add(pm4, EVENT_TYPE(V_028A90_THREAD_TRACE_START) | EVENT_INDEX(0));
   }

}

static const uint32_t gfx8_sqtt_info_regs[] = {
   R_030CE4_SQ_THREAD_TRACE_WPTR,
   R_030CE8_SQ_THREAD_TRACE_STATUS,
   R_008E40_SQ_THREAD_TRACE_CNTR,
};

static const uint32_t gfx9_sqtt_info_regs[] = {
   R_030CE4_SQ_THREAD_TRACE_WPTR,
   R_030CE8_SQ_THREAD_TRACE_STATUS,
   R_030CF0_SQ_THREAD_TRACE_CNTR,
};

static const uint32_t gfx10_sqtt_info_regs[] = {
   R_008D10_SQ_THREAD_TRACE_WPTR,
   R_008D20_SQ_THREAD_TRACE_STATUS,
   R_008D24_SQ_THREAD_TRACE_DROPPED_CNTR,
};

static const uint32_t gfx11_sqtt_info_regs[] = {
   R_0367BC_SQ_THREAD_TRACE_WPTR,
   R_0367D0_SQ_THREAD_TRACE_STATUS,
   R_0367E8_SQ_THREAD_TRACE_DROPPED_CNTR,
};

static void
ac_sqtt_copy_info_regs(const struct radeon_info *info, struct ac_pm4_state *pm4,
                       const struct ac_sqtt *sqtt, uint32_t se_index)
{
   const uint32_t *sqtt_info_regs = NULL;

   if (info->gfx_level >= GFX11) {
      sqtt_info_regs = gfx11_sqtt_info_regs;
   } else if (info->gfx_level >= GFX10) {
      sqtt_info_regs = gfx10_sqtt_info_regs;
   } else if (info->gfx_level == GFX9) {
      sqtt_info_regs = gfx9_sqtt_info_regs;
   } else {
      assert(info->gfx_level == GFX8);
      sqtt_info_regs = gfx8_sqtt_info_regs;
   }

   /* Get the VA where the info struct is stored for this SE. */
   uint64_t info_va = ac_sqtt_get_info_va(sqtt->buffer_va, se_index);

   /* Copy back the info struct one DWORD at a time. */
   for (unsigned i = 0; i < 3; i++) {
      ac_pm4_cmd_add(pm4, PKT3(PKT3_COPY_DATA, 4, 0));
      ac_pm4_cmd_add(pm4, COPY_DATA_SRC_SEL(COPY_DATA_PERF) | COPY_DATA_DST_SEL(COPY_DATA_TC_L2) | COPY_DATA_WR_CONFIRM);
      ac_pm4_cmd_add(pm4, sqtt_info_regs[i] >> 2);
      ac_pm4_cmd_add(pm4, 0); /* unused */
      ac_pm4_cmd_add(pm4, (info_va + i * 4));
      ac_pm4_cmd_add(pm4, (info_va + i * 4) >> 32);
   }

   if (info->gfx_level == GFX11) {
      /* On GFX11, SQ_THREAD_TRACE_WPTR is incremented from the "initial WPTR address" instead of 0.
       * To get the number of bytes (in units of 32 bytes) written by SQTT, the workaround is to
       * subtract SQ_THREAD_TRACE_WPTR from the "initial WPTR address" as follow:
       *
       * 1) get the current buffer base address for this SE
       * 2) shift right by 5 bits because SQ_THREAD_TRACE_WPTR is 32-byte aligned
       * 3) mask off the higher 3 bits because WPTR.OFFSET is 29 bits
       */
      uint64_t data_va = ac_sqtt_get_data_va(info, sqtt, se_index);
      uint64_t shifted_data_va = (data_va >> 5);
      uint32_t init_wptr_value = shifted_data_va & 0x1fffffff;

      ac_pm4_cmd_add(pm4, PKT3(PKT3_ATOMIC_MEM, 7, 0));
      ac_pm4_cmd_add(pm4, ATOMIC_OP(TC_OP_ATOMIC_SUB_32));
      ac_pm4_cmd_add(pm4, info_va);         /* addr lo */
      ac_pm4_cmd_add(pm4, info_va >> 32);   /* addr hi */
      ac_pm4_cmd_add(pm4, init_wptr_value); /* data lo */
      ac_pm4_cmd_add(pm4, 0);               /* data hi */
      ac_pm4_cmd_add(pm4, 0);               /* compare data lo */
      ac_pm4_cmd_add(pm4, 0);               /* compare data hi */
      ac_pm4_cmd_add(pm4, 0);               /* loop interval */
   }
}

void
ac_sqtt_emit_stop(const struct radeon_info *info, struct ac_pm4_state *pm4,
                  bool is_compute_queue)
{
   /* Stop the thread trace with a different event based on the queue. */
   if (is_compute_queue) {
      ac_pm4_set_reg(pm4, R_00B878_COMPUTE_THREAD_TRACE_ENABLE, S_00B878_THREAD_TRACE_ENABLE(0));
   } else {
      ac_pm4_cmd_add(pm4, PKT3(PKT3_EVENT_WRITE, 0, 0));
      ac_pm4_cmd_add(pm4, EVENT_TYPE(V_028A90_THREAD_TRACE_STOP) | EVENT_INDEX(0));
   }

   ac_pm4_cmd_add(pm4, PKT3(PKT3_EVENT_WRITE, 0, 0));
   ac_pm4_cmd_add(pm4, EVENT_TYPE(V_028A90_THREAD_TRACE_FINISH) | EVENT_INDEX(0));
}

void
ac_sqtt_emit_wait(const struct radeon_info *info, struct ac_pm4_state *pm4,
                  const struct ac_sqtt *sqtt, bool is_compute_queue)
{
   const unsigned max_se = info->max_se;

   for (unsigned se = 0; se < max_se; se++) {
      if (ac_sqtt_se_is_disabled(info, se))
         continue;

      /* Target SEi and SH0. */
      ac_pm4_set_reg(pm4, R_030800_GRBM_GFX_INDEX, S_030800_SE_INDEX(se) |
                     S_030800_SH_INDEX(0) | S_030800_INSTANCE_BROADCAST_WRITES(1));

      if (info->gfx_level >= GFX11) {
         /* Make sure to wait for the trace buffer. */
         ac_pm4_cmd_add(pm4, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
         ac_pm4_cmd_add(pm4, WAIT_REG_MEM_NOT_EQUAL); /* wait until the register is equal to the reference value */
         ac_pm4_cmd_add(pm4, R_0367D0_SQ_THREAD_TRACE_STATUS >> 2); /* register */
         ac_pm4_cmd_add(pm4, 0);
         ac_pm4_cmd_add(pm4, 0); /* reference value */
         ac_pm4_cmd_add(pm4, ~C_0367D0_FINISH_DONE);
         ac_pm4_cmd_add(pm4, 4); /* poll interval */

         /* Disable the thread trace mode. */
         ac_pm4_set_reg(pm4, R_0367B0_SQ_THREAD_TRACE_CTRL, ac_sqtt_get_ctrl(info, false));

         /* Wait for thread trace completion. */
         ac_pm4_cmd_add(pm4, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
         ac_pm4_cmd_add(pm4, WAIT_REG_MEM_EQUAL); /* wait until the register is equal to the reference value */
         ac_pm4_cmd_add(pm4, R_0367D0_SQ_THREAD_TRACE_STATUS >> 2); /* register */
         ac_pm4_cmd_add(pm4, 0);
         ac_pm4_cmd_add(pm4, 0);              /* reference value */
         ac_pm4_cmd_add(pm4, ~C_0367D0_BUSY); /* mask */
         ac_pm4_cmd_add(pm4, 4);              /* poll interval */
      } else if (info->gfx_level >= GFX10) {
         if (!info->has_sqtt_rb_harvest_bug) {
            /* Make sure to wait for the trace buffer. */
            ac_pm4_cmd_add(pm4, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
            ac_pm4_cmd_add(pm4, WAIT_REG_MEM_NOT_EQUAL); /* wait until the register is equal to the reference value */
            ac_pm4_cmd_add(pm4, R_008D20_SQ_THREAD_TRACE_STATUS >> 2); /* register */
            ac_pm4_cmd_add(pm4, 0);
            ac_pm4_cmd_add(pm4, 0); /* reference value */
            ac_pm4_cmd_add(pm4, ~C_008D20_FINISH_DONE);
            ac_pm4_cmd_add(pm4, 4); /* poll interval */
         }

         /* Disable the thread trace mode. */
         ac_pm4_set_reg(pm4, R_008D1C_SQ_THREAD_TRACE_CTRL, ac_sqtt_get_ctrl(info, false));

         /* Wait for thread trace completion. */
         ac_pm4_cmd_add(pm4, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
         ac_pm4_cmd_add(pm4, WAIT_REG_MEM_EQUAL); /* wait until the register is equal to the reference value */
         ac_pm4_cmd_add(pm4, R_008D20_SQ_THREAD_TRACE_STATUS >> 2); /* register */
         ac_pm4_cmd_add(pm4, 0);
         ac_pm4_cmd_add(pm4, 0);              /* reference value */
         ac_pm4_cmd_add(pm4, ~C_008D20_BUSY); /* mask */
         ac_pm4_cmd_add(pm4, 4);              /* poll interval */
      } else {
         /* Disable the thread trace mode. */
         ac_pm4_set_reg(pm4, R_030CD8_SQ_THREAD_TRACE_MODE, S_030CD8_MODE(0));

         /* Wait for thread trace completion. */
         ac_pm4_cmd_add(pm4, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
         ac_pm4_cmd_add(pm4, WAIT_REG_MEM_EQUAL); /* wait until the register is equal to the reference value */
         ac_pm4_cmd_add(pm4, R_030CE8_SQ_THREAD_TRACE_STATUS >> 2); /* register */
         ac_pm4_cmd_add(pm4, 0);
         ac_pm4_cmd_add(pm4, 0);              /* reference value */
         ac_pm4_cmd_add(pm4, ~C_030CE8_BUSY); /* mask */
         ac_pm4_cmd_add(pm4, 4);              /* poll interval */
      }

      ac_sqtt_copy_info_regs(info, pm4, sqtt, se);
   }

   /* Restore global broadcasting. */
   ac_pm4_set_reg(pm4, R_030800_GRBM_GFX_INDEX, S_030800_SE_BROADCAST_WRITES(1) |
                  S_030800_SH_BROADCAST_WRITES(1) | S_030800_INSTANCE_BROADCAST_WRITES(1));
}
