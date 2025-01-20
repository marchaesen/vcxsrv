/**************************************************************************
 *
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/
#include "pipe/p_video_codec.h"

#include "util/u_video.h"

#include "si_pipe.h"
#include "radeon_vcn_enc.h"

#define RENCODE_FW_INTERFACE_MAJOR_VERSION   1
#define RENCODE_FW_INTERFACE_MINOR_VERSION   3

#define RENCODE_AV1_MIN_TILE_WIDTH                         256

static void radeon_enc_cdf_default_table(struct radeon_encoder *enc)
{
   bool use_cdf_default = enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_KEY ||
                          enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_INTRA_ONLY ||
                          enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_SWITCH ||
                          (enc->enc_pic.enable_error_resilient_mode);

   enc->enc_pic.av1_cdf_default_table.use_cdf_default = use_cdf_default ? 1 : 0;

   RADEON_ENC_BEGIN(enc->cmd.cdf_default_table_av1);
   RADEON_ENC_CS(enc->enc_pic.av1_cdf_default_table.use_cdf_default);
   RADEON_ENC_READWRITE(enc->cdf->res->buf, enc->cdf->res->domains, 0);
   RADEON_ENC_END();
}

static void radeon_enc_spec_misc(struct radeon_encoder *enc)
{
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

   bool is_av1 = u_reduce_video_profile(enc->base.profile)
                             == PIPE_VIDEO_FORMAT_AV1;
   if ( !is_av1 ) {
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
   } else {
      switch (enc->enc_pic.frame_type) {
         case PIPE_AV1_ENC_FRAME_TYPE_KEY:
            enc->enc_pic.enc_params.pic_type = RENCODE_PICTURE_TYPE_I;
            break;
         case PIPE_AV1_ENC_FRAME_TYPE_INTRA_ONLY:
            enc->enc_pic.enc_params.pic_type = RENCODE_PICTURE_TYPE_I;
            break;
         case PIPE_AV1_ENC_FRAME_TYPE_INTER:
         case PIPE_AV1_ENC_FRAME_TYPE_SWITCH:
            if (enc->enc_pic.av1.compound)
               enc->enc_pic.enc_params.pic_type = RENCODE_PICTURE_TYPE_B;
            else
               enc->enc_pic.enc_params.pic_type = RENCODE_PICTURE_TYPE_P;
            break;
         default:
            assert(0); /* never come to this condition */
      }
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
   RADEON_ENC_CS(enc->enc_pic.enc_params.reconstructed_picture_index);
   RADEON_ENC_END();
}

