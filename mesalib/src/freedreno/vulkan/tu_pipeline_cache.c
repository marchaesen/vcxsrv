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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "tu_private.h"

#include "util/debug.h"
#include "util/disk_cache.h"
#include "util/mesa-sha1.h"
#include "util/u_atomic.h"
#include "vulkan/util/vk_util.h"

struct cache_entry_variant_info
{
};

struct cache_entry
{
   union {
      unsigned char sha1[20];
      uint32_t sha1_dw[5];
   };
   uint32_t code_sizes[MESA_SHADER_STAGES];
   struct tu_shader_variant *variants[MESA_SHADER_STAGES];
   char code[0];
};

static void
tu_pipeline_cache_init(struct tu_pipeline_cache *cache,
                       struct tu_device *device)
{
   cache->device = device;
   pthread_mutex_init(&cache->mutex, NULL);

   cache->modified = false;
   cache->kernel_count = 0;
   cache->total_size = 0;
   cache->table_size = 1024;
   const size_t byte_size = cache->table_size * sizeof(cache->hash_table[0]);
   cache->hash_table = malloc(byte_size);

   /* We don't consider allocation failure fatal, we just start with a 0-sized
    * cache. Disable caching when we want to keep shader debug info, since
    * we don't get the debug info on cached shaders. */
   if (cache->hash_table == NULL)
      cache->table_size = 0;
   else
      memset(cache->hash_table, 0, byte_size);
}

static void
tu_pipeline_cache_finish(struct tu_pipeline_cache *cache)
{
   for (unsigned i = 0; i < cache->table_size; ++i)
      if (cache->hash_table[i]) {
         vk_free(&cache->alloc, cache->hash_table[i]);
      }
   pthread_mutex_destroy(&cache->mutex);
   free(cache->hash_table);
}

static uint32_t
entry_size(struct cache_entry *entry)
{
   size_t ret = sizeof(*entry);
   for (int i = 0; i < MESA_SHADER_STAGES; ++i)
      if (entry->code_sizes[i])
         ret +=
            sizeof(struct cache_entry_variant_info) + entry->code_sizes[i];
   return ret;
}

static struct cache_entry *
tu_pipeline_cache_search_unlocked(struct tu_pipeline_cache *cache,
                                  const unsigned char *sha1)
{
   const uint32_t mask = cache->table_size - 1;
   const uint32_t start = (*(uint32_t *) sha1);

   if (cache->table_size == 0)
      return NULL;

   for (uint32_t i = 0; i < cache->table_size; i++) {
      const uint32_t index = (start + i) & mask;
      struct cache_entry *entry = cache->hash_table[index];

      if (!entry)
         return NULL;

      if (memcmp(entry->sha1, sha1, sizeof(entry->sha1)) == 0) {
         return entry;
      }
   }

   unreachable("hash table should never be full");
}

static struct cache_entry *
tu_pipeline_cache_search(struct tu_pipeline_cache *cache,
                         const unsigned char *sha1)
{
   struct cache_entry *entry;

   pthread_mutex_lock(&cache->mutex);

   entry = tu_pipeline_cache_search_unlocked(cache, sha1);

   pthread_mutex_unlock(&cache->mutex);

   return entry;
}

static void
tu_pipeline_cache_set_entry(struct tu_pipeline_cache *cache,
                            struct cache_entry *entry)
{
   const uint32_t mask = cache->table_size - 1;
   const uint32_t start = entry->sha1_dw[0];

   /* We'll always be able to insert when we get here. */
   assert(cache->kernel_count < cache->table_size / 2);

   for (uint32_t i = 0; i < cache->table_size; i++) {
      const uint32_t index = (start + i) & mask;
      if (!cache->hash_table[index]) {
         cache->hash_table[index] = entry;
         break;
      }
   }

   cache->total_size += entry_size(entry);
   cache->kernel_count++;
}

