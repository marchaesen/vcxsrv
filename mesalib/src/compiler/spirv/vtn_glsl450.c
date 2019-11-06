/*
 * Copyright © 2015 Intel Corporation
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
 *    Jason Ekstrand (jason@jlekstrand.net)
 *
 */

#include <math.h>

#include "nir/nir_builtin_builder.h"

#include "vtn_private.h"
#include "GLSL.std.450.h"

#define M_PIf   ((float) M_PI)
#define M_PI_2f ((float) M_PI_2)
#define M_PI_4f ((float) M_PI_4)

static nir_ssa_def *
build_mat2_det(nir_builder *b, nir_ssa_def *col[2])
{
   unsigned swiz[2] = {1, 0 };
   nir_ssa_def *p = nir_fmul(b, col[0], nir_swizzle(b, col[1], swiz, 2));
   return nir_fsub(b, nir_channel(b, p, 0), nir_channel(b, p, 1));
}

static nir_ssa_def *
build_mat3_det(nir_builder *b, nir_ssa_def *col[3])
{
   unsigned yzx[3] = {1, 2, 0 };
   unsigned zxy[3] = {2, 0, 1 };

   nir_ssa_def *prod0 =
      nir_fmul(b, col[0],
               nir_fmul(b, nir_swizzle(b, col[1], yzx, 3),
                           nir_swizzle(b, col[2], zxy, 3)));
   nir_ssa_def *prod1 =
      nir_fmul(b, col[0],
               nir_fmul(b, nir_swizzle(b, col[1], zxy, 3),
                           nir_swizzle(b, col[2], yzx, 3)));

   nir_ssa_def *diff = nir_fsub(b, prod0, prod1);

   return nir_fadd(b, nir_channel(b, diff, 0),
                      nir_fadd(b, nir_channel(b, diff, 1),
                                  nir_channel(b, diff, 2)));
}

static nir_ssa_def *
build_mat4_det(nir_builder *b, nir_ssa_def **col)
{
   nir_ssa_def *subdet[4];
   for (unsigned i = 0; i < 4; i++) {
      unsigned swiz[3];
      for (unsigned j = 0; j < 3; j++)
         swiz[j] = j + (j >= i);

      nir_ssa_def *subcol[3];
      subcol[0] = nir_swizzle(b, col[1], swiz, 3);
      subcol[1] = nir_swizzle(b, col[2], swiz, 3);
      subcol[2] = nir_swizzle(b, col[3], swiz, 3);

      subdet[i] = build_mat3_det(b, subcol);
   }

   nir_ssa_def *prod = nir_fmul(b, col[0], nir_vec(b, subdet, 4));

   return nir_fadd(b, nir_fsub(b, nir_channel(b, prod, 0),
                                  nir_channel(b, prod, 1)),
                      nir_fsub(b, nir_channel(b, prod, 2),
                                  nir_channel(b, prod, 3)));
}

static nir_ssa_def *
build_mat_det(struct vtn_builder *b, struct vtn_ssa_value *src)
{
   unsigned size = glsl_get_vector_elements(src->type);

   nir_ssa_def *cols[4];
   for (unsigned i = 0; i < size; i++)
      cols[i] = src->elems[i]->def;

   switch(size) {
   case 2: return build_mat2_det(&b->nb, cols);
   case 3: return build_mat3_det(&b->nb, cols);
   case 4: return build_mat4_det(&b->nb, cols);
   default:
      vtn_fail("Invalid matrix size");
   }
}

/* Computes the determinate of the submatrix given by taking src and
 * removing the specified row and column.
 */
static nir_ssa_def *
build_mat_subdet(struct nir_builder *b, struct vtn_ssa_value *src,
                 unsigned size, unsigned row, unsigned col)
{
   assert(row < size && col < size);
   if (size == 2) {
      return nir_channel(b, src->elems[1 - col]->def, 1 - row);
   } else {
      /* Swizzle to get all but the specified row */
      unsigned swiz[3];
      for (unsigned j = 0; j < 3; j++)
         swiz[j] = j + (j >= row);

      /* Grab all but the specified column */
      nir_ssa_def *subcol[3];
      for (unsigned j = 0; j < size; j++) {
         if (j != col) {
            subcol[j - (j > col)] = nir_swizzle(b, src->elems[j]->def,
                                                swiz, size - 1);
         }
      }

      if (size == 3) {
         return build_mat2_det(b, subcol);
      } else {
         assert(size == 4);
         return build_mat3_det(b, subcol);
      }
   }
}

