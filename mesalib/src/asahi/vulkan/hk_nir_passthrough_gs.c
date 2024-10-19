/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "util/bitscan.h"
#include "hk_shader.h"
#include "nir.h"
#include "nir_builder.h"
#include "nir_xfb_info.h"
#include "shader_enums.h"

void
hk_nir_passthrough_gs(nir_builder *b, const void *key_)
{
   nir_shader *s = b->shader;
   const struct hk_passthrough_gs_key *key = key_;
   assert(key->prim == u_decomposed_prim(key->prim));
   assert(key->prim != MESA_PRIM_PATCHES && "tessellation consumes patches");

   enum mesa_prim out;
   if (key->prim == MESA_PRIM_POINTS)
      out = MESA_PRIM_POINTS;
   else if (u_reduced_prim(key->prim) == MESA_PRIM_LINES)
      out = MESA_PRIM_LINE_STRIP;
   else
      out = MESA_PRIM_TRIANGLE_STRIP;

#if 0
   assert((key->outputs &
           (VARYING_BIT_BOUNDING_BOX0 | VARYING_BIT_BOUNDING_BOX1)) == 0 &&
          "cull distance lowering not run yet");
#endif
   /* XXX: need rework of preprocess_nir */
   uint64_t outputs =
      key->outputs & ~(VARYING_BIT_BOUNDING_BOX0 | VARYING_BIT_BOUNDING_BOX1);

   s->info.outputs_written = s->info.inputs_read = outputs;
   s->info.clip_distance_array_size = key->clip_distance_array_size;
   s->info.cull_distance_array_size = key->cull_distance_array_size;
   s->info.stage = MESA_SHADER_GEOMETRY;
   s->info.gs.input_primitive = key->prim;
   s->info.gs.output_primitive = out;
   s->info.gs.vertices_in = mesa_vertices_per_prim(key->prim);
   s->info.gs.vertices_out = mesa_vertices_per_prim(out);
   s->info.gs.invocations = 1;
   s->info.gs.active_stream_mask = 1;

   if (key->xfb_info.output_count) {
      size_t size = nir_xfb_info_size(key->xfb_info.output_count);
      s->xfb_info = ralloc_memdup(s, &key->xfb_info, size);
      s->info.has_transform_feedback_varyings = true;
      memcpy(s->info.xfb_stride, key->xfb_stride, sizeof(key->xfb_stride));
   }

   unsigned int start_vert = key->prim == MESA_PRIM_LINES_ADJACENCY ? 1 : 0;
   unsigned int step = key->prim == MESA_PRIM_TRIANGLES_ADJACENCY ? 2 : 1;

   nir_def *zero = nir_imm_int(b, 0);
   nir_def *one = nir_imm_int(b, 1);

   for (unsigned i = 0; i < s->info.gs.vertices_out; ++i) {
      nir_def *vertex = nir_imm_int(b, start_vert + (i * step));

      /* Copy inputs to outputs. */
      u_foreach_bit64(loc, outputs) {
         unsigned adjusted_loc = loc;
         nir_def *offset = zero;
         unsigned num_slots = 1;

         bool scalar = loc == VARYING_SLOT_LAYER ||
                       loc == VARYING_SLOT_VIEW_INDEX ||
                       loc == VARYING_SLOT_VIEWPORT || loc == VARYING_SLOT_PSIZ;
         unsigned comps = scalar ? 1 : 4;

         /* We use combined, compact clip/cull */
         if (loc == VARYING_SLOT_CLIP_DIST1 || loc == VARYING_SLOT_CULL_DIST1) {
            adjusted_loc--;
            offset = one;
         }

         if (adjusted_loc == VARYING_SLOT_CLIP_DIST0 ||
             adjusted_loc == VARYING_SLOT_CULL_DIST0) {
            num_slots =
               key->cull_distance_array_size + key->clip_distance_array_size;

            if (loc > adjusted_loc)
               comps = num_slots - 4;
            else
               comps = MIN2(num_slots, 4);
         }

         nir_io_semantics sem = {
            .location = adjusted_loc,
            .num_slots = num_slots,
         };

         nir_def *val = nir_load_per_vertex_input(b, comps, 32, vertex, offset,
                                                  .io_semantics = sem);

         for (unsigned c = 0; c < comps; ++c) {
            nir_store_output(b, nir_channel(b, val, c), offset,
                             .io_semantics = sem, .src_type = nir_type_uint32,
                             .component = c);
         }
      }

      nir_emit_vertex(b, 0);
   }
}
