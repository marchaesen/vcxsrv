/*
 * Copyright 2021 Alyssa Rosenzweig
 * Copyright (C) 2019-2021 Collabora, Ltd.
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

#ifndef AGX_STATE_H
#define AGX_STATE_H

#include "gallium/include/pipe/p_context.h"
#include "gallium/include/pipe/p_state.h"
#include "gallium/include/pipe/p_screen.h"
#include "gallium/auxiliary/util/u_blitter.h"
#include "asahi/lib/agx_pack.h"
#include "asahi/lib/agx_bo.h"
#include "asahi/lib/agx_device.h"
#include "asahi/lib/pool.h"
#include "asahi/compiler/agx_compile.h"
#include "asahi/layout/layout.h"
#include "compiler/nir/nir_lower_blend.h"
#include "util/hash_table.h"
#include "util/bitset.h"

struct agx_streamout_target {
   struct pipe_stream_output_target base;
   uint32_t offset;
};

struct agx_streamout {
   struct pipe_stream_output_target *targets[PIPE_MAX_SO_BUFFERS];
   unsigned num_targets;
};

static inline struct agx_streamout_target *
agx_so_target(struct pipe_stream_output_target *target)
{
   return (struct agx_streamout_target *)target;
}

struct agx_compiled_shader {
   /* Mapped executable memory */
   struct agx_bo *bo;

   /* Metadata returned from the compiler */
   struct agx_shader_info info;
};

struct agx_uncompiled_shader {
   struct pipe_shader_state base;
   struct nir_shader *nir;
   struct hash_table *variants;

   /* Set on VS, passed to FS for linkage */
   unsigned base_varying;
};

struct agx_stage {
   struct agx_uncompiled_shader *shader;
   uint32_t dirty;

   struct pipe_constant_buffer cb[PIPE_MAX_CONSTANT_BUFFERS];
   uint32_t cb_mask;

   /* Need full CSOs for u_blitter */
   struct agx_sampler_state *samplers[PIPE_MAX_SAMPLERS];
   struct agx_sampler_view *textures[PIPE_MAX_SHADER_SAMPLER_VIEWS];

   unsigned sampler_count, texture_count;
};

/* Uploaded scissor or depth bias descriptors */
struct agx_array {
      struct agx_bo *bo;
      unsigned count;
};

struct agx_batch {
   unsigned width, height, nr_cbufs;
   struct pipe_surface *cbufs[8];
   struct pipe_surface *zsbuf;

   /* PIPE_CLEAR_* bitmask */
   uint32_t clear, draw, load;

   /* Base of uploaded texture descriptors */
   uint64_t textures;

   float clear_color[4];
   double clear_depth;
   unsigned clear_stencil;

   /* Whether we're drawing points, lines, or triangles */
   enum pipe_prim_type reduced_prim;

   /* Current varyings linkage structures */
   uint32_t varyings;

   /* Resource list requirements, represented as a bit set indexed by BO
    * handles (GEM handles on Linux, or IOGPU's equivalent on macOS)
    */
   struct {
      BITSET_WORD *set;
      unsigned word_count;
   } bo_list;

   struct agx_pool pool, pipeline_pool;
   struct agx_bo *encoder;
   uint8_t *encoder_current;
   uint8_t *encoder_end;

   struct agx_array scissor, depth_bias;
};

struct agx_zsa {
   struct pipe_depth_stencil_alpha_state base;
   struct agx_fragment_face_packed depth;
   struct agx_fragment_stencil_packed front_stencil, back_stencil;
};

struct agx_blend {
   bool logicop_enable, blend_enable;

   union {
      nir_lower_blend_rt rt[8];
      unsigned logicop_func;
   };
};

struct asahi_shader_key {
   struct agx_shader_key base;
   struct agx_blend blend;
   unsigned nr_cbufs;
   uint8_t clip_plane_enable;
   enum pipe_format rt_formats[PIPE_MAX_COLOR_BUFS];
};

