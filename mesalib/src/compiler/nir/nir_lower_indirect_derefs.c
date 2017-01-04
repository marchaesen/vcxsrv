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
 */

#include "nir.h"
#include "nir_builder.h"

static void
emit_load_store(nir_builder *b, nir_intrinsic_instr *orig_instr,
                nir_deref_var *deref, nir_deref *tail,
                nir_ssa_def **dest, nir_ssa_def *src);

static void
emit_indirect_load_store(nir_builder *b, nir_intrinsic_instr *orig_instr,
                         nir_deref_var *deref, nir_deref *arr_parent,
                         int start, int end,
                         nir_ssa_def **dest, nir_ssa_def *src)
{
   nir_deref_array *arr = nir_deref_as_array(arr_parent->child);
   assert(arr->deref_array_type == nir_deref_array_type_indirect);
   assert(arr->indirect.is_ssa);

   assert(start < end);
   if (start == end - 1) {
      /* Base case.  Just emit the load/store op */
      nir_deref_array direct = *arr;
      direct.deref_array_type = nir_deref_array_type_direct;
      direct.base_offset += start;
      direct.indirect = NIR_SRC_INIT;

      arr_parent->child = &direct.deref;
      emit_load_store(b, orig_instr, deref, &direct.deref, dest, src);
      arr_parent->child = &arr->deref;
   } else {
      int mid = start + (end - start) / 2;

      nir_ssa_def *then_dest, *else_dest;

      nir_if *if_stmt = nir_if_create(b->shader);
      if_stmt->condition = nir_src_for_ssa(nir_ilt(b, arr->indirect.ssa,
                                                      nir_imm_int(b, mid)));
      nir_cf_node_insert(b->cursor, &if_stmt->cf_node);

      b->cursor = nir_after_cf_list(&if_stmt->then_list);
      emit_indirect_load_store(b, orig_instr, deref, arr_parent,
                               start, mid, &then_dest, src);

      b->cursor = nir_after_cf_list(&if_stmt->else_list);
      emit_indirect_load_store(b, orig_instr, deref, arr_parent,
                               mid, end, &else_dest, src);

      b->cursor = nir_after_cf_node(&if_stmt->cf_node);

      if (src == NULL) {
         /* We're a load.  We need to insert a phi node */
         nir_phi_instr *phi = nir_phi_instr_create(b->shader);
         unsigned bit_size = then_dest->bit_size;
         nir_ssa_dest_init(&phi->instr, &phi->dest,
                           then_dest->num_components, bit_size, NULL);

         nir_phi_src *src0 = ralloc(phi, nir_phi_src);
         src0->pred = nir_if_last_then_block(if_stmt);
         src0->src = nir_src_for_ssa(then_dest);
         exec_list_push_tail(&phi->srcs, &src0->node);

         nir_phi_src *src1 = ralloc(phi, nir_phi_src);
         src1->pred = nir_if_last_else_block(if_stmt);
         src1->src = nir_src_for_ssa(else_dest);
         exec_list_push_tail(&phi->srcs, &src1->node);

         nir_builder_instr_insert(b, &phi->instr);
         *dest = &phi->dest.ssa;
      }
   }
}

static void
emit_load_store(nir_builder *b, nir_intrinsic_instr *orig_instr,
                nir_deref_var *deref, nir_deref *tail,
                nir_ssa_def **dest, nir_ssa_def *src)
{
   for (; tail->child; tail = tail->child) {
      if (tail->child->deref_type != nir_deref_type_array)
         continue;

      nir_deref_array *arr = nir_deref_as_array(tail->child);
      if (arr->deref_array_type != nir_deref_array_type_indirect)
         continue;

      int length = glsl_get_length(tail->type);

      emit_indirect_load_store(b, orig_instr, deref, tail, -arr->base_offset,
                               length - arr->base_offset, dest, src);
      return;
   }

   assert(tail && tail->child == NULL);

   /* We reached the end of the deref chain.  Emit the instruction */

   if (src == NULL) {
      /* This is a load instruction */
      nir_intrinsic_instr *load =
         nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_var);
      load->num_components = orig_instr->num_components;
      load->variables[0] = nir_deref_var_clone(deref, load);
      unsigned bit_size = orig_instr->dest.ssa.bit_size;
      nir_ssa_dest_init(&load->instr, &load->dest,
                        load->num_components, bit_size, NULL);
      nir_builder_instr_insert(b, &load->instr);
      *dest = &load->dest.ssa;
   } else {
      /* This is a store instruction */
      nir_intrinsic_instr *store =
         nir_intrinsic_instr_create(b->shader, nir_intrinsic_store_var);
      store->num_components = orig_instr->num_components;
      nir_intrinsic_set_write_mask(store, nir_intrinsic_write_mask(orig_instr));
      store->variables[0] = nir_deref_var_clone(deref, store);
      store->src[0] = nir_src_for_ssa(src);
      nir_builder_instr_insert(b, &store->instr);
   }
}

static bool
deref_has_indirect(nir_deref_var *deref)
{
   for (nir_deref *tail = deref->deref.child; tail; tail = tail->child) {
      if (tail->deref_type != nir_deref_type_array)
         continue;

      nir_deref_array *arr = nir_deref_as_array(tail);
      if (arr->deref_array_type == nir_deref_array_type_indirect)
         return true;
   }

   return false;
}

static bool
lower_indirect_block(nir_block *block, nir_builder *b,
                     nir_variable_mode modes)
{
   bool progress = false;

   nir_foreach_instr_safe(instr, block) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      if (intrin->intrinsic != nir_intrinsic_load_var &&
          intrin->intrinsic != nir_intrinsic_store_var)
         continue;

      if (!deref_has_indirect(intrin->variables[0]))
         continue;

      /* Only lower variables whose mode is in the mask, or compact
       * array variables.  (We can't handle indirects on tightly packed
       * scalar arrays, so we need to lower them regardless.)
       */
      if (!(modes & intrin->variables[0]->var->data.mode) &&
          !intrin->variables[0]->var->data.compact)
         continue;

      b->cursor = nir_before_instr(&intrin->instr);

      if (intrin->intrinsic == nir_intrinsic_load_var) {
         nir_ssa_def *result;
         emit_load_store(b, intrin, intrin->variables[0],
                         &intrin->variables[0]->deref, &result, NULL);
         nir_ssa_def_rewrite_uses(&intrin->dest.ssa, nir_src_for_ssa(result));
      } else {
         assert(intrin->src[0].is_ssa);
         emit_load_store(b, intrin, intrin->variables[0],
                         &intrin->variables[0]->deref, NULL, intrin->src[0].ssa);
      }
      nir_instr_remove(&intrin->instr);
      progress = true;
   }

   return progress;
}

static bool
lower_indirects_impl(nir_function_impl *impl, nir_variable_mode modes)
{
   nir_builder builder;
   nir_builder_init(&builder, impl);
   bool progress = false;

   nir_foreach_block_safe(block, impl) {
      progress |= lower_indirect_block(block, &builder, modes);
   }

   if (progress)
      nir_metadata_preserve(impl, nir_metadata_none);

   return progress;
}

/** Lowers indirect variable loads/stores to direct loads/stores.
 *
 * The pass works by replacing any indirect load or store with an if-ladder
 * that does a binary search on the array index.
 */
bool
nir_lower_indirect_derefs(nir_shader *shader, nir_variable_mode modes)
{
   bool progress = false;

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress = lower_indirects_impl(function->impl, modes) || progress;
   }

   return progress;
}
