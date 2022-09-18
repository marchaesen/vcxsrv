/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "hwdef/rogue_hw_utils.h"
#include "pvr_formats.h"
#include "pvr_private.h"
#include "util/log.h"
#include "util/macros.h"
#include "util/u_math.h"
#include "vk_enum_to_str.h"
#include "vk_format.h"
#include "vk_log.h"
#include "vk_util.h"

#define FORMAT(vk, tex_fmt, pack_mode, accum_format)           \
   [VK_FORMAT_##vk] = {                                        \
      .vk_format = VK_FORMAT_##vk,                             \
      .tex_format = ROGUE_TEXSTATE_FORMAT_##tex_fmt,           \
      .pbe_packmode = ROGUE_PBESTATE_PACKMODE_##pack_mode,     \
      .pbe_accum_format = PVR_PBE_ACCUM_FORMAT_##accum_format, \
      .supported = true,                                       \
   }

#define FORMAT_COMPRESSED(vk, tex_fmt)                          \
   [VK_FORMAT_##vk] = {                                         \
      .vk_format = VK_FORMAT_##vk,                              \
      .tex_format = ROGUE_TEXSTATE_FORMAT_COMPRESSED_##tex_fmt, \
      .pbe_packmode = ROGUE_PBESTATE_PACKMODE_INVALID,          \
      .pbe_accum_format = PVR_PBE_ACCUM_FORMAT_INVALID,         \
      .supported = true,                                        \
   }

struct pvr_format {
   VkFormat vk_format;
   uint32_t tex_format;
   uint32_t pbe_packmode;
   enum pvr_pbe_accum_format pbe_accum_format;
   bool supported;
};

