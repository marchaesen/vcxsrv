/*
 * Copyright (C) 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
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

#include "pipe/p_defines.h"
#include "util/format/u_format.h"

#include "fd6_format.h"
#include "freedreno_resource.h"


/* Specifies the table of all the formats and their features. Also supplies
 * the helpers that look up various data in those tables.
 */

struct fd6_format {
	enum a6xx_format vtx;
	enum a6xx_format tex;
	enum a6xx_format rb;
	enum a3xx_color_swap swap;
	boolean present;
};

#define FMT(pipe, vtxfmt, texfmt, rbfmt, swapfmt) \
	[PIPE_FORMAT_ ## pipe] = { \
		.present = 1, \
		.vtx = FMT6_ ## vtxfmt, \
		.tex = FMT6_ ## texfmt, \
		.rb = FMT6_ ## rbfmt, \
		.swap = swapfmt \
	}

/* vertex + texture + color */
#define VTC(pipe, fmt, swapfmt) FMT(pipe, fmt, fmt, fmt, swapfmt)

#define _TC(pipe, fmt, swapfmt) FMT(pipe, NONE, fmt, fmt, swapfmt)
#define _T_(pipe, fmt, swapfmt) FMT(pipe, NONE, fmt, NONE, swapfmt)
#define VT_(pipe, fmt, swapfmt) FMT(pipe, fmt, fmt, NONE, swapfmt)
#define V__(pipe, fmt, swapfmt) FMT(pipe, fmt, NONE, NONE, swapfmt)

static struct fd6_format formats[PIPE_FORMAT_COUNT] = {
	/* 8-bit */
	VTC(R8_UNORM,   8_UNORM,                     WZYX),
	VTC(R8_SNORM,   8_SNORM,                     WZYX),
	VTC(R8_UINT,    8_UINT,                      WZYX),
	VTC(R8_SINT,    8_SINT,                      WZYX),
	V__(R8_USCALED, 8_UINT,                      WZYX),
	V__(R8_SSCALED, 8_SINT,                      WZYX),

	FMT(A8_UNORM,   NONE, 8_UNORM, A8_UNORM,     WZYX),
	_TC(L8_UNORM,   8_UNORM,                     WZYX),
	_T_(I8_UNORM,   8_UNORM,                     WZYX),

	_T_(A8_UINT,    8_UINT,                      WZYX),
	_T_(A8_SINT,    8_SINT,                      WZYX),
	_T_(L8_UINT,    8_UINT,                      WZYX),
	_T_(L8_SINT,    8_SINT,                      WZYX),
	_T_(I8_UINT,    8_UINT,                      WZYX),
	_T_(I8_SINT,    8_SINT,                      WZYX),

	_TC(S8_UINT,    8_UINT,                      WZYX),

	/* 16-bit */
	VTC(R16_UNORM,   16_UNORM,                   WZYX),
	VTC(R16_SNORM,   16_SNORM,                   WZYX),
	VTC(R16_UINT,    16_UINT,                    WZYX),
	VTC(R16_SINT,    16_SINT,                    WZYX),
	V__(R16_USCALED, 16_UINT,                    WZYX),
	V__(R16_SSCALED, 16_SINT,                    WZYX),
	VTC(R16_FLOAT,   16_FLOAT,                   WZYX),
	_TC(Z16_UNORM,   16_UNORM,                   WZYX),

	_T_(A16_UNORM,   16_UNORM,                   WZYX),
	_T_(A16_SNORM,   16_SNORM,                   WZYX),
	_T_(A16_UINT,    16_UINT,                    WZYX),
	_T_(A16_SINT,    16_SINT,                    WZYX),
	_T_(L16_UNORM,   16_UNORM,                   WZYX),
	_T_(L16_SNORM,   16_SNORM,                   WZYX),
	_T_(L16_UINT,    16_UINT,                    WZYX),
	_T_(L16_SINT,    16_SINT,                    WZYX),
	_T_(I16_UNORM,   16_UNORM,                   WZYX),
	_T_(I16_SNORM,   16_SNORM,                   WZYX),
	_T_(I16_UINT,    16_UINT,                    WZYX),
	_T_(I16_SINT,    16_SINT,                    WZYX),

	VTC(R8G8_UNORM,   8_8_UNORM,                 WZYX),
	VTC(R8G8_SNORM,   8_8_SNORM,                 WZYX),
	VTC(R8G8_UINT,    8_8_UINT,                  WZYX),
	VTC(R8G8_SINT,    8_8_SINT,                  WZYX),
	V__(R8G8_USCALED, 8_8_UINT,                  WZYX),
	V__(R8G8_SSCALED, 8_8_SINT,                  WZYX),

