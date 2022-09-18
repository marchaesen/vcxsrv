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

#ifndef D3D12_VIDEO_DEC_H
#define D3D12_VIDEO_DEC_H

#include "d3d12_video_types.h"
#include "d3d12_video_dec_references_mgr.h"

///
/// Pipe video interface starts
///

/**
 * creates a video decoder
 */
struct pipe_video_codec *
d3d12_video_create_decoder(struct pipe_context *context, const struct pipe_video_codec *templ);

/**
 * destroy this video decoder
 */
void
d3d12_video_decoder_destroy(struct pipe_video_codec *codec);

/**
 * start decoding of a new frame
 */
void
d3d12_video_decoder_begin_frame(struct pipe_video_codec * codec,
                                struct pipe_video_buffer *target,
                                struct pipe_picture_desc *picture);

/**
 * decode a bitstream
 */
void
d3d12_video_decoder_decode_bitstream(struct pipe_video_codec * codec,
                                     struct pipe_video_buffer *target,
                                     struct pipe_picture_desc *picture,
                                     unsigned                  num_buffers,
                                     const void *const *       buffers,
                                     const unsigned *          sizes);

/**
 * end decoding of the current frame
 */
void
d3d12_video_decoder_end_frame(struct pipe_video_codec * codec,
                              struct pipe_video_buffer *target,
                              struct pipe_picture_desc *picture);

/**
 * flush any outstanding command buffers to the hardware
 * should be called before a video_buffer is acessed by the gallium frontend again
 */
void
d3d12_video_decoder_flush(struct pipe_video_codec *codec);

///
/// Pipe video interface ends
///

///
/// d3d12_video_decoder functions starts
///

struct d3d12_video_decoder_reference_poc_entry {
   uint8_t refpicset_index;
   int32_t poc_value;
};

struct d3d12_video_decoder
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

   ComPtr<ID3D12VideoDevice>             m_spD3D12VideoDevice;
   ComPtr<ID3D12VideoDecoder>            m_spVideoDecoder;
   ComPtr<ID3D12VideoDecoderHeap>        m_spVideoDecoderHeap;
   ComPtr<ID3D12CommandQueue>            m_spDecodeCommandQueue;
   ComPtr<ID3D12CommandAllocator>        m_spCommandAllocator;
   ComPtr<ID3D12VideoDecodeCommandList1> m_spDecodeCommandList;

   std::vector<D3D12_RESOURCE_BARRIER> m_transitionsBeforeCloseCmdList;
   std::vector<D3D12_RESOURCE_BARRIER> m_transitionsStorage;

   D3D12_VIDEO_DECODER_DESC               m_decoderDesc     = {};
   D3D12_VIDEO_DECODER_HEAP_DESC          m_decoderHeapDesc = {};
   D3D12_VIDEO_DECODE_TIER                m_tier            = D3D12_VIDEO_DECODE_TIER_NOT_SUPPORTED;
   DXGI_FORMAT                            m_decodeFormat;
   D3D12_FEATURE_DATA_FORMAT_INFO         m_decodeFormatInfo           = {};
   D3D12_VIDEO_DECODE_CONFIGURATION_FLAGS m_configurationFlags         = D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_NONE;
   GUID                                   m_d3d12DecProfile            = {};
   d3d12_video_decode_profile_type        m_d3d12DecProfileType        = {};
   uint                                   m_ConfigDecoderSpecificFlags = 0u;

   ///
   /// Current frame tracked state
   ///

   // Tracks DPB and reference picture textures
   std::unique_ptr<d3d12_video_decoder_references_manager> m_spDPBManager;

   // Holds pointers to current decode output target texture and reference textures from upper layer
   struct pipe_video_buffer *m_pCurrentDecodeTarget;
   struct pipe_video_buffer **m_pCurrentReferenceTargets;

   // Holds the input bitstream buffer while it's being constructed in decode_bitstream calls
   std::vector<uint8_t> m_stagingDecodeBitstream;

   const uint64_t m_InitialCompBitstreamGPUBufferSize = (1024 /*1K*/ * 1024 /*1MB*/) * 8 /*8 MB*/;   // 8MB

   // Holds the input bitstream buffer in GPU video memory
   ComPtr<ID3D12Resource> m_curFrameCompressedBitstreamBuffer;
   uint64_t               m_curFrameCompressedBitstreamBufferAllocatedSize =
      m_InitialCompBitstreamGPUBufferSize;   // Actual number of allocated bytes available in the buffer (after
                                             // m_curFrameCompressedBitstreamBufferPayloadSize might be garbage)
   uint64_t m_curFrameCompressedBitstreamBufferPayloadSize = 0u;   // Actual number of bytes of valid data

   // Holds a buffer for the DXVA struct layout of the picture params of the current frame
   std::vector<uint8_t> m_picParamsBuffer;   // size() has the byte size of the currently held picparams ; capacity()
                                             // has the underlying container allocation size

   // Set for each frame indicating whether to send VIDEO_DECODE_BUFFER_TYPE_INVERSE_QUANTIZATION_MATRIX
   bool qp_matrix_frame_argument_enabled = false;

   // Holds a buffer for the DXVA struct layout of the VIDEO_DECODE_BUFFER_TYPE_INVERSE_QUANTIZATION_MATRIX of the
   // current frame m_InverseQuantMatrixBuffer.size() == 0 means no quantization matrix buffer is set for current frame
   std::vector<uint8_t> m_InverseQuantMatrixBuffer;   // size() has the byte size of the currently held
                                                      // VIDEO_DECODE_BUFFER_TYPE_INVERSE_QUANTIZATION_MATRIX ;
                                                      // capacity() has the underlying container allocation size

   // Holds a buffer for the DXVA struct layout of the VIDEO_DECODE_BUFFER_TYPE_SLICE_CONTROL of the current frame
   // m_SliceControlBuffer.size() == 0 means no quantization matrix buffer is set for current frame
   std::vector<uint8_t>
      m_SliceControlBuffer;   // size() has the byte size of the currently held VIDEO_DECODE_BUFFER_TYPE_SLICE_CONTROL ;
                              // capacity() has the underlying container allocation size

   // Indicates if GPU commands have not been flushed and are pending.
   bool m_needsGPUFlush = false;

   std::vector<d3d12_video_decoder_reference_poc_entry> m_ReferencesConversionStorage;
};

