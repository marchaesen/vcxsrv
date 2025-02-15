/**************************************************************************
 *
 * Copyright 2010 Thomas Balling Sørensen & Orasanu Lucian.
 * Copyright 2014 Advanced Micro Devices, Inc.
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

#include "pipe/p_video_codec.h"

#include "util/u_handle_table.h"
#include "util/u_video.h"
#include "util/u_memory.h"
#include "util/set.h"

#include "util/vl_vlc.h"
#include "vl/vl_winsys.h"

#include "va_private.h"

void
vlVaSetSurfaceContext(vlVaDriver *drv, vlVaSurface *surf, vlVaContext *context)
{
   if (surf->ctx == context)
      return;

   if (surf->ctx) {
      assert(_mesa_set_search(surf->ctx->surfaces, surf));
      _mesa_set_remove_key(surf->ctx->surfaces, surf);

      /* Only drivers supporting PIPE_VIDEO_ENTRYPOINT_PROCESSING will create
       * decoder for postproc context and thus be able to wait on and destroy
       * the surface fence. On other drivers we need to destroy the fence here
       * otherwise vaQuerySurfaceStatus/vaSyncSurface will fail and we'll also
       * potentially leak the fence.
       */
      if (surf->fence && !context->decoder &&
          context->templat.entrypoint == PIPE_VIDEO_ENTRYPOINT_PROCESSING &&
          surf->ctx->decoder && surf->ctx->decoder->destroy_fence &&
          !drv->pipe->screen->get_video_param(drv->pipe->screen,
                                              PIPE_VIDEO_PROFILE_UNKNOWN,
                                              PIPE_VIDEO_ENTRYPOINT_PROCESSING,
                                              PIPE_VIDEO_CAP_SUPPORTED)) {
         surf->ctx->decoder->destroy_fence(surf->ctx->decoder, surf->fence);
         surf->fence = NULL;
      }
   }

   surf->ctx = context;
   _mesa_set_add(surf->ctx->surfaces, surf);
}

static void
vlVaSetBufferContext(vlVaDriver *drv, vlVaBuffer *buf, vlVaContext *context)
{
   if (buf->ctx == context)
      return;

   if (buf->ctx) {
      assert(_mesa_set_search(buf->ctx->buffers, buf));
      _mesa_set_remove_key(buf->ctx->buffers, buf);
   }

   buf->ctx = context;
   _mesa_set_add(buf->ctx->buffers, buf);
}

VAStatus
vlVaBeginPicture(VADriverContextP ctx, VAContextID context_id, VASurfaceID render_target)
{
   vlVaDriver *drv;
   vlVaContext *context;
   vlVaSurface *surf;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   mtx_lock(&drv->mutex);
   context = handle_table_get(drv->htab, context_id);
   if (!context) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_CONTEXT;
   }

   if (u_reduce_video_profile(context->templat.profile) == PIPE_VIDEO_FORMAT_MPEG12) {
      context->desc.mpeg12.intra_matrix = NULL;
      context->desc.mpeg12.non_intra_matrix = NULL;
   }

   surf = handle_table_get(drv->htab, render_target);
   vlVaGetSurfaceBuffer(drv, surf);
   if (!surf || !surf->buffer) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SURFACE;
   }

   if (surf->coded_buf) {
      surf->coded_buf->coded_surf = NULL;
      surf->coded_buf = NULL;
   }

   /* Encode only reads from the surface and doesn't set surface fence. */
   if (context->templat.entrypoint != PIPE_VIDEO_ENTRYPOINT_ENCODE)
      vlVaSetSurfaceContext(drv, surf, context);

   context->target_id = render_target;
   context->target = surf->buffer;

   if (context->templat.entrypoint != PIPE_VIDEO_ENTRYPOINT_ENCODE)
      context->needs_begin_frame = true;

   if (!context->decoder) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_SUCCESS;
   }

   /* meta data and seis are per picture basis, it needs to be
    * cleared before rendering the picture. */
   if (context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) {
      switch (u_reduce_video_profile(context->templat.profile)) {
         case PIPE_VIDEO_FORMAT_AV1:
            context->desc.av1enc.metadata_flags.value = 0;
            context->desc.av1enc.roi.num = 0;
            context->desc.av1enc.intra_refresh.mode = INTRA_REFRESH_MODE_NONE;
            break;
         case PIPE_VIDEO_FORMAT_HEVC:
            context->desc.h265enc.roi.num = 0;
            context->desc.h265enc.intra_refresh.mode = INTRA_REFRESH_MODE_NONE;
            break;
         case PIPE_VIDEO_FORMAT_MPEG4_AVC:
            context->desc.h264enc.roi.num = 0;
            context->desc.h264enc.intra_refresh.mode = INTRA_REFRESH_MODE_NONE;
            break;
         default:
            break;
      }
   }

   context->slice_data_offset = 0;
   context->have_slice_params = false;

   mtx_unlock(&drv->mutex);
   return VA_STATUS_SUCCESS;
}