static const struct pvr_format pvr_format_table[] = {
   /* VK_FORMAT_B4G4R4A4_UNORM_PACK16 = 3. */
   FORMAT(B4G4R4A4_UNORM_PACK16, A4R4G4B4, A4R4G4B4, U8),
   /* VK_FORMAT_R5G6B5_UNORM_PACK16 = 4. */
   FORMAT(R5G6B5_UNORM_PACK16, R5G6B5, R5G6B5, U8),
   /* VK_FORMAT_A1R5G5B5_UNORM_PACK16 = 8. */
   FORMAT(A1R5G5B5_UNORM_PACK16, A1R5G5B5, A1R5G5B5, U8),
   /* VK_FORMAT_R8_UNORM = 9. */
   FORMAT(R8_UNORM, U8, U8, U8),
   /* VK_FORMAT_R8_SNORM = 10. */
   FORMAT(R8_SNORM, S8, S8, S8),
   /* VK_FORMAT_R8_UINT = 13. */
   FORMAT(R8_UINT, U8, U8, UINT8),
   /* VK_FORMAT_R8_SINT = 14. */
   FORMAT(R8_SINT, S8, S8, SINT8),
   /* VK_FORMAT_R8G8_UNORM = 16. */
   FORMAT(R8G8_UNORM, U8U8, U8U8, U8),
   /* VK_FORMAT_R8G8_SNORM = 17. */
   FORMAT(R8G8_SNORM, S8S8, S8S8, S8),
   /* VK_FORMAT_R8G8_UINT = 20. */
   FORMAT(R8G8_UINT, U8U8, U8U8, UINT8),
   /* VK_FORMAT_R8G8_SINT = 21. */
   FORMAT(R8G8_SINT, S8S8, S8S8, SINT8),
   /* VK_FORMAT_R8G8B8A8_UNORM = 37. */
   FORMAT(R8G8B8A8_UNORM, U8U8U8U8, U8U8U8U8, U8),
   /* VK_FORMAT_R8G8B8A8_SNORM = 38. */
   FORMAT(R8G8B8A8_SNORM, S8S8S8S8, S8S8S8S8, S8),
   /* VK_FORMAT_R8G8B8A8_UINT = 41. */
   FORMAT(R8G8B8A8_UINT, U8U8U8U8, U8U8U8U8, UINT8),
   /* VK_FORMAT_R8G8B8A8_SINT = 42. */
   FORMAT(R8G8B8A8_SINT, S8S8S8S8, S8S8S8S8, SINT8),
   /* VK_FORMAT_R8G8B8A8_SRGB = 43. */
   FORMAT(R8G8B8A8_SRGB, U8U8U8U8, U8U8U8U8, F16),
   /* VK_FORMAT_B8G8R8A8_UNORM = 44. */
   FORMAT(B8G8R8A8_UNORM, U8U8U8U8, U8U8U8U8, U8),
   /* VK_FORMAT_B8G8R8A8_SRGB = 50. */
   FORMAT(B8G8R8A8_SRGB, U8U8U8U8, U8U8U8U8, F16),
   /* VK_FORMAT_A8B8G8R8_UNORM_PACK32 = 51. */
   FORMAT(A8B8G8R8_UNORM_PACK32, U8U8U8U8, U8U8U8U8, U8),
   /* VK_FORMAT_A8B8G8R8_SNORM_PACK32 = 52. */
   FORMAT(A8B8G8R8_SNORM_PACK32, S8S8S8S8, S8S8S8S8, S8),
   /* VK_FORMAT_A8B8G8R8_UINT_PACK32 = 55. */
   FORMAT(A8B8G8R8_UINT_PACK32, U8U8U8U8, U8U8U8U8, UINT8),
   /* VK_FORMAT_A8B8G8R8_SINT_PACK32 = 56. */
   FORMAT(A8B8G8R8_SINT_PACK32, S8S8S8S8, S8S8S8S8, SINT8),
   /* VK_FORMAT_A8B8G8R8_SRGB_PACK32 = 57. */
   FORMAT(A8B8G8R8_SRGB_PACK32, U8U8U8U8, U8U8U8U8, F16),
   /* VK_FORMAT_A2B10G10R10_UNORM_PACK32 = 64. */
   FORMAT(A2B10G10R10_UNORM_PACK32, A2R10B10G10, A2R10B10G10, F16),
   /* VK_FORMAT_A2B10G10R10_UINT_PACK32 = 68. */
   FORMAT(A2B10G10R10_UINT_PACK32, A2R10B10G10, U32, UINT32),
   /* VK_FORMAT_R16_UNORM = 70. */
   FORMAT(R16_UNORM, U16, U16, U16),
   /* VK_FORMAT_R16_SNORM = 71. */
   FORMAT(R16_SNORM, S16, S16, S16),
   /* VK_FORMAT_R16_UINT = 74. */
   FORMAT(R16_UINT, U16, U16, UINT16),
   /* VK_FORMAT_R16_SINT = 75. */
   FORMAT(R16_SINT, S16, S16, SINT16),
   /* VK_FORMAT_R16_SFLOAT = 76. */
   FORMAT(R16_SFLOAT, F16, F16, F16),
   /* VK_FORMAT_R16G16_UNORM = 77. */
   FORMAT(R16G16_UNORM, U16U16, U16U16, U16),
   /* VK_FORMAT_R16G16_SNORM = 78. */
   FORMAT(R16G16_SNORM, S16S16, S16S16, S16),
   /* VK_FORMAT_R16G16_UINT = 81. */
   FORMAT(R16G16_UINT, U16U16, U16U16, UINT16),
   /* VK_FORMAT_R16G16_SINT = 82. */
   FORMAT(R16G16_SINT, S16S16, S16S16, SINT16),
   /* VK_FORMAT_R16G16_SFLOAT = 83. */
   FORMAT(R16G16_SFLOAT, F16F16, F16F16, F16),
   /* VK_FORMAT_R16G16B16A16_UNORM = 91. */
   FORMAT(R16G16B16A16_UNORM, U16U16U16U16, U16U16U16U16, U16),
   /* VK_FORMAT_R16G16B16A16_SNORM = 92. */
   FORMAT(R16G16B16A16_SNORM, S16S16S16S16, S16S16S16S16, S16),
   /* VK_FORMAT_R16G16B16A16_UINT = 95. */
   FORMAT(R16G16B16A16_UINT, U16U16U16U16, U16U16U16U16, UINT16),
   /* VK_FORMAT_R16G16B16A16_SINT = 96 */
   FORMAT(R16G16B16A16_SINT, S16S16S16S16, S16S16S16S16, SINT16),
   /* VK_FORMAT_R16G16B16A16_SFLOAT = 97. */
   FORMAT(R16G16B16A16_SFLOAT, F16F16F16F16, F16F16F16F16, F16),
   /* VK_FORMAT_R32_UINT = 98. */
   FORMAT(R32_UINT, U32, U32, UINT32),
   /* VK_FORMAT_R32_SINT = 99. */
   FORMAT(R32_SINT, S32, S32, SINT32),
   /* VK_FORMAT_R32_SFLOAT = 100. */
   FORMAT(R32_SFLOAT, F32, F32, F32),
   /* VK_FORMAT_R32G32_UINT = 101. */
   FORMAT(R32G32_UINT, U32U32, U32U32, UINT32),
   /* VK_FORMAT_R32G32_SINT = 102. */
   FORMAT(R32G32_SINT, S32S32, S32S32, SINT32),
   /* VK_FORMAT_R32G32_SFLOAT = 103. */
   FORMAT(R32G32_SFLOAT, F32F32, F32F32, F32),
   /* VK_FORMAT_R32G32B32_UINT = 104. */
   FORMAT(R32G32B32_UINT, U32U32U32, U32U32U32, UINT32),
   /* VK_FORMAT_R32G32B32_SINT = 105. */
   FORMAT(R32G32B32_SINT, S32S32S32, S32S32S32, SINT32),
   /* VK_FORMAT_R32G32B32_SFLOAT = 106. */
   FORMAT(R32G32B32_SFLOAT, F32F32F32, F32F32F32, F32),
   /* VK_FORMAT_R32G32B32A32_UINT = 107. */
   FORMAT(R32G32B32A32_UINT, U32U32U32U32, U32U32U32U32, UINT32),
   /* VK_FORMAT_R32G32B32A32_SINT = 108. */
   FORMAT(R32G32B32A32_SINT, S32S32S32S32, S32S32S32S32, SINT32),
   /* VK_FORMAT_R32G32B32A32_SFLOAT = 109. */
   FORMAT(R32G32B32A32_SFLOAT, F32F32F32F32, F32F32F32F32, F32),
   /* VK_FORMAT_B10G11R11_UFLOAT_PACK32 = 122. */
   FORMAT(B10G11R11_UFLOAT_PACK32, F10F11F11, F10F11F11, F16),
   /* VK_FORMAT_E5B9G9R9_UFLOAT_PACK32 = 123. */
   FORMAT(E5B9G9R9_UFLOAT_PACK32, SE9995, SE9995, INVALID),
   /* VK_FORMAT_D16_UNORM = 124. */
   FORMAT(D16_UNORM, U16, U16, F16),
   /* VK_FORMAT_D32_SFLOAT = 126. */
   FORMAT(D32_SFLOAT, F32, F32, F16),
   /* VK_FORMAT_D24_UNORM_S8_UINT = 129. */
   FORMAT(D24_UNORM_S8_UINT, ST8U24, ST8U24, F16),
   /* VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK = 147. */
   FORMAT_COMPRESSED(ETC2_R8G8B8_UNORM_BLOCK, ETC2_RGB),
   /* VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK = 148. */
   FORMAT_COMPRESSED(ETC2_R8G8B8_SRGB_BLOCK, ETC2_RGB),
   /* VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK = 149. */
   FORMAT_COMPRESSED(ETC2_R8G8B8A1_UNORM_BLOCK, ETC2_PUNCHTHROUGHA),
   /* VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK = 150. */
   FORMAT_COMPRESSED(ETC2_R8G8B8A1_SRGB_BLOCK, ETC2_PUNCHTHROUGHA),
   /* VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK = 150. */
   FORMAT_COMPRESSED(ETC2_R8G8B8A8_UNORM_BLOCK, ETC2A_RGBA),
   /* VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK = 152. */
   FORMAT_COMPRESSED(ETC2_R8G8B8A8_SRGB_BLOCK, ETC2A_RGBA),
   /* VK_FORMAT_EAC_R11_UNORM_BLOCK = 153. */
   FORMAT_COMPRESSED(EAC_R11_UNORM_BLOCK, EAC_R11_UNSIGNED),
   /* VK_FORMAT_EAC_R11_SNORM_BLOCK = 154. */
   FORMAT_COMPRESSED(EAC_R11_SNORM_BLOCK, EAC_R11_SIGNED),
   /* VK_FORMAT_EAC_R11G11_UNORM_BLOCK = 155. */
   FORMAT_COMPRESSED(EAC_R11G11_UNORM_BLOCK, EAC_RG11_UNSIGNED),
   /* VK_FORMAT_EAC_R11G11_SNORM_BLOCK = 156. */
   FORMAT_COMPRESSED(EAC_R11G11_SNORM_BLOCK, EAC_RG11_SIGNED),
};

