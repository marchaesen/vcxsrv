/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * SPDX-License-Identifier: MIT
 */

#include "tu_formats.h"

#include "fdl/fd6_format_table.h"
#include "common/freedreno_ubwc.h"

#include "vk_android.h"
#include "vk_enum_defines.h"
#include "vk_util.h"
#include "drm-uapi/drm_fourcc.h"

#include "tu_android.h"
#include "tu_device.h"
#include "tu_image.h"

#include <vulkan/vulkan_android.h>

static bool
tu6_format_vtx_supported(enum pipe_format format)
{
   return fd6_vertex_format(format) != FMT6_NONE;
}

struct tu_native_format
tu6_format_vtx(enum pipe_format format)
{
   struct tu_native_format fmt = {
      .fmt = fd6_vertex_format(format),
      .swap = fd6_vertex_swap(format),
   };
   assert(tu6_format_vtx_supported(format));
   return fmt;
}

static bool
tu6_format_color_supported(enum pipe_format format)
{
   return fd6_color_format(format, TILE6_LINEAR) != FMT6_NONE;
}

struct tu_native_format
tu6_format_color(enum pipe_format format, enum a6xx_tile_mode tile_mode)
{
   struct tu_native_format fmt = {
      .fmt = fd6_color_format(format, tile_mode),
      .swap = fd6_color_swap(format, tile_mode),
   };
   assert(fmt.fmt != FMT6_NONE);
   return fmt;
}

static bool
tu6_format_texture_supported(enum pipe_format format)
{
   return fd6_texture_format(format, TILE6_LINEAR) != FMT6_NONE;
}

struct tu_native_format
tu6_format_texture(enum pipe_format format, enum a6xx_tile_mode tile_mode)
{
   struct tu_native_format fmt = {
      .fmt = fd6_texture_format(format, tile_mode),
      .swap = fd6_texture_swap(format, tile_mode),
   };
   assert(fmt.fmt != FMT6_NONE);
   return fmt;
}

static enum fd6_ubwc_compat_type
tu6_ubwc_compat_mode(const struct fd_dev_info *info, VkFormat format)
{
   return fd6_ubwc_compat_mode(info, vk_format_to_pipe_format(format));
}

bool
tu6_mutable_format_list_ubwc_compatible(const struct fd_dev_info *info,
                                        const VkImageFormatListCreateInfo *fmt_list)
{
   if (!fmt_list || !fmt_list->viewFormatCount)
      return false;

   /* We're only looking at format list cross compatibility here, check
    * ubwc_possible() for the base "is the format UBWC-able at all?"
    */
   if (fmt_list->viewFormatCount == 1)
      return true;

   enum fd6_ubwc_compat_type type =
      tu6_ubwc_compat_mode(info, fmt_list->pViewFormats[0]);
   if (type == FD6_UBWC_UNKNOWN_COMPAT)
      return false;

   for (uint32_t i = 1; i < fmt_list->viewFormatCount; i++) {
      if (tu6_ubwc_compat_mode(info, fmt_list->pViewFormats[i]) != type)
         return false;
   }

   return true;
}

