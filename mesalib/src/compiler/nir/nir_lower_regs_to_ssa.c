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
#include "nir_builder.h"
#include "nir_phi_builder.h"
#include "nir_vla.h"

struct regs_to_ssa_state {
   nir_shader *shader;

   struct nir_phi_builder_value **values;
};

static bool
rewrite_src(nir_src *src, void *_state)
{
   struct regs_to_ssa_state *state = _state;

   if (src->is_ssa)
      return true;

   nir_instr *instr = src->parent_instr;
   nir_register *reg = src->reg.reg;
   struct nir_phi_builder_value *value = state->values[reg->index];
   if (!value)
      return true;

   nir_block *block;
   if (instr->type == nir_instr_type_phi) {
      nir_phi_src *phi_src = exec_node_data(nir_phi_src, src, src);
      block = phi_src->pred;
   } else {
      block = instr->block;
   }

   nir_ssa_def *def = nir_phi_builder_value_get_block_def(value, block);
   nir_instr_rewrite_src(instr, src, nir_src_for_ssa(def));

   return true;
}

static void
rewrite_if_condition(nir_if *nif, struct regs_to_ssa_state *state)
{
   if (nif->condition.is_ssa)
      return;

   nir_block *block = nir_cf_node_as_block(nir_cf_node_prev(&nif->cf_node));
   nir_register *reg = nif->condition.reg.reg;
   struct nir_phi_builder_value *value = state->values[reg->index];
   if (!value)
      return;

   nir_ssa_def *def = nir_phi_builder_value_get_block_def(value, block);
   nir_if_rewrite_condition(nif, nir_src_for_ssa(def));
}

static bool
rewrite_dest(nir_dest *dest, void *_state)
{
   struct regs_to_ssa_state *state = _state;

   if (dest->is_ssa)
      return true;

   nir_instr *instr = dest->reg.parent_instr;
   nir_register *reg = dest->reg.reg;
   struct nir_phi_builder_value *value = state->values[reg->index];
   if (!value)
      return true;

   list_del(&dest->reg.def_link);
   nir_ssa_dest_init(instr, dest, reg->num_components,
                     reg->bit_size, reg->name);

   nir_phi_builder_value_set_block_def(value, instr->block, &dest->ssa);

   return true;
}

static void
rewrite_alu_instr(nir_alu_instr *alu, struct regs_to_ssa_state *state)
{
   nir_foreach_src(&alu->instr, rewrite_src, state);

   if (alu->dest.dest.is_ssa)
      return;

   nir_register *reg = alu->dest.dest.reg.reg;
   struct nir_phi_builder_value *value = state->values[reg->index];
   if (!value)
      return;

   unsigned write_mask = alu->dest.write_mask;
   if (write_mask == (1 << reg->num_components) - 1) {
      /* This is the simple case where the instruction writes all the
       * components.  We can handle that the same as any other destination.
       */
      rewrite_dest(&alu->dest.dest, state);
      return;
   }

   /* Calculate the number of components the final instruction, which for
    * per-component things is the number of output components of the
    * instruction and non-per-component things is the number of enabled
    * channels in the write mask.
    */
   unsigned num_components;
   unsigned vec_swizzle[4] = { 0, 1, 2, 3 };
   if (nir_op_infos[alu->op].output_size == 0) {
      /* Figure out the swizzle we need on the vecN operation and compute
       * the number of components in the SSA def at the same time.
       */
      num_components = 0;
      for (unsigned index = 0; index < 4; index++) {
         if (write_mask & (1 << index))
            vec_swizzle[index] = num_components++;
      }

      /* When we change the output writemask, we need to change
       * the swizzles for per-component inputs too
       */
      for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
         if (nir_op_infos[alu->op].input_sizes[i] != 0)
            continue;

         /*
          * We keep two indices:
          * 1. The index of the original (non-SSA) component
          * 2. The index of the post-SSA, compacted, component
          *
          * We need to map the swizzle component at index 1 to the swizzle
          * component at index 2.  Since index 1 is always larger than
          * index 2, we can do it in a single loop.
          */

         unsigned ssa_index = 0;
         for (unsigned index = 0; index < 4; index++) {
            if (!((write_mask >> index) & 1))
               continue;

            alu->src[i].swizzle[ssa_index++] = alu->src[i].swizzle[index];
         }
         assert(ssa_index == num_components);
      }
   } else {
      num_components = nir_op_infos[alu->op].output_size;
   }
   assert(num_components <= 4);

   alu->dest.write_mask = (1 << num_components) - 1;
   list_del(&alu->dest.dest.reg.def_link);
   nir_ssa_dest_init(&alu->instr, &alu->dest.dest, num_components,
                     reg->bit_size, reg->name);

   nir_op vecN_op;
   switch (reg->num_components) {
   case 2: vecN_op = nir_op_vec2; break;
   case 3: vecN_op = nir_op_vec3; break;
   case 4: vecN_op = nir_op_vec4; break;
   default: unreachable("not reached");
   }

   nir_alu_instr *vec = nir_alu_instr_create(state->shader, vecN_op);

   nir_ssa_def *old_src =
      nir_phi_builder_value_get_block_def(value, alu->instr.block);
   nir_ssa_def *new_src = &alu->dest.dest.ssa;

   for (unsigned i = 0; i < reg->num_components; i++) {
      if (write_mask & (1 << i)) {
         vec->src[i].src = nir_src_for_ssa(new_src);
         vec->src[i].swizzle[0] = vec_swizzle[i];
      } else {
         vec->src[i].src = nir_src_for_ssa(old_src);
         vec->src[i].swizzle[0] = i;
      }
   }

   nir_ssa_dest_init(&vec->instr, &vec->dest.dest, reg->num_components,
                     reg->bit_size, reg->name);
   nir_instr_insert(nir_after_instr(&alu->instr), &vec->instr);

   nir_phi_builder_value_set_block_def(value, alu->instr.block,
                                       &vec->dest.dest.ssa);
}