static void radeon_enc_encode_params_h264(struct radeon_encoder *enc)
{
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
   /* if enabled using the input parameters, it is required to have cdef_bits
    * > 0 */
   if (enc->enc_pic.av1_spec_misc.cdef_mode && !!(enc->enc_pic.av1_spec_misc.cdef_bits))
      enc->enc_pic.av1_spec_misc.cdef_mode = RENCODE_AV1_CDEF_MODE_EXPLICIT;
   else if (enc->enc_pic.av1_spec_misc.cdef_mode)
      enc->enc_pic.av1_spec_misc.cdef_mode = RENCODE_AV1_CDEF_MODE_DEFAULT;

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
   RADEON_ENC_CS(enc->enc_pic.av1_spec_misc.disallow_skip_mode);
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

static void radeon_enc_ctx_tier2(struct radeon_encoder *enc)
{
   uint32_t num_refs = 0;
   bool is_h264 = u_reduce_video_profile(enc->base.profile)
                             == PIPE_VIDEO_FORMAT_MPEG4_AVC;
   bool is_av1 = u_reduce_video_profile(enc->base.profile)
                             == PIPE_VIDEO_FORMAT_AV1;

   for (uint32_t i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      if (enc->enc_pic.dpb_bufs[i]) {
         radeon_enc_create_dpb_aux_buffers(enc, enc->enc_pic.dpb_bufs[i]);
         num_refs = i + 1;
      }
   }

   RADEON_ENC_BEGIN(enc->cmd.ctx);
   if (enc->dpb->res) {
      RADEON_ENC_READWRITE(enc->dpb->res->buf, enc->dpb->res->domains, 0);
   } else {
      RADEON_ENC_CS(0);
      RADEON_ENC_CS(0);
   }
   RADEON_ENC_CS(num_refs);

   for (uint32_t i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      if (!enc->enc_pic.dpb_bufs[i]) {
         for (int j = 0; j < 15; j++)
            RADEON_ENC_CS(0);
         continue;
      }
      struct si_texture *luma = enc->enc_pic.dpb_bufs[i]->luma;
      struct si_texture *chroma = enc->enc_pic.dpb_bufs[i]->chroma;
      struct rvid_buffer *fcb = enc->enc_pic.dpb_bufs[i]->fcb;
      RADEON_ENC_READWRITE(luma->buffer.buf, luma->buffer.domains, luma->surface.u.gfx9.surf_offset);
      RADEON_ENC_CS(luma->surface.u.gfx9.surf_pitch);
      RADEON_ENC_READWRITE(chroma->buffer.buf, chroma->buffer.domains, chroma->surface.u.gfx9.surf_offset);
      RADEON_ENC_CS(chroma->surface.u.gfx9.surf_pitch);
      RADEON_ENC_CS(0);
      RADEON_ENC_CS(0);
      RADEON_ENC_CS(0);
      RADEON_ENC_CS(luma->surface.u.gfx9.swizzle_mode);
      RADEON_ENC_READWRITE(fcb->res->buf, fcb->res->domains, 0);
      if (is_h264) {
         RADEON_ENC_CS(enc->enc_pic.fcb_offset.h264.colloc_buffer_offset);
         RADEON_ENC_CS(0);
      } else if (is_av1) {
         RADEON_ENC_CS(enc->enc_pic.fcb_offset.av1.av1_cdf_frame_context_offset);
         RADEON_ENC_CS(enc->enc_pic.fcb_offset.av1.av1_cdef_algorithm_context_offset);
      } else {
         RADEON_ENC_CS(0);
         RADEON_ENC_CS(0);
      }
      RADEON_ENC_CS(0);
   }

   /* pre-encoding */
   for (uint32_t i = 0; i < RENCODE_MAX_NUM_RECONSTRUCTED_PICTURES; i++) {
      if (!enc->enc_pic.quality_modes.pre_encode_mode || !enc->enc_pic.dpb_bufs[i]) {
         for (int j = 0; j < 15; j++)
            RADEON_ENC_CS(0);
         continue;
      }
      struct si_texture *luma = enc->enc_pic.dpb_bufs[i]->pre_luma;
      struct si_texture *chroma = enc->enc_pic.dpb_bufs[i]->pre_chroma;
      struct rvid_buffer *fcb = enc->enc_pic.dpb_bufs[i]->pre_fcb;
      RADEON_ENC_READWRITE(luma->buffer.buf, luma->buffer.domains, luma->surface.u.gfx9.surf_offset);
      RADEON_ENC_CS(luma->surface.u.gfx9.surf_pitch);
      RADEON_ENC_READWRITE(chroma->buffer.buf, chroma->buffer.domains, chroma->surface.u.gfx9.surf_offset);
      RADEON_ENC_CS(chroma->surface.u.gfx9.surf_pitch);
      RADEON_ENC_CS(0);
      RADEON_ENC_CS(0);
      RADEON_ENC_CS(0);
      RADEON_ENC_CS(luma->surface.u.gfx9.swizzle_mode);
      RADEON_ENC_READWRITE(fcb->res->buf, fcb->res->domains, 0);
      if (is_h264) {
         RADEON_ENC_CS(enc->enc_pic.fcb_offset.h264.colloc_buffer_offset);
         RADEON_ENC_CS(0);
      } else if (is_av1) {
         RADEON_ENC_CS(enc->enc_pic.fcb_offset.av1.av1_cdf_frame_context_offset);
         RADEON_ENC_CS(enc->enc_pic.fcb_offset.av1.av1_cdef_algorithm_context_offset);
      } else {
         RADEON_ENC_CS(0);
         RADEON_ENC_CS(0);
      }
      RADEON_ENC_CS(0);
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
   if (!enc->meta)
      return;

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
   RADEON_ENC_CS(enc->enc_pic.rc_per_pic.qvbr_quality_level);
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

static void radeon_enc_encode_params_av1(struct radeon_encoder *enc)
{
   RADEON_ENC_BEGIN(enc->cmd.enc_params_av1);
   for (int i = 0; i < RENCODE_AV1_REFS_PER_FRAME; i++)
      RADEON_ENC_CS(enc->enc_pic.av1_enc_params.ref_frames[i]);
   RADEON_ENC_CS(enc->enc_pic.av1_enc_params.lsm_reference_frame_index[0]);
   RADEON_ENC_CS(enc->enc_pic.av1_enc_params.lsm_reference_frame_index[1]);
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
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.transform_skip_disabled);
   RADEON_ENC_CS(0);
   RADEON_ENC_CS(enc->enc_pic.hevc_spec_misc.cu_qp_delta_enabled_flag);
   RADEON_ENC_END();
}

/* nb_sb: number of super blocks in width/height
 * nb_tiles: number of tiles trying to partition
 * min_nb_sb: the minimum amount of sbs in a tile
 */
bool radeon_enc_is_av1_uniform_tile (uint32_t nb_sb, uint32_t nb_tiles,
                                     uint32_t min_nb_sb, struct tile_1d_layout *p)
{
   if (!min_nb_sb)
      min_nb_sb = 1;

   if (IS_POT_NONZERO(nb_tiles)) {
      uint32_t nb_main_sb = DIV_ROUND_UP(nb_sb, nb_tiles);
      uint32_t nb_main_tile = nb_sb / nb_main_sb;
      uint32_t nb_remainder_sb = nb_sb % nb_main_sb;

      /* all nb in tile has to be larger than min_nb_sb */
      if (nb_main_sb < min_nb_sb)
         return false;

      /* if remainder exists it needs to larger than min_nb_sb */
      if ((nb_remainder_sb && (nb_remainder_sb < min_nb_sb))
         || ((nb_main_sb * nb_main_tile + nb_remainder_sb) != nb_sb)
         || (nb_main_tile + !!(nb_remainder_sb) != nb_tiles))
         return false;

      p->nb_main_sb     = nb_main_sb;
      p->nb_main_tile   = nb_main_tile;
      p->nb_border_sb   = nb_remainder_sb;
      p->nb_border_tile = !!(nb_remainder_sb);

      return true;
   }

   /* the number of tiles is not power of 2 */
   return false;
}

void radeon_enc_av1_tile_layout (uint32_t nb_sb, uint32_t nb_tiles, uint32_t min_nb_sb,
                                 struct tile_1d_layout *p)
{
   if (!min_nb_sb)
      min_nb_sb = 1;

   if (radeon_enc_is_av1_uniform_tile(nb_sb, nb_tiles, min_nb_sb, p))
         p->uniform_tile_flag = true;
   else {
      uint32_t nb_main_sb = nb_sb / nb_tiles;

      /* if some tile size is less than min_nb_sb need to re-divide tiles */
      if (nb_main_sb < min_nb_sb) {
         /* using maximum tile size (64), to recalc nb_tiles */
         nb_tiles = DIV_ROUND_UP(nb_sb, (RENCODE_AV1_MAX_TILE_WIDTH >> 6));
         nb_main_sb = nb_sb / nb_tiles;
         if (radeon_enc_is_av1_uniform_tile(nb_sb, nb_tiles, min_nb_sb, p)) {
            p->uniform_tile_flag = true;
            return;
         }
      }

      p->uniform_tile_flag = false;
      if (nb_tiles <= 1) {
         p->nb_main_sb     = nb_sb;
         p->nb_main_tile   = 1;
         p->nb_border_sb   = 0;
         p->nb_border_tile = 0;
      } else {
         uint32_t nb_remainder_sb = nb_sb % nb_tiles;

         if (nb_remainder_sb) {
            p->nb_main_sb = nb_main_sb + 1;
            p->nb_main_tile = nb_remainder_sb; /* in unit of tile */
            p->nb_border_sb = nb_main_sb;
            p->nb_border_tile = nb_tiles - nb_remainder_sb;
         } else {
            p->nb_main_sb = nb_main_sb;
            p->nb_main_tile = nb_tiles;
            p->nb_border_sb = 0;
            p->nb_border_tile = 0;
         }
      }
   }
}

/* num_tile_cols and num_tile_rows will be changed if not fit */
static void radeon_enc_av1_tile_default(struct radeon_encoder *enc,
                                        uint32_t *num_tile_cols,
                                        uint32_t *num_tile_rows)
{
   struct tile_1d_layout tile_layout;
   uint32_t i;
   bool uniform_col, uniform_row;
   rvcn_enc_av1_tile_config_t *p_config = &enc->enc_pic.av1_tile_config;
   uint32_t frame_width_in_sb =
      DIV_ROUND_UP(enc->enc_pic.pic_width_in_luma_samples, PIPE_AV1_ENC_SB_SIZE);
   uint32_t frame_height_in_sb =
      DIV_ROUND_UP(enc->enc_pic.pic_height_in_luma_samples, PIPE_AV1_ENC_SB_SIZE);
   uint32_t min_tile_width_in_sb = RENCODE_AV1_MIN_TILE_WIDTH >> 6;
   uint32_t max_tile_area_sb = RENCODE_AV1_MAX_TILE_AREA >> (2 * 6);
   uint32_t max_tile_width_in_sb = RENCODE_AV1_MAX_TILE_WIDTH >> 6;
   uint32_t widest_tiles_in_sb = 0;
   uint32_t max_tile_ares_in_sb = 0;
   uint32_t max_tile_height_in_sb = 0;
   uint32_t min_log2_tiles_width_in_sb =
                     radeon_enc_av1_tile_log2(max_tile_width_in_sb, frame_width_in_sb);
   uint32_t min_log2_tiles = MAX2(min_log2_tiles_width_in_sb,
                     radeon_enc_av1_tile_log2(max_tile_area_sb,
                                              frame_width_in_sb * frame_height_in_sb));

   radeon_enc_av1_tile_layout(frame_width_in_sb, *num_tile_cols,
                              min_tile_width_in_sb, &tile_layout);

   *num_tile_cols = tile_layout.nb_main_tile + tile_layout.nb_border_tile;
   uniform_col = tile_layout.uniform_tile_flag;

   for (i = 0; i < tile_layout.nb_main_tile; i++) {
      p_config->tile_widths[i] = tile_layout.nb_main_sb;
      widest_tiles_in_sb = MAX2(p_config->tile_widths[i], widest_tiles_in_sb);
   }

   for (i = 0; i < tile_layout.nb_border_tile; i++) {
      p_config->tile_widths[i + tile_layout.nb_main_tile] = tile_layout.nb_border_sb;
      widest_tiles_in_sb = MAX2(p_config->tile_widths[i], widest_tiles_in_sb);
   }

   if (min_log2_tiles)
      max_tile_ares_in_sb = (frame_width_in_sb * frame_height_in_sb)
                                             >> (min_log2_tiles + 1);
   else
      max_tile_ares_in_sb = frame_width_in_sb * frame_height_in_sb;

   max_tile_height_in_sb = DIV_ROUND_UP(max_tile_ares_in_sb, widest_tiles_in_sb);
   *num_tile_rows = MAX2(*num_tile_rows,
                         DIV_ROUND_UP(frame_height_in_sb, max_tile_height_in_sb));

   radeon_enc_av1_tile_layout(frame_height_in_sb, *num_tile_rows, 1, &tile_layout);
   *num_tile_rows = tile_layout.nb_main_tile + tile_layout.nb_border_tile;
   uniform_row = tile_layout.uniform_tile_flag;

   for (i = 0; i < tile_layout.nb_main_tile; i++)
      p_config->tile_height[i] = tile_layout.nb_main_sb;

   for (i = 0; i < tile_layout.nb_border_tile; i++)
      p_config->tile_height[i + tile_layout.nb_main_tile] = tile_layout.nb_border_sb;

   p_config->uniform_tile_spacing = !!(uniform_col && uniform_row);

   if (enc->enc_pic.is_obu_frame) {
      p_config->num_tile_groups = 1;
      p_config->tile_groups[0].start = 0;
      p_config->tile_groups[0].end = (*num_tile_rows) * (*num_tile_cols) - 1;
   } else {
      p_config->num_tile_groups = (*num_tile_rows) * (*num_tile_cols);
      for (int32_t i = 0; i < *num_tile_rows; i++)
         for (int32_t j = 0; j < *num_tile_cols; j++) {
            int32_t k = *num_tile_cols * i + j;
            p_config->tile_groups[k].start = k;
            p_config->tile_groups[k].end = k;
         }
   }
}

static void radeon_enc_tile_config_av1(struct radeon_encoder *enc)
{
   /* It needs to check if the input tile setting meet the current capability
    * or not, if not then regenerate the setting, if needed at the corresponding
    * variables for obu instruction to program headers easier and consistent
    * with this logic */

   rvcn_enc_av1_tile_config_t *p_config = &enc->enc_pic.av1_tile_config;
   uint32_t i = 0;
   uint32_t frame_width_in_sb =
      DIV_ROUND_UP(enc->enc_pic.pic_width_in_luma_samples, PIPE_AV1_ENC_SB_SIZE);
   uint32_t min_tile_width_in_sb = RENCODE_AV1_MIN_TILE_WIDTH >> 6;
   uint32_t max_tile_num_in_width = frame_width_in_sb / min_tile_width_in_sb;
   uint32_t max_tile_width_in_sb = RENCODE_AV1_MAX_TILE_WIDTH >> 6;
   uint32_t min_tile_num_in_width = DIV_ROUND_UP(frame_width_in_sb, max_tile_width_in_sb);
   uint32_t num_tile_cols, num_tile_rows;

   num_tile_cols = CLAMP(p_config->num_tile_cols,
                         MAX2(1, min_tile_num_in_width),
                         MIN2(RENCODE_AV1_TILE_CONFIG_MAX_NUM_COLS, max_tile_num_in_width));
   /* legacy way of splitting tiles, if width is less than or equal to 64 sbs, it cannot be
    * split */
   if (enc->enc_pic.av1_tile_splitting_legacy_flag)
      num_tile_cols = (frame_width_in_sb <= 64) ? 1 : num_tile_cols;

   num_tile_rows = CLAMP(p_config->num_tile_rows,
                         1, RENCODE_AV1_TILE_CONFIG_MAX_NUM_ROWS);

   p_config->apply_app_setting = false;

   /* if no adjust necessary then use user's setting */
   if (num_tile_rows == p_config->num_tile_rows &&
       num_tile_cols == p_config->num_tile_cols) {
      for (i = 0; i < num_tile_cols; i++) {
        if (p_config->tile_widths[i] <= min_tile_width_in_sb)
            break;
      }
      if (i == num_tile_cols)
         p_config->apply_app_setting = true;
   }
   p_config->tile_size_bytes_minus_1 = 3; /* fixed value */

   if (p_config->apply_app_setting && p_config->context_update_tile_id)
      p_config->context_update_tile_id_mode = RENCODE_AV1_CONTEXT_UPDATE_TILE_ID_MODE_CUSTOMIZED;
   else
      p_config->context_update_tile_id_mode = RENCODE_AV1_CONTEXT_UPDATE_TILE_ID_MODE_DEFAULT;

   if (!p_config->apply_app_setting) {
      radeon_enc_av1_tile_default(enc, &num_tile_cols, &num_tile_rows);

      /* re-layout tile */
      p_config->num_tile_cols = num_tile_cols;
      p_config->num_tile_rows = num_tile_rows;
   }

   RADEON_ENC_BEGIN(enc->cmd.tile_config_av1);
   RADEON_ENC_CS(p_config->num_tile_cols);
   RADEON_ENC_CS(p_config->num_tile_rows);
   for (i = 0; i < RENCODE_AV1_TILE_CONFIG_MAX_NUM_COLS; i++)
      RADEON_ENC_CS(p_config->tile_widths[i]);
   for (i = 0; i < RENCODE_AV1_TILE_CONFIG_MAX_NUM_ROWS; i++)
      RADEON_ENC_CS(p_config->tile_height[i]);
   p_config->num_tile_groups = MIN2(p_config->num_tile_groups,
                                    p_config->num_tile_cols * p_config->num_tile_rows);
   RADEON_ENC_CS(p_config->num_tile_groups);
   for (i = 0;
        i < RENCODE_AV1_TILE_CONFIG_MAX_NUM_COLS * RENCODE_AV1_TILE_CONFIG_MAX_NUM_ROWS;
        i++ ) {
      RADEON_ENC_CS(p_config->tile_groups[i].start);
      RADEON_ENC_CS(p_config->tile_groups[i].end);
   }
   RADEON_ENC_CS(p_config->context_update_tile_id_mode);
   RADEON_ENC_CS(p_config->context_update_tile_id);
   RADEON_ENC_CS(p_config->tile_size_bytes_minus_1);
   RADEON_ENC_END();
}

static void radeon_enc_av1_tile_info(struct radeon_encoder *enc, struct radeon_bitstream *bs)
{
   rvcn_enc_av1_tile_config_t *p_config = &enc->enc_pic.av1_tile_config;
   uint32_t i = 0;
   uint32_t sbCols = DIV_ROUND_UP(enc->enc_pic.pic_width_in_luma_samples, PIPE_AV1_ENC_SB_SIZE);
   uint32_t sbRows = DIV_ROUND_UP(enc->enc_pic.pic_height_in_luma_samples, PIPE_AV1_ENC_SB_SIZE);
   uint32_t maxTileWidthSb = RENCODE_AV1_MAX_TILE_WIDTH >> 6;
   uint32_t maxTileAreaSb = RENCODE_AV1_MAX_TILE_AREA >> (2 * 6);
   uint32_t minLog2TileCols = radeon_enc_av1_tile_log2(maxTileWidthSb, sbCols);
   uint32_t minLog2Tiles = MAX2(minLog2TileCols,
                                radeon_enc_av1_tile_log2(maxTileAreaSb, sbRows * sbCols));
   uint32_t TileColsLog2, TileRowsLog2;

   TileColsLog2 = util_logbase2_ceil(p_config->num_tile_cols);
   TileRowsLog2 = util_logbase2_ceil(p_config->num_tile_rows);

   radeon_bs_code_fixed_bits(bs, p_config->uniform_tile_spacing, 1);
   if (p_config->uniform_tile_spacing) {
      for ( i = minLog2TileCols; i < TileColsLog2; i++)
         radeon_bs_code_fixed_bits(bs, 1, 1);

      radeon_bs_code_fixed_bits(bs, 0, 1);

      for ( i = minLog2Tiles - TileColsLog2; i < TileRowsLog2; i++)
         radeon_bs_code_fixed_bits(bs, 1, 1);

      radeon_bs_code_fixed_bits(bs, 0, 1);
   } else {
      uint32_t widestTileSb = 0;
      uint32_t maxWidthInSb = 0;
      uint32_t maxHeightInSb = 0;
      uint32_t maxTileHeightSb = 0;
      uint32_t startSb = 0;

      for (i = 0; i < p_config->num_tile_cols; i++) {
         maxWidthInSb = MIN2(sbCols - startSb, maxTileWidthSb);
         radeon_bs_code_ns(bs, p_config->tile_widths[i] - 1, maxWidthInSb);
         startSb += p_config->tile_widths[i];
         widestTileSb = MAX2( p_config->tile_widths[i], widestTileSb);
      }

      if (minLog2Tiles > 0)
         maxTileAreaSb = (sbRows * sbCols) >> (minLog2Tiles + 1);
      else
         maxTileAreaSb = sbRows * sbCols;

      maxTileHeightSb = MAX2( maxTileAreaSb / widestTileSb, 1);
      startSb = 0;

      for (i = 0; i < p_config->num_tile_rows; i++) {
         maxHeightInSb = MIN2(sbRows - startSb, maxTileHeightSb);
         radeon_bs_code_ns(bs, p_config->tile_height[i] - 1, maxHeightInSb);
         startSb += p_config->tile_height[i];
      }
   }

   if (TileColsLog2 > 0 || TileRowsLog2 > 0) {
      radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_V5_AV1_BITSTREAM_INSTRUCTION_CONTEXT_UPDATE_TILE_ID, 0);

      radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_COPY, 0);

      radeon_bs_code_fixed_bits(bs, p_config->tile_size_bytes_minus_1, 2);
   }
}