bool
d3d12_video_decoder_create_command_objects(const struct d3d12_screen * pD3D12Screen,
                                           struct d3d12_video_decoder *pD3D12Dec);
bool
d3d12_video_decoder_check_caps_and_create_decoder(const struct d3d12_screen * pD3D12Screen,
                                                  struct d3d12_video_decoder *pD3D12Dec);
bool
d3d12_video_decoder_create_video_state_buffers(const struct d3d12_screen * pD3D12Screen,
                                               struct d3d12_video_decoder *pD3D12Dec);
bool
d3d12_video_decoder_create_staging_bitstream_buffer(const struct d3d12_screen * pD3D12Screen,
                                                    struct d3d12_video_decoder *pD3D12Dec,
                                                    uint64_t                    bufSize);
void
d3d12_video_decoder_store_upper_layer_references(struct d3d12_video_decoder *pD3D12Dec,
                                                struct pipe_video_buffer *target,
                                                struct pipe_picture_desc *picture);
bool
d3d12_video_decoder_prepare_for_decode_frame(struct d3d12_video_decoder *pD3D12Dec,
                                             struct pipe_video_buffer *  pCurrentDecodeTarget,
                                             struct d3d12_video_buffer * pD3D12VideoBuffer,
                                             ID3D12Resource **           ppOutTexture2D,
                                             uint32_t *                  pOutSubresourceIndex,
                                             ID3D12Resource **           ppRefOnlyOutTexture2D,
                                             uint32_t *                  pRefOnlyOutSubresourceIndex,
                                             const d3d12_video_decode_output_conversion_arguments &conversionArgs);
void
d3d12_video_decoder_refresh_dpb_active_references(struct d3d12_video_decoder *pD3D12Dec);
bool
d3d12_video_decoder_reconfigure_dpb(struct d3d12_video_decoder *                          pD3D12Dec,
                                    struct d3d12_video_buffer *                           pD3D12VideoBuffer,
                                    const d3d12_video_decode_output_conversion_arguments &conversionArguments);
void
d3d12_video_decoder_get_frame_info(
   struct d3d12_video_decoder *pD3D12Dec, uint32_t *pWidth, uint32_t *pHeight, uint16_t *pMaxDPB, bool &isInterlaced);
void
d3d12_video_decoder_store_converted_dxva_picparams_from_pipe_input(struct d3d12_video_decoder *codec,
                                                                   struct pipe_picture_desc *  picture,
                                                                   struct d3d12_video_buffer * pD3D12VideoBuffer);
template <typename T>
T *
d3d12_video_decoder_get_current_dxva_picparams(struct d3d12_video_decoder *codec)
{
   return reinterpret_cast<T *>(codec->m_picParamsBuffer.data());
}
bool
d3d12_video_decoder_supports_aot_dpb(D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT decodeSupport,
                                     d3d12_video_decode_profile_type         profileType);
d3d12_video_decode_profile_type
d3d12_video_decoder_convert_pipe_video_profile_to_profile_type(enum pipe_video_profile profile);
GUID
d3d12_video_decoder_resolve_profile(d3d12_video_decode_profile_type profileType, DXGI_FORMAT decode_format);
void
d3d12_video_decoder_store_dxva_picparams_in_picparams_buffer(struct d3d12_video_decoder *codec,
                                                             void *                      pDXVABuffer,
                                                             uint64_t                    DXVABufferSize);
void
d3d12_video_decoder_store_dxva_qmatrix_in_qmatrix_buffer(struct d3d12_video_decoder *pD3D12Dec,
                                                         void *                      pDXVAStruct,
                                                         uint64_t                    DXVAStructSize);
void
d3d12_video_decoder_prepare_dxva_slices_control(struct d3d12_video_decoder *pD3D12Dec, struct pipe_picture_desc *picture);
int
d3d12_video_decoder_get_next_startcode_offset(std::vector<uint8_t> &buf,
                                              unsigned int          bufferOffset,
                                              unsigned int          targetCode,
                                              unsigned int          targetCodeBitSize,
                                              unsigned int          numBitsToSearchIntoBuffer);

///
/// d3d12_video_decoder functions ends
///

#endif
