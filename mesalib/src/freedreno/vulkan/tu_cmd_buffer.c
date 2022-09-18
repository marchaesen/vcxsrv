/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 */

#include "tu_cmd_buffer.h"

#include "vk_render_pass.h"
#include "vk_util.h"
#include "vk_common_entrypoints.h"

#include "tu_clear_blit.h"
#include "tu_cs.h"
#include "tu_image.h"
#include "tu_tracepoints.h"

static void
tu_clone_trace_range(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                     struct u_trace_iterator begin, struct u_trace_iterator end)
{
   if (u_trace_iterator_equal(begin, end))
      return;

   tu_cs_emit_wfi(cs);
   tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);
   u_trace_clone_append(begin, end, &cmd->trace, cs,
         tu_copy_timestamp_buffer);
}

static void
tu_clone_trace(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
               struct u_trace *trace)
{
   tu_clone_trace_range(cmd, cs, u_trace_begin_iterator(trace),
         u_trace_end_iterator(trace));
}

void
tu6_emit_event_write(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     enum vgt_event_type event)
{
   bool need_seqno = false;
   switch (event) {
   case CACHE_FLUSH_TS:
   case WT_DONE_TS:
   case RB_DONE_TS:
   case PC_CCU_FLUSH_DEPTH_TS:
   case PC_CCU_FLUSH_COLOR_TS:
   case PC_CCU_RESOLVE_TS:
      need_seqno = true;
      break;
   default:
      break;
   }

   tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, need_seqno ? 4 : 1);
   tu_cs_emit(cs, CP_EVENT_WRITE_0_EVENT(event));
   if (need_seqno) {
      tu_cs_emit_qw(cs, global_iova(cmd, seqno_dummy));
      tu_cs_emit(cs, 0);
   }
}

/* Emits the tessfactor address to the top-level CS if it hasn't been already.
 * Updating this register requires a WFI if outstanding drawing is using it, but
 * tu6_init_hardware() will have WFIed before we started and no other draws
 * could be using the tessfactor address yet since we only emit one per cmdbuf.
 */
static void
tu6_lazy_emit_tessfactor_addr(struct tu_cmd_buffer *cmd)
{
   if (cmd->state.tessfactor_addr_set)
      return;

   tu_cs_emit_regs(&cmd->cs, A6XX_PC_TESSFACTOR_ADDR(.qword = cmd->device->tess_bo->iova));
   /* Updating PC_TESSFACTOR_ADDR could race with the next draw which uses it. */
   cmd->state.cache.flush_bits |= TU_CMD_FLAG_WAIT_FOR_IDLE;
   cmd->state.tessfactor_addr_set = true;
}

static void
tu6_emit_flushes(struct tu_cmd_buffer *cmd_buffer,
                 struct tu_cs *cs,
                 enum tu_cmd_flush_bits flushes)
{
   if (unlikely(cmd_buffer->device->physical_device->instance->debug_flags & TU_DEBUG_FLUSHALL))
      flushes |= TU_CMD_FLAG_ALL_FLUSH | TU_CMD_FLAG_ALL_INVALIDATE;

   if (unlikely(cmd_buffer->device->physical_device->instance->debug_flags & TU_DEBUG_SYNCDRAW))
      flushes |= TU_CMD_FLAG_WAIT_MEM_WRITES |
                 TU_CMD_FLAG_WAIT_FOR_IDLE |
                 TU_CMD_FLAG_WAIT_FOR_ME;

   /* Experiments show that invalidating CCU while it still has data in it
    * doesn't work, so make sure to always flush before invalidating in case
    * any data remains that hasn't yet been made available through a barrier.
    * However it does seem to work for UCHE.
    */
   if (flushes & (TU_CMD_FLAG_CCU_FLUSH_COLOR |
                  TU_CMD_FLAG_CCU_INVALIDATE_COLOR))
      tu6_emit_event_write(cmd_buffer, cs, PC_CCU_FLUSH_COLOR_TS);
   if (flushes & (TU_CMD_FLAG_CCU_FLUSH_DEPTH |
                  TU_CMD_FLAG_CCU_INVALIDATE_DEPTH))
      tu6_emit_event_write(cmd_buffer, cs, PC_CCU_FLUSH_DEPTH_TS);
   if (flushes & TU_CMD_FLAG_CCU_INVALIDATE_COLOR)
      tu6_emit_event_write(cmd_buffer, cs, PC_CCU_INVALIDATE_COLOR);
   if (flushes & TU_CMD_FLAG_CCU_INVALIDATE_DEPTH)
      tu6_emit_event_write(cmd_buffer, cs, PC_CCU_INVALIDATE_DEPTH);
   if (flushes & TU_CMD_FLAG_CACHE_FLUSH)
      tu6_emit_event_write(cmd_buffer, cs, CACHE_FLUSH_TS);
   if (flushes & TU_CMD_FLAG_CACHE_INVALIDATE)
      tu6_emit_event_write(cmd_buffer, cs, CACHE_INVALIDATE);
   if (flushes & TU_CMD_FLAG_WAIT_MEM_WRITES)
      tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);
   if ((flushes & TU_CMD_FLAG_WAIT_FOR_IDLE) ||
       (cmd_buffer->device->physical_device->info->a6xx.has_ccu_flush_bug &&
        (flushes & (TU_CMD_FLAG_CCU_FLUSH_COLOR | TU_CMD_FLAG_CCU_FLUSH_DEPTH))))
      tu_cs_emit_wfi(cs);
   if (flushes & TU_CMD_FLAG_WAIT_FOR_ME)
      tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);
}

/* "Normal" cache flushes, that don't require any special handling */

static void
tu_emit_cache_flush(struct tu_cmd_buffer *cmd_buffer,
                    struct tu_cs *cs)
{
   tu6_emit_flushes(cmd_buffer, cs, cmd_buffer->state.cache.flush_bits);
   cmd_buffer->state.cache.flush_bits = 0;
}

/* Renderpass cache flushes */

void
tu_emit_cache_flush_renderpass(struct tu_cmd_buffer *cmd_buffer,
                               struct tu_cs *cs)
{
   if (!cmd_buffer->state.renderpass_cache.flush_bits &&
       likely(!cmd_buffer->device->physical_device->instance->debug_flags))
      return;
   tu6_emit_flushes(cmd_buffer, cs, cmd_buffer->state.renderpass_cache.flush_bits);
   cmd_buffer->state.renderpass_cache.flush_bits = 0;
}

/* Cache flushes for things that use the color/depth read/write path (i.e.
 * blits and draws). This deals with changing CCU state as well as the usual
 * cache flushing.
 */

void
tu_emit_cache_flush_ccu(struct tu_cmd_buffer *cmd_buffer,
                        struct tu_cs *cs,
                        enum tu_cmd_ccu_state ccu_state)
{
   enum tu_cmd_flush_bits flushes = cmd_buffer->state.cache.flush_bits;

   assert(ccu_state != TU_CMD_CCU_UNKNOWN);
   /* It's unsafe to flush inside condition because we clear flush_bits */
   assert(!cs->cond_stack_depth);

   /* Changing CCU state must involve invalidating the CCU. In sysmem mode,
    * the CCU may also contain data that we haven't flushed out yet, so we
    * also need to flush. Also, in order to program RB_CCU_CNTL, we need to
    * emit a WFI as it isn't pipelined.
    */
   if (ccu_state != cmd_buffer->state.ccu_state) {
      if (cmd_buffer->state.ccu_state != TU_CMD_CCU_GMEM) {
         flushes |=
            TU_CMD_FLAG_CCU_FLUSH_COLOR |
            TU_CMD_FLAG_CCU_FLUSH_DEPTH;
         cmd_buffer->state.cache.pending_flush_bits &= ~(
            TU_CMD_FLAG_CCU_FLUSH_COLOR |
            TU_CMD_FLAG_CCU_FLUSH_DEPTH);
      }
      flushes |=
         TU_CMD_FLAG_CCU_INVALIDATE_COLOR |
         TU_CMD_FLAG_CCU_INVALIDATE_DEPTH |
         TU_CMD_FLAG_WAIT_FOR_IDLE;
      cmd_buffer->state.cache.pending_flush_bits &= ~(
         TU_CMD_FLAG_CCU_INVALIDATE_COLOR |
         TU_CMD_FLAG_CCU_INVALIDATE_DEPTH |
         TU_CMD_FLAG_WAIT_FOR_IDLE);
   }

   tu6_emit_flushes(cmd_buffer, cs, flushes);
   cmd_buffer->state.cache.flush_bits = 0;

   if (ccu_state != cmd_buffer->state.ccu_state) {
      struct tu_physical_device *phys_dev = cmd_buffer->device->physical_device;
      tu_cs_emit_regs(cs,
                      A6XX_RB_CCU_CNTL(.color_offset =
                                          ccu_state == TU_CMD_CCU_GMEM ?
                                          phys_dev->ccu_offset_gmem :
                                          phys_dev->ccu_offset_bypass,
                                       .gmem = ccu_state == TU_CMD_CCU_GMEM));
      cmd_buffer->state.ccu_state = ccu_state;
   }
}

static void
tu6_emit_zs(struct tu_cmd_buffer *cmd,
            const struct tu_subpass *subpass,
            struct tu_cs *cs)
{
   const uint32_t a = subpass->depth_stencil_attachment.attachment;
   if (a == VK_ATTACHMENT_UNUSED) {
      tu_cs_emit_regs(cs,
                      A6XX_RB_DEPTH_BUFFER_INFO(.depth_format = DEPTH6_NONE),
                      A6XX_RB_DEPTH_BUFFER_PITCH(0),
                      A6XX_RB_DEPTH_BUFFER_ARRAY_PITCH(0),
                      A6XX_RB_DEPTH_BUFFER_BASE(0),
                      A6XX_RB_DEPTH_BUFFER_BASE_GMEM(0));

      tu_cs_emit_regs(cs,
                      A6XX_GRAS_SU_DEPTH_BUFFER_INFO(.depth_format = DEPTH6_NONE));

      tu_cs_emit_regs(cs, A6XX_RB_STENCIL_INFO(0));

      return;
   }

   const struct tu_image_view *iview = cmd->state.attachments[a];
   const struct tu_render_pass_attachment *attachment =
      &cmd->state.pass->attachments[a];
   enum a6xx_depth_format fmt = tu6_pipe2depth(attachment->format);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_DEPTH_BUFFER_INFO, 6);
   tu_cs_emit(cs, A6XX_RB_DEPTH_BUFFER_INFO(.depth_format = fmt).value);
   if (attachment->format == VK_FORMAT_D32_SFLOAT_S8_UINT)
      tu_cs_image_depth_ref(cs, iview, 0);
   else
      tu_cs_image_ref(cs, &iview->view, 0);
   tu_cs_emit(cs, tu_attachment_gmem_offset(cmd, attachment));

   tu_cs_emit_regs(cs,
                   A6XX_GRAS_SU_DEPTH_BUFFER_INFO(.depth_format = fmt));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_DEPTH_FLAG_BUFFER_BASE, 3);
   tu_cs_image_flag_ref(cs, &iview->view, 0);

   if (attachment->format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
       attachment->format == VK_FORMAT_S8_UINT) {

      tu_cs_emit_pkt4(cs, REG_A6XX_RB_STENCIL_INFO, 6);
      tu_cs_emit(cs, A6XX_RB_STENCIL_INFO(.separate_stencil = true).value);
      if (attachment->format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
         tu_cs_image_stencil_ref(cs, iview, 0);
         tu_cs_emit(cs, tu_attachment_gmem_offset_stencil(cmd, attachment));
      } else {
         tu_cs_image_ref(cs, &iview->view, 0);
         tu_cs_emit(cs, tu_attachment_gmem_offset(cmd, attachment));
      }
   } else {
      tu_cs_emit_regs(cs,
                     A6XX_RB_STENCIL_INFO(0));
   }
}

static void
tu6_emit_mrt(struct tu_cmd_buffer *cmd,
             const struct tu_subpass *subpass,
             struct tu_cs *cs)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;

   enum a6xx_format mrt0_format = 0;

   for (uint32_t i = 0; i < subpass->color_count; ++i) {
      uint32_t a = subpass->color_attachments[i].attachment;
      if (a == VK_ATTACHMENT_UNUSED) {
         /* From the VkPipelineRenderingCreateInfo definition:
          *
          *    Valid formats indicate that an attachment can be used - but it
          *    is still valid to set the attachment to NULL when beginning
          *    rendering.
          *
          * This means that with dynamic rendering, pipelines may write to
          * some attachments that are UNUSED here. Setting the format to 0
          * here should prevent them from writing to anything.
          */
         tu_cs_emit_pkt4(cs, REG_A6XX_RB_MRT_BUF_INFO(i), 6);
         for (unsigned i = 0; i < 6; i++)
            tu_cs_emit(cs, 0);
         continue;
      }

      const struct tu_image_view *iview = cmd->state.attachments[a];

      tu_cs_emit_pkt4(cs, REG_A6XX_RB_MRT_BUF_INFO(i), 6);
      tu_cs_emit(cs, iview->view.RB_MRT_BUF_INFO);
      tu_cs_image_ref(cs, &iview->view, 0);
      tu_cs_emit(cs, tu_attachment_gmem_offset(cmd, &cmd->state.pass->attachments[a]));

      tu_cs_emit_regs(cs,
                      A6XX_SP_FS_MRT_REG(i, .dword = iview->view.SP_FS_MRT_REG));

      tu_cs_emit_pkt4(cs, REG_A6XX_RB_MRT_FLAG_BUFFER_ADDR(i), 3);
      tu_cs_image_flag_ref(cs, &iview->view, 0);

      if (i == 0)
         mrt0_format = iview->view.SP_FS_MRT_REG & 0xff;
   }

   tu_cs_emit_regs(cs, A6XX_GRAS_LRZ_MRT_BUF_INFO_0(.color_format = mrt0_format));

   tu_cs_emit_regs(cs,
                   A6XX_RB_SRGB_CNTL(.dword = subpass->srgb_cntl));
   tu_cs_emit_regs(cs,
                   A6XX_SP_SRGB_CNTL(.dword = subpass->srgb_cntl));

   unsigned layers = MAX2(fb->layers, util_logbase2(subpass->multiview_mask) + 1);
   tu_cs_emit_regs(cs, A6XX_GRAS_MAX_LAYER_INDEX(layers - 1));
}

void
tu6_emit_msaa(struct tu_cs *cs, VkSampleCountFlagBits vk_samples,
              enum a5xx_line_mode line_mode)
{
   const enum a3xx_msaa_samples samples = tu_msaa_samples(vk_samples);
   bool msaa_disable = (samples == MSAA_ONE) || (line_mode == BRESENHAM);

   tu_cs_emit_regs(cs,
                   A6XX_SP_TP_RAS_MSAA_CNTL(samples),
                   A6XX_SP_TP_DEST_MSAA_CNTL(.samples = samples,
                                             .msaa_disable = msaa_disable));

   tu_cs_emit_regs(cs,
                   A6XX_GRAS_RAS_MSAA_CNTL(samples),
                   A6XX_GRAS_DEST_MSAA_CNTL(.samples = samples,
                                            .msaa_disable = msaa_disable));

   tu_cs_emit_regs(cs,
                   A6XX_RB_RAS_MSAA_CNTL(samples),
                   A6XX_RB_DEST_MSAA_CNTL(.samples = samples,
                                          .msaa_disable = msaa_disable));

   tu_cs_emit_regs(cs,
                   A6XX_RB_MSAA_CNTL(samples));
}

static void
tu6_emit_bin_size(struct tu_cs *cs,
                  uint32_t bin_w, uint32_t bin_h, uint32_t flags)
{
   tu_cs_emit_regs(cs,
                   A6XX_GRAS_BIN_CONTROL(.binw = bin_w,
                                         .binh = bin_h,
                                         .dword = flags));

   tu_cs_emit_regs(cs,
                   A6XX_RB_BIN_CONTROL(.binw = bin_w,
                                       .binh = bin_h,
                                       .dword = flags));

   /* no flag for RB_BIN_CONTROL2... */
   tu_cs_emit_regs(cs,
                   A6XX_RB_BIN_CONTROL2(.binw = bin_w,
                                        .binh = bin_h));
}

static void
tu6_emit_render_cntl(struct tu_cmd_buffer *cmd,
                     const struct tu_subpass *subpass,
                     struct tu_cs *cs,
                     bool binning)
{
   /* doesn't RB_RENDER_CNTL set differently for binning pass: */
   bool no_track = !cmd->device->physical_device->info->a6xx.has_cp_reg_write;
   uint32_t cntl = 0;
   cntl |= A6XX_RB_RENDER_CNTL_CCUSINGLECACHELINESIZE(2);
   if (binning) {
      if (no_track)
         return;
      cntl |= A6XX_RB_RENDER_CNTL_BINNING;
   } else {
      uint32_t mrts_ubwc_enable = 0;
      for (uint32_t i = 0; i < subpass->color_count; ++i) {
         uint32_t a = subpass->color_attachments[i].attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;

         const struct tu_image_view *iview = cmd->state.attachments[a];
         if (iview->view.ubwc_enabled)
            mrts_ubwc_enable |= 1 << i;
      }

      cntl |= A6XX_RB_RENDER_CNTL_FLAG_MRTS(mrts_ubwc_enable);

      const uint32_t a = subpass->depth_stencil_attachment.attachment;
      if (a != VK_ATTACHMENT_UNUSED) {
         const struct tu_image_view *iview = cmd->state.attachments[a];
         if (iview->view.ubwc_enabled)
            cntl |= A6XX_RB_RENDER_CNTL_FLAG_DEPTH;
      }

      if (no_track) {
         tu_cs_emit_pkt4(cs, REG_A6XX_RB_RENDER_CNTL, 1);
         tu_cs_emit(cs, cntl);
         return;
      }

      /* In the !binning case, we need to set RB_RENDER_CNTL in the draw_cs
       * in order to set it correctly for the different subpasses. However,
       * that means the packets we're emitting also happen during binning. So
       * we need to guard the write on !BINNING at CP execution time.
       */
      tu_cs_reserve(cs, 3 + 4);
      tu_cs_emit_pkt7(cs, CP_COND_REG_EXEC, 2);
      tu_cs_emit(cs, CP_COND_REG_EXEC_0_MODE(RENDER_MODE) |
                     CP_COND_REG_EXEC_0_GMEM | CP_COND_REG_EXEC_0_SYSMEM);
      tu_cs_emit(cs, CP_COND_REG_EXEC_1_DWORDS(4));
   }

   tu_cs_emit_pkt7(cs, CP_REG_WRITE, 3);
   tu_cs_emit(cs, CP_REG_WRITE_0_TRACKER(TRACK_RENDER_CNTL));
   tu_cs_emit(cs, REG_A6XX_RB_RENDER_CNTL);
   tu_cs_emit(cs, cntl);
}

static void
tu6_emit_blit_scissor(struct tu_cmd_buffer *cmd, struct tu_cs *cs, bool align)
{
   struct tu_physical_device *phys_dev = cmd->device->physical_device;
   const VkRect2D *render_area = &cmd->state.render_area;

   /* Avoid assertion fails with an empty render area at (0, 0) where the
    * subtraction below wraps around. Empty render areas should be forced to
    * the sysmem path by use_sysmem_rendering(). It's not even clear whether
    * an empty scissor here works, and the blob seems to force sysmem too as
    * it sets something wrong (non-empty) for the scissor.
    */
   if (render_area->extent.width == 0 ||
       render_area->extent.height == 0)
      return;

   uint32_t x1 = render_area->offset.x;
   uint32_t y1 = render_area->offset.y;
   uint32_t x2 = x1 + render_area->extent.width - 1;
   uint32_t y2 = y1 + render_area->extent.height - 1;

   if (align) {
      x1 = x1 & ~(phys_dev->info->gmem_align_w - 1);
      y1 = y1 & ~(phys_dev->info->gmem_align_h - 1);
      x2 = ALIGN_POT(x2 + 1, phys_dev->info->gmem_align_w) - 1;
      y2 = ALIGN_POT(y2 + 1, phys_dev->info->gmem_align_h) - 1;
   }

   tu_cs_emit_regs(cs,
                   A6XX_RB_BLIT_SCISSOR_TL(.x = x1, .y = y1),
                   A6XX_RB_BLIT_SCISSOR_BR(.x = x2, .y = y2));
}

void
tu6_emit_window_scissor(struct tu_cs *cs,
                        uint32_t x1,
                        uint32_t y1,
                        uint32_t x2,
                        uint32_t y2)
{
   tu_cs_emit_regs(cs,
                   A6XX_GRAS_SC_WINDOW_SCISSOR_TL(.x = x1, .y = y1),
                   A6XX_GRAS_SC_WINDOW_SCISSOR_BR(.x = x2, .y = y2));

   tu_cs_emit_regs(cs,
                   A6XX_GRAS_2D_RESOLVE_CNTL_1(.x = x1, .y = y1),
                   A6XX_GRAS_2D_RESOLVE_CNTL_2(.x = x2, .y = y2));
}

void
tu6_emit_window_offset(struct tu_cs *cs, uint32_t x1, uint32_t y1)
{
   tu_cs_emit_regs(cs,
                   A6XX_RB_WINDOW_OFFSET(.x = x1, .y = y1));

   tu_cs_emit_regs(cs,
                   A6XX_RB_WINDOW_OFFSET2(.x = x1, .y = y1));

   tu_cs_emit_regs(cs,
                   A6XX_SP_WINDOW_OFFSET(.x = x1, .y = y1));

   tu_cs_emit_regs(cs,
                   A6XX_SP_TP_WINDOW_OFFSET(.x = x1, .y = y1));
}

void
tu6_apply_depth_bounds_workaround(struct tu_device *device,
                                  uint32_t *rb_depth_cntl)
{
   if (!device->physical_device->info->a6xx.depth_bounds_require_depth_test_quirk)
      return;

   /* On some GPUs it is necessary to enable z test for depth bounds test when
    * UBWC is enabled. Otherwise, the GPU would hang. FUNC_ALWAYS is required to
    * pass z test. Relevant tests:
    *  dEQP-VK.pipeline.extended_dynamic_state.two_draws_dynamic.depth_bounds_test_disable
    *  dEQP-VK.dynamic_state.ds_state.depth_bounds_1
    */
   *rb_depth_cntl |= A6XX_RB_DEPTH_CNTL_Z_TEST_ENABLE |
                     A6XX_RB_DEPTH_CNTL_ZFUNC(FUNC_ALWAYS);
}

static void
tu_cs_emit_draw_state(struct tu_cs *cs, uint32_t id, struct tu_draw_state state)
{
   uint32_t enable_mask;
   switch (id) {
   case TU_DRAW_STATE_PROGRAM:
   /* The blob seems to not enable this (DESC_SETS_LOAD) for binning, even
    * when resources would actually be used in the binning shader.
    * Presumably the overhead of prefetching the resources isn't
    * worth it.
    */
   case TU_DRAW_STATE_DESC_SETS_LOAD:
      enable_mask = CP_SET_DRAW_STATE__0_GMEM |
                    CP_SET_DRAW_STATE__0_SYSMEM;
      break;
   case TU_DRAW_STATE_PROGRAM_BINNING:
      enable_mask = CP_SET_DRAW_STATE__0_BINNING;
      break;
   case TU_DRAW_STATE_INPUT_ATTACHMENTS_GMEM:
   case TU_DRAW_STATE_PRIM_MODE_GMEM:
      enable_mask = CP_SET_DRAW_STATE__0_GMEM;
      break;
   case TU_DRAW_STATE_INPUT_ATTACHMENTS_SYSMEM:
   case TU_DRAW_STATE_PRIM_MODE_SYSMEM:
      enable_mask = CP_SET_DRAW_STATE__0_SYSMEM;
      break;
   default:
      enable_mask = CP_SET_DRAW_STATE__0_GMEM |
                    CP_SET_DRAW_STATE__0_SYSMEM |
                    CP_SET_DRAW_STATE__0_BINNING;
      break;
   }

   STATIC_ASSERT(TU_DRAW_STATE_COUNT <= 32);

   /* We need to reload the descriptors every time the descriptor sets
    * change. However, the commands we send only depend on the pipeline
    * because the whole point is to cache descriptors which are used by the
    * pipeline. There's a problem here, in that the firmware has an
    * "optimization" which skips executing groups that are set to the same
    * value as the last draw. This means that if the descriptor sets change
    * but not the pipeline, we'd try to re-execute the same buffer which
    * the firmware would ignore and we wouldn't pre-load the new
    * descriptors. Set the DIRTY bit to avoid this optimization
    */
   if (id == TU_DRAW_STATE_DESC_SETS_LOAD)
      enable_mask |= CP_SET_DRAW_STATE__0_DIRTY;

   tu_cs_emit(cs, CP_SET_DRAW_STATE__0_COUNT(state.size) |
                  enable_mask |
                  CP_SET_DRAW_STATE__0_GROUP_ID(id) |
                  COND(!state.size, CP_SET_DRAW_STATE__0_DISABLE));
   tu_cs_emit_qw(cs, state.iova);

   assert(!state.size || state.iova);
}

static bool
use_hw_binning(struct tu_cmd_buffer *cmd)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   const struct tu_tiling_config *tiling = &fb->tiling[cmd->state.gmem_layout];

   /* XFB commands are emitted for BINNING || SYSMEM, which makes it
    * incompatible with non-hw binning GMEM rendering. this is required because
    * some of the XFB commands need to only be executed once.
    * use_sysmem_rendering() should have made sure we only ended up here if no
    * XFB was used.
    */
   if (cmd->state.rp.xfb_used) {
      assert(tiling->binning_possible);
      return true;
   }

   /* VK_QUERY_TYPE_PRIMITIVES_GENERATED_EXT emulates GL_PRIMITIVES_GENERATED,
    * which wasn't designed to care about tilers and expects the result not to
    * be multiplied by tile count.
    * See https://gitlab.khronos.org/vulkan/vulkan/-/issues/3131
    */
   if (cmd->state.rp.has_prim_generated_query_in_rp ||
       cmd->state.prim_generated_query_running_before_rp) {
      assert(tiling->binning_possible);
      return true;
   }

   return tiling->binning;
}

static bool
use_sysmem_rendering(struct tu_cmd_buffer *cmd,
                     struct tu_renderpass_result **autotune_result)
{
   if (unlikely(cmd->device->physical_device->instance->debug_flags & TU_DEBUG_SYSMEM))
      return true;

   /* can't fit attachments into gmem */
   if (!cmd->state.pass->gmem_pixels[cmd->state.gmem_layout])
      return true;

   if (cmd->state.framebuffer->layers > 1)
      return true;

   /* Use sysmem for empty render areas */
   if (cmd->state.render_area.extent.width == 0 ||
       cmd->state.render_area.extent.height == 0)
      return true;

   if (cmd->state.rp.has_tess)
      return true;

   if (cmd->state.rp.disable_gmem)
      return true;

   /* XFB is incompatible with non-hw binning GMEM rendering, see use_hw_binning */
   if (cmd->state.rp.xfb_used && !cmd->state.tiling->binning_possible)
      return true;

   /* QUERY_TYPE_PRIMITIVES_GENERATED is incompatible with non-hw binning
    * GMEM rendering, see use_hw_binning.
    */
   if ((cmd->state.rp.has_prim_generated_query_in_rp ||
        cmd->state.prim_generated_query_running_before_rp) &&
       !cmd->state.tiling->binning_possible)
      return true;

   if (unlikely(cmd->device->physical_device->instance->debug_flags & TU_DEBUG_GMEM))
      return false;

   bool use_sysmem = tu_autotune_use_bypass(&cmd->device->autotune,
                                            cmd, autotune_result);
   if (*autotune_result) {
      list_addtail(&(*autotune_result)->node, &cmd->renderpass_autotune_results);
   }

   return use_sysmem;
}

/* Optimization: there is no reason to load gmem if there is no
 * geometry to process. COND_REG_EXEC predicate is set here,
 * but the actual skip happens in tu6_emit_tile_load() and tile_store_cs,
 * for each blit separately.
 */
static void
tu6_emit_cond_for_load_stores(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                              uint32_t pipe, uint32_t slot, bool wfm)
{
   if (cmd->state.tiling->binning_possible) {
      tu_cs_emit_pkt7(cs, CP_REG_TEST, 1);
      tu_cs_emit(cs, A6XX_CP_REG_TEST_0_REG(REG_A6XX_VSC_STATE_REG(pipe)) |
                     A6XX_CP_REG_TEST_0_BIT(slot) |
                     COND(wfm, A6XX_CP_REG_TEST_0_WAIT_FOR_ME));
   } else {
      /* COND_REG_EXECs are not emitted in non-binning case */
   }
}

static void
tu6_emit_tile_select(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     uint32_t tx, uint32_t ty, uint32_t pipe, uint32_t slot)
{
   const struct tu_tiling_config *tiling = cmd->state.tiling;

   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_GMEM));

   const uint32_t x1 = tiling->tile0.width * tx;
   const uint32_t y1 = tiling->tile0.height * ty;
   const uint32_t x2 = MIN2(x1 + tiling->tile0.width - 1, MAX_VIEWPORT_SIZE - 1);
   const uint32_t y2 = MIN2(y1 + tiling->tile0.height - 1, MAX_VIEWPORT_SIZE - 1);
   tu6_emit_window_scissor(cs, x1, y1, x2, y2);
   tu6_emit_window_offset(cs, x1, y1);

   bool hw_binning = use_hw_binning(cmd);

   if (hw_binning) {
      tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);

      tu_cs_emit_pkt7(cs, CP_SET_MODE, 1);
      tu_cs_emit(cs, 0x0);

      tu_cs_emit_pkt7(cs, CP_SET_BIN_DATA5_OFFSET, 4);
      tu_cs_emit(cs, tiling->pipe_sizes[pipe] |
                     CP_SET_BIN_DATA5_0_VSC_N(slot));
      tu_cs_emit(cs, pipe * cmd->vsc_draw_strm_pitch);
      tu_cs_emit(cs, pipe * 4);
      tu_cs_emit(cs, pipe * cmd->vsc_prim_strm_pitch);
   }

   tu6_emit_cond_for_load_stores(cmd, cs, pipe, slot, hw_binning);

   tu_cs_emit_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1);
   tu_cs_emit(cs, !hw_binning);

   tu_cs_emit_pkt7(cs, CP_SET_MODE, 1);
   tu_cs_emit(cs, 0x0);
}

