/**************************************************************************
 *
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#include "radeon_vcn_enc.h"
#include "ac_vcn_enc_av1_default_cdf.h"
#include "ac_debug.h"

#include "pipe/p_video_codec.h"
#include "radeon_video.h"
#include "radeonsi/si_pipe.h"
#include "util/u_memory.h"
#include "util/u_video.h"
#include "vl/vl_video_buffer.h"

/* set quality modes from the input */
static void radeon_vcn_enc_quality_modes(struct radeon_encoder *enc,
                                         struct pipe_enc_quality_modes *in)
{
   rvcn_enc_quality_modes_t *p = &enc->enc_pic.quality_modes;
   struct si_screen *sscreen = (struct si_screen *)enc->screen;

   p->preset_mode = in->preset_mode > RENCODE_PRESET_MODE_HIGH_QUALITY
                                    ? RENCODE_PRESET_MODE_HIGH_QUALITY
                                    : in->preset_mode;

   if (u_reduce_video_profile(enc->base.profile) != PIPE_VIDEO_FORMAT_AV1 &&
       p->preset_mode == RENCODE_PRESET_MODE_HIGH_QUALITY)
      p->preset_mode = RENCODE_PRESET_MODE_QUALITY;

   p->pre_encode_mode = in->pre_encode_mode ? RENCODE_PREENCODE_MODE_4X
                                            : RENCODE_PREENCODE_MODE_NONE;

   if (enc->enc_pic.rc_session_init.rate_control_method == RENCODE_RATE_CONTROL_METHOD_QUALITY_VBR)
      p->pre_encode_mode = RENCODE_PREENCODE_MODE_4X;

   /* Disabling 2pass encoding for VCN 5.0
    * This is a temporary limitation only for VCN 5.0 due to HW,
    * once verified in future VCN 5.X versions, it will be enabled again.
    */
   if (sscreen->info.vcn_ip_version >= VCN_5_0_0)
      p->pre_encode_mode = RENCODE_PREENCODE_MODE_NONE;

   p->vbaq_mode = in->vbaq_mode ? RENCODE_VBAQ_AUTO : RENCODE_VBAQ_NONE;

   if (enc->enc_pic.rc_session_init.rate_control_method == RENCODE_RATE_CONTROL_METHOD_NONE)
      p->vbaq_mode = RENCODE_VBAQ_NONE;

   enc->enc_pic.quality_params.vbaq_mode = p->vbaq_mode;
   enc->enc_pic.quality_params.scene_change_sensitivity = 0;
   enc->enc_pic.quality_params.scene_change_min_idr_interval = 0;
   enc->enc_pic.quality_params.two_pass_search_center_map_mode =
      (enc->enc_pic.quality_modes.pre_encode_mode &&
       !enc->enc_pic.spec_misc.b_picture_enabled) ? 1 : 0;
   enc->enc_pic.quality_params.vbaq_strength = 0;
}

/* to process invalid frame rate */
static void radeon_vcn_enc_invalid_frame_rate(uint32_t *den, uint32_t *num)
{
   if (*den == 0 || *num == 0) {
      *den = 1;
      *num = 30;
   }
}

static uint32_t radeon_vcn_per_frame_integer(uint32_t bitrate, uint32_t den, uint32_t num)
{
   uint64_t rate_den = (uint64_t)bitrate * (uint64_t)den;

   return (uint32_t)(rate_den/num);
}

static uint32_t radeon_vcn_per_frame_frac(uint32_t bitrate, uint32_t den, uint32_t num)
{
   uint64_t rate_den = (uint64_t)bitrate * (uint64_t)den;
   uint64_t remainder = rate_den % num;

   return (uint32_t)((remainder << 32) / num);
}

/* block length for av1 and hevc is the same, 64, for avc 16 */
static uint32_t radeon_vcn_enc_blocks_in_frame(struct radeon_encoder *enc,
                                           uint32_t *width_in_block,
                                           uint32_t *height_in_block)
{
   bool is_h264 = u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC;
   uint32_t block_length = is_h264 ? PIPE_H264_MB_SIZE : PIPE_H265_ENC_CTB_SIZE;

   *width_in_block  = PIPE_ALIGN_IN_BLOCK_SIZE(enc->base.width,  block_length);
   *height_in_block = PIPE_ALIGN_IN_BLOCK_SIZE(enc->base.height, block_length);

   return block_length;
}

static void radeon_vcn_enc_get_intra_refresh_param(struct radeon_encoder *enc,
                                                   bool need_filter_overlap,
                                                   struct pipe_enc_intra_refresh *intra_refresh)
{
   uint32_t width_in_block, height_in_block;

   enc->enc_pic.intra_refresh.intra_refresh_mode = RENCODE_INTRA_REFRESH_MODE_NONE;
   /* some exceptions where intra-refresh is disabled:
    * 1. if B frame is enabled
    * 2. if SVC (number of temproal layers is larger than 1) is enabled
    */
   if (enc->enc_pic.spec_misc.b_picture_enabled || enc->enc_pic.num_temporal_layers > 1) {
      enc->enc_pic.intra_refresh.region_size = 0;
      enc->enc_pic.intra_refresh.offset = 0;
      return;
   }

   radeon_vcn_enc_blocks_in_frame(enc, &width_in_block, &height_in_block);

   switch(intra_refresh->mode) {
      case INTRA_REFRESH_MODE_UNIT_ROWS:
         if (intra_refresh->offset < height_in_block)
            enc->enc_pic.intra_refresh.intra_refresh_mode
                                             = RENCODE_INTRA_REFRESH_MODE_CTB_MB_ROWS;
         break;
      case INTRA_REFRESH_MODE_UNIT_COLUMNS:
         if (intra_refresh->offset < width_in_block)
            enc->enc_pic.intra_refresh.intra_refresh_mode
                                             = RENCODE_INTRA_REFRESH_MODE_CTB_MB_COLUMNS;
         break;
      case INTRA_REFRESH_MODE_NONE:
      default:
         break;
   };

   /* with loop filters (avc/hevc/av1) enabled the region_size has to increase 1 to
    * get overlapped (av1 is enabling it all the time). The region_size and offset
    * require to be in unit of MB or CTB or SB according to different codecs.
    */
   if (enc->enc_pic.intra_refresh.intra_refresh_mode != RENCODE_INTRA_REFRESH_MODE_NONE) {
      enc->enc_pic.intra_refresh.region_size = (need_filter_overlap) ?
                                               intra_refresh->region_size + 1 :
                                               intra_refresh->region_size;
      enc->enc_pic.intra_refresh.offset = intra_refresh->offset;
   } else {
      enc->enc_pic.intra_refresh.region_size = 0;
      enc->enc_pic.intra_refresh.offset = 0;
   }
}

static void radeon_vcn_enc_get_roi_param(struct radeon_encoder *enc,
                                         struct pipe_enc_roi *roi)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;
   bool is_av1 = u_reduce_video_profile(enc->base.profile)
                             == PIPE_VIDEO_FORMAT_AV1;
   rvcn_enc_qp_map_t *qp_map = &enc->enc_pic.enc_qp_map;

   if (!roi->num)
      enc->enc_pic.enc_qp_map.qp_map_type = RENCODE_QP_MAP_TYPE_NONE;
   else {
      uint32_t width_in_block, height_in_block;
      uint32_t block_length;
      int32_t i, j, pa_format = 0;

      qp_map->version = sscreen->info.vcn_ip_version >= VCN_5_0_0
                        ? RENCODE_QP_MAP_VCN5 : RENCODE_QP_MAP_LEGACY;

      /* rate control is using a different qp map type, in case of below
       * vcn_5_0_0 */
      if (enc->enc_pic.rc_session_init.rate_control_method &&
            (qp_map->version ==  RENCODE_QP_MAP_LEGACY)) {
         enc->enc_pic.enc_qp_map.qp_map_type = RENCODE_QP_MAP_TYPE_MAP_PA;
         pa_format = 1;
      }
      else
         enc->enc_pic.enc_qp_map.qp_map_type = RENCODE_QP_MAP_TYPE_DELTA;

      block_length = radeon_vcn_enc_blocks_in_frame(enc, &width_in_block, &height_in_block);

      qp_map->width_in_block = width_in_block;
      qp_map->height_in_block = height_in_block;

      for (i = RENCODE_QP_MAP_MAX_REGIONS - 1; i >= roi->num; i--)
         enc->enc_pic.enc_qp_map.map[i].is_valid = false;

      /* reverse the map sequence */
      for (j = 0; i >= 0; i--, j++) {
         struct rvcn_enc_qp_map_region *map = &enc->enc_pic.enc_qp_map.map[j];
         struct pipe_enc_region_in_roi *region = &roi->region[i];

         map->is_valid = region->valid;
         if (region->valid) {
            int32_t av1_qi_value;
            /* mapped av1 qi into the legacy qp range by dividing by 5 and
             * rounding up in any rate control mode.
             */
            if (is_av1 && (pa_format || (qp_map->version ==  RENCODE_QP_MAP_VCN5))) {
               if (region->qp_value > 0)
                  av1_qi_value = (region->qp_value + 2) / 5;
               else if (region->qp_value < 0)
                  av1_qi_value = (region->qp_value - 2) / 5;
               else
                  av1_qi_value = region->qp_value;
               map->qp_delta = av1_qi_value;
            } else
               map->qp_delta = region->qp_value;

            map->x_in_unit = CLAMP((region->x / block_length), 0, width_in_block - 1);
            map->y_in_unit = CLAMP((region->y / block_length), 0, height_in_block - 1);
            map->width_in_unit = CLAMP((region->width / block_length), 0, width_in_block);
            map->height_in_unit = CLAMP((region->height / block_length), 0, width_in_block);
         }
      }
   }
}

static void radeon_vcn_enc_get_latency_param(struct radeon_encoder *enc)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;

   enc->enc_pic.enc_latency.encode_latency =
      sscreen->debug_flags & DBG(LOW_LATENCY_ENCODE) ? 1000 : 0;
}

static void radeon_vcn_enc_h264_get_session_param(struct radeon_encoder *enc,
                                                  struct pipe_h264_enc_picture_desc *pic)
{
   if (enc->enc_pic.session_init.aligned_picture_width)
      return;

   uint32_t align_width = PIPE_H264_MB_SIZE;
   uint32_t align_height = PIPE_H264_MB_SIZE;

   enc->enc_pic.session_init.encode_standard = RENCODE_ENCODE_STANDARD_H264;
   enc->enc_pic.session_init.aligned_picture_width = align(enc->base.width, align_width);
   enc->enc_pic.session_init.aligned_picture_height = align(enc->base.height, align_height);

   uint32_t padding_width = 0;
   uint32_t padding_height = 0;
   uint32_t max_padding_width = align_width - 2;
   uint32_t max_padding_height = align_height - 2;

   if (enc->enc_pic.session_init.aligned_picture_width > enc->source->width)
      padding_width = enc->enc_pic.session_init.aligned_picture_width - enc->source->width;
   if (enc->enc_pic.session_init.aligned_picture_height > enc->source->height)
      padding_height = enc->enc_pic.session_init.aligned_picture_height - enc->source->height;

   /* Input surface can be smaller if the difference is within padding bounds. */
   if (padding_width > max_padding_width || padding_height > max_padding_height)
      RADEON_ENC_ERR("Input surface size doesn't match aligned size\n");

   if (pic->seq.enc_frame_cropping_flag) {
      uint32_t pad_w =
         (pic->seq.enc_frame_crop_left_offset + pic->seq.enc_frame_crop_right_offset) * 2;
      uint32_t pad_h =
         (pic->seq.enc_frame_crop_top_offset + pic->seq.enc_frame_crop_bottom_offset) * 2;
      padding_width = CLAMP(pad_w, padding_width, max_padding_width);
      padding_height = CLAMP(pad_h, padding_height, max_padding_height);
   }

   enc->enc_pic.session_init.padding_width = padding_width;
   enc->enc_pic.session_init.padding_height = padding_height;
}

static void radeon_vcn_enc_h264_get_dbk_param(struct radeon_encoder *enc,
                                              struct pipe_h264_enc_picture_desc *pic)
{
   enc->enc_pic.h264_deblock.disable_deblocking_filter_idc =
      CLAMP(pic->dbk.disable_deblocking_filter_idc, 0, 2);
   enc->enc_pic.h264_deblock.alpha_c0_offset_div2 = pic->dbk.alpha_c0_offset_div2;
   enc->enc_pic.h264_deblock.beta_offset_div2 = pic->dbk.beta_offset_div2;
   enc->enc_pic.h264_deblock.cb_qp_offset = pic->pic_ctrl.chroma_qp_index_offset;
   enc->enc_pic.h264_deblock.cr_qp_offset = pic->pic_ctrl.second_chroma_qp_index_offset;
}

