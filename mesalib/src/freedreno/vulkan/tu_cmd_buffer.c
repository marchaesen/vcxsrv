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

#define OVERFLOW_FLAG_REG REG_A6XX_CP_SCRATCH_REG(0)

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

enum a3xx_msaa_samples
tu_msaa_samples(uint32_t samples)
{
   switch (samples) {
   case 1:
      return MSAA_ONE;
   case 2:
      return MSAA_TWO;
   case 4:
      return MSAA_FOUR;
   case 8:
      return MSAA_EIGHT;
   default:
      assert(!"invalid sample count");
      return MSAA_ONE;
   }
}

static enum a4xx_index_size
tu6_index_size(VkIndexType type)
{
   switch (type) {
   case VK_INDEX_TYPE_UINT16:
      return INDEX4_SIZE_16_BIT;
   case VK_INDEX_TYPE_UINT32:
      return INDEX4_SIZE_32_BIT;
   default:
      unreachable("invalid VkIndexType");
      return INDEX4_SIZE_8_BIT;
   }
}

unsigned
tu6_emit_event_write(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     enum vgt_event_type event,
                     bool need_seqno)
{
   unsigned seqno = 0;

   tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, need_seqno ? 4 : 1);
   tu_cs_emit(cs, CP_EVENT_WRITE_0_EVENT(event));
   if (need_seqno) {
      tu_cs_emit_qw(cs, cmd->scratch_bo.iova);
      seqno = ++cmd->scratch_seqno;
      tu_cs_emit(cs, seqno);
   }

   return seqno;
}

static void
tu6_emit_cache_flush(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   tu6_emit_event_write(cmd, cs, 0x31, false);
}

static void
tu6_emit_lrz_flush(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   tu6_emit_event_write(cmd, cs, LRZ_FLUSH, false);
}

static void
tu6_emit_wfi(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   if (cmd->wait_for_idle) {
      tu_cs_emit_wfi(cs);
      cmd->wait_for_idle = false;
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

   tu_cs_emit_regs(cs,
                   A6XX_RB_RENDER_COMPONENTS(.dword = subpass->render_components));
   tu_cs_emit_regs(cs,
                   A6XX_SP_FS_RENDER_COMPONENTS(.dword = subpass->render_components));

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

static bool
use_hw_binning(struct tu_cmd_buffer *cmd)
{
   const struct tu_tiling_config *tiling = &cmd->state.tiling_config;

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

      tu_cs_emit_pkt7(cs, CP_REG_TEST, 1);
      tu_cs_emit(cs, A6XX_CP_REG_TEST_0_REG(OVERFLOW_FLAG_REG) |
                     A6XX_CP_REG_TEST_0_BIT(0) |
                     A6XX_CP_REG_TEST_0_WAIT_FOR_ME);

      tu_cs_reserve(cs, 3 + 11);
      tu_cs_emit_pkt7(cs, CP_COND_REG_EXEC, 2);
      tu_cs_emit(cs, CP_COND_REG_EXEC_0_MODE(PRED_TEST));
      tu_cs_emit(cs, CP_COND_REG_EXEC_1_DWORDS(11));

      /* if (no overflow) */ {
         tu_cs_emit_pkt7(cs, CP_SET_BIN_DATA5, 7);
         tu_cs_emit(cs, cmd->state.tiling_config.pipe_sizes[tile->pipe] |
                        CP_SET_BIN_DATA5_0_VSC_N(tile->slot));
         tu_cs_emit_qw(cs, cmd->vsc_draw_strm.iova + tile->pipe * cmd->vsc_draw_strm_pitch);
         tu_cs_emit_qw(cs, cmd->vsc_draw_strm.iova + (tile->pipe * 4) + (32 * cmd->vsc_draw_strm_pitch));
         tu_cs_emit_qw(cs, cmd->vsc_prim_strm.iova + (tile->pipe * cmd->vsc_prim_strm_pitch));

         tu_cs_emit_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1);
         tu_cs_emit(cs, 0x0);

         /* use a NOP packet to skip over the 'else' side: */
         tu_cs_emit_pkt7(cs, CP_NOP, 2);
      } /* else */ {
         tu_cs_emit_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1);
         tu_cs_emit(cs, 0x1);
      }

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
tu6_emit_restart_index(struct tu_cs *cs, uint32_t restart_index)
{
   tu_cs_emit_regs(cs,
                   A6XX_PC_RESTART_INDEX(restart_index));
}

static void
tu6_init_hw(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   const struct tu_physical_device *phys_dev = cmd->device->physical_device;

   tu6_emit_cache_flush(cmd, cs);

   tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_UPDATE_CNTL, 0xfffff);

   tu_cs_emit_regs(cs,
                   A6XX_RB_CCU_CNTL(.offset = phys_dev->ccu_offset_bypass));
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
   tu_cs_emit_write_reg(cs, REG_A6XX_PC_UNKNOWN_9981, 0x3);
   tu_cs_emit_write_reg(cs, REG_A6XX_PC_UNKNOWN_9E72, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9108, 0x3);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_TP_UNKNOWN_B309, 0x000000a2);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8878, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8879, 0);
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

   /* Set not to use streamout by default, */
   tu_cs_emit_pkt7(cs, CP_CONTEXT_REG_BUNCH, 4);
   tu_cs_emit(cs, REG_A6XX_VPC_SO_CNTL);
   tu_cs_emit(cs, 0);
   tu_cs_emit(cs, REG_A6XX_VPC_SO_BUF_CNTL);
   tu_cs_emit(cs, 0);

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
tu6_cache_flush(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   unsigned seqno;

   seqno = tu6_emit_event_write(cmd, cs, RB_DONE_TS, true);

   tu_cs_emit_pkt7(cs, CP_WAIT_REG_MEM, 6);
   tu_cs_emit(cs, CP_WAIT_REG_MEM_0_FUNCTION(WRITE_EQ) |
                  CP_WAIT_REG_MEM_0_POLL_MEMORY);
   tu_cs_emit_qw(cs, cmd->scratch_bo.iova);
   tu_cs_emit(cs, CP_WAIT_REG_MEM_3_REF(seqno));
   tu_cs_emit(cs, CP_WAIT_REG_MEM_4_MASK(~0));
   tu_cs_emit(cs, CP_WAIT_REG_MEM_5_DELAY_LOOP_CYCLES(16));

   seqno = tu6_emit_event_write(cmd, cs, CACHE_FLUSH_TS, true);

   tu_cs_emit_pkt7(cs, CP_WAIT_MEM_GTE, 4);
   tu_cs_emit(cs, CP_WAIT_MEM_GTE_0_RESERVED(0));
   tu_cs_emit_qw(cs, cmd->scratch_bo.iova);
   tu_cs_emit(cs, CP_WAIT_MEM_GTE_3_REF(seqno));
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
                   A6XX_VSC_PRIM_STRM_ARRAY_PITCH(cmd->vsc_prim_strm.size));

   tu_cs_emit_regs(cs,
                   A6XX_VSC_DRAW_STRM_ADDRESS(.bo = &cmd->vsc_draw_strm),
                   A6XX_VSC_DRAW_STRM_PITCH(cmd->vsc_draw_strm_pitch),
                   A6XX_VSC_DRAW_STRM_ARRAY_PITCH(cmd->vsc_draw_strm.size));
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
      tu_cs_emit(cs, CP_COND_WRITE5_3_REF(cmd->vsc_draw_strm_pitch));
      tu_cs_emit(cs, CP_COND_WRITE5_4_MASK(~0));
      tu_cs_emit_qw(cs, cmd->scratch_bo.iova + ctrl_offset(vsc_scratch));
      tu_cs_emit(cs, CP_COND_WRITE5_7_WRITE_DATA(1 + cmd->vsc_draw_strm_pitch));

      tu_cs_emit_pkt7(cs, CP_COND_WRITE5, 8);
      tu_cs_emit(cs, CP_COND_WRITE5_0_FUNCTION(WRITE_GE) |
            CP_COND_WRITE5_0_WRITE_MEMORY);
      tu_cs_emit(cs, CP_COND_WRITE5_1_POLL_ADDR_LO(REG_A6XX_VSC_PRIM_STRM_SIZE_REG(i)));
      tu_cs_emit(cs, CP_COND_WRITE5_2_POLL_ADDR_HI(0));
      tu_cs_emit(cs, CP_COND_WRITE5_3_REF(cmd->vsc_prim_strm_pitch));
      tu_cs_emit(cs, CP_COND_WRITE5_4_MASK(~0));
      tu_cs_emit_qw(cs, cmd->scratch_bo.iova + ctrl_offset(vsc_scratch));
      tu_cs_emit(cs, CP_COND_WRITE5_7_WRITE_DATA(3 + cmd->vsc_prim_strm_pitch));
   }

   tu_cs_emit_pkt7(cs, CP_WAIT_MEM_WRITES, 0);

   tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);

   tu_cs_emit_pkt7(cs, CP_MEM_TO_REG, 3);
   tu_cs_emit(cs, CP_MEM_TO_REG_0_REG(OVERFLOW_FLAG_REG) |
         CP_MEM_TO_REG_0_CNT(1 - 1));
   tu_cs_emit_qw(cs, cmd->scratch_bo.iova + ctrl_offset(vsc_scratch));

   /*
    * This is a bit awkward, we really want a way to invert the
    * CP_REG_TEST/CP_COND_REG_EXEC logic, so that we can conditionally
    * execute cmds to use hwbinning when a bit is *not* set.  This
    * dance is to invert OVERFLOW_FLAG_REG
    *
    * A CP_NOP packet is used to skip executing the 'else' clause
    * if (b0 set)..
    */

   /* b0 will be set if VSC_DRAW_STRM or VSC_PRIM_STRM overflow: */
   tu_cs_emit_pkt7(cs, CP_REG_TEST, 1);
   tu_cs_emit(cs, A6XX_CP_REG_TEST_0_REG(OVERFLOW_FLAG_REG) |
         A6XX_CP_REG_TEST_0_BIT(0) |
         A6XX_CP_REG_TEST_0_WAIT_FOR_ME);

   tu_cs_reserve(cs, 3 + 7);
   tu_cs_emit_pkt7(cs, CP_COND_REG_EXEC, 2);
   tu_cs_emit(cs, CP_COND_REG_EXEC_0_MODE(PRED_TEST));
   tu_cs_emit(cs, CP_COND_REG_EXEC_1_DWORDS(7));

   /* if (b0 set) */ {
      /*
       * On overflow, mirror the value to control->vsc_overflow
       * which CPU is checking to detect overflow (see
       * check_vsc_overflow())
       */
      tu_cs_emit_pkt7(cs, CP_REG_TO_MEM, 3);
      tu_cs_emit(cs, CP_REG_TO_MEM_0_REG(OVERFLOW_FLAG_REG) |
            CP_REG_TO_MEM_0_CNT(0));
      tu_cs_emit_qw(cs, cmd->scratch_bo.iova + ctrl_offset(vsc_overflow));

      tu_cs_emit_pkt4(cs, OVERFLOW_FLAG_REG, 1);
      tu_cs_emit(cs, 0x0);

      tu_cs_emit_pkt7(cs, CP_NOP, 2);  /* skip 'else' when 'if' is taken */
   } /* else */ {
      tu_cs_emit_pkt4(cs, OVERFLOW_FLAG_REG, 1);
      tu_cs_emit(cs, 0x1);
   }
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

   tu6_emit_event_write(cmd, cs, CACHE_INVALIDATE, false);
   tu6_cache_flush(cmd, cs);

   tu_cs_emit_wfi(cs);

   tu_cs_emit_pkt7(cs, CP_WAIT_FOR_ME, 0);

   emit_vsc_overflow_test(cmd, cs);

   tu_cs_emit_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1);
   tu_cs_emit(cs, 0x0);

   tu_cs_emit_pkt7(cs, CP_SET_MODE, 1);
   tu_cs_emit(cs, 0x0);

   cmd->wait_for_idle = false;
}

