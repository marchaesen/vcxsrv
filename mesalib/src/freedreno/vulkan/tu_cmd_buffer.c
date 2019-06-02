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
#include "registers/a6xx.xml.h"

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

static VkResult
tu_tiling_config_update_gmem_layout(struct tu_tiling_config *tiling,
                                    const struct tu_device *dev)
{
   const uint32_t gmem_size = dev->physical_device->gmem_size;
   uint32_t offset = 0;

   for (uint32_t i = 0; i < tiling->buffer_count; i++) {
      /* 16KB-aligned */
      offset = align(offset, 0x4000);

      tiling->gmem_offsets[i] = offset;
      offset += tiling->tile0.extent.width * tiling->tile0.extent.height *
                tiling->buffer_cpp[i];
   }

   return offset <= gmem_size ? VK_SUCCESS : VK_ERROR_OUT_OF_DEVICE_MEMORY;
}

static void
tu_tiling_config_update_tile_layout(struct tu_tiling_config *tiling,
                                    const struct tu_device *dev)
{
   const uint32_t tile_align_w = dev->physical_device->tile_align_w;
   const uint32_t tile_align_h = dev->physical_device->tile_align_h;
   const uint32_t max_tile_width = 1024; /* A6xx */

   tiling->tile0.offset = (VkOffset2D) {
      .x = tiling->render_area.offset.x & ~(tile_align_w - 1),
      .y = tiling->render_area.offset.y & ~(tile_align_h - 1),
   };

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
      .width = align(ra_width, tile_align_w),
      .height = align(ra_height, tile_align_h),
   };

   /* do not exceed max tile width */
   while (tiling->tile0.extent.width > max_tile_width) {
      tiling->tile_count.width++;
      tiling->tile0.extent.width =
         align(ra_width / tiling->tile_count.width, tile_align_w);
   }

   /* do not exceed gmem size */
   while (tu_tiling_config_update_gmem_layout(tiling, dev) != VK_SUCCESS) {
      if (tiling->tile0.extent.width > tiling->tile0.extent.height) {
         tiling->tile_count.width++;
         tiling->tile0.extent.width =
            align(ra_width / tiling->tile_count.width, tile_align_w);
      } else {
         tiling->tile_count.height++;
         tiling->tile0.extent.height =
            align(ra_height / tiling->tile_count.height, tile_align_h);
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

   /* do not exceed max pipe count vertically */
   while (tiling->pipe_count.height > max_pipe_count) {
      tiling->pipe0.height += 2;
      tiling->pipe_count.height =
         (tiling->tile_count.height + tiling->pipe0.height - 1) /
         tiling->pipe0.height;
   }

   /* do not exceed max pipe count */
   while (tiling->pipe_count.width * tiling->pipe_count.height >
          max_pipe_count) {
      tiling->pipe0.width += 1;
      tiling->pipe_count.width =
         (tiling->tile_count.width + tiling->pipe0.width - 1) /
         tiling->pipe0.width;
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
      .width = tiling->tile_count.width % tiling->pipe0.width,
      .height = tiling->tile_count.height % tiling->pipe0.height,
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
tu_tiling_config_update(struct tu_tiling_config *tiling,
                        const struct tu_device *dev,
                        const uint32_t *buffer_cpp,
                        uint32_t buffer_count,
                        const VkRect2D *render_area)
{
   /* see if there is any real change */
   const bool ra_changed =
      render_area &&
      memcmp(&tiling->render_area, render_area, sizeof(*render_area));
   const bool buf_changed = tiling->buffer_count != buffer_count ||
                            memcmp(tiling->buffer_cpp, buffer_cpp,
                                   sizeof(*buffer_cpp) * buffer_count);
   if (!ra_changed && !buf_changed)
      return;

   if (ra_changed)
      tiling->render_area = *render_area;

   if (buf_changed) {
      memcpy(tiling->buffer_cpp, buffer_cpp,
             sizeof(*buffer_cpp) * buffer_count);
      tiling->buffer_count = buffer_count;
   }

   tu_tiling_config_update_tile_layout(tiling, dev);
   tu_tiling_config_update_pipe_layout(tiling, dev);
   tu_tiling_config_update_pipes(tiling, dev);
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

   assert(tx < tiling->tile_count.width && ty < tiling->tile_count.height);
   assert(px < tiling->pipe_count.width && py < tiling->pipe_count.height);
   assert(sx < tiling->pipe0.width && sy < tiling->pipe0.height);

   /* convert to 1D indices */
   tile->pipe = tiling->pipe_count.width * py + px;
   tile->slot = tiling->pipe0.width * sy + sx;

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

static enum a3xx_msaa_samples
tu6_msaa_samples(uint32_t samples)
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

static void
tu6_emit_marker(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   tu_cs_emit_write_reg(cs, cmd->marker_reg, ++cmd->marker_seqno);
}

void
tu6_emit_event_write(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     enum vgt_event_type event,
                     bool need_seqno)
{
   tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, need_seqno ? 4 : 1);
   tu_cs_emit(cs, CP_EVENT_WRITE_0_EVENT(event));
   if (need_seqno) {
      tu_cs_emit_qw(cs, cmd->scratch_bo.iova);
      tu_cs_emit(cs, ++cmd->scratch_seqno);
   }
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
tu6_emit_zs(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   const struct tu_subpass *subpass = cmd->state.subpass;

   const uint32_t a = subpass->depth_stencil_attachment.attachment;
   if (a == VK_ATTACHMENT_UNUSED) {
      tu_cs_emit_pkt4(cs, REG_A6XX_RB_DEPTH_BUFFER_INFO, 6);
      tu_cs_emit(cs, A6XX_RB_DEPTH_BUFFER_INFO_DEPTH_FORMAT(DEPTH6_NONE));
      tu_cs_emit(cs, 0x00000000); /* RB_DEPTH_BUFFER_PITCH */
      tu_cs_emit(cs, 0x00000000); /* RB_DEPTH_BUFFER_ARRAY_PITCH */
      tu_cs_emit(cs, 0x00000000); /* RB_DEPTH_BUFFER_BASE_LO */
      tu_cs_emit(cs, 0x00000000); /* RB_DEPTH_BUFFER_BASE_HI */
      tu_cs_emit(cs, 0x00000000); /* RB_DEPTH_BUFFER_BASE_GMEM */

      tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SU_DEPTH_BUFFER_INFO, 1);
      tu_cs_emit(cs,
                 A6XX_GRAS_SU_DEPTH_BUFFER_INFO_DEPTH_FORMAT(DEPTH6_NONE));

      tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_LRZ_BUFFER_BASE_LO, 5);
      tu_cs_emit(cs, 0x00000000); /* RB_DEPTH_FLAG_BUFFER_BASE_LO */
      tu_cs_emit(cs, 0x00000000); /* RB_DEPTH_FLAG_BUFFER_BASE_HI */
      tu_cs_emit(cs, 0x00000000); /* GRAS_LRZ_BUFFER_PITCH */
      tu_cs_emit(cs, 0x00000000); /* GRAS_LRZ_FAST_CLEAR_BUFFER_BASE_LO */
      tu_cs_emit(cs, 0x00000000); /* GRAS_LRZ_FAST_CLEAR_BUFFER_BASE_HI */

      tu_cs_emit_pkt4(cs, REG_A6XX_RB_STENCIL_INFO, 1);
      tu_cs_emit(cs, 0x00000000); /* RB_STENCIL_INFO */

      return;
   }

   /* enable zs? */
}

static void
tu6_emit_mrt(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   const struct tu_subpass *subpass = cmd->state.subpass;
   const struct tu_tiling_config *tiling = &cmd->state.tiling_config;
   unsigned char mrt_comp[MAX_RTS] = { 0 };
   unsigned srgb_cntl = 0;

   uint32_t gmem_index = 0;
   for (uint32_t i = 0; i < subpass->color_count; ++i) {
      uint32_t a = subpass->color_attachments[i].attachment;
      if (a == VK_ATTACHMENT_UNUSED)
         continue;

      const struct tu_image_view *iview = fb->attachments[a].attachment;
      const struct tu_image_level *slice =
         &iview->image->levels[iview->base_mip];
      const enum a6xx_tile_mode tile_mode = TILE6_LINEAR;
      uint32_t stride = 0;
      uint32_t offset = 0;

      mrt_comp[i] = 0xf;

      if (vk_format_is_srgb(iview->vk_format))
         srgb_cntl |= (1 << i);

      const struct tu_native_format *format =
         tu6_get_native_format(iview->vk_format);
      assert(format && format->rb >= 0);

      offset = slice->offset + slice->size * iview->base_layer;
      stride = slice->pitch * vk_format_get_blocksize(iview->vk_format);

      tu_cs_emit_pkt4(cs, REG_A6XX_RB_MRT_BUF_INFO(i), 6);
      tu_cs_emit(cs, A6XX_RB_MRT_BUF_INFO_COLOR_FORMAT(format->rb) |
                        A6XX_RB_MRT_BUF_INFO_COLOR_TILE_MODE(tile_mode) |
                        A6XX_RB_MRT_BUF_INFO_COLOR_SWAP(format->swap));
      tu_cs_emit(cs, A6XX_RB_MRT_PITCH(stride));
      tu_cs_emit(cs, A6XX_RB_MRT_ARRAY_PITCH(slice->size));
      tu_cs_emit_qw(cs, iview->image->bo->iova + iview->image->bo_offset +
                           offset); /* BASE_LO/HI */
      tu_cs_emit(
         cs, tiling->gmem_offsets[gmem_index++]); /* RB_MRT[i].BASE_GMEM */

      tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_MRT_REG(i), 1);
      tu_cs_emit(cs, A6XX_SP_FS_MRT_REG_COLOR_FORMAT(format->rb));

#if 0
      /* when we support UBWC, these would be the system memory
       * addr/pitch/etc:
       */
      tu_cs_emit_pkt4(cs, REG_A6XX_RB_MRT_FLAG_BUFFER(i), 4);
      tu_cs_emit(cs, 0x00000000);    /* RB_MRT_FLAG_BUFFER[i].ADDR_LO */
      tu_cs_emit(cs, 0x00000000);    /* RB_MRT_FLAG_BUFFER[i].ADDR_HI */
      tu_cs_emit(cs, A6XX_RB_MRT_FLAG_BUFFER_PITCH(0));
      tu_cs_emit(cs, A6XX_RB_MRT_FLAG_BUFFER_ARRAY_PITCH(0));
#endif
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_SRGB_CNTL, 1);
   tu_cs_emit(cs, srgb_cntl);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_SRGB_CNTL, 1);
   tu_cs_emit(cs, srgb_cntl);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_RENDER_COMPONENTS, 1);
   tu_cs_emit(cs, A6XX_RB_RENDER_COMPONENTS_RT0(mrt_comp[0]) |
                     A6XX_RB_RENDER_COMPONENTS_RT1(mrt_comp[1]) |
                     A6XX_RB_RENDER_COMPONENTS_RT2(mrt_comp[2]) |
                     A6XX_RB_RENDER_COMPONENTS_RT3(mrt_comp[3]) |
                     A6XX_RB_RENDER_COMPONENTS_RT4(mrt_comp[4]) |
                     A6XX_RB_RENDER_COMPONENTS_RT5(mrt_comp[5]) |
                     A6XX_RB_RENDER_COMPONENTS_RT6(mrt_comp[6]) |
                     A6XX_RB_RENDER_COMPONENTS_RT7(mrt_comp[7]));

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_FS_RENDER_COMPONENTS, 1);
   tu_cs_emit(cs, A6XX_SP_FS_RENDER_COMPONENTS_RT0(mrt_comp[0]) |
                     A6XX_SP_FS_RENDER_COMPONENTS_RT1(mrt_comp[1]) |
                     A6XX_SP_FS_RENDER_COMPONENTS_RT2(mrt_comp[2]) |
                     A6XX_SP_FS_RENDER_COMPONENTS_RT3(mrt_comp[3]) |
                     A6XX_SP_FS_RENDER_COMPONENTS_RT4(mrt_comp[4]) |
                     A6XX_SP_FS_RENDER_COMPONENTS_RT5(mrt_comp[5]) |
                     A6XX_SP_FS_RENDER_COMPONENTS_RT6(mrt_comp[6]) |
                     A6XX_SP_FS_RENDER_COMPONENTS_RT7(mrt_comp[7]));
}

