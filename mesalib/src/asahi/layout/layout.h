/*
 * Copyright (C) 2022 Alyssa Rosenzweig
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef __AIL_LAYOUT_H_
#define __AIL_LAYOUT_H_

#include "util/format/u_format.h"
#include "util/u_math.h"
#include "util/macros.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AIL_CACHELINE 0x80
#define AIL_PAGESIZE  0x4000
#define AIL_MAX_MIP_LEVELS 16

enum ail_tiling {
   /**
    * Strided linear (raster order). Only allowed for 1D or 2D, without
    * mipmapping, multisampling, block-compression, or arrays.
    */
   AIL_TILING_LINEAR,

   /**
    * Twiddled (Morton order). Always allowed.
    */
   AIL_TILING_TWIDDLED,
};

/*
 * Represents the dimensions of a single tile. Used to describe tiled layouts.
 * Width and height are in units of elements, not pixels, to model compressed
 * textures corrects.
 *
 * Invariant: width_el and height_el are powers of two.
 */
struct ail_tile {
   unsigned width_el, height_el;
};

/*
 * An AGX image layout.
 */
struct ail_layout {
   /** Width, height, and depth in pixels at level 0 */
   uint32_t width_px, height_px, depth_px;

   /** Number of miplevels. 1 if no mipmapping is used. */
   uint8_t levels;

   /** Tiling mode used */
   enum ail_tiling tiling;

   /** Texture format */
   enum pipe_format format;

   /**
    * If tiling is LINEAR, the number of bytes between adjacent rows of
    * elements. Otherwise, this field is zero.
    */
   uint32_t linear_stride_B;

   /**
    * Stride between layers of an array texture, including a cube map. Layer i
    * begins at offset (i * layer_stride_B) from the beginning of the texture.
    *
    * If depth_px = 1, the value of this field is UNDEFINED.
    */
   uint32_t layer_stride_B;

   /**
    * Offsets of mip levels within a layer.
    */
   uint32_t level_offsets_B[AIL_MAX_MIP_LEVELS];

   /**
    * If tiling is TWIDDLED, the tile size used for each mip level within a
    * layer. Calculating tile sizes is the sole responsibility of
    * ail_initialized_twiddled.
    */
   struct ail_tile tilesize_el[AIL_MAX_MIP_LEVELS];

   /* Size of entire texture */
   uint32_t size_B;
};

static inline uint32_t
ail_get_linear_stride_B(struct ail_layout *layout, ASSERTED uint8_t level)
{
   assert(layout->tiling == AIL_TILING_LINEAR && "Invalid usage");
   assert(level == 0 && "Strided linear mipmapped textures are unsupported");

   return layout->linear_stride_B;
}

static inline uint32_t
ail_get_layer_offset_B(struct ail_layout *layout, unsigned z_px)
{
   return z_px * layout->layer_stride_B;
}

static inline uint32_t
ail_get_level_offset_B(struct ail_layout *layout, unsigned level)
{
   return layout->level_offsets_B[level];
}

static inline uint32_t
ail_get_layer_level_B(struct ail_layout *layout, unsigned z_px, unsigned level)
{
   return ail_get_layer_offset_B(layout, z_px) +
          ail_get_level_offset_B(layout, level);
}

static inline uint32_t
ail_get_linear_pixel_B(struct ail_layout *layout, ASSERTED unsigned level,
                       uint32_t x_px, uint32_t y_px, uint32_t z_px)
{
   assert(level == 0 && "Strided linear mipmapped textures are unsupported");
   assert(z_px == 0 && "Strided linear 3D textures are unsupported");
   assert(util_format_get_blockwidth(layout->format) == 1 &&
         "Strided linear block formats unsupported");
   assert(util_format_get_blockheight(layout->format) == 1 &&
         "Strided linear block formats unsupported");

   return (y_px * ail_get_linear_stride_B(layout, level)) +
          (x_px * util_format_get_blocksize(layout->format));
}

void ail_make_miptree(struct ail_layout *layout);

void
ail_detile(void *_tiled, void *_linear,
           struct ail_layout *tiled_layout, unsigned level,
           unsigned linear_pitch_B, unsigned sx_px, unsigned sy_px,
           unsigned width_px, unsigned height_px);

void
ail_tile(void *_tiled, void *_linear,
         struct ail_layout *tiled_layout, unsigned level,
         unsigned linear_pitch_B,
         unsigned sx_px, unsigned sy_px, unsigned width_px, unsigned height_px);

#ifdef __cplusplus
} /* extern C */
#endif

#endif