void
nir_lower_regs_to_ssa_impl(nir_function_impl *impl)
{
   if (exec_list_is_empty(&impl->registers))
      return;

   nir_metadata_require(impl, nir_metadata_block_index |
                              nir_metadata_dominance);
   nir_index_local_regs(impl);

   struct regs_to_ssa_state state;
   state.shader = impl->function->shader;
   state.values = malloc(impl->reg_alloc * sizeof(*state.values));

   struct nir_phi_builder *phi_build = nir_phi_builder_create(impl);

   const unsigned block_set_words = BITSET_WORDS(impl->num_blocks);
   NIR_VLA(BITSET_WORD, defs, block_set_words);

   nir_foreach_register(reg, &impl->registers) {
      if (reg->num_array_elems != 0 || reg->is_packed) {
         /* This pass only really works on "plain" registers.  If it's a
          * packed or array register, just set the value to NULL so that the
          * rewrite portion of the pass will know to ignore it.
          */
         state.values[reg->index] = NULL;
         continue;
      }

      memset(defs, 0, block_set_words * sizeof(*defs));

      nir_foreach_def(dest, reg)
         BITSET_SET(defs, dest->reg.parent_instr->block->index);

      state.values[reg->index] =
         nir_phi_builder_add_value(phi_build, reg->num_components,
                                   reg->bit_size, defs);
   }

   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         if (instr->type == nir_instr_type_alu) {
            rewrite_alu_instr(nir_instr_as_alu(instr), &state);
         } else {
            nir_foreach_src(instr, rewrite_src, &state);
            nir_foreach_dest(instr, rewrite_dest, &state);
         }
      }

      nir_if *following_if = nir_block_get_following_if(block);
      if (following_if)
         rewrite_if_condition(following_if, &state);
   }

   nir_phi_builder_finish(phi_build);

   nir_foreach_register_safe(reg, &impl->registers) {
      if (state.values[reg->index]) {
         assert(list_empty(&reg->uses));
         assert(list_empty(&reg->if_uses));
         assert(list_empty(&reg->defs));
         exec_node_remove(&reg->node);
      }
   }

   free(state.values);

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
}

void
nir_lower_regs_to_ssa(nir_shader *shader)
{
   assert(exec_list_is_empty(&shader->registers));

   nir_foreach_function(function, shader) {
      if (function->impl)
         nir_lower_regs_to_ssa_impl(function->impl);
   }
}
