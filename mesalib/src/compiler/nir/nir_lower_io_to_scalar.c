/*
 * Copyright © 2016 Broadcom
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

/** @file nir_lower_io_to_scalar.c
 *
 * Replaces nir_load_input/nir_store_output operations with num_components !=
 * 1 with individual per-channel operations.
 */

static void
lower_load_input_to_scalar(nir_builder *b, nir_intrinsic_instr *intr)
{
   b->cursor = nir_before_instr(&intr->instr);

   assert(intr->dest.is_ssa);

   nir_ssa_def *loads[4];

   for (unsigned i = 0; i < intr->num_components; i++) {
      nir_intrinsic_instr *chan_intr =
         nir_intrinsic_instr_create(b->shader, intr->intrinsic);
      nir_ssa_dest_init(&chan_intr->instr, &chan_intr->dest,
                        1, intr->dest.ssa.bit_size, NULL);
      chan_intr->num_components = 1;

      nir_intrinsic_set_base(chan_intr, nir_intrinsic_base(intr));
      nir_intrinsic_set_component(chan_intr, nir_intrinsic_component(intr) + i);
      /* offset */
      chan_intr->src[0] = intr->src[0];

      nir_builder_instr_insert(b, &chan_intr->instr);

      loads[i] = &chan_intr->dest.ssa;
   }

   nir_ssa_def_rewrite_uses(&intr->dest.ssa,
                            nir_src_for_ssa(nir_vec(b, loads,
                                                    intr->num_components)));
   nir_instr_remove(&intr->instr);
}

static void
lower_store_output_to_scalar(nir_builder *b, nir_intrinsic_instr *intr)
{
   b->cursor = nir_before_instr(&intr->instr);

   nir_ssa_def *value = nir_ssa_for_src(b, intr->src[0], intr->num_components);

   for (unsigned i = 0; i < intr->num_components; i++) {
      if (!(nir_intrinsic_write_mask(intr) & (1 << i)))
         continue;

      nir_intrinsic_instr *chan_intr =
         nir_intrinsic_instr_create(b->shader, intr->intrinsic);
      chan_intr->num_components = 1;

      nir_intrinsic_set_base(chan_intr, nir_intrinsic_base(intr));
      nir_intrinsic_set_write_mask(chan_intr, 0x1);
      nir_intrinsic_set_component(chan_intr, nir_intrinsic_component(intr) + i);

      /* value */
      chan_intr->src[0] = nir_src_for_ssa(nir_channel(b, value, i));
      /* offset */
      chan_intr->src[1] = intr->src[1];

      nir_builder_instr_insert(b, &chan_intr->instr);
   }

   nir_instr_remove(&intr->instr);
}

void
nir_lower_io_to_scalar(nir_shader *shader, nir_variable_mode mask)
{
   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_builder b;
         nir_builder_init(&b, function->impl);

         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               if (instr->type != nir_instr_type_intrinsic)
                  continue;

               nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

               if (intr->num_components == 1)
                  continue;

               switch (intr->intrinsic) {
               case nir_intrinsic_load_input:
                  if (mask & nir_var_shader_in)
                     lower_load_input_to_scalar(&b, intr);
                  break;
               case nir_intrinsic_store_output:
                  if (mask & nir_var_shader_out)
                     lower_store_output_to_scalar(&b, intr);
                  break;
               default:
                  break;
               }
            }
         }
      }
   }
}
