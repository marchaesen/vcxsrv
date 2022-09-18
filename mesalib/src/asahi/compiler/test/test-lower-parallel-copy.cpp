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

#include "agx_test.h"

#include <gtest/gtest.h>

#define CASE(copies, expected) do { \
   agx_builder *A = agx_test_builder(mem_ctx); \
   agx_builder *B = agx_test_builder(mem_ctx); \
   \
   agx_emit_parallel_copies(A, copies, ARRAY_SIZE(copies)); \
   \
   { \
      agx_builder *b = B; \
      expected; \
   } \
   \
   ASSERT_SHADER_EQUAL(A->shader, B->shader); \
} while(0)

static inline void
xor_swap(agx_builder *b, agx_index x, agx_index y)
{
   agx_xor_to(b, x, x, y);
   agx_xor_to(b, y, x, y);
   agx_xor_to(b, x, x, y);
}

class LowerParallelCopy : public testing::Test {
protected:
   LowerParallelCopy() {
      mem_ctx = ralloc_context(NULL);
   }

   ~LowerParallelCopy() {
      ralloc_free(mem_ctx);
   }

   void *mem_ctx;
};

TEST_F(LowerParallelCopy, UnrelatedCopies) {
   struct agx_copy test_1[] = {
        { .dest = 0, .src = 2, .size = AGX_SIZE_32 },
        { .dest = 4, .src = 6, .size = AGX_SIZE_32 },
   };

   CASE(test_1, {
        agx_mov_to(b, agx_register(0, AGX_SIZE_32), agx_register(2, AGX_SIZE_32));
        agx_mov_to(b, agx_register(4, AGX_SIZE_32), agx_register(6, AGX_SIZE_32));
   });

   struct agx_copy test_2[] = {
        { .dest = 0, .src = 1, .size = AGX_SIZE_16 },
        { .dest = 4, .src = 5, .size = AGX_SIZE_16 },
   };

   CASE(test_2, {
        agx_mov_to(b, agx_register(0, AGX_SIZE_16), agx_register(1, AGX_SIZE_16));
        agx_mov_to(b, agx_register(4, AGX_SIZE_16), agx_register(5, AGX_SIZE_16));
   });
}

TEST_F(LowerParallelCopy, RelatedSource)
{
   struct agx_copy test_1[] = {
        { .dest = 0, .src = 2, .size = AGX_SIZE_32 },
        { .dest = 4, .src = 2, .size = AGX_SIZE_32 },
   };

   CASE(test_1, {
        agx_mov_to(b, agx_register(0, AGX_SIZE_32), agx_register(2, AGX_SIZE_32));
        agx_mov_to(b, agx_register(4, AGX_SIZE_32), agx_register(2, AGX_SIZE_32));
   });

   struct agx_copy test_2[] = {
        { .dest = 0, .src = 1, .size = AGX_SIZE_16 },
        { .dest = 4, .src = 1, .size = AGX_SIZE_16 },
   };

   CASE(test_2, {
        agx_mov_to(b, agx_register(0, AGX_SIZE_16), agx_register(1, AGX_SIZE_16));
        agx_mov_to(b, agx_register(4, AGX_SIZE_16), agx_register(1, AGX_SIZE_16));
   });
}

TEST_F(LowerParallelCopy, DependentCopies)
{
   struct agx_copy test_1[] = {
        { .dest = 0, .src = 2, .size = AGX_SIZE_32 },
        { .dest = 4, .src = 0, .size = AGX_SIZE_32 },
   };

   CASE(test_1, {
        agx_mov_to(b, agx_register(4, AGX_SIZE_32), agx_register(0, AGX_SIZE_32));
        agx_mov_to(b, agx_register(0, AGX_SIZE_32), agx_register(2, AGX_SIZE_32));
   });

   struct agx_copy test_2[] = {
        { .dest = 0, .src = 1, .size = AGX_SIZE_16 },
        { .dest = 4, .src = 0, .size = AGX_SIZE_16 },
   };

   CASE(test_2, {
        agx_mov_to(b, agx_register(4, AGX_SIZE_16), agx_register(0, AGX_SIZE_16));
        agx_mov_to(b, agx_register(0, AGX_SIZE_16), agx_register(1, AGX_SIZE_16));
   });
}