static struct vtn_ssa_value *
matrix_inverse(struct vtn_builder *b, struct vtn_ssa_value *src)
{
   nir_ssa_def *adj_col[4];
   unsigned size = glsl_get_vector_elements(src->type);

   /* Build up an adjugate matrix */
   for (unsigned c = 0; c < size; c++) {
      nir_ssa_def *elem[4];
      for (unsigned r = 0; r < size; r++) {
         elem[r] = build_mat_subdet(&b->nb, src, size, c, r);

         if ((r + c) % 2)
            elem[r] = nir_fneg(&b->nb, elem[r]);
      }

      adj_col[c] = nir_vec(&b->nb, elem, size);
   }

   nir_ssa_def *det_inv = nir_frcp(&b->nb, build_mat_det(b, src));

   struct vtn_ssa_value *val = vtn_create_ssa_value(b, src->type);
   for (unsigned i = 0; i < size; i++)
      val->elems[i]->def = nir_fmul(&b->nb, adj_col[i], det_inv);

   return val;
}

/**
 * Return e^x.
 */
static nir_ssa_def *
build_exp(nir_builder *b, nir_ssa_def *x)
{
   return nir_fexp2(b, nir_fmul_imm(b, x, M_LOG2E));
}

/**
 * Return ln(x) - the natural logarithm of x.
 */
static nir_ssa_def *
build_log(nir_builder *b, nir_ssa_def *x)
{
   return nir_fmul_imm(b, nir_flog2(b, x), 1.0 / M_LOG2E);
}

/**
 * Approximate asin(x) by the formula:
 *    asin~(x) = sign(x) * (pi/2 - sqrt(1 - |x|) * (pi/2 + |x|(pi/4 - 1 + |x|(p0 + |x|p1))))
 *
 * which is correct to first order at x=0 and x=±1 regardless of the p
 * coefficients but can be made second-order correct at both ends by selecting
 * the fit coefficients appropriately.  Different p coefficients can be used
 * in the asin and acos implementation to minimize some relative error metric
 * in each case.
 */
static nir_ssa_def *
build_asin(nir_builder *b, nir_ssa_def *x, float p0, float p1)
{
   if (x->bit_size == 16) {
      /* The polynomial approximation isn't precise enough to meet half-float
       * precision requirements. Alternatively, we could implement this using
       * the formula:
       *
       * asin(x) = atan2(x, sqrt(1 - x*x))
       *
       * But that is very expensive, so instead we just do the polynomial
       * approximation in 32-bit math and then we convert the result back to
       * 16-bit.
       */
      return nir_f2f16(b, build_asin(b, nir_f2f32(b, x), p0, p1));
   }

   nir_ssa_def *one = nir_imm_floatN_t(b, 1.0f, x->bit_size);
   nir_ssa_def *abs_x = nir_fabs(b, x);

   nir_ssa_def *p0_plus_xp1 = nir_fadd_imm(b, nir_fmul_imm(b, abs_x, p1), p0);

   nir_ssa_def *expr_tail =
      nir_fadd_imm(b, nir_fmul(b, abs_x,
                                  nir_fadd_imm(b, nir_fmul(b, abs_x,
                                                               p0_plus_xp1),
                                                  M_PI_4f - 1.0f)),
                      M_PI_2f);

   return nir_fmul(b, nir_fsign(b, x),
                      nir_fsub(b, nir_imm_floatN_t(b, M_PI_2f, x->bit_size),
                                  nir_fmul(b, nir_fsqrt(b, nir_fsub(b, one, abs_x)),
                                                           expr_tail)));
}

