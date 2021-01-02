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
#include "pan_context.h"
#include "util/hash_table.h"
#include "util/ralloc.h"
#include "util/format/u_format.h"
#include "util/u_pack_color.h"
#include "util/rounding.h"
#include "pan_util.h"
#include "pan_blending.h"
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
        if (!batch->scoreboard.first_job)
                return batch;

        /* Otherwise, we need to freeze the existing one and instantiate a new
         * one.
         */
        panfrost_freeze_batch(batch);
        return panfrost_get_batch(ctx, &ctx->pipe_framebuffer);
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
        panfrost_batch_add_bo(batch, rsrc->bo, flags);

        for (unsigned i = 0; i < MAX_MIP_LEVELS; i++)
                if (rsrc->slices[i].checksum_bo)
                        panfrost_batch_add_bo(batch, rsrc->slices[i].checksum_bo, flags);

        if (rsrc->separate_stencil)
                panfrost_batch_add_bo(batch, rsrc->separate_stencil->bo, flags);
}

static void
panfrost_batch_add_fbo_bos(struct panfrost_batch *batch)
{
        uint32_t flags = PAN_BO_ACCESS_SHARED | PAN_BO_ACCESS_WRITE |
                         PAN_BO_ACCESS_VERTEX_TILER |
                         PAN_BO_ACCESS_FRAGMENT;

        for (unsigned i = 0; i < batch->key.nr_cbufs; ++i) {
                struct panfrost_resource *rsrc = pan_resource(batch->key.cbufs[i]->texture);
                panfrost_batch_add_resource_bos(batch, rsrc, flags);
        }

        if (batch->key.zsbuf) {
                struct panfrost_resource *rsrc = pan_resource(batch->key.zsbuf->texture);
                panfrost_batch_add_resource_bos(batch, rsrc, flags);
        }
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

mali_ptr
panfrost_batch_get_polygon_list(struct panfrost_batch *batch, unsigned size)
{
        if (batch->polygon_list) {
                assert(batch->polygon_list->size >= size);
        } else {
                /* Create the BO as invisible, as there's no reason to map */
                size = util_next_power_of_two(size);

                batch->polygon_list = panfrost_batch_create_bo(batch, size,
                                                               PAN_BO_INVISIBLE,
                                                               PAN_BO_ACCESS_PRIVATE |
                                                               PAN_BO_ACCESS_RW |
                                                               PAN_BO_ACCESS_VERTEX_TILER |
                                                               PAN_BO_ACCESS_FRAGMENT);
        }

        return batch->polygon_list->ptr.gpu;
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
        if (!vertex_count)
                return 0;

        if (batch->tiler_meta)
                return batch->tiler_meta;

        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);
        struct panfrost_ptr t =
                panfrost_pool_alloc_aligned(&batch->pool, MALI_BIFROST_TILER_HEAP_LENGTH, 64);

        pan_pack(t.cpu, BIFROST_TILER_HEAP, heap) {
                heap.size = dev->tiler_heap->size;
                heap.base = dev->tiler_heap->ptr.gpu;
                heap.bottom = dev->tiler_heap->ptr.gpu;
                heap.top = dev->tiler_heap->ptr.gpu + dev->tiler_heap->size;
        }

        mali_ptr heap = t.gpu;

        t = panfrost_pool_alloc_aligned(&batch->pool, MALI_BIFROST_TILER_LENGTH, 64);
        pan_pack(t.cpu, BIFROST_TILER, tiler) {
                tiler.hierarchy_mask = 0x28;
                tiler.fb_width = batch->key.width;
                tiler.fb_height = batch->key.height;
                tiler.heap = heap;
        }

        batch->tiler_meta = t.gpu;
        return batch->tiler_meta;
}

struct panfrost_bo *
panfrost_batch_get_tiler_dummy(struct panfrost_batch *batch)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);

        uint32_t create_flags = 0;

        if (batch->tiler_dummy)
                return batch->tiler_dummy;

        if (!(dev->quirks & MIDGARD_NO_HIER_TILING))
                create_flags = PAN_BO_INVISIBLE;

        batch->tiler_dummy = panfrost_batch_create_bo(batch, 4096,
                                                      create_flags,
                                                      PAN_BO_ACCESS_PRIVATE |
                                                      PAN_BO_ACCESS_RW |
                                                      PAN_BO_ACCESS_VERTEX_TILER |
                                                      PAN_BO_ACCESS_FRAGMENT);
        assert(batch->tiler_dummy);
        return batch->tiler_dummy;
}

