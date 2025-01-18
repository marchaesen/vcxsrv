/**************************************************************************
 *
 * Copyright 2013 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 *
 **************************************************************************/

#include "radeon_vce.h"

#include "pipe/p_video_codec.h"
#include "radeon_video.h"
#include "radeonsi/si_pipe.h"
#include "util/u_memory.h"
#include "util/u_video.h"
#include "vl/vl_video_buffer.h"

#include <stdio.h>

#define FW_52_0_3  ((52 << 24) | (0 << 16) | (3 << 8))
#define FW_52_4_3  ((52 << 24) | (4 << 16) | (3 << 8))
#define FW_52_8_3  ((52 << 24) | (8 << 16) | (3 << 8))
#define FW_53       (53 << 24)

/**
 * flush commands to the hardware
 */
static void flush(struct rvce_encoder *enc, unsigned flags, struct pipe_fence_handle **fence)
{
   enc->ws->cs_flush(&enc->cs, flags, fence);
}

#if 0
static void dump_feedback(struct rvce_encoder *enc, struct rvid_buffer *fb)
{
   uint32_t *ptr = enc->ws->buffer_map(fb->res->buf, &enc->cs, PIPE_MAP_READ_WRITE);
   unsigned i = 0;
   fprintf(stderr, "\n");
   fprintf(stderr, "encStatus:\t\t\t%08x\n", ptr[i++]);
   fprintf(stderr, "encHasBitstream:\t\t%08x\n", ptr[i++]);
   fprintf(stderr, "encHasAudioBitstream:\t\t%08x\n", ptr[i++]);
   fprintf(stderr, "encBitstreamOffset:\t\t%08x\n", ptr[i++]);
   fprintf(stderr, "encBitstreamSize:\t\t%08x\n", ptr[i++]);
   fprintf(stderr, "encAudioBitstreamOffset:\t%08x\n", ptr[i++]);
   fprintf(stderr, "encAudioBitstreamSize:\t\t%08x\n", ptr[i++]);
   fprintf(stderr, "encExtrabytes:\t\t\t%08x\n", ptr[i++]);
   fprintf(stderr, "encAudioExtrabytes:\t\t%08x\n", ptr[i++]);
   fprintf(stderr, "videoTimeStamp:\t\t\t%08x\n", ptr[i++]);
   fprintf(stderr, "audioTimeStamp:\t\t\t%08x\n", ptr[i++]);
   fprintf(stderr, "videoOutputType:\t\t%08x\n", ptr[i++]);
   fprintf(stderr, "attributeFlags:\t\t\t%08x\n", ptr[i++]);
   fprintf(stderr, "seiPrivatePackageOffset:\t%08x\n", ptr[i++]);
   fprintf(stderr, "seiPrivatePackageSize:\t\t%08x\n", ptr[i++]);
   fprintf(stderr, "\n");
   enc->ws->buffer_unmap(fb->res->buf);
}
#endif

/**
 * Calculate the offsets into the DPB
 */
void si_vce_frame_offset(struct rvce_encoder *enc, unsigned slot, signed *luma_offset,
                         signed *chroma_offset)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;
   unsigned pitch, vpitch, fsize, offset = 0;

   if (enc->dual_pipe)
      offset += RVCE_MAX_AUX_BUFFER_NUM * RVCE_MAX_BITSTREAM_OUTPUT_ROW_SIZE * 2;

   if (sscreen->info.gfx_level < GFX9) {
      pitch = align(enc->luma->u.legacy.level[0].nblk_x * enc->luma->bpe, 128);
      vpitch = align(enc->luma->u.legacy.level[0].nblk_y, 16);
   } else {
      pitch = align(enc->luma->u.gfx9.surf_pitch * enc->luma->bpe, 256);
      vpitch = align(enc->luma->u.gfx9.surf_height, 16);
   }
   fsize = pitch * (vpitch + vpitch / 2);

   *luma_offset = offset + slot * fsize;
   *chroma_offset = *luma_offset + pitch * vpitch;
}

/**
 * destroy this video encoder
 */
