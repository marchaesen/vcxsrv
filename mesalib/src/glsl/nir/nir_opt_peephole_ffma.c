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

/*
 * Implements a small peephole optimization that looks for a multiply that
 * is only ever used in an add and replaces both with an fma.
 */

struct peephole_ffma_state {
   void *mem_ctx;
   nir_function_impl *impl;
   bool progress;
};

static inline bool
are_all_uses_fadd(nir_ssa_def *def)
{
   if (!list_empty(&def->if_uses))
      return false;

   nir_foreach_use(def, use_src) {
      nir_instr *use_instr = use_src->parent_instr;

      if (use_instr->type != nir_instr_type_alu)
         return false;

      nir_alu_instr *use_alu = nir_instr_as_alu(use_instr);
      switch (use_alu->op) {
      case nir_op_fadd:
         break; /* This one's ok */

      case nir_op_imov:
      case nir_op_fmov:
      case nir_op_fneg:
      case nir_op_fabs:
         assert(use_alu->dest.dest.is_ssa);
         if (!are_all_uses_fadd(&use_alu->dest.dest.ssa))
            return false;
         break;

      default:
         return false;
      }
   }

   return true;
}

static nir_alu_instr *
get_mul_for_src(nir_alu_src *src, uint8_t swizzle[4], bool *negate, bool *abs)
{
   assert(src->src.is_ssa && !src->abs && !src->negate);

   nir_instr *instr = src->src.ssa->parent_instr;
   if (instr->type != nir_instr_type_alu)
      return NULL;

   nir_alu_instr *alu = nir_instr_as_alu(instr);
   switch (alu->op) {
   case nir_op_imov:
   case nir_op_fmov:
      alu = get_mul_for_src(&alu->src[0], swizzle, negate, abs);
      break;

   case nir_op_fneg:
      alu = get_mul_for_src(&alu->src[0], swizzle, negate, abs);
      *negate = !*negate;
      break;

   case nir_op_fabs:
      alu = get_mul_for_src(&alu->src[0], swizzle, negate, abs);
      *negate = false;
      *abs = true;
      break;

   case nir_op_fmul:
      /* Only absorb a fmul into a ffma if the fmul is is only used in fadd
       * operations.  This prevents us from being too aggressive with our
       * fusing which can actually lead to more instructions.
       */
      if (!are_all_uses_fadd(&alu->dest.dest.ssa))
         return NULL;
      break;

   default:
      return NULL;
   }

   if (!alu)
      return NULL;

   for (unsigned i = 0; i < 4; i++) {
      if (!(alu->dest.write_mask & (1 << i)))
         break;

      swizzle[i] = swizzle[src->swizzle[i]];
   }

   return alu;
}

static bool
nir_opt_peephole_ffma_block(nir_block *block, void *void_state)
{
   struct peephole_ffma_state *state = void_state;

   nir_foreach_instr_safe(block, instr) {
      if (instr->type != nir_instr_type_alu)
         continue;

      nir_alu_instr *add = nir_instr_as_alu(instr);
      if (add->op != nir_op_fadd)
         continue;

      /* TODO: Maybe bail if this expression is considered "precise"? */

      assert(add->src[0].src.is_ssa && add->src[1].src.is_ssa);

      /* This, is the case a + a.  We would rather handle this with an
       * algebraic reduction than fuse it.  Also, we want to only fuse
       * things where the multiply is used only once and, in this case,
       * it would be used twice by the same instruction.
       */
      if (add->src[0].src.ssa == add->src[1].src.ssa)
         continue;

      nir_alu_instr *mul;
      uint8_t add_mul_src, swizzle[4];
      bool negate, abs;
      for (add_mul_src = 0; add_mul_src < 2; add_mul_src++) {
         for (unsigned i = 0; i < 4; i++)
            swizzle[i] = i;

         negate = false;
         abs = false;

         mul = get_mul_for_src(&add->src[add_mul_src], swizzle, &negate, &abs);

         if (mul != NULL)
            break;
      }

      if (mul == NULL)
         continue;

      nir_ssa_def *mul_src[2];
      mul_src[0] = mul->src[0].src.ssa;
      mul_src[1] = mul->src[1].src.ssa;

      if (abs) {
         for (unsigned i = 0; i < 2; i++) {
            nir_alu_instr *abs = nir_alu_instr_create(state->mem_ctx,
                                                      nir_op_fabs);
            abs->src[0].src = nir_src_for_ssa(mul_src[i]);
            nir_ssa_dest_init(&abs->instr, &abs->dest.dest,
                              mul_src[i]->num_components, NULL);
            abs->dest.write_mask = (1 << mul_src[i]->num_components) - 1;
            nir_instr_insert_before(&add->instr, &abs->instr);
            mul_src[i] = &abs->dest.dest.ssa;
         }
      }

      if (negate) {
         nir_alu_instr *neg = nir_alu_instr_create(state->mem_ctx,
                                                   nir_op_fneg);
         neg->src[0].src = nir_src_for_ssa(mul_src[0]);
         nir_ssa_dest_init(&neg->instr, &neg->dest.dest,
                           mul_src[0]->num_components, NULL);
         neg->dest.write_mask = (1 << mul_src[0]->num_components) - 1;
         nir_instr_insert_before(&add->instr, &neg->instr);
         mul_src[0] = &neg->dest.dest.ssa;
      }

      nir_alu_instr *ffma = nir_alu_instr_create(state->mem_ctx, nir_op_ffma);
      ffma->dest.saturate = add->dest.saturate;
      ffma->dest.write_mask = add->dest.write_mask;

      for (unsigned i = 0; i < 2; i++) {
         ffma->src[i].src = nir_src_for_ssa(mul_src[i]);
         for (unsigned j = 0; j < add->dest.dest.ssa.num_components; j++)
            ffma->src[i].swizzle[j] = mul->src[i].swizzle[swizzle[j]];
      }
      nir_alu_src_copy(&ffma->src[2], &add->src[1 - add_mul_src],
                       state->mem_ctx);

      assert(add->dest.dest.is_ssa);

      nir_ssa_dest_init(&ffma->instr, &ffma->dest.dest,
                        add->dest.dest.ssa.num_components,
                        add->dest.dest.ssa.name);
      nir_ssa_def_rewrite_uses(&add->dest.dest.ssa,
                               nir_src_for_ssa(&ffma->dest.dest.ssa),
                               state->mem_ctx);

      nir_instr_insert_before(&add->instr, &ffma->instr);
      assert(list_empty(&add->dest.dest.ssa.uses));
      nir_instr_remove(&add->instr);

      state->progress = true;
   }

   return true;
}

static bool
nir_opt_peephole_ffma_impl(nir_function_impl *impl)
{
   struct peephole_ffma_state state;

   state.mem_ctx = ralloc_parent(impl);
   state.impl = impl;
   state.progress = false;

   nir_foreach_block(impl, nir_opt_peephole_ffma_block, &state);

   if (state.progress)
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);

   return state.progress;
}

bool
nir_opt_peephole_ffma(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_overload(shader, overload) {
      if (overload->impl)
         progress |= nir_opt_peephole_ffma_impl(overload->impl);
   }

   return progress;
}
