/*
 * Copyright © 2019 Valve Corporation
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
 */

#include "nir.h"
#include "util/debug.h"

/* This pass removes information which is only useful for debugging,
 * making cache hits from similar shaders more likely.
 */

static void
strip_variable(nir_variable *var)
{
   var->name = NULL;

   if (var->data.mode != nir_var_shader_in &&
       var->data.mode != nir_var_shader_out) {
      /* We assume that this is called after nir_lower_io(), at which point
       * the original user-facing location is irrelevant except for inputs and
       * outputs.
       */
      var->data.location = 0;
   }
}

static void
strip_register(nir_register *reg)
{
   reg->name = NULL;
}

static bool
strip_def(nir_ssa_def *def, void *_unused)
{
   (void) _unused;
   def->name = NULL;
   return true;
}

static void
strip_impl(nir_function_impl *impl)
{
   nir_index_ssa_defs(impl);

   nir_foreach_variable(var, &impl->locals)
      strip_variable(var);
   nir_foreach_register(reg, &impl->registers)
      strip_register(reg);
   nir_foreach_block(block, impl) {
      nir_foreach_instr(instr, block) {
         nir_foreach_ssa_def(instr, strip_def, NULL);
      }
   }
}

void
nir_strip(nir_shader *shader)
{
   static int should_strip = -1;
   if (should_strip < 0)
      should_strip = env_var_as_boolean("NIR_STRIP", true);
   if (!should_strip)
      return;

   shader->info.name = NULL;
   shader->info.label = NULL;

   nir_foreach_variable(var, &shader->uniforms)
      strip_variable(var);
   nir_foreach_variable(var, &shader->inputs)
      strip_variable(var);
   nir_foreach_variable(var, &shader->outputs)
      strip_variable(var);
   nir_foreach_variable(var, &shader->system_values)
      strip_variable(var);
   nir_foreach_variable(var, &shader->globals)
      strip_variable(var);

   nir_foreach_register(reg, &shader->registers)
      strip_register(reg);

   nir_foreach_function(func, shader) {
      if (func->impl)
         strip_impl(func->impl);
   }
}
