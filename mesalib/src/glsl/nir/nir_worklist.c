/*
 * Copyright © 2014 Intel Corporation
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
 * Authors:
 *    Jason Ekstrand (jason@jlekstrand.net)
 *
 */

#include "nir_worklist.h"

void
nir_block_worklist_init(nir_block_worklist *w, unsigned num_blocks,
                        void *mem_ctx)
{
   w->size = num_blocks;
   w->count = 0;
   w->start = 0;

   w->blocks_present = rzalloc_array(mem_ctx, BITSET_WORD,
                                     BITSET_WORDS(num_blocks));
   w->blocks = ralloc_array(mem_ctx, nir_block *, num_blocks);
}

void
nir_block_worklist_fini(nir_block_worklist *w)
{
   ralloc_free(w->blocks_present);
   ralloc_free(w->blocks);
}

static bool
worklist_add_block(nir_block *block, void *w)
{
   nir_block_worklist_push_tail(w, block);

   return true;
}

void
nir_block_worklist_add_all(nir_block_worklist *w, nir_function_impl *impl)
{
   nir_foreach_block(impl, worklist_add_block, w);
}

void
nir_block_worklist_push_head(nir_block_worklist *w, nir_block *block)
{
   /* Pushing a block we already have is a no-op */
   if (BITSET_TEST(w->blocks_present, block->index))
      return;

   assert(w->count < w->size);

   if (w->start == 0)
      w->start = w->size - 1;
   else
      w->start--;

   w->count++;

   w->blocks[w->start] = block;
   BITSET_SET(w->blocks_present, block->index);
}

nir_block *
nir_block_worklist_peek_head(const nir_block_worklist *w)
{
   assert(w->count > 0);

   return w->blocks[w->start];
}

nir_block *
nir_block_worklist_pop_head(nir_block_worklist *w)
{
   assert(w->count > 0);

   unsigned head = w->start;

   w->start = (w->start + 1) % w->size;
   w->count--;

   BITSET_CLEAR(w->blocks_present, w->blocks[head]->index);
   return w->blocks[head];
}

void
nir_block_worklist_push_tail(nir_block_worklist *w, nir_block *block)
{
   /* Pushing a block we already have is a no-op */
   if (BITSET_TEST(w->blocks_present, block->index))
      return;

   assert(w->count < w->size);

   w->count++;

   unsigned tail = (w->start + w->count - 1) % w->size;

   w->blocks[tail] = block;
   BITSET_SET(w->blocks_present, block->index);
}

nir_block *
nir_block_worklist_peek_tail(const nir_block_worklist *w)
{
   assert(w->count > 0);

   unsigned tail = (w->start + w->count - 1) % w->size;

   return w->blocks[tail];
}

nir_block *
nir_block_worklist_pop_tail(nir_block_worklist *w)
{
   assert(w->count > 0);

   unsigned tail = (w->start + w->count - 1) % w->size;

   w->count--;

   BITSET_CLEAR(w->blocks_present, w->blocks[tail]->index);
   return w->blocks[tail];
}