void
vlVaGetReferenceFrame(vlVaDriver *drv, VASurfaceID surface_id,
                      struct pipe_video_buffer **ref_frame)
{
   vlVaSurface *surf = handle_table_get(drv->htab, surface_id);
   if (surf)
      *ref_frame = vlVaGetSurfaceBuffer(drv, surf);
   else
      *ref_frame = NULL;
}
/*
 * in->quality = 0; without any settings, it is using speed preset
 *                  and no preencode and no vbaq. It is the fastest setting.
 * in->quality = 1; suggested setting, with balanced preset, and
 *                  preencode and vbaq
 * in->quality = others; it is the customized setting
 *                  with valid bit (bit #0) set to "1"
 *                  for example:
 *
 *                  0x3  (balance preset, no pre-encoding, no vbaq)
 *                  0x13 (balanced preset, no pre-encoding, vbaq)
 *                  0x13 (balanced preset, no pre-encoding, vbaq)
 *                  0x9  (speed preset, pre-encoding, no vbaq)
 *                  0x19 (speed preset, pre-encoding, vbaq)
 *
 *                  The quality value has to be treated as a combination
 *                  of preset mode, pre-encoding and vbaq settings.
 *                  The quality and speed could be vary according to
 *                  different settings,
 */
void
vlVaHandleVAEncMiscParameterTypeQualityLevel(struct pipe_enc_quality_modes *p, vlVaQualityBits *in)
{
   if (!in->quality) {
      p->level = 0;
      p->preset_mode = PRESET_MODE_SPEED;
      p->pre_encode_mode = PREENCODING_MODE_DISABLE;
      p->vbaq_mode = VBAQ_DISABLE;

      return;
   }

   if (p->level != in->quality) {
      if (in->quality == 1) {
         p->preset_mode = PRESET_MODE_BALANCE;
         p->pre_encode_mode = PREENCODING_MODE_DEFAULT;
         p->vbaq_mode = VBAQ_AUTO;
      } else {
         p->preset_mode = in->preset_mode;
         p->pre_encode_mode = in->pre_encode_mode;
         p->vbaq_mode = in->vbaq_mode;
      }
   }
   p->level = in->quality;
}

static VAStatus
handlePictureParameterBuffer(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAStatus vaStatus = VA_STATUS_SUCCESS;
   enum pipe_video_format format =
      u_reduce_video_profile(context->templat.profile);

   switch (format) {
   case PIPE_VIDEO_FORMAT_MPEG12:
      vlVaHandlePictureParameterBufferMPEG12(drv, context, buf);
      break;

   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      vlVaHandlePictureParameterBufferH264(drv, context, buf);
      break;

   case PIPE_VIDEO_FORMAT_VC1:
      vlVaHandlePictureParameterBufferVC1(drv, context, buf);
      break;

   case PIPE_VIDEO_FORMAT_MPEG4:
      vlVaHandlePictureParameterBufferMPEG4(drv, context, buf);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      vlVaHandlePictureParameterBufferHEVC(drv, context, buf);
      break;

   case PIPE_VIDEO_FORMAT_JPEG:
      vlVaHandlePictureParameterBufferMJPEG(drv, context, buf);
      break;

   case PIPE_VIDEO_FORMAT_VP9:
      vlVaHandlePictureParameterBufferVP9(drv, context, buf);
      break;

   case PIPE_VIDEO_FORMAT_AV1:
      vlVaHandlePictureParameterBufferAV1(drv, context, buf);
      break;

   default:
      break;
   }

   /* Create the decoder once max_references is known. */
   if (!context->decoder) {
      if (!context->target)
         return VA_STATUS_ERROR_INVALID_CONTEXT;

      mtx_lock(&context->mutex);

      if (format == PIPE_VIDEO_FORMAT_MPEG4_AVC)
         context->templat.level = u_get_h264_level(context->templat.width,
            context->templat.height, &context->templat.max_references);

      context->decoder = drv->pipe->create_video_codec(drv->pipe,
         &context->templat);

      mtx_unlock(&context->mutex);

      if (!context->decoder)
         return VA_STATUS_ERROR_ALLOCATION_FAILED;

      context->needs_begin_frame = true;
   }

   if (format == PIPE_VIDEO_FORMAT_VP9) {
      context->decoder->width =
         context->desc.vp9.picture_parameter.frame_width;
      context->decoder->height =
         context->desc.vp9.picture_parameter.frame_height;
   }

   return vaStatus;
}

