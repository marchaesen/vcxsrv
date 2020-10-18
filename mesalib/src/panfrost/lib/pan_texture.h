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
#include "drm-uapi/drm_fourcc.h"
#include "util/format/u_format.h"
#include "compiler/shader_enums.h"
#include "midgard_pack.h"
#include "pan_bo.h"

#define PAN_MODIFIER_COUNT 4
extern uint64_t pan_best_modifiers[PAN_MODIFIER_COUNT];

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
        struct panfrost_bo *checksum_bo;

        /* Has anything been written to this slice? */
        bool initialized;
};

struct pan_image {
        /* Format and size */
        uint16_t width0, height0, depth0, array_size;
        enum pipe_format format;
        enum mali_texture_dimension dim;
        unsigned first_level, last_level;
        unsigned first_layer, last_layer;
        unsigned nr_samples;
        struct panfrost_bo *bo;
        struct panfrost_slice *slices;
        unsigned cubemap_stride;
        uint64_t modifier;
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

bool
panfrost_afbc_can_ytr(enum pipe_format format);

unsigned
panfrost_estimate_texture_payload_size(
                unsigned first_level, unsigned last_level,
                unsigned first_layer, unsigned last_layer,
                unsigned nr_samples,
                enum mali_texture_dimension dim, uint64_t modifier);

void
panfrost_new_texture(
        void *out,
        uint16_t width, uint16_t height,
        uint16_t depth, uint16_t array_size,
        enum pipe_format format,
        enum mali_texture_dimension dim,
        uint64_t modifier,
        unsigned first_level, unsigned last_level,
        unsigned first_layer, unsigned last_layer,
        unsigned nr_samples,
        unsigned cube_stride,
        unsigned swizzle,
        mali_ptr base,
        struct panfrost_slice *slices);

void
panfrost_new_texture_bifrost(
        const struct panfrost_device *dev,
        struct mali_bifrost_texture_packed *out,
        uint16_t width, uint16_t height,
        uint16_t depth, uint16_t array_size,
        enum pipe_format format,
        enum mali_texture_dimension dim,
        uint64_t modifier,
        unsigned first_level, unsigned last_level,
        unsigned first_layer, unsigned last_layer,
        unsigned nr_samples,
        unsigned cube_stride,
        unsigned swizzle,
        mali_ptr base,
        struct panfrost_slice *slices,
        struct panfrost_bo *payload);


unsigned
panfrost_get_layer_stride(struct panfrost_slice *slices, bool is_3d, unsigned cube_stride, unsigned level);

unsigned
panfrost_texture_offset(struct panfrost_slice *slices, bool is_3d, unsigned cube_stride, unsigned level, unsigned face, unsigned sample);

/* Formats */

struct panfrost_format {
        enum mali_format hw;
        unsigned bind;
};

extern struct panfrost_format panfrost_pipe_format_table[PIPE_FORMAT_COUNT];

bool
panfrost_is_z24s8_variant(enum pipe_format fmt);

enum mali_z_internal_format
panfrost_get_z_internal_format(enum pipe_format fmt);

unsigned
panfrost_translate_swizzle_4(const unsigned char swizzle[4]);

void
panfrost_invert_swizzle(const unsigned char *in, unsigned char *out);

static inline unsigned
panfrost_get_default_swizzle(unsigned components)
{
        switch (components) {
        case 1:
                return (MALI_CHANNEL_R << 0) | (MALI_CHANNEL_0 << 3) |
                        (MALI_CHANNEL_0 << 6) | (MALI_CHANNEL_1 << 9);
        case 2:
                return (MALI_CHANNEL_R << 0) | (MALI_CHANNEL_G << 3) |
                        (MALI_CHANNEL_0 << 6) | (MALI_CHANNEL_1 << 9);
        case 3:
                return (MALI_CHANNEL_R << 0) | (MALI_CHANNEL_G << 3) |
                        (MALI_CHANNEL_B << 6) | (MALI_CHANNEL_1 << 9);
        case 4:
                return (MALI_CHANNEL_R << 0) | (MALI_CHANNEL_G << 3) |
                        (MALI_CHANNEL_B << 6) | (MALI_CHANNEL_A << 9);
        default:
                unreachable("Invalid number of components");
        }
}

static inline unsigned
panfrost_bifrost_swizzle(unsigned components)
{
        /* Set all components to 0 and force w if needed */
        return components < 4 ? 0x10 : 0x00;
}

enum mali_format
panfrost_format_to_bifrost_blend(const struct util_format_description *desc);

struct pan_pool;
struct pan_scoreboard;

void
panfrost_init_blit_shaders(struct panfrost_device *dev);

void
panfrost_load_midg(
                struct pan_pool *pool,
                struct pan_scoreboard *scoreboard,
                mali_ptr blend_shader,
                mali_ptr fbd,
                mali_ptr coordinates, unsigned vertex_count,
                struct pan_image *image,
                unsigned loc);

/* DRM modifier helper */

#define drm_is_afbc(mod) \
        ((mod >> 52) == (DRM_FORMAT_MOD_ARM_TYPE_AFBC | \
                (DRM_FORMAT_MOD_VENDOR_ARM << 4)))

#endif
