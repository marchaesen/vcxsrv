/*
 * Copyright © 2020 Mike Blumenkrantz
 * Copyright © 2022 Valve Corporation
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

#include "zink_context.h"
#include "zink_descriptors.h"
#include "zink_program.h"
#include "zink_render_pass.h"
#include "zink_resource.h"
#include "zink_screen.h"

#define XXH_INLINE_ALL
#include "util/xxhash.h"

static VkDescriptorSetLayout
descriptor_layout_create(struct zink_screen *screen, enum zink_descriptor_type t, VkDescriptorSetLayoutBinding *bindings, unsigned num_bindings)
{
   VkDescriptorSetLayout dsl;
   VkDescriptorSetLayoutCreateInfo dcslci = {0};
   dcslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
   dcslci.pNext = NULL;
   VkDescriptorSetLayoutBindingFlagsCreateInfo fci = {0};
   VkDescriptorBindingFlags flags[ZINK_MAX_DESCRIPTORS_PER_TYPE];
   dcslci.pNext = &fci;
   if (t == ZINK_DESCRIPTOR_TYPES)
      dcslci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_PUSH_DESCRIPTOR_BIT_KHR;
   fci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
   fci.bindingCount = num_bindings;
   fci.pBindingFlags = flags;
   for (unsigned i = 0; i < num_bindings; i++) {
      flags[i] = 0;
   }
   dcslci.bindingCount = num_bindings;
   dcslci.pBindings = bindings;
   VkDescriptorSetLayoutSupport supp;
   supp.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_SUPPORT;
   supp.pNext = NULL;
   supp.supported = VK_FALSE;
   if (VKSCR(GetDescriptorSetLayoutSupport)) {
      VKSCR(GetDescriptorSetLayoutSupport)(screen->dev, &dcslci, &supp);
      if (supp.supported == VK_FALSE) {
         debug_printf("vkGetDescriptorSetLayoutSupport claims layout is unsupported\n");
         return VK_NULL_HANDLE;
      }
   }
   VkResult result = VKSCR(CreateDescriptorSetLayout)(screen->dev, &dcslci, 0, &dsl);
   if (result != VK_SUCCESS)
      mesa_loge("ZINK: vkCreateDescriptorSetLayout failed (%s)", vk_Result_to_str(result));
   return dsl;
}

static uint32_t
hash_descriptor_layout(const void *key)
{
   uint32_t hash = 0;
   const struct zink_descriptor_layout_key *k = key;
   hash = XXH32(&k->num_bindings, sizeof(unsigned), hash);
   /* only hash first 3 members: no holes and the rest are always constant */
   for (unsigned i = 0; i < k->num_bindings; i++)
      hash = XXH32(&k->bindings[i], offsetof(VkDescriptorSetLayoutBinding, stageFlags), hash);

   return hash;
}

static bool
equals_descriptor_layout(const void *a, const void *b)
{
   const struct zink_descriptor_layout_key *a_k = a;
   const struct zink_descriptor_layout_key *b_k = b;
   return a_k->num_bindings == b_k->num_bindings &&
          !memcmp(a_k->bindings, b_k->bindings, a_k->num_bindings * sizeof(VkDescriptorSetLayoutBinding));
}

static struct zink_descriptor_layout *
create_layout(struct zink_context *ctx, enum zink_descriptor_type type,
              VkDescriptorSetLayoutBinding *bindings, unsigned num_bindings,
              struct zink_descriptor_layout_key **layout_key)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   VkDescriptorSetLayout dsl = descriptor_layout_create(screen, type, bindings, num_bindings);
   if (!dsl)
      return NULL;

   size_t bindings_size = num_bindings * sizeof(VkDescriptorSetLayoutBinding);
   struct zink_descriptor_layout_key *k = ralloc_size(ctx, sizeof(struct zink_descriptor_layout_key) + bindings_size);
   k->num_bindings = num_bindings;
   if (num_bindings) {
      k->bindings = (void *)(k + 1);
      memcpy(k->bindings, bindings, bindings_size);
   }

   struct zink_descriptor_layout *layout = rzalloc(ctx, struct zink_descriptor_layout);
   layout->layout = dsl;
   *layout_key = k;
   return layout;
}

struct zink_descriptor_layout *
zink_descriptor_util_layout_get(struct zink_context *ctx, enum zink_descriptor_type type,
                      VkDescriptorSetLayoutBinding *bindings, unsigned num_bindings,
                      struct zink_descriptor_layout_key **layout_key)
{
   uint32_t hash = 0;
   struct zink_descriptor_layout_key key = {
      .num_bindings = num_bindings,
      .bindings = bindings,
   };

   if (type != ZINK_DESCRIPTOR_TYPES) {
      hash = hash_descriptor_layout(&key);
      simple_mtx_lock(&ctx->desc_set_layouts_lock);
      struct hash_entry *he = _mesa_hash_table_search_pre_hashed(&ctx->desc_set_layouts[type], hash, &key);
      simple_mtx_unlock(&ctx->desc_set_layouts_lock);
      if (he) {
         *layout_key = (void*)he->key;
         return he->data;
      }
   }

   struct zink_descriptor_layout *layout = create_layout(ctx, type, bindings, num_bindings, layout_key);
   if (layout && type != ZINK_DESCRIPTOR_TYPES) {
      simple_mtx_lock(&ctx->desc_set_layouts_lock);
      _mesa_hash_table_insert_pre_hashed(&ctx->desc_set_layouts[type], hash, *layout_key, layout);
      simple_mtx_unlock(&ctx->desc_set_layouts_lock);
   }
   return layout;
}


static uint32_t
hash_descriptor_pool_key(const void *key)
{
   uint32_t hash = 0;
   const struct zink_descriptor_pool_key *k = key;
   hash = XXH32(&k->layout, sizeof(void*), hash);
   for (unsigned i = 0; i < k->num_type_sizes; i++)
      hash = XXH32(&k->sizes[i], sizeof(VkDescriptorPoolSize), hash);

   return hash;
}

static bool
equals_descriptor_pool_key(const void *a, const void *b)
{
   const struct zink_descriptor_pool_key *a_k = a;
   const struct zink_descriptor_pool_key *b_k = b;
   const unsigned a_num_type_sizes = a_k->num_type_sizes;
   const unsigned b_num_type_sizes = b_k->num_type_sizes;
   return a_k->layout == b_k->layout &&
          a_num_type_sizes == b_num_type_sizes &&
          !memcmp(a_k->sizes, b_k->sizes, b_num_type_sizes * sizeof(VkDescriptorPoolSize));
}

