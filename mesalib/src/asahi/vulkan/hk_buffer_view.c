/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "hk_buffer_view.h"
#include "asahi/layout/layout.h"
#include "asahi/lib/agx_nir_lower_vbo.h"
#include "util/bitscan.h"
#include "util/format/u_format.h"
#include "util/format/u_formats.h"

#include "agx_helpers.h"
#include "agx_nir_passes.h"
#include "agx_pack.h"
#include "hk_buffer.h"
#include "hk_device.h"
#include "hk_entrypoints.h"
#include "hk_image.h"
#include "hk_physical_device.h"

#include "vk_format.h"

VkFormatFeatureFlags2
hk_get_buffer_format_features(struct hk_physical_device *pdev,
                              VkFormat vk_format)
{
   VkFormatFeatureFlags2 features = 0;
   enum pipe_format p_format = hk_format_to_pipe_format(vk_format);

   if (p_format == PIPE_FORMAT_NONE)
      return 0;

   if (agx_vbo_supports_format(p_format))
      features |= VK_FORMAT_FEATURE_2_VERTEX_BUFFER_BIT;

   if (ail_pixel_format[p_format].texturable &&
       !util_format_is_depth_or_stencil(p_format)) {

      /* Only power-of-two supported by hardware. We have common RGB32 emulation
       * code for GL, but we don't want to use it for VK as it has a performance
       * cost on every buffer view load.
       */
      if (util_is_power_of_two_nonzero(util_format_get_blocksize(p_format))) {
         features |= VK_FORMAT_FEATURE_2_UNIFORM_TEXEL_BUFFER_BIT |
                     VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_BIT |
                     VK_FORMAT_FEATURE_2_STORAGE_WRITE_WITHOUT_FORMAT_BIT;
      }

      if (p_format == PIPE_FORMAT_R32_UINT || p_format == PIPE_FORMAT_R32_SINT)
         features |= VK_FORMAT_FEATURE_2_STORAGE_TEXEL_BUFFER_ATOMIC_BIT;
   }

   return features;
}

