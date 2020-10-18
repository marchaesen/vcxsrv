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

static nir_intrinsic_instr *
lower_subgroups_64bit_split_intrinsic(nir_builder *b, nir_intrinsic_instr *intrin,
                                      unsigned int component)
{
   nir_ssa_def *comp;
   if (component == 0)
      comp = nir_unpack_64_2x32_split_x(b, intrin->src[0].ssa);
   else
      comp = nir_unpack_64_2x32_split_y(b, intrin->src[0].ssa);

   nir_intrinsic_instr *intr = nir_intrinsic_instr_create(b->shader, intrin->intrinsic);
   nir_ssa_dest_init(&intr->instr, &intr->dest, 1, 32, NULL);
   intr->const_index[0] = intrin->const_index[0];
   intr->const_index[1] = intrin->const_index[1];
   intr->src[0] = nir_src_for_ssa(comp);
   if (nir_intrinsic_infos[intrin->intrinsic].num_srcs == 2)
      nir_src_copy(&intr->src[1], &intrin->src[1], intr);

   intr->num_components = 1;
   nir_builder_instr_insert(b, &intr->instr);
   return intr;
}

static nir_ssa_def *
lower_subgroup_op_to_32bit(nir_builder *b, nir_intrinsic_instr *intrin)
{
   assert(intrin->src[0].ssa->bit_size == 64);
   nir_intrinsic_instr *intr_x = lower_subgroups_64bit_split_intrinsic(b, intrin, 0);
   nir_intrinsic_instr *intr_y = lower_subgroups_64bit_split_intrinsic(b, intrin, 1);
   return nir_pack_64_2x32_split(b, &intr_x->dest.ssa, &intr_y->dest.ssa);
}

static nir_ssa_def *
ballot_type_to_uint(nir_builder *b, nir_ssa_def *value, unsigned bit_size)
{
   /* We only use this on uvec4 types */
   assert(value->num_components == 4 && value->bit_size == 32);

   if (bit_size == 32) {
      return nir_channel(b, value, 0);
   } else {
      assert(bit_size == 64);
      return nir_pack_64_2x32_split(b, nir_channel(b, value, 0),
                                       nir_channel(b, value, 1));
   }
}

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
lower_subgroup_op_to_scalar(nir_builder *b, nir_intrinsic_instr *intrin,
                            bool lower_to_32bit)
{
   /* This is safe to call on scalar things but it would be silly */
   assert(intrin->dest.ssa.num_components > 1);

   nir_ssa_def *value = nir_ssa_for_src(b, intrin->src[0],
                                           intrin->num_components);
   nir_ssa_def *reads[NIR_MAX_VEC_COMPONENTS];

   for (unsigned i = 0; i < intrin->num_components; i++) {
      nir_intrinsic_instr *chan_intrin =
         nir_intrinsic_instr_create(b->shader, intrin->intrinsic);
      nir_ssa_dest_init(&chan_intrin->instr, &chan_intrin->dest,
                        1, intrin->dest.ssa.bit_size, NULL);
      chan_intrin->num_components = 1;

      /* value */
      chan_intrin->src[0] = nir_src_for_ssa(nir_channel(b, value, i));
      /* invocation */
      if (nir_intrinsic_infos[intrin->intrinsic].num_srcs > 1) {
         assert(nir_intrinsic_infos[intrin->intrinsic].num_srcs == 2);
         nir_src_copy(&chan_intrin->src[1], &intrin->src[1], chan_intrin);
      }

      chan_intrin->const_index[0] = intrin->const_index[0];
      chan_intrin->const_index[1] = intrin->const_index[1];

      if (lower_to_32bit && chan_intrin->src[0].ssa->bit_size == 64) {
         reads[i] = lower_subgroup_op_to_32bit(b, chan_intrin);
      } else {
         nir_builder_instr_insert(b, &chan_intrin->instr);
         reads[i] = &chan_intrin->dest.ssa;
      }
   }

   return nir_vec(b, reads, intrin->num_components);
}