static void
handleIQMatrixBuffer(vlVaContext *context, vlVaBuffer *buf)
{
   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG12:
      vlVaHandleIQMatrixBufferMPEG12(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      vlVaHandleIQMatrixBufferH264(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_MPEG4:
      vlVaHandleIQMatrixBufferMPEG4(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      vlVaHandleIQMatrixBufferHEVC(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_JPEG:
      vlVaHandleIQMatrixBufferMJPEG(context, buf);
      break;

   default:
      break;
   }
}

static void
handleSliceParameterBuffer(vlVaContext *context, vlVaBuffer *buf)
{
   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG12:
      vlVaHandleSliceParameterBufferMPEG12(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_VC1:
      vlVaHandleSliceParameterBufferVC1(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      vlVaHandleSliceParameterBufferH264(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_MPEG4:
      vlVaHandleSliceParameterBufferMPEG4(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      vlVaHandleSliceParameterBufferHEVC(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_JPEG:
      vlVaHandleSliceParameterBufferMJPEG(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_VP9:
      vlVaHandleSliceParameterBufferVP9(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_AV1:
      vlVaHandleSliceParameterBufferAV1(context, buf);
      break;

   default:
      break;
   }
}

static unsigned int
bufHasStartcode(vlVaBuffer *buf, unsigned int code, unsigned int bits)
{
   struct vl_vlc vlc = {0};
   int i;

   /* search the first 64 bytes for a startcode */
   vl_vlc_init(&vlc, 1, (const void * const*)&buf->data, &buf->size);
   for (i = 0; i < 64 && vl_vlc_bits_left(&vlc) >= bits; ++i) {
      if (vl_vlc_peekbits(&vlc, bits) == code)
         return 1;
      vl_vlc_eatbits(&vlc, 8);
      vl_vlc_fillbits(&vlc);
   }

   return 0;
}

static VAStatus
handleVAProtectedSliceDataBufferType(vlVaContext *context, vlVaBuffer *buf)
{
   uint8_t *encrypted_data = (uint8_t*)buf->data;
   uint8_t *drm_key;
   unsigned int drm_key_size = buf->size;

   if (!context->desc.base.protected_playback)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drm_key = REALLOC(context->desc.base.decrypt_key,
         context->desc.base.key_size, drm_key_size);
   if (!drm_key)
      return VA_STATUS_ERROR_ALLOCATION_FAILED;
   context->desc.base.decrypt_key = drm_key;
   memcpy(context->desc.base.decrypt_key, encrypted_data, drm_key_size);
   context->desc.base.key_size = drm_key_size;

   return VA_STATUS_SUCCESS;
}

static VAStatus
handleVASliceDataBufferType(vlVaContext *context, vlVaBuffer *buf)
{
   enum pipe_video_format format = u_reduce_video_profile(context->templat.profile);
   static const uint8_t start_code_h264[] = { 0x00, 0x00, 0x01 };
   static const uint8_t start_code_h265[] = { 0x00, 0x00, 0x01 };
   static const uint8_t start_code_vc1_frame[] = { 0x00, 0x00, 0x01, 0x0d };
   static const uint8_t start_code_vc1_field[] = { 0x00, 0x00, 0x01, 0x0c };
   static const uint8_t start_code_vc1_slice[] = { 0x00, 0x00, 0x01, 0x0b };
   static const uint8_t eoi_jpeg[] = { 0xff, 0xd9 };

   if (!context->decoder)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (context->bs.allocated_size - context->bs.num_buffers < 3) {
      context->bs.buffers = REALLOC(context->bs.buffers,
                                    context->bs.allocated_size * sizeof(*context->bs.buffers),
                                    (context->bs.allocated_size + 3) * sizeof(*context->bs.buffers));
      context->bs.sizes = REALLOC(context->bs.sizes,
                                  context->bs.allocated_size * sizeof(*context->bs.sizes),
                                  (context->bs.allocated_size + 3) * sizeof(*context->bs.sizes));
      context->bs.allocated_size += 3;
   }

   format = u_reduce_video_profile(context->templat.profile);
   if (!context->desc.base.protected_playback) {
      switch (format) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
         if (bufHasStartcode(buf, 0x000001, 24))
            break;

         context->bs.buffers[context->bs.num_buffers] = (void *const)&start_code_h264;
         context->bs.sizes[context->bs.num_buffers++] = sizeof(start_code_h264);
         break;
      case PIPE_VIDEO_FORMAT_HEVC:
         if (bufHasStartcode(buf, 0x000001, 24))
            break;

         context->bs.buffers[context->bs.num_buffers] = (void *const)&start_code_h265;
         context->bs.sizes[context->bs.num_buffers++] = sizeof(start_code_h265);
         break;
      case PIPE_VIDEO_FORMAT_VC1:
         if (bufHasStartcode(buf, 0x000001, 24))
            break;

         if (context->decoder->profile == PIPE_VIDEO_PROFILE_VC1_ADVANCED) {
            const uint8_t *start_code;
            if (context->slice_data_offset)
               start_code = start_code_vc1_slice;
            else if (context->desc.vc1.is_first_field)
               start_code = start_code_vc1_frame;
            else
               start_code = start_code_vc1_field;
            context->bs.buffers[context->bs.num_buffers] = (void *const)start_code;
            context->bs.sizes[context->bs.num_buffers++] = sizeof(start_code_vc1_frame);
         }
         break;
      case PIPE_VIDEO_FORMAT_MPEG4:
         if (bufHasStartcode(buf, 0x000001, 24))
            break;

         vlVaDecoderFixMPEG4Startcode(context);
         context->bs.buffers[context->bs.num_buffers] = (void *)context->mpeg4.start_code;
         context->bs.sizes[context->bs.num_buffers++] = context->mpeg4.start_code_size;
         break;
      case PIPE_VIDEO_FORMAT_JPEG:
         if (bufHasStartcode(buf, 0xffd8ffdb, 32))
            break;

         vlVaGetJpegSliceHeader(context);
         context->bs.buffers[context->bs.num_buffers] = (void *)context->mjpeg.slice_header;
         context->bs.sizes[context->bs.num_buffers++] = context->mjpeg.slice_header_size;
         break;
      case PIPE_VIDEO_FORMAT_VP9:
         vlVaDecoderVP9BitstreamHeader(context, buf);
         break;
      case PIPE_VIDEO_FORMAT_AV1:
         break;
      default:
         break;
      }
   }

   context->bs.buffers[context->bs.num_buffers] = buf->data;
   context->bs.sizes[context->bs.num_buffers++] = buf->size;

   if (format == PIPE_VIDEO_FORMAT_JPEG) {
      context->bs.buffers[context->bs.num_buffers] = (void *const)&eoi_jpeg;
      context->bs.sizes[context->bs.num_buffers++] = sizeof(eoi_jpeg);
   }

   if (context->needs_begin_frame) {
      context->decoder->begin_frame(context->decoder, context->target,
         &context->desc.base);
      context->needs_begin_frame = false;
   }
   return VA_STATUS_SUCCESS;
}

static VAStatus
handleVAEncMiscParameterTypeRateControl(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncMiscParameterTypeRateControlH264(context, misc);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncMiscParameterTypeRateControlHEVC(context, misc);
      break;

#if VA_CHECK_VERSION(1, 16, 0)
   case PIPE_VIDEO_FORMAT_AV1:
      status = vlVaHandleVAEncMiscParameterTypeRateControlAV1(context, misc);
      break;
#endif
   default:
      break;
   }

   return status;
}

static VAStatus
handleVAEncMiscParameterTypeFrameRate(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncMiscParameterTypeFrameRateH264(context, misc);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncMiscParameterTypeFrameRateHEVC(context, misc);
      break;

#if VA_CHECK_VERSION(1, 16, 0)
   case PIPE_VIDEO_FORMAT_AV1:
      status = vlVaHandleVAEncMiscParameterTypeFrameRateAV1(context, misc);
      break;
#endif
   default:
      break;
   }

   return status;
}

static VAStatus
handleVAEncMiscParameterTypeTemporalLayer(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncMiscParameterTypeTemporalLayerH264(context, misc);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncMiscParameterTypeTemporalLayerHEVC(context, misc);
      break;

   default:
      break;
   }

   return status;
}

static VAStatus
handleVAEncSequenceParameterBufferType(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncSequenceParameterBufferTypeH264(drv, context, buf);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncSequenceParameterBufferTypeHEVC(drv, context, buf);
      break;

#if VA_CHECK_VERSION(1, 16, 0)
   case PIPE_VIDEO_FORMAT_AV1:
      status = vlVaHandleVAEncSequenceParameterBufferTypeAV1(drv, context, buf);
      break;
#endif

   default:
      break;
   }

   return status;
}

static VAStatus
handleVAEncMiscParameterTypeQualityLevel(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncMiscParameterTypeQualityLevelH264(context, misc);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncMiscParameterTypeQualityLevelHEVC(context, misc);
      break;

#if VA_CHECK_VERSION(1, 16, 0)
   case PIPE_VIDEO_FORMAT_AV1:
      status = vlVaHandleVAEncMiscParameterTypeQualityLevelAV1(context, misc);
      break;
#endif

   default:
      break;
   }

   return status;
}

static VAStatus
handleVAEncMiscParameterTypeMaxFrameSize(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncMiscParameterTypeMaxFrameSizeH264(context, misc);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncMiscParameterTypeMaxFrameSizeHEVC(context, misc);
      break;

#if VA_CHECK_VERSION(1, 16, 0)
   case PIPE_VIDEO_FORMAT_AV1:
      status = vlVaHandleVAEncMiscParameterTypeMaxFrameSizeAV1(context, misc);
      break;
#endif

   default:
      break;
   }

   return status;
}
static VAStatus
handleVAEncMiscParameterTypeHRD(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncMiscParameterTypeHRDH264(context, misc);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncMiscParameterTypeHRDHEVC(context, misc);
      break;

#if VA_CHECK_VERSION(1, 16, 0)
   case PIPE_VIDEO_FORMAT_AV1:
      status = vlVaHandleVAEncMiscParameterTypeHRDAV1(context, misc);
      break;
#endif

   default:
      break;
   }

   return status;
}

static VAStatus
handleVAEncMiscParameterTypeMaxSliceSize(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAStatus status = VA_STATUS_SUCCESS;
   VAEncMiscParameterMaxSliceSize *max_slice_size_buffer = (VAEncMiscParameterMaxSliceSize *)misc->data;
   switch (u_reduce_video_profile(context->templat.profile)) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         context->desc.h264enc.slice_mode = PIPE_VIDEO_SLICE_MODE_MAX_SLICE_SIZE;
         context->desc.h264enc.max_slice_bytes = max_slice_size_buffer->max_slice_size;
      } break;
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         context->desc.h265enc.slice_mode = PIPE_VIDEO_SLICE_MODE_MAX_SLICE_SIZE;
         context->desc.h265enc.max_slice_bytes = max_slice_size_buffer->max_slice_size;
      } break;
      default:
         break;
   }
   return status;
}

