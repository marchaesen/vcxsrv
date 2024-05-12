/**************************************************************************
 *
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#include <stdio.h>

#include "pipe/p_video_codec.h"

#include "util/u_video.h"

#include "si_pipe.h"
#include "radeon_vcn_enc.h"

#define RENCODE_FW_INTERFACE_MAJOR_VERSION   0
#define RENCODE_FW_INTERFACE_MINOR_VERSION   0

#define RENCODE_REC_SWIZZLE_MODE_256B_D_VCN5                        1

#define RENCODE_IB_PARAM_RATE_CONTROL_PER_PICTURE          0x00000008
#define RENCODE_IB_PARAM_METADATA_BUFFER                   0x0000001c
#define RENCODE_IB_PARAM_ENCODE_CONTEXT_BUFFER_OVERRIDE    0x0000001d
#define RENCODE_IB_PARAM_HEVC_ENCODE_PARAMS                0x00100004

#define RENCODE_AV1_IB_PARAM_TILE_CONFIG                   0x00300002
#define RENCODE_AV1_IB_PARAM_BITSTREAM_INSTRUCTION         0x00300003
#define RENCODE_IB_PARAM_AV1_ENCODE_PARAMS                 0x00300004


static void radeon_enc_cdf_default_table(struct radeon_encoder *enc)
{
   bool use_cdf_default = enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_KEY ||
                          enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_INTRA_ONLY ||
                          enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_SWITCH;

   enc->enc_pic.av1_cdf_default_table.use_cdf_default = use_cdf_default ? 1 : 0;

   RADEON_ENC_BEGIN(enc->cmd.cdf_default_table_av1);
   RADEON_ENC_CS(enc->enc_pic.av1_cdf_default_table.use_cdf_default);
   RADEON_ENC_READWRITE(enc->cdf->res->buf, enc->cdf->res->domains, 0);
   RADEON_ENC_END();
}

static void radeon_enc_spec_misc(struct radeon_encoder *enc)
{
   enc->enc_pic.spec_misc.constrained_intra_pred_flag = 0;
   enc->enc_pic.spec_misc.transform_8x8_mode = 0;
   enc->enc_pic.spec_misc.half_pel_enabled = 1;
   enc->enc_pic.spec_misc.quarter_pel_enabled = 1;
   enc->enc_pic.spec_misc.level_idc = enc->base.level;
   enc->enc_pic.spec_misc.weighted_bipred_idc = 0;

   RADEON_ENC_BEGIN(enc->cmd.spec_misc_h264);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.constrained_intra_pred_flag);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.cabac_enable);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.cabac_init_idc);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.transform_8x8_mode);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.half_pel_enabled);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.quarter_pel_enabled);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.profile_idc);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.level_idc);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.b_picture_enabled);
   RADEON_ENC_CS(enc->enc_pic.spec_misc.weighted_bipred_idc);
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

   if (enc->luma->meta_offset) {
      RVID_ERR("DCC surfaces not supported.\n");
      assert(false);
   }

   enc->enc_pic.enc_params.allowed_max_bitstream_size = enc->bs_size;
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
   RADEON_ENC_CS(enc->enc_pic.enc_params.reconstructed_picture_index);
   RADEON_ENC_END();
}

static void radeon_enc_encode_params_h264(struct radeon_encoder *enc)
{
   enc->enc_pic.h264_enc_params.input_picture_structure = RENCODE_H264_PICTURE_STRUCTURE_FRAME;
   enc->enc_pic.h264_enc_params.input_pic_order_cnt = 0;
   enc->enc_pic.h264_enc_params.is_reference = !enc->enc_pic.not_referenced;
   enc->enc_pic.h264_enc_params.is_long_term = enc->enc_pic.is_ltr;
   enc->enc_pic.h264_enc_params.interlaced_mode = RENCODE_H264_INTERLACING_MODE_PROGRESSIVE;

   if (enc->enc_pic.enc_params.reference_picture_index != 0xFFFFFFFF){
      enc->enc_pic.h264_enc_params.lsm_reference_pictures[0].list = 0;
      enc->enc_pic.h264_enc_params.lsm_reference_pictures[0].list_index = 0;
      enc->enc_pic.h264_enc_params.ref_list0[0] =
               enc->enc_pic.enc_params.reference_picture_index;
      enc->enc_pic.h264_enc_params.num_active_references_l0 = 1;
   } else {
      enc->enc_pic.h264_enc_params.lsm_reference_pictures[0].list = 0;
      enc->enc_pic.h264_enc_params.lsm_reference_pictures[0].list_index = 0xFFFFFFFF;
      enc->enc_pic.h264_enc_params.ref_list0[0] = 0xFFFFFFFF;
      enc->enc_pic.h264_enc_params.num_active_references_l0 = 0;
   }

   if (enc->enc_pic.h264_enc_params.l1_reference_picture0_index != 0xFFFFFFFF) {
      enc->enc_pic.h264_enc_params.lsm_reference_pictures[1].list = 1;
      enc->enc_pic.h264_enc_params.lsm_reference_pictures[1].list_index = 0;
      enc->enc_pic.h264_enc_params.ref_list1[0] =
               enc->enc_pic.h264_enc_params.l1_reference_picture0_index;
      enc->enc_pic.h264_enc_params.num_active_references_l1 = 1;
   } else {
      enc->enc_pic.h264_enc_params.lsm_reference_pictures[1].list = 0;
      enc->enc_pic.h264_enc_params.lsm_reference_pictures[1].list_index = 0xFFFFFFFF;
      enc->enc_pic.h264_enc_params.ref_list0[1] = 0;
      enc->enc_pic.h264_enc_params.ref_list1[0] = 0;
      enc->enc_pic.h264_enc_params.num_active_references_l1 = 0;
   }

   RADEON_ENC_BEGIN(enc->cmd.enc_params_h264);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.input_picture_structure);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.input_pic_order_cnt);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.is_reference);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.is_long_term);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.interlaced_mode);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.ref_list0[0]);
   for (int i = 1; i < RENCODE_H264_MAX_REFERENCE_LIST_SIZE; i++)
      RADEON_ENC_CS(0x00000000);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.num_active_references_l0);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.ref_list1[0]);
   for (int i = 1; i < RENCODE_H264_MAX_REFERENCE_LIST_SIZE; i++)
      RADEON_ENC_CS(0x00000000);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.num_active_references_l1);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.lsm_reference_pictures[0].list);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.lsm_reference_pictures[0].list_index);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.lsm_reference_pictures[1].list);
   RADEON_ENC_CS(enc->enc_pic.h264_enc_params.lsm_reference_pictures[1].list_index);
   RADEON_ENC_END();
}

static void radeon_enc_spec_misc_av1(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.spec_misc_av1);
   RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.palette_mode_enable);
   RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.mv_precision);
   RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.cdef_mode);
   RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.cdef_bits);
   RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.cdef_damping_minus3);
   for (int i = 0; i < RENCODE_AV1_CDEF_MAX_NUM; i++)
      RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.cdef_y_pri_strength[i]);
   for (int i = 0; i < RENCODE_AV1_CDEF_MAX_NUM; i++)
      RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.cdef_y_sec_strength[i]);
   for (int i = 0; i < RENCODE_AV1_CDEF_MAX_NUM; i++)
      RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.cdef_uv_pri_strength[i]);
   for (int i = 0; i < RENCODE_AV1_CDEF_MAX_NUM; i++)
      RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.cdef_uv_sec_strength[i]);
   RADEON_ENC_CS(0);
   RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.disable_cdf_update);
   RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.disable_frame_end_update_cdf);
   RADEON_ENC_CS(0);
   RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.delta_q_y_dc);
   RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.delta_q_u_dc);
   RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.delta_q_u_ac);
   RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.delta_q_v_dc);
   RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.delta_q_v_ac);
   RADEON_ENC_CS(0);
   RADEON_ENC_CS(0);
   RADEON_ENC_END();
}

static uint32_t radeon_enc_ref_swizzle_mode(struct radeon_encoder *enc)
{
   /* return RENCODE_REC_SWIZZLE_MODE_LINEAR; for debugging purpose */
   return RENCODE_REC_SWIZZLE_MODE_256B_D_VCN5;
}

