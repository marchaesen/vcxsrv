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
 *    Connor Abbott (cwabbott0@gmail.com)
 *
 */

#include "nir.h"

/*
 * Implements the algorithms for computing the dominance tree and the
 * dominance frontier from "A Simple, Fast Dominance Algorithm" by Cooper,
 * Harvey, and Kennedy.
 */

typedef struct {
   nir_function_impl *impl;
   bool progress;
} dom_state;

static bool
init_block_cb(nir_block *block, void *_state)
{
   dom_state *state = (dom_state *) _state;
   if (block == nir_start_block(state->impl))
      block->imm_dom = block;
   else
      block->imm_dom = NULL;
   block->num_dom_children = 0;

   struct set_entry *entry;
   set_foreach(block->dom_frontier, entry) {
      _mesa_set_remove(block->dom_frontier, entry);
   }

   return true;
}

static nir_block *
intersect(nir_block *b1, nir_block *b2)
{
   while (b1 != b2) {
      /*
       * Note, the comparisons here are the opposite of what the paper says
       * because we index blocks from beginning -> end (i.e. reverse
       * post-order) instead of post-order like they assume.
       */
      while (b1->index > b2->index)
         b1 = b1->imm_dom;
      while (b2->index > b1->index)
         b2 = b2->imm_dom;
   }

   return b1;
}

static bool
calc_dominance_cb(nir_block *block, void *_state)
{
   dom_state *state = (dom_state *) _state;
   if (block == nir_start_block(state->impl))
      return true;

   nir_block *new_idom = NULL;
   struct set_entry *entry;
   set_foreach(block->predecessors, entry) {
      nir_block *pred = (nir_block *) entry->key;

      if (pred->imm_dom) {
         if (new_idom)
            new_idom = intersect(pred, new_idom);
         else
            new_idom = pred;
      }
   }

   assert(new_idom);
   if (block->imm_dom != new_idom) {
      block->imm_dom = new_idom;
      state->progress = true;
   }

   return true;
}

static bool
calc_dom_frontier_cb(nir_block *block, void *state)
{
   (void) state;

   if (block->predecessors->entries > 1) {
      struct set_entry *entry;
      set_foreach(block->predecessors, entry) {
         nir_block *runner = (nir_block *) entry->key;
         while (runner != block->imm_dom) {
            _mesa_set_add(runner->dom_frontier, block);
            runner = runner->imm_dom;
         }
      }
   }

   return true;
}

/*
 * Compute each node's children in the dominance tree from the immediate
 * dominator information. We do this in three stages:
 *
 * 1. Calculate the number of children each node has
 * 2. Allocate arrays, setting the number of children to 0 again
 * 3. For each node, add itself to its parent's list of children, using
 *    num_dom_children as an index - at the end of this step, num_dom_children
 *    for each node will be the same as it was at the end of step #1.
 */

static bool
block_count_children(nir_block *block, void *state)
{
   (void) state;

   if (block->imm_dom)
      block->imm_dom->num_dom_children++;

   return true;
}

static bool
block_alloc_children(nir_block *block, void *state)
{
   void *mem_ctx = state;

   block->dom_children = ralloc_array(mem_ctx, nir_block *,
                                      block->num_dom_children);
   block->num_dom_children = 0;

   return true;
}

static bool
block_add_child(nir_block *block, void *state)
{
   (void) state;

   if (block->imm_dom)
      block->imm_dom->dom_children[block->imm_dom->num_dom_children++] = block;

   return true;
}

static void
calc_dom_children(nir_function_impl* impl)
{
   void *mem_ctx = ralloc_parent(impl);

   nir_foreach_block(impl, block_count_children, NULL);
   nir_foreach_block(impl, block_alloc_children, mem_ctx);
   nir_foreach_block(impl, block_add_child, NULL);
}

static void
calc_dfs_indicies(nir_block *block, unsigned *index)
{
   block->dom_pre_index = (*index)++;

   for (unsigned i = 0; i < block->num_dom_children; i++)
      calc_dfs_indicies(block->dom_children[i], index);

   block->dom_post_index = (*index)++;
}

