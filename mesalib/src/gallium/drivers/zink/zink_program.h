/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef ZINK_PROGRAM_H
#define ZINK_PROGRAM_H

#include <vulkan/vulkan.h>

#include "compiler/shader_enums.h"
#include "pipe/p_state.h"
#include "util/u_inlines.h"

#include "zink_context.h"
#include "zink_compiler.h"
#include "zink_shader_keys.h"
#ifdef __cplusplus
extern "C" {
#endif

struct zink_screen;
struct zink_shader;
struct zink_gfx_pipeline_state;
struct zink_descriptor_set;

struct hash_table;
struct set;
struct util_dynarray;

struct zink_program;

struct zink_gfx_push_constant {
   unsigned draw_mode_is_indexed;
   unsigned draw_id;
   float default_inner_level[2];
   float default_outer_level[4];
};

struct zink_cs_push_constant {
   unsigned work_dim;
};

/* a shader module is used for directly reusing a shader module between programs,
 * e.g., in the case where we're swapping out only one shader,
 * allowing us to skip going through shader keys
 */
struct zink_shader_module {
   struct list_head list;
   VkShaderModule shader;
   uint32_t hash;
   bool default_variant;
   uint8_t num_uniforms;
   uint8_t key_size;
   uint8_t key[0]; /* | key | uniforms | */
};

struct zink_program {
   struct pipe_reference reference;
   unsigned char sha1[20];
   struct util_queue_fence cache_fence;
   VkPipelineCache pipeline_cache;
   size_t pipeline_cache_size;
   struct zink_batch_usage *batch_uses;
   bool is_compute;

   struct zink_program_descriptor_data *dd;

   VkPipelineLayout layout;
   VkDescriptorSetLayout dsl[ZINK_DESCRIPTOR_TYPES + 1]; // one for each type + push
   unsigned num_dsl;

   bool removed;
};

#define ZINK_MAX_INLINED_VARIANTS 5

struct zink_gfx_program {
   struct zink_program base;

   uint32_t stages_present; //mask of stages present in this program
   struct nir_shader *nir[ZINK_SHADER_COUNT];

   struct zink_shader_module *modules[ZINK_SHADER_COUNT]; // compute stage doesn't belong here

   struct zink_shader *last_vertex_stage;

   struct list_head shader_cache[ZINK_SHADER_COUNT][2]; //normal, inline uniforms
   unsigned inlined_variant_count[ZINK_SHADER_COUNT];

   struct zink_shader *shaders[ZINK_SHADER_COUNT];
   struct hash_table pipelines[11]; // number of draw modes we support
   uint32_t default_variant_hash;
   uint32_t last_variant_hash;
};

struct zink_compute_program {
   struct zink_program base;

   struct zink_shader_module *module;
   struct zink_shader *shader;
   struct hash_table *pipelines;
};

static inline enum zink_descriptor_type
zink_desc_type_from_vktype(VkDescriptorType type)
{
   switch (type) {
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
      return ZINK_DESCRIPTOR_TYPE_UBO;
   case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
   case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
      return ZINK_DESCRIPTOR_TYPE_SAMPLER_VIEW;
   case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
      return ZINK_DESCRIPTOR_TYPE_SSBO;
   case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
   case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
      return ZINK_DESCRIPTOR_TYPE_IMAGE;
   default:
      unreachable("unhandled descriptor type");
   }
}

static inline VkPrimitiveTopology
zink_primitive_topology(enum pipe_prim_type mode)
{
   switch (mode) {
   case PIPE_PRIM_POINTS:
      return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;

   case PIPE_PRIM_LINES:
      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;

   case PIPE_PRIM_LINE_STRIP:
      return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;

   case PIPE_PRIM_TRIANGLES:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

   case PIPE_PRIM_TRIANGLE_STRIP:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;

   case PIPE_PRIM_TRIANGLE_FAN:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;

   case PIPE_PRIM_LINE_STRIP_ADJACENCY:
      return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY;

   case PIPE_PRIM_LINES_ADJACENCY:
      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST_WITH_ADJACENCY;

   case PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY;

   case PIPE_PRIM_TRIANGLES_ADJACENCY:
      return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST_WITH_ADJACENCY;

   case PIPE_PRIM_PATCHES:
      return VK_PRIMITIVE_TOPOLOGY_PATCH_LIST;

   default:
      unreachable("unexpected enum pipe_prim_type");
   }
}

void
zink_delete_shader_state(struct pipe_context *pctx, void *cso);
void *
zink_create_gfx_shader_state(struct pipe_context *pctx, const struct pipe_shader_state *shader);

unsigned
zink_program_num_bindings_typed(const struct zink_program *pg, enum zink_descriptor_type type, bool is_compute);

unsigned
zink_program_num_bindings(const struct zink_program *pg, bool is_compute);

bool
zink_program_descriptor_is_buffer(struct zink_context *ctx, enum pipe_shader_type stage, enum zink_descriptor_type type, unsigned i);

void
zink_update_gfx_program(struct zink_context *ctx, struct zink_gfx_program *prog);

struct zink_gfx_program *
zink_create_gfx_program(struct zink_context *ctx,
                        struct zink_shader *stages[ZINK_SHADER_COUNT],
                        unsigned vertices_per_patch);

void
zink_destroy_gfx_program(struct zink_screen *screen,
                         struct zink_gfx_program *prog);

VkPipeline
zink_get_gfx_pipeline(struct zink_context *ctx,
                      struct zink_gfx_program *prog,
                      struct zink_gfx_pipeline_state *state,
                      enum pipe_prim_type mode);

void
zink_program_init(struct zink_context *ctx);

uint32_t
zink_program_get_descriptor_usage(struct zink_context *ctx, enum pipe_shader_type stage, enum zink_descriptor_type type);

void
debug_describe_zink_gfx_program(char* buf, const struct zink_gfx_program *ptr);

static inline bool
zink_gfx_program_reference(struct zink_screen *screen,
                           struct zink_gfx_program **dst,
                           struct zink_gfx_program *src)
{
   struct zink_gfx_program *old_dst = dst ? *dst : NULL;
   bool ret = false;

   if (pipe_reference_described(old_dst ? &old_dst->base.reference : NULL, &src->base.reference,
                                (debug_reference_descriptor)debug_describe_zink_gfx_program)) {
      zink_destroy_gfx_program(screen, old_dst);
      ret = true;
   }
   if (dst) *dst = src;
   return ret;
}

struct zink_compute_program *
zink_create_compute_program(struct zink_context *ctx, struct zink_shader *shader);
void
zink_destroy_compute_program(struct zink_screen *screen,
                         struct zink_compute_program *comp);

void
debug_describe_zink_compute_program(char* buf, const struct zink_compute_program *ptr);

static inline bool
zink_compute_program_reference(struct zink_screen *screen,
                           struct zink_compute_program **dst,
                           struct zink_compute_program *src)
{
   struct zink_compute_program *old_dst = dst ? *dst : NULL;
   bool ret = false;

   if (pipe_reference_described(old_dst ? &old_dst->base.reference : NULL, &src->base.reference,
                                (debug_reference_descriptor)debug_describe_zink_compute_program)) {
      zink_destroy_compute_program(screen, old_dst);
      ret = true;
   }
   if (dst) *dst = src;
   return ret;
}

VkPipelineLayout
zink_pipeline_layout_create(struct zink_screen *screen, struct zink_program *pg);

void
zink_program_update_compute_pipeline_state(struct zink_context *ctx, struct zink_compute_program *comp, const uint block[3]);

VkPipeline
zink_get_compute_pipeline(struct zink_screen *screen,
                      struct zink_compute_program *comp,
                      struct zink_compute_pipeline_state *state);

static inline bool
zink_program_has_descriptors(const struct zink_program *pg)
{
   return pg->num_dsl > 0;
}

static inline struct zink_fs_key *
zink_set_fs_key(struct zink_context *ctx)
{
   ctx->dirty_shader_stages |= BITFIELD_BIT(PIPE_SHADER_FRAGMENT);
   return (struct zink_fs_key *)&ctx->gfx_pipeline_state.shader_keys.key[PIPE_SHADER_FRAGMENT];
}

static inline const struct zink_fs_key *
zink_get_fs_key(struct zink_context *ctx)
{
   return (const struct zink_fs_key *)&ctx->gfx_pipeline_state.shader_keys.key[PIPE_SHADER_FRAGMENT];
}

static inline struct zink_vs_key *
zink_set_vs_key(struct zink_context *ctx)
{
   ctx->dirty_shader_stages |= BITFIELD_BIT(PIPE_SHADER_VERTEX);
   return (struct zink_vs_key *)&ctx->gfx_pipeline_state.shader_keys.key[PIPE_SHADER_VERTEX];
}

static inline const struct zink_vs_key *
zink_get_vs_key(struct zink_context *ctx)
{
   return (const struct zink_vs_key *)&ctx->gfx_pipeline_state.shader_keys.key[PIPE_SHADER_VERTEX];
}

static inline struct zink_vs_key_base *
zink_set_last_vertex_key(struct zink_context *ctx)
{
   ctx->last_vertex_stage_dirty = true;
   return (struct zink_vs_key_base *)&ctx->gfx_pipeline_state.shader_keys.last_vertex;
}

static inline const struct zink_vs_key_base *
zink_get_last_vertex_key(struct zink_context *ctx)
{
   return (const struct zink_vs_key_base *)&ctx->gfx_pipeline_state.shader_keys.last_vertex;
}

static inline void
zink_set_fs_point_coord_key(struct zink_context *ctx)
{
   const struct zink_fs_key *fs = zink_get_fs_key(ctx);
   bool disable = !ctx->gfx_pipeline_state.has_points || !ctx->rast_state->base.sprite_coord_enable;
   uint8_t coord_replace_bits = disable ? 0 : ctx->rast_state->base.sprite_coord_enable;
   bool coord_replace_yinvert = disable ? false : !!ctx->rast_state->base.sprite_coord_mode;
   if (fs->coord_replace_bits != coord_replace_bits || fs->coord_replace_yinvert != coord_replace_yinvert) {
      zink_set_fs_key(ctx)->coord_replace_bits = coord_replace_bits;
      zink_set_fs_key(ctx)->coord_replace_yinvert = coord_replace_yinvert;
   }
}

#ifdef __cplusplus
}
#endif

#endif
