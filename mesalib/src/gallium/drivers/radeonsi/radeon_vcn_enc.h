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

#ifndef _RADEON_VCN_ENC_H
#define _RADEON_VCN_ENC_H

#include "radeon_vcn.h"

#define RENCODE_IB_OP_INITIALIZE                                                    0x01000001
#define RENCODE_IB_OP_CLOSE_SESSION                                                 0x01000002
#define RENCODE_IB_OP_ENCODE                                                        0x01000003
#define RENCODE_IB_OP_INIT_RC                                                       0x01000004
#define RENCODE_IB_OP_INIT_RC_VBV_BUFFER_LEVEL                                      0x01000005
#define RENCODE_IB_OP_SET_SPEED_ENCODING_MODE                                       0x01000006
#define RENCODE_IB_OP_SET_BALANCE_ENCODING_MODE                                     0x01000007
#define RENCODE_IB_OP_SET_QUALITY_ENCODING_MODE                                     0x01000008

#define RENCODE_IF_MAJOR_VERSION_MASK                                               0xFFFF0000
#define RENCODE_IF_MAJOR_VERSION_SHIFT                                              16
#define RENCODE_IF_MINOR_VERSION_MASK                                               0x0000FFFF
#define RENCODE_IF_MINOR_VERSION_SHIFT                                              0

#define RENCODE_ENGINE_TYPE_ENCODE                                                  1

#define RENCODE_ENCODE_STANDARD_HEVC                                                0
#define RENCODE_ENCODE_STANDARD_H264                                                1

#define RENCODE_PREENCODE_MODE_NONE                                                 0x00000000
#define RENCODE_PREENCODE_MODE_1X                                                   0x00000001
#define RENCODE_PREENCODE_MODE_2X                                                   0x00000002
#define RENCODE_PREENCODE_MODE_4X                                                   0x00000004

#define RENCODE_VBAQ_NONE                                                           0x00000000
#define RENCODE_VBAQ_AUTO                                                           0x00000001

#define RENCODE_PRESET_MODE_SPEED                                                   0x00000000
#define RENCODE_PRESET_MODE_BALANCE                                                 0x00000001
#define RENCODE_PRESET_MODE_QUALITY                                                 0x00000002

#define RENCODE_H264_SLICE_CONTROL_MODE_FIXED_MBS                                   0x00000000
#define RENCODE_H264_SLICE_CONTROL_MODE_FIXED_BITS                                  0x00000001

#define RENCODE_HEVC_SLICE_CONTROL_MODE_FIXED_CTBS                                  0x00000000
#define RENCODE_HEVC_SLICE_CONTROL_MODE_FIXED_BITS                                  0x00000001

#define RENCODE_RATE_CONTROL_METHOD_NONE                                            0x00000000
#define RENCODE_RATE_CONTROL_METHOD_LATENCY_CONSTRAINED_VBR                         0x00000001
#define RENCODE_RATE_CONTROL_METHOD_PEAK_CONSTRAINED_VBR                            0x00000002
#define RENCODE_RATE_CONTROL_METHOD_CBR                                             0x00000003

#define RENCODE_DIRECT_OUTPUT_NALU_TYPE_AUD                                         0x00000000
#define RENCODE_DIRECT_OUTPUT_NALU_TYPE_VPS                                         0x00000001
#define RENCODE_DIRECT_OUTPUT_NALU_TYPE_SPS                                         0x00000002
#define RENCODE_DIRECT_OUTPUT_NALU_TYPE_PPS                                         0x00000003
#define RENCODE_DIRECT_OUTPUT_NALU_TYPE_PREFIX                                      0x00000004
#define RENCODE_DIRECT_OUTPUT_NALU_TYPE_END_OF_SEQUENCE                             0x00000005
#define RENCODE_DIRECT_OUTPUT_NALU_TYPE_SEI                                         0x00000006

#define RENCODE_SLICE_HEADER_TEMPLATE_MAX_TEMPLATE_SIZE_IN_DWORDS                   16
#define RENCODE_SLICE_HEADER_TEMPLATE_MAX_NUM_INSTRUCTIONS                          16

