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

#include "registers/adreno_pm4.xml.h"
#include "registers/adreno_common.xml.h"

#include "vk_format.h"

#include "tu_cs.h"

void
tu_bo_list_init(struct tu_bo_list *list)
{
   list->count = list->capacity = 0;
   list->bo_infos = NULL;
}

void
tu_bo_list_destroy(struct tu_bo_list *list)
{
   free(list->bo_infos);
}

void
tu_bo_list_reset(struct tu_bo_list *list)
{
   list->count = 0;
}

/**
 * \a flags consists of MSM_SUBMIT_BO_FLAGS.
 */
static uint32_t
tu_bo_list_add_info(struct tu_bo_list *list,
                    const struct drm_msm_gem_submit_bo *bo_info)
{
   assert(bo_info->handle != 0);

   for (uint32_t i = 0; i < list->count; ++i) {
      if (list->bo_infos[i].handle == bo_info->handle) {
         assert(list->bo_infos[i].presumed == bo_info->presumed);
         list->bo_infos[i].flags |= bo_info->flags;
         return i;
      }
   }

   /* grow list->bo_infos if needed */
   if (list->count == list->capacity) {
      uint32_t new_capacity = MAX2(2 * list->count, 16);
      struct drm_msm_gem_submit_bo *new_bo_infos = realloc(
         list->bo_infos, new_capacity * sizeof(struct drm_msm_gem_submit_bo));
      if (!new_bo_infos)
         return TU_BO_LIST_FAILED;
      list->bo_infos = new_bo_infos;
      list->capacity = new_capacity;
   }

   list->bo_infos[list->count] = *bo_info;
   return list->count++;
}

uint32_t
tu_bo_list_add(struct tu_bo_list *list,
               const struct tu_bo *bo,
               uint32_t flags)
{
   return tu_bo_list_add_info(list, &(struct drm_msm_gem_submit_bo) {
                                       .flags = flags,
                                       .handle = bo->gem_handle,
                                       .presumed = bo->iova,
                                    });
}

VkResult
tu_bo_list_merge(struct tu_bo_list *list, const struct tu_bo_list *other)
{
   for (uint32_t i = 0; i < other->count; i++) {
      if (tu_bo_list_add_info(list, other->bo_infos + i) == TU_BO_LIST_FAILED)
         return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   return VK_SUCCESS;
}

static void
tu_tiling_config_update_tile_layout(struct tu_tiling_config *tiling,
                                    const struct tu_device *dev,
                                    const struct tu_render_pass *pass)
{
   const uint32_t tile_align_w = pass->tile_align_w;
   const uint32_t max_tile_width = 1024;

   /* note: don't offset the tiling config by render_area.offset,
    * because binning pass can't deal with it
    * this means we might end up with more tiles than necessary,
    * but load/store/etc are still scissored to the render_area
    */
   tiling->tile0.offset = (VkOffset2D) {};

   const uint32_t ra_width =
      tiling->render_area.extent.width +
      (tiling->render_area.offset.x - tiling->tile0.offset.x);
   const uint32_t ra_height =
      tiling->render_area.extent.height +
      (tiling->render_area.offset.y - tiling->tile0.offset.y);

   /* start from 1 tile */
   tiling->tile_count = (VkExtent2D) {
      .width = 1,
      .height = 1,
   };
   tiling->tile0.extent = (VkExtent2D) {
      .width = util_align_npot(ra_width, tile_align_w),
      .height = align(ra_height, TILE_ALIGN_H),
   };

   if (unlikely(dev->physical_device->instance->debug_flags & TU_DEBUG_FORCEBIN)) {
      /* start with 2x2 tiles */
      tiling->tile_count.width = 2;
      tiling->tile_count.height = 2;
      tiling->tile0.extent.width = util_align_npot(DIV_ROUND_UP(ra_width, 2), tile_align_w);
      tiling->tile0.extent.height = align(DIV_ROUND_UP(ra_height, 2), TILE_ALIGN_H);
   }

   /* do not exceed max tile width */
   while (tiling->tile0.extent.width > max_tile_width) {
      tiling->tile_count.width++;
      tiling->tile0.extent.width =
         util_align_npot(DIV_ROUND_UP(ra_width, tiling->tile_count.width), tile_align_w);
   }

   /* will force to sysmem, don't bother trying to have a valid tile config
    * TODO: just skip all GMEM stuff when sysmem is forced?
    */
   if (!pass->gmem_pixels)
      return;

   /* do not exceed gmem size */
   while (tiling->tile0.extent.width * tiling->tile0.extent.height > pass->gmem_pixels) {
      if (tiling->tile0.extent.width > MAX2(tile_align_w, tiling->tile0.extent.height)) {
         tiling->tile_count.width++;
         tiling->tile0.extent.width =
            util_align_npot(DIV_ROUND_UP(ra_width, tiling->tile_count.width), tile_align_w);
      } else {
         /* if this assert fails then layout is impossible.. */
         assert(tiling->tile0.extent.height > TILE_ALIGN_H);
         tiling->tile_count.height++;
         tiling->tile0.extent.height =
            align(DIV_ROUND_UP(ra_height, tiling->tile_count.height), TILE_ALIGN_H);
      }
   }
}

static void
tu_tiling_config_update_pipe_layout(struct tu_tiling_config *tiling,
                                    const struct tu_device *dev)
{
   const uint32_t max_pipe_count = 32; /* A6xx */

   /* start from 1 tile per pipe */
   tiling->pipe0 = (VkExtent2D) {
      .width = 1,
      .height = 1,
   };
   tiling->pipe_count = tiling->tile_count;

   while (tiling->pipe_count.width * tiling->pipe_count.height > max_pipe_count) {
      if (tiling->pipe0.width < tiling->pipe0.height) {
         tiling->pipe0.width += 1;
         tiling->pipe_count.width =
            DIV_ROUND_UP(tiling->tile_count.width, tiling->pipe0.width);
      } else {
         tiling->pipe0.height += 1;
         tiling->pipe_count.height =
            DIV_ROUND_UP(tiling->tile_count.height, tiling->pipe0.height);
      }
   }
}

static void
tu_tiling_config_update_pipes(struct tu_tiling_config *tiling,
                              const struct tu_device *dev)
{
   const uint32_t max_pipe_count = 32; /* A6xx */
   const uint32_t used_pipe_count =
      tiling->pipe_count.width * tiling->pipe_count.height;
   const VkExtent2D last_pipe = {
      .width = (tiling->tile_count.width - 1) % tiling->pipe0.width + 1,
      .height = (tiling->tile_count.height - 1) % tiling->pipe0.height + 1,
   };

   assert(used_pipe_count <= max_pipe_count);
   assert(max_pipe_count <= ARRAY_SIZE(tiling->pipe_config));

   for (uint32_t y = 0; y < tiling->pipe_count.height; y++) {
      for (uint32_t x = 0; x < tiling->pipe_count.width; x++) {
         const uint32_t pipe_x = tiling->pipe0.width * x;
         const uint32_t pipe_y = tiling->pipe0.height * y;
         const uint32_t pipe_w = (x == tiling->pipe_count.width - 1)
                                    ? last_pipe.width
                                    : tiling->pipe0.width;
         const uint32_t pipe_h = (y == tiling->pipe_count.height - 1)
                                    ? last_pipe.height
                                    : tiling->pipe0.height;
         const uint32_t n = tiling->pipe_count.width * y + x;

         tiling->pipe_config[n] = A6XX_VSC_PIPE_CONFIG_REG_X(pipe_x) |
                                  A6XX_VSC_PIPE_CONFIG_REG_Y(pipe_y) |
                                  A6XX_VSC_PIPE_CONFIG_REG_W(pipe_w) |
                                  A6XX_VSC_PIPE_CONFIG_REG_H(pipe_h);
         tiling->pipe_sizes[n] = CP_SET_BIN_DATA5_0_VSC_SIZE(pipe_w * pipe_h);
      }
   }

   memset(tiling->pipe_config + used_pipe_count, 0,
          sizeof(uint32_t) * (max_pipe_count - used_pipe_count));
}

static void
tu_tiling_config_get_tile(const struct tu_tiling_config *tiling,
                          const struct tu_device *dev,
                          uint32_t tx,
                          uint32_t ty,
                          struct tu_tile *tile)
{
   /* find the pipe and the slot for tile (tx, ty) */
   const uint32_t px = tx / tiling->pipe0.width;
   const uint32_t py = ty / tiling->pipe0.height;
   const uint32_t sx = tx - tiling->pipe0.width * px;
   const uint32_t sy = ty - tiling->pipe0.height * py;
   /* last pipe has different width */
   const uint32_t pipe_width =
      MIN2(tiling->pipe0.width,
           tiling->tile_count.width - px * tiling->pipe0.width);

   assert(tx < tiling->tile_count.width && ty < tiling->tile_count.height);
   assert(px < tiling->pipe_count.width && py < tiling->pipe_count.height);
   assert(sx < tiling->pipe0.width && sy < tiling->pipe0.height);

   /* convert to 1D indices */
   tile->pipe = tiling->pipe_count.width * py + px;
   tile->slot = pipe_width * sy + sx;

   /* get the blit area for the tile */
   tile->begin = (VkOffset2D) {
      .x = tiling->tile0.offset.x + tiling->tile0.extent.width * tx,
      .y = tiling->tile0.offset.y + tiling->tile0.extent.height * ty,
   };
   tile->end.x =
      (tx == tiling->tile_count.width - 1)
         ? tiling->render_area.offset.x + tiling->render_area.extent.width
         : tile->begin.x + tiling->tile0.extent.width;
   tile->end.y =
      (ty == tiling->tile_count.height - 1)
         ? tiling->render_area.offset.y + tiling->render_area.extent.height
         : tile->begin.y + tiling->tile0.extent.height;
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
      tu_cs_emit_qw(cs, cmd->scratch_bo.iova);
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
   if (flushes & TU_CMD_FLAG_WFI)
      tu_cs_emit_wfi(cs);
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
         TU_CMD_FLAG_WFI;
      cmd_buffer->state.cache.pending_flush_bits &= ~(
         TU_CMD_FLAG_CCU_INVALIDATE_COLOR |
         TU_CMD_FLAG_CCU_INVALIDATE_DEPTH);
   }

   tu6_emit_flushes(cmd_buffer, cs, flushes);
   cmd_buffer->state.cache.flush_bits = 0;

   if (ccu_state != cmd_buffer->state.ccu_state) {
      struct tu_physical_device *phys_dev = cmd_buffer->device->physical_device;
      tu_cs_emit_regs(cs,
                      A6XX_RB_CCU_CNTL(.offset =
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

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_DEPTH_FLAG_BUFFER_BASE_LO, 3);
   tu_cs_image_flag_ref(cs, iview, 0);

   tu_cs_emit_regs(cs,
                   A6XX_GRAS_LRZ_BUFFER_BASE(0),
                   A6XX_GRAS_LRZ_BUFFER_PITCH(0),
                   A6XX_GRAS_LRZ_FAST_CLEAR_BUFFER_BASE(0));

   if (attachment->format == VK_FORMAT_S8_UINT) {
      tu_cs_emit_pkt4(cs, REG_A6XX_RB_STENCIL_INFO, 6);
      tu_cs_emit(cs, A6XX_RB_STENCIL_INFO(.separate_stencil = true).value);
      tu_cs_image_ref(cs, iview, 0);
      tu_cs_emit(cs, attachment->gmem_offset);
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

      tu_cs_emit_pkt4(cs, REG_A6XX_RB_MRT_FLAG_BUFFER_ADDR_LO(i), 3);
      tu_cs_image_flag_ref(cs, iview, 0);
   }

   tu_cs_emit_regs(cs,
                   A6XX_RB_SRGB_CNTL(.dword = subpass->srgb_cntl));
   tu_cs_emit_regs(cs,
                   A6XX_SP_SRGB_CNTL(.dword = subpass->srgb_cntl));

   tu_cs_emit_regs(cs, A6XX_GRAS_MAX_LAYER_INDEX(fb->layers - 1));
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
   const VkRect2D *render_area = &cmd->state.tiling_config.render_area;
   uint32_t x1 = render_area->offset.x;
   uint32_t y1 = render_area->offset.y;
   uint32_t x2 = x1 + render_area->extent.width - 1;
   uint32_t y2 = y1 + render_area->extent.height - 1;

   if (align) {
      x1 = x1 & ~(GMEM_ALIGN_W - 1);
      y1 = y1 & ~(GMEM_ALIGN_H - 1);
      x2 = ALIGN_POT(x2 + 1, GMEM_ALIGN_W) - 1;
      y2 = ALIGN_POT(y2 + 1, GMEM_ALIGN_H) - 1;
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
                   A6XX_GRAS_RESOLVE_CNTL_1(.x = x1, .y = y1),
                   A6XX_GRAS_RESOLVE_CNTL_2(.x = x2, .y = y2));
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

   tu_cs_emit(cs, CP_SET_DRAW_STATE__0_COUNT(state.size) |
                  enable_mask |
                  CP_SET_DRAW_STATE__0_GROUP_ID(id) |
                  COND(!state.size, CP_SET_DRAW_STATE__0_DISABLE));
   tu_cs_emit_qw(cs, state.iova);
}

/* note: get rid of this eventually */
static void
tu_cs_emit_sds_ib(struct tu_cs *cs, uint32_t id, struct tu_cs_entry entry)
{
   tu_cs_emit_draw_state(cs, id, (struct tu_draw_state) {
      .iova = entry.size ? entry.bo->iova + entry.offset : 0,
      .size = entry.size / 4,
   });
}

static bool
use_hw_binning(struct tu_cmd_buffer *cmd)
{
   const struct tu_tiling_config *tiling = &cmd->state.tiling_config;

   /* XFB commands are emitted for BINNING || SYSMEM, which makes it incompatible
    * with non-hw binning GMEM rendering. this is required because some of the
    * XFB commands need to only be executed once
    */
   if (cmd->state.xfb_used)
      return true;

   if (unlikely(cmd->device->physical_device->instance->debug_flags & TU_DEBUG_NOBIN))
      return false;

   if (unlikely(cmd->device->physical_device->instance->debug_flags & TU_DEBUG_FORCEBIN))
      return true;

   return (tiling->tile_count.width * tiling->tile_count.height) > 2;
}

static bool
use_sysmem_rendering(struct tu_cmd_buffer *cmd)
{
   if (unlikely(cmd->device->physical_device->instance->debug_flags & TU_DEBUG_SYSMEM))
      return true;

   /* can't fit attachments into gmem */
   if (!cmd->state.pass->gmem_pixels)
      return true;

   if (cmd->state.framebuffer->layers > 1)
      return true;

   if (cmd->has_tess)
      return true;

   return cmd->state.tiling_config.force_sysmem;
}

static void
tu6_emit_tile_select(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     const struct tu_tile *tile)
{
   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_YIELD));

   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_GMEM));

   const uint32_t x1 = tile->begin.x;
   const uint32_t y1 = tile->begin.y;
   const uint32_t x2 = tile->end.x - 1;
   const uint32_t y2 = tile->end.y - 1;
   tu6_emit_window_scissor(cs, x1, y1, x2, y2);
   tu6_emit_window_offset(cs, x1, y1);

   tu_cs_emit_regs(cs,
                   A6XX_VPC_SO_OVERRIDE(.so_disable = false));

   if (use_hw_binning(cmd)) {
      tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);

      tu_cs_emit_pkt7(cs, CP_SET_MODE, 1);
      tu_cs_emit(cs, 0x0);

      tu_cs_emit_pkt7(cs, CP_SET_BIN_DATA5, 7);
      tu_cs_emit(cs, cmd->state.tiling_config.pipe_sizes[tile->pipe] |
                     CP_SET_BIN_DATA5_0_VSC_N(tile->slot));
      tu_cs_emit_qw(cs, cmd->vsc_draw_strm.iova + tile->pipe * cmd->vsc_draw_strm_pitch);
      tu_cs_emit_qw(cs, cmd->vsc_draw_strm.iova + (tile->pipe * 4) + (32 * cmd->vsc_draw_strm_pitch));
      tu_cs_emit_qw(cs, cmd->vsc_prim_strm.iova + (tile->pipe * cmd->vsc_prim_strm_pitch));

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
                        uint32_t a,
                        uint32_t gmem_a)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   struct tu_image_view *dst = fb->attachments[a].attachment;
   struct tu_image_view *src = fb->attachments[gmem_a].attachment;

   tu_resolve_sysmem(cmd, cs, src, dst, fb->layers, &cmd->state.tiling_config.render_area);
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
       *    attachment writes for the purposes of synchronization. That is,
       *    they are considered to execute in the
       *    VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT pipeline stage and
       *    their writes are synchronized with
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
      tu6_emit_event_write(cmd, cs, CACHE_INVALIDATE);

      /* Wait for the flushes to land before using the 2D engine */
      tu_cs_emit_wfi(cs);

      for (unsigned i = 0; i < subpass->color_count; i++) {
         uint32_t a = subpass->resolve_attachments[i].attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;

         tu6_emit_sysmem_resolve(cmd, cs, a,
                                 subpass->color_attachments[i].attachment);
      }
   }
}