static void radeon_enc_ctx(struct radeon_encoder *enc)
{
   int i;
   uint32_t swizzle_mode = radeon_enc_ref_swizzle_mode(enc);
   bool is_h264 = u_reduce_video_profile(enc->base.profile)
                             == PIPE_VIDEO_FORMAT_MPEG4_AVC;
   bool is_av1 = u_reduce_video_profile(enc->base.profile)
                             == PIPE_VIDEO_FORMAT_AV1;

   RADEON_ENC_BEGIN(enc->cmd.ctx);
   RADEON_ENC_READWRITE(enc->dpb->res->buf, enc->dpb->res->domains, 0);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.num_reconstructed_pictures);

   for (i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      rvcn_enc_reconstructed_picture_t *pic =
                            &enc->enc_pic.ctx_buf.reconstructed_pictures[i];
      RADEON_ENC_READWRITE(enc->dpb->res->buf, enc->dpb->res->domains, 0);
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.rec_luma_pitch);
      RADEON_ENC_READWRITE(enc->dpb->res->buf, enc->dpb->res->domains, 0);
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.rec_chroma_pitch);
      RADEON_ENC_READWRITE(enc->dpb->res->buf, enc->dpb->res->domains, 0);
      RADEON_ENC_CS(0);
      RADEON_ENC_CS(swizzle_mode);
      RADEON_ENC_READWRITE(enc->meta->res->buf, enc->meta->res->domains,
                           pic->frame_context_buffer_offset);
      if (is_h264) {
         RADEON_ENC_CS(pic->h264.colloc_buffer_offset);
         RADEON_ENC_CS(0);
      } else if (is_av1) {
         RADEON_ENC_CS(pic->av1.av1_cdf_frame_context_offset);
         RADEON_ENC_CS(pic->av1.av1_cdef_algorithm_context_offset);
      } else {
         RADEON_ENC_CS(0);
         RADEON_ENC_CS(0);
      }
      RADEON_ENC_CS(pic->encode_metadata_offset);
   }

   for (int i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      rvcn_enc_reconstructed_picture_t *pic =
                            &enc->enc_pic.ctx_buf.pre_encode_reconstructed_pictures[i];
      RADEON_ENC_READWRITE(enc->dpb->res->buf, enc->dpb->res->domains, 0);
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.rec_luma_pitch);
      RADEON_ENC_READWRITE(enc->dpb->res->buf, enc->dpb->res->domains, 0);
      RADEON_ENC_CS(enc->enc_pic.ctx_buf.rec_chroma_pitch);
      RADEON_ENC_READWRITE(enc->dpb->res->buf, enc->dpb->res->domains, 0);
      RADEON_ENC_CS(0);
      RADEON_ENC_CS(swizzle_mode);
      RADEON_ENC_READWRITE(enc->meta->res->buf, enc->meta->res->domains,
                           pic->frame_context_buffer_offset);
      if (is_h264) {
         RADEON_ENC_CS(pic->h264.colloc_buffer_offset);
         RADEON_ENC_CS(0);
      } else if (is_av1) {
         RADEON_ENC_CS(pic->av1.av1_cdf_frame_context_offset);
         RADEON_ENC_CS(pic->av1.av1_cdef_algorithm_context_offset);
      } else {
         RADEON_ENC_CS(0);
         RADEON_ENC_CS(0);
      }
      RADEON_ENC_CS(pic->encode_metadata_offset);
   }

   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_picture_luma_pitch);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_picture_chroma_pitch);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_input_picture.rgb.red_offset);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_input_picture.rgb.green_offset);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.pre_encode_input_picture.rgb.blue_offset);
   RADEON_ENC_CS(enc->enc_pic.ctx_buf.av1.av1_sdb_intermediate_context_offset);
   RADEON_ENC_END();
}

