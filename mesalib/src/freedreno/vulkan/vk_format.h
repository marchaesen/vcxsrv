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

static inline bool
vk_format_is_compressed(VkFormat format)
{
   /* this includes 4:2:2 formats, which are compressed formats for vulkan */
   return vk_format_get_blockwidth(format) > 1;
}

static inline bool
vk_format_is_depth_or_stencil(VkFormat format)
{
   const struct util_format_description *desc = vk_format_description(format);

   return util_format_has_depth(desc) || util_format_has_stencil(desc);
}

static inline bool
vk_format_has_alpha(VkFormat format)
{
   return util_format_has_alpha(vk_format_to_pipe_format(format));
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

static inline bool
vk_format_is_unorm(VkFormat format)
{
   return util_format_is_unorm(vk_format_to_pipe_format(format));
}

static inline bool
vk_format_is_snorm(VkFormat format)
{
   return util_format_is_snorm(vk_format_to_pipe_format(format));
}

static inline bool
vk_format_is_float(VkFormat format)
{
   return util_format_is_float(vk_format_to_pipe_format(format));
}

static inline unsigned
vk_format_get_component_bits(VkFormat format,
                             enum util_format_colorspace colorspace,
                             unsigned component)
{
   switch (format) {
   case VK_FORMAT_G8B8G8R8_422_UNORM:
   case VK_FORMAT_B8G8R8G8_422_UNORM:
   case VK_FORMAT_G8_B8R8_2PLANE_420_UNORM:
   case VK_FORMAT_G8_B8_R8_3PLANE_420_UNORM:
      /* util_format_get_component_bits doesn't return what we want */
      return 8;
   default:
      break;
   }

   return util_format_get_component_bits(vk_format_to_pipe_format(format),
                                         colorspace, component);
}

static inline unsigned
vk_format_get_nr_components(VkFormat format)
{
   return util_format_get_nr_components(vk_format_to_pipe_format(format));
}

#endif /* VK_FORMAT_H */
