/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "si_build_pm4.h"

static struct si_resource *si_get_wait_mem_scratch_bo(struct si_context *ctx,
                                                      struct radeon_cmdbuf *cs, bool is_secure)
{
   struct si_screen *sscreen = ctx->screen;

   assert(ctx->gfx_level < GFX11);

   if (likely(!is_secure)) {
      return ctx->wait_mem_scratch;
   } else {
      assert(sscreen->info.has_tmz_support);
      if (!ctx->wait_mem_scratch_tmz) {
         ctx->wait_mem_scratch_tmz =
            si_aligned_buffer_create(&sscreen->b,
                                     PIPE_RESOURCE_FLAG_UNMAPPABLE |
                                     SI_RESOURCE_FLAG_DRIVER_INTERNAL |
                                     PIPE_RESOURCE_FLAG_ENCRYPTED,
                                     PIPE_USAGE_DEFAULT, 4,
                                     sscreen->info.tcc_cache_line_size);
         si_cp_write_data(ctx, ctx->wait_mem_scratch_tmz, 0, 4, V_370_MEM, V_370_ME,
                          &ctx->wait_mem_number);
      }

      return ctx->wait_mem_scratch_tmz;
   }
}

static unsigned get_reduced_barrier_flags(struct si_context *ctx)
{
   unsigned flags = ctx->barrier_flags;

   if (!flags)
      return 0;

   if (!ctx->has_graphics) {
      /* Only process compute flags. */
      flags &= SI_BARRIER_INV_ICACHE | SI_BARRIER_INV_SMEM | SI_BARRIER_INV_VMEM |
               SI_BARRIER_INV_L2 | SI_BARRIER_WB_L2 | SI_BARRIER_INV_L2_METADATA |
               SI_BARRIER_SYNC_CS;
   }

   /* Don't flush CB and DB if there have been no draw calls. */
   if (ctx->num_draw_calls == ctx->last_cb_flush_num_draw_calls &&
       ctx->num_decompress_calls == ctx->last_cb_flush_num_decompress_calls)
      flags &= ~SI_BARRIER_SYNC_AND_INV_CB;

   if (ctx->num_draw_calls == ctx->last_db_flush_num_draw_calls &&
       ctx->num_decompress_calls == ctx->last_db_flush_num_decompress_calls)
      flags &= ~SI_BARRIER_SYNC_AND_INV_DB;

   if (!ctx->compute_is_busy)
      flags &= ~SI_BARRIER_SYNC_CS;

   /* Track the last CB/DB flush. */
   if (flags & SI_BARRIER_SYNC_AND_INV_CB) {
      ctx->num_cb_cache_flushes++;
      ctx->last_cb_flush_num_draw_calls = ctx->num_draw_calls;
      ctx->last_cb_flush_num_decompress_calls = ctx->num_decompress_calls;
   }
   if (flags & SI_BARRIER_SYNC_AND_INV_DB) {
      ctx->num_db_cache_flushes++;
      ctx->last_db_flush_num_draw_calls = ctx->num_draw_calls;
      ctx->last_db_flush_num_decompress_calls = ctx->num_decompress_calls;
   }

   /* Skip VS and PS synchronization if they are idle. */
   if (ctx->num_draw_calls == ctx->last_ps_sync_num_draw_calls)
      flags &= ~SI_BARRIER_SYNC_VS & ~SI_BARRIER_SYNC_PS;
   else if (ctx->num_draw_calls == ctx->last_vs_sync_num_draw_calls)
      flags &= ~SI_BARRIER_SYNC_VS;

   /* Track the last VS/PS flush. Flushing CB or DB also waits for PS (obviously). */
   if (flags & (SI_BARRIER_SYNC_AND_INV_CB | SI_BARRIER_SYNC_AND_INV_DB | SI_BARRIER_SYNC_PS)) {
      ctx->last_ps_sync_num_draw_calls = ctx->num_draw_calls;
      ctx->last_vs_sync_num_draw_calls = ctx->num_draw_calls;
   } else if (SI_BARRIER_SYNC_VS) {
      ctx->last_vs_sync_num_draw_calls = ctx->num_draw_calls;
   }

   /* We use a TS event to flush CB/DB on GFX9+. */
   bool uses_ts_event = ctx->gfx_level >= GFX9 &&
                        flags & (SI_BARRIER_SYNC_AND_INV_CB | SI_BARRIER_SYNC_AND_INV_DB);

   /* TS events wait for everything. */
   if (uses_ts_event)
      flags &= ~SI_BARRIER_SYNC_VS & ~SI_BARRIER_SYNC_PS & ~SI_BARRIER_SYNC_CS;

   /* TS events wait for compute too. */
   if (flags & SI_BARRIER_SYNC_CS || uses_ts_event)
      ctx->compute_is_busy = false;

   if (flags & SI_BARRIER_SYNC_VS)
      ctx->num_vs_flushes++;
   if (flags & SI_BARRIER_SYNC_PS)
      ctx->num_ps_flushes++;
   if (flags & SI_BARRIER_SYNC_CS)
      ctx->num_cs_flushes++;

   if (flags & SI_BARRIER_INV_L2)
      ctx->num_L2_invalidates++;
   else if (flags & SI_BARRIER_WB_L2)
      ctx->num_L2_writebacks++;

   ctx->barrier_flags = 0;
   return flags;
}

