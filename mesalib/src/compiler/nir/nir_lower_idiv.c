/*
 * Copyright © 2015 Red Hat
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

#include "nir.h"
#include "nir_builder.h"

/* Lowers idiv/udiv/umod
 * Based on NV50LegalizeSSA::handleDIV()
 *
 * Note that this is probably not enough precision for compute shaders.
 * Perhaps we want a second higher precision (looping) version of this?
 * Or perhaps we assume if you can do compute shaders you can also
 * branch out to a pre-optimized shader library routine..
 */

static bool
convert_instr(nir_builder *bld, nir_alu_instr *alu)
{
   nir_ssa_def *numer, *denom, *af, *bf, *a, *b, *q, *r;
   nir_op op = alu->op;
   bool is_signed;

   if ((op != nir_op_idiv) &&
       (op != nir_op_udiv) &&
       (op != nir_op_umod))
      return false;

   is_signed = (op == nir_op_idiv);

   bld->cursor = nir_before_instr(&alu->instr);

   numer = nir_ssa_for_alu_src(bld, alu, 0);
   denom = nir_ssa_for_alu_src(bld, alu, 1);

   if (is_signed) {
      af = nir_i2f(bld, numer);
      bf = nir_i2f(bld, denom);
      af = nir_fabs(bld, af);
      bf = nir_fabs(bld, bf);
      a  = nir_iabs(bld, numer);
      b  = nir_iabs(bld, denom);
   } else {
      af = nir_u2f(bld, numer);
      bf = nir_u2f(bld, denom);
      a  = numer;
      b  = denom;
   }

   /* get first result: */
   bf = nir_frcp(bld, bf);
   bf = nir_isub(bld, bf, nir_imm_int(bld, 2));  /* yes, really */
   q  = nir_fmul(bld, af, bf);

   if (is_signed) {
      q = nir_f2i(bld, q);
   } else {
      q = nir_f2u(bld, q);
   }

   /* get error of first result: */
   r = nir_imul(bld, q, b);
   r = nir_isub(bld, a, r);
   r = nir_u2f(bld, r);
   r = nir_fmul(bld, r, bf);
   r = nir_f2u(bld, r);

   /* add quotients: */
   q = nir_iadd(bld, q, r);

   /* correction: if modulus >= divisor, add 1 */
   r = nir_imul(bld, q, b);
   r = nir_isub(bld, a, r);

   r = nir_uge(bld, r, b);
   r = nir_b2i(bld, r);

   q = nir_iadd(bld, q, r);
   if (is_signed)  {
      /* fix the sign: */
      r = nir_ixor(bld, numer, denom);
      r = nir_ishr(bld, r, nir_imm_int(bld, 31));
      b = nir_ineg(bld, q);
      q = nir_bcsel(bld, r, b, q);
   }

   if (op == nir_op_umod) {
      /* division result in q */
      r = nir_imul(bld, q, b);
      q = nir_isub(bld, a, r);
   }

   assert(alu->dest.dest.is_ssa);
   nir_ssa_def_rewrite_uses(&alu->dest.dest.ssa, nir_src_for_ssa(q));

   return true;
}

static bool
convert_impl(nir_function_impl *impl)
{
   nir_builder b;
   nir_builder_init(&b, impl);
   bool progress = false;

   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type == nir_instr_type_alu)
            progress |= convert_instr(&b, nir_instr_as_alu(instr));
      }
   }

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);

   return progress;
}

bool
nir_lower_idiv(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= convert_impl(function->impl);
   }

   return progress;
}
