/*
 * Copyright 2010 Jerome Glisse <glisse@freedesktop.org>
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_build_pm4.h"
#include "si_pipe.h"
#include "sid.h"
#include "util/os_time.h"
#include "util/u_log.h"
#include "util/u_upload_mgr.h"
#include "ac_debug.h"
#include "si_utrace.h"

void si_reset_debug_log_buffer(struct si_context *sctx)
{
#if SHADER_DEBUG_LOG
   /* Create and bind the debug log buffer. */
   unsigned size = 256 * 16 + 4;
   struct pipe_resource *buf = &si_aligned_buffer_create(sctx->b.screen, SI_RESOURCE_FLAG_CLEAR,
                                                         PIPE_USAGE_STAGING, size, 256)->b.b;
   si_set_internal_shader_buffer(sctx, SI_RING_SHADER_LOG,
                                 &(struct pipe_shader_buffer){
                                    .buffer = buf,
                                    .buffer_size = size});
   pipe_resource_reference(&buf, NULL);
#endif
}

static void si_dump_debug_log(struct si_context *sctx, bool sync)
{
   struct pipe_resource *buf = sctx->internal_bindings.buffers[SI_RING_SHADER_LOG];
   if (!buf)
      return;

   struct pipe_transfer *transfer = NULL;
   unsigned size = sctx->descriptors[SI_DESCS_INTERNAL].list[SI_RING_SHADER_LOG * 4 + 2];
   unsigned max_entries = (size - 4) / 16;

   /* If not syncing (e.g. expecting a GPU hang), wait some time and then just print
    * the log buffer.
    */
   if (!sync)
      usleep(1000000);

   fprintf(stderr, "Reading shader log...\n");

   uint32_t *map = pipe_buffer_map(&sctx->b, buf,
                                   PIPE_MAP_READ | (sync ? 0 : PIPE_MAP_UNSYNCHRONIZED),
                                   &transfer);
   unsigned num = map[0];
   fprintf(stderr, "Shader log items: %u\n", num);

   if (!num) {
      pipe_buffer_unmap(&sctx->b, transfer);
      return;
   }


   unsigned first = num > max_entries ? num - max_entries : 0;
   map++;

   for (unsigned i = first; i < num; i++) {
      unsigned idx = i % max_entries;

      fprintf(stderr, "   [%u(%u)] = {%u, %u, %u, %u}\n", i, idx,
              map[idx * 4], map[idx * 4 + 1], map[idx * 4 + 2], map[idx * 4 + 3]);
   }
   pipe_buffer_unmap(&sctx->b, transfer);

   si_reset_debug_log_buffer(sctx);
}

