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
#include "nir_instr_set.h"

/*
 * Implements Global Code Motion.  A description of GCM can be found in
 * "Global Code Motion; Global Value Numbering" by Cliff Click.
 * Unfortunately, the algorithm presented in the paper is broken in a
 * number of ways.  The algorithm used here differs substantially from the
 * one in the paper but it is, in my opinion, much easier to read and
 * verify correcness.
 */

struct gcm_block_info {
   /* Number of loops this block is inside */
   unsigned loop_depth;

   /* The last instruction inserted into this block.  This is used as we
    * traverse the instructions and insert them back into the program to
    * put them in the right order.
    */
   nir_instr *last_instr;
};

/* Flags used in the instr->pass_flags field for various instruction states */
enum {
   GCM_INSTR_PINNED =            (1 << 0),
   GCM_INSTR_SCHEDULED_EARLY =   (1 << 1),
   GCM_INSTR_SCHEDULED_LATE =    (1 << 2),
   GCM_INSTR_PLACED =            (1 << 3),
};

struct gcm_state {
   nir_function_impl *impl;
   nir_instr *instr;

   /* The list of non-pinned instructions.  As we do the late scheduling,
    * we pull non-pinned instructions out of their blocks and place them in
    * this list.  This saves us from having linked-list problems when we go
    * to put instructions back in their blocks.
    */
   struct exec_list instrs;

   struct gcm_block_info *blocks;
};

/* Recursively walks the CFG and builds the block_info structure */
static void
gcm_build_block_info(struct exec_list *cf_list, struct gcm_state *state,
                     unsigned loop_depth)
{
   foreach_list_typed(nir_cf_node, node, node, cf_list) {
      switch (node->type) {
      case nir_cf_node_block: {
         nir_block *block = nir_cf_node_as_block(node);
         state->blocks[block->index].loop_depth = loop_depth;
         break;
      }
      case nir_cf_node_if: {
         nir_if *if_stmt = nir_cf_node_as_if(node);
         gcm_build_block_info(&if_stmt->then_list, state, loop_depth);
         gcm_build_block_info(&if_stmt->else_list, state, loop_depth);
         break;
      }
      case nir_cf_node_loop: {
         nir_loop *loop = nir_cf_node_as_loop(node);
         gcm_build_block_info(&loop->body, state, loop_depth + 1);
         break;
      }
      default:
         unreachable("Invalid CF node type");
      }
   }
}

/* Walks the instruction list and marks immovable instructions as pinned
 *
 * This function also serves to initialize the instr->pass_flags field.
 * After this is completed, all instructions' pass_flags fields will be set
 * to either GCM_INSTR_PINNED or 0.
 */
static bool
gcm_pin_instructions_block(nir_block *block, struct gcm_state *state)
{
   nir_foreach_instr_safe(instr, block) {
      switch (instr->type) {
      case nir_instr_type_alu:
         switch (nir_instr_as_alu(instr)->op) {
         case nir_op_fddx:
         case nir_op_fddy:
         case nir_op_fddx_fine:
         case nir_op_fddy_fine:
         case nir_op_fddx_coarse:
         case nir_op_fddy_coarse:
            /* These can only go in uniform control flow; pin them for now */
            instr->pass_flags = GCM_INSTR_PINNED;
            break;

         default:
            instr->pass_flags = 0;
            break;
         }
         break;

      case nir_instr_type_tex:
         switch (nir_instr_as_tex(instr)->op) {
         case nir_texop_tex:
         case nir_texop_txb:
         case nir_texop_lod:
            /* These two take implicit derivatives so they need to be pinned */
            instr->pass_flags = GCM_INSTR_PINNED;
            break;

         default:
            instr->pass_flags = 0;
            break;
         }
         break;

      case nir_instr_type_load_const:
         instr->pass_flags = 0;
         break;

      case nir_instr_type_intrinsic: {
         const nir_intrinsic_info *info =
            &nir_intrinsic_infos[nir_instr_as_intrinsic(instr)->intrinsic];

         if ((info->flags & NIR_INTRINSIC_CAN_ELIMINATE) &&
             (info->flags & NIR_INTRINSIC_CAN_REORDER)) {
            instr->pass_flags = 0;
         } else {
            instr->pass_flags = GCM_INSTR_PINNED;
         }
         break;
      }

      case nir_instr_type_jump:
      case nir_instr_type_ssa_undef:
      case nir_instr_type_phi:
         instr->pass_flags = GCM_INSTR_PINNED;
         break;

      default:
         unreachable("Invalid instruction type in GCM");
      }

      if (!(instr->pass_flags & GCM_INSTR_PINNED)) {
         /* If this is an unpinned instruction, go ahead and pull it out of
          * the program and put it on the instrs list.  This has a couple
          * of benifits.  First, it makes the scheduling algorithm more
          * efficient because we can avoid walking over basic blocks and
          * pinned instructions.  Second, it keeps us from causing linked
          * list confusion when we're trying to put everything in its
          * proper place at the end of the pass.
          *
          * Note that we don't use nir_instr_remove here because that also
          * cleans up uses and defs and we want to keep that information.
          */
         exec_node_remove(&instr->node);
         exec_list_push_tail(&state->instrs, &instr->node);
      }
   }

   return true;
}

