/*
 * Copyright © 2020 Igalia S.L.
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

/* Lower gl_PointCoord to account for user requested point-coord origin
 * and for whether draw buffer is flipped.
 */

typedef struct {
   const gl_state_index16 *pntc_state_tokens;
   nir_shader *shader;
   nir_builder b;
   nir_variable *pntc_transform;
} lower_pntc_ytransform_state;

static nir_def *
get_pntc_transform(lower_pntc_ytransform_state *state)
{
   if (state->pntc_transform == NULL) {
      /* NOTE: name must be prefixed w/ "gl_" to trigger slot based
       * special handling in uniform setup:
       */
      nir_variable *var = nir_state_variable_create(state->shader,
                                                    glsl_vec4_type(),
                                                    "gl_PntcYTransform",
                                                    state->pntc_state_tokens);

      var->data.how_declared = nir_var_hidden;
      state->pntc_transform = var;
   }
   return nir_load_var(&state->b, state->pntc_transform);
}

static void
lower_load_pointcoord(lower_pntc_ytransform_state *state,
                      nir_intrinsic_instr *intr)
{
   nir_builder *b = &state->b;
   b->cursor = nir_after_instr(&intr->instr);

   unsigned y_swizzle = 1;

   if (nir_intrinsic_has_component(intr)) {
      unsigned component = nir_intrinsic_component(intr);

      assert(component <= 1);
      assert(component + intr->num_components <= 2);
      if (component + intr->num_components == 1)
         return; /* only gl_PointCoord.x is loaded */

      y_swizzle -= component;
   }

   nir_def *pntc = &intr->def;
   nir_def *transform = get_pntc_transform(state);
   nir_def *flipped_y = nir_ffma(b, nir_channel(b, pntc, y_swizzle),
                                 /* Flip the sign of y if we're flipping. */
                                 nir_channel(b, transform, 0),
                                 /* The offset is 1 if we're flipping, 0 otherwise. */
                                 nir_channel(b, transform, 1));

   /* Reassemble the vector. */
   pntc = nir_vector_insert_imm(b, pntc, flipped_y, y_swizzle);
   nir_def_rewrite_uses_after(&intr->def, pntc, pntc->parent_instr);
}

static void
lower_pntc_ytransform_block(lower_pntc_ytransform_state *state,
                            nir_block *block)
{
   nir_foreach_instr_safe(instr, block) {
      if (instr->type == nir_instr_type_intrinsic) {
         nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
         if (intr->intrinsic == nir_intrinsic_load_deref) {
            nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
            nir_variable *var = nir_deref_instr_get_variable(deref);

            if (var->data.mode == nir_var_system_value &&
                var->data.location == SYSTEM_VALUE_POINT_COORD)
               lower_load_pointcoord(state, intr);
         }

         if (intr->intrinsic == nir_intrinsic_load_interpolated_input &&
             nir_intrinsic_io_semantics(intr).location == VARYING_SLOT_PNTC)
            lower_load_pointcoord(state, intr);
      }
   }
}

bool
nir_lower_pntc_ytransform(nir_shader *shader,
                          const gl_state_index16 pntc_state_tokens[][STATE_LENGTH])
{
   assert(shader->info.io_lowered);

   if (!shader->options->lower_wpos_pntc)
      return false;

   lower_pntc_ytransform_state state = {
      .pntc_state_tokens = *pntc_state_tokens,
      .shader = shader,
      .pntc_transform = NULL,
   };

   assert(shader->info.stage == MESA_SHADER_FRAGMENT);

   nir_foreach_function_impl(impl, shader) {
      state.b = nir_builder_create(impl);

      nir_foreach_block(block, impl) {
         lower_pntc_ytransform_block(&state, block);
      }
      nir_progress(true, impl, nir_metadata_control_flow);
   }

   return state.pntc_transform != NULL;
}
