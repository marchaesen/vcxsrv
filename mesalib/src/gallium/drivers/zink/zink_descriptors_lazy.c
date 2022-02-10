/*
 * Copyright © 2021 Valve Corporation
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
#include "zink_compiler.h"
#include "zink_descriptors.h"
#include "zink_program.h"
#include "zink_resource.h"
#include "zink_screen.h"

#define MAX_LAZY_DESCRIPTORS (ZINK_DEFAULT_MAX_DESCS / 10)

struct zink_descriptor_data_lazy {
   struct zink_descriptor_data base;
   VkDescriptorUpdateTemplateEntry push_entries[PIPE_SHADER_TYPES]; //gfx+fbfetch
   VkDescriptorUpdateTemplateEntry compute_push_entry;
   bool push_state_changed[2]; //gfx, compute
   uint8_t state_changed[2]; //gfx, compute
};

struct zink_descriptor_pool {
   VkDescriptorPool pool;
   VkDescriptorSet sets[MAX_LAZY_DESCRIPTORS];
   unsigned set_idx;
   unsigned sets_alloc;
};

struct zink_batch_descriptor_data_lazy {
   struct zink_batch_descriptor_data base;
   struct util_dynarray overflowed_pools;
   struct hash_table pools[ZINK_DESCRIPTOR_TYPES];
   struct zink_descriptor_pool *push_pool[2];
   struct zink_program *pg[2]; //gfx, compute
   uint32_t compat_id[2];
   VkDescriptorSetLayout dsl[2][ZINK_DESCRIPTOR_TYPES];
   VkDescriptorSet sets[2][ZINK_DESCRIPTOR_TYPES + 1];
   unsigned push_usage[2];
   bool has_fbfetch;
};

ALWAYS_INLINE static struct zink_descriptor_data_lazy *
dd_lazy(struct zink_context *ctx)
{
   return (struct zink_descriptor_data_lazy*)ctx->dd;
}

ALWAYS_INLINE static struct zink_batch_descriptor_data_lazy *
bdd_lazy(struct zink_batch_state *bs)
{
   return (struct zink_batch_descriptor_data_lazy*)bs->dd;
}

static void
init_template_entry(struct zink_shader *shader, enum zink_descriptor_type type,
                    unsigned idx, unsigned offset, VkDescriptorUpdateTemplateEntry *entry, unsigned *entry_idx, bool flatten_dynamic)
{
    int index = shader->bindings[type][idx].index;
    enum pipe_shader_type stage = pipe_shader_type_from_mesa(shader->nir->info.stage);
    entry->dstArrayElement = 0;
    entry->dstBinding = shader->bindings[type][idx].binding;
    if (shader->bindings[type][idx].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC && flatten_dynamic)
       /* filter out DYNAMIC type here */
       entry->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    else
       entry->descriptorType = shader->bindings[type][idx].type;
    switch (shader->bindings[type][idx].type) {
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
       entry->descriptorCount = 1;
       entry->offset = offsetof(struct zink_context, di.ubos[stage][index + offset]);
       entry->stride = sizeof(VkDescriptorBufferInfo);
       break;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
       entry->descriptorCount = shader->bindings[type][idx].size;
       entry->offset = offsetof(struct zink_context, di.textures[stage][index + offset]);
       entry->stride = sizeof(VkDescriptorImageInfo);
       break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
       entry->descriptorCount = shader->bindings[type][idx].size;
       entry->offset = offsetof(struct zink_context, di.tbos[stage][index + offset]);
       entry->stride = sizeof(VkBufferView);
       break;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
       entry->descriptorCount = 1;
       entry->offset = offsetof(struct zink_context, di.ssbos[stage][index + offset]);
       entry->stride = sizeof(VkDescriptorBufferInfo);
       break;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
       entry->descriptorCount = shader->bindings[type][idx].size;
       entry->offset = offsetof(struct zink_context, di.images[stage][index + offset]);
       entry->stride = sizeof(VkDescriptorImageInfo);
       break;
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
       entry->descriptorCount = shader->bindings[type][idx].size;
       entry->offset = offsetof(struct zink_context, di.texel_images[stage][index + offset]);
       entry->stride = sizeof(VkBufferView);
       break;
    default:
       unreachable("unknown type");
    }
    (*entry_idx)++;
}

