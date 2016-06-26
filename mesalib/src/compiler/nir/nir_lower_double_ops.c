/*
 * Copyright © 2015 Intel Corporation
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
 */

#include "nir.h"
#include "nir_builder.h"
#include "c99_math.h"

/*
 * Lowers some unsupported double operations, using only:
 *
 * - pack/unpackDouble2x32
 * - conversion to/from single-precision
 * - double add, mul, and fma
 * - conditional select
 * - 32-bit integer and floating point arithmetic
 */

/* Creates a double with the exponent bits set to a given integer value */
static nir_ssa_def *
set_exponent(nir_builder *b, nir_ssa_def *src, nir_ssa_def *exp)
{
   /* Split into bits 0-31 and 32-63 */
   nir_ssa_def *lo = nir_unpack_double_2x32_split_x(b, src);
   nir_ssa_def *hi = nir_unpack_double_2x32_split_y(b, src);

   /* The exponent is bits 52-62, or 20-30 of the high word, so set the exponent
    * to 1023
    */
   nir_ssa_def *new_hi = nir_bfi(b, nir_imm_int(b, 0x7ff00000), exp, hi);
   /* recombine */
   return nir_pack_double_2x32_split(b, lo, new_hi);
}

static nir_ssa_def *
get_exponent(nir_builder *b, nir_ssa_def *src)
{
   /* get bits 32-63 */
   nir_ssa_def *hi = nir_unpack_double_2x32_split_y(b, src);

   /* extract bits 20-30 of the high word */
   return nir_ubitfield_extract(b, hi, nir_imm_int(b, 20), nir_imm_int(b, 11));
}

/* Return infinity with the sign of the given source which is +/-0 */

static nir_ssa_def *
get_signed_inf(nir_builder *b, nir_ssa_def *zero)
{
   nir_ssa_def *zero_hi = nir_unpack_double_2x32_split_y(b, zero);

   /* The bit pattern for infinity is 0x7ff0000000000000, where the sign bit
    * is the highest bit. Only the sign bit can be non-zero in the passed in
    * source. So we essentially need to OR the infinity and the zero, except
    * the low 32 bits are always 0 so we can construct the correct high 32
    * bits and then pack it together with zero low 32 bits.
    */
   nir_ssa_def *inf_hi = nir_ior(b, nir_imm_int(b, 0x7ff00000), zero_hi);
   return nir_pack_double_2x32_split(b, nir_imm_int(b, 0), inf_hi);
}

/*
 * Generates the correctly-signed infinity if the source was zero, and flushes
 * the result to 0 if the source was infinity or the calculated exponent was
 * too small to be representable.
 */

static nir_ssa_def *
fix_inv_result(nir_builder *b, nir_ssa_def *res, nir_ssa_def *src,
               nir_ssa_def *exp)
{
   /* If the exponent is too small or the original input was infinity/NaN,
    * force the result to 0 (flush denorms) to avoid the work of handling
    * denorms properly. Note that this doesn't preserve positive/negative
    * zeros, but GLSL doesn't require it.
    */
   res = nir_bcsel(b, nir_ior(b, nir_ige(b, nir_imm_int(b, 0), exp),
                              nir_feq(b, nir_fabs(b, src),
                                      nir_imm_double(b, INFINITY))),
                   nir_imm_double(b, 0.0f), res);

   /* If the original input was 0, generate the correctly-signed infinity */
   res = nir_bcsel(b, nir_fne(b, src, nir_imm_double(b, 0.0f)),
                   res, get_signed_inf(b, src));

   return res;

}