#undef FORMAT

static inline const struct pvr_format *pvr_get_format(VkFormat vk_format)
{
   if (vk_format < ARRAY_SIZE(pvr_format_table) &&
       pvr_format_table[vk_format].supported) {
      return &pvr_format_table[vk_format];
   }

   mesa_logd("Format %s(%d) not supported\n",
             vk_Format_to_str(vk_format),
             vk_format);

   return NULL;
}

uint32_t pvr_get_tex_format(VkFormat vk_format)
{
   const struct pvr_format *pvr_format = pvr_get_format(vk_format);
   if (pvr_format) {
      return pvr_format->tex_format;
   }

   return ROGUE_TEXSTATE_FORMAT_INVALID;
}

uint32_t pvr_get_pbe_packmode(VkFormat vk_format)
{
   const struct pvr_format *pvr_format = pvr_get_format(vk_format);
   if (pvr_format)
      return pvr_format->pbe_packmode;

   return ROGUE_PBESTATE_PACKMODE_INVALID;
}

uint32_t pvr_get_pbe_accum_format(VkFormat vk_format)
{
   const struct pvr_format *pvr_format = pvr_get_format(vk_format);
   if (pvr_format)
      return pvr_format->pbe_accum_format;

   return PVR_PBE_ACCUM_FORMAT_INVALID;
}

