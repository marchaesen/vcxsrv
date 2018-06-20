/*
 * Copyright © 2018 Red Hat
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
 *    Rob Clark (robdclark@gmail.com>
 *
 */

#include "nir.h"


/*
 * A simple pass that moves load_const's into consuming block if
 * they are only consumed in a single block, to try to counter-
 * act nir's tendency to move all load_const to the top of the
 * first block.
 */

/* iterate a ssa def's use's and try to find a more optimal block to
 * move it to, using the dominance tree.  In short, if all of the uses
 * are contained in a single block, the load will be moved there,
 * otherwise it will be move to the least common ancestor block of all
 * the uses
 */
static nir_block *
get_preferred_block(nir_ssa_def *def)
{
   nir_block *lca = NULL;

   /* hmm, probably ignore if-uses: */
   if (!list_empty(&def->if_uses))
      return NULL;

   nir_foreach_use(use, def) {
      nir_instr *instr = use->parent_instr;
      nir_block *use_block = instr->block;

      /*
       * Kind of an ugly special-case, but phi instructions
       * need to appear first in the block, so by definition
       * we can't move a load_immed into a block where it is
       * consumed by a phi instruction.  We could conceivably
       * move it into a dominator block.
       */
      if (instr->type == nir_instr_type_phi) {
         nir_phi_instr *phi = nir_instr_as_phi(instr);
         nir_block *phi_lca = NULL;
         nir_foreach_phi_src(src, phi)
            phi_lca = nir_dominance_lca(phi_lca, src->pred);
         use_block = phi_lca;
      }

      lca = nir_dominance_lca(lca, use_block);
   }

   return lca;
}

/* insert before first non-phi instruction: */
static void
insert_after_phi(nir_instr *instr, nir_block *block)
{
   nir_foreach_instr(instr2, block) {
      if (instr2->type == nir_instr_type_phi)
         continue;

      exec_node_insert_node_before(&instr2->node,
                                   &instr->node);

      return;
   }

   /* if haven't inserted it, push to tail (ie. empty block or possibly
    * a block only containing phi's?)
    */
   exec_list_push_tail(&block->instr_list, &instr->node);
}

bool
nir_move_load_const(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;

      nir_foreach_block(block, function->impl) {
         nir_metadata_require(function->impl,
                              nir_metadata_block_index | nir_metadata_dominance);

         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_load_const)
               continue;

            nir_load_const_instr *load =
                  nir_instr_as_load_const(instr);
            nir_block *use_block =
                  get_preferred_block(&load->def);

            if (!use_block)
               continue;

            if (use_block == load->instr.block)
               continue;

            exec_node_remove(&load->instr.node);

            insert_after_phi(&load->instr, use_block);

            load->instr.block = use_block;

            progress = true;
         }
      }

      nir_metadata_preserve(function->impl,
                            nir_metadata_block_index | nir_metadata_dominance);
   }

   return progress;
}
