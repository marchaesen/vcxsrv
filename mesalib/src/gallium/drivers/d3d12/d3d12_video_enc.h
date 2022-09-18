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

#ifndef D3D12_VIDEO_ENC_H
#define D3D12_VIDEO_ENC_H

#include "d3d12_video_types.h"
#include "d3d12_video_encoder_references_manager.h"
#include "d3d12_video_dpb_storage_manager.h"
#include "d3d12_video_encoder_bitstream_builder_h264.h"
#include "d3d12_video_encoder_bitstream_builder_hevc.h"

///
/// Pipe video interface starts
///

/**
 * creates a video encoder
 */
struct pipe_video_codec *
d3d12_video_encoder_create_encoder(struct pipe_context *context, const struct pipe_video_codec *templ);

/**
 * destroy this video encoder
 */
void
d3d12_video_encoder_destroy(struct pipe_video_codec *codec);

/**
 * start encoding of a new frame
 */
void
d3d12_video_encoder_begin_frame(struct pipe_video_codec * codec,
                                struct pipe_video_buffer *target,
                                struct pipe_picture_desc *picture);

/**
 * encode to a bitstream
 */
void
d3d12_video_encoder_encode_bitstream(struct pipe_video_codec * codec,
                                     struct pipe_video_buffer *source,
                                     struct pipe_resource *    destination,
                                     void **                   feedback);

/**
 * get encoder feedback
 */
void
d3d12_video_encoder_get_feedback(struct pipe_video_codec *codec, void *feedback, unsigned *size);

/**
 * end encoding of the current frame
 */
void
d3d12_video_encoder_end_frame(struct pipe_video_codec * codec,
                              struct pipe_video_buffer *target,
                              struct pipe_picture_desc *picture);

/**
 * flush any outstanding command buffers to the hardware
 * should be called before a video_buffer is acessed by the gallium frontend again
 */
void
d3d12_video_encoder_flush(struct pipe_video_codec *codec);

///
/// Pipe video interface ends
///

enum d3d12_video_encoder_config_dirty_flags
{
   d3d12_video_encoder_config_dirty_flag_none                   = 0x0,
   d3d12_video_encoder_config_dirty_flag_codec                  = 0x1,
   d3d12_video_encoder_config_dirty_flag_profile                = 0x2,
   d3d12_video_encoder_config_dirty_flag_level                  = 0x4,
   d3d12_video_encoder_config_dirty_flag_codec_config           = 0x8,
   d3d12_video_encoder_config_dirty_flag_input_format           = 0x10,
   d3d12_video_encoder_config_dirty_flag_resolution             = 0x20,
   d3d12_video_encoder_config_dirty_flag_rate_control           = 0x40,
   d3d12_video_encoder_config_dirty_flag_slices                 = 0x80,
   d3d12_video_encoder_config_dirty_flag_gop                    = 0x100,
   d3d12_video_encoder_config_dirty_flag_motion_precision_limit = 0x200,
};
DEFINE_ENUM_FLAG_OPERATORS(d3d12_video_encoder_config_dirty_flags);

///
/// d3d12_video_encoder functions starts
///

struct d3d12_video_encoder
{
   struct pipe_video_codec base;
   struct pipe_screen *    m_screen;
   struct d3d12_screen *   m_pD3D12Screen;

   ///
   /// D3D12 objects and context info
   ///

   const uint m_NodeMask  = 0u;
   const uint m_NodeIndex = 0u;

   ComPtr<ID3D12Fence> m_spFence;
   uint                m_fenceValue = 1u;

   ComPtr<ID3D12VideoDevice3>            m_spD3D12VideoDevice;
   ComPtr<ID3D12VideoEncoder>            m_spVideoEncoder;
   ComPtr<ID3D12VideoEncoderHeap>        m_spVideoEncoderHeap;
   ComPtr<ID3D12CommandQueue>            m_spEncodeCommandQueue;
   ComPtr<ID3D12CommandAllocator>        m_spCommandAllocator;
   ComPtr<ID3D12VideoEncodeCommandList2> m_spEncodeCommandList;
   std::vector<D3D12_RESOURCE_BARRIER>   m_transitionsBeforeCloseCmdList;

   std::unique_ptr<d3d12_video_encoder_references_manager_interface> m_upDPBManager;
   std::unique_ptr<d3d12_video_dpb_storage_manager_interface>        m_upDPBStorageManager;
   std::unique_ptr<d3d12_video_bitstream_builder_interface>          m_upBitstreamBuilder;

   bool m_needsGPUFlush = false;