#define RENCODE_HEADER_INSTRUCTION_END                                              0x00000000
#define RENCODE_HEADER_INSTRUCTION_COPY                                             0x00000001

#define RENCODE_HEVC_HEADER_INSTRUCTION_DEPENDENT_SLICE_END                         0x00010000
#define RENCODE_HEVC_HEADER_INSTRUCTION_FIRST_SLICE                                 0x00010001
#define RENCODE_HEVC_HEADER_INSTRUCTION_SLICE_SEGMENT                               0x00010002
#define RENCODE_HEVC_HEADER_INSTRUCTION_SLICE_QP_DELTA                              0x00010003
#define RENCODE_HEVC_HEADER_INSTRUCTION_SAO_ENABLE                                  0x00010004
#define RENCODE_HEVC_HEADER_INSTRUCTION_LOOP_FILTER_ACROSS_SLICES_ENABLE            0x00010005

#define RENCODE_H264_HEADER_INSTRUCTION_FIRST_MB                                    0x00020000
#define RENCODE_H264_HEADER_INSTRUCTION_SLICE_QP_DELTA                              0x00020001

#define RENCODE_PICTURE_TYPE_B                                                      0
#define RENCODE_PICTURE_TYPE_P                                                      1
#define RENCODE_PICTURE_TYPE_I                                                      2
#define RENCODE_PICTURE_TYPE_P_SKIP                                                 3

#define RENCODE_INPUT_SWIZZLE_MODE_LINEAR                                           0
#define RENCODE_INPUT_SWIZZLE_MODE_256B_S                                           1
#define RENCODE_INPUT_SWIZZLE_MODE_4kB_S                                            5
#define RENCODE_INPUT_SWIZZLE_MODE_64kB_S                                           9

#define RENCODE_H264_PICTURE_STRUCTURE_FRAME                                        0
#define RENCODE_H264_PICTURE_STRUCTURE_TOP_FIELD                                    1
#define RENCODE_H264_PICTURE_STRUCTURE_BOTTOM_FIELD                                 2

#define RENCODE_H264_INTERLACING_MODE_PROGRESSIVE                                   0
#define RENCODE_H264_INTERLACING_MODE_INTERLACED_STACKED                            1
#define RENCODE_H264_INTERLACING_MODE_INTERLACED_INTERLEAVED                        2

#define RENCODE_H264_DISABLE_DEBLOCKING_FILTER_IDC_ENABLE                           0
#define RENCODE_H264_DISABLE_DEBLOCKING_FILTER_IDC_DISABLE                          1
#define RENCODE_H264_DISABLE_DEBLOCKING_FILTER_IDC_DISALBE_ACROSS_SLICE_BOUNDARY    2

#define RENCODE_INTRA_REFRESH_MODE_NONE                                             0
#define RENCODE_INTRA_REFRESH_MODE_CTB_MB_ROWS                                      1
#define RENCODE_INTRA_REFRESH_MODE_CTB_MB_COLUMNS                                   2

#define RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES                                      34

#define RENCODE_REC_SWIZZLE_MODE_LINEAR                                             0
#define RENCODE_REC_SWIZZLE_MODE_256B_S                                             1

#define RENCODE_VIDEO_BITSTREAM_BUFFER_MODE_LINEAR                                  0
#define RENCODE_VIDEO_BITSTREAM_BUFFER_MODE_CIRCULAR                                1

#define RENCODE_FEEDBACK_BUFFER_MODE_LINEAR                                         0
#define RENCODE_FEEDBACK_BUFFER_MODE_CIRCULAR                                       1

#define RENCODE_MAX_NUM_TEMPORAL_LAYERS                                             4

