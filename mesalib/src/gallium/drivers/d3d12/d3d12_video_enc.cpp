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

#include "d3d12_common.h"

#include "d3d12_util.h"
#include "d3d12_context.h"
#include "d3d12_format.h"
#include "d3d12_resource.h"
#include "d3d12_screen.h"
#include "d3d12_surface.h"
#include "d3d12_video_enc.h"
#include "d3d12_video_enc_h264.h"
#include "d3d12_video_enc_hevc.h"
#include "d3d12_video_buffer.h"
#include "d3d12_video_texture_array_dpb_manager.h"
#include "d3d12_video_array_of_textures_dpb_manager.h"
#include "d3d12_video_encoder_references_manager_h264.h"
#include "d3d12_video_encoder_references_manager_hevc.h"
#include "d3d12_residency.h"

#include "vl/vl_video_buffer.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_video.h"

#include <cmath>

/**
 * flush any outstanding command buffers to the hardware
 * should be called before a video_buffer is acessed by the gallium frontend again
 */
void
d3d12_video_encoder_flush(struct pipe_video_codec *codec)
{
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   assert(pD3D12Enc);
   assert(pD3D12Enc->m_spD3D12VideoDevice);
   assert(pD3D12Enc->m_spEncodeCommandQueue);

   // Flush buffer_subdata batch and Wait the m_spEncodeCommandQueue for GPU upload completion
   // before recording EncodeFrame below.
   struct pipe_fence_handle *completion_fence = NULL;
   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_flush - Flushing pD3D12Enc->base.context and GPU sync between Video/Context queues before flushing Video Encode Queue.\n");
   pD3D12Enc->base.context->flush(pD3D12Enc->base.context, &completion_fence, PIPE_FLUSH_ASYNC | PIPE_FLUSH_HINT_FINISH);
   assert(completion_fence);
   struct d3d12_fence *casted_completion_fence = d3d12_fence(completion_fence);
   pD3D12Enc->m_spEncodeCommandQueue->Wait(casted_completion_fence->cmdqueue_fence, casted_completion_fence->value);
   pD3D12Enc->m_pD3D12Screen->base.fence_reference(&pD3D12Enc->m_pD3D12Screen->base, &completion_fence, NULL);

   if (!pD3D12Enc->m_needsGPUFlush) {
      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_flush started. Nothing to flush, all up to date.\n");
   } else {
      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_flush started. Will flush video queue work and CPU wait "
                    "on fenceValue: %d\n",
                    pD3D12Enc->m_fenceValue);

      HRESULT hr = pD3D12Enc->m_pD3D12Screen->dev->GetDeviceRemovedReason();
      if (hr != S_OK) {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_flush"
                         " - D3D12Device was removed BEFORE commandlist "
                         "execution with HR %x.\n",
                         hr);
         goto flush_fail;
      }

      // Close and execute command list and wait for idle on CPU blocking
      // this method before resetting list and allocator for next submission.

      if (pD3D12Enc->m_transitionsBeforeCloseCmdList.size() > 0) {
         pD3D12Enc->m_spEncodeCommandList->ResourceBarrier(pD3D12Enc->m_transitionsBeforeCloseCmdList.size(),
                                                           pD3D12Enc->m_transitionsBeforeCloseCmdList.data());
         pD3D12Enc->m_transitionsBeforeCloseCmdList.clear();
      }

      hr = pD3D12Enc->m_spEncodeCommandList->Close();
      if (FAILED(hr)) {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_flush - Can't close command list with HR %x\n", hr);
         goto flush_fail;
      }

      ID3D12CommandList *ppCommandLists[1] = { pD3D12Enc->m_spEncodeCommandList.Get() };
      pD3D12Enc->m_spEncodeCommandQueue->ExecuteCommandLists(1, ppCommandLists);
      pD3D12Enc->m_spEncodeCommandQueue->Signal(pD3D12Enc->m_spFence.Get(), pD3D12Enc->m_fenceValue);
      pD3D12Enc->m_spFence->SetEventOnCompletion(pD3D12Enc->m_fenceValue, nullptr);
      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_flush - ExecuteCommandLists finished on signal with "
                    "fenceValue: %d\n",
                    pD3D12Enc->m_fenceValue);

      hr = pD3D12Enc->m_spCommandAllocator->Reset();
      if (FAILED(hr)) {
         debug_printf(
            "[d3d12_video_encoder] d3d12_video_encoder_flush - resetting ID3D12CommandAllocator failed with HR %x\n",
            hr);
         goto flush_fail;
      }

      hr = pD3D12Enc->m_spEncodeCommandList->Reset(pD3D12Enc->m_spCommandAllocator.Get());
      if (FAILED(hr)) {
         debug_printf(
            "[d3d12_video_encoder] d3d12_video_encoder_flush - resetting ID3D12GraphicsCommandList failed with HR %x\n",
            hr);
         goto flush_fail;
      }

      // Validate device was not removed
      hr = pD3D12Enc->m_pD3D12Screen->dev->GetDeviceRemovedReason();
      if (hr != S_OK) {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_flush" 
                         " - D3D12Device was removed AFTER commandlist "
                         "execution with HR %x, but wasn't before.\n",
                         hr);
         goto flush_fail;
      }

      debug_printf(
         "[d3d12_video_encoder] d3d12_video_encoder_flush - GPU signaled execution finalized for fenceValue: %d\n",
         pD3D12Enc->m_fenceValue);

      pD3D12Enc->m_fenceValue++;
      pD3D12Enc->m_needsGPUFlush = false;
   }
   return;

flush_fail:
   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_flush failed for fenceValue: %d\n", pD3D12Enc->m_fenceValue);
   assert(false);
}

/**
 * Destroys a d3d12_video_encoder
 * Call destroy_XX for applicable XX nested member types before deallocating
 * Destroy methods should check != nullptr on their input target argument as this method can be called as part of
 * cleanup from failure on the creation method
 */
void
d3d12_video_encoder_destroy(struct pipe_video_codec *codec)
{
   if (codec == nullptr) {
      return;
   }

   d3d12_video_encoder_flush(codec);   // Flush pending work before destroying.

   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;

   // Call d3d12_video_encoder dtor to make ComPtr and other member's destructors work
   delete pD3D12Enc;
}

void
d3d12_video_encoder_update_picparams_tracking(struct d3d12_video_encoder *pD3D12Enc,
                                              struct pipe_video_buffer *  srcTexture,
                                              struct pipe_picture_desc *  picture)
{
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA currentPicParams =
      d3d12_video_encoder_get_current_picture_param_settings(pD3D12Enc);

   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   bool bUsedAsReference = false;
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         d3d12_video_encoder_update_current_frame_pic_params_info_h264(pD3D12Enc, srcTexture, picture, currentPicParams, bUsedAsReference);
      } break;

      case PIPE_VIDEO_FORMAT_HEVC:
      {
         d3d12_video_encoder_update_current_frame_pic_params_info_hevc(pD3D12Enc, srcTexture, picture, currentPicParams, bUsedAsReference);
      } break;

      default:
      {
         unreachable("Unsupported pipe_video_format");
      } break;
   }

   pD3D12Enc->m_upDPBManager->begin_frame(currentPicParams, bUsedAsReference, picture);
}