void si_flush_gfx_cs(struct si_context *ctx, unsigned flags, struct pipe_fence_handle **fence)
{
   struct radeon_cmdbuf *cs = &ctx->gfx_cs;
   struct radeon_winsys *ws = ctx->ws;
   struct si_screen *sscreen = ctx->screen;
   const unsigned wait_ps_cs = SI_BARRIER_SYNC_PS | SI_BARRIER_SYNC_CS;
   unsigned wait_flags = 0;

   if (ctx->gfx_flush_in_progress)
      return;

   /* The amdgpu kernel driver synchronizes execution for shared DMABUFs between
    * processes on DRM >= 3.39.0, so we don't have to wait at the end of IBs to
    * make sure everything is idle.
    *
    * The amdgpu winsys synchronizes execution for buffers shared by different
    * contexts within the same process.
    *
    * Interop with AMDVLK, RADV, or OpenCL within the same process requires
    * explicit fences or glFinish.
    */
   if (sscreen->info.is_amdgpu && sscreen->info.drm_minor >= 39)
      flags |= RADEON_FLUSH_START_NEXT_GFX_IB_NOW;

   if (ctx->gfx_level == GFX6) {
      /* The kernel flushes L2 before shaders are finished. */
      wait_flags |= wait_ps_cs;
   } else if (!(flags & RADEON_FLUSH_START_NEXT_GFX_IB_NOW) ||
              ((flags & RADEON_FLUSH_TOGGLE_SECURE_SUBMISSION) &&
                !ws->cs_is_secure(cs))) {
      /* TODO: this workaround fixes subtitles rendering with mpv -vo=vaapi and
       * tmz but shouldn't be necessary.
       */
      wait_flags |= wait_ps_cs;
   }

   /* Drop this flush if it's a no-op. */
   if (!radeon_emitted(cs, ctx->initial_gfx_cs_size) &&
       (!wait_flags || !ctx->gfx_last_ib_is_busy) &&
       !(flags & RADEON_FLUSH_TOGGLE_SECURE_SUBMISSION)) {
      tc_driver_internal_flush_notify(ctx->tc);
      return;
   }

   /* Non-aux contexts must set up no-op API dispatch on GPU resets. This is
    * similar to si_get_reset_status but here we can ignore soft-recoveries,
    * while si_get_reset_status can't. */
   if (!(ctx->context_flags & SI_CONTEXT_FLAG_AUX) &&
       ctx->device_reset_callback.reset) {
      enum pipe_reset_status status = ctx->ws->ctx_query_reset_status(ctx->ctx, true, NULL, NULL);
      if (status != PIPE_NO_RESET)
         ctx->device_reset_callback.reset(ctx->device_reset_callback.data, status);
   }

   if (sscreen->debug_flags & DBG(CHECK_VM))
      flags &= ~PIPE_FLUSH_ASYNC;

   ctx->gfx_flush_in_progress = true;

   if (ctx->has_graphics) {
      if (!list_is_empty(&ctx->active_queries))
         si_suspend_queries(ctx);

      ctx->streamout.suspended = false;
      if (ctx->streamout.begin_emitted) {
         si_emit_streamout_end(ctx);
         ctx->streamout.suspended = true;

         /* Make sure streamout is idle because the next process might change
          * GE_GS_ORDERED_ID_BASE (which must not be changed when streamout is busy)
          * and make this process guilty of hanging.
          */
         if (ctx->gfx_level >= GFX12)
            wait_flags |= SI_BARRIER_SYNC_VS;
      }
   }

   /* Make sure CP DMA is idle at the end of IBs after L2 prefetches
    * because the kernel doesn't wait for it. */
   if (ctx->gfx_level >= GFX7 && ctx->screen->info.has_cp_dma)
      si_cp_dma_wait_for_idle(ctx, &ctx->gfx_cs);

   /* If we use s_sendmsg to set tess factors to all 0 or all 1 instead of writing to the tess
    * factor buffer, we need this at the end of command buffers:
    */
   if ((ctx->gfx_level == GFX11 || ctx->gfx_level == GFX11_5) && ctx->has_tessellation) {
      radeon_begin(cs);
      radeon_event_write(V_028A90_SQ_NON_EVENT);
      radeon_end();
   }

   /* Wait for draw calls to finish if needed. */
   if (wait_flags) {
      ctx->barrier_flags |= wait_flags;
      si_emit_barrier_direct(ctx);
   }
   ctx->gfx_last_ib_is_busy = (wait_flags & wait_ps_cs) != wait_ps_cs;

   if (ctx->current_saved_cs) {
      si_trace_emit(ctx);

      /* Save the IB for debug contexts. */
      si_save_cs(ws, cs, &ctx->current_saved_cs->gfx, true);
      ctx->current_saved_cs->flushed = true;
      ctx->current_saved_cs->time_flush = os_time_get_nano();

      si_log_hw_flush(ctx);
   }

   if (sscreen->debug_flags & DBG(IB))
      si_print_current_ib(ctx, stderr);

   if (sscreen->context_roll_log_filename)
      si_gather_context_rolls(ctx);

   if (ctx->is_noop)
      flags |= RADEON_FLUSH_NOOP;

   uint64_t start_ts = 0, submission_id = 0;
   if (u_trace_perfetto_active(&ctx->ds.trace_context)) {
      start_ts = si_ds_begin_submit(&ctx->ds_queue);
      submission_id = ctx->ds_queue.submission_id;
   }

   /* Flush the CS. */
   ws->cs_flush(cs, flags, &ctx->last_gfx_fence);

   if (u_trace_perfetto_active(&ctx->ds.trace_context) && start_ts > 0) {
      si_ds_end_submit(&ctx->ds_queue, start_ts);
   }

   tc_driver_internal_flush_notify(ctx->tc);
   if (fence)
      ws->fence_reference(ws, fence, ctx->last_gfx_fence);

   ctx->num_gfx_cs_flushes++;

   /* Check VM faults if needed. */
   if (sscreen->debug_flags & DBG(CHECK_VM)) {
      /* Use conservative timeout 800ms, after which we won't wait any
       * longer and assume the GPU is hung.
       */
      ctx->ws->fence_wait(ctx->ws, ctx->last_gfx_fence, 800 * 1000 * 1000);

      si_check_vm_faults(ctx, &ctx->current_saved_cs->gfx);
   }

   if (unlikely(ctx->sqtt && (flags & PIPE_FLUSH_END_OF_FRAME))) {
      si_handle_sqtt(ctx, &ctx->gfx_cs);
   }

   if (ctx->current_saved_cs)
      si_saved_cs_reference(&ctx->current_saved_cs, NULL);

   if (u_trace_perfetto_active(&ctx->ds.trace_context))
      si_utrace_flush(ctx, submission_id);

   si_begin_new_gfx_cs(ctx, false);
   ctx->gfx_flush_in_progress = false;

#if SHADER_DEBUG_LOG
   if (debug_get_bool_option("shaderlog", false))
      si_dump_debug_log(ctx, false);
#endif
}

