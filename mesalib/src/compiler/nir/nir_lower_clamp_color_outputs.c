/*
 * Copyright © 2015 Red Hat
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

typedef struct {
   nir_shader *shader;
   nir_builder b;
} lower_state;

static bool
is_color_output(lower_state *state, nir_variable *out)
{
   switch (state->shader->info.stage) {
   case MESA_SHADER_VERTEX:
   case MESA_SHADER_GEOMETRY:
   case MESA_SHADER_TESS_EVAL:
      switch (out->data.location) {
      case VARYING_SLOT_COL0:
      case VARYING_SLOT_COL1:
      case VARYING_SLOT_BFC0:
      case VARYING_SLOT_BFC1:
         return true;
      default:
         return false;
      }
      break;
   case MESA_SHADER_FRAGMENT:
      return (out->data.location == FRAG_RESULT_COLOR ||
              out->data.location >= FRAG_RESULT_DATA0);
   default:
      return false;
   }
}

static bool
lower_intrinsic(lower_state *state, nir_intrinsic_instr *intr)
{
   nir_variable *out = NULL;
   nir_builder *b = &state->b;
   nir_ssa_def *s;

   switch (intr->intrinsic) {
   case nir_intrinsic_store_deref:
      out = nir_deref_instr_get_variable(nir_src_as_deref(intr->src[0]));
      break;
   case nir_intrinsic_store_output:
      /* already had i/o lowered.. lookup the matching output var: */
      nir_foreach_shader_out_variable(var, state->shader) {
         int drvloc = var->data.driver_location;
         if (nir_intrinsic_base(intr) == drvloc) {
            out = var;
            break;
         }
      }
      assume(out);
      break;
   default:
      return false;
   }

   if (out->data.mode != nir_var_shader_out)
      return false;

   if (is_color_output(state, out)) {
      b->cursor = nir_before_instr(&intr->instr);
      int src = intr->intrinsic == nir_intrinsic_store_deref ? 1 : 0;
      s = nir_ssa_for_src(b, intr->src[src], intr->num_components);
      s = nir_fsat(b, s);
      nir_instr_rewrite_src(&intr->instr, &intr->src[src], nir_src_for_ssa(s));
   }

   return true;
}

static bool
lower_block(lower_state *state, nir_block *block)
{
   bool progress = false;

   nir_foreach_instr_safe(instr, block) {
      if (instr->type == nir_instr_type_intrinsic)
         progress |= lower_intrinsic(state, nir_instr_as_intrinsic(instr));
   }

   return progress;
}

static bool
lower_impl(lower_state *state, nir_function_impl *impl)
{
   nir_builder_init(&state->b, impl);
   bool progress = false;

   nir_foreach_block(block, impl) {
      progress |= lower_block(state, block);
   }
   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);

   return progress;
}

bool
nir_lower_clamp_color_outputs(nir_shader *shader)
{
   bool progress = false;
   lower_state state = {
      .shader = shader,
   };

   nir_foreach_function(function, shader) {
      if (function->impl)
         progress |= lower_impl(&state, function->impl);
   }

   return progress;
}