static nir_ssa_def *
lower_rcp(nir_builder *b, nir_ssa_def *src)
{
   /* normalize the input to avoid range issues */
   nir_ssa_def *src_norm = set_exponent(b, src, nir_imm_int(b, 1023));

   /* cast to float, do an rcp, and then cast back to get an approximate
    * result
    */
   nir_ssa_def *ra = nir_f2d(b, nir_frcp(b, nir_d2f(b, src_norm)));

   /* Fixup the exponent of the result - note that we check if this is too
    * small below.
    */
   nir_ssa_def *new_exp = nir_isub(b, get_exponent(b, ra),
                                   nir_isub(b, get_exponent(b, src),
                                            nir_imm_int(b, 1023)));

   ra = set_exponent(b, ra, new_exp);

   /* Do a few Newton-Raphson steps to improve precision.
    *
    * Each step doubles the precision, and we started off with around 24 bits,
    * so we only need to do 2 steps to get to full precision. The step is:
    *
    * x_new = x * (2 - x*src)
    *
    * But we can re-arrange this to improve precision by using another fused
    * multiply-add:
    *
    * x_new = x + x * (1 - x*src)
    *
    * See https://en.wikipedia.org/wiki/Division_algorithm for more details.
    */

   ra = nir_ffma(b, ra, nir_ffma(b, ra, src, nir_imm_double(b, -1)), ra);
   ra = nir_ffma(b, ra, nir_ffma(b, ra, src, nir_imm_double(b, -1)), ra);

   return fix_inv_result(b, ra, src, new_exp);
}

static nir_ssa_def *
lower_sqrt_rsq(nir_builder *b, nir_ssa_def *src, bool sqrt)
{
   /* We want to compute:
    *
    * 1/sqrt(m * 2^e)
    *
    * When the exponent is even, this is equivalent to:
    *
    * 1/sqrt(m) * 2^(-e/2)
    *
    * and then the exponent is odd, this is equal to:
    *
    * 1/sqrt(m * 2) * 2^(-(e - 1)/2)
    *
    * where the m * 2 is absorbed into the exponent. So we want the exponent
    * inside the square root to be 1 if e is odd and 0 if e is even, and we
    * want to subtract off e/2 from the final exponent, rounded to negative
    * infinity. We can do the former by first computing the unbiased exponent,
    * and then AND'ing it with 1 to get 0 or 1, and we can do the latter by
    * shifting right by 1.
    */

   nir_ssa_def *unbiased_exp = nir_isub(b, get_exponent(b, src),
                                        nir_imm_int(b, 1023));
   nir_ssa_def *even = nir_iand(b, unbiased_exp, nir_imm_int(b, 1));
   nir_ssa_def *half = nir_ishr(b, unbiased_exp, nir_imm_int(b, 1));

   nir_ssa_def *src_norm = set_exponent(b, src,
                                        nir_iadd(b, nir_imm_int(b, 1023),
                                                 even));

   nir_ssa_def *ra = nir_f2d(b, nir_frsq(b, nir_d2f(b, src_norm)));
   nir_ssa_def *new_exp = nir_isub(b, get_exponent(b, ra), half);
   ra = set_exponent(b, ra, new_exp);

   /*
    * The following implements an iterative algorithm that's very similar
    * between sqrt and rsqrt. We start with an iteration of Goldschmit's
    * algorithm, which looks like:
    *
    * a = the source
    * y_0 = initial (single-precision) rsqrt estimate
    *
    * h_0 = .5 * y_0
    * g_0 = a * y_0
    * r_0 = .5 - h_0 * g_0
    * g_1 = g_0 * r_0 + g_0
    * h_1 = h_0 * r_0 + h_0
    *
    * Now g_1 ~= sqrt(a), and h_1 ~= 1/(2 * sqrt(a)). We could continue
    * applying another round of Goldschmit, but since we would never refer
    * back to a (the original source), we would add too much rounding error.
    * So instead, we do one last round of Newton-Raphson, which has better
    * rounding characteristics, to get the final rounding correct. This is
    * split into two cases:
    *
    * 1. sqrt
    *
    * Normally, doing a round of Newton-Raphson for sqrt involves taking a
    * reciprocal of the original estimate, which is slow since it isn't
    * supported in HW. But we can take advantage of the fact that we already
    * computed a good estimate of 1/(2 * g_1) by rearranging it like so:
    *
    * g_2 = .5 * (g_1 + a / g_1)
    *     = g_1 + .5 * (a / g_1 - g_1)
    *     = g_1 + (.5 / g_1) * (a - g_1^2)
    *     = g_1 + h_1 * (a - g_1^2)
    *
    * The second term represents the error, and by splitting it out we can get
    * better precision by computing it as part of a fused multiply-add. Since
    * both Newton-Raphson and Goldschmit approximately double the precision of
    * the result, these two steps should be enough.
    *
    * 2. rsqrt
    *
    * First off, note that the first round of the Goldschmit algorithm is
    * really just a Newton-Raphson step in disguise:
    *
    * h_1 = h_0 * (.5 - h_0 * g_0) + h_0
    *     = h_0 * (1.5 - h_0 * g_0)
    *     = h_0 * (1.5 - .5 * a * y_0^2)
    *     = (.5 * y_0) * (1.5 - .5 * a * y_0^2)
    *
    * which is the standard formula multiplied by .5. Unlike in the sqrt case,
    * we don't need the inverse to do a Newton-Raphson step; we just need h_1,
    * so we can skip the calculation of g_1. Instead, we simply do another
    * Newton-Raphson step:
    *
    * y_1 = 2 * h_1
    * r_1 = .5 - h_1 * y_1 * a
    * y_2 = y_1 * r_1 + y_1
    *
    * Where the difference from Goldschmit is that we calculate y_1 * a
    * instead of using g_1. Doing it this way should be as fast as computing
    * y_1 up front instead of h_1, and it lets us share the code for the
    * initial Goldschmit step with the sqrt case.
    *
    * Putting it together, the computations are:
    *
    * h_0 = .5 * y_0
    * g_0 = a * y_0
    * r_0 = .5 - h_0 * g_0
    * h_1 = h_0 * r_0 + h_0
    * if sqrt:
    *    g_1 = g_0 * r_0 + g_0
    *    r_1 = a - g_1 * g_1
    *    g_2 = h_1 * r_1 + g_1
    * else:
    *    y_1 = 2 * h_1
    *    r_1 = .5 - y_1 * (h_1 * a)
    *    y_2 = y_1 * r_1 + y_1
    *
    * For more on the ideas behind this, see "Software Division and Square
    * Root Using Goldschmit's Algorithms" by Markstein and the Wikipedia page
    * on square roots
    * (https://en.wikipedia.org/wiki/Methods_of_computing_square_roots).
    */

   nir_ssa_def *one_half = nir_imm_double(b, 0.5);
   nir_ssa_def *h_0 = nir_fmul(b, one_half, ra);
   nir_ssa_def *g_0 = nir_fmul(b, src, ra);
   nir_ssa_def *r_0 = nir_ffma(b, nir_fneg(b, h_0), g_0, one_half);
   nir_ssa_def *h_1 = nir_ffma(b, h_0, r_0, h_0);
   nir_ssa_def *res;
   if (sqrt) {
      nir_ssa_def *g_1 = nir_ffma(b, g_0, r_0, g_0);
      nir_ssa_def *r_1 = nir_ffma(b, nir_fneg(b, g_1), g_1, src);
      res = nir_ffma(b, h_1, r_1, g_1);
   } else {
      nir_ssa_def *y_1 = nir_fmul(b, nir_imm_double(b, 2.0), h_1);
      nir_ssa_def *r_1 = nir_ffma(b, nir_fneg(b, y_1), nir_fmul(b, h_1, src),
                                  one_half);
      res = nir_ffma(b, y_1, r_1, y_1);
   }

   if (sqrt) {
      /* Here, the special cases we need to handle are
       * 0 -> 0 and
       * +inf -> +inf
       */
      res = nir_bcsel(b, nir_ior(b, nir_feq(b, src, nir_imm_double(b, 0.0)),
                                 nir_feq(b, src, nir_imm_double(b, INFINITY))),
                                 src, res);
   } else {
      res = fix_inv_result(b, res, src, new_exp);
   }

   return res;
}