   ComPtr<ID3D12Resource> m_spResolvedMetadataBuffer;
   ComPtr<ID3D12Resource> m_spMetadataOutputBuffer;

   std::vector<uint8_t> m_BitstreamHeadersBuffer;

   struct
   {
      bool m_fArrayOfTexturesDpb;

      D3D12_VIDEO_ENCODER_SUPPORT_FLAGS                          m_SupportFlags;
      D3D12_VIDEO_ENCODER_VALIDATION_FLAGS                       m_ValidationFlags;
      D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOLUTION_SUPPORT_LIMITS m_currentResolutionSupportCaps;
      union
      {
         D3D12_VIDEO_ENCODER_PROFILE_H264 m_H264Profile;
         D3D12_VIDEO_ENCODER_PROFILE_HEVC m_HEVCProfile;
      } m_encoderSuggestedProfileDesc = {};

      union
      {
         D3D12_VIDEO_ENCODER_LEVELS_H264                 m_H264LevelSetting;
         D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC m_HEVCLevelSetting;
      } m_encoderLevelSuggestedDesc = {};

      union
      {
         D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_H264 m_H264CodecCaps;
         D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_SUPPORT_HEVC m_HEVCCodecCaps;
      } m_encoderCodecSpecificConfigCaps = {};

      // Required size for the layout-resolved metadata buffer of current frame to be encoded
      size_t m_resolvedLayoutMetadataBufferRequiredSize;

      // The maximum number of slices that the output of the current frame to be encoded will contain
      uint32_t m_MaxSlicesInOutput;

      D3D12_FEATURE_DATA_VIDEO_ENCODER_RESOURCE_REQUIREMENTS m_ResourceRequirementsCaps;

   } m_currentEncodeCapabilities;

   struct
   {
      d3d12_video_encoder_config_dirty_flags m_ConfigDirtyFlags = d3d12_video_encoder_config_dirty_flag_none;

      D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC m_currentResolution = {};
      D3D12_BOX m_FrameCroppingCodecConfig = {};

      D3D12_FEATURE_DATA_FORMAT_INFO m_encodeFormatInfo = {};

      D3D12_VIDEO_ENCODER_CODEC m_encoderCodecDesc = {};

      D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAGS m_seqFlags = D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_NONE;

      /// As the following D3D12 Encode types have pointers in their structures, we need to keep a deep copy of them

      union
      {
         D3D12_VIDEO_ENCODER_PROFILE_H264 m_H264Profile;
         D3D12_VIDEO_ENCODER_PROFILE_HEVC m_HEVCProfile;
      } m_encoderProfileDesc = {};

      union
      {
         D3D12_VIDEO_ENCODER_LEVELS_H264                 m_H264LevelSetting;
         D3D12_VIDEO_ENCODER_LEVEL_TIER_CONSTRAINTS_HEVC m_HEVCLevelSetting;
      } m_encoderLevelDesc = {};

      struct
      {
         D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE  m_Mode;
         D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAGS m_Flags;
         DXGI_RATIONAL                          m_FrameRate;
         union
         {
            D3D12_VIDEO_ENCODER_RATE_CONTROL_CQP  m_Configuration_CQP;
            D3D12_VIDEO_ENCODER_RATE_CONTROL_CBR  m_Configuration_CBR;
            D3D12_VIDEO_ENCODER_RATE_CONTROL_VBR  m_Configuration_VBR;
            D3D12_VIDEO_ENCODER_RATE_CONTROL_QVBR m_Configuration_QVBR;
         } m_Config;
      } m_encoderRateControlDesc = {};

      union
      {
         D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_H264 m_H264Config;
         D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC m_HEVCConfig;
      } m_encoderCodecSpecificConfigDesc = {};


      D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE m_encoderSliceConfigMode;
      union
      {
         D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES m_SlicesPartition_H264;
         D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES m_SlicesPartition_HEVC;
      } m_encoderSliceConfigDesc = {};

      union
      {
         D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE_H264 m_H264GroupOfPictures;
         D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE_HEVC m_HEVCGroupOfPictures;
      } m_encoderGOPConfigDesc = {};

      union
      {
         D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264 m_H264PicData;
         D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC m_HEVCPicData;
      } m_encoderPicParamsDesc = {};

      D3D12_VIDEO_ENCODER_MOTION_ESTIMATION_PRECISION_MODE m_encoderMotionPrecisionLimit =
         D3D12_VIDEO_ENCODER_MOTION_ESTIMATION_PRECISION_MODE_MAXIMUM;