TEST_F(LowerParallelCopy, ManyDependentCopies)
{
   struct agx_copy test_1[] = {
        { .dest = 0, .src = 2, .size = AGX_SIZE_32 },
        { .dest = 4, .src = 0, .size = AGX_SIZE_32 },
        { .dest = 8, .src = 6, .size = AGX_SIZE_32 },
        { .dest = 6, .src = 4, .size = AGX_SIZE_32 },
   };

   CASE(test_1, {
        agx_mov_to(b, agx_register(8, AGX_SIZE_32), agx_register(6, AGX_SIZE_32));
        agx_mov_to(b, agx_register(6, AGX_SIZE_32), agx_register(4, AGX_SIZE_32));
        agx_mov_to(b, agx_register(4, AGX_SIZE_32), agx_register(0, AGX_SIZE_32));
        agx_mov_to(b, agx_register(0, AGX_SIZE_32), agx_register(2, AGX_SIZE_32));
   });

   struct agx_copy test_2[] = {
        { .dest = 0, .src = 1, .size = AGX_SIZE_16 },
        { .dest = 2, .src = 0, .size = AGX_SIZE_16 },
        { .dest = 4, .src = 3, .size = AGX_SIZE_16 },
        { .dest = 3, .src = 2, .size = AGX_SIZE_16 },
   };

   CASE(test_2, {
        agx_mov_to(b, agx_register(4, AGX_SIZE_16), agx_register(3, AGX_SIZE_16));
        agx_mov_to(b, agx_register(3, AGX_SIZE_16), agx_register(2, AGX_SIZE_16));
        agx_mov_to(b, agx_register(2, AGX_SIZE_16), agx_register(0, AGX_SIZE_16));
        agx_mov_to(b, agx_register(0, AGX_SIZE_16), agx_register(1, AGX_SIZE_16));
   });
}

TEST_F(LowerParallelCopy, Swap) {
   struct agx_copy test_1[] = {
        { .dest = 0, .src = 2, .size = AGX_SIZE_32 },
        { .dest = 2, .src = 0, .size = AGX_SIZE_32 },
   };

   CASE(test_1, {
        xor_swap(b, agx_register(0, AGX_SIZE_32), agx_register(2, AGX_SIZE_32));
   });

   struct agx_copy test_2[] = {
        { .dest = 0, .src = 1, .size = AGX_SIZE_16 },
        { .dest = 1, .src = 0, .size = AGX_SIZE_16 },
   };

   CASE(test_2, {
        xor_swap(b, agx_register(0, AGX_SIZE_16), agx_register(1, AGX_SIZE_16));
   });
}

TEST_F(LowerParallelCopy, Cycle3) {
   struct agx_copy test[] = {
        { .dest = 0, .src = 1, .size = AGX_SIZE_16 },
        { .dest = 1, .src = 2, .size = AGX_SIZE_16 },
        { .dest = 2, .src = 0, .size = AGX_SIZE_16 },
   };

   /* XXX: requires 6 instructions. if we had a temp free, could do it in 4 */
   CASE(test, {
        xor_swap(b, agx_register(0, AGX_SIZE_16), agx_register(1, AGX_SIZE_16));
        xor_swap(b, agx_register(1, AGX_SIZE_16), agx_register(2, AGX_SIZE_16));
   });
}

/* Test case from Hack et al */
TEST_F(LowerParallelCopy, TwoSwaps) {
   struct agx_copy test[] = {
        { .dest = 4, .src = 2, .size = AGX_SIZE_32 },
        { .dest = 6, .src = 4, .size = AGX_SIZE_32 },
        { .dest = 2, .src = 6, .size = AGX_SIZE_32 },
        { .dest = 8, .src = 8, .size = AGX_SIZE_32 },
   };

   CASE(test, {
        xor_swap(b, agx_register(4, AGX_SIZE_32), agx_register(2, AGX_SIZE_32));
        xor_swap(b, agx_register(6, AGX_SIZE_32), agx_register(2, AGX_SIZE_32));
   });
}

#if 0
TEST_F(LowerParallelCopy, LooksLikeASwap) {
   struct agx_copy test[] = {
        { .dest = 0, .src = 2, .size = AGX_SIZE_32 },
        { .dest = 2, .src = 0, .size = AGX_SIZE_32 },
        { .dest = 4, .src = 2, .size = AGX_SIZE_32 },
   };

   CASE(test, {
         agx_mov_to(b, agx_register(4, AGX_SIZE_32), agx_register(2, AGX_SIZE_32));
         agx_mov_to(b, agx_register(2, AGX_SIZE_32), agx_register(0, AGX_SIZE_32));
         agx_mov_to(b, agx_register(0, AGX_SIZE_32), agx_register(4, AGX_SIZE_32));
   });
}
#endif