static nir_ssa_def *
lower_trunc(nir_builder *b, nir_ssa_def *src)
{
   nir_ssa_def *unbiased_exp = nir_isub(b, get_exponent(b, src),
                                        nir_imm_int(b, 1023));

   nir_ssa_def *frac_bits = nir_isub(b, nir_imm_int(b, 52), unbiased_exp);

   /*
    * Decide the operation to apply depending on the unbiased exponent:
    *
    * if (unbiased_exp < 0)
    *    return 0
    * else if (unbiased_exp > 52)
    *    return src
    * else
    *    return src & (~0 << frac_bits)
    *
    * Notice that the else branch is a 64-bit integer operation that we need
    * to implement in terms of 32-bit integer arithmetics (at least until we
    * support 64-bit integer arithmetics).
    */

   /* Compute "~0 << frac_bits" in terms of hi/lo 32-bit integer math */
   nir_ssa_def *mask_lo =
      nir_bcsel(b,
                nir_ige(b, frac_bits, nir_imm_int(b, 32)),
                nir_imm_int(b, 0),
                nir_ishl(b, nir_imm_int(b, ~0), frac_bits));

   nir_ssa_def *mask_hi =
      nir_bcsel(b,
                nir_ilt(b, frac_bits, nir_imm_int(b, 33)),
                nir_imm_int(b, ~0),
                nir_ishl(b,
                         nir_imm_int(b, ~0),
                         nir_isub(b, frac_bits, nir_imm_int(b, 32))));

   nir_ssa_def *src_lo = nir_unpack_double_2x32_split_x(b, src);
   nir_ssa_def *src_hi = nir_unpack_double_2x32_split_y(b, src);

   return
      nir_bcsel(b,
                nir_ilt(b, unbiased_exp, nir_imm_int(b, 0)),
                nir_imm_double(b, 0.0),
                nir_bcsel(b, nir_ige(b, unbiased_exp, nir_imm_int(b, 53)),
                          src,
                          nir_pack_double_2x32_split(b,
                                                     nir_iand(b, mask_lo, src_lo),
                                                     nir_iand(b, mask_hi, src_hi))));
}

