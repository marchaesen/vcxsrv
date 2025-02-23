/*
 * Copyright 2017 Advanced Micro Devices, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir.h"
#include "ac_nir.h"
#include "ac_shader_util.h"
#include "radeon_uvd_enc.h"
#include "radeon_vce.h"
#include "radeon_video.h"
#include "si_pipe.h"
#include "util/u_cpu_detect.h"
#include "util/u_screen.h"
#include "util/u_video.h"
#include "vl/vl_decoder.h"
#include "vl/vl_video_buffer.h"
#include <sys/utsname.h>

/* The capabilities reported by the kernel has priority
   over the existing logic in si_get_video_param */
#define QUERYABLE_KERNEL   (sscreen->info.is_amdgpu && \
   !!(sscreen->info.drm_minor >= 41))
#define KERNEL_DEC_CAP(codec, attrib)    \
   (codec > PIPE_VIDEO_FORMAT_UNKNOWN && codec <= PIPE_VIDEO_FORMAT_AV1) ? \
   (sscreen->info.dec_caps.codec_info[codec - 1].valid ? \
    sscreen->info.dec_caps.codec_info[codec - 1].attrib : 0) : 0
#define KERNEL_ENC_CAP(codec, attrib)    \
   (codec > PIPE_VIDEO_FORMAT_UNKNOWN && codec <= PIPE_VIDEO_FORMAT_AV1) ? \
   (sscreen->info.enc_caps.codec_info[codec - 1].valid ? \
    sscreen->info.enc_caps.codec_info[codec - 1].attrib : 0) : 0

static const char *si_get_vendor(struct pipe_screen *pscreen)
{
   return "AMD";
}

static const char *si_get_device_vendor(struct pipe_screen *pscreen)
{
   return "AMD";
}

static bool
si_is_compute_copy_faster(struct pipe_screen *pscreen,
                          enum pipe_format src_format,
                          enum pipe_format dst_format,
                          unsigned width,
                          unsigned height,
                          unsigned depth,
                          bool cpu)
{
   if (cpu)
      /* very basic for now */
      return width * height * depth > 64 * 64;
   return false;
}

static const void *si_get_compiler_options(struct pipe_screen *screen, enum pipe_shader_ir ir,
                                           enum pipe_shader_type shader)
{
   struct si_screen *sscreen = (struct si_screen *)screen;

   assert(ir == PIPE_SHADER_IR_NIR);
   return sscreen->nir_options;
}

static void si_get_driver_uuid(struct pipe_screen *pscreen, char *uuid)
{
   ac_compute_driver_uuid(uuid, PIPE_UUID_SIZE);
}

static void si_get_device_uuid(struct pipe_screen *pscreen, char *uuid)
{
   struct si_screen *sscreen = (struct si_screen *)pscreen;

   ac_compute_device_uuid(&sscreen->info, uuid, PIPE_UUID_SIZE);
}

static const char *si_get_name(struct pipe_screen *pscreen)
{
   struct si_screen *sscreen = (struct si_screen *)pscreen;

   return sscreen->renderer_string;
}

static int si_get_video_param_no_video_hw(struct pipe_screen *screen, enum pipe_video_profile profile,
                                          enum pipe_video_entrypoint entrypoint,
                                          enum pipe_video_cap param)
{
   switch (param) {
   case PIPE_VIDEO_CAP_SUPPORTED:
      return vl_profile_supported(screen, profile, entrypoint);
   case PIPE_VIDEO_CAP_NPOT_TEXTURES:
      return 1;
   case PIPE_VIDEO_CAP_MAX_WIDTH:
   case PIPE_VIDEO_CAP_MAX_HEIGHT:
      return vl_video_buffer_max_size(screen);
   case PIPE_VIDEO_CAP_PREFERRED_FORMAT:
      return PIPE_FORMAT_NV12;
   case PIPE_VIDEO_CAP_PREFERS_INTERLACED:
      return false;
   case PIPE_VIDEO_CAP_SUPPORTS_INTERLACED:
      return false;
   case PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE:
      return true;
   case PIPE_VIDEO_CAP_MAX_LEVEL:
      return vl_level_supported(screen, profile);
   default:
      return 0;
   }
}

