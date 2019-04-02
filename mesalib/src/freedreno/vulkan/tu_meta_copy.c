/*
 * Copyright © 2016 Intel Corporation
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

#include "a6xx.xml.h"
#include "adreno_common.xml.h"
#include "adreno_pm4.xml.h"

#include "vk_format.h"

#include "tu_cs.h"

/*
 * TODO:
 *   - image -> image copies
 *   - 3D textures
 *   - compressed image formats (need to divide offset/extent)
 */

static uint32_t
blit_control(enum a6xx_color_fmt fmt)
{
   unsigned blit_cntl = 0xf00000;
   blit_cntl |= A6XX_RB_2D_BLIT_CNTL_COLOR_FORMAT(fmt);
   blit_cntl |= A6XX_RB_2D_BLIT_CNTL_IFMT(tu6_rb_fmt_to_ifmt(fmt));
   return blit_cntl;
}

static uint32_t tu6_sp_2d_src_format(VkFormat format)
{
   const struct vk_format_description *desc = vk_format_description(format);
   uint32_t reg = 0xf000 | A6XX_SP_2D_SRC_FORMAT_COLOR_FORMAT(tu6_get_native_format(format)->rb);

   int channel = vk_format_get_first_non_void_channel(format);
   if (channel < 0) {
      /* TODO special format. */
      return reg;
   }
   if (desc->channel[channel].normalized) {
      if (desc->channel[channel].type == VK_FORMAT_TYPE_SIGNED)
         reg |= A6XX_SP_2D_SRC_FORMAT_SINT;
      reg |= A6XX_SP_2D_SRC_FORMAT_NORM;
   } else if (desc->channel[channel].pure_integer) {
      if (desc->channel[channel].type == VK_FORMAT_TYPE_SIGNED)
         reg |= A6XX_SP_2D_SRC_FORMAT_SINT;
      else
         reg |= A6XX_SP_2D_SRC_FORMAT_UINT;
   }
   return reg;
}

static void
tu_dma_prepare(struct tu_cmd_buffer *cmdbuf)
{
   tu_cs_reserve_space(cmdbuf->device, &cmdbuf->cs, 10);

   tu_cs_emit_pkt7(&cmdbuf->cs, CP_EVENT_WRITE, 1);
   tu_cs_emit(&cmdbuf->cs, PC_CCU_INVALIDATE_COLOR);

   tu_cs_emit_pkt7(&cmdbuf->cs, CP_EVENT_WRITE, 1);
   tu_cs_emit(&cmdbuf->cs, LRZ_FLUSH);

   tu_cs_emit_pkt7(&cmdbuf->cs, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   tu_cs_emit(&cmdbuf->cs, 0x0);

   tu_cs_emit_wfi(&cmdbuf->cs);

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_RB_CCU_CNTL, 1);
   tu_cs_emit(&cmdbuf->cs, 0x10000000);
}

