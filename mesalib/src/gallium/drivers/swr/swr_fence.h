/****************************************************************************
 * Copyright (C) 2015 Intel Corporation.   All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * BRIAN PAUL BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 ***************************************************************************/

#ifndef SWR_FENCE_H
#define SWR_FENCE_H

#include "pipe/p_state.h"
#include "util/u_inlines.h"

#include "swr_fence_work.h"

struct pipe_screen;

struct swr_fence {
   struct pipe_reference reference;

   uint64_t read;
   uint64_t write;

   unsigned pending;

   unsigned id; /* Just for reference */
   
   struct {
      uint32_t count;
      struct swr_fence_work head;
      struct swr_fence_work *tail;
   } work;
};


static inline struct swr_fence *
swr_fence(struct pipe_fence_handle *fence)
{
   return (struct swr_fence *)fence;
}


static INLINE bool
swr_is_fence_done(struct pipe_fence_handle *fence_handle)
{
   struct swr_fence *fence = swr_fence(fence_handle);
   return (fence->read == fence->write);
}

static INLINE bool
swr_is_fence_pending(struct pipe_fence_handle *fence_handle)
{
   return swr_fence(fence_handle)->pending;
}


void swr_fence_init(struct pipe_screen *screen);

struct pipe_fence_handle *swr_fence_create();

void swr_fence_reference(struct pipe_screen *screen,
                         struct pipe_fence_handle **ptr,
                         struct pipe_fence_handle *f);

bool swr_fence_finish(struct pipe_screen *screen,
                      struct pipe_context *ctx,
                      struct pipe_fence_handle *fence_handle,
                      uint64_t timeout);

void
swr_fence_submit(struct swr_context *ctx, struct pipe_fence_handle *fence);

uint64_t swr_get_timestamp(struct pipe_screen *screen);

#endif