static void
tu6_emit_msaa(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   const struct tu_subpass *subpass = cmd->state.subpass;
   const enum a3xx_msaa_samples samples =
      tu6_msaa_samples(subpass->max_sample_count);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_TP_RAS_MSAA_CNTL, 2);
   tu_cs_emit(cs, A6XX_SP_TP_RAS_MSAA_CNTL_SAMPLES(samples));
   tu_cs_emit(
      cs, A6XX_SP_TP_DEST_MSAA_CNTL_SAMPLES(samples) |
             ((samples == MSAA_ONE) ? A6XX_SP_TP_DEST_MSAA_CNTL_MSAA_DISABLE
                                    : 0));

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_RAS_MSAA_CNTL, 2);
   tu_cs_emit(cs, A6XX_GRAS_RAS_MSAA_CNTL_SAMPLES(samples));
   tu_cs_emit(
      cs,
      A6XX_GRAS_DEST_MSAA_CNTL_SAMPLES(samples) |
         ((samples == MSAA_ONE) ? A6XX_GRAS_DEST_MSAA_CNTL_MSAA_DISABLE : 0));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_RAS_MSAA_CNTL, 2);
   tu_cs_emit(cs, A6XX_RB_RAS_MSAA_CNTL_SAMPLES(samples));
   tu_cs_emit(
      cs,
      A6XX_RB_DEST_MSAA_CNTL_SAMPLES(samples) |
         ((samples == MSAA_ONE) ? A6XX_RB_DEST_MSAA_CNTL_MSAA_DISABLE : 0));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_MSAA_CNTL, 1);
   tu_cs_emit(cs, A6XX_RB_MSAA_CNTL_SAMPLES(samples));
}

