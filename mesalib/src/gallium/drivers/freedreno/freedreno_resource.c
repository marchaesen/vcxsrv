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

#include "util/format/u_format.h"
#include "util/format/u_format_rgtc.h"
#include "util/format/u_format_zs.h"
#include "util/u_inlines.h"
#include "util/u_transfer.h"
#include "util/u_string.h"
#include "util/u_surface.h"
#include "util/set.h"
#include "util/u_drm.h"

#include "decode/util.h"

#include "freedreno_resource.h"
#include "freedreno_batch_cache.h"
#include "freedreno_blitter.h"
#include "freedreno_fence.h"
#include "freedreno_screen.h"
#include "freedreno_surface.h"
#include "freedreno_context.h"
#include "freedreno_query_hw.h"
#include "freedreno_util.h"

#include "drm-uapi/drm_fourcc.h"
#include <errno.h>

/* XXX this should go away, needed for 'struct winsys_handle' */
#include "frontend/drm_driver.h"

/* A private modifier for now, so we have a way to request tiled but not
 * compressed.  It would perhaps be good to get real modifiers for the
 * tiled formats, but would probably need to do some work to figure out
 * the layout(s) of the tiled modes, and whether they are the same
 * across generations.
 */
#define FD_FORMAT_MOD_QCOM_TILED	fourcc_mod_code(QCOM, 0xffffffff)

/**
 * Go through the entire state and see if the resource is bound
 * anywhere. If it is, mark the relevant state as dirty. This is
 * called on realloc_bo to ensure the necessary state is re-
 * emitted so the GPU looks at the new backing bo.
 */
static void
rebind_resource_in_ctx(struct fd_context *ctx, struct fd_resource *rsc)
{
	struct pipe_resource *prsc = &rsc->base;

	if (ctx->rebind_resource)
		ctx->rebind_resource(ctx, rsc);

	/* VBOs */
	if (rsc->dirty & FD_DIRTY_VTXBUF) {
		struct fd_vertexbuf_stateobj *vb = &ctx->vtx.vertexbuf;
		for (unsigned i = 0; i < vb->count && !(ctx->dirty & FD_DIRTY_VTXBUF); i++) {
			if (vb->vb[i].buffer.resource == prsc)
				ctx->dirty |= FD_DIRTY_VTXBUF;
		}
	}

	const enum fd_dirty_3d_state per_stage_dirty =
			FD_DIRTY_CONST | FD_DIRTY_TEX | FD_DIRTY_IMAGE | FD_DIRTY_SSBO;

	if (!(rsc->dirty & per_stage_dirty))
		return;

	/* per-shader-stage resources: */
	for (unsigned stage = 0; stage < PIPE_SHADER_TYPES; stage++) {
		/* Constbufs.. note that constbuf[0] is normal uniforms emitted in
		 * cmdstream rather than by pointer..
		 */
		if ((rsc->dirty & FD_DIRTY_CONST) &&
				!(ctx->dirty_shader[stage] & FD_DIRTY_CONST)) {
			struct fd_constbuf_stateobj *cb = &ctx->constbuf[stage];
			const unsigned num_ubos = util_last_bit(cb->enabled_mask);
			for (unsigned i = 1; i < num_ubos; i++) {
				if (cb->cb[i].buffer == prsc) {
					ctx->dirty_shader[stage] |= FD_DIRTY_SHADER_CONST;
					ctx->dirty |= FD_DIRTY_CONST;
					break;
				}
			}
		}

		/* Textures */
		if ((rsc->dirty & FD_DIRTY_TEX) &&
				!(ctx->dirty_shader[stage] & FD_DIRTY_TEX)) {
			struct fd_texture_stateobj *tex = &ctx->tex[stage];
			for (unsigned i = 0; i < tex->num_textures; i++) {
				if (tex->textures[i] && (tex->textures[i]->texture == prsc)) {
					ctx->dirty_shader[stage] |= FD_DIRTY_SHADER_TEX;
					ctx->dirty |= FD_DIRTY_TEX;
					break;
				}
			}
		}

		/* Images */
		if ((rsc->dirty & FD_DIRTY_IMAGE) &&
				!(ctx->dirty_shader[stage] & FD_DIRTY_IMAGE)) {
			struct fd_shaderimg_stateobj *si = &ctx->shaderimg[stage];
			const unsigned num_images = util_last_bit(si->enabled_mask);
			for (unsigned i = 0; i < num_images; i++) {
				if (si->si[i].resource == prsc) {
					ctx->dirty_shader[stage] |= FD_DIRTY_SHADER_IMAGE;
					ctx->dirty |= FD_DIRTY_IMAGE;
					break;
				}
			}
		}

		/* SSBOs */
		if ((rsc->dirty & FD_DIRTY_SSBO) &&
				!(ctx->dirty_shader[stage] & FD_DIRTY_SSBO)) {
			struct fd_shaderbuf_stateobj *sb = &ctx->shaderbuf[stage];
			const unsigned num_ssbos = util_last_bit(sb->enabled_mask);
			for (unsigned i = 0; i < num_ssbos; i++) {
				if (sb->sb[i].buffer == prsc) {
					ctx->dirty_shader[stage] |= FD_DIRTY_SHADER_SSBO;
					ctx->dirty |= FD_DIRTY_SSBO;
					break;
				}
			}
		}
	}
}

static void
rebind_resource(struct fd_resource *rsc)
{
	struct fd_screen *screen = fd_screen(rsc->base.screen);

	fd_screen_lock(screen);
	fd_resource_lock(rsc);

	if (rsc->dirty)
		list_for_each_entry (struct fd_context, ctx, &screen->context_list, node)
			rebind_resource_in_ctx(ctx, rsc);

	fd_resource_unlock(rsc);
	fd_screen_unlock(screen);
}

static inline void
fd_resource_set_bo(struct fd_resource *rsc, struct fd_bo *bo)
{
	struct fd_screen *screen = fd_screen(rsc->base.screen);

	rsc->bo = bo;
	rsc->seqno = p_atomic_inc_return(&screen->rsc_seqno);
}

