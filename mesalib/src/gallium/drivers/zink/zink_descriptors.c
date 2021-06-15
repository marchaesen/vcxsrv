/*
 * Copyright © 2020 Mike Blumenkrantz
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
 * 
 * Authors:
 *    Mike Blumenkrantz <michael.blumenkrantz@gmail.com>
 */

#include "tgsi/tgsi_from_mesa.h"



#include "zink_context.h"
#include "zink_descriptors.h"
#include "zink_program.h"
#include "zink_resource.h"
#include "zink_screen.h"

#define XXH_INLINE_ALL
#include "util/xxhash.h"

void
debug_describe_zink_descriptor_pool(char *buf, const struct zink_descriptor_pool *ptr)
{
   sprintf(buf, "zink_descriptor_pool");
}

static bool
desc_state_equal(const void *a, const void *b)
{
   const struct zink_descriptor_state_key *a_k = (void*)a;
   const struct zink_descriptor_state_key *b_k = (void*)b;

   for (unsigned i = 0; i < ZINK_SHADER_COUNT; i++) {
      if (a_k->exists[i] != b_k->exists[i])
         return false;
      if (a_k->exists[i] && b_k->exists[i] &&
          a_k->state[i] != b_k->state[i])
         return false;
   }
   return true;
}

static uint32_t
desc_state_hash(const void *key)
{
   const struct zink_descriptor_state_key *d_key = (void*)key;
   uint32_t hash = 0;
   bool first = true;
   for (unsigned i = 0; i < ZINK_SHADER_COUNT; i++) {
      if (d_key->exists[i]) {
         if (!first)
            hash = XXH32(&d_key->state[i], sizeof(uint32_t), hash);
         else
            hash = d_key->state[i];
         first = false;
      }
   }
   return hash;
}

static struct zink_descriptor_pool *
descriptor_pool_create(struct zink_screen *screen, enum zink_descriptor_type type, VkDescriptorSetLayoutBinding *bindings, unsigned num_bindings, VkDescriptorPoolSize *sizes, unsigned num_type_sizes)
{
   struct zink_descriptor_pool *pool = rzalloc(NULL, struct zink_descriptor_pool);
   if (!pool)
      return NULL;
   pipe_reference_init(&pool->reference, 1);
   pool->type = type;
   pool->key.num_descriptors = num_bindings;
   pool->key.num_type_sizes = num_type_sizes;
   size_t bindings_size = num_bindings * sizeof(VkDescriptorSetLayoutBinding);
   size_t types_size = num_type_sizes * sizeof(VkDescriptorPoolSize);
   pool->key.bindings = ralloc_size(pool, bindings_size);
   pool->key.sizes = ralloc_size(pool, types_size);
   if (!pool->key.bindings || !pool->key.sizes) {
      ralloc_free(pool);
      return NULL;
   }
   memcpy(pool->key.bindings, bindings, bindings_size);
   memcpy(pool->key.sizes, sizes, types_size);
   simple_mtx_init(&pool->mtx, mtx_plain);
   for (unsigned i = 0; i < num_bindings; i++) {
       pool->num_resources += bindings[i].descriptorCount;
   }
   pool->desc_sets = _mesa_hash_table_create(NULL, desc_state_hash, desc_state_equal);
   if (!pool->desc_sets)
      goto fail;

   pool->free_desc_sets = _mesa_hash_table_create(NULL, desc_state_hash, desc_state_equal);
   if (!pool->free_desc_sets)
      goto fail;

   util_dynarray_init(&pool->alloc_desc_sets, NULL);

   VkDescriptorSetLayoutCreateInfo dcslci = {};
   dcslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
   dcslci.pNext = NULL;
   dcslci.flags = 0;
   dcslci.bindingCount = num_bindings;
   dcslci.pBindings = bindings;
   if (vkCreateDescriptorSetLayout(screen->dev, &dcslci, 0, &pool->dsl) != VK_SUCCESS) {
      debug_printf("vkCreateDescriptorSetLayout failed\n");
      goto fail;
   }

   VkDescriptorPoolCreateInfo dpci = {};
   dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
   dpci.pPoolSizes = sizes;
   dpci.poolSizeCount = num_type_sizes;
   dpci.flags = 0;
   dpci.maxSets = ZINK_DEFAULT_MAX_DESCS;
   if (vkCreateDescriptorPool(screen->dev, &dpci, 0, &pool->descpool) != VK_SUCCESS) {
      debug_printf("vkCreateDescriptorPool failed\n");
      goto fail;
   }

   return pool;
fail:
   zink_descriptor_pool_free(screen, pool);
   return NULL;
}

static uint32_t
hash_descriptor_pool(const void *key)
{
   uint32_t hash = 0;
   const struct zink_descriptor_pool_key *k = key;
   hash = XXH32(&k->num_type_sizes, sizeof(unsigned), hash);
   hash = XXH32(&k->num_descriptors, sizeof(unsigned), hash);
   hash = XXH32(k->bindings, k->num_descriptors * sizeof(VkDescriptorSetLayoutBinding), hash);
   hash = XXH32(k->sizes, k->num_type_sizes * sizeof(VkDescriptorPoolSize), hash);

   return hash;
}

static bool
equals_descriptor_pool(const void *a, const void *b)
{
   const struct zink_descriptor_pool_key *a_k = a;
   const struct zink_descriptor_pool_key *b_k = b;
   return a_k->num_type_sizes == b_k->num_type_sizes &&
          a_k->num_descriptors == b_k->num_descriptors &&
          !memcmp(a_k->bindings, b_k->bindings, a_k->num_descriptors * sizeof(VkDescriptorSetLayoutBinding)) &&
          !memcmp(a_k->sizes, b_k->sizes, a_k->num_type_sizes * sizeof(VkDescriptorPoolSize));
}

static struct zink_descriptor_pool *
descriptor_pool_get(struct zink_context *ctx, enum zink_descriptor_type type, VkDescriptorSetLayoutBinding *bindings, unsigned num_bindings, VkDescriptorPoolSize *sizes, unsigned num_type_sizes)
{
   uint32_t hash = 0;
   struct zink_descriptor_pool_key key = {
      .num_type_sizes = num_type_sizes,
      .num_descriptors = num_bindings,
      .bindings = bindings,
      .sizes = sizes,
   };

   hash = hash_descriptor_pool(&key);
   struct hash_entry *he = _mesa_hash_table_search_pre_hashed(ctx->descriptor_pools[type], hash, &key);
   if (he)
      return (void*)he->data;
   struct zink_descriptor_pool *pool = descriptor_pool_create(zink_screen(ctx->base.screen), type, bindings, num_bindings, sizes, num_type_sizes);
   _mesa_hash_table_insert_pre_hashed(ctx->descriptor_pools[type], hash, &pool->key, pool);
   return pool;
}

static bool
get_invalidated_desc_set(struct zink_descriptor_set *zds)
{
   if (!zds->invalid)
      return false;
   return p_atomic_read(&zds->reference.count) == 1;
}