static void radeon_vcn_enc_h264_get_spec_misc_param(struct radeon_encoder *enc,
                                                    struct pipe_h264_enc_picture_desc *pic)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;

   enc->enc_pic.spec_misc.profile_idc = u_get_h264_profile_idc(enc->base.profile);
   if (enc->enc_pic.spec_misc.profile_idc >= PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN &&
         enc->enc_pic.spec_misc.profile_idc != PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED)
      enc->enc_pic.spec_misc.cabac_enable = pic->pic_ctrl.enc_cabac_enable;
   else
      enc->enc_pic.spec_misc.cabac_enable = false;

   enc->enc_pic.spec_misc.cabac_init_idc = enc->enc_pic.spec_misc.cabac_enable ?
                                           pic->pic_ctrl.enc_cabac_init_idc : 0;
   enc->enc_pic.spec_misc.deblocking_filter_control_present_flag =
      pic->pic_ctrl.deblocking_filter_control_present_flag;
   enc->enc_pic.spec_misc.redundant_pic_cnt_present_flag =
      pic->pic_ctrl.redundant_pic_cnt_present_flag;
   enc->enc_pic.spec_misc.b_picture_enabled = !!pic->seq.max_num_reorder_frames;
   enc->enc_pic.spec_misc.constrained_intra_pred_flag =
      pic->pic_ctrl.constrained_intra_pred_flag;
   enc->enc_pic.spec_misc.half_pel_enabled = 1;
   enc->enc_pic.spec_misc.quarter_pel_enabled = 1;
   enc->enc_pic.spec_misc.weighted_bipred_idc = 0;
   enc->enc_pic.spec_misc.transform_8x8_mode =
      sscreen->info.vcn_ip_version >= VCN_5_0_0 &&
      pic->pic_ctrl.transform_8x8_mode_flag;
   enc->enc_pic.spec_misc.level_idc = pic->seq.level_idc;
}

static void radeon_vcn_enc_h264_get_rc_param(struct radeon_encoder *enc,
                                             struct pipe_h264_enc_picture_desc *pic)
{
   uint32_t frame_rate_den, frame_rate_num, max_qp;

   enc->enc_pic.num_temporal_layers = pic->seq.num_temporal_layers ? pic->seq.num_temporal_layers : 1;
   enc->enc_pic.temporal_id = MIN2(pic->pic_ctrl.temporal_id, enc->enc_pic.num_temporal_layers - 1);

   for (int i = 0; i < enc->enc_pic.num_temporal_layers; i++) {
      enc->enc_pic.rc_layer_init[i].target_bit_rate = pic->rate_ctrl[i].target_bitrate;
      enc->enc_pic.rc_layer_init[i].peak_bit_rate = pic->rate_ctrl[i].peak_bitrate;
      frame_rate_den = pic->rate_ctrl[i].frame_rate_den;
      frame_rate_num = pic->rate_ctrl[i].frame_rate_num;
      radeon_vcn_enc_invalid_frame_rate(&frame_rate_den, &frame_rate_num);
      enc->enc_pic.rc_layer_init[i].frame_rate_den = frame_rate_den;
      enc->enc_pic.rc_layer_init[i].frame_rate_num = frame_rate_num;
      enc->enc_pic.rc_layer_init[i].vbv_buffer_size = pic->rate_ctrl[i].vbv_buffer_size;
      enc->enc_pic.rc_layer_init[i].avg_target_bits_per_picture =
         radeon_vcn_per_frame_integer(pic->rate_ctrl[i].target_bitrate,
               frame_rate_den,
               frame_rate_num);
      enc->enc_pic.rc_layer_init[i].peak_bits_per_picture_integer =
         radeon_vcn_per_frame_integer(pic->rate_ctrl[i].peak_bitrate,
               frame_rate_den,
               frame_rate_num);
      enc->enc_pic.rc_layer_init[i].peak_bits_per_picture_fractional =
         radeon_vcn_per_frame_frac(pic->rate_ctrl[i].peak_bitrate,
               frame_rate_den,
               frame_rate_num);
   }
   enc->enc_pic.rc_session_init.vbv_buffer_level = pic->rate_ctrl[0].vbv_buf_lv;
   enc->enc_pic.rc_per_pic.qp_obs = pic->quant_i_frames;
   enc->enc_pic.rc_per_pic.min_qp_app_obs = pic->rate_ctrl[0].min_qp;
   enc->enc_pic.rc_per_pic.max_qp_app_obs = pic->rate_ctrl[0].max_qp ?
                                        pic->rate_ctrl[0].max_qp : 51;
   enc->enc_pic.rc_per_pic.qp_i = pic->quant_i_frames;
   enc->enc_pic.rc_per_pic.qp_p = pic->quant_p_frames;
   enc->enc_pic.rc_per_pic.qp_b = pic->quant_b_frames;
   enc->enc_pic.rc_per_pic.min_qp_i = pic->rate_ctrl[0].min_qp;
   enc->enc_pic.rc_per_pic.min_qp_p = pic->rate_ctrl[0].min_qp;
   enc->enc_pic.rc_per_pic.min_qp_b = pic->rate_ctrl[0].min_qp;
   max_qp = pic->rate_ctrl[0].max_qp ? pic->rate_ctrl[0].max_qp : 51;
   enc->enc_pic.rc_per_pic.max_qp_i = max_qp;
   enc->enc_pic.rc_per_pic.max_qp_p = max_qp;
   enc->enc_pic.rc_per_pic.max_qp_b = max_qp;
   enc->enc_pic.rc_per_pic.enabled_filler_data = 0;
   enc->enc_pic.rc_per_pic.skip_frame_enable = pic->rate_ctrl[0].skip_frame_enable;
   enc->enc_pic.rc_per_pic.enforce_hrd = pic->rate_ctrl[0].enforce_hrd;
   enc->enc_pic.rc_per_pic.qvbr_quality_level = pic->rate_ctrl[0].vbr_quality_factor;

   switch (pic->rate_ctrl[0].rate_ctrl_method) {
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE:
         enc->enc_pic.rc_session_init.rate_control_method = RENCODE_RATE_CONTROL_METHOD_NONE;
         break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT_SKIP:
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT:
         enc->enc_pic.rc_session_init.rate_control_method = RENCODE_RATE_CONTROL_METHOD_CBR;
         enc->enc_pic.rc_per_pic.enabled_filler_data = pic->rate_ctrl[0].fill_data_enable;
         break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE_SKIP:
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE:
         enc->enc_pic.rc_session_init.rate_control_method =
            RENCODE_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
         break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_QUALITY_VARIABLE:
         enc->enc_pic.rc_session_init.rate_control_method =
            RENCODE_RATE_CONTROL_METHOD_QUALITY_VBR;
         break;
      default:
         enc->enc_pic.rc_session_init.rate_control_method = RENCODE_RATE_CONTROL_METHOD_NONE;
   }
   enc->enc_pic.rc_per_pic.max_au_size_obs = pic->rate_ctrl[0].max_au_size;
   enc->enc_pic.rc_per_pic.max_au_size_i = pic->rate_ctrl[0].max_au_size;
   enc->enc_pic.rc_per_pic.max_au_size_p = pic->rate_ctrl[0].max_au_size;
   enc->enc_pic.rc_per_pic.max_au_size_b = pic->rate_ctrl[0].max_au_size;
}

static void radeon_vcn_enc_h264_get_slice_ctrl_param(struct radeon_encoder *enc,
                                                     struct pipe_h264_enc_picture_desc *pic)
{
   uint32_t num_mbs_total, num_mbs_in_slice;

   num_mbs_total =
      PIPE_ALIGN_IN_BLOCK_SIZE(enc->base.width, PIPE_H264_MB_SIZE) *
      PIPE_ALIGN_IN_BLOCK_SIZE(enc->base.height, PIPE_H264_MB_SIZE);

   if (pic->num_slice_descriptors <= 1) {
      num_mbs_in_slice = num_mbs_total;
   } else {
      bool use_app_config = true;
      num_mbs_in_slice = pic->slices_descriptors[0].num_macroblocks;

      /* All slices must have equal size */
      for (unsigned i = 1; i < pic->num_slice_descriptors - 1; i++) {
         if (num_mbs_in_slice != pic->slices_descriptors[i].num_macroblocks)
            use_app_config = false;
      }
      /* Except last one can be smaller */
      if (pic->slices_descriptors[pic->num_slice_descriptors - 1].num_macroblocks > num_mbs_in_slice)
         use_app_config = false;

      if (!use_app_config) {
         assert(num_mbs_total >= pic->num_slice_descriptors);
         num_mbs_in_slice =
            (num_mbs_total + pic->num_slice_descriptors - 1) / pic->num_slice_descriptors;
      }
   }

   num_mbs_in_slice = MAX2(4, num_mbs_in_slice);

   enc->enc_pic.slice_ctrl.slice_control_mode = RENCODE_H264_SLICE_CONTROL_MODE_FIXED_MBS;
   enc->enc_pic.slice_ctrl.num_mbs_per_slice = num_mbs_in_slice;
}

static void radeon_vcn_enc_get_output_format_param(struct radeon_encoder *enc, bool full_range)
{
   switch (enc->enc_pic.bit_depth_luma_minus8) {
   case 2: /* 10 bits */
      enc->enc_pic.enc_output_format.output_color_volume = RENCODE_COLOR_VOLUME_G22_BT709;
      enc->enc_pic.enc_output_format.output_color_range = full_range ?
         RENCODE_COLOR_RANGE_FULL : RENCODE_COLOR_RANGE_STUDIO;
      enc->enc_pic.enc_output_format.output_chroma_location = RENCODE_CHROMA_LOCATION_INTERSTITIAL;
      enc->enc_pic.enc_output_format.output_color_bit_depth = RENCODE_COLOR_BIT_DEPTH_10_BIT;
      break;
   default: /* 8 bits */
      enc->enc_pic.enc_output_format.output_color_volume = RENCODE_COLOR_VOLUME_G22_BT709;
      enc->enc_pic.enc_output_format.output_color_range = full_range ?
         RENCODE_COLOR_RANGE_FULL : RENCODE_COLOR_RANGE_STUDIO;
      enc->enc_pic.enc_output_format.output_chroma_location = RENCODE_CHROMA_LOCATION_INTERSTITIAL;
      enc->enc_pic.enc_output_format.output_color_bit_depth = RENCODE_COLOR_BIT_DEPTH_8_BIT;
      break;
   }
}

static void radeon_vcn_enc_get_input_format_param(struct radeon_encoder *enc,
                                                  struct pipe_picture_desc *pic_base)
{
   switch (pic_base->input_format) {
   case PIPE_FORMAT_P010:
      enc->enc_pic.enc_input_format.input_color_bit_depth = RENCODE_COLOR_BIT_DEPTH_10_BIT;
      enc->enc_pic.enc_input_format.input_color_packing_format = RENCODE_COLOR_PACKING_FORMAT_P010;
      enc->enc_pic.enc_input_format.input_chroma_subsampling = RENCODE_CHROMA_SUBSAMPLING_4_2_0;
      enc->enc_pic.enc_input_format.input_color_space = RENCODE_COLOR_SPACE_YUV;
      break;
   case PIPE_FORMAT_B8G8R8A8_UNORM:
   case PIPE_FORMAT_B8G8R8X8_UNORM:
      enc->enc_pic.enc_input_format.input_color_bit_depth = RENCODE_COLOR_BIT_DEPTH_8_BIT;
      enc->enc_pic.enc_input_format.input_chroma_subsampling = RENCODE_CHROMA_SUBSAMPLING_4_4_4;
      enc->enc_pic.enc_input_format.input_color_packing_format = RENCODE_COLOR_PACKING_FORMAT_A8R8G8B8;
      enc->enc_pic.enc_input_format.input_color_space = RENCODE_COLOR_SPACE_RGB;
      break;
   case PIPE_FORMAT_R8G8B8A8_UNORM:
   case PIPE_FORMAT_R8G8B8X8_UNORM:
      enc->enc_pic.enc_input_format.input_color_bit_depth = RENCODE_COLOR_BIT_DEPTH_8_BIT;
      enc->enc_pic.enc_input_format.input_chroma_subsampling = RENCODE_CHROMA_SUBSAMPLING_4_4_4;
      enc->enc_pic.enc_input_format.input_color_packing_format = RENCODE_COLOR_PACKING_FORMAT_A8B8G8R8;
      enc->enc_pic.enc_input_format.input_color_space = RENCODE_COLOR_SPACE_RGB;
      break;
   case PIPE_FORMAT_B10G10R10A2_UNORM:
   case PIPE_FORMAT_B10G10R10X2_UNORM:
      enc->enc_pic.enc_input_format.input_color_bit_depth = RENCODE_COLOR_BIT_DEPTH_10_BIT;
      enc->enc_pic.enc_input_format.input_chroma_subsampling = RENCODE_CHROMA_SUBSAMPLING_4_4_4;
      enc->enc_pic.enc_input_format.input_color_packing_format = RENCODE_COLOR_PACKING_FORMAT_A2R10G10B10;
      enc->enc_pic.enc_input_format.input_color_space = RENCODE_COLOR_SPACE_RGB;
      break;
   case PIPE_FORMAT_R10G10B10A2_UNORM:
   case PIPE_FORMAT_R10G10B10X2_UNORM:
      enc->enc_pic.enc_input_format.input_color_bit_depth = RENCODE_COLOR_BIT_DEPTH_10_BIT;
      enc->enc_pic.enc_input_format.input_chroma_subsampling = RENCODE_CHROMA_SUBSAMPLING_4_4_4;
      enc->enc_pic.enc_input_format.input_color_packing_format = RENCODE_COLOR_PACKING_FORMAT_A2B10G10R10;
      enc->enc_pic.enc_input_format.input_color_space = RENCODE_COLOR_SPACE_RGB;
      break;
   case PIPE_FORMAT_NV12: /* FALL THROUGH */
   default:
      enc->enc_pic.enc_input_format.input_color_bit_depth = RENCODE_COLOR_BIT_DEPTH_8_BIT;
      enc->enc_pic.enc_input_format.input_color_packing_format = RENCODE_COLOR_PACKING_FORMAT_NV12;
      enc->enc_pic.enc_input_format.input_chroma_subsampling = RENCODE_CHROMA_SUBSAMPLING_4_2_0;
      enc->enc_pic.enc_input_format.input_color_space = RENCODE_COLOR_SPACE_YUV;
      break;
   }

