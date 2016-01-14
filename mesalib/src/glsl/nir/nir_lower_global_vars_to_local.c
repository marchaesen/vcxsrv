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
 *    Jason Ekstrand (jason@jlekstrand.net)
 *
 */

/*
 * This lowering pass detects when a global variable is only being used by
 * one function and makes it local to that function
 */

#include "nir.h"

struct global_to_local_state {
   nir_function_impl *impl;
   /* A hash table keyed on variable pointers that stores the unique
    * nir_function_impl that uses the given variable.  If a variable is
    * used in multiple functions, the data for the given key will be NULL.
    */
   struct hash_table *var_func_table;
};

static bool
mark_global_var_uses_block(nir_block *block, void *void_state)
{
   struct global_to_local_state *state = void_state;

   nir_foreach_instr(block, instr) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
      unsigned num_vars = nir_intrinsic_infos[intrin->intrinsic].num_variables;

      for (unsigned i = 0; i < num_vars; i++) {
         nir_variable *var = intrin->variables[i]->var;
         if (var->data.mode != nir_var_global)
            continue;

         struct hash_entry *entry =
            _mesa_hash_table_search(state->var_func_table, var);

         if (entry) {
            if (entry->data != state->impl)
               entry->data = NULL;
         } else {
            _mesa_hash_table_insert(state->var_func_table, var, state->impl);
         }
      }
   }

   return true;
}

bool
nir_lower_global_vars_to_local(nir_shader *shader)
{
   struct global_to_local_state state;
   bool progress = false;

   state.var_func_table = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                                  _mesa_key_pointer_equal);

   nir_foreach_function(shader, function) {
      if (function->impl) {
         state.impl = function->impl;
         nir_foreach_block(function->impl, mark_global_var_uses_block, &state);
      }
   }

   struct hash_entry *entry;
   hash_table_foreach(state.var_func_table, entry) {
      nir_variable *var = (void *)entry->key;
      nir_function_impl *impl = entry->data;

      assert(var->data.mode == nir_var_global);

      if (impl != NULL) {
         exec_node_remove(&var->node);
         var->data.mode = nir_var_local;
         exec_list_push_tail(&impl->locals, &var->node);
         nir_metadata_preserve(impl, nir_metadata_block_index |
                                     nir_metadata_dominance |
                                     nir_metadata_live_ssa_defs);
         progress = true;
      }
   }

   _mesa_hash_table_destroy(state.var_func_table, NULL);

   return progress;
}