mali_ptr
panfrost_batch_reserve_framebuffer(struct panfrost_batch *batch)
{
        struct panfrost_device *dev = pan_device(batch->ctx->base.screen);

        /* If we haven't, reserve space for the thread storage descriptor (or a
         * full framebuffer descriptor on Midgard) */

        if (!batch->framebuffer.gpu) {
                unsigned size = (dev->quirks & IS_BIFROST) ?
                        MALI_LOCAL_STORAGE_LENGTH :
                        (dev->quirks & MIDGARD_SFBD) ?
                        MALI_SINGLE_TARGET_FRAMEBUFFER_LENGTH :
                        MALI_MULTI_TARGET_FRAMEBUFFER_LENGTH;

                batch->framebuffer = panfrost_pool_alloc_aligned(&batch->pool, size, 64);

                /* Tag the pointer */
                if (!(dev->quirks & (MIDGARD_SFBD | IS_BIFROST)))
                        batch->framebuffer.gpu |= MALI_FBD_TAG_IS_MFBD;
        }

        return batch->framebuffer.gpu;
}

static void
panfrost_load_surface(struct panfrost_batch *batch, struct pipe_surface *surf, unsigned loc)
{
        if (!surf)
                return;

        struct panfrost_resource *rsrc = pan_resource(surf->texture);
        unsigned level = surf->u.tex.level;

        if (!rsrc->slices[level].initialized)
                return;

        if (!rsrc->damage.inverted_len)
                return;

        /* Clamp the rendering area to the damage extent. The
         * KHR_partial_update() spec states that trying to render outside of
         * the damage region is "undefined behavior", so we should be safe.
         */
        unsigned damage_width = (rsrc->damage.extent.maxx - rsrc->damage.extent.minx);
        unsigned damage_height = (rsrc->damage.extent.maxy - rsrc->damage.extent.miny);

        if (damage_width && damage_height) {
                panfrost_batch_intersection_scissor(batch,
                                                    rsrc->damage.extent.minx,
                                                    rsrc->damage.extent.miny,
                                                    rsrc->damage.extent.maxx,
                                                    rsrc->damage.extent.maxy);
        }

        enum pipe_format format = rsrc->base.format;

        if (loc == FRAG_RESULT_DEPTH) {
                if (!util_format_has_depth(util_format_description(format)))
                        return;

                format = util_format_get_depth_only(format);
        } else if (loc == FRAG_RESULT_STENCIL) {
                if (!util_format_has_stencil(util_format_description(format)))
                        return;

                if (rsrc->separate_stencil) {
                        rsrc = rsrc->separate_stencil;
                        format = rsrc->base.format;
                }

                format = util_format_stencil_only(format);
        }

        enum mali_texture_dimension dim =
                panfrost_translate_texture_dimension(rsrc->base.target);

        struct pan_image img = {
                .width0 = rsrc->base.width0,
                .height0 = rsrc->base.height0,
                .depth0 = rsrc->base.depth0,
                .format = format,
                .dim = dim,
                .modifier = rsrc->modifier,
                .array_size = rsrc->base.array_size,
                .first_level = level,
                .last_level = level,
                .first_layer = surf->u.tex.first_layer,
                .last_layer = surf->u.tex.last_layer,
                .nr_samples = rsrc->base.nr_samples,
                .cubemap_stride = rsrc->cubemap_stride,
                .bo = rsrc->bo,
                .slices = rsrc->slices
        };

        mali_ptr blend_shader = 0;

        if (loc >= FRAG_RESULT_DATA0 && !panfrost_can_fixed_blend(rsrc->base.format)) {
                struct panfrost_blend_shader *b =
                        panfrost_get_blend_shader(batch->ctx, batch->ctx->blit_blend,
                                                  rsrc->base.format,
                                                  rsrc->base.nr_samples,
                                                  loc - FRAG_RESULT_DATA0,
                                                  NULL);

                struct panfrost_bo *bo = panfrost_batch_create_bo(batch, b->size,
                   PAN_BO_EXECUTE,
                   PAN_BO_ACCESS_PRIVATE |
                   PAN_BO_ACCESS_READ |
                   PAN_BO_ACCESS_FRAGMENT);

                memcpy(bo->ptr.cpu, b->buffer, b->size);
                assert(b->work_count <= 4);

                blend_shader = bo->ptr.gpu | b->first_tag;
        }

        struct panfrost_ptr transfer = panfrost_pool_alloc_aligned(&batch->pool,
                        4 * 4 * 6 * rsrc->damage.inverted_len, 64);

        for (unsigned i = 0; i < rsrc->damage.inverted_len; ++i) {
                float *o = (float *) (transfer.cpu + (4 * 4 * 6 * i));
                struct pan_rect r = rsrc->damage.inverted_rects[i];

                float rect[] = {
                        r.minx, rsrc->base.height0 - r.miny, 0.0, 1.0,
                        r.maxx, rsrc->base.height0 - r.miny, 0.0, 1.0,
                        r.minx, rsrc->base.height0 - r.maxy, 0.0, 1.0,

                        r.maxx, rsrc->base.height0 - r.miny, 0.0, 1.0,
                        r.minx, rsrc->base.height0 - r.maxy, 0.0, 1.0,
                        r.maxx, rsrc->base.height0 - r.maxy, 0.0, 1.0,
                };

                assert(sizeof(rect) == 4 * 4 * 6);
                memcpy(o, rect, sizeof(rect));
        }

        unsigned vertex_count = rsrc->damage.inverted_len * 6;
        if (batch->pool.dev->quirks & IS_BIFROST) {
                mali_ptr tiler =
                        panfrost_batch_get_bifrost_tiler(batch, vertex_count);
                panfrost_load_bifrost(&batch->pool, &batch->scoreboard,
                                      blend_shader,
                                      batch->framebuffer.gpu,
                                      tiler,
                                      transfer.gpu, vertex_count,
                                      &img, loc);
        } else {
                panfrost_load_midg(&batch->pool, &batch->scoreboard,
                                   blend_shader,
                                   batch->framebuffer.gpu,
                                   transfer.gpu, vertex_count,
                                   &img, loc);
        }

        panfrost_batch_add_bo(batch, batch->pool.dev->blit_shaders.bo,
                        PAN_BO_ACCESS_SHARED | PAN_BO_ACCESS_READ | PAN_BO_ACCESS_FRAGMENT);
}