static void
tu6_emit_tile_store(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   const struct tu_render_pass *pass = cmd->state.pass;
   const struct tu_subpass *subpass = &pass->subpasses[pass->subpass_count-1];

   tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3);
   tu_cs_emit(cs, CP_SET_DRAW_STATE__0_COUNT(0) |
                     CP_SET_DRAW_STATE__0_DISABLE_ALL_GROUPS |
                     CP_SET_DRAW_STATE__0_GROUP_ID(0));
   tu_cs_emit(cs, CP_SET_DRAW_STATE__1_ADDR_LO(0));
   tu_cs_emit(cs, CP_SET_DRAW_STATE__2_ADDR_HI(0));

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
      for (unsigned i = 0; i < subpass->color_count; i++) {
         uint32_t a = subpass->resolve_attachments[i].attachment;
         if (a != VK_ATTACHMENT_UNUSED)
            tu_store_gmem_attachment(cmd, cs, a,
                                     subpass->color_attachments[i].attachment);
      }
   }
}

static void
tu6_init_hw(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   const struct tu_physical_device *phys_dev = cmd->device->physical_device;

   tu6_emit_event_write(cmd, cs, CACHE_INVALIDATE);

   tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_UPDATE_CNTL, 0xfffff);

   tu_cs_emit_regs(cs,
                   A6XX_RB_CCU_CNTL(.offset = phys_dev->ccu_offset_bypass));
   cmd->state.ccu_state = TU_CMD_CCU_SYSMEM;
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8E04, 0x00100000);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_AE04, 0x8);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_AE00, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_AE0F, 0x3f);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_B605, 0x44);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_B600, 0x100000);
   tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_UNKNOWN_BE00, 0x80);
   tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_UNKNOWN_BE01, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9600, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_UNKNOWN_8600, 0x880);
   tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_UNKNOWN_BE04, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_AE03, 0x00000410);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_IBO_COUNT, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_B182, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_UNKNOWN_BB11, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_UCHE_UNKNOWN_0E12, 0x3200000);
   tu_cs_emit_write_reg(cs, REG_A6XX_UCHE_CLIENT_PF, 4);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8E01, 0x0);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_A982, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_A9A8, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_AB00, 0x5);
   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_GS_SIV_CNTL, 0x0000ffff);

   /* TODO: set A6XX_VFD_ADD_OFFSET_INSTANCE and fix ir3 to avoid adding base instance */
   tu_cs_emit_write_reg(cs, REG_A6XX_VFD_ADD_OFFSET, A6XX_VFD_ADD_OFFSET_VERTEX);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8811, 0x00000010);
   tu_cs_emit_write_reg(cs, REG_A6XX_PC_MODE_CNTL, 0x1f);

   tu_cs_emit_write_reg(cs, REG_A6XX_RB_SRGB_CNTL, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_UNKNOWN_8110, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_RB_RENDER_CONTROL0, 0x401);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_RENDER_CONTROL1, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_FS_OUTPUT_CNTL0, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8818, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8819, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_881A, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_881B, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_881C, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_881D, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_881E, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_88F0, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9101, 0xffff00);
   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9107, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9236,
                        A6XX_VPC_UNKNOWN_9236_POINT_COORD_INVERT(0));
   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9300, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_SO_OVERRIDE,
                        A6XX_VPC_SO_OVERRIDE_SO_DISABLE);

   tu_cs_emit_write_reg(cs, REG_A6XX_PC_UNKNOWN_9801, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_PC_UNKNOWN_9980, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_PC_UNKNOWN_9990, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_PC_PRIMITIVE_CNTL_6, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_PC_UNKNOWN_9B07, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_A81B, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_B183, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_UNKNOWN_8099, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_UNKNOWN_809B, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_UNKNOWN_80A0, 2);
   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_UNKNOWN_80AF, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9210, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9211, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9602, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_PC_UNKNOWN_9E72, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_TP_UNKNOWN_B309, 0x000000a2);
   tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_CONTROL_5_REG, 0xfc);

   tu_cs_emit_write_reg(cs, REG_A6XX_VFD_MODE_CNTL, 0x00000000);

   tu_cs_emit_write_reg(cs, REG_A6XX_VFD_UNKNOWN_A008, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_PC_MODE_CNTL, 0x0000001f);

   /* we don't use this yet.. probably best to disable.. */
   tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3);
   tu_cs_emit(cs, CP_SET_DRAW_STATE__0_COUNT(0) |
                     CP_SET_DRAW_STATE__0_DISABLE_ALL_GROUPS |
                     CP_SET_DRAW_STATE__0_GROUP_ID(0));
   tu_cs_emit(cs, CP_SET_DRAW_STATE__1_ADDR_LO(0));
   tu_cs_emit(cs, CP_SET_DRAW_STATE__2_ADDR_HI(0));

   tu_cs_emit_regs(cs,
                   A6XX_SP_HS_CTRL_REG0(0));

   tu_cs_emit_regs(cs,
                   A6XX_SP_GS_CTRL_REG0(0));

   tu_cs_emit_regs(cs,
                   A6XX_GRAS_LRZ_CNTL(0));

   tu_cs_emit_regs(cs,
                   A6XX_RB_LRZ_CNTL(0));

   tu_cs_emit_regs(cs,
                   A6XX_SP_TP_BORDER_COLOR_BASE_ADDR(.bo = &cmd->device->border_color));
   tu_cs_emit_regs(cs,
                   A6XX_SP_PS_TP_BORDER_COLOR_BASE_ADDR(.bo = &cmd->device->border_color));

   tu_cs_sanity_check(cs);
}