static uint16_t
descriptor_program_num_sizes(VkDescriptorPoolSize *sizes, enum zink_descriptor_type type)
{
   switch (type) {
   case ZINK_DESCRIPTOR_TYPE_UBO:
      return 1;
   case ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW:
      return !!sizes[ZDS_INDEX_COMBINED_SAMPLER].descriptorCount +
             !!sizes[ZDS_INDEX_UNIFORM_TEXELS].descriptorCount;
   case ZINK_DESCRIPTOR_TYPE_SSBO:
      return 1;
   case ZINK_DESCRIPTOR_TYPE_IMAGE:
      return !!sizes[ZDS_INDEX_STORAGE_IMAGE].descriptorCount +
             !!sizes[ZDS_INDEX_STORAGE_TEXELS].descriptorCount;
   default: break;
   }
   unreachable("unknown type");
}

bool
zink_descriptor_program_init_lazy(struct zink_context *ctx, struct zink_program *pg)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   VkDescriptorSetLayoutBinding bindings[ZINK_DESCRIPTOR_TYPES][PIPE_SHADER_TYPES * 32];
   VkDescriptorUpdateTemplateEntry entries[ZINK_DESCRIPTOR_TYPES][PIPE_SHADER_TYPES * 32];
   unsigned num_bindings[ZINK_DESCRIPTOR_TYPES] = {0};
   uint8_t has_bindings = 0;
   unsigned push_count = 0;
   uint16_t num_type_sizes[ZINK_DESCRIPTOR_TYPES];
   VkDescriptorPoolSize sizes[6] = {0}; //zink_descriptor_size_index

   struct zink_shader **stages;
   if (pg->is_compute)
      stages = &((struct zink_compute_program*)pg)->shader;
   else
      stages = ((struct zink_gfx_program*)pg)->shaders;

   if (!pg->dd)
      pg->dd = (void*)rzalloc(pg, struct zink_program_descriptor_data);
   if (!pg->dd)
      return false;

   if (!pg->is_compute && stages[PIPE_SHADER_FRAGMENT]->nir->info.fs.uses_fbfetch_output) {
      zink_descriptor_util_init_fbfetch(ctx);
      push_count = 1;
      pg->dd->fbfetch = true;
   }

   unsigned entry_idx[ZINK_DESCRIPTOR_TYPES] = {0};

   unsigned num_shaders = pg->is_compute ? 1 : ZINK_SHADER_COUNT;
   bool have_push = screen->info.have_KHR_push_descriptor;
   for (int i = 0; i < num_shaders; i++) {
      struct zink_shader *shader = stages[i];
      if (!shader)
         continue;

      enum pipe_shader_type stage = pipe_shader_type_from_mesa(shader->nir->info.stage);
      VkShaderStageFlagBits stage_flags = zink_shader_stage(stage);
      for (int j = 0; j < ZINK_DESCRIPTOR_TYPES; j++) {
         for (int k = 0; k < shader->num_bindings[j]; k++) {
            /* dynamic ubos handled in push */
            if (shader->bindings[j][k].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
               pg->dd->push_usage |= BITFIELD64_BIT(stage);

               push_count++;
               continue;
            }

            assert(num_bindings[j] < ARRAY_SIZE(bindings[j]));
            VkDescriptorSetLayoutBinding *binding = &bindings[j][num_bindings[j]];
            binding->binding = shader->bindings[j][k].binding;
            binding->descriptorType = shader->bindings[j][k].type;
            binding->descriptorCount = shader->bindings[j][k].size;
            binding->stageFlags = stage_flags;
            binding->pImmutableSamplers = NULL;

            enum zink_descriptor_size_index idx = zink_vktype_to_size_idx(shader->bindings[j][k].type);
            sizes[idx].descriptorCount += shader->bindings[j][k].size;
            sizes[idx].type = shader->bindings[j][k].type;
            switch (shader->bindings[j][k].type) {
            case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
            case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
            case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
               init_template_entry(shader, j, k, 0, &entries[j][entry_idx[j]], &entry_idx[j], screen->descriptor_mode == ZINK_DESCRIPTOR_MODE_LAZY);
               break;
            case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
            case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
               for (unsigned l = 0; l < shader->bindings[j][k].size; l++)
                  init_template_entry(shader, j, k, l, &entries[j][entry_idx[j]], &entry_idx[j], screen->descriptor_mode == ZINK_DESCRIPTOR_MODE_LAZY);
               break;
            default:
               break;
            }
            num_bindings[j]++;
            has_bindings |= BITFIELD_BIT(j);
         }
         num_type_sizes[j] = descriptor_program_num_sizes(sizes, j);
      }
      pg->dd->bindless |= shader->bindless;
   }
   if (pg->dd->bindless)
      zink_descriptors_init_bindless(ctx);
   pg->dd->binding_usage = has_bindings;
   if (!has_bindings && !push_count) {
      ralloc_free(pg->dd);
      pg->dd = NULL;

      pg->layout = zink_pipeline_layout_create(screen, pg, &pg->compat_id);
      return !!pg->layout;
   }

   pg->dsl[pg->num_dsl++] = push_count ? ctx->dd->push_dsl[pg->is_compute]->layout : ctx->dd->dummy_dsl->layout;
   if (has_bindings) {
      for (unsigned i = 0; i < ARRAY_SIZE(sizes); i++)
         sizes[i].descriptorCount *= screen->descriptor_mode == ZINK_DESCRIPTOR_MODE_LAZY ? MAX_LAZY_DESCRIPTORS : ZINK_DEFAULT_MAX_DESCS;
      u_foreach_bit(type, has_bindings) {
         for (unsigned i = 0; i < type; i++) {
            /* push set is always 0 */
            if (!pg->dsl[i + 1]) {
               /* inject a null dsl */
               pg->dsl[pg->num_dsl++] = ctx->dd->dummy_dsl->layout;
               pg->dd->binding_usage |= BITFIELD_BIT(i);
            }
         }
         struct zink_descriptor_layout_key *key;
         pg->dd->layouts[pg->num_dsl] = zink_descriptor_util_layout_get(ctx, type, bindings[type], num_bindings[type], &key);
         enum zink_descriptor_size_index idx = zink_descriptor_type_to_size_idx(type);
         VkDescriptorPoolSize *sz = &sizes[idx];
         if (!sz->descriptorCount)
            sz++;
         pg->dd->pool_key[type] = zink_descriptor_util_pool_key_get(ctx, type, key, sz, num_type_sizes[type]);
         pg->dd->pool_key[type]->use_count++;
         pg->dsl[pg->num_dsl] = pg->dd->layouts[pg->num_dsl]->layout;
         pg->num_dsl++;
      }
   }
   /* TODO: make this dynamic? */
   if (pg->dd->bindless) {
      pg->num_dsl = ZINK_DESCRIPTOR_BINDLESS + 1;
      pg->dsl[ZINK_DESCRIPTOR_BINDLESS] = ctx->dd->bindless_layout;
      for (unsigned i = 0; i < ZINK_DESCRIPTOR_BINDLESS; i++) {
         if (!pg->dsl[i]) {
            /* inject a null dsl */
            pg->dsl[i] = ctx->dd->dummy_dsl->layout;
            if (i != ZINK_DESCRIPTOR_TYPES)
               pg->dd->binding_usage |= BITFIELD_BIT(i);
         }
      }
   }

   pg->layout = zink_pipeline_layout_create(screen, pg, &pg->compat_id);
   if (!pg->layout)
      return false;
   if (!screen->info.have_KHR_descriptor_update_template || screen->descriptor_mode == ZINK_DESCRIPTOR_MODE_NOTEMPLATES)
      return true;

   VkDescriptorUpdateTemplateCreateInfo template[ZINK_DESCRIPTOR_TYPES + 1] = {0};
   /* type of template */
   VkDescriptorUpdateTemplateType types[ZINK_DESCRIPTOR_TYPES + 1] = {VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET};
   if (have_push && screen->descriptor_mode == ZINK_DESCRIPTOR_MODE_LAZY)
      types[0] = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR;

   /* number of descriptors in template */
   unsigned wd_count[ZINK_DESCRIPTOR_TYPES + 1];
   if (push_count)
      wd_count[0] = pg->is_compute ? 1 : (ZINK_SHADER_COUNT + !!ctx->dd->has_fbfetch);
   for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPES; i++)
      wd_count[i + 1] = pg->dd->pool_key[i] ? pg->dd->pool_key[i]->layout->num_bindings : 0;

   VkDescriptorUpdateTemplateEntry *push_entries[2] = {
      dd_lazy(ctx)->push_entries,
      &dd_lazy(ctx)->compute_push_entry,
   };
   for (unsigned i = 0; i < pg->num_dsl; i++) {
      bool is_push = i == 0;
      /* no need for empty templates */
      if (pg->dsl[i] == ctx->dd->dummy_dsl->layout ||
          pg->dsl[i] == ctx->dd->bindless_layout ||
          (!is_push && pg->dd->layouts[i]->desc_template))
         continue;
      template[i].sType = VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO;
      assert(wd_count[i]);
      template[i].descriptorUpdateEntryCount = wd_count[i];
      if (is_push)
         template[i].pDescriptorUpdateEntries = push_entries[pg->is_compute];
      else
         template[i].pDescriptorUpdateEntries = entries[i - 1];
      template[i].templateType = types[i];
      template[i].descriptorSetLayout = pg->dsl[i];
      template[i].pipelineBindPoint = pg->is_compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
      template[i].pipelineLayout = pg->layout;
      template[i].set = i;
      VkDescriptorUpdateTemplateKHR t;
      if (VKSCR(CreateDescriptorUpdateTemplate)(screen->dev, &template[i], NULL, &t) != VK_SUCCESS)
         return false;
      if (is_push)
         pg->dd->push_template = t;
      else
         pg->dd->layouts[i]->desc_template = t;
   }
   return true;
}