struct zink_descriptor_pool_key *
zink_descriptor_util_pool_key_get(struct zink_context *ctx, enum zink_descriptor_type type,
                                  struct zink_descriptor_layout_key *layout_key,
                                  VkDescriptorPoolSize *sizes, unsigned num_type_sizes)
{
   uint32_t hash = 0;
   struct zink_descriptor_pool_key key;
   key.num_type_sizes = num_type_sizes;
   if (type != ZINK_DESCRIPTOR_TYPES) {
      key.layout = layout_key;
      memcpy(key.sizes, sizes, num_type_sizes * sizeof(VkDescriptorPoolSize));
      hash = hash_descriptor_pool_key(&key);
      simple_mtx_lock(&ctx->desc_pool_keys_lock);
      struct set_entry *he = _mesa_set_search_pre_hashed(&ctx->desc_pool_keys[type], hash, &key);
      simple_mtx_unlock(&ctx->desc_pool_keys_lock);
      if (he)
         return (void*)he->key;
   }

   struct zink_descriptor_pool_key *pool_key = rzalloc(ctx, struct zink_descriptor_pool_key);
   pool_key->layout = layout_key;
   pool_key->num_type_sizes = num_type_sizes;
   assert(pool_key->num_type_sizes);
   memcpy(pool_key->sizes, sizes, num_type_sizes * sizeof(VkDescriptorPoolSize));
   if (type != ZINK_DESCRIPTOR_TYPES) {
      simple_mtx_lock(&ctx->desc_pool_keys_lock);
      _mesa_set_add_pre_hashed(&ctx->desc_pool_keys[type], hash, pool_key);
      pool_key->id = ctx->desc_pool_keys[type].entries - 1;
      simple_mtx_unlock(&ctx->desc_pool_keys_lock);
   }
   return pool_key;
}

static void
init_push_binding(VkDescriptorSetLayoutBinding *binding, unsigned i, VkDescriptorType type)
{
   binding->binding = i;
   binding->descriptorType = type;
   binding->descriptorCount = 1;
   binding->stageFlags = mesa_to_vk_shader_stage(i);
   binding->pImmutableSamplers = NULL;
}

static VkDescriptorType
get_push_types(struct zink_screen *screen, enum zink_descriptor_type *dsl_type)
{
   *dsl_type = screen->info.have_KHR_push_descriptor ? ZINK_DESCRIPTOR_TYPES : ZINK_DESCRIPTOR_TYPE_UBO;
   return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
}

static struct zink_descriptor_layout *
create_gfx_layout(struct zink_context *ctx, struct zink_descriptor_layout_key **layout_key, bool fbfetch)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   VkDescriptorSetLayoutBinding bindings[MESA_SHADER_STAGES];
   enum zink_descriptor_type dsl_type;
   VkDescriptorType vktype = get_push_types(screen, &dsl_type);
   for (unsigned i = 0; i < ZINK_GFX_SHADER_COUNT; i++)
      init_push_binding(&bindings[i], i, vktype);
   if (fbfetch) {
      bindings[ZINK_GFX_SHADER_COUNT].binding = ZINK_FBFETCH_BINDING;
      bindings[ZINK_GFX_SHADER_COUNT].descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
      bindings[ZINK_GFX_SHADER_COUNT].descriptorCount = 1;
      bindings[ZINK_GFX_SHADER_COUNT].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
      bindings[ZINK_GFX_SHADER_COUNT].pImmutableSamplers = NULL;
   }
   return create_layout(ctx, dsl_type, bindings, fbfetch ? ARRAY_SIZE(bindings) : ARRAY_SIZE(bindings) - 1, layout_key);
}

bool
zink_descriptor_util_push_layouts_get(struct zink_context *ctx, struct zink_descriptor_layout **dsls, struct zink_descriptor_layout_key **layout_keys)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   VkDescriptorSetLayoutBinding compute_binding;
   enum zink_descriptor_type dsl_type;
   VkDescriptorType vktype = get_push_types(screen, &dsl_type);
   init_push_binding(&compute_binding, MESA_SHADER_COMPUTE, vktype);
   dsls[0] = create_gfx_layout(ctx, &layout_keys[0], false);
   dsls[1] = create_layout(ctx, dsl_type, &compute_binding, 1, &layout_keys[1]);
   return dsls[0] && dsls[1];
}

