/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "ac_nir_helpers.h"

#include "nir_builder.h"

static bool
needs_rounding_mode_16_64(nir_instr *instr)
{
   if (instr->type != nir_instr_type_alu)
      return false;
   nir_alu_instr *alu = nir_instr_as_alu(instr);
   if (alu->op == nir_op_fquantize2f16)
      return true;
   if (alu->def.bit_size != 16 && alu->def.bit_size != 64)
      return false;
   if (nir_alu_type_get_base_type(nir_op_infos[alu->op].output_type) != nir_type_float)
      return false;

   switch (alu->op) {
   case nir_op_f2f64:
   case nir_op_b2f64:
   case nir_op_f2f16_rtz:
   case nir_op_b2f16:
   case nir_op_fsat:
   case nir_op_fabs:
   case nir_op_fneg:
   case nir_op_fsign:
   case nir_op_ftrunc:
   case nir_op_fceil:
   case nir_op_ffloor:
   case nir_op_ffract:
   case nir_op_fround_even:
   case nir_op_fmin:
   case nir_op_fmax:
      return false;
   default:
      return true;
   }
}

static bool
can_use_fmamix(nir_scalar s, enum amd_gfx_level gfx_level)
{
   s = nir_scalar_chase_movs(s);
   if (!list_is_singular(&s.def->uses))
      return false;

   if (nir_scalar_is_intrinsic(s) &&
       nir_scalar_intrinsic_op(s) == nir_intrinsic_load_interpolated_input)
      return gfx_level >= GFX11;

   if (!nir_scalar_is_alu(s))
      return false;

   switch (nir_scalar_alu_op(s)) {
   case nir_op_fmul:
   case nir_op_ffma:
   case nir_op_fadd:
   case nir_op_fsub:
      return true;
   case nir_op_fsat:
      return can_use_fmamix(nir_scalar_chase_alu_src(s, 0), gfx_level);
   default:
      return false;
   }
}

static bool
split_pack_half(nir_builder *b, nir_instr *instr, void *param)
{
   enum amd_gfx_level gfx_level = *(enum amd_gfx_level *)param;

   if (instr->type != nir_instr_type_alu)
      return false;
   nir_alu_instr *alu = nir_instr_as_alu(instr);
   if (alu->op != nir_op_pack_half_2x16_rtz_split && alu->op != nir_op_pack_half_2x16_split)
      return false;

   nir_scalar s = nir_get_scalar(&alu->def, 0);

   if (!can_use_fmamix(nir_scalar_chase_alu_src(s, 0), gfx_level) ||
       !can_use_fmamix(nir_scalar_chase_alu_src(s, 1), gfx_level))
      return false;

   b->cursor = nir_before_instr(instr);

   /* Split pack_half into two f2f16 to create v_fma_mix{lo,hi}_f16
    * in the backend.
    */
   nir_def *lo = nir_f2f16(b, nir_ssa_for_alu_src(b, alu, 0));
   nir_def *hi = nir_f2f16(b, nir_ssa_for_alu_src(b, alu, 1));
   nir_def_replace(&alu->def, nir_pack_32_2x16_split(b, lo, hi));
   return true;
}

bool
ac_nir_opt_pack_half(nir_shader *shader, enum amd_gfx_level gfx_level)
{
   if (gfx_level < GFX10)
      return false;

   unsigned exec_mode = shader->info.float_controls_execution_mode;
   bool set_mode = false;
   if (!nir_is_rounding_mode_rtz(exec_mode, 16)) {
      nir_foreach_function_impl(impl, shader) {
         nir_foreach_block(block, impl) {
            nir_foreach_instr(instr, block) {
               if (needs_rounding_mode_16_64(instr))
                  return false;
            }
         }
      }
      set_mode = true;
   }

   bool progress = nir_shader_instructions_pass(shader, split_pack_half,
                                                nir_metadata_control_flow,
                                                &gfx_level);

   if (set_mode && progress) {
      exec_mode &= ~(FLOAT_CONTROLS_ROUNDING_MODE_RTE_FP16 | FLOAT_CONTROLS_ROUNDING_MODE_RTE_FP64);
      exec_mode |= FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP16 | FLOAT_CONTROLS_ROUNDING_MODE_RTZ_FP64;
      shader->info.float_controls_execution_mode = exec_mode;
   }
   return progress;
}
