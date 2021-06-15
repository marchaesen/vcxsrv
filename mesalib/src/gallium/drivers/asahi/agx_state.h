/*
 * Copyright 2021 Alyssa Rosenzweig
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
#include "asahi/lib/agx_pack.h"
#include "asahi/lib/agx_bo.h"
#include "asahi/lib/agx_device.h"
#include "asahi/lib/pool.h"
#include "asahi/compiler/agx_compile.h"
#include "util/hash_table.h"
#include "util/bitset.h"

struct agx_compiled_shader {
   /* Mapped executable memory */
   struct agx_bo *bo;

   /* Varying descriptor (TODO: is this the right place?) */
   uint64_t varyings;

   /* # of varyings (currently vec4, should probably be changed) */
   unsigned varying_count;

   /* Metadata returned from the compiler */
   struct agx_shader_info info;
};

struct agx_uncompiled_shader {
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

   /* BOs for bound samplers. This is all the information we need at
    * draw time to assemble the pipeline */
   struct agx_bo *samplers[PIPE_MAX_SAMPLERS];

   /* Sampler views need the full CSO due to Gallium state management */
   struct agx_sampler_view *textures[PIPE_MAX_SHADER_SAMPLER_VIEWS];
   unsigned texture_count;
};

struct agx_batch {
   unsigned width, height, nr_cbufs;
   struct pipe_surface *cbufs[8];
   struct pipe_surface *zsbuf;

   /* PIPE_CLEAR_* bitmask */
   uint32_t clear, draw;

   float clear_color[4];

   /* Resource list requirements, represented as a bit set indexed by BO
    * handles (GEM handles on Linux, or IOGPU's equivalent on macOS) */
   BITSET_WORD bo_list[256];

   struct agx_pool pool, pipeline_pool;
   struct agx_bo *encoder;
   uint8_t *encoder_current;
};

struct agx_zsa {
   enum agx_zs_func z_func;
   bool disable_z_write;
};

#define AGX_DIRTY_VERTEX (1 << 0)

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
   struct agx_zsa zs;

   uint8_t viewport[AGX_VIEWPORT_LENGTH];
   uint8_t render_target[8][AGX_RENDER_TARGET_LENGTH];
};

static inline struct agx_context *
agx_context(struct pipe_context *pctx)
{
   return (struct agx_context *) pctx;
}

struct agx_rasterizer {
   struct pipe_rasterizer_state base;
   uint8_t cull[AGX_CULL_LENGTH];
};

struct agx_query {
   unsigned	query;
};

struct agx_sampler_view {
   struct pipe_sampler_view base;

   /* Prepared descriptor */
   struct agx_bo *desc;
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
#define DRM_FORMAT_MOD_APPLE_64X64_MORTON_ORDER (2)

struct agx_resource {
   struct pipe_resource	base;
   uint64_t modifier;

   /* Hardware backing */
   struct agx_bo *bo;

   /* Software backing (XXX) */
   struct sw_displaytarget	*dt;
   unsigned dt_stride;

   struct {
      bool data_valid;
      unsigned offset;
      unsigned line_stride;
   } slices[PIPE_MAX_TEXTURE_LEVELS];
};

static inline struct agx_resource *
agx_resource(struct pipe_resource *pctx)
{
   return (struct agx_resource *) pctx;
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

/* Add a BO to a batch. This needs to be amortized O(1) since it's called in
 * hot paths. To achieve this we model BO lists by bit sets */

static inline void
agx_batch_add_bo(struct agx_batch *batch, struct agx_bo *bo)
{
   if (unlikely(bo->handle > (sizeof(batch->bo_list) * 8)))
      unreachable("todo: growable");

   BITSET_SET(batch->bo_list, bo->handle);
}

#endif
