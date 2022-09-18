/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef ROGUE_NIR_H
#define ROGUE_NIR_H

#include "compiler/shader_enums.h"
#include "nir/nir.h"
#include "util/macros.h"

struct rogue_build_ctx;
struct rogue_compiler;
struct spirv_to_nir_options;

PUBLIC
const struct spirv_to_nir_options *
rogue_get_spirv_options(const struct rogue_compiler *compiler);

PUBLIC
const nir_shader_compiler_options *
rogue_get_compiler_options(const struct rogue_compiler *compiler);

bool rogue_nir_passes(struct rogue_build_ctx *ctx,
                      nir_shader *nir,
                      gl_shader_stage stage);

/* Custom passes. */
void rogue_nir_pfo(nir_shader *shader);
void rogue_nir_constreg(nir_shader *shader);
bool rogue_nir_lower_io(nir_shader *shader, void *layout);

#endif /* ROGUE_NIR_H */
