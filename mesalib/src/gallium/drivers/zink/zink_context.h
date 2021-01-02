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

#ifndef ZINK_CONTEXT_H
#define ZINK_CONTEXT_H

#include "zink_pipeline.h"
#include "zink_batch.h"

#include "pipe/p_context.h"
#include "pipe/p_state.h"

#include "util/slab.h"
#include "util/list.h"

#include <vulkan/vulkan.h>

struct blitter_context;
struct primconvert_context;
struct list_head;

struct zink_blend_state;
struct zink_depth_stencil_alpha_state;
struct zink_gfx_program;
struct zink_rasterizer_state;
struct zink_resource;
struct zink_vertex_elements_state;

struct zink_sampler_view {
   struct pipe_sampler_view base;
   union {
      VkImageView image_view;
      VkBufferView buffer_view;
   };
};

static inline struct zink_sampler_view *
zink_sampler_view(struct pipe_sampler_view *pview)
{
   return (struct zink_sampler_view *)pview;
}

struct zink_so_target {
   struct pipe_stream_output_target base;
   struct pipe_resource *counter_buffer;
   VkDeviceSize counter_buffer_offset;
   uint32_t stride;
   bool counter_buffer_valid;
};

static inline struct zink_so_target *
zink_so_target(struct pipe_stream_output_target *so_target)
{
   return (struct zink_so_target *)so_target;
}

#define ZINK_SHADER_COUNT (PIPE_SHADER_TYPES - 1)

struct zink_context {
   struct pipe_context base;
   struct slab_child_pool transfer_pool;
   struct blitter_context *blitter;

   struct pipe_device_reset_callback reset;

   VkCommandPool cmdpool;
   struct zink_batch batches[4];
   bool is_device_lost;
   unsigned curr_batch;

   VkQueue queue;

   struct pipe_constant_buffer ubos[PIPE_SHADER_TYPES][PIPE_MAX_CONSTANT_BUFFERS];
   struct pipe_framebuffer_state fb_state;

   struct zink_vertex_elements_state *element_state;
   struct zink_rasterizer_state *rast_state;
   struct zink_depth_stencil_alpha_state *dsa_state;

   struct zink_shader *gfx_stages[ZINK_SHADER_COUNT];
   struct zink_gfx_pipeline_state gfx_pipeline_state;
   struct hash_table *program_cache;
   struct zink_gfx_program *curr_program;

   unsigned dirty_shader_stages : 6; /* mask of changed shader stages */

   struct hash_table *render_pass_cache;

   struct primconvert_context *primconvert;

   struct zink_framebuffer *framebuffer;

   struct pipe_viewport_state viewport_states[PIPE_MAX_VIEWPORTS];
   struct pipe_scissor_state scissor_states[PIPE_MAX_VIEWPORTS];
   VkViewport viewports[PIPE_MAX_VIEWPORTS];
   VkRect2D scissors[PIPE_MAX_VIEWPORTS];
   struct pipe_vertex_buffer buffers[PIPE_MAX_ATTRIBS];
   uint32_t buffers_enabled_mask;

   void *sampler_states[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
   VkSampler samplers[PIPE_SHADER_TYPES][PIPE_MAX_SAMPLERS];
   unsigned num_samplers[PIPE_SHADER_TYPES];
   struct pipe_sampler_view *image_views[PIPE_SHADER_TYPES][PIPE_MAX_SHADER_SAMPLER_VIEWS];
   unsigned num_image_views[PIPE_SHADER_TYPES];

   float line_width;
   float blend_constants[4];

   struct pipe_stencil_ref stencil_ref;

   union {
      struct {
         float default_inner_level[2];
         float default_outer_level[4];
      };
      float tess_levels[6];
   };

   struct list_head suspended_queries;
   struct list_head primitives_generated_queries;
   bool queries_disabled, render_condition_active;

   struct pipe_resource *dummy_buffer;
   struct pipe_resource *null_buffers[5]; /* used to create zink_framebuffer->null_surface, one buffer per samplecount */

   uint32_t num_so_targets;
   struct pipe_stream_output_target *so_targets[PIPE_MAX_SO_OUTPUTS];
   bool dirty_so_targets;
   bool xfb_barrier;
};

static inline struct zink_context *
zink_context(struct pipe_context *context)
{
   return (struct zink_context *)context;
}

static inline struct zink_batch *
zink_curr_batch(struct zink_context *ctx)
{
   assert(ctx->curr_batch < ARRAY_SIZE(ctx->batches));
   return ctx->batches + ctx->curr_batch;
}

struct zink_batch *
zink_batch_rp(struct zink_context *ctx);

struct zink_batch *
zink_batch_no_rp(struct zink_context *ctx);

void
zink_fence_wait(struct pipe_context *ctx);

void
zink_resource_barrier(VkCommandBuffer cmdbuf, struct zink_resource *res,
                      VkImageAspectFlags aspect, VkImageLayout new_layout);

 void
 zink_begin_render_pass(struct zink_context *ctx,
                        struct zink_batch *batch);


VkShaderStageFlagBits
zink_shader_stage(enum pipe_shader_type type);

struct pipe_context *
zink_context_create(struct pipe_screen *pscreen, void *priv, unsigned flags);

void
zink_context_query_init(struct pipe_context *ctx);

void
zink_blit(struct pipe_context *pctx,
          const struct pipe_blit_info *info);

void
zink_draw_vbo(struct pipe_context *pctx,
              const struct pipe_draw_info *dinfo,
              const struct pipe_draw_indirect_info *indirect,
              const struct pipe_draw_start_count *draws,
              unsigned num_draws);

#endif
