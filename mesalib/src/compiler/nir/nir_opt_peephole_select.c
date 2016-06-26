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
#include "nir_control_flow.h"

/*
 * Implements a small peephole optimization that looks for
 *
 * if (cond) {
 *    <empty>
 * } else {
 *    <empty>
 * }
 * phi
 * ...
 * phi
 *
 * and replaces it with a series of selects.  It can also handle the case
 * where, instead of being empty, the if may contain some move operations
 * whose only use is one of the following phi nodes.  This happens all the
 * time when the SSA form comes from a conditional assignment with a
 * swizzle.
 */

static bool
block_check_for_allowed_instrs(nir_block *block)
{
   nir_foreach_instr(instr, block) {
      switch (instr->type) {
      case nir_instr_type_intrinsic: {
         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

         switch (intrin->intrinsic) {
         case nir_intrinsic_load_var:
            switch (intrin->variables[0]->var->data.mode) {
            case nir_var_shader_in:
            case nir_var_uniform:
               break;

            default:
               return false;
            }
            break;

         default:
            return false;
         }

         break;
      }

      case nir_instr_type_load_const:
         break;

      case nir_instr_type_alu: {
         nir_alu_instr *mov = nir_instr_as_alu(instr);
         switch (mov->op) {
         case nir_op_fmov:
         case nir_op_imov:
         case nir_op_fneg:
         case nir_op_ineg:
         case nir_op_fabs:
         case nir_op_iabs:
         case nir_op_vec2:
         case nir_op_vec3:
         case nir_op_vec4:
            /* It must be a move-like operation. */
            break;
         default:
            return false;
         }

         /* Can't handle saturate */
         if (mov->dest.saturate)
            return false;

         /* It must be SSA */
         if (!mov->dest.dest.is_ssa)
            return false;

         /* It cannot have any if-uses */
         if (!list_empty(&mov->dest.dest.ssa.if_uses))
            return false;

         /* The only uses of this definition must be phi's in the successor */
         nir_foreach_use(use, &mov->dest.dest.ssa) {
            if (use->parent_instr->type != nir_instr_type_phi ||
                use->parent_instr->block != block->successors[0])
               return false;
         }
         break;
      }

      default:
         return false;
      }
   }

   return true;
}

static bool
nir_opt_peephole_select_block(nir_block *block, void *mem_ctx)
{
   /* If the block is empty, then it certainly doesn't have any phi nodes,
    * so we can skip it.  This also ensures that we do an early skip on the
    * end block of the function which isn't actually attached to the CFG.
    */
   if (exec_list_is_empty(&block->instr_list))
      return false;

   if (nir_cf_node_is_first(&block->cf_node))
      return false;

   nir_cf_node *prev_node = nir_cf_node_prev(&block->cf_node);
   if (prev_node->type != nir_cf_node_if)
      return false;

   nir_if *if_stmt = nir_cf_node_as_if(prev_node);
   nir_cf_node *then_node = nir_if_first_then_node(if_stmt);
   nir_cf_node *else_node = nir_if_first_else_node(if_stmt);

   /* We can only have one block in each side ... */
   if (nir_if_last_then_node(if_stmt) != then_node ||
       nir_if_last_else_node(if_stmt) != else_node)
      return false;

   nir_block *then_block = nir_cf_node_as_block(then_node);
   nir_block *else_block = nir_cf_node_as_block(else_node);

   /* ... and those blocks must only contain "allowed" instructions. */
   if (!block_check_for_allowed_instrs(then_block) ||
       !block_check_for_allowed_instrs(else_block))
      return false;

   /* At this point, we know that the previous CFG node is an if-then
    * statement containing only moves to phi nodes in this block.  We can
    * just remove that entire CF node and replace all of the phi nodes with
    * selects.
    */

   nir_block *prev_block = nir_cf_node_as_block(nir_cf_node_prev(prev_node));
   assert(prev_block->cf_node.type == nir_cf_node_block);

   /* First, we move the remaining instructions from the blocks to the
    * block before.  We have already guaranteed that this is safe by
    * calling block_check_for_allowed_instrs()
    */
   nir_foreach_instr_safe(instr, then_block) {
      exec_node_remove(&instr->node);
      instr->block = prev_block;
      exec_list_push_tail(&prev_block->instr_list, &instr->node);
   }

   nir_foreach_instr_safe(instr, else_block) {
      exec_node_remove(&instr->node);
      instr->block = prev_block;
      exec_list_push_tail(&prev_block->instr_list, &instr->node);
   }

   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_phi)
         break;

      nir_phi_instr *phi = nir_instr_as_phi(instr);
      nir_alu_instr *sel = nir_alu_instr_create(mem_ctx, nir_op_bcsel);
      nir_src_copy(&sel->src[0].src, &if_stmt->condition, sel);
      /* Splat the condition to all channels */
      memset(sel->src[0].swizzle, 0, sizeof sel->src[0].swizzle);

      assert(exec_list_length(&phi->srcs) == 2);
      nir_foreach_phi_src(src, phi) {
         assert(src->pred == then_block || src->pred == else_block);
         assert(src->src.is_ssa);

         unsigned idx = src->pred == then_block ? 1 : 2;
         nir_src_copy(&sel->src[idx].src, &src->src, sel);
      }

      nir_ssa_dest_init(&sel->instr, &sel->dest.dest,
                        phi->dest.ssa.num_components,
                        phi->dest.ssa.bit_size, phi->dest.ssa.name);
      sel->dest.write_mask = (1 << phi->dest.ssa.num_components) - 1;

      nir_ssa_def_rewrite_uses(&phi->dest.ssa,
                               nir_src_for_ssa(&sel->dest.dest.ssa));

      nir_instr_insert_before(&phi->instr, &sel->instr);
      nir_instr_remove(&phi->instr);
   }

   nir_cf_node_remove(&if_stmt->cf_node);
   return true;
}

static bool
nir_opt_peephole_select_impl(nir_function_impl *impl)
{
   void *mem_ctx = ralloc_parent(impl);
   bool progress = false;

   nir_foreach_block_safe(block, impl) {
      progress |= nir_opt_peephole_select_block(block, mem_ctx);
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_none);

   return progress;
}

bool
nir_opt_peephole_select(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= nir_opt_peephole_select_impl(function->impl);
   }

   return progress;
}
