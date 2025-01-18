/**************************************************************************
 *
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#ifndef _RADEON_VCN_ENC_H
#define _RADEON_VCN_ENC_H

#include "radeon_vcn.h"
#include "util/macros.h"
#include "radeon_bitstream.h"

#include "ac_vcn_enc.h"

#define PIPE_ALIGN_IN_BLOCK_SIZE(value, alignment) DIV_ROUND_UP(value, alignment)

#define RADEON_ENC_CS(value) (enc->cs.current.buf[enc->cs.current.cdw++] = (value))
#define RADEON_ENC_BEGIN(cmd)                                                                    \
   {                                                                                             \
      uint32_t *begin = &enc->cs.current.buf[enc->cs.current.cdw++];                             \
      RADEON_ENC_CS(cmd)
#define RADEON_ENC_READ(buf, domain, off)                                                        \
   radeon_enc_add_buffer(enc, (buf), RADEON_USAGE_READ, (domain), (off))
#define RADEON_ENC_WRITE(buf, domain, off)                                                       \
   radeon_enc_add_buffer(enc, (buf), RADEON_USAGE_WRITE, (domain), (off))
#define RADEON_ENC_READWRITE(buf, domain, off)                                                   \
   radeon_enc_add_buffer(enc, (buf), RADEON_USAGE_READWRITE, (domain), (off))
#define RADEON_ENC_END()                                                                         \
   *begin = (&enc->cs.current.buf[enc->cs.current.cdw] - begin) * 4;                             \
   enc->total_task_size += *begin;                                                               \
   }
#define RADEON_ENC_ADDR_SWAP()                                                                   \
   do {                                                                                          \
      unsigned int *low  = &enc->cs.current.buf[enc->cs.current.cdw - 2];                        \
      unsigned int *high = &enc->cs.current.buf[enc->cs.current.cdw - 1];                        \
      unsigned int temp = *low;                                                                  \
      *low = *high;                                                                              \
      *high = temp;                                                                              \
   } while(0)

#define RADEON_ENC_DESTROY_VIDEO_BUFFER(buf)                                                     \
   do {                                                                                          \
      if (buf) {                                                                                 \
         si_vid_destroy_buffer(buf);                                                             \
         FREE(buf);                                                                              \
         (buf) = NULL;                                                                           \
      }                                                                                          \
   } while(0)

#define RADEON_ENC_ERR(fmt, args...)                                                             \
   do {                                                                                          \
      enc->error = true;                                                                         \
      fprintf(stderr, "EE %s:%d %s VCN - " fmt, __FILE__, __LINE__, __func__, ##args);           \
   } while(0)

typedef void (*radeon_enc_get_buffer)(struct pipe_resource *resource, struct pb_buffer_lean **handle,
                                      struct radeon_surf **surface);

struct pipe_video_codec *radeon_create_encoder(struct pipe_context *context,
                                               const struct pipe_video_codec *templat,
                                               struct radeon_winsys *ws,
                                               radeon_enc_get_buffer get_buffer);

struct radeon_enc_dpb_buffer {
   struct pipe_video_buffer templ, *pre;

   struct si_texture *luma;      /* recon luma */
   struct si_texture *chroma;    /* recon chroma */
   struct rvid_buffer *fcb;      /* frame context buffer*/
   struct si_texture *pre_luma;  /* preenc recon luma */
   struct si_texture *pre_chroma;/* preenc recon chroma */
   struct rvid_buffer *pre_fcb;  /* preenc frame context buffer */
};

struct radeon_enc_pic {
   union {
      enum pipe_h2645_enc_picture_type picture_type;
      enum pipe_av1_enc_frame_type frame_type;
   };

   union {
      struct {
         struct pipe_h264_enc_picture_desc *desc;
      } h264;
      struct {
         struct pipe_h265_enc_picture_desc *desc;
      } hevc;
      struct {
         struct pipe_av1_enc_picture_desc *desc;
         uint32_t coded_width;
         uint32_t coded_height;
         bool compound;
         bool skip_mode_allowed;
      } av1;
   };

   unsigned pic_width_in_luma_samples;
   unsigned pic_height_in_luma_samples;
   unsigned bit_depth_luma_minus8;
   unsigned bit_depth_chroma_minus8;
   unsigned nal_unit_type;
   unsigned temporal_id;
   unsigned num_temporal_layers;
   unsigned total_coloc_bytes;
   rvcn_enc_quality_modes_t quality_modes;

