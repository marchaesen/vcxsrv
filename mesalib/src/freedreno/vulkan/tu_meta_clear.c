/*
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
#include "tu_blit.h"

static void
clear_image(struct tu_cmd_buffer *cmdbuf,
            struct tu_image *image,
            uint32_t clear_value[4],
            const VkImageSubresourceRange *range)
{
   uint32_t level_count = tu_get_levelCount(image, range);
   uint32_t layer_count = tu_get_layerCount(image, range);

   if (image->type == VK_IMAGE_TYPE_3D) {
      assert(layer_count == 1);
      assert(range->baseArrayLayer == 0);
   }

   for (unsigned j = 0; j < level_count; j++) {
      if (image->type == VK_IMAGE_TYPE_3D)
         layer_count = u_minify(image->extent.depth, range->baseMipLevel + j);

      tu_blit(cmdbuf, &(struct tu_blit) {
         .dst = tu_blit_surf_whole(image, range->baseMipLevel + j, range->baseArrayLayer),
         .src = tu_blit_surf_whole(image, range->baseMipLevel + j, range->baseArrayLayer),
         .layers = layer_count,
         .clear_value = {clear_value[0], clear_value[1], clear_value[2], clear_value[3]},
         .type = TU_BLIT_CLEAR,
      });
   }
}

void
tu_CmdClearColorImage(VkCommandBuffer commandBuffer,
                      VkImage image_h,
                      VkImageLayout imageLayout,
                      const VkClearColorValue *pColor,
                      uint32_t rangeCount,
                      const VkImageSubresourceRange *pRanges)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   TU_FROM_HANDLE(tu_image, image, image_h);
   uint32_t clear_value[4] = {};

   tu_2d_clear_color(pColor, image->vk_format, clear_value);

   tu_bo_list_add(&cmdbuf->bo_list, image->bo, MSM_SUBMIT_BO_WRITE);

   for (unsigned i = 0; i < rangeCount; i++)
      clear_image(cmdbuf, image, clear_value, pRanges + i);
}

void
tu_CmdClearDepthStencilImage(VkCommandBuffer commandBuffer,
                             VkImage image_h,
                             VkImageLayout imageLayout,
                             const VkClearDepthStencilValue *pDepthStencil,
                             uint32_t rangeCount,
                             const VkImageSubresourceRange *pRanges)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   TU_FROM_HANDLE(tu_image, image, image_h);
   uint32_t clear_value[4] = {};

   tu_2d_clear_zs(pDepthStencil, image->vk_format, clear_value);

   tu_bo_list_add(&cmdbuf->bo_list, image->bo, MSM_SUBMIT_BO_WRITE);

   for (unsigned i = 0; i < rangeCount; i++)
      clear_image(cmdbuf, image, clear_value, pRanges + i);
}

void
tu_CmdClearAttachments(VkCommandBuffer commandBuffer,
                       uint32_t attachmentCount,
                       const VkClearAttachment *pAttachments,
                       uint32_t rectCount,
                       const VkClearRect *pRects)
{
   tu_finishme("CmdClearAttachments");
}
