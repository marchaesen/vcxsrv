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
#include "nir/nir_builder.h"
#include "nir_constant_expressions.h"
#include "nir_control_flow.h"
#include "nir_loop_analyze.h"

/**
 * Gets the single block that jumps back to the loop header. Already assumes
 * there is exactly one such block.
 */
static nir_block*
find_continue_block(nir_loop *loop)
{
   nir_block *header_block = nir_loop_first_block(loop);
   nir_block *prev_block =
      nir_cf_node_as_block(nir_cf_node_prev(&loop->cf_node));

   assert(header_block->predecessors->entries == 2);

   set_foreach(header_block->predecessors, pred_entry) {
      if (pred_entry->key != prev_block)
         return (nir_block*)pred_entry->key;
   }

   unreachable("Continue block not found!");
}

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
   MAYBE_UNUSED nir_block *prev_block =
      nir_cf_node_as_block(nir_cf_node_prev(&loop->cf_node));

   /* It would be insane if this were not true */
   assert(_mesa_set_search(header_block->predecessors, prev_block));

   /* The loop must have exactly one continue block which could be a block
    * ending in a continue instruction or the "natural" continue from the
    * last block in the loop back to the top.
    */
   if (header_block->predecessors->entries != 2)
      return false;

   nir_block *continue_block = find_continue_block(loop);

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

   /* We're about to re-arrange a bunch of blocks so make sure that we don't
    * have deref uses which cross block boundaries.  We don't want a deref
    * accidentally ending up in a phi.
    */
   nir_rematerialize_derefs_in_use_blocks_impl(
      nir_cf_node_get_function(&loop->cf_node));

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

   /* Get continue block again as the previous reinsert might have removed the block. */
   continue_block = find_continue_block(loop);

   nir_cf_extract(&tmp, nir_before_cf_list(continue_list),
                        nir_after_cf_list(continue_list));
   nir_cf_reinsert(&tmp, nir_after_block_before_jump(continue_block));

   nir_cf_node_remove(&nif->cf_node);

   return true;
}

static bool
is_block_empty(nir_block *block)
{
   return nir_cf_node_is_last(&block->cf_node) &&
          exec_list_is_empty(&block->instr_list);
}

/**
 * This optimization turns:
 *
 *     if (cond) {
 *     } else {
 *         do_work();
 *     }
 *
 * into:
 *
 *     if (!cond) {
 *         do_work();
 *     } else {
 *     }
 */
static bool
opt_if_simplification(nir_builder *b, nir_if *nif)
{
   /* Only simplify if the then block is empty and the else block is not. */
   if (!is_block_empty(nir_if_first_then_block(nif)) ||
       is_block_empty(nir_if_first_else_block(nif)))
      return false;

   /* Make sure the condition is a comparison operation. */
   nir_instr *src_instr = nif->condition.ssa->parent_instr;
   if (src_instr->type != nir_instr_type_alu)
      return false;

   nir_alu_instr *alu_instr = nir_instr_as_alu(src_instr);
   if (!nir_alu_instr_is_comparison(alu_instr))
      return false;

   /* Insert the inverted instruction and rewrite the condition. */
   b->cursor = nir_after_instr(&alu_instr->instr);

   nir_ssa_def *new_condition =
      nir_inot(b, &alu_instr->dest.dest.ssa);

   nir_if_rewrite_condition(nif, nir_src_for_ssa(new_condition));

   /* Grab pointers to the last then/else blocks for fixing up the phis. */
   nir_block *then_block = nir_if_last_then_block(nif);
   nir_block *else_block = nir_if_last_else_block(nif);

   /* Walk all the phis in the block immediately following the if statement and
    * swap the blocks.
    */
   nir_block *after_if_block =
      nir_cf_node_as_block(nir_cf_node_next(&nif->cf_node));

   nir_foreach_instr(instr, after_if_block) {
      if (instr->type != nir_instr_type_phi)
         continue;

      nir_phi_instr *phi = nir_instr_as_phi(instr);

      foreach_list_typed(nir_phi_src, src, node, &phi->srcs) {
         if (src->pred == else_block) {
            src->pred = then_block;
         } else if (src->pred == then_block) {
            src->pred = else_block;
         }
      }
   }

   /* Finally, move the else block to the then block. */
   nir_cf_list tmp;
   nir_cf_extract(&tmp, nir_before_cf_list(&nif->else_list),
                        nir_after_cf_list(&nif->else_list));
   nir_cf_reinsert(&tmp, nir_before_cf_list(&nif->then_list));

   return true;
}

/**
 * This optimization simplifies potential loop terminators which then allows
 * other passes such as opt_if_simplification() and loop unrolling to progress
 * further:
 *
 *     if (cond) {
 *        ... then block instructions ...
 *     } else {
 *         ...
 *        break;
 *     }
 *
 * into:
 *
 *     if (cond) {
 *     } else {
 *         ...
 *        break;
 *     }
 *     ... then block instructions ...
 */
