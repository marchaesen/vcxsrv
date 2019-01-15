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
lower_b2i64(nir_builder *b, nir_ssa_def *x)
{
   return nir_pack_64_2x32_split(b, nir_b2i32(b, x), nir_imm_int(b, 0));
}

static nir_ssa_def *
lower_i2b(nir_builder *b, nir_ssa_def *x)
{
   return nir_ine(b, nir_ior(b, nir_unpack_64_2x32_split_x(b, x),
                                nir_unpack_64_2x32_split_y(b, x)),
                     nir_imm_int(b, 0));
}

static nir_ssa_def *
lower_i2i8(nir_builder *b, nir_ssa_def *x)
{
   return nir_i2i8(b, nir_unpack_64_2x32_split_x(b, x));
}

static nir_ssa_def *
lower_i2i16(nir_builder *b, nir_ssa_def *x)
{
   return nir_i2i16(b, nir_unpack_64_2x32_split_x(b, x));
}


static nir_ssa_def *
lower_i2i32(nir_builder *b, nir_ssa_def *x)
{
   return nir_unpack_64_2x32_split_x(b, x);
}

static nir_ssa_def *
lower_i2i64(nir_builder *b, nir_ssa_def *x)
{
   nir_ssa_def *x32 = x->bit_size == 32 ? x : nir_i2i32(b, x);
   return nir_pack_64_2x32_split(b, x32, nir_ishr(b, x32, nir_imm_int(b, 31)));
}

static nir_ssa_def *
lower_u2u8(nir_builder *b, nir_ssa_def *x)
{
   return nir_u2u8(b, nir_unpack_64_2x32_split_x(b, x));
}

static nir_ssa_def *
lower_u2u16(nir_builder *b, nir_ssa_def *x)
{
   return nir_u2u16(b, nir_unpack_64_2x32_split_x(b, x));
}

static nir_ssa_def *
lower_u2u32(nir_builder *b, nir_ssa_def *x)
{
   return nir_unpack_64_2x32_split_x(b, x);
}

static nir_ssa_def *
lower_u2u64(nir_builder *b, nir_ssa_def *x)
{
   nir_ssa_def *x32 = x->bit_size == 32 ? x : nir_u2u32(b, x);
   return nir_pack_64_2x32_split(b, x32, nir_imm_int(b, 0));
}

static nir_ssa_def *
lower_bcsel64(nir_builder *b, nir_ssa_def *cond, nir_ssa_def *x, nir_ssa_def *y)
{
   nir_ssa_def *x_lo = nir_unpack_64_2x32_split_x(b, x);
   nir_ssa_def *x_hi = nir_unpack_64_2x32_split_y(b, x);
   nir_ssa_def *y_lo = nir_unpack_64_2x32_split_x(b, y);
   nir_ssa_def *y_hi = nir_unpack_64_2x32_split_y(b, y);

   return nir_pack_64_2x32_split(b, nir_bcsel(b, cond, x_lo, y_lo),
                                    nir_bcsel(b, cond, x_hi, y_hi));
}

static nir_ssa_def *
lower_inot64(nir_builder *b, nir_ssa_def *x)
{
   nir_ssa_def *x_lo = nir_unpack_64_2x32_split_x(b, x);
   nir_ssa_def *x_hi = nir_unpack_64_2x32_split_y(b, x);

   return nir_pack_64_2x32_split(b, nir_inot(b, x_lo), nir_inot(b, x_hi));
}

static nir_ssa_def *
lower_iand64(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   nir_ssa_def *x_lo = nir_unpack_64_2x32_split_x(b, x);
   nir_ssa_def *x_hi = nir_unpack_64_2x32_split_y(b, x);
   nir_ssa_def *y_lo = nir_unpack_64_2x32_split_x(b, y);
   nir_ssa_def *y_hi = nir_unpack_64_2x32_split_y(b, y);

   return nir_pack_64_2x32_split(b, nir_iand(b, x_lo, y_lo),
                                    nir_iand(b, x_hi, y_hi));
}