static void
tu6_emit_bin_size(struct tu_cmd_buffer *cmd, struct tu_cs *cs, uint32_t flags)
{
   const struct tu_tiling_config *tiling = &cmd->state.tiling_config;
   const uint32_t bin_w = tiling->tile0.extent.width;
   const uint32_t bin_h = tiling->tile0.extent.height;

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_BIN_CONTROL, 1);
   tu_cs_emit(cs, A6XX_GRAS_BIN_CONTROL_BINW(bin_w) |
                     A6XX_GRAS_BIN_CONTROL_BINH(bin_h) | flags);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BIN_CONTROL, 1);
   tu_cs_emit(cs, A6XX_RB_BIN_CONTROL_BINW(bin_w) |
                     A6XX_RB_BIN_CONTROL_BINH(bin_h) | flags);

   /* no flag for RB_BIN_CONTROL2... */
   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BIN_CONTROL2, 1);
   tu_cs_emit(cs, A6XX_RB_BIN_CONTROL2_BINW(bin_w) |
                     A6XX_RB_BIN_CONTROL2_BINH(bin_h));
}

static void
tu6_emit_render_cntl(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     bool binning)
{
   uint32_t cntl = 0;
   cntl |= A6XX_RB_RENDER_CNTL_UNK4;
   if (binning)
      cntl |= A6XX_RB_RENDER_CNTL_BINNING;

   tu_cs_emit_pkt7(cs, CP_REG_WRITE, 3);
   tu_cs_emit(cs, 0x2);
   tu_cs_emit(cs, REG_A6XX_RB_RENDER_CNTL);
   tu_cs_emit(cs, cntl);
}

static void
tu6_emit_blit_scissor(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   const VkRect2D *render_area = &cmd->state.tiling_config.render_area;
   const uint32_t x1 = render_area->offset.x;
   const uint32_t y1 = render_area->offset.y;
   const uint32_t x2 = x1 + render_area->extent.width - 1;
   const uint32_t y2 = y1 + render_area->extent.height - 1;

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_SCISSOR_TL, 2);
   tu_cs_emit(cs,
              A6XX_RB_BLIT_SCISSOR_TL_X(x1) | A6XX_RB_BLIT_SCISSOR_TL_Y(y1));
   tu_cs_emit(cs,
              A6XX_RB_BLIT_SCISSOR_BR_X(x2) | A6XX_RB_BLIT_SCISSOR_BR_Y(y2));
}

static void
tu6_emit_blit_info(struct tu_cmd_buffer *cmd,
                   struct tu_cs *cs,
                   const struct tu_image_view *iview,
                   uint32_t gmem_offset,
                   uint32_t blit_info)
{
   const struct tu_image_level *slice =
      &iview->image->levels[iview->base_mip];
   const uint32_t offset = slice->offset + slice->size * iview->base_layer;
   const uint32_t stride =
      slice->pitch * vk_format_get_blocksize(iview->vk_format);
   const enum a6xx_tile_mode tile_mode = TILE6_LINEAR;
   const enum a3xx_msaa_samples samples = tu6_msaa_samples(1);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_INFO, 1);
   tu_cs_emit(cs, blit_info);

   /* tile mode? */
   const struct tu_native_format *format =
      tu6_get_native_format(iview->vk_format);
   assert(format && format->rb >= 0);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_DST_INFO, 5);
   tu_cs_emit(cs, A6XX_RB_BLIT_DST_INFO_TILE_MODE(tile_mode) |
                     A6XX_RB_BLIT_DST_INFO_SAMPLES(samples) |
                     A6XX_RB_BLIT_DST_INFO_COLOR_FORMAT(format->rb) |
                     A6XX_RB_BLIT_DST_INFO_COLOR_SWAP(format->swap));
   tu_cs_emit_qw(cs,
                 iview->image->bo->iova + iview->image->bo_offset + offset);
   tu_cs_emit(cs, A6XX_RB_BLIT_DST_PITCH(stride));
   tu_cs_emit(cs, A6XX_RB_BLIT_DST_ARRAY_PITCH(slice->size));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_BASE_GMEM, 1);
   tu_cs_emit(cs, gmem_offset);
}

static void
tu6_emit_blit_clear(struct tu_cmd_buffer *cmd,
                    struct tu_cs *cs,
                    const struct tu_image_view *iview,
                    uint32_t gmem_offset,
                    const VkClearValue *clear_value)
{
   const enum a6xx_tile_mode tile_mode = TILE6_LINEAR;
   const enum a3xx_msaa_samples samples = tu6_msaa_samples(1);

   const struct tu_native_format *format =
      tu6_get_native_format(iview->vk_format);
   assert(format && format->rb >= 0);
   /* must be WZYX; other values are ignored */
   const enum a3xx_color_swap swap = WZYX;

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_DST_INFO, 1);
   tu_cs_emit(cs, A6XX_RB_BLIT_DST_INFO_TILE_MODE(tile_mode) |
                     A6XX_RB_BLIT_DST_INFO_SAMPLES(samples) |
                     A6XX_RB_BLIT_DST_INFO_COLOR_FORMAT(format->rb) |
                     A6XX_RB_BLIT_DST_INFO_COLOR_SWAP(swap));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_INFO, 1);
   tu_cs_emit(cs, A6XX_RB_BLIT_INFO_GMEM | A6XX_RB_BLIT_INFO_CLEAR_MASK(0xf));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_BASE_GMEM, 1);
   tu_cs_emit(cs, gmem_offset);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_UNKNOWN_88D0, 1);
   tu_cs_emit(cs, 0);

   /* pack clear_value into WZYX order */
   uint32_t clear_vals[4] = { 0 };
   tu_pack_clear_value(clear_value, iview->vk_format, clear_vals);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_BLIT_CLEAR_COLOR_DW0, 4);
   tu_cs_emit(cs, clear_vals[0]);
   tu_cs_emit(cs, clear_vals[1]);
   tu_cs_emit(cs, clear_vals[2]);
   tu_cs_emit(cs, clear_vals[3]);
}

static void
tu6_emit_blit(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   tu6_emit_marker(cmd, cs);
   tu6_emit_event_write(cmd, cs, BLIT, false);
   tu6_emit_marker(cmd, cs);
}

static void
tu6_emit_window_scissor(struct tu_cmd_buffer *cmd,
                        struct tu_cs *cs,
                        uint32_t x1,
                        uint32_t y1,
                        uint32_t x2,
                        uint32_t y2)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_SC_WINDOW_SCISSOR_TL, 2);
   tu_cs_emit(cs, A6XX_GRAS_SC_WINDOW_SCISSOR_TL_X(x1) |
                     A6XX_GRAS_SC_WINDOW_SCISSOR_TL_Y(y1));
   tu_cs_emit(cs, A6XX_GRAS_SC_WINDOW_SCISSOR_BR_X(x2) |
                     A6XX_GRAS_SC_WINDOW_SCISSOR_BR_Y(y2));

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_RESOLVE_CNTL_1, 2);
   tu_cs_emit(
      cs, A6XX_GRAS_RESOLVE_CNTL_1_X(x1) | A6XX_GRAS_RESOLVE_CNTL_1_Y(y1));
   tu_cs_emit(
      cs, A6XX_GRAS_RESOLVE_CNTL_2_X(x2) | A6XX_GRAS_RESOLVE_CNTL_2_Y(y2));
}

