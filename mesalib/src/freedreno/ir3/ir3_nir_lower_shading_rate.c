/*
 * Copyright 2024 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "ir3_nir.h"

/* Values written in VS/GS to gl_PrimitiveShadingRateEXT have to
 * be translated into HW internal representation.
 */
static const uint32_t vk_to_hw_shading_rate_lut[] = {
   0, 4, 8, 11, 1, 5, 9, 11, 2, 6, 10, 11, 11, 11, 11, 11,
   0, 1, 2, 11, 4, 5, 6, 11, 8, 9, 10, 11, 11, 11, 11, 11};

/* Values read from gl_ShadingRateEXT in FS have to be translated from
 * HW representation.
 */
static const uint32_t hw_to_vk_shading_rate_lut[] = {
   0, 4, 8, 0, 1, 5, 9, 0, 2, 6, 10, 11, 11, 11, 11, 11,
   0, 1, 2, 0, 4, 5, 6, 0, 8, 9, 10, 11, 11, 11, 11, 11};

static nir_deref_instr *
create_lut(nir_builder *b, const uint32_t *lut, uint32_t lut_size,
           const char *lut_name)
{
   nir_variable *lut_var = nir_local_variable_create(
      b->impl, glsl_array_type(glsl_uint_type(), lut_size, 0), lut_name);
   nir_deref_instr *deref = nir_build_deref_var(b, lut_var);

   for (uint32_t i = 0; i < lut_size; i++) {
      nir_deref_instr *element =
         nir_build_deref_array(b, deref, nir_imm_int(b, i));
      nir_build_store_deref(b, &element->def, nir_imm_int(b, lut[i]), 0x1);
   }

   return deref;
}

static bool
nir_lower_frag_shading_rate(nir_builder *b, nir_intrinsic_instr *intr,
                            UNUSED void *cb_data)
{
   if (intr->intrinsic != nir_intrinsic_load_frag_shading_rate)
      return false;

   b->cursor = nir_after_instr(&intr->instr);

   nir_deref_instr *lut = create_lut(b, hw_to_vk_shading_rate_lut,
                                     ARRAY_SIZE(hw_to_vk_shading_rate_lut),
                                     "hw_to_vk_shading_rate_lut");
   nir_deref_instr *result = nir_build_deref_array(b, lut, &intr->def);
   nir_def *r = nir_build_load_deref(b, 1, 32, &result->def, 0);

   nir_def_rewrite_uses_after(&intr->def, r, r->parent_instr);
   return true;
}

bool
ir3_nir_lower_frag_shading_rate(nir_shader *shader)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);
   return nir_shader_intrinsics_pass(shader, nir_lower_frag_shading_rate,
                                     nir_metadata_control_flow, NULL);
}

static bool
nir_lower_primitive_shading_rate(nir_builder *b, nir_intrinsic_instr *intr,
                                 UNUSED void *cb_data)
{
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   unsigned loc = nir_intrinsic_io_semantics(intr).location;
   if (loc != VARYING_SLOT_PRIMITIVE_SHADING_RATE)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   nir_deref_instr *lut = create_lut(b, vk_to_hw_shading_rate_lut,
                                    ARRAY_SIZE(vk_to_hw_shading_rate_lut),
                                    "vk_to_hw_shading_rate_lut");
   nir_deref_instr *result = nir_build_deref_array(b, lut, intr->src[0].ssa);
   nir_def *r = nir_build_load_deref(b, 1, 32, &result->def, 0);

   nir_src_rewrite(&intr->src[0], r);
   return true;
}

bool
ir3_nir_lower_primitive_shading_rate(nir_shader *shader)
{
   assert(shader->info.stage != MESA_SHADER_FRAGMENT);
   return nir_shader_intrinsics_pass(shader, nir_lower_primitive_shading_rate,
                                     nir_metadata_control_flow, NULL);
}
