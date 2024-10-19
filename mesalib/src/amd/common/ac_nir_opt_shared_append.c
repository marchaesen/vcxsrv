/*
 * Copyright 2024 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "nir_builder.h"

static bool
opt_shared_append(nir_builder *b,
                  nir_intrinsic_instr *intrin,
                  void *unused)
{
   if (intrin->intrinsic != nir_intrinsic_shared_atomic)
      return false;
   if (nir_intrinsic_atomic_op(intrin) != nir_atomic_op_iadd)
      return false;
   if (intrin->def.bit_size != 32)
      return false;

   if (!nir_src_is_const(intrin->src[0]) || !nir_src_is_const(intrin->src[1]))
      return false;

   uint32_t addr = nir_src_as_uint(intrin->src[0]) + nir_intrinsic_base(intrin);
   int32_t data = nir_src_as_int(intrin->src[1]);

   if (data != 1 && data != -1)
      return false;

   if (addr >= 65536 || addr % 4)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);
   nir_def *res;
   if (data == 1)
      res = nir_shared_append_amd(b, .base = addr);
   else
      res = nir_shared_consume_amd(b, .base = addr);

   if (nir_def_is_unused(&intrin->def)) {
      nir_instr_remove(&intrin->instr);
      return true;
   }

   res = nir_iadd(b, res, nir_exclusive_scan(b, intrin->src[1].ssa, .reduction_op = nir_op_iadd));
   nir_def_replace(&intrin->def, res);
   return true;
}


bool
ac_nir_opt_shared_append(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, opt_shared_append, nir_metadata_control_flow, NULL);
}
