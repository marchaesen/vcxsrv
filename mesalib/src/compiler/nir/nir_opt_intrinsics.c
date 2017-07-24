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
opt_intrinsics_impl(nir_function_impl *impl)
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
         case nir_intrinsic_vote_all: {
            nir_const_value *val = nir_src_as_const_value(intrin->src[0]);
            if (!val && !b.shader->options->lower_vote_trivial)
               continue;

            replacement = nir_ssa_for_src(&b, intrin->src[0], 1);
            break;
         }
         case nir_intrinsic_vote_eq: {
            nir_const_value *val = nir_src_as_const_value(intrin->src[0]);
            if (!val && !b.shader->options->lower_vote_trivial)
               continue;

            replacement = nir_imm_int(&b, NIR_TRUE);
            break;
         }
         case nir_intrinsic_ballot: {
            assert(b.shader->options->max_subgroup_size != 0);
            if (b.shader->options->max_subgroup_size > 32 ||
                intrin->dest.ssa.bit_size <= 32)
               continue;

            nir_intrinsic_instr *ballot =
               nir_intrinsic_instr_create(b.shader, nir_intrinsic_ballot);
            nir_ssa_dest_init(&ballot->instr, &ballot->dest, 1, 32, NULL);
            nir_src_copy(&ballot->src[0], &intrin->src[0], ballot);

            nir_builder_instr_insert(&b, &ballot->instr);

            replacement = nir_pack_64_2x32_split(&b,
                                                 &ballot->dest.ssa,
                                                 nir_imm_int(&b, 0));
            break;
         }
         case nir_intrinsic_load_subgroup_eq_mask:
         case nir_intrinsic_load_subgroup_ge_mask:
         case nir_intrinsic_load_subgroup_gt_mask:
         case nir_intrinsic_load_subgroup_le_mask:
         case nir_intrinsic_load_subgroup_lt_mask: {
            if (!b.shader->options->lower_subgroup_masks)
               break;

            nir_ssa_def *count = nir_load_subgroup_invocation(&b);

            switch (intrin->intrinsic) {
            case nir_intrinsic_load_subgroup_eq_mask:
               replacement = nir_ishl(&b, nir_imm_int64(&b, 1ull), count);
               break;
            case nir_intrinsic_load_subgroup_ge_mask:
               replacement = nir_ishl(&b, nir_imm_int64(&b, ~0ull), count);
               break;
            case nir_intrinsic_load_subgroup_gt_mask:
               replacement = nir_ishl(&b, nir_imm_int64(&b, ~1ull), count);
               break;
            case nir_intrinsic_load_subgroup_le_mask:
               replacement = nir_inot(&b, nir_ishl(&b, nir_imm_int64(&b, ~1ull), count));
               break;
            case nir_intrinsic_load_subgroup_lt_mask:
               replacement = nir_inot(&b, nir_ishl(&b, nir_imm_int64(&b, ~0ull), count));
               break;
            default:
               unreachable("you seriously can't tell this is unreachable?");
            }
            break;
         }
         default:
            break;
         }

         if (!replacement)
            continue;

         nir_ssa_def_rewrite_uses(&intrin->dest.ssa,
                                  nir_src_for_ssa(replacement));
         nir_instr_remove(instr);
         nir_metadata_preserve(impl, nir_metadata_block_index |
                                     nir_metadata_dominance);
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
      if (function->impl)
         progress |= opt_intrinsics_impl(function->impl);
   }

   return false;
}
