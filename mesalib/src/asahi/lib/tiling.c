/*
 * Copyright (C) 2021 Alyssa Rosenzweig <alyssa@rosenzweig.io>
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
 */

#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include "tiling.h"

/* Z-order with 64x64 tiles:
 *
 * 	[y5][x5][y4][x4][y3][x3][y2][x2][y1][x1][y0][x0]
 *
 * Efficient tiling algorithm described in
 * https://fgiesen.wordpress.com/2011/01/17/texture-tiling-and-swizzling/ but
 * for posterity, we split into X and Y parts, and are faced with the problem
 * of incrementing:
 *
 * 	0 [x5] 0 [x4] 0 [x3] 0 [x2] 0 [x1] 0 [x0]
 *
 * To do so, we fill in the "holes" with 1's by adding the bitwise inverse of
 * the mask of bits we care about
 *
 * 	0 [x5] 0 [x4] 0 [x3] 0 [x2] 0 [x1] 0 [x0]
 *    + 1  0   1  0   1  0   1  0   1  0   1  0
 *    ------------------------------------------
 * 	1 [x5] 1 [x4] 1 [x3] 1 [x2] 1 [x1] 1 [x0]
 *
 * Then when we add one, the holes are passed over by forcing carry bits high.
 * Finally, we need to zero out the holes, by ANDing with the mask of bits we
 * care about. In total, we get the expression (X + ~mask + 1) & mask, and
 * applying the two's complement identity, we are left with (X - mask) & mask
 */

#define TILE_WIDTH 64
#define TILE_HEIGHT 64
#define TILE_SHIFT 6
#define TILE_MASK ((1 << TILE_SHIFT) - 1)

/* mask of bits used for X coordinate in a tile */
#define SPACE_MASK 0x555 // 0b010101010101

#define MAX2(x, y) (((x) > (y)) ? (x) : (y))
#define MIN2(x, y) (((x) < (y)) ? (x) : (y))

static uint32_t
agx_space_bits(unsigned x)
{
   assert(x < TILE_WIDTH);
   return ((x & 1) << 0) | ((x & 2) << 1) | ((x & 4) << 2) |
          ((x & 8) << 3) | ((x & 16) << 4) | ((x & 32) << 5);
}

#define TILED_UNALIGNED_TYPE(pixel_t, is_store) { \
	unsigned tiles_per_row = (width + TILE_WIDTH - 1) >> TILE_SHIFT;\
	unsigned y_offs = agx_space_bits(sy & TILE_MASK) << 1;\
	unsigned x_offs_start = agx_space_bits(sx & TILE_MASK);\
\
	for (unsigned y = sy; y < smaxy; ++y) {\
		unsigned tile_y = (y >> TILE_SHIFT);\
		unsigned tile_row = tile_y * tiles_per_row;\
		unsigned x_offs = x_offs_start;\
\
		pixel_t *linear_row = linear;\
		\
		for (unsigned x = sx; x < smaxx; ++x) {\
			unsigned tile_x = (x >> TILE_SHIFT);\
			unsigned tile_idx = (tile_row + tile_x);\
			unsigned tile_base = tile_idx * (TILE_WIDTH * TILE_HEIGHT);\
\
			pixel_t *ptiled = &tiled[tile_base + y_offs + x_offs];\
			pixel_t *plinear = (linear_row++);\
			pixel_t *outp = (pixel_t *) (is_store ? ptiled : plinear); \
			pixel_t *inp = (pixel_t *) (is_store ? plinear : ptiled); \
			*outp = *inp;\
			x_offs = (x_offs - SPACE_MASK) & SPACE_MASK;\
		}\
\
		y_offs = (((y_offs >> 1) - SPACE_MASK) & SPACE_MASK) << 1;\
		linear += linear_pitch;\
	}\
}

void
agx_detile(void *_tiled, void *_linear,
           unsigned width, unsigned bpp, unsigned linear_pitch,
           unsigned sx, unsigned sy, unsigned smaxx, unsigned smaxy)
{
   /* TODO: parametrize with macro magic */
   assert(bpp == 32);

   uint32_t *linear = _linear;
   uint32_t *tiled = _tiled;
   TILED_UNALIGNED_TYPE(uint32_t, false);
}

void
agx_tile(void *_tiled, void *_linear,
         unsigned width, unsigned bpp, unsigned linear_pitch,
         unsigned sx, unsigned sy, unsigned smaxx, unsigned smaxy)
{
   /* TODO: parametrize with macro magic */
   assert(bpp == 32);

   uint32_t *linear = _linear;
   uint32_t *tiled = _tiled;
   TILED_UNALIGNED_TYPE(uint32_t, true);
}
