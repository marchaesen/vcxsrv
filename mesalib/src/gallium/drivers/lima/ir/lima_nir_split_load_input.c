/*
 * Copyright © 2019 Vasily Khoruzhick <anarsoul@gmail.com>
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
#include "nir_builder.h"

#include "lima_ir.h"

static bool
lima_nir_split_load_input_block(nir_block *block, nir_builder *b)
{
   bool progress = false;

   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_alu)
         continue;

      nir_alu_instr *alu = nir_instr_as_alu(instr);
      if (alu->op != nir_op_mov)
         continue;

      if (!alu->dest.dest.is_ssa)
         continue;

      if (!alu->src[0].src.is_ssa)
         continue;

      nir_ssa_def *ssa = alu->src[0].src.ssa;
      if (ssa->parent_instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(ssa->parent_instr);
      if (intrin->intrinsic != nir_intrinsic_load_input)
         continue;

      uint8_t swizzle = alu->src[0].swizzle[0];
      int i;

      for (i = 1; i < nir_dest_num_components(alu->dest.dest); i++)
         if (alu->src[0].swizzle[i] != (swizzle + i))
            break;

      if (i != nir_dest_num_components(alu->dest.dest))
         continue;

      /* mali4xx can't access unaligned vec3, don't split load input */
      if (nir_dest_num_components(alu->dest.dest) == 3 && swizzle > 0)
         continue;

      b->cursor = nir_before_instr(&intrin->instr);
      nir_intrinsic_instr *new_intrin = nir_intrinsic_instr_create(
                                             b->shader,
                                             intrin->intrinsic);
      nir_ssa_dest_init(&new_intrin->instr, &new_intrin->dest,
                        nir_dest_num_components(alu->dest.dest),
                        ssa->bit_size,
                        NULL);
      new_intrin->num_components = nir_dest_num_components(alu->dest.dest);
      nir_intrinsic_set_base(new_intrin, nir_intrinsic_base(intrin));
      nir_intrinsic_set_component(new_intrin, nir_intrinsic_component(intrin) + swizzle);
      nir_intrinsic_set_dest_type(new_intrin, nir_intrinsic_dest_type(intrin));

      /* offset */
      nir_src_copy(&new_intrin->src[0], &intrin->src[0], new_intrin);

      nir_builder_instr_insert(b, &new_intrin->instr);
      nir_ssa_def_rewrite_uses(&alu->dest.dest.ssa,
                               nir_src_for_ssa(&new_intrin->dest.ssa));
      nir_instr_remove(&alu->instr);
      progress = true;
   }

   return progress;
}

static bool
lima_nir_split_load_input_impl(nir_function_impl *impl)
{
   bool progress = false;
   nir_builder builder;
   nir_builder_init(&builder, impl);

   nir_foreach_block(block, impl) {
      progress |= lima_nir_split_load_input_block(block, &builder);
   }

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
   return progress;
}

/* Replaces a single load of several packed varyings and number of movs with
 * a number of loads of smaller size
 */
bool
lima_nir_split_load_input(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= lima_nir_split_load_input_impl(function->impl);
   }

   return progress;
}