static void si_begin_gfx_cs_debug(struct si_context *ctx)
{
   static const uint32_t zeros[1];
   assert(!ctx->current_saved_cs);

   ctx->current_saved_cs = calloc(1, sizeof(*ctx->current_saved_cs));
   if (!ctx->current_saved_cs)
      return;

   pipe_reference_init(&ctx->current_saved_cs->reference, 1);

   ctx->current_saved_cs->trace_buf =
      si_resource(pipe_buffer_create(ctx->b.screen, 0, PIPE_USAGE_STAGING, 4));
   if (!ctx->current_saved_cs->trace_buf) {
      free(ctx->current_saved_cs);
      ctx->current_saved_cs = NULL;
      return;
   }

   pipe_buffer_write_nooverlap(&ctx->b, &ctx->current_saved_cs->trace_buf->b.b, 0, sizeof(zeros),
                               zeros);
   ctx->current_saved_cs->trace_id = 0;

   si_trace_emit(ctx);

   radeon_add_to_buffer_list(ctx, &ctx->gfx_cs, ctx->current_saved_cs->trace_buf,
                             RADEON_USAGE_READWRITE | RADEON_PRIO_FENCE_TRACE);
}

void si_set_tracked_regs_to_clear_state(struct si_context *ctx)
{
   assert(ctx->gfx_level < GFX12);
   STATIC_ASSERT(SI_NUM_ALL_TRACKED_REGS <= sizeof(ctx->tracked_regs.reg_saved_mask) * 8);

   ctx->tracked_regs.reg_value[SI_TRACKED_DB_RENDER_CONTROL] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_DB_COUNT_CONTROL] = 0;

   ctx->tracked_regs.reg_value[SI_TRACKED_DB_DEPTH_CONTROL] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_DB_STENCIL_CONTROL] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_DB_DEPTH_BOUNDS_MIN] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_DB_DEPTH_BOUNDS_MAX] = 0;

   ctx->tracked_regs.reg_value[SI_TRACKED_SPI_INTERP_CONTROL_0] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SU_POINT_SIZE] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SU_POINT_MINMAX] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SU_LINE_CNTL] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SC_MODE_CNTL_0] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SU_SC_MODE_CNTL] = 0x4;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SC_EDGERULE] = 0xaa99aaaa;

   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SU_POLY_OFFSET_DB_FMT_CNTL] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SU_POLY_OFFSET_CLAMP] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SU_POLY_OFFSET_FRONT_SCALE] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SU_POLY_OFFSET_FRONT_OFFSET] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SU_POLY_OFFSET_BACK_SCALE] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SU_POLY_OFFSET_BACK_OFFSET] = 0;

   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SC_LINE_CNTL] = 0x1000;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SC_AA_CONFIG] = 0;

   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SU_VTX_CNTL] = 0x5;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_CL_GB_VERT_CLIP_ADJ] = 0x3f800000;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_CL_GB_VERT_DISC_ADJ] = 0x3f800000;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_CL_GB_HORZ_CLIP_ADJ] = 0x3f800000;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_CL_GB_HORZ_DISC_ADJ] = 0x3f800000;

   ctx->tracked_regs.reg_value[SI_TRACKED_SPI_SHADER_POS_FORMAT] = 0;

   ctx->tracked_regs.reg_value[SI_TRACKED_SPI_SHADER_Z_FORMAT] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_SPI_SHADER_COL_FORMAT] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_SPI_PS_INPUT_ENA] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_SPI_PS_INPUT_ADDR] = 0;

   ctx->tracked_regs.reg_value[SI_TRACKED_DB_EQAA] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_DB_RENDER_OVERRIDE2] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_DB_SHADER_CONTROL] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_CB_SHADER_MASK] = 0xffffffff;
   ctx->tracked_regs.reg_value[SI_TRACKED_CB_TARGET_MASK] = 0xffffffff;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_CL_CLIP_CNTL] = 0x90000;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_CL_VS_OUT_CNTL] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_CL_VTE_CNTL] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SC_CLIPRECT_RULE] = 0xffff;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SC_LINE_STIPPLE] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SC_MODE_CNTL_1] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SU_HARDWARE_SCREEN_OFFSET] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_SPI_PS_IN_CONTROL] = 0x2;
   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_GS_INSTANCE_CNT] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_GS_MAX_VERT_OUT] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_SHADER_STAGES_EN] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_LS_HS_CONFIG] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_TF_PARAM] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SU_SMALL_PRIM_FILTER_CNTL] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_SC_BINNER_CNTL_0] = 0x3;
   ctx->tracked_regs.reg_value[SI_TRACKED_GE_MAX_OUTPUT_PER_SUBGROUP] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_GE_NGG_SUBGRP_CNTL] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_PA_CL_NGG_CNTL] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_DB_PA_SC_VRS_OVERRIDE_CNTL] = 0;

   ctx->tracked_regs.reg_value[SI_TRACKED_SX_PS_DOWNCONVERT] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_SX_BLEND_OPT_EPSILON] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_SX_BLEND_OPT_CONTROL] = 0;

   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_ESGS_RING_ITEMSIZE] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_REUSE_OFF] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_IA_MULTI_VGT_PARAM] = 0xff;

   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_GS_MAX_PRIMS_PER_SUBGROUP] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_GS_ONCHIP_CNTL] = 0;

   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_GSVS_RING_ITEMSIZE] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_GS_MODE] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_VERTEX_REUSE_BLOCK_CNTL] = 0x1e;
   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_GS_OUT_PRIM_TYPE] = 0;

   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_GSVS_RING_OFFSET_1] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_GSVS_RING_OFFSET_2] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_GSVS_RING_OFFSET_3] = 0;

   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_GS_VERT_ITEMSIZE] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_GS_VERT_ITEMSIZE_1] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_GS_VERT_ITEMSIZE_2] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_GS_VERT_ITEMSIZE_3] = 0;

   if (ctx->gfx_level >= GFX12)
      ctx->tracked_regs.reg_value[SI_TRACKED_DB_RENDER_OVERRIDE] = 0;
   else
      ctx->tracked_regs.reg_value[SI_TRACKED_SPI_VS_OUT_CONFIG] = 0;

   ctx->tracked_regs.reg_value[SI_TRACKED_VGT_PRIMITIVEID_EN] = 0;
   ctx->tracked_regs.reg_value[SI_TRACKED_CB_DCC_CONTROL] = 0;

   /* Set all cleared context registers to saved. */
   BITSET_SET_RANGE(ctx->tracked_regs.reg_saved_mask, 0, SI_NUM_TRACKED_CONTEXT_REGS - 1);
}

