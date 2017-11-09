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

/* Converts a uint32_t or uint64_t value to uint64_t or uvec4 */
static nir_ssa_def *
uint_to_ballot_type(nir_builder *b, nir_ssa_def *value,
                    unsigned num_components, unsigned bit_size)
{
   assert(value->num_components == 1);
   assert(value->bit_size == 32 || value->bit_size == 64);

   nir_ssa_def *zero = nir_imm_int(b, 0);
   if (num_components > 1) {
      /* SPIR-V uses a uvec4 for ballot values */
      assert(num_components == 4);
      assert(bit_size == 32);

      if (value->bit_size == 32) {
         return nir_vec4(b, value, zero, zero, zero);
      } else {
         assert(value->bit_size == 64);
         return nir_vec4(b, nir_unpack_64_2x32_split_x(b, value),
                            nir_unpack_64_2x32_split_y(b, value),
                            zero, zero);
      }
   } else {
      /* GLSL uses a uint64_t for ballot values */
      assert(num_components == 1);
      assert(bit_size == 64);

      if (value->bit_size == 32) {
         return nir_pack_64_2x32_split(b, value, zero);
      } else {
         assert(value->bit_size == 64);
         return value;
      }
   }
}

static nir_ssa_def *
lower_read_invocation_to_scalar(nir_builder *b, nir_intrinsic_instr *intrin)
{
   /* This is safe to call on scalar things but it would be silly */
   assert(intrin->dest.ssa.num_components > 1);

   nir_ssa_def *value = nir_ssa_for_src(b, intrin->src[0],
                                           intrin->num_components);
   nir_ssa_def *reads[4];

   for (unsigned i = 0; i < intrin->num_components; i++) {
      nir_intrinsic_instr *chan_intrin =
         nir_intrinsic_instr_create(b->shader, intrin->intrinsic);
      nir_ssa_dest_init(&chan_intrin->instr, &chan_intrin->dest,
                        1, intrin->dest.ssa.bit_size, NULL);
      chan_intrin->num_components = 1;

      /* value */
      chan_intrin->src[0] = nir_src_for_ssa(nir_channel(b, value, i));
      /* invocation */
      if (intrin->intrinsic == nir_intrinsic_read_invocation)
         nir_src_copy(&chan_intrin->src[1], &intrin->src[1], chan_intrin);

      nir_builder_instr_insert(b, &chan_intrin->instr);

      reads[i] = &chan_intrin->dest.ssa;
   }

   return nir_vec(b, reads, intrin->num_components);
}

static nir_ssa_def *
lower_subgroups_intrin(nir_builder *b, nir_intrinsic_instr *intrin,
                       const nir_lower_subgroups_options *options)
{
   switch (intrin->intrinsic) {
   case nir_intrinsic_vote_any:
   case nir_intrinsic_vote_all:
      if (options->lower_vote_trivial)
         return nir_ssa_for_src(b, intrin->src[0], 1);
      break;

   case nir_intrinsic_vote_eq:
      if (options->lower_vote_trivial)
         return nir_imm_int(b, NIR_TRUE);
      break;

   case nir_intrinsic_load_subgroup_size:
      if (options->subgroup_size)
         return nir_imm_int(b, options->subgroup_size);
      break;

   case nir_intrinsic_read_invocation:
   case nir_intrinsic_read_first_invocation:
      if (options->lower_to_scalar && intrin->num_components > 1)
         return lower_read_invocation_to_scalar(b, intrin);
      break;

   case nir_intrinsic_load_subgroup_eq_mask:
   case nir_intrinsic_load_subgroup_ge_mask:
   case nir_intrinsic_load_subgroup_gt_mask:
   case nir_intrinsic_load_subgroup_le_mask:
   case nir_intrinsic_load_subgroup_lt_mask: {
      if (!options->lower_subgroup_masks)
         return NULL;

      /* If either the result or the requested bit size is 64-bits then we
       * know that we have 64-bit types and using them will probably be more
       * efficient than messing around with 32-bit shifts and packing.
       */
      const unsigned bit_size = MAX2(options->ballot_bit_size,
                                     intrin->dest.ssa.bit_size);

      assert(options->subgroup_size <= 64);
      uint64_t group_mask = ~0ull >> (64 - options->subgroup_size);

      nir_ssa_def *count = nir_load_subgroup_invocation(b);
      nir_ssa_def *val;
      switch (intrin->intrinsic) {
      case nir_intrinsic_load_subgroup_eq_mask:
         val = nir_ishl(b, nir_imm_intN_t(b, 1ull, bit_size), count);
         break;
      case nir_intrinsic_load_subgroup_ge_mask:
         val = nir_iand(b, nir_ishl(b, nir_imm_intN_t(b, ~0ull, bit_size), count),
                           nir_imm_intN_t(b, group_mask, bit_size));
         break;
      case nir_intrinsic_load_subgroup_gt_mask:
         val = nir_iand(b, nir_ishl(b, nir_imm_intN_t(b, ~1ull, bit_size), count),
                           nir_imm_intN_t(b, group_mask, bit_size));
         break;
      case nir_intrinsic_load_subgroup_le_mask:
         val = nir_inot(b, nir_ishl(b, nir_imm_intN_t(b, ~1ull, bit_size), count));
         break;
      case nir_intrinsic_load_subgroup_lt_mask:
         val = nir_inot(b, nir_ishl(b, nir_imm_intN_t(b, ~0ull, bit_size), count));
         break;
      default:
         unreachable("you seriously can't tell this is unreachable?");
      }

      return uint_to_ballot_type(b, val,
                                 intrin->dest.ssa.num_components,
                                 intrin->dest.ssa.bit_size);
   }

   case nir_intrinsic_ballot: {
      if (intrin->dest.ssa.num_components == 1 &&
          intrin->dest.ssa.bit_size == options->ballot_bit_size)
         return NULL;

      nir_intrinsic_instr *ballot =
         nir_intrinsic_instr_create(b->shader, nir_intrinsic_ballot);
      ballot->num_components = 1;
      nir_ssa_dest_init(&ballot->instr, &ballot->dest,
                        1, options->ballot_bit_size, NULL);
      nir_src_copy(&ballot->src[0], &intrin->src[0], ballot);
      nir_builder_instr_insert(b, &ballot->instr);

      return uint_to_ballot_type(b, &ballot->dest.ssa,
                                 intrin->dest.ssa.num_components,
                                 intrin->dest.ssa.bit_size);
   }

   default:
      break;
   }

   return NULL;
}

static bool
lower_subgroups_impl(nir_function_impl *impl,
                     const nir_lower_subgroups_options *options)
{
   nir_builder b;
   nir_builder_init(&b, impl);
   bool progress = false;

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         b.cursor = nir_before_instr(instr);

         nir_ssa_def *lower = lower_subgroups_intrin(&b, intrin, options);
         if (!lower)
            continue;

         nir_ssa_def_rewrite_uses(&intrin->dest.ssa, nir_src_for_ssa(lower));
         nir_instr_remove(instr);
         progress = true;
      }
   }

   return progress;
}

bool
nir_lower_subgroups(nir_shader *shader,
                    const nir_lower_subgroups_options *options)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;

      if (lower_subgroups_impl(function->impl, options)) {
         progress = true;
         nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                               nir_metadata_dominance);
      }
   }

   return progress;
}
