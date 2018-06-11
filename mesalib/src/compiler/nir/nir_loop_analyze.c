/*
 * Copyright © 2015 Thomas Helland
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
#include "nir_constant_expressions.h"
#include "nir_loop_analyze.h"

typedef enum {
   undefined,
   invariant,
   not_invariant,
   basic_induction
} nir_loop_variable_type;

struct nir_basic_induction_var;

typedef struct {
   /* A link for the work list */
   struct list_head process_link;

   bool in_loop;

   /* The ssa_def associated with this info */
   nir_ssa_def *def;

   /* The type of this ssa_def */
   nir_loop_variable_type type;

   /* If this is of type basic_induction */
   struct nir_basic_induction_var *ind;

   /* True if variable is in an if branch or a nested loop */
   bool in_control_flow;

} nir_loop_variable;

typedef struct nir_basic_induction_var {
   nir_op alu_op;                           /* The type of alu-operation    */
   nir_loop_variable *alu_def;              /* The def of the alu-operation */
   nir_loop_variable *invariant;            /* The invariant alu-operand    */
   nir_loop_variable *def_outside_loop;     /* The phi-src outside the loop */
} nir_basic_induction_var;

typedef struct {
   /* The loop we store information for */
   nir_loop *loop;

   /* Loop_variable for all ssa_defs in function */
   nir_loop_variable *loop_vars;

   /* A list of the loop_vars to analyze */
   struct list_head process_list;

   nir_variable_mode indirect_mask;

} loop_info_state;

static nir_loop_variable *
get_loop_var(nir_ssa_def *value, loop_info_state *state)
{
   return &(state->loop_vars[value->index]);
}

typedef struct {
   loop_info_state *state;
   bool in_control_flow;
} init_loop_state;

static bool
init_loop_def(nir_ssa_def *def, void *void_init_loop_state)
{
   init_loop_state *loop_init_state = void_init_loop_state;
   nir_loop_variable *var = get_loop_var(def, loop_init_state->state);

   if (loop_init_state->in_control_flow) {
      var->in_control_flow = true;
   } else {
      /* Add to the tail of the list. That way we start at the beginning of
       * the defs in the loop instead of the end when walking the list. This
       * means less recursive calls. Only add defs that are not in nested
       * loops or conditional blocks.
       */
      list_addtail(&var->process_link, &loop_init_state->state->process_list);
   }

   var->in_loop = true;

   return true;
}

static bool
init_loop_block(nir_block *block, loop_info_state *state,
                bool in_control_flow)
{
   init_loop_state init_state = {.in_control_flow = in_control_flow,
                                 .state = state };

   nir_foreach_instr(instr, block) {
      if (instr->type == nir_instr_type_intrinsic ||
          instr->type == nir_instr_type_alu ||
          instr->type == nir_instr_type_tex) {
         state->loop->info->num_instructions++;
      }

      nir_foreach_ssa_def(instr, init_loop_def, &init_state);
   }

   return true;
}

static inline bool
is_var_alu(nir_loop_variable *var)
{
   return var->def->parent_instr->type == nir_instr_type_alu;
}

static inline bool
is_var_constant(nir_loop_variable *var)
{
   return var->def->parent_instr->type == nir_instr_type_load_const;
}

static inline bool
is_var_phi(nir_loop_variable *var)
{
   return var->def->parent_instr->type == nir_instr_type_phi;
}

static inline bool
mark_invariant(nir_ssa_def *def, loop_info_state *state)
{
   nir_loop_variable *var = get_loop_var(def, state);

   if (var->type == invariant)
      return true;

   if (!var->in_loop) {
      var->type = invariant;
      return true;
   }

   if (var->type == not_invariant)
      return false;

   if (is_var_alu(var)) {
      nir_alu_instr *alu = nir_instr_as_alu(def->parent_instr);

      for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
         if (!mark_invariant(alu->src[i].src.ssa, state)) {
            var->type = not_invariant;
            return false;
         }
      }
      var->type = invariant;
      return true;
   }

   /* Phis shouldn't be invariant except if one operand is invariant, and the
    * other is the phi itself. These should be removed by opt_remove_phis.
    * load_consts are already set to invariant and constant during init,
    * and so should return earlier. Remaining op_codes are set undefined.
    */
   var->type = not_invariant;
   return false;
}

