/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "tu_private.h"

#include "adreno_pm4.xml.h"
#include "adreno_common.xml.h"

#include "vk_format.h"
#include "vk_util.h"

#include "tu_cs.h"

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

static void
tu6_emit_flushes(struct tu_cmd_buffer *cmd_buffer,
                 struct tu_cs *cs,
                 enum tu_cmd_flush_bits flushes)
{
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
   if (flushes & TU_CMD_FLAG_WAIT_FOR_IDLE)
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
                      A6XX_RB_CCU_CNTL(.offset =
                                          ccu_state == TU_CMD_CCU_GMEM ?
                                          phys_dev->info.a6xx.ccu_offset_gmem :
                                          phys_dev->info.a6xx.ccu_offset_bypass,
                                       .gmem = ccu_state == TU_CMD_CCU_GMEM));
      cmd_buffer->state.ccu_state = ccu_state;
   }
}

static void
tu6_emit_zs(struct tu_cmd_buffer *cmd,
            const struct tu_subpass *subpass,
            struct tu_cs *cs)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;

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

      tu_cs_emit_regs(cs,
                      A6XX_GRAS_LRZ_BUFFER_BASE(0),
                      A6XX_GRAS_LRZ_BUFFER_PITCH(0),
                      A6XX_GRAS_LRZ_FAST_CLEAR_BUFFER_BASE(0));

      tu_cs_emit_regs(cs, A6XX_RB_STENCIL_INFO(0));

      return;
   }

   const struct tu_image_view *iview = fb->attachments[a].attachment;
   const struct tu_render_pass_attachment *attachment =
      &cmd->state.pass->attachments[a];
   enum a6xx_depth_format fmt = tu6_pipe2depth(attachment->format);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_DEPTH_BUFFER_INFO, 6);
   tu_cs_emit(cs, A6XX_RB_DEPTH_BUFFER_INFO(.depth_format = fmt).value);
   tu_cs_image_ref(cs, iview, 0);
   tu_cs_emit(cs, attachment->gmem_offset);

   tu_cs_emit_regs(cs,
                   A6XX_GRAS_SU_DEPTH_BUFFER_INFO(.depth_format = fmt));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_DEPTH_FLAG_BUFFER_BASE, 3);
   tu_cs_image_flag_ref(cs, iview, 0);

   tu_cs_emit_regs(cs, A6XX_GRAS_LRZ_BUFFER_BASE(.bo = iview->image->bo,
                                                 .bo_offset = iview->image->bo_offset + iview->image->lrz_offset),
                   A6XX_GRAS_LRZ_BUFFER_PITCH(.pitch = iview->image->lrz_pitch),
                   A6XX_GRAS_LRZ_FAST_CLEAR_BUFFER_BASE());

   if (attachment->format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
       attachment->format == VK_FORMAT_S8_UINT) {

      tu_cs_emit_pkt4(cs, REG_A6XX_RB_STENCIL_INFO, 6);
      tu_cs_emit(cs, A6XX_RB_STENCIL_INFO(.separate_stencil = true).value);
      if (attachment->format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
         tu_cs_image_stencil_ref(cs, iview, 0);
         tu_cs_emit(cs, attachment->gmem_offset_stencil);
      } else {
         tu_cs_image_ref(cs, iview, 0);
         tu_cs_emit(cs, attachment->gmem_offset);
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

   for (uint32_t i = 0; i < subpass->color_count; ++i) {
      uint32_t a = subpass->color_attachments[i].attachment;
      if (a == VK_ATTACHMENT_UNUSED)
         continue;

      const struct tu_image_view *iview = fb->attachments[a].attachment;

      tu_cs_emit_pkt4(cs, REG_A6XX_RB_MRT_BUF_INFO(i), 6);
      tu_cs_emit(cs, iview->RB_MRT_BUF_INFO);
      tu_cs_image_ref(cs, iview, 0);
      tu_cs_emit(cs, cmd->state.pass->attachments[a].gmem_offset);

      tu_cs_emit_regs(cs,
                      A6XX_SP_FS_MRT_REG(i, .dword = iview->SP_FS_MRT_REG));

      tu_cs_emit_pkt4(cs, REG_A6XX_RB_MRT_FLAG_BUFFER_ADDR(i), 3);
      tu_cs_image_flag_ref(cs, iview, 0);
   }

   tu_cs_emit_regs(cs,
                   A6XX_RB_SRGB_CNTL(.dword = subpass->srgb_cntl));
   tu_cs_emit_regs(cs,
                   A6XX_SP_SRGB_CNTL(.dword = subpass->srgb_cntl));

   unsigned layers = MAX2(fb->layers, util_logbase2(subpass->multiview_mask) + 1);
   tu_cs_emit_regs(cs, A6XX_GRAS_MAX_LAYER_INDEX(layers - 1));
}

void
tu6_emit_msaa(struct tu_cs *cs, VkSampleCountFlagBits vk_samples)
{
   const enum a3xx_msaa_samples samples = tu_msaa_samples(vk_samples);
   bool msaa_disable = samples == MSAA_ONE;

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
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   uint32_t cntl = 0;
   cntl |= A6XX_RB_RENDER_CNTL_UNK4;
   if (binning) {
      cntl |= A6XX_RB_RENDER_CNTL_BINNING;
   } else {
      uint32_t mrts_ubwc_enable = 0;
      for (uint32_t i = 0; i < subpass->color_count; ++i) {
         uint32_t a = subpass->color_attachments[i].attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;

         const struct tu_image_view *iview = fb->attachments[a].attachment;
         if (iview->ubwc_enabled)
            mrts_ubwc_enable |= 1 << i;
      }

      cntl |= A6XX_RB_RENDER_CNTL_FLAG_MRTS(mrts_ubwc_enable);

      const uint32_t a = subpass->depth_stencil_attachment.attachment;
      if (a != VK_ATTACHMENT_UNUSED) {
         const struct tu_image_view *iview = fb->attachments[a].attachment;
         if (iview->ubwc_enabled)
            cntl |= A6XX_RB_RENDER_CNTL_FLAG_DEPTH;
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
      x1 = x1 & ~(phys_dev->info.gmem_align_w - 1);
      y1 = y1 & ~(phys_dev->info.gmem_align_h - 1);
      x2 = ALIGN_POT(x2 + 1, phys_dev->info.gmem_align_w) - 1;
      y2 = ALIGN_POT(y2 + 1, phys_dev->info.gmem_align_h) - 1;
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

static void
tu_cs_emit_draw_state(struct tu_cs *cs, uint32_t id, struct tu_draw_state state)
{
   uint32_t enable_mask;
   switch (id) {
   case TU_DRAW_STATE_PROGRAM:
   case TU_DRAW_STATE_VI:
   case TU_DRAW_STATE_FS_CONST:
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
   case TU_DRAW_STATE_VI_BINNING:
      enable_mask = CP_SET_DRAW_STATE__0_BINNING;
      break;
   case TU_DRAW_STATE_INPUT_ATTACHMENTS_GMEM:
      enable_mask = CP_SET_DRAW_STATE__0_GMEM;
      break;
   case TU_DRAW_STATE_INPUT_ATTACHMENTS_SYSMEM:
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
}

static bool
use_hw_binning(struct tu_cmd_buffer *cmd)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;

   /* XFB commands are emitted for BINNING || SYSMEM, which makes it incompatible
    * with non-hw binning GMEM rendering. this is required because some of the
    * XFB commands need to only be executed once
    */
   if (cmd->state.xfb_used)
      return true;

   /* Some devices have a newer a630_sqe.fw in which, only in CP_DRAW_INDX and
    * CP_DRAW_INDX_OFFSET, visibility-based skipping happens *before*
    * predication-based skipping. It seems this breaks predication, because
    * draws skipped by predication will not be executed in the binning phase,
    * and therefore won't have an entry in the draw stream, but the
    * visibility-based skipping will expect it to have an entry. The result is
    * a GPU hang when actually executing the first non-predicated draw.
    * However, it seems that things still work if the whole renderpass is
    * predicated. Affected tests are
    * dEQP-VK.conditional_rendering.draw_clear.draw.case_2 as well as a few
    * other case_N.
    *
    * Broken FW version: 016ee181
    * linux-firmware (working) FW version: 016ee176
    *
    * All known a650_sqe.fw versions don't have this bug.
    *
    * TODO: we should do version detection of the FW so that devices using the
    * linux-firmware version of a630_sqe.fw don't need this workaround.
    */
   if (cmd->state.has_subpass_predication && cmd->device->physical_device->gpu_id != 650)
      return false;

   if (unlikely(cmd->device->physical_device->instance->debug_flags & TU_DEBUG_NOBIN))
      return false;

   if (unlikely(cmd->device->physical_device->instance->debug_flags & TU_DEBUG_FORCEBIN))
      return true;

   return (fb->tile_count.width * fb->tile_count.height) > 2;
}

static bool
use_sysmem_rendering(struct tu_cmd_buffer *cmd)
{
   if (unlikely(cmd->device->physical_device->instance->debug_flags & TU_DEBUG_SYSMEM))
      return true;

   /* If hw binning is required because of XFB but doesn't work because of the
    * conditional rendering bug, fallback to sysmem.
    */
   if (cmd->state.xfb_used && cmd->state.has_subpass_predication &&
       cmd->device->physical_device->gpu_id != 650)
      return true;

   /* can't fit attachments into gmem */
   if (!cmd->state.pass->gmem_pixels)
      return true;

   if (cmd->state.framebuffer->layers > 1)
      return true;

   /* Use sysmem for empty render areas */
   if (cmd->state.render_area.extent.width == 0 ||
       cmd->state.render_area.extent.height == 0)
      return true;

   if (cmd->state.has_tess)
      return true;

   return false;
}

static void
tu6_emit_tile_select(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     uint32_t tx, uint32_t ty, uint32_t pipe, uint32_t slot)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;

   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_GMEM));

   const uint32_t x1 = fb->tile0.width * tx;
   const uint32_t y1 = fb->tile0.height * ty;
   const uint32_t x2 = x1 + fb->tile0.width - 1;
   const uint32_t y2 = y1 + fb->tile0.height - 1;
   tu6_emit_window_scissor(cs, x1, y1, x2, y2);
   tu6_emit_window_offset(cs, x1, y1);

   tu_cs_emit_regs(cs, A6XX_VPC_SO_DISABLE(false));

   if (use_hw_binning(cmd)) {
      tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);

      tu_cs_emit_pkt7(cs, CP_SET_MODE, 1);
      tu_cs_emit(cs, 0x0);

      tu_cs_emit_pkt7(cs, CP_SET_BIN_DATA5_OFFSET, 4);
      tu_cs_emit(cs, fb->pipe_sizes[pipe] |
                     CP_SET_BIN_DATA5_0_VSC_N(slot));
      tu_cs_emit(cs, pipe * cmd->vsc_draw_strm_pitch);
      tu_cs_emit(cs, pipe * 4);
      tu_cs_emit(cs, pipe * cmd->vsc_prim_strm_pitch);

      tu_cs_emit_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1);
      tu_cs_emit(cs, 0x0);

      tu_cs_emit_pkt7(cs, CP_SET_MODE, 1);
      tu_cs_emit(cs, 0x0);
   } else {
      tu_cs_emit_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1);
      tu_cs_emit(cs, 0x1);

      tu_cs_emit_pkt7(cs, CP_SET_MODE, 1);
      tu_cs_emit(cs, 0x0);
   }
}

static void
tu6_emit_sysmem_resolve(struct tu_cmd_buffer *cmd,
                        struct tu_cs *cs,
                        uint32_t layer_mask,
                        uint32_t a,
                        uint32_t gmem_a)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   struct tu_image_view *dst = fb->attachments[a].attachment;
   struct tu_image_view *src = fb->attachments[gmem_a].attachment;

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
tu6_emit_tile_store(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   const struct tu_render_pass *pass = cmd->state.pass;
   const struct tu_subpass *subpass = &pass->subpasses[pass->subpass_count-1];

   tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0x0);

   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_RESOLVE));

   tu6_emit_blit_scissor(cmd, cs, true);

   for (uint32_t a = 0; a < pass->attachment_count; ++a) {
      if (pass->attachments[a].gmem_offset >= 0)
         tu_store_gmem_attachment(cmd, cs, a, a);
   }

   if (subpass->resolve_attachments) {
      for (unsigned i = 0; i < subpass->resolve_count; i++) {
         uint32_t a = subpass->resolve_attachments[i].attachment;
         if (a != VK_ATTACHMENT_UNUSED) {
            uint32_t gmem_a = tu_subpass_get_attachment_to_resolve(subpass, i);
            tu_store_gmem_attachment(cmd, cs, a, gmem_a);
         }
      }
   }
}

