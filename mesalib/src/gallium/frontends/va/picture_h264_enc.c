/**************************************************************************
 *
 * Copyright 2018 Advanced Micro Devices, Inc.
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

#include "util/u_handle_table.h"
#include "util/u_video.h"
#include "va_private.h"

#include "util/vl_rbsp.h"

VAStatus
vlVaHandleVAEncPictureParameterBufferTypeH264(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAEncPictureParameterBufferH264 *h264;
   vlVaBuffer *coded_buf;
   vlVaSurface *surf;
   unsigned i, j;

   h264 = buf->data;
   if (h264->pic_fields.bits.idr_pic_flag == 1)
      context->desc.h264enc.frame_num = 0;
   context->desc.h264enc.not_referenced = !h264->pic_fields.bits.reference_pic_flag;
   context->desc.h264enc.pic_order_cnt = h264->CurrPic.TopFieldOrderCnt;
   context->desc.h264enc.is_ltr = h264->CurrPic.flags & VA_PICTURE_H264_LONG_TERM_REFERENCE;
   if (context->desc.h264enc.is_ltr)
      context->desc.h264enc.ltr_index = h264->CurrPic.frame_idx;
   if (context->desc.h264enc.gop_cnt == 0)
      context->desc.h264enc.i_remain = context->gop_coeff;
   else if (context->desc.h264enc.frame_num == 1)
      context->desc.h264enc.i_remain--;

   /* Evict unused surfaces */
   for (i = 0; i < context->desc.h264enc.dpb_size; i++) {
      struct pipe_h264_enc_dpb_entry *dpb = &context->desc.h264enc.dpb[i];
      if (!dpb->id || dpb->id == h264->CurrPic.picture_id)
         continue;
      for (j = 0; j < ARRAY_SIZE(h264->ReferenceFrames); j++) {
         if (h264->ReferenceFrames[j].picture_id == dpb->id) {
            dpb->evict = false;
            break;
         }
      }
      if (j == ARRAY_SIZE(h264->ReferenceFrames)) {
         if (dpb->evict) {
            surf = handle_table_get(drv->htab, dpb->id);
            assert(surf);
            surf->is_dpb = false;
            surf->buffer = NULL;
            /* Keep the buffer for reuse later */
            dpb->id = 0;
         }
         dpb->evict = !dpb->evict;
      }
   }

   surf = handle_table_get(drv->htab, h264->CurrPic.picture_id);
   if (!surf)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   for (i = 0; i < ARRAY_SIZE(context->desc.h264enc.dpb); i++) {
      if (context->desc.h264enc.dpb[i].id == h264->CurrPic.picture_id) {
         assert(surf->is_dpb);
         break;
      }
      if (!surf->is_dpb && !context->desc.h264enc.dpb[i].id) {
         surf->is_dpb = true;
         if (surf->buffer) {
            surf->buffer->destroy(surf->buffer);
            surf->buffer = NULL;
         }
         if (context->decoder->create_dpb_buffer) {
            struct pipe_video_buffer *buffer = context->desc.h264enc.dpb[i].buffer;
            if (!buffer) {
               /* Find unused buffer */
               for (j = 0; j < context->desc.h264enc.dpb_size; j++) {
                  struct pipe_h264_enc_dpb_entry *dpb = &context->desc.h264enc.dpb[j];
                  if (!dpb->id && dpb->buffer) {
                     buffer = dpb->buffer;
                     dpb->buffer = NULL;
                     break;
                  }
               }
            }
            if (!buffer)
               buffer = context->decoder->create_dpb_buffer(context->decoder, &context->desc.base, &surf->templat);
            surf->buffer = buffer;
         }
         vlVaSetSurfaceContext(drv, surf, context);
         if (i == context->desc.h264enc.dpb_size)
            context->desc.h264enc.dpb_size++;
         break;
      }
   }
   if (i == ARRAY_SIZE(context->desc.h264enc.dpb))
      return VA_STATUS_ERROR_INVALID_PARAMETER;
   context->desc.h264enc.dpb_curr_pic = i;
   context->desc.h264enc.dpb[i].id = h264->CurrPic.picture_id;
   context->desc.h264enc.dpb[i].frame_idx = h264->CurrPic.frame_idx;
   context->desc.h264enc.dpb[i].pic_order_cnt = h264->CurrPic.TopFieldOrderCnt;
   context->desc.h264enc.dpb[i].is_ltr = h264->CurrPic.flags & VA_PICTURE_H264_LONG_TERM_REFERENCE;
   context->desc.h264enc.dpb[i].buffer = surf->buffer;
   context->desc.h264enc.dpb[i].evict = false;

   context->desc.h264enc.p_remain = context->desc.h264enc.gop_size - context->desc.h264enc.gop_cnt - context->desc.h264enc.i_remain;

   coded_buf = handle_table_get(drv->htab, h264->coded_buf);
   if (!coded_buf)
      return VA_STATUS_ERROR_INVALID_BUFFER;

   if (!coded_buf->derived_surface.resource)
      coded_buf->derived_surface.resource = pipe_buffer_create(drv->pipe->screen, PIPE_BIND_VERTEX_BUFFER,
                                            PIPE_USAGE_STAGING, coded_buf->size);
   context->coded_buf = coded_buf;

   if (context->desc.h264enc.is_ltr)
      _mesa_hash_table_insert(context->desc.h264enc.frame_idx,
		       UINT_TO_PTR(h264->CurrPic.picture_id + 1),
		       UINT_TO_PTR(context->desc.h264enc.ltr_index));
   else
      _mesa_hash_table_insert(context->desc.h264enc.frame_idx,
		       UINT_TO_PTR(h264->CurrPic.picture_id + 1),
		       UINT_TO_PTR(context->desc.h264enc.frame_num));

   if (h264->pic_fields.bits.idr_pic_flag == 1)
      context->desc.h264enc.picture_type = PIPE_H2645_ENC_PICTURE_TYPE_IDR;
   else
      context->desc.h264enc.picture_type = PIPE_H2645_ENC_PICTURE_TYPE_P;

   /* Initialize slice descriptors for this picture */
   context->desc.h264enc.num_slice_descriptors = 0;
   memset(&context->desc.h264enc.slices_descriptors, 0, sizeof(context->desc.h264enc.slices_descriptors));

   context->desc.h264enc.init_qp = h264->pic_init_qp;
   context->desc.h264enc.gop_cnt++;
   if (context->desc.h264enc.gop_cnt == context->desc.h264enc.gop_size)
      context->desc.h264enc.gop_cnt = 0;

   context->desc.h264enc.pic_ctrl.enc_cabac_enable = h264->pic_fields.bits.entropy_coding_mode_flag;
   context->desc.h264enc.num_ref_idx_l0_active_minus1 = h264->num_ref_idx_l0_active_minus1;
   context->desc.h264enc.num_ref_idx_l1_active_minus1 = h264->num_ref_idx_l1_active_minus1;
   context->desc.h264enc.pic_ctrl.deblocking_filter_control_present_flag
      = h264->pic_fields.bits.deblocking_filter_control_present_flag;
   context->desc.h264enc.pic_ctrl.redundant_pic_cnt_present_flag
      = h264->pic_fields.bits.redundant_pic_cnt_present_flag;
   context->desc.h264enc.pic_ctrl.chroma_qp_index_offset = h264->chroma_qp_index_offset;
   context->desc.h264enc.pic_ctrl.second_chroma_qp_index_offset
      = h264->second_chroma_qp_index_offset;
   context->desc.h264enc.pic_ctrl.constrained_intra_pred_flag =
      h264->pic_fields.bits.constrained_intra_pred_flag;
   context->desc.h264enc.pic_ctrl.transform_8x8_mode_flag =
      h264->pic_fields.bits.transform_8x8_mode_flag;

   return VA_STATUS_SUCCESS;
}

