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

#include "nir_constant_expressions.h"
#include <math.h>

/*
 * Implements SSA-based constant folding.
 */

struct constant_fold_state {
   nir_shader *shader;
   unsigned execution_mode;
   bool has_load_constant;
   bool has_indirect_load_const;
};

static bool
constant_fold_alu_instr(struct constant_fold_state *state, nir_alu_instr *instr)
{
   nir_const_value src[NIR_MAX_VEC_COMPONENTS][NIR_MAX_VEC_COMPONENTS];

   if (!instr->dest.dest.is_ssa)
      return false;

   /* In the case that any outputs/inputs have unsized types, then we need to
    * guess the bit-size. In this case, the validator ensures that all
    * bit-sizes match so we can just take the bit-size from first
    * output/input with an unsized type. If all the outputs/inputs are sized
    * then we don't need to guess the bit-size at all because the code we
    * generate for constant opcodes in this case already knows the sizes of
    * the types involved and does not need the provided bit-size for anything
    * (although it still requires to receive a valid bit-size).
    */
   unsigned bit_size = 0;
   if (!nir_alu_type_get_type_size(nir_op_infos[instr->op].output_type))
      bit_size = instr->dest.dest.ssa.bit_size;

   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
      if (!instr->src[i].src.is_ssa)
         return false;

      if (bit_size == 0 &&
          !nir_alu_type_get_type_size(nir_op_infos[instr->op].input_types[i]))
         bit_size = instr->src[i].src.ssa->bit_size;

      nir_instr *src_instr = instr->src[i].src.ssa->parent_instr;

      if (src_instr->type != nir_instr_type_load_const)
         return false;
      nir_load_const_instr* load_const = nir_instr_as_load_const(src_instr);

      for (unsigned j = 0; j < nir_ssa_alu_instr_src_components(instr, i);
           j++) {
         src[i][j] = load_const->value[instr->src[i].swizzle[j]];
      }

      /* We shouldn't have any source modifiers in the optimization loop. */
      assert(!instr->src[i].abs && !instr->src[i].negate);
   }

   if (bit_size == 0)
      bit_size = 32;

   /* We shouldn't have any saturate modifiers in the optimization loop. */
   assert(!instr->dest.saturate);

   nir_const_value dest[NIR_MAX_VEC_COMPONENTS];
   nir_const_value *srcs[NIR_MAX_VEC_COMPONENTS];
   memset(dest, 0, sizeof(dest));
   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; ++i)
      srcs[i] = src[i];
   nir_eval_const_opcode(instr->op, dest, instr->dest.dest.ssa.num_components,
                         bit_size, srcs, state->execution_mode);

   nir_load_const_instr *new_instr =
      nir_load_const_instr_create(state->shader,
                                  instr->dest.dest.ssa.num_components,
                                  instr->dest.dest.ssa.bit_size);

   memcpy(new_instr->value, dest, sizeof(*new_instr->value) * new_instr->def.num_components);

   nir_instr_insert_before(&instr->instr, &new_instr->instr);

   nir_ssa_def_rewrite_uses(&instr->dest.dest.ssa,
                            nir_src_for_ssa(&new_instr->def));

   nir_instr_remove(&instr->instr);
   ralloc_free(instr);

   return true;
}

static bool
constant_fold_intrinsic_instr(struct constant_fold_state *state, nir_intrinsic_instr *instr)
{
   bool progress = false;

   if ((instr->intrinsic == nir_intrinsic_demote_if ||
        instr->intrinsic == nir_intrinsic_discard_if) &&
       nir_src_is_const(instr->src[0])) {
      if (nir_src_as_bool(instr->src[0])) {
         nir_intrinsic_op op = instr->intrinsic == nir_intrinsic_discard_if ?
                               nir_intrinsic_discard :
                               nir_intrinsic_demote;
         nir_intrinsic_instr *new_instr = nir_intrinsic_instr_create(state->shader, op);
         nir_instr_insert_before(&instr->instr, &new_instr->instr);
         nir_instr_remove(&instr->instr);
         progress = true;
      } else {
         /* We're not discarding, just delete the instruction */
         nir_instr_remove(&instr->instr);
         progress = true;
      }
   } else if (instr->intrinsic == nir_intrinsic_load_constant) {
      state->has_load_constant = true;

      if (!nir_src_is_const(instr->src[0])) {
         state->has_indirect_load_const = true;
         return progress;
      }

      unsigned offset = nir_src_as_uint(instr->src[0]);
      unsigned base = nir_intrinsic_base(instr);
      unsigned range = nir_intrinsic_range(instr);
      assert(base + range <= state->shader->constant_data_size);

      nir_instr *new_instr = NULL;
      if (offset >= range) {
         nir_ssa_undef_instr *undef =
            nir_ssa_undef_instr_create(state->shader,
                                       instr->num_components,
                                       instr->dest.ssa.bit_size);

         nir_ssa_def_rewrite_uses(&instr->dest.ssa, nir_src_for_ssa(&undef->def));
         new_instr = &undef->instr;
      } else {
         nir_load_const_instr *load_const =
            nir_load_const_instr_create(state->shader,
                                        instr->num_components,
                                        instr->dest.ssa.bit_size);

         uint8_t *data = (uint8_t*)state->shader->constant_data + base;
         for (unsigned i = 0; i < instr->num_components; i++) {
            unsigned bytes = instr->dest.ssa.bit_size / 8;
            bytes = MIN2(bytes, range - offset);

            memcpy(&load_const->value[i].u64, data + offset, bytes);
            offset += bytes;
         }

         nir_ssa_def_rewrite_uses(&instr->dest.ssa, nir_src_for_ssa(&load_const->def));
         new_instr = &load_const->instr;
      }

      nir_instr_insert_before(&instr->instr, new_instr);
      nir_instr_remove(&instr->instr);
      progress = true;
   }

   return progress;
}

static bool
constant_fold_block(struct constant_fold_state *state, nir_block *block)
{
   bool progress = false;

   nir_foreach_instr_safe(instr, block) {
      switch (instr->type) {
      case nir_instr_type_alu:
         progress |= constant_fold_alu_instr(state, nir_instr_as_alu(instr));
         break;
      case nir_instr_type_intrinsic:
         progress |=
            constant_fold_intrinsic_instr(state, nir_instr_as_intrinsic(instr));
         break;
      default:
         /* Don't know how to constant fold */
         break;
      }
   }

   return progress;
}

static bool
nir_opt_constant_folding_impl(struct constant_fold_state *state, nir_function_impl *impl)
{
   bool progress = false;

   nir_foreach_block(block, impl) {
      progress |= constant_fold_block(state, block);
   }

   if (progress) {
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);
   } else {
#ifndef NDEBUG
      impl->valid_metadata &= ~nir_metadata_not_properly_reset;
#endif
   }

   return progress;
}

bool
nir_opt_constant_folding(nir_shader *shader)
{
   bool progress = false;
   struct constant_fold_state state;
   state.shader = shader;
   state.execution_mode = shader->info.float_controls_execution_mode;
   state.has_load_constant = false;
   state.has_indirect_load_const = false;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= nir_opt_constant_folding_impl(&state, function->impl);
   }

   /* This doesn't free the constant data if there are no constant loads because
    * the data might still be used but the loads have been lowered to load_ubo
    */
   if (state.has_load_constant && !state.has_indirect_load_const &&
       shader->constant_data_size) {
      ralloc_free(shader->constant_data);
      shader->constant_data = NULL;
      shader->constant_data_size = 0;
   }

   return progress;
}