static void
compute_invariance_information(loop_info_state *state)
{
   /* An expression is invariant in a loop L if:
    *  (base cases)
    *    – it’s a constant
    *    – it’s a variable use, all of whose single defs are outside of L
    *  (inductive cases)
    *    – it’s a pure computation all of whose args are loop invariant
    *    – it’s a variable use whose single reaching def, and the
    *      rhs of that def is loop-invariant
    */
   list_for_each_entry_safe(nir_loop_variable, var, &state->process_list,
                            process_link) {
      assert(!var->in_control_flow);

      if (mark_invariant(var->def, state))
         list_del(&var->process_link);
   }
}

static bool
compute_induction_information(loop_info_state *state)
{
   bool found_induction_var = false;
   list_for_each_entry_safe(nir_loop_variable, var, &state->process_list,
                            process_link) {

      /* It can't be an induction variable if it is invariant. Invariants and
       * things in nested loops or conditionals should have been removed from
       * the list by compute_invariance_information().
       */
      assert(!var->in_control_flow && var->type != invariant);

      /* We are only interested in checking phis for the basic induction
       * variable case as its simple to detect. All basic induction variables
       * have a phi node
       */
      if (!is_var_phi(var))
         continue;

      nir_phi_instr *phi = nir_instr_as_phi(var->def->parent_instr);
      nir_basic_induction_var *biv = rzalloc(state, nir_basic_induction_var);

      nir_foreach_phi_src(src, phi) {
         nir_loop_variable *src_var = get_loop_var(src->src.ssa, state);

         /* If one of the sources is in a conditional or nested block then
          * panic.
          */
         if (src_var->in_control_flow)
            break;

         if (!src_var->in_loop) {
            biv->def_outside_loop = src_var;
         } else if (is_var_alu(src_var)) {
            nir_alu_instr *alu = nir_instr_as_alu(src_var->def->parent_instr);

            if (nir_op_infos[alu->op].num_inputs == 2) {
               biv->alu_def = src_var;
               biv->alu_op = alu->op;

               for (unsigned i = 0; i < 2; i++) {
                  /* Is one of the operands const, and the other the phi */
                  if (alu->src[i].src.ssa->parent_instr->type == nir_instr_type_load_const &&
                      alu->src[1-i].src.ssa == &phi->dest.ssa)
                     biv->invariant = get_loop_var(alu->src[i].src.ssa, state);
               }
            }
         }
      }

      if (biv->alu_def && biv->def_outside_loop && biv->invariant &&
          is_var_constant(biv->def_outside_loop)) {
         assert(is_var_constant(biv->invariant));
         biv->alu_def->type = basic_induction;
         biv->alu_def->ind = biv;
         var->type = basic_induction;
         var->ind = biv;

         found_induction_var = true;
      } else {
         ralloc_free(biv);
      }
   }
   return found_induction_var;
}

static bool
initialize_ssa_def(nir_ssa_def *def, void *void_state)
{
   loop_info_state *state = void_state;
   nir_loop_variable *var = get_loop_var(def, state);

   var->in_loop = false;
   var->def = def;

   if (def->parent_instr->type == nir_instr_type_load_const) {
      var->type = invariant;
   } else {
      var->type = undefined;
   }

   return true;
}

static bool
find_loop_terminators(loop_info_state *state)
{
   bool success = false;
   foreach_list_typed_safe(nir_cf_node, node, node, &state->loop->body) {
      if (node->type == nir_cf_node_if) {
         nir_if *nif = nir_cf_node_as_if(node);

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

         /* If there is a break then we should find a terminator. If we can
          * not find a loop terminator, but there is a break-statement then
          * we should return false so that we do not try to find trip-count
          */
         if (!nir_is_trivial_loop_if(nif, break_blk))
            return false;

         /* Continue if the if contained no jumps at all */
         if (!break_blk)
            continue;

         if (nif->condition.ssa->parent_instr->type == nir_instr_type_phi)
            return false;

         nir_loop_terminator *terminator =
            rzalloc(state->loop->info, nir_loop_terminator);

         list_add(&terminator->loop_terminator_link,
                  &state->loop->info->loop_terminator_list);

         terminator->nif = nif;
         terminator->break_block = break_blk;
         terminator->continue_from_block = continue_from_blk;
         terminator->continue_from_then = continue_from_then;
         terminator->conditional_instr = nif->condition.ssa->parent_instr;

         success = true;
      }
   }

   return success;
}