static void
update_vsc_pipe(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   const struct tu_tiling_config *tiling = &cmd->state.tiling_config;

   tu_cs_emit_regs(cs,
                   A6XX_VSC_BIN_SIZE(.width = tiling->tile0.extent.width,
                                     .height = tiling->tile0.extent.height),
                   A6XX_VSC_DRAW_STRM_SIZE_ADDRESS(.bo = &cmd->vsc_draw_strm,
                                                   .bo_offset = 32 * cmd->vsc_draw_strm_pitch));

   tu_cs_emit_regs(cs,
                   A6XX_VSC_BIN_COUNT(.nx = tiling->tile_count.width,
                                      .ny = tiling->tile_count.height));

   tu_cs_emit_pkt4(cs, REG_A6XX_VSC_PIPE_CONFIG_REG(0), 32);
   for (unsigned i = 0; i < 32; i++)
      tu_cs_emit(cs, tiling->pipe_config[i]);

   tu_cs_emit_regs(cs,
                   A6XX_VSC_PRIM_STRM_ADDRESS(.bo = &cmd->vsc_prim_strm),
                   A6XX_VSC_PRIM_STRM_PITCH(cmd->vsc_prim_strm_pitch),
                   A6XX_VSC_PRIM_STRM_LIMIT(cmd->vsc_prim_strm_pitch - 64));

   tu_cs_emit_regs(cs,
                   A6XX_VSC_DRAW_STRM_ADDRESS(.bo = &cmd->vsc_draw_strm),
                   A6XX_VSC_DRAW_STRM_PITCH(cmd->vsc_draw_strm_pitch),
                   A6XX_VSC_DRAW_STRM_LIMIT(cmd->vsc_draw_strm_pitch - 64));
}

static void
emit_vsc_overflow_test(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   const struct tu_tiling_config *tiling = &cmd->state.tiling_config;
   const uint32_t used_pipe_count =
      tiling->pipe_count.width * tiling->pipe_count.height;

   /* Clear vsc_scratch: */
   tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 3);
   tu_cs_emit_qw(cs, cmd->scratch_bo.iova + ctrl_offset(vsc_scratch));
   tu_cs_emit(cs, 0x0);

   /* Check for overflow, write vsc_scratch if detected: */
   for (int i = 0; i < used_pipe_count; i++) {
      tu_cs_emit_pkt7(cs, CP_COND_WRITE5, 8);
      tu_cs_emit(cs, CP_COND_WRITE5_0_FUNCTION(WRITE_GE) |
            CP_COND_WRITE5_0_WRITE_MEMORY);
      tu_cs_emit(cs, CP_COND_WRITE5_1_POLL_ADDR_LO(REG_A6XX_VSC_DRAW_STRM_SIZE_REG(i)));
      tu_cs_emit(cs, CP_COND_WRITE5_2_POLL_ADDR_HI(0));
      tu_cs_emit(cs, CP_COND_WRITE5_3_REF(cmd->vsc_draw_strm_pitch - 64));
      tu_cs_emit(cs, CP_COND_WRITE5_4_MASK(~0));
      tu_cs_emit_qw(cs, cmd->scratch_bo.iova + ctrl_offset(vsc_scratch));
      tu_cs_emit(cs, CP_COND_WRITE5_7_WRITE_DATA(1 + cmd->vsc_draw_strm_pitch));

      tu_cs_emit_pkt7(cs, CP_COND_WRITE5, 8);
      tu_cs_emit(cs, CP_COND_WRITE5_0_FUNCTION(WRITE_GE) |
            CP_COND_WRITE5_0_WRITE_MEMORY);
      tu_cs_emit(cs, CP_COND_WRITE5_1_POLL_ADDR_LO(REG_A6XX_VSC_PRIM_STRM_SIZE_REG(i)));
      tu_cs_emit(cs, CP_COND_WRITE5_2_POLL_ADDR_HI(0));
      tu_cs_emit(cs, CP_COND_WRITE5_3_REF(cmd->vsc_prim_strm_pitch - 64));
      tu_cs_emit(cs, CP_COND_WRITE5_4_MASK(~0));
      tu_cs_emit_qw(cs, cmd->scratch_bo.iova + ctrl_offset(vsc_scratch));
      tu_cs_emit(cs, CP_COND_WRITE5_7_WRITE_DATA(3 + cmd->vsc_prim_strm_pitch));
   }

   tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);
}

static void
tu6_emit_binning_pass(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   struct tu_physical_device *phys_dev = cmd->device->physical_device;
   const struct tu_tiling_config *tiling = &cmd->state.tiling_config;

   uint32_t x1 = tiling->tile0.offset.x;
   uint32_t y1 = tiling->tile0.offset.y;
   uint32_t x2 = tiling->render_area.offset.x + tiling->render_area.extent.width - 1;
   uint32_t y2 = tiling->render_area.offset.y + tiling->render_area.extent.height - 1;

   tu6_emit_window_scissor(cs, x1, y1, x2, y2);

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
                   A6XX_PC_UNKNOWN_9805(.unknown = phys_dev->magic.PC_UNKNOWN_9805));

   tu_cs_emit_regs(cs,
                   A6XX_SP_UNKNOWN_A0F8(.unknown = phys_dev->magic.SP_UNKNOWN_A0F8));

   tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 1);
   tu_cs_emit(cs, UNK_2C);

   tu_cs_emit_regs(cs,
                   A6XX_RB_WINDOW_OFFSET(.x = 0, .y = 0));

   tu_cs_emit_regs(cs,
                   A6XX_SP_TP_WINDOW_OFFSET(.x = 0, .y = 0));

   /* emit IB to binning drawcmds: */
   tu_cs_emit_call(cs, &cmd->draw_cs);

   tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3);
   tu_cs_emit(cs, CP_SET_DRAW_STATE__0_COUNT(0) |
                  CP_SET_DRAW_STATE__0_DISABLE_ALL_GROUPS |
                  CP_SET_DRAW_STATE__0_GROUP_ID(0));
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

