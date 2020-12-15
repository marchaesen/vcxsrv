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

#include "pipe/p_screen.h"

#include "util/u_video.h"
#include "util/u_memory.h"

#include "vl/vl_winsys.h"

#include "va_private.h"

#include "util/u_handle_table.h"

DEBUG_GET_ONCE_BOOL_OPTION(mpeg4, "VAAPI_MPEG4_ENABLED", false)

VAStatus
vlVaQueryConfigProfiles(VADriverContextP ctx, VAProfile *profile_list, int *num_profiles)
{
   struct pipe_screen *pscreen;
   enum pipe_video_profile p;
   VAProfile vap;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   *num_profiles = 0;

   pscreen = VL_VA_PSCREEN(ctx);
   for (p = PIPE_VIDEO_PROFILE_MPEG2_SIMPLE; p <= PIPE_VIDEO_PROFILE_VP9_PROFILE2; ++p) {
      if (u_reduce_video_profile(p) == PIPE_VIDEO_FORMAT_MPEG4 && !debug_get_option_mpeg4())
         continue;

      if (pscreen->get_video_param(pscreen, p, PIPE_VIDEO_ENTRYPOINT_BITSTREAM, PIPE_VIDEO_CAP_SUPPORTED)) {
         vap = PipeToProfile(p);
         if (vap != VAProfileNone)
            profile_list[(*num_profiles)++] = vap;
      }
   }

   /* Support postprocessing through vl_compositor */
   profile_list[(*num_profiles)++] = VAProfileNone;

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaQueryConfigEntrypoints(VADriverContextP ctx, VAProfile profile,
                           VAEntrypoint *entrypoint_list, int *num_entrypoints)
{
   struct pipe_screen *pscreen;
   enum pipe_video_profile p;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   *num_entrypoints = 0;

   if (profile == VAProfileNone) {
      entrypoint_list[(*num_entrypoints)++] = VAEntrypointVideoProc;
      return VA_STATUS_SUCCESS;
   }

   p = ProfileToPipe(profile);
   if (p == PIPE_VIDEO_PROFILE_UNKNOWN)
      return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;

   pscreen = VL_VA_PSCREEN(ctx);
   if (pscreen->get_video_param(pscreen, p, PIPE_VIDEO_ENTRYPOINT_BITSTREAM,
				PIPE_VIDEO_CAP_SUPPORTED))
      entrypoint_list[(*num_entrypoints)++] = VAEntrypointVLD;

   if (pscreen->get_video_param(pscreen, p, PIPE_VIDEO_ENTRYPOINT_ENCODE,
				PIPE_VIDEO_CAP_SUPPORTED))
      entrypoint_list[(*num_entrypoints)++] = VAEntrypointEncSlice;

   if (*num_entrypoints == 0)
      return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;

   assert(*num_entrypoints <= ctx->max_entrypoints);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaGetConfigAttributes(VADriverContextP ctx, VAProfile profile, VAEntrypoint entrypoint,
                        VAConfigAttrib *attrib_list, int num_attribs)
{
   struct pipe_screen *pscreen;
   int i;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   pscreen = VL_VA_PSCREEN(ctx);

   for (i = 0; i < num_attribs; ++i) {
      unsigned int value;
      if ((entrypoint == VAEntrypointVLD) &&
          (pscreen->get_video_param(pscreen, ProfileToPipe(profile),
           PIPE_VIDEO_ENTRYPOINT_BITSTREAM, PIPE_VIDEO_CAP_SUPPORTED))) {
         switch (attrib_list[i].type) {
         case VAConfigAttribRTFormat:
            value = VA_RT_FORMAT_YUV420 | VA_RT_FORMAT_YUV422;
            if (pscreen->is_video_format_supported(pscreen, PIPE_FORMAT_P010,
                                                   ProfileToPipe(profile),
                                                   PIPE_VIDEO_ENTRYPOINT_BITSTREAM) ||
                pscreen->is_video_format_supported(pscreen, PIPE_FORMAT_P016,
                                                   ProfileToPipe(profile),
                                                   PIPE_VIDEO_ENTRYPOINT_BITSTREAM))
               value |= VA_RT_FORMAT_YUV420_10BPP;
            break;
         default:
            value = VA_ATTRIB_NOT_SUPPORTED;
            break;
         }
      } else if ((entrypoint == VAEntrypointEncSlice) &&
                 (pscreen->get_video_param(pscreen, ProfileToPipe(profile),
                  PIPE_VIDEO_ENTRYPOINT_ENCODE, PIPE_VIDEO_CAP_SUPPORTED))) {
         switch (attrib_list[i].type) {
         case VAConfigAttribRTFormat:
            value = VA_RT_FORMAT_YUV420;
            if (pscreen->is_video_format_supported(pscreen, PIPE_FORMAT_P010,
                                                   ProfileToPipe(profile),
                                                   PIPE_VIDEO_ENTRYPOINT_BITSTREAM) ||
                pscreen->is_video_format_supported(pscreen, PIPE_FORMAT_P016,
                                                   ProfileToPipe(profile),
                                                   PIPE_VIDEO_ENTRYPOINT_BITSTREAM))
               value |= VA_RT_FORMAT_YUV420_10BPP;
            break;
         case VAConfigAttribRateControl:
            value = VA_RC_CQP | VA_RC_CBR | VA_RC_VBR;
            break;
         case VAConfigAttribEncPackedHeaders:
            value = VA_ENC_PACKED_HEADER_NONE;
            if (u_reduce_video_profile(ProfileToPipe(profile)) == PIPE_VIDEO_FORMAT_MPEG4_AVC ||
                u_reduce_video_profile(ProfileToPipe(profile)) == PIPE_VIDEO_FORMAT_HEVC)
               value |= VA_ENC_PACKED_HEADER_SEQUENCE;
            break;
         case VAConfigAttribEncMaxRefFrames:
            value = 1;
            break;
         default:
            value = VA_ATTRIB_NOT_SUPPORTED;
            break;
         }
      } else if (entrypoint == VAEntrypointVideoProc) {
         switch (attrib_list[i].type) {
         case VAConfigAttribRTFormat:
            value = (VA_RT_FORMAT_YUV420 |
                     VA_RT_FORMAT_YUV420_10BPP |
                     VA_RT_FORMAT_RGB32);
            break;
         default:
            value = VA_ATTRIB_NOT_SUPPORTED;
            break;
         }
      } else {
         value = VA_ATTRIB_NOT_SUPPORTED;
      }
      attrib_list[i].value = value;
   }

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaCreateConfig(VADriverContextP ctx, VAProfile profile, VAEntrypoint entrypoint,
                 VAConfigAttrib *attrib_list, int num_attribs, VAConfigID *config_id)
{
   vlVaDriver *drv;
   vlVaConfig *config;
   struct pipe_screen *pscreen;
   enum pipe_video_profile p;
   unsigned int supported_rt_formats;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);

   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   config = CALLOC(1, sizeof(vlVaConfig));
   if (!config)
      return VA_STATUS_ERROR_ALLOCATION_FAILED;

   if (profile == VAProfileNone && entrypoint == VAEntrypointVideoProc) {
      config->entrypoint = PIPE_VIDEO_ENTRYPOINT_UNKNOWN;
      config->profile = PIPE_VIDEO_PROFILE_UNKNOWN;
      supported_rt_formats = VA_RT_FORMAT_YUV420 |
                             VA_RT_FORMAT_YUV420_10BPP |
                             VA_RT_FORMAT_RGB32;
      for (int i = 0; i < num_attribs; i++) {
         if (attrib_list[i].type == VAConfigAttribRTFormat) {
            if (attrib_list[i].value & supported_rt_formats) {
               config->rt_format = attrib_list[i].value;
            } else {
               FREE(config);
               return VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;
            }
         }
      }

      /* Default value if not specified in the input attributes. */
      if (!config->rt_format)
         config->rt_format = supported_rt_formats;

      mtx_lock(&drv->mutex);
      *config_id = handle_table_add(drv->htab, config);
      mtx_unlock(&drv->mutex);
      return VA_STATUS_SUCCESS;
   }

   p = ProfileToPipe(profile);
   if (p == PIPE_VIDEO_PROFILE_UNKNOWN) {
      FREE(config);
      return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
   }

   pscreen = VL_VA_PSCREEN(ctx);

   switch (entrypoint) {
   case VAEntrypointVLD:
      if (!pscreen->get_video_param(pscreen, p, PIPE_VIDEO_ENTRYPOINT_BITSTREAM,
				    PIPE_VIDEO_CAP_SUPPORTED)) {
         FREE(config);
         return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
      }

      config->entrypoint = PIPE_VIDEO_ENTRYPOINT_BITSTREAM;
      break;

   case VAEntrypointEncSlice:
      if (!pscreen->get_video_param(pscreen, p, PIPE_VIDEO_ENTRYPOINT_ENCODE,
				    PIPE_VIDEO_CAP_SUPPORTED)) {
         FREE(config);
         return VA_STATUS_ERROR_UNSUPPORTED_PROFILE;
      }

      config->entrypoint = PIPE_VIDEO_ENTRYPOINT_ENCODE;
      break;

   default:
      FREE(config);
      return VA_STATUS_ERROR_UNSUPPORTED_ENTRYPOINT;
   }

   config->profile = p;
   supported_rt_formats = VA_RT_FORMAT_YUV420 | VA_RT_FORMAT_YUV422;
   if (pscreen->is_video_format_supported(pscreen, PIPE_FORMAT_P010, p,
         config->entrypoint) ||
       pscreen->is_video_format_supported(pscreen, PIPE_FORMAT_P016, p,
         config->entrypoint))
      supported_rt_formats |= VA_RT_FORMAT_YUV420_10BPP;

   for (int i = 0; i <num_attribs ; i++) {
      if (attrib_list[i].type == VAConfigAttribRateControl) {
         if (attrib_list[i].value == VA_RC_CBR)
            config->rc = PIPE_H264_ENC_RATE_CONTROL_METHOD_CONSTANT;
         else if (attrib_list[i].value == VA_RC_VBR)
            config->rc = PIPE_H264_ENC_RATE_CONTROL_METHOD_VARIABLE;
         else
            config->rc = PIPE_H264_ENC_RATE_CONTROL_METHOD_DISABLE;
      }
      if (attrib_list[i].type == VAConfigAttribRTFormat) {
         if (attrib_list[i].value & supported_rt_formats) {
            config->rt_format = attrib_list[i].value;
         } else {
            FREE(config);
            return VA_STATUS_ERROR_UNSUPPORTED_RT_FORMAT;
         }
      }
   }

   /* Default value if not specified in the input attributes. */
   if (!config->rt_format)
      config->rt_format = supported_rt_formats;

   mtx_lock(&drv->mutex);
   *config_id = handle_table_add(drv->htab, config);
   mtx_unlock(&drv->mutex);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaDestroyConfig(VADriverContextP ctx, VAConfigID config_id)
{
   vlVaDriver *drv;
   vlVaConfig *config;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);

   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   mtx_lock(&drv->mutex);
   config = handle_table_get(drv->htab, config_id);

   if (!config) {
      mtx_unlock(&drv->mutex);
      return VA_STATUS_ERROR_INVALID_CONFIG;
   }

   FREE(config);
   handle_table_remove(drv->htab, config_id);
   mtx_unlock(&drv->mutex);

   return VA_STATUS_SUCCESS;
}

VAStatus
vlVaQueryConfigAttributes(VADriverContextP ctx, VAConfigID config_id, VAProfile *profile,
                          VAEntrypoint *entrypoint, VAConfigAttrib *attrib_list, int *num_attribs)
{
   vlVaDriver *drv;
   vlVaConfig *config;

   if (!ctx)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   drv = VL_VA_DRIVER(ctx);

   if (!drv)
      return VA_STATUS_ERROR_INVALID_CONTEXT;

   mtx_lock(&drv->mutex);
   config = handle_table_get(drv->htab, config_id);
   mtx_unlock(&drv->mutex);

   if (!config)
      return VA_STATUS_ERROR_INVALID_CONFIG;

   *profile = PipeToProfile(config->profile);

   switch (config->entrypoint) {
   case PIPE_VIDEO_ENTRYPOINT_BITSTREAM:
      *entrypoint = VAEntrypointVLD;
      break;
   case PIPE_VIDEO_ENTRYPOINT_ENCODE:
      *entrypoint = VAEntrypointEncSlice;
      break;
   case PIPE_VIDEO_ENTRYPOINT_UNKNOWN:
      *entrypoint = VAEntrypointVideoProc;
      break;
   default:
      return VA_STATUS_ERROR_INVALID_CONFIG;
   }

   *num_attribs = 1;
   attrib_list[0].type = VAConfigAttribRTFormat;
   attrib_list[0].value = config->rt_format;

   return VA_STATUS_SUCCESS;
}
