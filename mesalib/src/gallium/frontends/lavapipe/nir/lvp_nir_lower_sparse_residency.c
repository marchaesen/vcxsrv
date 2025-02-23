/*
 * Copyright © 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "lvp_nir.h"

#include "nir.h"
#include "nir_builder.h"

static bool
pass(nir_builder *b, nir_intrinsic_instr *instr, void *data)
{
   b->cursor = nir_before_instr(&instr->instr);

   if (instr->intrinsic == nir_intrinsic_sparse_residency_code_and) {
      nir_def_rewrite_uses(&instr->def, nir_iand(b, instr->src[0].ssa, instr->src[1].ssa));
      return true;
   } else if (instr->intrinsic == nir_intrinsic_is_sparse_texels_resident) {
      nir_def_rewrite_uses(&instr->def, nir_ine_imm(b, instr->src[0].ssa, 0));
      return true;
   }

   return false;
}

bool
lvp_nir_lower_sparse_residency(struct nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, pass, nir_metadata_block_index | nir_metadata_dominance, NULL);
}