void
nir_calc_dominance_impl(nir_function_impl *impl)
{
   if (impl->valid_metadata & nir_metadata_dominance)
      return;

   nir_metadata_require(impl, nir_metadata_block_index);

   dom_state state;
   state.impl = impl;
   state.progress = true;

   nir_foreach_block(impl, init_block_cb, &state);

   while (state.progress) {
      state.progress = false;
      nir_foreach_block(impl, calc_dominance_cb, &state);
   }

   nir_foreach_block(impl, calc_dom_frontier_cb, &state);

   nir_block *start_block = nir_start_block(impl);
   start_block->imm_dom = NULL;

   calc_dom_children(impl);

   unsigned dfs_index = 0;
   calc_dfs_indicies(start_block, &dfs_index);
}

void
nir_calc_dominance(nir_shader *shader)
{
   nir_foreach_function(shader, function) {
      if (function->impl)
         nir_calc_dominance_impl(function->impl);
   }
}

/**
 * Computes the least common anscestor of two blocks.  If one of the blocks
 * is null, the other block is returned.
 */
nir_block *
nir_dominance_lca(nir_block *b1, nir_block *b2)
{
   if (b1 == NULL)
      return b2;

   if (b2 == NULL)
      return b1;

   assert(nir_cf_node_get_function(&b1->cf_node) ==
          nir_cf_node_get_function(&b2->cf_node));

   assert(nir_cf_node_get_function(&b1->cf_node)->valid_metadata &
          nir_metadata_dominance);

   return intersect(b1, b2);
}

/**
 * Returns true if parent dominates child
 */
bool
nir_block_dominates(nir_block *parent, nir_block *child)
{
   assert(nir_cf_node_get_function(&parent->cf_node) ==
          nir_cf_node_get_function(&child->cf_node));

   assert(nir_cf_node_get_function(&parent->cf_node)->valid_metadata &
          nir_metadata_dominance);

   return child->dom_pre_index >= parent->dom_pre_index &&
          child->dom_post_index <= parent->dom_post_index;
}

static bool
dump_block_dom(nir_block *block, void *state)
{
   FILE *fp = state;
   if (block->imm_dom)
      fprintf(fp, "\t%u -> %u\n", block->imm_dom->index, block->index);
   return true;
}

void
nir_dump_dom_tree_impl(nir_function_impl *impl, FILE *fp)
{
   fprintf(fp, "digraph doms_%s {\n", impl->function->name);
   nir_foreach_block(impl, dump_block_dom, fp);
   fprintf(fp, "}\n\n");
}

void
nir_dump_dom_tree(nir_shader *shader, FILE *fp)
{
   nir_foreach_function(shader, function) {
      if (function->impl)
         nir_dump_dom_tree_impl(function->impl, fp);
   }
}

static bool
dump_block_dom_frontier(nir_block *block, void *state)
{
   FILE *fp = state;

   fprintf(fp, "DF(%u) = {", block->index);
   struct set_entry *entry;
   set_foreach(block->dom_frontier, entry) {
      nir_block *df = (nir_block *) entry->key;
      fprintf(fp, "%u, ", df->index);
   }
   fprintf(fp, "}\n");
   return true;
}

void
nir_dump_dom_frontier_impl(nir_function_impl *impl, FILE *fp)
{
   nir_foreach_block(impl, dump_block_dom_frontier, fp);
}

void
nir_dump_dom_frontier(nir_shader *shader, FILE *fp)
{
   nir_foreach_function(shader, function) {
      if (function->impl)
         nir_dump_dom_frontier_impl(function->impl, fp);
   }
}

static bool
dump_block_succs(nir_block *block, void *state)
{
   FILE *fp = state;
   if (block->successors[0])
      fprintf(fp, "\t%u -> %u\n", block->index, block->successors[0]->index);
   if (block->successors[1])
      fprintf(fp, "\t%u -> %u\n", block->index, block->successors[1]->index);
   return true;
}

void
nir_dump_cfg_impl(nir_function_impl *impl, FILE *fp)
{
   fprintf(fp, "digraph cfg_%s {\n", impl->function->name);
   nir_foreach_block(impl, dump_block_succs, fp);
   fprintf(fp, "}\n\n");
}

void
nir_dump_cfg(nir_shader *shader, FILE *fp)
{
   nir_foreach_function(shader, function) {
      if (function->impl)
         nir_dump_cfg_impl(function->impl, fp);
   }
}
