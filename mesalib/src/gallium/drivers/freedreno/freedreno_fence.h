/*
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FREEDRENO_FENCE_H_
#define FREEDRENO_FENCE_H_

#include "pipe/p_context.h"

void fd_fence_populate(struct pipe_fence_handle *fence,
		uint32_t timestamp, int fence_fd);
void fd_fence_ref(struct pipe_fence_handle **ptr,
		struct pipe_fence_handle *pfence);
bool fd_fence_finish(struct pipe_screen *pscreen,
		struct pipe_context *ctx,
		struct pipe_fence_handle *pfence,
		uint64_t timeout);
void fd_create_fence_fd(struct pipe_context *pctx,
		struct pipe_fence_handle **pfence, int fd,
		enum pipe_fd_type type);
void fd_fence_server_sync(struct pipe_context *pctx,
		struct pipe_fence_handle *fence);
void fd_fence_server_signal(struct pipe_context *ctx,
		struct pipe_fence_handle *fence);
int fd_fence_get_fd(struct pipe_screen *pscreen,
		struct pipe_fence_handle *pfence);
bool fd_fence_is_fd(struct pipe_fence_handle *fence);

struct fd_batch;
struct pipe_fence_handle * fd_fence_create(struct fd_batch *batch);

#endif /* FREEDRENO_FENCE_H_ */
