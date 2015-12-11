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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "nir.h"
#include "nir_builder.h"

#define MAX_COLORS 2  /* VARYING_SLOT_COL0/COL1 */

typedef struct {
   nir_builder   b;
   nir_shader   *shader;
   nir_variable *face;
   struct {
      nir_variable *front;        /* COLn */
      nir_variable *back;         /* BFCn */
   } colors[MAX_COLORS];
   int colors_count;
} lower_2side_state;


/* Lowering pass for fragment shaders to emulated two-sided-color.  For
 * each COLOR input, a corresponding BCOLOR input is created, and bcsel
 * instruction used to select front or back color based on FACE.
 */

static nir_variable *
create_input(nir_shader *shader, unsigned drvloc, gl_varying_slot slot)
{
   nir_variable *var = rzalloc(shader, nir_variable);

   var->data.driver_location = drvloc;
   var->type = glsl_vec4_type();
   var->data.mode = nir_var_shader_in;
   var->name = ralloc_asprintf(var, "in_%d", drvloc);
   var->data.index = 0;
   var->data.location = slot;

   exec_list_push_tail(&shader->inputs, &var->node);

   shader->num_inputs++;     /* TODO use type_size() */

   return var;
}

static nir_ssa_def *
load_input(nir_builder *b, nir_variable *in)
{
   nir_intrinsic_instr *load;

   load = nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_input);
   load->num_components = 4;
   load->const_index[0] = in->data.driver_location;
   load->src[0] = nir_src_for_ssa(nir_imm_int(b, 0));
   nir_ssa_dest_init(&load->instr, &load->dest, 4, NULL);
   nir_builder_instr_insert(b, &load->instr);

   return &load->dest.ssa;
}

static int
setup_inputs(lower_2side_state *state)
{
   int maxloc = -1;

   /* find color/face inputs: */
   nir_foreach_variable(var, &state->shader->inputs) {
      int loc = var->data.driver_location;

      /* keep track of last used driver-location.. we'll be
       * appending BCLr/FACE after last existing input:
       */
      maxloc = MAX2(maxloc, loc);

      switch (var->data.location) {
      case VARYING_SLOT_COL0:
      case VARYING_SLOT_COL1:
         assert(state->colors_count < ARRAY_SIZE(state->colors));
         state->colors[state->colors_count].front = var;
         state->colors_count++;
         break;
      case VARYING_SLOT_FACE:
         state->face = var;
         break;
      }
   }

   /* if we don't have any color inputs, nothing to do: */
   if (state->colors_count == 0)
      return -1;

   /* if we don't already have one, insert a FACE input: */
   if (!state->face) {
      state->face = create_input(state->shader, ++maxloc, VARYING_SLOT_FACE);
      state->face->data.interpolation = INTERP_QUALIFIER_FLAT;
   }

   /* add required back-face color inputs: */
   for (int i = 0; i < state->colors_count; i++) {
      gl_varying_slot slot;

      if (state->colors[i].front->data.location == VARYING_SLOT_COL0)
         slot = VARYING_SLOT_BFC0;
      else
         slot = VARYING_SLOT_BFC1;

      state->colors[i].back = create_input(state->shader, ++maxloc, slot);
   }

   return 0;
}

static bool
nir_lower_two_sided_color_block(nir_block *block, void *void_state)
{
   lower_2side_state *state = void_state;
   nir_builder *b = &state->b;

   nir_foreach_instr_safe(block, instr) {
      if (instr->type != nir_instr_type_intrinsic)
         continue;

      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

      if (intr->intrinsic != nir_intrinsic_load_input)
         continue;

      int idx;
      for (idx = 0; idx < state->colors_count; idx++) {
         unsigned drvloc =
            state->colors[idx].front->data.driver_location;
         if (intr->const_index[0] == drvloc) {
            assert(nir_src_as_const_value(intr->src[0]));
            break;
         }
      }

      if (idx == state->colors_count)
         continue;

      /* replace load_input(COLn) with
       * bcsel(load_input(FACE), load_input(COLn), load_input(BFCn))
       */
      b->cursor = nir_before_instr(&intr->instr);
      nir_ssa_def *face  = nir_channel(b, load_input(b, state->face), 0);
      nir_ssa_def *front = load_input(b, state->colors[idx].front);
      nir_ssa_def *back  = load_input(b, state->colors[idx].back);
      nir_ssa_def *cond  = nir_flt(b, face, nir_imm_float(b, 0.0));
      nir_ssa_def *color = nir_bcsel(b, cond, back, front);

      assert(intr->dest.is_ssa);
      nir_ssa_def_rewrite_uses(&intr->dest.ssa, nir_src_for_ssa(color));
   }

   return true;
}

static void
nir_lower_two_sided_color_impl(nir_function_impl *impl,
                               lower_2side_state *state)
{
   nir_builder *b = &state->b;

   nir_builder_init(b, impl);

   nir_foreach_block(impl, nir_lower_two_sided_color_block, state);

   nir_metadata_preserve(impl, nir_metadata_block_index |
                               nir_metadata_dominance);
}

void
nir_lower_two_sided_color(nir_shader *shader)
{
   lower_2side_state state = {
      .shader = shader,
   };

   if (shader->stage != MESA_SHADER_FRAGMENT)
      return;

   if (setup_inputs(&state) != 0)
      return;

   nir_foreach_overload(shader, overload) {
      if (overload->impl)
         nir_lower_two_sided_color_impl(overload->impl, &state);
   }

}
