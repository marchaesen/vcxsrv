/*
 * Copyright © 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include <math.h>

static unsigned
get_tess_level_component(nir_intrinsic_instr *intr)
{
   unsigned location = nir_intrinsic_io_semantics(intr).location;

   return (location == VARYING_SLOT_TESS_LEVEL_INNER ? 4 : 0) +
          nir_intrinsic_component(intr);
}

static unsigned
get_inst_tesslevel_writemask(nir_intrinsic_instr *intr)
{
   if (intr->intrinsic != nir_intrinsic_store_output)
      return 0;

   unsigned location = nir_intrinsic_io_semantics(intr).location;
   if (location != VARYING_SLOT_TESS_LEVEL_OUTER &&
       location != VARYING_SLOT_TESS_LEVEL_INNER)
      return 0;

   return nir_intrinsic_write_mask(intr) << get_tess_level_component(intr);
}

static bool
is_tcs_output_barrier(nir_intrinsic_instr *intr)
{
   return intr->intrinsic == nir_intrinsic_barrier &&
          nir_intrinsic_memory_modes(intr) & nir_var_shader_out &&
          nir_intrinsic_memory_scope(intr) >= SCOPE_WORKGROUP &&
          nir_intrinsic_execution_scope(intr) >= SCOPE_WORKGROUP;
}

static void
scan_tess_levels(struct exec_list *cf_list, unsigned *upper_block_tl_writemask,
                 unsigned *cond_block_tl_writemask,
                 bool *all_invocs_define_tess_levels, bool is_nested_cf)
{
   foreach_list_typed(nir_cf_node, cf_node, node, cf_list) {
      switch (cf_node->type) {
      case nir_cf_node_block: {
         nir_block *block = nir_cf_node_as_block(cf_node);
         nir_foreach_instr (instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);

            if (!is_tcs_output_barrier(intrin)) {
               *upper_block_tl_writemask |= get_inst_tesslevel_writemask(intrin);
               continue;
            }

            /* This is a barrier. If it's in nested control flow, put this
             * in the too hard basket. In GLSL this is not possible but it is
             * in SPIR-V.
             */
            if (is_nested_cf) {
               *all_invocs_define_tess_levels = false;
               return;
            }

            /* The following case must be prevented:
             *    gl_TessLevelInner = ...;
             *    barrier();
             *    if (gl_InvocationID == 1)
             *       gl_TessLevelInner = ...;
             *
             * If you consider disjoint code segments separated by barriers,
             * each such segment that writes tess level channels should write
             * the same channels in all codepaths within that segment.
             */
            if (*upper_block_tl_writemask || *cond_block_tl_writemask) {
               /* Accumulate the result: */
               *all_invocs_define_tess_levels &=
                  !(*cond_block_tl_writemask & ~(*upper_block_tl_writemask));

               /* Analyze the next code segment from scratch. */
               *upper_block_tl_writemask = 0;
               *cond_block_tl_writemask = 0;
            }
         }
         break;
      }
      case nir_cf_node_if: {
         unsigned then_tesslevel_writemask = 0;
         unsigned else_tesslevel_writemask = 0;
         nir_if *if_stmt = nir_cf_node_as_if(cf_node);

         scan_tess_levels(&if_stmt->then_list, &then_tesslevel_writemask,
                          cond_block_tl_writemask,
                          all_invocs_define_tess_levels, true);

         scan_tess_levels(&if_stmt->else_list, &else_tesslevel_writemask,
                          cond_block_tl_writemask,
                          all_invocs_define_tess_levels, true);

         if (then_tesslevel_writemask || else_tesslevel_writemask) {
            /* If both statements write the same tess level channels,
             * we can say that the upper block writes them too.
             */
            *upper_block_tl_writemask |= then_tesslevel_writemask &
                                         else_tesslevel_writemask;
            *cond_block_tl_writemask |= then_tesslevel_writemask |
                                        else_tesslevel_writemask;
         }
         break;
      }
      case nir_cf_node_loop: {
         nir_loop *loop = nir_cf_node_as_loop(cf_node);
         assert(!nir_loop_has_continue_construct(loop));

         scan_tess_levels(&loop->body, cond_block_tl_writemask,
                          cond_block_tl_writemask,
                          all_invocs_define_tess_levels, true);
         break;
      }
      default:
         unreachable("unknown cf node type");
      }
   }
}