static nir_op
vtn_nir_alu_op_for_spirv_glsl_opcode(struct vtn_builder *b,
                                     enum GLSLstd450 opcode,
                                     unsigned execution_mode)
{
   switch (opcode) {
   case GLSLstd450Round:         return nir_op_fround_even;
   case GLSLstd450RoundEven:     return nir_op_fround_even;
   case GLSLstd450Trunc:         return nir_op_ftrunc;
   case GLSLstd450FAbs:          return nir_op_fabs;
   case GLSLstd450SAbs:          return nir_op_iabs;
   case GLSLstd450FSign:         return nir_op_fsign;
   case GLSLstd450SSign:         return nir_op_isign;
   case GLSLstd450Floor:         return nir_op_ffloor;
   case GLSLstd450Ceil:          return nir_op_fceil;
   case GLSLstd450Fract:         return nir_op_ffract;
   case GLSLstd450Sin:           return nir_op_fsin;
   case GLSLstd450Cos:           return nir_op_fcos;
   case GLSLstd450Pow:           return nir_op_fpow;
   case GLSLstd450Exp2:          return nir_op_fexp2;
   case GLSLstd450Log2:          return nir_op_flog2;
   case GLSLstd450Sqrt:          return nir_op_fsqrt;
   case GLSLstd450InverseSqrt:   return nir_op_frsq;
   case GLSLstd450NMin:          return nir_op_fmin;
   case GLSLstd450FMin:          return nir_op_fmin;
   case GLSLstd450UMin:          return nir_op_umin;
   case GLSLstd450SMin:          return nir_op_imin;
   case GLSLstd450NMax:          return nir_op_fmax;
   case GLSLstd450FMax:          return nir_op_fmax;
   case GLSLstd450UMax:          return nir_op_umax;
   case GLSLstd450SMax:          return nir_op_imax;
   case GLSLstd450FMix:          return nir_op_flrp;
   case GLSLstd450Fma:           return nir_op_ffma;
   case GLSLstd450Ldexp:         return nir_op_ldexp;
   case GLSLstd450FindILsb:      return nir_op_find_lsb;
   case GLSLstd450FindSMsb:      return nir_op_ifind_msb;
   case GLSLstd450FindUMsb:      return nir_op_ufind_msb;

   /* Packing/Unpacking functions */
   case GLSLstd450PackSnorm4x8:     return nir_op_pack_snorm_4x8;
   case GLSLstd450PackUnorm4x8:     return nir_op_pack_unorm_4x8;
   case GLSLstd450PackSnorm2x16:    return nir_op_pack_snorm_2x16;
   case GLSLstd450PackUnorm2x16:    return nir_op_pack_unorm_2x16;
   case GLSLstd450PackHalf2x16:     return nir_op_pack_half_2x16;
   case GLSLstd450PackDouble2x32:   return nir_op_pack_64_2x32;
   case GLSLstd450UnpackSnorm4x8:   return nir_op_unpack_snorm_4x8;
   case GLSLstd450UnpackUnorm4x8:   return nir_op_unpack_unorm_4x8;
   case GLSLstd450UnpackSnorm2x16:  return nir_op_unpack_snorm_2x16;
   case GLSLstd450UnpackUnorm2x16:  return nir_op_unpack_unorm_2x16;
   case GLSLstd450UnpackHalf2x16:
      if (execution_mode & FLOAT_CONTROLS_DENORM_FLUSH_TO_ZERO_FP16)
         return nir_op_unpack_half_2x16_flush_to_zero;
      else
         return nir_op_unpack_half_2x16;
   case GLSLstd450UnpackDouble2x32: return nir_op_unpack_64_2x32;

   default:
      vtn_fail("No NIR equivalent");
   }
}

#define NIR_IMM_FP(n, v) (nir_imm_floatN_t(n, v, src[0]->bit_size))

