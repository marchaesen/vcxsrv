/*
 * Copyright © 2016 Intel Corporation
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

#include "nir_builder.h"

void
nir_lower_tes_patch_vertices(nir_shader *tes_nir, unsigned patch_vertices)
{
   nir_foreach_function(function, tes_nir) {
      if (function->impl) {
         nir_foreach_block(block, function->impl) {
            nir_builder b;
            nir_builder_init(&b, function->impl);
            nir_foreach_instr_safe(instr, block) {
               if (instr->type == nir_instr_type_intrinsic) {
                  nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
                  if (intr->intrinsic != nir_intrinsic_load_patch_vertices_in)
                     continue;

                  b.cursor = nir_before_instr(&intr->instr);
                  nir_ssa_def *val = nir_imm_int(&b, patch_vertices);
                  nir_ssa_def_rewrite_uses(&intr->dest.ssa,
                                           nir_src_for_ssa(val));
                  nir_instr_remove(instr);
               }
            }
         }

         nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                               nir_metadata_dominance);
      }
   }
}