static struct zink_descriptor_set *
allocate_desc_set(struct zink_screen *screen, struct zink_program *pg, enum zink_descriptor_type type, unsigned descs_used, bool is_compute)
{
   VkDescriptorSetAllocateInfo dsai;
   struct zink_descriptor_pool *pool = pg->pool[type];
#define DESC_BUCKET_FACTOR 10
   unsigned bucket_size = pool->key.num_descriptors ? DESC_BUCKET_FACTOR : 1;
   if (pool->key.num_descriptors) {
      for (unsigned desc_factor = DESC_BUCKET_FACTOR; desc_factor < descs_used; desc_factor *= DESC_BUCKET_FACTOR)
         bucket_size = desc_factor;
   }
   VkDescriptorSetLayout layouts[bucket_size];
   memset((void *)&dsai, 0, sizeof(dsai));
   dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
   dsai.pNext = NULL;
   dsai.descriptorPool = pool->descpool;
   dsai.descriptorSetCount = bucket_size;
   for (unsigned i = 0; i < bucket_size; i ++)
      layouts[i] = pool->dsl;
   dsai.pSetLayouts = layouts;

   VkDescriptorSet desc_set[bucket_size];
   if (vkAllocateDescriptorSets(screen->dev, &dsai, desc_set) != VK_SUCCESS) {
      debug_printf("ZINK: %p failed to allocate descriptor set :/\n", pg);
      return VK_NULL_HANDLE;
   }

   struct zink_descriptor_set *alloc = ralloc_array(pool, struct zink_descriptor_set, bucket_size);
   assert(alloc);
   unsigned num_resources = pool->num_resources;
   struct zink_resource_object **res_objs = rzalloc_array(pool, struct zink_resource_object*, num_resources * bucket_size);
   assert(res_objs);
   void **samplers = NULL;
   if (type == ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW) {
      samplers = rzalloc_array(pool, void*, num_resources * bucket_size);
      assert(samplers);
   }
   for (unsigned i = 0; i < bucket_size; i ++) {
      struct zink_descriptor_set *zds = &alloc[i];
      pipe_reference_init(&zds->reference, 1);
      zds->pool = pool;
      zds->hash = 0;
      zds->batch_uses.usage = 0;
      zds->invalid = true;
      zds->punted = zds->recycled = false;
      if (num_resources) {
         util_dynarray_init(&zds->barriers, alloc);
         if (!util_dynarray_grow(&zds->barriers, struct zink_descriptor_barrier, num_resources)) {
            debug_printf("ZINK: %p failed to allocate descriptor set barriers :/\n", pg);
            return NULL;
         }
      }
#ifndef NDEBUG
      zds->num_resources = num_resources;
#endif
      if (type == ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW) {
         zds->sampler_views = (struct zink_sampler_view**)&res_objs[i * pool->key.num_descriptors];
         zds->sampler_states = (struct zink_sampler_state**)&samplers[i * pool->key.num_descriptors];
      } else
         zds->res_objs = (struct zink_resource_object**)&res_objs[i * pool->key.num_descriptors];
      zds->desc_set = desc_set[i];
      if (i > 0)
         util_dynarray_append(&pool->alloc_desc_sets, struct zink_descriptor_set *, zds);
   }
   pool->num_sets_allocated += bucket_size;
   return alloc;
}

static void
populate_zds_key(struct zink_context *ctx, enum zink_descriptor_type type, bool is_compute,
                 struct zink_descriptor_state_key *key) {
   if (is_compute) {
      for (unsigned i = 1; i < ZINK_SHADER_COUNT; i++)
         key->exists[i] = false;
      key->exists[0] = true;
      key->state[0] = ctx->descriptor_states[is_compute].state[type];
   } else {
      for (unsigned i = 0; i < ZINK_SHADER_COUNT; i++) {
         key->exists[i] = ctx->gfx_descriptor_states[i].valid[type];
         key->state[i] = ctx->gfx_descriptor_states[i].state[type];
      }
   }
}

static void
punt_invalid_set(struct zink_descriptor_set *zds, struct hash_entry *he)
{
   /* this is no longer usable, so we punt it for now until it gets recycled */
   assert(!zds->recycled);
   if (!he)
      he = _mesa_hash_table_search_pre_hashed(zds->pool->desc_sets, zds->hash, &zds->key);
   _mesa_hash_table_remove(zds->pool->desc_sets, he);
   zds->punted = true;
}

static void
zink_descriptor_set_invalidate(struct zink_descriptor_set *zds)
{
   zds->invalid = true;
}

static struct zink_descriptor_set *
zink_descriptor_set_get(struct zink_context *ctx,
                               enum zink_descriptor_type type,
                               bool is_compute,
                               bool *cache_hit,
                               bool *need_resource_refs)
{
   *cache_hit = false;
   struct zink_descriptor_set *zds;
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_program *pg = is_compute ? (struct zink_program *)ctx->curr_compute : (struct zink_program *)ctx->curr_program;
   struct zink_batch *batch = &ctx->batch;
   struct zink_descriptor_pool *pool = pg->pool[type];
   unsigned descs_used = 1;
   assert(type < ZINK_DESCRIPTOR_TYPES);
   uint32_t hash = pool->key.num_descriptors ? ctx->descriptor_states[is_compute].state[type] : 0;
   struct zink_descriptor_state_key key;
   populate_zds_key(ctx, type, is_compute, &key);

   simple_mtx_lock(&pool->mtx);
   if (pg->last_set[type] && pg->last_set[type]->hash == hash &&
       desc_state_equal(&pg->last_set[type]->key, &key)) {
      zds = pg->last_set[type];
      *cache_hit = !zds->invalid;
      if (pool->key.num_descriptors) {
         if (zds->recycled) {
            struct hash_entry *he = _mesa_hash_table_search_pre_hashed(pool->free_desc_sets, hash, &key);
            if (he)
               _mesa_hash_table_remove(pool->free_desc_sets, he);
            zds->recycled = false;
         }
         if (zds->invalid) {
             if (zink_batch_usage_exists(&zds->batch_uses))
                punt_invalid_set(zds, NULL);
             else
                /* this set is guaranteed to be in pool->alloc_desc_sets */
                goto skip_hash_tables;
             zds = NULL;
         }
      }
      if (zds)
         goto out;
   }

   if (pool->key.num_descriptors) {
      struct hash_entry *he = _mesa_hash_table_search_pre_hashed(pool->desc_sets, hash, &key);
      bool recycled = false, punted = false;
      if (he) {
          zds = (void*)he->data;
          if (zds->invalid && zink_batch_usage_exists(&zds->batch_uses)) {
             punt_invalid_set(zds, he);
             zds = NULL;
             punted = true;
          }
      }
      if (!he) {
         he = _mesa_hash_table_search_pre_hashed(pool->free_desc_sets, hash, &key);
         recycled = true;
      }
      if (he && !punted) {
         zds = (void*)he->data;
         *cache_hit = !zds->invalid;
         if (recycled) {
            /* need to migrate this entry back to the in-use hash */
            _mesa_hash_table_remove(pool->free_desc_sets, he);
            goto out;
         }
         goto quick_out;
      }
skip_hash_tables:
      if (util_dynarray_num_elements(&pool->alloc_desc_sets, struct zink_descriptor_set *)) {
         /* grab one off the allocated array */
         zds = util_dynarray_pop(&pool->alloc_desc_sets, struct zink_descriptor_set *);
         goto out;
      }

      if (_mesa_hash_table_num_entries(pool->free_desc_sets)) {
         /* try for an invalidated set first */
         unsigned count = 0;
         hash_table_foreach(pool->free_desc_sets, he) {
            struct zink_descriptor_set *tmp = he->data;
            if ((count++ >= 100 && tmp->reference.count == 1) || get_invalidated_desc_set(he->data)) {
               zds = tmp;
               assert(p_atomic_read(&zds->reference.count) == 1);
               zink_descriptor_set_invalidate(zds);
               _mesa_hash_table_remove(pool->free_desc_sets, he);
               goto out;
            }
         }
      }

      if (pool->num_sets_allocated + pool->key.num_descriptors > ZINK_DEFAULT_MAX_DESCS) {
         simple_mtx_unlock(&pool->mtx);
         zink_fence_wait(&ctx->base);
         zink_batch_reference_program(batch, pg);
         return zink_descriptor_set_get(ctx, type, is_compute, cache_hit, need_resource_refs);
      }
   } else {
      if (pg->last_set[type] && !pg->last_set[type]->hash) {
         zds = pg->last_set[type];
         *cache_hit = true;
         goto quick_out;
      }
   }

   zds = allocate_desc_set(screen, pg, type, descs_used, is_compute);
out:
   zds->hash = hash;
   populate_zds_key(ctx, type, is_compute, &zds->key);
   zds->recycled = false;
   if (pool->key.num_descriptors)
      _mesa_hash_table_insert_pre_hashed(pool->desc_sets, hash, &zds->key, zds);
   else {
      /* we can safely apply the null set to all the slots which will need it here */
      for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPES; i++) {
         if (pg->pool[i] && !pg->pool[i]->key.num_descriptors)
            pg->last_set[i] = zds;
      }
   }
