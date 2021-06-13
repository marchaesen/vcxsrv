/*
 * Copyright (C) 2019-2020 Collabora, Ltd.
 * Copyright (C) 2019 Alyssa Rosenzweig
 * Copyright (C) 2014-2017 Broadcom
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
 */

#include <assert.h>

#include "drm-uapi/panfrost_drm.h"

#include "pan_bo.h"
#include "pan_blitter.h"
#include "pan_context.h"
#include "util/hash_table.h"
#include "util/ralloc.h"
#include "util/format/u_format.h"
#include "util/u_pack_color.h"
#include "util/rounding.h"
#include "util/u_framebuffer.h"
#include "pan_util.h"
#include "pan_cmdstream.h"
#include "decode.h"
#include "panfrost-quirks.h"

/* panfrost_bo_access is here to help us keep track of batch accesses to BOs
 * and build a proper dependency graph such that batches can be pipelined for
 * better GPU utilization.
 *
 * Each accessed BO has a corresponding entry in the ->accessed_bos hash table.
 * A BO is either being written or read at any time (see last_is_write).
 * When the last access is a write, the batch writing the BO might have read
 * dependencies (readers that have not been executed yet and want to read the
 * previous BO content), and when the last access is a read, all readers might
 * depend on another batch to push its results to memory. That's what the
 * readers/writers keep track off.
 * There can only be one writer at any given time, if a new batch wants to
 * write to the same BO, a dependency will be added between the new writer and
 * the old writer (at the batch level), and panfrost_bo_access->writer will be
 * updated to point to the new writer.
 */
struct panfrost_bo_access {
        struct util_dynarray readers;
        struct panfrost_batch_fence *writer;
        bool last_is_write;
};

static struct panfrost_batch_fence *
panfrost_create_batch_fence(struct panfrost_batch *batch)
{
        struct panfrost_batch_fence *fence;

        fence = rzalloc(NULL, struct panfrost_batch_fence);
        assert(fence);
        pipe_reference_init(&fence->reference, 1);
        fence->batch = batch;

        return fence;
}

static void
panfrost_free_batch_fence(struct panfrost_batch_fence *fence)
{
        ralloc_free(fence);
}

void
panfrost_batch_fence_unreference(struct panfrost_batch_fence *fence)
{
        if (pipe_reference(&fence->reference, NULL))
                 panfrost_free_batch_fence(fence);
}

void
panfrost_batch_fence_reference(struct panfrost_batch_fence *fence)
{
        pipe_reference(NULL, &fence->reference);
}

static void
panfrost_batch_add_fbo_bos(struct panfrost_batch *batch);

static struct panfrost_batch *
panfrost_create_batch(struct panfrost_context *ctx,
                      const struct pipe_framebuffer_state *key)
{
        struct panfrost_batch *batch = rzalloc(ctx, struct panfrost_batch);
        struct panfrost_device *dev = pan_device(ctx->base.screen);

        batch->ctx = ctx;

        batch->bos = _mesa_hash_table_create(batch, _mesa_hash_pointer,
                        _mesa_key_pointer_equal);

        batch->minx = batch->miny = ~0;
        batch->maxx = batch->maxy = 0;

        batch->out_sync = panfrost_create_batch_fence(batch);
        util_copy_framebuffer_state(&batch->key, key);

        /* Preallocate the main pool, since every batch has at least one job
         * structure so it will be used */
        panfrost_pool_init(&batch->pool, batch, dev, 0, true);

        /* Don't preallocate the invisible pool, since not every batch will use
         * the pre-allocation, particularly if the varyings are larger than the
         * preallocation and a reallocation is needed after anyway. */
        panfrost_pool_init(&batch->invisible_pool, batch, dev, PAN_BO_INVISIBLE, false);

        panfrost_batch_add_fbo_bos(batch);

        return batch;
}

static void
panfrost_freeze_batch(struct panfrost_batch *batch)
{
        struct panfrost_context *ctx = batch->ctx;
        struct hash_entry *entry;

        /* Remove the entry in the FBO -> batch hash table if the batch
         * matches and drop the context reference. This way, next draws/clears
         * targeting this FBO will trigger the creation of a new batch.
         */
        entry = _mesa_hash_table_search(ctx->batches, &batch->key);
        if (entry && entry->data == batch)
                _mesa_hash_table_remove(ctx->batches, entry);

        if (ctx->batch == batch)
                ctx->batch = NULL;
}

#ifdef PAN_BATCH_DEBUG
static bool panfrost_batch_is_frozen(struct panfrost_batch *batch)
{
        struct panfrost_context *ctx = batch->ctx;
        struct hash_entry *entry;

        entry = _mesa_hash_table_search(ctx->batches, &batch->key);
        if (entry && entry->data == batch)
                return false;

        if (ctx->batch == batch)
                return false;

        return true;
}
#endif

static void
panfrost_free_batch(struct panfrost_batch *batch)
{
        if (!batch)
                return;

#ifdef PAN_BATCH_DEBUG
        assert(panfrost_batch_is_frozen(batch));
#endif

        hash_table_foreach(batch->bos, entry)
                panfrost_bo_unreference((struct panfrost_bo *)entry->key);

        panfrost_pool_cleanup(&batch->pool);
        panfrost_pool_cleanup(&batch->invisible_pool);

        util_dynarray_foreach(&batch->dependencies,
                              struct panfrost_batch_fence *, dep) {
                panfrost_batch_fence_unreference(*dep);
        }

        util_dynarray_fini(&batch->dependencies);

        /* The out_sync fence lifetime is different from the the batch one
         * since other batches might want to wait on a fence of already
         * submitted/signaled batch. All we need to do here is make sure the
         * fence does not point to an invalid batch, which the core will
         * interpret as 'batch is already submitted'.
         */
        batch->out_sync->batch = NULL;
        panfrost_batch_fence_unreference(batch->out_sync);

        util_unreference_framebuffer_state(&batch->key);
        ralloc_free(batch);
}

#ifdef PAN_BATCH_DEBUG
static bool
panfrost_dep_graph_contains_batch(struct panfrost_batch *root,
                                  struct panfrost_batch *batch)
{
        if (!root)
                return false;

        util_dynarray_foreach(&root->dependencies,
                              struct panfrost_batch_fence *, dep) {
                if ((*dep)->batch == batch ||
                    panfrost_dep_graph_contains_batch((*dep)->batch, batch))
                        return true;
        }

