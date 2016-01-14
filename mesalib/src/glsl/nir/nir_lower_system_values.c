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
#include "nir_builder.h"

struct lower_system_values_state {
   nir_builder builder;
   bool progress;
};

static bool
convert_block(nir_block *block, void *void_state)
{
   struct lower_system_values_state *state = void_state;

   nir_builder *b = &state->builder;

   nir_foreach_instr_safe(block, instr) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *load_var = nir_instr_as_intrinsic(instr);

      if (load_var->intrinsic != nir_intrinsic_load_var)
         continue;

      nir_variable *var = load_var->variables[0]->var;
      if (var->data.mode != nir_var_system_value)
         continue;

      b->cursor = nir_after_instr(&load_var->instr);

      nir_intrinsic_op sysval_op =
         nir_intrinsic_from_system_value(var->data.location);
      nir_ssa_def *sysval = nir_load_system_value(b, sysval_op, 0);

      nir_ssa_def_rewrite_uses(&load_var->dest.ssa, nir_src_for_ssa(sysval));
      nir_instr_remove(&load_var->instr);

      state->progress = true;
   }

   return true;
}

static bool
convert_impl(nir_function_impl *impl)
{
   struct lower_system_values_state state;

   state.progress = false;
   nir_builder_init(&state.builder, impl);

   nir_foreach_block(impl, convert_block, &state);
   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
   return state.progress;
}

bool
nir_lower_system_values(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function(shader, function) {
      if (function->impl)
         progress = convert_impl(function->impl) || progress;
   }

   exec_list_make_empty(&shader->system_values);

   return progress;
}