static void radeon_enc_ctx_override(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.ctx_override);
   for (int i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      rvcn_enc_reconstructed_picture_t *pic =
                            &enc->enc_pic.ctx_buf.reconstructed_pictures[i];
      RADEON_ENC_CS(pic->luma_offset);
      RADEON_ENC_CS(pic->chroma_offset);
      RADEON_ENC_CS(pic->chroma_v_offset);
   }
   for (int i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      rvcn_enc_reconstructed_picture_t *pic =
                            &enc->enc_pic.ctx_buf.pre_encode_reconstructed_pictures[i];
      RADEON_ENC_CS(pic->luma_offset);
      RADEON_ENC_CS(pic->chroma_offset);
      RADEON_ENC_CS(pic->chroma_v_offset);
   }
   RADEON_ENC_END();
}

static void radeon_enc_metadata(struct radeon_encoder *enc)
{
   enc->enc_pic.metadata.two_pass_search_center_map_offset =
               enc->enc_pic.ctx_buf.two_pass_search_center_map_offset;
   RADEON_ENC_BEGIN(enc->cmd.metadata);
   RADEON_ENC_READWRITE(enc->meta->res->buf, enc->meta->res->domains, 0);
   RADEON_ENC_CS(enc->enc_pic.metadata.two_pass_search_center_map_offset);
   RADEON_ENC_END();
}

static void radeon_enc_output_format(struct radeon_encoder *enc)
{
   enc->enc_pic.enc_output_format.output_chroma_subsampling = 0;

   RADEON_ENC_BEGIN(enc->cmd.output_format);
   RADEON_ENC_CS(enc->enc_pic.enc_output_format.output_color_volume);
   RADEON_ENC_CS(enc->enc_pic.enc_output_format.output_color_range);
   RADEON_ENC_CS(enc->enc_pic.enc_output_format.output_chroma_subsampling);
   RADEON_ENC_CS(enc->enc_pic.enc_output_format.output_chroma_location);
   RADEON_ENC_CS(enc->enc_pic.enc_output_format.output_color_bit_depth);
   RADEON_ENC_END();
}

