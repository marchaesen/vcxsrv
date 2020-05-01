/*
 * Copyright (C) 2020 Google, Inc.
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "nir.h"
#include "nir_builder.h"

/* Lower mediump FS outputs to fp16 */

static bool
lower_output_var(nir_shader *nir, int location)
{
   nir_foreach_variable (var, &nir->outputs) {
      if (var->data.driver_location == location &&
            ((var->data.precision == GLSL_PRECISION_MEDIUM) ||
             (var->data.precision == GLSL_PRECISION_LOW))) {
         if (glsl_get_base_type(var->type) == GLSL_TYPE_FLOAT)
            var->type = glsl_float16_type(var->type);
         
         return glsl_get_base_type(var->type) == GLSL_TYPE_FLOAT16;
      }
   }

   return false;
}

void
nir_lower_mediump_outputs(nir_shader *nir)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);
   assert(impl);

   /* Get rid of old derefs before we change the types of the variables */
   nir_opt_dce(nir);

   nir_builder b;
   nir_builder_init(&b, impl);

   nir_foreach_block_safe (block, impl) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (intr->intrinsic != nir_intrinsic_store_output)
            continue;

         if (!lower_output_var(nir, nir_intrinsic_base(intr)))
            continue;

         b.cursor = nir_before_instr(&intr->instr);
         nir_instr_rewrite_src(&intr->instr, &intr->src[0],
         nir_src_for_ssa(nir_f2f16(&b, intr->src[0].ssa)));

         nir_intrinsic_set_type(intr, nir_type_float16);
      }
   }
}
