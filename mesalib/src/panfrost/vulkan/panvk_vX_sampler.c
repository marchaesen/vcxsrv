/*
 * Copyright © 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "genxml/gen_macros.h"

#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_sampler.h"

#include "pan_encoder.h"
#include "pan_format.h"

#include "vk_format.h"
#include "vk_log.h"
#include "vk_ycbcr_conversion.h"

static enum mali_mipmap_mode
panvk_translate_sampler_mipmap_mode(VkSamplerMipmapMode mode)
{
   switch (mode) {
   case VK_SAMPLER_MIPMAP_MODE_NEAREST:
      return MALI_MIPMAP_MODE_NEAREST;
   case VK_SAMPLER_MIPMAP_MODE_LINEAR:
      return MALI_MIPMAP_MODE_TRILINEAR;
   default:
      unreachable("Invalid mipmap mode");
   }
}

static unsigned
panvk_translate_sampler_address_mode(VkSamplerAddressMode mode)
{
   switch (mode) {
   case VK_SAMPLER_ADDRESS_MODE_REPEAT:
      return MALI_WRAP_MODE_REPEAT;
   case VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:
      return MALI_WRAP_MODE_MIRRORED_REPEAT;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:
      return MALI_WRAP_MODE_CLAMP_TO_EDGE;
   case VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER:
      return MALI_WRAP_MODE_CLAMP_TO_BORDER;
   case VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE:
      return MALI_WRAP_MODE_MIRRORED_CLAMP_TO_EDGE;
   default:
      unreachable("Invalid wrap");
   }
}

static enum mali_func
panvk_translate_sampler_compare_func(const VkSamplerCreateInfo *pCreateInfo)
{
   if (!pCreateInfo->compareEnable)
      return MALI_FUNC_NEVER;

   return panfrost_flip_compare_func((enum mali_func)pCreateInfo->compareOp);
}

#if PAN_ARCH >= 10
static enum mali_reduction_mode
panvk_translate_reduction_mode(VkSamplerReductionMode reduction_mode)
{
   switch (reduction_mode) {
   case VK_SAMPLER_REDUCTION_MODE_WEIGHTED_AVERAGE:
      return MALI_REDUCTION_MODE_AVERAGE;
   case VK_SAMPLER_REDUCTION_MODE_MIN:
      return MALI_REDUCTION_MODE_MINIMUM;
   case VK_SAMPLER_REDUCTION_MODE_MAX:
      return MALI_REDUCTION_MODE_MAXIMUM;
   default:
      unreachable("Invalid reduction mode");
   }
}
#endif

#if PAN_ARCH == 7
static void
panvk_afbc_reswizzle_border_color(VkClearColorValue *border_color, VkFormat fmt)
{
   /* Doing border color reswizzle implies disabling support for
    * customBorderColorWithoutFormat. */

   enum pipe_format pfmt = vk_format_to_pipe_format(fmt);
   if (panfrost_format_is_yuv(pfmt) || util_format_is_depth_or_stencil(pfmt) ||
       !panfrost_format_supports_afbc(PAN_ARCH, pfmt))
      return;

   const struct util_format_description *fdesc = util_format_description(pfmt);
   if (fdesc->swizzle[0] == PIPE_SWIZZLE_Z &&
       fdesc->swizzle[2] == PIPE_SWIZZLE_X) {
      uint32_t red = border_color->uint32[0];

      border_color->uint32[0] = border_color->uint32[2];
      border_color->uint32[2] = red;
   }
}
#endif

