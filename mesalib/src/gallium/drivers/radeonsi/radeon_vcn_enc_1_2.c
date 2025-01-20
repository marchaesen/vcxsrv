/**************************************************************************
 *
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#include "pipe/p_video_codec.h"
#include "radeon_vcn_enc.h"
#include "radeon_video.h"
#include "si_pipe.h"
#include "util/u_video.h"

#include <stdio.h>

#define RENCODE_FW_INTERFACE_MAJOR_VERSION 1
#define RENCODE_FW_INTERFACE_MINOR_VERSION 9

static void radeon_enc_session_info(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.session_info);
   RADEON_ENC_CS(enc->enc_pic.session_info.interface_version);
   RADEON_ENC_READWRITE(enc->si->res->buf, enc->si->res->domains, 0x0);
   RADEON_ENC_CS(RENCODE_ENGINE_TYPE_ENCODE);
   RADEON_ENC_END();
}

static void radeon_enc_task_info(struct radeon_encoder *enc, bool need_feedback)
{
   enc->enc_pic.task_info.task_id++;

   if (need_feedback)
      enc->enc_pic.task_info.allowed_max_num_feedbacks = 1;
   else
      enc->enc_pic.task_info.allowed_max_num_feedbacks = 0;

   RADEON_ENC_BEGIN(enc->cmd.task_info);
   enc->p_task_size = &enc->cs.current.buf[enc->cs.current.cdw++];
   RADEON_ENC_CS(enc->enc_pic.task_info.task_id);
   RADEON_ENC_CS(enc->enc_pic.task_info.allowed_max_num_feedbacks);
   RADEON_ENC_END();
}

static void radeon_enc_session_init(struct radeon_encoder *enc)
{
   enc->enc_pic.session_init.display_remote = 0;
   enc->enc_pic.session_init.pre_encode_mode = enc->enc_pic.quality_modes.pre_encode_mode;
   enc->enc_pic.session_init.pre_encode_chroma_enabled = !!(enc->enc_pic.quality_modes.pre_encode_mode);

   RADEON_ENC_BEGIN(enc->cmd.session_init);
   RADEON_ENC_CS(enc->enc_pic.session_init.encode_standard);
   RADEON_ENC_CS(enc->enc_pic.session_init.aligned_picture_width);
   RADEON_ENC_CS(enc->enc_pic.session_init.aligned_picture_height);
   RADEON_ENC_CS(enc->enc_pic.session_init.padding_width);
   RADEON_ENC_CS(enc->enc_pic.session_init.padding_height);
   RADEON_ENC_CS(enc->enc_pic.session_init.pre_encode_mode);
   RADEON_ENC_CS(enc->enc_pic.session_init.pre_encode_chroma_enabled);
   RADEON_ENC_CS(enc->enc_pic.session_init.display_remote);
   RADEON_ENC_END();
}

static void radeon_enc_layer_control(struct radeon_encoder *enc)
{
   enc->enc_pic.layer_ctrl.max_num_temporal_layers = enc->enc_pic.num_temporal_layers;
   enc->enc_pic.layer_ctrl.num_temporal_layers = enc->enc_pic.num_temporal_layers;

   RADEON_ENC_BEGIN(enc->cmd.layer_control);
   RADEON_ENC_CS(enc->enc_pic.layer_ctrl.max_num_temporal_layers);
   RADEON_ENC_CS(enc->enc_pic.layer_ctrl.num_temporal_layers);
   RADEON_ENC_END();
}

static void radeon_enc_layer_select(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.layer_select);
   RADEON_ENC_CS(enc->enc_pic.layer_sel.temporal_layer_index);
   RADEON_ENC_END();
}

static void radeon_enc_slice_control(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.slice_control_h264);
   RADEON_ENC_CS(enc->enc_pic.slice_ctrl.slice_control_mode);
   RADEON_ENC_CS(enc->enc_pic.slice_ctrl.num_mbs_per_slice);
   RADEON_ENC_END();
}

static void radeon_enc_slice_control_hevc(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.slice_control_hevc);
   RADEON_ENC_CS(enc->enc_pic.hevc_slice_ctrl.slice_control_mode);
   RADEON_ENC_CS(enc->enc_pic.hevc_slice_ctrl.fixed_ctbs_per_slice.num_ctbs_per_slice);
   RADEON_ENC_CS(enc->enc_pic.hevc_slice_ctrl.fixed_ctbs_per_slice.num_ctbs_per_slice_segment);
   RADEON_ENC_END();
}

static void radeon_enc_spec_misc(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.spec_misc_h264);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.constrained_intra_pred_flag);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.cabac_enable);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.cabac_init_idc);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.half_pel_enabled);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.quarter_pel_enabled);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.profile_idc);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.level_idc);
   RADEON_ENC_END();
}

static void radeon_enc_spec_misc_hevc(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.spec_misc_hevc);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.log2_min_luma_coding_block_size_minus3);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.amp_disabled);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.strong_intra_smoothing_enabled);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.constrained_intra_pred_flag);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.cabac_init_flag);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.half_pel_enabled);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.quarter_pel_enabled);
   RADEON_ENC_END();
}

static void radeon_enc_rc_session_init(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.rc_session_init);
   RADEON_ENC_CS(enc->enc_pic.rc_session_init.rate_control_method);
   RADEON_ENC_CS(enc->enc_pic.rc_session_init.vbv_buffer_level);
   RADEON_ENC_END();
}

static void radeon_enc_rc_layer_init(struct radeon_encoder *enc)
{
   unsigned int i = enc->enc_pic.layer_sel.temporal_layer_index;
   RADEON_ENC_BEGIN(enc->cmd.rc_layer_init);
   RADEON_ENC_CS(enc->enc_pic.rc_layer_init[i].target_bit_rate);
   RADEON_ENC_CS(enc->enc_pic.rc_layer_init[i].peak_bit_rate);
   RADEON_ENC_CS(enc->enc_pic.rc_layer_init[i].frame_rate_num);
   RADEON_ENC_CS(enc->enc_pic.rc_layer_init[i].frame_rate_den);
   RADEON_ENC_CS(enc->enc_pic.rc_layer_init[i].vbv_buffer_size);
   RADEON_ENC_CS(enc->enc_pic.rc_layer_init[i].avg_target_bits_per_picture);
   RADEON_ENC_CS(enc->enc_pic.rc_layer_init[i].peak_bits_per_picture_integer);
   RADEON_ENC_CS(enc->enc_pic.rc_layer_init[i].peak_bits_per_picture_fractional);
   RADEON_ENC_END();
}

static void radeon_enc_deblocking_filter_h264(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.deblocking_filter_h264);
   RADEON_ENC_CS(enc->enc_pic.h264_deblock.disable_deblocking_filter_idc);
   RADEON_ENC_CS(enc->enc_pic.h264_deblock.alpha_c0_offset_div2);
   RADEON_ENC_CS(enc->enc_pic.h264_deblock.beta_offset_div2);
   RADEON_ENC_CS(enc->enc_pic.h264_deblock.cb_qp_offset);
   RADEON_ENC_CS(enc->enc_pic.h264_deblock.cr_qp_offset);
   RADEON_ENC_END();
}

static void radeon_enc_deblocking_filter_hevc(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.deblocking_filter_hevc);
   RADEON_ENC_CS(enc->enc_pic.hevc_deblock.loop_filter_across_slices_enabled);
   RADEON_ENC_CS(enc->enc_pic.hevc_deblock.deblocking_filter_disabled);
   RADEON_ENC_CS(enc->enc_pic.hevc_deblock.beta_offset_div2);
   RADEON_ENC_CS(enc->enc_pic.hevc_deblock.tc_offset_div2);
   RADEON_ENC_CS(enc->enc_pic.hevc_deblock.cb_qp_offset);
   RADEON_ENC_CS(enc->enc_pic.hevc_deblock.cr_qp_offset);
   RADEON_ENC_END();
}

static void radeon_enc_quality_params(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.quality_params);
   RADEON_ENC_CS(enc->enc_pic.quality_params.vbaq_mode);
   RADEON_ENC_CS(enc->enc_pic.quality_params.scene_change_sensitivity);
   RADEON_ENC_CS(enc->enc_pic.quality_params.scene_change_min_idr_interval);
   RADEON_ENC_CS(enc->enc_pic.quality_params.two_pass_search_center_map_mode);
   RADEON_ENC_END();
}

unsigned int radeon_enc_write_sps(struct radeon_encoder *enc, uint8_t nal_byte, uint8_t *out)
{
   struct radeon_bitstream bs;
   struct radeon_enc_pic *pic = &enc->enc_pic;
   struct pipe_h264_enc_seq_param *sps = &pic->h264.desc->seq;

   radeon_bs_reset(&bs, out, NULL);
   radeon_bs_set_emulation_prevention(&bs, false);
   radeon_bs_code_fixed_bits(&bs, 0x00000001, 32);
   radeon_bs_code_fixed_bits(&bs, nal_byte, 8);
   radeon_bs_set_emulation_prevention(&bs, true);
   radeon_bs_code_fixed_bits(&bs, pic->spec_misc.profile_idc, 8);
   radeon_bs_code_fixed_bits(&bs, sps->enc_constraint_set_flags, 6);
   radeon_bs_code_fixed_bits(&bs, 0x0, 2); /* reserved_zero_2bits */
   radeon_bs_code_fixed_bits(&bs, pic->spec_misc.level_idc, 8);
   radeon_bs_code_ue(&bs, 0x0); /* seq_parameter_set_id */

   if (pic->spec_misc.profile_idc == 100 || pic->spec_misc.profile_idc == 110 ||
       pic->spec_misc.profile_idc == 122 || pic->spec_misc.profile_idc == 244 ||
       pic->spec_misc.profile_idc == 44  || pic->spec_misc.profile_idc == 83 ||
       pic->spec_misc.profile_idc == 86  || pic->spec_misc.profile_idc == 118 ||
       pic->spec_misc.profile_idc == 128 || pic->spec_misc.profile_idc == 138) {
      radeon_bs_code_ue(&bs, 0x1); /* chroma_format_idc */
      radeon_bs_code_ue(&bs, 0x0); /* bit_depth_luma_minus8 */
      radeon_bs_code_ue(&bs, 0x0); /* bit_depth_chroma_minus8 */
      radeon_bs_code_fixed_bits(&bs, 0x0, 2); /* qpprime_y_zero_transform_bypass_flag + seq_scaling_matrix_present_flag */
   }

   radeon_bs_code_ue(&bs, sps->log2_max_frame_num_minus4);
   radeon_bs_code_ue(&bs, sps->pic_order_cnt_type);

   if (sps->pic_order_cnt_type == 0)
      radeon_bs_code_ue(&bs, sps->log2_max_pic_order_cnt_lsb_minus4);

   radeon_bs_code_ue(&bs, sps->max_num_ref_frames);
   radeon_bs_code_fixed_bits(&bs, sps->gaps_in_frame_num_value_allowed_flag, 1);
   radeon_bs_code_ue(&bs, (pic->session_init.aligned_picture_width / 16 - 1));
   radeon_bs_code_ue(&bs, (pic->session_init.aligned_picture_height / 16 - 1));
   radeon_bs_code_fixed_bits(&bs, 0x1, 1); /* frame_mbs_only_flag */
   radeon_bs_code_fixed_bits(&bs, 0x1, 1); /* direct_8x8_inference_flag */

   radeon_bs_code_fixed_bits(&bs, sps->enc_frame_cropping_flag, 1);
   if (sps->enc_frame_cropping_flag) {
      radeon_bs_code_ue(&bs, sps->enc_frame_crop_left_offset);
      radeon_bs_code_ue(&bs, sps->enc_frame_crop_right_offset);
      radeon_bs_code_ue(&bs, sps->enc_frame_crop_top_offset);
      radeon_bs_code_ue(&bs, sps->enc_frame_crop_bottom_offset);
   }

   radeon_bs_code_fixed_bits(&bs, sps->vui_parameters_present_flag, 1);
   if (sps->vui_parameters_present_flag) {
      radeon_bs_code_fixed_bits(&bs, (sps->vui_flags.aspect_ratio_info_present_flag), 1);
      if (sps->vui_flags.aspect_ratio_info_present_flag) {
         radeon_bs_code_fixed_bits(&bs, (sps->aspect_ratio_idc), 8);
         if (sps->aspect_ratio_idc == PIPE_H2645_EXTENDED_SAR) {
            radeon_bs_code_fixed_bits(&bs, (sps->sar_width), 16);
            radeon_bs_code_fixed_bits(&bs, (sps->sar_height), 16);
         }
      }
      radeon_bs_code_fixed_bits(&bs, sps->vui_flags.overscan_info_present_flag, 1);
      if (sps->vui_flags.overscan_info_present_flag)
         radeon_bs_code_fixed_bits(&bs, sps->vui_flags.overscan_appropriate_flag, 1);
      radeon_bs_code_fixed_bits(&bs, sps->vui_flags.video_signal_type_present_flag, 1);
      if (sps->vui_flags.video_signal_type_present_flag) {
         radeon_bs_code_fixed_bits(&bs, sps->video_format, 3);
         radeon_bs_code_fixed_bits(&bs, sps->video_full_range_flag, 1);
         radeon_bs_code_fixed_bits(&bs, sps->vui_flags.colour_description_present_flag, 1);
         if (sps->vui_flags.colour_description_present_flag) {
            radeon_bs_code_fixed_bits(&bs, sps->colour_primaries, 8);
            radeon_bs_code_fixed_bits(&bs, sps->transfer_characteristics, 8);
            radeon_bs_code_fixed_bits(&bs, sps->matrix_coefficients, 8);
         }
      }
      radeon_bs_code_fixed_bits(&bs, sps->vui_flags.chroma_loc_info_present_flag, 1);
      if (sps->vui_flags.chroma_loc_info_present_flag) {
         radeon_bs_code_ue(&bs, sps->chroma_sample_loc_type_top_field);
         radeon_bs_code_ue(&bs, sps->chroma_sample_loc_type_bottom_field);
      }
      radeon_bs_code_fixed_bits(&bs, (sps->vui_flags.timing_info_present_flag), 1);
      if (sps->vui_flags.timing_info_present_flag) {
         radeon_bs_code_fixed_bits(&bs, (sps->num_units_in_tick), 32);
         radeon_bs_code_fixed_bits(&bs, (sps->time_scale), 32);
         radeon_bs_code_fixed_bits(&bs, (sps->vui_flags.fixed_frame_rate_flag), 1);
      }
      radeon_bs_code_fixed_bits(&bs, sps->vui_flags.nal_hrd_parameters_present_flag, 1);
      if (sps->vui_flags.nal_hrd_parameters_present_flag)
         radeon_bs_h264_hrd_parameters(&bs, &sps->nal_hrd_parameters);
      radeon_bs_code_fixed_bits(&bs, sps->vui_flags.vcl_hrd_parameters_present_flag, 1);
      if (sps->vui_flags.vcl_hrd_parameters_present_flag)
         radeon_bs_h264_hrd_parameters(&bs, &sps->vcl_hrd_parameters);
      if (sps->vui_flags.nal_hrd_parameters_present_flag || sps->vui_flags.vcl_hrd_parameters_present_flag)
         radeon_bs_code_fixed_bits(&bs, sps->vui_flags.low_delay_hrd_flag, 1);
      radeon_bs_code_fixed_bits(&bs, sps->vui_flags.pic_struct_present_flag, 1);
      radeon_bs_code_fixed_bits(&bs, sps->vui_flags.bitstream_restriction_flag, 1);
      if (sps->vui_flags.bitstream_restriction_flag) {
         radeon_bs_code_fixed_bits(&bs, 0x1, 1); /* motion_vectors_over_pic_boundaries_flag */
         radeon_bs_code_ue(&bs, 0x0); /* max_bytes_per_pic_denom */
         radeon_bs_code_ue(&bs, 0x0); /* max_bits_per_mb_denom */
         radeon_bs_code_ue(&bs, 16); /* log2_max_mv_length_horizontal */
         radeon_bs_code_ue(&bs, 16); /* log2_max_mv_length_vertical */
         radeon_bs_code_ue(&bs, sps->max_num_reorder_frames);
         radeon_bs_code_ue(&bs, sps->max_dec_frame_buffering);
      }
   }

   radeon_bs_code_fixed_bits(&bs, 0x1, 1);
   radeon_bs_byte_align(&bs);

   return bs.bits_output / 8;
}

