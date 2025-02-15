/*
 * Copyright © Microsoft Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "d3d12_video_enc.h"
#include "d3d12_video_enc_hevc.h"
#include "util/u_video.h"
#include "d3d12_screen.h"
#include "d3d12_format.h"

#include <cmath>
#include <algorithm>
#include <numeric>

void
d3d12_video_encoder_update_current_rate_control_hevc(struct d3d12_video_encoder *pD3D12Enc,
                                                     pipe_h265_enc_picture_desc *picture)
{
   assert(picture->pic.temporal_id < ARRAY_SIZE(pipe_h265_enc_picture_desc::rc));
   assert(picture->pic.temporal_id < std::max(static_cast<uint8_t>(1u), pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificSequenceStateDescH265.sps_max_sub_layers_minus1));
   assert(picture->pic.temporal_id < ARRAY_SIZE(D3D12EncodeConfiguration::m_encoderRateControlDesc));

   struct D3D12EncodeRateControlState m_prevRCState = pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id];
   pD3D12Enc->m_currentEncodeConfig.m_activeRateControlIndex = picture->pic.temporal_id;
   pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id] = {};
   pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_FrameRate.Numerator =
      picture->rc[picture->pic.temporal_id].frame_rate_num;
   pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_FrameRate.Denominator =
      picture->rc[picture->pic.temporal_id].frame_rate_den;
   pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags = D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_NONE;

   if (picture->roi.num > 0)
      pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
         D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_DELTA_QP;

   switch (picture->rc[picture->pic.temporal_id].rate_ctrl_method) {
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE_SKIP:
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE:
      {
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Mode = D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_VBR.TargetAvgBitRate =
            picture->rc[picture->pic.temporal_id].target_bitrate;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_VBR.PeakBitRate =
            picture->rc[picture->pic.temporal_id].peak_bitrate;

         if (D3D12_VIDEO_ENC_CBR_FORCE_VBV_EQUAL_BITRATE) {
            debug_printf("[d3d12_video_encoder_hevc] d3d12_video_encoder_update_current_rate_control_hevc D3D12_VIDEO_ENC_CBR_FORCE_VBV_EQUAL_BITRATE environment variable is set, "
                       ", forcing VBV Size = VBV Initial Capacity = Target Bitrate = %" PRIu64 " (bits)\n", pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CBR.TargetBitRate);
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CBR.VBVCapacity =
               pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CBR.TargetBitRate;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CBR.InitialVBVFullness =
               pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CBR.TargetBitRate;
         } else if (picture->rc[picture->pic.temporal_id].app_requested_hrd_buffer) {
            debug_printf("[d3d12_video_encoder_hevc] d3d12_video_encoder_update_current_rate_control_hevc HRD required by app,"
                       " setting VBV Size = %d (bits) - VBV Initial Capacity %d (bits)\n", picture->rc[picture->pic.temporal_id].vbv_buffer_size, picture->rc[picture->pic.temporal_id].vbv_buf_initial_size);
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_VBR.VBVCapacity =
               picture->rc[picture->pic.temporal_id].vbv_buffer_size;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_VBR.InitialVBVFullness =
               picture->rc[picture->pic.temporal_id].vbv_buf_initial_size;
         }

         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].max_frame_size = picture->rc[picture->pic.temporal_id].max_au_size;
         if (picture->rc[picture->pic.temporal_id].max_au_size > 0) {
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_MAX_FRAME_SIZE;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_VBR.MaxFrameBitSize =
               picture->rc[picture->pic.temporal_id].max_au_size;

            debug_printf(
               "[d3d12_video_encoder_hevc] d3d12_video_encoder_update_current_rate_control_hevc "
               "Upper layer requested explicit MaxFrameBitSize: %" PRIu64 "\n",
               pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_VBR.MaxFrameBitSize);
         }

         if (picture->rc[picture->pic.temporal_id].app_requested_qp_range) {
            debug_printf(
               "[d3d12_video_encoder_hevc] d3d12_video_encoder_update_current_rate_control_hevc "
               "Upper layer requested explicit MinQP: %d MaxQP: %d\n",
               picture->rc[picture->pic.temporal_id].min_qp, picture->rc[picture->pic.temporal_id].max_qp);
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QP_RANGE;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_VBR.MinQP =
               picture->rc[picture->pic.temporal_id].min_qp;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_VBR.MaxQP =
               picture->rc[picture->pic.temporal_id].max_qp;
         }

         if (picture->quality_modes.level > 0) {
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QUALITY_VS_SPEED;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_EXTENSION1_SUPPORT;

            // Convert between D3D12 definition and PIPE definition
            // D3D12: QualityVsSpeed must be in the range [0, D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT1.MaxQualityVsSpeed]
            // The lower the value, the fastest the encode operation
            // PIPE: The quality level range can be queried through the VAConfigAttribEncQualityRange attribute. 
            // A lower value means higher quality, and a value of 1 represents the highest quality. 
            // The quality level setting is used as a trade-off between quality and speed/power 
            // consumption, with higher quality corresponds to lower speed and higher power consumption.

            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_VBR1.QualityVsSpeed =
               pD3D12Enc->max_quality_levels - picture->quality_modes.level;
         }

      } break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_QUALITY_VARIABLE:
      {
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Mode = D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_QVBR.TargetAvgBitRate =
            picture->rc[picture->pic.temporal_id].target_bitrate;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_QVBR.PeakBitRate =
            picture->rc[picture->pic.temporal_id].peak_bitrate;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_QVBR.ConstantQualityTarget =
            picture->rc[picture->pic.temporal_id].vbr_quality_factor;
         if (D3D12_VIDEO_ENC_CBR_FORCE_VBV_EQUAL_BITRATE) {
            debug_printf("[d3d12_video_encoder_hevc] d3d12_video_encoder_update_current_rate_control_hevc D3D12_VIDEO_ENC_CBR_FORCE_VBV_EQUAL_BITRATE environment variable is set, "
                       ", forcing VBV Size = VBV Initial Capacity = Target Bitrate = %" PRIu64 " (bits)\n", pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_QVBR1.TargetAvgBitRate);
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_EXTENSION1_SUPPORT;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_QVBR1.VBVCapacity =
               pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_QVBR1.TargetAvgBitRate;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_QVBR1.InitialVBVFullness =
               pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_QVBR1.TargetAvgBitRate;
         } else if (picture->rc[picture->pic.temporal_id].app_requested_hrd_buffer) {
            debug_printf("[d3d12_video_encoder_hevc] d3d12_video_encoder_update_current_rate_control_hevc HRD required by app,"
                       " setting VBV Size = %d (bits) - VBV Initial Capacity %d (bits)\n", picture->rc[picture->pic.temporal_id].vbv_buffer_size, picture->rc[picture->pic.temporal_id].vbv_buf_initial_size);
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_EXTENSION1_SUPPORT;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_QVBR1.VBVCapacity =
               picture->rc[picture->pic.temporal_id].vbv_buffer_size;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_QVBR1.InitialVBVFullness =
               picture->rc[picture->pic.temporal_id].vbv_buf_initial_size;
         }
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].max_frame_size = picture->rc[picture->pic.temporal_id].max_au_size;
         if (picture->rc[picture->pic.temporal_id].max_au_size > 0) {
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_MAX_FRAME_SIZE;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_QVBR.MaxFrameBitSize =
               picture->rc[picture->pic.temporal_id].max_au_size;

            debug_printf(
               "[d3d12_video_encoder_hevc] d3d12_video_encoder_update_current_rate_control_hevc "
               "Upper layer requested explicit MaxFrameBitSize: %" PRIu64 "\n",
               pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_QVBR.MaxFrameBitSize);
         }

         if (picture->rc[picture->pic.temporal_id].app_requested_qp_range) {
            debug_printf(
               "[d3d12_video_encoder_hevc] d3d12_video_encoder_update_current_rate_control_hevc "
               "Upper layer requested explicit MinQP: %d MaxQP: %d\n",
               picture->rc[picture->pic.temporal_id].min_qp, picture->rc[picture->pic.temporal_id].max_qp);
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QP_RANGE;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_QVBR.MinQP =
               picture->rc[picture->pic.temporal_id].min_qp;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_QVBR.MaxQP =
               picture->rc[picture->pic.temporal_id].max_qp;
         }

         if (picture->quality_modes.level > 0) {
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QUALITY_VS_SPEED;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_EXTENSION1_SUPPORT;

            // Convert between D3D12 definition and PIPE definition
            // D3D12: QualityVsSpeed must be in the range [0, D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT1.MaxQualityVsSpeed]
            // The lower the value, the fastest the encode operation
            // PIPE: The quality level range can be queried through the VAConfigAttribEncQualityRange attribute. 
            // A lower value means higher quality, and a value of 1 represents the highest quality. 
            // The quality level setting is used as a trade-off between quality and speed/power 
            // consumption, with higher quality corresponds to lower speed and higher power consumption.

            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_QVBR1.QualityVsSpeed =
               pD3D12Enc->max_quality_levels - picture->quality_modes.level;
         }

      } break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT_SKIP:
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT:
      {
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Mode = D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CBR.TargetBitRate =
            picture->rc[picture->pic.temporal_id].target_bitrate;

         /* For CBR mode, to guarantee bitrate of generated stream complies with
          * target bitrate (e.g. no over +/-10%), vbv_buffer_size and initial capacity should be same
          * as target bitrate. Controlled by OS env var D3D12_VIDEO_ENC_CBR_FORCE_VBV_EQUAL_BITRATE
          */
         if (D3D12_VIDEO_ENC_CBR_FORCE_VBV_EQUAL_BITRATE) {
            debug_printf("[d3d12_video_encoder_hevc] d3d12_video_encoder_update_current_rate_control_hevc D3D12_VIDEO_ENC_CBR_FORCE_VBV_EQUAL_BITRATE environment variable is set, "
                       ", forcing VBV Size = VBV Initial Capacity = Target Bitrate = %" PRIu64 " (bits)\n", pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CBR.TargetBitRate);
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CBR.VBVCapacity =
               pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CBR.TargetBitRate;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CBR.InitialVBVFullness =
               pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CBR.TargetBitRate;
         } else if (picture->rc[picture->pic.temporal_id].app_requested_hrd_buffer) {
            debug_printf("[d3d12_video_encoder_hevc] d3d12_video_encoder_update_current_rate_control_hevc HRD required by app,"
                       " setting VBV Size = %d (bits) - VBV Initial Capacity %d (bits)\n", picture->rc[picture->pic.temporal_id].vbv_buffer_size, picture->rc[picture->pic.temporal_id].vbv_buf_initial_size);
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CBR.VBVCapacity =
               picture->rc[picture->pic.temporal_id].vbv_buffer_size;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CBR.InitialVBVFullness =
               picture->rc[picture->pic.temporal_id].vbv_buf_initial_size;
         }

         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].max_frame_size = picture->rc[picture->pic.temporal_id].max_au_size;
         if (picture->rc[picture->pic.temporal_id].max_au_size > 0) {
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_MAX_FRAME_SIZE;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CBR.MaxFrameBitSize =
               picture->rc[picture->pic.temporal_id].max_au_size;

            debug_printf(
               "[d3d12_video_encoder_hevc] d3d12_video_encoder_update_current_rate_control_hevc "
               "Upper layer requested explicit MaxFrameBitSize: %" PRIu64 "\n",
               pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CBR.MaxFrameBitSize);
         }

         if (picture->rc[picture->pic.temporal_id].app_requested_qp_range) {
            debug_printf(
               "[d3d12_video_encoder_hevc] d3d12_video_encoder_update_current_rate_control_hevc "
               "Upper layer requested explicit MinQP: %d MaxQP: %d\n",
               picture->rc[picture->pic.temporal_id].min_qp, picture->rc[picture->pic.temporal_id].max_qp);
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QP_RANGE;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CBR.MinQP =
               picture->rc[picture->pic.temporal_id].min_qp;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CBR.MaxQP =
               picture->rc[picture->pic.temporal_id].max_qp;
         }

         if (picture->quality_modes.level > 0) {
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QUALITY_VS_SPEED;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_EXTENSION1_SUPPORT;

            // Convert between D3D12 definition and PIPE definition
            // D3D12: QualityVsSpeed must be in the range [0, D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT1.MaxQualityVsSpeed]
            // The lower the value, the fastest the encode operation
            // PIPE: The quality level range can be queried through the VAConfigAttribEncQualityRange attribute. 
            // A lower value means higher quality, and a value of 1 represents the highest quality. 
            // The quality level setting is used as a trade-off between quality and speed/power 
            // consumption, with higher quality corresponds to lower speed and higher power consumption.

            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CBR1.QualityVsSpeed =
               pD3D12Enc->max_quality_levels - picture->quality_modes.level;
         }

      } break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE:
      {
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Mode = D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP;
         // Load previous RC state for all frames and only update the current frame
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CQP =
                  m_prevRCState.m_Config.m_Configuration_CQP;
         switch (picture->picture_type) {
            case PIPE_H2645_ENC_PICTURE_TYPE_P:
            {
               pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CQP
                  .ConstantQP_InterPredictedFrame_PrevRefOnly = picture->rc[picture->pic.temporal_id].quant_p_frames;
            } break;
            case PIPE_H2645_ENC_PICTURE_TYPE_B:
            {
               pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CQP
                  .ConstantQP_InterPredictedFrame_BiDirectionalRef = picture->rc[picture->pic.temporal_id].quant_b_frames;
            } break;
            case PIPE_H2645_ENC_PICTURE_TYPE_I:
            case PIPE_H2645_ENC_PICTURE_TYPE_IDR:
            {
               pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CQP
                  .ConstantQP_FullIntracodedFrame = picture->rc[picture->pic.temporal_id].quant_i_frames;
            } break;
            default:
            {
               unreachable("Unsupported pipe_h2645_enc_picture_type");
            } break;
         }

         if (picture->quality_modes.level > 0) {
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_QUALITY_VS_SPEED;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_EXTENSION1_SUPPORT;

            // Convert between D3D12 definition and PIPE definition
            // D3D12: QualityVsSpeed must be in the range [0, D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT1.MaxQualityVsSpeed]
            // The lower the value, the fastest the encode operation
            // PIPE: The quality level range can be queried through the VAConfigAttribEncQualityRange attribute. 
            // A lower value means higher quality, and a value of 1 represents the highest quality. 
            // The quality level setting is used as a trade-off between quality and speed/power 
            // consumption, with higher quality corresponds to lower speed and higher power consumption.

            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CQP1.QualityVsSpeed =
               pD3D12Enc->max_quality_levels - picture->quality_modes.level;
         }
      } break;
      default:
      {
         debug_printf("[d3d12_video_encoder_hevc] d3d12_video_encoder_update_current_rate_control_hevc invalid RC "
                       "config, using default RC CQP mode\n");
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Mode = D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CQP
            .ConstantQP_FullIntracodedFrame = 30;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CQP
            .ConstantQP_InterPredictedFrame_PrevRefOnly = 30;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[picture->pic.temporal_id].m_Config.m_Configuration_CQP
            .ConstantQP_InterPredictedFrame_BiDirectionalRef = 30;
      } break;
   }
}