static void
tu_physical_device_get_format_properties(
   struct tu_physical_device *physical_device,
   VkFormat vk_format,
   VkFormatProperties3 *out_properties)
{
   VkFormatFeatureFlags2 linear = 0, optimal = 0, buffer = 0;
   enum pipe_format format = vk_format_to_pipe_format(vk_format);
   const struct util_format_description *desc = util_format_description(format);
   const struct vk_format_ycbcr_info *ycbcr_info = vk_format_get_ycbcr_info(vk_format);

   bool supported_vtx = tu6_format_vtx_supported(format);
   bool supported_color = tu6_format_color_supported(format);
   bool supported_tex = tu6_format_texture_supported(format);
   bool is_npot = !util_is_power_of_two_or_zero(desc->block.bits);

   if (format == PIPE_FORMAT_NONE ||
       !(supported_vtx || supported_color || supported_tex)) {
      goto end;
   }

   /* We don't support BufferToImage/ImageToBuffer for npot formats */
   if (!is_npot)
      buffer |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT;

   if (supported_vtx)
      buffer |= VK_FORMAT_FEATURE_2_VERTEX_BUFFER_BIT;

   if (supported_tex)
      buffer |= VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT;

   /* We don't support D24S8 because copying just one aspect would require a
    * special codepath and that doesn't seem worth it.
    */
   if (!is_npot && vk_format != VK_FORMAT_D24_UNORM_S8_UINT) {
      optimal |= VK_FORMAT_FEATURE_2_HOST_IMAGE_TRANSFER_BIT_EXT;
   }

   /* Don't support anything but texel buffers for non-power-of-two formats
    * with 3 components. We'd need several workarounds for copying and
    * clearing them because they're not renderable.
    */
   if (supported_tex && !is_npot) {
      optimal |= VK_FORMAT_FEATURE_2_TRANSFER_SRC_BIT |
                 VK_FORMAT_FEATURE_2_TRANSFER_DST_BIT |
                 VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT |
                 VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_MINMAX_BIT;

      if (ycbcr_info) {
         /* This is supported on all YCbCr formats */
         optimal |= VK_FORMAT_FEATURE_2_MIDPOINT_CHROMA_SAMPLES_BIT;

         if (ycbcr_info->n_planes > 1) {
            optimal |= VK_FORMAT_FEATURE_2_COSITED_CHROMA_SAMPLES_BIT |
                       VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT;
            if (physical_device->info->a6xx.has_separate_chroma_filter)
               optimal |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_YCBCR_CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT;
         }
      } else {
         /* BLIT_SRC_BIT isn't allowed for YCbCr formats */
         optimal |= VK_FORMAT_FEATURE_2_BLIT_SRC_BIT;
      }

      if (!vk_format_is_int(vk_format)) {
         optimal |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

         if (physical_device->vk.supported_extensions.EXT_filter_cubic)
            optimal |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_CUBIC_BIT_EXT;
      }

      /* We sample on the CPU so we can technically support anything as long
       * as it's floating point, but this restricts it to "reasonable" formats
       * to use, which means two channels and not something weird like
       * luminance-alpha.
       */
      if (vk_format_is_float(vk_format) && desc->nr_channels == 2 &&
          desc->swizzle[0] == PIPE_SWIZZLE_X &&
          desc->swizzle[1] == PIPE_SWIZZLE_Y) {
         optimal |= VK_FORMAT_FEATURE_2_FRAGMENT_DENSITY_MAP_BIT_EXT;
      }
   }

   if (supported_color) {
      assert(supported_tex);
      optimal |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
                 VK_FORMAT_FEATURE_2_BLIT_DST_BIT |
                 VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT |
                 VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT |
                 VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;

      buffer |= VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT |
                VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT |
                VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;

      /* TODO: The blob also exposes these for R16G16_UINT/R16G16_SINT/
       * R32G32_SFLOAT/R32G32B32A32_SFLOAT, but we don't have any tests for those.
       * R32_SFLOAT is also included here by the blob, but that requires
       * implementing VK_EXT_shader_atomic_float.
       */
      if (vk_format == VK_FORMAT_R32_UINT || vk_format == VK_FORMAT_R32_SINT) {
         optimal |= VK_FORMAT_FEATURE_2_STORAGE_IMAGE_ATOMIC_BIT;
         buffer |= VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;
      }

      if (!vk_format_is_int(vk_format))
         optimal |= VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BLEND_BIT;
   }

   /* All our depth formats support shadow comparisons. */
   if (vk_format_has_depth(vk_format) && (optimal & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT)) {
      optimal |= VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_DEPTH_COMPARISON_BIT;
   }

   /* We don't support writing into VK_FORMAT_*_PACK16 images/buffers  */
   if (desc->nr_channels > 2 && desc->block.bits == 16) {
      buffer &= VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT;
      optimal &= ~(VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT |
                   VK_FORMAT_FEATURE_2_STORAGE_IMAGE_ATOMIC_BIT);
   }

   /* For the most part, we can do anything with a linear image that we could
    * do with a tiled image. However, we can't support sysmem rendering with a
    * linear depth texture, because we don't know if there's a bit to control
    * the tiling of the depth buffer in BYPASS mode, and the blob also
    * disables linear depth rendering, so there's no way to discover it. We
    * also can't force GMEM mode, because there are other situations where we
    * have to use sysmem rendering. So follow the blob here, and only enable
    * DEPTH_STENCIL_ATTACHMENT_BIT for the optimal features.
    */
   linear = optimal;
   if (tu6_pipe2depth(vk_format) != DEPTH6_NONE)
      optimal |= VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT;

   if (!tiling_possible(vk_format) &&
       /* We don't actually support tiling for this format, but we need to
        * fake it as it's required by VK_KHR_sampler_ycbcr_conversion.
        */
       vk_format != VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM) {
      optimal = 0;
   }

   /* Disable buffer texturing of subsampled (422) and planar YUV textures,
    * as well as for depth/stencil formats. The subsampling requirement comes
    * from "If format is a block-compressed format, then bufferFeatures must
    * not support any features for the format" plus the specification of
    * subsampled as 2x1 compressed block format.  I couldn't find the citation
    * for planar, but 1D access of planar YUV would be really silly.
    *
    * From the Vulkan 1.3.205 spec, section 19.3 "43.3. Required Format Support":
    *
    *    Mandatory format support: depth/stencil with VkImageType
    *    VK_IMAGE_TYPE_2D
    *    [...]
    *    bufferFeatures must not support any features for these formats
    */
   if (ycbcr_info || vk_format_is_depth_or_stencil(vk_format))
      buffer = 0;

   /* D32_SFLOAT_S8_UINT is tiled as two images, so no linear format */
   if (vk_format == VK_FORMAT_D32_SFLOAT_S8_UINT)
      linear = 0;

end:
   out_properties->linearTilingFeatures = linear;
   out_properties->optimalTilingFeatures = optimal;
   out_properties->bufferFeatures = buffer;
}

