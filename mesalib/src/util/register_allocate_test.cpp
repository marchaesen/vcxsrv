/*
 * Copyright © 2021 Google LLC
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

#include <gtest/gtest.h>
#include "ralloc.h"
#include "register_allocate.h"
#include "register_allocate_internal.h"

class ra_test : public ::testing::Test {
public:
   void *mem_ctx;

protected:
   ra_test();
   ~ra_test();
};

ra_test::ra_test()
{
   mem_ctx = ralloc_context(NULL);
}

ra_test::~ra_test()
{
   ralloc_free(mem_ctx);
}

void
thumb_checks(struct ra_regs *regs, unsigned reg32_base, unsigned reg64_base)
{
   struct ra_class *reg32low = ra_get_class_from_index(regs, 0);
   struct ra_class *reg64low = ra_get_class_from_index(regs, 1);
   struct ra_class *reg96 = ra_get_class_from_index(regs, 2);

   /* Table 4.1 */
   ASSERT_EQ(reg32low->p, 8);
   ASSERT_EQ(reg32low->q[reg32low->index], 1);
   ASSERT_EQ(reg32low->q[reg64low->index], 2);
   ASSERT_EQ(reg32low->q[reg96->index], 3);
   ASSERT_EQ(reg64low->p, 8);
   ASSERT_EQ(reg64low->q[reg32low->index], 2);
   ASSERT_EQ(reg64low->q[reg64low->index], 3);
   ASSERT_EQ(reg64low->q[reg96->index], 4);
   ASSERT_EQ(reg96->p, 2);
   ASSERT_EQ(reg96->q[reg96->index], 2);
   ASSERT_EQ(reg96->q[reg64low->index], 2);
   ASSERT_EQ(reg96->q[reg96->index], 2);

   /* These individual regs should conflict with themselves, but nothing else from their class */
   for (int i = 0; i < 7; i++) {
      ASSERT_FALSE(ra_class_allocations_conflict(reg32low, reg32_base + i, reg32low, reg32_base + i + 1));
      ASSERT_TRUE(ra_class_allocations_conflict(reg32low, reg32_base + i, reg32low, reg32_base + i));
   }

   /* Check that reg64low conflicts with the pairs of reg32low but not neighbors */
   ASSERT_TRUE(ra_class_allocations_conflict(reg64low, reg64_base + 0, reg32low, reg32_base + 0));
   ASSERT_TRUE(ra_class_allocations_conflict(reg64low, reg64_base + 0, reg32low, reg32_base + 1));
   ASSERT_FALSE(ra_class_allocations_conflict(reg64low, reg64_base + 0, reg32low, reg32_base + 2));

   ASSERT_FALSE(ra_class_allocations_conflict(reg64low, reg64_base + 1, reg32low, reg32_base + 0));
   ASSERT_TRUE(ra_class_allocations_conflict(reg64low, reg64_base + 1, reg32low, reg32_base + 1));
   ASSERT_TRUE(ra_class_allocations_conflict(reg64low, reg64_base + 1, reg32low, reg32_base + 2));
   ASSERT_FALSE(ra_class_allocations_conflict(reg64low, reg64_base + 1, reg32low, reg32_base + 3));
}

TEST_F(ra_test, thumb)
{
   struct ra_regs *regs = ra_alloc_reg_set(mem_ctx, 100, true);

   /* r0..15 are the real HW registers. */
   int next_vreg = 16;

   /* reg32low is any of the low 8 registers. */
   unsigned int reg32_base = next_vreg;
   struct ra_class *reg32low = ra_alloc_reg_class(regs);
   for (int i = 0; i < 8; i++) {
      int vreg = next_vreg++;
      ra_class_add_reg(reg32low, vreg);
      ra_add_transitive_reg_conflict(regs, i, vreg);
   }

   /* reg64low is pairs of the low 8 registers (with wraparound!) */
   unsigned int reg64_base = next_vreg;
   struct ra_class *reg64low = ra_alloc_reg_class(regs);
   for (int i = 0; i < 8; i++) {
      int vreg = next_vreg++;
      ra_class_add_reg(reg64low, vreg);
      ra_add_transitive_reg_conflict(regs, i, vreg);
      ra_add_transitive_reg_conflict(regs, (i + 1) % 8, vreg);
   }

   /* reg96 is one of either r[0..2] or r[1..3] */
   struct ra_class *reg96 = ra_alloc_reg_class(regs);
   for (int i = 0; i < 2; i++) {
      int vreg = next_vreg++;
      ra_class_add_reg(reg96, vreg);
      for (int j = 0; j < 3; j++)
         ra_add_transitive_reg_conflict(regs, i + j, vreg);
   }

   ra_set_finalize(regs, NULL);

   thumb_checks(regs, reg32_base, reg64_base);
}