        return false;
}
#endif

static void
panfrost_batch_add_dep(struct panfrost_batch *batch,
                       struct panfrost_batch_fence *newdep)
{
        if (batch == newdep->batch)
                return;

        /* We might want to turn ->dependencies into a set if the number of
         * deps turns out to be big enough to make this 'is dep already there'
         * search inefficient.
         */
        util_dynarray_foreach(&batch->dependencies,
                              struct panfrost_batch_fence *, dep) {
                if (*dep == newdep)
                        return;
        }

#ifdef PAN_BATCH_DEBUG
        /* Make sure the dependency graph is acyclic. */
        assert(!panfrost_dep_graph_contains_batch(newdep->batch, batch));
#endif

        panfrost_batch_fence_reference(newdep);
        util_dynarray_append(&batch->dependencies,
                             struct panfrost_batch_fence *, newdep);

        /* We now have a batch depending on us, let's make sure new draw/clear
         * calls targeting the same FBO use a new batch object.
         */
        if (newdep->batch)
                panfrost_freeze_batch(newdep->batch);
}

static struct panfrost_batch *
panfrost_get_batch(struct panfrost_context *ctx,
                   const struct pipe_framebuffer_state *key)
{
        /* Lookup the job first */
        struct hash_entry *entry = _mesa_hash_table_search(ctx->batches, key);

        if (entry)
                return entry->data;

        /* Otherwise, let's create a job */

        struct panfrost_batch *batch = panfrost_create_batch(ctx, key);

        /* Save the created job */
        _mesa_hash_table_insert(ctx->batches, &batch->key, batch);

        return batch;
}

/* Get the job corresponding to the FBO we're currently rendering into */

struct panfrost_batch *
panfrost_get_batch_for_fbo(struct panfrost_context *ctx)
{
        /* If we already began rendering, use that */

        if (ctx->batch) {
                assert(util_framebuffer_state_equal(&ctx->batch->key,
                                                    &ctx->pipe_framebuffer));
                return ctx->batch;
        }

        /* If not, look up the job */
        struct panfrost_batch *batch = panfrost_get_batch(ctx,
                                                          &ctx->pipe_framebuffer);

        /* Set this job as the current FBO job. Will be reset when updating the
         * FB state and when submitting or releasing a job.
         */
        ctx->batch = batch;
        return batch;
}

struct panfrost_batch *
panfrost_get_fresh_batch_for_fbo(struct panfrost_context *ctx)
{
        struct panfrost_batch *batch;

        batch = panfrost_get_batch(ctx, &ctx->pipe_framebuffer);

        /* The batch has no draw/clear queued, let's return it directly.
         * Note that it's perfectly fine to re-use a batch with an
         * existing clear, we'll just update it with the new clear request.
         */
        if (!batch->scoreboard.first_job) {
                ctx->batch = batch;
                return batch;
        }

        /* Otherwise, we need to freeze the existing one and instantiate a new
         * one.
         */
        panfrost_freeze_batch(batch);
        batch = panfrost_get_batch(ctx, &ctx->pipe_framebuffer);
        ctx->batch = batch;
        return batch;
}

static void
panfrost_bo_access_gc_fences(struct panfrost_context *ctx,
                             struct panfrost_bo_access *access,
			     const struct panfrost_bo *bo)
{
        if (access->writer) {
                panfrost_batch_fence_unreference(access->writer);
                access->writer = NULL;
        }

        struct panfrost_batch_fence **readers_array = util_dynarray_begin(&access->readers);
        struct panfrost_batch_fence **new_readers = readers_array;

        util_dynarray_foreach(&access->readers, struct panfrost_batch_fence *,
                              reader) {
                if (!(*reader))
                        continue;

                panfrost_batch_fence_unreference(*reader);
                *reader = NULL;
        }

        if (!util_dynarray_resize(&access->readers, struct panfrost_batch_fence *,
                                  new_readers - readers_array) &&
            new_readers != readers_array)
                unreachable("Invalid dynarray access->readers");
}

/* Collect signaled fences to keep the kernel-side syncobj-map small. The
 * idea is to collect those signaled fences at the end of each flush_all
 * call. This function is likely to collect only fences from previous
 * batch flushes not the one that have just have just been submitted and
 * are probably still in flight when we trigger the garbage collection.
 * Anyway, we need to do this garbage collection at some point if we don't
 * want the BO access map to keep invalid entries around and retain
 * syncobjs forever.
 */
static void
panfrost_gc_fences(struct panfrost_context *ctx)
{
        hash_table_foreach(ctx->accessed_bos, entry) {
                struct panfrost_bo_access *access = entry->data;

                assert(access);
                panfrost_bo_access_gc_fences(ctx, access, entry->key);
                if (!util_dynarray_num_elements(&access->readers,
                                                struct panfrost_batch_fence *) &&
                    !access->writer) {
                        ralloc_free(access);
                        _mesa_hash_table_remove(ctx->accessed_bos, entry);
                }
        }
}

#ifdef PAN_BATCH_DEBUG
static bool
panfrost_batch_in_readers(struct panfrost_batch *batch,
                          struct panfrost_bo_access *access)
{
        util_dynarray_foreach(&access->readers, struct panfrost_batch_fence *,
                              reader) {
                if (*reader && (*reader)->batch == batch)
                        return true;
        }

        return false;
}
#endif

