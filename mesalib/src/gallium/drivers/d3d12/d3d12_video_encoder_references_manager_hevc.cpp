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

#include "d3d12_video_encoder_references_manager_hevc.h"
#include <algorithm>
#include <string>
#include "d3d12_screen.h"
#include "d3d12_resource.h"
#include "d3d12_video_buffer.h"

using namespace std;

bool
d3d12_video_encoder_references_manager_hevc::get_current_frame_picture_control_data(
   D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA &codecAllocation)
{
   assert((codecAllocation.DataSize == sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC)) ||
          (codecAllocation.DataSize == sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1)));
   memcpy(codecAllocation.pHEVCPicData1, &m_curFrameState, codecAllocation.DataSize);
   memset((uint8_t *)(codecAllocation.pHEVCPicData1) + codecAllocation.DataSize, 0, sizeof(m_curFrameState) - codecAllocation.DataSize);
   return true;
}

D3D12_VIDEO_ENCODE_REFERENCE_FRAMES
d3d12_video_encoder_references_manager_hevc::get_current_reference_frames()
{
   D3D12_VIDEO_ENCODE_REFERENCE_FRAMES retVal = { 0,
                                                  // ppTexture2Ds
                                                  nullptr,
                                                  // pSubresources
                                                  nullptr };

   // Return nullptr for fully intra frames (eg IDR)
   // and return references information for inter frames (eg.P/B) and I frame that doesn't flush DPB

   if (m_curFrameState.FrameType != D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_IDR_FRAME) {
      retVal.NumTexture2Ds = static_cast<UINT>(m_CurrentFrameReferencesData.ReferenceTextures.pResources.size());
      retVal.ppTexture2Ds = m_CurrentFrameReferencesData.ReferenceTextures.pResources.data();

      // D3D12 Encode expects null subresources for AoT
      retVal.pSubresources = m_fArrayOfTextures ? nullptr : m_CurrentFrameReferencesData.ReferenceTextures.pSubresources.data();
   }

   return retVal;
}