static void
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
                   A6XX_RB_CCU_CNTL(.offset = phys_dev->info.a6xx.ccu_offset_bypass));
   cmd->state.ccu_state = TU_CMD_CCU_SYSMEM;
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8E04, 0x00100000);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_AE04, 0x8);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_AE00, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_PERFCTR_ENABLE, 0x3f);
   tu_cs_emit_write_reg(cs, REG_A6XX_TPL1_UNKNOWN_B605, 0x44);
   tu_cs_emit_write_reg(cs, REG_A6XX_TPL1_UNKNOWN_B600, 0x100000);
   tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_UNKNOWN_BE00, 0x80);
   tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_UNKNOWN_BE01, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9600, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_UNKNOWN_8600, 0x880);
   tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_UNKNOWN_BE04, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_AE03, 0x00000410);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_IBO_COUNT, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_B182, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_SHARED_CONSTS, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_UCHE_UNKNOWN_0E12, 0x3200000);
   tu_cs_emit_write_reg(cs, REG_A6XX_UCHE_CLIENT_PF, 4);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8E01, 0x0);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_A9A8, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_MODE_CONTROL,
                        A6XX_SP_MODE_CONTROL_CONSTANT_DEMOTION_ENABLE | 4);

   /* TODO: set A6XX_VFD_ADD_OFFSET_INSTANCE and fix ir3 to avoid adding base instance */
   tu_cs_emit_write_reg(cs, REG_A6XX_VFD_ADD_OFFSET, A6XX_VFD_ADD_OFFSET_VERTEX);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8811, 0x00000010);
   tu_cs_emit_write_reg(cs, REG_A6XX_PC_MODE_CNTL, 0x1f);

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

   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_UNKNOWN_8099, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_UNKNOWN_80A0, 2);
   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_UNKNOWN_80AF, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9210, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9211, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9602, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_PC_UNKNOWN_9E72, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_TP_UNKNOWN_B309, 0x000000a2);
   tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_CONTROL_5_REG, 0xfc);

   tu_cs_emit_write_reg(cs, REG_A6XX_VFD_MODE_CNTL, 0x00000000);

   tu_cs_emit_write_reg(cs, REG_A6XX_PC_MODE_CNTL, 0x0000001f);

   tu_cs_emit_regs(cs, A6XX_RB_ALPHA_CONTROL()); /* always disable alpha test */
   tu_cs_emit_regs(cs, A6XX_RB_DITHER_CNTL()); /* always disable dithering */

   tu_disable_draw_states(cmd, cs);

   tu_cs_emit_regs(cs,
                   A6XX_SP_TP_BORDER_COLOR_BASE_ADDR(.bo = &dev->global_bo,
                                                     .bo_offset = gb_offset(bcolor_builtin)));
   tu_cs_emit_regs(cs,
                   A6XX_SP_PS_TP_BORDER_COLOR_BASE_ADDR(.bo = &dev->global_bo,
                                                        .bo_offset = gb_offset(bcolor_builtin)));

   /* VSC buffers:
    * use vsc pitches from the largest values used so far with this device
    * if there hasn't been overflow, there will already be a scratch bo
    * allocated for these sizes
    *
    * if overflow is detected, the stream size is increased by 2x
    */
   mtx_lock(&dev->mutex);

   struct tu6_global *global = dev->global_bo.map;

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
   const struct tu_framebuffer *fb = cmd->state.framebuffer;

   tu_cs_emit_regs(cs,
                   A6XX_VSC_BIN_SIZE(.width = fb->tile0.width,
                                     .height = fb->tile0.height));

   tu_cs_emit_regs(cs,
                   A6XX_VSC_BIN_COUNT(.nx = fb->tile_count.width,
                                      .ny = fb->tile_count.height));

   tu_cs_emit_pkt4(cs, REG_A6XX_VSC_PIPE_CONFIG_REG(0), 32);
   tu_cs_emit_array(cs, fb->pipe_config, 32);

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
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   const uint32_t used_pipe_count =
      fb->pipe_count.width * fb->pipe_count.height;

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
                   A6XX_VFD_MODE_CNTL(.binning_pass = true));

   update_vsc_pipe(cmd, cs);

   tu_cs_emit_regs(cs,
                   A6XX_PC_UNKNOWN_9805(.unknown = phys_dev->info.a6xx.magic.PC_UNKNOWN_9805));

   tu_cs_emit_regs(cs,
                   A6XX_SP_UNKNOWN_A0F8(.unknown = phys_dev->info.a6xx.magic.SP_UNKNOWN_A0F8));

   tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 1);
   tu_cs_emit(cs, UNK_2C);

   tu_cs_emit_regs(cs,
                   A6XX_RB_WINDOW_OFFSET(.x = 0, .y = 0));

   tu_cs_emit_regs(cs,
                   A6XX_SP_TP_WINDOW_OFFSET(.x = 0, .y = 0));

   /* emit IB to binning drawcmds: */
   tu_cs_emit_call(cs, &cmd->draw_cs);

   /* switching from binning pass to GMEM pass will cause a switch from
    * PROGRAM_BINNING to PROGRAM, which invalidates const state (XS_CONST states)
    * so make sure these states are re-emitted
    * (eventually these states shouldn't exist at all with shader prologue)
    * only VS and GS are invalidated, as FS isn't emitted in binning pass,
    * and we don't use HW binning when tesselation is used
    */
   tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 6);
   tu_cs_emit(cs, CP_SET_DRAW_STATE__0_COUNT(0) |
                  CP_SET_DRAW_STATE__0_DISABLE |
                  CP_SET_DRAW_STATE__0_GROUP_ID(TU_DRAW_STATE_VS_CONST));
   tu_cs_emit(cs, CP_SET_DRAW_STATE__1_ADDR_LO(0));
   tu_cs_emit(cs, CP_SET_DRAW_STATE__2_ADDR_HI(0));
   tu_cs_emit(cs, CP_SET_DRAW_STATE__0_COUNT(0) |
                  CP_SET_DRAW_STATE__0_DISABLE |
                  CP_SET_DRAW_STATE__0_GROUP_ID(TU_DRAW_STATE_GS_CONST));
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
      cmd->record_result = result;
      return (struct tu_draw_state) {};
   }

   for (unsigned i = 0; i < subpass->input_count * 2; i++) {
      uint32_t a = subpass->input_attachments[i / 2].attachment;
      if (a == VK_ATTACHMENT_UNUSED)
         continue;

      struct tu_image_view *iview =
         cmd->state.framebuffer->attachments[a].attachment;
      const struct tu_render_pass_attachment *att =
         &cmd->state.pass->attachments[a];
      uint32_t *dst = &texture.map[A6XX_TEX_CONST_DWORDS * i];
      uint32_t gmem_offset = att->gmem_offset;
      uint32_t cpp = att->cpp;

      memcpy(dst, iview->descriptor, A6XX_TEX_CONST_DWORDS * 4);

      if (i % 2 == 1 && att->format == VK_FORMAT_D24_UNORM_S8_UINT) {
         /* note this works because spec says fb and input attachments
          * must use identity swizzle
          */
         dst[0] &= ~(A6XX_TEX_CONST_0_FMT__MASK |
            A6XX_TEX_CONST_0_SWIZ_X__MASK | A6XX_TEX_CONST_0_SWIZ_Y__MASK |
            A6XX_TEX_CONST_0_SWIZ_Z__MASK | A6XX_TEX_CONST_0_SWIZ_W__MASK);
         if (!cmd->device->physical_device->info.a6xx.has_z24uint_s8uint) {
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
         gmem_offset = att->gmem_offset_stencil;
      }

      if (!gmem)
         continue;

      /* patched for gmem */
      dst[0] &= ~(A6XX_TEX_CONST_0_SWAP__MASK | A6XX_TEX_CONST_0_TILE_MODE__MASK);
      dst[0] |= A6XX_TEX_CONST_0_TILE_MODE(TILE6_2);
      dst[2] =
         A6XX_TEX_CONST_2_TYPE(A6XX_TEX_2D) |
         A6XX_TEX_CONST_2_PITCH(cmd->state.framebuffer->tile0.width * cpp);
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
                         const VkRenderPassBeginInfo *info)
{
   struct tu_cs *cs = &cmd->draw_cs;

   tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_GMEM);

   tu6_emit_blit_scissor(cmd, cs, true);

   for (uint32_t i = 0; i < cmd->state.pass->attachment_count; ++i)
      tu_load_gmem_attachment(cmd, cs, i, false);

   tu6_emit_blit_scissor(cmd, cs, false);

   for (uint32_t i = 0; i < cmd->state.pass->attachment_count; ++i)
      tu_clear_gmem_attachment(cmd, cs, i, info);

   tu_cond_exec_end(cs);

   tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_SYSMEM);

   for (uint32_t i = 0; i < cmd->state.pass->attachment_count; ++i)
      tu_clear_sysmem_attachment(cmd, cs, i, info);

   tu_cond_exec_end(cs);
}

static void
tu6_sysmem_render_begin(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;

   assert(fb->width > 0 && fb->height > 0);
   tu6_emit_window_scissor(cs, 0, 0, fb->width - 1, fb->height - 1);
   tu6_emit_window_offset(cs, 0, 0);

   tu6_emit_bin_size(cs, 0, 0, 0xc00000); /* 0xc00000 = BYPASS? */

   tu6_emit_event_write(cmd, cs, LRZ_FLUSH);

   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_BYPASS));

   tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0x0);

   tu_emit_cache_flush_ccu(cmd, cs, TU_CMD_CCU_SYSMEM);

   /* enable stream-out, with sysmem there is only one pass: */
   tu_cs_emit_regs(cs, A6XX_VPC_SO_DISABLE(false));

   tu_cs_emit_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1);
   tu_cs_emit(cs, 0x1);

   tu_cs_emit_pkt7(cs, CP_SET_MODE, 1);
   tu_cs_emit(cs, 0x0);

   tu_cs_sanity_check(cs);
}

static void
tu6_sysmem_render_end(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   /* Do any resolves of the last subpass. These are handled in the
    * tile_store_ib in the gmem path.
    */
   tu6_emit_sysmem_resolves(cmd, cs, cmd->state.subpass);

   tu_cs_emit_call(cs, &cmd->draw_epilogue_cs);

   tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0x0);

   tu6_emit_event_write(cmd, cs, LRZ_FLUSH);

   tu_cs_sanity_check(cs);
}

static void
tu6_tile_render_begin(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   struct tu_physical_device *phys_dev = cmd->device->physical_device;

   tu6_emit_event_write(cmd, cs, LRZ_FLUSH);

   tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0x0);

   tu_emit_cache_flush_ccu(cmd, cs, TU_CMD_CCU_GMEM);

   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   if (use_hw_binning(cmd)) {
      /* enable stream-out during binning pass: */
      tu_cs_emit_regs(cs, A6XX_VPC_SO_DISABLE(false));

      tu6_emit_bin_size(cs, fb->tile0.width, fb->tile0.height,
                        A6XX_RB_BIN_CONTROL_BINNING_PASS | 0x6000000);

      tu6_emit_render_cntl(cmd, cmd->state.subpass, cs, true);

      tu6_emit_binning_pass(cmd, cs);

      /* and disable stream-out for draw pass: */
      tu_cs_emit_regs(cs, A6XX_VPC_SO_DISABLE(true));

      tu6_emit_bin_size(cs, fb->tile0.width, fb->tile0.height,
                        A6XX_RB_BIN_CONTROL_USE_VIZ | 0x6000000);

      tu_cs_emit_regs(cs,
                      A6XX_VFD_MODE_CNTL(0));

      tu_cs_emit_regs(cs, A6XX_PC_UNKNOWN_9805(.unknown = phys_dev->info.a6xx.magic.PC_UNKNOWN_9805));

      tu_cs_emit_regs(cs, A6XX_SP_UNKNOWN_A0F8(.unknown = phys_dev->info.a6xx.magic.SP_UNKNOWN_A0F8));

      tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
      tu_cs_emit(cs, 0x1);
   } else {
      /* no binning pass, so enable stream-out for draw pass:: */
      tu_cs_emit_regs(cs, A6XX_VPC_SO_DISABLE(false));

      tu6_emit_bin_size(cs, fb->tile0.width, fb->tile0.height, 0x6000000);
   }

   tu_cs_sanity_check(cs);
}

static void
tu6_render_tile(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   tu_cs_emit_call(cs, &cmd->draw_cs);

   if (use_hw_binning(cmd)) {
      tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
      tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_ENDVIS));
   }

   tu_cs_emit_ib(cs, &cmd->state.tile_store_ib);

   tu_cs_sanity_check(cs);
}

static void
tu6_tile_render_end(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   tu_cs_emit_call(cs, &cmd->draw_epilogue_cs);

   tu_cs_emit_regs(cs,
                   A6XX_GRAS_LRZ_CNTL(0));

   tu6_emit_event_write(cmd, cs, LRZ_FLUSH);

   tu6_emit_event_write(cmd, cs, PC_CCU_RESOLVE_TS);

   tu_cs_sanity_check(cs);
}

static void
tu_cmd_render_tiles(struct tu_cmd_buffer *cmd)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;

   tu6_tile_render_begin(cmd, &cmd->cs);

   uint32_t pipe = 0;
   for (uint32_t py = 0; py < fb->pipe_count.height; py++) {
      for (uint32_t px = 0; px < fb->pipe_count.width; px++, pipe++) {
         uint32_t tx1 = px * fb->pipe0.width;
         uint32_t ty1 = py * fb->pipe0.height;
         uint32_t tx2 = MIN2(tx1 + fb->pipe0.width, fb->tile_count.width);
         uint32_t ty2 = MIN2(ty1 + fb->pipe0.height, fb->tile_count.height);
         uint32_t slot = 0;
         for (uint32_t ty = ty1; ty < ty2; ty++) {
            for (uint32_t tx = tx1; tx < tx2; tx++, slot++) {
               tu6_emit_tile_select(cmd, &cmd->cs, tx, ty, pipe, slot);
               tu6_render_tile(cmd, &cmd->cs);
            }
         }
      }
   }

   tu6_tile_render_end(cmd, &cmd->cs);
}

static void
tu_cmd_render_sysmem(struct tu_cmd_buffer *cmd)
{
   tu6_sysmem_render_begin(cmd, &cmd->cs);

   tu_cs_emit_call(&cmd->cs, &cmd->draw_cs);

   tu6_sysmem_render_end(cmd, &cmd->cs);
}