static void
gcm_schedule_early_instr(nir_instr *instr, struct gcm_state *state);

/** Update an instructions schedule for the given source
 *
 * This function is called iteratively as we walk the sources of an
 * instruction.  It ensures that the given source instruction has been
 * scheduled and then update this instruction's block if the source
 * instruction is lower down the tree.
 */
static bool
gcm_schedule_early_src(nir_src *src, void *void_state)
{
   struct gcm_state *state = void_state;
   nir_instr *instr = state->instr;

   assert(src->is_ssa);

   gcm_schedule_early_instr(src->ssa->parent_instr, void_state);

   /* While the index isn't a proper dominance depth, it does have the
    * property that if A dominates B then A->index <= B->index.  Since we
    * know that this instruction must have been dominated by all of its
    * sources at some point (even if it's gone through value-numbering),
    * all of the sources must lie on the same branch of the dominance tree.
    * Therefore, we can just go ahead and just compare indices.
    */
   if (instr->block->index < src->ssa->parent_instr->block->index)
      instr->block = src->ssa->parent_instr->block;

   /* We need to restore the state instruction because it may have been
    * changed through the gcm_schedule_early_instr call above.  Since we
    * may still be iterating through sources and future calls to
    * gcm_schedule_early_src for the same instruction will still need it.
    */
   state->instr = instr;

   return true;
}

/** Schedules an instruction early
 *
 * This function performs a recursive depth-first search starting at the
 * given instruction and proceeding through the sources to schedule
 * instructions as early as they can possibly go in the dominance tree.
 * The instructions are "scheduled" by updating their instr->block field.
 */
static void
gcm_schedule_early_instr(nir_instr *instr, struct gcm_state *state)
{
   if (instr->pass_flags & GCM_INSTR_SCHEDULED_EARLY)
      return;

   instr->pass_flags |= GCM_INSTR_SCHEDULED_EARLY;

   /* Pinned instructions are already scheduled so we don't need to do
    * anything.  Also, bailing here keeps us from ever following the
    * sources of phi nodes which can be back-edges.
    */
   if (instr->pass_flags & GCM_INSTR_PINNED)
      return;

   /* Start with the instruction at the top.  As we iterate over the
    * sources, it will get moved down as needed.
    */
   instr->block = nir_start_block(state->impl);
   state->instr = instr;

   nir_foreach_src(instr, gcm_schedule_early_src, state);
}

static void
gcm_schedule_late_instr(nir_instr *instr, struct gcm_state *state);

/** Schedules the instruction associated with the given SSA def late
 *
 * This function works by first walking all of the uses of the given SSA
 * definition, ensuring that they are scheduled, and then computing the LCA
 * (least common ancestor) of its uses.  It then schedules this instruction
 * as close to the LCA as possible while trying to stay out of loops.
 */