static int si_get_video_param(struct pipe_screen *screen, enum pipe_video_profile profile,
                              enum pipe_video_entrypoint entrypoint, enum pipe_video_cap param)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   enum pipe_video_format codec = u_reduce_video_profile(profile);
   bool fully_supported_profile = ((profile >= PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE) &&
                                   (profile <= PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH)) ||
                                  (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN) ||
                                  (profile == PIPE_VIDEO_PROFILE_AV1_MAIN);

   /* Return the capability of Video Post Processor.
    * Have to determine the HW version of VPE.
    * Have to check the HW limitation and
    * Check if the VPE exists and is valid
    */
   if (sscreen->info.ip[AMD_IP_VPE].num_queues && entrypoint == PIPE_VIDEO_ENTRYPOINT_PROCESSING) {

      switch(param) {
      case PIPE_VIDEO_CAP_SUPPORTED:
         return true;
      case PIPE_VIDEO_CAP_MAX_WIDTH:
         return 10240;
      case PIPE_VIDEO_CAP_MAX_HEIGHT:
         return 10240;
      case PIPE_VIDEO_CAP_VPP_MAX_INPUT_WIDTH:
         return 10240;
      case PIPE_VIDEO_CAP_VPP_MAX_INPUT_HEIGHT:
         return 10240;
      case PIPE_VIDEO_CAP_VPP_MIN_INPUT_WIDTH:
         return 16;
      case PIPE_VIDEO_CAP_VPP_MIN_INPUT_HEIGHT:
         return 16;
      case PIPE_VIDEO_CAP_VPP_MAX_OUTPUT_WIDTH:
         return 10240;
      case PIPE_VIDEO_CAP_VPP_MAX_OUTPUT_HEIGHT:
         return 10240;
      case PIPE_VIDEO_CAP_VPP_MIN_OUTPUT_WIDTH:
         return 16;
      case PIPE_VIDEO_CAP_VPP_MIN_OUTPUT_HEIGHT:
         return 16;
      case PIPE_VIDEO_CAP_VPP_ORIENTATION_MODES:
         /* VPE 1st generation does not support orientation
          * Have to determine the version and features of VPE in future.
          */
         return PIPE_VIDEO_VPP_ORIENTATION_DEFAULT;
      case PIPE_VIDEO_CAP_VPP_BLEND_MODES:
         /* VPE 1st generation does not support blending.
          * Have to determine the version and features of VPE in future.
          */
         return PIPE_VIDEO_VPP_BLEND_MODE_NONE;
      case PIPE_VIDEO_CAP_PREFERRED_FORMAT:
         return PIPE_FORMAT_NV12;
      case PIPE_VIDEO_CAP_PREFERS_INTERLACED:
         return false;
      case PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE:
         return true;
      case PIPE_VIDEO_CAP_REQUIRES_FLUSH_ON_END_FRAME:
         /* true: VPP flush function will be called within vaEndPicture() */
         /* false: VPP flush function will be skipped */
         return false;
      case PIPE_VIDEO_CAP_SUPPORTS_INTERLACED:
         /* for VPE we prefer non-interlaced buffer */
         return false;
      default:
         return 0;
      }
   }

   if (entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) {
      if (!(sscreen->info.ip[AMD_IP_VCE].num_queues ||
            sscreen->info.ip[AMD_IP_UVD_ENC].num_queues ||
            sscreen->info.ip[AMD_IP_VCN_ENC].num_queues))
         return 0;

      if (sscreen->info.vcn_ip_version == VCN_4_0_3 ||
	  sscreen->info.vcn_ip_version == VCN_5_0_1)
	 return 0;

      switch (param) {
      case PIPE_VIDEO_CAP_SUPPORTED:
         return (
             /* in case it is explicitly marked as not supported by the kernel */
            ((QUERYABLE_KERNEL && fully_supported_profile) ? KERNEL_ENC_CAP(codec, valid) : 1) &&
            ((codec == PIPE_VIDEO_FORMAT_MPEG4_AVC && profile != PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10 &&
             (sscreen->info.vcn_ip_version >= VCN_1_0_0 || si_vce_is_fw_version_supported(sscreen))) ||
            (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN &&
             (sscreen->info.vcn_ip_version >= VCN_1_0_0 || si_radeon_uvd_enc_supported(sscreen))) ||
            (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10 && sscreen->info.vcn_ip_version >= VCN_2_0_0) ||
            (profile == PIPE_VIDEO_PROFILE_AV1_MAIN &&
	     (sscreen->info.vcn_ip_version >= VCN_4_0_0 && sscreen->info.vcn_ip_version != VCN_4_0_3))));
      case PIPE_VIDEO_CAP_NPOT_TEXTURES:
         return 1;
      case PIPE_VIDEO_CAP_MIN_WIDTH:
         if (sscreen->info.vcn_ip_version >= VCN_5_0_0) {
            if (codec == PIPE_VIDEO_FORMAT_MPEG4_AVC)
               return 96;
            else if (codec == PIPE_VIDEO_FORMAT_HEVC)
               return 384;
            else if (codec == PIPE_VIDEO_FORMAT_AV1)
               return 320;
         }
         return (codec == PIPE_VIDEO_FORMAT_HEVC) ? 130 : 128;
      case PIPE_VIDEO_CAP_MIN_HEIGHT:
         if (sscreen->info.vcn_ip_version >= VCN_5_0_0 && codec == PIPE_VIDEO_FORMAT_MPEG4_AVC)
            return 32;
         return 128;
      case PIPE_VIDEO_CAP_MAX_WIDTH:
         if (codec != PIPE_VIDEO_FORMAT_UNKNOWN && QUERYABLE_KERNEL)
            return KERNEL_ENC_CAP(codec, max_width);
         else
            return (sscreen->info.family < CHIP_TONGA) ? 2048 : 4096;
      case PIPE_VIDEO_CAP_MAX_HEIGHT:
         if (codec != PIPE_VIDEO_FORMAT_UNKNOWN && QUERYABLE_KERNEL)
            return KERNEL_ENC_CAP(codec, max_height);
         else
            return (sscreen->info.family < CHIP_TONGA) ? 1152 : 2304;
      case PIPE_VIDEO_CAP_PREFERRED_FORMAT:
         if (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10)
            return PIPE_FORMAT_P010;
         else
            return PIPE_FORMAT_NV12;
      case PIPE_VIDEO_CAP_PREFERS_INTERLACED:
         return false;
      case PIPE_VIDEO_CAP_SUPPORTS_INTERLACED:
         return false;
      case PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE:
         return true;
      case PIPE_VIDEO_CAP_STACKED_FRAMES:
         return (sscreen->info.family < CHIP_TONGA) ? 1 : 2;
      case PIPE_VIDEO_CAP_MAX_TEMPORAL_LAYERS:
         return (sscreen->info.ip[AMD_IP_UVD_ENC].num_queues ||
                 sscreen->info.vcn_ip_version >= VCN_1_0_0) ? 4 : 0;
      case PIPE_VIDEO_CAP_ENC_QUALITY_LEVEL:
         return 32;
      case PIPE_VIDEO_CAP_ENC_SUPPORTS_MAX_FRAME_SIZE:
         return 1;

      case PIPE_VIDEO_CAP_ENC_HEVC_FEATURE_FLAGS:
         if (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN ||
             profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10) {
            union pipe_h265_enc_cap_features pipe_features;
            pipe_features.value = 0;

            pipe_features.bits.amp = PIPE_ENC_FEATURE_SUPPORTED;
            pipe_features.bits.strong_intra_smoothing = PIPE_ENC_FEATURE_SUPPORTED;
            pipe_features.bits.constrained_intra_pred = PIPE_ENC_FEATURE_SUPPORTED;
            pipe_features.bits.deblocking_filter_disable
                                                      = PIPE_ENC_FEATURE_SUPPORTED;
            if (sscreen->info.vcn_ip_version >= VCN_2_0_0) {
               pipe_features.bits.sao = PIPE_ENC_FEATURE_SUPPORTED;
               pipe_features.bits.cu_qp_delta = PIPE_ENC_FEATURE_SUPPORTED;
            }
            if (sscreen->info.vcn_ip_version >= VCN_3_0_0)
               pipe_features.bits.transform_skip = PIPE_ENC_FEATURE_SUPPORTED;

            return pipe_features.value;
         } else
            return 0;

      case PIPE_VIDEO_CAP_ENC_HEVC_BLOCK_SIZES:
         if (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN ||
             profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10) {
            union pipe_h265_enc_cap_block_sizes pipe_block_sizes;
            pipe_block_sizes.value = 0;

            pipe_block_sizes.bits.log2_max_coding_tree_block_size_minus3 = 3;
            pipe_block_sizes.bits.log2_min_coding_tree_block_size_minus3 = 3;
            pipe_block_sizes.bits.log2_min_luma_coding_block_size_minus3 = 0;
            pipe_block_sizes.bits.log2_max_luma_transform_block_size_minus2 = 3;
            pipe_block_sizes.bits.log2_min_luma_transform_block_size_minus2 = 0;

            if (sscreen->info.ip[AMD_IP_UVD_ENC].num_queues) {
               pipe_block_sizes.bits.max_max_transform_hierarchy_depth_inter = 3;
               pipe_block_sizes.bits.min_max_transform_hierarchy_depth_inter = 3;
               pipe_block_sizes.bits.max_max_transform_hierarchy_depth_intra = 3;
               pipe_block_sizes.bits.min_max_transform_hierarchy_depth_intra = 3;
            }

            return pipe_block_sizes.value;
         } else
            return 0;

      case PIPE_VIDEO_CAP_ENC_MAX_SLICES_PER_FRAME:
         return 128;

      case PIPE_VIDEO_CAP_ENC_SLICES_STRUCTURE:
         return PIPE_VIDEO_CAP_SLICE_STRUCTURE_ARBITRARY_MACROBLOCKS |
                PIPE_VIDEO_CAP_SLICE_STRUCTURE_EQUAL_ROWS |
                PIPE_VIDEO_CAP_SLICE_STRUCTURE_EQUAL_MULTI_ROWS;

      case PIPE_VIDEO_CAP_ENC_AV1_FEATURE:
         if (sscreen->info.vcn_ip_version >= VCN_4_0_0 && sscreen->info.vcn_ip_version != VCN_4_0_3) {
            union pipe_av1_enc_cap_features attrib;
            attrib.value = 0;

            attrib.bits.support_128x128_superblock = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_filter_intra = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_intra_edge_filter = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_interintra_compound = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_masked_compound = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_warped_motion = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_palette_mode = PIPE_ENC_FEATURE_SUPPORTED;
            attrib.bits.support_dual_filter = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_jnt_comp = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_ref_frame_mvs = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_superres = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_restoration = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_allow_intrabc = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.support_cdef_channel_strength = PIPE_ENC_FEATURE_SUPPORTED;

            return attrib.value;
         } else
            return 0;

      case PIPE_VIDEO_CAP_ENC_AV1_FEATURE_EXT1:
         if (sscreen->info.vcn_ip_version >= VCN_4_0_0 && sscreen->info.vcn_ip_version != VCN_4_0_3) {
            union pipe_av1_enc_cap_features_ext1 attrib_ext1;
            attrib_ext1.value = 0;
            attrib_ext1.bits.interpolation_filter = PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_EIGHT_TAP |
                           PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_EIGHT_TAP_SMOOTH |
                           PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_EIGHT_TAP_SHARP |
                           PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_BILINEAR |
                           PIPE_VIDEO_CAP_ENC_AV1_INTERPOLATION_FILTER_SWITCHABLE;
            attrib_ext1.bits.min_segid_block_size_accepted = 0;
            attrib_ext1.bits.segment_feature_support = 0;

            return attrib_ext1.value;
         } else
            return 0;

      case PIPE_VIDEO_CAP_ENC_AV1_FEATURE_EXT2:
         if (sscreen->info.vcn_ip_version >= VCN_4_0_0 && sscreen->info.vcn_ip_version != VCN_4_0_3) {
            union pipe_av1_enc_cap_features_ext2 attrib_ext2;
            attrib_ext2.value = 0;

           attrib_ext2.bits.tile_size_bytes_minus1 = 3;
           attrib_ext2.bits.obu_size_bytes_minus1 = 1;
           /**
            * tx_mode supported.
            * (tx_mode_support & 0x01) == 1: ONLY_4X4 is supported, 0: not.
            * (tx_mode_support & 0x02) == 1: TX_MODE_LARGEST is supported, 0: not.
            * (tx_mode_support & 0x04) == 1: TX_MODE_SELECT is supported, 0: not.
            */
           attrib_ext2.bits.tx_mode_support = PIPE_VIDEO_CAP_ENC_AV1_TX_MODE_SELECT;
           attrib_ext2.bits.max_tile_num_minus1 = 31;

            return attrib_ext2.value;
         } else
            return 0;
      case PIPE_VIDEO_CAP_ENC_SUPPORTS_TILE:
         if ((sscreen->info.vcn_ip_version >= VCN_4_0_0 && sscreen->info.vcn_ip_version != VCN_4_0_3) &&
              profile == PIPE_VIDEO_PROFILE_AV1_MAIN)
            return 1;
         else
            return 0;

      case PIPE_VIDEO_CAP_ENC_MAX_REFERENCES_PER_FRAME:
         if (sscreen->info.vcn_ip_version >= VCN_3_0_0) {
            int refPicList0 = 1;
            int refPicList1 = codec == PIPE_VIDEO_FORMAT_MPEG4_AVC ? 1 : 0;
            if (sscreen->info.vcn_ip_version >= VCN_5_0_0 && codec == PIPE_VIDEO_FORMAT_AV1) {
               refPicList0 = 2;
               refPicList1 = 1;
            }
            return refPicList0 | (refPicList1 << 16);
         } else
            return 1;

      case PIPE_VIDEO_CAP_ENC_INTRA_REFRESH:
            return PIPE_VIDEO_ENC_INTRA_REFRESH_ROW |
                   PIPE_VIDEO_ENC_INTRA_REFRESH_COLUMN |
                   PIPE_VIDEO_ENC_INTRA_REFRESH_P_FRAME;

      case PIPE_VIDEO_CAP_ENC_ROI:
         if (sscreen->info.vcn_ip_version >= VCN_1_0_0) {
            union pipe_enc_cap_roi attrib;
            attrib.value = 0;

            attrib.bits.num_roi_regions = PIPE_ENC_ROI_REGION_NUM_MAX;
            attrib.bits.roi_rc_priority_support = PIPE_ENC_FEATURE_NOT_SUPPORTED;
            attrib.bits.roi_rc_qp_delta_support = PIPE_ENC_FEATURE_SUPPORTED;
            return attrib.value;
         }
         else
            return 0;

      case PIPE_VIDEO_CAP_ENC_SURFACE_ALIGNMENT: {
         union pipe_enc_cap_surface_alignment attrib = {0};
         if (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN ||
             profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10) {
            /* 64 x 16 */
            attrib.bits.log2_width_alignment = 6;
            attrib.bits.log2_height_alignment = 4;
         } else if (profile == PIPE_VIDEO_PROFILE_AV1_MAIN) {
            if (sscreen->info.vcn_ip_version < VCN_5_0_0) {
               /* 64 x 16 */
               attrib.bits.log2_width_alignment = 6;
               attrib.bits.log2_height_alignment = 4;
            } else {
               /* 8 x 2 */
               attrib.bits.log2_width_alignment = 3;
               attrib.bits.log2_height_alignment = 1;
            }
         }
         return attrib.value;
      }

      case PIPE_VIDEO_CAP_ENC_RATE_CONTROL_QVBR:
         if (sscreen->info.vcn_ip_version >= VCN_3_0_0 &&
             sscreen->info.vcn_ip_version < VCN_4_0_0)
            return sscreen->info.vcn_enc_minor_version >= 30;

         if (sscreen->info.vcn_ip_version >= VCN_4_0_0 &&
             sscreen->info.vcn_ip_version < VCN_5_0_0)
            return sscreen->info.vcn_enc_minor_version >= 15;

         if (sscreen->info.vcn_ip_version >= VCN_5_0_0)
            return sscreen->info.vcn_enc_minor_version >= 3;

         return 0;

      default:
         return 0;
      }
   }

   switch (param) {
   case PIPE_VIDEO_CAP_SUPPORTED:
      if (codec != PIPE_VIDEO_FORMAT_JPEG &&
          !(sscreen->info.ip[AMD_IP_UVD].num_queues ||
            ((sscreen->info.vcn_ip_version >= VCN_4_0_0) ?
	      sscreen->info.ip[AMD_IP_VCN_UNIFIED].num_queues :
	      sscreen->info.ip[AMD_IP_VCN_DEC].num_queues)))
         return false;
      if (QUERYABLE_KERNEL && fully_supported_profile &&
          sscreen->info.vcn_ip_version >= VCN_1_0_0)
         return KERNEL_DEC_CAP(codec, valid);
      if (codec < PIPE_VIDEO_FORMAT_MPEG4_AVC &&
          sscreen->info.vcn_ip_version >= VCN_3_0_33)
         return false;

      switch (codec) {
      case PIPE_VIDEO_FORMAT_MPEG12:
         return !(sscreen->info.vcn_ip_version >= VCN_3_0_33 || profile == PIPE_VIDEO_PROFILE_MPEG1);
      case PIPE_VIDEO_FORMAT_MPEG4:
         return !(sscreen->info.vcn_ip_version >= VCN_3_0_33);
      case PIPE_VIDEO_FORMAT_MPEG4_AVC:
         if ((sscreen->info.family == CHIP_POLARIS10 || sscreen->info.family == CHIP_POLARIS11) &&
             sscreen->info.uvd_fw_version < UVD_FW_1_66_16) {
            RVID_ERR("POLARIS10/11 firmware version need to be updated.\n");
            return false;
         }
         return (profile != PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH10);
      case PIPE_VIDEO_FORMAT_VC1:
         return !(sscreen->info.vcn_ip_version >= VCN_3_0_33);
      case PIPE_VIDEO_FORMAT_HEVC:
         /* Carrizo only supports HEVC Main */
         if (sscreen->info.family >= CHIP_STONEY)
            return (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN ||
                    profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10);
         else if (sscreen->info.family >= CHIP_CARRIZO)
            return profile == PIPE_VIDEO_PROFILE_HEVC_MAIN;
         return false;
      case PIPE_VIDEO_FORMAT_JPEG:
         if (sscreen->info.vcn_ip_version >= VCN_1_0_0) {
            if (!sscreen->info.ip[AMD_IP_VCN_JPEG].num_queues)
               return false;
            else
               return true;
         }
         if (sscreen->info.family < CHIP_CARRIZO || sscreen->info.family >= CHIP_VEGA10)
            return false;
         if (!sscreen->info.is_amdgpu) {
            RVID_ERR("No MJPEG support for the kernel version\n");
            return false;
         }
         return true;
      case PIPE_VIDEO_FORMAT_VP9:
         return sscreen->info.vcn_ip_version >= VCN_1_0_0;
      case PIPE_VIDEO_FORMAT_AV1:
         if (profile == PIPE_VIDEO_PROFILE_AV1_PROFILE2)
            return sscreen->info.vcn_ip_version >= VCN_5_0_0 || sscreen->info.vcn_ip_version == VCN_4_0_0;
         return sscreen->info.vcn_ip_version >= VCN_3_0_0 && sscreen->info.vcn_ip_version != VCN_3_0_33;
      default:
         return false;
      }
   case PIPE_VIDEO_CAP_NPOT_TEXTURES:
      return 1;
   case PIPE_VIDEO_CAP_MIN_WIDTH:
   case PIPE_VIDEO_CAP_MIN_HEIGHT:
      return (codec == PIPE_VIDEO_FORMAT_AV1) ? 16 : 64;
   case PIPE_VIDEO_CAP_MAX_WIDTH:
      if (codec != PIPE_VIDEO_FORMAT_UNKNOWN && QUERYABLE_KERNEL)
            return KERNEL_DEC_CAP(codec, max_width);
      else {
         switch (codec) {
         case PIPE_VIDEO_FORMAT_HEVC:
         case PIPE_VIDEO_FORMAT_VP9:
         case PIPE_VIDEO_FORMAT_AV1:
            return (sscreen->info.vcn_ip_version < VCN_2_0_0) ?
               ((sscreen->info.family < CHIP_TONGA) ? 2048 : 4096) : 8192;
         default:
            return (sscreen->info.family < CHIP_TONGA) ? 2048 : 4096;
         }
      }
   case PIPE_VIDEO_CAP_MAX_HEIGHT:
      if (codec != PIPE_VIDEO_FORMAT_UNKNOWN && QUERYABLE_KERNEL)
            return KERNEL_DEC_CAP(codec, max_height);
      else {
         switch (codec) {
         case PIPE_VIDEO_FORMAT_HEVC:
         case PIPE_VIDEO_FORMAT_VP9:
         case PIPE_VIDEO_FORMAT_AV1:
            return (sscreen->info.vcn_ip_version < VCN_2_0_0) ?
               ((sscreen->info.family < CHIP_TONGA) ? 1152 : 4096) : 4352;
         default:
            return (sscreen->info.family < CHIP_TONGA) ? 1152 : 4096;
         }
      }
   case PIPE_VIDEO_CAP_PREFERRED_FORMAT:
      if (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10)
         return PIPE_FORMAT_P010;
      else if (profile == PIPE_VIDEO_PROFILE_VP9_PROFILE2)
         return PIPE_FORMAT_P010;
      else
         return PIPE_FORMAT_NV12;

   case PIPE_VIDEO_CAP_PREFERS_INTERLACED:
      return false;
   case PIPE_VIDEO_CAP_SUPPORTS_INTERLACED: {
      enum pipe_video_format format = u_reduce_video_profile(profile);

      if (format >= PIPE_VIDEO_FORMAT_HEVC)
         return false;

      return true;
   }
   case PIPE_VIDEO_CAP_SUPPORTS_PROGRESSIVE:
      return true;
   case PIPE_VIDEO_CAP_MAX_LEVEL:
      if ((profile == PIPE_VIDEO_PROFILE_MPEG2_SIMPLE ||
           profile == PIPE_VIDEO_PROFILE_MPEG2_MAIN ||
           profile == PIPE_VIDEO_PROFILE_MPEG4_ADVANCED_SIMPLE ||
           profile == PIPE_VIDEO_PROFILE_VC1_ADVANCED) &&
          sscreen->info.dec_caps.codec_info[codec - 1].valid) {
         return sscreen->info.dec_caps.codec_info[codec - 1].max_level;
      } else {
         switch (profile) {
         case PIPE_VIDEO_PROFILE_MPEG1:
            return 0;
         case PIPE_VIDEO_PROFILE_MPEG2_SIMPLE:
         case PIPE_VIDEO_PROFILE_MPEG2_MAIN:
            return 3;
         case PIPE_VIDEO_PROFILE_MPEG4_SIMPLE:
            return 3;
         case PIPE_VIDEO_PROFILE_MPEG4_ADVANCED_SIMPLE:
            return 5;
         case PIPE_VIDEO_PROFILE_VC1_SIMPLE:
            return 1;
         case PIPE_VIDEO_PROFILE_VC1_MAIN:
            return 2;
         case PIPE_VIDEO_PROFILE_VC1_ADVANCED:
            return 4;
         case PIPE_VIDEO_PROFILE_MPEG4_AVC_BASELINE:
         case PIPE_VIDEO_PROFILE_MPEG4_AVC_MAIN:
         case PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH:
            return (sscreen->info.family < CHIP_TONGA) ? 41 : 52;
         case PIPE_VIDEO_PROFILE_HEVC_MAIN:
         case PIPE_VIDEO_PROFILE_HEVC_MAIN_10:
            return 186;
         default:
            return 0;
         }
      }
   case PIPE_VIDEO_CAP_SUPPORTS_CONTIGUOUS_PLANES_MAP:
      return true;
   case PIPE_VIDEO_CAP_ROI_CROP_DEC:
      if (codec == PIPE_VIDEO_FORMAT_JPEG &&
          (sscreen->info.vcn_ip_version == VCN_4_0_3 ||
           sscreen->info.vcn_ip_version == VCN_5_0_1))
         return true;
      return false;
   case PIPE_VIDEO_CAP_SKIP_CLEAR_SURFACE:
      return sscreen->info.is_amdgpu && sscreen->info.drm_minor >= 59;
   default:
      return 0;
   }
}

