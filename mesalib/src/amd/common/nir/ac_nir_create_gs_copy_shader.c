/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "ac_nir_helpers.h"

#include "nir_builder.h"
#include "nir_xfb_info.h"

nir_shader *
ac_nir_create_gs_copy_shader(const nir_shader *gs_nir,
                             enum amd_gfx_level gfx_level,
                             uint32_t clip_cull_mask,
                             const uint8_t *param_offsets,
                             bool has_param_exports,
                             bool disable_streamout,
                             bool kill_pointsize,
                             bool kill_layer,
                             bool force_vrs,
                             ac_nir_gs_output_info *output_info)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_VERTEX, gs_nir->options, "gs_copy");

   nir_foreach_shader_out_variable(var, gs_nir)
      nir_shader_add_variable(b.shader, nir_variable_clone(var, b.shader));

   b.shader->info.outputs_written = gs_nir->info.outputs_written;
   b.shader->info.outputs_written_16bit = gs_nir->info.outputs_written_16bit;

   nir_def *gsvs_ring = nir_load_ring_gsvs_amd(&b);

   nir_xfb_info *info = ac_nir_get_sorted_xfb_info(gs_nir);
   nir_def *stream_id = NULL;
   if (!disable_streamout && info)
      stream_id = nir_ubfe_imm(&b, nir_load_streamout_config_amd(&b), 24, 2);

   nir_def *vtx_offset = nir_imul_imm(&b, nir_load_vertex_id_zero_base(&b), 4);
   nir_def *zero = nir_imm_zero(&b, 1, 32);

   for (unsigned stream = 0; stream < 4; stream++) {
      if (stream > 0 && (!stream_id || !(info->streams_written & BITFIELD_BIT(stream))))
         continue;

      if (stream_id)
         nir_push_if(&b, nir_ieq_imm(&b, stream_id, stream));

      uint32_t offset = 0;
      ac_nir_prerast_out out = {0};
      if (output_info->types_16bit_lo)
         memcpy(&out.types_16bit_lo, output_info->types_16bit_lo, sizeof(out.types_16bit_lo));
      if (output_info->types_16bit_hi)
         memcpy(&out.types_16bit_hi, output_info->types_16bit_hi, sizeof(out.types_16bit_hi));

      u_foreach_bit64 (i, gs_nir->info.outputs_written) {
         const uint8_t usage_mask = output_info->varying_mask[i] | output_info->sysval_mask[i];
         out.infos[i].components_mask = usage_mask;
         out.infos[i].as_varying_mask = output_info->varying_mask[i];
         out.infos[i].as_sysval_mask = output_info->sysval_mask[i];

         u_foreach_bit (j, usage_mask) {
            if (((output_info->streams[i] >> (j * 2)) & 0x3) != stream)
               continue;

            out.outputs[i][j] =
               nir_load_buffer_amd(&b, 1, 32, gsvs_ring, vtx_offset, zero, zero,
                                   .base = offset,
                                   .access = ACCESS_COHERENT | ACCESS_NON_TEMPORAL);
            offset += gs_nir->info.gs.vertices_out * 16 * 4;
         }
      }

      u_foreach_bit (i, gs_nir->info.outputs_written_16bit) {
         out.infos_16bit_lo[i].components_mask = output_info->varying_mask_16bit_lo[i];
         out.infos_16bit_lo[i].as_varying_mask = output_info->varying_mask_16bit_lo[i];
         out.infos_16bit_hi[i].components_mask = output_info->varying_mask_16bit_hi[i];
         out.infos_16bit_hi[i].as_varying_mask = output_info->varying_mask_16bit_hi[i];

         for (unsigned j = 0; j < 4; j++) {
            out.infos[i].as_varying_mask = output_info->varying_mask[i];
            out.infos[i].as_sysval_mask = output_info->sysval_mask[i];

            bool has_lo_16bit = (output_info->varying_mask_16bit_lo[i] & (1 << j)) &&
               ((output_info->streams_16bit_lo[i] >> (j * 2)) & 0x3) == stream;
            bool has_hi_16bit = (output_info->varying_mask_16bit_hi[i] & (1 << j)) &&
               ((output_info->streams_16bit_hi[i] >> (j * 2)) & 0x3) == stream;
            if (!has_lo_16bit && !has_hi_16bit)
               continue;

            nir_def *data =
               nir_load_buffer_amd(&b, 1, 32, gsvs_ring, vtx_offset, zero, zero,
                                   .base = offset,
                                   .access = ACCESS_COHERENT | ACCESS_NON_TEMPORAL);

            if (has_lo_16bit)
               out.outputs_16bit_lo[i][j] = nir_unpack_32_2x16_split_x(&b, data);

            if (has_hi_16bit)
               out.outputs_16bit_hi[i][j] = nir_unpack_32_2x16_split_y(&b, data);

            offset += gs_nir->info.gs.vertices_out * 16 * 4;
         }
      }

      if (stream_id)
         ac_nir_emit_legacy_streamout(&b, stream, info, &out);

      /* This should be after streamout and before exports. */
      ac_nir_clamp_vertex_color_outputs(&b, &out);

      if (stream == 0) {
         uint64_t export_outputs = b.shader->info.outputs_written | VARYING_BIT_POS;
         if (kill_pointsize)
            export_outputs &= ~VARYING_BIT_PSIZ;
         if (kill_layer)
            export_outputs &= ~VARYING_BIT_LAYER;

         ac_nir_export_position(&b, gfx_level, clip_cull_mask, !has_param_exports,
                                force_vrs, true, export_outputs, &out, NULL);

         if (has_param_exports) {
            ac_nir_export_parameters(&b, param_offsets,
                                     b.shader->info.outputs_written,
                                     b.shader->info.outputs_written_16bit,
                                     &out);
         }
      }

      if (stream_id)
         nir_push_else(&b, NULL);
   }

   b.shader->info.clip_distance_array_size = gs_nir->info.clip_distance_array_size;
   b.shader->info.cull_distance_array_size = gs_nir->info.cull_distance_array_size;

   return b.shader;
}
