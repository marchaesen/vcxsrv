/*
 * Copyright © 2015 Intel Corporation
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

#include "nir.h"

/*
 * Implements a pass that lowers vector phi nodes to scalar phi nodes when
 * we don't think it will hurt anything.
 */

struct lower_phis_to_scalar_state {
   void *mem_ctx;
   void *dead_ctx;

   /* Hash table marking which phi nodes are scalarizable.  The key is
    * pointers to phi instructions and the entry is either NULL for not
    * scalarizable or non-null for scalarizable.
    */
   struct hash_table *phi_table;
};

static bool
should_lower_phi(nir_phi_instr *phi, struct lower_phis_to_scalar_state *state);

static bool
is_phi_src_scalarizable(nir_phi_src *src,
                        struct lower_phis_to_scalar_state *state)
{
   /* Don't know what to do with non-ssa sources */
   if (!src->src.is_ssa)
      return false;

   nir_instr *src_instr = src->src.ssa->parent_instr;
   switch (src_instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *src_alu = nir_instr_as_alu(src_instr);

      /* ALU operations with output_size == 0 should be scalarized.  We
       * will also see a bunch of vecN operations from scalarizing ALU
       * operations and, since they can easily be copy-propagated, they
       * are ok too.
       */
      return nir_op_infos[src_alu->op].output_size == 0 ||
             src_alu->op == nir_op_vec2 ||
             src_alu->op == nir_op_vec3 ||
             src_alu->op == nir_op_vec4;
   }

   case nir_instr_type_phi:
      /* A phi is scalarizable if we're going to lower it */
      return should_lower_phi(nir_instr_as_phi(src_instr), state);

   case nir_instr_type_load_const:
   case nir_instr_type_ssa_undef:
      /* These are trivially scalarizable */
      return true;

   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *src_intrin = nir_instr_as_intrinsic(src_instr);

      switch (src_intrin->intrinsic) {
      case nir_intrinsic_load_var:
         return src_intrin->variables[0]->var->data.mode == nir_var_shader_in ||
                src_intrin->variables[0]->var->data.mode == nir_var_uniform;

      case nir_intrinsic_interp_var_at_centroid:
      case nir_intrinsic_interp_var_at_sample:
      case nir_intrinsic_interp_var_at_offset:
      case nir_intrinsic_load_uniform:
      case nir_intrinsic_load_uniform_indirect:
      case nir_intrinsic_load_ubo:
      case nir_intrinsic_load_ubo_indirect:
      case nir_intrinsic_load_input:
      case nir_intrinsic_load_input_indirect:
         return true;
      default:
         break;
      }
   }

   default:
      /* We can't scalarize this type of instruction */
      return false;
   }
}

/**
 * Determines if the given phi node should be lowered.  The only phi nodes
 * we will scalarize at the moment are those where all of the sources are
 * scalarizable.
 *
 * The reason for this comes down to coalescing.  Since phi sources can't
 * swizzle, swizzles on phis have to be resolved by inserting a mov right
 * before the phi.  The choice then becomes between movs to pick off
 * components for a scalar phi or potentially movs to recombine components
 * for a vector phi.  The problem is that the movs generated to pick off
 * the components are almost uncoalescable.  We can't coalesce them in NIR
 * because we need them to pick off components and we can't coalesce them
 * in the backend because the source register is a vector and the
 * destination is a scalar that may be used at other places in the program.
 * On the other hand, if we have a bunch of scalars going into a vector
 * phi, the situation is much better.  In this case, if the SSA def is
 * generated in the predecessor block to the corresponding phi source, the
 * backend code will be an ALU op into a temporary and then a mov into the
 * given vector component;  this move can almost certainly be coalesced
 * away.
 */
static bool
should_lower_phi(nir_phi_instr *phi, struct lower_phis_to_scalar_state *state)
{
   /* Already scalar */
   if (phi->dest.ssa.num_components == 1)
      return false;

   struct hash_entry *entry = _mesa_hash_table_search(state->phi_table, phi);
   if (entry)
      return entry->data != NULL;

   /* Insert an entry and mark it as scalarizable for now. That way
    * we don't recurse forever and a cycle in the dependence graph
    * won't automatically make us fail to scalarize.
    */
   entry = _mesa_hash_table_insert(state->phi_table, phi, (void *)(intptr_t)1);

   bool scalarizable = true;

   nir_foreach_phi_src(phi, src) {
      scalarizable = is_phi_src_scalarizable(src, state);
      if (!scalarizable)
         break;
   }

   /* The hash table entry for 'phi' may have changed while recursing the
    * dependence graph, so we need to reset it */
   entry = _mesa_hash_table_search(state->phi_table, phi);
   assert(entry);

   entry->data = (void *)(intptr_t)scalarizable;

   return scalarizable;
}