VkImageLayout
zink_descriptor_util_image_layout_eval(const struct zink_context *ctx, const struct zink_resource *res, bool is_compute)
{
   if (res->bindless[0] || res->bindless[1]) {
      /* bindless needs most permissive layout */
      if (res->image_bind_count[0] || res->image_bind_count[1])
         return VK_IMAGE_LAYOUT_GENERAL;
      return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
   }
   if (res->image_bind_count[is_compute])
      return VK_IMAGE_LAYOUT_GENERAL;
   if (res->aspect & (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)) {
      if (!is_compute && res->fb_binds &&
          ctx->gfx_pipeline_state.render_pass && ctx->gfx_pipeline_state.render_pass->state.rts[ctx->fb_state.nr_cbufs].mixed_zs)
         return VK_IMAGE_LAYOUT_GENERAL;
      if (res->obj->vkusage & VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)
         return VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
   }
   return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

bool
zink_descriptor_util_alloc_sets(struct zink_screen *screen, VkDescriptorSetLayout dsl, VkDescriptorPool pool, VkDescriptorSet *sets, unsigned num_sets)
{
   VkDescriptorSetAllocateInfo dsai;
   VkDescriptorSetLayout layouts[100];
   assert(num_sets <= ARRAY_SIZE(layouts));
   memset((void *)&dsai, 0, sizeof(dsai));
   dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
   dsai.pNext = NULL;
   dsai.descriptorPool = pool;
   dsai.descriptorSetCount = num_sets;
   for (unsigned i = 0; i < num_sets; i ++)
      layouts[i] = dsl;
   dsai.pSetLayouts = layouts;

   VkResult result = VKSCR(AllocateDescriptorSets)(screen->dev, &dsai, sets);
   if (result != VK_SUCCESS) {
      mesa_loge("ZINK: %" PRIu64 " failed to allocate descriptor set :/ (%s)", (uint64_t)dsl, vk_Result_to_str(result));
      return false;
   }
   return true;
}

static void
init_template_entry(struct zink_shader *shader, enum zink_descriptor_type type,
                    unsigned idx, VkDescriptorUpdateTemplateEntry *entry, unsigned *entry_idx)
{
    int index = shader->bindings[type][idx].index;
    gl_shader_stage stage = shader->nir->info.stage;
    entry->dstArrayElement = 0;
    entry->dstBinding = shader->bindings[type][idx].binding;
    entry->descriptorCount = shader->bindings[type][idx].size;
    if (shader->bindings[type][idx].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
       /* filter out DYNAMIC type here */
       entry->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    else
       entry->descriptorType = shader->bindings[type][idx].type;
    switch (shader->bindings[type][idx].type) {
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
    case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
       entry->offset = offsetof(struct zink_context, di.ubos[stage][index]);
       entry->stride = sizeof(VkDescriptorBufferInfo);
       break;
    case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
       entry->offset = offsetof(struct zink_context, di.textures[stage][index]);
       entry->stride = sizeof(VkDescriptorImageInfo);
       break;
    case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
       entry->offset = offsetof(struct zink_context, di.tbos[stage][index]);
       entry->stride = sizeof(VkBufferView);
       break;
    case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
       entry->offset = offsetof(struct zink_context, di.ssbos[stage][index]);
       entry->stride = sizeof(VkDescriptorBufferInfo);
       break;
    case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
       entry->offset = offsetof(struct zink_context, di.images[stage][index]);
       entry->stride = sizeof(VkDescriptorImageInfo);
       break;
    case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
       entry->offset = offsetof(struct zink_context, di.texel_images[stage][index]);
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
      return !!sizes[ZDS_INDEX_UBO].descriptorCount;
   case ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW:
      return !!sizes[ZDS_INDEX_COMBINED_SAMPLER].descriptorCount +
             !!sizes[ZDS_INDEX_UNIFORM_TEXELS].descriptorCount;
   case ZINK_DESCRIPTOR_TYPE_SSBO:
      return !!sizes[ZDS_INDEX_STORAGE_BUFFER].descriptorCount;
   case ZINK_DESCRIPTOR_TYPE_IMAGE:
      return !!sizes[ZDS_INDEX_STORAGE_IMAGE].descriptorCount +
             !!sizes[ZDS_INDEX_STORAGE_TEXELS].descriptorCount;
   default: break;
   }
   unreachable("unknown type");
}

static uint16_t
descriptor_program_num_sizes_compact(VkDescriptorPoolSize *sizes, unsigned desc_set)
{
   switch (desc_set) {
   case ZINK_DESCRIPTOR_TYPE_UBO:
      return !!sizes[ZDS_INDEX_COMP_UBO].descriptorCount + !!sizes[ZDS_INDEX_COMP_STORAGE_BUFFER].descriptorCount;
   case ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW:
      return !!sizes[ZDS_INDEX_COMP_COMBINED_SAMPLER].descriptorCount +
             !!sizes[ZDS_INDEX_COMP_UNIFORM_TEXELS].descriptorCount +
             !!sizes[ZDS_INDEX_COMP_STORAGE_IMAGE].descriptorCount +
             !!sizes[ZDS_INDEX_COMP_STORAGE_TEXELS].descriptorCount;
   case ZINK_DESCRIPTOR_TYPE_SSBO:
   case ZINK_DESCRIPTOR_TYPE_IMAGE:
   default: break;
   }
   unreachable("unknown type");
}

bool
zink_descriptor_program_init(struct zink_context *ctx, struct zink_program *pg)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   VkDescriptorSetLayoutBinding bindings[ZINK_DESCRIPTOR_TYPES][MESA_SHADER_STAGES * 64];
   VkDescriptorUpdateTemplateEntry entries[ZINK_DESCRIPTOR_TYPES][MESA_SHADER_STAGES * 64];
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

   if (!pg->is_compute && stages[MESA_SHADER_FRAGMENT]->nir->info.fs.uses_fbfetch_output) {
      push_count = 1;
      pg->dd.fbfetch = true;
   }

   unsigned entry_idx[ZINK_DESCRIPTOR_TYPES] = {0};

   unsigned num_shaders = pg->is_compute ? 1 : ZINK_GFX_SHADER_COUNT;
   bool have_push = screen->info.have_KHR_push_descriptor;
   for (int i = 0; i < num_shaders; i++) {
      struct zink_shader *shader = stages[i];
      if (!shader)
         continue;

      gl_shader_stage stage = shader->nir->info.stage;
      VkShaderStageFlagBits stage_flags = mesa_to_vk_shader_stage(stage);
      for (int j = 0; j < ZINK_DESCRIPTOR_TYPES; j++) {
         unsigned desc_set = screen->desc_set_id[j] - 1;
         for (int k = 0; k < shader->num_bindings[j]; k++) {
            /* dynamic ubos handled in push */
            if (shader->bindings[j][k].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC) {
               pg->dd.push_usage |= BITFIELD64_BIT(stage);

               push_count++;
               continue;
            }

            assert(num_bindings[desc_set] < ARRAY_SIZE(bindings[desc_set]));
            VkDescriptorSetLayoutBinding *binding = &bindings[desc_set][num_bindings[desc_set]];
            binding->binding = shader->bindings[j][k].binding;
            binding->descriptorType = shader->bindings[j][k].type;
            binding->descriptorCount = shader->bindings[j][k].size;
            binding->stageFlags = stage_flags;
            binding->pImmutableSamplers = NULL;

            unsigned idx = screen->compact_descriptors ? zink_vktype_to_size_idx_comp(shader->bindings[j][k].type) :
                                                         zink_vktype_to_size_idx(shader->bindings[j][k].type);
            sizes[idx].descriptorCount += shader->bindings[j][k].size;
            sizes[idx].type = shader->bindings[j][k].type;
            init_template_entry(shader, j, k, &entries[desc_set][entry_idx[desc_set]], &entry_idx[desc_set]);
            num_bindings[desc_set]++;
            has_bindings |= BITFIELD_BIT(desc_set);
            pg->dd.real_binding_usage |= BITFIELD_BIT(j);
         }
         num_type_sizes[desc_set] = screen->compact_descriptors ?
                                    descriptor_program_num_sizes_compact(sizes, desc_set) :
                                    descriptor_program_num_sizes(sizes, j);
      }
      pg->dd.bindless |= shader->bindless;
   }
   pg->dd.binding_usage = has_bindings;
   if (!has_bindings && !push_count && !pg->dd.bindless) {
      pg->layout = zink_pipeline_layout_create(screen, pg, &pg->compat_id);
      return !!pg->layout;
   }

   pg->dsl[pg->num_dsl++] = push_count ? ctx->dd.push_dsl[pg->is_compute]->layout : ctx->dd.dummy_dsl->layout;
   if (has_bindings) {
      for (unsigned i = 0; i < ARRAY_SIZE(sizes); i++)
         sizes[i].descriptorCount *= MAX_LAZY_DESCRIPTORS;
      u_foreach_bit(desc_set, has_bindings) {
         for (unsigned i = 0; i < desc_set; i++) {
            /* push set is always 0 */
            if (!pg->dsl[i + 1]) {
               /* inject a null dsl */
               pg->dsl[pg->num_dsl++] = ctx->dd.dummy_dsl->layout;
               pg->dd.binding_usage |= BITFIELD_BIT(i);
            }
         }
         struct zink_descriptor_layout_key *key;
         pg->dd.layouts[pg->num_dsl] = zink_descriptor_util_layout_get(ctx, desc_set, bindings[desc_set], num_bindings[desc_set], &key);
         unsigned idx = screen->compact_descriptors ? zink_descriptor_type_to_size_idx_comp(desc_set) :
                                                      zink_descriptor_type_to_size_idx(desc_set);
         VkDescriptorPoolSize *sz = &sizes[idx];
         VkDescriptorPoolSize sz2[4];
         if (screen->compact_descriptors) {
            unsigned found = 0;
            while (found < num_type_sizes[desc_set]) {
               if (sz->descriptorCount) {
                  memcpy(&sz2[found], sz, sizeof(VkDescriptorPoolSize));
                  found++;
               }
               sz++;
            }
            sz = sz2;
         } else {
            if (!sz->descriptorCount)
               sz++;
         }
         pg->dd.pool_key[desc_set] = zink_descriptor_util_pool_key_get(ctx, desc_set, key, sz, num_type_sizes[desc_set]);
         pg->dd.pool_key[desc_set]->use_count++;
         pg->dsl[pg->num_dsl] = pg->dd.layouts[pg->num_dsl]->layout;
         pg->num_dsl++;
      }
   }
   /* TODO: make this dynamic? */
   if (pg->dd.bindless) {
      unsigned desc_set = screen->desc_set_id[ZINK_DESCRIPTOR_BINDLESS];
      pg->num_dsl = desc_set + 1;
      pg->dsl[desc_set] = ctx->dd.bindless_layout;
      for (unsigned i = 0; i < desc_set; i++) {
         if (!pg->dsl[i]) {
            /* inject a null dsl */
            pg->dsl[i] = ctx->dd.dummy_dsl->layout;
            if (i != screen->desc_set_id[ZINK_DESCRIPTOR_TYPES])
               pg->dd.binding_usage |= BITFIELD_BIT(i);
         }
      }
      pg->dd.binding_usage |= BITFIELD_MASK(ZINK_DESCRIPTOR_TYPES);
   }

   pg->layout = zink_pipeline_layout_create(screen, pg, &pg->compat_id);
   if (!pg->layout)
      return false;

   VkDescriptorUpdateTemplateCreateInfo template[ZINK_DESCRIPTOR_TYPES + 1] = {0};
   /* type of template */
   VkDescriptorUpdateTemplateType types[ZINK_DESCRIPTOR_TYPES + 1] = {VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET};
   if (have_push)
      types[0] = VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR;

   /* number of descriptors in template */
   unsigned wd_count[ZINK_DESCRIPTOR_TYPES + 1];
   if (push_count)
      wd_count[0] = pg->is_compute ? 1 : (ZINK_GFX_SHADER_COUNT + !!ctx->dd.has_fbfetch);
   for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPES; i++)
      wd_count[i + 1] = pg->dd.pool_key[i] ? pg->dd.pool_key[i]->layout->num_bindings : 0;

   VkDescriptorUpdateTemplateEntry *push_entries[2] = {
      ctx->dd.push_entries,
      &ctx->dd.compute_push_entry,
   };
   for (unsigned i = 0; i < pg->num_dsl; i++) {
      bool is_push = i == 0;
      /* no need for empty templates */
      if (pg->dsl[i] == ctx->dd.dummy_dsl->layout ||
          pg->dsl[i] == ctx->dd.bindless_layout ||
          (!is_push && pg->dd.templates[i]))
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
      VkDescriptorUpdateTemplate t;
      if (VKSCR(CreateDescriptorUpdateTemplate)(screen->dev, &template[i], NULL, &t) != VK_SUCCESS)
         return false;
      pg->dd.templates[i] = t;
   }
   return true;
}