unsigned int radeon_enc_write_sps_hevc(struct radeon_encoder *enc, uint8_t *out)
{
   struct radeon_bitstream bs;
   struct radeon_enc_pic *pic = &enc->enc_pic;
   struct pipe_h265_enc_seq_param *sps = &pic->hevc.desc->seq;
   int i;

   radeon_bs_reset(&bs, out, NULL);
   radeon_bs_set_emulation_prevention(&bs, false);
   radeon_bs_code_fixed_bits(&bs, 0x00000001, 32);
   radeon_bs_code_fixed_bits(&bs, 0x4201, 16);
   radeon_bs_set_emulation_prevention(&bs, true);
   radeon_bs_code_fixed_bits(&bs, 0x0, 4); /* sps_video_parameter_set_id */
   radeon_bs_code_fixed_bits(&bs, sps->sps_max_sub_layers_minus1, 3);
   radeon_bs_code_fixed_bits(&bs, sps->sps_temporal_id_nesting_flag, 1);
   radeon_bs_hevc_profile_tier_level(&bs, sps->sps_max_sub_layers_minus1, &sps->profile_tier_level);
   radeon_bs_code_ue(&bs, 0x0); /* sps_seq_parameter_set_id */
   radeon_bs_code_ue(&bs, sps->chroma_format_idc);
   radeon_bs_code_ue(&bs, pic->session_init.aligned_picture_width);
   radeon_bs_code_ue(&bs, pic->session_init.aligned_picture_height);

   radeon_bs_code_fixed_bits(&bs, sps->conformance_window_flag, 1);
   if (sps->conformance_window_flag) {
      radeon_bs_code_ue(&bs, sps->conf_win_left_offset);
      radeon_bs_code_ue(&bs, sps->conf_win_right_offset);
      radeon_bs_code_ue(&bs, sps->conf_win_top_offset);
      radeon_bs_code_ue(&bs, sps->conf_win_bottom_offset);
   }

   radeon_bs_code_ue(&bs, sps->bit_depth_luma_minus8);
   radeon_bs_code_ue(&bs, sps->bit_depth_chroma_minus8);
   radeon_bs_code_ue(&bs, sps->log2_max_pic_order_cnt_lsb_minus4);
   radeon_bs_code_fixed_bits(&bs, sps->sps_sub_layer_ordering_info_present_flag, 1);
   i = sps->sps_sub_layer_ordering_info_present_flag ? 0 : sps->sps_max_sub_layers_minus1;
   for (; i <= sps->sps_max_sub_layers_minus1; i++) {
      radeon_bs_code_ue(&bs, sps->sps_max_dec_pic_buffering_minus1[i]);
      radeon_bs_code_ue(&bs, sps->sps_max_num_reorder_pics[i]);
      radeon_bs_code_ue(&bs, sps->sps_max_latency_increase_plus1[i]);
   }

   unsigned log2_diff_max_min_luma_coding_block_size =
      6 - (enc->enc_pic.hevc_spec_misc.log2_min_luma_coding_block_size_minus3 + 3);
   unsigned log2_min_transform_block_size_minus2 =
      enc->enc_pic.hevc_spec_misc.log2_min_luma_coding_block_size_minus3;
   unsigned log2_diff_max_min_transform_block_size = log2_diff_max_min_luma_coding_block_size;
   unsigned max_transform_hierarchy_depth_inter = log2_diff_max_min_luma_coding_block_size + 1;
   unsigned max_transform_hierarchy_depth_intra = max_transform_hierarchy_depth_inter;

   radeon_bs_code_ue(&bs, pic->hevc_spec_misc.log2_min_luma_coding_block_size_minus3);
   radeon_bs_code_ue(&bs, log2_diff_max_min_luma_coding_block_size);
   radeon_bs_code_ue(&bs, log2_min_transform_block_size_minus2);
   radeon_bs_code_ue(&bs, log2_diff_max_min_transform_block_size);
   radeon_bs_code_ue(&bs, max_transform_hierarchy_depth_inter);
   radeon_bs_code_ue(&bs, max_transform_hierarchy_depth_intra);

   radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* scaling_list_enabled_flag */
   radeon_bs_code_fixed_bits(&bs, !pic->hevc_spec_misc.amp_disabled, 1);
   radeon_bs_code_fixed_bits(&bs, !pic->hevc_deblock.disable_sao, 1);
   radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* pcm_enabled_flag */

   radeon_bs_code_ue(&bs, sps->num_short_term_ref_pic_sets);
   for (i = 0; i < sps->num_short_term_ref_pic_sets; i++)
      radeon_bs_hevc_st_ref_pic_set(&bs, i, sps->num_short_term_ref_pic_sets, sps->st_ref_pic_set);

   radeon_bs_code_fixed_bits(&bs, sps->long_term_ref_pics_present_flag, 1);
   if (sps->long_term_ref_pics_present_flag) {
      radeon_bs_code_ue(&bs, sps->num_long_term_ref_pics_sps);
      for (i = 0; i < sps->num_long_term_ref_pics_sps; i++) {
         radeon_bs_code_fixed_bits(&bs, sps->lt_ref_pic_poc_lsb_sps[i], sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
         radeon_bs_code_fixed_bits(&bs, sps->used_by_curr_pic_lt_sps_flag[i], 1);
      }
   }

   radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* sps_temporal_mvp_enabled_flag */
   radeon_bs_code_fixed_bits(&bs, pic->hevc_spec_misc.strong_intra_smoothing_enabled, 1);

   /* VUI parameters present flag */
   radeon_bs_code_fixed_bits(&bs, (sps->vui_parameters_present_flag), 1);
   if (sps->vui_parameters_present_flag) {
      /* aspect ratio present flag */
      radeon_bs_code_fixed_bits(&bs, (sps->vui_flags.aspect_ratio_info_present_flag), 1);
      if (sps->vui_flags.aspect_ratio_info_present_flag) {
         radeon_bs_code_fixed_bits(&bs, (sps->aspect_ratio_idc), 8);
         if (sps->aspect_ratio_idc == PIPE_H2645_EXTENDED_SAR) {
            radeon_bs_code_fixed_bits(&bs, (sps->sar_width), 16);
            radeon_bs_code_fixed_bits(&bs, (sps->sar_height), 16);
         }
      }
      radeon_bs_code_fixed_bits(&bs, sps->vui_flags.overscan_info_present_flag, 1);
      if (sps->vui_flags.overscan_info_present_flag)
         radeon_bs_code_fixed_bits(&bs, sps->vui_flags.overscan_appropriate_flag, 1);
      /* video signal type present flag  */
      radeon_bs_code_fixed_bits(&bs, sps->vui_flags.video_signal_type_present_flag, 1);
      if (sps->vui_flags.video_signal_type_present_flag) {
         radeon_bs_code_fixed_bits(&bs, sps->video_format, 3);
         radeon_bs_code_fixed_bits(&bs, sps->video_full_range_flag, 1);
         radeon_bs_code_fixed_bits(&bs, sps->vui_flags.colour_description_present_flag, 1);
         if (sps->vui_flags.colour_description_present_flag) {
            radeon_bs_code_fixed_bits(&bs, sps->colour_primaries, 8);
            radeon_bs_code_fixed_bits(&bs, sps->transfer_characteristics, 8);
            radeon_bs_code_fixed_bits(&bs, sps->matrix_coefficients, 8);
         }
      }
      /* chroma loc info present flag */
      radeon_bs_code_fixed_bits(&bs, sps->vui_flags.chroma_loc_info_present_flag, 1);
      if (sps->vui_flags.chroma_loc_info_present_flag) {
         radeon_bs_code_ue(&bs, sps->chroma_sample_loc_type_top_field);
         radeon_bs_code_ue(&bs, sps->chroma_sample_loc_type_bottom_field);
      }
      radeon_bs_code_fixed_bits(&bs, 0x0, 1);  /* neutral chroma indication flag */
      radeon_bs_code_fixed_bits(&bs, 0x0, 1);  /* field seq flag */
      radeon_bs_code_fixed_bits(&bs, 0x0, 1);  /* frame field info present flag */
      radeon_bs_code_fixed_bits(&bs, 0x0, 1);  /* default display windows flag */
      /* vui timing info present flag */
      radeon_bs_code_fixed_bits(&bs, (sps->vui_flags.timing_info_present_flag), 1);
      if (sps->vui_flags.timing_info_present_flag) {
         radeon_bs_code_fixed_bits(&bs, (sps->num_units_in_tick), 32);
         radeon_bs_code_fixed_bits(&bs, (sps->time_scale), 32);
         radeon_bs_code_fixed_bits(&bs, sps->vui_flags.poc_proportional_to_timing_flag, 1);
         if (sps->vui_flags.poc_proportional_to_timing_flag)
            radeon_bs_code_ue(&bs, sps->num_ticks_poc_diff_one_minus1);
         radeon_bs_code_fixed_bits(&bs, sps->vui_flags.hrd_parameters_present_flag, 1);
         if (sps->vui_flags.hrd_parameters_present_flag)
            radeon_bs_hevc_hrd_parameters(&bs, 1, sps->sps_max_sub_layers_minus1, &sps->hrd_parameters);
      }
      radeon_bs_code_fixed_bits(&bs, 0x0, 1);  /* bitstream restriction flag */
   }
   radeon_bs_code_fixed_bits(&bs, 0x0, 1);  /* sps extension present flag */

   radeon_bs_code_fixed_bits(&bs, 0x1, 1);
   radeon_bs_byte_align(&bs);

   return bs.bits_output / 8;
}

unsigned int radeon_enc_write_pps(struct radeon_encoder *enc, uint8_t nal_byte, uint8_t *out)
{
   struct radeon_bitstream bs;

   radeon_bs_reset(&bs, out, NULL);
   radeon_bs_set_emulation_prevention(&bs, false);
   radeon_bs_code_fixed_bits(&bs, 0x00000001, 32);
   radeon_bs_code_fixed_bits(&bs, nal_byte, 8);
   radeon_bs_set_emulation_prevention(&bs, true);
   radeon_bs_code_ue(&bs, 0x0); /* pic_parameter_set_id */
   radeon_bs_code_ue(&bs, 0x0); /* seq_parameter_set_id */
   radeon_bs_code_fixed_bits(&bs, (enc->enc_pic.spec_misc.cabac_enable ? 0x1 : 0x0), 1);
   radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* bottom_field_pic_order_in_frame_present_flag */
   radeon_bs_code_ue(&bs, 0x0); /* num_slice_groups_minus_1 */
   radeon_bs_code_ue(&bs, enc->enc_pic.h264.desc->pic_ctrl.num_ref_idx_l0_default_active_minus1);
   radeon_bs_code_ue(&bs, enc->enc_pic.h264.desc->pic_ctrl.num_ref_idx_l1_default_active_minus1);
   radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* weighted_pred_flag */
   radeon_bs_code_fixed_bits(&bs, 0x0, 2); /* weighted_bipred_idc */
   radeon_bs_code_se(&bs, 0x0); /* pic_init_qp_minus26 */
   radeon_bs_code_se(&bs, 0x0); /* pic_init_qs_minus26 */
   radeon_bs_code_se(&bs, enc->enc_pic.h264_deblock.cb_qp_offset); /* chroma_qp_index_offset */
   radeon_bs_code_fixed_bits(&bs, (enc->enc_pic.spec_misc.deblocking_filter_control_present_flag), 1);
   radeon_bs_code_fixed_bits(&bs, (enc->enc_pic.spec_misc.constrained_intra_pred_flag), 1);
   radeon_bs_code_fixed_bits(&bs, (enc->enc_pic.spec_misc.redundant_pic_cnt_present_flag), 1);
   radeon_bs_code_fixed_bits(&bs, (enc->enc_pic.spec_misc.transform_8x8_mode), 1);
   radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* pic_scaling_matrix_present_flag */
   radeon_bs_code_se(&bs, enc->enc_pic.h264_deblock.cr_qp_offset); /* second_chroma_qp_index_offset */

   radeon_bs_code_fixed_bits(&bs, 0x1, 1);
   radeon_bs_byte_align(&bs);

   return bs.bits_output / 8;
}

unsigned int radeon_enc_write_pps_hevc(struct radeon_encoder *enc, uint8_t *out)
{
   struct radeon_bitstream bs;
   struct pipe_h265_enc_pic_param *pps = &enc->enc_pic.hevc.desc->pic;

   radeon_bs_reset(&bs, out, NULL);
   radeon_bs_set_emulation_prevention(&bs, false);
   radeon_bs_code_fixed_bits(&bs, 0x00000001, 32);
   radeon_bs_code_fixed_bits(&bs, 0x4401, 16);
   radeon_bs_set_emulation_prevention(&bs, true);
   radeon_bs_code_ue(&bs, 0x0); /* pps_pic_parameter_set_id */
   radeon_bs_code_ue(&bs, 0x0); /* pps_seq_parameter_set_id */
   radeon_bs_code_fixed_bits(&bs, 0x1, 1); /* dependent_slice_segments_enabled_flag */
   radeon_bs_code_fixed_bits(&bs, pps->output_flag_present_flag, 1);
   radeon_bs_code_fixed_bits(&bs, 0x0, 3); /* num_extra_slice_header_bits */
   radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* sign_data_hiding_enabled_flag */
   radeon_bs_code_fixed_bits(&bs, 0x1, 1); /* cabac_init_present_flag */
   radeon_bs_code_ue(&bs, pps->num_ref_idx_l0_default_active_minus1);
   radeon_bs_code_ue(&bs, pps->num_ref_idx_l1_default_active_minus1);
   radeon_bs_code_se(&bs, 0x0); /* init_qp_minus26 */
   radeon_bs_code_fixed_bits(&bs, enc->enc_pic.hevc_spec_misc.constrained_intra_pred_flag, 1);
   radeon_bs_code_fixed_bits(&bs, !enc->enc_pic.hevc_spec_misc.transform_skip_disabled, 1);
   radeon_bs_code_fixed_bits(&bs, enc->enc_pic.hevc_spec_misc.cu_qp_delta_enabled_flag, 1);
   if (enc->enc_pic.hevc_spec_misc.cu_qp_delta_enabled_flag)
      radeon_bs_code_ue(&bs, 0); /* diff_cu_qp_delta_depth */
   radeon_bs_code_se(&bs, enc->enc_pic.hevc_deblock.cb_qp_offset);
   radeon_bs_code_se(&bs, enc->enc_pic.hevc_deblock.cr_qp_offset);
   radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* pps_slice_chroma_qp_offsets_present_flag */
   radeon_bs_code_fixed_bits(&bs, 0x0, 2); /* weighted_pred_flag + weighted_bipred_flag */
   radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* transquant_bypass_enabled_flag */
   radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* tiles_enabled_flag */
   radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* entropy_coding_sync_enabled_flag */
   radeon_bs_code_fixed_bits(&bs, enc->enc_pic.hevc_deblock.loop_filter_across_slices_enabled, 1);
   radeon_bs_code_fixed_bits(&bs, 0x1, 1); /* deblocking_filter_control_present_flag */
   radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* deblocking_filter_override_enabled_flag */
   radeon_bs_code_fixed_bits(&bs, enc->enc_pic.hevc_deblock.deblocking_filter_disabled, 1);

   if (!enc->enc_pic.hevc_deblock.deblocking_filter_disabled) {
      radeon_bs_code_se(&bs, enc->enc_pic.hevc_deblock.beta_offset_div2);
      radeon_bs_code_se(&bs, enc->enc_pic.hevc_deblock.tc_offset_div2);
   }

   radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* pps_scaling_list_data_present_flag */
   radeon_bs_code_fixed_bits(&bs, pps->lists_modification_present_flag, 1);
   radeon_bs_code_ue(&bs, pps->log2_parallel_merge_level_minus2);
   radeon_bs_code_fixed_bits(&bs, 0x0, 2);

   radeon_bs_code_fixed_bits(&bs, 0x1, 1);
   radeon_bs_byte_align(&bs);

   return bs.bits_output / 8;
}

unsigned int radeon_enc_write_vps(struct radeon_encoder *enc, uint8_t *out)
{
   struct radeon_bitstream bs;
   struct pipe_h265_enc_vid_param *vps = &enc->enc_pic.hevc.desc->vid;
   int i;

   radeon_bs_reset(&bs, out, NULL);
   radeon_bs_set_emulation_prevention(&bs, false);
   radeon_bs_code_fixed_bits(&bs, 0x00000001, 32);
   radeon_bs_code_fixed_bits(&bs, 0x4001, 16);
   radeon_bs_set_emulation_prevention(&bs, true);
   radeon_bs_code_fixed_bits(&bs, 0x0, 4); /* vps_video_parameter_set_id*/
   radeon_bs_code_fixed_bits(&bs, vps->vps_base_layer_internal_flag, 1);
   radeon_bs_code_fixed_bits(&bs, vps->vps_base_layer_available_flag, 1);
   radeon_bs_code_fixed_bits(&bs, 0x0, 6); /* vps_max_layers_minus1 */
   radeon_bs_code_fixed_bits(&bs, vps->vps_max_sub_layers_minus1, 3);
   radeon_bs_code_fixed_bits(&bs, vps->vps_temporal_id_nesting_flag, 1);
   radeon_bs_code_fixed_bits(&bs, 0xffff, 16); /* vps_reserved_0xffff_16bits */
   radeon_bs_hevc_profile_tier_level(&bs, vps->vps_max_sub_layers_minus1, &vps->profile_tier_level);
   radeon_bs_code_fixed_bits(&bs, vps->vps_sub_layer_ordering_info_present_flag, 1);
   i = vps->vps_sub_layer_ordering_info_present_flag ? 0 : vps->vps_max_sub_layers_minus1;
   for (; i <= vps->vps_max_sub_layers_minus1; i++) {
      radeon_bs_code_ue(&bs, vps->vps_max_dec_pic_buffering_minus1[i]);
      radeon_bs_code_ue(&bs, vps->vps_max_num_reorder_pics[i]);
      radeon_bs_code_ue(&bs, vps->vps_max_latency_increase_plus1[i]);
   }
   radeon_bs_code_fixed_bits(&bs, 0x0, 6); /* vps_max_layer_id */
   radeon_bs_code_ue(&bs, 0x0); /* vps_num_layer_sets_minus1 */
   radeon_bs_code_fixed_bits(&bs, vps->vps_timing_info_present_flag, 1);
   if (vps->vps_timing_info_present_flag) {
      radeon_bs_code_fixed_bits(&bs, vps->vps_num_units_in_tick, 32);
      radeon_bs_code_fixed_bits(&bs, vps->vps_time_scale, 32);
      radeon_bs_code_fixed_bits(&bs, vps->vps_poc_proportional_to_timing_flag, 1);
      if (vps->vps_poc_proportional_to_timing_flag)
         radeon_bs_code_ue(&bs, vps->vps_num_ticks_poc_diff_one_minus1);
      radeon_bs_code_ue(&bs, 0x0); /* vps_num_hrd_parameters */
   }
   radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* vps_extension_flag */

   radeon_bs_code_fixed_bits(&bs, 0x1, 1);
   radeon_bs_byte_align(&bs);

   return bs.bits_output / 8;
}

static void radeon_enc_slice_header(struct radeon_encoder *enc)
{
   struct radeon_bitstream bs;
   struct pipe_h264_enc_seq_param *sps = &enc->enc_pic.h264.desc->seq;
   struct pipe_h264_enc_pic_control *pps = &enc->enc_pic.h264.desc->pic_ctrl;
   struct pipe_h264_enc_slice_param *slice = &enc->enc_pic.h264.desc->slice;
   uint32_t instruction[RENCODE_SLICE_HEADER_TEMPLATE_MAX_NUM_INSTRUCTIONS] = {0};
   uint32_t num_bits[RENCODE_SLICE_HEADER_TEMPLATE_MAX_NUM_INSTRUCTIONS] = {0};
   unsigned int inst_index = 0;
   unsigned int cdw_start = 0;
   unsigned int cdw_filled = 0;
   unsigned int bits_copied = 0;

   RADEON_ENC_BEGIN(enc->cmd.slice_header);
   radeon_bs_reset(&bs, NULL, &enc->cs);
   radeon_bs_set_emulation_prevention(&bs, false);

   cdw_start = enc->cs.current.cdw;
   radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* forbidden_zero_bit */
   radeon_bs_code_fixed_bits(&bs, pps->nal_ref_idc, 2);
   radeon_bs_code_fixed_bits(&bs, pps->nal_unit_type, 5);

   radeon_bs_flush_headers(&bs);
   instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_COPY;
   num_bits[inst_index] = bs.bits_output - bits_copied;
   bits_copied = bs.bits_output;
   inst_index++;

   instruction[inst_index] = RENCODE_H264_HEADER_INSTRUCTION_FIRST_MB;
   inst_index++;

   switch (enc->enc_pic.picture_type) {
   case PIPE_H2645_ENC_PICTURE_TYPE_I:
   case PIPE_H2645_ENC_PICTURE_TYPE_IDR:
      radeon_bs_code_fixed_bits(&bs, 0x08, 7);
      break;
   case PIPE_H2645_ENC_PICTURE_TYPE_P:
   case PIPE_H2645_ENC_PICTURE_TYPE_SKIP:
      radeon_bs_code_fixed_bits(&bs, 0x06, 5);
      break;
   case PIPE_H2645_ENC_PICTURE_TYPE_B:
      radeon_bs_code_fixed_bits(&bs, 0x07, 5);
      break;
   default:
      radeon_bs_code_fixed_bits(&bs, 0x08, 7);
   }

   radeon_bs_code_ue(&bs, 0x0); /* pic_parameter_set_id */
   radeon_bs_code_fixed_bits(&bs, slice->frame_num, sps->log2_max_frame_num_minus4 + 4);

   if (enc->enc_pic.h264_enc_params.input_picture_structure !=
       RENCODE_H264_PICTURE_STRUCTURE_FRAME) {
      radeon_bs_code_fixed_bits(&bs, 0x1, 1);
      radeon_bs_code_fixed_bits(&bs,
                                 enc->enc_pic.h264_enc_params.input_picture_structure ==
                                       RENCODE_H264_PICTURE_STRUCTURE_BOTTOM_FIELD
                                    ? 1
                                    : 0,
                                 1);
   }

   if (enc->enc_pic.picture_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR)
      radeon_bs_code_ue(&bs, slice->idr_pic_id);

   if (sps->pic_order_cnt_type == 0)
      radeon_bs_code_fixed_bits(&bs, slice->pic_order_cnt_lsb, sps->log2_max_pic_order_cnt_lsb_minus4 + 4);

   if (pps->redundant_pic_cnt_present_flag)
      radeon_bs_code_ue(&bs, slice->redundant_pic_cnt);

   if (enc->enc_pic.picture_type == PIPE_H2645_ENC_PICTURE_TYPE_B)
      radeon_bs_code_fixed_bits(&bs, 0x1, 1); /* direct_spatial_mv_pred_flag */

   if (enc->enc_pic.picture_type == PIPE_H2645_ENC_PICTURE_TYPE_P ||
       enc->enc_pic.picture_type == PIPE_H2645_ENC_PICTURE_TYPE_B) {
      radeon_bs_code_fixed_bits(&bs, slice->num_ref_idx_active_override_flag, 1);
      if (slice->num_ref_idx_active_override_flag) {
         radeon_bs_code_ue(&bs, slice->num_ref_idx_l0_active_minus1);
         if (enc->enc_pic.picture_type == PIPE_H2645_ENC_PICTURE_TYPE_B)
            radeon_bs_code_ue(&bs, slice->num_ref_idx_l1_active_minus1);
      }
      radeon_bs_code_fixed_bits(&bs, slice->ref_pic_list_modification_flag_l0, 1);
      if (slice->ref_pic_list_modification_flag_l0) {
         for (unsigned i = 0; i < slice->num_ref_list0_mod_operations; i++) {
            struct pipe_h264_ref_list_mod_entry *op =
               &slice->ref_list0_mod_operations[i];
            radeon_bs_code_ue(&bs, op->modification_of_pic_nums_idc);
            if (op->modification_of_pic_nums_idc == 0 ||
                op->modification_of_pic_nums_idc == 1)
               radeon_bs_code_ue(&bs, op->abs_diff_pic_num_minus1);
            else if (op->modification_of_pic_nums_idc == 2)
               radeon_bs_code_ue(&bs, op->long_term_pic_num);
         }
         radeon_bs_code_ue(&bs, 0x3); /* modification_of_pic_nums_idc */
      }
      if (enc->enc_pic.picture_type == PIPE_H2645_ENC_PICTURE_TYPE_B) {
         radeon_bs_code_fixed_bits(&bs, slice->ref_pic_list_modification_flag_l1, 1);
         if (slice->ref_pic_list_modification_flag_l1) {
            for (unsigned i = 0; i < slice->num_ref_list1_mod_operations; i++) {
               struct pipe_h264_ref_list_mod_entry *op =
                  &slice->ref_list1_mod_operations[i];
               radeon_bs_code_ue(&bs, op->modification_of_pic_nums_idc);
               if (op->modification_of_pic_nums_idc == 0 ||
                   op->modification_of_pic_nums_idc == 1)
                  radeon_bs_code_ue(&bs, op->abs_diff_pic_num_minus1);
               else if (op->modification_of_pic_nums_idc == 2)
                  radeon_bs_code_ue(&bs, op->long_term_pic_num);
            }
            radeon_bs_code_ue(&bs, 0x3); /* modification_of_pic_nums_idc */
         }
      }
   }

   if (!enc->enc_pic.not_referenced) {
      if (enc->enc_pic.picture_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR) {
         radeon_bs_code_fixed_bits(&bs, slice->no_output_of_prior_pics_flag, 1);
         radeon_bs_code_fixed_bits(&bs, slice->long_term_reference_flag, 1);
      } else {
         radeon_bs_code_fixed_bits(&bs, slice->adaptive_ref_pic_marking_mode_flag, 1);
         if (slice->adaptive_ref_pic_marking_mode_flag) {
            for (unsigned i = 0; i < slice->num_ref_pic_marking_operations; i++) {
               struct pipe_h264_ref_pic_marking_entry *op =
                  &slice->ref_pic_marking_operations[i];
               radeon_bs_code_ue(&bs, op->memory_management_control_operation);
               if (op->memory_management_control_operation == 1 ||
                   op->memory_management_control_operation == 3)
                  radeon_bs_code_ue(&bs, op->difference_of_pic_nums_minus1);
               if (op->memory_management_control_operation == 2)
                  radeon_bs_code_ue(&bs, op->long_term_pic_num);
               if (op->memory_management_control_operation == 3 ||
                   op->memory_management_control_operation == 6)
                  radeon_bs_code_ue(&bs, op->long_term_frame_idx);
               if (op->memory_management_control_operation == 4)
                  radeon_bs_code_ue(&bs, op->max_long_term_frame_idx_plus1);
            }
            radeon_bs_code_ue(&bs, 0); /* memory_management_control_operation */
         }
      }
   }

   if ((enc->enc_pic.picture_type != PIPE_H2645_ENC_PICTURE_TYPE_IDR) &&
       (enc->enc_pic.picture_type != PIPE_H2645_ENC_PICTURE_TYPE_I) &&
       (enc->enc_pic.spec_misc.cabac_enable))
      radeon_bs_code_ue(&bs, enc->enc_pic.spec_misc.cabac_init_idc);

   radeon_bs_flush_headers(&bs);
   instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_COPY;
   num_bits[inst_index] = bs.bits_output - bits_copied;
   bits_copied = bs.bits_output;
   inst_index++;

   instruction[inst_index] = RENCODE_H264_HEADER_INSTRUCTION_SLICE_QP_DELTA;
   inst_index++;

   if (enc->enc_pic.spec_misc.deblocking_filter_control_present_flag) {
      radeon_bs_code_ue(&bs, enc->enc_pic.h264_deblock.disable_deblocking_filter_idc);
      if (!enc->enc_pic.h264_deblock.disable_deblocking_filter_idc) {
         radeon_bs_code_se(&bs, enc->enc_pic.h264_deblock.alpha_c0_offset_div2);
         radeon_bs_code_se(&bs, enc->enc_pic.h264_deblock.beta_offset_div2);
      }
   }

   radeon_bs_flush_headers(&bs);
   instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_COPY;
   num_bits[inst_index] = bs.bits_output - bits_copied;
   bits_copied = bs.bits_output;
   inst_index++;

   instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_END;

   cdw_filled = enc->cs.current.cdw - cdw_start;
   for (int i = 0; i < RENCODE_SLICE_HEADER_TEMPLATE_MAX_TEMPLATE_SIZE_IN_DWORDS - cdw_filled; i++)
      RADEON_ENC_CS(0x00000000);

   for (int j = 0; j < RENCODE_SLICE_HEADER_TEMPLATE_MAX_NUM_INSTRUCTIONS; j++) {
      RADEON_ENC_CS(instruction[j]);
      RADEON_ENC_CS(num_bits[j]);
   }

   RADEON_ENC_END();
}

static void radeon_enc_slice_header_hevc(struct radeon_encoder *enc)
{
   struct radeon_bitstream bs;
   struct pipe_h265_enc_seq_param *sps = &enc->enc_pic.hevc.desc->seq;
   struct pipe_h265_enc_pic_param *pps = &enc->enc_pic.hevc.desc->pic;
   struct pipe_h265_enc_slice_param *slice = &enc->enc_pic.hevc.desc->slice;
   uint32_t instruction[RENCODE_SLICE_HEADER_TEMPLATE_MAX_NUM_INSTRUCTIONS] = {0};
   uint32_t num_bits[RENCODE_SLICE_HEADER_TEMPLATE_MAX_NUM_INSTRUCTIONS] = {0};
   unsigned int inst_index = 0;
   unsigned int cdw_start = 0;
   unsigned int cdw_filled = 0;
   unsigned int bits_copied = 0;
   unsigned int num_pic_total_curr = 0;

   RADEON_ENC_BEGIN(enc->cmd.slice_header);
   radeon_bs_reset(&bs, NULL, &enc->cs);
   radeon_bs_set_emulation_prevention(&bs, false);

   cdw_start = enc->cs.current.cdw;
   radeon_bs_code_fixed_bits(&bs, 0x0, 1);
   radeon_bs_code_fixed_bits(&bs, enc->enc_pic.nal_unit_type, 6);
   radeon_bs_code_fixed_bits(&bs, 0x0, 6);
   radeon_bs_code_fixed_bits(&bs, enc->enc_pic.temporal_id + 1, 3);

   radeon_bs_flush_headers(&bs);
   instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_COPY;
   num_bits[inst_index] = bs.bits_output - bits_copied;
   bits_copied = bs.bits_output;
   inst_index++;

   instruction[inst_index] = RENCODE_HEVC_HEADER_INSTRUCTION_FIRST_SLICE;
   inst_index++;

   if ((enc->enc_pic.nal_unit_type >= 16) && (enc->enc_pic.nal_unit_type <= 23))
      radeon_bs_code_fixed_bits(&bs, slice->no_output_of_prior_pics_flag, 1);

   radeon_bs_code_ue(&bs, 0x0); /* slice_pic_parameter_set_id */

   radeon_bs_flush_headers(&bs);
   instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_COPY;
   num_bits[inst_index] = bs.bits_output - bits_copied;
   bits_copied = bs.bits_output;
   inst_index++;

   instruction[inst_index] = RENCODE_HEVC_HEADER_INSTRUCTION_SLICE_SEGMENT;
   inst_index++;

   instruction[inst_index] = RENCODE_HEVC_HEADER_INSTRUCTION_DEPENDENT_SLICE_END;
   inst_index++;

   switch (enc->enc_pic.picture_type) {
   case PIPE_H2645_ENC_PICTURE_TYPE_I:
   case PIPE_H2645_ENC_PICTURE_TYPE_IDR:
      radeon_bs_code_ue(&bs, 0x2);
      break;
   case PIPE_H2645_ENC_PICTURE_TYPE_P:
   case PIPE_H2645_ENC_PICTURE_TYPE_SKIP:
      radeon_bs_code_ue(&bs, 0x1);
      break;
   case PIPE_H2645_ENC_PICTURE_TYPE_B:
      radeon_bs_code_ue(&bs, 0x0);
      break;
   default:
      radeon_bs_code_ue(&bs, 0x1);
   }

   if (pps->output_flag_present_flag)
      radeon_bs_code_fixed_bits(&bs, slice->pic_output_flag, 1);

   if ((enc->enc_pic.nal_unit_type != 19) && (enc->enc_pic.nal_unit_type != 20)) {
      radeon_bs_code_fixed_bits(&bs, slice->slice_pic_order_cnt_lsb, sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
      radeon_bs_code_fixed_bits(&bs, slice->short_term_ref_pic_set_sps_flag, 1);
      if (!slice->short_term_ref_pic_set_sps_flag) {
         num_pic_total_curr =
            radeon_bs_hevc_st_ref_pic_set(&bs, sps->num_short_term_ref_pic_sets,
                                          sps->num_short_term_ref_pic_sets, sps->st_ref_pic_set);
      } else if (sps->num_short_term_ref_pic_sets > 1) {
         radeon_bs_code_fixed_bits(&bs, slice->short_term_ref_pic_set_idx,
                                    util_logbase2_ceil(sps->num_short_term_ref_pic_sets));
      }
      if (sps->long_term_ref_pics_present_flag) {
         if (sps->num_long_term_ref_pics_sps > 0)
            radeon_bs_code_ue(&bs, slice->num_long_term_sps);
         radeon_bs_code_ue(&bs, slice->num_long_term_pics);
         for (unsigned i = 0; i < slice->num_long_term_sps + slice->num_long_term_pics; i++) {
            if (i < slice->num_long_term_sps) {
               if (sps->num_long_term_ref_pics_sps > 1)
                  radeon_bs_code_fixed_bits(&bs, slice->lt_idx_sps[i], util_logbase2_ceil(sps->num_long_term_ref_pics_sps));
            } else {
               radeon_bs_code_fixed_bits(&bs, slice->poc_lsb_lt[i], sps->log2_max_pic_order_cnt_lsb_minus4 + 4);
               radeon_bs_code_fixed_bits(&bs, slice->used_by_curr_pic_lt_flag[i], 1);
               if (slice->used_by_curr_pic_lt_flag[i])
                  num_pic_total_curr++;
            }
            radeon_bs_code_fixed_bits(&bs, slice->delta_poc_msb_present_flag[i], 1);
            if (slice->delta_poc_msb_present_flag[i])
               radeon_bs_code_ue(&bs, slice->delta_poc_msb_cycle_lt[i]);
         }
      }
   }

   if (!enc->enc_pic.hevc_deblock.disable_sao) {
      radeon_bs_flush_headers(&bs);
      instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_COPY;
      num_bits[inst_index] = bs.bits_output - bits_copied;
      bits_copied = bs.bits_output;
      inst_index++;

      instruction[inst_index] = RENCODE_HEVC_HEADER_INSTRUCTION_SAO_ENABLE;
      inst_index++;
   }

   if ((enc->enc_pic.picture_type == PIPE_H2645_ENC_PICTURE_TYPE_P) ||
       (enc->enc_pic.picture_type == PIPE_H2645_ENC_PICTURE_TYPE_B)) {
      radeon_bs_code_fixed_bits(&bs, slice->num_ref_idx_active_override_flag, 1);
      if (slice->num_ref_idx_active_override_flag) {
         radeon_bs_code_ue(&bs, slice->num_ref_idx_l0_active_minus1);
         if (enc->enc_pic.picture_type == PIPE_H2645_ENC_PICTURE_TYPE_B)
            radeon_bs_code_ue(&bs, slice->num_ref_idx_l1_active_minus1);
      }
      if (pps->lists_modification_present_flag && num_pic_total_curr > 1) {
         unsigned num_bits = util_logbase2_ceil(num_pic_total_curr);
         unsigned num_ref_l0_minus1 = slice->num_ref_idx_active_override_flag ?
            slice->num_ref_idx_l0_active_minus1 : pps->num_ref_idx_l0_default_active_minus1;
         radeon_bs_code_fixed_bits(&bs, slice->ref_pic_lists_modification.ref_pic_list_modification_flag_l0, 1);
         for (unsigned i = 0; i <= num_ref_l0_minus1; i++)
            radeon_bs_code_fixed_bits(&bs, slice->ref_pic_lists_modification.list_entry_l0[i], num_bits);
         if (enc->enc_pic.picture_type == PIPE_H2645_ENC_PICTURE_TYPE_B) {
            unsigned num_ref_l1_minus1 = slice->num_ref_idx_active_override_flag ?
               slice->num_ref_idx_l1_active_minus1 : pps->num_ref_idx_l1_default_active_minus1;
            radeon_bs_code_fixed_bits(&bs, slice->ref_pic_lists_modification.ref_pic_list_modification_flag_l1, 1);
            for (unsigned i = 0; i <= num_ref_l1_minus1; i++)
               radeon_bs_code_fixed_bits(&bs, slice->ref_pic_lists_modification.list_entry_l1[i], num_bits);
         }
      }
      if (enc->enc_pic.picture_type == PIPE_H2645_ENC_PICTURE_TYPE_B)
         radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* mvd_l1_zero_flag */
      radeon_bs_code_fixed_bits(&bs, enc->enc_pic.hevc_spec_misc.cabac_init_flag, 1);
      radeon_bs_code_ue(&bs, 5 - slice->max_num_merge_cand);
   }

   radeon_bs_flush_headers(&bs);
   instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_COPY;
   num_bits[inst_index] = bs.bits_output - bits_copied;
   bits_copied = bs.bits_output;
   inst_index++;

   instruction[inst_index] = RENCODE_HEVC_HEADER_INSTRUCTION_SLICE_QP_DELTA;
   inst_index++;

   if ((enc->enc_pic.hevc_deblock.loop_filter_across_slices_enabled) &&
       (!enc->enc_pic.hevc_deblock.deblocking_filter_disabled ||
        !enc->enc_pic.hevc_deblock.disable_sao)) {
       if (!enc->enc_pic.hevc_deblock.disable_sao) {
           radeon_bs_flush_headers(&bs);
           instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_COPY;
           num_bits[inst_index] = bs.bits_output - bits_copied;
           bits_copied = bs.bits_output;
           inst_index++;

           instruction[inst_index] = RENCODE_HEVC_HEADER_INSTRUCTION_LOOP_FILTER_ACROSS_SLICES_ENABLE;
           inst_index++;
       } else {
           radeon_bs_code_fixed_bits(&bs, enc->enc_pic.hevc_deblock.loop_filter_across_slices_enabled, 1);
           radeon_bs_flush_headers(&bs);
           instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_COPY;
           num_bits[inst_index] = bs.bits_output - bits_copied;
           bits_copied = bs.bits_output;
           inst_index++;
       }
   }

   instruction[inst_index] = RENCODE_HEADER_INSTRUCTION_END;

   cdw_filled = enc->cs.current.cdw - cdw_start;
   for (int i = 0; i < RENCODE_SLICE_HEADER_TEMPLATE_MAX_TEMPLATE_SIZE_IN_DWORDS - cdw_filled; i++)
      RADEON_ENC_CS(0x00000000);

   for (int j = 0; j < RENCODE_SLICE_HEADER_TEMPLATE_MAX_NUM_INSTRUCTIONS; j++) {
      RADEON_ENC_CS(instruction[j]);
      RADEON_ENC_CS(num_bits[j]);
   }

   RADEON_ENC_END();
}

static void radeon_enc_ctx(struct radeon_encoder *enc)
{
   enc->enc_pic.ctx_buf.swizzle_mode = 0;
   enc->enc_pic.ctx_buf.two_pass_search_center_map_offset = 0;

   RADEON_ENC_BEGIN(enc->cmd.ctx);
   RADEON_ENC_READWRITE(enc->dpb->res->buf, enc->dpb->res->domains, 0);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.swizzle_mode);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.rec_luma_pitch);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.rec_chroma_pitch);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.num_reconstructed_pictures);

   for (int i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.reconstructed_pictures[i].luma_offset);
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.reconstructed_pictures[i].chroma_offset);
   }

   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_picture_luma_pitch);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_picture_chroma_pitch);

   for (int i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_reconstructed_pictures[i].luma_offset);
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_reconstructed_pictures[i].chroma_offset);
   }

   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_input_picture.yuv.luma_offset);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_input_picture.yuv.chroma_offset);

   RADEON_ENC_CS(enc->enc_pic.ctx_buf.two_pass_search_center_map_offset);
   RADEON_ENC_END();
}

