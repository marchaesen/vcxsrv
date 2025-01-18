/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"

/**
 * If a clip/cull distance is constant >= 0,
 * we know that it will never cause clipping/culling.
 * Remove the sysval_output in that case.
 *
 * Assumes that nir_lower_io_to_temporaries was run,
 * and works best with scalar store_outputs.
 */

static bool
opt_clip_cull(nir_builder *b, nir_intrinsic_instr *intr, void *unused)
{
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   const nir_io_semantics io_sem = nir_intrinsic_io_semantics(intr);
   const unsigned location = io_sem.location;

   if (io_sem.no_sysval_output)
      return false;

   if (location != VARYING_SLOT_CLIP_DIST0 && location != VARYING_SLOT_CLIP_DIST1)
      return false;

   nir_def *val = intr->src[0].ssa;
   for (unsigned i = 0; i < val->num_components; i++) {
      nir_scalar s = nir_scalar_resolved(val, i);
      if (!nir_scalar_is_const(s))
         return false;
      float distance = nir_scalar_as_float(s);

      /* NaN gets clipped, and INF after interpolation is NaN. */
      if (isnan(distance) || distance < 0.0 || distance == INFINITY)
         return false;
   }

   nir_remove_sysval_output(intr, MESA_SHADER_FRAGMENT);
   return true;
}

bool
nir_opt_clip_cull_const(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, opt_clip_cull, nir_metadata_control_flow, NULL);
}
