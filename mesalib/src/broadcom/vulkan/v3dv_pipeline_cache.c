/*
 * Copyright © 2019 Raspberry Pi
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

#include "v3dv_private.h"
#include "vulkan/util/vk_util.h"
#include "util/blob.h"
#include "nir/nir_serialize.h"

static const bool dump_stats = false;
static const bool dump_stats_verbose = false;

/* Shared for nir/variants */
#define V3DV_MAX_PIPELINE_CACHE_ENTRIES 4096

static uint32_t
sha1_hash_func(const void *sha1)
{
   return _mesa_hash_data(sha1, 20);
}

static bool
sha1_compare_func(const void *sha1_a, const void *sha1_b)
{
   return memcmp(sha1_a, sha1_b, 20) == 0;
}

struct serialized_nir {
   unsigned char sha1_key[20];
   size_t size;
   char data[0];
};

static void
cache_dump_stats(struct v3dv_pipeline_cache *cache)
{
   if (!dump_stats_verbose)
      return;

   fprintf(stderr, "  NIR cache entries:      %d\n", cache->nir_stats.count);
   fprintf(stderr, "  NIR cache miss count:   %d\n", cache->nir_stats.miss);
   fprintf(stderr, "  NIR cache hit  count:   %d\n", cache->nir_stats.hit);

   fprintf(stderr, "  variant cache entries:      %d\n", cache->variant_stats.count);
   fprintf(stderr, "  variant cache miss count:   %d\n", cache->variant_stats.miss);
   fprintf(stderr, "  variant cache hit  count:   %d\n", cache->variant_stats.hit);
}

void
v3dv_pipeline_cache_upload_nir(struct v3dv_pipeline *pipeline,
                               struct v3dv_pipeline_cache *cache,
                               nir_shader *nir,
                               unsigned char sha1_key[20])
{
   if (!cache || !cache->nir_cache)
      return;

   if (cache->nir_stats.count > V3DV_MAX_PIPELINE_CACHE_ENTRIES)
      return;

   pthread_mutex_lock(&cache->mutex);
   struct hash_entry *entry =
      _mesa_hash_table_search(cache->nir_cache, sha1_key);
   pthread_mutex_unlock(&cache->mutex);
   if (entry)
      return;

   struct blob blob;
   blob_init(&blob);

   nir_serialize(&blob, nir, false);
   if (blob.out_of_memory) {
      blob_finish(&blob);
      return;
   }

   pthread_mutex_lock(&cache->mutex);
   /* Because ralloc isn't thread-safe, we have to do all this inside the
    * lock.  We could unlock for the big memcpy but it's probably not worth
    * the hassle.
    */
   entry = _mesa_hash_table_search(cache->nir_cache, sha1_key);
   if (entry) {
      blob_finish(&blob);
      pthread_mutex_unlock(&cache->mutex);
      return;
   }

   struct serialized_nir *snir =
      ralloc_size(cache->nir_cache, sizeof(*snir) + blob.size);
   memcpy(snir->sha1_key, sha1_key, 20);
   snir->size = blob.size;
   memcpy(snir->data, blob.data, blob.size);

   blob_finish(&blob);

   cache->nir_stats.count++;
   if (unlikely(dump_stats)) {
      char sha1buf[41];
      _mesa_sha1_format(sha1buf, snir->sha1_key);
      fprintf(stderr, "pipeline cache %p, new nir entry %s\n", cache, sha1buf);
      cache_dump_stats(cache);
   }

   _mesa_hash_table_insert(cache->nir_cache, snir->sha1_key, snir);

   pthread_mutex_unlock(&cache->mutex);
}