static void
realloc_bo(struct fd_resource *rsc, uint32_t size)
{
	struct pipe_resource *prsc = &rsc->base;
	struct fd_screen *screen = fd_screen(rsc->base.screen);
	uint32_t flags = DRM_FREEDRENO_GEM_CACHE_WCOMBINE |
			DRM_FREEDRENO_GEM_TYPE_KMEM |
			COND(prsc->bind & PIPE_BIND_SCANOUT, DRM_FREEDRENO_GEM_SCANOUT);
			/* TODO other flags? */

	/* if we start using things other than write-combine,
	 * be sure to check for PIPE_RESOURCE_FLAG_MAP_COHERENT
	 */

	if (rsc->bo)
		fd_bo_del(rsc->bo);

	struct fd_bo *bo = fd_bo_new(screen->dev, size, flags, "%ux%ux%u@%u:%x",
			prsc->width0, prsc->height0, prsc->depth0, rsc->layout.cpp, prsc->bind);
	fd_resource_set_bo(rsc, bo);

	/* Zero out the UBWC area on allocation.  This fixes intermittent failures
	 * with UBWC, which I suspect are due to the HW having a hard time
	 * interpreting arbitrary values populating the flags buffer when the BO
	 * was recycled through the bo cache (instead of fresh allocations from
	 * the kernel, which are zeroed).  sleep(1) in this spot didn't work
	 * around the issue, but any memset value seems to.
	 */
	if (rsc->layout.ubwc) {
		rsc->needs_ubwc_clear = true;
	}

	util_range_set_empty(&rsc->valid_buffer_range);
	fd_bc_invalidate_resource(rsc, true);
}

static void
do_blit(struct fd_context *ctx, const struct pipe_blit_info *blit, bool fallback)
{
	struct pipe_context *pctx = &ctx->base;

	/* TODO size threshold too?? */
	if (fallback || !fd_blit(pctx, blit)) {
		/* do blit on cpu: */
		util_resource_copy_region(pctx,
				blit->dst.resource, blit->dst.level, blit->dst.box.x,
				blit->dst.box.y, blit->dst.box.z,
				blit->src.resource, blit->src.level, &blit->src.box);
	}
}

static void
flush_resource(struct fd_context *ctx, struct fd_resource *rsc, unsigned usage);

/**
 * @rsc: the resource to shadow
 * @level: the level to discard (if box != NULL, otherwise ignored)
 * @box: the box to discard (or NULL if none)
 * @modifier: the modifier for the new buffer state
 */
static bool
fd_try_shadow_resource(struct fd_context *ctx, struct fd_resource *rsc,
		unsigned level, const struct pipe_box *box, uint64_t modifier)
{
	struct pipe_context *pctx = &ctx->base;
	struct pipe_resource *prsc = &rsc->base;
	bool fallback = false;

	if (prsc->next)
		return false;

	/* If you have a sequence where there is a single rsc associated
	 * with the current render target, and then you end up shadowing
	 * that same rsc on the 3d pipe (u_blitter), because of how we
	 * swap the new shadow and rsc before the back-blit, you could end
	 * up confusing things into thinking that u_blitter's framebuffer
	 * state is the same as the current framebuffer state, which has
	 * the result of blitting to rsc rather than shadow.
	 *
	 * Normally we wouldn't want to unconditionally trigger a flush,
	 * since that defeats the purpose of shadowing, but this is a
	 * case where we'd have to flush anyways.
	 */
	if (rsc->write_batch == ctx->batch)
		flush_resource(ctx, rsc, 0);

	/* TODO: somehow munge dimensions and format to copy unsupported
	 * render target format to something that is supported?
	 */
	if (!pctx->screen->is_format_supported(pctx->screen,
			prsc->format, prsc->target, prsc->nr_samples,
			prsc->nr_storage_samples,
			PIPE_BIND_RENDER_TARGET))
		fallback = true;

	/* do shadowing back-blits on the cpu for buffers: */
	if (prsc->target == PIPE_BUFFER)
		fallback = true;

	bool discard_whole_level = box && util_texrange_covers_whole_level(prsc, level,
		box->x, box->y, box->z, box->width, box->height, box->depth);

	/* TODO need to be more clever about current level */
	if ((prsc->target >= PIPE_TEXTURE_2D) && box && !discard_whole_level)
		return false;

	struct pipe_resource *pshadow =
		pctx->screen->resource_create_with_modifiers(pctx->screen,
				prsc, &modifier, 1);

	if (!pshadow)
		return false;

	assert(!ctx->in_shadow);
	ctx->in_shadow = true;

	/* get rid of any references that batch-cache might have to us (which
	 * should empty/destroy rsc->batches hashset)
	 */
	fd_bc_invalidate_resource(rsc, false);
	rebind_resource(rsc);

	fd_screen_lock(ctx->screen);

	/* Swap the backing bo's, so shadow becomes the old buffer,
	 * blit from shadow to new buffer.  From here on out, we
	 * cannot fail.
	 *
	 * Note that we need to do it in this order, otherwise if
	 * we go down cpu blit path, the recursive transfer_map()
	 * sees the wrong status..
	 */
	struct fd_resource *shadow = fd_resource(pshadow);

	DBG("shadow: %p (%d) -> %p (%d)\n", rsc, rsc->base.reference.count,
			shadow, shadow->base.reference.count);

	/* TODO valid_buffer_range?? */
	swap(rsc->bo,        shadow->bo);
	swap(rsc->write_batch,   shadow->write_batch);
	swap(rsc->layout, shadow->layout);
	rsc->seqno = p_atomic_inc_return(&ctx->screen->rsc_seqno);

	/* at this point, the newly created shadow buffer is not referenced
	 * by any batches, but the existing rsc (probably) is.  We need to
	 * transfer those references over:
	 */
	debug_assert(shadow->batch_mask == 0);
	struct fd_batch *batch;
	foreach_batch(batch, &ctx->screen->batch_cache, rsc->batch_mask) {
		struct set_entry *entry = _mesa_set_search(batch->resources, rsc);
		_mesa_set_remove(batch->resources, entry);
		_mesa_set_add(batch->resources, shadow);
	}
	swap(rsc->batch_mask, shadow->batch_mask);

	fd_screen_unlock(ctx->screen);

	struct pipe_blit_info blit = {};
	blit.dst.resource = prsc;
	blit.dst.format   = prsc->format;
	blit.src.resource = pshadow;
	blit.src.format   = pshadow->format;
	blit.mask = util_format_get_mask(prsc->format);
	blit.filter = PIPE_TEX_FILTER_NEAREST;

#define set_box(field, val) do {     \
		blit.dst.field = (val);      \
		blit.src.field = (val);      \
	} while (0)

	/* blit the other levels in their entirety: */
	for (unsigned l = 0; l <= prsc->last_level; l++) {
		if (box && l == level)
			continue;

		/* just blit whole level: */
		set_box(level, l);
		set_box(box.width,  u_minify(prsc->width0, l));
		set_box(box.height, u_minify(prsc->height0, l));
		set_box(box.depth,  u_minify(prsc->depth0, l));

		for (int i = 0; i < prsc->array_size; i++) {
			set_box(box.z, i);
			do_blit(ctx, &blit, fallback);
		}
	}

	/* deal w/ current level specially, since we might need to split
	 * it up into a couple blits:
	 */
	if (box && !discard_whole_level) {
		set_box(level, level);

		switch (prsc->target) {
		case PIPE_BUFFER:
		case PIPE_TEXTURE_1D:
			set_box(box.y, 0);
			set_box(box.z, 0);
			set_box(box.height, 1);
			set_box(box.depth, 1);

			if (box->x > 0) {
				set_box(box.x, 0);
				set_box(box.width, box->x);

				do_blit(ctx, &blit, fallback);
			}
			if ((box->x + box->width) < u_minify(prsc->width0, level)) {
				set_box(box.x, box->x + box->width);
				set_box(box.width, u_minify(prsc->width0, level) - (box->x + box->width));

				do_blit(ctx, &blit, fallback);
			}
			break;
		case PIPE_TEXTURE_2D:
			/* TODO */
		default:
			unreachable("TODO");
		}
	}

	ctx->in_shadow = false;

	pipe_resource_reference(&pshadow, NULL);

	return true;
}

