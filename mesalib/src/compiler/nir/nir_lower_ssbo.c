/*
 * Copyright 2024 Valve Corporation
 * Copyright 2019 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"

/*
 * Lowers SSBOs to global memory. SSBO base addresses are passed via
 * load_ssbo_address. Run nir_lower_robust_access first for bounds checks.
 */

static nir_def *
calc_address(nir_builder *b, nir_intrinsic_instr *intr,
             const nir_lower_ssbo_options *opts)
{
   unsigned index_src = intr->intrinsic == nir_intrinsic_store_ssbo ? 1 : 0;
   bool lower_offset = !opts || !opts->native_offset;
   nir_def *offset = nir_get_io_offset_src(intr)->ssa;
   nir_def *addr =
      nir_load_ssbo_address(b, 1, 64, intr->src[index_src].ssa,
                            lower_offset ? nir_imm_int(b, 0) : offset);

   if (lower_offset)
      addr = nir_iadd(b, addr, nir_u2u64(b, offset));

   return addr;
}

static bool
pass(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   const nir_lower_ssbo_options *opts = data;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *def = NULL;
   switch (intr->intrinsic) {
   case nir_intrinsic_load_ssbo:
      if (opts && opts->native_loads)
         return false;

      def = nir_build_load_global(b, intr->def.num_components,
                                  intr->def.bit_size,
                                  calc_address(b, intr, opts),
                                  .align_mul = nir_intrinsic_align_mul(intr),
                                  .align_offset = nir_intrinsic_align_offset(intr));
      break;

   case nir_intrinsic_store_ssbo:
      nir_build_store_global(b, intr->src[0].ssa,
                             calc_address(b, intr, opts),
                             .align_mul = nir_intrinsic_align_mul(intr),
                             .align_offset = nir_intrinsic_align_offset(intr),
                             .write_mask = nir_intrinsic_write_mask(intr));
      break;

   case nir_intrinsic_ssbo_atomic:
      def = nir_global_atomic(b, intr->def.bit_size,
                              calc_address(b, intr, opts),
                              intr->src[2].ssa,
                              .atomic_op = nir_intrinsic_atomic_op(intr));
      break;

   case nir_intrinsic_ssbo_atomic_swap:
      def = nir_global_atomic_swap(b, intr->def.bit_size,
                                   calc_address(b, intr, opts),
                                   intr->src[2].ssa, intr->src[3].ssa,
                                   .atomic_op = nir_intrinsic_atomic_op(intr));
      break;

   default:
      return false;
   }

   if (def)
      nir_def_rewrite_uses(&intr->def, def);

   nir_instr_remove(&intr->instr);
   return true;
}

bool
nir_lower_ssbo(nir_shader *shader, const nir_lower_ssbo_options *opts)
{
   return nir_shader_intrinsics_pass(shader, pass,
                                     nir_metadata_control_flow,
                                     (void *)opts);
}