static void
panfrost_batch_draw_wallpaper(struct panfrost_batch *batch)
{
        panfrost_batch_reserve_framebuffer(batch);

        /* Assume combined. If either depth or stencil is written, they will
         * both be written so we need to be careful for reloading */

        unsigned reload = batch->draws | batch->read;

        if (reload & PIPE_CLEAR_DEPTHSTENCIL)
                reload |= PIPE_CLEAR_DEPTHSTENCIL;

        /* Mask of buffers which need reload since they are not cleared and
         * they are drawn. (If they are cleared, reload is useless; if they are
         * not drawn or read and also not cleared, we can generally omit the
         * attachment at the framebuffer descriptor level */

        reload &= ~batch->clear;

        for (unsigned i = 0; i < batch->key.nr_cbufs; ++i) {
                if (reload & (PIPE_CLEAR_COLOR0 << i)) 
                        panfrost_load_surface(batch, batch->key.cbufs[i], FRAG_RESULT_DATA0 + i);
        }

        if (reload & PIPE_CLEAR_DEPTH)
                panfrost_load_surface(batch, batch->key.zsbuf, FRAG_RESULT_DEPTH);

        if (reload & PIPE_CLEAR_STENCIL)
                panfrost_load_surface(batch, batch->key.zsbuf, FRAG_RESULT_STENCIL);
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
                            batch->bos->entries + 1,
                            sizeof(*bo_handles));
        assert(bo_handles);

        hash_table_foreach(batch->bos, entry)
                panfrost_batch_record_bo(entry, bo_handles, submit.bo_handle_count++);

        panfrost_pool_get_bo_handles(&batch->pool, bo_handles + submit.bo_handle_count);
        submit.bo_handle_count += panfrost_pool_num_bos(&batch->pool);
        panfrost_pool_get_bo_handles(&batch->invisible_pool, bo_handles + submit.bo_handle_count);
        submit.bo_handle_count += panfrost_pool_num_bos(&batch->invisible_pool);

        /* Used by all tiler jobs (XXX: skip for compute-only) */
        if (!(reqs & PANFROST_JD_REQ_FS))
                bo_handles[submit.bo_handle_count++] = dev->tiler_heap->gem_handle;

        submit.bo_handles = (u64) (uintptr_t) bo_handles;
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
                pandecode_jc(submit.jc, dev->quirks & IS_BIFROST, dev->gpu_id, minimal);
        }

        return 0;
}