bool
d3d12_video_encoder_reconfigure_encoder_objects(struct d3d12_video_encoder *pD3D12Enc,
                                                struct pipe_video_buffer *  srcTexture,
                                                struct pipe_picture_desc *  picture)
{
   bool codecChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_codec) != 0);
   bool profileChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_profile) != 0);
   bool levelChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_level) != 0);
   bool codecConfigChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_codec_config) != 0);
   bool inputFormatChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_input_format) != 0);
   bool resolutionChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_resolution) != 0);
   bool rateControlChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_rate_control) != 0);
   bool slicesChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_slices) != 0);
   bool gopChanged =
      ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags & d3d12_video_encoder_config_dirty_flag_gop) != 0);
   bool motionPrecisionLimitChanged = ((pD3D12Enc->m_currentEncodeConfig.m_ConfigDirtyFlags &
                                        d3d12_video_encoder_config_dirty_flag_motion_precision_limit) != 0);

   // Events that that trigger a re-creation of the reference picture manager
   // Stores codec agnostic textures so only input format, resolution and gop (num dpb references) affects this
   if (!pD3D12Enc->m_upDPBManager
       // || codecChanged
       // || profileChanged
       // || levelChanged
       // || codecConfigChanged
       || inputFormatChanged ||
       resolutionChanged
       // || rateControlChanged
       // || slicesChanged
       || gopChanged
       // || motionPrecisionLimitChanged
   ) {
      if (!pD3D12Enc->m_upDPBManager) {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_reconfigure_encoder_objects - Creating Reference "
                       "Pictures Manager for the first time\n");
      } else {
         debug_printf("[d3d12_video_encoder] Reconfiguration triggered -> Re-creating Reference Pictures Manager\n");
      }

      D3D12_RESOURCE_FLAGS resourceAllocFlags =
         D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
      bool     fArrayOfTextures = ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                                D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RECONSTRUCTED_FRAMES_REQUIRE_TEXTURE_ARRAYS) == 0);
      uint32_t texturePoolSize  = d3d12_video_encoder_get_current_max_dpb_capacity(pD3D12Enc) +
                                 1u;   // adding an extra slot as we also need to count the current frame output recon
                                       // allocation along max reference frame allocations
      assert(texturePoolSize < UINT16_MAX);
      if (fArrayOfTextures) {
         pD3D12Enc->m_upDPBStorageManager = std::make_unique<d3d12_array_of_textures_dpb_manager>(
            static_cast<uint16_t>(texturePoolSize),
            pD3D12Enc->m_pD3D12Screen->dev,
            pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
            pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
            (D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY | D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE),
            true,   // setNullSubresourcesOnAllZero - D3D12 Video Encode expects nullptr pSubresources if AoT,
            pD3D12Enc->m_NodeMask,
            /*use underlying pool, we can't reuse upper level allocations, need D3D12_RESOURCE_FLAG_VIDEO_ENCODE_REFERENCE_ONLY*/
            true);
      } else {
         pD3D12Enc->m_upDPBStorageManager = std::make_unique<d3d12_texture_array_dpb_manager>(
            static_cast<uint16_t>(texturePoolSize),
            pD3D12Enc->m_pD3D12Screen->dev,
            pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
            pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
            resourceAllocFlags,
            pD3D12Enc->m_NodeMask);
      }
      d3d12_video_encoder_create_reference_picture_manager(pD3D12Enc);
   }

   bool reCreatedEncoder = false;
   // Events that that trigger a re-creation of the encoder
   if (!pD3D12Enc->m_spVideoEncoder || codecChanged ||
       profileChanged
       // || levelChanged // Only affects encoder heap
       || codecConfigChanged ||
       inputFormatChanged
       // || resolutionChanged // Only affects encoder heap
       // Only re-create if there is NO SUPPORT for reconfiguring rateControl on the fly
       || (rateControlChanged && ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                                   D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_RECONFIGURATION_AVAILABLE) ==
                                  0 /*checking the flag is NOT set*/))
       // Only re-create if there is NO SUPPORT for reconfiguring slices on the fly
       || (slicesChanged && ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                              D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SUBREGION_LAYOUT_RECONFIGURATION_AVAILABLE) ==
                             0 /*checking the flag is NOT set*/))
       // Only re-create if there is NO SUPPORT for reconfiguring gop on the fly
       || (gopChanged && ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                           D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SEQUENCE_GOP_RECONFIGURATION_AVAILABLE) ==
                          0 /*checking the flag is NOT set*/)) ||
       motionPrecisionLimitChanged) {
      if (!pD3D12Enc->m_spVideoEncoder) {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_reconfigure_encoder_objects - Creating "
                       "D3D12VideoEncoder for the first time\n");
      } else {
         debug_printf("[d3d12_video_encoder] Reconfiguration triggered -> Re-creating D3D12VideoEncoder\n");
         reCreatedEncoder = true;
      }

      D3D12_VIDEO_ENCODER_DESC encoderDesc = { pD3D12Enc->m_NodeMask,
                                               D3D12_VIDEO_ENCODER_FLAG_NONE,
                                               pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc,
                                               d3d12_video_encoder_get_current_profile_desc(pD3D12Enc),
                                               pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
                                               d3d12_video_encoder_get_current_codec_config_desc(pD3D12Enc),
                                               pD3D12Enc->m_currentEncodeConfig.m_encoderMotionPrecisionLimit };

      // Create encoder
      HRESULT hr = pD3D12Enc->m_spD3D12VideoDevice->CreateVideoEncoder(&encoderDesc,
                                                             IID_PPV_ARGS(pD3D12Enc->m_spVideoEncoder.GetAddressOf()));
      if (FAILED(hr)) {
         debug_printf("CreateVideoEncoder failed with HR %x\n", hr);
         return false;
      }
   }

   bool reCreatedEncoderHeap = false;
   // Events that that trigger a re-creation of the encoder heap
   if (!pD3D12Enc->m_spVideoEncoderHeap || codecChanged || profileChanged ||
       levelChanged
       // || codecConfigChanged // Only affects encoder
       || inputFormatChanged   // Might affect internal textures in the heap
       || resolutionChanged
       // Only re-create if there is NO SUPPORT for reconfiguring rateControl on the fly
       || (rateControlChanged && ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                                   D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_RECONFIGURATION_AVAILABLE) ==
                                  0 /*checking the flag is NOT set*/))
       // Only re-create if there is NO SUPPORT for reconfiguring slices on the fly
       || (slicesChanged && ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                              D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SUBREGION_LAYOUT_RECONFIGURATION_AVAILABLE) ==
                             0 /*checking the flag is NOT set*/))
       // Only re-create if there is NO SUPPORT for reconfiguring gop on the fly
       || (gopChanged && ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
                           D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SEQUENCE_GOP_RECONFIGURATION_AVAILABLE) ==
                          0 /*checking the flag is NOT set*/))
       // || motionPrecisionLimitChanged // Only affects encoder
   ) {
      if (!pD3D12Enc->m_spVideoEncoderHeap) {
         debug_printf("[d3d12_video_encoder] d3d12_video_encoder_reconfigure_encoder_objects - Creating "
                       "D3D12VideoEncoderHeap for the first time\n");
      } else {
         debug_printf("[d3d12_video_encoder] Reconfiguration triggered -> Re-creating D3D12VideoEncoderHeap\n");
         reCreatedEncoderHeap = true;
      }

      D3D12_VIDEO_ENCODER_HEAP_DESC heapDesc = { pD3D12Enc->m_NodeMask,
                                                 D3D12_VIDEO_ENCODER_HEAP_FLAG_NONE,
                                                 pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc,
                                                 d3d12_video_encoder_get_current_profile_desc(pD3D12Enc),
                                                 d3d12_video_encoder_get_current_level_desc(pD3D12Enc),
                                                 // resolution list count
                                                 1,
                                                 // resolution list
                                                 &pD3D12Enc->m_currentEncodeConfig.m_currentResolution };

      // Create encoder heap
      HRESULT hr = pD3D12Enc->m_spD3D12VideoDevice->CreateVideoEncoderHeap(&heapDesc,
                                                                           IID_PPV_ARGS(pD3D12Enc->m_spVideoEncoderHeap.GetAddressOf()));
      if (FAILED(hr)) {
         debug_printf("CreateVideoEncoderHeap failed with HR %x\n", hr);
         return false;
      }
   }

   // If on-the-fly reconfiguration happened without object recreation, set
   // D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_*_CHANGED reconfiguration flags in EncodeFrame
   if (rateControlChanged &&
       ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
         D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_RECONFIGURATION_AVAILABLE) !=
        0 /*checking if the flag it's actually set*/) &&
       (pD3D12Enc->m_fenceValue > 1) && (!reCreatedEncoder || !reCreatedEncoderHeap)) {
      pD3D12Enc->m_currentEncodeConfig.m_seqFlags |= D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_RATE_CONTROL_CHANGE;
   }

   if (slicesChanged &&
       ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
         D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SUBREGION_LAYOUT_RECONFIGURATION_AVAILABLE) !=
        0 /*checking if the flag it's actually set*/) &&
       (pD3D12Enc->m_fenceValue > 1) && (!reCreatedEncoder || !reCreatedEncoderHeap)) {
      pD3D12Enc->m_currentEncodeConfig.m_seqFlags |= D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_SUBREGION_LAYOUT_CHANGE;
   }

   if (gopChanged &&
       ((pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags &
         D3D12_VIDEO_ENCODER_SUPPORT_FLAG_SEQUENCE_GOP_RECONFIGURATION_AVAILABLE) !=
        0 /*checking if the flag it's actually set*/) &&
       (pD3D12Enc->m_fenceValue > 1) && (!reCreatedEncoder || !reCreatedEncoderHeap)) {
      pD3D12Enc->m_currentEncodeConfig.m_seqFlags |= D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAG_GOP_SEQUENCE_CHANGE;
   }
   return true;
}

void
d3d12_video_encoder_create_reference_picture_manager(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         bool gopHasPFrames =
            (pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures.PPicturePeriod > 0) &&
            ((pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures.GOPLength == 0) ||
             (pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures.PPicturePeriod <
              pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures.GOPLength));

         pD3D12Enc->m_upDPBManager = std::make_unique<d3d12_video_encoder_references_manager_h264>(
            gopHasPFrames,
            *pD3D12Enc->m_upDPBStorageManager,
            // Max number of frames to be used as a reference, without counting the current recon picture
            d3d12_video_encoder_get_current_max_dpb_capacity(pD3D12Enc)
         );

         pD3D12Enc->m_upBitstreamBuilder = std::make_unique<d3d12_video_bitstream_builder_h264>();
      } break;

      case PIPE_VIDEO_FORMAT_HEVC:
      {
         bool gopHasPFrames =
            (pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_HEVCGroupOfPictures.PPicturePeriod > 0) &&
            ((pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_HEVCGroupOfPictures.GOPLength == 0) ||
             (pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_HEVCGroupOfPictures.PPicturePeriod <
              pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_HEVCGroupOfPictures.GOPLength));

         pD3D12Enc->m_upDPBManager = std::make_unique<d3d12_video_encoder_references_manager_hevc>(
            gopHasPFrames,
            *pD3D12Enc->m_upDPBStorageManager,
            // Max number of frames to be used as a reference, without counting the current recon picture
            d3d12_video_encoder_get_current_max_dpb_capacity(pD3D12Enc)
         );

         pD3D12Enc->m_upBitstreamBuilder = std::make_unique<d3d12_video_bitstream_builder_hevc>();
      } break;

      default:
      {
         unreachable("Unsupported pipe_video_format");
      } break;
   }
}

