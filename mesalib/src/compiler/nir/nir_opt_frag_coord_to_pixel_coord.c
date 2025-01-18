/*
 * Copyright 2024 Valve Corpoation
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"
#include "nir_builder_opcodes.h"

/**
 * If load_frag_coord.xy is only used by conversions to integer,
 * replace it with load_pixel_coord.
 */

static bool
opt_frag_pos(nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_frag_coord)
      return false;

   /* Don't increase precision. */
   if (intr->def.bit_size != 32)
      return false;

   /* Check if xy are only used by casts to integers. */
   nir_foreach_use(use, &intr->def) {
      if (nir_src_is_if(use))
         return false;

      unsigned mask = nir_src_components_read(use);

      if (!(mask & 0x3))
         continue;

      /* Don't handle instructions that read x/y and z/w for simplicity. */
      if (mask & ~0x3)
         return false;

      nir_instr *use_instr = nir_src_parent_instr(use);

      if (use_instr->type != nir_instr_type_alu)
         return false;

      switch (nir_instr_as_alu(use_instr)->op) {
      case nir_op_f2i8:
      case nir_op_f2i16:
      case nir_op_f2i32:
      case nir_op_f2i64:
      case nir_op_f2u8:
      case nir_op_f2u16:
      case nir_op_f2u32:
      case nir_op_f2u64:
      case nir_op_ftrunc:
      case nir_op_ffloor:
         continue;
      default:
         return false;
      }
   }

   b->cursor = nir_before_instr(&intr->instr);
   nir_def *pixel_coord = nir_load_pixel_coord(b);

   nir_foreach_use_safe(use, &intr->def) {
      unsigned mask = nir_src_components_read(use);

      if (!(mask & 0x3))
         continue;

      nir_src_rewrite(use, pixel_coord);

      nir_alu_instr *use_instr = nir_instr_as_alu(nir_src_parent_instr(use));

      /* load_frag_coord is always positive, so we should never sign extend here. */
      bool needs_float = use_instr->op == nir_op_ffloor || use_instr->op == nir_op_ftrunc;
      nir_alu_type dst_type = (needs_float ? nir_type_float : nir_type_uint) | use_instr->def.bit_size;
      use_instr->op = nir_type_conversion_op(nir_type_uint16, dst_type, nir_rounding_mode_undef);
   }

   return true;
}

bool
nir_opt_frag_coord_to_pixel_coord(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, opt_frag_pos,
                                     nir_metadata_control_flow,
                                     NULL);
}