quick_out:
   if (pool->key.num_descriptors && !*cache_hit)
      util_dynarray_clear(&zds->barriers);
   zds->punted = zds->invalid = false;
   *need_resource_refs = false;
   if (zink_batch_add_desc_set(batch, zds)) {
      batch->state->descs_used += pool->key.num_descriptors;
      *need_resource_refs = true;
   }
   pg->last_set[type] = zds;
   simple_mtx_unlock(&pool->mtx);

   return zds;
}

void
zink_descriptor_set_recycle(struct zink_descriptor_set *zds)
{
   struct zink_descriptor_pool *pool = zds->pool;
   /* if desc set is still in use by a batch, don't recache */
   uint32_t refcount = p_atomic_read(&zds->reference.count);
   if (refcount != 1)
      return;
   /* this is a null set */
   if (!pool->key.num_descriptors)
      return;
   simple_mtx_lock(&pool->mtx);
   if (zds->punted)
      zds->invalid = true;
   else {
      /* if we've previously punted this set, then it won't have a hash or be in either of the tables */
      struct hash_entry *he = _mesa_hash_table_search_pre_hashed(pool->desc_sets, zds->hash, &zds->key);
      if (!he) {
         /* desc sets can be used multiple times in the same batch */
         simple_mtx_unlock(&pool->mtx);
         return;
      }
      _mesa_hash_table_remove(pool->desc_sets, he);
   }

   if (zds->invalid) {
      util_dynarray_append(&pool->alloc_desc_sets, struct zink_descriptor_set *, zds);
   } else {
      zds->recycled = true;
      _mesa_hash_table_insert_pre_hashed(pool->free_desc_sets, zds->hash, &zds->key, zds);
   }
   simple_mtx_unlock(&pool->mtx);
}


static void
desc_set_ref_add(struct zink_descriptor_set *zds, struct zink_descriptor_refs *refs, void **ref_ptr, void *ptr)
{
   struct zink_descriptor_reference ref = {ref_ptr, &zds->invalid};
   *ref_ptr = ptr;
   if (ptr)
      util_dynarray_append(&refs->refs, struct zink_descriptor_reference, ref);
}

static void
zink_image_view_desc_set_add(struct zink_image_view *image_view, struct zink_descriptor_set *zds, unsigned idx)
{
   desc_set_ref_add(zds, &image_view->desc_set_refs, (void**)&zds->image_views[idx], image_view);
}

static void
zink_sampler_state_desc_set_add(struct zink_sampler_state *sampler_state, struct zink_descriptor_set *zds, unsigned idx)
{
   desc_set_ref_add(zds, &sampler_state->desc_set_refs, (void**)&zds->sampler_states[idx], sampler_state);
}

static void
zink_sampler_view_desc_set_add(struct zink_sampler_view *sampler_view, struct zink_descriptor_set *zds, unsigned idx)
{
   desc_set_ref_add(zds, &sampler_view->desc_set_refs, (void**)&zds->sampler_views[idx], sampler_view);
}

static void
zink_resource_desc_set_add(struct zink_resource *res, struct zink_descriptor_set *zds, unsigned idx)
{
   desc_set_ref_add(zds, res ? &res->obj->desc_set_refs : NULL, (void**)&zds->res_objs[idx], res ? res->obj : NULL);
}

void
zink_descriptor_set_refs_clear(struct zink_descriptor_refs *refs, void *ptr)
{
   util_dynarray_foreach(&refs->refs, struct zink_descriptor_reference, ref) {
      if (*ref->ref == ptr) {
         *ref->invalid = true;
         *ref->ref = NULL;
      }
   }
   util_dynarray_fini(&refs->refs);
}

bool
zink_descriptor_program_init(struct zink_context *ctx,
                       struct zink_shader *stages[ZINK_SHADER_COUNT],
                       struct zink_program *pg)
{
   VkDescriptorSetLayoutBinding bindings[ZINK_DESCRIPTOR_TYPES][PIPE_SHADER_TYPES * 32];
   int num_bindings[ZINK_DESCRIPTOR_TYPES] = {};

   VkDescriptorPoolSize sizes[6] = {};
   int type_map[12];
   unsigned num_types = 0;
   memset(type_map, -1, sizeof(type_map));

   for (int i = 0; i < ZINK_SHADER_COUNT; i++) {
      struct zink_shader *shader = stages[i];
      if (!shader)
         continue;

      VkShaderStageFlagBits stage_flags = zink_shader_stage(pipe_shader_type_from_mesa(shader->nir->info.stage));
      for (int j = 0; j < ZINK_DESCRIPTOR_TYPES; j++) {
         for (int k = 0; k < shader->num_bindings[j]; k++) {
            assert(num_bindings[j] < ARRAY_SIZE(bindings[j]));
            bindings[j][num_bindings[j]].binding = shader->bindings[j][k].binding;
            bindings[j][num_bindings[j]].descriptorType = shader->bindings[j][k].type;
            bindings[j][num_bindings[j]].descriptorCount = shader->bindings[j][k].size;
            bindings[j][num_bindings[j]].stageFlags = stage_flags;
            bindings[j][num_bindings[j]].pImmutableSamplers = NULL;
            if (type_map[shader->bindings[j][k].type] == -1) {
               type_map[shader->bindings[j][k].type] = num_types++;
               sizes[type_map[shader->bindings[j][k].type]].type = shader->bindings[j][k].type;
            }
            sizes[type_map[shader->bindings[j][k].type]].descriptorCount += shader->bindings[j][k].size;
            ++num_bindings[j];
         }
      }
   }

   unsigned total_descs = 0;
   for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPES; i++) {
      total_descs += num_bindings[i];
   }
   if (!total_descs)
      return true;

   for (int i = 0; i < num_types; i++)
      sizes[i].descriptorCount *= ZINK_DEFAULT_MAX_DESCS;

   bool found_descriptors = false;
   for (unsigned i = ZINK_DESCRIPTOR_TYPES - 1; i < ZINK_DESCRIPTOR_TYPES; i--) {
      struct zink_descriptor_pool *pool;
      if (!num_bindings[i]) {
         if (!found_descriptors)
            continue;
         VkDescriptorSetLayoutBinding null_binding;
         null_binding.binding = 1;
         null_binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
         null_binding.descriptorCount = 1;
         null_binding.pImmutableSamplers = NULL;
         null_binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT |
                                   VK_SHADER_STAGE_GEOMETRY_BIT | VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT |
                                   VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
         VkDescriptorPoolSize null_size = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, ZINK_DEFAULT_MAX_DESCS};
         pool = descriptor_pool_get(ctx, i, &null_binding, 1, &null_size, 1);
         if (!pool)
            return false;
         pool->key.num_descriptors = 0;
         zink_descriptor_pool_reference(zink_screen(ctx->base.screen), &pg->pool[i], pool);
         continue;
      }
      found_descriptors = true;

      VkDescriptorPoolSize type_sizes[2] = {};
      int num_type_sizes = 0;
      switch (i) {
      case ZINK_DESCRIPTOR_TYPE_UBO:
         if (type_map[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER] != -1) {
            type_sizes[num_type_sizes] = sizes[type_map[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER]];
            num_type_sizes++;
         }
         if (type_map[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC] != -1) {
            type_sizes[num_type_sizes] = sizes[type_map[VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC]];
            num_type_sizes++;
         }
         break;
      case ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW:
         if (type_map[VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER] != -1) {
            type_sizes[num_type_sizes] = sizes[type_map[VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER]];
            num_type_sizes++;
         }
         if (type_map[VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER] != -1) {
            type_sizes[num_type_sizes] = sizes[type_map[VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER]];
            num_type_sizes++;
         }
         break;
      case ZINK_DESCRIPTOR_TYPE_SSBO:
         if (type_map[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER] != -1) {
            num_type_sizes = 1;
            type_sizes[0] = sizes[type_map[VK_DESCRIPTOR_TYPE_STORAGE_BUFFER]];
         }
         break;
      case ZINK_DESCRIPTOR_TYPE_IMAGE:
         if (type_map[VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER] != -1) {
            type_sizes[num_type_sizes] = sizes[type_map[VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER]];
            num_type_sizes++;
         }
         if (type_map[VK_DESCRIPTOR_TYPE_STORAGE_IMAGE] != -1) {
            type_sizes[num_type_sizes] = sizes[type_map[VK_DESCRIPTOR_TYPE_STORAGE_IMAGE]];
            num_type_sizes++;
         }
         break;
      }
      pool = descriptor_pool_get(ctx, i, bindings[i], num_bindings[i], type_sizes, num_type_sizes);
      if (!pool)
         return false;
      zink_descriptor_pool_reference(zink_screen(ctx->base.screen), &pg->pool[i], pool);
   }
   return true;
}