D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA
d3d12_video_encoder_get_current_slice_param_settings(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA subregionData = {};
         if (pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode !=
             D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME) {
            subregionData.pSlicesPartition_H264 =
               &pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigDesc.m_SlicesPartition_H264;
            subregionData.DataSize = sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES);
         }
         return subregionData;
      } break;

      case PIPE_VIDEO_FORMAT_HEVC:
      {
         D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA subregionData = {};
         if (pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode !=
             D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME) {
            subregionData.pSlicesPartition_HEVC =
               &pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigDesc.m_SlicesPartition_HEVC;
            subregionData.DataSize = sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES);
         }
         return subregionData;
      } break;

      default:
      {
         unreachable("Unsupported pipe_video_format");
      } break;
   }
}

D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA
d3d12_video_encoder_get_current_picture_param_settings(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA curPicParamsData = {};
         curPicParamsData.pH264PicData = &pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_H264PicData;
         curPicParamsData.DataSize     = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_H264PicData);
         return curPicParamsData;
      } break;

      case PIPE_VIDEO_FORMAT_HEVC:
      {
         D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA curPicParamsData = {};
         curPicParamsData.pHEVCPicData = &pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_HEVCPicData;
         curPicParamsData.DataSize     = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderPicParamsDesc.m_HEVCPicData);
         return curPicParamsData;
      } break;

      default:
      {
         unreachable("Unsupported pipe_video_format");
      } break;
   }
}

D3D12_VIDEO_ENCODER_RATE_CONTROL
d3d12_video_encoder_get_current_rate_control_settings(struct d3d12_video_encoder *pD3D12Enc)
{
   D3D12_VIDEO_ENCODER_RATE_CONTROL curRateControlDesc = {};
   curRateControlDesc.Mode            = pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Mode;
   curRateControlDesc.Flags           = pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Flags;
   curRateControlDesc.TargetFrameRate = pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_FrameRate;

   switch (pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Mode) {
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_ABSOLUTE_QP_MAP:
      {
         curRateControlDesc.ConfigParams.pConfiguration_CQP = nullptr;
         curRateControlDesc.ConfigParams.DataSize           = 0;
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CQP:
      {
         curRateControlDesc.ConfigParams.pConfiguration_CQP =
            &pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP;
         curRateControlDesc.ConfigParams.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CQP);
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_CBR:
      {
         curRateControlDesc.ConfigParams.pConfiguration_CBR =
            &pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CBR;
         curRateControlDesc.ConfigParams.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CBR);
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_VBR:
      {
         curRateControlDesc.ConfigParams.pConfiguration_VBR =
            &pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_VBR;
         curRateControlDesc.ConfigParams.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_VBR);
      } break;
      case D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE_QVBR:
      {
         curRateControlDesc.ConfigParams.pConfiguration_QVBR =
            &pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_QVBR;
         curRateControlDesc.ConfigParams.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_QVBR);
      } break;
      default:
      {
         unreachable("Unsupported D3D12_VIDEO_ENCODER_RATE_CONTROL_MODE");
      } break;
   }

   return curRateControlDesc;
}

D3D12_VIDEO_ENCODER_LEVEL_SETTING
d3d12_video_encoder_get_current_level_desc(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_LEVEL_SETTING curLevelDesc = {};
         curLevelDesc.pH264LevelSetting = &pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_H264LevelSetting;
         curLevelDesc.DataSize = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_H264LevelSetting);
         return curLevelDesc;
      } break;

      case PIPE_VIDEO_FORMAT_HEVC:
      {
         D3D12_VIDEO_ENCODER_LEVEL_SETTING curLevelDesc = {};
         curLevelDesc.pHEVCLevelSetting = &pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_HEVCLevelSetting;
         curLevelDesc.DataSize = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderLevelDesc.m_HEVCLevelSetting);
         return curLevelDesc;
      } break;

      default:
      {
         unreachable("Unsupported pipe_video_format");
      } break;
   }
}

uint32_t
d3d12_video_encoder_build_codec_headers(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         return d3d12_video_encoder_build_codec_headers_h264(pD3D12Enc);

      } break;

      case PIPE_VIDEO_FORMAT_HEVC:
      {
         return d3d12_video_encoder_build_codec_headers_hevc(pD3D12Enc);

      } break;

      default:
      {
         unreachable("Unsupported pipe_video_format");
      } break;
   }
   return 0u;
}

D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE
d3d12_video_encoder_get_current_gop_desc(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE curGOPDesc = {};
         curGOPDesc.pH264GroupOfPictures =
            &pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures;
         curGOPDesc.DataSize = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_H264GroupOfPictures);
         return curGOPDesc;
      } break;

      case PIPE_VIDEO_FORMAT_HEVC:
      {
         D3D12_VIDEO_ENCODER_SEQUENCE_GOP_STRUCTURE curGOPDesc = {};
         curGOPDesc.pHEVCGroupOfPictures =
            &pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_HEVCGroupOfPictures;
         curGOPDesc.DataSize = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderGOPConfigDesc.m_HEVCGroupOfPictures);
         return curGOPDesc;
      } break;

      default:
      {
         unreachable("Unsupported pipe_video_format");
      } break;
   }
}

D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION
d3d12_video_encoder_get_current_codec_config_desc(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION codecConfigDesc = {};
         codecConfigDesc.pH264Config = &pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_H264Config;
         codecConfigDesc.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_H264Config);
         return codecConfigDesc;
      } break;

      case PIPE_VIDEO_FORMAT_HEVC:
      {
         D3D12_VIDEO_ENCODER_CODEC_CONFIGURATION codecConfigDesc = {};
         codecConfigDesc.pHEVCConfig = &pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_HEVCConfig;
         codecConfigDesc.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderCodecSpecificConfigDesc.m_HEVCConfig);
         return codecConfigDesc;
      } break;

      default:
      {
         unreachable("Unsupported pipe_video_format");
      } break;
   }
}

D3D12_VIDEO_ENCODER_CODEC
d3d12_video_encoder_get_current_codec(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         return D3D12_VIDEO_ENCODER_CODEC_H264;
      } break;
      case PIPE_VIDEO_FORMAT_HEVC:
      {
         return D3D12_VIDEO_ENCODER_CODEC_HEVC;
      } break;
      default:
      {
         unreachable("Unsupported pipe_video_format");
      } break;
   }
}

