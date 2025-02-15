/*
 * Copyright © 2024 Collabora Ltd.
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "genxml/gen_macros.h"
#include "util/macros.h"
#include "bifrost_compile.h"
#include "libpan_shaders.h"
#include "panvk_device.h"
#include "panvk_precomp_cache.h"
#include "panvk_shader.h"
#include "vk_alloc.h"
#include "vk_shader.h"

struct panvk_precomp_cache *
panvk_per_arch(precomp_cache_init)(struct panvk_device *dev)
{
   struct panvk_precomp_cache *res =
      vk_zalloc(&dev->vk.alloc, sizeof(struct panvk_precomp_cache), 8,
                VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

   if (res == NULL)
      return NULL;

   simple_mtx_init(&res->lock, mtx_plain);
   res->dev = dev;
   res->programs = GENX(libpan_shaders_default);

   return res;
}

void
panvk_per_arch(precomp_cache_cleanup)(struct panvk_precomp_cache *cache)
{
   for (unsigned i = 0; i < ARRAY_SIZE(cache->precomp); i++) {
      if (cache->precomp[i])
         vk_shader_destroy(&cache->dev->vk, &cache->precomp[i]->vk,
                           &cache->dev->vk.alloc);
   }

   simple_mtx_destroy(&cache->lock);
   vk_free(&cache->dev->vk.alloc, cache);
}

static struct panvk_shader *
panvk_get_precompiled_locked(struct panvk_precomp_cache *cache,
                             unsigned program)
{
   simple_mtx_assert_locked(&cache->lock);

   /* It is possible that, while waiting for the lock, another thread uploaded
    * the shader. Check for that so we don't double-upload.
    */
   if (cache->precomp[program])
      return cache->precomp[program];

   const uint32_t *bin = cache->programs[program];
   const struct bifrost_precompiled_kernel_info *info = (void *)bin;
   const void *binary = (const uint8_t *)bin + sizeof(*info);
   struct pan_compute_dim local_dim = {
      .x = info->local_size_x,
      .y = info->local_size_y,
      .z = info->local_size_z,
   };

   struct panvk_shader *shader;
   VkResult result = panvk_per_arch(create_shader_from_binary)(
      cache->dev, &info->info, local_dim, binary, info->binary_size, &shader);

   if (result != VK_SUCCESS)
      return NULL;

   /* We must only write to the cache once we are done compiling, since other
    * threads may be reading the cache concurrently. Do this last.
    */
   p_atomic_set(&cache->precomp[program], shader);

   return shader;
}

struct panvk_shader *
panvk_per_arch(precomp_cache_get)(struct panvk_precomp_cache *cache,
                                  unsigned program)
{
   /* Shaders are immutable once written, so if we atomically read a non-NULL
    * shader, then we have a valid cached shader and are done.
    */
   struct panvk_shader *ret = p_atomic_read(cache->precomp + program);

   if (ret != NULL)
      return ret;

   /* Otherwise, take the lock and upload. */
   simple_mtx_lock(&cache->lock);
   ret = panvk_get_precompiled_locked(cache, program);
   simple_mtx_unlock(&cache->lock);

   return ret;
}
