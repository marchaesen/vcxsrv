/*
 * Copyright (C) 2021 Collabora, Ltd.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "compiler.h"
#include "bi_test.h"
#include "bi_builder.h"

#define CASE(instr, expected) do { \
   bi_builder *A = bit_builder(ralloc_ctx); \
   bi_builder *B = bit_builder(ralloc_ctx); \
   { \
      bi_builder *b = A; \
      instr; \
   } \
   { \
      bi_builder *b = B; \
      expected; \
   } \
   bi_opt_mod_prop_forward(A->shader); \
   bi_opt_mod_prop_backward(A->shader); \
   bi_opt_dead_code_eliminate(A->shader); \
   if (bit_shader_equal(A->shader, B->shader)) { \
      nr_pass++; \
   } else { \
      fprintf(stderr, "Got:\n"); \
      bi_print_shader(A->shader, stderr); \
      fprintf(stderr, "Expected:\n"); \
      bi_print_shader(B->shader, stderr); \
      fprintf(stderr, "\n"); \
      nr_fail++; \
   } \
} while(0)

#define NEGCASE(instr) CASE(instr, instr)

int
main(int argc, const char **argv)
{
   unsigned nr_fail = 0, nr_pass = 0;
   void *ralloc_ctx = ralloc_context(NULL);
   bi_index zero = bi_zero();
   bi_index reg = bi_register(0);
   bi_index x = bi_register(1);
   bi_index y = bi_register(2);
   bi_index negabsx = bi_neg(bi_abs(x));

   /* Check absneg is fused */

   CASE(bi_fadd_f32_to(b, reg, bi_fabsneg_f32(b, bi_abs(x)), y, BI_ROUND_NONE),
        bi_fadd_f32_to(b, reg, bi_abs(x), y, BI_ROUND_NONE));

   CASE(bi_fadd_f32_to(b, reg, bi_fabsneg_f32(b, bi_neg(x)), y, BI_ROUND_NONE),
        bi_fadd_f32_to(b, reg, bi_neg(x), y, BI_ROUND_NONE));

   CASE(bi_fadd_f32_to(b, reg, bi_fabsneg_f32(b, negabsx), y, BI_ROUND_NONE),
        bi_fadd_f32_to(b, reg, negabsx, y, BI_ROUND_NONE));

   CASE(bi_fadd_f32_to(b, reg, bi_fabsneg_f32(b, x), y, BI_ROUND_NONE),
        bi_fadd_f32_to(b, reg, x, y, BI_ROUND_NONE));

   /* Check absneg is fused on a variety of instructions */

   CASE(bi_fadd_f32_to(b, reg, bi_fabsneg_f32(b, negabsx), y, BI_ROUND_RTP),
        bi_fadd_f32_to(b, reg, negabsx, y, BI_ROUND_RTP));

   CASE(bi_fmin_f32_to(b, reg, bi_fabsneg_f32(b, negabsx), bi_neg(y)),
        bi_fmin_f32_to(b, reg, negabsx, bi_neg(y)));

   /* Check absneg is fused on fp16 */

   CASE(bi_fadd_v2f16_to(b, reg, bi_fabsneg_v2f16(b, negabsx), y, BI_ROUND_RTP),
        bi_fadd_v2f16_to(b, reg, negabsx, y, BI_ROUND_RTP));

   CASE(bi_fmin_v2f16_to(b, reg, bi_fabsneg_v2f16(b, negabsx), bi_neg(y)),
        bi_fmin_v2f16_to(b, reg, negabsx, bi_neg(y)));

   /* Check that swizzles are composed for fp16 */

   CASE(bi_fadd_v2f16_to(b, reg, bi_fabsneg_v2f16(b, bi_swz_16(negabsx, true, false)), y, BI_ROUND_RTP),
        bi_fadd_v2f16_to(b, reg, bi_swz_16(negabsx, true, false), y, BI_ROUND_RTP));

   CASE(bi_fadd_v2f16_to(b, reg, bi_swz_16(bi_fabsneg_v2f16(b, negabsx), true, false), y, BI_ROUND_RTP),
        bi_fadd_v2f16_to(b, reg, bi_swz_16(negabsx, true, false), y, BI_ROUND_RTP));

   CASE(bi_fadd_v2f16_to(b, reg, bi_swz_16(bi_fabsneg_v2f16(b, bi_swz_16(negabsx, true, false)), true, false), y, BI_ROUND_RTP),
        bi_fadd_v2f16_to(b, reg, negabsx, y, BI_ROUND_RTP));

   CASE(bi_fadd_v2f16_to(b, reg, bi_swz_16(bi_fabsneg_v2f16(b, bi_half(negabsx, false)), true, false), y, BI_ROUND_RTP),
        bi_fadd_v2f16_to(b, reg, bi_half(negabsx, false), y, BI_ROUND_RTP));

   CASE(bi_fadd_v2f16_to(b, reg, bi_swz_16(bi_fabsneg_v2f16(b, bi_half(negabsx, true)), true, false), y, BI_ROUND_RTP),
        bi_fadd_v2f16_to(b, reg, bi_half(negabsx, true), y, BI_ROUND_RTP));

   /* Check that widens are passed through */

   CASE(bi_fadd_f32_to(b, reg, bi_fabsneg_f32(b, bi_half(negabsx, false)), y, BI_ROUND_NONE),
        bi_fadd_f32_to(b, reg, bi_half(negabsx, false), y, BI_ROUND_NONE));

   CASE(bi_fadd_f32_to(b, reg, bi_fabsneg_f32(b, bi_half(negabsx, true)), y, BI_ROUND_NONE),
        bi_fadd_f32_to(b, reg, bi_half(negabsx, true), y, BI_ROUND_NONE));

   CASE(bi_fadd_f32_to(b, reg, bi_fabsneg_f32(b, bi_half(x, true)), bi_fabsneg_f32(b, bi_half(x, false)), BI_ROUND_NONE),
        bi_fadd_f32_to(b, reg, bi_half(x, true), bi_half(x, false), BI_ROUND_NONE));

   /* Refuse to mix sizes for fabsneg, that's wrong */

   NEGCASE(bi_fadd_f32_to(b, reg, bi_fabsneg_v2f16(b, negabsx), y, BI_ROUND_NONE));
   NEGCASE(bi_fadd_v2f16_to(b, reg, bi_fabsneg_f32(b, negabsx), y, BI_ROUND_NONE));

   /* It's tempting to use addition by 0.0 as the absneg primitive, but that
    * has footguns around signed zero and round modes. Check we don't
    * incorrectly fuse these rules. */

   NEGCASE(bi_fadd_f32_to(b, reg, bi_fadd_f32(b, bi_abs(x), zero, BI_ROUND_NONE), y, BI_ROUND_NONE));
   NEGCASE(bi_fadd_f32_to(b, reg, bi_fadd_f32(b, bi_neg(x), zero, BI_ROUND_NONE), y, BI_ROUND_NONE));
   NEGCASE(bi_fadd_f32_to(b, reg, bi_fadd_f32(b, bi_neg(bi_abs(x)), zero, BI_ROUND_NONE), y, BI_ROUND_NONE));
   NEGCASE(bi_fadd_f32_to(b, reg, bi_fadd_f32(b, x, zero, BI_ROUND_NONE), y, BI_ROUND_NONE));

   /* Check clamps are propagated */
   CASE({
      bi_instr *I = bi_fclamp_f32_to(b, reg, bi_fadd_f32(b, x, y, BI_ROUND_NONE));
      I->clamp = BI_CLAMP_CLAMP_0_INF;
   }, {
      bi_instr *I = bi_fadd_f32_to(b, reg, x, y, BI_ROUND_NONE);
      I->clamp = BI_CLAMP_CLAMP_0_INF;
   });

   CASE({
      bi_instr *I = bi_fclamp_v2f16_to(b, reg, bi_fadd_v2f16(b, x, y, BI_ROUND_NONE));
      I->clamp = BI_CLAMP_CLAMP_0_1;
   }, {
      bi_instr *I = bi_fadd_v2f16_to(b, reg, x, y, BI_ROUND_NONE);
      I->clamp = BI_CLAMP_CLAMP_0_1;
   });

   /* Check clamps are composed */
   CASE({
      bi_instr *I = bi_fadd_f32_to(b, bi_temp(b->shader), x, y, BI_ROUND_NONE);
      bi_instr *J = bi_fclamp_f32_to(b, reg, I->dest[0]);
      I->clamp = BI_CLAMP_CLAMP_M1_1;
      J->clamp = BI_CLAMP_CLAMP_0_INF;
   }, {
      bi_instr *I = bi_fadd_f32_to(b, reg, x, y, BI_ROUND_NONE);
      I->clamp = BI_CLAMP_CLAMP_0_1;
   });

   CASE({
      bi_instr *I = bi_fadd_f32_to(b, bi_temp(b->shader), x, y, BI_ROUND_NONE);
      bi_instr *J = bi_fclamp_f32_to(b, reg, I->dest[0]);
      I->clamp = BI_CLAMP_CLAMP_0_1;
      J->clamp = BI_CLAMP_CLAMP_0_INF;
   }, {
      bi_instr *I = bi_fadd_f32_to(b, reg, x, y, BI_ROUND_NONE);
      I->clamp = BI_CLAMP_CLAMP_0_1;
   });

   CASE({
      bi_instr *I = bi_fadd_f32_to(b, bi_temp(b->shader), x, y, BI_ROUND_NONE);
      bi_instr *J = bi_fclamp_f32_to(b, reg, I->dest[0]);
      I->clamp = BI_CLAMP_CLAMP_0_INF;
      J->clamp = BI_CLAMP_CLAMP_0_INF;
   }, {
      bi_instr *I = bi_fadd_f32_to(b, reg, x, y, BI_ROUND_NONE);
      I->clamp = BI_CLAMP_CLAMP_0_INF;
   });

   CASE({
      bi_instr *I = bi_fadd_v2f16_to(b, bi_temp(b->shader), x, y, BI_ROUND_NONE);
      bi_instr *J = bi_fclamp_v2f16_to(b, reg, I->dest[0]);
      I->clamp = BI_CLAMP_CLAMP_M1_1;
      J->clamp = BI_CLAMP_CLAMP_0_INF;
   }, {
      bi_instr *I = bi_fadd_v2f16_to(b, reg, x, y, BI_ROUND_NONE);
      I->clamp = BI_CLAMP_CLAMP_0_1;
   });

   CASE({
      bi_instr *I = bi_fadd_v2f16_to(b, bi_temp(b->shader), x, y, BI_ROUND_NONE);
      bi_instr *J = bi_fclamp_v2f16_to(b, reg, I->dest[0]);
      I->clamp = BI_CLAMP_CLAMP_0_1;
      J->clamp = BI_CLAMP_CLAMP_0_INF;
   }, {
      bi_instr *I = bi_fadd_v2f16_to(b, reg, x, y, BI_ROUND_NONE);
      I->clamp = BI_CLAMP_CLAMP_0_1;
   });

   CASE({
      bi_instr *I = bi_fadd_v2f16_to(b, bi_temp(b->shader), x, y, BI_ROUND_NONE);
      bi_instr *J = bi_fclamp_v2f16_to(b, reg, I->dest[0]);
      I->clamp = BI_CLAMP_CLAMP_0_INF;
      J->clamp = BI_CLAMP_CLAMP_0_INF;
   }, {
      bi_instr *I = bi_fadd_v2f16_to(b, reg, x, y, BI_ROUND_NONE);
      I->clamp = BI_CLAMP_CLAMP_0_INF;
   });

   /* We can't mix sizes */

   NEGCASE({
      bi_instr *I = bi_fclamp_f32_to(b, reg, bi_fadd_v2f16(b, x, y, BI_ROUND_NONE));
      I->clamp = BI_CLAMP_CLAMP_0_1;
   });

   NEGCASE({
      bi_instr *I = bi_fclamp_v2f16_to(b, reg, bi_fadd_f32(b, x, y, BI_ROUND_NONE));
      I->clamp = BI_CLAMP_CLAMP_0_1;
   });

   /* We can't use addition by 0.0 for clamps due to signed zeros. */
   NEGCASE({
      bi_instr *I = bi_fadd_f32_to(b, reg, bi_fadd_f32(b, x, y, BI_ROUND_NONE), zero, BI_ROUND_NONE);
      I->clamp = BI_CLAMP_CLAMP_M1_1;
   });

   NEGCASE({
      bi_instr *I = bi_fadd_v2f16_to(b, reg, bi_fadd_v2f16(b, x, y, BI_ROUND_NONE), zero, BI_ROUND_NONE);
      I->clamp = BI_CLAMP_CLAMP_0_1;
   });

   /* Check that we fuse comparisons with DISCARD */

   CASE(bi_discard_b32(b, bi_fcmp_f32(b, x, y, BI_CMPF_LE, BI_RESULT_TYPE_F1)),
        bi_discard_f32(b, x, y, BI_CMPF_LE));

   CASE(bi_discard_b32(b, bi_fcmp_f32(b, x, y, BI_CMPF_NE, BI_RESULT_TYPE_I1)),
        bi_discard_f32(b, x, y, BI_CMPF_NE));

   CASE(bi_discard_b32(b, bi_fcmp_f32(b, x, y, BI_CMPF_EQ, BI_RESULT_TYPE_M1)),
        bi_discard_f32(b, x, y, BI_CMPF_EQ));

   for (unsigned h = 0; h < 2; ++h) {
      CASE(bi_discard_b32(b, bi_half(bi_fcmp_v2f16(b, x, y, BI_CMPF_LE, BI_RESULT_TYPE_F1), h)),
           bi_discard_f32(b, bi_half(x, h), bi_half(y, h), BI_CMPF_LE));

      CASE(bi_discard_b32(b, bi_half(bi_fcmp_v2f16(b, x, y, BI_CMPF_NE, BI_RESULT_TYPE_I1), h)),
           bi_discard_f32(b, bi_half(x, h), bi_half(y, h), BI_CMPF_NE));

      CASE(bi_discard_b32(b, bi_half(bi_fcmp_v2f16(b, x, y, BI_CMPF_EQ, BI_RESULT_TYPE_M1), h)),
           bi_discard_f32(b, bi_half(x, h), bi_half(y, h), BI_CMPF_EQ));
   }

   /* Refuse to fuse special comparisons */
   NEGCASE(bi_discard_b32(b, bi_fcmp_f32(b, x, y, BI_CMPF_GTLT, BI_RESULT_TYPE_F1)));
   NEGCASE(bi_discard_b32(b, bi_fcmp_f32(b, x, y, BI_CMPF_TOTAL, BI_RESULT_TYPE_F1)));

   ralloc_free(ralloc_ctx);
   TEST_END(nr_pass, nr_fail);
}
