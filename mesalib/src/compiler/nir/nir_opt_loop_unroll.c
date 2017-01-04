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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_control_flow.h"
#include "nir_loop_analyze.h"

/* Prepare this loop for unrolling by first converting to lcssa and then
 * converting the phis from the loops first block and the block that follows
 * the loop into regs.  Partially converting out of SSA allows us to unroll
 * the loop without having to keep track of and update phis along the way
 * which gets tricky and doesn't add much value over conveting to regs.
 *
 * The loop may have a continue instruction at the end of the loop which does
 * nothing.  Once we're out of SSA, we can safely delete it so we don't have
 * to deal with it later.
 */
static void
loop_prepare_for_unroll(nir_loop *loop)
{
   nir_convert_loop_to_lcssa(loop);

   nir_lower_phis_to_regs_block(nir_loop_first_block(loop));

   nir_block *block_after_loop =
      nir_cf_node_as_block(nir_cf_node_next(&loop->cf_node));

   nir_lower_phis_to_regs_block(block_after_loop);

   nir_instr *last_instr = nir_block_last_instr(nir_loop_last_block(loop));
   if (last_instr && last_instr->type == nir_instr_type_jump) {
      assert(nir_instr_as_jump(last_instr)->type == nir_jump_continue);
      nir_instr_remove(last_instr);
   }
}

static void
get_first_blocks_in_terminator(nir_loop_terminator *term,
                               nir_block **first_break_block,
                               nir_block **first_continue_block)
{
   if (term->continue_from_then) {
      *first_continue_block = nir_if_first_then_block(term->nif);
      *first_break_block = nir_if_first_else_block(term->nif);
   } else {
      *first_continue_block = nir_if_first_else_block(term->nif);
      *first_break_block = nir_if_first_then_block(term->nif);
   }
}

/**
 * Unroll a loop where we know exactly how many iterations there are and there
 * is only a single exit point.  Note here we can unroll loops with multiple
 * theoretical exits that only have a single terminating exit that we always
 * know is the "real" exit.
 *
 *     loop {
 *         ...instrs...
 *     }
 *
 * And the iteration count is 3, the output will be:
 *
 *     ...instrs... ...instrs... ...instrs...
 */
static void
simple_unroll(nir_loop *loop)
{
   nir_loop_terminator *limiting_term = loop->info->limiting_terminator;
   assert(nir_is_trivial_loop_if(limiting_term->nif,
                                 limiting_term->break_block));

   loop_prepare_for_unroll(loop);

   /* Skip over loop terminator and get the loop body. */
   list_for_each_entry(nir_loop_terminator, terminator,
                       &loop->info->loop_terminator_list,
                       loop_terminator_link) {

      /* Remove all but the limiting terminator as we know the other exit
       * conditions can never be met. Note we need to extract any instructions
       * in the continue from branch and insert then into the loop body before
       * removing it.
       */
      if (terminator->nif != limiting_term->nif) {
         nir_block *first_break_block;
         nir_block *first_continue_block;
         get_first_blocks_in_terminator(terminator, &first_break_block,
                                        &first_continue_block);

         assert(nir_is_trivial_loop_if(terminator->nif,
                                       terminator->break_block));

         nir_cf_list continue_from_lst;
         nir_cf_extract(&continue_from_lst,
                        nir_before_block(first_continue_block),
                        nir_after_block(terminator->continue_from_block));
         nir_cf_reinsert(&continue_from_lst,
                         nir_after_cf_node(&terminator->nif->cf_node));

         nir_cf_node_remove(&terminator->nif->cf_node);
      }
   }

   nir_block *first_break_block;
   nir_block *first_continue_block;
   get_first_blocks_in_terminator(limiting_term, &first_break_block,
                                  &first_continue_block);

   /* Pluck out the loop header */
   nir_block *header_blk = nir_loop_first_block(loop);
   nir_cf_list lp_header;
   nir_cf_extract(&lp_header, nir_before_block(header_blk),
                  nir_before_cf_node(&limiting_term->nif->cf_node));

   /* Add the continue from block of the limiting terminator to the loop body
    */
   nir_cf_list continue_from_lst;
   nir_cf_extract(&continue_from_lst, nir_before_block(first_continue_block),
                  nir_after_block(limiting_term->continue_from_block));
   nir_cf_reinsert(&continue_from_lst,
                   nir_after_cf_node(&limiting_term->nif->cf_node));

   /* Pluck out the loop body */
   nir_cf_list loop_body;
   nir_cf_extract(&loop_body, nir_after_cf_node(&limiting_term->nif->cf_node),
                  nir_after_block(nir_loop_last_block(loop)));

   struct hash_table *remap_table =
      _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                              _mesa_key_pointer_equal);

   /* Clone the loop header */
   nir_cf_list cloned_header;
   nir_cf_list_clone(&cloned_header, &lp_header, loop->cf_node.parent,
                     remap_table);

   /* Insert cloned loop header before the loop */
   nir_cf_reinsert(&cloned_header, nir_before_cf_node(&loop->cf_node));

   /* Temp list to store the cloned loop body as we unroll */
   nir_cf_list unrolled_lp_body;

   /* Clone loop header and append to the loop body */
   for (unsigned i = 0; i < loop->info->trip_count; i++) {
      /* Clone loop body */
      nir_cf_list_clone(&unrolled_lp_body, &loop_body, loop->cf_node.parent,
                        remap_table);

      /* Insert unrolled loop body before the loop */
      nir_cf_reinsert(&unrolled_lp_body, nir_before_cf_node(&loop->cf_node));

      /* Clone loop header */
      nir_cf_list_clone(&cloned_header, &lp_header, loop->cf_node.parent,
                        remap_table);

      /* Insert loop header after loop body */
      nir_cf_reinsert(&cloned_header, nir_before_cf_node(&loop->cf_node));
   }

   /* Remove the break from the loop terminator and add instructions from
    * the break block after the unrolled loop.
    */
   nir_instr *break_instr = nir_block_last_instr(limiting_term->break_block);
   nir_instr_remove(break_instr);
   nir_cf_list break_list;
   nir_cf_extract(&break_list, nir_before_block(first_break_block),
                  nir_after_block(limiting_term->break_block));

   /* Clone so things get properly remapped */
   nir_cf_list cloned_break_list;
   nir_cf_list_clone(&cloned_break_list, &break_list, loop->cf_node.parent,
                     remap_table);

   nir_cf_reinsert(&cloned_break_list, nir_before_cf_node(&loop->cf_node));

   /* Remove the loop */
   nir_cf_node_remove(&loop->cf_node);

   /* Delete the original loop body, break block & header */
   nir_cf_delete(&lp_header);
   nir_cf_delete(&loop_body);
   nir_cf_delete(&break_list);

   _mesa_hash_table_destroy(remap_table, NULL);
}