static void
tu_emit_load_clear(struct tu_cmd_buffer *cmd,
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
   const struct tu_physical_device *phys_dev = cmd->device->physical_device;
   const struct tu_framebuffer *fb = cmd->state.framebuffer;

   assert(fb->width > 0 && fb->height > 0);
   tu6_emit_window_scissor(cs, 0, 0, fb->width - 1, fb->height - 1);
   tu6_emit_window_offset(cs, 0, 0);

   tu6_emit_bin_size(cs, 0, 0, 0xc00000); /* 0xc00000 = BYPASS? */

   tu6_emit_lrz_flush(cmd, cs);

   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_BYPASS));

   tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0x0);

   tu6_emit_event_write(cmd, cs, PC_CCU_INVALIDATE_COLOR, false);
   tu6_emit_event_write(cmd, cs, PC_CCU_INVALIDATE_DEPTH, false);
   tu6_emit_event_write(cmd, cs, CACHE_INVALIDATE, false);

   tu6_emit_wfi(cmd, cs);
   tu_cs_emit_regs(cs,
                   A6XX_RB_CCU_CNTL(.offset = phys_dev->ccu_offset_bypass));

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
   const struct tu_subpass *subpass = cmd->state.subpass;
   if (subpass->resolve_attachments) {
      for (unsigned i = 0; i < subpass->color_count; i++) {
         uint32_t a = subpass->resolve_attachments[i].attachment;
         if (a != VK_ATTACHMENT_UNUSED)
            tu6_emit_sysmem_resolve(cmd, cs, a,
                                    subpass->color_attachments[i].attachment);
      }
   }

   tu_cs_emit_call(cs, &cmd->draw_epilogue_cs);

   tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0x0);

   tu6_emit_lrz_flush(cmd, cs);

   tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_COLOR_TS, true);
   tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_DEPTH_TS, true);

   tu_cs_sanity_check(cs);
}


static void
tu6_tile_render_begin(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   struct tu_physical_device *phys_dev = cmd->device->physical_device;

   tu6_emit_lrz_flush(cmd, cs);

   /* lrz clear? */

   tu6_emit_cache_flush(cmd, cs);

   tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0x0);

   /* TODO: flushing with barriers instead of blindly always flushing */
   tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_COLOR_TS, true);
   tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_DEPTH_TS, true);
   tu6_emit_event_write(cmd, cs, PC_CCU_INVALIDATE_COLOR, false);
   tu6_emit_event_write(cmd, cs, PC_CCU_INVALIDATE_DEPTH, false);

   tu_cs_emit_wfi(cs);
   tu_cs_emit_regs(cs,
                   A6XX_RB_CCU_CNTL(.offset = phys_dev->ccu_offset_gmem, .gmem = 1));

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
   cmd->wait_for_idle = true;

   if (use_hw_binning(cmd)) {
      tu_cs_emit_pkt7(cs, CP_REG_TEST, 1);
      tu_cs_emit(cs, A6XX_CP_REG_TEST_0_REG(OVERFLOW_FLAG_REG) |
                     A6XX_CP_REG_TEST_0_BIT(0) |
                     A6XX_CP_REG_TEST_0_WAIT_FOR_ME);

      tu_cs_reserve(cs, 3 + 2);
      tu_cs_emit_pkt7(cs, CP_COND_REG_EXEC, 2);
      tu_cs_emit(cs, CP_COND_REG_EXEC_0_MODE(PRED_TEST));
      tu_cs_emit(cs, CP_COND_REG_EXEC_1_DWORDS(2));

      /* if (no overflow) */ {
         tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
         tu_cs_emit(cs, A6XX_CP_SET_MARKER_0_MODE(RM6_ENDVIS));
      }
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

   tu6_emit_lrz_flush(cmd, cs);

   tu6_emit_event_write(cmd, cs, PC_CCU_RESOLVE_TS, true);

   tu_cs_sanity_check(cs);
}

