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

#include "zink_types.h"
#ifdef __cplusplus
extern "C" {
#endif


struct gfx_pipeline_cache_entry {
   struct zink_gfx_pipeline_state state;
   VkPipeline pipeline;
};

struct compute_pipeline_cache_entry {
   struct zink_compute_pipeline_state state;
   VkPipeline pipeline;
};

#define ZINK_MAX_INLINED_VARIANTS 5

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
zink_program_descriptor_is_buffer(struct zink_context *ctx, gl_shader_stage stage, enum zink_descriptor_type type, unsigned i);

void
zink_gfx_program_update(struct zink_context *ctx);


uint32_t hash_gfx_output(const void *key);
uint32_t hash_gfx_input(const void *key);
uint32_t hash_gfx_input_dynamic(const void *key);

struct zink_gfx_program *
zink_create_gfx_program(struct zink_context *ctx,
                        struct zink_shader **stages,
                        unsigned vertices_per_patch);

void
zink_destroy_gfx_program(struct zink_context *ctx,
                         struct zink_gfx_program *prog);

void
zink_program_init(struct zink_context *ctx);

uint32_t
zink_program_get_descriptor_usage(struct zink_context *ctx, gl_shader_stage stage, enum zink_descriptor_type type);

void
debug_describe_zink_gfx_program(char* buf, const struct zink_gfx_program *ptr);

static inline bool
zink_gfx_program_reference(struct zink_context *ctx,
                           struct zink_gfx_program **dst,
                           struct zink_gfx_program *src)
{
   struct zink_gfx_program *old_dst = dst ? *dst : NULL;
   bool ret = false;

   if (pipe_reference_described(old_dst ? &old_dst->base.reference : NULL, &src->base.reference,
                                (debug_reference_descriptor)debug_describe_zink_gfx_program)) {
      zink_destroy_gfx_program(ctx, old_dst);
      ret = true;
   }
   if (dst) *dst = src;
   return ret;
}

void
zink_destroy_compute_program(struct zink_context *ctx,
                             struct zink_compute_program *comp);

void
debug_describe_zink_compute_program(char* buf, const struct zink_compute_program *ptr);

static inline bool
zink_compute_program_reference(struct zink_context *ctx,
                           struct zink_compute_program **dst,
                           struct zink_compute_program *src)
{
   struct zink_compute_program *old_dst = dst ? *dst : NULL;
   bool ret = false;

   if (pipe_reference_described(old_dst ? &old_dst->base.reference : NULL, &src->base.reference,
                                (debug_reference_descriptor)debug_describe_zink_compute_program)) {
      zink_destroy_compute_program(ctx, old_dst);
      ret = true;
   }
   if (dst) *dst = src;
   return ret;
}

static inline bool
zink_program_reference(struct zink_context *ctx,
                       struct zink_program **dst,
                       struct zink_program *src)
{
   struct zink_program *pg = src ? src : dst ? *dst : NULL;
   if (!pg)
      return false;
   if (pg->is_compute) {
      struct zink_compute_program *comp = (struct zink_compute_program*)pg;
      return zink_compute_program_reference(ctx, &comp, NULL);
   } else {
      struct zink_gfx_program *prog = (struct zink_gfx_program*)pg;
      return zink_gfx_program_reference(ctx, &prog, NULL);
   }
}

VkPipelineLayout
zink_pipeline_layout_create(struct zink_screen *screen, struct zink_program *pg, uint32_t *compat);

void
zink_program_update_compute_pipeline_state(struct zink_context *ctx, struct zink_compute_program *comp, const uint block[3]);
void
zink_update_compute_program(struct zink_context *ctx);
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
   ctx->dirty_shader_stages |= BITFIELD_BIT(MESA_SHADER_FRAGMENT);
   return (struct zink_fs_key *)&ctx->gfx_pipeline_state.shader_keys.key[MESA_SHADER_FRAGMENT];
}

static inline const struct zink_fs_key *
zink_get_fs_key(struct zink_context *ctx)
{
   return (const struct zink_fs_key *)&ctx->gfx_pipeline_state.shader_keys.key[MESA_SHADER_FRAGMENT];
}

static inline bool
zink_set_tcs_key_patches(struct zink_context *ctx, uint8_t patch_vertices)
{
   struct zink_tcs_key *tcs = (struct zink_tcs_key*)&ctx->gfx_pipeline_state.shader_keys.key[MESA_SHADER_TESS_CTRL];
   if (tcs->patch_vertices == patch_vertices)
      return false;
   ctx->dirty_shader_stages |= BITFIELD_BIT(MESA_SHADER_TESS_CTRL);
   tcs->patch_vertices = patch_vertices;
   return true;
}

static inline const struct zink_tcs_key *
zink_get_tcs_key(struct zink_context *ctx)
{
   return (const struct zink_tcs_key *)&ctx->gfx_pipeline_state.shader_keys.key[MESA_SHADER_TESS_CTRL];
}

void
zink_update_fs_key_samples(struct zink_context *ctx);

static inline struct zink_vs_key *
zink_set_vs_key(struct zink_context *ctx)
{
   ctx->dirty_shader_stages |= BITFIELD_BIT(MESA_SHADER_VERTEX);
   return (struct zink_vs_key *)&ctx->gfx_pipeline_state.shader_keys.key[MESA_SHADER_VERTEX];
}

static inline const struct zink_vs_key *
zink_get_vs_key(struct zink_context *ctx)
{
   return (const struct zink_vs_key *)&ctx->gfx_pipeline_state.shader_keys.key[MESA_SHADER_VERTEX];
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

bool
zink_set_rasterizer_discard(struct zink_context *ctx, bool disable);
void
zink_driver_thread_add_job(struct pipe_screen *pscreen, void *data,
                           struct util_queue_fence *fence,
                           pipe_driver_thread_func execute,
                           pipe_driver_thread_func cleanup,
                           const size_t job_size);
equals_gfx_pipeline_state_func
zink_get_gfx_pipeline_eq_func(struct zink_screen *screen, struct zink_gfx_program *prog);
#ifdef __cplusplus
}
#endif

#endif