#ifndef NDEBUG
static void
descriptor_pool_clear(struct hash_table *ht)
{
   hash_table_foreach(ht, entry) {
      struct zink_descriptor_set *zds = entry->data;
      zink_descriptor_set_invalidate(zds);
   }
   _mesa_hash_table_clear(ht, NULL);
}
#endif

void
zink_descriptor_pool_free(struct zink_screen *screen, struct zink_descriptor_pool *pool)
{
   if (!pool)
      return;
   if (pool->dsl)
      vkDestroyDescriptorSetLayout(screen->dev, pool->dsl, NULL);
   if (pool->descpool)
      vkDestroyDescriptorPool(screen->dev, pool->descpool, NULL);

   simple_mtx_lock(&pool->mtx);
#ifndef NDEBUG
   if (pool->desc_sets)
      descriptor_pool_clear(pool->desc_sets);
   if (pool->free_desc_sets)
      descriptor_pool_clear(pool->free_desc_sets);
#endif
   if (pool->desc_sets)
      _mesa_hash_table_destroy(pool->desc_sets, NULL);
   if (pool->free_desc_sets)
      _mesa_hash_table_destroy(pool->free_desc_sets, NULL);

   simple_mtx_unlock(&pool->mtx);
   util_dynarray_fini(&pool->alloc_desc_sets);
   simple_mtx_destroy(&pool->mtx);
   ralloc_free(pool);
}

void
zink_descriptor_pool_deinit(struct zink_context *ctx)
{
   for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPES; i++) {
      hash_table_foreach(ctx->descriptor_pools[i], entry) {
         struct zink_descriptor_pool *pool = (void*)entry->data;
         zink_descriptor_pool_reference(zink_screen(ctx->base.screen), &pool, NULL);
      }
      _mesa_hash_table_destroy(ctx->descriptor_pools[i], NULL);
   }
}

bool
zink_descriptor_pool_init(struct zink_context *ctx)
{
   for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPES; i++) {
      ctx->descriptor_pools[i] = _mesa_hash_table_create(ctx, hash_descriptor_pool, equals_descriptor_pool);
      if (!ctx->descriptor_pools[i])
         return false;
   }
   return true;
}


static void
desc_set_res_add(struct zink_descriptor_set *zds, struct zink_resource *res, unsigned int i, bool cache_hit)
{
   /* if we got a cache hit, we have to verify that the cached set is still valid;
    * we store the vk resource to the set here to avoid a more complex and costly mechanism of maintaining a
    * hash table on every resource with the associated descriptor sets that then needs to be iterated through
    * whenever a resource is destroyed
    */
   assert(!cache_hit || zds->res_objs[i] == (res ? res->obj : NULL));
   if (!cache_hit)
      zink_resource_desc_set_add(res, zds, i);
}

static void
desc_set_sampler_add(struct zink_context *ctx, struct zink_descriptor_set *zds, struct zink_sampler_view *sv,
                     struct zink_sampler_state *state, unsigned int i, bool is_buffer, bool cache_hit)
{
   /* if we got a cache hit, we have to verify that the cached set is still valid;
    * we store the vk resource to the set here to avoid a more complex and costly mechanism of maintaining a
    * hash table on every resource with the associated descriptor sets that then needs to be iterated through
    * whenever a resource is destroyed
    */
#ifndef NDEBUG
   uint32_t cur_hash = zink_get_sampler_view_hash(ctx, zds->sampler_views[i], is_buffer);
   uint32_t new_hash = zink_get_sampler_view_hash(ctx, sv, is_buffer);
#endif
   assert(!cache_hit || cur_hash == new_hash);
   assert(!cache_hit || zds->sampler_states[i] == state);
   if (!cache_hit) {
      zink_sampler_view_desc_set_add(sv, zds, i);
      zink_sampler_state_desc_set_add(state, zds, i);
   }
}

static void
desc_set_image_add(struct zink_context *ctx, struct zink_descriptor_set *zds, struct zink_image_view *image_view,
                   unsigned int i, bool is_buffer, bool cache_hit)
{
   /* if we got a cache hit, we have to verify that the cached set is still valid;
    * we store the vk resource to the set here to avoid a more complex and costly mechanism of maintaining a
    * hash table on every resource with the associated descriptor sets that then needs to be iterated through
    * whenever a resource is destroyed
    */
#ifndef NDEBUG
   uint32_t cur_hash = zink_get_image_view_hash(ctx, zds->image_views[i], is_buffer);
   uint32_t new_hash = zink_get_image_view_hash(ctx, image_view, is_buffer);
#endif
   assert(!cache_hit || cur_hash == new_hash);
   if (!cache_hit)
      zink_image_view_desc_set_add(image_view, zds, i);
}

static bool
barrier_equals(const void *a, const void *b)
{
   const struct zink_descriptor_barrier *t1 = a, *t2 = b;
   if (t1->res != t2->res)
      return false;
   if ((t1->access & t2->access) != t2->access)
      return false;
   if (t1->layout != t2->layout)
      return false;
   return true;
}

static uint32_t
barrier_hash(const void *key)
{
   return _mesa_hash_data(key, offsetof(struct zink_descriptor_barrier, stage));
}

static inline void
add_barrier(struct zink_resource *res, VkImageLayout layout, VkAccessFlags flags, enum pipe_shader_type stage, struct util_dynarray *barriers, struct set *ht)
{
   VkPipelineStageFlags pipeline = zink_pipeline_flags_from_stage(zink_shader_stage(stage));
   struct zink_descriptor_barrier key = {res, layout, flags, 0}, *t;

   uint32_t hash = barrier_hash(&key);
   struct set_entry *entry = _mesa_set_search_pre_hashed(ht, hash, &key);
   if (entry)
      t = (struct zink_descriptor_barrier*)entry->key;
   else {
      util_dynarray_append(barriers, struct zink_descriptor_barrier, key);
      t = util_dynarray_element(barriers, struct zink_descriptor_barrier,
                                util_dynarray_num_elements(barriers, struct zink_descriptor_barrier) - 1);
      t->stage = 0;
      t->layout = layout;
      t->res = res;
      t->access = flags;
      _mesa_set_add_pre_hashed(ht, hash, t);
   }
   t->stage |= pipeline;
}

static int
cmp_dynamic_offset_binding(const void *a, const void *b)
{
   const uint32_t *binding_a = a, *binding_b = b;
   return *binding_a - *binding_b;
}

static void
write_descriptors(struct zink_context *ctx, unsigned num_wds, VkWriteDescriptorSet *wds, bool cache_hit)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);

   if (!cache_hit && num_wds)
      vkUpdateDescriptorSets(screen->dev, num_wds, wds, 0, NULL);
}

