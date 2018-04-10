/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

/*
 * Remap load_uniform intrinsics to UBO accesses of UBO binding point 0. Both
 * the base and the offset are interpreted as 16-byte units.
 *
 * Simultaneously, remap existing UBO accesses by increasing their binding
 * point by 1.
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "st_nir.h"

#include "program/prog_parameter.h"

static bool
lower_instr(nir_intrinsic_instr *instr, nir_builder *b)
{
   b->cursor = nir_before_instr(&instr->instr);

   if (instr->intrinsic == nir_intrinsic_load_ubo) {
      nir_ssa_def *old_idx = nir_ssa_for_src(b, instr->src[0], 1);
      nir_ssa_def *new_idx = nir_iadd(b, old_idx, nir_imm_int(b, 1));
      nir_instr_rewrite_src(&instr->instr, &instr->src[0],
                            nir_src_for_ssa(new_idx));
      return true;
   }

   if (instr->intrinsic == nir_intrinsic_load_uniform) {
      nir_ssa_def *ubo_idx = nir_imm_int(b, 0);
      nir_ssa_def *ubo_offset =
         nir_iadd(b, nir_imm_int(b, 4 * nir_intrinsic_base(instr)),
                  nir_imul(b, nir_imm_int(b, 4),
                           nir_ssa_for_src(b, instr->src[0], 1)));

      nir_intrinsic_instr *load =
         nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_ubo);
      load->num_components = instr->num_components;
      load->src[0] = nir_src_for_ssa(ubo_idx);
      load->src[1] = nir_src_for_ssa(ubo_offset);
      nir_ssa_dest_init(&load->instr, &load->dest,
                        load->num_components, instr->dest.ssa.bit_size,
                        instr->dest.ssa.name);
      nir_builder_instr_insert(b, &load->instr);
      nir_ssa_def_rewrite_uses(&instr->dest.ssa, nir_src_for_ssa(&load->dest.ssa));

      nir_instr_remove(&instr->instr);
      return true;
   }

   return false;
}

bool
st_nir_lower_uniforms_to_ubo(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_builder builder;
         nir_builder_init(&builder, function->impl);
         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               if (instr->type == nir_instr_type_intrinsic)
                  progress |= lower_instr(nir_instr_as_intrinsic(instr),
                                          &builder);
            }
         }

         nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                               nir_metadata_dominance);
      }
   }

   return progress;
}

