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

#include "d3d12_context.h"
#include "d3d12_format.h"
#include "d3d12_resource.h"
#include "d3d12_screen.h"
#include "d3d12_surface.h"
#include "d3d12_video_dec.h"
#include "d3d12_video_dec_h264.h"
#include "d3d12_video_dec_hevc.h"
#include "d3d12_video_buffer.h"
#include "d3d12_residency.h"

#include "vl/vl_video_buffer.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_video.h"
#include "util/vl_vlc.h"

struct pipe_video_codec *
d3d12_video_create_decoder(struct pipe_context *context, const struct pipe_video_codec *codec)
{
   ///
   /// Initialize d3d12_video_decoder
   ///


   // Not using new doesn't call ctor and the initializations in the class declaration are lost
   struct d3d12_video_decoder *pD3D12Dec = new d3d12_video_decoder;

   pD3D12Dec->base = *codec;
   pD3D12Dec->m_screen = context->screen;

   pD3D12Dec->base.context = context;
   pD3D12Dec->base.width = codec->width;
   pD3D12Dec->base.height = codec->height;
   // Only fill methods that are supported by the d3d12 decoder, leaving null the rest (ie. encode_* / decode_macroblock
   // / get_feedback for encode)
   pD3D12Dec->base.destroy = d3d12_video_decoder_destroy;
   pD3D12Dec->base.begin_frame = d3d12_video_decoder_begin_frame;
   pD3D12Dec->base.decode_bitstream = d3d12_video_decoder_decode_bitstream;
   pD3D12Dec->base.end_frame = d3d12_video_decoder_end_frame;
   pD3D12Dec->base.flush = d3d12_video_decoder_flush;

   pD3D12Dec->m_decodeFormat = d3d12_convert_pipe_video_profile_to_dxgi_format(codec->profile);
   pD3D12Dec->m_d3d12DecProfileType = d3d12_video_decoder_convert_pipe_video_profile_to_profile_type(codec->profile);
   pD3D12Dec->m_d3d12DecProfile = d3d12_video_decoder_convert_pipe_video_profile_to_d3d12_profile(codec->profile);

   ///
   /// Try initializing D3D12 Video device and check for device caps
   ///

   struct d3d12_context *pD3D12Ctx = (struct d3d12_context *) context;
   pD3D12Dec->m_pD3D12Screen = d3d12_screen(pD3D12Ctx->base.screen);

   ///
   /// Create decode objects
   ///
   HRESULT hr = S_OK;
   if (FAILED(pD3D12Dec->m_pD3D12Screen->dev->QueryInterface(
          IID_PPV_ARGS(pD3D12Dec->m_spD3D12VideoDevice.GetAddressOf())))) {
      debug_printf("[d3d12_video_decoder] d3d12_video_create_decoder - D3D12 Device has no Video support\n");
      goto failed;
   }

   if (!d3d12_video_decoder_check_caps_and_create_decoder(pD3D12Dec->m_pD3D12Screen, pD3D12Dec)) {
      debug_printf("[d3d12_video_decoder] d3d12_video_create_decoder - Failure on "
                      "d3d12_video_decoder_check_caps_and_create_decoder\n");
      goto failed;
   }

   if (!d3d12_video_decoder_create_command_objects(pD3D12Dec->m_pD3D12Screen, pD3D12Dec)) {
      debug_printf(
         "[d3d12_video_decoder] d3d12_video_create_decoder - Failure on d3d12_video_decoder_create_command_objects\n");
      goto failed;
   }

   if (!d3d12_video_decoder_create_video_state_buffers(pD3D12Dec->m_pD3D12Screen, pD3D12Dec)) {
      debug_printf("[d3d12_video_decoder] d3d12_video_create_decoder - Failure on "
                      "d3d12_video_decoder_create_video_state_buffers\n");
      goto failed;
   }

   pD3D12Dec->m_decodeFormatInfo = { pD3D12Dec->m_decodeFormat };
   hr = pD3D12Dec->m_pD3D12Screen->dev->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO,
                                                                        &pD3D12Dec->m_decodeFormatInfo,
                                                                        sizeof(pD3D12Dec->m_decodeFormatInfo));
   if(FAILED(hr)) {
      debug_printf("CheckFeatureSupport failed with HR %x\n", hr);
      goto failed;
   }

   return &pD3D12Dec->base;

failed:
   if (pD3D12Dec != nullptr) {
      d3d12_video_decoder_destroy((struct pipe_video_codec *) pD3D12Dec);
   }

   return nullptr;
}

/**
 * Destroys a d3d12_video_decoder
 * Call destroy_XX for applicable XX nested member types before deallocating
 * Destroy methods should check != nullptr on their input target argument as this method can be called as part of
 * cleanup from failure on the creation method
 */
void
d3d12_video_decoder_destroy(struct pipe_video_codec *codec)
{
   if (codec == nullptr) {
      return;
   }

   d3d12_video_decoder_flush(codec);   // Flush pending work before destroying.

   struct d3d12_video_decoder *pD3D12Dec = (struct d3d12_video_decoder *) codec;

   //
   // Destroys a decoder
   // Call destroy_XX for applicable XX nested member types before deallocating
   // Destroy methods should check != nullptr on their input target argument as this method can be called as part of
   // cleanup from failure on the creation method
   //

   // No need for d3d12_destroy_video_objects
   //    All the objects created here are smart pointer members of d3d12_video_decoder
   // No need for d3d12_destroy_video_decoder_and_heap
   //    All the objects created here are smart pointer members of d3d12_video_decoder
   // No need for d3d12_destroy_video_dpbmanagers
   //    All the objects created here are smart pointer members of d3d12_video_decoder

   // No need for m_pD3D12Screen as it is not managed by d3d12_video_decoder

   // Call dtor to make ComPtr work
   delete pD3D12Dec;
}

/**
 * start decoding of a new frame
 */
void
d3d12_video_decoder_begin_frame(struct pipe_video_codec *codec,
                                struct pipe_video_buffer *target,
                                struct pipe_picture_desc *picture)
{
   // Do nothing here. Initialize happens on decoder creation, re-config (if any) happens in
   // d3d12_video_decoder_decode_bitstream
   struct d3d12_video_decoder *pD3D12Dec = (struct d3d12_video_decoder *) codec;
   assert(pD3D12Dec);
   debug_printf("[d3d12_video_decoder] d3d12_video_decoder_begin_frame finalized for fenceValue: %d\n",
                 pD3D12Dec->m_fenceValue);
}

/**
 * decode a bitstream
 */