static void
tu_cmd_render_tiles(struct tu_cmd_buffer *cmd)
{
   const struct tu_tiling_config *tiling = &cmd->state.tiling_config;

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
   cmd->wait_for_idle = true;

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

const struct tu_dynamic_state default_dynamic_state = {
   .viewport =
     {
       .count = 0,
     },
   .scissor =
     {
       .count = 0,
     },
   .line_width = 1.0f,
   .depth_bias =
     {
       .bias = 0.0f,
       .clamp = 0.0f,
       .slope = 0.0f,
     },
   .blend_constants = { 0.0f, 0.0f, 0.0f, 0.0f },
   .depth_bounds =
     {
       .min = 0.0f,
       .max = 1.0f,
     },
   .stencil_compare_mask =
     {
       .front = ~0u,
       .back = ~0u,
     },
   .stencil_write_mask =
     {
       .front = ~0u,
       .back = ~0u,
     },
   .stencil_reference =
     {
       .front = 0u,
       .back = 0u,
     },
};

static void UNUSED /* FINISHME */
tu_bind_dynamic_state(struct tu_cmd_buffer *cmd_buffer,
                      const struct tu_dynamic_state *src)
{
   struct tu_dynamic_state *dest = &cmd_buffer->state.dynamic;
   uint32_t copy_mask = src->mask;
   uint32_t dest_mask = 0;

   tu_use_args(cmd_buffer); /* FINISHME */

   /* Make sure to copy the number of viewports/scissors because they can
    * only be specified at pipeline creation time.
    */
   dest->viewport.count = src->viewport.count;
   dest->scissor.count = src->scissor.count;
   dest->discard_rectangle.count = src->discard_rectangle.count;

   if (copy_mask & TU_DYNAMIC_VIEWPORT) {
      if (memcmp(&dest->viewport.viewports, &src->viewport.viewports,
                 src->viewport.count * sizeof(VkViewport))) {
         typed_memcpy(dest->viewport.viewports, src->viewport.viewports,
                      src->viewport.count);
         dest_mask |= TU_DYNAMIC_VIEWPORT;
      }
   }

   if (copy_mask & TU_DYNAMIC_SCISSOR) {
      if (memcmp(&dest->scissor.scissors, &src->scissor.scissors,
                 src->scissor.count * sizeof(VkRect2D))) {
         typed_memcpy(dest->scissor.scissors, src->scissor.scissors,
                      src->scissor.count);
         dest_mask |= TU_DYNAMIC_SCISSOR;
      }
   }

   if (copy_mask & TU_DYNAMIC_LINE_WIDTH) {
      if (dest->line_width != src->line_width) {
         dest->line_width = src->line_width;
         dest_mask |= TU_DYNAMIC_LINE_WIDTH;
      }
   }

   if (copy_mask & TU_DYNAMIC_DEPTH_BIAS) {
      if (memcmp(&dest->depth_bias, &src->depth_bias,
                 sizeof(src->depth_bias))) {
         dest->depth_bias = src->depth_bias;
         dest_mask |= TU_DYNAMIC_DEPTH_BIAS;
      }
   }

   if (copy_mask & TU_DYNAMIC_BLEND_CONSTANTS) {
      if (memcmp(&dest->blend_constants, &src->blend_constants,
                 sizeof(src->blend_constants))) {
         typed_memcpy(dest->blend_constants, src->blend_constants, 4);
         dest_mask |= TU_DYNAMIC_BLEND_CONSTANTS;
      }
   }

   if (copy_mask & TU_DYNAMIC_DEPTH_BOUNDS) {
      if (memcmp(&dest->depth_bounds, &src->depth_bounds,
                 sizeof(src->depth_bounds))) {
         dest->depth_bounds = src->depth_bounds;
         dest_mask |= TU_DYNAMIC_DEPTH_BOUNDS;
      }
   }

   if (copy_mask & TU_DYNAMIC_STENCIL_COMPARE_MASK) {
      if (memcmp(&dest->stencil_compare_mask, &src->stencil_compare_mask,
                 sizeof(src->stencil_compare_mask))) {
         dest->stencil_compare_mask = src->stencil_compare_mask;
         dest_mask |= TU_DYNAMIC_STENCIL_COMPARE_MASK;
      }
   }

   if (copy_mask & TU_DYNAMIC_STENCIL_WRITE_MASK) {
      if (memcmp(&dest->stencil_write_mask, &src->stencil_write_mask,
                 sizeof(src->stencil_write_mask))) {
         dest->stencil_write_mask = src->stencil_write_mask;
         dest_mask |= TU_DYNAMIC_STENCIL_WRITE_MASK;
      }
   }

   if (copy_mask & TU_DYNAMIC_STENCIL_REFERENCE) {
      if (memcmp(&dest->stencil_reference, &src->stencil_reference,
                 sizeof(src->stencil_reference))) {
         dest->stencil_reference = src->stencil_reference;
         dest_mask |= TU_DYNAMIC_STENCIL_REFERENCE;
      }
   }

   if (copy_mask & TU_DYNAMIC_DISCARD_RECTANGLE) {
      if (memcmp(&dest->discard_rectangle.rectangles,
                 &src->discard_rectangle.rectangles,
                 src->discard_rectangle.count * sizeof(VkRect2D))) {
         typed_memcpy(dest->discard_rectangle.rectangles,
                      src->discard_rectangle.rectangles,
                      src->discard_rectangle.count);
         dest_mask |= TU_DYNAMIC_DISCARD_RECTANGLE;
      }
   }
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

   for (unsigned i = 0; i < VK_PIPELINE_BIND_POINT_RANGE_SIZE; i++)
      free(cmd_buffer->descriptors[i].push_set.set.mapped_ptr);

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
   cmd_buffer->wait_for_idle = true;

   cmd_buffer->record_result = VK_SUCCESS;

   tu_bo_list_reset(&cmd_buffer->bo_list);
   tu_cs_reset(&cmd_buffer->cs);
   tu_cs_reset(&cmd_buffer->draw_cs);
   tu_cs_reset(&cmd_buffer->draw_epilogue_cs);
   tu_cs_reset(&cmd_buffer->sub_cs);

   for (unsigned i = 0; i < VK_PIPELINE_BIND_POINT_RANGE_SIZE; i++) {
      cmd_buffer->descriptors[i].valid = 0;
      cmd_buffer->descriptors[i].push_dirty = false;
   }

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
   cmd_buffer->usage_flags = pBeginInfo->flags;

   tu_cs_begin(&cmd_buffer->cs);
   tu_cs_begin(&cmd_buffer->draw_cs);
   tu_cs_begin(&cmd_buffer->draw_epilogue_cs);

   cmd_buffer->scratch_seqno = 0;

   /* setup initial configuration into command buffer */
   if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
      switch (cmd_buffer->queue_family_index) {
      case TU_QUEUE_GENERAL:
         tu6_init_hw(cmd_buffer, &cmd_buffer->cs);
         break;
      default:
         break;
      }
   } else if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY &&
              (pBeginInfo->flags & VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT)) {
      assert(pBeginInfo->pInheritanceInfo);
      cmd_buffer->state.pass = tu_render_pass_from_handle(pBeginInfo->pInheritanceInfo->renderPass);
      cmd_buffer->state.subpass = &cmd_buffer->state.pass->subpasses[pBeginInfo->pInheritanceInfo->subpass];
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
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   assert(firstBinding + bindingCount <= MAX_VBS);

   for (uint32_t i = 0; i < bindingCount; i++) {
      cmd->state.vb.buffers[firstBinding + i] =
         tu_buffer_from_handle(pBuffers[i]);
      cmd->state.vb.offsets[firstBinding + i] = pOffsets[i];
   }

   /* VB states depend on VkPipelineVertexInputStateCreateInfo */
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

   /* initialize/update the restart index */
   if (!cmd->state.index_buffer || cmd->state.index_type != indexType) {
      struct tu_cs *draw_cs = &cmd->draw_cs;

      tu6_emit_restart_index(
         draw_cs, indexType == VK_INDEX_TYPE_UINT32 ? 0xffffffff : 0xffff);

      tu_cs_sanity_check(draw_cs);
   }

   /* track the BO */
   if (cmd->state.index_buffer != buf)
      tu_bo_list_add(&cmd->bo_list, buf->bo, MSM_SUBMIT_BO_READ);

   cmd->state.index_buffer = buf;
   cmd->state.index_offset = offset;
   cmd->state.index_type = indexType;
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
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   TU_FROM_HANDLE(tu_pipeline_layout, layout, _layout);
   unsigned dyn_idx = 0;

   struct tu_descriptor_state *descriptors_state =
      tu_get_descriptors_state(cmd_buffer, pipelineBindPoint);

   for (unsigned i = 0; i < descriptorSetCount; ++i) {
      unsigned idx = i + firstSet;
      TU_FROM_HANDLE(tu_descriptor_set, set, pDescriptorSets[i]);

      descriptors_state->sets[idx] = set;
      descriptors_state->valid |= (1u << idx);

      /* Note: the actual input attachment indices come from the shader
       * itself, so we can't generate the patched versions of these until
       * draw time when both the pipeline and descriptors are bound and
       * we're inside the render pass.
       */
      unsigned dst_idx = layout->set[idx].input_attachment_start;
      memcpy(&descriptors_state->input_attachments[dst_idx * A6XX_TEX_CONST_DWORDS],
             set->dynamic_descriptors,
             set->layout->input_attachment_count * A6XX_TEX_CONST_DWORDS * 4);

      for(unsigned j = 0; j < set->layout->dynamic_offset_count; ++j, ++dyn_idx) {
         /* Dynamic buffers come after input attachments in the descriptor set
          * itself, but due to how the Vulkan descriptor set binding works, we
          * have to put input attachments and dynamic buffers in separate
          * buffers in the descriptor_state and then combine them at draw
          * time. Binding a descriptor set only invalidates the descriptor
          * sets after it, but if we try to tightly pack the descriptors after
          * the input attachments then we could corrupt dynamic buffers in the
          * descriptor set before it, or we'd have to move all the dynamic
          * buffers over. We just put them into separate buffers to make
          * binding as well as the later patching of input attachments easy.
          */
         unsigned src_idx = j + set->layout->input_attachment_count;
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

   if (pipelineBindPoint == VK_PIPELINE_BIND_POINT_COMPUTE)
      cmd_buffer->state.dirty |= TU_CMD_DIRTY_COMPUTE_DESCRIPTOR_SETS;
   else
      cmd_buffer->state.dirty |= TU_CMD_DIRTY_DESCRIPTOR_SETS;
}

void tu_CmdBindTransformFeedbackBuffersEXT(VkCommandBuffer commandBuffer,
                                           uint32_t firstBinding,
                                           uint32_t bindingCount,
                                           const VkBuffer *pBuffers,
                                           const VkDeviceSize *pOffsets,
                                           const VkDeviceSize *pSizes)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   assert(firstBinding + bindingCount <= IR3_MAX_SO_BUFFERS);

   for (uint32_t i = 0; i < bindingCount; i++) {
      uint32_t idx = firstBinding + i;
      TU_FROM_HANDLE(tu_buffer, buf, pBuffers[i]);

      if (pOffsets[i] != 0)
         cmd->state.streamout_reset |= 1 << idx;

      cmd->state.streamout_buf.buffers[idx] = buf;
      cmd->state.streamout_buf.offsets[idx] = pOffsets[i];
      cmd->state.streamout_buf.sizes[idx] = pSizes[i];

      cmd->state.streamout_enabled |= 1 << idx;
   }

   cmd->state.dirty |= TU_CMD_DIRTY_STREAMOUT_BUFFERS;
}

void tu_CmdBeginTransformFeedbackEXT(VkCommandBuffer commandBuffer,
                                       uint32_t firstCounterBuffer,
                                       uint32_t counterBufferCount,
                                       const VkBuffer *pCounterBuffers,
                                       const VkDeviceSize *pCounterBufferOffsets)
{
   assert(firstCounterBuffer + counterBufferCount <= IR3_MAX_SO_BUFFERS);
   /* TODO do something with counter buffer? */
}

void tu_CmdEndTransformFeedbackEXT(VkCommandBuffer commandBuffer,
                                       uint32_t firstCounterBuffer,
                                       uint32_t counterBufferCount,
                                       const VkBuffer *pCounterBuffers,
                                       const VkDeviceSize *pCounterBufferOffsets)
{
   assert(firstCounterBuffer + counterBufferCount <= IR3_MAX_SO_BUFFERS);
   /* TODO do something with counter buffer? */

   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   cmd->state.streamout_enabled = 0;
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
   cmd->state.dirty |= TU_CMD_DIRTY_PUSH_CONSTANTS;
}

VkResult
tu_EndCommandBuffer(VkCommandBuffer commandBuffer)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);

   if (cmd_buffer->scratch_seqno) {
      tu_bo_list_add(&cmd_buffer->bo_list, &cmd_buffer->scratch_bo,
                     MSM_SUBMIT_BO_WRITE);
   }

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