/**
 * Uncompress an UBWC compressed buffer "in place".  This works basically
 * like resource shadowing, creating a new resource, and doing an uncompress
 * blit, and swapping the state between shadow and original resource so it
 * appears to the gallium frontends as if nothing changed.
 */
void
fd_resource_uncompress(struct fd_context *ctx, struct fd_resource *rsc)
{
	bool success =
		fd_try_shadow_resource(ctx, rsc, 0, NULL, FD_FORMAT_MOD_QCOM_TILED);

	/* shadow should not fail in any cases where we need to uncompress: */
	debug_assert(success);
}

/**
 * Debug helper to hexdump a resource.
 */
void
fd_resource_dump(struct fd_resource *rsc, const char *name)
{
	fd_bo_cpu_prep(rsc->bo, NULL, DRM_FREEDRENO_PREP_READ);
	printf("%s: \n", name);
	dump_hex(fd_bo_map(rsc->bo), fd_bo_size(rsc->bo));
}

static struct fd_resource *
fd_alloc_staging(struct fd_context *ctx, struct fd_resource *rsc,
		unsigned level, const struct pipe_box *box)
{
	struct pipe_context *pctx = &ctx->base;
	struct pipe_resource tmpl = rsc->base;

	tmpl.width0  = box->width;
	tmpl.height0 = box->height;
	/* for array textures, box->depth is the array_size, otherwise
	 * for 3d textures, it is the depth:
	 */
	if (tmpl.array_size > 1) {
		if (tmpl.target == PIPE_TEXTURE_CUBE)
			tmpl.target = PIPE_TEXTURE_2D_ARRAY;
		tmpl.array_size = box->depth;
		tmpl.depth0 = 1;
	} else {
		tmpl.array_size = 1;
		tmpl.depth0 = box->depth;
	}
	tmpl.last_level = 0;
	tmpl.bind |= PIPE_BIND_LINEAR;

	struct pipe_resource *pstaging =
		pctx->screen->resource_create(pctx->screen, &tmpl);
	if (!pstaging)
		return NULL;

	return fd_resource(pstaging);
}

static void
fd_blit_from_staging(struct fd_context *ctx, struct fd_transfer *trans)
{
	struct pipe_resource *dst = trans->base.resource;
	struct pipe_blit_info blit = {};

	blit.dst.resource = dst;
	blit.dst.format   = dst->format;
	blit.dst.level    = trans->base.level;
	blit.dst.box      = trans->base.box;
	blit.src.resource = trans->staging_prsc;
	blit.src.format   = trans->staging_prsc->format;
	blit.src.level    = 0;
	blit.src.box      = trans->staging_box;
	blit.mask = util_format_get_mask(trans->staging_prsc->format);
	blit.filter = PIPE_TEX_FILTER_NEAREST;

	do_blit(ctx, &blit, false);
}

static void
fd_blit_to_staging(struct fd_context *ctx, struct fd_transfer *trans)
{
	struct pipe_resource *src = trans->base.resource;
	struct pipe_blit_info blit = {};

	blit.src.resource = src;
	blit.src.format   = src->format;
	blit.src.level    = trans->base.level;
	blit.src.box      = trans->base.box;
	blit.dst.resource = trans->staging_prsc;
	blit.dst.format   = trans->staging_prsc->format;
	blit.dst.level    = 0;
	blit.dst.box      = trans->staging_box;
	blit.mask = util_format_get_mask(trans->staging_prsc->format);
	blit.filter = PIPE_TEX_FILTER_NEAREST;

	do_blit(ctx, &blit, false);
}

static void fd_resource_transfer_flush_region(struct pipe_context *pctx,
		struct pipe_transfer *ptrans,
		const struct pipe_box *box)
{
	struct fd_resource *rsc = fd_resource(ptrans->resource);

	if (ptrans->resource->target == PIPE_BUFFER)
		util_range_add(&rsc->base, &rsc->valid_buffer_range,
					   ptrans->box.x + box->x,
					   ptrans->box.x + box->x + box->width);
}

static void
flush_resource(struct fd_context *ctx, struct fd_resource *rsc, unsigned usage)
{
	struct fd_batch *write_batch = NULL;

	fd_screen_lock(ctx->screen);
	fd_batch_reference_locked(&write_batch, rsc->write_batch);
	fd_screen_unlock(ctx->screen);

	if (usage & PIPE_MAP_WRITE) {
		struct fd_batch *batch, *batches[32] = {};
		uint32_t batch_mask;

		/* This is a bit awkward, probably a fd_batch_flush_locked()
		 * would make things simpler.. but we need to hold the lock
		 * to iterate the batches which reference this resource.  So
		 * we must first grab references under a lock, then flush.
		 */
		fd_screen_lock(ctx->screen);
		batch_mask = rsc->batch_mask;
		foreach_batch(batch, &ctx->screen->batch_cache, batch_mask)
			fd_batch_reference_locked(&batches[batch->idx], batch);
		fd_screen_unlock(ctx->screen);

		foreach_batch(batch, &ctx->screen->batch_cache, batch_mask)
			fd_batch_flush(batch);

		foreach_batch(batch, &ctx->screen->batch_cache, batch_mask) {
			fd_batch_reference(&batches[batch->idx], NULL);
		}
		assert(rsc->batch_mask == 0);
	} else if (write_batch) {
		fd_batch_flush(write_batch);
	}

	fd_batch_reference(&write_batch, NULL);

	assert(!rsc->write_batch);
}