void
zink_descriptor_program_deinit(struct zink_screen *screen, struct zink_program *pg)
{
   for (unsigned i = 0; pg->num_dsl && i < ZINK_DESCRIPTOR_TYPES; i++) {
      if (pg->dd.pool_key[i]) {
         pg->dd.pool_key[i]->use_count--;
         pg->dd.pool_key[i] = NULL;
      }
      if (pg->dd.templates[i]) {
         VKSCR(DestroyDescriptorUpdateTemplate)(screen->dev, pg->dd.templates[i], NULL);
         pg->dd.templates[i] = VK_NULL_HANDLE;
      }
   }
}

static void
pool_destroy(struct zink_screen *screen, struct zink_descriptor_pool *pool)
{
   VKSCR(DestroyDescriptorPool)(screen->dev, pool->pool, NULL);
   ralloc_free(pool);
}

static void
multi_pool_destroy(struct zink_screen *screen, struct zink_descriptor_pool_multi *mpool)
{
   if (mpool->pool)
      pool_destroy(screen, mpool->pool);
   ralloc_free(mpool);
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
   VkResult result = VKSCR(CreateDescriptorPool)(screen->dev, &dpci, 0, &pool);
   if (result != VK_SUCCESS) {
      mesa_loge("ZINK: vkCreateDescriptorPool failed (%s)", vk_Result_to_str(result));
      return VK_NULL_HANDLE;
   }
   return pool;
}

static struct zink_descriptor_pool *
get_descriptor_pool(struct zink_context *ctx, struct zink_program *pg, enum zink_descriptor_type type, struct zink_batch_state *bs, bool is_compute);

static bool
set_pool(struct zink_batch_state *bs, struct zink_program *pg, struct zink_descriptor_pool_multi *mpool, enum zink_descriptor_type type)
{
   assert(type != ZINK_DESCRIPTOR_TYPES);
   assert(mpool);
   const struct zink_descriptor_pool_key *pool_key = pg->dd.pool_key[type];
   size_t size = bs->dd.pools[type].capacity;
   if (!util_dynarray_resize(&bs->dd.pools[type], struct zink_descriptor_pool*, pool_key->id + 1))
      return false;
   if (size != bs->dd.pools[type].capacity) {
      uint8_t *data = bs->dd.pools[type].data;
      memset(data + size, 0, bs->dd.pools[type].capacity - size);
   }
   bs->dd.pool_size[type] = MAX2(bs->dd.pool_size[type], pool_key->id + 1);
   struct zink_descriptor_pool_multi **mppool = util_dynarray_element(&bs->dd.pools[type], struct zink_descriptor_pool_multi*, pool_key->id);
   *mppool = mpool;
   return true;
}

