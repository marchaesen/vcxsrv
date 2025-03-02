/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2023 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"
#include "radv_nir.h"

static bool
pass(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   bool *lower_view_index_to_zero = data;
   nir_def *def = NULL;
   b->cursor = nir_before_instr(&intrin->instr);

   switch (intrin->intrinsic) {
   case nir_intrinsic_is_sparse_texels_resident:
      def = nir_ieq_imm(b, intrin->src[0].ssa, 0);
      break;
   case nir_intrinsic_sparse_residency_code_and:
      def = nir_ior(b, intrin->src[0].ssa, intrin->src[1].ssa);
      break;
   case nir_intrinsic_load_view_index:
      if (!(*lower_view_index_to_zero))
         return false;

      def = nir_imm_zero(b, 1, 32);
      break;
   default:
      return false;
   }

   nir_def_replace(&intrin->def, def);
   return true;
}

bool
radv_nir_lower_intrinsics_early(nir_shader *nir, bool lower_view_index_to_zero)
{
   return nir_shader_intrinsics_pass(nir, pass, nir_metadata_control_flow,
                                     &lower_view_index_to_zero);
}