static void radeon_enc_bitstream(struct radeon_encoder *enc)
{
   enc->enc_pic.bit_buf.mode = RENCODE_REC_SWIZZLE_MODE_LINEAR;
   enc->enc_pic.bit_buf.video_bitstream_buffer_size = enc->bs_size;
   enc->enc_pic.bit_buf.video_bitstream_data_offset = enc->bs_offset;

   RADEON_ENC_BEGIN(enc->cmd.bitstream);
   RADEON_ENC_CS(enc->enc_pic.bit_buf.mode);
   RADEON_ENC_WRITE(enc->bs_handle, RADEON_DOMAIN_GTT, 0);
   RADEON_ENC_CS(enc->enc_pic.bit_buf.video_bitstream_buffer_size);
   RADEON_ENC_CS(enc->enc_pic.bit_buf.video_bitstream_data_offset);
   RADEON_ENC_END();
}

static void radeon_enc_feedback(struct radeon_encoder *enc)
{
   enc->enc_pic.fb_buf.mode = RENCODE_FEEDBACK_BUFFER_MODE_LINEAR;
   enc->enc_pic.fb_buf.feedback_buffer_size = 16;
   enc->enc_pic.fb_buf.feedback_data_size = 40;

   RADEON_ENC_BEGIN(enc->cmd.feedback);
   RADEON_ENC_CS(enc->enc_pic.fb_buf.mode);
   RADEON_ENC_WRITE(enc->fb->res->buf, enc->fb->res->domains, 0x0);
   RADEON_ENC_CS(enc->enc_pic.fb_buf.feedback_buffer_size);
   RADEON_ENC_CS(enc->enc_pic.fb_buf.feedback_data_size);
   RADEON_ENC_END();
}

