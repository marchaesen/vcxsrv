/*
 * Copyright 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"

bool
nir_lower_view_index_to_device_index(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_variable_with_modes(var, shader, nir_var_system_value) {
      if (var->data.location == SYSTEM_VALUE_VIEW_INDEX) {
         var->data.location = SYSTEM_VALUE_DEVICE_INDEX;
         progress = true;
         /* Can there be more than one of these or should we break here? */
      }
   }

   nir_shader_preserve_all_metadata(shader);

   return progress;
}