static void si_handle_common_barrier_events(struct si_context *ctx, struct radeon_cmdbuf *cs,
                                            unsigned flags)
{
   radeon_begin(cs);

   if (flags & SI_BARRIER_EVENT_PIPELINESTAT_START && ctx->pipeline_stats_enabled != 1) {
      radeon_event_write(V_028A90_PIPELINESTAT_START);
      ctx->pipeline_stats_enabled = 1;
   } else if (flags & SI_BARRIER_EVENT_PIPELINESTAT_STOP && ctx->pipeline_stats_enabled != 0) {
      radeon_event_write(V_028A90_PIPELINESTAT_STOP);
      ctx->pipeline_stats_enabled = 0;
   }

   if (flags & SI_BARRIER_EVENT_VGT_FLUSH)
      radeon_event_write(V_028A90_VGT_FLUSH);

   radeon_end();
}

static void gfx10_emit_barrier(struct si_context *ctx, struct radeon_cmdbuf *cs)
{
   assert(ctx->gfx_level >= GFX10);
   uint32_t gcr_cntl = 0;
   unsigned flags = get_reduced_barrier_flags(ctx);

   if (!flags)
      return;

   si_handle_common_barrier_events(ctx, cs, flags);

   /* We don't need these. */
   assert(!(flags & SI_BARRIER_EVENT_FLUSH_AND_INV_DB_META));
   assert(ctx->gfx_level < GFX12 || !(flags & SI_BARRIER_INV_L2_METADATA));

   if (flags & SI_BARRIER_INV_ICACHE)
      gcr_cntl |= S_586_GLI_INV(V_586_GLI_ALL);
   if (flags & SI_BARRIER_INV_SMEM)
      gcr_cntl |= S_586_GL1_INV(1) | S_586_GLK_INV(1);
   if (flags & SI_BARRIER_INV_VMEM)
      gcr_cntl |= S_586_GL1_INV(1) | S_586_GLV_INV(1);

   /* The L2 cache ops are:
    * - INV: - invalidate lines that reflect memory (were loaded from memory)
    *        - don't touch lines that were overwritten (were stored by gfx clients)
    * - WB: - don't touch lines that reflect memory
    *       - write back lines that were overwritten
    * - WB | INV: - invalidate lines that reflect memory
    *             - write back lines that were overwritten
    *
    * GLM doesn't support WB alone. If WB is set, INV must be set too.
    */
   if (flags & SI_BARRIER_INV_L2)
      gcr_cntl |= S_586_GL2_INV(1) | S_586_GL2_WB(1); /* Writeback and invalidate everything in L2. */
   else if (flags & SI_BARRIER_WB_L2)
      gcr_cntl |= S_586_GL2_WB(1);

   /* Invalidate the metadata cache. */
   if (ctx->gfx_level < GFX12 &&
       flags & (SI_BARRIER_INV_L2 | SI_BARRIER_WB_L2 | SI_BARRIER_INV_L2_METADATA))
      gcr_cntl |= S_586_GLM_INV(1) | S_586_GLM_WB(1);

   /* Flush CB/DB. Note that this also idles all shaders, including compute shaders. */
   if (flags & (SI_BARRIER_SYNC_AND_INV_CB | SI_BARRIER_SYNC_AND_INV_DB)) {
      unsigned cb_db_event = 0;

      /* Determine the TS event that we'll use to flush CB/DB. */
      if ((flags & SI_BARRIER_SYNC_AND_INV_CB && flags & SI_BARRIER_SYNC_AND_INV_DB) ||
          /* Gfx11 can't use the DB_META event and must use a full flush to flush DB_META. */
          (ctx->gfx_level == GFX11 && flags & SI_BARRIER_SYNC_AND_INV_DB)) {
         cb_db_event = V_028A90_CACHE_FLUSH_AND_INV_TS_EVENT;
      } else if (flags & SI_BARRIER_SYNC_AND_INV_CB) {
         cb_db_event = V_028A90_FLUSH_AND_INV_CB_DATA_TS;
      } else {
         assert(flags & SI_BARRIER_SYNC_AND_INV_DB);
         cb_db_event = V_028A90_FLUSH_AND_INV_DB_DATA_TS;
      }

      /* We must flush CMASK/FMASK/DCC separately if the main event only flushes CB_DATA. */
      radeon_begin(cs);
      if (ctx->gfx_level < GFX12 && cb_db_event == V_028A90_FLUSH_AND_INV_CB_DATA_TS)
         radeon_event_write(V_028A90_FLUSH_AND_INV_CB_META);

      /* We must flush HTILE separately if the main event only flushes DB_DATA. */
      if (ctx->gfx_level < GFX12 && cb_db_event == V_028A90_FLUSH_AND_INV_DB_DATA_TS)
         radeon_event_write(V_028A90_FLUSH_AND_INV_DB_META);

      radeon_end();

      /* First flush CB/DB, then L1/L2. */
      gcr_cntl |= S_586_SEQ(V_586_SEQ_FORWARD);

      if (ctx->gfx_level >= GFX11) {
         si_cp_release_mem_pws(ctx, cs, cb_db_event, gcr_cntl & C_586_GLI_INV);

         /* Wait for the event and invalidate remaining caches if needed. */
         si_cp_acquire_mem_pws(ctx, cs, cb_db_event,
                               flags & SI_BARRIER_PFP_SYNC_ME ? V_580_CP_PFP : V_580_CP_ME,
                               gcr_cntl & ~C_586_GLI_INV, /* keep only GLI_INV */
                               0, flags);

         gcr_cntl = 0; /* all done */
         /* ACQUIRE_MEM in PFP is implemented as ACQUIRE_MEM in ME + PFP_SYNC_ME. */
         flags &= ~SI_BARRIER_PFP_SYNC_ME;
      } else {
         /* GFX10 */
         struct si_resource *wait_mem_scratch =
           si_get_wait_mem_scratch_bo(ctx, cs, ctx->ws->cs_is_secure(cs));

         /* CB/DB flush and invalidate via RELEASE_MEM.
          * Combine this with other cache flushes when possible.
          */
         uint64_t va = wait_mem_scratch->gpu_address;
         ctx->wait_mem_number++;

         /* Get GCR_CNTL fields, because the encoding is different in RELEASE_MEM. */
         unsigned glm_wb = G_586_GLM_WB(gcr_cntl);
         unsigned glm_inv = G_586_GLM_INV(gcr_cntl);
         unsigned glv_inv = G_586_GLV_INV(gcr_cntl);
         unsigned gl1_inv = G_586_GL1_INV(gcr_cntl);
         assert(G_586_GL2_US(gcr_cntl) == 0);
         assert(G_586_GL2_RANGE(gcr_cntl) == 0);
         assert(G_586_GL2_DISCARD(gcr_cntl) == 0);
         unsigned gl2_inv = G_586_GL2_INV(gcr_cntl);
         unsigned gl2_wb = G_586_GL2_WB(gcr_cntl);
         unsigned gcr_seq = G_586_SEQ(gcr_cntl);

         gcr_cntl &= C_586_GLM_WB & C_586_GLM_INV & C_586_GLV_INV & C_586_GL1_INV & C_586_GL2_INV &
                     C_586_GL2_WB; /* keep SEQ */

         si_cp_release_mem(ctx, cs, cb_db_event,
                           S_490_GLM_WB(glm_wb) | S_490_GLM_INV(glm_inv) | S_490_GLV_INV(glv_inv) |
                           S_490_GL1_INV(gl1_inv) | S_490_GL2_INV(gl2_inv) | S_490_GL2_WB(gl2_wb) |
                           S_490_SEQ(gcr_seq),
                           EOP_DST_SEL_MEM, EOP_INT_SEL_SEND_DATA_AFTER_WR_CONFIRM,
                           EOP_DATA_SEL_VALUE_32BIT, wait_mem_scratch, va, ctx->wait_mem_number,
                           SI_NOT_QUERY);

         if (unlikely(ctx->sqtt_enabled)) {
            si_sqtt_describe_barrier_start(ctx, &ctx->gfx_cs);
         }

         si_cp_wait_mem(ctx, cs, va, ctx->wait_mem_number, 0xffffffff, WAIT_REG_MEM_EQUAL);

         if (unlikely(ctx->sqtt_enabled)) {
            si_sqtt_describe_barrier_end(ctx, &ctx->gfx_cs, flags);
         }
      }
   } else {
      /* The TS event above also makes sure that PS and CS are idle, so we have to do this only
       * if we are not flushing CB or DB.
       */
      radeon_begin(cs);
      if (flags & SI_BARRIER_SYNC_PS)
         radeon_event_write(V_028A90_PS_PARTIAL_FLUSH);
      else if (flags & SI_BARRIER_SYNC_VS)
         radeon_event_write(V_028A90_VS_PARTIAL_FLUSH);

      if (flags & SI_BARRIER_SYNC_CS)
         radeon_event_write(V_028A90_CS_PARTIAL_FLUSH);

      radeon_end();
   }

   /* Ignore fields that only modify the behavior of other fields. */
   if (gcr_cntl & C_586_GL1_RANGE & C_586_GL2_RANGE & C_586_SEQ) {
      si_cp_acquire_mem(ctx, cs, gcr_cntl,
                        flags & SI_BARRIER_PFP_SYNC_ME ? V_580_CP_PFP : V_580_CP_ME);
   } else if (flags & SI_BARRIER_PFP_SYNC_ME) {
      si_cp_pfp_sync_me(cs);
   }
}