static void
tu6_emit_window_offset(struct tu_cmd_buffer *cmd,
                       struct tu_cs *cs,
                       uint32_t x1,
                       uint32_t y1)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_RB_WINDOW_OFFSET, 1);
   tu_cs_emit(cs, A6XX_RB_WINDOW_OFFSET_X(x1) | A6XX_RB_WINDOW_OFFSET_Y(y1));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_WINDOW_OFFSET2, 1);
   tu_cs_emit(cs,
              A6XX_RB_WINDOW_OFFSET2_X(x1) | A6XX_RB_WINDOW_OFFSET2_Y(y1));

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_WINDOW_OFFSET, 1);
   tu_cs_emit(cs, A6XX_SP_WINDOW_OFFSET_X(x1) | A6XX_SP_WINDOW_OFFSET_Y(y1));

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_TP_WINDOW_OFFSET, 1);
   tu_cs_emit(
      cs, A6XX_SP_TP_WINDOW_OFFSET_X(x1) | A6XX_SP_TP_WINDOW_OFFSET_Y(y1));
}

static void
tu6_emit_tile_select(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     const struct tu_tile *tile)
{
   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A2XX_CP_SET_MARKER_0_MODE(0x7));

   tu6_emit_marker(cmd, cs);
   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A2XX_CP_SET_MARKER_0_MODE(RM6_GMEM) | 0x10);
   tu6_emit_marker(cmd, cs);

   const uint32_t x1 = tile->begin.x;
   const uint32_t y1 = tile->begin.y;
   const uint32_t x2 = tile->end.x - 1;
   const uint32_t y2 = tile->end.y - 1;
   tu6_emit_window_scissor(cmd, cs, x1, y1, x2, y2);
   tu6_emit_window_offset(cmd, cs, x1, y1);

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_SO_OVERRIDE, 1);
   tu_cs_emit(cs, A6XX_VPC_SO_OVERRIDE_SO_DISABLE);

   if (false) {
      /* hw binning? */
   } else {
      tu_cs_emit_pkt7(cs, CP_SET_VISIBILITY_OVERRIDE, 1);
      tu_cs_emit(cs, 0x1);

      tu_cs_emit_pkt7(cs, CP_SET_MODE, 1);
      tu_cs_emit(cs, 0x0);
   }
}

static void
tu6_emit_tile_load(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   const struct tu_subpass *subpass = cmd->state.subpass;
   const struct tu_tiling_config *tiling = &cmd->state.tiling_config;
   const struct tu_attachment_state *attachments = cmd->state.attachments;

   tu6_emit_blit_scissor(cmd, cs);

   uint32_t gmem_index = 0;
   for (uint32_t i = 0; i < subpass->color_count; ++i) {
      const uint32_t a = subpass->color_attachments[i].attachment;
      if (a == VK_ATTACHMENT_UNUSED)
         continue;

      const struct tu_image_view *iview = fb->attachments[a].attachment;
      const struct tu_attachment_state *att = attachments + a;
      if (att->pending_clear_aspects) {
         assert(att->pending_clear_aspects == VK_IMAGE_ASPECT_COLOR_BIT);
         tu6_emit_blit_clear(cmd, cs, iview,
                             tiling->gmem_offsets[gmem_index++],
                             &att->clear_value);
      } else {
         tu6_emit_blit_info(cmd, cs, iview,
                            tiling->gmem_offsets[gmem_index++],
                            A6XX_RB_BLIT_INFO_UNK0 | A6XX_RB_BLIT_INFO_GMEM);
      }

      tu6_emit_blit(cmd, cs);
   }

   /* load/clear zs? */
}

static void
tu6_emit_tile_store(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   const struct tu_framebuffer *fb = cmd->state.framebuffer;
   const struct tu_tiling_config *tiling = &cmd->state.tiling_config;

   if (false) {
      /* hw binning? */
   }

   tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3);
   tu_cs_emit(cs, CP_SET_DRAW_STATE__0_COUNT(0) |
                     CP_SET_DRAW_STATE__0_DISABLE_ALL_GROUPS |
                     CP_SET_DRAW_STATE__0_GROUP_ID(0));
   tu_cs_emit(cs, CP_SET_DRAW_STATE__1_ADDR_LO(0));
   tu_cs_emit(cs, CP_SET_DRAW_STATE__2_ADDR_HI(0));

   tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0x0);

   tu6_emit_marker(cmd, cs);
   tu_cs_emit_pkt7(cs, CP_SET_MARKER, 1);
   tu_cs_emit(cs, A2XX_CP_SET_MARKER_0_MODE(RM6_RESOLVE) | 0x10);
   tu6_emit_marker(cmd, cs);

   tu6_emit_blit_scissor(cmd, cs);

   uint32_t gmem_index = 0;
   for (uint32_t i = 0; i < cmd->state.subpass->color_count; ++i) {
      uint32_t a = cmd->state.subpass->color_attachments[i].attachment;
      if (a == VK_ATTACHMENT_UNUSED)
         continue;

      const struct tu_image_view *iview = fb->attachments[a].attachment;
      tu6_emit_blit_info(cmd, cs, iview, tiling->gmem_offsets[gmem_index++],
                         0);
      tu6_emit_blit(cmd, cs);
   }
}

static void
tu6_emit_restart_index(struct tu_cs *cs, uint32_t restart_index)
{
   tu_cs_emit_pkt4(cs, REG_A6XX_PC_RESTART_INDEX, 1);
   tu_cs_emit(cs, restart_index);
}

