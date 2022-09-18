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

void
d3d12_video_encoder_update_current_rate_control_hevc(struct d3d12_video_encoder *pD3D12Enc,
                                                     pipe_h265_enc_picture_desc *picture)
{
   auto previousConfig = pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc;

   pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc = {};
   pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_FrameRate.Numerator =
      picture->rc.frame_rate_num;
   pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_FrameRate.Denominator =
      picture->rc.frame_rate_den;
   pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Flags = D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_NONE;

   switch (picture->rc.rate_ctrl_method) {
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE_SKIP:
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_VARIABLE:
      {
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Mode = D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_VBR.TargetAvgBitRate =
            picture->rc.target_bitrate;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_VBR.PeakBitRate =
            picture->rc.peak_bitrate;
      } break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT_SKIP:
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_CONSTANT:
      {
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Mode = D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CBR.TargetBitRate =
            picture->rc.target_bitrate;

         /* For CBR mode, to guarantee bitrate of generated stream complies with
          * target bitrate (e.g. no over +/-10%), vbv_buffer_size should be same
          * as target bitrate. Controlled by OS env var D3D12_VIDEO_ENC_CBR_FORCE_VBV_EQUAL_BITRATE
          */
         if (D3D12_VIDEO_ENC_CBR_FORCE_VBV_EQUAL_BITRATE) {
            debug_printf("[d3d12_video_encoder_hevc] d3d12_video_encoder_update_current_rate_control_hevc D3D12_VIDEO_ENC_CBR_FORCE_VBV_EQUAL_BITRATE environment variable is set, "
                       ", forcing VBV Size = Target Bitrate = %" PRIu64 " (bits)\n", pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CBR.TargetBitRate/2);
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Flags |=
               D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CBR.VBVCapacity =
               pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CBR.TargetBitRate/2;
         }

      } break;
      case PIPE_H2645_ENC_RATE_CONTROL_METHOD_DISABLE:
      {
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Mode = D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP
            .ConstantQP_FullIntracodedFrame = picture->rc.quant_i_frames;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP
            .ConstantQP_InterPredictedFrame_PrevRefOnly = picture->rc.quant_p_frames;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP
            .ConstantQP_InterPredictedFrame_BiDirectionalRef = picture->rc.quant_b_frames;
      } break;
      default:
      {
         debug_printf("[d3d12_video_encoder_hevc] d3d12_video_encoder_update_current_rate_control_hevc invalid RC "
                       "config, using default RC CQP mode\n");
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Mode = D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP
            .ConstantQP_FullIntracodedFrame = 30;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP
            .ConstantQP_InterPredictedFrame_PrevRefOnly = 30;
         pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP
            .ConstantQP_InterPredictedFrame_BiDirectionalRef = 30;
      } break;
   }

   if (memcmp(&previousConfig,
              &pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc,
              sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc)) != 0) {
      pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_rate_control;
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

   bUsedAsReference = !hevcPic->not_referenced;

   picParams.pHEVCPicData->slice_pic_parameter_set_id = pHEVCBitstreamBuilder->get_active_pps_id();
   picParams.pHEVCPicData->FrameType = d3d12_video_encoder_convert_frame_type_hevc(hevcPic->picture_type);
   picParams.pHEVCPicData->PictureOrderCountNumber = hevcPic->pic_order_cnt;

   picParams.pHEVCPicData->List0ReferenceFramesCount = 0;
   picParams.pHEVCPicData->pList0ReferenceFrames = nullptr;
   picParams.pHEVCPicData->List1ReferenceFramesCount = 0;
   picParams.pHEVCPicData->pList1ReferenceFrames = nullptr;

   if (picParams.pHEVCPicData->FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_P_FRAME) {
      picParams.pHEVCPicData->List0ReferenceFramesCount = hevcPic->num_ref_idx_l0_active_minus1 + 1;
      picParams.pHEVCPicData->pList0ReferenceFrames = hevcPic->ref_idx_l0_list;
   } else if (picParams.pHEVCPicData->FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_B_FRAME) {
      picParams.pHEVCPicData->List0ReferenceFramesCount = hevcPic->num_ref_idx_l0_active_minus1 + 1;
      picParams.pHEVCPicData->pList0ReferenceFrames = hevcPic->ref_idx_l0_list;
      picParams.pHEVCPicData->List1ReferenceFramesCount = hevcPic->num_ref_idx_l1_active_minus1 + 1;
      picParams.pHEVCPicData->pList1ReferenceFrames = hevcPic->ref_idx_l1_list;
   }

   if ((pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_HEVCConfig.ConfigurationFlags 
      & D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_FLAG_ALLOW_REQUEST_INTRA_CONSTRAINED_SLICES) != 0)
      picParams.pHEVCPicData->Flags |= D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC_FLAG_REQUEST_INTRA_CONSTRAINED_SLICES;
}