void
zink_descriptor_program_deinit_lazy(struct zink_context *ctx, struct zink_program *pg)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   for (unsigned i = 0; pg->num_dsl && i < ZINK_DESCRIPTOR_TYPES; i++) {
      if (pg->dd->pool_key[i])
         pg->dd->pool_key[i]->use_count--;
   }
   if (pg->dd && pg->dd->push_template)
      VKSCR(DestroyDescriptorUpdateTemplate)(screen->dev, pg->dd->push_template, NULL);
   ralloc_free(pg->dd);
}

static VkDescriptorPool
create_pool(struct zink_screen *screen, unsigned num_type_sizes, const VkDescriptorPoolSize *sizes, unsigned flags)
{
   VkDescriptorPool pool;
   VkDescriptorPoolCreateInfo dpci = {0};
   dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
   dpci.pPoolSizes = sizes;
   dpci.poolSizeCount = num_type_sizes;
   dpci.flags = flags;
   dpci.maxSets = MAX_LAZY_DESCRIPTORS;
   if (VKSCR(CreateDescriptorPool)(screen->dev, &dpci, 0, &pool) != VK_SUCCESS) {
      debug_printf("vkCreateDescriptorPool failed\n");
      return VK_NULL_HANDLE;
   }
   return pool;
}

static struct zink_descriptor_pool *
get_descriptor_pool_lazy(struct zink_context *ctx, struct zink_program *pg, enum zink_descriptor_type type, struct zink_batch_descriptor_data_lazy *bdd, bool is_compute);