  enc->enc_pic.enc_input_format.input_color_volume = RENCODE_COLOR_VOLUME_G22_BT709;
  enc->enc_pic.enc_input_format.input_color_range = pic_base->input_full_range ?
     RENCODE_COLOR_RANGE_FULL : RENCODE_COLOR_RANGE_STUDIO;
   enc->enc_pic.enc_input_format.input_chroma_location = RENCODE_CHROMA_LOCATION_INTERSTITIAL;
}

static void radeon_vcn_enc_h264_get_param(struct radeon_encoder *enc,
                                          struct pipe_h264_enc_picture_desc *pic)
{
   bool use_filter;

   enc->enc_pic.h264.desc = pic;
   enc->enc_pic.picture_type = pic->picture_type;
   enc->enc_pic.bit_depth_luma_minus8 = 0;
   enc->enc_pic.bit_depth_chroma_minus8 = 0;
   enc->enc_pic.enc_params.reference_picture_index =
      pic->ref_list0[0] == PIPE_H2645_LIST_REF_INVALID_ENTRY ? 0xffffffff : pic->ref_list0[0];
   enc->enc_pic.h264_enc_params.l1_reference_picture0_index =
      pic->ref_list1[0] == PIPE_H2645_LIST_REF_INVALID_ENTRY ? 0xffffffff : pic->ref_list1[0];
   enc->enc_pic.h264_enc_params.input_picture_structure = RENCODE_H264_PICTURE_STRUCTURE_FRAME;
   enc->enc_pic.h264_enc_params.interlaced_mode = RENCODE_H264_INTERLACING_MODE_PROGRESSIVE;
   enc->enc_pic.h264_enc_params.l0_reference_picture1_index = 0xffffffff;
   enc->enc_pic.enc_params.reconstructed_picture_index = pic->dpb_curr_pic;
   enc->enc_pic.h264_enc_params.is_reference = !pic->not_referenced;
   enc->enc_pic.h264_enc_params.is_long_term = pic->is_ltr;
   enc->enc_pic.not_referenced = pic->not_referenced;

   if ((pic->ref_list0[0] != PIPE_H2645_LIST_REF_INVALID_ENTRY &&
        pic->dpb[pic->ref_list0[0]].picture_type == PIPE_H2645_ENC_PICTURE_TYPE_B) ||
       (pic->ref_list1[0] != PIPE_H2645_LIST_REF_INVALID_ENTRY &&
        pic->dpb[pic->ref_list1[0]].picture_type == PIPE_H2645_ENC_PICTURE_TYPE_B))
      RADEON_ENC_ERR("B-frame references not supported\n");

   if (enc->dpb_type == DPB_TIER_2) {
      for (uint32_t i = 0; i < ARRAY_SIZE(pic->dpb); i++) {
         struct pipe_video_buffer *buf = pic->dpb[i].buffer;
         enc->enc_pic.dpb_bufs[i] =
            buf ? vl_video_buffer_get_associated_data(buf, &enc->base) : NULL;
         assert(!buf || enc->enc_pic.dpb_bufs[i]);
      }
   }

   radeon_vcn_enc_h264_get_session_param(enc, pic);
   radeon_vcn_enc_h264_get_dbk_param(enc, pic);
   radeon_vcn_enc_h264_get_rc_param(enc, pic);
   radeon_vcn_enc_h264_get_spec_misc_param(enc, pic);
   radeon_vcn_enc_h264_get_slice_ctrl_param(enc, pic);
   radeon_vcn_enc_get_input_format_param(enc, &pic->base);
   radeon_vcn_enc_get_output_format_param(enc, pic->seq.video_full_range_flag);

   use_filter = enc->enc_pic.h264_deblock.disable_deblocking_filter_idc != 1;
   radeon_vcn_enc_get_intra_refresh_param(enc, use_filter, &pic->intra_refresh);
   radeon_vcn_enc_get_roi_param(enc, &pic->roi);
   radeon_vcn_enc_get_latency_param(enc);
   radeon_vcn_enc_quality_modes(enc, &pic->quality_modes);
}

static void radeon_vcn_enc_hevc_get_session_param(struct radeon_encoder *enc,
                                                  struct pipe_h265_enc_picture_desc *pic)
{
   if (enc->enc_pic.session_init.aligned_picture_width)
      return;

   uint32_t align_width = PIPE_H265_ENC_CTB_SIZE;
   uint32_t align_height = 16;

   enc->enc_pic.session_init.encode_standard = RENCODE_ENCODE_STANDARD_HEVC;
   enc->enc_pic.session_init.aligned_picture_width = align(enc->base.width, align_width);
   enc->enc_pic.session_init.aligned_picture_height = align(enc->base.height, align_height);

   uint32_t padding_width = 0;
   uint32_t padding_height = 0;
   uint32_t max_padding_width = align_width - 2;
   uint32_t max_padding_height = align_height - 2;

   if (enc->enc_pic.session_init.aligned_picture_width > enc->source->width)
      padding_width = enc->enc_pic.session_init.aligned_picture_width - enc->source->width;
   if (enc->enc_pic.session_init.aligned_picture_height > enc->source->height)
      padding_height = enc->enc_pic.session_init.aligned_picture_height - enc->source->height;

   /* Input surface can be smaller if the difference is within padding bounds. */
   if (padding_width > max_padding_width || padding_height > max_padding_height)
      RADEON_ENC_ERR("Input surface size doesn't match aligned size\n");

   if (pic->seq.conformance_window_flag) {
      uint32_t pad_w =
         (pic->seq.conf_win_left_offset + pic->seq.conf_win_right_offset) * 2;
      uint32_t pad_h =
         (pic->seq.conf_win_top_offset + pic->seq.conf_win_bottom_offset) * 2;
      padding_width = CLAMP(pad_w, padding_width, max_padding_width);
      padding_height = CLAMP(pad_h, padding_height, max_padding_height);
   }

   enc->enc_pic.session_init.padding_width = padding_width;
   enc->enc_pic.session_init.padding_height = padding_height;
}

static void radeon_vcn_enc_hevc_get_dbk_param(struct radeon_encoder *enc,
                                              struct pipe_h265_enc_picture_desc *pic)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;

   enc->enc_pic.hevc_deblock.loop_filter_across_slices_enabled =
      pic->pic.pps_loop_filter_across_slices_enabled_flag;
   enc->enc_pic.hevc_deblock.deblocking_filter_disabled =
      pic->slice.slice_deblocking_filter_disabled_flag;
   enc->enc_pic.hevc_deblock.beta_offset_div2 = pic->slice.slice_beta_offset_div2;
   enc->enc_pic.hevc_deblock.tc_offset_div2 = pic->slice.slice_tc_offset_div2;
   enc->enc_pic.hevc_deblock.cb_qp_offset = pic->slice.slice_cb_qp_offset;
   enc->enc_pic.hevc_deblock.cr_qp_offset = pic->slice.slice_cr_qp_offset;
   enc->enc_pic.hevc_deblock.disable_sao =
      sscreen->info.vcn_ip_version < VCN_2_0_0 ||
      !pic->seq.sample_adaptive_offset_enabled_flag;
}

static void radeon_vcn_enc_hevc_get_spec_misc_param(struct radeon_encoder *enc,
                                                    struct pipe_h265_enc_picture_desc *pic)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;

   enc->enc_pic.hevc_spec_misc.log2_min_luma_coding_block_size_minus3 =
      pic->seq.log2_min_luma_coding_block_size_minus3;
   enc->enc_pic.hevc_spec_misc.amp_disabled = !pic->seq.amp_enabled_flag;
   enc->enc_pic.hevc_spec_misc.strong_intra_smoothing_enabled =
      pic->seq.strong_intra_smoothing_enabled_flag;
   enc->enc_pic.hevc_spec_misc.constrained_intra_pred_flag =
      pic->pic.constrained_intra_pred_flag;
   enc->enc_pic.hevc_spec_misc.cabac_init_flag = pic->slice.cabac_init_flag;
   enc->enc_pic.hevc_spec_misc.half_pel_enabled = 1;
   enc->enc_pic.hevc_spec_misc.quarter_pel_enabled = 1;
   enc->enc_pic.hevc_spec_misc.transform_skip_disabled =
      sscreen->info.vcn_ip_version < VCN_3_0_0 ||
      !pic->pic.transform_skip_enabled_flag;
   enc->enc_pic.hevc_spec_misc.cu_qp_delta_enabled_flag =
      (sscreen->info.vcn_ip_version >= VCN_2_0_0 &&
      pic->pic.cu_qp_delta_enabled_flag) ||
      enc->enc_pic.enc_qp_map.qp_map_type ||
      enc->enc_pic.rc_session_init.rate_control_method;
}

static void radeon_vcn_enc_hevc_get_rc_param(struct radeon_encoder *enc,
                                             struct pipe_h265_enc_picture_desc *pic)
{
   uint32_t frame_rate_den, frame_rate_num, max_qp;

   enc->enc_pic.num_temporal_layers = pic->seq.num_temporal_layers ? pic->seq.num_temporal_layers : 1;
   enc->enc_pic.temporal_id = MIN2(pic->pic.temporal_id, enc->enc_pic.num_temporal_layers - 1);

   for (int i = 0; i < enc->enc_pic.num_temporal_layers; i++) {
      enc->enc_pic.rc_layer_init[i].target_bit_rate = pic->rc[i].target_bitrate;
      enc->enc_pic.rc_layer_init[i].peak_bit_rate = pic->rc[i].peak_bitrate;
      frame_rate_den = pic->rc[i].frame_rate_den;
      frame_rate_num = pic->rc[i].frame_rate_num;
      radeon_vcn_enc_invalid_frame_rate(&frame_rate_den, &frame_rate_num);
      enc->enc_pic.rc_layer_init[i].frame_rate_den = frame_rate_den;
      enc->enc_pic.rc_layer_init[i].frame_rate_num = frame_rate_num;
      enc->enc_pic.rc_layer_init[i].vbv_buffer_size = pic->rc[i].vbv_buffer_size;
      enc->enc_pic.rc_layer_init[i].avg_target_bits_per_picture =
         radeon_vcn_per_frame_integer(pic->rc[i].target_bitrate,
               frame_rate_den,
               frame_rate_num);
      enc->enc_pic.rc_layer_init[i].peak_bits_per_picture_integer =
         radeon_vcn_per_frame_integer(pic->rc[i].peak_bitrate,
               frame_rate_den,
               frame_rate_num);
      enc->enc_pic.rc_layer_init[i].peak_bits_per_picture_fractional =
         radeon_vcn_per_frame_frac(pic->rc[i].peak_bitrate,
               frame_rate_den,
               frame_rate_num);
   }
   enc->enc_pic.rc_session_init.vbv_buffer_level = pic->rc[0].vbv_buf_lv;
   enc->enc_pic.rc_per_pic.qp_obs = pic->rc[0].quant_i_frames;
   enc->enc_pic.rc_per_pic.min_qp_app_obs = pic->rc[0].min_qp;
   enc->enc_pic.rc_per_pic.max_qp_app_obs = pic->rc[0].max_qp ? pic->rc[0].max_qp : 51;
   enc->enc_pic.rc_per_pic.qp_i = pic->rc[0].quant_i_frames;
   enc->enc_pic.rc_per_pic.qp_p = pic->rc[0].quant_p_frames;
   enc->enc_pic.rc_per_pic.min_qp_i = pic->rc[0].min_qp;
   enc->enc_pic.rc_per_pic.min_qp_p = pic->rc[0].min_qp;
   max_qp = pic->rc[0].max_qp ? pic->rc[0].max_qp : 51;
   enc->enc_pic.rc_per_pic.max_qp_i = max_qp;
   enc->enc_pic.rc_per_pic.max_qp_p = max_qp;
   enc->enc_pic.rc_per_pic.enabled_filler_data = 0;
   enc->enc_pic.rc_per_pic.skip_frame_enable = pic->rc[0].skip_frame_enable;
   enc->enc_pic.rc_per_pic.enforce_hrd = pic->rc[0].enforce_hrd;
   enc->enc_pic.rc_per_pic.qvbr_quality_level = pic->rc[0].vbr_quality_factor;
   switch (pic->rc[0].rate_ctrl_method) {
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE:
         enc->enc_pic.rc_session_init.rate_control_method = RENCODE_RATE_CONTROL_METHOD_NONE;
         break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT_SKIP:
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT:
         enc->enc_pic.rc_session_init.rate_control_method = RENCODE_RATE_CONTROL_METHOD_CBR;
         enc->enc_pic.rc_per_pic.enabled_filler_data = pic->rc[0].fill_data_enable;
         break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE_SKIP:
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE:
         enc->enc_pic.rc_session_init.rate_control_method =
            RENCODE_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
         break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_QUALITY_VARIABLE:
         enc->enc_pic.rc_session_init.rate_control_method =
            RENCODE_RATE_CONTROL_METHOD_QUALITY_VBR;
         break;
      default:
         enc->enc_pic.rc_session_init.rate_control_method = RENCODE_RATE_CONTROL_METHOD_NONE;
   }
   enc->enc_pic.rc_per_pic.max_au_size_obs = pic->rc[0].max_au_size;
   enc->enc_pic.rc_per_pic.max_au_size_i = pic->rc[0].max_au_size;
   enc->enc_pic.rc_per_pic.max_au_size_p = pic->rc[0].max_au_size;
}

static void radeon_vcn_enc_hevc_get_slice_ctrl_param(struct radeon_encoder *enc,
                                                     struct pipe_h265_enc_picture_desc *pic)
{
   uint32_t num_ctbs_total, num_ctbs_in_slice;