static int32_t
get_iteration(nir_op cond_op, nir_const_value *initial, nir_const_value *step,
              nir_const_value *limit)
{
   int32_t iter;

   switch (cond_op) {
   case nir_op_ige:
   case nir_op_ilt:
   case nir_op_ieq:
   case nir_op_ine: {
      int32_t initial_val = initial->i32[0];
      int32_t span = limit->i32[0] - initial_val;
      iter = span / step->i32[0];
      break;
   }
   case nir_op_uge:
   case nir_op_ult: {
      uint32_t initial_val = initial->u32[0];
      uint32_t span = limit->u32[0] - initial_val;
      iter = span / step->u32[0];
      break;
   }
   case nir_op_fge:
   case nir_op_flt:
   case nir_op_feq:
   case nir_op_fne: {
      float initial_val = initial->f32[0];
      float span = limit->f32[0] - initial_val;
      iter = span / step->f32[0];
      break;
   }
   default:
      return -1;
   }

   return iter;
}

static bool
test_iterations(int32_t iter_int, nir_const_value *step,
                nir_const_value *limit, nir_op cond_op, unsigned bit_size,
                nir_alu_type induction_base_type,
                nir_const_value *initial, bool limit_rhs, bool invert_cond)
{
   assert(nir_op_infos[cond_op].num_inputs == 2);

   nir_const_value iter_src = { {0, } };
   nir_op mul_op;
   nir_op add_op;
   switch (induction_base_type) {
   case nir_type_float:
      iter_src.f32[0] = (float) iter_int;
      mul_op = nir_op_fmul;
      add_op = nir_op_fadd;
      break;
   case nir_type_int:
   case nir_type_uint:
      iter_src.i32[0] = iter_int;
      mul_op = nir_op_imul;
      add_op = nir_op_iadd;
      break;
   default:
      unreachable("Unhandled induction variable base type!");
   }

   /* Multiple the iteration count we are testing by the number of times we
    * step the induction variable each iteration.
    */
   nir_const_value mul_src[2] = { iter_src, *step };
   nir_const_value mul_result =
      nir_eval_const_opcode(mul_op, 1, bit_size, mul_src);

   /* Add the initial value to the accumulated induction variable total */
   nir_const_value add_src[2] = { mul_result, *initial };
   nir_const_value add_result =
      nir_eval_const_opcode(add_op, 1, bit_size, add_src);

   nir_const_value src[2] = { { {0, } }, { {0, } } };
   src[limit_rhs ? 0 : 1] = add_result;
   src[limit_rhs ? 1 : 0] = *limit;

   /* Evaluate the loop exit condition */
   nir_const_value result = nir_eval_const_opcode(cond_op, 1, bit_size, src);

   return invert_cond ? (result.u32[0] == 0) : (result.u32[0] != 0);
}

