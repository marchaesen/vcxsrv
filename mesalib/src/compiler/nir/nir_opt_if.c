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

#include "nir.h"
#include "nir_control_flow.h"

/**
 * This optimization detects if statements at the tops of loops where the
 * condition is a phi node of two constants and moves half of the if to above
 * the loop and the other half of the if to the end of the loop.  A simple for
 * loop "for (int i = 0; i < 4; i++)", when run through the SPIR-V front-end,
 * ends up looking something like this:
 *
 * vec1 32 ssa_0 = load_const (0x00000000)
 * vec1 32 ssa_1 = load_const (0xffffffff)
 * loop {
 *    block block_1:
 *    vec1 32 ssa_2 = phi block_0: ssa_0, block_7: ssa_5
 *    vec1 32 ssa_3 = phi block_0: ssa_0, block_7: ssa_1
 *    if ssa_2 {
 *       block block_2:
 *       vec1 32 ssa_4 = load_const (0x00000001)
 *       vec1 32 ssa_5 = iadd ssa_2, ssa_4
 *    } else {
 *       block block_3:
 *    }
 *    block block_4:
 *    vec1 32 ssa_6 = load_const (0x00000004)
 *    vec1 32 ssa_7 = ilt ssa_5, ssa_6
 *    if ssa_7 {
 *       block block_5:
 *    } else {
 *       block block_6:
 *       break
 *    }
 *    block block_7:
 * }
 *
 * This turns it into something like this:
 *
 * // Stuff from block 1
 * // Stuff from block 3
 * loop {
 *    block block_1:
 *    vec1 32 ssa_3 = phi block_0: ssa_0, block_7: ssa_1
 *    vec1 32 ssa_6 = load_const (0x00000004)
 *    vec1 32 ssa_7 = ilt ssa_5, ssa_6
 *    if ssa_7 {
 *       block block_5:
 *    } else {
 *       block block_6:
 *       break
 *    }
 *    block block_7:
 *    // Stuff from block 1
 *    // Stuff from block 2
 *    vec1 32 ssa_4 = load_const (0x00000001)
 *    vec1 32 ssa_5 = iadd ssa_2, ssa_4
 * }
 */