static void
tu6_emit_sysmem_resolve(struct tu_cmd_buffer *cmd,
                        struct tu_cs *cs,
                        uint32_t layer_mask,
                        uint32_t a,
                        uint32_t gmem_a)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   const struct tu_image_view *dst = cmd->state.attachments[a];
   const struct tu_image_view *src = cmd->state.attachments[gmem_a];

   tu_resolve_sysmem(cmd, cs, src, dst, layer_mask, fb->layers, &cmd->state.render_area);
}

static void
tu6_emit_sysmem_resolves(struct tu_cmd_buffer *cmd,
                         struct tu_cs *cs,
                         const struct tu_subpass *subpass)
{
   if (subpass->resolve_attachments) {
      /* From the documentation for vkCmdNextSubpass, section 7.4 "Render Pass
       * Commands":
       *
       *    End-of-subpass multisample resolves are treated as color
       *    attachment writes for the purposes of synchronization.
       *    This applies to resolve operations for both color and
       *    depth/stencil attachments. That is, they are considered to
       *    execute in the VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
       *    pipeline stage and their writes are synchronized with
       *    VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT. Synchronization between
       *    rendering within a subpass and any resolve operations at the end
       *    of the subpass occurs automatically, without need for explicit
       *    dependencies or pipeline barriers. However, if the resolve
       *    attachment is also used in a different subpass, an explicit
       *    dependency is needed.
       *
       * We use the CP_BLIT path for sysmem resolves, which is really a
       * transfer command, so we have to manually flush similar to the gmem
       * resolve case. However, a flush afterwards isn't needed because of the
       * last sentence and the fact that we're in sysmem mode.
       */
      tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_COLOR_TS);
      if (subpass->resolve_depth_stencil)
         tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_DEPTH_TS);

      tu6_emit_event_write(cmd, cs, CACHE_INVALIDATE);

      /* Wait for the flushes to land before using the 2D engine */
      tu_cs_emit_wfi(cs);

      for (unsigned i = 0; i < subpass->resolve_count; i++) {
         uint32_t a = subpass->resolve_attachments[i].attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;

         uint32_t gmem_a = tu_subpass_get_attachment_to_resolve(subpass, i);

         tu6_emit_sysmem_resolve(cmd, cs, subpass->multiview_mask, a, gmem_a);
      }
   }
}

static void
tu6_emit_tile_load(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   tu6_emit_blit_scissor(cmd, cs, true);

   for (uint32_t i = 0; i < cmd->state.pass->attachment_count; ++i)
      tu_load_gmem_attachment(cmd, cs, i, cmd->state.tiling->binning, false);
}

static void
tu6_emit_tile_store(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   const struct tu_render_pass *pass = cmd->state.pass;
   const struct tu_subpass *subpass = &pass->subpasses[pass->subpass_count-1];

   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_RESOLVE));

   tu6_emit_blit_scissor(cmd, cs, true);

   for (uint32_t a = 0; a < pass->attachment_count; ++a) {
      if (pass->attachments[a].gmem)
         tu_store_gmem_attachment(cmd, cs, a, a, cmd->state.tiling->binning_possible);
   }

   if (subpass->resolve_attachments) {
      for (unsigned i = 0; i < subpass->resolve_count; i++) {
         uint32_t a = subpass->resolve_attachments[i].attachment;
         if (a != VK_ATTACHMENT_UNUSED) {
            uint32_t gmem_a = tu_subpass_get_attachment_to_resolve(subpass, i);
            tu_store_gmem_attachment(cmd, cs, a, gmem_a, false);
         }
      }
   }
}

void
tu_disable_draw_states(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3);
   tu_cs_emit(cs, CP_SET_DRAW_STATE__0_COUNT(0) |
                     CP_SET_DRAW_STATE__0_DISABLE_ALL_GROUPS |
                     CP_SET_DRAW_STATE__0_GROUP_ID(0));
   tu_cs_emit(cs, CP_SET_DRAW_STATE__1_ADDR_LO(0));
   tu_cs_emit(cs, CP_SET_DRAW_STATE__2_ADDR_HI(0));

   cmd->state.dirty |= TU_CMD_DIRTY_DRAW_STATE;
}

static void
tu6_init_hw(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   struct tu_device *dev = cmd->device;
   const struct tu_physical_device *phys_dev = dev->physical_device;

   tu6_emit_event_write(cmd, cs, CACHE_INVALIDATE);

   tu_cs_emit_regs(cs, A6XX_HLSQ_INVALIDATE_CMD(
         .vs_state = true,
         .hs_state = true,
         .ds_state = true,
         .gs_state = true,
         .fs_state = true,
         .cs_state = true,
         .gfx_ibo = true,
         .cs_ibo = true,
         .gfx_shared_const = true,
         .cs_shared_const = true,
         .gfx_bindless = 0x1f,
         .cs_bindless = 0x1f));

   tu_cs_emit_wfi(cs);

   cmd->state.cache.pending_flush_bits &=
      ~(TU_CMD_FLAG_WAIT_FOR_IDLE | TU_CMD_FLAG_CACHE_INVALIDATE);

   tu_cs_emit_regs(cs,
                   A6XX_RB_CCU_CNTL(.color_offset = phys_dev->ccu_offset_bypass));
   cmd->state.ccu_state = TU_CMD_CCU_SYSMEM;
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_DBG_ECO_CNTL, 0x00100000);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_FLOAT_CNTL, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_DBG_ECO_CNTL,
                        phys_dev->info->a6xx.magic.SP_DBG_ECO_CNTL);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_PERFCTR_ENABLE, 0x3f);
   tu_cs_emit_write_reg(cs, REG_A6XX_TPL1_UNKNOWN_B605, 0x44);
   tu_cs_emit_write_reg(cs, REG_A6XX_TPL1_DBG_ECO_CNTL,
                        phys_dev->info->a6xx.magic.TPL1_DBG_ECO_CNTL);
   tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_UNKNOWN_BE00, 0x80);
   tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_UNKNOWN_BE01, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_DBG_ECO_CNTL,
                        phys_dev->info->a6xx.magic.VPC_DBG_ECO_CNTL);
   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_DBG_ECO_CNTL,
                        phys_dev->info->a6xx.magic.GRAS_DBG_ECO_CNTL);
   tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_DBG_ECO_CNTL,
                        phys_dev->info->a6xx.magic.HLSQ_DBG_ECO_CNTL);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_CHICKEN_BITS,
                        phys_dev->info->a6xx.magic.SP_CHICKEN_BITS);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_IBO_COUNT, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_B182, 0);
   tu_cs_emit_regs(cs, A6XX_HLSQ_SHARED_CONSTS(.enable = false));
   tu_cs_emit_write_reg(cs, REG_A6XX_UCHE_UNKNOWN_0E12,
                        phys_dev->info->a6xx.magic.UCHE_UNKNOWN_0E12);
   tu_cs_emit_write_reg(cs, REG_A6XX_UCHE_CLIENT_PF,
                        phys_dev->info->a6xx.magic.UCHE_CLIENT_PF);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8E01,
                        phys_dev->info->a6xx.magic.RB_UNKNOWN_8E01);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_A9A8, 0);
   tu_cs_emit_regs(cs, A6XX_SP_MODE_CONTROL(.constant_demotion_enable = true,
                                            .isammode = ISAMMODE_GL,
                                            .shared_consts_enable = false));

   /* TODO: set A6XX_VFD_ADD_OFFSET_INSTANCE and fix ir3 to avoid adding base instance */
   tu_cs_emit_write_reg(cs, REG_A6XX_VFD_ADD_OFFSET, A6XX_VFD_ADD_OFFSET_VERTEX);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8811, 0x00000010);
   tu_cs_emit_write_reg(cs, REG_A6XX_PC_MODE_CNTL,
                        phys_dev->info->a6xx.magic.PC_MODE_CNTL);

   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_UNKNOWN_8110, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8818, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8819, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_881A, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_881B, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_881C, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_881D, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_881E, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_88F0, 0);

   tu_cs_emit_regs(cs, A6XX_VPC_POINT_COORD_INVERT(false));
   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9300, 0);

   tu_cs_emit_regs(cs, A6XX_VPC_SO_DISABLE(true));

   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_B183, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_SU_CONSERVATIVE_RAS_CNTL, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_UNKNOWN_80AF, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9210, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9211, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9602, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_PC_UNKNOWN_9E72, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_TP_MODE_CNTL,
                        0x000000a0 |
                        A6XX_SP_TP_MODE_CNTL_ISAMMODE(ISAMMODE_GL));
   tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_CONTROL_5_REG, 0xfc);

   tu_cs_emit_write_reg(cs, REG_A6XX_VFD_MODE_CNTL, 0x00000000);

   tu_cs_emit_write_reg(cs, REG_A6XX_PC_MODE_CNTL, 0x0000001f);

   tu_cs_emit_regs(cs, A6XX_RB_ALPHA_CONTROL()); /* always disable alpha test */
   tu_cs_emit_regs(cs, A6XX_RB_DITHER_CNTL()); /* always disable dithering */

   tu_disable_draw_states(cmd, cs);

   tu_cs_emit_regs(cs,
                   A6XX_SP_TP_BORDER_COLOR_BASE_ADDR(.bo = dev->global_bo,
                                                     .bo_offset = gb_offset(bcolor_builtin)));
   tu_cs_emit_regs(cs,
                   A6XX_SP_PS_TP_BORDER_COLOR_BASE_ADDR(.bo = dev->global_bo,
                                                        .bo_offset = gb_offset(bcolor_builtin)));

   /* VSC buffers:
    * use vsc pitches from the largest values used so far with this device
    * if there hasn't been overflow, there will already be a scratch bo
    * allocated for these sizes
    *
    * if overflow is detected, the stream size is increased by 2x
    */
   mtx_lock(&dev->mutex);

   struct tu6_global *global = dev->global_bo->map;

   uint32_t vsc_draw_overflow = global->vsc_draw_overflow;
   uint32_t vsc_prim_overflow = global->vsc_prim_overflow;

   if (vsc_draw_overflow >= dev->vsc_draw_strm_pitch)
      dev->vsc_draw_strm_pitch = (dev->vsc_draw_strm_pitch - VSC_PAD) * 2 + VSC_PAD;

   if (vsc_prim_overflow >= dev->vsc_prim_strm_pitch)
      dev->vsc_prim_strm_pitch = (dev->vsc_prim_strm_pitch - VSC_PAD) * 2 + VSC_PAD;

   cmd->vsc_prim_strm_pitch = dev->vsc_prim_strm_pitch;
   cmd->vsc_draw_strm_pitch = dev->vsc_draw_strm_pitch;

   mtx_unlock(&dev->mutex);

   struct tu_bo *vsc_bo;
   uint32_t size0 = cmd->vsc_prim_strm_pitch * MAX_VSC_PIPES +
                    cmd->vsc_draw_strm_pitch * MAX_VSC_PIPES;

   tu_get_scratch_bo(dev, size0 + MAX_VSC_PIPES * 4, &vsc_bo);

   tu_cs_emit_regs(cs,
                   A6XX_VSC_DRAW_STRM_SIZE_ADDRESS(.bo = vsc_bo, .bo_offset = size0));
   tu_cs_emit_regs(cs,
                   A6XX_VSC_PRIM_STRM_ADDRESS(.bo = vsc_bo));
   tu_cs_emit_regs(cs,
                   A6XX_VSC_DRAW_STRM_ADDRESS(.bo = vsc_bo,
                                              .bo_offset = cmd->vsc_prim_strm_pitch * MAX_VSC_PIPES));

   tu_cs_sanity_check(cs);
}

static void
update_vsc_pipe(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   const struct tu_tiling_config *tiling = cmd->state.tiling;

   tu_cs_emit_regs(cs,
                   A6XX_VSC_BIN_SIZE(.width = tiling->tile0.width,
                                     .height = tiling->tile0.height));

   tu_cs_emit_regs(cs,
                   A6XX_VSC_BIN_COUNT(.nx = tiling->tile_count.width,
                                      .ny = tiling->tile_count.height));

   tu_cs_emit_pkt4(cs, REG_A6XX_VSC_PIPE_CONFIG_REG(0), 32);
   tu_cs_emit_array(cs, tiling->pipe_config, 32);

   tu_cs_emit_regs(cs,
                   A6XX_VSC_PRIM_STRM_PITCH(cmd->vsc_prim_strm_pitch),
                   A6XX_VSC_PRIM_STRM_LIMIT(cmd->vsc_prim_strm_pitch - VSC_PAD));

   tu_cs_emit_regs(cs,
                   A6XX_VSC_DRAW_STRM_PITCH(cmd->vsc_draw_strm_pitch),
                   A6XX_VSC_DRAW_STRM_LIMIT(cmd->vsc_draw_strm_pitch - VSC_PAD));
}

static void
emit_vsc_overflow_test(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   const struct tu_tiling_config *tiling = cmd->state.tiling;
   const uint32_t used_pipe_count =
      tiling->pipe_count.width * tiling->pipe_count.height;

   for (int i = 0; i < used_pipe_count; i++) {
      tu_cs_emit_pkt7(cs, CP_COND_WRITE5, 8);
      tu_cs_emit(cs, CP_COND_WRITE5_0_FUNCTION(WRITE_GE) |
            CP_COND_WRITE5_0_WRITE_MEMORY);
      tu_cs_emit(cs, CP_COND_WRITE5_1_POLL_ADDR_LO(REG_A6XX_VSC_DRAW_STRM_SIZE_REG(i)));
      tu_cs_emit(cs, CP_COND_WRITE5_2_POLL_ADDR_HI(0));
      tu_cs_emit(cs, CP_COND_WRITE5_3_REF(cmd->vsc_draw_strm_pitch - VSC_PAD));
      tu_cs_emit(cs, CP_COND_WRITE5_4_MASK(~0));
      tu_cs_emit_qw(cs, global_iova(cmd, vsc_draw_overflow));
      tu_cs_emit(cs, CP_COND_WRITE5_7_WRITE_DATA(cmd->vsc_draw_strm_pitch));

      tu_cs_emit_pkt7(cs, CP_COND_WRITE5, 8);
      tu_cs_emit(cs, CP_COND_WRITE5_0_FUNCTION(WRITE_GE) |
            CP_COND_WRITE5_0_WRITE_MEMORY);
      tu_cs_emit(cs, CP_COND_WRITE5_1_POLL_ADDR_LO(REG_A6XX_VSC_PRIM_STRM_SIZE_REG(i)));
      tu_cs_emit(cs, CP_COND_WRITE5_2_POLL_ADDR_HI(0));
      tu_cs_emit(cs, CP_COND_WRITE5_3_REF(cmd->vsc_prim_strm_pitch - VSC_PAD));
      tu_cs_emit(cs, CP_COND_WRITE5_4_MASK(~0));
      tu_cs_emit_qw(cs, global_iova(cmd, vsc_prim_overflow));
      tu_cs_emit(cs, CP_COND_WRITE5_7_WRITE_DATA(cmd->vsc_prim_strm_pitch));
   }

   tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);
}

static void
tu6_emit_binning_pass(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   struct tu_physical_device *phys_dev = cmd->device->physical_device;
   const struct tu_framebuffer *fb = cmd->state.framebuffer;

   tu6_emit_window_scissor(cs, 0, 0, fb->width - 1, fb->height - 1);

   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_BINNING));

   tu_cs_emit_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1);
   tu_cs_emit(cs, 0x1);

   tu_cs_emit_pkt7(cs, CP_SET_MODE, 1);
   tu_cs_emit(cs, 0x1);

   tu_cs_emit_wfi(cs);

   tu_cs_emit_regs(cs,
                   A6XX_VFD_MODE_CNTL(.render_mode = BINNING_PASS));

   update_vsc_pipe(cmd, cs);

   tu_cs_emit_regs(cs,
                   A6XX_PC_POWER_CNTL(phys_dev->info->a6xx.magic.PC_POWER_CNTL));

   tu_cs_emit_regs(cs,
                   A6XX_VFD_POWER_CNTL(phys_dev->info->a6xx.magic.PC_POWER_CNTL));

   tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 1);
   tu_cs_emit(cs, UNK_2C);

   tu_cs_emit_regs(cs,
                   A6XX_RB_WINDOW_OFFSET(.x = 0, .y = 0));

   tu_cs_emit_regs(cs,
                   A6XX_SP_TP_WINDOW_OFFSET(.x = 0, .y = 0));

   trace_start_binning_ib(&cmd->trace, cs);

   /* emit IB to binning drawcmds: */
   tu_cs_emit_call(cs, &cmd->draw_cs);

   trace_end_binning_ib(&cmd->trace, cs);

   /* switching from binning pass to GMEM pass will cause a switch from
    * PROGRAM_BINNING to PROGRAM, which invalidates const state (XS_CONST states)
    * so make sure these states are re-emitted
    * (eventually these states shouldn't exist at all with shader prologue)
    * only VS and GS are invalidated, as FS isn't emitted in binning pass,
    * and we don't use HW binning when tesselation is used
    */
   tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3);
   tu_cs_emit(cs, CP_SET_DRAW_STATE__0_COUNT(0) |
                  CP_SET_DRAW_STATE__0_DISABLE |
                  CP_SET_DRAW_STATE__0_GROUP_ID(TU_DRAW_STATE_CONST));
   tu_cs_emit(cs, CP_SET_DRAW_STATE__1_ADDR_LO(0));
   tu_cs_emit(cs, CP_SET_DRAW_STATE__2_ADDR_HI(0));

   tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 1);
   tu_cs_emit(cs, UNK_2D);

   /* This flush is probably required because the VSC, which produces the
    * visibility stream, is a client of UCHE, whereas the CP needs to read the
    * visibility stream (without caching) to do draw skipping. The
    * WFI+WAIT_FOR_ME combination guarantees that the binning commands
    * submitted are finished before reading the VSC regs (in
    * emit_vsc_overflow_test) or the VSC_DATA buffer directly (implicitly as
    * part of draws).
    */
   tu6_emit_event_write(cmd, cs, CACHE_FLUSH_TS);

   tu_cs_emit_wfi(cs);

   tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);

   emit_vsc_overflow_test(cmd, cs);

   tu_cs_emit_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1);
   tu_cs_emit(cs, 0x0);

   tu_cs_emit_pkt7(cs, CP_SET_MODE, 1);
   tu_cs_emit(cs, 0x0);
}

static struct tu_draw_state
tu_emit_input_attachments(struct tu_cmd_buffer *cmd,
                          const struct tu_subpass *subpass,
                          bool gmem)
{
   const struct tu_tiling_config *tiling = cmd->state.tiling;

   /* note: we can probably emit input attachments just once for the whole
    * renderpass, this would avoid emitting both sysmem/gmem versions
    *
    * emit two texture descriptors for each input, as a workaround for
    * d24s8/d32s8, which can be sampled as both float (depth) and integer (stencil)
    * tu_shader lowers uint input attachment loads to use the 2nd descriptor
    * in the pair
    * TODO: a smarter workaround
    */

   if (!subpass->input_count)
      return (struct tu_draw_state) {};

   struct tu_cs_memory texture;
   VkResult result = tu_cs_alloc(&cmd->sub_cs, subpass->input_count * 2,
                                 A6XX_TEX_CONST_DWORDS, &texture);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return (struct tu_draw_state) {};
   }

   for (unsigned i = 0; i < subpass->input_count * 2; i++) {
      uint32_t a = subpass->input_attachments[i / 2].attachment;
      if (a == VK_ATTACHMENT_UNUSED)
         continue;

      const struct tu_image_view *iview = cmd->state.attachments[a];
      const struct tu_render_pass_attachment *att =
         &cmd->state.pass->attachments[a];
      uint32_t *dst = &texture.map[A6XX_TEX_CONST_DWORDS * i];
      uint32_t gmem_offset = tu_attachment_gmem_offset(cmd, att);
      uint32_t cpp = att->cpp;

      memcpy(dst, iview->view.descriptor, A6XX_TEX_CONST_DWORDS * 4);

      /* Cube descriptors require a different sampling instruction in shader,
       * however we don't know whether image is a cube or not until the start
       * of a renderpass. We have to patch the descriptor to make it compatible
       * with how it is sampled in shader.
       */
      enum a6xx_tex_type tex_type = (dst[2] & A6XX_TEX_CONST_2_TYPE__MASK) >>
                                    A6XX_TEX_CONST_2_TYPE__SHIFT;
      if (tex_type == A6XX_TEX_CUBE) {
         dst[2] &= ~A6XX_TEX_CONST_2_TYPE__MASK;
         dst[2] |= A6XX_TEX_CONST_2_TYPE(A6XX_TEX_2D);

         uint32_t depth = (dst[5] & A6XX_TEX_CONST_5_DEPTH__MASK) >>
                          A6XX_TEX_CONST_5_DEPTH__SHIFT;
         dst[5] &= ~A6XX_TEX_CONST_5_DEPTH__MASK;
         dst[5] |= A6XX_TEX_CONST_5_DEPTH(depth * 6);
      }

      if (i % 2 == 1 && att->format == VK_FORMAT_D24_UNORM_S8_UINT) {
         /* note this works because spec says fb and input attachments
          * must use identity swizzle
          *
          * Also we clear swap to WZYX.  This is because the view might have
          * picked XYZW to work better with border colors.
          */
         dst[0] &= ~(A6XX_TEX_CONST_0_FMT__MASK |
            A6XX_TEX_CONST_0_SWAP__MASK |
            A6XX_TEX_CONST_0_SWIZ_X__MASK | A6XX_TEX_CONST_0_SWIZ_Y__MASK |
            A6XX_TEX_CONST_0_SWIZ_Z__MASK | A6XX_TEX_CONST_0_SWIZ_W__MASK);
         if (!cmd->device->physical_device->info->a6xx.has_z24uint_s8uint) {
            dst[0] |= A6XX_TEX_CONST_0_FMT(FMT6_8_8_8_8_UINT) |
               A6XX_TEX_CONST_0_SWIZ_X(A6XX_TEX_W) |
               A6XX_TEX_CONST_0_SWIZ_Y(A6XX_TEX_ZERO) |
               A6XX_TEX_CONST_0_SWIZ_Z(A6XX_TEX_ZERO) |
               A6XX_TEX_CONST_0_SWIZ_W(A6XX_TEX_ONE);
         } else {
            dst[0] |= A6XX_TEX_CONST_0_FMT(FMT6_Z24_UINT_S8_UINT) |
               A6XX_TEX_CONST_0_SWIZ_X(A6XX_TEX_Y) |
               A6XX_TEX_CONST_0_SWIZ_Y(A6XX_TEX_ZERO) |
               A6XX_TEX_CONST_0_SWIZ_Z(A6XX_TEX_ZERO) |
               A6XX_TEX_CONST_0_SWIZ_W(A6XX_TEX_ONE);
         }
      }

      if (i % 2 == 1 && att->format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
         dst[0] &= ~A6XX_TEX_CONST_0_FMT__MASK;
         dst[0] |= A6XX_TEX_CONST_0_FMT(FMT6_8_UINT);
         dst[2] &= ~(A6XX_TEX_CONST_2_PITCHALIGN__MASK | A6XX_TEX_CONST_2_PITCH__MASK);
         dst[2] |= A6XX_TEX_CONST_2_PITCH(iview->stencil_PITCH << 6);
         dst[3] = 0;
         dst[4] = iview->stencil_base_addr;
         dst[5] = (dst[5] & 0xffff) | iview->stencil_base_addr >> 32;

         cpp = att->samples;
         gmem_offset = att->gmem_offset_stencil[cmd->state.gmem_layout];
      }

      if (!gmem || !subpass->input_attachments[i / 2].patch_input_gmem)
         continue;

      /* patched for gmem */
      dst[0] &= ~(A6XX_TEX_CONST_0_SWAP__MASK | A6XX_TEX_CONST_0_TILE_MODE__MASK);
      dst[0] |= A6XX_TEX_CONST_0_TILE_MODE(TILE6_2);
      dst[2] =
         A6XX_TEX_CONST_2_TYPE(A6XX_TEX_2D) |
         A6XX_TEX_CONST_2_PITCH(tiling->tile0.width * cpp);
      dst[3] = 0;
      dst[4] = cmd->device->physical_device->gmem_base + gmem_offset;
      dst[5] = A6XX_TEX_CONST_5_DEPTH(1);
      for (unsigned i = 6; i < A6XX_TEX_CONST_DWORDS; i++)
         dst[i] = 0;
   }

   struct tu_cs cs;
   struct tu_draw_state ds = tu_cs_draw_state(&cmd->sub_cs, &cs, 9);

   tu_cs_emit_pkt7(&cs, CP_LOAD_STATE6_FRAG, 3);
   tu_cs_emit(&cs, CP_LOAD_STATE6_0_DST_OFF(0) |
                  CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                  CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                  CP_LOAD_STATE6_0_STATE_BLOCK(SB6_FS_TEX) |
                  CP_LOAD_STATE6_0_NUM_UNIT(subpass->input_count * 2));
   tu_cs_emit_qw(&cs, texture.iova);

   tu_cs_emit_regs(&cs, A6XX_SP_FS_TEX_CONST(.qword = texture.iova));

   tu_cs_emit_regs(&cs, A6XX_SP_FS_TEX_COUNT(subpass->input_count * 2));

   assert(cs.cur == cs.end); /* validate draw state size */

   return ds;
}

static void
tu_set_input_attachments(struct tu_cmd_buffer *cmd, const struct tu_subpass *subpass)
{
   struct tu_cs *cs = &cmd->draw_cs;

   tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 6);
   tu_cs_emit_draw_state(cs, TU_DRAW_STATE_INPUT_ATTACHMENTS_GMEM,
                         tu_emit_input_attachments(cmd, subpass, true));
   tu_cs_emit_draw_state(cs, TU_DRAW_STATE_INPUT_ATTACHMENTS_SYSMEM,
                         tu_emit_input_attachments(cmd, subpass, false));
}


static void
tu_emit_renderpass_begin(struct tu_cmd_buffer *cmd,
                         const VkClearValue *clear_values)
{
   struct tu_cs *cs = &cmd->draw_cs;

   tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_GMEM);

   tu6_emit_tile_load(cmd, cs);

   tu6_emit_blit_scissor(cmd, cs, false);

   for (uint32_t i = 0; i < cmd->state.pass->attachment_count; ++i)
      tu_clear_gmem_attachment(cmd, cs, i, &clear_values[i]);

   tu_cond_exec_end(cs);

   tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_SYSMEM);

   for (uint32_t i = 0; i < cmd->state.pass->attachment_count; ++i)
      tu_clear_sysmem_attachment(cmd, cs, i, &clear_values[i]);

   tu_cond_exec_end(cs);
}

static void
tu6_sysmem_render_begin(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                        struct tu_renderpass_result *autotune_result)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;

   tu_lrz_sysmem_begin(cmd, cs);

   assert(fb->width > 0 && fb->height > 0);
   tu6_emit_window_scissor(cs, 0, 0, fb->width - 1, fb->height - 1);
   tu6_emit_window_offset(cs, 0, 0);

   tu6_emit_bin_size(cs, 0, 0,
                     A6XX_RB_BIN_CONTROL_BUFFERS_LOCATION(BUFFERS_IN_SYSMEM) |
                     A6XX_RB_BIN_CONTROL_FORCE_LRZ_WRITE_DIS);

   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_BYPASS));

   tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0x0);

   tu_emit_cache_flush_ccu(cmd, cs, TU_CMD_CCU_SYSMEM);

   tu_cs_emit_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1);
   tu_cs_emit(cs, 0x1);

   tu_cs_emit_pkt7(cs, CP_SET_MODE, 1);
   tu_cs_emit(cs, 0x0);

   tu_autotune_begin_renderpass(cmd, cs, autotune_result);

   tu_cs_sanity_check(cs);
}

static void
tu6_sysmem_render_end(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                      struct tu_renderpass_result *autotune_result)
{
   tu_autotune_end_renderpass(cmd, cs, autotune_result);

   /* Do any resolves of the last subpass. These are handled in the
    * tile_store_cs in the gmem path.
    */
   tu6_emit_sysmem_resolves(cmd, cs, cmd->state.subpass);

   tu_cs_emit_call(cs, &cmd->draw_epilogue_cs);

   tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0x0);

   tu_lrz_sysmem_end(cmd, cs);

   tu_cs_sanity_check(cs);
}