void
d3d12_video_decoder_decode_bitstream(struct pipe_video_codec *codec,
                                     struct pipe_video_buffer *target,
                                     struct pipe_picture_desc *picture,
                                     unsigned num_buffers,
                                     const void *const *buffers,
                                     const unsigned *sizes)
{
   struct d3d12_video_decoder *pD3D12Dec = (struct d3d12_video_decoder *) codec;
   assert(pD3D12Dec);
   debug_printf("[d3d12_video_decoder] d3d12_video_decoder_decode_bitstream started for fenceValue: %d\n",
                 pD3D12Dec->m_fenceValue);
   assert(pD3D12Dec->m_spD3D12VideoDevice);
   assert(pD3D12Dec->m_spDecodeCommandQueue);
   assert(pD3D12Dec->m_pD3D12Screen);
   struct d3d12_video_buffer *pD3D12VideoBuffer = (struct d3d12_video_buffer *) target;
   assert(pD3D12VideoBuffer);

   ///
   /// Compressed bitstream buffers
   ///

   /// Mesa VA frontend Video buffer passing semantics for H264, HEVC, MPEG4, VC1 and PIPE_VIDEO_PROFILE_VC1_ADVANCED
   /// are: If num_buffers == 1 -> buf[0] has the compressed bitstream WITH the starting code If num_buffers == 2 ->
   /// buf[0] has the NALU starting code and buf[1] has the compressed bitstream WITHOUT any starting code. If
   /// num_buffers = 3 -> It's JPEG, not supported in D3D12. num_buffers is at most 3.
   /// Mesa VDPAU frontend passes the buffers as they get passed in VdpDecoderRender without fixing any start codes
   /// except for PIPE_VIDEO_PROFILE_VC1_ADVANCED
   // In https://http.download.nvidia.com/XFree86/vdpau/doxygen/html/index.html#video_mixer_usage it's mentioned that:
   // It is recommended that applications pass solely the slice data to VDPAU; specifically that any header data
   // structures be excluded from the portion of the bitstream passed to VDPAU. VDPAU implementations must operate
   // correctly if non-slice data is included, at least for formats employing start codes to delimit slice data. For all
   // codecs/profiles it's highly recommended (when the codec/profile has such codes...) that the start codes are passed
   // to VDPAU, even when not included in the bitstream the VDPAU client is parsing. Let's assume we get all the start
   // codes for VDPAU. The doc also says "VDPAU implementations must operate correctly if non-slice data is included, at
   // least for formats employing start codes to delimit slice data" if we ever get an issue with VDPAU start codes we
   // should consider adding the code that handles this in the VDPAU layer above the gallium driver like mesa VA does.

   // To handle the multi-slice case end_frame already takes care of this by parsing the start codes from the
   // combined bitstream of all decode_bitstream calls.

   // VAAPI seems to send one decode_bitstream command per slice, but we should also support the VDPAU case where the
   // buffers have multiple buffer array entry per slice {startCode (optional), slice1, slice2, ..., startCode
   // (optional) , sliceN}

   if (num_buffers > 2)   // Assume this means multiple slices at once in a decode_bitstream call
   {
      // Based on VA frontend codebase, this never happens for video (no JPEG)
      // Based on VDPAU frontends codebase, this only happens when sending more than one slice at once in decode bitstream

      // To handle the case where VDPAU send all the slices at once in a single decode_bitstream call, let's pretend it
      // was a series of different calls

      // group by start codes and buffers and perform calls for the number of slices
      debug_printf("[d3d12_video_decoder] d3d12_video_decoder_decode_bitstream multiple slices on same call detected "
                     "for fenceValue: %d, breaking down the calls into one per slice\n",
                     pD3D12Dec->m_fenceValue);

      size_t curBufferIdx = 0;

      // Vars to be used for the delegation calls to decode_bitstream
      unsigned call_num_buffers = 0;
      const void *const *call_buffers = nullptr;
      const unsigned *call_sizes = nullptr;

      while (curBufferIdx < num_buffers) {
         // Store the current buffer as the base array pointer for the delegated call, later decide if it'll be a
         // startcode+slicedata or just slicedata call
         call_buffers = &buffers[curBufferIdx];
         call_sizes = &sizes[curBufferIdx];

         // Usually start codes are less or equal than 4 bytes
         // If the current buffer is a start code buffer, send it along with the next buffer. Otherwise, just send the
         // current buffer.
         call_num_buffers = (sizes[curBufferIdx] <= 4) ? 2 : 1;

         // Delegate call with one or two buffers only
         d3d12_video_decoder_decode_bitstream(codec, target, picture, call_num_buffers, call_buffers, call_sizes);

         curBufferIdx += call_num_buffers;   // Consume from the loop the buffers sent in the last call
      }
   } else {
      ///
      /// Handle single slice buffer path, maybe with an extra start code buffer at buffers[0].
      ///

      // Both the start codes being present at buffers[0] and the rest in buffers [1] or full buffer at [0] cases can be
      // handled by flattening all the buffers into a single one and passing that to HW.

      size_t totalReceivedBuffersSize = 0u;   // Combined size of all sizes[]
      for (size_t bufferIdx = 0; bufferIdx < num_buffers; bufferIdx++) {
         totalReceivedBuffersSize += sizes[bufferIdx];
      }

      // Bytes of data pre-staged before this decode_frame call
      size_t preStagedDataSize = pD3D12Dec->m_stagingDecodeBitstream.size();

      // Extend the staging buffer size, as decode_frame can be called several times before end_frame
      pD3D12Dec->m_stagingDecodeBitstream.resize(preStagedDataSize + totalReceivedBuffersSize);

      // Point newSliceDataPositionDstBase to the end of the pre-staged data in m_stagingDecodeBitstream, where the new
      // buffers will be appended
      uint8_t *newSliceDataPositionDstBase = pD3D12Dec->m_stagingDecodeBitstream.data() + preStagedDataSize;

      // Append new data at the end.
      size_t dstOffset = 0u;
      for (size_t bufferIdx = 0; bufferIdx < num_buffers; bufferIdx++) {
         memcpy(newSliceDataPositionDstBase + dstOffset, buffers[bufferIdx], sizes[bufferIdx]);
         dstOffset += sizes[bufferIdx];
      }

      debug_printf("[d3d12_video_decoder] d3d12_video_decoder_decode_bitstream finalized for fenceValue: %d\n",
                    pD3D12Dec->m_fenceValue);
   }
}

void
d3d12_video_decoder_store_upper_layer_references(struct d3d12_video_decoder *pD3D12Dec,
                                                 struct pipe_video_buffer *target,
                                                 struct pipe_picture_desc *picture)
{
   pD3D12Dec->m_pCurrentDecodeTarget = target;
   switch (pD3D12Dec->m_d3d12DecProfileType) {
      case d3d12_video_decode_profile_type_h264:
      {
         pipe_h264_picture_desc *pPicControlH264 = (pipe_h264_picture_desc *) picture;
         pD3D12Dec->m_pCurrentReferenceTargets = pPicControlH264->ref;
      } break;

      case d3d12_video_decode_profile_type_hevc:
      {
         pipe_h265_picture_desc *pPicControlHevc = (pipe_h265_picture_desc *) picture;
         pD3D12Dec->m_pCurrentReferenceTargets = pPicControlHevc->ref;
      } break;

      default:
      {
         unreachable("Unsupported d3d12_video_decode_profile_type");
      } break;
   }
}

/**
 * end decoding of the current frame
 */
void
d3d12_video_decoder_end_frame(struct pipe_video_codec *codec,
                              struct pipe_video_buffer *target,
                              struct pipe_picture_desc *picture)
{
   struct d3d12_video_decoder *pD3D12Dec = (struct d3d12_video_decoder *) codec;
   assert(pD3D12Dec);
   struct d3d12_screen *pD3D12Screen = (struct d3d12_screen *) pD3D12Dec->m_pD3D12Screen;
   assert(pD3D12Screen);
   debug_printf("[d3d12_video_decoder] d3d12_video_decoder_end_frame started for fenceValue: %d\n",
                 pD3D12Dec->m_fenceValue);
   assert(pD3D12Dec->m_spD3D12VideoDevice);
   assert(pD3D12Dec->m_spDecodeCommandQueue);
   struct d3d12_video_buffer *pD3D12VideoBuffer = (struct d3d12_video_buffer *) target;
   assert(pD3D12VideoBuffer);

   ///
   /// Store current decode output target texture and reference textures from upper layer
   ///
   d3d12_video_decoder_store_upper_layer_references(pD3D12Dec, target, picture);

   ///
   /// Codec header picture parameters buffers
   ///

   d3d12_video_decoder_store_converted_dxva_picparams_from_pipe_input(pD3D12Dec, picture, pD3D12VideoBuffer);
   assert(pD3D12Dec->m_picParamsBuffer.size() > 0);

   ///
   /// Prepare Slice control buffers before clearing staging buffer
   ///
   assert(pD3D12Dec->m_stagingDecodeBitstream.size() > 0);   // Make sure the staging wasn't cleared yet in end_frame
   d3d12_video_decoder_prepare_dxva_slices_control(pD3D12Dec, picture);
   assert(pD3D12Dec->m_SliceControlBuffer.size() > 0);

   ///
   /// Upload m_stagingDecodeBitstream to GPU memory now that end_frame is called and clear staging buffer
   ///

   uint64_t sliceDataStagingBufferSize = pD3D12Dec->m_stagingDecodeBitstream.size();
   uint8_t *sliceDataStagingBufferPtr = pD3D12Dec->m_stagingDecodeBitstream.data();

   // Reallocate if necessary to accomodate the current frame bitstream buffer in GPU memory
   if (pD3D12Dec->m_curFrameCompressedBitstreamBufferAllocatedSize < sliceDataStagingBufferSize) {
      if (!d3d12_video_decoder_create_staging_bitstream_buffer(pD3D12Screen, pD3D12Dec, sliceDataStagingBufferSize)) {
         debug_printf("[d3d12_video_decoder] d3d12_video_decoder_end_frame - Failure on "
                         "d3d12_video_decoder_create_staging_bitstream_buffer\n");
         debug_printf("[d3d12_video_encoder] d3d12_video_decoder_end_frame failed for fenceValue: %d\n",
                pD3D12Dec->m_fenceValue);
         assert(false);
         return;
      }
   }

   // Upload frame bitstream CPU data to ID3D12Resource buffer
   pD3D12Dec->m_curFrameCompressedBitstreamBufferPayloadSize =
      sliceDataStagingBufferSize;   // This can be less than m_curFrameCompressedBitstreamBufferAllocatedSize.
   assert(pD3D12Dec->m_curFrameCompressedBitstreamBufferPayloadSize <=
          pD3D12Dec->m_curFrameCompressedBitstreamBufferAllocatedSize);