nir_shader*
v3dv_pipeline_cache_search_for_nir(struct v3dv_pipeline *pipeline,
                                   struct v3dv_pipeline_cache *cache,
                                   const nir_shader_compiler_options *nir_options,
                                   unsigned char sha1_key[20])
{
   if (!cache || !cache->nir_cache)
      return NULL;

   if (unlikely(dump_stats)) {
      char sha1buf[41];
      _mesa_sha1_format(sha1buf, sha1_key);

      fprintf(stderr, "pipeline cache %p, search for nir %s\n", cache, sha1buf);
   }

   const struct serialized_nir *snir = NULL;

   pthread_mutex_lock(&cache->mutex);
   struct hash_entry *entry =
      _mesa_hash_table_search(cache->nir_cache, sha1_key);
   if (entry)
      snir = entry->data;
   pthread_mutex_unlock(&cache->mutex);

   if (snir) {
      struct blob_reader blob;
      blob_reader_init(&blob, snir->data, snir->size);

      /* We use context NULL as we want the p_stage to keep the reference to
       * nir, as we keep open the possibility of provide a shader variant
       * after cache creation
       */
      nir_shader *nir = nir_deserialize(NULL, nir_options, &blob);
      if (blob.overrun) {
         ralloc_free(nir);
      } else {
         cache->nir_stats.hit++;
         cache_dump_stats(cache);
         return nir;
      }
   }

   cache->nir_stats.miss++;
   cache_dump_stats(cache);

   return NULL;
}

void
v3dv_pipeline_cache_init(struct v3dv_pipeline_cache *cache,
                         struct v3dv_device *device,
                         bool cache_enabled)
{
   cache->_loader_data.loaderMagic = ICD_LOADER_MAGIC;

   cache->device = device;
   pthread_mutex_init(&cache->mutex, NULL);

   if (cache_enabled) {
      cache->nir_cache = _mesa_hash_table_create(NULL, sha1_hash_func,
                                                 sha1_compare_func);
      cache->nir_stats.miss = 0;
      cache->nir_stats.hit = 0;
      cache->nir_stats.count = 0;

      cache->variant_cache = _mesa_hash_table_create(NULL, sha1_hash_func,
                                                     sha1_compare_func);
      cache->variant_stats.miss = 0;
      cache->variant_stats.hit = 0;
      cache->variant_stats.count = 0;
   } else {
      cache->nir_cache = NULL;
      cache->variant_cache = NULL;
   }

}

struct v3dv_shader_variant*
v3dv_pipeline_cache_search_for_variant(struct v3dv_pipeline *pipeline,
                                       struct v3dv_pipeline_cache *cache,
                                       unsigned char sha1_key[20])
{
   if (!cache || !cache->variant_cache)
      return NULL;

   if (unlikely(dump_stats)) {
      char sha1buf[41];
      _mesa_sha1_format(sha1buf, sha1_key);

      fprintf(stderr, "pipeline cache %p, search variant with key %s\n", cache, sha1buf);
   }

   pthread_mutex_lock(&cache->mutex);

   struct hash_entry *entry =
      _mesa_hash_table_search(cache->variant_cache, sha1_key);

   if (entry) {
      struct v3dv_shader_variant *variant =
         (struct v3dv_shader_variant *) entry->data;

      cache->variant_stats.hit++;
      if (unlikely(dump_stats)) {
         fprintf(stderr, "\tcache hit: %p\n", variant);
         cache_dump_stats(cache);
      }

      if (variant)
         v3dv_shader_variant_ref(variant);

      pthread_mutex_unlock(&cache->mutex);
      return variant;
   }

   cache->variant_stats.miss++;
   if (unlikely(dump_stats)) {
      fprintf(stderr, "\tcache miss\n");
      cache_dump_stats(cache);
   }

   pthread_mutex_unlock(&cache->mutex);
   return NULL;
}

void
v3dv_pipeline_cache_upload_variant(struct v3dv_pipeline *pipeline,
                                   struct v3dv_pipeline_cache *cache,
                                   struct v3dv_shader_variant  *variant)
{
   if (!cache || !cache->variant_cache)
      return;

   if (cache->variant_stats.count > V3DV_MAX_PIPELINE_CACHE_ENTRIES)
      return;