static bool
all_invocations_define_tess_levels(const struct nir_shader *nir)
{
   assert(nir->info.stage == MESA_SHADER_TESS_CTRL);

   /* The pass works as follows:
    *
    * If all codepaths write tess levels, we can say that all invocations
    * define tess level values. Whether a tess level value is defined is
    * determined for each component separately.
    */
   unsigned main_block_tl_writemask = 0; /* if main block writes tess levels */
   unsigned cond_block_tl_writemask = 0; /* if cond block writes tess levels */

   /* Initial value = true. Here the pass will accumulate results from
    * multiple segments surrounded by barriers. If tess levels aren't
    * written at all, it's a shader bug and we don't care if this will be
    * true.
    */
   bool result = true;

   nir_foreach_function_impl(impl, nir) {
      scan_tess_levels(&impl->body, &main_block_tl_writemask,
                       &cond_block_tl_writemask,
                       &result, false);
   }

   /* Accumulate the result for the last code segment separated by a
    * barrier.
    */
   if (main_block_tl_writemask || cond_block_tl_writemask)
      result &= !(cond_block_tl_writemask & ~main_block_tl_writemask);

   return result;
}

/* It's OK to pass UNSPECIFIED to prim and spacing. */
void
nir_gather_tcs_info(const nir_shader *nir, nir_tcs_info *info,
                    enum tess_primitive_mode prim,
                    enum gl_tess_spacing spacing)
{
   memset(info, 0, sizeof(*info));
   info->all_invocations_define_tess_levels =
      all_invocations_define_tess_levels(nir);

   unsigned tess_level_writes_le_zero = 0;
   unsigned tess_level_writes_le_one = 0;
   unsigned tess_level_writes_le_two = 0;
   unsigned tess_level_writes_other = 0;

   /* Gather barriers and which values are written to tess level outputs. */
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

            if (is_tcs_output_barrier(intr)) {
               /* Only gather barriers outside control flow. */
               if (block->cf_node.parent->type == nir_cf_node_function)
                  info->always_executes_barrier = true;
               continue;
            }

            if (intr->intrinsic != nir_intrinsic_store_output)
               continue;

            unsigned location = nir_intrinsic_io_semantics(intr).location;
            if (location != VARYING_SLOT_TESS_LEVEL_OUTER &&
                location != VARYING_SLOT_TESS_LEVEL_INNER)
               continue;

            unsigned base_shift = get_tess_level_component(intr);
            unsigned writemask = nir_intrinsic_write_mask(intr);