static VkFormatFeatureFlags
pvr_get_image_format_features(const struct pvr_format *pvr_format,
                              VkImageTiling vk_tiling)
{
   VkFormatFeatureFlags flags = 0;
   VkImageAspectFlags aspects;

   if (!pvr_format)
      return 0;

   aspects = vk_format_aspects(pvr_format->vk_format);
   if (aspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
      flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
               VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
               VK_FORMAT_FEATURE_BLIT_SRC_BIT;
   }

   return flags;
}

const uint8_t *pvr_get_format_swizzle(VkFormat vk_format)
{
   const struct util_format_description *vf = vk_format_description(vk_format);

   return vf->swizzle;
}

static VkFormatFeatureFlags
pvr_get_buffer_format_features(const struct pvr_format *pvr_format)
{
   VkFormatFeatureFlags flags = 0;

   if (!pvr_format)
      return 0;

   return flags;
}

void pvr_GetPhysicalDeviceFormatProperties2(
   VkPhysicalDevice physicalDevice,
   VkFormat format,
   VkFormatProperties2 *pFormatProperties)
{
   const struct pvr_format *pvr_format = pvr_get_format(format);

   pFormatProperties->formatProperties = (VkFormatProperties){
      .linearTilingFeatures =
         pvr_get_image_format_features(pvr_format, VK_IMAGE_TILING_LINEAR),
      .optimalTilingFeatures =
         pvr_get_image_format_features(pvr_format, VK_IMAGE_TILING_OPTIMAL),
      .bufferFeatures = pvr_get_buffer_format_features(pvr_format),
   };

   vk_foreach_struct (ext, pFormatProperties->pNext) {
      pvr_debug_ignored_stype(ext->sType);
   }
}

