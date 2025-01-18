/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "hk_sampler.h"

#include "hk_device.h"
#include "hk_entrypoints.h"
#include "hk_instance.h"
#include "hk_physical_device.h"

#include "vk_format.h"
#include "vk_sampler.h"

#include "asahi/genxml/agx_pack.h"

static inline uint32_t
translate_address_mode(VkSamplerAddressMode addr_mode)
{
#define MODE(VK, AGX_) [VK_SAMPLER_ADDRESS_MODE_##VK] = AGX_WRAP_##AGX_
   static const uint8_t translate[] = {
      MODE(REPEAT, REPEAT),
      MODE(MIRRORED_REPEAT, MIRRORED_REPEAT),
      MODE(CLAMP_TO_EDGE, CLAMP_TO_EDGE),
      MODE(CLAMP_TO_BORDER, CLAMP_TO_BORDER),
      MODE(MIRROR_CLAMP_TO_EDGE, MIRRORED_CLAMP_TO_EDGE),
   };
#undef MODE

   assert(addr_mode < ARRAY_SIZE(translate));
   return translate[addr_mode];
}

static uint32_t
translate_texsamp_compare_op(VkCompareOp op)
{
#define OP(VK, AGX_) [VK_COMPARE_OP_##VK] = AGX_COMPARE_FUNC_##AGX_
   static const uint8_t translate[] = {
      OP(NEVER, NEVER),
      OP(LESS, LESS),
      OP(EQUAL, EQUAL),
      OP(LESS_OR_EQUAL, LEQUAL),
      OP(GREATER, GREATER),
      OP(NOT_EQUAL, NOT_EQUAL),
      OP(GREATER_OR_EQUAL, GEQUAL),
      OP(ALWAYS, ALWAYS),
   };
#undef OP

   assert(op < ARRAY_SIZE(translate));
   return translate[op];
}

static enum agx_filter
translate_filter(VkFilter filter)
{
   static_assert((enum agx_filter)VK_FILTER_NEAREST == AGX_FILTER_NEAREST);
   static_assert((enum agx_filter)VK_FILTER_LINEAR == AGX_FILTER_LINEAR);

   return (enum agx_filter)filter;
}

static enum agx_mip_filter
translate_mipfilter(VkSamplerMipmapMode mode)
{
   switch (mode) {
   case VK_SAMPLER_MIPMAP_MODE_NEAREST:
      return AGX_MIP_FILTER_NEAREST;

   case VK_SAMPLER_MIPMAP_MODE_LINEAR:
      return AGX_MIP_FILTER_LINEAR;

   default:
      unreachable("Invalid filter");
   }
}

static bool
uses_border(const VkSamplerCreateInfo *info)
{
   return info->addressModeU == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
          info->addressModeV == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER ||
          info->addressModeW == VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
}

static enum agx_border_colour
is_border_color_custom(VkBorderColor color, bool workaround_rgba4)
{
   switch (color) {
   case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
      /* We may need to workaround RGBA4 UNORM issues with opaque black. This
       * only affects float opaque black, there are no pure integer RGBA4
       * formats to worry about.
       */
      return workaround_rgba4;

   case VK_BORDER_COLOR_INT_CUSTOM_EXT:
   case VK_BORDER_COLOR_FLOAT_CUSTOM_EXT:
      return true;

   default:
      return false;
   }
}

/* Translate an American VkBorderColor into a Canadian agx_border_colour */
static enum agx_border_colour
translate_border_color(VkBorderColor color, bool custom_to_1,
                       bool workaround_rgba4)
{
   if (is_border_color_custom(color, workaround_rgba4)) {
      return custom_to_1 ? AGX_BORDER_COLOUR_OPAQUE_WHITE
                         : AGX_BORDER_COLOUR_TRANSPARENT_BLACK;
   }

   switch (color) {
   case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
   case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
      return AGX_BORDER_COLOUR_TRANSPARENT_BLACK;

   case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
   case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
      return AGX_BORDER_COLOUR_OPAQUE_BLACK;

   case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
   case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
      return AGX_BORDER_COLOUR_OPAQUE_WHITE;

   default:
      unreachable("invalid");
   }
}