static void radeon_enc_av1_write_delta_q(struct radeon_bitstream *bs, int32_t q)
{
   radeon_bs_code_fixed_bits(bs, !!(q), 1);

   if (q)
      radeon_bs_code_fixed_bits(bs, q, ( 1 + 6 ));
}

static void radeon_enc_av1_quantization_params(struct radeon_encoder *enc, struct radeon_bitstream *bs)
{
   rvcn_enc_av1_spec_misc_t *p = &enc->enc_pic.av1_spec_misc;

   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_V5_AV1_BITSTREAM_INSTRUCTION_BASE_Q_IDX, 0);

   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_COPY, 0);

   radeon_enc_av1_write_delta_q(bs, p->delta_q_y_dc);

   /* only support multi-planes at the time */
   if (p->separate_delta_q)
      radeon_bs_code_fixed_bits(bs, 1, 1);

   radeon_enc_av1_write_delta_q(bs, p->delta_q_u_dc);
   radeon_enc_av1_write_delta_q(bs, p->delta_q_u_ac);

   if (p->separate_delta_q) {
      radeon_enc_av1_write_delta_q(bs, p->delta_q_v_dc);
      radeon_enc_av1_write_delta_q(bs, p->delta_q_v_ac);
   }

   /* using qmatrix */
   radeon_bs_code_fixed_bits(bs, 0, 1);
}