static nir_ssa_def *
lower_vote_eq_to_scalar(nir_builder *b, nir_intrinsic_instr *intrin)
{
   assert(intrin->src[0].is_ssa);
   nir_ssa_def *value = intrin->src[0].ssa;

   nir_ssa_def *result = NULL;
   for (unsigned i = 0; i < intrin->num_components; i++) {
      nir_intrinsic_instr *chan_intrin =
         nir_intrinsic_instr_create(b->shader, intrin->intrinsic);
      nir_ssa_dest_init(&chan_intrin->instr, &chan_intrin->dest,
                        1, intrin->dest.ssa.bit_size, NULL);
      chan_intrin->num_components = 1;
      chan_intrin->src[0] = nir_src_for_ssa(nir_channel(b, value, i));
      nir_builder_instr_insert(b, &chan_intrin->instr);

      if (result) {
         result = nir_iand(b, result, &chan_intrin->dest.ssa);
      } else {
         result = &chan_intrin->dest.ssa;
      }
   }

   return result;
}

static nir_ssa_def *
lower_vote_eq_to_ballot(nir_builder *b, nir_intrinsic_instr *intrin,
                        const nir_lower_subgroups_options *options)
{
   assert(intrin->src[0].is_ssa);
   nir_ssa_def *value = intrin->src[0].ssa;

   /* We have to implicitly lower to scalar */
   nir_ssa_def *all_eq = NULL;
   for (unsigned i = 0; i < intrin->num_components; i++) {
      nir_intrinsic_instr *rfi =
         nir_intrinsic_instr_create(b->shader,
                                    nir_intrinsic_read_first_invocation);
      nir_ssa_dest_init(&rfi->instr, &rfi->dest,
                        1, value->bit_size, NULL);
      rfi->num_components = 1;
      rfi->src[0] = nir_src_for_ssa(nir_channel(b, value, i));
      nir_builder_instr_insert(b, &rfi->instr);

      nir_ssa_def *is_eq;
      if (intrin->intrinsic == nir_intrinsic_vote_feq) {
         is_eq = nir_feq(b, &rfi->dest.ssa, nir_channel(b, value, i));
      } else {
         is_eq = nir_ieq(b, &rfi->dest.ssa, nir_channel(b, value, i));
      }

      if (all_eq == NULL) {
         all_eq = is_eq;
      } else {
         all_eq = nir_iand(b, all_eq, is_eq);
      }
   }

   nir_intrinsic_instr *ballot =
      nir_intrinsic_instr_create(b->shader, nir_intrinsic_ballot);
   nir_ssa_dest_init(&ballot->instr, &ballot->dest,
                     1, options->ballot_bit_size, NULL);
   ballot->num_components = 1;
   ballot->src[0] = nir_src_for_ssa(nir_inot(b, all_eq));
   nir_builder_instr_insert(b, &ballot->instr);

   return nir_ieq(b, &ballot->dest.ssa,
                  nir_imm_intN_t(b, 0, options->ballot_bit_size));
}

static nir_ssa_def *
lower_shuffle_to_swizzle(nir_builder *b, nir_intrinsic_instr *intrin,
                         const nir_lower_subgroups_options *options)
{
   unsigned mask = nir_src_as_uint(intrin->src[1]);

   if (mask >= 32)
      return NULL;

   nir_intrinsic_instr *swizzle = nir_intrinsic_instr_create(
      b->shader, nir_intrinsic_masked_swizzle_amd);
   swizzle->num_components = intrin->num_components;
   nir_src_copy(&swizzle->src[0], &intrin->src[0], swizzle);
   nir_intrinsic_set_swizzle_mask(swizzle, (mask << 10) | 0x1f);
   nir_ssa_dest_init(&swizzle->instr, &swizzle->dest,
                     intrin->dest.ssa.num_components,
                     intrin->dest.ssa.bit_size, NULL);

   if (options->lower_to_scalar && swizzle->num_components > 1) {
      return lower_subgroup_op_to_scalar(b, swizzle, options->lower_shuffle_to_32bit);
   } else if (options->lower_shuffle_to_32bit && swizzle->src[0].ssa->bit_size == 64) {
      return lower_subgroup_op_to_32bit(b, swizzle);
   } else {
      nir_builder_instr_insert(b, &swizzle->instr);
      return &swizzle->dest.ssa;
   }
}