static void
panfrost_batch_update_bo_access(struct panfrost_batch *batch,
                                struct panfrost_bo *bo, bool writes,
                                bool already_accessed)
{
        struct panfrost_context *ctx = batch->ctx;
        struct panfrost_bo_access *access;
        bool old_writes = false;
        struct hash_entry *entry;

        entry = _mesa_hash_table_search(ctx->accessed_bos, bo);
        access = entry ? entry->data : NULL;
        if (access) {
                old_writes = access->last_is_write;
        } else {
                access = rzalloc(ctx, struct panfrost_bo_access);
                util_dynarray_init(&access->readers, access);
                _mesa_hash_table_insert(ctx->accessed_bos, bo, access);
                /* We are the first to access this BO, let's initialize
                 * old_writes to our own access type in that case.
                 */
                old_writes = writes;
        }

        assert(access);

        if (writes && !old_writes) {
                /* Previous access was a read and we want to write this BO.
                 * We first need to add explicit deps between our batch and
                 * the previous readers.
                 */
                util_dynarray_foreach(&access->readers,
                                      struct panfrost_batch_fence *, reader) {
                        /* We were already reading the BO, no need to add a dep
                         * on ourself (the acyclic check would complain about
                         * that).
                         */
                        if (!(*reader) || (*reader)->batch == batch)
                                continue;

                        panfrost_batch_add_dep(batch, *reader);
                }
                panfrost_batch_fence_reference(batch->out_sync);

                if (access->writer)
                        panfrost_batch_fence_unreference(access->writer);

                /* We now are the new writer. */
                access->writer = batch->out_sync;

                /* Release the previous readers and reset the readers array. */
                util_dynarray_foreach(&access->readers,
                                      struct panfrost_batch_fence *,
                                      reader) {
                        if (!*reader)
                                continue;
                        panfrost_batch_fence_unreference(*reader);
                }

                util_dynarray_clear(&access->readers);
        } else if (writes && old_writes) {
                /* First check if we were the previous writer, in that case
                 * there's nothing to do. Otherwise we need to add a
                 * dependency between the new writer and the old one.
                 */
		if (access->writer != batch->out_sync) {
                        if (access->writer) {
                                panfrost_batch_add_dep(batch, access->writer);
                                panfrost_batch_fence_unreference(access->writer);
                        }
                        panfrost_batch_fence_reference(batch->out_sync);
                        access->writer = batch->out_sync;
                }
        } else if (!writes && old_writes) {
                /* First check if we were the previous writer, in that case
                 * we want to keep the access type unchanged, as a write is
                 * more constraining than a read.
                 */
                if (access->writer != batch->out_sync) {
                        /* Add a dependency on the previous writer. */
                        panfrost_batch_add_dep(batch, access->writer);

                        /* The previous access was a write, there's no reason
                         * to have entries in the readers array.
                         */
                        assert(!util_dynarray_num_elements(&access->readers,
                                                           struct panfrost_batch_fence *));

                        /* Add ourselves to the readers array. */
                        panfrost_batch_fence_reference(batch->out_sync);
                        util_dynarray_append(&access->readers,
                                             struct panfrost_batch_fence *,
                                             batch->out_sync);
                }
        } else {
                /* We already accessed this BO before, so we should already be
                 * in the reader array.
                 */
#ifdef PAN_BATCH_DEBUG
                if (already_accessed) {
                        assert(panfrost_batch_in_readers(batch, access));
                        return;
                }
#endif

                /* Previous access was a read and we want to read this BO.
                 * Add ourselves to the readers array and add a dependency on
                 * the previous writer if any.
                 */
                panfrost_batch_fence_reference(batch->out_sync);
                util_dynarray_append(&access->readers,
                                     struct panfrost_batch_fence *,
                                     batch->out_sync);

                if (access->writer)
                        panfrost_batch_add_dep(batch, access->writer);
        }

        access->last_is_write = writes;
}

void
panfrost_batch_add_bo(struct panfrost_batch *batch, struct panfrost_bo *bo,
                      uint32_t flags)
{
        if (!bo)
                return;

        struct hash_entry *entry;
        uint32_t old_flags = 0;

        entry = _mesa_hash_table_search(batch->bos, bo);
        if (!entry) {
                entry = _mesa_hash_table_insert(batch->bos, bo,
                                                (void *)(uintptr_t)flags);
                panfrost_bo_reference(bo);
	} else {
                old_flags = (uintptr_t)entry->data;

                /* All batches have to agree on the shared flag. */
                assert((old_flags & PAN_BO_ACCESS_SHARED) ==
                       (flags & PAN_BO_ACCESS_SHARED));
        }

        assert(entry);

        if (old_flags == flags)
                return;

        flags |= old_flags;
        entry->data = (void *)(uintptr_t)flags;

        /* If this is not a shared BO, we don't really care about dependency
         * tracking.
         */
        if (!(flags & PAN_BO_ACCESS_SHARED))
                return;

        assert(flags & PAN_BO_ACCESS_RW);
        panfrost_batch_update_bo_access(batch, bo, flags & PAN_BO_ACCESS_WRITE,
                        old_flags != 0);
}

static void
panfrost_batch_add_resource_bos(struct panfrost_batch *batch,
                                struct panfrost_resource *rsrc,
                                uint32_t flags)
{
        panfrost_batch_add_bo(batch, rsrc->image.data.bo, flags);

        if (rsrc->image.crc.bo)
                panfrost_batch_add_bo(batch, rsrc->image.crc.bo, flags);

        if (rsrc->separate_stencil)
                panfrost_batch_add_bo(batch, rsrc->separate_stencil->image.data.bo, flags);
}

/* Adds the BO backing surface to a batch if the surface is non-null */

static void
panfrost_batch_add_surface(struct panfrost_batch *batch, struct pipe_surface *surf)
{
        uint32_t flags = PAN_BO_ACCESS_SHARED | PAN_BO_ACCESS_WRITE |
                         PAN_BO_ACCESS_VERTEX_TILER |
                         PAN_BO_ACCESS_FRAGMENT;
        if (surf) {
                struct panfrost_resource *rsrc = pan_resource(surf->texture);
                panfrost_batch_add_resource_bos(batch, rsrc, flags);
        }

}
static void
panfrost_batch_add_fbo_bos(struct panfrost_batch *batch)
{
        for (unsigned i = 0; i < batch->key.nr_cbufs; ++i)
                panfrost_batch_add_surface(batch, batch->key.cbufs[i]);

        panfrost_batch_add_surface(batch, batch->key.zsbuf);
}

struct panfrost_bo *
panfrost_batch_create_bo(struct panfrost_batch *batch, size_t size,
                         uint32_t create_flags, uint32_t access_flags)
{
        struct panfrost_bo *bo;

        bo = panfrost_bo_create(pan_device(batch->ctx->base.screen), size,
                                create_flags);
        panfrost_batch_add_bo(batch, bo, access_flags);

        /* panfrost_batch_add_bo() has retained a reference and
         * panfrost_bo_create() initialize the refcnt to 1, so let's
         * unreference the BO here so it gets released when the batch is
         * destroyed (unless it's retained by someone else in the meantime).
         */
        panfrost_bo_unreference(bo);
        return bo;
}

/* Returns the polygon list's GPU address if available, or otherwise allocates
 * the polygon list.  It's perfectly fast to use allocate/free BO directly,
 * since we'll hit the BO cache and this is one-per-batch anyway. */

