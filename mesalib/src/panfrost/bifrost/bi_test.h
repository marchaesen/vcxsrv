/*
 * Copyright (C) 2020-2021 Collabora Ltd.
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
 *
 * Authors (Collabora):
 *      Alyssa Rosenzweig <alyssa.rosenzweig@collabora.com>
 */

#ifndef __BI_TEST_H
#define __BI_TEST_H

#include <stdio.h>
#include <inttypes.h>
#include "compiler.h"

/* Helpers for unit testing */
#define TEST_END(nr_pass, nr_fail) \
   printf("Passed %u/%u tests.\n", nr_pass, nr_pass + nr_fail); \
   return nr_fail ? 1 : 0;

/* Helper to generate a bi_builder suitable for creating test instructions */
static inline bi_builder *
bit_builder(void *memctx)
{
        bi_context *ctx = rzalloc(memctx, bi_context);
        list_inithead(&ctx->blocks);

        bi_block *blk = rzalloc(ctx, bi_block);

        blk->predecessors = _mesa_set_create(blk,
                        _mesa_hash_pointer,
                        _mesa_key_pointer_equal);

        list_addtail(&blk->link, &ctx->blocks);
        list_inithead(&blk->instructions);

        bi_builder *b = rzalloc(memctx, bi_builder);
        b->shader = ctx;
        b->cursor = bi_after_block(blk);
        return b;
}

/* Helper to compare for logical equality of instructions. Need to skip over
 * the link, guaranteed to be first. After that we can compare raw data. */
static inline bool
bit_instr_equal(bi_instr *A, bi_instr *B)
{
   return memcmp((uint8_t *) A    + sizeof(struct list_head),
                 (uint8_t *) B    + sizeof(struct list_head),
                 sizeof(bi_instr) - sizeof(struct list_head)) == 0;
}

static inline bool
bit_block_equal(bi_block *A, bi_block *B)
{
   if (list_length(&A->instructions) != list_length(&B->instructions))
      return false;

   list_pair_for_each_entry(bi_instr, insA, insB,
                            &A->instructions, &B->instructions, link) {
      if (!bit_instr_equal(insA, insB))
         return false;
   }

   return true;
}

static inline bool
bit_shader_equal(bi_context *A, bi_context *B)
{
   if (list_length(&A->blocks) != list_length(&B->blocks))
      return false;

   list_pair_for_each_entry(bi_block, blockA, blockB,
                            &A->blocks, &B->blocks, link) {
      if (!bit_block_equal(blockA, blockB))
         return false;
   }

   return true;
}

#define INSTRUCTION_CASE(instr, expected, CB) do { \
   bi_instr *left = instr; \
   bi_instr *right = expected; \
   CB(b, left); \
   if (bit_instr_equal(left, right)) { \
      nr_pass++; \
   } else { \
      fprintf(stderr, "Incorrect optimization\n"); \
      bi_print_instr(instr, stderr); \
      bi_print_instr(left, stderr); \
      bi_print_instr(right, stderr); \
      fprintf(stderr, "\n"); \
      nr_fail++; \
   } \
} while(0)

#define BIT_ASSERT(condition) do { \
   if (condition) { \
      nr_pass++; \
   } else { \
      fprintf(stderr, "Assertion failed: %s\n", #condition); \
      nr_fail++; \
   } \
} while(0)

#endif
