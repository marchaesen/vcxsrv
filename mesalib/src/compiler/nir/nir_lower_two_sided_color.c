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

#define MAX_COLORS 2 /* VARYING_SLOT_COL0/COL1 */

typedef struct {
   nir_builder b;
   nir_shader *shader;
   bool face_sysval;
   int colors_count;
} lower_2side_state;

/* Lowering pass for fragment shaders to emulated two-sided-color.  For
 * each COLOR input, a corresponding BCOLOR input is created, and bcsel
 * instruction used to select front or back color based on FACE.
 */

static nir_def *
load_input(nir_builder *b, nir_intrinsic_instr *intr, int location)
{
   nir_def *load;
   int c = nir_intrinsic_component(intr);
   nir_def *zero = nir_imm_int(b, 0);
   if (intr->intrinsic == nir_intrinsic_load_input)
      load = nir_load_input(b, intr->def.num_components, intr->def.bit_size, zero,
                            .io_semantics.location = location,
                            .component = c);
   else
      load = nir_load_interpolated_input(b, intr->def.num_components, intr->def.bit_size,
                                         intr->src[0].ssa, zero,
                                         .io_semantics.location = location,
                                         .component = c);
   return load;
}

static int
setup_inputs(lower_2side_state *state)
{
   state->colors_count = util_bitcount64(state->shader->info.inputs_read & (VARYING_BIT_COL0 | VARYING_BIT_COL1));
   return state->colors_count ? 0 : -1;
}

static bool
nir_lower_two_sided_color_instr(nir_builder *b, nir_instr *instr, void *data)
{
   lower_2side_state *state = data;

   if (instr->type != nir_instr_type_intrinsic)
      return false;
   nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

   int idx;
   if (intr->intrinsic == nir_intrinsic_load_input || intr->intrinsic == nir_intrinsic_load_interpolated_input) {
      nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
      if (sem.location != VARYING_SLOT_COL0 && sem.location != VARYING_SLOT_COL1)
         return false;
      idx = sem.location;
   } else
      return false;

   /* replace load_input(COLn) with
    * bcsel(load_system_value(FACE), load_input(COLn), load_input(BFCn))
    */
   b->cursor = nir_before_instr(&intr->instr);
   /* gl_FrontFace is a boolean but the intrinsic constructor creates
    * 32-bit value by default.
    */
   nir_def *face;
   if (state->face_sysval)
      face = nir_load_front_face(b, 1);
   else {
      face = nir_load_input(b, 1, 32, nir_imm_int(b, 0), .base = 0,
                            .dest_type = nir_type_bool32,
                            .io_semantics.location = VARYING_SLOT_FACE);
      face = nir_b2b1(b, face);
   }

   nir_def *front = load_input(b, intr, idx);
   nir_def *back = load_input(b, intr, idx == VARYING_SLOT_COL0 ? VARYING_SLOT_BFC0 : VARYING_SLOT_BFC1);
   nir_def *color = nir_bcsel(b, face, front, back);

   nir_def_rewrite_uses(&intr->def, color);

   return true;
}

bool
nir_lower_two_sided_color(nir_shader *shader, bool face_sysval)
{
   assert(shader->info.io_lowered);

   lower_2side_state state = {
      .shader = shader,
      .face_sysval = face_sysval,
   };

   if (shader->info.stage != MESA_SHADER_FRAGMENT)
      return false;

   if (setup_inputs(&state) != 0)
      return false;

   return nir_shader_instructions_pass(shader,
                                       nir_lower_two_sided_color_instr,
                                       nir_metadata_control_flow,
                                       &state);
}