static void
tu6_tile_render_begin(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                      struct tu_renderpass_result *autotune_result)
{
   struct tu_physical_device *phys_dev = cmd->device->physical_device;
   const struct tu_tiling_config *tiling = cmd->state.tiling;
   tu_lrz_tiling_begin(cmd, cs);

   tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0x0);

   tu_emit_cache_flush_ccu(cmd, cs, TU_CMD_CCU_GMEM);

   if (use_hw_binning(cmd)) {
      tu6_emit_bin_size(cs, tiling->tile0.width, tiling->tile0.height,
                        A6XX_RB_BIN_CONTROL_RENDER_MODE(BINNING_PASS) |
                        A6XX_RB_BIN_CONTROL_LRZ_FEEDBACK_ZMODE_MASK(0x6));

      tu6_emit_render_cntl(cmd, cmd->state.subpass, cs, true);

      tu6_emit_binning_pass(cmd, cs);

      tu6_emit_bin_size(cs, tiling->tile0.width, tiling->tile0.height,
                        A6XX_RB_BIN_CONTROL_FORCE_LRZ_WRITE_DIS |
                        A6XX_RB_BIN_CONTROL_LRZ_FEEDBACK_ZMODE_MASK(0x6));

      tu_cs_emit_regs(cs,
                      A6XX_VFD_MODE_CNTL(0));

      tu_cs_emit_regs(cs,
                      A6XX_PC_POWER_CNTL(phys_dev->info->a6xx.magic.PC_POWER_CNTL));

      tu_cs_emit_regs(cs,
                      A6XX_VFD_POWER_CNTL(phys_dev->info->a6xx.magic.PC_POWER_CNTL));

      tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
      tu_cs_emit(cs, 0x1);
      tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_LOCAL, 1);
      tu_cs_emit(cs, 0x1);
   } else {
      tu6_emit_bin_size(cs, tiling->tile0.width, tiling->tile0.height,
                        A6XX_RB_BIN_CONTROL_LRZ_FEEDBACK_ZMODE_MASK(0x6));

      if (tiling->binning_possible) {
         /* Mark all tiles as visible for tu6_emit_cond_for_load_stores(), since
          * the actual binner didn't run.
          */
         int pipe_count = tiling->pipe_count.width * tiling->pipe_count.height;
         tu_cs_emit_pkt4(cs, REG_A6XX_VSC_STATE_REG(0), pipe_count);
         for (int i = 0; i < pipe_count; i++)
            tu_cs_emit(cs, ~0);
      }
   }

   tu_autotune_begin_renderpass(cmd, cs, autotune_result);

   tu_cs_sanity_check(cs);
}

static void
tu6_render_tile(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                uint32_t tx, uint32_t ty, uint32_t pipe, uint32_t slot)
{
   tu6_emit_tile_select(cmd, &cmd->cs, tx, ty, pipe, slot);

   trace_start_draw_ib_gmem(&cmd->trace, &cmd->cs);

   /* Primitives that passed all tests are still counted in in each
    * tile even with HW binning beforehand. Do not permit it.
    */
   if (cmd->state.prim_generated_query_running_before_rp)
      tu6_emit_event_write(cmd, cs, STOP_PRIMITIVE_CTRS);

   tu_cs_emit_call(cs, &cmd->draw_cs);

   if (cmd->state.prim_generated_query_running_before_rp)
      tu6_emit_event_write(cmd, cs, START_PRIMITIVE_CTRS);

   if (use_hw_binning(cmd)) {
      tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
      tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_ENDVIS));
   }

   /* Predicate is changed in draw_cs so we have to re-emit it */
   if (cmd->state.rp.draw_cs_writes_to_cond_pred)
      tu6_emit_cond_for_load_stores(cmd, cs, pipe, slot, false);

   tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0x0);

   tu_cs_emit_call(cs, &cmd->tile_store_cs);

   tu_clone_trace_range(cmd, cs, cmd->trace_renderpass_start,
         cmd->trace_renderpass_end);

   tu_cs_sanity_check(cs);

   trace_end_draw_ib_gmem(&cmd->trace, &cmd->cs);
}

static void
tu6_tile_render_end(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                    struct tu_renderpass_result *autotune_result)
{
   tu_autotune_end_renderpass(cmd, cs, autotune_result);

   tu_cs_emit_call(cs, &cmd->draw_epilogue_cs);

   tu_lrz_tiling_end(cmd, cs);

   tu6_emit_event_write(cmd, cs, PC_CCU_RESOLVE_TS);

   tu_cs_sanity_check(cs);
}

static void
tu_cmd_render_tiles(struct tu_cmd_buffer *cmd,
                    struct tu_renderpass_result *autotune_result)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   const struct tu_tiling_config *tiling = cmd->state.tiling;

   /* Create gmem stores now (at EndRenderPass time)) because they needed to
    * know whether to allow their conditional execution, which was tied to a
    * state that was known only at the end of the renderpass.  They will be
    * called from tu6_render_tile().
    */
   tu_cs_begin(&cmd->tile_store_cs);
   tu6_emit_tile_store(cmd, &cmd->tile_store_cs);
   tu_cs_end(&cmd->tile_store_cs);

   cmd->trace_renderpass_end = u_trace_end_iterator(&cmd->trace);

   tu6_tile_render_begin(cmd, &cmd->cs, autotune_result);

   /* Note: we reverse the order of walking the pipes and tiles on every
    * other row, to improve texture cache locality compared to raster order.
    */
   for (uint32_t py = 0; py < tiling->pipe_count.height; py++) {
      uint32_t pipe_row = py * tiling->pipe_count.width;
      for (uint32_t pipe_row_i = 0; pipe_row_i < tiling->pipe_count.width; pipe_row_i++) {
         uint32_t px;
         if (py & 1)
            px = tiling->pipe_count.width - 1 - pipe_row_i;
         else
            px = pipe_row_i;
         uint32_t pipe = pipe_row + px;
         uint32_t tx1 = px * tiling->pipe0.width;
         uint32_t ty1 = py * tiling->pipe0.height;
         uint32_t tx2 = MIN2(tx1 + tiling->pipe0.width, tiling->tile_count.width);
         uint32_t ty2 = MIN2(ty1 + tiling->pipe0.height, tiling->tile_count.height);
         uint32_t tile_row_stride = tx2 - tx1;
         uint32_t slot_row = 0;
         for (uint32_t ty = ty1; ty < ty2; ty++) {
            for (uint32_t tile_row_i = 0; tile_row_i < tile_row_stride; tile_row_i++) {
               uint32_t tx;
               if (ty & 1)
                  tx = tile_row_stride - 1 - tile_row_i;
               else
                  tx = tile_row_i;
               uint32_t slot = slot_row + tx;
               tu6_render_tile(cmd, &cmd->cs, tx1 + tx, ty, pipe, slot);
            }
            slot_row += tile_row_stride;
         }
      }
   }

   tu6_tile_render_end(cmd, &cmd->cs, autotune_result);

   trace_end_render_pass(&cmd->trace, &cmd->cs, fb, tiling);

   /* tu6_render_tile has cloned these tracepoints for each tile */
   if (!u_trace_iterator_equal(cmd->trace_renderpass_start, cmd->trace_renderpass_end))
      u_trace_disable_event_range(cmd->trace_renderpass_start,
                                  cmd->trace_renderpass_end);

   /* Reset the gmem store CS entry lists so that the next render pass
    * does its own stores.
    */
   tu_cs_discard_entries(&cmd->tile_store_cs);
}

static void
tu_cmd_render_sysmem(struct tu_cmd_buffer *cmd,
                     struct tu_renderpass_result *autotune_result)
{
   cmd->trace_renderpass_end = u_trace_end_iterator(&cmd->trace);

   tu6_sysmem_render_begin(cmd, &cmd->cs, autotune_result);

   trace_start_draw_ib_sysmem(&cmd->trace, &cmd->cs);

   tu_cs_emit_call(&cmd->cs, &cmd->draw_cs);

   trace_end_draw_ib_sysmem(&cmd->trace, &cmd->cs);

   tu6_sysmem_render_end(cmd, &cmd->cs, autotune_result);

   trace_end_render_pass(&cmd->trace, &cmd->cs, cmd->state.framebuffer, cmd->state.tiling);
}

void
tu_cmd_render(struct tu_cmd_buffer *cmd_buffer)
{
   if (cmd_buffer->state.rp.has_tess)
      tu6_lazy_emit_tessfactor_addr(cmd_buffer);

   struct tu_renderpass_result *autotune_result = NULL;
   if (use_sysmem_rendering(cmd_buffer, &autotune_result))
      tu_cmd_render_sysmem(cmd_buffer, autotune_result);
   else
      tu_cmd_render_tiles(cmd_buffer, autotune_result);

   /* Outside of renderpasses we assume all draw states are disabled. We do
    * this outside the draw CS for the normal case where 3d gmem stores aren't
    * used.
    */
   tu_disable_draw_states(cmd_buffer, &cmd_buffer->cs);

}

static void tu_reset_render_pass(struct tu_cmd_buffer *cmd_buffer)
{
   /* discard draw_cs and draw_epilogue_cs entries now that the tiles are
      rendered */
   tu_cs_discard_entries(&cmd_buffer->draw_cs);
   tu_cs_begin(&cmd_buffer->draw_cs);
   tu_cs_discard_entries(&cmd_buffer->draw_epilogue_cs);
   tu_cs_begin(&cmd_buffer->draw_epilogue_cs);

   cmd_buffer->state.pass = NULL;
   cmd_buffer->state.subpass = NULL;
   cmd_buffer->state.framebuffer = NULL;
   cmd_buffer->state.attachments = NULL;
   cmd_buffer->state.gmem_layout = TU_GMEM_LAYOUT_COUNT; /* invalid value to prevent looking up gmem offsets */
   memset(&cmd_buffer->state.rp, 0, sizeof(cmd_buffer->state.rp));

   /* LRZ is not valid next time we use it */
   cmd_buffer->state.lrz.valid = false;
   cmd_buffer->state.dirty |= TU_CMD_DIRTY_LRZ;
}

static VkResult
tu_create_cmd_buffer(struct vk_command_pool *pool,
                     struct vk_command_buffer **cmd_buffer_out)
{
   struct tu_device *device =
      container_of(pool->base.device, struct tu_device, vk);
   struct tu_cmd_buffer *cmd_buffer;

   cmd_buffer = vk_zalloc2(&device->vk.alloc, NULL, sizeof(*cmd_buffer), 8,
                           VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (cmd_buffer == NULL)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   VkResult result = vk_command_buffer_init(pool, &cmd_buffer->vk,
                                            &tu_cmd_buffer_ops, 0);
   if (result != VK_SUCCESS) {
      vk_free2(&device->vk.alloc, NULL, cmd_buffer);
      return result;
   }

   cmd_buffer->device = device;

   u_trace_init(&cmd_buffer->trace, &device->trace_context);
   list_inithead(&cmd_buffer->renderpass_autotune_results);

   tu_cs_init(&cmd_buffer->cs, device, TU_CS_MODE_GROW, 4096);
   tu_cs_init(&cmd_buffer->draw_cs, device, TU_CS_MODE_GROW, 4096);
   tu_cs_init(&cmd_buffer->tile_store_cs, device, TU_CS_MODE_GROW, 2048);
   tu_cs_init(&cmd_buffer->draw_epilogue_cs, device, TU_CS_MODE_GROW, 4096);
   tu_cs_init(&cmd_buffer->sub_cs, device, TU_CS_MODE_SUB_STREAM, 2048);
   tu_cs_init(&cmd_buffer->pre_chain.draw_cs, device, TU_CS_MODE_GROW, 4096);
   tu_cs_init(&cmd_buffer->pre_chain.draw_epilogue_cs, device, TU_CS_MODE_GROW, 4096);

   *cmd_buffer_out = &cmd_buffer->vk;

   return VK_SUCCESS;
}

static void
tu_cmd_buffer_destroy(struct vk_command_buffer *vk_cmd_buffer)
{
   struct tu_cmd_buffer *cmd_buffer =
      container_of(vk_cmd_buffer, struct tu_cmd_buffer, vk);

   tu_cs_finish(&cmd_buffer->cs);
   tu_cs_finish(&cmd_buffer->draw_cs);
   tu_cs_finish(&cmd_buffer->tile_store_cs);
   tu_cs_finish(&cmd_buffer->draw_epilogue_cs);
   tu_cs_finish(&cmd_buffer->sub_cs);
   tu_cs_finish(&cmd_buffer->pre_chain.draw_cs);
   tu_cs_finish(&cmd_buffer->pre_chain.draw_epilogue_cs);

   u_trace_fini(&cmd_buffer->trace);

   tu_autotune_free_results(cmd_buffer->device, &cmd_buffer->renderpass_autotune_results);

   for (unsigned i = 0; i < MAX_BIND_POINTS; i++) {
      if (cmd_buffer->descriptors[i].push_set.layout)
         vk_descriptor_set_layout_unref(&cmd_buffer->device->vk,
                                        &cmd_buffer->descriptors[i].push_set.layout->vk);
      vk_free(&cmd_buffer->device->vk.alloc,
              cmd_buffer->descriptors[i].push_set.mapped_ptr);
   }

   vk_command_buffer_finish(&cmd_buffer->vk);
   vk_free2(&cmd_buffer->device->vk.alloc, &cmd_buffer->vk.pool->alloc,
            cmd_buffer);
}

static void
tu_reset_cmd_buffer(struct vk_command_buffer *vk_cmd_buffer,
                    UNUSED VkCommandBufferResetFlags flags)
{
   struct tu_cmd_buffer *cmd_buffer =
      container_of(vk_cmd_buffer, struct tu_cmd_buffer, vk);

   vk_command_buffer_reset(&cmd_buffer->vk);

   tu_cs_reset(&cmd_buffer->cs);
   tu_cs_reset(&cmd_buffer->draw_cs);
   tu_cs_reset(&cmd_buffer->tile_store_cs);
   tu_cs_reset(&cmd_buffer->draw_epilogue_cs);
   tu_cs_reset(&cmd_buffer->sub_cs);
   tu_cs_reset(&cmd_buffer->pre_chain.draw_cs);
   tu_cs_reset(&cmd_buffer->pre_chain.draw_epilogue_cs);

   tu_autotune_free_results(cmd_buffer->device, &cmd_buffer->renderpass_autotune_results);

   for (unsigned i = 0; i < MAX_BIND_POINTS; i++) {
      memset(&cmd_buffer->descriptors[i].sets, 0, sizeof(cmd_buffer->descriptors[i].sets));
      if (cmd_buffer->descriptors[i].push_set.layout) {
         vk_descriptor_set_layout_unref(&cmd_buffer->device->vk,
                                        &cmd_buffer->descriptors[i].push_set.layout->vk);
      }
      memset(&cmd_buffer->descriptors[i].push_set, 0, sizeof(cmd_buffer->descriptors[i].push_set));
      cmd_buffer->descriptors[i].push_set.base.type = VK_OBJECT_TYPE_DESCRIPTOR_SET;
      cmd_buffer->descriptors[i].max_sets_bound = 0;
      cmd_buffer->descriptors[i].dynamic_bound = 0;
   }

   u_trace_fini(&cmd_buffer->trace);
   u_trace_init(&cmd_buffer->trace, &cmd_buffer->device->trace_context);

   cmd_buffer->state.max_vbs_bound = 0;

   cmd_buffer->status = TU_CMD_BUFFER_STATUS_INITIAL;
}

const struct vk_command_buffer_ops tu_cmd_buffer_ops = {
   .create = tu_create_cmd_buffer,
   .reset = tu_reset_cmd_buffer,
   .destroy = tu_cmd_buffer_destroy,
};

/* Initialize the cache, assuming all necessary flushes have happened but *not*
 * invalidations.
 */
static void
tu_cache_init(struct tu_cache_state *cache)
{
   cache->flush_bits = 0;
   cache->pending_flush_bits = TU_CMD_FLAG_ALL_INVALIDATE;
}

/* Unlike the public entrypoint, this doesn't handle cache tracking, and
 * tracking the CCU state. It's used for the driver to insert its own command
 * buffer in the middle of a submit.
 */
VkResult
tu_cmd_buffer_begin(struct tu_cmd_buffer *cmd_buffer,
                    VkCommandBufferUsageFlags usage_flags)
{
   if (cmd_buffer->status != TU_CMD_BUFFER_STATUS_INITIAL) {
      /* If the command buffer has already been resetted with
       * vkResetCommandBuffer, no need to do it again.
       */
      tu_reset_cmd_buffer(&cmd_buffer->vk, 0);
   }

   memset(&cmd_buffer->state, 0, sizeof(cmd_buffer->state));
   cmd_buffer->state.index_size = 0xff; /* dirty restart index */
   cmd_buffer->state.line_mode = RECTANGULAR;
   cmd_buffer->state.gmem_layout = TU_GMEM_LAYOUT_COUNT; /* dirty value */

   tu_cache_init(&cmd_buffer->state.cache);
   tu_cache_init(&cmd_buffer->state.renderpass_cache);
   cmd_buffer->usage_flags = usage_flags;

   tu_cs_begin(&cmd_buffer->cs);
   tu_cs_begin(&cmd_buffer->draw_cs);
   tu_cs_begin(&cmd_buffer->draw_epilogue_cs);

   cmd_buffer->status = TU_CMD_BUFFER_STATUS_RECORDING;
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_BeginCommandBuffer(VkCommandBuffer commandBuffer,
                      const VkCommandBufferBeginInfo *pBeginInfo)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   VkResult result = tu_cmd_buffer_begin(cmd_buffer, pBeginInfo->flags);
   if (result != VK_SUCCESS)
      return result;

   /* setup initial configuration into command buffer */
   if (cmd_buffer->vk.level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
      trace_start_cmd_buffer(&cmd_buffer->trace, &cmd_buffer->cs);

      switch (cmd_buffer->queue_family_index) {
      case TU_QUEUE_GENERAL:
         tu6_init_hw(cmd_buffer, &cmd_buffer->cs);
         break;
      default:
         break;
      }
   } else if (cmd_buffer->vk.level == VK_COMMAND_BUFFER_LEVEL_SECONDARY) {
      const bool pass_continue =
         pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;

      trace_start_cmd_buffer(&cmd_buffer->trace,
            pass_continue ? &cmd_buffer->draw_cs : &cmd_buffer->cs);

      assert(pBeginInfo->pInheritanceInfo);

      cmd_buffer->inherited_pipeline_statistics =
         pBeginInfo->pInheritanceInfo->pipelineStatistics;

      vk_foreach_struct_const(ext, pBeginInfo->pInheritanceInfo) {
         switch (ext->sType) {
         case VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_CONDITIONAL_RENDERING_INFO_EXT: {
            const VkCommandBufferInheritanceConditionalRenderingInfoEXT *cond_rend = (void *) ext;
            cmd_buffer->state.predication_active = cond_rend->conditionalRenderingEnable;
            break;
         default:
            break;
         }
         }
      }

      if (pass_continue) {
         const VkCommandBufferInheritanceRenderingInfo *rendering_info =
            vk_find_struct_const(pBeginInfo->pInheritanceInfo->pNext,
                                 COMMAND_BUFFER_INHERITANCE_RENDERING_INFO);

         if (unlikely(cmd_buffer->device->instance->debug_flags & TU_DEBUG_DYNAMIC)) {
            rendering_info =
               vk_get_command_buffer_inheritance_rendering_info(cmd_buffer->vk.level,
                                                                pBeginInfo);
         }

         if (rendering_info) {
            tu_setup_dynamic_inheritance(cmd_buffer, rendering_info);
            cmd_buffer->state.pass = &cmd_buffer->dynamic_pass;
            cmd_buffer->state.subpass = &cmd_buffer->dynamic_subpass;
         } else {
            cmd_buffer->state.pass = tu_render_pass_from_handle(pBeginInfo->pInheritanceInfo->renderPass);
            cmd_buffer->state.subpass =
               &cmd_buffer->state.pass->subpasses[pBeginInfo->pInheritanceInfo->subpass];
         }

         /* We can't set the gmem layout here, because the state.pass only has
          * to be compatible (same formats/sample counts) with the primary's
          * renderpass, rather than exactly equal.
          */

         tu_lrz_begin_secondary_cmdbuf(cmd_buffer);
      } else {
         /* When executing in the middle of another command buffer, the CCU
          * state is unknown.
          */
         cmd_buffer->state.ccu_state = TU_CMD_CCU_UNKNOWN;
      }
   }

   return VK_SUCCESS;
}

static void
tu6_emit_vertex_strides(struct tu_cmd_buffer *cmd, unsigned num_vbs)
{
   struct tu_cs cs;
   cmd->state.dynamic_state[TU_DYNAMIC_STATE_VB_STRIDE].iova =
      tu_cs_draw_state(&cmd->sub_cs, &cs, 2 * num_vbs).iova;

   for (uint32_t i = 0; i < num_vbs; i++)
      tu_cs_emit_regs(&cs, A6XX_VFD_FETCH_STRIDE(i, cmd->state.vb[i].stride));

   cmd->state.dirty |= TU_CMD_DIRTY_VB_STRIDE;
}

static struct tu_cs
tu_cmd_dynamic_state(struct tu_cmd_buffer *cmd, uint32_t id, uint32_t size)
{
   struct tu_cs cs;

   assert(id < ARRAY_SIZE(cmd->state.dynamic_state));
   cmd->state.dynamic_state[id] = tu_cs_draw_state(&cmd->sub_cs, &cs, size);

   /* note: this also avoids emitting draw states before renderpass clears,
    * which may use the 3D clear path (for MSAA cases)
    */
   if (cmd->state.dirty & TU_CMD_DIRTY_DRAW_STATE)
      return cs;

   tu_cs_emit_pkt7(&cmd->draw_cs, CP_SET_DRAW_STATE, 3);
   tu_cs_emit_draw_state(&cmd->draw_cs, TU_DRAW_STATE_DYNAMIC + id, cmd->state.dynamic_state[id]);

   return cs;
}

static void
tu_cmd_end_dynamic_state(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                         uint32_t id)
{
   assert(id < ARRAY_SIZE(cmd->state.dynamic_state));
   cmd->state.dynamic_state[id] = tu_cs_end_draw_state(&cmd->sub_cs, cs);

   /* note: this also avoids emitting draw states before renderpass clears,
    * which may use the 3D clear path (for MSAA cases)
    */
   if (cmd->state.dirty & TU_CMD_DIRTY_DRAW_STATE)
      return;

   tu_cs_emit_pkt7(&cmd->draw_cs, CP_SET_DRAW_STATE, 3);
   tu_cs_emit_draw_state(&cmd->draw_cs, TU_DRAW_STATE_DYNAMIC + id, cmd->state.dynamic_state[id]);
}

static void
tu_update_num_vbs(struct tu_cmd_buffer *cmd, unsigned num_vbs)
{
   /* the vertex_buffers draw state always contains all the currently
    * bound vertex buffers. update its size to only emit the vbs which
    * are actually used by the pipeline
    * note there is a HW optimization which makes it so the draw state
    * is not re-executed completely when only the size changes
    */
   if (cmd->state.vertex_buffers.size != num_vbs * 4) {
      cmd->state.vertex_buffers.size = num_vbs * 4;
      cmd->state.dirty |= TU_CMD_DIRTY_VERTEX_BUFFERS;
   }

   if (cmd->state.dynamic_state[TU_DYNAMIC_STATE_VB_STRIDE].size != num_vbs * 2) {
      cmd->state.dynamic_state[TU_DYNAMIC_STATE_VB_STRIDE].size = num_vbs * 2;
      cmd->state.dirty |= TU_CMD_DIRTY_VB_STRIDE;
   }
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetVertexInputEXT(VkCommandBuffer commandBuffer,
                        uint32_t vertexBindingDescriptionCount,
                        const VkVertexInputBindingDescription2EXT *pVertexBindingDescriptions,
                        uint32_t vertexAttributeDescriptionCount,
                        const VkVertexInputAttributeDescription2EXT *pVertexAttributeDescriptions)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs;

   unsigned num_vbs = 0;
   for (unsigned i = 0; i < vertexBindingDescriptionCount; i++) {
      const VkVertexInputBindingDescription2EXT *binding =
         &pVertexBindingDescriptions[i];
      num_vbs = MAX2(num_vbs, binding->binding + 1);
      cmd->state.vb[binding->binding].stride = binding->stride;
   }

   tu6_emit_vertex_strides(cmd, num_vbs);
   tu_update_num_vbs(cmd, num_vbs);

   tu_cs_begin_sub_stream(&cmd->sub_cs, TU6_EMIT_VERTEX_INPUT_MAX_DWORDS, &cs);
   tu6_emit_vertex_input(&cs, vertexBindingDescriptionCount,
                         pVertexBindingDescriptions,
                         vertexAttributeDescriptionCount,
                         pVertexAttributeDescriptions);
   tu_cmd_end_dynamic_state(cmd, &cs, TU_DYNAMIC_STATE_VERTEX_INPUT);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdBindVertexBuffers2EXT(VkCommandBuffer commandBuffer,
                            uint32_t firstBinding,
                            uint32_t bindingCount,
                            const VkBuffer* pBuffers,
                            const VkDeviceSize* pOffsets,
                            const VkDeviceSize* pSizes,
                            const VkDeviceSize* pStrides)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs;

   cmd->state.max_vbs_bound = MAX2(
      cmd->state.max_vbs_bound, firstBinding + bindingCount);

   cmd->state.vertex_buffers.iova =
      tu_cs_draw_state(&cmd->sub_cs, &cs, 4 * cmd->state.max_vbs_bound).iova;

   for (uint32_t i = 0; i < bindingCount; i++) {
      if (pBuffers[i] == VK_NULL_HANDLE) {
         cmd->state.vb[firstBinding + i].base = 0;
         cmd->state.vb[firstBinding + i].size = 0;
      } else {
         struct tu_buffer *buf = tu_buffer_from_handle(pBuffers[i]);
         cmd->state.vb[firstBinding + i].base = buf->iova + pOffsets[i];
         cmd->state.vb[firstBinding + i].size = pSizes ? pSizes[i] : (buf->vk.size - pOffsets[i]);
      }

      if (pStrides)
         cmd->state.vb[firstBinding + i].stride = pStrides[i];
   }

   for (uint32_t i = 0; i < cmd->state.max_vbs_bound; i++) {
      tu_cs_emit_regs(&cs,
                      A6XX_VFD_FETCH_BASE(i, .qword = cmd->state.vb[i].base),
                      A6XX_VFD_FETCH_SIZE(i, cmd->state.vb[i].size));
   }

   cmd->state.dirty |= TU_CMD_DIRTY_VERTEX_BUFFERS;

   if (pStrides)
      tu6_emit_vertex_strides(cmd, cmd->state.max_vbs_bound);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdBindIndexBuffer(VkCommandBuffer commandBuffer,
                      VkBuffer buffer,
                      VkDeviceSize offset,
                      VkIndexType indexType)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, buf, buffer);



   uint32_t index_size, index_shift, restart_index;

   switch (indexType) {
   case VK_INDEX_TYPE_UINT16:
      index_size = INDEX4_SIZE_16_BIT;
      index_shift = 1;
      restart_index = 0xffff;
      break;
   case VK_INDEX_TYPE_UINT32:
      index_size = INDEX4_SIZE_32_BIT;
      index_shift = 2;
      restart_index = 0xffffffff;
      break;
   case VK_INDEX_TYPE_UINT8_EXT:
      index_size = INDEX4_SIZE_8_BIT;
      index_shift = 0;
      restart_index = 0xff;
      break;
   default:
      unreachable("invalid VkIndexType");
   }

   /* initialize/update the restart index */
   if (cmd->state.index_size != index_size)
      tu_cs_emit_regs(&cmd->draw_cs, A6XX_PC_RESTART_INDEX(restart_index));

   assert(buf->vk.size >= offset);

