/*
 * Copyright © 2017 Intel Corporation
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef GL_NIR_LINKER_H
#define GL_NIR_LINKER_H

#ifdef __cplusplus
extern "C" {
#endif

struct gl_constants;
struct gl_extensions;
struct gl_shader_program;

struct gl_nir_linker_options {
   bool fill_parameters;
};

#define nir_foreach_gl_uniform_variable(var, shader) \
   nir_foreach_variable_with_modes(var, shader, nir_var_uniform | \
                                                nir_var_mem_ubo | \
                                                nir_var_mem_ssbo | \
                                                nir_var_image)

void gl_nir_opts(nir_shader *nir);

bool gl_nir_link_spirv(const struct gl_constants *consts,
                       struct gl_shader_program *prog,
                       const struct gl_nir_linker_options *options);

bool gl_nir_link_glsl(const struct gl_constants *consts,
                      const struct gl_extensions *exts,
                      struct gl_shader_program *prog);

bool gl_nir_link_uniforms(const struct gl_constants *consts,
                          struct gl_shader_program *prog,
                          bool fill_parameters);

void gl_nir_set_uniform_initializers(const struct gl_constants *consts,
                                     struct gl_shader_program *prog);

void nir_build_program_resource_list(const struct gl_constants *consts,
                                     struct gl_shader_program *prog,
                                     bool rebuild_resourse_list);

void gl_nir_link_assign_atomic_counter_resources(const struct gl_constants *consts,
                                                 struct gl_shader_program *prog);

void gl_nir_link_check_atomic_counter_resources(const struct gl_constants *consts,
                                                struct gl_shader_program *prog);

void gl_nir_link_assign_xfb_resources(const struct gl_constants *consts,
                                      struct gl_shader_program *prog);

bool gl_nir_link_uniform_blocks(struct gl_shader_program *prog);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* GL_NIR_LINKER_H */