void
d3d12_video_encoder_update_current_frame_pic_params_info_hevc(struct d3d12_video_encoder *pD3D12Enc,
                                                              struct pipe_video_buffer *srcTexture,
                                                              struct pipe_picture_desc *picture,
                                                              D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA &picParams,
                                                              bool &bUsedAsReference)
{
   struct pipe_h265_enc_picture_desc *hevcPic = (struct pipe_h265_enc_picture_desc *) picture;
   d3d12_video_bitstream_builder_hevc *pHEVCBitstreamBuilder =
      static_cast<d3d12_video_bitstream_builder_hevc *>(pD3D12Enc->m_upBitstreamBuilder.get());
   assert(pHEVCBitstreamBuilder != nullptr);

   pD3D12Enc->m_currentEncodeConfig.m_bUsedAsReference = !hevcPic->not_referenced;
   bUsedAsReference = pD3D12Enc->m_currentEncodeConfig.m_bUsedAsReference;

   if (pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags &
       D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_NUM_REF_IDX_ACTIVE_OVERRIDE_FLAG_SLICE_SUPPORT)
   {
      picParams.pHEVCPicData->Flags |= D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_REQUEST_NUM_REF_IDX_ACTIVE_OVERRIDE_FLAG_SLICE;
   }

   if ((hevcPic->base.profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_444) ||
       (hevcPic->base.profile == PIPE_VIDEO_PROFILE_HEVC_MAIN10_444) ||
       (hevcPic->base.profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_422) ||
       (hevcPic->base.profile == PIPE_VIDEO_PROFILE_HEVC_MAIN10_422))
   {
      assert(picParams.DataSize == sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1));

      if (hevcPic->pic.pps_range_extension.pps_range_extension_flag)
      {
         //
         // Clear pps_range_extension() params if pps_range_extension_flag not enabled
         //
         picParams.pHEVCPicData1->log2_max_transform_skip_block_size_minus2 = 0u;
         picParams.pHEVCPicData1->Flags &= ~D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_CROSS_COMPONENT_PREDICTION;
         picParams.pHEVCPicData1->Flags &= ~D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_CHROMA_QP_OFFSET_LIST;
         picParams.pHEVCPicData1->diff_cu_chroma_qp_offset_depth = 0u;
         picParams.pHEVCPicData1->chroma_qp_offset_list_len_minus1 = 0u;
         for (uint32_t i = 0; i < ARRAY_SIZE(picParams.pHEVCPicData1->cb_qp_offset_list) ; i++)
         {
            picParams.pHEVCPicData1->cb_qp_offset_list[i] = 0u;
            picParams.pHEVCPicData1->cr_qp_offset_list[i] = 0u;
         }
         picParams.pHEVCPicData1->log2_sao_offset_scale_luma = 0u;
         picParams.pHEVCPicData1->log2_sao_offset_scale_chroma = 0u;
      }
      else
      {
         //
         // Copy pps_range_extension() from pipe params if pps_range_extension_flag set
         //

         //
         // Set and validate log2_max_transform_skip_block_size_minus2
         //
         {
            if (hevcPic->pic.transform_skip_enabled_flag)
            {
               picParams.pHEVCPicData1->log2_max_transform_skip_block_size_minus2 = static_cast<CHAR>(hevcPic->pic.pps_range_extension.log2_max_transform_skip_block_size_minus2);
               if ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.allowed_log2_max_transform_skip_block_size_minus2_values & (1 << picParams.pHEVCPicData1->log2_max_transform_skip_block_size_minus2)) == 0)
               {
                  debug_printf("D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1 arguments are not supported - log2_max_transform_skip_block_size_minus2 %d is not supported.\n", picParams.pHEVCPicData1->log2_max_transform_skip_block_size_minus2);
                  assert(false);
               }
            }
         }

         //
         // Set and validate cross_component_prediction_enabled_flag
         //
         {
            if (hevcPic->pic.pps_range_extension.cross_component_prediction_enabled_flag)
               picParams.pHEVCPicData1->Flags |= D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_CROSS_COMPONENT_PREDICTION;

            if(((picParams.pHEVCPicData1->Flags & D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_CROSS_COMPONENT_PREDICTION) != 0)
                  && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_CROSS_COMPONENT_PREDICTION_ENABLED_FLAG_SUPPORT) == 0))
            {
                  debug_printf("D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1 arguments are not supported - D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_CROSS_COMPONENT_PREDICTION is not supported."
                  " Ignoring the request for this feature flag on this encode session\n");
                  // Disable it and keep going with a warning
                  picParams.pHEVCPicData1->Flags &= ~D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_CROSS_COMPONENT_PREDICTION;
            }

            if(((picParams.pHEVCPicData1->Flags & D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_CROSS_COMPONENT_PREDICTION) == 0)
                  && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_CROSS_COMPONENT_PREDICTION_ENABLED_FLAG_REQUIRED) != 0))
            {
                  debug_printf("D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1 arguments are not supported - D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_CROSS_COMPONENT_PREDICTION is required to be set."
                  " Enabling this HW required feature flag on this encode session\n");
                  // HW doesn't support otherwise, so set it
                  picParams.pHEVCPicData1->Flags |= D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_CROSS_COMPONENT_PREDICTION;
            }
         }

         //
         // Set and validate chroma_qp_offset_list_enabled_flag
         //
         if (hevcPic->pic.pps_range_extension.chroma_qp_offset_list_enabled_flag)
         {
            picParams.pHEVCPicData1->Flags |= D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_CHROMA_QP_OFFSET_LIST;
            if(((picParams.pHEVCPicData1->Flags & D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_CHROMA_QP_OFFSET_LIST) != 0)
                  && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_CHROMA_QP_OFFSET_LIST_ENABLED_FLAG_SUPPORT) == 0))
            {
                  debug_printf("D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1 arguments are not supported - D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_CHROMA_QP_OFFSET_LIST is not supported."
                  " Ignoring the request for this feature flag on this encode session\n");
                  // Disable it and keep going with a warning
                  picParams.pHEVCPicData1->Flags &= ~D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_CHROMA_QP_OFFSET_LIST;
            }

            if(((picParams.pHEVCPicData1->Flags & D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_CHROMA_QP_OFFSET_LIST) == 0)
                  && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_CHROMA_QP_OFFSET_LIST_ENABLED_FLAG_REQUIRED) != 0))
            {
                  debug_printf("D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1 arguments are not supported - D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_CHROMA_QP_OFFSET_LIST is required to be set."
                  " Enabling this HW required feature flag on this encode session\n");
                  // HW doesn't support otherwise, so set it
                  picParams.pHEVCPicData1->Flags |= D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_CHROMA_QP_OFFSET_LIST;
            }

            //
            // Set and validate diff_cu_chroma_qp_offset_depth
            //
            picParams.pHEVCPicData1->diff_cu_chroma_qp_offset_depth = static_cast<UCHAR>(hevcPic->pic.pps_range_extension.diff_cu_chroma_qp_offset_depth);
            if ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.allowed_diff_cu_chroma_qp_offset_depth_values & (1 << picParams.pHEVCPicData1->diff_cu_chroma_qp_offset_depth)) == 0)
            {
               debug_printf("D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1 arguments are not supported - diff_cu_chroma_qp_offset_depth %d is not supported.\n", picParams.pHEVCPicData1->diff_cu_chroma_qp_offset_depth);
               assert(false);
            }

            //
            // Set and validate chroma_qp_offset_list_len_minus1
            //
            picParams.pHEVCPicData1->chroma_qp_offset_list_len_minus1 = static_cast<CHAR>(hevcPic->pic.pps_range_extension.chroma_qp_offset_list_len_minus1);
            if (hevcPic->pic.pps_range_extension.chroma_qp_offset_list_len_minus1 > 5)
            {
               debug_printf("D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1 arguments are not supported - chroma_qp_offset_list_len_minus1 %d is not supported.\n", hevcPic->pic.pps_range_extension.chroma_qp_offset_list_len_minus1);
               assert(false);
            }

            if ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.allowed_chroma_qp_offset_list_len_minus1_values & (1 << picParams.pHEVCPicData1->chroma_qp_offset_list_len_minus1)) == 0)
            {
               debug_printf("D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1 arguments are not supported - chroma_qp_offset_list_len_minus1 %d is not supported.\n", picParams.pHEVCPicData1->chroma_qp_offset_list_len_minus1);
               assert(false);
            }

            //
            // Set and validate cb_qp_offset_list, cr_qp_offset_list
            //
            for (uint32_t i = 0; i < picParams.pHEVCPicData1->chroma_qp_offset_list_len_minus1 ; i++)
            {
               picParams.pHEVCPicData1->cb_qp_offset_list[i] = static_cast<CHAR>(hevcPic->pic.pps_range_extension.cb_qp_offset_list[i]);
               if ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.allowed_cb_qp_offset_list_values[i] & (1 << (picParams.pHEVCPicData1->cb_qp_offset_list[i] + 12))) == 0)
               {
                  debug_printf("D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1 arguments are not supported - cb_qp_offset_list[%d] %d is not supported.\n", i, picParams.pHEVCPicData1->chroma_qp_offset_list_len_minus1);
                  assert(false);
               }
               picParams.pHEVCPicData1->cr_qp_offset_list[i] = static_cast<CHAR>(hevcPic->pic.pps_range_extension.cr_qp_offset_list[i]);
               if ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.allowed_cr_qp_offset_list_values[i] & (1 << (picParams.pHEVCPicData1->cr_qp_offset_list[i] + 12))) == 0)
               {
                  debug_printf("D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1 arguments are not supported - cr_qp_offset_list[%d] %d is not supported.\n", i, picParams.pHEVCPicData1->chroma_qp_offset_list_len_minus1);
                  assert(false);
               }
            }
         }

         //
         // Set and validate log2_sao_offset_scale_luma
         //
         picParams.pHEVCPicData1->log2_sao_offset_scale_luma = static_cast<UCHAR>(hevcPic->pic.pps_range_extension.log2_sao_offset_scale_luma);
         if ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.allowed_log2_sao_offset_scale_luma_values & (1 << picParams.pHEVCPicData1->log2_sao_offset_scale_luma)) == 0)
         {
            debug_printf("D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1 arguments are not supported - log2_sao_offset_scale_luma %d is not supported.\n", picParams.pHEVCPicData1->log2_sao_offset_scale_luma);
            assert(false);
         }

         //
         // Set and validate log2_sao_offset_scale_chroma
         //
         picParams.pHEVCPicData1->log2_sao_offset_scale_chroma = static_cast<UCHAR>(hevcPic->pic.pps_range_extension.log2_sao_offset_scale_chroma);
         if ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.allowed_log2_sao_offset_scale_chroma_values & (1 << picParams.pHEVCPicData1->log2_sao_offset_scale_chroma)) == 0)
         {
            debug_printf("D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1 arguments are not supported - log2_sao_offset_scale_chroma %d is not supported.\n", picParams.pHEVCPicData1->log2_sao_offset_scale_chroma);
            assert(false);
         }
      }
   }

   picParams.pHEVCPicData->slice_pic_parameter_set_id = pHEVCBitstreamBuilder->get_active_pps().pps_pic_parameter_set_id;

   //
   // These need to be set here so they're available for SPS/PPS header building (reference manager updates after that, for slice header params)
   //
   picParams.pHEVCPicData->TemporalLayerIndex = hevcPic->pic.temporal_id;
   picParams.pHEVCPicData->List0ReferenceFramesCount = 0;
   picParams.pHEVCPicData->List1ReferenceFramesCount = 0;
   if ((hevcPic->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_P) ||
       (hevcPic->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_B))
      picParams.pHEVCPicData->List0ReferenceFramesCount = hevcPic->num_ref_idx_l0_active_minus1 + 1;
   if (hevcPic->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_B)
      picParams.pHEVCPicData->List1ReferenceFramesCount = hevcPic->num_ref_idx_l1_active_minus1 + 1;

   if ((pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_HEVCConfig.ConfigurationFlags 
      & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_ALLOW_REQUEST_INTRA_CONSTRAINED_SLICES) != 0)
      picParams.pHEVCPicData->Flags |= D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_REQUEST_INTRA_CONSTRAINED_SLICES;

   if ((pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[hevcPic->pic.temporal_id].m_Flags & D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_DELTA_QP) != 0)
   {
      // Use 8 bit qpmap array for HEVC picparams (-51, 51 range and int8_t pRateControlQPMap type)
      const int32_t hevc_min_delta_qp = -51;
      const int32_t hevc_max_delta_qp = 51;
      d3d12_video_encoder_update_picparams_region_of_interest_qpmap(
         pD3D12Enc,
         &hevcPic->roi,
         hevc_min_delta_qp,
         hevc_max_delta_qp,
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[hevcPic->pic.temporal_id].m_pRateControlQPMap8Bit);
      picParams.pHEVCPicData->pRateControlQPMap = pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[hevcPic->pic.temporal_id].m_pRateControlQPMap8Bit.data();
      picParams.pHEVCPicData->QPMapValuesCount = static_cast<UINT>(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc[hevcPic->pic.temporal_id].m_pRateControlQPMap8Bit.size());
   }

   pD3D12Enc->m_upDPBManager->begin_frame(picParams, bUsedAsReference, picture);
   pD3D12Enc->m_upDPBManager->get_current_frame_picture_control_data(picParams);

   // Save state snapshot from record time to resolve headers at get_feedback time
   size_t current_metadata_slot = static_cast<size_t>(pD3D12Enc->m_fenceValue % D3D12_VIDEO_ENC_METADATA_BUFFERS_COUNT);
   pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_associatedEncodeCapabilities =
      pD3D12Enc->m_currentEncodeCapabilities;
   pD3D12Enc->m_spEncodedFrameMetadata[current_metadata_slot].m_associatedEncodeConfig =
      pD3D12Enc->m_currentEncodeConfig;
}

