/*
 * Copyright (C) 2021 Alyssa Rosenzweig
 * Copyright (C) 2019-2020 Collabora, Ltd.
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

#include "agx_compiler.h"
#include "util/u_memory.h"
#include "util/list.h"
#include "util/set.h"

/* Liveness analysis is a backwards-may dataflow analysis pass. Within a block,
 * we compute live_out from live_in. The intrablock pass is linear-time. It
 * returns whether progress was made. */

/* live_in[s] = GEN[s] + (live_out[s] - KILL[s]) */

void
agx_liveness_ins_update(BITSET_WORD *live, agx_instr *I)
{
   agx_foreach_dest(I, d) {
      if (I->dest[d].type == AGX_INDEX_NORMAL)
         BITSET_CLEAR(live, I->dest[d].value);
   }

   agx_foreach_src(I, s) {
      if (I->src[s].type == AGX_INDEX_NORMAL) {
         /* If the source is not live after this instruction, but becomes live
          * at this instruction, this is the use that kills the source */
         I->src[s].kill = !BITSET_TEST(live, I->src[s].value);
         BITSET_SET(live, I->src[s].value);
      }
   }
}

static bool
liveness_block_update(agx_block *blk, unsigned words)
{
   bool progress = false;

   /* live_out[s] = sum { p in succ[s] } ( live_in[p] ) */
   agx_foreach_successor(blk, succ) {
      for (unsigned i = 0; i < words; ++i)
         blk->live_out[i] |= succ->live_in[i];
   }

   /* live_in is live_out after iteration */
   BITSET_WORD *live = ralloc_array(blk, BITSET_WORD, words);
   memcpy(live, blk->live_out, words * sizeof(BITSET_WORD));

   agx_foreach_instr_in_block_rev(blk, I)
      agx_liveness_ins_update(live, I);

   /* To figure out progress, diff live_in */
   for (unsigned i = 0; i < words; ++i)
      progress |= (blk->live_in[i] != live[i]);

   ralloc_free(blk->live_in);
   blk->live_in = live;

   return progress;
}

/* Globally, liveness analysis uses a fixed-point algorithm based on a
 * worklist. We initialize a work list with the exit block. We iterate the work
 * list to compute live_in from live_out for each block on the work list,
 * adding the predecessors of the block to the work list if we made progress.
 */

void
agx_compute_liveness(agx_context *ctx)
{
   if (ctx->has_liveness)
      return;

   /* Set of agx_block */
   struct set *work_list = _mesa_set_create(NULL, _mesa_hash_pointer,
                                                  _mesa_key_pointer_equal);

   /* Free any previous liveness, and allocate */
   unsigned words = BITSET_WORDS(ctx->alloc);

   agx_foreach_block(ctx, block) {
      if (block->live_in)
         ralloc_free(block->live_in);

      if (block->live_out)
         ralloc_free(block->live_out);

      block->pass_flags = false;
      block->live_in = rzalloc_array(block, BITSET_WORD, words);
      block->live_out = rzalloc_array(block, BITSET_WORD, words);
   }

   /* Initialize the work list with the exit block */
   struct set_entry *cur = _mesa_set_add(work_list, agx_exit_block(ctx));

   /* Iterate the work list */
   do {
      /* Pop off a block */
      agx_block *blk = (struct agx_block *) cur->key;
      _mesa_set_remove(work_list, cur);

      /* Update its liveness information */
      bool progress = liveness_block_update(blk, words);

      /* If we made progress, we need to process the predecessors */

      if (progress || !blk->pass_flags) {
         agx_foreach_predecessor(blk, pred)
            _mesa_set_add(work_list, pred);
      }

      /* Use pass flags to communicate that we've visited this block */
      blk->pass_flags = true;
   } while((cur = _mesa_set_next_entry(work_list, NULL)) != NULL);

   _mesa_set_destroy(work_list, NULL);

   ctx->has_liveness = true;
}