static void
tu_copy_buffer(struct tu_cmd_buffer *cmdbuf,
               struct tu_bo *src_bo,
               uint64_t src_offset,
               struct tu_bo *dst_bo,
               uint64_t dst_offset,
               uint64_t size)
{
   const unsigned max_size_per_iter = 0x4000 - 0x40;
   const unsigned max_iterations =
      (size + max_size_per_iter) / max_size_per_iter;

   tu_bo_list_add(&cmdbuf->bo_list, src_bo, MSM_SUBMIT_BO_READ);
   tu_bo_list_add(&cmdbuf->bo_list, dst_bo, MSM_SUBMIT_BO_WRITE);

   tu_dma_prepare(cmdbuf);

   tu_cs_reserve_space(cmdbuf->device, &cmdbuf->cs, 21 + 48 * max_iterations);

   /* buffer copy setup */
   tu_cs_emit_pkt7(&cmdbuf->cs, CP_SET_MARKER, 1);
   tu_cs_emit(&cmdbuf->cs, A2XX_CP_SET_MARKER_0_MODE(RM6_BLIT2DSCALE));

   const uint32_t blit_cntl = blit_control(RB6_R8_UNORM) | 0x20000000;

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_RB_2D_BLIT_CNTL, 1);
   tu_cs_emit(&cmdbuf->cs, blit_cntl);

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_GRAS_2D_BLIT_CNTL, 1);
   tu_cs_emit(&cmdbuf->cs, blit_cntl);

   for (; size;) {
      uint64_t src_va = src_bo->iova + src_offset;
      uint64_t dst_va = dst_bo->iova + dst_offset;

      unsigned src_shift = src_va & 0x3f;
      unsigned dst_shift = dst_va & 0x3f;
      unsigned max_shift = MAX2(src_shift, dst_shift);

      src_va -= src_shift;
      dst_va -= dst_shift;

      uint32_t size_todo = MIN2(0x4000 - max_shift, size);
      unsigned pitch = (size_todo + max_shift + 63) & ~63;

      /*
       * Emit source:
       */
      tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_SP_PS_2D_SRC_INFO, 13);
      tu_cs_emit(&cmdbuf->cs,
                 A6XX_SP_PS_2D_SRC_INFO_COLOR_FORMAT(RB6_R8_UNORM) |
                    A6XX_SP_PS_2D_SRC_INFO_TILE_MODE(TILE6_LINEAR) |
                    A6XX_SP_PS_2D_SRC_INFO_COLOR_SWAP(WZYX) | 0x500000);
      tu_cs_emit(&cmdbuf->cs,
                 A6XX_SP_PS_2D_SRC_SIZE_WIDTH(src_shift + size_todo) |
                    A6XX_SP_PS_2D_SRC_SIZE_HEIGHT(1)); /* SP_PS_2D_SRC_SIZE */
      tu_cs_emit_qw(&cmdbuf->cs, src_va);
      tu_cs_emit(&cmdbuf->cs, A6XX_SP_PS_2D_SRC_PITCH_PITCH(pitch));

      tu_cs_emit(&cmdbuf->cs, 0x00000000);
      tu_cs_emit(&cmdbuf->cs, 0x00000000);
      tu_cs_emit(&cmdbuf->cs, 0x00000000);
      tu_cs_emit(&cmdbuf->cs, 0x00000000);
      tu_cs_emit(&cmdbuf->cs, 0x00000000);

      tu_cs_emit(&cmdbuf->cs, 0x00000000);
      tu_cs_emit(&cmdbuf->cs, 0x00000000);
      tu_cs_emit(&cmdbuf->cs, 0x00000000);

      /*
       * Emit destination:
       */
      tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_RB_2D_DST_INFO, 9);
      tu_cs_emit(&cmdbuf->cs, A6XX_RB_2D_DST_INFO_COLOR_FORMAT(RB6_R8_UNORM) |
                                 A6XX_RB_2D_DST_INFO_TILE_MODE(TILE6_LINEAR) |
                                 A6XX_RB_2D_DST_INFO_COLOR_SWAP(WZYX));
      tu_cs_emit_qw(&cmdbuf->cs, dst_va);

      tu_cs_emit(&cmdbuf->cs, A6XX_RB_2D_DST_SIZE_PITCH(pitch));
      tu_cs_emit(&cmdbuf->cs, 0x00000000);
      tu_cs_emit(&cmdbuf->cs, 0x00000000);
      tu_cs_emit(&cmdbuf->cs, 0x00000000);
      tu_cs_emit(&cmdbuf->cs, 0x00000000);
      tu_cs_emit(&cmdbuf->cs, 0x00000000);

      /*
       * Blit command:
       */
      tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_GRAS_2D_SRC_TL_X, 4);
      tu_cs_emit(&cmdbuf->cs, A6XX_GRAS_2D_SRC_TL_X_X(src_shift));
      tu_cs_emit(&cmdbuf->cs,
                 A6XX_GRAS_2D_SRC_BR_X_X(src_shift + size_todo - 1));
      tu_cs_emit(&cmdbuf->cs, A6XX_GRAS_2D_SRC_TL_Y_Y(0));
      tu_cs_emit(&cmdbuf->cs, A6XX_GRAS_2D_SRC_BR_Y_Y(0));

      tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_GRAS_2D_DST_TL, 2);
      tu_cs_emit(&cmdbuf->cs,
                 A6XX_GRAS_2D_DST_TL_X(dst_shift) | A6XX_GRAS_2D_DST_TL_Y(0));
      tu_cs_emit(&cmdbuf->cs,
                 A6XX_GRAS_2D_DST_BR_X(dst_shift + size_todo - 1) |
                    A6XX_GRAS_2D_DST_BR_Y(0));

      tu_cs_emit_pkt7(&cmdbuf->cs, CP_EVENT_WRITE, 1);
      tu_cs_emit(&cmdbuf->cs, 0x3f);
      tu_cs_emit_wfi(&cmdbuf->cs);

      tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_RB_UNKNOWN_8C01, 1);
      tu_cs_emit(&cmdbuf->cs, 0);

      tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_SP_2D_SRC_FORMAT, 1);
      tu_cs_emit(&cmdbuf->cs, 0xf180);

      tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_RB_UNKNOWN_8E04, 1);
      tu_cs_emit(&cmdbuf->cs, 0x01000000);

      tu_cs_emit_pkt7(&cmdbuf->cs, CP_BLIT, 1);
      tu_cs_emit(&cmdbuf->cs, CP_BLIT_0_OP(BLIT_OP_SCALE));

      tu_cs_emit_wfi(&cmdbuf->cs);

      tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_RB_UNKNOWN_8E04, 1);
      tu_cs_emit(&cmdbuf->cs, 0);

      src_offset += size_todo;
      dst_offset += size_todo;
      size -= size_todo;
   }

   tu6_emit_event_write(cmdbuf, &cmdbuf->cs, 0x1d, true);
   tu6_emit_event_write(cmdbuf, &cmdbuf->cs, FACENESS_FLUSH, true);
   tu6_emit_event_write(cmdbuf, &cmdbuf->cs, CACHE_FLUSH_TS, true);
}

