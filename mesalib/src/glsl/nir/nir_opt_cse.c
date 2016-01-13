/*
 * Copyright © 2014 Intel Corporation
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
 *    Jason Ekstrand (jason@jlekstrand.net)
 *    Connor Abbott (cwabbott0@gmail.com)
 *
 */

#include "nir_instr_set.h"

/*
 * Implements common subexpression elimination
 */

/*
 * Visits and CSE's the given block and all its descendants in the dominance
 * tree recursively. Note that the instr_set is guaranteed to only ever
 * contain instructions that dominate the current block.
 */

static bool
cse_block(nir_block *block, struct set *instr_set)
{
   bool progress = false;

   nir_foreach_instr_safe(block, instr) {
      if (nir_instr_set_add_or_rewrite(instr_set, instr)) {
         progress = true;
         nir_instr_remove(instr);
      }
   }

   for (unsigned i = 0; i < block->num_dom_children; i++) {
      nir_block *child = block->dom_children[i];
      progress |= cse_block(child, instr_set);
   }

   nir_foreach_instr(block, instr)
     nir_instr_set_remove(instr_set, instr);

   return progress;
}

static bool
nir_opt_cse_impl(nir_function_impl *impl)
{
   struct set *instr_set = nir_instr_set_create(NULL);

   nir_metadata_require(impl, nir_metadata_dominance);

   bool progress = cse_block(nir_start_block(impl), instr_set);

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);

   nir_instr_set_destroy(instr_set);
   return progress;
}

bool
nir_opt_cse(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(shader, function) {
      if (function->impl)
         progress |= nir_opt_cse_impl(function->impl);
   }

   return progress;
}