static void radeon_enc_intra_refresh(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.intra_refresh);
   RADEON_ENC_CS(enc->enc_pic.intra_refresh.intra_refresh_mode);
   RADEON_ENC_CS(enc->enc_pic.intra_refresh.offset);
   RADEON_ENC_CS(enc->enc_pic.intra_refresh.region_size);
   RADEON_ENC_END();
}

static void radeon_enc_rc_per_pic(struct radeon_encoder *enc)
{
   debug_warn_once("Obsoleted rate control is being used due to outdated VCN firmware on system. "
                   "Updating VCN firmware is highly recommended.");
   RADEON_ENC_BEGIN(enc->cmd.rc_per_pic);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.qp_obs);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.min_qp_app_obs);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.max_qp_app_obs);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.max_au_size_obs);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.enabled_filler_data);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.skip_frame_enable);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.enforce_hrd);
   RADEON_ENC_END();
}

static void radeon_enc_rc_per_pic_ex(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.rc_per_pic_ex);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.qp_i);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.qp_p);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.qp_b);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.min_qp_i);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.max_qp_i);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.min_qp_p);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.max_qp_p);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.min_qp_b);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.max_qp_b);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.max_au_size_i);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.max_au_size_p);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.max_au_size_b);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.enabled_filler_data);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.skip_frame_enable);
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.enforce_hrd);
   RADEON_ENC_END();
}

