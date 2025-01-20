/*
 * Copyright 2023 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "midgard_nir.h"
#include "nir_opcodes.h"

static bool
pass(nir_builder *b, nir_alu_instr *alu, void *data)
{
   if (alu->op != nir_op_b32csel)
      return false;

   BITSET_WORD *float_types = data;
   if (BITSET_TEST(float_types, alu->def.index)) {
      alu->op = nir_op_b32fcsel_mdg;
      return true;
   } else {
      return false;
   }
}

bool
midgard_nir_type_csel(nir_shader *shader)
{
   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   nir_index_ssa_defs(impl);

   BITSET_WORD *float_types =
      calloc(BITSET_WORDS(impl->ssa_alloc), sizeof(BITSET_WORD));
   nir_gather_types(impl, float_types, NULL);

   bool progress =
      nir_shader_alu_pass(shader, pass, nir_metadata_control_flow, float_types);

   free(float_types);

   return progress;
}