static VAStatus
handleVAEncMiscParameterTypeRIR(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAStatus status = VA_STATUS_SUCCESS;
   struct pipe_enc_intra_refresh *p_intra_refresh = NULL;

   switch (u_reduce_video_profile(context->templat.profile)) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
         p_intra_refresh = &context->desc.h264enc.intra_refresh;
         break;
      case PIPE_VIDEO_FORMAT_HEVC:
         p_intra_refresh = &context->desc.h265enc.intra_refresh;
         break;
#if VA_CHECK_VERSION(1, 16, 0)
      case PIPE_VIDEO_FORMAT_AV1:
         p_intra_refresh = &context->desc.av1enc.intra_refresh;
         break;
#endif
      default:
         return status;
   };

   VAEncMiscParameterRIR *ir = (VAEncMiscParameterRIR *)misc->data;

   if (ir->rir_flags.value == VA_ENC_INTRA_REFRESH_ROLLING_ROW)
      p_intra_refresh->mode = INTRA_REFRESH_MODE_UNIT_ROWS;
   else if (ir->rir_flags.value == VA_ENC_INTRA_REFRESH_ROLLING_COLUMN)
      p_intra_refresh->mode = INTRA_REFRESH_MODE_UNIT_COLUMNS;
   else if (ir->rir_flags.value) /* if any other values to use the default one*/
      p_intra_refresh->mode = INTRA_REFRESH_MODE_UNIT_COLUMNS;
   else /* if no mode specified then no intra-refresh */
      p_intra_refresh->mode = INTRA_REFRESH_MODE_NONE;

   /* intra refresh should be started with sequence level headers */
   p_intra_refresh->need_sequence_header = 0;
   if (p_intra_refresh->mode) {
      p_intra_refresh->region_size = ir->intra_insert_size;
      p_intra_refresh->offset = ir->intra_insertion_location;
      if (p_intra_refresh->offset == 0)
         p_intra_refresh->need_sequence_header = 1;
   }

   return status;
}