static void
tu_copy_buffer_to_image_step(struct tu_cmd_buffer *cmdbuf,
                             struct tu_buffer *src_buffer,
                             struct tu_image *dst_image,
                             const VkBufferImageCopy *copy_info,
                             VkFormat format,
                             uint32_t layer,
                             uint64_t src_va)
{
   const enum a6xx_color_fmt rb_fmt = tu6_get_native_format(format)->rb;

   uint64_t dst_va = dst_image->bo->iova + dst_image->bo_offset + dst_image->layer_size * layer + dst_image->levels[copy_info->imageSubresource.mipLevel].offset;
   unsigned dst_pitch = dst_image->levels[copy_info->imageSubresource.mipLevel].pitch *
                        vk_format_get_blocksize(format);

   unsigned src_pitch;
   unsigned src_offset = 0;
   if (copy_info->imageExtent.height == 1) {
      /* Can't find this in the spec, but not having it is sort of insane? */
      assert(src_va % vk_format_get_blocksize(format) == 0);

      src_offset = (src_va & 63) / vk_format_get_blocksize(format);
      src_va &= ~63;

      src_pitch = align((src_offset + copy_info->imageExtent.width) * vk_format_get_blocksize(format), 64);
   } else {
      unsigned src_pixel_stride = copy_info->bufferRowLength
                                  ? copy_info->bufferRowLength
                                  : copy_info->imageExtent.width;
      src_pitch = src_pixel_stride * vk_format_get_blocksize(format);
      assert(!(src_pitch & 63));
      assert(!(src_va & 63));
   }

   tu_cs_reserve_space(cmdbuf->device, &cmdbuf->cs, 48);

   /*
    * Emit source:
    */
   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_SP_PS_2D_SRC_INFO, 13);
   tu_cs_emit(&cmdbuf->cs, A6XX_SP_PS_2D_SRC_INFO_COLOR_FORMAT(rb_fmt) |
                              A6XX_SP_PS_2D_SRC_INFO_TILE_MODE(TILE6_LINEAR) |
                              A6XX_SP_PS_2D_SRC_INFO_COLOR_SWAP(WZYX) |
                              0x500000);
   tu_cs_emit(&cmdbuf->cs,
              A6XX_SP_PS_2D_SRC_SIZE_WIDTH(src_offset + copy_info->imageExtent.width) |
                 A6XX_SP_PS_2D_SRC_SIZE_HEIGHT(
                    copy_info->imageExtent.height)); /* SP_PS_2D_SRC_SIZE */
   tu_cs_emit_qw(&cmdbuf->cs, src_va);
   tu_cs_emit(&cmdbuf->cs, A6XX_SP_PS_2D_SRC_PITCH_PITCH(src_pitch));

   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);

   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);

   /*
    * Emit destination:
    */
   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_RB_2D_DST_INFO, 9);
   tu_cs_emit(&cmdbuf->cs,
              A6XX_RB_2D_DST_INFO_COLOR_FORMAT(rb_fmt) |
                 A6XX_RB_2D_DST_INFO_TILE_MODE(dst_image->tile_mode) |
                 A6XX_RB_2D_DST_INFO_COLOR_SWAP(WZYX));
   tu_cs_emit_qw(&cmdbuf->cs, dst_va);
   tu_cs_emit(&cmdbuf->cs, A6XX_RB_2D_DST_SIZE_PITCH(dst_pitch));
   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_GRAS_2D_SRC_TL_X, 4);
   tu_cs_emit(&cmdbuf->cs, A6XX_GRAS_2D_SRC_TL_X_X(src_offset));
   tu_cs_emit(&cmdbuf->cs,
              A6XX_GRAS_2D_SRC_BR_X_X(src_offset + copy_info->imageExtent.width - 1));
   tu_cs_emit(&cmdbuf->cs, A6XX_GRAS_2D_SRC_TL_Y_Y(0));
   tu_cs_emit(&cmdbuf->cs,
              A6XX_GRAS_2D_SRC_BR_Y_Y(copy_info->imageExtent.height - 1));

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_GRAS_2D_DST_TL, 2);
   tu_cs_emit(&cmdbuf->cs,
              A6XX_GRAS_2D_DST_TL_X(copy_info->imageOffset.x) |
                 A6XX_GRAS_2D_DST_TL_Y(copy_info->imageOffset.y));
   tu_cs_emit(&cmdbuf->cs,
              A6XX_GRAS_2D_DST_BR_X(copy_info->imageOffset.x +
                                    copy_info->imageExtent.width - 1) |
                 A6XX_GRAS_2D_DST_BR_Y(copy_info->imageOffset.y +
                                       copy_info->imageExtent.height - 1));

   tu_cs_emit_pkt7(&cmdbuf->cs, CP_EVENT_WRITE, 1);
   tu_cs_emit(&cmdbuf->cs, 0x3f);
   tu_cs_emit_wfi(&cmdbuf->cs);

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_RB_UNKNOWN_8C01, 1);
   tu_cs_emit(&cmdbuf->cs, 0);

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_SP_2D_SRC_FORMAT, 1);
   tu_cs_emit(&cmdbuf->cs, tu6_sp_2d_src_format(format));

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_RB_UNKNOWN_8E04, 1);
   tu_cs_emit(&cmdbuf->cs, 0x01000000);

   tu_cs_emit_pkt7(&cmdbuf->cs, CP_BLIT, 1);
   tu_cs_emit(&cmdbuf->cs, CP_BLIT_0_OP(BLIT_OP_SCALE));

   tu_cs_emit_wfi(&cmdbuf->cs);

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_RB_UNKNOWN_8E04, 1);
   tu_cs_emit(&cmdbuf->cs, 0);
}