static bool
gcm_schedule_late_def(nir_ssa_def *def, void *void_state)
{
   struct gcm_state *state = void_state;

   nir_block *lca = NULL;

   nir_foreach_use(use_src, def) {
      nir_instr *use_instr = use_src->parent_instr;

      gcm_schedule_late_instr(use_instr, state);

      /* Phi instructions are a bit special.  SSA definitions don't have to
       * dominate the sources of the phi nodes that use them; instead, they
       * have to dominate the predecessor block corresponding to the phi
       * source.  We handle this by looking through the sources, finding
       * any that are usingg this SSA def, and using those blocks instead
       * of the one the phi lives in.
       */
      if (use_instr->type == nir_instr_type_phi) {
         nir_phi_instr *phi = nir_instr_as_phi(use_instr);

         nir_foreach_phi_src(phi_src, phi) {
            if (phi_src->src.ssa == def)
               lca = nir_dominance_lca(lca, phi_src->pred);
         }
      } else {
         lca = nir_dominance_lca(lca, use_instr->block);
      }
   }

   nir_foreach_if_use(use_src, def) {
      nir_if *if_stmt = use_src->parent_if;

      /* For if statements, we consider the block to be the one immediately
       * preceding the if CF node.
       */
      nir_block *pred_block =
         nir_cf_node_as_block(nir_cf_node_prev(&if_stmt->cf_node));

      lca = nir_dominance_lca(lca, pred_block);
   }

   /* Some instructions may never be used.  We'll just leave them scheduled
    * early and let dead code clean them up.
    */
   if (lca == NULL)
      return true;

   /* We now have the LCA of all of the uses.  If our invariants hold,
    * this is dominated by the block that we chose when scheduling early.
    * We now walk up the dominance tree and pick the lowest block that is
    * as far outside loops as we can get.
    */
   nir_block *best = lca;
   for (nir_block *block = lca; block != NULL; block = block->imm_dom) {
      if (state->blocks[block->index].loop_depth <
          state->blocks[best->index].loop_depth)
         best = block;

      if (block == def->parent_instr->block)
         break;
   }
   def->parent_instr->block = best;

   return true;
}

/** Schedules an instruction late
 *
 * This function performs a depth-first search starting at the given
 * instruction and proceeding through its uses to schedule instructions as
 * late as they can reasonably go in the dominance tree.  The instructions
 * are "scheduled" by updating their instr->block field.
 *
 * The name of this function is actually a bit of a misnomer as it doesn't
 * schedule them "as late as possible" as the paper implies.  Instead, it
 * first finds the lates possible place it can schedule the instruction and
 * then possibly schedules it earlier than that.  The actual location is as
 * far down the tree as we can go while trying to stay out of loops.
 */
static void
gcm_schedule_late_instr(nir_instr *instr, struct gcm_state *state)
{
   if (instr->pass_flags & GCM_INSTR_SCHEDULED_LATE)
      return;

   instr->pass_flags |= GCM_INSTR_SCHEDULED_LATE;

   /* Pinned instructions are already scheduled so we don't need to do
    * anything.  Also, bailing here keeps us from ever following phi nodes
    * which can be back-edges.
    */
   if (instr->pass_flags & GCM_INSTR_PINNED)
      return;

   nir_foreach_ssa_def(instr, gcm_schedule_late_def, state);
}

static void
gcm_place_instr(nir_instr *instr, struct gcm_state *state);

static bool
gcm_place_instr_def(nir_ssa_def *def, void *state)
{
   nir_foreach_use(use_src, def)
      gcm_place_instr(use_src->parent_instr, state);

   return false;
}