static VAStatus
handleVAEncMiscParameterTypeROI(vlVaContext *context, VAEncMiscParameterBuffer *misc)
{
   VAStatus status = VA_STATUS_SUCCESS;
   struct pipe_enc_roi *proi= NULL;
   switch (u_reduce_video_profile(context->templat.profile)) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
         proi = &context->desc.h264enc.roi;
         break;
      case PIPE_VIDEO_FORMAT_HEVC:
         proi = &context->desc.h265enc.roi;
         break;
#if VA_CHECK_VERSION(1, 16, 0)
      case PIPE_VIDEO_FORMAT_AV1:
         proi = &context->desc.av1enc.roi;
         break;
#endif
      default:
         break;
   };

   if (proi) {
      VAEncMiscParameterBufferROI *roi = (VAEncMiscParameterBufferROI *)misc->data;
      /* do not support priority type, and the maximum region is 32  */
      if ((roi->num_roi > 0 && roi->roi_flags.bits.roi_value_is_qp_delta == 0)
           || roi->num_roi > PIPE_ENC_ROI_REGION_NUM_MAX)
         status = VA_STATUS_ERROR_FLAG_NOT_SUPPORTED;
      else {
         uint32_t i;
         VAEncROI *src = roi->roi;

         proi->num = roi->num_roi;
         for (i = 0; i < roi->num_roi; i++) {
            proi->region[i].valid = true;
            proi->region[i].x = src->roi_rectangle.x;
            proi->region[i].y = src->roi_rectangle.y;
            proi->region[i].width = src->roi_rectangle.width;
            proi->region[i].height = src->roi_rectangle.height;
            proi->region[i].qp_value = (int32_t)CLAMP(src->roi_value, roi->min_delta_qp, roi->max_delta_qp);
            src++;
         }

         for (; i < PIPE_ENC_ROI_REGION_NUM_MAX; i++)
            proi->region[i].valid = false;
      }
   }

   return status;
}

