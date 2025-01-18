/*
 * Copyright © 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

/* The pass uses information on which branches are divergent in order to
 * determine which blocks are "reconvergence points" where parked threads may
 * become reactivated as well as to add "physical" edges where the machine may
 * fall through to the next reconvergence point. Reconvergence points need a
 * (jp) added in the assembly, and physical edges are needed to model shared
 * register liveness correctly. Reconvergence happens in the following two
 * scenarios:
 *
 * 1. When there is a divergent branch, the later of the two block destinations
 *    becomes a reconvergence point.
 * 2. When a forward edge crosses over a reconvergence point that may be
 *    outstanding at the start of the edge, we need to park the threads that
 *    take the edge and resume execution at the reconvergence point. This means
 *    that there is a physical edge from the start of the edge to the
 *    reconvergence point, and the destination of the edge becomes a new
 *    reconvergence point.
 *
 * For example, consider this simple if-else:
 *
 *    bb0:
 *    ...
 *    br p0.x, #bb1, #bb2
 *    bb1:
 *    ...
 *    jump bb3
 *    bb2:
 *    ...
 *    jump bb3
 *    bb3:
 *    ...
 *
 * The divergent branch at the end of bb0 makes bb2 a reconvergence point
 * following (1), which starts being outstanding after the branch at the end of
 * bb1. The jump to bb3 at the end of bb1 goes over bb2 while it is outstanding,
 * so there is a physical edge from bb1 to bb2 and bb3 is a reconvergence point
 * following (2).
 * 
 * Note that (2) can apply recursively. To handle this efficiently we build an
 * interval tree of forward edges that cross other blocks and whenever a block
 * becomes a RP we iterate through the edges jumping across it using the tree.
 * We also need to keep track of the range where each RP may be
 * "outstanding." A RP becomes outstanding after a branch to it parks its
 * threads there. This range may increase in size as we discover more and more
 * branches to it that may park their threads there.
 *
 * Finally, we need to compute the branchstack value, which is the maximum
 * number of outstanding reconvergence points. For the if-else, the branchstack
 * is 2, because after the jump at the end of bb2 both reconvergence points are
 * outstanding (although the first is removed immediately afterwards). Because
 * we already computed the range where each RP is outstanding, this part is
 * relatively straightforward.
 */

#include <limits.h>

#include "ir3_shader.h"

#include "util/rb_tree.h"
#include "util/u_worklist.h"
#include "util/ralloc.h"

struct logical_edge {
   struct uinterval_node node;
   struct ir3_block *start_block;
   struct ir3_block *end_block;
};

struct block_data {
   /* For a reconvergance point, the index of the first block where, upon
    * exiting, the RP may be outstanding. Normally this is a predecessor but may
    * be a loop header for loops.
    */
   unsigned first_divergent_pred;

   /* The last processed first_divergent_pred. */
   unsigned first_processed_divergent_pred;

   /* The number of blocks that have this block as a first_divergent_pred. */
   unsigned divergence_count;
};

