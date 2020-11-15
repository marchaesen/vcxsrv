/*
 * Copyright © 2016 Intel Corporation
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

/*
 * This lowering pass converts references to variables with loads/stores to
 * scratch space based on a few configurable parameters.
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_deref.h"

static void
lower_load_store(nir_builder *b,
                 nir_intrinsic_instr *intrin,
                 glsl_type_size_align_func size_align)
{
   b->cursor = nir_before_instr(&intrin->instr);

   nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
   nir_variable *var = nir_deref_instr_get_variable(deref);

   nir_ssa_def *offset =
      nir_iadd_imm(b, nir_build_deref_offset(b, deref, size_align),
                      var->data.location);

   unsigned align, UNUSED size;
   size_align(deref->type, &size, &align);

   if (intrin->intrinsic == nir_intrinsic_load_deref) {
      nir_intrinsic_instr *load =
         nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_scratch);
      load->num_components = intrin->num_components;
      load->src[0] = nir_src_for_ssa(offset);
      nir_intrinsic_set_align(load, align, 0);
      unsigned bit_size = intrin->dest.ssa.bit_size;
      nir_ssa_dest_init(&load->instr, &load->dest,
                        intrin->dest.ssa.num_components,
                        bit_size == 1 ? 32 : bit_size, NULL);
      nir_builder_instr_insert(b, &load->instr);

      nir_ssa_def *value = &load->dest.ssa;
      if (bit_size == 1)
         value = nir_b2b1(b, value);

      nir_ssa_def_rewrite_uses(&intrin->dest.ssa,
                               nir_src_for_ssa(value));
   } else {
      assert(intrin->intrinsic == nir_intrinsic_store_deref);

      assert(intrin->src[1].is_ssa);
      nir_ssa_def *value = intrin->src[1].ssa;
      if (value->bit_size == 1)
         value = nir_b2b32(b, value);

      nir_intrinsic_instr *store =
         nir_intrinsic_instr_create(b->shader, nir_intrinsic_store_scratch);
      store->num_components = intrin->num_components;
      store->src[0] = nir_src_for_ssa(value);
      store->src[1] = nir_src_for_ssa(offset);
      nir_intrinsic_set_write_mask(store, nir_intrinsic_write_mask(intrin));
      nir_intrinsic_set_align(store, align, 0);
      nir_builder_instr_insert(b, &store->instr);
   }

   nir_instr_remove(&intrin->instr);
   nir_deref_instr_remove_if_unused(deref);
}

bool
nir_lower_vars_to_scratch(nir_shader *shader,
                          nir_variable_mode modes,
                          int size_threshold,
                          glsl_type_size_align_func size_align)
{
   /* First, we walk the instructions and flag any variables we want to lower
    * by removing them from their respective list and setting the mode to 0.
    */
   nir_foreach_function(function, shader) {
      nir_foreach_block(block, function->impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_load_deref &&
                intrin->intrinsic != nir_intrinsic_store_deref)
               continue;

            nir_deref_instr *deref = nir_src_as_deref(intrin->src[0]);
            if (!nir_deref_mode_is_one_of(deref, modes))
               continue;

            if (!nir_deref_instr_has_indirect(nir_src_as_deref(intrin->src[0])))
               continue;

            nir_variable *var = nir_deref_instr_get_variable(deref);

            /* We set var->mode to 0 to indicate that a variable will be moved
             * to scratch.  Don't assign a scratch location twice.
             */
            if (var->data.mode == 0)
               continue;

            unsigned var_size, var_align;
            size_align(var->type, &var_size, &var_align);
            if (var_size <= size_threshold)
               continue;

            /* Remove it from its list */
            exec_node_remove(&var->node);
            /* Invalid mode used to flag "moving to scratch" */
            var->data.mode = 0;

            var->data.location = ALIGN_POT(shader->scratch_size, var_align);
            shader->scratch_size = var->data.location + var_size;
         }
      }
   }

   bool progress = false;
   nir_foreach_function(function, shader) {
      if (!function->impl)
         continue;

      nir_builder build;
      nir_builder_init(&build, function->impl);

      bool impl_progress = false;
      nir_foreach_block(block, function->impl) {
         nir_foreach_instr_safe(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            if (intrin->intrinsic != nir_intrinsic_load_deref &&
                intrin->intrinsic != nir_intrinsic_store_deref)
               continue;

            nir_variable *var = nir_intrinsic_get_var(intrin, 0);
            /* Variables flagged for lowering above have mode == 0 */
            if (!var || var->data.mode)
               continue;

            lower_load_store(&build, intrin, size_align);
            impl_progress = true;
         }
      }

      if (impl_progress) {
         progress = true;
         nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                               nir_metadata_dominance);
      } else {
         nir_metadata_preserve(function->impl, nir_metadata_all);
      }
   }

   return progress;
}