static mali_ptr
panfrost_batch_get_polygon_list(struct panfrost_batch *batch)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);

        assert(!pan_is_bifrost(dev));

        if (!batch->tiler_ctx.midgard.polygon_list) {
                bool has_draws = batch->scoreboard.first_tiler != NULL;
                unsigned size =
                        panfrost_tiler_get_polygon_list_size(dev,
                                                             batch->key.width,
                                                             batch->key.height,
                                                             has_draws);
                size = util_next_power_of_two(size);

                /* Create the BO as invisible if we can. In the non-hierarchical tiler case,
                 * we need to write the polygon list manually because there's not WRITE_VALUE
                 * job in the chain (maybe we should add one...). */
                bool init_polygon_list = !has_draws && (dev->quirks & MIDGARD_NO_HIER_TILING);
                batch->tiler_ctx.midgard.polygon_list =
                        panfrost_batch_create_bo(batch, size,
                                                 init_polygon_list ? 0 : PAN_BO_INVISIBLE,
                                                 PAN_BO_ACCESS_PRIVATE |
                                                 PAN_BO_ACCESS_RW |
                                                 PAN_BO_ACCESS_VERTEX_TILER |
                                                 PAN_BO_ACCESS_FRAGMENT);


                if (init_polygon_list) {
                        assert(batch->tiler_ctx.midgard.polygon_list->ptr.cpu);
                        uint32_t *polygon_list_body =
                                batch->tiler_ctx.midgard.polygon_list->ptr.cpu +
                                MALI_MIDGARD_TILER_MINIMUM_HEADER_SIZE;
                         polygon_list_body[0] = 0xa0000000; /* TODO: Just that? */
                }

                batch->tiler_ctx.midgard.disable = !has_draws;
        }

        return batch->tiler_ctx.midgard.polygon_list->ptr.gpu;
}

struct panfrost_bo *
panfrost_batch_get_scratchpad(struct panfrost_batch *batch,
                unsigned size_per_thread,
                unsigned thread_tls_alloc,
                unsigned core_count)
{
        unsigned size = panfrost_get_total_stack_size(size_per_thread,
                        thread_tls_alloc,
                        core_count);

        if (batch->scratchpad) {
                assert(batch->scratchpad->size >= size);
        } else {
                batch->scratchpad = panfrost_batch_create_bo(batch, size,
                                             PAN_BO_INVISIBLE,
                                             PAN_BO_ACCESS_PRIVATE |
                                             PAN_BO_ACCESS_RW |
                                             PAN_BO_ACCESS_VERTEX_TILER |
                                             PAN_BO_ACCESS_FRAGMENT);
        }

        return batch->scratchpad;
}

struct panfrost_bo *
panfrost_batch_get_shared_memory(struct panfrost_batch *batch,
                unsigned size,
                unsigned workgroup_count)
{
        if (batch->shared_memory) {
                assert(batch->shared_memory->size >= size);
        } else {
                batch->shared_memory = panfrost_batch_create_bo(batch, size,
                                             PAN_BO_INVISIBLE,
                                             PAN_BO_ACCESS_PRIVATE |
                                             PAN_BO_ACCESS_RW |
                                             PAN_BO_ACCESS_VERTEX_TILER);
        }

        return batch->shared_memory;
}

mali_ptr
panfrost_batch_get_bifrost_tiler(struct panfrost_batch *batch, unsigned vertex_count)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);
        assert(pan_is_bifrost(dev));

        if (!vertex_count)
                return 0;

        if (batch->tiler_ctx.bifrost)
                return batch->tiler_ctx.bifrost;

        struct panfrost_ptr t =
                panfrost_pool_alloc_desc(&batch->pool, BIFROST_TILER_HEAP);

        pan_emit_bifrost_tiler_heap(dev, t.cpu);

        mali_ptr heap = t.gpu;

        t = panfrost_pool_alloc_desc(&batch->pool, BIFROST_TILER);
        pan_emit_bifrost_tiler(dev, batch->key.width, batch->key.height,
                               util_framebuffer_get_num_samples(&batch->key),
                               heap, t.cpu);

        batch->tiler_ctx.bifrost = t.gpu;
        return batch->tiler_ctx.bifrost;
}

