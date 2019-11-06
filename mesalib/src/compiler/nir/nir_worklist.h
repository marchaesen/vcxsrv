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


#ifndef _NIR_WORKLIST_
#define _NIR_WORKLIST_

#include "nir.h"
#include "util/set.h"
#include "util/u_vector.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Represents a double-ended queue of unique blocks
 *
 * The worklist datastructure guarantees that eacy block is in the queue at
 * most once.  Pushing a block onto either end of the queue is a no-op if
 * the block is already in the queue.  In order for this to work, the
 * caller must ensure that the blocks are properly indexed.
 */
typedef struct {
   /* The total size of the worklist */
   unsigned size;

   /* The number of blocks currently in the worklist */
   unsigned count;

   /* The offset in the array of blocks at which the list starts */
   unsigned start;

   /* A bitset of all of the blocks currently present in the worklist */
   BITSET_WORD *blocks_present;

   /* The actual worklist */
   nir_block **blocks;
} nir_block_worklist;

void nir_block_worklist_init(nir_block_worklist *w, unsigned num_blocks,
                             void *mem_ctx);
void nir_block_worklist_fini(nir_block_worklist *w);

void nir_block_worklist_add_all(nir_block_worklist *w, nir_function_impl *impl);

static inline bool
nir_block_worklist_is_empty(const nir_block_worklist *w)
{
   return w->count == 0;
}

void nir_block_worklist_push_head(nir_block_worklist *w, nir_block *block);

nir_block *nir_block_worklist_peek_head(const nir_block_worklist *w);

nir_block *nir_block_worklist_pop_head(nir_block_worklist *w);

void nir_block_worklist_push_tail(nir_block_worklist *w, nir_block *block);

nir_block *nir_block_worklist_peek_tail(const nir_block_worklist *w);

nir_block *nir_block_worklist_pop_tail(nir_block_worklist *w);


/*
 * This worklist implementation, in contrast to the block worklist, does not
 * have unique entries, meaning a nir_instr can be inserted more than once
 * into the worklist. It uses u_vector to keep the overhead and memory
 * footprint at a minimum.
 *
 * Making it unique by using a set was tested, but for the single usecase
 * (nir_opt_dce) it did not improve speed. There we check the pass_flag bit
 * and abort immediately if there's nothing to do, so the added overhead of
 * the set was higher than just processing the few extra entries.
 */

typedef struct {
   struct u_vector instr_vec;
} nir_instr_worklist;

static inline nir_instr_worklist *
nir_instr_worklist_create() {
   nir_instr_worklist *wl = malloc(sizeof(nir_instr_worklist));
   if (!wl)
      return NULL;

   if (!u_vector_init(&wl->instr_vec, sizeof(struct nir_instr *),
                      sizeof(struct nir_instr *) * 8)) {
      free(wl);
      return NULL;
   }

   return wl;
}

static inline uint32_t
nir_instr_worklist_length(nir_instr_worklist *wl)
{
   return u_vector_length(&wl->instr_vec);
}

static inline bool
nir_instr_worklist_is_empty(nir_instr_worklist *wl)
{
   return nir_instr_worklist_length(wl) == 0;
}

static inline void
nir_instr_worklist_destroy(nir_instr_worklist *wl)
{
   u_vector_finish(&wl->instr_vec);
   free(wl);
}

static inline void
nir_instr_worklist_push_tail(nir_instr_worklist *wl, nir_instr *instr)
{
   struct nir_instr **vec_instr = u_vector_add(&wl->instr_vec);
   *vec_instr = instr;
}

static inline nir_instr *
nir_instr_worklist_pop_head(nir_instr_worklist *wl)
{
   struct nir_instr **vec_instr = u_vector_remove(&wl->instr_vec);

   if (vec_instr == NULL)
      return NULL;

   return *vec_instr;
}

#define nir_foreach_instr_in_worklist(instr, wl) \
   for (nir_instr *instr; (instr = nir_instr_worklist_pop_head(wl));)

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* _NIR_WORKLIST_ */