#define RADEON_ENC_CS(value) (enc->cs.current.buf[enc->cs.current.cdw++] = (value))
#define RADEON_ENC_BEGIN(cmd)                                                                      \
   {                                                                                               \
      uint32_t *begin = &enc->cs.current.buf[enc->cs.current.cdw++];                             \
      RADEON_ENC_CS(cmd)
#define RADEON_ENC_READ(buf, domain, off)                                                          \
   radeon_enc_add_buffer(enc, (buf), RADEON_USAGE_READ, (domain), (off))
#define RADEON_ENC_WRITE(buf, domain, off)                                                         \
   radeon_enc_add_buffer(enc, (buf), RADEON_USAGE_WRITE, (domain), (off))
#define RADEON_ENC_READWRITE(buf, domain, off)                                                     \
   radeon_enc_add_buffer(enc, (buf), RADEON_USAGE_READWRITE, (domain), (off))
#define RADEON_ENC_END()                                                                           \
   *begin = (&enc->cs.current.buf[enc->cs.current.cdw] - begin) * 4;                             \
   enc->total_task_size += *begin;                                                                 \
   }

#define RADEON_ENC_DESTROY_VIDEO_BUFFER(buf)                                                     \
   do {                                                                                          \
      if (buf) {                                                                                 \
         si_vid_destroy_buffer(buf);                                                             \
         FREE(buf);                                                                              \
         (buf) = NULL;                                                                           \
      }                                                                                          \
   } while(0)

typedef struct rvcn_enc_session_info_s {
   uint32_t interface_version;
   uint32_t sw_context_address_hi;
   uint32_t sw_context_address_lo;
} rvcn_enc_session_info_t;

typedef struct rvcn_enc_task_info_s {
   uint32_t total_size_of_all_packages;
   uint32_t task_id;
   uint32_t allowed_max_num_feedbacks;
} rvcn_enc_task_info_t;

typedef struct rvcn_enc_session_init_s {
   uint32_t encode_standard;
   uint32_t aligned_picture_width;
   uint32_t aligned_picture_height;
   uint32_t padding_width;
   uint32_t padding_height;
   uint32_t pre_encode_mode;
   uint32_t pre_encode_chroma_enabled;
   uint32_t slice_output_enabled;
   uint32_t display_remote;
} rvcn_enc_session_init_t;

typedef struct rvcn_enc_layer_control_s {
   uint32_t max_num_temporal_layers;
   uint32_t num_temporal_layers;
} rvcn_enc_layer_control_t;

typedef struct rvcn_enc_layer_select_s {
   uint32_t temporal_layer_index;
} rvcn_enc_layer_select_t;

typedef struct rvcn_enc_h264_slice_control_s {
   uint32_t slice_control_mode;
   union {
      uint32_t num_mbs_per_slice;
      uint32_t num_bits_per_slice;
   };
} rvcn_enc_h264_slice_control_t;

typedef struct rvcn_enc_hevc_slice_control_s {
   uint32_t slice_control_mode;
   union {
      struct {
         uint32_t num_ctbs_per_slice;
         uint32_t num_ctbs_per_slice_segment;
      } fixed_ctbs_per_slice;

      struct {
         uint32_t num_bits_per_slice;
         uint32_t num_bits_per_slice_segment;
      } fixed_bits_per_slice;
   };
} rvcn_enc_hevc_slice_control_t;

typedef struct rvcn_enc_h264_spec_misc_s {
   uint32_t constrained_intra_pred_flag;
   uint32_t cabac_enable;
   uint32_t cabac_init_idc;
   uint32_t half_pel_enabled;
   uint32_t quarter_pel_enabled;
   uint32_t profile_idc;
   uint32_t level_idc;
   uint32_t b_picture_enabled;
   uint32_t weighted_bipred_idc;
} rvcn_enc_h264_spec_misc_t;

typedef struct rvcn_enc_hevc_spec_misc_s {
   uint32_t log2_min_luma_coding_block_size_minus3;
   uint32_t amp_disabled;
   uint32_t strong_intra_smoothing_enabled;
   uint32_t constrained_intra_pred_flag;
   uint32_t cabac_init_flag;
   uint32_t half_pel_enabled;
   uint32_t quarter_pel_enabled;
} rvcn_enc_hevc_spec_misc_t;

typedef struct rvcn_enc_rate_ctl_session_init_s {
   uint32_t rate_control_method;
   uint32_t vbv_buffer_level;
} rvcn_enc_rate_ctl_session_init_t;

