/*
 * Copyright (C) 2017 Rob Clark <robclark@freedesktop.org>
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

#ifndef FREEDRENO_QUERY_ACC_H_
#define FREEDRENO_QUERY_ACC_H_

#include "util/list.h"

#include "freedreno_query.h"
#include "freedreno_context.h"


/*
 * Accumulated HW Queries:
 *
 * Unlike the original HW Queries in earlier adreno generations (see
 * freedreno_query_hw.[ch], later generations can accumulate the per-
 * tile results of some (a4xx) or all (a5xx+?) queries in the cmdstream.
 * But we still need to handle pausing/resuming the query across stage
 * changes (in particular when switching between batches).
 *
 * fd_acc_sample_provider:
 *   - one per accumulated query type, registered/implemented by gpu
 *     generation specific code
 *   - knows how to emit cmdstream to pause/resume a query instance
 *
 * fd_acc_query:
 *   - one instance per query object
 *   - each query object has it's own result buffer, which may
 *     span multiple batches, etc.
 */


struct fd_acc_query;

struct fd_acc_sample_provider {
	unsigned query_type;

	/* Set if the provider should still count while !ctx->active_queries */
	bool always;

	unsigned size;

	void (*resume)(struct fd_acc_query *aq, struct fd_batch *batch);
	void (*pause)(struct fd_acc_query *aq, struct fd_batch *batch);

	void (*result)(struct fd_acc_query *aq, void *buf,
			union pipe_query_result *result);
};

struct fd_acc_query {
	struct fd_query base;

	const struct fd_acc_sample_provider *provider;

	struct pipe_resource *prsc;

	/* Pointer to the batch that our query has had resume() called on (if
	 * any).
	 */
	struct fd_batch *batch;

	/* usually the same as provider->size but for batch queries we
	 * need to calculate the size dynamically when the query is
	 * allocated:
	 */
	unsigned size;

	struct list_head node;   /* list-node in ctx->active_acc_queries */

	int no_wait_cnt;         /* see fd_acc_get_query_result() */

	void *query_data;        /* query specific data */
};

static inline struct fd_acc_query *
fd_acc_query(struct fd_query *q)
{
	return (struct fd_acc_query *)q;
}

struct fd_query * fd_acc_create_query(struct fd_context *ctx, unsigned query_type,
	unsigned index);
struct fd_query * fd_acc_create_query2(struct fd_context *ctx, unsigned query_type,
		unsigned index, const struct fd_acc_sample_provider *provider);
void fd_acc_query_set_stage(struct fd_batch *batch, enum fd_render_stage stage);
void fd_acc_query_register_provider(struct pipe_context *pctx,
		const struct fd_acc_sample_provider *provider);

#endif /* FREEDRENO_QUERY_ACC_H_ */
