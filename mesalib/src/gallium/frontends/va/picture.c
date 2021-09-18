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

#include "vl/vl_vlc.h"
#include "vl/vl_winsys.h"

#include "va_private.h"

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
   mtx_unlock(&drv->mutex);
   if (!surf || !surf->buffer)
      return VA_STATUS_ERROR_INVALID_SURFACE;

   context->target_id = render_target;
   surf->ctx = context_id;
   context->target = surf->buffer;
   context->mjpeg.sampling_factor = 0;

   if (!context->decoder) {

      /* VPP */
      if (context->templat.profile == PIPE_VIDEO_PROFILE_UNKNOWN &&
          context->target->buffer_format != PIPE_FORMAT_B8G8R8A8_UNORM &&
          context->target->buffer_format != PIPE_FORMAT_R8G8B8A8_UNORM &&
          context->target->buffer_format != PIPE_FORMAT_B8G8R8X8_UNORM &&
          context->target->buffer_format != PIPE_FORMAT_R8G8B8X8_UNORM &&
          context->target->buffer_format != PIPE_FORMAT_NV12 &&
          context->target->buffer_format != PIPE_FORMAT_P010 &&
          context->target->buffer_format != PIPE_FORMAT_P016)
         return VA_STATUS_ERROR_UNIMPLEMENTED;

      return VA_STATUS_SUCCESS;
   }

   if (context->decoder->entrypoint != PIPE_VIDEO_ENTRYPOINT_ENCODE)
      context->needs_begin_frame = true;

   return VA_STATUS_SUCCESS;
}

void
vlVaGetReferenceFrame(vlVaDriver *drv, VASurfaceID surface_id,
                      struct pipe_video_buffer **ref_frame)
{
   vlVaSurface *surf = handle_table_get(drv->htab, surface_id);
   if (surf)
      *ref_frame = surf->buffer;
   else
      *ref_frame = NULL;
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

      if (format == PIPE_VIDEO_FORMAT_MPEG4_AVC)
         context->templat.level = u_get_h264_level(context->templat.width,
            context->templat.height, &context->templat.max_references);

      context->decoder = drv->pipe->create_video_codec(drv->pipe,
         &context->templat);

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
handleSliceParameterBuffer(vlVaContext *context, vlVaBuffer *buf, unsigned num)
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
      vlVaHandleSliceParameterBufferAV1(context, buf, num);
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

static void
handleVAProtectedSliceDataBufferType(vlVaContext *context, vlVaBuffer *buf)
{
	uint8_t* encrypted_data = (uint8_t*) buf->data;

	unsigned int drm_key_size = buf->size;

	context->desc.base.decrypt_key = CALLOC(1, drm_key_size);
	memcpy(context->desc.base.decrypt_key, encrypted_data, drm_key_size);
	context->desc.base.protected_playback = true;
}

static void
handleVASliceDataBufferType(vlVaContext *context, vlVaBuffer *buf)
{
   enum pipe_video_format format = u_reduce_video_profile(context->templat.profile);
   unsigned num_buffers = 0;
   void * const *buffers[3];
   unsigned sizes[3];
   static const uint8_t start_code_h264[] = { 0x00, 0x00, 0x01 };
   static const uint8_t start_code_h265[] = { 0x00, 0x00, 0x01 };
   static const uint8_t start_code_vc1[] = { 0x00, 0x00, 0x01, 0x0d };
   static const uint8_t eoi_jpeg[] = { 0xff, 0xd9 };

   format = u_reduce_video_profile(context->templat.profile);
   if (!context->desc.base.protected_playback) {
      switch (format) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
         if (bufHasStartcode(buf, 0x000001, 24))
            break;

         buffers[num_buffers] = (void *const)&start_code_h264;
         sizes[num_buffers++] = sizeof(start_code_h264);
         break;
      case PIPE_VIDEO_FORMAT_HEVC:
         if (bufHasStartcode(buf, 0x000001, 24))
            break;

         buffers[num_buffers] = (void *const)&start_code_h265;
         sizes[num_buffers++] = sizeof(start_code_h265);
         break;
      case PIPE_VIDEO_FORMAT_VC1:
         if (bufHasStartcode(buf, 0x0000010d, 32) ||
             bufHasStartcode(buf, 0x0000010c, 32) ||
             bufHasStartcode(buf, 0x0000010b, 32))
            break;

         if (context->decoder->profile == PIPE_VIDEO_PROFILE_VC1_ADVANCED) {
            buffers[num_buffers] = (void *const)&start_code_vc1;
            sizes[num_buffers++] = sizeof(start_code_vc1);
         }
         break;
      case PIPE_VIDEO_FORMAT_MPEG4:
         if (bufHasStartcode(buf, 0x000001, 24))
            break;

         vlVaDecoderFixMPEG4Startcode(context);
         buffers[num_buffers] = (void *)context->mpeg4.start_code;
         sizes[num_buffers++] = context->mpeg4.start_code_size;
         break;
      case PIPE_VIDEO_FORMAT_JPEG:
         vlVaGetJpegSliceHeader(context);
         buffers[num_buffers] = (void *)context->mjpeg.slice_header;
         sizes[num_buffers++] = context->mjpeg.slice_header_size;
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

   if (context->desc.base.protected_playback && PIPE_VIDEO_FORMAT_VP9 == format){
        vlVaDecoderVP9BitstreamHeader(context, buf);
        buffers[num_buffers] = buf->data + context->desc.vp9.picture_parameter.frame_header_length_in_bytes;
        sizes[num_buffers] = buf->size - context->desc.vp9.picture_parameter.frame_header_length_in_bytes;
        ++num_buffers;
   } else {
        buffers[num_buffers] = buf->data;
        sizes[num_buffers] = buf->size;
        ++num_buffers;
   }

   if (format == PIPE_VIDEO_FORMAT_JPEG) {
      buffers[num_buffers] = (void *const)&eoi_jpeg;
      sizes[num_buffers++] = sizeof(eoi_jpeg);
   }

   if (context->needs_begin_frame) {
      context->decoder->begin_frame(context->decoder, context->target,
         &context->desc.base);
      context->needs_begin_frame = false;
   }
   context->decoder->decode_bitstream(context->decoder, context->target, &context->desc.base,
      num_buffers, (const void * const*)buffers, sizes);
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

   default:
      break;
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

   default:
      break;
   }

   return status;
}

static VAStatus
handleVAEncPackedHeaderParameterBufferType(vlVaContext *context, vlVaBuffer *buf)
{
   VAStatus status = VA_STATUS_SUCCESS;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_HEVC:
      break;

   default:
      return VA_STATUS_ERROR_UNIMPLEMENTED;
   }

