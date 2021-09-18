/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 */


#include "si_pipe.h"
#include "si_build_pm4.h"
#include "si_compute.h"

#include "ac_rgp.h"
#include "ac_sqtt.h"
#include "util/u_memory.h"

static void
si_emit_spi_config_cntl(struct si_context* sctx,
                        struct radeon_cmdbuf *cs, bool enable);

static bool
si_thread_trace_init_bo(struct si_context *sctx)
{
   unsigned max_se = sctx->screen->info.max_se;
   struct radeon_winsys *ws = sctx->ws;
   uint64_t size;

   /* The buffer size and address need to be aligned in HW regs. Align the
    * size as early as possible so that we do all the allocation & addressing
    * correctly. */
   sctx->thread_trace->buffer_size = align64(sctx->thread_trace->buffer_size,
                                             1u << SQTT_BUFFER_ALIGN_SHIFT);

   /* Compute total size of the thread trace BO for all SEs. */
   size = align64(sizeof(struct ac_thread_trace_info) * max_se,
                  1 << SQTT_BUFFER_ALIGN_SHIFT);
   size += sctx->thread_trace->buffer_size * (uint64_t)max_se;

   sctx->thread_trace->bo =
      ws->buffer_create(ws, size, 4096,
                        RADEON_DOMAIN_VRAM,
                        RADEON_FLAG_NO_INTERPROCESS_SHARING |
                        RADEON_FLAG_GTT_WC |
                        RADEON_FLAG_NO_SUBALLOC);
   if (!sctx->thread_trace->bo)
      return false;

   return true;
}