            u_foreach_bit(i, writemask) {
               nir_scalar scalar = nir_scalar_resolved(intr->src[0].ssa, i);
               unsigned shift = base_shift + i;

               if (nir_scalar_is_const(scalar)) {
                  float f = nir_scalar_as_float(scalar);

                  if (f <= 0 || isnan(f))
                     tess_level_writes_le_zero |= BITFIELD_BIT(shift);
                  else if (f <= 1)
                     tess_level_writes_le_one |= BITFIELD_BIT(shift);
                  else if (f <= 2)
                     tess_level_writes_le_two |= BITFIELD_BIT(shift);
                  else
                     tess_level_writes_other |= BITFIELD_BIT(shift);
               } else {
                  /* TODO: This could use range analysis. */
                  tess_level_writes_other |= BITFIELD_BIT(shift);
               }
            }
         }
      }
   }

   /* Determine which outer tess level components can discard patches.
    * If the primitive type is unspecified, we have to assume the worst case.
    */
   unsigned min_outer, min_inner, max_outer, max_inner;
   mesa_count_tess_level_components(prim == TESS_PRIMITIVE_UNSPECIFIED ?
                                       TESS_PRIMITIVE_ISOLINES : prim,
                                    &min_outer, &min_inner);
   mesa_count_tess_level_components(prim, &max_outer, &max_inner);
   const unsigned min_valid_outer_comp_mask = BITFIELD_RANGE(0, min_outer);
   const unsigned max_valid_outer_comp_mask = BITFIELD_RANGE(0, max_outer);
   const unsigned max_valid_inner_comp_mask = BITFIELD_RANGE(4, max_inner);

   /* All tessellation levels are effectively 0 if the patch has at least one
    * outer tess level component either in the [-inf, 0] range or equal to NaN,
    * causing it to be discarded. Inner tess levels have no effect.
    */
   info->all_tess_levels_are_effectively_zero =
      tess_level_writes_le_zero & ~tess_level_writes_le_one &
      ~tess_level_writes_le_two & ~tess_level_writes_other &
      min_valid_outer_comp_mask;

   const unsigned tess_level_writes_any =
      tess_level_writes_le_zero | tess_level_writes_le_one |
      tess_level_writes_le_two | tess_level_writes_other;

   const bool outer_is_gt_zero_le_one =
      (tess_level_writes_le_one & ~tess_level_writes_le_zero &
       ~tess_level_writes_le_two & ~tess_level_writes_other &
       max_valid_outer_comp_mask) ==
      (tess_level_writes_any & max_valid_outer_comp_mask);

   /* Whether the inner tess levels are in the [-inf, 1] range. */
   const bool inner_is_le_one =
      ((tess_level_writes_le_zero | tess_level_writes_le_one) &
       ~tess_level_writes_le_two & ~tess_level_writes_other &
       max_valid_inner_comp_mask) ==
      (tess_level_writes_any & max_valid_inner_comp_mask);

   /* If the patch has tess level values set to 1 or equivalent numbers, it's
    * not discarded, but different things happen depending on the spacing.
    */
   switch (spacing) {
   case TESS_SPACING_EQUAL:
   case TESS_SPACING_FRACTIONAL_ODD:
   case TESS_SPACING_UNSPECIFIED:
      /* The tessellator clamps all tess levels greater than 0 to 1.
       * If all outer and inner tess levels are in the (0, 1] range, which is
       * effectively 1, untessellated patches are drawn.
       */
      info->all_tess_levels_are_effectively_one = outer_is_gt_zero_le_one &&
                                                  inner_is_le_one;
      break;

   case TESS_SPACING_FRACTIONAL_EVEN: {
      /* The tessellator clamps all tess levels to 2 (both outer and inner)
       * except outer tess level component 0 of isolines, which is clamped
       * to 1. If all outer tess levels are in the (0, 2] or (0, 1] range
       * (for outer[0] of isolines) and all inner tess levels are
       * in the [-inf, 2] range, it's the same as writing 1 to all tess
       * levels.
       */
      bool isolines_are_eff_one =
         /* The (0, 1] range of outer[0]. */
         (tess_level_writes_le_one & ~tess_level_writes_le_zero &
          ~tess_level_writes_le_two & ~tess_level_writes_other & 0x1) ==
         (tess_level_writes_any & 0x1) &&
         /* The (0, 2] range of outer[1]. */
         ((tess_level_writes_le_one | tess_level_writes_le_two) &
          ~tess_level_writes_le_zero & ~tess_level_writes_other & 0x2) ==
         (tess_level_writes_any & 0x2);

      bool triquads_are_eff_one =
         /* The (0, 2] outer range. */
         ((tess_level_writes_le_one | tess_level_writes_le_two) &
          ~tess_level_writes_le_zero & ~tess_level_writes_other &
          max_valid_outer_comp_mask) ==
         (tess_level_writes_any & max_valid_outer_comp_mask) &&
         /* The [-inf, 2] inner range. */
         ((tess_level_writes_le_zero | tess_level_writes_le_one |
           tess_level_writes_le_two) & ~tess_level_writes_other &
          max_valid_inner_comp_mask) ==
         (tess_level_writes_any & max_valid_inner_comp_mask);

      if (prim == TESS_PRIMITIVE_UNSPECIFIED) {
         info->all_tess_levels_are_effectively_one = isolines_are_eff_one &&
                                                     triquads_are_eff_one;
      } else if (prim == TESS_PRIMITIVE_ISOLINES) {
         info->all_tess_levels_are_effectively_one = isolines_are_eff_one;
      } else {
         info->all_tess_levels_are_effectively_one = triquads_are_eff_one;
      }
      break;
   }
   }

   assert(!info->all_tess_levels_are_effectively_zero ||
          !info->all_tess_levels_are_effectively_one);

   info->discards_patches =
      (tess_level_writes_le_zero & min_valid_outer_comp_mask) != 0;
}
