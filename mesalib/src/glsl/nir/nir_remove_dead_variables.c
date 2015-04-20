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

static void
add_var_use_intrinsic(nir_intrinsic_instr *instr, struct set *live)
{
   unsigned num_vars = nir_intrinsic_infos[instr->intrinsic].num_variables;
   for (unsigned i = 0; i < num_vars; i++) {
      nir_variable *var = instr->variables[i]->var;
      _mesa_set_add(live, var);
   }
}

static void
add_var_use_call(nir_call_instr *instr, struct set *live)
{
   if (instr->return_deref != NULL) {
      nir_variable *var = instr->return_deref->var;
      _mesa_set_add(live, var);
   }

   for (unsigned i = 0; i < instr->num_params; i++) {
      nir_variable *var = instr->params[i]->var;
      _mesa_set_add(live, var);
   }
}

static void
add_var_use_tex(nir_tex_instr *instr, struct set *live)
{
   if (instr->sampler != NULL) {
      nir_variable *var = instr->sampler->var;
      _mesa_set_add(live, var);
   }
}

static bool
add_var_use_block(nir_block *block, void *state)
{
   struct set *live = state;

   nir_foreach_instr(block, instr) {
      switch(instr->type) {
      case nir_instr_type_intrinsic:
         add_var_use_intrinsic(nir_instr_as_intrinsic(instr), live);
         break;

      case nir_instr_type_call:
         add_var_use_call(nir_instr_as_call(instr), live);
         break;

      case nir_instr_type_tex:
         add_var_use_tex(nir_instr_as_tex(instr), live);
         break;

      default:
         break;
      }
   }

   return true;
}

static void
add_var_use_shader(nir_shader *shader, struct set *live)
{
   nir_foreach_overload(shader, overload) {
      if (overload->impl) {
         nir_foreach_block(overload->impl, add_var_use_block, live);
      }
   }
}

static void
remove_dead_vars(struct exec_list *var_list, struct set *live)
{
   foreach_list_typed_safe(nir_variable, var, node, var_list) {
      struct set_entry *entry = _mesa_set_search(live, var);
      if (entry == NULL) {
         exec_node_remove(&var->node);
         ralloc_free(var);
      }
   }
}

void
nir_remove_dead_variables(nir_shader *shader)
{
   struct set *live =
      _mesa_set_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);

   add_var_use_shader(shader, live);

   remove_dead_vars(&shader->globals, live);

   nir_foreach_overload(shader, overload) {
      if (overload->impl)
         remove_dead_vars(&overload->impl->locals, live);
   }

   _mesa_set_destroy(live, NULL);
}