   num_ctbs_total =
      PIPE_ALIGN_IN_BLOCK_SIZE(pic->seq.pic_width_in_luma_samples, PIPE_H265_ENC_CTB_SIZE) *
      PIPE_ALIGN_IN_BLOCK_SIZE(pic->seq.pic_height_in_luma_samples, PIPE_H265_ENC_CTB_SIZE);

   if (pic->num_slice_descriptors <= 1) {
      num_ctbs_in_slice = num_ctbs_total;
   } else {
      bool use_app_config = true;
      num_ctbs_in_slice = pic->slices_descriptors[0].num_ctu_in_slice;

      /* All slices must have equal size */
      for (unsigned i = 1; i < pic->num_slice_descriptors - 1; i++) {
         if (num_ctbs_in_slice != pic->slices_descriptors[i].num_ctu_in_slice)
            use_app_config = false;
      }
      /* Except last one can be smaller */
      if (pic->slices_descriptors[pic->num_slice_descriptors - 1].num_ctu_in_slice > num_ctbs_in_slice)
         use_app_config = false;

      if (!use_app_config) {
         assert(num_ctbs_total >= pic->num_slice_descriptors);
         num_ctbs_in_slice =
            (num_ctbs_total + pic->num_slice_descriptors - 1) / pic->num_slice_descriptors;
      }
   }

   num_ctbs_in_slice = MAX2(4, num_ctbs_in_slice);

   enc->enc_pic.hevc_slice_ctrl.slice_control_mode = RENCODE_HEVC_SLICE_CONTROL_MODE_FIXED_CTBS;
   enc->enc_pic.hevc_slice_ctrl.fixed_ctbs_per_slice.num_ctbs_per_slice =
      num_ctbs_in_slice;
   enc->enc_pic.hevc_slice_ctrl.fixed_ctbs_per_slice.num_ctbs_per_slice_segment =
      num_ctbs_in_slice;
}

static void radeon_vcn_enc_hevc_get_param(struct radeon_encoder *enc,
                                          struct pipe_h265_enc_picture_desc *pic)
{
   enc->enc_pic.hevc.desc = pic;
   enc->enc_pic.picture_type = pic->picture_type;
   enc->enc_pic.enc_params.reference_picture_index =
      pic->ref_list0[0] == PIPE_H2645_LIST_REF_INVALID_ENTRY ? 0xffffffff : pic->ref_list0[0];
   enc->enc_pic.enc_params.reconstructed_picture_index = pic->dpb_curr_pic;
   enc->enc_pic.bit_depth_luma_minus8 = pic->seq.bit_depth_luma_minus8;
   enc->enc_pic.bit_depth_chroma_minus8 = pic->seq.bit_depth_chroma_minus8;
   enc->enc_pic.nal_unit_type = pic->pic.nal_unit_type;

   if (enc->dpb_type == DPB_TIER_2) {
      for (uint32_t i = 0; i < ARRAY_SIZE(pic->dpb); i++) {
         struct pipe_video_buffer *buf = pic->dpb[i].buffer;
         enc->enc_pic.dpb_bufs[i] =
            buf ? vl_video_buffer_get_associated_data(buf, &enc->base) : NULL;
         assert(!buf || enc->enc_pic.dpb_bufs[i]);
      }
   }

   radeon_vcn_enc_hevc_get_session_param(enc, pic);
   radeon_vcn_enc_hevc_get_dbk_param(enc, pic);
   radeon_vcn_enc_hevc_get_rc_param(enc, pic);
   radeon_vcn_enc_hevc_get_slice_ctrl_param(enc, pic);
   radeon_vcn_enc_get_input_format_param(enc, &pic->base);
   radeon_vcn_enc_get_output_format_param(enc, pic->seq.video_full_range_flag);
   radeon_vcn_enc_get_intra_refresh_param(enc,
                                        !(enc->enc_pic.hevc_deblock.deblocking_filter_disabled),
                                         &pic->intra_refresh);
   radeon_vcn_enc_get_roi_param(enc, &pic->roi);
   radeon_vcn_enc_hevc_get_spec_misc_param(enc, pic);
   radeon_vcn_enc_get_latency_param(enc);
   radeon_vcn_enc_quality_modes(enc, &pic->quality_modes);
}

static void radeon_vcn_enc_av1_get_session_param(struct radeon_encoder *enc,
                                                 struct pipe_av1_enc_picture_desc *pic)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;

   if (enc->enc_pic.session_init.aligned_picture_width)
      return;

   enc->enc_pic.session_init.encode_standard = RENCODE_ENCODE_STANDARD_AV1;

   uint32_t width = enc->enc_pic.pic_width_in_luma_samples;
   uint32_t height = enc->enc_pic.pic_height_in_luma_samples;
   uint32_t align_width, align_height;

   if (sscreen->info.vcn_ip_version < VCN_5_0_0) {
      align_width = PIPE_AV1_ENC_SB_SIZE;
      align_height = 16;
      enc->enc_pic.session_init.aligned_picture_width = align(width, align_width);
      enc->enc_pic.session_init.aligned_picture_height = align(height, align_height);
      if (!(height % 8) && (height % 16))
         enc->enc_pic.session_init.aligned_picture_height = height + 2;
      if (sscreen->info.vcn_ip_version == VCN_4_0_2 ||
          sscreen->info.vcn_ip_version == VCN_4_0_5 ||
          sscreen->info.vcn_ip_version == VCN_4_0_6)
         enc->enc_pic.session_init.WA_flags = 1;
   } else {
      align_width = 8;
      align_height = 2;
      enc->enc_pic.session_init.aligned_picture_width = align(width, align_width);
      enc->enc_pic.session_init.aligned_picture_height = align(height, align_height);
   }
   enc->enc_pic.av1.coded_width = enc->enc_pic.session_init.aligned_picture_width;
   enc->enc_pic.av1.coded_height = enc->enc_pic.session_init.aligned_picture_height;

   uint32_t padding_width = 0;
   uint32_t padding_height = 0;
   uint32_t max_padding_width = align_width - 2;
   uint32_t max_padding_height = align_height - 2;

   if (enc->enc_pic.session_init.aligned_picture_width > enc->source->width)
      padding_width = enc->enc_pic.session_init.aligned_picture_width - enc->source->width;
   if (enc->enc_pic.session_init.aligned_picture_height > enc->source->height)
      padding_height = enc->enc_pic.session_init.aligned_picture_height - enc->source->height;

   /* Input surface can be smaller if the difference is within padding bounds. */
   if (padding_width > max_padding_width || padding_height > max_padding_height)
      RADEON_ENC_ERR("Input surface size doesn't match aligned size\n");

   padding_width = MAX2(padding_width, enc->enc_pic.session_init.aligned_picture_width - width);
   padding_height = MAX2(padding_height, enc->enc_pic.session_init.aligned_picture_height - height);

   enc->enc_pic.session_init.padding_width = padding_width;
   enc->enc_pic.session_init.padding_height = padding_height;
}

static void radeon_vcn_enc_av1_get_spec_misc_param(struct radeon_encoder *enc,
                                                   struct pipe_av1_enc_picture_desc *pic)
{
   enc->enc_pic.av1_spec_misc.cdef_mode = pic->seq.seq_bits.enable_cdef;
   enc->enc_pic.av1_spec_misc.disable_cdf_update = pic->disable_cdf_update;
   enc->enc_pic.av1_spec_misc.disable_frame_end_update_cdf = pic->disable_frame_end_update_cdf;
   enc->enc_pic.av1_spec_misc.palette_mode_enable = pic->palette_mode_enable;
   enc->enc_pic.av1_spec_misc.cdef_bits = pic->cdef.cdef_bits;
   enc->enc_pic.av1_spec_misc.cdef_damping_minus3 = pic->cdef.cdef_damping_minus_3;
   for (int i = 0; i < (pic->cdef.cdef_bits << 1); i++ ){
      enc->enc_pic.av1_spec_misc.cdef_y_pri_strength[i] = (pic->cdef.cdef_y_strengths[i] >> 2);
      enc->enc_pic.av1_spec_misc.cdef_y_sec_strength[i] = (pic->cdef.cdef_y_strengths[i] & 0x3);
      enc->enc_pic.av1_spec_misc.cdef_uv_pri_strength[i] = (pic->cdef.cdef_uv_strengths[i] >> 2);
      enc->enc_pic.av1_spec_misc.cdef_uv_sec_strength[i] = (pic->cdef.cdef_uv_strengths[i] & 0x3);
   }

   enc->enc_pic.av1_spec_misc.delta_q_y_dc = pic->quantization.y_dc_delta_q;
   enc->enc_pic.av1_spec_misc.delta_q_u_dc = pic->quantization.u_dc_delta_q;
   enc->enc_pic.av1_spec_misc.delta_q_u_ac = pic->quantization.u_ac_delta_q;
   enc->enc_pic.av1_spec_misc.delta_q_v_dc = pic->quantization.v_dc_delta_q;
   enc->enc_pic.av1_spec_misc.delta_q_v_ac = pic->quantization.v_ac_delta_q;

   if (enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_KEY)
      enc->enc_pic.av1_spec_misc.separate_delta_q =
         (pic->quantization.u_dc_delta_q != pic->quantization.v_dc_delta_q) ||
         (pic->quantization.u_ac_delta_q != pic->quantization.v_ac_delta_q);

   if (enc->enc_pic.disable_screen_content_tools) {
       enc->enc_pic.force_integer_mv  = 0;
       enc->enc_pic.av1_spec_misc.palette_mode_enable = 0;
   }

   if (enc->enc_pic.force_integer_mv)
      enc->enc_pic.av1_spec_misc.mv_precision = RENCODE_AV1_MV_PRECISION_FORCE_INTEGER_MV;
   else
      enc->enc_pic.av1_spec_misc.mv_precision = RENCODE_AV1_MV_PRECISION_ALLOW_HIGH_PRECISION;
}

static void radeon_vcn_enc_av1_get_rc_param(struct radeon_encoder *enc,
                                            struct pipe_av1_enc_picture_desc *pic)
{
   uint32_t frame_rate_den, frame_rate_num, min_qp, max_qp;

   enc->enc_pic.num_temporal_layers = pic->seq.num_temporal_layers ? pic->seq.num_temporal_layers : 1;
   enc->enc_pic.temporal_id = MIN2(pic->temporal_id, enc->enc_pic.num_temporal_layers - 1);

   for (int i = 0; i < ARRAY_SIZE(enc->enc_pic.rc_layer_init); i++) {
      enc->enc_pic.rc_layer_init[i].target_bit_rate = pic->rc[i].target_bitrate;
      enc->enc_pic.rc_layer_init[i].peak_bit_rate = pic->rc[i].peak_bitrate;
      frame_rate_den = pic->rc[i].frame_rate_den;
      frame_rate_num = pic->rc[i].frame_rate_num;
      radeon_vcn_enc_invalid_frame_rate(&frame_rate_den, &frame_rate_num);
      enc->enc_pic.rc_layer_init[i].frame_rate_den = frame_rate_den;
      enc->enc_pic.rc_layer_init[i].frame_rate_num = frame_rate_num;
      enc->enc_pic.rc_layer_init[i].vbv_buffer_size = pic->rc[i].vbv_buffer_size;
      enc->enc_pic.rc_layer_init[i].avg_target_bits_per_picture =
          radeon_vcn_per_frame_integer(pic->rc[i].target_bitrate,
                                       frame_rate_den,
                                       frame_rate_num);
      enc->enc_pic.rc_layer_init[i].peak_bits_per_picture_integer =
          radeon_vcn_per_frame_integer(pic->rc[i].peak_bitrate,
                                       frame_rate_den,
                                       frame_rate_num);
      enc->enc_pic.rc_layer_init[i].peak_bits_per_picture_fractional =
          radeon_vcn_per_frame_frac(pic->rc[i].peak_bitrate,
                                    frame_rate_den,
                                    frame_rate_num);
   }
   enc->enc_pic.rc_session_init.vbv_buffer_level = pic->rc[0].vbv_buf_lv;
   enc->enc_pic.rc_per_pic.qp_obs = pic->rc[0].qp;
   enc->enc_pic.rc_per_pic.min_qp_app_obs = pic->rc[0].min_qp ? pic->rc[0].min_qp : 1;
   enc->enc_pic.rc_per_pic.max_qp_app_obs = pic->rc[0].max_qp ? pic->rc[0].max_qp : 255;
   enc->enc_pic.rc_per_pic.qp_i = pic->rc[0].qp;
   enc->enc_pic.rc_per_pic.qp_p = pic->rc[0].qp_inter;
   enc->enc_pic.rc_per_pic.qp_b = pic->rc[0].qp_inter;
   min_qp = pic->rc[0].min_qp ? pic->rc[0].min_qp : 1;
   enc->enc_pic.rc_per_pic.min_qp_i = min_qp;
   enc->enc_pic.rc_per_pic.min_qp_p = min_qp;
   enc->enc_pic.rc_per_pic.min_qp_b = min_qp;
   max_qp = pic->rc[0].max_qp ? pic->rc[0].max_qp : 255;
   enc->enc_pic.rc_per_pic.max_qp_i = max_qp;
   enc->enc_pic.rc_per_pic.max_qp_p = max_qp;
   enc->enc_pic.rc_per_pic.max_qp_b = max_qp;
   enc->enc_pic.rc_per_pic.enabled_filler_data = 0;
   enc->enc_pic.rc_per_pic.skip_frame_enable = pic->rc[0].skip_frame_enable;
   enc->enc_pic.rc_per_pic.enforce_hrd = pic->rc[0].enforce_hrd;
   enc->enc_pic.rc_per_pic.qvbr_quality_level = (pic->rc[0].vbr_quality_factor + 2) / 5;
   switch (pic->rc[0].rate_ctrl_method) {
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE:
         enc->enc_pic.rc_session_init.rate_control_method = RENCODE_RATE_CONTROL_METHOD_NONE;
         break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT_SKIP:
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT:
         enc->enc_pic.rc_session_init.rate_control_method = RENCODE_RATE_CONTROL_METHOD_CBR;
         enc->enc_pic.rc_per_pic.enabled_filler_data = pic->rc[0].fill_data_enable;
         break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE_SKIP:
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE:
         enc->enc_pic.rc_session_init.rate_control_method =
            RENCODE_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
         break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_QUALITY_VARIABLE:
         enc->enc_pic.rc_session_init.rate_control_method =
            RENCODE_RATE_CONTROL_METHOD_QUALITY_VBR;
         break;
      default:
         enc->enc_pic.rc_session_init.rate_control_method = RENCODE_RATE_CONTROL_METHOD_NONE;
   }
   enc->enc_pic.rc_per_pic.max_au_size_obs = pic->rc[0].max_au_size;
   enc->enc_pic.rc_per_pic.max_au_size_i = pic->rc[0].max_au_size;
   enc->enc_pic.rc_per_pic.max_au_size_p = pic->rc[0].max_au_size;
   enc->enc_pic.rc_per_pic.max_au_size_b = pic->rc[0].max_au_size;
}

