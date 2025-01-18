/*
 * Copyright © 2023 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "util/u_math.h"
#include "ir3_compiler.h"
#include "ir3_nir.h"

static nir_def *
load_primitive_map_ubo(nir_builder *b, struct ir3_shader_variant *v,
                       unsigned components, unsigned offset)
{
   if (v->binning_pass) {
      return ir3_load_shared_driver_ubo(
         b, components, &ir3_const_state(v)->primitive_map_ubo, offset);
   }
   struct ir3_const_state *const_state = ir3_const_state_mut(v);
   return ir3_load_driver_ubo(b, components, &const_state->primitive_map_ubo,
                              offset);
}

static nir_def *
load_primitive_param_ubo(nir_builder *b, struct ir3_shader_variant *v,
                         unsigned components, unsigned offset)
{
   if (v->binning_pass) {
      return ir3_load_shared_driver_ubo(
         b, components, &ir3_const_state(v)->primitive_param_ubo, offset);
   }
   struct ir3_const_state *const_state = ir3_const_state_mut(v);
   return ir3_load_driver_ubo(b, components, &const_state->primitive_param_ubo,
                              offset);
}

static nir_def *
load_driver_params_ubo(nir_builder *b, struct ir3_shader_variant *v,
                       unsigned components, unsigned offset)
{
   if (v->binning_pass) {
      return ir3_load_shared_driver_ubo(
         b, components, &ir3_const_state(v)->driver_params_ubo, offset);
   }
   struct ir3_const_state *const_state = ir3_const_state_mut(v);
   return ir3_load_driver_ubo(b, components, &const_state->driver_params_ubo,
                              offset);
}

static bool
lower_driver_param_to_ubo(nir_builder *b, nir_intrinsic_instr *intr, void *in)
{
   struct ir3_shader_variant *v = in;

   unsigned components = nir_intrinsic_dest_components(intr);

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *result;
   switch (intr->intrinsic) {
   case nir_intrinsic_load_primitive_location_ir3:
      result = load_primitive_map_ubo(b, v, components,
                                      nir_intrinsic_driver_location(intr));
      break;
   case nir_intrinsic_load_vs_primitive_stride_ir3:
      result = load_primitive_param_ubo(b, v, components, 0);
      break;
   case nir_intrinsic_load_vs_vertex_stride_ir3:
      result = load_primitive_param_ubo(b, v, components, 1);
      break;
   case nir_intrinsic_load_hs_patch_stride_ir3:
      result = load_primitive_param_ubo(b, v, components, 2);
      break;
   case nir_intrinsic_load_patch_vertices_in:
      result = load_primitive_param_ubo(b, v, components, 3);
      break;
   case nir_intrinsic_load_tess_param_base_ir3:
      result = load_primitive_param_ubo(b, v, components, 4);
      break;
   case nir_intrinsic_load_tess_factor_base_ir3:
      result = load_primitive_param_ubo(b, v, components, 6);
      break;
   default: {
      /* On current hw these are still pushed the old way for VS, because of
       * the way SQE patches draw_id/base_vertex/first_vertex/base_instance.
       */
      if (v->type == MESA_SHADER_VERTEX)
         return false;

      struct driver_param_info param_info;
      if (!ir3_get_driver_param_info(b->shader, intr, &param_info))
         return false;

      result = load_driver_params_ubo(b, v, components, param_info.offset);
   }
   }

   nir_instr_remove(&intr->instr);
   nir_def_rewrite_uses(&intr->def, result);

   return true;
}

bool
ir3_nir_lower_driver_params_to_ubo(nir_shader *nir,
                                   struct ir3_shader_variant *v)
{
   bool result = nir_shader_intrinsics_pass(
      nir, lower_driver_param_to_ubo,
      nir_metadata_control_flow, v);

   if (result) {
      const struct ir3_const_state *const_state = ir3_const_state(v);

      ir3_update_driver_ubo(nir, &const_state->primitive_map_ubo, "$primitive_map");
      ir3_update_driver_ubo(nir, &const_state->primitive_param_ubo, "$primitive_param");
      ir3_update_driver_ubo(nir, &const_state->driver_params_ubo, "$driver_params");
   }

   return result;
}