static void
tu_cmd_prepare_tile_store_ib(struct tu_cmd_buffer *cmd)
{
   const uint32_t tile_store_space = 7 + (35 * 2) * cmd->state.pass->attachment_count;
   struct tu_cs sub_cs;

   VkResult result =
      tu_cs_begin_sub_stream(&cmd->sub_cs, tile_store_space, &sub_cs);
   if (result != VK_SUCCESS) {
      cmd->record_result = result;
      return;
   }

   /* emit to tile-store sub_cs */
   tu6_emit_tile_store(cmd, &sub_cs);

   cmd->state.tile_store_ib = tu_cs_end_sub_stream(&cmd->sub_cs, &sub_cs);
}

static VkResult
tu_create_cmd_buffer(struct tu_device *device,
                     struct tu_cmd_pool *pool,
                     VkCommandBufferLevel level,
                     VkCommandBuffer *pCommandBuffer)
{
   struct tu_cmd_buffer *cmd_buffer;

   cmd_buffer = vk_object_zalloc(&device->vk, NULL, sizeof(*cmd_buffer),
                                 VK_OBJECT_TYPE_COMMAND_BUFFER);
   if (cmd_buffer == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   cmd_buffer->device = device;
   cmd_buffer->pool = pool;
   cmd_buffer->level = level;

   if (pool) {
      list_addtail(&cmd_buffer->pool_link, &pool->cmd_buffers);
      cmd_buffer->queue_family_index = pool->queue_family_index;

   } else {
      /* Init the pool_link so we can safely call list_del when we destroy
       * the command buffer
       */
      list_inithead(&cmd_buffer->pool_link);
      cmd_buffer->queue_family_index = TU_QUEUE_GENERAL;
   }

   tu_cs_init(&cmd_buffer->cs, device, TU_CS_MODE_GROW, 4096);
   tu_cs_init(&cmd_buffer->draw_cs, device, TU_CS_MODE_GROW, 4096);
   tu_cs_init(&cmd_buffer->draw_epilogue_cs, device, TU_CS_MODE_GROW, 4096);
   tu_cs_init(&cmd_buffer->sub_cs, device, TU_CS_MODE_SUB_STREAM, 2048);

   *pCommandBuffer = tu_cmd_buffer_to_handle(cmd_buffer);

   return VK_SUCCESS;
}

static void
tu_cmd_buffer_destroy(struct tu_cmd_buffer *cmd_buffer)
{
   list_del(&cmd_buffer->pool_link);

   tu_cs_finish(&cmd_buffer->cs);
   tu_cs_finish(&cmd_buffer->draw_cs);
   tu_cs_finish(&cmd_buffer->draw_epilogue_cs);
   tu_cs_finish(&cmd_buffer->sub_cs);

   vk_object_free(&cmd_buffer->device->vk, &cmd_buffer->pool->alloc, cmd_buffer);
}

static VkResult
tu_reset_cmd_buffer(struct tu_cmd_buffer *cmd_buffer)
{
   cmd_buffer->record_result = VK_SUCCESS;

   tu_cs_reset(&cmd_buffer->cs);
   tu_cs_reset(&cmd_buffer->draw_cs);
   tu_cs_reset(&cmd_buffer->draw_epilogue_cs);
   tu_cs_reset(&cmd_buffer->sub_cs);

   for (unsigned i = 0; i < MAX_BIND_POINTS; i++)
      memset(&cmd_buffer->descriptors[i].sets, 0, sizeof(cmd_buffer->descriptors[i].sets));

   cmd_buffer->status = TU_CMD_BUFFER_STATUS_INITIAL;

   return cmd_buffer->record_result;
}

VkResult
tu_AllocateCommandBuffers(VkDevice _device,
                          const VkCommandBufferAllocateInfo *pAllocateInfo,
                          VkCommandBuffer *pCommandBuffers)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_cmd_pool, pool, pAllocateInfo->commandPool);

   VkResult result = VK_SUCCESS;
   uint32_t i;

   for (i = 0; i < pAllocateInfo->commandBufferCount; i++) {

      if (!list_is_empty(&pool->free_cmd_buffers)) {
         struct tu_cmd_buffer *cmd_buffer = list_first_entry(
            &pool->free_cmd_buffers, struct tu_cmd_buffer, pool_link);

         list_del(&cmd_buffer->pool_link);
         list_addtail(&cmd_buffer->pool_link, &pool->cmd_buffers);

         result = tu_reset_cmd_buffer(cmd_buffer);
         cmd_buffer->level = pAllocateInfo->level;
         vk_object_base_reset(&cmd_buffer->base);

         pCommandBuffers[i] = tu_cmd_buffer_to_handle(cmd_buffer);
      } else {
         result = tu_create_cmd_buffer(device, pool, pAllocateInfo->level,
                                       &pCommandBuffers[i]);
      }
      if (result != VK_SUCCESS)
         break;
   }

   if (result != VK_SUCCESS) {
      tu_FreeCommandBuffers(_device, pAllocateInfo->commandPool, i,
                            pCommandBuffers);

      /* From the Vulkan 1.0.66 spec:
       *
       * "vkAllocateCommandBuffers can be used to create multiple
       *  command buffers. If the creation of any of those command
       *  buffers fails, the implementation must destroy all
       *  successfully created command buffer objects from this
       *  command, set all entries of the pCommandBuffers array to
       *  NULL and return the error."
       */
      memset(pCommandBuffers, 0,
             sizeof(*pCommandBuffers) * pAllocateInfo->commandBufferCount);
   }

   return result;
}

void
tu_FreeCommandBuffers(VkDevice device,
                      VkCommandPool commandPool,
                      uint32_t commandBufferCount,
                      const VkCommandBuffer *pCommandBuffers)
{
   for (uint32_t i = 0; i < commandBufferCount; i++) {
      TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, pCommandBuffers[i]);

      if (cmd_buffer) {
         if (cmd_buffer->pool) {
            list_del(&cmd_buffer->pool_link);
            list_addtail(&cmd_buffer->pool_link,
                         &cmd_buffer->pool->free_cmd_buffers);
         } else
            tu_cmd_buffer_destroy(cmd_buffer);
      }
   }
}

VkResult
tu_ResetCommandBuffer(VkCommandBuffer commandBuffer,
                      VkCommandBufferResetFlags flags)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   return tu_reset_cmd_buffer(cmd_buffer);
}

/* Initialize the cache, assuming all necessary flushes have happened but *not*
 * invalidations.
 */
static void
tu_cache_init(struct tu_cache_state *cache)
{
   cache->flush_bits = 0;
   cache->pending_flush_bits = TU_CMD_FLAG_ALL_INVALIDATE;
}

VkResult
tu_BeginCommandBuffer(VkCommandBuffer commandBuffer,
                      const VkCommandBufferBeginInfo *pBeginInfo)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   VkResult result = VK_SUCCESS;

   if (cmd_buffer->status != TU_CMD_BUFFER_STATUS_INITIAL) {
      /* If the command buffer has already been resetted with
       * vkResetCommandBuffer, no need to do it again.
       */
      result = tu_reset_cmd_buffer(cmd_buffer);
      if (result != VK_SUCCESS)
         return result;
   }

   memset(&cmd_buffer->state, 0, sizeof(cmd_buffer->state));
   cmd_buffer->state.index_size = 0xff; /* dirty restart index */

   tu_cache_init(&cmd_buffer->state.cache);
   tu_cache_init(&cmd_buffer->state.renderpass_cache);
   cmd_buffer->usage_flags = pBeginInfo->flags;

   tu_cs_begin(&cmd_buffer->cs);
   tu_cs_begin(&cmd_buffer->draw_cs);
   tu_cs_begin(&cmd_buffer->draw_epilogue_cs);

   /* setup initial configuration into command buffer */
   if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
      switch (cmd_buffer->queue_family_index) {
      case TU_QUEUE_GENERAL:
         tu6_init_hw(cmd_buffer, &cmd_buffer->cs);
         break;
      default:
         break;
      }
   } else if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY) {
      assert(pBeginInfo->pInheritanceInfo);

      vk_foreach_struct(ext, pBeginInfo->pInheritanceInfo) {
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

      if (pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT) {
         cmd_buffer->state.pass = tu_render_pass_from_handle(pBeginInfo->pInheritanceInfo->renderPass);
         cmd_buffer->state.subpass =
            &cmd_buffer->state.pass->subpasses[pBeginInfo->pInheritanceInfo->subpass];
      } else {
         /* When executing in the middle of another command buffer, the CCU
          * state is unknown.
          */
         cmd_buffer->state.ccu_state = TU_CMD_CCU_UNKNOWN;
      }
   }

   cmd_buffer->status = TU_CMD_BUFFER_STATUS_RECORDING;

   return VK_SUCCESS;
}

void
tu_CmdBindVertexBuffers(VkCommandBuffer commandBuffer,
                        uint32_t firstBinding,
                        uint32_t bindingCount,
                        const VkBuffer *pBuffers,
                        const VkDeviceSize *pOffsets)
{
   tu_CmdBindVertexBuffers2EXT(commandBuffer, firstBinding, bindingCount,
                               pBuffers, pOffsets, NULL, NULL);
}

void
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
   /* TODO: track a "max_vb" value for the cmdbuf to save a bit of memory  */
   cmd->state.vertex_buffers.iova = tu_cs_draw_state(&cmd->sub_cs, &cs, 4 * MAX_VBS).iova;

   for (uint32_t i = 0; i < bindingCount; i++) {
      struct tu_buffer *buf = tu_buffer_from_handle(pBuffers[i]);

      cmd->state.vb[firstBinding + i].base = tu_buffer_iova(buf) + pOffsets[i];
      cmd->state.vb[firstBinding + i].size = pSizes ? pSizes[i] : (buf->size - pOffsets[i]);
      if (pStrides)
         cmd->state.vb[firstBinding + i].stride = pStrides[i];
   }

   for (uint32_t i = 0; i < MAX_VBS; i++) {
      tu_cs_emit_regs(&cs,
                      A6XX_VFD_FETCH_BASE(i, .qword = cmd->state.vb[i].base),
                      A6XX_VFD_FETCH_SIZE(i, cmd->state.vb[i].size));
   }

   cmd->state.dirty |= TU_CMD_DIRTY_VERTEX_BUFFERS;

   if (pStrides) {
      cmd->state.dynamic_state[TU_DYNAMIC_STATE_VB_STRIDE].iova =
         tu_cs_draw_state(&cmd->sub_cs, &cs, 2 * MAX_VBS).iova;

      for (uint32_t i = 0; i < MAX_VBS; i++)
         tu_cs_emit_regs(&cs, A6XX_VFD_FETCH_STRIDE(i, cmd->state.vb[i].stride));

      cmd->state.dirty |= TU_CMD_DIRTY_VB_STRIDE;
   }
}

void
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

   assert(buf->size >= offset);

   cmd->state.index_va = buf->bo->iova + buf->bo_offset + offset;
   cmd->state.max_index_count = (buf->size - offset) >> index_shift;
   cmd->state.index_size = index_size;
}

void
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

   for (unsigned i = 0; i < descriptorSetCount; ++i) {
      unsigned idx = i + firstSet;
      TU_FROM_HANDLE(tu_descriptor_set, set, pDescriptorSets[i]);

      descriptors_state->sets[idx] = set;

      for(unsigned j = 0; j < set->layout->dynamic_offset_count; ++j, ++dyn_idx) {
         /* update the contents of the dynamic descriptor set */
         unsigned src_idx = j;
         unsigned dst_idx = j + layout->set[idx].dynamic_offset_start;
         assert(dyn_idx < dynamicOffsetCount);

         uint32_t *dst =
            &descriptors_state->dynamic_descriptors[dst_idx * A6XX_TEX_CONST_DWORDS];
         uint32_t *src =
            &set->dynamic_descriptors[src_idx * A6XX_TEX_CONST_DWORDS];
         uint32_t offset = pDynamicOffsets[dyn_idx];

         /* Patch the storage/uniform descriptors right away. */
         if (layout->set[idx].layout->dynamic_ubo & (1 << j)) {
            /* Note: we can assume here that the addition won't roll over and
             * change the SIZE field.
             */
            uint64_t va = src[0] | ((uint64_t)src[1] << 32);
            va += offset;
            dst[0] = va;
            dst[1] = va >> 32;
         } else {
            memcpy(dst, src, A6XX_TEX_CONST_DWORDS * 4);
            /* Note: A6XX_IBO_5_DEPTH is always 0 */
            uint64_t va = dst[4] | ((uint64_t)dst[5] << 32);
            va += offset;
            dst[4] = va;
            dst[5] = va >> 32;
         }
      }
   }
   assert(dyn_idx == dynamicOffsetCount);

   uint32_t sp_bindless_base_reg, hlsq_bindless_base_reg, hlsq_invalidate_value;
   uint64_t addr[MAX_SETS + 1] = {};
   struct tu_cs *cs, state_cs;

   for (uint32_t i = 0; i < MAX_SETS; i++) {
      struct tu_descriptor_set *set = descriptors_state->sets[i];
      if (set)
         addr[i] = set->va | 3;
   }

   if (layout->dynamic_offset_count) {
      /* allocate and fill out dynamic descriptor set */
      struct tu_cs_memory dynamic_desc_set;
      VkResult result = tu_cs_alloc(&cmd->sub_cs, layout->dynamic_offset_count,
                                    A6XX_TEX_CONST_DWORDS, &dynamic_desc_set);
      if (result != VK_SUCCESS) {
         cmd->record_result = result;
         return;
      }

      memcpy(dynamic_desc_set.map, descriptors_state->dynamic_descriptors,
             layout->dynamic_offset_count * A6XX_TEX_CONST_DWORDS * 4);
      addr[MAX_SETS] = dynamic_desc_set.iova | 3;
   }

   if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      sp_bindless_base_reg = REG_A6XX_SP_BINDLESS_BASE(0);
      hlsq_bindless_base_reg = REG_A6XX_HLSQ_BINDLESS_BASE(0);
      hlsq_invalidate_value = A6XX_HLSQ_INVALIDATE_CMD_GFX_BINDLESS(0x1f);

      cmd->state.desc_sets = tu_cs_draw_state(&cmd->sub_cs, &state_cs, 24);
      cmd->state.dirty |= TU_CMD_DIRTY_DESC_SETS_LOAD | TU_CMD_DIRTY_SHADER_CONSTS;
      cs = &state_cs;
   } else {
      assert(pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE);

      sp_bindless_base_reg = REG_A6XX_SP_CS_BINDLESS_BASE(0);
      hlsq_bindless_base_reg = REG_A6XX_HLSQ_CS_BINDLESS_BASE(0);
      hlsq_invalidate_value = A6XX_HLSQ_INVALIDATE_CMD_CS_BINDLESS(0x1f);

      cmd->state.dirty |= TU_CMD_DIRTY_COMPUTE_DESC_SETS_LOAD;
      cs = &cmd->cs;
   }

   tu_cs_emit_pkt4(cs, sp_bindless_base_reg, 10);
   tu_cs_emit_array(cs, (const uint32_t*) addr, 10);
   tu_cs_emit_pkt4(cs, hlsq_bindless_base_reg, 10);
   tu_cs_emit_array(cs, (const uint32_t*) addr, 10);
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

