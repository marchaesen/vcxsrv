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
#include <stdlib.h>

/*
 * Implements the classic to-SSA algorithm described by Cytron et. al. in
 * "Efficiently Computing Static Single Assignment Form and the Control
 * Dependence Graph."
 */

/* inserts a phi node of the form reg = phi(reg, reg, reg, ...) */

static void
insert_trivial_phi(nir_register *reg, nir_block *block, void *mem_ctx)
{
   nir_phi_instr *instr = nir_phi_instr_create(mem_ctx);

   instr->dest.reg.reg = reg;
   struct set_entry *entry;
   set_foreach(block->predecessors, entry) {
      nir_block *pred = (nir_block *) entry->key;

      nir_phi_src *src = ralloc(instr, nir_phi_src);
      src->pred = pred;
      src->src.is_ssa = false;
      src->src.reg.base_offset = 0;
      src->src.reg.indirect = NULL;
      src->src.reg.reg = reg;
      exec_list_push_tail(&instr->srcs, &src->node);
   }

   nir_instr_insert_before_block(block, &instr->instr);
}

static void
insert_phi_nodes(nir_function_impl *impl)
{
   void *mem_ctx = ralloc_parent(impl);

   unsigned *work = calloc(impl->num_blocks, sizeof(unsigned));
   unsigned *has_already = calloc(impl->num_blocks, sizeof(unsigned));

   /*
    * Since the work flags already prevent us from inserting a node that has
    * ever been inserted into W, we don't need to use a set to represent W.
    * Also, since no block can ever be inserted into W more than once, we know
    * that the maximum size of W is the number of basic blocks in the
    * function. So all we need to handle W is an array and a pointer to the
    * next element to be inserted and the next element to be removed.
    */
   nir_block **W = malloc(impl->num_blocks * sizeof(nir_block *));
   unsigned w_start, w_end;

   unsigned iter_count = 0;

   nir_index_blocks(impl);

   foreach_list_typed(nir_register, reg, node, &impl->registers) {
      if (reg->num_array_elems != 0)
         continue;

      w_start = w_end = 0;
      iter_count++;

      nir_foreach_def(dest, reg) {
         nir_instr *def = dest->reg.parent_instr;
         if (work[def->block->index] < iter_count)
            W[w_end++] = def->block;
         work[def->block->index] = iter_count;
      }

      while (w_start != w_end) {
         nir_block *cur = W[w_start++];
         struct set_entry *entry;
         set_foreach(cur->dom_frontier, entry) {
            nir_block *next = (nir_block *) entry->key;

            /*
             * If there's more than one return statement, then the end block
             * can be a join point for some definitions. However, there are
             * no instructions in the end block, so nothing would use those
             * phi nodes. Of course, we couldn't place those phi nodes
             * anyways due to the restriction of having no instructions in the
             * end block...
             */
            if (next == impl->end_block)
               continue;

            if (has_already[next->index] < iter_count) {
               insert_trivial_phi(reg, next, mem_ctx);
               has_already[next->index] = iter_count;
               if (work[next->index] < iter_count) {
                  work[next->index] = iter_count;
                  W[w_end++] = next;
               }
            }
         }
      }
   }

   free(work);
   free(has_already);
   free(W);
}

typedef struct {
   nir_ssa_def **stack;
   int index;
   unsigned num_defs; /** < used to add indices to debug names */
#ifndef NDEBUG
   unsigned stack_size;
#endif
} reg_state;

typedef struct {
   reg_state *states;
   void *mem_ctx;
   nir_instr *parent_instr;
   nir_if *parent_if;
   nir_function_impl *impl;

   /* map from SSA value -> original register */
   struct hash_table *ssa_map;
} rewrite_state;