   /* One-shot transfer operation with data supplied in a user
    * pointer.
    */
   pipe_resource *pPipeCompressedBufferObj =
      d3d12_resource_from_resource(&pD3D12Screen->base, pD3D12Dec->m_curFrameCompressedBitstreamBuffer.Get());
   assert(pPipeCompressedBufferObj);
   pD3D12Dec->base.context->buffer_subdata(pD3D12Dec->base.context,    // context
                                           pPipeCompressedBufferObj,   // dst buffer
                                           PIPE_MAP_WRITE,             // usage PIPE_MAP_x
                                           0,                          // offset
                                           sizeof(*sliceDataStagingBufferPtr) * sliceDataStagingBufferSize,   // size
                                           sliceDataStagingBufferPtr                                          // data
   );

   // Flush buffer_subdata batch and wait on this CPU thread for GPU work completion
   // before deleting the source CPU buffer below
   struct pipe_fence_handle *pUploadGPUCompletionFence = NULL;
   pD3D12Dec->base.context->flush(pD3D12Dec->base.context,
                                  &pUploadGPUCompletionFence,
                                  PIPE_FLUSH_ASYNC | PIPE_FLUSH_HINT_FINISH);
   assert(pUploadGPUCompletionFence);
   debug_printf("[d3d12_video_decoder] d3d12_video_decoder_end_frame - Waiting on GPU completion fence for "
                  "buffer_subdata to upload compressed bitstream.\n");
   pD3D12Screen->base.fence_finish(&pD3D12Screen->base, NULL, pUploadGPUCompletionFence, PIPE_TIMEOUT_INFINITE);
   pD3D12Screen->base.fence_reference(&pD3D12Screen->base, &pUploadGPUCompletionFence, NULL);
   pipe_resource_reference(&pPipeCompressedBufferObj, NULL);

   // [After buffer_subdata GPU work is finished] Clear CPU staging buffer now that end_frame is called and was uploaded
   // to GPU for DecodeFrame call.
   pD3D12Dec->m_stagingDecodeBitstream.resize(0);

   ///
   /// Proceed to record the GPU Decode commands
   ///

   // Requested conversions by caller upper layer (none for now)
   d3d12_video_decode_output_conversion_arguments requestedConversionArguments = {};

   ///
   /// Record DecodeFrame operation and resource state transitions.
   ///

   // Translate input D3D12 structure
   D3D12_VIDEO_DECODE_INPUT_STREAM_ARGUMENTS d3d12InputArguments = {};

   d3d12InputArguments.CompressedBitstream.pBuffer = pD3D12Dec->m_curFrameCompressedBitstreamBuffer.Get();
   d3d12InputArguments.CompressedBitstream.Offset = 0u;
   constexpr uint64_t d3d12BitstreamOffsetAlignment =
      128u;   // specified in
              // https://docs.microsoft.com/en-us/windows/win32/api/d3d12video/ne-d3d12video-d3d12_video_decode_tier
   assert((d3d12InputArguments.CompressedBitstream.Offset == 0) ||
         ((d3d12InputArguments.CompressedBitstream.Offset % d3d12BitstreamOffsetAlignment) == 0));
   d3d12InputArguments.CompressedBitstream.Size = pD3D12Dec->m_curFrameCompressedBitstreamBufferPayloadSize;

   D3D12_RESOURCE_BARRIER resourceBarrierCommonToDecode[1] = {
      CD3DX12_RESOURCE_BARRIER::Transition(d3d12InputArguments.CompressedBitstream.pBuffer,
                                           D3D12_RESOURCE_STATE_COMMON,
                                           D3D12_RESOURCE_STATE_VIDEO_DECODE_READ),
   };
   pD3D12Dec->m_spDecodeCommandList->ResourceBarrier(1u, resourceBarrierCommonToDecode);

   // Schedule reverse (back to common) transitions before command list closes for current frame
   pD3D12Dec->m_transitionsBeforeCloseCmdList.push_back(
      CD3DX12_RESOURCE_BARRIER::Transition(d3d12InputArguments.CompressedBitstream.pBuffer,
                                           D3D12_RESOURCE_STATE_VIDEO_DECODE_READ,
                                           D3D12_RESOURCE_STATE_COMMON));

   ///
   /// Clear texture (no reference only flags in resource allocation) to use as decode output to send downstream for
   /// display/consumption
   ///
   ID3D12Resource *pOutputD3D12Texture;
   uint outputD3D12Subresource = 0;

   ///
   /// Ref Only texture (with reference only flags in resource allocation) to use as reconstructed picture decode output
   /// and to store as future reference in DPB
   ///
   ID3D12Resource *pRefOnlyOutputD3D12Texture;
   uint refOnlyOutputD3D12Subresource = 0;

   if(!d3d12_video_decoder_prepare_for_decode_frame(pD3D12Dec,
                                                target,
                                                pD3D12VideoBuffer,
                                                &pOutputD3D12Texture,             // output
                                                &outputD3D12Subresource,          // output
                                                &pRefOnlyOutputD3D12Texture,      // output
                                                &refOnlyOutputD3D12Subresource,   // output
                                                requestedConversionArguments)) {
      debug_printf("[d3d12_video_decoder] d3d12_video_decoder_end_frame - Failure on "
                      "d3d12_video_decoder_prepare_for_decode_frame\n");
      debug_printf("[d3d12_video_encoder] d3d12_video_decoder_end_frame failed for fenceValue: %d\n",
                pD3D12Dec->m_fenceValue);
      assert(false);
      return;
   }

   ///
   /// Set codec picture parameters CPU buffer
   ///

   d3d12InputArguments.NumFrameArguments =
      1u;   // Only the codec data received from the above layer with picture params
   d3d12InputArguments.FrameArguments[d3d12InputArguments.NumFrameArguments - 1] = {
      D3D12_VIDEO_DECODE_ARGUMENT_TYPE_PICTURE_PARAMETERS,
      static_cast<uint32_t>(pD3D12Dec->m_picParamsBuffer.size()),
      pD3D12Dec->m_picParamsBuffer.data(),
   };

   if (pD3D12Dec->m_SliceControlBuffer.size() > 0) {
      d3d12InputArguments.NumFrameArguments++;
      d3d12InputArguments.FrameArguments[d3d12InputArguments.NumFrameArguments - 1] = {
         D3D12_VIDEO_DECODE_ARGUMENT_TYPE_SLICE_CONTROL,
         static_cast<uint32_t>(pD3D12Dec->m_SliceControlBuffer.size()),
         pD3D12Dec->m_SliceControlBuffer.data(),
      };
   }

   if (pD3D12Dec->qp_matrix_frame_argument_enabled && (pD3D12Dec->m_InverseQuantMatrixBuffer.size() > 0)) {
      d3d12InputArguments.NumFrameArguments++;
      d3d12InputArguments.FrameArguments[d3d12InputArguments.NumFrameArguments - 1] = {
         D3D12_VIDEO_DECODE_ARGUMENT_TYPE_INVERSE_QUANTIZATION_MATRIX,
         static_cast<uint32_t>(pD3D12Dec->m_InverseQuantMatrixBuffer.size()),
         pD3D12Dec->m_InverseQuantMatrixBuffer.data(),
      };
   }

   d3d12InputArguments.ReferenceFrames = pD3D12Dec->m_spDPBManager->get_current_reference_frames();
   if (D3D12_DEBUG_VERBOSE & d3d12_debug) {
      pD3D12Dec->m_spDPBManager->print_dpb();
   }

   d3d12InputArguments.pHeap = pD3D12Dec->m_spVideoDecoderHeap.Get();

   // translate output D3D12 structure
   D3D12_VIDEO_DECODE_OUTPUT_STREAM_ARGUMENTS1 d3d12OutputArguments = {};
   d3d12OutputArguments.pOutputTexture2D = pOutputD3D12Texture;
   d3d12OutputArguments.OutputSubresource = outputD3D12Subresource;