typedef struct rvcn_enc_rate_ctl_layer_init_s {
   uint32_t target_bit_rate;
   uint32_t peak_bit_rate;
   uint32_t frame_rate_num;
   uint32_t frame_rate_den;
   uint32_t vbv_buffer_size;
   uint32_t avg_target_bits_per_picture;
   uint32_t peak_bits_per_picture_integer;
   uint32_t peak_bits_per_picture_fractional;
} rvcn_enc_rate_ctl_layer_init_t;

typedef struct rvcn_enc_rate_ctl_per_picture_s {
   uint32_t qp;
   uint32_t min_qp_app;
   uint32_t max_qp_app;
   uint32_t max_au_size;
   uint32_t enabled_filler_data;
   uint32_t skip_frame_enable;
   uint32_t enforce_hrd;
} rvcn_enc_rate_ctl_per_picture_t;

typedef struct rvcn_enc_quality_params_s {
   uint32_t vbaq_mode;
   uint32_t scene_change_sensitivity;
   uint32_t scene_change_min_idr_interval;
   uint32_t two_pass_search_center_map_mode;
} rvcn_enc_quality_params_t;

typedef struct rvcn_enc_direct_output_nalu_s {
   uint32_t type;
   uint32_t size;
   uint32_t data[1];
} rvcn_enc_direct_output_nalu_t;

typedef struct rvcn_enc_slice_header_s {
   uint32_t bitstream_template[RENCODE_SLICE_HEADER_TEMPLATE_MAX_TEMPLATE_SIZE_IN_DWORDS];
   struct {
      uint32_t instruction;
      uint32_t num_bits;
   } instructions[RENCODE_SLICE_HEADER_TEMPLATE_MAX_NUM_INSTRUCTIONS];
} rvcn_enc_slice_header_t;

typedef struct rvcn_enc_h264_reference_picture_info_s {
   unsigned int pic_type;
   unsigned int is_long_term;
   unsigned int picture_structure;
   unsigned int pic_order_cnt;
} rvcn_enc_h264_reference_picture_info_t;

typedef struct rvcn_enc_encode_params_s {
   uint32_t pic_type;
   uint32_t allowed_max_bitstream_size;
   uint32_t input_picture_luma_address_hi;
   uint32_t input_picture_luma_address_lo;
   uint32_t input_picture_chroma_address_hi;
   uint32_t input_picture_chroma_address_lo;
   uint32_t input_pic_luma_pitch;
   uint32_t input_pic_chroma_pitch;
   uint8_t input_pic_swizzle_mode;
   uint32_t reference_picture_index;
   uint32_t reconstructed_picture_index;
} rvcn_enc_encode_params_t;

typedef struct rvcn_enc_h264_encode_params_s {
   uint32_t input_picture_structure;
   uint32_t input_pic_order_cnt;
   uint32_t interlaced_mode;
   uint32_t reference_picture_structure;
   uint32_t reference_picture1_index;
   rvcn_enc_h264_reference_picture_info_t picture_info_l0_reference_picture0;
   uint32_t l0_reference_picture1_index;
   rvcn_enc_h264_reference_picture_info_t picture_info_l0_reference_picture1;
   uint32_t l1_reference_picture0_index;
   rvcn_enc_h264_reference_picture_info_t picture_info_l1_reference_picture0;
} rvcn_enc_h264_encode_params_t;

typedef struct rvcn_enc_h264_deblocking_filter_s {
   uint32_t disable_deblocking_filter_idc;
   int32_t alpha_c0_offset_div2;
   int32_t beta_offset_div2;
   int32_t cb_qp_offset;
   int32_t cr_qp_offset;
} rvcn_enc_h264_deblocking_filter_t;

typedef struct rvcn_enc_hevc_deblocking_filter_s {
   uint32_t loop_filter_across_slices_enabled;
   int32_t deblocking_filter_disabled;
   int32_t beta_offset_div2;
   int32_t tc_offset_div2;
   int32_t cb_qp_offset;
   int32_t cr_qp_offset;
} rvcn_enc_hevc_deblocking_filter_t;