D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC
d3d12_video_encoder_convert_frame_type_hevc(enum pipe_h2645_enc_picture_type picType)
{
   switch (picType) {
      case PIPE_H2645_ENC_PICTURE_TYPE_P:
      {
         return D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_P_FRAME;
      } break;
      case PIPE_H2645_ENC_PICTURE_TYPE_B:
      {
         return D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_B_FRAME;
      } break;
      case PIPE_H2645_ENC_PICTURE_TYPE_I:
      {
         return D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_I_FRAME;
      } break;
      case PIPE_H2645_ENC_PICTURE_TYPE_IDR:
      {
         return D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_IDR_FRAME;
      } break;
      default:
      {
         unreachable("Unsupported pipe_h2645_enc_picture_type");
      } break;
   }
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
   if (picture->num_slice_descriptors > 1) {
      /* Last slice can be less for rounding frame size and leave some error for mb rounding */
      bool bUniformSizeSlices = true;
      const double rounding_delta = 1.0;
      for (uint32_t sliceIdx = 1; (sliceIdx < picture->num_slice_descriptors - 1) && bUniformSizeSlices; sliceIdx++) {
         int64_t curSlice = picture->slices_descriptors[sliceIdx].num_ctu_in_slice;
         int64_t prevSlice = picture->slices_descriptors[sliceIdx - 1].num_ctu_in_slice;
         bUniformSizeSlices = bUniformSizeSlices && (std::abs(curSlice - prevSlice) <= rounding_delta);
      }

      uint32_t subregion_block_pixel_size = pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.SubregionBlockPixelsSize;
      uint32_t num_subregions_per_scanline =
         pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Width / subregion_block_pixel_size;

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
      uint32_t num_subregions_per_slice = picture->slices_descriptors[0].num_ctu_in_slice / (subregionsize_to_ctu_factor*subregionsize_to_ctu_factor);

      bool bSliceAligned = ((num_subregions_per_slice % num_subregions_per_scanline) == 0);

      if (!bUniformSizeSlices &&
          d3d12_video_encoder_check_subregion_mode_support(
             pD3D12Enc,
             D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_SUBREGIONS_PER_FRAME)) {

         if (D3D12_VIDEO_ENC_FALLBACK_SLICE_CONFIG) {   // Check if fallback mode is enabled, or we should just fail
                                                        // without support
            // Not supported to have custom slice sizes in D3D12 Video Encode fallback to uniform multi-slice
            debug_printf(
               "[d3d12_video_encoder_hevc] WARNING: Requested slice control mode is not supported: All slices must "
               "have the same number of macroblocks. Falling back to encoding uniform %d slices per frame.\n",
               picture->num_slice_descriptors);
            requestedSlicesMode =
               D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_SUBREGIONS_PER_FRAME;
            requestedSlicesConfig.NumberOfSlicesPerFrame = picture->num_slice_descriptors;
            debug_printf("[d3d12_video_encoder_hevc] Using multi slice encoding mode: "
                           "D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_SUBREGIONS_PER_FRAME "
                           "with %d slices per frame.\n",
                           requestedSlicesConfig.NumberOfSlicesPerFrame);
         } else {
            debug_printf("[d3d12_video_encoder_hevc] Requested slice control mode is not supported: All slices must "
                            "have the same number of macroblocks. To continue with uniform slices as a fallback, must "
                            "enable the OS environment variable D3D12_VIDEO_ENC_FALLBACK_SLICE_CONFIG");
            return false;
         }
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
      } else if (bUniformSizeSlices &&
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
      } else if (D3D12_VIDEO_ENC_FALLBACK_SLICE_CONFIG) {   // Check if fallback mode is enabled, or we should just fail
                                                            // without support
         // Fallback to single slice encoding (assigned by default when initializing variables requestedSlicesMode,
         // requestedSlicesConfig)
         debug_printf(
            "[d3d12_video_encoder_hevc] WARNING: Slice mode for %d slices with bUniformSizeSlices: %d - bSliceAligned "
            "%d not supported by the D3D12 driver, falling back to encoding a single slice per frame.\n",
            picture->num_slice_descriptors,
            bUniformSizeSlices,
            bSliceAligned);
      } else {
         debug_printf("[d3d12_video_encoder_hevc] Requested slice control mode is not supported: All slices must "
                         "have the same number of macroblocks. To continue with uniform slices as a fallback, must "
                         "enable the OS environment variable D3D12_VIDEO_ENC_FALLBACK_SLICE_CONFIG\n");
         return false;
      }
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
   if (picture->picture_type == PIPE_H2645_ENC_PICTURE_TYPE_IDR) {
      uint32_t GOPLength = picture->seq.intra_period;
      uint32_t PPicturePeriod = picture->seq.ip_period;

      const uint32_t max_pic_order_cnt_lsb = MAX2(16, util_next_power_of_two(GOPLength));
      double log2_max_pic_order_cnt_lsb_minus4 = std::max(0.0, std::ceil(std::log2(max_pic_order_cnt_lsb)) - 4);
      assert(log2_max_pic_order_cnt_lsb_minus4 < UCHAR_MAX);

      // Set dirty flag if m_HEVCGroupOfPictures changed
      auto previousGOPConfig = pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_HEVCGroupOfPictures;
      pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_HEVCGroupOfPictures = {
         GOPLength,
         PPicturePeriod,
         static_cast<uint8_t>(log2_max_pic_order_cnt_lsb_minus4)
      };

      if (memcmp(&previousGOPConfig,
                 &pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_HEVCGroupOfPictures,
                 sizeof(D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE_HEVC)) != 0) {
         pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_gop;
      }
   }
   return true;
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
   capCodecConfigData.CodecSupportLimits.pHEVCSupport = &pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps;
   capCodecConfigData.CodecSupportLimits.DataSize = sizeof(pD3D12Enc->m_currentEncodeCapabilities.m_encoderCodecSpecificConfigCaps.m_HEVCCodecCaps);

   if(FAILED(pD3D12Enc->m_spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT, &capCodecConfigData, sizeof(capCodecConfigData))
      || !capCodecConfigData.IsSupported))
   {
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

         is_supported = false;
         return config;
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

   return config;
}

bool
d3d12_video_encoder_update_current_encoder_config_state_hevc(struct d3d12_video_encoder *pD3D12Enc,
                                                             struct pipe_video_buffer *srcTexture,
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
   if ((pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Width != srcTexture->width) ||
       (pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Height != srcTexture->height)) {
      pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags |= d3d12_video_encoder_config_dirty_flag_resolution;
   }
   pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Width = srcTexture->width;
   pD3D12Enc->m_currentEncodeConfig.m_currentResolution.Height = srcTexture->height;

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

   ///
   /// Check for video encode support detailed capabilities
   ///

   // Will call for d3d12 driver support based on the initial requested features, then
   // try to fallback if any of them is not supported and return the negotiated d3d12 settings
   D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT capEncoderSupportData = {};
   if (!d3d12_video_encoder_negotiate_requested_features_and_d3d12_driver_caps(pD3D12Enc, capEncoderSupportData)) {
      debug_printf("[d3d12_video_encoder_hevc] After negotiating caps, D3D12_FEATURE_VIDEO_ENCODER_SUPPORT "
                      "arguments are not supported - "
                      "ValidationFlags: 0x%x - SupportFlags: 0x%x\n",
                      capEncoderSupportData.ValidationFlags,
                      capEncoderSupportData.SupportFlags);
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

   // Set slices config
   if(!d3d12_video_encoder_negotiate_current_hevc_slices_configuration(pD3D12Enc, hevcPic)) {
      debug_printf("d3d12_video_encoder_negotiate_current_hevc_slices_configuration failed!\n");
      return false;
   }

   // Set GOP config
   if(!d3d12_video_encoder_update_hevc_gop_configuration(pD3D12Enc, hevcPic)) {
      debug_printf("d3d12_video_encoder_update_hevc_gop_configuration failed!\n");
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
      debug_printf("[d3d12_video_encoder_hevc] Desired number of subregions is not supported (higher than max "
                      "reported slice number in query caps)\n.");
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

uint32_t
d3d12_video_encoder_build_codec_headers_hevc(struct d3d12_video_encoder *pD3D12Enc)
{
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA currentPicParams =
      d3d12_video_encoder_get_current_picture_param_settings(pD3D12Enc);

   auto profDesc = d3d12_video_encoder_get_current_profile_desc(pD3D12Enc);
   auto levelDesc = d3d12_video_encoder_get_current_level_desc(pD3D12Enc);
   auto codecConfigDesc = d3d12_video_encoder_get_current_codec_config_desc(pD3D12Enc);
   auto MaxDPBCapacity = d3d12_video_encoder_get_current_max_dpb_capacity(pD3D12Enc);

   size_t writtenSPSBytesCount = 0;
   size_t writtenVPSBytesCount = 0;
   bool isFirstFrame = (pD3D12Enc->m_fenceValue == 1);
   bool writeNewSPS = isFirstFrame                                         // on first frame
                      || ((pD3D12Enc->m_currentEncodeConfig.m_seqFlags &   // also on resolution change
                           D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_RESOLUTION_CHANGE) != 0);

   d3d12_video_bitstream_builder_hevc *pHEVCBitstreamBuilder =
      static_cast<d3d12_video_bitstream_builder_hevc *>(pD3D12Enc->m_upBitstreamBuilder.get());
   assert(pHEVCBitstreamBuilder);

   uint32_t active_seq_parameter_set_id = pHEVCBitstreamBuilder->get_active_sps_id();
   uint32_t active_video_parameter_set_id = pHEVCBitstreamBuilder->get_active_vps_id();
   
   bool writeNewVPS = isFirstFrame;
   
   if (writeNewVPS) {
      bool gopHasBFrames = (pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_HEVCGroupOfPictures.PPicturePeriod > 1);
      pHEVCBitstreamBuilder->build_vps(*profDesc.pHEVCProfile,
                                       *levelDesc.pHEVCLevelSetting,
                                       pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
                                       MaxDPBCapacity,   // max_num_ref_frames
                                       gopHasBFrames,
                                       active_video_parameter_set_id,
                                       pD3D12Enc->m_BitstreamHeadersBuffer,
                                       pD3D12Enc->m_BitstreamHeadersBuffer.begin(),
                                       writtenVPSBytesCount);      
   }

   if (writeNewSPS) {
      // For every new SPS for reconfiguration, increase the active_sps_id
      if (!isFirstFrame) {
         active_seq_parameter_set_id++;
         pHEVCBitstreamBuilder->set_active_sps_id(active_seq_parameter_set_id);
      }

      pHEVCBitstreamBuilder->build_sps(
                           pHEVCBitstreamBuilder->get_latest_vps(),
                           active_seq_parameter_set_id,
                           pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
                           pD3D12Enc->m_currentEncodeConfig.m_FrameCroppingCodecConfig,
                           pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps.SubregionBlockPixelsSize,
                           pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
                           *codecConfigDesc.pHEVCConfig,
                           pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_HEVCGroupOfPictures,
                           pD3D12Enc->m_BitstreamHeadersBuffer,
                           pD3D12Enc->m_BitstreamHeadersBuffer.begin() + writtenVPSBytesCount,
                           writtenSPSBytesCount);
   }

   size_t writtenPPSBytesCount = 0;
   
   pHEVCBitstreamBuilder->build_pps(pHEVCBitstreamBuilder->get_latest_sps(),
                                    currentPicParams.pHEVCPicData->slice_pic_parameter_set_id,
                                    *codecConfigDesc.pHEVCConfig,
                                    *currentPicParams.pHEVCPicData,
                                    pD3D12Enc->m_BitstreamHeadersBuffer,
                                    pD3D12Enc->m_BitstreamHeadersBuffer.begin() + writtenSPSBytesCount + writtenVPSBytesCount,
                                    writtenPPSBytesCount);

   // Shrink buffer to fit the headers
   if (pD3D12Enc->m_BitstreamHeadersBuffer.size() > (writtenPPSBytesCount + writtenSPSBytesCount + writtenVPSBytesCount)) {
      pD3D12Enc->m_BitstreamHeadersBuffer.resize(writtenPPSBytesCount + writtenSPSBytesCount + writtenVPSBytesCount);
   }

   return pD3D12Enc->m_BitstreamHeadersBuffer.size();
}