void tu_CmdPushDescriptorSetKHR(VkCommandBuffer commandBuffer,
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
      cmd->record_result = result;
      return;
   }

   /* preserve previous content if the layout is the same: */
   if (set->layout == layout)
      memcpy(set_mem.map, set->mapped_ptr, MIN2(set->size, layout->size));

   set->layout = layout;
   set->mapped_ptr = set_mem.map;
   set->va = set_mem.iova;

   tu_update_descriptor_sets(tu_descriptor_set_to_handle(set),
                             descriptorWriteCount, pDescriptorWrites, 0, NULL);

   tu_CmdBindDescriptorSets(commandBuffer, pipelineBindPoint, _layout, _set,
                            1, (VkDescriptorSet[]) { tu_descriptor_set_to_handle(set) },
                            0, NULL);
}

void tu_CmdPushDescriptorSetWithTemplateKHR(
   VkCommandBuffer commandBuffer,
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
      cmd->record_result = result;
      return;
   }

   /* preserve previous content if the layout is the same: */
   if (set->layout == layout)
      memcpy(set_mem.map, set->mapped_ptr, MIN2(set->size, layout->size));

   set->layout = layout;
   set->mapped_ptr = set_mem.map;
   set->va = set_mem.iova;

   tu_update_descriptor_set_with_template(set, descriptorUpdateTemplate, pData);

   tu_CmdBindDescriptorSets(commandBuffer, templ->bind_point, _layout, _set,
                            1, (VkDescriptorSet[]) { tu_descriptor_set_to_handle(set) },
                            0, NULL);
}

void tu_CmdBindTransformFeedbackBuffersEXT(VkCommandBuffer commandBuffer,
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
      uint64_t iova = buf->bo->iova + pOffsets[i];
      uint32_t size = buf->bo->size - pOffsets[i];
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

void
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
      tu_cs_emit_qw(cs, buf->bo->iova + counter_buffer_offset);

      if (offset) {
         tu_cs_emit_pkt7(cs, CP_REG_RMW, 3);
         tu_cs_emit(cs, CP_REG_RMW_0_DST_REG(REG_A6XX_VPC_SO_BUFFER_OFFSET(idx)) |
                        CP_REG_RMW_0_SRC1_ADD);
         tu_cs_emit_qw(cs, 0xffffffff);
         tu_cs_emit_qw(cs, offset);
      }
   }

   tu_cond_exec_end(cs);
}

void tu_CmdEndTransformFeedbackEXT(VkCommandBuffer commandBuffer,
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
         tu_cs_emit_qw(cs, 0xffffffff);
         tu_cs_emit_qw(cs, -offset);
      }

      tu_cs_emit_pkt7(cs, CP_REG_TO_MEM, 3);
      tu_cs_emit(cs, CP_REG_TO_MEM_0_REG(REG_A6XX_CP_SCRATCH_REG(0)) |
                     CP_REG_TO_MEM_0_CNT(1));
      tu_cs_emit_qw(cs, buf->bo->iova + counter_buffer_offset);
   }

   tu_cond_exec_end(cs);

   cmd->state.xfb_used = true;
}

void
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

VkResult
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
   } else {
      tu_flush_all_pending(&cmd_buffer->state.cache);
      cmd_buffer->state.cache.flush_bits |=
         TU_CMD_FLAG_CCU_FLUSH_COLOR |
         TU_CMD_FLAG_CCU_FLUSH_DEPTH;
      tu_emit_cache_flush(cmd_buffer, &cmd_buffer->cs);
   }

   tu_cs_end(&cmd_buffer->cs);
   tu_cs_end(&cmd_buffer->draw_cs);
   tu_cs_end(&cmd_buffer->draw_epilogue_cs);

   cmd_buffer->status = TU_CMD_BUFFER_STATUS_EXECUTABLE;

   return cmd_buffer->record_result;
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

void
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
   cmd->state.dirty |= TU_CMD_DIRTY_DESC_SETS_LOAD | TU_CMD_DIRTY_SHADER_CONSTS | TU_CMD_DIRTY_LRZ;

   /* note: this also avoids emitting draw states before renderpass clears,
    * which may use the 3D clear path (for MSAA cases)
    */
   if (!(cmd->state.dirty & TU_CMD_DIRTY_DRAW_STATE)) {
      struct tu_cs *cs = &cmd->draw_cs;
      uint32_t mask = ~pipeline->dynamic_state_mask & BITFIELD_MASK(TU_DYNAMIC_STATE_COUNT);

      tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3 * (6 + util_bitcount(mask)));
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_PROGRAM, pipeline->program.state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_PROGRAM_BINNING, pipeline->program.binning_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VI, pipeline->vi.state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VI_BINNING, pipeline->vi.binning_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_RAST, pipeline->rast_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_BLEND, pipeline->blend_state);

      u_foreach_bit(i, mask)
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DYNAMIC + i, pipeline->dynamic_state[i]);
   }

   /* the vertex_buffers draw state always contains all the currently
    * bound vertex buffers. update its size to only emit the vbs which
    * are actually used by the pipeline
    * note there is a HW optimization which makes it so the draw state
    * is not re-executed completely when only the size changes
    */
   if (cmd->state.vertex_buffers.size != pipeline->num_vbs * 4) {
      cmd->state.vertex_buffers.size = pipeline->num_vbs * 4;
      cmd->state.dirty |= TU_CMD_DIRTY_VERTEX_BUFFERS;
   }

   if ((pipeline->dynamic_state_mask & BIT(TU_DYNAMIC_STATE_VB_STRIDE)) &&
       cmd->state.dynamic_state[TU_DYNAMIC_STATE_VB_STRIDE].size != pipeline->num_vbs * 2) {
      cmd->state.dynamic_state[TU_DYNAMIC_STATE_VB_STRIDE].size = pipeline->num_vbs * 2;
      cmd->state.dirty |= TU_CMD_DIRTY_VB_STRIDE;
   }

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
#undef UPDATE_REG

   if (pipeline->rb_depth_cntl_disable)
      cmd->state.dirty |= TU_CMD_DIRTY_RB_DEPTH_CNTL;
}

void
tu_CmdSetViewport(VkCommandBuffer commandBuffer,
                  uint32_t firstViewport,
                  uint32_t viewportCount,
                  const VkViewport *pViewports)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs;

   memcpy(&cmd->state.viewport[firstViewport], pViewports, viewportCount * sizeof(*pViewports));
   cmd->state.max_viewport = MAX2(cmd->state.max_viewport, firstViewport + viewportCount);

   cs = tu_cmd_dynamic_state(cmd, VK_DYNAMIC_STATE_VIEWPORT, 8 + 10 * cmd->state.max_viewport);
   tu6_emit_viewport(&cs, cmd->state.viewport, cmd->state.max_viewport);
}

void
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

void
tu_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.gras_su_cntl &= ~A6XX_GRAS_SU_CNTL_LINEHALFWIDTH__MASK;
   cmd->state.gras_su_cntl |= A6XX_GRAS_SU_CNTL_LINEHALFWIDTH(lineWidth / 2.0f);

   cmd->state.dirty |= TU_CMD_DIRTY_GRAS_SU_CNTL;
}

void
tu_CmdSetDepthBias(VkCommandBuffer commandBuffer,
                   float depthBiasConstantFactor,
                   float depthBiasClamp,
                   float depthBiasSlopeFactor)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs = tu_cmd_dynamic_state(cmd, VK_DYNAMIC_STATE_DEPTH_BIAS, 4);

   tu6_emit_depth_bias(&cs, depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor);
}

void
tu_CmdSetBlendConstants(VkCommandBuffer commandBuffer,
                        const float blendConstants[4])
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs = tu_cmd_dynamic_state(cmd, VK_DYNAMIC_STATE_BLEND_CONSTANTS, 5);

   tu_cs_emit_pkt4(&cs, REG_A6XX_RB_BLEND_RED_F32, 4);
   tu_cs_emit_array(&cs, (const uint32_t *) blendConstants, 4);
}

void
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

static void
update_stencil_mask(uint32_t *value, VkStencilFaceFlags face, uint32_t mask)
{
   if (face & VK_STENCIL_FACE_FRONT_BIT)
      *value = (*value & 0xff00) | (mask & 0xff);
   if (face & VK_STENCIL_FACE_BACK_BIT)
      *value = (*value & 0xff) | (mask & 0xff) << 8;
}

void
tu_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer,
                            VkStencilFaceFlags faceMask,
                            uint32_t compareMask)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs = tu_cmd_dynamic_state(cmd, VK_DYNAMIC_STATE_STENCIL_COMPARE_MASK, 2);

   update_stencil_mask(&cmd->state.dynamic_stencil_mask, faceMask, compareMask);

   tu_cs_emit_regs(&cs, A6XX_RB_STENCILMASK(.dword = cmd->state.dynamic_stencil_mask));
}

void
tu_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer,
                          VkStencilFaceFlags faceMask,
                          uint32_t writeMask)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs = tu_cmd_dynamic_state(cmd, VK_DYNAMIC_STATE_STENCIL_WRITE_MASK, 2);

   update_stencil_mask(&cmd->state.dynamic_stencil_wrmask, faceMask, writeMask);

   tu_cs_emit_regs(&cs, A6XX_RB_STENCILWRMASK(.dword = cmd->state.dynamic_stencil_wrmask));
}

void
tu_CmdSetStencilReference(VkCommandBuffer commandBuffer,
                          VkStencilFaceFlags faceMask,
                          uint32_t reference)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs = tu_cmd_dynamic_state(cmd, VK_DYNAMIC_STATE_STENCIL_REFERENCE, 2);

   update_stencil_mask(&cmd->state.dynamic_stencil_ref, faceMask, reference);

   tu_cs_emit_regs(&cs, A6XX_RB_STENCILREF(.dword = cmd->state.dynamic_stencil_ref));
}

void
tu_CmdSetSampleLocationsEXT(VkCommandBuffer commandBuffer,
                            const VkSampleLocationsInfoEXT* pSampleLocationsInfo)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs = tu_cmd_dynamic_state(cmd, TU_DYNAMIC_STATE_SAMPLE_LOCATIONS, 9);

   assert(pSampleLocationsInfo);

   tu6_emit_sample_locations(&cs, pSampleLocationsInfo);
}

void
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

void
tu_CmdSetFrontFaceEXT(VkCommandBuffer commandBuffer, VkFrontFace frontFace)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.gras_su_cntl &= ~A6XX_GRAS_SU_CNTL_FRONT_CW;

   if (frontFace == VK_FRONT_FACE_CLOCKWISE)
      cmd->state.gras_su_cntl |= A6XX_GRAS_SU_CNTL_FRONT_CW;

   cmd->state.dirty |= TU_CMD_DIRTY_GRAS_SU_CNTL;
}

void
tu_CmdSetPrimitiveTopologyEXT(VkCommandBuffer commandBuffer,
                              VkPrimitiveTopology primitiveTopology)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.primtype = tu6_primtype(primitiveTopology);
}

void
tu_CmdSetViewportWithCountEXT(VkCommandBuffer commandBuffer,
                              uint32_t viewportCount,
                              const VkViewport* pViewports)
{
   tu_CmdSetViewport(commandBuffer, 0, viewportCount, pViewports);
}

void
tu_CmdSetScissorWithCountEXT(VkCommandBuffer commandBuffer,
                             uint32_t scissorCount,
                             const VkRect2D* pScissors)
{
   tu_CmdSetScissor(commandBuffer, 0, scissorCount, pScissors);
}

void
tu_CmdSetDepthTestEnableEXT(VkCommandBuffer commandBuffer,
                            VkBool32 depthTestEnable)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.rb_depth_cntl &= ~A6XX_RB_DEPTH_CNTL_Z_ENABLE;

   if (depthTestEnable)
      cmd->state.rb_depth_cntl |= A6XX_RB_DEPTH_CNTL_Z_ENABLE;

   cmd->state.dirty |= TU_CMD_DIRTY_RB_DEPTH_CNTL;
}

void
tu_CmdSetDepthWriteEnableEXT(VkCommandBuffer commandBuffer,
                             VkBool32 depthWriteEnable)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.rb_depth_cntl &= ~A6XX_RB_DEPTH_CNTL_Z_WRITE_ENABLE;

   if (depthWriteEnable)
      cmd->state.rb_depth_cntl |= A6XX_RB_DEPTH_CNTL_Z_WRITE_ENABLE;

   cmd->state.dirty |= TU_CMD_DIRTY_RB_DEPTH_CNTL;
}