static void
tu_emit_input_attachments(struct tu_cmd_buffer *cmd,
                          const struct tu_subpass *subpass,
                          struct tu_cs_entry *ib,
                          bool gmem)
{
   /* note: we can probably emit input attachments just once for the whole
    * renderpass, this would avoid emitting both sysmem/gmem versions
    *
    * emit two texture descriptors for each input, as a workaround for
    * d24s8, which can be sampled as both float (depth) and integer (stencil)
    * tu_shader lowers uint input attachment loads to use the 2nd descriptor
    * in the pair
    * TODO: a smarter workaround
    */

   if (!subpass->input_count)
      return;

   struct tu_cs_memory texture;
   VkResult result = tu_cs_alloc(&cmd->sub_cs, subpass->input_count * 2,
                                 A6XX_TEX_CONST_DWORDS, &texture);
   assert(result == VK_SUCCESS);

   for (unsigned i = 0; i < subpass->input_count * 2; i++) {
      uint32_t a = subpass->input_attachments[i / 2].attachment;
      if (a == VK_ATTACHMENT_UNUSED)
         continue;

      struct tu_image_view *iview =
         cmd->state.framebuffer->attachments[a].attachment;
      const struct tu_render_pass_attachment *att =
         &cmd->state.pass->attachments[a];
      uint32_t *dst = &texture.map[A6XX_TEX_CONST_DWORDS * i];

      memcpy(dst, iview->descriptor, A6XX_TEX_CONST_DWORDS * 4);

      if (i % 2 == 1 && att->format == VK_FORMAT_D24_UNORM_S8_UINT) {
         /* note this works because spec says fb and input attachments
          * must use identity swizzle
          */
         dst[0] &= ~(A6XX_TEX_CONST_0_FMT__MASK |
            A6XX_TEX_CONST_0_SWIZ_X__MASK | A6XX_TEX_CONST_0_SWIZ_Y__MASK |
            A6XX_TEX_CONST_0_SWIZ_Z__MASK | A6XX_TEX_CONST_0_SWIZ_W__MASK);
         dst[0] |= A6XX_TEX_CONST_0_FMT(FMT6_S8Z24_UINT) |
            A6XX_TEX_CONST_0_SWIZ_X(A6XX_TEX_Y) |
            A6XX_TEX_CONST_0_SWIZ_Y(A6XX_TEX_ZERO) |
            A6XX_TEX_CONST_0_SWIZ_Z(A6XX_TEX_ZERO) |
            A6XX_TEX_CONST_0_SWIZ_W(A6XX_TEX_ONE);
      }

      if (!gmem)
         continue;

      /* patched for gmem */
      dst[0] &= ~(A6XX_TEX_CONST_0_SWAP__MASK | A6XX_TEX_CONST_0_TILE_MODE__MASK);
      dst[0] |= A6XX_TEX_CONST_0_TILE_MODE(TILE6_2);
      dst[2] =
         A6XX_TEX_CONST_2_TYPE(A6XX_TEX_2D) |
         A6XX_TEX_CONST_2_PITCH(cmd->state.tiling_config.tile0.extent.width * att->cpp);
      dst[3] = 0;
      dst[4] = cmd->device->physical_device->gmem_base + att->gmem_offset;
      dst[5] = A6XX_TEX_CONST_5_DEPTH(1);
      for (unsigned i = 6; i < A6XX_TEX_CONST_DWORDS; i++)
         dst[i] = 0;
   }

   struct tu_cs cs;
   tu_cs_begin_sub_stream(&cmd->sub_cs, 9, &cs);

   tu_cs_emit_pkt7(&cs, CP_LOAD_STATE6_FRAG, 3);
   tu_cs_emit(&cs, CP_LOAD_STATE6_0_DST_OFF(0) |
                  CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
                  CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
                  CP_LOAD_STATE6_0_STATE_BLOCK(SB6_FS_TEX) |
                  CP_LOAD_STATE6_0_NUM_UNIT(subpass->input_count * 2));
   tu_cs_emit_qw(&cs, texture.iova);

   tu_cs_emit_pkt4(&cs, REG_A6XX_SP_FS_TEX_CONST_LO, 2);
   tu_cs_emit_qw(&cs, texture.iova);

   tu_cs_emit_regs(&cs, A6XX_SP_FS_TEX_COUNT(subpass->input_count * 2));

   *ib = tu_cs_end_sub_stream(&cmd->sub_cs, &cs);
}

static void
tu_set_input_attachments(struct tu_cmd_buffer *cmd, const struct tu_subpass *subpass)
{
   struct tu_cs *cs = &cmd->draw_cs;

   tu_emit_input_attachments(cmd, subpass, &cmd->state.ia_gmem_ib, true);
   tu_emit_input_attachments(cmd, subpass, &cmd->state.ia_sysmem_ib, false);

   tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 6);
   tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_INPUT_ATTACHMENTS_GMEM, cmd->state.ia_gmem_ib);
   tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_INPUT_ATTACHMENTS_SYSMEM, cmd->state.ia_sysmem_ib);
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
tu6_sysmem_render_begin(struct tu_cmd_buffer *cmd, struct tu_cs *cs,
                        const struct VkRect2D *renderArea)
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
   tu_cs_emit_regs(cs,
                   A6XX_VPC_SO_OVERRIDE(.so_disable = false));

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

   /* lrz clear? */

   tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0x0);

   tu_emit_cache_flush_ccu(cmd, cs, TU_CMD_CCU_GMEM);

   const struct tu_tiling_config *tiling = &cmd->state.tiling_config;
   if (use_hw_binning(cmd)) {
      /* enable stream-out during binning pass: */
      tu_cs_emit_regs(cs, A6XX_VPC_SO_OVERRIDE(.so_disable=false));

      tu6_emit_bin_size(cs,
                        tiling->tile0.extent.width,
                        tiling->tile0.extent.height,
                        A6XX_RB_BIN_CONTROL_BINNING_PASS | 0x6000000);

      tu6_emit_render_cntl(cmd, cmd->state.subpass, cs, true);

      tu6_emit_binning_pass(cmd, cs);

      /* and disable stream-out for draw pass: */
      tu_cs_emit_regs(cs, A6XX_VPC_SO_OVERRIDE(.so_disable=true));

      tu6_emit_bin_size(cs,
                        tiling->tile0.extent.width,
                        tiling->tile0.extent.height,
                        A6XX_RB_BIN_CONTROL_USE_VIZ | 0x6000000);

      tu_cs_emit_regs(cs,
                      A6XX_VFD_MODE_CNTL(0));

      tu_cs_emit_regs(cs, A6XX_PC_UNKNOWN_9805(.unknown = phys_dev->magic.PC_UNKNOWN_9805));

      tu_cs_emit_regs(cs, A6XX_SP_UNKNOWN_A0F8(.unknown = phys_dev->magic.SP_UNKNOWN_A0F8));

      tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
      tu_cs_emit(cs, 0x1);
   } else {
      /* no binning pass, so enable stream-out for draw pass:: */
      tu_cs_emit_regs(cs, A6XX_VPC_SO_OVERRIDE(.so_disable=false));

      tu6_emit_bin_size(cs,
                        tiling->tile0.extent.width,
                        tiling->tile0.extent.height,
                        0x6000000);
   }

   tu_cs_sanity_check(cs);
}

static void
tu6_render_tile(struct tu_cmd_buffer *cmd,
                struct tu_cs *cs,
                const struct tu_tile *tile)
{
   tu6_emit_tile_select(cmd, cs, tile);

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
   const struct tu_tiling_config *tiling = &cmd->state.tiling_config;

   if (use_hw_binning(cmd))
      cmd->use_vsc_data = true;

   tu6_tile_render_begin(cmd, &cmd->cs);

   for (uint32_t y = 0; y < tiling->tile_count.height; y++) {
      for (uint32_t x = 0; x < tiling->tile_count.width; x++) {
         struct tu_tile tile;
         tu_tiling_config_get_tile(tiling, cmd->device, x, y, &tile);
         tu6_render_tile(cmd, &cmd->cs, &tile);
      }
   }

   tu6_tile_render_end(cmd, &cmd->cs);
}

static void
tu_cmd_render_sysmem(struct tu_cmd_buffer *cmd)
{
   const struct tu_tiling_config *tiling = &cmd->state.tiling_config;

   tu6_sysmem_render_begin(cmd, &cmd->cs, &tiling->render_area);

   tu_cs_emit_call(&cmd->cs, &cmd->draw_cs);

   tu6_sysmem_render_end(cmd, &cmd->cs);
}

