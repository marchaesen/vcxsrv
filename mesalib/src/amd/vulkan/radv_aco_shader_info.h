/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef RADV_ACO_SHADER_INFO_H
#define RADV_ACO_SHADER_INFO_H

/* this will convert from radv shader info to the ACO one. */

#include "ac_hw_stage.h"
#include "aco_shader_info.h"

#define ASSIGN_FIELD(x)    aco_info->x = radv->x
#define ASSIGN_FIELD_CP(x) memcpy(&aco_info->x, &radv->x, sizeof(radv->x))

static inline void radv_aco_convert_ps_epilog_key(struct aco_ps_epilog_info *aco_info,
                                                  const struct radv_ps_epilog_key *radv,
                                                  const struct radv_shader_args *radv_args);

static inline void
radv_aco_convert_shader_info(struct aco_shader_info *aco_info, const struct radv_shader_info *radv,
                             const struct radv_shader_args *radv_args, const struct radv_device_cache_key *radv_key,
                             const enum amd_gfx_level gfx_level)
{
   ASSIGN_FIELD(wave_size);
   ASSIGN_FIELD(workgroup_size);
   ASSIGN_FIELD(ps.has_epilog);
   ASSIGN_FIELD(merged_shader_compiled_separately);
   ASSIGN_FIELD(vs.tcs_in_out_eq);
   ASSIGN_FIELD(vs.has_prolog);
   ASSIGN_FIELD(tcs.num_lds_blocks);
   ASSIGN_FIELD(ps.num_inputs);
   ASSIGN_FIELD(cs.uses_full_subgroups);
   aco_info->vs.any_tcs_inputs_via_lds = radv->vs.tcs_inputs_via_lds != 0;
   aco_info->ps.spi_ps_input_ena = radv->ps.spi_ps_input_ena;
   aco_info->ps.spi_ps_input_addr = radv->ps.spi_ps_input_addr;
   aco_info->ps.has_prolog = false;
   aco_info->gfx9_gs_ring_lds_size = radv->gs_ring_info.lds_size;
   aco_info->image_2d_view_of_3d = radv_key->image_2d_view_of_3d;
   aco_info->epilog_pc = radv_args->epilog_pc;
   aco_info->hw_stage = radv_select_hw_stage(radv, gfx_level);
   aco_info->tcs.tcs_offchip_layout = radv_args->tcs_offchip_layout;
   aco_info->next_stage_pc = radv_args->next_stage_pc;
   aco_info->schedule_ngg_pos_exports = gfx_level < GFX11 && radv->has_ngg_culling && radv->has_ngg_early_prim_export;
}

static inline void
radv_aco_convert_vs_prolog_key(struct aco_vs_prolog_info *aco_info, const struct radv_vs_prolog_key *radv,
                               const struct radv_shader_args *radv_args)
{
   ASSIGN_FIELD(instance_rate_inputs);
   ASSIGN_FIELD(nontrivial_divisors);
   ASSIGN_FIELD(zero_divisors);
   ASSIGN_FIELD(post_shuffle);
   ASSIGN_FIELD(alpha_adjust_lo);
   ASSIGN_FIELD(alpha_adjust_hi);
   ASSIGN_FIELD_CP(formats);
   ASSIGN_FIELD(num_attributes);
   ASSIGN_FIELD(misaligned_mask);
   ASSIGN_FIELD(unaligned_mask);
   ASSIGN_FIELD(is_ngg);
   ASSIGN_FIELD(next_stage);

   aco_info->inputs = radv_args->prolog_inputs;
}

static inline void
radv_aco_convert_ps_epilog_key(struct aco_ps_epilog_info *aco_info, const struct radv_ps_epilog_key *radv,
                               const struct radv_shader_args *radv_args)
{
   ASSIGN_FIELD(spi_shader_col_format);
   ASSIGN_FIELD(color_is_int8);
   ASSIGN_FIELD(color_is_int10);
   ASSIGN_FIELD(mrt0_is_dual_src);
   ASSIGN_FIELD(alpha_to_coverage_via_mrtz);
   ASSIGN_FIELD(alpha_to_one);

   memcpy(aco_info->colors, radv_args->colors, sizeof(aco_info->colors));
   memcpy(aco_info->color_map, radv->color_map, sizeof(aco_info->color_map));
   aco_info->depth = radv_args->depth;
   aco_info->stencil = radv_args->stencil;
   aco_info->samplemask = radv_args->sample_mask;

   aco_info->alpha_func = COMPARE_FUNC_ALWAYS;
}

static inline void
radv_aco_convert_opts(struct aco_compiler_options *aco_info, const struct radv_nir_compiler_options *radv,
                      const struct radv_shader_args *radv_args, const struct radv_shader_stage_key *stage_key)
{
   ASSIGN_FIELD(dump_ir);
   ASSIGN_FIELD(dump_preoptir);
   ASSIGN_FIELD(record_asm);
   ASSIGN_FIELD(record_ir);
   ASSIGN_FIELD(record_stats);
   ASSIGN_FIELD(enable_mrt_output_nan_fixup);
   ASSIGN_FIELD(wgp_mode);
   ASSIGN_FIELD(debug.func);
   ASSIGN_FIELD(debug.private_data);
   ASSIGN_FIELD(debug.private_data);
   aco_info->is_opengl = false;
   aco_info->load_grid_size_from_user_sgpr = radv_args->load_grid_size_from_user_sgpr;
   aco_info->optimisations_disabled = stage_key->optimisations_disabled;
   aco_info->gfx_level = radv->info->gfx_level;
   aco_info->family = radv->info->family;
   aco_info->address32_hi = radv->info->address32_hi;
   aco_info->has_ls_vgpr_init_bug = radv->info->has_ls_vgpr_init_bug;
}
#undef ASSIGN_VS_STATE_FIELD
#undef ASSIGN_VS_STATE_FIELD_CP
#undef ASSIGN_FIELD
#undef ASSIGN_FIELD_CP

#endif /* RADV_ACO_SHADER_INFO_H */