static void
tu6_init_hw(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   VkResult result = tu_cs_reserve_space(cmd->device, cs, 256);
   if (result != VK_SUCCESS) {
      cmd->record_result = result;
      return;
   }

   tu6_emit_cache_flush(cmd, cs);

   tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_UPDATE_CNTL, 0xfffff);

   tu_cs_emit_write_reg(cs, REG_A6XX_RB_CCU_CNTL, 0x7c400004);
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
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_UNKNOWN_AB00, 0x5);
   tu_cs_emit_write_reg(cs, REG_A6XX_VFD_UNKNOWN_A009, 0x00000001);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8811, 0x00000010);
   tu_cs_emit_write_reg(cs, REG_A6XX_PC_MODE_CNTL, 0x1f);

   tu_cs_emit_write_reg(cs, REG_A6XX_RB_SRGB_CNTL, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_UNKNOWN_8101, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_SAMPLE_CNTL, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_UNKNOWN_8110, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_RB_RENDER_CONTROL0, 0x401);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_RENDER_CONTROL1, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_FS_OUTPUT_CNTL0, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_SAMPLE_CNTL, 0);
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

   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9236, 1);
   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_UNKNOWN_9300, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_VPC_SO_OVERRIDE,
                        A6XX_VPC_SO_OVERRIDE_SO_DISABLE);

   tu_cs_emit_write_reg(cs, REG_A6XX_PC_UNKNOWN_9801, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_PC_UNKNOWN_9806, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_PC_UNKNOWN_9980, 0);

   tu_cs_emit_write_reg(cs, REG_A6XX_PC_UNKNOWN_9B06, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_PC_UNKNOWN_9B06, 0);

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
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_TP_UNKNOWN_B304, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_SP_TP_UNKNOWN_B309, 0x000000a2);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8804, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_UNKNOWN_80A4, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_UNKNOWN_80A5, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_GRAS_UNKNOWN_80A6, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8805, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8806, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8878, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_RB_UNKNOWN_8879, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_HLSQ_CONTROL_5_REG, 0xfc);

   tu6_emit_marker(cmd, cs);

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

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_SO_BUFFER_BASE_LO(0), 3);
   tu_cs_emit(cs, 0x00000000); /* VPC_SO_BUFFER_BASE_LO_0 */
   tu_cs_emit(cs, 0x00000000); /* VPC_SO_BUFFER_BASE_HI_0 */
   tu_cs_emit(cs, 0x00000000); /* VPC_SO_BUFFER_SIZE_0 */

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_SO_FLUSH_BASE_LO(0), 2);
   tu_cs_emit(cs, 0x00000000); /* VPC_SO_FLUSH_BASE_LO_0 */
   tu_cs_emit(cs, 0x00000000); /* VPC_SO_FLUSH_BASE_HI_0 */

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_SO_BUF_CNTL, 1);
   tu_cs_emit(cs, 0x00000000); /* VPC_SO_BUF_CNTL */

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_SO_BUFFER_OFFSET(0), 1);
   tu_cs_emit(cs, 0x00000000); /* UNKNOWN_E2AB */

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_SO_BUFFER_BASE_LO(1), 3);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_SO_BUFFER_OFFSET(1), 6);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_SO_BUFFER_OFFSET(2), 6);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);

   tu_cs_emit_pkt4(cs, REG_A6XX_VPC_SO_BUFFER_OFFSET(3), 3);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_HS_CTRL_REG0, 1);
   tu_cs_emit(cs, 0x00000000);

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_GS_CTRL_REG0, 1);
   tu_cs_emit(cs, 0x00000000);

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_LRZ_CNTL, 1);
   tu_cs_emit(cs, 0x00000000);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_LRZ_CNTL, 1);
   tu_cs_emit(cs, 0x00000000);

   tu_cs_sanity_check(cs);
}

static void
tu6_render_begin(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   VkResult result = tu_cs_reserve_space(cmd->device, cs, 256);
   if (result != VK_SUCCESS) {
      cmd->record_result = result;
      return;
   }

   tu6_emit_lrz_flush(cmd, cs);

   /* lrz clear? */

   tu6_emit_cache_flush(cmd, cs);

   tu_cs_emit_pkt7(cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   tu_cs_emit(cs, 0x0);

   /* 0x10000000 for BYPASS.. 0x7c13c080 for GMEM: */
   tu6_emit_wfi(cmd, cs);
   tu_cs_emit_pkt4(cs, REG_A6XX_RB_CCU_CNTL, 1);
   tu_cs_emit(cs, 0x7c400004); /* RB_CCU_CNTL */

   tu6_emit_zs(cmd, cs);
   tu6_emit_mrt(cmd, cs);
   tu6_emit_msaa(cmd, cs);

   if (false) {
      /* hw binning? */
   } else {
      tu6_emit_bin_size(cmd, cs, 0x6000000);
      /* no draws */
   }

   tu6_emit_render_cntl(cmd, cs, false);

   tu_cs_sanity_check(cs);
}

static void
tu6_render_tile(struct tu_cmd_buffer *cmd,
                struct tu_cs *cs,
                const struct tu_tile *tile)
{
   const uint32_t render_tile_space = 64 + tu_cs_get_call_size(&cmd->draw_cs);
   VkResult result = tu_cs_reserve_space(cmd->device, cs, render_tile_space);
   if (result != VK_SUCCESS) {
      cmd->record_result = result;
      return;
   }

   tu6_emit_tile_select(cmd, cs, tile);
   tu_cs_emit_ib(cs, &cmd->state.tile_load_ib);

   tu_cs_emit_call(cs, &cmd->draw_cs);
   cmd->wait_for_idle = true;

   tu_cs_emit_ib(cs, &cmd->state.tile_store_ib);

   tu_cs_sanity_check(cs);
}

static void
tu6_render_end(struct tu_cmd_buffer *cmd, struct tu_cs *cs)
{
   VkResult result = tu_cs_reserve_space(cmd->device, cs, 16);
   if (result != VK_SUCCESS) {
      cmd->record_result = result;
      return;
   }

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_LRZ_CNTL, 1);
   tu_cs_emit(cs, A6XX_GRAS_LRZ_CNTL_ENABLE | A6XX_GRAS_LRZ_CNTL_UNK3);

   tu6_emit_lrz_flush(cmd, cs);

   tu6_emit_event_write(cmd, cs, CACHE_FLUSH_TS, true);

   tu_cs_sanity_check(cs);
}

static void
tu_cmd_render_tiles(struct tu_cmd_buffer *cmd)
{
   const struct tu_tiling_config *tiling = &cmd->state.tiling_config;

   tu6_render_begin(cmd, &cmd->cs);

   for (uint32_t y = 0; y < tiling->tile_count.height; y++) {
      for (uint32_t x = 0; x < tiling->tile_count.width; x++) {
         struct tu_tile tile;
         tu_tiling_config_get_tile(tiling, cmd->device, x, y, &tile);
         tu6_render_tile(cmd, &cmd->cs, &tile);
      }
   }

   tu6_render_end(cmd, &cmd->cs);
}

static void
tu_cmd_prepare_tile_load_ib(struct tu_cmd_buffer *cmd)
{
   const uint32_t tile_load_space = 16 + 32 * MAX_RTS;
   const struct tu_subpass *subpass = cmd->state.subpass;
   struct tu_attachment_state *attachments = cmd->state.attachments;
   struct tu_cs sub_cs;

   VkResult result = tu_cs_begin_sub_stream(cmd->device, &cmd->tile_cs,
                                            tile_load_space, &sub_cs);
   if (result != VK_SUCCESS) {
      cmd->record_result = result;
      return;
   }

   /* emit to tile-load sub_cs */
   tu6_emit_tile_load(cmd, &sub_cs);

   cmd->state.tile_load_ib = tu_cs_end_sub_stream(&cmd->tile_cs, &sub_cs);

   for (uint32_t i = 0; i < subpass->color_count; ++i) {
      const uint32_t a = subpass->color_attachments[i].attachment;
      if (a != VK_ATTACHMENT_UNUSED)
         attachments[a].pending_clear_aspects = 0;
   }
}

static void
tu_cmd_prepare_tile_store_ib(struct tu_cmd_buffer *cmd)
{
   const uint32_t tile_store_space = 32 + 32 * MAX_RTS;
   struct tu_cs sub_cs;

   VkResult result = tu_cs_begin_sub_stream(cmd->device, &cmd->tile_cs,
                                            tile_store_space, &sub_cs);
   if (result != VK_SUCCESS) {
      cmd->record_result = result;
      return;
   }

   /* emit to tile-store sub_cs */
   tu6_emit_tile_store(cmd, &sub_cs);

   cmd->state.tile_store_ib = tu_cs_end_sub_stream(&cmd->tile_cs, &sub_cs);
}