static nir_ssa_def *get_ssa_src(nir_register *reg, rewrite_state *state)
{
   unsigned index = reg->index;

   if (state->states[index].index == -1) {
      /*
       * We're using an undefined register, create a new undefined SSA value
       * to preserve the information that this source is undefined
       */
      nir_ssa_undef_instr *instr =
         nir_ssa_undef_instr_create(state->mem_ctx, reg->num_components,
                                    reg->bit_size);

      /*
       * We could just insert the undefined instruction before the instruction
       * we're rewriting, but we could be rewriting a phi source in which case
       * we can't do that, so do the next easiest thing - insert it at the
       * beginning of the program. In the end, it doesn't really matter where
       * the undefined instructions are because they're going to be ignored
       * in the backend.
       */
      nir_instr_insert_before_cf_list(&state->impl->body, &instr->instr);
      return &instr->def;
   }

   return state->states[index].stack[state->states[index].index];
}

static bool
rewrite_use(nir_src *src, void *_state)
{
   rewrite_state *state = (rewrite_state *) _state;

   if (src->is_ssa)
      return true;

   unsigned index = src->reg.reg->index;

   if (state->states[index].stack == NULL)
      return true;

   nir_ssa_def *def = get_ssa_src(src->reg.reg, state);
   if (state->parent_instr)
      nir_instr_rewrite_src(state->parent_instr, src, nir_src_for_ssa(def));
   else
      nir_if_rewrite_condition(state->parent_if, nir_src_for_ssa(def));

   return true;
}

static bool
rewrite_def_forwards(nir_dest *dest, void *_state)
{
   rewrite_state *state = (rewrite_state *) _state;

   if (dest->is_ssa)
      return true;

   nir_register *reg = dest->reg.reg;
   unsigned index = reg->index;

   if (state->states[index].stack == NULL)
      return true;

   char *name = NULL;
   if (dest->reg.reg->name)
      name = ralloc_asprintf(state->mem_ctx, "%s_%u", dest->reg.reg->name,
                             state->states[index].num_defs);

   list_del(&dest->reg.def_link);
   nir_ssa_dest_init(state->parent_instr, dest, reg->num_components,
                     reg->bit_size, name);
   ralloc_free(name);

   /* push our SSA destination on the stack */
   state->states[index].index++;
   assert(state->states[index].index < state->states[index].stack_size);
   state->states[index].stack[state->states[index].index] = &dest->ssa;
   state->states[index].num_defs++;

   _mesa_hash_table_insert(state->ssa_map, &dest->ssa, reg);

   return true;
}