void
tu_CmdSetDepthCompareOpEXT(VkCommandBuffer commandBuffer,
                           VkCompareOp depthCompareOp)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.rb_depth_cntl &= ~A6XX_RB_DEPTH_CNTL_ZFUNC__MASK;

   cmd->state.rb_depth_cntl |=
      A6XX_RB_DEPTH_CNTL_ZFUNC(tu6_compare_func(depthCompareOp));

   cmd->state.dirty |= TU_CMD_DIRTY_RB_DEPTH_CNTL;
}

void
tu_CmdSetDepthBoundsTestEnableEXT(VkCommandBuffer commandBuffer,
                                  VkBool32 depthBoundsTestEnable)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.rb_depth_cntl &= ~A6XX_RB_DEPTH_CNTL_Z_BOUNDS_ENABLE;

   if (depthBoundsTestEnable)
      cmd->state.rb_depth_cntl |= A6XX_RB_DEPTH_CNTL_Z_BOUNDS_ENABLE;

   cmd->state.dirty |= TU_CMD_DIRTY_RB_DEPTH_CNTL;
}

void
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

void
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

static void
tu_flush_for_access(struct tu_cache_state *cache,
                    enum tu_cmd_access_mask src_mask,
                    enum tu_cmd_access_mask dst_mask)
{
   enum tu_cmd_flush_bits flush_bits = 0;

   if (src_mask & TU_ACCESS_HOST_WRITE) {
      /* Host writes are always visible to CP, so only invalidate GPU caches */
      cache->pending_flush_bits |= TU_CMD_FLAG_GPU_INVALIDATE;
   }

   if (src_mask & TU_ACCESS_SYSMEM_WRITE) {
      /* Invalidate CP and 2D engine (make it do WFI + WFM if necessary) as
       * well.
       */
      cache->pending_flush_bits |= TU_CMD_FLAG_ALL_INVALIDATE;
   }

   if (src_mask & TU_ACCESS_CP_WRITE) {
      /* Flush the CP write queue. However a WFI shouldn't be necessary as
       * WAIT_MEM_WRITES should cover it.
       */
      cache->pending_flush_bits |=
         TU_CMD_FLAG_WAIT_MEM_WRITES |
         TU_CMD_FLAG_GPU_INVALIDATE |
         TU_CMD_FLAG_WAIT_FOR_ME;
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
   if (dst_mask & (TU_ACCESS_SYSMEM_READ | TU_ACCESS_SYSMEM_WRITE |
                   TU_ACCESS_HOST_READ | TU_ACCESS_HOST_WRITE)) {
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

   if (dst_mask & TU_ACCESS_WFI_READ) {
      flush_bits |= cache->pending_flush_bits &
         (TU_CMD_FLAG_ALL_FLUSH | TU_CMD_FLAG_WAIT_FOR_IDLE);
   }

   if (dst_mask & TU_ACCESS_WFM_READ) {
      flush_bits |= cache->pending_flush_bits &
         (TU_CMD_FLAG_ALL_FLUSH | TU_CMD_FLAG_WAIT_FOR_ME);
   }

   cache->flush_bits |= flush_bits;
   cache->pending_flush_bits &= ~flush_bits;
}

static enum tu_cmd_access_mask
vk2tu_access(VkAccessFlags flags, bool gmem)
{
   enum tu_cmd_access_mask mask = 0;

   /* If the GPU writes a buffer that is then read by an indirect draw
    * command, we theoretically need to emit a WFI to wait for any cache
    * flushes, and then a WAIT_FOR_ME to wait on the CP for the WFI to
    * complete. Waiting for the WFI to complete is performed as part of the
    * draw by the firmware, so we just need to execute the WFI.
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
   if (flags &
       (VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
        VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT |
        VK_ACCESS_CONDITIONAL_RENDERING_READ_BIT_EXT |
        VK_ACCESS_MEMORY_READ_BIT)) {
      mask |= TU_ACCESS_WFI_READ;
   }

   if (flags &
       (VK_ACCESS_INDIRECT_COMMAND_READ_BIT | /* Read performed by CP */
        VK_ACCESS_CONDITIONAL_RENDERING_READ_BIT_EXT | /* Read performed by CP */
        VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT | /* Read performed by CP */
        VK_ACCESS_MEMORY_READ_BIT)) {
      mask |= TU_ACCESS_SYSMEM_READ;
   }

   if (flags &
       (VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT |
        VK_ACCESS_MEMORY_WRITE_BIT)) {
      mask |= TU_ACCESS_CP_WRITE;
   }

   if (flags &
       (VK_ACCESS_HOST_READ_BIT |
        VK_ACCESS_MEMORY_WRITE_BIT)) {
      mask |= TU_ACCESS_HOST_READ;
   }

   if (flags &
       (VK_ACCESS_HOST_WRITE_BIT |
        VK_ACCESS_MEMORY_WRITE_BIT)) {
      mask |= TU_ACCESS_HOST_WRITE;
   }

   if (flags &
       (VK_ACCESS_INDEX_READ_BIT | /* Read performed by PC, I think */
        VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | /* Read performed by VFD */
        VK_ACCESS_UNIFORM_READ_BIT | /* Read performed by SP */
        /* TODO: Is there a no-cache bit for textures so that we can ignore
         * these?
         */
        VK_ACCESS_INPUT_ATTACHMENT_READ_BIT | /* Read performed by TP */
        VK_ACCESS_SHADER_READ_BIT | /* Read perfomed by SP/TP */
        VK_ACCESS_MEMORY_READ_BIT)) {
      mask |= TU_ACCESS_UCHE_READ;
   }

   if (flags &
       (VK_ACCESS_SHADER_WRITE_BIT | /* Write performed by SP */
        VK_ACCESS_TRANSFORM_FEEDBACK_WRITE_BIT_EXT | /* Write performed by VPC */
        VK_ACCESS_MEMORY_WRITE_BIT)) {
      mask |= TU_ACCESS_UCHE_WRITE;
   }

   /* When using GMEM, the CCU is always flushed automatically to GMEM, and
    * then GMEM is flushed to sysmem. Furthermore, we already had to flush any
    * previous writes in sysmem mode when transitioning to GMEM. Therefore we
    * can ignore CCU and pretend that color attachments and transfers use
    * sysmem directly.
    */

   if (flags &
       (VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_READ_NONCOHERENT_BIT_EXT |
        VK_ACCESS_MEMORY_READ_BIT)) {
      if (gmem)
         mask |= TU_ACCESS_SYSMEM_READ;
      else
         mask |= TU_ACCESS_CCU_COLOR_INCOHERENT_READ;
   }

   if (flags &
       (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
        VK_ACCESS_MEMORY_READ_BIT)) {
      if (gmem)
         mask |= TU_ACCESS_SYSMEM_READ;
      else
         mask |= TU_ACCESS_CCU_DEPTH_INCOHERENT_READ;
   }

   if (flags &
       (VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_MEMORY_WRITE_BIT)) {
      if (gmem) {
         mask |= TU_ACCESS_SYSMEM_WRITE;
      } else {
         mask |= TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE;
      }
   }

   if (flags &
       (VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_MEMORY_WRITE_BIT)) {
      if (gmem) {
         mask |= TU_ACCESS_SYSMEM_WRITE;
      } else {
         mask |= TU_ACCESS_CCU_DEPTH_INCOHERENT_WRITE;
      }
   }

   /* When the dst access is a transfer read/write, it seems we sometimes need
    * to insert a WFI after any flushes, to guarantee that the flushes finish
    * before the 2D engine starts. However the opposite (i.e. a WFI after
    * CP_BLIT and before any subsequent flush) does not seem to be needed, and
    * the blob doesn't emit such a WFI.
    */

   if (flags &
       (VK_ACCESS_TRANSFER_WRITE_BIT |
        VK_ACCESS_MEMORY_WRITE_BIT)) {
      if (gmem) {
         mask |= TU_ACCESS_SYSMEM_WRITE;
      } else {
         mask |= TU_ACCESS_CCU_COLOR_WRITE;
      }
      mask |= TU_ACCESS_WFI_READ;
   }

   if (flags &
       (VK_ACCESS_TRANSFER_READ_BIT | /* Access performed by TP */
        VK_ACCESS_MEMORY_READ_BIT)) {
      mask |= TU_ACCESS_UCHE_READ | TU_ACCESS_WFI_READ;
   }

   return mask;
}


void
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
            cmd->record_result = result;
            break;
         }

         result = tu_cs_add_entries(&cmd->draw_epilogue_cs,
               &secondary->draw_epilogue_cs);
         if (result != VK_SUCCESS) {
            cmd->record_result = result;
            break;
         }

         if (secondary->state.has_tess)
            cmd->state.has_tess = true;
         if (secondary->state.has_subpass_predication)
            cmd->state.has_subpass_predication = true;
      } else {
         assert(tu_cs_is_empty(&secondary->draw_cs));
         assert(tu_cs_is_empty(&secondary->draw_epilogue_cs));

         tu_cs_add_entries(&cmd->cs, &secondary->cs);
      }

      cmd->state.index_size = secondary->state.index_size; /* for restart index update */
   }
   cmd->state.dirty = ~0u; /* TODO: set dirty only what needs to be */

   if (cmd->state.pass) {
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

VkResult
tu_CreateCommandPool(VkDevice _device,
                     const VkCommandPoolCreateInfo *pCreateInfo,
                     const VkAllocationCallbacks *pAllocator,
                     VkCommandPool *pCmdPool)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   struct tu_cmd_pool *pool;

   pool = vk_object_alloc(&device->vk, pAllocator, sizeof(*pool),
                          VK_OBJECT_TYPE_COMMAND_POOL);
   if (pool == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (pAllocator)
      pool->alloc = *pAllocator;
   else
      pool->alloc = device->vk.alloc;

   list_inithead(&pool->cmd_buffers);
   list_inithead(&pool->free_cmd_buffers);

   pool->queue_family_index = pCreateInfo->queueFamilyIndex;

   *pCmdPool = tu_cmd_pool_to_handle(pool);

   return VK_SUCCESS;
}

void
tu_DestroyCommandPool(VkDevice _device,
                      VkCommandPool commandPool,
                      const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_cmd_pool, pool, commandPool);

   if (!pool)
      return;

   list_for_each_entry_safe(struct tu_cmd_buffer, cmd_buffer,
                            &pool->cmd_buffers, pool_link)
   {
      tu_cmd_buffer_destroy(cmd_buffer);
   }

   list_for_each_entry_safe(struct tu_cmd_buffer, cmd_buffer,
                            &pool->free_cmd_buffers, pool_link)
   {
      tu_cmd_buffer_destroy(cmd_buffer);
   }

   vk_object_free(&device->vk, pAllocator, pool);
}

VkResult
tu_ResetCommandPool(VkDevice device,
                    VkCommandPool commandPool,
                    VkCommandPoolResetFlags flags)
{
   TU_FROM_HANDLE(tu_cmd_pool, pool, commandPool);
   VkResult result;

   list_for_each_entry(struct tu_cmd_buffer, cmd_buffer, &pool->cmd_buffers,
                       pool_link)
   {
      result = tu_reset_cmd_buffer(cmd_buffer);
      if (result != VK_SUCCESS)
         return result;
   }

   return VK_SUCCESS;
}

void
tu_TrimCommandPool(VkDevice device,
                   VkCommandPool commandPool,
                   VkCommandPoolTrimFlags flags)
{
   TU_FROM_HANDLE(tu_cmd_pool, pool, commandPool);

   if (!pool)
      return;

   list_for_each_entry_safe(struct tu_cmd_buffer, cmd_buffer,
                            &pool->free_cmd_buffers, pool_link)
   {
      tu_cmd_buffer_destroy(cmd_buffer);
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
   enum tu_cmd_access_mask src_flags =
      vk2tu_access(barrier->src_access_mask, false);
   enum tu_cmd_access_mask dst_flags =
      vk2tu_access(barrier->dst_access_mask, false);

   if (barrier->incoherent_ccu_color)
      src_flags |= TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE;
   if (barrier->incoherent_ccu_depth)
      src_flags |= TU_ACCESS_CCU_DEPTH_INCOHERENT_WRITE;

   tu_flush_for_access(cache, src_flags, dst_flags);
}

void
tu_CmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                       const VkRenderPassBeginInfo *pRenderPassBegin,
                       const VkSubpassBeginInfo *pSubpassBeginInfo)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_render_pass, pass, pRenderPassBegin->renderPass);
   TU_FROM_HANDLE(tu_framebuffer, fb, pRenderPassBegin->framebuffer);

   cmd->state.pass = pass;
   cmd->state.subpass = pass->subpasses;
   cmd->state.framebuffer = fb;
   cmd->state.render_area = pRenderPassBegin->renderArea;

   tu_cmd_prepare_tile_store_ib(cmd);

   /* Note: because this is external, any flushes will happen before draw_cs
    * gets called. However deferred flushes could have to happen later as part
    * of the subpass.
    */
   tu_subpass_barrier(cmd, &pass->subpasses[0].start_barrier, true);
   cmd->state.renderpass_cache.pending_flush_bits =
      cmd->state.cache.pending_flush_bits;
   cmd->state.renderpass_cache.flush_bits = 0;

   /* Track LRZ valid state */
   uint32_t a = cmd->state.subpass->depth_stencil_attachment.attachment;
   if (a != VK_ATTACHMENT_UNUSED) {
      const struct tu_render_pass_attachment *att = &cmd->state.pass->attachments[a];
      struct tu_image *image = fb->attachments[a].attachment->image;
      /* if image as lrz and it isn't a stencil-only clear: */
      if (image->lrz_height &&
          (att->clear_mask & (VK_IMAGE_ASPECT_COLOR_BIT | VK_IMAGE_ASPECT_DEPTH_BIT))) {
         cmd->state.lrz.image = image;
         cmd->state.lrz.valid = true;

         tu6_clear_lrz(cmd, &cmd->cs, image, &pRenderPassBegin->pClearValues[a]);
         tu6_emit_event_write(cmd, &cmd->cs, PC_CCU_FLUSH_COLOR_TS);
      } else {
         cmd->state.lrz.valid = false;
      }
      cmd->state.dirty |= TU_CMD_DIRTY_LRZ;
   }

   tu_emit_renderpass_begin(cmd, pRenderPassBegin);

   tu6_emit_zs(cmd, cmd->state.subpass, &cmd->draw_cs);
   tu6_emit_mrt(cmd, cmd->state.subpass, &cmd->draw_cs);
   tu6_emit_msaa(&cmd->draw_cs, cmd->state.subpass->samples);
   tu6_emit_render_cntl(cmd, cmd->state.subpass, &cmd->draw_cs, false);

   tu_set_input_attachments(cmd, cmd->state.subpass);
}