static void
tu_cmd_update_tiling_config(struct tu_cmd_buffer *cmd,
                            const VkRect2D *render_area)
{
   const struct tu_device *dev = cmd->device;
   const struct tu_render_pass *pass = cmd->state.pass;
   const struct tu_subpass *subpass = cmd->state.subpass;
   struct tu_tiling_config *tiling = &cmd->state.tiling_config;

   uint32_t buffer_cpp[MAX_RTS + 2];
   uint32_t buffer_count = 0;

   for (uint32_t i = 0; i < subpass->color_count; ++i) {
      const uint32_t a = subpass->color_attachments[i].attachment;
      if (a == VK_ATTACHMENT_UNUSED)
         continue;

      const struct tu_render_pass_attachment *att = &pass->attachments[a];
      buffer_cpp[buffer_count++] =
         vk_format_get_blocksize(att->format) * att->samples;
   }

   if (subpass->depth_stencil_attachment.attachment != VK_ATTACHMENT_UNUSED) {
      const uint32_t a = subpass->depth_stencil_attachment.attachment;
      const struct tu_render_pass_attachment *att = &pass->attachments[a];

      /* TODO */
      assert(att->format != VK_FORMAT_D32_SFLOAT_S8_UINT);

      buffer_cpp[buffer_count++] =
         vk_format_get_blocksize(att->format) * att->samples;
   }

   tu_tiling_config_update(tiling, dev, buffer_cpp, buffer_count,
                           render_area);
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
   tu_cs_init(&cmd_buffer->cs, TU_CS_MODE_GROW, 4096);
   tu_cs_init(&cmd_buffer->draw_cs, TU_CS_MODE_GROW, 4096);
   tu_cs_init(&cmd_buffer->tile_cs, TU_CS_MODE_SUB_STREAM, 1024);

   *pCommandBuffer = tu_cmd_buffer_to_handle(cmd_buffer);

   list_inithead(&cmd_buffer->upload.list);

   cmd_buffer->marker_reg = REG_A6XX_CP_SCRATCH_REG(
      cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY ? 7 : 6);

   VkResult result = tu_bo_init_new(device, &cmd_buffer->scratch_bo, 0x1000);
   if (result != VK_SUCCESS)
      return result;

   return VK_SUCCESS;
}

static void
tu_cmd_buffer_destroy(struct tu_cmd_buffer *cmd_buffer)
{
   tu_bo_finish(cmd_buffer->device, &cmd_buffer->scratch_bo);

   list_del(&cmd_buffer->pool_link);

   for (unsigned i = 0; i < VK_PIPELINE_BIND_POINT_RANGE_SIZE; i++)
      free(cmd_buffer->descriptors[i].push_set.set.mapped_ptr);

   tu_cs_finish(cmd_buffer->device, &cmd_buffer->cs);
   tu_cs_finish(cmd_buffer->device, &cmd_buffer->draw_cs);
   tu_cs_finish(cmd_buffer->device, &cmd_buffer->tile_cs);

   tu_bo_list_destroy(&cmd_buffer->bo_list);
   vk_free(&cmd_buffer->pool->alloc, cmd_buffer);
}

static VkResult
tu_reset_cmd_buffer(struct tu_cmd_buffer *cmd_buffer)
{
   cmd_buffer->wait_for_idle = true;

   cmd_buffer->record_result = VK_SUCCESS;

   tu_bo_list_reset(&cmd_buffer->bo_list);
   tu_cs_reset(cmd_buffer->device, &cmd_buffer->cs);
   tu_cs_reset(cmd_buffer->device, &cmd_buffer->draw_cs);
   tu_cs_reset(cmd_buffer->device, &cmd_buffer->tile_cs);

   for (unsigned i = 0; i < VK_PIPELINE_BIND_POINT_RANGE_SIZE; i++) {
      cmd_buffer->descriptors[i].dirty = 0;
      cmd_buffer->descriptors[i].valid = 0;
      cmd_buffer->descriptors[i].push_dirty = false;
   }

   cmd_buffer->status = TU_CMD_BUFFER_STATUS_INITIAL;

   return cmd_buffer->record_result;
}

static VkResult
tu_cmd_state_setup_attachments(struct tu_cmd_buffer *cmd_buffer,
                               const VkRenderPassBeginInfo *info)
{
   struct tu_cmd_state *state = &cmd_buffer->state;
   const struct tu_framebuffer *fb = state->framebuffer;
   const struct tu_render_pass *pass = state->pass;

   for (uint32_t i = 0; i < fb->attachment_count; ++i) {
      const struct tu_image_view *iview = fb->attachments[i].attachment;
      tu_bo_list_add(&cmd_buffer->bo_list, iview->image->bo,
                     MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_WRITE);
   }

   if (pass->attachment_count == 0) {
      state->attachments = NULL;
      return VK_SUCCESS;
   }

   state->attachments =
      vk_alloc(&cmd_buffer->pool->alloc,
               pass->attachment_count * sizeof(state->attachments[0]), 8,
               VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (state->attachments == NULL) {
      cmd_buffer->record_result = VK_ERROR_OUT_OF_HOST_MEMORY;
      return cmd_buffer->record_result;
   }

   for (uint32_t i = 0; i < pass->attachment_count; ++i) {
      const struct tu_render_pass_attachment *att = &pass->attachments[i];
      VkImageAspectFlags att_aspects = vk_format_aspects(att->format);
      VkImageAspectFlags clear_aspects = 0;

      if (att_aspects == VK_IMAGE_ASPECT_COLOR_BIT) {
         /* color attachment */
         if (att->load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            clear_aspects |= VK_IMAGE_ASPECT_COLOR_BIT;
         }
      } else {
         /* depthstencil attachment */
         if ((att_aspects & VK_IMAGE_ASPECT_DEPTH_BIT) &&
             att->load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            clear_aspects |= VK_IMAGE_ASPECT_DEPTH_BIT;
            if ((att_aspects & VK_IMAGE_ASPECT_STENCIL_BIT) &&
                att->stencil_load_op == VK_ATTACHMENT_LOAD_OP_DONT_CARE)
               clear_aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
         }
         if ((att_aspects & VK_IMAGE_ASPECT_STENCIL_BIT) &&
             att->stencil_load_op == VK_ATTACHMENT_LOAD_OP_CLEAR) {
            clear_aspects |= VK_IMAGE_ASPECT_STENCIL_BIT;
         }
      }

      state->attachments[i].pending_clear_aspects = clear_aspects;
      state->attachments[i].cleared_views = 0;
      if (clear_aspects && info) {
         assert(info->clearValueCount > i);
         state->attachments[i].clear_value = info->pClearValues[i];
      }

      state->attachments[i].current_layout = att->initial_layout;
   }

   return VK_SUCCESS;
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

      if (!list_empty(&pool->free_cmd_buffers)) {
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

   cmd_buffer->marker_seqno = 0;
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
      VkResult result = tu_cs_reserve_space(cmd->device, draw_cs, 2);
      if (result != VK_SUCCESS) {
         cmd->record_result = result;
         return;
      }

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
}

void
tu_CmdPushConstants(VkCommandBuffer commandBuffer,
                    VkPipelineLayout layout,
                    VkShaderStageFlags stageFlags,
                    uint32_t offset,
                    uint32_t size,
                    const void *pValues)
{
}

VkResult
tu_EndCommandBuffer(VkCommandBuffer commandBuffer)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);

   if (cmd_buffer->scratch_seqno) {
      tu_bo_list_add(&cmd_buffer->bo_list, &cmd_buffer->scratch_bo,
                     MSM_SUBMIT_BO_WRITE);
   }

   for (uint32_t i = 0; i < cmd_buffer->draw_cs.bo_count; i++) {
      tu_bo_list_add(&cmd_buffer->bo_list, cmd_buffer->draw_cs.bos[i],
                     MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_DUMP);
   }

   for (uint32_t i = 0; i < cmd_buffer->tile_cs.bo_count; i++) {
      tu_bo_list_add(&cmd_buffer->bo_list, cmd_buffer->tile_cs.bos[i],
                     MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_DUMP);
   }

   tu_cs_end(&cmd_buffer->cs);

   assert(!cmd_buffer->state.attachments);

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
      tu_finishme("binding compute pipeline");
      break;
   default:
      unreachable("unrecognized pipeline bind point");
      break;
   }
}