static void radeon_enc_rc_per_pic(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.rc_per_pic);
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

static void radeon_enc_encode_params_hevc(struct radeon_encoder *enc)
{
   enc->enc_pic.hevc_enc_params.lsm_reference_pictures_list_index = 0;
   enc->enc_pic.hevc_enc_params.ref_list0[0] =
            enc->enc_pic.enc_params.reference_picture_index;
   enc->enc_pic.hevc_enc_params.num_active_references_l0 =
            (enc->enc_pic.enc_params.pic_type == RENCODE_PICTURE_TYPE_I) ? 0 : 1;

   RADEON_ENC_BEGIN(enc->cmd.enc_params_hevc);
   RADEON_ENC_CS(enc->enc_pic.hevc_enc_params.ref_list0[0]);
   for (int i = 1; i < RENCODE_HEVC_MAX_REFERENCE_LIST_SIZE; i++)
      RADEON_ENC_CS(0x00000000);
   RADEON_ENC_CS(enc->enc_pic.hevc_enc_params.num_active_references_l0);
   RADEON_ENC_CS(enc->enc_pic.hevc_enc_params.lsm_reference_pictures_list_index);
   RADEON_ENC_END();
}

static void radeon_enc_spec_misc_hevc(struct radeon_encoder *enc)
{
   enc->enc_pic.hevc_spec_misc.transform_skip_discarded = 0;
   enc->enc_pic.hevc_spec_misc.cu_qp_delta_enabled_flag = 0;

   RADEON_ENC_BEGIN(enc->cmd.spec_misc_hevc);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.log2_min_luma_coding_block_size_minus3);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.amp_disabled);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.strong_intra_smoothing_enabled);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.constrained_intra_pred_flag);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.cabac_init_flag);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.half_pel_enabled);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.quarter_pel_enabled);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.transform_skip_discarded);
   RADEON_ENC_CS(0);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.cu_qp_delta_enabled_flag);
   RADEON_ENC_END();
}

/* TODO */
static void radeon_enc_tile_config_av1(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.tile_config_av1);
   RADEON_ENC_END();
}

void radeon_enc_5_0_init(struct radeon_encoder *enc)
{
   radeon_enc_4_0_init(enc);

   enc->ctx = radeon_enc_ctx;
   enc->output_format = radeon_enc_output_format;
   enc->metadata = radeon_enc_metadata;
   enc->ctx_override = radeon_enc_ctx_override;
   enc->encode_params = radeon_enc_encode_params;
   enc->rc_per_pic = radeon_enc_rc_per_pic;

   if (u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC) {
      enc->spec_misc = radeon_enc_spec_misc;
      enc->encode_params_codec_spec = radeon_enc_encode_params_h264;
   } else if (u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_HEVC) {
      enc->encode_params_codec_spec = radeon_enc_encode_params_hevc;
      enc->spec_misc = radeon_enc_spec_misc_hevc;
   } else if (u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_AV1) {
      /* TODO adding other functions*/
      enc->cdf_default_table = radeon_enc_cdf_default_table;
      enc->spec_misc = radeon_enc_spec_misc_av1;
      enc->tile_config = radeon_enc_tile_config_av1;
   }

   enc->cmd.rc_per_pic = RENCODE_IB_PARAM_RATE_CONTROL_PER_PICTURE;
   enc->cmd.metadata = RENCODE_IB_PARAM_METADATA_BUFFER;
   enc->cmd.ctx_override = RENCODE_IB_PARAM_ENCODE_CONTEXT_BUFFER_OVERRIDE;
   enc->cmd.enc_params_hevc = RENCODE_IB_PARAM_HEVC_ENCODE_PARAMS;
   enc->cmd.tile_config_av1 = RENCODE_AV1_IB_PARAM_TILE_CONFIG;
   enc->cmd.bitstream_instruction_av1 = RENCODE_AV1_IB_PARAM_BITSTREAM_INSTRUCTION;

   enc->enc_pic.session_info.interface_version =
      ((RENCODE_FW_INTERFACE_MAJOR_VERSION << RENCODE_IF_MAJOR_VERSION_SHIFT) |
      (RENCODE_FW_INTERFACE_MINOR_VERSION << RENCODE_IF_MINOR_VERSION_SHIFT));
}
