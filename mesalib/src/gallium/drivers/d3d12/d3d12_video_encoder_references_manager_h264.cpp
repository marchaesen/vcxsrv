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

#include "d3d12_video_encoder_references_manager_h264.h"
#include <algorithm>
#include <string>
#include "d3d12_screen.h"
#include "d3d12_video_buffer.h"
#include "d3d12_resource.h"

using namespace std;

bool
d3d12_video_encoder_references_manager_h264::get_current_frame_picture_control_data(
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA &codecAllocation)
{
   assert(codecAllocation.DataSize == sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264));
   if (codecAllocation.DataSize != sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264))
      return false;

   *codecAllocation.pH264PicData = m_curFrameState;

   return true;
}

D3D12_VIDEO_ENCODE_REFERENCE_FRAMES
d3d12_video_encoder_references_manager_h264::get_current_reference_frames()
{
   D3D12_VIDEO_ENCODE_REFERENCE_FRAMES retVal = { 0,
                                                  // ppTexture2Ds
                                                  nullptr,
                                                  // pSubresources
                                                  nullptr };

   // Return nullptr for fully intra frames (eg IDR)
   // and return references information for inter frames (eg.P/B) and I frame that doesn't flush DPB

   if ((m_curFrameState.FrameType != D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_IDR_FRAME) &&
       (m_curFrameState.FrameType != D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_I_FRAME)) {
      retVal.NumTexture2Ds = m_CurrentFrameReferencesData.ReferenceTextures.pResources.size();
      retVal.ppTexture2Ds = m_CurrentFrameReferencesData.ReferenceTextures.pResources.data();

      // D3D12 Encode expects null subresources for AoT
      bool isAoT = (std::all_of(m_CurrentFrameReferencesData.ReferenceTextures.pSubresources.begin(),
                                m_CurrentFrameReferencesData.ReferenceTextures.pSubresources.end(),
                                [](UINT i) { return i == 0; }));
      retVal.pSubresources = isAoT ? nullptr : m_CurrentFrameReferencesData.ReferenceTextures.pSubresources.data();
   }

   return retVal;
}