static void radeon_enc_encode_params(struct radeon_encoder *enc)
{
   switch (enc->enc_pic.picture_type) {
   case PIPE_H2645_ENC_PICTURE_TYPE_I:
   case PIPE_H2645_ENC_PICTURE_TYPE_IDR:
      enc->enc_pic.enc_params.pic_type = RENCODE_PICTURE_TYPE_I;
      break;
   case PIPE_H2645_ENC_PICTURE_TYPE_P:
      enc->enc_pic.enc_params.pic_type = RENCODE_PICTURE_TYPE_P;
      break;
   case PIPE_H2645_ENC_PICTURE_TYPE_SKIP:
      enc->enc_pic.enc_params.pic_type = RENCODE_PICTURE_TYPE_P_SKIP;
      break;
   case PIPE_H2645_ENC_PICTURE_TYPE_B:
      enc->enc_pic.enc_params.pic_type = RENCODE_PICTURE_TYPE_B;
      break;
   default:
      enc->enc_pic.enc_params.pic_type = RENCODE_PICTURE_TYPE_I;
   }

   if (enc->luma->meta_offset)
      RADEON_ENC_ERR("DCC surfaces not supported.\n");

   enc->enc_pic.enc_params.input_pic_luma_pitch = enc->luma->u.gfx9.surf_pitch;
   enc->enc_pic.enc_params.input_pic_chroma_pitch = enc->chroma ?
      enc->chroma->u.gfx9.surf_pitch : enc->luma->u.gfx9.surf_pitch;
   enc->enc_pic.enc_params.input_pic_swizzle_mode = enc->luma->u.gfx9.swizzle_mode;

   RADEON_ENC_BEGIN(enc->cmd.enc_params);
   RADEON_ENC_CS(enc->enc_pic.enc_params.pic_type);
   RADEON_ENC_CS(enc->enc_pic.enc_params.allowed_max_bitstream_size);
   RADEON_ENC_READ(enc->handle, RADEON_DOMAIN_VRAM, enc->luma->u.gfx9.surf_offset);
   RADEON_ENC_READ(enc->handle, RADEON_DOMAIN_VRAM, enc->chroma ?
      enc->chroma->u.gfx9.surf_offset : enc->luma->u.gfx9.surf_pitch);
   RADEON_ENC_CS(enc->enc_pic.enc_params.input_pic_luma_pitch);
   RADEON_ENC_CS(enc->enc_pic.enc_params.input_pic_chroma_pitch);
   RADEON_ENC_CS(enc->enc_pic.enc_params.input_pic_swizzle_mode);
   RADEON_ENC_CS(enc->enc_pic.enc_params.reference_picture_index);
   RADEON_ENC_CS(enc->enc_pic.enc_params.reconstructed_picture_index);
   RADEON_ENC_END();
}

