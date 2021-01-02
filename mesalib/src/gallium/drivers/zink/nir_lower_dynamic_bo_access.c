/*
 * Copyright © 2020 Mike Blumenkrantz
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
 *    Mike Blumenkrantz <michael.blumenkrantz@gmail.com>
 */

#include "nir.h"
#include "nir_builder.h"

bool nir_lower_dynamic_bo_access(nir_shader *shader);
/**
 * This pass converts dynamic UBO/SSBO block indices to constant indices by generating
 * conditional chains which reduce to single values.
 *
 * This is needed by anything which intends to convert GLSL-like shaders to SPIRV,
 * as SPIRV requires explicit load points for UBO/SSBO variables and has no instruction for
 * loading based on an offset in the underlying driver's binding table
 */


/* generate a single ssa value which conditionally selects the right value that
 * was previously loaded by the load_ubo conditional chain
 */
static nir_ssa_def *
recursive_generate_bo_ssa_def(nir_builder *b, nir_intrinsic_instr *instr, nir_ssa_def *index, unsigned start, unsigned end)
{
   if (start == end - 1) {
      /* block index src is 1 for this op */
      unsigned block_idx = instr->intrinsic == nir_intrinsic_store_ssbo;
      nir_intrinsic_instr *new_instr = nir_intrinsic_instr_create(b->shader, instr->intrinsic);
      new_instr->src[block_idx] = nir_src_for_ssa(nir_imm_int(b, start));
      for (unsigned i = 0; i < nir_intrinsic_infos[instr->intrinsic].num_srcs; i++) {
         if (i != block_idx)
            nir_src_copy(&new_instr->src[i], &instr->src[i], &new_instr->instr);
      }
      if (instr->intrinsic != nir_intrinsic_load_ubo_vec4) {
         nir_intrinsic_set_align(new_instr, nir_intrinsic_align_mul(instr), nir_intrinsic_align_offset(instr));
         if (instr->intrinsic != nir_intrinsic_load_ssbo)
            nir_intrinsic_set_range(new_instr, nir_intrinsic_range(instr));
      }
      new_instr->num_components = instr->num_components;
      if (instr->intrinsic != nir_intrinsic_store_ssbo)
         nir_ssa_dest_init(&new_instr->instr, &new_instr->dest,
                           nir_dest_num_components(instr->dest),
                           nir_dest_bit_size(instr->dest), NULL);
      nir_builder_instr_insert(b, &new_instr->instr);
      return &new_instr->dest.ssa;
   }

   unsigned mid = start + (end - start) / 2;
   return nir_build_alu(b, nir_op_bcsel, nir_build_alu(b, nir_op_ilt, index, nir_imm_int(b, mid), NULL, NULL),
      recursive_generate_bo_ssa_def(b, instr, index, start, mid),
      recursive_generate_bo_ssa_def(b, instr, index, mid, end),
      NULL
   );
}

static bool
lower_dynamic_bo_access_instr(nir_intrinsic_instr *instr, nir_builder *b)
{
   if (instr->intrinsic != nir_intrinsic_load_ubo &&
       instr->intrinsic != nir_intrinsic_load_ubo_vec4 &&
       instr->intrinsic != nir_intrinsic_get_ssbo_size &&
       instr->intrinsic != nir_intrinsic_load_ssbo &&
       instr->intrinsic != nir_intrinsic_store_ssbo)
      return false;
   /* block index src is 1 for this op */
   unsigned block_idx = instr->intrinsic == nir_intrinsic_store_ssbo;
   if (nir_src_is_const(instr->src[block_idx]))
      return false;
   b->cursor = nir_after_instr(&instr->instr);
   bool ssbo_mode = instr->intrinsic != nir_intrinsic_load_ubo && instr->intrinsic != nir_intrinsic_load_ubo_vec4;
   unsigned first_idx = UINT_MAX, last_idx;
   if (ssbo_mode) {
      /* ssbo bindings don't always start at 0 */
      nir_foreach_variable_with_modes(var, b->shader, nir_var_mem_ssbo) {
         first_idx = var->data.binding;
         break;
      }
      assert(first_idx != UINT_MAX);
      last_idx = first_idx + b->shader->info.num_ssbos;
   } else {
      /* skip 0 index if uniform_0 is one we created previously */
      first_idx = !b->shader->info.first_ubo_is_default_ubo;
      last_idx = first_idx + b->shader->info.num_ubos;
   }

   /* now create the composite dest with a bcsel chain based on the original value */
   nir_ssa_def *new_dest = recursive_generate_bo_ssa_def(b, instr,
                                                       instr->src[block_idx].ssa,
                                                       first_idx, last_idx);

   if (instr->intrinsic != nir_intrinsic_store_ssbo)
      /* now use the composite dest in all cases where the original dest (from the dynamic index)
       * was used and remove the dynamically-indexed load_*bo instruction
       */
      nir_ssa_def_rewrite_uses_after(&instr->dest.ssa, nir_src_for_ssa(new_dest), &instr->instr);

   nir_instr_remove(&instr->instr);
   return true;
}

bool
nir_lower_dynamic_bo_access(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_builder builder;
         nir_builder_init(&builder, function->impl);
         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               if (instr->type == nir_instr_type_intrinsic)
                  progress |= lower_dynamic_bo_access_instr(
                                                  nir_instr_as_intrinsic(instr),
                                                  &builder);
            }
         }

         nir_metadata_preserve(function->impl, nir_metadata_dominance);
      }
   }

   return progress;
}