void
tu_CmdBindPipeline(VkCommandBuffer commandBuffer,
                   VkPipelineBindPoint pipelineBindPoint,
                   VkPipeline _pipeline)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_pipeline, pipeline, _pipeline);

   switch (pipelineBindPoint) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      cmd->state.pipeline = pipeline;
      cmd->state.dirty |= TU_CMD_DIRTY_PIPELINE;
      break;
   case VK_PIPELINE_BIND_POINT_COMPUTE:
      cmd->state.compute_pipeline = pipeline;
      cmd->state.dirty |= TU_CMD_DIRTY_COMPUTE_PIPELINE;
      break;
   default:
      unreachable("unrecognized pipeline bind point");
      break;
   }

   tu_bo_list_add(&cmd->bo_list, &pipeline->program.binary_bo,
                  MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_DUMP);
   for (uint32_t i = 0; i < pipeline->cs.bo_count; i++) {
      tu_bo_list_add(&cmd->bo_list, pipeline->cs.bos[i],
                     MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_DUMP);
   }
}

void
tu_CmdSetViewport(VkCommandBuffer commandBuffer,
                  uint32_t firstViewport,
                  uint32_t viewportCount,
                  const VkViewport *pViewports)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   assert(firstViewport == 0 && viewportCount == 1);
   cmd->state.dynamic.viewport.viewports[0] = pViewports[0];
   cmd->state.dirty |= TU_CMD_DIRTY_DYNAMIC_VIEWPORT;
}

void
tu_CmdSetScissor(VkCommandBuffer commandBuffer,
                 uint32_t firstScissor,
                 uint32_t scissorCount,
                 const VkRect2D *pScissors)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   assert(firstScissor == 0 && scissorCount == 1);
   cmd->state.dynamic.scissor.scissors[0] = pScissors[0];
   cmd->state.dirty |= TU_CMD_DIRTY_DYNAMIC_SCISSOR;
}

void
tu_CmdSetLineWidth(VkCommandBuffer commandBuffer, float lineWidth)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   cmd->state.dynamic.line_width = lineWidth;

   /* line width depends on VkPipelineRasterizationStateCreateInfo */
   cmd->state.dirty |= TU_CMD_DIRTY_DYNAMIC_LINE_WIDTH;
}

void
tu_CmdSetDepthBias(VkCommandBuffer commandBuffer,
                   float depthBiasConstantFactor,
                   float depthBiasClamp,
                   float depthBiasSlopeFactor)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *draw_cs = &cmd->draw_cs;

   tu6_emit_depth_bias(draw_cs, depthBiasConstantFactor, depthBiasClamp,
                       depthBiasSlopeFactor);

   tu_cs_sanity_check(draw_cs);
}

void
tu_CmdSetBlendConstants(VkCommandBuffer commandBuffer,
                        const float blendConstants[4])
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *draw_cs = &cmd->draw_cs;

   tu6_emit_blend_constants(draw_cs, blendConstants);

   tu_cs_sanity_check(draw_cs);
}

void
tu_CmdSetDepthBounds(VkCommandBuffer commandBuffer,
                     float minDepthBounds,
                     float maxDepthBounds)
{
}

void
tu_CmdSetStencilCompareMask(VkCommandBuffer commandBuffer,
                            VkStencilFaceFlags faceMask,
                            uint32_t compareMask)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
      cmd->state.dynamic.stencil_compare_mask.front = compareMask;
   if (faceMask & VK_STENCIL_FACE_BACK_BIT)
      cmd->state.dynamic.stencil_compare_mask.back = compareMask;

   /* the front/back compare masks must be updated together */
   cmd->state.dirty |= TU_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK;
}

void
tu_CmdSetStencilWriteMask(VkCommandBuffer commandBuffer,
                          VkStencilFaceFlags faceMask,
                          uint32_t writeMask)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
      cmd->state.dynamic.stencil_write_mask.front = writeMask;
   if (faceMask & VK_STENCIL_FACE_BACK_BIT)
      cmd->state.dynamic.stencil_write_mask.back = writeMask;

   /* the front/back write masks must be updated together */
   cmd->state.dirty |= TU_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK;
}

void
tu_CmdSetStencilReference(VkCommandBuffer commandBuffer,
                          VkStencilFaceFlags faceMask,
                          uint32_t reference)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   if (faceMask & VK_STENCIL_FACE_FRONT_BIT)
      cmd->state.dynamic.stencil_reference.front = reference;
   if (faceMask & VK_STENCIL_FACE_BACK_BIT)
      cmd->state.dynamic.stencil_reference.back = reference;

   /* the front/back references must be updated together */
   cmd->state.dirty |= TU_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE;
}

void
tu_CmdSetSampleLocationsEXT(VkCommandBuffer commandBuffer,
                            const VkSampleLocationsInfoEXT* pSampleLocationsInfo)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);

   tu6_emit_sample_locations(&cmd->draw_cs, pSampleLocationsInfo);
}

void
tu_CmdExecuteCommands(VkCommandBuffer commandBuffer,
                      uint32_t commandBufferCount,
                      const VkCommandBuffer *pCmdBuffers)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   VkResult result;

   assert(commandBufferCount > 0);

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
      } else {
         assert(tu_cs_is_empty(&secondary->draw_cs));
         assert(tu_cs_is_empty(&secondary->draw_epilogue_cs));

         for (uint32_t j = 0; j < secondary->cs.bo_count; j++) {
            tu_bo_list_add(&cmd->bo_list, secondary->cs.bos[j],
                           MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_DUMP);
         }

         tu_cs_add_entries(&cmd->cs, &secondary->cs);
      }
   }
   cmd->state.dirty = ~0u; /* TODO: set dirty only what needs to be */
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

   tu_emit_load_clear(cmd, pRenderPassBegin);

   tu6_emit_zs(cmd, cmd->state.subpass, &cmd->draw_cs);
   tu6_emit_mrt(cmd, cmd->state.subpass, &cmd->draw_cs);
   tu6_emit_msaa(&cmd->draw_cs, cmd->state.subpass->samples);
   tu6_emit_render_cntl(cmd, cmd->state.subpass, &cmd->draw_cs, false);

   /* note: use_hw_binning only checks tiling config */
   if (use_hw_binning(cmd))
      cmd->use_vsc_data = true;

   for (uint32_t i = 0; i < fb->attachment_count; ++i) {
      const struct tu_image_view *iview = fb->attachments[i].attachment;
      tu_bo_list_add(&cmd->bo_list, iview->image->bo,
                     MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE);
   }

   /* Flag input attachment descriptors for re-emission if necessary */
   cmd->state.dirty |= TU_CMD_DIRTY_INPUT_ATTACHMENTS;
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

   /* Emit flushes so that input attachments will read the correct value.
    * TODO: use subpass dependencies to flush or not
    */
   tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_COLOR_TS, true);
   tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_DEPTH_TS, true);

   if (subpass->resolve_attachments) {
      tu6_emit_event_write(cmd, cs, CACHE_INVALIDATE, false);

      for (unsigned i = 0; i < subpass->color_count; i++) {
         uint32_t a = subpass->resolve_attachments[i].attachment;
         if (a == VK_ATTACHMENT_UNUSED)
            continue;

         tu6_emit_sysmem_resolve(cmd, cs, a,
                                 subpass->color_attachments[i].attachment);
      }

      tu6_emit_event_write(cmd, cs, PC_CCU_FLUSH_COLOR_TS, true);
   }

   tu_cond_exec_end(cs);

   /* subpass->input_count > 0 then texture cache invalidate is likely to be needed */
   if (cmd->state.subpass->input_count)
      tu6_emit_event_write(cmd, cs, CACHE_INVALIDATE, false);

   /* emit mrt/zs/msaa/ubwc state for the subpass that is starting */
   tu6_emit_zs(cmd, cmd->state.subpass, cs);
   tu6_emit_mrt(cmd, cmd->state.subpass, cs);
   tu6_emit_msaa(cs, cmd->state.subpass->samples);
   tu6_emit_render_cntl(cmd, cmd->state.subpass, cs, false);

   /* Flag input attachment descriptors for re-emission if necessary */
   cmd->state.dirty |= TU_CMD_DIRTY_INPUT_ATTACHMENTS;
}

void
tu_CmdNextSubpass2(VkCommandBuffer commandBuffer,
                   const VkSubpassBeginInfoKHR *pSubpassBeginInfo,
                   const VkSubpassEndInfoKHR *pSubpassEndInfo)
{
   tu_CmdNextSubpass(commandBuffer, pSubpassBeginInfo->contents);
}

struct tu_draw_info
{
   /**
    * Number of vertices.
    */
   uint32_t count;

   /**
    * Index of the first vertex.
    */
   int32_t vertex_offset;

   /**
    * First instance id.
    */
   uint32_t first_instance;

   /**
    * Number of instances.
    */
   uint32_t instance_count;

   /**
    * First index (indexed draws only).
    */
   uint32_t first_index;

   /**
    * Whether it's an indexed draw.
    */
   bool indexed;

   /**
    * Indirect draw parameters resource.
    */
   struct tu_buffer *indirect;
   uint64_t indirect_offset;
   uint32_t stride;

   /**
    * Draw count parameters resource.
    */
   struct tu_buffer *count_buffer;
   uint64_t count_buffer_offset;

   /**
    * Stream output parameters resource.
    */
   struct tu_buffer *streamout_buffer;
   uint64_t streamout_buffer_offset;
};

#define ENABLE_ALL (CP_SET_DRAW_STATE__0_BINNING | CP_SET_DRAW_STATE__0_GMEM | CP_SET_DRAW_STATE__0_SYSMEM)
#define ENABLE_DRAW (CP_SET_DRAW_STATE__0_GMEM | CP_SET_DRAW_STATE__0_SYSMEM)
#define ENABLE_NON_GMEM (CP_SET_DRAW_STATE__0_BINNING | CP_SET_DRAW_STATE__0_SYSMEM)

