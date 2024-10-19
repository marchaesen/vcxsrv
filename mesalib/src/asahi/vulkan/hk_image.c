/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "hk_image.h"
#include "asahi/layout/layout.h"
#include "drm-uapi/drm_fourcc.h"
#include "util/bitscan.h"
#include "util/format/u_format.h"
#include "util/format/u_formats.h"
#include "util/macros.h"
#include "util/u_math.h"
#include "vulkan/vulkan_core.h"

#include "hk_device.h"
#include "hk_device_memory.h"
#include "hk_entrypoints.h"
#include "hk_physical_device.h"

#include "vk_format.h"

/* Minimum alignment encodable for our descriptors. The hardware texture/PBE
 * descriptors require 16-byte alignment. Our software PBE atomic descriptor
 * requires 128-byte alignment, but we could relax that one if we wanted.
 */
#define HK_PLANE_ALIGN_B 128

static VkFormatFeatureFlags2
hk_get_image_plane_format_features(struct hk_physical_device *pdev,
                                   VkFormat vk_format, VkImageTiling tiling)
{
   VkFormatFeatureFlags2 features = 0;

   /* Conformance fails with these optional formats. Just drop them for now.
    * TODO: Investigate later if we have a use case.
    */
   switch (vk_format) {
   case VK_FORMAT_A1B5G5R5_UNORM_PACK16_KHR:
   case VK_FORMAT_A8_UNORM_KHR:
      return 0;
   default:
      break;
   }

   enum pipe_format p_format = hk_format_to_pipe_format(vk_format);
   if (p_format == PIPE_FORMAT_NONE)
      return 0;

   /* NPOT formats only supported for texel buffers */
   if (!util_is_power_of_two_nonzero(util_format_get_blocksize(p_format)))
      return 0;

   if (util_format_is_compressed(p_format)) {
      /* Linear block-compressed images are all sorts of problematic, not sure
       * if AGX even supports them. Don't try.
       */
      if (tiling != VK_IMAGE_TILING_OPTIMAL)
         return 0;

      /* XXX: Conformance fails, e.g.:
       * dEQP-VK.pipeline.monolithic.sampler.view_type.2d.format.etc2_r8g8b8a1_unorm_block.mipmap.linear.lod.select_bias_3_7
       *
       * I suspect ail bug with mipmapping of compressed :-/
       */
      switch (util_format_description(p_format)->layout) {
      case UTIL_FORMAT_LAYOUT_ETC:
      case UTIL_FORMAT_LAYOUT_ASTC:
         return 0;
      default:
         break;
      }
   }

   if (ail_pixel_format[p_format].texturable) {
      features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;
      features |= VK_FORMAT_FEATURE_2_BLIT_SRC_BIT;

      /* We can sample integer formats but it doesn't make sense to linearly
       * filter them.
       */
      if (!util_format_is_pure_integer(p_format)) {
         features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
      }

      if (vk_format_has_depth(vk_format)) {
         features |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT;
      }
   }

   if (ail_pixel_format[p_format].renderable) {
      /* For now, disable snorm rendering due to nir_lower_blend bugs.
       *
       * TODO: revisit.
       */
      if (!util_format_is_snorm(p_format)) {
         features |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT;
         features |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT;
      }

      features |= VK_FORMAT_FEATURE_2_BLIT_DST_BIT;
      features |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT |
                  VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT |
                  VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT;
   }

   if (vk_format_is_depth_or_stencil(vk_format)) {
      if (!(p_format == PIPE_FORMAT_Z32_FLOAT ||
            p_format == PIPE_FORMAT_S8_UINT ||
            p_format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT ||
            p_format == PIPE_FORMAT_Z16_UNORM) ||
          tiling == VK_IMAGE_TILING_LINEAR)
         return 0;

      features |= VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;
   }

   /* Our image atomic lowering doesn't bother to handle linear */
   if ((p_format == PIPE_FORMAT_R32_UINT || p_format == PIPE_FORMAT_R32_SINT) &&
       tiling == VK_IMAGE_TILING_OPTIMAL) {

      features |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_ATOMIC_BIT;
   }

   if (features != 0) {
      features |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT;
      features |= VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;
      features |= VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT_EXT;
   }

   return features;
}

VkFormatFeatureFlags2
hk_get_image_format_features(struct hk_physical_device *pdev,
                             VkFormat vk_format, VkImageTiling tiling)
{
   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(vk_format);
   if (ycbcr_info == NULL)
      return hk_get_image_plane_format_features(pdev, vk_format, tiling);

   /* For multi-plane, we get the feature flags of each plane separately,
    * then take their intersection as the overall format feature flags
    */
   VkFormatFeatureFlags2 features = ~0ull;
   bool cosited_chroma = false;
   for (uint8_t plane = 0; plane < ycbcr_info->n_planes; plane++) {
      const struct vk_format_ycbcr_plane *plane_info =
         &ycbcr_info->planes[plane];
      features &=
         hk_get_image_plane_format_features(pdev, plane_info->format, tiling);
      if (plane_info->denominator_scales[0] > 1 ||
          plane_info->denominator_scales[1] > 1)
         cosited_chroma = true;
   }
   if (features == 0)
      return 0;

   /* Uh... We really should be able to sample from YCbCr */
   assert(features & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT);
   assert(features & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT);

   /* These aren't allowed for YCbCr formats */
   features &=
      ~(VK_FORMAT_FEATURE_2_BLIT_SRC_BIT | VK_FORMAT_FEATURE_2_BLIT_DST_BIT |
        VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT |
        VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT);

   /* This is supported on all YCbCr formats */
   features |=
      VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT;

   if (ycbcr_info->n_planes > 1) {
      /* DISJOINT_BIT implies that each plane has its own separate binding,
       * while SEPARATE_RECONSTRUCTION_FILTER_BIT implies that luma and chroma
       * each have their own, separate filters, so these two bits make sense
       * for multi-planar formats only.
       *
       * For MIDPOINT_CHROMA_SAMPLES_BIT, NVIDIA HW on single-plane interleaved
       * YCbCr defaults to COSITED_EVEN, which is inaccurate and fails tests.
       * This can be fixed with a NIR tweak but for now, we only enable this bit
       * for multi-plane formats. See Issue #9525 on the mesa/main tracker.
       */
      features |=
         VK_FORMAT_FEATURE_DISJOINT_BIT |
         VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT |
         VK_FORMAT_FEATURE_2_MIDPOINT_CHROMA_SAMPLES_BIT;
   }

   if (cosited_chroma)
      features |= VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT;

   return features;
}

static VkFormatFeatureFlags2
vk_image_usage_to_format_features(VkImageUsageFlagBits usage_flag)
{
   assert(util_bitcount(usage_flag) == 1);
   switch (usage_flag) {
   case VK_IMAGE_USAGE_TRANSFER_SRC_BIT:
      return VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT |
             VK_FORMAT_FEATURE_BLIT_SRC_BIT;
   case VK_IMAGE_USAGE_TRANSFER_DST_BIT:
      return VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT |
             VK_FORMAT_FEATURE_BLIT_DST_BIT;
   case VK_IMAGE_USAGE_SAMPLED_BIT:
      return VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT;
   case VK_IMAGE_USAGE_STORAGE_BIT:
      return VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT;
   case VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT:
      return VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT;
   case VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT:
      return VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;
   default:
      return 0;
   }
}