static nir_ssa_def *
lower_ior64(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   nir_ssa_def *x_lo = nir_unpack_64_2x32_split_x(b, x);
   nir_ssa_def *x_hi = nir_unpack_64_2x32_split_y(b, x);
   nir_ssa_def *y_lo = nir_unpack_64_2x32_split_x(b, y);
   nir_ssa_def *y_hi = nir_unpack_64_2x32_split_y(b, y);

   return nir_pack_64_2x32_split(b, nir_ior(b, x_lo, y_lo),
                                    nir_ior(b, x_hi, y_hi));
}

static nir_ssa_def *
lower_ixor64(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   nir_ssa_def *x_lo = nir_unpack_64_2x32_split_x(b, x);
   nir_ssa_def *x_hi = nir_unpack_64_2x32_split_y(b, x);
   nir_ssa_def *y_lo = nir_unpack_64_2x32_split_x(b, y);
   nir_ssa_def *y_hi = nir_unpack_64_2x32_split_y(b, y);

   return nir_pack_64_2x32_split(b, nir_ixor(b, x_lo, y_lo),
                                    nir_ixor(b, x_hi, y_hi));
}

static nir_ssa_def *
lower_ishl64(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   /* Implemented as
    *
    * uint64_t lshift(uint64_t x, int c)
    * {
    *    if (c == 0) return x;
    *
    *    uint32_t lo = LO(x), hi = HI(x);
    *
    *    if (c < 32) {
    *       uint32_t lo_shifted = lo << c;
    *       uint32_t hi_shifted = hi << c;
    *       uint32_t lo_shifted_hi = lo >> abs(32 - c);
    *       return pack_64(lo_shifted, hi_shifted | lo_shifted_hi);
    *    } else {
    *       uint32_t lo_shifted_hi = lo << abs(32 - c);
    *       return pack_64(0, lo_shifted_hi);
    *    }
    * }
    */
   nir_ssa_def *x_lo = nir_unpack_64_2x32_split_x(b, x);
   nir_ssa_def *x_hi = nir_unpack_64_2x32_split_y(b, x);

   nir_ssa_def *reverse_count = nir_iabs(b, nir_iadd(b, y, nir_imm_int(b, -32)));
   nir_ssa_def *lo_shifted = nir_ishl(b, x_lo, y);
   nir_ssa_def *hi_shifted = nir_ishl(b, x_hi, y);
   nir_ssa_def *lo_shifted_hi = nir_ushr(b, x_lo, reverse_count);

   nir_ssa_def *res_if_lt_32 =
      nir_pack_64_2x32_split(b, lo_shifted,
                                nir_ior(b, hi_shifted, lo_shifted_hi));
   nir_ssa_def *res_if_ge_32 =
      nir_pack_64_2x32_split(b, nir_imm_int(b, 0),
                                nir_ishl(b, x_lo, reverse_count));

   return nir_bcsel(b,
                    nir_ieq(b, y, nir_imm_int(b, 0)), x,
                    nir_bcsel(b, nir_uge(b, y, nir_imm_int(b, 32)),
                                 res_if_ge_32, res_if_lt_32));
}