static bool
opt_if_loop_terminator(nir_if *nif)
{
   nir_block *break_blk = NULL;
   nir_block *continue_from_blk = NULL;
   bool continue_from_then = true;

   nir_block *last_then = nir_if_last_then_block(nif);
   nir_block *last_else = nir_if_last_else_block(nif);

   if (nir_block_ends_in_break(last_then)) {
      break_blk = last_then;
      continue_from_blk = last_else;
      continue_from_then = false;
   } else if (nir_block_ends_in_break(last_else)) {
      break_blk = last_else;
      continue_from_blk = last_then;
   }

   /* Continue if the if-statement contained no jumps at all */
   if (!break_blk)
      return false;

   /* If the continue from block is empty then return as there is nothing to
    * move.
    */
   nir_block *first_continue_from_blk = continue_from_then ?
      nir_if_first_then_block(nif) :
      nir_if_first_else_block(nif);
   if (is_block_empty(first_continue_from_blk))
      return false;

   if (!nir_is_trivial_loop_if(nif, break_blk))
      return false;

   /* Finally, move the continue from branch after the if-statement. */
   nir_cf_list tmp;
   nir_cf_extract(&tmp, nir_before_block(first_continue_from_blk),
                        nir_after_block(continue_from_blk));
   nir_cf_reinsert(&tmp, nir_after_cf_node(&nif->cf_node));

   return true;
}

static bool
evaluate_if_condition(nir_if *nif, nir_cursor cursor, bool *value)
{
   nir_block *use_block = nir_cursor_current_block(cursor);
   if (nir_block_dominates(nir_if_first_then_block(nif), use_block)) {
      *value = true;
      return true;
   } else if (nir_block_dominates(nir_if_first_else_block(nif), use_block)) {
      *value = false;
      return true;
   } else {
      return false;
   }
}

static nir_ssa_def *
clone_alu_and_replace_src_defs(nir_builder *b, const nir_alu_instr *alu,
                               nir_ssa_def **src_defs)
{
   nir_alu_instr *nalu = nir_alu_instr_create(b->shader, alu->op);
   nalu->exact = alu->exact;

   nir_ssa_dest_init(&nalu->instr, &nalu->dest.dest,
                     alu->dest.dest.ssa.num_components,
                     alu->dest.dest.ssa.bit_size, alu->dest.dest.ssa.name);

   nalu->dest.saturate = alu->dest.saturate;
   nalu->dest.write_mask = alu->dest.write_mask;

   for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
      assert(alu->src[i].src.is_ssa);
      nalu->src[i].src = nir_src_for_ssa(src_defs[i]);
      nalu->src[i].negate = alu->src[i].negate;
      nalu->src[i].abs = alu->src[i].abs;
      memcpy(nalu->src[i].swizzle, alu->src[i].swizzle,
             sizeof(nalu->src[i].swizzle));
   }

   nir_builder_instr_insert(b, &nalu->instr);

   return &nalu->dest.dest.ssa;;
}

/*
 * This propagates if condition evaluation down the chain of some alu
 * instructions. For example by checking the use of some of the following alu
 * instruction we can eventually replace ssa_107 with NIR_TRUE.
 *
 *   loop {
 *      block block_1:
 *      vec1 32 ssa_85 = load_const (0x00000002)
 *      vec1 32 ssa_86 = ieq ssa_48, ssa_85
 *      vec1 32 ssa_87 = load_const (0x00000001)
 *      vec1 32 ssa_88 = ieq ssa_48, ssa_87
 *      vec1 32 ssa_89 = ior ssa_86, ssa_88
 *      vec1 32 ssa_90 = ieq ssa_48, ssa_0
 *      vec1 32 ssa_91 = ior ssa_89, ssa_90
 *      if ssa_86 {
 *         block block_2:
 *             ...
 *            break
 *      } else {
 *            block block_3:
 *      }
 *      block block_4:
 *      if ssa_88 {
 *            block block_5:
 *             ...
 *            break
 *      } else {
 *            block block_6:
 *      }
 *      block block_7:
 *      if ssa_90 {
 *            block block_8:
 *             ...
 *            break
 *      } else {
 *            block block_9:
 *      }
 *      block block_10:
 *      vec1 32 ssa_107 = inot ssa_91
 *      if ssa_107 {
 *            block block_11:
 *            break
 *      } else {
 *            block block_12:
 *      }
 *   }
 */
static bool
propagate_condition_eval(nir_builder *b, nir_if *nif, nir_src *use_src,
                         nir_src *alu_use, nir_alu_instr *alu,
                         bool is_if_condition)
{
   bool bool_value;
   b->cursor = nir_before_src(alu_use, is_if_condition);
   if (!evaluate_if_condition(nif, b->cursor, &bool_value))
      return false;

   nir_ssa_def *def[4] = {0};
   for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
      if (alu->src[i].src.ssa == use_src->ssa) {
         def[i] = nir_imm_bool(b, bool_value);
      } else {
         def[i] = alu->src[i].src.ssa;
      }
   }

   nir_ssa_def *nalu = clone_alu_and_replace_src_defs(b, alu, def);

   /* Rewrite use to use new alu instruction */
   nir_src new_src = nir_src_for_ssa(nalu);

   if (is_if_condition)
      nir_if_rewrite_condition(alu_use->parent_if, new_src);
   else
      nir_instr_rewrite_src(alu_use->parent_instr, alu_use, new_src);

   return true;
}

