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
#include "util/simple_mtx.h"

#include "zink_batch.h"
#ifdef __cplusplus
extern "C" {
#endif

#ifndef ZINK_SHADER_COUNT
#define ZINK_SHADER_COUNT (PIPE_SHADER_TYPES - 1)
#endif

enum zink_descriptor_type {
   ZINK_DESCRIPTOR_TYPE_UBO,
   ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW,
   ZINK_DESCRIPTOR_TYPE_SSBO,
   ZINK_DESCRIPTOR_TYPE_IMAGE,
   ZINK_DESCRIPTOR_TYPES,
};

#define ZINK_MAX_DESCRIPTORS_PER_TYPE (32 * ZINK_SHADER_COUNT)

struct zink_descriptor_refs {
   struct util_dynarray refs;
};


/* hashes of all the named types in a given state */
struct zink_descriptor_state {
   bool valid[ZINK_DESCRIPTOR_TYPES];
   uint32_t state[ZINK_DESCRIPTOR_TYPES];
};

enum zink_descriptor_size_index {
   ZDS_INDEX_UBO,
   ZDS_INDEX_COMBINED_SAMPLER,
   ZDS_INDEX_UNIFORM_TEXELS,
   ZDS_INDEX_STORAGE_BUFFER,
   ZDS_INDEX_STORAGE_IMAGE,
   ZDS_INDEX_STORAGE_TEXELS,
};

struct hash_table;

struct zink_context;
struct zink_image_view;
struct zink_program;
struct zink_resource;
struct zink_sampler;
struct zink_sampler_view;
struct zink_shader;
struct zink_screen;


struct zink_descriptor_state_key {
   bool exists[ZINK_SHADER_COUNT];
   uint32_t state[ZINK_SHADER_COUNT];
};

struct zink_descriptor_layout_key {
   unsigned num_descriptors;
   VkDescriptorSetLayoutBinding *bindings;
   unsigned use_count;
};

struct zink_descriptor_layout {
   VkDescriptorSetLayout layout;
   VkDescriptorUpdateTemplateKHR desc_template;
};

struct zink_descriptor_pool_key {
   struct zink_descriptor_layout_key *layout;
   unsigned num_type_sizes;
   VkDescriptorPoolSize *sizes;
};

struct zink_descriptor_reference {
   void **ref;
   bool *invalid;
};


struct zink_descriptor_data {
   struct zink_descriptor_state gfx_descriptor_states[ZINK_SHADER_COUNT]; // keep incremental hashes here
   struct zink_descriptor_state descriptor_states[2]; // gfx, compute
   struct hash_table *descriptor_pools[ZINK_DESCRIPTOR_TYPES];

   struct zink_descriptor_layout_key *push_layout_keys[2]; //gfx, compute
   struct zink_descriptor_pool *push_pool[2]; //gfx, compute
   struct zink_descriptor_layout *push_dsl[2]; //gfx, compute
   uint8_t last_push_usage[2];
   bool push_valid[2];
   uint32_t push_state[2];
   bool gfx_push_valid[ZINK_SHADER_COUNT];
   uint32_t gfx_push_state[ZINK_SHADER_COUNT];
   struct zink_descriptor_set *last_set[2];

   VkDescriptorPool dummy_pool;
   struct zink_descriptor_layout *dummy_dsl;
   VkDescriptorSet dummy_set;

   bool changed[2][ZINK_DESCRIPTOR_TYPES + 1];
   bool has_fbfetch;
   struct zink_program *pg[2]; //gfx, compute
};

struct zink_program_descriptor_data {
   uint8_t push_usage;
   VkDescriptorPoolSize sizes[6]; //zink_descriptor_size_index
   struct zink_descriptor_layout_key *layout_key[ZINK_DESCRIPTOR_TYPES]; //push set doesn't need one
   uint8_t binding_usage;
   struct zink_descriptor_layout *layouts[ZINK_DESCRIPTOR_TYPES + 1];
   VkDescriptorUpdateTemplateKHR push_template;
};

struct zink_batch_descriptor_data {
   struct set *desc_sets;
};

static inline enum zink_descriptor_size_index
zink_vktype_to_size_idx(VkDescriptorType type)
{
   switch (type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      return ZDS_INDEX_UBO;
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
      return ZDS_INDEX_COMBINED_SAMPLER;
   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      return ZDS_INDEX_UNIFORM_TEXELS;
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      return ZDS_INDEX_STORAGE_BUFFER;
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
      return ZDS_INDEX_STORAGE_IMAGE;
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      return ZDS_INDEX_STORAGE_TEXELS;
   default: break;
   }
   unreachable("unknown type");
}

static inline enum zink_descriptor_size_index
zink_descriptor_type_to_size_idx(enum zink_descriptor_type type)
{
   switch (type) {
   case ZINK_DESCRIPTOR_TYPE_UBO:
      return ZDS_INDEX_UBO;
   case ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW:
      return ZDS_INDEX_COMBINED_SAMPLER;
   case ZINK_DESCRIPTOR_TYPE_SSBO:
      return ZDS_INDEX_STORAGE_BUFFER;
   case ZINK_DESCRIPTOR_TYPE_IMAGE:
      return ZDS_INDEX_STORAGE_IMAGE;
   default: break;
   }
   unreachable("unknown type");
}
unsigned
zink_descriptor_program_num_sizes(struct zink_program *pg, enum zink_descriptor_type type);
bool
zink_descriptor_layouts_init(struct zink_context *ctx);

void
zink_descriptor_layouts_deinit(struct zink_context *ctx);

uint32_t
zink_get_sampler_view_hash(struct zink_context *ctx, struct zink_sampler_view *sampler_view, bool is_buffer);
uint32_t
zink_get_image_view_hash(struct zink_context *ctx, struct zink_image_view *image_view, bool is_buffer);
bool
zink_descriptor_util_alloc_sets(struct zink_screen *screen, VkDescriptorSetLayout dsl, VkDescriptorPool pool, VkDescriptorSet *sets, unsigned num_sets);
struct zink_descriptor_layout *
zink_descriptor_util_layout_get(struct zink_context *ctx, enum zink_descriptor_type type,
                      VkDescriptorSetLayoutBinding *bindings, unsigned num_bindings,
                      struct zink_descriptor_layout_key **layout_key);
void
zink_descriptor_util_init_fbfetch(struct zink_context *ctx);
bool
zink_descriptor_util_push_layouts_get(struct zink_context *ctx, struct zink_descriptor_layout **dsls, struct zink_descriptor_layout_key **layout_keys);
void
zink_descriptor_util_init_null_set(struct zink_context *ctx, VkDescriptorSet desc_set);
VkImageLayout
zink_descriptor_util_image_layout_eval(const struct zink_resource *res, bool is_compute);

/* these two can't be called in lazy mode */
void
zink_descriptor_set_refs_clear(struct zink_descriptor_refs *refs, void *ptr);
void
zink_descriptor_set_recycle(struct zink_descriptor_set *zds);





bool
zink_descriptor_program_init(struct zink_context *ctx, struct zink_program *pg);

void
zink_descriptor_program_deinit(struct zink_screen *screen, struct zink_program *pg);

void
zink_descriptors_update(struct zink_context *ctx, bool is_compute);


void
zink_context_invalidate_descriptor_state(struct zink_context *ctx, enum pipe_shader_type shader, enum zink_descriptor_type type, unsigned, unsigned);

uint32_t
zink_get_sampler_view_hash(struct zink_context *ctx, struct zink_sampler_view *sampler_view, bool is_buffer);
uint32_t
zink_get_image_view_hash(struct zink_context *ctx, struct zink_image_view *image_view, bool is_buffer);
struct zink_resource *
zink_get_resource_for_descriptor(struct zink_context *ctx, enum zink_descriptor_type type, enum pipe_shader_type shader, int idx);

void
zink_batch_descriptor_deinit(struct zink_screen *screen, struct zink_batch_state *bs);
void
zink_batch_descriptor_reset(struct zink_screen *screen, struct zink_batch_state *bs);
bool
zink_batch_descriptor_init(struct zink_screen *screen, struct zink_batch_state *bs);

bool
zink_descriptors_init(struct zink_context *ctx);

void
zink_descriptors_deinit(struct zink_context *ctx);

//LAZY
bool
zink_descriptor_program_init_lazy(struct zink_context *ctx, struct zink_program *pg);

void
zink_descriptor_program_deinit_lazy(struct zink_screen *screen, struct zink_program *pg);

void
zink_descriptors_update_lazy(struct zink_context *ctx, bool is_compute);


void
zink_context_invalidate_descriptor_state_lazy(struct zink_context *ctx, enum pipe_shader_type shader, enum zink_descriptor_type type, unsigned, unsigned);

void
zink_batch_descriptor_deinit_lazy(struct zink_screen *screen, struct zink_batch_state *bs);
void
zink_batch_descriptor_reset_lazy(struct zink_screen *screen, struct zink_batch_state *bs);
bool
zink_batch_descriptor_init_lazy(struct zink_screen *screen, struct zink_batch_state *bs);

bool
zink_descriptors_init_lazy(struct zink_context *ctx);

void
zink_descriptors_deinit_lazy(struct zink_context *ctx);

void
zink_descriptor_set_update_lazy(struct zink_context *ctx, struct zink_program *pg, enum zink_descriptor_type type, VkDescriptorSet set);
void
zink_descriptors_update_lazy_masked(struct zink_context *ctx, bool is_compute, uint8_t changed_sets, bool need_push, bool update_push);
#ifdef __cplusplus
}
#endif

#endif
