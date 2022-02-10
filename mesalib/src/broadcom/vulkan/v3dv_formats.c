/*
 * Copyright © 2019 Raspberry Pi
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "v3dv_private.h"
#include "vk_util.h"

#include "drm-uapi/drm_fourcc.h"
#include "util/format/u_format.h"
#include "vulkan/wsi/wsi_common.h"

const uint8_t *
v3dv_get_format_swizzle(struct v3dv_device *device, VkFormat f)
{
   const struct v3dv_format *vf = v3dv_X(device, get_format)(f);
   static const uint8_t fallback[] = {0, 1, 2, 3};

   if (!vf)
      return fallback;

   return vf->swizzle;
}

bool
v3dv_format_swizzle_needs_rb_swap(const uint8_t *swizzle)
{
   /* Normal case */
   if (swizzle[0] == PIPE_SWIZZLE_Z)
      return swizzle[2] == PIPE_SWIZZLE_X;

   /* Format uses reverse flag */
   if (swizzle[0] == PIPE_SWIZZLE_Y)
      return swizzle[2] == PIPE_SWIZZLE_W;

   return false;
}

bool
v3dv_format_swizzle_needs_reverse(const uint8_t *swizzle)
{
   /* Normal case */
   if (swizzle[0] == PIPE_SWIZZLE_W &&
       swizzle[1] == PIPE_SWIZZLE_Z &&
       swizzle[2] == PIPE_SWIZZLE_Y &&
       swizzle[3] == PIPE_SWIZZLE_X) {
      return true;
   }

   /* Format uses RB swap flag */
   if (swizzle[0] == PIPE_SWIZZLE_Y &&
       swizzle[1] == PIPE_SWIZZLE_Z &&
       swizzle[2] == PIPE_SWIZZLE_W &&
       swizzle[3] == PIPE_SWIZZLE_X) {
      return true;
   }

   return false;
}

uint8_t
v3dv_get_tex_return_size(const struct v3dv_format *vf,
                         bool compare_enable)
{
   if (unlikely(V3D_DEBUG & V3D_DEBUG_TMU_16BIT))
      return 16;

   if (unlikely(V3D_DEBUG & V3D_DEBUG_TMU_32BIT))
      return 32;

   if (compare_enable)
      return 16;

   return vf->return_size;
}

/* Some cases of transfer operations are raw data copies that don't depend
 * on the semantics of the pixel format (no pixel format conversions are
 * involved). In these cases, it is safe to choose any format supported by
 * the TFU so long as it has the same texel size, which allows us to use the
 * TFU paths with formats that are not TFU supported otherwise.
 */
const struct v3dv_format *
v3dv_get_compatible_tfu_format(struct v3dv_device *device,
                               uint32_t bpp,
                               VkFormat *out_vk_format)
{
   VkFormat vk_format;
   switch (bpp) {
   case 16: vk_format = VK_FORMAT_R32G32B32A32_SFLOAT;  break;
   case 8:  vk_format = VK_FORMAT_R16G16B16A16_SFLOAT;  break;
   case 4:  vk_format = VK_FORMAT_R32_SFLOAT;           break;
   case 2:  vk_format = VK_FORMAT_R16_SFLOAT;           break;
   case 1:  vk_format = VK_FORMAT_R8_UNORM;             break;
   default: unreachable("unsupported format bit-size"); break;
   };

   if (out_vk_format)
      *out_vk_format = vk_format;

   const struct v3dv_format *format = v3dv_X(device, get_format)(vk_format);
   assert(v3dv_X(device, tfu_supports_tex_format)(format->tex_type));

   return format;
}

