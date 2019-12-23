/*
 * Copyright (C) 2018 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018-2019 Google, Inc.
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
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include <stdio.h>

#include "freedreno_layout.h"

/* indexed by cpp, including msaa 2x and 4x:
 * TODO:
 * cpp=1 UBWC needs testing at larger texture sizes
 * missing UBWC blockwidth/blockheight for npot+64 cpp
 * missing 96/128 CPP for 8x MSAA with 32_32_32/32_32_32_32
 */
static const struct {
	unsigned pitchalign;
	unsigned heightalign;
	uint8_t ubwc_blockwidth;
	uint8_t ubwc_blockheight;
} tile_alignment[] = {
	[1]  = { 128, 32, 16, 4 },
	[2]  = { 128, 16, 16, 4 },
	[3]  = {  64, 32 },
	[4]  = {  64, 16, 16, 4 },
	[6]  = {  64, 16 },
	[8]  = {  64, 16, 8, 4, },
	[12] = {  64, 16 },
	[16] = {  64, 16, 4, 4, },
	[24] = {  64, 16 },
	[32] = {  64, 16, 4, 2 },
	[48] = {  64, 16 },
	[64] = {  64, 16 },

	/* special cases for r8g8: */
	[0]  = {  64, 32, 16, 4 },
};

#define RGB_TILE_WIDTH_ALIGNMENT 64
#define RGB_TILE_HEIGHT_ALIGNMENT 16
#define UBWC_PLANE_SIZE_ALIGNMENT 4096

/* NOTE: good way to test this is:  (for example)
 *  piglit/bin/texelFetch fs sampler3D 100x100x8
 */
