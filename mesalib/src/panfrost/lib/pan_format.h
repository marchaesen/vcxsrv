/*
 * Copyright (C) 2008 VMware, Inc.
 * Copyright (C) 2014 Broadcom
 * Copyright (C) 2018-2019 Alyssa Rosenzweig
 * Copyright (C) 2019-2020 Collabora, Ltd.
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

#ifndef __PAN_FORMAT_H
#define __PAN_FORMAT_H

#include "gen_macros.h"

#include "util/format/u_format.h"

/* Formats */

typedef uint32_t mali_pixel_format;

struct panfrost_format {
        mali_pixel_format hw;
        unsigned bind;
};

struct pan_blendable_format {
        /* enum mali_color_buffer_internal_format */ uint16_t internal;
        /* enum mali_mfbd_color_format */ uint16_t writeback;

        /* Indexed by the dithered? flag. So _PU first, then _AU */
        mali_pixel_format bifrost[2];
};

extern const struct pan_blendable_format panfrost_blendable_formats_v6[PIPE_FORMAT_COUNT];
extern const struct pan_blendable_format panfrost_blendable_formats_v7[PIPE_FORMAT_COUNT];
extern const struct panfrost_format panfrost_pipe_format_v6[PIPE_FORMAT_COUNT];
extern const struct panfrost_format panfrost_pipe_format_v7[PIPE_FORMAT_COUNT];

/* Helpers to construct swizzles */

#define PAN_V6_SWIZZLE(R, G, B, A) ( \
        ((MALI_CHANNEL_ ## R) << 0) | \
        ((MALI_CHANNEL_ ## G) << 3) | \
        ((MALI_CHANNEL_ ## B) << 6) | \
        ((MALI_CHANNEL_ ## A) << 9))

static inline unsigned
panfrost_get_default_swizzle(unsigned components)
{
        switch (components) {
        case 1:
                return PAN_V6_SWIZZLE(R, 0, 0, 1);
        case 2:
                return PAN_V6_SWIZZLE(R, G, 0, 1);
        case 3:
                return PAN_V6_SWIZZLE(R, G, B, 1);
        case 4:
                return PAN_V6_SWIZZLE(R, G, B, A);
        default:
                unreachable("Invalid number of components");
        }
}

#endif