      D3D12_VIDEO_ENCODER_INTRA_REFRESH m_IntraRefresh = { D3D12_VIDEO_ENCODER_INTRA_REFRESH_MODE_NONE, 0 };
      uint32_t                          m_IntraRefreshCurrentFrameIndex = 0;

   } m_currentEncodeConfig;
};

bool
d3d12_video_encoder_create_command_objects(struct d3d12_video_encoder *pD3D12Enc);
bool
d3d12_video_encoder_reconfigure_session(struct d3d12_video_encoder *pD3D12Enc,
                                        struct pipe_video_buffer *  srcTexture,
                                        struct pipe_picture_desc *  picture);
bool
d3d12_video_encoder_update_current_encoder_config_state(struct d3d12_video_encoder *pD3D12Enc,
                                                        struct pipe_video_buffer *  srcTexture,
                                                        struct pipe_picture_desc *  picture);
bool
d3d12_video_encoder_reconfigure_encoder_objects(struct d3d12_video_encoder *pD3D12Enc,
                                                struct pipe_video_buffer *  srcTexture,
                                                struct pipe_picture_desc *  picture);
D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA
d3d12_video_encoder_get_current_picture_param_settings(struct d3d12_video_encoder *pD3D12Enc);
D3D12_VIDEO_ENCODER_LEVEL_SETTING
d3d12_video_encoder_get_current_level_desc(struct d3d12_video_encoder *pD3D12Enc);
D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION
d3d12_video_encoder_get_current_codec_config_desc(struct d3d12_video_encoder *pD3D12Enc);
D3D12_VIDEO_ENCODER_PROFILE_DESC
d3d12_video_encoder_get_current_profile_desc(struct d3d12_video_encoder *pD3D12Enc);
D3D12_VIDEO_ENCODER_RATE_CONTROL
d3d12_video_encoder_get_current_rate_control_settings(struct d3d12_video_encoder *pD3D12Enc);
D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA
d3d12_video_encoder_get_current_slice_param_settings(struct d3d12_video_encoder *pD3D12Enc);
D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE
d3d12_video_encoder_get_current_gop_desc(struct d3d12_video_encoder *pD3D12Enc);
uint32_t
d3d12_video_encoder_get_current_max_dpb_capacity(struct d3d12_video_encoder *pD3D12Enc);
void
d3d12_video_encoder_create_reference_picture_manager(struct d3d12_video_encoder *pD3D12Enc);
void
d3d12_video_encoder_update_picparams_tracking(struct d3d12_video_encoder *pD3D12Enc,
                                              struct pipe_video_buffer *  srcTexture,
                                              struct pipe_picture_desc *  picture);
void
d3d12_video_encoder_calculate_metadata_resolved_buffer_size(uint32_t maxSliceNumber, size_t &bufferSize);
uint32_t
d3d12_video_encoder_calculate_max_slices_count_in_output(
   D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE                          slicesMode,
   const D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES *slicesConfig,
   uint32_t                                                                 MaxSubregionsNumberFromCaps,
   D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC                              sequenceTargetResolution,
   uint32_t                                                                 SubregionBlockPixelsSize);
bool
d3d12_video_encoder_prepare_output_buffers(struct d3d12_video_encoder *pD3D12Enc,
                                           struct pipe_video_buffer *  srcTexture,
                                           struct pipe_picture_desc *  picture);
uint32_t
d3d12_video_encoder_build_codec_headers(struct d3d12_video_encoder *pD3D12Enc);
void
d3d12_video_encoder_extract_encode_metadata(
   struct d3d12_video_encoder *                               pD3D12Dec,
   ID3D12Resource *                                           pResolvedMetadataBuffer,
   size_t                                                     resourceMetadataSize,
   D3D12_VIDEO_ENCODER_OUTPUT_METADATA &                      encoderMetadata,
   std::vector<D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA> &pSubregionsMetadata);

D3D12_VIDEO_ENCODER_CODEC
d3d12_video_encoder_get_current_codec(struct d3d12_video_encoder *pD3D12Enc);

bool d3d12_video_encoder_negotiate_requested_features_and_d3d12_driver_caps(struct d3d12_video_encoder *pD3D12Enc, D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT &capEncoderSupportData);
bool d3d12_video_encoder_query_d3d12_driver_caps(struct d3d12_video_encoder *pD3D12Enc, D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT &capEncoderSupportData);
bool d3d12_video_encoder_check_subregion_mode_support(struct d3d12_video_encoder *pD3D12Enc, D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE requestedSlicesMode);

///
/// d3d12_video_encoder functions ends
///

#endif
