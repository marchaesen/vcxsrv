/*
 * Copyright © 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "lvp_nir.h"
#include "lvp_private.h"

#include "vk_pipeline.h"

#include "nir.h"
#include "nir_builder.h"

struct state {
   struct lvp_device *device;
   struct vk_pipeline_robustness_state *robustness;
};

static bool
pass(nir_builder *b, nir_intrinsic_instr *instr, void *data)
{
   struct state *state = data;

   if (state->device->vk.enabled_features.nullDescriptor)
      return false;

   switch (instr->intrinsic) {
   case nir_intrinsic_load_ubo:
      if (state->robustness->uniform_buffers == VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED)
         nir_intrinsic_set_access(instr, nir_intrinsic_access(instr) | ACCESS_IN_BOUNDS);
      break;

   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_ssbo_atomic_swap:
   case nir_intrinsic_get_ssbo_size:
   case nir_intrinsic_store_ssbo:
      if (state->robustness->storage_buffers == VK_PIPELINE_ROBUSTNESS_BUFFER_BEHAVIOR_DISABLED)
         nir_intrinsic_set_access(instr, nir_intrinsic_access(instr) | ACCESS_IN_BOUNDS);
      break;

   default:
      break;
   }

   return false;
}

bool
lvp_nir_opt_robustness(struct nir_shader *shader, struct lvp_device *device,
                       struct vk_pipeline_robustness_state *robustness)
{
   struct state state = {
      .device = device,
      .robustness = robustness,
   };

   return nir_shader_intrinsics_pass(shader, pass, nir_metadata_all, &state);
}
