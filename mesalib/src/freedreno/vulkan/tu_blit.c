/*
 * Copyright © 2019 Valve Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Jonathan Marek <jonathan@marek.ca>
 *
 */

#include "tu_blit.h"

#include "a6xx.xml.h"
#include "adreno_common.xml.h"
#include "adreno_pm4.xml.h"

#include "vk_format.h"

#include "tu_cs.h"

/* TODO:
 *   - Avoid disabling tiling for swapped formats
 *     (image_to_image copy doesn't deal with it)
 *   - Fix d24_unorm_s8_uint support & aspects
 *   - UBWC
 */

static VkFormat
blit_copy_format(VkFormat format)
{
   switch (vk_format_get_blocksizebits(format)) {
   case 8:  return VK_FORMAT_R8_UINT;
   case 16: return VK_FORMAT_R16_UINT;
   case 32: return VK_FORMAT_R8G8B8A8_UINT;
   case 64: return VK_FORMAT_R32G32_UINT;
   case 96: return VK_FORMAT_R32G32B32_UINT;
   case 128:return VK_FORMAT_R32G32B32A32_UINT;
   default:
      unreachable("unhandled format size");
   }
}

static uint32_t
blit_image_info(const struct tu_blit_surf *img, bool src, bool stencil_read)
{
   const struct tu_native_format *fmt = tu6_get_native_format(img->fmt);
   enum a6xx_color_fmt rb = fmt->rb;
   enum a3xx_color_swap swap = img->tiled ? WZYX : fmt->swap;
   if (rb == RB6_R10G10B10A2_UNORM && src)
      rb = RB6_R10G10B10A2_FLOAT16;
   if (rb == RB6_X8Z24_UNORM)
      rb = RB6_Z24_UNORM_S8_UINT;

   if (stencil_read)
      swap = XYZW;

   return A6XX_SP_PS_2D_SRC_INFO_COLOR_FORMAT(rb) |
          A6XX_SP_PS_2D_SRC_INFO_TILE_MODE(img->tile_mode) |
          A6XX_SP_PS_2D_SRC_INFO_COLOR_SWAP(swap) |
          COND(vk_format_is_srgb(img->fmt), A6XX_SP_PS_2D_SRC_INFO_SRGB);
}

