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

#ifndef D3D12_VIDEO_TYPES_H
#define D3D12_VIDEO_TYPES_H

#include <stdarg.h>
#include <memory>
#include <vector>
#include <functional>

#include "pipe/p_context.h"
#include "pipe/p_video_codec.h"
#include "d3d12_fence.h"
#include "d3d12_debug.h"

#include <directx/d3d12video.h>
#include <dxguids/dxguids.h>

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

#if !defined(_WIN32) || defined(_MSC_VER) || D3D12_SDK_VERSION < 606
inline D3D12_VIDEO_DECODER_HEAP_DESC
GetDesc(ID3D12VideoDecoderHeap *heap)
{
   return heap->GetDesc();
}
#else
inline D3D12_VIDEO_DECODER_HEAP_DESC
GetDesc(ID3D12VideoDecoderHeap *heap)
{
   D3D12_VIDEO_DECODER_HEAP_DESC ret;
   heap->GetDesc(&ret);
   return ret;
}
#endif

// Allow encoder to continue the encoding session when an optional 
// rate control mode such as the following is used but not supported
//
// D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES
// D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_MAX_FRAME_SIZE
//
// If setting this OS Env variable to true, the encoding process will continue, disregarding the settings
// requested for the optional RC mode
//

const bool D3D12_VIDEO_ENC_FALLBACK_RATE_CONTROL_CONFIG = debug_get_bool_option("D3D12_VIDEO_ENC_FALLBACK_RATE_CONTROL_CONFIG", false);

/* For CBR mode, to guarantee bitrate of generated stream complies with
* target bitrate (e.g. no over +/-10%), vbv_buffer_size should be same
* as target bitrate. Controlled by OS env var D3D12_VIDEO_ENC_CBR_FORCE_VBV_EQUAL_BITRATE
*/
const bool D3D12_VIDEO_ENC_CBR_FORCE_VBV_EQUAL_BITRATE = debug_get_bool_option("D3D12_VIDEO_ENC_CBR_FORCE_VBV_EQUAL_BITRATE", false);

// Allow encoder to continue the encoding session when aa slice mode 
// is requested but not supported.
//
// If setting this OS Env variable to true, the encoder will try to adjust to the closest slice
// setting available and encode using that configuration anyway
//
const bool D3D12_VIDEO_ENC_FALLBACK_SLICE_CONFIG = debug_get_bool_option("D3D12_VIDEO_ENC_FALLBACK_SLICE_CONFIG", false);

constexpr unsigned int D3D12_VIDEO_H264_MB_IN_PIXELS = 16;

enum d3d12_video_decode_config_specific_flags
{
   d3d12_video_decode_config_specific_flag_none              = 0,
   d3d12_video_decode_config_specific_flag_alignment_height  = 1 << 12,   // set by accelerator
   d3d12_video_decode_config_specific_flag_array_of_textures = 1 << 14,   // set by accelerator
   d3d12_video_decode_config_specific_flag_reuse_decoder =
      1 << 15,   // set by accelerator - This bit means that the decoder can be re-used with resolution change and bit
                 // depth change (including profile GUID change from 8bit to 10bit and vice versa).
   d3d12_video_decode_config_specific_flag_reference_only_textures_required = 1 << 30,   // custom created for WSL
};

enum d3d12_video_decode_profile_type
{
   d3d12_video_decode_profile_type_none,
   d3d12_video_decode_profile_type_h264,
   d3d12_video_decode_profile_type_hevc,
   d3d12_video_decode_profile_type_max_valid
};

struct d3d12_video_decode_dpb_descriptor
{
   DXGI_FORMAT Format          = DXGI_FORMAT_UNKNOWN;
   uint64_t    Width           = 0;
   uint32_t    Height          = 0;
   bool        fArrayOfTexture = false;
   bool        fReferenceOnly  = false;
   uint16_t    dpbSize         = 0;
   uint32_t    m_NodeMask      = 0;
};

struct d3d12_video_decode_output_conversion_arguments
{
   BOOL                  Enable;
   DXGI_COLOR_SPACE_TYPE OutputColorSpace;
   D3D12_VIDEO_SAMPLE    ReferenceInfo;
   uint32_t              ReferenceFrameCount;
};

void
d3d12_video_encoder_convert_from_d3d12_level_h264(D3D12_VIDEO_ENCODER_LEVELS_H264 level12,
                                                  uint32_t &                      specLevel,
                                                  uint32_t &                      constraint_set3_flag);
void
d3d12_video_encoder_convert_from_d3d12_level_hevc(D3D12_VIDEO_ENCODER_LEVELS_HEVC level12,
                                                  uint32_t &                      specLevel);
D3D12_VIDEO_ENCODER_PROFILE_H264
d3d12_video_encoder_convert_profile_to_d3d12_enc_profile_h264(enum pipe_video_profile profile);
D3D12_VIDEO_ENCODER_PROFILE_HEVC
d3d12_video_encoder_convert_profile_to_d3d12_enc_profile_hevc(enum pipe_video_profile profile);
D3D12_VIDEO_ENCODER_CODEC
d3d12_video_encoder_convert_codec_to_d3d12_enc_codec(enum pipe_video_profile profile);
GUID
d3d12_video_decoder_convert_pipe_video_profile_to_d3d12_profile(enum pipe_video_profile profile);
D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE 
d3d12_video_encoder_convert_pixel_size_hevc_to_12tusize(const uint32_t& TUSize);
D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE
d3d12_video_encoder_convert_pixel_size_hevc_to_12cusize(const uint32_t& cuSize);
uint8_t
d3d12_video_encoder_convert_12cusize_to_pixel_size_hevc(const D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_CUSIZE& cuSize);
uint8_t 
d3d12_video_encoder_convert_12tusize_to_pixel_size_hevc(const D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION_HEVC_TUSIZE& TUSize);

DEFINE_ENUM_FLAG_OPERATORS(pipe_h265_enc_feature);
DEFINE_ENUM_FLAG_OPERATORS(pipe_h265_enc_pred_direction);

#endif
