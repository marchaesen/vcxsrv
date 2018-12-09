/*
 * Copyright © 2016 Intel Corporation
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

static nir_ssa_def *
lower_imul64(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   nir_ssa_def *x_lo = nir_unpack_64_2x32_split_x(b, x);
   nir_ssa_def *x_hi = nir_unpack_64_2x32_split_y(b, x);
   nir_ssa_def *y_lo = nir_unpack_64_2x32_split_x(b, y);
   nir_ssa_def *y_hi = nir_unpack_64_2x32_split_y(b, y);

   nir_ssa_def *res_lo = nir_imul(b, x_lo, y_lo);
   nir_ssa_def *res_hi = nir_iadd(b, nir_umul_high(b, x_lo, y_lo),
                         nir_iadd(b, nir_imul(b, x_lo, y_hi),
                                     nir_imul(b, x_hi, y_lo)));

   return nir_pack_64_2x32_split(b, res_lo, res_hi);
}

static nir_ssa_def *
lower_isign64(nir_builder *b, nir_ssa_def *x)
{
   nir_ssa_def *x_lo = nir_unpack_64_2x32_split_x(b, x);
   nir_ssa_def *x_hi = nir_unpack_64_2x32_split_y(b, x);

   nir_ssa_def *is_non_zero = nir_i2b(b, nir_ior(b, x_lo, x_hi));
   nir_ssa_def *res_hi = nir_ishr(b, x_hi, nir_imm_int(b, 31));
   nir_ssa_def *res_lo = nir_ior(b, res_hi, nir_b2i32(b, is_non_zero));

   return nir_pack_64_2x32_split(b, res_lo, res_hi);
}

static void
lower_udiv64_mod64(nir_builder *b, nir_ssa_def *n, nir_ssa_def *d,
                   nir_ssa_def **q, nir_ssa_def **r)
{
   /* TODO: We should specially handle the case where the denominator is a
    * constant.  In that case, we should be able to reduce it to a multiply by
    * a constant, some shifts, and an add.
    */
   nir_ssa_def *n_lo = nir_unpack_64_2x32_split_x(b, n);
   nir_ssa_def *n_hi = nir_unpack_64_2x32_split_y(b, n);
   nir_ssa_def *d_lo = nir_unpack_64_2x32_split_x(b, d);
   nir_ssa_def *d_hi = nir_unpack_64_2x32_split_y(b, d);

   nir_const_value v = { .u32 = { 0, 0, 0, 0 } };
   nir_ssa_def *q_lo = nir_build_imm(b, n->num_components, 32, v);
   nir_ssa_def *q_hi = nir_build_imm(b, n->num_components, 32, v);

   nir_ssa_def *n_hi_before_if = n_hi;
   nir_ssa_def *q_hi_before_if = q_hi;

   /* If the upper 32 bits of denom are non-zero, it is impossible for shifts
    * greater than 32 bits to occur.  If the upper 32 bits of the numerator
    * are zero, it is impossible for (denom << [63, 32]) <= numer unless
    * denom == 0.
    */
   nir_ssa_def *need_high_div =
      nir_iand(b, nir_ieq(b, d_hi, nir_imm_int(b, 0)), nir_uge(b, n_hi, d_lo));
   nir_push_if(b, nir_bany(b, need_high_div));
   {
      /* If we only have one component, then the bany above goes away and
       * this is always true within the if statement.
       */
      if (n->num_components == 1)
         need_high_div = nir_imm_true(b);

      nir_ssa_def *log2_d_lo = nir_ufind_msb(b, d_lo);

      for (int i = 31; i >= 0; i--) {
         /* if ((d.x << i) <= n.y) {
          *    n.y -= d.x << i;
          *    quot.y |= 1U << i;
          * }
          */
         nir_ssa_def *d_shift = nir_ishl(b, d_lo, nir_imm_int(b, i));
         nir_ssa_def *new_n_hi = nir_isub(b, n_hi, d_shift);
         nir_ssa_def *new_q_hi = nir_ior(b, q_hi, nir_imm_int(b, 1u << i));
         nir_ssa_def *cond = nir_iand(b, need_high_div,
                                         nir_uge(b, n_hi, d_shift));
         if (i != 0) {
            /* log2_d_lo is always <= 31, so we don't need to bother with it
             * in the last iteration.
             */
            cond = nir_iand(b, cond,
                               nir_ige(b, nir_imm_int(b, 31 - i), log2_d_lo));
         }
         n_hi = nir_bcsel(b, cond, new_n_hi, n_hi);
         q_hi = nir_bcsel(b, cond, new_q_hi, q_hi);
      }
   }
   nir_pop_if(b, NULL);
   n_hi = nir_if_phi(b, n_hi, n_hi_before_if);
   q_hi = nir_if_phi(b, q_hi, q_hi_before_if);

   nir_ssa_def *log2_denom = nir_ufind_msb(b, d_hi);

   n = nir_pack_64_2x32_split(b, n_lo, n_hi);
   d = nir_pack_64_2x32_split(b, d_lo, d_hi);
   for (int i = 31; i >= 0; i--) {
      /* if ((d64 << i) <= n64) {
       *    n64 -= d64 << i;
       *    quot.x |= 1U << i;
       * }
       */
      nir_ssa_def *d_shift = nir_ishl(b, d, nir_imm_int(b, i));
      nir_ssa_def *new_n = nir_isub(b, n, d_shift);
      nir_ssa_def *new_q_lo = nir_ior(b, q_lo, nir_imm_int(b, 1u << i));
      nir_ssa_def *cond = nir_uge(b, n, d_shift);
      if (i != 0) {
         /* log2_denom is always <= 31, so we don't need to bother with it
          * in the last iteration.
          */
         cond = nir_iand(b, cond,
                            nir_ige(b, nir_imm_int(b, 31 - i), log2_denom));
      }
      n = nir_bcsel(b, cond, new_n, n);
      q_lo = nir_bcsel(b, cond, new_q_lo, q_lo);
   }

   *q = nir_pack_64_2x32_split(b, q_lo, q_hi);
   *r = n;
}