static void
handle_glsl450_alu(struct vtn_builder *b, enum GLSLstd450 entrypoint,
                   const uint32_t *w, unsigned count)
{
   struct nir_builder *nb = &b->nb;
   const struct glsl_type *dest_type =
      vtn_value(b, w[1], vtn_value_type_type)->type->type;

   struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_ssa);
   val->ssa = vtn_create_ssa_value(b, dest_type);

   /* Collect the various SSA sources */
   unsigned num_inputs = count - 5;
   nir_ssa_def *src[3] = { NULL, };
   for (unsigned i = 0; i < num_inputs; i++) {
      /* These are handled specially below */
      if (vtn_untyped_value(b, w[i + 5])->value_type == vtn_value_type_pointer)
         continue;

      src[i] = vtn_ssa_value(b, w[i + 5])->def;
   }

   switch (entrypoint) {
   case GLSLstd450Radians:
      val->ssa->def = nir_radians(nb, src[0]);
      return;
   case GLSLstd450Degrees:
      val->ssa->def = nir_degrees(nb, src[0]);
      return;
   case GLSLstd450Tan:
      val->ssa->def = nir_fdiv(nb, nir_fsin(nb, src[0]),
                               nir_fcos(nb, src[0]));
      return;

   case GLSLstd450Modf: {
      nir_ssa_def *sign = nir_fsign(nb, src[0]);
      nir_ssa_def *abs = nir_fabs(nb, src[0]);
      val->ssa->def = nir_fmul(nb, sign, nir_ffract(nb, abs));
      nir_store_deref(nb, vtn_nir_deref(b, w[6]),
                      nir_fmul(nb, sign, nir_ffloor(nb, abs)), 0xf);
      return;
   }

   case GLSLstd450ModfStruct: {
      nir_ssa_def *sign = nir_fsign(nb, src[0]);
      nir_ssa_def *abs = nir_fabs(nb, src[0]);
      vtn_assert(glsl_type_is_struct_or_ifc(val->ssa->type));
      val->ssa->elems[0]->def = nir_fmul(nb, sign, nir_ffract(nb, abs));
      val->ssa->elems[1]->def = nir_fmul(nb, sign, nir_ffloor(nb, abs));
      return;
   }

   case GLSLstd450Step:
      val->ssa->def = nir_sge(nb, src[1], src[0]);
      return;

   case GLSLstd450Length:
      val->ssa->def = nir_fast_length(nb, src[0]);
      return;
   case GLSLstd450Distance:
      val->ssa->def = nir_fast_distance(nb, src[0], src[1]);
      return;
   case GLSLstd450Normalize:
      val->ssa->def = nir_fast_normalize(nb, src[0]);
      return;

   case GLSLstd450Exp:
      val->ssa->def = build_exp(nb, src[0]);
      return;

   case GLSLstd450Log:
      val->ssa->def = build_log(nb, src[0]);
      return;

   case GLSLstd450FClamp:
   case GLSLstd450NClamp:
      val->ssa->def = nir_fclamp(nb, src[0], src[1], src[2]);
      return;
   case GLSLstd450UClamp:
      val->ssa->def = nir_uclamp(nb, src[0], src[1], src[2]);
      return;
   case GLSLstd450SClamp:
      val->ssa->def = nir_iclamp(nb, src[0], src[1], src[2]);
      return;

   case GLSLstd450Cross: {
      val->ssa->def = nir_cross3(nb, src[0], src[1]);
      return;
   }

   case GLSLstd450SmoothStep: {
      val->ssa->def = nir_smoothstep(nb, src[0], src[1], src[2]);
      return;
   }

   case GLSLstd450FaceForward:
      val->ssa->def =
         nir_bcsel(nb, nir_flt(nb, nir_fdot(nb, src[2], src[1]),
                                   NIR_IMM_FP(nb, 0.0)),
                       src[0], nir_fneg(nb, src[0]));
      return;

   case GLSLstd450Reflect:
      /* I - 2 * dot(N, I) * N */
      val->ssa->def =
         nir_fsub(nb, src[0], nir_fmul(nb, NIR_IMM_FP(nb, 2.0),
                              nir_fmul(nb, nir_fdot(nb, src[0], src[1]),
                                           src[1])));
      return;

   case GLSLstd450Refract: {
      nir_ssa_def *I = src[0];
      nir_ssa_def *N = src[1];
      nir_ssa_def *eta = src[2];
      nir_ssa_def *n_dot_i = nir_fdot(nb, N, I);
      nir_ssa_def *one = NIR_IMM_FP(nb, 1.0);
      nir_ssa_def *zero = NIR_IMM_FP(nb, 0.0);
      /* According to the SPIR-V and GLSL specs, eta is always a float
       * regardless of the type of the other operands. However in practice it
       * seems that if you try to pass it a float then glslang will just
       * promote it to a double and generate invalid SPIR-V. In order to
       * support a hypothetical fixed version of glslang we’ll promote eta to
       * double if the other operands are double also.
       */
      if (I->bit_size != eta->bit_size) {
         nir_op conversion_op =
            nir_type_conversion_op(nir_type_float | eta->bit_size,
                                   nir_type_float | I->bit_size,
                                   nir_rounding_mode_undef);
         eta = nir_build_alu(nb, conversion_op, eta, NULL, NULL, NULL);
      }
      /* k = 1.0 - eta * eta * (1.0 - dot(N, I) * dot(N, I)) */
      nir_ssa_def *k =
         nir_fsub(nb, one, nir_fmul(nb, eta, nir_fmul(nb, eta,
                      nir_fsub(nb, one, nir_fmul(nb, n_dot_i, n_dot_i)))));
      nir_ssa_def *result =
         nir_fsub(nb, nir_fmul(nb, eta, I),
                      nir_fmul(nb, nir_fadd(nb, nir_fmul(nb, eta, n_dot_i),
                                                nir_fsqrt(nb, k)), N));
      /* XXX: bcsel, or if statement? */
      val->ssa->def = nir_bcsel(nb, nir_flt(nb, k, zero), zero, result);
      return;
   }

   case GLSLstd450Sinh:
      /* 0.5 * (e^x - e^(-x)) */
      val->ssa->def =
         nir_fmul_imm(nb, nir_fsub(nb, build_exp(nb, src[0]),
                                       build_exp(nb, nir_fneg(nb, src[0]))),
                          0.5f);
      return;

   case GLSLstd450Cosh:
      /* 0.5 * (e^x + e^(-x)) */
      val->ssa->def =
         nir_fmul_imm(nb, nir_fadd(nb, build_exp(nb, src[0]),
                                       build_exp(nb, nir_fneg(nb, src[0]))),
                          0.5f);
      return;

   case GLSLstd450Tanh: {
      /* tanh(x) := (0.5 * (e^x - e^(-x))) / (0.5 * (e^x + e^(-x)))
       *
       * With a little algebra this reduces to (e^2x - 1) / (e^2x + 1)
       *
       * We clamp x to (-inf, +10] to avoid precision problems.  When x > 10,
       * e^2x is so much larger than 1.0 that 1.0 gets flushed to zero in the
       * computation e^2x +/- 1 so it can be ignored.
       *
       * For 16-bit precision we clamp x to (-inf, +4.2] since the maximum
       * representable number is only 65,504 and e^(2*6) exceeds that. Also,
       * if x > 4.2, tanh(x) will return 1.0 in fp16.
       */
      const uint32_t bit_size = src[0]->bit_size;
      const double clamped_x = bit_size > 16 ? 10.0 : 4.2;
      nir_ssa_def *x = nir_fmin(nb, src[0],
                                    nir_imm_floatN_t(nb, clamped_x, bit_size));
      nir_ssa_def *exp2x = build_exp(nb, nir_fmul_imm(nb, x, 2.0));
      val->ssa->def = nir_fdiv(nb, nir_fadd_imm(nb, exp2x, -1.0),
                                   nir_fadd_imm(nb, exp2x, 1.0));
      return;
   }

   case GLSLstd450Asinh:
      val->ssa->def = nir_fmul(nb, nir_fsign(nb, src[0]),
         build_log(nb, nir_fadd(nb, nir_fabs(nb, src[0]),
                       nir_fsqrt(nb, nir_fadd_imm(nb, nir_fmul(nb, src[0], src[0]),
                                                      1.0f)))));
      return;
   case GLSLstd450Acosh:
      val->ssa->def = build_log(nb, nir_fadd(nb, src[0],
         nir_fsqrt(nb, nir_fadd_imm(nb, nir_fmul(nb, src[0], src[0]),
                                        -1.0f))));
      return;
   case GLSLstd450Atanh: {
      nir_ssa_def *one = nir_imm_floatN_t(nb, 1.0, src[0]->bit_size);
      val->ssa->def =
         nir_fmul_imm(nb, build_log(nb, nir_fdiv(nb, nir_fadd(nb, src[0], one),
                                        nir_fsub(nb, one, src[0]))),
                          0.5f);
      return;
   }

   case GLSLstd450Asin:
      val->ssa->def = build_asin(nb, src[0], 0.086566724, -0.03102955);
      return;

   case GLSLstd450Acos:
      val->ssa->def =
         nir_fsub(nb, nir_imm_floatN_t(nb, M_PI_2f, src[0]->bit_size),
                      build_asin(nb, src[0], 0.08132463, -0.02363318));
      return;

   case GLSLstd450Atan:
      val->ssa->def = nir_atan(nb, src[0]);
      return;

   case GLSLstd450Atan2:
      val->ssa->def = nir_atan2(nb, src[0], src[1]);
      return;

   case GLSLstd450Frexp: {
      nir_ssa_def *exponent = nir_frexp_exp(nb, src[0]);
      val->ssa->def = nir_frexp_sig(nb, src[0]);
      nir_store_deref(nb, vtn_nir_deref(b, w[6]), exponent, 0xf);
      return;
   }

   case GLSLstd450FrexpStruct: {
      vtn_assert(glsl_type_is_struct_or_ifc(val->ssa->type));
      val->ssa->elems[0]->def = nir_frexp_sig(nb, src[0]);
      val->ssa->elems[1]->def = nir_frexp_exp(nb, src[0]);
      return;
   }

   default: {
      unsigned execution_mode =
         b->shader->info.float_controls_execution_mode;
      val->ssa->def =
         nir_build_alu(&b->nb,
                       vtn_nir_alu_op_for_spirv_glsl_opcode(b, entrypoint, execution_mode),
                       src[0], src[1], src[2], NULL);
      return;
   }
   }
}