static void radeon_enc_encode_params_h264(struct radeon_encoder *enc)
{
   enc->enc_pic.h264_enc_params.input_picture_structure = RENCODE_H264_PICTURE_STRUCTURE_FRAME;
   enc->enc_pic.h264_enc_params.interlaced_mode = RENCODE_H264_INTERLACING_MODE_PROGRESSIVE;
   enc->enc_pic.h264_enc_params.reference_picture_structure = RENCODE_H264_PICTURE_STRUCTURE_FRAME;
   enc->enc_pic.h264_enc_params.reference_picture1_index = 0xFFFFFFFF;

   RADEON_ENC_BEGIN(enc->cmd.enc_params_h264);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.input_picture_structure);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.interlaced_mode);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.reference_picture_structure);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.reference_picture1_index);
   RADEON_ENC_END();
}

static void radeon_enc_encode_statistics(struct radeon_encoder *enc)
{
   if (!enc->stats) return;

   enc->enc_pic.enc_statistics.encode_stats_type = RENCODE_STATISTICS_TYPE_0;

   RADEON_ENC_BEGIN(enc->cmd.enc_statistics);
   RADEON_ENC_CS(enc->enc_pic.enc_statistics.encode_stats_type);
   RADEON_ENC_WRITE(enc->stats, RADEON_DOMAIN_GTT, 0);
   RADEON_ENC_END();
}

