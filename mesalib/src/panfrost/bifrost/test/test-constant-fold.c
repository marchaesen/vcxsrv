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
   bool _unsupported = false; \
   uint32_t _value = bi_fold_constant(instr, &_unsupported); \
   if (_unsupported) { \
      fprintf(stderr, "Constant folding failed:\n"); \
      bi_print_instr(instr, stderr); \
      fprintf(stderr, "\n"); \
   } else if (_value == (expected)) { \
      nr_pass++; \
   } else { \
      fprintf(stderr, "Got %" PRIx32 ", expected %" PRIx32 "\n", _value, expected); \
      bi_print_instr(instr, stderr); \
      fprintf(stderr, "\n"); \
      nr_fail++; \
   } \
} while(0)

#define NEGCASE(instr) do { \
   bool _unsupported = false; \
   bi_fold_constant(instr, &_unsupported); \
   if (_unsupported) { \
      nr_pass++; \
   } else { \
      fprintf(stderr, "Should not have constant folded:\n"); \
      bi_print_instr(instr, stderr); \
      fprintf(stderr, "\n"); \
      nr_fail++; \
   } \
} while(0)

int
main(int argc, const char **argv)
{
   unsigned nr_fail = 0, nr_pass = 0;
   void *ralloc_ctx = ralloc_context(NULL);
   bi_builder *b = bit_builder(ralloc_ctx);
   bi_index zero = bi_fau(BIR_FAU_IMMEDIATE | 0, false);
   bi_index reg = bi_register(0);

   /* Swizzles should be constant folded */
   CASE(bi_swz_v2i16_to(b, reg, bi_imm_u32(0xCAFEBABE)), 0xCAFEBABE);
   CASE(bi_swz_v2i16_to(b, reg, bi_swz_16(bi_imm_u32(0xCAFEBABE), false, false)),
        0xBABEBABE);
   CASE(bi_swz_v2i16_to(b, reg, bi_swz_16(bi_imm_u32(0xCAFEBABE), true, false)),
        0xBABECAFE);
   CASE(bi_swz_v2i16_to(b, reg, bi_swz_16(bi_imm_u32(0xCAFEBABE), true, true)),
        0xCAFECAFE);

   /* Vector constructions should be constant folded */
   CASE(bi_mkvec_v2i16_to(b, reg, bi_imm_u16(0xCAFE), bi_imm_u16(0xBABE)), 0xBABECAFE);
   CASE(bi_mkvec_v2i16_to(b, reg, bi_swz_16(bi_imm_u32(0xCAFEBABE), true, true),
         bi_imm_u16(0xBABE)), 0xBABECAFE);
   CASE(bi_mkvec_v2i16_to(b, reg, bi_swz_16(bi_imm_u32(0xCAFEBABE), true, true),
         bi_swz_16(bi_imm_u32(0xCAFEBABE), false, false)), 0xBABECAFE);

   {
      bi_index u32 = bi_imm_u32(0xCAFEBABE);

      bi_index a = bi_byte(u32, 0); /* 0xBE */
      bi_index c = bi_byte(u32, 2); /* 0xFE */

      CASE(bi_mkvec_v4i8_to(b, reg, a, a, a, a), 0xBEBEBEBE);
      CASE(bi_mkvec_v4i8_to(b, reg, a, c, a, c), 0xFEBEFEBE);
      CASE(bi_mkvec_v4i8_to(b, reg, c, a, c, a), 0xBEFEBEFE);
      CASE(bi_mkvec_v4i8_to(b, reg, c, c, c, c), 0xFEFEFEFE);
   }

   /* Limited shifts required for texturing */
   CASE(bi_lshift_or_i32_to(b, reg, bi_imm_u32(0xCAFE), bi_imm_u32(0xA0000), bi_imm_u8(4)), (0xCAFE << 4) | 0xA0000);
   NEGCASE(bi_lshift_or_i32_to(b, reg, bi_imm_u32(0xCAFE), bi_not(bi_imm_u32(0xA0000)), bi_imm_u8(4)));
   NEGCASE(bi_lshift_or_i32_to(b, reg, bi_not(bi_imm_u32(0xCAFE)), bi_imm_u32(0xA0000), bi_imm_u8(4)));
   {
      bi_instr *I = bi_lshift_or_i32_to(b, reg, bi_imm_u32(0xCAFE), bi_imm_u32(0xA0000), bi_imm_u8(4));
      I->not_result = true;
      NEGCASE(I);
   }

   /* Limited rounding needed for texturing */
   CASE(bi_f32_to_u32_to(b, reg, bi_imm_f32(15.0), BI_ROUND_NONE), 15);
   CASE(bi_f32_to_u32_to(b, reg, bi_imm_f32(15.9), BI_ROUND_NONE), 15);
   CASE(bi_f32_to_u32_to(b, reg, bi_imm_f32(-20.4), BI_ROUND_NONE), 0);
   NEGCASE(bi_f32_to_u32_to(b, reg, bi_imm_f32(-20.4), BI_ROUND_RTP));
   NEGCASE(bi_f32_to_u32_to(b, reg, bi_imm_f32(-20.4), BI_ROUND_RTZ));

   /* Instructions with non-constant sources cannot be constant folded */
   NEGCASE(bi_swz_v2i16_to(b, reg, bi_temp(b->shader)));
   NEGCASE(bi_mkvec_v2i16_to(b, reg, bi_temp(b->shader), bi_temp(b->shader)));
   NEGCASE(bi_mkvec_v2i16_to(b, reg, bi_temp(b->shader), bi_imm_u32(0xDEADBEEF)));
   NEGCASE(bi_mkvec_v2i16_to(b, reg, bi_imm_u32(0xDEADBEEF), bi_temp(b->shader)));

   /* Other operations should not be constant folded */
   NEGCASE(bi_fma_f32_to(b, reg, zero, zero, zero, BI_ROUND_NONE));
   NEGCASE(bi_fadd_f32_to(b, reg, zero, zero, BI_ROUND_NONE));

   ralloc_free(ralloc_ctx);
   TEST_END(nr_pass, nr_fail);
}
