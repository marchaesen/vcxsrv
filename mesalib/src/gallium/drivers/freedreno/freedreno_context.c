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

#include "freedreno_context.h"
#include "freedreno_blitter.h"
#include "freedreno_draw.h"
#include "freedreno_fence.h"
#include "freedreno_program.h"
#include "freedreno_resource.h"
#include "freedreno_texture.h"
#include "freedreno_state.h"
#include "freedreno_gmem.h"
#include "freedreno_query.h"
#include "freedreno_query_hw.h"
#include "freedreno_util.h"
#include "util/u_upload_mgr.h"

static void
fd_context_flush(struct pipe_context *pctx, struct pipe_fence_handle **fencep,
		unsigned flags)
{
	struct fd_context *ctx = fd_context(pctx);
	struct pipe_fence_handle *fence = NULL;
	struct fd_batch *batch = NULL;

	/* We want to lookup current batch if it exists, but not create a new
	 * one if not (unless we need a fence)
	 */
	fd_batch_reference(&batch, ctx->batch);

	DBG("%p: flush: flags=%x", batch, flags);

	/* In some sequence of events, we can end up with a last_fence that is
	 * not an "fd" fence, which results in eglDupNativeFenceFDANDROID()
	 * errors.
	 */
	if ((flags & PIPE_FLUSH_FENCE_FD) && ctx->last_fence &&
			!fd_fence_is_fd(ctx->last_fence))
		fd_fence_ref(&ctx->last_fence, NULL);

	/* if no rendering since last flush, ie. app just decided it needed
	 * a fence, re-use the last one:
	 */
	if (ctx->last_fence) {
		fd_fence_ref(&fence, ctx->last_fence);
		fd_bc_dump(ctx->screen, "%p: reuse last_fence, remaining:\n", ctx);
		goto out;
	}

	if (fencep && !batch) {
		batch = fd_context_batch(ctx);
	} else if (!batch) {
		fd_bc_dump(ctx->screen, "%p: NULL batch, remaining:\n", ctx);
		return;
	}

	/* Take a ref to the batch's fence (batch can be unref'd when flushed: */
	fd_fence_ref(&fence, batch->fence);

	if (flags & PIPE_FLUSH_FENCE_FD)
		batch->needs_out_fence_fd = true;

	fd_bc_dump(ctx->screen, "%p: flushing %p<%u>, flags=0x%x, pending:\n",
			ctx, batch, batch->seqno, flags);

	if (!ctx->screen->reorder) {
		fd_batch_flush(batch);
	} else if (flags & PIPE_FLUSH_DEFERRED) {
		fd_bc_flush_deferred(&ctx->screen->batch_cache, ctx);
	} else {
		fd_bc_flush(&ctx->screen->batch_cache, ctx);
	}

	fd_bc_dump(ctx->screen, "%p: remaining:\n", ctx);

out:
	if (fencep)
		fd_fence_ref(fencep, fence);

	fd_fence_ref(&ctx->last_fence, fence);

	fd_fence_ref(&fence, NULL);

	fd_batch_reference(&batch, NULL);

	u_trace_context_process(&ctx->trace_context,
		!!(flags & PIPE_FLUSH_END_OF_FRAME));
}

static void
fd_texture_barrier(struct pipe_context *pctx, unsigned flags)
{
	if (flags == PIPE_TEXTURE_BARRIER_FRAMEBUFFER) {
		struct fd_context *ctx = fd_context(pctx);

		if (ctx->framebuffer_barrier) {
			ctx->framebuffer_barrier(ctx);
			return;
		}
	}

	/* On devices that could sample from GMEM we could possibly do better.
	 * Or if we knew that we were doing GMEM bypass we could just emit a
	 * cache flush, perhaps?  But we don't know if future draws would cause
	 * us to use GMEM, and a flush in bypass isn't the end of the world.
	 */
	fd_context_flush(pctx, NULL, 0);
}

static void
fd_memory_barrier(struct pipe_context *pctx, unsigned flags)
{
	if (!(flags & ~PIPE_BARRIER_UPDATE))
		return;

	fd_context_flush(pctx, NULL, 0);
	/* TODO do we need to check for persistently mapped buffers and fd_bo_cpu_prep()?? */
}

