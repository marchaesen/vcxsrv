/*
 * Copyright © 2019 Google, Inc.
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
 */

#ifndef FREEDRENO_LAYOUT_H_
#define FREEDRENO_LAYOUT_H_

#include <stdbool.h>
#include <stdint.h>

#include "util/u_debug.h"
#include "util/u_math.h"
#include "util/format/u_format.h"

/* Shared freedreno mipmap layout helper
 *
 * It does *not* attempt to track surface transitions, in particular
 * about UBWC state.  Possibly it should, but
 *  (a) I'm not sure if in all cases we can transparently do in-
 *      place transitions (ie. a5xx textures with interleaved
 *      meta and pixel data
 *  (b) Even if we can, we probably can't assume that we have
 *      figured out yet how to do in-place transition for every
 *      generation.
 */

/* Texture Layout on a3xx:
 * -----------------------
 *
 * Each mipmap-level contains all of it's layers (ie. all cubmap
 * faces, all 1d/2d array elements, etc).  The texture sampler is
 * programmed with the start address of each mipmap level, and hw
 * derives the layer offset within the level.
 *
 *
 * Texture Layout on a4xx+:
 * -----------------------
 *
 * For cubemap and 2d array, each layer contains all of it's mipmap
 * levels (layer_first layout).
 *
 * 3d textures are laid out as on a3xx.
 *
 * In either case, the slice represents the per-miplevel information,
 * but in layer_first layout it only includes the first layer, and
 * an additional offset of (rsc->layer_size * layer) must be added.
 *
 *
 * UBWC Color Compressions (a5xx+):
 * -------------------------------
 *
 * Color compression is only supported for tiled layouts.  In general
 * the meta "flag" buffer (ie. what holds the compression state for
 * each block) can be separate from the color data, except for textures
 * on a5xx where it needs to be interleaved with layers/levels of a
 * texture.
 */

#define FDL_MAX_MIP_LEVELS 15

struct fdl_slice {
	uint32_t offset;         /* offset of first layer in slice */
	uint32_t size0;          /* size of first layer in slice */
};

/* parameters for explicit (imported) layout */
struct fdl_explicit_layout {
	uint32_t offset;
	uint32_t pitch;
};

/**
 * Encapsulates the layout of a resource, including position of given 2d
 * surface (layer, level) within.  Or rather all the information needed
 * to derive this.
 */
struct fdl_layout {
	struct fdl_slice slices[FDL_MAX_MIP_LEVELS];
	struct fdl_slice ubwc_slices[FDL_MAX_MIP_LEVELS];
	uint32_t pitch0;
	uint32_t ubwc_width0;
	uint32_t layer_size;
	uint32_t ubwc_layer_size; /* in bytes */
	bool ubwc : 1;
	bool layer_first : 1;    /* see above description */
	bool tile_all : 1;

	/* Note that for tiled textures, beyond a certain mipmap level (ie.
	 * when width is less than block size) things switch to linear.  In
	 * general you should not directly look at fdl_layout::tile_mode,
	 * but instead use fdl_surface::tile_mode which will correctly take
	 * this into account.
	 */
	uint32_t tile_mode : 2;
	/* Bytes per pixel (where a "pixel" is a single row of a block in the case
	 * of compression), including each sample in the case of multisample
	 * layouts.
	 */
	uint8_t cpp;

	/**
	 * Left shift necessary to multiply by cpp.  Invalid for NPOT cpp, please
	 * use fdl_cpp_shift() to sanity check you aren't hitting that case.
	 */
	uint8_t cpp_shift;

	uint32_t width0, height0, depth0;
	uint32_t nr_samples;
	enum pipe_format format;

	uint32_t size; /* Size of the whole image, in bytes. */
	uint32_t base_align; /* Alignment of the base address, in bytes. */
	uint8_t pitchalign; /* log2(pitchalign) */
};

static inline uint32_t
fdl_cpp_shift(const struct fdl_layout *layout)
{
	assert(util_is_power_of_two_or_zero(layout->cpp));
	return layout->cpp_shift;
}