static nir_ssa_def *
lower_shuffle(nir_builder *b, nir_intrinsic_instr *intrin,
              const nir_lower_subgroups_options *options)
{
   if (intrin->intrinsic == nir_intrinsic_shuffle_xor &&
       options->lower_shuffle_to_swizzle_amd &&
       nir_src_is_const(intrin->src[1])) {
      nir_ssa_def *result =
         lower_shuffle_to_swizzle(b, intrin, options);
      if (result)
         return result;
   }

   nir_ssa_def *index = nir_load_subgroup_invocation(b);
   bool is_shuffle = false;
   switch (intrin->intrinsic) {
   case nir_intrinsic_shuffle_xor:
      assert(intrin->src[1].is_ssa);
      index = nir_ixor(b, index, intrin->src[1].ssa);
      is_shuffle = true;
      break;
   case nir_intrinsic_shuffle_up:
      assert(intrin->src[1].is_ssa);
      index = nir_isub(b, index, intrin->src[1].ssa);
      is_shuffle = true;
      break;
   case nir_intrinsic_shuffle_down:
      assert(intrin->src[1].is_ssa);
      index = nir_iadd(b, index, intrin->src[1].ssa);
      is_shuffle = true;
      break;
   case nir_intrinsic_quad_broadcast:
      assert(intrin->src[1].is_ssa);
      index = nir_ior(b, nir_iand(b, index, nir_imm_int(b, ~0x3)),
                         intrin->src[1].ssa);
      break;
   case nir_intrinsic_quad_swap_horizontal:
      /* For Quad operations, subgroups are divided into quads where
       * (invocation % 4) is the index to a square arranged as follows:
       *
       *    +---+---+
       *    | 0 | 1 |
       *    +---+---+
       *    | 2 | 3 |
       *    +---+---+
       */
      index = nir_ixor(b, index, nir_imm_int(b, 0x1));
      break;
   case nir_intrinsic_quad_swap_vertical:
      index = nir_ixor(b, index, nir_imm_int(b, 0x2));
      break;
   case nir_intrinsic_quad_swap_diagonal:
      index = nir_ixor(b, index, nir_imm_int(b, 0x3));
      break;
   default:
      unreachable("Invalid intrinsic");
   }

   nir_intrinsic_instr *shuffle =
      nir_intrinsic_instr_create(b->shader, nir_intrinsic_shuffle);
   shuffle->num_components = intrin->num_components;
   nir_src_copy(&shuffle->src[0], &intrin->src[0], shuffle);
   shuffle->src[1] = nir_src_for_ssa(index);
   nir_ssa_dest_init(&shuffle->instr, &shuffle->dest,
                     intrin->dest.ssa.num_components,
                     intrin->dest.ssa.bit_size, NULL);

   bool lower_to_32bit = options->lower_shuffle_to_32bit && is_shuffle;
   if (options->lower_to_scalar && shuffle->num_components > 1) {
      return lower_subgroup_op_to_scalar(b, shuffle, lower_to_32bit);
   } else if (lower_to_32bit && shuffle->src[0].ssa->bit_size == 64) {
      return lower_subgroup_op_to_32bit(b, shuffle);
   } else {
      nir_builder_instr_insert(b, &shuffle->instr);
      return &shuffle->dest.ssa;
   }
}

static bool
lower_subgroups_filter(const nir_instr *instr, const void *_options)
{
   return instr->type == nir_instr_type_intrinsic;
}

static nir_ssa_def *
build_subgroup_mask(nir_builder *b, unsigned bit_size,
                    const nir_lower_subgroups_options *options)
{
   return nir_ushr(b, nir_imm_intN_t(b, ~0ull, bit_size),
                      nir_isub(b, nir_imm_int(b, bit_size),
                                  nir_load_subgroup_size(b)));
}

static nir_ssa_def *
lower_dynamic_quad_broadcast(nir_builder *b, nir_intrinsic_instr *intrin,
                             const nir_lower_subgroups_options *options)
{
   if (!options->lower_quad_broadcast_dynamic_to_const)
      return lower_shuffle(b, intrin, options);

   nir_ssa_def *dst = NULL;

   for (unsigned i = 0; i < 4; ++i) {
      nir_intrinsic_instr *qbcst =
         nir_intrinsic_instr_create(b->shader, nir_intrinsic_quad_broadcast);

      qbcst->num_components = intrin->num_components;
      qbcst->src[1] = nir_src_for_ssa(nir_imm_int(b, i));
      nir_src_copy(&qbcst->src[0], &intrin->src[0], qbcst);
      nir_ssa_dest_init(&qbcst->instr, &qbcst->dest,
                        intrin->dest.ssa.num_components,
                        intrin->dest.ssa.bit_size, NULL);

      nir_ssa_def *qbcst_dst = NULL;

      if (options->lower_to_scalar && qbcst->num_components > 1) {
         qbcst_dst = lower_subgroup_op_to_scalar(b, qbcst, false);
      } else {
         nir_builder_instr_insert(b, &qbcst->instr);
         qbcst_dst = &qbcst->dest.ssa;
      }

      if (i)
         dst = nir_bcsel(b, nir_ieq(b, intrin->src[1].ssa,
                                    nir_src_for_ssa(nir_imm_int(b, i)).ssa),
                         qbcst_dst, dst);
      else
         dst = qbcst_dst;
   }

   return dst;
}