void
fdl6_layout(struct fdl_layout *layout,
		enum pipe_format format, uint32_t nr_samples,
		uint32_t width0, uint32_t height0, uint32_t depth0,
		uint32_t mip_levels, uint32_t array_size, bool is_3d, bool ubwc)
{
	assert(nr_samples > 0);
	layout->width0 = width0;
	layout->height0 = height0;
	layout->depth0 = depth0;

	layout->cpp = util_format_get_blocksize(format);
	layout->cpp *= nr_samples;

	const struct util_format_description *format_desc =
		util_format_description(format);
	uint32_t depth = depth0;
	/* linear dimensions: */
	uint32_t lwidth = width0;
	uint32_t lheight = height0;
	/* tile_mode dimensions: */
	uint32_t twidth = util_next_power_of_two(lwidth);
	uint32_t theight = util_next_power_of_two(lheight);
	int ta = layout->cpp;

	/* The z16/r16 formats seem to not play by the normal tiling rules: */
	if ((layout->cpp == 2) && (util_format_get_nr_components(format) == 2))
		ta = 0;

	uint32_t alignment;
	if (is_3d) {
		layout->layer_first = false;
		alignment = 4096;
	} else {
		layout->layer_first = true;
		alignment = 1;
	}
	/* in layer_first layout, the level (slice) contains just one
	 * layer (since in fact the layer contains the slices)
	 */
	uint32_t layers_in_level = layout->layer_first ? 1 : array_size;

	debug_assert(ta < ARRAY_SIZE(tile_alignment));
	debug_assert(tile_alignment[ta].pitchalign);

	for (uint32_t level = 0; level < mip_levels; level++) {
		struct fdl_slice *slice = &layout->slices[level];
		struct fdl_slice *ubwc_slice = &layout->ubwc_slices[level];
		uint32_t tile_mode = (ubwc ?
				layout->tile_mode : fdl_tile_mode(layout, level));
		uint32_t width, height;

		/* tiled levels of 3D textures are rounded up to PoT dimensions: */
		if (is_3d && tile_mode) {
			width = twidth;
			height = theight;
		} else {
			width = lwidth;
			height = lheight;
		}
		uint32_t aligned_height = height;
		uint32_t pitchalign;

		if (tile_mode) {
			pitchalign = tile_alignment[ta].pitchalign;
			aligned_height = align(aligned_height,
					tile_alignment[ta].heightalign);
		} else {
			pitchalign = 64;
		}

		/* The blits used for mem<->gmem work at a granularity of
		 * 32x32, which can cause faults due to over-fetch on the
		 * last level.  The simple solution is to over-allocate a
		 * bit the last level to ensure any over-fetch is harmless.
		 * The pitch is already sufficiently aligned, but height
		 * may not be:
		 */
		if (level == mip_levels - 1)
			aligned_height = align(aligned_height, 32);

		if (format_desc->layout == UTIL_FORMAT_LAYOUT_ASTC)
			slice->pitch =
				util_align_npot(width, pitchalign * util_format_get_blockwidth(format));
		else
			slice->pitch = align(width, pitchalign);

		slice->offset = layout->size;
		uint32_t blocks = util_format_get_nblocks(format,
				slice->pitch, aligned_height);

		/* 1d array and 2d array textures must all have the same layer size
		 * for each miplevel on a6xx. 3d textures can have different layer
		 * sizes for high levels, but the hw auto-sizer is buggy (or at least
		 * different than what this code does), so as soon as the layer size
		 * range gets into range, we stop reducing it.
		 */
		if (is_3d) {
			if (level < 1 || layout->slices[level - 1].size0 > 0xf000) {
				slice->size0 = align(blocks * layout->cpp, alignment);
			} else {
				slice->size0 = layout->slices[level - 1].size0;
			}
		} else {
			slice->size0 = align(blocks * layout->cpp, alignment);
		}

		layout->size += slice->size0 * depth * layers_in_level;

		if (ubwc) {
			/* with UBWC every level is aligned to 4K */
			layout->size = align(layout->size, 4096);

			uint32_t block_width = tile_alignment[ta].ubwc_blockwidth;
			uint32_t block_height = tile_alignment[ta].ubwc_blockheight;
			uint32_t meta_pitch = align(DIV_ROUND_UP(width, block_width),
					RGB_TILE_WIDTH_ALIGNMENT);
			uint32_t meta_height = align(DIV_ROUND_UP(height, block_height),
					RGB_TILE_HEIGHT_ALIGNMENT);

			/* it looks like mipmaps need alignment to power of two
			 * TODO: needs testing with large npot textures
			 * (needed for the first level?)
			 */
			if (mip_levels > 1) {
				meta_pitch = util_next_power_of_two(meta_pitch);
				meta_height = util_next_power_of_two(meta_height);
			}

			ubwc_slice->size0 = align(meta_pitch * meta_height, UBWC_PLANE_SIZE_ALIGNMENT);
			ubwc_slice->pitch = meta_pitch;
			ubwc_slice->offset = layout->ubwc_size;
			layout->ubwc_size += ubwc_slice->size0;
		}

		depth = u_minify(depth, 1);
		lwidth = u_minify(lwidth, 1);
		lheight = u_minify(lheight, 1);
		twidth = u_minify(twidth, 1);
		theight = u_minify(theight, 1);
	}

	if (layout->layer_first) {
		layout->layer_size = align(layout->size, 4096);
		layout->size = layout->layer_size * array_size;
	}

	/* Place the UBWC slices before the uncompressed slices, because the
	 * kernel expects UBWC to be at the start of the buffer.  In the HW, we
	 * get to program the UBWC and non-UBWC offset/strides
	 * independently.
	 */
	if (ubwc) {
		for (uint32_t level = 0; level < mip_levels; level++)
			layout->slices[level].offset += layout->ubwc_size * array_size;
		layout->size += layout->ubwc_size * array_size;
	}

	if (false) {
		for (uint32_t level = 0; level < mip_levels; level++) {
			struct fdl_slice *slice = &layout->slices[level];
			struct fdl_slice *ubwc_slice = &layout->ubwc_slices[level];
			uint32_t tile_mode = (ubwc ?
					layout->tile_mode : fdl_tile_mode(layout, level));

			fprintf(stderr, "%s: %ux%ux%u@%ux%u:\t%2u: stride=%4u, size=%6u,%6u, aligned_height=%3u, offset=0x%x,0x%x tiling=%d\n",
					util_format_name(format),
					u_minify(layout->width0, level),
					u_minify(layout->height0, level),
					u_minify(layout->depth0, level),
					layout->cpp, nr_samples,
					level,
					slice->pitch * layout->cpp,
					slice->size0, ubwc_slice->size0,
					slice->size0 / (slice->pitch * layout->cpp),
					slice->offset, ubwc_slice->offset,
					tile_mode);
		}
	}
}

void
fdl6_get_ubwc_blockwidth(struct fdl_layout *layout,
		uint32_t *blockwidth, uint32_t *blockheight)
{
	*blockwidth = tile_alignment[layout->cpp].ubwc_blockwidth;
	*blockheight = tile_alignment[layout->cpp].ubwc_blockheight;
}
