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

#include "d3d12_video_encoder_bitstream_builder_h264.h"

#include <cmath>

inline H264_SPEC_PROFILES
Convert12ToSpecH264Profiles(D3D12_VIDEO_ENCODER_PROFILE_H264 profile12)
{
   switch (profile12) {
      case D3D12_VIDEO_ENCODER_PROFILE_H264_MAIN:
      {
         return H264_PROFILE_MAIN;
      } break;
      case D3D12_VIDEO_ENCODER_PROFILE_H264_HIGH:
      {
         return H264_PROFILE_HIGH;
      } break;
      case D3D12_VIDEO_ENCODER_PROFILE_H264_HIGH_10:
      {
         return H264_PROFILE_HIGH10;
      } break;
      default:
      {
         unreachable("Unsupported D3D12_VIDEO_ENCODER_PROFILE_H264");
      } break;
   }
}

void
d3d12_video_bitstream_builder_h264::build_sps(const D3D12_VIDEO_ENCODER_PROFILE_H264 &               profile,
                                              const D3D12_VIDEO_ENCODER_LEVELS_H264 &                level,
                                              const DXGI_FORMAT &                                    inputFmt,
                                              const D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264 &   codecConfig,
                                              const D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE_H264 &gopConfig,
                                              uint32_t                                    seq_parameter_set_id,
                                              uint32_t                                    max_num_ref_frames,
                                              D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC sequenceTargetResolution,
                                              D3D12_BOX                                   frame_cropping_codec_config,
                                              std::vector<uint8_t> &                      headerBitstream,
                                              std::vector<uint8_t>::iterator              placingPositionStart,
                                              size_t &                                    writtenBytes)
{
   H264_SPEC_PROFILES profile_idc          = Convert12ToSpecH264Profiles(profile);
   uint32_t           constraint_set3_flag = 0;
   uint32_t           level_idc            = 0;
   d3d12_video_encoder_convert_from_d3d12_level_h264(
      level,
      level_idc,
      constraint_set3_flag /*Always 0 except if level is 11 or 1b in which case 0 means 11, 1 means 1b*/);

   // constraint_set3_flag is for Main profile only and levels 11 or 1b: levels 11 if off, level 1b if on. Always 0 for
   // HIGH/HIGH10 profiles
   if ((profile == D3D12_VIDEO_ENCODER_PROFILE_H264_HIGH) || (profile == D3D12_VIDEO_ENCODER_PROFILE_H264_HIGH_10)) {
      // Force 0 for high profiles
      constraint_set3_flag = 0;
   }

   assert((inputFmt == DXGI_FORMAT_NV12) || (inputFmt == DXGI_FORMAT_P010));

   // Assume NV12 YUV 420 8 bits
   uint32_t bit_depth_luma_minus8   = 0;
   uint32_t bit_depth_chroma_minus8 = 0;

   // In case is 420 10 bits fix it
   if (inputFmt == DXGI_FORMAT_P010) {
      bit_depth_luma_minus8   = 2;
      bit_depth_chroma_minus8 = 2;
   }

   // Calculate sequence resolution sizes in MBs
   // Always in MBs since we don't support interlace in D3D12 Encode
   uint32_t pic_width_in_mbs_minus1 = static_cast<uint32_t>(std::ceil(sequenceTargetResolution.Width / 16.0)) - 1;
   uint32_t pic_height_in_map_units_minus1 =
      static_cast<uint32_t>(std::ceil(sequenceTargetResolution.Height / 16.0)) - 1;

   uint32_t frame_cropping_flag               = 0;
   if (frame_cropping_codec_config.left 
      || frame_cropping_codec_config.right 
      || frame_cropping_codec_config.top
      || frame_cropping_codec_config.bottom
   ) {
      frame_cropping_flag               = 1;
   }

   H264_SPS spsStructure = { static_cast<uint32_t>(profile_idc),
                             constraint_set3_flag,
                             level_idc,
                             seq_parameter_set_id,
                             bit_depth_luma_minus8,
                             bit_depth_chroma_minus8,
                             gopConfig.log2_max_frame_num_minus4,
                             gopConfig.pic_order_cnt_type,
                             gopConfig.log2_max_pic_order_cnt_lsb_minus4,
                             max_num_ref_frames,
                             0,   // gaps_in_frame_num_value_allowed_flag
                             pic_width_in_mbs_minus1,
                             pic_height_in_map_units_minus1,
                             ((codecConfig.ConfigurationFlags &
                               D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_FLAG_USE_ADAPTIVE_8x8_TRANSFORM) != 0) ?
                                1u :
                                0u,   // direct_8x8_inference_flag
                             frame_cropping_flag,
                             frame_cropping_codec_config.left,
                             frame_cropping_codec_config.right,
                             frame_cropping_codec_config.top,
                             frame_cropping_codec_config.bottom };

   // Print built PPS structure
   debug_printf(
      "[D3D12 d3d12_video_bitstream_builder_h264] H264_SPS Structure generated before writing to bitstream:\n");
   print_sps(spsStructure);

   // Convert the H264 SPS structure into bytes
   m_h264Encoder.sps_to_nalu_bytes(&spsStructure, headerBitstream, placingPositionStart, writtenBytes);
}