static int32_t radeon_enc_av1_get_relative_dist(struct radeon_encoder *enc, uint32_t a, uint32_t b)
{
   uint32_t diff = a - b;
   uint32_t m = 1 << (enc->enc_pic.av1.desc->seq.order_hint_bits - 1);
   diff = (diff & (m - 1)) - (diff & m);
   return diff;
}

bool radeon_enc_av1_skip_mode_allowed(struct radeon_encoder *enc, uint32_t frames[2])
{
   if (enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_KEY ||
       enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_INTRA_ONLY ||
       !enc->enc_pic.av1.compound ||
       !enc->enc_pic.av1.desc->seq.seq_bits.enable_order_hint)
      return false;

   int32_t forward_idx = -1, backward_idx = -1;
   uint32_t forward_hint = 0, backward_hint = 0;

   for (uint32_t i = 0; i < RENCODE_AV1_REFS_PER_FRAME; i++) {
      uint32_t ref_hint = enc->enc_pic.av1.desc->dpb[enc->enc_pic.av1.desc->dpb_ref_frame_idx[i]].order_hint;
      int32_t dist = radeon_enc_av1_get_relative_dist(enc, ref_hint, enc->enc_pic.av1.desc->order_hint);
      if (dist < 0) {
         if (forward_idx < 0 || radeon_enc_av1_get_relative_dist(enc, ref_hint, forward_hint) > 0) {
            forward_idx = i;
            forward_hint = ref_hint;
         }
      } else if (dist > 0) {
         if (backward_idx < 0 || radeon_enc_av1_get_relative_dist(enc, ref_hint, backward_hint) < 0) {
            backward_idx = i;
            backward_hint = ref_hint;
         }
      }
   }

   if (forward_idx < 0)
      return false;

   if (backward_idx >= 0) {
      frames[0] = MIN2(forward_idx, backward_idx);
      frames[1] = MAX2(forward_idx, backward_idx);
      return true;
   }

   int32_t second_forward_idx = -1;
   uint32_t second_forward_hint;

   for (uint32_t i = 0; i < RENCODE_AV1_REFS_PER_FRAME; i++) {
      uint32_t ref_hint = enc->enc_pic.av1.desc->dpb[enc->enc_pic.av1.desc->dpb_ref_frame_idx[i]].order_hint;
      if (radeon_enc_av1_get_relative_dist(enc, ref_hint, forward_hint) < 0) {
         if (second_forward_idx < 0 || radeon_enc_av1_get_relative_dist(enc, ref_hint, second_forward_hint) > 0) {
            second_forward_idx = i;
            second_forward_hint = ref_hint;
         }
      }
   }

   if (second_forward_idx < 0)
      return false;

   frames[0] = MIN2(forward_idx, second_forward_idx);
   frames[1] = MAX2(forward_idx, second_forward_idx);
   return true;
}

