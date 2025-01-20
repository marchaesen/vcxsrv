/**************************************************************************
 *
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#include "pipe/p_video_codec.h"
#include "radeon_vce.h"
#include "radeon_video.h"
#include "radeon_bitstream.h"
#include "radeonsi/si_pipe.h"
#include "util/u_memory.h"
#include "util/u_video.h"
#include "vl/vl_video_buffer.h"

#include <stdio.h>

#define REF_LIST_MODIFICATION_OP_END                  0
#define REF_LIST_MODIFICATION_OP_SHORT_TERM_SUBTRACT  1
#define REF_LIST_MODIFICATION_OP_LONG_TERM            2
#define REF_LIST_MODIFICATION_OP_VIEW_ADD             3

#define INTRAREFRESH_METHOD_BAR_BASED                 6

static void get_rate_control_param(struct rvce_encoder *enc, struct pipe_h264_enc_picture_desc *pic)
{
   enc->enc_pic.rc.rc_method = pic->rate_ctrl[0].rate_ctrl_method;
   enc->enc_pic.rc.target_bitrate = pic->rate_ctrl[0].target_bitrate;
   enc->enc_pic.rc.peak_bitrate = pic->rate_ctrl[0].peak_bitrate;
   enc->enc_pic.rc.quant_i_frames = pic->quant_i_frames;
   enc->enc_pic.rc.quant_p_frames = pic->quant_p_frames;
   enc->enc_pic.rc.quant_b_frames = pic->quant_b_frames;
   enc->enc_pic.rc.gop_size = pic->gop_size;
   enc->enc_pic.rc.frame_rate_num = pic->rate_ctrl[0].frame_rate_num;
   enc->enc_pic.rc.frame_rate_den = pic->rate_ctrl[0].frame_rate_den;
   enc->enc_pic.rc.min_qp = pic->rate_ctrl[0].min_qp;
   enc->enc_pic.rc.max_qp = pic->rate_ctrl[0].max_qp ? pic->rate_ctrl[0].max_qp : 51;
   enc->enc_pic.rc.max_au_size = pic->rate_ctrl[0].max_au_size;
   enc->enc_pic.rc.vbv_buffer_size = pic->rate_ctrl[0].vbv_buffer_size;
   enc->enc_pic.rc.vbv_buf_lv = pic->rate_ctrl[0].vbv_buf_lv;
   enc->enc_pic.rc.fill_data_enable = pic->rate_ctrl[0].fill_data_enable;
   enc->enc_pic.rc.enforce_hrd = pic->rate_ctrl[0].enforce_hrd;
   enc->enc_pic.rc.target_bits_picture =
      enc->pic.rate_ctrl[0].target_bitrate *
      ((float)enc->pic.rate_ctrl[0].frame_rate_den /
      enc->pic.rate_ctrl[0].frame_rate_num);
   enc->enc_pic.rc.peak_bits_picture_integer =
      enc->pic.rate_ctrl[0].peak_bitrate *
      ((float)enc->pic.rate_ctrl[0].frame_rate_den /
      enc->pic.rate_ctrl[0].frame_rate_num);
   enc->enc_pic.rc.peak_bits_picture_fraction =
      (((enc->pic.rate_ctrl[0].peak_bitrate *
      (uint64_t)enc->pic.rate_ctrl[0].frame_rate_den) %
      enc->pic.rate_ctrl[0].frame_rate_num) << 32) /
      enc->pic.rate_ctrl[0].frame_rate_num;
}

static void get_motion_estimation_param(struct rvce_encoder *enc,
                                        struct pipe_h264_enc_picture_desc *pic)
{
   enc->enc_pic.me.enc_ime_decimation_search = 1;
   enc->enc_pic.me.motion_est_half_pixel = 1;
   enc->enc_pic.me.motion_est_quarter_pixel = 1;
   enc->enc_pic.me.disable_favor_pmv_point = 0;
   enc->enc_pic.me.lsmvert = 2;
   enc->enc_pic.me.disable_16x16_frame1 = 0;
   enc->enc_pic.me.disable_satd = 0;
   enc->enc_pic.me.enc_ime_skip_x = 0;
   enc->enc_pic.me.enc_ime_skip_y = 0;
   enc->enc_pic.me.enc_ime2_search_range_x = 4;
   enc->enc_pic.me.enc_ime2_search_range_y = 4;
   enc->enc_pic.me.parallel_mode_speedup_enable = 0;
   enc->enc_pic.me.fme0_enc_disable_sub_mode = 0;
   enc->enc_pic.me.fme1_enc_disable_sub_mode = 0;
   enc->enc_pic.me.ime_sw_speedup_enable = 0;

   switch (pic->quality_modes.preset_mode) {
   case 0: /* SPEED */
      enc->enc_pic.me.force_zero_point_center = 0;
      enc->enc_pic.me.enc_search_range_x = 16;
      enc->enc_pic.me.enc_search_range_y = 16;
      enc->enc_pic.me.enc_search1_range_x = 16;
      enc->enc_pic.me.enc_search1_range_y = 16;
      enc->enc_pic.me.enable_amd = 0;
      enc->enc_pic.me.enc_disable_sub_mode = 126;
      enc->enc_pic.me.enc_en_ime_overw_dis_subm = 0;
      enc->enc_pic.me.enc_ime_overw_dis_subm_no = 0;
      break;
   case 1: /* BALANCED */
      enc->enc_pic.me.force_zero_point_center = 0;
      enc->enc_pic.me.enc_search_range_x = 16;
      enc->enc_pic.me.enc_search_range_y = 16;
      enc->enc_pic.me.enc_search1_range_x = 16;
      enc->enc_pic.me.enc_search1_range_y = 16;
      enc->enc_pic.me.enable_amd = 0;
      enc->enc_pic.me.enc_disable_sub_mode = 120;
      enc->enc_pic.me.enc_en_ime_overw_dis_subm = 1;
      enc->enc_pic.me.enc_ime_overw_dis_subm_no = 1;
      break;
   case 2: /* QUALITY */
   default:
      enc->enc_pic.me.force_zero_point_center = 1;
      enc->enc_pic.me.enc_search_range_x = 36;
      enc->enc_pic.me.enc_search_range_y = 36;
      enc->enc_pic.me.enc_search1_range_x = 36;
      enc->enc_pic.me.enc_search1_range_y = 36;
      enc->enc_pic.me.enable_amd = 1;
      enc->enc_pic.me.enc_disable_sub_mode = 0;
      enc->enc_pic.me.enc_en_ime_overw_dis_subm = 0;
      enc->enc_pic.me.enc_ime_overw_dis_subm_no = 0;
      break;
   }
}

