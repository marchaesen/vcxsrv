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

#include <stdbool.h>
#include "pan_bo.h"
#include "midgard_pack.h"

/* Indices for named (non-XFB) varyings that are present. These are packed
 * tightly so they correspond to a bitfield present (P) indexed by (1 <<
 * PAN_VARY_*). This has the nice property that you can lookup the buffer index
 * of a given special field given a shift S by:
 *
 *      idx = popcount(P & ((1 << S) - 1))
 *
 * That is... look at all of the varyings that come earlier and count them, the
 * count is the new index since plus one. Likewise, the total number of special
 * buffers required is simply popcount(P)
 */

enum pan_special_varying {
        PAN_VARY_GENERAL = 0,
        PAN_VARY_POSITION = 1,
        PAN_VARY_PSIZ = 2,
        PAN_VARY_PNTCOORD = 3,
        PAN_VARY_FACE = 4,
        PAN_VARY_FRAGCOORD = 5,

        /* Keep last */
        PAN_VARY_MAX,
};

/* Invocation packing */

void
panfrost_pack_work_groups_compute(
        struct mali_invocation_packed *out,
        unsigned num_x,
        unsigned num_y,
        unsigned num_z,
        unsigned size_x,
        unsigned size_y,
        unsigned size_z,
        bool quirk_graphics);

/* Tiler structure size computation */

struct panfrost_device;

unsigned
panfrost_tiler_get_polygon_list_size(const struct panfrost_device *dev,
                                     unsigned fb_width, unsigned fb_height,
                                     bool has_draws);

unsigned
panfrost_tiler_header_size(unsigned width, unsigned height, unsigned mask, bool hierarchy);

unsigned
panfrost_tiler_full_size(unsigned width, unsigned height, unsigned mask, bool hierarchy);

unsigned
panfrost_choose_hierarchy_mask(
        unsigned width, unsigned height,
        unsigned vertex_count, bool hierarchy);

/* Stack sizes */

unsigned
panfrost_get_stack_shift(unsigned stack_size);

unsigned
panfrost_get_total_stack_size(
                unsigned thread_size,
                unsigned threads_per_core,
                unsigned core_count);

const char * panfrost_model_name(unsigned gpu_id);

/* Attributes / instancing */

unsigned
panfrost_padded_vertex_count(unsigned vertex_count);

unsigned
panfrost_compute_magic_divisor(unsigned hw_divisor, unsigned *o_shift, unsigned *extra_flags);

void panfrost_vertex_id(unsigned padded_count, struct mali_attribute_buffer_packed *attr, bool instanced);
void panfrost_instance_id(unsigned padded_count, struct mali_attribute_buffer_packed *attr, bool instanced);

/* Samplers */

enum mali_func
panfrost_flip_compare_func(enum mali_func f);



#endif
