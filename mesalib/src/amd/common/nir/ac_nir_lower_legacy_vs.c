/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "ac_nir.h"
#include "ac_nir_helpers.h"

#include "nir_builder.h"

static void
gather_outputs(nir_builder *b, nir_function_impl *impl, ac_nir_prerast_out *out)
{
   /* Assume:
    * - the shader used nir_lower_io_to_temporaries
    * - 64-bit outputs are lowered
    * - no indirect indexing is present
    */
   nir_foreach_block (block, impl) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
         if (intrin->intrinsic != nir_intrinsic_store_output)
            continue;

         ac_nir_gather_prerast_store_output_info(b, intrin, out);
         nir_instr_remove(instr);
      }
   }
}

void
ac_nir_lower_legacy_vs(nir_shader *nir,
                       enum amd_gfx_level gfx_level,
                       uint32_t clip_cull_mask,
                       const uint8_t *param_offsets,
                       bool has_param_exports,
                       bool export_primitive_id,
                       bool disable_streamout,
                       bool kill_pointsize,
                       bool kill_layer,
                       bool force_vrs)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(nir);

   nir_builder b = nir_builder_at(nir_after_impl(impl));

   ac_nir_prerast_out out = {0};
   gather_outputs(&b, impl, &out);
   b.cursor = nir_after_impl(impl);

   if (export_primitive_id) {
      /* When the primitive ID is read by FS, we must ensure that it's exported by the previous
       * vertex stage because it's implicit for VS or TES (but required by the Vulkan spec for GS
       * or MS).
       */
      out.outputs[VARYING_SLOT_PRIMITIVE_ID][0] = nir_load_primitive_id(&b);
      out.infos[VARYING_SLOT_PRIMITIVE_ID].as_varying_mask = 0x1;

      /* Update outputs_written to reflect that the pass added a new output. */
      nir->info.outputs_written |= BITFIELD64_BIT(VARYING_SLOT_PRIMITIVE_ID);
   }

   if (!disable_streamout && nir->xfb_info)
      ac_nir_emit_legacy_streamout(&b, 0, ac_nir_get_sorted_xfb_info(nir), &out);

   /* This should be after streamout and before exports. */
   ac_nir_clamp_vertex_color_outputs(&b, &out);

   /* This should be after streamout and before exports. */
   ac_nir_clamp_vertex_color_outputs(&b, &out);

   uint64_t export_outputs = nir->info.outputs_written | VARYING_BIT_POS;
   if (kill_pointsize)
      export_outputs &= ~VARYING_BIT_PSIZ;
   if (kill_layer)
      export_outputs &= ~VARYING_BIT_LAYER;

   ac_nir_export_position(&b, gfx_level, clip_cull_mask, !has_param_exports,
                          force_vrs, true, export_outputs, &out, NULL);

   if (has_param_exports) {
      ac_nir_export_parameters(&b, param_offsets,
                               nir->info.outputs_written,
                               nir->info.outputs_written_16bit,
                               &out);
   }

   nir_metadata_preserve(impl, nir_metadata_none);
}