static unsigned
init_write_descriptor(struct zink_shader *shader, struct zink_descriptor_set *zds, int idx, VkWriteDescriptorSet *wd, unsigned num_wds)
{
    wd->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    wd->pNext = NULL;
    wd->dstBinding = shader->bindings[zds->pool->type][idx].binding;
    wd->dstArrayElement = 0;
    wd->descriptorCount = shader->bindings[zds->pool->type][idx].size;
    wd->descriptorType = shader->bindings[zds->pool->type][idx].type;
    wd->dstSet = zds->desc_set;
    return num_wds + 1;
}

static void
update_ubo_descriptors(struct zink_context *ctx, struct zink_descriptor_set *zds,
                       bool is_compute, bool cache_hit, bool need_resource_refs,
                       uint32_t *dynamic_offsets, unsigned *dynamic_offset_idx)
{
   struct zink_program *pg = is_compute ? (struct zink_program *)ctx->curr_compute : (struct zink_program *)ctx->curr_program;
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   unsigned num_descriptors = pg->pool[zds->pool->type]->key.num_descriptors;
   unsigned num_bindings = zds->pool->num_resources;
   VkWriteDescriptorSet wds[num_descriptors];
   VkDescriptorBufferInfo buffer_infos[num_bindings];
   unsigned num_wds = 0;
   unsigned num_buffer_info = 0;
   unsigned num_resources = 0;
   struct zink_shader **stages;
   struct {
      uint32_t binding;
      uint32_t offset;
   } dynamic_buffers[PIPE_MAX_CONSTANT_BUFFERS];
   unsigned dynamic_offset_count = 0;
   struct set *ht = NULL;
   if (!cache_hit) {
      ht = _mesa_set_create(NULL, barrier_hash, barrier_equals);
      _mesa_set_resize(ht, num_bindings);
   }

   unsigned num_stages = is_compute ? 1 : ZINK_SHADER_COUNT;
   if (is_compute)
      stages = &ctx->curr_compute->shader;
   else
      stages = &ctx->gfx_stages[0];

   for (int i = 0; i < num_stages; i++) {
      struct zink_shader *shader = stages[i];
      if (!shader)
         continue;
      enum pipe_shader_type stage = pipe_shader_type_from_mesa(shader->nir->info.stage);

      for (int j = 0; j < shader->num_bindings[zds->pool->type]; j++) {
         int index = shader->bindings[zds->pool->type][j].index;
         assert(shader->bindings[zds->pool->type][j].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
             shader->bindings[zds->pool->type][j].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC);
         assert(ctx->ubos[stage][index].buffer_size <= screen->info.props.limits.maxUniformBufferRange);
         struct zink_resource *res = zink_resource(ctx->ubos[stage][index].buffer);
         assert(!res || ctx->ubos[stage][index].buffer_size > 0);
         assert(!res || ctx->ubos[stage][index].buffer);
         assert(num_resources < num_bindings);
         desc_set_res_add(zds, res, num_resources++, cache_hit);
         assert(num_buffer_info < num_bindings);
         buffer_infos[num_buffer_info].buffer = res ? res->obj->buffer :
                                                (screen->info.rb2_feats.nullDescriptor ?
                                                 VK_NULL_HANDLE :
                                                 zink_resource(ctx->dummy_vertex_buffer)->obj->buffer);
         if (shader->bindings[zds->pool->type][j].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
            buffer_infos[num_buffer_info].offset = 0;
            /* we're storing this to qsort later */
            dynamic_buffers[dynamic_offset_count].binding = shader->bindings[zds->pool->type][j].binding;
            dynamic_buffers[dynamic_offset_count++].offset = res ? ctx->ubos[stage][index].buffer_offset : 0;
         } else
            buffer_infos[num_buffer_info].offset = res ? ctx->ubos[stage][index].buffer_offset : 0;
         buffer_infos[num_buffer_info].range = res ? ctx->ubos[stage][index].buffer_size : VK_WHOLE_SIZE;
         if (res && !cache_hit)
            add_barrier(res, 0, VK_ACCESS_UNIFORM_READ_BIT, stage, &zds->barriers, ht);
         wds[num_wds].pBufferInfo = buffer_infos + num_buffer_info;
         ++num_buffer_info;

         num_wds = init_write_descriptor(shader, zds, j, &wds[num_wds], num_wds);
      }
   }
   _mesa_set_destroy(ht, NULL);
   /* Values are taken from pDynamicOffsets in an order such that all entries for set N come before set N+1;
    * within a set, entries are ordered by the binding numbers in the descriptor set layouts
    * - vkCmdBindDescriptorSets spec
    *
    * because of this, we have to sort all the dynamic offsets by their associated binding to ensure they
    * match what the driver expects
    */
   if (dynamic_offset_count > 1)
      qsort(dynamic_buffers, dynamic_offset_count, sizeof(uint32_t) * 2, cmp_dynamic_offset_binding);
   for (int i = 0; i < dynamic_offset_count; i++)
      dynamic_offsets[i] = dynamic_buffers[i].offset;
   *dynamic_offset_idx = dynamic_offset_count;

   write_descriptors(ctx, num_wds, wds, cache_hit);
}

