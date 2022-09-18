/*
 * Copyright (c) 2020 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Christian Gmeiner <christian.gmeiner@gmail.com>
 */

#include "etnaviv_compiler.h"
#include "etnaviv_compiler_nir.h"
#include "etnaviv_debug.h"
#include "etnaviv_disk_cache.h"
#include "util/ralloc.h"

struct etna_compiler *
etna_compiler_create(const char *renderer, const struct etna_specs *specs)
{
   struct etna_compiler *compiler = rzalloc(NULL, struct etna_compiler);

   compiler->options = (nir_shader_compiler_options) {
      .lower_fpow = true,
      .lower_ftrunc = true,
      .fuse_ffma16 = true,
      .fuse_ffma32 = true,
      .fuse_ffma64 = true,
      .lower_bitops = true,
      .lower_all_io_to_temps = true,
      .vertex_id_zero_based = true,
      .lower_flrp32 = true,
      .lower_fmod = true,
      .lower_vector_cmp = true,
      .lower_fdph = true,
      .lower_insert_byte = true,
      .lower_insert_word = true,
      .lower_fdiv = true, /* !specs->has_new_transcendentals */
      .lower_fsign = !specs->has_sign_floor_ceil,
      .lower_ffloor = !specs->has_sign_floor_ceil,
      .lower_fceil = !specs->has_sign_floor_ceil,
      .lower_fsqrt = !specs->has_sin_cos_sqrt,
      .lower_sincos = !specs->has_sin_cos_sqrt,
      .lower_uniforms_to_ubo = specs->halti >= 2,
      .force_indirect_unrolling = nir_var_all,
      .max_unroll_iterations = 32,
      .vectorize_io = true,
   };

   compiler->regs = etna_ra_setup(compiler);
   if (!compiler->regs) {
      ralloc_free((void *)compiler);
      compiler = NULL;
   }

   etna_disk_cache_init(compiler, renderer);

   return compiler;
}

void
etna_compiler_destroy(const struct etna_compiler *compiler)
{
   ralloc_free((void *)compiler);
}

const nir_shader_compiler_options *
etna_compiler_get_options(struct etna_compiler *compiler)
{
   return &compiler->options;
}