static void
tu_cmd_prepare_tile_store_ib(struct tu_cmd_buffer *cmd)
{
   const uint32_t tile_store_space = 11 + (35 * 2) * cmd->state.pass->attachment_count;
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

static void
tu_cmd_update_tiling_config(struct tu_cmd_buffer *cmd,
                            const VkRect2D *render_area)
{
   const struct tu_device *dev = cmd->device;
   struct tu_tiling_config *tiling = &cmd->state.tiling_config;

   tiling->render_area = *render_area;
   tiling->force_sysmem = false;

   tu_tiling_config_update_tile_layout(tiling, dev, cmd->state.pass);
   tu_tiling_config_update_pipe_layout(tiling, dev);
   tu_tiling_config_update_pipes(tiling, dev);
}

static VkResult
tu_create_cmd_buffer(struct tu_device *device,
                     struct tu_cmd_pool *pool,
                     VkCommandBufferLevel level,
                     VkCommandBuffer *pCommandBuffer)
{
   struct tu_cmd_buffer *cmd_buffer;
   cmd_buffer = vk_zalloc(&pool->alloc, sizeof(*cmd_buffer), 8,
                          VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (cmd_buffer == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   cmd_buffer->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
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

   tu_bo_list_init(&cmd_buffer->bo_list);
   tu_cs_init(&cmd_buffer->cs, device, TU_CS_MODE_GROW, 4096);
   tu_cs_init(&cmd_buffer->draw_cs, device, TU_CS_MODE_GROW, 4096);
   tu_cs_init(&cmd_buffer->draw_epilogue_cs, device, TU_CS_MODE_GROW, 4096);
   tu_cs_init(&cmd_buffer->sub_cs, device, TU_CS_MODE_SUB_STREAM, 2048);

   *pCommandBuffer = tu_cmd_buffer_to_handle(cmd_buffer);

   list_inithead(&cmd_buffer->upload.list);

   VkResult result = tu_bo_init_new(device, &cmd_buffer->scratch_bo, 0x1000);
   if (result != VK_SUCCESS)
      goto fail_scratch_bo;

   /* TODO: resize on overflow */
   cmd_buffer->vsc_draw_strm_pitch = device->vsc_draw_strm_pitch;
   cmd_buffer->vsc_prim_strm_pitch = device->vsc_prim_strm_pitch;
   cmd_buffer->vsc_draw_strm = device->vsc_draw_strm;
   cmd_buffer->vsc_prim_strm = device->vsc_prim_strm;

   return VK_SUCCESS;

fail_scratch_bo:
   list_del(&cmd_buffer->pool_link);
   return result;
}

static void
tu_cmd_buffer_destroy(struct tu_cmd_buffer *cmd_buffer)
{
   tu_bo_finish(cmd_buffer->device, &cmd_buffer->scratch_bo);

   list_del(&cmd_buffer->pool_link);

   tu_cs_finish(&cmd_buffer->cs);
   tu_cs_finish(&cmd_buffer->draw_cs);
   tu_cs_finish(&cmd_buffer->draw_epilogue_cs);
   tu_cs_finish(&cmd_buffer->sub_cs);

   tu_bo_list_destroy(&cmd_buffer->bo_list);
   vk_free(&cmd_buffer->pool->alloc, cmd_buffer);
}

static VkResult
tu_reset_cmd_buffer(struct tu_cmd_buffer *cmd_buffer)
{
   cmd_buffer->record_result = VK_SUCCESS;

   tu_bo_list_reset(&cmd_buffer->bo_list);
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
         cmd_buffer->_loader_data.loaderMagic = ICD_LOADER_MAGIC;
         cmd_buffer->level = pAllocateInfo->level;

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
      if (pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT) {
         assert(pBeginInfo->pInheritanceInfo);
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

/* Sets vertex buffers to HW binding points.  We emit VBs in SDS (so that bin
 * rendering can skip over unused state), so we need to collect all the
 * bindings together into a single state emit at draw time.
 */
void
tu_CmdBindVertexBuffers(VkCommandBuffer commandBuffer,
                        uint32_t firstBinding,
                        uint32_t bindingCount,
                        const VkBuffer *pBuffers,
                        const VkDeviceSize *pOffsets)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   assert(firstBinding + bindingCount <= MAX_VBS);

   for (uint32_t i = 0; i < bindingCount; i++) {
      struct tu_buffer *buf = tu_buffer_from_handle(pBuffers[i]);

      cmd->state.vb.buffers[firstBinding + i] = buf;
      cmd->state.vb.offsets[firstBinding + i] = pOffsets[i];

      tu_bo_list_add(&cmd->bo_list, buf->bo, MSM_SUBMIT_BO_READ);
   }

   cmd->state.dirty |= TU_CMD_DIRTY_VERTEX_BUFFERS;
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

   tu_bo_list_add(&cmd->bo_list, buf->bo, MSM_SUBMIT_BO_READ);
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

      for (unsigned j = 0; j < set->layout->buffer_count; ++j) {
         if (set->buffers[j]) {
            tu_bo_list_add(&cmd->bo_list, set->buffers[j],
                           MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE);
         }
      }

      if (set->size > 0) {
         tu_bo_list_add(&cmd->bo_list, &set->pool->bo,
                        MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_DUMP);
      }
   }
   assert(dyn_idx == dynamicOffsetCount);

   uint32_t sp_bindless_base_reg, hlsq_bindless_base_reg, hlsq_update_value;
   uint64_t addr[MAX_SETS + 1] = {};
   struct tu_cs cs;

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
      assert(result == VK_SUCCESS);

      memcpy(dynamic_desc_set.map, descriptors_state->dynamic_descriptors,
             layout->dynamic_offset_count * A6XX_TEX_CONST_DWORDS * 4);
      addr[MAX_SETS] = dynamic_desc_set.iova | 3;
   }

   if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      sp_bindless_base_reg = REG_A6XX_SP_BINDLESS_BASE(0);
      hlsq_bindless_base_reg = REG_A6XX_HLSQ_BINDLESS_BASE(0);
      hlsq_update_value = 0x7c000;

      cmd->state.dirty |= TU_CMD_DIRTY_DESCRIPTOR_SETS | TU_CMD_DIRTY_SHADER_CONSTS;
   } else {
      assert(pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE);

      sp_bindless_base_reg = REG_A6XX_SP_CS_BINDLESS_BASE(0);
      hlsq_bindless_base_reg = REG_A6XX_HLSQ_CS_BINDLESS_BASE(0);
      hlsq_update_value = 0x3e00;

      cmd->state.dirty |= TU_CMD_DIRTY_COMPUTE_DESCRIPTOR_SETS;
   }

   tu_cs_begin_sub_stream(&cmd->sub_cs, 24, &cs);

   tu_cs_emit_pkt4(&cs, sp_bindless_base_reg, 10);
   tu_cs_emit_array(&cs, (const uint32_t*) addr, 10);
   tu_cs_emit_pkt4(&cs, hlsq_bindless_base_reg, 10);
   tu_cs_emit_array(&cs, (const uint32_t*) addr, 10);
   tu_cs_emit_regs(&cs, A6XX_HLSQ_UPDATE_CNTL(.dword = hlsq_update_value));

   struct tu_cs_entry ib = tu_cs_end_sub_stream(&cmd->sub_cs, &cs);
   if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS) {
      tu_cs_emit_pkt7(&cmd->draw_cs, CP_SET_DRAW_STATE, 3);
      tu_cs_emit_sds_ib(&cmd->draw_cs, TU_DRAW_STATE_DESC_SETS, ib);
      cmd->state.desc_sets_ib = ib;
   } else {
      /* note: for compute we could emit directly, instead of a CP_INDIRECT
       * however, the blob uses draw states for compute
       */
      tu_cs_emit_ib(&cmd->cs, &ib);
   }
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

      tu_bo_list_add(&cmd->bo_list, buf->bo, MSM_SUBMIT_BO_WRITE);
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

   for (uint32_t i = 0; i < counterBufferCount; i++) {
      uint32_t idx = firstCounterBuffer + i;
      uint32_t offset = cmd->state.streamout_offset[idx];

      if (!pCounterBuffers[i])
         continue;

      TU_FROM_HANDLE(tu_buffer, buf, pCounterBuffers[i]);

      tu_bo_list_add(&cmd->bo_list, buf->bo, MSM_SUBMIT_BO_READ);

      tu_cs_emit_pkt7(cs, CP_MEM_TO_REG, 3);
      tu_cs_emit(cs, CP_MEM_TO_REG_0_REG(REG_A6XX_VPC_SO_BUFFER_OFFSET(idx)) |
                     CP_MEM_TO_REG_0_UNK31 |
                     CP_MEM_TO_REG_0_CNT(1));
      tu_cs_emit_qw(cs, buf->bo->iova + pCounterBufferOffsets[i]);

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
      tu_cs_emit_qw(cs, cmd->scratch_bo.iova + ctrl_offset(flush_base[i]));
      tu6_emit_event_write(cmd, cs, FLUSH_SO_0 + i);
   }

   for (uint32_t i = 0; i < counterBufferCount; i++) {
      uint32_t idx = firstCounterBuffer + i;
      uint32_t offset = cmd->state.streamout_offset[idx];

      if (!pCounterBuffers[i])
         continue;

      TU_FROM_HANDLE(tu_buffer, buf, pCounterBuffers[i]);

      tu_bo_list_add(&cmd->bo_list, buf->bo, MSM_SUBMIT_BO_WRITE);

      /* VPC_SO_FLUSH_BASE has dwords counter, but counter should be in bytes */
      tu_cs_emit_pkt7(cs, CP_MEM_TO_REG, 3);
      tu_cs_emit(cs, CP_MEM_TO_REG_0_REG(REG_A6XX_CP_SCRATCH_REG(0)) |
                     CP_MEM_TO_REG_0_SHIFT_BY_2 |
                     0x40000 | /* ??? */
                     CP_MEM_TO_REG_0_UNK31 |
                     CP_MEM_TO_REG_0_CNT(1));
      tu_cs_emit_qw(cs, cmd->scratch_bo.iova + ctrl_offset(flush_base[idx]));

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
      tu_cs_emit_qw(cs, buf->bo->iova + pCounterBufferOffsets[i]);
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

   tu_bo_list_add(&cmd_buffer->bo_list, &cmd_buffer->scratch_bo,
                  MSM_SUBMIT_BO_WRITE);

   if (cmd_buffer->use_vsc_data) {
      tu_bo_list_add(&cmd_buffer->bo_list, &cmd_buffer->vsc_draw_strm,
                     MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE);
      tu_bo_list_add(&cmd_buffer->bo_list, &cmd_buffer->vsc_prim_strm,
                     MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE);
   }

   tu_bo_list_add(&cmd_buffer->bo_list, &cmd_buffer->device->border_color,
                  MSM_SUBMIT_BO_READ);

   for (uint32_t i = 0; i < cmd_buffer->draw_cs.bo_count; i++) {
      tu_bo_list_add(&cmd_buffer->bo_list, cmd_buffer->draw_cs.bos[i],
                     MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_DUMP);
   }

   for (uint32_t i = 0; i < cmd_buffer->draw_epilogue_cs.bo_count; i++) {
      tu_bo_list_add(&cmd_buffer->bo_list, cmd_buffer->draw_epilogue_cs.bos[i],
                     MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_DUMP);
   }

   for (uint32_t i = 0; i < cmd_buffer->sub_cs.bo_count; i++) {
      tu_bo_list_add(&cmd_buffer->bo_list, cmd_buffer->sub_cs.bos[i],
                     MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_DUMP);
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
   struct tu_cs_memory memory;
   struct tu_cs cs;

   /* TODO: share this logic with tu_pipeline_static_state */
   tu_cs_alloc(&cmd->sub_cs, size, 1, &memory);
   tu_cs_init_external(&cs, memory.map, memory.map + size);
   tu_cs_begin(&cs);
   tu_cs_reserve_space(&cs, size);

   assert(id < ARRAY_SIZE(cmd->state.dynamic_state));
   cmd->state.dynamic_state[id].iova = memory.iova;
   cmd->state.dynamic_state[id].size = size;

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

   for (uint32_t i = 0; i < pipeline->cs.bo_count; i++) {
      tu_bo_list_add(&cmd->bo_list, pipeline->cs.bos[i],
                     MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_DUMP);
   }

   if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE) {
      cmd->state.compute_pipeline = pipeline;
      cmd->state.dirty |= TU_CMD_DIRTY_COMPUTE_PIPELINE;
      return;
   }

   assert(pipelineBindPoint == VK_PIPELINE_BIND_POINT_GRAPHICS);

   cmd->state.pipeline = pipeline;
   cmd->state.dirty |= TU_CMD_DIRTY_SHADER_CONSTS;

   struct tu_cs *cs = &cmd->draw_cs;
   uint32_t mask = ~pipeline->dynamic_state_mask & BITFIELD_MASK(TU_DYNAMIC_STATE_COUNT);
   uint32_t i;

   tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3 * (7 + util_bitcount(mask)));
   tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_PROGRAM, pipeline->program.state_ib);
   tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_PROGRAM_BINNING, pipeline->program.binning_state_ib);
   tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_VI, pipeline->vi.state_ib);
   tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_VI_BINNING, pipeline->vi.binning_state_ib);
   tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_RAST, pipeline->rast.state_ib);
   tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_DS, pipeline->ds.state_ib);
   tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_BLEND, pipeline->blend.state_ib);

   for_each_bit(i, mask)
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_DYNAMIC + i, pipeline->dynamic_state[i]);

   /* If the new pipeline requires more VBs than we had previously set up, we
    * need to re-emit them in SDS.  If it requires the same set or fewer, we
    * can just re-use the old SDS.
    */
   if (pipeline->vi.bindings_used & ~cmd->vertex_bindings_set)
      cmd->state.dirty |= TU_CMD_DIRTY_VERTEX_BUFFERS;

   /* If the pipeline needs a dynamic descriptor, re-emit descriptor sets */
   if (pipeline->layout->dynamic_offset_count)
      cmd->state.dirty |= TU_CMD_DIRTY_DESCRIPTOR_SETS;

   /* dynamic linewidth state depends pipeline state's gras_su_cntl
    * so the dynamic state ib must be updated when pipeline changes
    */
   if (pipeline->dynamic_state_mask & BIT(VK_DYNAMIC_STATE_LINE_WIDTH)) {
      struct tu_cs cs = tu_cmd_dynamic_state(cmd, VK_DYNAMIC_STATE_LINE_WIDTH, 2);

      cmd->state.dynamic_gras_su_cntl &= A6XX_GRAS_SU_CNTL_LINEHALFWIDTH__MASK;
      cmd->state.dynamic_gras_su_cntl |= pipeline->gras_su_cntl;

      tu_cs_emit_regs(&cs, A6XX_GRAS_SU_CNTL(.dword = cmd->state.dynamic_gras_su_cntl));
   }
}

