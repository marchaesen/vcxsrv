/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * Based on u_format.h which is:
 * Copyright 2009-2010 Vmware, Inc.
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

#ifndef VK_FORMAT_H
#define VK_FORMAT_H

#include <assert.h>
#include <util/macros.h>
#include <util/format/u_format.h>
#include <vulkan/util/vk_format.h>

#include <vulkan/vulkan.h>

static inline const struct util_format_description *
vk_format_description(VkFormat format)
{
   return util_format_description(vk_format_to_pipe_format(format));
}

/**
 * Return total bits needed for the pixel format per block.
 */
static inline unsigned
vk_format_get_blocksizebits(VkFormat format)
{
   return util_format_get_blocksizebits(vk_format_to_pipe_format(format));
}

/**
 * Return bytes per block (not pixel) for the given format.
 */
static inline unsigned
vk_format_get_blocksize(VkFormat format)
{
   return util_format_get_blocksize(vk_format_to_pipe_format(format));
}

static inline unsigned
vk_format_get_blockwidth(VkFormat format)
{
   return util_format_get_blockwidth(vk_format_to_pipe_format(format));
}

static inline unsigned
vk_format_get_blockheight(VkFormat format)
{
   return util_format_get_blockheight(vk_format_to_pipe_format(format));
}

static inline unsigned
vk_format_get_block_count_width(VkFormat format, unsigned width)
{
   return util_format_get_nblocksx(vk_format_to_pipe_format(format), width);
}

static inline unsigned
vk_format_get_block_count_height(VkFormat format, unsigned height)
{
   return util_format_get_nblocksy(vk_format_to_pipe_format(format), height);
}

static inline unsigned
vk_format_get_block_count(VkFormat format, unsigned width, unsigned height)
{
   return util_format_get_nblocks(vk_format_to_pipe_format(format),
                                  width, height);
}

static inline VkImageAspectFlags
vk_format_aspects(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_UNDEFINED:
      return 0;

   case VK_FORMAT_S8_UINT:
      return VK_IMAGE_ASPECT_STENCIL_BIT;

   case VK_FORMAT_D16_UNORM_S8_UINT:
   case VK_FORMAT_D24_UNORM_S8_UINT:
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;

   case VK_FORMAT_D16_UNORM:
   case VK_FORMAT_X8_D24_UNORM_PACK32:
   case VK_FORMAT_D32_SFLOAT:
      return VK_IMAGE_ASPECT_DEPTH_BIT;

   default:
      return VK_IMAGE_ASPECT_COLOR_BIT;
   }
}

static inline enum pipe_swizzle
tu_swizzle_conv(VkComponentSwizzle component,
                const unsigned char chan[4],
                VkComponentSwizzle vk_swiz)
{
   int x;

   if (vk_swiz == VK_COMPONENT_SWIZZLE_IDENTITY)
      vk_swiz = component;
   switch (vk_swiz) {
   case VK_COMPONENT_SWIZZLE_ZERO:
      return PIPE_SWIZZLE_0;
   case VK_COMPONENT_SWIZZLE_ONE:
      return PIPE_SWIZZLE_1;
   case VK_COMPONENT_SWIZZLE_R:
      for (x = 0; x < 4; x++)
         if (chan[x] == 0)
            return x;
      return PIPE_SWIZZLE_0;
   case VK_COMPONENT_SWIZZLE_G:
      for (x = 0; x < 4; x++)
         if (chan[x] == 1)
            return x;
      return PIPE_SWIZZLE_0;
   case VK_COMPONENT_SWIZZLE_B:
      for (x = 0; x < 4; x++)
         if (chan[x] == 2)
            return x;
      return PIPE_SWIZZLE_0;
   case VK_COMPONENT_SWIZZLE_A:
      for (x = 0; x < 4; x++)
         if (chan[x] == 3)
            return x;
      return PIPE_SWIZZLE_1;
   default:
      unreachable("Illegal swizzle");
   }
}

static inline void
vk_format_compose_swizzles(const VkComponentMapping *mapping,
                           const unsigned char swz[4],
                           enum pipe_swizzle dst[4])
{
   dst[0] = tu_swizzle_conv(VK_COMPONENT_SWIZZLE_R, swz, mapping->r);
   dst[1] = tu_swizzle_conv(VK_COMPONENT_SWIZZLE_G, swz, mapping->g);
   dst[2] = tu_swizzle_conv(VK_COMPONENT_SWIZZLE_B, swz, mapping->b);
   dst[3] = tu_swizzle_conv(VK_COMPONENT_SWIZZLE_A, swz, mapping->a);
}

