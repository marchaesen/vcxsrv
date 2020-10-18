/*
 * Copyright © 2019 Collabora Ltd
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

#include "nir.h"
#include "nir_builder.h"

/** nir_lower_point_size_mov.c
 *
 * This pass lowers glPointSize into gl_PointSize, by adding a uniform
 * and a move from that uniform to VARYING_SLOT_PSIZ. This is useful for
 * OpenGL ES level hardware that lack constant point-size hardware state.
 */

static bool
lower_impl(nir_function_impl *impl,
           const gl_state_index16 *pointsize_state_tokens,
           nir_variable *out)
{
   nir_shader *shader = impl->function->shader;
   nir_builder b;
   nir_variable *in;

   nir_builder_init(&b, impl);
   b.cursor = nir_before_cf_list(&impl->body);

   in = nir_variable_create(shader, nir_var_uniform,
                            glsl_float_type(), "gl_PointSizeClampedMESA");
   in->num_state_slots = 1;
   in->state_slots = ralloc_array(in, nir_state_slot, 1);
   memcpy(in->state_slots[0].tokens,
         pointsize_state_tokens,
         sizeof(in->state_slots[0].tokens));

   if (!out) {
      out = nir_variable_create(shader, nir_var_shader_out,
                                glsl_float_type(), "gl_PointSize");
      out->data.location = VARYING_SLOT_PSIZ;
   }

   nir_copy_var(&b, out, in);

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
   return true;
}

void
nir_lower_point_size_mov(nir_shader *shader,
                         const gl_state_index16 *pointsize_state_tokens)
{
   assert(shader->info.stage != MESA_SHADER_FRAGMENT &&
          shader->info.stage != MESA_SHADER_COMPUTE);

   nir_variable *out =
      nir_find_variable_with_location(shader, nir_var_shader_out,
                                      VARYING_SLOT_PSIZ);

   lower_impl(nir_shader_get_entrypoint(shader), pointsize_state_tokens,
              out);
}
