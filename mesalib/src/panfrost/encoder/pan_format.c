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

#include <stdio.h>
#include "panfrost-job.h"
#include "pan_texture.h"

static unsigned
panfrost_translate_channel_width(unsigned size)
{
        switch (size) {
        case 4:
                return MALI_CHANNEL_4;
        case 8:
                return MALI_CHANNEL_8;
        case 16:
                return MALI_CHANNEL_16;
        case 32:
                return MALI_CHANNEL_32;
        default:
                unreachable("Invalid format width\n");
        }
}

static unsigned
panfrost_translate_channel_type(unsigned type, unsigned size, bool norm)
{
        switch (type) {
        case UTIL_FORMAT_TYPE_UNSIGNED:
                return norm ? MALI_FORMAT_UNORM : MALI_FORMAT_UINT;

        case UTIL_FORMAT_TYPE_SIGNED:
                return norm ? MALI_FORMAT_SNORM : MALI_FORMAT_SINT;

        case UTIL_FORMAT_TYPE_FLOAT:
                /* fp16 -- SINT, fp32 -- UNORM ... gotta use those bits */

                if (size == 16)
                        return MALI_FORMAT_SINT;
                else if (size == 32)
                        return MALI_FORMAT_UNORM;
                else
                        unreachable("Invalid float size");

        default:
                unreachable("Invalid type");
        }
}

/* Constructs a mali_format satisfying the specified Gallium format
 * description */

enum mali_format
panfrost_find_format(const struct util_format_description *desc)
{
        /* Find first non-VOID channel */
        struct util_format_channel_description chan = desc->channel[0];

        for (unsigned c = 0; c < 4; ++c)
        {
                if (desc->channel[c].type == UTIL_FORMAT_TYPE_VOID)
                        continue;

                chan = desc->channel[c];
                break;
        }

        /* Check for special formats */
        switch (desc->format)
        {
        case PIPE_FORMAT_R10G10B10X2_UNORM:
        case PIPE_FORMAT_B10G10R10X2_UNORM:
        case PIPE_FORMAT_R10G10B10A2_UNORM:
        case PIPE_FORMAT_B10G10R10A2_UNORM:
                return MALI_RGB10_A2_UNORM;

        case PIPE_FORMAT_R10G10B10X2_SNORM:
        case PIPE_FORMAT_R10G10B10A2_SNORM:
        case PIPE_FORMAT_B10G10R10A2_SNORM:
                return MALI_RGB10_A2_SNORM;

        case PIPE_FORMAT_R10G10B10A2_UINT:
        case PIPE_FORMAT_B10G10R10A2_UINT:
        case PIPE_FORMAT_R10G10B10A2_USCALED:
        case PIPE_FORMAT_B10G10R10A2_USCALED:
                return MALI_RGB10_A2UI;

        case PIPE_FORMAT_R10G10B10A2_SSCALED:
        case PIPE_FORMAT_B10G10R10A2_SSCALED:
                return MALI_RGB10_A2I;

        case PIPE_FORMAT_Z32_UNORM:
        case PIPE_FORMAT_Z24X8_UNORM:
        case PIPE_FORMAT_Z24_UNORM_S8_UINT:
                return MALI_Z32_UNORM;

        case PIPE_FORMAT_Z32_FLOAT_S8X24_UINT:
                /* Z32F = R32F to the hardware */
                return MALI_R32F;

        case PIPE_FORMAT_R3G3B2_UNORM:
                return MALI_RGB332_UNORM;

        case PIPE_FORMAT_B5G6R5_UNORM:
                return MALI_RGB565;

        case PIPE_FORMAT_B5G5R5X1_UNORM:
                return MALI_RGB5_X1_UNORM;

        case PIPE_FORMAT_B5G5R5A1_UNORM:
                return MALI_RGB5_A1_UNORM;

        case PIPE_FORMAT_A1B5G5R5_UNORM:
        case PIPE_FORMAT_X1B5G5R5_UNORM:
                /* Not supported - this is backwards from OpenGL! */
                assert(0);
                break;

        case PIPE_FORMAT_R32_FIXED:
                return MALI_R32_FIXED;
        case PIPE_FORMAT_R32G32_FIXED:
                return MALI_RG32_FIXED;
        case PIPE_FORMAT_R32G32B32_FIXED:
                return MALI_RGB32_FIXED;
        case PIPE_FORMAT_R32G32B32A32_FIXED:
                return MALI_RGBA32_FIXED;

        case PIPE_FORMAT_R11G11B10_FLOAT:
                return MALI_R11F_G11F_B10F;
        case PIPE_FORMAT_R9G9B9E5_FLOAT:
                return MALI_R9F_G9F_B9F_E5F;

        case PIPE_FORMAT_ETC1_RGB8:
        case PIPE_FORMAT_ETC2_RGB8:
        case PIPE_FORMAT_ETC2_SRGB8:
                return MALI_ETC2_RGB8;

        case PIPE_FORMAT_ETC2_RGB8A1:
        case PIPE_FORMAT_ETC2_SRGB8A1:
                return MALI_ETC2_RGB8A1;

        case PIPE_FORMAT_ETC2_RGBA8:
        case PIPE_FORMAT_ETC2_SRGBA8:
                return MALI_ETC2_RGBA8;

        case PIPE_FORMAT_ETC2_R11_UNORM:
                return MALI_ETC2_R11_UNORM;
        case PIPE_FORMAT_ETC2_R11_SNORM:
                return MALI_ETC2_R11_SNORM;

        case PIPE_FORMAT_ETC2_RG11_UNORM:
                return MALI_ETC2_RG11_UNORM;
        case PIPE_FORMAT_ETC2_RG11_SNORM:
                return MALI_ETC2_RG11_SNORM;

        default:
                /* Fallthrough to default */
                break;
        }

        if (desc->layout == UTIL_FORMAT_LAYOUT_ASTC) {
                if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB)
                        return MALI_ASTC_SRGB_SUPP;
                else
                        return MALI_ASTC_HDR_SUPP;
        }

        /* Formats must match in channel count */
        assert(desc->nr_channels >= 1 && desc->nr_channels <= 4);
        unsigned format = MALI_NR_CHANNELS(desc->nr_channels);

        switch (chan.type)
        {
        case UTIL_FORMAT_TYPE_UNSIGNED:
        case UTIL_FORMAT_TYPE_SIGNED:
        case UTIL_FORMAT_TYPE_FIXED:
                /* Channel width */
                format |= panfrost_translate_channel_width(chan.size);

                /* Channel type */
                format |= panfrost_translate_channel_type(chan.type, chan.size, chan.normalized);
                break;

        case UTIL_FORMAT_TYPE_FLOAT:
                /* Float formats use a special width and encode width
                 * with type mixed */

                format |= MALI_CHANNEL_FLOAT;
                format |= panfrost_translate_channel_type(chan.type, chan.size, chan.normalized);
                break;

        default:
                fprintf(stderr, "%s\n", util_format_name(desc->format));
                unreachable("Invalid format type");
        }

        return (enum mali_format) format;
}

