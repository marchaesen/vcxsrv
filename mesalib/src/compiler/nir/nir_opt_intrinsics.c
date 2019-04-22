/*
 * Copyright © 2017 Intel Corporation
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

/**
 * \file nir_opt_intrinsics.c
 */

static bool
opt_intrinsics_impl(nir_function_impl *impl,
                    const struct nir_shader_compiler_options *options)
{
   nir_builder b;
   nir_builder_init(&b, impl);
   bool progress = false;

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         nir_ssa_def *replacement = NULL;
         b.cursor = nir_before_instr(instr);

         switch (intrin->intrinsic) {
         case nir_intrinsic_vote_any:
         case nir_intrinsic_vote_all:
            if (nir_src_is_const(intrin->src[0]))
               replacement = nir_ssa_for_src(&b, intrin->src[0], 1);
            break;
         case nir_intrinsic_vote_feq:
         case nir_intrinsic_vote_ieq:
            if (nir_src_is_const(intrin->src[0]))
               replacement = nir_imm_true(&b);
            break;
         case nir_intrinsic_load_sample_mask_in:
            /* Transform:
             *   gl_SampleMaskIn == 0 ---> gl_HelperInvocation
             *   gl_SampleMaskIn != 0 ---> !gl_HelperInvocation
             */
            if (!options->optimize_sample_mask_in)
               continue;

            nir_foreach_use_safe(use_src, &intrin->dest.ssa) {
               if (use_src->parent_instr->type == nir_instr_type_alu) {
                  nir_alu_instr *alu = nir_instr_as_alu(use_src->parent_instr);

                  if (alu->op == nir_op_ieq ||
                      alu->op == nir_op_ine) {
                     /* Check for 0 in either operand. */
                     nir_const_value *const_val =
                         nir_src_as_const_value(alu->src[0].src);
                     if (!const_val)
                        const_val = nir_src_as_const_value(alu->src[1].src);
                     if (!const_val || const_val->i32 != 0)
                        continue;

                     nir_ssa_def *new_expr = nir_load_helper_invocation(&b, 1);

                     if (alu->op == nir_op_ine)
                        new_expr = nir_inot(&b, new_expr);

                     nir_ssa_def_rewrite_uses(&alu->dest.dest.ssa,
                                              nir_src_for_ssa(new_expr));
                     nir_instr_remove(&alu->instr);
                     continue;
                  }
               }
            }
            continue;
         default:
            break;
         }

         if (!replacement)
            continue;

         nir_ssa_def_rewrite_uses(&intrin->dest.ssa,
                                  nir_src_for_ssa(replacement));
         nir_instr_remove(instr);
         progress = true;
      }
   }

   return progress;
}

bool
nir_opt_intrinsics(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;

      if (opt_intrinsics_impl(function->impl, shader->options)) {
         progress = true;
         nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                               nir_metadata_dominance);
      }
   }

   return progress;
}