static void
emit_string_tail(struct fd_ringbuffer *ring, const char *string, int len)
{
	const uint32_t *buf = (const void *)string;

	while (len >= 4) {
		OUT_RING(ring, *buf);
		buf++;
		len -= 4;
	}

	/* copy remainder bytes without reading past end of input string: */
	if (len > 0) {
		uint32_t w = 0;
		memcpy(&w, buf, len);
		OUT_RING(ring, w);
	}
}

/* for prior to a5xx: */
void
fd_emit_string(struct fd_ringbuffer *ring,
		const char *string, int len)
{
	/* max packet size is 0x3fff+1 dwords: */
	len = MIN2(len, 0x4000 * 4);

	OUT_PKT3(ring, CP_NOP, align(len, 4) / 4);
	emit_string_tail(ring, string, len);
}

/* for a5xx+ */
void
fd_emit_string5(struct fd_ringbuffer *ring,
		const char *string, int len)
{
	/* max packet size is 0x3fff dwords: */
	len = MIN2(len, 0x3fff * 4);

	OUT_PKT7(ring, CP_NOP, align(len, 4) / 4);
	emit_string_tail(ring, string, len);
}

/**
 * emit marker string as payload of a no-op packet, which can be
 * decoded by cffdump.
 */
static void
fd_emit_string_marker(struct pipe_context *pctx, const char *string, int len)
{
	struct fd_context *ctx = fd_context(pctx);

	if (!ctx->batch)
		return;

	struct fd_batch *batch = fd_context_batch_locked(ctx);

	ctx->batch->needs_flush = true;

	if (ctx->screen->gpu_id >= 500) {
		fd_emit_string5(batch->draw, string, len);
	} else {
		fd_emit_string(batch->draw, string, len);
	}

	fd_batch_unlock_submit(batch);
	fd_batch_reference(&batch, NULL);
}

/**
 * If we have a pending fence_server_sync() (GPU side sync), flush now.
 * The alternative to try to track this with batch dependencies gets
 * hairy quickly.
 *
 * Call this before switching to a different batch, to handle this case.
 */
void
fd_context_switch_from(struct fd_context *ctx)
{
	if (ctx->batch && (ctx->batch->in_fence_fd != -1))
		fd_batch_flush(ctx->batch);
}

/**
 * If there is a pending fence-fd that we need to sync on, this will
 * transfer the reference to the next batch we are going to render
 * to.
 */
void
fd_context_switch_to(struct fd_context *ctx, struct fd_batch *batch)
{
	if (ctx->in_fence_fd != -1) {
		sync_accumulate("freedreno", &batch->in_fence_fd, ctx->in_fence_fd);
		close(ctx->in_fence_fd);
		ctx->in_fence_fd = -1;
	}
}

/**
 * Return a reference to the current batch, caller must unref.
 */
struct fd_batch *
fd_context_batch(struct fd_context *ctx)
{
	struct fd_batch *batch = NULL;

	fd_batch_reference(&batch, ctx->batch);

	if (unlikely(!batch)) {
		batch = fd_batch_from_fb(&ctx->screen->batch_cache, ctx, &ctx->framebuffer);
		util_copy_framebuffer_state(&batch->framebuffer, &ctx->framebuffer);
		fd_batch_reference(&ctx->batch, batch);
		fd_context_all_dirty(ctx);
	}
	fd_context_switch_to(ctx, batch);

	return batch;
}

/**
 * Return a locked reference to the current batch.  A batch with emit
 * lock held is protected against flushing while the lock is held.
 * The emit-lock should be acquired before screen-lock.  The emit-lock
 * should be held while emitting cmdstream.
 */
struct fd_batch *
fd_context_batch_locked(struct fd_context *ctx)
{
	struct fd_batch *batch = NULL;

	while (!batch) {
		batch = fd_context_batch(ctx);
		if (!fd_batch_lock_submit(batch)) {
			fd_batch_reference(&batch, NULL);
		}
	}

	return batch;
}

