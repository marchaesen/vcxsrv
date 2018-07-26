/*
 * Copyright © 2018 Red Hat Inc.
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
#include "nir_builtin_builder.h"

nir_ssa_def*
nir_cross(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   unsigned yzx[3] = { 1, 2, 0 };
   unsigned zxy[3] = { 2, 0, 1 };

   return nir_fsub(b, nir_fmul(b, nir_swizzle(b, x, yzx, 3, true),
                                  nir_swizzle(b, y, zxy, 3, true)),
                      nir_fmul(b, nir_swizzle(b, x, zxy, 3, true),
                                  nir_swizzle(b, y, yzx, 3, true)));
}

nir_ssa_def*
nir_fast_length(nir_builder *b, nir_ssa_def *vec)
{
   switch (vec->num_components) {
   case 1: return nir_fsqrt(b, nir_fmul(b, vec, vec));
   case 2: return nir_fsqrt(b, nir_fdot2(b, vec, vec));
   case 3: return nir_fsqrt(b, nir_fdot3(b, vec, vec));
   case 4: return nir_fsqrt(b, nir_fdot4(b, vec, vec));
   default:
      unreachable("Invalid number of components");
   }
}

nir_ssa_def*
nir_smoothstep(nir_builder *b, nir_ssa_def *edge0, nir_ssa_def *edge1, nir_ssa_def *x)
{
   nir_ssa_def *f2 = nir_imm_floatN_t(b, 2.0, x->bit_size);
   nir_ssa_def *f3 = nir_imm_floatN_t(b, 3.0, x->bit_size);

   /* t = clamp((x - edge0) / (edge1 - edge0), 0, 1) */
   nir_ssa_def *t =
      nir_fsat(b, nir_fdiv(b, nir_fsub(b, x, edge0),
                              nir_fsub(b, edge1, edge0)));

   /* result = t * t * (3 - 2 * t) */
   return nir_fmul(b, t, nir_fmul(b, t, nir_fsub(b, f3, nir_fmul(b, f2, t))));
}
