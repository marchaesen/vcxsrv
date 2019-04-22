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

   /* True if variable is in an if branch */
   bool in_if_branch;

   /* True if variable is in a nested loop */
   bool in_nested_loop;

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
   bool in_if_branch;
   bool in_nested_loop;
} init_loop_state;

static bool
init_loop_def(nir_ssa_def *def, void *void_init_loop_state)
{
   init_loop_state *loop_init_state = void_init_loop_state;
   nir_loop_variable *var = get_loop_var(def, loop_init_state->state);

   if (loop_init_state->in_nested_loop) {
      var->in_nested_loop = true;
   } else if (loop_init_state->in_if_branch) {
      var->in_if_branch = true;
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

/** Calculate an estimated cost in number of instructions
 *
 * We do this so that we don't unroll loops which will later get massively
 * inflated due to int64 or fp64 lowering.  The estimates provided here don't
 * have to be massively accurate; they just have to be good enough that loop
 * unrolling doesn't cause things to blow up too much.
 */
static unsigned
instr_cost(nir_instr *instr, const nir_shader_compiler_options *options)
{
   if (instr->type == nir_instr_type_intrinsic ||
       instr->type == nir_instr_type_tex)
      return 1;

   if (instr->type != nir_instr_type_alu)
      return 0;

   nir_alu_instr *alu = nir_instr_as_alu(instr);
   const nir_op_info *info = &nir_op_infos[alu->op];

   /* Assume everything 16 or 32-bit is cheap.
    *
    * There are no 64-bit ops that don't have a 64-bit thing as their
    * destination or first source.
    */
   if (nir_dest_bit_size(alu->dest.dest) < 64 &&
       nir_src_bit_size(alu->src[0].src) < 64)
      return 1;

   bool is_fp64 = nir_dest_bit_size(alu->dest.dest) == 64 &&
      nir_alu_type_get_base_type(info->output_type) == nir_type_float;
   for (unsigned i = 0; i < info->num_inputs; i++) {
      if (nir_src_bit_size(alu->src[i].src) == 64 &&
          nir_alu_type_get_base_type(info->input_types[i]) == nir_type_float)
         is_fp64 = true;
   }

   if (is_fp64) {
      /* If it's something lowered normally, it's expensive. */
      unsigned cost = 1;
      if (options->lower_doubles_options &
          nir_lower_doubles_op_to_options_mask(alu->op))
         cost *= 20;

      /* If it's full software, it's even more expensive */
      if (options->lower_doubles_options & nir_lower_fp64_full_software)
         cost *= 100;

      return cost;
   } else {
      if (options->lower_int64_options &
          nir_lower_int64_op_to_options_mask(alu->op)) {
         /* These require a doing the division algorithm. */
         if (alu->op == nir_op_idiv || alu->op == nir_op_udiv ||
             alu->op == nir_op_imod || alu->op == nir_op_umod ||
             alu->op == nir_op_irem)
            return 100;

         /* Other int64 lowering isn't usually all that expensive */
         return 5;
      }

      return 1;
   }
}

static bool
init_loop_block(nir_block *block, loop_info_state *state,
                bool in_if_branch, bool in_nested_loop,
                const nir_shader_compiler_options *options)
{
   init_loop_state init_state = {.in_if_branch = in_if_branch,
                                 .in_nested_loop = in_nested_loop,
                                 .state = state };

   nir_foreach_instr(instr, block) {
      state->loop->info->instr_cost += instr_cost(instr, options);
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
      assert(!var->in_if_branch && !var->in_nested_loop);

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
      assert(!var->in_if_branch && !var->in_nested_loop &&
             var->type != invariant);

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

         /* If one of the sources is in an if branch or nested loop then don't
          * attempt to go any further.
          */
         if (src_var->in_if_branch || src_var->in_nested_loop)
            break;

         /* Detect inductions variables that are incremented in both branches
          * of an unnested if rather than in a loop block.
          */
         if (is_var_phi(src_var)) {
            nir_phi_instr *src_phi =
               nir_instr_as_phi(src_var->def->parent_instr);

            nir_op alu_op = nir_num_opcodes; /* avoid uninitialized warning */
            nir_ssa_def *alu_srcs[2] = {0};
            nir_foreach_phi_src(src2, src_phi) {
               nir_loop_variable *src_var2 =
                  get_loop_var(src2->src.ssa, state);

               if (!src_var2->in_if_branch || !is_var_alu(src_var2))
                  break;

               nir_alu_instr *alu =
                  nir_instr_as_alu(src_var2->def->parent_instr);
               if (nir_op_infos[alu->op].num_inputs != 2)
                  break;

               if (alu->src[0].src.ssa == alu_srcs[0] &&
                   alu->src[1].src.ssa == alu_srcs[1] &&
                   alu->op == alu_op) {
                  /* Both branches perform the same calculation so we can use
                   * one of them to find the induction variable.
                   */
                  src_var = src_var2;
               } else {
                  alu_srcs[0] = alu->src[0].src.ssa;
                  alu_srcs[1] = alu->src[1].src.ssa;
                  alu_op = alu->op;
               }
            }
         }

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
         if (!nir_is_trivial_loop_if(nif, break_blk)) {
            state->loop->info->complex_loop = true;
            return false;
         }

         /* Continue if the if contained no jumps at all */
         if (!break_blk)
            continue;

         if (nif->condition.ssa->parent_instr->type == nir_instr_type_phi) {
            state->loop->info->complex_loop = true;
            return false;
         }

         nir_loop_terminator *terminator =
            rzalloc(state->loop->info, nir_loop_terminator);

         list_addtail(&terminator->loop_terminator_link,
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

/* This function looks for an array access within a loop that uses an
 * induction variable for the array index. If found it returns the size of the
 * array, otherwise 0 is returned. If we find an induction var we pass it back
 * to the caller via array_index_out.
 */
static unsigned
find_array_access_via_induction(loop_info_state *state,
                                nir_deref_instr *deref,
                                nir_loop_variable **array_index_out)
{
   for (nir_deref_instr *d = deref; d; d = nir_deref_instr_parent(d)) {
      if (d->deref_type != nir_deref_type_array)
         continue;

      assert(d->arr.index.is_ssa);
      nir_loop_variable *array_index = get_loop_var(d->arr.index.ssa, state);

      if (array_index->type != basic_induction)
         continue;

      if (array_index_out)
         *array_index_out = array_index;

      nir_deref_instr *parent = nir_deref_instr_parent(d);
      if (glsl_type_is_array_or_matrix(parent->type)) {
         return glsl_get_length(parent->type);
      } else {
         assert(glsl_type_is_vector(parent->type));
         return glsl_get_vector_elements(parent->type);
      }
   }

   return 0;
}

static bool
guess_loop_limit(loop_info_state *state, nir_const_value *limit_val,
                 nir_loop_variable *basic_ind)
{
   unsigned min_array_size = 0;

   nir_foreach_block_in_cf_node(block, &state->loop->cf_node) {
      nir_foreach_instr(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         /* Check for arrays variably-indexed by a loop induction variable. */
         if (intrin->intrinsic == nir_intrinsic_load_deref ||
             intrin->intrinsic == nir_intrinsic_store_deref ||
             intrin->intrinsic == nir_intrinsic_copy_deref) {

            nir_loop_variable *array_idx = NULL;
            unsigned array_size =
               find_array_access_via_induction(state,
                                               nir_src_as_deref(intrin->src[0]),
                                               &array_idx);
            if (basic_ind == array_idx &&
                (min_array_size == 0 || min_array_size > array_size)) {
               min_array_size = array_size;
            }

            if (intrin->intrinsic != nir_intrinsic_copy_deref)
               continue;

            array_size =
               find_array_access_via_induction(state,
                                               nir_src_as_deref(intrin->src[1]),
                                               &array_idx);
            if (basic_ind == array_idx &&
                (min_array_size == 0 || min_array_size > array_size)) {
               min_array_size = array_size;
            }
         }
      }
   }

   if (min_array_size) {
      limit_val->i32 = min_array_size;
      return true;
   }

   return false;
}

static bool
try_find_limit_of_alu(nir_loop_variable *limit, nir_const_value *limit_val,
                      nir_loop_terminator *terminator, loop_info_state *state)
{
   if(!is_var_alu(limit))
      return false;

   nir_alu_instr *limit_alu = nir_instr_as_alu(limit->def->parent_instr);

   if (limit_alu->op == nir_op_imin ||
       limit_alu->op == nir_op_fmin) {
      limit = get_loop_var(limit_alu->src[0].src.ssa, state);

      if (!is_var_constant(limit))
         limit = get_loop_var(limit_alu->src[1].src.ssa, state);

      if (!is_var_constant(limit))
         return false;

      *limit_val = nir_instr_as_load_const(limit->def->parent_instr)->value[0];

      terminator->exact_trip_count_unknown = true;

      return true;
   }

   return false;
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
      int32_t initial_val = initial->i32;
      int32_t span = limit->i32 - initial_val;
      iter = span / step->i32;
      break;
   }
   case nir_op_uge:
   case nir_op_ult: {
      uint32_t initial_val = initial->u32;
      uint32_t span = limit->u32 - initial_val;
      iter = span / step->u32;
      break;
   }
   case nir_op_fge:
   case nir_op_flt:
   case nir_op_feq:
   case nir_op_fne: {
      float initial_val = initial->f32;
      float span = limit->f32 - initial_val;
      iter = span / step->f32;
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

   nir_const_value iter_src = {0, };
   nir_op mul_op;
   nir_op add_op;
   switch (induction_base_type) {
   case nir_type_float:
      iter_src.f32 = (float) iter_int;
      mul_op = nir_op_fmul;
      add_op = nir_op_fadd;
      break;
   case nir_type_int:
   case nir_type_uint:
      iter_src.i32 = iter_int;
      mul_op = nir_op_imul;
      add_op = nir_op_iadd;
      break;
   default:
      unreachable("Unhandled induction variable base type!");
   }

   /* Multiple the iteration count we are testing by the number of times we
    * step the induction variable each iteration.
    */
   nir_const_value *mul_src[2] = { &iter_src, step };
   nir_const_value mul_result;
   nir_eval_const_opcode(mul_op, &mul_result, 1, bit_size, mul_src);

   /* Add the initial value to the accumulated induction variable total */
   nir_const_value *add_src[2] = { &mul_result, initial };
   nir_const_value add_result;
   nir_eval_const_opcode(add_op, &add_result, 1, bit_size, add_src);

   nir_const_value *src[2];
   src[limit_rhs ? 0 : 1] = &add_result;
   src[limit_rhs ? 1 : 0] = limit;

   /* Evaluate the loop exit condition */
   nir_const_value result;
   nir_eval_const_opcode(cond_op, &result, 1, bit_size, src);

   return invert_cond ? !result.b : result.b;
}

static int
calculate_iterations(nir_const_value *initial, nir_const_value *step,
                     nir_const_value *limit, nir_loop_variable *alu_def,
                     nir_alu_instr *cond_alu, nir_op alu_op, bool limit_rhs,
                     bool invert_cond)
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
      assert(nir_alu_type_get_base_type(nir_op_infos[alu_op].input_types[1]) == nir_type_int ||
             nir_alu_type_get_base_type(nir_op_infos[alu_op].input_types[1]) == nir_type_uint);
   } else {
      assert(nir_alu_type_get_base_type(nir_op_infos[alu_op].input_types[0]) ==
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

   int iter_int = get_iteration(alu_op, initial, step, limit);

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

      if (test_iterations(iter_bias, step, limit, alu_op, bit_size,
                          induction_base_type, initial,
                          limit_rhs, invert_cond)) {
         return iter_bias > 0 ? iter_bias - trip_offset : iter_bias;
      }
   }

   return -1;
}

static nir_op
inverse_comparison(nir_alu_instr *alu)
{
   switch (alu->op) {
   case nir_op_fge:
      return nir_op_flt;
   case nir_op_ige:
      return nir_op_ilt;
   case nir_op_uge:
      return nir_op_ult;
   case nir_op_flt:
      return nir_op_fge;
   case nir_op_ilt:
      return nir_op_ige;
   case nir_op_ult:
      return nir_op_uge;
   case nir_op_feq:
      return nir_op_fne;
   case nir_op_ieq:
      return nir_op_ine;
   case nir_op_fne:
      return nir_op_feq;
   case nir_op_ine:
      return nir_op_ieq;
   default:
      unreachable("Unsuported comparison!");
   }
}

static bool
is_supported_terminator_condition(nir_alu_instr *alu)
{
   return nir_alu_instr_is_comparison(alu) &&
          nir_op_infos[alu->op].num_inputs == 2;
}

static bool
get_induction_and_limit_vars(nir_alu_instr *alu, nir_loop_variable **ind,
                             nir_loop_variable **limit,
                             loop_info_state *state)
{
   bool limit_rhs = true;

   /* We assume that the limit is the "right" operand */
   *ind = get_loop_var(alu->src[0].src.ssa, state);
   *limit = get_loop_var(alu->src[1].src.ssa, state);

   if ((*ind)->type != basic_induction) {
      /* We had it the wrong way, flip things around */
      *ind = get_loop_var(alu->src[1].src.ssa, state);
      *limit = get_loop_var(alu->src[0].src.ssa, state);
      limit_rhs = false;
   }

   return limit_rhs;
}

static void
try_find_trip_count_vars_in_iand(nir_alu_instr **alu,
                                 nir_loop_variable **ind,
                                 nir_loop_variable **limit,
                                 bool *limit_rhs,
                                 loop_info_state *state)
{
   assert((*alu)->op == nir_op_ieq || (*alu)->op == nir_op_inot);

   nir_ssa_def *iand_def = (*alu)->src[0].src.ssa;

   if ((*alu)->op == nir_op_ieq) {
      nir_ssa_def *zero_def = (*alu)->src[1].src.ssa;

      if (iand_def->parent_instr->type != nir_instr_type_alu ||
          zero_def->parent_instr->type != nir_instr_type_load_const) {

         /* Maybe we had it the wrong way, flip things around */
         iand_def = (*alu)->src[1].src.ssa;
         zero_def = (*alu)->src[0].src.ssa;

         /* If we still didn't find what we need then return */
         if (zero_def->parent_instr->type != nir_instr_type_load_const)
            return;
      }

      /* If the loop is not breaking on (x && y) == 0 then return */
      nir_const_value *zero =
         nir_instr_as_load_const(zero_def->parent_instr)->value;
      if (zero[0].i32 != 0)
         return;
   }

   if (iand_def->parent_instr->type != nir_instr_type_alu)
      return;

   nir_alu_instr *iand = nir_instr_as_alu(iand_def->parent_instr);
   if (iand->op != nir_op_iand)
      return;

   /* Check if iand src is a terminator condition and try get induction var
    * and trip limit var.
    */
   nir_ssa_def *src = iand->src[0].src.ssa;
   if (src->parent_instr->type == nir_instr_type_alu) {
      *alu = nir_instr_as_alu(src->parent_instr);
      if (is_supported_terminator_condition(*alu))
         *limit_rhs = get_induction_and_limit_vars(*alu, ind, limit, state);
   }

   /* Try the other iand src if needed */
   if (*ind == NULL || (*ind && (*ind)->type != basic_induction) ||
       !is_var_constant(*limit)) {
      src = iand->src[1].src.ssa;
      if (src->parent_instr->type == nir_instr_type_alu) {
         nir_alu_instr *tmp_alu = nir_instr_as_alu(src->parent_instr);
         if (is_supported_terminator_condition(tmp_alu)) {
            *alu = tmp_alu;
            *limit_rhs = get_induction_and_limit_vars(*alu, ind, limit, state);
         }
      }
   }
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
   bool guessed_trip_count = false;
   nir_loop_terminator *limiting_terminator = NULL;
   int max_trip_count = -1;

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
      nir_op alu_op = alu->op;

      bool limit_rhs;
      nir_loop_variable *basic_ind = NULL;
      nir_loop_variable *limit;
      if (alu->op == nir_op_inot || alu->op == nir_op_ieq) {
         nir_alu_instr *new_alu = alu;
         try_find_trip_count_vars_in_iand(&new_alu, &basic_ind, &limit,
                                          &limit_rhs, state);

         /* The loop is exiting on (x && y) == 0 so we need to get the
          * inverse of x or y (i.e. which ever contained the induction var) in
          * order to compute the trip count.
          */
         if (basic_ind && basic_ind->type == basic_induction) {
            alu = new_alu;
            alu_op = inverse_comparison(alu);
            trip_count_known = false;
            terminator->exact_trip_count_unknown = true;
         }
      }

      if (!basic_ind) {
         if (!is_supported_terminator_condition(alu)) {
            trip_count_known = false;
            continue;
         }

         limit_rhs = get_induction_and_limit_vars(alu, &basic_ind, &limit,
                                                  state);
      }

      /* The comparison has to have a basic induction variable for us to be
       * able to find trip counts.
       */
      if (basic_ind->type != basic_induction) {
         trip_count_known = false;
         continue;
      }

      terminator->induction_rhs = !limit_rhs;

      /* Attempt to find a constant limit for the loop */
      nir_const_value limit_val;
      if (is_var_constant(limit)) {
         limit_val =
            nir_instr_as_load_const(limit->def->parent_instr)->value[0];
      } else {
         trip_count_known = false;

         if (!try_find_limit_of_alu(limit, &limit_val, terminator, state)) {
            /* Guess loop limit based on array access */
            if (!guess_loop_limit(state, &limit_val, basic_ind)) {
               continue;
            }

            guessed_trip_count = true;
         }
      }

      /* We have determined that we have the following constants:
       * (With the typical int i = 0; i < x; i++; as an example)
       *    - Upper limit.
       *    - Starting value
       *    - Step / iteration size
       * Thats all thats needed to calculate the trip-count
       */

      nir_const_value *initial_val =
         nir_instr_as_load_const(basic_ind->ind->def_outside_loop->
                                    def->parent_instr)->value;

      nir_const_value *step_val =
         nir_instr_as_load_const(basic_ind->ind->invariant->def->
                                    parent_instr)->value;

      int iterations = calculate_iterations(initial_val, step_val,
                                            &limit_val,
                                            basic_ind->ind->alu_def, alu,
                                            alu_op, limit_rhs,
                                            terminator->continue_from_then);

      /* Where we not able to calculate the iteration count */
      if (iterations == -1) {
         trip_count_known = false;
         guessed_trip_count = false;
         continue;
      }

      if (guessed_trip_count) {
         guessed_trip_count = false;
         if (state->loop->info->guessed_trip_count == 0 ||
             state->loop->info->guessed_trip_count > iterations)
            state->loop->info->guessed_trip_count = iterations;

         continue;
      }

      /* If this is the first run or we have found a smaller amount of
       * iterations than previously (we have identified a more limiting
       * terminator) set the trip count and limiting terminator.
       */
      if (max_trip_count == -1 || iterations < max_trip_count) {
         max_trip_count = iterations;
         limiting_terminator = terminator;
      }
   }

   state->loop->info->exact_trip_count_known = trip_count_known;
   if (max_trip_count > -1)
      state->loop->info->max_trip_count = max_trip_count;
   state->loop->info->limiting_terminator = limiting_terminator;
}

static bool
force_unroll_array_access(loop_info_state *state, nir_deref_instr *deref)
{
   unsigned array_size = find_array_access_via_induction(state, deref, NULL);
   if (array_size) {
      if (array_size == state->loop->info->max_trip_count)
         return true;

      if (deref->mode & state->indirect_mask)
         return true;
   }

   return false;
}

static bool
force_unroll_heuristics(loop_info_state *state, nir_block *block)
{
   nir_foreach_instr(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

      /* Check for arrays variably-indexed by a loop induction variable.
       * Unrolling the loop may convert that access into constant-indexing.
       */
      if (intrin->intrinsic == nir_intrinsic_load_deref ||
          intrin->intrinsic == nir_intrinsic_store_deref ||
          intrin->intrinsic == nir_intrinsic_copy_deref) {
         if (force_unroll_array_access(state,
                                       nir_src_as_deref(intrin->src[0])))
            return true;

         if (intrin->intrinsic == nir_intrinsic_copy_deref &&
             force_unroll_array_access(state,
                                       nir_src_as_deref(intrin->src[1])))
            return true;
      }
   }

   return false;
}

static void
get_loop_info(loop_info_state *state, nir_function_impl *impl)
{
   nir_shader *shader = impl->function->shader;
   const nir_shader_compiler_options *options = shader->options;

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
         init_loop_block(nir_cf_node_as_block(node), state,
                         false, false, options);
         break;

      case nir_cf_node_if:
         nir_foreach_block_in_cf_node(block, node)
            init_loop_block(block, state, true, false, options);
         break;

      case nir_cf_node_loop:
         nir_foreach_block_in_cf_node(block, node) {
            init_loop_block(block, state, false, true, options);
         }
         break;

      case nir_cf_node_function:
         break;
      }
   }

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

   /* Induction analysis needs invariance information so get that first */
   compute_invariance_information(state);

   /* We have invariance information so try to find induction variables */
   if (!compute_induction_information(state))
      return;

   /* Run through each of the terminators and try to compute a trip-count */
   find_trip_count(state);

   nir_foreach_block_in_cf_node(block, &state->loop->cf_node) {
      if (force_unroll_heuristics(state, block)) {
         state->loop->info->force_unroll = true;
         break;
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