static nir_ssa_def *
lower_floor(nir_builder *b, nir_ssa_def *src)
{
   /*
    * For x >= 0, floor(x) = trunc(x)
    * For x < 0,
    *    - if x is integer, floor(x) = x
    *    - otherwise, floor(x) = trunc(x) - 1
    */
   nir_ssa_def *tr = nir_ftrunc(b, src);
   nir_ssa_def *positive = nir_fge(b, src, nir_imm_double(b, 0.0));
   return nir_bcsel(b,
                    nir_ior(b, positive, nir_feq(b, src, tr)),
                    tr,
                    nir_fsub(b, tr, nir_imm_double(b, 1.0)));
}

static nir_ssa_def *
lower_ceil(nir_builder *b, nir_ssa_def *src)
{
   /* if x < 0,                    ceil(x) = trunc(x)
    * else if (x - trunc(x) == 0), ceil(x) = x
    * else,                        ceil(x) = trunc(x) + 1
    */
   nir_ssa_def *tr = nir_ftrunc(b, src);
   nir_ssa_def *negative = nir_flt(b, src, nir_imm_double(b, 0.0));
   return nir_bcsel(b,
                    nir_ior(b, negative, nir_feq(b, src, tr)),
                    tr,
                    nir_fadd(b, tr, nir_imm_double(b, 1.0)));
}

static nir_ssa_def *
lower_fract(nir_builder *b, nir_ssa_def *src)
{
   return nir_fsub(b, src, nir_ffloor(b, src));
}

static nir_ssa_def *
lower_round_even(nir_builder *b, nir_ssa_def *src)
{
   /* If fract(src) == 0.5, then we will have to decide the rounding direction.
    * We will do this by computing the mod(abs(src), 2) and testing if it
    * is < 1 or not.
    *
    * We compute mod(abs(src), 2) as:
    * abs(src) - 2.0 * floor(abs(src) / 2.0)
    */
   nir_ssa_def *two = nir_imm_double(b, 2.0);
   nir_ssa_def *abs_src = nir_fabs(b, src);
   nir_ssa_def *mod =
      nir_fsub(b,
               abs_src,
               nir_fmul(b,
                        two,
                        nir_ffloor(b,
                                   nir_fmul(b,
                                            abs_src,
                                            nir_imm_double(b, 0.5)))));

   /*
    * If fract(src) != 0.5, then we round as floor(src + 0.5)
    *
    * If fract(src) == 0.5, then we have to check the modulo:
    *
    *   if it is < 1 we need a trunc operation so we get:
    *      0.5 -> 0,   -0.5 -> -0
    *      2.5 -> 2,   -2.5 -> -2
    *
    *   otherwise we need to check if src >= 0, in which case we need to round
    *   upwards, or not, in which case we need to round downwards so we get:
    *      1.5 -> 2,   -1.5 -> -2
    *      3.5 -> 4,   -3.5 -> -4
    */
   nir_ssa_def *fract = nir_ffract(b, src);
   return nir_bcsel(b,
                    nir_fne(b, fract, nir_imm_double(b, 0.5)),
                    nir_ffloor(b, nir_fadd(b, src, nir_imm_double(b, 0.5))),
                    nir_bcsel(b,
                              nir_flt(b, mod, nir_imm_double(b, 1.0)),
                              nir_ftrunc(b, src),
                              nir_bcsel(b,
                                        nir_fge(b, src, nir_imm_double(b, 0.0)),
                                        nir_fadd(b, src, nir_imm_double(b, 0.5)),
                                        nir_fsub(b, src, nir_imm_double(b, 0.5)))));
}