static void
rewrite_alu_instr_forward(nir_alu_instr *instr, rewrite_state *state)
{
   state->parent_instr = &instr->instr;

   nir_foreach_src(&instr->instr, rewrite_use, state);

   if (instr->dest.dest.is_ssa)
      return;

   nir_register *reg = instr->dest.dest.reg.reg;
   unsigned index = reg->index;

   if (state->states[index].stack == NULL)
      return;

   unsigned write_mask = instr->dest.write_mask;
   if (write_mask != (1 << instr->dest.dest.reg.reg->num_components) - 1) {
      /*
       * Calculate the number of components the final instruction, which for
       * per-component things is the number of output components of the
       * instruction and non-per-component things is the number of enabled
       * channels in the write mask.
       */
      unsigned num_components;
      if (nir_op_infos[instr->op].output_size == 0) {
         unsigned temp = (write_mask & 0x5) + ((write_mask >> 1) & 0x5);
         num_components = (temp & 0x3) + ((temp >> 2) & 0x3);
      } else {
         num_components = nir_op_infos[instr->op].output_size;
      }

      char *name = NULL;
      if (instr->dest.dest.reg.reg->name)
         name = ralloc_asprintf(state->mem_ctx, "%s_%u",
                                reg->name, state->states[index].num_defs);

      instr->dest.write_mask = (1 << num_components) - 1;
      list_del(&instr->dest.dest.reg.def_link);
      nir_ssa_dest_init(&instr->instr, &instr->dest.dest, num_components,
                        reg->bit_size, name);
      ralloc_free(name);

      if (nir_op_infos[instr->op].output_size == 0) {
         /*
          * When we change the output writemask, we need to change the
          * swizzles for per-component inputs too
          */
         for (unsigned i = 0; i < nir_op_infos[instr->op].num_inputs; i++) {
            if (nir_op_infos[instr->op].input_sizes[i] != 0)
               continue;

            unsigned new_swizzle[4] = {0, 0, 0, 0};

            /*
             * We keep two indices:
             * 1. The index of the original (non-SSA) component
             * 2. The index of the post-SSA, compacted, component
             *
             * We need to map the swizzle component at index 1 to the swizzle
             * component at index 2.
             */

            unsigned ssa_index = 0;
            for (unsigned index = 0; index < 4; index++) {
               if (!((write_mask >> index) & 1))
                  continue;

               new_swizzle[ssa_index] = instr->src[i].swizzle[index];
               ssa_index++;
            }

            for (unsigned j = 0; j < 4; j++)
               instr->src[i].swizzle[j] = new_swizzle[j];
         }
      }

      nir_op op;
      switch (reg->num_components) {
      case 2: op = nir_op_vec2; break;
      case 3: op = nir_op_vec3; break;
      case 4: op = nir_op_vec4; break;
      default: unreachable("not reached");
      }

      nir_alu_instr *vec = nir_alu_instr_create(state->mem_ctx, op);

      vec->dest.dest.reg.reg = reg;
      vec->dest.write_mask = (1 << reg->num_components) - 1;

      nir_ssa_def *old_src = get_ssa_src(reg, state);
      nir_ssa_def *new_src = &instr->dest.dest.ssa;

      unsigned ssa_index = 0;
      for (unsigned i = 0; i < reg->num_components; i++) {
         vec->src[i].src.is_ssa = true;
         if ((write_mask >> i) & 1) {
            vec->src[i].src.ssa = new_src;
            if (nir_op_infos[instr->op].output_size == 0)
               vec->src[i].swizzle[0] = ssa_index;
            else
               vec->src[i].swizzle[0] = i;
            ssa_index++;
         } else {
            vec->src[i].src.ssa = old_src;
            vec->src[i].swizzle[0] = i;
         }
      }

      nir_instr_insert_after(&instr->instr, &vec->instr);

      state->parent_instr = &vec->instr;
      rewrite_def_forwards(&vec->dest.dest, state);
   } else {
      rewrite_def_forwards(&instr->dest.dest, state);
   }
}

static void
rewrite_phi_instr(nir_phi_instr *instr, rewrite_state *state)
{
   state->parent_instr = &instr->instr;
   rewrite_def_forwards(&instr->dest, state);
}

static void
rewrite_instr_forward(nir_instr *instr, rewrite_state *state)
{
   if (instr->type == nir_instr_type_alu) {
      rewrite_alu_instr_forward(nir_instr_as_alu(instr), state);
      return;
   }

   if (instr->type == nir_instr_type_phi) {
      rewrite_phi_instr(nir_instr_as_phi(instr), state);
      return;
   }

   state->parent_instr = instr;

   nir_foreach_src(instr, rewrite_use, state);
   nir_foreach_dest(instr, rewrite_def_forwards, state);
}

static void
rewrite_phi_sources(nir_block *block, nir_block *pred, rewrite_state *state)
{
   nir_foreach_instr(instr, block) {
      if (instr->type != nir_instr_type_phi)
         break;

      nir_phi_instr *phi_instr = nir_instr_as_phi(instr);

      state->parent_instr = instr;

      nir_foreach_phi_src(src, phi_instr) {
         if (src->pred == pred) {
            rewrite_use(&src->src, state);
            break;
         }
      }
   }
}