static bool
can_propagate_through_alu(nir_src *src)
{
   if (src->parent_instr->type != nir_instr_type_alu)
      return false;

   nir_alu_instr *alu = nir_instr_as_alu(src->parent_instr);
   switch (alu->op) {
      case nir_op_ior:
      case nir_op_iand:
      case nir_op_inot:
      case nir_op_b2i32:
         return true;
      case nir_op_bcsel:
         return src == &alu->src[0].src;
      default:
         return false;
   }
}

static bool
evaluate_condition_use(nir_builder *b, nir_if *nif, nir_src *use_src,
                       bool is_if_condition)
{
   bool progress = false;

   b->cursor = nir_before_src(use_src, is_if_condition);

   bool bool_value;
   if (evaluate_if_condition(nif, b->cursor, &bool_value)) {
      /* Rewrite use to use const */
      nir_src imm_src = nir_src_for_ssa(nir_imm_bool(b, bool_value));
      if (is_if_condition)
         nir_if_rewrite_condition(use_src->parent_if, imm_src);
      else
         nir_instr_rewrite_src(use_src->parent_instr, use_src, imm_src);

      progress = true;
   }

   if (!is_if_condition && can_propagate_through_alu(use_src)) {
      nir_alu_instr *alu = nir_instr_as_alu(use_src->parent_instr);

      nir_foreach_use_safe(alu_use, &alu->dest.dest.ssa) {
         progress |= propagate_condition_eval(b, nif, use_src, alu_use, alu,
                                              false);
      }

      nir_foreach_if_use_safe(alu_use, &alu->dest.dest.ssa) {
         progress |= propagate_condition_eval(b, nif, use_src, alu_use, alu,
                                              true);
      }
   }

   return progress;
}

static bool
opt_if_evaluate_condition_use(nir_builder *b, nir_if *nif)
{
   bool progress = false;

   /* Evaluate any uses of the if condition inside the if branches */
   assert(nif->condition.is_ssa);
   nir_foreach_use_safe(use_src, nif->condition.ssa) {
      progress |= evaluate_condition_use(b, nif, use_src, false);
   }

   nir_foreach_if_use_safe(use_src, nif->condition.ssa) {
      if (use_src->parent_if != nif)
         progress |= evaluate_condition_use(b, nif, use_src, true);
   }

   return progress;
}

static bool
opt_if_cf_list(nir_builder *b, struct exec_list *cf_list)
{
   bool progress = false;
   foreach_list_typed(nir_cf_node, cf_node, node, cf_list) {
      switch (cf_node->type) {
      case nir_cf_node_block:
         break;

      case nir_cf_node_if: {
         nir_if *nif = nir_cf_node_as_if(cf_node);
         progress |= opt_if_cf_list(b, &nif->then_list);
         progress |= opt_if_cf_list(b, &nif->else_list);
         progress |= opt_if_loop_terminator(nif);
         progress |= opt_if_simplification(b, nif);
         break;
      }

      case nir_cf_node_loop: {
         nir_loop *loop = nir_cf_node_as_loop(cf_node);
         progress |= opt_if_cf_list(b, &loop->body);
         progress |= opt_peel_loop_initial_if(loop);
         break;
      }

      case nir_cf_node_function:
         unreachable("Invalid cf type");
      }
   }

   return progress;
}

/**
 * These optimisations depend on nir_metadata_block_index and therefore must
 * not do anything to cause the metadata to become invalid.
 */
static bool
opt_if_safe_cf_list(nir_builder *b, struct exec_list *cf_list)
{
   bool progress = false;
   foreach_list_typed(nir_cf_node, cf_node, node, cf_list) {
      switch (cf_node->type) {
      case nir_cf_node_block:
         break;

      case nir_cf_node_if: {
         nir_if *nif = nir_cf_node_as_if(cf_node);
         progress |= opt_if_safe_cf_list(b, &nif->then_list);
         progress |= opt_if_safe_cf_list(b, &nif->else_list);
         progress |= opt_if_evaluate_condition_use(b, nif);
         break;
      }

      case nir_cf_node_loop: {
         nir_loop *loop = nir_cf_node_as_loop(cf_node);
         progress |= opt_if_safe_cf_list(b, &loop->body);
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

      nir_builder b;
      nir_builder_init(&b, function->impl);

      nir_metadata_require(function->impl, nir_metadata_block_index |
                           nir_metadata_dominance);
      progress = opt_if_safe_cf_list(&b, &function->impl->body);
      nir_metadata_preserve(function->impl, nir_metadata_block_index |
                            nir_metadata_dominance);

      if (opt_if_cf_list(&b, &function->impl->body)) {
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