static struct zink_descriptor_pool *
check_pool_alloc(struct zink_context *ctx, struct zink_descriptor_pool *pool, struct hash_entry *he, struct zink_program *pg,
                 enum zink_descriptor_type type, struct zink_batch_descriptor_data_lazy *bdd, bool is_compute)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   /* allocate up to $current * 10, e.g., 10 -> 100 or 100 -> 1000 */
   if (pool->set_idx == pool->sets_alloc) {
      unsigned sets_to_alloc = MIN2(MIN2(MAX2(pool->sets_alloc * 10, 10), MAX_LAZY_DESCRIPTORS) - pool->sets_alloc, 100);
      if (!sets_to_alloc) {
         /* overflowed pool: queue for deletion on next reset */
         util_dynarray_append(&bdd->overflowed_pools, struct zink_descriptor_pool*, pool);
         _mesa_hash_table_remove(&bdd->pools[type], he);
         return get_descriptor_pool_lazy(ctx, pg, type, bdd, is_compute);
      }
      if (!zink_descriptor_util_alloc_sets(screen, pg->dsl[type + 1],
                                           pool->pool, &pool->sets[pool->sets_alloc], sets_to_alloc))
         return NULL;
      pool->sets_alloc += sets_to_alloc;
   }
   return pool;
}

static struct zink_descriptor_pool *
create_push_pool(struct zink_screen *screen, struct zink_batch_descriptor_data_lazy *bdd, bool is_compute, bool has_fbfetch)
{
   struct zink_descriptor_pool *pool = rzalloc(bdd, struct zink_descriptor_pool);
   VkDescriptorPoolSize sizes[2];
   sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
   if (is_compute)
      sizes[0].descriptorCount = MAX_LAZY_DESCRIPTORS;
   else {
      sizes[0].descriptorCount = ZINK_SHADER_COUNT * MAX_LAZY_DESCRIPTORS;
      sizes[1].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
      sizes[1].descriptorCount = MAX_LAZY_DESCRIPTORS;
   }
   pool->pool = create_pool(screen, !is_compute && has_fbfetch ? 2 : 1, sizes, 0);
   return pool;
}

