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

static void
tu_copy_buffer(struct tu_cmd_buffer *cmd,
               struct tu_buffer *src,
               struct tu_buffer *dst,
               const VkBufferCopy *region)
{
   tu_bo_list_add(&cmd->bo_list, src->bo, MSM_SUBMIT_BO_READ);
   tu_bo_list_add(&cmd->bo_list, dst->bo, MSM_SUBMIT_BO_WRITE);

   tu_blit(cmd, &(struct tu_blit) {
      .dst = {
         .fmt = VK_FORMAT_R8_UNORM,
         .va = tu_buffer_iova(dst) + region->dstOffset,
         .width = region->size,
         .height = 1,
         .samples = 1,
      },
      .src = {
         .fmt = VK_FORMAT_R8_UNORM,
         .va = tu_buffer_iova(src) + region->srcOffset,
         .width = region->size,
         .height = 1,
         .samples = 1,
      },
      .layers = 1,
      .type = TU_BLIT_COPY,
      .buffer = true,
   });
}

static struct tu_blit_surf
tu_blit_buffer(struct tu_buffer *buffer,
               VkFormat format,
               const VkBufferImageCopy *info)
{
   if (info->imageSubresource.aspectMask == VK_IMAGE_ASPECT_STENCIL_BIT)
      format = VK_FORMAT_R8_UNORM;

   unsigned pitch = (info->bufferRowLength ?: info->imageExtent.width) *
                        vk_format_get_blocksize(format);

   return (struct tu_blit_surf) {
      .fmt = format,
      .tile_mode = TILE6_LINEAR,
      .va = tu_buffer_iova(buffer) + info->bufferOffset,
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
      .type = TU_BLIT_COPY,
   });
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
      .type = TU_BLIT_COPY,
   });
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
      .type = TU_BLIT_COPY,
   });
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

   for (unsigned i = 0; i < regionCount; ++i)
      tu_copy_buffer(cmdbuf, src_buffer, dst_buffer, &pRegions[i]);
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
