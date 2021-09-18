/*
 * Copyright © 2016 Intel Corporation
 * Copyright © 2019 Google LLC
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

#ifndef U_FORMAT_VK_H
#define U_FORMAT_VK_H

#include <vulkan/vulkan_core.h>
#include "util/format/u_format.h"

#ifdef __cplusplus
extern "C" {
#endif

enum pipe_format
vk_format_to_pipe_format(enum VkFormat vkformat);

VkImageAspectFlags
vk_format_aspects(VkFormat format);

static inline bool
vk_format_is_color(VkFormat format)
{
   return vk_format_aspects(format) == VK_IMAGE_ASPECT_COLOR_BIT;
}

static inline bool
vk_format_is_depth_or_stencil(VkFormat format)
{
   const VkImageAspectFlags aspects = vk_format_aspects(format);
   return aspects & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT);
}

static inline bool
vk_format_has_depth(VkFormat format)
{
   const VkImageAspectFlags aspects = vk_format_aspects(format);
   return aspects & VK_IMAGE_ASPECT_DEPTH_BIT;
}

static inline bool
vk_format_has_stencil(VkFormat format)
{
   const VkImageAspectFlags aspects = vk_format_aspects(format);
   return aspects & VK_IMAGE_ASPECT_STENCIL_BIT;
}

static inline VkFormat
vk_format_depth_only(VkFormat format)
{
   assert(vk_format_has_depth(format));
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

static inline VkFormat
vk_format_stencil_only(VkFormat format)
{
   assert(vk_format_has_stencil(format));
   return VK_FORMAT_S8_UINT;
}

#ifdef __cplusplus
}
#endif

#endif