static void radeon_enc_av1_frame_header(struct radeon_encoder *enc, struct radeon_bitstream *bs, bool frame_header)
{
   bool frame_is_intra = enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_KEY ||
                         enc->enc_pic.frame_type == PIPE_AV1_ENC_FRAME_TYPE_INTRA_ONLY;

   radeon_enc_av1_frame_header_common(enc, bs, frame_header);

   /*  tile_info  */
   radeon_enc_av1_tile_info(enc, bs);
   /*  quantization_params  */
   radeon_enc_av1_quantization_params(enc, bs);
   /*  segmentation_enable  */
   radeon_bs_code_fixed_bits(bs, 0, 1);
   /*  delta_q_params  */
   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_DELTA_Q_PARAMS, 0);
   /*  delta_lf_params  */
   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_DELTA_LF_PARAMS, 0);
   /*  loop_filter_params  */
   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_LOOP_FILTER_PARAMS, 0);
   /*  cdef_params  */
   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_CDEF_PARAMS, 0);
   /*  lr_params  */
   /*  read_tx_mode  */
   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_READ_TX_MODE, 0);

   radeon_enc_av1_bs_instruction_type(enc, bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_COPY, 0);

   if (!frame_is_intra)
      /*  reference_select  */
      radeon_bs_code_fixed_bits(bs, enc->enc_pic.av1.compound, 1);

   if (enc->enc_pic.av1.skip_mode_allowed)
      /*  skip_mode_present  */
      radeon_bs_code_fixed_bits(bs, !enc->enc_pic.av1_spec_misc.disallow_skip_mode, 1);

   /*  reduced_tx_set  */
   radeon_bs_code_fixed_bits(bs, 0, 1);

   if (!frame_is_intra)
      for (uint32_t ref = 1 /*LAST_FRAME*/; ref <= 7 /*ALTREF_FRAME*/; ref++)
         /*  is_global  */
         radeon_bs_code_fixed_bits(bs, 0, 1);
   /*  film_grain_params() */
}