   bool fReferenceOnly = (pD3D12Dec->m_ConfigDecoderSpecificFlags &
                          d3d12_video_decode_config_specific_flag_reference_only_textures_required) != 0;
   if (fReferenceOnly) {
      d3d12OutputArguments.ConversionArguments.Enable = TRUE;

      assert(pRefOnlyOutputD3D12Texture);
      d3d12OutputArguments.ConversionArguments.pReferenceTexture2D = pRefOnlyOutputD3D12Texture;
      d3d12OutputArguments.ConversionArguments.ReferenceSubresource = refOnlyOutputD3D12Subresource;

      const D3D12_RESOURCE_DESC &descReference = GetDesc(d3d12OutputArguments.ConversionArguments.pReferenceTexture2D);
      d3d12OutputArguments.ConversionArguments.DecodeColorSpace = d3d12_convert_from_legacy_color_space(
         !util_format_is_yuv(d3d12_get_pipe_format(descReference.Format)),
         util_format_get_blocksize(d3d12_get_pipe_format(descReference.Format)) * 8 /*bytes to bits conversion*/,
         /* StudioRGB= */ false,
         /* P709= */ true,
         /* StudioYUV= */ true);

      const D3D12_RESOURCE_DESC &descOutput = GetDesc(d3d12OutputArguments.pOutputTexture2D);
      d3d12OutputArguments.ConversionArguments.OutputColorSpace = d3d12_convert_from_legacy_color_space(
         !util_format_is_yuv(d3d12_get_pipe_format(descOutput.Format)),
         util_format_get_blocksize(d3d12_get_pipe_format(descOutput.Format)) * 8 /*bytes to bits conversion*/,
         /* StudioRGB= */ false,
         /* P709= */ true,
         /* StudioYUV= */ true);

      const D3D12_VIDEO_DECODER_HEAP_DESC &HeapDesc = GetDesc(pD3D12Dec->m_spVideoDecoderHeap.Get());
      d3d12OutputArguments.ConversionArguments.OutputWidth = HeapDesc.DecodeWidth;
      d3d12OutputArguments.ConversionArguments.OutputHeight = HeapDesc.DecodeHeight;
   } else {
      d3d12OutputArguments.ConversionArguments.Enable = FALSE;
   }

   CD3DX12_RESOURCE_DESC outputDesc(GetDesc(d3d12OutputArguments.pOutputTexture2D));
   uint32_t MipLevel, PlaneSlice, ArraySlice;
   D3D12DecomposeSubresource(d3d12OutputArguments.OutputSubresource,
                             outputDesc.MipLevels,
                             outputDesc.ArraySize(),
                             MipLevel,
                             ArraySlice,
                             PlaneSlice);

   for (PlaneSlice = 0; PlaneSlice < pD3D12Dec->m_decodeFormatInfo.PlaneCount; PlaneSlice++) {
      uint planeOutputSubresource = outputDesc.CalcSubresource(MipLevel, ArraySlice, PlaneSlice);

      D3D12_RESOURCE_BARRIER resourceBarrierCommonToDecode[1] = {
         CD3DX12_RESOURCE_BARRIER::Transition(d3d12OutputArguments.pOutputTexture2D,
                                              D3D12_RESOURCE_STATE_COMMON,
                                              D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
                                              planeOutputSubresource),
      };
      pD3D12Dec->m_spDecodeCommandList->ResourceBarrier(1u, resourceBarrierCommonToDecode);
   }

   // Schedule reverse (back to common) transitions before command list closes for current frame
   for (PlaneSlice = 0; PlaneSlice < pD3D12Dec->m_decodeFormatInfo.PlaneCount; PlaneSlice++) {
      uint planeOutputSubresource = outputDesc.CalcSubresource(MipLevel, ArraySlice, PlaneSlice);
      pD3D12Dec->m_transitionsBeforeCloseCmdList.push_back(
         CD3DX12_RESOURCE_BARRIER::Transition(d3d12OutputArguments.pOutputTexture2D,
                                              D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
                                              D3D12_RESOURCE_STATE_COMMON,
                                              planeOutputSubresource));
   }

   // Record DecodeFrame

   pD3D12Dec->m_spDecodeCommandList->DecodeFrame1(pD3D12Dec->m_spVideoDecoder.Get(),
                                                  &d3d12OutputArguments,
                                                  &d3d12InputArguments);

   debug_printf("[d3d12_video_decoder] d3d12_video_decoder_end_frame finalized for fenceValue: %d\n",
                 pD3D12Dec->m_fenceValue);

   ///
   /// Flush work to the GPU and blocking wait until decode finishes
   ///
   pD3D12Dec->m_needsGPUFlush = true;
   d3d12_video_decoder_flush(codec);

   if (!pD3D12Dec->m_spDPBManager->is_pipe_buffer_underlying_output_decode_allocation()) {
      ///
      /// If !pD3D12Dec->m_spDPBManager->is_pipe_buffer_underlying_output_decode_allocation()
      /// We cannot use the standalone video buffer allocation directly and we must use instead
      /// either a ID3D12Resource with DECODE_REFERENCE only flag or a texture array within the same
      /// allocation
      /// Do GPU->GPU texture copy from decode output to pipe target decode texture sampler view planes
      ///

      // Get destination resource
      struct pipe_sampler_view **pPipeDstViews = target->get_sampler_view_planes(target);

      // Get source pipe_resource
      pipe_resource *pPipeSrc =
         d3d12_resource_from_resource(&pD3D12Screen->base, d3d12OutputArguments.pOutputTexture2D);
      assert(pPipeSrc);

      // Copy all format subresources/texture planes

      for (PlaneSlice = 0; PlaneSlice < pD3D12Dec->m_decodeFormatInfo.PlaneCount; PlaneSlice++) {
         assert(d3d12OutputArguments.OutputSubresource < INT16_MAX);
         struct pipe_box box = { 0,
                                 0,
                                 // src array slice, taken as Z for TEXTURE_2D_ARRAY
                                 static_cast<int16_t>(d3d12OutputArguments.OutputSubresource),
                                 static_cast<int>(pPipeDstViews[PlaneSlice]->texture->width0),
                                 static_cast<int16_t>(pPipeDstViews[PlaneSlice]->texture->height0),
                                 1 };

         pD3D12Dec->base.context->resource_copy_region(pD3D12Dec->base.context,
                                                       pPipeDstViews[PlaneSlice]->texture,              // dst
                                                       0,                                               // dst level
                                                       0,                                               // dstX
                                                       0,                                               // dstY
                                                       0,                                               // dstZ
                                                       (PlaneSlice == 0) ? pPipeSrc : pPipeSrc->next,   // src
                                                       0,                                               // src level
                                                       &box);
      }
      // Flush resource_copy_region batch and wait on this CPU thread for GPU work completion
      struct pipe_fence_handle *completion_fence = NULL;
      pD3D12Dec->base.context->flush(pD3D12Dec->base.context,
                                     &completion_fence,
                                     PIPE_FLUSH_ASYNC | PIPE_FLUSH_HINT_FINISH);
      assert(completion_fence);
      debug_printf("[d3d12_video_decoder] d3d12_video_decoder_end_frame - Waiting on GPU completion fence for "
                     "resource_copy_region on decoded frame.\n");
      pD3D12Screen->base.fence_finish(&pD3D12Screen->base, NULL, completion_fence, PIPE_TIMEOUT_INFINITE);
      pD3D12Screen->base.fence_reference(&pD3D12Screen->base, &completion_fence, NULL);
      pipe_resource_reference(&pPipeSrc, NULL);
   }
}

/**
 * flush any outstanding command buffers to the hardware
 * should be called before a video_buffer is acessed by the gallium frontend again
 */