static bool
rewrite_def_backwards(nir_dest *dest, void *_state)
{
   rewrite_state *state = (rewrite_state *) _state;

   if (!dest->is_ssa)
      return true;

   struct hash_entry *entry =
      _mesa_hash_table_search(state->ssa_map, &dest->ssa);

   if (!entry)
      return true;

   nir_register *reg = (nir_register *) entry->data;
   unsigned index = reg->index;

   state->states[index].index--;
   assert(state->states[index].index >= -1);

   return true;
}

static void
rewrite_instr_backwards(nir_instr *instr, rewrite_state *state)
{
   nir_foreach_dest(instr, rewrite_def_backwards, state);
}

static void
rewrite_block(nir_block *block, rewrite_state *state)
{
   /* This will skip over any instructions after the current one, which is
    * what we want because those instructions (vector gather, conditional
    * select) will already be in SSA form.
    */
   nir_foreach_instr_safe(instr, block) {
      rewrite_instr_forward(instr, state);
   }

   if (block != state->impl->end_block &&
       !nir_cf_node_is_last(&block->cf_node) &&
       nir_cf_node_next(&block->cf_node)->type == nir_cf_node_if) {
      nir_if *if_stmt = nir_cf_node_as_if(nir_cf_node_next(&block->cf_node));
      state->parent_instr = NULL;
      state->parent_if = if_stmt;
      rewrite_use(&if_stmt->condition, state);
   }

   if (block->successors[0])
      rewrite_phi_sources(block->successors[0], block, state);
   if (block->successors[1])
      rewrite_phi_sources(block->successors[1], block, state);

   for (unsigned i = 0; i < block->num_dom_children; i++)
      rewrite_block(block->dom_children[i], state);

   nir_foreach_instr_reverse(instr, block) {
      rewrite_instr_backwards(instr, state);
   }
}

static void
remove_unused_regs(nir_function_impl *impl, rewrite_state *state)
{
   foreach_list_typed_safe(nir_register, reg, node, &impl->registers) {
      if (state->states[reg->index].stack != NULL)
         exec_node_remove(&reg->node);
   }
}

static void
init_rewrite_state(nir_function_impl *impl, rewrite_state *state)
{
   state->impl = impl;
   state->mem_ctx = ralloc_parent(impl);
   state->ssa_map = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                            _mesa_key_pointer_equal);
   state->states = ralloc_array(NULL, reg_state, impl->reg_alloc);

   foreach_list_typed(nir_register, reg, node, &impl->registers) {
      assert(reg->index < impl->reg_alloc);
      if (reg->num_array_elems > 0) {
         state->states[reg->index].stack = NULL;
      } else {
         /*
          * Calculate a conservative estimate of the stack size based on the
          * number of definitions there are. Note that this function *must* be
          * called after phi nodes are inserted so we can count phi node
          * definitions too.
          */
         unsigned stack_size = list_length(&reg->defs);

         state->states[reg->index].stack = ralloc_array(state->states,
                                                        nir_ssa_def *,
                                                        stack_size);
#ifndef NDEBUG
         state->states[reg->index].stack_size = stack_size;
#endif
         state->states[reg->index].index = -1;
         state->states[reg->index].num_defs = 0;
      }
   }
}

static void
destroy_rewrite_state(rewrite_state *state)
{
   _mesa_hash_table_destroy(state->ssa_map, NULL);
   ralloc_free(state->states);
}

void
nir_convert_to_ssa_impl(nir_function_impl *impl)
{
   nir_metadata_require(impl, nir_metadata_dominance);

   insert_phi_nodes(impl);

   rewrite_state state;
   init_rewrite_state(impl, &state);

   rewrite_block(nir_start_block(impl), &state);

   remove_unused_regs(impl, &state);

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);

   destroy_rewrite_state(&state);
}

void
nir_convert_to_ssa(nir_shader *shader)
{
   nir_foreach_function(function, shader) {
      if (function->impl)
         nir_convert_to_ssa_impl(function->impl);
   }
}