   cmd->state.index_va = buf->iova + offset;
   cmd->state.max_index_count = (buf->vk.size - offset) >> index_shift;
   cmd->state.index_size = index_size;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdBindDescriptorSets(VkCommandBuffer commandBuffer,
                         VkPipelineBindPoint pipelineBindPoint,
                         VkPipelineLayout _layout,
                         uint32_t firstSet,
                         uint32_t descriptorSetCount,
                         const VkDescriptorSet *pDescriptorSets,
                         uint32_t dynamicOffsetCount,
                         const uint32_t *pDynamicOffsets)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_pipeline_layout, layout, _layout);
   unsigned dyn_idx = 0;

   struct tu_descriptor_state *descriptors_state =
      tu_get_descriptors_state(cmd, pipelineBindPoint);

   descriptors_state->max_sets_bound =
      MAX2(descriptors_state->max_sets_bound, firstSet + descriptorSetCount);

   for (unsigned i = 0; i < descriptorSetCount; ++i) {
      unsigned idx = i + firstSet;
      TU_FROM_HANDLE(tu_descriptor_set, set, pDescriptorSets[i]);

      descriptors_state->sets[idx] = set;

      if (!set->layout->dynamic_offset_size)
         continue;

      uint32_t *src = set->dynamic_descriptors;
      uint32_t *dst = descriptors_state->dynamic_descriptors +
         layout->set[idx].dynamic_offset_start / 4;
      for (unsigned j = 0; j < set->layout->binding_count; j++) {
         struct tu_descriptor_set_binding_layout *binding =
            &set->layout->binding[j];
         if (binding->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC ||
             binding->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC) {
            for (unsigned k = 0; k < binding->array_size; k++, dyn_idx++) {
               assert(dyn_idx < dynamicOffsetCount);
               uint32_t offset = pDynamicOffsets[dyn_idx];
               memcpy(dst, src, binding->size);

               if (binding->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
                  /* Note: we can assume here that the addition won't roll
                   * over and change the SIZE field.
                   */
                  uint64_t va = src[0] | ((uint64_t)src[1] << 32);
                  va += offset;
                  dst[0] = va;
                  dst[1] = va >> 32;
               } else {
                  uint32_t *dst_desc = dst;
                  for (unsigned i = 0;
                       i < binding->size / (4 * A6XX_TEX_CONST_DWORDS);
                       i++, dst_desc += A6XX_TEX_CONST_DWORDS) {
                     /* Note: A6XX_TEX_CONST_5_DEPTH is always 0 */
                     uint64_t va = dst_desc[4] | ((uint64_t)dst_desc[5] << 32);
                     va += offset;
                     dst_desc[4] = va;
                     dst_desc[5] = va >> 32;
                  }
               }

               dst += binding->size / 4;
               src += binding->size / 4;
            }
         }
      }
   }
   assert(dyn_idx == dynamicOffsetCount);

   uint32_t sp_bindless_base_reg, hlsq_bindless_base_reg, hlsq_invalidate_value;
   uint64_t addr[MAX_SETS] = {};
   uint64_t dynamic_addr = 0;
   struct tu_cs *cs, state_cs;

   for (uint32_t i = 0; i < descriptors_state->max_sets_bound; i++) {
      struct tu_descriptor_set *set = descriptors_state->sets[i];
      if (set)
         addr[i] = set->va | 3;
   }

   if (layout->dynamic_offset_size) {
      /* allocate and fill out dynamic descriptor set */
      struct tu_cs_memory dynamic_desc_set;
      VkResult result = tu_cs_alloc(&cmd->sub_cs,
                                    layout->dynamic_offset_size / (4 * A6XX_TEX_CONST_DWORDS),
                                    A6XX_TEX_CONST_DWORDS, &dynamic_desc_set);
      if (result != VK_SUCCESS) {
         vk_command_buffer_set_error(&cmd->vk, result);
         return;
      }

      memcpy(dynamic_desc_set.map, descriptors_state->dynamic_descriptors,
             layout->dynamic_offset_size);
      dynamic_addr = dynamic_desc_set.iova | 3;
      descriptors_state->dynamic_bound = true;
   }

   if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      sp_bindless_base_reg = REG_A6XX_SP_BINDLESS_BASE(0);
      hlsq_bindless_base_reg = REG_A6XX_HLSQ_BINDLESS_BASE(0);
      hlsq_invalidate_value = A6XX_HLSQ_INVALIDATE_CMD_GFX_BINDLESS(0x1f);

      cmd->state.desc_sets =
         tu_cs_draw_state(&cmd->sub_cs, &state_cs,
                          4 + 4 * descriptors_state->max_sets_bound +
                             (descriptors_state->dynamic_bound ? 6 : 0));
      cmd->state.dirty |= TU_CMD_DIRTY_DESC_SETS_LOAD;
      cs = &state_cs;
   } else {
      assert(pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE);

      sp_bindless_base_reg = REG_A6XX_SP_CS_BINDLESS_BASE(0);
      hlsq_bindless_base_reg = REG_A6XX_HLSQ_CS_BINDLESS_BASE(0);
      hlsq_invalidate_value = A6XX_HLSQ_INVALIDATE_CMD_CS_BINDLESS(0x1f);

      cmd->state.dirty |= TU_CMD_DIRTY_COMPUTE_DESC_SETS_LOAD;
      cs = &cmd->cs;
   }

   tu_cs_emit_pkt4(cs, sp_bindless_base_reg, 2 * descriptors_state->max_sets_bound);
   tu_cs_emit_array(cs, (const uint32_t*) addr, 2 * descriptors_state->max_sets_bound);
   tu_cs_emit_pkt4(cs, hlsq_bindless_base_reg, 2 * descriptors_state->max_sets_bound);
   tu_cs_emit_array(cs, (const uint32_t*) addr, 2 * descriptors_state->max_sets_bound);

   /* Dynamic descriptors get the last descriptor set. */
   if (descriptors_state->dynamic_bound) {
      tu_cs_emit_pkt4(cs, sp_bindless_base_reg + 4 * 2, 2);
      tu_cs_emit_qw(cs, dynamic_addr);
      tu_cs_emit_pkt4(cs, hlsq_bindless_base_reg + 4 * 2, 2);
      tu_cs_emit_qw(cs, dynamic_addr);
   }

   tu_cs_emit_regs(cs, A6XX_HLSQ_INVALIDATE_CMD(.dword = hlsq_invalidate_value));

   if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      assert(cs->cur == cs->end); /* validate draw state size */
      /* note: this also avoids emitting draw states before renderpass clears,
       * which may use the 3D clear path (for MSAA cases)
       */
      if (!(cmd->state.dirty & TU_CMD_DIRTY_DRAW_STATE)) {
         tu_cs_emit_pkt7(&cmd->draw_cs, CP_SET_DRAW_STATE, 3);
         tu_cs_emit_draw_state(&cmd->draw_cs, TU_DRAW_STATE_DESC_SETS, cmd->state.desc_sets);
      }
   }
}

static enum VkResult
tu_push_descriptor_set_update_layout(struct tu_device *device,
                                     struct tu_descriptor_set *set,
                                     struct tu_descriptor_set_layout *layout)
{
   if (set->layout == layout)
      return VK_SUCCESS;

   if (set->layout)
      vk_descriptor_set_layout_unref(&device->vk, &set->layout->vk);
   vk_descriptor_set_layout_ref(&layout->vk);
   set->layout = layout;

   if (set->host_size < layout->size) {
      void *new_buf =
         vk_realloc(&device->vk.alloc, set->mapped_ptr, layout->size, 8,
                    VK_QUERY_SCOPE_COMMAND_BUFFER_KHR);
      if (!new_buf)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      set->mapped_ptr = new_buf;
      set->host_size = layout->size;
   }
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer,
                           VkPipelineBindPoint pipelineBindPoint,
                           VkPipelineLayout _layout,
                           uint32_t _set,
                           uint32_t descriptorWriteCount,
                           const VkWriteDescriptorSet *pDescriptorWrites)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_pipeline_layout, pipe_layout, _layout);
   struct tu_descriptor_set_layout *layout = pipe_layout->set[_set].layout;
   struct tu_descriptor_set *set =
      &tu_get_descriptors_state(cmd, pipelineBindPoint)->push_set;

   struct tu_cs_memory set_mem;
   VkResult result = tu_cs_alloc(&cmd->sub_cs,
                                 DIV_ROUND_UP(layout->size, A6XX_TEX_CONST_DWORDS * 4),
                                 A6XX_TEX_CONST_DWORDS, &set_mem);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   result = tu_push_descriptor_set_update_layout(cmd->device, set, layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   tu_update_descriptor_sets(cmd->device, tu_descriptor_set_to_handle(set),
                             descriptorWriteCount, pDescriptorWrites, 0, NULL);

   memcpy(set_mem.map, set->mapped_ptr, layout->size);
   set->va = set_mem.iova;

   tu_CmdBindDescriptorSets(commandBuffer, pipelineBindPoint, _layout, _set,
                            1, (VkDescriptorSet[]) { tu_descriptor_set_to_handle(set) },
                            0, NULL);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer commandBuffer,
                                       VkDescriptorUpdateTemplate descriptorUpdateTemplate,
                                       VkPipelineLayout _layout,
                                       uint32_t _set,
                                       const void* pData)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_pipeline_layout, pipe_layout, _layout);
   TU_FROM_HANDLE(tu_descriptor_update_template, templ, descriptorUpdateTemplate);
   struct tu_descriptor_set_layout *layout = pipe_layout->set[_set].layout;
   struct tu_descriptor_set *set =
      &tu_get_descriptors_state(cmd, templ->bind_point)->push_set;

   struct tu_cs_memory set_mem;
   VkResult result = tu_cs_alloc(&cmd->sub_cs,
                                 DIV_ROUND_UP(layout->size, A6XX_TEX_CONST_DWORDS * 4),
                                 A6XX_TEX_CONST_DWORDS, &set_mem);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   result = tu_push_descriptor_set_update_layout(cmd->device, set, layout);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   tu_update_descriptor_set_with_template(cmd->device, set, descriptorUpdateTemplate, pData);

   memcpy(set_mem.map, set->mapped_ptr, layout->size);
   set->va = set_mem.iova;

   tu_CmdBindDescriptorSets(commandBuffer, templ->bind_point, _layout, _set,
                            1, (VkDescriptorSet[]) { tu_descriptor_set_to_handle(set) },
                            0, NULL);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdBindTransformFeedbackBuffersEXT(VkCommandBuffer commandBuffer,
                                      uint32_t firstBinding,
                                      uint32_t bindingCount,
                                      const VkBuffer *pBuffers,
                                      const VkDeviceSize *pOffsets,
                                      const VkDeviceSize *pSizes)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   /* using COND_REG_EXEC for xfb commands matches the blob behavior
    * presumably there isn't any benefit using a draw state when the
    * condition is (SYSMEM | BINNING)
    */
   tu_cond_exec_start(cs, CP_COND_REG_EXEC_0_MODE(RENDER_MODE) |
                          CP_COND_REG_EXEC_0_SYSMEM |
                          CP_COND_REG_EXEC_0_BINNING);

   for (uint32_t i = 0; i < bindingCount; i++) {
      TU_FROM_HANDLE(tu_buffer, buf, pBuffers[i]);
      uint64_t iova = buf->iova + pOffsets[i];
      uint32_t size = buf->bo->size - (iova - buf->bo->iova);
      uint32_t idx = i + firstBinding;

      if (pSizes && pSizes[i] != VK_WHOLE_SIZE)
         size = pSizes[i];

      /* BUFFER_BASE is 32-byte aligned, add remaining offset to BUFFER_OFFSET */
      uint32_t offset = iova & 0x1f;
      iova &= ~(uint64_t) 0x1f;

      tu_cs_emit_pkt4(cs, REG_A6XX_VPC_SO_BUFFER_BASE(idx), 3);
      tu_cs_emit_qw(cs, iova);
      tu_cs_emit(cs, size + offset);

      cmd->state.streamout_offset[idx] = offset;
   }

   tu_cond_exec_end(cs);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdBeginTransformFeedbackEXT(VkCommandBuffer commandBuffer,
                                uint32_t firstCounterBuffer,
                                uint32_t counterBufferCount,
                                const VkBuffer *pCounterBuffers,
                                const VkDeviceSize *pCounterBufferOffsets)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   tu_cond_exec_start(cs, CP_COND_REG_EXEC_0_MODE(RENDER_MODE) |
                          CP_COND_REG_EXEC_0_SYSMEM |
                          CP_COND_REG_EXEC_0_BINNING);

   tu_cs_emit_regs(cs, A6XX_VPC_SO_DISABLE(false));

   /* TODO: only update offset for active buffers */
   for (uint32_t i = 0; i < IR3_MAX_SO_BUFFERS; i++)
      tu_cs_emit_regs(cs, A6XX_VPC_SO_BUFFER_OFFSET(i, cmd->state.streamout_offset[i]));

   for (uint32_t i = 0; i < (pCounterBuffers ? counterBufferCount : 0); i++) {
      uint32_t idx = firstCounterBuffer + i;
      uint32_t offset = cmd->state.streamout_offset[idx];
      uint64_t counter_buffer_offset = pCounterBufferOffsets ? pCounterBufferOffsets[i] : 0u;

      if (!pCounterBuffers[i])
         continue;

      TU_FROM_HANDLE(tu_buffer, buf, pCounterBuffers[i]);

      tu_cs_emit_pkt7(cs, CP_MEM_TO_REG, 3);
      tu_cs_emit(cs, CP_MEM_TO_REG_0_REG(REG_A6XX_VPC_SO_BUFFER_OFFSET(idx)) |
                     CP_MEM_TO_REG_0_UNK31 |
                     CP_MEM_TO_REG_0_CNT(1));
      tu_cs_emit_qw(cs, buf->iova + counter_buffer_offset);

      if (offset) {
         tu_cs_emit_pkt7(cs, CP_REG_RMW, 3);
         tu_cs_emit(cs, CP_REG_RMW_0_DST_REG(REG_A6XX_VPC_SO_BUFFER_OFFSET(idx)) |
                        CP_REG_RMW_0_SRC1_ADD);
         tu_cs_emit(cs, 0xffffffff);
         tu_cs_emit(cs, offset);
      }
   }

   tu_cond_exec_end(cs);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdEndTransformFeedbackEXT(VkCommandBuffer commandBuffer,
                              uint32_t firstCounterBuffer,
                              uint32_t counterBufferCount,
                              const VkBuffer *pCounterBuffers,
                              const VkDeviceSize *pCounterBufferOffsets)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   tu_cond_exec_start(cs, CP_COND_REG_EXEC_0_MODE(RENDER_MODE) |
                          CP_COND_REG_EXEC_0_SYSMEM |
                          CP_COND_REG_EXEC_0_BINNING);

   tu_cs_emit_regs(cs, A6XX_VPC_SO_DISABLE(true));

   /* TODO: only flush buffers that need to be flushed */
   for (uint32_t i = 0; i < IR3_MAX_SO_BUFFERS; i++) {
      /* note: FLUSH_BASE is always the same, so it could go in init_hw()? */
      tu_cs_emit_pkt4(cs, REG_A6XX_VPC_SO_FLUSH_BASE(i), 2);
      tu_cs_emit_qw(cs, global_iova(cmd, flush_base[i]));
      tu6_emit_event_write(cmd, cs, FLUSH_SO_0 + i);
   }

   for (uint32_t i = 0; i < (pCounterBuffers ? counterBufferCount : 0); i++) {
      uint32_t idx = firstCounterBuffer + i;
      uint32_t offset = cmd->state.streamout_offset[idx];
      uint64_t counter_buffer_offset = pCounterBufferOffsets ? pCounterBufferOffsets[i] : 0u;

      if (!pCounterBuffers[i])
         continue;

      TU_FROM_HANDLE(tu_buffer, buf, pCounterBuffers[i]);

      /* VPC_SO_FLUSH_BASE has dwords counter, but counter should be in bytes */
      tu_cs_emit_pkt7(cs, CP_MEM_TO_REG, 3);
      tu_cs_emit(cs, CP_MEM_TO_REG_0_REG(REG_A6XX_CP_SCRATCH_REG(0)) |
                     CP_MEM_TO_REG_0_SHIFT_BY_2 |
                     0x40000 | /* ??? */
                     CP_MEM_TO_REG_0_UNK31 |
                     CP_MEM_TO_REG_0_CNT(1));
      tu_cs_emit_qw(cs, global_iova(cmd, flush_base[idx]));

      if (offset) {
         tu_cs_emit_pkt7(cs, CP_REG_RMW, 3);
         tu_cs_emit(cs, CP_REG_RMW_0_DST_REG(REG_A6XX_CP_SCRATCH_REG(0)) |
                        CP_REG_RMW_0_SRC1_ADD);
         tu_cs_emit(cs, 0xffffffff);
         tu_cs_emit(cs, -offset);
      }

      tu_cs_emit_pkt7(cs, CP_REG_TO_MEM, 3);
      tu_cs_emit(cs, CP_REG_TO_MEM_0_REG(REG_A6XX_CP_SCRATCH_REG(0)) |
                     CP_REG_TO_MEM_0_CNT(1));
      tu_cs_emit_qw(cs, buf->iova + counter_buffer_offset);
   }

   tu_cond_exec_end(cs);

   cmd->state.rp.xfb_used = true;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdPushConstants(VkCommandBuffer commandBuffer,
                    VkPipelineLayout layout,
                    VkShaderStageFlags stageFlags,
                    uint32_t offset,
                    uint32_t size,
                    const void *pValues)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   memcpy((void*) cmd->push_constants + offset, pValues, size);
   cmd->state.dirty |= TU_CMD_DIRTY_SHADER_CONSTS;
}

/* Flush everything which has been made available but we haven't actually
 * flushed yet.
 */
static void
tu_flush_all_pending(struct tu_cache_state *cache)
{
   cache->flush_bits |= cache->pending_flush_bits & TU_CMD_FLAG_ALL_FLUSH;
   cache->pending_flush_bits &= ~TU_CMD_FLAG_ALL_FLUSH;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_EndCommandBuffer(VkCommandBuffer commandBuffer)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);

   /* We currently flush CCU at the end of the command buffer, like
    * what the blob does. There's implicit synchronization around every
    * vkQueueSubmit, but the kernel only flushes the UCHE, and we don't
    * know yet if this command buffer will be the last in the submit so we
    * have to defensively flush everything else.
    *
    * TODO: We could definitely do better than this, since these flushes
    * aren't required by Vulkan, but we'd need kernel support to do that.
    * Ideally, we'd like the kernel to flush everything afterwards, so that we
    * wouldn't have to do any flushes here, and when submitting multiple
    * command buffers there wouldn't be any unnecessary flushes in between.
    */
   if (cmd_buffer->state.pass) {
      tu_flush_all_pending(&cmd_buffer->state.renderpass_cache);
      tu_emit_cache_flush_renderpass(cmd_buffer, &cmd_buffer->draw_cs);

      trace_end_cmd_buffer(&cmd_buffer->trace, &cmd_buffer->draw_cs, cmd_buffer);
   } else {
      tu_flush_all_pending(&cmd_buffer->state.cache);
      cmd_buffer->state.cache.flush_bits |=
         TU_CMD_FLAG_CCU_FLUSH_COLOR |
         TU_CMD_FLAG_CCU_FLUSH_DEPTH;
      tu_emit_cache_flush(cmd_buffer, &cmd_buffer->cs);

      trace_end_cmd_buffer(&cmd_buffer->trace, &cmd_buffer->cs, cmd_buffer);
   }

   tu_cs_end(&cmd_buffer->cs);
   tu_cs_end(&cmd_buffer->draw_cs);
   tu_cs_end(&cmd_buffer->draw_epilogue_cs);

   cmd_buffer->status = TU_CMD_BUFFER_STATUS_EXECUTABLE;

   return vk_command_buffer_get_record_result(&cmd_buffer->vk);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdBindPipeline(VkCommandBuffer commandBuffer,
                   VkPipelineBindPoint pipelineBindPoint,
                   VkPipeline _pipeline)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_pipeline, pipeline, _pipeline);

   if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
      cmd->state.compute_pipeline = pipeline;
      tu_cs_emit_state_ib(&cmd->cs, pipeline->program.state);
      return;
   }

   assert(pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS);

   cmd->state.pipeline = pipeline;
   cmd->state.dirty |= TU_CMD_DIRTY_DESC_SETS_LOAD | TU_CMD_DIRTY_SHADER_CONSTS |
                       TU_CMD_DIRTY_LRZ | TU_CMD_DIRTY_VS_PARAMS;

   if (pipeline->feedback_loop_may_involve_textures) {
      /* VK_EXT_attachment_feedback_loop_layout allows feedback loop to involve
       * not only input attachments but also sampled images or image resources.
       * But we cannot just patch gmem for image in the descriptors.
       *
       * At the moment, in context of DXVK, it is expected that only a few
       * drawcalls in a frame would use feedback loop and they would be wrapped
       * in their own renderpasses, so it should be ok to force sysmem.
       *
       * However, there are two further possible optimizations if need would
       * arise for other translation layer:
       * - Tiling could be enabled if we ensure that there is no barrier in
       *   the renderpass;
       * - Check that both pipeline and attachments agree that feedback loop
       *   is needed.
       */
      cmd->state.rp.disable_gmem = true;
   }
   cmd->state.rp.sysmem_single_prim_mode |= pipeline->sysmem_single_prim_mode;

   struct tu_cs *cs = &cmd->draw_cs;

   /* note: this also avoids emitting draw states before renderpass clears,
    * which may use the 3D clear path (for MSAA cases)
    */
   if (!(cmd->state.dirty & TU_CMD_DIRTY_DRAW_STATE)) {
      uint32_t mask = ~pipeline->dynamic_state_mask & BITFIELD_MASK(TU_DYNAMIC_STATE_COUNT);

      tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3 * (6 + util_bitcount(mask)));
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_PROGRAM_CONFIG, pipeline->program.config_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_PROGRAM, pipeline->program.state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_PROGRAM_BINNING, pipeline->program.binning_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_RAST, pipeline->rast_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_PRIM_MODE_SYSMEM, pipeline->prim_order_state_sysmem);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_PRIM_MODE_GMEM, pipeline->prim_order_state_gmem);

      u_foreach_bit(i, mask)
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DYNAMIC + i, pipeline->dynamic_state[i]);
   }

   if (pipeline->active_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT) {
      cmd->state.rp.has_tess = true;

      /* maximum number of patches that can fit in tess factor/param buffers */
      uint32_t subdraw_size = MIN2(TU_TESS_FACTOR_SIZE / ir3_tess_factor_stride(pipeline->tess.patch_type),
                           TU_TESS_PARAM_SIZE / pipeline->tess.param_stride);
      /* convert from # of patches to draw count */
      subdraw_size *= (pipeline->ia.primtype - DI_PT_PATCHES0);

      /* TODO: Move this packet to pipeline state, since it's constant based on the pipeline. */
      tu_cs_emit_pkt7(cs, CP_SET_SUBDRAW_SIZE, 1);
      tu_cs_emit(cs, subdraw_size);
   }

   if (cmd->state.line_mode != pipeline->line_mode) {
      cmd->state.line_mode = pipeline->line_mode;

      /* We have to disable MSAA when bresenham lines are used, this is
       * a hardware limitation and spec allows it:
       *
       *    When Bresenham lines are being rasterized, sample locations may
       *    all be treated as being at the pixel center (this may affect
       *    attribute and depth interpolation).
       */
      if (cmd->state.subpass && cmd->state.subpass->samples) {
         tu6_emit_msaa(cs, cmd->state.subpass->samples, cmd->state.line_mode);
      }
   }

   if ((pipeline->dynamic_state_mask & BIT(VK_DYNAMIC_STATE_VIEWPORT)) &&
       (pipeline->z_negative_one_to_one != cmd->state.z_negative_one_to_one)) {
      cmd->state.z_negative_one_to_one = pipeline->z_negative_one_to_one;
      cmd->state.dirty |= TU_CMD_DIRTY_VIEWPORTS;
   }

   if (!(pipeline->dynamic_state_mask & BIT(TU_DYNAMIC_STATE_VERTEX_INPUT)))
      tu_update_num_vbs(cmd, pipeline->num_vbs);

