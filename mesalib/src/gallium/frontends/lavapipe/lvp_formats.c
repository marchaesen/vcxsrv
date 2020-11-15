/*
 * Copyright © 2019 Red Hat.
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

#include "lvp_private.h"
#include "util/format/u_format.h"
#include "util/u_math.h"
#define COMMON_NAME(x) [VK_FORMAT_##x] = PIPE_FORMAT_##x

#define FLOAT_NAME(x) [VK_FORMAT_##x##_SFLOAT] = PIPE_FORMAT_##x##_FLOAT

static enum pipe_format format_to_vk_table[VK_FORMAT_ASTC_12x12_SRGB_BLOCK + 1] = {

   COMMON_NAME(R8_UNORM),
   COMMON_NAME(R8G8_UNORM),
   COMMON_NAME(R8G8B8_UNORM),
   COMMON_NAME(R8G8B8A8_UNORM),

   COMMON_NAME(R8_SNORM),
   COMMON_NAME(R8G8_SNORM),
   COMMON_NAME(R8G8B8_SNORM),
   COMMON_NAME(R8G8B8A8_SNORM),

   //   COMMON_NAME(R8_SRGB),
   COMMON_NAME(R8G8B8_SRGB),
   COMMON_NAME(R8G8B8A8_SRGB),

   COMMON_NAME(B8G8R8A8_UNORM),
   COMMON_NAME(R8G8B8A8_SRGB),
   COMMON_NAME(B8G8R8A8_SRGB),

   COMMON_NAME(R8_UINT),
   COMMON_NAME(R8G8_UINT),
   COMMON_NAME(R8G8B8_UINT),
   COMMON_NAME(R8G8B8A8_UINT),

   COMMON_NAME(R16_UINT),
   COMMON_NAME(R16G16_UINT),
   COMMON_NAME(R16G16B16_UINT),
   COMMON_NAME(R16G16B16A16_UINT),

   COMMON_NAME(R32_UINT),
   COMMON_NAME(R32G32_UINT),
   COMMON_NAME(R32G32B32_UINT),
   COMMON_NAME(R32G32B32A32_UINT),

   COMMON_NAME(R8_SINT),
   COMMON_NAME(R8G8_SINT),
   COMMON_NAME(R8G8B8_SINT),
   COMMON_NAME(R8G8B8A8_SINT),

   COMMON_NAME(R16_SINT),
   COMMON_NAME(R16G16_SINT),
   COMMON_NAME(R16G16B16_SINT),
   COMMON_NAME(R16G16B16A16_SINT),

   COMMON_NAME(R32_SINT),
   COMMON_NAME(R32G32_SINT),
   COMMON_NAME(R32G32B32_SINT),
   COMMON_NAME(R32G32B32A32_SINT),

   COMMON_NAME(R16_UNORM),
   COMMON_NAME(R16G16_UNORM),
   COMMON_NAME(R16G16B16A16_UNORM),

   COMMON_NAME(R16_SNORM),
   COMMON_NAME(R16G16_SNORM),
   COMMON_NAME(R16G16B16A16_SNORM),
   FLOAT_NAME(R16),
   FLOAT_NAME(R16G16),
   FLOAT_NAME(R16G16B16),
   FLOAT_NAME(R16G16B16A16),

   FLOAT_NAME(R32),
   FLOAT_NAME(R32G32),
   FLOAT_NAME(R32G32B32),
   FLOAT_NAME(R32G32B32A32),

   COMMON_NAME(S8_UINT),
   [VK_FORMAT_UNDEFINED] = PIPE_FORMAT_NONE,
   [VK_FORMAT_R5G6B5_UNORM_PACK16] = PIPE_FORMAT_B5G6R5_UNORM,
   [VK_FORMAT_A1R5G5B5_UNORM_PACK16] = PIPE_FORMAT_B5G5R5A1_UNORM,
   [VK_FORMAT_B4G4R4A4_UNORM_PACK16] = PIPE_FORMAT_A4R4G4B4_UNORM,
   [VK_FORMAT_D16_UNORM] = PIPE_FORMAT_Z16_UNORM,

   [VK_FORMAT_A8B8G8R8_UNORM_PACK32] = PIPE_FORMAT_R8G8B8A8_UNORM,
   [VK_FORMAT_A8B8G8R8_SNORM_PACK32] = PIPE_FORMAT_R8G8B8A8_SNORM,
   [VK_FORMAT_A8B8G8R8_UINT_PACK32] = PIPE_FORMAT_R8G8B8A8_UINT,
   [VK_FORMAT_A8B8G8R8_SINT_PACK32] = PIPE_FORMAT_R8G8B8A8_SINT,
   [VK_FORMAT_A8B8G8R8_SRGB_PACK32] = PIPE_FORMAT_R8G8B8A8_SRGB,

   [VK_FORMAT_A2B10G10R10_UNORM_PACK32] = PIPE_FORMAT_R10G10B10A2_UNORM,
   [VK_FORMAT_A2B10G10R10_UINT_PACK32] = PIPE_FORMAT_R10G10B10A2_UINT,

   [VK_FORMAT_B10G11R11_UFLOAT_PACK32] = PIPE_FORMAT_R11G11B10_FLOAT,
   [VK_FORMAT_E5B9G9R9_UFLOAT_PACK32] = PIPE_FORMAT_R9G9B9E5_FLOAT,

   [VK_FORMAT_X8_D24_UNORM_PACK32] = PIPE_FORMAT_Z24X8_UNORM,
   [VK_FORMAT_D32_SFLOAT] = PIPE_FORMAT_Z32_FLOAT,
   [VK_FORMAT_D24_UNORM_S8_UINT] = PIPE_FORMAT_Z24_UNORM_S8_UINT,
   [VK_FORMAT_D32_SFLOAT_S8_UINT] = PIPE_FORMAT_Z32_FLOAT_S8X24_UINT,

   [VK_FORMAT_BC1_RGB_UNORM_BLOCK] = PIPE_FORMAT_DXT1_RGB,
   [VK_FORMAT_BC1_RGBA_UNORM_BLOCK] = PIPE_FORMAT_DXT1_RGBA,
   [VK_FORMAT_BC2_UNORM_BLOCK] = PIPE_FORMAT_DXT3_RGBA,
   [VK_FORMAT_BC3_UNORM_BLOCK] = PIPE_FORMAT_DXT5_RGBA,
   [VK_FORMAT_BC4_UNORM_BLOCK] = PIPE_FORMAT_RGTC1_UNORM,
   [VK_FORMAT_BC5_UNORM_BLOCK] = PIPE_FORMAT_RGTC2_UNORM,

   [VK_FORMAT_BC1_RGB_SRGB_BLOCK] = PIPE_FORMAT_DXT1_SRGB,
   [VK_FORMAT_BC1_RGBA_SRGB_BLOCK] = PIPE_FORMAT_DXT1_SRGBA,
   [VK_FORMAT_BC2_SRGB_BLOCK] = PIPE_FORMAT_DXT3_SRGBA,
   [VK_FORMAT_BC3_SRGB_BLOCK] = PIPE_FORMAT_DXT5_SRGBA,

   [VK_FORMAT_BC4_SNORM_BLOCK] = PIPE_FORMAT_RGTC1_SNORM,
   [VK_FORMAT_BC5_SNORM_BLOCK] = PIPE_FORMAT_RGTC2_SNORM,

   [VK_FORMAT_BC6H_UFLOAT_BLOCK] = PIPE_FORMAT_BPTC_RGB_UFLOAT,
   [VK_FORMAT_BC6H_SFLOAT_BLOCK] = PIPE_FORMAT_BPTC_RGB_FLOAT,
   [VK_FORMAT_BC7_UNORM_BLOCK] = PIPE_FORMAT_BPTC_RGBA_UNORM,
   [VK_FORMAT_BC7_SRGB_BLOCK] = PIPE_FORMAT_BPTC_SRGBA,
};

enum pipe_format vk_format_to_pipe(VkFormat format)
{
   if (format > VK_FORMAT_ASTC_12x12_SRGB_BLOCK)
      return PIPE_FORMAT_NONE;
   return format_to_vk_table[format];
}

static void
lvp_physical_device_get_format_properties(struct lvp_physical_device *physical_device,
                                          VkFormat format,
                                          VkFormatProperties *out_properties)
{
   enum pipe_format pformat = vk_format_to_pipe(format);
   unsigned features = 0, buffer_features = 0;
   if (pformat == PIPE_FORMAT_NONE) {
     out_properties->linearTilingFeatures = 0;
     out_properties->optimalTilingFeatures = 0;
     out_properties->bufferFeatures = 0;
     return;
   }

   if (physical_device->pscreen->is_format_supported(physical_device->pscreen, pformat,
                                                     PIPE_TEXTURE_2D, 0, 0, PIPE_BIND_DEPTH_STENCIL)) {
      out_properties->linearTilingFeatures = 0;
      out_properties->optimalTilingFeatures = VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
         VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT |
         VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;

      out_properties->bufferFeatures = 0;
      return;
   }

   if (util_format_is_compressed(pformat)) {
      if (physical_device->pscreen->is_format_supported(physical_device->pscreen, pformat,
                                                        PIPE_TEXTURE_2D, 0, 0, PIPE_BIND_SAMPLER_VIEW)) {
         features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
         features |= VK_FORMAT_FEATURE_BLIT_SRC_BIT;
         features |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
         features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
      }
      out_properties->linearTilingFeatures = features;
      out_properties->optimalTilingFeatures = features;
      out_properties->bufferFeatures = buffer_features;
      return;
   }
   buffer_features = VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT;
   if (!util_format_is_srgb(pformat) &&
       physical_device->pscreen->is_format_supported(physical_device->pscreen, pformat,
                                                     PIPE_BUFFER, 0, 0, PIPE_BIND_VERTEX_BUFFER)) {
      buffer_features |= VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
   }

   if (physical_device->pscreen->is_format_supported(physical_device->pscreen, pformat,
                                                     PIPE_BUFFER, 0, 0, PIPE_BIND_CONSTANT_BUFFER)) {
      buffer_features |= VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT;
   }


   if (physical_device->pscreen->is_format_supported(physical_device->pscreen, pformat,
                                                     PIPE_TEXTURE_2D, 0, 0, PIPE_BIND_SAMPLER_VIEW)) {
      features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
      if (!util_format_is_pure_integer(pformat))
         features |= VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
   }

   if (physical_device->pscreen->is_format_supported(physical_device->pscreen, pformat,
                                                     PIPE_TEXTURE_2D, 0, 0, PIPE_BIND_RENDER_TARGET)) {
      features |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
      /* SNORM blending on llvmpipe fails CTS - disable for now */
      if (!util_format_is_snorm(pformat))
         features |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
      features |= VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT;
   }

   if (pformat == PIPE_FORMAT_R32_UINT || pformat == PIPE_FORMAT_R32_SINT) {
      features |= VK_FORMAT_FEATURE_STORAGE_IMAGE_ATOMIC_BIT;
      buffer_features |= VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;
   }

   if (pformat == PIPE_FORMAT_R11G11B10_FLOAT || pformat == PIPE_FORMAT_R9G9B9E5_FLOAT)
     features |= VK_FORMAT_FEATURE_BLIT_SRC_BIT;

   features |= VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
   if (pformat == PIPE_FORMAT_B5G6R5_UNORM)
     features |= VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
   if ((pformat != PIPE_FORMAT_R9G9B9E5_FLOAT) && util_format_get_nr_components(pformat) != 3) {
      features |= VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;
   }
   out_properties->linearTilingFeatures = features;
   out_properties->optimalTilingFeatures = features;
   out_properties->bufferFeatures = buffer_features;
   return;
}