static void gfx6_emit_barrier(struct si_context *sctx, struct radeon_cmdbuf *cs)
{
   assert(sctx->gfx_level <= GFX9);
   unsigned flags = get_reduced_barrier_flags(sctx);

   if (!flags)
      return;

   si_handle_common_barrier_events(sctx, cs, flags);

   uint32_t cp_coher_cntl = 0;
   const uint32_t flush_cb_db = flags & (SI_BARRIER_SYNC_AND_INV_CB | SI_BARRIER_SYNC_AND_INV_DB);

   /* GFX6 has a bug that it always flushes ICACHE and KCACHE if either
    * bit is set. An alternative way is to write SQC_CACHES, but that
    * doesn't seem to work reliably. Since the bug doesn't affect
    * correctness (it only does more work than necessary) and
    * the performance impact is likely negligible, there is no plan
    * to add a workaround for it.
    */

   if (flags & SI_BARRIER_INV_ICACHE)
      cp_coher_cntl |= S_0085F0_SH_ICACHE_ACTION_ENA(1);
   if (flags & SI_BARRIER_INV_SMEM)
      cp_coher_cntl |= S_0085F0_SH_KCACHE_ACTION_ENA(1);

   if (sctx->gfx_level <= GFX8) {
      if (flags & SI_BARRIER_SYNC_AND_INV_CB) {
         cp_coher_cntl |= S_0085F0_CB_ACTION_ENA(1) | S_0085F0_CB0_DEST_BASE_ENA(1) |
                          S_0085F0_CB1_DEST_BASE_ENA(1) | S_0085F0_CB2_DEST_BASE_ENA(1) |
                          S_0085F0_CB3_DEST_BASE_ENA(1) | S_0085F0_CB4_DEST_BASE_ENA(1) |
                          S_0085F0_CB5_DEST_BASE_ENA(1) | S_0085F0_CB6_DEST_BASE_ENA(1) |
                          S_0085F0_CB7_DEST_BASE_ENA(1);

         /* Necessary for DCC */
         if (sctx->gfx_level == GFX8)
            si_cp_release_mem(sctx, cs, V_028A90_FLUSH_AND_INV_CB_DATA_TS, 0, EOP_DST_SEL_MEM,
                              EOP_INT_SEL_NONE, EOP_DATA_SEL_DISCARD, NULL, 0, 0, SI_NOT_QUERY);
      }
      if (flags & SI_BARRIER_SYNC_AND_INV_DB)
         cp_coher_cntl |= S_0085F0_DB_ACTION_ENA(1) | S_0085F0_DB_DEST_BASE_ENA(1);
   }

   radeon_begin(cs);

   /* Flush CMASK/FMASK/DCC. SURFACE_SYNC will wait for idle. */
   if (flags & SI_BARRIER_SYNC_AND_INV_CB)
      radeon_event_write(V_028A90_FLUSH_AND_INV_CB_META);

   /* Flush HTILE. SURFACE_SYNC will wait for idle. */
   if (flags & (SI_BARRIER_SYNC_AND_INV_DB | SI_BARRIER_EVENT_FLUSH_AND_INV_DB_META))
      radeon_event_write(V_028A90_FLUSH_AND_INV_DB_META);

   /* Wait for shader engines to go idle.
    * VS and PS waits are unnecessary if SURFACE_SYNC is going to wait
    * for everything including CB/DB cache flushes.
    *
    * GFX6-8: SURFACE_SYNC with CB_ACTION_ENA doesn't do anything if there are no CB/DB bindings.
    * Reproducible with: piglit/arb_framebuffer_no_attachments-atomic
    *
    * GFX9: The TS event is always written after full pipeline completion regardless of CB/DB
    * bindings.
    */
   if (sctx->gfx_level <= GFX8 || !flush_cb_db) {
      if (flags & SI_BARRIER_SYNC_PS)
         radeon_event_write(V_028A90_PS_PARTIAL_FLUSH);
      else if (flags & SI_BARRIER_SYNC_VS)
         radeon_event_write(V_028A90_VS_PARTIAL_FLUSH);
   }

   if (flags & SI_BARRIER_SYNC_CS)
      radeon_event_write(V_028A90_CS_PARTIAL_FLUSH);

   radeon_end();

   /* GFX9: Wait for idle if we're flushing CB or DB. ACQUIRE_MEM doesn't
    * wait for idle on GFX9. We have to use a TS event.
    */
   if (sctx->gfx_level == GFX9 && flush_cb_db) {
      uint64_t va;
      unsigned tc_flags, cb_db_event;

      /* Set the CB/DB flush event. */
      switch (flush_cb_db) {
      case SI_BARRIER_SYNC_AND_INV_CB:
         cb_db_event = V_028A90_FLUSH_AND_INV_CB_DATA_TS;
         break;
      case SI_BARRIER_SYNC_AND_INV_DB:
         cb_db_event = V_028A90_FLUSH_AND_INV_DB_DATA_TS;
         break;
      default:
         /* both CB & DB */
         cb_db_event = V_028A90_CACHE_FLUSH_AND_INV_TS_EVENT;
      }

      /* These are the only allowed combinations. If you need to
       * do multiple operations at once, do them separately.
       * All operations that invalidate L2 also seem to invalidate
       * metadata. Volatile (VOL) and WC flushes are not listed here.
       *
       * TC    | TC_WB         = writeback & invalidate L2
       * TC    | TC_WB | TC_NC = writeback & invalidate L2 for MTYPE == NC
       *         TC_WB | TC_NC = writeback L2 for MTYPE == NC
       * TC            | TC_NC = invalidate L2 for MTYPE == NC
       * TC    | TC_MD         = writeback & invalidate L2 metadata (DCC, etc.)
       * TCL1                  = invalidate L1
       */
      tc_flags = 0;

      if (flags & SI_BARRIER_INV_L2_METADATA) {
         tc_flags = EVENT_TC_ACTION_ENA | EVENT_TC_MD_ACTION_ENA;
      }

      /* Ideally flush L2 together with CB/DB. */
      if (flags & SI_BARRIER_INV_L2) {
         /* Writeback and invalidate everything in L2 & L1. */
         tc_flags = EVENT_TC_ACTION_ENA | EVENT_TC_WB_ACTION_ENA;

         /* Clear the flags. */
         flags &= ~(SI_BARRIER_INV_L2 | SI_BARRIER_WB_L2);
      }

      /* Do the flush (enqueue the event and wait for it). */
      struct si_resource* wait_mem_scratch =
        si_get_wait_mem_scratch_bo(sctx, cs, sctx->ws->cs_is_secure(cs));

      va = wait_mem_scratch->gpu_address;
      sctx->wait_mem_number++;

      si_cp_release_mem(sctx, cs, cb_db_event, tc_flags, EOP_DST_SEL_MEM,
                        EOP_INT_SEL_SEND_DATA_AFTER_WR_CONFIRM, EOP_DATA_SEL_VALUE_32BIT,
                        wait_mem_scratch, va, sctx->wait_mem_number, SI_NOT_QUERY);

      if (unlikely(sctx->sqtt_enabled)) {
         si_sqtt_describe_barrier_start(sctx, cs);
      }

      si_cp_wait_mem(sctx, cs, va, sctx->wait_mem_number, 0xffffffff, WAIT_REG_MEM_EQUAL);

      if (unlikely(sctx->sqtt_enabled)) {
         si_sqtt_describe_barrier_end(sctx, cs, sctx->barrier_flags);
      }
   }

   /* GFX6-GFX8 only: When one of the CP_COHER_CNTL.DEST_BASE flags is set, SURFACE_SYNC waits
    * for idle, so it should be last.
    *
    * cp_coher_cntl should contain everything except TC flags at this point.
    *
    * GFX6-GFX7 don't support L2 write-back.
    */
   unsigned engine = flags & SI_BARRIER_PFP_SYNC_ME ? V_580_CP_PFP : V_580_CP_ME;

   if (flags & SI_BARRIER_INV_L2 || (sctx->gfx_level <= GFX7 && flags & SI_BARRIER_WB_L2)) {
      /* Invalidate L1 & L2. WB must be set on GFX8+ when TC_ACTION is set. */
      si_cp_acquire_mem(sctx, cs,
                        cp_coher_cntl | S_0085F0_TC_ACTION_ENA(1) | S_0085F0_TCL1_ACTION_ENA(1) |
                        S_0301F0_TC_WB_ACTION_ENA(sctx->gfx_level >= GFX8), engine);
   } else {
      /* L1 invalidation and L2 writeback must be done separately, because both operations can't
       * be done together.
       */
      if (flags & SI_BARRIER_WB_L2) {
         /* WB = write-back
          * NC = apply to non-coherent MTYPEs
          *      (i.e. MTYPE <= 1, which is what we use everywhere)
          *
          * WB doesn't work without NC.
          *
          * If we get here, the only flag that can't be executed together with WB_L2 is VMEM cache
          * invalidation.
          */
         bool last_acquire_mem = !(flags & SI_BARRIER_INV_VMEM);

         si_cp_acquire_mem(sctx, cs,
                           cp_coher_cntl | S_0301F0_TC_WB_ACTION_ENA(1) |
                           S_0301F0_TC_NC_ACTION_ENA(1),
                           /* If this is not the last ACQUIRE_MEM, flush in ME.
                            * We only want to synchronize with PFP in the last ACQUIRE_MEM. */
                           last_acquire_mem ? engine : V_580_CP_ME);

         if (last_acquire_mem)
            flags &= ~SI_BARRIER_PFP_SYNC_ME;
         cp_coher_cntl = 0;
      }

      if (flags & SI_BARRIER_INV_VMEM)
         cp_coher_cntl |= S_0085F0_TCL1_ACTION_ENA(1);

      /* If there are still some cache flags left... */
      if (cp_coher_cntl) {
         si_cp_acquire_mem(sctx, cs, cp_coher_cntl, engine);
         flags &= ~SI_BARRIER_PFP_SYNC_ME;
      }

      /* This might be needed even without any cache flags, such as when doing buffer stores
       * to an index buffer.
       */
      if (flags & SI_BARRIER_PFP_SYNC_ME)
         si_cp_pfp_sync_me(cs);
   }
}

