/**************************************************************************
 *
 * Copyright 2017 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "radeon_vcn_enc.h"

#include "pipe/p_video_codec.h"
#include "radeon_video.h"
#include "radeonsi/si_pipe.h"
#include "util/u_memory.h"
#include "util/u_video.h"
#include "vl/vl_video_buffer.h"

#include <stdio.h>

static const unsigned index_to_shifts[4] = {24, 16, 8, 0};

/* set quality modes from the input */
static void radeon_vcn_enc_quality_modes(struct radeon_encoder *enc,
                                         struct pipe_enc_quality_modes *in)
{
   rvcn_enc_quality_modes_t *p = &enc->enc_pic.quality_modes;

   p->preset_mode = in->preset_mode > RENCODE_PRESET_MODE_QUALITY
                                    ? RENCODE_PRESET_MODE_QUALITY
                                    : in->preset_mode;
   p->pre_encode_mode = in->pre_encode_mode ? RENCODE_PREENCODE_MODE_4X
                                            : RENCODE_PREENCODE_MODE_NONE;
   p->vbaq_mode = in->vbaq_mode ? RENCODE_VBAQ_AUTO : RENCODE_VBAQ_NONE;
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
static void radeon_vcn_enc_get_param(struct radeon_encoder *enc, struct pipe_picture_desc *picture)
{
   if (u_reduce_video_profile(picture->profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC) {
      struct pipe_h264_enc_picture_desc *pic = (struct pipe_h264_enc_picture_desc *)picture;
      enc->enc_pic.picture_type = pic->picture_type;
      enc->enc_pic.bit_depth_luma_minus8 = 0;
      enc->enc_pic.bit_depth_chroma_minus8 = 0;
      radeon_vcn_enc_quality_modes(enc, &pic->quality_modes);
      enc->enc_pic.frame_num = pic->frame_num;
      enc->enc_pic.pic_order_cnt = pic->pic_order_cnt;
      enc->enc_pic.pic_order_cnt_type = pic->pic_order_cnt_type;
      enc->enc_pic.ref_idx_l0 = pic->ref_idx_l0_list[0];
      enc->enc_pic.ref_idx_l1 = pic->ref_idx_l1_list[0];
      enc->enc_pic.not_referenced = pic->not_referenced;
      enc->enc_pic.is_idr = (pic->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR);
      if (pic->pic_ctrl.enc_frame_cropping_flag) {
         enc->enc_pic.crop_left = pic->pic_ctrl.enc_frame_crop_left_offset;
         enc->enc_pic.crop_right = pic->pic_ctrl.enc_frame_crop_right_offset;
         enc->enc_pic.crop_top = pic->pic_ctrl.enc_frame_crop_top_offset;
         enc->enc_pic.crop_bottom = pic->pic_ctrl.enc_frame_crop_bottom_offset;
      } else {
         enc->enc_pic.crop_left = 0;
         enc->enc_pic.crop_right = (align(enc->base.width, 16) - enc->base.width) / 2;
         enc->enc_pic.crop_top = 0;
         enc->enc_pic.crop_bottom = (align(enc->base.height, 16) - enc->base.height) / 2;
      }
      enc->enc_pic.num_temporal_layers = pic->num_temporal_layers ? pic->num_temporal_layers : 1;
      enc->enc_pic.temporal_id = 0;
      for (int i = 0; i < enc->enc_pic.num_temporal_layers; i++) {
         enc->enc_pic.rc_layer_init[i].target_bit_rate = pic->rate_ctrl[i].target_bitrate;
         enc->enc_pic.rc_layer_init[i].peak_bit_rate = pic->rate_ctrl[i].peak_bitrate;
         enc->enc_pic.rc_layer_init[i].frame_rate_num = pic->rate_ctrl[i].frame_rate_num;
         enc->enc_pic.rc_layer_init[i].frame_rate_den = pic->rate_ctrl[i].frame_rate_den;
         enc->enc_pic.rc_layer_init[i].vbv_buffer_size = pic->rate_ctrl[i].vbv_buffer_size;
         enc->enc_pic.rc_layer_init[i].avg_target_bits_per_picture =
               radeon_vcn_per_frame_integer(pic->rate_ctrl[i].target_bitrate,
                                            pic->rate_ctrl[i].frame_rate_den,
                                            pic->rate_ctrl[i].frame_rate_num);
         enc->enc_pic.rc_layer_init[i].peak_bits_per_picture_integer =
               radeon_vcn_per_frame_integer(pic->rate_ctrl[i].peak_bitrate,
                                            pic->rate_ctrl[i].frame_rate_den,
                                            pic->rate_ctrl[i].frame_rate_num);
         enc->enc_pic.rc_layer_init[i].peak_bits_per_picture_fractional =
               radeon_vcn_per_frame_frac(pic->rate_ctrl[i].peak_bitrate,
                                         pic->rate_ctrl[i].frame_rate_den,
                                         pic->rate_ctrl[i].frame_rate_num);
      }
      enc->enc_pic.rc_session_init.vbv_buffer_level = pic->rate_ctrl[0].vbv_buf_lv;
      enc->enc_pic.rc_per_pic.qp = pic->quant_i_frames;
      enc->enc_pic.rc_per_pic.min_qp_app = pic->rate_ctrl[0].min_qp;
      enc->enc_pic.rc_per_pic.max_qp_app = pic->rate_ctrl[0].max_qp ?
                                           pic->rate_ctrl[0].max_qp : 51;
      enc->enc_pic.rc_per_pic.enabled_filler_data = pic->rate_ctrl[0].fill_data_enable;
      enc->enc_pic.rc_per_pic.skip_frame_enable = pic->rate_ctrl[0].skip_frame_enable;
      enc->enc_pic.rc_per_pic.enforce_hrd = pic->rate_ctrl[0].enforce_hrd;

      switch (pic->rate_ctrl[0].rate_ctrl_method) {
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE:
         enc->enc_pic.rc_session_init.rate_control_method = RENCODE_RATE_CONTROL_METHOD_NONE;
         break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT_SKIP:
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT:
         enc->enc_pic.rc_session_init.rate_control_method = RENCODE_RATE_CONTROL_METHOD_CBR;
         break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE_SKIP:
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE:
         enc->enc_pic.rc_session_init.rate_control_method =
            RENCODE_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
         break;
      default:
         enc->enc_pic.rc_session_init.rate_control_method = RENCODE_RATE_CONTROL_METHOD_NONE;
      }
      enc->enc_pic.spec_misc.profile_idc = u_get_h264_profile_idc(enc->base.profile);
      if (enc->enc_pic.spec_misc.profile_idc >= PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN &&
          enc->enc_pic.spec_misc.profile_idc != PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED)
         enc->enc_pic.spec_misc.cabac_enable = pic->pic_ctrl.enc_cabac_enable;
      else
         enc->enc_pic.spec_misc.cabac_enable = false;

      enc->enc_pic.rc_per_pic.max_au_size = pic->rate_ctrl[0].max_au_size;
      enc->enc_pic.spec_misc.cabac_init_idc = enc->enc_pic.spec_misc.cabac_enable ? pic->pic_ctrl.enc_cabac_init_idc : 0;

   } else if (u_reduce_video_profile(picture->profile) == PIPE_VIDEO_FORMAT_HEVC) {
      struct pipe_h265_enc_picture_desc *pic = (struct pipe_h265_enc_picture_desc *)picture;
      enc->enc_pic.picture_type = pic->picture_type;
      enc->enc_pic.frame_num = pic->frame_num;
      radeon_vcn_enc_quality_modes(enc, &pic->quality_modes);
      enc->enc_pic.pic_order_cnt = pic->pic_order_cnt;
      enc->enc_pic.pic_order_cnt_type = pic->pic_order_cnt_type;
      enc->enc_pic.ref_idx_l0 = pic->ref_idx_l0_list[0];
      enc->enc_pic.ref_idx_l1 = pic->ref_idx_l1_list[0];
      enc->enc_pic.not_referenced = pic->not_referenced;
      enc->enc_pic.is_idr = (pic->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR) ||
                            (pic->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_I);

      if (pic->seq.conformance_window_flag) {
          enc->enc_pic.crop_left = pic->seq.conf_win_left_offset;
          enc->enc_pic.crop_right = pic->seq.conf_win_right_offset;
          enc->enc_pic.crop_top = pic->seq.conf_win_top_offset;
          enc->enc_pic.crop_bottom = pic->seq.conf_win_bottom_offset;
      } else {
          enc->enc_pic.crop_left = 0;
          enc->enc_pic.crop_right = (align(enc->base.width, 16) - enc->base.width) / 2;
          enc->enc_pic.crop_top = 0;
          enc->enc_pic.crop_bottom = (align(enc->base.height, 16) - enc->base.height) / 2;
      }

      enc->enc_pic.general_tier_flag = pic->seq.general_tier_flag;
      enc->enc_pic.general_profile_idc = pic->seq.general_profile_idc;
      enc->enc_pic.general_level_idc = pic->seq.general_level_idc;
      enc->enc_pic.max_poc = MAX2(16, util_next_power_of_two(pic->seq.intra_period));
      enc->enc_pic.log2_max_poc = 0;
      enc->enc_pic.num_temporal_layers = 1;
      for (int i = enc->enc_pic.max_poc; i != 0; enc->enc_pic.log2_max_poc++)
         i = (i >> 1);
      enc->enc_pic.chroma_format_idc = pic->seq.chroma_format_idc;
      enc->enc_pic.pic_width_in_luma_samples = pic->seq.pic_width_in_luma_samples;
      enc->enc_pic.pic_height_in_luma_samples = pic->seq.pic_height_in_luma_samples;
      enc->enc_pic.log2_diff_max_min_luma_coding_block_size =
         pic->seq.log2_diff_max_min_luma_coding_block_size;
      enc->enc_pic.log2_min_transform_block_size_minus2 =
         pic->seq.log2_min_transform_block_size_minus2;
      enc->enc_pic.log2_diff_max_min_transform_block_size =
         pic->seq.log2_diff_max_min_transform_block_size;

      /* To fix incorrect hardcoded values set by player
       * log2_diff_max_min_luma_coding_block_size = log2(64) - (log2_min_luma_coding_block_size_minus3 + 3)
       * max_transform_hierarchy_depth_inter = log2_diff_max_min_luma_coding_block_size + 1
       * max_transform_hierarchy_depth_intra = log2_diff_max_min_luma_coding_block_size + 1
       */
      enc->enc_pic.max_transform_hierarchy_depth_inter =
         6 - (pic->seq.log2_min_luma_coding_block_size_minus3 + 3) + 1;
      enc->enc_pic.max_transform_hierarchy_depth_intra =
         enc->enc_pic.max_transform_hierarchy_depth_inter;

      enc->enc_pic.log2_parallel_merge_level_minus2 = pic->pic.log2_parallel_merge_level_minus2;
      enc->enc_pic.bit_depth_luma_minus8 = pic->seq.bit_depth_luma_minus8;
      enc->enc_pic.bit_depth_chroma_minus8 = pic->seq.bit_depth_chroma_minus8;
      enc->enc_pic.nal_unit_type = pic->pic.nal_unit_type;
      enc->enc_pic.max_num_merge_cand = pic->slice.max_num_merge_cand;
      enc->enc_pic.sample_adaptive_offset_enabled_flag =
         pic->seq.sample_adaptive_offset_enabled_flag;
      enc->enc_pic.pcm_enabled_flag = pic->seq.pcm_enabled_flag;
      enc->enc_pic.sps_temporal_mvp_enabled_flag = pic->seq.sps_temporal_mvp_enabled_flag;
      enc->enc_pic.hevc_deblock.loop_filter_across_slices_enabled =
         pic->slice.slice_loop_filter_across_slices_enabled_flag;
      enc->enc_pic.hevc_deblock.deblocking_filter_disabled =
         pic->slice.slice_deblocking_filter_disabled_flag;
      enc->enc_pic.hevc_deblock.beta_offset_div2 = pic->slice.slice_beta_offset_div2;
      enc->enc_pic.hevc_deblock.tc_offset_div2 = pic->slice.slice_tc_offset_div2;
      enc->enc_pic.hevc_deblock.cb_qp_offset = pic->slice.slice_cb_qp_offset;
      enc->enc_pic.hevc_deblock.cr_qp_offset = pic->slice.slice_cr_qp_offset;
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
      enc->enc_pic.rc_layer_init[0].target_bit_rate = pic->rc.target_bitrate;
      enc->enc_pic.rc_layer_init[0].peak_bit_rate = pic->rc.peak_bitrate;
      enc->enc_pic.rc_layer_init[0].frame_rate_num = pic->rc.frame_rate_num;
      enc->enc_pic.rc_layer_init[0].frame_rate_den = pic->rc.frame_rate_den;
      enc->enc_pic.rc_layer_init[0].vbv_buffer_size = pic->rc.vbv_buffer_size;
      enc->enc_pic.rc_layer_init[0].avg_target_bits_per_picture =
               radeon_vcn_per_frame_integer(pic->rc.target_bitrate,
                                            pic->rc.frame_rate_den,
                                            pic->rc.frame_rate_num);
      enc->enc_pic.rc_layer_init[0].peak_bits_per_picture_integer =
               radeon_vcn_per_frame_integer(pic->rc.peak_bitrate,
                                            pic->rc.frame_rate_den,
                                            pic->rc.frame_rate_num);
      enc->enc_pic.rc_layer_init[0].peak_bits_per_picture_fractional =
               radeon_vcn_per_frame_frac(pic->rc.peak_bitrate,
                                         pic->rc.frame_rate_den,
                                         pic->rc.frame_rate_num);
      enc->enc_pic.rc_session_init.vbv_buffer_level = pic->rc.vbv_buf_lv;
      enc->enc_pic.rc_per_pic.qp = pic->rc.quant_i_frames;
      enc->enc_pic.rc_per_pic.min_qp_app = pic->rc.min_qp;
      enc->enc_pic.rc_per_pic.max_qp_app = pic->rc.max_qp ? pic->rc.max_qp : 51;
      enc->enc_pic.rc_per_pic.enabled_filler_data = pic->rc.fill_data_enable;
      enc->enc_pic.rc_per_pic.skip_frame_enable = pic->rc.skip_frame_enable;
      enc->enc_pic.rc_per_pic.enforce_hrd = pic->rc.enforce_hrd;
      switch (pic->rc.rate_ctrl_method) {
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE:
         enc->enc_pic.rc_session_init.rate_control_method = RENCODE_RATE_CONTROL_METHOD_NONE;
         break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT_SKIP:
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT:
         enc->enc_pic.rc_session_init.rate_control_method = RENCODE_RATE_CONTROL_METHOD_CBR;
         break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE_SKIP:
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE:
         enc->enc_pic.rc_session_init.rate_control_method =
            RENCODE_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR;
         break;
      default:
         enc->enc_pic.rc_session_init.rate_control_method = RENCODE_RATE_CONTROL_METHOD_NONE;
      }
      enc->enc_pic.rc_per_pic.max_au_size = pic->rc.max_au_size;
   }
}

static void flush(struct radeon_encoder *enc)
{
   enc->ws->cs_flush(&enc->cs, PIPE_FLUSH_ASYNC, NULL);
}

static void radeon_enc_flush(struct pipe_video_codec *encoder)
{
   struct radeon_encoder *enc = (struct radeon_encoder *)encoder;
   flush(enc);
}

static void radeon_enc_cs_flush(void *ctx, unsigned flags, struct pipe_fence_handle **fence)
{
   // just ignored
}

/* configure reconstructed picture offset */
static void radeon_enc_rec_offset(rvcn_enc_reconstructed_picture_t *recon,
                                  uint32_t *offset,
                                  uint32_t luma_size,
                                  uint32_t chroma_size)
{
   if (offset) {
      recon->luma_offset = *offset;
      *offset += luma_size;
      recon->chroma_offset = *offset;
      *offset += chroma_size;
   } else {
      recon->luma_offset = 0;
      recon->chroma_offset = 0;
   }
}

static int setup_dpb(struct radeon_encoder *enc)
{
   uint32_t rec_alignment = (u_reduce_video_profile(enc->base.profile)
                             == PIPE_VIDEO_FORMAT_MPEG4_AVC) ? 16 : 64;
   uint32_t aligned_width = align(enc->base.width, rec_alignment);
   uint32_t aligned_height = align(enc->base.height, rec_alignment);
   uint32_t aligned_chroma_height = align(aligned_height / 2, rec_alignment);
   uint32_t pitch = align(aligned_width, enc->alignment);
   uint32_t num_reconstructed_pictures = enc->base.max_references + 1;
   uint32_t luma_size, chroma_size, offset;
   struct radeon_enc_pic *enc_pic = &enc->enc_pic;
   int i;

   luma_size = align(pitch * aligned_height, enc->alignment);
   chroma_size = align(pitch * aligned_chroma_height, enc->alignment);
   if (enc_pic->bit_depth_luma_minus8 || enc_pic->bit_depth_chroma_minus8) {
      luma_size *= 2;
      chroma_size *= 2;
   }

   assert(num_reconstructed_pictures <= RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES);

   enc_pic->ctx_buf.rec_luma_pitch   = pitch;
   enc_pic->ctx_buf.rec_chroma_pitch = pitch;
   enc_pic->ctx_buf.pre_encode_picture_luma_pitch   = pitch;
   enc_pic->ctx_buf.pre_encode_picture_chroma_pitch = pitch;

   offset = 0;
   for (i = 0; i < num_reconstructed_pictures; i++) {
      radeon_enc_rec_offset(&enc_pic->ctx_buf.reconstructed_pictures[i],
                            &offset, luma_size, chroma_size);

      if (enc_pic->quality_modes.pre_encode_mode)
         radeon_enc_rec_offset(&enc_pic->ctx_buf.pre_encode_reconstructed_pictures[i],
                               &offset, luma_size, chroma_size);
   }

   for (; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      radeon_enc_rec_offset(&enc_pic->ctx_buf.reconstructed_pictures[i],
                            NULL, 0, 0);
      if (enc_pic->quality_modes.pre_encode_mode)
         radeon_enc_rec_offset(&enc_pic->ctx_buf.pre_encode_reconstructed_pictures[i],
                               NULL, 0, 0);
   }

   if (enc_pic->quality_modes.pre_encode_mode) {
      enc_pic->ctx_buf.pre_encode_input_picture.rgb.red_offset = offset;
      offset += luma_size;
      enc_pic->ctx_buf.pre_encode_input_picture.rgb.green_offset = offset;
      offset += luma_size;
      enc_pic->ctx_buf.pre_encode_input_picture.rgb.blue_offset = offset;
      offset += luma_size;
   }

   enc_pic->ctx_buf.num_reconstructed_pictures = num_reconstructed_pictures;
   enc_pic->ctx_buf.colloc_buffer_offset = 0;
   enc->dpb_size = offset;

   return offset;
}

static void radeon_enc_begin_frame(struct pipe_video_codec *encoder,
                                   struct pipe_video_buffer *source,
                                   struct pipe_picture_desc *picture)
{
   struct radeon_encoder *enc = (struct radeon_encoder *)encoder;
   struct vl_video_buffer *vid_buf = (struct vl_video_buffer *)source;
   bool need_rate_control = false;

   if (u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC) {
      struct pipe_h264_enc_picture_desc *pic = (struct pipe_h264_enc_picture_desc *)picture;
      need_rate_control =
         (enc->enc_pic.rc_layer_init[0].target_bit_rate != pic->rate_ctrl[0].target_bitrate) ||
         (enc->enc_pic.rc_layer_init[0].frame_rate_num != pic->rate_ctrl[0].frame_rate_num) ||
         (enc->enc_pic.rc_layer_init[0].frame_rate_den != pic->rate_ctrl[0].frame_rate_den);
   } else if (u_reduce_video_profile(picture->profile) == PIPE_VIDEO_FORMAT_HEVC) {
      struct pipe_h265_enc_picture_desc *pic = (struct pipe_h265_enc_picture_desc *)picture;
      need_rate_control = enc->enc_pic.rc_layer_init[0].target_bit_rate != pic->rc.target_bitrate;
   }

   radeon_vcn_enc_get_param(enc, picture);
   if (!enc->dpb) {
      enc->dpb = CALLOC_STRUCT(rvid_buffer);
      setup_dpb(enc);
      if (!enc->dpb ||
          !si_vid_create_buffer(enc->screen, enc->dpb, enc->dpb_size, PIPE_USAGE_DEFAULT)) {
         RVID_ERR("Can't create DPB buffer.\n");
         goto error;
      }
   }

   enc->get_buffer(vid_buf->resources[0], &enc->handle, &enc->luma);
   enc->get_buffer(vid_buf->resources[1], NULL, &enc->chroma);

   enc->need_feedback = false;

   if (!enc->stream_handle || need_rate_control) {
      struct rvid_buffer fb;
      if (!enc->stream_handle) {
         enc->stream_handle = si_vid_alloc_stream_handle();
         enc->si = CALLOC_STRUCT(rvid_buffer);
         if (!enc->si ||
             !enc->stream_handle ||
             !si_vid_create_buffer(enc->screen, enc->si, 128 * 1024, PIPE_USAGE_STAGING)) {
            RVID_ERR("Can't create session buffer.\n");
            goto error;
         }
      }
      si_vid_create_buffer(enc->screen, &fb, 4096, PIPE_USAGE_STAGING);
      enc->fb = &fb;
      enc->begin(enc);
      flush(enc);
      si_vid_destroy_buffer(&fb);
   }

   return;

error:
   RADEON_ENC_DESTROY_VIDEO_BUFFER(enc->dpb);
   RADEON_ENC_DESTROY_VIDEO_BUFFER(enc->si);
}

static void radeon_enc_encode_bitstream(struct pipe_video_codec *encoder,
                                        struct pipe_video_buffer *source,
                                        struct pipe_resource *destination, void **fb)
{
   struct radeon_encoder *enc = (struct radeon_encoder *)encoder;
   enc->get_buffer(destination, &enc->bs_handle, NULL);
   enc->bs_size = destination->width0;

   *fb = enc->fb = CALLOC_STRUCT(rvid_buffer);

   if (!si_vid_create_buffer(enc->screen, enc->fb, 4096, PIPE_USAGE_STAGING)) {
      RVID_ERR("Can't create feedback buffer.\n");
      return;
   }

   enc->need_feedback = true;
   enc->encode(enc);
}

static void radeon_enc_end_frame(struct pipe_video_codec *encoder, struct pipe_video_buffer *source,
                                 struct pipe_picture_desc *picture)
{
   struct radeon_encoder *enc = (struct radeon_encoder *)encoder;
   flush(enc);
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
      flush(enc);
      RADEON_ENC_DESTROY_VIDEO_BUFFER(enc->si);
      si_vid_destroy_buffer(&fb);
   }

   RADEON_ENC_DESTROY_VIDEO_BUFFER(enc->dpb);
   enc->ws->cs_destroy(&enc->cs);
   FREE(enc);
}

static void radeon_enc_get_feedback(struct pipe_video_codec *encoder, void *feedback,
                                    unsigned *size)
{
   struct radeon_encoder *enc = (struct radeon_encoder *)encoder;
   struct rvid_buffer *fb = feedback;

   if (size) {
      uint32_t *ptr = enc->ws->buffer_map(enc->ws, fb->res->buf, &enc->cs,
                                          PIPE_MAP_READ_WRITE | RADEON_MAP_TEMPORARY);
      if (ptr[1])
         *size = ptr[6];
      else
         *size = 0;
      enc->ws->buffer_unmap(enc->ws, fb->res->buf);
   }

   si_vid_destroy_buffer(fb);
   FREE(fb);
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

   enc->alignment = 256;
   enc->base = *templ;
   enc->base.context = context;
   enc->base.destroy = radeon_enc_destroy;
   enc->base.begin_frame = radeon_enc_begin_frame;
   enc->base.encode_bitstream = radeon_enc_encode_bitstream;
   enc->base.end_frame = radeon_enc_end_frame;
   enc->base.flush = radeon_enc_flush;
   enc->base.get_feedback = radeon_enc_get_feedback;
   enc->get_buffer = get_buffer;
   enc->bits_in_shifter = 0;
   enc->screen = context->screen;
   enc->ws = ws;

   if (!ws->cs_create(&enc->cs, sctx->ctx, AMD_IP_VCN_ENC, radeon_enc_cs_flush, enc, false)) {
      RVID_ERR("Can't get command submission context.\n");
      goto error;
   }

   if (sscreen->info.gfx_level >= GFX11)
      radeon_enc_4_0_init(enc);
   else if (sscreen->info.family >= CHIP_NAVI21)
      radeon_enc_3_0_init(enc);
   else if (sscreen->info.family >= CHIP_RENOIR)
      radeon_enc_2_0_init(enc);
   else
      radeon_enc_1_2_init(enc);

   return &enc->base;

error:
   enc->ws->cs_destroy(&enc->cs);
   FREE(enc);
   return NULL;
}

void radeon_enc_add_buffer(struct radeon_encoder *enc, struct pb_buffer *buf,
                           unsigned usage, enum radeon_bo_domain domain, signed offset)
{
   enc->ws->cs_add_buffer(&enc->cs, buf, usage | RADEON_USAGE_SYNCHRONIZED, domain);
   uint64_t addr;
   addr = enc->ws->buffer_get_virtual_address(buf);
   addr = addr + offset;
   RADEON_ENC_CS(addr >> 32);
   RADEON_ENC_CS(addr);
}

void radeon_enc_set_emulation_prevention(struct radeon_encoder *enc, bool set)
{
   if (set != enc->emulation_prevention) {
      enc->emulation_prevention = set;
      enc->num_zeros = 0;
   }
}

void radeon_enc_output_one_byte(struct radeon_encoder *enc, unsigned char byte)
{
   if (enc->byte_index == 0)
      enc->cs.current.buf[enc->cs.current.cdw] = 0;
   enc->cs.current.buf[enc->cs.current.cdw] |=
      ((unsigned int)(byte) << index_to_shifts[enc->byte_index]);
   enc->byte_index++;

   if (enc->byte_index >= 4) {
      enc->byte_index = 0;
      enc->cs.current.cdw++;
   }
}

void radeon_enc_emulation_prevention(struct radeon_encoder *enc, unsigned char byte)
{
   if (enc->emulation_prevention) {
      if ((enc->num_zeros >= 2) && ((byte == 0x00) || (byte == 0x01) ||
         (byte == 0x02) || (byte == 0x03))) {
         radeon_enc_output_one_byte(enc, 0x03);
         enc->bits_output += 8;
         enc->num_zeros = 0;
      }
      enc->num_zeros = (byte == 0 ? (enc->num_zeros + 1) : 0);
   }
}

void radeon_enc_code_fixed_bits(struct radeon_encoder *enc, unsigned int value,
                                unsigned int num_bits)
{
   unsigned int bits_to_pack = 0;
   enc->bits_size += num_bits;

   while (num_bits > 0) {
      unsigned int value_to_pack = value & (0xffffffff >> (32 - num_bits));
      bits_to_pack =
         num_bits > (32 - enc->bits_in_shifter) ? (32 - enc->bits_in_shifter) : num_bits;

      if (bits_to_pack < num_bits)
         value_to_pack = value_to_pack >> (num_bits - bits_to_pack);

      enc->shifter |= value_to_pack << (32 - enc->bits_in_shifter - bits_to_pack);
      num_bits -= bits_to_pack;
      enc->bits_in_shifter += bits_to_pack;

      while (enc->bits_in_shifter >= 8) {
         unsigned char output_byte = (unsigned char)(enc->shifter >> 24);
         enc->shifter <<= 8;
         radeon_enc_emulation_prevention(enc, output_byte);
         radeon_enc_output_one_byte(enc, output_byte);
         enc->bits_in_shifter -= 8;
         enc->bits_output += 8;
      }
   }
}

void radeon_enc_reset(struct radeon_encoder *enc)
{
   enc->emulation_prevention = false;
   enc->shifter = 0;
   enc->bits_in_shifter = 0;
   enc->bits_output = 0;
   enc->num_zeros = 0;
   enc->byte_index = 0;
   enc->bits_size = 0;
}

void radeon_enc_byte_align(struct radeon_encoder *enc)
{
   unsigned int num_padding_zeros = (32 - enc->bits_in_shifter) % 8;

   if (num_padding_zeros > 0)
      radeon_enc_code_fixed_bits(enc, 0, num_padding_zeros);
}

void radeon_enc_flush_headers(struct radeon_encoder *enc)
{
   if (enc->bits_in_shifter != 0) {
      unsigned char output_byte = (unsigned char)(enc->shifter >> 24);
      radeon_enc_emulation_prevention(enc, output_byte);
      radeon_enc_output_one_byte(enc, output_byte);
      enc->bits_output += enc->bits_in_shifter;
      enc->shifter = 0;
      enc->bits_in_shifter = 0;
      enc->num_zeros = 0;
   }

   if (enc->byte_index > 0) {
      enc->cs.current.cdw++;
      enc->byte_index = 0;
   }
}

void radeon_enc_code_ue(struct radeon_encoder *enc, unsigned int value)
{
   int x = -1;
   unsigned int ue_code = value + 1;
   value += 1;

   while (value) {
      value = (value >> 1);
      x += 1;
   }

   unsigned int ue_length = (x << 1) + 1;
   radeon_enc_code_fixed_bits(enc, ue_code, ue_length);
}

void radeon_enc_code_se(struct radeon_encoder *enc, int value)
{
   unsigned int v = 0;

   if (value != 0)
      v = (value < 0 ? ((unsigned int)(0 - value) << 1) : (((unsigned int)(value) << 1) - 1));

   radeon_enc_code_ue(enc, v);
}