/* Submit both vertex/tiler and fragment jobs for a batch, possibly with an
 * outsync corresponding to the later of the two (since there will be an
 * implicit dep between them) */

static int
panfrost_batch_submit_jobs(struct panfrost_batch *batch, uint32_t in_sync, uint32_t out_sync)
{
        bool has_draws = batch->scoreboard.first_job;
        bool has_frag = batch->scoreboard.tiler_dep || batch->clear;
        int ret = 0;

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

                mali_ptr fragjob = panfrost_fragment_job(batch,
                                batch->scoreboard.tiler_dep != 0);
                ret = panfrost_batch_submit_ioctl(batch, fragjob,
                                                  PANFROST_JD_REQ_FS, 0,
                                                  out_sync);
                assert(!ret);
        }

        return ret;
}

static void
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

        int ret;

        /* Nothing to do! */
        if (!batch->scoreboard.first_job && !batch->clear)
                goto out;

        panfrost_batch_draw_wallpaper(batch);

        /* Now that all draws are in, we can finally prepare the
         * FBD for the batch */

        if (batch->framebuffer.gpu && batch->scoreboard.first_job) {
                struct panfrost_context *ctx = batch->ctx;
                struct pipe_context *gallium = (struct pipe_context *) ctx;
                struct panfrost_device *dev = pan_device(gallium->screen);

                if (dev->quirks & MIDGARD_SFBD)
                        panfrost_attach_sfbd(batch, ~0);
                else
                        panfrost_attach_mfbd(batch, ~0);
        }

        mali_ptr polygon_list = panfrost_batch_get_polygon_list(batch,
                MALI_MIDGARD_TILER_MINIMUM_HEADER_SIZE);

        panfrost_scoreboard_initialize_tiler(&batch->pool, &batch->scoreboard, polygon_list);

        ret = panfrost_batch_submit_jobs(batch, in_sync, out_sync);

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

                panfrost_resource_set_damage_region(NULL,
                                batch->key.cbufs[i]->texture, 0, NULL);
        }

out:
        panfrost_freeze_batch(batch);
        panfrost_free_batch(batch);
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

        if (ctx->rasterizer->base.multisample)
                batch->requirements |= PAN_REQ_MSAA;

        if (ctx->depth_stencil && ctx->depth_stencil->base.depth_writemask) {
                batch->requirements |= PAN_REQ_DEPTH_WRITE;
                batch->draws |= PIPE_CLEAR_DEPTH;
        }

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

                batch->stack_size = MAX2(batch->stack_size, ss->stack_size);
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
                else if (size == 6)
                        pan_pack_color_64(packed, out.ui[0], out.ui[1] | (out.ui[1] << 16)); /* RGB16F -- RGBB */
                else if (size == 8)
                        pan_pack_color_64(packed, out.ui[0], out.ui[1]);
                else if (size == 16)
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

/* Are we currently rendering to the dev (rather than an FBO)? */

bool
panfrost_batch_is_scanout(struct panfrost_batch *batch)
{
        /* If there is no color buffer, it's an FBO */
        if (batch->key.nr_cbufs != 1)
                return false;

        /* If we're too early that no framebuffer was sent, it's scanout */
        if (!batch->key.cbufs[0])
                return true;

        return batch->key.cbufs[0]->texture->bind & PIPE_BIND_DISPLAY_TARGET ||
               batch->key.cbufs[0]->texture->bind & PIPE_BIND_SCANOUT ||
               batch->key.cbufs[0]->texture->bind & PIPE_BIND_SHARED;
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
