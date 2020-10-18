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
   context->desc.h264enc.frame_num = h264->frame_num;
   context->desc.h264enc.not_referenced = false;
   context->desc.h264enc.pic_order_cnt = h264->CurrPic.TopFieldOrderCnt;
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

   _mesa_hash_table_insert(context->desc.h264enc.frame_idx,
		       UINT_TO_PTR(h264->CurrPic.picture_id),
		       UINT_TO_PTR(h264->frame_num));

   if (h264->pic_fields.bits.idr_pic_flag == 1)
      context->desc.h264enc.picture_type = PIPE_H264_ENC_PICTURE_TYPE_IDR;
   else
      context->desc.h264enc.picture_type = PIPE_H264_ENC_PICTURE_TYPE_P;

   context->desc.h264enc.quant_i_frames = h264->pic_init_qp;
   context->desc.h264enc.quant_b_frames = h264->pic_init_qp;
   context->desc.h264enc.quant_p_frames = h264->pic_init_qp;
   context->desc.h264enc.gop_cnt++;
   if (context->desc.h264enc.gop_cnt == context->desc.h264enc.gop_size)
      context->desc.h264enc.gop_cnt = 0;

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncSliceParameterBufferTypeH264(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAEncSliceParameterBufferH264 *h264;

   h264 = buf->data;
   context->desc.h264enc.ref_idx_l0 = VA_INVALID_ID;
   context->desc.h264enc.ref_idx_l1 = VA_INVALID_ID;

   for (int i = 0; i < 32; i++) {
      if (h264->RefPicList0[i].picture_id != VA_INVALID_ID) {
         if (context->desc.h264enc.ref_idx_l0 == VA_INVALID_ID)
            context->desc.h264enc.ref_idx_l0 = PTR_TO_UINT(util_hash_table_get(context->desc.h264enc.frame_idx,
									       UINT_TO_PTR(h264->RefPicList0[i].picture_id)));
      }
      if (h264->RefPicList1[i].picture_id != VA_INVALID_ID && h264->slice_type == 1) {
         if (context->desc.h264enc.ref_idx_l1 == VA_INVALID_ID)
            context->desc.h264enc.ref_idx_l1 = PTR_TO_UINT(util_hash_table_get(context->desc.h264enc.frame_idx,
									       UINT_TO_PTR(h264->RefPicList1[i].picture_id)));
      }
   }

   if (h264->slice_type == 1)
      context->desc.h264enc.picture_type = PIPE_H264_ENC_PICTURE_TYPE_B;
   else if (h264->slice_type == 0)
      context->desc.h264enc.picture_type = PIPE_H264_ENC_PICTURE_TYPE_P;
   else if (h264->slice_type == 2) {
      if (context->desc.h264enc.picture_type == PIPE_H264_ENC_PICTURE_TYPE_IDR)
         context->desc.h264enc.idr_pic_id++;
	   else
         context->desc.h264enc.picture_type = PIPE_H264_ENC_PICTURE_TYPE_I;
   } else
      context->desc.h264enc.picture_type = PIPE_H264_ENC_PICTURE_TYPE_SKIP;

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
   }

   context->gop_coeff = ((1024 + h264->intra_idr_period - 1) / h264->intra_idr_period + 1) / 2 * 2;
   if (context->gop_coeff > VL_VA_ENC_GOP_COEFF)
      context->gop_coeff = VL_VA_ENC_GOP_COEFF;
   context->desc.h264enc.gop_size = h264->intra_idr_period * context->gop_coeff;
   context->desc.h264enc.rate_ctrl.frame_rate_num = h264->time_scale / 2;
   context->desc.h264enc.rate_ctrl.frame_rate_den = h264->num_units_in_tick;
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
   VAEncMiscParameterRateControl *rc = (VAEncMiscParameterRateControl *)misc->data;
   if (context->desc.h264enc.rate_ctrl.rate_ctrl_method ==
         PIPE_H264_ENC_RATE_CONTROL_METHOD_CONSTANT)
      context->desc.h264enc.rate_ctrl.target_bitrate = rc->bits_per_second;
   else
      context->desc.h264enc.rate_ctrl.target_bitrate = rc->bits_per_second * (rc->target_percentage / 100.0);
   context->desc.h264enc.rate_ctrl.peak_bitrate = rc->bits_per_second;
   if (context->desc.h264enc.rate_ctrl.target_bitrate < 2000000)
      context->desc.h264enc.rate_ctrl.vbv_buffer_size = MIN2((context->desc.h264enc.rate_ctrl.target_bitrate * 2.75), 2000000);
   else
      context->desc.h264enc.rate_ctrl.vbv_buffer_size = context->desc.h264enc.rate_ctrl.target_bitrate;

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncMiscParameterTypeFrameRateH264(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAEncMiscParameterFrameRate *fr = (VAEncMiscParameterFrameRate *)misc->data;
   if (fr->framerate & 0xffff0000) {
      context->desc.h264enc.rate_ctrl.frame_rate_num = fr->framerate       & 0xffff;
      context->desc.h264enc.rate_ctrl.frame_rate_den = fr->framerate >> 16 & 0xffff;
   } else {
      context->desc.h264enc.rate_ctrl.frame_rate_num = fr->framerate;
      context->desc.h264enc.rate_ctrl.frame_rate_den = 1;
   }
   return VA_STATUS_SUCCESS;
}