///
/// Call d3d12_video_encoder_query_d3d12_driver_caps and see if any optional feature requested
/// is not supported, disable it, query again until finding a negotiated cap/feature set
/// Note that with fallbacks, the upper layer will not get exactly the encoding seetings they requested
/// but for very particular settings it's better to continue with warnings than failing the whole encoding process
///
bool d3d12_video_encoder_negotiate_requested_features_and_d3d12_driver_caps(struct d3d12_video_encoder *pD3D12Enc, D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT &capEncoderSupportData) {

   ///
   /// Check for general support
   /// Check for validation errors (some drivers return general support but also validation errors anyways, work around for those unexpected cases)
   ///

   bool configSupported = d3d12_video_encoder_query_d3d12_driver_caps(pD3D12Enc, /*inout*/ capEncoderSupportData)
    && (((capEncoderSupportData.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_GENERAL_SUPPORT_OK) != 0)
                        && (capEncoderSupportData.ValidationFlags == D3D12_VIDEO_ENCODER_VALIDATION_FLAG_NONE));

   ///
   /// If rate control config is not supported, try falling back and check for caps again
   ///   

   if ((capEncoderSupportData.ValidationFlags & (D3D12_VIDEO_ENCODER_VALIDATION_FLAG_RATE_CONTROL_CONFIGURATION_NOT_SUPPORTED | D3D12_VIDEO_ENCODER_VALIDATION_FLAG_RATE_CONTROL_MODE_NOT_SUPPORTED)) != 0) {

      if (D3D12_VIDEO_ENC_FALLBACK_RATE_CONTROL_CONFIG){ // Check if fallback mode is enabled, or we should just fail without support
      
         debug_printf("[d3d12_video_encoder] WARNING: Requested rate control is not supported, trying fallback to unsetting optional features\n");

         bool isRequestingVBVSizesSupported = ((capEncoderSupportData.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_VBV_SIZE_CONFIG_AVAILABLE) != 0);
         bool isClientRequestingVBVSizes = ((pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Flags & D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES) != 0);
         
         if(isClientRequestingVBVSizes && !isRequestingVBVSizesSupported) {
            debug_printf("[d3d12_video_encoder] WARNING: Requested D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES with VBVCapacity (bits): %" PRIu64 " and InitialVBVFullness (bits) %" PRIu64 " is not supported, will continue encoding unsetting this feature as fallback.\n",
                  pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CBR.VBVCapacity,
                  pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CBR.InitialVBVFullness);

            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Flags &= ~D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_VBV_SIZES;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CBR.VBVCapacity = 0;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_CBR.InitialVBVFullness = 0;
         }
         
         bool isRequestingPeakFrameSizeSupported = ((capEncoderSupportData.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_RATE_CONTROL_MAX_FRAME_SIZE_AVAILABLE) != 0);
         bool isClientRequestingPeakFrameSize = ((pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Flags & D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_MAX_FRAME_SIZE) != 0);

         if(isClientRequestingPeakFrameSize && !isRequestingPeakFrameSizeSupported) {
            debug_printf("[d3d12_video_encoder] WARNING: Requested D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_MAX_FRAME_SIZE with MaxFrameBitSize %" PRIu64 " but the feature is not supported, will continue encoding unsetting this feature as fallback.\n",
               pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_VBR.MaxFrameBitSize);

            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Flags &= ~D3D12_VIDEO_ENCODER_RATE_CONTROL_FLAG_ENABLE_MAX_FRAME_SIZE;
            pD3D12Enc->m_currentEncodeConfig.m_encoderRateControlDesc.m_Config.m_Configuration_VBR.MaxFrameBitSize = 0;
         }

         ///
         /// Try fallback configuration
         ///
         configSupported = d3d12_video_encoder_query_d3d12_driver_caps(pD3D12Enc, /*inout*/ capEncoderSupportData)
          && (((capEncoderSupportData.SupportFlags & D3D12_VIDEO_ENCODER_SUPPORT_FLAG_GENERAL_SUPPORT_OK) != 0)
                           && (capEncoderSupportData.ValidationFlags == D3D12_VIDEO_ENCODER_VALIDATION_FLAG_NONE));

      } else {
         debug_printf("[d3d12_video_encoder] WARNING: Requested rate control is not supported. To continue with a fallback, must enable the OS environment variable D3D12_VIDEO_ENC_FALLBACK_RATE_CONTROL_CONFIG\n");
      }
   }

   if(!configSupported) {
      debug_printf("[d3d12_video_encoder] Cap negotiation failed, see more details below:\n");
      
      if ((capEncoderSupportData.ValidationFlags & D3D12_VIDEO_ENCODER_VALIDATION_FLAG_CODEC_NOT_SUPPORTED) != 0) {
         debug_printf("[d3d12_video_encoder] Requested codec is not supported\n");
      }

      if ((capEncoderSupportData.ValidationFlags &
         D3D12_VIDEO_ENCODER_VALIDATION_FLAG_RESOLUTION_NOT_SUPPORTED_IN_LIST) != 0) {
         debug_printf("[d3d12_video_encoder] Requested resolution is not supported\n");
      }

      if ((capEncoderSupportData.ValidationFlags &
         D3D12_VIDEO_ENCODER_VALIDATION_FLAG_RATE_CONTROL_CONFIGURATION_NOT_SUPPORTED) != 0) {
         debug_printf("[d3d12_video_encoder] Requested bitrate or rc config is not supported\n");
      }

      if ((capEncoderSupportData.ValidationFlags &
         D3D12_VIDEO_ENCODER_VALIDATION_FLAG_CODEC_CONFIGURATION_NOT_SUPPORTED) != 0) {
         debug_printf("[d3d12_video_encoder] Requested codec config is not supported\n");
      }

      if ((capEncoderSupportData.ValidationFlags &
         D3D12_VIDEO_ENCODER_VALIDATION_FLAG_RATE_CONTROL_MODE_NOT_SUPPORTED) != 0) {
         debug_printf("[d3d12_video_encoder] Requested rate control mode is not supported\n");
      }

      if ((capEncoderSupportData.ValidationFlags &
         D3D12_VIDEO_ENCODER_VALIDATION_FLAG_INTRA_REFRESH_MODE_NOT_SUPPORTED) != 0) {
         debug_printf("[d3d12_video_encoder] Requested intra refresh config is not supported\n");
      }

      if ((capEncoderSupportData.ValidationFlags &
         D3D12_VIDEO_ENCODER_VALIDATION_FLAG_SUBREGION_LAYOUT_MODE_NOT_SUPPORTED) != 0) {
         debug_printf("[d3d12_video_encoder] Requested subregion layout mode is not supported\n");
      }

      if ((capEncoderSupportData.ValidationFlags & D3D12_VIDEO_ENCODER_VALIDATION_FLAG_INPUT_FORMAT_NOT_SUPPORTED) !=
         0) {
         debug_printf("[d3d12_video_encoder] Requested input dxgi format is not supported\n");
      }
   }

   return configSupported;
}

bool d3d12_video_encoder_query_d3d12_driver_caps(struct d3d12_video_encoder *pD3D12Enc, D3D12_FEATURE_DATA_VIDEO_ENCODER_SUPPORT &capEncoderSupportData) {
   capEncoderSupportData.NodeIndex                                = pD3D12Enc->m_NodeIndex;
   capEncoderSupportData.Codec                                    = d3d12_video_encoder_get_current_codec(pD3D12Enc);
   capEncoderSupportData.InputFormat            = pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format;
   capEncoderSupportData.RateControl            = d3d12_video_encoder_get_current_rate_control_settings(pD3D12Enc);
   capEncoderSupportData.IntraRefresh           = pD3D12Enc->m_currentEncodeConfig.m_IntraRefresh.Mode;
   capEncoderSupportData.SubregionFrameEncoding = pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode;
   capEncoderSupportData.ResolutionsListCount   = 1;
   capEncoderSupportData.pResolutionList        = &pD3D12Enc->m_currentEncodeConfig.m_currentResolution;
   capEncoderSupportData.CodecGopSequence       = d3d12_video_encoder_get_current_gop_desc(pD3D12Enc);
   capEncoderSupportData.MaxReferenceFramesInDPB = d3d12_video_encoder_get_current_max_dpb_capacity(pD3D12Enc);
   capEncoderSupportData.CodecConfiguration = d3d12_video_encoder_get_current_codec_config_desc(pD3D12Enc);

   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         capEncoderSupportData.SuggestedProfile.pH264Profile =
            &pD3D12Enc->m_currentEncodeCapabilities.m_encoderSuggestedProfileDesc.m_H264Profile;
         capEncoderSupportData.SuggestedProfile.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeCapabilities.m_encoderSuggestedProfileDesc.m_H264Profile);
         capEncoderSupportData.SuggestedLevel.pH264LevelSetting =
            &pD3D12Enc->m_currentEncodeCapabilities.m_encoderLevelSuggestedDesc.m_H264LevelSetting;
         capEncoderSupportData.SuggestedLevel.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeCapabilities.m_encoderLevelSuggestedDesc.m_H264LevelSetting);
      } break;

      case PIPE_VIDEO_FORMAT_HEVC:
      {
         capEncoderSupportData.SuggestedProfile.pHEVCProfile =
            &pD3D12Enc->m_currentEncodeCapabilities.m_encoderSuggestedProfileDesc.m_HEVCProfile;
         capEncoderSupportData.SuggestedProfile.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeCapabilities.m_encoderSuggestedProfileDesc.m_HEVCProfile);
         capEncoderSupportData.SuggestedLevel.pHEVCLevelSetting =
            &pD3D12Enc->m_currentEncodeCapabilities.m_encoderLevelSuggestedDesc.m_HEVCLevelSetting;
         capEncoderSupportData.SuggestedLevel.DataSize =
            sizeof(pD3D12Enc->m_currentEncodeCapabilities.m_encoderLevelSuggestedDesc.m_HEVCLevelSetting);
      } break;

      default:
      {
         unreachable("Unsupported pipe_video_format");
      } break;
   }

   // prepare inout storage for the resolution dependent result.
   capEncoderSupportData.pResolutionDependentSupport =
      &pD3D12Enc->m_currentEncodeCapabilities.m_currentResolutionSupportCaps;

   HRESULT hr = pD3D12Enc->m_spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_SUPPORT,
                                                                         &capEncoderSupportData,
                                                                         sizeof(capEncoderSupportData));
   if (FAILED(hr)) {
      debug_printf("CheckFeatureSupport failed with HR %x\n", hr);
      return false;
   }
   pD3D12Enc->m_currentEncodeCapabilities.m_SupportFlags    = capEncoderSupportData.SupportFlags;
   pD3D12Enc->m_currentEncodeCapabilities.m_ValidationFlags = capEncoderSupportData.ValidationFlags;
   return true;
}

bool d3d12_video_encoder_check_subregion_mode_support(struct d3d12_video_encoder *pD3D12Enc,
                                    D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE requestedSlicesMode
   )
{
   D3D12_FEATURE_DATA_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE capDataSubregionLayout = { }; 
   capDataSubregionLayout.NodeIndex = pD3D12Enc->m_NodeIndex;
   capDataSubregionLayout.Codec = d3d12_video_encoder_get_current_codec(pD3D12Enc);
   capDataSubregionLayout.Profile = d3d12_video_encoder_get_current_profile_desc(pD3D12Enc);
   capDataSubregionLayout.Level = d3d12_video_encoder_get_current_level_desc(pD3D12Enc);
   capDataSubregionLayout.SubregionMode = requestedSlicesMode;
   HRESULT hr = pD3D12Enc->m_spD3D12VideoDevice->CheckFeatureSupport(D3D12_FEATURE_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE, &capDataSubregionLayout, sizeof(capDataSubregionLayout));
   if (FAILED(hr)) {
      debug_printf("CheckFeatureSupport failed with HR %x\n", hr);
      return false;
   }
   return capDataSubregionLayout.IsSupported;
}

