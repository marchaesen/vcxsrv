/*
 * Copyright © 2018 Red Hat
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
 *    Rob Clark (robdclark@gmail.com)
 */

#include "math.h"

#include "nir/nir_builtin_builder.h"

#include "vtn_private.h"
#include "OpenCL.std.h"

typedef nir_ssa_def *(*nir_handler)(struct vtn_builder *b,
                                    enum OpenCLstd_Entrypoints opcode,
                                    unsigned num_srcs, nir_ssa_def **srcs,
                                    const struct glsl_type *dest_type);

static void
handle_instr(struct vtn_builder *b, enum OpenCLstd_Entrypoints opcode,
             const uint32_t *w, unsigned count, nir_handler handler)
{
   const struct glsl_type *dest_type =
      vtn_value(b, w[1], vtn_value_type_type)->type->type;

   unsigned num_srcs = count - 5;
   nir_ssa_def *srcs[3] = { NULL };
   vtn_assert(num_srcs <= ARRAY_SIZE(srcs));
   for (unsigned i = 0; i < num_srcs; i++) {
      srcs[i] = vtn_ssa_value(b, w[i + 5])->def;
   }

   nir_ssa_def *result = handler(b, opcode, num_srcs, srcs, dest_type);
   if (result) {
      struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_ssa);
      val->ssa = vtn_create_ssa_value(b, dest_type);
      val->ssa->def = result;
   } else {
      vtn_assert(dest_type == glsl_void_type());
   }
}

static nir_op
nir_alu_op_for_opencl_opcode(struct vtn_builder *b,
                             enum OpenCLstd_Entrypoints opcode)
{
   switch (opcode) {
   case OpenCLstd_Fabs: return nir_op_fabs;
   case OpenCLstd_SAbs: return nir_op_iabs;
   case OpenCLstd_SAdd_sat: return nir_op_iadd_sat;
   case OpenCLstd_UAdd_sat: return nir_op_uadd_sat;
   case OpenCLstd_Ceil: return nir_op_fceil;
   case OpenCLstd_Cos: return nir_op_fcos;
   case OpenCLstd_Exp2: return nir_op_fexp2;
   case OpenCLstd_Log2: return nir_op_flog2;
   case OpenCLstd_Floor: return nir_op_ffloor;
   case OpenCLstd_SHadd: return nir_op_ihadd;
   case OpenCLstd_UHadd: return nir_op_uhadd;
   case OpenCLstd_Fma: return nir_op_ffma;
   case OpenCLstd_Fmax: return nir_op_fmax;
   case OpenCLstd_SMax: return nir_op_imax;
   case OpenCLstd_UMax: return nir_op_umax;
   case OpenCLstd_Fmin: return nir_op_fmin;
   case OpenCLstd_SMin: return nir_op_imin;
   case OpenCLstd_UMin: return nir_op_umin;
   case OpenCLstd_Fmod: return nir_op_fmod;
   case OpenCLstd_Mix: return nir_op_flrp;
   case OpenCLstd_SMul_hi: return nir_op_imul_high;
   case OpenCLstd_UMul_hi: return nir_op_umul_high;
   case OpenCLstd_Popcount: return nir_op_bit_count;
   case OpenCLstd_Pow: return nir_op_fpow;
   case OpenCLstd_Remainder: return nir_op_frem;
   case OpenCLstd_SRhadd: return nir_op_irhadd;
   case OpenCLstd_URhadd: return nir_op_urhadd;
   case OpenCLstd_Rsqrt: return nir_op_frsq;
   case OpenCLstd_Sign: return nir_op_fsign;
   case OpenCLstd_Sin: return nir_op_fsin;
   case OpenCLstd_Sqrt: return nir_op_fsqrt;
   case OpenCLstd_SSub_sat: return nir_op_isub_sat;
   case OpenCLstd_USub_sat: return nir_op_usub_sat;
   case OpenCLstd_Trunc: return nir_op_ftrunc;
   /* uhm... */
   case OpenCLstd_UAbs: return nir_op_mov;
   default:
      vtn_fail("No NIR equivalent");
   }
}

static nir_ssa_def *
handle_alu(struct vtn_builder *b, enum OpenCLstd_Entrypoints opcode,
           unsigned num_srcs, nir_ssa_def **srcs,
           const struct glsl_type *dest_type)
{
   return nir_build_alu(&b->nb, nir_alu_op_for_opencl_opcode(b, opcode),
                        srcs[0], srcs[1], srcs[2], NULL);
}