static bool si_vid_is_format_supported(struct pipe_screen *screen, enum pipe_format format,
                                       enum pipe_video_profile profile,
                                       enum pipe_video_entrypoint entrypoint)
{
   struct si_screen *sscreen = (struct si_screen *)screen;

   if (sscreen->info.ip[AMD_IP_VPE].num_queues && entrypoint == PIPE_VIDEO_ENTRYPOINT_PROCESSING) {
      /* Todo:
       * Unable to confirm whether it is asking for an input or output type
       * Have to modify va frontend for solving this problem
       */
      /* VPE Supported input type */
      if ((format == PIPE_FORMAT_NV12) || (format == PIPE_FORMAT_NV21) || (format == PIPE_FORMAT_P010))
         return true;

      /* VPE Supported output type */
      if ((format == PIPE_FORMAT_A8R8G8B8_UNORM) || (format == PIPE_FORMAT_A8B8G8R8_UNORM) || (format == PIPE_FORMAT_R8G8B8A8_UNORM) ||
          (format == PIPE_FORMAT_B8G8R8A8_UNORM) || (format == PIPE_FORMAT_X8R8G8B8_UNORM) || (format == PIPE_FORMAT_X8B8G8R8_UNORM) ||
          (format == PIPE_FORMAT_R8G8B8X8_UNORM) || (format == PIPE_FORMAT_B8G8R8X8_UNORM) || (format == PIPE_FORMAT_A2R10G10B10_UNORM) ||
          (format == PIPE_FORMAT_A2B10G10R10_UNORM) || (format == PIPE_FORMAT_B10G10R10A2_UNORM) || (format == PIPE_FORMAT_R10G10B10A2_UNORM))
         return true;
   }

