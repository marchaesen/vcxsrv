/*
 * Copyright © 2023 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "util/u_math.h"
#include "ir3_compiler.h"
#include "ir3_nir.h"

bool
ir3_nir_lower_push_consts_to_preamble(nir_shader *nir,
                                      struct ir3_shader_variant *v)
{
   const struct ir3_const_state *const_state = ir3_const_state(v);
   nir_function_impl *preamble = nir_shader_get_preamble(nir);
   nir_builder _b = nir_builder_at(nir_before_impl(preamble));
   nir_builder *b = &_b;

   uint32_t offset_vec4 =
      const_state->allocs.consts[IR3_CONST_ALLOC_PUSH_CONSTS].offset_vec4 * 4;
   nir_copy_push_const_to_uniform_ir3(
      b, nir_imm_int(b, offset_vec4),
      .base = v->shader_options.push_consts_base,
      .range = v->shader_options.push_consts_dwords);

   nir_foreach_function_impl(impl, nir) {
      nir_progress(true, impl, nir_metadata_none);
   }
   return true;
}