void
tu_CmdNextSubpass2(VkCommandBuffer commandBuffer,
                   const VkSubpassBeginInfo *pSubpassBeginInfo,
                   const VkSubpassEndInfo *pSubpassEndInfo)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   const struct tu_render_pass *pass = cmd->state.pass;
   struct tu_cs *cs = &cmd->draw_cs;

   const struct tu_subpass *subpass = cmd->state.subpass++;

   /* Track LRZ valid state
    *
    * TODO: Improve this tracking for keeping the state of the past depth/stencil images,
    * so if they become active again, we reuse its old state.
    */
   cmd->state.lrz.valid = false;
   cmd->state.dirty |= TU_CMD_DIRTY_LRZ;

   tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_GMEM);

   if (subpass->resolve_attachments) {
      tu6_emit_blit_scissor(cmd, cs, true);

      for (unsigned i = 0; i < subpass->resolve_count; i++) {
         uint32_t a = subpass->resolve_attachments[i].attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;

         uint32_t gmem_a = tu_subpass_get_attachment_to_resolve(subpass, i);

         tu_store_gmem_attachment(cmd, cs, a, gmem_a);

         if (pass->attachments[a].gmem_offset < 0)
            continue;

         /* TODO:
          * check if the resolved attachment is needed by later subpasses,
          * if it is, should be doing a GMEM->GMEM resolve instead of GMEM->MEM->GMEM..
          */
         tu_finishme("missing GMEM->GMEM resolve path\n");
         tu_load_gmem_attachment(cmd, cs, a, true);
      }
   }

   tu_cond_exec_end(cs);

   tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_SYSMEM);

   tu6_emit_sysmem_resolves(cmd, cs, subpass);

   tu_cond_exec_end(cs);

   /* Handle dependencies for the next subpass */
   tu_subpass_barrier(cmd, &cmd->state.subpass->start_barrier, false);

   /* emit mrt/zs/msaa/ubwc state for the subpass that is starting */
   tu6_emit_zs(cmd, cmd->state.subpass, cs);
   tu6_emit_mrt(cmd, cmd->state.subpass, cs);
   tu6_emit_msaa(cs, cmd->state.subpass->samples);
   tu6_emit_render_cntl(cmd, cmd->state.subpass, cs, false);

   tu_set_input_attachments(cmd, cmd->state.subpass);
}

static void
tu6_emit_user_consts(struct tu_cs *cs, const struct tu_pipeline *pipeline,
                     struct tu_descriptor_state *descriptors_state,
                     gl_shader_stage type,
                     uint32_t *push_constants)
{
   const struct tu_program_descriptor_linkage *link =
      &pipeline->program.link[type];
   const struct ir3_const_state *const_state = &link->const_state;
   const struct ir3_ubo_analysis_state *state = &const_state->ubo_state;

   if (link->push_consts.count > 0) {
      unsigned num_units = link->push_consts.count;
      unsigned offset = link->push_consts.lo;
      tu_cs_emit_pkt7(cs, tu6_stage2opcode(type), 3 + num_units * 4);
      tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(offset) |
            CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
            CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
            CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(type)) |
            CP_LOAD_STATE6_0_NUM_UNIT(num_units));
      tu_cs_emit(cs, 0);
      tu_cs_emit(cs, 0);
      for (unsigned i = 0; i < num_units * 4; i++)
         tu_cs_emit(cs, push_constants[i + offset * 4]);
   }

   for (uint32_t i = 0; i < state->num_enabled; i++) {
      uint32_t size = state->range[i].end - state->range[i].start;
      uint32_t offset = state->range[i].start;

      /* and even if the start of the const buffer is before
       * first_immediate, the end may not be:
       */
      size = MIN2(size, (16 * link->constlen) - state->range[i].offset);

      if (size == 0)
         continue;

      /* things should be aligned to vec4: */
      debug_assert((state->range[i].offset % 16) == 0);
      debug_assert((size % 16) == 0);
      debug_assert((offset % 16) == 0);

      /* Dig out the descriptor from the descriptor state and read the VA from
       * it.  All our UBOs are bindless with the exception of the NIR
       * constant_data, which is uploaded once in the pipeline.
       */
      if (!state->range[i].ubo.bindless) {
         assert(state->range[i].ubo.block == const_state->constant_data_ubo);
         continue;
      }

      uint32_t *base = state->range[i].ubo.bindless_base == MAX_SETS ?
         descriptors_state->dynamic_descriptors :
         descriptors_state->sets[state->range[i].ubo.bindless_base]->mapped_ptr;
      unsigned block = state->range[i].ubo.block;
      uint32_t *desc = base + block * A6XX_TEX_CONST_DWORDS;
      uint64_t va = desc[0] | ((uint64_t)(desc[1] & A6XX_UBO_1_BASE_HI__MASK) << 32);
      assert(va);

      tu_cs_emit_pkt7(cs, tu6_stage2opcode(type), 3);
      tu_cs_emit(cs, CP_LOAD_STATE6_0_DST_OFF(state->range[i].offset / 16) |
            CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
            CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
            CP_LOAD_STATE6_0_STATE_BLOCK(tu6_stage2shadersb(type)) |
            CP_LOAD_STATE6_0_NUM_UNIT(size / 16));
      tu_cs_emit_qw(cs, va + offset);
   }
}

static struct tu_draw_state
tu6_emit_consts(struct tu_cmd_buffer *cmd,
                const struct tu_pipeline *pipeline,
                struct tu_descriptor_state *descriptors_state,
                gl_shader_stage type)
{
   struct tu_cs cs;
   tu_cs_begin_sub_stream(&cmd->sub_cs, 512, &cs); /* TODO: maximum size? */

   tu6_emit_user_consts(&cs, pipeline, descriptors_state, type, cmd->push_constants);

   return tu_cs_end_draw_state(&cmd->sub_cs, &cs);
}

static uint64_t
get_tess_param_bo_size(const struct tu_pipeline *pipeline,
                       uint32_t draw_count)
{
   /* TODO: For indirect draws, we can't compute the BO size ahead of time.
    * Still not sure what to do here, so just allocate a reasonably large
    * BO and hope for the best for now. */
   if (!draw_count)
      draw_count = 2048;

   /* the tess param BO is pipeline->tess.param_stride bytes per patch,
    * which includes both the per-vertex outputs and per-patch outputs
    * build_primitive_map in ir3 calculates this stride
    */
   uint32_t verts_per_patch = pipeline->ia.primtype - DI_PT_PATCHES0;
   uint32_t num_patches = draw_count / verts_per_patch;
   return num_patches * pipeline->tess.param_stride;
}

static uint64_t
get_tess_factor_bo_size(const struct tu_pipeline *pipeline,
                        uint32_t draw_count)
{
   /* TODO: For indirect draws, we can't compute the BO size ahead of time.
    * Still not sure what to do here, so just allocate a reasonably large
    * BO and hope for the best for now. */
   if (!draw_count)
      draw_count = 2048;

   /* Each distinct patch gets its own tess factor output. */
   uint32_t verts_per_patch = pipeline->ia.primtype - DI_PT_PATCHES0;
   uint32_t num_patches = draw_count / verts_per_patch;
   uint32_t factor_stride;
   switch (pipeline->tess.patch_type) {
   case IR3_TESS_ISOLINES:
      factor_stride = 12;
      break;
   case IR3_TESS_TRIANGLES:
      factor_stride = 20;
      break;
   case IR3_TESS_QUADS:
      factor_stride = 28;
      break;
   default:
      unreachable("bad tessmode");
   }
   return factor_stride * num_patches;
}

static VkResult
tu6_emit_tess_consts(struct tu_cmd_buffer *cmd,
                     uint32_t draw_count,
                     const struct tu_pipeline *pipeline,
                     struct tu_draw_state *state,
                     uint64_t *factor_iova)
{
   struct tu_cs cs;
   VkResult result = tu_cs_begin_sub_stream(&cmd->sub_cs, 16, &cs);
   if (result != VK_SUCCESS)
      return result;

   const struct tu_program_descriptor_linkage *hs_link =
      &pipeline->program.link[MESA_SHADER_TESS_CTRL];
   bool hs_uses_bo = pipeline->tess.hs_bo_regid < hs_link->constlen;

   const struct tu_program_descriptor_linkage *ds_link =
      &pipeline->program.link[MESA_SHADER_TESS_EVAL];
   bool ds_uses_bo = pipeline->tess.ds_bo_regid < ds_link->constlen;

   uint64_t tess_factor_size = get_tess_factor_bo_size(pipeline, draw_count);
   uint64_t tess_param_size = get_tess_param_bo_size(pipeline, draw_count);
   uint64_t tess_bo_size =  tess_factor_size + tess_param_size;
   if ((hs_uses_bo || ds_uses_bo) && tess_bo_size > 0) {
      struct tu_bo *tess_bo;
      result = tu_get_scratch_bo(cmd->device, tess_bo_size, &tess_bo);
      if (result != VK_SUCCESS)
         return result;

      uint64_t tess_factor_iova = tess_bo->iova;
      uint64_t tess_param_iova = tess_factor_iova + tess_factor_size;

      if (hs_uses_bo) {
         tu_cs_emit_pkt7(&cs, CP_LOAD_STATE6_GEOM, 3 + 4);
         tu_cs_emit(&cs, CP_LOAD_STATE6_0_DST_OFF(pipeline->tess.hs_bo_regid) |
               CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
               CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
               CP_LOAD_STATE6_0_STATE_BLOCK(SB6_HS_SHADER) |
               CP_LOAD_STATE6_0_NUM_UNIT(1));
         tu_cs_emit(&cs, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
         tu_cs_emit(&cs, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));
         tu_cs_emit_qw(&cs, tess_param_iova);
         tu_cs_emit_qw(&cs, tess_factor_iova);
      }

      if (ds_uses_bo) {
         tu_cs_emit_pkt7(&cs, CP_LOAD_STATE6_GEOM, 3 + 4);
         tu_cs_emit(&cs, CP_LOAD_STATE6_0_DST_OFF(pipeline->tess.ds_bo_regid) |
               CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
               CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
               CP_LOAD_STATE6_0_STATE_BLOCK(SB6_DS_SHADER) |
               CP_LOAD_STATE6_0_NUM_UNIT(1));
         tu_cs_emit(&cs, CP_LOAD_STATE6_1_EXT_SRC_ADDR(0));
         tu_cs_emit(&cs, CP_LOAD_STATE6_2_EXT_SRC_ADDR_HI(0));
         tu_cs_emit_qw(&cs, tess_param_iova);
         tu_cs_emit_qw(&cs, tess_factor_iova);
      }

      *factor_iova = tess_factor_iova;
   }
   *state = tu_cs_end_draw_state(&cmd->sub_cs, &cs);
   return VK_SUCCESS;
}

static struct tu_draw_state
tu6_build_lrz(struct tu_cmd_buffer *cmd)
{
   const uint32_t a = cmd->state.subpass->depth_stencil_attachment.attachment;
   struct tu_cs lrz_cs;
   struct tu_draw_state ds = tu_cs_draw_state(&cmd->sub_cs, &lrz_cs, 4);

   if (cmd->state.pipeline->lrz.invalidate) {
      /* LRZ is not valid for next draw commands, so don't use it until cleared */
      cmd->state.lrz.valid = false;
   }

   if (a == VK_ATTACHMENT_UNUSED || !cmd->state.lrz.valid) {
      tu_cs_emit_regs(&lrz_cs, A6XX_GRAS_LRZ_CNTL(0));
      tu_cs_emit_regs(&lrz_cs, A6XX_RB_LRZ_CNTL(0));
      return ds;
   }

   /* Disable LRZ writes when blend is enabled, since the
    * resulting pixel value from the blend-draw
    * depends on an earlier draw, which LRZ in the draw pass
    * could early-reject if the previous blend-enabled draw wrote LRZ.
    *
    * TODO: We need to disable LRZ writes only for the binning pass.
    * Therefore, we need to emit it in a separate draw state. We keep
    * it disabled for sysmem path as well for the moment.
    */
   bool lrz_write = cmd->state.pipeline->lrz.write;
   if (cmd->state.pipeline->lrz.blend_disable_write)
      lrz_write = false;

   tu_cs_emit_regs(&lrz_cs, A6XX_GRAS_LRZ_CNTL(
      .enable = cmd->state.pipeline->lrz.enable,
      .greater = cmd->state.pipeline->lrz.greater,
      .lrz_write = lrz_write,
      .z_test_enable = cmd->state.pipeline->lrz.z_test_enable,
   ));

   tu_cs_emit_regs(&lrz_cs, A6XX_RB_LRZ_CNTL(.enable = cmd->state.pipeline->lrz.enable));
   return ds;
}