#define UPDATE_REG(X, Y) {                                           \
   /* note: would be better to have pipeline bits already masked */  \
   uint32_t pipeline_bits = pipeline->X & pipeline->X##_mask;        \
   if ((cmd->state.X & pipeline->X##_mask) != pipeline_bits) {       \
      cmd->state.X &= ~pipeline->X##_mask;                           \
      cmd->state.X |= pipeline_bits;                                 \
      cmd->state.dirty |= TU_CMD_DIRTY_##Y;                          \
   }                                                                 \
   if (!(pipeline->dynamic_state_mask & BIT(TU_DYNAMIC_STATE_##Y)))  \
      cmd->state.dirty &= ~TU_CMD_DIRTY_##Y;                         \
}

   /* these registers can have bits set from both pipeline and dynamic state
    * this updates the bits set by the pipeline
    * if the pipeline doesn't use a dynamic state for the register, then
    * the relevant dirty bit is cleared to avoid overriding the non-dynamic
    * state with a dynamic state the next draw.
    */
   UPDATE_REG(gras_su_cntl, GRAS_SU_CNTL);
   UPDATE_REG(rb_depth_cntl, RB_DEPTH_CNTL);
   UPDATE_REG(rb_stencil_cntl, RB_STENCIL_CNTL);
   UPDATE_REG(pc_raster_cntl, RASTERIZER_DISCARD);
   UPDATE_REG(vpc_unknown_9107, RASTERIZER_DISCARD);
   UPDATE_REG(sp_blend_cntl, BLEND);
   UPDATE_REG(rb_blend_cntl, BLEND);

   for (unsigned i = 0; i < pipeline->num_rts; i++) {
      if ((cmd->state.rb_mrt_control[i] & pipeline->rb_mrt_control_mask) !=
          pipeline->rb_mrt_control[i]) {
         cmd->state.rb_mrt_control[i] &= ~pipeline->rb_mrt_control_mask;
         cmd->state.rb_mrt_control[i] |= pipeline->rb_mrt_control[i];
         cmd->state.dirty |= TU_CMD_DIRTY_BLEND;
      }

      if (cmd->state.rb_mrt_blend_control[i] != pipeline->rb_mrt_blend_control[i]) {
         cmd->state.rb_mrt_blend_control[i] = pipeline->rb_mrt_blend_control[i];
         cmd->state.dirty |= TU_CMD_DIRTY_BLEND;
      }
   }
#undef UPDATE_REG

   if (cmd->state.pipeline_color_write_enable != pipeline->color_write_enable) {
      cmd->state.pipeline_color_write_enable = pipeline->color_write_enable;
      cmd->state.dirty |= TU_CMD_DIRTY_BLEND;
   }
   if (cmd->state.pipeline_blend_enable != pipeline->blend_enable) {
      cmd->state.pipeline_blend_enable = pipeline->blend_enable;
      cmd->state.dirty |= TU_CMD_DIRTY_BLEND;
   }
   if (cmd->state.logic_op_enabled != pipeline->logic_op_enabled) {
      cmd->state.logic_op_enabled = pipeline->logic_op_enabled;
      cmd->state.dirty |= TU_CMD_DIRTY_BLEND;
   }
   if (!(pipeline->dynamic_state_mask & BIT(TU_DYNAMIC_STATE_LOGIC_OP)) &&
       cmd->state.rop_reads_dst != pipeline->rop_reads_dst) {
      cmd->state.rop_reads_dst = pipeline->rop_reads_dst;
      cmd->state.dirty |= TU_CMD_DIRTY_BLEND;
   }
   if (cmd->state.dynamic_state[TU_DYNAMIC_STATE_BLEND].size != pipeline->num_rts * 3 + 4) {
      cmd->state.dirty |= TU_CMD_DIRTY_BLEND;
   }
   if (!(pipeline->dynamic_state_mask & BIT(TU_DYNAMIC_STATE_BLEND))) {
      cmd->state.dirty &= ~TU_CMD_DIRTY_BLEND;
   }

   if (pipeline->rb_depth_cntl_disable)
      cmd->state.dirty |= TU_CMD_DIRTY_RB_DEPTH_CNTL;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetViewport(VkCommandBuffer commandBuffer,
                  uint32_t firstViewport,
                  uint32_t viewportCount,
                  const VkViewport *pViewports)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   memcpy(&cmd->state.viewport[firstViewport], pViewports, viewportCount * sizeof(*pViewports));
   cmd->state.max_viewport = MAX2(cmd->state.max_viewport, firstViewport + viewportCount);

   /* With VK_EXT_depth_clip_control we have to take into account
    * negativeOneToOne property of the pipeline, so the viewport calculations
    * are deferred until it is known.
    */
   cmd->state.dirty |= TU_CMD_DIRTY_VIEWPORTS;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetScissor(VkCommandBuffer commandBuffer,
                 uint32_t firstScissor,
                 uint32_t scissorCount,
                 const VkRect2D *pScissors)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs;

   memcpy(&cmd->state.scissor[firstScissor], pScissors, scissorCount * sizeof(*pScissors));
   cmd->state.max_scissor = MAX2(cmd->state.max_scissor, firstScissor + scissorCount);

   cs = tu_cmd_dynamic_state(cmd, VK_DYNAMIC_STATE_SCISSOR, 1 + 2 * cmd->state.max_scissor);
   tu6_emit_scissor(&cs, cmd->state.scissor, cmd->state.max_scissor);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.gras_su_cntl &= ~A6XX_GRAS_SU_CNTL_LINEHALFWIDTH__MASK;
   cmd->state.gras_su_cntl |= A6XX_GRAS_SU_CNTL_LINEHALFWIDTH(lineWidth / 2.0f);

   cmd->state.dirty |= TU_CMD_DIRTY_GRAS_SU_CNTL;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetDepthBias(VkCommandBuffer commandBuffer,
                   float depthBiasConstantFactor,
                   float depthBiasClamp,
                   float depthBiasSlopeFactor)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs = tu_cmd_dynamic_state(cmd, VK_DYNAMIC_STATE_DEPTH_BIAS, 4);

   tu6_emit_depth_bias(&cs, depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetBlendConstants(VkCommandBuffer commandBuffer,
                        const float blendConstants[4])
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs = tu_cmd_dynamic_state(cmd, VK_DYNAMIC_STATE_BLEND_CONSTANTS, 5);

   tu_cs_emit_pkt4(&cs, REG_A6XX_RB_BLEND_RED_F32, 4);
   tu_cs_emit_array(&cs, (const uint32_t *) blendConstants, 4);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetDepthBounds(VkCommandBuffer commandBuffer,
                     float minDepthBounds,
                     float maxDepthBounds)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs = tu_cmd_dynamic_state(cmd, VK_DYNAMIC_STATE_DEPTH_BOUNDS, 3);

   tu_cs_emit_regs(&cs,
                   A6XX_RB_Z_BOUNDS_MIN(minDepthBounds),
                   A6XX_RB_Z_BOUNDS_MAX(maxDepthBounds));
}

void
update_stencil_mask(uint32_t *value, VkStencilFaceFlags face, uint32_t mask)
{
   if (face & VK_STENCIL_FACE_FRONT_BIT)
      *value = (*value & 0xff00) | (mask & 0xff);
   if (face & VK_STENCIL_FACE_BACK_BIT)
      *value = (*value & 0xff) | (mask & 0xff) << 8;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer,
                            VkStencilFaceFlags faceMask,
                            uint32_t compareMask)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs = tu_cmd_dynamic_state(cmd, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, 2);

   update_stencil_mask(&cmd->state.dynamic_stencil_mask, faceMask, compareMask);

   tu_cs_emit_regs(&cs, A6XX_RB_STENCILMASK(.dword = cmd->state.dynamic_stencil_mask));
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer,
                          VkStencilFaceFlags faceMask,
                          uint32_t writeMask)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs = tu_cmd_dynamic_state(cmd, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, 2);

   update_stencil_mask(&cmd->state.dynamic_stencil_wrmask, faceMask, writeMask);

   tu_cs_emit_regs(&cs, A6XX_RB_STENCILWRMASK(.dword = cmd->state.dynamic_stencil_wrmask));

   cmd->state.dirty |= TU_CMD_DIRTY_LRZ;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetStencilReference(VkCommandBuffer commandBuffer,
                          VkStencilFaceFlags faceMask,
                          uint32_t reference)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs = tu_cmd_dynamic_state(cmd, VK_DYNAMIC_STATE_STENCIL_REFERENCE, 2);

   update_stencil_mask(&cmd->state.dynamic_stencil_ref, faceMask, reference);

   tu_cs_emit_regs(&cs, A6XX_RB_STENCILREF(.dword = cmd->state.dynamic_stencil_ref));
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetSampleLocationsEXT(VkCommandBuffer commandBuffer,
                            const VkSampleLocationsInfoEXT* pSampleLocationsInfo)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs = tu_cmd_dynamic_state(cmd, TU_DYNAMIC_STATE_SAMPLE_LOCATIONS, 9);

   assert(pSampleLocationsInfo);

   tu6_emit_sample_locations(&cs, pSampleLocationsInfo);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetCullModeEXT(VkCommandBuffer commandBuffer, VkCullModeFlags cullMode)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.gras_su_cntl &=
      ~(A6XX_GRAS_SU_CNTL_CULL_FRONT | A6XX_GRAS_SU_CNTL_CULL_BACK);

   if (cullMode & VK_CULL_MODE_FRONT_BIT)
      cmd->state.gras_su_cntl |= A6XX_GRAS_SU_CNTL_CULL_FRONT;
   if (cullMode & VK_CULL_MODE_BACK_BIT)
      cmd->state.gras_su_cntl |= A6XX_GRAS_SU_CNTL_CULL_BACK;

   cmd->state.dirty |= TU_CMD_DIRTY_GRAS_SU_CNTL;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetFrontFaceEXT(VkCommandBuffer commandBuffer, VkFrontFace frontFace)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.gras_su_cntl &= ~A6XX_GRAS_SU_CNTL_FRONT_CW;

   if (frontFace == VK_FRONT_FACE_CLOCKWISE)
      cmd->state.gras_su_cntl |= A6XX_GRAS_SU_CNTL_FRONT_CW;

   cmd->state.dirty |= TU_CMD_DIRTY_GRAS_SU_CNTL;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetPrimitiveTopologyEXT(VkCommandBuffer commandBuffer,
                              VkPrimitiveTopology primitiveTopology)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.primtype = tu6_primtype(primitiveTopology);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetViewportWithCountEXT(VkCommandBuffer commandBuffer,
                              uint32_t viewportCount,
                              const VkViewport* pViewports)
{
   tu_CmdSetViewport(commandBuffer, 0, viewportCount, pViewports);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetScissorWithCountEXT(VkCommandBuffer commandBuffer,
                             uint32_t scissorCount,
                             const VkRect2D* pScissors)
{
   tu_CmdSetScissor(commandBuffer, 0, scissorCount, pScissors);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetDepthTestEnableEXT(VkCommandBuffer commandBuffer,
                            VkBool32 depthTestEnable)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.rb_depth_cntl &= ~A6XX_RB_DEPTH_CNTL_Z_TEST_ENABLE;

   if (depthTestEnable)
      cmd->state.rb_depth_cntl |= A6XX_RB_DEPTH_CNTL_Z_TEST_ENABLE;

   cmd->state.dirty |= TU_CMD_DIRTY_RB_DEPTH_CNTL;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetDepthWriteEnableEXT(VkCommandBuffer commandBuffer,
                             VkBool32 depthWriteEnable)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.rb_depth_cntl &= ~A6XX_RB_DEPTH_CNTL_Z_WRITE_ENABLE;

   if (depthWriteEnable)
      cmd->state.rb_depth_cntl |= A6XX_RB_DEPTH_CNTL_Z_WRITE_ENABLE;

   cmd->state.dirty |= TU_CMD_DIRTY_RB_DEPTH_CNTL;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetDepthCompareOpEXT(VkCommandBuffer commandBuffer,
                           VkCompareOp depthCompareOp)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.rb_depth_cntl &= ~A6XX_RB_DEPTH_CNTL_ZFUNC__MASK;

   cmd->state.rb_depth_cntl |=
      A6XX_RB_DEPTH_CNTL_ZFUNC(tu6_compare_func(depthCompareOp));

   cmd->state.dirty |= TU_CMD_DIRTY_RB_DEPTH_CNTL;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetDepthBoundsTestEnableEXT(VkCommandBuffer commandBuffer,
                                  VkBool32 depthBoundsTestEnable)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.rb_depth_cntl &= ~A6XX_RB_DEPTH_CNTL_Z_BOUNDS_ENABLE;

   if (depthBoundsTestEnable)
      cmd->state.rb_depth_cntl |= A6XX_RB_DEPTH_CNTL_Z_BOUNDS_ENABLE;

   cmd->state.dirty |= TU_CMD_DIRTY_RB_DEPTH_CNTL;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetStencilTestEnableEXT(VkCommandBuffer commandBuffer,
                              VkBool32 stencilTestEnable)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.rb_stencil_cntl &= ~(
      A6XX_RB_STENCIL_CONTROL_STENCIL_ENABLE |
      A6XX_RB_STENCIL_CONTROL_STENCIL_ENABLE_BF |
      A6XX_RB_STENCIL_CONTROL_STENCIL_READ);

   if (stencilTestEnable) {
      cmd->state.rb_stencil_cntl |=
         A6XX_RB_STENCIL_CONTROL_STENCIL_ENABLE |
         A6XX_RB_STENCIL_CONTROL_STENCIL_ENABLE_BF |
         A6XX_RB_STENCIL_CONTROL_STENCIL_READ;
   }

   cmd->state.dirty |= TU_CMD_DIRTY_RB_STENCIL_CNTL;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetStencilOpEXT(VkCommandBuffer commandBuffer,
                      VkStencilFaceFlags faceMask,
                      VkStencilOp failOp,
                      VkStencilOp passOp,
                      VkStencilOp depthFailOp,
                      VkCompareOp compareOp)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT) {
      cmd->state.rb_stencil_cntl &= ~(
         A6XX_RB_STENCIL_CONTROL_FUNC__MASK |
         A6XX_RB_STENCIL_CONTROL_FAIL__MASK |
         A6XX_RB_STENCIL_CONTROL_ZPASS__MASK |
         A6XX_RB_STENCIL_CONTROL_ZFAIL__MASK);

      cmd->state.rb_stencil_cntl |=
         A6XX_RB_STENCIL_CONTROL_FUNC(tu6_compare_func(compareOp)) |
         A6XX_RB_STENCIL_CONTROL_FAIL(tu6_stencil_op(failOp)) |
         A6XX_RB_STENCIL_CONTROL_ZPASS(tu6_stencil_op(passOp)) |
         A6XX_RB_STENCIL_CONTROL_ZFAIL(tu6_stencil_op(depthFailOp));
   }

   if (faceMask & VK_STENCIL_FACE_BACK_BIT) {
      cmd->state.rb_stencil_cntl &= ~(
         A6XX_RB_STENCIL_CONTROL_FUNC_BF__MASK |
         A6XX_RB_STENCIL_CONTROL_FAIL_BF__MASK |
         A6XX_RB_STENCIL_CONTROL_ZPASS_BF__MASK |
         A6XX_RB_STENCIL_CONTROL_ZFAIL_BF__MASK);

      cmd->state.rb_stencil_cntl |=
         A6XX_RB_STENCIL_CONTROL_FUNC_BF(tu6_compare_func(compareOp)) |
         A6XX_RB_STENCIL_CONTROL_FAIL_BF(tu6_stencil_op(failOp)) |
         A6XX_RB_STENCIL_CONTROL_ZPASS_BF(tu6_stencil_op(passOp)) |
         A6XX_RB_STENCIL_CONTROL_ZFAIL_BF(tu6_stencil_op(depthFailOp));
   }

   cmd->state.dirty |= TU_CMD_DIRTY_RB_STENCIL_CNTL;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetDepthBiasEnableEXT(VkCommandBuffer commandBuffer,
                            VkBool32 depthBiasEnable)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.gras_su_cntl &= ~A6XX_GRAS_SU_CNTL_POLY_OFFSET;
   if (depthBiasEnable)
      cmd->state.gras_su_cntl |= A6XX_GRAS_SU_CNTL_POLY_OFFSET;

   cmd->state.dirty |= TU_CMD_DIRTY_GRAS_SU_CNTL;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetPrimitiveRestartEnableEXT(VkCommandBuffer commandBuffer,
                                   VkBool32 primitiveRestartEnable)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.primitive_restart_enable = primitiveRestartEnable;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetRasterizerDiscardEnableEXT(VkCommandBuffer commandBuffer,
                                    VkBool32 rasterizerDiscardEnable)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.pc_raster_cntl &= ~A6XX_PC_RASTER_CNTL_DISCARD;
   cmd->state.vpc_unknown_9107 &= ~A6XX_VPC_UNKNOWN_9107_RASTER_DISCARD;
   if (rasterizerDiscardEnable) {
      cmd->state.pc_raster_cntl |= A6XX_PC_RASTER_CNTL_DISCARD;
      cmd->state.vpc_unknown_9107 |= A6XX_VPC_UNKNOWN_9107_RASTER_DISCARD;
   }

   cmd->state.dirty |= TU_CMD_DIRTY_RASTERIZER_DISCARD;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetLogicOpEXT(VkCommandBuffer commandBuffer,
                    VkLogicOp logicOp)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.rb_mrt_control_rop =
      tu6_rb_mrt_control_rop(logicOp, &cmd->state.rop_reads_dst);

   cmd->state.dirty |= TU_CMD_DIRTY_BLEND;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetPatchControlPointsEXT(VkCommandBuffer commandBuffer,
                               uint32_t patchControlPoints)
{
   tu_stub();
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetLineStippleEXT(VkCommandBuffer commandBuffer,
                        uint32_t lineStippleFactor,
                        uint16_t lineStipplePattern)
{
   tu_stub();
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetColorWriteEnableEXT(VkCommandBuffer commandBuffer, uint32_t attachmentCount,
                             const VkBool32 *pColorWriteEnables)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   uint32_t color_write_enable = 0;

   for (unsigned i = 0; i < attachmentCount; i++) {
      if (pColorWriteEnables[i])
         color_write_enable |= BIT(i);
   }

   cmd->state.color_write_enable = color_write_enable;
   cmd->state.dirty |= TU_CMD_DIRTY_BLEND;
}

static void
tu_flush_for_access(struct tu_cache_state *cache,
                    enum tu_cmd_access_mask src_mask,
                    enum tu_cmd_access_mask dst_mask)
{
   enum tu_cmd_flush_bits flush_bits = 0;

   if (src_mask & TU_ACCESS_SYSMEM_WRITE) {
      cache->pending_flush_bits |= TU_CMD_FLAG_ALL_INVALIDATE;
   }

   if (src_mask & TU_ACCESS_CP_WRITE) {
      /* Flush the CP write queue.
       */
      cache->pending_flush_bits |=
         TU_CMD_FLAG_WAIT_MEM_WRITES |
         TU_CMD_FLAG_ALL_INVALIDATE;
   }

#define SRC_FLUSH(domain, flush, invalidate) \
   if (src_mask & TU_ACCESS_##domain##_WRITE) {                      \
      cache->pending_flush_bits |= TU_CMD_FLAG_##flush |             \
         (TU_CMD_FLAG_ALL_INVALIDATE & ~TU_CMD_FLAG_##invalidate);   \
   }

   SRC_FLUSH(UCHE, CACHE_FLUSH, CACHE_INVALIDATE)
   SRC_FLUSH(CCU_COLOR, CCU_FLUSH_COLOR, CCU_INVALIDATE_COLOR)
   SRC_FLUSH(CCU_DEPTH, CCU_FLUSH_DEPTH, CCU_INVALIDATE_DEPTH)

#undef SRC_FLUSH

#define SRC_INCOHERENT_FLUSH(domain, flush, invalidate)              \
   if (src_mask & TU_ACCESS_##domain##_INCOHERENT_WRITE) {           \
      flush_bits |= TU_CMD_FLAG_##flush;                             \
      cache->pending_flush_bits |=                                   \
         (TU_CMD_FLAG_ALL_INVALIDATE & ~TU_CMD_FLAG_##invalidate);   \
   }

   SRC_INCOHERENT_FLUSH(CCU_COLOR, CCU_FLUSH_COLOR, CCU_INVALIDATE_COLOR)
   SRC_INCOHERENT_FLUSH(CCU_DEPTH, CCU_FLUSH_DEPTH, CCU_INVALIDATE_DEPTH)

#undef SRC_INCOHERENT_FLUSH

   /* Treat host & sysmem write accesses the same, since the kernel implicitly
    * drains the queue before signalling completion to the host.
    */
   if (dst_mask & (TU_ACCESS_SYSMEM_READ | TU_ACCESS_SYSMEM_WRITE)) {
      flush_bits |= cache->pending_flush_bits & TU_CMD_FLAG_ALL_FLUSH;
   }

#define DST_FLUSH(domain, flush, invalidate) \
   if (dst_mask & (TU_ACCESS_##domain##_READ |                 \
                   TU_ACCESS_##domain##_WRITE)) {              \
      flush_bits |= cache->pending_flush_bits &                \
         (TU_CMD_FLAG_##invalidate |                           \
          (TU_CMD_FLAG_ALL_FLUSH & ~TU_CMD_FLAG_##flush));     \
   }

   DST_FLUSH(UCHE, CACHE_FLUSH, CACHE_INVALIDATE)
   DST_FLUSH(CCU_COLOR, CCU_FLUSH_COLOR, CCU_INVALIDATE_COLOR)
   DST_FLUSH(CCU_DEPTH, CCU_FLUSH_DEPTH, CCU_INVALIDATE_DEPTH)

#undef DST_FLUSH

#define DST_INCOHERENT_FLUSH(domain, flush, invalidate) \
   if (dst_mask & (TU_ACCESS_##domain##_INCOHERENT_READ |      \
                   TU_ACCESS_##domain##_INCOHERENT_WRITE)) {   \
      flush_bits |= TU_CMD_FLAG_##invalidate |                 \
          (cache->pending_flush_bits &                         \
           (TU_CMD_FLAG_ALL_FLUSH & ~TU_CMD_FLAG_##flush));    \
   }

   DST_INCOHERENT_FLUSH(CCU_COLOR, CCU_FLUSH_COLOR, CCU_INVALIDATE_COLOR)
   DST_INCOHERENT_FLUSH(CCU_DEPTH, CCU_FLUSH_DEPTH, CCU_INVALIDATE_DEPTH)

#undef DST_INCOHERENT_FLUSH

   cache->flush_bits |= flush_bits;
   cache->pending_flush_bits &= ~flush_bits;
}

/* When translating Vulkan access flags to which cache is accessed
 * (CCU/UCHE/sysmem), we should take into account both the access flags and
 * the stage so that accesses with MEMORY_READ_BIT/MEMORY_WRITE_BIT + a
 * specific stage return something sensible. The specification for
 * VK_KHR_synchronization2 says that we should do this:
 *
 *    Additionally, scoping the pipeline stages into the barrier structs
 *    allows the use of the MEMORY_READ and MEMORY_WRITE flags without
 *    sacrificing precision. The per-stage access flags should be used to
 *    disambiguate specific accesses in a given stage or set of stages - for
 *    instance, between uniform reads and sampling operations.
 *
 * Note that while in all known cases the stage is actually enough, we should
 * still narrow things down based on the access flags to handle "old-style"
 * barriers that may specify a wider range of stages but more precise access
 * flags. These helpers allow us to do both.
 */

static bool
filter_read_access(VkAccessFlags2 flags, VkPipelineStageFlags2 stages,
                   VkAccessFlags2 tu_flags, VkPipelineStageFlags2 tu_stages)
{
   return (flags & (tu_flags | VK_ACCESS_2_MEMORY_READ_BIT)) &&
      (stages & (tu_stages | VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT));
}

static bool
filter_write_access(VkAccessFlags2 flags, VkPipelineStageFlags2 stages,
                    VkAccessFlags2 tu_flags, VkPipelineStageFlags2 tu_stages)
{
   return (flags & (tu_flags | VK_ACCESS_2_MEMORY_WRITE_BIT)) &&
      (stages & (tu_stages | VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT));
}

static bool
gfx_read_access(VkAccessFlags2 flags, VkPipelineStageFlags2 stages,
                VkAccessFlags2 tu_flags, VkPipelineStageFlags2 tu_stages)
{
   return filter_read_access(flags, stages, tu_flags,
                             tu_stages | VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
}

static bool
gfx_write_access(VkAccessFlags2 flags, VkPipelineStageFlags2 stages,
                 VkAccessFlags2 tu_flags, VkPipelineStageFlags2 tu_stages)
{
   return filter_write_access(flags, stages, tu_flags,
                              tu_stages | VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT);
}
static enum tu_cmd_access_mask
vk2tu_access(VkAccessFlags2 flags, VkPipelineStageFlags2 stages, bool image_only, bool gmem)
{
   enum tu_cmd_access_mask mask = 0;

   if (gfx_read_access(flags, stages,
                       VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT |
                       VK_ACCESS_2_CONDITIONAL_RENDERING_READ_BIT_EXT |
                       VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT |
                       VK_ACCESS_2_HOST_READ_BIT,
                       VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
                       VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT |
                       VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT |
                       VK_PIPELINE_STAGE_2_HOST_BIT))
      mask |= TU_ACCESS_SYSMEM_READ;

   if (gfx_write_access(flags, stages,
                        VK_ACCESS_2_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT,
                        VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT))
      mask |= TU_ACCESS_CP_WRITE;

   if (gfx_write_access(flags, stages,
                        VK_ACCESS_2_HOST_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_HOST_BIT))
      mask |= TU_ACCESS_SYSMEM_WRITE;

#define SHADER_STAGES \
   (VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | \
    VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT | \
    VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT | \
    VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT | \
    VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT | \
    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | \
    VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)


   if (gfx_read_access(flags, stages,
                       VK_ACCESS_2_INDEX_READ_BIT |
                       VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT |
                       VK_ACCESS_2_UNIFORM_READ_BIT |
                       VK_ACCESS_2_INPUT_ATTACHMENT_READ_BIT |
                       VK_ACCESS_2_SHADER_READ_BIT,
                       VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT |
                       VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
                       VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT |
                       SHADER_STAGES))
       mask |= TU_ACCESS_UCHE_READ;

   if (gfx_write_access(flags, stages,
                        VK_ACCESS_2_SHADER_WRITE_BIT |
                        VK_ACCESS_2_TRANSFORM_FEEDBACK_WRITE_BIT_EXT,
                        VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT |
                        SHADER_STAGES))
       mask |= TU_ACCESS_UCHE_WRITE;

   /* When using GMEM, the CCU is always flushed automatically to GMEM, and
    * then GMEM is flushed to sysmem. Furthermore, we already had to flush any
    * previous writes in sysmem mode when transitioning to GMEM. Therefore we
    * can ignore CCU and pretend that color attachments and transfers use
    * sysmem directly.
    */

   if (gfx_read_access(flags, stages,
                       VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT |
                       VK_ACCESS_2_COLOR_ATTACHMENT_READ_NONCOHERENT_BIT_EXT,
                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT)) {
      if (gmem)
         mask |= TU_ACCESS_SYSMEM_READ;
      else
         mask |= TU_ACCESS_CCU_COLOR_INCOHERENT_READ;
   }

   if (gfx_read_access(flags, stages,
                       VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT,
                       VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                       VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT)) {
      if (gmem)
         mask |= TU_ACCESS_SYSMEM_READ;
      else
         mask |= TU_ACCESS_CCU_DEPTH_INCOHERENT_READ;
   }

   if (gfx_write_access(flags, stages,
                        VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT)) {
      if (gmem) {
         mask |= TU_ACCESS_SYSMEM_WRITE;
      } else {
         mask |= TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE;
      }
   }

   if (gfx_write_access(flags, stages,
                        VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
                        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT |
                        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT)) {
      if (gmem) {
         mask |= TU_ACCESS_SYSMEM_WRITE;
      } else {
         mask |= TU_ACCESS_CCU_DEPTH_INCOHERENT_WRITE;
      }
   }

   if (filter_write_access(flags, stages,
                           VK_ACCESS_2_TRANSFER_WRITE_BIT,
                           VK_PIPELINE_STAGE_2_COPY_BIT |
                           VK_PIPELINE_STAGE_2_BLIT_BIT |
                           VK_PIPELINE_STAGE_2_CLEAR_BIT |
                           VK_PIPELINE_STAGE_2_RESOLVE_BIT |
                           VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT)) {
      if (gmem) {
         mask |= TU_ACCESS_SYSMEM_WRITE;
      } else if (image_only) {
         /* Because we always split up blits/copies of images involving
          * multiple layers, we always access each layer in the same way, with
          * the same base address, same format, etc. This means we can avoid
          * flushing between multiple writes to the same image. This elides
          * flushes between e.g. multiple blits to the same image.
          */
         mask |= TU_ACCESS_CCU_COLOR_WRITE;
      } else {
         mask |= TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE;
      }
   }

   if (filter_read_access(flags, stages,
                          VK_ACCESS_2_TRANSFER_READ_BIT,
                          VK_PIPELINE_STAGE_2_COPY_BIT |
                          VK_PIPELINE_STAGE_2_BLIT_BIT |
                          VK_PIPELINE_STAGE_2_RESOLVE_BIT |
                          VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT)) {
      mask |= TU_ACCESS_UCHE_READ;
   }

   return mask;
}

/* These helpers deal with legacy BOTTOM_OF_PIPE/TOP_OF_PIPE stages.
 */

static VkPipelineStageFlags2
sanitize_src_stage(VkPipelineStageFlags2 stage_mask)
{
   /* From the Vulkan spec:
    *
    *    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT is ...  equivalent to
    *    VK_PIPELINE_STAGE_2_NONE in the first scope.
    *
    *    VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT is equivalent to
    *    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT with VkAccessFlags2 set to 0
    *    when specified in the first synchronization scope, ...
    */
   if (stage_mask & VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT)
      return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

   return stage_mask & ~VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
}

static VkPipelineStageFlags2
sanitize_dst_stage(VkPipelineStageFlags2 stage_mask)
{
   /* From the Vulkan spec:
    *
    *    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT is equivalent to
    *    VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT with VkAccessFlags2 set to 0
    *    when specified in the second synchronization scope, ...
    *
    *    VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT is ... equivalent to
    *    VK_PIPELINE_STAGE_2_NONE in the second scope.
    *
    */
   if (stage_mask & VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT)
      return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

   return stage_mask & ~VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT;
}

static enum tu_stage
vk2tu_single_stage(VkPipelineStageFlags2 vk_stage, bool dst)
{
   if (vk_stage == VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT ||
       vk_stage == VK_PIPELINE_STAGE_2_CONDITIONAL_RENDERING_BIT_EXT)
      return TU_STAGE_CP;

   if (vk_stage == VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT ||
       vk_stage == VK_PIPELINE_STAGE_2_INDEX_INPUT_BIT ||
       vk_stage == VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT)
      return TU_STAGE_FE;

   if (vk_stage == VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT ||
       vk_stage == VK_PIPELINE_STAGE_2_TESSELLATION_CONTROL_SHADER_BIT ||
       vk_stage == VK_PIPELINE_STAGE_2_TESSELLATION_EVALUATION_SHADER_BIT ||
       vk_stage == VK_PIPELINE_STAGE_2_GEOMETRY_SHADER_BIT ||
       vk_stage == VK_PIPELINE_STAGE_2_PRE_RASTERIZATION_SHADERS_BIT)
      return TU_STAGE_SP_VS;

   if (vk_stage == VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT ||
       vk_stage == VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT)
      return TU_STAGE_SP_PS;

   if (vk_stage == VK_PIPELINE_STAGE_2_TRANSFORM_FEEDBACK_BIT_EXT || /* Yes, really */
   /* See comment in TU_STAGE_GRAS about early fragment tests */
       vk_stage == VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT ||
       vk_stage == VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT ||
       vk_stage == VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT)

      return TU_STAGE_PS;

   if (vk_stage == VK_PIPELINE_STAGE_2_COPY_BIT ||
       vk_stage == VK_PIPELINE_STAGE_2_BLIT_BIT ||
       vk_stage == VK_PIPELINE_STAGE_2_RESOLVE_BIT ||
       vk_stage == VK_PIPELINE_STAGE_2_CLEAR_BIT ||
       vk_stage == VK_PIPELINE_STAGE_2_ALL_TRANSFER_BIT)
      /* Blits read in SP_PS and write in PS, in both 2d and 3d cases */
      return dst ? TU_STAGE_SP_PS : TU_STAGE_PS;

   if (vk_stage == VK_PIPELINE_STAGE_2_ALL_GRAPHICS_BIT ||
       vk_stage == VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)
      /* Be conservative */
      return dst ? TU_STAGE_CP : TU_STAGE_PS;

   if (vk_stage == VK_PIPELINE_STAGE_2_HOST_BIT)
      return dst ? TU_STAGE_PS : TU_STAGE_CP;

   unreachable("unknown pipeline stage");
}

static enum tu_stage
vk2tu_src_stage(VkPipelineStageFlags vk_stages)
{
   enum tu_stage stage = TU_STAGE_CP;
   u_foreach_bit (bit, vk_stages) {
      enum tu_stage new_stage = vk2tu_single_stage(1ull << bit, false);
      stage = MAX2(stage, new_stage);
   }

   return stage;
}

static enum tu_stage
vk2tu_dst_stage(VkPipelineStageFlags vk_stages)
{
   enum tu_stage stage = TU_STAGE_PS;
   u_foreach_bit (bit, vk_stages) {
      enum tu_stage new_stage = vk2tu_single_stage(1ull << bit, true);
      stage = MIN2(stage, new_stage);
   }

   return stage;
}

static void
tu_flush_for_stage(struct tu_cache_state *cache,
                   enum tu_stage src_stage, enum tu_stage dst_stage)
{
   /* As far as we know, flushes take place in the last stage so if there are
    * any pending flushes then we have to move down the source stage, because
    * the data only becomes available when the flush finishes. In particular
    * this can matter when the CP writes something and we need to invalidate
    * UCHE to read it.
    */
   if (cache->flush_bits & (TU_CMD_FLAG_ALL_FLUSH | TU_CMD_FLAG_ALL_INVALIDATE))
      src_stage = TU_STAGE_PS;

   /* Note: if the destination stage is the CP, then the CP also has to wait
    * for any WFI's to finish. This is already done for draw calls, including
    * before indirect param reads, for the most part, so we just need to WFI.
    *
    * However, some indirect draw opcodes, depending on firmware, don't have
    * implicit CP_WAIT_FOR_ME so we have to handle it manually.
    *
    * Transform feedback counters are read via CP_MEM_TO_REG, which implicitly
    * does CP_WAIT_FOR_ME, but we still need a WFI if the GPU writes it.
    *
    * Currently we read the draw predicate using CP_MEM_TO_MEM, which
    * also implicitly does CP_WAIT_FOR_ME. However CP_DRAW_PRED_SET does *not*
    * implicitly do CP_WAIT_FOR_ME, it seems to only wait for counters to
    * complete since it's written for DX11 where you can only predicate on the
    * result of a query object. So if we implement 64-bit comparisons in the
    * future, or if CP_DRAW_PRED_SET grows the capability to do 32-bit
    * comparisons, then this will have to be dealt with.
    */
   if (src_stage > dst_stage) {
      cache->flush_bits |= TU_CMD_FLAG_WAIT_FOR_IDLE;
      if (dst_stage == TU_STAGE_CP)
         cache->pending_flush_bits |= TU_CMD_FLAG_WAIT_FOR_ME;
   }
}

void
tu_render_pass_state_merge(struct tu_render_pass_state *dst,
                           const struct tu_render_pass_state *src)
{
   dst->xfb_used |= src->xfb_used;
   dst->has_tess |= src->has_tess;
   dst->has_prim_generated_query_in_rp |= src->has_prim_generated_query_in_rp;
   dst->disable_gmem |= src->disable_gmem;
   dst->sysmem_single_prim_mode |= src->sysmem_single_prim_mode;
   dst->draw_cs_writes_to_cond_pred |= src->draw_cs_writes_to_cond_pred;

   dst->drawcall_count += src->drawcall_count;
   dst->drawcall_bandwidth_per_sample_sum +=
      src->drawcall_bandwidth_per_sample_sum;
}

void
tu_restore_suspended_pass(struct tu_cmd_buffer *cmd,
                          struct tu_cmd_buffer *suspended)
{
   cmd->state.pass = suspended->state.suspended_pass.pass;
   cmd->state.subpass = suspended->state.suspended_pass.subpass;
   cmd->state.framebuffer = suspended->state.suspended_pass.framebuffer;
   cmd->state.attachments = suspended->state.suspended_pass.attachments;
   cmd->state.render_area = suspended->state.suspended_pass.render_area;
   cmd->state.gmem_layout = suspended->state.suspended_pass.gmem_layout;
   cmd->state.tiling = &cmd->state.framebuffer->tiling[cmd->state.gmem_layout];
   cmd->state.lrz = suspended->state.suspended_pass.lrz;
}

/* Take the saved pre-chain in "secondary" and copy its commands to "cmd",
 * appending it after any saved-up commands in "cmd".
 */
void
tu_append_pre_chain(struct tu_cmd_buffer *cmd,
                    struct tu_cmd_buffer *secondary)
{
   tu_cs_add_entries(&cmd->draw_cs, &secondary->pre_chain.draw_cs);
   tu_cs_add_entries(&cmd->draw_epilogue_cs,
                     &secondary->pre_chain.draw_epilogue_cs);

   tu_render_pass_state_merge(&cmd->state.rp,
                              &secondary->pre_chain.state);
   tu_clone_trace_range(cmd, &cmd->draw_cs, secondary->pre_chain.trace_renderpass_start,
         secondary->pre_chain.trace_renderpass_end);
}

/* Take the saved post-chain in "secondary" and copy it to "cmd".
 */
void
tu_append_post_chain(struct tu_cmd_buffer *cmd,
                     struct tu_cmd_buffer *secondary)
{
   tu_cs_add_entries(&cmd->draw_cs, &secondary->draw_cs);
   tu_cs_add_entries(&cmd->draw_epilogue_cs, &secondary->draw_epilogue_cs);

   tu_clone_trace_range(cmd, &cmd->draw_cs, secondary->trace_renderpass_start,
         secondary->trace_renderpass_end);
   cmd->state.rp = secondary->state.rp;
}

/* Assuming "secondary" is just a sequence of suspended and resuming passes,
 * copy its state to "cmd". This also works instead of tu_append_post_chain(),
 * but it's a bit slower because we don't assume that the chain begins in
 * "secondary" and therefore have to care about the command buffer's
 * renderpass state.
 */
void
tu_append_pre_post_chain(struct tu_cmd_buffer *cmd,
                         struct tu_cmd_buffer *secondary)
{
   tu_cs_add_entries(&cmd->draw_cs, &secondary->draw_cs);
   tu_cs_add_entries(&cmd->draw_epilogue_cs, &secondary->draw_epilogue_cs);

   tu_clone_trace_range(cmd, &cmd->draw_cs, secondary->trace_renderpass_start,
         secondary->trace_renderpass_end);
   tu_render_pass_state_merge(&cmd->state.rp,
                              &secondary->state.rp);
}

/* Take the current render pass state and save it to "pre_chain" to be
 * combined later.
 */
static void
tu_save_pre_chain(struct tu_cmd_buffer *cmd)
{
   tu_cs_add_entries(&cmd->pre_chain.draw_cs,
                     &cmd->draw_cs);
   tu_cs_add_entries(&cmd->pre_chain.draw_epilogue_cs,
                     &cmd->draw_epilogue_cs);
   cmd->pre_chain.trace_renderpass_start =
      cmd->trace_renderpass_start;
   cmd->pre_chain.trace_renderpass_end =
      cmd->trace_renderpass_end;
   cmd->pre_chain.state = cmd->state.rp;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdExecuteCommands(VkCommandBuffer commandBuffer,
                      uint32_t commandBufferCount,
                      const VkCommandBuffer *pCmdBuffers)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VkResult result;

   assert(commandBufferCount > 0);

   /* Emit any pending flushes. */
   if (cmd->state.pass) {
      tu_flush_all_pending(&cmd->state.renderpass_cache);
      tu_emit_cache_flush_renderpass(cmd, &cmd->draw_cs);
   } else {
      tu_flush_all_pending(&cmd->state.cache);
      tu_emit_cache_flush(cmd, &cmd->cs);
   }

   for (uint32_t i = 0; i < commandBufferCount; i++) {
      TU_FROM_HANDLE(tu_cmd_buffer, secondary, pCmdBuffers[i]);

      if (secondary->usage_flags &
          VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT) {
         assert(tu_cs_is_empty(&secondary->cs));

         result = tu_cs_add_entries(&cmd->draw_cs, &secondary->draw_cs);
         if (result != VK_SUCCESS) {
            vk_command_buffer_set_error(&cmd->vk, result);
            break;
         }

         result = tu_cs_add_entries(&cmd->draw_epilogue_cs,
               &secondary->draw_epilogue_cs);
         if (result != VK_SUCCESS) {
            vk_command_buffer_set_error(&cmd->vk, result);
            break;
         }

         /* If LRZ was made invalid in secondary - we should disable
          * LRZ retroactively for the whole renderpass.
          */
         if (!secondary->state.lrz.valid)
            cmd->state.lrz.valid = false;

         tu_clone_trace(cmd, &cmd->draw_cs, &secondary->trace);
         tu_render_pass_state_merge(&cmd->state.rp, &secondary->state.rp);
      } else {
         switch (secondary->state.suspend_resume) {
         case SR_NONE:
            assert(tu_cs_is_empty(&secondary->draw_cs));
            assert(tu_cs_is_empty(&secondary->draw_epilogue_cs));
            tu_cs_add_entries(&cmd->cs, &secondary->cs);
            tu_clone_trace(cmd, &cmd->cs, &secondary->trace);
            break;

         case SR_IN_PRE_CHAIN:
            /* cmd may be empty, which means that the chain begins before cmd
             * in which case we have to update its state.
             */
            if (cmd->state.suspend_resume == SR_NONE) {
               cmd->state.suspend_resume = SR_IN_PRE_CHAIN;
               cmd->trace_renderpass_start = u_trace_end_iterator(&cmd->trace);
            }

            /* The secondary is just a continuous suspend/resume chain so we
             * just have to append it to the the command buffer.
             */
            assert(tu_cs_is_empty(&secondary->cs));
            tu_append_pre_post_chain(cmd, secondary);
            break;

         case SR_AFTER_PRE_CHAIN:
         case SR_IN_CHAIN:
         case SR_IN_CHAIN_AFTER_PRE_CHAIN:
            if (secondary->state.suspend_resume == SR_AFTER_PRE_CHAIN ||
                secondary->state.suspend_resume == SR_IN_CHAIN_AFTER_PRE_CHAIN) {
               /* In thse cases there is a `pre_chain` in the secondary which
                * ends that we need to append to the primary.
                */

               if (cmd->state.suspend_resume == SR_NONE)
                  cmd->trace_renderpass_start = u_trace_end_iterator(&cmd->trace);

               tu_append_pre_chain(cmd, secondary);

               /* We're about to render, so we need to end the command stream
                * in case there were any extra commands generated by copying
                * the trace.
                */
               tu_cs_end(&cmd->draw_cs);
               tu_cs_end(&cmd->draw_epilogue_cs);

               switch (cmd->state.suspend_resume) {
               case SR_NONE:
               case SR_IN_PRE_CHAIN:
                  /* The renderpass chain ends in the secondary but isn't
                   * started in the primary, so we have to move the state to
                   * `pre_chain`.
                   */
                  cmd->trace_renderpass_end = u_trace_end_iterator(&cmd->trace);
                  tu_save_pre_chain(cmd);
                  cmd->state.suspend_resume = SR_AFTER_PRE_CHAIN;
                  break;
               case SR_IN_CHAIN:
               case SR_IN_CHAIN_AFTER_PRE_CHAIN:
                  /* The renderpass ends in the secondary and starts somewhere
                   * earlier in this primary. Since the last render pass in
                   * the chain is in the secondary, we are technically outside
                   * of a render pass.  Fix that here by reusing the dynamic
                   * render pass that was setup for the last suspended render
                   * pass before the secondary.
                   */
                  tu_restore_suspended_pass(cmd, cmd);

                  tu_cmd_render(cmd);
                  if (cmd->state.suspend_resume == SR_IN_CHAIN)
                     cmd->state.suspend_resume = SR_NONE;
                  else
                     cmd->state.suspend_resume = SR_AFTER_PRE_CHAIN;
                  break;
               case SR_AFTER_PRE_CHAIN:
                  unreachable("resuming render pass is not preceded by suspending one");
               }

               tu_reset_render_pass(cmd);
            }

            tu_cs_add_entries(&cmd->cs, &secondary->cs);

            if (secondary->state.suspend_resume == SR_IN_CHAIN_AFTER_PRE_CHAIN ||
                secondary->state.suspend_resume == SR_IN_CHAIN) {
               /* The secondary ends in a "post-chain" (the opposite of a
                * pre-chain) that we need to copy into the current command
                * buffer.
                */
               cmd->trace_renderpass_start = u_trace_end_iterator(&cmd->trace);
               tu_append_post_chain(cmd, secondary);
               cmd->trace_renderpass_end = u_trace_end_iterator(&cmd->trace);
               cmd->state.suspended_pass = secondary->state.suspended_pass;

               switch (cmd->state.suspend_resume) {
               case SR_NONE:
                  cmd->state.suspend_resume = SR_IN_CHAIN;
                  break;
               case SR_AFTER_PRE_CHAIN:
                  cmd->state.suspend_resume = SR_IN_CHAIN_AFTER_PRE_CHAIN;
                  break;
               default:
                  unreachable("suspending render pass is followed by a not resuming one");
               }
            }
         }
      }

      cmd->state.index_size = secondary->state.index_size; /* for restart index update */
   }
   cmd->state.dirty = ~0u; /* TODO: set dirty only what needs to be */

   if (!cmd->state.lrz.gpu_dir_tracking && cmd->state.pass) {
      /* After a secondary command buffer is executed, LRZ is not valid
       * until it is cleared again.
       */
      cmd->state.lrz.valid = false;
   }

   /* After executing secondary command buffers, there may have been arbitrary
    * flushes executed, so when we encounter a pipeline barrier with a
    * srcMask, we have to assume that we need to invalidate. Therefore we need
    * to re-initialize the cache with all pending invalidate bits set.
    */
   if (cmd->state.pass) {
      tu_cache_init(&cmd->state.renderpass_cache);
   } else {
      tu_cache_init(&cmd->state.cache);
   }
}

static void
tu_subpass_barrier(struct tu_cmd_buffer *cmd_buffer,
                   const struct tu_subpass_barrier *barrier,
                   bool external)
{
   /* Note: we don't know until the end of the subpass whether we'll use
    * sysmem, so assume sysmem here to be safe.
    */
   struct tu_cache_state *cache =
      external ? &cmd_buffer->state.cache : &cmd_buffer->state.renderpass_cache;
   VkPipelineStageFlags2 src_stage_vk =
      sanitize_src_stage(barrier->src_stage_mask);
   VkPipelineStageFlags2 dst_stage_vk =
      sanitize_dst_stage(barrier->dst_stage_mask);
   enum tu_cmd_access_mask src_flags =
      vk2tu_access(barrier->src_access_mask, src_stage_vk, false, false);
   enum tu_cmd_access_mask dst_flags =
      vk2tu_access(barrier->dst_access_mask, dst_stage_vk, false, false);

   if (barrier->incoherent_ccu_color)
      src_flags |= TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE;
   if (barrier->incoherent_ccu_depth)
      src_flags |= TU_ACCESS_CCU_DEPTH_INCOHERENT_WRITE;

   tu_flush_for_access(cache, src_flags, dst_flags);

   enum tu_stage src_stage = vk2tu_src_stage(src_stage_vk);
   enum tu_stage dst_stage = vk2tu_dst_stage(dst_stage_vk);
   tu_flush_for_stage(cache, src_stage, dst_stage);
}

/* emit mrt/zs/msaa/ubwc state for the subpass that is starting (either at
 * vkCmdBeginRenderPass2() or vkCmdNextSubpass2())
 */
static void
tu_emit_subpass_begin(struct tu_cmd_buffer *cmd)
{
   tu6_emit_zs(cmd, cmd->state.subpass, &cmd->draw_cs);
   tu6_emit_mrt(cmd, cmd->state.subpass, &cmd->draw_cs);
   if (cmd->state.subpass->samples)
      tu6_emit_msaa(&cmd->draw_cs, cmd->state.subpass->samples, cmd->state.line_mode);
   tu6_emit_render_cntl(cmd, cmd->state.subpass, &cmd->draw_cs, false);

   tu_set_input_attachments(cmd, cmd->state.subpass);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                       const VkRenderPassBeginInfo *pRenderPassBegin,
                       const VkSubpassBeginInfo *pSubpassBeginInfo)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   if (unlikely(cmd->device->instance->debug_flags & TU_DEBUG_DYNAMIC)) {
      vk_common_CmdBeginRenderPass2(commandBuffer, pRenderPassBegin,
                                    pSubpassBeginInfo);
      return;
   }

   TU_FROM_HANDLE(tu_render_pass, pass, pRenderPassBegin->renderPass);
   TU_FROM_HANDLE(tu_framebuffer, fb, pRenderPassBegin->framebuffer);

   const struct VkRenderPassAttachmentBeginInfo *pAttachmentInfo =
      vk_find_struct_const(pRenderPassBegin->pNext,
                           RENDER_PASS_ATTACHMENT_BEGIN_INFO);

   cmd->state.pass = pass;
   cmd->state.subpass = pass->subpasses;
   cmd->state.framebuffer = fb;
   cmd->state.render_area = pRenderPassBegin->renderArea;

   cmd->state.attachments =
      vk_alloc(&cmd->vk.pool->alloc, pass->attachment_count *
               sizeof(cmd->state.attachments[0]), 8,
               VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (!cmd->state.attachments) {
      vk_command_buffer_set_error(&cmd->vk, VK_ERROR_OUT_OF_HOST_MEMORY);
      return;
   }

   for (unsigned i = 0; i < pass->attachment_count; i++) {
      cmd->state.attachments[i] = pAttachmentInfo ?
         tu_image_view_from_handle(pAttachmentInfo->pAttachments[i]) :
         cmd->state.framebuffer->attachments[i].attachment;
   }
   tu_choose_gmem_layout(cmd);

   trace_start_render_pass(&cmd->trace, &cmd->cs);

   /* Note: because this is external, any flushes will happen before draw_cs
    * gets called. However deferred flushes could have to happen later as part
    * of the subpass.
    */
   tu_subpass_barrier(cmd, &pass->subpasses[0].start_barrier, true);
   cmd->state.renderpass_cache.pending_flush_bits =
      cmd->state.cache.pending_flush_bits;
   cmd->state.renderpass_cache.flush_bits = 0;

   if (pass->subpasses[0].feedback_invalidate)
      cmd->state.renderpass_cache.flush_bits |= TU_CMD_FLAG_CACHE_INVALIDATE;

   tu_lrz_begin_renderpass(cmd, pRenderPassBegin->pClearValues);

   cmd->trace_renderpass_start = u_trace_end_iterator(&cmd->trace);

   tu_emit_renderpass_begin(cmd, pRenderPassBegin->pClearValues);
   tu_emit_subpass_begin(cmd);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdBeginRendering(VkCommandBuffer commandBuffer,
                     const VkRenderingInfo *pRenderingInfo)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VkClearValue clear_values[2 * (MAX_RTS + 1)];

   tu_setup_dynamic_render_pass(cmd, pRenderingInfo);
   tu_setup_dynamic_framebuffer(cmd, pRenderingInfo);

   cmd->state.pass = &cmd->dynamic_pass;
   cmd->state.subpass = &cmd->dynamic_subpass;
   cmd->state.framebuffer = &cmd->dynamic_framebuffer;
   cmd->state.render_area = pRenderingInfo->renderArea;

   cmd->state.attachments = cmd->dynamic_attachments;

   for (unsigned i = 0; i < pRenderingInfo->colorAttachmentCount; i++) {
      uint32_t a = cmd->dynamic_subpass.color_attachments[i].attachment;
      if (!pRenderingInfo->pColorAttachments[i].imageView)
         continue;

      TU_FROM_HANDLE(tu_image_view, view,
                     pRenderingInfo->pColorAttachments[i].imageView);
      cmd->state.attachments[a] = view;
      clear_values[a] = pRenderingInfo->pColorAttachments[i].clearValue;

      a = cmd->dynamic_subpass.resolve_attachments[i].attachment;
      if (a != VK_ATTACHMENT_UNUSED) {
         TU_FROM_HANDLE(tu_image_view, resolve_view,
                        pRenderingInfo->pColorAttachments[i].resolveImageView);
         cmd->state.attachments[a] = resolve_view;
      }
   }

   uint32_t a = cmd->dynamic_subpass.depth_stencil_attachment.attachment;
   if (pRenderingInfo->pDepthAttachment || pRenderingInfo->pStencilAttachment) {
      const struct VkRenderingAttachmentInfo *common_info =
         (pRenderingInfo->pDepthAttachment &&
          pRenderingInfo->pDepthAttachment->imageView != VK_NULL_HANDLE) ?
         pRenderingInfo->pDepthAttachment :
         pRenderingInfo->pStencilAttachment;
      if (common_info && common_info->imageView != VK_NULL_HANDLE) {
         TU_FROM_HANDLE(tu_image_view, view, common_info->imageView);
         cmd->state.attachments[a] = view;
         if (pRenderingInfo->pDepthAttachment) {
            clear_values[a].depthStencil.depth =
               pRenderingInfo->pDepthAttachment->clearValue.depthStencil.depth;
         }

         if (pRenderingInfo->pStencilAttachment) {
            clear_values[a].depthStencil.stencil =
               pRenderingInfo->pStencilAttachment->clearValue.depthStencil.stencil;
         }

         if (cmd->dynamic_subpass.resolve_count >
             cmd->dynamic_subpass.color_count) {
            TU_FROM_HANDLE(tu_image_view, resolve_view,
                           common_info->resolveImageView);
            a = cmd->dynamic_subpass.resolve_attachments[cmd->dynamic_subpass.color_count].attachment;
            cmd->state.attachments[a] = resolve_view;
         }
      }
   }

   if (unlikely(cmd->device->instance->debug_flags & TU_DEBUG_DYNAMIC)) {
      const VkRenderingSelfDependencyInfoMESA *self_dependency =
         vk_find_struct_const(pRenderingInfo->pNext, RENDERING_SELF_DEPENDENCY_INFO_MESA);
      if (self_dependency &&
          (self_dependency->colorSelfDependencies ||
           self_dependency->depthSelfDependency ||
           self_dependency->stencilSelfDependency)) {
         /* Mesa's renderpass emulation requires us to use normal attachments
          * for input attachments, and currently doesn't try to keep track of
          * which color/depth attachment an input attachment corresponds to.
          * So when there's a self-dependency, we have to use sysmem.
          */
         cmd->state.rp.disable_gmem = true;
      }
   }

   tu_choose_gmem_layout(cmd);

   cmd->state.renderpass_cache.pending_flush_bits =
      cmd->state.cache.pending_flush_bits;
   cmd->state.renderpass_cache.flush_bits = 0;

   bool resuming = pRenderingInfo->flags & VK_RENDERING_RESUMING_BIT;
   bool suspending = pRenderingInfo->flags & VK_RENDERING_SUSPENDING_BIT;
   cmd->state.suspending = suspending;
   cmd->state.resuming = resuming;

   /* We can't track LRZ across command buffer boundaries, so we have to
    * disable LRZ when resuming/suspending unless we can track on the GPU.
    */
   if ((resuming || suspending) &&
       !cmd->device->physical_device->info->a6xx.has_lrz_dir_tracking) {
      cmd->state.lrz.valid = false;
   } else {
      if (resuming)
         tu_lrz_begin_resumed_renderpass(cmd, clear_values);
      else
         tu_lrz_begin_renderpass(cmd, clear_values);
   }


   if (suspending) {
      cmd->state.suspended_pass.pass = cmd->state.pass;
      cmd->state.suspended_pass.subpass = cmd->state.subpass;
      cmd->state.suspended_pass.framebuffer = cmd->state.framebuffer;
      cmd->state.suspended_pass.render_area = cmd->state.render_area;
      cmd->state.suspended_pass.attachments = cmd->state.attachments;
      cmd->state.suspended_pass.gmem_layout = cmd->state.gmem_layout;
   }

   if (!resuming) {
      trace_start_render_pass(&cmd->trace, &cmd->cs);
   }

   if (!resuming || cmd->state.suspend_resume == SR_NONE) {
      cmd->trace_renderpass_start = u_trace_end_iterator(&cmd->trace);
   }

   if (!resuming) {
      tu_emit_renderpass_begin(cmd, clear_values);
      tu_emit_subpass_begin(cmd);
   }

   if (suspending && !resuming) {
      /* entering a chain */
      switch (cmd->state.suspend_resume) {
      case SR_NONE:
         cmd->state.suspend_resume = SR_IN_CHAIN;
         break;
      case SR_AFTER_PRE_CHAIN:
         cmd->state.suspend_resume = SR_IN_CHAIN_AFTER_PRE_CHAIN;
         break;
      case SR_IN_PRE_CHAIN:
      case SR_IN_CHAIN:
      case SR_IN_CHAIN_AFTER_PRE_CHAIN:
         unreachable("suspending render pass not followed by resuming pass");
         break;
      }
   }

   if (resuming && cmd->state.suspend_resume == SR_NONE)
      cmd->state.suspend_resume = SR_IN_PRE_CHAIN;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdNextSubpass2(VkCommandBuffer commandBuffer,
                   const VkSubpassBeginInfo *pSubpassBeginInfo,
                   const VkSubpassEndInfo *pSubpassEndInfo)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   if (unlikely(cmd->device->instance->debug_flags & TU_DEBUG_DYNAMIC)) {
      vk_common_CmdNextSubpass2(commandBuffer, pSubpassBeginInfo,
                                pSubpassEndInfo);
      return;
   }

   const struct tu_render_pass *pass = cmd->state.pass;
   struct tu_cs *cs = &cmd->draw_cs;
   const struct tu_subpass *last_subpass = cmd->state.subpass;

   const struct tu_subpass *subpass = cmd->state.subpass++;

   /* Track LRZ valid state
    *
    * TODO: Improve this tracking for keeping the state of the past depth/stencil images,
    * so if they become active again, we reuse its old state.
    */
   if (last_subpass->depth_stencil_attachment.attachment != subpass->depth_stencil_attachment.attachment) {
      cmd->state.lrz.valid = false;
      cmd->state.dirty |= TU_CMD_DIRTY_LRZ;
   }

   tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_GMEM);

   if (subpass->resolve_attachments) {
      tu6_emit_blit_scissor(cmd, cs, true);

      for (unsigned i = 0; i < subpass->resolve_count; i++) {
         uint32_t a = subpass->resolve_attachments[i].attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;

         uint32_t gmem_a = tu_subpass_get_attachment_to_resolve(subpass, i);

         tu_store_gmem_attachment(cmd, cs, a, gmem_a, false);

         if (!pass->attachments[a].gmem)
            continue;

         /* check if the resolved attachment is needed by later subpasses,
          * if it is, should be doing a GMEM->GMEM resolve instead of GMEM->MEM->GMEM..
          */
         perf_debug(cmd->device, "TODO: missing GMEM->GMEM resolve path\n");
         tu_load_gmem_attachment(cmd, cs, a, false, true);
      }
   }

   tu_cond_exec_end(cs);

   tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_SYSMEM);

   tu6_emit_sysmem_resolves(cmd, cs, subpass);

   tu_cond_exec_end(cs);

   /* Handle dependencies for the next subpass */
   tu_subpass_barrier(cmd, &cmd->state.subpass->start_barrier, false);

   if (cmd->state.subpass->feedback_invalidate)
      cmd->state.renderpass_cache.flush_bits |= TU_CMD_FLAG_CACHE_INVALIDATE;

   tu_emit_subpass_begin(cmd);
}

static uint32_t
tu6_user_consts_size(const struct tu_pipeline *pipeline,
                     gl_shader_stage type)
{
   const struct tu_program_descriptor_linkage *link =
      &pipeline->program.link[type];
   uint32_t dwords = 0;

   if (link->push_consts.dwords > 0) {
      unsigned num_units = link->push_consts.dwords;
      dwords += 4 + num_units;
   }

   return dwords;
}

static void
tu6_emit_user_consts(struct tu_cs *cs,
                     const struct tu_pipeline *pipeline,
                     gl_shader_stage type,
                     uint32_t *push_constants)
{
   const struct tu_program_descriptor_linkage *link =
      &pipeline->program.link[type];

   if (link->push_consts.dwords > 0) {
      unsigned num_units = link->push_consts.dwords;
      unsigned offset = link->push_consts.lo;

      /* DST_OFF and NUM_UNIT requires vec4 units */
      tu_cs_emit_pkt7(cs, tu6_stage2opcode(type), 3 + num_units);
      tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(offset / 4) |
            CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
            CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
            CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(type)) |
            CP_LOAD_STATE6_0_NUM_UNIT(num_units / 4));
      tu_cs_emit(cs, 0);
      tu_cs_emit(cs, 0);
      for (unsigned i = 0; i < num_units; i++)
         tu_cs_emit(cs, push_constants[i + offset]);
   }
}

static void
tu6_emit_shared_consts(struct tu_cs *cs,
                       const struct tu_pipeline *pipeline,
                       uint32_t *push_constants,
                       bool compute)
{
   if (pipeline->shared_consts.dwords > 0) {
      /* Offset and num_units for shared consts are in units of dwords. */
      unsigned num_units = pipeline->shared_consts.dwords;
      unsigned offset = pipeline->shared_consts.lo;

      enum a6xx_state_type st = compute ? ST6_UBO : ST6_CONSTANTS;
      uint32_t cp_load_state = compute ? CP_LOAD_STATE6_FRAG : CP_LOAD_STATE6;

      tu_cs_emit_pkt7(cs, cp_load_state, 3 + num_units);
      tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(offset) |
            CP_LOAD_STATE6_0_STATE_TYPE(st) |
            CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
            CP_LOAD_STATE6_0_STATE_BLOCK(SB6_IBO) |
            CP_LOAD_STATE6_0_NUM_UNIT(num_units));
      tu_cs_emit(cs, 0);
      tu_cs_emit(cs, 0);

      for (unsigned i = 0; i < num_units; i++)
         tu_cs_emit(cs, push_constants[i + offset]);
   }
}

static uint32_t
tu6_const_size(struct tu_cmd_buffer *cmd,
               const struct tu_pipeline *pipeline,
               bool compute)
{
   uint32_t dwords = 0;

   if (pipeline->shared_consts.dwords > 0) {
      dwords = pipeline->shared_consts.dwords + 4;
   } else {
      if (compute) {
         dwords = tu6_user_consts_size(pipeline, MESA_SHADER_COMPUTE);
      } else {
         for (uint32_t type = MESA_SHADER_VERTEX; type <= MESA_SHADER_FRAGMENT; type++)
            dwords += tu6_user_consts_size(pipeline, type);
      }
   }

   return dwords;
}

static struct tu_draw_state
tu6_emit_consts(struct tu_cmd_buffer *cmd,
                const struct tu_pipeline *pipeline,
                bool compute)
{
   uint32_t dwords = 0;

   dwords = tu6_const_size(cmd, pipeline, compute);

   if (dwords == 0)
      return (struct tu_draw_state) {};

   struct tu_cs cs;
   tu_cs_begin_sub_stream(&cmd->sub_cs, dwords, &cs);

   if (pipeline->shared_consts.dwords > 0) {
      tu6_emit_shared_consts(&cs, pipeline, cmd->push_constants, compute);

      for (uint32_t i = 0; i < ARRAY_SIZE(pipeline->program.link); i++) {
         const struct tu_program_descriptor_linkage *link =
            &pipeline->program.link[i];
         assert(!link->push_consts.dwords);
      }
   } else {
      if (compute) {
         tu6_emit_user_consts(&cs, pipeline, MESA_SHADER_COMPUTE, cmd->push_constants);
      } else {
         for (uint32_t type = MESA_SHADER_VERTEX; type <= MESA_SHADER_FRAGMENT; type++)
            tu6_emit_user_consts(&cs, pipeline, type, cmd->push_constants);
      }
   }

   return tu_cs_end_draw_state(&cmd->sub_cs, &cs);
}

static bool
tu6_writes_depth(struct tu_cmd_buffer *cmd, bool depth_test_enable)
{
   bool depth_write_enable =
      cmd->state.rb_depth_cntl & A6XX_RB_DEPTH_CNTL_Z_WRITE_ENABLE;

   VkCompareOp depth_compare_op =
      (cmd->state.rb_depth_cntl & A6XX_RB_DEPTH_CNTL_ZFUNC__MASK) >> A6XX_RB_DEPTH_CNTL_ZFUNC__SHIFT;

   bool depth_compare_op_writes = depth_compare_op != VK_COMPARE_OP_NEVER;

   return depth_test_enable && depth_write_enable && depth_compare_op_writes;
}

static bool
tu6_writes_stencil(struct tu_cmd_buffer *cmd)
{
   bool stencil_test_enable =
      cmd->state.rb_stencil_cntl & A6XX_RB_STENCIL_CONTROL_STENCIL_ENABLE;

   bool stencil_front_writemask =
      (cmd->state.pipeline->dynamic_state_mask & BIT(VK_DYNAMIC_STATE_STENCIL_WRITE_MASK)) ?
      (cmd->state.dynamic_stencil_wrmask & 0xff) :
      (cmd->state.pipeline->stencil_wrmask & 0xff);

   bool stencil_back_writemask =
      (cmd->state.pipeline->dynamic_state_mask & BIT(VK_DYNAMIC_STATE_STENCIL_WRITE_MASK)) ?
      ((cmd->state.dynamic_stencil_wrmask & 0xff00) >> 8) :
      (cmd->state.pipeline->stencil_wrmask & 0xff00) >> 8;

   VkStencilOp front_fail_op =
      (cmd->state.rb_stencil_cntl & A6XX_RB_STENCIL_CONTROL_FAIL__MASK) >> A6XX_RB_STENCIL_CONTROL_FAIL__SHIFT;
   VkStencilOp front_pass_op =
      (cmd->state.rb_stencil_cntl & A6XX_RB_STENCIL_CONTROL_ZPASS__MASK) >> A6XX_RB_STENCIL_CONTROL_ZPASS__SHIFT;
   VkStencilOp front_depth_fail_op =
      (cmd->state.rb_stencil_cntl & A6XX_RB_STENCIL_CONTROL_ZFAIL__MASK) >> A6XX_RB_STENCIL_CONTROL_ZFAIL__SHIFT;
   VkStencilOp back_fail_op =
      (cmd->state.rb_stencil_cntl & A6XX_RB_STENCIL_CONTROL_FAIL_BF__MASK) >> A6XX_RB_STENCIL_CONTROL_FAIL_BF__SHIFT;
   VkStencilOp back_pass_op =
      (cmd->state.rb_stencil_cntl & A6XX_RB_STENCIL_CONTROL_ZPASS_BF__MASK) >> A6XX_RB_STENCIL_CONTROL_ZPASS_BF__SHIFT;
   VkStencilOp back_depth_fail_op =
      (cmd->state.rb_stencil_cntl & A6XX_RB_STENCIL_CONTROL_ZFAIL_BF__MASK) >> A6XX_RB_STENCIL_CONTROL_ZFAIL_BF__SHIFT;

   bool stencil_front_op_writes =
      front_pass_op != VK_STENCIL_OP_KEEP &&
      front_fail_op != VK_STENCIL_OP_KEEP &&
      front_depth_fail_op != VK_STENCIL_OP_KEEP;

   bool stencil_back_op_writes =
      back_pass_op != VK_STENCIL_OP_KEEP &&
      back_fail_op != VK_STENCIL_OP_KEEP &&
      back_depth_fail_op != VK_STENCIL_OP_KEEP;

   return stencil_test_enable &&
      ((stencil_front_writemask && stencil_front_op_writes) ||
       (stencil_back_writemask && stencil_back_op_writes));
}

static void
tu6_build_depth_plane_z_mode(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   enum a6xx_ztest_mode zmode = A6XX_EARLY_Z;
   bool depth_test_enable = cmd->state.rb_depth_cntl & A6XX_RB_DEPTH_CNTL_Z_TEST_ENABLE;
   bool depth_write = tu6_writes_depth(cmd, depth_test_enable);
   bool stencil_write = tu6_writes_stencil(cmd);

   if ((cmd->state.pipeline->lrz.fs_has_kill ||
        cmd->state.pipeline->subpass_feedback_loop_ds) &&
       (depth_write || stencil_write)) {
      zmode = (cmd->state.lrz.valid && cmd->state.lrz.enabled)
                 ? A6XX_EARLY_LRZ_LATE_Z
                 : A6XX_LATE_Z;
   }

   if (cmd->state.pipeline->lrz.force_late_z || !depth_test_enable)
      zmode = A6XX_LATE_Z;

   /* User defined early tests take precedence above all else */
   if (cmd->state.pipeline->lrz.early_fragment_tests)
      zmode = A6XX_EARLY_Z;

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SU_DEPTH_PLANE_CNTL, 1);
   tu_cs_emit(cs, A6XX_GRAS_SU_DEPTH_PLANE_CNTL_Z_MODE(zmode));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_DEPTH_PLANE_CNTL, 1);
   tu_cs_emit(cs, A6XX_RB_DEPTH_PLANE_CNTL_Z_MODE(zmode));
}

static void
tu6_emit_blend(struct tu_cs *cs, struct tu_cmd_buffer *cmd)
{
   struct tu_pipeline *pipeline = cmd->state.pipeline;
   uint32_t color_write_enable = cmd->state.pipeline_color_write_enable;

   if (pipeline->dynamic_state_mask &
       BIT(TU_DYNAMIC_STATE_COLOR_WRITE_ENABLE))
      color_write_enable &= cmd->state.color_write_enable;

   for (unsigned i = 0; i < pipeline->num_rts; i++) {
      tu_cs_emit_pkt4(cs, REG_A6XX_RB_MRT_CONTROL(i), 2);
      if (color_write_enable & BIT(i)) {
         tu_cs_emit(cs, cmd->state.rb_mrt_control[i] |
                        ((cmd->state.logic_op_enabled ?
                          cmd->state.rb_mrt_control_rop : 0) &
                         ~pipeline->rb_mrt_control_mask));
         tu_cs_emit(cs, cmd->state.rb_mrt_blend_control[i]);
      } else {
         tu_cs_emit(cs, 0);
         tu_cs_emit(cs, 0);
      }
   }

   uint32_t blend_enable_mask =
      (cmd->state.logic_op_enabled && cmd->state.rop_reads_dst) ?
      color_write_enable : (cmd->state.pipeline_blend_enable &
                            cmd->state.color_write_enable);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_BLEND_CNTL, 1);
   tu_cs_emit(cs, cmd->state.sp_blend_cntl |
                  (A6XX_SP_BLEND_CNTL_ENABLE_BLEND(blend_enable_mask) &
                   ~pipeline->sp_blend_cntl_mask));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLEND_CNTL, 1);
   tu_cs_emit(cs, cmd->state.rb_blend_cntl |
                  (A6XX_RB_BLEND_CNTL_ENABLE_BLEND(blend_enable_mask) &
                   ~pipeline->rb_blend_cntl_mask));
}

static VkResult
tu6_draw_common(struct tu_cmd_buffer *cmd,
                struct tu_cs *cs,
                bool indexed,
                /* note: draw_count is 0 for indirect */
                uint32_t draw_count)
{
   const struct tu_pipeline *pipeline = cmd->state.pipeline;

   /* Fill draw stats for autotuner */
   cmd->state.rp.drawcall_count++;

   cmd->state.rp.drawcall_bandwidth_per_sample_sum +=
      cmd->state.pipeline->color_bandwidth_per_sample;

   /* add depth memory bandwidth cost */
   const uint32_t depth_bandwidth = cmd->state.pipeline->depth_cpp_per_sample;
   if (cmd->state.rb_depth_cntl & A6XX_RB_DEPTH_CNTL_Z_WRITE_ENABLE)
      cmd->state.rp.drawcall_bandwidth_per_sample_sum += depth_bandwidth;
   if (cmd->state.rb_depth_cntl & A6XX_RB_DEPTH_CNTL_Z_TEST_ENABLE)
      cmd->state.rp.drawcall_bandwidth_per_sample_sum += depth_bandwidth;

   /* add stencil memory bandwidth cost */
   const uint32_t stencil_bandwidth =
      cmd->state.pipeline->stencil_cpp_per_sample;
   if (cmd->state.rb_stencil_cntl & A6XX_RB_STENCIL_CONTROL_STENCIL_ENABLE)
      cmd->state.rp.drawcall_bandwidth_per_sample_sum += stencil_bandwidth * 2;

   tu_emit_cache_flush_renderpass(cmd, cs);

   bool primitive_restart_enabled = pipeline->ia.primitive_restart;
   if (pipeline->dynamic_state_mask & BIT(TU_DYNAMIC_STATE_PRIMITIVE_RESTART_ENABLE))
      primitive_restart_enabled = cmd->state.primitive_restart_enable;

   tu_cs_emit_regs(cs, A6XX_PC_PRIMITIVE_CNTL_0(
         .primitive_restart =
               primitive_restart_enabled && indexed,
         .provoking_vtx_last = pipeline->provoking_vertex_last,
         .tess_upper_left_domain_origin =
               pipeline->tess.upper_left_domain_origin));

   /* Early exit if there is nothing to emit, saves CPU cycles */
   if (!(cmd->state.dirty & ~TU_CMD_DIRTY_COMPUTE_DESC_SETS_LOAD))
      return VK_SUCCESS;

   bool dirty_lrz =
      cmd->state.dirty & (TU_CMD_DIRTY_LRZ | TU_CMD_DIRTY_RB_DEPTH_CNTL |
                          TU_CMD_DIRTY_RB_STENCIL_CNTL | TU_CMD_DIRTY_BLEND);

   if (dirty_lrz) {
      struct tu_cs cs;
      uint32_t size = cmd->device->physical_device->info->a6xx.lrz_track_quirk ? 10 : 8;

      cmd->state.lrz_and_depth_plane_state =
         tu_cs_draw_state(&cmd->sub_cs, &cs, size);
      tu6_emit_lrz(cmd, &cs);
      tu6_build_depth_plane_z_mode(cmd, &cs);
   }

   if (cmd->state.dirty & TU_CMD_DIRTY_RASTERIZER_DISCARD) {
      struct tu_cs cs = tu_cmd_dynamic_state(cmd, TU_DYNAMIC_STATE_RASTERIZER_DISCARD, 4);
      tu_cs_emit_regs(&cs, A6XX_PC_RASTER_CNTL(.dword = cmd->state.pc_raster_cntl));
      tu_cs_emit_regs(&cs, A6XX_VPC_UNKNOWN_9107(.dword = cmd->state.vpc_unknown_9107));
   }

   if (cmd->state.dirty & TU_CMD_DIRTY_GRAS_SU_CNTL) {
      struct tu_cs cs = tu_cmd_dynamic_state(cmd, TU_DYNAMIC_STATE_GRAS_SU_CNTL, 2);
      tu_cs_emit_regs(&cs, A6XX_GRAS_SU_CNTL(.dword = cmd->state.gras_su_cntl));
   }

   if (cmd->state.dirty & TU_CMD_DIRTY_RB_DEPTH_CNTL) {
      struct tu_cs cs = tu_cmd_dynamic_state(cmd, TU_DYNAMIC_STATE_RB_DEPTH_CNTL, 2);
      uint32_t rb_depth_cntl = cmd->state.rb_depth_cntl;

      if ((rb_depth_cntl & A6XX_RB_DEPTH_CNTL_Z_TEST_ENABLE) ||
          (rb_depth_cntl & A6XX_RB_DEPTH_CNTL_Z_BOUNDS_ENABLE))
         rb_depth_cntl |= A6XX_RB_DEPTH_CNTL_Z_READ_ENABLE;

      if ((rb_depth_cntl & A6XX_RB_DEPTH_CNTL_Z_BOUNDS_ENABLE) &&
          !(rb_depth_cntl & A6XX_RB_DEPTH_CNTL_Z_TEST_ENABLE))
         tu6_apply_depth_bounds_workaround(cmd->device, &rb_depth_cntl);

      if (pipeline->rb_depth_cntl_disable)
         rb_depth_cntl = 0;

      tu_cs_emit_regs(&cs, A6XX_RB_DEPTH_CNTL(.dword = rb_depth_cntl));
   }

   if (cmd->state.dirty & TU_CMD_DIRTY_RB_STENCIL_CNTL) {
      struct tu_cs cs = tu_cmd_dynamic_state(cmd, TU_DYNAMIC_STATE_RB_STENCIL_CNTL, 2);
      tu_cs_emit_regs(&cs, A6XX_RB_STENCIL_CONTROL(.dword = cmd->state.rb_stencil_cntl));
   }

   if (cmd->state.dirty & TU_CMD_DIRTY_SHADER_CONSTS)
      cmd->state.shader_const = tu6_emit_consts(cmd, pipeline, false);

   if (cmd->state.dirty & TU_CMD_DIRTY_VIEWPORTS) {
      struct tu_cs cs = tu_cmd_dynamic_state(cmd, VK_DYNAMIC_STATE_VIEWPORT, 8 + 10 * cmd->state.max_viewport);
      tu6_emit_viewport(&cs, cmd->state.viewport, cmd->state.max_viewport,
                        pipeline->z_negative_one_to_one);
   }

   if (cmd->state.dirty & TU_CMD_DIRTY_BLEND) {
      struct tu_cs cs = tu_cmd_dynamic_state(cmd, TU_DYNAMIC_STATE_BLEND,
                                             4 + 3 * cmd->state.pipeline->num_rts);
      tu6_emit_blend(&cs, cmd);
   }

   /* for the first draw in a renderpass, re-emit all the draw states
    *
    * and if a draw-state disabling path (CmdClearAttachments 3D fallback) was
    * used, then draw states must be re-emitted. note however this only happens
    * in the sysmem path, so this can be skipped this for the gmem path (TODO)
    *
    * the two input attachment states are excluded because secondary command
    * buffer doesn't have a state ib to restore it, and not re-emitting them
    * is OK since CmdClearAttachments won't disable/overwrite them
    */
   if (cmd->state.dirty & TU_CMD_DIRTY_DRAW_STATE) {
      tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3 * (TU_DRAW_STATE_COUNT - 2));

      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_PROGRAM_CONFIG, pipeline->program.config_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_PROGRAM, pipeline->program.state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_PROGRAM_BINNING, pipeline->program.binning_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_RAST, pipeline->rast_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_PRIM_MODE_SYSMEM, pipeline->prim_order_state_sysmem);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_PRIM_MODE_GMEM, pipeline->prim_order_state_gmem);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_CONST, cmd->state.shader_const);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DESC_SETS, cmd->state.desc_sets);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DESC_SETS_LOAD, pipeline->load_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VB, cmd->state.vertex_buffers);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VS_PARAMS, cmd->state.vs_params);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_LRZ_AND_DEPTH_PLANE, cmd->state.lrz_and_depth_plane_state);

      for (uint32_t i = 0; i < ARRAY_SIZE(cmd->state.dynamic_state); i++) {
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DYNAMIC + i,
                               ((pipeline->dynamic_state_mask & BIT(i)) ?
                                cmd->state.dynamic_state[i] :
                                pipeline->dynamic_state[i]));
      }
   } else {
      /* emit draw states that were just updated
       * note we eventually don't want to have to emit anything here
       */
      bool emit_binding_stride = false, emit_blend = false;
      uint32_t draw_state_count =
         ((cmd->state.dirty & TU_CMD_DIRTY_SHADER_CONSTS) ? 1 : 0) +
         ((cmd->state.dirty & TU_CMD_DIRTY_DESC_SETS_LOAD) ? 1 : 0) +
         ((cmd->state.dirty & TU_CMD_DIRTY_VERTEX_BUFFERS) ? 1 : 0) +
         ((cmd->state.dirty & TU_CMD_DIRTY_VS_PARAMS) ? 1 : 0) +
         (dirty_lrz ? 1 : 0);

      if ((cmd->state.dirty & TU_CMD_DIRTY_VB_STRIDE) &&
          (pipeline->dynamic_state_mask & BIT(TU_DYNAMIC_STATE_VB_STRIDE))) {
         emit_binding_stride = true;
         draw_state_count += 1;
      }

      if ((cmd->state.dirty & TU_CMD_DIRTY_BLEND) &&
          (pipeline->dynamic_state_mask & BIT(TU_DYNAMIC_STATE_BLEND))) {
         emit_blend = true;
         draw_state_count += 1;
      }

      if (draw_state_count > 0)
         tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3 * draw_state_count);

      if (cmd->state.dirty & TU_CMD_DIRTY_SHADER_CONSTS)
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_CONST, cmd->state.shader_const);
      if (cmd->state.dirty & TU_CMD_DIRTY_DESC_SETS_LOAD)
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DESC_SETS_LOAD, pipeline->load_state);
      if (cmd->state.dirty & TU_CMD_DIRTY_VERTEX_BUFFERS)
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VB, cmd->state.vertex_buffers);
      if (emit_binding_stride) {
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DYNAMIC + TU_DYNAMIC_STATE_VB_STRIDE,
                               cmd->state.dynamic_state[TU_DYNAMIC_STATE_VB_STRIDE]);
      }
      if (emit_blend) {
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DYNAMIC + TU_DYNAMIC_STATE_BLEND,
                               cmd->state.dynamic_state[TU_DYNAMIC_STATE_BLEND]);
      }
      if (cmd->state.dirty & TU_CMD_DIRTY_VS_PARAMS)
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VS_PARAMS, cmd->state.vs_params);

      if (dirty_lrz) {
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_LRZ_AND_DEPTH_PLANE, cmd->state.lrz_and_depth_plane_state);
      }
   }

   tu_cs_sanity_check(cs);

   /* There are too many graphics dirty bits to list here, so just list the
    * bits to preserve instead. The only things not emitted here are
    * compute-related state.
    */
   cmd->state.dirty &= TU_CMD_DIRTY_COMPUTE_DESC_SETS_LOAD;
   return VK_SUCCESS;
}