   VAEncPackedHeaderParameterBuffer *param = (VAEncPackedHeaderParameterBuffer *)buf->data;
   if (param->type == VAEncPackedHeaderSequence)
      context->packed_header_type = param->type;
   else
      status = VA_STATUS_ERROR_UNIMPLEMENTED;

   return status;
}

static VAStatus
handleVAEncPackedHeaderDataBufferType(vlVaContext *context, vlVaBuffer *buf)
{
   VAStatus status = VA_STATUS_SUCCESS;

   if (context->packed_header_type != VAEncPackedHeaderSequence)
      return VA_STATUS_ERROR_UNIMPLEMENTED;

   switch (u_reduce_video_profile(context->templat.profile)) {
   case PIPE_VIDEO_FORMAT_HEVC:
      status = vlVaHandleVAEncPackedHeaderDataBufferTypeHEVC(context, buf);
      break;

   default:
      break;
   }

   return status;
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

   /* Always process VAProtectedSliceDataBufferType first because it changes the state */
   for (i = 0; i < num_buffers; ++i) {
      vlVaBuffer *buf = handle_table_get(drv->htab, buffers[i]);
      if (!buf) {
         mtx_unlock(&drv->mutex);
         return VA_STATUS_ERROR_INVALID_BUFFER;
      }

      if (buf->type == VAProtectedSliceDataBufferType)
         handleVAProtectedSliceDataBufferType(context, buf);
   }

   for (i = 0; i < num_buffers && vaStatus == VA_STATUS_SUCCESS; ++i) {
      vlVaBuffer *buf = handle_table_get(drv->htab, buffers[i]);

      switch (buf->type) {
      case VAPictureParameterBufferType:
         vaStatus = handlePictureParameterBuffer(drv, context, buf);
         break;

      case VAIQMatrixBufferType:
         handleIQMatrixBuffer(context, buf);
         break;

      case VASliceParameterBufferType:
         handleSliceParameterBuffer(context, buf, i);
         break;

      case VASliceDataBufferType:
         handleVASliceDataBufferType(context, buf);
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

      default:
         break;
      }
   }
   mtx_unlock(&drv->mutex);

   return vaStatus;
}