static struct zink_descriptor_pool *
alloc_new_pool(struct zink_screen *screen, struct zink_descriptor_pool_multi *mpool)
{
   struct zink_descriptor_pool *pool = rzalloc(mpool, struct zink_descriptor_pool);
   if (!pool)
      return NULL;
   const unsigned num_type_sizes = mpool->pool_key->sizes[1].descriptorCount ? 2 : 1;
   pool->pool = create_pool(screen, num_type_sizes, mpool->pool_key->sizes, 0);
   if (!pool->pool) {
      ralloc_free(pool);
      return NULL;
   }
   return pool;
}

static struct zink_descriptor_pool *
check_pool_alloc(struct zink_context *ctx, struct zink_descriptor_pool_multi *mpool, struct zink_program *pg,
                 enum zink_descriptor_type type, struct zink_batch_state *bs, bool is_compute)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   if (!mpool->pool) {
      if (util_dynarray_contains(&mpool->overflowed_pools[!mpool->overflow_idx], struct zink_descriptor_pool*))
         mpool->pool = util_dynarray_pop(&mpool->overflowed_pools[!mpool->overflow_idx], struct zink_descriptor_pool*);
      else
         mpool->pool = alloc_new_pool(screen, mpool);
   }
   struct zink_descriptor_pool *pool = mpool->pool;
   /* allocate up to $current * 10, e.g., 10 -> 100 or 100 -> 1000 */
   if (pool->set_idx == pool->sets_alloc) {
      unsigned sets_to_alloc = MIN2(MIN2(MAX2(pool->sets_alloc * 10, 10), MAX_LAZY_DESCRIPTORS) - pool->sets_alloc, 100);
      if (!sets_to_alloc) {
         /* overflowed pool: store for reuse */
         pool->set_idx = 0;
         util_dynarray_append(&mpool->overflowed_pools[mpool->overflow_idx], struct zink_descriptor_pool*, pool);
         mpool->pool = NULL;
         return get_descriptor_pool(ctx, pg, type, bs, is_compute);
      }
      if (!zink_descriptor_util_alloc_sets(screen, pg->dsl[type + 1],
                                           pool->pool, &pool->sets[pool->sets_alloc], sets_to_alloc))
         return NULL;
      pool->sets_alloc += sets_to_alloc;
   }
   return pool;
}

static struct zink_descriptor_pool *
create_push_pool(struct zink_screen *screen, struct zink_batch_state *bs, bool is_compute, bool has_fbfetch)
{
   struct zink_descriptor_pool *pool = rzalloc(bs, struct zink_descriptor_pool);
   VkDescriptorPoolSize sizes[2];
   sizes[0].type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
   if (is_compute)
      sizes[0].descriptorCount = MAX_LAZY_DESCRIPTORS;
   else {
      sizes[0].descriptorCount = ZINK_GFX_SHADER_COUNT * MAX_LAZY_DESCRIPTORS;
      sizes[1].type = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
      sizes[1].descriptorCount = MAX_LAZY_DESCRIPTORS;
   }
   pool->pool = create_pool(screen, !is_compute && has_fbfetch ? 2 : 1, sizes, 0);
   return pool;
}

static struct zink_descriptor_pool *
check_push_pool_alloc(struct zink_context *ctx, struct zink_descriptor_pool_multi *mpool, struct zink_batch_state *bs, bool is_compute)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_descriptor_pool *pool = mpool->pool;
   /* allocate up to $current * 10, e.g., 10 -> 100 or 100 -> 1000 */
   if (pool->set_idx == pool->sets_alloc || unlikely(ctx->dd.has_fbfetch != bs->dd.has_fbfetch)) {
      unsigned sets_to_alloc = MIN2(MIN2(MAX2(pool->sets_alloc * 10, 10), MAX_LAZY_DESCRIPTORS) - pool->sets_alloc, 100);
      if (!sets_to_alloc || unlikely(ctx->dd.has_fbfetch != bs->dd.has_fbfetch)) {
         /* overflowed pool: store for reuse */
         pool->set_idx = 0;
         util_dynarray_append(&mpool->overflowed_pools[mpool->overflow_idx], struct zink_descriptor_pool*, pool);
         if (util_dynarray_contains(&mpool->overflowed_pools[!mpool->overflow_idx], struct zink_descriptor_pool*))
            bs->dd.push_pool[is_compute].pool = util_dynarray_pop(&mpool->overflowed_pools[!mpool->overflow_idx], struct zink_descriptor_pool*);
         else
            bs->dd.push_pool[is_compute].pool = create_push_pool(screen, bs, is_compute, ctx->dd.has_fbfetch);
         if (unlikely(ctx->dd.has_fbfetch != bs->dd.has_fbfetch))
            mpool->reinit_overflow = true;
         bs->dd.has_fbfetch = ctx->dd.has_fbfetch;
         return check_push_pool_alloc(ctx, &bs->dd.push_pool[is_compute], bs, is_compute);
      }
      if (!zink_descriptor_util_alloc_sets(screen, ctx->dd.push_dsl[is_compute]->layout,
                                           pool->pool, &pool->sets[pool->sets_alloc], sets_to_alloc)) {
         mesa_loge("ZINK: failed to allocate push set!");
         return NULL;
      }
      pool->sets_alloc += sets_to_alloc;
   }
   return pool;
}

static struct zink_descriptor_pool *
get_descriptor_pool(struct zink_context *ctx, struct zink_program *pg, enum zink_descriptor_type type, struct zink_batch_state *bs, bool is_compute)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   const struct zink_descriptor_pool_key *pool_key = pg->dd.pool_key[type];
   struct zink_descriptor_pool_multi **mppool = bs->dd.pool_size[type] > pool_key->id ?
                                         util_dynarray_element(&bs->dd.pools[type], struct zink_descriptor_pool_multi *, pool_key->id) :
                                         NULL;
   if (mppool && *mppool)
      return check_pool_alloc(ctx, *mppool, pg, type, bs, is_compute);
   struct zink_descriptor_pool_multi *mpool = rzalloc(bs, struct zink_descriptor_pool_multi);
   if (!mpool)
      return NULL;
   util_dynarray_init(&mpool->overflowed_pools[0], mpool);
   util_dynarray_init(&mpool->overflowed_pools[1], mpool);
   mpool->pool_key = pool_key;
   if (!set_pool(bs, pg, mpool, type)) {
      multi_pool_destroy(screen, mpool);
      return NULL;
   }
   assert(pool_key->id < bs->dd.pool_size[type]);
   return check_pool_alloc(ctx, mpool, pg, type, bs, is_compute);
}

ALWAYS_INLINE static VkDescriptorSet
get_descriptor_set(struct zink_descriptor_pool *pool)
{
   if (!pool)
      return VK_NULL_HANDLE;

   assert(pool->set_idx < pool->sets_alloc);
   return pool->sets[pool->set_idx++];
}

