/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <assert.h>
#include <stdint.h>

#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "nir/nir_search_helpers.h"
#include "rogue_nir.h"
#include "rogue_nir_helpers.h"

static void lower_vulkan_resource_index(nir_builder *b,
                                        nir_intrinsic_instr *intr,
                                        void *pipeline_layout)
{
   unsigned desc_set = nir_intrinsic_desc_set(intr);
   unsigned binding = nir_intrinsic_binding(intr);

   nir_ssa_def *def = nir_vec3(b,
                               nir_imm_int(b, desc_set),
                               nir_imm_int(b, binding),
                               nir_imm_int(b, 0));
   nir_ssa_def_rewrite_uses(&intr->dest.ssa, def);
   nir_instr_remove(&intr->instr);
}

static void lower_load_vulkan_descriptor(nir_builder *b,
                                         nir_intrinsic_instr *intr)
{
   /* Loading the descriptor happens as part of the load/store instruction so
    * this is a no-op.
    */

   nir_ssa_def_rewrite_uses(&intr->dest.ssa, intr->src[0].ssa);
   nir_instr_remove(&intr->instr);
}

static void lower_load_ubo_to_scalar(nir_builder *b, nir_intrinsic_instr *intr)
{
   /* Scalarize the load_ubo. */
   b->cursor = nir_before_instr(&intr->instr);

   assert(intr->dest.is_ssa);
   assert(intr->num_components > 1);

   nir_ssa_def *loads[NIR_MAX_VEC_COMPONENTS];

   for (uint8_t i = 0; i < intr->num_components; i++) {
      size_t scaled_range = nir_intrinsic_range(intr) / intr->num_components;
      nir_intrinsic_instr *chan_intr =
         nir_intrinsic_instr_create(b->shader, intr->intrinsic);
      nir_ssa_dest_init(&chan_intr->instr,
                        &chan_intr->dest,
                        1,
                        intr->dest.ssa.bit_size,
                        NULL);
      chan_intr->num_components = 1;

      nir_intrinsic_set_access(chan_intr, nir_intrinsic_access(intr));
      nir_intrinsic_set_align_mul(chan_intr, nir_intrinsic_align_mul(intr));
      nir_intrinsic_set_align_offset(chan_intr,
                                     nir_intrinsic_align_offset(intr));
      nir_intrinsic_set_range_base(chan_intr,
                                   nir_intrinsic_range_base(intr) +
                                      (i * intr->num_components));
      nir_intrinsic_set_range(chan_intr, scaled_range);

      /* Base (desc_set, binding). */
      nir_src_copy(&chan_intr->src[0], &intr->src[0], &chan_intr->instr);

      /* Offset (unused). */
      chan_intr->src[1] = nir_src_for_ssa(nir_imm_int(b, 0));

      nir_builder_instr_insert(b, &chan_intr->instr);

      loads[i] = &chan_intr->dest.ssa;
   }

   nir_ssa_def_rewrite_uses(&intr->dest.ssa,
                            nir_vec(b, loads, intr->num_components));
   nir_instr_remove(&intr->instr);
}

static bool
lower_intrinsic(nir_builder *b, nir_intrinsic_instr *instr, void *layout)
{
   switch (instr->intrinsic) {
   case nir_intrinsic_load_vulkan_descriptor:
      lower_load_vulkan_descriptor(b, instr);
      return true;

   case nir_intrinsic_vulkan_resource_index:
      lower_vulkan_resource_index(b, instr, layout);
      return true;

   case nir_intrinsic_load_ubo:
      lower_load_ubo_to_scalar(b, instr);
      return true;

   default:
      break;
   }

   return false;
}

static bool lower_impl(nir_function_impl *impl, void *layout)
{
   bool progress = false;
   nir_builder b;

   nir_builder_init(&b, impl);

   nir_foreach_block (block, impl) {
      nir_foreach_instr_safe (instr, block) {
         b.cursor = nir_before_instr(instr);
         switch (instr->type) {
         case nir_instr_type_intrinsic:
            progress |=
               lower_intrinsic(&b, nir_instr_as_intrinsic(instr), layout);
            break;

         default:
            break;
         }
      }
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_none);
   else
      nir_metadata_preserve(impl, nir_metadata_all);

   return progress;
}

bool rogue_nir_lower_io(nir_shader *shader, void *layout)
{
   bool progress = false;

   nir_foreach_function (function, shader) {
      if (function->impl)
         progress |= lower_impl(function->impl, layout);
   }

   if (progress)
      nir_opt_dce(shader);

   return progress;
}