void
d3d12_video_decoder_flush(struct pipe_video_codec *codec)
{
   struct d3d12_video_decoder *pD3D12Dec = (struct d3d12_video_decoder *) codec;
   assert(pD3D12Dec);
   assert(pD3D12Dec->m_spD3D12VideoDevice);
   assert(pD3D12Dec->m_spDecodeCommandQueue);
   debug_printf("[d3d12_video_decoder] d3d12_video_decoder_flush started. Will flush video queue work and CPU wait on "
                 "fenceValue: %d\n",
                 pD3D12Dec->m_fenceValue);

   if (!pD3D12Dec->m_needsGPUFlush) {
      debug_printf("[d3d12_video_decoder] d3d12_video_decoder_flush started. Nothing to flush, all up to date.\n");
   } else {
      HRESULT hr = pD3D12Dec->m_pD3D12Screen->dev->GetDeviceRemovedReason();
      if (hr != S_OK) {
         debug_printf("[d3d12_video_decoder] d3d12_video_decoder_flush"
                         " - D3D12Device was removed BEFORE commandlist "
                         "execution with HR %x.\n",
                         hr);
         goto flush_fail;
      }

      // Close and execute command list and wait for idle on CPU blocking
      // this method before resetting list and allocator for next submission.

      if (pD3D12Dec->m_transitionsBeforeCloseCmdList.size() > 0) {
         pD3D12Dec->m_spDecodeCommandList->ResourceBarrier(pD3D12Dec->m_transitionsBeforeCloseCmdList.size(),
                                                           pD3D12Dec->m_transitionsBeforeCloseCmdList.data());
         pD3D12Dec->m_transitionsBeforeCloseCmdList.clear();
      }

      hr = pD3D12Dec->m_spDecodeCommandList->Close();
      if (FAILED(hr)) {
         debug_printf("[d3d12_video_decoder] d3d12_video_decoder_flush - Can't close command list with HR %x\n", hr);
         goto flush_fail;
      }

      ID3D12CommandList *ppCommandLists[1] = { pD3D12Dec->m_spDecodeCommandList.Get() };
      pD3D12Dec->m_spDecodeCommandQueue->ExecuteCommandLists(1, ppCommandLists);
      pD3D12Dec->m_spDecodeCommandQueue->Signal(pD3D12Dec->m_spFence.Get(), pD3D12Dec->m_fenceValue);
      pD3D12Dec->m_spFence->SetEventOnCompletion(pD3D12Dec->m_fenceValue, nullptr);
      debug_printf("[d3d12_video_decoder] d3d12_video_decoder_flush - ExecuteCommandLists finished on signal with "
                    "fenceValue: %d\n",
                    pD3D12Dec->m_fenceValue);

      hr = pD3D12Dec->m_spCommandAllocator->Reset();
      if (FAILED(hr)) {
         debug_printf(
            "[d3d12_video_decoder] d3d12_video_decoder_flush - resetting ID3D12CommandAllocator failed with HR %x\n",
            hr);
         goto flush_fail;
      }

      hr = pD3D12Dec->m_spDecodeCommandList->Reset(pD3D12Dec->m_spCommandAllocator.Get());
      if (FAILED(hr)) {
         debug_printf(
            "[d3d12_video_decoder] d3d12_video_decoder_flush - resetting ID3D12GraphicsCommandList failed with HR %x\n",
            hr);
         goto flush_fail;
      }

      // Validate device was not removed
      hr = pD3D12Dec->m_pD3D12Screen->dev->GetDeviceRemovedReason();
      if (hr != S_OK) {
         debug_printf("[d3d12_video_decoder] d3d12_video_decoder_flush"
                         " - D3D12Device was removed AFTER commandlist "
                         "execution with HR %x, but wasn't before.\n",
                         hr);
         goto flush_fail;
      }

      debug_printf(
         "[d3d12_video_decoder] d3d12_video_decoder_flush - GPU signaled execution finalized for fenceValue: %d\n",
         pD3D12Dec->m_fenceValue);

      pD3D12Dec->m_fenceValue++;
      pD3D12Dec->m_needsGPUFlush = false;
   }
   return;

flush_fail:
   debug_printf("[d3d12_video_decoder] d3d12_video_decoder_flush failed for fenceValue: %d\n", pD3D12Dec->m_fenceValue);
   assert(false);
}

bool
d3d12_video_decoder_create_command_objects(const struct d3d12_screen *pD3D12Screen,
                                           struct d3d12_video_decoder *pD3D12Dec)
{
   assert(pD3D12Dec->m_spD3D12VideoDevice);

   D3D12_COMMAND_QUEUE_DESC commandQueueDesc = { D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE };
   HRESULT hr = pD3D12Screen->dev->CreateCommandQueue(&commandQueueDesc,
                                                      IID_PPV_ARGS(pD3D12Dec->m_spDecodeCommandQueue.GetAddressOf()));
   if (FAILED(hr)) {
      debug_printf("[d3d12_video_decoder] d3d12_video_decoder_create_command_objects - Call to CreateCommandQueue "
                      "failed with HR %x\n",
                      hr);
      return false;
   }

   hr = pD3D12Screen->dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pD3D12Dec->m_spFence));
   if (FAILED(hr)) {
      debug_printf(
         "[d3d12_video_decoder] d3d12_video_decoder_create_command_objects - Call to CreateFence failed with HR %x\n",
         hr);
      return false;
   }

   hr = pD3D12Screen->dev->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE,
                                                  IID_PPV_ARGS(pD3D12Dec->m_spCommandAllocator.GetAddressOf()));
   if (FAILED(hr)) {
      debug_printf("[d3d12_video_decoder] d3d12_video_decoder_create_command_objects - Call to "
                      "CreateCommandAllocator failed with HR %x\n",
                      hr);
      return false;
   }

   hr = pD3D12Screen->dev->CreateCommandList(0,
                                             D3D12_COMMAND_LIST_TYPE_VIDEO_DECODE,
                                             pD3D12Dec->m_spCommandAllocator.Get(),
                                             nullptr,
                                             IID_PPV_ARGS(pD3D12Dec->m_spDecodeCommandList.GetAddressOf()));

   if (FAILED(hr)) {
      debug_printf("[d3d12_video_decoder] d3d12_video_decoder_create_command_objects - Call to CreateCommandList "
                      "failed with HR %x\n",
                      hr);
      return false;
   }

   return true;
}

bool
d3d12_video_decoder_check_caps_and_create_decoder(const struct d3d12_screen *pD3D12Screen,
                                                  struct d3d12_video_decoder *pD3D12Dec)
{
   assert(pD3D12Dec->m_spD3D12VideoDevice);

   pD3D12Dec->m_decoderDesc = {};

   D3D12_VIDEO_DECODE_CONFIGURATION decodeConfiguration = { pD3D12Dec->m_d3d12DecProfile,
                                                            D3D12_BITSTREAM_ENCRYPTION_TYPE_NONE,
                                                            D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_NONE };

   D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT decodeSupport = {};
   decodeSupport.NodeIndex = pD3D12Dec->m_NodeIndex;
   decodeSupport.Configuration = decodeConfiguration;
   decodeSupport.Width = pD3D12Dec->base.width;
   decodeSupport.Height = pD3D12Dec->base.height;
   decodeSupport.DecodeFormat = pD3D12Dec->m_decodeFormat;
   // no info from above layer on framerate/bitrate
   decodeSupport.FrameRate.Numerator = 0;
   decodeSupport.FrameRate.Denominator = 0;
   decodeSupport.BitRate = 0;

   HRESULT hr = pD3D12Dec->m_spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_DECODE_SUPPORT,
                                                                     &decodeSupport,
                                                                     sizeof(decodeSupport));
   if (FAILED(hr)) {
      debug_printf("[d3d12_video_decoder] d3d12_video_decoder_check_caps_and_create_decoder - CheckFeatureSupport "
                      "failed with HR %x\n",
                      hr);
      return false;
   }

   if (!(decodeSupport.SupportFlags & D3D12_VIDEO_DECODE_SUPPORT_FLAG_SUPPORTED)) {
      debug_printf("[d3d12_video_decoder] d3d12_video_decoder_check_caps_and_create_decoder - "
                      "D3D12_VIDEO_DECODE_SUPPORT_FLAG_SUPPORTED was false when checking caps \n");
      return false;
   }

   pD3D12Dec->m_configurationFlags = decodeSupport.ConfigurationFlags;
   pD3D12Dec->m_tier = decodeSupport.DecodeTier;

   if (d3d12_video_decoder_supports_aot_dpb(decodeSupport, pD3D12Dec->m_d3d12DecProfileType)) {
      pD3D12Dec->m_ConfigDecoderSpecificFlags |= d3d12_video_decode_config_specific_flag_array_of_textures;
   }

   if (decodeSupport.ConfigurationFlags & D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_HEIGHT_ALIGNMENT_MULTIPLE_32_REQUIRED) {
      pD3D12Dec->m_ConfigDecoderSpecificFlags |= d3d12_video_decode_config_specific_flag_alignment_height;
   }

   if (decodeSupport.ConfigurationFlags & D3D12_VIDEO_DECODE_CONFIGURATION_FLAG_REFERENCE_ONLY_ALLOCATIONS_REQUIRED) {
      pD3D12Dec->m_ConfigDecoderSpecificFlags |=
         d3d12_video_decode_config_specific_flag_reference_only_textures_required;
   }

   pD3D12Dec->m_decoderDesc.NodeMask = pD3D12Dec->m_NodeMask;
   pD3D12Dec->m_decoderDesc.Configuration = decodeConfiguration;

   hr = pD3D12Dec->m_spD3D12VideoDevice->CreateVideoDecoder(&pD3D12Dec->m_decoderDesc,
                                                            IID_PPV_ARGS(pD3D12Dec->m_spVideoDecoder.GetAddressOf()));
   if (FAILED(hr)) {
      debug_printf("[d3d12_video_decoder] d3d12_video_decoder_check_caps_and_create_decoder - CreateVideoDecoder "
                      "failed with HR %x\n",
                      hr);
      return false;
   }

   return true;
}