static nir_ssa_def *
lower_subgroups_instr(nir_builder *b, nir_instr *instr, void *_options)
{
   const nir_lower_subgroups_options *options = _options;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   switch (intrin->intrinsic) {
   case nir_intrinsic_vote_any:
   case nir_intrinsic_vote_all:
      if (options->lower_vote_trivial)
         return nir_ssa_for_src(b, intrin->src[0], 1);
      break;

   case nir_intrinsic_vote_feq:
   case nir_intrinsic_vote_ieq:
      if (options->lower_vote_trivial)
         return nir_imm_true(b);

      if (options->lower_vote_eq_to_ballot)
         return lower_vote_eq_to_ballot(b, intrin, options);

      if (options->lower_to_scalar && intrin->num_components > 1)
         return lower_vote_eq_to_scalar(b, intrin);
      break;

   case nir_intrinsic_load_subgroup_size:
      if (options->subgroup_size)
         return nir_imm_int(b, options->subgroup_size);
      break;

   case nir_intrinsic_read_invocation:
   case nir_intrinsic_read_first_invocation:
      if (options->lower_to_scalar && intrin->num_components > 1)
         return lower_subgroup_op_to_scalar(b, intrin, false);
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

      nir_ssa_def *count = nir_load_subgroup_invocation(b);
      nir_ssa_def *val;
      switch (intrin->intrinsic) {
      case nir_intrinsic_load_subgroup_eq_mask:
         val = nir_ishl(b, nir_imm_intN_t(b, 1ull, bit_size), count);
         break;
      case nir_intrinsic_load_subgroup_ge_mask:
         val = nir_iand(b, nir_ishl(b, nir_imm_intN_t(b, ~0ull, bit_size), count),
                           build_subgroup_mask(b, bit_size, options));
         break;
      case nir_intrinsic_load_subgroup_gt_mask:
         val = nir_iand(b, nir_ishl(b, nir_imm_intN_t(b, ~1ull, bit_size), count),
                           build_subgroup_mask(b, bit_size, options));
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

   case nir_intrinsic_ballot_bitfield_extract:
   case nir_intrinsic_ballot_bit_count_reduce:
   case nir_intrinsic_ballot_find_lsb:
   case nir_intrinsic_ballot_find_msb: {
      assert(intrin->src[0].is_ssa);
      nir_ssa_def *int_val = ballot_type_to_uint(b, intrin->src[0].ssa,
                                                 options->ballot_bit_size);

      if (intrin->intrinsic != nir_intrinsic_ballot_bitfield_extract &&
          intrin->intrinsic != nir_intrinsic_ballot_find_lsb) {
         /* For OpGroupNonUniformBallotFindMSB, the SPIR-V Spec says:
          *
          *    "Find the most significant bit set to 1 in Value, considering
          *    only the bits in Value required to represent all bits of the
          *    group’s invocations.  If none of the considered bits is set to
          *    1, the result is undefined."
          *
          * It has similar text for the other three.  This means that, in case
          * the subgroup size is less than 32, we have to mask off the unused
          * bits.  If the subgroup size is fixed and greater than or equal to
          * 32, the mask will be 0xffffffff and nir_opt_algebraic will delete
          * the iand.
          *
          * We only have to worry about this for BitCount and FindMSB because
          * FindLSB counts from the bottom and BitfieldExtract selects
          * individual bits.  In either case, if run outside the range of
          * valid bits, we hit the undefined results case and we can return
          * anything we want.
          */
         int_val = nir_iand(b, int_val,
            build_subgroup_mask(b, options->ballot_bit_size, options));
      }

      switch (intrin->intrinsic) {
      case nir_intrinsic_ballot_bitfield_extract:
         assert(intrin->src[1].is_ssa);
         return nir_i2b(b, nir_iand(b, nir_ushr(b, int_val,
                                                   intrin->src[1].ssa),
                                       nir_imm_intN_t(b, 1, options->ballot_bit_size)));
      case nir_intrinsic_ballot_bit_count_reduce:
         return nir_bit_count(b, int_val);
      case nir_intrinsic_ballot_find_lsb:
         return nir_find_lsb(b, int_val);
      case nir_intrinsic_ballot_find_msb:
         return nir_ufind_msb(b, int_val);
      default:
         unreachable("you seriously can't tell this is unreachable?");
      }
   }

   case nir_intrinsic_ballot_bit_count_exclusive:
   case nir_intrinsic_ballot_bit_count_inclusive: {
      nir_ssa_def *count = nir_load_subgroup_invocation(b);
      nir_ssa_def *mask = nir_imm_intN_t(b, ~0ull, options->ballot_bit_size);
      if (intrin->intrinsic == nir_intrinsic_ballot_bit_count_inclusive) {
         const unsigned bits = options->ballot_bit_size;
         mask = nir_ushr(b, mask, nir_isub(b, nir_imm_int(b, bits - 1), count));
      } else {
         mask = nir_inot(b, nir_ishl(b, mask, count));
      }

      assert(intrin->src[0].is_ssa);
      nir_ssa_def *int_val = ballot_type_to_uint(b, intrin->src[0].ssa,
                                                 options->ballot_bit_size);

      return nir_bit_count(b, nir_iand(b, int_val, mask));
   }

   case nir_intrinsic_elect: {
      if (!options->lower_elect)
         return NULL;

      nir_intrinsic_instr *first =
         nir_intrinsic_instr_create(b->shader,
                                    nir_intrinsic_first_invocation);
      nir_ssa_dest_init(&first->instr, &first->dest, 1, 32, NULL);
      nir_builder_instr_insert(b, &first->instr);

      return nir_ieq(b, nir_load_subgroup_invocation(b), &first->dest.ssa);
   }

   case nir_intrinsic_shuffle:
      if (options->lower_to_scalar && intrin->num_components > 1)
         return lower_subgroup_op_to_scalar(b, intrin, options->lower_shuffle_to_32bit);
      else if (options->lower_shuffle_to_32bit && intrin->src[0].ssa->bit_size == 64)
         return lower_subgroup_op_to_32bit(b, intrin);
      break;
   case nir_intrinsic_shuffle_xor:
   case nir_intrinsic_shuffle_up:
   case nir_intrinsic_shuffle_down:
      if (options->lower_shuffle)
         return lower_shuffle(b, intrin, options);
      else if (options->lower_to_scalar && intrin->num_components > 1)
         return lower_subgroup_op_to_scalar(b, intrin, options->lower_shuffle_to_32bit);
      else if (options->lower_shuffle_to_32bit && intrin->src[0].ssa->bit_size == 64)
         return lower_subgroup_op_to_32bit(b, intrin);
      break;

   case nir_intrinsic_quad_broadcast:
   case nir_intrinsic_quad_swap_horizontal:
   case nir_intrinsic_quad_swap_vertical:
   case nir_intrinsic_quad_swap_diagonal:
      if (options->lower_quad ||
          (options->lower_quad_broadcast_dynamic &&
           intrin->intrinsic == nir_intrinsic_quad_broadcast &&
           !nir_src_is_const(intrin->src[1])))
         return lower_dynamic_quad_broadcast(b, intrin, options);
      else if (options->lower_to_scalar && intrin->num_components > 1)
         return lower_subgroup_op_to_scalar(b, intrin, false);
      break;

   case nir_intrinsic_reduce: {
      nir_ssa_def *ret = NULL;
      /* A cluster size greater than the subgroup size is implemention defined */
      if (options->subgroup_size &&
          nir_intrinsic_cluster_size(intrin) >= options->subgroup_size) {
         nir_intrinsic_set_cluster_size(intrin, 0);
         ret = NIR_LOWER_INSTR_PROGRESS;
      }
      if (options->lower_to_scalar && intrin->num_components > 1)
         ret = lower_subgroup_op_to_scalar(b, intrin, false);
      return ret;
   }
   case nir_intrinsic_inclusive_scan:
   case nir_intrinsic_exclusive_scan:
      if (options->lower_to_scalar && intrin->num_components > 1)
         return lower_subgroup_op_to_scalar(b, intrin, false);
      break;

   default:
      break;
   }

   return NULL;
}

bool
nir_lower_subgroups(nir_shader *shader,
                    const nir_lower_subgroups_options *options)
{
   return nir_shader_lower_instructions(shader,
                                        lower_subgroups_filter,
                                        lower_subgroups_instr,
                                        (void *)options);
}