typedef struct rvcn_enc_intra_refresh_s {
   uint32_t intra_refresh_mode;
   uint32_t offset;
   uint32_t region_size;
} rvcn_enc_intra_refresh_t;

typedef struct rvcn_enc_reconstructed_picture_s {
   uint32_t luma_offset;
   uint32_t chroma_offset;
   union {
      struct
      {
         uint32_t  unused_offset1;
         uint32_t  unused_offset2;
      } unused;
   };
} rvcn_enc_reconstructed_picture_t;

typedef struct rvcn_enc_picture_info_s
{
   bool in_use;
   uint32_t frame_num;
} rvcn_enc_picture_info_t;

typedef struct rvcn_enc_pre_encode_input_picture_s {
   union {
      struct {
         uint32_t luma_offset;
         uint32_t chroma_offset;
      } yuv;
      struct {
         uint32_t red_offset;
         uint32_t green_offset;
         uint32_t blue_offset;
      } rgb;
   };
} rvcn_enc_pre_encode_input_picture_t;

typedef struct rvcn_enc_encode_context_buffer_s {
   uint32_t encode_context_address_hi;
   uint32_t encode_context_address_lo;
   uint32_t swizzle_mode;
   uint32_t rec_luma_pitch;
   uint32_t rec_chroma_pitch;
   uint32_t num_reconstructed_pictures;
   rvcn_enc_reconstructed_picture_t reconstructed_pictures[RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES];
   uint32_t pre_encode_picture_luma_pitch;
   uint32_t pre_encode_picture_chroma_pitch;
   rvcn_enc_reconstructed_picture_t
   pre_encode_reconstructed_pictures[RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES];
   rvcn_enc_pre_encode_input_picture_t pre_encode_input_picture;
   uint32_t two_pass_search_center_map_offset;
   uint32_t colloc_buffer_offset;
} rvcn_enc_encode_context_buffer_t;

typedef struct rvcn_enc_video_bitstream_buffer_s {
   uint32_t mode;
   uint32_t video_bitstream_buffer_address_hi;
   uint32_t video_bitstream_buffer_address_lo;
   uint32_t video_bitstream_buffer_size;
   uint32_t video_bitstream_data_offset;
} rvcn_enc_video_bitstream_buffer_t;

typedef struct rvcn_enc_feedback_buffer_s {
   uint32_t mode;
   uint32_t feedback_buffer_address_hi;
   uint32_t feedback_buffer_address_lo;
   uint32_t feedback_buffer_size;
   uint32_t feedback_data_size;
} rvcn_enc_feedback_buffer_t;

typedef struct rvcn_enc_cmd_s {
   uint32_t session_info;
   uint32_t task_info;
   uint32_t session_init;
   uint32_t layer_control;
   uint32_t layer_select;
   uint32_t rc_session_init;
   uint32_t rc_layer_init;
   uint32_t rc_per_pic;
   uint32_t quality_params;
   uint32_t slice_header;
   uint32_t enc_params;
   uint32_t intra_refresh;
   uint32_t ctx;
   uint32_t bitstream;
   uint32_t feedback;
   uint32_t nalu;
   uint32_t slice_control_hevc;
   uint32_t spec_misc_hevc;
   uint32_t enc_params_hevc;
   uint32_t deblocking_filter_hevc;
   uint32_t slice_control_h264;
   uint32_t spec_misc_h264;
   uint32_t enc_params_h264;
   uint32_t deblocking_filter_h264;
   uint32_t input_format;
   uint32_t output_format;
} rvcn_enc_cmd_t;

typedef struct rvcn_enc_quality_modes_s
{
   unsigned pre_encode_mode;
   unsigned vbaq_mode;
   unsigned preset_mode;
} rvcn_enc_quality_modes_t;

typedef void (*radeon_enc_get_buffer)(struct pipe_resource *resource, struct pb_buffer **handle,
                                      struct radeon_surf **surface);

struct pipe_video_codec *radeon_create_encoder(struct pipe_context *context,
                                               const struct pipe_video_codec *templat,
                                               struct radeon_winsys *ws,
                                               radeon_enc_get_buffer get_buffer);