static const char *
d3d12_video_encoder_friendly_frame_type_h264(D3D12_VIDEO_ENCODER_FRAME_TYPE_H264 picType)
{
   switch (picType) {
      case D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_P_FRAME:
      {
         return "H264_P_FRAME";
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME:
      {
         return "H264_B_FRAME";
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_I_FRAME:
      {
         return "H264_I_FRAME";
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_IDR_FRAME:
      {
         return "H264_IDR_FRAME";
      } break;
      default:
      {
         unreachable("Unsupported pipe_h2645_enc_picture_type");
      } break;
   }
}

void
d3d12_video_encoder_references_manager_h264::print_l0_l1_lists()
{
   debug_printf(
      "[D3D12 Video Encoder Picture Manager H264] L0 (%d entries) and L1 (%d entries) lists for frame with POC "
      "%d (frame_num: %d) and frame_type %s are:\n",
      m_curFrameState.List0ReferenceFramesCount,
      m_curFrameState.List1ReferenceFramesCount,
      m_curFrameState.PictureOrderCountNumber,
      m_curFrameState.FrameDecodingOrderNumber,
      d3d12_video_encoder_friendly_frame_type_h264(m_curFrameState.FrameType));

   if ((D3D12_DEBUG_VERBOSE & d3d12_debug) &&
       ((m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_P_FRAME) ||
        (m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME))) {
      std::string list0ContentsString;
      for (uint32_t idx = 0; idx < m_curFrameState.List0ReferenceFramesCount; idx++) {
         uint32_t value = m_curFrameState.pList0ReferenceFrames[idx];
         list0ContentsString += "{ DPBidx: ";
         list0ContentsString += std::to_string(value);
         list0ContentsString += " - POC: ";
         list0ContentsString += std::to_string(
            m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[value].PictureOrderCountNumber);
         list0ContentsString += " - FrameDecodingOrderNumber: ";
         list0ContentsString += std::to_string(
            m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[value].FrameDecodingOrderNumber);
         list0ContentsString += "}\n";
      }

      debug_printf("[D3D12 Video Encoder Picture Manager H264] L0 list (%d entries) for frame with POC %d - frame_num "
                   "(%d) is: \n %s \n",
                   m_curFrameState.List0ReferenceFramesCount,
                   m_curFrameState.PictureOrderCountNumber,
                   m_curFrameState.FrameDecodingOrderNumber,
                   list0ContentsString.c_str());

      std::string modificationOrderList0ContentsString;
      for (uint32_t idx = 0; idx < m_curFrameState.List0RefPicModificationsCount; idx++) {
         D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264_REFERENCE_PICTURE_LIST_MODIFICATION_OPERATION value =
            m_curFrameState.pList0RefPicModifications[idx];
         modificationOrderList0ContentsString += "{ modification_of_pic_nums_idc: ";
         modificationOrderList0ContentsString += std::to_string(value.modification_of_pic_nums_idc);
         modificationOrderList0ContentsString += " - abs_diff_pic_num_minus1: ";
         modificationOrderList0ContentsString += std::to_string(value.abs_diff_pic_num_minus1);
         modificationOrderList0ContentsString += " - long_term_pic_num: ";
         modificationOrderList0ContentsString += std::to_string(value.long_term_pic_num);
         modificationOrderList0ContentsString += "}\n";
      }
      debug_printf("[D3D12 Video Encoder Picture Manager H264] L0 modification list (%d entries) for frame with POC %d "
                   "- frame_num "
                   "(%d) temporal_id (%d) is: \n %s \n",
                   m_curFrameState.List0RefPicModificationsCount,
                   m_curFrameState.PictureOrderCountNumber,
                   m_curFrameState.FrameDecodingOrderNumber,
                   m_curFrameState.TemporalLayerIndex,
                   modificationOrderList0ContentsString.c_str());

      std::string list1ContentsString;
      for (uint32_t idx = 0; idx < m_curFrameState.List1ReferenceFramesCount; idx++) {
         uint32_t value = m_curFrameState.pList1ReferenceFrames[idx];
         list1ContentsString += "{ DPBidx: ";
         list1ContentsString += std::to_string(value);
         list1ContentsString += " - POC: ";
         list1ContentsString += std::to_string(
            m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[value].PictureOrderCountNumber);
         list1ContentsString += " - FrameDecodingOrderNumber: ";
         list1ContentsString += std::to_string(
            m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[value].FrameDecodingOrderNumber);
         list1ContentsString += "}\n";
      }

      debug_printf("[D3D12 Video Encoder Picture Manager H264] L1 list (%d entries) for frame with POC %d - frame_num "
                   "(%d) is: \n %s \n",
                   m_curFrameState.List1ReferenceFramesCount,
                   m_curFrameState.PictureOrderCountNumber,
                   m_curFrameState.FrameDecodingOrderNumber,
                   list1ContentsString.c_str());

      std::string modificationOrderList1ContentsString;
      for (uint32_t idx = 0; idx < m_curFrameState.List1RefPicModificationsCount; idx++) {
         D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264_REFERENCE_PICTURE_LIST_MODIFICATION_OPERATION value =
            m_curFrameState.pList1RefPicModifications[idx];
         modificationOrderList1ContentsString += "{ modification_of_pic_nums_idc: ";
         modificationOrderList1ContentsString += std::to_string(value.modification_of_pic_nums_idc);
         modificationOrderList1ContentsString += " - abs_diff_pic_num_minus1: ";
         modificationOrderList1ContentsString += std::to_string(value.abs_diff_pic_num_minus1);
         modificationOrderList1ContentsString += " - long_term_pic_num: ";
         modificationOrderList1ContentsString += std::to_string(value.long_term_pic_num);
         modificationOrderList1ContentsString += "}\n";
      }

      debug_printf("[D3D12 Video Encoder Picture Manager H264] L1 modification list (%d entries) for frame with POC %d "
                   "- frame_num "
                   "(%d) temporal_id (%d) is: \n %s \n",
                   m_curFrameState.List1RefPicModificationsCount,
                   m_curFrameState.PictureOrderCountNumber,
                   m_curFrameState.FrameDecodingOrderNumber,
                   m_curFrameState.TemporalLayerIndex,
                   modificationOrderList1ContentsString.c_str());
   }
}

void
d3d12_video_encoder_references_manager_h264::print_dpb()
{
   if (D3D12_DEBUG_VERBOSE & d3d12_debug) {
      std::string dpbContents;
      for (uint32_t dpbResIdx = 0;
           dpbResIdx < m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size();
           dpbResIdx++) {
         auto &dpbDesc = m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[dpbResIdx];

         dpbContents += "{ DPBidx: ";
         dpbContents += std::to_string(dpbResIdx);

         if (dpbDesc.PictureOrderCountNumber == m_curFrameState.PictureOrderCountNumber) {
            dpbContents += " - CURRENT FRAME RECON PIC ";
         }

         dpbContents += " - POC: ";
         dpbContents += std::to_string(dpbDesc.PictureOrderCountNumber);
         dpbContents += " - FrameDecodingOrderNumber: ";
         dpbContents += std::to_string(dpbDesc.FrameDecodingOrderNumber);
         dpbContents += " - DPBStorageIdx: ";
         dpbContents += std::to_string(dpbDesc.ReconstructedPictureResourceIndex);
         dpbContents += " - DPBStorageResourcePtr: ";
         char strBuf[256];
         memset(&strBuf, '\0', 256);
         sprintf(strBuf,
                 "%p",
                 m_CurrentFrameReferencesData.ReferenceTextures.pResources[dpbDesc.ReconstructedPictureResourceIndex]);
         dpbContents += std::string(strBuf);
         dpbContents += " - DPBStorageSubresource: ";
         dpbContents += std::to_string(
            m_CurrentFrameReferencesData.ReferenceTextures.pSubresources[dpbDesc.ReconstructedPictureResourceIndex]);
         dpbContents += "}\n";
      }

      debug_printf("[D3D12 Video Encoder Picture Manager H264] DPB has %d frames - DPB references for frame with POC "
                   "%d (frame_num: %d) are: \n %s \n",
                   static_cast<UINT>(m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size()),
                   m_curFrameState.PictureOrderCountNumber,
                   m_curFrameState.FrameDecodingOrderNumber,
                   dpbContents.c_str());
   }
}

static D3D12_VIDEO_ENCODER_FRAME_TYPE_H264
d3d12_video_encoder_convert_frame_type_h264(enum pipe_h2645_enc_picture_type picType)
{
   switch (picType) {
      case PIPE_H2645_ENC_PICTURE_TYPE_P:
      {
         return D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_P_FRAME;
      } break;
      case PIPE_H2645_ENC_PICTURE_TYPE_B:
      {
         return D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME;
      } break;
      case PIPE_H2645_ENC_PICTURE_TYPE_I:
      {
         return D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_I_FRAME;
      } break;
      case PIPE_H2645_ENC_PICTURE_TYPE_IDR:
      {
         return D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_IDR_FRAME;
      } break;
      default:
      {
         unreachable("Unsupported pipe_h2645_enc_picture_type");
      } break;
   }
}

void
d3d12_video_encoder_references_manager_h264::begin_frame(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA curFrameData,
                                                         bool bUsedAsReference,
                                                         struct pipe_picture_desc *picture)
{
   m_curFrameState = *curFrameData.pH264PicData;
   m_isCurrentFrameUsedAsReference = bUsedAsReference;

   struct pipe_h264_enc_picture_desc *h264Pic = (struct pipe_h264_enc_picture_desc *) picture;

   ///
   /// Copy DPB snapshot from pipe params
   ///

   m_curFrameState.ReferenceFramesReconPictureDescriptorsCount =
      static_cast<uint32_t>(m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size());
   m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.resize(h264Pic->dpb_size);
   m_CurrentFrameReferencesData.ReferenceTextures.pResources.resize(h264Pic->dpb_size);
   m_CurrentFrameReferencesData.ReferenceTextures.pSubresources.resize(h264Pic->dpb_size);
   m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.resize(h264Pic->dpb_size);
   for (uint8_t i = 0; i < h264Pic->dpb_size; i++) {
      //
      // Set entry DPB members
      //
      m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[i].FrameDecodingOrderNumber =
         h264Pic->dpb[i].frame_idx;
      m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[i].IsLongTermReference =
         h264Pic->dpb[i].is_ltr;
      m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[i].LongTermPictureIdx =
         h264Pic->dpb[i].is_ltr ? h264Pic->dpb[i].frame_idx : 0u;
      m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[i].PictureOrderCountNumber =
         h264Pic->dpb[i].pic_order_cnt;
      // mirror indices between DPB entries and allocation arrays
      m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[i].ReconstructedPictureResourceIndex = i;
      m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[i].TemporalLayerIndex =
         h264Pic->dpb[i].temporal_id;

      //
      // Set texture allocations
      //

      struct d3d12_video_buffer *vidbuf = (struct d3d12_video_buffer *) h264Pic->dpb[i].buffer;
      m_CurrentFrameReferencesData.ReferenceTextures.pResources[i] = d3d12_resource_resource(vidbuf->texture);
      m_CurrentFrameReferencesData.ReferenceTextures.pSubresources[i] = vidbuf->idx_texarray_slots;

      if (h264Pic->dpb[i].pic_order_cnt == h264Pic->pic_order_cnt) {
         m_CurrentFrameReferencesData.ReconstructedPicTexture.pReconstructedPicture =
            m_CurrentFrameReferencesData.ReferenceTextures.pResources[i];
         m_CurrentFrameReferencesData.ReconstructedPicTexture.ReconstructedPictureSubresource =
            m_CurrentFrameReferencesData.ReferenceTextures.pSubresources[i];
      }
   }

   ///
   /// Set pic control info
   ///

   m_curFrameState.idr_pic_id = h264Pic->idr_pic_id;
   m_curFrameState.FrameType = d3d12_video_encoder_convert_frame_type_h264(h264Pic->picture_type);
   m_curFrameState.PictureOrderCountNumber = h264Pic->pic_order_cnt;
   m_curFrameState.FrameDecodingOrderNumber = h264Pic->slice.frame_num;

   ///
   /// Set MMCO info
   ///

   // Deep Copy MMCO list
   m_curFrameState.pRefPicMarkingOperationsCommands = nullptr;
   m_curFrameState.RefPicMarkingOperationsCommandsCount = 0u;
   m_curFrameState.adaptive_ref_pic_marking_mode_flag = 0u;

   if (m_curFrameState.FrameType != D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_IDR_FRAME) {

      // Only send mmco ops to IHV driver on non-idr frames since dec_ref_pic_marking() in the IDR slice headers doesn't
      // have the memory operation list coded in the bitstream
      m_curFrameState.adaptive_ref_pic_marking_mode_flag = h264Pic->slice.adaptive_ref_pic_marking_mode_flag;
      if (m_curFrameState.adaptive_ref_pic_marking_mode_flag) {
         m_curFrameState.RefPicMarkingOperationsCommandsCount = h264Pic->slice.num_ref_pic_marking_operations;
         m_CurrentFrameReferencesData.pMemoryOps.resize(m_curFrameState.RefPicMarkingOperationsCommandsCount);
         for (unsigned i = 0; i < m_curFrameState.RefPicMarkingOperationsCommandsCount; i++) {
            m_CurrentFrameReferencesData.pMemoryOps[i].difference_of_pic_nums_minus1 =
               h264Pic->slice.ref_pic_marking_operations[i].difference_of_pic_nums_minus1;
            m_CurrentFrameReferencesData.pMemoryOps[i].long_term_frame_idx =
               h264Pic->slice.ref_pic_marking_operations[i].long_term_frame_idx;
            m_CurrentFrameReferencesData.pMemoryOps[i].long_term_pic_num =
               h264Pic->slice.ref_pic_marking_operations[i].long_term_pic_num;
            m_CurrentFrameReferencesData.pMemoryOps[i].max_long_term_frame_idx_plus1 =
               h264Pic->slice.ref_pic_marking_operations[i].max_long_term_frame_idx_plus1;
            m_CurrentFrameReferencesData.pMemoryOps[i].memory_management_control_operation =
               h264Pic->slice.ref_pic_marking_operations[i].memory_management_control_operation;
         }

         // DX12 driver requires "End memory_management_control_operation syntax element loop" to be
         // sent at the end of the list for coding the slice header when sending down mmco commands
         if ((m_curFrameState.RefPicMarkingOperationsCommandsCount > 0) &&
             m_CurrentFrameReferencesData.pMemoryOps[m_curFrameState.RefPicMarkingOperationsCommandsCount - 1]
                   .memory_management_control_operation != 0) {

            // Add it if the frontend didn't send it
            m_curFrameState.RefPicMarkingOperationsCommandsCount++;
            D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264_REFERENCE_PICTURE_MARKING_OPERATION endMMCOOperation = {};
            endMMCOOperation.memory_management_control_operation = 0u;
            m_CurrentFrameReferencesData.pMemoryOps.push_back(endMMCOOperation);
         }

         m_curFrameState.pRefPicMarkingOperationsCommands = m_CurrentFrameReferencesData.pMemoryOps.data();
      }
   } else if (h264Pic->slice.long_term_reference_flag) {
      assert(m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_IDR_FRAME);
      // See https://microsoft.github.io/DirectX-Specs/d3d/D3D12VideoEncoding.html
      // Note that for marking an IDR frame as long term reference, the proposed explicit mechanism is to mark it as
      // short term reference first, by setting D3D12_VIDEO_ENCODER_PICTURE_CONTROL_FLAG_USED_AS_REFERENCE_PICTURE when
      // calling EncodeFrame for such IDR frame, and later promoting it to be a long term reference frame using memory
      // management operation '3' Mark a short-term reference picture as "used for long-term reference" and assign a
      // long-term frame index to it.
      // Alternatively, if encoding an IDR frame and setting adaptive_ref_pic_marking_mode_flag = 1, the driver will
      // assume that the client is attempting to set the H264 slice header long_term_reference_flag and will do so in
      // the output bitstream for such EncodeFrame call.
      m_curFrameState.adaptive_ref_pic_marking_mode_flag = 1;

      // Workaround for D3D12 validation bug requiring pRefPicMarkingOperationsCommands for IDR frames
      m_curFrameState.RefPicMarkingOperationsCommandsCount = 1u;
      m_CurrentFrameReferencesData.pMemoryOps.resize(m_curFrameState.RefPicMarkingOperationsCommandsCount);
      m_curFrameState.pRefPicMarkingOperationsCommands = m_CurrentFrameReferencesData.pMemoryOps.data();
   }

   ///
   /// Set ref pic modifications info
   ///
   // d3d12 needs the array allocations passed in D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264
   // to avoid copies, and taking advantage of the same memory layout with pipe, shallow copy them
   // If these static asserts do not pass anymore, change below ALL OCCURRENCES OF reinterpret_casts between
   // pipe_h264_ref_list_mod_entry and
   // D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264_REFERENCE_PICTURE_LIST_MODIFICATION_OPERATION
   static_assert(
      sizeof(struct pipe_h264_ref_list_mod_entry) ==
      sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264_REFERENCE_PICTURE_LIST_MODIFICATION_OPERATION));
   static_assert(
      offsetof(struct pipe_h264_ref_list_mod_entry, modification_of_pic_nums_idc) ==
      offsetof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264_REFERENCE_PICTURE_LIST_MODIFICATION_OPERATION,
               modification_of_pic_nums_idc));
   static_assert(
      offsetof(struct pipe_h264_ref_list_mod_entry, abs_diff_pic_num_minus1) ==
      offsetof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264_REFERENCE_PICTURE_LIST_MODIFICATION_OPERATION,
               abs_diff_pic_num_minus1));
   static_assert(
      offsetof(struct pipe_h264_ref_list_mod_entry, long_term_pic_num) ==
      offsetof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264_REFERENCE_PICTURE_LIST_MODIFICATION_OPERATION,
               long_term_pic_num));

   m_curFrameState.List0ReferenceFramesCount = 0;
   m_curFrameState.pList0ReferenceFrames = nullptr;
   m_curFrameState.List0RefPicModificationsCount = 0;
   m_curFrameState.pList0RefPicModifications = nullptr;
   m_curFrameState.List1ReferenceFramesCount = 0;
   m_curFrameState.pList1ReferenceFrames = nullptr;
   m_curFrameState.List1RefPicModificationsCount = 0;
   m_curFrameState.pList1RefPicModifications = nullptr;
   m_curFrameState.ReferenceFramesReconPictureDescriptorsCount = 0u;
   m_curFrameState.pReferenceFramesReconPictureDescriptors = nullptr;

   if ((m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_P_FRAME) ||
       (m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME)) {

      // Set DPB descriptors
      m_curFrameState.ReferenceFramesReconPictureDescriptorsCount =
         m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size();
      m_curFrameState.pReferenceFramesReconPictureDescriptors =
         m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.data();

      // Deep Copy L0 list
      m_curFrameState.List0ReferenceFramesCount = h264Pic->num_ref_idx_l0_active_minus1 + 1;
      m_CurrentFrameReferencesData.pList0ReferenceFrames.resize(m_curFrameState.List0ReferenceFramesCount);
      for (unsigned i = 0; i < m_curFrameState.List0ReferenceFramesCount; i++)
         m_CurrentFrameReferencesData.pList0ReferenceFrames[i] = h264Pic->ref_list0[i];
      m_curFrameState.pList0ReferenceFrames = m_CurrentFrameReferencesData.pList0ReferenceFrames.data();

      // Shallow Copy L0 ref modification list
      m_curFrameState.List0RefPicModificationsCount = h264Pic->slice.num_ref_list0_mod_operations;
      if (m_curFrameState.List0RefPicModificationsCount > 0) {
         m_curFrameState.pList0RefPicModifications = reinterpret_cast<
            D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264_REFERENCE_PICTURE_LIST_MODIFICATION_OPERATION *>(
            &h264Pic->slice.ref_list0_mod_operations[0]);
         // DX12 driver requires "End modification_of_pic_nums_idc syntax element loop" to be
         // sent at the end of the list for coding the slice header when sending down reordering commands
         assert((m_curFrameState.List0RefPicModificationsCount == 0) ||
                m_curFrameState.pList0RefPicModifications[m_curFrameState.List0RefPicModificationsCount - 1]
                      .modification_of_pic_nums_idc == 3);
      }
   }

   if (m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_H264_B_FRAME) {

      // Deep Copy L1 list
      m_curFrameState.List1ReferenceFramesCount = h264Pic->num_ref_idx_l1_active_minus1 + 1;
      m_CurrentFrameReferencesData.pList1ReferenceFrames.resize(m_curFrameState.List1ReferenceFramesCount);
      for (unsigned i = 0; i < m_curFrameState.List1ReferenceFramesCount; i++)
         m_CurrentFrameReferencesData.pList1ReferenceFrames[i] = h264Pic->ref_list1[i];
      m_curFrameState.pList1ReferenceFrames = m_CurrentFrameReferencesData.pList1ReferenceFrames.data();

      // Shallow Copy L1 ref modification list
      m_curFrameState.List1RefPicModificationsCount = h264Pic->slice.num_ref_list1_mod_operations;
      if (m_curFrameState.List1RefPicModificationsCount > 0) {
         m_curFrameState.pList1RefPicModifications = reinterpret_cast<
            D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264_REFERENCE_PICTURE_LIST_MODIFICATION_OPERATION *>(
            &h264Pic->slice.ref_list1_mod_operations[0]);
         // DX12 driver requires "End modification_of_pic_nums_idc syntax element loop" to be
         // sent at the end of the list for coding the slice header when sending down reordering commands
         assert((m_curFrameState.List1RefPicModificationsCount == 0) ||
                m_curFrameState.pList1RefPicModifications[m_curFrameState.List1RefPicModificationsCount - 1]
                      .modification_of_pic_nums_idc == 3);
      }
   }

   print_dpb();
   print_l0_l1_lists();
   print_mmco_lists();
}