static void
fd_flush_resource(struct pipe_context *pctx, struct pipe_resource *prsc)
{
	flush_resource(fd_context(pctx), fd_resource(prsc), PIPE_MAP_READ);
}

static void
fd_resource_transfer_unmap(struct pipe_context *pctx,
		struct pipe_transfer *ptrans)
{
	struct fd_context *ctx = fd_context(pctx);
	struct fd_resource *rsc = fd_resource(ptrans->resource);
	struct fd_transfer *trans = fd_transfer(ptrans);

	if (trans->staging_prsc) {
		if (ptrans->usage & PIPE_MAP_WRITE)
			fd_blit_from_staging(ctx, trans);
		pipe_resource_reference(&trans->staging_prsc, NULL);
	}

	if (!(ptrans->usage & PIPE_MAP_UNSYNCHRONIZED)) {
		fd_bo_cpu_fini(rsc->bo);
	}

	util_range_add(&rsc->base, &rsc->valid_buffer_range,
				   ptrans->box.x,
				   ptrans->box.x + ptrans->box.width);

	pipe_resource_reference(&ptrans->resource, NULL);
	slab_free(&ctx->transfer_pool, ptrans);
}

static void *
fd_resource_transfer_map(struct pipe_context *pctx,
		struct pipe_resource *prsc,
		unsigned level, unsigned usage,
		const struct pipe_box *box,
		struct pipe_transfer **pptrans)
{
	struct fd_context *ctx = fd_context(pctx);
	struct fd_resource *rsc = fd_resource(prsc);
	struct fd_transfer *trans;
	struct pipe_transfer *ptrans;
	enum pipe_format format = prsc->format;
	uint32_t op = 0;
	uint32_t offset;
	char *buf;
	int ret = 0;

	DBG("prsc=%p, level=%u, usage=%x, box=%dx%d+%d,%d", prsc, level, usage,
		box->width, box->height, box->x, box->y);

	if ((usage & PIPE_MAP_DIRECTLY) && rsc->layout.tile_mode) {
		DBG("CANNOT MAP DIRECTLY!\n");
		return NULL;
	}

	ptrans = slab_alloc(&ctx->transfer_pool);
	if (!ptrans)
		return NULL;

	/* slab_alloc_st() doesn't zero: */
	trans = fd_transfer(ptrans);
	memset(trans, 0, sizeof(*trans));

	pipe_resource_reference(&ptrans->resource, prsc);
	ptrans->level = level;
	ptrans->usage = usage;
	ptrans->box = *box;
	ptrans->stride = fd_resource_pitch(rsc, level);
	ptrans->layer_stride = fd_resource_layer_stride(rsc, level);

	/* we always need a staging texture for tiled buffers:
	 *
	 * TODO we might sometimes want to *also* shadow the resource to avoid
	 * splitting a batch.. for ex, mid-frame texture uploads to a tiled
	 * texture.
	 */
	if (rsc->layout.tile_mode) {
		struct fd_resource *staging_rsc;

		staging_rsc = fd_alloc_staging(ctx, rsc, level, box);
		if (staging_rsc) {
			// TODO for PIPE_MAP_READ, need to do untiling blit..
			trans->staging_prsc = &staging_rsc->base;
			trans->base.stride = fd_resource_pitch(staging_rsc, 0);
			trans->base.layer_stride = fd_resource_layer_stride(staging_rsc, 0);
			trans->staging_box = *box;
			trans->staging_box.x = 0;
			trans->staging_box.y = 0;
			trans->staging_box.z = 0;

			if (usage & PIPE_MAP_READ) {
				fd_blit_to_staging(ctx, trans);

				fd_bo_cpu_prep(staging_rsc->bo, ctx->pipe,
						DRM_FREEDRENO_PREP_READ);
			}

			buf = fd_bo_map(staging_rsc->bo);
			offset = 0;

			*pptrans = ptrans;

			ctx->stats.staging_uploads++;

			return buf;
		}
	}

	if (ctx->in_shadow && !(usage & PIPE_MAP_READ))
		usage |= PIPE_MAP_UNSYNCHRONIZED;

	if (usage & PIPE_MAP_READ)
		op |= DRM_FREEDRENO_PREP_READ;

	if (usage & PIPE_MAP_WRITE)
		op |= DRM_FREEDRENO_PREP_WRITE;

	bool needs_flush = pending(rsc, !!(usage & PIPE_MAP_WRITE));

