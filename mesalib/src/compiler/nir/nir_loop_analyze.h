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

#pragma once

#include "nir.h"

/* Returns true if nir_cf_node contains a jump other than the expected_jump
 * parameter.
 */
static inline bool
contains_other_jump(nir_cf_node *node, nir_instr *expected_jump)
{
   switch (node->type) {
   case nir_cf_node_block: {
      nir_instr *lst_instr = nir_block_last_instr(nir_cf_node_as_block(node));

      /* dead_cf should have eliminated any instruction after the first break
       */
      nir_foreach_instr(instr, nir_cf_node_as_block(node))
         assert(instr->type != nir_instr_type_jump || instr == lst_instr);

      if (lst_instr && lst_instr->type == nir_instr_type_jump &&
          lst_instr != expected_jump)
         return true;
      else
         return false;
   }
   case nir_cf_node_if: {
      nir_if *if_stmt = nir_cf_node_as_if(node);

      foreach_list_typed_safe(nir_cf_node, node, node, &if_stmt->then_list) {
         if (contains_other_jump(node, expected_jump))
            return true;
      }

      foreach_list_typed_safe(nir_cf_node, node, node, &if_stmt->else_list) {
         if (contains_other_jump(node, expected_jump))
            return true;
      }

      return false;
   }
   case nir_cf_node_loop:
      return true;

   default:
      unreachable("Unhandled cf node type");
   }
}

/* Here we define a trivial if as containing only a single break that must be
 * located at the end of either the then or else branch of the top level if,
 * there must be no other breaks or any other type of jump.  Or we pass NULL
 * to break_block the if must contains no jumps at all.
 */
static inline bool
nir_is_trivial_loop_if(nir_if *nif, nir_block *break_block)
{
   nir_instr *last_instr = NULL;

   if (break_block) {
      last_instr = nir_block_last_instr(break_block);
      assert(last_instr && last_instr->type == nir_instr_type_jump &&
             nir_instr_as_jump(last_instr)->type == nir_jump_break);
   }

   if (contains_other_jump(&nif->cf_node, last_instr))
      return false;

   return true;
}