	_T_(L8A8_UINT,    8_8_UINT,                  WZYX),
	_T_(L8A8_SINT,    8_8_SINT,                  WZYX),

	_TC(B5G6R5_UNORM,   5_6_5_UNORM,             WXYZ),
	_TC(B5G5R5A1_UNORM, 5_5_5_1_UNORM,           WXYZ),
	_TC(B5G5R5X1_UNORM, 5_5_5_1_UNORM,           WXYZ),
	_TC(B4G4R4A4_UNORM, 4_4_4_4_UNORM,           WXYZ),

	/* 24-bit */
	V__(R8G8B8_UNORM,   8_8_8_UNORM,             WZYX),
	V__(R8G8B8_SNORM,   8_8_8_SNORM,             WZYX),
	V__(R8G8B8_UINT,    8_8_8_UINT,              WZYX),
	V__(R8G8B8_SINT,    8_8_8_SINT,              WZYX),
	V__(R8G8B8_USCALED, 8_8_8_UINT,              WZYX),
	V__(R8G8B8_SSCALED, 8_8_8_SINT,              WZYX),

	/* 32-bit */
	V__(R32_UNORM,   32_UNORM,                   WZYX),
	V__(R32_SNORM,   32_SNORM,                   WZYX),
	VTC(R32_UINT,    32_UINT,                    WZYX),
	VTC(R32_SINT,    32_SINT,                    WZYX),
	V__(R32_USCALED, 32_UINT,                    WZYX),
	V__(R32_SSCALED, 32_SINT,                    WZYX),
	VTC(R32_FLOAT,   32_FLOAT,                   WZYX),
	V__(R32_FIXED,   32_FIXED,                   WZYX),

	_T_(A32_UINT,    32_UINT,                    WZYX),
	_T_(A32_SINT,    32_SINT,                    WZYX),
	_T_(L32_UINT,    32_UINT,                    WZYX),
	_T_(L32_SINT,    32_SINT,                    WZYX),
	_T_(I32_UINT,    32_UINT,                    WZYX),
	_T_(I32_SINT,    32_SINT,                    WZYX),

	VTC(R16G16_UNORM,   16_16_UNORM,             WZYX),
	VTC(R16G16_SNORM,   16_16_SNORM,             WZYX),
	VTC(R16G16_UINT,    16_16_UINT,              WZYX),
	VTC(R16G16_SINT,    16_16_SINT,              WZYX),
	VT_(R16G16_USCALED, 16_16_UINT,              WZYX),
	VT_(R16G16_SSCALED, 16_16_SINT,              WZYX),
	VTC(R16G16_FLOAT,   16_16_FLOAT,             WZYX),

	_T_(L16A16_UNORM,   16_16_UNORM,             WZYX),
	_T_(L16A16_SNORM,   16_16_SNORM,             WZYX),
	_T_(L16A16_UINT,    16_16_UINT,              WZYX),
	_T_(L16A16_SINT,    16_16_SINT,              WZYX),

	VTC(R8G8B8A8_UNORM,   8_8_8_8_UNORM,         WZYX),
	_TC(R8G8B8X8_UNORM,   8_8_8_8_UNORM,         WZYX),
	_TC(R8G8B8A8_SRGB,    8_8_8_8_UNORM,         WZYX),
	_TC(R8G8B8X8_SRGB,    8_8_8_8_UNORM,         WZYX),
	VTC(R8G8B8A8_SNORM,   8_8_8_8_SNORM,         WZYX),
	VTC(R8G8B8A8_UINT,    8_8_8_8_UINT,          WZYX),
	VTC(R8G8B8A8_SINT,    8_8_8_8_SINT,          WZYX),
	V__(R8G8B8A8_USCALED, 8_8_8_8_UINT,          WZYX),
	V__(R8G8B8A8_SSCALED, 8_8_8_8_SINT,          WZYX),

	VTC(B8G8R8A8_UNORM,   8_8_8_8_UNORM,         WXYZ),
	_TC(B8G8R8X8_UNORM,   8_8_8_8_UNORM,         WXYZ),
	VTC(B8G8R8A8_SRGB,    8_8_8_8_UNORM,         WXYZ),
	_TC(B8G8R8X8_SRGB,    8_8_8_8_UNORM,         WXYZ),

