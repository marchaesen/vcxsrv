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
 * Authors (Collabora):
 *   Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#ifndef __PAN_ENCODER_H
#define __PAN_ENCODER_H

#include "panfrost-job.h"

/* Invocation packing */

void
panfrost_pack_work_groups_compute(
        struct mali_vertex_tiler_prefix *out,
        unsigned num_x,
        unsigned num_y,
        unsigned num_z,
        unsigned size_x,
        unsigned size_y,
        unsigned size_z,
        bool quirk_graphics);

void
panfrost_pack_work_groups_fused(
        struct mali_vertex_tiler_prefix *vertex,
        struct mali_vertex_tiler_prefix *tiler,
        unsigned num_x,
        unsigned num_y,
        unsigned num_z,
        unsigned size_x,
        unsigned size_y,
        unsigned size_z);

/* Tiler structure size computation */

unsigned
panfrost_tiler_header_size(unsigned width, unsigned height, unsigned mask);

unsigned
panfrost_tiler_full_size(unsigned width, unsigned height, unsigned mask);

unsigned
panfrost_choose_hierarchy_mask(
        unsigned width, unsigned height,
        unsigned vertex_count);

#endif