static void radeon_vcn_enc_av1_get_tile_config(struct radeon_encoder *enc,
                                               struct pipe_av1_enc_picture_desc *pic)
{
   uint32_t num_tile_cols, num_tile_rows;

   num_tile_cols = MIN2(RENCODE_AV1_TILE_CONFIG_MAX_NUM_COLS, pic->tile_cols);
   num_tile_rows = MIN2(RENCODE_AV1_TILE_CONFIG_MAX_NUM_ROWS, pic->tile_rows);

   enc->enc_pic.av1_tile_config.uniform_tile_spacing = !!(pic->uniform_tile_spacing);
   enc->enc_pic.av1_tile_config.num_tile_cols = pic->tile_cols;
   enc->enc_pic.av1_tile_config.num_tile_rows = pic->tile_rows;
   enc->enc_pic.av1_tile_config.num_tile_groups = pic->num_tile_groups;
   for (int i = 0; i < num_tile_cols; i++ )
      enc->enc_pic.av1_tile_config.tile_widths[i] = pic->width_in_sbs_minus_1[i] + 1;
   for (int i = 0; i < num_tile_rows; i++ )
      enc->enc_pic.av1_tile_config.tile_height[i] = pic->height_in_sbs_minus_1[i] + 1;
   for (int i = 0; i < num_tile_cols * num_tile_rows; i++ ) {
      enc->enc_pic.av1_tile_config.tile_groups[i].start =
         (uint32_t)pic->tile_groups[i].tile_group_start;
      enc->enc_pic.av1_tile_config.tile_groups[i].end =
         (uint32_t)pic->tile_groups[i].tile_group_end;
   }
   enc->enc_pic.av1_tile_config.context_update_tile_id = pic->context_update_tile_id;
}

static void radeon_vcn_enc_av1_get_param(struct radeon_encoder *enc,
                                         struct pipe_av1_enc_picture_desc *pic)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;
   struct radeon_enc_pic *enc_pic = &enc->enc_pic;

   enc_pic->av1.desc = pic;
   enc_pic->frame_type = pic->frame_type;
   enc_pic->bit_depth_luma_minus8 = enc_pic->bit_depth_chroma_minus8 =
      pic->seq.bit_depth_minus8;
   enc_pic->pic_width_in_luma_samples = pic->seq.pic_width_in_luma_samples;
   enc_pic->pic_height_in_luma_samples = pic->seq.pic_height_in_luma_samples;
   enc_pic->enable_error_resilient_mode = pic->error_resilient_mode;
   enc_pic->force_integer_mv = pic->force_integer_mv;
   enc_pic->disable_screen_content_tools = !pic->allow_screen_content_tools;
   enc_pic->is_obu_frame = pic->enable_frame_obu;

   enc_pic->enc_params.reference_picture_index =
      pic->ref_list0[0] == PIPE_H2645_LIST_REF_INVALID_ENTRY ?
      0xffffffff : pic->dpb_ref_frame_idx[pic->ref_list0[0]];
   enc_pic->enc_params.reconstructed_picture_index = pic->dpb_curr_pic;

   if (sscreen->info.vcn_ip_version >= VCN_5_0_0) {
      for (uint32_t i = 0; i < RENCODE_AV1_REFS_PER_FRAME; i++)
         enc_pic->av1_enc_params.ref_frames[i] = pic->dpb_ref_frame_idx[i];

      enc_pic->av1_enc_params.lsm_reference_frame_index[0] =
         pic->ref_list0[0] == PIPE_H2645_LIST_REF_INVALID_ENTRY ? 0xffffffff : pic->ref_list0[0];
      enc_pic->av1_enc_params.lsm_reference_frame_index[1] = 0xffffffff;
      enc_pic->av1.compound = false;

      if (pic->ref_list1[0] != PIPE_H2645_LIST_REF_INVALID_ENTRY) {
         enc_pic->av1.compound = true; /* BIDIR_COMP */
         enc_pic->av1_enc_params.lsm_reference_frame_index[1] = pic->ref_list1[0];
      } else if (pic->ref_list0[1] != PIPE_H2645_LIST_REF_INVALID_ENTRY) {
         enc_pic->av1.compound = true; /* UNIDIR_COMP */
         enc_pic->av1_enc_params.lsm_reference_frame_index[1] = pic->ref_list0[1];
      }

      uint32_t skip_frames[2];
      enc_pic->av1.skip_mode_allowed = radeon_enc_av1_skip_mode_allowed(enc, skip_frames);

      if (enc_pic->av1.compound) {
         bool disallow_skip_mode = enc_pic->av1_spec_misc.disallow_skip_mode;
         enc_pic->av1_spec_misc.disallow_skip_mode = !enc_pic->av1.skip_mode_allowed;
         /* Skip mode frames must match reference frames */
         if (enc_pic->av1.skip_mode_allowed) {
            enc_pic->av1_spec_misc.disallow_skip_mode =
               skip_frames[0] != enc_pic->av1_enc_params.lsm_reference_frame_index[0] ||
               skip_frames[1] != enc_pic->av1_enc_params.lsm_reference_frame_index[1];
         }
         enc->need_spec_misc = disallow_skip_mode != enc_pic->av1_spec_misc.disallow_skip_mode;
      } else {
         enc->need_spec_misc = false;
      }
   }

   if (enc->dpb_type == DPB_TIER_2) {
      for (uint32_t i = 0; i < ARRAY_SIZE(pic->dpb); i++) {
         struct pipe_video_buffer *buf = pic->dpb[i].buffer;
         enc->enc_pic.dpb_bufs[i] =
            buf ? vl_video_buffer_get_associated_data(buf, &enc->base) : NULL;
         assert(!buf || enc->enc_pic.dpb_bufs[i]);
      }
   }

   radeon_vcn_enc_av1_get_session_param(enc, pic);
   radeon_vcn_enc_av1_get_spec_misc_param(enc, pic);
   radeon_vcn_enc_av1_get_rc_param(enc, pic);
   radeon_vcn_enc_av1_get_tile_config(enc, pic);
   radeon_vcn_enc_get_input_format_param(enc, &pic->base);
   radeon_vcn_enc_get_output_format_param(enc, pic->seq.color_config.color_range);
   /* loop filter enabled all the time */
   radeon_vcn_enc_get_intra_refresh_param(enc,
                                         true,
                                         &pic->intra_refresh);
   radeon_vcn_enc_get_roi_param(enc, &pic->roi);
   radeon_vcn_enc_get_latency_param(enc);
   radeon_vcn_enc_quality_modes(enc, &pic->quality_modes);
}

static void radeon_vcn_enc_get_param(struct radeon_encoder *enc, struct pipe_picture_desc *picture)
{
   enc->enc_pic.enc_params.allowed_max_bitstream_size = enc->bs_size - enc->bs_offset;

   if (u_reduce_video_profile(picture->profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC)
      radeon_vcn_enc_h264_get_param(enc, (struct pipe_h264_enc_picture_desc *)picture);
   else if (u_reduce_video_profile(picture->profile) == PIPE_VIDEO_FORMAT_HEVC)
      radeon_vcn_enc_hevc_get_param(enc, (struct pipe_h265_enc_picture_desc *)picture);
   else if (u_reduce_video_profile(picture->profile) == PIPE_VIDEO_FORMAT_AV1)
      radeon_vcn_enc_av1_get_param(enc, (struct pipe_av1_enc_picture_desc *)picture);
}

static int flush(struct radeon_encoder *enc, unsigned flags, struct pipe_fence_handle **fence)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;

   if (sscreen->debug_flags & DBG(IB)) {
      struct ac_ib_parser ib_parser = {
         .f = stderr,
         .ib = enc->cs.current.buf,
         .num_dw = enc->cs.current.cdw,
         .gfx_level = sscreen->info.gfx_level,
         .vcn_version = sscreen->info.vcn_ip_version,
         .family = sscreen->info.family,
         .ip_type = AMD_IP_VCN_ENC,
      };
      ac_parse_ib(&ib_parser, "IB");
   }

   return enc->ws->cs_flush(&enc->cs, flags, fence);
}

static void radeon_enc_flush(struct pipe_video_codec *encoder)
{
   struct radeon_encoder *enc = (struct radeon_encoder *)encoder;
   flush(enc, PIPE_FLUSH_ASYNC, NULL);
}

static void radeon_enc_cs_flush(void *ctx, unsigned flags, struct pipe_fence_handle **fence)
{
   // just ignored
}

/* configure reconstructed picture offset */
static void radeon_enc_rec_offset(rvcn_enc_reconstructed_picture_t *recon,
                                  uint32_t *offset,
                                  uint32_t luma_size,
                                  uint32_t chroma_size,
                                  bool is_av1)
{
   if (offset) {
      recon->luma_offset = *offset;
      *offset += luma_size;
      recon->chroma_offset = *offset;
      *offset += chroma_size;
      if (is_av1) {
         recon->av1.av1_cdf_frame_context_offset = *offset;
         *offset += RENCODE_AV1_FRAME_CONTEXT_CDF_TABLE_SIZE;
         recon->av1.av1_cdef_algorithm_context_offset = *offset;
         *offset += RENCODE_AV1_CDEF_ALGORITHM_FRAME_CONTEXT_SIZE;
      }
   } else {
      recon->luma_offset = 0;
      recon->chroma_offset = 0;
      recon->av1.av1_cdf_frame_context_offset = 0;
      recon->av1.av1_cdef_algorithm_context_offset = 0;
   }
   recon->chroma_v_offset = 0;
}

/* configure reconstructed picture offset */
static void radeon_enc_rec_meta_offset(rvcn_enc_reconstructed_picture_t *recon,
                                  uint32_t *offset,
                                  uint32_t total_coloc_size,
                                  uint32_t alignment,
                                  bool has_b,
                                  bool is_h264,
                                  bool is_av1)
{
   uint32_t context_offset = 0;

   if (offset) {
      recon->frame_context_buffer_offset = *offset;
      recon->encode_metadata_offset = context_offset;
      context_offset += RENCODE_MAX_METADATA_BUFFER_SIZE_PER_FRAME;
      if (is_h264) {
         if (has_b) {
            recon->h264.colloc_buffer_offset = context_offset;
            context_offset += total_coloc_size;
         } else
            recon->h264.colloc_buffer_offset = RENCODE_INVALID_COLOC_OFFSET;
      }

      if (is_av1) {
         recon->av1.av1_cdf_frame_context_offset = context_offset;
         context_offset += RENCODE_AV1_FRAME_CONTEXT_CDF_TABLE_SIZE;
         recon->av1.av1_cdef_algorithm_context_offset = context_offset;
         context_offset += RENCODE_AV1_CDEF_ALGORITHM_FRAME_CONTEXT_SIZE;
      }
      context_offset = align(context_offset, alignment);
      *offset += context_offset;
   } else {
      recon->frame_context_buffer_offset = 0;
      recon->encode_metadata_offset = 0;
      recon->av1.av1_cdf_frame_context_offset = 0;
      recon->av1.av1_cdef_algorithm_context_offset = 0;
   }
}

static int setup_cdf(struct radeon_encoder *enc)
{
   unsigned char *p_cdf = NULL;

   if (!enc->cdf ||
         !si_vid_create_buffer(enc->screen,
                               enc->cdf,
                               VCN_ENC_AV1_DEFAULT_CDF_SIZE,
                               PIPE_USAGE_DYNAMIC)) {
      RADEON_ENC_ERR("Can't create CDF buffer.\n");
      goto error;
   }

   p_cdf = enc->ws->buffer_map(enc->ws,
                               enc->cdf->res->buf,
                              &enc->cs,
                               PIPE_MAP_READ_WRITE | RADEON_MAP_TEMPORARY);
   if (!p_cdf)
      goto error;

   memcpy(p_cdf, rvcn_av1_cdf_default_table, VCN_ENC_AV1_DEFAULT_CDF_SIZE);
   enc->ws->buffer_unmap(enc->ws, enc->cdf->res->buf);

   return 0;

error:
   return -1;
}

