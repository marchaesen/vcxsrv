/*
 * © Copyright 2018 Alyssa Rosenzweig
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

#ifndef __PAN_BLEND_SHADERS_H__
#define __PAN_BLEND_SHADERS_H__

#include "pipe/p_state.h"
#include "pipe/p_defines.h"
#include <midgard_pack.h>
#include "pan_context.h"
#include "pan_blend_cso.h"

struct panfrost_blend_shader *
panfrost_create_blend_shader(struct panfrost_context *ctx,
                             struct panfrost_blend_state *state,
                             const struct panfrost_blend_shader_key *key);

void
panfrost_compile_blend_shader(struct panfrost_blend_shader *shader,
                              const float *constants);

uint64_t
bifrost_get_blend_desc(const struct panfrost_device *dev,
                       enum pipe_format fmt, unsigned rt, unsigned force_size);

#endif
