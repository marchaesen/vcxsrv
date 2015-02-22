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
 *
 */

#include "nir.h"

/*
 * Implements a small peephole optimization that looks for
 *
 * if (cond) {
 *    <empty>
 * } else {
 *    <empty>
 * }
 * phi
 * ...
 * phi
 *
 * and replaces it with a series of selects.  It can also handle the case
 * where, instead of being empty, the if may contain some move operations
 * whose only use is one of the following phi nodes.  This happens all the
 * time when the SSA form comes from a conditional assignment with a
 * swizzle.
 */

struct peephole_select_state {
   void *mem_ctx;
   bool progress;
};

static bool
are_all_move_to_phi(nir_block *block)
{
   nir_foreach_instr(block, instr) {
      if (instr->type != nir_instr_type_alu)
         return false;

      /* It must be a move operation */
      nir_alu_instr *mov = nir_instr_as_alu(instr);
      if (mov->op != nir_op_fmov && mov->op != nir_op_imov)
         return false;

      /* Can't handle saturate */
      if (mov->dest.saturate)
         return false;

      /* It must be SSA */
      if (!mov->dest.dest.is_ssa)
         return false;

      /* It cannot have any if-uses */
      if (mov->dest.dest.ssa.if_uses->entries != 0)
         return false;

      /* The only uses of this definition must be phi's in the successor */
      struct set_entry *entry;
      set_foreach(mov->dest.dest.ssa.uses, entry) {
         const nir_instr *dest_instr = entry->key;
         if (dest_instr->type != nir_instr_type_phi ||
             dest_instr->block != block->successors[0])
            return false;
      }
   }

   return true;
}

static bool
nir_opt_peephole_select_block(nir_block *block, void *void_state)
{
   struct peephole_select_state *state = void_state;

   /* If the block is empty, then it certainly doesn't have any phi nodes,
    * so we can skip it.  This also ensures that we do an early skip on the
    * end block of the function which isn't actually attached to the CFG.
    */
   if (exec_list_is_empty(&block->instr_list))
      return true;

   if (nir_cf_node_is_first(&block->cf_node))
      return true;

   nir_cf_node *prev_node = nir_cf_node_prev(&block->cf_node);
   if (prev_node->type != nir_cf_node_if)
      return true;

   nir_if *if_stmt = nir_cf_node_as_if(prev_node);
   nir_cf_node *then_node = nir_if_first_then_node(if_stmt);
   nir_cf_node *else_node = nir_if_first_else_node(if_stmt);

   /* We can only have one block in each side ... */
   if (nir_if_last_then_node(if_stmt) != then_node ||
       nir_if_last_else_node(if_stmt) != else_node)
      return true;

   nir_block *then_block = nir_cf_node_as_block(then_node);
   nir_block *else_block = nir_cf_node_as_block(else_node);

   /* ... and those blocks must only contain move-to-phi. */
   if (!are_all_move_to_phi(then_block) || !are_all_move_to_phi(else_block))
      return true;

   /* At this point, we know that the previous CFG node is an if-then
    * statement containing only moves to phi nodes in this block.  We can
    * just remove that entire CF node and replace all of the phi nodes with
    * selects.
    */

   nir_foreach_instr_safe(block, instr) {
      if (instr->type != nir_instr_type_phi)
         break;

      nir_phi_instr *phi = nir_instr_as_phi(instr);
      nir_alu_instr *sel = nir_alu_instr_create(state->mem_ctx, nir_op_bcsel);
      nir_src_copy(&sel->src[0].src, &if_stmt->condition, state->mem_ctx);
      /* Splat the condition to all channels */
      memset(sel->src[0].swizzle, 0, sizeof sel->src[0].swizzle);

      assert(exec_list_length(&phi->srcs) == 2);
      nir_foreach_phi_src(phi, src) {
         assert(src->pred == then_block || src->pred == else_block);
         assert(src->src.is_ssa);

         unsigned idx = src->pred == then_block ? 1 : 2;

         if (src->src.ssa->parent_instr->block == src->pred) {
            /* We already know that this instruction must be a move with
             * this phi's in this block as its only users.
             */
            nir_alu_instr *mov = nir_instr_as_alu(src->src.ssa->parent_instr);
            assert(mov->instr.type == nir_instr_type_alu);
            assert(mov->op == nir_op_fmov || mov->op == nir_op_imov);

            nir_alu_src_copy(&sel->src[idx], &mov->src[0], state->mem_ctx);
         } else {
            nir_src_copy(&sel->src[idx].src, &src->src, state->mem_ctx);
         }
      }

      nir_ssa_dest_init(&sel->instr, &sel->dest.dest,
                        phi->dest.ssa.num_components, phi->dest.ssa.name);
      sel->dest.write_mask = (1 << phi->dest.ssa.num_components) - 1;

      nir_ssa_def_rewrite_uses(&phi->dest.ssa,
                               nir_src_for_ssa(&sel->dest.dest.ssa),
                               state->mem_ctx);

      nir_instr_insert_before(&phi->instr, &sel->instr);
      nir_instr_remove(&phi->instr);
   }

   nir_cf_node_remove(&if_stmt->cf_node);
   state->progress = true;

   return true;
}

static bool
nir_opt_peephole_select_impl(nir_function_impl *impl)
{
   struct peephole_select_state state;

   state.mem_ctx = ralloc_parent(impl);
   state.progress = false;

   nir_foreach_block(impl, nir_opt_peephole_select_block, &state);

   if (state.progress)
      nir_metadata_preserve(impl, nir_metadata_none);

   return state.progress;
}

bool
nir_opt_peephole_select(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_overload(shader, overload) {
      if (overload->impl)
         progress |= nir_opt_peephole_select_impl(overload->impl);
   }

   return progress;
}