static void
panvk_sampler_fill_desc(const struct VkSamplerCreateInfo *info,
                        struct mali_sampler_packed *desc,
                        VkClearColorValue border_color,
                        VkFilter min_filter, VkFilter mag_filter,
                        VkSamplerReductionMode reduction_mode)
{
   pan_pack(desc, SAMPLER, cfg) {
      cfg.magnify_nearest = mag_filter == VK_FILTER_NEAREST;
      cfg.minify_nearest = min_filter == VK_FILTER_NEAREST;
      cfg.mipmap_mode =
         panvk_translate_sampler_mipmap_mode(info->mipmapMode);
      cfg.normalized_coordinates = !info->unnormalizedCoordinates;
      cfg.clamp_integer_array_indices = false;

      /* Normalized float texture coordinates are rounded to fixed-point
       * before rounding to integer coordinates. When round_to_nearest_even is
       * enabled with VK_FILTER_NEAREST, the upper 2^-9 float coordinates in
       * each texel are rounded up to the next texel.
       *
       * The Vulkan 1.4.304 spec seems to allow both rounding modes for all
       * filters, but a CTS bug[1] causes test failures when round-to-nearest
       * is used with VK_FILTER_NEAREST.
       *
       * Regardless, disabling round_to_nearest_even for NEAREST filters
       * is a desirable precision improvement.
       *
       * [1]: https://gitlab.khronos.org/Tracker/vk-gl-cts/-/issues/5547
       */
      if (min_filter == VK_FILTER_NEAREST &&
          mag_filter == VK_FILTER_NEAREST)
         cfg.round_to_nearest_even = false;

      cfg.lod_bias = info->mipLodBias;
      cfg.minimum_lod = info->minLod;
      cfg.maximum_lod = info->maxLod;
      cfg.wrap_mode_s =
         panvk_translate_sampler_address_mode(info->addressModeU);
      cfg.wrap_mode_t =
         panvk_translate_sampler_address_mode(info->addressModeV);

      /* "
       * When unnormalizedCoordinates is VK_TRUE, images the sampler is used
       * with in the shader have the following requirements:
       * - The viewType must be either VK_IMAGE_VIEW_TYPE_1D or
       *   VK_IMAGE_VIEW_TYPE_2D.
       * - The image view must have a single layer and a single mip level.
       * "
       *
       * This means addressModeW should be ignored. We pick a default value
       * that works for normalized_coordinates=false.
       */
      cfg.wrap_mode_r =
         info->unnormalizedCoordinates
            ? MALI_WRAP_MODE_CLAMP_TO_EDGE
            : panvk_translate_sampler_address_mode(info->addressModeW);
      cfg.compare_function = panvk_translate_sampler_compare_func(info);
      cfg.border_color_r = border_color.uint32[0];
      cfg.border_color_g = border_color.uint32[1];
      cfg.border_color_b = border_color.uint32[2];
      cfg.border_color_a = border_color.uint32[3];

      if (info->anisotropyEnable && info->maxAnisotropy > 1) {
         cfg.maximum_anisotropy = info->maxAnisotropy;
         cfg.lod_algorithm = MALI_LOD_ALGORITHM_ANISOTROPIC;
      }

#if PAN_ARCH >= 10
   cfg.reduction_mode = panvk_translate_reduction_mode(reduction_mode);
#endif
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(CreateSampler)(VkDevice _device,
                              const VkSamplerCreateInfo *pCreateInfo,
                              const VkAllocationCallbacks *pAllocator,
                              VkSampler *pSampler)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk_sampler *sampler;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);

   sampler =
      vk_sampler_create(&device->vk, pCreateInfo, pAllocator, sizeof(*sampler));
   if (!sampler)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   STATIC_ASSERT(sizeof(sampler->descs[0]) >= pan_size(SAMPLER));

   VkFormat fmt;
   VkClearColorValue border_color =
      vk_sampler_border_color_value(pCreateInfo, &fmt);

#if PAN_ARCH == 7
   panvk_afbc_reswizzle_border_color(&border_color, fmt);
#endif

   sampler->desc_count = 1;
   panvk_sampler_fill_desc(pCreateInfo, &sampler->descs[0], border_color,
                           pCreateInfo->minFilter, pCreateInfo->magFilter,
                           sampler->vk.reduction_mode);

   /* In order to support CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT,
    * we need multiple sampler planes: at minimum we will need one for
    * luminance (the default), and one for chroma.
    */
   if (sampler->vk.ycbcr_conversion) {
      const VkFilter chroma_filter =
         sampler->vk.ycbcr_conversion->state.chroma_filter;
      if (pCreateInfo->magFilter != chroma_filter ||
          pCreateInfo->minFilter != chroma_filter) {
         sampler->desc_count = 2;
         panvk_sampler_fill_desc(pCreateInfo, &sampler->descs[1],
                                 border_color, chroma_filter, chroma_filter,
                                 sampler->vk.reduction_mode);
      }
   }

   *pSampler = panvk_sampler_to_handle(sampler);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(DestroySampler)(VkDevice _device, VkSampler _sampler,
                               const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_sampler, sampler, _sampler);

   if (!sampler)
      return;

   vk_sampler_destroy(&device->vk, pAllocator, &sampler->vk);
}