static const char *
d3d12_video_encoder_friendly_frame_type_hevc(D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC picType)
{
   switch (picType) {
      case D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_P_FRAME:
      {
         return "HEVC_P_FRAME";
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_B_FRAME:
      {
         return "HEVC_B_FRAME";
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_I_FRAME:
      {
         return "HEVC_I_FRAME";
      } break;
      case D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_IDR_FRAME:
      {
         return "HEVC_IDR_FRAME";
      } break;
      default:
      {
         unreachable("Unsupported D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC");
      } break;
   }
}

void
d3d12_video_encoder_references_manager_hevc::print_l0_l1_lists()
{
   if ((D3D12_DEBUG_VERBOSE & d3d12_debug) &&
       ((m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_P_FRAME) ||
        (m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_B_FRAME))) {

      debug_printf(
         "[D3D12 Video Encoder Picture Manager HEVC] L0 (%d entries) and L1 (%d entries) lists for frame with POC "
         "%d and frame_type %s are:\n",
         m_curFrameState.List0ReferenceFramesCount,
         m_curFrameState.List1ReferenceFramesCount,
         m_curFrameState.PictureOrderCountNumber,
         d3d12_video_encoder_friendly_frame_type_hevc(m_curFrameState.FrameType));

      std::string list0ContentsString;
      for (uint32_t idx = 0; idx < m_curFrameState.List0ReferenceFramesCount; idx++) {
         uint32_t value = m_curFrameState.pList0ReferenceFrames[idx];
         list0ContentsString += "{ DPBidx: ";
         list0ContentsString += std::to_string(value);
         list0ContentsString += " - POC: ";
         list0ContentsString += std::to_string(
            m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[value].PictureOrderCountNumber);
         list0ContentsString += " }\n";
      }

      debug_printf("[D3D12 Video Encoder Picture Manager HEVC] L0 list (%d entries) for frame with POC %d is: \n%s \n",
                   m_curFrameState.List0ReferenceFramesCount,
                   m_curFrameState.PictureOrderCountNumber,
                   list0ContentsString.c_str());

      std::string modificationOrderList0ContentsString;
      for (uint32_t idx = 0; idx < m_curFrameState.List0RefPicModificationsCount; idx++) {
         modificationOrderList0ContentsString += "{ ";
         modificationOrderList0ContentsString += std::to_string(m_curFrameState.pList0RefPicModifications[idx]);
         modificationOrderList0ContentsString += " }\n";
      }
      debug_printf("[D3D12 Video Encoder Picture Manager HEVC] L0 modification list (%d entries) for frame with POC %d "
                   " - temporal_id (%d) is: \n%s \n",
                   m_curFrameState.List0RefPicModificationsCount,
                   m_curFrameState.PictureOrderCountNumber,
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
         list1ContentsString += " }\n";
      }

      debug_printf("[D3D12 Video Encoder Picture Manager HEVC] L1 list (%d entries) for frame with POC %d is: \n%s \n",
                   m_curFrameState.List1ReferenceFramesCount,
                   m_curFrameState.PictureOrderCountNumber,
                   list1ContentsString.c_str());

      std::string modificationOrderList1ContentsString;
      for (uint32_t idx = 0; idx < m_curFrameState.List1RefPicModificationsCount; idx++) {
         modificationOrderList1ContentsString += "{ ";
         modificationOrderList1ContentsString += std::to_string(m_curFrameState.pList1RefPicModifications[idx]);
         modificationOrderList1ContentsString += " }\n";
      }

      debug_printf("[D3D12 Video Encoder Picture Manager HEVC] L1 modification list (%d entries) for frame with POC %d "
                   "- temporal_id (%d) is: \n%s \n",
                   m_curFrameState.List1RefPicModificationsCount,
                   m_curFrameState.PictureOrderCountNumber,
                   m_curFrameState.TemporalLayerIndex,
                   modificationOrderList1ContentsString.c_str());
   }
}

void
d3d12_video_encoder_references_manager_hevc::print_dpb()
{
   if (D3D12_DEBUG_VERBOSE & d3d12_debug) {
      std::string dpbContents;
      for (uint32_t dpbResIdx = 0;
           dpbResIdx < m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size();
           dpbResIdx++) {
         auto &dpbDesc = m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[dpbResIdx];

         dpbContents += "{ DPBidx: ";
         dpbContents += std::to_string(dpbResIdx);
         dpbContents += " - POC: ";
         dpbContents += std::to_string(dpbDesc.PictureOrderCountNumber);
         dpbContents += " - IsRefUsedByCurrentPic: ";
         dpbContents += std::to_string(dpbDesc.IsRefUsedByCurrentPic);
         dpbContents += " - IsLongTermReference: ";
         dpbContents += std::to_string(dpbDesc.IsLongTermReference);
         dpbContents += " - TemporalLayerIndex: ";
         dpbContents += std::to_string(dpbDesc.TemporalLayerIndex);
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

         if (dpbDesc.PictureOrderCountNumber == m_curFrameState.PictureOrderCountNumber) {
            dpbContents += " - CURRENT FRAME RECON PIC ";
         }

         dpbContents += "}\n";
      }

      debug_printf("[D3D12 Video Encoder Picture Manager HEVC] DPB has %d frames - DPB references for frame with POC "
                   "%d and frame_type %s are: \n%s \n",
                   static_cast<UINT>(m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size()),
                   m_curFrameState.PictureOrderCountNumber,
                   d3d12_video_encoder_friendly_frame_type_hevc(m_curFrameState.FrameType),
                   dpbContents.c_str());
   }
}

static D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC
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

void
d3d12_video_encoder_references_manager_hevc::begin_frame(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA curFrameData,
                                                         bool bUsedAsReference,
                                                         struct pipe_picture_desc *picture)
{
   assert((curFrameData.DataSize == sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC)) ||
          (curFrameData.DataSize == sizeof(D3D12_VIDEO_ENCODER_PICTURE_CONTROL_CODEC_DATA_HEVC1)));
   memcpy(&m_curFrameState, curFrameData.pHEVCPicData1, curFrameData.DataSize);
   memset(((uint8_t*)(&m_curFrameState) + curFrameData.DataSize), 0, sizeof(m_curFrameState) - curFrameData.DataSize);

   m_isCurrentFrameUsedAsReference = bUsedAsReference;

   struct pipe_h265_enc_picture_desc *hevcPic = (struct pipe_h265_enc_picture_desc *) picture;

   ///
   /// Copy DPB snapshot from pipe params
   ///

   m_curFrameState.ReferenceFramesReconPictureDescriptorsCount =
      static_cast<uint32_t>(m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size());
   m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.resize(hevcPic->dpb_size);
   m_CurrentFrameReferencesData.ReferenceTextures.pResources.resize(hevcPic->dpb_size);
   m_CurrentFrameReferencesData.ReferenceTextures.pSubresources.resize(hevcPic->dpb_size);
   m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.resize(hevcPic->dpb_size);
   m_CurrentFrameReferencesData.ReconstructedPicTexture = { NULL, 0u };
   for (uint8_t i = 0; i < hevcPic->dpb_size; i++) {
      //
      // Set entry DPB members
      //

      m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[i].IsLongTermReference =
         hevcPic->dpb[i].is_ltr;
      m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[i].PictureOrderCountNumber =
         hevcPic->dpb[i].pic_order_cnt;
      // mirror indices between DPB entries and allocation arrays
      m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[i].ReconstructedPictureResourceIndex = i;
      m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[i].TemporalLayerIndex =
         hevcPic->dpb[i].temporal_id;

      // Check if this i-th dpb descriptor entry is referenced by any entry in L0 or L1 lists
      // and set IsRefUsedByCurrentPic accordingly
      auto endItL0 = hevcPic->ref_list0 + (hevcPic->num_ref_idx_l0_active_minus1 + 1);
      bool bReferencesFromL0 = std::find(hevcPic->ref_list0, endItL0, i) != endItL0;
      bool bReferencesFromL1 = false;
      if (d3d12_video_encoder_convert_frame_type_hevc(hevcPic->picture_type) == D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_B_FRAME) {
         auto endItL1 = hevcPic->ref_list1 + (hevcPic->num_ref_idx_l1_active_minus1 + 1);
         bReferencesFromL1 = std::find(hevcPic->ref_list1, endItL1, i) != endItL1;
      }
      m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors[i].IsRefUsedByCurrentPic =
         bReferencesFromL0 || bReferencesFromL1;

      //
      // Set texture allocations
      //

      struct d3d12_video_buffer *vidbuf = (struct d3d12_video_buffer *) hevcPic->dpb[i].buffer;
      m_CurrentFrameReferencesData.ReferenceTextures.pResources[i] = d3d12_resource_resource(vidbuf->texture);
      m_CurrentFrameReferencesData.ReferenceTextures.pSubresources[i] = vidbuf->idx_texarray_slots;

      if (hevcPic->dpb[i].pic_order_cnt == hevcPic->pic_order_cnt) {
         m_CurrentFrameReferencesData.ReconstructedPicTexture.pReconstructedPicture =
            m_CurrentFrameReferencesData.ReferenceTextures.pResources[i];
         m_CurrentFrameReferencesData.ReconstructedPicTexture.ReconstructedPictureSubresource =
            m_CurrentFrameReferencesData.ReferenceTextures.pSubresources[i];
      }
   }

   ///
   /// Set pic control info
   ///

   m_curFrameState.FrameType = d3d12_video_encoder_convert_frame_type_hevc(hevcPic->picture_type);
   m_curFrameState.PictureOrderCountNumber = hevcPic->pic_order_cnt;

   ///
   /// Set reference pics info
   ///

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

   if ((m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_P_FRAME) ||
       (m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_B_FRAME)) {

      // Set DPB descriptors
      m_curFrameState.ReferenceFramesReconPictureDescriptorsCount =
         static_cast<UINT>(m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.size());
      m_curFrameState.pReferenceFramesReconPictureDescriptors =
         m_CurrentFrameReferencesData.pReferenceFramesReconPictureDescriptors.data();

      // Deep Copy L0 list
      m_curFrameState.List0ReferenceFramesCount = hevcPic->num_ref_idx_l0_active_minus1 + 1;
      m_CurrentFrameReferencesData.pList0ReferenceFrames.resize(m_curFrameState.List0ReferenceFramesCount);
      for (unsigned i = 0; i < m_curFrameState.List0ReferenceFramesCount; i++)
         m_CurrentFrameReferencesData.pList0ReferenceFrames[i] = hevcPic->ref_list0[i];
      m_curFrameState.pList0ReferenceFrames = m_CurrentFrameReferencesData.pList0ReferenceFrames.data();

      // Deep Copy L0 ref modification list
      if (hevcPic->slice.ref_pic_lists_modification.ref_pic_list_modification_flag_l0) {
         m_curFrameState.List0RefPicModificationsCount = hevcPic->num_ref_idx_l0_active_minus1 + 1;
         m_CurrentFrameReferencesData.pList0RefPicModifications.resize(m_curFrameState.List0RefPicModificationsCount);
         for (unsigned i = 0; i < m_curFrameState.List0RefPicModificationsCount; i++)
            m_CurrentFrameReferencesData.pList0RefPicModifications[i] =
               hevcPic->slice.ref_pic_lists_modification.list_entry_l0[i];
         m_curFrameState.pList0RefPicModifications = m_CurrentFrameReferencesData.pList0RefPicModifications.data();
      }
   }

   if (m_curFrameState.FrameType == D3D12_VIDEO_ENCODER_FRAME_TYPE_HEVC_B_FRAME) {

      // Deep Copy L1 list
      m_curFrameState.List1ReferenceFramesCount = hevcPic->num_ref_idx_l1_active_minus1 + 1;
      m_CurrentFrameReferencesData.pList1ReferenceFrames.resize(m_curFrameState.List1ReferenceFramesCount);
      for (unsigned i = 0; i < m_curFrameState.List1ReferenceFramesCount; i++)
         m_CurrentFrameReferencesData.pList1ReferenceFrames[i] = hevcPic->ref_list1[i];
      m_curFrameState.pList1ReferenceFrames = m_CurrentFrameReferencesData.pList1ReferenceFrames.data();

      // Deep Copy L1 ref modification list
      if (hevcPic->slice.ref_pic_lists_modification.ref_pic_list_modification_flag_l1) {
         m_curFrameState.List1RefPicModificationsCount = hevcPic->num_ref_idx_l1_active_minus1 + 1;
         m_CurrentFrameReferencesData.pList1RefPicModifications.resize(m_curFrameState.List1RefPicModificationsCount);
         for (unsigned i = 0; i < m_curFrameState.List1RefPicModificationsCount; i++)
            m_CurrentFrameReferencesData.pList1RefPicModifications[i] =
               hevcPic->slice.ref_pic_lists_modification.list_entry_l1[i];
         m_curFrameState.pList1RefPicModifications = m_CurrentFrameReferencesData.pList1RefPicModifications.data();
      }
   }

   print_dpb();
   print_l0_l1_lists();
}
