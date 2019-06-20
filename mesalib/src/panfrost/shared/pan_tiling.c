/*
 * Copyright (c) 2011-2013 Luc Verhaegen <libv@skynet.be>
 * Copyright (c) 2018 Alyssa Rosenzweig <alyssa@rosenzweig.io>
 * Copyright (c) 2018 Vasily Khoruzhick <anarsoul@gmail.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 */

#include "pan_tiling.h"

uint32_t space_filler[16][16] = {
   { 0,   1,   4,   5,   16,  17,  20,  21,  64,  65,  68,  69,  80,  81,  84,  85, },
   { 3,   2,   7,   6,   19,  18,  23,  22,  67,  66,  71,  70,  83,  82,  87,  86, },
   { 12,  13,  8,   9,   28,  29,  24,  25,  76,  77,  72,  73,  92,  93,  88,  89, },
   { 15,  14,  11,  10,  31,  30,  27,  26,  79,  78,  75,  74,  95,  94,  91,  90, },
   { 48,  49,  52,  53,  32,  33,  36,  37,  112, 113, 116, 117, 96,  97,  100, 101, },
   { 51,  50,  55,  54,  35,  34,  39,  38,  115, 114, 119, 118, 99,  98,  103, 102, },
   { 60,  61,  56,  57,  44,  45,  40,  41,  124, 125, 120, 121, 108, 109, 104, 105, },
   { 63,  62,  59,  58,  47,  46,  43,  42,  127, 126, 123, 122, 111, 110, 107, 106, },
   { 192, 193, 196, 197, 208, 209, 212, 213, 128, 129, 132, 133, 144, 145, 148, 149, },
   { 195, 194, 199, 198, 211, 210, 215, 214, 131, 130, 135, 134, 147, 146, 151, 150, },
   { 204, 205, 200, 201, 220, 221, 216, 217, 140, 141, 136, 137, 156, 157, 152, 153, },
   { 207, 206, 203, 202, 223, 222, 219, 218, 143, 142, 139, 138, 159, 158, 155, 154, },
   { 240, 241, 244, 245, 224, 225, 228, 229, 176, 177, 180, 181, 160, 161, 164, 165, },
   { 243, 242, 247, 246, 227, 226, 231, 230, 179, 178, 183, 182, 163, 162, 167, 166, },
   { 252, 253, 248, 249, 236, 237, 232, 233, 188, 189, 184, 185, 172, 173, 168, 169, },
   { 255, 254, 251, 250, 239, 238, 235, 234, 191, 190, 187, 186, 175, 174, 171, 170, },
};

static void
panfrost_store_tiled_image_bpp4(void *dst, const void *src,
                               const struct pipe_box *box,
                               uint32_t dst_stride,
                               uint32_t src_stride)
{
   for (int y = box->y, src_y = 0; src_y < box->height; ++y, ++src_y) {
      int block_y = y & ~0x0f;
      int rem_y = y & 0x0F;
      int block_start_s = block_y * dst_stride;
      int source_start = src_y * src_stride;

      for (int x = box->x, src_x = 0; src_x < box->width; ++x, ++src_x) {
         int block_x_s = (x >> 4) * 256;
         int rem_x = x & 0x0F;

         int index = space_filler[rem_y][rem_x];
         const uint32_t *source = src + source_start + 4 * src_x;
         uint32_t *dest = dst + block_start_s + 4 * (block_x_s + index);

         *dest = *source;
      }
   }
}

static void
panfrost_store_tiled_image_generic(void *dst, const void *src,
                               const struct pipe_box *box,
                               uint32_t dst_stride,
                               uint32_t src_stride,
                               uint32_t bpp)
{
   for (int y = box->y, src_y = 0; src_y < box->height; ++y, ++src_y) {
      int block_y = y & ~0x0f;
      int rem_y = y & 0x0F;
      int block_start_s = block_y * dst_stride;
      int source_start = src_y * src_stride;

      for (int x = box->x, src_x = 0; src_x < box->width; ++x, ++src_x) {
         int block_x_s = (x >> 4) * 256;
         int rem_x = x & 0x0F;

         int index = space_filler[rem_y][rem_x];
         const uint8_t *src8 = src;
         const uint8_t *source = &src8[source_start + bpp * src_x];
         uint8_t *dest = dst + block_start_s + bpp * (block_x_s + index);

         for (int b = 0; b < bpp; ++b)
            dest[b] = source[b];
      }
   }
}

static void
panfrost_load_tiled_image_bpp4(void *dst, const void *src,
                              const struct pipe_box *box,
                              uint32_t dst_stride,
                              uint32_t src_stride)
{
   for (int y = box->y, dest_y = 0; dest_y < box->height; ++y, ++dest_y) {
      int block_y = y & ~0x0f;
      int rem_y = y & 0x0F;
      int block_start_s = block_y * src_stride;
      int dest_start = dest_y * dst_stride;

      for (int x = box->x, dest_x = 0; dest_x < box->width; ++x, ++dest_x) {
         int block_x_s = (x >> 4) * 256;
         int rem_x = x & 0x0F;

         int index = space_filler[rem_y][rem_x];
         uint32_t *dest = dst + dest_start + 4 * dest_x;
         const uint32_t *source = src + block_start_s + 4 * (block_x_s + index);

         *dest = *source;
      }
   }
}

static void
panfrost_load_tiled_image_generic(void *dst, const void *src,
                              const struct pipe_box *box,
                              uint32_t dst_stride,
                              uint32_t src_stride,
                              uint32_t bpp)
{
   for (int y = box->y, dest_y = 0; dest_y < box->height; ++y, ++dest_y) {
      int block_y = y & ~0x0f;
      int rem_y = y & 0x0F;
      int block_start_s = block_y * src_stride;
      int dest_start = dest_y * dst_stride;

      for (int x = box->x, dest_x = 0; dest_x < box->width; ++x, ++dest_x) {
         int block_x_s = (x >> 4) * 256;
         int rem_x = x & 0x0F;

         int index = space_filler[rem_y][rem_x];
         uint8_t *dst8 = dst;
         uint8_t *dest = &dst8[dest_start + bpp * dest_x];
         const uint8_t *source = src + block_start_s + bpp * (block_x_s + index);

         for (int b = 0; b < bpp; ++b)
            dest[b] = source[b];
      }
   }
}

void
panfrost_store_tiled_image(void *dst, const void *src,
                           const struct pipe_box *box,
                           uint32_t dst_stride,
                           uint32_t src_stride,
                           uint32_t bpp)
{
	switch (bpp) {
	case 4:
		panfrost_store_tiled_image_bpp4(dst, src, box, dst_stride, src_stride);
		break;
	default:
		panfrost_store_tiled_image_generic(dst, src, box, dst_stride, src_stride, bpp);
	}
}

void
panfrost_load_tiled_image(void *dst, const void *src,
                           const struct pipe_box *box,
                           uint32_t dst_stride,
                           uint32_t src_stride,
                           uint32_t bpp)
{
	switch (bpp) {
	case 4:
		panfrost_load_tiled_image_bpp4(dst, src, box, dst_stride, src_stride);
		break;
	default:
		panfrost_load_tiled_image_generic(dst, src, box, dst_stride, src_stride, bpp);
	}
}