void
tu_CmdSetViewport(VkCommandBuffer commandBuffer,
                  uint32_t firstViewport,
                  uint32_t viewportCount,
                  const VkViewport *pViewports)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs = tu_cmd_dynamic_state(cmd, VK_DYNAMIC_STATE_VIEWPORT, 18);

   assert(firstViewport == 0 && viewportCount == 1);

   tu6_emit_viewport(&cs, pViewports);
}

void
tu_CmdSetScissor(VkCommandBuffer commandBuffer,
                 uint32_t firstScissor,
                 uint32_t scissorCount,
                 const VkRect2D *pScissors)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs = tu_cmd_dynamic_state(cmd, VK_DYNAMIC_STATE_SCISSOR, 3);

   assert(firstScissor == 0 && scissorCount == 1);

   tu6_emit_scissor(&cs, pScissors);
}

void
tu_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs cs = tu_cmd_dynamic_state(cmd, VK_DYNAMIC_STATE_LINE_WIDTH, 2);

   cmd->state.dynamic_gras_su_cntl &= ~A6XX_GRAS_SU_CNTL_LINEHALFWIDTH__MASK;
   cmd->state.dynamic_gras_su_cntl |= A6XX_GRAS_SU_CNTL_LINEHALFWIDTH(lineWidth / 2.0f);

   tu_cs_emit_regs(&cs, A6XX_GRAS_SU_CNTL(.dword = cmd->state.dynamic_gras_su_cntl));
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

static void
tu_flush_for_access(struct tu_cache_state *cache,
                    enum tu_cmd_access_mask src_mask,
                    enum tu_cmd_access_mask dst_mask)
{
   enum tu_cmd_flush_bits flush_bits = 0;