///
/// Tries to configurate the encoder using the requested slice configuration
/// or falls back to single slice encoding.
///
bool
d3d12_video_encoder_negotiate_current_hevc_slices_configuration(struct d3d12_video_encoder *pD3D12Enc,
                                                                pipe_h265_enc_picture_desc *picture)
{
   ///
   /// Initialize single slice by default
   ///
   D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE requestedSlicesMode =
      D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME;
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES requestedSlicesConfig = {};
   requestedSlicesConfig.NumberOfSlicesPerFrame = 1;

   ///
   /// Try to see if can accomodate for multi-slice request by user
   ///
   if ((picture->slice_mode == PIPE_VIDEO_SLICE_MODE_BLOCKS) && (picture->num_slice_descriptors > 1)) {
      /* Some apps send all same size slices minus 1 slice in any position in the descriptors */
      /* Lets validate that there are at most 2 different slice sizes in all the descriptors */
      std::vector<int> slice_sizes(picture->num_slice_descriptors);
      for (uint32_t i = 0; i < picture->num_slice_descriptors; i++)
         slice_sizes[i] = picture->slices_descriptors[i].num_ctu_in_slice;
      std::sort(slice_sizes.begin(), slice_sizes.end());
      bool bUniformSizeSlices = (std::unique(slice_sizes.begin(), slice_sizes.end()) - slice_sizes.begin()) <= 2;

      uint32_t subregion_block_pixel_size = pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.SubregionBlockPixelsSize;
      uint32_t num_subregions_per_scanline =
         DIV_ROUND_UP(pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Width, subregion_block_pixel_size);

      /* m_currentResolutionSupportCaps.SubregionBlockPixelsSize can be a multiple of MinCUSize to accomodate for HW requirements 
         So, if the allowed subregion (slice) pixel size partition is bigger (a multiple) than the CTU size, we have to adjust
         num_subregions_per_slice by this factor respect from slices_descriptors[X].num_ctu_in_slice
      */

      /* This assert should always be true according to the spec
         https://github.com/microsoft/DirectX-Specs/blob/master/d3d/D3D12VideoEncoding.md#3150-struct-d3d12_feature_data_video_encoder_resolution_support_limits
       */
      uint8_t minCUSize = d3d12_video_encoder_convert_12cusize_to_pixel_size_hevc(pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_HEVCConfig.MinLumaCodingUnitSize);
      assert((pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.SubregionBlockPixelsSize 
         % minCUSize) == 0);

      uint32_t subregionsize_to_ctu_factor = pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.SubregionBlockPixelsSize / 
         minCUSize;
      uint32_t num_subregions_per_slice = picture->slices_descriptors[0].num_ctu_in_slice
                                                   * pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.SubregionBlockPixelsSize
                                                   / (subregionsize_to_ctu_factor * subregionsize_to_ctu_factor);

      bool bSliceAligned = ((num_subregions_per_slice % num_subregions_per_scanline) == 0);

      if (bUniformSizeSlices &&
                 d3d12_video_encoder_check_subregion_mode_support(
                    pD3D12Enc,
                    D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_SUBREGIONS_PER_FRAME)) {
            requestedSlicesMode =
               D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_SUBREGIONS_PER_FRAME;
            requestedSlicesConfig.NumberOfSlicesPerFrame = picture->num_slice_descriptors;
            debug_printf("[d3d12_video_encoder_hevc] Using multi slice encoding mode: "
                           "D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_SUBREGIONS_PER_FRAME "
                           "with %d slices per frame.\n",
                           requestedSlicesConfig.NumberOfSlicesPerFrame);
      } else if (bUniformSizeSlices &&
                 d3d12_video_encoder_check_subregion_mode_support(
                    pD3D12Enc,
                    D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_SQUARE_UNITS_PER_SUBREGION_ROW_UNALIGNED)) {
            requestedSlicesMode =
               D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_SQUARE_UNITS_PER_SUBREGION_ROW_UNALIGNED;
            requestedSlicesConfig.NumberOfCodingUnitsPerSlice = num_subregions_per_slice;
            debug_printf("[d3d12_video_encoder_hevc] Using multi slice encoding mode: "
                           "D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_SQUARE_UNITS_PER_SUBREGION_ROW_UNALIGNED "
                           "with %d NumberOfCodingUnitsPerSlice per frame.\n",
                           requestedSlicesConfig.NumberOfCodingUnitsPerSlice);

      } else if (bUniformSizeSlices && bSliceAligned &&
                 d3d12_video_encoder_check_subregion_mode_support(
                    pD3D12Enc,
                    D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_ROWS_PER_SUBREGION)) {

         // Number of subregion block per slice is aligned to a scanline width, in which case we can
         // use D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_ROWS_PER_SUBREGION
         requestedSlicesMode = D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_ROWS_PER_SUBREGION;
         requestedSlicesConfig.NumberOfRowsPerSlice = (num_subregions_per_slice / num_subregions_per_scanline);
         debug_printf("[d3d12_video_encoder_hevc] Using multi slice encoding mode: "
                        "D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_ROWS_PER_SUBREGION with "
                        "%d subregion block rows (%d pix scanlines) per slice.\n",
                        requestedSlicesConfig.NumberOfRowsPerSlice,
                        pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.SubregionBlockPixelsSize);
      } else {
         debug_printf("[d3d12_video_encoder_hevc] Requested slice control mode is not supported: All slices must "
                         "have the same number of macroblocks.\n");
         return false;
      }
   } else if(picture->slice_mode == PIPE_VIDEO_SLICE_MODE_MAX_SLICE_SIZE) {
      if ((picture->max_slice_bytes > 0) &&
                 d3d12_video_encoder_check_subregion_mode_support(
                    pD3D12Enc,
                    D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_BYTES_PER_SUBREGION )) {
            requestedSlicesMode =
               D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_BYTES_PER_SUBREGION;
            requestedSlicesConfig.MaxBytesPerSlice = picture->max_slice_bytes;
            debug_printf("[d3d12_video_encoder_hevc] Using multi slice encoding mode: "
                           "D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_BYTES_PER_SUBREGION  "
                           "with %d MaxBytesPerSlice per frame.\n",
                           requestedSlicesConfig.MaxBytesPerSlice);
      } else {
         debug_printf("[d3d12_video_encoder_hevc] Requested slice control mode is not supported: All slices must "
                         "have the same number of macroblocks.\n");
         return false;
      }
   } else {
      requestedSlicesMode = D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME;
      requestedSlicesConfig.NumberOfSlicesPerFrame = 1;
      debug_printf("[d3d12_video_encoder_hevc] Requested slice control mode is full frame. m_SlicesPartition_H264.NumberOfSlicesPerFrame = %d - m_encoderSliceConfigMode = %d \n",
      requestedSlicesConfig.NumberOfSlicesPerFrame, requestedSlicesMode);
   }

   if (!d3d12_video_encoder_isequal_slice_config_hevc(
          pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode,
          pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigDesc.m_SlicesPartition_HEVC,
          requestedSlicesMode,
          requestedSlicesConfig)) {
      pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_slices;
   }

   pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigDesc.m_SlicesPartition_HEVC = requestedSlicesConfig;
   pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode = requestedSlicesMode;

   return true;
}