static void get_pic_control_param(struct rvce_encoder *enc, struct pipe_h264_enc_picture_desc *pic)
{
   uint32_t num_mbs_total, num_mbs_in_slice;

   num_mbs_total = DIV_ROUND_UP(enc->base.width, 16) * DIV_ROUND_UP(enc->base.height, 16);

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

   if (pic->seq.enc_frame_cropping_flag) {
      enc->enc_pic.pc.enc_crop_left_offset = pic->seq.enc_frame_crop_left_offset;
      enc->enc_pic.pc.enc_crop_right_offset = pic->seq.enc_frame_crop_right_offset;
      enc->enc_pic.pc.enc_crop_top_offset = pic->seq.enc_frame_crop_top_offset;
      enc->enc_pic.pc.enc_crop_bottom_offset = pic->seq.enc_frame_crop_bottom_offset;
   }
   enc->enc_pic.pc.enc_num_mbs_per_slice = num_mbs_in_slice;
   enc->enc_pic.pc.enc_number_of_reference_frames = 1;
   enc->enc_pic.pc.enc_max_num_ref_frames = pic->seq.max_num_ref_frames;
   enc->enc_pic.pc.enc_num_default_active_ref_l0 = pic->pic_ctrl.num_ref_idx_l0_default_active_minus1 + 1;
   enc->enc_pic.pc.enc_num_default_active_ref_l1 = pic->pic_ctrl.num_ref_idx_l1_default_active_minus1 + 1;
   enc->enc_pic.pc.enc_slice_mode = 1;
   enc->enc_pic.pc.enc_use_constrained_intra_pred = pic->pic_ctrl.constrained_intra_pred_flag;
   enc->enc_pic.pc.enc_cabac_enable = pic->pic_ctrl.enc_cabac_enable;
   enc->enc_pic.pc.enc_cabac_idc = pic->pic_ctrl.enc_cabac_init_idc;
   enc->enc_pic.pc.enc_constraint_set_flags = pic->seq.enc_constraint_set_flags << 2;
   enc->enc_pic.pc.enc_loop_filter_disable = !!pic->dbk.disable_deblocking_filter_idc;
   enc->enc_pic.pc.enc_lf_beta_offset = pic->dbk.beta_offset_div2;
   enc->enc_pic.pc.enc_lf_alpha_c0_offset = pic->dbk.alpha_c0_offset_div2;
   enc->enc_pic.pc.enc_pic_order_cnt_type = pic->seq.pic_order_cnt_type;
   enc->enc_pic.pc.log2_max_pic_order_cnt_lsb_minus4 = pic->seq.log2_max_pic_order_cnt_lsb_minus4;
}

static void get_task_info_param(struct rvce_encoder *enc)
{
   enc->enc_pic.ti.offset_of_next_task_info = 0xffffffff;
}

static void get_feedback_buffer_param(struct rvce_encoder *enc, struct pipe_enc_feedback_metadata* metadata)
{
   enc->enc_pic.fb.feedback_ring_size = 0x00000001;
}

static void get_config_ext_param(struct rvce_encoder *enc)
{
   enc->enc_pic.ce.enc_enable_perf_logging = 0x00000003;
}