static void radeon_enc_qp_map(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.enc_qp_map);
   RADEON_ENC_CS(enc->enc_pic.enc_qp_map.qp_map_type);
   if (enc->enc_pic.enc_qp_map.qp_map_type != RENCODE_QP_MAP_TYPE_NONE)
      RADEON_ENC_READWRITE(enc->roi->res->buf, enc->roi->res->domains, 0);
   else {
      RADEON_ENC_CS(0); /* use null for roi buffer */
      RADEON_ENC_CS(0); /* use null for roi buffer */
   }
   RADEON_ENC_CS(0); /* qp_map pitch set to 0 for the ib */
   RADEON_ENC_END();
}

static void radeon_enc_encode_latency(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.enc_latency);
   RADEON_ENC_CS(enc->enc_pic.enc_latency.encode_latency);
   RADEON_ENC_END();
}

static void radeon_enc_op_init(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(RENCODE_IB_OP_INITIALIZE);
   RADEON_ENC_END();
}

static void radeon_enc_op_close(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(RENCODE_IB_OP_CLOSE_SESSION);
   RADEON_ENC_END();
}

static void radeon_enc_op_enc(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(RENCODE_IB_OP_ENCODE);
   RADEON_ENC_END();
}

static void radeon_enc_op_init_rc(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(RENCODE_IB_OP_INIT_RC);
   RADEON_ENC_END();
}