D3D12_VIDEO_ENCODER_MOTION_ESTIMATION_PRECISION_MODE
d3d12_video_encoder_convert_hevc_motion_configuration(struct d3d12_video_encoder *pD3D12Enc,
                                                      pipe_h265_enc_picture_desc *picture)
{
   return D3D12_VIDEO_ENCODER_MOTION_ESTIMATION_PRECISION_MODE_MAXIMUM;
}

bool
d3d12_video_encoder_update_hevc_gop_configuration(struct d3d12_video_encoder *pD3D12Enc,
                                                  pipe_h265_enc_picture_desc *picture)
{
   // Only update GOP when it begins
   // This triggers DPB/encoder/heap re-creation, so only check on IDR when a GOP might change
   if ((picture->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR)
      || (picture->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_I)) {
      uint32_t GOPLength = picture->seq.intra_period;
      uint32_t PPicturePeriod = picture->seq.ip_period;

      // Set dirty flag if m_HEVCGroupOfPictures changed
      auto previousGOPConfig = pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_HEVCGroupOfPictures;
      pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_HEVCGroupOfPictures = {
         GOPLength,
         PPicturePeriod,
         picture->seq.log2_max_pic_order_cnt_lsb_minus4
      };

      if (memcmp(&previousGOPConfig,
                 &pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_HEVCGroupOfPictures,
                 sizeof(D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE_HEVC)) != 0) {
         pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_gop;
      }
   }
   return true;
}

D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT
ConvertHEVCSupportFromProfile(D3D12_VIDEO_ENCODER_PROFILE_HEVC profile, D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC1* pSupport1)
{
   D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT capCodecConfigData = {};
   if (profile <= D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN10)
   {
      // Profiles defined up to D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN10 use D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC
      capCodecConfigData.DataSize = sizeof(D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC);
      // D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC1 binary-compatible with D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC
      capCodecConfigData.pHEVCSupport = reinterpret_cast<D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC*>(pSupport1);
   }
   else
   {
      // Profiles defined between D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN12 and D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN16_444 use D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC1
      assert (profile <= D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN16_444);
      capCodecConfigData.DataSize = sizeof(D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC1);
      capCodecConfigData.pHEVCSupport1 = pSupport1;
   }
   return capCodecConfigData;
}

D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA
ConvertHEVCPicParamsFromProfile(D3D12_VIDEO_ENCODER_PROFILE_HEVC profile, D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1* pPictureParams1)
{
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA curPicParamsData = {};
   if (profile <= D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN10)
   {
      // Profiles defined up to D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN10 use D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC
      curPicParamsData.pHEVCPicData  = reinterpret_cast<D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC*>(pPictureParams1);
      // D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1 binary-compatible with D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC
      curPicParamsData.DataSize      = sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC);
   }
   else
   {
      // Profiles defined between D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN12 and D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN16_444 use D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1
      assert (profile <= D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN16_444);
      curPicParamsData.pHEVCPicData1 = pPictureParams1;
      curPicParamsData.DataSize      = sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1);
   }
   return curPicParamsData;
}

