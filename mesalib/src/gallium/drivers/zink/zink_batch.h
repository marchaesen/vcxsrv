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
#include "util/set.h"
#include "util/u_dynarray.h"

#include "zink_fence.h"

#ifdef __cplusplus
extern "C" {
#endif

struct pipe_reference;

struct zink_buffer_view;
struct zink_context;
struct zink_descriptor_set;
struct zink_image_view;
struct zink_program;
struct zink_render_pass;
struct zink_resource;
struct zink_sampler_view;
struct zink_surface;

struct zink_batch_usage {
   uint32_t usage;
   cnd_t flush;
   mtx_t mtx;
   bool unflushed;
};

/* not real api don't use */
bool
batch_ptr_add_usage(struct zink_batch *batch, struct set *s, void *ptr);

struct zink_batch_state {
   struct zink_fence fence;
   struct zink_batch_state *next;

   struct zink_batch_usage usage;
   struct zink_context *ctx;
   VkCommandPool cmdpool;
   VkCommandBuffer cmdbuf;
   VkCommandBuffer barrier_cmdbuf;
   VkSemaphore signal_semaphore; //external signal semaphore
   struct util_dynarray wait_semaphores; //external wait semaphores
   struct util_dynarray wait_semaphore_stages; //external wait semaphores

   VkQueue queue; //duplicated from batch for threading
   VkSemaphore sem;

   struct util_queue_fence flush_completed;

   struct pipe_resource *flush_res;

   struct set *programs;

   struct set *resources;
   struct set *surfaces;
   struct set *bufferviews;

   struct util_dynarray unref_resources;
   struct util_dynarray bindless_releases[2];

   struct util_dynarray persistent_resources;
   struct util_dynarray zombie_samplers;
   struct util_dynarray dead_framebuffers;

   struct set *active_queries; /* zink_query objects which were active at some point in this batch */

   struct zink_batch_descriptor_data *dd;

   VkDeviceSize resource_size;

    /* this is a monotonic int used to disambiguate internal fences from their tc fence references */
   unsigned submit_count;

   bool is_device_lost;
   bool have_timelines;
   bool has_barriers;
   bool scanout_flush;
};

struct zink_batch {
   struct zink_batch_state *state;

   struct zink_batch_usage *last_batch_usage;

   unsigned work_count;

   bool has_work;
   bool last_was_compute;
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
zink_start_batch(struct zink_context *ctx, struct zink_batch *batch);

void
zink_end_batch(struct zink_context *ctx, struct zink_batch *batch);

void
zink_batch_resource_usage_set(struct zink_batch *batch, struct zink_resource *res, bool write);

void
zink_batch_reference_resource_rw(struct zink_batch *batch,
                                 struct zink_resource *res,
                                 bool write);
void
zink_batch_reference_resource(struct zink_batch *batch, struct zink_resource *res);

void
zink_batch_reference_resource_move(struct zink_batch *batch, struct zink_resource *res);

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

static inline bool
zink_batch_usage_is_unflushed(const struct zink_batch_usage *u)
{
   return u && u->unflushed;
}

static inline void
zink_batch_usage_unset(struct zink_batch_usage **u, struct zink_batch_state *bs)
{
   (void)p_atomic_cmpxchg((uintptr_t *)u, (uintptr_t)&bs->usage, (uintptr_t)NULL);
}

static inline void
zink_batch_usage_set(struct zink_batch_usage **u, struct zink_batch_state *bs)
{
   *u = &bs->usage;
}

static inline bool
zink_batch_usage_matches(const struct zink_batch_usage *u, const struct zink_batch_state *bs)
{
   return u == &bs->usage;
}

static inline bool
zink_batch_usage_exists(const struct zink_batch_usage *u)
{
   return u && (u->usage || u->unflushed);
}

bool
zink_screen_usage_check_completion(struct zink_screen *screen, const struct zink_batch_usage *u);

bool
zink_batch_usage_check_completion(struct zink_context *ctx, const struct zink_batch_usage *u);

void
zink_batch_usage_wait(struct zink_context *ctx, struct zink_batch_usage *u);

#ifdef __cplusplus
}
#endif

#endif