static bool
populate_sets(struct zink_context *ctx, struct zink_batch_state *bs,
              struct zink_program *pg, uint8_t *changed_sets, VkDescriptorSet *sets)
{
   u_foreach_bit(type, *changed_sets) {
      if (pg->dd.pool_key[type]) {
         struct zink_descriptor_pool *pool = get_descriptor_pool(ctx, pg, type, bs, pg->is_compute);
         sets[type] = get_descriptor_set(pool);
         if (!sets[type])
            return false;
      } else
         sets[type] = VK_NULL_HANDLE;
   }
   return true;
}

void
zink_descriptor_set_update(struct zink_context *ctx, struct zink_program *pg, enum zink_descriptor_type type, VkDescriptorSet set)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   VKCTX(UpdateDescriptorSetWithTemplate)(screen->dev, set, pg->dd.templates[type + 1], ctx);
}

void
zink_descriptors_update_masked(struct zink_context *ctx, bool is_compute, uint8_t changed_sets, uint8_t bind_sets)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_batch_state *bs = ctx->batch.state;
   struct zink_program *pg = is_compute ? &ctx->curr_compute->base : &ctx->curr_program->base;
   VkDescriptorSet desc_sets[ZINK_DESCRIPTOR_TYPES];
   if (!pg->dd.binding_usage || (!changed_sets && !bind_sets))
      return;

   if (!populate_sets(ctx, bs, pg, &changed_sets, desc_sets)) {
      debug_printf("ZINK: couldn't get descriptor sets!\n");
      return;
   }
   /* no flushing allowed */
   assert(ctx->batch.state == bs);

   u_foreach_bit(type, changed_sets) {
      assert(type + 1 < pg->num_dsl);
      if (pg->dd.pool_key[type]) {
         VKSCR(UpdateDescriptorSetWithTemplate)(screen->dev, desc_sets[type], pg->dd.templates[type + 1], ctx);
         VKSCR(CmdBindDescriptorSets)(bs->cmdbuf,
                                 is_compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 /* set index incremented by 1 to account for push set */
                                 pg->layout, type + 1, 1, &desc_sets[type],
                                 0, NULL);
         bs->dd.sets[is_compute][type + 1] = desc_sets[type];
      }
   }
   u_foreach_bit(type, bind_sets & ~changed_sets) {
      if (!pg->dd.pool_key[type])
         continue;
      assert(bs->dd.sets[is_compute][type + 1]);
      VKSCR(CmdBindDescriptorSets)(bs->cmdbuf,
                              is_compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
                              /* set index incremented by 1 to account for push set */
                              pg->layout, type + 1, 1, &bs->dd.sets[is_compute][type + 1],
                              0, NULL);
   }
}

void
zink_descriptors_update(struct zink_context *ctx, bool is_compute)
{
   struct zink_batch_state *bs = ctx->batch.state;
   struct zink_program *pg = is_compute ? &ctx->curr_compute->base : &ctx->curr_program->base;
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   bool have_KHR_push_descriptor = screen->info.have_KHR_push_descriptor;

   bool batch_changed = !bs->dd.pg[is_compute];
   if (batch_changed) {
      /* update all sets and bind null sets */
      ctx->dd.state_changed[is_compute] = pg->dd.binding_usage & BITFIELD_MASK(ZINK_DESCRIPTOR_TYPES);
      ctx->dd.push_state_changed[is_compute] = !!pg->dd.push_usage;
   }

   if (pg != bs->dd.pg[is_compute]) {
      /* if we don't already know that we have to update all sets,
       * check to see if any dsls changed
       *
       * also always update the dsl pointers on program change
       */
       for (unsigned i = 0; i < ARRAY_SIZE(bs->dd.dsl[is_compute]); i++) {
          /* push set is already detected, start at 1 */
          if (bs->dd.dsl[is_compute][i] != pg->dsl[i + 1])
             ctx->dd.state_changed[is_compute] |= BITFIELD_BIT(i);
          bs->dd.dsl[is_compute][i] = pg->dsl[i + 1];
       }
       ctx->dd.push_state_changed[is_compute] |= bs->dd.push_usage[is_compute] != pg->dd.push_usage;
       bs->dd.push_usage[is_compute] = pg->dd.push_usage;
   }

   uint8_t changed_sets = pg->dd.binding_usage & ctx->dd.state_changed[is_compute];
   bool need_push = pg->dd.push_usage &&
                    (ctx->dd.push_state_changed[is_compute] || batch_changed);
   VkDescriptorSet push_set = VK_NULL_HANDLE;
   if (need_push && !have_KHR_push_descriptor) {
      struct zink_descriptor_pool *pool = check_push_pool_alloc(ctx, &bs->dd.push_pool[pg->is_compute], bs, pg->is_compute);
      push_set = get_descriptor_set(pool);
      if (!push_set)
         mesa_loge("ZINK: failed to get push descriptor set! prepare to crash!");
   }
   /*
    * when binding a pipeline, the pipeline can correctly access any previously bound
    * descriptor sets which were bound with compatible pipeline layouts
    * VK 14.2.2
    */
   uint8_t bind_sets = bs->dd.pg[is_compute] && bs->dd.compat_id[is_compute] == pg->compat_id ? 0 : pg->dd.binding_usage;
   if (pg->dd.push_usage && (ctx->dd.push_state_changed[is_compute] || bind_sets)) {
      if (have_KHR_push_descriptor) {
         if (ctx->dd.push_state_changed[is_compute])
            VKCTX(CmdPushDescriptorSetWithTemplateKHR)(bs->cmdbuf, pg->dd.templates[0],
                                                        pg->layout, 0, ctx);
      } else {
         if (ctx->dd.push_state_changed[is_compute]) {
            VKCTX(UpdateDescriptorSetWithTemplate)(screen->dev, push_set, pg->dd.templates[0], ctx);
            bs->dd.sets[is_compute][0] = push_set;
         }
         assert(push_set || bs->dd.sets[is_compute][0]);
         VKCTX(CmdBindDescriptorSets)(bs->cmdbuf,
                                 is_compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
                                 pg->layout, 0, 1, push_set ? &push_set : &bs->dd.sets[is_compute][0],
                                 0, NULL);
      }
   }
   ctx->dd.push_state_changed[is_compute] = false;
   zink_descriptors_update_masked(ctx, is_compute, changed_sets, bind_sets);
   if (pg->dd.bindless && unlikely(!ctx->dd.bindless_bound)) {
      VKCTX(CmdBindDescriptorSets)(ctx->batch.state->cmdbuf, is_compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS,
                                   pg->layout, ZINK_DESCRIPTOR_BINDLESS, 1, &ctx->dd.bindless_set,
                                   0, NULL);
      ctx->dd.bindless_bound = true;
   }
   bs->dd.pg[is_compute] = pg;
   ctx->dd.pg[is_compute] = pg;
   bs->dd.compat_id[is_compute] = pg->compat_id;
   ctx->dd.state_changed[is_compute] = 0;
}