static VkResult
pvr_get_image_format_properties(struct pvr_physical_device *pdevice,
                                const VkPhysicalDeviceImageFormatInfo2 *info,
                                VkImageFormatProperties *pImageFormatProperties)
{
   /* Input attachments aren't rendered but they must have the same size
    * restrictions as any framebuffer attachment.
    */
   const VkImageUsageFlags render_usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT |
      VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
      VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
   const struct pvr_format *pvr_format = pvr_get_format(info->format);
   VkFormatFeatureFlags tiling_features;
   VkResult result;

   if (!pvr_format) {
      result = vk_error(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED);
      goto err_unsupported_format;
   }

   tiling_features = pvr_get_image_format_features(pvr_format, info->tiling);
   if (tiling_features == 0) {
      result = vk_error(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED);
      goto err_unsupported_format;
   }

   /* If VK_IMAGE_CREATE_EXTENDED_USAGE_BIT is set, the driver can't decide if a
    * specific format isn't supported based on the usage.
    */
   if ((info->flags & VK_IMAGE_CREATE_EXTENDED_USAGE_BIT) == 0 &&
       info->usage & (VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT |
                      VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) &&
       pvr_format->pbe_accum_format == PVR_PBE_ACCUM_FORMAT_INVALID) {
      result = vk_error(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED);
      goto err_unsupported_format;
   }

   if (info->type == VK_IMAGE_TYPE_3D) {
      const VkImageUsageFlags transfer_usage =
         VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
         VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT;

      /* We don't support 3D depth/stencil images. */
      if (tiling_features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
         result = vk_error(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED);
         goto err_unsupported_format;
      }

      /* Linear tiled 3D images may only be used for transfer or blit
       * operations.
       */
      if (info->tiling == VK_IMAGE_TILING_LINEAR &&
          info->usage & ~transfer_usage) {
         result = vk_error(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED);
         goto err_unsupported_format;
      }
   }

   if (info->usage & render_usage) {
      const uint32_t max_render_size =
         rogue_get_render_size_max(&pdevice->dev_info);

      pImageFormatProperties->maxExtent.width = max_render_size;
      pImageFormatProperties->maxExtent.height = max_render_size;
      pImageFormatProperties->maxExtent.depth = PVR_MAX_TEXTURE_EXTENT_Z;
   } else {
      const uint32_t max_texture_extent_xy =
         PVRX(TEXSTATE_IMAGE_WORD0_WIDTH_MAX_SIZE) + 1U;

      pImageFormatProperties->maxExtent.width = max_texture_extent_xy;
      pImageFormatProperties->maxExtent.height = max_texture_extent_xy;
      pImageFormatProperties->maxExtent.depth = PVR_MAX_TEXTURE_EXTENT_Z;
   }

   if (info->tiling == VK_IMAGE_TILING_LINEAR) {
      pImageFormatProperties->maxExtent.depth = 1;
      pImageFormatProperties->maxArrayLayers = 1;
      pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   } else {
      /* Default value is the minimum value found in all existing cores. */
      const uint32_t max_multisample =
         PVR_GET_FEATURE_VALUE(&pdevice->dev_info, max_multisample, 4);

      const uint32_t max_sample_bits = ((max_multisample << 1) - 1);

      pImageFormatProperties->maxArrayLayers = PVR_MAX_ARRAY_LAYERS;
      pImageFormatProperties->sampleCounts = max_sample_bits;
   }

   if (!(tiling_features & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT ||
         tiling_features & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) {
      pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
   }

   switch (info->type) {
   case VK_IMAGE_TYPE_1D:
      pImageFormatProperties->maxExtent.height = 1;
      pImageFormatProperties->maxExtent.depth = 1;
      pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
      break;

   case VK_IMAGE_TYPE_2D:
      pImageFormatProperties->maxExtent.depth = 1;

      /* If a 2D image is created to be used in a cube map, then the sample
       * count must be restricted to 1 sample.
       */
      if (info->flags & VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT)
         pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;

      break;

   case VK_IMAGE_TYPE_3D:
      pImageFormatProperties->maxArrayLayers = 1;
      pImageFormatProperties->sampleCounts = VK_SAMPLE_COUNT_1_BIT;
      break;

   default:
      unreachable("Invalid image type.");
   }

   /* The spec says maxMipLevels may be 1 when tiling is VK_IMAGE_TILING_LINEAR
    * or VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT, so for simplicity don't
    * support miplevels for these tilings.
    */
   if (info->tiling == VK_IMAGE_TILING_LINEAR) {
      pImageFormatProperties->maxMipLevels = 1;
   } else {
      const uint32_t max_size = MAX3(pImageFormatProperties->maxExtent.width,
                                     pImageFormatProperties->maxExtent.height,
                                     pImageFormatProperties->maxExtent.depth);

      pImageFormatProperties->maxMipLevels = util_logbase2(max_size) + 1U;
   }

   /* Return 2GB (minimum required from spec).
    *
    * From the Vulkan spec:
    *
    *    maxResourceSize is an upper bound on the total image size in bytes,
    *    inclusive of all image subresources. Implementations may have an
    *    address space limit on total size of a resource, which is advertised by
    *    this property. maxResourceSize must be at least 2^31.
    */
   pImageFormatProperties->maxResourceSize = 2ULL * 1024 * 1024 * 1024;

   return VK_SUCCESS;

err_unsupported_format:
   /* From the Vulkan 1.0.42 spec:
    *
    *    If the combination of parameters to
    *    vkGetPhysicalDeviceImageFormatProperties2 is not supported by the
    *    implementation for use in vkCreateImage, then all members of
    *    imageFormatProperties will be filled with zero.
    */
   *pImageFormatProperties = (VkImageFormatProperties){ 0 };

   return result;
}

/* FIXME: Should this be returning VK_ERROR_FORMAT_NOT_SUPPORTED when tiling is
 * linear and the image type is 3D or flags contains
 * VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT? This should avoid well behaved apps
 * attempting to create invalid image views, as pvr_pack_tex_state() will return
 * VK_ERROR_FORMAT_NOT_SUPPORTED in these cases.
 */
VkResult pvr_GetPhysicalDeviceImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceImageFormatInfo2 *pImageFormatInfo,
   VkImageFormatProperties2 *pImageFormatProperties)
{
   const VkPhysicalDeviceExternalImageFormatInfo *external_info = NULL;
   PVR_FROM_HANDLE(pvr_physical_device, pdevice, physicalDevice);
   VkExternalImageFormatProperties *external_props = NULL;
   VkResult result;

   result = pvr_get_image_format_properties(
      pdevice,
      pImageFormatInfo,
      &pImageFormatProperties->imageFormatProperties);
   if (result != VK_SUCCESS)
      return result;

   /* Extract input structs */
   vk_foreach_struct_const (ext, pImageFormatInfo->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO:
         external_info = (const void *)ext;
         break;
      default:
         pvr_debug_ignored_stype(ext->sType);
         break;
      }
   }

   /* Extract output structs */
   vk_foreach_struct (ext, pImageFormatProperties->pNext) {
      switch (ext->sType) {
      case VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES:
         external_props = (void *)ext;
         break;
      default:
         pvr_debug_ignored_stype(ext->sType);
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
      switch (external_info->handleType) {
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
      case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
         if (!external_props)
            break;

         external_props->externalMemoryProperties.externalMemoryFeatures =
            VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
            VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
         external_props->externalMemoryProperties.compatibleHandleTypes =
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
         external_props->externalMemoryProperties.exportFromImportedHandleTypes =
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
            VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
         break;
      default:
         return vk_error(pdevice, VK_ERROR_FORMAT_NOT_SUPPORTED);
      }
   }

   return VK_SUCCESS;
}