static void si_emit_barrier_as_atom(struct si_context *sctx, unsigned index)
{
   sctx->emit_barrier(sctx, &sctx->gfx_cs);
}

static bool si_is_buffer_idle(struct si_context *sctx, struct si_resource *buf,
                              unsigned usage)
{
   return !si_cs_is_buffer_referenced(sctx, buf->buf, usage) &&
          sctx->ws->buffer_wait(sctx->ws, buf->buf, 0, usage);
}

void si_barrier_before_internal_op(struct si_context *sctx, unsigned flags,
                                   unsigned num_buffers,
                                   const struct pipe_shader_buffer *buffers,
                                   unsigned writable_buffers_mask,
                                   unsigned num_images,
                                   const struct pipe_image_view *images)
{
   for (unsigned i = 0; i < num_images; i++) {
      /* The driver doesn't decompress resources automatically for internal blits, so do it manually. */
      si_decompress_subresource(&sctx->b, images[i].resource, PIPE_MASK_RGBAZS,
                                images[i].u.tex.level, images[i].u.tex.first_layer,
                                images[i].u.tex.last_layer,
                                images[i].access & PIPE_IMAGE_ACCESS_WRITE);
   }

   /* Don't sync if buffers are idle. */
   const unsigned ps_mask = SI_BIND_CONSTANT_BUFFER(PIPE_SHADER_FRAGMENT) |
                            SI_BIND_SHADER_BUFFER(PIPE_SHADER_FRAGMENT) |
                            SI_BIND_IMAGE_BUFFER(PIPE_SHADER_FRAGMENT) |
                            SI_BIND_SAMPLER_BUFFER(PIPE_SHADER_FRAGMENT);
   const unsigned cs_mask = SI_BIND_CONSTANT_BUFFER(PIPE_SHADER_COMPUTE) |
                            SI_BIND_SHADER_BUFFER(PIPE_SHADER_COMPUTE) |
                            SI_BIND_IMAGE_BUFFER(PIPE_SHADER_COMPUTE) |
                            SI_BIND_SAMPLER_BUFFER(PIPE_SHADER_COMPUTE);

   for (unsigned i = 0; i < num_buffers; i++) {
      struct si_resource *buf = si_resource(buffers[i].buffer);

      if (!buf)
         continue;

      /* We always wait for the last write. If the buffer is used for write, also wait
       * for the last read.
       */
      if (!si_is_buffer_idle(sctx, buf, RADEON_USAGE_WRITE |
                             (writable_buffers_mask & BITFIELD_BIT(i) ? RADEON_USAGE_READ : 0))) {
         if (buf->bind_history & ps_mask)
            sctx->barrier_flags |= SI_BARRIER_SYNC_PS;
         else
            sctx->barrier_flags |= SI_BARRIER_SYNC_VS;

         if (buf->bind_history & cs_mask)
            sctx->barrier_flags |= SI_BARRIER_SYNC_CS;
      }
   }

   /* Don't sync if images are idle. */
   for (unsigned i = 0; i < num_images; i++) {
      struct si_resource *img = si_resource(images[i].resource);
      bool writable = images[i].access & PIPE_IMAGE_ACCESS_WRITE;

      /* We always wait for the last write. If the buffer is used for write, also wait
       * for the last read.
       */
      if (!si_is_buffer_idle(sctx, img, RADEON_USAGE_WRITE | (writable ? RADEON_USAGE_READ : 0))) {
         si_make_CB_shader_coherent(sctx, images[i].resource->nr_samples, true,
               ((struct si_texture*)images[i].resource)->surface.u.gfx9.color.dcc.pipe_aligned);
         sctx->barrier_flags |= SI_BARRIER_SYNC_PS | SI_BARRIER_SYNC_CS;
      }
   }

   /* Invalidate the VMEM cache only. The SMEM cache isn't used by shader buffers. */
   sctx->barrier_flags |= SI_BARRIER_INV_VMEM;
   si_mark_atom_dirty(sctx, &sctx->atoms.s.barrier);
}