static bool
hk_can_compress(struct agx_device *dev, VkFormat format, unsigned plane,
                unsigned width, unsigned height, unsigned samples,
                VkImageCreateFlagBits flags, VkImageUsageFlagBits usage,
                const void *pNext)
{
   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(format);

   if (ycbcr_info) {
      format = ycbcr_info->planes[plane].format;
      width /= ycbcr_info->planes[plane].denominator_scales[0];
      height /= ycbcr_info->planes[plane].denominator_scales[0];
   } else if (format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
      format = (plane == 0) ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_S8_UINT;
   }

   /* Allow disabling compression for debugging */
   if (dev->debug & AGX_DBG_NOCOMPRESS)
      return false;

   /* Image compression is not (yet?) supported with host image copies,
    * although the vendor driver does support something similar if I recall.
    * Compression is not supported in hardware for storage images or mutable
    * formats in general.
    *
    * Feedback loops are problematic with compression. The GL driver bans them.
    * Interestingly, the relevant CTS tests pass on G13G and G14C, but not on
    * G13D. For now, conservatively ban compression with feedback loops.
    */
   if (usage &
       (VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT | VK_IMAGE_USAGE_STORAGE_BIT |
        VK_IMAGE_USAGE_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT)) {

      perf_debug_dev(
         dev, "No compression: incompatible usage -%s%s%s",
         (usage & VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT) ? " host-transfer" : "",
         (usage & VK_IMAGE_USAGE_STORAGE_BIT) ? " storage" : "",
         (usage & VK_IMAGE_USAGE_ATTACHMENT_FEEDBACK_LOOP_BIT_EXT)
            ? " feedback-loop"
            : "");
      return false;
   }

   enum pipe_format p_format = hk_format_to_pipe_format(format);

   /* Check for format compatibility if mutability is enabled. */
   if (flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) {
      const struct VkImageFormatListCreateInfo *format_list =
         (void *)vk_find_struct_const(pNext, IMAGE_FORMAT_LIST_CREATE_INFO);

      if (!format_list || format_list->viewFormatCount == 0)
         return false;

      for (unsigned i = 0; i < format_list->viewFormatCount; ++i) {
         if (format_list->pViewFormats[i] == VK_FORMAT_UNDEFINED)
            continue;

         enum pipe_format view_format =
            hk_format_to_pipe_format(format_list->pViewFormats[i]);

         if (!ail_formats_compatible(p_format, view_format)) {
            perf_debug_dev(dev, "No compression: incompatible image view");
            return false;
         }
      }
   }

   if (!ail_can_compress(p_format, width, height, samples)) {
      perf_debug_dev(dev, "No compression: invalid layout %s %ux%ux%u",
                     util_format_short_name(p_format), width, height, samples);
      return false;
   }

   return true;
}

