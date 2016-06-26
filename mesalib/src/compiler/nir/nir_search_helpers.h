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

static inline bool
__is_power_of_two(unsigned int x)
{
   return ((x != 0) && !(x & (x - 1)));
}

static inline bool
is_pos_power_of_two(nir_alu_instr *instr, unsigned src, unsigned num_components,
                    const uint8_t *swizzle)
{
   nir_const_value *val = nir_src_as_const_value(instr->src[src].src);

   /* only constant src's: */
   if (!val)
      return false;

   for (unsigned i = 0; i < num_components; i++) {
      switch (nir_op_infos[instr->op].input_types[src]) {
      case nir_type_int:
         if (val->i32[swizzle[i]] < 0)
            return false;
         if (!__is_power_of_two(val->i32[swizzle[i]]))
            return false;
         break;
      case nir_type_uint:
         if (!__is_power_of_two(val->u32[swizzle[i]]))
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

   /* only constant src's: */
   if (!val)
      return false;

   for (unsigned i = 0; i < num_components; i++) {
      switch (nir_op_infos[instr->op].input_types[src]) {
      case nir_type_int:
         if (val->i32[swizzle[i]] > 0)
            return false;
         if (!__is_power_of_two(abs(val->i32[swizzle[i]])))
            return false;
         break;
      default:
         return false;
      }
   }

   return true;
}

#endif /* _NIR_SEARCH_ */