D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC
d3d12_video_encoder_convert_hevc_codec_configuration(struct d3d12_video_encoder *pD3D12Enc,
                                                     pipe_h265_enc_picture_desc *picture,
                                                     bool&is_supported)
{
   is_supported = true;
   uint32_t min_cu_size = (1 << (picture->seq.log2_min_luma_coding_block_size_minus3 + 3));
   uint32_t max_cu_size = (1 << (picture->seq.log2_min_luma_coding_block_size_minus3 + 3 + picture->seq.log2_diff_max_min_luma_coding_block_size));

   uint32_t min_tu_size = (1 << (picture->seq.log2_min_transform_block_size_minus2 + 2));
   uint32_t max_tu_size = (1 << (picture->seq.log2_min_transform_block_size_minus2 + 2 + picture->seq.log2_diff_max_min_transform_block_size));

   D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC config = {
      D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_NONE,
      d3d12_video_encoder_convert_pixel_size_hevc_to_12cusize(min_cu_size),
      d3d12_video_encoder_convert_pixel_size_hevc_to_12cusize(max_cu_size),
      d3d12_video_encoder_convert_pixel_size_hevc_to_12tusize(min_tu_size),
      d3d12_video_encoder_convert_pixel_size_hevc_to_12tusize(max_tu_size),
      picture->seq.max_transform_hierarchy_depth_inter,
      picture->seq.max_transform_hierarchy_depth_intra,
   };

   pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps = 
   {
         D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_NONE,
         config.MinLumaCodingUnitSize,
         config.MaxLumaCodingUnitSize,
         config.MinLumaTransformUnitSize,
         config.MaxLumaTransformUnitSize,
         config.max_transform_hierarchy_depth_inter,
         config.max_transform_hierarchy_depth_intra
   };

   D3D12_FEATURE_DATA_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT capCodecConfigData = { };
   capCodecConfigData.NodeIndex = pD3D12Enc->m_NodeIndex;
   capCodecConfigData.Codec = D3D12_VIDEO_ENCODER_CODEC_HEVC;
   D3D12_VIDEO_ENCODER_PROFILE_HEVC prof = d3d12_video_encoder_convert_profile_to_d3d12_enc_profile_hevc(pD3D12Enc->base.profile);
   capCodecConfigData.Profile.pHEVCProfile = &prof;
   capCodecConfigData.Profile.DataSize = sizeof(prof);

   capCodecConfigData.CodecSupportLimits = ConvertHEVCSupportFromProfile((*capCodecConfigData.Profile.pHEVCProfile),
                                                                         &pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps);

   if(FAILED(pD3D12Enc->m_spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT, &capCodecConfigData, sizeof(capCodecConfigData)))
      || !capCodecConfigData.IsSupported)
   {
         is_supported = false;

         // Workaround for https://github.com/intel/libva/issues/641
         if ( !capCodecConfigData.IsSupported
            && ((picture->seq.max_transform_hierarchy_depth_inter == 0)
               || (picture->seq.max_transform_hierarchy_depth_intra == 0)) )
         {
            // Try and see if the values where 4 and overflowed in the 2 bit fields
            capCodecConfigData.CodecSupportLimits.pHEVCSupport->max_transform_hierarchy_depth_inter =
               (picture->seq.max_transform_hierarchy_depth_inter == 0) ? 4 : picture->seq.max_transform_hierarchy_depth_inter;
            capCodecConfigData.CodecSupportLimits.pHEVCSupport->max_transform_hierarchy_depth_intra =
               (picture->seq.max_transform_hierarchy_depth_intra == 0) ? 4 : picture->seq.max_transform_hierarchy_depth_intra;

            // Call the caps check again
            if(SUCCEEDED(pD3D12Enc->m_spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT, &capCodecConfigData, sizeof(capCodecConfigData)))
               && capCodecConfigData.IsSupported)
            {
               // If this was the case, then update the config return variable with the overriden values too
               is_supported = true;
               config.max_transform_hierarchy_depth_inter =
                  capCodecConfigData.CodecSupportLimits.pHEVCSupport->max_transform_hierarchy_depth_inter;
               config.max_transform_hierarchy_depth_intra =
                  capCodecConfigData.CodecSupportLimits.pHEVCSupport->max_transform_hierarchy_depth_intra;
            }
         }

         if (!is_supported) {
            debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - "
               "Call to CheckFeatureCaps (D3D12_FEATURE_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT, ...) returned failure "
               "or not supported for Codec HEVC -  MinLumaSize %d - MaxLumaSize %d -  MinTransformSize %d - "
               "MaxTransformSize %d - Depth_inter %d - Depth intra %d\n",
               config.MinLumaCodingUnitSize,
               config.MaxLumaCodingUnitSize,
               config.MinLumaTransformUnitSize,
               config.MaxLumaTransformUnitSize,
               config.max_transform_hierarchy_depth_inter,
               config.max_transform_hierarchy_depth_intra);

            return config;
         }
   }

   if (picture->seq.amp_enabled_flag)
      config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_USE_ASYMETRIC_MOTION_PARTITION;

   if (picture->seq.sample_adaptive_offset_enabled_flag)
      config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_ENABLE_SAO_FILTER;

   if (picture->pic.pps_loop_filter_across_slices_enabled_flag)
      config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_DISABLE_LOOP_FILTER_ACROSS_SLICES;

   if (picture->pic.transform_skip_enabled_flag)
      config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_ENABLE_TRANSFORM_SKIPPING;
   
   if (picture->pic.constrained_intra_pred_flag)
      config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_USE_CONSTRAINED_INTRAPREDICTION;

   if ((picture->base.profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_444) ||
       (picture->base.profile == PIPE_VIDEO_PROFILE_HEVC_MAIN10_444) ||
       (picture->base.profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_422) ||
       (picture->base.profile == PIPE_VIDEO_PROFILE_HEVC_MAIN10_422))
   {
      if (picture->seq.sps_range_extension.transform_skip_rotation_enabled_flag)
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_TRANSFORM_SKIP_ROTATION;
      if (picture->seq.sps_range_extension.transform_skip_context_enabled_flag)
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_TRANSFORM_SKIP_CONTEXT;
      if (picture->seq.sps_range_extension.implicit_rdpcm_enabled_flag)
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_IMPLICIT_RDPCM;
      if (picture->seq.sps_range_extension.explicit_rdpcm_enabled_flag)
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_EXPLICIT_RDPCM;
      if (picture->seq.sps_range_extension.extended_precision_processing_flag)
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_EXTENDED_PRECISION_PROCESSING;
      if (picture->seq.sps_range_extension.intra_smoothing_disabled_flag)
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_INTRA_SMOOTHING_DISABLED;
      if (picture->seq.sps_range_extension.high_precision_offsets_enabled_flag)
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_HIGH_PRECISION_OFFSETS;
      if (picture->seq.sps_range_extension.persistent_rice_adaptation_enabled_flag)
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_PERSISTENT_RICE_ADAPTATION;
      if (picture->seq.sps_range_extension.cabac_bypass_alignment_enabled_flag)
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_CABAC_BYPASS_ALIGNMENT;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_DISABLE_LOOP_FILTER_ACROSS_SLICES) != 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_DISABLING_LOOP_FILTER_ACROSS_SLICES_SUPPORT) == 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - Disable deblocking across slice boundary mode not supported."
         " Ignoring the request for this feature flag on this encode session\n");
         // Disable it and keep going with a warning
         config.ConfigurationFlags &= ~D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_DISABLE_LOOP_FILTER_ACROSS_SLICES;
   }  

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_ALLOW_REQUEST_INTRA_CONSTRAINED_SLICES) != 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_INTRA_SLICE_CONSTRAINED_ENCODING_SUPPORT) == 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - Intra slice constrained mode not supported."
         " Ignoring the request for this feature flag on this encode session\n");
         // Disable it and keep going with a warning
         config.ConfigurationFlags &= ~D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_ALLOW_REQUEST_INTRA_CONSTRAINED_SLICES;
   }  

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_ENABLE_SAO_FILTER) != 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_SAO_FILTER_SUPPORT) == 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - SAO Filter mode not supported."
         " Ignoring the request for this feature flag on this encode session\n");
         // Disable it and keep going with a warning
         config.ConfigurationFlags &= ~D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_ENABLE_SAO_FILTER;
   }  


   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_USE_ASYMETRIC_MOTION_PARTITION) != 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_ASYMETRIC_MOTION_PARTITION_SUPPORT) == 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - Asymetric motion partition not supported."
         " Ignoring the request for this feature flag on this encode session\n");
         // Disable it and keep going with a warning
         config.ConfigurationFlags &= ~D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_USE_ASYMETRIC_MOTION_PARTITION;
   }  

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_USE_ASYMETRIC_MOTION_PARTITION) == 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_ASYMETRIC_MOTION_PARTITION_REQUIRED) != 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - Asymetric motion partition is required to be set."
         " Enabling this HW required feature flag on this encode session\n");
         // HW doesn't support otherwise, so set it
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_USE_ASYMETRIC_MOTION_PARTITION;
   }  

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_ENABLE_TRANSFORM_SKIPPING) != 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_TRANSFORM_SKIP_SUPPORT) == 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - Allow transform skipping is not supported."
         " Ignoring the request for this feature flag on this encode session\n");
         // Disable it and keep going with a warning
         config.ConfigurationFlags &= ~D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_ENABLE_TRANSFORM_SKIPPING;
   }  

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_USE_CONSTRAINED_INTRAPREDICTION) != 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_CONSTRAINED_INTRAPREDICTION_SUPPORT) == 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - Constrained intra-prediction use is not supported."
         " Ignoring the request for this feature flag on this encode session\n");
         // Disable it and keep going with a warning
         config.ConfigurationFlags &= ~D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_USE_CONSTRAINED_INTRAPREDICTION;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_TRANSFORM_SKIP_ROTATION) != 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_TRANSFORM_SKIP_ROTATION_ENABLED_SUPPORT) == 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_TRANSFORM_SKIP_ROTATION is not supported."
         " Ignoring the request for this feature flag on this encode session\n");
         // Disable it and keep going with a warning
         config.ConfigurationFlags &= ~D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_TRANSFORM_SKIP_ROTATION;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_TRANSFORM_SKIP_ROTATION) == 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_TRANSFORM_SKIP_ROTATION_ENABLED_REQUIRED) != 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_TRANSFORM_SKIP_ROTATION is required to be set."
         " Enabling this HW required feature flag on this encode session\n");
         // HW doesn't support otherwise, so set it
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_TRANSFORM_SKIP_ROTATION;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_TRANSFORM_SKIP_CONTEXT) != 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_TRANSFORM_SKIP_CONTEXT_ENABLED_SUPPORT) == 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_TRANSFORM_SKIP_CONTEXT is not supported."
         " Ignoring the request for this feature flag on this encode session\n");
         // Disable it and keep going with a warning
         config.ConfigurationFlags &= ~D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_TRANSFORM_SKIP_CONTEXT;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_TRANSFORM_SKIP_CONTEXT) == 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_TRANSFORM_SKIP_CONTEXT_ENABLED_REQUIRED) != 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_TRANSFORM_SKIP_CONTEXT is required to be set."
         " Enabling this HW required feature flag on this encode session\n");
         // HW doesn't support otherwise, so set it
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_TRANSFORM_SKIP_CONTEXT;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_IMPLICIT_RDPCM) != 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_IMPLICIT_RDPCM_ENABLED_SUPPORT) == 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_IMPLICIT_RDPCM is not supported."
         " Ignoring the request for this feature flag on this encode session\n");
         // Disable it and keep going with a warning
         config.ConfigurationFlags &= ~D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_IMPLICIT_RDPCM;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_IMPLICIT_RDPCM) == 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_IMPLICIT_RDPCM_ENABLED_REQUIRED) != 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_IMPLICIT_RDPCM is required to be set."
         " Enabling this HW required feature flag on this encode session\n");
         // HW doesn't support otherwise, so set it
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_IMPLICIT_RDPCM;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_EXPLICIT_RDPCM) != 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_EXPLICIT_RDPCM_ENABLED_SUPPORT) == 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_EXPLICIT_RDPCM is not supported."
         " Ignoring the request for this feature flag on this encode session\n");
         // Disable it and keep going with a warning
         config.ConfigurationFlags &= ~D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_EXPLICIT_RDPCM;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_EXPLICIT_RDPCM) == 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_EXPLICIT_RDPCM_ENABLED_REQUIRED) != 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_EXPLICIT_RDPCM is required to be set."
         " Enabling this HW required feature flag on this encode session\n");
         // HW doesn't support otherwise, so set it
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_EXPLICIT_RDPCM;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_EXTENDED_PRECISION_PROCESSING) != 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_EXTENDED_PRECISION_PROCESSING_SUPPORT) == 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_EXTENDED_PRECISION_PROCESSING is not supported."
         " Ignoring the request for this feature flag on this encode session\n");
         // Disable it and keep going with a warning
         config.ConfigurationFlags &= ~D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_EXTENDED_PRECISION_PROCESSING;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_EXTENDED_PRECISION_PROCESSING) == 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_EXTENDED_PRECISION_PROCESSING_REQUIRED) != 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_EXTENDED_PRECISION_PROCESSING is required to be set."
         " Enabling this HW required feature flag on this encode session\n");
         // HW doesn't support otherwise, so set it
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_EXTENDED_PRECISION_PROCESSING;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_INTRA_SMOOTHING_DISABLED) != 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_INTRA_SMOOTHING_DISABLED_SUPPORT) == 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_INTRA_SMOOTHING_DISABLED is not supported."
         " Ignoring the request for this feature flag on this encode session\n");
         // Disable it and keep going with a warning
         config.ConfigurationFlags &= ~D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_INTRA_SMOOTHING_DISABLED;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_INTRA_SMOOTHING_DISABLED) == 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_INTRA_SMOOTHING_DISABLED_REQUIRED) != 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_INTRA_SMOOTHING_DISABLED is required to be set."
         " Enabling this HW required feature flag on this encode session\n");
         // HW doesn't support otherwise, so set it
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_INTRA_SMOOTHING_DISABLED;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_HIGH_PRECISION_OFFSETS) != 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_HIGH_PRECISION_OFFSETS_ENABLED_SUPPORT) == 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_HIGH_PRECISION_OFFSETS is not supported."
         " Ignoring the request for this feature flag on this encode session\n");
         // Disable it and keep going with a warning
         config.ConfigurationFlags &= ~D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_HIGH_PRECISION_OFFSETS;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_HIGH_PRECISION_OFFSETS) == 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_HIGH_PRECISION_OFFSETS_ENABLED_REQUIRED) != 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_HIGH_PRECISION_OFFSETS is required to be set."
         " Enabling this HW required feature flag on this encode session\n");
         // HW doesn't support otherwise, so set it
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_HIGH_PRECISION_OFFSETS;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_PERSISTENT_RICE_ADAPTATION) != 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_PERSISTENT_RICE_ADAPTATION_ENABLED_SUPPORT) == 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_PERSISTENT_RICE_ADAPTATION is not supported."
         " Ignoring the request for this feature flag on this encode session\n");
         // Disable it and keep going with a warning
         config.ConfigurationFlags &= ~D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_PERSISTENT_RICE_ADAPTATION;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_PERSISTENT_RICE_ADAPTATION) == 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_PERSISTENT_RICE_ADAPTATION_ENABLED_REQUIRED) != 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_PERSISTENT_RICE_ADAPTATION is required to be set."
         " Enabling this HW required feature flag on this encode session\n");
         // HW doesn't support otherwise, so set it
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_PERSISTENT_RICE_ADAPTATION;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_CABAC_BYPASS_ALIGNMENT) != 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_CABAC_BYPASS_ALIGNMENT_ENABLED_SUPPORT) == 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_CABAC_BYPASS_ALIGNMENT is not supported."
         " Ignoring the request for this feature flag on this encode session\n");
         // Disable it and keep going with a warning
         config.ConfigurationFlags &= ~D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_CABAC_BYPASS_ALIGNMENT;
   }

   if(((config.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_CABAC_BYPASS_ALIGNMENT) == 0)
         && ((pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.SupportFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC_FLAG_CABAC_BYPASS_ALIGNMENT_ENABLED_REQUIRED) != 0))
   {
         debug_printf("D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION arguments are not supported - D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_CABAC_BYPASS_ALIGNMENT is required to be set."
         " Enabling this HW required feature flag on this encode session\n");
         // HW doesn't support otherwise, so set it
         config.ConfigurationFlags |= D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_CABAC_BYPASS_ALIGNMENT;
   }

   return config;
}