	if (usage & PIPE_MAP_DISCARD_WHOLE_RESOURCE) {
		if (needs_flush || fd_resource_busy(rsc, op)) {
			rebind_resource(rsc);
			realloc_bo(rsc, fd_bo_size(rsc->bo));
		}
	} else if ((usage & PIPE_MAP_WRITE) &&
			   prsc->target == PIPE_BUFFER &&
			   !util_ranges_intersect(&rsc->valid_buffer_range,
									  box->x, box->x + box->width)) {
		/* We are trying to write to a previously uninitialized range. No need
		 * to wait.
		 */
	} else if (!(usage & PIPE_MAP_UNSYNCHRONIZED)) {
		struct fd_batch *write_batch = NULL;

		/* hold a reference, so it doesn't disappear under us: */
		fd_screen_lock(ctx->screen);
		fd_batch_reference_locked(&write_batch, rsc->write_batch);
		fd_screen_unlock(ctx->screen);

		if ((usage & PIPE_MAP_WRITE) && write_batch &&
				write_batch->back_blit) {
			/* if only thing pending is a back-blit, we can discard it: */
			fd_batch_reset(write_batch);
		}

		/* If the GPU is writing to the resource, or if it is reading from the
		 * resource and we're trying to write to it, flush the renders.
		 */
		bool busy = needs_flush || fd_resource_busy(rsc, op);

		/* if we need to flush/stall, see if we can make a shadow buffer
		 * to avoid this:
		 *
		 * TODO we could go down this path !reorder && !busy_for_read
		 * ie. we only *don't* want to go down this path if the blit
		 * will trigger a flush!
		 */
		if (ctx->screen->reorder && busy && !(usage & PIPE_MAP_READ) &&
				(usage & PIPE_MAP_DISCARD_RANGE)) {
			/* try shadowing only if it avoids a flush, otherwise staging would
			 * be better:
			 */
			if (needs_flush && fd_try_shadow_resource(ctx, rsc, level,
							box, DRM_FORMAT_MOD_LINEAR)) {
				needs_flush = busy = false;
				ctx->stats.shadow_uploads++;
			} else {
				struct fd_resource *staging_rsc;

				if (needs_flush) {
					flush_resource(ctx, rsc, usage);
					needs_flush = false;
				}

				/* in this case, we don't need to shadow the whole resource,
				 * since any draw that references the previous contents has
				 * already had rendering flushed for all tiles.  So we can
				 * use a staging buffer to do the upload.
				 */
				staging_rsc = fd_alloc_staging(ctx, rsc, level, box);
				if (staging_rsc) {
					trans->staging_prsc = &staging_rsc->base;
					trans->base.stride = fd_resource_pitch(staging_rsc, 0);
					trans->base.layer_stride =
						fd_resource_layer_stride(staging_rsc, 0);
					trans->staging_box = *box;
					trans->staging_box.x = 0;
					trans->staging_box.y = 0;
					trans->staging_box.z = 0;
					buf = fd_bo_map(staging_rsc->bo);
					offset = 0;

					*pptrans = ptrans;

					fd_batch_reference(&write_batch, NULL);

					ctx->stats.staging_uploads++;

					return buf;
				}
			}
		}

		if (needs_flush) {
			flush_resource(ctx, rsc, usage);
			needs_flush = false;
		}

		fd_batch_reference(&write_batch, NULL);

		/* The GPU keeps track of how the various bo's are being used, and
		 * will wait if necessary for the proper operation to have
		 * completed.
		 */
		if (busy) {
			ret = fd_bo_cpu_prep(rsc->bo, ctx->pipe, op);
			if (ret)
				goto fail;
		}
	}

	buf = fd_bo_map(rsc->bo);
	offset =
		box->y / util_format_get_blockheight(format) * ptrans->stride +
		box->x / util_format_get_blockwidth(format) * rsc->layout.cpp +
		fd_resource_offset(rsc, level, box->z);

	if (usage & PIPE_MAP_WRITE)
		rsc->valid = true;

	*pptrans = ptrans;

	return buf + offset;

fail:
	fd_resource_transfer_unmap(pctx, ptrans);
	return NULL;
}

static void
fd_resource_destroy(struct pipe_screen *pscreen,
		struct pipe_resource *prsc)
{
	struct fd_resource *rsc = fd_resource(prsc);
	fd_bc_invalidate_resource(rsc, true);
	if (rsc->bo)
		fd_bo_del(rsc->bo);
	if (rsc->lrz)
		fd_bo_del(rsc->lrz);
	if (rsc->scanout)
		renderonly_scanout_destroy(rsc->scanout, fd_screen(pscreen)->ro);

	util_range_destroy(&rsc->valid_buffer_range);
	simple_mtx_destroy(&rsc->lock);
	FREE(rsc);
}

static uint64_t
fd_resource_modifier(struct fd_resource *rsc)
{
	if (!rsc->layout.tile_mode)
		return DRM_FORMAT_MOD_LINEAR;

	if (rsc->layout.ubwc_layer_size)
		return DRM_FORMAT_MOD_QCOM_COMPRESSED;

	/* TODO invent a modifier for tiled but not UBWC buffers: */
	return DRM_FORMAT_MOD_INVALID;
}

static bool
fd_resource_get_handle(struct pipe_screen *pscreen,
		struct pipe_context *pctx,
		struct pipe_resource *prsc,
		struct winsys_handle *handle,
		unsigned usage)
{
	struct fd_resource *rsc = fd_resource(prsc);

	handle->modifier = fd_resource_modifier(rsc);

	DBG("%p: target=%d, format=%s, %ux%ux%u, array_size=%u, last_level=%u, "
			"nr_samples=%u, usage=%u, bind=%x, flags=%x, modifier=%"PRIx64,
			prsc, prsc->target, util_format_name(prsc->format),
			prsc->width0, prsc->height0, prsc->depth0,
			prsc->array_size, prsc->last_level, prsc->nr_samples,
			prsc->usage, prsc->bind, prsc->flags,
			handle->modifier);

	return fd_screen_bo_get_handle(pscreen, rsc->bo, rsc->scanout,
			fd_resource_pitch(rsc, 0), handle);
}

/* special case to resize query buf after allocated.. */
void
fd_resource_resize(struct pipe_resource *prsc, uint32_t sz)
{
	struct fd_resource *rsc = fd_resource(prsc);

	debug_assert(prsc->width0 == 0);
	debug_assert(prsc->target == PIPE_BUFFER);
	debug_assert(prsc->bind == PIPE_BIND_QUERY_BUFFER);

	prsc->width0 = sz;
	realloc_bo(rsc, fd_screen(prsc->screen)->setup_slices(rsc));
}

static void
fd_resource_layout_init(struct pipe_resource *prsc)
{
	struct fd_resource *rsc = fd_resource(prsc);
	struct fdl_layout *layout = &rsc->layout;

	layout->format = prsc->format;

	layout->width0 = prsc->width0;
	layout->height0 = prsc->height0;
	layout->depth0 = prsc->depth0;

	layout->cpp = util_format_get_blocksize(prsc->format);
	layout->cpp *= fd_resource_nr_samples(prsc);
	layout->cpp_shift = ffs(layout->cpp) - 1;
}

/**
 * Helper that allocates a resource and resolves its layout (but doesn't
 * allocate its bo).
 *
 * It returns a pipe_resource (as fd_resource_create_with_modifiers()
 * would do), and also bo's minimum required size as an output argument.
 */
