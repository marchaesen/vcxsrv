/*
 * Copyright © 2015 Intel Corporation
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
#ifndef VK_ALLOC_H
#define VK_ALLOC_H

/* common allocation inlines for vulkan drivers */

#include <string.h>
#include <vulkan/vulkan.h>

static inline void *
vk_alloc(const VkAllocationCallbacks *alloc,
         size_t size, size_t align,
         VkSystemAllocationScope scope)
{
   return alloc->pfnAllocation(alloc->pUserData, size, align, scope);
}

static inline void *
vk_realloc(const VkAllocationCallbacks *alloc,
           void *ptr, size_t size, size_t align,
           VkSystemAllocationScope scope)
{
   return alloc->pfnReallocation(alloc->pUserData, ptr, size, align, scope);
}

static inline void
vk_free(const VkAllocationCallbacks *alloc, void *data)
{
   if (data == NULL)
      return;

   alloc->pfnFree(alloc->pUserData, data);
}

static inline void *
vk_alloc2(const VkAllocationCallbacks *parent_alloc,
          const VkAllocationCallbacks *alloc,
          size_t size, size_t align,
          VkSystemAllocationScope scope)
{
   if (alloc)
      return vk_alloc(alloc, size, align, scope);
   else
      return vk_alloc(parent_alloc, size, align, scope);
}

static inline void *
vk_zalloc2(const VkAllocationCallbacks *parent_alloc,
           const VkAllocationCallbacks *alloc,
           size_t size, size_t align,
           VkSystemAllocationScope scope)
{
   void *mem = vk_alloc2(parent_alloc, alloc, size, align, scope);
   if (mem == NULL)
      return NULL;

   memset(mem, 0, size);

   return mem;
}

static inline void
vk_free2(const VkAllocationCallbacks *parent_alloc,
         const VkAllocationCallbacks *alloc,
         void *data)
{
   if (alloc)
      vk_free(alloc, data);
   else
      vk_free(parent_alloc, data);
}

#endif
