/**************************************************************************
 *
 * Copyright 2018 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#include "radeon_uvd_enc.h"

#include "pipe/p_video_codec.h"
#include "radeon_video.h"
#include "radeonsi/si_pipe.h"
#include "util/u_memory.h"
#include "util/u_video.h"
#include "vl/vl_video_buffer.h"

#include <stdio.h>

static void radeon_uvd_enc_get_param(struct radeon_uvd_encoder *enc,
                                     struct pipe_h265_enc_picture_desc *pic)
{
   enc->enc_pic.desc = pic;
   enc->enc_pic.picture_type = pic->picture_type;
   enc->enc_pic.nal_unit_type = pic->pic.nal_unit_type;
   enc->enc_pic.enc_params.reference_picture_index =
      pic->ref_list0[0] == PIPE_H2645_LIST_REF_INVALID_ENTRY ? 0xffffffff : pic->ref_list0[0];
   enc->enc_pic.enc_params.reconstructed_picture_index = pic->dpb_curr_pic;

   enc->enc_pic.session_init.pre_encode_mode =
      pic->quality_modes.pre_encode_mode ? RENC_UVD_PREENCODE_MODE_4X : RENC_UVD_PREENCODE_MODE_NONE;
   enc->enc_pic.session_init.pre_encode_chroma_enabled = !!enc->enc_pic.session_init.pre_encode_mode;
   enc->enc_pic.quality_params.vbaq_mode =
      pic->rc[0].rate_ctrl_method != PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE &&
      pic->quality_modes.vbaq_mode;

   enc->enc_pic.layer_ctrl.num_temporal_layers = pic->seq.num_temporal_layers ? pic->seq.num_temporal_layers : 1;
   enc->enc_pic.layer_ctrl.max_num_temporal_layers = enc->enc_pic.layer_ctrl.num_temporal_layers;
   enc->enc_pic.temporal_id = MIN2(pic->pic.temporal_id, enc->enc_pic.layer_ctrl.num_temporal_layers - 1);

   for (uint32_t i = 0; i < enc->enc_pic.layer_ctrl.num_temporal_layers; i++) {
      enc->enc_pic.rc_layer_init[i].target_bit_rate = pic->rc[i].target_bitrate;
      enc->enc_pic.rc_layer_init[i].peak_bit_rate = pic->rc[i].peak_bitrate;
      enc->enc_pic.rc_layer_init[i].frame_rate_num = pic->rc[i].frame_rate_num;
      enc->enc_pic.rc_layer_init[i].frame_rate_den = pic->rc[i].frame_rate_den;
      enc->enc_pic.rc_layer_init[i].vbv_buffer_size = pic->rc[i].vbv_buffer_size;
      enc->enc_pic.rc_layer_init[i].avg_target_bits_per_picture =
         pic->rc[i].target_bitrate * ((float)pic->rc[i].frame_rate_den / pic->rc[i].frame_rate_num);
      enc->enc_pic.rc_layer_init[i].peak_bits_per_picture_integer =
         pic->rc[i].peak_bitrate * ((float)pic->rc[i].frame_rate_den / pic->rc[i].frame_rate_num);
      enc->enc_pic.rc_layer_init[i].peak_bits_per_picture_fractional =
         (((pic->rc[i].peak_bitrate * (uint64_t)pic->rc[i].frame_rate_den) % pic->rc[i].frame_rate_num) << 32) /
         pic->rc[i].frame_rate_num;
   }
   enc->enc_pic.rc_per_pic.qp = pic->rc[0].quant_i_frames;
   enc->enc_pic.rc_per_pic.min_qp_app = pic->rc[0].min_qp;
   enc->enc_pic.rc_per_pic.max_qp_app = pic->rc[0].max_qp ? pic->rc[0].max_qp : 51;
   enc->enc_pic.rc_per_pic.max_au_size = pic->rc[0].max_au_size;
   enc->enc_pic.rc_per_pic.enabled_filler_data = pic->rc[0].fill_data_enable;
   enc->enc_pic.rc_per_pic.skip_frame_enable = false;
   enc->enc_pic.rc_per_pic.enforce_hrd = pic->rc[0].enforce_hrd;
}

static int flush(struct radeon_uvd_encoder *enc, unsigned flags, struct pipe_fence_handle **fence)
{
   return enc->ws->cs_flush(&enc->cs, flags, fence);
}

static void radeon_uvd_enc_flush(struct pipe_video_codec *encoder)
{
   struct radeon_uvd_encoder *enc = (struct radeon_uvd_encoder *)encoder;
   flush(enc, PIPE_FLUSH_ASYNC, NULL);
}

static void radeon_uvd_enc_cs_flush(void *ctx, unsigned flags, struct pipe_fence_handle **fence)
{
   // just ignored
}

static uint32_t setup_dpb(struct radeon_uvd_encoder *enc, uint32_t num_reconstructed_pictures)
{
   uint32_t i;
   uint32_t alignment = 256;
   uint32_t aligned_width = align(enc->base.width, 64);
   uint32_t aligned_height = align(enc->base.height, 16);
   uint32_t pitch = align(aligned_width, alignment);
   uint32_t luma_size = align(pitch * MAX2(256, aligned_height), alignment);
   uint32_t chroma_size = align(luma_size / 2, alignment);
   uint32_t offset = 0;
   uint32_t pre_encode_luma_size, pre_encode_chroma_size;

   assert(num_reconstructed_pictures <= RENC_UVD_MAX_NUM_RECONSTRUCTED_PICTURES);

   enc->enc_pic.ctx_buf.rec_luma_pitch = pitch;
   enc->enc_pic.ctx_buf.rec_chroma_pitch = pitch;
   enc->enc_pic.ctx_buf.num_reconstructed_pictures = num_reconstructed_pictures;

   if (enc->enc_pic.session_init.pre_encode_mode) {
      uint32_t pre_encode_pitch =
         align(pitch / enc->enc_pic.session_init.pre_encode_mode, alignment);
      uint32_t pre_encode_aligned_height =
         align(aligned_height / enc->enc_pic.session_init.pre_encode_mode, alignment);
      pre_encode_luma_size =
         align(pre_encode_pitch * MAX2(256, pre_encode_aligned_height), alignment);
      pre_encode_chroma_size = align(pre_encode_luma_size / 2, alignment);

      enc->enc_pic.ctx_buf.pre_encode_picture_luma_pitch = pre_encode_pitch;
      enc->enc_pic.ctx_buf.pre_encode_picture_chroma_pitch = pre_encode_pitch;

      enc->enc_pic.ctx_buf.pre_encode_input_picture.luma_offset = offset;
      offset += pre_encode_luma_size;
      enc->enc_pic.ctx_buf.pre_encode_input_picture.chroma_offset = offset;
      offset += pre_encode_chroma_size;
   }

   for (i = 0; i < num_reconstructed_pictures; i++) {
      enc->enc_pic.ctx_buf.reconstructed_pictures[i].luma_offset = offset;
      offset += luma_size;
      enc->enc_pic.ctx_buf.reconstructed_pictures[i].chroma_offset = offset;
      offset += chroma_size;

      if (enc->enc_pic.session_init.pre_encode_mode) {
         enc->enc_pic.ctx_buf.pre_encode_reconstructed_pictures[i].luma_offset = offset;
         offset += pre_encode_luma_size;
         enc->enc_pic.ctx_buf.pre_encode_reconstructed_pictures[i].chroma_offset = offset;
         offset += pre_encode_chroma_size;
      }
   }

   enc->dpb_slots = num_reconstructed_pictures;

   return offset;
}

static void radeon_uvd_enc_begin_frame(struct pipe_video_codec *encoder,
                                       struct pipe_video_buffer *source,
                                       struct pipe_picture_desc *picture)
{
   struct radeon_uvd_encoder *enc = (struct radeon_uvd_encoder *)encoder;
   struct vl_video_buffer *vid_buf = (struct vl_video_buffer *)source;
   struct pipe_h265_enc_picture_desc *pic = (struct pipe_h265_enc_picture_desc *)picture;

   enc->need_rate_control =
      (enc->enc_pic.rc_layer_init[0].target_bit_rate != pic->rc[0].target_bitrate) ||
      (enc->enc_pic.rc_layer_init[0].frame_rate_num != pic->rc[0].frame_rate_num) ||
      (enc->enc_pic.rc_layer_init[0].frame_rate_den != pic->rc[0].frame_rate_den);

   enc->need_rc_per_pic =
      (enc->enc_pic.rc_per_pic.qp != pic->rc[0].quant_i_frames) ||
      (enc->enc_pic.rc_per_pic.max_au_size != pic->rc[0].max_au_size);

   radeon_uvd_enc_get_param(enc, pic);

   enc->get_buffer(vid_buf->resources[0], &enc->handle, &enc->luma);
   enc->get_buffer(vid_buf->resources[1], NULL, &enc->chroma);

   enc->source = source;
   enc->need_feedback = false;

   unsigned dpb_slots = MAX2(pic->seq.sps_max_dec_pic_buffering_minus1[0] + 1, pic->dpb_size);

   if (enc->dpb_slots < dpb_slots) {
      uint32_t dpb_size = setup_dpb(enc, dpb_slots);
      if (!enc->dpb.res) {
         if (!si_vid_create_buffer(enc->screen, &enc->dpb, dpb_size, PIPE_USAGE_DEFAULT)) {
            RVID_ERR("Can't create DPB buffer.\n");
            return;
         }
      } else if (!si_vid_resize_buffer(enc->base.context, &enc->cs, &enc->dpb, dpb_size, NULL)) {
         RVID_ERR("Can't resize DPB buffer.\n");
         return;
      }
   }

   if (!enc->stream_handle) {
      struct rvid_buffer fb;
      enc->stream_handle = si_vid_alloc_stream_handle();
      enc->si = CALLOC_STRUCT(rvid_buffer);
      si_vid_create_buffer(enc->screen, enc->si, 128 * 1024, PIPE_USAGE_DEFAULT);
      si_vid_create_buffer(enc->screen, &fb, 4096, PIPE_USAGE_STAGING);
      enc->fb = &fb;
      enc->begin(enc, picture);
      flush(enc, PIPE_FLUSH_ASYNC, NULL);
      si_vid_destroy_buffer(&fb);
   }
}

static void *radeon_uvd_enc_encode_headers(struct radeon_uvd_encoder *enc)
{
   unsigned num_slices = 0, num_headers = 0;

   util_dynarray_foreach(&enc->enc_pic.desc->raw_headers, struct pipe_enc_raw_header, header) {
      if (header->is_slice)
         num_slices++;
      num_headers++;
   }

   if (!num_headers || !num_slices || num_headers == num_slices)
      return NULL;

   size_t segments_size =
      sizeof(struct ruvd_enc_output_unit_segment) * (num_headers - num_slices + 1);
   struct ruvd_enc_feedback_data *data =
      CALLOC_VARIANT_LENGTH_STRUCT(ruvd_enc_feedback_data, segments_size);
   if (!data)
      return NULL;

   uint8_t *ptr = enc->ws->buffer_map(enc->ws, enc->bs_handle, &enc->cs,
                                      PIPE_MAP_WRITE | RADEON_MAP_TEMPORARY);
   if (!ptr) {
      RVID_ERR("Can't map bs buffer.\n");
      FREE(data);
      return NULL;
   }

   unsigned offset = 0;
   struct ruvd_enc_output_unit_segment *slice_segment = NULL;

   util_dynarray_foreach(&enc->enc_pic.desc->raw_headers, struct pipe_enc_raw_header, header) {
      if (header->is_slice) {
         if (slice_segment)
            continue;
         slice_segment = &data->segments[data->num_segments];
         slice_segment->is_slice = true;
      } else {
         unsigned size;
         switch (header->type) {
         case PIPE_H265_NAL_VPS:
            size = radeon_uvd_enc_write_vps(enc, ptr + offset);
            break;
         case PIPE_H265_NAL_SPS:
            size = radeon_uvd_enc_write_sps(enc, ptr + offset);
            break;
         case PIPE_H265_NAL_PPS:
            size = radeon_uvd_enc_write_pps(enc, ptr + offset);
            break;
         default:
            assert(header->buffer);
            memcpy(ptr + offset, header->buffer, header->size);
            size = header->size;
            break;
         }
         data->segments[data->num_segments].size = size;
         data->segments[data->num_segments].offset = offset;
         offset += size;
      }
      data->num_segments++;
   }

   enc->bs_offset = align(offset, 16);
   assert(enc->bs_offset < enc->bs_size);

   assert(slice_segment);
   slice_segment->offset = enc->bs_offset;

   enc->ws->buffer_unmap(enc->ws, enc->bs_handle);

   return data;
}

static void radeon_uvd_enc_encode_bitstream(struct pipe_video_codec *encoder,
                                            struct pipe_video_buffer *source,
                                            struct pipe_resource *destination, void **fb)
{
   struct radeon_uvd_encoder *enc = (struct radeon_uvd_encoder *)encoder;
   enc->get_buffer(destination, &enc->bs_handle, NULL);
   enc->bs_size = destination->width0;
   enc->bs_offset = 0;

   *fb = enc->fb = CALLOC_STRUCT(rvid_buffer);

   if (!si_vid_create_buffer(enc->screen, enc->fb, 4096, PIPE_USAGE_STAGING)) {
      RVID_ERR("Can't create feedback buffer.\n");
      return;
   }

   enc->fb->user_data = radeon_uvd_enc_encode_headers(enc);

   enc->need_feedback = true;
   enc->encode(enc);
}

static int radeon_uvd_enc_end_frame(struct pipe_video_codec *encoder,
                                     struct pipe_video_buffer *source,
                                     struct pipe_picture_desc *picture)
{
   struct radeon_uvd_encoder *enc = (struct radeon_uvd_encoder *)encoder;
   return flush(enc, picture->flush_flags, picture->fence);
}

static void radeon_uvd_enc_destroy(struct pipe_video_codec *encoder)
{
   struct radeon_uvd_encoder *enc = (struct radeon_uvd_encoder *)encoder;

   if (enc->stream_handle) {
      struct rvid_buffer fb;
      enc->need_feedback = false;
      si_vid_create_buffer(enc->screen, &fb, 512, PIPE_USAGE_STAGING);
      enc->fb = &fb;
      enc->destroy(enc);
      flush(enc, PIPE_FLUSH_ASYNC, NULL);
      if (enc->si) {
         si_vid_destroy_buffer(enc->si);
         FREE(enc->si);
      }
      si_vid_destroy_buffer(&fb);
   }

   if (enc->dpb.res)
      si_vid_destroy_buffer(&enc->dpb);
   enc->ws->cs_destroy(&enc->cs);
   FREE(enc);
}

static void radeon_uvd_enc_get_feedback(struct pipe_video_codec *encoder, void *feedback,
                                        unsigned *size, struct pipe_enc_feedback_metadata* metadata)
{
   struct radeon_uvd_encoder *enc = (struct radeon_uvd_encoder *)encoder;
   struct rvid_buffer *fb = feedback;

   radeon_uvd_enc_feedback_t *fb_data = (radeon_uvd_enc_feedback_t *)enc->ws->buffer_map(
      enc->ws, fb->res->buf, &enc->cs, PIPE_MAP_READ_WRITE | RADEON_MAP_TEMPORARY);

   if (!fb_data->status)
      *size = fb_data->bitstream_size;
   else
      *size = 0;

   enc->ws->buffer_unmap(enc->ws, fb->res->buf);

   metadata->present_metadata = PIPE_VIDEO_FEEDBACK_METADATA_TYPE_CODEC_UNIT_LOCATION;

   if (fb->user_data) {
      struct ruvd_enc_feedback_data *data = fb->user_data;
      metadata->codec_unit_metadata_count = data->num_segments;
      for (unsigned i = 0; i < data->num_segments; i++) {
         metadata->codec_unit_metadata[i].offset = data->segments[i].offset;
         if (data->segments[i].is_slice) {
            metadata->codec_unit_metadata[i].size = *size;
            metadata->codec_unit_metadata[i].flags = 0;
         } else {
            metadata->codec_unit_metadata[i].size = data->segments[i].size;
            metadata->codec_unit_metadata[i].flags = PIPE_VIDEO_CODEC_UNIT_LOCATION_FLAG_SINGLE_NALU;
         }
      }
      FREE(fb->user_data);
      fb->user_data = NULL;
   } else {
      metadata->codec_unit_metadata_count = 1;
      metadata->codec_unit_metadata[0].offset = 0;
      metadata->codec_unit_metadata[0].size = *size;
      metadata->codec_unit_metadata[0].flags = 0;
   }

   si_vid_destroy_buffer(fb);
   FREE(fb);
}

static int radeon_uvd_enc_fence_wait(struct pipe_video_codec *encoder,
                                     struct pipe_fence_handle *fence,
                                     uint64_t timeout)
{
   struct radeon_uvd_encoder *enc = (struct radeon_uvd_encoder *)encoder;

   return enc->ws->fence_wait(enc->ws, fence, timeout);
}

static void radeon_uvd_enc_destroy_fence(struct pipe_video_codec *encoder,
                                         struct pipe_fence_handle *fence)
{
   struct radeon_uvd_encoder *enc = (struct radeon_uvd_encoder *)encoder;

   enc->ws->fence_reference(enc->ws, &fence, NULL);
}

struct pipe_video_codec *radeon_uvd_create_encoder(struct pipe_context *context,
                                                   const struct pipe_video_codec *templ,
                                                   struct radeon_winsys *ws,
                                                   radeon_uvd_enc_get_buffer get_buffer)
{
   struct si_screen *sscreen = (struct si_screen *)context->screen;
   struct si_context *sctx = (struct si_context *)context;
   struct radeon_uvd_encoder *enc;

   if (!si_radeon_uvd_enc_supported(sscreen)) {
      RVID_ERR("Unsupported UVD ENC fw version loaded!\n");
      return NULL;
   }

   enc = CALLOC_STRUCT(radeon_uvd_encoder);

   if (!enc)
      return NULL;

   enc->base = *templ;
   enc->base.context = context;
   enc->base.destroy = radeon_uvd_enc_destroy;
   enc->base.begin_frame = radeon_uvd_enc_begin_frame;
   enc->base.encode_bitstream = radeon_uvd_enc_encode_bitstream;
   enc->base.end_frame = radeon_uvd_enc_end_frame;
   enc->base.flush = radeon_uvd_enc_flush;
   enc->base.get_feedback = radeon_uvd_enc_get_feedback;
   enc->base.fence_wait = radeon_uvd_enc_fence_wait;
   enc->base.destroy_fence = radeon_uvd_enc_destroy_fence;
   enc->get_buffer = get_buffer;
   enc->screen = context->screen;
   enc->ws = ws;

   if (!ws->cs_create(&enc->cs, sctx->ctx, AMD_IP_UVD_ENC, radeon_uvd_enc_cs_flush, enc)) {
      RVID_ERR("Can't get command submission context.\n");
      goto error;
   }

   radeon_uvd_enc_1_1_init(enc);

   return &enc->base;

error:
   enc->ws->cs_destroy(&enc->cs);

   FREE(enc);
   return NULL;
}

bool si_radeon_uvd_enc_supported(struct si_screen *sscreen)
{
   return sscreen->info.ip[AMD_IP_UVD_ENC].num_queues;
}