static void pre_encode_size(struct radeon_encoder *enc,
                            uint32_t *offset)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;
   bool is_h264 = u_reduce_video_profile(enc->base.profile)
                             == PIPE_VIDEO_FORMAT_MPEG4_AVC;
   uint32_t rec_alignment = is_h264 ? 16 : 64;
   uint32_t aligned_width = align(enc->base.width, rec_alignment);
   uint32_t aligned_height = align(enc->base.height, rec_alignment);
   struct radeon_enc_pic *enc_pic = &enc->enc_pic;
   bool has_b = enc_pic->spec_misc.b_picture_enabled; /* for h264 only */
   uint32_t pre_size  = DIV_ROUND_UP((aligned_width >> 2), rec_alignment) *
      DIV_ROUND_UP((aligned_height >> 2), rec_alignment);
   uint32_t full_size = DIV_ROUND_UP(aligned_width, rec_alignment) *
      DIV_ROUND_UP(aligned_height, rec_alignment);

   enc_pic->ctx_buf.two_pass_search_center_map_offset = *offset;

   if (sscreen->info.vcn_ip_version < VCN_5_0_0) {
      if (is_h264 && !has_b)
         *offset += align((pre_size * 4 + full_size) * sizeof(uint32_t), enc->alignment);
      else if (!is_h264)
         *offset += align((pre_size * 52 + full_size) * sizeof(uint32_t), enc->alignment);
   } else { /* only for vcn5.x rather than VCN5_0_0 */
      if (is_h264 && !has_b)
         *offset += align(full_size * 8, enc->alignment);
      else if (!is_h264)
         *offset += align(full_size * 24, enc->alignment);
   }
}

static int setup_dpb(struct radeon_encoder *enc, uint32_t num_reconstructed_pictures)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;
   bool is_h264 = u_reduce_video_profile(enc->base.profile)
                             == PIPE_VIDEO_FORMAT_MPEG4_AVC;
   bool is_av1 = u_reduce_video_profile(enc->base.profile)
                             == PIPE_VIDEO_FORMAT_AV1;
   uint32_t rec_alignment = is_h264 ? 16 : 64;
   uint32_t aligned_width = align(enc->base.width, rec_alignment);
   uint32_t aligned_height = align(enc->base.height, rec_alignment);
   uint32_t pitch = align(aligned_width, enc->alignment);
   uint32_t luma_size, chroma_size, offset;
   struct radeon_enc_pic *enc_pic = &enc->enc_pic;
   int i;
   bool has_b = enc_pic->spec_misc.b_picture_enabled; /* for h264 only */
   uint32_t aligned_dpb_height = MAX2(256, aligned_height);
   uint32_t total_coloc_bytes = (align((aligned_width / 16), 64) / 2)
                                 * (aligned_height / 16);

   luma_size = align(pitch * aligned_dpb_height , enc->alignment);
   chroma_size = align(luma_size / 2 , enc->alignment);
   if (enc_pic->bit_depth_luma_minus8 || enc_pic->bit_depth_chroma_minus8) {
      luma_size *= 2;
      chroma_size *= 2;
   }

   assert(num_reconstructed_pictures <= RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES);

   enc_pic->ctx_buf.rec_luma_pitch   = pitch;
   enc_pic->ctx_buf.pre_encode_picture_luma_pitch   = pitch;
   enc_pic->ctx_buf.num_reconstructed_pictures = num_reconstructed_pictures;
   enc_pic->total_coloc_bytes = total_coloc_bytes;

   offset = 0;
   enc->metadata_size = 0;
   if (sscreen->info.vcn_ip_version < VCN_5_0_0) {
      enc_pic->ctx_buf.rec_chroma_pitch = pitch;
      enc_pic->ctx_buf.pre_encode_picture_chroma_pitch = pitch;
      if (has_b) {
         enc_pic->ctx_buf.colloc_buffer_offset = offset;
         offset += total_coloc_bytes;
      } else
         enc_pic->ctx_buf.colloc_buffer_offset = 0;

      if (enc_pic->quality_modes.pre_encode_mode)
         pre_encode_size(enc, &offset);
      else
         enc_pic->ctx_buf.two_pass_search_center_map_offset = 0;

      if (enc_pic->quality_modes.pre_encode_mode) {
         enc_pic->ctx_buf.pre_encode_input_picture.rgb.red_offset = offset;
         offset += luma_size;
         enc_pic->ctx_buf.pre_encode_input_picture.rgb.green_offset = offset;
         offset += luma_size;
         enc_pic->ctx_buf.pre_encode_input_picture.rgb.blue_offset = offset;
         offset += luma_size;
      }

      if (is_av1) {
         enc_pic->ctx_buf.av1.av1_sdb_intermediate_context_offset = offset;
         offset += RENCODE_AV1_SDB_FRAME_CONTEXT_SIZE;
      }

      for (i = 0; i < num_reconstructed_pictures; i++) {
         radeon_enc_rec_offset(&enc_pic->ctx_buf.reconstructed_pictures[i],
                               &offset, luma_size, chroma_size, is_av1);

         if (enc_pic->quality_modes.pre_encode_mode)
            radeon_enc_rec_offset(&enc_pic->ctx_buf.pre_encode_reconstructed_pictures[i],
                                  &offset, luma_size, chroma_size, is_av1);
      }

      for (; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
         radeon_enc_rec_offset(&enc_pic->ctx_buf.reconstructed_pictures[i],
                               NULL, 0, 0, false);
         if (enc_pic->quality_modes.pre_encode_mode)
            radeon_enc_rec_offset(&enc_pic->ctx_buf.pre_encode_reconstructed_pictures[i],
                                  NULL, 0, 0, false);
      }

      enc->dpb_size = offset;
   } else { /* vcn 5.0 */
      enc_pic->ctx_buf.rec_chroma_pitch = pitch / 2;
      enc_pic->ctx_buf.pre_encode_picture_chroma_pitch = pitch / 2;
      /* dpb buffer */
      if (is_av1) {
         enc_pic->ctx_buf.av1.av1_sdb_intermediate_context_offset = offset;
         offset += RENCODE_AV1_SDB_FRAME_CONTEXT_SIZE;
      } else
         enc_pic->ctx_buf.av1.av1_sdb_intermediate_context_offset = 0;

      if (enc_pic->quality_modes.pre_encode_mode) {
         enc_pic->ctx_buf.pre_encode_input_picture.rgb.red_offset = offset;
         offset += luma_size;
         enc_pic->ctx_buf.pre_encode_input_picture.rgb.green_offset = offset;
         offset += luma_size;
         enc_pic->ctx_buf.pre_encode_input_picture.rgb.blue_offset = offset;
         offset += luma_size;
      }

      for (i = 0; i < num_reconstructed_pictures; i++) {
         radeon_enc_rec_offset(&enc_pic->ctx_buf.reconstructed_pictures[i],
                               &offset, luma_size, chroma_size, false);

         if (enc_pic->quality_modes.pre_encode_mode)
            radeon_enc_rec_offset(&enc_pic->ctx_buf.pre_encode_reconstructed_pictures[i],
                                  &offset, luma_size, chroma_size, false);
      }

      for (; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
         radeon_enc_rec_offset(&enc_pic->ctx_buf.reconstructed_pictures[i],
                               NULL, 0, 0, false);
         if (enc_pic->quality_modes.pre_encode_mode)
            radeon_enc_rec_offset(&enc_pic->ctx_buf.pre_encode_reconstructed_pictures[i],
                                  NULL, 0, 0, false);
      }

      enc->dpb_size = offset;

      /* meta buffer*/
      offset = 0;
      if (enc_pic->quality_modes.pre_encode_mode)
         pre_encode_size(enc, &offset);
      else
         enc_pic->ctx_buf.two_pass_search_center_map_offset = 0;

      for (i = 0; i < num_reconstructed_pictures; i++) {
         radeon_enc_rec_meta_offset(&enc_pic->ctx_buf.reconstructed_pictures[i],
                               &offset, total_coloc_bytes, enc->alignment, has_b, is_h264, is_av1);
         if (enc_pic->quality_modes.pre_encode_mode)
            radeon_enc_rec_meta_offset(&enc_pic->ctx_buf.pre_encode_reconstructed_pictures[i],
                               &offset, total_coloc_bytes, enc->alignment, has_b, is_h264, is_av1);
      }
      for (; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
         radeon_enc_rec_meta_offset(&enc_pic->ctx_buf.reconstructed_pictures[i],
                               NULL, 0, 0, false, false, false);
         if (enc_pic->quality_modes.pre_encode_mode)
            radeon_enc_rec_meta_offset(&enc_pic->ctx_buf.pre_encode_reconstructed_pictures[i],
                               NULL, 0, 0, false, false, false);
      }
      enc->metadata_size = offset;
   }

   enc->dpb_slots = num_reconstructed_pictures;

   return enc->dpb_size;
}

/* each block (MB/CTB/SB) has one QP/QI value */
static uint32_t roi_buffer_size(struct radeon_encoder *enc)
{
   uint32_t pitch_size_in_dword = 0;
   rvcn_enc_qp_map_t *qp_map = &enc->enc_pic.enc_qp_map;

   if ( qp_map->version == RENCODE_QP_MAP_LEGACY){
      pitch_size_in_dword = qp_map->width_in_block;
      qp_map->qp_map_pitch = qp_map->width_in_block;
   } else {
      /* two units merge into 1 dword */
      pitch_size_in_dword = DIV_ROUND_UP(qp_map->width_in_block, 2);
      qp_map->qp_map_pitch = pitch_size_in_dword * 2;
   }

   return pitch_size_in_dword * qp_map->height_in_block * sizeof(uint32_t);
}

static void arrange_qp_map(void *start,
                           struct rvcn_enc_qp_map_region *regin,
                           rvcn_enc_qp_map_t *map)
{
   uint32_t i, j;
   uint32_t offset;
   uint32_t num_in_x = MIN2(regin->x_in_unit + regin->width_in_unit, map->width_in_block)
                      - regin->x_in_unit;
   uint32_t num_in_y = MIN2(regin->y_in_unit + regin->height_in_unit, map->height_in_block)
                      - regin->y_in_unit;;

   for (j = 0; j < num_in_y; j++) {
      for (i = 0; i < num_in_x; i++) {
         offset = regin->x_in_unit + i + (regin->y_in_unit + j) * map->qp_map_pitch;
         if (map->version == RENCODE_QP_MAP_LEGACY)
            *((uint32_t *)start + offset) = (int32_t)regin->qp_delta;
         else
            *((int16_t *)start + offset) =
               (int16_t)(regin->qp_delta << RENCODE_QP_MAP_UNIFIED_QP_BITS_SHIFT);
      }
   }
}

/* Arrange roi map values according to the input regions.
 * The arrangment will consider the lower sequence region
 * higher priority and that could overlap the higher sequence
 * map region. */
static int generate_roi_map(struct radeon_encoder *enc)
{
   uint32_t width_in_block, height_in_block;
   uint32_t i;
   void *p_roi = NULL;

   radeon_vcn_enc_blocks_in_frame(enc, &width_in_block, &height_in_block);

   p_roi = enc->ws->buffer_map(enc->ws,
                               enc->roi->res->buf,
                              &enc->cs,
                               PIPE_MAP_READ_WRITE | RADEON_MAP_TEMPORARY);
   if (!p_roi)
      goto error;

   memset(p_roi, 0, enc->roi_size);

   for (i = 0; i < ARRAY_SIZE(enc->enc_pic.enc_qp_map.map); i++) {
      struct rvcn_enc_qp_map_region *region = &enc->enc_pic.enc_qp_map.map[i];
      if (region->is_valid)
         arrange_qp_map(p_roi, region, &enc->enc_pic.enc_qp_map);
   }

   enc->ws->buffer_unmap(enc->ws, enc->roi->res->buf);
   return 0;
error:
   return -1;
}

static void radeon_enc_begin_frame(struct pipe_video_codec *encoder,
                                   struct pipe_video_buffer *source,
                                   struct pipe_picture_desc *picture)
{
   struct radeon_encoder *enc = (struct radeon_encoder *)encoder;
   struct si_screen *sscreen = (struct si_screen *)enc->screen;
   struct vl_video_buffer *vid_buf = (struct vl_video_buffer *)source;
   unsigned dpb_slots = 0;

   enc->source = source;
   enc->need_rate_control = false;
   enc->need_rc_per_pic = false;