static void get_param(struct rvce_encoder *enc, struct pipe_h264_enc_picture_desc *pic)
{
   int i;

   get_rate_control_param(enc, pic);
   get_motion_estimation_param(enc, pic);
   get_pic_control_param(enc, pic);
   get_task_info_param(enc);
   get_feedback_buffer_param(enc, NULL);
   get_config_ext_param(enc);

   enc->enc_pic.picture_type = pic->picture_type;
   enc->enc_pic.frame_num = pic->frame_num;
   enc->enc_pic.frame_num_cnt = pic->frame_num_cnt - 1;
   enc->enc_pic.p_remain = pic->p_remain;
   enc->enc_pic.i_remain = pic->i_remain;
   enc->enc_pic.pic_order_cnt = pic->pic_order_cnt;
   enc->enc_pic.not_referenced = pic->not_referenced;
   enc->enc_pic.addrmode_arraymode_disrdo_distwoinstants = 0x01000201;
   enc->enc_pic.is_idr = (pic->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR);
   enc->enc_pic.eo.enc_idr_pic_id = pic->idr_pic_id;
   enc->enc_pic.ec.enc_vbaq_mode =
      pic->rate_ctrl[0].rate_ctrl_method != PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE &&
      pic->quality_modes.vbaq_mode;
   if (pic->intra_refresh.mode != PIPE_VIDEO_ENC_INTRA_REFRESH_NONE) {
      enc->enc_pic.eo.enable_intra_refresh = 1;
      enc->enc_pic.pc.enc_force_intra_refresh = INTRAREFRESH_METHOD_BAR_BASED;
      enc->enc_pic.pc.enc_intra_refresh_num_mbs_per_slot = pic->intra_refresh.region_size;
   } else {
      enc->enc_pic.eo.enable_intra_refresh = 0;
   }

   enc->enc_pic.eo.num_ref_idx_active_override_flag = pic->slice.num_ref_idx_active_override_flag;
   enc->enc_pic.eo.num_ref_idx_l0_active_minus1 = pic->slice.num_ref_idx_l0_active_minus1;
   enc->enc_pic.eo.num_ref_idx_l1_active_minus1 = pic->slice.num_ref_idx_l1_active_minus1;

   i = 0;
   if (pic->slice.ref_pic_list_modification_flag_l0) {
      for (; i < MIN2(4, pic->slice.num_ref_list0_mod_operations); i++) {
         struct pipe_h264_ref_list_mod_entry *entry = &pic->slice.ref_list0_mod_operations[i];
         switch (entry->modification_of_pic_nums_idc) {
         case 0:
            enc->enc_pic.eo.enc_ref_list_modification_op[i] = REF_LIST_MODIFICATION_OP_SHORT_TERM_SUBTRACT;
            enc->enc_pic.eo.enc_ref_list_modification_num[i] = entry->abs_diff_pic_num_minus1;
            break;
         case 2:
            enc->enc_pic.eo.enc_ref_list_modification_op[i] = REF_LIST_MODIFICATION_OP_LONG_TERM;
            enc->enc_pic.eo.enc_ref_list_modification_num[i] = entry->long_term_pic_num;
            break;
         case 5:
            enc->enc_pic.eo.enc_ref_list_modification_op[i] = REF_LIST_MODIFICATION_OP_VIEW_ADD;
            enc->enc_pic.eo.enc_ref_list_modification_num[i] = entry->abs_diff_pic_num_minus1;
            break;
         default:
         case 3:
            enc->enc_pic.eo.enc_ref_list_modification_op[i] = REF_LIST_MODIFICATION_OP_END;
            break;
         }
      }
   }
   if (i < 4)
      enc->enc_pic.eo.enc_ref_list_modification_op[i] = REF_LIST_MODIFICATION_OP_END;

   i = 0;
   if (pic->pic_ctrl.nal_unit_type == PIPE_H264_NAL_IDR_SLICE) {
      enc->enc_pic.eo.enc_decoded_picture_marking_op[i++] = pic->slice.long_term_reference_flag ? 6 : 0;
   } else if (pic->slice.adaptive_ref_pic_marking_mode_flag) {
      for (; i < MIN2(4, pic->slice.num_ref_pic_marking_operations); i++) {
         struct pipe_h264_ref_pic_marking_entry *entry = &pic->slice.ref_pic_marking_operations[i];
         enc->enc_pic.eo.enc_decoded_picture_marking_op[i] = entry->memory_management_control_operation;
         switch (entry->memory_management_control_operation) {
         case 1:
            enc->enc_pic.eo.enc_decoded_picture_marking_num[i] = entry->difference_of_pic_nums_minus1;
            break;
         case 2:
            enc->enc_pic.eo.enc_decoded_picture_marking_num[i] = entry->long_term_pic_num;
            break;
         case 3:
            enc->enc_pic.eo.enc_decoded_picture_marking_num[i] = entry->difference_of_pic_nums_minus1;
            enc->enc_pic.eo.enc_decoded_picture_marking_idx[i] = entry->long_term_frame_idx;
            break;
         case 4:
            enc->enc_pic.eo.enc_decoded_picture_marking_idx[i] = entry->max_long_term_frame_idx_plus1;
            break;
         case 6:
            enc->enc_pic.eo.enc_decoded_picture_marking_idx[i] = entry->long_term_frame_idx;
            break;
         default:
            break;
         }
      }
   }
   if (i < 4)
      enc->enc_pic.eo.enc_decoded_picture_marking_op[i] = 0;

   enc->enc_pic.eo.cur_dpb_idx = pic->dpb_curr_pic;

   enc->enc_pic.eo.l0_dpb_idx = pic->ref_list0[0];

   enc->enc_pic.eo.l1_dpb_idx = PIPE_H2645_LIST_REF_INVALID_ENTRY;
   enc->enc_pic.eo.l1_luma_offset = 0xffffffff;
   enc->enc_pic.eo.l1_chroma_offset = 0xffffffff;
}

static void create(struct rvce_encoder *enc)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;
   enc->task_info(enc, 0x00000000, 0);

   RVCE_BEGIN(0x01000001); // create cmd
   RVCE_CS(enc->enc_pic.ec.enc_use_circular_buffer);
   RVCE_CS(enc->pic.seq.profile_idc); // encProfile
   RVCE_CS(enc->pic.seq.level_idc);                    // encLevel
   RVCE_CS(enc->enc_pic.ec.enc_pic_struct_restriction);
   RVCE_CS(align(enc->base.width, 16));  // encImageWidth
   RVCE_CS(align(enc->base.height, 16)); // encImageHeight

   if (sscreen->info.gfx_level < GFX9) {
      RVCE_CS(enc->luma->u.legacy.level[0].nblk_x * enc->luma->bpe);     // encRefPicLumaPitch
      RVCE_CS(enc->chroma->u.legacy.level[0].nblk_x * enc->chroma->bpe); // encRefPicChromaPitch
      RVCE_CS(align(enc->luma->u.legacy.level[0].nblk_y, 16) / 8);       // encRefYHeightInQw
   } else {
      RVCE_CS(enc->luma->u.gfx9.surf_pitch * enc->luma->bpe);     // encRefPicLumaPitch
      RVCE_CS(enc->chroma->u.gfx9.surf_pitch * enc->chroma->bpe); // encRefPicChromaPitch
      RVCE_CS(align(enc->luma->u.gfx9.surf_height, 16) / 8);      // encRefYHeightInQw
   }

   RVCE_CS(enc->enc_pic.addrmode_arraymode_disrdo_distwoinstants);

   RVCE_CS(enc->enc_pic.ec.enc_pre_encode_context_buffer_offset);
   RVCE_CS(enc->enc_pic.ec.enc_pre_encode_input_luma_buffer_offset);
   RVCE_CS(enc->enc_pic.ec.enc_pre_encode_input_chroma_buffer_offset);
   RVCE_CS(enc->enc_pic.ec.enc_pre_encode_mode_chromaflag_vbaqmode_scenechangesensitivity);
   RVCE_END();
}