   /* HEVC 10 bit decoding should use P010 instead of NV12 if possible */
   if (profile == PIPE_VIDEO_PROFILE_HEVC_MAIN_10)
      return (format == PIPE_FORMAT_NV12) || (format == PIPE_FORMAT_P010) ||
             (format == PIPE_FORMAT_P016);

   /* Vp9 profile 2 supports 10 bit decoding using P016 */
   if (profile == PIPE_VIDEO_PROFILE_VP9_PROFILE2)
      return (format == PIPE_FORMAT_P010) || (format == PIPE_FORMAT_P016);

   if (profile == PIPE_VIDEO_PROFILE_AV1_MAIN && entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM)
      return (format == PIPE_FORMAT_P010) || (format == PIPE_FORMAT_P016) ||
             (format == PIPE_FORMAT_NV12);

   if (profile == PIPE_VIDEO_PROFILE_AV1_PROFILE2 && entrypoint == PIPE_VIDEO_ENTRYPOINT_BITSTREAM)
      return (format == PIPE_FORMAT_P010) || (format == PIPE_FORMAT_P016) ||
             (format == PIPE_FORMAT_P012) || (format == PIPE_FORMAT_NV12);

   /* JPEG supports YUV400 and YUV444 */
   if (profile == PIPE_VIDEO_PROFILE_JPEG_BASELINE) {
      switch (format) {
      case PIPE_FORMAT_NV12:
      case PIPE_FORMAT_YUYV:
      case PIPE_FORMAT_Y8_400_UNORM:
         return true;
      case PIPE_FORMAT_Y8_U8_V8_444_UNORM:
      case PIPE_FORMAT_Y8_U8_V8_440_UNORM:
         if (sscreen->info.vcn_ip_version >= VCN_2_0_0)
            return true;
         else
            return false;
      case PIPE_FORMAT_R8G8B8A8_UNORM:
      case PIPE_FORMAT_A8R8G8B8_UNORM:
      case PIPE_FORMAT_R8_G8_B8_UNORM:
         if (sscreen->info.vcn_ip_version == VCN_4_0_3 ||
             sscreen->info.vcn_ip_version == VCN_5_0_1)
            return true;
         else
            return false;
      default:
         return false;
      }
   }

   if ((entrypoint == PIPE_VIDEO_ENTRYPOINT_ENCODE) &&
          (((profile == PIPE_VIDEO_PROFILE_MPEG4_AVC_HIGH) &&
          (sscreen->info.vcn_ip_version >= VCN_2_0_0)) ||
          ((profile == PIPE_VIDEO_PROFILE_AV1_MAIN) &&
           (sscreen->info.vcn_ip_version >= VCN_4_0_0 &&
            sscreen->info.vcn_ip_version != VCN_4_0_3 &&
            sscreen->info.vcn_ip_version != VCN_5_0_1))))
      return (format == PIPE_FORMAT_P010 || format == PIPE_FORMAT_NV12);


