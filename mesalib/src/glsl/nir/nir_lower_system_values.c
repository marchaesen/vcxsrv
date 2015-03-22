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
 *    Connor Abbott (cwabbott0@gmail.com)
 *
 */

#include "nir.h"
#include "main/mtypes.h"

static void
convert_instr(nir_intrinsic_instr *instr)
{
   if (instr->intrinsic != nir_intrinsic_load_var)
      return;

   nir_variable *var = instr->variables[0]->var;
   if (var->data.mode != nir_var_system_value)
      return;

   void *mem_ctx = ralloc_parent(instr);

   nir_intrinsic_op op;

   switch (var->data.location) {
   case SYSTEM_VALUE_FRONT_FACE:
      op = nir_intrinsic_load_front_face;
      break;
   case SYSTEM_VALUE_VERTEX_ID:
      op = nir_intrinsic_load_vertex_id;
      break;
   case SYSTEM_VALUE_VERTEX_ID_ZERO_BASE:
      op = nir_intrinsic_load_vertex_id_zero_base;
      break;
   case SYSTEM_VALUE_BASE_VERTEX:
      op = nir_intrinsic_load_base_vertex;
      break;
   case SYSTEM_VALUE_INSTANCE_ID:
      op = nir_intrinsic_load_instance_id;
      break;
   case SYSTEM_VALUE_SAMPLE_ID:
      op = nir_intrinsic_load_sample_id;
      break;
   case SYSTEM_VALUE_SAMPLE_POS:
      op = nir_intrinsic_load_sample_pos;
      break;
   case SYSTEM_VALUE_SAMPLE_MASK_IN:
      op = nir_intrinsic_load_sample_mask_in;
      break;
   case SYSTEM_VALUE_INVOCATION_ID:
      op = nir_intrinsic_load_invocation_id;
      break;
   default:
      unreachable("not reached");
   }

   nir_intrinsic_instr *new_instr = nir_intrinsic_instr_create(mem_ctx, op);

   if (instr->dest.is_ssa) {
      nir_ssa_dest_init(&new_instr->instr, &new_instr->dest,
                        instr->dest.ssa.num_components, NULL);
      nir_ssa_def_rewrite_uses(&instr->dest.ssa,
                               nir_src_for_ssa(&new_instr->dest.ssa),
                               mem_ctx);
   } else {
      nir_dest_copy(&new_instr->dest, &instr->dest, mem_ctx);
   }

   nir_instr_insert_before(&instr->instr, &new_instr->instr);
   nir_instr_remove(&instr->instr);
}

static bool
convert_block(nir_block *block, void *state)
{
   (void) state;

   nir_foreach_instr_safe(block, instr) {
      if (instr->type == nir_instr_type_intrinsic)
         convert_instr(nir_instr_as_intrinsic(instr));
   }

   return true;
}

static void
convert_impl(nir_function_impl *impl)
{
   nir_foreach_block(impl, convert_block, NULL);
   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
}

void
nir_lower_system_values(nir_shader *shader)
{
   nir_foreach_overload(shader, overload) {
      if (overload->impl)
         convert_impl(overload->impl);
   }

   exec_list_make_empty(&shader->system_values);
}