static uint32_t
tu_draw_initiator(struct tu_cmd_buffer *cmd, enum pc_di_src_sel src_sel)
{
   const struct tu_pipeline *pipeline = cmd->state.pipeline;
   enum pc_di_primtype primtype = pipeline->ia.primtype;

   if (pipeline->dynamic_state_mask & BIT(TU_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY)) {
      if (primtype < DI_PT_PATCHES0) {
         /* If tesselation used, only VK_PRIMITIVE_TOPOLOGY_PATCH_LIST can be
          * set via vkCmdSetPrimitiveTopology, but primtype is already
          * calculated at the pipeline creation based on control points
          * for each patch.
          *
          * Just use the primtype as is for the case.
          */
         primtype = cmd->state.primtype;
      }
   }

   uint32_t initiator =
      CP_DRAW_INDX_OFFSET_0_PRIM_TYPE(primtype) |
      CP_DRAW_INDX_OFFSET_0_SOURCE_SELECT(src_sel) |
      CP_DRAW_INDX_OFFSET_0_INDEX_SIZE(cmd->state.index_size) |
      CP_DRAW_INDX_OFFSET_0_VIS_CULL(USE_VISIBILITY);

   if (pipeline->active_stages & VK_SHADER_STAGE_GEOMETRY_BIT)
      initiator |= CP_DRAW_INDX_OFFSET_0_GS_ENABLE;

   switch (pipeline->tess.patch_type) {
   case IR3_TESS_TRIANGLES:
      initiator |= CP_DRAW_INDX_OFFSET_0_PATCH_TYPE(TESS_TRIANGLES) |
                   CP_DRAW_INDX_OFFSET_0_TESS_ENABLE;
      break;
   case IR3_TESS_ISOLINES:
      initiator |= CP_DRAW_INDX_OFFSET_0_PATCH_TYPE(TESS_ISOLINES) |
                   CP_DRAW_INDX_OFFSET_0_TESS_ENABLE;
      break;
   case IR3_TESS_NONE:
      initiator |= CP_DRAW_INDX_OFFSET_0_PATCH_TYPE(TESS_QUADS);
      break;
   case IR3_TESS_QUADS:
      initiator |= CP_DRAW_INDX_OFFSET_0_PATCH_TYPE(TESS_QUADS) |
                   CP_DRAW_INDX_OFFSET_0_TESS_ENABLE;
      break;
   }
   return initiator;
}


