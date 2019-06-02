/*
 * Copyright © 2019 Red Hat
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
#include "util/bitset.h"

TEST(bitset, sizes)
{
   EXPECT_EQ(sizeof(BITSET_WORD), 4);

   BITSET_DECLARE(mask32, 32);
   BITSET_DECLARE(mask64, 64);
   BITSET_DECLARE(mask128, 128);

   EXPECT_EQ(sizeof(mask32), 4);
   EXPECT_EQ(sizeof(mask64), 8);
   EXPECT_EQ(sizeof(mask128), 16);
}

TEST(bitset, testsetclear)
{
   BITSET_DECLARE(mask128, 128);
   BITSET_ZERO(mask128);

   for (int i = 0; i < 128; i++) {
      EXPECT_EQ(BITSET_TEST(mask128, i), false);
      BITSET_SET(mask128, i);
      EXPECT_EQ(BITSET_TEST(mask128, i), true);
      BITSET_CLEAR(mask128, i);
      EXPECT_EQ(BITSET_TEST(mask128, i), false);
   }
}

TEST(bitset, testsetones)
{
   BITSET_DECLARE(mask128, 128);
   BITSET_ONES(mask128);

   EXPECT_EQ(BITSET_FFS(mask128), 1);

   for (int i = 0; i < 128; i++) {
      EXPECT_EQ(BITSET_TEST(mask128, i), true);
      BITSET_CLEAR(mask128, i);
      EXPECT_EQ(BITSET_TEST(mask128, i), false);
      BITSET_SET(mask128, i);
      EXPECT_EQ(BITSET_TEST(mask128, i), true);
   }
}

TEST(bitset, testbasicrange)
{
   BITSET_DECLARE(mask128, 128);
   BITSET_ZERO(mask128);

   const int max_set = 15;
   BITSET_SET_RANGE(mask128, 0, max_set);
   EXPECT_EQ(BITSET_TEST_RANGE(mask128, 0, max_set), true);
   EXPECT_EQ(BITSET_TEST_RANGE(mask128, max_set + 1, max_set + 15), false);
   for (int i = 0; i < 128; i++) {
      if (i <= max_set)
         EXPECT_EQ(BITSET_TEST(mask128, i), true);
      else
         EXPECT_EQ(BITSET_TEST(mask128, i), false);
   }
   BITSET_CLEAR_RANGE(mask128, 0, max_set);
   EXPECT_EQ(BITSET_TEST_RANGE(mask128, 0, max_set), false);
   for (int i = 0; i < 128; i++) {
      EXPECT_EQ(BITSET_TEST(mask128, i), false);
   }
}

TEST(bitset, testbitsetffs)
{
   BITSET_DECLARE(mask128, 128);
   BITSET_ZERO(mask128);

   EXPECT_EQ(BITSET_FFS(mask128), 0);

   BITSET_SET(mask128, 14);
   EXPECT_EQ(BITSET_FFS(mask128), 15);

   BITSET_SET(mask128, 28);
   EXPECT_EQ(BITSET_FFS(mask128), 15);

   BITSET_CLEAR(mask128, 14);
   EXPECT_EQ(BITSET_FFS(mask128), 29);

   BITSET_SET_RANGE(mask128, 14, 18);
   EXPECT_EQ(BITSET_FFS(mask128), 15);
}

TEST(bitset, testrangebits)
{
   BITSET_DECLARE(mask128, 128);
   BITSET_ZERO(mask128);

   BITSET_SET_RANGE(mask128, 0, 31);
   BITSET_SET_RANGE(mask128, 32, 63);
   BITSET_SET_RANGE(mask128, 64, 95);
   BITSET_SET_RANGE(mask128, 96, 127);

   EXPECT_EQ(BITSET_TEST_RANGE(mask128, 0, 31), true);
   EXPECT_EQ(BITSET_TEST_RANGE(mask128, 32, 63), true);
   EXPECT_EQ(BITSET_TEST_RANGE(mask128, 64, 95), true);
   EXPECT_EQ(BITSET_TEST_RANGE(mask128, 96, 127), true);
   for (int i = 0; i < 128; i++) {
      EXPECT_EQ(BITSET_TEST(mask128, i), true);
   }
}