void si_barrier_after_internal_op(struct si_context *sctx, unsigned flags,
                                  unsigned num_buffers,
                                  const struct pipe_shader_buffer *buffers,
                                  unsigned writable_buffers_mask,
                                  unsigned num_images,
                                  const struct pipe_image_view *images)
{
   sctx->barrier_flags |= SI_BARRIER_SYNC_CS;

   if (num_images) {
      /* Make sure image stores are visible to CB, which doesn't use L2 on GFX6-8. */
      sctx->barrier_flags |= sctx->gfx_level <= GFX8 ? SI_BARRIER_WB_L2 : 0;
      /* Make sure image stores are visible to all CUs. */
      sctx->barrier_flags |= SI_BARRIER_INV_VMEM;
   }

   /* Make sure buffer stores are visible to all CUs and also as index/indirect buffers. */
   if (num_buffers)
      sctx->barrier_flags |= SI_BARRIER_INV_SMEM | SI_BARRIER_INV_VMEM | SI_BARRIER_PFP_SYNC_ME;

   /* We must set L2_cache_dirty for buffers because:
    * - GFX6,12: CP DMA doesn't use L2.
    * - GFX6-7,12: Index buffer reads don't use L2.
    * - GFX6-8,12: CP doesn't use L2.
    * - GFX6-8: CB/DB don't use L2.
    *
    * L2_cache_dirty is checked explicitly when buffers are used in those cases to enforce coherency.
    */
   while (writable_buffers_mask)
      si_resource(buffers[u_bit_scan(&writable_buffers_mask)].buffer)->L2_cache_dirty = true;

   /* Make sure RBs see our DCC image stores if RBs and TCCs (L2 instances) are non-coherent. */
   if (sctx->gfx_level >= GFX10 && sctx->screen->info.tcc_rb_non_coherent) {
      for (unsigned i = 0; i < num_images; i++) {
         if (vi_dcc_enabled((struct si_texture*)images[i].resource, images[i].u.tex.level) &&
             images[i].access & PIPE_IMAGE_ACCESS_WRITE &&
             (sctx->screen->always_allow_dcc_stores ||
              images[i].access & SI_IMAGE_ACCESS_ALLOW_DCC_STORE)) {
            sctx->barrier_flags |= SI_BARRIER_INV_L2;
            break;
         }
      }
   }

   si_mark_atom_dirty(sctx, &sctx->atoms.s.barrier);
}