static uint8_t
vlVaDpbIndex(vlVaContext *context, VASurfaceID id)
{
   for (uint8_t i = 0; i < context->desc.h264enc.dpb_size; i++) {
      if (context->desc.h264enc.dpb[i].id == id)
         return i;
   }
   return PIPE_H2645_LIST_REF_INVALID_ENTRY;
}

VAStatus
vlVaHandleVAEncSliceParameterBufferTypeH264(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAEncSliceParameterBufferH264 *h264;
   unsigned slice_qp;

   h264 = buf->data;

   /* Handle the slice control parameters */
   struct h264_slice_descriptor slice_descriptor;
   memset(&slice_descriptor, 0, sizeof(slice_descriptor));
   slice_descriptor.macroblock_address = h264->macroblock_address;
   slice_descriptor.num_macroblocks = h264->num_macroblocks;
   slice_descriptor.slice_type = h264->slice_type;
   assert(slice_descriptor.slice_type <= PIPE_H264_SLICE_TYPE_I);

   if (context->desc.h264enc.num_slice_descriptors < ARRAY_SIZE(context->desc.h264enc.slices_descriptors))
      context->desc.h264enc.slices_descriptors[context->desc.h264enc.num_slice_descriptors++] = slice_descriptor;
   else
      return VA_STATUS_ERROR_NOT_ENOUGH_BUFFER;

   /* Only use parameters for first slice */
   if (h264->macroblock_address)
      return VA_STATUS_SUCCESS;

   memset(&context->desc.h264enc.ref_idx_l0_list, VA_INVALID_ID, sizeof(context->desc.h264enc.ref_idx_l0_list));
   memset(&context->desc.h264enc.ref_idx_l1_list, VA_INVALID_ID, sizeof(context->desc.h264enc.ref_idx_l1_list));
   memset(&context->desc.h264enc.ref_list0, PIPE_H2645_LIST_REF_INVALID_ENTRY, sizeof(context->desc.h264enc.ref_list0));
   memset(&context->desc.h264enc.ref_list1, PIPE_H2645_LIST_REF_INVALID_ENTRY, sizeof(context->desc.h264enc.ref_list1));

   if(h264->num_ref_idx_active_override_flag) {
      context->desc.h264enc.num_ref_idx_l0_active_minus1 = h264->num_ref_idx_l0_active_minus1;
      context->desc.h264enc.num_ref_idx_l1_active_minus1 = h264->num_ref_idx_l1_active_minus1;
   }

   if (h264->slice_type != PIPE_H264_SLICE_TYPE_I && h264->slice_type != PIPE_H264_SLICE_TYPE_SI) {
      for (int i = 0; i < 32; i++) {
         if (h264->RefPicList0[i].picture_id != VA_INVALID_ID) {
            context->desc.h264enc.ref_list0[i] = vlVaDpbIndex(context, h264->RefPicList0[i].picture_id);
            if (context->desc.h264enc.ref_list0[i] == PIPE_H2645_LIST_REF_INVALID_ENTRY)
               return VA_STATUS_ERROR_INVALID_PARAMETER;

            context->desc.h264enc.ref_idx_l0_list[i] = PTR_TO_UINT(util_hash_table_get(context->desc.h264enc.frame_idx,
                                    UINT_TO_PTR(h264->RefPicList0[i].picture_id + 1)));
            context->desc.h264enc.l0_is_long_term[i] = h264->RefPicList0[i].flags & VA_PICTURE_H264_LONG_TERM_REFERENCE;
         }
         if (h264->RefPicList1[i].picture_id != VA_INVALID_ID && h264->slice_type == PIPE_H264_SLICE_TYPE_B) {
            context->desc.h264enc.ref_list1[i] = vlVaDpbIndex(context, h264->RefPicList1[i].picture_id);
            if (context->desc.h264enc.ref_list1[i] == PIPE_H2645_LIST_REF_INVALID_ENTRY)
               return VA_STATUS_ERROR_INVALID_PARAMETER;

            context->desc.h264enc.ref_idx_l1_list[i] = PTR_TO_UINT(util_hash_table_get(context->desc.h264enc.frame_idx,
                                    UINT_TO_PTR(h264->RefPicList1[i].picture_id + 1)));
            context->desc.h264enc.l1_is_long_term[i] = h264->RefPicList1[i].flags & VA_PICTURE_H264_LONG_TERM_REFERENCE;
         }
      }
   }

   slice_qp = context->desc.h264enc.init_qp + h264->slice_qp_delta;

   if ((h264->slice_type == 1) || (h264->slice_type == 6)) {
      context->desc.h264enc.picture_type = PIPE_H2645_ENC_PICTURE_TYPE_B;
      context->desc.h264enc.quant_b_frames = slice_qp;
   } else if ((h264->slice_type == 0) || (h264->slice_type == 5)) {
      context->desc.h264enc.picture_type = PIPE_H2645_ENC_PICTURE_TYPE_P;
      context->desc.h264enc.quant_p_frames = slice_qp;
   } else if ((h264->slice_type == 2) || (h264->slice_type == 7)) {
      if (context->desc.h264enc.picture_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR)
         context->desc.h264enc.idr_pic_id++;
      else
         context->desc.h264enc.picture_type = PIPE_H2645_ENC_PICTURE_TYPE_I;
      context->desc.h264enc.quant_i_frames = slice_qp;
   } else {
      context->desc.h264enc.picture_type = PIPE_H2645_ENC_PICTURE_TYPE_SKIP;
   }

   context->desc.h264enc.dpb[context->desc.h264enc.dpb_curr_pic].picture_type = context->desc.h264enc.picture_type;

   context->desc.h264enc.pic_ctrl.enc_cabac_init_idc = h264->cabac_init_idc;
   context->desc.h264enc.dbk.disable_deblocking_filter_idc = h264->disable_deblocking_filter_idc;
   context->desc.h264enc.dbk.alpha_c0_offset_div2 = h264->slice_alpha_c0_offset_div2;
   context->desc.h264enc.dbk.beta_offset_div2 = h264->slice_beta_offset_div2;

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncSequenceParameterBufferTypeH264(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAEncSequenceParameterBufferH264 *h264 = buf->data;
   uint32_t num_units_in_tick = 0, time_scale  = 0;

   context->desc.h264enc.ip_period = h264->ip_period;
   context->desc.h264enc.intra_idr_period =
      h264->intra_idr_period != 0 ? h264->intra_idr_period : PIPE_DEFAULT_INTRA_IDR_PERIOD;
   context->gop_coeff = ((1024 + context->desc.h264enc.intra_idr_period - 1) /
                        context->desc.h264enc.intra_idr_period + 1) / 2 * 2;
   if (context->gop_coeff > VL_VA_ENC_GOP_COEFF)
      context->gop_coeff = VL_VA_ENC_GOP_COEFF;
   context->desc.h264enc.gop_size = context->desc.h264enc.intra_idr_period * context->gop_coeff;
   context->desc.h264enc.seq.pic_order_cnt_type = h264->seq_fields.bits.pic_order_cnt_type;
   context->desc.h264enc.seq.log2_max_frame_num_minus4 = h264->seq_fields.bits.log2_max_frame_num_minus4;
   context->desc.h264enc.seq.log2_max_pic_order_cnt_lsb_minus4 = h264->seq_fields.bits.log2_max_pic_order_cnt_lsb_minus4;
   context->desc.h264enc.seq.vui_parameters_present_flag = h264->vui_parameters_present_flag;
   if (h264->vui_parameters_present_flag) {
      context->desc.h264enc.seq.vui_flags.aspect_ratio_info_present_flag =
         h264->vui_fields.bits.aspect_ratio_info_present_flag;
      context->desc.h264enc.seq.aspect_ratio_idc = h264->aspect_ratio_idc;
      context->desc.h264enc.seq.sar_width = h264->sar_width;
      context->desc.h264enc.seq.sar_height = h264->sar_height;
      context->desc.h264enc.seq.vui_flags.timing_info_present_flag =
         h264->vui_fields.bits.timing_info_present_flag;
      num_units_in_tick = h264->num_units_in_tick;
      time_scale = h264->time_scale;
      context->desc.h264enc.seq.vui_flags.fixed_frame_rate_flag =
         h264->vui_fields.bits.fixed_frame_rate_flag;
      context->desc.h264enc.seq.vui_flags.low_delay_hrd_flag =
         h264->vui_fields.bits.low_delay_hrd_flag;
      context->desc.h264enc.seq.vui_flags.bitstream_restriction_flag =
         h264->vui_fields.bits.bitstream_restriction_flag;
      context->desc.h264enc.seq.vui_flags.motion_vectors_over_pic_boundaries_flag =
         h264->vui_fields.bits.motion_vectors_over_pic_boundaries_flag;
      context->desc.h264enc.seq.log2_max_mv_length_vertical =
            h264->vui_fields.bits.log2_max_mv_length_vertical;
      context->desc.h264enc.seq.log2_max_mv_length_horizontal =
            h264->vui_fields.bits.log2_max_mv_length_horizontal;
   } else {
      context->desc.h264enc.seq.vui_flags.timing_info_present_flag = 0;
      context->desc.h264enc.seq.vui_flags.fixed_frame_rate_flag = 0;
      context->desc.h264enc.seq.vui_flags.low_delay_hrd_flag = 0;
      context->desc.h264enc.seq.vui_flags.bitstream_restriction_flag = 0;
      context->desc.h264enc.seq.vui_flags.motion_vectors_over_pic_boundaries_flag = 0;
      context->desc.h264enc.seq.log2_max_mv_length_vertical = 0;
      context->desc.h264enc.seq.log2_max_mv_length_horizontal = 0;
   }

   if (!context->desc.h264enc.seq.vui_flags.timing_info_present_flag) {
      /* if not present, set default value */
      num_units_in_tick = PIPE_DEFAULT_FRAME_RATE_DEN;
      time_scale = PIPE_DEFAULT_FRAME_RATE_NUM * 2;
   }

   context->desc.h264enc.seq.num_units_in_tick = num_units_in_tick;
   context->desc.h264enc.seq.time_scale = time_scale;
   context->desc.h264enc.rate_ctrl[0].frame_rate_num = time_scale / 2;
   context->desc.h264enc.rate_ctrl[0].frame_rate_den = num_units_in_tick;

   if (h264->frame_cropping_flag) {
      context->desc.h264enc.seq.enc_frame_cropping_flag = h264->frame_cropping_flag;
      context->desc.h264enc.seq.enc_frame_crop_left_offset = h264->frame_crop_left_offset;
      context->desc.h264enc.seq.enc_frame_crop_right_offset = h264->frame_crop_right_offset;
      context->desc.h264enc.seq.enc_frame_crop_top_offset = h264->frame_crop_top_offset;
      context->desc.h264enc.seq.enc_frame_crop_bottom_offset = h264->frame_crop_bottom_offset;
   }

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncMiscParameterTypeRateControlH264(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   unsigned temporal_id;
   VAEncMiscParameterRateControl *rc = (VAEncMiscParameterRateControl *)misc->data;

   temporal_id = context->desc.h264enc.rate_ctrl[0].rate_ctrl_method !=
                 PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE ?
                 rc->rc_flags.bits.temporal_id :
                 0;

   if (context->desc.h264enc.rate_ctrl[0].rate_ctrl_method ==
       PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT)
      context->desc.h264enc.rate_ctrl[temporal_id].target_bitrate =
         rc->bits_per_second;
   else
      context->desc.h264enc.rate_ctrl[temporal_id].target_bitrate =
         rc->bits_per_second * (rc->target_percentage / 100.0);

   if (context->desc.h264enc.seq.num_temporal_layers > 0 &&
       temporal_id >= context->desc.h264enc.seq.num_temporal_layers)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   context->desc.h264enc.rate_ctrl[temporal_id].fill_data_enable = !(rc->rc_flags.bits.disable_bit_stuffing);
   /* context->desc.h264enc.rate_ctrl[temporal_id].skip_frame_enable = !(rc->rc_flags.bits.disable_frame_skip); */
   context->desc.h264enc.rate_ctrl[temporal_id].skip_frame_enable = 0;
   context->desc.h264enc.rate_ctrl[temporal_id].peak_bitrate = rc->bits_per_second;

   if ((context->desc.h264enc.rate_ctrl[0].rate_ctrl_method == PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT) ||
       (context->desc.h264enc.rate_ctrl[0].rate_ctrl_method == PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT_SKIP))
      context->desc.h264enc.rate_ctrl[temporal_id].vbv_buffer_size =
         context->desc.h264enc.rate_ctrl[temporal_id].target_bitrate;
   else if (context->desc.h264enc.rate_ctrl[temporal_id].target_bitrate < 2000000)
      context->desc.h264enc.rate_ctrl[temporal_id].vbv_buffer_size =
         MIN2((context->desc.h264enc.rate_ctrl[0].target_bitrate * 2.75), 2000000);
   else
      context->desc.h264enc.rate_ctrl[temporal_id].vbv_buffer_size =
         context->desc.h264enc.rate_ctrl[temporal_id].target_bitrate;

   context->desc.h264enc.rate_ctrl[temporal_id].max_qp = rc->max_qp;
   context->desc.h264enc.rate_ctrl[temporal_id].min_qp = rc->min_qp;
   /* Distinguishes from the default params set for these values in other
      functions and app specific params passed down */
   context->desc.h264enc.rate_ctrl[temporal_id].app_requested_qp_range = ((rc->max_qp > 0) || (rc->min_qp > 0));

   if (context->desc.h264enc.rate_ctrl[0].rate_ctrl_method ==
       PIPE_H2645_ENC_RATE_CONTROL_METHOD_QUALITY_VARIABLE)
      context->desc.h264enc.rate_ctrl[temporal_id].vbr_quality_factor =
         rc->quality_factor;

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncMiscParameterTypeFrameRateH264(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   unsigned temporal_id;
   VAEncMiscParameterFrameRate *fr = (VAEncMiscParameterFrameRate *)misc->data;

   temporal_id = context->desc.h264enc.rate_ctrl[0].rate_ctrl_method !=
                 PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE ?
                 fr->framerate_flags.bits.temporal_id :
                 0;

   if (context->desc.h264enc.seq.num_temporal_layers > 0 &&
       temporal_id >= context->desc.h264enc.seq.num_temporal_layers)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   if (fr->framerate & 0xffff0000) {
      context->desc.h264enc.rate_ctrl[temporal_id].frame_rate_num = fr->framerate       & 0xffff;
      context->desc.h264enc.rate_ctrl[temporal_id].frame_rate_den = fr->framerate >> 16 & 0xffff;
   } else {
      context->desc.h264enc.rate_ctrl[temporal_id].frame_rate_num = fr->framerate;
      context->desc.h264enc.rate_ctrl[temporal_id].frame_rate_den = 1;
   }

   return VA_STATUS_SUCCESS;
}

static void parseEncSliceParamsH264(vlVaContext *context,
                                    struct vl_rbsp *rbsp,
                                    unsigned nal_ref_idc,
                                    unsigned nal_unit_type)
{
   struct pipe_h264_enc_seq_param *seq = &context->desc.h264enc.seq;
   struct pipe_h264_enc_pic_control *pic = &context->desc.h264enc.pic_ctrl;
   struct pipe_h264_enc_slice_param *slice = &context->desc.h264enc.slice;
   unsigned modification_of_pic_nums_idc, memory_management_control_operation;

   /* Only parse first slice */
   if (vl_rbsp_ue(rbsp) != 0) /* first_mb_in_slice */
      return;

   pic->nal_ref_idc = nal_ref_idc;
   pic->nal_unit_type = nal_unit_type;

   slice->slice_type = vl_rbsp_ue(rbsp) % 5;
   vl_rbsp_ue(rbsp); /* pic_parameter_set_id */
   slice->frame_num = vl_rbsp_u(rbsp, seq->log2_max_frame_num_minus4 + 4);

   if (context->desc.h264enc.picture_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR)
      slice->idr_pic_id = vl_rbsp_ue(rbsp);

   if (seq->pic_order_cnt_type == 0)
      slice->pic_order_cnt_lsb = vl_rbsp_u(rbsp, seq->log2_max_pic_order_cnt_lsb_minus4 + 4);

   if (pic->redundant_pic_cnt_present_flag)
      slice->redundant_pic_cnt = vl_rbsp_ue(rbsp);

   if (slice->slice_type == PIPE_H264_SLICE_TYPE_B)
      slice->direct_spatial_mv_pred_flag = vl_rbsp_u(rbsp, 1);

   if (slice->slice_type == PIPE_H264_SLICE_TYPE_P ||
       slice->slice_type == PIPE_H264_SLICE_TYPE_SP ||
       slice->slice_type == PIPE_H264_SLICE_TYPE_B) {
      slice->num_ref_idx_active_override_flag = vl_rbsp_u(rbsp, 1);
      if (slice->num_ref_idx_active_override_flag) {
         slice->num_ref_idx_l0_active_minus1 = vl_rbsp_ue(rbsp);
         if (slice->slice_type == PIPE_H264_SLICE_TYPE_B)
            slice->num_ref_idx_l1_active_minus1 = vl_rbsp_ue(rbsp);
      }
   }

   if (slice->slice_type != PIPE_H264_SLICE_TYPE_I &&
       slice->slice_type != PIPE_H264_SLICE_TYPE_SI) {
      slice->ref_pic_list_modification_flag_l0 = vl_rbsp_u(rbsp, 1);
      if (slice->ref_pic_list_modification_flag_l0) {
         slice->num_ref_list0_mod_operations = 0;
         while (true) {
            modification_of_pic_nums_idc = vl_rbsp_ue(rbsp);
            if (modification_of_pic_nums_idc == 3)
               break;
            struct pipe_h264_ref_list_mod_entry *op =
               &slice->ref_list0_mod_operations[slice->num_ref_list0_mod_operations++];
            op->modification_of_pic_nums_idc = modification_of_pic_nums_idc;
            if (op->modification_of_pic_nums_idc == 0 ||
                op->modification_of_pic_nums_idc == 1)
               op->abs_diff_pic_num_minus1 = vl_rbsp_ue(rbsp);
            else if (op->modification_of_pic_nums_idc == 2)
               op->long_term_pic_num = vl_rbsp_ue(rbsp);
         }
      }
   }

   if (slice->slice_type == PIPE_H264_SLICE_TYPE_B) {
      slice->ref_pic_list_modification_flag_l1 = vl_rbsp_u(rbsp, 1);
      if (slice->ref_pic_list_modification_flag_l1) {
         slice->num_ref_list1_mod_operations = 0;
         while (true) {
            modification_of_pic_nums_idc = vl_rbsp_ue(rbsp);
            if (modification_of_pic_nums_idc == 3)
               break;
            struct pipe_h264_ref_list_mod_entry *op =
               &slice->ref_list1_mod_operations[slice->num_ref_list1_mod_operations++];
            op->modification_of_pic_nums_idc = modification_of_pic_nums_idc;
            if (op->modification_of_pic_nums_idc == 0 ||
                op->modification_of_pic_nums_idc == 1)
               op->abs_diff_pic_num_minus1 = vl_rbsp_ue(rbsp);
            else if (op->modification_of_pic_nums_idc == 2)
               op->long_term_pic_num = vl_rbsp_ue(rbsp);
         }
      }
   }

   if (nal_ref_idc != 0) {
      if (nal_unit_type == PIPE_H264_NAL_IDR_SLICE) {
         slice->no_output_of_prior_pics_flag = vl_rbsp_u(rbsp, 1);
         slice->long_term_reference_flag = vl_rbsp_u(rbsp, 1);
      } else {
         slice->adaptive_ref_pic_marking_mode_flag = vl_rbsp_u(rbsp, 1);
         if (slice->adaptive_ref_pic_marking_mode_flag) {
            slice->num_ref_pic_marking_operations = 0;
            while (true) {
               memory_management_control_operation = vl_rbsp_ue(rbsp);
               if (memory_management_control_operation == 0)
                  break;
               struct pipe_h264_ref_pic_marking_entry *op =
                  &slice->ref_pic_marking_operations[slice->num_ref_pic_marking_operations++];
               op->memory_management_control_operation = memory_management_control_operation;
               if (memory_management_control_operation == 1 ||
                   memory_management_control_operation == 3)
                  op->difference_of_pic_nums_minus1 = vl_rbsp_ue(rbsp);
               if (memory_management_control_operation == 2)
                  op->long_term_pic_num = vl_rbsp_ue(rbsp);
               if (memory_management_control_operation == 3 ||
                   memory_management_control_operation == 6)
                  op->long_term_frame_idx = vl_rbsp_ue(rbsp);
               if (memory_management_control_operation == 4)
                  op->max_long_term_frame_idx_plus1 = vl_rbsp_ue(rbsp);
            }
         }
      }
   }

   if (pic->entropy_coding_mode_flag &&
       slice->slice_type != PIPE_H264_SLICE_TYPE_I &&
       slice->slice_type != PIPE_H264_SLICE_TYPE_SI)
      slice->cabac_init_idc = vl_rbsp_ue(rbsp);

   slice->slice_qp_delta = vl_rbsp_se(rbsp);

   if (slice->slice_type == PIPE_H264_SLICE_TYPE_SP ||
       slice->slice_type == PIPE_H264_SLICE_TYPE_SI) {
      if (slice->slice_type == PIPE_H264_SLICE_TYPE_SP)
         vl_rbsp_u(rbsp, 1); /* sp_for_switch_flag */
      vl_rbsp_se(rbsp); /* slice_qs_delta */
   }

   if (pic->deblocking_filter_control_present_flag) {
      slice->disable_deblocking_filter_idc = vl_rbsp_ue(rbsp);
      if (slice->disable_deblocking_filter_idc != 1) {
         slice->slice_alpha_c0_offset_div2 = vl_rbsp_se(rbsp);
         slice->slice_beta_offset_div2 = vl_rbsp_se(rbsp);
      }
   }
}

static void parseEncHrdParamsH264(struct vl_rbsp *rbsp, pipe_h264_enc_hrd_params* hrd_params)
{
   unsigned i;

   hrd_params->cpb_cnt_minus1 = vl_rbsp_ue(rbsp);
   hrd_params->bit_rate_scale = vl_rbsp_u(rbsp, 4);
   hrd_params->cpb_size_scale = vl_rbsp_u(rbsp, 4);
   for (i = 0; i <= hrd_params->cpb_cnt_minus1; ++i) {
      hrd_params->bit_rate_value_minus1[i] = vl_rbsp_ue(rbsp);
      hrd_params->cpb_size_value_minus1[i] = vl_rbsp_ue(rbsp);
      hrd_params->cbr_flag[i] = vl_rbsp_u(rbsp, 1);
   }
   hrd_params->initial_cpb_removal_delay_length_minus1 = vl_rbsp_u(rbsp, 5);
   hrd_params->cpb_removal_delay_length_minus1 = vl_rbsp_u(rbsp, 5);
   hrd_params->dpb_output_delay_length_minus1 = vl_rbsp_u(rbsp, 5);
   hrd_params->time_offset_length = vl_rbsp_u(rbsp, 5);
}

static void parseEncSpsParamsH264(vlVaContext *context, struct vl_rbsp *rbsp)
{
   unsigned i, profile_idc, num_ref_frames_in_pic_order_cnt_cycle;

   context->desc.h264enc.seq.profile_idc = vl_rbsp_u(rbsp, 8);
   context->desc.h264enc.seq.enc_constraint_set_flags = vl_rbsp_u(rbsp, 6);
   vl_rbsp_u(rbsp, 2); /* reserved_zero_2bits */
   context->desc.h264enc.seq.level_idc = vl_rbsp_u(rbsp, 8);

   vl_rbsp_ue(rbsp); /* seq_parameter_set_id */

   profile_idc = context->desc.h264enc.seq.profile_idc;
   if (profile_idc == 100 || profile_idc == 110 ||
       profile_idc == 122 || profile_idc == 244 || profile_idc == 44 ||
       profile_idc == 83 || profile_idc == 86 || profile_idc == 118 ||
       profile_idc == 128 || profile_idc == 138 || profile_idc == 139 ||
       profile_idc == 134 || profile_idc == 135) {

      if (vl_rbsp_ue(rbsp) == 3) /* chroma_format_idc */
         vl_rbsp_u(rbsp, 1); /* separate_colour_plane_flag */

      context->desc.h264enc.seq.bit_depth_luma_minus8 = vl_rbsp_ue(rbsp);
      context->desc.h264enc.seq.bit_depth_chroma_minus8 = vl_rbsp_ue(rbsp);
      vl_rbsp_u(rbsp, 1); /* qpprime_y_zero_transform_bypass_flag */

      if (vl_rbsp_u(rbsp, 1)) { /* seq_scaling_matrix_present_flag */
         debug_error("SPS scaling matrix not supported");
         return;
      }
   }

   context->desc.h264enc.seq.log2_max_frame_num_minus4 = vl_rbsp_ue(rbsp);
   context->desc.h264enc.seq.pic_order_cnt_type = vl_rbsp_ue(rbsp);

   if (context->desc.h264enc.seq.pic_order_cnt_type == 0)
      context->desc.h264enc.seq.log2_max_pic_order_cnt_lsb_minus4 = vl_rbsp_ue(rbsp);
   else if (context->desc.h264enc.seq.pic_order_cnt_type == 1) {
      vl_rbsp_u(rbsp, 1); /* delta_pic_order_always_zero_flag */
      vl_rbsp_se(rbsp); /* offset_for_non_ref_pic */
      vl_rbsp_se(rbsp); /* offset_for_top_to_bottom_field */
      num_ref_frames_in_pic_order_cnt_cycle = vl_rbsp_ue(rbsp);
      for (i = 0; i < num_ref_frames_in_pic_order_cnt_cycle; ++i)
         vl_rbsp_se(rbsp); /* offset_for_ref_frame[i] */
   }

   context->desc.h264enc.seq.max_num_ref_frames = vl_rbsp_ue(rbsp);
   context->desc.h264enc.seq.gaps_in_frame_num_value_allowed_flag = vl_rbsp_u(rbsp, 1);
   context->desc.h264enc.seq.pic_width_in_mbs_minus1 = vl_rbsp_ue(rbsp);
   context->desc.h264enc.seq.pic_height_in_map_units_minus1 = vl_rbsp_ue(rbsp);
   if (!vl_rbsp_u(rbsp, 1)) /* frame_mbs_only_flag */
      vl_rbsp_u(rbsp, 1); /* mb_adaptive_frame_field_flag */

   context->desc.h264enc.seq.direct_8x8_inference_flag = vl_rbsp_u(rbsp, 1);
   context->desc.h264enc.seq.enc_frame_cropping_flag = vl_rbsp_u(rbsp, 1);
   if (context->desc.h264enc.seq.enc_frame_cropping_flag) {
      context->desc.h264enc.seq.enc_frame_crop_left_offset = vl_rbsp_ue(rbsp);
      context->desc.h264enc.seq.enc_frame_crop_right_offset = vl_rbsp_ue(rbsp);
      context->desc.h264enc.seq.enc_frame_crop_top_offset = vl_rbsp_ue(rbsp);
      context->desc.h264enc.seq.enc_frame_crop_bottom_offset = vl_rbsp_ue(rbsp);
   }

   context->desc.h264enc.seq.vui_parameters_present_flag = vl_rbsp_u(rbsp, 1);
   if (context->desc.h264enc.seq.vui_parameters_present_flag) {
      context->desc.h264enc.seq.vui_flags.aspect_ratio_info_present_flag = vl_rbsp_u(rbsp, 1);
      if (context->desc.h264enc.seq.vui_flags.aspect_ratio_info_present_flag) {
         context->desc.h264enc.seq.aspect_ratio_idc = vl_rbsp_u(rbsp, 8);
         if (context->desc.h264enc.seq.aspect_ratio_idc == PIPE_H2645_EXTENDED_SAR) {
            context->desc.h264enc.seq.sar_width = vl_rbsp_u(rbsp, 16);
            context->desc.h264enc.seq.sar_height = vl_rbsp_u(rbsp, 16);
         }
      }

      context->desc.h264enc.seq.vui_flags.overscan_info_present_flag = vl_rbsp_u(rbsp, 1);
      if (context->desc.h264enc.seq.vui_flags.overscan_info_present_flag)
         context->desc.h264enc.seq.vui_flags.overscan_appropriate_flag = vl_rbsp_u(rbsp, 1);

      context->desc.h264enc.seq.vui_flags.video_signal_type_present_flag = vl_rbsp_u(rbsp, 1);
      if (context->desc.h264enc.seq.vui_flags.video_signal_type_present_flag) {
         context->desc.h264enc.seq.video_format = vl_rbsp_u(rbsp, 3);
         context->desc.h264enc.seq.video_full_range_flag = vl_rbsp_u(rbsp, 1);
         context->desc.h264enc.seq.vui_flags.colour_description_present_flag = vl_rbsp_u(rbsp, 1);
         if (context->desc.h264enc.seq.vui_flags.colour_description_present_flag) {
            context->desc.h264enc.seq.colour_primaries = vl_rbsp_u(rbsp, 8);
            context->desc.h264enc.seq.transfer_characteristics = vl_rbsp_u(rbsp, 8);
            context->desc.h264enc.seq.matrix_coefficients = vl_rbsp_u(rbsp, 8);
         }
      }

      context->desc.h264enc.seq.vui_flags.chroma_loc_info_present_flag = vl_rbsp_u(rbsp, 1);
      if (context->desc.h264enc.seq.vui_flags.chroma_loc_info_present_flag) {
         context->desc.h264enc.seq.chroma_sample_loc_type_top_field = vl_rbsp_ue(rbsp);
         context->desc.h264enc.seq.chroma_sample_loc_type_bottom_field = vl_rbsp_ue(rbsp);
      }

      context->desc.h264enc.seq.vui_flags.timing_info_present_flag = vl_rbsp_u(rbsp, 1);
      if (context->desc.h264enc.seq.vui_flags.timing_info_present_flag) {
         context->desc.h264enc.seq.num_units_in_tick = vl_rbsp_u(rbsp, 32);
         context->desc.h264enc.seq.time_scale = vl_rbsp_u(rbsp, 32);
         context->desc.h264enc.seq.vui_flags.fixed_frame_rate_flag = vl_rbsp_u(rbsp, 1);
      }

      context->desc.h264enc.seq.vui_flags.nal_hrd_parameters_present_flag = vl_rbsp_u(rbsp, 1);
      if (context->desc.h264enc.seq.vui_flags.nal_hrd_parameters_present_flag)
         parseEncHrdParamsH264(rbsp, &context->desc.h264enc.seq.nal_hrd_parameters);

      context->desc.h264enc.seq.vui_flags.vcl_hrd_parameters_present_flag = vl_rbsp_u(rbsp, 1);
      if (context->desc.h264enc.seq.vui_flags.vcl_hrd_parameters_present_flag)
         parseEncHrdParamsH264(rbsp, &context->desc.h264enc.seq.vcl_hrd_parameters);

      if (context->desc.h264enc.seq.vui_flags.nal_hrd_parameters_present_flag ||
          context->desc.h264enc.seq.vui_flags.vcl_hrd_parameters_present_flag)
         context->desc.h264enc.seq.vui_flags.low_delay_hrd_flag = vl_rbsp_u(rbsp, 1);

      context->desc.h264enc.seq.vui_flags.pic_struct_present_flag = vl_rbsp_u(rbsp, 1);

      context->desc.h264enc.seq.vui_flags.bitstream_restriction_flag = vl_rbsp_u(rbsp, 1);
      if (context->desc.h264enc.seq.vui_flags.bitstream_restriction_flag) {
         context->desc.h264enc.seq.vui_flags.motion_vectors_over_pic_boundaries_flag = vl_rbsp_u(rbsp, 1);
         context->desc.h264enc.seq.max_bytes_per_pic_denom = vl_rbsp_ue(rbsp);
         context->desc.h264enc.seq.max_bits_per_mb_denom = vl_rbsp_ue(rbsp);
         context->desc.h264enc.seq.log2_max_mv_length_horizontal = vl_rbsp_ue(rbsp);
         context->desc.h264enc.seq.log2_max_mv_length_vertical = vl_rbsp_ue(rbsp);
         context->desc.h264enc.seq.max_num_reorder_frames = vl_rbsp_ue(rbsp);
         context->desc.h264enc.seq.max_dec_frame_buffering = vl_rbsp_ue(rbsp);
      }
   }
}

static void slice_group_map(struct vl_rbsp *rbsp, unsigned num_slice_groups_minus1)
{
   unsigned slice_group_map_type = vl_rbsp_ue(rbsp);
   if (slice_group_map_type == 0) {
      for (unsigned i = 0; i <= num_slice_groups_minus1; i++)
         vl_rbsp_ue(rbsp); /* run_length_minus1[i] */
   } else if (slice_group_map_type == 2) {
      for (unsigned i = 0; i <= num_slice_groups_minus1; i++) {
         vl_rbsp_ue(rbsp); /* top_left[i] */
         vl_rbsp_ue(rbsp); /* bottom_right[i] */
      }
   } else if (slice_group_map_type == 3 ||
              slice_group_map_type == 4 ||
              slice_group_map_type == 5) {
      vl_rbsp_u(rbsp, 1); /* slice_group_change_direction_flag */
      vl_rbsp_ue(rbsp); /* slice_group_change_rate_minus1 */
   } else if (slice_group_map_type == 6) {
      unsigned pic_size_in_map_units_minus1 = vl_rbsp_ue(rbsp);
      for (unsigned i = 0; i <= pic_size_in_map_units_minus1; i++)
         vl_rbsp_u(rbsp, util_logbase2_ceil(num_slice_groups_minus1 + 1)); /* slice_group_id[i] */
   }
}

static void parseEncPpsParamsH264(vlVaContext *context, struct vl_rbsp *rbsp)
{
   struct pipe_h264_enc_pic_control *pic = &context->desc.h264enc.pic_ctrl;

   vl_rbsp_ue(rbsp); /* pic_parameter_set_id */
   vl_rbsp_ue(rbsp); /* seq_parameter_set_id */
   pic->entropy_coding_mode_flag = vl_rbsp_u(rbsp, 1);
   vl_rbsp_u(rbsp, 1); /* bottom_field_pic_order_in_frame_present_flag */
   unsigned num_slice_groups_minus1 = vl_rbsp_ue(rbsp);
   if (num_slice_groups_minus1 > 0)
      slice_group_map(rbsp, num_slice_groups_minus1);
   pic->num_ref_idx_l0_default_active_minus1 = vl_rbsp_ue(rbsp);
   pic->num_ref_idx_l1_default_active_minus1 = vl_rbsp_ue(rbsp);
   pic->weighted_pred_flag = vl_rbsp_u(rbsp, 1);
   pic->weighted_bipred_idc = vl_rbsp_u(rbsp, 2);
   pic->pic_init_qp_minus26 = vl_rbsp_se(rbsp);
   pic->pic_init_qs_minus26 = vl_rbsp_se(rbsp);
   pic->chroma_qp_index_offset = vl_rbsp_se(rbsp);
   pic->deblocking_filter_control_present_flag = vl_rbsp_u(rbsp, 1);
   pic->constrained_intra_pred_flag = vl_rbsp_u(rbsp, 1);
   pic->redundant_pic_cnt_present_flag = vl_rbsp_u(rbsp, 1);
   if (vl_rbsp_more_data(rbsp)) {
      pic->transform_8x8_mode_flag = vl_rbsp_u(rbsp, 1);
      if (vl_rbsp_u(rbsp, 1)) { /* pic_scaling_matrix_present_flag */
         debug_error("PPS scaling matrix not supported");
         return;
      }
      pic->second_chroma_qp_index_offset = vl_rbsp_se(rbsp);
   } else {
      pic->transform_8x8_mode_flag = 0;
      pic->second_chroma_qp_index_offset = pic->chroma_qp_index_offset;
   }
}

static void parseEncPrefixH264(vlVaContext *context, struct vl_rbsp *rbsp)
{
   if (!vl_rbsp_u(rbsp, 1)) /* svc_extension_flag */
      return;

   vl_rbsp_u(rbsp, 1); /* idr_flag */
   vl_rbsp_u(rbsp, 6); /* priority_id */
   vl_rbsp_u(rbsp, 1); /* no_inter_layer_pred_flag */
   vl_rbsp_u(rbsp, 3); /* dependency_id */
   vl_rbsp_u(rbsp, 4); /* quality_id */
   context->desc.h264enc.pic_ctrl.temporal_id = vl_rbsp_u(rbsp, 3);
}

VAStatus
vlVaHandleVAEncPackedHeaderDataBufferTypeH264(vlVaContext *context, vlVaBuffer *buf)
{
   struct vl_vlc vlc = {0};
   uint8_t *data = buf->data;
   int nal_start = -1;
   unsigned nal_unit_type = 0, emulation_bytes_start = 0;
   bool is_slice = false;

   vl_vlc_init(&vlc, 1, (const void * const*)&data, &buf->size);

   while (vl_vlc_bits_left(&vlc) > 0) {
      /* search the first 64 bytes for a startcode */
      for (int i = 0; i < 64 && vl_vlc_bits_left(&vlc) >= 24; ++i) {
         if (vl_vlc_peekbits(&vlc, 24) == 0x000001)
            break;
         vl_vlc_eatbits(&vlc, 8);
         vl_vlc_fillbits(&vlc);
      }

      unsigned start = vlc.data - data - vl_vlc_valid_bits(&vlc) / 8;
      emulation_bytes_start = 4; /* 3 bytes startcode + 1 byte header */
      /* handle 4 bytes startcode */
      if (start > 0 && data[start - 1] == 0x00) {
         start--;
         emulation_bytes_start++;
      }
      if (nal_start >= 0) {
         vlVaAddRawHeader(&context->desc.h264enc.raw_headers, nal_unit_type,
                          start - nal_start, data + nal_start, is_slice, 0);
      }
      nal_start = start;
      is_slice = false;

      vl_vlc_eatbits(&vlc, 24); /* eat the startcode */

      if (vl_vlc_valid_bits(&vlc) < 15)
         vl_vlc_fillbits(&vlc);

      vl_vlc_eatbits(&vlc, 1);
      unsigned nal_ref_idc = vl_vlc_get_uimsbf(&vlc, 2);
      nal_unit_type = vl_vlc_get_uimsbf(&vlc, 5);

      struct vl_rbsp rbsp;
      vl_rbsp_init(&rbsp, &vlc, ~0, context->packed_header_emulation_bytes);

      switch (nal_unit_type) {
      case PIPE_H264_NAL_SLICE:
      case PIPE_H264_NAL_IDR_SLICE:
         is_slice = true;
         parseEncSliceParamsH264(context, &rbsp, nal_ref_idc, nal_unit_type);
         break;
      case PIPE_H264_NAL_SPS:
         parseEncSpsParamsH264(context, &rbsp);
         break;
      case PIPE_H264_NAL_PPS:
         parseEncPpsParamsH264(context, &rbsp);
         break;
      case PIPE_H264_NAL_PREFIX:
         parseEncPrefixH264(context, &rbsp);
         break;
      default:
         break;
      }

      if (!context->packed_header_emulation_bytes)
         break;
   }

   if (nal_start >= 0) {
      vlVaAddRawHeader(&context->desc.h264enc.raw_headers, nal_unit_type,
                       buf->size - nal_start, data + nal_start, is_slice,
                       context->packed_header_emulation_bytes ? 0 : emulation_bytes_start);
   }

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncMiscParameterTypeTemporalLayerH264(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAEncMiscParameterTemporalLayerStructure *tl = (VAEncMiscParameterTemporalLayerStructure *)misc->data;

   context->desc.h264enc.seq.num_temporal_layers = tl->number_of_layers;

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncMiscParameterTypeQualityLevelH264(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAEncMiscParameterBufferQualityLevel *ql = (VAEncMiscParameterBufferQualityLevel *)misc->data;
   vlVaHandleVAEncMiscParameterTypeQualityLevel(&context->desc.h264enc.quality_modes,
                               (vlVaQualityBits *)&ql->quality_level);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncMiscParameterTypeMaxFrameSizeH264(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAEncMiscParameterBufferMaxFrameSize *ms = (VAEncMiscParameterBufferMaxFrameSize *)misc->data;
   context->desc.h264enc.rate_ctrl[0].max_au_size = ms->max_frame_size;
   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncMiscParameterTypeHRDH264(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAEncMiscParameterHRD *ms = (VAEncMiscParameterHRD *)misc->data;

   if (ms->buffer_size == 0)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   /* Distinguishes from the default params set for these values in other
      functions and app specific params passed down via HRD buffer */
   context->desc.h264enc.rate_ctrl[0].app_requested_hrd_buffer = true;
   context->desc.h264enc.rate_ctrl[0].vbv_buffer_size = ms->buffer_size;
   context->desc.h264enc.rate_ctrl[0].vbv_buf_lv = (ms->initial_buffer_fullness << 6) / ms->buffer_size;
   context->desc.h264enc.rate_ctrl[0].vbv_buf_initial_size = ms->initial_buffer_fullness;

   for (unsigned i = 1; i < context->desc.h264enc.seq.num_temporal_layers; i++) {
      context->desc.h264enc.rate_ctrl[i].vbv_buffer_size =
         (float)ms->buffer_size / context->desc.h264enc.rate_ctrl[0].peak_bitrate *
         context->desc.h264enc.rate_ctrl[i].peak_bitrate;
      context->desc.h264enc.rate_ctrl[i].vbv_buf_lv = context->desc.h264enc.rate_ctrl[0].vbv_buf_lv;
      context->desc.h264enc.rate_ctrl[i].vbv_buf_initial_size =
         (context->desc.h264enc.rate_ctrl[i].vbv_buffer_size * context->desc.h264enc.rate_ctrl[i].vbv_buf_lv) >> 6;
   }

   return VA_STATUS_SUCCESS;
}