static VkFormatFeatureFlags
image_format_features(struct v3dv_physical_device *pdevice,
                      VkFormat vk_format,
                      const struct v3dv_format *v3dv_format,
                      VkImageTiling tiling)
{
   if (!v3dv_format || !v3dv_format->supported)
      return 0;

   const VkImageAspectFlags aspects = vk_format_aspects(vk_format);

   const VkImageAspectFlags zs_aspects = VK_IMAGE_ASPECT_DEPTH_BIT |
                                         VK_IMAGE_ASPECT_STENCIL_BIT;
   const VkImageAspectFlags supported_aspects = VK_IMAGE_ASPECT_COLOR_BIT |
                                                zs_aspects;
   if ((aspects & supported_aspects) != aspects)
      return 0;

   /* FIXME: We don't support separate stencil yet */
   if ((aspects & zs_aspects) == VK_IMAGE_ASPECT_STENCIL_BIT)
      return 0;

   if (v3dv_format->tex_type == TEXTURE_DATA_FORMAT_NO &&
       v3dv_format->rt_type == V3D_OUTPUT_IMAGE_FORMAT_NO) {
      return 0;
   }

   VkFormatFeatureFlags flags = 0;

   /* Raster format is only supported for 1D textures, so let's just
    * always require optimal tiling for anything that requires sampling.
    * Note: even if the user requests optimal for a 1D image, we will still
    * use raster format since that is what the HW requires.
    */
   if (v3dv_format->tex_type != TEXTURE_DATA_FORMAT_NO &&
       tiling == VK_IMAGE_TILING_OPTIMAL) {
      flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
               VK_FORMAT_FEATURE_BLIT_SRC_BIT;

      if (v3dv_format->supports_filtering)
         flags |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
   }

   if (v3dv_format->rt_type != V3D_OUTPUT_IMAGE_FORMAT_NO) {
      if (aspects & VK_IMAGE_ASPECT_COLOR_BIT) {
         flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                  VK_FORMAT_FEATURE_BLIT_DST_BIT;
         if (v3dv_X(pdevice, format_supports_blending)(v3dv_format))
            flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
      } else if (aspects & zs_aspects) {
         flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
                  VK_FORMAT_FEATURE_BLIT_DST_BIT;
      }
   }

   const struct util_format_description *desc =
      vk_format_description(vk_format);
   assert(desc);

   if (desc->layout == UTIL_FORMAT_LAYOUT_PLAIN && desc->is_array) {
      flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
      if (desc->nr_channels == 1 && vk_format_is_int(vk_format))
         flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT;
   } else if (vk_format == VK_FORMAT_A2B10G10R10_UNORM_PACK32 ||
              vk_format == VK_FORMAT_A2B10G10R10_UINT_PACK32 ||
              vk_format == VK_FORMAT_B10G11R11_UFLOAT_PACK32) {
      /* To comply with shaderStorageImageExtendedFormats */
      flags |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
   }

   if (flags) {
      flags |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT |
               VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
   }

   return flags;
}

static VkFormatFeatureFlags
buffer_format_features(VkFormat vk_format, const struct v3dv_format *v3dv_format)
{
   if (!v3dv_format || !v3dv_format->supported)
      return 0;

   if (!v3dv_format->supported)
      return 0;

   /* We probably only want to support buffer formats that have a
    * color format specification.
    */
   if (!vk_format_is_color(vk_format))
      return 0;

   const struct util_format_description *desc =
      vk_format_description(vk_format);
   assert(desc);

   VkFormatFeatureFlags flags = 0;
   if (desc->layout == UTIL_FORMAT_LAYOUT_PLAIN &&
       desc->colorspace == UTIL_FORMAT_COLORSPACE_RGB &&
       desc->is_array) {
      flags |=  VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
      if (v3dv_format->tex_type != TEXTURE_DATA_FORMAT_NO) {
         flags |= VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT |
                  VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
      }
   } else if (vk_format == VK_FORMAT_A2B10G10R10_UNORM_PACK32) {
      flags |= VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT |
               VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT |
               VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
   } else if (vk_format == VK_FORMAT_A2B10G10R10_UINT_PACK32 ||
              vk_format == VK_FORMAT_B10G11R11_UFLOAT_PACK32) {
      flags |= VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT |
               VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
   }

   if (desc->layout == UTIL_FORMAT_LAYOUT_PLAIN &&
       desc->is_array &&
       desc->nr_channels == 1 &&
       vk_format_is_int(vk_format)) {
      flags |= VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;
   }

   return flags;
}