static nir_ssa_def *
lower_udiv64(nir_builder *b, nir_ssa_def *n, nir_ssa_def *d)
{
   nir_ssa_def *q, *r;
   lower_udiv64_mod64(b, n, d, &q, &r);
   return q;
}

static nir_ssa_def *
lower_idiv64(nir_builder *b, nir_ssa_def *n, nir_ssa_def *d)
{
   nir_ssa_def *n_hi = nir_unpack_64_2x32_split_y(b, n);
   nir_ssa_def *d_hi = nir_unpack_64_2x32_split_y(b, d);

   nir_ssa_def *negate = nir_ine(b, nir_ilt(b, n_hi, nir_imm_int(b, 0)),
                                    nir_ilt(b, d_hi, nir_imm_int(b, 0)));
   nir_ssa_def *q, *r;
   lower_udiv64_mod64(b, nir_iabs(b, n), nir_iabs(b, d), &q, &r);
   return nir_bcsel(b, negate, nir_ineg(b, q), q);
}

static nir_ssa_def *
lower_umod64(nir_builder *b, nir_ssa_def *n, nir_ssa_def *d)
{
   nir_ssa_def *q, *r;
   lower_udiv64_mod64(b, n, d, &q, &r);
   return r;
}

static nir_ssa_def *
lower_imod64(nir_builder *b, nir_ssa_def *n, nir_ssa_def *d)
{
   nir_ssa_def *n_hi = nir_unpack_64_2x32_split_y(b, n);
   nir_ssa_def *d_hi = nir_unpack_64_2x32_split_y(b, d);
   nir_ssa_def *n_is_neg = nir_ilt(b, n_hi, nir_imm_int(b, 0));
   nir_ssa_def *d_is_neg = nir_ilt(b, d_hi, nir_imm_int(b, 0));

   nir_ssa_def *q, *r;
   lower_udiv64_mod64(b, nir_iabs(b, n), nir_iabs(b, d), &q, &r);

   nir_ssa_def *rem = nir_bcsel(b, n_is_neg, nir_ineg(b, r), r);

   return nir_bcsel(b, nir_ieq(b, r, nir_imm_int64(b, 0)), nir_imm_int64(b, 0),
          nir_bcsel(b, nir_ieq(b, n_is_neg, d_is_neg), rem,
                       nir_iadd(b, rem, d)));
}

static nir_ssa_def *
lower_irem64(nir_builder *b, nir_ssa_def *n, nir_ssa_def *d)
{
   nir_ssa_def *n_hi = nir_unpack_64_2x32_split_y(b, n);
   nir_ssa_def *n_is_neg = nir_ilt(b, n_hi, nir_imm_int(b, 0));

   nir_ssa_def *q, *r;
   lower_udiv64_mod64(b, nir_iabs(b, n), nir_iabs(b, d), &q, &r);
   return nir_bcsel(b, n_is_neg, nir_ineg(b, r), r);
}

static nir_lower_int64_options
opcode_to_options_mask(nir_op opcode)
{
   switch (opcode) {
   case nir_op_imul:
      return nir_lower_imul64;
   case nir_op_isign:
      return nir_lower_isign64;
   case nir_op_udiv:
   case nir_op_idiv:
   case nir_op_umod:
   case nir_op_imod:
   case nir_op_irem:
      return nir_lower_divmod64;
   default:
      return 0;
   }
}

static nir_ssa_def *
lower_int64_alu_instr(nir_builder *b, nir_alu_instr *alu)
{
   nir_ssa_def *src[4];
   for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++)
      src[i] = nir_ssa_for_alu_src(b, alu, i);

   switch (alu->op) {
   case nir_op_imul:
      return lower_imul64(b, src[0], src[1]);
   case nir_op_isign:
      return lower_isign64(b, src[0]);
   case nir_op_udiv:
      return lower_udiv64(b, src[0], src[1]);
   case nir_op_idiv:
      return lower_idiv64(b, src[0], src[1]);
   case nir_op_umod:
      return lower_umod64(b, src[0], src[1]);
   case nir_op_imod:
      return lower_imod64(b, src[0], src[1]);
   case nir_op_irem:
      return lower_irem64(b, src[0], src[1]);
   default:
      unreachable("Invalid ALU opcode to lower");
   }
}

static bool
lower_int64_impl(nir_function_impl *impl, nir_lower_int64_options options)
{
   nir_builder b;
   nir_builder_init(&b, impl);

   bool progress = false;
   nir_foreach_block(block, impl) {
      nir_foreach_instr_safe(instr, block) {
         if (instr->type != nir_instr_type_alu)
            continue;

         nir_alu_instr *alu = nir_instr_as_alu(instr);
         assert(alu->dest.dest.is_ssa);
         if (alu->dest.dest.ssa.bit_size != 64)
            continue;

         if (!(options & opcode_to_options_mask(alu->op)))
            continue;

         b.cursor = nir_before_instr(instr);

         nir_ssa_def *lowered = lower_int64_alu_instr(&b, alu);
         nir_ssa_def_rewrite_uses(&alu->dest.dest.ssa,
                                  nir_src_for_ssa(lowered));
         nir_instr_remove(&alu->instr);
         progress = true;
      }
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_none);

   return progress;
}

bool
nir_lower_int64(nir_shader *shader, nir_lower_int64_options options)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= lower_int64_impl(function->impl, options);
   }

   return progress;
}
