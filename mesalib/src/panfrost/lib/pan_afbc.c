/*
 * Copyright (C) 2019 Collabora, Ltd.
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
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#include "pan_texture.h"

/* Arm FrameBuffer Compression (AFBC) is a lossless compression scheme natively
 * implemented in Mali GPUs (as well as many display controllers paired with
 * Mali GPUs, etc). Where possible, Panfrost prefers to use AFBC for both
 * rendering and texturing. In most cases, this is a performance-win due to a
 * dramatic reduction in memory bandwidth and cache locality compared to a
 * linear resources.
 *
 * AFBC divides the framebuffer into 16x16 tiles (other sizes possible, TODO:
 * do we need to support this?). So, the width and height each must be aligned
 * up to 16 pixels. This is inherently good for performance; note that for a 4
 * byte-per-pixel format like RGBA8888, that means that rows are 16*4=64 byte
 * aligned, which is the cache-line size.
 *
 * For each AFBC-compressed resource, there is a single contiguous
 * (CPU/GPU-shared) buffer. This buffer itself is divided into two parts:
 * header and body, placed immediately after each other.
 *
 * The AFBC header contains 16 bytes of metadata per tile.
 *
 * The AFBC body is the same size as the original linear resource (padded to
 * the nearest tile). Although the body comes immediately after the header, it
 * must also be cache-line aligned, so there can sometimes be a bit of padding
 * between the header and body.
 *
 * As an example, a 64x64 RGBA framebuffer contains 64/16 = 4 tiles horizontally and
 * 4 tiles vertically. There are 4*4=16 tiles in total, each containing 16
 * bytes of metadata, so there is a 16*16=256 byte header. 64x64 is already
 * tile aligned, so the body is 64*64 * 4 bytes per pixel = 16384 bytes of
 * body.
 *
 * From userspace, Panfrost needs to be able to calculate these sizes. It
 * explicitly does not and can not know the format of the data contained within
 * this header and body. The GPU has native support for AFBC encode/decode. For
 * an internal FBO or a framebuffer used for scanout with an AFBC-compatible
 * winsys/display-controller, the buffer is maintained AFBC throughout flight,
 * and the driver never needs to know the internal data. For edge cases where
 * the driver really does need to read/write from the AFBC resource, we
 * generate a linear staging buffer and use the GPU to blit AFBC<--->linear.
 * TODO: Implement me. */

#define AFBC_TILE_WIDTH 16
#define AFBC_TILE_HEIGHT 16
#define AFBC_CACHE_ALIGN 64

/* Is it possible to AFBC compress a particular format? Common formats (and
 * YUV) are compressible. Some obscure formats are not and fallback on linear,
 * at a performance hit. Also, if you need to disable AFBC entirely in the
 * driver for debug/profiling, just always return false here. */

bool
panfrost_format_supports_afbc(const struct panfrost_device *dev, enum pipe_format format)
{
        const struct util_format_description *desc =
                util_format_description(format);

        /* sRGB cannot be AFBC, but it can be tiled. TODO: Verify. The blob
         * does not do AFBC for SRGB8_ALPHA8, but it's not clear why it
         * shouldn't be able to. */

        if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB)
                return false;

        if (util_format_is_rgba8_variant(desc))
                return true;

        switch (format) {
        case PIPE_FORMAT_R8G8B8_UNORM:
        case PIPE_FORMAT_B8G8R8_UNORM:
        case PIPE_FORMAT_R5G6B5_UNORM:
        case PIPE_FORMAT_B5G6R5_UNORM:
        case PIPE_FORMAT_Z24_UNORM_S8_UINT:
        case PIPE_FORMAT_Z24X8_UNORM:
        case PIPE_FORMAT_Z16_UNORM:
                return true;
        default:
                return false;
        }
}

unsigned
panfrost_afbc_header_size(unsigned width, unsigned height)
{
        /* Align to tile */
        unsigned aligned_width  = ALIGN_POT(width,  AFBC_TILE_WIDTH);
        unsigned aligned_height = ALIGN_POT(height, AFBC_TILE_HEIGHT);

        /* Compute size in tiles, rather than pixels */
        unsigned tile_count_x = aligned_width  / AFBC_TILE_WIDTH;
        unsigned tile_count_y = aligned_height / AFBC_TILE_HEIGHT;
        unsigned tile_count = tile_count_x * tile_count_y;

        /* Multiply to find the header size */
        unsigned header_bytes = tile_count * AFBC_HEADER_BYTES_PER_TILE;

        /* Align and go */
        return ALIGN_POT(header_bytes, AFBC_CACHE_ALIGN);

}

/* The lossless colour transform (AFBC_FORMAT_MOD_YTR) requires RGB. */

bool
panfrost_afbc_can_ytr(enum pipe_format format)
{
        const struct util_format_description *desc =
                util_format_description(format);

        /* YTR is only defined for RGB(A) */
        if (desc->nr_channels != 3 && desc->nr_channels != 4)
                return false;

        /* The fourth channel if it exists doesn't matter */
        return desc->colorspace == UTIL_FORMAT_COLORSPACE_RGB;
}

bool
panfrost_afbc_format_needs_fixup(const struct panfrost_device *dev,
                                 enum pipe_format format)
{
        if (dev->arch < 7)
                return false;

        const struct util_format_description *desc =
                util_format_description(format);

        unsigned nr_channels = desc->nr_channels;

        /* rgb1 is a valid component order, don't test channel 3 in that
         * case.
         */
        if (nr_channels == 4 && desc->swizzle[3] == PIPE_SWIZZLE_1)
                nr_channels = 3;

        bool identity_swizzle = true;
        for (unsigned c = 0; c < nr_channels; c++) {
                if (desc->swizzle[c] != c) {
                        identity_swizzle = false;
                        break;
                }
        }

        if (identity_swizzle ||
            desc->colorspace == UTIL_FORMAT_COLORSPACE_ZS)
                return false;

        return true;
}

enum pipe_format
panfrost_afbc_format_fixup(const struct panfrost_device *dev,
                           enum pipe_format format)
{
        if (!panfrost_afbc_format_needs_fixup(dev, format))
                return format;

        const struct util_format_description *desc =
                util_format_description(format);

        switch (format) {
        case PIPE_FORMAT_B8G8R8_UNORM:
                return PIPE_FORMAT_R8G8B8_UNORM;
        case PIPE_FORMAT_B5G6R5_UNORM:
                return PIPE_FORMAT_R5G6B5_UNORM;
        default:
                if (util_format_is_rgba8_variant(desc))
                        return PIPE_FORMAT_R8G8B8A8_UNORM;

                unreachable("Invalid format");
        }
}
