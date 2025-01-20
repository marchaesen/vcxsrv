/*
 * Copyright © 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "pan_ir.h"

/* Lowers nir_load_frag_coord_zw to nir_load_frag_coord_zw_pan. */

static bool
lower_frag_coord_zw(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   if (intrin->intrinsic != nir_intrinsic_load_frag_coord_zw)
      return false;

   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *bary = nir_load_barycentric_pixel(b, 32,
      .interp_mode = INTERP_MODE_NOPERSPECTIVE
   );
   unsigned component = nir_intrinsic_component(intrin);
   nir_def *new = nir_load_frag_coord_zw_pan(b, bary, .component = component);
   nir_def_replace(&intrin->def, new);

   return true;
}

bool
pan_nir_lower_frag_coord_zw(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, lower_frag_coord_zw,
                                     nir_metadata_control_flow, NULL);
}