static bool
d3d12_video_encoder_update_intra_refresh_hevc(struct d3d12_video_encoder *pD3D12Enc,
                                                        D3D12_VIDEO_SAMPLE srcTextureDesc,
                                                        struct pipe_h265_enc_picture_desc *  picture)
{
   if (picture->intra_refresh.mode != INTRA_REFRESH_MODE_NONE)
   {
      // D3D12 only supports row intra-refresh
      if (picture->intra_refresh.mode != INTRA_REFRESH_MODE_UNIT_ROWS)
      {
         debug_printf("[d3d12_video_encoder_update_intra_refresh_hevc] Unsupported INTRA_REFRESH_MODE %d\n", picture->intra_refresh.mode);
         return false;
      }

      uint8_t ctbSize = d3d12_video_encoder_convert_12cusize_to_pixel_size_hevc(
         pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps.MaxLumaCodingUnitSize);
      uint32_t total_frame_blocks = static_cast<uint32_t>(std::ceil(srcTextureDesc.Height / ctbSize)) *
                              static_cast<uint32_t>(std::ceil(srcTextureDesc.Width / ctbSize));
      D3D12_VIDEO_ENCODER_INTRA_REFRESH targetIntraRefresh = {
         D3D12_VIDEO_ENCODER_INTRA_REFRESH_MODE_ROW_BASED,
         total_frame_blocks / picture->intra_refresh.region_size,
      };
      double ir_wave_progress = (picture->intra_refresh.offset == 0) ? 0 :
         picture->intra_refresh.offset / (double) total_frame_blocks;
      pD3D12Enc->m_currentEncodeConfig.m_IntraRefreshCurrentFrameIndex =
         static_cast<uint32_t>(std::ceil(ir_wave_progress * targetIntraRefresh.IntraRefreshDuration));

      // Set intra refresh state
      pD3D12Enc->m_currentEncodeConfig.m_IntraRefresh = targetIntraRefresh;
      // Need to send the sequence flag during all the IR duration
      pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_intra_refresh;
   } else {
      pD3D12Enc->m_currentEncodeConfig.m_IntraRefreshCurrentFrameIndex = 0;
      pD3D12Enc->m_currentEncodeConfig.m_IntraRefresh = {
         D3D12_VIDEO_ENCODER_INTRA_REFRESH_MODE_NONE,
         0,
      };
   }

   return true;
}

bool
d3d12_video_encoder_update_current_encoder_config_state_hevc(struct d3d12_video_encoder *pD3D12Enc,
                                                             D3D12_VIDEO_SAMPLE srcTextureDesc,
                                                             struct pipe_picture_desc *picture)
{
   struct pipe_h265_enc_picture_desc *hevcPic = (struct pipe_h265_enc_picture_desc *) picture;

   // Reset reconfig dirty flags
   pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags = d3d12_video_encoder_config_dirty_flag_none;
   // Reset sequence changes flags
   pD3D12Enc->m_currentEncodeConfig.m_seqFlags = D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_NONE;

   // Set codec
   if (pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc != D3D12_VIDEO_ENCODER_CODEC_HEVC) {
      pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_codec;
   }
   pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc = D3D12_VIDEO_ENCODER_CODEC_HEVC;

   // Set VPS information
   if (memcmp(&pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificVideoStateDescH265,
              &hevcPic->vid,
              sizeof(hevcPic->vid)) != 0) {
      pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_video_header;
   }
   pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificVideoStateDescH265 = hevcPic->vid;

