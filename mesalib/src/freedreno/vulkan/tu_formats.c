
/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "tu_private.h"

#include "adreno_common.xml.h"
#include "a6xx.xml.h"
#include "fdl/fd6_format_table.h"

#include "vk_format.h"
#include "vk_util.h"
#include "drm-uapi/drm_fourcc.h"

struct tu_native_format
tu6_format_vtx(VkFormat vk_format)
{
   enum pipe_format format = vk_format_to_pipe_format(vk_format);
   struct tu_native_format fmt = {
      .fmt = fd6_vertex_format(format),
      .swap = fd6_vertex_swap(format),
   };
   assert(fmt.fmt != FMT6_NONE);
   return fmt;
}

bool
tu6_format_vtx_supported(VkFormat vk_format)
{
   enum pipe_format format = vk_format_to_pipe_format(vk_format);
   return fd6_vertex_format(format) != FMT6_NONE;
}

/* Map non-colorspace-converted YUV formats to RGB pipe formats where we can,
 * since our hardware doesn't support colorspace conversion.
 *
 * Really, we should probably be returning the RGB formats in
 * vk_format_to_pipe_format, but we don't have all the equivalent pipe formats
 * for VK RGB formats yet, and we'd have to switch all consumers of that
 * function at once.
 */
enum pipe_format
tu_vk_format_to_pipe_format(VkFormat vk_format)
{
   switch (vk_format) {
   case VK_FORMAT_G8B8G8R8_422_UNORM: /* YUYV */
      return PIPE_FORMAT_R8G8_R8B8_UNORM;
   case VK_FORMAT_B8G8R8G8_422_UNORM: /* UYVY */
      return PIPE_FORMAT_G8R8_B8R8_UNORM;
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
      return PIPE_FORMAT_G8_B8R8_420_UNORM;
   case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
      return PIPE_FORMAT_G8_B8_R8_420_UNORM;
   default:
      return vk_format_to_pipe_format(vk_format);
   }
}

static struct tu_native_format
tu6_format_color_unchecked(enum pipe_format format, enum a6xx_tile_mode tile_mode)
{
   struct tu_native_format fmt = {
      .fmt = fd6_color_format(format, tile_mode),
      .swap = fd6_color_swap(format, tile_mode),
   };

   switch (format) {
   case PIPE_FORMAT_Z24X8_UNORM:
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      fmt.fmt = FMT6_8_8_8_8_UNORM;
      break;

   default:
      break;
   }

   return fmt;
}

bool
tu6_format_color_supported(enum pipe_format format)
{
   return tu6_format_color_unchecked(format, TILE6_LINEAR).fmt != FMT6_NONE;
}

struct tu_native_format
tu6_format_color(enum pipe_format format, enum a6xx_tile_mode tile_mode)
{
   struct tu_native_format fmt = tu6_format_color_unchecked(format, tile_mode);
   assert(fmt.fmt != FMT6_NONE);
   return fmt;
}

static struct tu_native_format
tu6_format_texture_unchecked(enum pipe_format format, enum a6xx_tile_mode tile_mode)
{
   struct tu_native_format fmt = {
      .fmt = fd6_texture_format(format, tile_mode),
      .swap = fd6_texture_swap(format, tile_mode),
   };

   /* No texturing support for NPOT textures yet.  See
    * https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/5536
    */
   if (util_format_is_plain(format) &&
       !util_is_power_of_two_nonzero(util_format_get_blocksize(format))) {
      fmt.fmt = FMT6_NONE;
   }

   switch (format) {
   case PIPE_FORMAT_Z24X8_UNORM:
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      /* freedreno uses Z24_UNORM_S8_UINT (sampling) or
       * FMT6_Z24_UNORM_S8_UINT_AS_R8G8B8A8 (blits) for this format, while we use
       * FMT6_8_8_8_8_UNORM or FMT6_Z24_UNORM_S8_UINT_AS_R8G8B8A8
       */
      fmt.fmt = FMT6_8_8_8_8_UNORM;
      break;

   default:
      break;
   }

   return fmt;
}

