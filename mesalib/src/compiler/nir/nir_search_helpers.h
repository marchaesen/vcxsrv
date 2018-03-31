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

static inline bool
is_pos_power_of_two(nir_alu_instr *instr, unsigned src, unsigned num_components,
                    const uint8_t *swizzle)
{
   nir_const_value *val = nir_src_as_const_value(instr->src[src].src);

   /* only constant srcs: */
   if (!val)
      return false;

   for (unsigned i = 0; i < num_components; i++) {
      switch (nir_op_infos[instr->op].input_types[src]) {
      case nir_type_int:
         if (val->i32[swizzle[i]] < 0)
            return false;
         if (!util_is_power_of_two_nonzero(val->i32[swizzle[i]]))
            return false;
         break;
      case nir_type_uint:
         if (!util_is_power_of_two_nonzero(val->u32[swizzle[i]]))
            return false;
         break;
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
   nir_const_value *val = nir_src_as_const_value(instr->src[src].src);

   /* only constant srcs: */
   if (!val)
      return false;

   for (unsigned i = 0; i < num_components; i++) {
      switch (nir_op_infos[instr->op].input_types[src]) {
      case nir_type_int:
         if (val->i32[swizzle[i]] > 0)
            return false;
         if (!util_is_power_of_two_nonzero(abs(val->i32[swizzle[i]])))
            return false;
         break;
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
   nir_const_value *val = nir_src_as_const_value(instr->src[src].src);

   if (!val)
      return false;

   for (unsigned i = 0; i < num_components; i++) {
      switch (nir_op_infos[instr->op].input_types[src]) {
      case nir_type_float:
         if (val->f32[swizzle[i]] < 0.0f || val->f32[swizzle[i]] > 1.0f)
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
   nir_const_value *val = nir_src_as_const_value(instr->src[src].src);

   if (val)
      return false;

   return true;
}

static inline bool
is_used_more_than_once(nir_alu_instr *instr)
{
   bool zero_if_use = list_empty(&instr->dest.dest.ssa.if_uses);
   bool zero_use = list_empty(&instr->dest.dest.ssa.uses);

   if (zero_use && zero_if_use)
      return false;
   else if (zero_use && list_is_singular(&instr->dest.dest.ssa.if_uses))
      return false;
   else if (zero_if_use && list_is_singular(&instr->dest.dest.ssa.uses))
      return false;

   return true;
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
is_not_used_by_if(nir_alu_instr *instr)
{
   return list_empty(&instr->dest.dest.ssa.if_uses);
}

#endif /* _NIR_SEARCH_ */
