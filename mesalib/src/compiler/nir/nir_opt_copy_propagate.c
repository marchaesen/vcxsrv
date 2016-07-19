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
 *    Connor Abbott (cwabbott0@gmail.com)
 *
 */

#include "nir.h"
#include <main/imports.h>

/**
 * SSA-based copy propagation
 */

static bool is_move(nir_alu_instr *instr)
{
   if (instr->op != nir_op_fmov &&
       instr->op != nir_op_imov)
      return false;

   if (instr->dest.saturate)
      return false;

   /* we handle modifiers in a separate pass */

   if (instr->src[0].abs || instr->src[0].negate)
      return false;

   if (!instr->src[0].src.is_ssa)
      return false;

   return true;

}

static bool is_vec(nir_alu_instr *instr)
{
   for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
      if (!instr->src[i].src.is_ssa)
         return false;

      /* we handle modifiers in a separate pass */
      if (instr->src[i].abs || instr->src[i].negate)
         return false;
   }

   return instr->op == nir_op_vec2 ||
          instr->op == nir_op_vec3 ||
          instr->op == nir_op_vec4;
}

static bool
is_swizzleless_move(nir_alu_instr *instr)
{
   if (is_move(instr)) {
      for (unsigned i = 0; i < 4; i++) {
         if (!((instr->dest.write_mask >> i) & 1))
            break;
         if (instr->src[0].swizzle[i] != i)
            return false;
      }
      return true;
   } else if (is_vec(instr)) {
      nir_ssa_def *def = NULL;
      for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
         if (instr->src[i].swizzle[0] != i)
            return false;

         if (def == NULL) {
            def = instr->src[i].src.ssa;
         } else if (instr->src[i].src.ssa != def) {
            return false;
         }
      }
      return true;
   } else {
      return false;
   }
}

static bool
copy_prop_src(nir_src *src, nir_instr *parent_instr, nir_if *parent_if)
{
   if (!src->is_ssa) {
      if (src->reg.indirect)
         return copy_prop_src(src->reg.indirect, parent_instr, parent_if);
      return false;
   }

   nir_instr *src_instr = src->ssa->parent_instr;
   if (src_instr->type != nir_instr_type_alu)
      return false;

   nir_alu_instr *alu_instr = nir_instr_as_alu(src_instr);
   if (!is_swizzleless_move(alu_instr))
      return false;

   /* Don't let copy propagation land us with a phi that has more
    * components in its source than it has in its destination.  That badly
    * messes up out-of-ssa.
    */
   if (parent_instr && parent_instr->type == nir_instr_type_phi) {
      nir_phi_instr *phi = nir_instr_as_phi(parent_instr);
      assert(phi->dest.is_ssa);
      if (phi->dest.ssa.num_components !=
          alu_instr->src[0].src.ssa->num_components)
         return false;
   }

   if (parent_instr) {
      nir_instr_rewrite_src(parent_instr, src,
                            nir_src_for_ssa(alu_instr->src[0].src.ssa));
   } else {
      assert(src == &parent_if->condition);
      nir_if_rewrite_condition(parent_if,
                               nir_src_for_ssa(alu_instr->src[0].src.ssa));
   }

   return true;
}

static bool
copy_prop_alu_src(nir_alu_instr *parent_alu_instr, unsigned index)
{
   nir_alu_src *src = &parent_alu_instr->src[index];
   if (!src->src.is_ssa) {
      if (src->src.reg.indirect)
         return copy_prop_src(src->src.reg.indirect, &parent_alu_instr->instr,
                              NULL);
      return false;
   }

   nir_instr *src_instr =  src->src.ssa->parent_instr;
   if (src_instr->type != nir_instr_type_alu)
      return false;

   nir_alu_instr *alu_instr = nir_instr_as_alu(src_instr);
   if (!is_move(alu_instr) && !is_vec(alu_instr))
      return false;

   nir_ssa_def *def;
   unsigned new_swizzle[4] = {0, 0, 0, 0};

   if (alu_instr->op == nir_op_fmov ||
       alu_instr->op == nir_op_imov) {
      for (unsigned i = 0; i < 4; i++)
         new_swizzle[i] = alu_instr->src[0].swizzle[src->swizzle[i]];
      def = alu_instr->src[0].src.ssa;
   } else {
      def = NULL;

      for (unsigned i = 0; i < 4; i++) {
         if (!nir_alu_instr_channel_used(parent_alu_instr, index, i))
            continue;

         nir_ssa_def *new_def = alu_instr->src[src->swizzle[i]].src.ssa;
         if (def == NULL)
            def = new_def;
         else {
            if (def != new_def)
               return false;
         }
         new_swizzle[i] = alu_instr->src[src->swizzle[i]].swizzle[0];
      }
   }

   for (unsigned i = 0; i < 4; i++)
      src->swizzle[i] = new_swizzle[i];

   nir_instr_rewrite_src(&parent_alu_instr->instr, &src->src,
                         nir_src_for_ssa(def));

   return true;
}

typedef struct {
   nir_instr *parent_instr;
   bool progress;
} copy_prop_state;

static bool
copy_prop_src_cb(nir_src *src, void *_state)
{
   copy_prop_state *state = (copy_prop_state *) _state;
   while (copy_prop_src(src, state->parent_instr, NULL))
      state->progress = true;

   return true;
}

static bool
copy_prop_instr(nir_instr *instr)
{
   if (instr->type == nir_instr_type_alu) {
      nir_alu_instr *alu_instr = nir_instr_as_alu(instr);
      bool progress = false;

      for (unsigned i = 0; i < nir_op_infos[alu_instr->op].num_inputs; i++)
         while (copy_prop_alu_src(alu_instr, i))
            progress = true;

      if (!alu_instr->dest.dest.is_ssa && alu_instr->dest.dest.reg.indirect)
         while (copy_prop_src(alu_instr->dest.dest.reg.indirect, instr, NULL))
            progress = true;

      return progress;
   }

   copy_prop_state state;
   state.parent_instr = instr;
   state.progress = false;
   nir_foreach_src(instr, copy_prop_src_cb, &state);

   return state.progress;
}

static bool
copy_prop_if(nir_if *if_stmt)
{
   return copy_prop_src(&if_stmt->condition, NULL, if_stmt);
}

static bool
nir_copy_prop_impl(nir_function_impl *impl)
{
   bool progress = false;

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (copy_prop_instr(instr))
            progress = true;
      }

      nir_if *if_stmt = nir_block_get_following_if(block);
      if (if_stmt && copy_prop_if(if_stmt))
         progress = true;
      }

   if (progress) {
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);
   }

   return progress;
}

bool
nir_copy_prop(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl && nir_copy_prop_impl(function->impl))
         progress = true;
   }

   return progress;
}
