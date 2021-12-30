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

#include <gtest/gtest.h>

#define CASE(instr, expected) do { \
   bi_builder *A = bit_builder(mem_ctx); \
   bi_builder *B = bit_builder(mem_ctx); \
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
   if (!bit_shader_equal(A->shader, B->shader)) { \
      ADD_FAILURE(); \
      fprintf(stderr, "Optimization produce unexpected result"); \
      fprintf(stderr, "  Actual:\n"); \
      bi_print_shader(A->shader, stderr); \
      fprintf(stderr, "Expected:\n"); \
      bi_print_shader(B->shader, stderr); \
      fprintf(stderr, "\n"); \
   } \
} while(0)

#define NEGCASE(instr) CASE(instr, instr)

class Optimizer : public testing::Test {
protected:
   Optimizer() {
      mem_ctx = ralloc_context(NULL);

      reg     = bi_register(0);
      x       = bi_register(1);
      y       = bi_register(2);
      negabsx = bi_neg(bi_abs(x));
   }

   ~Optimizer() {
      ralloc_free(mem_ctx);
   }

   void *mem_ctx;

   bi_index reg;
   bi_index x;
   bi_index y;
   bi_index negabsx;
};

TEST_F(Optimizer, FusedFABSNEG)
{
   CASE(bi_fadd_f32_to(b, reg, bi_fabsneg_f32(b, bi_abs(x)), y, BI_ROUND_NONE),
        bi_fadd_f32_to(b, reg, bi_abs(x), y, BI_ROUND_NONE));

   CASE(bi_fadd_f32_to(b, reg, bi_fabsneg_f32(b, bi_neg(x)), y, BI_ROUND_NONE),
        bi_fadd_f32_to(b, reg, bi_neg(x), y, BI_ROUND_NONE));

   CASE(bi_fadd_f32_to(b, reg, bi_fabsneg_f32(b, negabsx), y, BI_ROUND_NONE),
        bi_fadd_f32_to(b, reg, negabsx, y, BI_ROUND_NONE));

   CASE(bi_fadd_f32_to(b, reg, bi_fabsneg_f32(b, x), y, BI_ROUND_NONE),
        bi_fadd_f32_to(b, reg, x, y, BI_ROUND_NONE));

   CASE(bi_fadd_f32_to(b, reg, bi_fabsneg_f32(b, negabsx), y, BI_ROUND_RTP),
        bi_fadd_f32_to(b, reg, negabsx, y, BI_ROUND_RTP));

   CASE(bi_fmin_f32_to(b, reg, bi_fabsneg_f32(b, negabsx), bi_neg(y)),
        bi_fmin_f32_to(b, reg, negabsx, bi_neg(y)));
}

TEST_F(Optimizer, FusedFABSNEGForFP16)
{
   CASE(bi_fadd_v2f16_to(b, reg, bi_fabsneg_v2f16(b, negabsx), y, BI_ROUND_RTP),
        bi_fadd_v2f16_to(b, reg, negabsx, y, BI_ROUND_RTP));

   CASE(bi_fmin_v2f16_to(b, reg, bi_fabsneg_v2f16(b, negabsx), bi_neg(y)),
        bi_fmin_v2f16_to(b, reg, negabsx, bi_neg(y)));
}

TEST_F(Optimizer, SwizzlesComposedForFP16)
{
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
}

TEST_F(Optimizer, PreserveWidens)
{
   /* Check that widens are passed through */
   CASE(bi_fadd_f32_to(b, reg, bi_fabsneg_f32(b, bi_half(negabsx, false)), y, BI_ROUND_NONE),
        bi_fadd_f32_to(b, reg, bi_half(negabsx, false), y, BI_ROUND_NONE));

   CASE(bi_fadd_f32_to(b, reg, bi_fabsneg_f32(b, bi_half(negabsx, true)), y, BI_ROUND_NONE),
        bi_fadd_f32_to(b, reg, bi_half(negabsx, true), y, BI_ROUND_NONE));

   CASE(bi_fadd_f32_to(b, reg, bi_fabsneg_f32(b, bi_half(x, true)), bi_fabsneg_f32(b, bi_half(x, false)), BI_ROUND_NONE),
        bi_fadd_f32_to(b, reg, bi_half(x, true), bi_half(x, false), BI_ROUND_NONE));
}

