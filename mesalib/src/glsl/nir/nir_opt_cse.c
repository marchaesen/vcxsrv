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

#include "nir.h"

/*
 * Implements common subexpression elimination
 */

struct cse_state {
   void *mem_ctx;
   bool progress;
};

static bool
nir_alu_srcs_equal(nir_alu_src src1, nir_alu_src src2, uint8_t read_mask)
{
   if (src1.abs != src2.abs || src1.negate != src2.negate)
      return false;

   for (int i = 0; i < 4; ++i) {
      if (!(read_mask & (1 << i)))
         continue;

      if (src1.swizzle[i] != src2.swizzle[i])
         return false;
   }

   return nir_srcs_equal(src1.src, src2.src);
}

static bool
nir_instrs_equal(nir_instr *instr1, nir_instr *instr2)
{
   if (instr1->type != instr2->type)
      return false;

   switch (instr1->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu1 = nir_instr_as_alu(instr1);
      nir_alu_instr *alu2 = nir_instr_as_alu(instr2);

      if (alu1->op != alu2->op)
         return false;

      /* TODO: We can probably acutally do something more inteligent such
       * as allowing different numbers and taking a maximum or something
       * here */
      if (alu1->dest.dest.ssa.num_components != alu2->dest.dest.ssa.num_components)
         return false;

      for (unsigned i = 0; i < nir_op_infos[alu1->op].num_inputs; i++) {
         if (!nir_alu_srcs_equal(alu1->src[i], alu2->src[i],
                                 (1 << alu1->dest.dest.ssa.num_components) - 1))
            return false;
      }
      return true;
   }
   case nir_instr_type_tex:
      return false;
   case nir_instr_type_load_const: {
      nir_load_const_instr *load1 = nir_instr_as_load_const(instr1);
      nir_load_const_instr *load2 = nir_instr_as_load_const(instr2);

      if (load1->def.num_components != load2->def.num_components)
         return false;

      return memcmp(load1->value.f, load2->value.f,
                    load1->def.num_components * sizeof(*load2->value.f)) == 0;
   }
   case nir_instr_type_phi: {
      nir_phi_instr *phi1 = nir_instr_as_phi(instr1);
      nir_phi_instr *phi2 = nir_instr_as_phi(instr2);

      if (phi1->instr.block != phi2->instr.block)
         return false;

      nir_foreach_phi_src(phi1, src1) {
         nir_foreach_phi_src(phi2, src2) {
            if (src1->pred == src2->pred) {
               if (!nir_srcs_equal(src1->src, src2->src))
                  return false;

               break;
            }
         }
      }

      return true;
   }
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrinsic1 = nir_instr_as_intrinsic(instr1);
      nir_intrinsic_instr *intrinsic2 = nir_instr_as_intrinsic(instr2);
      const nir_intrinsic_info *info =
         &nir_intrinsic_infos[intrinsic1->intrinsic];

      if (intrinsic1->intrinsic != intrinsic2->intrinsic ||
          intrinsic1->num_components != intrinsic2->num_components)
         return false;

      if (info->has_dest && intrinsic1->dest.ssa.num_components !=
                            intrinsic2->dest.ssa.num_components)
         return false;

      for (unsigned i = 0; i < info->num_srcs; i++) {
         if (!nir_srcs_equal(intrinsic1->src[i], intrinsic2->src[i]))
            return false;
      }

      assert(info->num_variables == 0);

      for (unsigned i = 0; i < info->num_indices; i++) {
         if (intrinsic1->const_index[i] != intrinsic2->const_index[i])
            return false;
      }

      return true;
   }
   case nir_instr_type_call:
   case nir_instr_type_jump:
   case nir_instr_type_ssa_undef:
   case nir_instr_type_parallel_copy:
   default:
      unreachable("Invalid instruction type");
   }

   return false;
}

static bool
src_is_ssa(nir_src *src, void *data)
{
   return src->is_ssa;
}

static bool
dest_is_ssa(nir_dest *dest, void *data)
{
   return dest->is_ssa;
}

