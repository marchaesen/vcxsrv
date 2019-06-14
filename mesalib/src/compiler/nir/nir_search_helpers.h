/*
 * Copyright © 2016 Red Hat
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
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef _NIR_SEARCH_HELPERS_
#define _NIR_SEARCH_HELPERS_

#include "nir.h"
#include "util/bitscan.h"
#include <math.h>

static inline bool
is_pos_power_of_two(nir_alu_instr *instr, unsigned src, unsigned num_components,
                    const uint8_t *swizzle)
{
   /* only constant srcs: */
   if (!nir_src_is_const(instr->src[src].src))
      return false;

   for (unsigned i = 0; i < num_components; i++) {
      switch (nir_op_infos[instr->op].input_types[src]) {
      case nir_type_int: {
         int64_t val = nir_src_comp_as_int(instr->src[src].src, swizzle[i]);
         if (val <= 0 || !util_is_power_of_two_or_zero64(val))
            return false;
         break;
      }
      case nir_type_uint: {
         uint64_t val = nir_src_comp_as_uint(instr->src[src].src, swizzle[i]);
         if (val == 0 || !util_is_power_of_two_or_zero64(val))
            return false;
         break;
      }
      default:
         return false;
      }
   }

   return true;
}

static inline bool
is_neg_power_of_two(nir_alu_instr *instr, unsigned src, unsigned num_components,
                    const uint8_t *swizzle)
{
   /* only constant srcs: */
   if (!nir_src_is_const(instr->src[src].src))
      return false;

   for (unsigned i = 0; i < num_components; i++) {
      switch (nir_op_infos[instr->op].input_types[src]) {
      case nir_type_int: {
         int64_t val = nir_src_comp_as_int(instr->src[src].src, swizzle[i]);
         if (val >= 0 || !util_is_power_of_two_or_zero64(-val))
            return false;
         break;
      }
      default:
         return false;
      }
   }

   return true;
}

static inline bool
is_zero_to_one(nir_alu_instr *instr, unsigned src, unsigned num_components,
               const uint8_t *swizzle)
{
   /* only constant srcs: */
   if (!nir_src_is_const(instr->src[src].src))
      return false;

   for (unsigned i = 0; i < num_components; i++) {
      switch (nir_op_infos[instr->op].input_types[src]) {
      case nir_type_float: {
         double val = nir_src_comp_as_float(instr->src[src].src, swizzle[i]);
         if (isnan(val) || val < 0.0f || val > 1.0f)
            return false;
         break;
      }
      default:
         return false;
      }
   }

   return true;
}

/**
 * Exclusive compare with (0, 1).
 *
 * This differs from \c is_zero_to_one because that function tests 0 <= src <=
 * 1 while this function tests 0 < src < 1.
 */
static inline bool
is_gt_0_and_lt_1(nir_alu_instr *instr, unsigned src, unsigned num_components,
                 const uint8_t *swizzle)
{
   /* only constant srcs: */
   if (!nir_src_is_const(instr->src[src].src))
      return false;

   for (unsigned i = 0; i < num_components; i++) {
      switch (nir_op_infos[instr->op].input_types[src]) {
      case nir_type_float: {
         double val = nir_src_comp_as_float(instr->src[src].src, swizzle[i]);
         if (isnan(val) || val <= 0.0f || val >= 1.0f)
            return false;
         break;
      }
      default:
         return false;
      }
   }

   return true;
}

static inline bool
is_not_const_zero(nir_alu_instr *instr, unsigned src, unsigned num_components,
                  const uint8_t *swizzle)
{
   if (nir_src_as_const_value(instr->src[src].src) == NULL)
      return true;

   for (unsigned i = 0; i < num_components; i++) {
      switch (nir_op_infos[instr->op].input_types[src]) {
      case nir_type_float:
         if (nir_src_comp_as_float(instr->src[src].src, swizzle[i]) == 0.0)
            return false;
         break;
      case nir_type_bool:
      case nir_type_int:
      case nir_type_uint:
         if (nir_src_comp_as_uint(instr->src[src].src, swizzle[i]) == 0)
            return false;
         break;
      default:
         return false;
      }
   }

   return true;
}

