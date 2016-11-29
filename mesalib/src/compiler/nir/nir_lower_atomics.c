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

#include "compiler/glsl/ir_uniform.h"
#include "nir.h"
#include "main/config.h"
#include <assert.h>

/*
 * replace atomic counter intrinsics that use a variable with intrinsics
 * that directly store the buffer index and byte offset
 */

static void
lower_instr(nir_intrinsic_instr *instr,
            const struct gl_shader_program *shader_program,
            nir_shader *shader)
{
   nir_intrinsic_op op;
   switch (instr->intrinsic) {
   case nir_intrinsic_atomic_counter_read_var:
      op = nir_intrinsic_atomic_counter_read;
      break;

   case nir_intrinsic_atomic_counter_inc_var:
      op = nir_intrinsic_atomic_counter_inc;
      break;

   case nir_intrinsic_atomic_counter_dec_var:
      op = nir_intrinsic_atomic_counter_dec;
      break;

   case nir_intrinsic_atomic_counter_add_var:
      op = nir_intrinsic_atomic_counter_add;
      break;

   case nir_intrinsic_atomic_counter_min_var:
      op = nir_intrinsic_atomic_counter_min;
      break;

   case nir_intrinsic_atomic_counter_max_var:
      op = nir_intrinsic_atomic_counter_max;
      break;

   case nir_intrinsic_atomic_counter_and_var:
      op = nir_intrinsic_atomic_counter_and;
      break;

   case nir_intrinsic_atomic_counter_or_var:
      op = nir_intrinsic_atomic_counter_or;
      break;

   case nir_intrinsic_atomic_counter_xor_var:
      op = nir_intrinsic_atomic_counter_xor;
      break;

   case nir_intrinsic_atomic_counter_exchange_var:
      op = nir_intrinsic_atomic_counter_exchange;
      break;

   case nir_intrinsic_atomic_counter_comp_swap_var:
      op = nir_intrinsic_atomic_counter_comp_swap;
      break;

   default:
      return;
   }

   if (instr->variables[0]->var->data.mode != nir_var_uniform &&
       instr->variables[0]->var->data.mode != nir_var_shader_storage &&
       instr->variables[0]->var->data.mode != nir_var_shared)
      return; /* atomics passed as function arguments can't be lowered */

   void *mem_ctx = ralloc_parent(instr);
   unsigned uniform_loc = instr->variables[0]->var->data.location;

   nir_intrinsic_instr *new_instr = nir_intrinsic_instr_create(mem_ctx, op);
   nir_intrinsic_set_base(new_instr,
      shader_program->data->UniformStorage[uniform_loc].opaque[shader->stage].index);

   nir_load_const_instr *offset_const =
      nir_load_const_instr_create(mem_ctx, 1, 32);
   offset_const->value.u32[0] = instr->variables[0]->var->data.offset;

   nir_instr_insert_before(&instr->instr, &offset_const->instr);

   nir_ssa_def *offset_def = &offset_const->def;

   nir_deref *tail = &instr->variables[0]->deref;
   while (tail->child != NULL) {
      nir_deref_array *deref_array = nir_deref_as_array(tail->child);
      tail = tail->child;

      unsigned child_array_elements = tail->child != NULL ?
         glsl_get_aoa_size(tail->type) : 1;

      offset_const->value.u32[0] += deref_array->base_offset *
         child_array_elements * ATOMIC_COUNTER_SIZE;

      if (deref_array->deref_array_type == nir_deref_array_type_indirect) {
         nir_load_const_instr *atomic_counter_size =
            nir_load_const_instr_create(mem_ctx, 1, 32);
         atomic_counter_size->value.u32[0] = child_array_elements * ATOMIC_COUNTER_SIZE;
         nir_instr_insert_before(&instr->instr, &atomic_counter_size->instr);

         nir_alu_instr *mul = nir_alu_instr_create(mem_ctx, nir_op_imul);
         nir_ssa_dest_init(&mul->instr, &mul->dest.dest, 1, 32, NULL);
         mul->dest.write_mask = 0x1;
         nir_src_copy(&mul->src[0].src, &deref_array->indirect, mul);
         mul->src[1].src.is_ssa = true;
         mul->src[1].src.ssa = &atomic_counter_size->def;
         nir_instr_insert_before(&instr->instr, &mul->instr);

         nir_alu_instr *add = nir_alu_instr_create(mem_ctx, nir_op_iadd);
         nir_ssa_dest_init(&add->instr, &add->dest.dest, 1, 32, NULL);
         add->dest.write_mask = 0x1;
         add->src[0].src.is_ssa = true;
         add->src[0].src.ssa = &mul->dest.dest.ssa;
         add->src[1].src.is_ssa = true;
         add->src[1].src.ssa = offset_def;
         nir_instr_insert_before(&instr->instr, &add->instr);

         offset_def = &add->dest.dest.ssa;
      }
   }

   new_instr->src[0].is_ssa = true;
   new_instr->src[0].ssa = offset_def;

   /* Copy the other sources, if any, from the original instruction to the new
    * instruction.
    */
   for (unsigned i = 0; i < nir_intrinsic_infos[instr->intrinsic].num_srcs; i++)
      new_instr->src[i + 1] = instr->src[i];

   if (instr->dest.is_ssa) {
      nir_ssa_dest_init(&new_instr->instr, &new_instr->dest,
                        instr->dest.ssa.num_components, 32, NULL);
      nir_ssa_def_rewrite_uses(&instr->dest.ssa,
                               nir_src_for_ssa(&new_instr->dest.ssa));
   } else {
      nir_dest_copy(&new_instr->dest, &instr->dest, mem_ctx);
   }

   nir_instr_insert_before(&instr->instr, &new_instr->instr);
   nir_instr_remove(&instr->instr);
}

void
nir_lower_atomics(nir_shader *shader,
                  const struct gl_shader_program *shader_program)
{
   nir_foreach_function(function, shader) {
      if (function->impl) {
         nir_foreach_block(block, function->impl) {
            nir_foreach_instr_safe(instr, block) {
               if (instr->type == nir_instr_type_intrinsic)
                  lower_instr(nir_instr_as_intrinsic(instr),
                              shader_program, shader);
            }
         }

         nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                               nir_metadata_dominance);
      }
   }
}
