/*
 * Copyright © 2016 Red Hat
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

/** @file nir_opt_conditional_discard.c
 *
 * Handles optimization of lowering if (cond) discard to discard_if(cond).
 */

static bool
nir_opt_conditional_discard_block(nir_builder *b, nir_block *block)
{
   if (nir_cf_node_is_first(&block->cf_node))
      return false;

   nir_cf_node *prev_node = nir_cf_node_prev(&block->cf_node);
   if (prev_node->type != nir_cf_node_if)
      return false;

   nir_if *if_stmt = nir_cf_node_as_if(prev_node);
   nir_block *then_block = nir_if_first_then_block(if_stmt);
   nir_block *else_block = nir_if_first_else_block(if_stmt);

   /* check there is only one else block and it is empty */
   if (nir_if_last_else_block(if_stmt) != else_block)
      return false;
   if (!exec_list_is_empty(&else_block->instr_list))
      return false;

   /* check there is only one then block and it has only one instruction in it */
   if (nir_if_last_then_block(if_stmt) != then_block)
      return false;
   if (exec_list_is_empty(&then_block->instr_list))
      return false;
   if (exec_list_length(&then_block->instr_list) > 1)
      return false;
   /*
    * make sure no subsequent phi nodes point at this if.
    */
   nir_block *after = nir_cf_node_as_block(nir_cf_node_next(&if_stmt->cf_node));
   nir_foreach_instr_safe(instr, after) {
      if (instr->type != nir_instr_type_phi)
         break;
      nir_phi_instr *phi = nir_instr_as_phi(instr);

      nir_foreach_phi_src(phi_src, phi) {
         if (phi_src->pred == then_block ||
             phi_src->pred == else_block)
            return false;
      }
   }

   /* Get the first instruction in the then block and confirm it is
    * a discard or a discard_if
    */
   nir_instr *instr = nir_block_first_instr(then_block);
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   if (intrin->intrinsic != nir_intrinsic_discard &&
       intrin->intrinsic != nir_intrinsic_discard_if)
      return false;

   nir_src cond;

   b->cursor = nir_before_cf_node(prev_node);
   if (intrin->intrinsic == nir_intrinsic_discard)
      cond = if_stmt->condition;
   else
      cond = nir_src_for_ssa(nir_iand(b,
                                      nir_ssa_for_src(b, if_stmt->condition, 1),
                                      nir_ssa_for_src(b, intrin->src[0], 1)));

   nir_intrinsic_instr *discard_if =
      nir_intrinsic_instr_create(b->shader, nir_intrinsic_discard_if);
   nir_src_copy(&discard_if->src[0], &cond, discard_if);

   nir_instr_insert_before_cf(prev_node, &discard_if->instr);
   nir_instr_remove(&intrin->instr);
   nir_cf_node_remove(&if_stmt->cf_node);

   return true;
}

bool
nir_opt_conditional_discard(nir_shader *shader)
{
   bool progress = false;

   nir_builder builder;

   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_builder_init(&builder, function->impl);
         nir_foreach_block_safe(block, function->impl) {
            progress |= nir_opt_conditional_discard_block(&builder, block);
         }
      }
   }
   return progress;
}