static void
emit_blit_step(struct tu_cmd_buffer *cmdbuf, const struct tu_blit *blt)
{
   struct tu_cs *cs = &cmdbuf->cs;

   tu_cs_reserve_space(cmdbuf->device, cs, 52);

   enum a6xx_color_fmt fmt = tu6_get_native_format(blt->dst.fmt)->rb;
   if (fmt == RB6_X8Z24_UNORM)
      fmt = RB6_Z24_UNORM_S8_UINT;

   enum a6xx_2d_ifmt ifmt = tu6_rb_fmt_to_ifmt(fmt);

   if (vk_format_is_srgb(blt->dst.fmt)) {
      assert(ifmt == R2D_UNORM8);
      ifmt = R2D_UNORM8_SRGB;
   }

   uint32_t blit_cntl = A6XX_RB_2D_BLIT_CNTL_ROTATE(blt->rotation) |
                        A6XX_RB_2D_BLIT_CNTL_COLOR_FORMAT(fmt) | /* not required? */
                        COND(fmt == RB6_Z24_UNORM_S8_UINT, A6XX_RB_2D_BLIT_CNTL_D24S8) |
                        A6XX_RB_2D_BLIT_CNTL_MASK(0xf) |
                        A6XX_RB_2D_BLIT_CNTL_IFMT(ifmt);

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_RB_2D_BLIT_CNTL, 1);
   tu_cs_emit(&cmdbuf->cs, blit_cntl);

   tu_cs_emit_pkt4(&cmdbuf->cs, REG_A6XX_GRAS_2D_BLIT_CNTL, 1);
   tu_cs_emit(&cmdbuf->cs, blit_cntl);

   /*
    * Emit source:
    */
   tu_cs_emit_pkt4(cs, REG_A6XX_SP_PS_2D_SRC_INFO, 10);
   tu_cs_emit(cs, blit_image_info(&blt->src, true, blt->stencil_read) |
                  A6XX_SP_PS_2D_SRC_INFO_SAMPLES(tu_msaa_samples(blt->src.samples)) |
                  /* TODO: should disable this bit for integer formats ? */
                  COND(blt->src.samples > 1, A6XX_SP_PS_2D_SRC_INFO_SAMPLES_AVERAGE) |
                  COND(blt->filter, A6XX_SP_PS_2D_SRC_INFO_FILTER) |
                  0x500000);
   tu_cs_emit(cs, A6XX_SP_PS_2D_SRC_SIZE_WIDTH(blt->src.x + blt->src.width) |
                  A6XX_SP_PS_2D_SRC_SIZE_HEIGHT(blt->src.y + blt->src.height));
   tu_cs_emit_qw(cs, blt->src.va);
   tu_cs_emit(cs, A6XX_SP_PS_2D_SRC_PITCH_PITCH(blt->src.pitch));

   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);

   /*
    * Emit destination:
    */
   tu_cs_emit_pkt4(cs, REG_A6XX_RB_2D_DST_INFO, 9);
   tu_cs_emit(cs, blit_image_info(&blt->dst, false, false));
   tu_cs_emit_qw(cs, blt->dst.va);
   tu_cs_emit(cs, A6XX_RB_2D_DST_SIZE_PITCH(blt->dst.pitch));
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);
   tu_cs_emit(cs, 0x00000000);

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_2D_SRC_TL_X, 4);
   tu_cs_emit(cs, A6XX_GRAS_2D_SRC_TL_X_X(blt->src.x));
   tu_cs_emit(cs, A6XX_GRAS_2D_SRC_BR_X_X(blt->src.x + blt->src.width - 1));
   tu_cs_emit(cs, A6XX_GRAS_2D_SRC_TL_Y_Y(blt->src.y));
   tu_cs_emit(cs, A6XX_GRAS_2D_SRC_BR_Y_Y(blt->src.y + blt->src.height - 1));

   tu_cs_emit_pkt4(cs, REG_A6XX_GRAS_2D_DST_TL, 2);
   tu_cs_emit(cs, A6XX_GRAS_2D_DST_TL_X(blt->dst.x) |
                  A6XX_GRAS_2D_DST_TL_Y(blt->dst.y));
   tu_cs_emit(cs, A6XX_GRAS_2D_DST_BR_X(blt->dst.x + blt->dst.width - 1) |
                  A6XX_GRAS_2D_DST_BR_Y(blt->dst.y + blt->dst.height - 1));

   tu_cs_emit_pkt7(cs, CP_EVENT_WRITE, 1);
   tu_cs_emit(cs, 0x3f);
   tu_cs_emit_wfi(cs);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_UNKNOWN_8C01, 1);
   tu_cs_emit(cs, 0);

   if (fmt == RB6_R10G10B10A2_UNORM)
      fmt = RB6_R16G16B16A16_FLOAT;

   tu_cs_emit_pkt4(cs, REG_A6XX_SP_2D_SRC_FORMAT, 1);
   tu_cs_emit(cs, COND(vk_format_is_sint(blt->src.fmt), A6XX_SP_2D_SRC_FORMAT_SINT) |
                  COND(vk_format_is_uint(blt->src.fmt), A6XX_SP_2D_SRC_FORMAT_UINT) |
                  A6XX_SP_2D_SRC_FORMAT_COLOR_FORMAT(fmt) |
                  COND(ifmt == R2D_UNORM8_SRGB, A6XX_SP_2D_SRC_FORMAT_SRGB) |
                  A6XX_SP_2D_SRC_FORMAT_MASK(0xf));

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_UNKNOWN_8E04, 1);
   tu_cs_emit(cs, 0x01000000);

   tu_cs_emit_pkt7(cs, CP_BLIT, 1);
   tu_cs_emit(cs, CP_BLIT_0_OP(BLIT_OP_SCALE));

   tu_cs_emit_wfi(cs);

   tu_cs_emit_pkt4(cs, REG_A6XX_RB_UNKNOWN_8E04, 1);
   tu_cs_emit(cs, 0);
}