D3D12_VIDEO_ENCODER_PROFILE_DESC
d3d12_video_encoder_get_current_profile_desc(struct d3d12_video_encoder *pD3D12Enc)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         D3D12_VIDEO_ENCODER_PROFILE_DESC curProfDesc = {};
         curProfDesc.pH264Profile = &pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_H264Profile;
         curProfDesc.DataSize     = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_H264Profile);
         return curProfDesc;
      } break;

      case PIPE_VIDEO_FORMAT_HEVC:
      {
         D3D12_VIDEO_ENCODER_PROFILE_DESC curProfDesc = {};
         curProfDesc.pHEVCProfile = &pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_HEVCProfile;
         curProfDesc.DataSize     = sizeof(pD3D12Enc->m_currentEncodeConfig.m_encoderProfileDesc.m_HEVCProfile);
         return curProfDesc;
      } break;

      default:
      {
         unreachable("Unsupported pipe_video_format");
      } break;
   }
}

uint32_t
d3d12_video_encoder_get_current_max_dpb_capacity(struct d3d12_video_encoder *pD3D12Enc)
{
   return pD3D12Enc->base.max_references;
}

bool
d3d12_video_encoder_update_current_encoder_config_state(struct d3d12_video_encoder *pD3D12Enc,
                                                        struct pipe_video_buffer *  srcTexture,
                                                        struct pipe_picture_desc *  picture)
{
   enum pipe_video_format codec = u_reduce_video_profile(pD3D12Enc->base.profile);
   switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
      {
         return d3d12_video_encoder_update_current_encoder_config_state_h264(pD3D12Enc, srcTexture, picture);
      } break;

      case PIPE_VIDEO_FORMAT_HEVC:
      {
         return d3d12_video_encoder_update_current_encoder_config_state_hevc(pD3D12Enc, srcTexture, picture);
      } break;

      default:
      {
         unreachable("Unsupported pipe_video_format");
      } break;
   }
}

bool
d3d12_video_encoder_create_command_objects(struct d3d12_video_encoder *pD3D12Enc)
{
   assert(pD3D12Enc->m_spD3D12VideoDevice);

   D3D12_COMMAND_QUEUE_DESC commandQueueDesc = { D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE };
   HRESULT                  hr               = pD3D12Enc->m_pD3D12Screen->dev->CreateCommandQueue(
      &commandQueueDesc,
      IID_PPV_ARGS(pD3D12Enc->m_spEncodeCommandQueue.GetAddressOf()));
   if (FAILED(hr)) {
      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_create_command_objects - Call to CreateCommandQueue "
                      "failed with HR %x\n",
                      hr);
      return false;
   }

   hr = pD3D12Enc->m_pD3D12Screen->dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&pD3D12Enc->m_spFence));
   if (FAILED(hr)) {
      debug_printf(
         "[d3d12_video_encoder] d3d12_video_encoder_create_command_objects - Call to CreateFence failed with HR %x\n",
         hr);
      return false;
   }

   hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE,
      IID_PPV_ARGS(pD3D12Enc->m_spCommandAllocator.GetAddressOf()));
   if (FAILED(hr)) {
      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_create_command_objects - Call to "
                      "CreateCommandAllocator failed with HR %x\n",
                      hr);
      return false;
   }

   hr =
      pD3D12Enc->m_pD3D12Screen->dev->CreateCommandList(0,
                                                        D3D12_COMMAND_LIST_TYPE_VIDEO_ENCODE,
                                                        pD3D12Enc->m_spCommandAllocator.Get(),
                                                        nullptr,
                                                        IID_PPV_ARGS(pD3D12Enc->m_spEncodeCommandList.GetAddressOf()));

   if (FAILED(hr)) {
      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_create_command_objects - Call to CreateCommandList "
                      "failed with HR %x\n",
                      hr);
      return false;
   }

   return true;
}

struct pipe_video_codec *
d3d12_video_encoder_create_encoder(struct pipe_context *context, const struct pipe_video_codec *codec)
{
   ///
   /// Initialize d3d12_video_encoder
   ///

   // Not using new doesn't call ctor and the initializations in the class declaration are lost
   struct d3d12_video_encoder *pD3D12Enc = new d3d12_video_encoder;

   pD3D12Enc->base         = *codec;
   pD3D12Enc->m_screen     = context->screen;
   pD3D12Enc->base.context = context;
   pD3D12Enc->base.width   = codec->width;
   pD3D12Enc->base.height  = codec->height;
   pD3D12Enc->base.max_references  = codec->max_references;
   // Only fill methods that are supported by the d3d12 encoder, leaving null the rest (ie. encode_* / encode_macroblock)
   pD3D12Enc->base.destroy          = d3d12_video_encoder_destroy;
   pD3D12Enc->base.begin_frame      = d3d12_video_encoder_begin_frame;
   pD3D12Enc->base.encode_bitstream = d3d12_video_encoder_encode_bitstream;
   pD3D12Enc->base.end_frame        = d3d12_video_encoder_end_frame;
   pD3D12Enc->base.flush            = d3d12_video_encoder_flush;
   pD3D12Enc->base.get_feedback     = d3d12_video_encoder_get_feedback;

   struct d3d12_context *pD3D12Ctx = (struct d3d12_context *) context;
   pD3D12Enc->m_pD3D12Screen       = d3d12_screen(pD3D12Ctx->base.screen);

   if (FAILED(pD3D12Enc->m_pD3D12Screen->dev->QueryInterface(
          IID_PPV_ARGS(pD3D12Enc->m_spD3D12VideoDevice.GetAddressOf())))) {
      debug_printf(
         "[d3d12_video_encoder] d3d12_video_encoder_create_encoder - D3D12 Device has no Video encode support\n");
      goto failed;
   }

   if (!d3d12_video_encoder_create_command_objects(pD3D12Enc)) {
      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_create_encoder - Failure on "
                      "d3d12_video_encoder_create_command_objects\n");
      goto failed;
   }

   return &pD3D12Enc->base;

failed:
   if (pD3D12Enc != nullptr) {
      d3d12_video_encoder_destroy((struct pipe_video_codec *) pD3D12Enc);
   }

   return nullptr;
}

bool
d3d12_video_encoder_prepare_output_buffers(struct d3d12_video_encoder *pD3D12Enc,
                                           struct pipe_video_buffer *  srcTexture,
                                           struct pipe_picture_desc *  picture)
{
   pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.NodeIndex = pD3D12Enc->m_NodeIndex;
   pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.Codec =
      pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc;
   pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.Profile =
      d3d12_video_encoder_get_current_profile_desc(pD3D12Enc);
   pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.InputFormat =
      pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format;
   pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.PictureTargetResolution =
      pD3D12Enc->m_currentEncodeConfig.m_currentResolution;

   HRESULT hr = pD3D12Enc->m_spD3D12VideoDevice->CheckFeatureSupport(
      D3D12_FEATURE_VIDEO_ENCODER_RESOURCE_REQUIREMENTS,
      &pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps,
      sizeof(pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps));

   if (FAILED(hr)) {
      debug_printf("CheckFeatureSupport failed with HR %x\n", hr);
      return false;
   }

   if (!pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.IsSupported) {
      debug_printf("[d3d12_video_encoder] D3D12_FEATURE_VIDEO_ENCODER_RESOURCE_REQUIREMENTS arguments are not supported.\n");
      return false;
   }

   d3d12_video_encoder_calculate_metadata_resolved_buffer_size(
      pD3D12Enc->m_currentEncodeCapabilities.m_MaxSlicesInOutput,
      pD3D12Enc->m_currentEncodeCapabilities.m_resolvedLayoutMetadataBufferRequiredSize);

   D3D12_HEAP_PROPERTIES Properties = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
   if ((pD3D12Enc->m_spResolvedMetadataBuffer == nullptr) ||
       (GetDesc(pD3D12Enc->m_spResolvedMetadataBuffer.Get()).Width <
        pD3D12Enc->m_currentEncodeCapabilities.m_resolvedLayoutMetadataBufferRequiredSize)) {
      CD3DX12_RESOURCE_DESC resolvedMetadataBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
         pD3D12Enc->m_currentEncodeCapabilities.m_resolvedLayoutMetadataBufferRequiredSize);

      HRESULT hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommittedResource(
         &Properties,
         D3D12_HEAP_FLAG_NONE,
         &resolvedMetadataBufferDesc,
         D3D12_RESOURCE_STATE_COMMON,
         nullptr,
         IID_PPV_ARGS(pD3D12Enc->m_spResolvedMetadataBuffer.GetAddressOf()));

      if (FAILED(hr)) {
         debug_printf("CreateCommittedResource failed with HR %x\n", hr);
         return false;
      }
   }

   if ((pD3D12Enc->m_spMetadataOutputBuffer == nullptr) ||
       (GetDesc(pD3D12Enc->m_spMetadataOutputBuffer.Get()).Width <
        pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.MaxEncoderOutputMetadataBufferSize)) {
      CD3DX12_RESOURCE_DESC metadataBufferDesc = CD3DX12_RESOURCE_DESC::Buffer(
         pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.MaxEncoderOutputMetadataBufferSize);

      HRESULT hr = pD3D12Enc->m_pD3D12Screen->dev->CreateCommittedResource(
         &Properties,
         D3D12_HEAP_FLAG_NONE,
         &metadataBufferDesc,
         D3D12_RESOURCE_STATE_COMMON,
         nullptr,
         IID_PPV_ARGS(pD3D12Enc->m_spMetadataOutputBuffer.GetAddressOf()));

      if (FAILED(hr)) {
         debug_printf("CreateCommittedResource failed with HR %x\n", hr);
         return false;
      }
   }
   return true;
}