static void
panfrost_batch_to_fb_info(const struct panfrost_batch *batch,
                          struct pan_fb_info *fb,
                          struct pan_image_view *rts,
                          struct pan_image_view *zs,
                          struct pan_image_view *s,
                          bool reserve)
{
        memset(fb, 0, sizeof(*fb));
        memset(rts, 0, sizeof(*rts) * 8);
        memset(zs, 0, sizeof(*zs));
        memset(s, 0, sizeof(*s));

        fb->width = batch->key.width;
        fb->height = batch->key.height;
        fb->extent.minx = batch->minx;
        fb->extent.miny = batch->miny;
        fb->extent.maxx = batch->maxx - 1;
        fb->extent.maxy = batch->maxy - 1;
        fb->nr_samples = util_framebuffer_get_num_samples(&batch->key);
        fb->rt_count = batch->key.nr_cbufs;

        static const unsigned char id_swz[] = {
                PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y, PIPE_SWIZZLE_Z, PIPE_SWIZZLE_W,
        };

        for (unsigned i = 0; i < fb->rt_count; i++) {
                struct pipe_surface *surf = batch->key.cbufs[i];

                if (!surf)
                        continue;

                struct panfrost_resource *prsrc = pan_resource(surf->texture);
                unsigned mask = PIPE_CLEAR_COLOR0 << i;

                if (batch->clear & mask) {
                        fb->rts[i].clear = true;
                        memcpy(fb->rts[i].clear_value, batch->clear_color[i],
                               sizeof((fb->rts[i].clear_value)));
                }

                /* Discard RTs that have no draws or clear. */
                if (!reserve && !((batch->clear | batch->draws) & mask))
                        fb->rts[i].discard = true;

                rts[i].format = surf->format;
                rts[i].dim = MALI_TEXTURE_DIMENSION_2D;
                rts[i].last_level = rts[i].first_level = surf->u.tex.level;
                rts[i].first_layer = surf->u.tex.first_layer;
                rts[i].last_layer = surf->u.tex.last_layer;
                rts[i].image = &prsrc->image;
                memcpy(rts[i].swizzle, id_swz, sizeof(rts[i].swizzle));
                fb->rts[i].state = &prsrc->state;
                fb->rts[i].view = &rts[i];

                /* Preload if the RT is read or updated */
                if (!(batch->clear & mask) &&
                    ((batch->read & mask) ||
                     ((batch->draws & mask) &&
                      fb->rts[i].state->slices[fb->rts[i].view->first_level].data_valid)))
                        fb->rts[i].preload = true;

        }

        const struct pan_image_view *s_view = NULL, *z_view = NULL;
        const struct pan_image_state *s_state = NULL, *z_state = NULL;

        if (batch->key.zsbuf) {
                struct pipe_surface *surf = batch->key.zsbuf;
                struct panfrost_resource *prsrc = pan_resource(surf->texture);

                zs->format = surf->format == PIPE_FORMAT_Z32_FLOAT_S8X24_UINT ?
                             PIPE_FORMAT_Z32_FLOAT : surf->format;
                zs->dim = MALI_TEXTURE_DIMENSION_2D;
                zs->last_level = zs->first_level = surf->u.tex.level;
                zs->first_layer = surf->u.tex.first_layer;
                zs->last_layer = surf->u.tex.last_layer;
                zs->image = &prsrc->image;
                memcpy(zs->swizzle, id_swz, sizeof(zs->swizzle));
                fb->zs.view.zs = zs;
                fb->zs.state.zs = &prsrc->state;
                z_view = zs;
                z_state = &prsrc->state;
                if (util_format_is_depth_and_stencil(zs->format)) {
                        s_view = zs;
                        s_state = &prsrc->state;
                }

                if (prsrc->separate_stencil) {
                        s->format = PIPE_FORMAT_S8_UINT;
                        s->dim = MALI_TEXTURE_DIMENSION_2D;
                        s->last_level = s->first_level = surf->u.tex.level;
                        s->first_layer = surf->u.tex.first_layer;
                        s->last_layer = surf->u.tex.last_layer;
                        s->image = &prsrc->separate_stencil->image;
                        memcpy(s->swizzle, id_swz, sizeof(s->swizzle));
                        fb->zs.view.s = s;
                        fb->zs.state.s = &prsrc->separate_stencil->state;
                        s_view = s;
                        s_state = &prsrc->separate_stencil->state;
                }
        }

        if (batch->clear & PIPE_CLEAR_DEPTH) {
                fb->zs.clear.z = true;
                fb->zs.clear_value.depth = batch->clear_depth;
        }

        if (batch->clear & PIPE_CLEAR_STENCIL) {
                fb->zs.clear.s = true;
                fb->zs.clear_value.stencil = batch->clear_stencil;
        }

        /* Discard if Z/S are not updated */
        if (!reserve && !((batch->draws | batch->clear) & PIPE_CLEAR_DEPTH))
                fb->zs.discard.z = true;

        if (!reserve && !((batch->draws | batch->clear) & PIPE_CLEAR_STENCIL))
                fb->zs.discard.s = true;

        if (!fb->zs.clear.z &&
            ((batch->read & PIPE_CLEAR_DEPTH) ||
             ((batch->draws & PIPE_CLEAR_DEPTH) &&
              z_state->slices[z_view->first_level].data_valid)))
                fb->zs.preload.z = true;

        if (!fb->zs.clear.s &&
            ((batch->read & PIPE_CLEAR_STENCIL) ||
             ((batch->draws & PIPE_CLEAR_STENCIL) &&
              s_state->slices[s_view->first_level].data_valid)))
                fb->zs.preload.s = true;

        /* Preserve both component if we have a combined ZS view and
         * one component needs to be preserved.
         */
        if (s_view == z_view && fb->zs.discard.z != fb->zs.discard.s) {
                bool valid = z_state->slices[z_view->first_level].data_valid;

                fb->zs.discard.z = false;
                fb->zs.discard.s = false;
                fb->zs.preload.z = !fb->zs.clear.z && valid;
                fb->zs.preload.s = !fb->zs.clear.s && valid;
        }
}

static mali_ptr
panfrost_batch_reserve_framebuffer(struct panfrost_batch *batch)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);

        if (batch->framebuffer.gpu)
                return batch->framebuffer.gpu;

        /* If we haven't, reserve space for a framebuffer descriptor */

        struct pan_image_view rts[8];
        struct pan_image_view zs;
        struct pan_image_view s;
        struct pan_fb_info fb;

        panfrost_batch_to_fb_info(batch, &fb, rts, &zs, &s, true);

        unsigned zs_crc_count = pan_fbd_has_zs_crc_ext(dev, &fb) ? 1 : 0;
        unsigned rt_count = MAX2(fb.rt_count, 1);
        batch->framebuffer =
                (dev->quirks & MIDGARD_SFBD) ?
                panfrost_pool_alloc_desc(&batch->pool, SINGLE_TARGET_FRAMEBUFFER) :
                panfrost_pool_alloc_desc_aggregate(&batch->pool,
                                                   PAN_DESC(MULTI_TARGET_FRAMEBUFFER),
                                                   PAN_DESC_ARRAY(zs_crc_count, ZS_CRC_EXTENSION),
                                                   PAN_DESC_ARRAY(rt_count, RENDER_TARGET));

        /* Add the MFBD tag now, other tags will be added when emitting the
         * FB desc.
         */
        if (!(dev->quirks & MIDGARD_SFBD))
                batch->framebuffer.gpu |= MALI_FBD_TAG_IS_MFBD;

        return batch->framebuffer.gpu;
}

mali_ptr
panfrost_batch_reserve_tls(struct panfrost_batch *batch, bool compute)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);

        /* If we haven't, reserve space for the thread storage descriptor */

        if (batch->tls.gpu)
                return batch->tls.gpu;

        if (pan_is_bifrost(dev) || compute) {
                batch->tls = panfrost_pool_alloc_desc(&batch->pool, LOCAL_STORAGE);
        } else {
                /* On Midgard, the FB descriptor contains a thread storage
                 * descriptor, and tiler jobs need more than thread storage
                 * info. Let's point to the FB desc in that case.
                 */
                panfrost_batch_reserve_framebuffer(batch);
                batch->tls = batch->framebuffer;
        }

        return batch->tls.gpu;
}