static void si_set_dst_src_barrier_buffers(struct pipe_shader_buffer *buffers,
                                           struct pipe_resource *dst, struct pipe_resource *src)
{
   assert(dst);
   memset(buffers, 0, sizeof(buffers[0]) * 2);
   /* Only the "buffer" field is going to be used. */
   buffers[0].buffer = dst;
   buffers[1].buffer = src;
}

/* This is for simple buffer ops that have 1 dst and 0-1 src. */
void si_barrier_before_simple_buffer_op(struct si_context *sctx, unsigned flags,
                                        struct pipe_resource *dst, struct pipe_resource *src)
{
   struct pipe_shader_buffer barrier_buffers[2];
   si_set_dst_src_barrier_buffers(barrier_buffers, dst, src);
   si_barrier_before_internal_op(sctx, flags, src ? 2 : 1, barrier_buffers, 0x1, 0, NULL);
}

/* This is for simple buffer ops that have 1 dst and 0-1 src. */
void si_barrier_after_simple_buffer_op(struct si_context *sctx, unsigned flags,
                                       struct pipe_resource *dst, struct pipe_resource *src)
{
   struct pipe_shader_buffer barrier_buffers[2];
   si_set_dst_src_barrier_buffers(barrier_buffers, dst, src);
   si_barrier_after_internal_op(sctx, flags, src ? 2 : 1, barrier_buffers, 0x1, 0, NULL);
}

static void si_texture_barrier(struct pipe_context *ctx, unsigned flags)
{
   si_fb_barrier_after_rendering((struct si_context *)ctx, SI_FB_BARRIER_SYNC_CB);
}