struct tu_native_format
tu6_format_texture(enum pipe_format format, enum a6xx_tile_mode tile_mode)
{
   struct tu_native_format fmt = tu6_format_texture_unchecked(format, tile_mode);
   assert(fmt.fmt != FMT6_NONE);
   return fmt;
}

bool
tu6_format_texture_supported(enum pipe_format format)
{
   return tu6_format_texture_unchecked(format, TILE6_LINEAR).fmt != FMT6_NONE;
}

static void
tu_physical_device_get_format_properties(
   struct tu_physical_device *physical_device,
   VkFormat vk_format,
   VkFormatProperties *out_properties)
{
   VkFormatFeatureFlags linear = 0, optimal = 0, buffer = 0;
   enum pipe_format format = tu_vk_format_to_pipe_format(vk_format);
   const struct util_format_description *desc = util_format_description(format);

   bool supported_vtx = tu6_format_vtx_supported(vk_format);
   bool supported_color = tu6_format_color_supported(format);
   bool supported_tex = tu6_format_texture_supported(format);

   if (format == PIPE_FORMAT_NONE ||
       !(supported_vtx || supported_color || supported_tex)) {
      goto end;
   }

   buffer |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
   if (supported_vtx)
      buffer |= VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;

   if (supported_tex) {
      optimal |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
                 VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
                 VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_MINMAX_BIT |
                 VK_FORMAT_FEATURE_COSITED_CHROMA_SAMPLES_BIT |
                 VK_FORMAT_FEATURE_MIDPOINT_CHROMA_SAMPLES_BIT;

      buffer |= VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT;

      /* no blit src bit for YUYV/NV12/I420 formats */
      if (desc->layout != UTIL_FORMAT_LAYOUT_SUBSAMPLED &&
          desc->layout != UTIL_FORMAT_LAYOUT_PLANAR2 &&
          desc->layout != UTIL_FORMAT_LAYOUT_PLANAR3)
         optimal |= VK_FORMAT_FEATURE_BLIT_SRC_BIT;

      if (desc->layout != UTIL_FORMAT_LAYOUT_SUBSAMPLED)
         optimal |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_YCBCR_CONVERSION_LINEAR_FILTER_BIT;

      if (!vk_format_is_int(vk_format)) {
         optimal |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;

         if (physical_device->vk.supported_extensions.EXT_filter_cubic)
            optimal |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_CUBIC_BIT_EXT;
      }
   }

   if (supported_color) {
      assert(supported_tex);
      optimal |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                 VK_FORMAT_FEATURE_BLIT_DST_BIT;

      /* IBO's don't have a swap field at all, so swapped formats can't be
       * supported, even with linear images.
       *
       * TODO: See if setting the swap field from the tex descriptor works,
       * after we enable shaderStorageImageReadWithoutFormat and there are
       * tests for these formats.
       */
      struct tu_native_format tex = tu6_format_texture(format, TILE6_LINEAR);
      if (tex.swap == WZYX && tex.fmt != FMT6_1_5_5_5_UNORM) {
         optimal |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
         buffer |= VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
      }

      /* TODO: The blob also exposes these for R16G16_UINT/R16G16_SINT, but we
       * don't have any tests for those.
       */
      if (vk_format == VK_FORMAT_R32_UINT || vk_format == VK_FORMAT_R32_SINT) {
         optimal |= VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT;
         buffer |= VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;
      }

      if (!util_format_is_pure_integer(format))
         optimal |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
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
   if (tu6_pipe2depth(vk_format) != (enum a6xx_depth_format)~0)
      optimal |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;

   if (!tiling_possible(vk_format) &&
       /* We don't actually support tiling for this format, but we need to
        * fake it as it's required by VK_KHR_sampler_ycbcr_conversion.
        */
       vk_format != VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM) {
      optimal = 0;
   }

   if (vk_format == VK_FORMAT_G8B8G8R8_422_UNORM ||
       vk_format == VK_FORMAT_B8G8R8G8_422_UNORM ||
       vk_format == VK_FORMAT_G8_B8R8_2PLANE_420_UNORM ||
       vk_format == VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM) {
      /* Disable buffer texturing of subsampled (422) and planar YUV textures.
       * The subsampling requirement comes from "If format is a block-compressed
       * format, then bufferFeatures must not support any features for the
       * format" plus the specification of subsampled as 2x1 compressed block
       * format.  I couldn't find the citation for planar, but 1D access of
       * planar YUV would be really silly.
       */
      buffer = 0;
   }

   /* D32_SFLOAT_S8_UINT is tiled as two images, so no linear format
    * blob enables some linear features, but its not useful, so don't bother.
    */
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
   TU_FROM_HANDLE(tu_physical_device, physical_device, physicalDevice);

   tu_physical_device_get_format_properties(
      physical_device, format, &pFormatProperties->formatProperties);

   VkDrmFormatModifierPropertiesListEXT *list =
      vk_find_struct(pFormatProperties->pNext, DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT);
   if (list) {
      VK_OUTARRAY_MAKE(out, list->pDrmFormatModifierProperties,
                       &list->drmFormatModifierCount);

      if (pFormatProperties->formatProperties.linearTilingFeatures) {
         vk_outarray_append(&out, mod_props) {
            mod_props->drmFormatModifier = DRM_FORMAT_MOD_LINEAR;
            mod_props->drmFormatModifierPlaneCount = 1;
         }
      }

      /* note: ubwc_possible() argument values to be ignored except for format */
      if (pFormatProperties->formatProperties.optimalTilingFeatures &&
          tiling_possible(format) &&
          ubwc_possible(format, VK_IMAGE_TYPE_2D, 0, 0, physical_device->info, VK_SAMPLE_COUNT_1_BIT)) {
         vk_outarray_append(&out, mod_props) {
            mod_props->drmFormatModifier = DRM_FORMAT_MOD_QCOM_COMPRESSED;
            mod_props->drmFormatModifierPlaneCount = 1;
         }
      }
   }
}

static VkResult
tu_get_image_format_properties(
   struct tu_physical_device *physical_device,
   const VkPhysicalDeviceImageFormatInfo2 *info,
   VkImageFormatProperties *pImageFormatProperties,
   VkFormatFeatureFlags *p_feature_flags)
{
   VkFormatProperties format_props;
   VkFormatFeatureFlags format_feature_flags;
   VkExtent3D maxExtent;
   uint32_t maxMipLevels;
   uint32_t maxArraySize;
   VkSampleCountFlags sampleCounts = VK_SAMPLE_COUNT_1_BIT;

   tu_physical_device_get_format_properties(physical_device, info->format,
                                            &format_props);

   switch (info->tiling) {
   case VK_IMAGE_TILING_LINEAR:
      format_feature_flags = format_props.linearTilingFeatures;
      break;

   case VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT: {
      const VkPhysicalDeviceImageDrmFormatModifierInfoEXT *drm_info =
         vk_find_struct_const(info->pNext, PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT);

      switch (drm_info->drmFormatModifier) {
      case DRM_FORMAT_MOD_QCOM_COMPRESSED:
         /* falling back to linear/non-UBWC isn't possible with explicit modifier */

         /* formats which don't support tiling */
         if (!format_props.optimalTilingFeatures ||
             !tiling_possible(info->format))
            return VK_ERROR_FORMAT_NOT_SUPPORTED;

         /* for mutable formats, its very unlikely to be possible to use UBWC */
         if (info->flags & VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT)
            return VK_ERROR_FORMAT_NOT_SUPPORTED;


         if (!ubwc_possible(info->format, info->type, info->usage, info->usage, physical_device->info, sampleCounts))
            return VK_ERROR_FORMAT_NOT_SUPPORTED;

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
      goto unsupported;

   if (info->type != VK_IMAGE_TYPE_2D &&
       vk_format_is_depth_or_stencil(info->format))
      goto unsupported;

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
        (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
         VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) &&
       !(info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
       !(info->usage & VK_IMAGE_USAGE_STORAGE_BIT)) {
      sampleCounts |= VK_SAMPLE_COUNT_2_BIT | VK_SAMPLE_COUNT_4_BIT;
      /* note: most operations support 8 samples (GMEM render/resolve do at least)
       * but some do not (which ones?), just disable 8 samples completely,
       * (no 8x msaa matches the blob driver behavior)
       */
   }

   if (info->usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_STORAGE_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) {
      if (!(format_feature_flags &
            VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
         goto unsupported;
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
unsupported:
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
tu_get_external_image_format_properties(
   const struct tu_physical_device *physical_device,
   const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
   VkExternalMemoryHandleTypeFlagBits handleType,
   VkExternalMemoryProperties *external_properties)
{
   VkExternalMemoryFeatureFlagBits flags = 0;
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
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT:
      flags = VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      compat_flags = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
      break;
   default:
      return vk_errorf(physical_device, VK_ERROR_FORMAT_NOT_SUPPORTED,
                       "VkExternalMemoryTypeFlagBits(0x%x) unsupported",
                       handleType);
   }

   *external_properties = (VkExternalMemoryProperties) {
      .externalMemoryFeatures = flags,
      .exportFromImportedHandleTypes = export_flags,
      .compatibleHandleTypes = compat_flags,
   };

   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
tu_GetPhysicalDeviceImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceImageFormatInfo2 *base_info,
   VkImageFormatProperties2 *base_props)
{
   TU_FROM_HANDLE(tu_physical_device, physical_device, physicalDevice);
   const VkPhysicalDeviceExternalImageFormatInfo *external_info = NULL;
   const VkPhysicalDeviceImageViewImageFormatInfoEXT *image_view_info = NULL;
   VkExternalImageFormatProperties *external_props = NULL;
   VkFilterCubicImageViewImageFormatPropertiesEXT *cubic_props = NULL;
   VkFormatFeatureFlags format_feature_flags;
   VkSamplerYcbcrConversionImageFormatProperties *ycbcr_props = NULL;
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
         external_info = (const void *) s;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_VIEW_IMAGE_FORMAT_INFO_EXT:
         image_view_info = (const void *) s;
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
         external_props = (void *) s;
         break;
      case VK_STRUCTURE_TYPE_FILTER_CUBIC_IMAGE_VIEW_IMAGE_FORMAT_PROPERTIES_EXT:
         cubic_props = (void *) s;
         break;
      case VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_IMAGE_FORMAT_PROPERTIES:
         ycbcr_props = (void *) s;
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
         &external_props->externalMemoryProperties);
      if (result != VK_SUCCESS)
         goto fail;
   }

   if (cubic_props) {
      /* note: blob only allows cubic filtering for 2D and 2D array views
       * its likely we can enable it for 1D and CUBE, needs testing however
       */
      if ((image_view_info->imageViewType == VK_IMAGE_VIEW_TYPE_2D ||
           image_view_info->imageViewType == VK_IMAGE_VIEW_TYPE_2D_ARRAY) &&
          (format_feature_flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_CUBIC_BIT_EXT)) {
         cubic_props->filterCubic = true;
         cubic_props->filterCubicMinmax = true;
      } else {
         cubic_props->filterCubic = false;
         cubic_props->filterCubicMinmax = false;
      }
   }

   if (ycbcr_props)
      ycbcr_props->combinedImageSamplerDescriptorCount = 1;

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

VKAPI_ATTR void VKAPI_CALL
tu_GetPhysicalDeviceExternalBufferProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
   VkExternalBufferProperties *pExternalBufferProperties)
{
   VkExternalMemoryFeatureFlagBits flags = 0;
   VkExternalMemoryHandleTypeFlags export_flags = 0;
   VkExternalMemoryHandleTypeFlags compat_flags = 0;
   switch (pExternalBufferInfo->handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      flags = VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
              VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      compat_flags = export_flags =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      break;
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT:
      flags = VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      compat_flags = VK_EXTERNAL_MEMORY_HANDLE_TYPE_HOST_ALLOCATION_BIT_EXT;
      break;
   default:
      break;
   }
   pExternalBufferProperties->externalMemoryProperties =
      (VkExternalMemoryProperties) {
         .externalMemoryFeatures = flags,
         .exportFromImportedHandleTypes = export_flags,
         .compatibleHandleTypes = compat_flags,
      };
}
