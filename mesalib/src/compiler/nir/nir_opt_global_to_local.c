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

static bool
global_to_local(nir_register *reg)
{
   nir_function_impl *impl = NULL;

   assert(reg->is_global);

   nir_foreach_def(reg, def_dest) {
      nir_instr *instr = def_dest->reg.parent_instr;
      nir_function_impl *instr_impl =
         nir_cf_node_get_function(&instr->block->cf_node);
      if (impl != NULL) {
         if (impl != instr_impl)
            return false;
      } else {
         impl = instr_impl;
      }
   }

   nir_foreach_use(reg, use_src) {
      nir_instr *instr = use_src->parent_instr;
      nir_function_impl *instr_impl =
         nir_cf_node_get_function(&instr->block->cf_node);
      if (impl != NULL) {
         if (impl != instr_impl)
            return false;
      } else {
         impl = instr_impl;
      }
   }

   nir_foreach_if_use(reg, use_src) {
      nir_if *if_stmt = use_src->parent_if;
      nir_function_impl *if_impl = nir_cf_node_get_function(&if_stmt->cf_node);
      if (impl != NULL) {
         if (impl != if_impl)
            return false;
      } else {
         impl = if_impl;
      }
   }

   if (impl == NULL) {
      /* this instruction is never used/defined, delete it */
      nir_reg_remove(reg);
      return true;
   }

   /*
    * if we've gotten to this point, the register is always used/defined in
    * the same implementation so we can move it to be local to that
    * implementation.
    */

   exec_node_remove(&reg->node);
   exec_list_push_tail(&impl->registers, &reg->node);
   reg->index = impl->reg_alloc++;
   reg->is_global = false;
   return true;
}

bool
nir_opt_global_to_local(nir_shader *shader)
{
   bool progress = false;

   foreach_list_typed_safe(nir_register, reg, node, &shader->registers) {
      if (global_to_local(reg))
         progress = true;
   }

   return progress;
}
