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

#include "util/macros.h"
#include "util/mesa-sha1.h"
#include "util/debug.h"
#include "util/disk_cache.h"
#include "util/u_atomic.h"
#include "radv_debug.h"
#include "radv_private.h"
#include "radv_shader.h"
#include "vulkan/util/vk_util.h"

#include "ac_nir_to_llvm.h"

struct cache_entry {
	union {
		unsigned char sha1[20];
		uint32_t sha1_dw[5];
	};
	uint32_t binary_sizes[MESA_SHADER_STAGES];
	struct radv_shader_variant *variants[MESA_SHADER_STAGES];
	char code[0];
};

static void
radv_pipeline_cache_lock(struct radv_pipeline_cache *cache)
{
	if (cache->flags & VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT_EXT)
		return;

	pthread_mutex_lock(&cache->mutex);
}

static void
radv_pipeline_cache_unlock(struct radv_pipeline_cache *cache)
{
	if (cache->flags & VK_PIPELINE_CACHE_CREATE_EXTERNALLY_SYNCHRONIZED_BIT_EXT)
		return;

	pthread_mutex_unlock(&cache->mutex);
}

void
radv_pipeline_cache_init(struct radv_pipeline_cache *cache,
			 struct radv_device *device)
{
	cache->device = device;
	pthread_mutex_init(&cache->mutex, NULL);
	cache->flags = 0;

	cache->modified = false;
	cache->kernel_count = 0;
	cache->total_size = 0;
	cache->table_size = 1024;
	const size_t byte_size = cache->table_size * sizeof(cache->hash_table[0]);
	cache->hash_table = malloc(byte_size);

	/* We don't consider allocation failure fatal, we just start with a 0-sized
	 * cache. Disable caching when we want to keep shader debug info, since
	 * we don't get the debug info on cached shaders. */
	if (cache->hash_table == NULL ||
	    (device->instance->debug_flags & RADV_DEBUG_NO_CACHE))
		cache->table_size = 0;
	else
		memset(cache->hash_table, 0, byte_size);
}