void
d3d12_video_bitstream_builder_h264::write_end_of_stream_nalu(std::vector<uint8_t> &         headerBitstream,
                                                             std::vector<uint8_t>::iterator placingPositionStart,
                                                             size_t &                       writtenBytes)
{
   m_h264Encoder.write_end_of_stream_nalu(headerBitstream, placingPositionStart, writtenBytes);
}

void
d3d12_video_bitstream_builder_h264::write_end_of_sequence_nalu(std::vector<uint8_t> &         headerBitstream,
                                                               std::vector<uint8_t>::iterator placingPositionStart,
                                                               size_t &                       writtenBytes)
{
   m_h264Encoder.write_end_of_sequence_nalu(headerBitstream, placingPositionStart, writtenBytes);
}

void
d3d12_video_bitstream_builder_h264::build_pps(const D3D12_VIDEO_ENCODER_PROFILE_H264 &                   profile,
                                              const D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264 &       codecConfig,
                                              const D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264 &pictureControl,
                                              uint32_t                       pic_parameter_set_id,
                                              uint32_t                       seq_parameter_set_id,
                                              std::vector<uint8_t> &         headerBitstream,
                                              std::vector<uint8_t>::iterator placingPositionStart,
                                              size_t &                       writtenBytes)
{
   BOOL bIsHighProfile =
      ((profile == D3D12_VIDEO_ENCODER_PROFILE_H264_HIGH) || (profile == D3D12_VIDEO_ENCODER_PROFILE_H264_HIGH_10));

   H264_PPS ppsStructure = {
      pic_parameter_set_id,
      seq_parameter_set_id,
      ((codecConfig.ConfigurationFlags & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_FLAG_ENABLE_CABAC_ENCODING) != 0) ?
         1u :
         0u,   // entropy_coding_mode_flag
      0,   // pic_order_present_flag (bottom_field_pic_order_in_frame_present_flag) - will use pic_cnt 0 or 2, always
           // off ; used with pic_cnt_type 1 and deltas.
      static_cast<uint32_t>(std::max(static_cast<int32_t>(pictureControl.List0ReferenceFramesCount) - 1,
                                     0)),   // num_ref_idx_l0_active_minus1
      static_cast<uint32_t>(std::max(static_cast<int32_t>(pictureControl.List1ReferenceFramesCount) - 1,
                                     0)),   // num_ref_idx_l1_active_minus1
      ((codecConfig.ConfigurationFlags &
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_FLAG_USE_CONSTRAINED_INTRAPREDICTION) != 0) ?
         1u :
         0u,   // constrained_intra_pred_flag
      ((codecConfig.ConfigurationFlags &
        D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264_FLAG_USE_ADAPTIVE_8x8_TRANSFORM) != 0) ?
         1u :
         0u   // transform_8x8_mode_flag
   };

   // Print built PPS structure
   debug_printf(
      "[D3D12 d3d12_video_bitstream_builder_h264] H264_PPS Structure generated before writing to bitstream:\n");
   print_pps(ppsStructure);

   // Convert the H264 SPS structure into bytes
   m_h264Encoder.pps_to_nalu_bytes(&ppsStructure, headerBitstream, bIsHighProfile, placingPositionStart, writtenBytes);
}