bool
d3d12_video_encoder_reconfigure_session(struct d3d12_video_encoder *pD3D12Enc,
                                        struct pipe_video_buffer *  srcTexture,
                                        struct pipe_picture_desc *  picture)
{
   assert(pD3D12Enc->m_spD3D12VideoDevice);
   if(!d3d12_video_encoder_update_current_encoder_config_state(pD3D12Enc, srcTexture, picture)) {
      debug_printf("d3d12_video_encoder_update_current_encoder_config_state failed!\n");
      return false;
   }
   if(!d3d12_video_encoder_reconfigure_encoder_objects(pD3D12Enc, srcTexture, picture)) {
      debug_printf("d3d12_video_encoder_reconfigure_encoder_objects failed!\n");
      return false;
   }
   d3d12_video_encoder_update_picparams_tracking(pD3D12Enc, srcTexture, picture);
   if(!d3d12_video_encoder_prepare_output_buffers(pD3D12Enc, srcTexture, picture)) {
      debug_printf("d3d12_video_encoder_prepare_output_buffers failed!\n");
      return false;
   }
   return true;
}

/**
 * start encoding of a new frame
 */
void
d3d12_video_encoder_begin_frame(struct pipe_video_codec * codec,
                                struct pipe_video_buffer *target,
                                struct pipe_picture_desc *picture)
{
   // Do nothing here. Initialize happens on encoder creation, re-config (if any) happens in
   // d3d12_video_encoder_encode_bitstream
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   assert(pD3D12Enc);
   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_begin_frame started for fenceValue: %d\n",
                 pD3D12Enc->m_fenceValue);

   if (!d3d12_video_encoder_reconfigure_session(pD3D12Enc, target, picture)) {
      debug_printf("[d3d12_video_encoder] d3d12_video_encoder_begin_frame - Failure on "
                      "d3d12_video_encoder_reconfigure_session\n");
      goto fail;
   }

   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_begin_frame finalized for fenceValue: %d\n",
                 pD3D12Enc->m_fenceValue);
   return;

fail:
   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_begin_frame failed for fenceValue: %d\n",
                pD3D12Enc->m_fenceValue);
   assert(false);
}

void
d3d12_video_encoder_calculate_metadata_resolved_buffer_size(uint32_t maxSliceNumber, size_t &bufferSize)
{
   bufferSize = sizeof(D3D12_VIDEO_ENCODER_OUTPUT_METADATA) +
                (maxSliceNumber * sizeof(D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA));
}

