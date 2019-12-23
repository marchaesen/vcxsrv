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
   uint64_t ubwc_va;
   uint32_t ubwc_pitch;
   uint32_t ubwc_size;
};

static inline struct tu_blit_surf
tu_blit_surf(struct tu_image *image,
             VkImageSubresourceLayers subres,
             const VkOffset3D *offsets)
{
   unsigned layer = subres.baseArrayLayer;
   if (image->type == VK_IMAGE_TYPE_3D) {
      assert(layer == 0);
      layer = MIN2(offsets[0].z, offsets[1].z);
   }

   return (struct tu_blit_surf) {
      .fmt = image->vk_format,
      .tile_mode = tu6_get_image_tile_mode(image, subres.mipLevel),
      .tiled = image->layout.tile_mode != TILE6_LINEAR,
      .va = tu_image_base(image, subres.mipLevel, layer),
      .pitch = tu_image_stride(image, subres.mipLevel),
      .layer_size = tu_layer_size(image, subres.mipLevel),
      .x = MIN2(offsets[0].x, offsets[1].x),
      .y = MIN2(offsets[0].y, offsets[1].y),
      .width = abs(offsets[1].x - offsets[0].x),
      .height = abs(offsets[1].y - offsets[0].y),
      .samples = image->samples,
      .ubwc_va = tu_image_ubwc_base(image, subres.mipLevel, layer),
      .ubwc_pitch = tu_image_ubwc_pitch(image, subres.mipLevel),
      .ubwc_size = tu_image_ubwc_size(image, subres.mipLevel),
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
tu_blit_surf_whole(struct tu_image *image, int level, int layer)
{
   return tu_blit_surf(image, (VkImageSubresourceLayers){
      .mipLevel = level,
      .baseArrayLayer = layer,
   }, (VkOffset3D[]) {
      {}, {
         u_minify(image->extent.width, level),
         u_minify(image->extent.height, level),
      }
   });
}

enum tu_blit_type {
   TU_BLIT_DEFAULT,
   TU_BLIT_COPY,
   TU_BLIT_CLEAR,
};

struct tu_blit {
   struct tu_blit_surf dst;
   struct tu_blit_surf src;
   uint32_t layers;
   bool filter;
   bool stencil_read;
   bool buffer; /* 1d copy/clear */
   enum a6xx_rotation rotation;
   uint32_t clear_value[4];
   enum tu_blit_type type;
};

void tu_blit(struct tu_cmd_buffer *cmdbuf, struct tu_blit *blt);

#endif /* TU_BLIT_H */