	VTC(A8B8G8R8_UNORM,   8_8_8_8_UNORM,         XYZW),
	_TC(X8B8G8R8_UNORM,   8_8_8_8_UNORM,         XYZW),
	_TC(A8B8G8R8_SRGB,    8_8_8_8_UNORM,         XYZW),
	_TC(X8B8G8R8_SRGB,    8_8_8_8_UNORM,         XYZW),

	VTC(A8R8G8B8_UNORM,   8_8_8_8_UNORM,         ZYXW),
	_TC(X8R8G8B8_UNORM,   8_8_8_8_UNORM,         ZYXW),
	_TC(A8R8G8B8_SRGB,    8_8_8_8_UNORM,         ZYXW),
	_TC(X8R8G8B8_SRGB,    8_8_8_8_UNORM,         ZYXW),

	FMT(R10G10B10A2_UNORM, 10_10_10_2_UNORM, 10_10_10_2_UNORM, 10_10_10_2_UNORM_DEST, WZYX),
	FMT(B10G10R10A2_UNORM, 10_10_10_2_UNORM, 10_10_10_2_UNORM, 10_10_10_2_UNORM_DEST, WXYZ),
	FMT(B10G10R10X2_UNORM, NONE,             10_10_10_2_UNORM, 10_10_10_2_UNORM_DEST, WXYZ),
	V__(R10G10B10A2_SNORM,   10_10_10_2_SNORM,   WZYX),
	V__(B10G10R10A2_SNORM,   10_10_10_2_SNORM,   WXYZ),
	VTC(R10G10B10A2_UINT,    10_10_10_2_UINT,    WZYX),
	VTC(B10G10R10A2_UINT,    10_10_10_2_UINT,    WXYZ),
	V__(R10G10B10A2_USCALED, 10_10_10_2_UINT,    WZYX),
	V__(B10G10R10A2_USCALED, 10_10_10_2_UINT,    WXYZ),
	V__(R10G10B10A2_SSCALED, 10_10_10_2_SINT,    WZYX),
	V__(B10G10R10A2_SSCALED, 10_10_10_2_SINT,    WXYZ),

	VTC(R11G11B10_FLOAT, 11_11_10_FLOAT,         WZYX),
	_T_(R9G9B9E5_FLOAT,  9_9_9_E5_FLOAT,         WZYX),

	_TC(Z24X8_UNORM,          Z24_UNORM_S8_UINT, WZYX),
	_TC(X24S8_UINT,           8_8_8_8_UINT,      WZYX),
	_TC(Z24_UNORM_S8_UINT,    Z24_UNORM_S8_UINT, WZYX),
	_TC(Z32_FLOAT,            32_FLOAT,          WZYX),
	_TC(Z32_FLOAT_S8X24_UINT, 32_FLOAT,          WZYX),
	_TC(X32_S8X24_UINT,       8_UINT,            WZYX),

	/* special format for blits: */
	_TC(Z24_UNORM_S8_UINT_AS_R8G8B8A8, Z24_UNORM_S8_UINT_AS_R8G8B8A8, WZYX),

	/* 48-bit */
	V__(R16G16B16_UNORM,   16_16_16_UNORM,       WZYX),
	V__(R16G16B16_SNORM,   16_16_16_SNORM,       WZYX),
	V__(R16G16B16_UINT,    16_16_16_UINT,        WZYX),
	V__(R16G16B16_SINT,    16_16_16_SINT,        WZYX),
	V__(R16G16B16_USCALED, 16_16_16_UINT,        WZYX),
	V__(R16G16B16_SSCALED, 16_16_16_SINT,        WZYX),
	V__(R16G16B16_FLOAT,   16_16_16_FLOAT,       WZYX),

	/* 64-bit */
	VTC(R16G16B16A16_UNORM,   16_16_16_16_UNORM, WZYX),
	VTC(R16G16B16X16_UNORM,   16_16_16_16_UNORM, WZYX),
	VTC(R16G16B16A16_SNORM,   16_16_16_16_SNORM, WZYX),
	VTC(R16G16B16X16_SNORM,   16_16_16_16_SNORM, WZYX),
	VTC(R16G16B16A16_UINT,    16_16_16_16_UINT,  WZYX),
	VTC(R16G16B16X16_UINT,    16_16_16_16_UINT,  WZYX),
	VTC(R16G16B16A16_SINT,    16_16_16_16_SINT,  WZYX),
	VTC(R16G16B16X16_SINT,    16_16_16_16_SINT,  WZYX),
	VT_(R16G16B16A16_USCALED, 16_16_16_16_UINT,  WZYX),
	VT_(R16G16B16A16_SSCALED, 16_16_16_16_SINT,  WZYX),
	VTC(R16G16B16A16_FLOAT,   16_16_16_16_FLOAT, WZYX),
	VTC(R16G16B16X16_FLOAT,   16_16_16_16_FLOAT, WZYX),