static void encode(struct rvce_encoder *enc)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;
   signed luma_offset, chroma_offset;
   int i;

   enc->task_info(enc, 0x00000003, 0);

   RVCE_BEGIN(0x05000001);                                      // context buffer
   RVCE_READWRITE(enc->dpb.res->buf, enc->dpb.res->domains, 0); // encodeContextAddressHi/Lo
   RVCE_END();

   RVCE_BEGIN(0x05000004);                                   // video bitstream buffer
   RVCE_WRITE(enc->bs_handle, RADEON_DOMAIN_GTT, enc->bs_offset); // videoBitstreamRingAddressHi/Lo
   RVCE_CS(enc->bs_size);                                    // videoBitstreamRingSize
   RVCE_END();

   if (enc->dual_pipe) {
      unsigned aux_offset = 0;
      RVCE_BEGIN(0x05000002); // auxiliary buffer
      for (i = 0; i < 8; ++i) {
         RVCE_CS(aux_offset);
         aux_offset += RVCE_MAX_BITSTREAM_OUTPUT_ROW_SIZE;
      }
      for (i = 0; i < 8; ++i)
         RVCE_CS(RVCE_MAX_BITSTREAM_OUTPUT_ROW_SIZE);
      RVCE_END();
   }

   RVCE_BEGIN(0x03000001);                       // encode
   RVCE_CS(enc->enc_pic.eo.insert_headers);
   RVCE_CS(enc->enc_pic.eo.picture_structure);
   RVCE_CS(enc->bs_size - enc->bs_offset); // allowedMaxBitstreamSize
   RVCE_CS(enc->enc_pic.eo.force_refresh_map);
   RVCE_CS(enc->enc_pic.eo.insert_aud);
   RVCE_CS(enc->enc_pic.eo.end_of_sequence);
   RVCE_CS(enc->enc_pic.eo.end_of_stream);

   if (sscreen->info.gfx_level < GFX9) {
      RVCE_READ(enc->handle, RADEON_DOMAIN_VRAM,
                (uint64_t)enc->luma->u.legacy.level[0].offset_256B * 256); // inputPictureLumaAddressHi/Lo
      RVCE_READ(enc->handle, RADEON_DOMAIN_VRAM,
                (uint64_t)enc->chroma->u.legacy.level[0].offset_256B * 256);        // inputPictureChromaAddressHi/Lo
      RVCE_CS(align(enc->luma->u.legacy.level[0].nblk_y, 16)); // encInputFrameYPitch
      RVCE_CS(enc->luma->u.legacy.level[0].nblk_x * enc->luma->bpe);     // encInputPicLumaPitch
      RVCE_CS(enc->chroma->u.legacy.level[0].nblk_x * enc->chroma->bpe); // encInputPicChromaPitch
   } else {
      RVCE_READ(enc->handle, RADEON_DOMAIN_VRAM,
                enc->luma->u.gfx9.surf_offset); // inputPictureLumaAddressHi/Lo
      RVCE_READ(enc->handle, RADEON_DOMAIN_VRAM,
                enc->chroma->u.gfx9.surf_offset);                 // inputPictureChromaAddressHi/Lo
      RVCE_CS(align(enc->luma->u.gfx9.surf_height, 16));          // encInputFrameYPitch
      RVCE_CS(enc->luma->u.gfx9.surf_pitch * enc->luma->bpe);     // encInputPicLumaPitch
      RVCE_CS(enc->chroma->u.gfx9.surf_pitch * enc->chroma->bpe); // encInputPicChromaPitch
      enc->enc_pic.eo.enc_input_pic_swizzle_mode = enc->luma->u.gfx9.swizzle_mode;
   }

   enc->enc_pic.eo.enc_disable_two_pipe_mode = !enc->dual_pipe;
   RVCE_CS(enc->enc_pic.eo.enc_input_pic_addr_array_disable2pipe_disablemboffload);
   RVCE_CS(enc->enc_pic.eo.enc_input_pic_tile_config);
   RVCE_CS(enc->enc_pic.picture_type);                                    // encPicType
   RVCE_CS(enc->enc_pic.picture_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR); // encIdrFlag
   RVCE_CS(enc->enc_pic.eo.enc_idr_pic_id);
   RVCE_CS(enc->enc_pic.eo.enc_mgs_key_pic);
   RVCE_CS(!enc->enc_pic.not_referenced);
   RVCE_CS(enc->enc_pic.eo.enc_temporal_layer_index);
   RVCE_CS(enc->enc_pic.eo.num_ref_idx_active_override_flag);
   RVCE_CS(enc->enc_pic.eo.num_ref_idx_l0_active_minus1);
   RVCE_CS(enc->enc_pic.eo.num_ref_idx_l1_active_minus1);

   for (i = 0; i < 4; ++i) {
      RVCE_CS(enc->enc_pic.eo.enc_ref_list_modification_op[i]);
      RVCE_CS(enc->enc_pic.eo.enc_ref_list_modification_num[i]);
   }

   for (i = 0; i < 4; ++i) {
      RVCE_CS(enc->enc_pic.eo.enc_decoded_picture_marking_op[i]);
      RVCE_CS(enc->enc_pic.eo.enc_decoded_picture_marking_num[i]);
      RVCE_CS(enc->enc_pic.eo.enc_decoded_picture_marking_idx[i]);
   }

   for (i = 0; i < 4; ++i) {
      RVCE_CS(enc->enc_pic.eo.enc_decoded_ref_base_picture_marking_op[i]);
      RVCE_CS(enc->enc_pic.eo.enc_decoded_ref_base_picture_marking_num[i]);
   }

   // encReferencePictureL0[0]
   if (enc->enc_pic.eo.l0_dpb_idx != PIPE_H2645_LIST_REF_INVALID_ENTRY) {
      si_vce_frame_offset(enc, enc->enc_pic.eo.l0_dpb_idx, &luma_offset, &chroma_offset);
      enc->enc_pic.eo.l0_luma_offset = luma_offset;
      enc->enc_pic.eo.l0_chroma_offset = chroma_offset;
   } else {
      enc->enc_pic.eo.l0_luma_offset = 0xffffffff;
      enc->enc_pic.eo.l0_chroma_offset = 0xffffffff;
   }
   RVCE_CS(0x00000000); // pictureStructure
   RVCE_CS(enc->enc_pic.eo.l0_enc_pic_type);
   RVCE_CS(enc->enc_pic.eo.l0_frame_number);
   RVCE_CS(enc->enc_pic.eo.l0_picture_order_count);
   RVCE_CS(enc->enc_pic.eo.l0_luma_offset);
   RVCE_CS(enc->enc_pic.eo.l0_chroma_offset);

   // encReferencePictureL0[1]
   enc->enc_pic.eo.l0_picture_structure = 0x00000000;
   enc->enc_pic.eo.l0_enc_pic_type = 0x00000000;
   enc->enc_pic.eo.l0_frame_number = 0x00000000;
   enc->enc_pic.eo.l0_picture_order_count = 0x00000000;
   enc->enc_pic.eo.l0_luma_offset = 0xffffffff;
   enc->enc_pic.eo.l0_chroma_offset = 0xffffffff;
   RVCE_CS(enc->enc_pic.eo.l0_picture_structure);
   RVCE_CS(enc->enc_pic.eo.l0_enc_pic_type);
   RVCE_CS(enc->enc_pic.eo.l0_frame_number);
   RVCE_CS(enc->enc_pic.eo.l0_picture_order_count);
   RVCE_CS(enc->enc_pic.eo.l0_luma_offset);
   RVCE_CS(enc->enc_pic.eo.l0_chroma_offset);

   // encReferencePictureL1[0]
   RVCE_CS(0x00000000); // pictureStructure
   RVCE_CS(enc->enc_pic.eo.l1_enc_pic_type);
   RVCE_CS(enc->enc_pic.eo.l1_frame_number);
   RVCE_CS(enc->enc_pic.eo.l1_picture_order_count);
   RVCE_CS(enc->enc_pic.eo.l1_luma_offset);
   RVCE_CS(enc->enc_pic.eo.l1_chroma_offset);

   si_vce_frame_offset(enc, enc->enc_pic.eo.cur_dpb_idx, &luma_offset, &chroma_offset);
   RVCE_CS(luma_offset);
   RVCE_CS(chroma_offset);
   RVCE_CS(enc->enc_pic.eo.enc_coloc_buffer_offset);
   RVCE_CS(enc->enc_pic.eo.enc_reconstructed_ref_base_picture_luma_offset);
   RVCE_CS(enc->enc_pic.eo.enc_reconstructed_ref_base_picture_chroma_offset);
   RVCE_CS(enc->enc_pic.eo.enc_reference_ref_base_picture_luma_offset);
   RVCE_CS(enc->enc_pic.eo.enc_reference_ref_base_picture_chroma_offset);
   RVCE_CS(enc->enc_pic.frame_num_cnt);
   RVCE_CS(enc->enc_pic.frame_num);
   RVCE_CS(enc->enc_pic.pic_order_cnt);
   RVCE_CS(enc->enc_pic.i_remain);
   RVCE_CS(enc->enc_pic.p_remain);
   RVCE_CS(enc->enc_pic.eo.num_b_pic_remain_in_rcgop);
   RVCE_CS(enc->enc_pic.eo.num_ir_pic_remain_in_rcgop);
   RVCE_CS(enc->enc_pic.eo.enable_intra_refresh);

   RVCE_CS(enc->enc_pic.eo.aq_variance_en);
   RVCE_CS(enc->enc_pic.eo.aq_block_size);
   RVCE_CS(enc->enc_pic.eo.aq_mb_variance_sel);
   RVCE_CS(enc->enc_pic.eo.aq_frame_variance_sel);
   RVCE_CS(enc->enc_pic.eo.aq_param_a);
   RVCE_CS(enc->enc_pic.eo.aq_param_b);
   RVCE_CS(enc->enc_pic.eo.aq_param_c);
   RVCE_CS(enc->enc_pic.eo.aq_param_d);
   RVCE_CS(enc->enc_pic.eo.aq_param_e);

   RVCE_CS(enc->enc_pic.eo.context_in_sfb);
   RVCE_END();
}