bool
v3dv_buffer_format_supports_features(struct v3dv_device *device,
                                     VkFormat vk_format,
                                     VkFormatFeatureFlags features)
{
   const struct v3dv_format *v3dv_format = v3dv_X(device, get_format)(vk_format);
   const VkFormatFeatureFlags supported =
      buffer_format_features(vk_format, v3dv_format);
   return (supported & features) == features;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetPhysicalDeviceFormatProperties(VkPhysicalDevice physicalDevice,
                                       VkFormat format,
                                       VkFormatProperties* pFormatProperties)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, pdevice, physicalDevice);
   const struct v3dv_format *v3dv_format = v3dv_X(pdevice, get_format)(format);

   *pFormatProperties = (VkFormatProperties) {
      .linearTilingFeatures =
         image_format_features(pdevice, format, v3dv_format, VK_IMAGE_TILING_LINEAR),
      .optimalTilingFeatures =
         image_format_features(pdevice, format, v3dv_format, VK_IMAGE_TILING_OPTIMAL),
      .bufferFeatures =
         buffer_format_features(format, v3dv_format),
   };
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetPhysicalDeviceFormatProperties2(VkPhysicalDevice physicalDevice,
                                        VkFormat format,
                                        VkFormatProperties2 *pFormatProperties)
{
   v3dv_GetPhysicalDeviceFormatProperties(physicalDevice, format,
                                          &pFormatProperties->formatProperties);

   vk_foreach_struct(ext, pFormatProperties->pNext) {
      switch ((unsigned)ext->sType) {
      case VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT: {
         struct VkDrmFormatModifierPropertiesListEXT *list = (void *)ext;
         VK_OUTARRAY_MAKE(out, list->pDrmFormatModifierProperties,
                          &list->drmFormatModifierCount);
         if (pFormatProperties->formatProperties.linearTilingFeatures) {
            vk_outarray_append(&out, mod_props) {
               mod_props->drmFormatModifier = DRM_FORMAT_MOD_LINEAR;
               mod_props->drmFormatModifierPlaneCount = 1;
               mod_props->drmFormatModifierTilingFeatures =
                  pFormatProperties->formatProperties.linearTilingFeatures;
            }
         }
         if (pFormatProperties->formatProperties.optimalTilingFeatures) {
            vk_outarray_append(&out, mod_props) {
               mod_props->drmFormatModifier = DRM_FORMAT_MOD_BROADCOM_UIF;
               mod_props->drmFormatModifierPlaneCount = 1;
               mod_props->drmFormatModifierTilingFeatures =
                  pFormatProperties->formatProperties.optimalTilingFeatures;
            }
         }
         break;
      }
      default:
         v3dv_debug_ignored_stype(ext->sType);
         break;
      }
   }
}