VKAPI_ATTR VkResult VKAPI_CALL
hk_CreateBufferView(VkDevice _device, const VkBufferViewCreateInfo *pCreateInfo,
                    const VkAllocationCallbacks *pAllocator,
                    VkBufferView *pBufferView)
{
   VK_FROM_HANDLE(hk_device, device, _device);
   VK_FROM_HANDLE(hk_buffer, buffer, pCreateInfo->buffer);
   struct hk_buffer_view *view;
   VkResult result;

   view = vk_buffer_view_create(&device->vk, pCreateInfo, pAllocator,
                                sizeof(*view));
   if (!view)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   enum pipe_format format = hk_format_to_pipe_format(view->vk.format);
   const struct util_format_description *desc = util_format_description(format);

   uint8_t format_swizzle[4] = {
      desc->swizzle[0],
      desc->swizzle[1],
      desc->swizzle[2],
      desc->swizzle[3],
   };

   if (util_format_is_depth_or_stencil(format)) {
      assert(!util_format_is_depth_and_stencil(format) &&
             "separate stencil always used");

      /* Broadcast depth and stencil */
      format_swizzle[0] = 0;
      format_swizzle[1] = 0;
      format_swizzle[2] = 0;
      format_swizzle[3] = 0;
   }

   /* Decompose the offset into a multiple of 16-bytes (which we can include in
    * the address) and an extra texel-aligned tail offset of up to 15 bytes.
    *
    * This lets us offset partially in the shader instead, getting
    * around alignment restrictions on the base address pointer.
    */
   uint64_t base = hk_buffer_address(buffer, 0) + (view->vk.offset & ~0xf);
   uint32_t tail_offset_B = view->vk.offset & 0xf;
   uint32_t tail_offset_el = tail_offset_B / util_format_get_blocksize(format);
   assert(tail_offset_el * util_format_get_blocksize(format) == tail_offset_B &&
          "must be texel aligned");

   struct agx_texture_packed tex;
   agx_pack(&tex, TEXTURE, cfg) {
      cfg.dimension = AGX_TEXTURE_DIMENSION_2D;
      cfg.layout = AGX_LAYOUT_LINEAR;
      cfg.channels = ail_pixel_format[format].channels;
      cfg.type = ail_pixel_format[format].type;
      cfg.swizzle_r = agx_channel_from_pipe(format_swizzle[0]);
      cfg.swizzle_g = agx_channel_from_pipe(format_swizzle[1]);
      cfg.swizzle_b = agx_channel_from_pipe(format_swizzle[2]);
      cfg.swizzle_a = agx_channel_from_pipe(format_swizzle[3]);

      cfg.width = AGX_TEXTURE_BUFFER_WIDTH;
      cfg.height = DIV_ROUND_UP(view->vk.elements, cfg.width);
      cfg.first_level = cfg.last_level = 0;

      cfg.address = base;
      cfg.buffer_size_sw = view->vk.elements;
      cfg.buffer_offset_sw = tail_offset_el;

      cfg.srgb = (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB);
      cfg.srgb_2_channel = cfg.srgb && util_format_colormask(desc) == 0x3;

      cfg.depth = 1;
      cfg.stride = (cfg.width * util_format_get_blocksize(format)) - 16;
   }

   struct agx_pbe_packed pbe;
   agx_pack(&pbe, PBE, cfg) {
      cfg.dimension = AGX_TEXTURE_DIMENSION_2D;
      cfg.layout = AGX_LAYOUT_LINEAR;
      cfg.channels = ail_pixel_format[format].channels;
      cfg.type = ail_pixel_format[format].type;
      cfg.srgb = util_format_is_srgb(format);

      assert(desc->nr_channels >= 1 && desc->nr_channels <= 4);

      for (unsigned i = 0; i < desc->nr_channels; ++i) {
         if (desc->swizzle[i] == 0)
            cfg.swizzle_r = i;
         else if (desc->swizzle[i] == 1)
            cfg.swizzle_g = i;
         else if (desc->swizzle[i] == 2)
            cfg.swizzle_b = i;
         else if (desc->swizzle[i] == 3)
            cfg.swizzle_a = i;
      }

      cfg.buffer = base;
      cfg.buffer_offset_sw = tail_offset_el;

      cfg.width = AGX_TEXTURE_BUFFER_WIDTH;
      cfg.height = DIV_ROUND_UP(view->vk.elements, cfg.width);
      cfg.level = 0;
      cfg.stride = (cfg.width * util_format_get_blocksize(format)) - 4;
      cfg.layers = 1;
      cfg.levels = 1;
   };

   result = hk_descriptor_table_add(device, &device->images, &tex, sizeof(tex),
                                    &view->tex_desc_index);
   if (result != VK_SUCCESS) {
      vk_buffer_view_destroy(&device->vk, pAllocator, &view->vk);
      return result;
   }

   result = hk_descriptor_table_add(device, &device->images, &pbe, sizeof(pbe),
                                    &view->pbe_desc_index);
   if (result != VK_SUCCESS) {
      hk_descriptor_table_remove(device, &device->images, view->tex_desc_index);
      vk_buffer_view_destroy(&device->vk, pAllocator, &view->vk);
      return result;
   }

   *pBufferView = hk_buffer_view_to_handle(view);

   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
hk_DestroyBufferView(VkDevice _device, VkBufferView bufferView,
                     const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(hk_device, device, _device);
   VK_FROM_HANDLE(hk_buffer_view, view, bufferView);

   if (!view)
      return;

   hk_descriptor_table_remove(device, &device->images, view->tex_desc_index);
   hk_descriptor_table_remove(device, &device->images, view->pbe_desc_index);

   vk_buffer_view_destroy(&device->vk, pAllocator, &view->vk);
}