static void rate_control(struct rvce_encoder *enc)
{
   RVCE_BEGIN(0x04000005); // rate control
   RVCE_CS(enc->enc_pic.rc.rc_method);
   RVCE_CS(enc->enc_pic.rc.target_bitrate);
   RVCE_CS(enc->enc_pic.rc.peak_bitrate);
   RVCE_CS(enc->enc_pic.rc.frame_rate_num);
   RVCE_CS(enc->enc_pic.rc.gop_size);
   RVCE_CS(enc->enc_pic.rc.quant_i_frames);
   RVCE_CS(enc->enc_pic.rc.quant_p_frames);
   RVCE_CS(enc->enc_pic.rc.quant_b_frames);
   RVCE_CS(enc->enc_pic.rc.vbv_buffer_size);
   RVCE_CS(enc->enc_pic.rc.frame_rate_den);
   RVCE_CS(enc->enc_pic.rc.vbv_buf_lv);
   RVCE_CS(enc->enc_pic.rc.max_au_size);
   RVCE_CS(enc->enc_pic.rc.qp_initial_mode);
   RVCE_CS(enc->enc_pic.rc.target_bits_picture);
   RVCE_CS(enc->enc_pic.rc.peak_bits_picture_integer);
   RVCE_CS(enc->enc_pic.rc.peak_bits_picture_fraction);
   RVCE_CS(enc->enc_pic.rc.min_qp);
   RVCE_CS(enc->enc_pic.rc.max_qp);
   RVCE_CS(enc->enc_pic.rc.skip_frame_enable);
   RVCE_CS(enc->enc_pic.rc.fill_data_enable);
   RVCE_CS(enc->enc_pic.rc.enforce_hrd);
   RVCE_CS(enc->enc_pic.rc.b_pics_delta_qp);
   RVCE_CS(enc->enc_pic.rc.ref_b_pics_delta_qp);
   RVCE_CS(enc->enc_pic.rc.rc_reinit_disable);
   RVCE_CS(enc->enc_pic.rc.enc_lcvbr_init_qp_flag);
   RVCE_CS(enc->enc_pic.rc.lcvbrsatd_based_nonlinear_bit_budget_flag);
   RVCE_END();
}