static inline bool
is_not_const(nir_alu_instr *instr, unsigned src, UNUSED unsigned num_components,
             UNUSED const uint8_t *swizzle)
{
   return !nir_src_is_const(instr->src[src].src);
}

static inline bool
is_not_fmul(nir_alu_instr *instr, unsigned src,
            UNUSED unsigned num_components, UNUSED const uint8_t *swizzle)
{
   nir_alu_instr *src_alu =
      nir_src_as_alu_instr(instr->src[src].src);

   if (src_alu == NULL)
      return true;

   if (src_alu->op == nir_op_fneg)
      return is_not_fmul(src_alu, 0, 0, NULL);

   return src_alu->op != nir_op_fmul;
}

static inline bool
is_used_once(nir_alu_instr *instr)
{
   bool zero_if_use = list_empty(&instr->dest.dest.ssa.if_uses);
   bool zero_use = list_empty(&instr->dest.dest.ssa.uses);

   if (zero_if_use && zero_use)
      return false;

   if (!zero_if_use && list_is_singular(&instr->dest.dest.ssa.uses))
     return false;

   if (!zero_use && list_is_singular(&instr->dest.dest.ssa.if_uses))
     return false;

   if (!list_is_singular(&instr->dest.dest.ssa.if_uses) &&
       !list_is_singular(&instr->dest.dest.ssa.uses))
      return false;

   return true;
}

static inline bool
is_used_by_if(nir_alu_instr *instr)
{
   return !list_empty(&instr->dest.dest.ssa.if_uses);
}

static inline bool
is_not_used_by_if(nir_alu_instr *instr)
{
   return list_empty(&instr->dest.dest.ssa.if_uses);
}

static inline bool
is_used_by_non_fsat(nir_alu_instr *instr)
{
   nir_foreach_use(src, &instr->dest.dest.ssa) {
      const nir_instr *const user_instr = src->parent_instr;

      if (user_instr->type != nir_instr_type_alu)
         return true;

      const nir_alu_instr *const user_alu = nir_instr_as_alu(user_instr);

      assert(instr != user_alu);
      if (user_alu->op != nir_op_fsat)
         return true;
   }

   return false;
}

/**
 * Returns true if a NIR ALU src represents a constant integer
 * of either 32 or 64 bits, and the higher word (bit-size / 2)
 * of all its components is zero.
 */
static inline bool
is_upper_half_zero(nir_alu_instr *instr, unsigned src,
                   unsigned num_components, const uint8_t *swizzle)
{
   if (nir_src_as_const_value(instr->src[src].src) == NULL)
      return false;

   for (unsigned i = 0; i < num_components; i++) {
      unsigned half_bit_size = nir_src_bit_size(instr->src[src].src) / 2;
      uint32_t high_bits = ((1 << half_bit_size) - 1) << half_bit_size;
      if ((nir_src_comp_as_uint(instr->src[src].src,
                                swizzle[i]) & high_bits) != 0) {
         return false;
      }
   }

   return true;
}

/**
 * Returns true if a NIR ALU src represents a constant integer
 * of either 32 or 64 bits, and the lower word (bit-size / 2)
 * of all its components is zero.
 */
static inline bool
is_lower_half_zero(nir_alu_instr *instr, unsigned src,
                   unsigned num_components, const uint8_t *swizzle)
{
   if (nir_src_as_const_value(instr->src[src].src) == NULL)
      return false;

   for (unsigned i = 0; i < num_components; i++) {
      uint32_t low_bits =
         (1 << (nir_src_bit_size(instr->src[src].src) / 2)) - 1;
      if ((nir_src_comp_as_int(instr->src[src].src, swizzle[i]) & low_bits) != 0)
         return false;
   }

   return true;
}

#endif /* _NIR_SEARCH_ */