static void
move_cf_list_into_loop_term(nir_cf_list *lst, nir_loop_terminator *term)
{
   /* Move the rest of the loop inside the continue-from-block */
   nir_cf_reinsert(lst, nir_after_block(term->continue_from_block));

   /* Remove the break */
   nir_instr_remove(nir_block_last_instr(term->break_block));
}

static nir_cursor
get_complex_unroll_insert_location(nir_cf_node *node, bool continue_from_then)
{
   if (node->type == nir_cf_node_loop) {
      return nir_before_cf_node(node);
   } else {
      nir_if *if_stmt = nir_cf_node_as_if(node);
      if (continue_from_then) {
         return nir_after_block(nir_if_last_then_block(if_stmt));
      } else {
         return nir_after_block(nir_if_last_else_block(if_stmt));
      }
   }
}

/**
 * Unroll a loop with two exists when the trip count of one of the exits is
 * unknown.  If continue_from_then is true, the loop is repeated only when the
 * "then" branch of the if is taken; otherwise it is repeated only
 * when the "else" branch of the if is taken.
 *
 * For example, if the input is:
 *
 *      loop {
 *         ...phis/condition...
 *         if condition {
 *            ...then instructions...
 *         } else {
 *            ...continue instructions...
 *            break
 *         }
 *         ...body...
 *      }
 *
 * And the iteration count is 3, and unlimit_term->continue_from_then is true,
 * then the output will be:
 *
 *      ...condition...
 *      if condition {
 *         ...then instructions...
 *         ...body...
 *         if condition {
 *            ...then instructions...
 *            ...body...
 *            if condition {
 *               ...then instructions...
 *               ...body...
 *            } else {
 *               ...continue instructions...
 *            }
 *         } else {
 *            ...continue instructions...
 *         }
 *      } else {
 *         ...continue instructions...
 *      }
 */
