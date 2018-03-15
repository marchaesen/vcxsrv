/*
 * Copyright © 2016 Intel Corporation
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
 */

#include "vtn_private.h"

/*
 * Normally, column vectors in SPIR-V correspond to a single NIR SSA
 * definition. But for matrix multiplies, we want to do one routine for
 * multiplying a matrix by a matrix and then pretend that vectors are matrices
 * with one column. So we "wrap" these things, and unwrap the result before we
 * send it off.
 */

static struct vtn_ssa_value *
wrap_matrix(struct vtn_builder *b, struct vtn_ssa_value *val)
{
   if (val == NULL)
      return NULL;

   if (glsl_type_is_matrix(val->type))
      return val;

   struct vtn_ssa_value *dest = rzalloc(b, struct vtn_ssa_value);
   dest->type = val->type;
   dest->elems = ralloc_array(b, struct vtn_ssa_value *, 1);
   dest->elems[0] = val;

   return dest;
}

static struct vtn_ssa_value *
unwrap_matrix(struct vtn_ssa_value *val)
{
   if (glsl_type_is_matrix(val->type))
         return val;

   return val->elems[0];
}

static struct vtn_ssa_value *
matrix_multiply(struct vtn_builder *b,
                struct vtn_ssa_value *_src0, struct vtn_ssa_value *_src1)
{

   struct vtn_ssa_value *src0 = wrap_matrix(b, _src0);
   struct vtn_ssa_value *src1 = wrap_matrix(b, _src1);
   struct vtn_ssa_value *src0_transpose = wrap_matrix(b, _src0->transposed);
   struct vtn_ssa_value *src1_transpose = wrap_matrix(b, _src1->transposed);

   unsigned src0_rows = glsl_get_vector_elements(src0->type);
   unsigned src0_columns = glsl_get_matrix_columns(src0->type);
   unsigned src1_columns = glsl_get_matrix_columns(src1->type);

   const struct glsl_type *dest_type;
   if (src1_columns > 1) {
      dest_type = glsl_matrix_type(glsl_get_base_type(src0->type),
                                   src0_rows, src1_columns);
   } else {
      dest_type = glsl_vector_type(glsl_get_base_type(src0->type), src0_rows);
   }
   struct vtn_ssa_value *dest = vtn_create_ssa_value(b, dest_type);

   dest = wrap_matrix(b, dest);

   bool transpose_result = false;
   if (src0_transpose && src1_transpose) {
      /* transpose(A) * transpose(B) = transpose(B * A) */
      src1 = src0_transpose;
      src0 = src1_transpose;
      src0_transpose = NULL;
      src1_transpose = NULL;
      transpose_result = true;
   }

   if (src0_transpose && !src1_transpose &&
       glsl_get_base_type(src0->type) == GLSL_TYPE_FLOAT) {
      /* We already have the rows of src0 and the columns of src1 available,
       * so we can just take the dot product of each row with each column to
       * get the result.
       */

      for (unsigned i = 0; i < src1_columns; i++) {
         nir_ssa_def *vec_src[4];
         for (unsigned j = 0; j < src0_rows; j++) {
            vec_src[j] = nir_fdot(&b->nb, src0_transpose->elems[j]->def,
                                          src1->elems[i]->def);
         }
         dest->elems[i]->def = nir_vec(&b->nb, vec_src, src0_rows);
      }
   } else {
      /* We don't handle the case where src1 is transposed but not src0, since
       * the general case only uses individual components of src1 so the
       * optimizer should chew through the transpose we emitted for src1.
       */

      for (unsigned i = 0; i < src1_columns; i++) {
         /* dest[i] = sum(src0[j] * src1[i][j] for all j) */
         dest->elems[i]->def =
            nir_fmul(&b->nb, src0->elems[0]->def,
                     nir_channel(&b->nb, src1->elems[i]->def, 0));
         for (unsigned j = 1; j < src0_columns; j++) {
            dest->elems[i]->def =
               nir_fadd(&b->nb, dest->elems[i]->def,
                        nir_fmul(&b->nb, src0->elems[j]->def,
                                 nir_channel(&b->nb, src1->elems[i]->def, j)));
         }
      }
   }

   dest = unwrap_matrix(dest);

   if (transpose_result)
      dest = vtn_ssa_transpose(b, dest);

   return dest;
}