void lvp_GetPhysicalDeviceFormatProperties(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkFormatProperties*                         pFormatProperties)
{
   LVP_FROM_HANDLE(lvp_physical_device, physical_device, physicalDevice);

   lvp_physical_device_get_format_properties(physical_device,
                                             format,
                                             pFormatProperties);
}

void lvp_GetPhysicalDeviceFormatProperties2(
        VkPhysicalDevice                            physicalDevice,
        VkFormat                                    format,
        VkFormatProperties2*                        pFormatProperties)
{
   LVP_FROM_HANDLE(lvp_physical_device, physical_device, physicalDevice);

   lvp_physical_device_get_format_properties(physical_device,
                                             format,
                                             &pFormatProperties->formatProperties);
}
static VkResult lvp_get_image_format_properties(struct lvp_physical_device *physical_device,
                                                 const VkPhysicalDeviceImageFormatInfo2 *info,
                                                 VkImageFormatProperties *pImageFormatProperties)
{
   VkFormatProperties format_props;
   VkFormatFeatureFlags format_feature_flags;
   VkExtent3D maxExtent;
   uint32_t maxMipLevels;
   uint32_t maxArraySize;
   VkSampleCountFlags sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   enum pipe_format pformat = vk_format_to_pipe(info->format);
   lvp_physical_device_get_format_properties(physical_device, info->format,
                                             &format_props);
   if (info->tiling == VK_IMAGE_TILING_LINEAR) {
      format_feature_flags = format_props.linearTilingFeatures;
   } else if (info->tiling == VK_IMAGE_TILING_OPTIMAL) {
      format_feature_flags = format_props.optimalTilingFeatures;
   } else {
      unreachable("bad VkImageTiling");
   }

   if (format_feature_flags == 0)
      goto unsupported;

   uint32_t max_2d_ext = physical_device->pscreen->get_param(physical_device->pscreen, PIPE_CAP_MAX_TEXTURE_2D_SIZE);
   uint32_t max_layers = physical_device->pscreen->get_param(physical_device->pscreen, PIPE_CAP_MAX_TEXTURE_ARRAY_LAYERS);
   switch (info->type) {
   default:
      unreachable("bad vkimage type\n");
   case VK_IMAGE_TYPE_1D:
      if (util_format_is_compressed(pformat))
         goto unsupported;

      maxExtent.width = max_2d_ext;
      maxExtent.height = 1;
      maxExtent.depth = 1;
      maxMipLevels = util_logbase2(max_2d_ext);
      maxArraySize = max_layers;
      break;
   case VK_IMAGE_TYPE_2D:
      maxExtent.width = max_2d_ext;
      maxExtent.height = max_2d_ext;
      maxExtent.depth = 1;
      maxMipLevels = util_logbase2(max_2d_ext);
      maxArraySize = max_layers;
      sampleCounts |= VK_SAMPLE_COUNT_4_BIT;
      break;
   case VK_IMAGE_TYPE_3D:
      maxExtent.width = max_2d_ext;
      maxExtent.height = max_2d_ext;
      maxExtent.depth = (1 << physical_device->pscreen->get_param(physical_device->pscreen, PIPE_CAP_MAX_TEXTURE_3D_LEVELS));
      maxMipLevels = util_logbase2(max_2d_ext);
      maxArraySize = 1;
      break;
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
      if (!(format_feature_flags & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_TRANSFER_DST_BIT) {
      if (!(format_feature_flags & VK_FORMAT_FEATURE_TRANSFER_DST_BIT)) {
         goto unsupported;
      }
   }

   if (info->usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) {
      if (!(format_feature_flags & (VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT |
                                    VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT))) {
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

VkResult lvp_GetPhysicalDeviceImageFormatProperties(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkImageType                                 type,
    VkImageTiling                               tiling,
    VkImageUsageFlags                           usage,
    VkImageCreateFlags                          createFlags,
    VkImageFormatProperties*                    pImageFormatProperties)
{
   LVP_FROM_HANDLE(lvp_physical_device, physical_device, physicalDevice);

   const VkPhysicalDeviceImageFormatInfo2 info = {
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
      .pNext = NULL,
      .format = format,
      .type = type,
      .tiling = tiling,
      .usage = usage,
      .flags = createFlags,
   };

   return lvp_get_image_format_properties(physical_device, &info,
                                           pImageFormatProperties);
}

VkResult lvp_GetPhysicalDeviceImageFormatProperties2(
        VkPhysicalDevice                            physicalDevice,
        const VkPhysicalDeviceImageFormatInfo2     *base_info,
        VkImageFormatProperties2                   *base_props)
{
   LVP_FROM_HANDLE(lvp_physical_device, physical_device, physicalDevice);
   VkResult result;
   result = lvp_get_image_format_properties(physical_device, base_info,
                                             &base_props->imageFormatProperties);
   if (result != VK_SUCCESS)
      return result;

   return VK_SUCCESS;
}

void lvp_GetPhysicalDeviceSparseImageFormatProperties(
    VkPhysicalDevice                            physicalDevice,
    VkFormat                                    format,
    VkImageType                                 type,
    uint32_t                                    samples,
    VkImageUsageFlags                           usage,
    VkImageTiling                               tiling,
    uint32_t*                                   pNumProperties,
    VkSparseImageFormatProperties*              pProperties)
{
   /* Sparse images are not yet supported. */
   *pNumProperties = 0;
}

void lvp_GetPhysicalDeviceSparseImageFormatProperties2(
        VkPhysicalDevice                            physicalDevice,
        const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
        uint32_t                                   *pPropertyCount,
        VkSparseImageFormatProperties2             *pProperties)
{
        /* Sparse images are not yet supported. */
        *pPropertyCount = 0;
}
