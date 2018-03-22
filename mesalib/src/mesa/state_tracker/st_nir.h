/*
 * Copyright © 2016 Red Hat
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

#ifndef ST_NIR_H
#define ST_NIR_H

#include "st_context.h"
#include "compiler/shader_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nir_shader;

void st_nir_lower_builtin(struct nir_shader *shader);
void st_nir_lower_tex_src_plane(struct nir_shader *shader, unsigned free_slots,
                                unsigned lower_2plane, unsigned lower_3plane);
bool st_nir_lower_uniforms_to_ubo(struct nir_shader *shader,
                                  const struct gl_program_parameter_list *params);

void st_finalize_nir(struct st_context *st, struct gl_program *prog,
                     struct gl_shader_program *shader_program,
                     struct nir_shader *nir);

bool
st_link_nir(struct gl_context *ctx,
            struct gl_shader_program *shader_program);

#ifdef __cplusplus
}
#endif

#endif /* ST_NIR_H */
