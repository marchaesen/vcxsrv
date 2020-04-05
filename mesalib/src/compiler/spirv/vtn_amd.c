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
vtn_handle_amd_gcn_shader_instruction(struct vtn_builder *b, SpvOp ext_opcode,
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

bool
vtn_handle_amd_shader_ballot_instruction(struct vtn_builder *b, SpvOp ext_opcode,
                                         const uint32_t *w, unsigned count)
{
   const struct glsl_type *dest_type =
                           vtn_value(b, w[1], vtn_value_type_type)->type->type;
   struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_ssa);
   val->ssa = vtn_create_ssa_value(b, dest_type);

   unsigned num_args;
   nir_intrinsic_op op;
   switch ((enum ShaderBallotAMD)ext_opcode) {
   case SwizzleInvocationsAMD:
      num_args = 1;
      op = nir_intrinsic_quad_swizzle_amd;
      break;
   case SwizzleInvocationsMaskedAMD:
      num_args = 1;
      op = nir_intrinsic_masked_swizzle_amd;
      break;
   case WriteInvocationAMD:
      num_args = 3;
      op = nir_intrinsic_write_invocation_amd;
      break;
   case MbcntAMD:
      num_args = 1;
      op = nir_intrinsic_mbcnt_amd;
      break;
   default:
      unreachable("Invalid opcode");
   }

   nir_intrinsic_instr *intrin = nir_intrinsic_instr_create(b->nb.shader, op);
   nir_ssa_dest_init_for_type(&intrin->instr, &intrin->dest, dest_type, NULL);
   intrin->num_components = intrin->dest.ssa.num_components;

   for (unsigned i = 0; i < num_args; i++)
      intrin->src[i] = nir_src_for_ssa(vtn_ssa_value(b, w[i + 5])->def);

   if (intrin->intrinsic == nir_intrinsic_quad_swizzle_amd) {
      struct vtn_value *val = vtn_value(b, w[6], vtn_value_type_constant);
      unsigned mask = val->constant->values[0].u32 |
                      val->constant->values[1].u32 << 2 |
                      val->constant->values[2].u32 << 4 |
                      val->constant->values[3].u32 << 6;
      nir_intrinsic_set_swizzle_mask(intrin, mask);

   } else if (intrin->intrinsic == nir_intrinsic_masked_swizzle_amd) {
      struct vtn_value *val = vtn_value(b, w[6], vtn_value_type_constant);
      unsigned mask = val->constant->values[0].u32 |
                      val->constant->values[1].u32 << 5 |
                      val->constant->values[2].u32 << 10;
      nir_intrinsic_set_swizzle_mask(intrin, mask);
   }

   nir_builder_instr_insert(&b->nb, &intrin->instr);
   val->ssa->def = &intrin->dest.ssa;

   return true;
}

bool
vtn_handle_amd_shader_trinary_minmax_instruction(struct vtn_builder *b, SpvOp ext_opcode,
                                                 const uint32_t *w, unsigned count)
{
   struct nir_builder *nb = &b->nb;
   const struct glsl_type *dest_type =
      vtn_value(b, w[1], vtn_value_type_type)->type->type;
   struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_ssa);
   val->ssa = vtn_create_ssa_value(b, dest_type);

   unsigned num_inputs = count - 5;
   assert(num_inputs == 3);
   nir_ssa_def *src[3] = { NULL, };
   for (unsigned i = 0; i < num_inputs; i++)
      src[i] = vtn_ssa_value(b, w[i + 5])->def;

   switch ((enum ShaderTrinaryMinMaxAMD)ext_opcode) {
   case FMin3AMD:
      val->ssa->def = nir_fmin3(nb, src[0], src[1], src[2]);
      break;
   case UMin3AMD:
      val->ssa->def = nir_umin3(nb, src[0], src[1], src[2]);
      break;
   case SMin3AMD:
      val->ssa->def = nir_imin3(nb, src[0], src[1], src[2]);
      break;
   case FMax3AMD:
      val->ssa->def = nir_fmax3(nb, src[0], src[1], src[2]);
      break;
   case UMax3AMD:
      val->ssa->def = nir_umax3(nb, src[0], src[1], src[2]);
      break;
   case SMax3AMD:
      val->ssa->def = nir_imax3(nb, src[0], src[1], src[2]);
      break;
   case FMid3AMD:
      val->ssa->def = nir_fmed3(nb, src[0], src[1], src[2]);
      break;
   case UMid3AMD:
      val->ssa->def = nir_umed3(nb, src[0], src[1], src[2]);
      break;
   case SMid3AMD:
      val->ssa->def = nir_imed3(nb, src[0], src[1], src[2]);
      break;
   default:
      unreachable("unknown opcode\n");
      break;
   }

   return true;
}

bool
vtn_handle_amd_shader_explicit_vertex_parameter_instruction(struct vtn_builder *b, SpvOp ext_opcode,
                                                            const uint32_t *w, unsigned count)
{
   const struct glsl_type *dest_type =
      vtn_value(b, w[1], vtn_value_type_type)->type->type;

   struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_ssa);
   val->ssa = vtn_create_ssa_value(b, dest_type);

   nir_intrinsic_op op;
   switch ((enum ShaderExplicitVertexParameterAMD)ext_opcode) {
   case InterpolateAtVertexAMD:
      op = nir_intrinsic_interp_deref_at_vertex;
      break;
   default:
      unreachable("unknown opcode");
   }

   nir_intrinsic_instr *intrin = nir_intrinsic_instr_create(b->nb.shader, op);

   struct vtn_pointer *ptr =
      vtn_value(b, w[5], vtn_value_type_pointer)->pointer;
   nir_deref_instr *deref = vtn_pointer_to_deref(b, ptr);

   /* If the value we are interpolating has an index into a vector then
    * interpolate the vector and index the result of that instead. This is
    * necessary because the index will get generated as a series of nir_bcsel
    * instructions so it would no longer be an input variable.
    */
   const bool vec_array_deref = deref->deref_type == nir_deref_type_array &&
      glsl_type_is_vector(nir_deref_instr_parent(deref)->type);

   nir_deref_instr *vec_deref = NULL;
   if (vec_array_deref) {
      vec_deref = deref;
      deref = nir_deref_instr_parent(deref);
   }
   intrin->src[0] = nir_src_for_ssa(&deref->dest.ssa);
   intrin->src[1] = nir_src_for_ssa(vtn_ssa_value(b, w[6])->def);

   intrin->num_components = glsl_get_vector_elements(deref->type);
   nir_ssa_dest_init(&intrin->instr, &intrin->dest,
                     glsl_get_vector_elements(deref->type),
                     glsl_get_bit_size(deref->type), NULL);

   nir_builder_instr_insert(&b->nb, &intrin->instr);

   if (vec_array_deref) {
      assert(vec_deref);
      if (nir_src_is_const(vec_deref->arr.index)) {
         val->ssa->def = vtn_vector_extract(b, &intrin->dest.ssa,
                                            nir_src_as_uint(vec_deref->arr.index));
      } else {
         val->ssa->def = vtn_vector_extract_dynamic(b, &intrin->dest.ssa,
                                                    vec_deref->arr.index.ssa);
      }
   } else {
      val->ssa->def = &intrin->dest.ssa;
   }

   return true;
}