static void config(struct rvce_encoder *enc)
{
   enc->task_info(enc, 0x00000002, 0xffffffff);
   enc->rate_control(enc);
   enc->config_extension(enc);
   enc->motion_estimation(enc);
   enc->rdo(enc);
   enc->pic_control(enc);
}

static void config_extension(struct rvce_encoder *enc)
{
   RVCE_BEGIN(0x04000001); // config extension
   RVCE_CS(enc->enc_pic.ce.enc_enable_perf_logging);
   RVCE_END();
}

static void feedback(struct rvce_encoder *enc)
{
   RVCE_BEGIN(0x05000005);                                    // feedback buffer
   RVCE_WRITE(enc->fb->res->buf, enc->fb->res->domains, 0x0); // feedbackRingAddressHi/Lo
   RVCE_CS(enc->enc_pic.fb.feedback_ring_size);
   RVCE_END();
}

static void destroy(struct rvce_encoder *enc)
{
   enc->task_info(enc, 0x00000001, 0);

   feedback(enc);

   RVCE_BEGIN(0x02000001); // destroy
   RVCE_END();
}

static void motion_estimation(struct rvce_encoder *enc)
{
   RVCE_BEGIN(0x04000007); // motion estimation
   RVCE_CS(enc->enc_pic.me.enc_ime_decimation_search);
   RVCE_CS(enc->enc_pic.me.motion_est_half_pixel);
   RVCE_CS(enc->enc_pic.me.motion_est_quarter_pixel);
   RVCE_CS(enc->enc_pic.me.disable_favor_pmv_point);
   RVCE_CS(enc->enc_pic.me.force_zero_point_center);
   RVCE_CS(enc->enc_pic.me.lsmvert);
   RVCE_CS(enc->enc_pic.me.enc_search_range_x);
   RVCE_CS(enc->enc_pic.me.enc_search_range_y);
   RVCE_CS(enc->enc_pic.me.enc_search1_range_x);
   RVCE_CS(enc->enc_pic.me.enc_search1_range_y);
   RVCE_CS(enc->enc_pic.me.disable_16x16_frame1);
   RVCE_CS(enc->enc_pic.me.disable_satd);
   RVCE_CS(enc->enc_pic.me.enable_amd);
   RVCE_CS(enc->enc_pic.me.enc_disable_sub_mode);
   RVCE_CS(enc->enc_pic.me.enc_ime_skip_x);
   RVCE_CS(enc->enc_pic.me.enc_ime_skip_y);
   RVCE_CS(enc->enc_pic.me.enc_en_ime_overw_dis_subm);
   RVCE_CS(enc->enc_pic.me.enc_ime_overw_dis_subm_no);
   RVCE_CS(enc->enc_pic.me.enc_ime2_search_range_x);
   RVCE_CS(enc->enc_pic.me.enc_ime2_search_range_y);
   RVCE_CS(enc->enc_pic.me.parallel_mode_speedup_enable);
   RVCE_CS(enc->enc_pic.me.fme0_enc_disable_sub_mode);
   RVCE_CS(enc->enc_pic.me.fme1_enc_disable_sub_mode);
   RVCE_CS(enc->enc_pic.me.ime_sw_speedup_enable);
   RVCE_END();
}

