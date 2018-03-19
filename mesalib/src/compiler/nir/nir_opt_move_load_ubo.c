/*
 * Copyright © 2016 Intel Corporation
 * Copyright © 2018 Valve Corporation
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

#include "nir.h"

/**
 * \file nir_opt_move_load_ubo.c
 *
 * This pass moves load UBO operations just before their first use inside
 * the same basic block.
 */
static bool
move_load_ubo_source(nir_src *src, nir_block *block, nir_instr *before)
{
   if (!src->is_ssa)
      return false;

   nir_instr *src_instr = src->ssa->parent_instr;

   if (src_instr->block == block &&
       src_instr->type == nir_instr_type_intrinsic &&
       nir_instr_as_intrinsic(src_instr)->intrinsic == nir_intrinsic_load_ubo) {

      exec_node_remove(&src_instr->node);

      if (before)
         exec_node_insert_node_before(&before->node, &src_instr->node);
      else
         exec_list_push_tail(&block->instr_list, &src_instr->node);

      return true;
   }
   return false;
}

static bool
move_load_ubo_source_cb(nir_src *src, void *data)
{
   bool *progress = data;

   nir_instr *instr = src->parent_instr;
   if (move_load_ubo_source(src, instr->block, instr))
      *progress = true;

   return true; /* nir_foreach_src should keep going */
}

static bool
move_load_ubo(nir_block *block)
{
   bool progress = false;

   nir_if *iff = nir_block_get_following_if(block);
   if (iff) {
      progress |= move_load_ubo_source(&iff->condition, block, NULL);
   }

   nir_foreach_instr_reverse(instr, block) {

      if (instr->type == nir_instr_type_phi) {
         /* We're going backwards so everything else is a phi too */
      } else if (instr->type == nir_instr_type_alu) {
         nir_alu_instr *alu = nir_instr_as_alu(instr);

         for (int i = nir_op_infos[alu->op].num_inputs - 1; i >= 0; i--) {
            progress |= move_load_ubo_source(&alu->src[i].src, block, instr);
         }
      } else {
         nir_foreach_src(instr, move_load_ubo_source_cb, &progress);
      }
   }

   return false;
}

bool
nir_opt_move_load_ubo(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(func, shader) {
      if (!func->impl)
         continue;

      nir_foreach_block(block, func->impl) {
         if (move_load_ubo(block)) {
            nir_metadata_preserve(func->impl, nir_metadata_block_index |
                                              nir_metadata_dominance |
                                              nir_metadata_live_ssa_defs);
            progress = true;
         }
      }
   }

   return progress;
}