static bool
lower_phis_to_scalar_block(nir_block *block, void *void_state)
{
   struct lower_phis_to_scalar_state *state = void_state;

   /* Find the last phi node in the block */
   nir_phi_instr *last_phi = NULL;
   nir_foreach_instr(block, instr) {
      if (instr->type != nir_instr_type_phi)
         break;

      last_phi = nir_instr_as_phi(instr);
   }

   /* We have to handle the phi nodes in their own pass due to the way
    * we're modifying the linked list of instructions.
    */
   nir_foreach_instr_safe(block, instr) {
      if (instr->type != nir_instr_type_phi)
         break;

      nir_phi_instr *phi = nir_instr_as_phi(instr);

      if (!should_lower_phi(phi, state))
         continue;

      /* Create a vecN operation to combine the results.  Most of these
       * will be redundant, but copy propagation should clean them up for
       * us.  No need to add the complexity here.
       */
      nir_op vec_op;
      switch (phi->dest.ssa.num_components) {
      case 2: vec_op = nir_op_vec2; break;
      case 3: vec_op = nir_op_vec3; break;
      case 4: vec_op = nir_op_vec4; break;
      default: unreachable("Invalid number of components");
      }

      nir_alu_instr *vec = nir_alu_instr_create(state->mem_ctx, vec_op);
      nir_ssa_dest_init(&vec->instr, &vec->dest.dest,
                        phi->dest.ssa.num_components, NULL);
      vec->dest.write_mask = (1 << phi->dest.ssa.num_components) - 1;

      for (unsigned i = 0; i < phi->dest.ssa.num_components; i++) {
         nir_phi_instr *new_phi = nir_phi_instr_create(state->mem_ctx);
         nir_ssa_dest_init(&new_phi->instr, &new_phi->dest, 1, NULL);

         vec->src[i].src = nir_src_for_ssa(&new_phi->dest.ssa);

         nir_foreach_phi_src(phi, src) {
            /* We need to insert a mov to grab the i'th component of src */
            nir_alu_instr *mov = nir_alu_instr_create(state->mem_ctx,
                                                      nir_op_imov);
            nir_ssa_dest_init(&mov->instr, &mov->dest.dest, 1, NULL);
            mov->dest.write_mask = 1;
            nir_src_copy(&mov->src[0].src, &src->src, state->mem_ctx);
            mov->src[0].swizzle[0] = i;

            /* Insert at the end of the predecessor but before the jump */
            nir_instr *pred_last_instr = nir_block_last_instr(src->pred);
            if (pred_last_instr && pred_last_instr->type == nir_instr_type_jump)
               nir_instr_insert_before(pred_last_instr, &mov->instr);
            else
               nir_instr_insert_after_block(src->pred, &mov->instr);

            nir_phi_src *new_src = ralloc(new_phi, nir_phi_src);
            new_src->pred = src->pred;
            new_src->src = nir_src_for_ssa(&mov->dest.dest.ssa);

            exec_list_push_tail(&new_phi->srcs, &new_src->node);
         }

         nir_instr_insert_before(&phi->instr, &new_phi->instr);
      }

      nir_instr_insert_after(&last_phi->instr, &vec->instr);

      nir_ssa_def_rewrite_uses(&phi->dest.ssa,
                               nir_src_for_ssa(&vec->dest.dest.ssa),
                               state->mem_ctx);

      ralloc_steal(state->dead_ctx, phi);
      nir_instr_remove(&phi->instr);

      /* We're using the safe iterator and inserting all the newly
       * scalarized phi nodes before their non-scalarized version so that's
       * ok.  However, we are also inserting vec operations after all of
       * the last phi node so once we get here, we can't trust even the
       * safe iterator to stop properly.  We have to break manually.
       */
      if (instr == &last_phi->instr)
         break;
   }

   return true;
}

static void
lower_phis_to_scalar_impl(nir_function_impl *impl)
{
   struct lower_phis_to_scalar_state state;

   state.mem_ctx = ralloc_parent(impl);
   state.dead_ctx = ralloc_context(NULL);
   state.phi_table = _mesa_hash_table_create(state.dead_ctx, _mesa_hash_pointer,
                                             _mesa_key_pointer_equal);

   nir_foreach_block(impl, lower_phis_to_scalar_block, &state);

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);

   ralloc_free(state.dead_ctx);
}

/** A pass that lowers vector phi nodes to scalar
 *
 * This pass loops through the blocks and lowers looks for vector phi nodes
 * it can lower to scalar phi nodes.  Not all phi nodes are lowered.  For
 * instance, if one of the sources is a non-scalarizable vector, then we
 * don't bother lowering because that would generate hard-to-coalesce movs.
 */
void
nir_lower_phis_to_scalar(nir_shader *shader)
{
   nir_foreach_overload(shader, overload) {
      if (overload->impl)
         lower_phis_to_scalar_impl(overload->impl);
   }
}