void pvr_GetPhysicalDeviceSparseImageFormatProperties(
   VkPhysicalDevice physicalDevice,
   VkFormat format,
   VkImageType type,
   uint32_t samples,
   VkImageUsageFlags usage,
   VkImageTiling tiling,
   uint32_t *pNumProperties,
   VkSparseImageFormatProperties *pProperties)
{
   /* Sparse images are not yet supported. */
   *pNumProperties = 0;
}

void pvr_GetPhysicalDeviceSparseImageFormatProperties2(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceSparseImageFormatInfo2 *pFormatInfo,
   uint32_t *pPropertyCount,
   VkSparseImageFormatProperties2 *pProperties)
{
   /* Sparse images are not yet supported. */
   *pPropertyCount = 0;
}

void pvr_GetPhysicalDeviceExternalBufferProperties(
   VkPhysicalDevice physicalDevice,
   const VkPhysicalDeviceExternalBufferInfo *pExternalBufferInfo,
   VkExternalBufferProperties *pExternalBufferProperties)
{
   /* The Vulkan 1.0.42 spec says "handleType must be a valid
    * VkExternalMemoryHandleTypeFlagBits value" in
    * VkPhysicalDeviceExternalBufferInfo. This differs from
    * VkPhysicalDeviceExternalImageFormatInfo, which surprisingly permits
    * handleType == 0.
    */
   assert(pExternalBufferInfo->handleType != 0);

   /* All of the current flags are for sparse which we don't support. */
   if (pExternalBufferInfo->flags)
      goto unsupported;

   switch (pExternalBufferInfo->handleType) {
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT:
   case VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT:
      /* clang-format off */
      pExternalBufferProperties->externalMemoryProperties.externalMemoryFeatures =
         VK_EXTERNAL_MEMORY_FEATURE_EXPORTABLE_BIT |
         VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT;
      pExternalBufferProperties->externalMemoryProperties.exportFromImportedHandleTypes =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      pExternalBufferProperties->externalMemoryProperties.compatibleHandleTypes =
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT |
         VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT;
      /* clang-format on */
      return;
   default:
      break;
   }

unsupported:
   /* From the Vulkan 1.1.113 spec:
    *
    *    compatibleHandleTypes must include at least handleType.
    */
   pExternalBufferProperties->externalMemoryProperties =
      (VkExternalMemoryProperties){
         .compatibleHandleTypes = pExternalBufferInfo->handleType,
      };
}

