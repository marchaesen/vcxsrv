/*
 * Copyright © 2022 Collabora, LTD
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

#include "vk_sampler.h"
#include "vk_util.h"

VkClearColorValue
vk_border_color_value(VkBorderColor color)
{
   switch (color) {
   case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
      return (VkClearColorValue) { .float32 = { 0, 0, 0, 0 } };
   case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
      return (VkClearColorValue) { .int32 = { 0, 0, 0, 0 } };
   case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
      return (VkClearColorValue) { .float32 = { 0, 0, 0, 1 } };
   case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
      return (VkClearColorValue) { .int32 = { 0, 0, 0, 1 } };
   case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
      return (VkClearColorValue) { .float32 = { 1, 1, 1, 1 } };
   case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
      return (VkClearColorValue) { .int32 = { 1, 1, 1, 1 } };
   default:
      unreachable("Invalid or custom border color enum");
   }
}

bool
vk_border_color_is_int(VkBorderColor color)
{
   switch (color) {
   case VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK:
   case VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK:
   case VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE:
   case VK_BORDER_COLOR_FLOAT_CUSTOM_EXT:
      return false;
   case VK_BORDER_COLOR_INT_TRANSPARENT_BLACK:
   case VK_BORDER_COLOR_INT_OPAQUE_BLACK:
   case VK_BORDER_COLOR_INT_OPAQUE_WHITE:
   case VK_BORDER_COLOR_INT_CUSTOM_EXT:
      return true;
   default:
      unreachable("Invalid border color enum");
   }
}

VkClearColorValue
vk_sampler_border_color_value(const VkSamplerCreateInfo *pCreateInfo,
                              VkFormat *format_out)
{
   if (pCreateInfo->borderColor == VK_BORDER_COLOR_FLOAT_CUSTOM_EXT ||
       pCreateInfo->borderColor == VK_BORDER_COLOR_INT_CUSTOM_EXT) {
      const VkSamplerCustomBorderColorCreateInfoEXT *border_color_info =
         vk_find_struct_const(pCreateInfo->pNext,
                              SAMPLER_CUSTOM_BORDER_COLOR_CREATE_INFO_EXT);
      if (format_out)
         *format_out = border_color_info->format;

      return border_color_info->customBorderColor;
   } else {
      if (format_out)
         *format_out = VK_FORMAT_UNDEFINED;

      return vk_border_color_value(pCreateInfo->borderColor);
   }
}

