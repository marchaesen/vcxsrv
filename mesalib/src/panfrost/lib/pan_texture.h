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

#include "genxml/gen_macros.h"

#include <stdbool.h>
#include "drm-uapi/drm_fourcc.h"
#include "util/format/u_format.h"
#include "compiler/shader_enums.h"
#include "genxml/gen_macros.h"
#include "pan_bo.h"
#include "pan_device.h"
#include "pan_util.h"
#include "pan_format.h"

#define PAN_MODIFIER_COUNT 4
extern uint64_t pan_best_modifiers[PAN_MODIFIER_COUNT];

struct pan_image_slice_layout {
        unsigned offset;
        unsigned line_stride;
        unsigned row_stride;
        unsigned surface_stride;

        struct {
                /* Size of the AFBC header preceding each slice */
                unsigned header_size;

                /* Size of the AFBC body */
                unsigned body_size;

                /* Stride between two rows of AFBC headers */
                unsigned row_stride;

                /* Stride between AFBC headers of two consecutive surfaces.
                 * For 3D textures, this must be set to header size since
                 * AFBC headers are allocated together, for 2D arrays this
                 * should be set to size0, since AFBC headers are placed at
                 * the beginning of each layer
                 */
                unsigned surface_stride;
        } afbc;

        /* If checksumming is enabled following the slice, what
         * is its offset/stride? */
        struct {
                unsigned offset;
                unsigned stride;
                unsigned size;
        } crc;

        unsigned size;
};

enum pan_image_crc_mode {
      PAN_IMAGE_CRC_NONE,
      PAN_IMAGE_CRC_INBAND,
      PAN_IMAGE_CRC_OOB,
};

struct pan_image_layout {
        uint64_t modifier;
        enum pipe_format format;
        unsigned width, height, depth;
        unsigned nr_samples;
        enum mali_texture_dimension dim;
        unsigned nr_slices;
        struct pan_image_slice_layout slices[MAX_MIP_LEVELS];
        unsigned array_size;
        unsigned array_stride;
        unsigned data_size;

        enum pan_image_crc_mode crc_mode;
        /* crc_size != 0 only if crc_mode == OOB otherwise CRC words are
         * counted in data_size */
        unsigned crc_size;
};

struct pan_image_mem {
        struct panfrost_bo *bo;
        unsigned offset;
};

struct pan_image {
        struct pan_image_mem data;
        struct pan_image_mem crc;
        struct pan_image_layout layout;
};

struct pan_image_view {
        /* Format, dimension and sample count of the view might differ from
         * those of the image (2D view of a 3D image surface for instance).
         */
        enum pipe_format format;
        enum mali_texture_dimension dim;
        unsigned first_level, last_level;
        unsigned first_layer, last_layer;
        unsigned char swizzle[4];
        const struct pan_image *image;

        /* If EXT_multisampled_render_to_texture is used, this may be
         * greater than image->layout.nr_samples. */
        unsigned nr_samples;

        /* Only valid if dim == 1D, needed to implement buffer views */
        struct {
                unsigned offset;
                unsigned size;
        } buf;
};

unsigned
panfrost_compute_checksum_size(
        struct pan_image_slice_layout *slice,
        unsigned width,
        unsigned height);

/* AFBC */

bool
panfrost_format_supports_afbc(const struct panfrost_device *dev,
                enum pipe_format format);

enum pipe_format
panfrost_afbc_format(const struct panfrost_device *dev, enum pipe_format format);

#define AFBC_HEADER_BYTES_PER_TILE 16

unsigned
panfrost_afbc_header_size(unsigned width, unsigned height);

bool
panfrost_afbc_can_ytr(enum pipe_format format);

unsigned
panfrost_block_dim(uint64_t modifier, bool width, unsigned plane);

#ifdef PAN_ARCH
unsigned
GENX(panfrost_estimate_texture_payload_size)(const struct pan_image_view *iview);

void
GENX(panfrost_new_texture)(const struct panfrost_device *dev,
                           const struct pan_image_view *iview,
                           void *out,
                           const struct panfrost_ptr *payload);
#endif

unsigned
panfrost_get_layer_stride(const struct pan_image_layout *layout,
                          unsigned level);

unsigned
panfrost_texture_offset(const struct pan_image_layout *layout,
                        unsigned level, unsigned array_idx,
                        unsigned surface_idx);

struct pan_pool;
struct pan_scoreboard;

/* DRM modifier helper */

#define drm_is_afbc(mod) \
        ((mod >> 52) == (DRM_FORMAT_MOD_ARM_TYPE_AFBC | \
                (DRM_FORMAT_MOD_VENDOR_ARM << 4)))

struct pan_image_explicit_layout {
        unsigned offset;
        unsigned line_stride;
};

bool
pan_image_layout_init(const struct panfrost_device *dev,
                      struct pan_image_layout *layout,
                      uint64_t modifier,
                      enum pipe_format format,
                      enum mali_texture_dimension dim,
                      unsigned width, unsigned height, unsigned depth,
                      unsigned array_size, unsigned nr_samples,
                      unsigned nr_slices,
                      enum pan_image_crc_mode crc_mode,
                      const struct pan_image_explicit_layout *explicit_layout);

struct pan_surface {
        union {
                mali_ptr data;
                struct {
                        mali_ptr header;
                        mali_ptr body;
                } afbc;
        };
};

void
pan_iview_get_surface(const struct pan_image_view *iview,
                      unsigned level, unsigned layer, unsigned sample,
                      struct pan_surface *surf);

#endif
