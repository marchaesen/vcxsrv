/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <string.h>

#include "pvr_device_info.h"
#include "pvr_private.h"
#include "util/blob.h"
#include "vk_log.h"
#include "vk_object.h"
#include "vulkan/util/vk_util.h"

static void pvr_pipeline_cache_load(struct pvr_pipeline_cache *cache,
                                    const void *data,
                                    size_t size)
{
   struct pvr_device *device = cache->device;
   struct pvr_physical_device *pdevice = device->pdevice;
   struct vk_pipeline_cache_header header;
   struct blob_reader blob;

   blob_reader_init(&blob, data, size);

   blob_copy_bytes(&blob, &header, sizeof(header));
   if (blob.overrun)
      return;

   if (header.header_size < sizeof(header))
      return;
   if (header.header_version != VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
      return;
   if (header.vendor_id != VK_VENDOR_ID_IMAGINATION)
      return;
   if (header.device_id != pdevice->dev_info.ident.device_id)
      return;
   if (memcmp(header.uuid, pdevice->pipeline_cache_uuid, VK_UUID_SIZE) != 0)
      return;

   /* TODO: There isn't currently any cached data so there's nothing to load
    * at this point. Once there is something to load then load it now.
    */
}

VkResult pvr_CreatePipelineCache(VkDevice _device,
                                 const VkPipelineCacheCreateInfo *pCreateInfo,
                                 const VkAllocationCallbacks *pAllocator,
                                 VkPipelineCache *pPipelineCache)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   struct pvr_pipeline_cache *cache;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
   assert(pCreateInfo->flags == 0);

   cache = vk_object_alloc(&device->vk,
                           pAllocator,
                           sizeof(*cache),
                           VK_OBJECT_TYPE_PIPELINE_CACHE);
   if (!cache)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   cache->device = device;

   if (pCreateInfo->initialDataSize > 0) {
      pvr_pipeline_cache_load(cache,
                              pCreateInfo->pInitialData,
                              pCreateInfo->initialDataSize);
   }

   *pPipelineCache = pvr_pipeline_cache_to_handle(cache);

   return VK_SUCCESS;
}

void pvr_DestroyPipelineCache(VkDevice _device,
                              VkPipelineCache _cache,
                              const VkAllocationCallbacks *pAllocator)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   PVR_FROM_HANDLE(pvr_pipeline_cache, cache, _cache);

   if (!cache)
      return;

   vk_object_free(&device->vk, pAllocator, cache);
}

VkResult pvr_GetPipelineCacheData(VkDevice _device,
                                  VkPipelineCache _cache,
                                  size_t *pDataSize,
                                  void *pData)
{
   PVR_FROM_HANDLE(pvr_device, device, _device);
   struct pvr_physical_device *pdevice = device->pdevice;
   struct blob blob;

   if (pData)
      blob_init_fixed(&blob, pData, *pDataSize);
   else
      blob_init_fixed(&blob, NULL, SIZE_MAX);

   struct vk_pipeline_cache_header header = {
      .header_size = sizeof(struct vk_pipeline_cache_header),
      .header_version = VK_PIPELINE_CACHE_HEADER_VERSION_ONE,
      .vendor_id = VK_VENDOR_ID_IMAGINATION,
      .device_id = pdevice->dev_info.ident.device_id,
   };
   memcpy(header.uuid, pdevice->pipeline_cache_uuid, VK_UUID_SIZE);
   blob_write_bytes(&blob, &header, sizeof(header));

   /* TODO: Once there's some data to cache then this should be written to
    * 'blob'.
    */

   *pDataSize = blob.size;

   blob_finish(&blob);

   return VK_SUCCESS;
}

VkResult pvr_MergePipelineCaches(VkDevice _device,
                                 VkPipelineCache destCache,
                                 uint32_t srcCacheCount,
                                 const VkPipelineCache *pSrcCaches)
{
   /* TODO: Once there's some data to cache then this will need to be able to
    * merge caches together.
    */

   return VK_SUCCESS;
}
