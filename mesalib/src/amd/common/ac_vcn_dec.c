/**************************************************************************
 *
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#include <stdint.h>
#include "util/u_math.h"

#include "ac_vcn_dec.h"
#include "ac_vcn_av1_default.h"

static unsigned
ac_vcn_dec_frame_ctx_size_av1(unsigned av1_version)
{
   return av1_version == RDECODE_AV1_VER_0
      ? align(sizeof(rvcn_av1_frame_context_t), 2048)
      : align(sizeof(rvcn_av1_vcn4_frame_context_t), 2048);
}

unsigned
ac_vcn_dec_calc_ctx_size_av1(unsigned av1_version)
{
   unsigned frame_ctxt_size = ac_vcn_dec_frame_ctx_size_av1(av1_version);
   unsigned ctx_size = (9 + 4) * frame_ctxt_size + 9 * 64 * 34 * 512 + 9 * 64 * 34 * 256 * 5;

   int num_64x64_CTB_8k = 68;
   int num_128x128_CTB_8k = 34;
   int sdb_pitch_64x64 = align(32 * num_64x64_CTB_8k, 256) * 2;
   int sdb_pitch_128x128 = align(32 * num_128x128_CTB_8k, 256) * 2;
   int sdb_lf_size_ctb_64x64 = sdb_pitch_64x64 * (align(1728, 64) / 64);
   int sdb_lf_size_ctb_128x128 = sdb_pitch_128x128 * (align(3008, 64) / 64);
   int sdb_superres_size_ctb_64x64 = sdb_pitch_64x64 * (align(3232, 64) / 64);
   int sdb_superres_size_ctb_128x128 = sdb_pitch_128x128 * (align(6208, 64) / 64);
   int sdb_output_size_ctb_64x64 = sdb_pitch_64x64 * (align(1312, 64) / 64);
   int sdb_output_size_ctb_128x128 = sdb_pitch_128x128 * (align(2336, 64) / 64);
   int sdb_fg_avg_luma_size_ctb_64x64 = sdb_pitch_64x64 * (align(384, 64) / 64);
   int sdb_fg_avg_luma_size_ctb_128x128 = sdb_pitch_128x128 * (align(640, 64) / 64);

   ctx_size += (MAX2(sdb_lf_size_ctb_64x64, sdb_lf_size_ctb_128x128) +
                MAX2(sdb_superres_size_ctb_64x64, sdb_superres_size_ctb_128x128) +
                MAX2(sdb_output_size_ctb_64x64, sdb_output_size_ctb_128x128) +
                MAX2(sdb_fg_avg_luma_size_ctb_64x64, sdb_fg_avg_luma_size_ctb_128x128)) *
                  2 +
               68 * 512;

   return ctx_size;
}

static void
ac_vcn_av1_init_mode_probs(void *prob)
{
   rvcn_av1_frame_context_t *fc = (rvcn_av1_frame_context_t *)prob;
   int i;

   memcpy(fc->palette_y_size_cdf, default_palette_y_size_cdf, sizeof(default_palette_y_size_cdf));
   memcpy(fc->palette_uv_size_cdf, default_palette_uv_size_cdf, sizeof(default_palette_uv_size_cdf));
   memcpy(fc->palette_y_color_index_cdf, default_palette_y_color_index_cdf, sizeof(default_palette_y_color_index_cdf));
   memcpy(fc->palette_uv_color_index_cdf, default_palette_uv_color_index_cdf,
          sizeof(default_palette_uv_color_index_cdf));
   memcpy(fc->kf_y_cdf, default_kf_y_mode_cdf, sizeof(default_kf_y_mode_cdf));
   memcpy(fc->angle_delta_cdf, default_angle_delta_cdf, sizeof(default_angle_delta_cdf));
   memcpy(fc->comp_inter_cdf, default_comp_inter_cdf, sizeof(default_comp_inter_cdf));
   memcpy(fc->comp_ref_type_cdf, default_comp_ref_type_cdf, sizeof(default_comp_ref_type_cdf));
   memcpy(fc->uni_comp_ref_cdf, default_uni_comp_ref_cdf, sizeof(default_uni_comp_ref_cdf));
   memcpy(fc->palette_y_mode_cdf, default_palette_y_mode_cdf, sizeof(default_palette_y_mode_cdf));
   memcpy(fc->palette_uv_mode_cdf, default_palette_uv_mode_cdf, sizeof(default_palette_uv_mode_cdf));
   memcpy(fc->comp_ref_cdf, default_comp_ref_cdf, sizeof(default_comp_ref_cdf));
   memcpy(fc->comp_bwdref_cdf, default_comp_bwdref_cdf, sizeof(default_comp_bwdref_cdf));
   memcpy(fc->single_ref_cdf, default_single_ref_cdf, sizeof(default_single_ref_cdf));
   memcpy(fc->txfm_partition_cdf, default_txfm_partition_cdf, sizeof(default_txfm_partition_cdf));
   memcpy(fc->compound_index_cdf, default_compound_idx_cdfs, sizeof(default_compound_idx_cdfs));
   memcpy(fc->comp_group_idx_cdf, default_comp_group_idx_cdfs, sizeof(default_comp_group_idx_cdfs));
   memcpy(fc->newmv_cdf, default_newmv_cdf, sizeof(default_newmv_cdf));
   memcpy(fc->zeromv_cdf, default_zeromv_cdf, sizeof(default_zeromv_cdf));
   memcpy(fc->refmv_cdf, default_refmv_cdf, sizeof(default_refmv_cdf));
   memcpy(fc->drl_cdf, default_drl_cdf, sizeof(default_drl_cdf));
   memcpy(fc->motion_mode_cdf, default_motion_mode_cdf, sizeof(default_motion_mode_cdf));
   memcpy(fc->obmc_cdf, default_obmc_cdf, sizeof(default_obmc_cdf));
   memcpy(fc->inter_compound_mode_cdf, default_inter_compound_mode_cdf, sizeof(default_inter_compound_mode_cdf));
   memcpy(fc->compound_type_cdf, default_compound_type_cdf, sizeof(default_compound_type_cdf));
   memcpy(fc->wedge_idx_cdf, default_wedge_idx_cdf, sizeof(default_wedge_idx_cdf));
   memcpy(fc->interintra_cdf, default_interintra_cdf, sizeof(default_interintra_cdf));
   memcpy(fc->wedge_interintra_cdf, default_wedge_interintra_cdf, sizeof(default_wedge_interintra_cdf));
   memcpy(fc->interintra_mode_cdf, default_interintra_mode_cdf, sizeof(default_interintra_mode_cdf));
   memcpy(fc->pred_cdf, default_segment_pred_cdf, sizeof(default_segment_pred_cdf));
   memcpy(fc->switchable_restore_cdf, default_switchable_restore_cdf, sizeof(default_switchable_restore_cdf));
   memcpy(fc->wiener_restore_cdf, default_wiener_restore_cdf, sizeof(default_wiener_restore_cdf));
   memcpy(fc->sgrproj_restore_cdf, default_sgrproj_restore_cdf, sizeof(default_sgrproj_restore_cdf));
   memcpy(fc->y_mode_cdf, default_if_y_mode_cdf, sizeof(default_if_y_mode_cdf));
   memcpy(fc->uv_mode_cdf, default_uv_mode_cdf, sizeof(default_uv_mode_cdf));
   memcpy(fc->switchable_interp_cdf, default_switchable_interp_cdf, sizeof(default_switchable_interp_cdf));
   memcpy(fc->partition_cdf, default_partition_cdf, sizeof(default_partition_cdf));
   memcpy(fc->intra_ext_tx_cdf, default_intra_ext_tx_cdf, sizeof(default_intra_ext_tx_cdf));
   memcpy(fc->inter_ext_tx_cdf, default_inter_ext_tx_cdf, sizeof(default_inter_ext_tx_cdf));
   memcpy(fc->skip_cdfs, default_skip_cdfs, sizeof(default_skip_cdfs));
   memcpy(fc->intra_inter_cdf, default_intra_inter_cdf, sizeof(default_intra_inter_cdf));
   memcpy(fc->tree_cdf, default_seg_tree_cdf, sizeof(default_seg_tree_cdf));
   for (i = 0; i < SPATIAL_PREDICTION_PROBS; ++i)
      memcpy(fc->spatial_pred_seg_cdf[i], default_spatial_pred_seg_tree_cdf[i],
             sizeof(default_spatial_pred_seg_tree_cdf[i]));
   memcpy(fc->tx_size_cdf, default_tx_size_cdf, sizeof(default_tx_size_cdf));
   memcpy(fc->delta_q_cdf, default_delta_q_cdf, sizeof(default_delta_q_cdf));
   memcpy(fc->skip_mode_cdfs, default_skip_mode_cdfs, sizeof(default_skip_mode_cdfs));
   memcpy(fc->delta_lf_cdf, default_delta_lf_cdf, sizeof(default_delta_lf_cdf));
   memcpy(fc->delta_lf_multi_cdf, default_delta_lf_multi_cdf, sizeof(default_delta_lf_multi_cdf));
   memcpy(fc->cfl_sign_cdf, default_cfl_sign_cdf, sizeof(default_cfl_sign_cdf));
   memcpy(fc->cfl_alpha_cdf, default_cfl_alpha_cdf, sizeof(default_cfl_alpha_cdf));
   memcpy(fc->filter_intra_cdfs, default_filter_intra_cdfs, sizeof(default_filter_intra_cdfs));
   memcpy(fc->filter_intra_mode_cdf, default_filter_intra_mode_cdf, sizeof(default_filter_intra_mode_cdf));
   memcpy(fc->intrabc_cdf, default_intrabc_cdf, sizeof(default_intrabc_cdf));
}

static void
ac_vcn_av1_init_mv_probs(void *prob)
{
   rvcn_av1_frame_context_t *fc = (rvcn_av1_frame_context_t *)prob;

   memcpy(fc->nmvc_joints_cdf, default_nmv_context.joints_cdf, sizeof(default_nmv_context.joints_cdf));
   memcpy(fc->nmvc_0_bits_cdf, default_nmv_context.comps[0].bits_cdf, sizeof(default_nmv_context.comps[0].bits_cdf));
   memcpy(fc->nmvc_0_class0_cdf, default_nmv_context.comps[0].class0_cdf,
          sizeof(default_nmv_context.comps[0].class0_cdf));
   memcpy(fc->nmvc_0_class0_fp_cdf, default_nmv_context.comps[0].class0_fp_cdf,
          sizeof(default_nmv_context.comps[0].class0_fp_cdf));
   memcpy(fc->nmvc_0_class0_hp_cdf, default_nmv_context.comps[0].class0_hp_cdf,
          sizeof(default_nmv_context.comps[0].class0_hp_cdf));
   memcpy(fc->nmvc_0_classes_cdf, default_nmv_context.comps[0].classes_cdf,
          sizeof(default_nmv_context.comps[0].classes_cdf));
   memcpy(fc->nmvc_0_fp_cdf, default_nmv_context.comps[0].fp_cdf, sizeof(default_nmv_context.comps[0].fp_cdf));
   memcpy(fc->nmvc_0_hp_cdf, default_nmv_context.comps[0].hp_cdf, sizeof(default_nmv_context.comps[0].hp_cdf));
   memcpy(fc->nmvc_0_sign_cdf, default_nmv_context.comps[0].sign_cdf, sizeof(default_nmv_context.comps[0].sign_cdf));
   memcpy(fc->nmvc_1_bits_cdf, default_nmv_context.comps[1].bits_cdf, sizeof(default_nmv_context.comps[1].bits_cdf));
   memcpy(fc->nmvc_1_class0_cdf, default_nmv_context.comps[1].class0_cdf,
          sizeof(default_nmv_context.comps[1].class0_cdf));
   memcpy(fc->nmvc_1_class0_fp_cdf, default_nmv_context.comps[1].class0_fp_cdf,
          sizeof(default_nmv_context.comps[1].class0_fp_cdf));
   memcpy(fc->nmvc_1_class0_hp_cdf, default_nmv_context.comps[1].class0_hp_cdf,
          sizeof(default_nmv_context.comps[1].class0_hp_cdf));
   memcpy(fc->nmvc_1_classes_cdf, default_nmv_context.comps[1].classes_cdf,
          sizeof(default_nmv_context.comps[1].classes_cdf));
   memcpy(fc->nmvc_1_fp_cdf, default_nmv_context.comps[1].fp_cdf, sizeof(default_nmv_context.comps[1].fp_cdf));
   memcpy(fc->nmvc_1_hp_cdf, default_nmv_context.comps[1].hp_cdf, sizeof(default_nmv_context.comps[1].hp_cdf));
   memcpy(fc->nmvc_1_sign_cdf, default_nmv_context.comps[1].sign_cdf, sizeof(default_nmv_context.comps[1].sign_cdf));
   memcpy(fc->ndvc_joints_cdf, default_nmv_context.joints_cdf, sizeof(default_nmv_context.joints_cdf));
   memcpy(fc->ndvc_0_bits_cdf, default_nmv_context.comps[0].bits_cdf, sizeof(default_nmv_context.comps[0].bits_cdf));
   memcpy(fc->ndvc_0_class0_cdf, default_nmv_context.comps[0].class0_cdf,
          sizeof(default_nmv_context.comps[0].class0_cdf));
   memcpy(fc->ndvc_0_class0_fp_cdf, default_nmv_context.comps[0].class0_fp_cdf,
          sizeof(default_nmv_context.comps[0].class0_fp_cdf));
   memcpy(fc->ndvc_0_class0_hp_cdf, default_nmv_context.comps[0].class0_hp_cdf,
          sizeof(default_nmv_context.comps[0].class0_hp_cdf));
   memcpy(fc->ndvc_0_classes_cdf, default_nmv_context.comps[0].classes_cdf,
          sizeof(default_nmv_context.comps[0].classes_cdf));
   memcpy(fc->ndvc_0_fp_cdf, default_nmv_context.comps[0].fp_cdf, sizeof(default_nmv_context.comps[0].fp_cdf));
   memcpy(fc->ndvc_0_hp_cdf, default_nmv_context.comps[0].hp_cdf, sizeof(default_nmv_context.comps[0].hp_cdf));
   memcpy(fc->ndvc_0_sign_cdf, default_nmv_context.comps[0].sign_cdf, sizeof(default_nmv_context.comps[0].sign_cdf));
   memcpy(fc->ndvc_1_bits_cdf, default_nmv_context.comps[1].bits_cdf, sizeof(default_nmv_context.comps[1].bits_cdf));
   memcpy(fc->ndvc_1_class0_cdf, default_nmv_context.comps[1].class0_cdf,
          sizeof(default_nmv_context.comps[1].class0_cdf));
   memcpy(fc->ndvc_1_class0_fp_cdf, default_nmv_context.comps[1].class0_fp_cdf,
          sizeof(default_nmv_context.comps[1].class0_fp_cdf));
   memcpy(fc->ndvc_1_class0_hp_cdf, default_nmv_context.comps[1].class0_hp_cdf,
          sizeof(default_nmv_context.comps[1].class0_hp_cdf));
   memcpy(fc->ndvc_1_classes_cdf, default_nmv_context.comps[1].classes_cdf,
          sizeof(default_nmv_context.comps[1].classes_cdf));
   memcpy(fc->ndvc_1_fp_cdf, default_nmv_context.comps[1].fp_cdf, sizeof(default_nmv_context.comps[1].fp_cdf));
   memcpy(fc->ndvc_1_hp_cdf, default_nmv_context.comps[1].hp_cdf, sizeof(default_nmv_context.comps[1].hp_cdf));
   memcpy(fc->ndvc_1_sign_cdf, default_nmv_context.comps[1].sign_cdf, sizeof(default_nmv_context.comps[1].sign_cdf));
}

static void
ac_vcn_av1_default_coef_probs(void *prob, int index)
{
   rvcn_av1_frame_context_t *fc = (rvcn_av1_frame_context_t *)prob;

   memcpy(fc->txb_skip_cdf, av1_default_txb_skip_cdfs[index], sizeof(av1_default_txb_skip_cdfs[index]));
   memcpy(fc->eob_extra_cdf, av1_default_eob_extra_cdfs[index], sizeof(av1_default_eob_extra_cdfs[index]));
   memcpy(fc->dc_sign_cdf, av1_default_dc_sign_cdfs[index], sizeof(av1_default_dc_sign_cdfs[index]));
   memcpy(fc->coeff_br_cdf, av1_default_coeff_lps_multi_cdfs[index], sizeof(av1_default_coeff_lps_multi_cdfs[index]));
   memcpy(fc->coeff_base_cdf, av1_default_coeff_base_multi_cdfs[index],
          sizeof(av1_default_coeff_base_multi_cdfs[index]));
   memcpy(fc->coeff_base_eob_cdf, av1_default_coeff_base_eob_multi_cdfs[index],
          sizeof(av1_default_coeff_base_eob_multi_cdfs[index]));
   memcpy(fc->eob_flag_cdf16, av1_default_eob_multi16_cdfs[index], sizeof(av1_default_eob_multi16_cdfs[index]));
   memcpy(fc->eob_flag_cdf32, av1_default_eob_multi32_cdfs[index], sizeof(av1_default_eob_multi32_cdfs[index]));
   memcpy(fc->eob_flag_cdf64, av1_default_eob_multi64_cdfs[index], sizeof(av1_default_eob_multi64_cdfs[index]));
   memcpy(fc->eob_flag_cdf128, av1_default_eob_multi128_cdfs[index], sizeof(av1_default_eob_multi128_cdfs[index]));
   memcpy(fc->eob_flag_cdf256, av1_default_eob_multi256_cdfs[index], sizeof(av1_default_eob_multi256_cdfs[index]));
   memcpy(fc->eob_flag_cdf512, av1_default_eob_multi512_cdfs[index], sizeof(av1_default_eob_multi512_cdfs[index]));
   memcpy(fc->eob_flag_cdf1024, av1_default_eob_multi1024_cdfs[index], sizeof(av1_default_eob_multi1024_cdfs[index]));
}

static void
ac_vcn_vcn4_av1_init_mode_probs(void *prob)
{
   rvcn_av1_vcn4_frame_context_t *fc = (rvcn_av1_vcn4_frame_context_t *)prob;
   int i;

   memcpy(fc->palette_y_size_cdf, default_palette_y_size_cdf, sizeof(default_palette_y_size_cdf));
   memcpy(fc->palette_uv_size_cdf, default_palette_uv_size_cdf, sizeof(default_palette_uv_size_cdf));
   memcpy(fc->palette_y_color_index_cdf, default_palette_y_color_index_cdf, sizeof(default_palette_y_color_index_cdf));
   memcpy(fc->palette_uv_color_index_cdf, default_palette_uv_color_index_cdf,
          sizeof(default_palette_uv_color_index_cdf));
   memcpy(fc->kf_y_cdf, default_kf_y_mode_cdf, sizeof(default_kf_y_mode_cdf));
   memcpy(fc->angle_delta_cdf, default_angle_delta_cdf, sizeof(default_angle_delta_cdf));
   memcpy(fc->comp_inter_cdf, default_comp_inter_cdf, sizeof(default_comp_inter_cdf));
   memcpy(fc->comp_ref_type_cdf, default_comp_ref_type_cdf, sizeof(default_comp_ref_type_cdf));
   memcpy(fc->uni_comp_ref_cdf, default_uni_comp_ref_cdf, sizeof(default_uni_comp_ref_cdf));
   memcpy(fc->palette_y_mode_cdf, default_palette_y_mode_cdf, sizeof(default_palette_y_mode_cdf));
   memcpy(fc->palette_uv_mode_cdf, default_palette_uv_mode_cdf, sizeof(default_palette_uv_mode_cdf));
   memcpy(fc->comp_ref_cdf, default_comp_ref_cdf, sizeof(default_comp_ref_cdf));
   memcpy(fc->comp_bwdref_cdf, default_comp_bwdref_cdf, sizeof(default_comp_bwdref_cdf));
   memcpy(fc->single_ref_cdf, default_single_ref_cdf, sizeof(default_single_ref_cdf));
   memcpy(fc->txfm_partition_cdf, default_txfm_partition_cdf, sizeof(default_txfm_partition_cdf));
   memcpy(fc->compound_index_cdf, default_compound_idx_cdfs, sizeof(default_compound_idx_cdfs));
   memcpy(fc->comp_group_idx_cdf, default_comp_group_idx_cdfs, sizeof(default_comp_group_idx_cdfs));
   memcpy(fc->newmv_cdf, default_newmv_cdf, sizeof(default_newmv_cdf));
   memcpy(fc->zeromv_cdf, default_zeromv_cdf, sizeof(default_zeromv_cdf));
   memcpy(fc->refmv_cdf, default_refmv_cdf, sizeof(default_refmv_cdf));
   memcpy(fc->drl_cdf, default_drl_cdf, sizeof(default_drl_cdf));
   memcpy(fc->motion_mode_cdf, default_motion_mode_cdf, sizeof(default_motion_mode_cdf));
   memcpy(fc->obmc_cdf, default_obmc_cdf, sizeof(default_obmc_cdf));
   memcpy(fc->inter_compound_mode_cdf, default_inter_compound_mode_cdf, sizeof(default_inter_compound_mode_cdf));
   memcpy(fc->compound_type_cdf, default_compound_type_cdf, sizeof(default_compound_type_cdf));
   memcpy(fc->wedge_idx_cdf, default_wedge_idx_cdf, sizeof(default_wedge_idx_cdf));
   memcpy(fc->interintra_cdf, default_interintra_cdf, sizeof(default_interintra_cdf));
   memcpy(fc->wedge_interintra_cdf, default_wedge_interintra_cdf, sizeof(default_wedge_interintra_cdf));
   memcpy(fc->interintra_mode_cdf, default_interintra_mode_cdf, sizeof(default_interintra_mode_cdf));
   memcpy(fc->pred_cdf, default_segment_pred_cdf, sizeof(default_segment_pred_cdf));
   memcpy(fc->switchable_restore_cdf, default_switchable_restore_cdf, sizeof(default_switchable_restore_cdf));
   memcpy(fc->wiener_restore_cdf, default_wiener_restore_cdf, sizeof(default_wiener_restore_cdf));
   memcpy(fc->sgrproj_restore_cdf, default_sgrproj_restore_cdf, sizeof(default_sgrproj_restore_cdf));
   memcpy(fc->y_mode_cdf, default_if_y_mode_cdf, sizeof(default_if_y_mode_cdf));
   memcpy(fc->uv_mode_cdf, default_uv_mode_cdf, sizeof(default_uv_mode_cdf));
   memcpy(fc->switchable_interp_cdf, default_switchable_interp_cdf, sizeof(default_switchable_interp_cdf));
   memcpy(fc->partition_cdf, default_partition_cdf, sizeof(default_partition_cdf));
   memcpy(fc->intra_ext_tx_cdf, &default_intra_ext_tx_cdf[1], sizeof(default_intra_ext_tx_cdf[1]) * 2);
   memcpy(fc->inter_ext_tx_cdf, &default_inter_ext_tx_cdf[1], sizeof(default_inter_ext_tx_cdf[1]) * 3);
   memcpy(fc->skip_cdfs, default_skip_cdfs, sizeof(default_skip_cdfs));
   memcpy(fc->intra_inter_cdf, default_intra_inter_cdf, sizeof(default_intra_inter_cdf));
   memcpy(fc->tree_cdf, default_seg_tree_cdf, sizeof(default_seg_tree_cdf));
   for (i = 0; i < SPATIAL_PREDICTION_PROBS; ++i)
      memcpy(fc->spatial_pred_seg_cdf[i], default_spatial_pred_seg_tree_cdf[i],
             sizeof(default_spatial_pred_seg_tree_cdf[i]));
   memcpy(fc->tx_size_cdf, default_tx_size_cdf, sizeof(default_tx_size_cdf));
   memcpy(fc->delta_q_cdf, default_delta_q_cdf, sizeof(default_delta_q_cdf));
   memcpy(fc->skip_mode_cdfs, default_skip_mode_cdfs, sizeof(default_skip_mode_cdfs));
   memcpy(fc->delta_lf_cdf, default_delta_lf_cdf, sizeof(default_delta_lf_cdf));
   memcpy(fc->delta_lf_multi_cdf, default_delta_lf_multi_cdf, sizeof(default_delta_lf_multi_cdf));
   memcpy(fc->cfl_sign_cdf, default_cfl_sign_cdf, sizeof(default_cfl_sign_cdf));
   memcpy(fc->cfl_alpha_cdf, default_cfl_alpha_cdf, sizeof(default_cfl_alpha_cdf));
   memcpy(fc->filter_intra_cdfs, default_filter_intra_cdfs, sizeof(default_filter_intra_cdfs));
   memcpy(fc->filter_intra_mode_cdf, default_filter_intra_mode_cdf, sizeof(default_filter_intra_mode_cdf));
   memcpy(fc->intrabc_cdf, default_intrabc_cdf, sizeof(default_intrabc_cdf));
}

static void
ac_vcn_vcn4_av1_init_mv_probs(void *prob)
{
   rvcn_av1_vcn4_frame_context_t *fc = (rvcn_av1_vcn4_frame_context_t *)prob;

   memcpy(fc->nmvc_joints_cdf, default_nmv_context.joints_cdf, sizeof(default_nmv_context.joints_cdf));
   memcpy(fc->nmvc_0_bits_cdf, default_nmv_context.comps[0].bits_cdf, sizeof(default_nmv_context.comps[0].bits_cdf));
   memcpy(fc->nmvc_0_class0_cdf, default_nmv_context.comps[0].class0_cdf,
          sizeof(default_nmv_context.comps[0].class0_cdf));
   memcpy(fc->nmvc_0_class0_fp_cdf, default_nmv_context.comps[0].class0_fp_cdf,
          sizeof(default_nmv_context.comps[0].class0_fp_cdf));
   memcpy(fc->nmvc_0_class0_hp_cdf, default_nmv_context.comps[0].class0_hp_cdf,
          sizeof(default_nmv_context.comps[0].class0_hp_cdf));
   memcpy(fc->nmvc_0_classes_cdf, default_nmv_context.comps[0].classes_cdf,
          sizeof(default_nmv_context.comps[0].classes_cdf));
   memcpy(fc->nmvc_0_fp_cdf, default_nmv_context.comps[0].fp_cdf, sizeof(default_nmv_context.comps[0].fp_cdf));
   memcpy(fc->nmvc_0_hp_cdf, default_nmv_context.comps[0].hp_cdf, sizeof(default_nmv_context.comps[0].hp_cdf));
   memcpy(fc->nmvc_0_sign_cdf, default_nmv_context.comps[0].sign_cdf, sizeof(default_nmv_context.comps[0].sign_cdf));
   memcpy(fc->nmvc_1_bits_cdf, default_nmv_context.comps[1].bits_cdf, sizeof(default_nmv_context.comps[1].bits_cdf));
   memcpy(fc->nmvc_1_class0_cdf, default_nmv_context.comps[1].class0_cdf,
          sizeof(default_nmv_context.comps[1].class0_cdf));
   memcpy(fc->nmvc_1_class0_fp_cdf, default_nmv_context.comps[1].class0_fp_cdf,
          sizeof(default_nmv_context.comps[1].class0_fp_cdf));
   memcpy(fc->nmvc_1_class0_hp_cdf, default_nmv_context.comps[1].class0_hp_cdf,
          sizeof(default_nmv_context.comps[1].class0_hp_cdf));
   memcpy(fc->nmvc_1_classes_cdf, default_nmv_context.comps[1].classes_cdf,
          sizeof(default_nmv_context.comps[1].classes_cdf));
   memcpy(fc->nmvc_1_fp_cdf, default_nmv_context.comps[1].fp_cdf, sizeof(default_nmv_context.comps[1].fp_cdf));
   memcpy(fc->nmvc_1_hp_cdf, default_nmv_context.comps[1].hp_cdf, sizeof(default_nmv_context.comps[1].hp_cdf));
   memcpy(fc->nmvc_1_sign_cdf, default_nmv_context.comps[1].sign_cdf, sizeof(default_nmv_context.comps[1].sign_cdf));
   memcpy(fc->ndvc_joints_cdf, default_nmv_context.joints_cdf, sizeof(default_nmv_context.joints_cdf));
   memcpy(fc->ndvc_0_bits_cdf, default_nmv_context.comps[0].bits_cdf, sizeof(default_nmv_context.comps[0].bits_cdf));
   memcpy(fc->ndvc_0_class0_cdf, default_nmv_context.comps[0].class0_cdf,
          sizeof(default_nmv_context.comps[0].class0_cdf));
   memcpy(fc->ndvc_0_class0_fp_cdf, default_nmv_context.comps[0].class0_fp_cdf,
          sizeof(default_nmv_context.comps[0].class0_fp_cdf));
   memcpy(fc->ndvc_0_class0_hp_cdf, default_nmv_context.comps[0].class0_hp_cdf,
          sizeof(default_nmv_context.comps[0].class0_hp_cdf));
   memcpy(fc->ndvc_0_classes_cdf, default_nmv_context.comps[0].classes_cdf,
          sizeof(default_nmv_context.comps[0].classes_cdf));
   memcpy(fc->ndvc_0_fp_cdf, default_nmv_context.comps[0].fp_cdf, sizeof(default_nmv_context.comps[0].fp_cdf));
   memcpy(fc->ndvc_0_hp_cdf, default_nmv_context.comps[0].hp_cdf, sizeof(default_nmv_context.comps[0].hp_cdf));
   memcpy(fc->ndvc_0_sign_cdf, default_nmv_context.comps[0].sign_cdf, sizeof(default_nmv_context.comps[0].sign_cdf));
   memcpy(fc->ndvc_1_bits_cdf, default_nmv_context.comps[1].bits_cdf, sizeof(default_nmv_context.comps[1].bits_cdf));
   memcpy(fc->ndvc_1_class0_cdf, default_nmv_context.comps[1].class0_cdf,
          sizeof(default_nmv_context.comps[1].class0_cdf));
   memcpy(fc->ndvc_1_class0_fp_cdf, default_nmv_context.comps[1].class0_fp_cdf,
          sizeof(default_nmv_context.comps[1].class0_fp_cdf));
   memcpy(fc->ndvc_1_class0_hp_cdf, default_nmv_context.comps[1].class0_hp_cdf,
          sizeof(default_nmv_context.comps[1].class0_hp_cdf));
   memcpy(fc->ndvc_1_classes_cdf, default_nmv_context.comps[1].classes_cdf,
          sizeof(default_nmv_context.comps[1].classes_cdf));
   memcpy(fc->ndvc_1_fp_cdf, default_nmv_context.comps[1].fp_cdf, sizeof(default_nmv_context.comps[1].fp_cdf));
   memcpy(fc->ndvc_1_hp_cdf, default_nmv_context.comps[1].hp_cdf, sizeof(default_nmv_context.comps[1].hp_cdf));
   memcpy(fc->ndvc_1_sign_cdf, default_nmv_context.comps[1].sign_cdf, sizeof(default_nmv_context.comps[1].sign_cdf));
}

static void
ac_vcn_vcn4_av1_default_coef_probs(void *prob, int index)
{
   rvcn_av1_vcn4_frame_context_t *fc = (rvcn_av1_vcn4_frame_context_t *)prob;
   char *p;
   int i, j;
   unsigned size;

   memcpy(fc->txb_skip_cdf, av1_default_txb_skip_cdfs[index], sizeof(av1_default_txb_skip_cdfs[index]));

   p = (char *)fc->eob_extra_cdf;
   size = sizeof(av1_default_eob_extra_cdfs[0][0][0][0]) * EOB_COEF_CONTEXTS_VCN4;
   for (i = 0; i < AV1_TX_SIZES; i++) {
      for (j = 0; j < AV1_PLANE_TYPES; j++) {
         memcpy(p, &av1_default_eob_extra_cdfs[index][i][j][3], size);
         p += size;
      }
   }

   memcpy(fc->dc_sign_cdf, av1_default_dc_sign_cdfs[index], sizeof(av1_default_dc_sign_cdfs[index]));
   memcpy(fc->coeff_br_cdf, av1_default_coeff_lps_multi_cdfs[index], sizeof(av1_default_coeff_lps_multi_cdfs[index]));
   memcpy(fc->coeff_base_cdf, av1_default_coeff_base_multi_cdfs[index],
          sizeof(av1_default_coeff_base_multi_cdfs[index]));
   memcpy(fc->coeff_base_eob_cdf, av1_default_coeff_base_eob_multi_cdfs[index],
          sizeof(av1_default_coeff_base_eob_multi_cdfs[index]));
   memcpy(fc->eob_flag_cdf16, av1_default_eob_multi16_cdfs[index], sizeof(av1_default_eob_multi16_cdfs[index]));
   memcpy(fc->eob_flag_cdf32, av1_default_eob_multi32_cdfs[index], sizeof(av1_default_eob_multi32_cdfs[index]));
   memcpy(fc->eob_flag_cdf64, av1_default_eob_multi64_cdfs[index], sizeof(av1_default_eob_multi64_cdfs[index]));
   memcpy(fc->eob_flag_cdf128, av1_default_eob_multi128_cdfs[index], sizeof(av1_default_eob_multi128_cdfs[index]));
   memcpy(fc->eob_flag_cdf256, av1_default_eob_multi256_cdfs[index], sizeof(av1_default_eob_multi256_cdfs[index]));
   memcpy(fc->eob_flag_cdf512, av1_default_eob_multi512_cdfs[index], sizeof(av1_default_eob_multi512_cdfs[index]));
   memcpy(fc->eob_flag_cdf1024, av1_default_eob_multi1024_cdfs[index], sizeof(av1_default_eob_multi1024_cdfs[index]));
}

void
ac_vcn_av1_init_probs(unsigned av1_version, uint8_t *prob)
{
   unsigned frame_ctxt_size = ac_vcn_dec_frame_ctx_size_av1(av1_version);
   if (av1_version == RDECODE_AV1_VER_0) {
      for (unsigned i = 0; i < 4; ++i) {
         ac_vcn_av1_init_mode_probs((void *)(prob + i * frame_ctxt_size));
         ac_vcn_av1_init_mv_probs((void *)(prob + i * frame_ctxt_size));
         ac_vcn_av1_default_coef_probs((void *)(prob + i * frame_ctxt_size), i);
      }
   } else {
      for (unsigned i = 0; i < 4; ++i) {
         ac_vcn_vcn4_av1_init_mode_probs((void *)(prob + i * frame_ctxt_size));
         ac_vcn_vcn4_av1_init_mv_probs((void *)(prob + i * frame_ctxt_size));
         ac_vcn_vcn4_av1_default_coef_probs((void *)(prob + i * frame_ctxt_size), i);
      }
   }

}

#define LUMA_BLOCK_SIZE_Y   73
#define LUMA_BLOCK_SIZE_X   82
#define CHROMA_BLOCK_SIZE_Y 38
#define CHROMA_BLOCK_SIZE_X 44

static int32_t
radv_vcn_av1_film_grain_random_number(unsigned short *seed, int32_t bits)
{
   unsigned short bit;
   unsigned short value = *seed;

   bit = ((value >> 0) ^ (value >> 1) ^ (value >> 3) ^ (value >> 12)) & 1;
   value = (value >> 1) | (bit << 15);
   *seed = value;

   return (value >> (16 - bits)) & ((1 << bits) - 1);
}

static void
radv_vcn_av1_film_grain_init_scaling(uint8_t scaling_points[][2], uint8_t num, short scaling_lut[])
{
   int32_t i, x, delta_x, delta_y;
   int64_t delta;

   if (num == 0)
      return;

   for (i = 0; i < scaling_points[0][0]; i++)
      scaling_lut[i] = scaling_points[0][1];

   for (i = 0; i < num - 1; i++) {
      delta_y = scaling_points[i + 1][1] - scaling_points[i][1];
      delta_x = scaling_points[i + 1][0] - scaling_points[i][0];

      delta = delta_y * ((65536 + (delta_x >> 1)) / delta_x);

      for (x = 0; x < delta_x; x++)
         scaling_lut[scaling_points[i][0] + x] = (short)(scaling_points[i][1] + (int32_t)((x * delta + 32768) >> 16));
   }

   for (i = scaling_points[num - 1][0]; i < 256; i++)
      scaling_lut[i] = scaling_points[num - 1][1];
}

void
ac_vcn_av1_init_film_grain_buffer(rvcn_dec_film_grain_params_t *fg_params, rvcn_dec_av1_fg_init_buf_t *fg_buf)
{
   const int32_t luma_block_size_y = LUMA_BLOCK_SIZE_Y;
   const int32_t luma_block_size_x = LUMA_BLOCK_SIZE_X;
   const int32_t chroma_block_size_y = CHROMA_BLOCK_SIZE_Y;
   const int32_t chroma_block_size_x = CHROMA_BLOCK_SIZE_X;
   const int32_t gauss_bits = 11;
   int32_t filt_luma_grain_block[LUMA_BLOCK_SIZE_Y][LUMA_BLOCK_SIZE_X];
   int32_t filt_cb_grain_block[CHROMA_BLOCK_SIZE_Y][CHROMA_BLOCK_SIZE_X];
   int32_t filt_cr_grain_block[CHROMA_BLOCK_SIZE_Y][CHROMA_BLOCK_SIZE_X];
   int32_t chroma_subsamp_y = 1;
   int32_t chroma_subsamp_x = 1;
   unsigned short seed = fg_params->random_seed;
   int32_t ar_coeff_lag = fg_params->ar_coeff_lag;
   int32_t bit_depth = fg_params->bit_depth_minus_8 + 8;
   short grain_center = 128 << (bit_depth - 8);
   short grain_min = 0 - grain_center;
   short grain_max = (256 << (bit_depth - 8)) - 1 - grain_center;
   int32_t shift = 12 - bit_depth + fg_params->grain_scale_shift;
   short luma_grain_block_tmp[64][80];
   short cb_grain_block_tmp[32][40];
   short cr_grain_block_tmp[32][40];
   short *align_ptr, *align_ptr0, *align_ptr1;
   int32_t x, y, g, i, j, c, c0, c1, delta_row, delta_col;
   int32_t s, s0, s1, pos, r;

   /* generate luma grain block */
   memset(filt_luma_grain_block, 0, sizeof(filt_luma_grain_block));
   for (y = 0; y < luma_block_size_y; y++) {
      for (x = 0; x < luma_block_size_x; x++) {
         g = 0;
         if (fg_params->num_y_points > 0) {
            r = radv_vcn_av1_film_grain_random_number(&seed, gauss_bits);
            g = gaussian_sequence[CLAMP(r, 0, 2048 - 1)];
         }
         filt_luma_grain_block[y][x] = ROUND_POWER_OF_TWO(g, shift);
      }
   }

   for (y = 3; y < luma_block_size_y; y++) {
      for (x = 3; x < luma_block_size_x - 3; x++) {
         s = 0;
         pos = 0;
         for (delta_row = -ar_coeff_lag; delta_row <= 0; delta_row++) {
            for (delta_col = -ar_coeff_lag; delta_col <= ar_coeff_lag; delta_col++) {
               if (delta_row == 0 && delta_col == 0)
                  break;
               c = fg_params->ar_coeffs_y[pos];
               s += filt_luma_grain_block[y + delta_row][x + delta_col] * c;
               pos++;
            }
         }
         filt_luma_grain_block[y][x] = AV1_CLAMP(
            filt_luma_grain_block[y][x] + ROUND_POWER_OF_TWO(s, fg_params->ar_coeff_shift), grain_min, grain_max);
      }
   }

   /* generate chroma grain block */
   memset(filt_cb_grain_block, 0, sizeof(filt_cb_grain_block));
   shift = 12 - bit_depth + fg_params->grain_scale_shift;
   seed = fg_params->random_seed ^ 0xb524;
   for (y = 0; y < chroma_block_size_y; y++) {
      for (x = 0; x < chroma_block_size_x; x++) {
         g = 0;
         if (fg_params->num_cb_points || fg_params->chroma_scaling_from_luma) {
            r = radv_vcn_av1_film_grain_random_number(&seed, gauss_bits);
            g = gaussian_sequence[CLAMP(r, 0, 2048 - 1)];
         }
         filt_cb_grain_block[y][x] = ROUND_POWER_OF_TWO(g, shift);
      }
   }

   memset(filt_cr_grain_block, 0, sizeof(filt_cr_grain_block));
   seed = fg_params->random_seed ^ 0x49d8;
   for (y = 0; y < chroma_block_size_y; y++) {
      for (x = 0; x < chroma_block_size_x; x++) {
         g = 0;
         if (fg_params->num_cr_points || fg_params->chroma_scaling_from_luma) {
            r = radv_vcn_av1_film_grain_random_number(&seed, gauss_bits);
            g = gaussian_sequence[CLAMP(r, 0, 2048 - 1)];
         }
         filt_cr_grain_block[y][x] = ROUND_POWER_OF_TWO(g, shift);
      }
   }

   for (y = 3; y < chroma_block_size_y; y++) {
      for (x = 3; x < chroma_block_size_x - 3; x++) {
         s0 = 0, s1 = 0, pos = 0;
         for (delta_row = -ar_coeff_lag; delta_row <= 0; delta_row++) {
            for (delta_col = -ar_coeff_lag; delta_col <= ar_coeff_lag; delta_col++) {
               c0 = fg_params->ar_coeffs_cb[pos];
               c1 = fg_params->ar_coeffs_cr[pos];
               if (delta_row == 0 && delta_col == 0) {
                  if (fg_params->num_y_points > 0) {
                     int luma = 0;
                     int luma_x = ((x - 3) << chroma_subsamp_x) + 3;
                     int luma_y = ((y - 3) << chroma_subsamp_y) + 3;
                     for (i = 0; i <= chroma_subsamp_y; i++)
                        for (j = 0; j <= chroma_subsamp_x; j++)
                           luma += filt_luma_grain_block[luma_y + i][luma_x + j];

                     luma = ROUND_POWER_OF_TWO(luma, chroma_subsamp_x + chroma_subsamp_y);
                     s0 += luma * c0;
                     s1 += luma * c1;
                  }
                  break;
               }
               s0 += filt_cb_grain_block[y + delta_row][x + delta_col] * c0;
               s1 += filt_cr_grain_block[y + delta_row][x + delta_col] * c1;
               pos++;
            }
         }
         filt_cb_grain_block[y][x] = AV1_CLAMP(
            filt_cb_grain_block[y][x] + ROUND_POWER_OF_TWO(s0, fg_params->ar_coeff_shift), grain_min, grain_max);
         filt_cr_grain_block[y][x] = AV1_CLAMP(
            filt_cr_grain_block[y][x] + ROUND_POWER_OF_TWO(s1, fg_params->ar_coeff_shift), grain_min, grain_max);
      }
   }

   for (i = 9; i < luma_block_size_y; i++)
      for (j = 9; j < luma_block_size_x; j++)
         luma_grain_block_tmp[i - 9][j - 9] = filt_luma_grain_block[i][j];

   for (i = 6; i < chroma_block_size_y; i++)
      for (j = 6; j < chroma_block_size_x; j++) {
         cb_grain_block_tmp[i - 6][j - 6] = filt_cb_grain_block[i][j];
         cr_grain_block_tmp[i - 6][j - 6] = filt_cr_grain_block[i][j];
      }

   align_ptr = &fg_buf->luma_grain_block[0][0];
   for (i = 0; i < 64; i++) {
      for (j = 0; j < 80; j++)
         *align_ptr++ = luma_grain_block_tmp[i][j];

      if (((i + 1) % 4) == 0)
         align_ptr += 64;
   }

   align_ptr0 = &fg_buf->cb_grain_block[0][0];
   align_ptr1 = &fg_buf->cr_grain_block[0][0];
   for (i = 0; i < 32; i++) {
      for (j = 0; j < 40; j++) {
         *align_ptr0++ = cb_grain_block_tmp[i][j];
         *align_ptr1++ = cr_grain_block_tmp[i][j];
      }
      if (((i + 1) % 8) == 0) {
         align_ptr0 += 64;
         align_ptr1 += 64;
      }
   }

   memset(fg_buf->scaling_lut_y, 0, sizeof(fg_buf->scaling_lut_y));
   radv_vcn_av1_film_grain_init_scaling(fg_params->scaling_points_y, fg_params->num_y_points, fg_buf->scaling_lut_y);
   if (fg_params->chroma_scaling_from_luma) {
      memcpy(fg_buf->scaling_lut_cb, fg_buf->scaling_lut_y, sizeof(fg_buf->scaling_lut_y));
      memcpy(fg_buf->scaling_lut_cr, fg_buf->scaling_lut_y, sizeof(fg_buf->scaling_lut_y));
   } else {
      memset(fg_buf->scaling_lut_cb, 0, sizeof(fg_buf->scaling_lut_cb));
      memset(fg_buf->scaling_lut_cr, 0, sizeof(fg_buf->scaling_lut_cr));
      radv_vcn_av1_film_grain_init_scaling(fg_params->scaling_points_cb, fg_params->num_cb_points,
                                           fg_buf->scaling_lut_cb);
      radv_vcn_av1_film_grain_init_scaling(fg_params->scaling_points_cr, fg_params->num_cr_points,
                                           fg_buf->scaling_lut_cr);
   }
}