static void radeon_enc_obu_instruction(struct radeon_encoder *enc)
{
   bool frame_header = !enc->enc_pic.is_obu_frame;
   struct radeon_bitstream bs;

   radeon_bs_reset(&bs, NULL, &enc->cs);

   RADEON_ENC_BEGIN(enc->cmd.bitstream_instruction_av1);

   radeon_enc_av1_bs_instruction_type(enc, &bs,
         RENCODE_AV1_BITSTREAM_INSTRUCTION_OBU_START,
            frame_header ? RENCODE_OBU_START_TYPE_FRAME_HEADER
                         : RENCODE_OBU_START_TYPE_FRAME);

   radeon_enc_av1_frame_header(enc, &bs, frame_header);

   if (!frame_header)
      radeon_enc_av1_bs_instruction_type(enc, &bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_TILE_GROUP_OBU, 0);

   radeon_enc_av1_bs_instruction_type(enc, &bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_OBU_END, 0);

   if (frame_header)
      radeon_enc_av1_tile_group(enc, &bs);

   radeon_enc_av1_bs_instruction_type(enc, &bs, RENCODE_AV1_BITSTREAM_INSTRUCTION_END, 0);
   RADEON_ENC_END();
}

static void radeon_enc_session_init(struct radeon_encoder *enc)
{
   enc->enc_pic.session_init.slice_output_enabled = 0;
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
   RADEON_ENC_CS(enc->enc_pic.session_init.slice_output_enabled);
   RADEON_ENC_CS(enc->enc_pic.session_init.display_remote);
   RADEON_ENC_END();
}