static nir_ssa_def *
handle_special(struct vtn_builder *b, enum OpenCLstd_Entrypoints opcode,
               unsigned num_srcs, nir_ssa_def **srcs,
               const struct glsl_type *dest_type)
{
   nir_builder *nb = &b->nb;

   switch (opcode) {
   case OpenCLstd_SAbs_diff:
      return nir_iabs_diff(nb, srcs[0], srcs[1]);
   case OpenCLstd_UAbs_diff:
      return nir_uabs_diff(nb, srcs[0], srcs[1]);
   case OpenCLstd_Bitselect:
      return nir_bitselect(nb, srcs[0], srcs[1], srcs[2]);
   case OpenCLstd_FClamp:
      return nir_fclamp(nb, srcs[0], srcs[1], srcs[2]);
   case OpenCLstd_SClamp:
      return nir_iclamp(nb, srcs[0], srcs[1], srcs[2]);
   case OpenCLstd_UClamp:
      return nir_uclamp(nb, srcs[0], srcs[1], srcs[2]);
   case OpenCLstd_Copysign:
      return nir_copysign(nb, srcs[0], srcs[1]);
   case OpenCLstd_Cross:
      if (glsl_get_components(dest_type) == 4)
         return nir_cross4(nb, srcs[0], srcs[1]);
      return nir_cross3(nb, srcs[0], srcs[1]);
   case OpenCLstd_Degrees:
      return nir_degrees(nb, srcs[0]);
   case OpenCLstd_Fdim:
      return nir_fdim(nb, srcs[0], srcs[1]);
   case OpenCLstd_Distance:
      return nir_distance(nb, srcs[0], srcs[1]);
   case OpenCLstd_Fast_distance:
      return nir_fast_distance(nb, srcs[0], srcs[1]);
   case OpenCLstd_Fast_length:
      return nir_fast_length(nb, srcs[0]);
   case OpenCLstd_Fast_normalize:
      return nir_fast_normalize(nb, srcs[0]);
   case OpenCLstd_Length:
      return nir_length(nb, srcs[0]);
   case OpenCLstd_Mad:
      return nir_fmad(nb, srcs[0], srcs[1], srcs[2]);
   case OpenCLstd_Maxmag:
      return nir_maxmag(nb, srcs[0], srcs[1]);
   case OpenCLstd_Minmag:
      return nir_minmag(nb, srcs[0], srcs[1]);
   case OpenCLstd_Nan:
      return nir_nan(nb, srcs[0]);
   case OpenCLstd_Nextafter:
      return nir_nextafter(nb, srcs[0], srcs[1]);
   case OpenCLstd_Normalize:
      return nir_normalize(nb, srcs[0]);
   case OpenCLstd_Radians:
      return nir_radians(nb, srcs[0]);
   case OpenCLstd_Rotate:
      return nir_rotate(nb, srcs[0], srcs[1]);
   case OpenCLstd_Smoothstep:
      return nir_smoothstep(nb, srcs[0], srcs[1], srcs[2]);
   case OpenCLstd_Select:
      return nir_select(nb, srcs[0], srcs[1], srcs[2]);
   case OpenCLstd_Step:
      return nir_sge(nb, srcs[1], srcs[0]);
   case OpenCLstd_S_Upsample:
   case OpenCLstd_U_Upsample:
      return nir_upsample(nb, srcs[0], srcs[1]);
   default:
      vtn_fail("No NIR equivalent");
      return NULL;
   }
}

static void
_handle_v_load_store(struct vtn_builder *b, enum OpenCLstd_Entrypoints opcode,
                     const uint32_t *w, unsigned count, bool load)
{
   struct vtn_type *type;
   if (load)
      type = vtn_value(b, w[1], vtn_value_type_type)->type;
   else
      type = vtn_untyped_value(b, w[5])->type;
   unsigned a = load ? 0 : 1;

   const struct glsl_type *dest_type = type->type;
   unsigned components = glsl_get_vector_elements(dest_type);
   unsigned stride = components * glsl_get_bit_size(dest_type) / 8;

   nir_ssa_def *offset = vtn_ssa_value(b, w[5 + a])->def;
   struct vtn_value *p = vtn_value(b, w[6 + a], vtn_value_type_pointer);

   nir_deref_instr *deref = vtn_pointer_to_deref(b, p->pointer);

   /* 1. cast to vec type with adjusted stride */
   deref = nir_build_deref_cast(&b->nb, &deref->dest.ssa, deref->mode,
                                dest_type, stride);
   /* 2. deref ptr_as_array */
   deref = nir_build_deref_ptr_as_array(&b->nb, deref, offset);

   if (load) {
      struct vtn_ssa_value *val = vtn_local_load(b, deref, p->type->access);
      vtn_push_ssa(b, w[2], type, val);
   } else {
      struct vtn_ssa_value *val = vtn_ssa_value(b, w[5]);
      vtn_local_store(b, val, deref, p->type->access);
   }
}

