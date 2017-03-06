/*
 * Copyright © 2017 Timothy Arceri
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "st_context.h"
#include "compiler/glsl/blob.h"
#include "main/mtypes.h"
#include "pipe/p_state.h"
#include "util/disk_cache.h"
#include "util/mesa-sha1.h"

#ifdef __cplusplus
extern "C" {
#endif

bool
st_load_tgsi_from_disk_cache(struct gl_context *ctx,
                             struct gl_shader_program *prog);

void
st_store_tgsi_in_disk_cache(struct st_context *st, struct gl_program *prog,
                            struct pipe_shader_state *out_state,
                            unsigned num_tokens);

#ifdef __cplusplus
}
#endif