struct radeon_enc_pic {
   enum pipe_h2645_enc_picture_type picture_type;

   unsigned frame_num;
   unsigned pic_order_cnt;
   unsigned pic_order_cnt_type;
   unsigned ref_idx_l0;
   unsigned ref_idx_l1;
   unsigned crop_left;
   unsigned crop_right;
   unsigned crop_top;
   unsigned crop_bottom;
   unsigned general_tier_flag;
   unsigned general_profile_idc;
   unsigned general_level_idc;
   unsigned max_poc;
   unsigned log2_max_poc;
   unsigned chroma_format_idc;
   unsigned pic_width_in_luma_samples;
   unsigned pic_height_in_luma_samples;
   unsigned log2_diff_max_min_luma_coding_block_size;
   unsigned log2_min_transform_block_size_minus2;
   unsigned log2_diff_max_min_transform_block_size;
   unsigned max_transform_hierarchy_depth_inter;
   unsigned max_transform_hierarchy_depth_intra;
   unsigned log2_parallel_merge_level_minus2;
   unsigned bit_depth_luma_minus8;
   unsigned bit_depth_chroma_minus8;
   unsigned nal_unit_type;
   unsigned max_num_merge_cand;
   unsigned temporal_id;
   unsigned num_temporal_layers;
   unsigned temporal_layer_pattern_index;
   rvcn_enc_quality_modes_t quality_modes;

   bool not_referenced;
   bool is_idr;
   bool is_even_frame;
   bool sample_adaptive_offset_enabled_flag;
   bool pcm_enabled_flag;
   bool sps_temporal_mvp_enabled_flag;

   rvcn_enc_session_info_t session_info;
   rvcn_enc_task_info_t task_info;
   rvcn_enc_session_init_t session_init;
   rvcn_enc_layer_control_t layer_ctrl;
   rvcn_enc_layer_select_t layer_sel;
   rvcn_enc_h264_slice_control_t slice_ctrl;
   rvcn_enc_hevc_slice_control_t hevc_slice_ctrl;
   rvcn_enc_h264_spec_misc_t spec_misc;
   rvcn_enc_hevc_spec_misc_t hevc_spec_misc;
   rvcn_enc_rate_ctl_session_init_t rc_session_init;
   rvcn_enc_rate_ctl_layer_init_t rc_layer_init[RENCODE_MAX_NUM_TEMPORAL_LAYERS];
   rvcn_enc_h264_encode_params_t h264_enc_params;
   rvcn_enc_h264_deblocking_filter_t h264_deblock;
   rvcn_enc_hevc_deblocking_filter_t hevc_deblock;
   rvcn_enc_rate_ctl_per_picture_t rc_per_pic;
   rvcn_enc_quality_params_t quality_params;
   rvcn_enc_encode_context_buffer_t ctx_buf;
   rvcn_enc_video_bitstream_buffer_t bit_buf;
   rvcn_enc_feedback_buffer_t fb_buf;
   rvcn_enc_intra_refresh_t intra_ref;
   rvcn_enc_encode_params_t enc_params;
};

struct radeon_encoder {
   struct pipe_video_codec base;