void tu_blit(struct tu_cmd_buffer *cmdbuf, struct tu_blit *blt, bool copy)
{
   if (copy) {
      blt->stencil_read =
         blt->dst.fmt == VK_FORMAT_R8_UINT &&
         blt->src.fmt == VK_FORMAT_D24_UNORM_S8_UINT;

      assert(vk_format_get_blocksize(blt->dst.fmt) ==
             vk_format_get_blocksize(blt->src.fmt) || blt->stencil_read);
      assert(blt->src.samples == blt->dst.samples);

      if (vk_format_is_compressed(blt->src.fmt)) {
         unsigned block_width = vk_format_get_blockwidth(blt->src.fmt);
         unsigned block_height = vk_format_get_blockheight(blt->src.fmt);

         blt->src.pitch /= block_width;
         blt->src.x /= block_width;
         blt->src.y /= block_height;

         /* for image_to_image copy, width/height is on the src format */
         blt->dst.width = blt->src.width = DIV_ROUND_UP(blt->src.width, block_width);
         blt->dst.height = blt->src.height = DIV_ROUND_UP(blt->src.height, block_height);
      }

      if (vk_format_is_compressed(blt->dst.fmt)) {
         unsigned block_width = vk_format_get_blockwidth(blt->dst.fmt);
         unsigned block_height = vk_format_get_blockheight(blt->dst.fmt);

         blt->dst.pitch /= block_width;
         blt->dst.x /= block_width;
         blt->dst.y /= block_height;
      }

      blt->src.fmt = blit_copy_format(blt->src.fmt);
      blt->dst.fmt = blit_copy_format(blt->dst.fmt);

      /* TODO: does this work correctly with tiling/etc ? */
      blt->src.x *= blt->src.samples;
      blt->dst.x *= blt->dst.samples;
      blt->src.width *= blt->src.samples;
      blt->dst.width *= blt->dst.samples;
      blt->src.samples = 1;
      blt->dst.samples = 1;
   } else {
      assert(blt->dst.samples == 1);
   }

   tu_cs_reserve_space(cmdbuf->device, &cmdbuf->cs, 18);

   tu6_emit_event_write(cmdbuf, &cmdbuf->cs, LRZ_FLUSH, false);
   tu6_emit_event_write(cmdbuf, &cmdbuf->cs, 0x1d, true);
   tu6_emit_event_write(cmdbuf, &cmdbuf->cs, FACENESS_FLUSH, true);
   tu6_emit_event_write(cmdbuf, &cmdbuf->cs, PC_CCU_INVALIDATE_COLOR, false);
   tu6_emit_event_write(cmdbuf, &cmdbuf->cs, PC_CCU_INVALIDATE_DEPTH, false);

   /* buffer copy setup */
   tu_cs_emit_pkt7(&cmdbuf->cs, CP_SET_MARKER, 1);
   tu_cs_emit(&cmdbuf->cs, A6XX_CP_SET_MARKER_0_MODE(RM6_BLIT2DSCALE));

   for (unsigned layer = 0; layer < blt->layers; layer++) {
      if ((blt->src.va & 63) || (blt->src.pitch & 63)) {
         /* per line copy path (buffer_to_image) */
         assert(copy && !blt->src.tiled);
         struct tu_blit line_blt = *blt;
         uint64_t src_va = line_blt.src.va + blt->src.pitch * blt->src.y;

         line_blt.src.y = 0;
         line_blt.src.pitch = 0;
         line_blt.src.height = 1;
         line_blt.dst.height = 1;

         for (unsigned y = 0; y < blt->src.height; y++) {
            line_blt.src.x = blt->src.x + (src_va & 63) / vk_format_get_blocksize(blt->src.fmt);
            line_blt.src.va = src_va & ~63;

            emit_blit_step(cmdbuf, &line_blt);

            line_blt.dst.y++;
            src_va += blt->src.pitch;
         }
      } else if ((blt->dst.va & 63) || (blt->dst.pitch & 63)) {
         /* per line copy path (image_to_buffer) */
         assert(copy && !blt->dst.tiled);
         struct tu_blit line_blt = *blt;
         uint64_t dst_va = line_blt.dst.va + blt->dst.pitch * blt->dst.y;

         line_blt.dst.y = 0;
         line_blt.dst.pitch = 0;
         line_blt.src.height = 1;
         line_blt.dst.height = 1;

         for (unsigned y = 0; y < blt->src.height; y++) {
            line_blt.dst.x = blt->dst.x + (dst_va & 63) / vk_format_get_blocksize(blt->dst.fmt);
            line_blt.dst.va = dst_va & ~63;

            emit_blit_step(cmdbuf, &line_blt);

            line_blt.src.y++;
            dst_va += blt->dst.pitch;
         }
      } else {
         emit_blit_step(cmdbuf, blt);
      }
      blt->dst.va += blt->dst.layer_size;
      blt->src.va += blt->src.layer_size;
   }

   tu_cs_reserve_space(cmdbuf->device, &cmdbuf->cs, 17);

   tu6_emit_event_write(cmdbuf, &cmdbuf->cs, 0x1d, true);
   tu6_emit_event_write(cmdbuf, &cmdbuf->cs, FACENESS_FLUSH, true);
   tu6_emit_event_write(cmdbuf, &cmdbuf->cs, CACHE_FLUSH_TS, true);
   tu6_emit_event_write(cmdbuf, &cmdbuf->cs, CACHE_INVALIDATE, false);
}