static void
tu_copy_buffer_to_image(struct tu_cmd_buffer *cmdbuf,
                        struct tu_buffer *src_buffer,
                        struct tu_image *dst_image,
                        const VkBufferImageCopy *copy_info)
{
   tu_bo_list_add(&cmdbuf->bo_list, src_buffer->bo, MSM_SUBMIT_BO_READ);
   tu_bo_list_add(&cmdbuf->bo_list, dst_image->bo, MSM_SUBMIT_BO_WRITE);

   /* general setup */
   tu_dma_prepare(cmdbuf);

   tu_cs_reserve_space(cmdbuf->device, &cmdbuf->cs, 6);

   /* buffer copy setup */
   tu_cs_emit_pkt7(&cmdbuf->cs, CP_SET_MARKER, 1);
   tu_cs_emit(&cmdbuf->cs, A2XX_CP_SET_MARKER_0_MODE(RM6_BLIT2DSCALE));

   VkFormat format = dst_image->vk_format;
   const enum a6xx_color_fmt rb_fmt = tu6_get_native_format(format)->rb;

   const uint32_t blit_cntl = blit_control(rb_fmt) | 0x20000000;

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_RB_2D_BLIT_CNTL, 1);
   tu_cs_emit(&cmdbuf->cs, blit_cntl);

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_GRAS_2D_BLIT_CNTL, 1);
   tu_cs_emit(&cmdbuf->cs, blit_cntl);

   unsigned src_pixel_stride = copy_info->bufferRowLength
                                  ? copy_info->bufferRowLength
                                  : copy_info->imageExtent.width;
   unsigned cpp = vk_format_get_blocksize(format);
   unsigned src_pitch = src_pixel_stride * cpp;

   for (unsigned layer_offset = 0; layer_offset < copy_info->imageSubresource.layerCount; ++layer_offset) {
      unsigned layer = copy_info->imageSubresource.baseArrayLayer + layer_offset;
      uint64_t src_va = src_buffer->bo->iova + src_buffer->bo_offset + copy_info->bufferOffset + layer_offset * copy_info->bufferImageHeight * src_pitch;
       
      if ((src_pitch & 63) || (src_va & 63)) {
         /* Do a per line copy */
         VkBufferImageCopy line_copy_info = *copy_info;
         line_copy_info.imageExtent.height = 1;
         for (unsigned r = 0; r < copy_info->imageExtent.height; ++r) {
            /*
             * if src_va is not aligned the line copy will need to adjust. Give it
             * room to do so.
             */
            unsigned max_width = 16384 - (src_va & 0x3f) ? 64 : 0;
            line_copy_info.imageOffset.x = copy_info->imageOffset.x;
            line_copy_info.imageExtent.width = copy_info->imageExtent.width;
            
            for (unsigned c = 0; c < copy_info->imageExtent.width; c += max_width) {
               tu_copy_buffer_to_image_step(cmdbuf, src_buffer, dst_image, &line_copy_info, format, layer, src_va + c * cpp);

               line_copy_info.imageOffset.x += max_width;
               line_copy_info.imageExtent.width -= max_width;
            }
   
            line_copy_info.imageOffset.y++;
            src_va += src_pitch;
         }
      } else {
         tu_copy_buffer_to_image_step(cmdbuf, src_buffer, dst_image, copy_info, format, layer, src_va);
      }
   }

   tu_cs_reserve_space(cmdbuf->device, &cmdbuf->cs, 15);

   tu6_emit_event_write(cmdbuf, &cmdbuf->cs, 0x1d, true);
   tu6_emit_event_write(cmdbuf, &cmdbuf->cs, FACENESS_FLUSH, true);
   tu6_emit_event_write(cmdbuf, &cmdbuf->cs, CACHE_FLUSH_TS, true);
}