   if (u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC) {
      struct pipe_h264_enc_picture_desc *pic = (struct pipe_h264_enc_picture_desc *)picture;
      dpb_slots = MAX2(pic->seq.max_num_ref_frames + 1, pic->dpb_size);
      enc->need_rate_control =
         (enc->enc_pic.rc_layer_init[0].target_bit_rate != pic->rate_ctrl[0].target_bitrate) ||
         (enc->enc_pic.rc_layer_init[0].frame_rate_num != pic->rate_ctrl[0].frame_rate_num) ||
         (enc->enc_pic.rc_layer_init[0].frame_rate_den != pic->rate_ctrl[0].frame_rate_den);

      enc->need_rc_per_pic =
         (enc->enc_pic.rc_per_pic.qp_i != pic->quant_i_frames) ||
         (enc->enc_pic.rc_per_pic.qp_p != pic->quant_p_frames) ||
         (enc->enc_pic.rc_per_pic.qp_b != pic->quant_b_frames) ||
         (enc->enc_pic.rc_per_pic.max_au_size_i != pic->rate_ctrl[0].max_au_size) ||
         (enc->enc_pic.rc_per_pic.qvbr_quality_level != pic->rate_ctrl[0].vbr_quality_factor);
   } else if (u_reduce_video_profile(picture->profile) == PIPE_VIDEO_FORMAT_HEVC) {
      struct pipe_h265_enc_picture_desc *pic = (struct pipe_h265_enc_picture_desc *)picture;
      dpb_slots = MAX2(pic->seq.sps_max_dec_pic_buffering_minus1[0] + 1, pic->dpb_size);
      enc->need_rate_control =
         (enc->enc_pic.rc_layer_init[0].target_bit_rate != pic->rc[0].target_bitrate) ||
         (enc->enc_pic.rc_layer_init[0].frame_rate_num != pic->rc[0].frame_rate_num) ||
         (enc->enc_pic.rc_layer_init[0].frame_rate_den != pic->rc[0].frame_rate_den);

      enc->need_rc_per_pic =
         (enc->enc_pic.rc_per_pic.qp_i != pic->rc[0].quant_i_frames) ||
         (enc->enc_pic.rc_per_pic.qp_p != pic->rc[0].quant_p_frames) ||
         (enc->enc_pic.rc_per_pic.max_au_size_i != pic->rc[0].max_au_size) ||
         (enc->enc_pic.rc_per_pic.qvbr_quality_level != pic->rc[0].vbr_quality_factor);
   } else if (u_reduce_video_profile(picture->profile) == PIPE_VIDEO_FORMAT_AV1) {
      struct pipe_av1_enc_picture_desc *pic = (struct pipe_av1_enc_picture_desc *)picture;
      dpb_slots = pic->dpb_size;
      enc->need_rate_control =
         (enc->enc_pic.rc_layer_init[0].target_bit_rate != pic->rc[0].target_bitrate) ||
         (enc->enc_pic.rc_layer_init[0].frame_rate_num != pic->rc[0].frame_rate_num) ||
         (enc->enc_pic.rc_layer_init[0].frame_rate_den != pic->rc[0].frame_rate_den);

      enc->need_rc_per_pic =
         (enc->enc_pic.rc_per_pic.qp_i != pic->rc[0].qp) ||
         (enc->enc_pic.rc_per_pic.qp_p != pic->rc[0].qp_inter) ||
         (enc->enc_pic.rc_per_pic.qp_b != pic->rc[0].qp_inter) ||
         (enc->enc_pic.rc_per_pic.max_au_size_i != pic->rc[0].max_au_size) ||
         (enc->enc_pic.rc_per_pic.qvbr_quality_level != pic->rc[0].vbr_quality_factor);

      if (!enc->cdf) {
         enc->cdf = CALLOC_STRUCT(rvid_buffer);
         if (setup_cdf(enc)) {
            RADEON_ENC_ERR("Can't create cdf buffer.\n");
            goto error;
         }
      }
   }

   if (enc->dpb_type == DPB_TIER_2)
      dpb_slots = 0;

   radeon_vcn_enc_get_param(enc, picture);
   if (!enc->dpb) {
      enc->dpb = CALLOC_STRUCT(rvid_buffer);
      if (setup_dpb(enc, dpb_slots)) {
         if (!enc->dpb ||
             !si_vid_create_buffer(enc->screen, enc->dpb, enc->dpb_size, PIPE_USAGE_DEFAULT)) {
            RADEON_ENC_ERR("Can't create DPB buffer.\n");
            goto error;
         }
      }
   }

   if ((sscreen->info.vcn_ip_version >= VCN_5_0_0) && enc->metadata_size && !enc->meta) {
      enc->meta = CALLOC_STRUCT(rvid_buffer);
      if (!enc->meta ||
          !si_vid_create_buffer(enc->screen, enc->meta, enc->metadata_size, PIPE_USAGE_DEFAULT)) {
         RADEON_ENC_ERR("Can't create meta buffer.\n");
         goto error;
      }
   }

   if (dpb_slots > enc->dpb_slots) {
      setup_dpb(enc, dpb_slots);
      if (!si_vid_resize_buffer(enc->base.context, &enc->cs, enc->dpb, enc->dpb_size, NULL)) {
         RADEON_ENC_ERR("Can't resize DPB buffer.\n");
         goto error;
      }
      if (sscreen->info.vcn_ip_version >= VCN_5_0_0 && enc->metadata_size &&
          !si_vid_resize_buffer(enc->base.context, &enc->cs, enc->meta, enc->metadata_size, NULL)) {
         RADEON_ENC_ERR("Can't resize meta buffer.\n");
         goto error;
      }
   }

   /* qp map buffer could be created here, and release at the end */
   if (enc->enc_pic.enc_qp_map.qp_map_type != RENCODE_QP_MAP_TYPE_NONE) {
      if (!enc->roi) {
         enc->roi = CALLOC_STRUCT(rvid_buffer);
         enc->roi_size = roi_buffer_size(enc);
         if (!enc->roi || !enc->roi_size ||
             !si_vid_create_buffer(enc->screen, enc->roi, enc->roi_size, PIPE_USAGE_DYNAMIC)) {
            RADEON_ENC_ERR("Can't create ROI buffer.\n");
            goto error;
         }
      }
      if(generate_roi_map(enc)) {
         RADEON_ENC_ERR("Can't form roi map.\n");
         goto error;
      }
   }

   if (source->buffer_format == PIPE_FORMAT_NV12 ||
       source->buffer_format == PIPE_FORMAT_P010 ||
       source->buffer_format == PIPE_FORMAT_P016) {
      enc->get_buffer(vid_buf->resources[0], &enc->handle, &enc->luma);
      enc->get_buffer(vid_buf->resources[1], NULL, &enc->chroma);
   }
   else {
      enc->get_buffer(vid_buf->resources[0], &enc->handle, &enc->luma);
      enc->chroma = NULL;
   }

   enc->need_feedback = false;

   if (!enc->stream_handle) {
      struct rvid_buffer fb;
      enc->stream_handle = si_vid_alloc_stream_handle();
      enc->si = CALLOC_STRUCT(rvid_buffer);
      if (!enc->si ||
          !enc->stream_handle ||
          !si_vid_create_buffer(enc->screen, enc->si, 128 * 1024, PIPE_USAGE_DEFAULT)) {
         RADEON_ENC_ERR("Can't create session buffer.\n");
         goto error;
      }
      si_vid_create_buffer(enc->screen, &fb, 4096, PIPE_USAGE_STAGING);
      enc->fb = &fb;
      enc->begin(enc);
      flush(enc, PIPE_FLUSH_ASYNC, NULL);
      si_vid_destroy_buffer(&fb);
      enc->need_rate_control = false;
      enc->need_rc_per_pic = false;
   }

   return;

error:
   RADEON_ENC_DESTROY_VIDEO_BUFFER(enc->dpb);
   RADEON_ENC_DESTROY_VIDEO_BUFFER(enc->si);
   RADEON_ENC_DESTROY_VIDEO_BUFFER(enc->cdf);
   RADEON_ENC_DESTROY_VIDEO_BUFFER(enc->roi);
   RADEON_ENC_DESTROY_VIDEO_BUFFER(enc->meta);
}

static uint32_t radeon_vcn_enc_encode_h264_header(struct radeon_encoder *enc,
                                                  struct pipe_enc_raw_header *header,
                                                  uint8_t *out)
{
   /* Startcode may be 3 or 4 bytes. */
   const uint8_t nal_byte = header->buffer[header->buffer[2] == 0x1 ? 3 : 4];

   switch (header->type) {
   case PIPE_H264_NAL_SPS:
      return radeon_enc_write_sps(enc, nal_byte, out);
   case PIPE_H264_NAL_PPS:
      return radeon_enc_write_pps(enc, nal_byte, out);
   default:
      assert(header->buffer);
      memcpy(out, header->buffer, header->size);
      return header->size;
   }
}

static uint32_t radeon_vcn_enc_encode_hevc_header(struct radeon_encoder *enc,
                                                  struct pipe_enc_raw_header *header,
                                                  uint8_t *out)
{
   switch (header->type) {
   case PIPE_H265_NAL_VPS:
      return radeon_enc_write_vps(enc, out);
   case PIPE_H265_NAL_SPS:
      return radeon_enc_write_sps_hevc(enc, out);
   case PIPE_H265_NAL_PPS:
      return radeon_enc_write_pps_hevc(enc, out);
   default:
      assert(header->buffer);
      memcpy(out, header->buffer, header->size);
      return header->size;
   }
}

static uint32_t radeon_vcn_enc_encode_av1_header(struct radeon_encoder *enc,
                                                 struct pipe_enc_raw_header *header,
                                                 uint8_t *out)
{
   switch (header->type) {
   case 1: /* SEQUENCE_HEADER */
      return radeon_enc_write_sequence_header(enc, header->buffer, out);
   default:
      assert(header->buffer);
      memcpy(out, header->buffer, header->size);
      return header->size;
   }
}

static void *radeon_vcn_enc_encode_headers(struct radeon_encoder *enc)
{
   const bool is_h264 = u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC;
   const bool is_hevc = u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_HEVC;
   const bool is_av1 = u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_AV1;
   struct util_dynarray *headers;
   unsigned num_slices = 0, num_headers = 0;

   if (is_h264)
      headers = &enc->enc_pic.h264.desc->raw_headers;
   else if (is_hevc)
      headers = &enc->enc_pic.hevc.desc->raw_headers;
   else if (is_av1)
      headers = &enc->enc_pic.av1.desc->raw_headers;
   else
      return NULL;

   util_dynarray_foreach(headers, struct pipe_enc_raw_header, header) {
      if (header->is_slice)
         num_slices++;
      num_headers++;
   }

   if (!num_headers || !num_slices || num_headers == num_slices)
      return NULL;

   size_t segments_size =
      sizeof(struct rvcn_enc_output_unit_segment) * (num_headers - num_slices + 1);
   struct rvcn_enc_feedback_data *data =
      CALLOC_VARIANT_LENGTH_STRUCT(rvcn_enc_feedback_data, segments_size);
   if (!data)
      return NULL;

   uint8_t *ptr = enc->ws->buffer_map(enc->ws, enc->bs_handle, &enc->cs,
                                      PIPE_MAP_WRITE | RADEON_MAP_TEMPORARY);
   if (!ptr) {
      RADEON_ENC_ERR("Can't map bs buffer.\n");
      FREE(data);
      return NULL;
   }

   unsigned offset = 0;
   struct rvcn_enc_output_unit_segment *slice_segment = NULL;

   util_dynarray_foreach(headers, struct pipe_enc_raw_header, header) {
      if (header->is_slice) {
         if (slice_segment)
            continue;
         slice_segment = &data->segments[data->num_segments];
         slice_segment->is_slice = true;
      } else {
         unsigned size = 0;
         if (is_h264)
            size = radeon_vcn_enc_encode_h264_header(enc, header, ptr + offset);
         else if (is_hevc)
            size = radeon_vcn_enc_encode_hevc_header(enc, header, ptr + offset);
         else if (is_av1)
            size = radeon_vcn_enc_encode_av1_header(enc, header, ptr + offset);
         data->segments[data->num_segments].size = size;
         data->segments[data->num_segments].offset = offset;
         offset += size;
      }
      data->num_segments++;
   }

   enc->bs_offset = align(offset, 16);
   assert(enc->bs_offset < enc->bs_size);

   assert(slice_segment);
   slice_segment->offset = enc->bs_offset;

   enc->ws->buffer_unmap(enc->ws, enc->bs_handle);

   return data;
}

static void radeon_enc_encode_bitstream(struct pipe_video_codec *encoder,
                                        struct pipe_video_buffer *source,
                                        struct pipe_resource *destination, void **fb)
{
   struct radeon_encoder *enc = (struct radeon_encoder *)encoder;
   struct vl_video_buffer *vid_buf = (struct vl_video_buffer *)source;

   if (enc->error)
      return;

   enc->get_buffer(destination, &enc->bs_handle, NULL);
   enc->bs_size = destination->width0;
   enc->bs_offset = 0;

   *fb = enc->fb = CALLOC_STRUCT(rvid_buffer);

   if (!si_vid_create_buffer(enc->screen, enc->fb, 4096, PIPE_USAGE_STAGING)) {
      RADEON_ENC_ERR("Can't create feedback buffer.\n");
      return;
   }

   enc->fb->user_data = radeon_vcn_enc_encode_headers(enc);

   if (vid_buf->base.statistics_data) {
      enc->get_buffer(vid_buf->base.statistics_data, &enc->stats, NULL);
      if (enc->stats->size < sizeof(rvcn_encode_stats_type_0_t)) {
         RADEON_ENC_ERR("Encoder statistics output buffer is too small.\n");
         enc->stats = NULL;
      }
      vid_buf->base.statistics_data = NULL;
   }
   else
      enc->stats = NULL;

   enc->need_feedback = true;
   enc->encode(enc);
}

static int radeon_enc_end_frame(struct pipe_video_codec *encoder, struct pipe_video_buffer *source,
                                struct pipe_picture_desc *picture)
{
   struct radeon_encoder *enc = (struct radeon_encoder *)encoder;

   if (enc->error)
      return -1;

   return flush(enc, picture->flush_flags, picture->fence);
}

static void radeon_enc_destroy(struct pipe_video_codec *encoder)
{
   struct radeon_encoder *enc = (struct radeon_encoder *)encoder;

   if (enc->stream_handle) {
      struct rvid_buffer fb;
      enc->need_feedback = false;
      si_vid_create_buffer(enc->screen, &fb, 512, PIPE_USAGE_STAGING);
      enc->fb = &fb;
      enc->destroy(enc);
      flush(enc, PIPE_FLUSH_ASYNC, NULL);
      RADEON_ENC_DESTROY_VIDEO_BUFFER(enc->si);
      si_vid_destroy_buffer(&fb);
   }

   RADEON_ENC_DESTROY_VIDEO_BUFFER(enc->dpb);
   RADEON_ENC_DESTROY_VIDEO_BUFFER(enc->cdf);
   RADEON_ENC_DESTROY_VIDEO_BUFFER(enc->roi);
   RADEON_ENC_DESTROY_VIDEO_BUFFER(enc->meta);
   enc->ws->cs_destroy(&enc->cs);
   if (enc->ectx)
      enc->ectx->destroy(enc->ectx);

   FREE(enc);
}