static struct pipe_resource *
fd_resource_allocate_and_resolve(struct pipe_screen *pscreen,
		const struct pipe_resource *tmpl,
		const uint64_t *modifiers, int count, uint32_t *psize)
{
	struct fd_screen *screen = fd_screen(pscreen);
	struct fd_resource *rsc;
	struct pipe_resource *prsc;
	enum pipe_format format = tmpl->format;
	uint32_t size;

	rsc = CALLOC_STRUCT(fd_resource);
	prsc = &rsc->base;

	DBG("%p: target=%d, format=%s, %ux%ux%u, array_size=%u, last_level=%u, "
			"nr_samples=%u, usage=%u, bind=%x, flags=%x", prsc,
			tmpl->target, util_format_name(format),
			tmpl->width0, tmpl->height0, tmpl->depth0,
			tmpl->array_size, tmpl->last_level, tmpl->nr_samples,
			tmpl->usage, tmpl->bind, tmpl->flags);

	if (!rsc)
		return NULL;

	*prsc = *tmpl;
	fd_resource_layout_init(prsc);

#define LINEAR \
	(PIPE_BIND_SCANOUT | \
	 PIPE_BIND_LINEAR  | \
	 PIPE_BIND_DISPLAY_TARGET)

	bool linear = drm_find_modifier(DRM_FORMAT_MOD_LINEAR, modifiers, count);
	if (tmpl->bind & LINEAR)
		linear = true;

	if (fd_mesa_debug & FD_DBG_NOTILE)
		linear = true;

	/* Normally, for non-shared buffers, allow buffer compression if
	 * not shared, otherwise only allow if QCOM_COMPRESSED modifier
	 * is requested:
	 *
	 * TODO we should probably also limit tiled in a similar way,
	 * except we don't have a format modifier for tiled.  (We probably
	 * should.)
	 */
	bool allow_ubwc = drm_find_modifier(DRM_FORMAT_MOD_INVALID, modifiers, count);
	if (tmpl->bind & PIPE_BIND_SHARED) {
		allow_ubwc = drm_find_modifier(DRM_FORMAT_MOD_QCOM_COMPRESSED, modifiers, count);
		if (!allow_ubwc) {
			linear = true;
		}
	}

	allow_ubwc &= !(fd_mesa_debug & FD_DBG_NOUBWC);

	pipe_reference_init(&prsc->reference, 1);

	prsc->screen = pscreen;

	if (screen->tile_mode &&
			(tmpl->target != PIPE_BUFFER) &&
			!linear) {
		rsc->layout.tile_mode = screen->tile_mode(prsc);
	}

	util_range_init(&rsc->valid_buffer_range);

	simple_mtx_init(&rsc->lock, mtx_plain);

	rsc->internal_format = format;

	rsc->layout.ubwc = rsc->layout.tile_mode && is_a6xx(screen) && allow_ubwc;

	if (prsc->target == PIPE_BUFFER) {
		assert(prsc->format == PIPE_FORMAT_R8_UNORM);
		size = prsc->width0;
		fdl_layout_buffer(&rsc->layout, size);
	} else {
		size = screen->setup_slices(rsc);
	}

	/* special case for hw-query buffer, which we need to allocate before we
	 * know the size:
	 */
	if (size == 0) {
		/* note, semi-intention == instead of & */
		debug_assert(prsc->bind == PIPE_BIND_QUERY_BUFFER);
		*psize = 0;
		return prsc;
	}

	/* Set the layer size if the (non-a6xx) backend hasn't done so. */
	if (rsc->layout.layer_first && !rsc->layout.layer_size) {
		rsc->layout.layer_size = align(size, 4096);
		size = rsc->layout.layer_size * prsc->array_size;
	}

	if (fd_mesa_debug & FD_DBG_LAYOUT)
		fdl_dump_layout(&rsc->layout);

	/* Hand out the resolved size. */
	if (psize)
		*psize = size;

	return prsc;
}

/**
 * Create a new texture object, using the given template info.
 */
static struct pipe_resource *
fd_resource_create_with_modifiers(struct pipe_screen *pscreen,
		const struct pipe_resource *tmpl,
		const uint64_t *modifiers, int count)
{
	struct fd_screen *screen = fd_screen(pscreen);
	struct fd_resource *rsc;
	struct pipe_resource *prsc;
	uint32_t size;

	/* when using kmsro, scanout buffers are allocated on the display device
	 * create_with_modifiers() doesn't give us usage flags, so we have to
	 * assume that all calls with modifiers are scanout-possible
	 */
	if (screen->ro &&
		((tmpl->bind & PIPE_BIND_SCANOUT) ||
		 !(count == 1 && modifiers[0] == DRM_FORMAT_MOD_INVALID))) {
		struct pipe_resource scanout_templat = *tmpl;
		struct renderonly_scanout *scanout;
		struct winsys_handle handle;

		/* note: alignment is wrong for a6xx */
		scanout_templat.width0 = align(tmpl->width0, screen->info.gmem_align_w);

		scanout = renderonly_scanout_for_resource(&scanout_templat,
												  screen->ro, &handle);
		if (!scanout)
			return NULL;

		renderonly_scanout_destroy(scanout, screen->ro);

		assert(handle.type == WINSYS_HANDLE_TYPE_FD);
		rsc = fd_resource(pscreen->resource_from_handle(pscreen, tmpl,
														&handle,
														PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE));
		close(handle.handle);
		if (!rsc)
			return NULL;

		return &rsc->base;
	}

	prsc = fd_resource_allocate_and_resolve(pscreen, tmpl, modifiers, count, &size);
	if (!prsc)
		return NULL;
	rsc = fd_resource(prsc);

	realloc_bo(rsc, size);
	if (!rsc->bo)
		goto fail;

	return prsc;
fail:
	fd_resource_destroy(pscreen, prsc);
	return NULL;
}

static struct pipe_resource *
fd_resource_create(struct pipe_screen *pscreen,
		const struct pipe_resource *tmpl)
{
	const uint64_t mod = DRM_FORMAT_MOD_INVALID;
	return fd_resource_create_with_modifiers(pscreen, tmpl, &mod, 1);
}

/**
 * Create a texture from a winsys_handle. The handle is often created in
 * another process by first creating a pipe texture and then calling
 * resource_get_handle.
 */
static struct pipe_resource *
fd_resource_from_handle(struct pipe_screen *pscreen,
		const struct pipe_resource *tmpl,
		struct winsys_handle *handle, unsigned usage)
{
	struct fd_screen *screen = fd_screen(pscreen);
	struct fd_resource *rsc = CALLOC_STRUCT(fd_resource);