static void
tu_copy_image_to_buffer_step(struct tu_cmd_buffer *cmdbuf,
                             struct tu_image *src_image,
                             struct tu_buffer *dst_buffer,
                             const VkBufferImageCopy *copy_info,
                             VkFormat format,
                             uint32_t layer,
                             uint64_t dst_va)
{
   const enum a6xx_color_fmt rb_fmt = tu6_get_native_format(format)->rb;

   uint64_t src_va = src_image->bo->iova + src_image->bo_offset + src_image->layer_size * layer + src_image->levels[copy_info->imageSubresource.mipLevel].offset;
   unsigned src_pitch = src_image->levels[copy_info->imageSubresource.mipLevel].pitch *
                        vk_format_get_blocksize(format);

   unsigned dst_pitch;
   unsigned dst_offset = 0;
   if (copy_info->imageExtent.height == 1) {
      /* Can't find this in the spec, but not having it is sort of insane? */
      assert(dst_va % vk_format_get_blocksize(format) == 0);

      dst_offset = (dst_va & 63) / vk_format_get_blocksize(format);
      dst_va &= ~63;

      dst_pitch = align((dst_offset + copy_info->imageExtent.width) * vk_format_get_blocksize(format), 64);
   } else {
      unsigned dst_pixel_stride = copy_info->bufferRowLength
                                  ? copy_info->bufferRowLength
                                  : copy_info->imageExtent.width;
      dst_pitch = dst_pixel_stride * vk_format_get_blocksize(format);
      assert(!(dst_pitch & 63));
      assert(!(dst_va & 63));
   }
   

   tu_cs_reserve_space(cmdbuf->device, &cmdbuf->cs, 48);

   /*
    * Emit source:
    */
   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_SP_PS_2D_SRC_INFO, 13);
   tu_cs_emit(&cmdbuf->cs,
              A6XX_SP_PS_2D_SRC_INFO_COLOR_FORMAT(rb_fmt) |
                 A6XX_SP_PS_2D_SRC_INFO_TILE_MODE(src_image->tile_mode) |
                 A6XX_SP_PS_2D_SRC_INFO_COLOR_SWAP(WZYX) | 0x500000);
   tu_cs_emit(&cmdbuf->cs,
              A6XX_SP_PS_2D_SRC_SIZE_WIDTH(src_image->extent.width) |
                 A6XX_SP_PS_2D_SRC_SIZE_HEIGHT(
                    src_image->extent.height)); /* SP_PS_2D_SRC_SIZE */
   tu_cs_emit_qw(&cmdbuf->cs, src_va);
   tu_cs_emit(&cmdbuf->cs, A6XX_SP_PS_2D_SRC_PITCH_PITCH(src_pitch));

   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);

   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);

   /*
    * Emit destination:
    */
   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_RB_2D_DST_INFO, 9);
   tu_cs_emit(&cmdbuf->cs, A6XX_RB_2D_DST_INFO_COLOR_FORMAT(rb_fmt) |
                              A6XX_RB_2D_DST_INFO_TILE_MODE(TILE6_LINEAR) |
                              A6XX_RB_2D_DST_INFO_COLOR_SWAP(WZYX));
   tu_cs_emit_qw(&cmdbuf->cs, dst_va);
   tu_cs_emit(&cmdbuf->cs, A6XX_RB_2D_DST_SIZE_PITCH(dst_pitch));
   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);
   tu_cs_emit(&cmdbuf->cs, 0x00000000);

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_GRAS_2D_SRC_TL_X, 4);
   tu_cs_emit(&cmdbuf->cs, A6XX_GRAS_2D_SRC_TL_X_X(copy_info->imageOffset.x));
   tu_cs_emit(&cmdbuf->cs,
              A6XX_GRAS_2D_SRC_BR_X_X(copy_info->imageOffset.x +
                                      copy_info->imageExtent.width - 1));
   tu_cs_emit(&cmdbuf->cs, A6XX_GRAS_2D_SRC_TL_Y_Y(copy_info->imageOffset.y));
   tu_cs_emit(&cmdbuf->cs,
              A6XX_GRAS_2D_SRC_BR_Y_Y(copy_info->imageOffset.y +
                                      copy_info->imageExtent.height - 1));

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_GRAS_2D_DST_TL, 2);
   tu_cs_emit(&cmdbuf->cs,
              A6XX_GRAS_2D_DST_TL_X(dst_offset) | A6XX_GRAS_2D_DST_TL_Y(0));
   tu_cs_emit(&cmdbuf->cs,
              A6XX_GRAS_2D_DST_BR_X(dst_offset + copy_info->imageExtent.width - 1) |
                 A6XX_GRAS_2D_DST_BR_Y(copy_info->imageExtent.height - 1));

   tu_cs_emit_pkt7(&cmdbuf->cs, CP_EVENT_WRITE, 1);
   tu_cs_emit(&cmdbuf->cs, 0x3f);
   tu_cs_emit_wfi(&cmdbuf->cs);

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_RB_UNKNOWN_8C01, 1);
   tu_cs_emit(&cmdbuf->cs, 0);

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_SP_2D_SRC_FORMAT, 1);
   tu_cs_emit(&cmdbuf->cs, tu6_sp_2d_src_format(format));

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_RB_UNKNOWN_8E04, 1);
   tu_cs_emit(&cmdbuf->cs, 0x01000000);

   tu_cs_emit_pkt7(&cmdbuf->cs, CP_BLIT, 1);
   tu_cs_emit(&cmdbuf->cs, CP_BLIT_0_OP(BLIT_OP_SCALE));

   tu_cs_emit_wfi(&cmdbuf->cs);

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_RB_UNKNOWN_8E04, 1);
   tu_cs_emit(&cmdbuf->cs, 0);
}

