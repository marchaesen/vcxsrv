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

VAStatus
vlVaHandleVAEncPictureParameterBufferTypeH264(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAEncPictureParameterBufferH264 *h264;
   vlVaBuffer *coded_buf;

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

   context->desc.h264enc.p_remain = context->desc.h264enc.gop_size - context->desc.h264enc.gop_cnt - context->desc.h264enc.i_remain;

   coded_buf = handle_table_get(drv->htab, h264->coded_buf);
   if (!coded_buf->derived_surface.resource)
      coded_buf->derived_surface.resource = pipe_buffer_create(drv->pipe->screen, PIPE_BIND_VERTEX_BUFFER,
                                            PIPE_USAGE_STREAM, coded_buf->size);
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

   context->desc.h264enc.quant_i_frames = h264->pic_init_qp;
   context->desc.h264enc.quant_b_frames = h264->pic_init_qp;
   context->desc.h264enc.quant_p_frames = h264->pic_init_qp;
   context->desc.h264enc.gop_cnt++;
   if (context->desc.h264enc.gop_cnt == context->desc.h264enc.gop_size)
      context->desc.h264enc.gop_cnt = 0;

   context->desc.h264enc.pic_ctrl.enc_cabac_enable = h264->pic_fields.bits.entropy_coding_mode_flag;
   context->desc.h264enc.num_ref_idx_l0_active_minus1 = h264->num_ref_idx_l0_active_minus1;
   context->desc.h264enc.num_ref_idx_l1_active_minus1 = h264->num_ref_idx_l1_active_minus1;

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncSliceParameterBufferTypeH264(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAEncSliceParameterBufferH264 *h264;

   h264 = buf->data;
   memset(&context->desc.h264enc.ref_idx_l0_list, VA_INVALID_ID, sizeof(context->desc.h264enc.ref_idx_l0_list));
   memset(&context->desc.h264enc.ref_idx_l1_list, VA_INVALID_ID, sizeof(context->desc.h264enc.ref_idx_l1_list));

   if(h264->num_ref_idx_active_override_flag) {
      context->desc.h264enc.num_ref_idx_l0_active_minus1 = h264->num_ref_idx_l0_active_minus1;
      context->desc.h264enc.num_ref_idx_l1_active_minus1 = h264->num_ref_idx_l1_active_minus1;
   }

   for (int i = 0; i < 32; i++) {
      if (h264->RefPicList0[i].picture_id != VA_INVALID_ID) {
               context->desc.h264enc.ref_idx_l0_list[i] = PTR_TO_UINT(util_hash_table_get(context->desc.h264enc.frame_idx,
                                 UINT_TO_PTR(h264->RefPicList0[i].picture_id + 1)));
               context->desc.h264enc.l0_is_long_term[i] = h264->RefPicList0[i].flags &
		       					  VA_PICTURE_H264_LONG_TERM_REFERENCE;
      }
      if (h264->RefPicList1[i].picture_id != VA_INVALID_ID && h264->slice_type == 1) {
            context->desc.h264enc.ref_idx_l1_list[i] = PTR_TO_UINT(util_hash_table_get(context->desc.h264enc.frame_idx,
               			 UINT_TO_PTR(h264->RefPicList1[i].picture_id + 1)));
            context->desc.h264enc.l1_is_long_term[i] = h264->RefPicList1[i].flags &
		    				       VA_PICTURE_H264_LONG_TERM_REFERENCE;
      }
   }

   /**
    *  VAEncSliceParameterBufferH264.slice_type
    *  Slice type.
    *  Range: 0..2, 5..7, i.e. no switching slices.
   */
   struct h264_slice_descriptor slice_descriptor = { };
   slice_descriptor.macroblock_address = h264->macroblock_address;
   slice_descriptor.num_macroblocks = h264->num_macroblocks;

   if ((h264->slice_type == 1) || (h264->slice_type == 6)) {
      context->desc.h264enc.picture_type = PIPE_H2645_ENC_PICTURE_TYPE_B;
      slice_descriptor.slice_type = PIPE_H264_SLICE_TYPE_B;
   } else if ((h264->slice_type == 0) || (h264->slice_type == 5)) {
      context->desc.h264enc.picture_type = PIPE_H2645_ENC_PICTURE_TYPE_P;
      slice_descriptor.slice_type = PIPE_H264_SLICE_TYPE_P;
   } else if ((h264->slice_type == 2) || (h264->slice_type == 7)) {
      if (context->desc.h264enc.picture_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR) {
         if (slice_descriptor.macroblock_address == 0) {
            /* Increment it only for the first slice of the IDR frame */
            context->desc.h264enc.idr_pic_id++;
         }
         slice_descriptor.slice_type = PIPE_H264_SLICE_TYPE_I;
      } else {
         context->desc.h264enc.picture_type = PIPE_H2645_ENC_PICTURE_TYPE_I;
         slice_descriptor.slice_type = PIPE_H264_SLICE_TYPE_I;
      }
   } else {
      context->desc.h264enc.picture_type = PIPE_H2645_ENC_PICTURE_TYPE_SKIP;
   }

   context->desc.h264enc.pic_ctrl.enc_cabac_init_idc = h264->cabac_init_idc;

   /* Handle the slice control parameters */
   if (context->desc.h264enc.num_slice_descriptors < ARRAY_SIZE(context->desc.h264enc.slices_descriptors)) {
      context->desc.h264enc.slices_descriptors[context->desc.h264enc.num_slice_descriptors++] = slice_descriptor;
   } else {
      return VA_STATUS_ERROR_NOT_ENOUGH_BUFFER;
   }

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncSequenceParameterBufferTypeH264(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAEncSequenceParameterBufferH264 *h264 = (VAEncSequenceParameterBufferH264 *)buf->data;
   if (!context->decoder) {
      context->templat.max_references = h264->max_num_ref_frames;
      context->templat.level = h264->level_idc;
      context->decoder = drv->pipe->create_video_codec(drv->pipe, &context->templat);
      if (!context->decoder)
         return VA_STATUS_ERROR_ALLOCATION_FAILED;

      getEncParamPresetH264(context);
   }

   context->gop_coeff = ((1024 + h264->intra_idr_period - 1) / h264->intra_idr_period + 1) / 2 * 2;
   if (context->gop_coeff > VL_VA_ENC_GOP_COEFF)
      context->gop_coeff = VL_VA_ENC_GOP_COEFF;
   context->desc.h264enc.gop_size = h264->intra_idr_period * context->gop_coeff;
   context->desc.h264enc.rate_ctrl[0].frame_rate_num = h264->time_scale / 2;
   context->desc.h264enc.rate_ctrl[0].frame_rate_den = h264->num_units_in_tick;
   context->desc.h264enc.pic_order_cnt_type = h264->seq_fields.bits.pic_order_cnt_type;

   if (h264->frame_cropping_flag) {
      context->desc.h264enc.pic_ctrl.enc_frame_cropping_flag = h264->frame_cropping_flag;
      context->desc.h264enc.pic_ctrl.enc_frame_crop_left_offset = h264->frame_crop_left_offset;
      context->desc.h264enc.pic_ctrl.enc_frame_crop_right_offset = h264->frame_crop_right_offset;
      context->desc.h264enc.pic_ctrl.enc_frame_crop_top_offset = h264->frame_crop_top_offset;
      context->desc.h264enc.pic_ctrl.enc_frame_crop_bottom_offset = h264->frame_crop_bottom_offset;
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

   if (context->desc.h264enc.num_temporal_layers > 0 &&
       temporal_id >= context->desc.h264enc.num_temporal_layers)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   context->desc.h264enc.rate_ctrl[temporal_id].fill_data_enable = !(rc->rc_flags.bits.disable_bit_stuffing);
   context->desc.h264enc.rate_ctrl[temporal_id].skip_frame_enable = !(rc->rc_flags.bits.disable_frame_skip);
   context->desc.h264enc.rate_ctrl[temporal_id].peak_bitrate = rc->bits_per_second;
   if (context->desc.h264enc.rate_ctrl[temporal_id].target_bitrate < 2000000)
       context->desc.h264enc.rate_ctrl[temporal_id].vbv_buffer_size =
         MIN2((context->desc.h264enc.rate_ctrl[0].target_bitrate * 2.75), 2000000);
   else
      context->desc.h264enc.rate_ctrl[temporal_id].vbv_buffer_size =
         context->desc.h264enc.rate_ctrl[0].target_bitrate;

   context->desc.h264enc.rate_ctrl[temporal_id].max_qp = rc->max_qp;
   context->desc.h264enc.rate_ctrl[temporal_id].min_qp = rc->min_qp;

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

   if (context->desc.h264enc.num_temporal_layers > 0 &&
       temporal_id >= context->desc.h264enc.num_temporal_layers)
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

VAStatus
vlVaHandleVAEncMiscParameterTypeTemporalLayerH264(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAEncMiscParameterTemporalLayerStructure *tl = (VAEncMiscParameterTemporalLayerStructure *)misc->data;

   context->desc.h264enc.num_temporal_layers = tl->number_of_layers;

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

   if (ms->buffer_size) {
      context->desc.h264enc.rate_ctrl[0].vbv_buffer_size = ms->buffer_size;
      context->desc.h264enc.rate_ctrl[0].vbv_buf_lv = (ms->initial_buffer_fullness << 6 ) / ms->buffer_size;
   }

   return VA_STATUS_SUCCESS;
}

void getEncParamPresetH264(vlVaContext *context)
{
   //rate control
   context->desc.h264enc.rate_ctrl[0].vbv_buffer_size = 20000000;
   context->desc.h264enc.rate_ctrl[0].vbv_buf_lv = 48;
   context->desc.h264enc.rate_ctrl[0].fill_data_enable = 1;
   context->desc.h264enc.rate_ctrl[0].enforce_hrd = 1;
   context->desc.h264enc.rate_ctrl[0].max_qp = 51;
   context->desc.h264enc.rate_ctrl[0].min_qp = 0;
   context->desc.h264enc.enable_vui = false;
   if (context->desc.h264enc.rate_ctrl[0].frame_rate_num == 0 ||
       context->desc.h264enc.rate_ctrl[0].frame_rate_den == 0) {
         context->desc.h264enc.rate_ctrl[0].frame_rate_num = 30;
         context->desc.h264enc.rate_ctrl[0].frame_rate_den = 1;
   }
   context->desc.h264enc.rate_ctrl[0].target_bits_picture =
      context->desc.h264enc.rate_ctrl[0].target_bitrate *
      ((float)context->desc.h264enc.rate_ctrl[0].frame_rate_den /
      context->desc.h264enc.rate_ctrl[0].frame_rate_num);
   context->desc.h264enc.rate_ctrl[0].peak_bits_picture_integer =
      context->desc.h264enc.rate_ctrl[0].peak_bitrate *
      ((float)context->desc.h264enc.rate_ctrl[0].frame_rate_den /
      context->desc.h264enc.rate_ctrl[0].frame_rate_num);

   context->desc.h264enc.rate_ctrl[0].peak_bits_picture_fraction = 0;
}