static void
panfrost_batch_draw_wallpaper(struct panfrost_batch *batch,
                              struct pan_fb_info *fb)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);

        pan_preload_fb(&batch->pool, &batch->scoreboard, fb, batch->tls.gpu,
                       pan_is_bifrost(dev) ? batch->tiler_ctx.bifrost : 0);
}

static void
panfrost_batch_record_bo(struct hash_entry *entry, unsigned *bo_handles, unsigned idx)
{
        struct panfrost_bo *bo = (struct panfrost_bo *)entry->key;
        uint32_t flags = (uintptr_t)entry->data;

        assert(bo->gem_handle > 0);
        bo_handles[idx] = bo->gem_handle;

        /* Update the BO access flags so that panfrost_bo_wait() knows
         * about all pending accesses.
         * We only keep the READ/WRITE info since this is all the BO
         * wait logic cares about.
         * We also preserve existing flags as this batch might not
         * be the first one to access the BO.
         */
        bo->gpu_access |= flags & (PAN_BO_ACCESS_RW);
}

static int
panfrost_batch_submit_ioctl(struct panfrost_batch *batch,
                            mali_ptr first_job_desc,
                            uint32_t reqs,
                            uint32_t in_sync,
                            uint32_t out_sync)
{
        struct panfrost_context *ctx = batch->ctx;
        struct pipe_context *gallium = (struct pipe_context *) ctx;
        struct panfrost_device *dev = pan_device(gallium->screen);
        struct drm_panfrost_submit submit = {0,};
        uint32_t *bo_handles;
        int ret;

        /* If we trace, we always need a syncobj, so make one of our own if we
         * weren't given one to use. Remember that we did so, so we can free it
         * after we're done but preventing double-frees if we were given a
         * syncobj */

        if (!out_sync && dev->debug & (PAN_DBG_TRACE | PAN_DBG_SYNC))
                out_sync = ctx->syncobj;

        submit.out_sync = out_sync;
        submit.jc = first_job_desc;
        submit.requirements = reqs;
        if (in_sync) {
                submit.in_syncs = (u64)(uintptr_t)(&in_sync);
                submit.in_sync_count = 1;
        }

        bo_handles = calloc(panfrost_pool_num_bos(&batch->pool) +
                            panfrost_pool_num_bos(&batch->invisible_pool) +
                            batch->bos->entries + 2,
                            sizeof(*bo_handles));
        assert(bo_handles);

        hash_table_foreach(batch->bos, entry)
                panfrost_batch_record_bo(entry, bo_handles, submit.bo_handle_count++);

        panfrost_pool_get_bo_handles(&batch->pool, bo_handles + submit.bo_handle_count);
        submit.bo_handle_count += panfrost_pool_num_bos(&batch->pool);
        panfrost_pool_get_bo_handles(&batch->invisible_pool, bo_handles + submit.bo_handle_count);
        submit.bo_handle_count += panfrost_pool_num_bos(&batch->invisible_pool);

        /* Add the tiler heap to the list of accessed BOs if the batch has at
         * least one tiler job. Tiler heap is written by tiler jobs and read
         * by fragment jobs (the polygon list is coming from this heap).
         */
        if (batch->scoreboard.first_tiler)
                bo_handles[submit.bo_handle_count++] = dev->tiler_heap->gem_handle;

        /* Always used on Bifrost, occassionally used on Midgard */
        bo_handles[submit.bo_handle_count++] = dev->sample_positions->gem_handle;

        submit.bo_handles = (u64) (uintptr_t) bo_handles;
        if (ctx->is_noop)
                ret = 0;
        else
                ret = drmIoctl(dev->fd, DRM_IOCTL_PANFROST_SUBMIT, &submit);
        free(bo_handles);

        if (ret) {
                if (dev->debug & PAN_DBG_MSGS)
                        fprintf(stderr, "Error submitting: %m\n");

                return errno;
        }

        /* Trace the job if we're doing that */
        if (dev->debug & (PAN_DBG_TRACE | PAN_DBG_SYNC)) {
                /* Wait so we can get errors reported back */
                drmSyncobjWait(dev->fd, &out_sync, 1,
                               INT64_MAX, 0, NULL);

                /* Trace gets priority over sync */
                bool minimal = !(dev->debug & PAN_DBG_TRACE);
                pandecode_jc(submit.jc, pan_is_bifrost(dev), dev->gpu_id, minimal);
        }

        return 0;
}

/* Submit both vertex/tiler and fragment jobs for a batch, possibly with an
 * outsync corresponding to the later of the two (since there will be an
 * implicit dep between them) */

static int
panfrost_batch_submit_jobs(struct panfrost_batch *batch,
                           const struct pan_fb_info *fb,
                           uint32_t in_sync, uint32_t out_sync)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);
        bool has_draws = batch->scoreboard.first_job;
        bool has_tiler = batch->scoreboard.first_tiler;
        bool has_frag = has_tiler || batch->clear;
        int ret = 0;

        /* Take the submit lock to make sure no tiler jobs from other context
         * are inserted between our tiler and fragment jobs, failing to do that
         * might result in tiler heap corruption.
         */
        if (has_tiler)
                pthread_mutex_lock(&dev->submit_lock);

        if (has_draws) {
                ret = panfrost_batch_submit_ioctl(batch, batch->scoreboard.first_job,
                                                  0, in_sync, has_frag ? 0 : out_sync);
                assert(!ret);
        }

        if (has_frag) {
                /* Whether we program the fragment job for draws or not depends
                 * on whether there is any *tiler* activity (so fragment
                 * shaders). If there are draws but entirely RASTERIZER_DISCARD
                 * (say, for transform feedback), we want a fragment job that
                 * *only* clears, since otherwise the tiler structures will be
                 * uninitialized leading to faults (or state leaks) */

                mali_ptr fragjob = panfrost_emit_fragment_job(batch, fb);
                ret = panfrost_batch_submit_ioctl(batch, fragjob,
                                                  PANFROST_JD_REQ_FS, 0,
                                                  out_sync);
                assert(!ret);
        }

        if (has_tiler)
                pthread_mutex_unlock(&dev->submit_lock);

        return ret;
}

