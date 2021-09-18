/*
 * Copyright (C) 2016 Rob Clark <robclark@freedesktop.org>
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

#ifndef FREEDRENO_BATCH_CACHE_H_
#define FREEDRENO_BATCH_CACHE_H_

#include "pipe/p_state.h"
#include "util/hash_table.h"

#include "freedreno_util.h"

struct fd_resource;
struct fd_batch;
struct fd_context;
struct fd_screen;

struct hash_table;

struct fd_batch_cache {
   /* Mapping from struct key (basically framebuffer state) to a batch of
    * rendering for rendering to that target.
    */
   struct hash_table *ht;
   unsigned cnt;

   /* Mapping from struct pipe_resource to the batch writing it.  Keeps a reference. */
   struct hash_table *written_resources;
};

#define foreach_batch(batch, cache) \
   hash_table_foreach ((cache)->ht, _batch_entry) \
      for (struct fd_batch *batch = _batch_entry->data; batch != NULL; batch = NULL)

void fd_bc_init(struct fd_context *ctx);
void fd_bc_fini(struct fd_context *ctx);

void fd_bc_flush(struct fd_context *ctx, bool deferred) assert_dt;
void fd_bc_flush_writer(struct fd_context *ctx, struct fd_resource *rsc) assert_dt;
void fd_bc_flush_readers(struct fd_context *ctx, struct fd_resource *rsc) assert_dt;
void fd_bc_flush_gmem_users(struct fd_context *ctx, struct fd_resource *rsc) assert_dt;
void fd_bc_dump(struct fd_context *ctx, const char *fmt, ...)
   _util_printf_format(2, 3);

void fd_bc_free_key(struct fd_batch *batch);
void fd_bc_invalidate_resource(struct fd_context *ctx, struct fd_resource *rsc);
struct fd_batch *fd_bc_alloc_batch(struct fd_context *ctx,
                                   bool nondraw) assert_dt;

struct fd_batch *
fd_batch_from_fb(struct fd_context *ctx,
                 const struct pipe_framebuffer_state *pfb) assert_dt;

#endif /* FREEDRENO_BATCH_CACHE_H_ */