static void
tu_copy_image_to_buffer(struct tu_cmd_buffer *cmdbuf,
                        struct tu_image *src_image,
                        struct tu_buffer *dst_buffer,
                        const VkBufferImageCopy *copy_info)
{
   tu_bo_list_add(&cmdbuf->bo_list, src_image->bo, MSM_SUBMIT_BO_READ);
   tu_bo_list_add(&cmdbuf->bo_list, dst_buffer->bo, MSM_SUBMIT_BO_WRITE);

   /* general setup */
   tu_dma_prepare(cmdbuf);

   tu_cs_reserve_space(cmdbuf->device, &cmdbuf->cs, 6);

   /* buffer copy setup */
   tu_cs_emit_pkt7(&cmdbuf->cs, CP_SET_MARKER, 1);
   tu_cs_emit(&cmdbuf->cs, A2XX_CP_SET_MARKER_0_MODE(RM6_BLIT2DSCALE));

   VkFormat format = src_image->vk_format;
   const enum a6xx_color_fmt rb_fmt = tu6_get_native_format(format)->rb;

   unsigned dst_pixel_stride = copy_info->bufferRowLength
                                  ? copy_info->bufferRowLength
                                  : copy_info->imageExtent.width;
   unsigned cpp = vk_format_get_blocksize(format);
   unsigned dst_pitch = dst_pixel_stride * cpp;


   const uint32_t blit_cntl = blit_control(rb_fmt) | 0x20000000;

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_RB_2D_BLIT_CNTL, 1);
   tu_cs_emit(&cmdbuf->cs, blit_cntl);

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_GRAS_2D_BLIT_CNTL, 1);
   tu_cs_emit(&cmdbuf->cs, blit_cntl);

   for (unsigned layer_offset = 0; layer_offset < copy_info->imageSubresource.layerCount; ++layer_offset) {
       unsigned layer = copy_info->imageSubresource.baseArrayLayer + layer_offset;
       uint64_t dst_va = dst_buffer->bo->iova + dst_buffer->bo_offset + copy_info->bufferOffset + layer_offset * copy_info->bufferImageHeight * dst_pitch;

       if ((dst_pitch & 63) || (dst_va & 63)) {
         /* Do a per line copy */
         VkBufferImageCopy line_copy_info = *copy_info;
         line_copy_info.imageExtent.height = 1;
         for (unsigned r = 0; r < copy_info->imageExtent.height; ++r) {
            /*
             * if dst_va is not aligned the line copy will need to adjust. Give it
             * room to do so.
             */
            unsigned max_width = 16384 - (dst_va & 0x3f) ? 64 : 0;
            line_copy_info.imageOffset.x = copy_info->imageOffset.x;
            line_copy_info.imageExtent.width = copy_info->imageExtent.width;

            for (unsigned c = 0; c < copy_info->imageExtent.width; c += max_width) {
               tu_copy_image_to_buffer_step(cmdbuf, src_image, dst_buffer, &line_copy_info, format, layer, dst_va + c * cpp);

               line_copy_info.imageOffset.x += max_width;
               line_copy_info.imageExtent.width -= max_width;
            }

            line_copy_info.imageOffset.y++;
            dst_va += dst_pitch;
         }
      } else {
         tu_copy_image_to_buffer_step(cmdbuf, src_image, dst_buffer, copy_info, format, layer, dst_va);
      }
   }

   tu_cs_reserve_space(cmdbuf->device, &cmdbuf->cs, 15);

   tu6_emit_event_write(cmdbuf, &cmdbuf->cs, 0x1d, true);
   tu6_emit_event_write(cmdbuf, &cmdbuf->cs, FACENESS_FLUSH, true);
   tu6_emit_event_write(cmdbuf, &cmdbuf->cs, CACHE_FLUSH_TS, true);
}

