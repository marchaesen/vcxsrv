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

#ifndef ZINK_DESCRIPTOR_H
# define  ZINK_DESCRIPTOR_H
#include <vulkan/vulkan.h>
#include "util/u_dynarray.h"
#include "util/u_inlines.h"
#include "util/u_dynarray.h"

enum zink_descriptor_type {
   ZINK_DESCRIPTOR_TYPE_UBO,
   ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW,
   ZINK_DESCRIPTOR_TYPE_SSBO,
   ZINK_DESCRIPTOR_TYPE_IMAGE,
   ZINK_DESCRIPTOR_TYPES,
};


struct zink_descriptor_refs {
   struct util_dynarray refs;
};


#include "zink_context.h"

struct hash_table;

struct zink_program;
struct zink_resource;
struct zink_shader;


struct zink_descriptor_state_key {
   bool exists[ZINK_SHADER_COUNT];
   uint32_t state[ZINK_SHADER_COUNT];
};

struct zink_descriptor_barrier {
   struct zink_resource *res;
   VkImageLayout layout;
   VkAccessFlags access;
   VkPipelineStageFlagBits stage;
};

struct zink_descriptor_pool_key {
   unsigned num_type_sizes;
   unsigned num_descriptors;
   VkDescriptorSetLayoutBinding *bindings;
   VkDescriptorPoolSize *sizes;
};

struct zink_descriptor_pool {
   struct pipe_reference reference;
   enum zink_descriptor_type type;
   struct hash_table *desc_sets;
   struct hash_table *free_desc_sets;
   struct util_dynarray alloc_desc_sets;
   VkDescriptorPool descpool;
   VkDescriptorSetLayout dsl;
   struct zink_descriptor_pool_key key;
   unsigned num_resources;
   unsigned num_sets_allocated;
};

struct zink_descriptor_set {
   struct zink_descriptor_pool *pool;
   struct pipe_reference reference; //incremented for batch usage
   VkDescriptorSet desc_set;
   uint32_t hash;
   bool invalid;
   bool punted;
   bool recycled;
   struct zink_descriptor_state_key key;
   struct util_dynarray barriers;
   struct zink_batch_usage batch_uses;
#ifndef NDEBUG
   /* for extra debug asserts */
   unsigned num_resources;
#endif
   union {
      struct zink_resource_object **res_objs;
      struct zink_image_view **image_views;
      struct {
         struct zink_sampler_view **sampler_views;
         struct zink_sampler_state **sampler_states;
      };
   };
};


struct zink_descriptor_reference {
   void **ref;
   bool *invalid;
};
void
zink_descriptor_set_refs_clear(struct zink_descriptor_refs *refs, void *ptr);

void
zink_image_view_desc_set_add(struct zink_image_view *image_view, struct zink_descriptor_set *zds, unsigned idx);
void
zink_sampler_state_desc_set_add(struct zink_sampler_state *sampler_state, struct zink_descriptor_set *zds, unsigned idx);
void
zink_sampler_view_desc_set_add(struct zink_sampler_view *sv, struct zink_descriptor_set *zds, unsigned idx);
void
zink_resource_desc_set_add(struct zink_resource *res, struct zink_descriptor_set *zds, unsigned idx);

struct zink_descriptor_set *
zink_descriptor_set_get(struct zink_context *ctx,
                               enum zink_descriptor_type type,
                               bool is_compute,
                               bool *cache_hit,
                               bool *need_resource_refs);
void
zink_descriptor_set_recycle(struct zink_descriptor_set *zds);

bool
zink_descriptor_program_init(struct zink_context *ctx,
                       struct zink_shader *stages[ZINK_SHADER_COUNT],
                       struct zink_program *pg);

void
zink_descriptor_set_invalidate(struct zink_descriptor_set *zds);

void
zink_descriptor_pool_free(struct zink_screen *screen, struct zink_descriptor_pool *pool);

void
zink_descriptor_pool_deinit(struct zink_context *ctx);

bool
zink_descriptor_pool_init(struct zink_context *ctx);


void
debug_describe_zink_descriptor_pool(char* buf, const struct zink_descriptor_pool *ptr);

static inline void
zink_descriptor_pool_reference(struct zink_screen *screen,
                               struct zink_descriptor_pool **dst,
                               struct zink_descriptor_pool *src)
{
   struct zink_descriptor_pool *old_dst = dst ? *dst : NULL;

   if (pipe_reference_described(old_dst ? &old_dst->reference : NULL, &src->reference,
                                (debug_reference_descriptor)debug_describe_zink_descriptor_pool))
      zink_descriptor_pool_free(screen, old_dst);
   if (dst) *dst = src;
}
#endif