   if (src_mask & TU_ACCESS_SYSMEM_WRITE) {
      cache->pending_flush_bits |= TU_CMD_FLAG_ALL_INVALIDATE;
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
   if (dst_mask & (TU_ACCESS_##domain##_READ |                 \
                   TU_ACCESS_##domain##_WRITE)) {              \
      flush_bits |= TU_CMD_FLAG_##invalidate |                 \
          (cache->pending_flush_bits &                         \
           (TU_CMD_FLAG_ALL_FLUSH & ~TU_CMD_FLAG_##flush));    \
   }

   DST_INCOHERENT_FLUSH(CCU_COLOR, CCU_FLUSH_COLOR, CCU_INVALIDATE_COLOR)
   DST_INCOHERENT_FLUSH(CCU_DEPTH, CCU_FLUSH_DEPTH, CCU_INVALIDATE_DEPTH)

#undef DST_INCOHERENT_FLUSH

   if (dst_mask & TU_ACCESS_WFI_READ) {
      flush_bits |= TU_CMD_FLAG_WFI;
   }

   cache->flush_bits |= flush_bits;
   cache->pending_flush_bits &= ~flush_bits;
}

static enum tu_cmd_access_mask
vk2tu_access(VkAccessFlags flags, bool gmem)
{
   enum tu_cmd_access_mask mask = 0;

   /* If the GPU writes a buffer that is then read by an indirect draw
    * command, we theoretically need a WFI + WAIT_FOR_ME combination to
    * wait for the writes to complete. The WAIT_FOR_ME is performed as part
    * of the draw by the firmware, so we just need to execute a WFI.
    */
   if (flags &
       (VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
        VK_ACCESS_MEMORY_READ_BIT)) {
      mask |= TU_ACCESS_WFI_READ;
   }

   if (flags &
       (VK_ACCESS_INDIRECT_COMMAND_READ_BIT | /* Read performed by CP */
        VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_READ_BIT_EXT | /* Read performed by CP, I think */
        VK_ACCESS_CONDITIONAL_RENDERING_READ_BIT_EXT | /* Read performed by CP */
        VK_ACCESS_HOST_READ_BIT | /* sysmem by definition */
        VK_ACCESS_MEMORY_READ_BIT)) {
      mask |= TU_ACCESS_SYSMEM_READ;
   }

   if (flags &
       (VK_ACCESS_HOST_WRITE_BIT |
        VK_ACCESS_TRANSFORM_FEEDBACK_COUNTER_WRITE_BIT_EXT | /* Write performed by CP, I think */
        VK_ACCESS_MEMORY_WRITE_BIT)) {
      mask |= TU_ACCESS_SYSMEM_WRITE;
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

      result = tu_bo_list_merge(&cmd->bo_list, &secondary->bo_list);
      if (result != VK_SUCCESS) {
         cmd->record_result = result;
         break;
      }

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

         if (secondary->has_tess)
            cmd->has_tess = true;
      } else {
         assert(tu_cs_is_empty(&secondary->draw_cs));
         assert(tu_cs_is_empty(&secondary->draw_epilogue_cs));

         for (uint32_t j = 0; j < secondary->cs.bo_count; j++) {
            tu_bo_list_add(&cmd->bo_list, secondary->cs.bos[j],
                           MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_DUMP);
         }

         tu_cs_add_entries(&cmd->cs, &secondary->cs);
      }

      cmd->state.index_size = secondary->state.index_size; /* for restart index update */
   }
   cmd->state.dirty = ~0u; /* TODO: set dirty only what needs to be */

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

   pool = vk_alloc2(&device->alloc, pAllocator, sizeof(*pool), 8,
                    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (pool == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (pAllocator)
      pool->alloc = *pAllocator;
   else
      pool->alloc = device->alloc;

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

   vk_free2(&device->alloc, pAllocator, pool);
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
tu_CmdBeginRenderPass(VkCommandBuffer commandBuffer,
                      const VkRenderPassBeginInfo *pRenderPassBegin,
                      VkSubpassContents contents)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_render_pass, pass, pRenderPassBegin->renderPass);
   TU_FROM_HANDLE(tu_framebuffer, fb, pRenderPassBegin->framebuffer);

   cmd->state.pass = pass;
   cmd->state.subpass = pass->subpasses;
   cmd->state.framebuffer = fb;

   tu_cmd_update_tiling_config(cmd, &pRenderPassBegin->renderArea);
   tu_cmd_prepare_tile_store_ib(cmd);

   /* Note: because this is external, any flushes will happen before draw_cs
    * gets called. However deferred flushes could have to happen later as part
    * of the subpass.
    */
   tu_subpass_barrier(cmd, &pass->subpasses[0].start_barrier, true);
   cmd->state.renderpass_cache.pending_flush_bits =
      cmd->state.cache.pending_flush_bits;
   cmd->state.renderpass_cache.flush_bits = 0;

   tu_emit_renderpass_begin(cmd, pRenderPassBegin);

   tu6_emit_zs(cmd, cmd->state.subpass, &cmd->draw_cs);
   tu6_emit_mrt(cmd, cmd->state.subpass, &cmd->draw_cs);
   tu6_emit_msaa(&cmd->draw_cs, cmd->state.subpass->samples);
   tu6_emit_render_cntl(cmd, cmd->state.subpass, &cmd->draw_cs, false);

   tu_set_input_attachments(cmd, cmd->state.subpass);

   for (uint32_t i = 0; i < fb->attachment_count; ++i) {
      const struct tu_image_view *iview = fb->attachments[i].attachment;
      tu_bo_list_add(&cmd->bo_list, iview->image->bo,
                     MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE);
   }

   cmd->state.dirty |= TU_CMD_DIRTY_DRAW_STATE;
}

void
tu_CmdBeginRenderPass2(VkCommandBuffer commandBuffer,
                       const VkRenderPassBeginInfo *pRenderPassBeginInfo,
                       const VkSubpassBeginInfoKHR *pSubpassBeginInfo)
{
   tu_CmdBeginRenderPass(commandBuffer, pRenderPassBeginInfo,
                         pSubpassBeginInfo->contents);
}

void
tu_CmdNextSubpass(VkCommandBuffer commandBuffer, VkSubpassContents contents)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   const struct tu_render_pass *pass = cmd->state.pass;
   struct tu_cs *cs = &cmd->draw_cs;

   const struct tu_subpass *subpass = cmd->state.subpass++;

   tu_cond_exec_start(cs, CP_COND_EXEC_0_RENDER_MODE_GMEM);

   if (subpass->resolve_attachments) {
      tu6_emit_blit_scissor(cmd, cs, true);

      for (unsigned i = 0; i < subpass->color_count; i++) {
         uint32_t a = subpass->resolve_attachments[i].attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;

         tu_store_gmem_attachment(cmd, cs, a,
                                  subpass->color_attachments[i].attachment);

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

void
tu_CmdNextSubpass2(VkCommandBuffer commandBuffer,
                   const VkSubpassBeginInfoKHR *pSubpassBeginInfo,
                   const VkSubpassEndInfoKHR *pSubpassEndInfo)
{
   tu_CmdNextSubpass(commandBuffer, pSubpassBeginInfo->contents);
}

static void
tu6_emit_user_consts(struct tu_cs *cs, const struct tu_pipeline *pipeline,
                     struct tu_descriptor_state *descriptors_state,
                     gl_shader_stage type,
                     uint32_t *push_constants)
{
   const struct tu_program_descriptor_linkage *link =
      &pipeline->program.link[type];
   const struct ir3_ubo_analysis_state *state = &link->const_state.ubo_state;

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
       * it.
       */
      assert(state->range[i].ubo.bindless);
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

static struct tu_cs_entry
tu6_emit_consts(struct tu_cmd_buffer *cmd,
                const struct tu_pipeline *pipeline,
                struct tu_descriptor_state *descriptors_state,
                gl_shader_stage type)
{
   struct tu_cs cs;
   tu_cs_begin_sub_stream(&cmd->sub_cs, 512, &cs); /* TODO: maximum size? */

   tu6_emit_user_consts(&cs, pipeline, descriptors_state, type, cmd->push_constants);

   return tu_cs_end_sub_stream(&cmd->sub_cs, &cs);
}

static struct tu_cs_entry
tu6_emit_vertex_buffers(struct tu_cmd_buffer *cmd,
                        const struct tu_pipeline *pipeline)
{
   struct tu_cs cs;
   tu_cs_begin_sub_stream(&cmd->sub_cs, 4 * MAX_VBS, &cs);

   int binding;
   for_each_bit(binding, pipeline->vi.bindings_used) {
      const struct tu_buffer *buf = cmd->state.vb.buffers[binding];
      const VkDeviceSize offset = buf->bo_offset +
         cmd->state.vb.offsets[binding];

      tu_cs_emit_regs(&cs,
                      A6XX_VFD_FETCH_BASE(binding, .bo = buf->bo, .bo_offset = offset),
                      A6XX_VFD_FETCH_SIZE(binding, buf->size - offset));

   }

   cmd->vertex_bindings_set = pipeline->vi.bindings_used;

   return tu_cs_end_sub_stream(&cmd->sub_cs, &cs);
}

static uint64_t
get_tess_param_bo_size(const struct tu_pipeline *pipeline,
                       uint32_t draw_count)
{
   /* TODO: For indirect draws, we can't compute the BO size ahead of time.
    * Still not sure what to do here, so just allocate a reasonably large
    * BO and hope for the best for now.
    * (maxTessellationControlPerVertexOutputComponents * 2048 vertices +
    *  maxTessellationControlPerPatchOutputComponents * 512 patches) */
   if (!draw_count) {
      return ((128 * 2048) + (128 * 512)) * 4;
   }

   /* For each patch, adreno lays out the tess param BO in memory as:
    * (v_input[0][0])...(v_input[i][j])(p_input[0])...(p_input[k]).
    * where i = # vertices per patch, j = # per-vertex outputs, and
    * k = # per-patch outputs.*/
   uint32_t verts_per_patch = pipeline->ia.primtype - DI_PT_PATCHES0;
   uint32_t num_patches = draw_count / verts_per_patch;
   return draw_count * pipeline->tess.per_vertex_output_size +
          pipeline->tess.per_patch_output_size * num_patches;
}

static uint64_t
get_tess_factor_bo_size(const struct tu_pipeline *pipeline,
                        uint32_t draw_count)
{
   /* TODO: For indirect draws, we can't compute the BO size ahead of time.
    * Still not sure what to do here, so just allocate a reasonably large
    * BO and hope for the best for now.
    * (quad factor stride * 512 patches) */
   if (!draw_count) {
      return (28 * 512) * 4;
   }

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
                     struct tu_cs_entry *entry)
{
   struct tu_cs cs;
   VkResult result = tu_cs_begin_sub_stream(&cmd->sub_cs, 20, &cs);
   if (result != VK_SUCCESS)
      return result;

   uint64_t tess_factor_size = get_tess_factor_bo_size(pipeline, draw_count);
   uint64_t tess_param_size = get_tess_param_bo_size(pipeline, draw_count);
   uint64_t tess_bo_size =  tess_factor_size + tess_param_size;
   if (tess_bo_size > 0) {
      struct tu_bo *tess_bo;
      result = tu_get_scratch_bo(cmd->device, tess_bo_size, &tess_bo);
      if (result != VK_SUCCESS)
         return result;

      tu_bo_list_add(&cmd->bo_list, tess_bo,
            MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE);
      uint64_t tess_factor_iova = tess_bo->iova;
      uint64_t tess_param_iova = tess_factor_iova + tess_factor_size;

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

      tu_cs_emit_pkt4(&cs, REG_A6XX_PC_TESSFACTOR_ADDR_LO, 2);
      tu_cs_emit_qw(&cs, tess_factor_iova);

      /* TODO: Without this WFI here, the hardware seems unable to read these
       * addresses we just emitted. Freedreno emits these consts as part of
       * IB1 instead of in a draw state which might make this WFI unnecessary,
       * but it requires a bit more indirection (SS6_INDIRECT for consts). */
      tu_cs_emit_wfi(&cs);
   }
   *entry = tu_cs_end_sub_stream(&cmd->sub_cs, &cs);
   return VK_SUCCESS;
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

   /* TODO lrz */

   tu_cs_emit_regs(cs, A6XX_PC_PRIMITIVE_CNTL_0(
         .primitive_restart =
               pipeline->ia.primitive_restart && indexed,
         .tess_upper_left_domain_origin =
               pipeline->tess.upper_left_domain_origin));

   if (cmd->state.dirty & TU_CMD_DIRTY_SHADER_CONSTS) {
      cmd->state.shader_const_ib[MESA_SHADER_VERTEX] =
         tu6_emit_consts(cmd, pipeline, descriptors_state, MESA_SHADER_VERTEX);
      cmd->state.shader_const_ib[MESA_SHADER_TESS_CTRL] =
         tu6_emit_consts(cmd, pipeline, descriptors_state, MESA_SHADER_TESS_CTRL);
      cmd->state.shader_const_ib[MESA_SHADER_TESS_EVAL] =
         tu6_emit_consts(cmd, pipeline, descriptors_state, MESA_SHADER_TESS_EVAL);
      cmd->state.shader_const_ib[MESA_SHADER_GEOMETRY] =
         tu6_emit_consts(cmd, pipeline, descriptors_state, MESA_SHADER_GEOMETRY);
      cmd->state.shader_const_ib[MESA_SHADER_FRAGMENT] =
         tu6_emit_consts(cmd, pipeline, descriptors_state, MESA_SHADER_FRAGMENT);
   }

   if (cmd->state.dirty & TU_CMD_DIRTY_DESCRIPTOR_SETS) {
      /* We need to reload the descriptors every time the descriptor sets
       * change. However, the commands we send only depend on the pipeline
       * because the whole point is to cache descriptors which are used by the
       * pipeline. There's a problem here, in that the firmware has an
       * "optimization" which skips executing groups that are set to the same
       * value as the last draw. This means that if the descriptor sets change
       * but not the pipeline, we'd try to re-execute the same buffer which
       * the firmware would ignore and we wouldn't pre-load the new
       * descriptors. The blob seems to re-emit the LOAD_STATE group whenever
       * the descriptor sets change, which we emulate here by copying the
       * pre-prepared buffer.
       */
      const struct tu_cs_entry *load_entry = &pipeline->load_state.state_ib;
      if (load_entry->size > 0) {
         struct tu_cs load_cs;
         result = tu_cs_begin_sub_stream(&cmd->sub_cs, load_entry->size, &load_cs);
         if (result != VK_SUCCESS)
            return result;
         tu_cs_emit_array(&load_cs,
                          (uint32_t *)((char  *)load_entry->bo->map + load_entry->offset),
                          load_entry->size / 4);
         cmd->state.desc_sets_load_ib = tu_cs_end_sub_stream(&cmd->sub_cs, &load_cs);
      } else {
         cmd->state.desc_sets_load_ib.size = 0;
      }
   }

   if (cmd->state.dirty & TU_CMD_DIRTY_VERTEX_BUFFERS)
      cmd->state.vertex_buffers_ib = tu6_emit_vertex_buffers(cmd, pipeline);

   bool has_tess =
         pipeline->active_stages & VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
   struct tu_cs_entry tess_consts = {};
   if (has_tess) {
      cmd->has_tess = true;
      result = tu6_emit_tess_consts(cmd, draw_count, pipeline, &tess_consts);
      if (result != VK_SUCCESS)
         return result;
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

      tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_PROGRAM, pipeline->program.state_ib);
      tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_PROGRAM_BINNING, pipeline->program.binning_state_ib);
      tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_TESS, tess_consts);
      tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_VI, pipeline->vi.state_ib);
      tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_VI_BINNING, pipeline->vi.binning_state_ib);
      tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_RAST, pipeline->rast.state_ib);
      tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_DS, pipeline->ds.state_ib);
      tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_BLEND, pipeline->blend.state_ib);
      tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_VS_CONST, cmd->state.shader_const_ib[MESA_SHADER_VERTEX]);
      tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_HS_CONST, cmd->state.shader_const_ib[MESA_SHADER_TESS_CTRL]);
      tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_DS_CONST, cmd->state.shader_const_ib[MESA_SHADER_TESS_EVAL]);
      tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_GS_CONST, cmd->state.shader_const_ib[MESA_SHADER_GEOMETRY]);
      tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_FS_CONST, cmd->state.shader_const_ib[MESA_SHADER_FRAGMENT]);
      tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_DESC_SETS, cmd->state.desc_sets_ib);
      tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_DESC_SETS_LOAD, cmd->state.desc_sets_load_ib);
      tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_VB, cmd->state.vertex_buffers_ib);
      tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VS_PARAMS, cmd->state.vs_params);

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
      uint32_t draw_state_count =
         has_tess +
         ((cmd->state.dirty & TU_CMD_DIRTY_SHADER_CONSTS) ? 5 : 0) +
         ((cmd->state.dirty & TU_CMD_DIRTY_DESCRIPTOR_SETS) ? 1 : 0) +
         ((cmd->state.dirty & TU_CMD_DIRTY_VERTEX_BUFFERS) ? 1 : 0) +
         1; /* vs_params */

         tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3 * draw_state_count);

         /* We may need to re-emit tess consts if the current draw call is
          * sufficiently larger than the last draw call. */
         if (has_tess)
            tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_TESS, tess_consts);
         if (cmd->state.dirty & TU_CMD_DIRTY_SHADER_CONSTS) {
            tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_VS_CONST, cmd->state.shader_const_ib[MESA_SHADER_VERTEX]);
            tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_HS_CONST, cmd->state.shader_const_ib[MESA_SHADER_TESS_CTRL]);
            tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_DS_CONST, cmd->state.shader_const_ib[MESA_SHADER_TESS_EVAL]);
            tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_GS_CONST, cmd->state.shader_const_ib[MESA_SHADER_GEOMETRY]);
            tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_FS_CONST, cmd->state.shader_const_ib[MESA_SHADER_FRAGMENT]);
         }
         if (cmd->state.dirty & TU_CMD_DIRTY_DESCRIPTOR_SETS)
            tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_DESC_SETS_LOAD, cmd->state.desc_sets_load_ib);
         if (cmd->state.dirty & TU_CMD_DIRTY_VERTEX_BUFFERS)
            tu_cs_emit_sds_ib(cs, TU_DRAW_STATE_VB, cmd->state.vertex_buffers_ib);
         tu_cs_emit_draw_state(cs, TU_DRAW_STATE_VS_PARAMS, cmd->state.vs_params);
   }

   tu_cs_sanity_check(cs);

   /* There are too many graphics dirty bits to list here, so just list the
    * bits to preserve instead. The only things not emitted here are
    * compute-related state.
    */
   cmd->state.dirty &= (TU_CMD_DIRTY_COMPUTE_DESCRIPTOR_SETS | TU_CMD_DIRTY_COMPUTE_PIPELINE);
   return VK_SUCCESS;
}

