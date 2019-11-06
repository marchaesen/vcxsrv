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
#include "tu_blit.h"

static uint32_t
blit_control(enum a6xx_color_fmt fmt)
{
   unsigned blit_cntl = 0xf00000;
   blit_cntl |= A6XX_RB_2D_BLIT_CNTL_COLOR_FORMAT(fmt);
   blit_cntl |= A6XX_RB_2D_BLIT_CNTL_IFMT(tu6_rb_fmt_to_ifmt(fmt));
   return blit_cntl;
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
   tu_cs_emit(&cmdbuf->cs, A6XX_CP_SET_MARKER_0_MODE(RM6_BLIT2DSCALE));

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

static struct tu_blit_surf
tu_blit_buffer(struct tu_buffer *buffer,
               VkFormat format,
               const VkBufferImageCopy *info)
{
   if (info->imageSubresource.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT)
      format = VK_FORMAT_R8_UINT;

   unsigned pitch = (info->bufferRowLength ?: info->imageExtent.width) *
                        vk_format_get_blocksize(format);

   return (struct tu_blit_surf) {
      .fmt = format,
      .tile_mode = TILE6_LINEAR,
      .va = buffer->bo->iova + buffer->bo_offset + info->bufferOffset,
      .pitch = pitch,
      .layer_size = (info->bufferImageHeight ?: info->imageExtent.height) * pitch / vk_format_get_blockwidth(format) / vk_format_get_blockheight(format),
      .width = info->imageExtent.width,
      .height = info->imageExtent.height,
      .samples = 1,
   };
}

static void
tu_copy_buffer_to_image(struct tu_cmd_buffer *cmdbuf,
                        struct tu_buffer *src_buffer,
                        struct tu_image *dst_image,
                        const VkBufferImageCopy *info)
{
   if (info->imageSubresource.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT &&
       vk_format_get_blocksize(dst_image->vk_format) == 4) {
      tu_finishme("aspect mask\n");
      return;
   }

   tu_blit(cmdbuf, &(struct tu_blit) {
      .dst = tu_blit_surf_ext(dst_image, info->imageSubresource, info->imageOffset, info->imageExtent),
      .src = tu_blit_buffer(src_buffer, dst_image->vk_format, info),
      .layers = MAX2(info->imageExtent.depth, info->imageSubresource.layerCount),
   }, true);
}

static void
tu_copy_image_to_buffer(struct tu_cmd_buffer *cmdbuf,
                        struct tu_image *src_image,
                        struct tu_buffer *dst_buffer,
                        const VkBufferImageCopy *info)
{
   tu_blit(cmdbuf, &(struct tu_blit) {
      .dst = tu_blit_buffer(dst_buffer, src_image->vk_format, info),
      .src = tu_blit_surf_ext(src_image, info->imageSubresource, info->imageOffset, info->imageExtent),
      .layers = MAX2(info->imageExtent.depth, info->imageSubresource.layerCount),
   }, true);
}

static void
tu_copy_image_to_image(struct tu_cmd_buffer *cmdbuf,
                       struct tu_image *src_image,
                       struct tu_image *dst_image,
                       const VkImageCopy *info)
{
   if ((info->dstSubresource.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT &&
        vk_format_get_blocksize(dst_image->vk_format) == 4) ||
       (info->srcSubresource.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT &&
        vk_format_get_blocksize(src_image->vk_format) == 4)) {
      tu_finishme("aspect mask\n");
      return;
   }

   tu_blit(cmdbuf, &(struct tu_blit) {
      .dst = tu_blit_surf_ext(dst_image, info->dstSubresource, info->dstOffset, info->extent),
      .src = tu_blit_surf_ext(src_image, info->srcSubresource, info->srcOffset, info->extent),
      .layers = info->extent.depth,
   }, true);
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
   TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   TU_FROM_HANDLE(tu_image, dst_image, destImage);
   TU_FROM_HANDLE(tu_buffer, src_buffer, srcBuffer);

   tu_bo_list_add(&cmdbuf->bo_list, src_buffer->bo, MSM_SUBMIT_BO_READ);
   tu_bo_list_add(&cmdbuf->bo_list, dst_image->bo, MSM_SUBMIT_BO_WRITE);

   for (unsigned i = 0; i < regionCount; ++i)
      tu_copy_buffer_to_image(cmdbuf, src_buffer, dst_image, pRegions + i);
}

void
tu_CmdCopyImageToBuffer(VkCommandBuffer commandBuffer,
                        VkImage srcImage,
                        VkImageLayout srcImageLayout,
                        VkBuffer destBuffer,
                        uint32_t regionCount,
                        const VkBufferImageCopy *pRegions)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   TU_FROM_HANDLE(tu_image, src_image, srcImage);
   TU_FROM_HANDLE(tu_buffer, dst_buffer, destBuffer);

   tu_bo_list_add(&cmdbuf->bo_list, src_image->bo, MSM_SUBMIT_BO_READ);
   tu_bo_list_add(&cmdbuf->bo_list, dst_buffer->bo, MSM_SUBMIT_BO_WRITE);

   for (unsigned i = 0; i < regionCount; ++i)
      tu_copy_image_to_buffer(cmdbuf, src_image, dst_buffer, pRegions + i);
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
   TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   TU_FROM_HANDLE(tu_image, src_image, srcImage);
   TU_FROM_HANDLE(tu_image, dst_image, destImage);

   tu_bo_list_add(&cmdbuf->bo_list, src_image->bo, MSM_SUBMIT_BO_READ);
   tu_bo_list_add(&cmdbuf->bo_list, dst_image->bo, MSM_SUBMIT_BO_WRITE);

   for (uint32_t i = 0; i < regionCount; ++i)
      tu_copy_image_to_image(cmdbuf, src_image, dst_image, pRegions + i);
}