void
d3d12_video_bitstream_builder_h264::print_pps(const H264_PPS &pps)
{
   // Be careful that build_pps also wraps some other NALU bytes in pps_to_nalu_bytes so bitstream returned by build_pps
   // won't be exactly the bytes from the H264_PPS struct

   static_assert(sizeof(H264_PPS) ==
                 (sizeof(uint32_t) *
                  8), "Update the number of uint32_t in struct in assert and add case below if structure changes");

   // Declared fields from definition in d3d12_video_encoder_bitstream_builder_h264.h

   debug_printf("[D3D12 d3d12_video_bitstream_builder_h264] H264_PPS values below:\n");
   debug_printf("pic_parameter_set_id: %d\n", pps.pic_parameter_set_id);
   debug_printf("seq_parameter_set_id: %d\n", pps.seq_parameter_set_id);
   debug_printf("entropy_coding_mode_flag: %d\n", pps.entropy_coding_mode_flag);
   debug_printf("pic_order_present_flag: %d\n", pps.pic_order_present_flag);
   debug_printf("num_ref_idx_l0_active_minus1: %d\n", pps.num_ref_idx_l0_active_minus1);
   debug_printf("num_ref_idx_l1_active_minus1: %d\n", pps.num_ref_idx_l1_active_minus1);
   debug_printf("constrained_intra_pred_flag: %d\n", pps.constrained_intra_pred_flag);
   debug_printf("transform_8x8_mode_flag: %d\n", pps.transform_8x8_mode_flag);
   debug_printf(
      "[D3D12 d3d12_video_bitstream_builder_h264] H264_PPS values end\n--------------------------------------\n");
}

void
d3d12_video_bitstream_builder_h264::print_sps(const H264_SPS &sps)
{
   // Be careful when calling this method that build_sps also wraps some other NALU bytes in sps_to_nalu_bytes so
   // bitstream returned by build_sps won't be exactly the bytes from the H264_SPS struct From definition in
   // d3d12_video_encoder_bitstream_builder_h264.h

   static_assert(sizeof(H264_SPS) ==
                 (sizeof(uint32_t) *
                  19), "Update the number of uint32_t in struct in assert and add case below if structure changes");

   // Declared fields from definition in d3d12_video_encoder_bitstream_builder_h264.h

   debug_printf("[D3D12 d3d12_video_bitstream_builder_h264] H264_SPS values below:\n");
   debug_printf("profile_idc: %d\n", sps.profile_idc);
   debug_printf("constraint_set3_flag: %d\n", sps.constraint_set3_flag);
   debug_printf("level_idc: %d\n", sps.level_idc);
   debug_printf("seq_parameter_set_id: %d\n", sps.seq_parameter_set_id);
   debug_printf("bit_depth_luma_minus8: %d\n", sps.bit_depth_luma_minus8);
   debug_printf("bit_depth_chroma_minus8: %d\n", sps.bit_depth_chroma_minus8);
   debug_printf("log2_max_frame_num_minus4: %d\n", sps.log2_max_frame_num_minus4);
   debug_printf("pic_order_cnt_type: %d\n", sps.pic_order_cnt_type);
   debug_printf("log2_max_pic_order_cnt_lsb_minus4: %d\n", sps.log2_max_pic_order_cnt_lsb_minus4);
   debug_printf("max_num_ref_frames: %d\n", sps.max_num_ref_frames);
   debug_printf("gaps_in_frame_num_value_allowed_flag: %d\n", sps.gaps_in_frame_num_value_allowed_flag);
   debug_printf("pic_width_in_mbs_minus1: %d\n", sps.pic_width_in_mbs_minus1);
   debug_printf("pic_height_in_map_units_minus1: %d\n", sps.pic_height_in_map_units_minus1);
   debug_printf("direct_8x8_inference_flag: %d\n", sps.direct_8x8_inference_flag);
   debug_printf("frame_cropping_flag: %d\n", sps.frame_cropping_flag);
   debug_printf("frame_cropping_rect_left_offset: %d\n", sps.frame_cropping_rect_left_offset);
   debug_printf("frame_cropping_rect_right_offset: %d\n", sps.frame_cropping_rect_right_offset);
   debug_printf("frame_cropping_rect_top_offset: %d\n", sps.frame_cropping_rect_top_offset);
   debug_printf("frame_cropping_rect_bottom_offset: %d\n", sps.frame_cropping_rect_bottom_offset);
   debug_printf(
      "[D3D12 d3d12_video_bitstream_builder_h264] H264_SPS values end\n--------------------------------------\n");
}