/* This enforces coherency between shader stores and any past and future access. */
static void si_memory_barrier(struct pipe_context *ctx, unsigned flags)
{
   struct si_context *sctx = (struct si_context *)ctx;

   /* Ignore PIPE_BARRIER_UPDATE_BUFFER - it synchronizes against updates like buffer_subdata. */
   /* Ignore PIPE_BARRIER_UPDATE_TEXTURE - it synchronizes against updates like texture_subdata. */
   /* Ignore PIPE_BARRIER_MAPPED_BUFFER - it synchronizes against buffer_map/unmap. */
   /* Ignore PIPE_BARRIER_QUERY_BUFFER - the GL spec description is confusing, and the driver
    * always inserts barriers around get_query_result_resource.
    */
   flags &= ~PIPE_BARRIER_UPDATE_BUFFER & ~PIPE_BARRIER_UPDATE_TEXTURE &
            ~PIPE_BARRIER_MAPPED_BUFFER & ~PIPE_BARRIER_QUERY_BUFFER;

   if (!flags)
      return;

   sctx->barrier_flags |= SI_BARRIER_SYNC_PS | SI_BARRIER_SYNC_CS;

   if (flags & PIPE_BARRIER_CONSTANT_BUFFER)
      sctx->barrier_flags |= SI_BARRIER_INV_SMEM | SI_BARRIER_INV_VMEM;

   /* VMEM cache contents are written back to L2 automatically at the end of waves, but
    * the contents of other VMEM caches might still be stale.
    *
    * TEXTURE and IMAGE mean sampler buffers and image buffers, respectively.
    */
   if (flags & (PIPE_BARRIER_VERTEX_BUFFER | PIPE_BARRIER_SHADER_BUFFER | PIPE_BARRIER_TEXTURE |
                PIPE_BARRIER_IMAGE | PIPE_BARRIER_STREAMOUT_BUFFER | PIPE_BARRIER_GLOBAL_BUFFER))
      sctx->barrier_flags |= SI_BARRIER_INV_VMEM;

   if (flags & (PIPE_BARRIER_INDEX_BUFFER | PIPE_BARRIER_INDIRECT_BUFFER))
      sctx->barrier_flags |= SI_BARRIER_PFP_SYNC_ME;

   /* Index buffers use L2 since GFX8 */
   if (flags & PIPE_BARRIER_INDEX_BUFFER &&
       (sctx->gfx_level <= GFX7 || sctx->screen->info.cp_sdma_ge_use_system_memory_scope))
      sctx->barrier_flags |= SI_BARRIER_WB_L2;

   /* Indirect buffers use L2 since GFX9. */
   if (flags & PIPE_BARRIER_INDIRECT_BUFFER &&
       (sctx->gfx_level <= GFX8 || sctx->screen->info.cp_sdma_ge_use_system_memory_scope))
      sctx->barrier_flags |= SI_BARRIER_WB_L2;

   /* MSAA color images are flushed in si_decompress_textures when needed.
    * Shaders never write to depth/stencil images.
    */
   if (flags & PIPE_BARRIER_FRAMEBUFFER && sctx->framebuffer.uncompressed_cb_mask) {
      sctx->barrier_flags |= SI_BARRIER_SYNC_AND_INV_CB;

      if (sctx->gfx_level >= GFX10 && sctx->gfx_level < GFX12) {
         if (sctx->screen->info.tcc_rb_non_coherent)
            sctx->barrier_flags |= SI_BARRIER_INV_L2;
         else /* We don't know which shaders do image stores with DCC: */
            sctx->barrier_flags |= SI_BARRIER_INV_L2_METADATA;
      } else if (sctx->gfx_level == GFX9) {
         /* We have to invalidate L2 for MSAA and when DCC can have pipe_aligned=0. */
         sctx->barrier_flags |= SI_BARRIER_INV_L2;
      } else if (sctx->gfx_level <= GFX8) {
         /* CB doesn't use L2 on GFX6-8.  */
         sctx->barrier_flags |= SI_BARRIER_WB_L2;
      }
   }

   si_mark_atom_dirty(sctx, &sctx->atoms.s.barrier);
}

static void si_set_sampler_depth_decompress_mask(struct si_context *sctx, struct si_texture *tex)
{
   assert(sctx->gfx_level < GFX12);

   /* Check all sampler bindings in all shaders where depth textures are bound, and update
    * which samplers should be decompressed.
    */
   u_foreach_bit(sh, sctx->shader_has_depth_tex) {
      u_foreach_bit(i, sctx->samplers[sh].has_depth_tex_mask) {
         if (sctx->samplers[sh].views[i]->texture == &tex->buffer.b.b) {
            sctx->samplers[sh].needs_depth_decompress_mask |= 1 << i;
            sctx->shader_needs_decompress_mask |= 1 << sh;
         }
      }
   }
}

void si_fb_barrier_before_rendering(struct si_context *sctx)
{
   /* Wait for all shaders because all image loads must finish before CB/DB can write there. */
   if (sctx->framebuffer.state.nr_cbufs || sctx->framebuffer.state.zsbuf) {
      sctx->barrier_flags |= SI_BARRIER_SYNC_CS | SI_BARRIER_SYNC_PS;
      si_mark_atom_dirty(sctx, &sctx->atoms.s.barrier);
   }
}