static inline bool
vk_format_is_compressed(VkFormat format)
{
   return util_format_is_compressed(vk_format_to_pipe_format(format));
}

static inline bool
vk_format_has_depth(VkFormat format)
{
   const struct util_format_description *desc = vk_format_description(format);

   return util_format_has_depth(desc);
}

static inline bool
vk_format_has_stencil(VkFormat format)
{
   const struct util_format_description *desc = vk_format_description(format);

   return util_format_has_stencil(desc);
}

static inline bool
vk_format_is_depth_or_stencil(VkFormat format)
{
   return vk_format_has_depth(format) || vk_format_has_stencil(format);
}

static inline bool
vk_format_is_color(VkFormat format)
{
   return !vk_format_is_depth_or_stencil(format);
}

static inline bool
vk_format_has_alpha(VkFormat format)
{
   return util_format_has_alpha(vk_format_to_pipe_format(format));
}

static inline VkFormat
vk_format_depth_only(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_D16_UNORM_S8_UINT:
      return VK_FORMAT_D16_UNORM;
   case VK_FORMAT_D24_UNORM_S8_UINT:
      return VK_FORMAT_X8_D24_UNORM_PACK32;
   case VK_FORMAT_D32_SFLOAT_S8_UINT:
      return VK_FORMAT_D32_SFLOAT;
   default:
      return format;
   }
}

static inline bool
vk_format_is_int(VkFormat format)
{
   return util_format_is_pure_integer(vk_format_to_pipe_format(format));
}

static inline bool
vk_format_is_uint(VkFormat format)
{
   return util_format_is_pure_uint(vk_format_to_pipe_format(format));
}

static inline bool
vk_format_is_sint(VkFormat format)
{
   return util_format_is_pure_sint(vk_format_to_pipe_format(format));
}

static inline bool
vk_format_is_srgb(VkFormat format)
{
   return util_format_is_srgb(vk_format_to_pipe_format(format));
}

static inline VkFormat
vk_format_no_srgb(VkFormat format)
{
   switch (format) {
   case VK_FORMAT_R8_SRGB:
      return VK_FORMAT_R8_UNORM;
   case VK_FORMAT_R8G8_SRGB:
      return VK_FORMAT_R8G8_UNORM;
   case VK_FORMAT_R8G8B8_SRGB:
      return VK_FORMAT_R8G8B8_UNORM;
   case VK_FORMAT_B8G8R8_SRGB:
      return VK_FORMAT_B8G8R8_UNORM;
   case VK_FORMAT_R8G8B8A8_SRGB:
      return VK_FORMAT_R8G8B8A8_UNORM;
   case VK_FORMAT_B8G8R8A8_SRGB:
      return VK_FORMAT_B8G8R8A8_UNORM;
   case VK_FORMAT_A8B8G8R8_SRGB_PACK32:
      return VK_FORMAT_A8B8G8R8_UNORM_PACK32;
   case VK_FORMAT_BC1_RGB_SRGB_BLOCK:
      return VK_FORMAT_BC1_RGB_UNORM_BLOCK;
   case VK_FORMAT_BC1_RGBA_SRGB_BLOCK:
      return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
   case VK_FORMAT_BC2_SRGB_BLOCK:
      return VK_FORMAT_BC2_UNORM_BLOCK;
   case VK_FORMAT_BC3_SRGB_BLOCK:
      return VK_FORMAT_BC3_UNORM_BLOCK;
   case VK_FORMAT_BC7_SRGB_BLOCK:
      return VK_FORMAT_BC7_UNORM_BLOCK;
   case VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK:
      return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
   case VK_FORMAT_ETC2_R8G8B8A1_SRGB_BLOCK:
      return VK_FORMAT_ETC2_R8G8B8A1_UNORM_BLOCK;
   case VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK:
      return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
   default:
      assert(!vk_format_is_srgb(format));
      return format;
   }
}

static inline VkFormat
vk_format_stencil_only(VkFormat format)
{
   return VK_FORMAT_S8_UINT;
}

static inline unsigned
vk_format_get_component_bits(VkFormat format,
                             enum util_format_colorspace colorspace,
                             unsigned component)
{
   return util_format_get_component_bits(vk_format_to_pipe_format(format),
                                         colorspace, component);
}

static inline unsigned
vk_format_get_nr_components(VkFormat format)
{
   return util_format_get_nr_components(vk_format_to_pipe_format(format));
}

#endif /* VK_FORMAT_H */