enum tu_draw_state_group_id
{
   TU_DRAW_STATE_PROGRAM,
   TU_DRAW_STATE_PROGRAM_BINNING,
   TU_DRAW_STATE_VI,
   TU_DRAW_STATE_VI_BINNING,
   TU_DRAW_STATE_VP,
   TU_DRAW_STATE_RAST,
   TU_DRAW_STATE_DS,
   TU_DRAW_STATE_BLEND,
   TU_DRAW_STATE_VS_CONST,
   TU_DRAW_STATE_GS_CONST,
   TU_DRAW_STATE_FS_CONST,
   TU_DRAW_STATE_DESC_SETS,
   TU_DRAW_STATE_DESC_SETS_GMEM,
   TU_DRAW_STATE_DESC_SETS_LOAD,
   TU_DRAW_STATE_VS_PARAMS,

   TU_DRAW_STATE_COUNT,
};

struct tu_draw_state_group
{
   enum tu_draw_state_group_id id;
   uint32_t enable_mask;
   struct tu_cs_entry ib;
};

static inline uint32_t
tu6_stage2opcode(gl_shader_stage type)
{
   switch (type) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_TESS_CTRL:
   case MESA_SHADER_TESS_EVAL:
   case MESA_SHADER_GEOMETRY:
      return CP_LOAD_STATE6_GEOM;
   case MESA_SHADER_FRAGMENT:
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      return CP_LOAD_STATE6_FRAG;
   default:
      unreachable("bad shader type");
   }
}

static inline enum a6xx_state_block
tu6_stage2shadersb(gl_shader_stage type)
{
   switch (type) {
   case MESA_SHADER_VERTEX:
      return SB6_VS_SHADER;
   case MESA_SHADER_GEOMETRY:
      return SB6_GS_SHADER;
   case MESA_SHADER_FRAGMENT:
      return SB6_FS_SHADER;
   case MESA_SHADER_COMPUTE:
   case MESA_SHADER_KERNEL:
      return SB6_CS_SHADER;
   default:
      unreachable("bad shader type");
      return ~0;
   }
}

