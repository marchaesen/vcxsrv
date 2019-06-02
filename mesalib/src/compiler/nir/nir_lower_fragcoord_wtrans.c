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
 * To trigger the transformation, gl_FragCoord currently has to be treated
 * as a system value with PIPE_CAP_TGSI_FS_POSITION_IS_SYSVAL enabled.
 */

static void
lower_fragcoord_wtrans(nir_builder *b, nir_intrinsic_instr *intr)
{
   assert(intr->dest.is_ssa);

   b->cursor = nir_before_instr(&intr->instr);

   nir_ssa_def *fragcoord_in = nir_load_frag_coord(b);
   nir_ssa_def *w_rcp = nir_frcp(b, nir_channel(b, fragcoord_in, 3));
   nir_ssa_def *fragcoord_wtrans = nir_vec4(b,
                                            nir_channel(b, fragcoord_in, 0),
                                            nir_channel(b, fragcoord_in, 1),
                                            nir_channel(b, fragcoord_in, 2),
                                            w_rcp);
   nir_ssa_def_rewrite_uses(&intr->dest.ssa,
                            nir_src_for_ssa(fragcoord_wtrans));
}

void
nir_lower_fragcoord_wtrans(nir_shader *shader)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);

   nir_foreach_function(func, shader) {
      if (!func->impl)
         continue;

      nir_builder b;
      nir_builder_init(&b, func->impl);
      nir_foreach_block(block, func->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic != nir_intrinsic_load_frag_coord)
               continue;

            lower_fragcoord_wtrans(&b, intr);
         }
      }
      nir_metadata_preserve(func->impl, nir_metadata_block_index |
                            nir_metadata_dominance);
   }
}