TEST_F(ra_test, thumb_contigregs)
{
   struct ra_regs *regs = ra_alloc_reg_set(mem_ctx, 16, true);

   /* reg32low is any of the low 8 registers. */
   struct ra_class *reg32low = ra_alloc_contig_reg_class(regs, 1);
   for (int i = 0; i < 8; i++)
      ra_class_add_reg(reg32low, i);

   /* reg64low is pairs of the low 8 registers (we're ignoring the wraparound thing here) */
   struct ra_class *reg64low = ra_alloc_contig_reg_class(regs, 2);
   for (int i = 0; i < 8; i++)
      ra_class_add_reg(reg64low, i);

   /* reg96 is one of either r[0..2] or r[1..3] */
   struct ra_class *reg96 = ra_alloc_contig_reg_class(regs, 3);
   for (int i = 0; i < 2; i++)
      ra_class_add_reg(reg96, i);

   ra_set_finalize(regs, NULL);

   thumb_checks(regs, 0, 0);
}

TEST_F(ra_test, nonintersect_contigregs)
{
   struct ra_regs *regs = ra_alloc_reg_set(mem_ctx, 16, true);

   struct ra_class *low = ra_alloc_contig_reg_class(regs, 1);
   for (int i = 0; i < 8; i++)
      ra_class_add_reg(low, i);

   struct ra_class *high = ra_alloc_contig_reg_class(regs, 1);
   for (int i = 8; i < 16; i++)
      ra_class_add_reg(high, i);

   ra_set_finalize(regs, NULL);

   ASSERT_EQ(low->q[low->index], 1);
   ASSERT_EQ(low->q[high->index], 0);
   ASSERT_EQ(high->q[low->index], 0);
   ASSERT_EQ(high->q[high->index], 1);
}

TEST_F(ra_test, aligned_contigregs)
{
   int base_regs = 32;
   struct ra_regs *regs = ra_alloc_reg_set(mem_ctx, base_regs, true);

   struct ra_class *c1 = ra_alloc_contig_reg_class(regs, 1);
   for (int i = 0; i < base_regs; i++)
      ra_class_add_reg(c1, i);

   struct ra_class *c2 = ra_alloc_contig_reg_class(regs, 2);
   for (int i = 8; i < base_regs; i += 2)
      ra_class_add_reg(c2, i);

   struct ra_class *c4 = ra_alloc_contig_reg_class(regs, 4);
   for (int i = 8; i < base_regs; i += 4)
      ra_class_add_reg(c4, i);

   ra_set_finalize(regs, NULL);

   ASSERT_EQ(c1->q[c1->index], 1);
   ASSERT_EQ(c1->q[c2->index], 2);
   ASSERT_EQ(c1->q[c4->index], 4);
   ASSERT_EQ(c2->q[c1->index], 1);
   ASSERT_EQ(c2->q[c2->index], 1);
   ASSERT_EQ(c2->q[c4->index], 2);
   ASSERT_EQ(c4->q[c1->index], 1);
   ASSERT_EQ(c4->q[c2->index], 1);
   ASSERT_EQ(c4->q[c4->index], 1);

   /* Check conflicts for a c4 allocation at i against other classes. */
   for (int i = 0; i < base_regs / 4; i += 4) {
      for (int j = 0; j < base_regs; j++) {
         ASSERT_EQ(ra_class_allocations_conflict(c4, i, c1, j),
                   j >= i && j < i + 4);
      }

      for (int j = 0; j < base_regs; j += 2) {
         ASSERT_EQ(ra_class_allocations_conflict(c4, i, c2, j),
                   j >= i && j < i + 4);
      }
   }
}
