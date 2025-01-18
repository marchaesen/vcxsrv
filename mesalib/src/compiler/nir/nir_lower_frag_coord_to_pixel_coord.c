/*
 * Copyright 2023 Valve Corpoation
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_builder_opcodes.h"

static bool
lower(nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_frag_coord)
      return false;

   /* Note: frag_coord should already have pixel-center lowering applied with
    * nir_lower_wpos_center for VK, or PIPE_CAP_PIXEL_CENTER_INTEGER for GL.
    */
   b->cursor = nir_before_instr(&intr->instr);
   nir_def *xy = nir_u2f32(b, nir_load_pixel_coord(b));
   nir_def *vec = nir_vec4(b, nir_channel(b, xy, 0), nir_channel(b, xy, 1),
                           nir_load_frag_coord_zw(b, .component = 2),
                           nir_load_frag_coord_zw(b, .component = 3));
   nir_def_rewrite_uses(&intr->def, vec);
   return true;
}

bool
nir_lower_frag_coord_to_pixel_coord(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, lower,
                                     nir_metadata_control_flow,
                                     NULL);
}
