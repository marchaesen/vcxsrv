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

#ifndef ZINK_BATCH_H
#define ZINK_BATCH_H

#include <vulkan/vulkan.h>

#include "util/list.h"
#include "util/u_dynarray.h"

#include "zink_fence.h"

struct pipe_reference;

struct zink_buffer_view;
struct zink_context;
struct zink_descriptor_set;
struct zink_framebuffer;
struct zink_image_view;
struct zink_program;
struct zink_render_pass;
struct zink_resource;
struct zink_sampler_view;
struct zink_surface;

struct zink_batch_usage {
   /* this has to be atomic for fence access, so we can't use a bitmask and make everything neat */
   uint32_t usage;
};

struct zink_batch_state {
   struct zink_fence fence;
   struct pipe_reference reference;
   struct zink_context *ctx;
   VkCommandPool cmdpool;
   VkCommandBuffer cmdbuf;

   VkQueue queue; //duplicated from batch for threading
   VkSemaphore sem;

   struct util_queue_fence flush_completed;

   struct zink_resource *flush_res;

   unsigned short descs_used; //number of descriptors currently allocated

   struct set *fbs;
   struct set *programs;

   struct set *surfaces;
   struct set *bufferviews;
   struct set *desc_sets;

   struct util_dynarray persistent_resources;
   struct util_dynarray zombie_samplers;

   struct set *active_queries; /* zink_query objects which were active at some point in this batch */

   VkDeviceSize resource_size;

   bool is_device_lost;
   bool have_timelines;
};

struct zink_batch {
   struct zink_batch_state *state;

   uint32_t last_batch_id;
   VkQueue queue; //gfx+compute
   VkQueue thread_queue; //gfx+compute
   struct util_queue flush_queue; //TODO: move to wsi

   bool has_work;
   bool in_rp; //renderpass is currently active
};


static inline struct zink_batch_state *
zink_batch_state(struct zink_fence *fence)
{
   return (struct zink_batch_state *)fence;
}

void
zink_reset_batch_state(struct zink_context *ctx, struct zink_batch_state *bs);

void
zink_clear_batch_state(struct zink_context *ctx, struct zink_batch_state *bs);

void
zink_batch_reset_all(struct zink_context *ctx);

void
zink_batch_state_destroy(struct zink_screen *screen, struct zink_batch_state *bs);

void
zink_batch_state_clear_resources(struct zink_screen *screen, struct zink_batch_state *bs);

void
zink_reset_batch(struct zink_context *ctx, struct zink_batch *batch);
void
zink_batch_reference_framebuffer(struct zink_batch *batch,
                                 struct zink_framebuffer *fb);
void
zink_start_batch(struct zink_context *ctx, struct zink_batch *batch);

void
zink_end_batch(struct zink_context *ctx, struct zink_batch *batch);

void
zink_batch_reference_resource_rw(struct zink_batch *batch,
                                 struct zink_resource *res,
                                 bool write);

void
zink_batch_reference_sampler_view(struct zink_batch *batch,
                                  struct zink_sampler_view *sv);

void
zink_batch_reference_program(struct zink_batch *batch,
                             struct zink_program *pg);

void
zink_batch_reference_image_view(struct zink_batch *batch,
                                struct zink_image_view *image_view);

void
zink_batch_reference_bufferview(struct zink_batch *batch, struct zink_buffer_view *buffer_view);
void
zink_batch_reference_surface(struct zink_batch *batch, struct zink_surface *surface);

void
debug_describe_zink_batch_state(char *buf, const struct zink_batch_state *ptr);

static inline void
zink_batch_state_reference(struct zink_screen *screen,
                           struct zink_batch_state **dst,
                           struct zink_batch_state *src)
{
   struct zink_batch_state *old_dst = dst ? *dst : NULL;

   if (pipe_reference_described(old_dst ? &old_dst->reference : NULL, src ? &src->reference : NULL,
                                (debug_reference_descriptor)debug_describe_zink_batch_state))
      zink_batch_state_destroy(screen, old_dst);
   if (dst) *dst = src;
}

bool
zink_batch_add_desc_set(struct zink_batch *batch, struct zink_descriptor_set *zds);

void
zink_batch_usage_set(struct zink_batch_usage *u, uint32_t batch_id);
bool
zink_batch_usage_matches(struct zink_batch_usage *u, uint32_t batch_id);
bool
zink_batch_usage_exists(struct zink_batch_usage *u);

static inline void
zink_batch_usage_unset(struct zink_batch_usage *u, uint32_t batch_id)
{
   p_atomic_cmpxchg(&u->usage, batch_id, 0);
}
#endif