static void radeon_enc_get_feedback(struct pipe_video_codec *encoder, void *feedback,
                                    unsigned *size, struct pipe_enc_feedback_metadata *metadata)
{
   struct radeon_encoder *enc = (struct radeon_encoder *)encoder;
   struct rvid_buffer *fb = feedback;

   uint32_t *ptr = enc->ws->buffer_map(enc->ws, fb->res->buf, &enc->cs,
                                       PIPE_MAP_READ_WRITE | RADEON_MAP_TEMPORARY);
   if (ptr[1])
      *size = ptr[6] - ptr[8];
   else
      *size = 0;
   enc->ws->buffer_unmap(enc->ws, fb->res->buf);

   metadata->present_metadata = PIPE_VIDEO_FEEDBACK_METADATA_TYPE_CODEC_UNIT_LOCATION;

   if (fb->user_data) {
      struct rvcn_enc_feedback_data *data = fb->user_data;
      metadata->codec_unit_metadata_count = data->num_segments;
      for (unsigned i = 0; i < data->num_segments; i++) {
         metadata->codec_unit_metadata[i].offset = data->segments[i].offset;
         if (data->segments[i].is_slice) {
            metadata->codec_unit_metadata[i].size = *size;
            metadata->codec_unit_metadata[i].flags = 0;
         } else {
            metadata->codec_unit_metadata[i].size = data->segments[i].size;
            metadata->codec_unit_metadata[i].flags = PIPE_VIDEO_CODEC_UNIT_LOCATION_FLAG_SINGLE_NALU;
         }
      }
      FREE(fb->user_data);
      fb->user_data = NULL;
   } else {
      metadata->codec_unit_metadata_count = 1;
      metadata->codec_unit_metadata[0].offset = 0;
      metadata->codec_unit_metadata[0].size = *size;
      metadata->codec_unit_metadata[0].flags = 0;
   }

   RADEON_ENC_DESTROY_VIDEO_BUFFER(fb);
}

static int radeon_enc_fence_wait(struct pipe_video_codec *encoder,
                                 struct pipe_fence_handle *fence,
                                 uint64_t timeout)
{
   struct radeon_encoder *enc = (struct radeon_encoder *)encoder;

   return enc->ws->fence_wait(enc->ws, fence, timeout);
}

static void radeon_enc_destroy_fence(struct pipe_video_codec *encoder,
                                     struct pipe_fence_handle *fence)
{
   struct radeon_encoder *enc = (struct radeon_encoder *)encoder;

   enc->ws->fence_reference(enc->ws, &fence, NULL);
}

static unsigned int radeon_enc_frame_context_buffer_size(struct radeon_encoder *enc)
{
   unsigned int size = 0;
   bool is_h264 = u_reduce_video_profile(enc->base.profile)
                             == PIPE_VIDEO_FORMAT_MPEG4_AVC;
   bool is_av1 = u_reduce_video_profile(enc->base.profile)
                             == PIPE_VIDEO_FORMAT_AV1;
   bool has_b = enc->enc_pic.spec_misc.b_picture_enabled; /* for h264 only */

   size = RENCODE_MAX_METADATA_BUFFER_SIZE_PER_FRAME;
   if (is_h264) {
      if (has_b) {
         enc->enc_pic.fcb_offset.h264.colloc_buffer_offset = size;
         size += enc->enc_pic.total_coloc_bytes;
      } else
         enc->enc_pic.fcb_offset.h264.colloc_buffer_offset =
                                    RENCODE_INVALID_COLOC_OFFSET;
   }

   if (is_av1) {
      enc->enc_pic.fcb_offset.av1.av1_cdf_frame_context_offset = size;
      size += RENCODE_AV1_FRAME_CONTEXT_CDF_TABLE_SIZE;
      enc->enc_pic.fcb_offset.av1.av1_cdef_algorithm_context_offset = size;
      size += RENCODE_AV1_CDEF_ALGORITHM_FRAME_CONTEXT_SIZE;
   }

   size = align(size, enc->alignment);
   return size;
}

void radeon_enc_create_dpb_aux_buffers(struct radeon_encoder *enc, struct radeon_enc_dpb_buffer *buf)
{
   if (buf->fcb)
      return;

   uint32_t fcb_size = radeon_enc_frame_context_buffer_size(enc);

   buf->fcb = CALLOC_STRUCT(rvid_buffer);
   if (!buf->fcb || !si_vid_create_buffer(enc->screen, buf->fcb, fcb_size, PIPE_USAGE_DEFAULT)) {
      RADEON_ENC_ERR("Can't create fcb buffer!\n");
      return;
   }

   if (enc->enc_pic.quality_modes.pre_encode_mode) {
      buf->pre = enc->base.context->create_video_buffer(enc->base.context, &buf->templ);
      if (!buf->pre) {
         RADEON_ENC_ERR("Can't create preenc buffer!\n");
         return;
      }
      buf->pre_luma = (struct si_texture *)((struct vl_video_buffer *)buf->pre)->resources[0];
      buf->pre_chroma = (struct si_texture *)((struct vl_video_buffer *)buf->pre)->resources[1];

      buf->pre_fcb = CALLOC_STRUCT(rvid_buffer);
      if (!buf->pre_fcb || !si_vid_create_buffer(enc->screen, buf->pre_fcb, fcb_size, PIPE_USAGE_DEFAULT)) {
         RADEON_ENC_ERR("Can't create preenc fcb buffer!\n");
         return;
      }
   }
}

static void radeon_enc_destroy_dpb_buffer(void *data)
{
   struct radeon_enc_dpb_buffer *dpb = data;

   if (dpb->pre)
      dpb->pre->destroy(dpb->pre);

   RADEON_ENC_DESTROY_VIDEO_BUFFER(dpb->fcb);
   RADEON_ENC_DESTROY_VIDEO_BUFFER(dpb->pre_fcb);
   FREE(dpb);
}

static struct pipe_video_buffer *radeon_enc_create_dpb_buffer(struct pipe_video_codec *encoder,
                                                              struct pipe_picture_desc *picture,
                                                              const struct pipe_video_buffer *templat)
{
   struct radeon_encoder *enc = (struct radeon_encoder *)encoder;

   struct pipe_video_buffer templ = *templat;
   templ.bind |= PIPE_BIND_VIDEO_ENCODE_DPB;
   struct pipe_video_buffer *buf = enc->base.context->create_video_buffer(enc->base.context, &templ);
   if (!buf) {
      RADEON_ENC_ERR("Can't create dpb buffer!\n");
      return NULL;
   }

   struct radeon_enc_dpb_buffer *dpb = CALLOC_STRUCT(radeon_enc_dpb_buffer);
   dpb->templ = templ;
   dpb->luma = (struct si_texture *)((struct vl_video_buffer *)buf)->resources[0];
   dpb->chroma = (struct si_texture *)((struct vl_video_buffer *)buf)->resources[1];

   vl_video_buffer_set_associated_data(buf, &enc->base, dpb, &radeon_enc_destroy_dpb_buffer);

   return buf;
}

struct pipe_video_codec *radeon_create_encoder(struct pipe_context *context,
                                               const struct pipe_video_codec *templ,
                                               struct radeon_winsys *ws,
                                               radeon_enc_get_buffer get_buffer)
{
   struct si_screen *sscreen = (struct si_screen *)context->screen;
   struct si_context *sctx = (struct si_context *)context;
   struct radeon_encoder *enc;

   enc = CALLOC_STRUCT(radeon_encoder);

   if (!enc)
      return NULL;

   if (sctx->vcn_has_ctx) {
      enc->ectx = context->screen->context_create(context->screen, NULL, PIPE_CONTEXT_COMPUTE_ONLY);
      if (!enc->ectx)
         sctx->vcn_has_ctx = false;
   }

   enc->alignment = 256;
   enc->base = *templ;
   enc->base.context = (sctx->vcn_has_ctx)? enc->ectx : context;
   enc->base.destroy = radeon_enc_destroy;
   enc->base.begin_frame = radeon_enc_begin_frame;
   enc->base.encode_bitstream = radeon_enc_encode_bitstream;
   enc->base.end_frame = radeon_enc_end_frame;
   enc->base.flush = radeon_enc_flush;
   enc->base.get_feedback = radeon_enc_get_feedback;
   enc->base.fence_wait = radeon_enc_fence_wait;
   enc->base.destroy_fence = radeon_enc_destroy_fence;
   enc->get_buffer = get_buffer;
   enc->screen = context->screen;
   enc->ws = ws;

   if (!ws->cs_create(&enc->cs,
       (sctx->vcn_has_ctx) ? ((struct si_context *)enc->ectx)->ctx : sctx->ctx,
       AMD_IP_VCN_ENC, radeon_enc_cs_flush, enc)) {
      RADEON_ENC_ERR("Can't get command submission context.\n");
      goto error;
   }

   enc->enc_pic.use_rc_per_pic_ex = false;

   ac_vcn_enc_init_cmds(&enc->cmd, sscreen->info.vcn_ip_version);

   if (sscreen->info.vcn_ip_version >= VCN_5_0_0)
      enc->dpb_type = DPB_TIER_2;

   if (enc->dpb_type == DPB_TIER_2)
      enc->base.create_dpb_buffer = radeon_enc_create_dpb_buffer;

   if (sscreen->info.vcn_ip_version >= VCN_5_0_0) {
      radeon_enc_5_0_init(enc);
      if (sscreen->info.vcn_ip_version == VCN_5_0_0) {
         /* this limits tile splitting scheme to use legacy method */
         enc->enc_pic.av1_tile_splitting_legacy_flag = true;
      }
   }
   else if (sscreen->info.vcn_ip_version >= VCN_4_0_0) {
      if (sscreen->info.vcn_enc_minor_version >= 1)
         enc->enc_pic.use_rc_per_pic_ex = true;
      radeon_enc_4_0_init(enc);
   }
   else if (sscreen->info.vcn_ip_version >= VCN_3_0_0) {
      if (sscreen->info.vcn_enc_minor_version >= 29)
         enc->enc_pic.use_rc_per_pic_ex = true;
      radeon_enc_3_0_init(enc);
   }
   else if (sscreen->info.vcn_ip_version >= VCN_2_0_0) {
      if (sscreen->info.vcn_enc_minor_version >= 18)
         enc->enc_pic.use_rc_per_pic_ex = true;
      radeon_enc_2_0_init(enc);
   }
   else {
      if (sscreen->info.vcn_enc_minor_version >= 15)
         enc->enc_pic.use_rc_per_pic_ex = true;
      radeon_enc_1_2_init(enc);
   }

   return &enc->base;

error:
   enc->ws->cs_destroy(&enc->cs);
   FREE(enc);
   return NULL;
}

void radeon_enc_add_buffer(struct radeon_encoder *enc, struct pb_buffer_lean *buf,
                           unsigned usage, enum radeon_bo_domain domain, signed offset)
{
   enc->ws->cs_add_buffer(&enc->cs, buf, usage | RADEON_USAGE_SYNCHRONIZED, domain);
   uint64_t addr;
   addr = enc->ws->buffer_get_virtual_address(buf);
   addr = addr + offset;
   RADEON_ENC_CS(addr >> 32);
   RADEON_ENC_CS(addr);
}

void radeon_enc_code_leb128(uint8_t *buf, uint32_t value,
                            uint32_t num_bytes)
{
   uint8_t leb128_byte = 0;
   uint32_t i = 0;

   do {
      leb128_byte = (value & 0x7f);
      value >>= 7;
      if (num_bytes > 1)
         leb128_byte |= 0x80;

      *(buf + i) = leb128_byte;
      num_bytes--;
      i++;
   } while((leb128_byte & 0x80));
}

unsigned int radeon_enc_av1_tile_log2(unsigned int blk_size, unsigned int max)
{
   unsigned int k;

   assert(blk_size);
   for (k = 0; (blk_size << k) < max; k++) {}

   return k;
}

/* dummy function for re-using the same pipeline */
void radeon_enc_dummy(struct radeon_encoder *enc) {}

/* this function has to be in pair with AV1 header copy instruction type at the end */
static void radeon_enc_av1_bs_copy_end(struct radeon_encoder *enc, uint32_t bits)
{
   assert(bits > 0);
   /* it must be dword aligned at the end */
   *enc->enc_pic.copy_start = DIV_ROUND_UP(bits, 32) * 4 + 12;
   *(enc->enc_pic.copy_start + 2) = bits;
}

/* av1 bitstream instruction type */
void radeon_enc_av1_bs_instruction_type(struct radeon_encoder *enc,
                                        struct radeon_bitstream *bs,
                                        uint32_t inst,
                                        uint32_t obu_type)
{
   radeon_bs_flush_headers(bs);

   if (bs->bits_output)
      radeon_enc_av1_bs_copy_end(enc, bs->bits_output);

   enc->enc_pic.copy_start = &enc->cs.current.buf[enc->cs.current.cdw++];
   RADEON_ENC_CS(inst);

   if (inst != RENCODE_HEADER_INSTRUCTION_COPY) {
      *enc->enc_pic.copy_start = 8;
      if (inst == RENCODE_AV1_BITSTREAM_INSTRUCTION_OBU_START) {
         *enc->enc_pic.copy_start += 4;
         RADEON_ENC_CS(obu_type);
      }
   } else
      RADEON_ENC_CS(0); /* allocate a dword for number of bits */

   radeon_bs_reset(bs, NULL, &enc->cs);
}

uint32_t radeon_enc_value_bits(uint32_t value)
{
   uint32_t i = 1;

   while (value > 1) {
      i++;
      value >>= 1;
   }

   return i;
}