static void
vtn_handle_opencl_vload(struct vtn_builder *b, enum OpenCLstd_Entrypoints opcode,
                        const uint32_t *w, unsigned count)
{
   _handle_v_load_store(b, opcode, w, count, true);
}

static void
vtn_handle_opencl_vstore(struct vtn_builder *b, enum OpenCLstd_Entrypoints opcode,
                         const uint32_t *w, unsigned count)
{
   _handle_v_load_store(b, opcode, w, count, false);
}

static nir_ssa_def *
handle_printf(struct vtn_builder *b, enum OpenCLstd_Entrypoints opcode,
              unsigned num_srcs, nir_ssa_def **srcs,
              const struct glsl_type *dest_type)
{
   /* hahah, yeah, right.. */
   return nir_imm_int(&b->nb, -1);
}

bool
vtn_handle_opencl_instruction(struct vtn_builder *b, SpvOp ext_opcode,
                              const uint32_t *w, unsigned count)
{
   switch ((enum OpenCLstd_Entrypoints)ext_opcode) {
   case OpenCLstd_Fabs:
   case OpenCLstd_SAbs:
   case OpenCLstd_UAbs:
   case OpenCLstd_SAdd_sat:
   case OpenCLstd_UAdd_sat:
   case OpenCLstd_Ceil:
   case OpenCLstd_Cos:
   case OpenCLstd_Exp2:
   case OpenCLstd_Log2:
   case OpenCLstd_Floor:
   case OpenCLstd_Fma:
   case OpenCLstd_Fmax:
   case OpenCLstd_SHadd:
   case OpenCLstd_UHadd:
   case OpenCLstd_SMax:
   case OpenCLstd_UMax:
   case OpenCLstd_Fmin:
   case OpenCLstd_SMin:
   case OpenCLstd_UMin:
   case OpenCLstd_Mix:
   case OpenCLstd_Fmod:
   case OpenCLstd_SMul_hi:
   case OpenCLstd_UMul_hi:
   case OpenCLstd_Popcount:
   case OpenCLstd_Pow:
   case OpenCLstd_Remainder:
   case OpenCLstd_SRhadd:
   case OpenCLstd_URhadd:
   case OpenCLstd_Rsqrt:
   case OpenCLstd_Sign:
   case OpenCLstd_Sin:
   case OpenCLstd_Sqrt:
   case OpenCLstd_SSub_sat:
   case OpenCLstd_USub_sat:
   case OpenCLstd_Trunc:
      handle_instr(b, ext_opcode, w, count, handle_alu);
      return true;
   case OpenCLstd_SAbs_diff:
   case OpenCLstd_UAbs_diff:
   case OpenCLstd_Bitselect:
   case OpenCLstd_FClamp:
   case OpenCLstd_SClamp:
   case OpenCLstd_UClamp:
   case OpenCLstd_Copysign:
   case OpenCLstd_Cross:
   case OpenCLstd_Degrees:
   case OpenCLstd_Fdim:
   case OpenCLstd_Distance:
   case OpenCLstd_Fast_distance:
   case OpenCLstd_Fast_length:
   case OpenCLstd_Fast_normalize:
   case OpenCLstd_Length:
   case OpenCLstd_Mad:
   case OpenCLstd_Maxmag:
   case OpenCLstd_Minmag:
   case OpenCLstd_Nan:
   case OpenCLstd_Nextafter:
   case OpenCLstd_Normalize:
   case OpenCLstd_Radians:
   case OpenCLstd_Rotate:
   case OpenCLstd_Select:
   case OpenCLstd_Step:
   case OpenCLstd_Smoothstep:
   case OpenCLstd_S_Upsample:
   case OpenCLstd_U_Upsample:
      handle_instr(b, ext_opcode, w, count, handle_special);
      return true;
   case OpenCLstd_Vloadn:
      vtn_handle_opencl_vload(b, ext_opcode, w, count);
      return true;
   case OpenCLstd_Vstoren:
      vtn_handle_opencl_vstore(b, ext_opcode, w, count);
      return true;
   case OpenCLstd_Printf:
      handle_instr(b, ext_opcode, w, count, handle_printf);
      return true;
   case OpenCLstd_Prefetch:
      /* TODO maybe add a nir instruction for this? */
      return true;
   default:
      vtn_fail("unhandled opencl opc: %u\n", ext_opcode);
      return false;
   }
}