   /* we can only handle this one with UVD */
   if (profile != PIPE_VIDEO_PROFILE_UNKNOWN)
      return format == PIPE_FORMAT_NV12;

   return vl_video_buffer_is_format_supported(screen, format, profile, entrypoint);
}

static bool si_vid_is_target_buffer_supported(struct pipe_screen *screen,
                                              enum pipe_format format,
                                              struct pipe_video_buffer *target,
                                              enum pipe_video_profile profile,
                                              enum pipe_video_entrypoint entrypoint)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   struct si_texture *tex = (struct si_texture *)((struct vl_video_buffer *)target)->resources[0];
   const bool is_dcc = tex->surface.meta_offset;
   const bool is_format_conversion = format != target->buffer_format;

   switch (entrypoint) {
   case PIPE_VIDEO_ENTRYPOINT_BITSTREAM:
      if (is_dcc || is_format_conversion)
         return false;
      break;

   case PIPE_VIDEO_ENTRYPOINT_ENCODE:
      if (is_dcc)
         return false;

      /* EFC */
      if (is_format_conversion) {
         const bool input_8bit =
            target->buffer_format == PIPE_FORMAT_B8G8R8A8_UNORM ||
            target->buffer_format == PIPE_FORMAT_B8G8R8X8_UNORM ||
            target->buffer_format == PIPE_FORMAT_R8G8B8A8_UNORM ||
            target->buffer_format == PIPE_FORMAT_R8G8B8X8_UNORM;
         const bool input_10bit =
            target->buffer_format == PIPE_FORMAT_B10G10R10A2_UNORM ||
            target->buffer_format == PIPE_FORMAT_B10G10R10X2_UNORM ||
            target->buffer_format == PIPE_FORMAT_R10G10B10A2_UNORM ||
            target->buffer_format == PIPE_FORMAT_R10G10B10X2_UNORM;

         if (sscreen->info.vcn_ip_version < VCN_2_0_0 ||
             sscreen->info.vcn_ip_version == VCN_2_2_0 ||
             sscreen->debug_flags & DBG(NO_EFC))
            return false;

         if (input_8bit && format != PIPE_FORMAT_NV12)
            return false;
         if (input_10bit && format != PIPE_FORMAT_NV12 && format != PIPE_FORMAT_P010)
            return false;
      }
      break;

   default:
      if (is_format_conversion)
         return false;
      break;
   }

   return si_vid_is_format_supported(screen, format, profile, entrypoint);
}

static uint64_t si_get_timestamp(struct pipe_screen *screen)
{
   struct si_screen *sscreen = (struct si_screen *)screen;

   return 1000000 * sscreen->ws->query_value(sscreen->ws, RADEON_TIMESTAMP) /
          sscreen->info.clock_crystal_freq;
}

static void si_query_memory_info(struct pipe_screen *screen, struct pipe_memory_info *info)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   struct radeon_winsys *ws = sscreen->ws;
   unsigned vram_usage, gtt_usage;

   info->total_device_memory = sscreen->info.vram_size_kb;
   info->total_staging_memory = sscreen->info.gart_size_kb;

   /* The real TTM memory usage is somewhat random, because:
    *
    * 1) TTM delays freeing memory, because it can only free it after
    *    fences expire.
    *
    * 2) The memory usage can be really low if big VRAM evictions are
    *    taking place, but the real usage is well above the size of VRAM.
    *
    * Instead, return statistics of this process.
    */
   vram_usage = ws->query_value(ws, RADEON_VRAM_USAGE) / 1024;
   gtt_usage = ws->query_value(ws, RADEON_GTT_USAGE) / 1024;

   info->avail_device_memory =
      vram_usage <= info->total_device_memory ? info->total_device_memory - vram_usage : 0;
   info->avail_staging_memory =
      gtt_usage <= info->total_staging_memory ? info->total_staging_memory - gtt_usage : 0;

   info->device_memory_evicted = ws->query_value(ws, RADEON_NUM_BYTES_MOVED) / 1024;

   if (sscreen->info.is_amdgpu)
      info->nr_device_memory_evictions = ws->query_value(ws, RADEON_NUM_EVICTIONS);
   else
      /* Just return the number of evicted 64KB pages. */
      info->nr_device_memory_evictions = info->device_memory_evicted / 64;
}

static struct disk_cache *si_get_disk_shader_cache(struct pipe_screen *pscreen)
{
   struct si_screen *sscreen = (struct si_screen *)pscreen;

   return sscreen->disk_shader_cache;
}

static void si_init_renderer_string(struct si_screen *sscreen)
{
   char first_name[256], second_name[32] = {}, kernel_version[128] = {};
   struct utsname uname_data;

   snprintf(first_name, sizeof(first_name), "%s",
            sscreen->info.marketing_name ? sscreen->info.marketing_name : sscreen->info.name);
   snprintf(second_name, sizeof(second_name), "%s, ", sscreen->info.lowercase_name);

   if (uname(&uname_data) == 0)
      snprintf(kernel_version, sizeof(kernel_version), ", %s", uname_data.release);

   const char *compiler_name =
#if AMD_LLVM_AVAILABLE
      !sscreen->use_aco ? "LLVM " MESA_LLVM_VERSION_STRING :
#endif
      "ACO";

   snprintf(sscreen->renderer_string, sizeof(sscreen->renderer_string),
            "%s (radeonsi, %s%s, DRM %i.%i%s)", first_name, second_name, compiler_name,
            sscreen->info.drm_major, sscreen->info.drm_minor, kernel_version);
}

static int si_get_screen_fd(struct pipe_screen *screen)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   struct radeon_winsys *ws = sscreen->ws;

   return ws->get_fd(ws);
}

static unsigned si_varying_expression_max_cost(nir_shader *producer, nir_shader *consumer)
{
   unsigned num_profiles = si_get_num_shader_profiles();

   for (unsigned i = 0; i < num_profiles; i++) {
      if (_mesa_printed_blake3_equal(consumer->info.source_blake3, si_shader_profiles[i].blake3)) {
         if (si_shader_profiles[i].options & SI_PROFILE_NO_OPT_UNIFORM_VARYINGS)
            return 0; /* only propagate constants */
         break;
      }
   }

   return ac_nir_varying_expression_max_cost(producer, consumer);
}


static void
si_driver_thread_add_job(struct pipe_screen *screen, void *data,
                         struct util_queue_fence *fence,
                         pipe_driver_thread_func execute,
                         pipe_driver_thread_func cleanup,
                         const size_t job_size)
{
   struct si_screen *sscreen = (struct si_screen *)screen;
   util_queue_add_job(&sscreen->shader_compiler_queue, data, fence, execute, cleanup, job_size);
}


