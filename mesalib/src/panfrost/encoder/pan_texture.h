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

#ifndef __PAN_TEXTURE_H
#define __PAN_TEXTURE_H

#include <stdbool.h>
#include "util/format/u_format.h"
#include "panfrost-job.h"

struct panfrost_slice {
        unsigned offset;
        unsigned stride;
        unsigned size0;

        /* If there is a header preceding each slice, how big is
         * that header? Used for AFBC */
        unsigned header_size;

        /* If checksumming is enabled following the slice, what
         * is its offset/stride? */
        unsigned checksum_offset;
        unsigned checksum_stride;

        /* Has anything been written to this slice? */
        bool initialized;
};

unsigned
panfrost_compute_checksum_size(
        struct panfrost_slice *slice,
        unsigned width,
        unsigned height);

/* AFBC */

bool
panfrost_format_supports_afbc(enum pipe_format format);

unsigned
panfrost_afbc_header_size(unsigned width, unsigned height);

/* mali_texture_descriptor */

unsigned
panfrost_estimate_texture_size(
                unsigned first_level, unsigned last_level,
                unsigned first_layer, unsigned last_layer,
                enum mali_texture_type type, enum mali_texture_layout layout);

void
panfrost_new_texture(
        void *out,
        uint16_t width, uint16_t height,
        uint16_t depth, uint16_t array_size,
        enum pipe_format format,
        enum mali_texture_type type,
        enum mali_texture_layout layout,
        unsigned first_level, unsigned last_level,
        unsigned first_layer, unsigned last_layer,
        unsigned cube_stride,
        unsigned swizzle,
        mali_ptr base,
        struct panfrost_slice *slices);


unsigned
panfrost_get_layer_stride(struct panfrost_slice *slices, bool is_3d, unsigned cube_stride, unsigned level);

unsigned
panfrost_texture_offset(struct panfrost_slice *slices, bool is_3d, unsigned cube_stride, unsigned level, unsigned face);

/* Formats */

enum mali_format
panfrost_find_format(const struct util_format_description *desc);

bool
panfrost_is_z24s8_variant(enum pipe_format fmt);

unsigned
panfrost_translate_swizzle_4(const unsigned char swizzle[4]);

void
panfrost_invert_swizzle(const unsigned char *in, unsigned char *out);

static inline unsigned
panfrost_get_default_swizzle(unsigned components)
{
        switch (components) {
        case 1:
                return (MALI_CHANNEL_RED << 0) | (MALI_CHANNEL_ZERO << 3) |
                        (MALI_CHANNEL_ZERO << 6) | (MALI_CHANNEL_ONE << 9);
        case 2:
                return (MALI_CHANNEL_RED << 0) | (MALI_CHANNEL_GREEN << 3) |
                        (MALI_CHANNEL_ZERO << 6) | (MALI_CHANNEL_ONE << 9);
        case 3:
                return (MALI_CHANNEL_RED << 0) | (MALI_CHANNEL_GREEN << 3) |
                        (MALI_CHANNEL_BLUE << 6) | (MALI_CHANNEL_ONE << 9);
        case 4:
                return (MALI_CHANNEL_RED << 0) | (MALI_CHANNEL_GREEN << 3) |
                        (MALI_CHANNEL_BLUE << 6) | (MALI_CHANNEL_ALPHA << 9);
        default:
                unreachable("Invalid number of components");
        }
}

#endif
