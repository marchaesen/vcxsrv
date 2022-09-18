/*
 * Copyright (C) 2021-2022 Alyssa Rosenzweig
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

#include "agx_test.h"

#include <gtest/gtest.h>

TEST(Minifloat, RepresentativeValues)
{
   EXPECT_EQ(agx_minifloat_decode(0), 0.0f);
   EXPECT_EQ(agx_minifloat_decode(25), 0.390625f);
   EXPECT_EQ(agx_minifloat_decode(135), -0.109375f);
   EXPECT_EQ(agx_minifloat_decode(255), -31.0);
}

TEST(Minifloat, Exactness)
{
   EXPECT_TRUE(agx_minifloat_exact(0.0f));
   EXPECT_TRUE(agx_minifloat_exact(0.390625f));
   EXPECT_TRUE(agx_minifloat_exact(-0.109375f));
   EXPECT_TRUE(agx_minifloat_exact(-31.0));
   EXPECT_FALSE(agx_minifloat_exact(3.141f));
   EXPECT_FALSE(agx_minifloat_exact(2.718f));
   EXPECT_FALSE(agx_minifloat_exact(1.618f));
}

TEST(Minifloat, AllValuesRoundtrip)
{
   for (unsigned i = 0; i < 0x100; ++i) {
      float f = agx_minifloat_decode(i);
      EXPECT_EQ(agx_minifloat_encode(f), i);
      EXPECT_TRUE(agx_minifloat_exact(f));
   }
}
