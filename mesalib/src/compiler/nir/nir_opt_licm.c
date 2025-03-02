/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"

static bool
defined_before_loop(nir_src *src, void *state)
{
   unsigned *loop_preheader_idx = state;
   return src->ssa->parent_instr->block->index <= *loop_preheader_idx;
}

static bool
is_instr_loop_invariant(nir_instr *instr, unsigned loop_preheader_idx)
{
   switch (instr->type) {
   case nir_instr_type_load_const:
   case nir_instr_type_undef:
      return true;

   case nir_instr_type_intrinsic:
      if (!nir_intrinsic_can_reorder(nir_instr_as_intrinsic(instr)))
         return false;
      FALLTHROUGH;

   case nir_instr_type_alu:
   case nir_instr_type_tex:
   case nir_instr_type_deref:
      return nir_foreach_src(instr, defined_before_loop, &loop_preheader_idx);

   case nir_instr_type_phi:
   case nir_instr_type_call:
   case nir_instr_type_jump:
   default:
      return false;
   }
}

static bool
visit_block(nir_block *block, nir_block *preheader)
{
   bool progress = false;
   nir_foreach_instr_safe(instr, block) {
      if (is_instr_loop_invariant(instr, preheader->index)) {
         nir_instr_remove(instr);
         nir_instr_insert_after_block(preheader, instr);
         progress = true;
      }
   }

   return progress;
}

static bool
should_optimize_loop(nir_loop *loop)
{
   /* Ignore loops without back-edge */
   if (nir_loop_first_block(loop)->predecessors->entries == 1)
      return false;

   nir_foreach_block_in_cf_node(block, &loop->cf_node) {
      /* Check for an early exit inside the loop. */
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_intrinsic) {
            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic == nir_intrinsic_terminate ||
                intrin->intrinsic == nir_intrinsic_terminate_if)
               return false;
         }
      }

      /* The loop must not contains any return statement. */
      if (nir_block_ends_in_return_or_halt(block))
         return false;
   }

   return true;
}

static bool
visit_cf_list(struct exec_list *list, nir_block *preheader, nir_block *exit)
{
   bool progress = false;

   foreach_list_typed(nir_cf_node, node, node, list) {
      switch (node->type) {
      case nir_cf_node_block: {
         /* By only visiting blocks which dominate the loop exit, we
          * ensure that we don't speculatively hoist any instructions
          * which otherwise might not be executed.
          *
          * Note, that the proper check would be whether this block
          * postdominates the loop preheader.
          */
         nir_block *block = nir_cf_node_as_block(node);
         if (exit && nir_block_dominates(block, exit))
            progress |= visit_block(block, preheader);
         break;
      }
      case nir_cf_node_if: {
         nir_if *nif = nir_cf_node_as_if(node);
         progress |= visit_cf_list(&nif->then_list, preheader, exit);
         progress |= visit_cf_list(&nif->else_list, preheader, exit);
         break;
      }
      case nir_cf_node_loop: {
         nir_loop *loop = nir_cf_node_as_loop(node);
         bool opt = should_optimize_loop(loop);
         nir_block *inner_preheader = opt ? nir_cf_node_cf_tree_prev(node) : preheader;
         nir_block *inner_exit = opt ? nir_cf_node_cf_tree_next(node) : exit;
         progress |= visit_cf_list(&loop->body, inner_preheader, inner_exit);
         progress |= visit_cf_list(&loop->continue_list, inner_preheader, inner_exit);
         break;
      }
      case nir_cf_node_function:
         unreachable("NIR LICM: Unsupported cf_node type.");
      }
   }

   return progress;
}

bool
nir_opt_licm(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function_impl(impl, shader) {
      nir_metadata_require(impl, nir_metadata_block_index |
                                    nir_metadata_dominance);

      bool impl_progress = visit_cf_list(&impl->body, NULL, NULL);
      progress |= nir_progress(impl_progress, impl,
                               nir_metadata_block_index | nir_metadata_dominance);
   }

   return progress;
}
