/*
 * Copyright (c) 2020 Lima Project
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
#include "nir_builder.h"
#include "lima_ir.h"

static bool
lima_nir_duplicate_intrinsic(nir_builder *b, nir_intrinsic_instr *itr,
                             nir_intrinsic_op op)
{
   nir_intrinsic_instr *last_dupl = NULL;
   nir_instr *last_parent_instr = NULL;

   nir_foreach_use_safe(use_src, &itr->dest.ssa) {
      nir_intrinsic_instr *dupl;

      if (last_parent_instr != use_src->parent_instr) {
         /* if ssa use, clone for the target block */
         b->cursor = nir_before_instr(use_src->parent_instr);
         dupl = nir_intrinsic_instr_create(b->shader, op);
         dupl->num_components = itr->num_components;
         memcpy(dupl->const_index, itr->const_index, sizeof(itr->const_index));
         dupl->src[0].is_ssa = itr->src[0].is_ssa;
         if (itr->src[0].is_ssa)
            dupl->src[0].ssa = itr->src[0].ssa;
         else
            dupl->src[0].reg = itr->src[0].reg;

         nir_ssa_dest_init(&dupl->instr, &dupl->dest,
               dupl->num_components, itr->dest.ssa.bit_size,
               itr->dest.ssa.name);

         dupl->instr.pass_flags = 1;
         nir_builder_instr_insert(b, &dupl->instr);
      }
      else {
         dupl = last_dupl;
      }

      nir_instr_rewrite_src(use_src->parent_instr, use_src, nir_src_for_ssa(&dupl->dest.ssa));
      last_parent_instr = use_src->parent_instr;
      last_dupl = dupl;
   }

   last_dupl = NULL;
   last_parent_instr = NULL;

   nir_foreach_if_use_safe(use_src, &itr->dest.ssa) {
      nir_intrinsic_instr *dupl;

      if (last_parent_instr != use_src->parent_instr) {
         /* if 'if use', clone where it is */
         b->cursor = nir_before_instr(&itr->instr);
         dupl = nir_intrinsic_instr_create(b->shader, op);
         dupl->num_components = itr->num_components;
         memcpy(dupl->const_index, itr->const_index, sizeof(itr->const_index));
         dupl->src[0].is_ssa = itr->src[0].is_ssa;
         if (itr->src[0].is_ssa)
            dupl->src[0].ssa = itr->src[0].ssa;
         else
            dupl->src[0].reg = itr->src[0].reg;

         nir_ssa_dest_init(&dupl->instr, &dupl->dest,
               dupl->num_components, itr->dest.ssa.bit_size,
               itr->dest.ssa.name);

         dupl->instr.pass_flags = 1;
         nir_builder_instr_insert(b, &dupl->instr);
      }
      else {
         dupl = last_dupl;
      }

      nir_if_rewrite_condition(use_src->parent_if, nir_src_for_ssa(&dupl->dest.ssa));
      last_parent_instr = use_src->parent_instr;
      last_dupl = dupl;
   }

   nir_instr_remove(&itr->instr);
   return true;
}

static void
lima_nir_duplicate_intrinsic_impl(nir_shader *shader, nir_function_impl *impl,
                                  nir_intrinsic_op op)
{
   nir_builder builder;
   nir_builder_init(&builder, impl);

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         instr->pass_flags = 0;
      }

      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *itr = nir_instr_as_intrinsic(instr);

         if (itr->intrinsic != op)
            continue;

         if (itr->instr.pass_flags)
            continue;

         if (!itr->dest.is_ssa)
            continue;

         lima_nir_duplicate_intrinsic(&builder, itr, op);
      }
   }

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
}

/* Duplicate load uniforms for every user.
 * Helps by utilizing the load uniform instruction slots that would
 * otherwise stay empty, and reduces register pressure. */
void
lima_nir_duplicate_load_uniforms(nir_shader *shader)
{
   nir_foreach_function(function, shader) {
      if (function->impl) {
         lima_nir_duplicate_intrinsic_impl(shader, function->impl, nir_intrinsic_load_uniform);
      }
   }
}

/* Duplicate load inputs for every user.
 * Helps by utilizing the load input instruction slots that would
 * otherwise stay empty, and reduces register pressure. */
void
lima_nir_duplicate_load_inputs(nir_shader *shader)
{
   nir_foreach_function(function, shader) {
      if (function->impl) {
         lima_nir_duplicate_intrinsic_impl(shader, function->impl, nir_intrinsic_load_input);
      }
   }
}
