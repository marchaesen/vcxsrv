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
   void *mem_ctx;
   nir_function_impl *impl;
   bool progress;
};

static bool
constant_fold_alu_instr(nir_alu_instr *instr, void *mem_ctx)
{
   nir_const_value src[4];

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
          !nir_alu_type_get_type_size(nir_op_infos[instr->op].input_sizes[i])) {
         bit_size = instr->src[i].src.ssa->bit_size;
      }

      nir_instr *src_instr = instr->src[i].src.ssa->parent_instr;

      if (src_instr->type != nir_instr_type_load_const)
         return false;
      nir_load_const_instr* load_const = nir_instr_as_load_const(src_instr);

      for (unsigned j = 0; j < nir_ssa_alu_instr_src_components(instr, i);
           j++) {
         if (load_const->def.bit_size == 64)
            src[i].u64[j] = load_const->value.u64[instr->src[i].swizzle[j]];
         else
            src[i].u32[j] = load_const->value.u32[instr->src[i].swizzle[j]];
      }

      /* We shouldn't have any source modifiers in the optimization loop. */
      assert(!instr->src[i].abs && !instr->src[i].negate);
   }

   if (bit_size == 0)
      bit_size = 32;

   /* We shouldn't have any saturate modifiers in the optimization loop. */
   assert(!instr->dest.saturate);

   nir_const_value dest =
      nir_eval_const_opcode(instr->op, instr->dest.dest.ssa.num_components,
                            bit_size, src);

   nir_load_const_instr *new_instr =
      nir_load_const_instr_create(mem_ctx,
                                  instr->dest.dest.ssa.num_components,
                                  instr->dest.dest.ssa.bit_size);

   new_instr->value = dest;

   nir_instr_insert_before(&instr->instr, &new_instr->instr);

   nir_ssa_def_rewrite_uses(&instr->dest.dest.ssa,
                            nir_src_for_ssa(&new_instr->def));

   nir_instr_remove(&instr->instr);
   ralloc_free(instr);

   return true;
}

static bool
constant_fold_deref(nir_instr *instr, nir_deref_var *deref)
{
   bool progress = false;

   for (nir_deref *tail = deref->deref.child; tail; tail = tail->child) {
      if (tail->deref_type != nir_deref_type_array)
         continue;

      nir_deref_array *arr = nir_deref_as_array(tail);

      if (arr->deref_array_type == nir_deref_array_type_indirect &&
          arr->indirect.is_ssa &&
          arr->indirect.ssa->parent_instr->type == nir_instr_type_load_const) {
         nir_load_const_instr *indirect =
            nir_instr_as_load_const(arr->indirect.ssa->parent_instr);

         arr->base_offset += indirect->value.u32[0];

         /* Clear out the source */
         nir_instr_rewrite_src(instr, &arr->indirect, nir_src_for_ssa(NULL));

         arr->deref_array_type = nir_deref_array_type_direct;

         progress = true;
      }
   }

   return progress;
}

static bool
constant_fold_intrinsic_instr(nir_intrinsic_instr *instr)
{
   bool progress = false;

   unsigned num_vars = nir_intrinsic_infos[instr->intrinsic].num_variables;
   for (unsigned i = 0; i < num_vars; i++) {
      progress |= constant_fold_deref(&instr->instr, instr->variables[i]);
   }

   return progress;
}

static bool
constant_fold_tex_instr(nir_tex_instr *instr)
{
   bool progress = false;

   if (instr->texture)
      progress |= constant_fold_deref(&instr->instr, instr->texture);

   if (instr->sampler)
      progress |= constant_fold_deref(&instr->instr, instr->sampler);

   return progress;
}

static bool
constant_fold_block(nir_block *block, void *mem_ctx)
{
   bool progress = false;

   nir_foreach_instr_safe(instr, block) {
      switch (instr->type) {
      case nir_instr_type_alu:
         progress |= constant_fold_alu_instr(nir_instr_as_alu(instr), mem_ctx);
         break;
      case nir_instr_type_intrinsic:
         progress |=
            constant_fold_intrinsic_instr(nir_instr_as_intrinsic(instr));
         break;
      case nir_instr_type_tex:
         progress |= constant_fold_tex_instr(nir_instr_as_tex(instr));
         break;
      default:
         /* Don't know how to constant fold */
         break;
      }
   }

   return progress;
}

static bool
nir_opt_constant_folding_impl(nir_function_impl *impl)
{
   void *mem_ctx = ralloc_parent(impl);
   bool progress = false;

   nir_foreach_block(block, impl) {
      progress |= constant_fold_block(block, mem_ctx);
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);

   return progress;
}

bool
nir_opt_constant_folding(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= nir_opt_constant_folding_impl(function->impl);
   }

   return progress;
}
