/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "agx_nir.h"

/*
 * AGX generally flushes FP32 denorms. However, the min/max instructions do not
 * as they are implemented with cmpsel. We need to flush the results of fp32
 * min/max for correctness. Doing so naively will generate redundant flushes, so
 * this pass tries to be clever and elide flushes when possible.
 *
 * This pass is still pretty simple, it doesn't see through phis or bcsels yet.
 */
static bool
could_be_denorm(nir_scalar s)
{
   /* Constants can be denorms only if they are denorms. */
   if (nir_scalar_is_const(s)) {
      return fpclassify(nir_scalar_as_float(s)) == FP_SUBNORMAL;
   }

   /* Floating-point instructions flush denormals, so ALU results can only be
    * denormal if they are not from a float instruction. Crucially fmin/fmax
    * flushes in NIR, so this pass handles chains of fmin/fmax properly.
    */
   if (nir_scalar_is_alu(s)) {
      nir_op op = nir_scalar_alu_op(s);
      nir_alu_type T = nir_op_infos[op].output_type;

      return nir_alu_type_get_base_type(T) != nir_type_float &&
             op != nir_op_fmin_agx && op != nir_op_fmax_agx;
   }

   /* Otherwise, assume it could be denormal (say, loading from a buffer). */
   return true;
}

static bool
lower(nir_builder *b, nir_alu_instr *alu, void *data)
{
   if ((alu->op != nir_op_fmin && alu->op != nir_op_fmax) ||
       (alu->def.bit_size != 32))
      return false;

   /* Lower the op, we'll fix up the denorms right after. */
   if (alu->op == nir_op_fmax)
      alu->op = nir_op_fmax_agx;
   else
      alu->op = nir_op_fmin_agx;

   /* We need to canonicalize the result if the output could be a denorm. That
    * occurs only when one of the sources could be a denorm. Check each source.
    * Swizzles don't affect denormalness so we can grab the def directly.
    */
   nir_scalar scalar = nir_get_scalar(&alu->def, 0);
   nir_scalar src0 = nir_scalar_chase_alu_src(scalar, 0);
   nir_scalar src1 = nir_scalar_chase_alu_src(scalar, 1);

   if (could_be_denorm(src0) || could_be_denorm(src1)) {
      b->cursor = nir_after_instr(&alu->instr);
      nir_def *canonicalized = nir_fadd_imm(b, &alu->def, -0.0);
      nir_def_rewrite_uses_after(&alu->def, canonicalized,
                                 canonicalized->parent_instr);
   }

   return true;
}

bool
agx_nir_lower_fminmax(nir_shader *s)
{
   return nir_shader_alu_pass(s, lower, nir_metadata_control_flow, NULL);
}