void si_install_draw_wrapper(struct si_context *sctx, pipe_draw_func wrapper,
                             pipe_draw_vertex_state_func vstate_wrapper)
{
   if (wrapper) {
      if (wrapper != sctx->b.draw_vbo) {
         assert(!sctx->real_draw_vbo);
         assert(!sctx->real_draw_vertex_state);
         sctx->real_draw_vbo = sctx->b.draw_vbo;
         sctx->real_draw_vertex_state = sctx->b.draw_vertex_state;
         sctx->b.draw_vbo = wrapper;
         sctx->b.draw_vertex_state = vstate_wrapper;
      }
   } else if (sctx->real_draw_vbo) {
      sctx->real_draw_vbo = NULL;
      sctx->real_draw_vertex_state = NULL;
      si_select_draw_vbo(sctx);
   }
}

static void si_tmz_preamble(struct si_context *sctx)
{
   bool secure = si_gfx_resources_check_encrypted(sctx);
   if (secure != sctx->ws->cs_is_secure(&sctx->gfx_cs)) {
      si_flush_gfx_cs(sctx, RADEON_FLUSH_ASYNC_START_NEXT_GFX_IB_NOW |
                            RADEON_FLUSH_TOGGLE_SECURE_SUBMISSION, NULL);
   }
}