static void
complex_unroll(nir_loop *loop, nir_loop_terminator *unlimit_term,
               bool limiting_term_second)
{
   assert(nir_is_trivial_loop_if(unlimit_term->nif,
                                 unlimit_term->break_block));

   nir_loop_terminator *limiting_term = loop->info->limiting_terminator;
   assert(nir_is_trivial_loop_if(limiting_term->nif,
                                 limiting_term->break_block));

   loop_prepare_for_unroll(loop);

   nir_block *header_blk = nir_loop_first_block(loop);

   nir_cf_list lp_header;
   nir_cf_list limit_break_list;
   unsigned num_times_to_clone;
   if (limiting_term_second) {
      /* Pluck out the loop header */
      nir_cf_extract(&lp_header, nir_before_block(header_blk),
                     nir_before_cf_node(&unlimit_term->nif->cf_node));

      /* We need some special handling when its the second terminator causing
       * us to exit the loop for example:
       *
       *   for (int i = 0; i < uniform_lp_count; i++) {
       *      colour = vec4(0.0, 1.0, 0.0, 1.0);
       *
       *      if (i == 1) {
       *         break;
       *      }
       *      ... any further code is unreachable after i == 1 ...
       *   }
       */
      nir_cf_list after_lt;
      nir_if *limit_if = limiting_term->nif;
      nir_cf_extract(&after_lt, nir_after_cf_node(&limit_if->cf_node),
                     nir_after_block(nir_loop_last_block(loop)));
      move_cf_list_into_loop_term(&after_lt, limiting_term);

      /* Because the trip count is the number of times we pass over the entire
       * loop before hitting a break when the second terminator is the
       * limiting terminator we can actually execute code inside the loop when
       * trip count == 0 e.g. the code above the break.  So we need to bump
       * the trip_count in order for the code below to clone anything.  When
       * trip count == 1 we execute the code above the break twice and the
       * code below it once so we need clone things twice and so on.
       */
      num_times_to_clone = loop->info->trip_count + 1;
   } else {
      /* Pluck out the loop header */
      nir_cf_extract(&lp_header, nir_before_block(header_blk),
                     nir_before_cf_node(&limiting_term->nif->cf_node));

      nir_block *first_break_block;
      nir_block *first_continue_block;
      get_first_blocks_in_terminator(limiting_term, &first_break_block,
                                     &first_continue_block);

      /* Remove the break then extract instructions from the break block so we
       * can insert them in the innermost else of the unrolled loop.
       */
      nir_instr *break_instr = nir_block_last_instr(limiting_term->break_block);
      nir_instr_remove(break_instr);
      nir_cf_extract(&limit_break_list, nir_before_block(first_break_block),
                     nir_after_block(limiting_term->break_block));

      nir_cf_list continue_list;
      nir_cf_extract(&continue_list, nir_before_block(first_continue_block),
                     nir_after_block(limiting_term->continue_from_block));

      nir_cf_reinsert(&continue_list,
                      nir_after_cf_node(&limiting_term->nif->cf_node));

      nir_cf_node_remove(&limiting_term->nif->cf_node);

      num_times_to_clone = loop->info->trip_count;
   }

   /* In the terminator that we have no trip count for move everything after
    * the terminator into the continue from branch.
    */
   nir_cf_list loop_end;
   nir_cf_extract(&loop_end, nir_after_cf_node(&unlimit_term->nif->cf_node),
                  nir_after_block(nir_loop_last_block(loop)));
   move_cf_list_into_loop_term(&loop_end, unlimit_term);

   /* Pluck out the loop body. */
   nir_cf_list loop_body;
   nir_cf_extract(&loop_body, nir_before_block(nir_loop_first_block(loop)),
                  nir_after_block(nir_loop_last_block(loop)));

   struct hash_table *remap_table =
      _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                              _mesa_key_pointer_equal);

   /* Set unroll_loc to the loop as we will insert the unrolled loop before it
    */
   nir_cf_node *unroll_loc = &loop->cf_node;

   /* Temp lists to store the cloned loop as we unroll */
   nir_cf_list unrolled_lp_body;
   nir_cf_list cloned_header;

   for (unsigned i = 0; i < num_times_to_clone; i++) {
      /* Clone loop header */
      nir_cf_list_clone(&cloned_header, &lp_header, loop->cf_node.parent,
                        remap_table);

      nir_cursor cursor =
         get_complex_unroll_insert_location(unroll_loc,
                                            unlimit_term->continue_from_then);

      /* Insert cloned loop header */
      nir_cf_reinsert(&cloned_header, cursor);

      cursor =
         get_complex_unroll_insert_location(unroll_loc,
                                            unlimit_term->continue_from_then);

      /* Clone loop body */
      nir_cf_list_clone(&unrolled_lp_body, &loop_body, loop->cf_node.parent,
                        remap_table);

      unroll_loc = exec_node_data(nir_cf_node,
                                  exec_list_get_tail(&unrolled_lp_body.list),
                                  node);
      assert(unroll_loc->type == nir_cf_node_block &&
             exec_list_is_empty(&nir_cf_node_as_block(unroll_loc)->instr_list));

      /* Get the unrolled if node */
      unroll_loc = nir_cf_node_prev(unroll_loc);

      /* Insert unrolled loop body */
      nir_cf_reinsert(&unrolled_lp_body, cursor);
   }

   if (!limiting_term_second) {
      assert(unroll_loc->type == nir_cf_node_if);

      nir_cf_list_clone(&cloned_header, &lp_header, loop->cf_node.parent,
                        remap_table);

      nir_cursor cursor =
         get_complex_unroll_insert_location(unroll_loc,
                                            unlimit_term->continue_from_then);

      /* Insert cloned loop header */
      nir_cf_reinsert(&cloned_header, cursor);

      /* Clone so things get properly remapped, and insert break block from
       * the limiting terminator.
       */
      nir_cf_list cloned_break_blk;
      nir_cf_list_clone(&cloned_break_blk, &limit_break_list,
                        loop->cf_node.parent, remap_table);

      cursor =
         get_complex_unroll_insert_location(unroll_loc,
                                            unlimit_term->continue_from_then);

      nir_cf_reinsert(&cloned_break_blk, cursor);
      nir_cf_delete(&limit_break_list);
   }

   /* The loop has been unrolled so remove it. */
   nir_cf_node_remove(&loop->cf_node);

   /* Delete the original loop header and body */
   nir_cf_delete(&lp_header);
   nir_cf_delete(&loop_body);

   _mesa_hash_table_destroy(remap_table, NULL);
}

