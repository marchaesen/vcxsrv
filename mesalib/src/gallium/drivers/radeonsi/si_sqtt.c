/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "amd_family.h"
#include "si_build_pm4.h"
#include "si_pipe.h"

#include "tgsi/tgsi_from_mesa.h"
#include "util/hash_table.h"
#include "util/u_debug.h"
#include "util/u_memory.h"
#include "ac_rgp.h"
#include "ac_sqtt.h"

static void
si_emit_spi_config_cntl(struct si_context *sctx,
                        struct radeon_cmdbuf *cs, bool enable);

static bool si_sqtt_init_bo(struct si_context *sctx)
{
   const uint32_t align_shift = ac_sqtt_get_buffer_align_shift(&sctx->screen->info);
   unsigned max_se = sctx->screen->info.max_se;
   struct radeon_winsys *ws = sctx->ws;
   uint64_t size;

   /* The buffer size and address need to be aligned in HW regs. Align the
    * size as early as possible so that we do all the allocation & addressing
    * correctly. */
   sctx->sqtt->buffer_size =
      align64(sctx->sqtt->buffer_size, 1ull << align_shift);

   /* Compute total size of the thread trace BO for all SEs. */
   size = align64(sizeof(struct ac_sqtt_data_info) * max_se,
                  1ull << align_shift);
   size += sctx->sqtt->buffer_size * (uint64_t)max_se;

   sctx->sqtt->bo =
      ws->buffer_create(ws, size, 4096, RADEON_DOMAIN_GTT,
                        RADEON_FLAG_NO_INTERPROCESS_SHARING |
                           RADEON_FLAG_GTT_WC | RADEON_FLAG_NO_SUBALLOC);
   if (!sctx->sqtt->bo)
      return false;

   sctx->sqtt->buffer_va = sctx->ws->buffer_get_virtual_address(sctx->sqtt->bo);

   return true;
}

static void si_emit_sqtt_start(struct si_context *sctx,
                               struct radeon_cmdbuf *cs,
                               enum amd_ip_type ip_type)
{
   struct si_screen *sscreen = sctx->screen;
   const bool is_compute_queue = ip_type == AMD_IP_COMPUTE;
   struct ac_pm4_state *pm4;

   pm4 = ac_pm4_create_sized(&sscreen->info, false, 512, is_compute_queue);
   if (!pm4)
      return;

   ac_sqtt_emit_start(&sscreen->info, pm4, sctx->sqtt, is_compute_queue);
   ac_pm4_finalize(pm4);

   radeon_begin(cs);
   radeon_emit_array(pm4->pm4, pm4->ndw);
   radeon_end();

   ac_pm4_free_state(pm4);
}

static void si_emit_sqtt_stop(struct si_context *sctx, struct radeon_cmdbuf *cs,
                              enum amd_ip_type ip_type)
{
   struct si_screen *sscreen = sctx->screen;
   const bool is_compute_queue = ip_type == AMD_IP_COMPUTE;
   struct ac_pm4_state *pm4;

   pm4 = ac_pm4_create_sized(&sscreen->info, false, 512, is_compute_queue);
   if (!pm4)
      return;

   ac_sqtt_emit_stop(&sscreen->info, pm4, is_compute_queue);
   ac_pm4_finalize(pm4);

   radeon_begin(cs);
   radeon_emit_array(pm4->pm4, pm4->ndw);
   radeon_end();

   ac_pm4_clear_state(pm4, &sscreen->info, false, is_compute_queue);

   if (sctx->screen->info.has_sqtt_rb_harvest_bug) {
      /* Some chips with disabled RBs should wait for idle because FINISH_DONE
       * doesn't work. */
      sctx->barrier_flags |= SI_BARRIER_SYNC_AND_INV_CB | SI_BARRIER_SYNC_AND_INV_DB |
                             SI_BARRIER_SYNC_CS;
      sctx->emit_barrier(sctx, cs);
   }

   ac_sqtt_emit_wait(&sscreen->info, pm4, sctx->sqtt, is_compute_queue);
   ac_pm4_finalize(pm4);

   radeon_begin_again(cs);
   radeon_emit_array(pm4->pm4, pm4->ndw);
   radeon_end();

   ac_pm4_free_state(pm4);
}