static uint32_t
tu_draw_initiator(struct tu_cmd_buffer *cmd, enum pc_di_src_sel src_sel)
{
   const struct tu_pipeline *pipeline = cmd->state.pipeline;
   uint32_t initiator =
      CP_DRAW_INDX_OFFSET_0_PRIM_TYPE(pipeline->ia.primtype) |
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

   tu6_draw_common(cmd, cs, false, 0);

   /* workaround for a firmware bug with CP_DRAW_INDIRECT_MULTI, where it
    * doesn't wait for WFIs to be completed and leads to GPU fault/hang
    * TODO: this could be worked around in a more performant way,
    * or there may exist newer firmware that has been fixed
    */
   if (cmd->device->physical_device->gpu_id != 650)
      tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDIRECT_MULTI, 6);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_AUTO_INDEX));
   tu_cs_emit(cs, A6XX_CP_DRAW_INDIRECT_MULTI_1_OPCODE(INDIRECT_OP_NORMAL) |
                  A6XX_CP_DRAW_INDIRECT_MULTI_1_DST_OFF(vs_params_offset(cmd)));
   tu_cs_emit(cs, drawCount);
   tu_cs_emit_qw(cs, buf->bo->iova + buf->bo_offset + offset);
   tu_cs_emit(cs, stride);

   tu_bo_list_add(&cmd->bo_list, buf->bo, MSM_SUBMIT_BO_READ);
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

   tu6_draw_common(cmd, cs, true, 0);

   /* workaround for a firmware bug with CP_DRAW_INDIRECT_MULTI, where it
    * doesn't wait for WFIs to be completed and leads to GPU fault/hang
    * TODO: this could be worked around in a more performant way,
    * or there may exist newer firmware that has been fixed
    */
   if (cmd->device->physical_device->gpu_id != 650)
      tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);

   tu_cs_emit_pkt7(cs, CP_DRAW_INDIRECT_MULTI, 9);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_DMA));
   tu_cs_emit(cs, A6XX_CP_DRAW_INDIRECT_MULTI_1_OPCODE(INDIRECT_OP_INDEXED) |
                  A6XX_CP_DRAW_INDIRECT_MULTI_1_DST_OFF(vs_params_offset(cmd)));
   tu_cs_emit(cs, drawCount);
   tu_cs_emit_qw(cs, cmd->state.index_va);
   tu_cs_emit(cs, cmd->state.max_index_count);
   tu_cs_emit_qw(cs, buf->bo->iova + buf->bo_offset + offset);
   tu_cs_emit(cs, stride);

   tu_bo_list_add(&cmd->bo_list, buf->bo, MSM_SUBMIT_BO_READ);
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

   cmd->state.vs_params = tu6_emit_vs_params(cmd, 0, firstInstance);

   tu6_draw_common(cmd, cs, false, 0);

   tu_cs_emit_pkt7(cs, CP_DRAW_AUTO, 6);
   tu_cs_emit(cs, tu_draw_initiator(cmd, DI_SRC_SEL_AUTO_XFB));
   tu_cs_emit(cs, instanceCount);
   tu_cs_emit_qw(cs, buf->bo->iova + buf->bo_offset + counterBufferOffset);
   tu_cs_emit(cs, counterOffset);
   tu_cs_emit(cs, vertexStride);

   tu_bo_list_add(&cmd->bo_list, buf->bo, MSM_SUBMIT_BO_READ);
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
tu_emit_compute_driver_params(struct tu_cs *cs, struct tu_pipeline *pipeline,
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
      uint32_t driver_params[IR3_DP_CS_COUNT] = {
         [IR3_DP_NUM_WORK_GROUPS_X] = info->blocks[0],
         [IR3_DP_NUM_WORK_GROUPS_Y] = info->blocks[1],
         [IR3_DP_NUM_WORK_GROUPS_Z] = info->blocks[2],
         [IR3_DP_LOCAL_GROUP_SIZE_X] = pipeline->compute.local_size[0],
         [IR3_DP_LOCAL_GROUP_SIZE_Y] = pipeline->compute.local_size[1],
         [IR3_DP_LOCAL_GROUP_SIZE_Z] = pipeline->compute.local_size[2],
      };

      uint32_t num_consts = MIN2(const_state->num_driver_params,
                                 (link->constlen - offset) * 4);
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
   } else {
      tu_finishme("Indirect driver params");
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

   if (cmd->state.dirty & TU_CMD_DIRTY_COMPUTE_PIPELINE)
      tu_cs_emit_ib(cs, &pipeline->program.state_ib);

   struct tu_cs_entry ib;

   ib = tu6_emit_consts(cmd, pipeline, descriptors_state, MESA_SHADER_COMPUTE);
   if (ib.size)
      tu_cs_emit_ib(cs, &ib);

   tu_emit_compute_driver_params(cs, pipeline, info);

   if ((cmd->state.dirty & TU_CMD_DIRTY_COMPUTE_DESCRIPTOR_SETS) &&
       pipeline->load_state.state_ib.size > 0) {
      tu_cs_emit_ib(cs, &pipeline->load_state.state_ib);
   }

   cmd->state.dirty &=
      ~(TU_CMD_DIRTY_COMPUTE_DESCRIPTOR_SETS | TU_CMD_DIRTY_COMPUTE_PIPELINE);

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

      tu_bo_list_add(&cmd->bo_list, info->indirect->bo,
                     MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE);

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
tu_CmdEndRenderPass(VkCommandBuffer commandBuffer)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);

   tu_cs_end(&cmd_buffer->draw_cs);
   tu_cs_end(&cmd_buffer->draw_epilogue_cs);

   if (use_sysmem_rendering(cmd_buffer))
      tu_cmd_render_sysmem(cmd_buffer);
   else
      tu_cmd_render_tiles(cmd_buffer);

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
}

void
tu_CmdEndRenderPass2(VkCommandBuffer commandBuffer,
                     const VkSubpassEndInfoKHR *pSubpassEndInfo)
{
   tu_CmdEndRenderPass(commandBuffer);
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
      TU_FROM_HANDLE(tu_image, image, pImageMemoryBarriers[i].image);
      VkImageLayout old_layout = pImageMemoryBarriers[i].oldLayout;
      /* For non-linear images, PREINITIALIZED is the same as UNDEFINED */
      if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED ||
          (image->tiling != VK_IMAGE_TILING_LINEAR &&
           old_layout == VK_IMAGE_LAYOUT_PREINITIALIZED)) {
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

      tu_bo_list_add(&cmd->bo_list, &event->bo, MSM_SUBMIT_BO_READ);

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

   tu_bo_list_add(&cmd->bo_list, &event->bo, MSM_SUBMIT_BO_WRITE);

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