void
radv_pipeline_cache_finish(struct radv_pipeline_cache *cache)
{
	for (unsigned i = 0; i < cache->table_size; ++i)
		if (cache->hash_table[i]) {
			for(int j = 0; j < MESA_SHADER_STAGES; ++j)  {
				if (cache->hash_table[i]->variants[j])
					radv_shader_variant_destroy(cache->device,
								    cache->hash_table[i]->variants[j]);
			}
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
		if (entry->binary_sizes[i])
			ret += entry->binary_sizes[i];
	ret = align(ret, alignof(struct cache_entry));
	return ret;
}

void
radv_hash_shaders(unsigned char *hash,
		  const VkPipelineShaderStageCreateInfo **stages,
		  const struct radv_pipeline_layout *layout,
		  const struct radv_pipeline_key *key,
		  uint32_t flags)
{
	struct mesa_sha1 ctx;

	_mesa_sha1_init(&ctx);
	if (key)
		_mesa_sha1_update(&ctx, key, sizeof(*key));
	if (layout)
		_mesa_sha1_update(&ctx, layout->sha1, sizeof(layout->sha1));

	for (int i = 0; i < MESA_SHADER_STAGES; ++i) {
		if (stages[i]) {
			RADV_FROM_HANDLE(radv_shader_module, module, stages[i]->module);
			const VkSpecializationInfo *spec_info = stages[i]->pSpecializationInfo;

			_mesa_sha1_update(&ctx, module->sha1, sizeof(module->sha1));
			_mesa_sha1_update(&ctx, stages[i]->pName, strlen(stages[i]->pName));
			if (spec_info && spec_info->mapEntryCount) {
				_mesa_sha1_update(&ctx, spec_info->pMapEntries,
				                  spec_info->mapEntryCount * sizeof spec_info->pMapEntries[0]);
				_mesa_sha1_update(&ctx, spec_info->pData, spec_info->dataSize);
			}
		}
	}
	_mesa_sha1_update(&ctx, &flags, 4);
	_mesa_sha1_final(&ctx, hash);
}


static struct cache_entry *
radv_pipeline_cache_search_unlocked(struct radv_pipeline_cache *cache,
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
radv_pipeline_cache_search(struct radv_pipeline_cache *cache,
			   const unsigned char *sha1)
{
	struct cache_entry *entry;

	radv_pipeline_cache_lock(cache);

	entry = radv_pipeline_cache_search_unlocked(cache, sha1);

	radv_pipeline_cache_unlock(cache);

	return entry;
}

static void
radv_pipeline_cache_set_entry(struct radv_pipeline_cache *cache,
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
radv_pipeline_cache_grow(struct radv_pipeline_cache *cache)
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

		radv_pipeline_cache_set_entry(cache, entry);
	}

	free(old_table);

	return VK_SUCCESS;
}

static void
radv_pipeline_cache_add_entry(struct radv_pipeline_cache *cache,
			      struct cache_entry *entry)
{
	if (cache->kernel_count == cache->table_size / 2)
		radv_pipeline_cache_grow(cache);

	/* Failing to grow that hash table isn't fatal, but may mean we don't
	 * have enough space to add this new kernel. Only add it if there's room.
	 */
	if (cache->kernel_count < cache->table_size / 2)
		radv_pipeline_cache_set_entry(cache, entry);
}

static bool
radv_is_cache_disabled(struct radv_device *device)
{
	/* Pipeline caches can be disabled with RADV_DEBUG=nocache, with
	 * MESA_GLSL_CACHE_DISABLE=1, and when VK_AMD_shader_info is requested.
	 */
	return (device->instance->debug_flags & RADV_DEBUG_NO_CACHE);
}

bool
radv_create_shader_variants_from_pipeline_cache(struct radv_device *device,
					        struct radv_pipeline_cache *cache,
					        const unsigned char *sha1,
					        struct radv_shader_variant **variants,
						bool *found_in_application_cache)
{
	struct cache_entry *entry;

	if (!cache) {
		cache = device->mem_cache;
		*found_in_application_cache = false;
	}

	radv_pipeline_cache_lock(cache);

	entry = radv_pipeline_cache_search_unlocked(cache, sha1);

	if (!entry) {
		*found_in_application_cache = false;

		/* Don't cache when we want debug info, since this isn't
		 * present in the cache.
		 */
		if (radv_is_cache_disabled(device) || !device->physical_device->disk_cache) {
			radv_pipeline_cache_unlock(cache);
			return false;
		}

		uint8_t disk_sha1[20];
		disk_cache_compute_key(device->physical_device->disk_cache,
				       sha1, 20, disk_sha1);

		entry = (struct cache_entry *)
			disk_cache_get(device->physical_device->disk_cache,
				       disk_sha1, NULL);
		if (!entry) {
			radv_pipeline_cache_unlock(cache);
			return false;
		} else {
			size_t size = entry_size(entry);
			struct cache_entry *new_entry = vk_alloc(&cache->alloc, size, 8,
								 VK_SYSTEM_ALLOCATION_SCOPE_CACHE);
			if (!new_entry) {
				free(entry);
				radv_pipeline_cache_unlock(cache);
				return false;
			}

			memcpy(new_entry, entry, entry_size(entry));
			free(entry);
			entry = new_entry;

			if (!(device->instance->debug_flags & RADV_DEBUG_NO_MEMORY_CACHE) ||
			    cache != device->mem_cache)
				radv_pipeline_cache_add_entry(cache, new_entry);
		}
	}

	char *p = entry->code;
	for(int i = 0; i < MESA_SHADER_STAGES; ++i) {
		if (!entry->variants[i] && entry->binary_sizes[i]) {
			struct radv_shader_binary *binary = calloc(1, entry->binary_sizes[i]);
			memcpy(binary, p, entry->binary_sizes[i]);
			p += entry->binary_sizes[i];

			entry->variants[i] = radv_shader_variant_create(device, binary, false);
			free(binary);
		} else if (entry->binary_sizes[i]) {
			p += entry->binary_sizes[i];
		}

	}

	memcpy(variants, entry->variants, sizeof(entry->variants));

	if (device->instance->debug_flags & RADV_DEBUG_NO_MEMORY_CACHE &&
	    cache == device->mem_cache)
		vk_free(&cache->alloc, entry);
	else {
		for (int i = 0; i < MESA_SHADER_STAGES; ++i)
			if (entry->variants[i])
				p_atomic_inc(&entry->variants[i]->ref_count);
	}

	radv_pipeline_cache_unlock(cache);
	return true;
}

void
radv_pipeline_cache_insert_shaders(struct radv_device *device,
				   struct radv_pipeline_cache *cache,
				   const unsigned char *sha1,
				   struct radv_shader_variant **variants,
				   struct radv_shader_binary *const *binaries)
{
	if (!cache)
		cache = device->mem_cache;

	radv_pipeline_cache_lock(cache);
	struct cache_entry *entry = radv_pipeline_cache_search_unlocked(cache, sha1);
	if (entry) {
		for (int i = 0; i < MESA_SHADER_STAGES; ++i) {
			if (entry->variants[i]) {
				radv_shader_variant_destroy(cache->device, variants[i]);
				variants[i] = entry->variants[i];
			} else {
				entry->variants[i] = variants[i];
			}
			if (variants[i])
				p_atomic_inc(&variants[i]->ref_count);
		}
		radv_pipeline_cache_unlock(cache);
		return;
	}

	/* Don't cache when we want debug info, since this isn't
	 * present in the cache.
	 */
	if (radv_is_cache_disabled(device)) {
		radv_pipeline_cache_unlock(cache);
		return;
	}

	size_t size = sizeof(*entry);
	for (int i = 0; i < MESA_SHADER_STAGES; ++i)
		if (variants[i])
			size += binaries[i]->total_size;
	size = align(size, alignof(struct cache_entry));


	entry = vk_alloc(&cache->alloc, size, 8,
			   VK_SYSTEM_ALLOCATION_SCOPE_CACHE);
	if (!entry) {
		radv_pipeline_cache_unlock(cache);
		return;
	}

	memset(entry, 0, sizeof(*entry));
	memcpy(entry->sha1, sha1, 20);

	char* p = entry->code;

	for (int i = 0; i < MESA_SHADER_STAGES; ++i) {
		if (!variants[i])
			continue;

		entry->binary_sizes[i] = binaries[i]->total_size;

		memcpy(p, binaries[i], binaries[i]->total_size);
		p += binaries[i]->total_size;
	}

	/* Always add cache items to disk. This will allow collection of
	 * compiled shaders by third parties such as steam, even if the app
	 * implements its own pipeline cache.
	 */
	if (device->physical_device->disk_cache) {
		uint8_t disk_sha1[20];
		disk_cache_compute_key(device->physical_device->disk_cache, sha1, 20,
			       disk_sha1);

		disk_cache_put(device->physical_device->disk_cache, disk_sha1,
			       entry, entry_size(entry), NULL);
	}

	if (device->instance->debug_flags & RADV_DEBUG_NO_MEMORY_CACHE &&
	    cache == device->mem_cache) {
		vk_free2(&cache->alloc, NULL, entry);
		radv_pipeline_cache_unlock(cache);
		return;
	}

	/* We delay setting the variant so we have reproducible disk cache
	 * items.
	 */
	for (int i = 0; i < MESA_SHADER_STAGES; ++i) {
		if (!variants[i])
			continue;

		entry->variants[i] = variants[i];
		p_atomic_inc(&variants[i]->ref_count);
	}

	radv_pipeline_cache_add_entry(cache, entry);

	cache->modified = true;
	radv_pipeline_cache_unlock(cache);
	return;
}

bool
radv_pipeline_cache_load(struct radv_pipeline_cache *cache,
			 const void *data, size_t size)
{
	struct radv_device *device = cache->device;
	struct vk_pipeline_cache_header header;

	if (size < sizeof(header))
		return false;
	memcpy(&header, data, sizeof(header));
	if (header.header_size < sizeof(header))
		return false;
	if (header.header_version != VK_PIPELINE_CACHE_HEADER_VERSION_ONE)
		return false;
	if (header.vendor_id != ATI_VENDOR_ID)
		return false;
	if (header.device_id != device->physical_device->rad_info.pci_id)
		return false;
	if (memcmp(header.uuid, device->physical_device->cache_uuid, VK_UUID_SIZE) != 0)
		return false;

	char *end = (void *) data + size;
	char *p = (void *) data + header.header_size;

	while (end - p >= sizeof(struct cache_entry)) {
		struct cache_entry *entry = (struct cache_entry*)p;
		struct cache_entry *dest_entry;
		size_t size = entry_size(entry);
		if(end - p < size)
			break;

		dest_entry = vk_alloc(&cache->alloc, size,
					8, VK_SYSTEM_ALLOCATION_SCOPE_CACHE);
		if (dest_entry) {
			memcpy(dest_entry, entry, size);
			for (int i = 0; i < MESA_SHADER_STAGES; ++i)
				dest_entry->variants[i] = NULL;
			radv_pipeline_cache_add_entry(cache, dest_entry);
		}
		p += size;
	}

	return true;
}

VkResult radv_CreatePipelineCache(
	VkDevice                                    _device,
	const VkPipelineCacheCreateInfo*            pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkPipelineCache*                            pPipelineCache)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	struct radv_pipeline_cache *cache;

	assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);

	cache = vk_alloc2(&device->vk.alloc, pAllocator,
			    sizeof(*cache), 8,
			    VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);
	if (cache == NULL)
		return vk_error(device->instance, VK_ERROR_OUT_OF_HOST_MEMORY);

	vk_object_base_init(&device->vk, &cache->base,
			    VK_OBJECT_TYPE_PIPELINE_CACHE);

	if (pAllocator)
		cache->alloc = *pAllocator;
	else
		cache->alloc = device->vk.alloc;

	radv_pipeline_cache_init(cache, device);
	cache->flags = pCreateInfo->flags;

	if (pCreateInfo->initialDataSize > 0) {
		radv_pipeline_cache_load(cache,
					 pCreateInfo->pInitialData,
					 pCreateInfo->initialDataSize);
	}

	*pPipelineCache = radv_pipeline_cache_to_handle(cache);

	return VK_SUCCESS;
}