void si_init_screen_get_functions(struct si_screen *sscreen)
{
   sscreen->b.get_name = si_get_name;
   sscreen->b.get_vendor = si_get_vendor;
   sscreen->b.get_device_vendor = si_get_device_vendor;
   sscreen->b.get_screen_fd = si_get_screen_fd;
   sscreen->b.is_compute_copy_faster = si_is_compute_copy_faster;
   sscreen->b.driver_thread_add_job = si_driver_thread_add_job;
   sscreen->b.get_timestamp = si_get_timestamp;
   sscreen->b.get_compiler_options = si_get_compiler_options;
   sscreen->b.get_device_uuid = si_get_device_uuid;
   sscreen->b.get_driver_uuid = si_get_driver_uuid;
   sscreen->b.query_memory_info = si_query_memory_info;
   sscreen->b.get_disk_shader_cache = si_get_disk_shader_cache;

   if (sscreen->info.ip[AMD_IP_UVD].num_queues ||
       ((sscreen->info.vcn_ip_version >= VCN_4_0_0) ?
	 sscreen->info.ip[AMD_IP_VCN_UNIFIED].num_queues : sscreen->info.ip[AMD_IP_VCN_DEC].num_queues) ||
       sscreen->info.ip[AMD_IP_VCN_JPEG].num_queues || sscreen->info.ip[AMD_IP_VCE].num_queues ||
       sscreen->info.ip[AMD_IP_UVD_ENC].num_queues || sscreen->info.ip[AMD_IP_VCN_ENC].num_queues ||
       sscreen->info.ip[AMD_IP_VPE].num_queues) {
      sscreen->b.get_video_param = si_get_video_param;
      sscreen->b.is_video_format_supported = si_vid_is_format_supported;
      sscreen->b.is_video_target_buffer_supported = si_vid_is_target_buffer_supported;
   } else {
      sscreen->b.get_video_param = si_get_video_param_no_video_hw;
      sscreen->b.is_video_format_supported = vl_video_buffer_is_format_supported;
   }

   si_init_renderer_string(sscreen);

   /*        |---------------------------------- Performance & Availability --------------------------------|
    *        |MAD/MAC/MADAK/MADMK|MAD_LEGACY|MAC_LEGACY|    FMA     |FMAC/FMAAK/FMAMK|FMA_LEGACY|PK_FMA_F16,|Best choice
    * Arch   |    F32,F16,F64    | F32,F16  | F32,F16  |F32,F16,F64 |    F32,F16     |   F32    |PK_FMAC_F16|F16,F32,F64
    * ------------------------------------------------------------------------------------------------------------------
    * gfx6,7 |     1 , - , -     |  1 , -   |  1 , -   |1/4, - ,1/16|     - , -      |    -     |   - , -   | - ,MAD,FMA
    * gfx8   |     1 , 1 , -     |  1 , -   |  - , -   |1/4, 1 ,1/16|     - , -      |    -     |   - , -   |MAD,MAD,FMA
    * gfx9   |     1 ,1|0, -     |  1 , -   |  - , -   | 1 , 1 ,1/16|    0|1, -      |    -     |   2 , -   |FMA,MAD,FMA
    * gfx10  |     1 , - , -     |  1 , -   |  1 , -   | 1 , 1 ,1/16|     1 , 1      |    -     |   2 , 2   |FMA,MAD,FMA
    * gfx10.3|     - , - , -     |  - , -   |  - , -   | 1 , 1 ,1/16|     1 , 1      |    1     |   2 , 2   |  all FMA
    * gfx11  |     - , - , -     |  - , -   |  - , -   | 2 , 2 ,1/16|     2 , 2      |    2     |   2 , 2   |  all FMA
    *
    * Tahiti, Hawaii, Carrizo, Vega20: FMA_F32 is full rate, FMA_F64 is 1/4
    * gfx9 supports MAD_F16 only on Vega10, Raven, Raven2, Renoir.
    * gfx9 supports FMAC_F32 only on Vega20, but doesn't support FMAAK and FMAMK.
    *
    * gfx8 prefers MAD for F16 because of MAC/MADAK/MADMK.
    * gfx9 and newer prefer FMA for F16 because of the packed instruction.
    * gfx10 and older prefer MAD for F32 because of the legacy instruction.
    */
   bool use_fma32 =
      sscreen->info.gfx_level >= GFX10_3 ||
      (sscreen->info.family >= CHIP_GFX940 && !sscreen->info.has_graphics) ||
      /* fma32 is too slow for gpu < gfx9, so apply the option only for gpu >= gfx9 */
      (sscreen->info.gfx_level >= GFX9 && sscreen->options.force_use_fma32);
   bool has_mediump = sscreen->info.gfx_level >= GFX8 && sscreen->options.fp16;

   nir_shader_compiler_options *options = sscreen->nir_options;
   ac_nir_set_options(&sscreen->info, !sscreen->use_aco, options);

   options->lower_ffma16 = sscreen->info.gfx_level < GFX9;
   options->lower_ffma32 = !use_fma32;
   options->lower_ffma64 = false;
   options->fuse_ffma16 = sscreen->info.gfx_level >= GFX9;
   options->fuse_ffma32 = use_fma32;
   options->fuse_ffma64 = true;
   options->lower_uniforms_to_ubo = true;
   options->lower_to_scalar = true;
   options->lower_to_scalar_filter =
      sscreen->info.has_packed_math_16bit ? si_alu_to_scalar_packed_math_filter : NULL;
   options->max_unroll_iterations = 128;
   options->max_unroll_iterations_aggressive = 128;
   /* For OpenGL, rounding mode is undefined. We want fast packing with v_cvt_pkrtz_f16,
    * but if we use it, all f32->f16 conversions have to round towards zero,
    * because both scalar and vec2 down-conversions have to round equally.
    *
    * For OpenCL, rounding mode is explicit. This will only lower f2f16 to f2f16_rtz
    * when execution mode is rtz instead of rtne.
    */
   options->force_f2f16_rtz = true;
   options->io_options |= (!has_mediump ? nir_io_mediump_is_32bit : 0) | nir_io_has_intrinsics;
   options->lower_mediump_io = has_mediump ? si_lower_mediump_io : NULL;
   /* HW supports indirect indexing for: | Enabled in driver
    * -------------------------------------------------------
    * TCS inputs                         | Yes
    * TES inputs                         | Yes
    * GS inputs                          | No
    * -------------------------------------------------------
    * VS outputs before TCS              | No
    * TCS outputs                        | Yes
    * VS/TES outputs before GS           | No
    */
   options->support_indirect_inputs = BITFIELD_BIT(MESA_SHADER_TESS_CTRL) |
                                      BITFIELD_BIT(MESA_SHADER_TESS_EVAL);
   options->support_indirect_outputs = BITFIELD_BIT(MESA_SHADER_TESS_CTRL);
   options->varying_expression_max_cost = si_varying_expression_max_cost;
}

void si_init_shader_caps(struct si_screen *sscreen)
{
   for (unsigned i = 0; i <= PIPE_SHADER_COMPUTE; i++) {
      struct pipe_shader_caps *caps =
         (struct pipe_shader_caps *)&sscreen->b.shader_caps[i];

      /* Shader limits. */
      caps->max_instructions =
      caps->max_alu_instructions =
      caps->max_tex_instructions =
      caps->max_tex_indirections =
      caps->max_control_flow_depth = 16384;
      caps->max_inputs = i == PIPE_SHADER_VERTEX ? SI_MAX_ATTRIBS : 32;
      caps->max_outputs = i == PIPE_SHADER_FRAGMENT ? 8 : 32;
      caps->max_temps = 256; /* Max native temporaries. */
      caps->max_const_buffer0_size = 1 << 26; /* 64 MB */
      caps->max_const_buffers = SI_NUM_CONST_BUFFERS;
      caps->max_texture_samplers =
      caps->max_sampler_views = SI_NUM_SAMPLERS;
      caps->max_shader_buffers = SI_NUM_SHADER_BUFFERS;
      caps->max_shader_images = SI_NUM_IMAGES;

      caps->supported_irs = (1 << PIPE_SHADER_IR_TGSI) | (1 << PIPE_SHADER_IR_NIR);
      if (i == PIPE_SHADER_COMPUTE)
         caps->supported_irs |= 1 << PIPE_SHADER_IR_NATIVE;

      /* Supported boolean features. */
      caps->cont_supported = true;
      caps->tgsi_sqrt_supported = true;
      caps->indirect_temp_addr = true;
      caps->indirect_const_addr = true;
      caps->integers = true;
      caps->int64_atomics = true;
      caps->tgsi_any_inout_decl_range = true;

      /* We need f16c for fast FP16 conversions in glUniform. */
      caps->fp16_const_buffers =
         util_get_cpu_caps()->has_f16c && sscreen->nir_options->lower_mediump_io;

      caps->fp16 =
      caps->fp16_derivatives =
      caps->glsl_16bit_consts =
      caps->int16 = sscreen->nir_options->lower_mediump_io != NULL;
   }
}

