/*
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "shaders/geometry.h"
#include "agx_nir_lower_gs.h"
#include "libagx_shaders.h"
#include "nir.h"

/*
 * This file implements basic input assembly in software. It runs on software
 * vertex shaders, as part of geometry/tessellation lowering. It does not apply
 * the topology, which happens in the geometry shader.
 */
static nir_def *
load_vertex_id(nir_builder *b, unsigned index_size_B)
{
   nir_def *id = nir_channel(b, nir_load_global_invocation_id(b, 32), 0);

   /* If drawing with an index buffer, pull the vertex ID. Otherwise, the
    * vertex ID is just the index as-is.
    */
   if (index_size_B) {
      nir_def *ia = nir_load_input_assembly_buffer_agx(b);
      id = libagx_load_index_buffer(b, ia, id, nir_imm_int(b, index_size_B));
   }

   /* Add the "start", either an index bias or a base vertex. This must happen
    * after indexing for proper index bias behaviour.
    */
   return nir_iadd(b, id, nir_load_first_vertex(b));
}

static bool
lower(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   unsigned *index_size_B = data;
   b->cursor = nir_before_instr(&intr->instr);

   if (intr->intrinsic == nir_intrinsic_load_vertex_id) {
      nir_def_replace(&intr->def, load_vertex_id(b, *index_size_B));
      return true;
   } else if (intr->intrinsic == nir_intrinsic_load_instance_id) {
      nir_def_replace(&intr->def,
                      nir_channel(b, nir_load_global_invocation_id(b, 32), 1));
      return true;
   }

   return false;
}

bool
agx_nir_lower_sw_vs(nir_shader *s, unsigned index_size_B)
{
   return nir_shader_intrinsics_pass(s, lower, nir_metadata_control_flow,
                                     &index_size_B);
}
