/*
 * Copyright (C) 2019 Andreas Baierl
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

/* Lower gl_FragCoord and transform the w component
 * according to the following pseudocode:
 *
 *    gl_FragCoord.xyz = gl_FragCoord_orig.xyz
 *    gl_FragCoord.w = 1.0 / gl_FragCoord_orig.w
 *
 */

static bool
lower_fragcoord_wtrans(nir_builder *b, nir_intrinsic_instr *intr,
                       UNUSED void *_options)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_load_deref: {
      nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
      if (!nir_deref_mode_must_be(deref, nir_var_shader_in))
         return false;
      nir_variable *var = nir_intrinsic_get_var(intr, 0);
      if (var->data.location != VARYING_SLOT_POS)
         return false;
      break;
   }
   case nir_intrinsic_load_frag_coord:
      break;
   default:
      return false;
   }

   /* W is not read. */
   if (intr->def.num_components < 4)
      return false;

   b->cursor = nir_after_instr(&intr->instr);

   nir_def *invert = nir_frcp(b, nir_channel(b, &intr->def, 3));

   nir_def *frag_coord = nir_vector_insert_imm(b, &intr->def, invert, 3);

   nir_def_rewrite_uses_after(&intr->def, frag_coord,
                              frag_coord->parent_instr);

   return true;
}

bool
nir_lower_fragcoord_wtrans(nir_shader *shader)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);

   return nir_shader_intrinsics_pass(shader,
                                     lower_fragcoord_wtrans,
                                     nir_metadata_control_flow,
                                     NULL);
}