void
tu_CmdCopyBuffer(VkCommandBuffer commandBuffer,
                 VkBuffer srcBuffer,
                 VkBuffer destBuffer,
                 uint32_t regionCount,
                 const VkBufferCopy *pRegions)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   TU_FROM_HANDLE(tu_buffer, src_buffer, srcBuffer);
   TU_FROM_HANDLE(tu_buffer, dst_buffer, destBuffer);

   for (unsigned i = 0; i < regionCount; ++i) {
      uint64_t src_offset = src_buffer->bo_offset + pRegions[i].srcOffset;
      uint64_t dst_offset = dst_buffer->bo_offset + pRegions[i].dstOffset;

      tu_copy_buffer(cmdbuf, src_buffer->bo, src_offset, dst_buffer->bo,
                     dst_offset, pRegions[i].size);
   }
}

void
tu_CmdCopyBufferToImage(VkCommandBuffer commandBuffer,
                        VkBuffer srcBuffer,
                        VkImage destImage,
                        VkImageLayout destImageLayout,
                        uint32_t regionCount,
                        const VkBufferImageCopy *pRegions)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   TU_FROM_HANDLE(tu_image, dest_image, destImage);
   TU_FROM_HANDLE(tu_buffer, src_buffer, srcBuffer);

   for (unsigned i = 0; i < regionCount; ++i) {
      tu_copy_buffer_to_image(cmd_buffer, src_buffer, dest_image,
                              pRegions + i);
   }
}

