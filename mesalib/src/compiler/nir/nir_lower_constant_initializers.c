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

static bool
deref_apply_constant_initializer(nir_deref_var *deref, void *state)
{
   struct nir_builder *b = state;

   nir_load_const_instr *initializer =
      nir_deref_get_const_initializer_load(b->shader, deref);
   nir_builder_instr_insert(b, &initializer->instr);

   nir_store_deref_var(b, deref, &initializer->def, 0xf);

   return true;
}

static bool
lower_const_initializer(struct nir_builder *b, struct exec_list *var_list)
{
   bool progress = false;

   b->cursor = nir_before_cf_list(&b->impl->body);

   nir_foreach_variable(var, var_list) {
      if (!var->constant_initializer)
         continue;

      progress = true;

      nir_deref_var deref;
      deref.deref.deref_type = nir_deref_type_var,
      deref.deref.child = NULL;
      deref.deref.type = var->type,
      deref.var = var;

      nir_deref_foreach_leaf(&deref, deref_apply_constant_initializer, b);

      var->constant_initializer = NULL;
   }

   return progress;
}

bool
nir_lower_constant_initializers(nir_shader *shader, nir_variable_mode modes)
{
   bool progress = false;

   nir_builder builder;
   if (modes & ~nir_var_local)
      nir_builder_init(&builder, nir_shader_get_entrypoint(shader));

   if (modes & nir_var_shader_out)
      progress |= lower_const_initializer(&builder, &shader->outputs);

   if (modes & nir_var_global)
      progress |= lower_const_initializer(&builder, &shader->globals);

   if (modes & nir_var_system_value)
      progress |= lower_const_initializer(&builder, &shader->system_values);

   if (progress) {
      nir_foreach_function(function, shader) {
         if (function->impl) {
            nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                                  nir_metadata_dominance |
                                                  nir_metadata_live_ssa_defs);
         }
      }
   }

   if (modes & nir_var_local) {
      nir_foreach_function(function, shader) {
         if (!function->impl)
            continue;

         nir_builder_init(&builder, function->impl);
         if (lower_const_initializer(&builder, &function->impl->locals)) {
            nir_metadata_preserve(function->impl, nir_metadata_block_index |
                                                  nir_metadata_dominance |
                                                  nir_metadata_live_ssa_defs);
            progress = true;
         }
      }
   }

   return progress;
}
