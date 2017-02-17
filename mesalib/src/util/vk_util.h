/*
 * Copyright © 2017 Intel Corporation
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
#ifndef VK_UTIL_H
#define VK_UTIL_H

/* common inlines and macros for vulkan drivers */

#include <vulkan/vulkan.h>

struct vk_struct_common {
    VkStructureType sType;
    struct vk_struct_common *pNext;
};

#define vk_foreach_struct(__iter, __start) \
   for (struct vk_struct_common *__iter = (struct vk_struct_common *)(__start); \
        __iter; __iter = __iter->pNext)

#define vk_foreach_struct_const(__iter, __start) \
   for (const struct vk_struct_common *__iter = (const struct vk_struct_common *)(__start); \
        __iter; __iter = __iter->pNext)

#endif /* VK_UTIL_H */