static void
si_emit_thread_trace_start(struct si_context* sctx,
                           struct radeon_cmdbuf *cs,
                           uint32_t queue_family_index)
{
   struct si_screen *sscreen = sctx->screen;
   uint32_t shifted_size = sctx->thread_trace->buffer_size >> SQTT_BUFFER_ALIGN_SHIFT;
   unsigned max_se = sscreen->info.max_se;

   radeon_begin(cs);

   for (unsigned se = 0; se < max_se; se++) {
      uint64_t va = sctx->ws->buffer_get_virtual_address(sctx->thread_trace->bo);
      uint64_t data_va = ac_thread_trace_get_data_va(&sctx->screen->info, sctx->thread_trace, va, se);
      uint64_t shifted_va = data_va >> SQTT_BUFFER_ALIGN_SHIFT;

      /* Target SEx and SH0. */
      radeon_set_uconfig_reg(cs, R_030800_GRBM_GFX_INDEX,
                             S_030800_SE_INDEX(se) |
                             S_030800_SH_INDEX(0) |
                             S_030800_INSTANCE_BROADCAST_WRITES(1));

      /* Select the first active CUs */
      int first_active_cu = ffs(sctx->screen->info.cu_mask[se][0]);

      if (sctx->chip_class >= GFX10) {
         /* Order seems important for the following 2 registers. */
         radeon_set_privileged_config_reg(cs, R_008D04_SQ_THREAD_TRACE_BUF0_SIZE,
                                          S_008D04_SIZE(shifted_size) |
                                          S_008D04_BASE_HI(shifted_va >> 32));

         radeon_set_privileged_config_reg(cs, R_008D00_SQ_THREAD_TRACE_BUF0_BASE, shifted_va);

         int wgp = first_active_cu / 2;
         radeon_set_privileged_config_reg(cs, R_008D14_SQ_THREAD_TRACE_MASK,
                                          S_008D14_WTYPE_INCLUDE(0x7f) | /* all shader stages */
                                          S_008D14_SA_SEL(0) |
                                          S_008D14_WGP_SEL(wgp) |
                                          S_008D14_SIMD_SEL(0));

         radeon_set_privileged_config_reg(cs, R_008D18_SQ_THREAD_TRACE_TOKEN_MASK,
                      S_008D18_REG_INCLUDE(V_008D18_REG_INCLUDE_SQDEC |
                                           V_008D18_REG_INCLUDE_SHDEC |
                                           V_008D18_REG_INCLUDE_GFXUDEC |
                                           V_008D18_REG_INCLUDE_CONTEXT |
                                           V_008D18_REG_INCLUDE_COMP |
                                           V_008D18_REG_INCLUDE_CONFIG) |
                      S_008D18_TOKEN_EXCLUDE(V_008D18_TOKEN_EXCLUDE_PERF));

         /* Should be emitted last (it enables thread traces). */
         radeon_set_privileged_config_reg(cs, R_008D1C_SQ_THREAD_TRACE_CTRL,
                                          S_008D1C_MODE(1) |
                                          S_008D1C_HIWATER(5) |
                                          S_008D1C_UTIL_TIMER(1) |
                                          S_008D1C_RT_FREQ(2) | /* 4096 clk */
                                          S_008D1C_DRAW_EVENT_EN(1) |
                                          S_008D1C_REG_STALL_EN(1) |
                                          S_008D1C_SPI_STALL_EN(1) |
                                          S_008D1C_SQ_STALL_EN(1) |
                                          S_008D1C_REG_DROP_ON_STALL(0) |
                                          S_008D1C_LOWATER_OFFSET(
                                             sctx->chip_class >= GFX10_3 ? 4 : 0));
      } else {
         /* Order seems important for the following 4 registers. */
         radeon_set_uconfig_reg(cs, R_030CDC_SQ_THREAD_TRACE_BASE2,
                                S_030CDC_ADDR_HI(shifted_va >> 32));

         radeon_set_uconfig_reg(cs, R_030CC0_SQ_THREAD_TRACE_BASE, shifted_va);

         radeon_set_uconfig_reg(cs, R_030CC4_SQ_THREAD_TRACE_SIZE,
                                S_030CC4_SIZE(shifted_size));

         radeon_set_uconfig_reg(cs, R_030CD4_SQ_THREAD_TRACE_CTRL,
                                S_030CD4_RESET_BUFFER(1));

         uint32_t thread_trace_mask = S_030CC8_CU_SEL(first_active_cu) |
                                      S_030CC8_SH_SEL(0) |
                                      S_030CC8_SIMD_EN(0xf) |
                                      S_030CC8_VM_ID_MASK(0) |
                                      S_030CC8_REG_STALL_EN(1) |
                                      S_030CC8_SPI_STALL_EN(1) |
                                      S_030CC8_SQ_STALL_EN(1);

         radeon_set_uconfig_reg(cs, R_030CC8_SQ_THREAD_TRACE_MASK,
                                thread_trace_mask);

         /* Trace all tokens and registers. */
         radeon_set_uconfig_reg(cs, R_030CCC_SQ_THREAD_TRACE_TOKEN_MASK,
                                S_030CCC_TOKEN_MASK(0xbfff) |
                                S_030CCC_REG_MASK(0xff) |
                                S_030CCC_REG_DROP_ON_STALL(0));

         /* Enable SQTT perf counters for all CUs. */
         radeon_set_uconfig_reg(cs, R_030CD0_SQ_THREAD_TRACE_PERF_MASK,
                                S_030CD0_SH0_MASK(0xffff) |
                                S_030CD0_SH1_MASK(0xffff));

         radeon_set_uconfig_reg(cs, R_030CE0_SQ_THREAD_TRACE_TOKEN_MASK2, 0xffffffff);

         radeon_set_uconfig_reg(cs, R_030CEC_SQ_THREAD_TRACE_HIWATER,
                                S_030CEC_HIWATER(4));

         if (sctx->chip_class == GFX9) {
            /* Reset thread trace status errors. */
            radeon_set_uconfig_reg(cs, R_030CE8_SQ_THREAD_TRACE_STATUS,
                                   S_030CE8_UTC_ERROR(0));
         }

         /* Enable the thread trace mode. */
         uint32_t thread_trace_mode =
            S_030CD8_MASK_PS(1) |
            S_030CD8_MASK_VS(1) |
            S_030CD8_MASK_GS(1) |
            S_030CD8_MASK_ES(1) |
            S_030CD8_MASK_HS(1) |
            S_030CD8_MASK_LS(1) |
            S_030CD8_MASK_CS(1) |
            S_030CD8_AUTOFLUSH_EN(1) | /* periodically flush SQTT data to memory */
            S_030CD8_MODE(1);

         if (sctx->chip_class == GFX9) {
            /* Count SQTT traffic in TCC perf counters. */
            thread_trace_mode |= S_030CD8_TC_PERF_EN(1);
         }

         radeon_set_uconfig_reg(cs, R_030CD8_SQ_THREAD_TRACE_MODE,
                                thread_trace_mode);
      }
   }

   /* Restore global broadcasting. */
   radeon_set_uconfig_reg(cs, R_030800_GRBM_GFX_INDEX,
                          S_030800_SE_BROADCAST_WRITES(1) |
                             S_030800_SH_BROADCAST_WRITES(1) |
                             S_030800_INSTANCE_BROADCAST_WRITES(1));

   /* Start the thread trace with a different event based on the queue. */
   if (queue_family_index == RING_COMPUTE) {
      radeon_set_sh_reg(cs, R_00B878_COMPUTE_THREAD_TRACE_ENABLE,
                        S_00B878_THREAD_TRACE_ENABLE(1));
   } else {
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_THREAD_TRACE_START) | EVENT_INDEX(0));
   }
   radeon_end();
}

static const uint32_t gfx9_thread_trace_info_regs[] =
{
   R_030CE4_SQ_THREAD_TRACE_WPTR,
   R_030CE8_SQ_THREAD_TRACE_STATUS,
   R_030CF0_SQ_THREAD_TRACE_CNTR,
};

static const uint32_t gfx10_thread_trace_info_regs[] =
{
   R_008D10_SQ_THREAD_TRACE_WPTR,
   R_008D20_SQ_THREAD_TRACE_STATUS,
   R_008D24_SQ_THREAD_TRACE_DROPPED_CNTR,
};