void
fd_context_destroy(struct pipe_context *pctx)
{
	struct fd_context *ctx = fd_context(pctx);
	unsigned i;

	DBG("");

	fd_screen_lock(ctx->screen);
	list_del(&ctx->node);
	fd_screen_unlock(ctx->screen);

	fd_fence_ref(&ctx->last_fence, NULL);

	if (ctx->in_fence_fd != -1)
		close(ctx->in_fence_fd);

	for (i = 0; i < ARRAY_SIZE(ctx->pvtmem); i++) {
		if (ctx->pvtmem[i].bo)
			fd_bo_del(ctx->pvtmem[i].bo);
	}

	util_copy_framebuffer_state(&ctx->framebuffer, NULL);
	fd_batch_reference(&ctx->batch, NULL);  /* unref current batch */
	fd_bc_invalidate_context(ctx);

	fd_prog_fini(pctx);

	if (ctx->blitter)
		util_blitter_destroy(ctx->blitter);

	if (pctx->stream_uploader)
		u_upload_destroy(pctx->stream_uploader);

	for (i = 0; i < ARRAY_SIZE(ctx->clear_rs_state); i++)
		if (ctx->clear_rs_state[i])
			pctx->delete_rasterizer_state(pctx, ctx->clear_rs_state[i]);

	if (ctx->primconvert)
		util_primconvert_destroy(ctx->primconvert);

	slab_destroy_child(&ctx->transfer_pool);

	for (i = 0; i < ARRAY_SIZE(ctx->vsc_pipe_bo); i++) {
		if (!ctx->vsc_pipe_bo[i])
			break;
		fd_bo_del(ctx->vsc_pipe_bo[i]);
	}

	fd_device_del(ctx->dev);
	fd_pipe_del(ctx->pipe);

	simple_mtx_destroy(&ctx->gmem_lock);

	u_trace_context_fini(&ctx->trace_context);

	if (fd_mesa_debug & (FD_DBG_BSTAT | FD_DBG_MSGS)) {
		printf("batch_total=%u, batch_sysmem=%u, batch_gmem=%u, batch_nondraw=%u, batch_restore=%u\n",
			(uint32_t)ctx->stats.batch_total, (uint32_t)ctx->stats.batch_sysmem,
			(uint32_t)ctx->stats.batch_gmem, (uint32_t)ctx->stats.batch_nondraw,
			(uint32_t)ctx->stats.batch_restore);
	}
}

static void
fd_set_debug_callback(struct pipe_context *pctx,
		const struct pipe_debug_callback *cb)
{
	struct fd_context *ctx = fd_context(pctx);

	if (cb)
		ctx->debug = *cb;
	else
		memset(&ctx->debug, 0, sizeof(ctx->debug));
}

static uint32_t
fd_get_reset_count(struct fd_context *ctx, bool per_context)
{
	uint64_t val;
	enum fd_param_id param =
		per_context ? FD_CTX_FAULTS : FD_GLOBAL_FAULTS;
	int ret = fd_pipe_get_param(ctx->pipe, param, &val);
	debug_assert(!ret);
	return val;
}

static enum pipe_reset_status
fd_get_device_reset_status(struct pipe_context *pctx)
{
	struct fd_context *ctx = fd_context(pctx);
	int context_faults = fd_get_reset_count(ctx, true);
	int global_faults  = fd_get_reset_count(ctx, false);
	enum pipe_reset_status status;

	if (context_faults != ctx->context_reset_count) {
		status = PIPE_GUILTY_CONTEXT_RESET;
	} else if (global_faults != ctx->global_reset_count) {
		status = PIPE_INNOCENT_CONTEXT_RESET;
	} else {
		status = PIPE_NO_RESET;
	}

	ctx->context_reset_count = context_faults;
	ctx->global_reset_count = global_faults;

	return status;
}

