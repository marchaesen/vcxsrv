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

#ifndef TU_BLIT_H
#define TU_BLIT_H

#include "tu_private.h"

#include "vk_format.h"

struct tu_blit_surf {
   VkFormat fmt;
   enum a6xx_tile_mode tile_mode;
   bool tiled;
   uint64_t va;
   uint32_t pitch, layer_size;
   uint32_t x, y;
   uint32_t width, height;
   unsigned samples;
};

static inline struct tu_blit_surf
tu_blit_surf(struct tu_image *img,
             VkImageSubresourceLayers subres,
             const VkOffset3D *offsets)
{
   return (struct tu_blit_surf) {
      .fmt = img->vk_format,
      .tile_mode = tu6_get_image_tile_mode(img, subres.mipLevel),
      .tiled = img->tile_mode != TILE6_LINEAR,
      .va = img->bo->iova + img->bo_offset + img->levels[subres.mipLevel].offset +
            subres.baseArrayLayer * img->layer_size +
            MIN2(offsets[0].z, offsets[1].z) * img->levels[subres.mipLevel].size,
      .pitch = img->levels[subres.mipLevel].pitch * vk_format_get_blocksize(img->vk_format) * img->samples,
      .layer_size = img->type == VK_IMAGE_TYPE_3D ? img->levels[subres.mipLevel].size : img->layer_size,
      .x = MIN2(offsets[0].x, offsets[1].x),
      .y = MIN2(offsets[0].y, offsets[1].y),
      .width = abs(offsets[1].x - offsets[0].x),
      .height = abs(offsets[1].y - offsets[0].y),
      .samples = img->samples,
   };
}

static inline struct tu_blit_surf
tu_blit_surf_ext(struct tu_image *image,
                 VkImageSubresourceLayers subres,
                 VkOffset3D offset,
                 VkExtent3D extent)
{
   return tu_blit_surf(image, subres, (VkOffset3D[]) {
      offset, {.x = offset.x + extent.width,
               .y = offset.y + extent.height,
               .z = offset.z}
   });
}

static inline struct tu_blit_surf
tu_blit_surf_whole(struct tu_image *image)
{
   return tu_blit_surf(image, (VkImageSubresourceLayers){}, (VkOffset3D[]) {
      {}, {image->extent.width, image->extent.height}
   });
}

struct tu_blit {
   struct tu_blit_surf dst;
   struct tu_blit_surf src;
   uint32_t layers;
   bool filter;
   bool stencil_read;
   enum a6xx_rotation rotation;
};

void tu_blit(struct tu_cmd_buffer *cmdbuf, struct tu_blit *blt, bool copy);

#endif /* TU_BLIT_H */