void si_fb_barrier_after_rendering(struct si_context *sctx, unsigned flags)
{
   if (sctx->gfx_level < GFX12 && !sctx->decompression_enabled) {
      /* Setting dirty_level_mask should ignore SI_FB_BARRIER_SYNC_* because it triggers
       * decompression, which is not syncing.
       */
      if (sctx->framebuffer.state.zsbuf) {
         struct pipe_surface *surf = sctx->framebuffer.state.zsbuf;
         struct si_texture *tex = (struct si_texture *)surf->texture;

         tex->dirty_level_mask |= 1 << surf->u.tex.level;

         if (tex->surface.has_stencil)
            tex->stencil_dirty_level_mask |= 1 << surf->u.tex.level;

         si_set_sampler_depth_decompress_mask(sctx, tex);
      }

      unsigned compressed_cb_mask = sctx->framebuffer.compressed_cb_mask;
      while (compressed_cb_mask) {
         unsigned i = u_bit_scan(&compressed_cb_mask);
         struct pipe_surface *surf = sctx->framebuffer.state.cbufs[i];
         struct si_texture *tex = (struct si_texture *)surf->texture;

         if (tex->surface.fmask_offset) {
            tex->dirty_level_mask |= 1 << surf->u.tex.level;
            tex->fmask_is_identity = false;
         }
      }
   }

   if (flags & SI_FB_BARRIER_SYNC_CB) {
      /* Compressed images (MSAA with FMASK) are flushed on demand in si_decompress_textures.
       *
       * Synchronize CB only if there is actually a bound color buffer.
       */
      if (sctx->framebuffer.uncompressed_cb_mask) {
         si_make_CB_shader_coherent(sctx, sctx->framebuffer.nr_samples,
                                    sctx->framebuffer.CB_has_shader_readable_metadata,
                                    sctx->framebuffer.all_DCC_pipe_aligned);
      }
   }

   if (flags & SI_FB_BARRIER_SYNC_DB && sctx->framebuffer.state.zsbuf) {
      /* DB caches are flushed on demand (using si_decompress_textures) except the cases below. */
      if (sctx->gfx_level >= GFX12) {
         si_make_DB_shader_coherent(sctx, sctx->framebuffer.nr_samples, true, false);
      } else if (sctx->generate_mipmap_for_depth) {
         /* u_blitter doesn't invoke depth decompression when it does multiple blits in a row,
          * but the only case when it matters for DB is when doing generate_mipmap, which writes Z,
          * which is always uncompressed. So here we flush DB manually between individual
          * generate_mipmap blits.
          */
         si_make_DB_shader_coherent(sctx, 1, false, sctx->framebuffer.DB_has_shader_readable_metadata);
      } else if (sctx->screen->info.family == CHIP_NAVI33) {
         struct si_surface *old_zsurf = (struct si_surface *)sctx->framebuffer.state.zsbuf;
         struct si_texture *old_ztex = (struct si_texture *)old_zsurf->base.texture;

         if (old_ztex->upgraded_depth) {
            /* TODO: some failures related to hyperz appeared after 969ed851 on nv33:
             * - piglit tex-miplevel-selection
             * - KHR-GL46.direct_state_access.framebuffers_texture_attachment
             * - GTF-GL46.gtf30.GL3Tests.blend_minmax.blend_minmax_draw
             * - KHR-GL46.direct_state_access.framebuffers_texture_layer_attachment
             *
             * This seems to fix them:
             */
            sctx->barrier_flags |= SI_BARRIER_SYNC_AND_INV_DB | SI_BARRIER_INV_L2;
            si_mark_atom_dirty(sctx, &sctx->atoms.s.barrier);
         }
      } else if (sctx->gfx_level == GFX9) {
         /* It appears that DB metadata "leaks" in a sequence of:
          *  - depth clear
          *  - DCC decompress for shader image writes (with DB disabled)
          *  - render with DEPTH_BEFORE_SHADER=1
          * Flushing DB metadata works around the problem.
          */
         sctx->barrier_flags |= SI_BARRIER_EVENT_FLUSH_AND_INV_DB_META;
         si_mark_atom_dirty(sctx, &sctx->atoms.s.barrier);
      }
   }
}

void si_barrier_before_image_fast_clear(struct si_context *sctx, unsigned types)
{
   /* Flush caches and wait for idle. */
   if (types & (SI_CLEAR_TYPE_CMASK | SI_CLEAR_TYPE_DCC)) {
      si_make_CB_shader_coherent(sctx, sctx->framebuffer.nr_samples,
                                 sctx->framebuffer.CB_has_shader_readable_metadata,
                                 sctx->framebuffer.all_DCC_pipe_aligned);
   }

   if (types & SI_CLEAR_TYPE_HTILE) {
      si_make_DB_shader_coherent(sctx, sctx->framebuffer.nr_samples, sctx->framebuffer.has_stencil,
                                 sctx->framebuffer.DB_has_shader_readable_metadata);
   }

   /* Invalidate the VMEM cache because we always use compute. */
   sctx->barrier_flags |= SI_BARRIER_INV_VMEM;

   /* GFX6-8: CB and DB don't use L2. */
   if (sctx->gfx_level <= GFX8)
      sctx->barrier_flags |= SI_BARRIER_INV_L2;

   si_mark_atom_dirty(sctx, &sctx->atoms.s.barrier);
}

void si_barrier_after_image_fast_clear(struct si_context *sctx)
{
   /* Wait for idle. */
   sctx->barrier_flags |= SI_BARRIER_SYNC_CS;

   /* GFX6-8: CB and DB don't use L2. */
   if (sctx->gfx_level <= GFX8)
      sctx->barrier_flags |= SI_BARRIER_WB_L2;

   si_mark_atom_dirty(sctx, &sctx->atoms.s.barrier);
}

void si_init_barrier_functions(struct si_context *sctx)
{
   if (sctx->gfx_level >= GFX10)
      sctx->emit_barrier = gfx10_emit_barrier;
   else
      sctx->emit_barrier = gfx6_emit_barrier;

   sctx->atoms.s.barrier.emit = si_emit_barrier_as_atom;

   sctx->b.memory_barrier = si_memory_barrier;
   sctx->b.texture_barrier = si_texture_barrier;
}