bool pvr_format_is_pbe_downscalable(VkFormat vk_format)
{
   if (vk_format_is_int(vk_format)) {
      /* PBE downscale behavior for integer formats does not match Vulkan
       * spec. Vulkan requires a single sample to be chosen instead of
       * taking the average sample color.
       */
      return false;
   }

   switch (pvr_get_pbe_packmode(vk_format)) {
   default:
      return true;

   case ROGUE_PBESTATE_PACKMODE_U16U16U16U16:
   case ROGUE_PBESTATE_PACKMODE_S16S16S16S16:
   case ROGUE_PBESTATE_PACKMODE_U32U32U32U32:
   case ROGUE_PBESTATE_PACKMODE_S32S32S32S32:
   case ROGUE_PBESTATE_PACKMODE_F32F32F32F32:
   case ROGUE_PBESTATE_PACKMODE_U16U16U16:
   case ROGUE_PBESTATE_PACKMODE_S16S16S16:
   case ROGUE_PBESTATE_PACKMODE_U32U32U32:
   case ROGUE_PBESTATE_PACKMODE_S32S32S32:
   case ROGUE_PBESTATE_PACKMODE_F32F32F32:
   case ROGUE_PBESTATE_PACKMODE_U16U16:
   case ROGUE_PBESTATE_PACKMODE_S16S16:
   case ROGUE_PBESTATE_PACKMODE_U32U32:
   case ROGUE_PBESTATE_PACKMODE_S32S32:
   case ROGUE_PBESTATE_PACKMODE_F32F32:
   case ROGUE_PBESTATE_PACKMODE_U24ST8:
   case ROGUE_PBESTATE_PACKMODE_ST8U24:
   case ROGUE_PBESTATE_PACKMODE_U16:
   case ROGUE_PBESTATE_PACKMODE_S16:
   case ROGUE_PBESTATE_PACKMODE_U32:
   case ROGUE_PBESTATE_PACKMODE_S32:
   case ROGUE_PBESTATE_PACKMODE_F32:
   case ROGUE_PBESTATE_PACKMODE_X24U8F32:
   case ROGUE_PBESTATE_PACKMODE_X24X8F32:
   case ROGUE_PBESTATE_PACKMODE_X24G8X32:
   case ROGUE_PBESTATE_PACKMODE_X8U24:
   case ROGUE_PBESTATE_PACKMODE_U8X24:
   case ROGUE_PBESTATE_PACKMODE_PBYTE:
   case ROGUE_PBESTATE_PACKMODE_PWORD:
   case ROGUE_PBESTATE_PACKMODE_INVALID:
      return false;
   }
}