void si_init_compute_caps(struct si_screen *sscreen)
{
   struct pipe_compute_caps *caps =
      (struct pipe_compute_caps *)&sscreen->b.compute_caps;

   snprintf(caps->ir_target, sizeof(caps->ir_target), "%s-amdgcn-mesa-mesa3d",
            ac_get_llvm_processor_name(sscreen->info.family));

   caps->grid_dimension = 3;

   /* Use this size, so that internal counters don't overflow 64 bits. */
   caps->max_grid_size[0] = UINT32_MAX;
   caps->max_grid_size[1] = UINT16_MAX;
   caps->max_grid_size[2] = UINT16_MAX;

   caps->max_block_size[0] =
   caps->max_block_size[1] =
   caps->max_block_size[2] = 1024;

   caps->max_block_size_clover[0] =
   caps->max_block_size_clover[1] =
   caps->max_block_size_clover[2] = 256;

   caps->max_threads_per_block = 1024;
   caps->max_threads_per_block_clover = 256;
   caps->address_bits = 64;

   /* Return 1/4 of the heap size as the maximum because the max size is not practically
    * allocatable.
    */
   caps->max_mem_alloc_size = (sscreen->info.max_heap_size_kb / 4) * 1024ull;

   /* In OpenCL, the MAX_MEM_ALLOC_SIZE must be at least
    * 1/4 of the MAX_GLOBAL_SIZE.  Since the
    * MAX_MEM_ALLOC_SIZE is fixed for older kernels,
    * make sure we never report more than
    * 4 * MAX_MEM_ALLOC_SIZE.
    */
   caps->max_global_size = MIN2(4 * caps->max_mem_alloc_size,
                                sscreen->info.max_heap_size_kb * 1024ull);

   /* Value reported by the closed source driver. */
   caps->max_local_size = sscreen->info.gfx_level == GFX6 ? 32 * 1024 : 64 * 1024;
   caps->max_input_size = 1024;

   caps->max_clock_frequency = sscreen->info.max_gpu_freq_mhz;
   caps->max_compute_units = sscreen->info.num_cu;

   unsigned threads = 1024;
   unsigned subgroup_size =
      sscreen->debug_flags & DBG(W64_CS) || sscreen->info.gfx_level < GFX10 ? 64 : 32;
   caps->max_subgroups = threads / subgroup_size;

   if (sscreen->debug_flags & DBG(W32_CS))
      caps->subgroup_sizes = 32;
   else if (sscreen->debug_flags & DBG(W64_CS))
      caps->subgroup_sizes = 64;
   else
      caps->subgroup_sizes = sscreen->info.gfx_level < GFX10 ? 64 : 64 | 32;

   caps->max_variable_threads_per_block = SI_MAX_VARIABLE_THREADS_PER_BLOCK;
}