void
d3d12_video_encoder_references_manager_h264::print_mmco_lists()
{
   debug_printf("[D3D12 Video Encoder Picture Manager H264] mmco list (%d entries) for frame with POC "
                "%d (frame_num: %d) and frame_type %d are:\n",
                m_curFrameState.RefPicMarkingOperationsCommandsCount,
                m_curFrameState.PictureOrderCountNumber,
                m_curFrameState.FrameDecodingOrderNumber,
                m_curFrameState.FrameType);
   for (uint32_t idx = 0; idx < m_curFrameState.RefPicMarkingOperationsCommandsCount; idx++) {
      D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_H264_REFERENCE_PICTURE_MARKING_OPERATION current_op =
         m_curFrameState.pRefPicMarkingOperationsCommands[idx];
      switch (current_op.memory_management_control_operation) {
         case 0:
         {
            debug_printf("End memory_management_control_operation syntax element loop\n");
         } break;
         case 1:
         {
            debug_printf(
               "Mark a short-term reference picture as \"unused for reference\" - difference_of_pic_nums_minus1: %d\n",
               current_op.difference_of_pic_nums_minus1);
         } break;
         case 2:
         {
            debug_printf("Mark a long-term reference picture as \"unused for reference\"\n - long_term_pic_num: %d\n",
                         current_op.long_term_pic_num);
         } break;
         case 3:
         {
            debug_printf("Mark a short-term reference picture as \"used for long-term reference\" and assign a "
                         "long-term frame index to it - difference_of_pic_nums_minus1: %d - long_term_frame_idx: %d\n",
                         current_op.difference_of_pic_nums_minus1,
                         current_op.long_term_frame_idx);
         } break;
         case 4:
         {
            debug_printf("Specify the maximum long-term frame index and mark all long-term reference pictures having "
                         "long-term frame indices greater than the maximum value as \"unused for reference\" - "
                         "max_long_term_frame_idx_plus1: %d",
                         current_op.max_long_term_frame_idx_plus1);
         } break;
         case 5:
         {
            debug_printf("Mark all reference pictures as \"unused for reference\" and set the MaxLongTermFrameIdx "
                         "variable to \"no long-term frame indices\"");
         } break;
         case 6:
         {
            debug_printf("Mark the current picture as \"used for long-term reference\" and assign a long-term frame "
                         "index to it - long_term_frame_idx: %d",
                         current_op.long_term_frame_idx);
         } break;
         default:
         {
            unreachable("Unsupported memory_management_control_operation");
         } break;
      }
   }
}