   // Set Sequence information
   if (memcmp(&pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificSequenceStateDescH265,
              &hevcPic->seq,
              sizeof(hevcPic->seq)) != 0) {
      pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_sequence_header;
   }
   pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificSequenceStateDescH265 = hevcPic->seq;
   pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificPictureStateDescH265 = hevcPic->pic;

   // Iterate over the headers the app requested and set flags to emit those for this frame
   util_dynarray_foreach(&hevcPic->raw_headers, struct pipe_enc_raw_header, header) {
      if (header->type == PIPE_H265_NAL_VPS)
         pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_video_header;
      else if (header->type == PIPE_H265_NAL_SPS)
         pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_sequence_header;
      else if (header->type == PIPE_H265_NAL_PPS)
         pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_picture_header;
      else if (header->type == PIPE_H265_NAL_AUD)
         pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_aud_header;
   }

   // Set input format
   DXGI_FORMAT targetFmt = d3d12_convert_pipe_video_profile_to_dxgi_format(pD3D12Enc->base.profile);
   if (pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format != targetFmt) {
      pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_input_format;
   }

   pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo = {};
   pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format = targetFmt;
   HRESULT hr = pD3D12Enc->m_pD3D12Screen->dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO,
                                                          &pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo,
                                                          sizeof(pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo));
   if (FAILED(hr)) {
      debug_printf("CheckFeatureSupport failed with HR %x\n", hr);
      return false;
   }

   // Set resolution
   if ((pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Width != srcTextureDesc.Width) ||
       (pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Height != srcTextureDesc.Height)) {
      pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_resolution;
   }
   pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Width = srcTextureDesc.Width;
   pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Height = srcTextureDesc.Height;

   // Set resolution codec dimensions (ie. cropping)
   memset(&pD3D12Enc->m_currentEncodeConfig.m_FrameCroppingCodecConfig, 0,
         sizeof(pD3D12Enc->m_currentEncodeConfig.m_FrameCroppingCodecConfig));
   pD3D12Enc->m_currentEncodeConfig.m_FrameCroppingCodecConfig.front = hevcPic->seq.pic_width_in_luma_samples;
   pD3D12Enc->m_currentEncodeConfig.m_FrameCroppingCodecConfig.back = hevcPic->seq.pic_height_in_luma_samples;
   if (hevcPic->seq.conformance_window_flag) {
      pD3D12Enc->m_currentEncodeConfig.m_FrameCroppingCodecConfig.left = hevcPic->seq.conf_win_left_offset;
      pD3D12Enc->m_currentEncodeConfig.m_FrameCroppingCodecConfig.right = hevcPic->seq.conf_win_right_offset;
      pD3D12Enc->m_currentEncodeConfig.m_FrameCroppingCodecConfig.top = hevcPic->seq.conf_win_top_offset;
      pD3D12Enc->m_currentEncodeConfig.m_FrameCroppingCodecConfig.bottom = hevcPic->seq.conf_win_bottom_offset;
   }
   // Set profile
   auto targetProfile = d3d12_video_encoder_convert_profile_to_d3d12_enc_profile_hevc(pD3D12Enc->base.profile);
   if (pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_HEVCProfile != targetProfile) {
      pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_profile;
   }
   pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_HEVCProfile = targetProfile;

   // Set level
   auto targetLevel = d3d12_video_encoder_convert_level_hevc(hevcPic->seq.general_level_idc);
   auto targetTier = (hevcPic->seq.general_tier_flag == 0) ? D3D12_VIDEO_ENCODER_TIER_HEVC_MAIN : D3D12_VIDEO_ENCODER_TIER_HEVC_HIGH;
   if ( (pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_HEVCLevelSetting.Level != targetLevel)
      || (pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_HEVCLevelSetting.Tier != targetTier)) {
      pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_level;
   }
   pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_HEVCLevelSetting.Tier = targetTier;
   pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_HEVCLevelSetting.Level = targetLevel;

   // Set codec config
   bool is_supported = true;
   auto targetCodecConfig = d3d12_video_encoder_convert_hevc_codec_configuration(pD3D12Enc, hevcPic, is_supported);
   if(!is_supported) {
      return false;
   }

   if (memcmp(&pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_HEVCConfig,
              &targetCodecConfig,
              sizeof(D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC)) != 0) {
      pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_codec_config;
   }
   pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_HEVCConfig = targetCodecConfig;

   // Set rate control
   d3d12_video_encoder_update_current_rate_control_hevc(pD3D12Enc, hevcPic);

   // Set GOP config
   if(!d3d12_video_encoder_update_hevc_gop_configuration(pD3D12Enc, hevcPic)) {
      debug_printf("d3d12_video_encoder_update_hevc_gop_configuration failed!\n");
      return false;
   }

   ///
   /// Check for video encode support detailed capabilities
   ///

   // Will call for d3d12 driver support based on the initial requested features, then
   // try to fallback if any of them is not supported and return the negotiated d3d12 settings
   D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT1 capEncoderSupportData1 = {};
   // Get max number of slices per frame supported
   if (hevcPic->num_slice_descriptors > 1)
      pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode =
         D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_SUBREGIONS_PER_FRAME;
   else
      pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode =
         D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME;

   if (!d3d12_video_encoder_negotiate_requested_features_and_d3d12_driver_caps(pD3D12Enc, capEncoderSupportData1)) {
      debug_printf("[d3d12_video_encoder_hevc] After negotiating caps, D3D12_FEATURE_VIDEO_ENCODER_SUPPORT1 "
                      "arguments are not supported - "
                      "ValidationFlags: 0x%x - SupportFlags: 0x%x\n",
                      capEncoderSupportData1.ValidationFlags,
                      capEncoderSupportData1.SupportFlags);
      return false;
   }

   // Set slices config  (configure before calling d3d12_video_encoder_calculate_max_slices_count_in_output)
   if(!d3d12_video_encoder_negotiate_current_hevc_slices_configuration(pD3D12Enc, hevcPic)) {
      debug_printf("d3d12_video_encoder_negotiate_current_hevc_slices_configuration failed!\n");
      return false;
   }

   ///
   // Calculate current settings based on the returned values from the caps query
   //
   pD3D12Enc->m_currentEncodeCapabilities.m_MaxSlicesInOutput =
      d3d12_video_encoder_calculate_max_slices_count_in_output(
         pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode,
         &pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigDesc.m_SlicesPartition_HEVC,
         pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.MaxSubregionsNumber,
         pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
         pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.SubregionBlockPixelsSize);

   // Set intra-refresh config
   if(!d3d12_video_encoder_update_intra_refresh_hevc(pD3D12Enc, srcTextureDesc, hevcPic)) {
      debug_printf("d3d12_video_encoder_update_intra_refresh_hevc failed!\n");
      return false;
   }

   // m_currentEncodeConfig.m_encoderPicParamsDesc pic params are set in d3d12_video_encoder_reconfigure_encoder_objects
   // after re-allocating objects if needed

   // Set motion estimation config
   auto targetMotionLimit = d3d12_video_encoder_convert_hevc_motion_configuration(pD3D12Enc, hevcPic);
   if (pD3D12Enc->m_currentEncodeConfig.m_encoderMotionPrecisionLimit != targetMotionLimit) {
      pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |=
         d3d12_video_encoder_config_dirty_flag_motion_precision_limit;
   }
   pD3D12Enc->m_currentEncodeConfig.m_encoderMotionPrecisionLimit = targetMotionLimit;

   //
   // Validate caps support returned values against current settings
   //
   if (pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_HEVCProfile !=
       pD3D12Enc->m_currentEncodeCapabilities.m_encoderSuggestedProfileDesc.m_HEVCProfile) {
      debug_printf("[d3d12_video_encoder_hevc] Warning: Requested D3D12_VIDEO_ENCODER_PROFILE_HEVC by upper layer: %d "
                    "mismatches UMD suggested D3D12_VIDEO_ENCODER_PROFILE_HEVC: %d\n",
                    pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_HEVCProfile,
                    pD3D12Enc->m_currentEncodeCapabilities.m_encoderSuggestedProfileDesc.m_HEVCProfile);
   }

   if (pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_HEVCLevelSetting.Tier !=
       pD3D12Enc->m_currentEncodeCapabilities.m_encoderLevelSuggestedDesc.m_HEVCLevelSetting.Tier) {
      debug_printf("[d3d12_video_encoder_hevc] Warning: Requested D3D12_VIDEO_ENCODER_LEVELS_HEVC.Tier by upper layer: %d "
                    "mismatches UMD suggested D3D12_VIDEO_ENCODER_LEVELS_HEVC.Tier: %d\n",
                    pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_HEVCLevelSetting.Tier,
                    pD3D12Enc->m_currentEncodeCapabilities.m_encoderLevelSuggestedDesc.m_HEVCLevelSetting.Tier);
   }

   if (pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_HEVCLevelSetting.Level !=
       pD3D12Enc->m_currentEncodeCapabilities.m_encoderLevelSuggestedDesc.m_HEVCLevelSetting.Level) {
      debug_printf("[d3d12_video_encoder_hevc] Warning: Requested D3D12_VIDEO_ENCODER_LEVELS_HEVC.Level by upper layer: %d "
                    "mismatches UMD suggested D3D12_VIDEO_ENCODER_LEVELS_HEVC.Level: %d\n",
                    pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_HEVCLevelSetting.Level,
                    pD3D12Enc->m_currentEncodeCapabilities.m_encoderLevelSuggestedDesc.m_HEVCLevelSetting.Level);
   }

   if (pD3D12Enc->m_currentEncodeCapabilities.m_MaxSlicesInOutput >
       pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.MaxSubregionsNumber) {
      debug_printf("[d3d12_video_encoder_hevc] Desired number of subregions %d is not supported (higher than max "
                      "reported slice number %d in query caps) for current resolution (%d, %d)\n.",
                      pD3D12Enc->m_currentEncodeCapabilities.m_MaxSlicesInOutput,
                      pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.MaxSubregionsNumber,
                      pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Width,
                      pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Height);
      return false;
   }
   return true;
}