static void ATTRIBUTE_NOINLINE
panfrost_batch_submit_nodep(struct panfrost_device *dev,
                            struct panfrost_batch *batch,
                            uint32_t in_sync, uint32_t out_sync)
{
        int ret;

        /* Nothing to do! */
        if (!batch->scoreboard.first_job && !batch->clear)
                goto out;

        if (batch->scoreboard.first_tiler || batch->clear)
                panfrost_batch_reserve_framebuffer(batch);

        struct pan_fb_info fb;
        struct pan_image_view rts[8], zs, s;

        panfrost_batch_to_fb_info(batch, &fb, rts, &zs, &s, false);

        panfrost_batch_reserve_tls(batch, false);
        panfrost_batch_draw_wallpaper(batch, &fb);


        if (!pan_is_bifrost(dev)) {
                mali_ptr polygon_list = panfrost_batch_get_polygon_list(batch);

                panfrost_scoreboard_initialize_tiler(&batch->pool, &batch->scoreboard, polygon_list);
        }

        /* Now that all draws are in, we can finally prepare the
         * FBD for the batch */

        panfrost_emit_tls(batch);

        panfrost_emit_tile_map(batch, &fb);

        if (batch->framebuffer.gpu)
                panfrost_emit_fbd(batch, &fb);

        ret = panfrost_batch_submit_jobs(batch, &fb, in_sync, out_sync);

        if (ret && dev->debug & PAN_DBG_MSGS)
                fprintf(stderr, "panfrost_batch_submit failed: %d\n", ret);

        /* We must reset the damage info of our render targets here even
         * though a damage reset normally happens when the DRI layer swaps
         * buffers. That's because there can be implicit flushes the GL
         * app is not aware of, and those might impact the damage region: if
         * part of the damaged portion is drawn during those implicit flushes,
         * you have to reload those areas before next draws are pushed, and
         * since the driver can't easily know what's been modified by the draws
         * it flushed, the easiest solution is to reload everything.
         */
        for (unsigned i = 0; i < batch->key.nr_cbufs; i++) {
                if (!batch->key.cbufs[i])
                        continue;

                panfrost_resource_set_damage_region(batch->ctx->base.screen,
                                                    batch->key.cbufs[i]->texture,
                                                    0, NULL);
        }

out:
        panfrost_freeze_batch(batch);
        panfrost_free_batch(batch);
}

static void ATTRIBUTE_NOINLINE
panfrost_batch_submit(struct panfrost_batch *batch,
                      uint32_t in_sync, uint32_t out_sync)
{
        assert(batch);
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);

        /* Submit the dependencies first. Don't pass along the out_sync since
         * they are guaranteed to terminate sooner */
        util_dynarray_foreach(&batch->dependencies,
                              struct panfrost_batch_fence *, dep) {
                if ((*dep)->batch)
                        panfrost_batch_submit((*dep)->batch, 0, 0);
        }

        panfrost_batch_submit_nodep(dev, batch, in_sync, out_sync);
}

/* Submit all batches, applying the out_sync to the currently bound batch */

void
panfrost_flush_all_batches(struct panfrost_context *ctx)
{
        struct panfrost_batch *batch = panfrost_get_batch_for_fbo(ctx);
        panfrost_batch_submit(batch, ctx->syncobj, ctx->syncobj);

        hash_table_foreach(ctx->batches, hentry) {
                struct panfrost_batch *batch = hentry->data;
                assert(batch);

                panfrost_batch_submit(batch, ctx->syncobj, ctx->syncobj);
        }

        assert(!ctx->batches->entries);

        /* Collect batch fences before returning */
        panfrost_gc_fences(ctx);
}

bool
panfrost_pending_batches_access_bo(struct panfrost_context *ctx,
                                   const struct panfrost_bo *bo)
{
        struct panfrost_bo_access *access;
        struct hash_entry *hentry;

        hentry = _mesa_hash_table_search(ctx->accessed_bos, bo);
        access = hentry ? hentry->data : NULL;
        if (!access)
                return false;

        if (access->writer && access->writer->batch)
                return true;

        util_dynarray_foreach(&access->readers, struct panfrost_batch_fence *,
                              reader) {
                if (*reader && (*reader)->batch)
                        return true;
        }

        return false;
}

/* We always flush writers. We might also need to flush readers */

void
panfrost_flush_batches_accessing_bo(struct panfrost_context *ctx,
                                    struct panfrost_bo *bo,
                                    bool flush_readers)
{
        struct panfrost_bo_access *access;
        struct hash_entry *hentry;

        hentry = _mesa_hash_table_search(ctx->accessed_bos, bo);
        access = hentry ? hentry->data : NULL;
        if (!access)
                return;

        if (access->writer && access->writer->batch)
                panfrost_batch_submit(access->writer->batch, ctx->syncobj, ctx->syncobj);

        if (!flush_readers)
                return;

        util_dynarray_foreach(&access->readers, struct panfrost_batch_fence *,
                              reader) {
                if (*reader && (*reader)->batch)
                        panfrost_batch_submit((*reader)->batch, ctx->syncobj, ctx->syncobj);
        }
}

void
panfrost_batch_set_requirements(struct panfrost_batch *batch)
{
        struct panfrost_context *ctx = batch->ctx;

        if (ctx->depth_stencil && ctx->depth_stencil->base.depth_writemask)
                batch->draws |= PIPE_CLEAR_DEPTH;

        if (ctx->depth_stencil && ctx->depth_stencil->base.stencil[0].enabled)
                batch->draws |= PIPE_CLEAR_STENCIL;
}

void
panfrost_batch_adjust_stack_size(struct panfrost_batch *batch)
{
        struct panfrost_context *ctx = batch->ctx;

        for (unsigned i = 0; i < PIPE_SHADER_TYPES; ++i) {
                struct panfrost_shader_state *ss;

                ss = panfrost_get_shader_state(ctx, i);
                if (!ss)
                        continue;

                batch->stack_size = MAX2(batch->stack_size, ss->info.tls_size);
        }
}

/* Helper to smear a 32-bit color across 128-bit components */

static void
pan_pack_color_32(uint32_t *packed, uint32_t v)
{
        for (unsigned i = 0; i < 4; ++i)
                packed[i] = v;
}

static void
pan_pack_color_64(uint32_t *packed, uint32_t lo, uint32_t hi)
{
        for (unsigned i = 0; i < 4; i += 2) {
                packed[i + 0] = lo;
                packed[i + 1] = hi;
        }
}