   void (*begin)(struct radeon_encoder *enc);
   void (*before_encode)(struct radeon_encoder *enc);
   void (*encode)(struct radeon_encoder *enc);
   void (*destroy)(struct radeon_encoder *enc);
   void (*session_info)(struct radeon_encoder *enc);
   void (*task_info)(struct radeon_encoder *enc, bool need_feedback);
   void (*session_init)(struct radeon_encoder *enc);
   void (*layer_control)(struct radeon_encoder *enc);
   void (*layer_select)(struct radeon_encoder *enc);
   void (*slice_control)(struct radeon_encoder *enc);
   void (*spec_misc)(struct radeon_encoder *enc);
   void (*rc_session_init)(struct radeon_encoder *enc);
   void (*rc_layer_init)(struct radeon_encoder *enc);
   void (*deblocking_filter)(struct radeon_encoder *enc);
   void (*quality_params)(struct radeon_encoder *enc);
   void (*nalu_sps)(struct radeon_encoder *enc);
   void (*nalu_pps)(struct radeon_encoder *enc);
   void (*nalu_vps)(struct radeon_encoder *enc);
   void (*nalu_aud)(struct radeon_encoder *enc);
   void (*nalu_sei)(struct radeon_encoder *enc);
   void (*nalu_prefix)(struct radeon_encoder *enc);
   void (*slice_header)(struct radeon_encoder *enc);
   void (*ctx)(struct radeon_encoder *enc);
   void (*bitstream)(struct radeon_encoder *enc);
   void (*feedback)(struct radeon_encoder *enc);
   void (*intra_refresh)(struct radeon_encoder *enc);
   void (*rc_per_pic)(struct radeon_encoder *enc);
   void (*encode_params)(struct radeon_encoder *enc);
   void (*encode_params_codec_spec)(struct radeon_encoder *enc);
   void (*op_init)(struct radeon_encoder *enc);
   void (*op_close)(struct radeon_encoder *enc);
   void (*op_enc)(struct radeon_encoder *enc);
   void (*op_init_rc)(struct radeon_encoder *enc);
   void (*op_init_rc_vbv)(struct radeon_encoder *enc);
   void (*op_preset)(struct radeon_encoder *enc);
   void (*encode_headers)(struct radeon_encoder *enc);
   void (*input_format)(struct radeon_encoder *enc);
   void (*output_format)(struct radeon_encoder *enc);
   /* mq is used for preversing multiple queue ibs */
   void (*mq_begin)(struct radeon_encoder *enc);
   void (*mq_encode)(struct radeon_encoder *enc);
   void (*mq_destroy)(struct radeon_encoder *enc);

   unsigned stream_handle;

   struct pipe_screen *screen;
   struct radeon_winsys *ws;
   struct radeon_cmdbuf cs;

   radeon_enc_get_buffer get_buffer;

   struct pb_buffer *handle;
   struct radeon_surf *luma;
   struct radeon_surf *chroma;

   struct pb_buffer *bs_handle;
   unsigned bs_size;

   struct rvid_buffer *si;
   struct rvid_buffer *fb;
   struct rvid_buffer *dpb;
   struct radeon_enc_pic enc_pic;
   rvcn_enc_cmd_t cmd;

   unsigned alignment;
   unsigned shifter;
   unsigned bits_in_shifter;
   unsigned num_zeros;
   unsigned byte_index;
   unsigned bits_output;
   unsigned bits_size;
   uint32_t total_task_size;
   uint32_t *p_task_size;
   struct rvcn_sq_var sq;

   bool emulation_prevention;
   bool need_feedback;
   unsigned dpb_size;
   rvcn_enc_picture_info_t dpb_info[RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES];
};

void radeon_enc_add_buffer(struct radeon_encoder *enc, struct pb_buffer *buf,
                           unsigned usage, enum radeon_bo_domain domain, signed offset);

void radeon_enc_set_emulation_prevention(struct radeon_encoder *enc, bool set);

void radeon_enc_output_one_byte(struct radeon_encoder *enc, unsigned char byte);

void radeon_enc_emulation_prevention(struct radeon_encoder *enc, unsigned char byte);

void radeon_enc_code_fixed_bits(struct radeon_encoder *enc, unsigned int value,
                                unsigned int num_bits);

void radeon_enc_reset(struct radeon_encoder *enc);

void radeon_enc_byte_align(struct radeon_encoder *enc);

void radeon_enc_flush_headers(struct radeon_encoder *enc);

void radeon_enc_code_ue(struct radeon_encoder *enc, unsigned int value);

void radeon_enc_code_se(struct radeon_encoder *enc, int value);

void radeon_enc_1_2_init(struct radeon_encoder *enc);

void radeon_enc_2_0_init(struct radeon_encoder *enc);

void radeon_enc_3_0_init(struct radeon_encoder *enc);

void radeon_enc_4_0_init(struct radeon_encoder *enc);

#endif // _RADEON_VCN_ENC_H
