/*
 * Copyright © 2016 Intel Corporation
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

#include "nir_phi_builder.h"
#include "nir/nir_vla.h"

struct nir_phi_builder {
   nir_shader *shader;
   nir_function_impl *impl;

   /* Copied from the impl for easy access */
   unsigned num_blocks;

   /* Array of all blocks indexed by block->index. */
   nir_block **blocks;

   /* Hold on to the values so we can easily iterate over them. */
   struct exec_list values;

   /* Worklist for phi adding */
   unsigned iter_count;
   unsigned *work;
   nir_block **W;
};

#define NEEDS_PHI ((nir_ssa_def *)(intptr_t)-1)

struct nir_phi_builder_value {
   struct exec_node node;

   struct nir_phi_builder *builder;

   /* Needed so we can create phis and undefs */
   unsigned num_components;
   unsigned bit_size;

   /* The list of phi nodes associated with this value.  Phi nodes are not
    * added directly.  Instead, they are created, the instr->block pointer
    * set, and then added to this list.  Later, in phi_builder_finish, we
    * set up their sources and add them to the top of their respective
    * blocks.
    */
   struct exec_list phis;

   /* Array of SSA defs, indexed by block.  For each block, this array has has
    * one of three types of values:
    *
    *  - NULL. Indicates that there is no known definition in this block.  If
    *    you need to find one, look at the block's immediate dominator.
    *
    *  - NEEDS_PHI. Indicates that the block may need a phi node but none has
    *    been created yet.  If a def is requested for a block, a phi will need
    *    to be created.
    *
    *  - A regular SSA def.  This will be either the result of a phi node or
    *    one of the defs provided by nir_phi_builder_value_set_blocK_def().
    */
   nir_ssa_def *defs[0];
};

struct nir_phi_builder *
nir_phi_builder_create(nir_function_impl *impl)
{
   struct nir_phi_builder *pb = ralloc(NULL, struct nir_phi_builder);

   pb->shader = impl->function->shader;
   pb->impl = impl;

   assert(impl->valid_metadata & (nir_metadata_block_index |
                                  nir_metadata_dominance));

   pb->num_blocks = impl->num_blocks;
   pb->blocks = ralloc_array(pb, nir_block *, pb->num_blocks);
   nir_foreach_block(block, impl) {
      pb->blocks[block->index] = block;
   }

   exec_list_make_empty(&pb->values);

   pb->iter_count = 0;
   pb->work = rzalloc_array(pb, unsigned, pb->num_blocks);
   pb->W = ralloc_array(pb, nir_block *, pb->num_blocks);

   return pb;
}

struct nir_phi_builder_value *
nir_phi_builder_add_value(struct nir_phi_builder *pb, unsigned num_components,
                          unsigned bit_size, const BITSET_WORD *defs)
{
   struct nir_phi_builder_value *val;
   unsigned i, w_start = 0, w_end = 0;

   val = rzalloc_size(pb, sizeof(*val) + sizeof(val->defs[0]) * pb->num_blocks);
   val->builder = pb;
   val->num_components = num_components;
   val->bit_size = bit_size;
   exec_list_make_empty(&val->phis);
   exec_list_push_tail(&pb->values, &val->node);

   pb->iter_count++;

   BITSET_WORD tmp;
   BITSET_FOREACH_SET(i, tmp, defs, pb->num_blocks) {
      if (pb->work[i] < pb->iter_count)
         pb->W[w_end++] = pb->blocks[i];
      pb->work[i] = pb->iter_count;
   }

   while (w_start != w_end) {
      nir_block *cur = pb->W[w_start++];
      struct set_entry *dom_entry;
      set_foreach(cur->dom_frontier, dom_entry) {
         nir_block *next = (nir_block *) dom_entry->key;

         /* If there's more than one return statement, then the end block
          * can be a join point for some definitions. However, there are
          * no instructions in the end block, so nothing would use those
          * phi nodes. Of course, we couldn't place those phi nodes
          * anyways due to the restriction of having no instructions in the
          * end block...
          */
         if (next == pb->impl->end_block)
            continue;

         if (val->defs[next->index] == NULL) {
            /* Instead of creating a phi node immediately, we simply set the
             * value to the magic value NEEDS_PHI.  Later, we create phi nodes
             * on demand in nir_phi_builder_value_get_block_def().
             */
            val->defs[next->index] = NEEDS_PHI;

            if (pb->work[next->index] < pb->iter_count) {
               pb->work[next->index] = pb->iter_count;
               pb->W[w_end++] = next;
            }
         }
      }
   }

   return val;
}

void
nir_phi_builder_value_set_block_def(struct nir_phi_builder_value *val,
                                    nir_block *block, nir_ssa_def *def)
{
   val->defs[block->index] = def;
}