static uint32_t
vs_params_offset(struct tu_cmd_buffer *cmd)
{
   const struct tu_program_descriptor_linkage *link =
      &cmd->state.pipeline->program.link[MESA_SHADER_VERTEX];
   const struct ir3_const_state *const_state = &link->const_state;

   if (const_state->offsets.driver_param >= link->constlen)
      return 0;

   /* this layout is required by CP_DRAW_INDIRECT_MULTI */
   STATIC_ASSERT(IR3_DP_DRAWID == 0);
   STATIC_ASSERT(IR3_DP_VTXID_BASE == 1);
   STATIC_ASSERT(IR3_DP_INSTID_BASE == 2);

   /* 0 means disabled for CP_DRAW_INDIRECT_MULTI */
   assert(const_state->offsets.driver_param != 0);

   return const_state->offsets.driver_param;
}

static void
tu6_emit_empty_vs_params(struct tu_cmd_buffer *cmd)
{
   if (cmd->state.vs_params.iova) {
      cmd->state.vs_params = (struct tu_draw_state) {};
      cmd->state.dirty |= TU_CMD_DIRTY_VS_PARAMS;
   }
}

static void
tu6_emit_vs_params(struct tu_cmd_buffer *cmd,
                   uint32_t draw_id,
                   uint32_t vertex_offset,
                   uint32_t first_instance)
{
   uint32_t offset = vs_params_offset(cmd);

   /* Beside re-emitting params when they are changed, we should re-emit
    * them after constants are invalidated via HLSQ_INVALIDATE_CMD.
    */
   if (!(cmd->state.dirty & (TU_CMD_DIRTY_DRAW_STATE | TU_CMD_DIRTY_VS_PARAMS)) &&
       (offset == 0 || draw_id == cmd->state.last_vs_params.draw_id) &&
       vertex_offset == cmd->state.last_vs_params.vertex_offset &&
       first_instance == cmd->state.last_vs_params.first_instance) {
      return;
   }

   struct tu_cs cs;
   VkResult result = tu_cs_begin_sub_stream(&cmd->sub_cs, 3 + (offset ? 8 : 0), &cs);
   if (result != VK_SUCCESS) {
      vk_command_buffer_set_error(&cmd->vk, result);
      return;
   }

   tu_cs_emit_regs(&cs,
                   A6XX_VFD_INDEX_OFFSET(vertex_offset),
                   A6XX_VFD_INSTANCE_START_OFFSET(first_instance));

   if (offset) {
      tu_cs_emit_pkt7(&cs, CP_LOAD_STATE6_GEOM, 3 + 4);
      tu_cs_emit(&cs, CP_LOAD_STATE6_0_DST_OFF(offset) |
            CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
            CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
            CP_LOAD_STATE6_0_STATE_BLOCK(SB6_VS_SHADER) |
            CP_LOAD_STATE6_0_NUM_UNIT(1));
      tu_cs_emit(&cs, 0);
      tu_cs_emit(&cs, 0);

      tu_cs_emit(&cs, draw_id);
      tu_cs_emit(&cs, vertex_offset);
      tu_cs_emit(&cs, first_instance);
      tu_cs_emit(&cs, 0);
   }

   cmd->state.last_vs_params.vertex_offset = vertex_offset;
   cmd->state.last_vs_params.first_instance = first_instance;
   cmd->state.last_vs_params.draw_id = draw_id;

   struct tu_cs_entry entry = tu_cs_end_sub_stream(&cmd->sub_cs, &cs);
   cmd->state.vs_params = (struct tu_draw_state) {entry.bo->iova + entry.offset, entry.size / 4};

   cmd->state.dirty |= TU_CMD_DIRTY_VS_PARAMS;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdDraw(VkCommandBuffer commandBuffer,
           uint32_t vertexCount,
           uint32_t instanceCount,
           uint32_t firstVertex,
           uint32_t firstInstance)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   tu6_emit_vs_params(cmd, 0, firstVertex, firstInstance);

   tu6_draw_common(cmd, cs, false, vertexCount);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDX_OFFSET, 3);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_AUTO_INDEX));
   tu_cs_emit(cs, instanceCount);
   tu_cs_emit(cs, vertexCount);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdDrawMultiEXT(VkCommandBuffer commandBuffer,
                   uint32_t drawCount,
                   const VkMultiDrawInfoEXT *pVertexInfo,
                   uint32_t instanceCount,
                   uint32_t firstInstance,
                   uint32_t stride)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   if (!drawCount)
      return;

   bool has_tess =
         cmd->state.pipeline->active_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;

   uint32_t max_vertex_count = 0;
   if (has_tess) {
      uint32_t i = 0;
      vk_foreach_multi_draw(draw, i, pVertexInfo, drawCount, stride) {
         max_vertex_count = MAX2(max_vertex_count, draw->vertexCount);
      }
   }

   uint32_t i = 0;
   vk_foreach_multi_draw(draw, i, pVertexInfo, drawCount, stride) {
      tu6_emit_vs_params(cmd, i, draw->firstVertex, firstInstance);

      if (i == 0)
         tu6_draw_common(cmd, cs, false, max_vertex_count);

      if (cmd->state.dirty & TU_CMD_DIRTY_VS_PARAMS) {
         tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3);
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VS_PARAMS, cmd->state.vs_params);
         cmd->state.dirty &= ~TU_CMD_DIRTY_VS_PARAMS;
      }

      tu_cs_emit_pkt7(cs, CP_DRAW_INDX_OFFSET, 3);
      tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_AUTO_INDEX));
      tu_cs_emit(cs, instanceCount);
      tu_cs_emit(cs, draw->vertexCount);
   }
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdDrawIndexed(VkCommandBuffer commandBuffer,
                  uint32_t indexCount,
                  uint32_t instanceCount,
                  uint32_t firstIndex,
                  int32_t vertexOffset,
                  uint32_t firstInstance)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   tu6_emit_vs_params(cmd, 0, vertexOffset, firstInstance);

   tu6_draw_common(cmd, cs, true, indexCount);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDX_OFFSET, 7);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_DMA));
   tu_cs_emit(cs, instanceCount);
   tu_cs_emit(cs, indexCount);
   tu_cs_emit(cs, firstIndex);
   tu_cs_emit_qw(cs, cmd->state.index_va);
   tu_cs_emit(cs, cmd->state.max_index_count);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdDrawMultiIndexedEXT(VkCommandBuffer commandBuffer,
                          uint32_t drawCount,
                          const VkMultiDrawIndexedInfoEXT *pIndexInfo,
                          uint32_t instanceCount,
                          uint32_t firstInstance,
                          uint32_t stride,
                          const int32_t *pVertexOffset)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   if (!drawCount)
      return;

   bool has_tess =
         cmd->state.pipeline->active_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;

   uint32_t max_index_count = 0;
   if (has_tess) {
      uint32_t i = 0;
      vk_foreach_multi_draw_indexed(draw, i, pIndexInfo, drawCount, stride) {
         max_index_count = MAX2(max_index_count, draw->indexCount);
      }
   }

   uint32_t i = 0;
   vk_foreach_multi_draw_indexed(draw, i, pIndexInfo, drawCount, stride) {
      int32_t vertexOffset = pVertexOffset ? *pVertexOffset : draw->vertexOffset;
      tu6_emit_vs_params(cmd, i, vertexOffset, firstInstance);

      if (i == 0)
         tu6_draw_common(cmd, cs, true, max_index_count);

      if (cmd->state.dirty & TU_CMD_DIRTY_VS_PARAMS) {
         tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3);
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VS_PARAMS, cmd->state.vs_params);
         cmd->state.dirty &= ~TU_CMD_DIRTY_VS_PARAMS;
      }

      tu_cs_emit_pkt7(cs, CP_DRAW_INDX_OFFSET, 7);
      tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_DMA));
      tu_cs_emit(cs, instanceCount);
      tu_cs_emit(cs, draw->indexCount);
      tu_cs_emit(cs, draw->firstIndex);
      tu_cs_emit_qw(cs, cmd->state.index_va);
      tu_cs_emit(cs, cmd->state.max_index_count);
   }
}

/* Various firmware bugs/inconsistencies mean that some indirect draw opcodes
 * do not wait for WFI's to complete before executing. Add a WAIT_FOR_ME if
 * pending for these opcodes. This may result in a few extra WAIT_FOR_ME's
 * with these opcodes, but the alternative would add unnecessary WAIT_FOR_ME's
 * before draw opcodes that don't need it.
 */