static void
handle_glsl450_interpolation(struct vtn_builder *b, enum GLSLstd450 opcode,
                             const uint32_t *w, unsigned count)
{
   const struct glsl_type *dest_type =
      vtn_value(b, w[1], vtn_value_type_type)->type->type;

   struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_ssa);
   val->ssa = vtn_create_ssa_value(b, dest_type);

   nir_intrinsic_op op;
   switch (opcode) {
   case GLSLstd450InterpolateAtCentroid:
      op = nir_intrinsic_interp_deref_at_centroid;
      break;
   case GLSLstd450InterpolateAtSample:
      op = nir_intrinsic_interp_deref_at_sample;
      break;
   case GLSLstd450InterpolateAtOffset:
      op = nir_intrinsic_interp_deref_at_offset;
      break;
   default:
      vtn_fail("Invalid opcode");
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

   switch (opcode) {
   case GLSLstd450InterpolateAtCentroid:
      break;
   case GLSLstd450InterpolateAtSample:
   case GLSLstd450InterpolateAtOffset:
      intrin->src[1] = nir_src_for_ssa(vtn_ssa_value(b, w[6])->def);
      break;
   default:
      vtn_fail("Invalid opcode");
   }

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
}

bool
vtn_handle_glsl450_instruction(struct vtn_builder *b, SpvOp ext_opcode,
                               const uint32_t *w, unsigned count)
{
   switch ((enum GLSLstd450)ext_opcode) {
   case GLSLstd450Determinant: {
      struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_ssa);
      val->ssa = rzalloc(b, struct vtn_ssa_value);
      val->ssa->type = vtn_value(b, w[1], vtn_value_type_type)->type->type;
      val->ssa->def = build_mat_det(b, vtn_ssa_value(b, w[5]));
      break;
   }

   case GLSLstd450MatrixInverse: {
      struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_ssa);
      val->ssa = matrix_inverse(b, vtn_ssa_value(b, w[5]));
      break;
   }

   case GLSLstd450InterpolateAtCentroid:
   case GLSLstd450InterpolateAtSample:
   case GLSLstd450InterpolateAtOffset:
      handle_glsl450_interpolation(b, (enum GLSLstd450)ext_opcode, w, count);
      break;

   default:
      handle_glsl450_alu(b, (enum GLSLstd450)ext_opcode, w, count);
   }

   return true;
}