   bool not_referenced;
   bool use_rc_per_pic_ex;
   bool av1_tile_splitting_legacy_flag;

   struct {
      union {
         struct
         {
            uint32_t av1_cdf_frame_context_offset;
            uint32_t av1_cdef_algorithm_context_offset;
         } av1;
         struct
         {
            uint32_t colloc_buffer_offset;
         } h264;
      };
   } fcb_offset;

   struct radeon_enc_dpb_buffer *dpb_bufs[RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES];

   struct {
      struct {
         struct {
            uint32_t enable_error_resilient_mode:1;
            uint32_t force_integer_mv:1;
            uint32_t disable_screen_content_tools:1;
            uint32_t is_obu_frame:1;
         };
         uint32_t *copy_start;
      };
      rvcn_enc_av1_spec_misc_t av1_spec_misc;
      rvcn_enc_av1_cdf_default_table_t av1_cdf_default_table;
   };

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
   rvcn_enc_hevc_encode_params_t hevc_enc_params;
   rvcn_enc_av1_encode_params_t av1_enc_params;
   rvcn_enc_av1_tile_config_t av1_tile_config;
   rvcn_enc_rate_ctl_per_picture_t rc_per_pic;
   rvcn_enc_quality_params_t quality_params;
   rvcn_enc_encode_context_buffer_t ctx_buf;
   rvcn_enc_video_bitstream_buffer_t bit_buf;
   rvcn_enc_feedback_buffer_t fb_buf;
   rvcn_enc_intra_refresh_t intra_refresh;
   rvcn_enc_encode_params_t enc_params;
   rvcn_enc_stats_t enc_statistics;
   rvcn_enc_input_format_t enc_input_format;
   rvcn_enc_output_format_t enc_output_format;
   rvcn_enc_qp_map_t enc_qp_map;
   rvcn_enc_metadata_buffer_t metadata;
   rvcn_enc_latency_t enc_latency;
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
   void (*slice_header)(struct radeon_encoder *enc);
   void (*ctx)(struct radeon_encoder *enc);
   void (*bitstream)(struct radeon_encoder *enc);
   void (*feedback)(struct radeon_encoder *enc);
   void (*intra_refresh)(struct radeon_encoder *enc);
   void (*rc_per_pic)(struct radeon_encoder *enc);
   void (*encode_params)(struct radeon_encoder *enc);
   void (*encode_params_codec_spec)(struct radeon_encoder *enc);
   void (*qp_map)(struct radeon_encoder *enc);
   void (*op_init)(struct radeon_encoder *enc);
   void (*op_close)(struct radeon_encoder *enc);
   void (*op_enc)(struct radeon_encoder *enc);
   void (*op_init_rc)(struct radeon_encoder *enc);
   void (*op_init_rc_vbv)(struct radeon_encoder *enc);
   void (*op_preset)(struct radeon_encoder *enc);
   void (*encode_headers)(struct radeon_encoder *enc);
   void (*input_format)(struct radeon_encoder *enc);
   void (*output_format)(struct radeon_encoder *enc);
   void (*encode_statistics)(struct radeon_encoder *enc);
   void (*obu_instructions)(struct radeon_encoder *enc);
   void (*cdf_default_table)(struct radeon_encoder *enc);
   void (*ctx_override)(struct radeon_encoder *enc);
   void (*metadata)(struct radeon_encoder *enc);
   void (*tile_config)(struct radeon_encoder *enc);
   void (*encode_latency)(struct radeon_encoder *enc);
   /* mq is used for preversing multiple queue ibs */
   void (*mq_begin)(struct radeon_encoder *enc);
   void (*mq_encode)(struct radeon_encoder *enc);
   void (*mq_destroy)(struct radeon_encoder *enc);

   unsigned stream_handle;

   struct pipe_screen *screen;
   struct radeon_winsys *ws;
   struct radeon_cmdbuf cs;

   radeon_enc_get_buffer get_buffer;

   struct pb_buffer_lean *handle;
   struct radeon_surf *luma;
   struct radeon_surf *chroma;
   struct pipe_video_buffer *source;

   struct pb_buffer_lean *bs_handle;
   unsigned bs_size;
   unsigned bs_offset;

   struct rvid_buffer *si;
   struct rvid_buffer *fb;
   struct rvid_buffer *dpb;
   struct rvid_buffer *cdf;
   struct rvid_buffer *roi;
   struct rvid_buffer *meta;
   struct radeon_enc_pic enc_pic;
   struct pb_buffer_lean *stats;
   rvcn_enc_cmd_t cmd;