bool
d3d12_video_decoder_create_video_state_buffers(const struct d3d12_screen *pD3D12Screen,
                                               struct d3d12_video_decoder *pD3D12Dec)
{
   assert(pD3D12Dec->m_spD3D12VideoDevice);
   if (!d3d12_video_decoder_create_staging_bitstream_buffer(pD3D12Screen,
                                                            pD3D12Dec,
                                                            pD3D12Dec->m_InitialCompBitstreamGPUBufferSize)) {
      debug_printf("[d3d12_video_decoder] d3d12_video_decoder_create_video_state_buffers - Failure on "
                      "d3d12_video_decoder_create_staging_bitstream_buffer\n");
      return false;
   }

   return true;
}

bool
d3d12_video_decoder_create_staging_bitstream_buffer(const struct d3d12_screen *pD3D12Screen,
                                                    struct d3d12_video_decoder *pD3D12Dec,
                                                    uint64_t bufSize)
{
   assert(pD3D12Dec->m_spD3D12VideoDevice);

   if (pD3D12Dec->m_curFrameCompressedBitstreamBuffer.Get() != nullptr) {
      pD3D12Dec->m_curFrameCompressedBitstreamBuffer.Reset();
   }

   auto descHeap = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT, pD3D12Dec->m_NodeMask, pD3D12Dec->m_NodeMask);
   auto descResource = CD3DX12_RESOURCE_DESC::Buffer(bufSize);
   HRESULT hr = pD3D12Screen->dev->CreateCommittedResource(
      &descHeap,
      D3D12_HEAP_FLAG_NONE,
      &descResource,
      D3D12_RESOURCE_STATE_COMMON,
      nullptr,
      IID_PPV_ARGS(pD3D12Dec->m_curFrameCompressedBitstreamBuffer.GetAddressOf()));
   if (FAILED(hr)) {
      debug_printf("[d3d12_video_decoder] d3d12_video_decoder_create_staging_bitstream_buffer - "
                      "CreateCommittedResource failed with HR %x\n",
                      hr);
      return false;
   }

   pD3D12Dec->m_curFrameCompressedBitstreamBufferAllocatedSize = bufSize;
   return true;
}

bool
d3d12_video_decoder_prepare_for_decode_frame(struct d3d12_video_decoder *pD3D12Dec,
                                             struct pipe_video_buffer *pCurrentDecodeTarget,
                                             struct d3d12_video_buffer *pD3D12VideoBuffer,
                                             ID3D12Resource **ppOutTexture2D,
                                             uint32_t *pOutSubresourceIndex,
                                             ID3D12Resource **ppRefOnlyOutTexture2D,
                                             uint32_t *pRefOnlyOutSubresourceIndex,
                                             const d3d12_video_decode_output_conversion_arguments &conversionArgs)
{
   if(!d3d12_video_decoder_reconfigure_dpb(pD3D12Dec, pD3D12VideoBuffer, conversionArgs)) {
      debug_printf("d3d12_video_decoder_reconfigure_dpb failed!\n");
      return false;
   }

   // Refresh DPB active references for current frame, release memory for unused references.
   d3d12_video_decoder_refresh_dpb_active_references(pD3D12Dec);

   // Get the output texture for the current frame to be decoded
   pD3D12Dec->m_spDPBManager->get_current_frame_decode_output_texture(pCurrentDecodeTarget,
                                                                      ppOutTexture2D,
                                                                      pOutSubresourceIndex);

   auto vidBuffer = (struct d3d12_video_buffer *)(pCurrentDecodeTarget);
   // If is_pipe_buffer_underlying_output_decode_allocation is enabled,
   // we can just use the underlying allocation in pCurrentDecodeTarget
   // and avoid an extra copy after decoding the frame.
   // If this is the case, we need to handle the residency of this resource
   // (if not we're actually creating the resources with CreateCommitedResource with
   // residency by default)
   if(pD3D12Dec->m_spDPBManager->is_pipe_buffer_underlying_output_decode_allocation()) {
      assert(d3d12_resource_resource(vidBuffer->texture) == *ppOutTexture2D);
      // Make it permanently resident for video use
      d3d12_promote_to_permanent_residency(pD3D12Dec->m_pD3D12Screen, vidBuffer->texture);
   }

   // Get the reference only texture for the current frame to be decoded (if applicable)
   bool fReferenceOnly = (pD3D12Dec->m_ConfigDecoderSpecificFlags &
                          d3d12_video_decode_config_specific_flag_reference_only_textures_required) != 0;
   if (fReferenceOnly) {
      bool needsTransitionToDecodeWrite = false;
      pD3D12Dec->m_spDPBManager->get_reference_only_output(pCurrentDecodeTarget,
                                                           ppRefOnlyOutTexture2D,
                                                           pRefOnlyOutSubresourceIndex,
                                                           needsTransitionToDecodeWrite);
      assert(needsTransitionToDecodeWrite);

      CD3DX12_RESOURCE_DESC outputDesc(GetDesc(*ppRefOnlyOutTexture2D));
      uint32_t MipLevel, PlaneSlice, ArraySlice;
      D3D12DecomposeSubresource(*pRefOnlyOutSubresourceIndex,
                                outputDesc.MipLevels,
                                outputDesc.ArraySize(),
                                MipLevel,
                                ArraySlice,
                                PlaneSlice);

      for (PlaneSlice = 0; PlaneSlice < pD3D12Dec->m_decodeFormatInfo.PlaneCount; PlaneSlice++) {
         uint planeOutputSubresource = outputDesc.CalcSubresource(MipLevel, ArraySlice, PlaneSlice);

         D3D12_RESOURCE_BARRIER resourceBarrierCommonToDecode[1] = {
            CD3DX12_RESOURCE_BARRIER::Transition(*ppRefOnlyOutTexture2D,
                                                 D3D12_RESOURCE_STATE_COMMON,
                                                 D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
                                                 planeOutputSubresource),
         };
         pD3D12Dec->m_spDecodeCommandList->ResourceBarrier(1u, resourceBarrierCommonToDecode);
      }

      // Schedule reverse (back to common) transitions before command list closes for current frame
      for (PlaneSlice = 0; PlaneSlice < pD3D12Dec->m_decodeFormatInfo.PlaneCount; PlaneSlice++) {
         uint planeOutputSubresource = outputDesc.CalcSubresource(MipLevel, ArraySlice, PlaneSlice);
         pD3D12Dec->m_transitionsBeforeCloseCmdList.push_back(
            CD3DX12_RESOURCE_BARRIER::Transition(*ppRefOnlyOutTexture2D,
                                                 D3D12_RESOURCE_STATE_VIDEO_DECODE_WRITE,
                                                 D3D12_RESOURCE_STATE_COMMON,
                                                 planeOutputSubresource));
      }
   }

   // If decoded needs reference_only entries in the dpb, use the reference_only allocation for current frame
   // otherwise, use the standard output resource
   ID3D12Resource *pCurrentFrameDPBEntry = fReferenceOnly ? *ppRefOnlyOutTexture2D : *ppOutTexture2D;
   uint32_t currentFrameDPBEntrySubresource = fReferenceOnly ? *pRefOnlyOutSubresourceIndex : *pOutSubresourceIndex;

   switch (pD3D12Dec->m_d3d12DecProfileType) {
      case d3d12_video_decode_profile_type_h264:
      {
         d3d12_video_decoder_prepare_current_frame_references_h264(pD3D12Dec,
                                                                   pCurrentFrameDPBEntry,
                                                                   currentFrameDPBEntrySubresource);
      } break;

      case d3d12_video_decode_profile_type_hevc:
      {
         d3d12_video_decoder_prepare_current_frame_references_hevc(pD3D12Dec,
                                                                   pCurrentFrameDPBEntry,
                                                                   currentFrameDPBEntrySubresource);
      } break;

      default:
      {
         unreachable("Unsupported d3d12_video_decode_profile_type");
      } break;
   }

   return true;
}

