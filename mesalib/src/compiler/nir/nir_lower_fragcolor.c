/*
 * Copyright © 2020 Mike Blumenkrantz
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
 * 
 * Authors:
 *    Mike Blumenkrantz <michael.blumenkrantz@gmail.com>
 */

#include "nir.h"
#include "nir_builder.h"

/**
 * This pass splits gl_FragColor into gl_FragData[0-7] for drivers which handle
 * the former but not the latter, e.g., zink.
 */

/*
 If a fragment shader writes to "gl_FragColor", DrawBuffersIndexedEXT
 specifies a set of draw buffers into which the color written to
 "gl_FragColor" is written. If a fragment shader writes to
 gl_FragData, DrawBuffersIndexedEXT specifies a set of draw buffers
 into which each of the multiple output colors defined by these
 variables are separately written. If a fragment shader writes to
 neither gl_FragColor nor gl_FragData, the values of the fragment
 colors following shader execution are undefined, and may differ
 for each fragment color.

 - EXT_multiview_draw_buffers

 */

static bool
lower_fragcolor_instr(nir_intrinsic_instr *instr, nir_builder *b)
{
   nir_variable *out;
   if (instr->intrinsic != nir_intrinsic_store_deref)
      return false;

   out = nir_deref_instr_get_variable(nir_src_as_deref(instr->src[0]));
   if (out->data.location != FRAG_RESULT_COLOR || out->data.mode != nir_var_shader_out)
      return false;
   b->cursor = nir_after_instr(&instr->instr);

   nir_ssa_def *frag_color = nir_load_var(b, out);
   ralloc_free(out->name);
   out->name = ralloc_strdup(out, "gl_FragData[0]");

   /* translate gl_FragColor -> gl_FragData since this is already handled */
   out->data.location = FRAG_RESULT_DATA0;
   nir_component_mask_t writemask = nir_intrinsic_write_mask(instr);

   for (unsigned i = 1; i < 8; i++) {
      char name[16];
      snprintf(name, sizeof(name), "gl_FragData[%u]", i);
      nir_variable *out_color = nir_variable_create(b->shader, nir_var_shader_out,
                                                   glsl_vec4_type(),
                                                   name);
      out_color->data.location = FRAG_RESULT_DATA0 + i;
      out_color->data.driver_location = i;
      out_color->data.index = out->data.index;
      nir_store_var(b, out_color, frag_color, writemask);
   }
   return true;
}

bool
nir_lower_fragcolor(nir_shader *shader)
{
   bool progress = false;

   if (shader->info.stage != MESA_SHADER_FRAGMENT)
      return false;

   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_builder builder;
         nir_builder_init(&builder, function->impl);
         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               if (instr->type == nir_instr_type_intrinsic)
                  progress |= lower_fragcolor_instr(nir_instr_as_intrinsic(instr),
                                                    &builder);
            }
         }

         nir_metadata_preserve(function->impl, nir_metadata_block_index | nir_metadata_dominance);
      }
   }

   return progress;
}