static bool
opt_peel_loop_initial_if(nir_loop *loop)
{
   nir_block *header_block = nir_loop_first_block(loop);
   nir_block *prev_block =
      nir_cf_node_as_block(nir_cf_node_prev(&loop->cf_node));

   /* It would be insane if this were not true */
   assert(_mesa_set_search(header_block->predecessors, prev_block));

   /* The loop must have exactly one continue block which could be a block
    * ending in a continue instruction or the "natural" continue from the
    * last block in the loop back to the top.
    */
   if (header_block->predecessors->entries != 2)
      return false;

   nir_block *continue_block = NULL;
   struct set_entry *pred_entry;
   set_foreach(header_block->predecessors, pred_entry) {
      if (pred_entry->key != prev_block)
         continue_block = (void *)pred_entry->key;
   }

   nir_cf_node *if_node = nir_cf_node_next(&header_block->cf_node);
   if (!if_node || if_node->type != nir_cf_node_if)
      return false;

   nir_if *nif = nir_cf_node_as_if(if_node);
   assert(nif->condition.is_ssa);

   nir_ssa_def *cond = nif->condition.ssa;
   if (cond->parent_instr->type != nir_instr_type_phi)
      return false;

   nir_phi_instr *cond_phi = nir_instr_as_phi(cond->parent_instr);
   if (cond->parent_instr->block != header_block)
      return false;

   /* We already know we have exactly one continue */
   assert(exec_list_length(&cond_phi->srcs) == 2);

   uint32_t entry_val = 0, continue_val = 0;
   nir_foreach_phi_src(src, cond_phi) {
      assert(src->src.is_ssa);
      nir_const_value *const_src = nir_src_as_const_value(src->src);
      if (!const_src)
         return false;

      if (src->pred == continue_block) {
         continue_val = const_src->u32[0];
      } else {
         assert(src->pred == prev_block);
         entry_val = const_src->u32[0];
      }
   }

   /* If they both execute or both don't execute, this is a job for
    * nir_dead_cf, not this pass.
    */
   if ((entry_val && continue_val) || (!entry_val && !continue_val))
      return false;

   struct exec_list *continue_list, *entry_list;
   if (continue_val) {
      continue_list = &nif->then_list;
      entry_list = &nif->else_list;
   } else {
      continue_list = &nif->else_list;
      entry_list = &nif->then_list;
   }

   /* We want to be moving the contents of entry_list to above the loop so it
    * can't contain any break or continue instructions.
    */
   foreach_list_typed(nir_cf_node, cf_node, node, entry_list) {
      nir_foreach_block_in_cf_node(block, cf_node) {
         nir_instr *last_instr = nir_block_last_instr(block);
         if (last_instr && last_instr->type == nir_instr_type_jump)
            return false;
      }
   }

   /* Before we do anything, convert the loop to LCSSA.  We're about to
    * replace a bunch of SSA defs with registers and this will prevent any of
    * it from leaking outside the loop.
    */
   nir_convert_loop_to_lcssa(loop);

   nir_block *after_if_block =
      nir_cf_node_as_block(nir_cf_node_next(&nif->cf_node));

   /* Get rid of phis in the header block since we will be duplicating it */
   nir_lower_phis_to_regs_block(header_block);
   /* Get rid of phis after the if since dominance will change */
   nir_lower_phis_to_regs_block(after_if_block);

   /* Get rid of SSA defs in the pieces we're about to move around */
   nir_lower_ssa_defs_to_regs_block(header_block);
   nir_foreach_block_in_cf_node(block, &nif->cf_node)
      nir_lower_ssa_defs_to_regs_block(block);

   nir_cf_list header, tmp;
   nir_cf_extract(&header, nir_before_block(header_block),
                           nir_after_block(header_block));

   nir_cf_list_clone(&tmp, &header, &loop->cf_node, NULL);
   nir_cf_reinsert(&tmp, nir_before_cf_node(&loop->cf_node));
   nir_cf_extract(&tmp, nir_before_cf_list(entry_list),
                        nir_after_cf_list(entry_list));
   nir_cf_reinsert(&tmp, nir_before_cf_node(&loop->cf_node));

   nir_cf_reinsert(&header, nir_after_block_before_jump(continue_block));
   nir_cf_extract(&tmp, nir_before_cf_list(continue_list),
                        nir_after_cf_list(continue_list));
   nir_cf_reinsert(&tmp, nir_after_block_before_jump(continue_block));

   nir_cf_node_remove(&nif->cf_node);

   return true;
}

static bool
opt_if_cf_list(struct exec_list *cf_list)
{
   bool progress = false;
   foreach_list_typed(nir_cf_node, cf_node, node, cf_list) {
      switch (cf_node->type) {
      case nir_cf_node_block:
         break;

      case nir_cf_node_if: {
         nir_if *nif = nir_cf_node_as_if(cf_node);
         progress |= opt_if_cf_list(&nif->then_list);
         progress |= opt_if_cf_list(&nif->else_list);
         break;
      }

      case nir_cf_node_loop: {
         nir_loop *loop = nir_cf_node_as_loop(cf_node);
         progress |= opt_if_cf_list(&loop->body);
         progress |= opt_peel_loop_initial_if(loop);
         break;
      }

      case nir_cf_node_function:
         unreachable("Invalid cf type");
      }
   }

   return progress;
}

bool
nir_opt_if(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl == NULL)
         continue;

      if (opt_if_cf_list(&function->impl->body)) {
         nir_metadata_preserve(function->impl, nir_metadata_none);

         /* If that made progress, we're no longer really in SSA form.  We
          * need to convert registers back into SSA defs and clean up SSA defs
          * that don't dominate their uses.
          */
         nir_lower_regs_to_ssa_impl(function->impl);
         progress = true;
      }
   }

   return progress;
}
