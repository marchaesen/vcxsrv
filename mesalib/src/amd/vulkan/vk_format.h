/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * Based on u_format.h which is:
 * Copyright 2009-2010 VMware, Inc.
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

#ifndef VK_FORMAT_H
#define VK_FORMAT_H

#include <assert.h>
#include <util/macros.h>
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

/**
 * Return the index of the first non-void channel
 * -1 if no non-void channels
 */
static inline int
vk_format_get_first_non_void_channel(VkFormat format)
{
   return util_format_get_first_non_void_channel(vk_format_to_pipe_format(format));
}

static inline enum pipe_swizzle
radv_swizzle_conv(VkComponentSwizzle component, const unsigned char chan[4],
                  VkComponentSwizzle vk_swiz)
{
   if (vk_swiz == VK_COMPONENT_SWIZZLE_IDENTITY)
      vk_swiz = component;
   switch (vk_swiz) {
   case VK_COMPONENT_SWIZZLE_ZERO:
      return PIPE_SWIZZLE_0;
   case VK_COMPONENT_SWIZZLE_ONE:
      return PIPE_SWIZZLE_1;
   case VK_COMPONENT_SWIZZLE_R:
   case VK_COMPONENT_SWIZZLE_G:
   case VK_COMPONENT_SWIZZLE_B:
   case VK_COMPONENT_SWIZZLE_A:
      return (enum pipe_swizzle)chan[vk_swiz - VK_COMPONENT_SWIZZLE_R];
   default:
      unreachable("Illegal swizzle");
   }
}

static inline void
vk_format_compose_swizzles(const VkComponentMapping *mapping, const unsigned char swz[4],
                           enum pipe_swizzle dst[4])
{
   dst[0] = radv_swizzle_conv(VK_COMPONENT_SWIZZLE_R, swz, mapping->r);
   dst[1] = radv_swizzle_conv(VK_COMPONENT_SWIZZLE_G, swz, mapping->g);
   dst[2] = radv_swizzle_conv(VK_COMPONENT_SWIZZLE_B, swz, mapping->b);
   dst[3] = radv_swizzle_conv(VK_COMPONENT_SWIZZLE_A, swz, mapping->a);
}

static inline bool
vk_format_is_compressed(VkFormat format)
{
   return util_format_is_compressed(vk_format_to_pipe_format(format));
}

static inline bool
vk_format_is_subsampled(VkFormat format)
{
   return util_format_is_subsampled_422(vk_format_to_pipe_format(format));
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
vk_format_is_unorm(VkFormat format)
{
   return util_format_is_unorm(vk_format_to_pipe_format(format));
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

static inline unsigned
vk_format_get_component_bits(VkFormat format, enum util_format_colorspace colorspace,
                             unsigned component)
{
   const struct util_format_description *desc = vk_format_description(format);
   enum util_format_colorspace desc_colorspace;

   assert(format);
   if (!format) {
      return 0;
   }

   assert(component < 4);

   /* Treat RGB and SRGB as equivalent. */
   if (colorspace == UTIL_FORMAT_COLORSPACE_SRGB) {
      colorspace = UTIL_FORMAT_COLORSPACE_RGB;
   }
   if (desc->colorspace == UTIL_FORMAT_COLORSPACE_SRGB) {
      desc_colorspace = UTIL_FORMAT_COLORSPACE_RGB;
   } else {
      desc_colorspace = desc->colorspace;
   }

   if (desc_colorspace != colorspace) {
      return 0;
   }

   switch (desc->swizzle[component]) {
   case PIPE_SWIZZLE_X:
      return desc->channel[0].size;
   case PIPE_SWIZZLE_Y:
      return desc->channel[1].size;
   case PIPE_SWIZZLE_Z:
      return desc->channel[2].size;
   case PIPE_SWIZZLE_W:
      return desc->channel[3].size;
   default:
      return 0;
   }
}

static inline VkFormat
vk_to_non_srgb_format(VkFormat format)
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
   default:
      return format;
   }
}

static inline unsigned
vk_format_get_nr_components(VkFormat format)
{
   return util_format_get_nr_components(vk_format_to_pipe_format(format));
}

static inline unsigned
vk_format_get_plane_count(VkFormat format)
{
   return util_format_get_num_planes(vk_format_to_pipe_format(format));
}

static inline unsigned
vk_format_get_plane_width(VkFormat format, unsigned plane, unsigned width)
{
   return util_format_get_plane_width(vk_format_to_pipe_format(format), plane, width);
}

static inline unsigned
vk_format_get_plane_height(VkFormat format, unsigned plane, unsigned height)
{
   return util_format_get_plane_height(vk_format_to_pipe_format(format), plane, height);
}

static inline VkFormat
vk_format_get_plane_format(VkFormat format, unsigned plane_id)
{
   assert(plane_id < vk_format_get_plane_count(format));

   switch (format) {
   case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
   case VK_FORMAT_G8_B8_R8_3PLANE_422_UNORM:
   case VK_FORMAT_G8_B8_R8_3PLANE_444_UNORM:
      return VK_FORMAT_R8_UNORM;
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
   case VK_FORMAT_G8_B8R8_2PLANE_422_UNORM:
      return plane_id ? VK_FORMAT_R8G8_UNORM : VK_FORMAT_R8_UNORM;
   case VK_FORMAT_G16_B16_R16_3PLANE_420_UNORM:
   case VK_FORMAT_G16_B16_R16_3PLANE_422_UNORM:
   case VK_FORMAT_G16_B16_R16_3PLANE_444_UNORM:
      return VK_FORMAT_R16_UNORM;
   case VK_FORMAT_G16_B16R16_2PLANE_420_UNORM:
   case VK_FORMAT_G16_B16R16_2PLANE_422_UNORM:
      return plane_id ? VK_FORMAT_R16G16_UNORM : VK_FORMAT_R16_UNORM;
   default:
      assert(vk_format_get_plane_count(format) == 1);
      return format;
   }
}

#endif /* VK_FORMAT_H */