static inline uint32_t
fdl_pitch(const struct fdl_layout *layout, unsigned level)
{
	return align(u_minify(layout->pitch0, level), 1 << layout->pitchalign);
}

#define RGB_TILE_WIDTH_ALIGNMENT 64
#define RGB_TILE_HEIGHT_ALIGNMENT 16
#define UBWC_PLANE_SIZE_ALIGNMENT 4096

static inline uint32_t
fdl_ubwc_pitch(const struct fdl_layout *layout, unsigned level)
{
	if (!layout->ubwc)
		return 0;
	return align(u_minify(layout->ubwc_width0, level), RGB_TILE_WIDTH_ALIGNMENT);
}

static inline uint32_t
fdl_layer_stride(const struct fdl_layout *layout, unsigned level)
{
	if (layout->layer_first)
		return layout->layer_size;
	else
		return layout->slices[level].size0;
}

/* a2xx is special and needs PoT alignment for mipmaps: */
static inline uint32_t
fdl2_pitch(const struct fdl_layout *layout, unsigned level)
{
	uint32_t pitch = fdl_pitch(layout, level);
	if (level)
		pitch = util_next_power_of_two(pitch);
	return pitch;
}

static inline uint32_t
fdl2_pitch_pixels(const struct fdl_layout *layout, unsigned level)
{
	return fdl2_pitch(layout, level) >> fdl_cpp_shift(layout);
}

static inline uint32_t
fdl_surface_offset(const struct fdl_layout *layout, unsigned level, unsigned layer)
{
	const struct fdl_slice *slice = &layout->slices[level];
	return slice->offset + fdl_layer_stride(layout, level) * layer;
}

static inline uint32_t
fdl_ubwc_offset(const struct fdl_layout *layout, unsigned level, unsigned layer)
{
	const struct fdl_slice *slice = &layout->ubwc_slices[level];
	return slice->offset + layer * layout->ubwc_layer_size;
}

/* Minimum layout width to enable UBWC. */
#define FDL_MIN_UBWC_WIDTH 16

static inline bool
fdl_level_linear(const struct fdl_layout *layout, int level)
{
	if (layout->tile_all)
		return false;

	unsigned w = u_minify(layout->width0, level);
	if (w < FDL_MIN_UBWC_WIDTH)
		return true;

	return false;
}

static inline uint32_t
fdl_tile_mode(const struct fdl_layout *layout, int level)
{
	if (layout->tile_mode && fdl_level_linear(layout, level))
		return 0; /* linear */
	else
		return layout->tile_mode;
}

static inline bool
fdl_ubwc_enabled(const struct fdl_layout *layout, int level)
{
	return layout->ubwc;
}

void
fdl_layout_buffer(struct fdl_layout *layout, uint32_t size);

void
fdl5_layout(struct fdl_layout *layout,
		enum pipe_format format, uint32_t nr_samples,
		uint32_t width0, uint32_t height0, uint32_t depth0,
		uint32_t mip_levels, uint32_t array_size, bool is_3d);

bool
fdl6_layout(struct fdl_layout *layout,
		enum pipe_format format, uint32_t nr_samples,
		uint32_t width0, uint32_t height0, uint32_t depth0,
		uint32_t mip_levels, uint32_t array_size, bool is_3d,
		struct fdl_explicit_layout *plane_layout);

static inline void
fdl_set_pitchalign(struct fdl_layout *layout, unsigned pitchalign)
{
	uint32_t nblocksx = util_format_get_nblocksx(layout->format, layout->width0);
	layout->pitchalign = pitchalign;
	layout->pitch0 = align(nblocksx * layout->cpp, 1 << pitchalign);
}

void
fdl_dump_layout(struct fdl_layout *layout);

void
fdl6_get_ubwc_blockwidth(struct fdl_layout *layout,
		uint32_t *blockwidth, uint32_t *blockheight);

#endif /* FREEDRENO_LAYOUT_H_ */