static void
tu6_emit_user_consts(struct tu_cs *cs, const struct tu_pipeline *pipeline,
                     struct tu_descriptor_state *descriptors_state,
                     gl_shader_stage type,
                     uint32_t *push_constants)
{
   const struct tu_program_descriptor_linkage *link =
      &pipeline->program.link[type];
   const struct ir3_ubo_analysis_state *state = &link->ubo_state;

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
      assert(state->range[i].bindless);
      uint32_t *base = state->range[i].bindless_base == MAX_SETS ?
         descriptors_state->dynamic_descriptors :
         descriptors_state->sets[state->range[i].bindless_base]->mapped_ptr;
      unsigned block = state->range[i].block;
      /* If the block in the shader here is in the dynamic descriptor set, it
       * is an index into the dynamic descriptor set which is combined from
       * dynamic descriptors and input attachments on-the-fly, and we don't
       * have access to it here. Instead we work backwards to get the index
       * into dynamic_descriptors.
       */
      if (state->range[i].bindless_base == MAX_SETS)
         block -= pipeline->layout->input_attachment_count;
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

static VkResult
tu6_emit_vs_params(struct tu_cmd_buffer *cmd,
                   const struct tu_draw_info *draw,
                   struct tu_cs_entry *entry)
{
   /* TODO: fill out more than just base instance */
   const struct tu_program_descriptor_linkage *link =
      &cmd->state.pipeline->program.link[MESA_SHADER_VERTEX];
   const struct ir3_const_state *const_state = &link->const_state;
   struct tu_cs cs;

   if (const_state->offsets.driver_param >= link->constlen) {
      *entry = (struct tu_cs_entry) {};
      return VK_SUCCESS;
   }

   VkResult result = tu_cs_begin_sub_stream(&cmd->sub_cs, 8, &cs);
   if (result != VK_SUCCESS)
      return result;

   tu_cs_emit_pkt7(&cs, CP_LOAD_STATE6_GEOM, 3 + 4);
   tu_cs_emit(&cs, CP_LOAD_STATE6_0_DST_OFF(const_state->offsets.driver_param) |
         CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
         CP_LOAD_STATE6_0_STATE_SRC(SS6_DIRECT) |
         CP_LOAD_STATE6_0_STATE_BLOCK(SB6_VS_SHADER) |
         CP_LOAD_STATE6_0_NUM_UNIT(1));
   tu_cs_emit(&cs, 0);
   tu_cs_emit(&cs, 0);

   STATIC_ASSERT(IR3_DP_INSTID_BASE == 2);

   tu_cs_emit(&cs, 0);
   tu_cs_emit(&cs, 0);
   tu_cs_emit(&cs, draw->first_instance);
   tu_cs_emit(&cs, 0);

   *entry = tu_cs_end_sub_stream(&cmd->sub_cs, &cs);
   return VK_SUCCESS;
}

static VkResult
tu6_emit_descriptor_sets(struct tu_cmd_buffer *cmd,
                         const struct tu_pipeline *pipeline,
                         VkPipelineBindPoint bind_point,
                         struct tu_cs_entry *entry,
                         bool gmem)
{
   struct tu_cs *draw_state = &cmd->sub_cs;
   struct tu_pipeline_layout *layout = pipeline->layout;
   struct tu_descriptor_state *descriptors_state =
      tu_get_descriptors_state(cmd, bind_point);
   const struct tu_tiling_config *tiling = &cmd->state.tiling_config;
   const uint32_t *input_attachment_idx =
      pipeline->program.input_attachment_idx;
   uint32_t num_dynamic_descs = layout->dynamic_offset_count +
      layout->input_attachment_count;
   struct ts_cs_memory dynamic_desc_set;
   VkResult result;

   if (num_dynamic_descs > 0) {
      /* allocate and fill out dynamic descriptor set */
      result = tu_cs_alloc(draw_state, num_dynamic_descs,
                           A6XX_TEX_CONST_DWORDS, &dynamic_desc_set);
      if (result != VK_SUCCESS)
         return result;

      memcpy(dynamic_desc_set.map, descriptors_state->input_attachments,
             layout->input_attachment_count * A6XX_TEX_CONST_DWORDS * 4);

      if (gmem) {
         /* Patch input attachments to refer to GMEM instead */
         for (unsigned i = 0; i < layout->input_attachment_count; i++) {
            uint32_t *dst =
               &dynamic_desc_set.map[A6XX_TEX_CONST_DWORDS * i];

            /* The compiler has already laid out input_attachment_idx in the
             * final order of input attachments, so there's no need to go
             * through the pipeline layout finding input attachments.
             */
            unsigned attachment_idx = input_attachment_idx[i];

            /* It's possible for the pipeline layout to include an input
             * attachment which doesn't actually exist for the current
             * subpass. Of course, this is only valid so long as the pipeline
             * doesn't try to actually load that attachment. Just skip
             * patching in that scenario to avoid out-of-bounds accesses.
             */
            if (attachment_idx >= cmd->state.subpass->input_count)
               continue;

            uint32_t a = cmd->state.subpass->input_attachments[attachment_idx].attachment;
            const struct tu_render_pass_attachment *att = &cmd->state.pass->attachments[a];

            assert(att->gmem_offset >= 0);

            dst[0] &= ~(A6XX_TEX_CONST_0_SWAP__MASK | A6XX_TEX_CONST_0_TILE_MODE__MASK);
            dst[0] |= A6XX_TEX_CONST_0_TILE_MODE(TILE6_2);
            dst[2] &= ~(A6XX_TEX_CONST_2_TYPE__MASK | A6XX_TEX_CONST_2_PITCH__MASK);
            dst[2] |=
               A6XX_TEX_CONST_2_TYPE(A6XX_TEX_2D) |
               A6XX_TEX_CONST_2_PITCH(tiling->tile0.extent.width * att->cpp);
            dst[3] = 0;
            dst[4] = cmd->device->physical_device->gmem_base + att->gmem_offset;
            dst[5] = A6XX_TEX_CONST_5_DEPTH(1);
            for (unsigned i = 6; i < A6XX_TEX_CONST_DWORDS; i++)
               dst[i] = 0;

            if (cmd->level == VK_COMMAND_BUFFER_LEVEL_SECONDARY)
               tu_finishme("patch input attachment pitch for secondary cmd buffer");
         }
      }

      memcpy(dynamic_desc_set.map + layout->input_attachment_count * A6XX_TEX_CONST_DWORDS,
             descriptors_state->dynamic_descriptors,
             layout->dynamic_offset_count * A6XX_TEX_CONST_DWORDS * 4);
   }

   uint32_t sp_bindless_base_reg, hlsq_bindless_base_reg;
   uint32_t hlsq_update_value;
   switch (bind_point) {
   case VK_PIPELINE_BIND_POINT_GRAPHICS:
      sp_bindless_base_reg = REG_A6XX_SP_BINDLESS_BASE(0);
      hlsq_bindless_base_reg = REG_A6XX_HLSQ_BINDLESS_BASE(0);
      hlsq_update_value = 0x7c000;
      break;
   case VK_PIPELINE_BIND_POINT_COMPUTE:
      sp_bindless_base_reg = REG_A6XX_SP_CS_BINDLESS_BASE(0);
      hlsq_bindless_base_reg = REG_A6XX_HLSQ_CS_BINDLESS_BASE(0);
      hlsq_update_value = 0x3e00;
      break;
   default:
      unreachable("bad bind point");
   }

   /* Be careful here to *not* refer to the pipeline, so that if only the
    * pipeline changes we don't have to emit this again (except if there are
    * dynamic descriptors in the pipeline layout). This means always emitting
    * all the valid descriptors, which means that we always have to put the
    * dynamic descriptor in the driver-only slot at the end
    */
   uint32_t num_user_sets = util_last_bit(descriptors_state->valid);
   uint32_t num_sets = num_user_sets;
   if (num_dynamic_descs > 0) {
      num_user_sets = MAX_SETS;
      num_sets = num_user_sets + 1;
   }

   unsigned regs[2] = { sp_bindless_base_reg, hlsq_bindless_base_reg };

   struct tu_cs cs;
   result = tu_cs_begin_sub_stream(draw_state, ARRAY_SIZE(regs) * (1 + num_sets * 2) + 2, &cs);
   if (result != VK_SUCCESS)
      return result;

   if (num_sets > 0) {
      for (unsigned i = 0; i < ARRAY_SIZE(regs); i++) {
         tu_cs_emit_pkt4(&cs, regs[i], num_sets * 2);
         for (unsigned j = 0; j < num_user_sets; j++) {
            if (descriptors_state->valid & (1 << j)) {
               /* magic | 3 copied from the blob */
               tu_cs_emit_qw(&cs, descriptors_state->sets[j]->va | 3);
            } else {
               tu_cs_emit_qw(&cs, 0 | 3);
            }
         }
         if (num_dynamic_descs > 0) {
            tu_cs_emit_qw(&cs, dynamic_desc_set.iova | 3);
         }
      }

      tu_cs_emit_regs(&cs, A6XX_HLSQ_UPDATE_CNTL(hlsq_update_value));
   }

   *entry = tu_cs_end_sub_stream(draw_state, &cs);
   return VK_SUCCESS;
}

static void
tu6_emit_streamout(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   struct tu_streamout_state *tf = &cmd->state.pipeline->streamout;

   for (unsigned i = 0; i < IR3_MAX_SO_BUFFERS; i++) {
      struct tu_buffer *buf = cmd->state.streamout_buf.buffers[i];
      if (!buf)
         continue;

      uint32_t offset;
      offset = cmd->state.streamout_buf.offsets[i];

      tu_cs_emit_regs(cs, A6XX_VPC_SO_BUFFER_BASE(i, .bo = buf->bo,
                                                     .bo_offset = buf->bo_offset));
      tu_cs_emit_regs(cs, A6XX_VPC_SO_BUFFER_SIZE(i, buf->size));

      if (cmd->state.streamout_reset & (1 << i)) {
         tu_cs_emit_regs(cs, A6XX_VPC_SO_BUFFER_OFFSET(i, offset));
         cmd->state.streamout_reset &= ~(1  << i);
      } else {
         tu_cs_emit_pkt7(cs, CP_MEM_TO_REG, 3);
         tu_cs_emit(cs, CP_MEM_TO_REG_0_REG(REG_A6XX_VPC_SO_BUFFER_OFFSET(i)) |
                        CP_MEM_TO_REG_0_SHIFT_BY_2 | CP_MEM_TO_REG_0_UNK31 |
                        CP_MEM_TO_REG_0_CNT(0));
         tu_cs_emit_qw(cs, cmd->scratch_bo.iova +
                           ctrl_offset(flush_base[i].offset));
      }

      tu_cs_emit_regs(cs, A6XX_VPC_SO_FLUSH_BASE(i, .bo = &cmd->scratch_bo,
                                                    .bo_offset =
                                                       ctrl_offset(flush_base[i])));
   }

   if (cmd->state.streamout_enabled) {
      tu_cs_emit_pkt7(cs, CP_CONTEXT_REG_BUNCH, 12 + (2 * tf->prog_count));
      tu_cs_emit(cs, REG_A6XX_VPC_SO_BUF_CNTL);
      tu_cs_emit(cs, tf->vpc_so_buf_cntl);
      tu_cs_emit(cs, REG_A6XX_VPC_SO_NCOMP(0));
      tu_cs_emit(cs, tf->ncomp[0]);
      tu_cs_emit(cs, REG_A6XX_VPC_SO_NCOMP(1));
      tu_cs_emit(cs, tf->ncomp[1]);
      tu_cs_emit(cs, REG_A6XX_VPC_SO_NCOMP(2));
      tu_cs_emit(cs, tf->ncomp[2]);
      tu_cs_emit(cs, REG_A6XX_VPC_SO_NCOMP(3));
      tu_cs_emit(cs, tf->ncomp[3]);
      tu_cs_emit(cs, REG_A6XX_VPC_SO_CNTL);
      tu_cs_emit(cs, A6XX_VPC_SO_CNTL_ENABLE);
      for (unsigned i = 0; i < tf->prog_count; i++) {
         tu_cs_emit(cs, REG_A6XX_VPC_SO_PROG);
         tu_cs_emit(cs, tf->prog[i]);
      }
   } else {
      tu_cs_emit_pkt7(cs, CP_CONTEXT_REG_BUNCH, 4);
      tu_cs_emit(cs, REG_A6XX_VPC_SO_CNTL);
      tu_cs_emit(cs, 0);
      tu_cs_emit(cs, REG_A6XX_VPC_SO_BUF_CNTL);
      tu_cs_emit(cs, 0);
   }
}

static VkResult
tu6_bind_draw_states(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     const struct tu_draw_info *draw)
{
   const struct tu_pipeline *pipeline = cmd->state.pipeline;
   const struct tu_dynamic_state *dynamic = &cmd->state.dynamic;
   struct tu_draw_state_group draw_state_groups[TU_DRAW_STATE_COUNT];
   uint32_t draw_state_group_count = 0;
   VkResult result;

   struct tu_descriptor_state *descriptors_state =
      &cmd->descriptors[VK_PIPELINE_BIND_POINT_GRAPHICS];

   /* TODO lrz */

   tu_cs_emit_regs(cs,
                   A6XX_PC_PRIMITIVE_CNTL_0(.primitive_restart =
                                            pipeline->ia.primitive_restart && draw->indexed));

   if (cmd->state.dirty &
          (TU_CMD_DIRTY_PIPELINE | TU_CMD_DIRTY_DYNAMIC_LINE_WIDTH) &&
       (pipeline->dynamic_state.mask & TU_DYNAMIC_LINE_WIDTH)) {
      tu6_emit_gras_su_cntl(cs, pipeline->rast.gras_su_cntl,
                            dynamic->line_width);
   }

   if ((cmd->state.dirty & TU_CMD_DIRTY_DYNAMIC_STENCIL_COMPARE_MASK) &&
       (pipeline->dynamic_state.mask & TU_DYNAMIC_STENCIL_COMPARE_MASK)) {
      tu6_emit_stencil_compare_mask(cs, dynamic->stencil_compare_mask.front,
                                    dynamic->stencil_compare_mask.back);
   }

   if ((cmd->state.dirty & TU_CMD_DIRTY_DYNAMIC_STENCIL_WRITE_MASK) &&
       (pipeline->dynamic_state.mask & TU_DYNAMIC_STENCIL_WRITE_MASK)) {
      tu6_emit_stencil_write_mask(cs, dynamic->stencil_write_mask.front,
                                  dynamic->stencil_write_mask.back);
   }

   if ((cmd->state.dirty & TU_CMD_DIRTY_DYNAMIC_STENCIL_REFERENCE) &&
       (pipeline->dynamic_state.mask & TU_DYNAMIC_STENCIL_REFERENCE)) {
      tu6_emit_stencil_reference(cs, dynamic->stencil_reference.front,
                                 dynamic->stencil_reference.back);
   }

   if ((cmd->state.dirty & TU_CMD_DIRTY_DYNAMIC_VIEWPORT) &&
       (pipeline->dynamic_state.mask & TU_DYNAMIC_VIEWPORT)) {
      tu6_emit_viewport(cs, &cmd->state.dynamic.viewport.viewports[0]);
   }

   if ((cmd->state.dirty & TU_CMD_DIRTY_DYNAMIC_SCISSOR) &&
       (pipeline->dynamic_state.mask & TU_DYNAMIC_SCISSOR)) {
      tu6_emit_scissor(cs, &cmd->state.dynamic.scissor.scissors[0]);
   }

   if (cmd->state.dirty &
       (TU_CMD_DIRTY_PIPELINE | TU_CMD_DIRTY_VERTEX_BUFFERS)) {
      for (uint32_t i = 0; i < pipeline->vi.count; i++) {
         const uint32_t binding = pipeline->vi.bindings[i];
         const struct tu_buffer *buf = cmd->state.vb.buffers[binding];
         const VkDeviceSize offset = buf->bo_offset +
                                     cmd->state.vb.offsets[binding];
         const VkDeviceSize size =
            offset < buf->size ? buf->size - offset : 0;

         tu_cs_emit_regs(cs,
                         A6XX_VFD_FETCH_BASE(i, .bo = buf->bo, .bo_offset = offset),
                         A6XX_VFD_FETCH_SIZE(i, size));
      }
   }

   if (cmd->state.dirty & TU_CMD_DIRTY_PIPELINE) {
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_PROGRAM,
            .enable_mask = ENABLE_DRAW,
            .ib = pipeline->program.state_ib,
         };
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_PROGRAM_BINNING,
            .enable_mask = CP_SET_DRAW_STATE__0_BINNING,
            .ib = pipeline->program.binning_state_ib,
         };
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_VI,
            .enable_mask = ENABLE_DRAW,
            .ib = pipeline->vi.state_ib,
         };
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_VI_BINNING,
            .enable_mask = CP_SET_DRAW_STATE__0_BINNING,
            .ib = pipeline->vi.binning_state_ib,
         };
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_VP,
            .enable_mask = ENABLE_ALL,
            .ib = pipeline->vp.state_ib,
         };
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_RAST,
            .enable_mask = ENABLE_ALL,
            .ib = pipeline->rast.state_ib,
         };
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_DS,
            .enable_mask = ENABLE_ALL,
            .ib = pipeline->ds.state_ib,
         };
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_BLEND,
            .enable_mask = ENABLE_ALL,
            .ib = pipeline->blend.state_ib,
         };
   }

   if (cmd->state.dirty &
         (TU_CMD_DIRTY_PIPELINE | TU_CMD_DIRTY_DESCRIPTOR_SETS | TU_CMD_DIRTY_PUSH_CONSTANTS)) {
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_VS_CONST,
            .enable_mask = ENABLE_ALL,
            .ib = tu6_emit_consts(cmd, pipeline, descriptors_state, MESA_SHADER_VERTEX)
         };
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_GS_CONST,
            .enable_mask = ENABLE_ALL,
            .ib = tu6_emit_consts(cmd, pipeline, descriptors_state, MESA_SHADER_GEOMETRY)
         };
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_FS_CONST,
            .enable_mask = ENABLE_DRAW,
            .ib = tu6_emit_consts(cmd, pipeline, descriptors_state, MESA_SHADER_FRAGMENT)
         };
   }

   if (cmd->state.dirty & TU_CMD_DIRTY_STREAMOUT_BUFFERS)
      tu6_emit_streamout(cmd, cs);

   /* If there are any any dynamic descriptors, then we may need to re-emit
    * them after every pipeline change in case the number of input attachments
    * changes. We also always need to re-emit after a pipeline change if there
    * are any input attachments, because the input attachment index comes from
    * the pipeline. Finally, it can also happen that the subpass changes
    * without the pipeline changing, in which case the GMEM descriptors need
    * to be patched differently.
    *
    * TODO: We could probably be clever and avoid re-emitting state on
    * pipeline changes if the number of input attachments is always 0. We
    * could also only re-emit dynamic state.
    */
   if (cmd->state.dirty & TU_CMD_DIRTY_DESCRIPTOR_SETS ||
       ((pipeline->layout->dynamic_offset_count +
         pipeline->layout->input_attachment_count > 0) &&
        cmd->state.dirty & TU_CMD_DIRTY_PIPELINE) ||
       (pipeline->layout->input_attachment_count > 0 &&
        cmd->state.dirty & TU_CMD_DIRTY_INPUT_ATTACHMENTS)) {
      struct tu_cs_entry desc_sets, desc_sets_gmem;
      bool need_gmem_desc_set = pipeline->layout->input_attachment_count > 0;

      result = tu6_emit_descriptor_sets(cmd, pipeline,
                                        VK_PIPELINE_BIND_POINT_GRAPHICS,
                                        &desc_sets, false);
      if (result != VK_SUCCESS)
         return result;

      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_DESC_SETS,
            .enable_mask = need_gmem_desc_set ? ENABLE_NON_GMEM : ENABLE_ALL,
            .ib = desc_sets,
         };

      if (need_gmem_desc_set) {
         result = tu6_emit_descriptor_sets(cmd, pipeline,
                                           VK_PIPELINE_BIND_POINT_GRAPHICS,
                                           &desc_sets_gmem, true);
         if (result != VK_SUCCESS)
            return result;

         draw_state_groups[draw_state_group_count++] =
            (struct tu_draw_state_group) {
               .id = TU_DRAW_STATE_DESC_SETS_GMEM,
               .enable_mask = CP_SET_DRAW_STATE__0_GMEM,
               .ib = desc_sets_gmem,
            };
      }

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
         struct tu_cs_entry load_copy = tu_cs_end_sub_stream(&cmd->sub_cs, &load_cs);

         draw_state_groups[draw_state_group_count++] =
            (struct tu_draw_state_group) {
               .id = TU_DRAW_STATE_DESC_SETS_LOAD,
               /* The blob seems to not enable this for binning, even when
                * resources would actually be used in the binning shader.
                * Presumably the overhead of prefetching the resources isn't
                * worth it.
                */
               .enable_mask = ENABLE_DRAW,
               .ib = load_copy,
            };
      }
   }

   struct tu_cs_entry vs_params;
   result = tu6_emit_vs_params(cmd, draw, &vs_params);
   if (result != VK_SUCCESS)
      return result;

   draw_state_groups[draw_state_group_count++] =
      (struct tu_draw_state_group) {
         .id = TU_DRAW_STATE_VS_PARAMS,
         .enable_mask = ENABLE_ALL,
         .ib = vs_params,
      };

   tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3 * draw_state_group_count);
   for (uint32_t i = 0; i < draw_state_group_count; i++) {
      const struct tu_draw_state_group *group = &draw_state_groups[i];
      debug_assert((group->enable_mask & ~ENABLE_ALL) == 0);
      uint32_t cp_set_draw_state =
         CP_SET_DRAW_STATE__0_COUNT(group->ib.size / 4) |
         group->enable_mask |
         CP_SET_DRAW_STATE__0_GROUP_ID(group->id);
      uint64_t iova;
      if (group->ib.size) {
         iova = group->ib.bo->iova + group->ib.offset;
      } else {
         cp_set_draw_state |= CP_SET_DRAW_STATE__0_DISABLE;
         iova = 0;
      }

      tu_cs_emit(cs, cp_set_draw_state);
      tu_cs_emit_qw(cs, iova);
   }

   tu_cs_sanity_check(cs);

   /* track BOs */
   if (cmd->state.dirty & TU_CMD_DIRTY_VERTEX_BUFFERS) {
      for (uint32_t i = 0; i < MAX_VBS; i++) {
         const struct tu_buffer *buf = cmd->state.vb.buffers[i];
         if (buf)
            tu_bo_list_add(&cmd->bo_list, buf->bo, MSM_SUBMIT_BO_READ);
      }
   }
   if (cmd->state.dirty & TU_CMD_DIRTY_DESCRIPTOR_SETS) {
      unsigned i;
      for_each_bit(i, descriptors_state->valid) {
         struct tu_descriptor_set *set = descriptors_state->sets[i];
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
   }
   if (cmd->state.dirty & TU_CMD_DIRTY_STREAMOUT_BUFFERS) {
      for (unsigned i = 0; i < IR3_MAX_SO_BUFFERS; i++) {
         const struct tu_buffer *buf = cmd->state.streamout_buf.buffers[i];
         if (buf) {
            tu_bo_list_add(&cmd->bo_list, buf->bo,
                              MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE);
         }
      }
   }

   /* There are too many graphics dirty bits to list here, so just list the
    * bits to preserve instead. The only things not emitted here are
    * compute-related state.
    */
   cmd->state.dirty &= TU_CMD_DIRTY_COMPUTE_DESCRIPTOR_SETS;

   /* Fragment shader state overwrites compute shader state, so flag the
    * compute pipeline for re-emit.
    */
   cmd->state.dirty |= TU_CMD_DIRTY_COMPUTE_PIPELINE;
   return VK_SUCCESS;
}