static VkResult
tu6_draw_common(struct tu_cmd_buffer *cmd,
                struct tu_cs *cs,
                bool indexed,
                /* note: draw_count is 0 for indirect */
                uint32_t draw_count)
{
   const struct tu_pipeline *pipeline = cmd->state.pipeline;
   VkResult result;

   struct tu_descriptor_state *descriptors_state =
      &cmd->descriptors[VK_PIPELINE_BIND_POINT_GRAPHICS];

   tu_emit_cache_flush_renderpass(cmd, cs);

   if (cmd->state.dirty & TU_CMD_DIRTY_LRZ)
      cmd->state.lrz.state = tu6_build_lrz(cmd);

   tu_cs_emit_regs(cs, A6XX_PC_PRIMITIVE_CNTL_0(
         .primitive_restart =
               pipeline->ia.primitive_restart && indexed,
         .tess_upper_left_domain_origin =
               pipeline->tess.upper_left_domain_origin));

   if (cmd->state.dirty & TU_CMD_DIRTY_GRAS_SU_CNTL) {
      struct tu_cs cs = tu_cmd_dynamic_state(cmd, TU_DYNAMIC_STATE_GRAS_SU_CNTL, 2);
      tu_cs_emit_regs(&cs, A6XX_GRAS_SU_CNTL(.dword = cmd->state.gras_su_cntl));
   }

   if (cmd->state.dirty & TU_CMD_DIRTY_RB_DEPTH_CNTL) {
      struct tu_cs cs = tu_cmd_dynamic_state(cmd, TU_DYNAMIC_STATE_RB_DEPTH_CNTL, 2);
      uint32_t rb_depth_cntl = cmd->state.rb_depth_cntl;

      if ((rb_depth_cntl & A6XX_RB_DEPTH_CNTL_Z_ENABLE) ||
          (rb_depth_cntl & A6XX_RB_DEPTH_CNTL_Z_BOUNDS_ENABLE))
         rb_depth_cntl |= A6XX_RB_DEPTH_CNTL_Z_TEST_ENABLE;

      if (pipeline->rb_depth_cntl_disable)
         rb_depth_cntl = 0;

      tu_cs_emit_regs(&cs, A6XX_RB_DEPTH_CNTL(.dword = rb_depth_cntl));
   }

   if (cmd->state.dirty & TU_CMD_DIRTY_RB_STENCIL_CNTL) {
      struct tu_cs cs = tu_cmd_dynamic_state(cmd, TU_DYNAMIC_STATE_RB_STENCIL_CNTL, 2);
      tu_cs_emit_regs(&cs, A6XX_RB_STENCIL_CONTROL(.dword = cmd->state.rb_stencil_cntl));
   }

   if (cmd->state.dirty & TU_CMD_DIRTY_SHADER_CONSTS) {
      cmd->state.shader_const[MESA_SHADER_VERTEX] =
         tu6_emit_consts(cmd, pipeline, descriptors_state, MESA_SHADER_VERTEX);
      cmd->state.shader_const[MESA_SHADER_TESS_CTRL] =
         tu6_emit_consts(cmd, pipeline, descriptors_state, MESA_SHADER_TESS_CTRL);
      cmd->state.shader_const[MESA_SHADER_TESS_EVAL] =
         tu6_emit_consts(cmd, pipeline, descriptors_state, MESA_SHADER_TESS_EVAL);
      cmd->state.shader_const[MESA_SHADER_GEOMETRY] =
         tu6_emit_consts(cmd, pipeline, descriptors_state, MESA_SHADER_GEOMETRY);
      cmd->state.shader_const[MESA_SHADER_FRAGMENT] =
         tu6_emit_consts(cmd, pipeline, descriptors_state, MESA_SHADER_FRAGMENT);
   }

   bool has_tess =
         pipeline->active_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
   struct tu_draw_state tess_consts = {};
   if (has_tess) {
      uint64_t tess_factor_iova = 0;

      cmd->state.has_tess = true;
      result = tu6_emit_tess_consts(cmd, draw_count, pipeline, &tess_consts, &tess_factor_iova);
      if (result != VK_SUCCESS)
         return result;

      /* this sequence matches what the blob does before every tess draw
       * PC_TESSFACTOR_ADDR_LO is a non-context register and needs a wfi
       * before writing to it
       */
      tu_cs_emit_wfi(cs);

      tu_cs_emit_regs(cs, A6XX_PC_TESSFACTOR_ADDR(.qword = tess_factor_iova));

      tu_cs_emit_pkt7(cs, CP_SET_SUBDRAW_SIZE, 1);
      tu_cs_emit(cs, draw_count);
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

      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_PROGRAM, pipeline->program.state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_PROGRAM_BINNING, pipeline->program.binning_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_TESS, tess_consts);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VI, pipeline->vi.state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VI_BINNING, pipeline->vi.binning_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_RAST, pipeline->rast_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_BLEND, pipeline->blend_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VS_CONST, cmd->state.shader_const[MESA_SHADER_VERTEX]);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_HS_CONST, cmd->state.shader_const[MESA_SHADER_TESS_CTRL]);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DS_CONST, cmd->state.shader_const[MESA_SHADER_TESS_EVAL]);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_GS_CONST, cmd->state.shader_const[MESA_SHADER_GEOMETRY]);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_FS_CONST, cmd->state.shader_const[MESA_SHADER_FRAGMENT]);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DESC_SETS, cmd->state.desc_sets);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DESC_SETS_LOAD, pipeline->load_state);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VB, cmd->state.vertex_buffers);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VS_PARAMS, cmd->state.vs_params);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_LRZ, cmd->state.lrz.state);

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
      bool emit_binding_stride = false;
      uint32_t draw_state_count =
         has_tess +
         ((cmd->state.dirty & TU_CMD_DIRTY_SHADER_CONSTS) ? 5 : 0) +
         ((cmd->state.dirty & TU_CMD_DIRTY_DESC_SETS_LOAD) ? 1 : 0) +
         ((cmd->state.dirty & TU_CMD_DIRTY_VERTEX_BUFFERS) ? 1 : 0) +
         ((cmd->state.dirty & TU_CMD_DIRTY_LRZ) ? 1 : 0) +
         1; /* vs_params */

      if ((cmd->state.dirty & TU_CMD_DIRTY_VB_STRIDE) &&
          !(pipeline->dynamic_state_mask & BIT(TU_DYNAMIC_STATE_VB_STRIDE))) {
         emit_binding_stride = true;
         draw_state_count += 1;
      }

      tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3 * draw_state_count);

      /* We may need to re-emit tess consts if the current draw call is
         * sufficiently larger than the last draw call. */
      if (has_tess)
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_TESS, tess_consts);
      if (cmd->state.dirty & TU_CMD_DIRTY_SHADER_CONSTS) {
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VS_CONST, cmd->state.shader_const[MESA_SHADER_VERTEX]);
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_HS_CONST, cmd->state.shader_const[MESA_SHADER_TESS_CTRL]);
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DS_CONST, cmd->state.shader_const[MESA_SHADER_TESS_EVAL]);
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_GS_CONST, cmd->state.shader_const[MESA_SHADER_GEOMETRY]);
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_FS_CONST, cmd->state.shader_const[MESA_SHADER_FRAGMENT]);
      }
      if (cmd->state.dirty & TU_CMD_DIRTY_DESC_SETS_LOAD)
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DESC_SETS_LOAD, pipeline->load_state);
      if (cmd->state.dirty & TU_CMD_DIRTY_VERTEX_BUFFERS)
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VB, cmd->state.vertex_buffers);
      if (emit_binding_stride) {
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DYNAMIC + TU_DYNAMIC_STATE_VB_STRIDE,
                               cmd->state.dynamic_state[TU_DYNAMIC_STATE_VB_STRIDE]);
      }
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VS_PARAMS, cmd->state.vs_params);

      if (cmd->state.dirty & TU_CMD_DIRTY_LRZ)
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_LRZ, cmd->state.lrz.state);
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

   if (pipeline->dynamic_state_mask & BIT(TU_DYNAMIC_STATE_PRIMITIVE_TOPOLOGY))
      primtype = cmd->state.primtype;

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

static struct tu_draw_state
tu6_emit_vs_params(struct tu_cmd_buffer *cmd,
                   uint32_t vertex_offset,
                   uint32_t first_instance)
{
   uint32_t offset = vs_params_offset(cmd);

   struct tu_cs cs;
   VkResult result = tu_cs_begin_sub_stream(&cmd->sub_cs, 3 + (offset ? 8 : 0), &cs);
   if (result != VK_SUCCESS) {
      cmd->record_result = result;
      return (struct tu_draw_state) {};
   }

   /* TODO: don't make a new draw state when it doesn't change */

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

      tu_cs_emit(&cs, 0);
      tu_cs_emit(&cs, vertex_offset);
      tu_cs_emit(&cs, first_instance);
      tu_cs_emit(&cs, 0);
   }

   struct tu_cs_entry entry = tu_cs_end_sub_stream(&cmd->sub_cs, &cs);
   return (struct tu_draw_state) {entry.bo->iova + entry.offset, entry.size / 4};
}

void
tu_CmdDraw(VkCommandBuffer commandBuffer,
           uint32_t vertexCount,
           uint32_t instanceCount,
           uint32_t firstVertex,
           uint32_t firstInstance)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   cmd->state.vs_params = tu6_emit_vs_params(cmd, firstVertex, firstInstance);

   tu6_draw_common(cmd, cs, false, vertexCount);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDX_OFFSET, 3);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_AUTO_INDEX));
   tu_cs_emit(cs, instanceCount);
   tu_cs_emit(cs, vertexCount);
}

void
tu_CmdDrawIndexed(VkCommandBuffer commandBuffer,
                  uint32_t indexCount,
                  uint32_t instanceCount,
                  uint32_t firstIndex,
                  int32_t vertexOffset,
                  uint32_t firstInstance)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *cs = &cmd->draw_cs;

   cmd->state.vs_params = tu6_emit_vs_params(cmd, vertexOffset, firstInstance);

   tu6_draw_common(cmd, cs, true, indexCount);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDX_OFFSET, 7);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_DMA));
   tu_cs_emit(cs, instanceCount);
   tu_cs_emit(cs, indexCount);
   tu_cs_emit(cs, firstIndex);
   tu_cs_emit_qw(cs, cmd->state.index_va);
   tu_cs_emit(cs, cmd->state.max_index_count);
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

void
tu_CmdDrawIndirect(VkCommandBuffer commandBuffer,
                   VkBuffer _buffer,
                   VkDeviceSize offset,
                   uint32_t drawCount,
                   uint32_t stride)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, buf, _buffer);
   struct tu_cs *cs = &cmd->draw_cs;

   cmd->state.vs_params = (struct tu_draw_state) {};

   /* The latest known a630_sqe.fw fails to wait for WFI before reading the
    * indirect buffer when using CP_DRAW_INDIRECT_MULTI, so we have to fall
    * back to CP_WAIT_FOR_ME except for a650 which has a fixed firmware.
    *
    * TODO: There may be newer a630_sqe.fw released in the future which fixes
    * this, if so we should detect it and avoid this workaround.
    */
   if (cmd->device->physical_device->gpu_id != 650)
      draw_wfm(cmd);

   tu6_draw_common(cmd, cs, false, 0);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDIRECT_MULTI, 6);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_AUTO_INDEX));
   tu_cs_emit(cs, A6XX_CP_DRAW_INDIRECT_MULTI_1_OPCODE(INDIRECT_OP_NORMAL) |
                  A6XX_CP_DRAW_INDIRECT_MULTI_1_DST_OFF(vs_params_offset(cmd)));
   tu_cs_emit(cs, drawCount);
   tu_cs_emit_qw(cs, buf->bo->iova + buf->bo_offset + offset);
   tu_cs_emit(cs, stride);
}

void
tu_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer,
                          VkBuffer _buffer,
                          VkDeviceSize offset,
                          uint32_t drawCount,
                          uint32_t stride)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, buf, _buffer);
   struct tu_cs *cs = &cmd->draw_cs;

   cmd->state.vs_params = (struct tu_draw_state) {};

   if (cmd->device->physical_device->gpu_id != 650)
      draw_wfm(cmd);

   tu6_draw_common(cmd, cs, true, 0);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDIRECT_MULTI, 9);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_DMA));
   tu_cs_emit(cs, A6XX_CP_DRAW_INDIRECT_MULTI_1_OPCODE(INDIRECT_OP_INDEXED) |
                  A6XX_CP_DRAW_INDIRECT_MULTI_1_DST_OFF(vs_params_offset(cmd)));
   tu_cs_emit(cs, drawCount);
   tu_cs_emit_qw(cs, cmd->state.index_va);
   tu_cs_emit(cs, cmd->state.max_index_count);
   tu_cs_emit_qw(cs, buf->bo->iova + buf->bo_offset + offset);
   tu_cs_emit(cs, stride);
}

void
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

   cmd->state.vs_params = (struct tu_draw_state) {};

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
   tu_cs_emit_qw(cs, buf->bo->iova + buf->bo_offset + offset);
   tu_cs_emit_qw(cs, count_buf->bo->iova + count_buf->bo_offset + countBufferOffset);
   tu_cs_emit(cs, stride);
}