static struct vtn_ssa_value *
mat_times_scalar(struct vtn_builder *b,
                 struct vtn_ssa_value *mat,
                 nir_ssa_def *scalar)
{
   struct vtn_ssa_value *dest = vtn_create_ssa_value(b, mat->type);
   for (unsigned i = 0; i < glsl_get_matrix_columns(mat->type); i++) {
      if (glsl_base_type_is_integer(glsl_get_base_type(mat->type)))
         dest->elems[i]->def = nir_imul(&b->nb, mat->elems[i]->def, scalar);
      else
         dest->elems[i]->def = nir_fmul(&b->nb, mat->elems[i]->def, scalar);
   }

   return dest;
}

static void
vtn_handle_matrix_alu(struct vtn_builder *b, SpvOp opcode,
                      struct vtn_value *dest,
                      struct vtn_ssa_value *src0, struct vtn_ssa_value *src1)
{
   switch (opcode) {
   case SpvOpFNegate: {
      dest->ssa = vtn_create_ssa_value(b, src0->type);
      unsigned cols = glsl_get_matrix_columns(src0->type);
      for (unsigned i = 0; i < cols; i++)
         dest->ssa->elems[i]->def = nir_fneg(&b->nb, src0->elems[i]->def);
      break;
   }

   case SpvOpFAdd: {
      dest->ssa = vtn_create_ssa_value(b, src0->type);
      unsigned cols = glsl_get_matrix_columns(src0->type);
      for (unsigned i = 0; i < cols; i++)
         dest->ssa->elems[i]->def =
            nir_fadd(&b->nb, src0->elems[i]->def, src1->elems[i]->def);
      break;
   }

   case SpvOpFSub: {
      dest->ssa = vtn_create_ssa_value(b, src0->type);
      unsigned cols = glsl_get_matrix_columns(src0->type);
      for (unsigned i = 0; i < cols; i++)
         dest->ssa->elems[i]->def =
            nir_fsub(&b->nb, src0->elems[i]->def, src1->elems[i]->def);
      break;
   }

   case SpvOpTranspose:
      dest->ssa = vtn_ssa_transpose(b, src0);
      break;

   case SpvOpMatrixTimesScalar:
      if (src0->transposed) {
         dest->ssa = vtn_ssa_transpose(b, mat_times_scalar(b, src0->transposed,
                                                           src1->def));
      } else {
         dest->ssa = mat_times_scalar(b, src0, src1->def);
      }
      break;

   case SpvOpVectorTimesMatrix:
   case SpvOpMatrixTimesVector:
   case SpvOpMatrixTimesMatrix:
      if (opcode == SpvOpVectorTimesMatrix) {
         dest->ssa = matrix_multiply(b, vtn_ssa_transpose(b, src1), src0);
      } else {
         dest->ssa = matrix_multiply(b, src0, src1);
      }
      break;

   default: vtn_fail("unknown matrix opcode");
   }
}

static void
vtn_handle_bitcast(struct vtn_builder *b, struct vtn_ssa_value *dest,
                   struct nir_ssa_def *src)
{
   if (glsl_get_vector_elements(dest->type) == src->num_components) {
      /* From the definition of OpBitcast in the SPIR-V 1.2 spec:
       *
       * "If Result Type has the same number of components as Operand, they
       * must also have the same component width, and results are computed per
       * component."
       */
      dest->def = nir_imov(&b->nb, src);
      return;
   }

   /* From the definition of OpBitcast in the SPIR-V 1.2 spec:
    *
    * "If Result Type has a different number of components than Operand, the
    * total number of bits in Result Type must equal the total number of bits
    * in Operand. Let L be the type, either Result Type or Operand’s type, that
    * has the larger number of components. Let S be the other type, with the
    * smaller number of components. The number of components in L must be an
    * integer multiple of the number of components in S. The first component
    * (that is, the only or lowest-numbered component) of S maps to the first
    * components of L, and so on, up to the last component of S mapping to the
    * last components of L. Within this mapping, any single component of S
    * (mapping to multiple components of L) maps its lower-ordered bits to the
    * lower-numbered components of L."
    */
   unsigned src_bit_size = src->bit_size;
   unsigned dest_bit_size = glsl_get_bit_size(dest->type);
   unsigned src_components = src->num_components;
   unsigned dest_components = glsl_get_vector_elements(dest->type);
   vtn_assert(src_bit_size * src_components == dest_bit_size * dest_components);