static void
fd_trace_record_ts(struct u_trace *ut, struct pipe_resource *timestamps,
		unsigned idx)
{
	struct fd_batch *batch = container_of(ut, struct fd_batch, trace);
	struct fd_ringbuffer *ring = batch->nondraw ? batch->draw : batch->gmem;

	if (ring->cur == batch->last_timestamp_cmd) {
		uint64_t *ts = fd_bo_map(fd_resource(timestamps)->bo);
		ts[idx] = U_TRACE_NO_TIMESTAMP;
		return;
	}

	unsigned ts_offset = idx * sizeof(uint64_t);
	batch->ctx->record_timestamp(ring, fd_resource(timestamps)->bo, ts_offset);
	batch->last_timestamp_cmd = ring->cur;
}

static uint64_t
fd_trace_read_ts(struct u_trace_context *utctx,
		struct pipe_resource *timestamps, unsigned idx)
{
	struct fd_context *ctx = container_of(utctx, struct fd_context, trace_context);
	struct fd_bo *ts_bo = fd_resource(timestamps)->bo;

	/* Only need to stall on results for the first entry: */
	if (idx == 0) {
		int ret = fd_bo_cpu_prep(ts_bo, ctx->pipe, DRM_FREEDRENO_PREP_READ);
		if (ret)
			return U_TRACE_NO_TIMESTAMP;
	}

	uint64_t *ts = fd_bo_map(ts_bo);

	/* Don't translate the no-timestamp marker: */
	if (ts[idx] == U_TRACE_NO_TIMESTAMP)
		return U_TRACE_NO_TIMESTAMP;

	return ctx->ts_to_ns(ts[idx]);
}

/* TODO we could combine a few of these small buffers (solid_vbuf,
 * blit_texcoord_vbuf, and vsc_size_mem, into a single buffer and
 * save a tiny bit of memory
 */

static struct pipe_resource *
create_solid_vertexbuf(struct pipe_context *pctx)
{
	static const float init_shader_const[] = {
			-1.000000, +1.000000, +1.000000,
			+1.000000, -1.000000, +1.000000,
	};
	struct pipe_resource *prsc = pipe_buffer_create(pctx->screen,
			PIPE_BIND_CUSTOM, PIPE_USAGE_IMMUTABLE, sizeof(init_shader_const));
	pipe_buffer_write(pctx, prsc, 0,
			sizeof(init_shader_const), init_shader_const);
	return prsc;
}

static struct pipe_resource *
create_blit_texcoord_vertexbuf(struct pipe_context *pctx)
{
	struct pipe_resource *prsc = pipe_buffer_create(pctx->screen,
			PIPE_BIND_CUSTOM, PIPE_USAGE_DYNAMIC, 16);
	return prsc;
}

void
fd_context_setup_common_vbos(struct fd_context *ctx)
{
	struct pipe_context *pctx = &ctx->base;

	ctx->solid_vbuf = create_solid_vertexbuf(pctx);
	ctx->blit_texcoord_vbuf = create_blit_texcoord_vertexbuf(pctx);

	/* setup solid_vbuf_state: */
	ctx->solid_vbuf_state.vtx = pctx->create_vertex_elements_state(
			pctx, 1, (struct pipe_vertex_element[]){{
				.vertex_buffer_index = 0,
				.src_offset = 0,
				.src_format = PIPE_FORMAT_R32G32B32_FLOAT,
			}});
	ctx->solid_vbuf_state.vertexbuf.count = 1;
	ctx->solid_vbuf_state.vertexbuf.vb[0].stride = 12;
	ctx->solid_vbuf_state.vertexbuf.vb[0].buffer.resource = ctx->solid_vbuf;

	/* setup blit_vbuf_state: */
	ctx->blit_vbuf_state.vtx = pctx->create_vertex_elements_state(
			pctx, 2, (struct pipe_vertex_element[]){{
				.vertex_buffer_index = 0,
				.src_offset = 0,
				.src_format = PIPE_FORMAT_R32G32_FLOAT,
			}, {
				.vertex_buffer_index = 1,
				.src_offset = 0,
				.src_format = PIPE_FORMAT_R32G32B32_FLOAT,
			}});
	ctx->blit_vbuf_state.vertexbuf.count = 2;
	ctx->blit_vbuf_state.vertexbuf.vb[0].stride = 8;
	ctx->blit_vbuf_state.vertexbuf.vb[0].buffer.resource = ctx->blit_texcoord_vbuf;
	ctx->blit_vbuf_state.vertexbuf.vb[1].stride = 12;
	ctx->blit_vbuf_state.vertexbuf.vb[1].buffer.resource = ctx->solid_vbuf;
}