static struct zink_descriptor_pool *
check_push_pool_alloc(struct zink_context *ctx, struct zink_descriptor_pool *pool, struct zink_batch_descriptor_data_lazy *bdd, bool is_compute)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   /* allocate up to $current * 10, e.g., 10 -> 100 or 100 -> 1000 */
   if (pool->set_idx == pool->sets_alloc || unlikely(ctx->dd->has_fbfetch != bdd->has_fbfetch)) {
      unsigned sets_to_alloc = MIN2(MIN2(MAX2(pool->sets_alloc * 10, 10), MAX_LAZY_DESCRIPTORS) - pool->sets_alloc, 100);
      if (!sets_to_alloc || unlikely(ctx->dd->has_fbfetch != bdd->has_fbfetch)) {
         /* overflowed pool: queue for deletion on next reset */
         util_dynarray_append(&bdd->overflowed_pools, struct zink_descriptor_pool*, pool);
         bdd->push_pool[is_compute] = create_push_pool(screen, bdd, is_compute, ctx->dd->has_fbfetch);
         bdd->has_fbfetch = ctx->dd->has_fbfetch;
         return check_push_pool_alloc(ctx, bdd->push_pool[is_compute], bdd, is_compute);
      }
      if (!zink_descriptor_util_alloc_sets(screen, ctx->dd->push_dsl[is_compute]->layout,
                                           pool->pool, &pool->sets[pool->sets_alloc], sets_to_alloc))
         return NULL;
      pool->sets_alloc += sets_to_alloc;
   }
   return pool;
}

static struct zink_descriptor_pool *
get_descriptor_pool_lazy(struct zink_context *ctx, struct zink_program *pg, enum zink_descriptor_type type, struct zink_batch_descriptor_data_lazy *bdd, bool is_compute)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   const struct zink_descriptor_pool_key *pool_key = pg->dd->pool_key[type];
   struct hash_entry *he = _mesa_hash_table_search(&bdd->pools[type], pool_key);
   struct zink_descriptor_pool *pool;
   if (he) {
      pool = he->data;
      return check_pool_alloc(ctx, pool, he, pg, type, bdd, is_compute);
   }
   pool = rzalloc(bdd, struct zink_descriptor_pool);
   if (!pool)
      return NULL;
   const unsigned num_type_sizes = pool_key->sizes[1].descriptorCount ? 2 : 1;
   pool->pool = create_pool(screen, num_type_sizes, pool_key->sizes, 0);
   if (!pool->pool) {
      ralloc_free(pool);
      return NULL;
   }
   _mesa_hash_table_insert(&bdd->pools[type], pool_key, pool);
   return check_pool_alloc(ctx, pool, he, pg, type, bdd, is_compute);
}

ALWAYS_INLINE static VkDescriptorSet
get_descriptor_set_lazy(struct zink_descriptor_pool *pool)
{
   if (!pool)
      return VK_NULL_HANDLE;

   assert(pool->set_idx < pool->sets_alloc);
   return pool->sets[pool->set_idx++];
}

static bool
populate_sets(struct zink_context *ctx, struct zink_batch_descriptor_data_lazy *bdd,
              struct zink_program *pg, uint8_t *changed_sets, VkDescriptorSet *sets)
{
   u_foreach_bit(type, *changed_sets) {
      if (pg->dd->pool_key[type]) {
         struct zink_descriptor_pool *pool = get_descriptor_pool_lazy(ctx, pg, type, bdd, pg->is_compute);
         sets[type] = get_descriptor_set_lazy(pool);
      } else
         sets[type] = ctx->dd->dummy_set;
      if (!sets[type])
         return false;
   }
   return true;
}

void
zink_descriptor_set_update_lazy(struct zink_context *ctx, struct zink_program *pg, enum zink_descriptor_type type, VkDescriptorSet set)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   VKCTX(UpdateDescriptorSetWithTemplate)(screen->dev, set, pg->dd->layouts[type + 1]->desc_template, ctx);
}

void
zink_descriptors_update_lazy_masked(struct zink_context *ctx, bool is_compute, uint8_t changed_sets, uint8_t bind_sets)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_batch_state *bs = ctx->batch.state;
   struct zink_batch_descriptor_data_lazy *bdd = bdd_lazy(bs);
   struct zink_program *pg = is_compute ? &ctx->curr_compute->base : &ctx->curr_program->base;
   VkDescriptorSet desc_sets[ZINK_DESCRIPTOR_TYPES];
   if (!pg->dd->binding_usage || (!changed_sets && !bind_sets))
      return;

   if (!populate_sets(ctx, bdd, pg, &changed_sets, desc_sets)) {
      debug_printf("ZINK: couldn't get descriptor sets!\n");
      return;
   }
   /* no flushing allowed */
   assert(ctx->batch.state == bs);

   u_foreach_bit(type, changed_sets) {
      assert(type + 1 < pg->num_dsl);
      if (pg->dd->pool_key[type]) {
         VKSCR(UpdateDescriptorSetWithTemplate)(screen->dev, desc_sets[type], pg->dd->layouts[type + 1]->desc_template, ctx);
         VKSCR(CmdBindDescriptorSets)(bs->cmdbuf,
                                 is_compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 /* set index incremented by 1 to account for push set */
                                 pg->layout, type + 1, 1, &desc_sets[type],
                                 0, NULL);
         bdd->sets[is_compute][type + 1] = desc_sets[type];
      }
   }
   u_foreach_bit(type, bind_sets & ~changed_sets) {
      if (!pg->dd->pool_key[type])
         bdd->sets[is_compute][type + 1] = ctx->dd->dummy_set;
      assert(bdd->sets[is_compute][type + 1]);
      VKSCR(CmdBindDescriptorSets)(bs->cmdbuf,
                              is_compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
                              /* set index incremented by 1 to account for push set */
                              pg->layout, type + 1, 1, &bdd->sets[is_compute][type + 1],
                              0, NULL);
   }
}