TEST_F(Optimizer, DoNotMixSizesForFABSNEG)
{
   /* Refuse to mix sizes for fabsneg, that's wrong */
   NEGCASE(bi_fadd_f32_to(b, reg, bi_fabsneg_v2f16(b, negabsx), y, BI_ROUND_NONE));
   NEGCASE(bi_fadd_v2f16_to(b, reg, bi_fabsneg_f32(b, negabsx), y, BI_ROUND_NONE));
}

TEST_F(Optimizer, AvoidZeroAndFABSNEGFootguns)
{
   /* It's tempting to use addition by 0.0 as the absneg primitive, but that
    * has footguns around signed zero and round modes. Check we don't
    * incorrectly fuse these rules. */

   bi_index zero = bi_zero();

   NEGCASE(bi_fadd_f32_to(b, reg, bi_fadd_f32(b, bi_abs(x), zero, BI_ROUND_NONE), y, BI_ROUND_NONE));
   NEGCASE(bi_fadd_f32_to(b, reg, bi_fadd_f32(b, bi_neg(x), zero, BI_ROUND_NONE), y, BI_ROUND_NONE));
   NEGCASE(bi_fadd_f32_to(b, reg, bi_fadd_f32(b, bi_neg(bi_abs(x)), zero, BI_ROUND_NONE), y, BI_ROUND_NONE));
   NEGCASE(bi_fadd_f32_to(b, reg, bi_fadd_f32(b, x, zero, BI_ROUND_NONE), y, BI_ROUND_NONE));
}

TEST_F(Optimizer, ClampsPropagated)
{
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
}


TEST_F(Optimizer, ClampsComposed)
{
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
}

TEST_F(Optimizer, DoNotMixSizesWhenClamping)
{
   NEGCASE({
      bi_instr *I = bi_fclamp_f32_to(b, reg, bi_fadd_v2f16(b, x, y, BI_ROUND_NONE));
      I->clamp = BI_CLAMP_CLAMP_0_1;
   });

   NEGCASE({
      bi_instr *I = bi_fclamp_v2f16_to(b, reg, bi_fadd_f32(b, x, y, BI_ROUND_NONE));
      I->clamp = BI_CLAMP_CLAMP_0_1;
   });
}

TEST_F(Optimizer, DoNotUseAdditionByZeroForClamps)
{
   bi_index zero = bi_zero();

   /* We can't use addition by 0.0 for clamps due to signed zeros. */
   NEGCASE({
      bi_instr *I = bi_fadd_f32_to(b, reg, bi_fadd_f32(b, x, y, BI_ROUND_NONE), zero, BI_ROUND_NONE);
      I->clamp = BI_CLAMP_CLAMP_M1_1;
   });

   NEGCASE({
      bi_instr *I = bi_fadd_v2f16_to(b, reg, bi_fadd_v2f16(b, x, y, BI_ROUND_NONE), zero, BI_ROUND_NONE);
      I->clamp = BI_CLAMP_CLAMP_0_1;
   });
}

TEST_F(Optimizer, FuseComparisonsWithDISCARD)
{
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
}

TEST_F(Optimizer, DoNotFuseSpecialComparisons)
{
   NEGCASE(bi_discard_b32(b, bi_fcmp_f32(b, x, y, BI_CMPF_GTLT, BI_RESULT_TYPE_F1)));
   NEGCASE(bi_discard_b32(b, bi_fcmp_f32(b, x, y, BI_CMPF_TOTAL, BI_RESULT_TYPE_F1)));
}