	V__(R32G32_UNORM,   32_32_UNORM,             WZYX),
	V__(R32G32_SNORM,   32_32_SNORM,             WZYX),
	VTC(R32G32_UINT,    32_32_UINT,              WZYX),
	VTC(R32G32_SINT,    32_32_SINT,              WZYX),
	V__(R32G32_USCALED, 32_32_UINT,              WZYX),
	V__(R32G32_SSCALED, 32_32_SINT,              WZYX),
	VTC(R32G32_FLOAT,   32_32_FLOAT,             WZYX),
	V__(R32G32_FIXED,   32_32_FIXED,             WZYX),

	_T_(L32A32_UINT,    32_32_UINT,              WZYX),
	_T_(L32A32_SINT,    32_32_SINT,              WZYX),

	/* 96-bit */
	V__(R32G32B32_UNORM,   32_32_32_UNORM,       WZYX),
	V__(R32G32B32_SNORM,   32_32_32_SNORM,       WZYX),
	VT_(R32G32B32_UINT,    32_32_32_UINT,        WZYX),
	VT_(R32G32B32_SINT,    32_32_32_SINT,        WZYX),
	V__(R32G32B32_USCALED, 32_32_32_UINT,        WZYX),
	V__(R32G32B32_SSCALED, 32_32_32_SINT,        WZYX),
	VT_(R32G32B32_FLOAT,   32_32_32_FLOAT,       WZYX),
	V__(R32G32B32_FIXED,   32_32_32_FIXED,       WZYX),

	/* 128-bit */
	V__(R32G32B32A32_UNORM,   32_32_32_32_UNORM, WZYX),
	V__(R32G32B32A32_SNORM,   32_32_32_32_SNORM, WZYX),
	VTC(R32G32B32A32_UINT,    32_32_32_32_UINT,  WZYX),
	_TC(R32G32B32X32_UINT,    32_32_32_32_UINT,  WZYX),
	VTC(R32G32B32A32_SINT,    32_32_32_32_SINT,  WZYX),
	_TC(R32G32B32X32_SINT,    32_32_32_32_SINT,  WZYX),
	V__(R32G32B32A32_USCALED, 32_32_32_32_UINT,  WZYX),
	V__(R32G32B32A32_SSCALED, 32_32_32_32_SINT,  WZYX),
	VTC(R32G32B32A32_FLOAT,   32_32_32_32_FLOAT, WZYX),
	_TC(R32G32B32X32_FLOAT,   32_32_32_32_FLOAT, WZYX),
	V__(R32G32B32A32_FIXED,   32_32_32_32_FIXED, WZYX),

	/* compressed */
	_T_(ETC1_RGB8, ETC1,                         WZYX),
	_T_(ETC2_RGB8, ETC2_RGB8,                    WZYX),
	_T_(ETC2_SRGB8, ETC2_RGB8,                   WZYX),
	_T_(ETC2_RGB8A1, ETC2_RGB8A1,                WZYX),
	_T_(ETC2_SRGB8A1, ETC2_RGB8A1,               WZYX),
	_T_(ETC2_RGBA8, ETC2_RGBA8,                  WZYX),
	_T_(ETC2_SRGBA8, ETC2_RGBA8,                 WZYX),
	_T_(ETC2_R11_UNORM, ETC2_R11_UNORM,          WZYX),
	_T_(ETC2_R11_SNORM, ETC2_R11_SNORM,          WZYX),
	_T_(ETC2_RG11_UNORM, ETC2_RG11_UNORM,        WZYX),
	_T_(ETC2_RG11_SNORM, ETC2_RG11_SNORM,        WZYX),

	_T_(DXT1_RGB,   DXT1,                        WZYX),
	_T_(DXT1_SRGB,  DXT1,                        WZYX),
	_T_(DXT1_RGBA,  DXT1,                        WZYX),
	_T_(DXT1_SRGBA, DXT1,                        WZYX),
	_T_(DXT3_RGBA,  DXT3,                        WZYX),
	_T_(DXT3_SRGBA, DXT3,                        WZYX),
	_T_(DXT5_RGBA,  DXT5,                        WZYX),
	_T_(DXT5_SRGBA, DXT5,                        WZYX),

