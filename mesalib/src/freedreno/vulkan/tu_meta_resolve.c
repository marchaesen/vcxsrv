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

#include <assert.h>
#include <stdbool.h>

#include "nir/nir_builder.h"
#include "vk_format.h"

#include "tu_blit.h"

static void
tu_resolve_image(struct tu_cmd_buffer *cmdbuf,
                 struct tu_image *src_image,
                 struct tu_image *dst_image,
                 const VkImageResolve *info)
{
   assert(info->dstSubresource.layerCount == info->srcSubresource.layerCount);

   tu_blit(cmdbuf, &(struct tu_blit) {
      .dst = tu_blit_surf_ext(dst_image, info->dstSubresource, info->dstOffset, info->extent),
      .src = tu_blit_surf_ext(src_image, info->srcSubresource, info->srcOffset, info->extent),
      .layers = MAX2(info->extent.depth, info->dstSubresource.layerCount)
   }, false);
}

void
tu_CmdResolveImage(VkCommandBuffer cmd_buffer_h,
                   VkImage src_image_h,
                   VkImageLayout src_image_layout,
                   VkImage dest_image_h,
                   VkImageLayout dest_image_layout,
                   uint32_t region_count,
                   const VkImageResolve *regions)
{
   TU_FROM_HANDLE(tu_cmd_buffer, cmdbuf, cmd_buffer_h);
   TU_FROM_HANDLE(tu_image, src_image, src_image_h);
   TU_FROM_HANDLE(tu_image, dst_image, dest_image_h);

   tu_bo_list_add(&cmdbuf->bo_list, src_image->bo, MSM_SUBMIT_BO_READ);
   tu_bo_list_add(&cmdbuf->bo_list, dst_image->bo, MSM_SUBMIT_BO_WRITE);

   for (uint32_t i = 0; i < region_count; ++i)
      tu_resolve_image(cmdbuf, src_image, dst_image, regions + i);
}
