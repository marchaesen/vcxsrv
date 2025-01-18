/**************************************************************************
 *
 * Copyright 2023 Advanced Micro Devices, Inc.
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
#include "util/vl_vlc.h"

#if VA_CHECK_VERSION(1, 16, 0)

#define AV1_SELECT_SCREEN_CONTENT_TOOLS (2)
#define AV1_SELECT_INTEGER_MV           (2)
#define AV1_MAXNUM_OPERATING_POINT      (32)
#define AV1_SUPERRES_DENOM_BITS  (8)
#define AV1_MAXNUM_REF_FRAMES    (8)
#define AV1_REFS_PER_FRAME       (7)
#define FRAME_TYPE_KEY_FRAME     (0)
#define FRAME_TYPE_INTER_FRAME   (1)
#define FRAME_TYPE_INTRA_ONLY    (2)
#define FRAME_TYPE_SWITCH        (3)
#define OBU_TYPE_SEQUENCE_HEADER (1)
#define OBU_TYPE_FRAME_HEADER    (3)
#define OBU_TYPE_META            (5)
#define OBU_TYPE_FRAME           (6)
#define METADATA_TYPE_HDR_CLL    (1)
#define METADATA_TYPE_HDR_MDCV   (2)
#define METADATA_TYPE_ITU_T35    (4)
#define METADATA_TYPE_TIMECODE   (5)
#define AV1_MIN_QP_DEFAULT (1)
#define AV1_MAX_QP_DEFAULT (255)

static unsigned av1_f(struct vl_vlc *vlc, unsigned n)
{
   unsigned valid = vl_vlc_valid_bits(vlc);

   if (n == 0)
      return 0;

   if (valid < 32)
      vl_vlc_fillbits(vlc);

   return vl_vlc_get_uimsbf(vlc, n);
}

static unsigned av1_uvlc(struct vl_vlc *vlc)
{
   unsigned value;
   unsigned leadingZeros = 0;

   while (1) {
      bool done = av1_f(vlc, 1);
      if (done)
         break;
      leadingZeros++;
   }

   if (leadingZeros >= 32)
      return 0xffffffff;

   value = av1_f(vlc, leadingZeros);

   return value + (1 << leadingZeros) - 1;
}

static unsigned av1_uleb128(struct vl_vlc *vlc)
{
   uint64_t value = 0;
   unsigned leb128Bytes = 0;
   unsigned i;

   for (i = 0; i < 8; ++i) {
      leb128Bytes = av1_f(vlc, 8);
      value |= ((uint64_t)(leb128Bytes & 0x7f) << (i * 7));
      if (!(leb128Bytes & 0x80))
         break;
   }

   assert(value <= UINT32_MAX);
   return value;
}

VAStatus vlVaHandleVAEncSequenceParameterBufferTypeAV1(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAEncSequenceParameterBufferAV1 *av1 = buf->data;

   context->desc.av1enc.seq.tier = av1->seq_tier;
   context->desc.av1enc.seq.level = av1->seq_level_idx;
   context->desc.av1enc.seq.intra_period = av1->intra_period;
   context->desc.av1enc.seq.ip_period = av1->ip_period;
   context->desc.av1enc.seq.bit_depth_minus8 = av1->seq_fields.bits.bit_depth_minus8;
   context->desc.av1enc.seq.seq_bits.enable_cdef = av1->seq_fields.bits.enable_cdef;
   context->desc.av1enc.seq.seq_bits.enable_order_hint = av1->seq_fields.bits.enable_order_hint;

   for (int i = 0; i < ARRAY_SIZE(context->desc.av1enc.rc); i++)
      context->desc.av1enc.rc[i].peak_bitrate = av1->bits_per_second;

   return VA_STATUS_SUCCESS;
}

static uint8_t
vlVaDpbIndex(vlVaContext *context, VASurfaceID id)
{
   for (uint8_t i = 0; i < context->desc.av1enc.dpb_size; i++) {
      if (context->desc.av1enc.dpb[i].id == id)
         return i;
   }
   return PIPE_H2645_LIST_REF_INVALID_ENTRY;
}

VAStatus vlVaHandleVAEncPictureParameterBufferTypeAV1(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAEncPictureParameterBufferAV1 *av1 = buf->data;
   vlVaBuffer *coded_buf;
   vlVaSurface *surf;
   int i, j;

   context->desc.av1enc.disable_frame_end_update_cdf = av1->picture_flags.bits.disable_frame_end_update_cdf;
   context->desc.av1enc.error_resilient_mode = av1->picture_flags.bits.error_resilient_mode;
   context->desc.av1enc.disable_cdf_update = av1->picture_flags.bits.disable_cdf_update;
   context->desc.av1enc.enable_frame_obu = av1->picture_flags.bits.enable_frame_obu;
   context->desc.av1enc.allow_high_precision_mv = av1->picture_flags.bits.allow_high_precision_mv;
   context->desc.av1enc.palette_mode_enable = av1->picture_flags.bits.palette_mode_enable;
   context->desc.av1enc.long_term_reference = av1->picture_flags.bits.long_term_reference;
   context->desc.av1enc.tile_rows = av1->tile_rows;
   context->desc.av1enc.tile_cols = av1->tile_cols;
   context->desc.av1enc.context_update_tile_id = av1->context_update_tile_id;
   context->desc.av1enc.use_superres = av1->picture_flags.bits.use_superres;
   context->desc.av1enc.reduced_tx_set = av1->picture_flags.bits.reduced_tx_set;
   context->desc.av1enc.skip_mode_present = av1->mode_control_flags.bits.skip_mode_present;
   context->desc.av1enc.tx_mode = av1->mode_control_flags.bits.tx_mode;
   context->desc.av1enc.compound_reference_mode = av1->mode_control_flags.bits.reference_mode;
   context->desc.av1enc.superres_scale_denominator = av1->superres_scale_denominator;
   context->desc.av1enc.interpolation_filter = av1->interpolation_filter;

   /* The last tile column or row size needs to be derived. */
    for (uint8_t i = 0 ; i < ARRAY_SIZE(av1->width_in_sbs_minus_1); i++)
        context->desc.av1enc.width_in_sbs_minus_1[i] = av1->width_in_sbs_minus_1[i];

    /* The last tile column or row size needs to be derived. */
    for (uint8_t i = 0 ; i < ARRAY_SIZE(av1->height_in_sbs_minus_1); i++)
        context->desc.av1enc.height_in_sbs_minus_1[i] = av1->height_in_sbs_minus_1[i];

   context->desc.av1enc.cdef.cdef_damping_minus_3 = av1->cdef_damping_minus_3;
   context->desc.av1enc.cdef.cdef_bits = av1->cdef_bits;

   for (uint8_t i = 0 ; i < ARRAY_SIZE(av1->cdef_y_strengths); i++)
      context->desc.av1enc.cdef.cdef_y_strengths[i] = av1->cdef_y_strengths[i];

   for (uint8_t i = 0 ; i < ARRAY_SIZE(av1->cdef_uv_strengths); i++)
      context->desc.av1enc.cdef.cdef_uv_strengths[i] = av1->cdef_uv_strengths[i];

   context->desc.av1enc.loop_filter.filter_level[0] = av1->filter_level[0];
   context->desc.av1enc.loop_filter.filter_level[1] = av1->filter_level[1];
   context->desc.av1enc.loop_filter.filter_level_u = av1->filter_level_u;
   context->desc.av1enc.loop_filter.filter_level_v = av1->filter_level_v;
   context->desc.av1enc.loop_filter.sharpness_level = av1->loop_filter_flags.bits.sharpness_level;
   context->desc.av1enc.loop_filter.mode_ref_delta_enabled = av1->loop_filter_flags.bits.mode_ref_delta_enabled;
   context->desc.av1enc.loop_filter.mode_ref_delta_update = av1->loop_filter_flags.bits.mode_ref_delta_update;
   context->desc.av1enc.loop_filter.delta_lf_present = av1->mode_control_flags.bits.delta_lf_present;
   context->desc.av1enc.loop_filter.delta_lf_res = av1->mode_control_flags.bits.delta_lf_res;
   context->desc.av1enc.loop_filter.delta_lf_multi = av1->mode_control_flags.bits.delta_lf_multi;

   context->desc.av1enc.restoration.yframe_restoration_type = av1->loop_restoration_flags.bits.yframe_restoration_type;
   context->desc.av1enc.restoration.cbframe_restoration_type = av1->loop_restoration_flags.bits.cbframe_restoration_type;
   context->desc.av1enc.restoration.crframe_restoration_type = av1->loop_restoration_flags.bits.crframe_restoration_type;
   context->desc.av1enc.restoration.lr_unit_shift = av1->loop_restoration_flags.bits.lr_unit_shift;
   context->desc.av1enc.restoration.lr_uv_shift = av1->loop_restoration_flags.bits.lr_uv_shift;
   context->desc.av1enc.quantization.base_qindex = av1->base_qindex;
   context->desc.av1enc.quantization.y_dc_delta_q = av1->y_dc_delta_q;
   context->desc.av1enc.quantization.u_dc_delta_q = av1->u_dc_delta_q;
   context->desc.av1enc.quantization.u_ac_delta_q = av1->u_ac_delta_q;
   context->desc.av1enc.quantization.v_dc_delta_q = av1->v_dc_delta_q;
   context->desc.av1enc.quantization.v_ac_delta_q = av1->v_ac_delta_q;
   context->desc.av1enc.quantization.min_base_qindex = av1->min_base_qindex;
   context->desc.av1enc.quantization.max_base_qindex = av1->max_base_qindex;
   context->desc.av1enc.quantization.using_qmatrix = av1->qmatrix_flags.bits.using_qmatrix;
   context->desc.av1enc.quantization.qm_y = av1->qmatrix_flags.bits.qm_y;
   context->desc.av1enc.quantization.qm_u = av1->qmatrix_flags.bits.qm_u;
   context->desc.av1enc.quantization.qm_v = av1->qmatrix_flags.bits.qm_v;
   context->desc.av1enc.quantization.delta_q_present = av1->mode_control_flags.bits.delta_q_present;
   context->desc.av1enc.quantization.delta_q_res = av1->mode_control_flags.bits.delta_q_res;

   /* VAEncWarpedMotionParamsAV1    wm[7]; */

   context->desc.av1enc.tg_obu_header.obu_extension_flag = av1->tile_group_obu_hdr_info.bits.obu_extension_flag;
   context->desc.av1enc.tg_obu_header.obu_has_size_field = av1->tile_group_obu_hdr_info.bits.obu_has_size_field;
   context->desc.av1enc.tg_obu_header.obu_has_size_field = av1->tile_group_obu_hdr_info.bits.obu_has_size_field;
   context->desc.av1enc.tg_obu_header.temporal_id = av1->tile_group_obu_hdr_info.bits.temporal_id;
   context->desc.av1enc.tg_obu_header.spatial_id = av1->tile_group_obu_hdr_info.bits.spatial_id;

   /* Evict unused surfaces */
   for (i = 0; i < context->desc.av1enc.dpb_size; i++) {
      struct pipe_av1_enc_dpb_entry *dpb = &context->desc.av1enc.dpb[i];
      if (!dpb->id || dpb->id == av1->reconstructed_frame)
         continue;
      for (j = 0; j < ARRAY_SIZE(av1->reference_frames); j++) {
         if (av1->reference_frames[j] == dpb->id)
            break;
      }
      if (j == ARRAY_SIZE(av1->reference_frames)) {
         surf = handle_table_get(drv->htab, dpb->id);
         assert(surf);
         surf->is_dpb = false;
         surf->buffer = NULL;
         /* Keep the buffer for reuse later */
         dpb->id = 0;
      }
   }

   surf = handle_table_get(drv->htab, av1->reconstructed_frame);
   if (!surf)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   for (i = 0; i < ARRAY_SIZE(context->desc.av1enc.dpb); i++) {
      if (context->desc.av1enc.dpb[i].id == av1->reconstructed_frame) {
         assert(surf->is_dpb);
         break;
      }
      if (!surf->is_dpb && !context->desc.av1enc.dpb[i].id) {
         surf->is_dpb = true;
         if (surf->buffer) {
            surf->buffer->destroy(surf->buffer);
            surf->buffer = NULL;
         }
         if (context->decoder->create_dpb_buffer) {
            struct pipe_video_buffer *buffer = context->desc.av1enc.dpb[i].buffer;
            if (!buffer) {
               /* Find unused buffer */
               for (j = 0; j < context->desc.av1enc.dpb_size; j++) {
                  struct pipe_av1_enc_dpb_entry *dpb = &context->desc.av1enc.dpb[j];
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
         if (i == context->desc.av1enc.dpb_size)
            context->desc.av1enc.dpb_size++;
         break;
      }
   }
   if (i == ARRAY_SIZE(context->desc.av1enc.dpb))
      return VA_STATUS_ERROR_INVALID_PARAMETER;
   context->desc.av1enc.dpb_curr_pic = i;
   context->desc.av1enc.dpb[i].id = av1->reconstructed_frame;
   context->desc.av1enc.dpb[i].order_hint = av1->order_hint;
   context->desc.av1enc.dpb[i].buffer = surf->buffer;

   for (i = 0; i < ARRAY_SIZE(av1->ref_frame_idx); i++) {
      if (av1->ref_frame_idx[i] > 7)
         context->desc.av1enc.dpb_ref_frame_idx[i] = PIPE_H2645_LIST_REF_INVALID_ENTRY;
      else
         context->desc.av1enc.dpb_ref_frame_idx[i] =
            vlVaDpbIndex(context, av1->reference_frames[av1->ref_frame_idx[i]]);
   }

   for (i = 0; i < PIPE_AV1_REFS_PER_FRAME; i++) {
      uint8_t l0 = (av1->ref_frame_ctrl_l0.value >> (3 * i)) & 0x7;
      uint8_t l1 = (av1->ref_frame_ctrl_l1.value >> (3 * i)) & 0x7;
      context->desc.av1enc.ref_list0[i] = l0 ? l0 - 1 : PIPE_H2645_LIST_REF_INVALID_ENTRY;
      context->desc.av1enc.ref_list1[i] = l1 ? l1 - 1 : PIPE_H2645_LIST_REF_INVALID_ENTRY;
      if ((l0 && context->desc.av1enc.dpb_ref_frame_idx[l0 - 1] == PIPE_H2645_LIST_REF_INVALID_ENTRY) ||
          (l1 && context->desc.av1enc.dpb_ref_frame_idx[l1 - 1] == PIPE_H2645_LIST_REF_INVALID_ENTRY))
         return VA_STATUS_ERROR_INVALID_PARAMETER;
   }

   coded_buf = handle_table_get(drv->htab, av1->coded_buf);
   if (!coded_buf)
      return VA_STATUS_ERROR_INVALID_BUFFER;

   if (!coded_buf->derived_surface.resource)
      coded_buf->derived_surface.resource = pipe_buffer_create(drv->pipe->screen, PIPE_BIND_VERTEX_BUFFER,
                                            PIPE_USAGE_STAGING, coded_buf->size);
   context->coded_buf = coded_buf;

   /* these frame types will need to be seen as force type */
   switch(av1->picture_flags.bits.frame_type)
   {
      case 0:
         context->desc.av1enc.frame_type = PIPE_AV1_ENC_FRAME_TYPE_KEY;
         break;
      case 1:
         context->desc.av1enc.frame_type = PIPE_AV1_ENC_FRAME_TYPE_INTER;
         break;
      case 2:
         context->desc.av1enc.frame_type = PIPE_AV1_ENC_FRAME_TYPE_INTRA_ONLY;
         break;
      case 3:
         context->desc.av1enc.frame_type = PIPE_AV1_ENC_FRAME_TYPE_SWITCH;
         break;
   };

   for (i = 0; i < ARRAY_SIZE(context->desc.av1enc.rc); i++) {
      unsigned qindex = av1->base_qindex ? av1->base_qindex : 60;
      if (context->desc.av1enc.frame_type == PIPE_AV1_ENC_FRAME_TYPE_KEY ||
          context->desc.av1enc.frame_type == PIPE_AV1_ENC_FRAME_TYPE_INTRA_ONLY)
         context->desc.av1enc.rc[i].qp = qindex;
      else
         context->desc.av1enc.rc[i].qp_inter = qindex;
      /* Distinguishes from the default params set for these values and app specific params passed down */
      context->desc.av1enc.rc[i].app_requested_initial_qp = (av1->base_qindex != 0);
      context->desc.av1enc.rc[i].min_qp = av1->min_base_qindex ? av1->min_base_qindex : 1;
      context->desc.av1enc.rc[i].max_qp = av1->max_base_qindex ? av1->max_base_qindex : 255;
      /* Distinguishes from the default params set for these values and app specific params passed down */
      context->desc.av1enc.rc[i].app_requested_qp_range =
         ((context->desc.av1enc.rc[i].max_qp != AV1_MAX_QP_DEFAULT) || (context->desc.av1enc.rc[i].min_qp != AV1_MIN_QP_DEFAULT));
   }

   if (context->desc.av1enc.frame_type == FRAME_TYPE_KEY_FRAME)
      context->desc.av1enc.last_key_frame_num = context->desc.av1enc.frame_num;

    /* Initialize slice descriptors for this picture */
    context->desc.av1enc.num_tile_groups = 0;
    memset(&context->desc.av1enc.tile_groups, 0, sizeof(context->desc.av1enc.tile_groups));

   return VA_STATUS_SUCCESS;
}

VAStatus vlVaHandleVAEncMiscParameterTypeRateControlAV1(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   unsigned temporal_id;
   VAEncMiscParameterRateControl *rc = (VAEncMiscParameterRateControl *)misc->data;
   struct pipe_av1_enc_rate_control *pipe_rc = NULL;

   temporal_id = context->desc.av1enc.rc[0].rate_ctrl_method !=
                 PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE ?
                 rc->rc_flags.bits.temporal_id :
                 0;

   if (context->desc.av1enc.seq.num_temporal_layers > 0 &&
       temporal_id >= context->desc.av1enc.seq.num_temporal_layers)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   pipe_rc = &context->desc.av1enc.rc[temporal_id];

   if (pipe_rc->rate_ctrl_method == PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT)
      pipe_rc->target_bitrate = rc->bits_per_second;
   else
      pipe_rc->target_bitrate = rc->bits_per_second * (rc->target_percentage / 100.0);
   pipe_rc->peak_bitrate = rc->bits_per_second;
   if (pipe_rc->target_bitrate < 2000000)
      pipe_rc->vbv_buffer_size = MIN2((pipe_rc->target_bitrate * 2.75), 2000000);
   else
      pipe_rc->vbv_buffer_size = pipe_rc->target_bitrate;

   pipe_rc->fill_data_enable = !(rc->rc_flags.bits.disable_bit_stuffing);
   pipe_rc->skip_frame_enable = 0;/* !(rc->rc_flags.bits.disable_frame_skip); */
   pipe_rc->max_qp = rc->max_qp;
   pipe_rc->min_qp = rc->min_qp;
   /* Distinguishes from the default params set for these values in other
      functions and app specific params passed down */
   pipe_rc->app_requested_qp_range = ((rc->max_qp > 0) || (rc->min_qp > 0));

   if (pipe_rc->rate_ctrl_method == PIPE_H2645_ENC_RATE_CONTROL_METHOD_QUALITY_VARIABLE)
      pipe_rc->vbr_quality_factor = rc->quality_factor;

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncMiscParameterTypeQualityLevelAV1(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAEncMiscParameterBufferQualityLevel *ql = (VAEncMiscParameterBufferQualityLevel *)misc->data;
   vlVaHandleVAEncMiscParameterTypeQualityLevel(&context->desc.av1enc.quality_modes,
                               (vlVaQualityBits *)&ql->quality_level);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncMiscParameterTypeMaxFrameSizeAV1(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAEncMiscParameterBufferMaxFrameSize *ms = (VAEncMiscParameterBufferMaxFrameSize *)misc->data;
   context->desc.av1enc.rc[0].max_au_size = ms->max_frame_size;
   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncMiscParameterTypeHRDAV1(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAEncMiscParameterHRD *ms = (VAEncMiscParameterHRD *)misc->data;

   if (ms->buffer_size == 0)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   /* Distinguishes from the default params set for these values in other
      functions and app specific params passed down via HRD buffer */
   context->desc.av1enc.rc[0].app_requested_hrd_buffer = true;
   context->desc.av1enc.rc[0].vbv_buffer_size = ms->buffer_size;
   context->desc.av1enc.rc[0].vbv_buf_lv = (ms->initial_buffer_fullness << 6) / ms->buffer_size;
   context->desc.av1enc.rc[0].vbv_buf_initial_size = ms->initial_buffer_fullness;

   for (unsigned i = 1; i < context->desc.av1enc.seq.num_temporal_layers; i++) {
      context->desc.av1enc.rc[i].vbv_buffer_size =
         (float)ms->buffer_size / context->desc.av1enc.rc[0].peak_bitrate *
         context->desc.av1enc.rc[i].peak_bitrate;
      context->desc.av1enc.rc[i].vbv_buf_lv = context->desc.av1enc.rc[0].vbv_buf_lv;
      context->desc.av1enc.rc[i].vbv_buf_initial_size =
         (context->desc.av1enc.rc[i].vbv_buffer_size * context->desc.av1enc.rc[i].vbv_buf_lv) >> 6;
   }

   return VA_STATUS_SUCCESS;
}

static void av1_color_config(vlVaContext *context, struct vl_vlc *vlc)
{
   unsigned high_bitdepth = 0;
   unsigned bit_depth = 8;
   unsigned mono_chrome = 0;
   unsigned subsampling_x = 0, subsampling_y = 0;

   struct pipe_av1_enc_seq_param *seq = &context->desc.av1enc.seq;

   high_bitdepth = av1_f(vlc, 1);
   if (seq->profile == 2 && high_bitdepth) {
      unsigned twelve_bit = av1_f(vlc, 1);
      bit_depth = twelve_bit ? 12 : 10;
   } else if (seq->profile <= 2)
      bit_depth = high_bitdepth ? 10 : 8;

   context->desc.av1enc.seq.bit_depth_minus8 = bit_depth - 8;

   if (seq->profile == 1)
      mono_chrome = 0;
   else
      mono_chrome = av1_f(vlc, 1);

   seq->seq_bits.color_description_present_flag = av1_f(vlc, 1);
   if (seq->seq_bits.color_description_present_flag) {
      seq->color_config.color_primaries = av1_f(vlc, 8);
      seq->color_config.transfer_characteristics = av1_f(vlc, 8);
      seq->color_config.matrix_coefficients = av1_f(vlc, 8);
   } else {
      seq->color_config.color_primaries = 2;
      seq->color_config.transfer_characteristics = 2;
      seq->color_config.matrix_coefficients = 2;
   }

   if (mono_chrome) {
      seq->color_config.color_range = av1_f(vlc, 1);
      subsampling_x = subsampling_y = 1;
      seq->color_config.chroma_sample_position = 0;
      return;
   } else if (seq->color_config.color_primaries == 1 &&  /* CP_BT_709 */
              seq->color_config.transfer_characteristics == 13 && /* TC_SRGB */
              seq->color_config.matrix_coefficients == 0) { /* MC_IDENTITY */
      seq->color_config.color_range = 1;
      subsampling_x = subsampling_y = 0;
   } else {
      seq->color_config.color_range = av1_f(vlc, 1);
      if (seq->profile == 0)
         subsampling_x = subsampling_y = 1;
      else if (seq->profile == 1)
         subsampling_x = subsampling_y = 0;
      else {
         if (bit_depth == 12) {
            subsampling_x = av1_f(vlc, 1);
            if (subsampling_x)
               subsampling_y = av1_f(vlc, 1);
            else
               subsampling_y = 0;
         }
      }
      if (subsampling_x && subsampling_y)
         seq->color_config.chroma_sample_position = av1_f(vlc, 2);
   }

   av1_f(vlc, 1);
}

static void av1_sequence_header(vlVaContext *context, struct vl_vlc *vlc)
{
   struct pipe_av1_enc_seq_param *seq = &context->desc.av1enc.seq;

   seq->profile = av1_f(vlc, 3);
   seq->seq_bits.still_picture = av1_f(vlc, 1);
   seq->seq_bits.reduced_still_picture_header = av1_f(vlc, 1);
   if (seq->seq_bits.reduced_still_picture_header) {
      seq->seq_level_idx[0] = av1_f(vlc, 5);
   } else {
      seq->seq_bits.timing_info_present_flag = av1_f(vlc, 1);
      if (seq->seq_bits.timing_info_present_flag) {
         seq->num_units_in_display_tick = av1_f(vlc, 32);
         seq->time_scale = av1_f(vlc, 32);
         seq->seq_bits.equal_picture_interval = av1_f(vlc, 1);
         if (seq->seq_bits.equal_picture_interval)
            seq->num_tick_per_picture_minus1 = av1_uvlc(vlc);
         seq->seq_bits.decoder_model_info_present_flag = av1_f(vlc, 1);
         if (seq->seq_bits.decoder_model_info_present_flag) {
            struct pipe_av1_enc_decoder_model_info *info = &seq->decoder_model_info;
            info->buffer_delay_length_minus1 = av1_f(vlc, 5);
            info->num_units_in_decoding_tick = av1_f(vlc, 32);
            info->buffer_removal_time_length_minus1 = av1_f(vlc, 5);
            info->frame_presentation_time_length_minus1 = av1_f(vlc, 5);
         }
      }
      seq->seq_bits.initial_display_delay_present_flag = av1_f(vlc, 1);
      seq->num_temporal_layers = av1_f(vlc, 5) + 1;
      for (unsigned i = 0; i < seq->num_temporal_layers; i++) {
         seq->operating_point_idc[i] = av1_f(vlc, 12);
         seq->seq_level_idx[i] = av1_f(vlc, 5);
         if (seq->seq_level_idx[i] > 7)
            seq->seq_tier[i] = av1_f(vlc, 1);
         if (seq->seq_bits.decoder_model_info_present_flag) {
            seq->decoder_model_present_for_this_op[i] = av1_f(vlc, 1);
            if (seq->decoder_model_present_for_this_op[i]) {
               seq->decoder_buffer_delay[i] = av1_f(vlc, seq->decoder_model_info.buffer_delay_length_minus1 + 1);
               seq->encoder_buffer_delay[i] = av1_f(vlc, seq->decoder_model_info.buffer_delay_length_minus1 + 1);
               seq->low_delay_mode_flag[i] = av1_f(vlc, 1);
            } else
               seq->decoder_model_present_for_this_op[i] = 0;
         }
         if (seq->seq_bits.initial_display_delay_present_flag) {
            seq->initial_display_delay_present_for_this_op[i] = av1_f(vlc, 1);
            if (seq->initial_display_delay_present_for_this_op[i])
               seq->initial_display_delay_minus_1[i] = av1_f(vlc, 4);
         }
      }
   }
   seq->frame_width_bits_minus1 = av1_f(vlc, 4);
   seq->frame_height_bits_minus1 = av1_f(vlc, 4);
   seq->pic_width_in_luma_samples = av1_f(vlc, seq->frame_width_bits_minus1 + 1) + 1;
   seq->pic_height_in_luma_samples = av1_f(vlc, seq->frame_height_bits_minus1 + 1) + 1;
   if (!seq->seq_bits.reduced_still_picture_header)
      seq->seq_bits.frame_id_number_present_flag = av1_f(vlc, 1);
   if (seq->seq_bits.frame_id_number_present_flag) {
      seq->delta_frame_id_length = av1_f(vlc, 4) + 2;
      seq->additional_frame_id_length = av1_f(vlc, 3) + 1;
   }
   seq->seq_bits.use_128x128_superblock = av1_f(vlc, 1);
   seq->seq_bits.enable_filter_intra = av1_f(vlc, 1);
   seq->seq_bits.enable_intra_edge_filter = av1_f(vlc, 1);
   if (!seq->seq_bits.reduced_still_picture_header) {
      seq->seq_bits.enable_interintra_compound = av1_f(vlc, 1);
      seq->seq_bits.enable_masked_compound = av1_f(vlc, 1);
      seq->seq_bits.enable_warped_motion = av1_f(vlc, 1);
      seq->seq_bits.enable_dual_filter = av1_f(vlc, 1);
      seq->seq_bits.enable_order_hint = av1_f(vlc, 1);
      if (seq->seq_bits.enable_order_hint) {
         seq->seq_bits.enable_jnt_comp = av1_f(vlc, 1);
         seq->seq_bits.enable_ref_frame_mvs = av1_f(vlc, 1);
      } else
         seq->seq_bits.enable_ref_frame_mvs = 0;

      seq->seq_bits.disable_screen_content_tools = av1_f(vlc, 1);
      if (seq->seq_bits.disable_screen_content_tools)
         seq->seq_bits.force_screen_content_tools = AV1_SELECT_SCREEN_CONTENT_TOOLS;
      else
         seq->seq_bits.force_screen_content_tools = av1_f(vlc, 1);

      seq->seq_bits.force_integer_mv = AV1_SELECT_INTEGER_MV;
      if (seq->seq_bits.force_screen_content_tools) {
         seq->seq_bits.choose_integer_mv = av1_f(vlc, 1);
         if (!seq->seq_bits.choose_integer_mv)
            seq->seq_bits.force_integer_mv = av1_f(vlc, 1);
      }
      if (seq->seq_bits.enable_order_hint)
         seq->order_hint_bits = av1_f(vlc, 3) + 1;
      else
         seq->order_hint_bits = 0;
   }
   seq->seq_bits.enable_superres = av1_f(vlc, 1);
   seq->seq_bits.enable_cdef = av1_f(vlc, 1);
   seq->seq_bits.enable_restoration = av1_f(vlc, 1);
   av1_color_config(context, vlc);
}

static void av1_superres_params(vlVaContext *context, struct vl_vlc *vlc)
{
   struct pipe_av1_enc_picture_desc *av1 = &context->desc.av1enc;
   uint8_t use_superres;

   if (av1->seq.seq_bits.enable_superres)
      use_superres = av1_f(vlc, 1);
   else
      use_superres = 0;

  if (use_superres)
     av1_f(vlc, AV1_SUPERRES_DENOM_BITS);

  av1->upscaled_width = av1->frame_width;
}

static void av1_frame_size(vlVaContext *context, struct vl_vlc *vlc)
{
   struct pipe_av1_enc_picture_desc *av1 = &context->desc.av1enc;

   if (av1->frame_size_override_flag) {
      av1->frame_width = av1_f(vlc, av1->seq.frame_width_bits_minus1 + 1) + 1;
      av1->frame_height = av1_f(vlc, av1->seq.frame_height_bits_minus1 + 1) + 1;
   } else {
      av1->frame_width = av1->seq.pic_width_in_luma_samples;
      av1->frame_height = av1->seq.pic_height_in_luma_samples;
   }

   unsigned MiCols = 2 * (((av1->frame_width - 1) + 8) >> 3);
   unsigned MiRows = 2 * (((av1->frame_height - 1) + 8) >> 3);
   context->desc.av1enc.frame_width_sb = (av1->seq.seq_bits.use_128x128_superblock) ?
      ((MiCols + 31) >> 5) : ((MiCols + 15) >> 4);
   context->desc.av1enc.frame_height_sb = (av1->seq.seq_bits.use_128x128_superblock) ?
      ((MiRows + 31) >> 5) : ((MiRows + 15) >> 4);

   av1_superres_params(context, vlc);
}

static void av1_render_size(vlVaContext *context, struct vl_vlc *vlc)
{
   struct pipe_av1_enc_picture_desc *av1 = &context->desc.av1enc;

   av1->enable_render_size = av1_f(vlc, 1);
   if (av1->enable_render_size) {
      av1->render_width_minus_1 = av1_f(vlc, 16);
      av1->render_height_minus_1 = av1_f(vlc, 16);
   }
}

static void av1_frame_size_with_refs(vlVaContext *context, struct vl_vlc *vlc)
{
   uint8_t found_ref = 0;

   for (int i = 0; i < AV1_REFS_PER_FRAME; i++) {
      found_ref = av1_f(vlc, 1);
      if (found_ref)
         break;
   }

   if (found_ref == 0) {
      av1_frame_size(context, vlc);
      av1_render_size(context, vlc);
   } else
      av1_superres_params(context, vlc);
}

static void av1_read_interpolation_filter(vlVaContext *context, struct vl_vlc *vlc)
{
   uint8_t is_filter_switchable = av1_f(vlc, 1);

   if (!is_filter_switchable)
      av1_f(vlc, 2);
}

static bool av1_frame_header(vlVaContext *context, struct vl_vlc *vlc,
                             uint32_t extension_flag, uint32_t temporal_id, uint32_t spatial_id)
{
   struct pipe_av1_enc_picture_desc *av1 = &context->desc.av1enc;
   uint32_t frame_type;
   uint32_t id_len = 0, all_frames = 255;
   bool frame_is_intra = false;

   if (av1->seq.seq_bits.reduced_still_picture_header) {
      frame_is_intra = true;
      frame_type = FRAME_TYPE_KEY_FRAME;
      av1->show_frame = 1;
   } else {
      if (av1->seq.seq_bits.frame_id_number_present_flag)
         id_len = av1->seq.delta_frame_id_length + av1->seq.additional_frame_id_length;

      if (av1_f(vlc, 1)) /* show_existing_frame */
         return false;

      av1->obu_extension_flag = extension_flag;
      av1->temporal_id = temporal_id;
      av1->spatial_id = spatial_id;

      frame_type = av1_f(vlc, 2);
      frame_is_intra = (frame_type == FRAME_TYPE_KEY_FRAME ||
                        frame_type == FRAME_TYPE_INTRA_ONLY);
      av1->show_frame = av1_f(vlc, 1);
      if (av1->show_frame && av1->seq.seq_bits.decoder_model_info_present_flag
                     && !(av1->seq.seq_bits.equal_picture_interval)) {
         struct pipe_av1_enc_decoder_model_info *info = &av1->seq.decoder_model_info;
         av1->frame_presentation_time = av1_f(vlc, info->frame_presentation_time_length_minus1 + 1);
      }

      if (!av1->show_frame)
         av1->showable_frame = av1_f(vlc, 1);

      if (frame_type == FRAME_TYPE_SWITCH ||
            (frame_type == FRAME_TYPE_KEY_FRAME && av1->show_frame))
         av1->error_resilient_mode = 1;
      else
         av1->error_resilient_mode = av1_f(vlc, 1);
   }

   av1->disable_cdf_update = av1_f(vlc, 1);
   if (av1->seq.seq_bits.force_screen_content_tools == AV1_SELECT_SCREEN_CONTENT_TOOLS)
      av1->allow_screen_content_tools = av1_f(vlc, 1);
   else
      av1->allow_screen_content_tools = !!(av1->seq.seq_bits.force_screen_content_tools);

   av1->force_integer_mv = 0;
   if (av1->allow_screen_content_tools) {
      if (av1->seq.seq_bits.force_integer_mv == AV1_SELECT_INTEGER_MV)
         av1->force_integer_mv = av1_f(vlc, 1);
      else
         av1->force_integer_mv = !!(av1->seq.seq_bits.force_integer_mv);
   }

   if (frame_is_intra)
      av1->force_integer_mv = 1;

   if (av1->seq.seq_bits.frame_id_number_present_flag)
      av1->current_frame_id = av1_f(vlc, id_len);

   if (frame_type == FRAME_TYPE_SWITCH)
      av1->frame_size_override_flag = 1;
   else if (!av1->seq.seq_bits.reduced_still_picture_header)
      av1->frame_size_override_flag = av1_f(vlc, 1);

   if (av1->seq.seq_bits.enable_order_hint)
      av1->order_hint = av1_f(vlc, av1->seq.order_hint_bits);

   if (!(frame_is_intra || av1->error_resilient_mode))
      av1->primary_ref_frame = av1_f(vlc, 3);

   if (av1->seq.seq_bits.decoder_model_info_present_flag) {
      unsigned buffer_removal_time_present_flag = av1_f(vlc, 1);
      if (buffer_removal_time_present_flag) {
         for (int opNum = 0; opNum <= av1->seq.num_temporal_layers - 1; opNum++) {
            if (av1->seq.decoder_model_present_for_this_op[opNum]) {
               uint16_t op_pt_idc = av1->seq.operating_point_idc[opNum];
               uint16_t temporal_layer = (op_pt_idc >> av1->temporal_id) & 1;
               uint16_t spatial_layer = (op_pt_idc >> (av1->spatial_id + 8)) & 1;
               if (op_pt_idc == 0 || (temporal_layer && spatial_layer))
                  av1_f(vlc, av1->seq.decoder_model_info.buffer_removal_time_length_minus1 + 1);
            }
         }
      }
   }

   if (frame_type == FRAME_TYPE_SWITCH ||
       (frame_type == FRAME_TYPE_KEY_FRAME && av1->show_frame))
       av1->refresh_frame_flags = all_frames;
   else
      av1->refresh_frame_flags = av1_f(vlc, 8);

   if ( !frame_is_intra || av1->refresh_frame_flags != all_frames) {
      if (av1->error_resilient_mode && av1->seq.seq_bits.enable_order_hint)
         for (int i = 0; i < AV1_MAXNUM_REF_FRAMES; i++)
            av1->ref_order_hint[i] = av1_f(vlc, av1->seq.order_hint_bits);
   }

   if ( frame_is_intra) {
      av1_frame_size(context, vlc);
      av1_render_size(context, vlc);
      if (av1->allow_screen_content_tools && av1->upscaled_width == av1->frame_width)
         av1->allow_intrabc = av1_f(vlc, 1);
   } else {
      av1->frame_refs_short_signaling = 0;
      if (av1->seq.seq_bits.enable_order_hint) {
         av1->frame_refs_short_signaling = av1_f(vlc, 1);
         if (av1->frame_refs_short_signaling) {
            av1->last_frame_idx = av1_f(vlc, 3);
            av1->gold_frame_idx = av1_f(vlc, 3);
         }
      }

      for (int i = 0; i < AV1_REFS_PER_FRAME; i++) {
         if (!av1->frame_refs_short_signaling)
            av1->ref_frame_idx[i] = av1_f(vlc, 3);
         if (av1->seq.seq_bits.frame_id_number_present_flag)
            av1->delta_frame_id_minus_1[i] = av1_f(vlc, av1->seq.delta_frame_id_length);
      }

      if (av1->frame_size_override_flag && av1->error_resilient_mode)
         av1_frame_size_with_refs(context, vlc);
      else {
         av1_frame_size(context, vlc);
         av1_render_size(context, vlc);
      }

      if (av1->force_integer_mv)
         av1->allow_high_precision_mv = 0;
      else
         av1->allow_high_precision_mv = av1_f(vlc, 1);

      av1_read_interpolation_filter(context, vlc);
      av1->is_motion_mode_switchable = av1_f(vlc, 1);
      if (av1->error_resilient_mode || !av1->seq.seq_bits.enable_ref_frame_mvs)
         av1->use_ref_frame_mvs = 0;
      else
         av1->use_ref_frame_mvs = av1_f(vlc, 1);
   }

   if (av1->seq.seq_bits.reduced_still_picture_header || av1->disable_cdf_update)
      av1->disable_frame_end_update_cdf = 1;
   else
      av1->disable_frame_end_update_cdf = av1_f(vlc, 1);

   /* tile_info()
    * trying to keep uniform_tile_spacing_flag
    * if the tile rows and columns are not within the range
    * of HW capability, it will need to redo the tiling
    * according to the limixation.
    */

   av1->uniform_tile_spacing = av1_f(vlc, 1);

   return true;
}

static void av1_metatype_hdr_cll(vlVaContext *context, struct vl_vlc *vlc)
{
   struct pipe_av1_enc_picture_desc *av1 = &context->desc.av1enc;

   av1->metadata_flags.hdr_cll = 1;
   av1->metadata_hdr_cll.max_cll = av1_f(vlc, 16);
   av1->metadata_hdr_cll.max_fall = av1_f(vlc, 16);
}

static void av1_metatype_hdr_mdcv(vlVaContext *context, struct vl_vlc *vlc)
{
   struct pipe_av1_enc_picture_desc *av1 = &context->desc.av1enc;

   av1->metadata_flags.hdr_mdcv = 1;

   for (int32_t i = 0; i < 3; i++) {
      av1->metadata_hdr_mdcv.primary_chromaticity_x[i] = av1_f(vlc, 16);
      av1->metadata_hdr_mdcv.primary_chromaticity_y[i] = av1_f(vlc, 16);
   }
   av1->metadata_hdr_mdcv.white_point_chromaticity_x = av1_f(vlc, 16);
   av1->metadata_hdr_mdcv.white_point_chromaticity_y = av1_f(vlc, 16);
   av1->metadata_hdr_mdcv.luminance_max = av1_f(vlc, 32);
   av1->metadata_hdr_mdcv.luminance_min = av1_f(vlc, 32);
}

static void av1_meta_obu(vlVaContext *context, struct vl_vlc *vlc)
{
   unsigned meta_type = av1_uleb128(vlc);

   if (meta_type == METADATA_TYPE_HDR_CLL)
      av1_metatype_hdr_cll(context, vlc);
   else if (meta_type == METADATA_TYPE_HDR_MDCV)
      av1_metatype_hdr_mdcv(context, vlc);
}

VAStatus
vlVaHandleVAEncPackedHeaderDataBufferTypeAV1(vlVaContext *context, vlVaBuffer *buf)
{
   struct vl_vlc vlc = {0};
   vl_vlc_init(&vlc, 1, (const void * const*)&buf->data, &buf->size);

   av1_f(&vlc, 1); /* obu_forbidden_bit */
   uint32_t obu_type = av1_f(&vlc, 4);

   if (obu_type != OBU_TYPE_SEQUENCE_HEADER &&
       obu_type != OBU_TYPE_FRAME_HEADER &&
       obu_type != OBU_TYPE_FRAME &&
       obu_type != OBU_TYPE_META) {
      vlVaAddRawHeader(&context->desc.av1enc.raw_headers, obu_type,
                       buf->size, buf->data, false, 0);
      return VA_STATUS_SUCCESS;
   }

   uint32_t extension_flag = av1_f(&vlc, 1);
   uint32_t has_size = av1_f(&vlc, 1);
   av1_f(&vlc, 1); /* obu_reserved_1bit */

   uint32_t temporal_id = 0;
   uint32_t spatial_id = 0;

   if (extension_flag) {
      temporal_id = av1_f(&vlc, 3);
      spatial_id = av1_f(&vlc, 2);
      av1_f(&vlc, 3); /* extension_header_reserved_3bits */
   }

   if (has_size)
       av1_uleb128(&vlc);

   bool is_frame = false;

   if (obu_type == OBU_TYPE_SEQUENCE_HEADER)
      av1_sequence_header(context, &vlc);
   else if (obu_type == OBU_TYPE_FRAME_HEADER || obu_type == OBU_TYPE_FRAME)
      is_frame = av1_frame_header(context, &vlc, extension_flag, temporal_id, spatial_id);
   else if (obu_type == OBU_TYPE_META)
      av1_meta_obu(context, &vlc);

   vlVaAddRawHeader(&context->desc.av1enc.raw_headers, obu_type,
                    buf->size, buf->data, is_frame, 0);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaHandleVAEncMiscParameterTypeFrameRateAV1(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   unsigned temporal_id;
   VAEncMiscParameterFrameRate *fr = (VAEncMiscParameterFrameRate *)misc->data;

   temporal_id = context->desc.av1enc.rc[0].rate_ctrl_method !=
                 PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE ?
                 fr->framerate_flags.bits.temporal_id :
                 0;

   if (context->desc.av1enc.seq.num_temporal_layers > 0 &&
       temporal_id >= context->desc.av1enc.seq.num_temporal_layers)
      return VA_STATUS_ERROR_INVALID_PARAMETER;

   if (fr->framerate & 0xffff0000) {
      context->desc.av1enc.rc[temporal_id].frame_rate_num = fr->framerate       & 0xffff;
      context->desc.av1enc.rc[temporal_id].frame_rate_den = fr->framerate >> 16 & 0xffff;
   } else {
      context->desc.av1enc.rc[temporal_id].frame_rate_num = fr->framerate;
      context->desc.av1enc.rc[temporal_id].frame_rate_den = 1;
   }

   return VA_STATUS_SUCCESS;
}

VAStatus vlVaHandleVAEncSliceParameterBufferTypeAV1(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
    VAEncTileGroupBufferAV1 *tile_buf = (VAEncTileGroupBufferAV1*) buf->data;

    if (context->desc.av1enc.num_tile_groups < ARRAY_SIZE(context->desc.av1enc.tile_groups)) {
        context->desc.av1enc.tile_groups[context->desc.av1enc.num_tile_groups].tile_group_start = tile_buf->tg_start;
        context->desc.av1enc.tile_groups[context->desc.av1enc.num_tile_groups].tile_group_end = tile_buf->tg_end;
        context->desc.av1enc.num_tile_groups++;
    } else {
        return VA_STATUS_ERROR_NOT_ENOUGH_BUFFER;
    }

    return VA_STATUS_SUCCESS;
}
#endif /* VA_CHECK_VERSION(1, 16, 0) */