static void pic_control(struct rvce_encoder *enc)
{
   RVCE_BEGIN(0x04000002); // pic control
   RVCE_CS(enc->enc_pic.pc.enc_use_constrained_intra_pred);
   RVCE_CS(enc->enc_pic.pc.enc_cabac_enable);
   RVCE_CS(enc->enc_pic.pc.enc_cabac_idc);
   RVCE_CS(enc->enc_pic.pc.enc_loop_filter_disable);
   RVCE_CS(enc->enc_pic.pc.enc_lf_beta_offset);
   RVCE_CS(enc->enc_pic.pc.enc_lf_alpha_c0_offset);
   RVCE_CS(enc->enc_pic.pc.enc_crop_left_offset);
   RVCE_CS(enc->enc_pic.pc.enc_crop_right_offset);
   RVCE_CS(enc->enc_pic.pc.enc_crop_top_offset);
   RVCE_CS(enc->enc_pic.pc.enc_crop_bottom_offset);
   RVCE_CS(enc->enc_pic.pc.enc_num_mbs_per_slice);
   RVCE_CS(enc->enc_pic.pc.enc_intra_refresh_num_mbs_per_slot);
   RVCE_CS(enc->enc_pic.pc.enc_force_intra_refresh);
   RVCE_CS(enc->enc_pic.pc.enc_force_imb_period);
   RVCE_CS(enc->enc_pic.pc.enc_pic_order_cnt_type);
   RVCE_CS(enc->enc_pic.pc.log2_max_pic_order_cnt_lsb_minus4);
   RVCE_CS(enc->enc_pic.pc.enc_sps_id);
   RVCE_CS(enc->enc_pic.pc.enc_pps_id);
   RVCE_CS(enc->enc_pic.pc.enc_constraint_set_flags);
   RVCE_CS(enc->enc_pic.pc.enc_b_pic_pattern);
   RVCE_CS(enc->enc_pic.pc.weight_pred_mode_b_picture);
   RVCE_CS(enc->enc_pic.pc.enc_number_of_reference_frames);
   RVCE_CS(enc->enc_pic.pc.enc_max_num_ref_frames);
   RVCE_CS(enc->enc_pic.pc.enc_num_default_active_ref_l0);
   RVCE_CS(enc->enc_pic.pc.enc_num_default_active_ref_l1);
   RVCE_CS(enc->enc_pic.pc.enc_slice_mode);
   RVCE_CS(enc->enc_pic.pc.enc_max_slice_size);
   RVCE_END();
}

static void rdo(struct rvce_encoder *enc)
{
   RVCE_BEGIN(0x04000008); // rdo
   RVCE_CS(enc->enc_pic.rdo.enc_disable_tbe_pred_i_frame);
   RVCE_CS(enc->enc_pic.rdo.enc_disable_tbe_pred_p_frame);
   RVCE_CS(enc->enc_pic.rdo.use_fme_interpol_y);
   RVCE_CS(enc->enc_pic.rdo.use_fme_interpol_uv);
   RVCE_CS(enc->enc_pic.rdo.use_fme_intrapol_y);
   RVCE_CS(enc->enc_pic.rdo.use_fme_intrapol_uv);
   RVCE_CS(enc->enc_pic.rdo.use_fme_interpol_y_1);
   RVCE_CS(enc->enc_pic.rdo.use_fme_interpol_uv_1);
   RVCE_CS(enc->enc_pic.rdo.use_fme_intrapol_y_1);
   RVCE_CS(enc->enc_pic.rdo.use_fme_intrapol_uv_1);
   RVCE_CS(enc->enc_pic.rdo.enc_16x16_cost_adj);
   RVCE_CS(enc->enc_pic.rdo.enc_skip_cost_adj);
   RVCE_CS(enc->enc_pic.rdo.enc_force_16x16_skip);
   RVCE_CS(enc->enc_pic.rdo.enc_disable_threshold_calc_a);
   RVCE_CS(enc->enc_pic.rdo.enc_luma_coeff_cost);
   RVCE_CS(enc->enc_pic.rdo.enc_luma_mb_coeff_cost);
   RVCE_CS(enc->enc_pic.rdo.enc_chroma_coeff_cost);
   RVCE_END();
}

static void session(struct rvce_encoder *enc)
{
   RVCE_BEGIN(0x00000001); // session cmd
   RVCE_CS(enc->stream_handle);
   RVCE_END();
}

static void task_info(struct rvce_encoder *enc, uint32_t op, uint32_t fb_idx)
{
   RVCE_BEGIN(0x00000002); // task info
   enc->enc_pic.ti.task_operation = op;
   enc->enc_pic.ti.reference_picture_dependency = 0;
   enc->enc_pic.ti.feedback_index = fb_idx;
   enc->enc_pic.ti.video_bitstream_ring_index = 0;
   RVCE_CS(enc->enc_pic.ti.offset_of_next_task_info);
   RVCE_CS(enc->enc_pic.ti.task_operation);
   RVCE_CS(enc->enc_pic.ti.reference_picture_dependency);
   RVCE_CS(enc->enc_pic.ti.collocate_flag_dependency);
   RVCE_CS(enc->enc_pic.ti.feedback_index);
   RVCE_CS(enc->enc_pic.ti.video_bitstream_ring_index);
   RVCE_END();
}