static VkResult
get_image_format_properties(
   struct v3dv_physical_device *physical_device,
   const VkPhysicalDeviceImageFormatInfo2 *info,
   VkImageTiling tiling,
   VkImageFormatProperties *pImageFormatProperties,
   VkSamplerYcbcrConversionImageFormatProperties *pYcbcrImageFormatProperties)
{
   const struct v3dv_format *v3dv_format = v3dv_X(physical_device, get_format)(info->format);
   VkFormatFeatureFlags format_feature_flags =
      image_format_features(physical_device, info->format, v3dv_format, tiling);
   if (!format_feature_flags)
      goto unsupported;

   /* This allows users to create uncompressed views of compressed images,
    * however this is not something the hardware supports naturally and requires
    * the driver to lie when programming the texture state to make the hardware
    * sample with the uncompressed view correctly, and even then, there are
    * issues when running on real hardware.
    *
    * See https://gitlab.freedesktop.org/mesa/mesa/-/merge_requests/11336
    * for details.
    */
   if (info->flags & VK_IMAGE_CREATE_BLOCK_TEXEL_VIEW_COMPATIBLE_BIT)
      goto unsupported;

   if (info->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)) {
         goto unsupported;
      }

      /* Sampling of raster depth/stencil images is not supported. Since 1D
       * images are always raster, even if the user requested optimal tiling,
       * we can't have them be used as transfer sources, since that includes
       * using them for blit sources, which might require sampling.
       */
      if (info->type == VK_IMAGE_TYPE_1D &&
          vk_format_is_depth_or_stencil(info->format)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_SAMPLED_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
         goto unsupported;
      }

      /* Sampling of raster depth/stencil images is not supported. Since 1D
       * images are always raster, even if the user requested optimal tiling,
       * we can't allow sampling if the format is depth/stencil.
       */
      if (info->type == VK_IMAGE_TYPE_1D &&
          vk_format_is_depth_or_stencil(info->format)) {
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

   /* FIXME: these are taken from VkPhysicalDeviceLimits, we should just put
    * these limits available in the physical device and read them from there
    * wherever we need them.
    */
   switch (info->type) {
   case VK_IMAGE_TYPE_1D:
      pImageFormatProperties->maxExtent.width = 4096;
      pImageFormatProperties->maxExtent.height = 1;
      pImageFormatProperties->maxExtent.depth = 1;
      pImageFormatProperties->maxArrayLayers = 2048;
      pImageFormatProperties->maxMipLevels = 13; /* log2(maxWidth) + 1 */
      break;
   case VK_IMAGE_TYPE_2D:
      pImageFormatProperties->maxExtent.width = 4096;
      pImageFormatProperties->maxExtent.height = 4096;
      pImageFormatProperties->maxExtent.depth = 1;
      pImageFormatProperties->maxArrayLayers = 2048;
      pImageFormatProperties->maxMipLevels = 13; /* log2(maxWidth) + 1 */
      break;
   case VK_IMAGE_TYPE_3D:
      pImageFormatProperties->maxExtent.width = 4096;
      pImageFormatProperties->maxExtent.height = 4096;
      pImageFormatProperties->maxExtent.depth = 4096;
      pImageFormatProperties->maxArrayLayers = 1;
      pImageFormatProperties->maxMipLevels = 13; /* log2(maxWidth) + 1 */
      break;
   default:
      unreachable("bad VkImageType");
   }

   /* Our hw doesn't support 1D compressed textures. */
   if (info->type == VK_IMAGE_TYPE_1D &&
       vk_format_is_compressed(info->format)) {
       goto unsupported;
   }

   /* From the Vulkan 1.0 spec, section 34.1.1. Supported Sample Counts:
    *
    * sampleCounts will be set to VK_SAMPLE_COUNT_1_BIT if at least one of the
    * following conditions is true:
    *
    *   - tiling is VK_IMAGE_TILING_LINEAR
    *   - type is not VK_IMAGE_TYPE_2D
    *   - flags contains VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT
    *   - neither the VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT flag nor the
    *     VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT flag in
    *     VkFormatProperties::optimalTilingFeatures returned by
    *     vkGetPhysicalDeviceFormatProperties is set.
    */
   pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   if (tiling != VK_IMAGE_TILING_LINEAR &&
       info->type == VK_IMAGE_TYPE_2D &&
       !(info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT) &&
       (format_feature_flags & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT ||
        format_feature_flags & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
      pImageFormatProperties->sampleCounts |= VK_SAMPLE_COUNT_4_BIT;
   }

   if (tiling == VK_IMAGE_TILING_LINEAR)
      pImageFormatProperties->maxMipLevels = 1;

   pImageFormatProperties->maxResourceSize = 0xffffffff; /* 32-bit allocation */

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

static const VkExternalMemoryProperties prime_fd_props = {
   .externalMemoryFeatures = VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
                             VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT,
   .exportFromImportedHandleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
   .compatibleHandleTypes =
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
      VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
};

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetPhysicalDeviceImageFormatProperties(
   VkPhysicalDevice physicalDevice,
   VkFormat format,
   VkImageType type,
   VkImageTiling tiling,
   VkImageUsageFlags usage,
   VkImageCreateFlags createFlags,
   VkImageFormatProperties *pImageFormatProperties)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, physical_device, physicalDevice);

   const VkPhysicalDeviceImageFormatInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .pNext = NULL,
      .format = format,
      .type = type,
      .tiling = tiling,
      .usage = usage,
      .flags = createFlags,
   };

   return get_image_format_properties(physical_device, &info, tiling,
                                      pImageFormatProperties, NULL);
}