static int
calculate_iterations(nir_const_value *initial, nir_const_value *step,
                     nir_const_value *limit, nir_loop_variable *alu_def,
                     nir_alu_instr *cond_alu, bool limit_rhs, bool invert_cond)
{
   assert(initial != NULL && step != NULL && limit != NULL);

   nir_alu_instr *alu = nir_instr_as_alu(alu_def->def->parent_instr);

   /* nir_op_isub should have been lowered away by this point */
   assert(alu->op != nir_op_isub);

   /* Make sure the alu type for our induction variable is compatible with the
    * conditional alus input type. If its not something has gone really wrong.
    */
   nir_alu_type induction_base_type =
      nir_alu_type_get_base_type(nir_op_infos[alu->op].output_type);
   if (induction_base_type == nir_type_int || induction_base_type == nir_type_uint) {
      assert(nir_alu_type_get_base_type(nir_op_infos[cond_alu->op].input_types[1]) == nir_type_int ||
             nir_alu_type_get_base_type(nir_op_infos[cond_alu->op].input_types[1]) == nir_type_uint);
   } else {
      assert(nir_alu_type_get_base_type(nir_op_infos[cond_alu->op].input_types[0]) ==
             induction_base_type);
   }

   /* Check for nsupported alu operations */
   if (alu->op != nir_op_iadd && alu->op != nir_op_fadd)
      return -1;

   /* do-while loops can increment the starting value before the condition is
    * checked. e.g.
    *
    *    do {
    *        ndx++;
    *     } while (ndx < 3);
    *
    * Here we check if the induction variable is used directly by the loop
    * condition and if so we assume we need to step the initial value.
    */
   unsigned trip_offset = 0;
   if (cond_alu->src[0].src.ssa == alu_def->def ||
       cond_alu->src[1].src.ssa == alu_def->def) {
      trip_offset = 1;
   }

   int iter_int = get_iteration(cond_alu->op, initial, step, limit);

   /* If iter_int is negative the loop is ill-formed or is the conditional is
    * unsigned with a huge iteration count so don't bother going any further.
    */
   if (iter_int < 0)
      return -1;

   /* An explanation from the GLSL unrolling pass:
    *
    * Make sure that the calculated number of iterations satisfies the exit
    * condition.  This is needed to catch off-by-one errors and some types of
    * ill-formed loops.  For example, we need to detect that the following
    * loop does not have a maximum iteration count.
    *
    *    for (float x = 0.0; x != 0.9; x += 0.2);
    */
   assert(nir_src_bit_size(alu->src[0].src) ==
          nir_src_bit_size(alu->src[1].src));
   unsigned bit_size = nir_src_bit_size(alu->src[0].src);
   for (int bias = -1; bias <= 1; bias++) {
      const int iter_bias = iter_int + bias;

      if (test_iterations(iter_bias, step, limit, cond_alu->op, bit_size,
                          induction_base_type, initial,
                          limit_rhs, invert_cond)) {
         return iter_bias > 0 ? iter_bias - trip_offset : iter_bias;
      }
   }

   return -1;
}

/* Run through each of the terminators of the loop and try to infer a possible
 * trip-count. We need to check them all, and set the lowest trip-count as the
 * trip-count of our loop. If one of the terminators has an undecidable
 * trip-count we can not safely assume anything about the duration of the
 * loop.
 */