bool
d3d12_video_decoder_reconfigure_dpb(struct d3d12_video_decoder *pD3D12Dec,
                                    struct d3d12_video_buffer *pD3D12VideoBuffer,
                                    const d3d12_video_decode_output_conversion_arguments &conversionArguments)
{
   uint32_t width;
   uint32_t height;
   uint16_t maxDPB;
   bool isInterlaced;
   d3d12_video_decoder_get_frame_info(pD3D12Dec, &width, &height, &maxDPB, isInterlaced);

   ID3D12Resource *pPipeD3D12DstResource = d3d12_resource_resource(pD3D12VideoBuffer->texture);
   D3D12_RESOURCE_DESC outputResourceDesc = GetDesc(pPipeD3D12DstResource);

   pD3D12VideoBuffer->base.interlaced = isInterlaced;
   D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE interlaceTypeRequested =
      isInterlaced ? D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_FIELD_BASED : D3D12_VIDEO_FRAME_CODED_INTERLACE_TYPE_NONE;
   if ((pD3D12Dec->m_decodeFormat != outputResourceDesc.Format) ||
       (pD3D12Dec->m_decoderDesc.Configuration.InterlaceType != interlaceTypeRequested)) {
      // Copy current pD3D12Dec->m_decoderDesc, modify decodeprofile and re-create decoder.
      D3D12_VIDEO_DECODER_DESC decoderDesc = pD3D12Dec->m_decoderDesc;
      decoderDesc.Configuration.InterlaceType = interlaceTypeRequested;
      decoderDesc.Configuration.DecodeProfile =
         d3d12_video_decoder_resolve_profile(pD3D12Dec->m_d3d12DecProfileType, pD3D12Dec->m_decodeFormat);
      pD3D12Dec->m_spVideoDecoder.Reset();
      HRESULT hr =
         pD3D12Dec->m_spD3D12VideoDevice->CreateVideoDecoder(&decoderDesc,
                                                             IID_PPV_ARGS(pD3D12Dec->m_spVideoDecoder.GetAddressOf()));
      if (FAILED(hr)) {
         debug_printf(
            "[d3d12_video_decoder] d3d12_video_decoder_reconfigure_dpb - CreateVideoDecoder failed with HR %x\n",
            hr);
         return false;
      }
      // Update state after CreateVideoDecoder succeeds only.
      pD3D12Dec->m_decoderDesc = decoderDesc;
   }

   if (!pD3D12Dec->m_spDPBManager || !pD3D12Dec->m_spVideoDecoderHeap ||
       pD3D12Dec->m_decodeFormat != outputResourceDesc.Format || pD3D12Dec->m_decoderHeapDesc.DecodeWidth != width ||
       pD3D12Dec->m_decoderHeapDesc.DecodeHeight != height ||
       pD3D12Dec->m_decoderHeapDesc.MaxDecodePictureBufferCount < maxDPB) {
      // Detect the combination of AOT/ReferenceOnly to configure the DPB manager
      uint16_t referenceCount = (conversionArguments.Enable) ? (uint16_t) conversionArguments.ReferenceFrameCount +
                                                                  1 /*extra slot for current picture*/ :
                                                               maxDPB;
      d3d12_video_decode_dpb_descriptor dpbDesc = {};
      dpbDesc.Width = (conversionArguments.Enable) ? conversionArguments.ReferenceInfo.Width : width;
      dpbDesc.Height = (conversionArguments.Enable) ? conversionArguments.ReferenceInfo.Height : height;
      dpbDesc.Format =
         (conversionArguments.Enable) ? conversionArguments.ReferenceInfo.Format.Format : outputResourceDesc.Format;
      dpbDesc.fArrayOfTexture =
         ((pD3D12Dec->m_ConfigDecoderSpecificFlags & d3d12_video_decode_config_specific_flag_array_of_textures) != 0);
      dpbDesc.dpbSize = referenceCount;
      dpbDesc.m_NodeMask = pD3D12Dec->m_NodeMask;
      dpbDesc.fReferenceOnly = ((pD3D12Dec->m_ConfigDecoderSpecificFlags &
                                 d3d12_video_decode_config_specific_flag_reference_only_textures_required) != 0);

      // Create DPB manager
      if (pD3D12Dec->m_spDPBManager == nullptr) {
         pD3D12Dec->m_spDPBManager.reset(new d3d12_video_decoder_references_manager(pD3D12Dec->m_pD3D12Screen,
                                                                                    pD3D12Dec->m_NodeMask,
                                                                                    pD3D12Dec->m_d3d12DecProfileType,
                                                                                    dpbDesc));
      }

      //
      // (Re)-create decoder heap
      //
      D3D12_VIDEO_DECODER_HEAP_DESC decoderHeapDesc = {};
      decoderHeapDesc.NodeMask = pD3D12Dec->m_NodeMask;
      decoderHeapDesc.Configuration = pD3D12Dec->m_decoderDesc.Configuration;
      decoderHeapDesc.DecodeWidth = dpbDesc.Width;
      decoderHeapDesc.DecodeHeight = dpbDesc.Height;
      decoderHeapDesc.Format = dpbDesc.Format;
      decoderHeapDesc.MaxDecodePictureBufferCount = maxDPB;
      pD3D12Dec->m_spVideoDecoderHeap.Reset();
      HRESULT hr = pD3D12Dec->m_spD3D12VideoDevice->CreateVideoDecoderHeap(
         &decoderHeapDesc,
         IID_PPV_ARGS(pD3D12Dec->m_spVideoDecoderHeap.GetAddressOf()));
      if (FAILED(hr)) {
         debug_printf(
            "[d3d12_video_decoder] d3d12_video_decoder_reconfigure_dpb - CreateVideoDecoderHeap failed with HR %x\n",
            hr);
         return false;
      }
      // Update pD3D12Dec after CreateVideoDecoderHeap succeeds only.
      pD3D12Dec->m_decoderHeapDesc = decoderHeapDesc;
   }

   pD3D12Dec->m_decodeFormat = outputResourceDesc.Format;

   return true;
}

void
d3d12_video_decoder_refresh_dpb_active_references(struct d3d12_video_decoder *pD3D12Dec)
{
   switch (pD3D12Dec->m_d3d12DecProfileType) {
      case d3d12_video_decode_profile_type_h264:
      {
         d3d12_video_decoder_refresh_dpb_active_references_h264(pD3D12Dec);
      } break;

      case d3d12_video_decode_profile_type_hevc:
      {
         d3d12_video_decoder_refresh_dpb_active_references_hevc(pD3D12Dec);
      } break;

      default:
      {
         unreachable("Unsupported d3d12_video_decode_profile_type");
      } break;
   }
}

void
d3d12_video_decoder_get_frame_info(
   struct d3d12_video_decoder *pD3D12Dec, uint32_t *pWidth, uint32_t *pHeight, uint16_t *pMaxDPB, bool &isInterlaced)
{
   *pWidth = 0;
   *pHeight = 0;
   *pMaxDPB = 0;
   isInterlaced = false;

   switch (pD3D12Dec->m_d3d12DecProfileType) {
      case d3d12_video_decode_profile_type_h264:
      {
         d3d12_video_decoder_get_frame_info_h264(pD3D12Dec, pWidth, pHeight, pMaxDPB, isInterlaced);
      } break;

      case d3d12_video_decode_profile_type_hevc:
      {
         d3d12_video_decoder_get_frame_info_hevc(pD3D12Dec, pWidth, pHeight, pMaxDPB, isInterlaced);
      } break;

      default:
      {
         unreachable("Unsupported d3d12_video_decode_profile_type");
      } break;
   }

   if (pD3D12Dec->m_ConfigDecoderSpecificFlags & d3d12_video_decode_config_specific_flag_alignment_height) {
      const uint32_t AlignmentMask = 31;
      *pHeight = (*pHeight + AlignmentMask) & ~AlignmentMask;
   }
}

///
/// Returns the number of bytes starting from [buf.data() + buffsetOffset] where the _targetCode_ is found
/// Returns -1 if start code not found
///
int
d3d12_video_decoder_get_next_startcode_offset(std::vector<uint8_t> &buf,
                                              unsigned int bufferOffset,
                                              unsigned int targetCode,
                                              unsigned int targetCodeBitSize,
                                              unsigned int numBitsToSearchIntoBuffer)
{
   struct vl_vlc vlc = { 0 };

   // Shorten the buffer to be [buffetOffset, endOfBuf)
   unsigned int bufSize = buf.size() - bufferOffset;
   uint8_t *bufPtr = buf.data();
   bufPtr += bufferOffset;

   /* search the first numBitsToSearchIntoBuffer bytes for a startcode */
   vl_vlc_init(&vlc, 1, (const void *const *) &bufPtr, &bufSize);
   for (uint i = 0; i < numBitsToSearchIntoBuffer && vl_vlc_bits_left(&vlc) >= targetCodeBitSize; ++i) {
      if (vl_vlc_peekbits(&vlc, targetCodeBitSize) == targetCode)
         return i;
      vl_vlc_eatbits(&vlc, 8);   // Stride is 8 bits = 1 byte
      vl_vlc_fillbits(&vlc);
   }

   return -1;
}