void
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

   cmd->state.vs_params = (struct tu_draw_state) {};

   draw_wfm(cmd);

   tu6_draw_common(cmd, cs, true, 0);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDIRECT_MULTI, 11);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_DMA));
   tu_cs_emit(cs, A6XX_CP_DRAW_INDIRECT_MULTI_1_OPCODE(INDIRECT_OP_INDIRECT_COUNT_INDEXED) |
                  A6XX_CP_DRAW_INDIRECT_MULTI_1_DST_OFF(vs_params_offset(cmd)));
   tu_cs_emit(cs, drawCount);
   tu_cs_emit_qw(cs, cmd->state.index_va);
   tu_cs_emit(cs, cmd->state.max_index_count);
   tu_cs_emit_qw(cs, buf->bo->iova + buf->bo_offset + offset);
   tu_cs_emit_qw(cs, count_buf->bo->iova + count_buf->bo_offset + countBufferOffset);
   tu_cs_emit(cs, stride);
}

void tu_CmdDrawIndirectByteCountEXT(VkCommandBuffer commandBuffer,
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

   cmd->state.vs_params = tu6_emit_vs_params(cmd, 0, firstInstance);

   tu6_draw_common(cmd, cs, false, 0);

   tu_cs_emit_pkt7(cs, CP_DRAW_AUTO, 6);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_AUTO_XFB));
   tu_cs_emit(cs, instanceCount);
   tu_cs_emit_qw(cs, buf->bo->iova + buf->bo_offset + counterBufferOffset);
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

   if (link->constlen <= offset)
      return;

   if (!info->indirect) {
      uint32_t driver_params[4] = {
         [IR3_DP_NUM_WORK_GROUPS_X] = info->blocks[0],
         [IR3_DP_NUM_WORK_GROUPS_Y] = info->blocks[1],
         [IR3_DP_NUM_WORK_GROUPS_Z] = info->blocks[2],
      };

      uint32_t num_consts = MIN2(const_state->num_driver_params,
                                 (link->constlen - offset) * 4);
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
      tu_cs_emit_qw(cs, tu_buffer_iova(info->indirect) + info->indirect_offset);
   } else {
      /* Vulkan guarantees only 4 byte alignment for indirect_offset.
       * However, CP_LOAD_STATE.EXT_SRC_ADDR needs 16 byte alignment.
       */

      uint64_t indirect_iova = tu_buffer_iova(info->indirect) + info->indirect_offset;

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
}

static void
tu_dispatch(struct tu_cmd_buffer *cmd,
            const struct tu_dispatch_info *info)
{
   struct tu_cs *cs = &cmd->cs;
   struct tu_pipeline *pipeline = cmd->state.compute_pipeline;
   struct tu_descriptor_state *descriptors_state =
      &cmd->descriptors[VK_PIPELINE_BIND_POINT_COMPUTE];

   /* TODO: We could probably flush less if we add a compute_flush_bits
    * bitfield.
    */
   tu_emit_cache_flush(cmd, cs);

   /* note: no reason to have this in a separate IB */
   tu_cs_emit_state_ib(cs,
         tu6_emit_consts(cmd, pipeline, descriptors_state, MESA_SHADER_COMPUTE));

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

   if (info->indirect) {
      uint64_t iova = tu_buffer_iova(info->indirect) + info->indirect_offset;

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

   tu_cs_emit_wfi(cs);
}

void
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

void
tu_CmdDispatch(VkCommandBuffer commandBuffer,
               uint32_t x,
               uint32_t y,
               uint32_t z)
{
   tu_CmdDispatchBase(commandBuffer, 0, 0, 0, x, y, z);
}

void
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

void
tu_CmdEndRenderPass2(VkCommandBuffer commandBuffer,
                     const VkSubpassEndInfoKHR *pSubpassEndInfo)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);

   tu_cs_end(&cmd_buffer->draw_cs);
   tu_cs_end(&cmd_buffer->draw_epilogue_cs);

   if (use_sysmem_rendering(cmd_buffer))
      tu_cmd_render_sysmem(cmd_buffer);
   else
      tu_cmd_render_tiles(cmd_buffer);

   /* outside of renderpasses we assume all draw states are disabled
    * we can do this in the main cs because no resolve/store commands
    * should use a draw command (TODO: this will change if unaligned
    * GMEM stores are supported)
    */
   tu_disable_draw_states(cmd_buffer, &cmd_buffer->cs);

   /* discard draw_cs and draw_epilogue_cs entries now that the tiles are
      rendered */
   tu_cs_discard_entries(&cmd_buffer->draw_cs);
   tu_cs_begin(&cmd_buffer->draw_cs);
   tu_cs_discard_entries(&cmd_buffer->draw_epilogue_cs);
   tu_cs_begin(&cmd_buffer->draw_epilogue_cs);

   cmd_buffer->state.cache.pending_flush_bits |=
      cmd_buffer->state.renderpass_cache.pending_flush_bits;
   tu_subpass_barrier(cmd_buffer, &cmd_buffer->state.pass->end_barrier, true);

   cmd_buffer->state.pass = NULL;
   cmd_buffer->state.subpass = NULL;
   cmd_buffer->state.framebuffer = NULL;
   cmd_buffer->state.has_tess = false;
   cmd_buffer->state.has_subpass_predication = false;

   /* LRZ is not valid next time we use it */
   cmd_buffer->state.lrz.valid = false;
   cmd_buffer->state.dirty |= TU_CMD_DIRTY_LRZ;
}

struct tu_barrier_info
{
   uint32_t eventCount;
   const VkEvent *pEvents;
   VkPipelineStageFlags srcStageMask;
};

static void
tu_barrier(struct tu_cmd_buffer *cmd,
           uint32_t memoryBarrierCount,
           const VkMemoryBarrier *pMemoryBarriers,
           uint32_t bufferMemoryBarrierCount,
           const VkBufferMemoryBarrier *pBufferMemoryBarriers,
           uint32_t imageMemoryBarrierCount,
           const VkImageMemoryBarrier *pImageMemoryBarriers,
           const struct tu_barrier_info *info)
{
   struct tu_cs *cs = cmd->state.pass ? &cmd->draw_cs : &cmd->cs;
   VkAccessFlags srcAccessMask = 0;
   VkAccessFlags dstAccessMask = 0;

   for (uint32_t i = 0; i < memoryBarrierCount; i++) {
      srcAccessMask |= pMemoryBarriers[i].srcAccessMask;
      dstAccessMask |= pMemoryBarriers[i].dstAccessMask;
   }

   for (uint32_t i = 0; i < bufferMemoryBarrierCount; i++) {
      srcAccessMask |= pBufferMemoryBarriers[i].srcAccessMask;
      dstAccessMask |= pBufferMemoryBarriers[i].dstAccessMask;
   }

   enum tu_cmd_access_mask src_flags = 0;
   enum tu_cmd_access_mask dst_flags = 0;

   for (uint32_t i = 0; i < imageMemoryBarrierCount; i++) {
      VkImageLayout old_layout = pImageMemoryBarriers[i].oldLayout;
      if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED) {
         /* The underlying memory for this image may have been used earlier
          * within the same queue submission for a different image, which
          * means that there may be old, stale cache entries which are in the
          * "wrong" location, which could cause problems later after writing
          * to the image. We don't want these entries being flushed later and
          * overwriting the actual image, so we need to flush the CCU.
          */
         src_flags |= TU_ACCESS_CCU_COLOR_INCOHERENT_WRITE;
      }
      srcAccessMask |= pImageMemoryBarriers[i].srcAccessMask;
      dstAccessMask |= pImageMemoryBarriers[i].dstAccessMask;
   }

   /* Inside a renderpass, we don't know yet whether we'll be using sysmem
    * so we have to use the sysmem flushes.
    */
   bool gmem = cmd->state.ccu_state == TU_CMD_CCU_GMEM &&
      !cmd->state.pass;
   src_flags |= vk2tu_access(srcAccessMask, gmem);
   dst_flags |= vk2tu_access(dstAccessMask, gmem);

   struct tu_cache_state *cache =
      cmd->state.pass  ? &cmd->state.renderpass_cache : &cmd->state.cache;
   tu_flush_for_access(cache, src_flags, dst_flags);

   for (uint32_t i = 0; i < info->eventCount; i++) {
      TU_FROM_HANDLE(tu_event, event, info->pEvents[i]);

      tu_cs_emit_pkt7(cs, CP_WAIT_REG_MEM, 6);
      tu_cs_emit(cs, CP_WAIT_REG_MEM_0_FUNCTION(WRITE_EQ) |
                     CP_WAIT_REG_MEM_0_POLL_MEMORY);
      tu_cs_emit_qw(cs, event->bo.iova); /* POLL_ADDR_LO/HI */
      tu_cs_emit(cs, CP_WAIT_REG_MEM_3_REF(1));
      tu_cs_emit(cs, CP_WAIT_REG_MEM_4_MASK(~0u));
      tu_cs_emit(cs, CP_WAIT_REG_MEM_5_DELAY_LOOP_CYCLES(20));
   }
}

void
tu_CmdPipelineBarrier(VkCommandBuffer commandBuffer,
                      VkPipelineStageFlags srcStageMask,
                      VkPipelineStageFlags dstStageMask,
                      VkDependencyFlags dependencyFlags,
                      uint32_t memoryBarrierCount,
                      const VkMemoryBarrier *pMemoryBarriers,
                      uint32_t bufferMemoryBarrierCount,
                      const VkBufferMemoryBarrier *pBufferMemoryBarriers,
                      uint32_t imageMemoryBarrierCount,
                      const VkImageMemoryBarrier *pImageMemoryBarriers)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   struct tu_barrier_info info;

   info.eventCount = 0;
   info.pEvents = NULL;
   info.srcStageMask = srcStageMask;

   tu_barrier(cmd_buffer, memoryBarrierCount, pMemoryBarriers,
              bufferMemoryBarrierCount, pBufferMemoryBarriers,
              imageMemoryBarrierCount, pImageMemoryBarriers, &info);
}

static void
write_event(struct tu_cmd_buffer *cmd, struct tu_event *event,
            VkPipelineStageFlags stageMask, unsigned value)
{
   struct tu_cs *cs = &cmd->cs;

   /* vkCmdSetEvent/vkCmdResetEvent cannot be called inside a render pass */
   assert(!cmd->state.pass);

   tu_emit_cache_flush(cmd, cs);

   /* Flags that only require a top-of-pipe event. DrawIndirect parameters are
    * read by the CP, so the draw indirect stage counts as top-of-pipe too.
    */
   VkPipelineStageFlags top_of_pipe_flags =
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT |
      VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT;

   if (!(stageMask & ~top_of_pipe_flags)) {
      tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 3);
      tu_cs_emit_qw(cs, event->bo.iova); /* ADDR_LO/HI */
      tu_cs_emit(cs, value);
   } else {
      /* Use a RB_DONE_TS event to wait for everything to complete. */
      tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 4);
      tu_cs_emit(cs, CP_EVENT_WRITE_0_EVENT(RB_DONE_TS));
      tu_cs_emit_qw(cs, event->bo.iova);
      tu_cs_emit(cs, value);
   }
}

void
tu_CmdSetEvent(VkCommandBuffer commandBuffer,
               VkEvent _event,
               VkPipelineStageFlags stageMask)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_event, event, _event);

   write_event(cmd, event, stageMask, 1);
}

void
tu_CmdResetEvent(VkCommandBuffer commandBuffer,
                 VkEvent _event,
                 VkPipelineStageFlags stageMask)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_event, event, _event);

   write_event(cmd, event, stageMask, 0);
}

void
tu_CmdWaitEvents(VkCommandBuffer commandBuffer,
                 uint32_t eventCount,
                 const VkEvent *pEvents,
                 VkPipelineStageFlags srcStageMask,
                 VkPipelineStageFlags dstStageMask,
                 uint32_t memoryBarrierCount,
                 const VkMemoryBarrier *pMemoryBarriers,
                 uint32_t bufferMemoryBarrierCount,
                 const VkBufferMemoryBarrier *pBufferMemoryBarriers,
                 uint32_t imageMemoryBarrierCount,
                 const VkImageMemoryBarrier *pImageMemoryBarriers)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_barrier_info info;

   info.eventCount = eventCount;
   info.pEvents = pEvents;
   info.srcStageMask = 0;

   tu_barrier(cmd, memoryBarrierCount, pMemoryBarriers,
              bufferMemoryBarrierCount, pBufferMemoryBarriers,
              imageMemoryBarrierCount, pImageMemoryBarriers, &info);
}

void
tu_CmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask)
{
   /* No-op */
}


void
tu_CmdBeginConditionalRenderingEXT(VkCommandBuffer commandBuffer,
                                   const VkConditionalRenderingBeginInfoEXT *pConditionalRenderingBegin)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.predication_active = true;
   if (cmd->state.pass)
      cmd->state.has_subpass_predication = true;

   struct tu_cs *cs = cmd->state.pass ? &cmd->draw_cs : &cmd->cs;

   tu_cs_emit_pkt7(cs, CP_DRAW_PRED_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 1);

   /* Wait for any writes to the predicate to land */
   if (cmd->state.pass)
      tu_emit_cache_flush_renderpass(cmd, cs);
   else
      tu_emit_cache_flush(cmd, cs);

   TU_FROM_HANDLE(tu_buffer, buf, pConditionalRenderingBegin->buffer);
   uint64_t iova = tu_buffer_iova(buf) + pConditionalRenderingBegin->offset;

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

void
tu_CmdEndConditionalRenderingEXT(VkCommandBuffer commandBuffer)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.predication_active = false;

   struct tu_cs *cs = cmd->state.pass ? &cmd->draw_cs : &cmd->cs;

   tu_cs_emit_pkt7(cs, CP_DRAW_PRED_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0);
}

