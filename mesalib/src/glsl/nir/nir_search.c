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

#include "nir_search.h"

struct match_state {
   unsigned variables_seen;
   nir_alu_src variables[NIR_SEARCH_MAX_VARIABLES];
};

static bool
match_expression(const nir_search_expression *expr, nir_alu_instr *instr,
                 unsigned num_components, const uint8_t *swizzle,
                 struct match_state *state);

static const uint8_t identity_swizzle[] = { 0, 1, 2, 3 };

static bool alu_instr_is_bool(nir_alu_instr *instr);

static bool
src_is_bool(nir_src src)
{
   if (!src.is_ssa)
      return false;
   if (src.ssa->parent_instr->type != nir_instr_type_alu)
      return false;
   return alu_instr_is_bool(nir_instr_as_alu(src.ssa->parent_instr));
}

static bool
alu_instr_is_bool(nir_alu_instr *instr)
{
   switch (instr->op) {
   case nir_op_iand:
   case nir_op_ior:
   case nir_op_ixor:
      return src_is_bool(instr->src[0].src) && src_is_bool(instr->src[1].src);
   case nir_op_inot:
      return src_is_bool(instr->src[0].src);
   default:
      return nir_op_infos[instr->op].output_type == nir_type_bool;
   }
}

static bool
match_value(const nir_search_value *value, nir_alu_instr *instr, unsigned src,
            unsigned num_components, const uint8_t *swizzle,
            struct match_state *state)
{
   uint8_t new_swizzle[4];

   /* If the source is an explicitly sized source, then we need to reset
    * both the number of components and the swizzle.
    */
   if (nir_op_infos[instr->op].input_sizes[src] != 0) {
      num_components = nir_op_infos[instr->op].input_sizes[src];
      swizzle = identity_swizzle;
   }

   for (int i = 0; i < num_components; ++i)
      new_swizzle[i] = instr->src[src].swizzle[swizzle[i]];

   switch (value->type) {
   case nir_search_value_expression:
      if (!instr->src[src].src.is_ssa)
         return false;

      if (instr->src[src].src.ssa->parent_instr->type != nir_instr_type_alu)
         return false;

      return match_expression(nir_search_value_as_expression(value),
                              nir_instr_as_alu(instr->src[src].src.ssa->parent_instr),
                              num_components, new_swizzle, state);

   case nir_search_value_variable: {
      nir_search_variable *var = nir_search_value_as_variable(value);
      assert(var->variable < NIR_SEARCH_MAX_VARIABLES);

      if (state->variables_seen & (1 << var->variable)) {
         if (!nir_srcs_equal(state->variables[var->variable].src,
                             instr->src[src].src))
            return false;

         assert(!instr->src[src].abs && !instr->src[src].negate);

         for (int i = 0; i < num_components; ++i) {
            if (state->variables[var->variable].swizzle[i] != new_swizzle[i])
               return false;
         }

         return true;
      } else {
         if (var->is_constant &&
             instr->src[src].src.ssa->parent_instr->type != nir_instr_type_load_const)
            return false;

         if (var->type != nir_type_invalid) {
            if (instr->src[src].src.ssa->parent_instr->type != nir_instr_type_alu)
               return false;

            nir_alu_instr *src_alu =
               nir_instr_as_alu(instr->src[src].src.ssa->parent_instr);

            if (nir_op_infos[src_alu->op].output_type != var->type &&
                !(var->type == nir_type_bool && alu_instr_is_bool(src_alu)))
               return false;
         }

         state->variables_seen |= (1 << var->variable);
         state->variables[var->variable].src = instr->src[src].src;
         state->variables[var->variable].abs = false;
         state->variables[var->variable].negate = false;

         for (int i = 0; i < 4; ++i) {
            if (i < num_components)
               state->variables[var->variable].swizzle[i] = new_swizzle[i];
            else
               state->variables[var->variable].swizzle[i] = 0;
         }

         return true;
      }
   }

   case nir_search_value_constant: {
      nir_search_constant *const_val = nir_search_value_as_constant(value);

      if (!instr->src[src].src.is_ssa)
         return false;

      if (instr->src[src].src.ssa->parent_instr->type != nir_instr_type_load_const)
         return false;

      nir_load_const_instr *load =
         nir_instr_as_load_const(instr->src[src].src.ssa->parent_instr);

      switch (nir_op_infos[instr->op].input_types[src]) {
      case nir_type_float:
         for (unsigned i = 0; i < num_components; ++i) {
            if (load->value.f[new_swizzle[i]] != const_val->data.f)
               return false;
         }
         return true;
      case nir_type_int:
      case nir_type_unsigned:
      case nir_type_bool:
         for (unsigned i = 0; i < num_components; ++i) {
            if (load->value.i[new_swizzle[i]] != const_val->data.i)
               return false;
         }
         return true;
      default:
         unreachable("Invalid alu source type");
      }
   }

   default:
      unreachable("Invalid search value type");
   }
}

static bool
match_expression(const nir_search_expression *expr, nir_alu_instr *instr,
                 unsigned num_components, const uint8_t *swizzle,
                 struct match_state *state)
{
   if (instr->op != expr->opcode)
      return false;

   assert(!instr->dest.saturate);
   assert(nir_op_infos[instr->op].num_inputs > 0);

   /* If we have an explicitly sized destination, we can only handle the
    * identity swizzle.  While dot(vec3(a, b, c).zxy) is a valid
    * expression, we don't have the information right now to propagate that
    * swizzle through.  We can only properly propagate swizzles if the
    * instruction is vectorized.
    */
   if (nir_op_infos[instr->op].output_size != 0) {
      for (unsigned i = 0; i < num_components; i++) {
         if (swizzle[i] != i)
            return false;
      }
   }