static void
find_trip_count(loop_info_state *state)
{
   bool trip_count_known = true;
   nir_loop_terminator *limiting_terminator = NULL;
   int min_trip_count = -1;

   list_for_each_entry(nir_loop_terminator, terminator,
                       &state->loop->info->loop_terminator_list,
                       loop_terminator_link) {

      if (terminator->conditional_instr->type != nir_instr_type_alu) {
         /* If we get here the loop is dead and will get cleaned up by the
          * nir_opt_dead_cf pass.
          */
         trip_count_known = false;
         continue;
      }

      nir_alu_instr *alu = nir_instr_as_alu(terminator->conditional_instr);
      nir_loop_variable *basic_ind = NULL;
      nir_loop_variable *limit = NULL;
      bool limit_rhs = true;

      switch (alu->op) {
      case nir_op_fge:      case nir_op_ige:      case nir_op_uge:
      case nir_op_flt:      case nir_op_ilt:      case nir_op_ult:
      case nir_op_feq:      case nir_op_ieq:
      case nir_op_fne:      case nir_op_ine:

         /* We assume that the limit is the "right" operand */
         basic_ind = get_loop_var(alu->src[0].src.ssa, state);
         limit = get_loop_var(alu->src[1].src.ssa, state);

         if (basic_ind->type != basic_induction) {
            /* We had it the wrong way, flip things around */
            basic_ind = get_loop_var(alu->src[1].src.ssa, state);
            limit = get_loop_var(alu->src[0].src.ssa, state);
            limit_rhs = false;
         }

         /* The comparison has to have a basic induction variable
          * and a constant for us to be able to find trip counts
          */
         if (basic_ind->type != basic_induction || !is_var_constant(limit)) {
            trip_count_known = false;
            continue;
         }

         /* We have determined that we have the following constants:
          * (With the typical int i = 0; i < x; i++; as an example)
          *    - Upper limit.
          *    - Starting value
          *    - Step / iteration size
          * Thats all thats needed to calculate the trip-count
          */

         nir_const_value initial_val =
            nir_instr_as_load_const(basic_ind->ind->def_outside_loop->
                                       def->parent_instr)->value;

         nir_const_value step_val =
            nir_instr_as_load_const(basic_ind->ind->invariant->def->
                                       parent_instr)->value;

         nir_const_value limit_val =
            nir_instr_as_load_const(limit->def->parent_instr)->value;

         int iterations = calculate_iterations(&initial_val, &step_val,
                                               &limit_val,
                                               basic_ind->ind->alu_def, alu,
                                               limit_rhs,
                                               terminator->continue_from_then);

         /* Where we not able to calculate the iteration count */
         if (iterations == -1) {
            trip_count_known = false;
            continue;
         }

         /* If this is the first run or we have found a smaller amount of
          * iterations than previously (we have identified a more limiting
          * terminator) set the trip count and limiting terminator.
          */
         if (min_trip_count == -1 || iterations < min_trip_count) {
            min_trip_count = iterations;
            limiting_terminator = terminator;
         }
         break;

      default:
         trip_count_known = false;
      }
   }

   state->loop->info->is_trip_count_known = trip_count_known;
   if (min_trip_count > -1)
      state->loop->info->trip_count = min_trip_count;
   state->loop->info->limiting_terminator = limiting_terminator;
}

/* Checks if we should force the loop to be unrolled regardless of size
 * due to array access heuristics.
 */
static bool
force_unroll_array_access(loop_info_state *state, nir_shader *ns,
                          nir_deref_var *variable)
{
   nir_deref *tail = &variable->deref;

   while (tail->child != NULL) {
      tail = tail->child;

      if (tail->deref_type == nir_deref_type_array) {

         nir_deref_array *deref_array = nir_deref_as_array(tail);
         if (deref_array->deref_array_type != nir_deref_array_type_indirect)
            continue;

         nir_loop_variable *array_index =
            get_loop_var(deref_array->indirect.ssa, state);

         if (array_index->type != basic_induction)
            continue;

         /* If an array is indexed by a loop induction variable, and the
          * array size is exactly the number of loop iterations, this is
          * probably a simple for-loop trying to access each element in
          * turn; the application may expect it to be unrolled.
          */
         if (glsl_get_length(variable->deref.type) ==
             state->loop->info->trip_count) {
            state->loop->info->force_unroll = true;
            return state->loop->info->force_unroll;
         }

         if (variable->var->data.mode & state->indirect_mask) {
            state->loop->info->force_unroll = true;
            return state->loop->info->force_unroll;
         }
      }
   }

   return false;
}

static bool
force_unroll_heuristics(loop_info_state *state, nir_shader *ns,
                        nir_block *block)
{
   nir_foreach_instr(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      /* Check for arrays variably-indexed by a loop induction variable.
       * Unrolling the loop may convert that access into constant-indexing.
       */
      if (intrin->intrinsic == nir_intrinsic_load_var ||
          intrin->intrinsic == nir_intrinsic_store_var ||
          intrin->intrinsic == nir_intrinsic_copy_var) {
         unsigned num_vars =
            nir_intrinsic_infos[intrin->intrinsic].num_variables;
         for (unsigned i = 0; i < num_vars; i++) {
            if (force_unroll_array_access(state, ns, intrin->variables[i]))
               return true;
         }
      }
   }

   return false;
}