   pthread_mutex_lock(&cache->mutex);
   struct hash_entry *entry =
      _mesa_hash_table_search(cache->variant_cache, variant->variant_sha1);

   if (entry) {
      pthread_mutex_unlock(&cache->mutex);
      return;
   }

   v3dv_shader_variant_ref(variant);
   _mesa_hash_table_insert(cache->variant_cache, variant->variant_sha1, variant);
   cache->variant_stats.count++;
   if (unlikely(dump_stats)) {
      char sha1buf[41];
      _mesa_sha1_format(sha1buf, variant->variant_sha1);

      fprintf(stderr, "pipeline cache %p, new variant entry with key %s\n\t%p\n",
              cache, sha1buf, variant);
      cache_dump_stats(cache);
   }

   pthread_mutex_unlock(&cache->mutex);
}

static struct serialized_nir*
serialized_nir_create_from_blob(struct v3dv_pipeline_cache *cache,
                                struct blob_reader *blob)
{
   const unsigned char *sha1_key = blob_read_bytes(blob, 20);
   uint32_t snir_size = blob_read_uint32(blob);
   const char* snir_data = blob_read_bytes(blob, snir_size);
   if (blob->overrun)
      return NULL;

   struct serialized_nir *snir =
      ralloc_size(cache->nir_cache, sizeof(*snir) + snir_size);
   memcpy(snir->sha1_key, sha1_key, 20);
   snir->size = snir_size;
   memcpy(snir->data, snir_data, snir_size);

   return snir;
}

static struct v3dv_shader_variant*
shader_variant_create_from_blob(struct v3dv_device *device,
                                struct blob_reader *blob)
{
   VkResult result;

   gl_shader_stage stage = blob_read_uint32(blob);
   bool is_coord = blob_read_uint8(blob);

   uint32_t v3d_key_size = blob_read_uint32(blob);
   const struct v3d_key *v3d_key = blob_read_bytes(blob, v3d_key_size);

   const unsigned char *variant_sha1 = blob_read_bytes(blob, 20);

   uint32_t prog_data_size = blob_read_uint32(blob);
   /* FIXME: as we include the stage perhaps we can avoid prog_data_size? */
   assert(prog_data_size == v3d_prog_data_size(stage));

   const void *prog_data = blob_read_bytes(blob, prog_data_size);
   if (blob->overrun)
      return NULL;

   uint32_t ulist_count = blob_read_uint32(blob);
   uint32_t contents_size = sizeof(enum quniform_contents) * ulist_count;
   const void *contents_data = blob_read_bytes(blob, contents_size);
   if (blob->overrun)
      return NULL;

   uint ulist_data_size = sizeof(uint32_t) * ulist_count;
   const void *ulist_data_data = blob_read_bytes(blob, ulist_data_size);
   if (blob->overrun)
      return NULL;

   uint32_t qpu_insts_size = blob_read_uint32(blob);
   const uint64_t *qpu_insts = blob_read_bytes(blob, qpu_insts_size);
   if (blob->overrun)
      return NULL;

   /* shader_variant_create expects a newly created prog_data for their own,
    * as it is what the v3d compiler returns. So we are also allocating one
    * (including the uniform list) and filled it up with the data that we read
    * from the blob
    */
   struct v3d_prog_data *new_prog_data = rzalloc_size(NULL, prog_data_size);
   memcpy(new_prog_data, prog_data, prog_data_size);
   struct v3d_uniform_list *ulist = &new_prog_data->uniforms;
   ulist->count = ulist_count;
   ulist->contents = ralloc_array(new_prog_data, enum quniform_contents, ulist->count);
   memcpy(ulist->contents, contents_data, contents_size);
   ulist->data = ralloc_array(new_prog_data, uint32_t, ulist->count);
   memcpy(ulist->data, ulist_data_data, ulist_data_size);