VKAPI_ATTR void VKAPI_CALL
tu_GetPhysicalDeviceFormatProperties2(
   VkPhysicalDevice physicalDevice,
   VkFormat format,
   VkFormatProperties2 *pFormatProperties)
{
   VK_FROM_HANDLE(tu_physical_device, physical_device, physicalDevice);

   VkFormatProperties3 local_props3;
   VkFormatProperties3 *props3 =
      vk_find_struct(pFormatProperties->pNext, FORMAT_PROPERTIES_3);
   if (!props3)
      props3 = &local_props3;

   tu_physical_device_get_format_properties(
      physical_device, format, props3);

   pFormatProperties->formatProperties = (VkFormatProperties) {
      .linearTilingFeatures =
         vk_format_features2_to_features(props3->linearTilingFeatures),
      .optimalTilingFeatures =
         vk_format_features2_to_features(props3->optimalTilingFeatures),
      .bufferFeatures =
         vk_format_features2_to_features(props3->bufferFeatures),
   };

   VkDrmFormatModifierPropertiesListEXT *list =
      vk_find_struct(pFormatProperties->pNext, DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT);
   if (list) {
      VK_OUTARRAY_MAKE_TYPED(VkDrmFormatModifierPropertiesEXT, out,
                             list->pDrmFormatModifierProperties,
                             &list->drmFormatModifierCount);

      if (pFormatProperties->formatProperties.linearTilingFeatures) {
         vk_outarray_append_typed(VkDrmFormatModifierPropertiesEXT, &out, mod_props) {
            mod_props->drmFormatModifier = DRM_FORMAT_MOD_LINEAR;
            mod_props->drmFormatModifierPlaneCount = tu6_plane_count(format);
            mod_props->drmFormatModifierTilingFeatures =
               pFormatProperties->formatProperties.linearTilingFeatures;
         }
      }

      /* note: ubwc_possible() argument values to be ignored except for format */
      if (pFormatProperties->formatProperties.optimalTilingFeatures &&
          tiling_possible(format) &&
          ubwc_possible(NULL, format, VK_IMAGE_TYPE_2D, 0, 0,
                        physical_device->info, VK_SAMPLE_COUNT_1_BIT,
                        false)) {
         vk_outarray_append_typed(VkDrmFormatModifierPropertiesEXT, &out, mod_props) {
            mod_props->drmFormatModifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
            mod_props->drmFormatModifierPlaneCount = tu6_plane_count(format);
            mod_props->drmFormatModifierTilingFeatures =
               pFormatProperties->formatProperties.optimalTilingFeatures;
         }
      }
   }
}