	_T_(BPTC_RGBA_UNORM, BPTC,                   WZYX),
	_T_(BPTC_SRGBA,      BPTC,                   WZYX),
	_T_(BPTC_RGB_FLOAT,  BPTC_FLOAT,             WZYX),
	_T_(BPTC_RGB_UFLOAT, BPTC_UFLOAT,            WZYX),

	_T_(RGTC1_UNORM, RGTC1_UNORM,                WZYX),
	_T_(RGTC1_SNORM, RGTC1_SNORM,                WZYX),
	_T_(RGTC2_UNORM, RGTC2_UNORM,                WZYX),
	_T_(RGTC2_SNORM, RGTC2_SNORM,                WZYX),
	_T_(LATC1_UNORM, RGTC1_UNORM,                WZYX),
	_T_(LATC1_SNORM, RGTC1_SNORM,                WZYX),
	_T_(LATC2_UNORM, RGTC2_UNORM,                WZYX),
	_T_(LATC2_SNORM, RGTC2_SNORM,                WZYX),

	_T_(ASTC_4x4,   ASTC_4x4,                    WZYX),
	_T_(ASTC_5x4,   ASTC_5x4,                    WZYX),
	_T_(ASTC_5x5,   ASTC_5x5,                    WZYX),
	_T_(ASTC_6x5,   ASTC_6x5,                    WZYX),
	_T_(ASTC_6x6,   ASTC_6x6,                    WZYX),
	_T_(ASTC_8x5,   ASTC_8x5,                    WZYX),
	_T_(ASTC_8x6,   ASTC_8x6,                    WZYX),
	_T_(ASTC_8x8,   ASTC_8x8,                    WZYX),
	_T_(ASTC_10x5,  ASTC_10x5,                   WZYX),
	_T_(ASTC_10x6,  ASTC_10x6,                   WZYX),
	_T_(ASTC_10x8,  ASTC_10x8,                   WZYX),
	_T_(ASTC_10x10, ASTC_10x10,                  WZYX),
	_T_(ASTC_12x10, ASTC_12x10,                  WZYX),
	_T_(ASTC_12x12, ASTC_12x12,                  WZYX),

	_T_(ASTC_4x4_SRGB,   ASTC_4x4,               WZYX),
	_T_(ASTC_5x4_SRGB,   ASTC_5x4,               WZYX),
	_T_(ASTC_5x5_SRGB,   ASTC_5x5,               WZYX),
	_T_(ASTC_6x5_SRGB,   ASTC_6x5,               WZYX),
	_T_(ASTC_6x6_SRGB,   ASTC_6x6,               WZYX),
	_T_(ASTC_8x5_SRGB,   ASTC_8x5,               WZYX),
	_T_(ASTC_8x6_SRGB,   ASTC_8x6,               WZYX),
	_T_(ASTC_8x8_SRGB,   ASTC_8x8,               WZYX),
	_T_(ASTC_10x5_SRGB,  ASTC_10x5,              WZYX),
	_T_(ASTC_10x6_SRGB,  ASTC_10x6,              WZYX),
	_T_(ASTC_10x8_SRGB,  ASTC_10x8,              WZYX),
	_T_(ASTC_10x10_SRGB, ASTC_10x10,             WZYX),
	_T_(ASTC_12x10_SRGB, ASTC_12x10,             WZYX),
	_T_(ASTC_12x12_SRGB, ASTC_12x12,             WZYX),

	_T_(R8_G8B8_420_UNORM, R8_G8B8_2PLANE_420_UNORM, WZYX),
};

/* convert pipe format to vertex buffer format: */
enum a6xx_format
fd6_pipe2vtx(enum pipe_format format)
{
	if (!formats[format].present)
		return FMT6_NONE;
	return formats[format].vtx;
}

/* convert pipe format to texture sampler format: */
enum a6xx_format
fd6_pipe2tex(enum pipe_format format)
{
	if (!formats[format].present)
		return FMT6_NONE;
	return formats[format].tex;
}

/* convert pipe format to MRT / copydest format used for render-target: */
enum a6xx_format
fd6_pipe2color(enum pipe_format format)
{
	if (!formats[format].present)
		return FMT6_NONE;
	return formats[format].rb;
}