void si_init_screen_caps(struct si_screen *sscreen)
{
   struct pipe_caps *caps = (struct pipe_caps *)&sscreen->b.caps;

   u_init_pipe_screen_caps(&sscreen->b, 1);

   /* Gfx8 (Polaris11) hangs, so don't enable this on Gfx8 and older chips. */
   bool enable_sparse =
      sscreen->info.gfx_level >= GFX9 && sscreen->info.gfx_level < GFX12 &&
      sscreen->info.has_sparse_vm_mappings;

   /* Supported features (boolean caps). */
   caps->max_dual_source_render_targets = true;
   caps->anisotropic_filter = true;
   caps->occlusion_query = true;
   caps->texture_mirror_clamp = true;
   caps->texture_shadow_lod = true;
   caps->texture_mirror_clamp_to_edge = true;
   caps->blend_equation_separate = true;
   caps->texture_swizzle = true;
   caps->depth_clip_disable = true;
   caps->depth_clip_disable_separate = true;
   caps->shader_stencil_export = true;
   caps->vertex_element_instance_divisor = true;
   caps->fs_coord_origin_upper_left = true;
   caps->fs_coord_pixel_center_half_integer = true;
   caps->fs_coord_pixel_center_integer = true;
   caps->fragment_shader_texture_lod = true;
   caps->fragment_shader_derivatives = true;
   caps->primitive_restart = true;
   caps->primitive_restart_fixed_index = true;
   caps->conditional_render = true;
   caps->texture_barrier = true;
   caps->indep_blend_enable = true;
   caps->indep_blend_func = true;
   caps->vertex_color_unclamped = true;
   caps->start_instance = true;
   caps->npot_textures = true;
   caps->mixed_framebuffer_sizes = true;
   caps->mixed_color_depth_bits = true;
   caps->vertex_color_clamped = true;
   caps->fragment_color_clamped = true;
   caps->vs_instanceid = true;
   caps->compute = true;
   caps->texture_buffer_objects = true;
   caps->vs_layer_viewport = true;
   caps->query_pipeline_statistics = true;
   caps->sample_shading = true;
   caps->draw_indirect = true;
   caps->clip_halfz = true;
   caps->vs_window_space_position = true;
   caps->polygon_offset_clamp = true;
   caps->multisample_z_resolve = true;
   caps->quads_follow_provoking_vertex_convention = true;
   caps->tgsi_texcoord = true;
   caps->fs_fine_derivative = true;
   caps->conditional_render_inverted = true;
   caps->texture_float_linear = true;
   caps->texture_half_float_linear = true;
   caps->depth_bounds_test = true;
   caps->sampler_view_target = true;
   caps->texture_query_lod = true;
   caps->texture_gather_sm5 = true;
   caps->texture_query_samples = true;
   caps->force_persample_interp = true;
   caps->copy_between_compressed_and_plain_formats = true;
   caps->fs_position_is_sysval = true;
   caps->fs_face_is_integer_sysval = true;
   caps->invalidate_buffer = true;
   caps->surface_reinterpret_blocks = true;
   caps->query_buffer_object = true;
   caps->query_memory_info = true;
   caps->shader_pack_half_float = true;
   caps->framebuffer_no_attachment = true;
   caps->robust_buffer_access_behavior = true;
   caps->polygon_offset_units_unscaled = true;
   caps->string_marker = true;
   caps->cull_distance = true;
   caps->shader_array_components = true;
   caps->stream_output_pause_resume = true;
   caps->stream_output_interleave_buffers = true;
   caps->doubles = true;
   caps->tgsi_tex_txf_lz = true;
   caps->tes_layer_viewport = true;
   caps->bindless_texture = true;
   caps->query_timestamp = true;
   caps->query_time_elapsed = true;
   caps->nir_samplers_as_deref = true;
   caps->memobj = true;
   caps->load_constbuf = true;
   caps->int64 = true;
   caps->shader_clock = true;
   caps->can_bind_const_buffer_as_vertex = true;
   caps->allow_mapped_buffers_during_execution = true;
   caps->signed_vertex_buffer_offset = true;
   caps->shader_ballot = true;
   caps->shader_group_vote = true;
   caps->compute_grid_info_last_block = true;
   caps->image_load_formatted = true;
   caps->prefer_compute_for_multimedia = true;
   caps->tgsi_div = true;
   caps->packed_uniforms = true;
   caps->gl_spirv = true;
   caps->alpha_to_coverage_dither_control = true;
   caps->map_unsynchronized_thread_safe = true;
   caps->no_clip_on_copy_tex = true;
   caps->shader_atomic_int64 = true;
   caps->frontend_noop = true;
   caps->demote_to_helper_invocation = true;
   caps->prefer_real_buffer_in_constbuf0 = true;
   caps->compute_shader_derivatives = true;
   caps->image_atomic_inc_wrap = true;
   caps->image_store_formatted = true;
   caps->allow_draw_out_of_order = true;
   caps->query_so_overflow = true;
   caps->glsl_tess_levels_as_inputs = true;
   caps->device_reset_status_query = true;
   caps->texture_multisample = true;
   caps->allow_glthread_buffer_subdata_opt = true; /* TODO: remove if it's slow */
   caps->null_textures = true;
   caps->has_const_bw = true;
   caps->cl_gl_sharing = true;
   caps->call_finalize_nir_in_linker = true;

   caps->fbfetch = 1;

   /* Tahiti and Verde only: reduction mode is unsupported due to a bug
    * (it might work sometimes, but that's not enough)
    */
   caps->sampler_reduction_minmax =
   caps->sampler_reduction_minmax_arb =
      !(sscreen->info.family == CHIP_TAHITI || sscreen->info.family == CHIP_VERDE);

   caps->texture_transfer_modes =
      PIPE_TEXTURE_TRANSFER_BLIT | PIPE_TEXTURE_TRANSFER_COMPUTE;

   caps->draw_vertex_state = !(sscreen->debug_flags & DBG(NO_FAST_DISPLAY_LIST));

   caps->shader_samples_identical =
      sscreen->info.gfx_level < GFX11 && !(sscreen->debug_flags & DBG(NO_FMASK));

   caps->glsl_zero_init = 2;

   caps->generate_mipmap =
   caps->seamless_cube_map =
   caps->seamless_cube_map_per_texture =
   caps->cube_map_array =
      sscreen->info.has_3d_cube_border_color_mipmap;

   caps->post_depth_coverage = sscreen->info.gfx_level >= GFX10;

   caps->graphics = sscreen->info.has_graphics;

   caps->resource_from_user_memory = !UTIL_ARCH_BIG_ENDIAN && sscreen->info.has_userptr;

   caps->device_protected_surface = sscreen->info.has_tmz_support;

   caps->min_map_buffer_alignment = SI_MAP_BUFFER_ALIGNMENT;

   caps->max_vertex_buffers = SI_MAX_ATTRIBS;

   caps->constant_buffer_offset_alignment =
   caps->texture_buffer_offset_alignment =
   caps->max_texture_gather_components =
   caps->max_stream_output_buffers =
   caps->max_vertex_streams =
   caps->shader_buffer_offset_alignment =
   caps->max_window_rectangles = 4;

   caps->glsl_feature_level =
   caps->glsl_feature_level_compatibility = 460;

   /* Optimal number for good TexSubImage performance on Polaris10. */
   caps->max_texture_upload_memory_budget = 64 * 1024 * 1024;

   caps->gl_begin_end_buffer_size = 4096 * 1024;

   /* Return 1/4th of the heap size as the maximum because the max size is not practically
    * allocatable. Also, this can only return UINT32_MAX at most.
    */
   unsigned max_size = MIN2((sscreen->info.max_heap_size_kb * 1024ull) / 4, UINT32_MAX);

   /* Allow max 512 MB to pass CTS with a 32-bit build. */
   if (sizeof(void*) == 4)
      max_size = MIN2(max_size, 512 * 1024 * 1024);

   caps->max_constant_buffer_size =
   caps->max_shader_buffer_size = max_size;

   unsigned max_texels = caps->max_shader_buffer_size;

   /* FYI, BUF_RSRC_WORD2.NUM_RECORDS field limit is UINT32_MAX. */

   /* Gfx8 and older use the size in bytes for bounds checking, and the max element size
    * is 16B. Gfx9 and newer use the VGPR index for bounds checking.
    */
   if (sscreen->info.gfx_level <= GFX8)
      max_texels = MIN2(max_texels, UINT32_MAX / 16);
   else
      /* Gallium has a limitation that it can only bind UINT32_MAX bytes, not texels.
       * TODO: Remove this after the gallium interface is changed. */
      max_texels = MIN2(max_texels, UINT32_MAX / 16);

   caps->max_texel_buffer_elements = max_texels;

   /* Allow 1/4th of the heap size. */
   caps->max_texture_mb = sscreen->info.max_heap_size_kb / 1024 / 4;

   caps->prefer_back_buffer_reuse = false;
   caps->uma = false;
   caps->prefer_imm_arrays_as_constbuf = false;

   caps->performance_monitor =
      sscreen->info.gfx_level >= GFX7 && sscreen->info.gfx_level <= GFX10_3;

   caps->sparse_buffer_page_size = enable_sparse ? RADEON_SPARSE_PAGE_SIZE : 0;

   caps->context_priority_mask = sscreen->info.is_amdgpu ?
      PIPE_CONTEXT_PRIORITY_LOW | PIPE_CONTEXT_PRIORITY_MEDIUM | PIPE_CONTEXT_PRIORITY_HIGH : 0;

   caps->fence_signal = sscreen->info.has_syncobj;

   caps->constbuf0_flags = SI_RESOURCE_FLAG_32BIT;

   caps->native_fence_fd = sscreen->info.has_fence_to_handle;

   caps->draw_parameters =
   caps->multi_draw_indirect =
   caps->multi_draw_indirect_params = sscreen->has_draw_indirect_multi;

   caps->max_shader_patch_varyings = 30;

   caps->max_varyings =
   caps->max_gs_invocations = 32;

   caps->texture_border_color_quirk =
      sscreen->info.gfx_level <= GFX8 ? PIPE_QUIRK_TEXTURE_BORDER_COLOR_SWIZZLE_R600 : 0;

   /* Stream output. */
   caps->max_stream_output_separate_components =
   caps->max_stream_output_interleaved_components = 32 * 4;

   /* gfx9 has to report 256 to make piglit/gs-max-output pass.
    * gfx8 and earlier can do 1024.
    */
   caps->max_geometry_output_vertices = 256;
   caps->max_geometry_total_output_components = 4095;

   caps->max_vertex_attrib_stride = 2048;

   /* TODO: Gfx12 supports 64K textures, but Gallium can't represent them at the moment. */
   caps->max_texture_2d_size = sscreen->info.gfx_level >= GFX12 ? 32768 : 16384;
   caps->max_texture_cube_levels = sscreen->info.has_3d_cube_border_color_mipmap ?
      (sscreen->info.gfx_level >= GFX12 ? 16 : 15) /* 32K : 16K */ : 0;
   caps->max_texture_3d_levels = sscreen->info.has_3d_cube_border_color_mipmap ?
      /* This is limited by maximums that both the texture unit and layered rendering support. */
      (sscreen->info.gfx_level >= GFX12 ? 15 : /* 16K */
       (sscreen->info.gfx_level >= GFX10 ? 14 : 12)) /* 8K : 2K */ : 0;
   /* This is limited by maximums that both the texture unit and layered rendering support. */
   caps->max_texture_array_layers = sscreen->info.gfx_level >= GFX10 ? 8192 : 2048;

   /* Sparse texture */
   caps->max_sparse_texture_size = enable_sparse ? caps->max_texture_2d_size : 0;
   caps->max_sparse_3d_texture_size = enable_sparse ? (1 << (caps->max_texture_3d_levels - 1)) : 0;
   caps->max_sparse_array_texture_layers = enable_sparse ? caps->max_texture_array_layers : 0;
   caps->sparse_texture_full_array_cube_mipmaps =
   caps->query_sparse_texture_residency =
   caps->clamp_sparse_texture_lod = enable_sparse;

   /* Viewports and render targets. */
   caps->max_viewports = SI_MAX_VIEWPORTS;
   caps->viewport_subpixel_bits =
   caps->rasterizer_subpixel_bits =
   caps->max_render_targets = 8;
   caps->framebuffer_msaa_constraints = sscreen->info.has_eqaa_surface_allocator ? 2 : 0;

   caps->min_texture_gather_offset =
   caps->min_texel_offset = -32;

   caps->max_texture_gather_offset =
   caps->max_texel_offset = 31;

   caps->endianness = PIPE_ENDIAN_LITTLE;

   caps->vendor_id = ATI_VENDOR_ID;
   caps->device_id = sscreen->info.pci_id;
   caps->video_memory = sscreen->info.vram_size_kb >> 10;
   caps->pci_group = sscreen->info.pci.domain;
   caps->pci_bus = sscreen->info.pci.bus;
   caps->pci_device = sscreen->info.pci.dev;
   caps->pci_function = sscreen->info.pci.func;

   /* Conversion to nanos from cycles per millisecond */
   caps->timer_resolution = DIV_ROUND_UP(1000000, sscreen->info.clock_crystal_freq);

   caps->shader_subgroup_size = 64;
   caps->shader_subgroup_supported_stages = BITFIELD_MASK(PIPE_SHADER_TYPES);
   caps->shader_subgroup_supported_features = BITFIELD_MASK(PIPE_SHADER_SUBGROUP_NUM_FEATURES);
   caps->shader_subgroup_quad_all_stages = true;

   caps->min_line_width =
   caps->min_line_width_aa = 1; /* due to axis-aligned end caps at line width 1 */

   caps->min_point_size =
   caps->min_point_size_aa =
   caps->point_size_granularity =
   caps->line_width_granularity = 1.0 / 8.0; /* due to the register field precision */

   /* This depends on the quant mode, though the precise interactions are unknown. */
   caps->max_line_width =
   caps->max_line_width_aa = 2048;

   caps->max_point_size =
   caps->max_point_size_aa = SI_MAX_POINT_SIZE;

   caps->max_texture_anisotropy = 16.0f;

   /* The hw can do 31, but this test fails if we use that:
    *    KHR-GL46.texture_lod_bias.texture_lod_bias_all
    */
   caps->max_texture_lod_bias = 16;
}