static void
si_copy_thread_trace_info_regs(struct si_context* sctx,
             struct radeon_cmdbuf *cs,
             unsigned se_index)
{
   const uint32_t *thread_trace_info_regs = NULL;

   switch (sctx->chip_class) {
   case GFX10_3:
   case GFX10:
      thread_trace_info_regs = gfx10_thread_trace_info_regs;
      break;
   case GFX9:
      thread_trace_info_regs = gfx9_thread_trace_info_regs;
      break;
   default:
      unreachable("Unsupported chip_class");
   }

   /* Get the VA where the info struct is stored for this SE. */
   uint64_t va = sctx->ws->buffer_get_virtual_address(sctx->thread_trace->bo);
   uint64_t info_va = ac_thread_trace_get_info_va(va, se_index);

   radeon_begin(cs);

   /* Copy back the info struct one DWORD at a time. */
   for (unsigned i = 0; i < 3; i++) {
      radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
      radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_PERF) |
                      COPY_DATA_DST_SEL(COPY_DATA_TC_L2) |
                  COPY_DATA_WR_CONFIRM);
      radeon_emit(cs, thread_trace_info_regs[i] >> 2);
      radeon_emit(cs, 0); /* unused */
      radeon_emit(cs, (info_va + i * 4));
      radeon_emit(cs, (info_va + i * 4) >> 32);
   }
   radeon_end();
}