static VkResult
tu_pipeline_cache_grow(struct tu_pipeline_cache *cache)
{
   const uint32_t table_size = cache->table_size * 2;
   const uint32_t old_table_size = cache->table_size;
   const size_t byte_size = table_size * sizeof(cache->hash_table[0]);
   struct cache_entry **table;
   struct cache_entry **old_table = cache->hash_table;

   table = malloc(byte_size);
   if (table == NULL)
      return vk_error(cache->device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   cache->hash_table = table;
   cache->table_size = table_size;
   cache->kernel_count = 0;
   cache->total_size = 0;

   memset(cache->hash_table, 0, byte_size);
   for (uint32_t i = 0; i < old_table_size; i++) {
      struct cache_entry *entry = old_table[i];
      if (!entry)
         continue;

      tu_pipeline_cache_set_entry(cache, entry);
   }

   free(old_table);

   return VK_SUCCESS;
}

static void
tu_pipeline_cache_add_entry(struct tu_pipeline_cache *cache,
                            struct cache_entry *entry)
{
   if (cache->kernel_count == cache->table_size / 2)
      tu_pipeline_cache_grow(cache);

   /* Failing to grow that hash table isn't fatal, but may mean we don't
    * have enough space to add this new kernel. Only add it if there's room.
    */
   if (cache->kernel_count < cache->table_size / 2)
      tu_pipeline_cache_set_entry(cache, entry);
}

static void
tu_pipeline_cache_load(struct tu_pipeline_cache *cache,
                       const void *data,
                       size_t size)
{
   struct tu_device *device = cache->device;
   struct vk_pipeline_cache_header header;

   if (size < sizeof(header))
      return;
   memcpy(&header, data, sizeof(header));
   if (header.header_size < sizeof(header))
      return;
   if (header.header_version != VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
      return;
   if (header.vendor_id != 0 /* TODO */)
      return;
   if (header.device_id != 0 /* TODO */)
      return;
   if (memcmp(header.uuid, device->physical_device->cache_uuid,
              VK_UUID_SIZE) != 0)
      return;

   char *end = (void *) data + size;
   char *p = (void *) data + header.header_size;

   while (end - p >= sizeof(struct cache_entry)) {
      struct cache_entry *entry = (struct cache_entry *) p;
      struct cache_entry *dest_entry;
      size_t size = entry_size(entry);
      if (end - p < size)
         break;

      dest_entry =
         vk_alloc(&cache->alloc, size, 8, VK_SYSTEM_ALLOCATION_SCOPE_CACHE);
      if (dest_entry) {
         memcpy(dest_entry, entry, size);
         for (int i = 0; i < MESA_SHADER_STAGES; ++i)
            dest_entry->variants[i] = NULL;
         tu_pipeline_cache_add_entry(cache, dest_entry);
      }
      p += size;
   }
}

VkResult
tu_CreatePipelineCache(VkDevice _device,
                       const VkPipelineCacheCreateInfo *pCreateInfo,
                       const VkAllocationCallbacks *pAllocator,
                       VkPipelineCache *pPipelineCache)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   struct tu_pipeline_cache *cache;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
   assert(pCreateInfo->flags == 0);

   cache = vk_object_alloc(&device->vk, pAllocator, sizeof(*cache),
                           VK_OBJECT_TYPE_PIPELINE_CACHE);
   if (cache == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   if (pAllocator)
      cache->alloc = *pAllocator;
   else
      cache->alloc = device->vk.alloc;

   tu_pipeline_cache_init(cache, device);

   if (pCreateInfo->initialDataSize > 0) {
      tu_pipeline_cache_load(cache, pCreateInfo->pInitialData,
                             pCreateInfo->initialDataSize);
   }

   *pPipelineCache = tu_pipeline_cache_to_handle(cache);

   return VK_SUCCESS;
}

void
tu_DestroyPipelineCache(VkDevice _device,
                        VkPipelineCache _cache,
                        const VkAllocationCallbacks *pAllocator)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_pipeline_cache, cache, _cache);

   if (!cache)
      return;
   tu_pipeline_cache_finish(cache);

   vk_object_free(&device->vk, pAllocator, cache);
}

VkResult
tu_GetPipelineCacheData(VkDevice _device,
                        VkPipelineCache _cache,
                        size_t *pDataSize,
                        void *pData)
{
   TU_FROM_HANDLE(tu_device, device, _device);
   TU_FROM_HANDLE(tu_pipeline_cache, cache, _cache);
   struct vk_pipeline_cache_header *header;
   VkResult result = VK_SUCCESS;

   pthread_mutex_lock(&cache->mutex);

   const size_t size = sizeof(*header) + cache->total_size;
   if (pData == NULL) {
      pthread_mutex_unlock(&cache->mutex);
      *pDataSize = size;
      return VK_SUCCESS;
   }
   if (*pDataSize < sizeof(*header)) {
      pthread_mutex_unlock(&cache->mutex);
      *pDataSize = 0;
      return VK_INCOMPLETE;
   }
   void *p = pData, *end = pData + *pDataSize;
   header = p;
   header->header_size = sizeof(*header);
   header->header_version = VK_PIPELINE_CACHE_HEADER_VERSION_ONE;
   header->vendor_id = 0 /* TODO */;
   header->device_id = 0 /* TODO */;
   memcpy(header->uuid, device->physical_device->cache_uuid, VK_UUID_SIZE);
   p += header->header_size;

   struct cache_entry *entry;
   for (uint32_t i = 0; i < cache->table_size; i++) {
      if (!cache->hash_table[i])
         continue;
      entry = cache->hash_table[i];
      const uint32_t size = entry_size(entry);
      if (end < p + size) {
         result = VK_INCOMPLETE;
         break;
      }

      memcpy(p, entry, size);
      for (int j = 0; j < MESA_SHADER_STAGES; ++j)
         ((struct cache_entry *) p)->variants[j] = NULL;
      p += size;
   }
   *pDataSize = p - pData;

   pthread_mutex_unlock(&cache->mutex);
   return result;
}

static void
tu_pipeline_cache_merge(struct tu_pipeline_cache *dst,
                        struct tu_pipeline_cache *src)
{
   for (uint32_t i = 0; i < src->table_size; i++) {
      struct cache_entry *entry = src->hash_table[i];
      if (!entry || tu_pipeline_cache_search(dst, entry->sha1))
         continue;

      tu_pipeline_cache_add_entry(dst, entry);

      src->hash_table[i] = NULL;
   }
}

VkResult
tu_MergePipelineCaches(VkDevice _device,
                       VkPipelineCache destCache,
                       uint32_t srcCacheCount,
                       const VkPipelineCache *pSrcCaches)
{
   TU_FROM_HANDLE(tu_pipeline_cache, dst, destCache);

   for (uint32_t i = 0; i < srcCacheCount; i++) {
      TU_FROM_HANDLE(tu_pipeline_cache, src, pSrcCaches[i]);

      tu_pipeline_cache_merge(dst, src);
   }

   return VK_SUCCESS;
}