nir_ssa_def *
nir_phi_builder_value_get_block_def(struct nir_phi_builder_value *val,
                                    nir_block *block)
{
   /* For each block, we have one of three types of values */
   if (val->defs[block->index] == NULL) {
      /* NULL indicates that we have no SSA def for this block. */
      if (block->imm_dom) {
         /* Grab it from our immediate dominator.  We'll stash it here for
          * easy access later.
          */
         val->defs[block->index] =
            nir_phi_builder_value_get_block_def(val, block->imm_dom);
         return val->defs[block->index];
      } else {
         /* No immediate dominator means that this block is either the
          * start block or unreachable.  In either case, the value is
          * undefined so we need an SSA undef.
          */
         nir_ssa_undef_instr *undef =
            nir_ssa_undef_instr_create(val->builder->shader,
                                       val->num_components,
                                       val->bit_size);
         nir_instr_insert(nir_before_cf_list(&val->builder->impl->body),
                          &undef->instr);
         val->defs[block->index] = &undef->def;
         return &undef->def;
      }
   } else if (val->defs[block->index] == NEEDS_PHI) {
      /* The magic value NEEDS_PHI indicates that the block needs a phi node
       * but none has been created.  We need to create one now so we can
       * return it to the caller.
       *
       * Because a phi node may use SSA defs that it does not dominate (this
       * happens in loops), we do not yet have enough information to fully
       * fill out the phi node.  Instead, the phi nodes we create here will be
       * empty (have no sources) and won't actually be placed in the block's
       * instruction list yet.  Later, in nir_phi_builder_finish(), we walk
       * over all of the phi instructions, fill out the sources lists, and
       * place them at the top of their respective block's instruction list.
       *
       * Creating phi nodes on-demand allows us to avoid creating dead phi
       * nodes that will just get deleted later. While this probably isn't a
       * big win for a full into-SSA pass, other users may use the phi builder
       * to make small SSA form repairs where most of the phi nodes will never
       * be used.
       */
      nir_phi_instr *phi = nir_phi_instr_create(val->builder->shader);
      nir_ssa_dest_init(&phi->instr, &phi->dest, val->num_components,
                        val->bit_size, NULL);
      phi->instr.block = block;
      exec_list_push_tail(&val->phis, &phi->instr.node);
      val->defs[block->index] = &phi->dest.ssa;
      return &phi->dest.ssa;
   } else {
      /* In this case, we have an actual SSA def.  It's either the result of a
       * phi node created by the case above or one passed to us through
       * nir_phi_builder_value_set_block_def().
       */
      return val->defs[block->index];
   }
}

static int
compare_blocks(const void *_a, const void *_b)
{
   nir_block * const * a = _a;
   nir_block * const * b = _b;

   return (*a)->index - (*b)->index;
}

void
nir_phi_builder_finish(struct nir_phi_builder *pb)
{
   const unsigned num_blocks = pb->num_blocks;
   NIR_VLA(nir_block *, preds, num_blocks);

   foreach_list_typed(struct nir_phi_builder_value, val, node, &pb->values) {
      /* We treat the linked list of phi nodes like a worklist.  The list is
       * pre-populated by calls to nir_phi_builder_value_get_block_def() that
       * create phi nodes.  As we fill in the sources of phi nodes, more may
       * be created and are added to the end of the list.
       *
       * Because we are adding and removing phi nodes from the list as we go,
       * we can't iterate over it normally.  Instead, we just iterate until
       * the list is empty.
       */
      while (!exec_list_is_empty(&val->phis)) {
         struct exec_node *head = exec_list_get_head(&val->phis);
         nir_phi_instr *phi = exec_node_data(nir_phi_instr, head, instr.node);
         assert(phi->instr.type == nir_instr_type_phi);

         exec_node_remove(&phi->instr.node);

         /* Construct an array of predecessors.  We sort it to ensure
          * determinism in the phi insertion algorithm.
          *
          * XXX: Calling qsort this many times seems expensive.
          */
         int num_preds = 0;
         struct set_entry *entry;
         set_foreach(phi->instr.block->predecessors, entry)
            preds[num_preds++] = (nir_block *)entry->key;
         qsort(preds, num_preds, sizeof(*preds), compare_blocks);

         for (unsigned i = 0; i < num_preds; i++) {
            nir_phi_src *src = ralloc(phi, nir_phi_src);
            src->pred = preds[i];
            src->src = nir_src_for_ssa(
               nir_phi_builder_value_get_block_def(val, preds[i]));
            exec_list_push_tail(&phi->srcs, &src->node);
         }

         nir_instr_insert(nir_before_block(phi->instr.block), &phi->instr);
      }
   }

   ralloc_free(pb);
}
