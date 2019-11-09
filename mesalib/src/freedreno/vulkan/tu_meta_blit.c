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
tu_blit_image(struct tu_cmd_buffer *cmdbuf,
              struct tu_image *src_image,
              struct tu_image *dst_image,
              const VkImageBlit *info,
              VkFilter filter)
{
   static const enum a6xx_rotation rotate[2][2] = {
      {ROTATE_0, ROTATE_HFLIP},
      {ROTATE_VFLIP, ROTATE_180},
   };
   bool mirror_x = (info->srcOffsets[1].x < info->srcOffsets[0].x) !=
                   (info->dstOffsets[1].x < info->dstOffsets[0].x);
   bool mirror_y = (info->srcOffsets[1].y < info->srcOffsets[0].y) !=
                   (info->dstOffsets[1].y < info->dstOffsets[0].y);
   bool mirror_z = (info->srcOffsets[1].z < info->srcOffsets[0].z) !=
                   (info->dstOffsets[1].z < info->dstOffsets[0].z);

   if (mirror_z) {
      tu_finishme("blit z mirror\n");
      return;
   }

   if (info->srcOffsets[1].z - info->srcOffsets[0].z !=
       info->dstOffsets[1].z - info->dstOffsets[0].z) {
      tu_finishme("blit z filter\n");
      return;
   }
   assert(info->dstSubresource.layerCount == info->srcSubresource.layerCount);

   struct tu_blit blt = {
      .dst = tu_blit_surf(dst_image, info->dstSubresource, info->dstOffsets),
      .src = tu_blit_surf(src_image, info->srcSubresource, info->srcOffsets),
      .layers = MAX2(info->srcOffsets[1].z - info->srcOffsets[0].z,
                     info->dstSubresource.layerCount),
      .filter = filter == VK_FILTER_LINEAR,
      .rotation = rotate[mirror_y][mirror_x],
   };

   tu_blit(cmdbuf, &blt, false);
}

void
tu_CmdBlitImage(VkCommandBuffer commandBuffer,
                VkImage srcImage,
                VkImageLayout srcImageLayout,
                VkImage destImage,
                VkImageLayout destImageLayout,
                uint32_t regionCount,
                const VkImageBlit *pRegions,
                VkFilter filter)

{
   TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, commandBuffer);
   TU_FROM_HANDLE(tu_image, src_image, srcImage);
   TU_FROM_HANDLE(tu_image, dst_image, destImage);

   tu_bo_list_add(&cmdbuf->bo_list, src_image->bo, MSM_SUBMIT_BO_READ);
   tu_bo_list_add(&cmdbuf->bo_list, dst_image->bo, MSM_SUBMIT_BO_WRITE);

   for (uint32_t i = 0; i < regionCount; ++i) {
      tu_blit_image(cmdbuf, src_image, dst_image, pRegions + i, filter);
   }
}