static void
tu6_emit_draw_indirect(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     const struct tu_draw_info *draw)
{
   const enum pc_di_primtype primtype = cmd->state.pipeline->ia.primtype;
   bool has_gs = cmd->state.pipeline->active_stages &
                 VK_SHADER_STAGE_GEOMETRY_BIT;

   tu_cs_emit_regs(cs,
                   A6XX_VFD_INDEX_OFFSET(draw->vertex_offset),
                   A6XX_VFD_INSTANCE_START_OFFSET(draw->first_instance));

   if (draw->indexed) {
      const enum a4xx_index_size index_size =
         tu6_index_size(cmd->state.index_type);
      const uint32_t index_bytes =
         (cmd->state.index_type == VK_INDEX_TYPE_UINT32) ? 4 : 2;
      const struct tu_buffer *index_buf = cmd->state.index_buffer;
      unsigned max_indicies =
         (index_buf->size - cmd->state.index_offset) / index_bytes;

      const uint32_t cp_draw_indx =
         CP_DRAW_INDX_OFFSET_0_PRIM_TYPE(primtype) |
         CP_DRAW_INDX_OFFSET_0_SOURCE_SELECT(DI_SRC_SEL_DMA) |
         CP_DRAW_INDX_OFFSET_0_INDEX_SIZE(index_size) |
         CP_DRAW_INDX_OFFSET_0_VIS_CULL(USE_VISIBILITY) |
         COND(has_gs, CP_DRAW_INDX_OFFSET_0_GS_ENABLE) | 0x2000;

      tu_cs_emit_pkt7(cs, CP_DRAW_INDX_INDIRECT, 6);
      tu_cs_emit(cs, cp_draw_indx);
      tu_cs_emit_qw(cs, index_buf->bo->iova + cmd->state.index_offset);
      tu_cs_emit(cs, A5XX_CP_DRAW_INDX_INDIRECT_3_MAX_INDICES(max_indicies));
      tu_cs_emit_qw(cs, draw->indirect->bo->iova + draw->indirect_offset);
   } else {
      const uint32_t cp_draw_indx =
         CP_DRAW_INDX_OFFSET_0_PRIM_TYPE(primtype) |
         CP_DRAW_INDX_OFFSET_0_SOURCE_SELECT(DI_SRC_SEL_AUTO_INDEX) |
         CP_DRAW_INDX_OFFSET_0_VIS_CULL(USE_VISIBILITY) |
         COND(has_gs, CP_DRAW_INDX_OFFSET_0_GS_ENABLE) | 0x2000;

      tu_cs_emit_pkt7(cs, CP_DRAW_INDIRECT, 3);
      tu_cs_emit(cs, cp_draw_indx);
      tu_cs_emit_qw(cs, draw->indirect->bo->iova + draw->indirect_offset);
   }

   tu_bo_list_add(&cmd->bo_list, draw->indirect->bo, MSM_SUBMIT_BO_READ);
}