VAStatus
vlVaEndPicture(VADriverContextP ctx, VAContextID context_id)
{
   vlVaDriver *drv;
   vlVaContext *context;
   vlVaBuffer *coded_buf;
   vlVaSurface *surf;
   void *feedback;
   struct pipe_screen *screen;
   bool supported;
   bool realloc = false;
   enum pipe_format format;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);
   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   mtx_lock(&drv->mutex);
   context = handle_table_get(drv->htab, context_id);
   mtx_unlock(&drv->mutex);
   if (!context)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   if (!context->decoder) {
      if (context->templat.profile != PIPE_VIDEO_PROFILE_UNKNOWN)
         return VA_STATUS_ERROR_INVALID_CONTEXT;

      /* VPP */
      return VA_STATUS_SUCCESS;
   }

   mtx_lock(&drv->mutex);
   surf = handle_table_get(drv->htab, context->target_id);
   context->mpeg4.frame_num++;

   screen = context->decoder->context->screen;
   supported = screen->get_video_param(screen, context->decoder->profile,
                                       context->decoder->entrypoint,
                                       surf->buffer->interlaced ?
                                       PIPE_VIDEO_CAP_SUPPORTS_INTERLACED :
                                       PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE);

   if (!supported) {
      surf->templat.interlaced = screen->get_video_param(screen,
                                       context->decoder->profile,
                                       context->decoder->entrypoint,
                                       PIPE_VIDEO_CAP_PREFERS_INTERLACED);
      realloc = true;
   }

   format = screen->get_video_param(screen, context->decoder->profile,
                                    context->decoder->entrypoint,
                                    PIPE_VIDEO_CAP_PREFERED_FORMAT);

   if (surf->buffer->buffer_format != format &&
       surf->buffer->buffer_format == PIPE_FORMAT_NV12) {
      /* check originally as NV12 only */
      surf->templat.buffer_format = format;
      realloc = true;
   }

   if (u_reduce_video_profile(context->templat.profile) == PIPE_VIDEO_FORMAT_JPEG &&
       surf->buffer->buffer_format == PIPE_FORMAT_NV12) {
      if (context->mjpeg.sampling_factor == 0x211111 ||
          context->mjpeg.sampling_factor == 0x221212) {
         surf->templat.buffer_format = PIPE_FORMAT_YUYV;
         realloc = true;
      } else if (context->mjpeg.sampling_factor != 0x221111) {
         /* Not NV12 either */
         mtx_unlock(&drv->mutex);
         return VA_STATUS_ERROR_INVALID_SURFACE;
      }
   }

   if ((bool)(surf->templat.bind & PIPE_BIND_PROTECTED) != context->desc.base.protected_playback) {
      if (context->desc.base.protected_playback) {
         surf->templat.bind |= PIPE_BIND_PROTECTED;
      }
      else
         surf->templat.bind &= ~PIPE_BIND_PROTECTED;
      realloc = true;
   }

   if (u_reduce_video_profile(context->templat.profile) == PIPE_VIDEO_FORMAT_AV1 &&
       surf->buffer->buffer_format == PIPE_FORMAT_NV12) {
      if (context->desc.av1.picture_parameter.bit_depth_idx == 1) {
         surf->templat.buffer_format = PIPE_FORMAT_P010;
         realloc = true;
      }
   }

   if (realloc) {
      struct pipe_video_buffer *old_buf = surf->buffer;

      if (vlVaHandleSurfaceAllocate(drv, surf, &surf->templat, NULL, 0) != VA_STATUS_SUCCESS) {
         mtx_unlock(&drv->mutex);
         return VA_STATUS_ERROR_ALLOCATION_FAILED;
      }

      if (context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) {
         if (old_buf->interlaced) {
            struct u_rect src_rect, dst_rect;

            dst_rect.x0 = src_rect.x0 = 0;
            dst_rect.y0 = src_rect.y0 = 0;
            dst_rect.x1 = src_rect.x1 = surf->templat.width;
            dst_rect.y1 = src_rect.y1 = surf->templat.height;
            vl_compositor_yuv_deint_full(&drv->cstate, &drv->compositor,
                                         old_buf, surf->buffer,
                                         &src_rect, &dst_rect, VL_COMPOSITOR_WEAVE);
         } else {
            /* Can't convert from progressive to interlaced yet */
            mtx_unlock(&drv->mutex);
            return VA_STATUS_ERROR_INVALID_SURFACE;
         }
      }

      old_buf->destroy(old_buf);
      context->target = surf->buffer;
   }

   if (context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) {
      coded_buf = context->coded_buf;
      if (u_reduce_video_profile(context->templat.profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC) {
         getEncParamPresetH264(context);
         context->desc.h264enc.frame_num_cnt++;
      } else if (u_reduce_video_profile(context->templat.profile) == PIPE_VIDEO_FORMAT_HEVC)
         getEncParamPresetH265(context);
      context->decoder->begin_frame(context->decoder, context->target, &context->desc.base);
      context->decoder->encode_bitstream(context->decoder, context->target,
                                         coded_buf->derived_surface.resource, &feedback);
      surf->feedback = feedback;
      surf->coded_buf = coded_buf;
   }

   context->decoder->end_frame(context->decoder, context->target, &context->desc.base);
   if (context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE &&
      u_reduce_video_profile(context->templat.profile) == PIPE_VIDEO_FORMAT_MPEG4_AVC) {
      int idr_period = context->desc.h264enc.gop_size / context->gop_coeff;
      int p_remain_in_idr = idr_period - context->desc.h264enc.frame_num;
      surf->frame_num_cnt = context->desc.h264enc.frame_num_cnt;
      surf->force_flushed = false;
      if (context->first_single_submitted) {
         context->decoder->flush(context->decoder);
         context->first_single_submitted = false;
         surf->force_flushed = true;
      }
      if (p_remain_in_idr == 1) {
         if ((context->desc.h264enc.frame_num_cnt % 2) != 0) {
            context->decoder->flush(context->decoder);
            context->first_single_submitted = true;
         }
         else
            context->first_single_submitted = false;
         surf->force_flushed = true;
      }
   } else if (context->decoder->entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE &&
              u_reduce_video_profile(context->templat.profile) == PIPE_VIDEO_FORMAT_HEVC)
      context->desc.h265enc.frame_num++;
   mtx_unlock(&drv->mutex);
   return VA_STATUS_SUCCESS;
}
