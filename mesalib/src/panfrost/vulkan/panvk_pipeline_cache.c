/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_pipeline_cache.c which is:
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "panvk_private.h"

#include "util/debug.h"
#include "util/disk_cache.h"
#include "util/mesa-sha1.h"
#include "util/u_atomic.h"

VkResult
panvk_CreatePipelineCache(VkDevice _device,
                          const VkPipelineCacheCreateInfo *pCreateInfo,
                          const VkAllocationCallbacks *pAllocator,
                          VkPipelineCache *pPipelineCache)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   struct panvk_pipeline_cache *cache;

   cache = vk_object_alloc(&device->vk, pAllocator, sizeof(*cache),
                           VK_OBJECT_TYPE_PIPELINE_CACHE);
   if (cache == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (pAllocator)
      cache->alloc = *pAllocator;
   else
      cache->alloc = device->vk.alloc;

   *pPipelineCache = panvk_pipeline_cache_to_handle(cache);
   return VK_SUCCESS;
}

void
panvk_DestroyPipelineCache(VkDevice _device,
                           VkPipelineCache _cache,
                           const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_pipeline_cache, cache, _cache);

   vk_object_free(&device->vk, pAllocator, cache);
}

VkResult
panvk_GetPipelineCacheData(VkDevice _device,
                           VkPipelineCache _cache,
                           size_t *pDataSize,
                           void *pData)
{
   panvk_stub();
   return VK_SUCCESS;
}

VkResult
panvk_MergePipelineCaches(VkDevice _device,
                          VkPipelineCache destCache,
                          uint32_t srcCacheCount,
                          const VkPipelineCache *pSrcCaches)
{
   panvk_stub();
   return VK_SUCCESS;
}
