/*
 * Copyright © 2016 Intel Corporation
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

#ifndef VK_FORMAT_INFO_H
#define VK_FORMAT_INFO_H

#include <stdbool.h>
#include <vulkan/vulkan.h>

#include "util/format/u_format.h"
#include "vulkan/util/vk_format.h"

/* FIXME: from freedreno vk_format.h, common place?*/
static inline bool
vk_format_is_int(VkFormat format)
{
   return util_format_is_pure_integer(vk_format_to_pipe_format(format));
}

static inline bool
vk_format_is_sint(VkFormat format)
{
   return util_format_is_pure_sint(vk_format_to_pipe_format(format));
}

static inline bool
vk_format_is_uint(VkFormat format)
{
   return util_format_is_pure_uint(vk_format_to_pipe_format(format));
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

static inline bool
vk_format_is_srgb(VkFormat format)
{
   return util_format_is_srgb(vk_format_to_pipe_format(format));
}

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
   return util_format_is_compressed(vk_format_to_pipe_format(format));
}

static inline const struct util_format_description *
vk_format_description(VkFormat format)
{
   return util_format_description(vk_format_to_pipe_format(format));
}

#endif /* VK_FORMAT_INFO_H */
