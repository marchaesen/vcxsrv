/*
 * Copyright © 2015 Red Hat
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

#include "nir.h"
#include "nir_builder.h"

static void
lower_impl(nir_function_impl *impl)
{
   nir_shader *shader = impl->function->shader;
   nir_builder b;
   nir_variable *in, *out;
   nir_ssa_def *def;

   nir_builder_init(&b, impl);
   b.cursor = nir_before_cf_list(&impl->body);

   in  = nir_variable_create(shader, nir_var_shader_in,
                             glsl_vec4_type(), "edgeflag_in");
   in->data.location = VERT_ATTRIB_EDGEFLAG;

   out = nir_variable_create(shader, nir_var_shader_out,
                             glsl_vec4_type(), "edgeflag_out");
   out->data.location = VARYING_SLOT_EDGE;

   def = nir_load_var(&b, in);
   nir_store_var(&b, out, def, 0xf);

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
}

void nir_lower_passthrough_edgeflags(nir_shader *shader)
{
   lower_impl(nir_shader_get_entrypoint(shader));
}