enum a3xx_color_swap
fd6_pipe2swap(enum pipe_format format)
{
	if (!formats[format].present)
		return WZYX;
	return formats[format].swap;
}

enum a6xx_depth_format
fd6_pipe2depth(enum pipe_format format)
{
	switch (format) {
	case PIPE_FORMAT_Z16_UNORM:
		return DEPTH6_16;
	case PIPE_FORMAT_Z24X8_UNORM:
	case PIPE_FORMAT_Z24_UNORM_S8_UINT:
	case PIPE_FORMAT_X8Z24_UNORM:
	case PIPE_FORMAT_S8_UINT_Z24_UNORM:
		return DEPTH6_24_8;
	case PIPE_FORMAT_Z32_FLOAT:
	case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
		return DEPTH6_32;
	default:
		return ~0;
	}
}

enum a6xx_tex_swiz
fd6_pipe2swiz(unsigned swiz)
{
	switch (swiz) {
	default:
	case PIPE_SWIZZLE_X: return A6XX_TEX_X;
	case PIPE_SWIZZLE_Y: return A6XX_TEX_Y;
	case PIPE_SWIZZLE_Z: return A6XX_TEX_Z;
	case PIPE_SWIZZLE_W: return A6XX_TEX_W;
	case PIPE_SWIZZLE_0: return A6XX_TEX_ZERO;
	case PIPE_SWIZZLE_1: return A6XX_TEX_ONE;
	}
}

void
fd6_tex_swiz(enum pipe_format format, unsigned char *swiz,
			 unsigned swizzle_r, unsigned swizzle_g,
			 unsigned swizzle_b, unsigned swizzle_a)
{
	const struct util_format_description *desc =
			util_format_description(format);
	const unsigned char uswiz[4] = {
		swizzle_r, swizzle_g, swizzle_b, swizzle_a
	};

	/* Gallium expects stencil sampler to return (s,s,s,s), so massage
	 * the swizzle to do so.
	 */
	if (format == PIPE_FORMAT_X24S8_UINT) {
		const unsigned char stencil_swiz[4] = {
			PIPE_SWIZZLE_W, PIPE_SWIZZLE_W, PIPE_SWIZZLE_W, PIPE_SWIZZLE_W
		};
		util_format_compose_swizzles(stencil_swiz, uswiz, swiz);
	} else if (fd6_pipe2swap(format) != WZYX) {
		/* Formats with a non-pass-through swap are permutations of RGBA
		 * formats. We program the permutation using the swap and don't
		 * need to compose the format swizzle with the user swizzle.
		 */
		memcpy(swiz, uswiz, sizeof(uswiz));
	} else {
		/* Otherwise, it's an unswapped RGBA format or a format like L8 where
		 * we need the XXX1 swizzle from the gallium format description.
		 */
		util_format_compose_swizzles(desc->swizzle, uswiz, swiz);
	}
}

/* Compute the TEX_CONST_0 value for texture state, including SWIZ/SWAP/etc: */
uint32_t
fd6_tex_const_0(struct pipe_resource *prsc,
			 unsigned level, enum pipe_format format,
			 unsigned swizzle_r, unsigned swizzle_g,
			 unsigned swizzle_b, unsigned swizzle_a)
{
	struct fd_resource *rsc = fd_resource(prsc);
	unsigned char swiz[4];

	fd6_tex_swiz(format, swiz,
			swizzle_r, swizzle_g,
			swizzle_b, swizzle_a);

	return
		A6XX_TEX_CONST_0_FMT(fd6_pipe2tex(format)) |
		A6XX_TEX_CONST_0_SAMPLES(fd_msaa_samples(prsc->nr_samples)) |
		A6XX_TEX_CONST_0_SWAP(fd6_resource_swap(rsc, format)) |
		A6XX_TEX_CONST_0_TILE_MODE(fd_resource_tile_mode(prsc, level)) |
		COND(util_format_is_srgb(format), A6XX_TEX_CONST_0_SRGB) |
		A6XX_TEX_CONST_0_SWIZ_X(fd6_pipe2swiz(swiz[0])) |
		A6XX_TEX_CONST_0_SWIZ_Y(fd6_pipe2swiz(swiz[1])) |
		A6XX_TEX_CONST_0_SWIZ_Z(fd6_pipe2swiz(swiz[2])) |
		A6XX_TEX_CONST_0_SWIZ_W(fd6_pipe2swiz(swiz[3]));
}