static void
pan_pack_color(uint32_t *packed, const union pipe_color_union *color, enum pipe_format format)
{
        /* Alpha magicked to 1.0 if there is no alpha */

        bool has_alpha = util_format_has_alpha(format);
        float clear_alpha = has_alpha ? color->f[3] : 1.0f;

        /* Packed color depends on the framebuffer format */

        const struct util_format_description *desc =
                util_format_description(format);

        if (util_format_is_rgba8_variant(desc) && desc->colorspace != UTIL_FORMAT_COLORSPACE_SRGB) {
                pan_pack_color_32(packed,
                                  ((uint32_t) float_to_ubyte(clear_alpha) << 24) |
                                  ((uint32_t) float_to_ubyte(color->f[2]) << 16) |
                                  ((uint32_t) float_to_ubyte(color->f[1]) <<  8) |
                                  ((uint32_t) float_to_ubyte(color->f[0]) <<  0));
        } else if (format == PIPE_FORMAT_B5G6R5_UNORM) {
                /* First, we convert the components to R5, G6, B5 separately */
                unsigned r5 = _mesa_roundevenf(SATURATE(color->f[0]) * 31.0);
                unsigned g6 = _mesa_roundevenf(SATURATE(color->f[1]) * 63.0);
                unsigned b5 = _mesa_roundevenf(SATURATE(color->f[2]) * 31.0);

                /* Then we pack into a sparse u32. TODO: Why these shifts? */
                pan_pack_color_32(packed, (b5 << 25) | (g6 << 14) | (r5 << 5));
        } else if (format == PIPE_FORMAT_B4G4R4A4_UNORM) {
                /* Convert to 4-bits */
                unsigned r4 = _mesa_roundevenf(SATURATE(color->f[0]) * 15.0);
                unsigned g4 = _mesa_roundevenf(SATURATE(color->f[1]) * 15.0);
                unsigned b4 = _mesa_roundevenf(SATURATE(color->f[2]) * 15.0);
                unsigned a4 = _mesa_roundevenf(SATURATE(clear_alpha) * 15.0);

                /* Pack on *byte* intervals */
                pan_pack_color_32(packed, (a4 << 28) | (b4 << 20) | (g4 << 12) | (r4 << 4));
        } else if (format == PIPE_FORMAT_B5G5R5A1_UNORM) {
                /* Scale as expected but shift oddly */
                unsigned r5 = _mesa_roundevenf(SATURATE(color->f[0]) * 31.0);
                unsigned g5 = _mesa_roundevenf(SATURATE(color->f[1]) * 31.0);
                unsigned b5 = _mesa_roundevenf(SATURATE(color->f[2]) * 31.0);
                unsigned a1 = _mesa_roundevenf(SATURATE(clear_alpha) * 1.0);

                pan_pack_color_32(packed, (a1 << 31) | (b5 << 25) | (g5 << 15) | (r5 << 5));
        } else {
                /* Otherwise, it's generic subject to replication */

                union util_color out = { 0 };
                unsigned size = util_format_get_blocksize(format);

                util_pack_color(color->f, format, &out);

                if (size == 1) {
                        unsigned b = out.ui[0];
                        unsigned s = b | (b << 8);
                        pan_pack_color_32(packed, s | (s << 16));
                } else if (size == 2)
                        pan_pack_color_32(packed, out.ui[0] | (out.ui[0] << 16));
                else if (size == 3 || size == 4)
                        pan_pack_color_32(packed, out.ui[0]);
                else if (size == 6 || size == 8)
                        pan_pack_color_64(packed, out.ui[0], out.ui[1]);
                else if (size == 12 || size == 16)
                        memcpy(packed, out.ui, 16);
                else
                        unreachable("Unknown generic format size packing clear colour");
        }
}

void
panfrost_batch_clear(struct panfrost_batch *batch,
                     unsigned buffers,
                     const union pipe_color_union *color,
                     double depth, unsigned stencil)
{
        struct panfrost_context *ctx = batch->ctx;

        if (buffers & PIPE_CLEAR_COLOR) {
                for (unsigned i = 0; i < PIPE_MAX_COLOR_BUFS; ++i) {
                        if (!(buffers & (PIPE_CLEAR_COLOR0 << i)))
                                continue;

                        enum pipe_format format = ctx->pipe_framebuffer.cbufs[i]->format;
                        pan_pack_color(batch->clear_color[i], color, format);
                }
        }

        if (buffers & PIPE_CLEAR_DEPTH) {
                batch->clear_depth = depth;
        }

        if (buffers & PIPE_CLEAR_STENCIL) {
                batch->clear_stencil = stencil;
        }

        batch->clear |= buffers;

        /* Clearing affects the entire framebuffer (by definition -- this is
         * the Gallium clear callback, which clears the whole framebuffer. If
         * the scissor test were enabled from the GL side, the gallium frontend
         * would emit a quad instead and we wouldn't go down this code path) */

        panfrost_batch_union_scissor(batch, 0, 0,
                                     ctx->pipe_framebuffer.width,
                                     ctx->pipe_framebuffer.height);
}

static bool
panfrost_batch_compare(const void *a, const void *b)
{
        return util_framebuffer_state_equal(a, b);
}

static uint32_t
panfrost_batch_hash(const void *key)
{
        return _mesa_hash_data(key, sizeof(struct pipe_framebuffer_state));
}

/* Given a new bounding rectangle (scissor), let the job cover the union of the
 * new and old bounding rectangles */

void
panfrost_batch_union_scissor(struct panfrost_batch *batch,
                             unsigned minx, unsigned miny,
                             unsigned maxx, unsigned maxy)
{
        batch->minx = MIN2(batch->minx, minx);
        batch->miny = MIN2(batch->miny, miny);
        batch->maxx = MAX2(batch->maxx, maxx);
        batch->maxy = MAX2(batch->maxy, maxy);
}

void
panfrost_batch_intersection_scissor(struct panfrost_batch *batch,
                                  unsigned minx, unsigned miny,
                                  unsigned maxx, unsigned maxy)
{
        batch->minx = MAX2(batch->minx, minx);
        batch->miny = MAX2(batch->miny, miny);
        batch->maxx = MIN2(batch->maxx, maxx);
        batch->maxy = MIN2(batch->maxy, maxy);
}

void
panfrost_batch_init(struct panfrost_context *ctx)
{
        ctx->batches = _mesa_hash_table_create(ctx,
                                               panfrost_batch_hash,
                                               panfrost_batch_compare);
        ctx->accessed_bos = _mesa_hash_table_create(ctx, _mesa_hash_pointer,
                                                    _mesa_key_pointer_equal);
}