static void
si_emit_thread_trace_stop(struct si_context *sctx,
                          struct radeon_cmdbuf *cs,
                          uint32_t queue_family_index)
{
   unsigned max_se = sctx->screen->info.max_se;

   radeon_begin(cs);

   /* Stop the thread trace with a different event based on the queue. */
   if (queue_family_index == RING_COMPUTE) {
      radeon_set_sh_reg(cs, R_00B878_COMPUTE_THREAD_TRACE_ENABLE,
                        S_00B878_THREAD_TRACE_ENABLE(0));
   } else {
      radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
      radeon_emit(cs, EVENT_TYPE(V_028A90_THREAD_TRACE_STOP) | EVENT_INDEX(0));
   }

   radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 0, 0));
   radeon_emit(cs, EVENT_TYPE(V_028A90_THREAD_TRACE_FINISH) | EVENT_INDEX(0));
   radeon_end();

   for (unsigned se = 0; se < max_se; se++) {
      radeon_begin(cs);

      /* Target SEi and SH0. */
      radeon_set_uconfig_reg(cs, R_030800_GRBM_GFX_INDEX,
                             S_030800_SE_INDEX(se) |
                             S_030800_SH_INDEX(0) |
                             S_030800_INSTANCE_BROADCAST_WRITES(1));

      if (sctx->chip_class >= GFX10) {
         /* Make sure to wait for the trace buffer. */
         radeon_emit(cs, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
         radeon_emit(cs, WAIT_REG_MEM_NOT_EQUAL); /* wait until the register is equal to the reference value */
         radeon_emit(cs, R_008D20_SQ_THREAD_TRACE_STATUS >> 2);  /* register */
         radeon_emit(cs, 0);
         radeon_emit(cs, 0); /* reference value */
         radeon_emit(cs, S_008D20_FINISH_DONE(1)); /* mask */
         radeon_emit(cs, 4); /* poll interval */

         /* Disable the thread trace mode. */
         radeon_set_privileged_config_reg(cs, R_008D1C_SQ_THREAD_TRACE_CTRL,
                                          S_008D1C_MODE(0));

         /* Wait for thread trace completion. */
         radeon_emit(cs, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
         radeon_emit(cs, WAIT_REG_MEM_EQUAL); /* wait until the register is equal to the reference value */
         radeon_emit(cs, R_008D20_SQ_THREAD_TRACE_STATUS >> 2);  /* register */
         radeon_emit(cs, 0);
         radeon_emit(cs, 0); /* reference value */
         radeon_emit(cs, S_008D20_BUSY(1)); /* mask */
         radeon_emit(cs, 4); /* poll interval */
      } else {
         /* Disable the thread trace mode. */
         radeon_set_uconfig_reg(cs, R_030CD8_SQ_THREAD_TRACE_MODE,
                                S_030CD8_MODE(0));

         /* Wait for thread trace completion. */
         radeon_emit(cs, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
         radeon_emit(cs, WAIT_REG_MEM_EQUAL); /* wait until the register is equal to the reference value */
         radeon_emit(cs, R_030CE8_SQ_THREAD_TRACE_STATUS >> 2);  /* register */
         radeon_emit(cs, 0);
         radeon_emit(cs, 0); /* reference value */
         radeon_emit(cs, S_030CE8_BUSY(1)); /* mask */
         radeon_emit(cs, 4); /* poll interval */
      }
      radeon_end();

      si_copy_thread_trace_info_regs(sctx, cs, se);
   }

   /* Restore global broadcasting. */
   radeon_begin_again(cs);
   radeon_set_uconfig_reg(cs, R_030800_GRBM_GFX_INDEX,
                          S_030800_SE_BROADCAST_WRITES(1) |
                             S_030800_SH_BROADCAST_WRITES(1) |
                             S_030800_INSTANCE_BROADCAST_WRITES(1));
   radeon_end();
}

static void
si_thread_trace_start(struct si_context *sctx, int family, struct radeon_cmdbuf *cs)
{
   struct radeon_winsys *ws = sctx->ws;

   radeon_begin(cs);

   switch (family) {
      case RING_GFX:
         radeon_emit(cs, PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
         radeon_emit(cs, CC0_UPDATE_LOAD_ENABLES(1));
         radeon_emit(cs, CC1_UPDATE_SHADOW_ENABLES(1));
         break;
      case RING_COMPUTE:
         radeon_emit(cs, PKT3(PKT3_NOP, 0, 0));
         radeon_emit(cs, 0);
         break;
   }
   radeon_end();

   ws->cs_add_buffer(cs,
                     sctx->thread_trace->bo,
                     RADEON_USAGE_READWRITE,
                     RADEON_DOMAIN_VRAM,
                     0);

   si_cp_dma_wait_for_idle(sctx, cs);

   /* Make sure to wait-for-idle before starting SQTT. */
   sctx->flags |=
      SI_CONTEXT_PS_PARTIAL_FLUSH | SI_CONTEXT_CS_PARTIAL_FLUSH |
      SI_CONTEXT_INV_ICACHE | SI_CONTEXT_INV_SCACHE | SI_CONTEXT_INV_VCACHE |
      SI_CONTEXT_INV_L2 | SI_CONTEXT_PFP_SYNC_ME;
   sctx->emit_cache_flush(sctx, cs);

   si_inhibit_clockgating(sctx, cs, true);

   /* Enable SQG events that collects thread trace data. */
   si_emit_spi_config_cntl(sctx, cs, true);

   si_emit_thread_trace_start(sctx, cs, family);
}

static void
si_thread_trace_stop(struct si_context *sctx, int family, struct radeon_cmdbuf *cs)
{
   struct radeon_winsys *ws = sctx->ws;

   radeon_begin(cs);

   switch (family) {
      case RING_GFX:
         radeon_emit(sctx->thread_trace->stop_cs[family], PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
         radeon_emit(sctx->thread_trace->stop_cs[family], CC0_UPDATE_LOAD_ENABLES(1));
         radeon_emit(sctx->thread_trace->stop_cs[family], CC1_UPDATE_SHADOW_ENABLES(1));
         break;
      case RING_COMPUTE:
         radeon_emit(sctx->thread_trace->stop_cs[family], PKT3(PKT3_NOP, 0, 0));
         radeon_emit(sctx->thread_trace->stop_cs[family], 0);
         break;
   }
   radeon_end();

   ws->cs_add_buffer(cs,
                     sctx->thread_trace->bo,
                     RADEON_USAGE_READWRITE,
                     RADEON_DOMAIN_VRAM,
                     0);

   si_cp_dma_wait_for_idle(sctx, cs);

   /* Make sure to wait-for-idle before stopping SQTT. */
   sctx->flags |=
      SI_CONTEXT_PS_PARTIAL_FLUSH | SI_CONTEXT_CS_PARTIAL_FLUSH |
      SI_CONTEXT_INV_ICACHE | SI_CONTEXT_INV_SCACHE | SI_CONTEXT_INV_VCACHE |
      SI_CONTEXT_INV_L2 | SI_CONTEXT_PFP_SYNC_ME;
   sctx->emit_cache_flush(sctx, cs);

   si_emit_thread_trace_stop(sctx, cs, family);

   /* Restore previous state by disabling SQG events. */
   si_emit_spi_config_cntl(sctx, cs, false);

   si_inhibit_clockgating(sctx, cs, false);
}


static void
si_thread_trace_init_cs(struct si_context *sctx)
{
   struct radeon_winsys *ws = sctx->ws;

   /* Thread trace start CS (only handles RING_GFX). */
   sctx->thread_trace->start_cs[RING_GFX] = CALLOC_STRUCT(radeon_cmdbuf);
   if (!ws->cs_create(sctx->thread_trace->start_cs[RING_GFX],
                      sctx->ctx, RING_GFX, NULL, NULL, 0)) {
      free(sctx->thread_trace->start_cs[RING_GFX]);
      sctx->thread_trace->start_cs[RING_GFX] = NULL;
      return;
   }

   si_thread_trace_start(sctx, RING_GFX, sctx->thread_trace->start_cs[RING_GFX]);

   /* Thread trace stop CS. */
   sctx->thread_trace->stop_cs[RING_GFX] = CALLOC_STRUCT(radeon_cmdbuf);
   if (!ws->cs_create(sctx->thread_trace->stop_cs[RING_GFX],
                      sctx->ctx, RING_GFX, NULL, NULL, 0)) {
      free(sctx->thread_trace->start_cs[RING_GFX]);
      sctx->thread_trace->start_cs[RING_GFX] = NULL;
      free(sctx->thread_trace->stop_cs[RING_GFX]);
      sctx->thread_trace->stop_cs[RING_GFX] = NULL;
      return;
   }

   si_thread_trace_stop(sctx, RING_GFX, sctx->thread_trace->stop_cs[RING_GFX]);
}

static void
si_begin_thread_trace(struct si_context *sctx, struct radeon_cmdbuf *rcs)
{
   struct radeon_cmdbuf *cs = sctx->thread_trace->start_cs[RING_GFX];
   sctx->ws->cs_flush(cs, 0, NULL);
}

static void
si_end_thread_trace(struct si_context *sctx, struct radeon_cmdbuf *rcs)
{
   struct radeon_cmdbuf *cs = sctx->thread_trace->stop_cs[RING_GFX];
   sctx->ws->cs_flush(cs, 0, &sctx->last_sqtt_fence);
}

static bool
si_get_thread_trace(struct si_context *sctx,
                    struct ac_thread_trace *thread_trace)
{
   unsigned max_se = sctx->screen->info.max_se;

   memset(thread_trace, 0, sizeof(*thread_trace));
   thread_trace->num_traces = max_se;

   sctx->thread_trace->ptr = sctx->ws->buffer_map(sctx->ws, sctx->thread_trace->bo,
                                                          NULL,
                                                          PIPE_MAP_READ);

   if (!sctx->thread_trace->ptr)
      return false;

   void *thread_trace_ptr = sctx->thread_trace->ptr;

   for (unsigned se = 0; se < max_se; se++) {
      uint64_t info_offset = ac_thread_trace_get_info_offset(se);
      uint64_t data_offset = ac_thread_trace_get_data_offset(&sctx->screen->info, sctx->thread_trace, se);
      void *info_ptr = thread_trace_ptr + info_offset;
      void *data_ptr = thread_trace_ptr + data_offset;
      struct ac_thread_trace_info *info =
         (struct ac_thread_trace_info *)info_ptr;

      struct ac_thread_trace_se thread_trace_se = {0};

      if (!ac_is_thread_trace_complete(&sctx->screen->info, sctx->thread_trace, info)) {
         uint32_t expected_size =
            ac_get_expected_buffer_size(&sctx->screen->info, info);
         uint32_t available_size = (info->cur_offset * 32) / 1024;

         fprintf(stderr, "Failed to get the thread trace "
                 "because the buffer is too small. The "
                 "hardware needs %d KB but the "
                 "buffer size is %d KB.\n",
                 expected_size, available_size);
         fprintf(stderr, "Please update the buffer size with "
                 "AMD_THREAD_TRACE_BUFFER_SIZE=<size_in_kbytes>\n");
         return false;
      }

      thread_trace_se.data_ptr = data_ptr;
      thread_trace_se.info = *info;
      thread_trace_se.shader_engine = se;

      int first_active_cu = ffs(sctx->screen->info.cu_mask[se][0]);

      /* For GFX10+ compute_unit really means WGP */
      thread_trace_se.compute_unit =
         sctx->screen->info.chip_class >= GFX10 ? (first_active_cu / 2) : first_active_cu;

      thread_trace->traces[se] = thread_trace_se;
   }

   thread_trace->data = sctx->thread_trace;
   return true;
}


bool
si_init_thread_trace(struct si_context *sctx)
{
   static bool warn_once = true;
   if (warn_once) {
      fprintf(stderr, "*************************************************\n");
      fprintf(stderr, "* WARNING: Thread trace support is experimental *\n");
      fprintf(stderr, "*************************************************\n");
      warn_once = false;
   }

   sctx->thread_trace = CALLOC_STRUCT(ac_thread_trace_data);

   if (sctx->chip_class < GFX8) {
      fprintf(stderr, "GPU hardware not supported: refer to "
              "the RGP documentation for the list of "
              "supported GPUs!\n");
      return false;
   }

   if (sctx->chip_class > GFX10_3) {
      fprintf(stderr, "radeonsi: Thread trace is not supported "
              "for that GPU!\n");
      return false;
   }

   /* Default buffer size set to 1MB per SE. */
   sctx->thread_trace->buffer_size = debug_get_num_option("AMD_THREAD_TRACE_BUFFER_SIZE", 1024) * 1024;
   sctx->thread_trace->start_frame = 10;

   const char *trigger = getenv("AMD_THREAD_TRACE_TRIGGER");
   if (trigger) {
      sctx->thread_trace->start_frame = atoi(trigger);
      if (sctx->thread_trace->start_frame <= 0) {
         /* This isn't a frame number, must be a file */
         sctx->thread_trace->trigger_file = strdup(trigger);
         sctx->thread_trace->start_frame = -1;
      }
   }

   if (!si_thread_trace_init_bo(sctx))
      return false;

   list_inithead(&sctx->thread_trace->rgp_pso_correlation.record);
   simple_mtx_init(&sctx->thread_trace->rgp_pso_correlation.lock, mtx_plain);

   list_inithead(&sctx->thread_trace->rgp_loader_events.record);
   simple_mtx_init(&sctx->thread_trace->rgp_loader_events.lock, mtx_plain);

   list_inithead(&sctx->thread_trace->rgp_code_object.record);
   simple_mtx_init(&sctx->thread_trace->rgp_code_object.lock, mtx_plain);

   si_thread_trace_init_cs(sctx);

   sctx->sqtt_next_event = EventInvalid;

   return true;
}

void
si_destroy_thread_trace(struct si_context *sctx)
{
   struct si_screen *sscreen = sctx->screen;
   struct pb_buffer *bo = sctx->thread_trace->bo;
   radeon_bo_reference(sctx->screen->ws, &bo, NULL);

   if (sctx->thread_trace->trigger_file)
      free(sctx->thread_trace->trigger_file);

   sscreen->ws->cs_destroy(sctx->thread_trace->start_cs[RING_GFX]);
   sscreen->ws->cs_destroy(sctx->thread_trace->stop_cs[RING_GFX]);

   struct rgp_pso_correlation *pso_correlation = &sctx->thread_trace->rgp_pso_correlation;
   struct rgp_loader_events *loader_events = &sctx->thread_trace->rgp_loader_events;
   struct rgp_code_object *code_object = &sctx->thread_trace->rgp_code_object;
   list_for_each_entry_safe(struct rgp_pso_correlation_record, record,
                            &pso_correlation->record, list) {
      list_del(&record->list);
      free(record);
   }
   simple_mtx_destroy(&sctx->thread_trace->rgp_pso_correlation.lock);

   list_for_each_entry_safe(struct rgp_loader_events_record, record,
                            &loader_events->record, list) {
      list_del(&record->list);
      free(record);
   }
   simple_mtx_destroy(&sctx->thread_trace->rgp_loader_events.lock);

   list_for_each_entry_safe(struct rgp_code_object_record, record,
             &code_object->record, list) {
      uint32_t mask = record->shader_stages_mask;
      int i;

      /* Free the disassembly. */
      while (mask) {
         i = u_bit_scan(&mask);
         free(record->shader_data[i].code);
      }
      list_del(&record->list);
      free(record);
   }
   simple_mtx_destroy(&sctx->thread_trace->rgp_code_object.lock);

   free(sctx->thread_trace);
   sctx->thread_trace = NULL;
}

static uint64_t num_frames = 0;

void
si_handle_thread_trace(struct si_context *sctx, struct radeon_cmdbuf *rcs)
{
   /* Should we enable SQTT yet? */
   if (!sctx->thread_trace_enabled) {
      bool frame_trigger = num_frames == sctx->thread_trace->start_frame;
      bool file_trigger = false;
      if (sctx->thread_trace->trigger_file &&
          access(sctx->thread_trace->trigger_file, W_OK) == 0) {
         if (unlink(sctx->thread_trace->trigger_file) == 0) {
            file_trigger = true;
         } else {
            /* Do not enable tracing if we cannot remove the file,
             * because by then we'll trace every frame.
             */
            fprintf(stderr, "radeonsi: could not remove thread trace trigger file, ignoring\n");
         }
      }

      if (frame_trigger || file_trigger) {
         /* Wait for last submission */
         sctx->ws->fence_wait(sctx->ws, sctx->last_gfx_fence, PIPE_TIMEOUT_INFINITE);

         /* Start SQTT */
         si_begin_thread_trace(sctx, rcs);

         sctx->thread_trace_enabled = true;
         sctx->thread_trace->start_frame = -1;

         /* Force shader update to make sure si_sqtt_describe_pipeline_bind is called
          * for the current "pipeline".
          */
         sctx->do_update_shaders = true;
      }
   } else {
      struct ac_thread_trace thread_trace = {0};

      /* Stop SQTT */
      si_end_thread_trace(sctx, rcs);
      sctx->thread_trace_enabled = false;
      sctx->thread_trace->start_frame = -1;
      assert (sctx->last_sqtt_fence);

      /* Wait for SQTT to finish and read back the bo */
      if (sctx->ws->fence_wait(sctx->ws, sctx->last_sqtt_fence, PIPE_TIMEOUT_INFINITE) &&
          si_get_thread_trace(sctx, &thread_trace)) {
         ac_dump_rgp_capture(&sctx->screen->info, &thread_trace);
      } else {
         fprintf(stderr, "Failed to read the trace\n");
      }
   }

   num_frames++;
}


static void
si_emit_thread_trace_userdata(struct si_context* sctx,
                              struct radeon_cmdbuf *cs,
                              const void *data, uint32_t num_dwords)
{
   const uint32_t *dwords = (uint32_t *)data;

   radeon_begin(cs);

   while (num_dwords > 0) {
      uint32_t count = MIN2(num_dwords, 2);

      /* Without the perfctr bit the CP might not always pass the
       * write on correctly. */
      radeon_set_uconfig_reg_seq(cs, R_030D08_SQ_THREAD_TRACE_USERDATA_2, count, sctx->chip_class >= GFX10);

      radeon_emit_array(cs, dwords, count);

      dwords += count;
      num_dwords -= count;
   }
   radeon_end();
}

static void
si_emit_spi_config_cntl(struct si_context* sctx,
           struct radeon_cmdbuf *cs, bool enable)
{
   radeon_begin(cs);

   if (sctx->chip_class >= GFX9) {
      uint32_t spi_config_cntl = S_031100_GPR_WRITE_PRIORITY(0x2c688) |
                                 S_031100_EXP_PRIORITY_ORDER(3) |
                                 S_031100_ENABLE_SQG_TOP_EVENTS(enable) |
                                 S_031100_ENABLE_SQG_BOP_EVENTS(enable);

      if (sctx->chip_class >= GFX10)
         spi_config_cntl |= S_031100_PS_PKR_PRIORITY_CNTL(3);

      radeon_set_uconfig_reg(cs, R_031100_SPI_CONFIG_CNTL, spi_config_cntl);
   } else {
      /* SPI_CONFIG_CNTL is a protected register on GFX6-GFX8. */
      radeon_set_privileged_config_reg(cs, R_009100_SPI_CONFIG_CNTL,
                                       S_009100_ENABLE_SQG_TOP_EVENTS(enable) |
                                       S_009100_ENABLE_SQG_BOP_EVENTS(enable));
   }
   radeon_end();
}

static uint32_t num_events = 0;
void
si_sqtt_write_event_marker(struct si_context* sctx, struct radeon_cmdbuf *rcs,
                           enum rgp_sqtt_marker_event_type api_type,
                           uint32_t vertex_offset_user_data,
                           uint32_t instance_offset_user_data,
                           uint32_t draw_index_user_data)
{
   struct rgp_sqtt_marker_event marker = {0};

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_EVENT;
   marker.api_type = api_type == EventInvalid ? EventCmdDraw : api_type;
   marker.cmd_id = num_events++;
   marker.cb_id = 0;

   if (vertex_offset_user_data == UINT_MAX ||
       instance_offset_user_data == UINT_MAX) {
      vertex_offset_user_data = 0;
      instance_offset_user_data = 0;
   }

   if (draw_index_user_data == UINT_MAX)
      draw_index_user_data = vertex_offset_user_data;

   marker.vertex_offset_reg_idx = vertex_offset_user_data;
   marker.instance_offset_reg_idx = instance_offset_user_data;
   marker.draw_index_reg_idx = draw_index_user_data;

   si_emit_thread_trace_userdata(sctx, rcs, &marker, sizeof(marker) / 4);

   sctx->sqtt_next_event = EventInvalid;
}

void
si_write_event_with_dims_marker(struct si_context* sctx, struct radeon_cmdbuf *rcs,
                                enum rgp_sqtt_marker_event_type api_type,
                                uint32_t x, uint32_t y, uint32_t z)
{
   struct rgp_sqtt_marker_event_with_dims marker = {0};

   marker.event.identifier = RGP_SQTT_MARKER_IDENTIFIER_EVENT;
   marker.event.api_type = api_type;
   marker.event.cmd_id = num_events++;
   marker.event.cb_id = 0;
   marker.event.has_thread_dims = 1;

   marker.thread_x = x;
   marker.thread_y = y;
   marker.thread_z = z;

   si_emit_thread_trace_userdata(sctx, rcs, &marker, sizeof(marker) / 4);
   sctx->sqtt_next_event = EventInvalid;
}

void
si_sqtt_describe_barrier_start(struct si_context* sctx, struct radeon_cmdbuf *rcs)
{
   struct rgp_sqtt_marker_barrier_start marker = {0};

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_BARRIER_START;
   marker.cb_id = 0;
   marker.dword02 = 0xC0000000 + 10; /* RGP_BARRIER_INTERNAL_BASE */

   si_emit_thread_trace_userdata(sctx, rcs, &marker, sizeof(marker) / 4);
}

void
si_sqtt_describe_barrier_end(struct si_context* sctx, struct radeon_cmdbuf *rcs,
                            unsigned flags)
{
   struct rgp_sqtt_marker_barrier_end marker = {0};

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_BARRIER_END;
   marker.cb_id = 0;

   if (flags & SI_CONTEXT_VS_PARTIAL_FLUSH)
      marker.vs_partial_flush = true;
   if (flags & SI_CONTEXT_PS_PARTIAL_FLUSH)
      marker.ps_partial_flush = true;
   if (flags & SI_CONTEXT_CS_PARTIAL_FLUSH)
      marker.cs_partial_flush = true;

   if (flags & SI_CONTEXT_PFP_SYNC_ME)
      marker.pfp_sync_me = true;

   if (flags & SI_CONTEXT_INV_VCACHE)
      marker.inval_tcp = true;
   if (flags & SI_CONTEXT_INV_ICACHE)
      marker.inval_sqI = true;
   if (flags & SI_CONTEXT_INV_SCACHE)
      marker.inval_sqK = true;
   if (flags & SI_CONTEXT_INV_L2)
      marker.inval_tcc = true;

   if (flags & SI_CONTEXT_FLUSH_AND_INV_CB) {
      marker.inval_cb = true;
      marker.flush_cb = true;
   }
   if (flags & SI_CONTEXT_FLUSH_AND_INV_DB) {
      marker.inval_db = true;
      marker.flush_db = true;
   }

   si_emit_thread_trace_userdata(sctx, rcs, &marker, sizeof(marker) / 4);
}

void
si_write_user_event(struct si_context* sctx, struct radeon_cmdbuf *rcs,
                    enum rgp_sqtt_marker_user_event_type type,
                    const char *str, int len)
{
   if (type == UserEventPop) {
      assert (str == NULL);
      struct rgp_sqtt_marker_user_event marker = { 0 };
      marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_USER_EVENT;
      marker.data_type = type;

      si_emit_thread_trace_userdata(sctx, rcs, &marker, sizeof(marker) / 4);
   } else {
      assert (str != NULL);
      struct rgp_sqtt_marker_user_event_with_length marker = { 0 };
      marker.user_event.identifier = RGP_SQTT_MARKER_IDENTIFIER_USER_EVENT;
      marker.user_event.data_type = type;
      len = MIN2(1024, len);
      marker.length = align(len, 4);

      uint8_t *buffer = alloca(sizeof(marker) + marker.length);
      memcpy(buffer, &marker, sizeof(marker));
      memcpy(buffer + sizeof(marker), str, len);
      buffer[sizeof(marker) + len - 1] = '\0';

      si_emit_thread_trace_userdata(sctx, rcs, buffer, sizeof(marker) / 4 + marker.length / 4);
   }
}


bool
si_sqtt_pipeline_is_registered(struct ac_thread_trace_data *thread_trace_data,
                               uint64_t pipeline_hash)
{
   simple_mtx_lock(&thread_trace_data->rgp_pso_correlation.lock);
   list_for_each_entry_safe(struct rgp_pso_correlation_record, record,
             &thread_trace_data->rgp_pso_correlation.record, list) {
      if (record->pipeline_hash[0] == pipeline_hash) {
         simple_mtx_unlock(&thread_trace_data->rgp_pso_correlation.lock);
         return true;
      }

   }
   simple_mtx_unlock(&thread_trace_data->rgp_pso_correlation.lock);

   return false;
}



static enum rgp_hardware_stages
si_sqtt_pipe_to_rgp_shader_stage(struct si_shader_key* key, enum pipe_shader_type stage)
{
   switch (stage) {
   case PIPE_SHADER_VERTEX:
      if (key->as_ls)
         return RGP_HW_STAGE_LS;
      else if (key->as_es)
         return RGP_HW_STAGE_ES;
      else if (key->as_ngg)
         return RGP_HW_STAGE_GS;
      else
         return RGP_HW_STAGE_VS;
   case PIPE_SHADER_TESS_CTRL:
      return RGP_HW_STAGE_HS;
   case PIPE_SHADER_TESS_EVAL:
      if (key->as_es)
         return RGP_HW_STAGE_ES;
      else if (key->as_ngg)
         return RGP_HW_STAGE_GS;
      else
         return RGP_HW_STAGE_VS;
   case PIPE_SHADER_GEOMETRY:
      return RGP_HW_STAGE_GS;
   case PIPE_SHADER_FRAGMENT:
      return RGP_HW_STAGE_PS;
   case PIPE_SHADER_COMPUTE:
      return RGP_HW_STAGE_CS;
   default:
      unreachable("invalid mesa shader stage");
   }
}


static bool
si_sqtt_add_code_object(struct si_context* sctx,
                        uint64_t pipeline_hash,
                        bool is_compute)
{
   struct ac_thread_trace_data *thread_trace_data = sctx->thread_trace;
   struct rgp_code_object *code_object = &thread_trace_data->rgp_code_object;
   struct rgp_code_object_record *record;

   record = malloc(sizeof(struct rgp_code_object_record));
   if (!record)
      return false;

   record->shader_stages_mask = 0;
   record->num_shaders_combined = 0;
   record->pipeline_hash[0] = pipeline_hash;
   record->pipeline_hash[1] = pipeline_hash;

   for (unsigned i = 0; i < PIPE_SHADER_TYPES; i++) {
      struct si_shader *shader;
      enum rgp_hardware_stages hw_stage;

      if (is_compute) {
         if (i != PIPE_SHADER_COMPUTE)
            continue;
         shader = &sctx->cs_shader_state.program->shader;
         hw_stage = RGP_HW_STAGE_CS;
      } else if (i != PIPE_SHADER_COMPUTE) {
         if (!sctx->shaders[i].cso || !sctx->shaders[i].current)
            continue;
         shader = sctx->shaders[i].current;
         hw_stage = si_sqtt_pipe_to_rgp_shader_stage(&shader->key, i);
      } else {
         continue;
      }

      uint8_t *code = malloc(shader->binary.uploaded_code_size);
      if (!code) {
         free(record);
         return false;
      }
      memcpy(code, shader->binary.uploaded_code, shader->binary.uploaded_code_size);

      uint64_t va = shader->bo->gpu_address;
      record->shader_data[i].hash[0] = _mesa_hash_data(code, shader->binary.uploaded_code_size);
      record->shader_data[i].hash[1] = record->shader_data[i].hash[0];
      record->shader_data[i].code_size = shader->binary.uploaded_code_size;
      record->shader_data[i].code = code;
      record->shader_data[i].vgpr_count = shader->config.num_vgprs;
      record->shader_data[i].sgpr_count = shader->config.num_sgprs;
      record->shader_data[i].base_address = va & 0xffffffffffff;
      record->shader_data[i].elf_symbol_offset = 0;
      record->shader_data[i].hw_stage = hw_stage;
      record->shader_data[i].is_combined = false;

      record->shader_stages_mask |= (1 << i);
      record->num_shaders_combined++;
   }

   simple_mtx_lock(&code_object->lock);
   list_addtail(&record->list, &code_object->record);
   code_object->record_count++;
   simple_mtx_unlock(&code_object->lock);

   return true;
}

bool
si_sqtt_register_pipeline(struct si_context* sctx, uint64_t pipeline_hash, uint64_t base_address, bool is_compute)
{
   struct ac_thread_trace_data *thread_trace_data = sctx->thread_trace;

   assert (!si_sqtt_pipeline_is_registered(thread_trace_data, pipeline_hash));

   bool result = ac_sqtt_add_pso_correlation(thread_trace_data, pipeline_hash);
   if (!result)
      return false;

   result = ac_sqtt_add_code_object_loader_event(thread_trace_data, pipeline_hash, base_address);
   if (!result)
      return false;

   return si_sqtt_add_code_object(sctx, pipeline_hash, is_compute);
}

void
si_sqtt_describe_pipeline_bind(struct si_context* sctx,
                               uint64_t pipeline_hash,
                               int bind_point)
{
   struct rgp_sqtt_marker_pipeline_bind marker = {0};
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   if (likely(!sctx->thread_trace_enabled)) {
      return;
   }

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_BIND_PIPELINE;
   marker.cb_id = 0;
   marker.bind_point = bind_point;
   marker.api_pso_hash[0] = pipeline_hash;
   marker.api_pso_hash[1] = pipeline_hash >> 32;

   si_emit_thread_trace_userdata(sctx, cs, &marker, sizeof(marker) / 4);
}