static void
tu6_emit_draw_direct(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     const struct tu_draw_info *draw)
{

   const enum pc_di_primtype primtype = cmd->state.pipeline->ia.primtype;
   bool has_gs = cmd->state.pipeline->active_stages &
                 VK_SHADER_STAGE_GEOMETRY_BIT;

   tu_cs_emit_regs(cs,
                   A6XX_VFD_INDEX_OFFSET(draw->vertex_offset),
                   A6XX_VFD_INSTANCE_START_OFFSET(draw->first_instance));

   /* TODO hw binning */
   if (draw->indexed) {
      const enum a4xx_index_size index_size =
         tu6_index_size(cmd->state.index_type);
      const uint32_t index_bytes =
         (cmd->state.index_type == VK_INDEX_TYPE_UINT32) ? 4 : 2;
      const struct tu_buffer *buf = cmd->state.index_buffer;
      const VkDeviceSize offset = buf->bo_offset + cmd->state.index_offset +
                                  index_bytes * draw->first_index;
      const uint32_t size = index_bytes * draw->count;

      const uint32_t cp_draw_indx =
         CP_DRAW_INDX_OFFSET_0_PRIM_TYPE(primtype) |
         CP_DRAW_INDX_OFFSET_0_SOURCE_SELECT(DI_SRC_SEL_DMA) |
         CP_DRAW_INDX_OFFSET_0_INDEX_SIZE(index_size) |
         CP_DRAW_INDX_OFFSET_0_VIS_CULL(USE_VISIBILITY) |
         COND(has_gs, CP_DRAW_INDX_OFFSET_0_GS_ENABLE) | 0x2000;

      tu_cs_emit_pkt7(cs, CP_DRAW_INDX_OFFSET, 7);
      tu_cs_emit(cs, cp_draw_indx);
      tu_cs_emit(cs, draw->instance_count);
      tu_cs_emit(cs, draw->count);
      tu_cs_emit(cs, 0x0); /* XXX */
      tu_cs_emit_qw(cs, buf->bo->iova + offset);
      tu_cs_emit(cs, size);
   } else {
      const uint32_t cp_draw_indx =
         CP_DRAW_INDX_OFFSET_0_PRIM_TYPE(primtype) |
         CP_DRAW_INDX_OFFSET_0_SOURCE_SELECT(DI_SRC_SEL_AUTO_INDEX) |
         CP_DRAW_INDX_OFFSET_0_VIS_CULL(USE_VISIBILITY) |
         COND(has_gs, CP_DRAW_INDX_OFFSET_0_GS_ENABLE) | 0x2000;

      tu_cs_emit_pkt7(cs, CP_DRAW_INDX_OFFSET, 3);
      tu_cs_emit(cs, cp_draw_indx);
      tu_cs_emit(cs, draw->instance_count);
      tu_cs_emit(cs, draw->count);
   }
}

static void
tu_draw(struct tu_cmd_buffer *cmd, const struct tu_draw_info *draw)
{
   struct tu_cs *cs = &cmd->draw_cs;
   VkResult result;

   result = tu6_bind_draw_states(cmd, cs, draw);
   if (result != VK_SUCCESS) {
      cmd->record_result = result;
      return;
   }

   if (draw->indirect)
      tu6_emit_draw_indirect(cmd, cs, draw);
   else
      tu6_emit_draw_direct(cmd, cs, draw);

   if (cmd->state.streamout_enabled) {
      for (unsigned i = 0; i < IR3_MAX_SO_BUFFERS; i++) {
         if (cmd->state.streamout_enabled & (1 << i))
            tu6_emit_event_write(cmd, cs, FLUSH_SO_0 + i, false);
      }
   }

   cmd->wait_for_idle = true;

   tu_cs_sanity_check(cs);
}

void
tu_CmdDraw(VkCommandBuffer commandBuffer,
           uint32_t vertexCount,
           uint32_t instanceCount,
           uint32_t firstVertex,
           uint32_t firstInstance)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   struct tu_draw_info info = {};

   info.count = vertexCount;
   info.instance_count = instanceCount;
   info.first_instance = firstInstance;
   info.vertex_offset = firstVertex;

   tu_draw(cmd_buffer, &info);
}

void
tu_CmdDrawIndexed(VkCommandBuffer commandBuffer,
                  uint32_t indexCount,
                  uint32_t instanceCount,
                  uint32_t firstIndex,
                  int32_t vertexOffset,
                  uint32_t firstInstance)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   struct tu_draw_info info = {};

   info.indexed = true;
   info.count = indexCount;
   info.instance_count = instanceCount;
   info.first_index = firstIndex;
   info.vertex_offset = vertexOffset;
   info.first_instance = firstInstance;

   tu_draw(cmd_buffer, &info);
}

void
tu_CmdDrawIndirect(VkCommandBuffer commandBuffer,
                   VkBuffer _buffer,
                   VkDeviceSize offset,
                   uint32_t drawCount,
                   uint32_t stride)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, buffer, _buffer);
   struct tu_draw_info info = {};

   info.count = drawCount;
   info.indirect = buffer;
   info.indirect_offset = offset;
   info.stride = stride;

   tu_draw(cmd_buffer, &info);
}

void
tu_CmdDrawIndexedIndirect(VkCommandBuffer commandBuffer,
                          VkBuffer _buffer,
                          VkDeviceSize offset,
                          uint32_t drawCount,
                          uint32_t stride)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, buffer, _buffer);
   struct tu_draw_info info = {};

   info.indexed = true;
   info.count = drawCount;
   info.indirect = buffer;
   info.indirect_offset = offset;
   info.stride = stride;

   tu_draw(cmd_buffer, &info);
}

void tu_CmdDrawIndirectByteCountEXT(VkCommandBuffer commandBuffer,
                                    uint32_t instanceCount,
                                    uint32_t firstInstance,
                                    VkBuffer _counterBuffer,
                                    VkDeviceSize counterBufferOffset,
                                    uint32_t counterOffset,
                                    uint32_t vertexStride)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, buffer, _counterBuffer);

   struct tu_draw_info info = {};

   info.instance_count = instanceCount;
   info.first_instance = firstInstance;
   info.streamout_buffer = buffer;
   info.streamout_buffer_offset = counterBufferOffset;
   info.stride = vertexStride;

   tu_draw(cmd_buffer, &info);
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
   VkResult result;

   if (cmd->state.dirty & TU_CMD_DIRTY_COMPUTE_PIPELINE)
      tu_cs_emit_ib(cs, &pipeline->program.state_ib);

   struct tu_cs_entry ib;

   ib = tu6_emit_consts(cmd, pipeline, descriptors_state, MESA_SHADER_COMPUTE);
   if (ib.size)
      tu_cs_emit_ib(cs, &ib);

   tu_emit_compute_driver_params(cs, pipeline, info);

   if (cmd->state.dirty & TU_CMD_DIRTY_COMPUTE_DESCRIPTOR_SETS) {
      result = tu6_emit_descriptor_sets(cmd, pipeline,
                                        VK_PIPELINE_BIND_POINT_COMPUTE, &ib,
                                        false);
      if (result != VK_SUCCESS) {
         cmd->record_result = result;
         return;
      }

      /* track BOs */
      unsigned i;
      for_each_bit(i, descriptors_state->valid) {
         struct tu_descriptor_set *set = descriptors_state->sets[i];
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
   }

   if (ib.size)
      tu_cs_emit_ib(cs, &ib);

   if (cmd->state.dirty & TU_CMD_DIRTY_COMPUTE_DESCRIPTOR_SETS)
      tu_cs_emit_ib(cs, &pipeline->load_state.state_ib);

   cmd->state.dirty &=
      ~(TU_CMD_DIRTY_COMPUTE_DESCRIPTOR_SETS | TU_CMD_DIRTY_COMPUTE_PIPELINE);

   /* Compute shader state overwrites fragment shader state, so we flag the
    * graphics pipeline for re-emit.
    */
   cmd->state.dirty |= TU_CMD_DIRTY_PIPELINE;

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

   tu6_emit_cache_flush(cmd, cs);
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
   /* renderpass case is only for subpass self-dependencies
    * which means syncing the render output with texture cache
    * note: only the CACHE_INVALIDATE is needed in GMEM mode
    * and in sysmem mode we might not need either color/depth flush
    */
   if (cmd->state.pass) {
      tu6_emit_event_write(cmd, &cmd->draw_cs, PC_CCU_FLUSH_COLOR_TS, true);
      tu6_emit_event_write(cmd, &cmd->draw_cs, PC_CCU_FLUSH_DEPTH_TS, true);
      tu6_emit_event_write(cmd, &cmd->draw_cs, CACHE_INVALIDATE, false);
      return;
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
write_event(struct tu_cmd_buffer *cmd, struct tu_event *event, unsigned value)
{
   struct tu_cs *cs = &cmd->cs;

   tu_bo_list_add(&cmd->bo_list, &event->bo, MSM_SUBMIT_BO_WRITE);

   /* TODO: any flush required before/after ? */

   tu_cs_emit_pkt7(cs, CP_MEM_WRITE, 3);
   tu_cs_emit_qw(cs, event->bo.iova); /* ADDR_LO/HI */
   tu_cs_emit(cs, value);
}

void
tu_CmdSetEvent(VkCommandBuffer commandBuffer,
               VkEvent _event,
               VkPipelineStageFlags stageMask)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_event, event, _event);

   write_event(cmd, event, 1);
}

void
tu_CmdResetEvent(VkCommandBuffer commandBuffer,
                 VkEvent _event,
                 VkPipelineStageFlags stageMask)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   TU_FROM_HANDLE(tu_event, event, _event);

   write_event(cmd, event, 0);
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
   struct tu_cs *cs = &cmd->cs;

   /* TODO: any flush required before/after? (CP_WAIT_FOR_ME?) */

   for (uint32_t i = 0; i < eventCount; i++) {
      TU_FROM_HANDLE(tu_event, event, pEvents[i]);

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
tu_CmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask)
{
   /* No-op */
}