   return v3dv_shader_variant_create(device, stage, is_coord,
                                     variant_sha1,
                                     v3d_key, v3d_key_size,
                                     new_prog_data, prog_data_size,
                                     qpu_insts, qpu_insts_size,
                                     &result);
}

static void
pipeline_cache_load(struct v3dv_pipeline_cache *cache,
                    size_t size,
                    const void *data)
{
   struct v3dv_device *device = cache->device;
   struct v3dv_physical_device *pdevice = &device->instance->physicalDevice;
   struct vk_pipeline_cache_header header;

   if (cache->variant_cache == NULL)
      return;

   struct blob_reader blob;
   blob_reader_init(&blob, data, size);

   blob_copy_bytes(&blob, &header, sizeof(header));
   if (size < sizeof(header))
      return;
   memcpy(&header, data, sizeof(header));
   if (header.header_size < sizeof(header))
      return;
   if (header.header_version != VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
      return;
   if (header.vendor_id != v3dv_physical_device_vendor_id(pdevice))
      return;
   if (header.device_id != v3dv_physical_device_device_id(pdevice))
      return;
   if (memcmp(header.uuid, pdevice->pipeline_cache_uuid, VK_UUID_SIZE) != 0)
      return;

   uint32_t nir_count = blob_read_uint32(&blob);
   if (blob.overrun)
      return;

   for (uint32_t i = 0; i < nir_count; i++) {
      struct serialized_nir *snir =
         serialized_nir_create_from_blob(cache, &blob);

      if (!snir)
         break;

      _mesa_hash_table_insert(cache->nir_cache, snir->sha1_key, snir);
      cache->nir_stats.count++;
   }

   uint32_t count = blob_read_uint32(&blob);
   if (blob.overrun)
      return;

   for (uint32_t i = 0; i < count; i++) {
      struct v3dv_shader_variant *variant =
         shader_variant_create_from_blob(device, &blob);
      if (!variant)
         break;
      _mesa_hash_table_insert(cache->variant_cache, variant->variant_sha1, variant);
      cache->variant_stats.count++;
   }

   if (unlikely(dump_stats)) {
      fprintf(stderr, "pipeline cache %p, loaded %i nir shaders and "
              "%i variant entries\n", cache, nir_count, count);
      cache_dump_stats(cache);
   }
}

VkResult
v3dv_CreatePipelineCache(VkDevice _device,
                         const VkPipelineCacheCreateInfo *pCreateInfo,
                         const VkAllocationCallbacks *pAllocator,
                         VkPipelineCache *pPipelineCache)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   struct v3dv_pipeline_cache *cache;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
   assert(pCreateInfo->flags == 0);

   cache = vk_alloc2(&device->alloc, pAllocator,
                     sizeof(*cache), 8,
                     VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

   if (cache == NULL)
      return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

   v3dv_pipeline_cache_init(cache, device,
                            device->instance->pipeline_cache_enabled);

   if (pCreateInfo->initialDataSize > 0) {
      pipeline_cache_load(cache,
                          pCreateInfo->initialDataSize,
                          pCreateInfo->pInitialData);
   }

   *pPipelineCache = v3dv_pipeline_cache_to_handle(cache);

   return VK_SUCCESS;
}

void
v3dv_pipeline_cache_finish(struct v3dv_pipeline_cache *cache)
{
   pthread_mutex_destroy(&cache->mutex);

   if (cache->nir_cache) {
      hash_table_foreach(cache->nir_cache, entry)
         ralloc_free(entry->data);

      _mesa_hash_table_destroy(cache->nir_cache, NULL);
   }

   if (cache->variant_cache) {
      hash_table_foreach(cache->variant_cache, entry) {
         struct v3dv_shader_variant *variant = entry->data;
         if (variant)
            v3dv_shader_variant_unref(cache->device, variant);
      }

      _mesa_hash_table_destroy(cache->variant_cache, NULL);

   }
}

void
v3dv_DestroyPipelineCache(VkDevice _device,
                          VkPipelineCache _cache,
                          const VkAllocationCallbacks *pAllocator)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_pipeline_cache, cache, _cache);

   if (!cache)
      return;

   v3dv_pipeline_cache_finish(cache);

   vk_free2(&device->alloc, pAllocator, cache);
}