static bool
hk_can_compress_format(struct agx_device *dev, VkFormat format)
{
   /* Check compressability of a sufficiently large image of the same
    * format, since we don't have dimensions here. This is lossy for
    * small images, but that's ok.
    *
    * Likewise, we do not set flags as flags only disable compression.
    */
   return hk_can_compress(dev, format, 0, 64, 64, 1, 0, 0, NULL);
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_GetPhysicalDeviceImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
   VkImageFormatProperties2 *pImageFormatProperties)
{
   VK_FROM_HANDLE(hk_physical_device, pdev, physicalDevice);

   const VkPhysicalDeviceExternalImageFormatInfo *external_info =
      vk_find_struct_const(pImageFormatInfo->pNext,
                           PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO);

   /* Initialize to zero in case we return VK_ERROR_FORMAT_NOT_SUPPORTED */
   memset(&pImageFormatProperties->imageFormatProperties, 0,
          sizeof(pImageFormatProperties->imageFormatProperties));

   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(pImageFormatInfo->format);

   /* For the purposes of these checks, we don't care about all the extra
    * YCbCr features and we just want the accumulation of features available
    * to all planes of the given format.
    */
   VkFormatFeatureFlags2 features;
   if (ycbcr_info == NULL) {
      features = hk_get_image_plane_format_features(
         pdev, pImageFormatInfo->format, pImageFormatInfo->tiling);
   } else {
      features = ~0ull;
      assert(ycbcr_info->n_planes > 0);
      for (uint8_t plane = 0; plane < ycbcr_info->n_planes; plane++) {
         const VkFormat plane_format = ycbcr_info->planes[plane].format;
         features &= hk_get_image_plane_format_features(
            pdev, plane_format, pImageFormatInfo->tiling);
      }
   }
   if (features == 0)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if (pImageFormatInfo->tiling == VK_IMAGE_TILING_LINEAR &&
       pImageFormatInfo->type != VK_IMAGE_TYPE_2D)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if (ycbcr_info && pImageFormatInfo->type != VK_IMAGE_TYPE_2D)
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   /* From the Vulkan 1.3.279 spec:
    *
    *    VUID-VkImageCreateInfo-tiling-04121
    *
    *    "If tiling is VK_IMAGE_TILING_LINEAR, flags must not contain
    *    VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT"
    *
    *    VUID-VkImageCreateInfo-imageType-00970
    *
    *    "If imageType is VK_IMAGE_TYPE_1D, flags must not contain
    *    VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT"
    */
   if (pImageFormatInfo->flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT &&
       (pImageFormatInfo->type == VK_IMAGE_TYPE_1D ||
        pImageFormatInfo->tiling == VK_IMAGE_TILING_LINEAR))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   /* From the Vulkan 1.3.279 spec:
    *
    *    VUID-VkImageCreateInfo-flags-09403
    *
    *    "If flags contains VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT, flags
    *    must not include VK_IMAGE_CREATE_SPARSE_ALIASED_BIT,
    *    VK_IMAGE_CREATE_SPARSE_BINDING_BIT, or
    *    VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT"
    */
   if ((pImageFormatInfo->flags & VK_IMAGE_CREATE_2D_ARRAY_COMPATIBLE_BIT) &&
       (pImageFormatInfo->flags & (VK_IMAGE_CREATE_SPARSE_ALIASED_BIT |
                                   VK_IMAGE_CREATE_SPARSE_BINDING_BIT |
                                   VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT)))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   /* We don't yet support sparse, but it shouldn't be too hard */
   if (pImageFormatInfo->flags & (VK_IMAGE_CREATE_SPARSE_ALIASED_BIT |
                                  VK_IMAGE_CREATE_SPARSE_BINDING_BIT |
                                  VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   const uint32_t max_dim = 16384;
   VkExtent3D maxExtent;
   uint32_t maxArraySize;
   switch (pImageFormatInfo->type) {
   case VK_IMAGE_TYPE_1D:
      maxExtent = (VkExtent3D){max_dim, 1, 1};
      maxArraySize = 2048;
      break;
   case VK_IMAGE_TYPE_2D:
      maxExtent = (VkExtent3D){max_dim, max_dim, 1};
      maxArraySize = 2048;
      break;
   case VK_IMAGE_TYPE_3D:
      maxExtent = (VkExtent3D){max_dim, max_dim, max_dim};
      maxArraySize = 1;
      break;
   default:
      unreachable("Invalid image type");
   }
   if (pImageFormatInfo->tiling == VK_IMAGE_TILING_LINEAR)
      maxArraySize = 1;

   assert(util_is_power_of_two_nonzero(max_dim));
   uint32_t maxMipLevels = util_logbase2(max_dim) + 1;
   if (ycbcr_info != NULL || pImageFormatInfo->tiling == VK_IMAGE_TILING_LINEAR)
      maxMipLevels = 1;

   VkSampleCountFlags sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   if (pImageFormatInfo->tiling == VK_IMAGE_TILING_OPTIMAL &&
       pImageFormatInfo->type == VK_IMAGE_TYPE_2D && ycbcr_info == NULL &&
       (features & (VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
                    VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT)) &&
       !(pImageFormatInfo->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)) {

      sampleCounts =
         VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT;
   }

   /* From the Vulkan 1.2.199 spec:
    *
    *    "VK_IMAGE_CREATE_EXTENDED_USAGE_BIT specifies that the image can be
    *    created with usage flags that are not supported for the format the
    *    image is created with but are supported for at least one format a
    *    VkImageView created from the image can have."
    *
    * If VK_IMAGE_CREATE_EXTENDED_USAGE_BIT is set, views can be created with
    * different usage than the image so we can't always filter on usage.
    * There is one exception to this below for storage.
    */
   const VkImageUsageFlags image_usage = pImageFormatInfo->usage;
   VkImageUsageFlags view_usage = image_usage;
   if (pImageFormatInfo->flags & VK_IMAGE_CREATE_EXTENDED_USAGE_BIT)
      view_usage = 0;

   if (view_usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) {
      if (!(features & (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))) {
         return VK_ERROR_FORMAT_NOT_SUPPORTED;
      }
   }

   u_foreach_bit(b, view_usage) {
      VkFormatFeatureFlags2 usage_features =
         vk_image_usage_to_format_features(1 << b);
      if (usage_features && !(features & usage_features))
         return VK_ERROR_FORMAT_NOT_SUPPORTED;
   }

   const VkExternalMemoryProperties *ext_mem_props = NULL;
   if (external_info != NULL && external_info->handleType != 0) {
      bool tiling_has_explicit_layout;
      switch (pImageFormatInfo->tiling) {
      case VK_IMAGE_TILING_LINEAR:
      case VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT:
         tiling_has_explicit_layout = true;
         break;
      case VK_IMAGE_TILING_OPTIMAL:
         tiling_has_explicit_layout = false;
         break;
      default:
         unreachable("Unsupported VkImageTiling");
      }

      switch (external_info->handleType) {
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
         /* No special restrictions */
         if (tiling_has_explicit_layout) {
            /* With an explicit memory layout, we don't care which type of
             * fd the image belongs too. Both OPAQUE_FD and DMA_BUF are
             * interchangeable here.
             */
            ext_mem_props = &hk_dma_buf_mem_props;
         } else {
            ext_mem_props = &hk_opaque_fd_mem_props;
         }
         break;

      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
         if (!tiling_has_explicit_layout) {
            return vk_errorf(pdev, VK_ERROR_FORMAT_NOT_SUPPORTED,
                             "VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT "
                             "requires VK_IMAGE_TILING_LINEAR or "
                             "VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT");
         }
         ext_mem_props = &hk_dma_buf_mem_props;
         break;

      default:
         /* From the Vulkan 1.3.256 spec:
          *
          *    "If handleType is not compatible with the [parameters] in
          *    VkPhysicalDeviceImageFormatInfo2, then
          *    vkGetPhysicalDeviceImageFormatProperties2 returns
          *    VK_ERROR_FORMAT_NOT_SUPPORTED."
          */
         return vk_errorf(pdev, VK_ERROR_FORMAT_NOT_SUPPORTED,
                          "unsupported VkExternalMemoryTypeFlagBits 0x%x",
                          external_info->handleType);
      }
   }

   const unsigned plane_count =
      vk_format_get_plane_count(pImageFormatInfo->format);

   /* From the Vulkan 1.3.259 spec, VkImageCreateInfo:
    *
    *    VUID-VkImageCreateInfo-imageCreateFormatFeatures-02260
    *
    *    "If format is a multi-planar format, and if imageCreateFormatFeatures
    *    (as defined in Image Creation Limits) does not contain
    *    VK_FORMAT_FEATURE_DISJOINT_BIT, then flags must not contain
    *    VK_IMAGE_CREATE_DISJOINT_BIT"
    *
    * This is satisfied trivially because we support DISJOINT on all
    * multi-plane formats.  Also,
    *
    *    VUID-VkImageCreateInfo-format-01577
    *
    *    "If format is not a multi-planar format, and flags does not include
    *    VK_IMAGE_CREATE_ALIAS_BIT, flags must not contain
    *    VK_IMAGE_CREATE_DISJOINT_BIT"
    */
   if (plane_count == 1 &&
       !(pImageFormatInfo->flags & VK_IMAGE_CREATE_ALIAS_BIT) &&
       (pImageFormatInfo->flags & VK_IMAGE_CREATE_DISJOINT_BIT))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   if (ycbcr_info &&
       ((pImageFormatInfo->flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT) ||
        (pImageFormatInfo->flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT)))
      return VK_ERROR_FORMAT_NOT_SUPPORTED;

   pImageFormatProperties->imageFormatProperties = (VkImageFormatProperties){
      .maxExtent = maxExtent,
      .maxMipLevels = maxMipLevels,
      .maxArrayLayers = maxArraySize,
      .sampleCounts = sampleCounts,
      .maxResourceSize = UINT32_MAX, /* TODO */
   };

   vk_foreach_struct(s, pImageFormatProperties->pNext) {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES: {
         VkExternalImageFormatProperties *p = (void *)s;
         /* From the Vulkan 1.3.256 spec:
          *
          *    "If handleType is 0, vkGetPhysicalDeviceImageFormatProperties2
          *    will behave as if VkPhysicalDeviceExternalImageFormatInfo was
          *    not present, and VkExternalImageFormatProperties will be
          *    ignored."
          *
          * This is true if and only if ext_mem_props == NULL
          */
         if (ext_mem_props != NULL)
            p->externalMemoryProperties = *ext_mem_props;
         break;
      }
      case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES: {
         VkSamplerYcbcrConversionImageFormatProperties *ycbcr_props = (void *)s;
         ycbcr_props->combinedImageSamplerDescriptorCount = plane_count;
         break;
      }
      case VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY_EXT: {
         VkHostImageCopyDevicePerformanceQueryEXT *hic_props = (void *)s;

         hic_props->optimalDeviceAccess = hic_props->identicalMemoryLayout =
            !(pImageFormatInfo->tiling == VK_IMAGE_TILING_OPTIMAL &&
              hk_can_compress_format(&pdev->dev, pImageFormatInfo->format));
         break;
      }
      default:
         vk_debug_ignored_stype(s->sType);
         break;
      }
   }

   return VK_SUCCESS;
}

static VkSparseImageFormatProperties
hk_fill_sparse_image_fmt_props(VkImageAspectFlags aspects)
{
   /* TODO */
   return (VkSparseImageFormatProperties){
      .aspectMask = aspects,
      .flags = VK_SPARSE_IMAGE_FORMAT_SINGLE_MIPTAIL_BIT,
      .imageGranularity =
         {
            .width = 1,
            .height = 1,
            .depth = 1,
         },
   };
}

VKAPI_ATTR void VKAPI_CALL
hk_GetPhysicalDeviceSparseImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
   uint32_t *pPropertyCount, VkSparseImageFormatProperties2 *pProperties)
{
   VkResult result;

   /* Check if the given format info is valid first before returning sparse
    * props.  The easiest way to do this is to just call
    * hk_GetPhysicalDeviceImageFormatProperties2()
    */
   const VkPhysicalDeviceImageFormatInfo2 img_fmt_info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .format = pFormatInfo->format,
      .type = pFormatInfo->type,
      .tiling = pFormatInfo->tiling,
      .usage = pFormatInfo->usage,
      .flags = VK_IMAGE_CREATE_SPARSE_BINDING_BIT |
               VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT,
   };

   VkImageFormatProperties2 img_fmt_props2 = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
      .pNext = NULL,
   };

   result = hk_GetPhysicalDeviceImageFormatProperties2(
      physicalDevice, &img_fmt_info, &img_fmt_props2);
   if (result != VK_SUCCESS) {
      *pPropertyCount = 0;
      return;
   }

   const VkImageFormatProperties *props = &img_fmt_props2.imageFormatProperties;
   if (!(pFormatInfo->samples & props->sampleCounts)) {
      *pPropertyCount = 0;
      return;
   }

   VK_OUTARRAY_MAKE_TYPED(VkSparseImageFormatProperties2, out, pProperties,
                          pPropertyCount);

   VkImageAspectFlags aspects = vk_format_aspects(pFormatInfo->format);

   vk_outarray_append_typed(VkSparseImageFormatProperties2, &out, props)
   {
      props->properties = hk_fill_sparse_image_fmt_props(aspects);
   }
}