/* Is a format encoded like Z24S8 and therefore compatible for render? */

bool
panfrost_is_z24s8_variant(enum pipe_format fmt)
{
        switch (fmt) {
                case PIPE_FORMAT_Z24_UNORM_S8_UINT:
                case PIPE_FORMAT_Z24X8_UNORM:
                        return true;
                default:
                        return false;
        }
}

/* Translate a PIPE swizzle quad to a 12-bit Mali swizzle code. PIPE
 * swizzles line up with Mali swizzles for the XYZW01, but PIPE swizzles have
 * an additional "NONE" field that we have to mask out to zero. Additionally,
 * PIPE swizzles are sparse but Mali swizzles are packed */

unsigned
panfrost_translate_swizzle_4(const unsigned char swizzle[4])
{
        unsigned out = 0;

        for (unsigned i = 0; i < 4; ++i) {
                unsigned translated = (swizzle[i] > PIPE_SWIZZLE_1) ? PIPE_SWIZZLE_0 : swizzle[i];
                out |= (translated << (3*i));
        }

        return out;
}

void
panfrost_invert_swizzle(const unsigned char *in, unsigned char *out)
{
        /* First, default to all zeroes to prevent uninitialized junk */

        for (unsigned c = 0; c < 4; ++c)
                out[c] = PIPE_SWIZZLE_0;

        /* Now "do" what the swizzle says */

        for (unsigned c = 0; c < 4; ++c) {
                unsigned char i = in[c];

                /* Who cares? */
                assert(PIPE_SWIZZLE_X == 0);
                if (i > PIPE_SWIZZLE_W)
                        continue;

                /* Invert */
                unsigned idx = i - PIPE_SWIZZLE_X;
                out[idx] = PIPE_SWIZZLE_X + c;
        }
}