void
fd_context_cleanup_common_vbos(struct fd_context *ctx)
{
	struct pipe_context *pctx = &ctx->base;

	pctx->delete_vertex_elements_state(pctx, ctx->solid_vbuf_state.vtx);
	pctx->delete_vertex_elements_state(pctx, ctx->blit_vbuf_state.vtx);

	pipe_resource_reference(&ctx->solid_vbuf, NULL);
	pipe_resource_reference(&ctx->blit_texcoord_vbuf, NULL);
}

struct pipe_context *
fd_context_init(struct fd_context *ctx, struct pipe_screen *pscreen,
		const uint8_t *primtypes, void *priv, unsigned flags)
{
	struct fd_screen *screen = fd_screen(pscreen);
	struct pipe_context *pctx;
	unsigned prio = 1;
	int i;

	/* lower numerical value == higher priority: */
	if (fd_mesa_debug & FD_DBG_HIPRIO)
		prio = 0;
	else if (flags & PIPE_CONTEXT_HIGH_PRIORITY)
		prio = 0;
	else if (flags & PIPE_CONTEXT_LOW_PRIORITY)
		prio = 2;

	ctx->screen = screen;
	ctx->pipe = fd_pipe_new2(screen->dev, FD_PIPE_3D, prio);

	ctx->in_fence_fd = -1;

	if (fd_device_version(screen->dev) >= FD_VERSION_ROBUSTNESS) {
		ctx->context_reset_count = fd_get_reset_count(ctx, true);
		ctx->global_reset_count = fd_get_reset_count(ctx, false);
	}

	ctx->primtypes = primtypes;
	ctx->primtype_mask = 0;
	for (i = 0; i <= PIPE_PRIM_MAX; i++)
		if (primtypes[i])
			ctx->primtype_mask |= (1 << i);

	simple_mtx_init(&ctx->gmem_lock, mtx_plain);

	/* need some sane default in case gallium frontends don't
	 * set some state:
	 */
	ctx->sample_mask = 0xffff;
	ctx->active_queries = true;

	pctx = &ctx->base;
	pctx->screen = pscreen;
	pctx->priv = priv;
	pctx->flush = fd_context_flush;
	pctx->emit_string_marker = fd_emit_string_marker;
	pctx->set_debug_callback = fd_set_debug_callback;
	pctx->get_device_reset_status = fd_get_device_reset_status;
	pctx->create_fence_fd = fd_create_fence_fd;
	pctx->fence_server_sync = fd_fence_server_sync;
	pctx->fence_server_signal = fd_fence_server_signal;
	pctx->texture_barrier = fd_texture_barrier;
	pctx->memory_barrier = fd_memory_barrier;

	pctx->stream_uploader = u_upload_create_default(pctx);
	if (!pctx->stream_uploader)
		goto fail;
	pctx->const_uploader = pctx->stream_uploader;

	slab_create_child(&ctx->transfer_pool, &screen->transfer_pool);

	fd_draw_init(pctx);
	fd_resource_context_init(pctx);
	fd_query_context_init(pctx);
	fd_texture_init(pctx);
	fd_state_init(pctx);

	ctx->blitter = util_blitter_create(pctx);
	if (!ctx->blitter)
		goto fail;

	ctx->primconvert = util_primconvert_create(pctx, ctx->primtype_mask);
	if (!ctx->primconvert)
		goto fail;

	list_inithead(&ctx->hw_active_queries);
	list_inithead(&ctx->acc_active_queries);

	fd_screen_lock(ctx->screen);
	ctx->seqno = ++screen->ctx_seqno;
	list_add(&ctx->node, &ctx->screen->context_list);
	fd_screen_unlock(ctx->screen);

	ctx->current_scissor = &ctx->disabled_scissor;

	u_trace_context_init(&ctx->trace_context, pctx,
			fd_trace_record_ts, fd_trace_read_ts);

	return pctx;

fail:
	pctx->destroy(pctx);
	return NULL;
}