   /* Stash off the current variables_seen bitmask.  This way we can
    * restore it prior to matching in the commutative case below.
    */
   unsigned variables_seen_stash = state->variables_seen;

   bool matched = true;
   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
      if (!match_value(expr->srcs[i], instr, i, num_components,
                       swizzle, state)) {
         matched = false;
         break;
      }
   }

   if (matched)
      return true;

   if (nir_op_infos[instr->op].algebraic_properties & NIR_OP_IS_COMMUTATIVE) {
      assert(nir_op_infos[instr->op].num_inputs == 2);

      /* Restore the variables_seen bitmask.  If we don't do this, then we
       * could end up with an erroneous failure due to variables found in the
       * first match attempt above not matching those in the second.
       */
      state->variables_seen = variables_seen_stash;

      if (!match_value(expr->srcs[0], instr, 1, num_components,
                       swizzle, state))
         return false;

      return match_value(expr->srcs[1], instr, 0, num_components,
                         swizzle, state);
   } else {
      return false;
   }
}

static nir_alu_src
construct_value(const nir_search_value *value, nir_alu_type type,
                unsigned num_components, struct match_state *state,
                nir_instr *instr, void *mem_ctx)
{
   switch (value->type) {
   case nir_search_value_expression: {
      const nir_search_expression *expr = nir_search_value_as_expression(value);

      if (nir_op_infos[expr->opcode].output_size != 0)
         num_components = nir_op_infos[expr->opcode].output_size;

      nir_alu_instr *alu = nir_alu_instr_create(mem_ctx, expr->opcode);
      nir_ssa_dest_init(&alu->instr, &alu->dest.dest, num_components, NULL);
      alu->dest.write_mask = (1 << num_components) - 1;
      alu->dest.saturate = false;

      for (unsigned i = 0; i < nir_op_infos[expr->opcode].num_inputs; i++) {
         /* If the source is an explicitly sized source, then we need to reset
          * the number of components to match.
          */
         if (nir_op_infos[alu->op].input_sizes[i] != 0)
            num_components = nir_op_infos[alu->op].input_sizes[i];

         alu->src[i] = construct_value(expr->srcs[i],
                                       nir_op_infos[alu->op].input_types[i],
                                       num_components,
                                       state, instr, mem_ctx);
      }

      nir_instr_insert_before(instr, &alu->instr);

      nir_alu_src val;
      val.src = nir_src_for_ssa(&alu->dest.dest.ssa);
      val.negate = false;
      val.abs = false,
      memcpy(val.swizzle, identity_swizzle, sizeof val.swizzle);

      return val;
   }

   case nir_search_value_variable: {
      const nir_search_variable *var = nir_search_value_as_variable(value);
      assert(state->variables_seen & (1 << var->variable));

      nir_alu_src val = { NIR_SRC_INIT };
      nir_alu_src_copy(&val, &state->variables[var->variable], mem_ctx);

      assert(!var->is_constant);

      return val;
   }

   case nir_search_value_constant: {
      const nir_search_constant *c = nir_search_value_as_constant(value);
      nir_load_const_instr *load = nir_load_const_instr_create(mem_ctx, 1);

      switch (type) {
      case nir_type_float:
         load->def.name = ralloc_asprintf(mem_ctx, "%f", c->data.f);
         load->value.f[0] = c->data.f;
         break;
      case nir_type_int:
         load->def.name = ralloc_asprintf(mem_ctx, "%d", c->data.i);
         load->value.i[0] = c->data.i;
         break;
      case nir_type_unsigned:
      case nir_type_bool:
         load->value.u[0] = c->data.u;
         break;
      default:
         unreachable("Invalid alu source type");
      }

      nir_instr_insert_before(instr, &load->instr);

      nir_alu_src val;
      val.src = nir_src_for_ssa(&load->def);
      val.negate = false;
      val.abs = false,
      memset(val.swizzle, 0, sizeof val.swizzle);

      return val;
   }

   default:
      unreachable("Invalid search value type");
   }
}

nir_alu_instr *
nir_replace_instr(nir_alu_instr *instr, const nir_search_expression *search,
                  const nir_search_value *replace, void *mem_ctx)
{
   uint8_t swizzle[4] = { 0, 0, 0, 0 };

   for (unsigned i = 0; i < instr->dest.dest.ssa.num_components; ++i)
      swizzle[i] = i;

   assert(instr->dest.dest.is_ssa);

   struct match_state state;
   state.variables_seen = 0;

   if (!match_expression(search, instr, instr->dest.dest.ssa.num_components,
                         swizzle, &state))
      return NULL;

   /* Inserting a mov may be unnecessary.  However, it's much easier to
    * simply let copy propagation clean this up than to try to go through
    * and rewrite swizzles ourselves.
    */
   nir_alu_instr *mov = nir_alu_instr_create(mem_ctx, nir_op_imov);
   mov->dest.write_mask = instr->dest.write_mask;
   nir_ssa_dest_init(&mov->instr, &mov->dest.dest,
                     instr->dest.dest.ssa.num_components, NULL);

   mov->src[0] = construct_value(replace, nir_op_infos[instr->op].output_type,
                                 instr->dest.dest.ssa.num_components, &state,
                                 &instr->instr, mem_ctx);
   nir_instr_insert_before(&instr->instr, &mov->instr);

   nir_ssa_def_rewrite_uses(&instr->dest.dest.ssa,
                            nir_src_for_ssa(&mov->dest.dest.ssa), mem_ctx);

   /* We know this one has no more uses because we just rewrote them all,
    * so we can remove it.  The rest of the matched expression, however, we
    * don't know so much about.  We'll just let dead code clean them up.
    */
   nir_instr_remove(&instr->instr);

   return mov;
}