static VAStatus
handleVAEncMiscParameterBufferType(vlVaContext *context, vlVaBuffer *buf)
{
   VAStatus vaStatus = VA_STATUS_SUCCESS;
   VAEncMiscParameterBuffer *misc;
   misc = buf->data;

   switch (misc->type) {
   case VAEncMiscParameterTypeRateControl:
      vaStatus = handleVAEncMiscParameterTypeRateControl(context, misc);
      break;

   case VAEncMiscParameterTypeFrameRate:
      vaStatus = handleVAEncMiscParameterTypeFrameRate(context, misc);
      break;

   case VAEncMiscParameterTypeTemporalLayerStructure:
      vaStatus = handleVAEncMiscParameterTypeTemporalLayer(context, misc);
      break;

   case VAEncMiscParameterTypeQualityLevel:
      vaStatus = handleVAEncMiscParameterTypeQualityLevel(context, misc);
      break;

   case VAEncMiscParameterTypeMaxFrameSize:
      vaStatus = handleVAEncMiscParameterTypeMaxFrameSize(context, misc);
      break;

   case VAEncMiscParameterTypeHRD:
      vaStatus = handleVAEncMiscParameterTypeHRD(context, misc);
      break;

   case VAEncMiscParameterTypeRIR:
      vaStatus = handleVAEncMiscParameterTypeRIR(context, misc);
      break;

   case VAEncMiscParameterTypeMaxSliceSize:
      vaStatus = handleVAEncMiscParameterTypeMaxSliceSize(context, misc);
      break;

   case VAEncMiscParameterTypeROI:
      vaStatus = handleVAEncMiscParameterTypeROI(context, misc);
      break;

   default:
      break;
   }

   return vaStatus;
}

static VAStatus
handleVAEncPictureParameterBufferType(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncPictureParameterBufferTypeH264(drv, context, buf);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncPictureParameterBufferTypeHEVC(drv, context, buf);
      break;

#if VA_CHECK_VERSION(1, 16, 0)
   case PIPE_VIDEO_FORMAT_AV1:
      status = vlVaHandleVAEncPictureParameterBufferTypeAV1(drv, context, buf);
      break;
#endif

   default:
      break;
   }

   return status;
}

static VAStatus
handleVAEncSliceParameterBufferType(vlVaDriver *drv, vlVaContext *context, vlVaBuffer *buf)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncSliceParameterBufferTypeH264(drv, context, buf);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncSliceParameterBufferTypeHEVC(drv, context, buf);
      break;

#if VA_CHECK_VERSION(1, 16, 0)
   case PIPE_VIDEO_FORMAT_AV1:
      status = vlVaHandleVAEncSliceParameterBufferTypeAV1(drv, context, buf);
      break;
#endif

   default:
      break;
   }

   return status;
}

static VAStatus
handleVAEncPackedHeaderParameterBufferType(vlVaContext *context, vlVaBuffer *buf)
{
   VAEncPackedHeaderParameterBuffer *param = buf->data;

   context->packed_header_emulation_bytes = param->has_emulation_bytes;
   context->packed_header_type = param->type;

   return VA_STATUS_SUCCESS;
}

static VAStatus
handleVAEncPackedHeaderDataBufferType(vlVaContext *context, vlVaBuffer *buf)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      status = vlVaHandleVAEncPackedHeaderDataBufferTypeH264(context, buf);
      break;

   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncPackedHeaderDataBufferTypeHEVC(context, buf);
      break;

#if VA_CHECK_VERSION(1, 16, 0)
   case PIPE_VIDEO_FORMAT_AV1:
      status = vlVaHandleVAEncPackedHeaderDataBufferTypeAV1(context, buf);
      break;
#endif

   default:
      break;
   }

   return status;
}