static void
get_loop_info(loop_info_state *state, nir_function_impl *impl)
{
   /* Initialize all variables to "outside_loop". This also marks defs
    * invariant and constant if they are nir_instr_type_load_consts
    */
   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block)
         nir_foreach_ssa_def(instr, initialize_ssa_def, state);
   }

   /* Add all entries in the outermost part of the loop to the processing list
    * Mark the entries in conditionals or in nested loops accordingly
    */
   foreach_list_typed_safe(nir_cf_node, node, node, &state->loop->body) {
      switch (node->type) {

      case nir_cf_node_block:
         init_loop_block(nir_cf_node_as_block(node), state, false);
         break;

      case nir_cf_node_if:
         nir_foreach_block_in_cf_node(block, node)
            init_loop_block(block, state, true);
         break;

      case nir_cf_node_loop:
         nir_foreach_block_in_cf_node(block, node) {
            init_loop_block(block, state, true);
         }
         break;

      case nir_cf_node_function:
         break;
      }
   }

   /* Induction analysis needs invariance information so get that first */
   compute_invariance_information(state);

   /* We have invariance information so try to find induction variables */
   if (!compute_induction_information(state))
      return;

   /* Try to find all simple terminators of the loop. If we can't find any,
    * or we find possible terminators that have side effects then bail.
    */
   if (!find_loop_terminators(state)) {
      list_for_each_entry_safe(nir_loop_terminator, terminator,
                               &state->loop->info->loop_terminator_list,
                               loop_terminator_link) {
         list_del(&terminator->loop_terminator_link);
         ralloc_free(terminator);
      }
      return;
   }

   /* Run through each of the terminators and try to compute a trip-count */
   find_trip_count(state);

   nir_shader *ns = impl->function->shader;
   foreach_list_typed_safe(nir_cf_node, node, node, &state->loop->body) {
      if (node->type == nir_cf_node_block) {
         if (force_unroll_heuristics(state, ns, nir_cf_node_as_block(node)))
            break;
      } else {
         nir_foreach_block_in_cf_node(block, node) {
            if (force_unroll_heuristics(state, ns, block))
               break;
         }
      }
   }
}

static loop_info_state *
initialize_loop_info_state(nir_loop *loop, void *mem_ctx,
                           nir_function_impl *impl)
{
   loop_info_state *state = rzalloc(mem_ctx, loop_info_state);
   state->loop_vars = rzalloc_array(mem_ctx, nir_loop_variable,
                                    impl->ssa_alloc);
   state->loop = loop;

   list_inithead(&state->process_list);

   if (loop->info)
     ralloc_free(loop->info);

   loop->info = rzalloc(loop, nir_loop_info);

   list_inithead(&loop->info->loop_terminator_list);

   return state;
}

static void
process_loops(nir_cf_node *cf_node, nir_variable_mode indirect_mask)
{
   switch (cf_node->type) {
   case nir_cf_node_block:
      return;
   case nir_cf_node_if: {
      nir_if *if_stmt = nir_cf_node_as_if(cf_node);
      foreach_list_typed(nir_cf_node, nested_node, node, &if_stmt->then_list)
         process_loops(nested_node, indirect_mask);
      foreach_list_typed(nir_cf_node, nested_node, node, &if_stmt->else_list)
         process_loops(nested_node, indirect_mask);
      return;
   }
   case nir_cf_node_loop: {
      nir_loop *loop = nir_cf_node_as_loop(cf_node);
      foreach_list_typed(nir_cf_node, nested_node, node, &loop->body)
         process_loops(nested_node, indirect_mask);
      break;
   }
   default:
      unreachable("unknown cf node type");
   }

   nir_loop *loop = nir_cf_node_as_loop(cf_node);
   nir_function_impl *impl = nir_cf_node_get_function(cf_node);
   void *mem_ctx = ralloc_context(NULL);

   loop_info_state *state = initialize_loop_info_state(loop, mem_ctx, impl);
   state->indirect_mask = indirect_mask;

   get_loop_info(state, impl);

   ralloc_free(mem_ctx);
}

void
nir_loop_analyze_impl(nir_function_impl *impl,
                      nir_variable_mode indirect_mask)
{
   nir_index_ssa_defs(impl);
   foreach_list_typed(nir_cf_node, node, node, &impl->body)
      process_loops(node, indirect_mask);
}