static void
pack_sampler(const struct hk_physical_device *pdev,
             const struct VkSamplerCreateInfo *info, bool custom_to_1,
             bool workaround_rgba4, struct agx_sampler_packed *out)
{
   agx_pack(out, SAMPLER, cfg) {
      cfg.minimum_lod = info->minLod;
      cfg.maximum_lod = info->maxLod;
      cfg.magnify = translate_filter(info->magFilter);
      cfg.minify = translate_filter(info->minFilter);
      cfg.mip_filter = translate_mipfilter(info->mipmapMode);
      cfg.wrap_s = translate_address_mode(info->addressModeU);
      cfg.wrap_t = translate_address_mode(info->addressModeV);
      cfg.wrap_r = translate_address_mode(info->addressModeW);
      cfg.pixel_coordinates = info->unnormalizedCoordinates;

      cfg.seamful_cube_maps =
         info->flags & VK_SAMPLER_CREATE_NON_SEAMLESS_CUBE_MAP_BIT_EXT;

      if (info->compareEnable) {
         cfg.compare_func = translate_texsamp_compare_op(info->compareOp);
         cfg.compare_enable = true;
      }

      if (info->anisotropyEnable) {
         cfg.maximum_anisotropy =
            util_next_power_of_two(MAX2(info->maxAnisotropy, 1));
      } else {
         cfg.maximum_anisotropy = 1;
      }

      if (uses_border(info)) {
         cfg.border_colour = translate_border_color(
            info->borderColor, custom_to_1, workaround_rgba4);
      }
   }
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_CreateSampler(VkDevice device,
                 const VkSamplerCreateInfo *info /* pCreateInfo */,
                 const VkAllocationCallbacks *pAllocator, VkSampler *pSampler)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   struct hk_physical_device *pdev = hk_device_physical(dev);
   struct hk_instance *instance = (struct hk_instance *)pdev->vk.instance;
   struct hk_sampler *sampler;
   VkResult result;

   sampler = vk_sampler_create(&dev->vk, info, pAllocator, sizeof(*sampler));
   if (!sampler)
      return vk_error(dev, VK_ERROR_OUT_OF_HOST_MEMORY);

   bool workaround_rgba4 = instance->workaround_rgba4;
   bool custom_border =
      uses_border(info) &&
      is_border_color_custom(info->borderColor, workaround_rgba4);

   /* Sanity check the noborder setting. There's no way to recover from it being
    * wrong but at least we can make noise to lint for errors in the driconf.
    */
   if (HK_PERF(dev, NOBORDER) && custom_border) {
      fprintf(stderr, "custom border colour used, but emulation is disabled\n");
      fprintf(stderr, "border %u\n", info->borderColor);
      fprintf(stderr, "rgba4 workaround: %u\n", workaround_rgba4);
      fprintf(stderr, "unnorm %X\n", info->unnormalizedCoordinates);
      fprintf(stderr, "compare %X\n", info->compareEnable);
      fprintf(stderr, "value: %X, %X, %X, %X\n",
              sampler->vk.border_color_value.uint32[0],
              sampler->vk.border_color_value.uint32[1],
              sampler->vk.border_color_value.uint32[2],
              sampler->vk.border_color_value.uint32[3]);
      fprintf(stderr, "wraps: %X, %X, %X\n", info->addressModeU,
              info->addressModeV, info->addressModeW);

      /* Blow up debug builds so we can fix the driconf. Allow the rare
       * misrendering on release builds.
       */
      assert(0);
   }

   struct agx_sampler_packed samp;
   pack_sampler(pdev, info, true, workaround_rgba4, &samp);

   /* LOD bias passed in the descriptor set */
   sampler->lod_bias_fp16 = _mesa_float_to_half(info->mipLodBias);

   result =
      hk_sampler_heap_add(dev, samp, &sampler->planes[sampler->plane_count].hw);
   if (result != VK_SUCCESS) {
      hk_DestroySampler(device, hk_sampler_to_handle(sampler), pAllocator);
      return result;
   }

   sampler->plane_count++;

   /* In order to support CONVERSION_SEPARATE_RECONSTRUCTION_FILTER_BIT, we
    * need multiple sampler planes: at minimum we will need one for luminance
    * (the default), and one for chroma.  Each sampler plane needs its own
    * sampler table entry.  However, sampler table entries are very rare on
    * G13, and each plane would burn one of those. So we make sure to allocate
    * only the minimum amount that we actually need (i.e., either 1 or 2), and
    * then just copy the last sampler plane out as far as we need to fill the
    * number of image planes.
    */
   if (sampler->vk.ycbcr_conversion) {
      assert(!uses_border(info) &&
             "consequence of VUID-VkSamplerCreateInfo-addressModeU-01646");

      const VkFilter chroma_filter =
         sampler->vk.ycbcr_conversion->state.chroma_filter;
      if (info->magFilter != chroma_filter ||
          info->minFilter != chroma_filter) {
         VkSamplerCreateInfo plane2_info = *info;
         plane2_info.magFilter = chroma_filter;
         plane2_info.minFilter = chroma_filter;

         pack_sampler(pdev, &plane2_info, false, workaround_rgba4, &samp);
         result = hk_sampler_heap_add(
            dev, samp, &sampler->planes[sampler->plane_count].hw);

         if (result != VK_SUCCESS) {
            hk_DestroySampler(device, hk_sampler_to_handle(sampler),
                              pAllocator);
            return result;
         }

         sampler->plane_count++;
      }
   } else if (custom_border) {
      /* If the sampler uses custom border colours, we need both clamp-to-1
       * and clamp-to-0 variants. We treat these as planes.
       */
      pack_sampler(pdev, info, false, workaround_rgba4, &samp);
      result = hk_sampler_heap_add(dev, samp,
                                   &sampler->planes[sampler->plane_count].hw);

      if (result != VK_SUCCESS) {
         hk_DestroySampler(device, hk_sampler_to_handle(sampler), pAllocator);
         return result;
      }

      sampler->plane_count++;

      /* We also need to record the border.
       *
       * If there is a border colour component mapping, we need to swizzle with
       * it. Otherwise, we can assume there's nothing to do.
       */
      VkClearColorValue bc = sampler->vk.border_color_value;

      const VkSamplerBorderColorComponentMappingCreateInfoEXT *swiz_info =
         vk_find_struct_const(
            info->pNext,
            SAMPLER_BORDER_COLOR_COMPONENT_MAPPING_CREATE_INFO_EXT);

      if (swiz_info) {
         const bool is_int = vk_border_color_is_int(info->borderColor);
         bc = vk_swizzle_color_value(bc, swiz_info->components, is_int);
      }

      sampler->custom_border = bc;
      sampler->has_border = true;
   }

   *pSampler = hk_sampler_to_handle(sampler);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
hk_DestroySampler(VkDevice device, VkSampler _sampler,
                  const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(hk_device, dev, device);
   VK_FROM_HANDLE(hk_sampler, sampler, _sampler);

   if (!sampler)
      return;

   for (uint8_t plane = 0; plane < sampler->plane_count; plane++) {
      hk_sampler_heap_remove(dev, sampler->planes[plane].hw);
   }

   vk_sampler_destroy(&dev->vk, pAllocator, &sampler->vk);
}
