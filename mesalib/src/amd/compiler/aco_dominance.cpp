/*
 * Copyright © 2018 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef ACO_DOMINANCE_CPP
#define ACO_DOMINANCE_CPP

#include "aco_ir.h"

/*
 * Implements the algorithms for computing the dominator tree from
 * "A Simple, Fast Dominance Algorithm" by Cooper, Harvey, and Kennedy.
 *
 * Different from the paper, our CFG allows to compute the dominator tree
 * in a single pass as it is guaranteed that the dominating predecessors
 * are processed before the current block.
 */

namespace aco {

namespace {

struct block_dom_info {
   uint32_t logical_descendants = 0;
   uint32_t linear_descendants = 0;
   uint32_t logical_depth = 0;
   uint32_t linear_depth = 0;
   small_vec<uint32_t, 4> logical_children;
   small_vec<uint32_t, 4> linear_children;
};

void
calc_indices(Program* program)
{
   std::vector<block_dom_info> info(program->blocks.size());

   /* Create the linear and logical dominance trees. Calculating logical_descendants and
    * linear_descendants requires no recursion because the immediate dominator of each block has a
    * lower index. */
   for (int i = program->blocks.size() - 1; i >= 0; i--) {
      Block& block = program->blocks[i];

      /* Add this as a child node of the parent. */
      if (block.logical_idom != i && block.logical_idom != -1) {
         assert(i > block.logical_idom);
         info[block.logical_idom].logical_children.push_back(i);
         /* Add this node's descendants and itself to the parent. */
         info[block.logical_idom].logical_descendants += info[i].logical_descendants + 1;
      }
      if (block.linear_idom != i) {
         assert(i > block.linear_idom);
         info[block.linear_idom].linear_children.push_back(i);
         info[block.linear_idom].linear_descendants += info[i].linear_descendants + 1;
      }
   }

   /* Fill in the indices that would be obtained in a preorder and postorder traversal of the
    * dominance trees. */
   for (unsigned i = 0; i < program->blocks.size(); i++) {
      Block& block = program->blocks[i];
      /* Because of block_kind_resume, the root node's indices start at the block index to avoid
       * reusing indices. */
      if (block.logical_idom == (int)i)
         block.logical_dom_pre_index = i;
      if (block.linear_idom == (int)i)
         block.linear_dom_pre_index = i;

      /* Visit each child and assign it's preorder indices and depth. */
      unsigned start = block.logical_dom_pre_index + 1;
      for (unsigned j = 0; j < info[i].logical_children.size(); j++) {
         unsigned child = info[i].logical_children[j];
         info[child].logical_depth = info[i].logical_depth + 1;
         program->blocks[child].logical_dom_pre_index = start;
         start += info[child].logical_descendants + 1;
      }
      start = block.linear_dom_pre_index + 1;
      for (unsigned j = 0; j < info[i].linear_children.size(); j++) {
         unsigned child = info[i].linear_children[j];
         info[child].linear_depth = info[i].linear_depth + 1;
         program->blocks[child].linear_dom_pre_index = start;
         start += info[child].linear_descendants + 1;
      }

      /* The postorder traversal is the same as the preorder traversal, except that when this block
       * is visited, we haven't visited it's ancestors and have already visited it's descendants.
       * This means that the postorder_index is preorder_index-depth+descendants. */
      block.logical_dom_post_index =
         block.logical_dom_pre_index - info[i].logical_depth + info[i].logical_descendants;
      block.linear_dom_post_index =
         block.linear_dom_pre_index - info[i].linear_depth + info[i].linear_descendants;
   }
}

} /* end namespace */

void
dominator_tree(Program* program)
{
   for (unsigned i = 0; i < program->blocks.size(); i++) {
      Block& block = program->blocks[i];

      /* If this block has no predecessor, it dominates itself by definition */
      if (block.linear_preds.empty()) {
         block.linear_idom = block.index;
         block.logical_idom = block.index;
         continue;
      }

      int new_logical_idom = -1;
      int new_linear_idom = -1;
      for (unsigned pred_idx : block.logical_preds) {
         if ((int)program->blocks[pred_idx].logical_idom == -1)
            continue;

         if (new_logical_idom == -1) {
            new_logical_idom = pred_idx;
            continue;
         }

         while ((int)pred_idx != new_logical_idom) {
            if ((int)pred_idx > new_logical_idom)
               pred_idx = program->blocks[pred_idx].logical_idom;
            if ((int)pred_idx < new_logical_idom)
               new_logical_idom = program->blocks[new_logical_idom].logical_idom;
         }
      }

      for (unsigned pred_idx : block.linear_preds) {
         if ((int)program->blocks[pred_idx].linear_idom == -1)
            continue;

         if (new_linear_idom == -1) {
            new_linear_idom = pred_idx;
            continue;
         }

         while ((int)pred_idx != new_linear_idom) {
            if ((int)pred_idx > new_linear_idom)
               pred_idx = program->blocks[pred_idx].linear_idom;
            if ((int)pred_idx < new_linear_idom)
               new_linear_idom = program->blocks[new_linear_idom].linear_idom;
         }
      }

      block.logical_idom = new_logical_idom;
      block.linear_idom = new_linear_idom;
   }

   calc_indices(program);
}

} // namespace aco
#endif