   nir_ssa_def *dest_chan[4];
   if (src_bit_size > dest_bit_size) {
      vtn_assert(src_bit_size % dest_bit_size == 0);
      unsigned divisor = src_bit_size / dest_bit_size;
      for (unsigned comp = 0; comp < src_components; comp++) {
         vtn_assert(src_bit_size == 64);
         vtn_assert(dest_bit_size == 32);
         nir_ssa_def *split =
            nir_unpack_64_2x32(&b->nb, nir_channel(&b->nb, src, comp));
         for (unsigned i = 0; i < divisor; i++)
            dest_chan[divisor * comp + i] = nir_channel(&b->nb, split, i);
      }
   } else {
      vtn_assert(dest_bit_size % src_bit_size == 0);
      unsigned divisor = dest_bit_size / src_bit_size;
      for (unsigned comp = 0; comp < dest_components; comp++) {
         unsigned channels = ((1 << divisor) - 1) << (comp * divisor);
         nir_ssa_def *src_chan =
            nir_channels(&b->nb, src, channels);
         vtn_assert(dest_bit_size == 64);
         vtn_assert(src_bit_size == 32);
         dest_chan[comp] = nir_pack_64_2x32(&b->nb, src_chan);
      }
   }
   dest->def = nir_vec(&b->nb, dest_chan, dest_components);
}

nir_op
vtn_nir_alu_op_for_spirv_opcode(struct vtn_builder *b,
                                SpvOp opcode, bool *swap,
                                unsigned src_bit_size, unsigned dst_bit_size)
{
   /* Indicates that the first two arguments should be swapped.  This is
    * used for implementing greater-than and less-than-or-equal.
    */
   *swap = false;

   switch (opcode) {
   case SpvOpSNegate:            return nir_op_ineg;
   case SpvOpFNegate:            return nir_op_fneg;
   case SpvOpNot:                return nir_op_inot;
   case SpvOpIAdd:               return nir_op_iadd;
   case SpvOpFAdd:               return nir_op_fadd;
   case SpvOpISub:               return nir_op_isub;
   case SpvOpFSub:               return nir_op_fsub;
   case SpvOpIMul:               return nir_op_imul;
   case SpvOpFMul:               return nir_op_fmul;
   case SpvOpUDiv:               return nir_op_udiv;
   case SpvOpSDiv:               return nir_op_idiv;
   case SpvOpFDiv:               return nir_op_fdiv;
   case SpvOpUMod:               return nir_op_umod;
   case SpvOpSMod:               return nir_op_imod;
   case SpvOpFMod:               return nir_op_fmod;
   case SpvOpSRem:               return nir_op_irem;
   case SpvOpFRem:               return nir_op_frem;

   case SpvOpShiftRightLogical:     return nir_op_ushr;
   case SpvOpShiftRightArithmetic:  return nir_op_ishr;
   case SpvOpShiftLeftLogical:      return nir_op_ishl;
   case SpvOpLogicalOr:             return nir_op_ior;
   case SpvOpLogicalEqual:          return nir_op_ieq;
   case SpvOpLogicalNotEqual:       return nir_op_ine;
   case SpvOpLogicalAnd:            return nir_op_iand;
   case SpvOpLogicalNot:            return nir_op_inot;
   case SpvOpBitwiseOr:             return nir_op_ior;
   case SpvOpBitwiseXor:            return nir_op_ixor;
   case SpvOpBitwiseAnd:            return nir_op_iand;
   case SpvOpSelect:                return nir_op_bcsel;
   case SpvOpIEqual:                return nir_op_ieq;

   case SpvOpBitFieldInsert:        return nir_op_bitfield_insert;
   case SpvOpBitFieldSExtract:      return nir_op_ibitfield_extract;
   case SpvOpBitFieldUExtract:      return nir_op_ubitfield_extract;
   case SpvOpBitReverse:            return nir_op_bitfield_reverse;
   case SpvOpBitCount:              return nir_op_bit_count;

   /* The ordered / unordered operators need special implementation besides
    * the logical operator to use since they also need to check if operands are
    * ordered.
    */
   case SpvOpFOrdEqual:                            return nir_op_feq;
   case SpvOpFUnordEqual:                          return nir_op_feq;
   case SpvOpINotEqual:                            return nir_op_ine;
   case SpvOpFOrdNotEqual:                         return nir_op_fne;
   case SpvOpFUnordNotEqual:                       return nir_op_fne;
   case SpvOpULessThan:                            return nir_op_ult;
   case SpvOpSLessThan:                            return nir_op_ilt;
   case SpvOpFOrdLessThan:                         return nir_op_flt;
   case SpvOpFUnordLessThan:                       return nir_op_flt;
   case SpvOpUGreaterThan:          *swap = true;  return nir_op_ult;
   case SpvOpSGreaterThan:          *swap = true;  return nir_op_ilt;
   case SpvOpFOrdGreaterThan:       *swap = true;  return nir_op_flt;
   case SpvOpFUnordGreaterThan:     *swap = true;  return nir_op_flt;
   case SpvOpULessThanEqual:        *swap = true;  return nir_op_uge;
   case SpvOpSLessThanEqual:        *swap = true;  return nir_op_ige;
   case SpvOpFOrdLessThanEqual:     *swap = true;  return nir_op_fge;
   case SpvOpFUnordLessThanEqual:   *swap = true;  return nir_op_fge;
   case SpvOpUGreaterThanEqual:                    return nir_op_uge;
   case SpvOpSGreaterThanEqual:                    return nir_op_ige;
   case SpvOpFOrdGreaterThanEqual:                 return nir_op_fge;
   case SpvOpFUnordGreaterThanEqual:               return nir_op_fge;

   /* Conversions: */
   case SpvOpQuantizeToF16:         return nir_op_fquantize2f16;
   case SpvOpUConvert:
   case SpvOpConvertFToU:
   case SpvOpConvertFToS:
   case SpvOpConvertSToF:
   case SpvOpConvertUToF:
   case SpvOpSConvert:
   case SpvOpFConvert: {
      nir_alu_type src_type;
      nir_alu_type dst_type;

      switch (opcode) {
      case SpvOpConvertFToS:
         src_type = nir_type_float;
         dst_type = nir_type_int;
         break;
      case SpvOpConvertFToU:
         src_type = nir_type_float;
         dst_type = nir_type_uint;
         break;
      case SpvOpFConvert:
         src_type = dst_type = nir_type_float;
         break;
      case SpvOpConvertSToF:
         src_type = nir_type_int;
         dst_type = nir_type_float;
         break;
      case SpvOpSConvert:
         src_type = dst_type = nir_type_int;
         break;
      case SpvOpConvertUToF:
         src_type = nir_type_uint;
         dst_type = nir_type_float;
         break;
      case SpvOpUConvert:
         src_type = dst_type = nir_type_uint;
         break;
      default:
         unreachable("Invalid opcode");
      }
      src_type |= src_bit_size;
      dst_type |= dst_bit_size;
      return nir_type_conversion_op(src_type, dst_type, nir_rounding_mode_undef);
   }
   /* Derivatives: */
   case SpvOpDPdx:         return nir_op_fddx;
   case SpvOpDPdy:         return nir_op_fddy;
   case SpvOpDPdxFine:     return nir_op_fddx_fine;
   case SpvOpDPdyFine:     return nir_op_fddy_fine;
   case SpvOpDPdxCoarse:   return nir_op_fddx_coarse;
   case SpvOpDPdyCoarse:   return nir_op_fddy_coarse;

   default:
      vtn_fail("No NIR equivalent");
   }
}