static void radeon_enc_op_init_rc_vbv(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(RENCODE_IB_OP_INIT_RC_VBV_BUFFER_LEVEL);
   RADEON_ENC_END();
}

static void radeon_enc_op_preset(struct radeon_encoder *enc)
{
   uint32_t preset_mode;

   if (enc->enc_pic.quality_modes.preset_mode == RENCODE_PRESET_MODE_QUALITY)
      preset_mode = RENCODE_IB_OP_SET_QUALITY_ENCODING_MODE;
   else if (enc->enc_pic.quality_modes.preset_mode == RENCODE_PRESET_MODE_BALANCE)
      preset_mode = RENCODE_IB_OP_SET_BALANCE_ENCODING_MODE;
   else
      preset_mode = RENCODE_IB_OP_SET_SPEED_ENCODING_MODE;

   RADEON_ENC_BEGIN(preset_mode);
   RADEON_ENC_END();
}

static void begin(struct radeon_encoder *enc)
{
   unsigned i;

   enc->session_info(enc);
   enc->total_task_size = 0;
   enc->task_info(enc, enc->need_feedback);
   enc->op_init(enc);

   enc->session_init(enc);
   enc->slice_control(enc);
   enc->spec_misc(enc);
   enc->deblocking_filter(enc);

   enc->layer_control(enc);
   enc->rc_session_init(enc);
   enc->quality_params(enc);
   enc->encode_latency(enc);

   i = 0;
   do {
      enc->enc_pic.layer_sel.temporal_layer_index = i;
      enc->layer_select(enc);
      enc->rc_layer_init(enc);
      enc->layer_select(enc);
      enc->rc_per_pic(enc);
   } while (++i < enc->enc_pic.num_temporal_layers);

   enc->op_init_rc(enc);
   enc->op_init_rc_vbv(enc);
   *enc->p_task_size = (enc->total_task_size);
}

static void radeon_enc_headers_h264(struct radeon_encoder *enc)
{
   enc->slice_header(enc);
   enc->encode_params(enc);
   enc->encode_params_codec_spec(enc);
}

static void radeon_enc_headers_hevc(struct radeon_encoder *enc)
{
   enc->slice_header(enc);
   enc->encode_params(enc);
   enc->encode_params_codec_spec(enc);
}

static void encode(struct radeon_encoder *enc)
{
   unsigned i;

   enc->before_encode(enc);
   enc->session_info(enc);
   enc->total_task_size = 0;
   enc->task_info(enc, enc->need_feedback);

   if (enc->need_rate_control || enc->need_rc_per_pic) {
      i = 0;
      do {
         enc->enc_pic.layer_sel.temporal_layer_index = i;
         if (enc->need_rate_control) {
            enc->layer_select(enc);
            enc->rc_layer_init(enc);
         }
         if (enc->need_rc_per_pic) {
            enc->layer_select(enc);
            enc->rc_per_pic(enc);
         }
      } while (++i < enc->enc_pic.num_temporal_layers);
   }

   enc->enc_pic.layer_sel.temporal_layer_index = enc->enc_pic.temporal_id;
   enc->layer_select(enc);

   enc->encode_headers(enc);
   enc->ctx(enc);
   enc->bitstream(enc);
   enc->feedback(enc);
   enc->intra_refresh(enc);
   enc->qp_map(enc);

   enc->op_preset(enc);
   enc->op_enc(enc);
   *enc->p_task_size = (enc->total_task_size);
}

static void destroy(struct radeon_encoder *enc)
{
   enc->session_info(enc);
   enc->total_task_size = 0;
   enc->task_info(enc, enc->need_feedback);
   enc->op_close(enc);
   *enc->p_task_size = (enc->total_task_size);
}

void radeon_enc_1_2_init(struct radeon_encoder *enc)
{
   enc->before_encode = radeon_enc_dummy;
   enc->begin = begin;
   enc->encode = encode;
   enc->destroy = destroy;
   enc->session_info = radeon_enc_session_info;
   enc->task_info = radeon_enc_task_info;
   enc->layer_control = radeon_enc_layer_control;
   enc->layer_select = radeon_enc_layer_select;
   enc->rc_session_init = radeon_enc_rc_session_init;
   enc->rc_layer_init = radeon_enc_rc_layer_init;
   enc->quality_params = radeon_enc_quality_params;
   enc->ctx = radeon_enc_ctx;
   enc->bitstream = radeon_enc_bitstream;
   enc->feedback = radeon_enc_feedback;
   enc->intra_refresh = radeon_enc_intra_refresh;
   if (enc->enc_pic.use_rc_per_pic_ex == true)
      enc->rc_per_pic = radeon_enc_rc_per_pic_ex;
   else
      enc->rc_per_pic = radeon_enc_rc_per_pic;
   enc->encode_params = radeon_enc_encode_params;
   enc->op_init = radeon_enc_op_init;
   enc->op_close = radeon_enc_op_close;
   enc->op_enc = radeon_enc_op_enc;
   enc->op_init_rc = radeon_enc_op_init_rc;
   enc->op_init_rc_vbv = radeon_enc_op_init_rc_vbv;
   enc->op_preset = radeon_enc_op_preset;
   enc->session_init = radeon_enc_session_init;
   enc->encode_statistics = radeon_enc_encode_statistics;
   enc->qp_map = radeon_enc_qp_map;
   enc->encode_latency = radeon_enc_encode_latency;

   if (u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC) {
      enc->slice_control = radeon_enc_slice_control;
      enc->spec_misc = radeon_enc_spec_misc;
      enc->deblocking_filter = radeon_enc_deblocking_filter_h264;
      enc->slice_header = radeon_enc_slice_header;
      enc->encode_params_codec_spec = radeon_enc_encode_params_h264;
      enc->encode_headers = radeon_enc_headers_h264;
   } else if (u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_HEVC) {
      enc->slice_control = radeon_enc_slice_control_hevc;
      enc->spec_misc = radeon_enc_spec_misc_hevc;
      enc->deblocking_filter = radeon_enc_deblocking_filter_hevc;
      enc->slice_header = radeon_enc_slice_header_hevc;
      enc->encode_headers = radeon_enc_headers_hevc;
      enc->encode_params_codec_spec = radeon_enc_dummy;
   }

   enc->enc_pic.session_info.interface_version =
      ((RENCODE_FW_INTERFACE_MAJOR_VERSION << RENCODE_IF_MAJOR_VERSION_SHIFT) |
       (RENCODE_FW_INTERFACE_MINOR_VERSION << RENCODE_IF_MINOR_VERSION_SHIFT));
}