static bool
nir_instr_can_cse(nir_instr *instr)
{
   /* We only handle SSA. */
   if (!nir_foreach_dest(instr, dest_is_ssa, NULL) ||
       !nir_foreach_src(instr, src_is_ssa, NULL))
      return false;

   switch (instr->type) {
   case nir_instr_type_alu:
   case nir_instr_type_load_const:
   case nir_instr_type_phi:
      return true;
   case nir_instr_type_tex:
      return false; /* TODO */
   case nir_instr_type_intrinsic: {
      const nir_intrinsic_info *info =
         &nir_intrinsic_infos[nir_instr_as_intrinsic(instr)->intrinsic];
      return (info->flags & NIR_INTRINSIC_CAN_ELIMINATE) &&
             (info->flags & NIR_INTRINSIC_CAN_REORDER) &&
             info->num_variables == 0; /* not implemented yet */
   }
   case nir_instr_type_call:
   case nir_instr_type_jump:
   case nir_instr_type_ssa_undef:
      return false;
   case nir_instr_type_parallel_copy:
   default:
      unreachable("Invalid instruction type");
   }

   return false;
}

static nir_ssa_def *
nir_instr_get_dest_ssa_def(nir_instr *instr)
{
   switch (instr->type) {
   case nir_instr_type_alu:
      assert(nir_instr_as_alu(instr)->dest.dest.is_ssa);
      return &nir_instr_as_alu(instr)->dest.dest.ssa;
   case nir_instr_type_load_const:
      return &nir_instr_as_load_const(instr)->def;
   case nir_instr_type_phi:
      assert(nir_instr_as_phi(instr)->dest.is_ssa);
      return &nir_instr_as_phi(instr)->dest.ssa;
   case nir_instr_type_intrinsic:
      assert(nir_instr_as_intrinsic(instr)->dest.is_ssa);
      return &nir_instr_as_intrinsic(instr)->dest.ssa;
   default:
      unreachable("We never ask for any of these");
   }
}

static void
nir_opt_cse_instr(nir_instr *instr, struct cse_state *state)
{
   if (!nir_instr_can_cse(instr))
      return;

   for (struct exec_node *node = instr->node.prev;
        !exec_node_is_head_sentinel(node); node = node->prev) {
      nir_instr *other = exec_node_data(nir_instr, node, node);
      if (nir_instrs_equal(instr, other)) {
         nir_ssa_def *other_def = nir_instr_get_dest_ssa_def(other);
         nir_ssa_def_rewrite_uses(nir_instr_get_dest_ssa_def(instr),
                                  nir_src_for_ssa(other_def),
                                  state->mem_ctx);
         nir_instr_remove(instr);
         state->progress = true;
         return;
      }
   }

   for (nir_block *block = instr->block->imm_dom;
        block != NULL; block = block->imm_dom) {
      nir_foreach_instr_reverse(block, other) {
         if (nir_instrs_equal(instr, other)) {
            nir_ssa_def *other_def = nir_instr_get_dest_ssa_def(other);
            nir_ssa_def_rewrite_uses(nir_instr_get_dest_ssa_def(instr),
                                     nir_src_for_ssa(other_def),
                                     state->mem_ctx);
            nir_instr_remove(instr);
            state->progress = true;
            return;
         }
      }
   }
}

static bool
nir_opt_cse_block(nir_block *block, void *void_state)
{
   struct cse_state *state = void_state;

   nir_foreach_instr_safe(block, instr)
      nir_opt_cse_instr(instr, state);

   return true;
}

static bool
nir_opt_cse_impl(nir_function_impl *impl)
{
   struct cse_state state;

   state.mem_ctx = ralloc_parent(impl);
   state.progress = false;

   nir_metadata_require(impl, nir_metadata_dominance);

   nir_foreach_block(impl, nir_opt_cse_block, &state);

   if (state.progress)
      nir_metadata_preserve(impl, nir_metadata_block_index |
                                  nir_metadata_dominance);

   return state.progress;
}

bool
nir_opt_cse(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_overload(shader, overload) {
      if (overload->impl)
         progress |= nir_opt_cse_impl(overload->impl);
   }

   return progress;
}