static VAStatus
handleVAStatsStatisticsBufferType(VADriverContextP ctx, vlVaContext *context, vlVaBuffer *buf)
{
   if (context->decoder->entrypoint != PIPE_VIDEO_ENTRYPOINT_ENCODE)
      return VA_STATUS_ERROR_UNIMPLEMENTED;

   vlVaDriver *drv;
   drv = VL_VA_DRIVER(ctx);

   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (!buf->derived_surface.resource)
      buf->derived_surface.resource = pipe_buffer_create(drv->pipe->screen, PIPE_BIND_VERTEX_BUFFER,
                                            PIPE_USAGE_STREAM, buf->size);

   context->target->statistics_data = buf->derived_surface.resource;

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaRenderPicture(VADriverContextP ctx, VAContextID context_id, VABufferID *buffers, int num_buffers)
{
   vlVaDriver *drv;
   vlVaContext *context;
   VAStatus vaStatus = VA_STATUS_SUCCESS;

   unsigned i;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   mtx_lock(&drv->mutex);
   context = handle_table_get(drv->htab, context_id);
   if (!context) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_CONTEXT;
   }

   if (!context->target_id) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_OPERATION_FAILED;
   }

   for (i = 0; i < num_buffers && vaStatus == VA_STATUS_SUCCESS; ++i) {
      vlVaBuffer *buf = handle_table_get(drv->htab, buffers[i]);
      if (!buf) {
         mtx_unlock(&drv->mutex);
         return VA_STATUS_ERROR_INVALID_BUFFER;
      }

      switch (buf->type) {
      case VAPictureParameterBufferType:
         vaStatus = handlePictureParameterBuffer(drv, context, buf);
         break;

      case VAIQMatrixBufferType:
         handleIQMatrixBuffer(context, buf);
         break;

      case VASliceParameterBufferType:
         handleSliceParameterBuffer(context, buf);
         context->have_slice_params = true;
         break;

      case VASliceDataBufferType:
         vaStatus = handleVASliceDataBufferType(context, buf);
         /* Workaround for apps sending single slice data buffer followed
          * by multiple slice parameter buffers. */
         if (context->have_slice_params)
            context->slice_data_offset += buf->size;
         break;

      case VAProcPipelineParameterBufferType:
         vaStatus = vlVaHandleVAProcPipelineParameterBufferType(drv, context, buf);
         break;

      case VAEncSequenceParameterBufferType:
         vaStatus = handleVAEncSequenceParameterBufferType(drv, context, buf);
         break;

      case VAEncMiscParameterBufferType:
         vaStatus = handleVAEncMiscParameterBufferType(context, buf);
         break;

      case VAEncPictureParameterBufferType:
         vaStatus = handleVAEncPictureParameterBufferType(drv, context, buf);
         break;

      case VAEncSliceParameterBufferType:
         vaStatus = handleVAEncSliceParameterBufferType(drv, context, buf);
         break;

      case VAHuffmanTableBufferType:
         vlVaHandleHuffmanTableBufferType(context, buf);
         break;

      case VAEncPackedHeaderParameterBufferType:
         handleVAEncPackedHeaderParameterBufferType(context, buf);
         break;
      case VAEncPackedHeaderDataBufferType:
         handleVAEncPackedHeaderDataBufferType(context, buf);
         break;

      case VAStatsStatisticsBufferType:
         handleVAStatsStatisticsBufferType(ctx, context, buf);
         break;

      case VAProtectedSliceDataBufferType:
         vaStatus = handleVAProtectedSliceDataBufferType(context, buf);
         break;

      default:
         break;
      }
   }

   if (context->decoder &&
       context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM &&
       context->bs.num_buffers) {
      context->decoder->decode_bitstream(context->decoder, context->target, &context->desc.base,
         context->bs.num_buffers, (const void * const*)context->bs.buffers, context->bs.sizes);
      context->bs.num_buffers = 0;
   }

   mtx_unlock(&drv->mutex);

   return vaStatus;
}

static bool vlVaQueryApplyFilmGrainAV1(vlVaContext *context,
                                 int *output_id,
                                 struct pipe_video_buffer ***out_target)
{
   struct pipe_av1_picture_desc *av1 = NULL;

   if (u_reduce_video_profile(context->templat.profile) != PIPE_VIDEO_FORMAT_AV1 ||
       context->decoder->entrypoint != PIPE_VIDEO_ENTRYPOINT_BITSTREAM)
      return false;

   av1 = &context->desc.av1;
   if (!av1->picture_parameter.film_grain_info.film_grain_info_fields.apply_grain)
      return false;

   *output_id = av1->picture_parameter.current_frame_id;
   *out_target = &av1->film_grain_target;
   return true;
}

static void vlVaClearRawHeaders(struct util_dynarray *headers)
{
   util_dynarray_foreach(headers, struct pipe_enc_raw_header, header)
      FREE(header->buffer);
   util_dynarray_clear(headers);
}