static VkResult
tu_image_unsupported_format(VkImageFormatProperties *pImageFormatProperties)
{
   *pImageFormatProperties = (VkImageFormatProperties) {
      .maxExtent = { 0, 0, 0 },
      .maxMipLevels = 0,
      .maxArrayLayers = 0,
      .sampleCounts = 0,
      .maxResourceSize = 0,
   };

   return VK_ERROR_FORMAT_NOT_SUPPORTED;
}

static VkResult
tu_get_image_format_properties(
   struct tu_physical_device *physical_device,
   const VkPhysicalDeviceImageFormatInfo2 *info,
   VkImageFormatProperties *pImageFormatProperties,
   VkFormatFeatureFlags *p_feature_flags)
{
   VkFormatProperties3 format_props;
   VkFormatFeatureFlags format_feature_flags;
   VkExtent3D maxExtent;
   uint32_t maxMipLevels;
   uint32_t maxArraySize;
   BITMASK_ENUM(VkSampleCountFlagBits) sampleCounts = VK_SAMPLE_COUNT_1_BIT;

   tu_physical_device_get_format_properties(physical_device, info->format,
                                            &format_props);

   switch (info->tiling) {
   case VK_IMAGE_TILING_LINEAR:
      format_feature_flags = format_props.linearTilingFeatures;
      break;

   case VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT: {
      const VkPhysicalDeviceImageDrmFormatModifierInfoEXT *drm_info =
         vk_find_struct_const(info->pNext, PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT);

      /* Subsampled format isn't stable yet, so don't allow
       * importing/exporting with modifiers yet.
       */
      if (info->flags & VK_IMAGE_CREATE_SUBSAMPLED_BIT_EXT)
         return VK_ERROR_FORMAT_NOT_SUPPORTED;

      switch (drm_info->drmFormatModifier) {
      case DRM_FORMAT_MOD_QCOM_COMPRESSED:
         /* falling back to linear/non-UBWC isn't possible with explicit modifier */

         /* formats which don't support tiling */
         if (!format_props.optimalTilingFeatures ||
             !tiling_possible(info->format))
            return VK_ERROR_FORMAT_NOT_SUPPORTED;

         if (info->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT) {
            const VkImageFormatListCreateInfo *format_list =
               vk_find_struct_const(info->pNext,
                                    IMAGE_FORMAT_LIST_CREATE_INFO);
            if (!tu6_mutable_format_list_ubwc_compatible(physical_device->info,
                                                         format_list))
               return VK_ERROR_FORMAT_NOT_SUPPORTED;
         }

         if (!ubwc_possible(NULL, info->format, info->type, info->usage,
                            info->usage, physical_device->info, sampleCounts,
                            false)) {
            return VK_ERROR_FORMAT_NOT_SUPPORTED;
         }

         format_feature_flags = format_props.optimalTilingFeatures;
         break;
      case DRM_FORMAT_MOD_LINEAR:
         format_feature_flags = format_props.linearTilingFeatures;
         break;
      default:
         return VK_ERROR_FORMAT_NOT_SUPPORTED;
      }
   } break;
   case VK_IMAGE_TILING_OPTIMAL:
      format_feature_flags = format_props.optimalTilingFeatures;
      break;
   default:
      unreachable("bad VkPhysicalDeviceImageFormatInfo2");
   }

   if (format_feature_flags == 0)
      return tu_image_unsupported_format(pImageFormatProperties);

   if (info->type != VK_IMAGE_TYPE_2D &&
       vk_format_is_depth_or_stencil(info->format))
      return tu_image_unsupported_format(pImageFormatProperties);

   switch (info->type) {
   default:
      unreachable("bad vkimage type\n");
   case VK_IMAGE_TYPE_1D:
      maxExtent.width = 16384;
      maxExtent.height = 1;
      maxExtent.depth = 1;
      maxMipLevels = 15; /* log2(maxWidth) + 1 */
      maxArraySize = 2048;
      break;
   case VK_IMAGE_TYPE_2D:
      maxExtent.width = 16384;
      maxExtent.height = 16384;
      maxExtent.depth = 1;
      maxMipLevels = 15; /* log2(maxWidth) + 1 */
      maxArraySize = 2048;
      break;
   case VK_IMAGE_TYPE_3D:
      maxExtent.width = 2048;
      maxExtent.height = 2048;
      maxExtent.depth = 2048;
      maxMipLevels = 12; /* log2(maxWidth) + 1 */
      maxArraySize = 1;
      break;
   }

   if (info->tiling == VK_IMAGE_TILING_OPTIMAL &&
       info->type == VK_IMAGE_TYPE_2D &&
       (format_feature_flags &
        (VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
         VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT)) &&
       !(info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
       !(info->usage & VK_IMAGE_USAGE_STORAGE_BIT)) {
      sampleCounts |= VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT;
      /* note: most operations support 8 samples (GMEM render/resolve do at least)
       * but some do not (which ones?), just disable 8 samples completely,
       * (no 8x msaa matches the blob driver behavior)
       */
   }

   /* From the Vulkan 1.3.206 spec:
    *
    * "VK_IMAGE_CREATE_EXTENDED_USAGE_BIT specifies that the image can be
    * created with usage flags that are not supported for the format the image
    * is created with but are supported for at least one format a VkImageView
    * created from the image can have."
    *
    * This means we should relax checks that only depend on the
    * format_feature_flags, to allow the user to create images that may be
    * e.g. reinterpreted as storage when the original format doesn't allow it.
    * The user will have to check against the format features anyway.
    * Otherwise we'd unnecessarily disallow it.
    */

   VkImageUsageFlags image_usage = info->usage;
   if (info->flags & VK_IMAGE_CREATE_EXTENDED_USAGE_BIT)
      image_usage = 0;

   if (image_usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_BIT)) {
         return tu_image_unsupported_format(pImageFormatProperties);
      }
   }

   if (image_usage & VK_IMAGE_USAGE_STORAGE_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT)) {
         return tu_image_unsupported_format(pImageFormatProperties);
      }
   }

   if (image_usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT)) {
         return tu_image_unsupported_format(pImageFormatProperties);
      }
   }

   if (image_usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      if (!(format_feature_flags &
            VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT)) {
         return tu_image_unsupported_format(pImageFormatProperties);
      }
   }

   if (image_usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) {
      if (!(format_feature_flags &
            (VK_FORMAT_FEATURE_2_COLOR_ATTACHMENT_BIT |
             VK_FORMAT_FEATURE_2_DEPTH_STENCIL_ATTACHMENT_BIT))) {
         return tu_image_unsupported_format(pImageFormatProperties);
      }
   }

   *pImageFormatProperties = (VkImageFormatProperties) {
      .maxExtent = maxExtent,
      .maxMipLevels = maxMipLevels,
      .maxArrayLayers = maxArraySize,
      .sampleCounts = sampleCounts,

      /* FINISHME: Accurately calculate
       * VkImageFormatProperties::maxResourceSize.
       */
      .maxResourceSize = UINT32_MAX,
   };

   if (p_feature_flags)
      *p_feature_flags = format_feature_flags;

   return VK_SUCCESS;
}

