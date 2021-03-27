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
 *
 */

#ifndef __PAN_BLEND_CSO_H
#define __PAN_BLEND_CSO_H

#include "pan_blend.h"
#include "util/hash_table.h"
#include "nir.h"

struct panfrost_bo;

struct panfrost_blend_shader_key {
        /* RT format */
        enum pipe_format format;

        /* Render target */
        unsigned rt : 3;

        /* Blend shader uses blend constants */
        unsigned has_constants : 1;

        /* Logic Op info */
        unsigned logicop_enable : 1;
        unsigned logicop_func:4;

        /* Number of samples */
        unsigned nr_samples : 5;

        struct pipe_rt_blend_state equation;
};

/* An internal blend shader descriptor, from the compiler */

struct panfrost_blend_shader {
        struct panfrost_blend_shader_key key;
        struct panfrost_context *ctx;

        nir_shader *nir;

        /* Blend constants */
        float constants[4];

        /* The compiled shader */
        void *buffer;

        /* Byte count of the shader */
        unsigned size;

        /* Number of 128-bit work registers required by the shader */
        unsigned work_count;

        /* First instruction tag (for tagging the pointer) */
        unsigned first_tag;
};

/* A blend shader descriptor ready for actual use */

struct panfrost_blend_shader_final {
        /* GPU address where we're compiled to */
        uint64_t gpu;

        /* First instruction tag (for tagging the pointer) */
        unsigned first_tag;

        /* Same meaning as panfrost_blend_shader */
        unsigned work_count;
};

struct panfrost_blend_equation_final {
        struct MALI_BLEND_EQUATION equation;
        float constant;
};

struct panfrost_blend_state {
        struct pipe_blend_state base;
        struct pan_blend_state pan;
};

/* Container for a final blend state, specialized to constants and a
 * framebuffer formats. */

struct panfrost_blend_final {
        /* Set for a shader, clear for an equation */
        bool is_shader;

        /* Set if this is the replace mode */
        bool opaque;

        /* Set if destination is loaded */
        bool load_dest;

        /* Set if the colour mask is 0x0 (nothing is written) */
        bool no_colour;

        union {
                struct panfrost_blend_shader_final shader;
                struct panfrost_blend_equation_final equation;
        };
};

void
panfrost_blend_context_init(struct pipe_context *pipe);

struct panfrost_blend_final
panfrost_get_blend_for_context(struct panfrost_context *ctx, unsigned rt, struct panfrost_bo **bo, unsigned *shader_offset);

struct panfrost_blend_shader *
panfrost_get_blend_shader(struct panfrost_context *ctx,
                          struct panfrost_blend_state *blend,
                          enum pipe_format fmt, unsigned nr_samples,
                          unsigned rt,
                          const float *constants);

#endif
