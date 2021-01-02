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

#include "util/u_inlines.h"
#include "util/u_dynarray.h"

#include <vulkan/vulkan.h>

struct pipe_screen;
struct zink_screen;

struct zink_fence {
   struct pipe_reference reference;
   unsigned batch_id : 2;
   VkFence fence;
   struct set *active_queries; /* zink_query objects which were active at some point in this batch */
   struct util_dynarray resources;
};

static inline struct zink_fence *
zink_fence(struct pipe_fence_handle *pfence)
{
   return (struct zink_fence *)pfence;
}

struct zink_fence *
zink_create_fence(struct pipe_screen *pscreen, struct zink_batch *batch);

void
zink_fence_reference(struct zink_screen *screen,
                     struct zink_fence **ptr,
                     struct zink_fence *fence);

bool
zink_fence_finish(struct zink_screen *screen, struct zink_fence *fence,
                  uint64_t timeout_ns);

void
zink_screen_fence_init(struct pipe_screen *pscreen);

#endif
