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

#ifndef ZINK_FENCE_H
#define ZINK_FENCE_H

#include "util/simple_mtx.h"
#include "util/u_inlines.h"
#include "util/u_queue.h"

#include <vulkan/vulkan.h>

struct pipe_context;
struct pipe_screen;
struct zink_batch;
struct zink_batch_state;
struct zink_context;
struct zink_screen;

struct tc_unflushed_batch_token;

struct zink_tc_fence {
   struct pipe_reference reference;
   uint32_t submit_count;
   struct util_queue_fence ready;
   struct tc_unflushed_batch_token *tc_token;
   struct pipe_context *deferred_ctx;
   struct zink_fence *fence;
   VkSemaphore sem;
};

struct zink_fence {
   VkFence fence;
   uint32_t batch_id;
   bool submitted;
   bool completed;
};

static inline struct zink_fence *
zink_fence(void *pfence)
{
   return (struct zink_fence *)pfence;
}

static inline struct zink_tc_fence *
zink_tc_fence(void *pfence)
{
   return (struct zink_tc_fence *)pfence;
}

struct zink_tc_fence *
zink_create_tc_fence(void);

struct pipe_fence_handle *
zink_create_tc_fence_for_tc(struct pipe_context *pctx, struct tc_unflushed_batch_token *tc_token);

void
zink_fence_reference(struct zink_screen *screen,
                     struct zink_tc_fence **ptr,
                     struct zink_tc_fence *fence);

void
zink_create_fence_fd(struct pipe_context *pctx, struct pipe_fence_handle **pfence, int fd, enum pipe_fd_type type);
void
zink_fence_server_signal(struct pipe_context *pctx, struct pipe_fence_handle *pfence);
void
zink_fence_server_sync(struct pipe_context *pctx, struct pipe_fence_handle *pfence);

void
zink_screen_fence_init(struct pipe_screen *pscreen);

bool
zink_vkfence_wait(struct zink_screen *screen, struct zink_fence *fence, uint64_t timeout_ns);

void
zink_fence_clear_resources(struct zink_screen *screen, struct zink_fence *fence);
#endif