void
zink_context_invalidate_descriptor_state(struct zink_context *ctx, gl_shader_stage shader, enum zink_descriptor_type type, unsigned start, unsigned count)
{
   if (type == ZINK_DESCRIPTOR_TYPE_UBO && !start)
      ctx->dd.push_state_changed[shader == MESA_SHADER_COMPUTE] = true;
   else {
      if (zink_screen(ctx->base.screen)->compact_descriptors && type > ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW)
         type -= ZINK_DESCRIPTOR_COMPACT;
      ctx->dd.state_changed[shader == MESA_SHADER_COMPUTE] |= BITFIELD_BIT(type);
   }
}

static void
clear_multi_pool_overflow(struct zink_screen *screen, struct util_dynarray *overflowed_pools)
{
   while (util_dynarray_num_elements(overflowed_pools, struct zink_descriptor_pool*)) {
      struct zink_descriptor_pool *pool = util_dynarray_pop(overflowed_pools, struct zink_descriptor_pool*);
      pool_destroy(screen, pool);
   }
}

static void
deinit_multi_pool_overflow(struct zink_screen *screen, struct zink_descriptor_pool_multi *mpool)
{
   for (unsigned i = 0; i < 2; i++) {
      clear_multi_pool_overflow(screen, &mpool->overflowed_pools[i]);
      util_dynarray_fini(&mpool->overflowed_pools[i]);
   }
}

void
zink_batch_descriptor_deinit(struct zink_screen *screen, struct zink_batch_state *bs)
{
   for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPES; i++) {
      while (util_dynarray_contains(&bs->dd.pools[i], struct zink_descriptor_pool_multi *)) {
         struct zink_descriptor_pool_multi *mpool = util_dynarray_pop(&bs->dd.pools[i], struct zink_descriptor_pool_multi *);
         if (mpool) {
            deinit_multi_pool_overflow(screen, mpool);
            multi_pool_destroy(screen, mpool);
         }
      }
      util_dynarray_fini(&bs->dd.pools[i]);
   }
   for (unsigned i = 0; i < 2; i++) {
      if (bs->dd.push_pool[0].pool)
         pool_destroy(screen, bs->dd.push_pool[i].pool);
      deinit_multi_pool_overflow(screen, &bs->dd.push_pool[i]);
   }
}

void
zink_batch_descriptor_reset(struct zink_screen *screen, struct zink_batch_state *bs)
{
   for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPES; i++) {
      struct zink_descriptor_pool_multi **mpools = bs->dd.pools[i].data;
      unsigned count = util_dynarray_num_elements(&bs->dd.pools[i], struct zink_descriptor_pool_multi *);
      for (unsigned j = 0; j < count; j++) {
         struct zink_descriptor_pool_multi *mpool = mpools[j];
         if (!mpool)
            continue;
         if (mpool->pool->set_idx)
            mpool->overflow_idx = !mpool->overflow_idx;
         if (mpool->pool_key->use_count)
            mpool->pool->set_idx = 0;
         else {
            multi_pool_destroy(screen, mpool);
            mpools[j] = NULL;
         }
      }
   }
   for (unsigned i = 0; i < 2; i++) {
      bs->dd.pg[i] = NULL;
      if (bs->dd.push_pool[i].reinit_overflow) {
         /* these don't match current fbfetch usage and can never be used again */
         clear_multi_pool_overflow(screen, &bs->dd.push_pool[i].overflowed_pools[bs->dd.push_pool[i].overflow_idx]);
      } else if (bs->dd.push_pool[i].pool && bs->dd.push_pool[i].pool->set_idx) {
         bs->dd.push_pool[i].overflow_idx = !bs->dd.push_pool[i].overflow_idx;
      }
      if (bs->dd.push_pool[i].pool)
         bs->dd.push_pool[i].pool->set_idx = 0;
   }
}

bool
zink_batch_descriptor_init(struct zink_screen *screen, struct zink_batch_state *bs)
{
   for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPES; i++)
      util_dynarray_init(&bs->dd.pools[i], bs);
   if (!screen->info.have_KHR_push_descriptor) {
      for (unsigned i = 0; i < 2; i++) {
         bs->dd.push_pool[i].pool = create_push_pool(screen, bs, i, false);
         util_dynarray_init(&bs->dd.push_pool[i].overflowed_pools[0], bs);
         util_dynarray_init(&bs->dd.push_pool[i].overflowed_pools[1], bs);
      }
   }
   return true;
}

static void
init_push_template_entry(VkDescriptorUpdateTemplateEntry *entry, unsigned i)
{
   entry->dstBinding = i;
   entry->descriptorCount = 1;
   entry->descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
   entry->offset = offsetof(struct zink_context, di.ubos[i][0]);
   entry->stride = sizeof(VkDescriptorBufferInfo);
}

bool
zink_descriptors_init(struct zink_context *ctx)
{
   for (unsigned i = 0; i < ZINK_GFX_SHADER_COUNT; i++) {
      VkDescriptorUpdateTemplateEntry *entry = &ctx->dd.push_entries[i];
      init_push_template_entry(entry, i);
   }
   init_push_template_entry(&ctx->dd.compute_push_entry, MESA_SHADER_COMPUTE);
   VkDescriptorUpdateTemplateEntry *entry = &ctx->dd.push_entries[ZINK_GFX_SHADER_COUNT]; //fbfetch
   entry->dstBinding = ZINK_FBFETCH_BINDING;
   entry->descriptorCount = 1;
   entry->descriptorType = VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
   entry->offset = offsetof(struct zink_context, di.fbfetch);
   entry->stride = sizeof(VkDescriptorImageInfo);
   struct zink_descriptor_layout_key *layout_key;
   if (!zink_descriptor_util_push_layouts_get(ctx, ctx->dd.push_dsl, ctx->dd.push_layout_keys))
      return false;

   ctx->dd.dummy_dsl = zink_descriptor_util_layout_get(ctx, 0, NULL, 0, &layout_key);
   if (!ctx->dd.dummy_dsl)
      return false;

   return true;
}

void
zink_descriptors_deinit(struct zink_context *ctx)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   if (ctx->dd.push_dsl[0])
      VKSCR(DestroyDescriptorSetLayout)(screen->dev, ctx->dd.push_dsl[0]->layout, NULL);
   if (ctx->dd.push_dsl[1])
      VKSCR(DestroyDescriptorSetLayout)(screen->dev, ctx->dd.push_dsl[1]->layout, NULL);
}

