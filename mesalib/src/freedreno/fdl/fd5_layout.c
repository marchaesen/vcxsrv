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

/* indexed by cpp: */
static const struct {
	unsigned pitchalign;
	unsigned heightalign;
} tile_alignment[] = {
	[1]  = { 128, 32 },
	[2]  = { 128, 16 },
	[3]  = { 128, 16 },
	[4]  = {  64, 16 },
	[8]  = {  64, 16 },
	[12] = {  64, 16 },
	[16] = {  64, 16 },
};

void
fdl5_layout(struct fdl_layout *layout,
		enum pipe_format format, uint32_t nr_samples,
		uint32_t width0, uint32_t height0, uint32_t depth0,
		uint32_t mip_levels, uint32_t array_size, bool is_3d)
{
	const struct util_format_description *format_desc =
		util_format_description(format);

	assert(nr_samples > 0);
	layout->width0 = width0;
	layout->height0 = height0;
	layout->depth0 = depth0;

	layout->cpp = util_format_get_blocksize(format);
	layout->cpp *= nr_samples;
	layout->cpp_shift = ffs(layout->cpp) - 1;

	layout->format = format;
	layout->nr_samples = nr_samples;
	layout->layer_first = !is_3d;

	uint32_t pitchalign;
	uint32_t heightalign;
	uint32_t width = width0;
	uint32_t height = height0;
	uint32_t depth = depth0;
	/* in layer_first layout, the level (slice) contains just one
	 * layer (since in fact the layer contains the slices)
	 */
	uint32_t layers_in_level = layout->layer_first ? 1 : array_size;

	heightalign = tile_alignment[layout->cpp].heightalign;

	for (uint32_t level = 0; level < mip_levels; level++) {
		struct fdl_slice *slice = &layout->slices[level];
		uint32_t tile_mode = fdl_tile_mode(layout, level);
		uint32_t aligned_height = height;
		uint32_t blocks;

		if (tile_mode) {
			pitchalign = tile_alignment[layout->cpp].pitchalign;
			aligned_height = align(aligned_height, heightalign);
		} else {
			pitchalign = 64;

			/* The blits used for mem<->gmem work at a granularity of
			 * 32x32, which can cause faults due to over-fetch on the
			 * last level.  The simple solution is to over-allocate a
			 * bit the last level to ensure any over-fetch is harmless.
			 * The pitch is already sufficiently aligned, but height
			 * may not be:
			 */
			if (level == mip_levels - 1)
				aligned_height = align(aligned_height, 32);
		}

		unsigned pitch_pixels;
		if (format_desc->layout == UTIL_FORMAT_LAYOUT_ASTC)
			pitch_pixels =
				util_align_npot(width, pitchalign * util_format_get_blockwidth(format));
		else
			pitch_pixels = align(width, pitchalign);

		slice->offset = layout->size;
		blocks = util_format_get_nblocks(format, pitch_pixels, aligned_height);
		slice->pitch = util_format_get_nblocksx(format, pitch_pixels) *
			layout->cpp;

		const int alignment = is_3d ? 4096 : 1;

		/* 1d array and 2d array textures must all have the same layer size
		 * for each miplevel on a3xx. 3d textures can have different layer
		 * sizes for high levels, but the hw auto-sizer is buggy (or at least
		 * different than what this code does), so as soon as the layer size
		 * range gets into range, we stop reducing it.
		 */
		if (is_3d && (
					level == 1 ||
					(level > 1 && layout->slices[level - 1].size0 > 0xf000)))
			slice->size0 = align(blocks * layout->cpp, alignment);
		else if (level == 0 || layout->layer_first || alignment == 1)
			slice->size0 = align(blocks * layout->cpp, alignment);
		else
			slice->size0 = layout->slices[level - 1].size0;

		layout->size += slice->size0 * depth * layers_in_level;

		width = u_minify(width, 1);
		height = u_minify(height, 1);
		depth = u_minify(depth, 1);
	}
}