	if (!rsc)
		return NULL;

	struct fdl_slice *slice = fd_resource_slice(rsc, 0);
	struct pipe_resource *prsc = &rsc->base;

	DBG("%p: target=%d, format=%s, %ux%ux%u, array_size=%u, last_level=%u, "
			"nr_samples=%u, usage=%u, bind=%x, flags=%x, modifier=%"PRIx64,
			prsc, tmpl->target, util_format_name(tmpl->format),
			tmpl->width0, tmpl->height0, tmpl->depth0,
			tmpl->array_size, tmpl->last_level, tmpl->nr_samples,
			tmpl->usage, tmpl->bind, tmpl->flags,
			handle->modifier);

	*prsc = *tmpl;
	fd_resource_layout_init(prsc);

	pipe_reference_init(&prsc->reference, 1);

	prsc->screen = pscreen;

	util_range_init(&rsc->valid_buffer_range);

	simple_mtx_init(&rsc->lock, mtx_plain);

	struct fd_bo *bo = fd_screen_bo_from_handle(pscreen, handle);
	if (!bo)
		goto fail;

	fd_resource_set_bo(rsc, bo);

	rsc->internal_format = tmpl->format;
	rsc->layout.pitch0 = handle->stride;
	slice->offset = handle->offset;
	slice->size0 = handle->stride * prsc->height0;

	/* use a pitchalign of gmem_align_w pixels, because GMEM resolve for
	 * lower alignments is not implemented (but possible for a6xx at least)
	 *
	 * for UBWC-enabled resources, layout_resource_for_modifier will further
	 * validate the pitch and set the right pitchalign
	 */
	rsc->layout.pitchalign =
		fdl_cpp_shift(&rsc->layout) + util_logbase2(screen->info.gmem_align_w);

	/* apply the minimum pitchalign (note: actually 4 for a3xx but doesn't matter) */
	if (is_a6xx(screen) || is_a5xx(screen))
		rsc->layout.pitchalign = MAX2(rsc->layout.pitchalign, 6);
	else
		rsc->layout.pitchalign = MAX2(rsc->layout.pitchalign, 5);

	if (rsc->layout.pitch0 < (prsc->width0 * rsc->layout.cpp) ||
		fd_resource_pitch(rsc, 0) != rsc->layout.pitch0)
		goto fail;

	assert(rsc->layout.cpp);

	if (screen->layout_resource_for_modifier(rsc, handle->modifier) < 0)
		goto fail;

	if (screen->ro) {
		rsc->scanout =
			renderonly_create_gpu_import_for_resource(prsc, screen->ro, NULL);
		/* failure is expected in some cases.. */
	}

	rsc->valid = true;

	return prsc;

fail:
	fd_resource_destroy(pscreen, prsc);
	return NULL;
}

bool
fd_render_condition_check(struct pipe_context *pctx)
{
	struct fd_context *ctx = fd_context(pctx);

	if (!ctx->cond_query)
		return true;

	union pipe_query_result res = { 0 };
	bool wait =
		ctx->cond_mode != PIPE_RENDER_COND_NO_WAIT &&
		ctx->cond_mode != PIPE_RENDER_COND_BY_REGION_NO_WAIT;

	if (pctx->get_query_result(pctx, ctx->cond_query, wait, &res))
			return (bool)res.u64 != ctx->cond_cond;

	return true;
}

static void
fd_invalidate_resource(struct pipe_context *pctx, struct pipe_resource *prsc)
{
	struct fd_context *ctx = fd_context(pctx);
	struct fd_resource *rsc = fd_resource(prsc);

	/*
	 * TODO I guess we could track that the resource is invalidated and
	 * use that as a hint to realloc rather than stall in _transfer_map(),
	 * even in the non-DISCARD_WHOLE_RESOURCE case?
	 *
	 * Note: we set dirty bits to trigger invalidate logic fd_draw_vbo
	 */

	if (rsc->write_batch) {
		struct fd_batch *batch = rsc->write_batch;
		struct pipe_framebuffer_state *pfb = &batch->framebuffer;

		if (pfb->zsbuf && pfb->zsbuf->texture == prsc) {
			batch->resolve &= ~(FD_BUFFER_DEPTH | FD_BUFFER_STENCIL);
			ctx->dirty |= FD_DIRTY_ZSA;
		}

		for (unsigned i = 0; i < pfb->nr_cbufs; i++) {
			if (pfb->cbufs[i] && pfb->cbufs[i]->texture == prsc) {
				batch->resolve &= ~(PIPE_CLEAR_COLOR0 << i);
				ctx->dirty |= FD_DIRTY_FRAMEBUFFER;
			}
		}
	}

	rsc->valid = false;
}

static enum pipe_format
fd_resource_get_internal_format(struct pipe_resource *prsc)
{
	return fd_resource(prsc)->internal_format;
}

static void
fd_resource_set_stencil(struct pipe_resource *prsc,
		struct pipe_resource *stencil)
{
	fd_resource(prsc)->stencil = fd_resource(stencil);
}

static struct pipe_resource *
fd_resource_get_stencil(struct pipe_resource *prsc)
{
	struct fd_resource *rsc = fd_resource(prsc);
	if (rsc->stencil)
		return &rsc->stencil->base;
	return NULL;
}

static const struct u_transfer_vtbl transfer_vtbl = {
		.resource_create          = fd_resource_create,
		.resource_destroy         = fd_resource_destroy,
		.transfer_map             = fd_resource_transfer_map,
		.transfer_flush_region    = fd_resource_transfer_flush_region,
		.transfer_unmap           = fd_resource_transfer_unmap,
		.get_internal_format      = fd_resource_get_internal_format,
		.set_stencil              = fd_resource_set_stencil,
		.get_stencil              = fd_resource_get_stencil,
};

static const uint64_t supported_modifiers[] = {
	DRM_FORMAT_MOD_LINEAR,
};

static int
fd_layout_resource_for_modifier(struct fd_resource *rsc, uint64_t modifier)
{
	switch (modifier) {
	case DRM_FORMAT_MOD_LINEAR:
		/* The dri gallium frontend will pass DRM_FORMAT_MOD_INVALID to us
		 * when it's called through any of the non-modifier BO create entry
		 * points.  Other drivers will determine tiling from the kernel or
		 * other legacy backchannels, but for freedreno it just means
		 * LINEAR. */
	case DRM_FORMAT_MOD_INVALID:
		return 0;
	default:
		return -1;
	}
}