// Returns the number of slices that the output will contain for fixed slicing modes
// and the maximum number of slices the output might contain for dynamic slicing modes (eg. max bytes per slice)
uint32_t
d3d12_video_encoder_calculate_max_slices_count_in_output(
   D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE                          slicesMode,
   const D3D12_VIDEO_ENCODER_PICTURE_CONTROL_SUBREGIONS_LAYOUT_DATA_SLICES *slicesConfig,
   uint32_t                                                                 MaxSubregionsNumberFromCaps,
   D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC                              sequenceTargetResolution,
   uint32_t                                                                 SubregionBlockPixelsSize)
{
   uint32_t pic_width_in_subregion_units =
      static_cast<uint32_t>(std::ceil(sequenceTargetResolution.Width / static_cast<double>(SubregionBlockPixelsSize)));
   uint32_t pic_height_in_subregion_units =
      static_cast<uint32_t>(std::ceil(sequenceTargetResolution.Height / static_cast<double>(SubregionBlockPixelsSize)));
   uint32_t total_picture_subregion_units = pic_width_in_subregion_units * pic_height_in_subregion_units;
   uint32_t maxSlices                     = 0u;
   switch (slicesMode) {
      case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_FULL_FRAME:
      {
         maxSlices = 1u;
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_BYTES_PER_SUBREGION:
      {
         maxSlices = MaxSubregionsNumberFromCaps;
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_SQUARE_UNITS_PER_SUBREGION_ROW_UNALIGNED:
      {
         maxSlices = static_cast<uint32_t>(
            std::ceil(total_picture_subregion_units / static_cast<double>(slicesConfig->NumberOfCodingUnitsPerSlice)));
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_ROWS_PER_SUBREGION:
      {
         maxSlices = static_cast<uint32_t>(
            std::ceil(pic_height_in_subregion_units / static_cast<double>(slicesConfig->NumberOfRowsPerSlice)));
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE_UNIFORM_PARTITIONING_SUBREGIONS_PER_FRAME:
      {
         maxSlices = slicesConfig->NumberOfSlicesPerFrame;
      } break;
      default:
      {
         unreachable("Unsupported D3D12_VIDEO_ENCODER_FRAME_SUBREGION_LAYOUT_MODE");
      } break;
   }

   return maxSlices;
}

/**
 * encode a bitstream
 */
void
d3d12_video_encoder_encode_bitstream(struct pipe_video_codec * codec,
                                     struct pipe_video_buffer *source,
                                     struct pipe_resource *    destination,
                                     void **                   feedback)
{
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   assert(pD3D12Enc);
   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_encode_bitstream started for fenceValue: %d\n",
                 pD3D12Enc->m_fenceValue);
   assert(pD3D12Enc->m_spD3D12VideoDevice);
   assert(pD3D12Enc->m_spEncodeCommandQueue);
   assert(pD3D12Enc->m_pD3D12Screen);
   *feedback = &pD3D12Enc->m_fenceValue;

   struct d3d12_video_buffer *pInputVideoBuffer = (struct d3d12_video_buffer *) source;
   assert(pInputVideoBuffer);
   ID3D12Resource *pInputVideoD3D12Res        = d3d12_resource_resource(pInputVideoBuffer->texture);
   uint32_t        inputVideoD3D12Subresource = 0u;

   struct d3d12_resource *pOutputBitstreamBuffer = (struct d3d12_resource *) destination;
   assert(pOutputBitstreamBuffer);
   ID3D12Resource *pOutputBufferD3D12Res = d3d12_resource_resource(pOutputBitstreamBuffer);

   // Make them permanently resident for video use
   d3d12_promote_to_permanent_residency(pD3D12Enc->m_pD3D12Screen, pOutputBitstreamBuffer);
   d3d12_promote_to_permanent_residency(pD3D12Enc->m_pD3D12Screen, pInputVideoBuffer->texture);

   ///
   /// Record Encode operation
   ///

   ///
   /// pInputVideoD3D12Res and pOutputBufferD3D12Res are unwrapped from pipe_resource objects that are passed externally
   /// and could be tracked by pipe_context and have pending ops. Flush any work on them and transition to
   /// D3D12_RESOURCE_STATE_COMMON before issuing work in Video command queue below. After the video work is done in the
   /// GPU, transition back to D3D12_RESOURCE_STATE_COMMON
   ///
   /// Note that unlike the D3D12TranslationLayer codebase, the state tracker here doesn't (yet) have any kind of
   /// multi-queue support, so it wouldn't implicitly synchronize when trying to transition between a graphics op and a
   /// video op.
   ///

   d3d12_transition_resource_state(
      d3d12_context(pD3D12Enc->base.context),
      pInputVideoBuffer->texture,   // d3d12_resource wrapper for pInputVideoD3D12Res
      D3D12_RESOURCE_STATE_COMMON,
      D3D12_TRANSITION_FLAG_INVALIDATE_BINDINGS);
   d3d12_transition_resource_state(d3d12_context(pD3D12Enc->base.context),
                                   pOutputBitstreamBuffer,   // d3d12_resource wrapped for pOutputBufferD3D12Res
                                   D3D12_RESOURCE_STATE_COMMON,
                                   D3D12_TRANSITION_FLAG_INVALIDATE_BINDINGS);
   d3d12_apply_resource_states(d3d12_context(pD3D12Enc->base.context), false);

   d3d12_resource_wait_idle(d3d12_context(pD3D12Enc->base.context),
                            pInputVideoBuffer->texture,
                            false /*wantToWrite*/);
   d3d12_resource_wait_idle(d3d12_context(pD3D12Enc->base.context), pOutputBitstreamBuffer, true /*wantToWrite*/);

   std::vector<D3D12_RESOURCE_BARRIER> rgCurrentFrameStateTransitions = {
      CD3DX12_RESOURCE_BARRIER::Transition(pInputVideoD3D12Res,
                                           D3D12_RESOURCE_STATE_COMMON,
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ),
      CD3DX12_RESOURCE_BARRIER::Transition(pOutputBufferD3D12Res,
                                           D3D12_RESOURCE_STATE_COMMON,
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE),
      CD3DX12_RESOURCE_BARRIER::Transition(pD3D12Enc->m_spMetadataOutputBuffer.Get(),
                                           D3D12_RESOURCE_STATE_COMMON,
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE)
   };

   pD3D12Enc->m_spEncodeCommandList->ResourceBarrier(rgCurrentFrameStateTransitions.size(),
                                                     rgCurrentFrameStateTransitions.data());

   D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE reconPicOutputTextureDesc =
      pD3D12Enc->m_upDPBManager->get_current_frame_recon_pic_output_allocation();
   D3D12_VIDEO_ENCODE_REFERENCE_FRAMES referenceFramesDescriptor =
      pD3D12Enc->m_upDPBManager->get_current_reference_frames();
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAGS picCtrlFlags = D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAG_NONE;

   // Transition DPB reference pictures to read mode
   uint32_t                            maxReferences = d3d12_video_encoder_get_current_max_dpb_capacity(pD3D12Enc);
   std::vector<D3D12_RESOURCE_BARRIER> rgReferenceTransitions(maxReferences);
   if ((referenceFramesDescriptor.NumTexture2Ds > 0) ||
       (pD3D12Enc->m_upDPBManager->is_current_frame_used_as_reference())) {
      rgReferenceTransitions.clear();
      rgReferenceTransitions.reserve(maxReferences);

      // Check if array of textures vs texture array

      if (referenceFramesDescriptor.pSubresources == nullptr) {

         // Array of resources mode for reference pictures

         // Transition all subresources of each reference frame independent resource allocation
         for (uint32_t referenceIdx = 0; referenceIdx < referenceFramesDescriptor.NumTexture2Ds; referenceIdx++) {
            rgReferenceTransitions.push_back(
               CD3DX12_RESOURCE_BARRIER::Transition(referenceFramesDescriptor.ppTexture2Ds[referenceIdx],
                                                    D3D12_RESOURCE_STATE_COMMON,
                                                    D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ));
         }

         // Transition all subresources the output recon pic independent resource allocation
         if (reconPicOutputTextureDesc.pReconstructedPicture != nullptr) {
            picCtrlFlags |= D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAG_USED_AS_REFERENCE_PICTURE;

            rgReferenceTransitions.push_back(
               CD3DX12_RESOURCE_BARRIER::Transition(reconPicOutputTextureDesc.pReconstructedPicture,
                                                    D3D12_RESOURCE_STATE_COMMON,
                                                    D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE));
         }
      } else if (referenceFramesDescriptor.NumTexture2Ds > 0) {

         // texture array mode for reference pictures

         // In Texture array mode, the dpb storage allocator uses the same texture array for all the input
         // reference pics in ppTexture2Ds and also for the pReconstructedPicture output allocations, just different
         // subresources.

         CD3DX12_RESOURCE_DESC referencesTexArrayDesc(GetDesc(referenceFramesDescriptor.ppTexture2Ds[0]));

         for (uint32_t referenceSubresource = 0; referenceSubresource < referencesTexArrayDesc.DepthOrArraySize;
              referenceSubresource++) {

            // all reference frames inputs should be all the same texarray allocation
            assert(referenceFramesDescriptor.ppTexture2Ds[0] ==
                   referenceFramesDescriptor.ppTexture2Ds[referenceSubresource]);

            // the reconpic output should be all the same texarray allocation
            assert(referenceFramesDescriptor.ppTexture2Ds[0] == reconPicOutputTextureDesc.pReconstructedPicture);

            uint32_t MipLevel, PlaneSlice, ArraySlice;
            D3D12DecomposeSubresource(referenceSubresource,
                                      referencesTexArrayDesc.MipLevels,
                                      referencesTexArrayDesc.ArraySize(),
                                      MipLevel,
                                      ArraySlice,
                                      PlaneSlice);

            for (PlaneSlice = 0; PlaneSlice < pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.PlaneCount;
                 PlaneSlice++) {

               uint32_t planeOutputSubresource =
                  referencesTexArrayDesc.CalcSubresource(MipLevel, ArraySlice, PlaneSlice);

               rgReferenceTransitions.push_back(CD3DX12_RESOURCE_BARRIER::Transition(
                  // Always same allocation in texarray mode
                  referenceFramesDescriptor.ppTexture2Ds[0],
                  D3D12_RESOURCE_STATE_COMMON,
                  // If this is the subresource for the reconpic output allocation, transition to ENCODE_WRITE
                  // Otherwise, it's a subresource for an input reference picture, transition to ENCODE_READ
                  (referenceSubresource == reconPicOutputTextureDesc.ReconstructedPictureSubresource) ?
                     D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE :
                     D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ,
                  planeOutputSubresource));
            }
         }
      }

      if (rgReferenceTransitions.size() > 0) {
         pD3D12Enc->m_spEncodeCommandList->ResourceBarrier(static_cast<uint32_t>(rgReferenceTransitions.size()),
                                                           rgReferenceTransitions.data());
      }
   }

   // Update current frame pic params state after reconfiguring above.
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA currentPicParams =
      d3d12_video_encoder_get_current_picture_param_settings(pD3D12Enc);
   pD3D12Enc->m_upDPBManager->get_current_frame_picture_control_data(currentPicParams);

   uint32_t prefixGeneratedHeadersByteSize = d3d12_video_encoder_build_codec_headers(pD3D12Enc);

   // If driver needs offset alignment for bitstream resource, we will pad zeroes on the codec header to this end.
   if (
      (pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.CompressedBitstreamBufferAccessAlignment > 1)
      && ((prefixGeneratedHeadersByteSize % pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.CompressedBitstreamBufferAccessAlignment) != 0)
   ) {
      prefixGeneratedHeadersByteSize = ALIGN(prefixGeneratedHeadersByteSize, pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.CompressedBitstreamBufferAccessAlignment);
      pD3D12Enc->m_BitstreamHeadersBuffer.resize(prefixGeneratedHeadersByteSize, 0);
   }

   const D3D12_VIDEO_ENCODER_ENCODEFRAME_INPUT_ARGUMENTS inputStreamArguments = {
      // D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_DESC
      { // D3D12_VIDEO_ENCODER_SEQUENCE_CONTROL_FLAGS
        pD3D12Enc->m_currentEncodeConfig.m_seqFlags,
        // D3D12_VIDEO_ENCODER_INTRA_REFRESH
        pD3D12Enc->m_currentEncodeConfig.m_IntraRefresh,
        d3d12_video_encoder_get_current_rate_control_settings(pD3D12Enc),
        // D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC
        pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
        pD3D12Enc->m_currentEncodeConfig.m_encoderSliceConfigMode,
        d3d12_video_encoder_get_current_slice_param_settings(pD3D12Enc),
        d3d12_video_encoder_get_current_gop_desc(pD3D12Enc) },
      // D3D12_VIDEO_ENCODER_PICTURE_CONTROL_DESC
      { // uint32_t IntraRefreshFrameIndex;
        pD3D12Enc->m_currentEncodeConfig.m_IntraRefreshCurrentFrameIndex,
        // D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAGS Flags;
        picCtrlFlags,
        // D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA PictureControlCodecData;
        currentPicParams,
        // D3D12_VIDEO_ENCODE_REFERENCE_FRAMES ReferenceFrames;
        referenceFramesDescriptor },
      pInputVideoD3D12Res,
      inputVideoD3D12Subresource,
      prefixGeneratedHeadersByteSize   // hint for driver to know header size in final bitstream for rate control internal
      // budgeting. - User can also calculate headers fixed size beforehand (eg. no VUI,
      // etc) and build them with final values after EncodeFrame is executed
   };

   const D3D12_VIDEO_ENCODER_ENCODEFRAME_OUTPUT_ARGUMENTS outputStreamArguments = {
      // D3D12_VIDEO_ENCODER_COMPRESSED_BITSTREAM
      {
         pOutputBufferD3D12Res,
         prefixGeneratedHeadersByteSize,   // Start writing after the reserved interval [0,
                                           // prefixGeneratedHeadersByteSize) for bitstream headers
      },
      // D3D12_VIDEO_ENCODER_RECONSTRUCTED_PICTURE
      reconPicOutputTextureDesc,
      // D3D12_VIDEO_ENCODER_ENCODE_OPERATION_METADATA_BUFFER
      { pD3D12Enc->m_spMetadataOutputBuffer.Get(), 0 }
   };

   // Upload the CPU buffers with the bitstream headers to the compressed bitstream resource in the interval [0,
   // prefixGeneratedHeadersByteSize)
   assert(prefixGeneratedHeadersByteSize == pD3D12Enc->m_BitstreamHeadersBuffer.size());

   pD3D12Enc->base.context->buffer_subdata(
      pD3D12Enc->base.context,   // context
      destination,               // dst buffer - "destination" is the pipe_resource object
                                 // wrapping pOutputBitstreamBuffer and eventually pOutputBufferD3D12Res
      PIPE_MAP_WRITE,            // usage PIPE_MAP_x
      0,                         // offset
      pD3D12Enc->m_BitstreamHeadersBuffer.size(),
      pD3D12Enc->m_BitstreamHeadersBuffer.data());

   // Note: The buffer_subdata is queued in pD3D12Enc->base.context but doesn't execute immediately
   // Will flush and sync this batch in d3d12_video_encoder_flush with the rest of the Video Encode Queue GPU work

   // Record EncodeFrame
   pD3D12Enc->m_spEncodeCommandList->EncodeFrame(pD3D12Enc->m_spVideoEncoder.Get(),
                                                 pD3D12Enc->m_spVideoEncoderHeap.Get(),
                                                 &inputStreamArguments,
                                                 &outputStreamArguments);

   D3D12_RESOURCE_BARRIER rgResolveMetadataStateTransitions[] = {
      CD3DX12_RESOURCE_BARRIER::Transition(pD3D12Enc->m_spResolvedMetadataBuffer.Get(),
                                           D3D12_RESOURCE_STATE_COMMON,
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE),
      CD3DX12_RESOURCE_BARRIER::Transition(pD3D12Enc->m_spMetadataOutputBuffer.Get(),
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE,
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ),
      CD3DX12_RESOURCE_BARRIER::Transition(pInputVideoD3D12Res,
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ,
                                           D3D12_RESOURCE_STATE_COMMON),
      CD3DX12_RESOURCE_BARRIER::Transition(pOutputBufferD3D12Res,
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE,
                                           D3D12_RESOURCE_STATE_COMMON)
   };

   pD3D12Enc->m_spEncodeCommandList->ResourceBarrier(_countof(rgResolveMetadataStateTransitions),
                                                     rgResolveMetadataStateTransitions);

   const D3D12_VIDEO_ENCODER_RESOLVE_METADATA_INPUT_ARGUMENTS inputMetadataCmd = {
      pD3D12Enc->m_currentEncodeConfig.m_encoderCodecDesc,
      d3d12_video_encoder_get_current_profile_desc(pD3D12Enc),
      pD3D12Enc->m_currentEncodeConfig.m_encodeFormatInfo.Format,
      // D3D12_VIDEO_ENCODER_PICTURE_RESOLUTION_DESC
      pD3D12Enc->m_currentEncodeConfig.m_currentResolution,
      { pD3D12Enc->m_spMetadataOutputBuffer.Get(), 0 }
   };

   const D3D12_VIDEO_ENCODER_RESOLVE_METADATA_OUTPUT_ARGUMENTS outputMetadataCmd = {
      /*If offset were to change, has to be aligned to pD3D12Enc->m_currentEncodeCapabilities.m_ResourceRequirementsCaps.EncoderMetadataBufferAccessAlignment*/
      { pD3D12Enc->m_spResolvedMetadataBuffer.Get(), 0 }
   };
   pD3D12Enc->m_spEncodeCommandList->ResolveEncoderOutputMetadata(&inputMetadataCmd, &outputMetadataCmd);

   // Transition DPB reference pictures back to COMMON
   if ((referenceFramesDescriptor.NumTexture2Ds > 0) ||
       (pD3D12Enc->m_upDPBManager->is_current_frame_used_as_reference())) {
      for (auto &BarrierDesc : rgReferenceTransitions) {
         std::swap(BarrierDesc.Transition.StateBefore, BarrierDesc.Transition.StateAfter);
      }

      if (rgReferenceTransitions.size() > 0) {
         pD3D12Enc->m_spEncodeCommandList->ResourceBarrier(static_cast<uint32_t>(rgReferenceTransitions.size()),
                                                           rgReferenceTransitions.data());
      }
   }

   D3D12_RESOURCE_BARRIER rgRevertResolveMetadataStateTransitions[] = {
      CD3DX12_RESOURCE_BARRIER::Transition(pD3D12Enc->m_spResolvedMetadataBuffer.Get(),
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_WRITE,
                                           D3D12_RESOURCE_STATE_COMMON),
      CD3DX12_RESOURCE_BARRIER::Transition(pD3D12Enc->m_spMetadataOutputBuffer.Get(),
                                           D3D12_RESOURCE_STATE_VIDEO_ENCODE_READ,
                                           D3D12_RESOURCE_STATE_COMMON),
   };

   pD3D12Enc->m_spEncodeCommandList->ResourceBarrier(_countof(rgRevertResolveMetadataStateTransitions),
                                                     rgRevertResolveMetadataStateTransitions);

   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_encode_bitstream finalized for fenceValue: %d\n",
                 pD3D12Enc->m_fenceValue);
}

void
d3d12_video_encoder_get_feedback(struct pipe_video_codec *codec, void *feedback, unsigned *size)
{
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   assert(pD3D12Enc);

   if (pD3D12Enc->m_needsGPUFlush) {
      d3d12_video_encoder_flush(codec);
   }

   D3D12_VIDEO_ENCODER_OUTPUT_METADATA                       encoderMetadata;
   std::vector<D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA> pSubregionsMetadata;
   d3d12_video_encoder_extract_encode_metadata(
      pD3D12Enc,
      pD3D12Enc->m_spResolvedMetadataBuffer.Get(),
      pD3D12Enc->m_currentEncodeCapabilities.m_resolvedLayoutMetadataBufferRequiredSize,
      encoderMetadata,
      pSubregionsMetadata);

   // Read metadata from encoderMetadata
   if (encoderMetadata.EncodeErrorFlags != D3D12_VIDEO_ENCODER_ENCODE_ERROR_FLAG_NO_ERROR) {
      debug_printf("[d3d12_video_encoder] Encode GPU command failed - EncodeErrorFlags: %" PRIu64 "\n",
                      encoderMetadata.EncodeErrorFlags);
      *size = 0;
   }

   assert(encoderMetadata.EncodedBitstreamWrittenBytesCount > 0u);
   *size = (pD3D12Enc->m_BitstreamHeadersBuffer.size() + encoderMetadata.EncodedBitstreamWrittenBytesCount);
}

void
d3d12_video_encoder_extract_encode_metadata(
   struct d3d12_video_encoder *                               pD3D12Enc,
   ID3D12Resource *                                           pResolvedMetadataBuffer,   // input
   size_t                                                     resourceMetadataSize,      // input
   D3D12_VIDEO_ENCODER_OUTPUT_METADATA &                      parsedMetadata,            // output
   std::vector<D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA> &pSubregionsMetadata        // output
)
{
   struct d3d12_screen *pD3D12Screen = (struct d3d12_screen *) pD3D12Enc->m_pD3D12Screen;
   assert(pD3D12Screen);
   pipe_resource *pPipeResolvedMetadataBuffer =
      d3d12_resource_from_resource(&pD3D12Screen->base, pResolvedMetadataBuffer);
   assert(pPipeResolvedMetadataBuffer);
   assert(resourceMetadataSize < INT_MAX);
   struct pipe_box box = {
      0,                                        // x
      0,                                        // y
      0,                                        // z
      static_cast<int>(resourceMetadataSize),   // width
      1,                                        // height
      1                                         // depth
   };
   struct pipe_transfer *mapTransfer;
   unsigned mapUsage = PIPE_MAP_READ;
   void *                pMetadataBufferSrc = pD3D12Enc->base.context->buffer_map(pD3D12Enc->base.context,
                                                                  pPipeResolvedMetadataBuffer,
                                                                  0,
                                                                  mapUsage,
                                                                  &box,
                                                                  &mapTransfer);

   assert(mapUsage & PIPE_MAP_READ);
   assert(pPipeResolvedMetadataBuffer->usage == PIPE_USAGE_DEFAULT);
   // Note: As we're calling buffer_map with PIPE_MAP_READ on a pPipeResolvedMetadataBuffer which has pipe_usage_default
   // buffer_map itself will do all the synchronization and waits so once the function returns control here
   // the contents of mapTransfer are ready to be accessed.

   // Clear output
   memset(&parsedMetadata, 0, sizeof(D3D12_VIDEO_ENCODER_OUTPUT_METADATA));

   // Calculate sizes
   size_t encoderMetadataSize = sizeof(D3D12_VIDEO_ENCODER_OUTPUT_METADATA);

   // Copy buffer to the appropriate D3D12_VIDEO_ENCODER_OUTPUT_METADATA memory layout
   parsedMetadata = *reinterpret_cast<D3D12_VIDEO_ENCODER_OUTPUT_METADATA *>(pMetadataBufferSrc);

   // As specified in D3D12 Encode spec, the array base for metadata for the slices
   // (D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA[]) is placed in memory immediately after the
   // D3D12_VIDEO_ENCODER_OUTPUT_METADATA structure
   D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA *pFrameSubregionMetadata =
      reinterpret_cast<D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA *>(reinterpret_cast<uint8_t *>(pMetadataBufferSrc) +
                                                                       encoderMetadataSize);

   // Copy fields into D3D12_VIDEO_ENCODER_FRAME_SUBREGION_METADATA
   assert(parsedMetadata.WrittenSubregionsCount < SIZE_MAX);
   pSubregionsMetadata.resize(static_cast<size_t>(parsedMetadata.WrittenSubregionsCount));
   for (uint32_t sliceIdx = 0; sliceIdx < parsedMetadata.WrittenSubregionsCount; sliceIdx++) {
      pSubregionsMetadata[sliceIdx].bHeaderSize  = pFrameSubregionMetadata[sliceIdx].bHeaderSize;
      pSubregionsMetadata[sliceIdx].bSize        = pFrameSubregionMetadata[sliceIdx].bSize;
      pSubregionsMetadata[sliceIdx].bStartOffset = pFrameSubregionMetadata[sliceIdx].bStartOffset;
   }

   // Unmap the buffer tmp storage
   pipe_buffer_unmap(pD3D12Enc->base.context, mapTransfer);
   pipe_resource_reference(&pPipeResolvedMetadataBuffer, NULL);
}

/**
 * end encoding of the current frame
 */
void
d3d12_video_encoder_end_frame(struct pipe_video_codec * codec,
                              struct pipe_video_buffer *target,
                              struct pipe_picture_desc *picture)
{
   struct d3d12_video_encoder *pD3D12Enc = (struct d3d12_video_encoder *) codec;
   assert(pD3D12Enc);
   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_end_frame started for fenceValue: %d\n",
                 pD3D12Enc->m_fenceValue);

   // Signal finish of current frame encoding to the picture management tracker
   pD3D12Enc->m_upDPBManager->end_frame();

   debug_printf("[d3d12_video_encoder] d3d12_video_encoder_end_frame finalized for fenceValue: %d\n",
                 pD3D12Enc->m_fenceValue);

   ///
   /// Flush work to the GPU and blocking wait until encode finishes
   ///
   pD3D12Enc->m_needsGPUFlush = true;
   d3d12_video_encoder_flush(codec);
}
