/*
 * Copyright © 2015 Connor Abbott
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
 *    Connor Abbott (cwabbott0@gmail.com)
 *
 */

#include "nir.h"

/*
 * This is a pass for removing phi nodes that look like:
 * a = phi(b, b, b, ...)
 *
 * Note that we can't ignore undef sources here, or else we may create a
 * situation where the definition of b isn't dominated by its uses. We're
 * allowed to do this since the definition of b must dominate all of the
 * phi node's predecessors, which means it must dominate the phi node as well
 * as all of the phi node's uses. In essence, the phi node acts as a copy
 * instruction. b can't be another phi node in the same block, since the only
 * time when phi nodes can source other phi nodes defined in the same block is
 * at the loop header, and in that case one of the sources of the phi has to
 * be from before the loop and that source can't be b.
 */

static bool
remove_phis_block(nir_block *block, void *state)
{
   bool *progress = state;

   nir_foreach_instr_safe(block, instr) {
      if (instr->type != nir_instr_type_phi)
         break;

      nir_phi_instr *phi = nir_instr_as_phi(instr);

      nir_ssa_def *def = NULL;
      bool srcs_same = true;

      nir_foreach_phi_src(phi, src) {
         assert(src->src.is_ssa);

         /* For phi nodes at the beginning of loops, we may encounter some
          * sources from backedges that point back to the destination of the
          * same phi, i.e. something like:
          *
          * a = phi(a, b, ...)
          *
          * We can safely ignore these sources, since if all of the normal
          * sources point to the same definition, then that definition must
          * still dominate the phi node, and the phi will still always take
          * the value of that definition.
          */
         if (src->src.ssa == &phi->dest.ssa)
            continue;
         
         if (def == NULL) {
            def  = src->src.ssa;
         } else {
            if (src->src.ssa != def) {
               srcs_same = false;
               break;
            }
         }
      }

      if (!srcs_same)
         continue;

      /* We must have found at least one definition, since there must be at
       * least one forward edge.
       */
      assert(def != NULL);

      assert(phi->dest.is_ssa);
      nir_ssa_def_rewrite_uses(&phi->dest.ssa, nir_src_for_ssa(def));
      nir_instr_remove(instr);

      *progress = true;
   }

   return true;
}

static bool
remove_phis_impl(nir_function_impl *impl)
{
   bool progress = false;

   nir_foreach_block(impl, remove_phis_block, &progress);

   if (progress) {
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);
   }

   return progress;
}

bool
nir_opt_remove_phis(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(shader, function)
      if (function->impl)
         progress = remove_phis_impl(function->impl) || progress;

   return progress;
}