VAStatus
vlVaEndPicture(VADriverContextP ctx, VAContextID context_id)
{
   vlVaDriver *drv;
   vlVaContext *context;
   vlVaBuffer *coded_buf;
   vlVaSurface *surf;
   void *feedback = NULL;
   struct pipe_screen *screen;
   bool apply_av1_fg = false;
   struct pipe_video_buffer **out_target;
   int output_id;
   enum pipe_format target_format;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   mtx_lock(&drv->mutex);
   context = handle_table_get(drv->htab, context_id);
   if (!context) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_CONTEXT;
   }

   if (!context->target_id) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_OPERATION_FAILED;
   }

   output_id = context->target_id;
   context->target_id = 0;

   if (!context->decoder) {
      if (context->templat.profile != PIPE_VIDEO_PROFILE_UNKNOWN) {
         mtx_unlock(&drv->mutex);
         return VA_STATUS_ERROR_INVALID_CONTEXT;
      }

      /* VPP */
      mtx_unlock(&drv->mutex);
      return VA_STATUS_SUCCESS;
   }

   if (context->needs_begin_frame) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_OPERATION_FAILED;
   }

   out_target = &context->target;
   apply_av1_fg = vlVaQueryApplyFilmGrainAV1(context, &output_id, &out_target);

   surf = handle_table_get(drv->htab, output_id);
   vlVaGetSurfaceBuffer(drv, surf);
   if (!surf || !surf->buffer) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SURFACE;
   }

   if (apply_av1_fg) {
      vlVaSetSurfaceContext(drv, surf, context);
      *out_target = surf->buffer;
   }

   context->mpeg4.frame_num++;

   screen = context->decoder->context->screen;

   if ((bool)(surf->templat.bind & PIPE_BIND_PROTECTED) != context->desc.base.protected_playback) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SURFACE;
   }

   target_format = context->target->buffer_format;

   if (context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) {
      coded_buf = context->coded_buf;
      context->desc.base.fence = &coded_buf->fence;
      if (u_reduce_video_profile(context->templat.profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC)
         context->desc.h264enc.frame_num_cnt++;

      if (surf->efc_surface) {
         assert(surf == drv->last_efc_surface);
         context->target = surf->efc_surface->buffer;
         context->desc.base.input_format = surf->efc_surface->buffer->buffer_format;
         context->desc.base.output_format = surf->buffer->buffer_format;
         surf->efc_surface = NULL;
         drv->last_efc_surface = NULL;
      } else {
         context->desc.base.input_format = surf->buffer->buffer_format;
         context->desc.base.output_format = surf->buffer->buffer_format;
      }
      context->desc.base.input_full_range = surf->full_range;
      target_format = context->desc.base.output_format;

      if (coded_buf->coded_surf)
         coded_buf->coded_surf->coded_buf = NULL;
      vlVaGetBufferFeedback(coded_buf);
      vlVaSetBufferContext(drv, coded_buf, context);

      int driver_metadata_support = drv->pipe->screen->get_video_param(drv->pipe->screen,
                                                                       context->decoder->profile,
                                                                       context->decoder->entrypoint,
                                                                       PIPE_VIDEO_CAP_ENC_SUPPORTS_FEEDBACK_METADATA);
      if (u_reduce_video_profile(context->templat.profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC)
         context->desc.h264enc.requested_metadata = driver_metadata_support;
      else if (u_reduce_video_profile(context->templat.profile) == PIPE_VIDEO_FORMAT_HEVC)
         context->desc.h265enc.requested_metadata = driver_metadata_support;
      else if (u_reduce_video_profile(context->templat.profile) == PIPE_VIDEO_FORMAT_AV1)
         context->desc.av1enc.requested_metadata = driver_metadata_support;

      context->decoder->begin_frame(context->decoder, context->target, &context->desc.base);
      context->decoder->encode_bitstream(context->decoder, context->target,
                                         coded_buf->derived_surface.resource, &feedback);
      coded_buf->feedback = feedback;
      coded_buf->coded_surf = surf;
      surf->coded_buf = coded_buf;
   } else if (context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM) {
      context->desc.base.fence = &surf->fence;
   } else if (context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_PROCESSING) {
      context->desc.base.fence = &surf->fence;
   }

   if (screen->is_video_target_buffer_supported &&
       !screen->is_video_target_buffer_supported(screen,
                                                 target_format,
                                                 context->target,
                                                 context->decoder->profile,
                                                 context->decoder->entrypoint)) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_SURFACE;
   }

   /* when there are external handles, we can't set PIPE_FLUSH_ASYNC */
   if (context->desc.base.fence)
      context->desc.base.flush_flags = drv->has_external_handles ? 0 : PIPE_FLUSH_ASYNC;

   if (context->decoder->end_frame(context->decoder, context->target, &context->desc.base) != 0) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_OPERATION_FAILED;
   }

   if (drv->pipe->screen->get_video_param(drv->pipe->screen,
                           context->decoder->profile,
                           context->decoder->entrypoint,
                           PIPE_VIDEO_CAP_REQUIRES_FLUSH_ON_END_FRAME))
      context->decoder->flush(context->decoder);

   if (context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) {
      switch (u_reduce_video_profile(context->templat.profile)) {
      case PIPE_VIDEO_FORMAT_AV1:
         context->desc.av1enc.frame_num++;
         vlVaClearRawHeaders(&context->desc.av1enc.raw_headers);
         break;
      case PIPE_VIDEO_FORMAT_HEVC:
         context->desc.h265enc.frame_num++;
         vlVaClearRawHeaders(&context->desc.h265enc.raw_headers);
         break;
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
         if (!context->desc.h264enc.not_referenced)
            context->desc.h264enc.frame_num++;
         vlVaClearRawHeaders(&context->desc.h264enc.raw_headers);
         break;
      default:
         break;
      }
   }

   mtx_unlock(&drv->mutex);
   return VA_STATUS_SUCCESS;
}

void
vlVaAddRawHeader(struct util_dynarray *headers, uint8_t type, uint32_t size,
                 uint8_t *buf, bool is_slice, uint32_t emulation_bytes_start)
{
   struct pipe_enc_raw_header header = {
      .type = type,
      .is_slice = is_slice,
   };
   if (emulation_bytes_start) {
      uint32_t pos = emulation_bytes_start, num_zeros = 0;
      header.buffer = MALLOC(size * 3 / 2);
      memcpy(header.buffer, buf, emulation_bytes_start);
      for (uint32_t i = emulation_bytes_start; i < size; i++) {
         uint8_t byte = buf[i];
         if (num_zeros >= 2 && byte <= 0x03) {
            header.buffer[pos++] = 0x03;
            num_zeros = 0;
         }
         header.buffer[pos++] = byte;
         num_zeros = byte == 0x00 ? num_zeros + 1 : 0;
      }
      header.size = pos;
   } else {
      header.size = size;
      header.buffer = MALLOC(header.size);
      memcpy(header.buffer, buf, size);
   }
   util_dynarray_append(headers, struct pipe_enc_raw_header, header);
}