static nir_ssa_def *
lower_ishr64(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   /* Implemented as
    *
    * uint64_t arshift(uint64_t x, int c)
    * {
    *    if (c == 0) return x;
    *
    *    uint32_t lo = LO(x);
    *    int32_t  hi = HI(x);
    *
    *    if (c < 32) {
    *       uint32_t lo_shifted = lo >> c;
    *       uint32_t hi_shifted = hi >> c;
    *       uint32_t hi_shifted_lo = hi << abs(32 - c);
    *       return pack_64(hi_shifted, hi_shifted_lo | lo_shifted);
    *    } else {
    *       uint32_t hi_shifted = hi >> 31;
    *       uint32_t hi_shifted_lo = hi >> abs(32 - c);
    *       return pack_64(hi_shifted, hi_shifted_lo);
    *    }
    * }
    */
   nir_ssa_def *x_lo = nir_unpack_64_2x32_split_x(b, x);
   nir_ssa_def *x_hi = nir_unpack_64_2x32_split_y(b, x);

   nir_ssa_def *reverse_count = nir_iabs(b, nir_iadd(b, y, nir_imm_int(b, -32)));
   nir_ssa_def *lo_shifted = nir_ushr(b, x_lo, y);
   nir_ssa_def *hi_shifted = nir_ishr(b, x_hi, y);
   nir_ssa_def *hi_shifted_lo = nir_ishl(b, x_hi, reverse_count);

   nir_ssa_def *res_if_lt_32 =
      nir_pack_64_2x32_split(b, nir_ior(b, lo_shifted, hi_shifted_lo),
                                hi_shifted);
   nir_ssa_def *res_if_ge_32 =
      nir_pack_64_2x32_split(b, nir_ishr(b, x_hi, reverse_count),
                                nir_ishr(b, x_hi, nir_imm_int(b, 31)));

   return nir_bcsel(b,
                    nir_ieq(b, y, nir_imm_int(b, 0)), x,
                    nir_bcsel(b, nir_uge(b, y, nir_imm_int(b, 32)),
                                 res_if_ge_32, res_if_lt_32));
}

static nir_ssa_def *
lower_ushr64(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   /* Implemented as
    *
    * uint64_t rshift(uint64_t x, int c)
    * {
    *    if (c == 0) return x;
    *
    *    uint32_t lo = LO(x), hi = HI(x);
    *
    *    if (c < 32) {
    *       uint32_t lo_shifted = lo >> c;
    *       uint32_t hi_shifted = hi >> c;
    *       uint32_t hi_shifted_lo = hi << abs(32 - c);
    *       return pack_64(hi_shifted, hi_shifted_lo | lo_shifted);
    *    } else {
    *       uint32_t hi_shifted_lo = hi >> abs(32 - c);
    *       return pack_64(0, hi_shifted_lo);
    *    }
    * }
    */

   nir_ssa_def *x_lo = nir_unpack_64_2x32_split_x(b, x);
   nir_ssa_def *x_hi = nir_unpack_64_2x32_split_y(b, x);

   nir_ssa_def *reverse_count = nir_iabs(b, nir_iadd(b, y, nir_imm_int(b, -32)));
   nir_ssa_def *lo_shifted = nir_ushr(b, x_lo, y);
   nir_ssa_def *hi_shifted = nir_ushr(b, x_hi, y);
   nir_ssa_def *hi_shifted_lo = nir_ishl(b, x_hi, reverse_count);

   nir_ssa_def *res_if_lt_32 =
      nir_pack_64_2x32_split(b, nir_ior(b, lo_shifted, hi_shifted_lo),
                                hi_shifted);
   nir_ssa_def *res_if_ge_32 =
      nir_pack_64_2x32_split(b, nir_ushr(b, x_hi, reverse_count),
                                nir_imm_int(b, 0));

   return nir_bcsel(b,
                    nir_ieq(b, y, nir_imm_int(b, 0)), x,
                    nir_bcsel(b, nir_uge(b, y, nir_imm_int(b, 32)),
                                 res_if_ge_32, res_if_lt_32));
}

static nir_ssa_def *
lower_iadd64(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   nir_ssa_def *x_lo = nir_unpack_64_2x32_split_x(b, x);
   nir_ssa_def *x_hi = nir_unpack_64_2x32_split_y(b, x);
   nir_ssa_def *y_lo = nir_unpack_64_2x32_split_x(b, y);
   nir_ssa_def *y_hi = nir_unpack_64_2x32_split_y(b, y);

   nir_ssa_def *res_lo = nir_iadd(b, x_lo, y_lo);
   nir_ssa_def *carry = nir_b2i32(b, nir_ult(b, res_lo, x_lo));
   nir_ssa_def *res_hi = nir_iadd(b, carry, nir_iadd(b, x_hi, y_hi));

   return nir_pack_64_2x32_split(b, res_lo, res_hi);
}