VkResult
v3dv_MergePipelineCaches(VkDevice device,
                         VkPipelineCache dstCache,
                         uint32_t srcCacheCount,
                         const VkPipelineCache *pSrcCaches)
{
   V3DV_FROM_HANDLE(v3dv_pipeline_cache, dst, dstCache);

   if (!dst->variant_cache || !dst->nir_cache)
      return VK_SUCCESS;

   for (uint32_t i = 0; i < srcCacheCount; i++) {
      V3DV_FROM_HANDLE(v3dv_pipeline_cache, src, pSrcCaches[i]);
      if (!src->variant_cache || !src->nir_cache)
         continue;

      hash_table_foreach(src->nir_cache, entry) {
         struct serialized_nir *src_snir = entry->data;
         assert(src_snir);

         if (_mesa_hash_table_search(dst->nir_cache, src_snir->sha1_key))
            continue;

         /* FIXME: we are using serialized nir shaders because they are
          * convenient to create and store on the cache, but requires to do a
          * copy here (and some other places) of the serialized NIR. Perhaps
          * it would make sense to move to handle the NIR shaders with shared
          * structures with ref counts, as the variants.
          */
         struct serialized_nir *snir_dst =
            ralloc_size(dst->nir_cache, sizeof(*snir_dst) + src_snir->size);
         memcpy(snir_dst->sha1_key, src_snir->sha1_key, 20);
         snir_dst->size = src_snir->size;
         memcpy(snir_dst->data, src_snir->data, src_snir->size);

         _mesa_hash_table_insert(dst->nir_cache, snir_dst->sha1_key, snir_dst);
         dst->nir_stats.count++;
         if (unlikely(dump_stats)) {
            char sha1buf[41];
            _mesa_sha1_format(sha1buf, snir_dst->sha1_key);

            fprintf(stderr, "pipeline cache %p, added nir entry %s "
                    "from pipeline cache %p\n",
                    dst, sha1buf, src);
            cache_dump_stats(dst);
         }
      }

      hash_table_foreach(src->variant_cache, entry) {
         struct v3dv_shader_variant *variant = entry->data;
         assert(variant);

         if (_mesa_hash_table_search(dst->variant_cache, variant->variant_sha1))
            continue;

         v3dv_shader_variant_ref(variant);
         _mesa_hash_table_insert(dst->variant_cache, variant->variant_sha1, variant);

         dst->variant_stats.count++;
         if (unlikely(dump_stats)) {
            char sha1buf[41];
            _mesa_sha1_format(sha1buf, variant->variant_sha1);

            fprintf(stderr, "pipeline cache %p, added variant entry %s "
                    "from pipeline cache %p\n",
                    dst, sha1buf, src);
            cache_dump_stats(dst);
         }
      }
   }

   return VK_SUCCESS;
}

static bool
shader_variant_write_to_blob(const struct v3dv_shader_variant *variant,
                             struct blob *blob)
{
   blob_write_uint32(blob, variant->stage);
   blob_write_uint8(blob, variant->is_coord);

   blob_write_uint32(blob, variant->v3d_key_size);
   blob_write_bytes(blob, &variant->key, variant->v3d_key_size);

   blob_write_bytes(blob, variant->variant_sha1, sizeof(variant->variant_sha1));

   blob_write_uint32(blob, variant->prog_data_size);
   blob_write_bytes(blob, variant->prog_data.base, variant->prog_data_size);

   struct v3d_uniform_list *ulist = &variant->prog_data.base->uniforms;
   blob_write_uint32(blob, ulist->count);
   blob_write_bytes(blob, ulist->contents, sizeof(enum quniform_contents) * ulist->count);
   blob_write_bytes(blob, ulist->data, sizeof(uint32_t) * ulist->count);