static void rvce_destroy(struct pipe_video_codec *encoder)
{
   struct rvce_encoder *enc = (struct rvce_encoder *)encoder;
   if (enc->stream_handle) {
      struct rvid_buffer fb;
      si_vid_create_buffer(enc->screen, &fb, 512, PIPE_USAGE_STAGING);
      enc->fb = &fb;
      enc->session(enc);
      enc->destroy(enc);
      flush(enc, PIPE_FLUSH_ASYNC, NULL);
      si_vid_destroy_buffer(&fb);
   }
   si_vid_destroy_buffer(&enc->dpb);
   enc->ws->cs_destroy(&enc->cs);
   FREE(enc);
}

static unsigned get_dpb_size(struct rvce_encoder *enc, unsigned slots)
{
   struct si_screen *sscreen = (struct si_screen *)enc->screen;
   unsigned dpb_size;

   dpb_size = (sscreen->info.gfx_level < GFX9)
                 ? align(enc->luma->u.legacy.level[0].nblk_x * enc->luma->bpe, 128) *
                      align(enc->luma->u.legacy.level[0].nblk_y, 32)
                 :

                 align(enc->luma->u.gfx9.surf_pitch * enc->luma->bpe, 256) *
                    align(enc->luma->u.gfx9.surf_height, 32);

   dpb_size = dpb_size * 3 / 2;
   dpb_size = dpb_size * slots;
   if (enc->dual_pipe)
      dpb_size += RVCE_MAX_AUX_BUFFER_NUM * RVCE_MAX_BITSTREAM_OUTPUT_ROW_SIZE * 2;

   enc->dpb_slots = slots;

   return dpb_size;
}

static void rvce_begin_frame(struct pipe_video_codec *encoder, struct pipe_video_buffer *source,
                             struct pipe_picture_desc *picture)
{
   struct rvce_encoder *enc = (struct rvce_encoder *)encoder;
   struct vl_video_buffer *vid_buf = (struct vl_video_buffer *)source;
   struct pipe_h264_enc_picture_desc *pic = (struct pipe_h264_enc_picture_desc *)picture;

   bool need_rate_control =
      enc->pic.rate_ctrl[0].rate_ctrl_method != pic->rate_ctrl[0].rate_ctrl_method ||
      enc->pic.quant_i_frames != pic->quant_i_frames ||
      enc->pic.quant_p_frames != pic->quant_p_frames ||
      enc->pic.quant_b_frames != pic->quant_b_frames ||
      enc->pic.rate_ctrl[0].target_bitrate != pic->rate_ctrl[0].target_bitrate ||
      enc->pic.rate_ctrl[0].frame_rate_num != pic->rate_ctrl[0].frame_rate_num ||
      enc->pic.rate_ctrl[0].frame_rate_den != pic->rate_ctrl[0].frame_rate_den;

   enc->pic = *pic;
   enc->si_get_pic_param(enc, pic);

   enc->get_buffer(vid_buf->resources[0], &enc->handle, &enc->luma);
   enc->get_buffer(vid_buf->resources[1], NULL, &enc->chroma);

   unsigned dpb_slots = MAX2(pic->seq.max_num_ref_frames + 1, pic->dpb_size);

   if (enc->dpb_slots < dpb_slots) {
      unsigned dpb_size;

      dpb_size = get_dpb_size(enc, dpb_slots);
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
      si_vid_create_buffer(enc->screen, &fb, 512, PIPE_USAGE_STAGING);
      enc->fb = &fb;
      enc->session(enc);
      enc->create(enc);
      enc->config(enc);
      enc->feedback(enc);
      flush(enc, PIPE_FLUSH_ASYNC, NULL);
      // dump_feedback(enc, &fb);
      si_vid_destroy_buffer(&fb);
      need_rate_control = false;
   }

   if (need_rate_control) {
      enc->session(enc);
      enc->config(enc);
      flush(enc, PIPE_FLUSH_ASYNC, NULL);
   }
}