static void
draw_wfm(struct tu_cmd_buffer *cmd)
{
   cmd->state.renderpass_cache.flush_bits |=
      cmd->state.renderpass_cache.pending_flush_bits & TU_CMD_FLAG_WAIT_FOR_ME;
   cmd->state.renderpass_cache.pending_flush_bits &= ~TU_CMD_FLAG_WAIT_FOR_ME;
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdDrawIndirect(VkCommandBuffer commandBuffer,
                   VkBuffer _buffer,
                   VkDeviceSize offset,
                   uint32_t drawCount,
                   uint32_t stride)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, buf, _buffer);
   struct tu_cs *cs = &cmd->draw_cs;

   tu6_emit_empty_vs_params(cmd);

   if (cmd->device->physical_device->info->a6xx.indirect_draw_wfm_quirk)
      draw_wfm(cmd);

   tu6_draw_common(cmd, cs, false, 0);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDIRECT_MULTI, 6);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_AUTO_INDEX));
   tu_cs_emit(cs, A6XX_CP_DRAW_INDIRECT_MULTI_1_OPCODE(INDIRECT_OP_NORMAL) |
                  A6XX_CP_DRAW_INDIRECT_MULTI_1_DST_OFF(vs_params_offset(cmd)));
   tu_cs_emit(cs, drawCount);
   tu_cs_emit_qw(cs, buf->iova + offset);
   tu_cs_emit(cs, stride);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer,
                          VkBuffer _buffer,
                          VkDeviceSize offset,
                          uint32_t drawCount,
                          uint32_t stride)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, buf, _buffer);
   struct tu_cs *cs = &cmd->draw_cs;

   tu6_emit_empty_vs_params(cmd);

   if (cmd->device->physical_device->info->a6xx.indirect_draw_wfm_quirk)
      draw_wfm(cmd);

   tu6_draw_common(cmd, cs, true, 0);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDIRECT_MULTI, 9);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_DMA));
   tu_cs_emit(cs, A6XX_CP_DRAW_INDIRECT_MULTI_1_OPCODE(INDIRECT_OP_INDEXED) |
                  A6XX_CP_DRAW_INDIRECT_MULTI_1_DST_OFF(vs_params_offset(cmd)));
   tu_cs_emit(cs, drawCount);
   tu_cs_emit_qw(cs, cmd->state.index_va);
   tu_cs_emit(cs, cmd->state.max_index_count);
   tu_cs_emit_qw(cs, buf->iova + offset);
   tu_cs_emit(cs, stride);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdDrawIndirectCount(VkCommandBuffer commandBuffer,
                        VkBuffer _buffer,
                        VkDeviceSize offset,
                        VkBuffer countBuffer,
                        VkDeviceSize countBufferOffset,
                        uint32_t drawCount,
                        uint32_t stride)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, buf, _buffer);
   TU_FROM_HANDLE(tu_buffer, count_buf, countBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   tu6_emit_empty_vs_params(cmd);

   /* It turns out that the firmware we have for a650 only partially fixed the
    * problem with CP_DRAW_INDIRECT_MULTI not waiting for WFI's to complete
    * before reading indirect parameters. It waits for WFI's before reading
    * the draw parameters, but after reading the indirect count :(.
    */
   draw_wfm(cmd);

   tu6_draw_common(cmd, cs, false, 0);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDIRECT_MULTI, 8);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_AUTO_INDEX));
   tu_cs_emit(cs, A6XX_CP_DRAW_INDIRECT_MULTI_1_OPCODE(INDIRECT_OP_INDIRECT_COUNT) |
                  A6XX_CP_DRAW_INDIRECT_MULTI_1_DST_OFF(vs_params_offset(cmd)));
   tu_cs_emit(cs, drawCount);
   tu_cs_emit_qw(cs, buf->iova + offset);
   tu_cs_emit_qw(cs, count_buf->iova + countBufferOffset);
   tu_cs_emit(cs, stride);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer,
                               VkBuffer _buffer,
                               VkDeviceSize offset,
                               VkBuffer countBuffer,
                               VkDeviceSize countBufferOffset,
                               uint32_t drawCount,
                               uint32_t stride)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, buf, _buffer);
   TU_FROM_HANDLE(tu_buffer, count_buf, countBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   tu6_emit_empty_vs_params(cmd);

   draw_wfm(cmd);

   tu6_draw_common(cmd, cs, true, 0);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDIRECT_MULTI, 11);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_DMA));
   tu_cs_emit(cs, A6XX_CP_DRAW_INDIRECT_MULTI_1_OPCODE(INDIRECT_OP_INDIRECT_COUNT_INDEXED) |
                  A6XX_CP_DRAW_INDIRECT_MULTI_1_DST_OFF(vs_params_offset(cmd)));
   tu_cs_emit(cs, drawCount);
   tu_cs_emit_qw(cs, cmd->state.index_va);
   tu_cs_emit(cs, cmd->state.max_index_count);
   tu_cs_emit_qw(cs, buf->iova + offset);
   tu_cs_emit_qw(cs, count_buf->iova + countBufferOffset);
   tu_cs_emit(cs, stride);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdDrawIndirectByteCountEXT(VkCommandBuffer commandBuffer,
                               uint32_t instanceCount,
                               uint32_t firstInstance,
                               VkBuffer _counterBuffer,
                               VkDeviceSize counterBufferOffset,
                               uint32_t counterOffset,
                               uint32_t vertexStride)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, buf, _counterBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   /* All known firmware versions do not wait for WFI's with CP_DRAW_AUTO.
    * Plus, for the common case where the counter buffer is written by
    * vkCmdEndTransformFeedback, we need to wait for the CP_WAIT_MEM_WRITES to
    * complete which means we need a WAIT_FOR_ME anyway.
    */
   draw_wfm(cmd);

   tu6_emit_vs_params(cmd, 0, 0, firstInstance);

   tu6_draw_common(cmd, cs, false, 0);

   tu_cs_emit_pkt7(cs, CP_DRAW_AUTO, 6);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_AUTO_XFB));
   tu_cs_emit(cs, instanceCount);
   tu_cs_emit_qw(cs, buf->iova + counterBufferOffset);
   tu_cs_emit(cs, counterOffset);
   tu_cs_emit(cs, vertexStride);
}

struct tu_dispatch_info
{
   /**
    * Determine the layout of the grid (in block units) to be used.
    */
   uint32_t blocks[3];

   /**
    * A starting offset for the grid. If unaligned is set, the offset
    * must still be aligned.
    */
   uint32_t offsets[3];
   /**
    * Whether it's an unaligned compute dispatch.
    */
   bool unaligned;

   /**
    * Indirect compute parameters resource.
    */
   struct tu_buffer *indirect;
   uint64_t indirect_offset;
};

static void
tu_emit_compute_driver_params(struct tu_cmd_buffer *cmd,
                              struct tu_cs *cs, struct tu_pipeline *pipeline,
                              const struct tu_dispatch_info *info)
{
   gl_shader_stage type = MESA_SHADER_COMPUTE;
   const struct tu_program_descriptor_linkage *link =
      &pipeline->program.link[type];
   const struct ir3_const_state *const_state = &link->const_state;
   uint32_t offset = const_state->offsets.driver_param;
   unsigned subgroup_size = pipeline->compute.subgroup_size;
   unsigned subgroup_shift = util_logbase2(subgroup_size);

   if (link->constlen <= offset)
      return;

   uint32_t num_consts = MIN2(const_state->num_driver_params,
                              (link->constlen - offset) * 4);

   if (!info->indirect) {
      uint32_t driver_params[12] = {
         [IR3_DP_NUM_WORK_GROUPS_X] = info->blocks[0],
         [IR3_DP_NUM_WORK_GROUPS_Y] = info->blocks[1],
         [IR3_DP_NUM_WORK_GROUPS_Z] = info->blocks[2],
         [IR3_DP_BASE_GROUP_X] = info->offsets[0],
         [IR3_DP_BASE_GROUP_Y] = info->offsets[1],
         [IR3_DP_BASE_GROUP_Z] = info->offsets[2],
         [IR3_DP_CS_SUBGROUP_SIZE] = subgroup_size,
         [IR3_DP_SUBGROUP_ID_SHIFT] = subgroup_shift,
      };

      assert(num_consts <= ARRAY_SIZE(driver_params));

      /* push constants */
      tu_cs_emit_pkt7(cs, tu6_stage2opcode(type), 3 + num_consts);
      tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(offset) |
                 CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                 CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
                 CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(type)) |
                 CP_LOAD_STATE6_0_NUM_UNIT(num_consts / 4));
      tu_cs_emit(cs, 0);
      tu_cs_emit(cs, 0);
      uint32_t i;
      for (i = 0; i < num_consts; i++)
         tu_cs_emit(cs, driver_params[i]);
   } else if (!(info->indirect_offset & 0xf)) {
      tu_cs_emit_pkt7(cs, tu6_stage2opcode(type), 3);
      tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(offset) |
                  CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                  CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                  CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(type)) |
                  CP_LOAD_STATE6_0_NUM_UNIT(1));
      tu_cs_emit_qw(cs, info->indirect->iova + info->indirect_offset);
   } else {
      /* Vulkan guarantees only 4 byte alignment for indirect_offset.
       * However, CP_LOAD_STATE.EXT_SRC_ADDR needs 16 byte alignment.
       */

      uint64_t indirect_iova = info->indirect->iova + info->indirect_offset;

      for (uint32_t i = 0; i < 3; i++) {
         tu_cs_emit_pkt7(cs, CP_MEM_TO_MEM, 5);
         tu_cs_emit(cs, 0);
         tu_cs_emit_qw(cs, global_iova(cmd, cs_indirect_xyz[i]));
         tu_cs_emit_qw(cs, indirect_iova + i * 4);
      }

      tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);
      tu6_emit_event_write(cmd, cs, CACHE_INVALIDATE);

      tu_cs_emit_pkt7(cs, tu6_stage2opcode(type), 3);
      tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(offset) |
                  CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                  CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                  CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(type)) |
                  CP_LOAD_STATE6_0_NUM_UNIT(1));
      tu_cs_emit_qw(cs, global_iova(cmd, cs_indirect_xyz[0]));
   }

   /* Fill out IR3_DP_CS_SUBGROUP_SIZE and IR3_DP_SUBGROUP_ID_SHIFT for
    * indirect dispatch.
    */
   if (info->indirect && num_consts > IR3_DP_BASE_GROUP_X) {
      tu_cs_emit_pkt7(cs, tu6_stage2opcode(type), 7);
      tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(offset + (IR3_DP_BASE_GROUP_X / 4)) |
                 CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                 CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
                 CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(type)) |
                 CP_LOAD_STATE6_0_NUM_UNIT((num_consts - IR3_DP_BASE_GROUP_X) / 4));
      tu_cs_emit_qw(cs, 0);
      tu_cs_emit(cs, 0); /* BASE_GROUP_X */
      tu_cs_emit(cs, 0); /* BASE_GROUP_Y */
      tu_cs_emit(cs, 0); /* BASE_GROUP_Z */
      tu_cs_emit(cs, subgroup_size);
      if (num_consts > IR3_DP_LOCAL_GROUP_SIZE_X) {
         assert(num_consts == align(IR3_DP_SUBGROUP_ID_SHIFT, 4));
         tu_cs_emit(cs, 0); /* LOCAL_GROUP_SIZE_X */
         tu_cs_emit(cs, 0); /* LOCAL_GROUP_SIZE_Y */
         tu_cs_emit(cs, 0); /* LOCAL_GROUP_SIZE_Z */
         tu_cs_emit(cs, subgroup_shift);
      }
   }
}

static void
tu_dispatch(struct tu_cmd_buffer *cmd,
            const struct tu_dispatch_info *info)
{
   if (!info->indirect &&
       (info->blocks[0] == 0 || info->blocks[1] == 0 || info->blocks[2] == 0))
      return;

   struct tu_cs *cs = &cmd->cs;
   struct tu_pipeline *pipeline = cmd->state.compute_pipeline;

   /* TODO: We could probably flush less if we add a compute_flush_bits
    * bitfield.
    */
   tu_emit_cache_flush(cmd, cs);

   /* note: no reason to have this in a separate IB */
   tu_cs_emit_state_ib(cs, tu6_emit_consts(cmd, pipeline, true));

   tu_emit_compute_driver_params(cmd, cs, pipeline, info);

   if (cmd->state.dirty & TU_CMD_DIRTY_COMPUTE_DESC_SETS_LOAD)
      tu_cs_emit_state_ib(cs, pipeline->load_state);

   cmd->state.dirty &= ~TU_CMD_DIRTY_COMPUTE_DESC_SETS_LOAD;

   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_COMPUTE));

   const uint32_t *local_size = pipeline->compute.local_size;
   const uint32_t *num_groups = info->blocks;
   tu_cs_emit_regs(cs,
                   A6XX_HLSQ_CS_NDRANGE_0(.kerneldim = 3,
                                          .localsizex = local_size[0] - 1,
                                          .localsizey = local_size[1] - 1,
                                          .localsizez = local_size[2] - 1),
                   A6XX_HLSQ_CS_NDRANGE_1(.globalsize_x = local_size[0] * num_groups[0]),
                   A6XX_HLSQ_CS_NDRANGE_2(.globaloff_x = 0),
                   A6XX_HLSQ_CS_NDRANGE_3(.globalsize_y = local_size[1] * num_groups[1]),
                   A6XX_HLSQ_CS_NDRANGE_4(.globaloff_y = 0),
                   A6XX_HLSQ_CS_NDRANGE_5(.globalsize_z = local_size[2] * num_groups[2]),
                   A6XX_HLSQ_CS_NDRANGE_6(.globaloff_z = 0));

   tu_cs_emit_regs(cs,
                   A6XX_HLSQ_CS_KERNEL_GROUP_X(1),
                   A6XX_HLSQ_CS_KERNEL_GROUP_Y(1),
                   A6XX_HLSQ_CS_KERNEL_GROUP_Z(1));

   trace_start_compute(&cmd->trace, cs);

   if (info->indirect) {
      uint64_t iova = info->indirect->iova + info->indirect_offset;

      tu_cs_emit_pkt7(cs, CP_EXEC_CS_INDIRECT, 4);
      tu_cs_emit(cs, 0x00000000);
      tu_cs_emit_qw(cs, iova);
      tu_cs_emit(cs,
                 A5XX_CP_EXEC_CS_INDIRECT_3_LOCALSIZEX(local_size[0] - 1) |
                 A5XX_CP_EXEC_CS_INDIRECT_3_LOCALSIZEY(local_size[1] - 1) |
                 A5XX_CP_EXEC_CS_INDIRECT_3_LOCALSIZEZ(local_size[2] - 1));
   } else {
      tu_cs_emit_pkt7(cs, CP_EXEC_CS, 4);
      tu_cs_emit(cs, 0x00000000);
      tu_cs_emit(cs, CP_EXEC_CS_1_NGROUPS_X(info->blocks[0]));
      tu_cs_emit(cs, CP_EXEC_CS_2_NGROUPS_Y(info->blocks[1]));
      tu_cs_emit(cs, CP_EXEC_CS_3_NGROUPS_Z(info->blocks[2]));
   }

   trace_end_compute(&cmd->trace, cs,
                     info->indirect != NULL,
                     local_size[0], local_size[1], local_size[2],
                     info->blocks[0], info->blocks[1], info->blocks[2]);

   tu_cs_emit_wfi(cs);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdDispatchBase(VkCommandBuffer commandBuffer,
                   uint32_t base_x,
                   uint32_t base_y,
                   uint32_t base_z,
                   uint32_t x,
                   uint32_t y,
                   uint32_t z)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   struct tu_dispatch_info info = {};

   info.blocks[0] = x;
   info.blocks[1] = y;
   info.blocks[2] = z;

   info.offsets[0] = base_x;
   info.offsets[1] = base_y;
   info.offsets[2] = base_z;
   tu_dispatch(cmd_buffer, &info);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdDispatch(VkCommandBuffer commandBuffer,
               uint32_t x,
               uint32_t y,
               uint32_t z)
{
   tu_CmdDispatchBase(commandBuffer, 0, 0, 0, x, y, z);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdDispatchIndirect(VkCommandBuffer commandBuffer,
                       VkBuffer _buffer,
                       VkDeviceSize offset)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, buffer, _buffer);
   struct tu_dispatch_info info = {};

   info.indirect = buffer;
   info.indirect_offset = offset;

   tu_dispatch(cmd_buffer, &info);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdEndRenderPass2(VkCommandBuffer commandBuffer,
                     const VkSubpassEndInfo *pSubpassEndInfo)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);

   if (unlikely(cmd_buffer->device->instance->debug_flags & TU_DEBUG_DYNAMIC)) {
      vk_common_CmdEndRenderPass2(commandBuffer, pSubpassEndInfo);
      return;
   }

   tu_cs_end(&cmd_buffer->draw_cs);
   tu_cs_end(&cmd_buffer->draw_epilogue_cs);
   tu_cmd_render(cmd_buffer);

   cmd_buffer->state.cache.pending_flush_bits |=
      cmd_buffer->state.renderpass_cache.pending_flush_bits;
   tu_subpass_barrier(cmd_buffer, &cmd_buffer->state.pass->end_barrier, true);

   vk_free(&cmd_buffer->vk.pool->alloc, cmd_buffer->state.attachments);

   tu_reset_render_pass(cmd_buffer);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdEndRendering(VkCommandBuffer commandBuffer)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);

   if (cmd_buffer->state.suspending)
      cmd_buffer->state.suspended_pass.lrz = cmd_buffer->state.lrz;

   if (!cmd_buffer->state.suspending) {
      tu_cs_end(&cmd_buffer->draw_cs);
      tu_cs_end(&cmd_buffer->draw_epilogue_cs);

      if (cmd_buffer->state.suspend_resume == SR_IN_PRE_CHAIN) {
         cmd_buffer->trace_renderpass_end = u_trace_end_iterator(&cmd_buffer->trace);
         tu_save_pre_chain(cmd_buffer);
      } else {
         tu_cmd_render(cmd_buffer);
      }

      tu_reset_render_pass(cmd_buffer);
   }

   if (cmd_buffer->state.resuming && !cmd_buffer->state.suspending) {
      /* exiting suspend/resume chain */
      switch (cmd_buffer->state.suspend_resume) {
      case SR_IN_CHAIN:
         cmd_buffer->state.suspend_resume = SR_NONE;
         break;
      case SR_IN_PRE_CHAIN:
      case SR_IN_CHAIN_AFTER_PRE_CHAIN:
         cmd_buffer->state.suspend_resume = SR_AFTER_PRE_CHAIN;
         break;
      default:
         unreachable("suspending render pass not followed by resuming pass");
      }
   }
}

static void
tu_barrier(struct tu_cmd_buffer *cmd,
           const VkDependencyInfo *dep_info)
{
   VkPipelineStageFlags2 srcStage = 0;
   VkPipelineStageFlags2 dstStage = 0;
   enum tu_cmd_access_mask src_flags = 0;
   enum tu_cmd_access_mask dst_flags = 0;

   /* Inside a renderpass, we don't know yet whether we'll be using sysmem
    * so we have to use the sysmem flushes.
    */
   bool gmem = cmd->state.ccu_state == TU_CMD_CCU_GMEM &&
      !cmd->state.pass;


   for (uint32_t i = 0; i < dep_info->memoryBarrierCount; i++) {
      VkPipelineStageFlags2 sanitized_src_stage =
         sanitize_src_stage(dep_info->pMemoryBarriers[i].srcStageMask);
      VkPipelineStageFlags2 sanitized_dst_stage =
         sanitize_dst_stage(dep_info->pMemoryBarriers[i].dstStageMask);
      src_flags |= vk2tu_access(dep_info->pMemoryBarriers[i].srcAccessMask,
                                sanitized_src_stage, false, gmem);
      dst_flags |= vk2tu_access(dep_info->pMemoryBarriers[i].dstAccessMask,
                                sanitized_dst_stage, false, gmem);
      srcStage |= sanitized_src_stage;
      dstStage |= sanitized_dst_stage;
   }

   for (uint32_t i = 0; i < dep_info->bufferMemoryBarrierCount; i++) {
      VkPipelineStageFlags2 sanitized_src_stage =
         sanitize_src_stage(dep_info->pBufferMemoryBarriers[i].srcStageMask);
      VkPipelineStageFlags2 sanitized_dst_stage =
         sanitize_dst_stage(dep_info->pBufferMemoryBarriers[i].dstStageMask);
      src_flags |= vk2tu_access(dep_info->pBufferMemoryBarriers[i].srcAccessMask,
                                sanitized_src_stage, false, gmem);
      dst_flags |= vk2tu_access(dep_info->pBufferMemoryBarriers[i].dstAccessMask,
                                sanitized_dst_stage, false, gmem);
      srcStage |= sanitized_src_stage;
      dstStage |= sanitized_dst_stage;
   }

   for (uint32_t i = 0; i < dep_info->imageMemoryBarrierCount; i++) {
      VkImageLayout old_layout = dep_info->pImageMemoryBarriers[i].oldLayout;
      if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
         /* The underlying memory for this image may have been used earlier
          * within the same queue submission for a different image, which
          * means that there may be old, stale cache entries which are in the
          * "wrong" location, which could cause problems later after writing
          * to the image. We don't want these entries being flushed later and
          * overwriting the actual image, so we need to flush the CCU.
          */
         TU_FROM_HANDLE(tu_image, image, dep_info->pImageMemoryBarriers[i].image);

         if (vk_format_is_depth_or_stencil(image->vk.format)) {
            src_flags |= TU_ACCESS_CCU_DEPTH_INCOHERENT_WRITE;
         } else {
            src_flags |= TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE;
         }
      }
      VkPipelineStageFlags2 sanitized_src_stage =
         sanitize_src_stage(dep_info->pImageMemoryBarriers[i].srcStageMask);
      VkPipelineStageFlags2 sanitized_dst_stage =
         sanitize_dst_stage(dep_info->pImageMemoryBarriers[i].dstStageMask);
      src_flags |= vk2tu_access(dep_info->pImageMemoryBarriers[i].srcAccessMask,
                                sanitized_src_stage, true, gmem);
      dst_flags |= vk2tu_access(dep_info->pImageMemoryBarriers[i].dstAccessMask,
                                sanitized_dst_stage, true, gmem);
      srcStage |= sanitized_src_stage;
      dstStage |= sanitized_dst_stage;
   }

   if (cmd->state.pass) {
      const VkPipelineStageFlags framebuffer_space_stages =
         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
         VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
         VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
         VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

      /* We cannot have non-by-region "fb-space to fb-space" barriers.
       *
       * From the Vulkan 1.2.185 spec, section 7.6.1 "Subpass Self-dependency":
       *
       *    If the source and destination stage masks both include
       *    framebuffer-space stages, then dependencyFlags must include
       *    VK_DEPENDENCY_BY_REGION_BIT.
       *    [...]
       *    Each of the synchronization scopes and access scopes of a
       *    vkCmdPipelineBarrier2 or vkCmdPipelineBarrier command inside
       *    a render pass instance must be a subset of the scopes of one of
       *    the self-dependencies for the current subpass.
       *
       *    If the self-dependency has VK_DEPENDENCY_BY_REGION_BIT or
       *    VK_DEPENDENCY_VIEW_LOCAL_BIT set, then so must the pipeline barrier.
       *
       * By-region barriers are ok for gmem. All other barriers would involve
       * vtx stages which are NOT ok for gmem rendering.
       * See dep_invalid_for_gmem().
       */
      if ((srcStage & ~framebuffer_space_stages) ||
          (dstStage & ~framebuffer_space_stages)) {
         cmd->state.rp.disable_gmem = true;
      }
   }

   struct tu_cache_state *cache =
      cmd->state.pass  ? &cmd->state.renderpass_cache : &cmd->state.cache;
   tu_flush_for_access(cache, src_flags, dst_flags);

   enum tu_stage src_stage = vk2tu_src_stage(srcStage);
   enum tu_stage dst_stage = vk2tu_dst_stage(dstStage);
   tu_flush_for_stage(cache, src_stage, dst_stage);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdPipelineBarrier2(VkCommandBuffer commandBuffer,
                       const VkDependencyInfo *pDependencyInfo)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);

   tu_barrier(cmd_buffer, pDependencyInfo);
}

static void
write_event(struct tu_cmd_buffer *cmd, struct tu_event *event,
            VkPipelineStageFlags2 stageMask, unsigned value)
{
   struct tu_cs *cs = &cmd->cs;

   /* vkCmdSetEvent/vkCmdResetEvent cannot be called inside a render pass */
   assert(!cmd->state.pass);

   tu_emit_cache_flush(cmd, cs);

   /* Flags that only require a top-of-pipe event. DrawIndirect parameters are
    * read by the CP, so the draw indirect stage counts as top-of-pipe too.
    */
   VkPipelineStageFlags2 top_of_pipe_flags =
      VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT |
      VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;

   if (!(stageMask & ~top_of_pipe_flags)) {
      tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 3);
      tu_cs_emit_qw(cs, event->bo->iova); /* ADDR_LO/HI */
      tu_cs_emit(cs, value);
   } else {
      /* Use a RB_DONE_TS event to wait for everything to complete. */
      tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 4);
      tu_cs_emit(cs, CP_EVENT_WRITE_0_EVENT(RB_DONE_TS));
      tu_cs_emit_qw(cs, event->bo->iova);
      tu_cs_emit(cs, value);
   }
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetEvent2(VkCommandBuffer commandBuffer,
                VkEvent _event,
                const VkDependencyInfo *pDependencyInfo)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_event, event, _event);
   VkPipelineStageFlags2 src_stage_mask = 0;

   for (uint32_t i = 0; i < pDependencyInfo->memoryBarrierCount; i++)
      src_stage_mask |= pDependencyInfo->pMemoryBarriers[i].srcStageMask;
   for (uint32_t i = 0; i < pDependencyInfo->bufferMemoryBarrierCount; i++)
      src_stage_mask |= pDependencyInfo->pBufferMemoryBarriers[i].srcStageMask;
   for (uint32_t i = 0; i < pDependencyInfo->imageMemoryBarrierCount; i++)
      src_stage_mask |= pDependencyInfo->pImageMemoryBarriers[i].srcStageMask;

   write_event(cmd, event, src_stage_mask, 1);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdResetEvent2(VkCommandBuffer commandBuffer,
                  VkEvent _event,
                  VkPipelineStageFlags2 stageMask)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_event, event, _event);

   write_event(cmd, event, stageMask, 0);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdWaitEvents2(VkCommandBuffer commandBuffer,
                  uint32_t eventCount,
                  const VkEvent *pEvents,
                  const VkDependencyInfo* pDependencyInfos)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = cmd->state.pass ? &cmd->draw_cs : &cmd->cs;

   for (uint32_t i = 0; i < eventCount; i++) {
      TU_FROM_HANDLE(tu_event, event, pEvents[i]);

      tu_cs_emit_pkt7(cs, CP_WAIT_REG_MEM, 6);
      tu_cs_emit(cs, CP_WAIT_REG_MEM_0_FUNCTION(WRITE_EQ) |
                     CP_WAIT_REG_MEM_0_POLL_MEMORY);
      tu_cs_emit_qw(cs, event->bo->iova); /* POLL_ADDR_LO/HI */
      tu_cs_emit(cs, CP_WAIT_REG_MEM_3_REF(1));
      tu_cs_emit(cs, CP_WAIT_REG_MEM_4_MASK(~0u));
      tu_cs_emit(cs, CP_WAIT_REG_MEM_5_DELAY_LOOP_CYCLES(20));
   }

   tu_barrier(cmd, pDependencyInfos);
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask)
{
   /* No-op */
}


VKAPI_ATTR void VKAPI_CALL
tu_CmdBeginConditionalRenderingEXT(VkCommandBuffer commandBuffer,
                                   const VkConditionalRenderingBeginInfoEXT *pConditionalRenderingBegin)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.predication_active = true;

   struct tu_cs *cs = cmd->state.pass ? &cmd->draw_cs : &cmd->cs;

   tu_cs_emit_pkt7(cs, CP_DRAW_PRED_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 1);

   /* Wait for any writes to the predicate to land */
   if (cmd->state.pass)
      tu_emit_cache_flush_renderpass(cmd, cs);
   else
      tu_emit_cache_flush(cmd, cs);

   TU_FROM_HANDLE(tu_buffer, buf, pConditionalRenderingBegin->buffer);
   uint64_t iova = buf->iova + pConditionalRenderingBegin->offset;

   /* qcom doesn't support 32-bit reference values, only 64-bit, but Vulkan
    * mandates 32-bit comparisons. Our workaround is to copy the the reference
    * value to the low 32-bits of a location where the high 32 bits are known
    * to be 0 and then compare that.
    */
   tu_cs_emit_pkt7(cs, CP_MEM_TO_MEM, 5);
   tu_cs_emit(cs, 0);
   tu_cs_emit_qw(cs, global_iova(cmd, predicate));
   tu_cs_emit_qw(cs, iova);

   tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);
   tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);

   bool inv = pConditionalRenderingBegin->flags & VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT;
   tu_cs_emit_pkt7(cs, CP_DRAW_PRED_SET, 3);
   tu_cs_emit(cs, CP_DRAW_PRED_SET_0_SRC(PRED_SRC_MEM) |
                  CP_DRAW_PRED_SET_0_TEST(inv ? EQ_0_PASS : NE_0_PASS));
   tu_cs_emit_qw(cs, global_iova(cmd, predicate));
}

VKAPI_ATTR void VKAPI_CALL
tu_CmdEndConditionalRenderingEXT(VkCommandBuffer commandBuffer)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.predication_active = false;

   struct tu_cs *cs = cmd->state.pass ? &cmd->draw_cs : &cmd->cs;

   tu_cs_emit_pkt7(cs, CP_DRAW_PRED_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0);
}

void
tu_CmdWriteBufferMarker2AMD(VkCommandBuffer commandBuffer,
                            VkPipelineStageFlagBits2 pipelineStage,
                            VkBuffer dstBuffer,
                            VkDeviceSize dstOffset,
                            uint32_t marker)
{
   /* Almost the same as write_event, but also allowed in renderpass */
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, buffer, dstBuffer);

   uint64_t va = buffer->bo->iova + dstOffset;

   struct tu_cs *cs = cmd->state.pass ? &cmd->draw_cs : &cmd->cs;
   struct tu_cache_state *cache =
      cmd->state.pass ? &cmd->state.renderpass_cache : &cmd->state.cache;

   /* From the Vulkan 1.2.203 spec:
    *
    *    The access scope for buffer marker writes falls under
    *    the VK_ACCESS_TRANSFER_WRITE_BIT, and the pipeline stages for
    *    identifying the synchronization scope must include both pipelineStage
    *    and VK_PIPELINE_STAGE_TRANSFER_BIT.
    *
    * Transfer operations use CCU however here we write via CP.
    * Flush CCU in order to make the results of previous transfer
    * operation visible to CP.
    */
   tu_flush_for_access(cache, 0, TU_ACCESS_SYSMEM_WRITE);

   /* Flags that only require a top-of-pipe event. DrawIndirect parameters are
    * read by the CP, so the draw indirect stage counts as top-of-pipe too.
    */
   VkPipelineStageFlags2 top_of_pipe_flags =
      VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT |
      VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT;

   bool is_top_of_pipe = !(pipelineStage & ~top_of_pipe_flags);

   /* We have to WFI only if we flushed CCU here and are using CP_MEM_WRITE.
    * Otherwise:
    * - We do CP_EVENT_WRITE(RB_DONE_TS) which should wait for flushes;
    * - There was a barrier to synchronize other writes with WriteBufferMarkerAMD
    *   and they had to include our pipelineStage which forces the WFI.
    */
   if (cache->flush_bits != 0 && is_top_of_pipe) {
      cache->flush_bits |= TU_CMD_FLAG_WAIT_FOR_IDLE;
   }

   if (cmd->state.pass) {
      tu_emit_cache_flush_renderpass(cmd, cs);
   } else {
      tu_emit_cache_flush(cmd, cs);
   }

   if (is_top_of_pipe) {
      tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 3);
      tu_cs_emit_qw(cs, va); /* ADDR_LO/HI */
      tu_cs_emit(cs, marker);
   } else {
      /* Use a RB_DONE_TS event to wait for everything to complete. */
      tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 4);
      tu_cs_emit(cs, CP_EVENT_WRITE_0_EVENT(RB_DONE_TS));
      tu_cs_emit_qw(cs, va);
      tu_cs_emit(cs, marker);
   }

   /* Make sure the result of this write is visible to others. */
   tu_flush_for_access(cache, TU_ACCESS_CP_WRITE, 0);
}