/* only called by cached manager for fbfetch handling */
VkDescriptorSet
zink_descriptors_alloc_lazy_push(struct zink_context *ctx)
{
   struct zink_batch_state *bs = ctx->batch.state;
   struct zink_batch_descriptor_data_lazy *bdd = bdd_lazy(bs);
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   VkDescriptorSet push_set = VK_NULL_HANDLE;
   if (!bdd->push_pool[0]) {
      bdd->push_pool[0] = create_push_pool(screen, bdd, false, true);
      bdd->has_fbfetch = true;
   }
   struct zink_descriptor_pool *pool = check_push_pool_alloc(ctx, bdd->push_pool[0], bdd, false);
   push_set = get_descriptor_set_lazy(pool);
   if (!push_set)
      mesa_loge("ZINK: failed to get push descriptor set!");
   return push_set;
}

void
zink_descriptors_update_lazy(struct zink_context *ctx, bool is_compute)
{
   struct zink_batch_state *bs = ctx->batch.state;
   struct zink_batch_descriptor_data_lazy *bdd = bdd_lazy(bs);
   struct zink_program *pg = is_compute ? &ctx->curr_compute->base : &ctx->curr_program->base;
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   bool have_KHR_push_descriptor = screen->info.have_KHR_push_descriptor;

   bool batch_changed = !bdd->pg[is_compute];
   if (batch_changed) {
      /* update all sets and bind null sets */
      dd_lazy(ctx)->state_changed[is_compute] = pg->dd->binding_usage;
      dd_lazy(ctx)->push_state_changed[is_compute] = !!pg->dd->push_usage;
   }

   if (pg != bdd->pg[is_compute]) {
      /* if we don't already know that we have to update all sets,
       * check to see if any dsls changed
       *
       * also always update the dsl pointers on program change
       */
       for (unsigned i = 0; i < ARRAY_SIZE(bdd->dsl[is_compute]); i++) {
          /* push set is already detected, start at 1 */
          if (bdd->dsl[is_compute][i] != pg->dsl[i + 1])
             dd_lazy(ctx)->state_changed[is_compute] |= BITFIELD_BIT(i);
          bdd->dsl[is_compute][i] = pg->dsl[i + 1];
       }
       dd_lazy(ctx)->push_state_changed[is_compute] |= bdd->push_usage[is_compute] != pg->dd->push_usage;
       bdd->push_usage[is_compute] = pg->dd->push_usage;
   }

   uint8_t changed_sets = pg->dd->binding_usage & dd_lazy(ctx)->state_changed[is_compute];
   bool need_push = pg->dd->push_usage &&
                    (dd_lazy(ctx)->push_state_changed[is_compute] || batch_changed);
   VkDescriptorSet push_set = VK_NULL_HANDLE;
   if (need_push && !have_KHR_push_descriptor) {
      struct zink_descriptor_pool *pool = check_push_pool_alloc(ctx, bdd->push_pool[pg->is_compute], bdd, pg->is_compute);
      push_set = get_descriptor_set_lazy(pool);
      if (!push_set) {
         mesa_loge("ZINK: failed to get push descriptor set!");
         /* just jam something in to avoid a hang */
         push_set = ctx->dd->dummy_set;
      }
   }
   /*
    * when binding a pipeline, the pipeline can correctly access any previously bound
    * descriptor sets which were bound with compatible pipeline layouts
    * VK 14.2.2
    */
   uint8_t bind_sets = bdd->pg[is_compute] && bdd->compat_id[is_compute] == pg->compat_id ? 0 : pg->dd->binding_usage;
   if (pg->dd->push_usage && (dd_lazy(ctx)->push_state_changed[is_compute] || bind_sets)) {
      if (have_KHR_push_descriptor) {
         if (dd_lazy(ctx)->push_state_changed[is_compute])
            VKCTX(CmdPushDescriptorSetWithTemplateKHR)(bs->cmdbuf, pg->dd->push_template,
                                                        pg->layout, 0, ctx);
      } else {
         if (dd_lazy(ctx)->push_state_changed[is_compute]) {
            VKCTX(UpdateDescriptorSetWithTemplate)(screen->dev, push_set, pg->dd->push_template, ctx);
            bdd->sets[is_compute][0] = push_set;
         }
         assert(push_set || bdd->sets[is_compute][0]);
         VKCTX(CmdBindDescriptorSets)(bs->cmdbuf,
                                 is_compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 pg->layout, 0, 1, push_set ? &push_set : &bdd->sets[is_compute][0],
                                 0, NULL);
      }
      dd_lazy(ctx)->push_state_changed[is_compute] = false;
   } else if (dd_lazy(ctx)->push_state_changed[is_compute] || bind_sets) {
      VKCTX(CmdBindDescriptorSets)(bs->cmdbuf,
                              is_compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pg->layout, 0, 1, &ctx->dd->dummy_set,
                              0, NULL);
      dd_lazy(ctx)->push_state_changed[is_compute] = false;
   }
   zink_descriptors_update_lazy_masked(ctx, is_compute, changed_sets, bind_sets);
   if (pg->dd->bindless && unlikely(!ctx->dd->bindless_bound)) {
      VKCTX(CmdBindDescriptorSets)(ctx->batch.state->cmdbuf, is_compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   pg->layout, ZINK_DESCRIPTOR_BINDLESS, 1, &ctx->dd->bindless_set,
                                   0, NULL);
      ctx->dd->bindless_bound = true;
   }
   bdd->pg[is_compute] = pg;
   ctx->dd->pg[is_compute] = pg;
   bdd->compat_id[is_compute] = pg->compat_id;
   dd_lazy(ctx)->state_changed[is_compute] = false;
}