static void *si_vce_encode_headers(struct rvce_encoder *enc)
{
   unsigned num_slices = 0, num_headers = 0;

   util_dynarray_foreach(&enc->pic.raw_headers, struct pipe_enc_raw_header, header) {
      if (header->is_slice)
         num_slices++;
      num_headers++;
   }

   if (!num_headers || !num_slices || num_headers == num_slices)
      return NULL;

   size_t segments_size =
      sizeof(struct rvce_output_unit_segment) * (num_headers - num_slices + 1);
   struct rvce_feedback_data *data =
      CALLOC_VARIANT_LENGTH_STRUCT(rvce_feedback_data, segments_size);
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
   struct rvce_output_unit_segment *slice_segment = NULL;

   util_dynarray_foreach(&enc->pic.raw_headers, struct pipe_enc_raw_header, header) {
      if (header->is_slice) {
         if (slice_segment)
            continue;
         slice_segment = &data->segments[data->num_segments];
         slice_segment->is_slice = true;
      } else {
         unsigned size;
         /* Startcode may be 3 or 4 bytes. */
         const uint8_t nal_byte = header->buffer[header->buffer[2] == 0x1 ? 3 : 4];

         switch (header->type) {
         case PIPE_H264_NAL_SPS:
            size = si_vce_write_sps(enc, nal_byte, ptr + offset);
            break;
         case PIPE_H264_NAL_PPS:
            size = si_vce_write_pps(enc, nal_byte, ptr + offset);
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

static void rvce_encode_bitstream(struct pipe_video_codec *encoder,
                                  struct pipe_video_buffer *source,
                                  struct pipe_resource *destination, void **fb)
{
   struct rvce_encoder *enc = (struct rvce_encoder *)encoder;
   enc->get_buffer(destination, &enc->bs_handle, NULL);
   enc->bs_size = destination->width0;
   enc->bs_offset = 0;

   *fb = enc->fb = CALLOC_STRUCT(rvid_buffer);
   if (!si_vid_create_buffer(enc->screen, enc->fb, 512, PIPE_USAGE_STAGING)) {
      RVID_ERR("Can't create feedback buffer.\n");
      return;
   }

   enc->fb->user_data = si_vce_encode_headers(enc);

   if (!radeon_emitted(&enc->cs, 0))
      enc->session(enc);
   enc->encode(enc);
   enc->feedback(enc);
}

static int rvce_end_frame(struct pipe_video_codec *encoder, struct pipe_video_buffer *source,
                          struct pipe_picture_desc *picture)
{
   struct rvce_encoder *enc = (struct rvce_encoder *)encoder;

   flush(enc, picture->flush_flags, picture->fence);

   return 0;
}

static void rvce_get_feedback(struct pipe_video_codec *encoder, void *feedback, unsigned *size,
                              struct pipe_enc_feedback_metadata* metadata)
{
   struct rvce_encoder *enc = (struct rvce_encoder *)encoder;
   struct rvid_buffer *fb = feedback;

   uint32_t *ptr = enc->ws->buffer_map(enc->ws, fb->res->buf, &enc->cs,
                                       PIPE_MAP_READ_WRITE | RADEON_MAP_TEMPORARY);

   if (ptr[1]) {
      *size = ptr[4] - ptr[9];
   } else {
      *size = 0;
   }

   enc->ws->buffer_unmap(enc->ws, fb->res->buf);

   metadata->present_metadata = PIPE_VIDEO_FEEDBACK_METADATA_TYPE_CODEC_UNIT_LOCATION;

   if (fb->user_data) {
      struct rvce_feedback_data *data = fb->user_data;
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

   // dump_feedback(enc, fb);
   si_vid_destroy_buffer(fb);
   FREE(fb);
}

static int rvce_fence_wait(struct pipe_video_codec *encoder,
                           struct pipe_fence_handle *fence,
                           uint64_t timeout)
{
   struct rvce_encoder *enc = (struct rvce_encoder *)encoder;

   return enc->ws->fence_wait(enc->ws, fence, timeout);
}

static void rvce_destroy_fence(struct pipe_video_codec *encoder,
                               struct pipe_fence_handle *fence)
{
   struct rvce_encoder *enc = (struct rvce_encoder *)encoder;

   enc->ws->fence_reference(enc->ws, &fence, NULL);
}

/**
 * flush any outstanding command buffers to the hardware
 */
static void rvce_flush(struct pipe_video_codec *encoder)
{
   struct rvce_encoder *enc = (struct rvce_encoder *)encoder;

   flush(enc, PIPE_FLUSH_ASYNC, NULL);
}

static void rvce_cs_flush(void *ctx, unsigned flags, struct pipe_fence_handle **fence)
{
   // just ignored
}

struct pipe_video_codec *si_vce_create_encoder(struct pipe_context *context,
                                               const struct pipe_video_codec *templ,
                                               struct radeon_winsys *ws, rvce_get_buffer get_buffer)
{
   struct si_screen *sscreen = (struct si_screen *)context->screen;
   struct si_context *sctx = (struct si_context *)context;
   struct rvce_encoder *enc;

   if (!sscreen->info.vce_fw_version) {
      RVID_ERR("Kernel doesn't supports VCE!\n");
      return NULL;

   } else if (!si_vce_is_fw_version_supported(sscreen)) {
      RVID_ERR("Unsupported VCE fw version loaded!\n");
      return NULL;
   }

   enc = CALLOC_STRUCT(rvce_encoder);
   if (!enc)
      return NULL;

   if (sscreen->info.is_amdgpu)
      enc->use_vm = true;

   if (sscreen->info.family >= CHIP_TONGA && sscreen->info.family != CHIP_STONEY &&
       sscreen->info.family != CHIP_POLARIS11 && sscreen->info.family != CHIP_POLARIS12 &&
       sscreen->info.family != CHIP_VEGAM)
      enc->dual_pipe = true;

   enc->base = *templ;
   enc->base.context = context;

   enc->base.destroy = rvce_destroy;
   enc->base.begin_frame = rvce_begin_frame;
   enc->base.encode_bitstream = rvce_encode_bitstream;
   enc->base.end_frame = rvce_end_frame;
   enc->base.flush = rvce_flush;
   enc->base.get_feedback = rvce_get_feedback;
   enc->base.fence_wait = rvce_fence_wait;
   enc->base.destroy_fence = rvce_destroy_fence;
   enc->get_buffer = get_buffer;

   enc->screen = context->screen;
   enc->ws = ws;

   if (!ws->cs_create(&enc->cs, sctx->ctx, AMD_IP_VCE, rvce_cs_flush, enc)) {
      RVID_ERR("Can't get command submission context.\n");
      goto error;
   }

   si_vce_52_init(enc);

   return &enc->base;

error:
   enc->ws->cs_destroy(&enc->cs);

   FREE(enc);
   return NULL;
}

/**
 * check if kernel has the right fw version loaded
 */
bool si_vce_is_fw_version_supported(struct si_screen *sscreen)
{
   switch (sscreen->info.vce_fw_version) {
   case FW_52_0_3:
   case FW_52_4_3:
   case FW_52_8_3:
      return true;
   default:
      if ((sscreen->info.vce_fw_version & (0xff << 24)) >= FW_53)
         return true;
      else
         return false;
   }
}

/**
 * Add the buffer as relocation to the current command submission
 */
void si_vce_add_buffer(struct rvce_encoder *enc, struct pb_buffer_lean *buf, unsigned usage,
                       enum radeon_bo_domain domain, signed offset)
{
   int reloc_idx;

   reloc_idx = enc->ws->cs_add_buffer(&enc->cs, buf, usage | RADEON_USAGE_SYNCHRONIZED, domain);
   if (enc->use_vm) {
      uint64_t addr;
      addr = enc->ws->buffer_get_virtual_address(buf);
      addr = addr + offset;
      RVCE_CS(addr >> 32);
      RVCE_CS(addr);
   } else {
      offset += enc->ws->buffer_get_reloc_offset(buf);
      RVCE_CS(reloc_idx * 4);
      RVCE_CS(offset);
   }
}