static nir_ssa_def *
lower_isub64(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   nir_ssa_def *x_lo = nir_unpack_64_2x32_split_x(b, x);
   nir_ssa_def *x_hi = nir_unpack_64_2x32_split_y(b, x);
   nir_ssa_def *y_lo = nir_unpack_64_2x32_split_x(b, y);
   nir_ssa_def *y_hi = nir_unpack_64_2x32_split_y(b, y);

   nir_ssa_def *res_lo = nir_isub(b, x_lo, y_lo);
   nir_ssa_def *borrow = nir_ineg(b, nir_b2i32(b, nir_ult(b, x_lo, y_lo)));
   nir_ssa_def *res_hi = nir_iadd(b, nir_isub(b, x_hi, y_hi), borrow);

   return nir_pack_64_2x32_split(b, res_lo, res_hi);
}

static nir_ssa_def *
lower_ineg64(nir_builder *b, nir_ssa_def *x)
{
   /* Since isub is the same number of instructions (with better dependencies)
    * as iadd, subtraction is actually more efficient for ineg than the usual
    * 2's complement "flip the bits and add one".
    */
   return lower_isub64(b, nir_imm_int64(b, 0), x);
}

static nir_ssa_def *
lower_iabs64(nir_builder *b, nir_ssa_def *x)
{
   nir_ssa_def *x_hi = nir_unpack_64_2x32_split_y(b, x);
   nir_ssa_def *x_is_neg = nir_ilt(b, x_hi, nir_imm_int(b, 0));
   return nir_bcsel(b, x_is_neg, nir_ineg(b, x), x);
}

static nir_ssa_def *
lower_int64_compare(nir_builder *b, nir_op op, nir_ssa_def *x, nir_ssa_def *y)
{
   nir_ssa_def *x_lo = nir_unpack_64_2x32_split_x(b, x);
   nir_ssa_def *x_hi = nir_unpack_64_2x32_split_y(b, x);
   nir_ssa_def *y_lo = nir_unpack_64_2x32_split_x(b, y);
   nir_ssa_def *y_hi = nir_unpack_64_2x32_split_y(b, y);

   switch (op) {
   case nir_op_ieq:
      return nir_iand(b, nir_ieq(b, x_hi, y_hi), nir_ieq(b, x_lo, y_lo));
   case nir_op_ine:
      return nir_ior(b, nir_ine(b, x_hi, y_hi), nir_ine(b, x_lo, y_lo));
   case nir_op_ult:
      return nir_ior(b, nir_ult(b, x_hi, y_hi),
                        nir_iand(b, nir_ieq(b, x_hi, y_hi),
                                    nir_ult(b, x_lo, y_lo)));
   case nir_op_ilt:
      return nir_ior(b, nir_ilt(b, x_hi, y_hi),
                        nir_iand(b, nir_ieq(b, x_hi, y_hi),
                                    nir_ult(b, x_lo, y_lo)));
      break;
   case nir_op_uge:
      /* Lower as !(x < y) in the hopes of better CSE */
      return nir_inot(b, lower_int64_compare(b, nir_op_ult, x, y));
   case nir_op_ige:
      /* Lower as !(x < y) in the hopes of better CSE */
      return nir_inot(b, lower_int64_compare(b, nir_op_ilt, x, y));
   default:
      unreachable("Invalid comparison");
   }
}

static nir_ssa_def *
lower_umax64(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   return nir_bcsel(b, lower_int64_compare(b, nir_op_ult, x, y), y, x);
}

static nir_ssa_def *
lower_imax64(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   return nir_bcsel(b, lower_int64_compare(b, nir_op_ilt, x, y), y, x);
}

static nir_ssa_def *
lower_umin64(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   return nir_bcsel(b, lower_int64_compare(b, nir_op_ult, x, y), x, y);
}