static struct pipe_resource *
fd_resource_from_memobj(struct pipe_screen *pscreen,
						const struct pipe_resource *tmpl,
						struct pipe_memory_object *pmemobj,
						uint64_t offset)
{
	struct fd_screen *screen = fd_screen(pscreen);
	struct fd_memory_object *memobj = fd_memory_object(pmemobj);
	struct pipe_resource *prsc;
	struct fd_resource *rsc;
	uint32_t size;
	assert(memobj->bo);

	/* We shouldn't get a scanout buffer here. */
	assert(!(tmpl->bind & PIPE_BIND_SCANOUT));

	uint64_t modifiers = DRM_FORMAT_MOD_INVALID;
	if (tmpl->bind & PIPE_BIND_LINEAR) {
		modifiers = DRM_FORMAT_MOD_LINEAR;
	} else if (is_a6xx(screen) && tmpl->width0 >= FDL_MIN_UBWC_WIDTH) {
		modifiers = DRM_FORMAT_MOD_QCOM_COMPRESSED;
	}

	/* Allocate new pipe resource. */
	prsc = fd_resource_allocate_and_resolve(pscreen, tmpl, &modifiers, 1, &size);
	if (!prsc)
		return NULL;
	rsc = fd_resource(prsc);

	/* bo's size has to be large enough, otherwise cleanup resource and fail
	 * gracefully.
	 */
	if (fd_bo_size(memobj->bo) < size) {
		fd_resource_destroy(pscreen, prsc);
		return NULL;
	}

	/* Share the bo with the memory object. */
	fd_resource_set_bo(rsc, fd_bo_ref(memobj->bo));

	return prsc;
}

static struct pipe_memory_object *
fd_memobj_create_from_handle(struct pipe_screen *pscreen,
							 struct winsys_handle *whandle,
							 bool dedicated)
{
	struct fd_memory_object *memobj = CALLOC_STRUCT(fd_memory_object);
	if (!memobj)
		return NULL;

	struct fd_bo *bo = fd_screen_bo_from_handle(pscreen, whandle);
	if (!bo) {
		free(memobj);
		return NULL;
	}

	memobj->b.dedicated = dedicated;
	memobj->bo = bo;

	return &memobj->b;
}

static void
fd_memobj_destroy(struct pipe_screen *pscreen,
		struct pipe_memory_object *pmemobj)
{
	struct fd_memory_object *memobj = fd_memory_object(pmemobj);

	assert(memobj->bo);
	fd_bo_del(memobj->bo);

	free(pmemobj);
}

void
fd_resource_screen_init(struct pipe_screen *pscreen)
{
	struct fd_screen *screen = fd_screen(pscreen);
	bool fake_rgtc = screen->gpu_id < 400;

	pscreen->resource_create = u_transfer_helper_resource_create;
	/* NOTE: u_transfer_helper does not yet support the _with_modifiers()
	 * variant:
	 */
	pscreen->resource_create_with_modifiers = fd_resource_create_with_modifiers;
	pscreen->resource_from_handle = fd_resource_from_handle;
	pscreen->resource_get_handle = fd_resource_get_handle;
	pscreen->resource_destroy = u_transfer_helper_resource_destroy;

	pscreen->transfer_helper = u_transfer_helper_create(&transfer_vtbl,
			true, false, fake_rgtc, true);

	if (!screen->layout_resource_for_modifier)
		screen->layout_resource_for_modifier = fd_layout_resource_for_modifier;
	if (!screen->supported_modifiers) {
		screen->supported_modifiers = supported_modifiers;
		screen->num_supported_modifiers = ARRAY_SIZE(supported_modifiers);
	}

	/* GL_EXT_memory_object */
	pscreen->memobj_create_from_handle = fd_memobj_create_from_handle;
	pscreen->memobj_destroy = fd_memobj_destroy;
	pscreen->resource_from_memobj = fd_resource_from_memobj;
}

static void
fd_get_sample_position(struct pipe_context *context,
                         unsigned sample_count, unsigned sample_index,
                         float *pos_out)
{
	/* The following is copied from nouveau/nv50 except for position
	 * values, which are taken from blob driver */
	static const uint8_t pos1[1][2] = { { 0x8, 0x8 } };
	static const uint8_t pos2[2][2] = {
		{ 0xc, 0xc }, { 0x4, 0x4 } };
	static const uint8_t pos4[4][2] = {
		{ 0x6, 0x2 }, { 0xe, 0x6 },
		{ 0x2, 0xa }, { 0xa, 0xe } };
	/* TODO needs to be verified on supported hw */
	static const uint8_t pos8[8][2] = {
		{ 0x9, 0x5 }, { 0x7, 0xb },
		{ 0xd, 0x9 }, { 0x5, 0x3 },
		{ 0x3, 0xd }, { 0x1, 0x7 },
		{ 0xb, 0xf }, { 0xf, 0x1 } };

	const uint8_t (*ptr)[2];

	switch (sample_count) {
	case 1:
		ptr = pos1;
		break;
	case 2:
		ptr = pos2;
		break;
	case 4:
		ptr = pos4;
		break;
	case 8:
		ptr = pos8;
		break;
	default:
		assert(0);
		return;
	}

	pos_out[0] = ptr[sample_index][0] / 16.0f;
	pos_out[1] = ptr[sample_index][1] / 16.0f;
}

static void
fd_blit_pipe(struct pipe_context *pctx, const struct pipe_blit_info *blit_info)
{
	/* wrap fd_blit to return void */
	fd_blit(pctx, blit_info);
}

void
fd_resource_context_init(struct pipe_context *pctx)
{
	pctx->transfer_map = u_transfer_helper_transfer_map;
	pctx->transfer_flush_region = u_transfer_helper_transfer_flush_region;
	pctx->transfer_unmap = u_transfer_helper_transfer_unmap;
	pctx->buffer_subdata = u_default_buffer_subdata;
	pctx->texture_subdata = u_default_texture_subdata;
	pctx->create_surface = fd_create_surface;
	pctx->surface_destroy = fd_surface_destroy;
	pctx->resource_copy_region = fd_resource_copy_region;
	pctx->blit = fd_blit_pipe;
	pctx->flush_resource = fd_flush_resource;
	pctx->invalidate_resource = fd_invalidate_resource;
	pctx->get_sample_position = fd_get_sample_position;
}