void
zink_context_invalidate_descriptor_state_lazy(struct zink_context *ctx, enum pipe_shader_type shader, enum zink_descriptor_type type, unsigned start, unsigned count)
{
   if (type == ZINK_DESCRIPTOR_TYPE_UBO && !start)
      dd_lazy(ctx)->push_state_changed[shader == PIPE_SHADER_COMPUTE] = true;
   else
      dd_lazy(ctx)->state_changed[shader == PIPE_SHADER_COMPUTE] |= BITFIELD_BIT(type);
}

void
zink_batch_descriptor_deinit_lazy(struct zink_screen *screen, struct zink_batch_state *bs)
{
   if (!bs->dd)
      return;
   struct zink_batch_descriptor_data_lazy *bdd = bdd_lazy(bs);
   if (screen->info.have_KHR_descriptor_update_template) {
      for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPES; i++) {
         hash_table_foreach(&bdd->pools[i], entry) {
            struct zink_descriptor_pool *pool = (void*)entry->data;
            VKSCR(DestroyDescriptorPool)(screen->dev, pool->pool, NULL);
         }
      }
      if (bdd->push_pool[0])
         VKSCR(DestroyDescriptorPool)(screen->dev, bdd->push_pool[0]->pool, NULL);
      if (bdd->push_pool[1])
         VKSCR(DestroyDescriptorPool)(screen->dev, bdd->push_pool[1]->pool, NULL);
   }
   ralloc_free(bs->dd);
}

static void
pool_destroy(struct zink_screen *screen, struct zink_descriptor_pool *pool)
{
   VKSCR(DestroyDescriptorPool)(screen->dev, pool->pool, NULL);
   ralloc_free(pool);
}

void
zink_batch_descriptor_reset_lazy(struct zink_screen *screen, struct zink_batch_state *bs)
{
   if (!screen->info.have_KHR_descriptor_update_template)
      return;
   struct zink_batch_descriptor_data_lazy *bdd = bdd_lazy(bs);
   for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPES; i++) {
      hash_table_foreach(&bdd->pools[i], entry) {
         const struct zink_descriptor_pool_key *key = entry->key;
         struct zink_descriptor_pool *pool = (void*)entry->data;
         if (key->use_count)
            pool->set_idx = 0;
         else {
            pool_destroy(screen, pool);
            _mesa_hash_table_remove(&bdd->pools[i], entry);
         }
      }
   }
   for (unsigned i = 0; i < 2; i++) {
      bdd->pg[i] = NULL;
      if (bdd->push_pool[i])
         bdd->push_pool[i]->set_idx = 0;
   }
   while (util_dynarray_num_elements(&bdd->overflowed_pools, struct zink_descriptor_pool*)) {
      struct zink_descriptor_pool *pool = util_dynarray_pop(&bdd->overflowed_pools, struct zink_descriptor_pool*);
      pool_destroy(screen, pool);
   }
}