VKAPI_ATTR VkResult VKAPI_CALL
v3dv_GetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice physicalDevice,
                                             const VkPhysicalDeviceImageFormatInfo2 *base_info,
                                             VkImageFormatProperties2 *base_props)
{
   V3DV_FROM_HANDLE(v3dv_physical_device, physical_device, physicalDevice);
   const VkPhysicalDeviceExternalImageFormatInfo *external_info = NULL;
   const VkPhysicalDeviceImageDrmFormatModifierInfoEXT *drm_format_mod_info = NULL;
   VkExternalImageFormatProperties *external_props = NULL;
   VkImageTiling tiling = base_info->tiling;

   /* Extract input structs */
   vk_foreach_struct_const(s, base_info->pNext) {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO:
         external_info = (const void *) s;
         break;
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT:
         drm_format_mod_info = (const void *) s;
         switch (drm_format_mod_info->drmFormatModifier) {
         case DRM_FORMAT_MOD_LINEAR:
            tiling = VK_IMAGE_TILING_LINEAR;
            break;
         case DRM_FORMAT_MOD_BROADCOM_UIF:
            tiling = VK_IMAGE_TILING_OPTIMAL;
            break;
         default:
            assert("Unknown DRM format modifier");
         }
         break;
      default:
         v3dv_debug_ignored_stype(s->sType);
         break;
      }
   }

   assert(tiling == VK_IMAGE_TILING_OPTIMAL ||
          tiling == VK_IMAGE_TILING_LINEAR);

   /* Extract output structs */
   vk_foreach_struct(s, base_props->pNext) {
      switch (s->sType) {
      case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES:
         external_props = (void *) s;
         break;
      default:
         v3dv_debug_ignored_stype(s->sType);
         break;
      }
   }

   VkResult result =
      get_image_format_properties(physical_device, base_info, tiling,
                                  &base_props->imageFormatProperties, NULL);
   if (result != VK_SUCCESS)
      goto done;

   if (external_info && external_info->handleType != 0) {
      switch (external_info->handleType) {
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
         if (external_props)
            external_props->externalMemoryProperties = prime_fd_props;
         break;
      default:
         result = VK_ERROR_FORMAT_NOT_SUPPORTED;
         break;
      }
   }

done:
   return result;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetPhysicalDeviceSparseImageFormatProperties(
   VkPhysicalDevice physicalDevice,
   VkFormat format,
   VkImageType type,
   VkSampleCountFlagBits samples,
   VkImageUsageFlags usage,
   VkImageTiling tiling,
   uint32_t *pPropertyCount,
   VkSparseImageFormatProperties *pProperties)
{
   *pPropertyCount = 0;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetPhysicalDeviceSparseImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
   uint32_t *pPropertyCount,
   VkSparseImageFormatProperties2 *pProperties)
{
   *pPropertyCount = 0;
}

VKAPI_ATTR void VKAPI_CALL
v3dv_GetPhysicalDeviceExternalBufferProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
   VkExternalBufferProperties *pExternalBufferProperties)
{
   switch (pExternalBufferInfo->handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      pExternalBufferProperties->externalMemoryProperties = prime_fd_props;
      return;
   default: /* Unsupported */
      pExternalBufferProperties->externalMemoryProperties =
         (VkExternalMemoryProperties) {
            .compatibleHandleTypes = pExternalBufferInfo->handleType,
         };
      break;
   }
}