unsigned int si_vce_write_sps(struct rvce_encoder *enc, uint8_t nal_byte, uint8_t *out)
{
   struct pipe_h264_enc_seq_param *sps = &enc->pic.seq;
   struct radeon_bitstream bs;

   radeon_bs_reset(&bs, out, NULL);
   radeon_bs_set_emulation_prevention(&bs, false);
   radeon_bs_code_fixed_bits(&bs, 0x00000001, 32);
   radeon_bs_code_fixed_bits(&bs, nal_byte, 8);
   radeon_bs_set_emulation_prevention(&bs, true);
   radeon_bs_code_fixed_bits(&bs, sps->profile_idc, 8);
   radeon_bs_code_fixed_bits(&bs, sps->enc_constraint_set_flags, 6);
   radeon_bs_code_fixed_bits(&bs, 0x0, 2); /* reserved_zero_2bits */
   radeon_bs_code_fixed_bits(&bs, sps->level_idc, 8);
   radeon_bs_code_ue(&bs, 0x0); /* seq_parameter_set_id */

   if (sps->profile_idc == 100 || sps->profile_idc == 110 ||
       sps->profile_idc == 122 || sps->profile_idc == 244 ||
       sps->profile_idc == 44  || sps->profile_idc == 83 ||
       sps->profile_idc == 86  || sps->profile_idc == 118 ||
       sps->profile_idc == 128 || sps->profile_idc == 138) {
      radeon_bs_code_ue(&bs, 0x1); /* chroma_format_idc */
      radeon_bs_code_ue(&bs, 0x0); /* bit_depth_luma_minus8 */
      radeon_bs_code_ue(&bs, 0x0); /* bit_depth_chroma_minus8 */
      radeon_bs_code_fixed_bits(&bs, 0x0, 2); /* qpprime_y_zero_transform_bypass_flag + seq_scaling_matrix_present_flag */
   }

   radeon_bs_code_ue(&bs, 3); /* log2_max_frame_num_minus4 */
   radeon_bs_code_ue(&bs, sps->pic_order_cnt_type);

   if (sps->pic_order_cnt_type == 0)
      radeon_bs_code_ue(&bs, sps->log2_max_pic_order_cnt_lsb_minus4);

   radeon_bs_code_ue(&bs, sps->max_num_ref_frames);
   radeon_bs_code_fixed_bits(&bs, sps->gaps_in_frame_num_value_allowed_flag, 1);
   radeon_bs_code_ue(&bs, DIV_ROUND_UP(enc->base.width, 16) - 1);
   radeon_bs_code_ue(&bs, DIV_ROUND_UP(enc->base.height, 16) - 1);
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
         radeon_bs_code_ue(&bs, 0x2); /* max_bytes_per_pic_denom */
         radeon_bs_code_ue(&bs, 0x1); /* max_bits_per_mb_denom */
         radeon_bs_code_ue(&bs, 0x10); /* log2_max_mv_length_horizontal */
         radeon_bs_code_ue(&bs, 0x10); /* log2_max_mv_length_vertical */
         radeon_bs_code_ue(&bs, sps->max_num_reorder_frames);
         radeon_bs_code_ue(&bs, sps->max_dec_frame_buffering);
      }
   }

   radeon_bs_code_fixed_bits(&bs, 0x1, 1);
   radeon_bs_byte_align(&bs);

   return bs.bits_output / 8;
}

unsigned int si_vce_write_pps(struct rvce_encoder *enc, uint8_t nal_byte, uint8_t *out)
{
   struct radeon_bitstream bs;

   radeon_bs_reset(&bs, out, NULL);
   radeon_bs_set_emulation_prevention(&bs, false);
   radeon_bs_code_fixed_bits(&bs, 0x00000001, 32);
   radeon_bs_code_fixed_bits(&bs, nal_byte, 8);
   radeon_bs_set_emulation_prevention(&bs, true);
   radeon_bs_code_ue(&bs, 0x0); /* pic_parameter_set_id */
   radeon_bs_code_ue(&bs, 0x0); /* seq_parameter_set_id */
   radeon_bs_code_fixed_bits(&bs, enc->enc_pic.pc.enc_cabac_enable, 1);
   radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* bottom_field_pic_order_in_frame_present_flag */
   radeon_bs_code_ue(&bs, 0x0); /* num_slice_groups_minus_1 */
   radeon_bs_code_ue(&bs, enc->enc_pic.pc.enc_num_default_active_ref_l0 - 1);
   radeon_bs_code_ue(&bs, enc->enc_pic.pc.enc_num_default_active_ref_l1 - 1);
   radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* weighted_pred_flag */
   radeon_bs_code_fixed_bits(&bs, 0x0, 2); /* weighted_bipred_idc */
   radeon_bs_code_se(&bs, 0x0); /* pic_init_qp_minus26 */
   radeon_bs_code_se(&bs, 0x0); /* pic_init_qs_minus26 */
   radeon_bs_code_se(&bs, 0x0); /* chroma_qp_index_offset */
   bool deblocking_filter_present_flag =
      enc->enc_pic.pc.enc_loop_filter_disable ||
      enc->enc_pic.pc.enc_lf_beta_offset ||
      enc->enc_pic.pc.enc_lf_alpha_c0_offset;
   radeon_bs_code_fixed_bits(&bs, deblocking_filter_present_flag, 1);
   radeon_bs_code_fixed_bits(&bs, enc->enc_pic.pc.enc_use_constrained_intra_pred, 1);
   radeon_bs_code_fixed_bits(&bs, 0x0, 1); /* redundant_pic_cnt_present_flag */

   radeon_bs_code_fixed_bits(&bs, 0x1, 1);
   radeon_bs_byte_align(&bs);

   return bs.bits_output / 8;
}

void si_vce_52_init(struct rvce_encoder *enc)
{
   enc->session = session;
   enc->task_info = task_info;
   enc->create = create;
   enc->feedback = feedback;
   enc->rate_control = rate_control;
   enc->config_extension = config_extension;
   enc->pic_control = pic_control;
   enc->motion_estimation = motion_estimation;
   enc->rdo = rdo;
   enc->config = config;
   enc->encode = encode;
   enc->destroy = destroy;
   enc->si_get_pic_param = get_param;
}
