/*
 * Copyright (C) 2018 Alyssa Rosenzweig
 * Copyright (C) 2019-2021 Collabora, Ltd.
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
 */

#ifndef __PAN_BLEND_H__
#define __PAN_BLEND_H__

#include "util/u_dynarray.h"
#include "util/format/u_format.h"
#include "compiler/shader_enums.h"
#include "compiler/nir/nir.h"

#include "panfrost/util/pan_ir.h"

struct MALI_BLEND_EQUATION;
struct panfrost_device;

struct pan_blend_equation {
        unsigned blend_enable : 1;
        enum blend_func rgb_func : 3;
        unsigned rgb_invert_src_factor : 1;
        enum blend_factor rgb_src_factor : 4;
        unsigned rgb_invert_dst_factor : 1;
        enum blend_factor rgb_dst_factor : 4;
        enum blend_func alpha_func : 3;
        unsigned alpha_invert_src_factor : 1;
        enum blend_factor alpha_src_factor : 4;
        unsigned alpha_invert_dst_factor : 1;
        enum blend_factor alpha_dst_factor : 4;
        unsigned color_mask : 4;
};

struct pan_blend_rt_state {
        /* RT format */
        enum pipe_format format;

        /* Number of samples */
        unsigned nr_samples;

        struct pan_blend_equation equation;
};

struct pan_blend_state {
        bool dither;
        bool logicop_enable;
        enum pipe_logicop logicop_func;
        float constants[4];
        unsigned rt_count;
        struct pan_blend_rt_state rts[8];
};

bool
pan_blend_reads_dest(const struct pan_blend_state *state, unsigned rt);

bool
pan_blend_can_fixed_function(const struct panfrost_device *dev,
                             const struct pan_blend_state *state,
                             unsigned rt);

bool
pan_blend_is_opaque(const struct pan_blend_state *state,
                    unsigned rt);

unsigned
pan_blend_constant_mask(const struct pan_blend_state *state,
                        unsigned rt);

float
pan_blend_get_constant(const struct panfrost_device *dev,
                       const struct pan_blend_state *state,
                       unsigned rt);

void
pan_blend_to_fixed_function_equation(const struct panfrost_device *dev,
                                     const struct pan_blend_state *state,
                                     unsigned rt,
                                     struct MALI_BLEND_EQUATION *equation);

nir_shader *
pan_blend_create_shader(const struct panfrost_device *dev,
                        const struct pan_blend_state *state,
                        unsigned rt);

#endif
