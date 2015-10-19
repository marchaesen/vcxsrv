/**************************************************************************
 *
 * Copyright 2009 Younes Manton.
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
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef U_VIDEO_H
#define U_VIDEO_H

#ifdef __cplusplus
extern "C" {
#endif

#include "pipe/p_defines.h"
#include "pipe/p_video_enums.h"

/* u_reduce_video_profile() needs these */
#include "pipe/p_compiler.h"
#include "util/u_debug.h"
#include "util/u_math.h"

static inline enum pipe_video_format
u_reduce_video_profile(enum pipe_video_profile profile)
{
   switch (profile)
   {
      case PIPE_VIDEO_PROFILE_MPEG1:
      case PIPE_VIDEO_PROFILE_MPEG2_SIMPLE:
      case PIPE_VIDEO_PROFILE_MPEG2_MAIN:
         return PIPE_VIDEO_FORMAT_MPEG12;

      case PIPE_VIDEO_PROFILE_MPEG4_SIMPLE:
      case PIPE_VIDEO_PROFILE_MPEG4_ADVANCED_SIMPLE:
         return PIPE_VIDEO_FORMAT_MPEG4;

      case PIPE_VIDEO_PROFILE_VC1_SIMPLE:
      case PIPE_VIDEO_PROFILE_VC1_MAIN:
      case PIPE_VIDEO_PROFILE_VC1_ADVANCED:
         return PIPE_VIDEO_FORMAT_VC1;

      case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH422:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH444:
         return PIPE_VIDEO_FORMAT_MPEG4_AVC;

      case PIPE_VIDEO_PROFILE_HEVC_MAIN:
      case PIPE_VIDEO_PROFILE_HEVC_MAIN_10:
      case PIPE_VIDEO_PROFILE_HEVC_MAIN_STILL:
      case PIPE_VIDEO_PROFILE_HEVC_MAIN_12:
      case PIPE_VIDEO_PROFILE_HEVC_MAIN_444:
         return PIPE_VIDEO_FORMAT_HEVC;

      default:
         return PIPE_VIDEO_FORMAT_UNKNOWN;
   }
}

static inline void
u_copy_nv12_to_yv12(void *const *destination_data,
                    uint32_t const *destination_pitches,
                    int src_plane, int src_field,
                    int src_stride, int num_fields,
                    uint8_t const *src,
                    int width, int height)
{
   int x, y;
   unsigned u_stride = destination_pitches[2] * num_fields;
   unsigned v_stride = destination_pitches[1] * num_fields;
   uint8_t *u_dst = (uint8_t *)destination_data[2] + destination_pitches[2] * src_field;
   uint8_t *v_dst = (uint8_t *)destination_data[1] + destination_pitches[1] * src_field;

   /* TODO: SIMD */
   for (y = 0; y < height; y++) {
      for (x = 0; x < width; x++) {
         u_dst[x] = src[2*x];
         v_dst[x] = src[2*x+1];
      }
      u_dst += u_stride;
      v_dst += v_stride;
      src += src_stride;
   }
}

static inline void
u_copy_yv12_to_nv12(void *const *destination_data,
                    uint32_t const *destination_pitches,
                    int src_plane, int src_field,
                    int src_stride, int num_fields,
                    uint8_t const *src,
                    int width, int height)
{
   int x, y;
   unsigned offset = 2 - src_plane;
   unsigned stride = destination_pitches[1] * num_fields;
   uint8_t *dst = (uint8_t *)destination_data[1] + destination_pitches[1] * src_field;

   /* TODO: SIMD */
   for (y = 0; y < height; y++) {
      for (x = 0; x < 2 * width; x += 2) {
         dst[x+offset] = src[x>>1];
      }
      dst += stride;
      src += src_stride;
   }
}

static inline void
u_copy_swap422_packed(void *const *destination_data,
                       uint32_t const *destination_pitches,
                       int src_plane, int src_field,
                       int src_stride, int num_fields,
                       uint8_t const *src,
                       int width, int height)
{
   int x, y;
   unsigned stride = destination_pitches[0] * num_fields;
   uint8_t *dst = (uint8_t *)destination_data[0] + destination_pitches[0] * src_field;

   /* TODO: SIMD */
   for (y = 0; y < height; y++) {
      for (x = 0; x < 4 * width; x += 4) {
         dst[x+0] = src[x+1];
         dst[x+1] = src[x+0];
         dst[x+2] = src[x+3];
         dst[x+3] = src[x+2];
      }
      dst += stride;
      src += src_stride;
   }
}

static inline uint32_t
u_get_h264_level(uint32_t width, uint32_t height, uint32_t *max_reference)
{
   uint32_t max_dpb_mbs;

   width = align(width, 16);
   height = align(height, 16);

   /* Max references will be used for caculation of number of DPB buffers
      in the UVD driver, limitation of max references is 16. Some client
      like mpv application for VA-API, it requires references more than that,
      so we have to set max of references to 16 here. */
   *max_reference = MIN2(*max_reference, 16);
   max_dpb_mbs = (width / 16) * (height / 16) * *max_reference;

   /* The calculation is based on "Decoded picture buffering" section
      from http://en.wikipedia.org/wiki/H.264/MPEG-4_AVC */
   if (max_dpb_mbs <= 8100)
      return 30;
   else if (max_dpb_mbs <= 18000)
      return 31;
   else if (max_dpb_mbs <= 20480)
      return 32;
   else if (max_dpb_mbs <= 32768)
      return 41;
   else if (max_dpb_mbs <= 34816)
      return 42;
   else if (max_dpb_mbs <= 110400)
      return 50;
   else if (max_dpb_mbs <= 184320)
      return 51;
   else
      return 52;
}

#ifdef __cplusplus
}
#endif

#endif /* U_VIDEO_H */