static enum ail_tiling
hk_map_tiling(struct hk_device *dev, const VkImageCreateInfo *info,
              unsigned plane, uint64_t modifier)
{
   switch (info->tiling) {
   case VK_IMAGE_TILING_LINEAR:
      return AIL_TILING_LINEAR;

   case VK_IMAGE_TILING_OPTIMAL:
      if (hk_can_compress(&dev->dev, info->format, plane, info->extent.width,
                          info->extent.height, info->samples, info->flags,
                          info->usage, info->pNext)) {
         return AIL_TILING_TWIDDLED_COMPRESSED;
      } else {
         return AIL_TILING_TWIDDLED;
      }

   case VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT:
      return ail_drm_modifier_to_tiling(modifier);

   default:
      unreachable("invalid tiling");
   }
}

static uint32_t
modifier_get_score(uint64_t mod)
{
   switch (mod) {
   case DRM_FORMAT_MOD_APPLE_TWIDDLED_COMPRESSED:
      return 10;

   case DRM_FORMAT_MOD_APPLE_TWIDDLED:
      return 5;

   case DRM_FORMAT_MOD_LINEAR:
      return 1;

   default:
      return 0;
   }
}

static uint64_t
choose_drm_format_mod(uint32_t modifier_count, const uint64_t *modifiers)
{
   uint64_t best_mod = UINT64_MAX;
   uint32_t best_score = 0;

   for (uint32_t i = 0; i < modifier_count; ++i) {
      uint32_t score = modifier_get_score(modifiers[i]);
      if (score > best_score) {
         best_mod = modifiers[i];
         best_score = score;
      }
   }

   if (best_score > 0)
      return best_mod;
   else
      return DRM_FORMAT_MOD_INVALID;
}

static VkResult
hk_image_init(struct hk_device *dev, struct hk_image *image,
              const VkImageCreateInfo *pCreateInfo)
{
   vk_image_init(&dev->vk, &image->vk, pCreateInfo);

   if ((image->vk.usage & (VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                           VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)) &&
       image->vk.samples > 1) {
      image->vk.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
      image->vk.stencil_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
   }

   if (image->vk.usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT)
      image->vk.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
   if (image->vk.usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT)
      image->vk.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

   image->plane_count = vk_format_get_plane_count(pCreateInfo->format);
   image->disjoint = image->plane_count > 1 &&
                     (pCreateInfo->flags & VK_IMAGE_CREATE_DISJOINT_BIT);

   /* We do not support interleaved depth/stencil. Instead, we decompose to
    * a depth plane and a stencil plane.
    */
   if (image->vk.format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
      image->plane_count = 2;
   }

   if (image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT) {
      /* Sparse multiplane is not supported. Sparse depth/stencil not supported
       * on G13 so we're fine there too.
       */
      assert(image->plane_count == 1);
   }

   const struct VkImageDrmFormatModifierExplicitCreateInfoEXT
      *mod_explicit_info = NULL;

   if (pCreateInfo->tiling == VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT) {
      assert(!image->vk.wsi_legacy_scanout);
      mod_explicit_info = vk_find_struct_const(
         pCreateInfo->pNext,
         IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT);

      uint64_t modifier = DRM_FORMAT_MOD_INVALID;

      if (mod_explicit_info) {
         modifier = mod_explicit_info->drmFormatModifier;
      } else {
         const struct VkImageDrmFormatModifierListCreateInfoEXT *mod_list_info =
            vk_find_struct_const(
               pCreateInfo->pNext,
               IMAGE_DRM_FORMAT_MODIFIER_LIST_CREATE_INFO_EXT);

         modifier = choose_drm_format_mod(mod_list_info->drmFormatModifierCount,
                                          mod_list_info->pDrmFormatModifiers);
      }

      assert(modifier != DRM_FORMAT_MOD_INVALID);
      assert(image->vk.drm_format_mod == DRM_FORMAT_MOD_INVALID);
      image->vk.drm_format_mod = modifier;
   }

   const struct vk_format_ycbcr_info *ycbcr_info =
      vk_format_get_ycbcr_info(pCreateInfo->format);
   for (uint8_t plane = 0; plane < image->plane_count; plane++) {
      VkFormat format =
         ycbcr_info ? ycbcr_info->planes[plane].format : pCreateInfo->format;

      if (format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
         format = (plane == 0) ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_S8_UINT;
      }

      const uint8_t width_scale =
         ycbcr_info ? ycbcr_info->planes[plane].denominator_scales[0] : 1;
      const uint8_t height_scale =
         ycbcr_info ? ycbcr_info->planes[plane].denominator_scales[1] : 1;

      enum ail_tiling tiling =
         hk_map_tiling(dev, pCreateInfo, plane, image->vk.drm_format_mod);

      image->planes[plane].layout = (struct ail_layout){
         .tiling = tiling,
         .mipmapped_z = pCreateInfo->imageType == VK_IMAGE_TYPE_3D,
         .format = hk_format_to_pipe_format(format),

         .width_px = pCreateInfo->extent.width / width_scale,
         .height_px = pCreateInfo->extent.height / height_scale,
         .depth_px = MAX2(pCreateInfo->extent.depth, pCreateInfo->arrayLayers),

         .levels = pCreateInfo->mipLevels,
         .sample_count_sa = pCreateInfo->samples,
         .writeable_image = tiling != AIL_TILING_TWIDDLED_COMPRESSED,

         /* TODO: Maybe optimize this, our GL driver doesn't bother though */
         .renderable = true,
      };

      ail_make_miptree(&image->planes[plane].layout);
   }

   return VK_SUCCESS;
}

static VkResult
hk_image_plane_alloc_vma(struct hk_device *dev, struct hk_image_plane *plane,
                         VkImageCreateFlags create_flags)
{
   const bool sparse_bound = create_flags & VK_IMAGE_CREATE_SPARSE_BINDING_BIT;
   const bool sparse_resident =
      create_flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT;
   assert(sparse_bound || !sparse_resident);

   if (sparse_bound) {
      plane->vma_size_B = plane->layout.size_B;
#if 0
      plane->addr = nouveau_ws_alloc_vma(dev->ws_dev, 0, plane->vma_size_B,
                                         plane->layout.align_B,
                                         false, sparse_resident);
#endif
      if (plane->addr == 0) {
         return vk_errorf(dev, VK_ERROR_OUT_OF_DEVICE_MEMORY,
                          "Sparse VMA allocation failed");
      }
   }

   return VK_SUCCESS;
}