   blob_write_uint32(blob, variant->qpu_insts_size);
   assert(variant->assembly_bo->map);
   blob_write_bytes(blob, variant->assembly_bo->map, variant->qpu_insts_size);

   return !blob->out_of_memory;
}

VkResult
v3dv_GetPipelineCacheData(VkDevice _device,
                          VkPipelineCache _cache,
                          size_t *pDataSize,
                          void *pData)
{
   V3DV_FROM_HANDLE(v3dv_device, device, _device);
   V3DV_FROM_HANDLE(v3dv_pipeline_cache, cache, _cache);

   struct blob blob;
   if (pData) {
      blob_init_fixed(&blob, pData, *pDataSize);
   } else {
      blob_init_fixed(&blob, NULL, SIZE_MAX);
   }

   struct v3dv_physical_device *pdevice = &device->instance->physicalDevice;
   VkResult result = VK_SUCCESS;

   pthread_mutex_lock(&cache->mutex);

   struct vk_pipeline_cache_header header = {
      .header_size = sizeof(struct vk_pipeline_cache_header),
      .header_version = VK_PIPELINE_CACHE_HEADER_VERSION_ONE,
      .vendor_id = v3dv_physical_device_vendor_id(pdevice),
      .device_id = v3dv_physical_device_device_id(pdevice),
   };
   memcpy(header.uuid, pdevice->pipeline_cache_uuid, VK_UUID_SIZE);
   blob_write_bytes(&blob, &header, sizeof(header));

   uint32_t nir_count = 0;
   intptr_t nir_count_offset = blob_reserve_uint32(&blob);
   if (nir_count_offset < 0) {
      *pDataSize = 0;
      blob_finish(&blob);
      pthread_mutex_unlock(&cache->mutex);
      return VK_INCOMPLETE;
   }

   if (cache->nir_cache) {
      hash_table_foreach(cache->nir_cache, entry) {
         const struct serialized_nir *snir = entry->data;

         size_t save_size = blob.size;

         blob_write_bytes(&blob, snir->sha1_key, 20);
         blob_write_uint32(&blob, snir->size);
         blob_write_bytes(&blob, snir->data, snir->size);

         if (blob.out_of_memory) {
            blob.size = save_size;
            pthread_mutex_unlock(&cache->mutex);
            result = VK_INCOMPLETE;
            break;
         }

         nir_count++;
      }
   }
   blob_overwrite_uint32(&blob, nir_count_offset, nir_count);

   uint32_t count = 0;
   intptr_t count_offset = blob_reserve_uint32(&blob);
   if (count_offset < 0) {
      *pDataSize = 0;
      blob_finish(&blob);
      pthread_mutex_unlock(&cache->mutex);
      return VK_INCOMPLETE;
   }

   if (cache->variant_cache) {
      hash_table_foreach(cache->variant_cache, entry) {
         struct v3dv_shader_variant *variant = entry->data;

         size_t save_size = blob.size;
         if (!shader_variant_write_to_blob(variant, &blob)) {
            /* If it fails reset to the previous size and bail */
            blob.size = save_size;
            pthread_mutex_unlock(&cache->mutex);
            result = VK_INCOMPLETE;
            break;
         }

         count++;
      }
   }

   blob_overwrite_uint32(&blob, count_offset, count);

   *pDataSize = blob.size;

   blob_finish(&blob);

   if (unlikely(dump_stats)) {
      assert(count <= cache->variant_stats.count);
      fprintf(stderr, "GetPipelineCacheData: serializing cache %p, "
              "%i nir shader entries "
              "%i variant entries, %u DataSize\n",
              cache, nir_count, count, (uint32_t) *pDataSize);
   }

   pthread_mutex_unlock(&cache->mutex);

   return result;
}