void
d3d12_video_decoder_store_converted_dxva_picparams_from_pipe_input(
   struct d3d12_video_decoder *codec,   // input argument, current decoder
   struct pipe_picture_desc
      *picture,   // input argument, base structure of pipe_XXX_picture_desc where XXX is the codec name
   struct d3d12_video_buffer *pD3D12VideoBuffer   // input argument, target video buffer
)
{
   assert(picture);
   assert(codec);
   struct d3d12_video_decoder *pD3D12Dec = (struct d3d12_video_decoder *) codec;

   d3d12_video_decode_profile_type profileType =
      d3d12_video_decoder_convert_pipe_video_profile_to_profile_type(codec->base.profile);
   ID3D12Resource *pPipeD3D12DstResource = d3d12_resource_resource(pD3D12VideoBuffer->texture);
   D3D12_RESOURCE_DESC outputResourceDesc = GetDesc(pPipeD3D12DstResource);
   pD3D12Dec->qp_matrix_frame_argument_enabled = false;
   switch (profileType) {
      case d3d12_video_decode_profile_type_h264:
      {
         size_t dxvaPicParamsBufferSize = sizeof(DXVA_PicParams_H264);
         pipe_h264_picture_desc *pPicControlH264 = (pipe_h264_picture_desc *) picture;
         DXVA_PicParams_H264 dxvaPicParamsH264 =
            d3d12_video_decoder_dxva_picparams_from_pipe_picparams_h264(pD3D12Dec->m_fenceValue,
                                                                        codec->base.profile,
                                                                        outputResourceDesc.Width,
                                                                        outputResourceDesc.Height,
                                                                        pPicControlH264);

         d3d12_video_decoder_store_dxva_picparams_in_picparams_buffer(codec,
                                                                      &dxvaPicParamsH264,
                                                                      dxvaPicParamsBufferSize);

         size_t dxvaQMatrixBufferSize = sizeof(DXVA_Qmatrix_H264);
         DXVA_Qmatrix_H264 dxvaQmatrixH264 = {};
         d3d12_video_decoder_dxva_qmatrix_from_pipe_picparams_h264((pipe_h264_picture_desc *) picture,
                                                                   dxvaQmatrixH264);
         pD3D12Dec->qp_matrix_frame_argument_enabled = true; // We don't have a way of knowing from the pipe params so send always  
         d3d12_video_decoder_store_dxva_qmatrix_in_qmatrix_buffer(codec, &dxvaQmatrixH264, dxvaQMatrixBufferSize);
      } break;

      case d3d12_video_decode_profile_type_hevc:
      {
         size_t dxvaPicParamsBufferSize = sizeof(DXVA_PicParams_HEVC);
         pipe_h265_picture_desc *pPicControlHEVC = (pipe_h265_picture_desc *) picture;
         DXVA_PicParams_HEVC dxvaPicParamsHEVC =
            d3d12_video_decoder_dxva_picparams_from_pipe_picparams_hevc(pD3D12Dec,
                                                                        codec->base.profile,
                                                                        pPicControlHEVC);

         d3d12_video_decoder_store_dxva_picparams_in_picparams_buffer(codec,
                                                                      &dxvaPicParamsHEVC,
                                                                      dxvaPicParamsBufferSize);

         size_t dxvaQMatrixBufferSize = sizeof(DXVA_Qmatrix_HEVC);
         DXVA_Qmatrix_HEVC dxvaQmatrixHEVC = {};
         pD3D12Dec->qp_matrix_frame_argument_enabled = false; 
         d3d12_video_decoder_dxva_qmatrix_from_pipe_picparams_hevc((pipe_h265_picture_desc *) picture,
                                                                   dxvaQmatrixHEVC,
                                                                   pD3D12Dec->qp_matrix_frame_argument_enabled);
         d3d12_video_decoder_store_dxva_qmatrix_in_qmatrix_buffer(codec, &dxvaQmatrixHEVC, dxvaQMatrixBufferSize);
      } break;

      default:
      {
         unreachable("Unsupported d3d12_video_decode_profile_type");
      } break;
   }
}

void
d3d12_video_decoder_prepare_dxva_slices_control(
   struct d3d12_video_decoder *pD3D12Dec,   // input argument, current decoder
   struct pipe_picture_desc *picture
)
{
   d3d12_video_decode_profile_type profileType =
      d3d12_video_decoder_convert_pipe_video_profile_to_profile_type(pD3D12Dec->base.profile);
   switch (profileType) {
      case d3d12_video_decode_profile_type_h264:
      {
         d3d12_video_decoder_prepare_dxva_slices_control_h264(pD3D12Dec, pD3D12Dec->m_SliceControlBuffer, (struct pipe_h264_picture_desc*) picture);
      } break;

      case d3d12_video_decode_profile_type_hevc:
      {
         d3d12_video_decoder_prepare_dxva_slices_control_hevc(pD3D12Dec, pD3D12Dec->m_SliceControlBuffer, (struct pipe_h265_picture_desc*) picture);
      } break;
      default:
      {
         unreachable("Unsupported d3d12_video_decode_profile_type");
      } break;
   }
}

void
d3d12_video_decoder_store_dxva_qmatrix_in_qmatrix_buffer(struct d3d12_video_decoder *pD3D12Dec,
                                                         void *pDXVAStruct,
                                                         uint64_t DXVAStructSize)
{
   if (pD3D12Dec->m_InverseQuantMatrixBuffer.capacity() < DXVAStructSize) {
      pD3D12Dec->m_InverseQuantMatrixBuffer.reserve(DXVAStructSize);
   }

   pD3D12Dec->m_InverseQuantMatrixBuffer.resize(DXVAStructSize);
   memcpy(pD3D12Dec->m_InverseQuantMatrixBuffer.data(), pDXVAStruct, DXVAStructSize);
}

void
d3d12_video_decoder_store_dxva_picparams_in_picparams_buffer(struct d3d12_video_decoder *pD3D12Dec,
                                                             void *pDXVAStruct,
                                                             uint64_t DXVAStructSize)
{
   if (pD3D12Dec->m_picParamsBuffer.capacity() < DXVAStructSize) {
      pD3D12Dec->m_picParamsBuffer.reserve(DXVAStructSize);
   }

   pD3D12Dec->m_picParamsBuffer.resize(DXVAStructSize);
   memcpy(pD3D12Dec->m_picParamsBuffer.data(), pDXVAStruct, DXVAStructSize);
}

bool
d3d12_video_decoder_supports_aot_dpb(D3D12_FEATURE_DATA_VIDEO_DECODE_SUPPORT decodeSupport,
                                     d3d12_video_decode_profile_type profileType)
{
   bool supportedProfile = false;
   switch (profileType) {
      case d3d12_video_decode_profile_type_h264:
      case d3d12_video_decode_profile_type_hevc:
         supportedProfile = true;
         break;
      default:
         supportedProfile = false;
         break;
   }

   return (decodeSupport.DecodeTier >= D3D12_VIDEO_DECODE_TIER_2) && supportedProfile;
}

d3d12_video_decode_profile_type
d3d12_video_decoder_convert_pipe_video_profile_to_profile_type(enum pipe_video_profile profile)
{
   switch (profile) {
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10:
         return d3d12_video_decode_profile_type_h264;
      case PIPE_VIDEO_PROFILE_HEVC_MAIN:
      case PIPE_VIDEO_PROFILE_HEVC_MAIN_10:
         return d3d12_video_decode_profile_type_hevc;
      default:
      {
         unreachable("Unsupported pipe video profile");
      } break;
   }
}

GUID
d3d12_video_decoder_convert_pipe_video_profile_to_d3d12_profile(enum pipe_video_profile profile)
{
   switch (profile) {
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_CONSTRAINED_BASELINE:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_EXTENDED:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
      case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10:
         return D3D12_VIDEO_DECODE_PROFILE_H264;
      case PIPE_VIDEO_PROFILE_HEVC_MAIN:
         return D3D12_VIDEO_DECODE_PROFILE_HEVC_MAIN;
      case PIPE_VIDEO_PROFILE_HEVC_MAIN_10:
         return D3D12_VIDEO_DECODE_PROFILE_HEVC_MAIN10;
      default:
         return {};
   }
}

GUID
d3d12_video_decoder_resolve_profile(d3d12_video_decode_profile_type profileType, DXGI_FORMAT decode_format)
{
   switch (profileType) {
      case d3d12_video_decode_profile_type_h264:
         return D3D12_VIDEO_DECODE_PROFILE_H264;
      case d3d12_video_decode_profile_type_hevc:
         return (decode_format == DXGI_FORMAT_NV12) ? D3D12_VIDEO_DECODE_PROFILE_HEVC_MAIN : D3D12_VIDEO_DECODE_PROFILE_HEVC_MAIN10;
      default:
      {
         unreachable("Unsupported d3d12_video_decode_profile_type");
      } break;
   }
}