void radeon_enc_5_0_init(struct radeon_encoder *enc)
{
   radeon_enc_4_0_init(enc);

   enc->session_init = radeon_enc_session_init;
   enc->output_format = radeon_enc_output_format;
   enc->metadata = radeon_enc_metadata;
   enc->encode_params = radeon_enc_encode_params;
   enc->rc_per_pic = radeon_enc_rc_per_pic;

   if (enc->dpb_type == DPB_LEGACY) {
      enc->ctx = radeon_enc_ctx;
      enc->ctx_override = radeon_enc_ctx_override;
   } else if (enc->dpb_type == DPB_TIER_2) {
      enc->ctx = radeon_enc_ctx_tier2;
      enc->ctx_override = radeon_enc_dummy;
   }

   if (u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC) {
      enc->spec_misc = radeon_enc_spec_misc;
      enc->encode_params_codec_spec = radeon_enc_encode_params_h264;
   } else if (u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_HEVC) {
      enc->encode_params_codec_spec = radeon_enc_encode_params_hevc;
      enc->spec_misc = radeon_enc_spec_misc_hevc;
   } else if (u_reduce_video_profile(enc->base.profile) == PIPE_VIDEO_FORMAT_AV1) {
      enc->cdf_default_table = radeon_enc_cdf_default_table;
      enc->spec_misc = radeon_enc_spec_misc_av1;
      enc->tile_config = radeon_enc_tile_config_av1;
      enc->obu_instructions = radeon_enc_obu_instruction;
      enc->encode_params_codec_spec = radeon_enc_encode_params_av1;
   }

   enc->enc_pic.session_info.interface_version =
      ((RENCODE_FW_INTERFACE_MAJOR_VERSION << RENCODE_IF_MAJOR_VERSION_SHIFT) |
      (RENCODE_FW_INTERFACE_MINOR_VERSION << RENCODE_IF_MINOR_VERSION_SHIFT));
}
