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

/**
 * We use layered rendering to implement multiview, which means we need to map
 * view_index to gl_Layer. The code generates a load from the layer_id sysval,
 * but since we don't have a way to get at this information from the fragment
 * shader, we also need to lower this to the gl_Layer varying.  This pass
 * lowers both to a varying load from the LAYER slot, before lowering io, so
 * that nir_assign_var_locations() will give the LAYER varying the correct
 * driver_location.
 */
bool
radv_nir_lower_view_index(nir_shader *nir)
{
   bool progress = false;
   nir_function_impl *entry = nir_shader_get_entrypoint(nir);
   nir_builder b = nir_builder_create(entry);

   nir_foreach_block (block, entry) {
      nir_foreach_instr_safe (instr, block) {
         if (instr->type != nir_instr_type_intrinsic)
            continue;

         nir_intrinsic_instr *load = nir_instr_as_intrinsic(instr);
         if (load->intrinsic != nir_intrinsic_load_view_index)
            continue;

         b.cursor = nir_before_instr(instr);
         nir_def *def = nir_load_layer_id(&b);
         nir_def_rewrite_uses(&load->def, def);

         nir_instr_remove(instr);
         progress = true;
      }
   }

   if (progress)
      nir_metadata_preserve(entry, nir_metadata_control_flow);
   else
      nir_metadata_preserve(entry, nir_metadata_all);

   return progress;
}
