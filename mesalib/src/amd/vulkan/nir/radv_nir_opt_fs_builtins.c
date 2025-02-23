/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "radv_nir.h"
#include "radv_pipeline_graphics.h"

static bool
pass(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   const struct radv_graphics_state_key *gfx_state = data;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *replacement = NULL;
   if (intr->intrinsic == nir_intrinsic_load_front_face) {
      if (gfx_state->rs.cull_mode == VK_CULL_MODE_FRONT_BIT) {
         replacement = nir_imm_false(b);
      } else if (gfx_state->rs.cull_mode == VK_CULL_MODE_BACK_BIT) {
         replacement = nir_imm_true(b);
      }
   } else if (intr->intrinsic == nir_intrinsic_load_sample_id) {
      if (!gfx_state->dynamic_rasterization_samples && gfx_state->ms.rasterization_samples == 0) {
         replacement = nir_imm_intN_t(b, 0, intr->def.bit_size);
      }
   }

   if (!replacement)
      return false;

   nir_def_replace(&intr->def, replacement);
   return true;
}

bool
radv_nir_opt_fs_builtins(nir_shader *shader, const struct radv_graphics_state_key *gfx_state)
{
   return nir_shader_intrinsics_pass(shader, pass, nir_metadata_control_flow, (void *)gfx_state);
}