bool
zink_descriptor_layouts_init(struct zink_context *ctx)
{
   for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPES; i++) {
      if (!_mesa_hash_table_init(&ctx->desc_set_layouts[i], ctx, hash_descriptor_layout, equals_descriptor_layout))
         return false;
      if (!_mesa_set_init(&ctx->desc_pool_keys[i], ctx, hash_descriptor_pool_key, equals_descriptor_pool_key))
         return false;
   }
   simple_mtx_init(&ctx->desc_set_layouts_lock, mtx_plain);
   simple_mtx_init(&ctx->desc_pool_keys_lock, mtx_plain);
   return true;
}

void
zink_descriptor_layouts_deinit(struct zink_context *ctx)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   for (unsigned i = 0; i < ZINK_DESCRIPTOR_TYPES; i++) {
      hash_table_foreach(&ctx->desc_set_layouts[i], he) {
         struct zink_descriptor_layout *layout = he->data;
         VKSCR(DestroyDescriptorSetLayout)(screen->dev, layout->layout, NULL);
         ralloc_free(layout);
         _mesa_hash_table_remove(&ctx->desc_set_layouts[i], he);
      }
   }
   simple_mtx_destroy(&ctx->desc_set_layouts_lock);
   simple_mtx_destroy(&ctx->desc_pool_keys_lock);
}


void
zink_descriptor_util_init_fbfetch(struct zink_context *ctx)
{
   if (ctx->dd.has_fbfetch)
      return;

   struct zink_screen *screen = zink_screen(ctx->base.screen);
   VKSCR(DestroyDescriptorSetLayout)(screen->dev, ctx->dd.push_dsl[0]->layout, NULL);
   //don't free these now, let ralloc free on teardown to avoid invalid access
   //ralloc_free(ctx->dd.push_dsl[0]);
   //ralloc_free(ctx->dd.push_layout_keys[0]);
   ctx->dd.push_dsl[0] = create_gfx_layout(ctx, &ctx->dd.push_layout_keys[0], true);
   ctx->dd.has_fbfetch = true;
}

ALWAYS_INLINE static VkDescriptorType
type_from_bindless_index(unsigned idx)
{
   switch (idx) {
   case 0: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
   case 1: return VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
   case 2: return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
   case 3: return VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
   default:
      unreachable("unknown index");
   }
}

void
zink_descriptors_init_bindless(struct zink_context *ctx)
{
   if (ctx->dd.bindless_set)
      return;

   struct zink_screen *screen = zink_screen(ctx->base.screen);
   VkDescriptorSetLayoutBinding bindings[4];
   const unsigned num_bindings = 4;
   VkDescriptorSetLayoutCreateInfo dcslci = {0};
   dcslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
   dcslci.pNext = NULL;
   VkDescriptorSetLayoutBindingFlagsCreateInfo fci = {0};
   VkDescriptorBindingFlags flags[4];
   dcslci.pNext = &fci;
   dcslci.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
   fci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO;
   fci.bindingCount = num_bindings;
   fci.pBindingFlags = flags;
   for (unsigned i = 0; i < num_bindings; i++) {
      flags[i] = VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT | VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_UNUSED_WHILE_PENDING_BIT;
   }
   for (unsigned i = 0; i < num_bindings; i++) {
      bindings[i].binding = i;
      bindings[i].descriptorType = type_from_bindless_index(i);
      bindings[i].descriptorCount = ZINK_MAX_BINDLESS_HANDLES;
      bindings[i].stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS | VK_SHADER_STAGE_COMPUTE_BIT;
      bindings[i].pImmutableSamplers = NULL;
   }
   
   dcslci.bindingCount = num_bindings;
   dcslci.pBindings = bindings;
   VkResult result = VKSCR(CreateDescriptorSetLayout)(screen->dev, &dcslci, 0, &ctx->dd.bindless_layout);
   if (result != VK_SUCCESS) {
      mesa_loge("ZINK: vkCreateDescriptorSetLayout failed (%s)", vk_Result_to_str(result));
      return;
   }

   VkDescriptorPoolCreateInfo dpci = {0};
   VkDescriptorPoolSize sizes[4];
   for (unsigned i = 0; i < 4; i++) {
      sizes[i].type = type_from_bindless_index(i);
      sizes[i].descriptorCount = ZINK_MAX_BINDLESS_HANDLES;
   }
   dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
   dpci.pPoolSizes = sizes;
   dpci.poolSizeCount = 4;
   dpci.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
   dpci.maxSets = 1;
   result = VKSCR(CreateDescriptorPool)(screen->dev, &dpci, 0, &ctx->dd.bindless_pool);
   if (result != VK_SUCCESS) {
      mesa_loge("ZINK: vkCreateDescriptorPool failed (%s)", vk_Result_to_str(result));
      return;
   }

   zink_descriptor_util_alloc_sets(screen, ctx->dd.bindless_layout, ctx->dd.bindless_pool, &ctx->dd.bindless_set, 1);
}

void
zink_descriptors_deinit_bindless(struct zink_context *ctx)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   if (ctx->dd.bindless_layout)
      VKSCR(DestroyDescriptorSetLayout)(screen->dev, ctx->dd.bindless_layout, NULL);
   if (ctx->dd.bindless_pool)
      VKSCR(DestroyDescriptorPool)(screen->dev, ctx->dd.bindless_pool, NULL);
}

void
zink_descriptors_update_bindless(struct zink_context *ctx)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   for (unsigned i = 0; i < 2; i++) {
      if (!ctx->di.bindless_dirty[i])
         continue;
      while (util_dynarray_contains(&ctx->di.bindless[i].updates, uint32_t)) {
         uint32_t handle = util_dynarray_pop(&ctx->di.bindless[i].updates, uint32_t);
         bool is_buffer = ZINK_BINDLESS_IS_BUFFER(handle);
         VkWriteDescriptorSet wd;
         wd.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
         wd.pNext = NULL;
         wd.dstSet = ctx->dd.bindless_set;
         wd.dstBinding = is_buffer ? i * 2 + 1: i * 2;
         wd.dstArrayElement = is_buffer ? handle - ZINK_MAX_BINDLESS_HANDLES : handle;
         wd.descriptorCount = 1;
         wd.descriptorType = type_from_bindless_index(wd.dstBinding);
         if (is_buffer)
            wd.pTexelBufferView = &ctx->di.bindless[i].buffer_infos[wd.dstArrayElement];
         else
            wd.pImageInfo = &ctx->di.bindless[i].img_infos[handle];
         VKSCR(UpdateDescriptorSets)(screen->dev, 1, &wd, 0, NULL);
      }
   }
   ctx->di.any_bindless_dirty = 0;
}