void radv_DestroyPipelineCache(
	VkDevice                                    _device,
	VkPipelineCache                             _cache,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_pipeline_cache, cache, _cache);

	if (!cache)
		return;
	radv_pipeline_cache_finish(cache);

	vk_object_base_finish(&cache->base);
	vk_free2(&device->vk.alloc, pAllocator, cache);
}

VkResult radv_GetPipelineCacheData(
	VkDevice                                    _device,
	VkPipelineCache                             _cache,
	size_t*                                     pDataSize,
	void*                                       pData)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_pipeline_cache, cache, _cache);
	struct vk_pipeline_cache_header *header;
	VkResult result = VK_SUCCESS;

	radv_pipeline_cache_lock(cache);

	const size_t size = sizeof(*header) + cache->total_size;
	if (pData == NULL) {
		radv_pipeline_cache_unlock(cache);
		*pDataSize = size;
		return VK_SUCCESS;
	}
	if (*pDataSize < sizeof(*header)) {
		radv_pipeline_cache_unlock(cache);
		*pDataSize = 0;
		return VK_INCOMPLETE;
	}
	void *p = pData, *end = pData + *pDataSize;
	header = p;
	header->header_size = align(sizeof(*header), alignof(struct cache_entry));
	header->header_version = VK_PIPELINE_CACHE_HEADER_VERSION_ONE;
	header->vendor_id = ATI_VENDOR_ID;
	header->device_id = device->physical_device->rad_info.pci_id;
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
		for(int j = 0; j < MESA_SHADER_STAGES; ++j)
			((struct cache_entry*)p)->variants[j] = NULL;
		p += size;
	}
	*pDataSize = p - pData;

	radv_pipeline_cache_unlock(cache);
	return result;
}

static void
radv_pipeline_cache_merge(struct radv_pipeline_cache *dst,
			  struct radv_pipeline_cache *src)
{
	for (uint32_t i = 0; i < src->table_size; i++) {
		struct cache_entry *entry = src->hash_table[i];
		if (!entry || radv_pipeline_cache_search(dst, entry->sha1))
			continue;

		radv_pipeline_cache_add_entry(dst, entry);

		src->hash_table[i] = NULL;
	}
}

VkResult radv_MergePipelineCaches(
	VkDevice                                    _device,
	VkPipelineCache                             destCache,
	uint32_t                                    srcCacheCount,
	const VkPipelineCache*                      pSrcCaches)
{
	RADV_FROM_HANDLE(radv_pipeline_cache, dst, destCache);

	for (uint32_t i = 0; i < srcCacheCount; i++) {
		RADV_FROM_HANDLE(radv_pipeline_cache, src, pSrcCaches[i]);

		radv_pipeline_cache_merge(dst, src);
	}

	return VK_SUCCESS;
}