void
tu_CmdSetViewport(VkCommandBuffer commandBuffer,
                  uint32_t firstViewport,
                  uint32_t viewportCount,
                  const VkViewport *pViewports)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *draw_cs = &cmd->draw_cs;

   VkResult result = tu_cs_reserve_space(cmd->device, draw_cs, 12);
   if (result != VK_SUCCESS) {
      cmd->record_result = result;
      return;
   }

   assert(firstViewport == 0 && viewportCount == 1);
   tu6_emit_viewport(draw_cs, pViewports);

   tu_cs_sanity_check(draw_cs);
}

void
tu_CmdSetScissor(VkCommandBuffer commandBuffer,
                 uint32_t firstScissor,
                 uint32_t scissorCount,
                 const VkRect2D *pScissors)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd, commandBuffer);
   struct tu_cs *draw_cs = &cmd->draw_cs;

   VkResult result = tu_cs_reserve_space(cmd->device, draw_cs, 3);
   if (result != VK_SUCCESS) {
      cmd->record_result = result;
      return;
   }

   assert(firstScissor == 0 && scissorCount == 1);
   tu6_emit_scissor(draw_cs, pScissors);

   tu_cs_sanity_check(draw_cs);
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

   VkResult result = tu_cs_reserve_space(cmd->device, draw_cs, 4);
   if (result != VK_SUCCESS) {
      cmd->record_result = result;
      return;
   }

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

   VkResult result = tu_cs_reserve_space(cmd->device, draw_cs, 5);
   if (result != VK_SUCCESS) {
      cmd->record_result = result;
      return;
   }

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
tu_CmdExecuteCommands(VkCommandBuffer commandBuffer,
                      uint32_t commandBufferCount,
                      const VkCommandBuffer *pCmdBuffers)
{
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
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   TU_FROM_HANDLE(tu_render_pass, pass, pRenderPassBegin->renderPass);
   TU_FROM_HANDLE(tu_framebuffer, framebuffer, pRenderPassBegin->framebuffer);
   VkResult result;

   cmd_buffer->state.pass = pass;
   cmd_buffer->state.subpass = pass->subpasses;
   cmd_buffer->state.framebuffer = framebuffer;

   result = tu_cmd_state_setup_attachments(cmd_buffer, pRenderPassBegin);
   if (result != VK_SUCCESS)
      return;

   tu_cmd_update_tiling_config(cmd_buffer, &pRenderPassBegin->renderArea);
   tu_cmd_prepare_tile_load_ib(cmd_buffer);
   tu_cmd_prepare_tile_store_ib(cmd_buffer);

   /* draw_cs should contain entries only for this render pass */
   assert(!cmd_buffer->draw_cs.entry_count);
   tu_cs_begin(&cmd_buffer->draw_cs);
}

void
tu_CmdBeginRenderPass2KHR(VkCommandBuffer commandBuffer,
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

   tu_cmd_render_tiles(cmd);

   cmd->state.subpass++;

   tu_cmd_update_tiling_config(cmd, NULL);
   tu_cmd_prepare_tile_load_ib(cmd);
   tu_cmd_prepare_tile_store_ib(cmd);
}

void
tu_CmdNextSubpass2KHR(VkCommandBuffer commandBuffer,
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
};

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

   TU_DRAW_STATE_COUNT,
};

struct tu_draw_state_group
{
   enum tu_draw_state_group_id id;
   uint32_t enable_mask;
   const struct tu_cs_entry *ib;
};