void getEncParamPresetH264(vlVaContext *context)
{
   //motion estimation preset
   context->desc.h264enc.motion_est.motion_est_quarter_pixel = 0x00000001;
   context->desc.h264enc.motion_est.lsmvert = 0x00000002;
   context->desc.h264enc.motion_est.enc_disable_sub_mode = 0x00000078;
   context->desc.h264enc.motion_est.enc_en_ime_overw_dis_subm = 0x00000001;
   context->desc.h264enc.motion_est.enc_ime_overw_dis_subm_no = 0x00000001;
   context->desc.h264enc.motion_est.enc_ime2_search_range_x = 0x00000004;
   context->desc.h264enc.motion_est.enc_ime2_search_range_y = 0x00000004;

   //pic control preset
   context->desc.h264enc.pic_ctrl.enc_cabac_enable = 0x00000001;
   context->desc.h264enc.pic_ctrl.enc_constraint_set_flags = 0x00000040;

   //rate control
   context->desc.h264enc.rate_ctrl.vbv_buffer_size = 20000000;
   context->desc.h264enc.rate_ctrl.vbv_buf_lv = 48;
   context->desc.h264enc.rate_ctrl.fill_data_enable = 1;
   context->desc.h264enc.rate_ctrl.enforce_hrd = 1;
   context->desc.h264enc.enable_vui = false;
   if (context->desc.h264enc.rate_ctrl.frame_rate_num == 0 ||
       context->desc.h264enc.rate_ctrl.frame_rate_den == 0) {
         context->desc.h264enc.rate_ctrl.frame_rate_num = 30;
         context->desc.h264enc.rate_ctrl.frame_rate_den = 1;
   }
   context->desc.h264enc.rate_ctrl.target_bits_picture =
      context->desc.h264enc.rate_ctrl.target_bitrate *
      ((float)context->desc.h264enc.rate_ctrl.frame_rate_den /
      context->desc.h264enc.rate_ctrl.frame_rate_num);
   context->desc.h264enc.rate_ctrl.peak_bits_picture_integer =
      context->desc.h264enc.rate_ctrl.peak_bitrate *
      ((float)context->desc.h264enc.rate_ctrl.frame_rate_den /
      context->desc.h264enc.rate_ctrl.frame_rate_num);

   context->desc.h264enc.rate_ctrl.peak_bits_picture_fraction = 0;
   context->desc.h264enc.ref_pic_mode = 0x00000201;
}