static void si_draw_vbo_tmz_preamble(struct pipe_context *ctx,
                                     const struct pipe_draw_info *info,
                                     unsigned drawid_offset,
                                     const struct pipe_draw_indirect_info *indirect,
                                     const struct pipe_draw_start_count_bias *draws,
                                     unsigned num_draws) {
   struct si_context *sctx = (struct si_context *)ctx;

   si_tmz_preamble(sctx);
   sctx->real_draw_vbo(ctx, info, drawid_offset, indirect, draws, num_draws);
}

static void si_draw_vstate_tmz_preamble(struct pipe_context *ctx,
                                        struct pipe_vertex_state *state,
                                        uint32_t partial_velem_mask,
                                        struct pipe_draw_vertex_state_info info,
                                        const struct pipe_draw_start_count_bias *draws,
                                        unsigned num_draws) {
   struct si_context *sctx = (struct si_context *)ctx;

   si_tmz_preamble(sctx);
   sctx->real_draw_vertex_state(ctx, state, partial_velem_mask, info, draws, num_draws);
}

void si_begin_new_gfx_cs(struct si_context *ctx, bool first_cs)
{
   bool is_secure = false;

   if (!first_cs)
      u_trace_fini(&ctx->trace);

   u_trace_init(&ctx->trace, &ctx->ds.trace_context);

   if (unlikely(radeon_uses_secure_bos(ctx->ws))) {
      is_secure = ctx->ws->cs_is_secure(&ctx->gfx_cs);

      si_install_draw_wrapper(ctx, si_draw_vbo_tmz_preamble,
                              si_draw_vstate_tmz_preamble);
   }

   if (ctx->is_debug)
      si_begin_gfx_cs_debug(ctx);

   if (ctx->screen->gds_oa)
      ctx->ws->cs_add_buffer(&ctx->gfx_cs, ctx->screen->gds_oa, RADEON_USAGE_READWRITE, 0);

   /* Always invalidate caches at the beginning of IBs, because external
    * users (e.g. BO evictions and SDMA/UVD/VCE IBs) can modify our
    * buffers.
    *
    * Gfx10+ automatically invalidates I$, SMEM$, VMEM$, and GL1$ at the beginning of IBs,
    * so we only need to flush the GL2 cache.
    *
    * Note that the cache flush done by the kernel at the end of GFX IBs
    * isn't useful here, because that flush can finish after the following
    * IB starts drawing.
    *
    * TODO: Do we also need to invalidate CB & DB caches?
    */
   ctx->barrier_flags |= SI_BARRIER_INV_L2;
   if (ctx->gfx_level < GFX10)
      ctx->barrier_flags |= SI_BARRIER_INV_ICACHE | SI_BARRIER_INV_SMEM | SI_BARRIER_INV_VMEM;

   /* Disable pipeline stats if there are no active queries. */
   ctx->barrier_flags &= ~SI_BARRIER_EVENT_PIPELINESTAT_START & ~SI_BARRIER_EVENT_PIPELINESTAT_STOP;
   if (ctx->num_hw_pipestat_streamout_queries)
      ctx->barrier_flags |= SI_BARRIER_EVENT_PIPELINESTAT_START;
   else
      ctx->barrier_flags |= SI_BARRIER_EVENT_PIPELINESTAT_STOP;

   ctx->pipeline_stats_enabled = -1; /* indicate that the current hw state is unknown */

   /* We don't know if the last draw used NGG because it can be a different process.
    * When switching NGG->legacy, we need to flush VGT for certain hw generations.
    */
   if (ctx->screen->info.has_vgt_flush_ngg_legacy_bug && !ctx->ngg)
      ctx->barrier_flags |= SI_BARRIER_EVENT_VGT_FLUSH;

   si_mark_atom_dirty(ctx, &ctx->atoms.s.barrier);
   si_mark_atom_dirty(ctx, &ctx->atoms.s.spi_ge_ring_state);

   if (ctx->screen->attribute_pos_prim_ring) {
      radeon_add_to_buffer_list(ctx, &ctx->gfx_cs, ctx->screen->attribute_pos_prim_ring,
                                RADEON_USAGE_READWRITE | RADEON_PRIO_SHADER_RINGS);
   }
   if (ctx->border_color_buffer) {
      radeon_add_to_buffer_list(ctx, &ctx->gfx_cs, ctx->border_color_buffer,
                                RADEON_USAGE_READ | RADEON_PRIO_BORDER_COLORS);
   }
   if (ctx->shadowing.registers) {
      radeon_add_to_buffer_list(ctx, &ctx->gfx_cs, ctx->shadowing.registers,
                                RADEON_USAGE_READWRITE | RADEON_PRIO_DESCRIPTORS);

      if (ctx->shadowing.csa)
         radeon_add_to_buffer_list(ctx, &ctx->gfx_cs, ctx->shadowing.csa,
                                   RADEON_USAGE_READWRITE | RADEON_PRIO_DESCRIPTORS);
   }

   si_add_all_descriptors_to_bo_list(ctx);
   si_shader_pointers_mark_dirty(ctx);
   ctx->cs_shader_state.emitted_program = NULL;

   /* The CS initialization should be emitted before everything else. */
   if (ctx->cs_preamble_state) {
      struct si_pm4_state *preamble = is_secure ? ctx->cs_preamble_state_tmz :
                                                  ctx->cs_preamble_state;
      radeon_begin(&ctx->gfx_cs);
      radeon_emit_array(preamble->base.pm4, preamble->base.ndw);
      radeon_end();
   }

   if (!ctx->has_graphics) {
      ctx->initial_gfx_cs_size = ctx->gfx_cs.current.cdw;
      return;
   }

   if (ctx->has_tessellation) {
      radeon_add_to_buffer_list(ctx, &ctx->gfx_cs,
                                unlikely(is_secure) ? si_resource(ctx->screen->tess_rings_tmz)
                                                    : si_resource(ctx->screen->tess_rings),
                                RADEON_USAGE_READWRITE | RADEON_PRIO_SHADER_RINGS);
   }

   /* set all valid group as dirty so they get reemited on
    * next draw command
    */
   si_pm4_reset_emitted(ctx);

   if (ctx->queued.named.ls)
      ctx->prefetch_L2_mask |= SI_PREFETCH_LS;
   if (ctx->queued.named.hs)
      ctx->prefetch_L2_mask |= SI_PREFETCH_HS;
   if (ctx->queued.named.es)
      ctx->prefetch_L2_mask |= SI_PREFETCH_ES;
   if (ctx->queued.named.gs)
      ctx->prefetch_L2_mask |= SI_PREFETCH_GS;
   if (ctx->queued.named.vs)
      ctx->prefetch_L2_mask |= SI_PREFETCH_VS;
   if (ctx->queued.named.ps)
      ctx->prefetch_L2_mask |= SI_PREFETCH_PS;

   /* CLEAR_STATE disables all colorbuffers, so only enable bound ones. */
   bool has_clear_state = ctx->screen->info.has_clear_state;
   if (has_clear_state) {
      ctx->framebuffer.dirty_cbufs =
            u_bit_consecutive(0, ctx->framebuffer.state.nr_cbufs);
      /* CLEAR_STATE disables the zbuffer, so only enable it if it's bound. */
      ctx->framebuffer.dirty_zsbuf = ctx->framebuffer.state.zsbuf != NULL;
   } else {
      ctx->framebuffer.dirty_cbufs = u_bit_consecutive(0, 8);
      ctx->framebuffer.dirty_zsbuf = true;
   }

   /* RB+ depth-only rendering needs to set CB_COLOR0_INFO differently from CLEAR_STATE. */
   if (ctx->screen->info.rbplus_allowed)
      ctx->framebuffer.dirty_cbufs |= 0x1;

   /* GFX11+ needs to set NUM_SAMPLES differently from CLEAR_STATE. */
   if (ctx->gfx_level >= GFX11)
      ctx->framebuffer.dirty_zsbuf = true;

   /* Even with shadowed registers, we have to add buffers to the buffer list.
    * These atoms are the only ones that add buffers.
    *
    * The framebuffer state also needs to set PA_SC_WINDOW_SCISSOR_BR differently from CLEAR_STATE.
    */
   si_mark_atom_dirty(ctx, &ctx->atoms.s.framebuffer);
   si_mark_atom_dirty(ctx, &ctx->atoms.s.render_cond);
   if (ctx->screen->use_ngg_culling)
      si_mark_atom_dirty(ctx, &ctx->atoms.s.ngg_cull_state);

   if (first_cs || !ctx->shadowing.registers) {
      /* These don't add any buffers, so skip them with shadowing. */
      si_mark_atom_dirty(ctx, &ctx->atoms.s.clip_regs);
      /* CLEAR_STATE sets zeros. */
      if (!has_clear_state || ctx->clip_state_any_nonzeros)
         si_mark_atom_dirty(ctx, &ctx->atoms.s.clip_state);
      ctx->sample_locs_num_samples = 0;
      si_mark_atom_dirty(ctx, &ctx->atoms.s.sample_locations);
      si_mark_atom_dirty(ctx, &ctx->atoms.s.msaa_config);
      /* CLEAR_STATE sets 0xffff. */
      if (!has_clear_state || ctx->sample_mask != 0xffff)
         si_mark_atom_dirty(ctx, &ctx->atoms.s.sample_mask);
      si_mark_atom_dirty(ctx, &ctx->atoms.s.cb_render_state);
      /* CLEAR_STATE sets zeros. */
      if (!has_clear_state || ctx->blend_color_any_nonzeros)
         si_mark_atom_dirty(ctx, &ctx->atoms.s.blend_color);
      si_mark_atom_dirty(ctx, &ctx->atoms.s.db_render_state);
      if (ctx->gfx_level >= GFX9)
         si_mark_atom_dirty(ctx, &ctx->atoms.s.dpbb_state);
      si_mark_atom_dirty(ctx, &ctx->atoms.s.stencil_ref);
      si_mark_atom_dirty(ctx, &ctx->atoms.s.spi_map);
      if (ctx->gfx_level < GFX11)
         si_mark_atom_dirty(ctx, &ctx->atoms.s.streamout_enable);
      /* CLEAR_STATE disables all window rectangles. */
      if (!has_clear_state || ctx->num_window_rectangles > 0)
         si_mark_atom_dirty(ctx, &ctx->atoms.s.window_rectangles);
      si_mark_atom_dirty(ctx, &ctx->atoms.s.guardband);
      si_mark_atom_dirty(ctx, &ctx->atoms.s.scissors);
      si_mark_atom_dirty(ctx, &ctx->atoms.s.viewports);
      si_mark_atom_dirty(ctx, &ctx->atoms.s.vgt_pipeline_state);
      si_mark_atom_dirty(ctx, &ctx->atoms.s.tess_io_layout);

      /* Set all register values to unknown. */
      BITSET_ZERO(ctx->tracked_regs.reg_saved_mask);

      if (has_clear_state)
         si_set_tracked_regs_to_clear_state(ctx);

      /* 0xffffffff is an impossible value for SPI_PS_INPUT_CNTL_n registers */
      memset(ctx->tracked_regs.spi_ps_input_cntl, 0xff, sizeof(uint32_t) * 32);
   }

   /* Invalidate various draw states so that they are emitted before
    * the first draw call. */
   ctx->last_instance_count = SI_INSTANCE_COUNT_UNKNOWN;
   ctx->last_index_size = -1;
   /* Primitive restart is set to false by the gfx preamble on GFX11+. */
   ctx->last_primitive_restart_en = ctx->gfx_level >= GFX11 ? false : -1;
   ctx->last_restart_index = SI_RESTART_INDEX_UNKNOWN;
   ctx->last_prim = -1;
   ctx->last_vs_state = ~0;
   ctx->last_gs_state = ~0;
   ctx->last_ls = NULL;
   ctx->last_tcs = NULL;
   ctx->last_tes_sh_base = -1;
   ctx->last_num_tcs_input_cp = -1;

   assert(ctx->num_buffered_gfx_sh_regs == 0);
   assert(ctx->num_buffered_compute_sh_regs == 0);
   ctx->num_buffered_gfx_sh_regs = 0;
   ctx->num_buffered_compute_sh_regs = 0;

   if (ctx->scratch_buffer)
      si_mark_atom_dirty(ctx, &ctx->atoms.s.scratch_state);

   if (ctx->streamout.suspended) {
      ctx->streamout.append_bitmask = ctx->streamout.enabled_mask;
      si_streamout_buffers_dirty(ctx);
   }

   if (!list_is_empty(&ctx->active_queries))
      si_resume_queries(ctx);

   assert(!ctx->gfx_cs.prev_dw);
   ctx->initial_gfx_cs_size = ctx->gfx_cs.current.cdw;

   /* All buffer references are removed on a flush, so si_check_needs_implicit_sync
    * cannot determine if si_make_CB_shader_coherent() needs to be called.
    * ctx->force_shader_coherency.with_cb will be cleared by the first call to
    * si_make_CB_shader_coherent.
    */
   ctx->force_shader_coherency.with_cb = true;
   ctx->force_shader_coherency.with_db = true;
}

void si_trace_emit(struct si_context *sctx)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   uint32_t trace_id = ++sctx->current_saved_cs->trace_id;

   si_cp_write_data(sctx, sctx->current_saved_cs->trace_buf, 0, 4, V_370_MEM, V_370_ME, &trace_id);

   radeon_begin(cs);
   radeon_emit(PKT3(PKT3_NOP, 0, 0));
   radeon_emit(AC_ENCODE_TRACE_POINT(trace_id));
   radeon_end();

   if (sctx->log)
      u_log_flush(sctx->log);
}

/* timestamp logging for u_trace: */
void si_emit_ts(struct si_context *sctx, struct si_resource* buffer, unsigned int offset)
{
   struct radeon_cmdbuf *cs = &sctx->gfx_cs;
   uint64_t va = buffer->gpu_address + offset;
   si_cp_release_mem(sctx, cs, V_028A90_BOTTOM_OF_PIPE_TS, 0, EOP_DST_SEL_MEM, EOP_INT_SEL_NONE,
                        EOP_DATA_SEL_TIMESTAMP, buffer, va, 0, PIPE_QUERY_TIMESTAMP);
}