static nir_ssa_def *
lower_imin64(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y)
{
   return nir_bcsel(b, lower_int64_compare(b, nir_op_ilt, x, y), x, y);
}

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
lower_mul_high64(nir_builder *b, nir_ssa_def *x, nir_ssa_def *y,
                 bool sign_extend)
{
   nir_ssa_def *x32[4], *y32[4];
   x32[0] = nir_unpack_64_2x32_split_x(b, x);
   x32[1] = nir_unpack_64_2x32_split_y(b, x);
   if (sign_extend) {
      x32[2] = x32[3] = nir_ishr(b, x32[1], nir_imm_int(b, 31));
   } else {
      x32[2] = x32[3] = nir_imm_int(b, 0);
   }

   y32[0] = nir_unpack_64_2x32_split_x(b, y);
   y32[1] = nir_unpack_64_2x32_split_y(b, y);
   if (sign_extend) {
      y32[2] = y32[3] = nir_ishr(b, y32[1], nir_imm_int(b, 31));
   } else {
      y32[2] = y32[3] = nir_imm_int(b, 0);
   }

   nir_ssa_def *res[8] = { NULL, };

   /* Yes, the following generates a pile of code.  However, we throw res[0]
    * and res[1] away in the end and, if we're in the umul case, four of our
    * eight dword operands will be constant zero and opt_algebraic will clean
    * this up nicely.
    */
   for (unsigned i = 0; i < 4; i++) {
      nir_ssa_def *carry = NULL;
      for (unsigned j = 0; j < 4; j++) {
         /* The maximum values of x32[i] and y32[i] are UINT32_MAX so the
          * maximum value of tmp is UINT32_MAX * UINT32_MAX.  The maximum
          * value that will fit in tmp is
          *
          *    UINT64_MAX = UINT32_MAX << 32 + UINT32_MAX
          *               = UINT32_MAX * (UINT32_MAX + 1) + UINT32_MAX
          *               = UINT32_MAX * UINT32_MAX + 2 * UINT32_MAX
          *
          * so we're guaranteed that we can add in two more 32-bit values
          * without overflowing tmp.
          */
         nir_ssa_def *tmp =
            nir_pack_64_2x32_split(b, nir_imul(b, x32[i], y32[j]),
                                      nir_umul_high(b, x32[i], y32[j]));
         if (res[i + j])
            tmp = nir_iadd(b, tmp, nir_u2u64(b, res[i + j]));
         if (carry)
            tmp = nir_iadd(b, tmp, carry);
         res[i + j] = nir_u2u32(b, tmp);
         carry = nir_ushr(b, tmp, nir_imm_int(b, 32));
      }
      res[i + 4] = nir_u2u32(b, carry);
   }

   return nir_pack_64_2x32_split(b, res[2], res[3]);
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
   case nir_op_imul_high:
   case nir_op_umul_high:
      return nir_lower_imul_high64;
   case nir_op_isign:
      return nir_lower_isign64;
   case nir_op_udiv:
   case nir_op_idiv:
   case nir_op_umod:
   case nir_op_imod:
   case nir_op_irem:
      return nir_lower_divmod64;
   case nir_op_b2i64:
   case nir_op_i2b1:
   case nir_op_i2i32:
   case nir_op_i2i64:
   case nir_op_u2u32:
   case nir_op_u2u64:
   case nir_op_bcsel:
      return nir_lower_mov64;
   case nir_op_ieq:
   case nir_op_ine:
   case nir_op_ult:
   case nir_op_ilt:
   case nir_op_uge:
   case nir_op_ige:
      return nir_lower_icmp64;
   case nir_op_iadd:
   case nir_op_isub:
      return nir_lower_iadd64;
   case nir_op_imin:
   case nir_op_imax:
   case nir_op_umin:
   case nir_op_umax:
      return nir_lower_minmax64;
   case nir_op_iabs:
      return nir_lower_iabs64;
   case nir_op_ineg:
      return nir_lower_ineg64;
   case nir_op_iand:
   case nir_op_ior:
   case nir_op_ixor:
   case nir_op_inot:
      return nir_lower_logic64;
   case nir_op_ishl:
   case nir_op_ishr:
   case nir_op_ushr:
      return nir_lower_shift64;
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
   case nir_op_imul_high:
      return lower_mul_high64(b, src[0], src[1], true);
   case nir_op_umul_high:
      return lower_mul_high64(b, src[0], src[1], false);
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
   case nir_op_b2i64:
      return lower_b2i64(b, src[0]);
   case nir_op_i2b1:
      return lower_i2b(b, src[0]);
   case nir_op_i2i8:
      return lower_i2i8(b, src[0]);
   case nir_op_i2i16:
      return lower_i2i16(b, src[0]);
   case nir_op_i2i32:
      return lower_i2i32(b, src[0]);
   case nir_op_i2i64:
      return lower_i2i64(b, src[0]);
   case nir_op_u2u8:
      return lower_u2u8(b, src[0]);
   case nir_op_u2u16:
      return lower_u2u16(b, src[0]);
   case nir_op_u2u32:
      return lower_u2u32(b, src[0]);
   case nir_op_u2u64:
      return lower_u2u64(b, src[0]);
   case nir_op_bcsel:
      return lower_bcsel64(b, src[0], src[1], src[2]);
   case nir_op_ieq:
   case nir_op_ine:
   case nir_op_ult:
   case nir_op_ilt:
   case nir_op_uge:
   case nir_op_ige:
      return lower_int64_compare(b, alu->op, src[0], src[1]);
   case nir_op_iadd:
      return lower_iadd64(b, src[0], src[1]);
   case nir_op_isub:
      return lower_isub64(b, src[0], src[1]);
   case nir_op_imin:
      return lower_imin64(b, src[0], src[1]);
   case nir_op_imax:
      return lower_imax64(b, src[0], src[1]);
   case nir_op_umin:
      return lower_umin64(b, src[0], src[1]);
   case nir_op_umax:
      return lower_umax64(b, src[0], src[1]);
   case nir_op_iabs:
      return lower_iabs64(b, src[0]);
   case nir_op_ineg:
      return lower_ineg64(b, src[0]);
   case nir_op_iand:
      return lower_iand64(b, src[0], src[1]);
   case nir_op_ior:
      return lower_ior64(b, src[0], src[1]);
   case nir_op_ixor:
      return lower_ixor64(b, src[0], src[1]);
   case nir_op_inot:
      return lower_inot64(b, src[0]);
   case nir_op_ishl:
      return lower_ishl64(b, src[0], src[1]);
   case nir_op_ishr:
      return lower_ishr64(b, src[0], src[1]);
   case nir_op_ushr:
      return lower_ushr64(b, src[0], src[1]);
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
         switch (alu->op) {
         case nir_op_i2b1:
         case nir_op_i2i32:
         case nir_op_u2u32:
            assert(alu->src[0].src.is_ssa);
            if (alu->src[0].src.ssa->bit_size != 64)
               continue;
            break;
         case nir_op_bcsel:
            assert(alu->src[1].src.is_ssa);
            assert(alu->src[2].src.is_ssa);
            assert(alu->src[1].src.ssa->bit_size ==
                   alu->src[2].src.ssa->bit_size);
            if (alu->src[1].src.ssa->bit_size != 64)
               continue;
            break;
         case nir_op_ieq:
         case nir_op_ine:
         case nir_op_ult:
         case nir_op_ilt:
         case nir_op_uge:
         case nir_op_ige:
            assert(alu->src[0].src.is_ssa);
            assert(alu->src[1].src.is_ssa);
            assert(alu->src[0].src.ssa->bit_size ==
                   alu->src[1].src.ssa->bit_size);
            if (alu->src[0].src.ssa->bit_size != 64)
               continue;
            break;
         default:
            assert(alu->dest.dest.is_ssa);
            if (alu->dest.dest.ssa.bit_size != 64)
               continue;
            break;
         }

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

   if (progress) {
      nir_metadata_preserve(impl, nir_metadata_none);
   } else {
#ifndef NDEBUG
      impl->valid_metadata &= ~nir_metadata_not_properly_reset;
#endif
   }

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