D3D12_VIDEO_ENCODER_PROFILE_HEVC
d3d12_video_encoder_convert_profile_to_d3d12_enc_profile_hevc(enum pipe_video_profile profile)
{
   switch (profile) {
      case PIPE_VIDEO_PROFILE_HEVC_MAIN:
      {
         return D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN;

      } break;
      case PIPE_VIDEO_PROFILE_HEVC_MAIN_10:
      {
         return D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN10;
      } break;
      case PIPE_VIDEO_PROFILE_HEVC_MAIN_444:
      {
         return D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN_444;
      } break;
      case PIPE_VIDEO_PROFILE_HEVC_MAIN10_444:
      {
         return D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN10_444;
      } break;
      case PIPE_VIDEO_PROFILE_HEVC_MAIN_422:
      case PIPE_VIDEO_PROFILE_HEVC_MAIN10_422:
      {
         return D3D12_VIDEO_ENCODER_PROFILE_HEVC_MAIN10_422;
      } break;
      default:
      {
         unreachable("Unsupported pipe_video_profile");
      } break;
   }
}

bool
d3d12_video_encoder_isequal_slice_config_hevc(
   D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE targetMode,
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES targetConfig,
   D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE otherMode,
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES otherConfig)
{
   return (targetMode == otherMode) &&
          (memcmp(&targetConfig,
                  &otherConfig,
                  sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES)) == 0);
}

static inline bool
d3d12_video_encoder_needs_new_pps_hevc(struct d3d12_video_encoder *pD3D12Enc,
                                       bool writeNewSPS,
                                       HevcPicParameterSet &tentative_pps,
                                       const HevcPicParameterSet &active_pps)
{
   bool bUseSliceL0L1Override = (pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_HEVCPicData.Flags &
                                 D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_REQUEST_NUM_REF_IDX_ACTIVE_OVERRIDE_FLAG_SLICE) != 0;

   bool bDifferentL0L1Lists = !bUseSliceL0L1Override &&
         ((tentative_pps.num_ref_idx_lx_default_active_minus1[0] != active_pps.num_ref_idx_lx_default_active_minus1[0]) ||
         (tentative_pps.num_ref_idx_lx_default_active_minus1[1] != active_pps.num_ref_idx_lx_default_active_minus1[1]));

   size_t offset_before_l0l1 = offsetof(HevcPicParameterSet, num_ref_idx_lx_default_active_minus1[0]);
   size_t offset_after_l0l1 = offset_before_l0l1 + sizeof(HevcPicParameterSet::num_ref_idx_lx_default_active_minus1);
   bool bDidPPSChange = memcmp(&tentative_pps, &active_pps, offset_before_l0l1) ||
                        bDifferentL0L1Lists ||
                        memcmp(reinterpret_cast<uint8_t*>(&tentative_pps) + offset_after_l0l1,
                               reinterpret_cast<const uint8_t*>(&active_pps) + offset_after_l0l1,
                               sizeof(HevcPicParameterSet) - offset_after_l0l1);

   return writeNewSPS || bDidPPSChange;
}

uint32_t
d3d12_video_encoder_build_codec_headers_hevc(struct d3d12_video_encoder *pD3D12Enc,
                                             std::vector<uint64_t> &pWrittenCodecUnitsSizes)
{
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA currentPicParams =
      d3d12_video_encoder_get_current_picture_param_settings(pD3D12Enc);

   auto profDesc = d3d12_video_encoder_get_current_profile_desc(pD3D12Enc);
   auto levelDesc = d3d12_video_encoder_get_current_level_desc(pD3D12Enc);
   auto codecConfigDesc = d3d12_video_encoder_get_current_codec_config_desc(pD3D12Enc);

   pWrittenCodecUnitsSizes.clear();
   bool isFirstFrame = (pD3D12Enc->m_fenceValue == 1);

   d3d12_video_bitstream_builder_hevc *pHEVCBitstreamBuilder =
      static_cast<d3d12_video_bitstream_builder_hevc *>(pD3D12Enc->m_upBitstreamBuilder.get());
   assert(pHEVCBitstreamBuilder);

   size_t writtenAUDBytesCount = 0;
   bool forceWriteAUD = (pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_aud_header) != 0;
   if (forceWriteAUD)
   {
      pHEVCBitstreamBuilder->write_aud(pD3D12Enc->m_BitstreamHeadersBuffer,
                                       pD3D12Enc->m_BitstreamHeadersBuffer.begin(),
                                       currentPicParams.pHEVCPicData->FrameType,
                                       writtenAUDBytesCount);
      pWrittenCodecUnitsSizes.push_back(writtenAUDBytesCount);
   }

   uint8_t active_seq_parameter_set_id = pHEVCBitstreamBuilder->get_active_sps().sps_seq_parameter_set_id;
   uint8_t active_video_parameter_set_id = pHEVCBitstreamBuilder->get_active_vps().vps_video_parameter_set_id;
   
   bool writeNewVPS = isFirstFrame || (pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_video_header) != 0;

   size_t writtenVPSBytesCount = 0;
   if (writeNewVPS) {
      bool gopHasBFrames = (pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_HEVCGroupOfPictures.PPicturePeriod > 1);
      HevcVideoParameterSet vps = pHEVCBitstreamBuilder->build_vps(pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificVideoStateDescH265,
                                                                  *profDesc.pHEVCProfile,
                                                                   *levelDesc.pHEVCLevelSetting,
                                                                   pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
                                                                   gopHasBFrames,
                                                                   active_video_parameter_set_id,
                                                                   pD3D12Enc->m_BitstreamHeadersBuffer,
                                                                   pD3D12Enc->m_BitstreamHeadersBuffer.begin() + writtenAUDBytesCount,
                                                                   writtenVPSBytesCount);
      pHEVCBitstreamBuilder->set_active_vps(vps);
      pWrittenCodecUnitsSizes.push_back(writtenVPSBytesCount);
   }

   bool forceWriteSPS = (pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_sequence_header) != 0;
   bool writeNewSPS = writeNewVPS                                          // on new VPS written
                      || ((pD3D12Enc->m_currentEncodeConfig.m_seqFlags &   // also on resolution change
                           D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_RESOLUTION_CHANGE) != 0)
                      || forceWriteSPS;

   size_t writtenSPSBytesCount = 0;
   if (writeNewSPS) {
      HevcSeqParameterSet sps = pHEVCBitstreamBuilder->build_sps(pHEVCBitstreamBuilder->get_active_vps(),
                                                                 pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificSequenceStateDescH265,
                                                                 active_seq_parameter_set_id,
                                                                 pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
                                                                 pD3D12Enc->m_currentEncodeConfig.m_FrameCroppingCodecConfig,
                                                                 pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.SubregionBlockPixelsSize,
                                                                 pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
                                                                 *codecConfigDesc.pHEVCConfig,
                                                                 pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_HEVCGroupOfPictures,
                                                                 pD3D12Enc->m_BitstreamHeadersBuffer,
                                                                 pD3D12Enc->m_BitstreamHeadersBuffer.begin() + writtenAUDBytesCount + writtenVPSBytesCount,
                                                                 writtenSPSBytesCount);
      pHEVCBitstreamBuilder->set_active_sps(sps);
      pWrittenCodecUnitsSizes.push_back(writtenSPSBytesCount);
   }

   size_t writtenPPSBytesCount = 0;
   HevcPicParameterSet tentative_pps = pHEVCBitstreamBuilder->build_pps(pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificPictureStateDescH265,
                                                                        pHEVCBitstreamBuilder->get_active_sps(),
                                                                        static_cast<uint8_t>(currentPicParams.pHEVCPicData->slice_pic_parameter_set_id),
                                                                        *codecConfigDesc.pHEVCConfig,
                                                                        *currentPicParams.pHEVCPicData1,
                                                                        pD3D12Enc->m_StagingHeadersBuffer,
                                                                        pD3D12Enc->m_StagingHeadersBuffer.begin(),
                                                                        writtenPPSBytesCount);

   const HevcPicParameterSet &active_pps = pHEVCBitstreamBuilder->get_active_pps();
   bool forceWritePPS = (pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_picture_header) != 0;
   if (forceWritePPS || d3d12_video_encoder_needs_new_pps_hevc(pD3D12Enc, writeNewSPS, tentative_pps, active_pps)) {
      pHEVCBitstreamBuilder->set_active_pps(tentative_pps);
      pD3D12Enc->m_BitstreamHeadersBuffer.resize(writtenAUDBytesCount + writtenVPSBytesCount + writtenSPSBytesCount + writtenPPSBytesCount);
      memcpy(&pD3D12Enc->m_BitstreamHeadersBuffer.data()[(writtenAUDBytesCount + writtenVPSBytesCount + writtenSPSBytesCount)], pD3D12Enc->m_StagingHeadersBuffer.data(), writtenPPSBytesCount);
      pWrittenCodecUnitsSizes.push_back(writtenPPSBytesCount);
   } else {
      writtenPPSBytesCount = 0;
      debug_printf("Skipping PPS (same as active PPS) for fenceValue: %" PRIu64 "\n", pD3D12Enc->m_fenceValue);
   }

   // Shrink buffer to fit the headers
   if (pD3D12Enc->m_BitstreamHeadersBuffer.size() > (writtenAUDBytesCount + writtenVPSBytesCount + writtenSPSBytesCount + writtenPPSBytesCount)) {
      pD3D12Enc->m_BitstreamHeadersBuffer.resize(writtenAUDBytesCount + writtenVPSBytesCount + writtenSPSBytesCount + writtenPPSBytesCount);
   }

   assert(std::accumulate(pWrittenCodecUnitsSizes.begin(), pWrittenCodecUnitsSizes.end(), 0ull) ==
      pD3D12Enc->m_BitstreamHeadersBuffer.size());
   return static_cast<uint32_t>(pD3D12Enc->m_BitstreamHeadersBuffer.size());
}