static void
hk_image_plane_finish(struct hk_device *dev, struct hk_image_plane *plane,
                      VkImageCreateFlags create_flags,
                      const VkAllocationCallbacks *pAllocator)
{
   if (plane->vma_size_B) {
#if 0
      const bool sparse_resident =
         create_flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT;

      agx_bo_unbind_vma(dev->ws_dev, plane->addr, plane->vma_size_B);
      nouveau_ws_free_vma(dev->ws_dev, plane->addr, plane->vma_size_B,
                          false, sparse_resident);
#endif
   }
}

static void
hk_image_finish(struct hk_device *dev, struct hk_image *image,
                const VkAllocationCallbacks *pAllocator)
{
   for (uint8_t plane = 0; plane < image->plane_count; plane++) {
      hk_image_plane_finish(dev, &image->planes[plane], image->vk.create_flags,
                            pAllocator);
   }

   vk_image_finish(&image->vk);
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_CreateImage(VkDevice _device, const VkImageCreateInfo *pCreateInfo,
               const VkAllocationCallbacks *pAllocator, VkImage *pImage)
{
   VK_FROM_HANDLE(hk_device, dev, _device);
   struct hk_physical_device *pdev = hk_device_physical(dev);
   struct hk_image *image;
   VkResult result;

#ifdef HK_USE_WSI_PLATFORM
   /* Ignore swapchain creation info on Android. Since we don't have an
    * implementation in Mesa, we're guaranteed to access an Android object
    * incorrectly.
    */
   const VkImageSwapchainCreateInfoKHR *swapchain_info =
      vk_find_struct_const(pCreateInfo->pNext, IMAGE_SWAPCHAIN_CREATE_INFO_KHR);
   if (swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE) {
      return wsi_common_create_swapchain_image(
         &pdev->wsi_device, pCreateInfo, swapchain_info->swapchain, pImage);
   }
#endif

   image = vk_zalloc2(&dev->vk.alloc, pAllocator, sizeof(*image), 8,
                      VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
   if (!image)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = hk_image_init(dev, image, pCreateInfo);
   if (result != VK_SUCCESS) {
      vk_free2(&dev->vk.alloc, pAllocator, image);
      return result;
   }

   for (uint8_t plane = 0; plane < image->plane_count; plane++) {
      result = hk_image_plane_alloc_vma(dev, &image->planes[plane],
                                        image->vk.create_flags);
      if (result != VK_SUCCESS) {
         hk_image_finish(dev, image, pAllocator);
         vk_free2(&dev->vk.alloc, pAllocator, image);
         return result;
      }
   }

   *pImage = hk_image_to_handle(image);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
hk_DestroyImage(VkDevice device, VkImage _image,
                const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   VK_FROM_HANDLE(hk_image, image, _image);

   if (!image)
      return;

   hk_image_finish(dev, image, pAllocator);
   vk_free2(&dev->vk.alloc, pAllocator, image);
}

static void
hk_image_plane_add_req(struct hk_image_plane *plane, uint64_t *size_B,
                       uint32_t *align_B)
{
   assert(util_is_power_of_two_or_zero64(*align_B));
   assert(util_is_power_of_two_or_zero64(HK_PLANE_ALIGN_B));

   *align_B = MAX2(*align_B, HK_PLANE_ALIGN_B);
   *size_B = align64(*size_B, HK_PLANE_ALIGN_B);
   *size_B += plane->layout.size_B;
}

static void
hk_get_image_memory_requirements(struct hk_device *dev, struct hk_image *image,
                                 VkImageAspectFlags aspects,
                                 VkMemoryRequirements2 *pMemoryRequirements)
{
   struct hk_physical_device *pdev = hk_device_physical(dev);
   uint32_t memory_types = (1 << pdev->mem_type_count) - 1;

   // TODO hope for the best?

   uint64_t size_B = 0;
   uint32_t align_B = 0;
   if (image->disjoint) {
      uint8_t plane = hk_image_aspects_to_plane(image, aspects);
      hk_image_plane_add_req(&image->planes[plane], &size_B, &align_B);
   } else {
      for (unsigned plane = 0; plane < image->plane_count; plane++)
         hk_image_plane_add_req(&image->planes[plane], &size_B, &align_B);
   }

   pMemoryRequirements->memoryRequirements.memoryTypeBits = memory_types;
   pMemoryRequirements->memoryRequirements.alignment = align_B;
   pMemoryRequirements->memoryRequirements.size = size_B;

   vk_foreach_struct_const(ext, pMemoryRequirements->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS: {
         VkMemoryDedicatedRequirements *dedicated = (void *)ext;
         dedicated->prefersDedicatedAllocation = false;
         dedicated->requiresDedicatedAllocation = false;
         break;
      }
      default:
         vk_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

VKAPI_ATTR void VKAPI_CALL
hk_GetImageMemoryRequirements2(VkDevice device,
                               const VkImageMemoryRequirementsInfo2 *pInfo,
                               VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   VK_FROM_HANDLE(hk_image, image, pInfo->image);

   const VkImagePlaneMemoryRequirementsInfo *plane_info =
      vk_find_struct_const(pInfo->pNext, IMAGE_PLANE_MEMORY_REQUIREMENTS_INFO);
   const VkImageAspectFlags aspects =
      image->disjoint ? plane_info->planeAspect : image->vk.aspects;

   hk_get_image_memory_requirements(dev, image, aspects, pMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL
hk_GetDeviceImageMemoryRequirements(VkDevice device,
                                    const VkDeviceImageMemoryRequirements *pInfo,
                                    VkMemoryRequirements2 *pMemoryRequirements)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   ASSERTED VkResult result;
   struct hk_image image = {0};

   result = hk_image_init(dev, &image, pInfo->pCreateInfo);
   assert(result == VK_SUCCESS);

   const VkImageAspectFlags aspects =
      image.disjoint ? pInfo->planeAspect : image.vk.aspects;

   hk_get_image_memory_requirements(dev, &image, aspects, pMemoryRequirements);

   hk_image_finish(dev, &image, NULL);
}

static VkSparseImageMemoryRequirements
hk_fill_sparse_image_memory_reqs(const struct ail_layout *layout,
                                 VkImageAspectFlags aspects)
{
   VkSparseImageFormatProperties sparse_format_props =
      hk_fill_sparse_image_fmt_props(aspects);

   // assert(layout->mip_tail_first_lod <= layout->num_levels);
   VkSparseImageMemoryRequirements sparse_memory_reqs = {
      .formatProperties = sparse_format_props,
      .imageMipTailFirstLod = 0, // layout->mip_tail_first_lod,
      .imageMipTailStride = 0,
   };

   sparse_memory_reqs.imageMipTailSize = layout->size_B;
   sparse_memory_reqs.imageMipTailOffset = 0;
   return sparse_memory_reqs;
}

static void
hk_get_image_sparse_memory_requirements(
   struct hk_device *dev, struct hk_image *image, VkImageAspectFlags aspects,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   VK_OUTARRAY_MAKE_TYPED(VkSparseImageMemoryRequirements2, out,
                          pSparseMemoryRequirements,
                          pSparseMemoryRequirementCount);

   /* From the Vulkan 1.3.279 spec:
    *
    *    "The sparse image must have been created using the
    *    VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT flag to retrieve valid sparse
    *    image memory requirements."
    */
   if (!(image->vk.create_flags & VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT))
      return;

   /* We don't support multiplane sparse for now */
   if (image->plane_count > 1)
      return;

   vk_outarray_append_typed(VkSparseImageMemoryRequirements2, &out, reqs)
   {
      reqs->memoryRequirements =
         hk_fill_sparse_image_memory_reqs(&image->planes[0].layout, aspects);
   };
}

VKAPI_ATTR void VKAPI_CALL
hk_GetImageSparseMemoryRequirements2(
   VkDevice device, const VkImageSparseMemoryRequirementsInfo2 *pInfo,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   VK_FROM_HANDLE(hk_image, image, pInfo->image);

   const VkImageAspectFlags aspects = image->vk.aspects;

   hk_get_image_sparse_memory_requirements(dev, image, aspects,
                                           pSparseMemoryRequirementCount,
                                           pSparseMemoryRequirements);
}

VKAPI_ATTR void VKAPI_CALL
hk_GetDeviceImageSparseMemoryRequirements(
   VkDevice device, const VkDeviceImageMemoryRequirements *pInfo,
   uint32_t *pSparseMemoryRequirementCount,
   VkSparseImageMemoryRequirements2 *pSparseMemoryRequirements)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   ASSERTED VkResult result;
   struct hk_image image = {0};

   result = hk_image_init(dev, &image, pInfo->pCreateInfo);
   assert(result == VK_SUCCESS);

   const VkImageAspectFlags aspects =
      image.disjoint ? pInfo->planeAspect : image.vk.aspects;

   hk_get_image_sparse_memory_requirements(dev, &image, aspects,
                                           pSparseMemoryRequirementCount,
                                           pSparseMemoryRequirements);

   hk_image_finish(dev, &image, NULL);
}

static void
hk_get_image_subresource_layout(UNUSED struct hk_device *dev,
                                struct hk_image *image,
                                const VkImageSubresource2KHR *pSubresource,
                                VkSubresourceLayout2KHR *pLayout)
{
   const VkImageSubresource *isr = &pSubresource->imageSubresource;

   const uint8_t p = hk_image_aspects_to_plane(image, isr->aspectMask);
   const struct hk_image_plane *plane = &image->planes[p];

   uint64_t offset_B = 0;
   if (!image->disjoint) {
      uint32_t align_B = 0;
      for (unsigned plane = 0; plane < p; plane++)
         hk_image_plane_add_req(&image->planes[plane], &offset_B, &align_B);
   }
   offset_B +=
      ail_get_layer_level_B(&plane->layout, isr->arrayLayer, isr->mipLevel);

   bool is_3d = image->vk.image_type == VK_IMAGE_TYPE_3D;

   pLayout->subresourceLayout = (VkSubresourceLayout){
      .offset = offset_B,
      .size = ail_get_level_size_B(&plane->layout, isr->mipLevel),

      /* From the spec:
       *
       *     It is legal to call vkGetImageSubresourceLayout2KHR with a image
       *     created with tiling equal to VK_IMAGE_TILING_OPTIMAL, but the
       * members of VkSubresourceLayout2KHR::subresourceLayout will have
       * undefined values in this case.
       *
       * So don't collapse with mips.
       */
      .rowPitch = isr->mipLevel
                     ? 0
                     : ail_get_wsi_stride_B(&plane->layout, isr->mipLevel),
      .arrayPitch = is_3d ? 0 : plane->layout.layer_stride_B,
      .depthPitch = is_3d ? plane->layout.layer_stride_B : 0,
   };

   VkSubresourceHostMemcpySizeEXT *memcpy_size =
      vk_find_struct(pLayout, SUBRESOURCE_HOST_MEMCPY_SIZE_EXT);
   if (memcpy_size) {
      memcpy_size->size = pLayout->subresourceLayout.size;
   }
}

VKAPI_ATTR void VKAPI_CALL
hk_GetImageSubresourceLayout2KHR(VkDevice device, VkImage _image,
                                 const VkImageSubresource2KHR *pSubresource,
                                 VkSubresourceLayout2KHR *pLayout)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   VK_FROM_HANDLE(hk_image, image, _image);

   hk_get_image_subresource_layout(dev, image, pSubresource, pLayout);
}

VKAPI_ATTR void VKAPI_CALL
hk_GetDeviceImageSubresourceLayoutKHR(
   VkDevice device, const VkDeviceImageSubresourceInfoKHR *pInfo,
   VkSubresourceLayout2KHR *pLayout)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   ASSERTED VkResult result;
   struct hk_image image = {0};

   result = hk_image_init(dev, &image, pInfo->pCreateInfo);
   assert(result == VK_SUCCESS);

   hk_get_image_subresource_layout(dev, &image, pInfo->pSubresource, pLayout);

   hk_image_finish(dev, &image, NULL);
}

static void
hk_image_plane_bind(struct hk_device *dev, struct hk_image_plane *plane,
                    struct hk_device_memory *mem, uint64_t *offset_B)
{
   *offset_B = align64(*offset_B, HK_PLANE_ALIGN_B);

   if (plane->vma_size_B) {
#if 0
      agx_bo_bind_vma(dev->ws_dev,
                             mem->bo,
                             plane->addr,
                             plane->vma_size_B,
                             *offset_B,
                             plane->nil.pte_kind);
#endif
      unreachable("todo");
   } else {
      plane->addr = mem->bo->va->addr + *offset_B;
      plane->map = mem->bo->map + *offset_B;
      plane->rem = mem->bo->size - (*offset_B);
   }

   *offset_B += plane->layout.size_B;
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_BindImageMemory2(VkDevice device, uint32_t bindInfoCount,
                    const VkBindImageMemoryInfo *pBindInfos)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   for (uint32_t i = 0; i < bindInfoCount; ++i) {
      VK_FROM_HANDLE(hk_device_memory, mem, pBindInfos[i].memory);
      VK_FROM_HANDLE(hk_image, image, pBindInfos[i].image);

      /* Ignore this struct on Android, we cannot access swapchain structures
       * there. */
#ifdef HK_USE_WSI_PLATFORM
      const VkBindImageMemorySwapchainInfoKHR *swapchain_info =
         vk_find_struct_const(pBindInfos[i].pNext,
                              BIND_IMAGE_MEMORY_SWAPCHAIN_INFO_KHR);

      if (swapchain_info && swapchain_info->swapchain != VK_NULL_HANDLE) {
         VkImage _wsi_image = wsi_common_get_image(swapchain_info->swapchain,
                                                   swapchain_info->imageIndex);
         VK_FROM_HANDLE(hk_image, wsi_img, _wsi_image);

         assert(image->plane_count == 1);
         assert(wsi_img->plane_count == 1);

         struct hk_image_plane *plane = &image->planes[0];
         struct hk_image_plane *swapchain_plane = &wsi_img->planes[0];

         /* Copy memory binding information from swapchain image to the current
          * image's plane. */
         plane->addr = swapchain_plane->addr;
         continue;
      }
#endif

      uint64_t offset_B = pBindInfos[i].memoryOffset;
      if (image->disjoint) {
         const VkBindImagePlaneMemoryInfo *plane_info = vk_find_struct_const(
            pBindInfos[i].pNext, BIND_IMAGE_PLANE_MEMORY_INFO);
         uint8_t plane =
            hk_image_aspects_to_plane(image, plane_info->planeAspect);
         hk_image_plane_bind(dev, &image->planes[plane], mem, &offset_B);
      } else {
         for (unsigned plane = 0; plane < image->plane_count; plane++) {
            hk_image_plane_bind(dev, &image->planes[plane], mem, &offset_B);
         }
      }

      const VkBindMemoryStatusKHR *status =
         vk_find_struct_const(pBindInfos[i].pNext, BIND_MEMORY_STATUS_KHR);
      if (status != NULL && status->pResult != NULL)
         *status->pResult = VK_SUCCESS;
   }

   return VK_SUCCESS;
}

static uint32_t
hk_plane_index(VkFormat format, VkImageAspectFlags aspect_mask)
{
   switch (aspect_mask) {
   default:
      assert(aspect_mask != VK_IMAGE_ASPECT_MEMORY_PLANE_3_BIT_EXT);
      return 0;
   case VK_IMAGE_ASPECT_PLANE_1_BIT:
   case VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT:
      return 1;
   case VK_IMAGE_ASPECT_PLANE_2_BIT:
   case VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT:
      return 2;
   case VK_IMAGE_ASPECT_STENCIL_BIT:
      return format == VK_FORMAT_D32_SFLOAT_S8_UINT;
   }
}

static void
hk_copy_memory_to_image(struct hk_device *device, struct hk_image *dst_image,
                        const VkMemoryToImageCopyEXT *info, bool copy_memcpy)
{
   unsigned plane =
      hk_plane_index(dst_image->vk.format, info->imageSubresource.aspectMask);
   const struct ail_layout *layout = &dst_image->planes[plane].layout;

   VkOffset3D offset = info->imageOffset;
   VkExtent3D extent = info->imageExtent;
   uint32_t src_width = info->memoryRowLength ?: extent.width;
   uint32_t src_height = info->memoryImageHeight ?: extent.height;

   uint32_t blocksize_B = util_format_get_blocksize(layout->format);
   uint32_t src_pitch = src_width * blocksize_B;

   unsigned start_layer = (dst_image->vk.image_type == VK_IMAGE_TYPE_3D)
                             ? offset.z
                             : info->imageSubresource.baseArrayLayer;
   uint32_t layers =
      MAX2(extent.depth, vk_image_subresource_layer_count(
                            &dst_image->vk, &info->imageSubresource));

   unsigned level = info->imageSubresource.mipLevel;
   uint32_t image_offset = ail_get_layer_level_B(layout, start_layer, level);
   uint32_t dst_layer_stride = layout->layer_stride_B;
   uint32_t src_layer_stride = copy_memcpy
                                  ? ail_get_level_size_B(layout, level)
                                  : (src_width * src_height * blocksize_B);
   bool tiled = ail_is_level_twiddled_uncompressed(
      layout, info->imageSubresource.mipLevel);

   const char *src =
      (const char *)info->pHostPointer + start_layer * dst_layer_stride;
   char *dst = (char *)dst_image->planes[plane].map + image_offset;
   for (unsigned layer = 0; layer < layers;
        layer++, src += src_layer_stride, dst += dst_layer_stride) {
      if (copy_memcpy) {
         memcpy(dst, src, ail_get_level_size_B(layout, level));
      } else if (!tiled) {
         uint32_t dst_pitch = ail_get_linear_stride_B(layout, level);
         /*TODO:comp*/
         for (unsigned y = 0; y < extent.height; y++) {
            memcpy(dst + dst_pitch * (y + offset.y) + offset.x * blocksize_B,
                   src + src_pitch * y, extent.width * blocksize_B);
         }
      } else {
         ail_tile(dst, (void *)src, layout, level, src_pitch, offset.x,
                  offset.y, extent.width, extent.height);
      }
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_CopyMemoryToImageEXT(VkDevice _device,
                        const VkCopyMemoryToImageInfoEXT *info)
{
   VK_FROM_HANDLE(hk_device, device, _device);
   VK_FROM_HANDLE(hk_image, dst_image, info->dstImage);

   for (unsigned i = 0; i < info->regionCount; i++) {
      hk_copy_memory_to_image(device, dst_image, &info->pRegions[i],
                              info->flags & VK_HOST_IMAGE_COPY_MEMCPY_EXT);
   }

   return VK_SUCCESS;
}

static void
hk_copy_image_to_memory(struct hk_device *device, struct hk_image *src_image,
                        const VkImageToMemoryCopyEXT *info, bool copy_memcpy)
{
   unsigned plane =
      hk_plane_index(src_image->vk.format, info->imageSubresource.aspectMask);
   const struct ail_layout *layout = &src_image->planes[plane].layout;

   VkOffset3D offset = info->imageOffset;
   VkExtent3D extent = info->imageExtent;
   uint32_t dst_width = info->memoryRowLength ?: extent.width;
   uint32_t dst_height = info->memoryImageHeight ?: extent.height;

#if 0
   copy_compressed(src_image->vk.format, &offset, &extent, &dst_width,
                   &dst_height);
#endif

   uint32_t blocksize_B = util_format_get_blocksize(layout->format);
   uint32_t dst_pitch = dst_width * blocksize_B;

   unsigned start_layer = (src_image->vk.image_type == VK_IMAGE_TYPE_3D)
                             ? offset.z
                             : info->imageSubresource.baseArrayLayer;
   uint32_t layers =
      MAX2(extent.depth, vk_image_subresource_layer_count(
                            &src_image->vk, &info->imageSubresource));
   unsigned level = info->imageSubresource.mipLevel;

   uint32_t image_offset = ail_get_layer_level_B(layout, start_layer, level);
   uint32_t src_layer_stride = layout->layer_stride_B;
   uint32_t dst_layer_stride = copy_memcpy
                                  ? ail_get_level_size_B(layout, level)
                                  : (dst_width * dst_height * blocksize_B);

   bool tiled = ail_is_level_twiddled_uncompressed(
      layout, info->imageSubresource.mipLevel);

   const char *src = (const char *)src_image->planes[plane].map + image_offset;
   char *dst = (char *)info->pHostPointer + start_layer * dst_layer_stride;
   for (unsigned layer = 0; layer < layers;
        layer++, src += src_layer_stride, dst += dst_layer_stride) {

      if (copy_memcpy) {
         memcpy(dst, src, dst_layer_stride);
      } else if (!tiled) {
         /* TODO: comp */
         uint32_t src_pitch = ail_get_linear_stride_B(layout, level);
         for (unsigned y = 0; y < extent.height; y++) {
            memcpy(dst + dst_pitch * y,
                   src + src_pitch * (y + offset.y) + offset.x * blocksize_B,
                   extent.width * blocksize_B);
         }
      } else {
         ail_detile((void *)src, dst, layout, info->imageSubresource.mipLevel,
                    dst_pitch, offset.x, offset.y, extent.width, extent.height);
      }
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_CopyImageToMemoryEXT(VkDevice _device,
                        const VkCopyImageToMemoryInfoEXT *info)
{
   VK_FROM_HANDLE(hk_device, device, _device);
   VK_FROM_HANDLE(hk_image, image, info->srcImage);

   for (unsigned i = 0; i < info->regionCount; i++) {
      hk_copy_image_to_memory(device, image, &info->pRegions[i],
                              info->flags & VK_HOST_IMAGE_COPY_MEMCPY_EXT);
   }

   return VK_SUCCESS;
}

static void
hk_copy_image_to_image_cpu(struct hk_device *device, struct hk_image *src_image,
                           struct hk_image *dst_image, const VkImageCopy2 *info,
                           bool copy_memcpy)
{
   unsigned src_plane =
      hk_plane_index(src_image->vk.format, info->srcSubresource.aspectMask);
   unsigned dst_plane =
      hk_plane_index(dst_image->vk.format, info->dstSubresource.aspectMask);

   const struct ail_layout *src_layout = &src_image->planes[src_plane].layout;
   const struct ail_layout *dst_layout = &dst_image->planes[dst_plane].layout;

   VkOffset3D src_offset = info->srcOffset;
   VkOffset3D dst_offset = info->dstOffset;
   VkExtent3D extent = info->extent;
   uint32_t layers_to_copy = MAX2(
      info->extent.depth,
      vk_image_subresource_layer_count(&src_image->vk, &info->srcSubresource));

   /* See comment above. */
#if 0
   copy_compressed(src_image->vk.format, &src_offset, &extent, NULL, NULL);
   copy_compressed(dst_image->vk.format, &dst_offset, NULL, NULL, NULL);
#endif

   unsigned src_start_layer = (src_image->vk.image_type == VK_IMAGE_TYPE_3D)
                                 ? src_offset.z
                                 : info->srcSubresource.baseArrayLayer;
   unsigned dst_start_layer = (dst_image->vk.image_type == VK_IMAGE_TYPE_3D)
                                 ? dst_offset.z
                                 : info->dstSubresource.baseArrayLayer;

   uint32_t src_layer_stride = src_layout->layer_stride_B;
   uint32_t dst_layer_stride = dst_layout->layer_stride_B;

   uint32_t dst_block_B = util_format_get_blocksize(dst_layout->format);
   uint32_t src_block_B = util_format_get_blocksize(src_layout->format);

   uint32_t src_image_offset = ail_get_layer_level_B(
      src_layout, src_start_layer, info->srcSubresource.mipLevel);
   uint32_t dst_image_offset = ail_get_layer_level_B(
      dst_layout, dst_start_layer, info->dstSubresource.mipLevel);

   bool src_tiled = ail_is_level_twiddled_uncompressed(
      src_layout, info->srcSubresource.mipLevel);
   bool dst_tiled = ail_is_level_twiddled_uncompressed(
      dst_layout, info->dstSubresource.mipLevel);

   const char *src =
      (const char *)src_image->planes[src_plane].map + src_image_offset;
   char *dst = (char *)dst_image->planes[dst_plane].map + dst_image_offset;
   for (unsigned layer = 0; layer < layers_to_copy;
        layer++, src += src_layer_stride, dst += dst_layer_stride) {

      if (copy_memcpy) {
         uint32_t src_size =
            ail_get_level_size_B(src_layout, info->srcSubresource.mipLevel);
         uint32_t dst_size =
            ail_get_level_size_B(dst_layout, info->dstSubresource.mipLevel);

         assert(src_size == dst_size);
         memcpy(dst, src, src_size);
      } else if (!src_tiled && !dst_tiled) {
         /* TODO comp */
         uint32_t src_pitch =
            ail_get_linear_stride_B(src_layout, info->srcSubresource.mipLevel);

         uint32_t dst_pitch =
            ail_get_linear_stride_B(dst_layout, info->dstSubresource.mipLevel);

         for (unsigned y = 0; y < extent.height; y++) {
            memcpy(dst + dst_pitch * (y + dst_offset.y) +
                      dst_offset.x * dst_block_B,
                   src + src_pitch * (y + src_offset.y) +
                      src_offset.x * src_block_B,
                   extent.width * src_block_B);
         }
      } else if (!src_tiled) {
         unreachable("todo");
#if 0
         fdl6_memcpy_linear_to_tiled(
            dst_offset.x, dst_offset.y, extent.width, extent.height, dst,
            src + src_pitch * src_offset.y + src_offset.x * src_layout->cpp,
            dst_layout, info->dstSubresource.mipLevel, src_pitch,
            &device->physical_device->ubwc_config);
#endif
      } else if (!dst_tiled) {
         unreachable("todo");
#if 0
         fdl6_memcpy_tiled_to_linear(
            src_offset.x, src_offset.y, extent.width, extent.height,
            dst + dst_pitch * dst_offset.y + dst_offset.x * dst_layout->cpp,
            src, src_layout, info->dstSubresource.mipLevel, dst_pitch,
            &device->physical_device->ubwc_config);
#endif
      } else {
         /* Work tile-by-tile, holding the unswizzled tile in a temporary
          * buffer.
          */
         char temp_tile[16384];

         unsigned src_level = info->srcSubresource.mipLevel;
         unsigned dst_level = info->dstSubresource.mipLevel;
         uint32_t block_width = src_layout->tilesize_el[src_level].width_el;
         uint32_t block_height = src_layout->tilesize_el[src_level].height_el;
         uint32_t temp_pitch = block_width * src_block_B;
         ;

         for (unsigned by = src_offset.y / block_height;
              by * block_height < src_offset.y + extent.height; by++) {
            uint32_t src_y_start = MAX2(src_offset.y, by * block_height);
            uint32_t dst_y_start = src_y_start - src_offset.y + dst_offset.y;
            uint32_t height =
               MIN2((by + 1) * block_height, src_offset.y + extent.height) -
               src_y_start;
            for (unsigned bx = src_offset.x / block_width;
                 bx * block_width < src_offset.x + extent.width; bx++) {
               uint32_t src_x_start = MAX2(src_offset.x, bx * block_width);
               uint32_t dst_x_start = src_x_start - src_offset.x + dst_offset.x;
               uint32_t width =
                  MIN2((bx + 1) * block_width, src_offset.x + extent.width) -
                  src_x_start;

               ail_detile((void *)src, temp_tile, src_layout, src_level,
                          temp_pitch, src_x_start, src_y_start, width, height);
               ail_tile(dst, temp_tile, dst_layout, dst_level, temp_pitch,
                        dst_x_start, dst_y_start, width, height);
            }
         }
      }
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_CopyImageToImageEXT(VkDevice _device,
                       const VkCopyImageToImageInfoEXT *pCopyImageToImageInfo)
{
   VK_FROM_HANDLE(hk_device, device, _device);
   VK_FROM_HANDLE(hk_image, src_image, pCopyImageToImageInfo->srcImage);
   VK_FROM_HANDLE(hk_image, dst_image, pCopyImageToImageInfo->dstImage);
   bool copy_memcpy =
      pCopyImageToImageInfo->flags & VK_HOST_IMAGE_COPY_MEMCPY_EXT;

   for (uint32_t i = 0; i < pCopyImageToImageInfo->regionCount; ++i) {
      if (src_image->vk.format == VK_FORMAT_D32_SFLOAT_S8_UINT) {
         VkImageCopy2 info = pCopyImageToImageInfo->pRegions[i];
         u_foreach_bit(b, info.dstSubresource.aspectMask) {
            info.srcSubresource.aspectMask = BITFIELD_BIT(b);
            info.dstSubresource.aspectMask = BITFIELD_BIT(b);
            hk_copy_image_to_image_cpu(device, src_image, dst_image, &info,
                                       copy_memcpy);
         }
         continue;
      }

      hk_copy_image_to_image_cpu(device, src_image, dst_image,
                                 pCopyImageToImageInfo->pRegions + i,
                                 copy_memcpy);
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_TransitionImageLayoutEXT(
   VkDevice device, uint32_t transitionCount,
   const VkHostImageLayoutTransitionInfoEXT *transitions)
{
   /* We don't do anything with layouts so this should be a no-op */
   return VK_SUCCESS;
}