static void si_sqtt_start(struct si_context *sctx, struct radeon_cmdbuf *cs)
{
   struct radeon_winsys *ws = sctx->ws;
   enum amd_ip_type ip_type = sctx->ws->cs_get_ip_type(cs);

   radeon_begin(cs);

   switch (ip_type) {
      case AMD_IP_GFX:
         radeon_emit(PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
         radeon_emit(CC0_UPDATE_LOAD_ENABLES(1));
         radeon_emit(CC1_UPDATE_SHADOW_ENABLES(1));
         break;
      case AMD_IP_COMPUTE:
         radeon_emit(PKT3(PKT3_NOP, 0, 0));
         radeon_emit(0);
         break;
      default:
        /* Unsupported. */
        assert(false);
   }
   radeon_end();

   ws->cs_add_buffer(cs, sctx->sqtt->bo, RADEON_USAGE_READWRITE,
                     RADEON_DOMAIN_VRAM);
   if (sctx->spm.bo)
      ws->cs_add_buffer(cs, sctx->spm.bo, RADEON_USAGE_READWRITE,
                        RADEON_DOMAIN_VRAM);

   si_cp_dma_wait_for_idle(sctx, cs);

   /* Make sure to wait-for-idle before starting SQTT. */
   sctx->barrier_flags |= SI_BARRIER_SYNC_PS | SI_BARRIER_SYNC_CS |
                          SI_BARRIER_INV_ICACHE | SI_BARRIER_INV_SMEM |
                          SI_BARRIER_INV_VMEM | SI_BARRIER_INV_L2 |
                          SI_BARRIER_PFP_SYNC_ME;
   sctx->emit_barrier(sctx, cs);

   si_inhibit_clockgating(sctx, cs, true);

   /* Enable SQG events that collects thread trace data. */
   si_emit_spi_config_cntl(sctx, cs, true);

   if (sctx->spm.bo) {
      si_pc_emit_spm_reset(cs);
      si_pc_emit_shaders(cs, ac_sqtt_get_shader_mask(&sctx->screen->info));
      si_emit_spm_setup(sctx, cs);
   }

   si_emit_sqtt_start(sctx, cs, ip_type);

   if (sctx->spm.bo)
      si_pc_emit_spm_start(cs);
}

static void si_sqtt_stop(struct si_context *sctx, struct radeon_cmdbuf *cs)
{
   struct radeon_winsys *ws = sctx->ws;
   enum amd_ip_type ip_type = sctx->ws->cs_get_ip_type(cs);

   radeon_begin(cs);

   switch (ip_type) {
      case AMD_IP_GFX:
         radeon_emit(PKT3(PKT3_CONTEXT_CONTROL, 1, 0));
         radeon_emit(CC0_UPDATE_LOAD_ENABLES(1));
         radeon_emit(CC1_UPDATE_SHADOW_ENABLES(1));
         break;
      case AMD_IP_COMPUTE:
         radeon_emit(PKT3(PKT3_NOP, 0, 0));
         radeon_emit(0);
         break;
      default:
        /* Unsupported. */
        assert(false);
   }
   radeon_end();

   ws->cs_add_buffer(cs, sctx->sqtt->bo, RADEON_USAGE_READWRITE,
                     RADEON_DOMAIN_VRAM);

   if (sctx->spm.bo)
      ws->cs_add_buffer(cs, sctx->spm.bo, RADEON_USAGE_READWRITE,
                        RADEON_DOMAIN_VRAM);

   si_cp_dma_wait_for_idle(sctx, cs);

   if (sctx->spm.bo)
      si_pc_emit_spm_stop(cs, sctx->screen->info.never_stop_sq_perf_counters,
                          sctx->screen->info.never_send_perfcounter_stop);

   /* Make sure to wait-for-idle before stopping SQTT. */
   sctx->barrier_flags |= SI_BARRIER_SYNC_PS | SI_BARRIER_SYNC_CS |
                          SI_BARRIER_INV_ICACHE | SI_BARRIER_INV_SMEM |
                          SI_BARRIER_INV_VMEM | SI_BARRIER_INV_L2 |
                          SI_BARRIER_PFP_SYNC_ME;
   sctx->emit_barrier(sctx, cs);

   si_emit_sqtt_stop(sctx, cs, ip_type);

   if (sctx->spm.bo)
      si_pc_emit_spm_reset(cs);

   /* Restore previous state by disabling SQG events. */
   si_emit_spi_config_cntl(sctx, cs, false);

   si_inhibit_clockgating(sctx, cs, false);
}

static void si_sqtt_init_cs(struct si_context *sctx)
{
   struct radeon_winsys *ws = sctx->ws;

   for (unsigned i = 0; i < ARRAY_SIZE(sctx->sqtt->start_cs); i++) {
      sctx->sqtt->start_cs[i] = CALLOC_STRUCT(radeon_cmdbuf);
      if (!ws->cs_create(sctx->sqtt->start_cs[i], sctx->ctx, (enum amd_ip_type)i,
                         NULL, NULL)) {
         free(sctx->sqtt->start_cs[i]);
         sctx->sqtt->start_cs[i] = NULL;
         return;
      }
      si_sqtt_start(sctx, sctx->sqtt->start_cs[i]);

      sctx->sqtt->stop_cs[i] = CALLOC_STRUCT(radeon_cmdbuf);
      if (!ws->cs_create(sctx->sqtt->stop_cs[i], sctx->ctx, (enum amd_ip_type)i,
                         NULL, NULL)) {
         ws->cs_destroy(sctx->sqtt->start_cs[i]);
         free(sctx->sqtt->start_cs[i]);
         sctx->sqtt->start_cs[i] = NULL;
         free(sctx->sqtt->stop_cs[i]);
         sctx->sqtt->stop_cs[i] = NULL;
         return;
      }

      si_sqtt_stop(sctx, sctx->sqtt->stop_cs[i]);
   }
}

static void si_begin_sqtt(struct si_context *sctx, struct radeon_cmdbuf *rcs)
{
   struct radeon_cmdbuf *cs = sctx->sqtt->start_cs[sctx->ws->cs_get_ip_type(rcs)];
   sctx->ws->cs_flush(cs, 0, NULL);
}

static void si_end_sqtt(struct si_context *sctx, struct radeon_cmdbuf *rcs)
{
   struct radeon_cmdbuf *cs = sctx->sqtt->stop_cs[sctx->ws->cs_get_ip_type(rcs)];
   sctx->ws->cs_flush(cs, 0, &sctx->last_sqtt_fence);
}

static bool
si_sqtt_resize_bo(struct si_context *sctx)
{
   /* Destroy the previous thread trace BO. */
   struct pb_buffer_lean *bo = sctx->sqtt->bo;
   radeon_bo_reference(sctx->screen->ws, &bo, NULL);

   /* Double the size of the thread trace buffer per SE. */
   sctx->sqtt->buffer_size *= 2;

   fprintf(stderr,
           "Failed to get the thread trace because the buffer "
           "was too small, resizing to %d KB\n",
           sctx->sqtt->buffer_size / 1024);

   /* Re-create the thread trace BO. */
   return si_sqtt_init_bo(sctx);
}

static bool si_get_sqtt_trace(struct si_context *sctx,
                              struct ac_sqtt_trace *sqtt)
{
   memset(sqtt, 0, sizeof(*sqtt));

   sctx->sqtt->ptr =
      sctx->ws->buffer_map(sctx->ws, sctx->sqtt->bo, NULL, PIPE_MAP_READ);

   if (!sctx->sqtt->ptr)
      return false;

   if (!ac_sqtt_get_trace(sctx->sqtt, &sctx->screen->info, sqtt)) {
      if (!si_sqtt_resize_bo(sctx)) {
         fprintf(stderr, "radeonsi: Failed to resize the SQTT buffer.\n");
      } else {
         for (int i = 0; i < ARRAY_SIZE(sctx->sqtt->start_cs); i++) {
            sctx->screen->ws->cs_destroy(sctx->sqtt->start_cs[i]);
            sctx->screen->ws->cs_destroy(sctx->sqtt->stop_cs[i]);
         }
         si_sqtt_init_cs(sctx);
      }
      return false;
   }
   return true;
}

bool si_init_sqtt(struct si_context *sctx)
{
   static bool warn_once = true;
   if (warn_once) {
      fprintf(stderr, "*************************************************\n");
      fprintf(stderr, "* WARNING: Thread trace support is experimental *\n");
      fprintf(stderr, "*************************************************\n");
      warn_once = false;
   }

   sctx->sqtt = CALLOC_STRUCT(ac_sqtt);

   if (sctx->gfx_level < GFX8) {
      fprintf(stderr, "GPU hardware not supported: refer to "
                      "the RGP documentation for the list of "
                      "supported GPUs!\n");
      return false;
   }

   if (sctx->gfx_level > GFX11) {
      fprintf(stderr, "radeonsi: Thread trace is not supported "
                      "for that GPU!\n");
      return false;
   }

   /* Default buffer size set to 32MB per SE. */
   sctx->sqtt->buffer_size =
      debug_get_num_option("AMD_THREAD_TRACE_BUFFER_SIZE", 32 * 1024) * 1024;
   sctx->sqtt->instruction_timing_enabled =
      debug_get_bool_option("AMD_THREAD_TRACE_INSTRUCTION_TIMING", true);
   sctx->sqtt->start_frame = 10;

   const char *trigger = getenv("AMD_THREAD_TRACE_TRIGGER");
   if (trigger) {
      sctx->sqtt->start_frame = atoi(trigger);
      if (sctx->sqtt->start_frame <= 0) {
         /* This isn't a frame number, must be a file */
         sctx->sqtt->trigger_file = strdup(trigger);
         sctx->sqtt->start_frame = -1;
      }
   }

   if (!si_sqtt_init_bo(sctx))
      return false;

   sctx->sqtt->pipeline_bos = _mesa_hash_table_u64_create(NULL);

   ac_sqtt_init(sctx->sqtt);

   if (sctx->gfx_level >= GFX10 &&
       debug_get_bool_option("AMD_THREAD_TRACE_SPM", sctx->gfx_level < GFX11)) {
      /* Limit SPM counters to GFX10 and GFX10_3 for now */
      ASSERTED bool r = si_spm_init(sctx);
      assert(r);
   }

   si_sqtt_init_cs(sctx);

   sctx->sqtt_next_event = EventInvalid;

   return true;
}

void si_destroy_sqtt(struct si_context *sctx)
{
   struct si_screen *sscreen = sctx->screen;
   struct pb_buffer_lean *bo = sctx->sqtt->bo;
   radeon_bo_reference(sctx->screen->ws, &bo, NULL);

   if (sctx->sqtt->trigger_file)
      free(sctx->sqtt->trigger_file);

   for (int i = 0; i < ARRAY_SIZE(sctx->sqtt->start_cs); i++) {
      sscreen->ws->cs_destroy(sctx->sqtt->start_cs[i]);
      sscreen->ws->cs_destroy(sctx->sqtt->stop_cs[i]);
   }

   struct rgp_pso_correlation *pso_correlation =
      &sctx->sqtt->rgp_pso_correlation;
   struct rgp_loader_events *loader_events = &sctx->sqtt->rgp_loader_events;
   struct rgp_code_object *code_object = &sctx->sqtt->rgp_code_object;
   list_for_each_entry_safe (struct rgp_pso_correlation_record, record,
                             &pso_correlation->record, list) {
      list_del(&record->list);
      pso_correlation->record_count--;
      free(record);
   }

   list_for_each_entry_safe (struct rgp_loader_events_record, record,
                             &loader_events->record, list) {
      list_del(&record->list);
      loader_events->record_count--;
      free(record);
   }

   list_for_each_entry_safe (struct rgp_code_object_record, record,
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
      code_object->record_count--;
   }

   ac_sqtt_finish(sctx->sqtt);

   hash_table_foreach (sctx->sqtt->pipeline_bos->table, entry) {
      struct si_sqtt_fake_pipeline *pipeline =
         (struct si_sqtt_fake_pipeline *)entry->data;
      si_resource_reference(&pipeline->bo, NULL);
      FREE(pipeline);
   }

   free(sctx->sqtt);
   sctx->sqtt = NULL;

   if (sctx->spm.bo)
      si_spm_finish(sctx);
}

static uint64_t num_frames = 0;

void si_handle_sqtt(struct si_context *sctx, struct radeon_cmdbuf *rcs)
{
   /* Should we enable SQTT yet? */
   if (!sctx->sqtt_enabled) {
      bool frame_trigger = num_frames == sctx->sqtt->start_frame;
      bool file_trigger = false;
      if (sctx->sqtt->trigger_file &&
          access(sctx->sqtt->trigger_file, W_OK) == 0) {
         if (unlink(sctx->sqtt->trigger_file) == 0) {
            file_trigger = true;
         } else {
            /* Do not enable tracing if we cannot remove the file,
             * because by then we'll trace every frame.
             */
            fprintf(stderr, "radeonsi: could not remove thread "
                            "trace trigger file, ignoring\n");
         }
      }

      if (frame_trigger || file_trigger) {
         /* Wait for last submission */
         sctx->ws->fence_wait(sctx->ws, sctx->last_gfx_fence,
                              OS_TIMEOUT_INFINITE);

         /* Start SQTT */
         si_begin_sqtt(sctx, rcs);

         sctx->sqtt_enabled = true;
         sctx->sqtt->start_frame = -1;

         /* Force shader update to make sure si_sqtt_describe_pipeline_bind is
          * called for the current "pipeline".
          */
         sctx->do_update_shaders = true;
      }
   } else {
      struct ac_sqtt_trace sqtt_trace = {0};

      /* Stop SQTT */
      si_end_sqtt(sctx, rcs);
      sctx->sqtt_enabled = false;
      sctx->sqtt->start_frame = -1;
      assert(sctx->last_sqtt_fence);

      /* Wait for SQTT to finish and read back the bo */
      if (sctx->ws->fence_wait(sctx->ws, sctx->last_sqtt_fence,
                               OS_TIMEOUT_INFINITE) &&
          si_get_sqtt_trace(sctx, &sqtt_trace)) {
         struct ac_spm_trace spm_trace;

         /* Map the SPM counter buffer */
         if (sctx->spm.bo) {
            sctx->spm.ptr = sctx->ws->buffer_map(
               sctx->ws, sctx->spm.bo, NULL, PIPE_MAP_READ | RADEON_MAP_TEMPORARY);
            ac_spm_get_trace(&sctx->spm, &spm_trace);
         }

         ac_dump_rgp_capture(&sctx->screen->info, &sqtt_trace,
                             sctx->spm.bo ? &spm_trace : NULL);

         if (sctx->spm.ptr)
            sctx->ws->buffer_unmap(sctx->ws, sctx->spm.bo);
      } else {
         fprintf(stderr, "Failed to read the trace\n");
         if (!sctx->sqtt->trigger_file) {
            sctx->sqtt->start_frame = num_frames + 10;
         }
      }
   }

   num_frames++;
}

static void si_emit_sqtt_userdata(struct si_context *sctx,
                                  struct radeon_cmdbuf *cs, const void *data,
                                  uint32_t num_dwords)
{
   const uint32_t *dwords = (uint32_t *)data;

   radeon_begin(cs);

   while (num_dwords > 0) {
      uint32_t count = MIN2(num_dwords, 2);

      radeon_set_uconfig_perfctr_reg_seq(R_030D08_SQ_THREAD_TRACE_USERDATA_2, count);
      radeon_emit_array(dwords, count);

      dwords += count;
      num_dwords -= count;
   }
   radeon_end();
}

static void
si_emit_spi_config_cntl(struct si_context *sctx,
                        struct radeon_cmdbuf *cs, bool enable)
{
   radeon_begin(cs);

   if (sctx->gfx_level >= GFX9) {
      uint32_t spi_config_cntl = S_031100_GPR_WRITE_PRIORITY(0x2c688) |
                                 S_031100_EXP_PRIORITY_ORDER(3) |
                                 S_031100_ENABLE_SQG_TOP_EVENTS(enable) |
                                 S_031100_ENABLE_SQG_BOP_EVENTS(enable);

      if (sctx->gfx_level >= GFX10)
         spi_config_cntl |= S_031100_PS_PKR_PRIORITY_CNTL(3);

      radeon_set_uconfig_reg(R_031100_SPI_CONFIG_CNTL, spi_config_cntl);
   } else {
      /* SPI_CONFIG_CNTL is a protected register on GFX6-GFX8. */
      radeon_set_privileged_config_reg(R_009100_SPI_CONFIG_CNTL,
                                       S_009100_ENABLE_SQG_TOP_EVENTS(enable) |
                                       S_009100_ENABLE_SQG_BOP_EVENTS(enable));
   }
   radeon_end();
}

static uint32_t num_events = 0;
void si_sqtt_write_event_marker(struct si_context *sctx, struct radeon_cmdbuf *rcs,
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

   si_emit_sqtt_userdata(sctx, rcs, &marker, sizeof(marker) / 4);

   sctx->sqtt_next_event = EventInvalid;
}

void si_write_event_with_dims_marker(struct si_context *sctx, struct radeon_cmdbuf *rcs,
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

   si_emit_sqtt_userdata(sctx, rcs, &marker, sizeof(marker) / 4);
   sctx->sqtt_next_event = EventInvalid;
}

void si_sqtt_describe_barrier_start(struct si_context *sctx, struct radeon_cmdbuf *rcs)
{
   struct rgp_sqtt_marker_barrier_start marker = {0};

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_BARRIER_START;
   marker.cb_id = 0;
   marker.dword02 = 0xC0000000 + 10; /* RGP_BARRIER_INTERNAL_BASE */

   si_emit_sqtt_userdata(sctx, rcs, &marker, sizeof(marker) / 4);
}

void si_sqtt_describe_barrier_end(struct si_context *sctx, struct radeon_cmdbuf *rcs,
                                  unsigned flags)
{
   struct rgp_sqtt_marker_barrier_end marker = {0};

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_BARRIER_END;
   marker.cb_id = 0;

   if (flags & SI_BARRIER_SYNC_VS)
      marker.vs_partial_flush = true;
   if (flags & SI_BARRIER_SYNC_PS)
      marker.ps_partial_flush = true;
   if (flags & SI_BARRIER_SYNC_CS)
      marker.cs_partial_flush = true;

   if (flags & SI_BARRIER_PFP_SYNC_ME)
      marker.pfp_sync_me = true;

   if (flags & SI_BARRIER_INV_VMEM)
      marker.inval_tcp = true;
   if (flags & SI_BARRIER_INV_ICACHE)
      marker.inval_sqI = true;
   if (flags & SI_BARRIER_INV_SMEM)
      marker.inval_sqK = true;
   if (flags & SI_BARRIER_INV_L2)
      marker.inval_tcc = true;

   if (flags & SI_BARRIER_SYNC_AND_INV_CB) {
      marker.inval_cb = true;
      marker.flush_cb = true;
   }
   if (flags & SI_BARRIER_SYNC_AND_INV_DB) {
      marker.inval_db = true;
      marker.flush_db = true;
   }

   si_emit_sqtt_userdata(sctx, rcs, &marker, sizeof(marker) / 4);
}

void si_write_user_event(struct si_context *sctx, struct radeon_cmdbuf *rcs,
                         enum rgp_sqtt_marker_user_event_type type,
                         const char *str, int len)
{
   if (type == UserEventPop) {
      assert(str == NULL);
      struct rgp_sqtt_marker_user_event marker = {0};
      marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_USER_EVENT;
      marker.data_type = type;

      si_emit_sqtt_userdata(sctx, rcs, &marker, sizeof(marker) / 4);
   } else {
      assert(str != NULL);
      struct rgp_sqtt_marker_user_event_with_length marker = {0};
      marker.user_event.identifier = RGP_SQTT_MARKER_IDENTIFIER_USER_EVENT;
      marker.user_event.data_type = type;
      len = MIN2(1024, len);
      marker.length = align(len, 4);

      uint8_t *buffer = alloca(sizeof(marker) + marker.length);
      memcpy(buffer, &marker, sizeof(marker));
      memcpy(buffer + sizeof(marker), str, len);
      buffer[sizeof(marker) + len - 1] = '\0';

      si_emit_sqtt_userdata(sctx, rcs, buffer,
                            sizeof(marker) / 4 + marker.length / 4);
   }
}

bool si_sqtt_pipeline_is_registered(struct ac_sqtt *sqtt,
                                    uint64_t pipeline_hash)
{
   simple_mtx_lock(&sqtt->rgp_pso_correlation.lock);
   list_for_each_entry_safe (struct rgp_pso_correlation_record, record,
                             &sqtt->rgp_pso_correlation.record, list) {
      if (record->pipeline_hash[0] == pipeline_hash) {
         simple_mtx_unlock(&sqtt->rgp_pso_correlation.lock);
         return true;
      }
   }
   simple_mtx_unlock(&sqtt->rgp_pso_correlation.lock);

   return false;
}

static enum rgp_hardware_stages
si_sqtt_pipe_to_rgp_shader_stage(union si_shader_key *key, enum pipe_shader_type stage)
{
   switch (stage) {
      case PIPE_SHADER_VERTEX:
         if (key->ge.as_ls)
            return RGP_HW_STAGE_LS;
         else if (key->ge.as_es)
            return RGP_HW_STAGE_ES;
         else if (key->ge.as_ngg)
            return RGP_HW_STAGE_GS;
         else
            return RGP_HW_STAGE_VS;
      case PIPE_SHADER_TESS_CTRL:
         return RGP_HW_STAGE_HS;
      case PIPE_SHADER_TESS_EVAL:
         if (key->ge.as_es)
            return RGP_HW_STAGE_ES;
         else if (key->ge.as_ngg)
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
si_sqtt_add_code_object(struct si_context *sctx,
                        struct si_sqtt_fake_pipeline *pipeline,
                        uint32_t *gfx_sh_offsets)
{
   struct rgp_code_object *code_object = &sctx->sqtt->rgp_code_object;
   struct rgp_code_object_record *record;
   bool is_compute = gfx_sh_offsets == NULL;

   record = calloc(1, sizeof(struct rgp_code_object_record));
   if (!record)
      return false;

   record->shader_stages_mask = 0;
   record->num_shaders_combined = 0;
   record->pipeline_hash[0] = pipeline->code_hash;
   record->pipeline_hash[1] = pipeline->code_hash;

   for (unsigned i = 0; i < MESA_VULKAN_SHADER_STAGES; i++) {
      struct si_shader *shader;
      enum rgp_hardware_stages hw_stage;

      if (is_compute) {
         if (i != PIPE_SHADER_COMPUTE)
            continue;
         shader = &sctx->cs_shader_state.program->shader;
         hw_stage = RGP_HW_STAGE_CS;
      } else if (i <= PIPE_SHADER_FRAGMENT) {
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

      uint64_t va = pipeline->bo->gpu_address + (is_compute ? 0 : gfx_sh_offsets[i]);
      unsigned lds_increment = sctx->gfx_level >= GFX11 && i == MESA_SHADER_FRAGMENT ?
         1024 : sctx->screen->info.lds_encode_granularity;

      memset(record->shader_data[i].rt_shader_name, 0, sizeof(record->shader_data[i].rt_shader_name));
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
      record->shader_data[i].scratch_memory_size = shader->config.scratch_bytes_per_wave;
      record->shader_data[i].lds_size = shader->config.lds_size * lds_increment;
      record->shader_data[i].wavefront_size = shader->wave_size;

      record->shader_stages_mask |= 1 << i;
      record->num_shaders_combined++;
   }

   simple_mtx_lock(&code_object->lock);
   list_addtail(&record->list, &code_object->record);
   code_object->record_count++;
   simple_mtx_unlock(&code_object->lock);

   return true;
}

bool si_sqtt_register_pipeline(struct si_context *sctx, struct si_sqtt_fake_pipeline *pipeline,
                               uint32_t *gfx_sh_offsets)
{
   assert(!si_sqtt_pipeline_is_registered(sctx->sqtt, pipeline->code_hash));

   bool result = ac_sqtt_add_pso_correlation(sctx->sqtt, pipeline->code_hash, pipeline->code_hash);
   if (!result)
      return false;

   result = ac_sqtt_add_code_object_loader_event(
      sctx->sqtt, pipeline->code_hash, pipeline->bo->gpu_address);
   if (!result)
      return false;

   return si_sqtt_add_code_object(sctx, pipeline, gfx_sh_offsets);
}

void si_sqtt_describe_pipeline_bind(struct si_context *sctx,
                                    uint64_t pipeline_hash,
                                    int bind_point)
{
   struct rgp_sqtt_marker_pipeline_bind marker = {0};
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;

   if (likely(!sctx->sqtt_enabled)) {
      return;
   }

   marker.identifier = RGP_SQTT_MARKER_IDENTIFIER_BIND_PIPELINE;
   marker.cb_id = 0;
   marker.bind_point = bind_point;
   marker.api_pso_hash[0] = pipeline_hash;
   marker.api_pso_hash[1] = pipeline_hash >> 32;

   si_emit_sqtt_userdata(sctx, cs, &marker, sizeof(marker) / 4);
}
