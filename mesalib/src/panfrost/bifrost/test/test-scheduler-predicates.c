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

#define TMP() bi_temp(b->shader)

int main(int argc, char **argv)
{
   unsigned nr_fail = 0, nr_pass = 0;
   void *ralloc_ctx = ralloc_context(NULL);
   bi_builder *b = bit_builder(ralloc_ctx);

   bi_instr *mov = bi_mov_i32_to(b, TMP(), TMP());
   BIT_ASSERT(bi_can_fma(mov));
   BIT_ASSERT(bi_can_add(mov));
   BIT_ASSERT(!bi_must_message(mov));
   BIT_ASSERT(bi_reads_zero(mov));
   BIT_ASSERT(bi_reads_temps(mov, 0));
   BIT_ASSERT(bi_reads_t(mov, 0));

   bi_instr *fma = bi_fma_f32_to(b, TMP(), TMP(), TMP(), bi_zero(), BI_ROUND_NONE);
   BIT_ASSERT(bi_can_fma(fma));
   BIT_ASSERT(!bi_can_add(fma));
   BIT_ASSERT(!bi_must_message(fma));
   BIT_ASSERT(bi_reads_zero(fma));
   for (unsigned i = 0; i < 3; ++i) {
      BIT_ASSERT(bi_reads_temps(fma, i));
      BIT_ASSERT(bi_reads_t(fma, i));
   }

   bi_instr *load = bi_load_i128_to(b, TMP(), TMP(), TMP(), BI_SEG_UBO);
   BIT_ASSERT(!bi_can_fma(load));
   BIT_ASSERT(bi_can_add(load));
   BIT_ASSERT(bi_must_message(load));
   for (unsigned i = 0; i < 2; ++i) {
      BIT_ASSERT(bi_reads_temps(load, i));
      BIT_ASSERT(bi_reads_t(load, i));
   }

   bi_instr *blend = bi_blend_to(b, TMP(), TMP(), TMP(), TMP(), TMP(), 4);
   BIT_ASSERT(!bi_can_fma(load));
   BIT_ASSERT(bi_can_add(load));
   BIT_ASSERT(bi_must_message(blend));
   for (unsigned i = 0; i < 4; ++i)
      BIT_ASSERT(bi_reads_temps(blend, i));
   BIT_ASSERT(!bi_reads_t(blend, 0));
   BIT_ASSERT(bi_reads_t(blend, 1));
   BIT_ASSERT(!bi_reads_t(blend, 2));
   BIT_ASSERT(!bi_reads_t(blend, 3));

   /* Test restrictions on modifiers of same cycle temporaries */
   bi_instr *fadd = bi_fadd_f32_to(b, TMP(), TMP(), TMP(), BI_ROUND_NONE);
   BIT_ASSERT(bi_reads_t(fadd, 0));

   for (unsigned i = 0; i < 2; ++i) {
      for (unsigned j = 0; j < 2; ++j) {
         bi_instr *fadd = bi_fadd_f32_to(b, TMP(), TMP(), TMP(), BI_ROUND_NONE);
         fadd->src[i] = bi_swz_16(TMP(), j, j);
         BIT_ASSERT(bi_reads_t(fadd, 1 - i));
         BIT_ASSERT(!bi_reads_t(fadd, i));
      }
   }

   ralloc_free(ralloc_ctx);
   TEST_END(nr_pass, nr_fail);
}