/** Places an instrution back into the program
 *
 * The earlier passes of GCM simply choose blocks for each instruction and
 * otherwise leave them alone.  This pass actually places the instructions
 * into their chosen blocks.
 *
 * To do so, we use a standard post-order depth-first search linearization
 * algorithm.  We walk over the uses of the given instruction and ensure
 * that they are placed and then place this instruction.  Because we are
 * working on multiple blocks at a time, we keep track of the last inserted
 * instruction per-block in the state structure's block_info array.  When
 * we insert an instruction in a block we insert it before the last
 * instruction inserted in that block rather than the last instruction
 * inserted globally.
 */
static void
gcm_place_instr(nir_instr *instr, struct gcm_state *state)
{
   if (instr->pass_flags & GCM_INSTR_PLACED)
      return;

   instr->pass_flags |= GCM_INSTR_PLACED;

   /* Phi nodes are our once source of back-edges.  Since right now we are
    * only doing scheduling within blocks, we don't need to worry about
    * them since they are always at the top.  Just skip them completely.
    */
   if (instr->type == nir_instr_type_phi) {
      assert(instr->pass_flags & GCM_INSTR_PINNED);
      return;
   }

   nir_foreach_ssa_def(instr, gcm_place_instr_def, state);

   if (instr->pass_flags & GCM_INSTR_PINNED) {
      /* Pinned instructions have an implicit dependence on the pinned
       * instructions that come after them in the block.  Since the pinned
       * instructions will naturally "chain" together, we only need to
       * explicitly visit one of them.
       */
      for (nir_instr *after = nir_instr_next(instr);
           after;
           after = nir_instr_next(after)) {
         if (after->pass_flags & GCM_INSTR_PINNED) {
            gcm_place_instr(after, state);
            break;
         }
      }
   }

   struct gcm_block_info *block_info = &state->blocks[instr->block->index];
   if (!(instr->pass_flags & GCM_INSTR_PINNED)) {
      exec_node_remove(&instr->node);

      if (block_info->last_instr) {
         exec_node_insert_node_before(&block_info->last_instr->node,
                                      &instr->node);
      } else {
         /* Schedule it at the end of the block */
         nir_instr *jump_instr = nir_block_last_instr(instr->block);
         if (jump_instr && jump_instr->type == nir_instr_type_jump) {
            exec_node_insert_node_before(&jump_instr->node, &instr->node);
         } else {
            exec_list_push_tail(&instr->block->instr_list, &instr->node);
         }
      }
   }

   block_info->last_instr = instr;
}

static bool
opt_gcm_impl(nir_function_impl *impl, bool value_number)
{
   nir_metadata_require(impl, nir_metadata_block_index |
                              nir_metadata_dominance);

   struct gcm_state state;

   state.impl = impl;
   state.instr = NULL;
   exec_list_make_empty(&state.instrs);
   state.blocks = rzalloc_array(NULL, struct gcm_block_info, impl->num_blocks);

   gcm_build_block_info(&impl->body, &state, 0);

   nir_foreach_block(block, impl) {
      gcm_pin_instructions_block(block, &state);
   }

   bool progress = false;
   if (value_number) {
      struct set *gvn_set = nir_instr_set_create(NULL);
      foreach_list_typed_safe(nir_instr, instr, node, &state.instrs) {
         if (nir_instr_set_add_or_rewrite(gvn_set, instr)) {
            nir_instr_remove(instr);
            progress = true;
         }
      }
      nir_instr_set_destroy(gvn_set);
   }

   foreach_list_typed(nir_instr, instr, node, &state.instrs)
      gcm_schedule_early_instr(instr, &state);

   foreach_list_typed(nir_instr, instr, node, &state.instrs)
      gcm_schedule_late_instr(instr, &state);

   while (!exec_list_is_empty(&state.instrs)) {
      nir_instr *instr = exec_node_data(nir_instr,
                                        state.instrs.tail_sentinel.prev, node);
      gcm_place_instr(instr, &state);
   }

   ralloc_free(state.blocks);

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);

   return progress;
}

bool
nir_opt_gcm(nir_shader *shader, bool value_number)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= opt_gcm_impl(function->impl, value_number);
   }

   return progress;
}