static void
update_ssbo_descriptors(struct zink_context *ctx, struct zink_descriptor_set *zds,
                        bool is_compute, bool cache_hit, bool need_resource_refs)
{
   struct zink_program *pg = is_compute ? (struct zink_program *)ctx->curr_compute : (struct zink_program *)ctx->curr_program;
   ASSERTED struct zink_screen *screen = zink_screen(ctx->base.screen);
   unsigned num_descriptors = pg->pool[zds->pool->type]->key.num_descriptors;
   unsigned num_bindings = zds->pool->num_resources;
   VkWriteDescriptorSet wds[num_descriptors];
   VkDescriptorBufferInfo buffer_infos[num_bindings];
   unsigned num_wds = 0;
   unsigned num_buffer_info = 0;
   unsigned num_resources = 0;
   struct zink_shader **stages;
   struct set *ht = NULL;
   if (!cache_hit) {
      ht = _mesa_set_create(NULL, barrier_hash, barrier_equals);
      _mesa_set_resize(ht, num_bindings);
   }

   unsigned num_stages = is_compute ? 1 : ZINK_SHADER_COUNT;
   if (is_compute)
      stages = &ctx->curr_compute->shader;
   else
      stages = &ctx->gfx_stages[0];

   for (int i = 0; (!cache_hit || need_resource_refs) && i < num_stages; i++) {
      struct zink_shader *shader = stages[i];
      if (!shader)
         continue;
      enum pipe_shader_type stage = pipe_shader_type_from_mesa(shader->nir->info.stage);

      for (int j = 0; j < shader->num_bindings[zds->pool->type]; j++) {
         int index = shader->bindings[zds->pool->type][j].index;
         assert(shader->bindings[zds->pool->type][j].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
         assert(num_resources < num_bindings);
         struct zink_resource *res = zink_resource(ctx->ssbos[stage][index].buffer);
         desc_set_res_add(zds, res, num_resources++, cache_hit);
         if (res) {
            assert(ctx->ssbos[stage][index].buffer_size > 0);
            assert(ctx->ssbos[stage][index].buffer_size <= screen->info.props.limits.maxStorageBufferRange);
            assert(num_buffer_info < num_bindings);
            unsigned flag = VK_ACCESS_SHADER_READ_BIT;
            if (ctx->writable_ssbos[stage] & (1 << index))
               flag |= VK_ACCESS_SHADER_WRITE_BIT;
            if (!cache_hit)
               add_barrier(res, 0, flag, stage, &zds->barriers, ht);
            buffer_infos[num_buffer_info].buffer = res->obj->buffer;
            buffer_infos[num_buffer_info].offset = ctx->ssbos[stage][index].buffer_offset;
            buffer_infos[num_buffer_info].range  = ctx->ssbos[stage][index].buffer_size;
         } else {
            assert(screen->info.rb2_feats.nullDescriptor);
            buffer_infos[num_buffer_info].buffer = VK_NULL_HANDLE;
            buffer_infos[num_buffer_info].offset = 0;
            buffer_infos[num_buffer_info].range  = VK_WHOLE_SIZE;
         }
         wds[num_wds].pBufferInfo = buffer_infos + num_buffer_info;
         ++num_buffer_info;

         num_wds = init_write_descriptor(shader, zds, j, &wds[num_wds], num_wds);
      }
   }
   _mesa_set_destroy(ht, NULL);
   write_descriptors(ctx, num_wds, wds, cache_hit);
}

static void
handle_image_descriptor(struct zink_screen *screen, struct zink_resource *res, enum zink_descriptor_type type, VkDescriptorType vktype, VkWriteDescriptorSet *wd,
                        VkImageLayout layout, unsigned *num_image_info, VkDescriptorImageInfo *image_info,
                        unsigned *num_buffer_info, VkBufferView *buffer_info,
                        struct zink_sampler_state *sampler,
                        VkImageView imageview, VkBufferView bufferview, bool do_set)
{
   if (!res) {
      /* if we're hitting this assert often, we can probably just throw a junk buffer in since
       * the results of this codepath are undefined in ARB_texture_buffer_object spec
       */
      assert(screen->info.rb2_feats.nullDescriptor);

      switch (vktype) {
      case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
         *buffer_info = VK_NULL_HANDLE;
         if (do_set)
            wd->pTexelBufferView = buffer_info;
         ++(*num_buffer_info);
         break;
      case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
         image_info->imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
         image_info->imageView = VK_NULL_HANDLE;
         image_info->sampler = sampler ? sampler->sampler : VK_NULL_HANDLE;
         if (do_set)
            wd->pImageInfo = image_info;
         ++(*num_image_info);
         break;
      default:
         unreachable("unknown descriptor type");
      }
   } else if (res->base.b.target != PIPE_BUFFER) {
      assert(layout != VK_IMAGE_LAYOUT_UNDEFINED);
      image_info->imageLayout = layout;
      image_info->imageView = imageview;
      image_info->sampler = sampler ? sampler->sampler : VK_NULL_HANDLE;
      if (do_set)
         wd->pImageInfo = image_info;
      ++(*num_image_info);
   } else {
      if (do_set)
         wd->pTexelBufferView = buffer_info;
      *buffer_info = bufferview;
      ++(*num_buffer_info);
   }
}

static void
update_sampler_descriptors(struct zink_context *ctx, struct zink_descriptor_set *zds,
                           bool is_compute, bool cache_hit, bool need_resource_refs)
{
   struct zink_program *pg = is_compute ? (struct zink_program *)ctx->curr_compute : (struct zink_program *)ctx->curr_program;
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   unsigned num_descriptors = pg->pool[zds->pool->type]->key.num_descriptors;
   unsigned num_bindings = zds->pool->num_resources;
   VkWriteDescriptorSet wds[num_descriptors];
   VkDescriptorImageInfo image_infos[num_bindings];
   VkBufferView buffer_views[num_bindings];
   unsigned num_wds = 0;
   unsigned num_image_info = 0;
   unsigned num_buffer_info = 0;
   unsigned num_resources = 0;
   struct zink_shader **stages;
   struct set *ht = NULL;
   if (!cache_hit) {
      ht = _mesa_set_create(NULL, barrier_hash, barrier_equals);
      _mesa_set_resize(ht, num_bindings);
   }

   unsigned num_stages = is_compute ? 1 : ZINK_SHADER_COUNT;
   if (is_compute)
      stages = &ctx->curr_compute->shader;
   else
      stages = &ctx->gfx_stages[0];

   for (int i = 0; (!cache_hit || need_resource_refs) && i < num_stages; i++) {
      struct zink_shader *shader = stages[i];
      if (!shader)
         continue;
      enum pipe_shader_type stage = pipe_shader_type_from_mesa(shader->nir->info.stage);

      for (int j = 0; j < shader->num_bindings[zds->pool->type]; j++) {
         int index = shader->bindings[zds->pool->type][j].index;
         assert(shader->bindings[zds->pool->type][j].type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ||
                shader->bindings[zds->pool->type][j].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);

         for (unsigned k = 0; k < shader->bindings[zds->pool->type][j].size; k++) {
            VkImageView imageview = VK_NULL_HANDLE;
            VkBufferView bufferview = VK_NULL_HANDLE;
            struct zink_resource *res = NULL;
            VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
            struct zink_sampler_state *sampler = NULL;

            struct pipe_sampler_view *psampler_view = ctx->sampler_views[stage][index + k];
            struct zink_sampler_view *sampler_view = zink_sampler_view(psampler_view);
            res = psampler_view ? zink_resource(psampler_view->texture) : NULL;
            if (res && res->base.b.target == PIPE_BUFFER) {
               bufferview = sampler_view->buffer_view->buffer_view;
            } else if (res) {
               imageview = sampler_view->image_view->image_view;
               layout = (res->bind_history & BITFIELD64_BIT(ZINK_DESCRIPTOR_TYPE_IMAGE)) ?
                        VK_IMAGE_LAYOUT_GENERAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
               sampler = ctx->sampler_states[stage][index + k];
            }
            assert(num_resources < num_bindings);
            if (res) {
               if (!cache_hit)
                  add_barrier(res, layout, VK_ACCESS_SHADER_READ_BIT, stage, &zds->barriers, ht);
            }
            assert(num_image_info < num_bindings);
            handle_image_descriptor(screen, res, zds->pool->type, shader->bindings[zds->pool->type][j].type,
                                    &wds[num_wds], layout, &num_image_info, &image_infos[num_image_info],
                                    &num_buffer_info, &buffer_views[num_buffer_info],
                                    sampler, imageview, bufferview, !k);
            desc_set_sampler_add(ctx, zds, sampler_view, sampler, num_resources++,
                                 zink_shader_descriptor_is_buffer(shader, ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW, j),
                                 cache_hit);
            struct zink_batch *batch = &ctx->batch;
            if (sampler_view)
               zink_batch_reference_sampler_view(batch, sampler_view);
            if (sampler)
               /* this only tracks the most recent usage for now */
               zink_batch_usage_set(&sampler->batch_uses, batch->state->fence.batch_id);
         }
         assert(num_wds < num_descriptors);

         num_wds = init_write_descriptor(shader, zds, j, &wds[num_wds], num_wds);
      }
   }
   _mesa_set_destroy(ht, NULL);
   write_descriptors(ctx, num_wds, wds, cache_hit);
}

static void
update_image_descriptors(struct zink_context *ctx, struct zink_descriptor_set *zds,
                         bool is_compute, bool cache_hit, bool need_resource_refs)
{
   struct zink_program *pg = is_compute ? (struct zink_program *)ctx->curr_compute : (struct zink_program *)ctx->curr_program;
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   unsigned num_descriptors = pg->pool[zds->pool->type]->key.num_descriptors;
   unsigned num_bindings = zds->pool->num_resources;
   VkWriteDescriptorSet wds[num_descriptors];
   VkDescriptorImageInfo image_infos[num_bindings];
   VkBufferView buffer_views[num_bindings];
   unsigned num_wds = 0;
   unsigned num_image_info = 0;
   unsigned num_buffer_info = 0;
   unsigned num_resources = 0;
   struct zink_shader **stages;
   struct set *ht = NULL;
   if (!cache_hit) {
      ht = _mesa_set_create(NULL, barrier_hash, barrier_equals);
      _mesa_set_resize(ht, num_bindings);
   }

   unsigned num_stages = is_compute ? 1 : ZINK_SHADER_COUNT;
   if (is_compute)
      stages = &ctx->curr_compute->shader;
   else
      stages = &ctx->gfx_stages[0];

   for (int i = 0; (!cache_hit || need_resource_refs) && i < num_stages; i++) {
      struct zink_shader *shader = stages[i];
      if (!shader)
         continue;
      enum pipe_shader_type stage = pipe_shader_type_from_mesa(shader->nir->info.stage);

      for (int j = 0; j < shader->num_bindings[zds->pool->type]; j++) {
         int index = shader->bindings[zds->pool->type][j].index;
         assert(shader->bindings[zds->pool->type][j].type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER ||
                shader->bindings[zds->pool->type][j].type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);

         for (unsigned k = 0; k < shader->bindings[zds->pool->type][j].size; k++) {
            VkImageView imageview = VK_NULL_HANDLE;
            VkBufferView bufferview = VK_NULL_HANDLE;
            struct zink_resource *res = NULL;
            VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
            struct zink_image_view *image_view = &ctx->image_views[stage][index + k];
            assert(image_view);
            res = zink_resource(image_view->base.resource);

            if (res && image_view->base.resource->target == PIPE_BUFFER) {
               bufferview = image_view->buffer_view->buffer_view;
            } else if (res) {
               imageview = image_view->surface->image_view;
               layout = VK_IMAGE_LAYOUT_GENERAL;
            }
            assert(num_resources < num_bindings);
            desc_set_image_add(ctx, zds, image_view, num_resources++,
                               zink_shader_descriptor_is_buffer(shader, ZINK_DESCRIPTOR_TYPE_IMAGE, j),
                               cache_hit);
            if (res) {
               VkAccessFlags flags = 0;
               if (image_view->base.access & PIPE_IMAGE_ACCESS_READ)
                  flags |= VK_ACCESS_SHADER_READ_BIT;
               if (image_view->base.access & PIPE_IMAGE_ACCESS_WRITE)
                  flags |= VK_ACCESS_SHADER_WRITE_BIT;
               if (!cache_hit)
                  add_barrier(res, layout, flags, stage, &zds->barriers, ht);
            }

            assert(num_image_info < num_bindings);
            handle_image_descriptor(screen, res, zds->pool->type, shader->bindings[zds->pool->type][j].type,
                                    &wds[num_wds], layout, &num_image_info, &image_infos[num_image_info],
                                    &num_buffer_info, &buffer_views[num_buffer_info],
                                    NULL, imageview, bufferview, !k);

            struct zink_batch *batch = &ctx->batch;
            if (res)
               zink_batch_reference_image_view(batch, image_view);
         }
         assert(num_wds < num_descriptors);

         num_wds = init_write_descriptor(shader, zds, j, &wds[num_wds], num_wds);
      }
   }
   _mesa_set_destroy(ht, NULL);
   write_descriptors(ctx, num_wds, wds, cache_hit);
}

void
zink_descriptors_update(struct zink_context *ctx, struct zink_screen *screen, bool is_compute)
{
   struct zink_program *pg = is_compute ? (struct zink_program *)ctx->curr_compute : (struct zink_program *)ctx->curr_program;

   zink_context_update_descriptor_states(ctx, is_compute);
   bool cache_hit[ZINK_DESCRIPTOR_TYPES];
   bool need_resource_refs[ZINK_DESCRIPTOR_TYPES];
   struct zink_descriptor_set *zds[ZINK_DESCRIPTOR_TYPES];
   for (int h = 0; h < ZINK_DESCRIPTOR_TYPES; h++) {
      if (pg->pool[h])
         zds[h] = zink_descriptor_set_get(ctx, h, is_compute, &cache_hit[h], &need_resource_refs[h]);
      else
         zds[h] = NULL;
   }
   struct zink_batch *batch = &ctx->batch;
   zink_batch_reference_program(batch, pg);

   uint32_t dynamic_offsets[PIPE_MAX_CONSTANT_BUFFERS];
   unsigned dynamic_offset_idx = 0;

   if (zds[ZINK_DESCRIPTOR_TYPE_UBO])
      update_ubo_descriptors(ctx, zds[ZINK_DESCRIPTOR_TYPE_UBO],
                                           is_compute, cache_hit[ZINK_DESCRIPTOR_TYPE_UBO],
                                           need_resource_refs[ZINK_DESCRIPTOR_TYPE_UBO], dynamic_offsets, &dynamic_offset_idx);
   if (zds[ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW])
      update_sampler_descriptors(ctx, zds[ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW],
                                               is_compute, cache_hit[ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW],
                                               need_resource_refs[ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW]);
   if (zds[ZINK_DESCRIPTOR_TYPE_SSBO])
      update_ssbo_descriptors(ctx, zds[ZINK_DESCRIPTOR_TYPE_SSBO],
                                               is_compute, cache_hit[ZINK_DESCRIPTOR_TYPE_SSBO],
                                               need_resource_refs[ZINK_DESCRIPTOR_TYPE_SSBO]);
   if (zds[ZINK_DESCRIPTOR_TYPE_IMAGE])
      update_image_descriptors(ctx, zds[ZINK_DESCRIPTOR_TYPE_IMAGE],
                                               is_compute, cache_hit[ZINK_DESCRIPTOR_TYPE_IMAGE],
                                               need_resource_refs[ZINK_DESCRIPTOR_TYPE_IMAGE]);

   for (int h = 0; h < ZINK_DESCRIPTOR_TYPES && zds[h]; h++) {
      /* skip null descriptor sets since they have no resources */
      if (!zds[h]->hash)
         continue;
      assert(zds[h]->desc_set);
      util_dynarray_foreach(&zds[h]->barriers, struct zink_descriptor_barrier, barrier) {
         if (need_resource_refs[h])
            zink_batch_reference_resource_rw(batch, barrier->res, zink_resource_access_is_write(barrier->access));
         zink_resource_barrier(ctx, NULL, barrier->res,
                               barrier->layout, barrier->access, barrier->stage);
      }
   }

   for (unsigned h = 0; h < ZINK_DESCRIPTOR_TYPES; h++) {
      if (zds[h]) {
         vkCmdBindDescriptorSets(batch->state->cmdbuf, is_compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 pg->layout, zds[h]->pool->type, 1, &zds[h]->desc_set,
                                 zds[h]->pool->type == ZINK_DESCRIPTOR_TYPE_UBO ? dynamic_offset_idx : 0, dynamic_offsets);
      }
   }
}

struct zink_resource *
zink_get_resource_for_descriptor(struct zink_context *ctx, enum zink_descriptor_type type, enum pipe_shader_type shader, int idx)
{
   switch (type) {
   case ZINK_DESCRIPTOR_TYPE_UBO:
      return zink_resource(ctx->ubos[shader][idx].buffer);
   case ZINK_DESCRIPTOR_TYPE_SSBO:
      return zink_resource(ctx->ssbos[shader][idx].buffer);
   case ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW:
      return ctx->sampler_views[shader][idx] ? zink_resource(ctx->sampler_views[shader][idx]->texture) : NULL;
   case ZINK_DESCRIPTOR_TYPE_IMAGE:
      return zink_resource(ctx->image_views[shader][idx].base.resource);
   default:
      break;
   }
   unreachable("unknown descriptor type!");
   return NULL;
}

static uint32_t
calc_descriptor_state_hash_ubo(struct zink_context *ctx, struct zink_shader *zs, enum pipe_shader_type shader, int i, int idx, uint32_t hash)
{
   struct zink_resource *res = zink_get_resource_for_descriptor(ctx, ZINK_DESCRIPTOR_TYPE_UBO, shader, idx);
   struct zink_resource_object *obj = res ? res->obj : NULL;
   hash = XXH32(&obj, sizeof(void*), hash);
   void *hash_data = &ctx->ubos[shader][idx].buffer_size;
   size_t data_size = sizeof(unsigned);
   hash = XXH32(hash_data, data_size, hash);
   if (zs->bindings[ZINK_DESCRIPTOR_TYPE_UBO][i].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER)
      hash = XXH32(&ctx->ubos[shader][idx].buffer_offset, sizeof(unsigned), hash);
   return hash;
}

static uint32_t
calc_descriptor_state_hash_ssbo(struct zink_context *ctx, struct zink_shader *zs, enum pipe_shader_type shader, int i, int idx, uint32_t hash)
{
   struct zink_resource *res = zink_get_resource_for_descriptor(ctx, ZINK_DESCRIPTOR_TYPE_SSBO, shader, idx);
   struct zink_resource_object *obj = res ? res->obj : NULL;
   hash = XXH32(&obj, sizeof(void*), hash);
   if (obj) {
      struct pipe_shader_buffer *ssbo = &ctx->ssbos[shader][idx];
      hash = XXH32(&ssbo->buffer_offset, sizeof(ssbo->buffer_offset), hash);
      hash = XXH32(&ssbo->buffer_size, sizeof(ssbo->buffer_size), hash);
   }
   return hash;
}

static inline uint32_t
get_sampler_view_hash(const struct zink_sampler_view *sampler_view)
{
   if (!sampler_view)
      return 0;
   return sampler_view->base.target == PIPE_BUFFER ?
          sampler_view->buffer_view->hash : sampler_view->image_view->hash;
}

static inline uint32_t
get_image_view_hash(const struct zink_image_view *image_view)
{
   if (!image_view || !image_view->base.resource)
      return 0;
   return image_view->base.resource->target == PIPE_BUFFER ?
          image_view->buffer_view->hash : image_view->surface->hash;
}

uint32_t
zink_get_sampler_view_hash(struct zink_context *ctx, struct zink_sampler_view *sampler_view, bool is_buffer)
{
   return get_sampler_view_hash(sampler_view) ? get_sampler_view_hash(sampler_view) :
          (is_buffer ? zink_screen(ctx->base.screen)->null_descriptor_hashes.buffer_view :
                       zink_screen(ctx->base.screen)->null_descriptor_hashes.image_view);
}

uint32_t
zink_get_image_view_hash(struct zink_context *ctx, struct zink_image_view *image_view, bool is_buffer)
{
   return get_image_view_hash(image_view) ? get_image_view_hash(image_view) :
          (is_buffer ? zink_screen(ctx->base.screen)->null_descriptor_hashes.buffer_view :
                       zink_screen(ctx->base.screen)->null_descriptor_hashes.image_view);
}

static uint32_t
calc_descriptor_state_hash_sampler(struct zink_context *ctx, struct zink_shader *zs, enum pipe_shader_type shader, int i, int idx, uint32_t hash)
{
   for (unsigned k = 0; k < zs->bindings[ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW][i].size; k++) {
      struct zink_sampler_view *sampler_view = zink_sampler_view(ctx->sampler_views[shader][idx + k]);
      bool is_buffer = zink_shader_descriptor_is_buffer(zs, ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW, i);
      uint32_t val = zink_get_sampler_view_hash(ctx, sampler_view, is_buffer);
      hash = XXH32(&val, sizeof(uint32_t), hash);
      if (is_buffer)
         continue;

      struct zink_sampler_state *sampler_state = ctx->sampler_states[shader][idx + k];

      if (sampler_state)
         hash = XXH32(&sampler_state->hash, sizeof(uint32_t), hash);
   }
   return hash;
}

static uint32_t
calc_descriptor_state_hash_image(struct zink_context *ctx, struct zink_shader *zs, enum pipe_shader_type shader, int i, int idx, uint32_t hash)
{
   for (unsigned k = 0; k < zs->bindings[ZINK_DESCRIPTOR_TYPE_IMAGE][i].size; k++) {
      uint32_t val = zink_get_image_view_hash(ctx, &ctx->image_views[shader][idx + k],
                                     zink_shader_descriptor_is_buffer(zs, ZINK_DESCRIPTOR_TYPE_IMAGE, i));
      hash = XXH32(&val, sizeof(uint32_t), hash);
   }
   return hash;
}

static uint32_t
update_descriptor_stage_state(struct zink_context *ctx, enum pipe_shader_type shader, enum zink_descriptor_type type)
{
   struct zink_shader *zs = shader == PIPE_SHADER_COMPUTE ? ctx->compute_stage : ctx->gfx_stages[shader];

   uint32_t hash = 0;
   for (int i = 0; i < zs->num_bindings[type]; i++) {
      int idx = zs->bindings[type][i].index;
      switch (type) {
      case ZINK_DESCRIPTOR_TYPE_UBO:
         hash = calc_descriptor_state_hash_ubo(ctx, zs, shader, i, idx, hash);
         break;
      case ZINK_DESCRIPTOR_TYPE_SSBO:
         hash = calc_descriptor_state_hash_ssbo(ctx, zs, shader, i, idx, hash);
         break;
      case ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW:
         hash = calc_descriptor_state_hash_sampler(ctx, zs, shader, i, idx, hash);
         break;
      case ZINK_DESCRIPTOR_TYPE_IMAGE:
         hash = calc_descriptor_state_hash_image(ctx, zs, shader, i, idx, hash);
         break;
      default:
         unreachable("unknown descriptor type");
      }
   }
   return hash;
}

static void
update_descriptor_state(struct zink_context *ctx, enum zink_descriptor_type type, bool is_compute)
{
   /* we shouldn't be calling this if we don't have to */
   assert(!ctx->descriptor_states[is_compute].valid[type]);
   bool has_any_usage = false;

   if (is_compute) {
      /* just update compute state */
      bool has_usage = zink_program_get_descriptor_usage(ctx, PIPE_SHADER_COMPUTE, type);
      if (has_usage)
         ctx->descriptor_states[is_compute].state[type] = update_descriptor_stage_state(ctx, PIPE_SHADER_COMPUTE, type);
      else
         ctx->descriptor_states[is_compute].state[type] = 0;
      has_any_usage = has_usage;
   } else {
      /* update all gfx states */
      bool first = true;
      for (unsigned i = 0; i < ZINK_SHADER_COUNT; i++) {
         bool has_usage = false;
         /* this is the incremental update for the shader stage */
         if (!ctx->gfx_descriptor_states[i].valid[type]) {
            ctx->gfx_descriptor_states[i].state[type] = 0;
            if (ctx->gfx_stages[i]) {
               has_usage = zink_program_get_descriptor_usage(ctx, i, type);
               if (has_usage)
                  ctx->gfx_descriptor_states[i].state[type] = update_descriptor_stage_state(ctx, i, type);
               ctx->gfx_descriptor_states[i].valid[type] = has_usage;
            }
         }
         if (ctx->gfx_descriptor_states[i].valid[type]) {
            /* this is the overall state update for the descriptor set hash */
            if (first) {
               /* no need to double hash the first state */
               ctx->descriptor_states[is_compute].state[type] = ctx->gfx_descriptor_states[i].state[type];
               first = false;
            } else {
               ctx->descriptor_states[is_compute].state[type] = XXH32(&ctx->gfx_descriptor_states[i].state[type],
                                                                      sizeof(uint32_t),
                                                                      ctx->descriptor_states[is_compute].state[type]);
            }
         }
         has_any_usage |= has_usage;
      }
   }
   ctx->descriptor_states[is_compute].valid[type] = has_any_usage;
}

void
zink_context_update_descriptor_states(struct zink_context *ctx, bool is_compute)
{
   for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPES; i++) {
      if (!ctx->descriptor_states[is_compute].valid[i])
         update_descriptor_state(ctx, i, is_compute);
   }
}

void
zink_context_invalidate_descriptor_state(struct zink_context *ctx, enum pipe_shader_type shader, enum zink_descriptor_type type)
{
   if (shader != PIPE_SHADER_COMPUTE) {
      ctx->gfx_descriptor_states[shader].valid[type] = false;
      ctx->gfx_descriptor_states[shader].state[type] = 0;
   }
   ctx->descriptor_states[shader == PIPE_SHADER_COMPUTE].valid[type] = false;
   ctx->descriptor_states[shader == PIPE_SHADER_COMPUTE].state[type] = 0;
}