enum agx_dirty {
   AGX_DIRTY_VERTEX   = BITFIELD_BIT(0),
   AGX_DIRTY_VIEWPORT = BITFIELD_BIT(1),
   AGX_DIRTY_SCISSOR_ZBIAS  = BITFIELD_BIT(2),
   AGX_DIRTY_ZS        = BITFIELD_BIT(3),
   AGX_DIRTY_STENCIL_REF = BITFIELD_BIT(4),
   AGX_DIRTY_RS         = BITFIELD_BIT(5),
   AGX_DIRTY_SPRITE_COORD_MODE = BITFIELD_BIT(6),
   AGX_DIRTY_PRIM       = BITFIELD_BIT(7),

   /* Vertex/fragment pipelines, including uniforms and textures */
   AGX_DIRTY_VS         = BITFIELD_BIT(8),
   AGX_DIRTY_FS         = BITFIELD_BIT(9),

   /* Just the progs themselves */
   AGX_DIRTY_VS_PROG    = BITFIELD_BIT(10),
   AGX_DIRTY_FS_PROG    = BITFIELD_BIT(11),
};

struct agx_context {
   struct pipe_context base;
   struct agx_compiled_shader *vs, *fs;
   uint32_t dirty;

   struct agx_batch *batch;

   struct pipe_vertex_buffer vertex_buffers[PIPE_MAX_ATTRIBS];
   uint32_t vb_mask;

   struct agx_stage stage[PIPE_SHADER_TYPES];
   struct agx_attribute *attributes;
   struct agx_rasterizer *rast;
   struct agx_zsa *zs;
   struct agx_blend *blend;
   struct pipe_blend_color blend_color;
   struct pipe_viewport_state viewport;
   struct pipe_scissor_state scissor;
   struct pipe_stencil_ref stencil_ref;
   struct agx_streamout streamout;
   uint16_t sample_mask;
   struct pipe_framebuffer_state framebuffer;

   struct pipe_query *cond_query;
   bool cond_cond;
   enum pipe_render_cond_flag cond_mode;

   bool is_noop;

   uint8_t render_target[8][AGX_RENDER_TARGET_LENGTH];

   struct blitter_context *blitter;
};

static inline struct agx_context *
agx_context(struct pipe_context *pctx)
{
   return (struct agx_context *) pctx;
}

static inline void
agx_dirty_all(struct agx_context *ctx)
{
   ctx->dirty = ~0;

   for (unsigned i = 0; i < ARRAY_SIZE(ctx->stage); ++i)
      ctx->stage[i].dirty = ~0;
}

struct agx_rasterizer {
   struct pipe_rasterizer_state base;
   uint8_t cull[AGX_CULL_LENGTH];
   uint8_t line_width;
};

struct agx_query {
   unsigned	query;
};

struct agx_sampler_state {
   struct pipe_sampler_state base;

   /* Prepared descriptor */
   struct agx_sampler_packed desc;
};

struct agx_sampler_view {
   struct pipe_sampler_view base;

   /* Prepared descriptor */
   struct agx_texture_packed desc;
};

struct agx_screen {
   struct pipe_screen pscreen;
   struct agx_device dev;
   struct sw_winsys *winsys;
};

static inline struct agx_screen *
agx_screen(struct pipe_screen *p)
{
   return (struct agx_screen *)p;
}

static inline struct agx_device *
agx_device(struct pipe_screen *p)
{
   return &(agx_screen(p)->dev);
}

/* TODO: UABI, fake for macOS */
#ifndef DRM_FORMAT_MOD_LINEAR
#define DRM_FORMAT_MOD_LINEAR 1
#endif
#define DRM_FORMAT_MOD_APPLE_TWIDDLED (2)

struct agx_resource {
   struct pipe_resource	base;
   uint64_t modifier;

   /* Should probably be part of the modifier. Affects the tiling algorithm, or
    * something like that.
    */
   bool mipmapped;