static nir_ssa_def *
lower_mod(nir_builder *b, nir_ssa_def *src0, nir_ssa_def *src1)
{
   /* mod(x,y) = x - y * floor(x/y)
    *
    * If the division is lowered, it could add some rounding errors that make
    * floor() to return the quotient minus one when x = N * y. If this is the
    * case, we return zero because mod(x, y) output value is [0, y).
    */
   nir_ssa_def *floor = nir_ffloor(b, nir_fdiv(b, src0, src1));
   nir_ssa_def *mod = nir_fsub(b, src0, nir_fmul(b, src1, floor));

   return nir_bcsel(b,
                    nir_fne(b, mod, src1),
                    mod,
                    nir_imm_double(b, 0.0));
}

static void
lower_doubles_instr(nir_alu_instr *instr, nir_lower_doubles_options options)
{
   assert(instr->dest.dest.is_ssa);
   if (instr->dest.dest.ssa.bit_size != 64)
      return;

   switch (instr->op) {
   case nir_op_frcp:
      if (!(options & nir_lower_drcp))
         return;
      break;

   case nir_op_fsqrt:
      if (!(options & nir_lower_dsqrt))
         return;
      break;

   case nir_op_frsq:
      if (!(options & nir_lower_drsq))
         return;
      break;

   case nir_op_ftrunc:
      if (!(options & nir_lower_dtrunc))
         return;
      break;

   case nir_op_ffloor:
      if (!(options & nir_lower_dfloor))
         return;
      break;

   case nir_op_fceil:
      if (!(options & nir_lower_dceil))
         return;
      break;

   case nir_op_ffract:
      if (!(options & nir_lower_dfract))
         return;
      break;

   case nir_op_fround_even:
      if (!(options & nir_lower_dround_even))
         return;
      break;

   case nir_op_fmod:
      if (!(options & nir_lower_dmod))
         return;
      break;

   default:
      return;
   }

   nir_builder bld;
   nir_builder_init(&bld, nir_cf_node_get_function(&instr->instr.block->cf_node));
   bld.cursor = nir_before_instr(&instr->instr);

   nir_ssa_def *src = nir_fmov_alu(&bld, instr->src[0],
                                   instr->dest.dest.ssa.num_components);

   nir_ssa_def *result;

   switch (instr->op) {
   case nir_op_frcp:
      result = lower_rcp(&bld, src);
      break;
   case nir_op_fsqrt:
      result = lower_sqrt_rsq(&bld, src, true);
      break;
   case nir_op_frsq:
      result = lower_sqrt_rsq(&bld, src, false);
      break;
   case nir_op_ftrunc:
      result = lower_trunc(&bld, src);
      break;
   case nir_op_ffloor:
      result = lower_floor(&bld, src);
      break;
   case nir_op_fceil:
      result = lower_ceil(&bld, src);
      break;
   case nir_op_ffract:
      result = lower_fract(&bld, src);
      break;
   case nir_op_fround_even:
      result = lower_round_even(&bld, src);
      break;

   case nir_op_fmod: {
      nir_ssa_def *src1 = nir_fmov_alu(&bld, instr->src[1],
                                      instr->dest.dest.ssa.num_components);
      result = lower_mod(&bld, src, src1);
   }
      break;
   default:
      unreachable("unhandled opcode");
   }

   nir_ssa_def_rewrite_uses(&instr->dest.dest.ssa, nir_src_for_ssa(result));
   nir_instr_remove(&instr->instr);
}

void
nir_lower_doubles(nir_shader *shader, nir_lower_doubles_options options)
{
   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;

      nir_foreach_block(block, function->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type == nir_instr_type_alu)
               lower_doubles_instr(nir_instr_as_alu(instr), options);
         }
      }
   }
}