static VkResult
tu_get_external_image_format_properties(
   const struct tu_physical_device *physical_device,
   const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
   VkExternalMemoryHandleTypeFlagBits handleType,
   VkExternalImageFormatProperties *external_properties)
{
   BITMASK_ENUM(VkExternalMemoryFeatureFlagBits) flags = 0;
   VkExternalMemoryHandleTypeFlags export_flags = 0;
   VkExternalMemoryHandleTypeFlags compat_flags = 0;

   /* From the Vulkan 1.1.98 spec:
    *
    *    If handleType is not compatible with the format, type, tiling,
    *    usage, and flags specified in VkPhysicalDeviceImageFormatInfo2,
    *    then vkGetPhysicalDeviceImageFormatProperties2 returns
    *    VK_ERROR_FORMAT_NOT_SUPPORTED.
    */

   switch (handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      switch (pImageFormatInfo->type) {
      case VK_IMAGE_TYPE_2D:
         flags = VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT |
                 VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
                 VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
         compat_flags = export_flags =
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
         break;
      default:
         return vk_errorf(physical_device, VK_ERROR_FORMAT_NOT_SUPPORTED,
                          "VkExternalMemoryTypeFlagBits(0x%x) unsupported for VkImageType(%d)",
                          handleType, pImageFormatInfo->type);
      }
      break;
#if DETECT_OS_ANDROID
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID:
      flags = VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT |
              VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
              VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      compat_flags = export_flags = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
      break;
#endif
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT:
      flags = VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      compat_flags = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
      break;
   default:
      return vk_errorf(physical_device, VK_ERROR_FORMAT_NOT_SUPPORTED,
                       "VkExternalMemoryTypeFlagBits(0x%x) unsupported",
                       handleType);
   }

   if (external_properties) {
      external_properties->externalMemoryProperties =
         (VkExternalMemoryProperties) {
            .externalMemoryFeatures = flags,
            .exportFromImportedHandleTypes = export_flags,
            .compatibleHandleTypes = compat_flags,
         };
   }

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetPhysicalDeviceImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceImageFormatInfo2 *base_info,
   VkImageFormatProperties2 *base_props)
{
   VK_FROM_HANDLE(tu_physical_device, physical_device, physicalDevice);
   const VkPhysicalDeviceExternalImageFormatInfo *external_info = NULL;
   const VkPhysicalDeviceImageViewImageFormatInfoEXT *image_view_info = NULL;
   VkExternalImageFormatProperties *external_props = NULL;
   VkAndroidHardwareBufferUsageANDROID *android_usage = NULL;
   VkFilterCubicImageViewImageFormatPropertiesEXT *cubic_props = NULL;
   VkFormatFeatureFlags format_feature_flags;
   VkSamplerYcbcrConversionImageFormatProperties *ycbcr_props = NULL;
   VkHostImageCopyDevicePerformanceQueryEXT *hic_props = NULL;
   VkResult result;

   result = tu_get_image_format_properties(physical_device,
      base_info, &base_props->imageFormatProperties, &format_feature_flags);
   if (result != VK_SUCCESS)
      return result;

   /* Extract input structs */
   vk_foreach_struct_const(s, base_info->pNext)
   {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO:
         external_info = (const VkPhysicalDeviceExternalImageFormatInfo *) s;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_IMAGE_FORMAT_INFO_EXT:
         image_view_info = (const VkPhysicalDeviceImageViewImageFormatInfoEXT *) s;
         break;
      default:
         break;
      }
   }

   /* Extract output structs */
   vk_foreach_struct(s, base_props->pNext)
   {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES:
         external_props = (VkExternalImageFormatProperties *) s;
         break;
      case VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_USAGE_ANDROID:
         android_usage = (VkAndroidHardwareBufferUsageANDROID *) s;
         break;
      case VK_STRUCTURE_TYPE_FILTER_CUBIC_IMAGE_VIEW_IMAGE_FORMAT_PROPERTIES_EXT:
         cubic_props = (VkFilterCubicImageViewImageFormatPropertiesEXT *) s;
         break;
      case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES:
         ycbcr_props = (VkSamplerYcbcrConversionImageFormatProperties *) s;
         break;
      case VK_STRUCTURE_TYPE_HOST_IMAGE_COPY_DEVICE_PERFORMANCE_QUERY_EXT:
         hic_props = (VkHostImageCopyDevicePerformanceQueryEXT *) s;
         break;
      default:
         break;
      }
   }

   /* From the Vulkan 1.0.42 spec:
    *
    *    If handleType is 0, vkGetPhysicalDeviceImageFormatProperties2 will
    *    behave as if VkPhysicalDeviceExternalImageFormatInfo was not
    *    present and VkExternalImageFormatProperties will be ignored.
    */
   if (external_info && external_info->handleType != 0) {
      result = tu_get_external_image_format_properties(
         physical_device, base_info, external_info->handleType,
         external_props);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (cubic_props) {
      /* note: blob only allows cubic filtering for 2D and 2D array views
       * its likely we can enable it for 1D and CUBE, needs testing however
       */
      if ((image_view_info->imageViewType == VK_IMAGE_VIEW_TYPE_2D ||
           image_view_info->imageViewType == VK_IMAGE_VIEW_TYPE_2D_ARRAY) &&
          (format_feature_flags & VK_FORMAT_FEATURE_2_SAMPLED_IMAGE_FILTER_CUBIC_BIT_EXT)) {
         cubic_props->filterCubic = true;
         cubic_props->filterCubicMinmax = true;
      } else {
         cubic_props->filterCubic = false;
         cubic_props->filterCubicMinmax = false;
      }
   }

   if (android_usage) {
      /* Don't expect gralloc to be able to allocate anything other than 3D: */
      if (base_info->type != VK_IMAGE_TYPE_2D) {
         result = vk_errorf(physical_device, VK_ERROR_FORMAT_NOT_SUPPORTED,
                            "type (%u) unsupported for AHB", base_info->type);
         goto fail;
      }
      VkImageFormatProperties *props = &base_props->imageFormatProperties;
      if (!(props->sampleCounts & VK_SAMPLE_COUNT_1_BIT)) {
         result = vk_errorf(physical_device, VK_ERROR_FORMAT_NOT_SUPPORTED,
                          "sampleCounts (%x) unsupported for AHB", props->sampleCounts);
         goto fail;
      }
      android_usage->androidHardwareBufferUsage =
         vk_image_usage_to_ahb_usage(base_info->flags, base_info->usage);
      uint32_t format = vk_image_format_to_ahb_format(base_info->format);
      if (!format) {
         result = vk_errorf(physical_device, VK_ERROR_FORMAT_NOT_SUPPORTED,
                            "format (%u) unsupported for AHB", base_info->format);
         goto fail;
      }
      /* We can't advertise support for anything that gralloc cannot allocate
       * so we are stuck without any better option than attempting a test
       * allocation:
       */
      if (!vk_ahb_probe_format(base_info->format, base_info->flags, base_info->usage)) {
         result = vk_errorf(physical_device, VK_ERROR_FORMAT_NOT_SUPPORTED,
                            "format (%x) with flags (%x) and usage (%x) unsupported for AHB",
                            base_info->format, base_info->flags, base_info->usage);
         goto fail;
      }

      /* AHBs with mipmap usage will ignore this property */
      props->maxMipLevels = 1;
      props->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   }

   if (ycbcr_props)
      ycbcr_props->combinedImageSamplerDescriptorCount = 1;

   if (hic_props) {
      /* This should match tu_image_init() as much as possible given the
       * information we have here. We are conservative and only return true if
       * we know that UBWC would never be enabled and copying the tiled image
       * is possible so we wouldn't have to fall back to linear. There are no
       * cases where we modify the layout for HIC but still have optimal
       * access, so we return the same value for both.
       *
       * ubwc_possible() returns false for block-compressed formats, which
       * satisfies the spec requirement that:
       *
       *    If VkPhysicalDeviceImageFormatInfo2::format is a block-compressed
       *    format and vkGetPhysicalDeviceImageFormatProperties2 returns
       *    VK_SUCCESS, the implementation must return VK_TRUE in
       *    optimalDeviceAccess.
       */
      hic_props->optimalDeviceAccess = hic_props->identicalMemoryLayout =
         base_info->tiling == VK_IMAGE_TILING_LINEAR ||
         base_info->type == VK_IMAGE_TYPE_1D ||
         !tiling_possible(base_info->format) ||
         (base_info->usage & VK_IMAGE_USAGE_FRAGMENT_DENSITY_MAP_BIT_EXT) ||
         /* If UBWC is impossible, tiling is possible, but it's a swapped
          * format, we'd hit the force_linear_tile fallback.
          */
         (fd6_color_swap(vk_format_to_pipe_format(base_info->format),
                                                  TILE6_LINEAR) == WZYX &&
         !ubwc_possible(NULL, base_info->format, base_info->type,
                        (base_info->usage & ~VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT),
                        (base_info->usage & ~VK_IMAGE_USAGE_HOST_TRANSFER_BIT_EXT),
                        physical_device->info, VK_SAMPLE_COUNT_1_BIT,
                        physical_device->info->a6xx.has_z24uint_s8uint));
   }

   return VK_SUCCESS;

fail:
   if (result == VK_ERROR_FORMAT_NOT_SUPPORTED) {
      /* From the Vulkan 1.0.42 spec:
       *
       *    If the combination of parameters to
       *    vkGetPhysicalDeviceImageFormatProperties2 is not supported by
       *    the implementation for use in vkCreateImage, then all members of
       *    imageFormatProperties will be filled with zero.
       */
      base_props->imageFormatProperties = (VkImageFormatProperties) {};
   }

   return result;
}

VKAPI_ATTR void VKAPI_CALL
tu_GetPhysicalDeviceSparseImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
   uint32_t *pPropertyCount,
   VkSparseImageFormatProperties2 *pProperties)
{
   /* Sparse images are not yet supported. */
   *pPropertyCount = 0;
}