static void
tu6_bind_draw_states(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     const struct tu_draw_info *draw)
{
   const struct tu_pipeline *pipeline = cmd->state.pipeline;
   const struct tu_dynamic_state *dynamic = &cmd->state.dynamic;
   struct tu_draw_state_group draw_state_groups[TU_DRAW_STATE_COUNT];
   uint32_t draw_state_group_count = 0;

   VkResult result = tu_cs_reserve_space(cmd->device, cs, 256);
   if (result != VK_SUCCESS) {
      cmd->record_result = result;
      return;
   }

   /* TODO lrz */

   uint32_t pc_primitive_cntl = 0;
   if (pipeline->ia.primitive_restart && draw->indexed)
      pc_primitive_cntl |= A6XX_PC_PRIMITIVE_CNTL_0_PRIMITIVE_RESTART;

   tu_cs_emit_write_reg(cs, REG_A6XX_PC_UNKNOWN_9806, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_PC_UNKNOWN_9990, 0);
   tu_cs_emit_write_reg(cs, REG_A6XX_VFD_UNKNOWN_A008, 0);

   tu_cs_emit_pkt4(cs, REG_A6XX_PC_PRIMITIVE_CNTL_0, 1);
   tu_cs_emit(cs, pc_primitive_cntl);

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

   if (cmd->state.dirty &
       (TU_CMD_DIRTY_PIPELINE | TU_CMD_DIRTY_VERTEX_BUFFERS)) {
      for (uint32_t i = 0; i < pipeline->vi.count; i++) {
         const uint32_t binding = pipeline->vi.bindings[i];
         const uint32_t stride = pipeline->vi.strides[i];
         const struct tu_buffer *buf = cmd->state.vb.buffers[binding];
         const VkDeviceSize offset = buf->bo_offset +
                                     cmd->state.vb.offsets[binding] +
                                     pipeline->vi.offsets[i];
         const VkDeviceSize size =
            offset < buf->bo->size ? buf->bo->size - offset : 0;

         tu_cs_emit_pkt4(cs, REG_A6XX_VFD_FETCH(i), 4);
         tu_cs_emit_qw(cs, buf->bo->iova + offset);
         tu_cs_emit(cs, size);
         tu_cs_emit(cs, stride);
      }
   }

   /* TODO shader consts */

   if (cmd->state.dirty & TU_CMD_DIRTY_PIPELINE) {
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_PROGRAM,
            .enable_mask = 0x6,
            .ib = &pipeline->program.state_ib,
         };
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_PROGRAM_BINNING,
            .enable_mask = 0x1,
            .ib = &pipeline->program.binning_state_ib,
         };
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_VI,
            .enable_mask = 0x6,
            .ib = &pipeline->vi.state_ib,
         };
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_VI_BINNING,
            .enable_mask = 0x1,
            .ib = &pipeline->vi.binning_state_ib,
         };
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_VP,
            .enable_mask = 0x7,
            .ib = &pipeline->vp.state_ib,
         };
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_RAST,
            .enable_mask = 0x7,
            .ib = &pipeline->rast.state_ib,
         };
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_DS,
            .enable_mask = 0x7,
            .ib = &pipeline->ds.state_ib,
         };
      draw_state_groups[draw_state_group_count++] =
         (struct tu_draw_state_group) {
            .id = TU_DRAW_STATE_BLEND,
            .enable_mask = 0x7,
            .ib = &pipeline->blend.state_ib,
         };
   }

   tu_cs_emit_pkt7(cs, CP_SET_DRAW_STATE, 3 * draw_state_group_count);
   for (uint32_t i = 0; i < draw_state_group_count; i++) {
      const struct tu_draw_state_group *group = &draw_state_groups[i];

      uint32_t cp_set_draw_state =
         CP_SET_DRAW_STATE__0_COUNT(group->ib->size / 4) |
         CP_SET_DRAW_STATE__0_ENABLE_MASK(group->enable_mask) |
         CP_SET_DRAW_STATE__0_GROUP_ID(group->id);
      uint64_t iova;
      if (group->ib->size) {
         iova = group->ib->bo->iova + group->ib->offset;
      } else {
         cp_set_draw_state |= CP_SET_DRAW_STATE__0_DISABLE;
         iova = 0;
      }

      tu_cs_emit(cs, cp_set_draw_state);
      tu_cs_emit_qw(cs, iova);
   }

   tu_cs_sanity_check(cs);

   /* track BOs */
   if (cmd->state.dirty & TU_CMD_DIRTY_PIPELINE) {
      tu_bo_list_add(&cmd->bo_list, &pipeline->program.binary_bo,
                     MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_DUMP);
      for (uint32_t i = 0; i < pipeline->cs.bo_count; i++) {
         tu_bo_list_add(&cmd->bo_list, pipeline->cs.bos[i],
                        MSM_SUBMIT_BO_READ | MSM_SUBMIT_BO_DUMP);
      }
   }
   if (cmd->state.dirty & TU_CMD_DIRTY_VERTEX_BUFFERS) {
      for (uint32_t i = 0; i < MAX_VBS; i++) {
         const struct tu_buffer *buf = cmd->state.vb.buffers[i];
         if (buf)
            tu_bo_list_add(&cmd->bo_list, buf->bo, MSM_SUBMIT_BO_READ);
      }
   }

   cmd->state.dirty = 0;
}

static void
tu6_emit_draw_direct(struct tu_cmd_buffer *cmd,
                     struct tu_cs *cs,
                     const struct tu_draw_info *draw)
{

   const enum pc_di_primtype primtype = cmd->state.pipeline->ia.primtype;

   tu_cs_emit_pkt4(cs, REG_A6XX_VFD_INDEX_OFFSET, 2);
   tu_cs_emit(cs, draw->vertex_offset);
   tu_cs_emit(cs, draw->first_instance);

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
         CP_DRAW_INDX_OFFSET_0_VIS_CULL(IGNORE_VISIBILITY) | 0x2000;

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
         CP_DRAW_INDX_OFFSET_0_VIS_CULL(IGNORE_VISIBILITY) | 0x2000;

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

   tu6_bind_draw_states(cmd, cs, draw);

   VkResult result = tu_cs_reserve_space(cmd->device, cs, 32);
   if (result != VK_SUCCESS) {
      cmd->record_result = result;
      return;
   }

   if (draw->indirect) {
      tu_finishme("indirect draw");
      return;
   }

   /* TODO tu6_emit_marker should pick different regs depending on cs */
   tu6_emit_marker(cmd, cs);
   tu6_emit_draw_direct(cmd, cs, draw);
   tu6_emit_marker(cmd, cs);

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
tu_dispatch(struct tu_cmd_buffer *cmd_buffer,
            const struct tu_dispatch_info *info)
{
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

   tu_cmd_render_tiles(cmd_buffer);

   /* discard draw_cs entries now that the tiles are rendered */
   tu_cs_discard_entries(&cmd_buffer->draw_cs);

   vk_free(&cmd_buffer->pool->alloc, cmd_buffer->state.attachments);
   cmd_buffer->state.attachments = NULL;

   cmd_buffer->state.pass = NULL;
   cmd_buffer->state.subpass = NULL;
   cmd_buffer->state.framebuffer = NULL;
}

void
tu_CmdEndRenderPass2KHR(VkCommandBuffer commandBuffer,
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
tu_barrier(struct tu_cmd_buffer *cmd_buffer,
           uint32_t memoryBarrierCount,
           const VkMemoryBarrier *pMemoryBarriers,
           uint32_t bufferMemoryBarrierCount,
           const VkBufferMemoryBarrier *pBufferMemoryBarriers,
           uint32_t imageMemoryBarrierCount,
           const VkImageMemoryBarrier *pImageMemoryBarriers,
           const struct tu_barrier_info *info)
{
}

void
tu_CmdPipelineBarrier(VkCommandBuffer commandBuffer,
                      VkPipelineStageFlags srcStageMask,
                      VkPipelineStageFlags destStageMask,
                      VkBool32 byRegion,
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
write_event(struct tu_cmd_buffer *cmd_buffer,
            struct tu_event *event,
            VkPipelineStageFlags stageMask,
            unsigned value)
{
}

void
tu_CmdSetEvent(VkCommandBuffer commandBuffer,
               VkEvent _event,
               VkPipelineStageFlags stageMask)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   TU_FROM_HANDLE(tu_event, event, _event);

   write_event(cmd_buffer, event, stageMask, 1);
}

void
tu_CmdResetEvent(VkCommandBuffer commandBuffer,
                 VkEvent _event,
                 VkPipelineStageFlags stageMask)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   TU_FROM_HANDLE(tu_event, event, _event);

   write_event(cmd_buffer, event, stageMask, 0);
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
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   struct tu_barrier_info info;

   info.eventCount = eventCount;
   info.pEvents = pEvents;
   info.srcStageMask = 0;

   tu_barrier(cmd_buffer, memoryBarrierCount, pMemoryBarriers,
              bufferMemoryBarrierCount, pBufferMemoryBarriers,
              imageMemoryBarrierCount, pImageMemoryBarriers, &info);
}

void
tu_CmdSetDeviceMask(VkCommandBuffer commandBuffer, uint32_t deviceMask)
{
   /* No-op */
}
