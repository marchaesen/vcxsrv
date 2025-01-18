/*
 * Copyright © 2018 Red Hat
 * Copyright © 2019 Valve Corporation
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
 *    Rob Clark (robdclark@gmail.com>
 *    Daniel Schürmann (daniel.schuermann@campus.tu-berlin.de)
 *    Rhys Perry (pendingchaos02@gmail.com)
 *
 */

#include "nir.h"

/*
 * A simple pass that moves some instructions into the least common
 * anscestor of consuming instructions.
 */

/*
 * Detect whether a source is like a constant for the purposes of register
 * pressure calculations (e.g. can be remat anywhere effectively for free).
 */
static bool
is_constant_like(nir_src *src)
{
   /* Constants are constants */
   if (nir_src_is_const(*src))
      return true;

   /* Otherwise, look for constant-like intrinsics */
   nir_instr *parent = src->ssa->parent_instr;
   if (parent->type != nir_instr_type_intrinsic)
      return false;

   return (nir_instr_as_intrinsic(parent)->intrinsic ==
           nir_intrinsic_load_preamble);
}

static bool
can_sink_instr(nir_instr *instr, nir_move_options options, bool *can_mov_out_of_loop)
{
   /* Some intrinsic might require uniform sources and
    * moving out of loops can add divergence.
    */
   *can_mov_out_of_loop = true;
   switch (instr->type) {
   case nir_instr_type_load_const:
   case nir_instr_type_undef: {
      return options & nir_move_const_undef;
   }
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);

      if (nir_op_is_vec_or_mov(alu->op) || alu->op == nir_op_b2i32)
         return options & nir_move_copies;
      if (nir_alu_instr_is_comparison(alu))
         return options & nir_move_comparisons;

      /* Assuming that constants do not contribute to register pressure, it is
       * beneficial to sink ALU instructions where all but one source is
       * constant. Detect that case last.
       */
      if (!(options & nir_move_alu))
         return false;

      unsigned inputs = nir_op_infos[alu->op].num_inputs;
      unsigned constant_inputs = 0;

      for (unsigned i = 0; i < inputs; ++i) {
         if (is_constant_like(&alu->src[i].src))
            constant_inputs++;
      }

      return (constant_inputs + 1 >= inputs);
   }
   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      switch (intrin->intrinsic) {
      case nir_intrinsic_load_ubo:
      case nir_intrinsic_load_ubo_vec4:
         *can_mov_out_of_loop = false;
         return options & nir_move_load_ubo;
      case nir_intrinsic_load_ssbo:
         *can_mov_out_of_loop = false;
         return (options & nir_move_load_ssbo) && nir_intrinsic_can_reorder(intrin);
      case nir_intrinsic_load_input:
      case nir_intrinsic_load_per_primitive_input:
      case nir_intrinsic_load_interpolated_input:
      case nir_intrinsic_load_per_vertex_input:
      case nir_intrinsic_load_frag_coord:
      case nir_intrinsic_load_frag_coord_zw:
      case nir_intrinsic_load_pixel_coord:
         return options & nir_move_load_input;
      case nir_intrinsic_load_uniform:
      case nir_intrinsic_load_kernel_input:
         return options & nir_move_load_uniform;
      case nir_intrinsic_inverse_ballot:
      case nir_intrinsic_is_subgroup_invocation_lt_amd:
         *can_mov_out_of_loop = false;
         return options & nir_move_copies;
      case nir_intrinsic_load_constant_agx:
      case nir_intrinsic_load_local_pixel_agx:
         return true;
      default:
         return false;
      }
   }
   default:
      return false;
   }
}

bool
nir_can_move_instr(nir_instr *instr, nir_move_options options)
{
   bool out_of_loop;
   return can_sink_instr(instr, options, &out_of_loop);
}

static nir_loop *
get_innermost_loop(nir_cf_node *node)
{
   for (; node != NULL; node = node->parent) {
      if (node->type == nir_cf_node_loop) {
         nir_loop *loop = nir_cf_node_as_loop(node);
         if (nir_loop_first_block(loop)->predecessors->entries > 1)
            return loop;
      }
   }
   return NULL;
}

static bool
loop_contains_block(nir_loop *loop, nir_block *block)
{
   assert(!nir_loop_has_continue_construct(loop));
   nir_block *before = nir_cf_node_as_block(nir_cf_node_prev(&loop->cf_node));
   nir_block *after = nir_cf_node_as_block(nir_cf_node_next(&loop->cf_node));

   return block->index > before->index && block->index < after->index;
}

/* Given the LCA of all uses and the definition, find a block on the path
 * between them in the dominance tree that is outside of as many loops as
 * possible. If "sink_out_of_loops" is false, then we disallow sinking the
 * definition outside of the loop it's defined in (if any).
 */

static nir_block *
adjust_block_for_loops(nir_block *use_block, nir_block *def_block,
                       bool sink_out_of_loops)
{
   nir_loop *def_loop = NULL;
   if (!sink_out_of_loops)
      def_loop = get_innermost_loop(&def_block->cf_node);

   for (nir_block *cur_block = use_block; cur_block != def_block->imm_dom;
        cur_block = cur_block->imm_dom) {
      if (!sink_out_of_loops && def_loop &&
          !loop_contains_block(def_loop, use_block)) {
         use_block = cur_block;
         continue;
      }

      nir_cf_node *next = nir_cf_node_next(&cur_block->cf_node);
      if (next && next->type == nir_cf_node_loop &&
          nir_block_cf_tree_next(cur_block)->predecessors->entries > 1) {
         nir_loop *following_loop = nir_cf_node_as_loop(next);
         if (loop_contains_block(following_loop, use_block)) {
            use_block = cur_block;
            continue;
         }
      }
   }

   return use_block;
}

/* iterate a ssa def's use's and try to find a more optimal block to
 * move it to, using the dominance tree.  In short, if all of the uses
 * are contained in a single block, the load will be moved there,
 * otherwise it will be move to the least common ancestor block of all
 * the uses
 */
static nir_block *
get_preferred_block(nir_def *def, bool sink_out_of_loops)
{
   nir_block *lca = NULL;

   nir_foreach_use_including_if(use, def) {
      lca = nir_dominance_lca(lca, nir_src_get_block(use));
   }

   /* return in case, we didn't find a reachable user */
   if (!lca)
      return NULL;

   /* We don't sink any instructions into loops to avoid repeated executions
    * This might occasionally increase register pressure, but seems overall
    * the better choice.
    */
   lca = adjust_block_for_loops(lca, def->parent_instr->block,
                                sink_out_of_loops);
   assert(nir_block_dominates(def->parent_instr->block, lca));

   return lca;
}

bool
nir_opt_sink(nir_shader *shader, nir_move_options options)
{
   bool progress = false;

   nir_foreach_function_impl(impl, shader) {
      nir_metadata_require(impl,
                           nir_metadata_control_flow);

      nir_foreach_block_reverse(block, impl) {
         nir_foreach_instr_reverse_safe(instr, block) {
            bool sink_out_of_loops;
            if (!can_sink_instr(instr, options, &sink_out_of_loops))
               continue;

            nir_def *def = nir_instr_def(instr);

            nir_block *use_block =
               get_preferred_block(def, sink_out_of_loops);

            if (!use_block || use_block == instr->block)
               continue;

            nir_instr_remove(instr);
            nir_instr_insert(nir_after_phis(use_block), instr);

            progress = true;
         }
      }

      nir_metadata_preserve(impl,
                            nir_metadata_control_flow);
   }

   return progress;
}