   /* Hardware backing */
   struct agx_bo *bo;

   /* Software backing (XXX) */
   struct sw_displaytarget	*dt;
   unsigned dt_stride;

   BITSET_DECLARE(data_valid, PIPE_MAX_TEXTURE_LEVELS);

   struct ail_layout layout;

   /* Metal does not support packed depth/stencil formats; presumably AGX does
    * not either. Instead, we create separate depth and stencil resources,
    * managed by u_transfer_helper.  We provide the illusion of packed
    * resources.
    */
   struct agx_resource *separate_stencil;
};

static inline struct agx_resource *
agx_resource(struct pipe_resource *pctx)
{
   return (struct agx_resource *) pctx;
}

static inline void *
agx_map_texture_cpu(struct agx_resource *rsrc, unsigned level, unsigned z)
{
   return ((uint8_t *) rsrc->bo->ptr.cpu) +
          ail_get_layer_level_B(&rsrc->layout, z, level);
}

static inline uint64_t
agx_map_texture_gpu(struct agx_resource *rsrc, unsigned z)
{
   return rsrc->bo->ptr.gpu +
          (uint64_t) ail_get_layer_offset_B(&rsrc->layout, z);
}

struct agx_transfer {
   struct pipe_transfer base;
   void *map;
   struct {
      struct pipe_resource *rsrc;
      struct pipe_box box;
   } staging;
};

static inline struct agx_transfer *
agx_transfer(struct pipe_transfer *p)
{
   return (struct agx_transfer *)p;
}

uint64_t
agx_push_location(struct agx_context *ctx, struct agx_push push,
                  enum pipe_shader_type stage);

uint64_t
agx_build_clear_pipeline(struct agx_context *ctx, uint32_t code, uint64_t clear_buf);

uint64_t
agx_build_store_pipeline(struct agx_context *ctx, uint32_t code,
                         uint64_t render_target);

uint64_t
agx_build_reload_pipeline(struct agx_context *ctx, uint32_t code, struct pipe_surface *surf);

/* Add a BO to a batch. This needs to be amortized O(1) since it's called in
 * hot paths. To achieve this we model BO lists by bit sets */

static unsigned
agx_batch_bo_list_bits(struct agx_batch *batch)
{
   return batch->bo_list.word_count * sizeof(BITSET_WORD) * 8;
}

static inline void
agx_batch_add_bo(struct agx_batch *batch, struct agx_bo *bo)
{
   /* Double the size of the BO list if we run out, this is amortized O(1) */
   if (unlikely(bo->handle > agx_batch_bo_list_bits(batch))) {
      batch->bo_list.set = rerzalloc(batch, batch->bo_list.set, BITSET_WORD,
                                     batch->bo_list.word_count,
                                     batch->bo_list.word_count * 2);
      batch->bo_list.word_count *= 2;
   }

   BITSET_SET(batch->bo_list.set, bo->handle);
}

static unsigned
agx_batch_num_bo(struct agx_batch *batch)
{
   return __bitset_count(batch->bo_list.set, batch->bo_list.word_count);
}

#define AGX_BATCH_FOREACH_BO_HANDLE(batch, handle) \
   BITSET_FOREACH_SET(handle, (batch)->bo_list.set, agx_batch_bo_list_bits(batch))

/* Blit shaders */
void
agx_blitter_save(struct agx_context *ctx, struct blitter_context *blitter,
                 bool render_cond);

void agx_blit(struct pipe_context *pipe,
              const struct pipe_blit_info *info);

void agx_internal_shaders(struct agx_device *dev);

/* Batch logic */
static void
agx_flush_all(struct agx_context *ctx, const char *reason)
{
   //printf("Flushing due to: %s\n", reason);
   ctx->base.flush(&ctx->base, NULL, 0);
}

void
agx_batch_init_state(struct agx_batch *batch);

#endif