   unsigned alignment;
   uint32_t total_task_size;
   uint32_t *p_task_size;
   struct rvcn_sq_var sq;

   bool need_feedback;
   bool need_rate_control;
   bool need_rc_per_pic;
   bool need_spec_misc;
   unsigned dpb_size;
   unsigned dpb_slots;
   unsigned roi_size;
   unsigned metadata_size;

   bool error;

   enum {
      DPB_LEGACY = 0,
      DPB_TIER_2
   } dpb_type;

   struct pipe_context *ectx;
};

struct rvcn_enc_output_unit_segment {
   bool is_slice;
   unsigned size;
   unsigned offset;
};

struct rvcn_enc_feedback_data {
   unsigned num_segments;
   struct rvcn_enc_output_unit_segment segments[];
};

/* structure for determining av1 tile division scheme.
 * In one direction, it is trying to split width/height into two parts,
 * main and  border, each of which has a length (number of sbs),
 * Therefore, it has two possible tile sizes, even with multiple
 * tiles, and in non-uniformed case, it is trying to make tile sizes
 * as similar as possible.
 */

struct tile_1d_layout {
   bool     uniform_tile_flag;
   uint32_t nb_main_sb;     /* if non-uniform, it means the first part */
   uint32_t nb_border_sb;   /* if non-uniform, it means the second part */
   uint32_t nb_main_tile;
   uint32_t nb_border_tile;
};

void radeon_enc_add_buffer(struct radeon_encoder *enc, struct pb_buffer_lean *buf,
                           unsigned usage, enum radeon_bo_domain domain, signed offset);

void radeon_enc_dummy(struct radeon_encoder *enc);

void radeon_enc_code_leb128(unsigned char *buf, unsigned int value,
                            unsigned int num_bytes);

void radeon_enc_1_2_init(struct radeon_encoder *enc);

void radeon_enc_2_0_init(struct radeon_encoder *enc);

void radeon_enc_3_0_init(struct radeon_encoder *enc);

void radeon_enc_4_0_init(struct radeon_encoder *enc);

void radeon_enc_5_0_init(struct radeon_encoder *enc);

unsigned int radeon_enc_write_sps(struct radeon_encoder *enc, uint8_t nal_byte, uint8_t *out);

unsigned int radeon_enc_write_pps(struct radeon_encoder *enc, uint8_t nal_byte, uint8_t *out);

unsigned int radeon_enc_write_vps(struct radeon_encoder *enc, uint8_t *out);

unsigned int radeon_enc_write_sps_hevc(struct radeon_encoder *enc, uint8_t *out);

unsigned int radeon_enc_write_pps_hevc(struct radeon_encoder *enc, uint8_t *out);

unsigned int radeon_enc_write_sequence_header(struct radeon_encoder *enc, uint8_t *obu_bytes, uint8_t *out);

void radeon_enc_av1_bs_instruction_type(struct radeon_encoder *enc,
                                        struct radeon_bitstream *bs,
                                        unsigned int inst, unsigned int obu_type);

void radeon_enc_av1_obu_header(struct radeon_encoder *enc, struct radeon_bitstream *bs, uint32_t obu_type);

void radeon_enc_av1_frame_header_common(struct radeon_encoder *enc, struct radeon_bitstream *bs, bool frame_header);

void radeon_enc_av1_tile_group(struct radeon_encoder *enc, struct radeon_bitstream *bs);

unsigned int radeon_enc_value_bits(unsigned int value);

unsigned int radeon_enc_av1_tile_log2(unsigned int blk_size, unsigned int max);

bool radeon_enc_is_av1_uniform_tile (uint32_t nb_sb, uint32_t nb_tiles,
                                     uint32_t min_nb_sb, struct tile_1d_layout *p);

void radeon_enc_av1_tile_layout (uint32_t nb_sb, uint32_t nb_tiles, uint32_t min_nb_sb,
                                 struct tile_1d_layout *p);

bool radeon_enc_av1_skip_mode_allowed(struct radeon_encoder *enc, uint32_t frames[2]);

void radeon_enc_create_dpb_aux_buffers(struct radeon_encoder *enc,
                                       struct radeon_enc_dpb_buffer *buf);

#endif // _RADEON_VCN_ENC_H