static void
handle_no_contraction(struct vtn_builder *b, struct vtn_value *val, int member,
                      const struct vtn_decoration *dec, void *_void)
{
   vtn_assert(dec->scope == VTN_DEC_DECORATION);
   if (dec->decoration != SpvDecorationNoContraction)
      return;

   b->nb.exact = true;
}

static void
handle_rounding_mode(struct vtn_builder *b, struct vtn_value *val, int member,
                     const struct vtn_decoration *dec, void *_out_rounding_mode)
{
   nir_rounding_mode *out_rounding_mode = _out_rounding_mode;
   assert(dec->scope == VTN_DEC_DECORATION);
   if (dec->decoration != SpvDecorationFPRoundingMode)
      return;
   switch (dec->literals[0]) {
   case SpvFPRoundingModeRTE:
      *out_rounding_mode = nir_rounding_mode_rtne;
      break;
   case SpvFPRoundingModeRTZ:
      *out_rounding_mode = nir_rounding_mode_rtz;
      break;
   default:
      unreachable("Not supported rounding mode");
      break;
   }
}

void
vtn_handle_alu(struct vtn_builder *b, SpvOp opcode,
               const uint32_t *w, unsigned count)
{
   struct vtn_value *val = vtn_push_value(b, w[2], vtn_value_type_ssa);
   const struct glsl_type *type =
      vtn_value(b, w[1], vtn_value_type_type)->type->type;

   vtn_foreach_decoration(b, val, handle_no_contraction, NULL);

   /* Collect the various SSA sources */
   const unsigned num_inputs = count - 3;
   struct vtn_ssa_value *vtn_src[4] = { NULL, };
   for (unsigned i = 0; i < num_inputs; i++)
      vtn_src[i] = vtn_ssa_value(b, w[i + 3]);

   if (glsl_type_is_matrix(vtn_src[0]->type) ||
       (num_inputs >= 2 && glsl_type_is_matrix(vtn_src[1]->type))) {
      vtn_handle_matrix_alu(b, opcode, val, vtn_src[0], vtn_src[1]);
      b->nb.exact = false;
      return;
   }

   val->ssa = vtn_create_ssa_value(b, type);
   nir_ssa_def *src[4] = { NULL, };
   for (unsigned i = 0; i < num_inputs; i++) {
      vtn_assert(glsl_type_is_vector_or_scalar(vtn_src[i]->type));
      src[i] = vtn_src[i]->def;
   }

   switch (opcode) {
   case SpvOpAny:
      if (src[0]->num_components == 1) {
         val->ssa->def = nir_imov(&b->nb, src[0]);
      } else {
         nir_op op;
         switch (src[0]->num_components) {
         case 2:  op = nir_op_bany_inequal2; break;
         case 3:  op = nir_op_bany_inequal3; break;
         case 4:  op = nir_op_bany_inequal4; break;
         default: vtn_fail("invalid number of components");
         }
         val->ssa->def = nir_build_alu(&b->nb, op, src[0],
                                       nir_imm_int(&b->nb, NIR_FALSE),
                                       NULL, NULL);
      }
      break;

   case SpvOpAll:
      if (src[0]->num_components == 1) {
         val->ssa->def = nir_imov(&b->nb, src[0]);
      } else {
         nir_op op;
         switch (src[0]->num_components) {
         case 2:  op = nir_op_ball_iequal2;  break;
         case 3:  op = nir_op_ball_iequal3;  break;
         case 4:  op = nir_op_ball_iequal4;  break;
         default: vtn_fail("invalid number of components");
         }
         val->ssa->def = nir_build_alu(&b->nb, op, src[0],
                                       nir_imm_int(&b->nb, NIR_TRUE),
                                       NULL, NULL);
      }
      break;

   case SpvOpOuterProduct: {
      for (unsigned i = 0; i < src[1]->num_components; i++) {
         val->ssa->elems[i]->def =
            nir_fmul(&b->nb, src[0], nir_channel(&b->nb, src[1], i));
      }
      break;
   }

   case SpvOpDot:
      val->ssa->def = nir_fdot(&b->nb, src[0], src[1]);
      break;

   case SpvOpIAddCarry:
      vtn_assert(glsl_type_is_struct(val->ssa->type));
      val->ssa->elems[0]->def = nir_iadd(&b->nb, src[0], src[1]);
      val->ssa->elems[1]->def = nir_uadd_carry(&b->nb, src[0], src[1]);
      break;

   case SpvOpISubBorrow:
      vtn_assert(glsl_type_is_struct(val->ssa->type));
      val->ssa->elems[0]->def = nir_isub(&b->nb, src[0], src[1]);
      val->ssa->elems[1]->def = nir_usub_borrow(&b->nb, src[0], src[1]);
      break;

   case SpvOpUMulExtended:
      vtn_assert(glsl_type_is_struct(val->ssa->type));
      val->ssa->elems[0]->def = nir_imul(&b->nb, src[0], src[1]);
      val->ssa->elems[1]->def = nir_umul_high(&b->nb, src[0], src[1]);
      break;

   case SpvOpSMulExtended:
      vtn_assert(glsl_type_is_struct(val->ssa->type));
      val->ssa->elems[0]->def = nir_imul(&b->nb, src[0], src[1]);
      val->ssa->elems[1]->def = nir_imul_high(&b->nb, src[0], src[1]);
      break;

   case SpvOpFwidth:
      val->ssa->def = nir_fadd(&b->nb,
                               nir_fabs(&b->nb, nir_fddx(&b->nb, src[0])),
                               nir_fabs(&b->nb, nir_fddy(&b->nb, src[0])));
      break;
   case SpvOpFwidthFine:
      val->ssa->def = nir_fadd(&b->nb,
                               nir_fabs(&b->nb, nir_fddx_fine(&b->nb, src[0])),
                               nir_fabs(&b->nb, nir_fddy_fine(&b->nb, src[0])));
      break;
   case SpvOpFwidthCoarse:
      val->ssa->def = nir_fadd(&b->nb,
                               nir_fabs(&b->nb, nir_fddx_coarse(&b->nb, src[0])),
                               nir_fabs(&b->nb, nir_fddy_coarse(&b->nb, src[0])));
      break;

   case SpvOpVectorTimesScalar:
      /* The builder will take care of splatting for us. */
      val->ssa->def = nir_fmul(&b->nb, src[0], src[1]);
      break;

   case SpvOpIsNan:
      val->ssa->def = nir_fne(&b->nb, src[0], src[0]);
      break;

   case SpvOpIsInf:
      val->ssa->def = nir_ieq(&b->nb, nir_fabs(&b->nb, src[0]),
                                      nir_imm_float(&b->nb, INFINITY));
      break;

   case SpvOpFUnordEqual:
   case SpvOpFUnordNotEqual:
   case SpvOpFUnordLessThan:
   case SpvOpFUnordGreaterThan:
   case SpvOpFUnordLessThanEqual:
   case SpvOpFUnordGreaterThanEqual: {
      bool swap;
      unsigned src_bit_size = glsl_get_bit_size(vtn_src[0]->type);
      unsigned dst_bit_size = glsl_get_bit_size(type);
      nir_op op = vtn_nir_alu_op_for_spirv_opcode(b, opcode, &swap,
                                                  src_bit_size, dst_bit_size);

      if (swap) {
         nir_ssa_def *tmp = src[0];
         src[0] = src[1];
         src[1] = tmp;
      }

      val->ssa->def =
         nir_ior(&b->nb,
                 nir_build_alu(&b->nb, op, src[0], src[1], NULL, NULL),
                 nir_ior(&b->nb,
                         nir_fne(&b->nb, src[0], src[0]),
                         nir_fne(&b->nb, src[1], src[1])));
      break;
   }

   case SpvOpFOrdEqual:
   case SpvOpFOrdNotEqual:
   case SpvOpFOrdLessThan:
   case SpvOpFOrdGreaterThan:
   case SpvOpFOrdLessThanEqual:
   case SpvOpFOrdGreaterThanEqual: {
      bool swap;
      unsigned src_bit_size = glsl_get_bit_size(vtn_src[0]->type);
      unsigned dst_bit_size = glsl_get_bit_size(type);
      nir_op op = vtn_nir_alu_op_for_spirv_opcode(b, opcode, &swap,
                                                  src_bit_size, dst_bit_size);

      if (swap) {
         nir_ssa_def *tmp = src[0];
         src[0] = src[1];
         src[1] = tmp;
      }

      val->ssa->def =
         nir_iand(&b->nb,
                  nir_build_alu(&b->nb, op, src[0], src[1], NULL, NULL),
                  nir_iand(&b->nb,
                          nir_feq(&b->nb, src[0], src[0]),
                          nir_feq(&b->nb, src[1], src[1])));
      break;
   }

   case SpvOpBitcast:
      vtn_handle_bitcast(b, val->ssa, src[0]);
      break;

   case SpvOpFConvert: {
      nir_alu_type src_alu_type = nir_get_nir_type_for_glsl_type(vtn_src[0]->type);
      nir_alu_type dst_alu_type = nir_get_nir_type_for_glsl_type(type);
      nir_rounding_mode rounding_mode = nir_rounding_mode_undef;

      vtn_foreach_decoration(b, val, handle_rounding_mode, &rounding_mode);
      nir_op op = nir_type_conversion_op(src_alu_type, dst_alu_type, rounding_mode);

      val->ssa->def = nir_build_alu(&b->nb, op, src[0], src[1], NULL, NULL);
      break;
   }

   default: {
      bool swap;
      unsigned src_bit_size = glsl_get_bit_size(vtn_src[0]->type);
      unsigned dst_bit_size = glsl_get_bit_size(type);
      nir_op op = vtn_nir_alu_op_for_spirv_opcode(b, opcode, &swap,
                                                  src_bit_size, dst_bit_size);

      if (swap) {
         nir_ssa_def *tmp = src[0];
         src[0] = src[1];
         src[1] = tmp;
      }

      val->ssa->def = nir_build_alu(&b->nb, op, src[0], src[1], src[2], src[3]);
      break;
   } /* default */
   }

   b->nb.exact = false;
}