static bool
is_loop_small_enough_to_unroll(nir_shader *shader, nir_loop_info *li)
{
   unsigned max_iter = shader->options->max_unroll_iterations;

   if (li->trip_count > max_iter)
      return false;

   if (li->force_unroll)
      return true;

   bool loop_not_too_large =
      li->num_instructions * li->trip_count <= max_iter * 25;

   return loop_not_too_large;
}

static bool
process_loops(nir_shader *sh, nir_cf_node *cf_node, bool *innermost_loop)
{
   bool progress = false;
   nir_loop *loop;

   switch (cf_node->type) {
   case nir_cf_node_block:
      return progress;
   case nir_cf_node_if: {
      nir_if *if_stmt = nir_cf_node_as_if(cf_node);
      foreach_list_typed_safe(nir_cf_node, nested_node, node, &if_stmt->then_list)
         progress |= process_loops(sh, nested_node, innermost_loop);
      foreach_list_typed_safe(nir_cf_node, nested_node, node, &if_stmt->else_list)
         progress |= process_loops(sh, nested_node, innermost_loop);
      return progress;
   }
   case nir_cf_node_loop: {
      loop = nir_cf_node_as_loop(cf_node);
      foreach_list_typed_safe(nir_cf_node, nested_node, node, &loop->body)
         progress |= process_loops(sh, nested_node, innermost_loop);
      break;
   }
   default:
      unreachable("unknown cf node type");
   }

   if (*innermost_loop) {
      /* Don't attempt to unroll outer loops or a second inner loop in
       * this pass wait until the next pass as we have altered the cf.
       */
      *innermost_loop = false;

      if (loop->info->limiting_terminator == NULL)
         return progress;

      if (!is_loop_small_enough_to_unroll(sh, loop->info))
         return progress;

      if (loop->info->is_trip_count_known) {
         simple_unroll(loop);
         progress = true;
      } else {
         /* Attempt to unroll loops with two terminators. */
         unsigned num_lt = list_length(&loop->info->loop_terminator_list);
         if (num_lt == 2) {
            bool limiting_term_second = true;
            nir_loop_terminator *terminator =
               list_last_entry(&loop->info->loop_terminator_list,
                                nir_loop_terminator, loop_terminator_link);


            if (terminator->nif == loop->info->limiting_terminator->nif) {
               limiting_term_second = false;
               terminator =
                  list_first_entry(&loop->info->loop_terminator_list,
                                  nir_loop_terminator, loop_terminator_link);
            }

            /* If the first terminator has a trip count of zero and is the
             * limiting terminator just do a simple unroll as the second
             * terminator can never be reached.
             */
            if (loop->info->trip_count == 0 && !limiting_term_second) {
               simple_unroll(loop);
            } else {
               complex_unroll(loop, terminator, limiting_term_second);
            }
            progress = true;
         }
      }
   }

   return progress;
}

static bool
nir_opt_loop_unroll_impl(nir_function_impl *impl,
                         nir_variable_mode indirect_mask)
{
   bool progress = false;
   nir_metadata_require(impl, nir_metadata_loop_analysis, indirect_mask);
   nir_metadata_require(impl, nir_metadata_block_index);

   foreach_list_typed_safe(nir_cf_node, node, node, &impl->body) {
      bool innermost_loop = true;
      progress |= process_loops(impl->function->shader, node,
                                &innermost_loop);
   }

   if (progress)
      nir_lower_regs_to_ssa_impl(impl);

   return progress;
}

bool
nir_opt_loop_unroll(nir_shader *shader, nir_variable_mode indirect_mask)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl) {
         progress |= nir_opt_loop_unroll_impl(function->impl, indirect_mask);
      }
   }
   return progress;
}