void
tu_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer,
                        VkImage srcImage,
                        VkImageLayout srcImageLayout,
                        VkBuffer destBuffer,
                        uint32_t regionCount,
                        const VkBufferImageCopy *pRegions)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   TU_FROM_HANDLE(tu_image, src_image, srcImage);
   TU_FROM_HANDLE(tu_buffer, dst_buffer, destBuffer);

   for (unsigned i = 0; i < regionCount; ++i) {
      tu_copy_image_to_buffer(cmd_buffer, src_image, dst_buffer,
                              pRegions + i);
   }
}

static void
meta_copy_image(struct tu_cmd_buffer *cmd_buffer,
                struct tu_image *src_image,
                VkImageLayout src_image_layout,
                struct tu_image *dest_image,
                VkImageLayout dest_image_layout,
                uint32_t regionCount,
                const VkImageCopy *pRegions)
{
}

void
tu_CmdCopyImage(VkCommandBuffer commandBuffer,
                VkImage srcImage,
                VkImageLayout srcImageLayout,
                VkImage destImage,
                VkImageLayout destImageLayout,
                uint32_t regionCount,
                const VkImageCopy *pRegions)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmd_buffer, commandBuffer);
   TU_FROM_HANDLE(tu_image, src_image, srcImage);
   TU_FROM_HANDLE(tu_image, dest_image, destImage);

   meta_copy_image(cmd_buffer, src_image, srcImageLayout, dest_image,
                   destImageLayout, regionCount, pRegions);
}
