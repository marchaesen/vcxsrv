/*
 * Copyright © 2018 Valve Corporation
 * Copyright © 2017 Red Hat
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

#include "vtn_private.h"
#include "GLSL.ext.AMD.h"

bool
vtn_handle_amd_gcn_shader_instruction(struct vtn_builder *b, uint32_t ext_opcode,
                                      const uint32_t *w, unsigned count)
{
   const struct glsl_type *dest_type =
                           vtn_value(b, w[1], vtn_value_type_type)->type->type;
   struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_ssa);
   val->ssa = vtn_create_ssa_value(b, dest_type);

   switch ((enum GcnShaderAMD)ext_opcode) {
   case CubeFaceIndexAMD:
      val->ssa->def = nir_cube_face_index(&b->nb, vtn_ssa_value(b, w[5])->def);
	  break;
   case CubeFaceCoordAMD:
      val->ssa->def = nir_cube_face_coord(&b->nb, vtn_ssa_value(b, w[5])->def);
      break;
   case TimeAMD: {
      nir_intrinsic_instr *intrin = nir_intrinsic_instr_create(b->nb.shader,
                                    nir_intrinsic_shader_clock);
      nir_ssa_dest_init(&intrin->instr, &intrin->dest, 2, 32, NULL);
      nir_builder_instr_insert(&b->nb, &intrin->instr);
      val->ssa->def = nir_pack_64_2x32(&b->nb, &intrin->dest.ssa);
      break;
   }
   default:
      unreachable("Invalid opcode");
   }
   return true;
}