bool
zink_batch_descriptor_init_lazy(struct zink_screen *screen, struct zink_batch_state *bs)
{
   bs->dd = (void*)rzalloc(bs, struct zink_batch_descriptor_data_lazy);
   if (!bs->dd)
      return false;
   if (!screen->info.have_KHR_descriptor_update_template)
      return true;
   struct zink_batch_descriptor_data_lazy *bdd = bdd_lazy(bs);
   for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPES; i++) {
      if (!_mesa_hash_table_init(&bdd->pools[i], bs->dd, _mesa_hash_pointer, _mesa_key_pointer_equal))
         return false;
   }
   util_dynarray_init(&bdd->overflowed_pools, bs->dd);
   if (!screen->info.have_KHR_push_descriptor) {
      bdd->push_pool[0] = create_push_pool(screen, bdd, false, false);
      bdd->push_pool[1] = create_push_pool(screen, bdd, true, false);
   }
   return true;
}

static void
init_push_template_entry(VkDescriptorUpdateTemplateEntry *entry, unsigned i)
{
   entry->dstBinding = tgsi_processor_to_shader_stage(i);
   entry->descriptorCount = 1;
   entry->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
   entry->offset = offsetof(struct zink_context, di.ubos[i][0]);
   entry->stride = sizeof(VkDescriptorBufferInfo);
}

bool
zink_descriptors_init_lazy(struct zink_context *ctx)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   ctx->dd = (void*)rzalloc(ctx, struct zink_descriptor_data_lazy);
   if (!ctx->dd)
      return false;

   if (screen->descriptor_mode == ZINK_DESCRIPTOR_MODE_NOTEMPLATES)
      printf("ZINK: CACHED/NOTEMPLATES DESCRIPTORS\n");
   else if (screen->info.have_KHR_descriptor_update_template) {
      for (unsigned i = 0; i < ZINK_SHADER_COUNT; i++) {
         VkDescriptorUpdateTemplateEntry *entry = &dd_lazy(ctx)->push_entries[i];
         init_push_template_entry(entry, i);
      }
      init_push_template_entry(&dd_lazy(ctx)->compute_push_entry, PIPE_SHADER_COMPUTE);
      VkDescriptorUpdateTemplateEntry *entry = &dd_lazy(ctx)->push_entries[ZINK_SHADER_COUNT]; //fbfetch
      entry->dstBinding = ZINK_FBFETCH_BINDING;
      entry->descriptorCount = 1;
      entry->descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
      entry->offset = offsetof(struct zink_context, di.fbfetch);
      entry->stride = sizeof(VkDescriptorImageInfo);
      if (screen->descriptor_mode == ZINK_DESCRIPTOR_MODE_LAZY)
         printf("ZINK: USING LAZY DESCRIPTORS\n");
   }
   struct zink_descriptor_layout_key *layout_key;
   if (!zink_descriptor_util_push_layouts_get(ctx, ctx->dd->push_dsl, ctx->dd->push_layout_keys))
      return false;

   ctx->dd->dummy_dsl = zink_descriptor_util_layout_get(ctx, 0, NULL, 0, &layout_key);
   if (!ctx->dd->dummy_dsl)
      return false;
   VkDescriptorPoolSize null_size = {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1};
   ctx->dd->dummy_pool = create_pool(screen, 1, &null_size, 0);
   zink_descriptor_util_alloc_sets(screen, ctx->dd->dummy_dsl->layout,
                                   ctx->dd->dummy_pool, &ctx->dd->dummy_set, 1);
   zink_descriptor_util_init_null_set(ctx, ctx->dd->dummy_set);

   return true;
}

void
zink_descriptors_deinit_lazy(struct zink_context *ctx)
{
   if (ctx->dd) {
      struct zink_screen *screen = zink_screen(ctx->base.screen);
      if (ctx->dd->dummy_pool)
         VKSCR(DestroyDescriptorPool)(screen->dev, ctx->dd->dummy_pool, NULL);
      if (ctx->dd->push_dsl[0])
         VKSCR(DestroyDescriptorSetLayout)(screen->dev, ctx->dd->push_dsl[0]->layout, NULL);
      if (ctx->dd->push_dsl[1])
         VKSCR(DestroyDescriptorSetLayout)(screen->dev, ctx->dd->push_dsl[1]->layout, NULL);
   }
   ralloc_free(ctx->dd);
}