void
ir3_calc_reconvergence(struct ir3_shader_variant *so)
{
   void *mem_ctx = ralloc_context(NULL);

   /* It's important that the index we use corresponds to the final order blocks
    * are emitted in!
    */
   unsigned index = 0;
   foreach_block (block, &so->ir->block_list) {
      block->index = index++;
   }

   /* Setup the tree of edges */
   unsigned edge_count = 0;
   foreach_block (block, &so->ir->block_list) {
      if (block->successors[0])
         edge_count++;
      if (block->successors[1])
         edge_count++;

      block->physical_predecessors_count = 0;
      block->physical_successors_count = 0;
      block->reconvergence_point = false;
   }

   struct rb_tree forward_edges, backward_edges;
   rb_tree_init(&forward_edges);
   rb_tree_init(&backward_edges);

   unsigned edge = 0;
   struct logical_edge *edges =
      ralloc_array(mem_ctx, struct logical_edge, edge_count);
   struct block_data *blocks =
      ralloc_array(mem_ctx, struct block_data, index);
   foreach_block (block, &so->ir->block_list) {
      blocks[block->index].divergence_count = 0;
      blocks[block->index].first_divergent_pred = UINT_MAX;
      blocks[block->index].first_processed_divergent_pred = UINT_MAX;
      for (unsigned i = 0; i < ARRAY_SIZE(block->successors); i++) {
         if (block->successors[i]) {
            ir3_block_link_physical(block, block->successors[i]);

            if (block->successors[i]->index > block->index + 1) {
               edges[edge] = (struct logical_edge) {
                  .node = {
                     .interval = {
                        block->index + 1,
                        block->successors[i]->index - 1
                     },
                  },
                  .start_block = block,
                  .end_block = block->successors[i],
               };

               uinterval_tree_insert(&forward_edges, &edges[edge++].node);
            } else if (block->successors[i]->index <= block->index) {
               edges[edge] = (struct logical_edge) {
                  .node = {
                     .interval = {
                        block->successors[i]->index - 1,
                        block->index + 1
                     },
                  },
                  .start_block = block->successors[i],
                  .end_block = block,
               };

               uinterval_tree_insert(&backward_edges, &edges[edge++].node);
            }
         } else {
            struct ir3_instruction *terminator =
               ir3_block_get_terminator(block);

            /* We don't want to mark targets of predicated branches as
             * reconvergence points below because they don't need the
             * branchstack:
             *        |-- i --|
             *        | ...   |
             *        | predt |
             *        |-------|
             *    succ0 /   \ succ1
             * |-- i+1 --| |-- i+2 --|
             * | tblock  | | fblock  |
             * | predf   | | jump    |
             * |---------| |---------|
             *    succ0 \   / succ0
             *        |-- j --|
             *        |  ...  |
             *        |-------|
             * Here, neither block i+2 nor block j need (jp). However, block i+1
             * still needs a physical edge to block i+2 (control flow will fall
             * through here) but the code below won't add it unless block i+2 is
             * a reconvergence point. Therefore, we add it manually here.
             *
             * Note: we are here because the current block has only one
             * successor which means that, if there is a predicated terminator,
             * block will be block i+1 in the diagram above.
             */
            if (terminator && (terminator->opc == OPC_PREDT ||
                               terminator->opc == OPC_PREDF)) {
               struct ir3_block *next =
                  list_entry(block->node.next, struct ir3_block, node);
               ir3_block_link_physical(block, next);
            }
         }
      }
   }

   assert(edge <= edge_count);

   u_worklist worklist;
   u_worklist_init(&worklist, index, mem_ctx);

   /* First, find and mark divergent branches. The later destination will be the
    * reconvergence point.
    */
   foreach_block (block, &so->ir->block_list) {
      struct ir3_instruction *terminator = ir3_block_get_terminator(block);
      if (!terminator)
         continue;
      if (terminator->opc == OPC_PREDT || terminator->opc == OPC_PREDF)
         continue;
      if (block->successors[0] && block->successors[1] &&
          block->divergent_condition) {
         struct ir3_block *reconv_points[2];
         unsigned num_reconv_points;
         struct ir3_instruction *prev_instr = NULL;

         if (!list_is_singular(&block->instr_list)) {
            prev_instr =
               list_entry(terminator->node.prev, struct ir3_instruction, node);
         }

         if (prev_instr && is_terminator(prev_instr)) {
            /* There are two terminating branches so both successors are
             * reconvergence points (i.e., there is no fall through into the
             * next block). This can only happen after ir3_legalize when we fail
             * to eliminate a non-invertible branch. For example:
             * getone #bb0
             * jump #bb1
             * bb0: (jp)...
             * bb1: (jp)...
             */
            reconv_points[0] = block->successors[0];
            reconv_points[1] = block->successors[1];
            num_reconv_points = 2;
         } else {
            unsigned idx =
               block->successors[0]->index > block->successors[1]->index ? 0
                                                                         : 1;
            reconv_points[0] = block->successors[idx];
            reconv_points[1] = NULL;
            num_reconv_points = 1;
         }

         for (unsigned i = 0; i < num_reconv_points; i++) {
            struct ir3_block *reconv_point = reconv_points[i];
            reconv_point->reconvergence_point = true;

            struct block_data *reconv_point_data = &blocks[reconv_point->index];
            if (reconv_point_data->first_divergent_pred > block->index) {
               reconv_point_data->first_divergent_pred = block->index;
            }

            u_worklist_push_tail(&worklist, reconv_point, index);
         }
      }
   }

   while (!u_worklist_is_empty(&worklist)) {
      struct ir3_block *block =
         u_worklist_pop_head(&worklist, struct ir3_block, index);
      assert(block->reconvergence_point);

      /* Backwards branches extend the range of divergence. For example, a
       * divergent break creates a reconvergence point after the loop that
       * stays outstanding throughout subsequent iterations, even at points
       * before the break. This takes that into account.
       *
       * More precisely, a backwards edge that originates between the block and
       * it's first_divergent_pred (i.e. in the divergence range) extends the
       * divergence range to the beginning of its destination if it is taken, or
       * alternatively to the end of the block before its destination.
       */
      struct uinterval interval2 = {
         blocks[block->index].first_divergent_pred,
         blocks[block->index].first_divergent_pred
      };
      uinterval_tree_foreach (struct logical_edge, back_edge, interval2, &backward_edges,
                              node) {
         if (back_edge->end_block->index < block->index) {
            if (blocks[block->index].first_divergent_pred >
                back_edge->start_block->index - 1) {
               blocks[block->index].first_divergent_pred =
                  back_edge->start_block->index - 1;
            }
         }
      }

      /* Iterate over all edges stepping over the block. */
      struct uinterval interval = { block->index, block->index };
      struct logical_edge *prev = NULL;
      uinterval_tree_foreach (struct logical_edge, edge, interval, &forward_edges,
                              node) {
         /* If "block" definitely isn't outstanding when the branch
          * corresponding to "edge" is taken, then we don't need to park
          * "edge->end_block" and we can ignore this.
          *
          * TODO: add uinterval_tree_foreach_from() and use that instead.
          */
         if (edge->start_block->index <= blocks[block->index].first_divergent_pred)
            continue;

         /* If we've already processed this edge + RP pair, don't process it
          * again. Because edges are ordered by start point, we must have
          * processed every edge after this too.
          */
         if (edge->start_block->index >
             blocks[block->index].first_processed_divergent_pred)
            break;

         edge->end_block->reconvergence_point = true;
         if (blocks[edge->end_block->index].first_divergent_pred >
             edge->start_block->index) {
            blocks[edge->end_block->index].first_divergent_pred =
               edge->start_block->index;
            u_worklist_push_tail(&worklist, edge->end_block, index);
         }

         if (!prev || prev->start_block != edge->start_block) {
            /* We should only process this edge + block combination once, and
             * we use the fact that edges are sorted by start point to avoid
             * adding redundant physical edges in case multiple edges have the
             * same start point by comparing with the previous edge. Therefore
             * we should only add the physical edge once.
             * However, we should skip logical successors of the edge's start
             * block since physical edges for those have already been added
             * initially.
             */
            if (block != edge->start_block->successors[0] &&
                block != edge->start_block->successors[1]) {
               for (unsigned i = 0; i < block->physical_predecessors_count; i++)
                  assert(block->physical_predecessors[i] != edge->start_block);
               ir3_block_link_physical(edge->start_block, block);
            }
         }
         prev = edge;
      }

      blocks[block->index].first_processed_divergent_pred =
         blocks[block->index].first_divergent_pred;
   }

   /* For each reconvergent point p we have an open range
    * (p->first_divergent_pred, p) where p may be outstanding. We need to keep
    * track of the number of outstanding RPs and calculate the maximum.
    */
   foreach_block (block, &so->ir->block_list) {
      if (block->reconvergence_point) {
         blocks[blocks[block->index].first_divergent_pred].divergence_count++;
      }
   }

   unsigned rc_level = 0;
   so->branchstack = 0;
   foreach_block (block, &so->ir->block_list) {
      if (block->reconvergence_point)
         rc_level--;

      /* Account for lowerings that produce divergent control flow. */
      foreach_instr (instr, &block->instr_list) {
         switch (instr->opc) {
         case OPC_SCAN_MACRO:
            so->branchstack = MAX2(so->branchstack, rc_level + 2);
            break;
         case OPC_BALLOT_MACRO:
         case OPC_READ_COND_MACRO:
         case OPC_ELECT_MACRO:
         case OPC_READ_FIRST_MACRO:
            so->branchstack = MAX2(so->branchstack, rc_level + 1);
            break;
         default:
            break;
         }
      }

      rc_level += blocks[block->index].divergence_count;

      so->branchstack = MAX2(so->branchstack, rc_level); 
   }
   assert(rc_level == 0);

   ralloc_free(mem_ctx);
}

