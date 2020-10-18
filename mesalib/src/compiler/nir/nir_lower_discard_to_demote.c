/*
 * Copyright © 2020 Valve Corporation
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
#include "nir_builder.h"

static bool
nir_lower_discard_to_demote_instr(nir_builder *b, nir_instr *instr, void *data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   switch (intrin->intrinsic) {
   case nir_intrinsic_discard:
      intrin->intrinsic = nir_intrinsic_demote;
      b->shader->info.fs.uses_demote = true;
      return true;
   case nir_intrinsic_discard_if:
      intrin->intrinsic = nir_intrinsic_demote_if;
      b->shader->info.fs.uses_demote = true;
      return true;
   case nir_intrinsic_load_helper_invocation:
      intrin->intrinsic = nir_intrinsic_is_helper_invocation;
      return true;
   default:
      return false;
   }
}

/**
 * This pass is intended as workaround for game bugs to force correct
 * derivatives after kill. This lowering is not valid in the general case
 * as it might change the result of subgroup operations and loop behavior.
 *
 * discard() will be lowered as demote() and gl_HelperInvocation
 * will be lowered as helperInvocationEXT().
 */
bool
nir_lower_discard_to_demote(nir_shader *shader)
{
   if (shader->info.stage != MESA_SHADER_FRAGMENT)
      return false;

   return nir_shader_instructions_pass(shader,
                                       nir_lower_discard_to_demote_instr,
                                       nir_metadata_all,
                                       NULL);
}
