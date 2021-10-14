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

#include "util/hash_table.h"
#include "util/list.h"
#include "util/set.h"
#include "util/u_string.h"

#include "freedreno_batch.h"
#include "freedreno_context.h"
#include "freedreno_fence.h"
#include "freedreno_query_hw.h"
#include "freedreno_resource.h"

static struct fd_ringbuffer *
alloc_ring(struct fd_batch *batch, unsigned sz, enum fd_ringbuffer_flags flags)
{
   struct fd_context *ctx = batch->ctx;

   /* if kernel is too old to support unlimited # of cmd buffers, we
    * have no option but to allocate large worst-case sizes so that
    * we don't need to grow the ringbuffer.  Performance is likely to
    * suffer, but there is no good alternative.
    *
    * Otherwise if supported, allocate a growable ring with initial
    * size of zero.
    */
   if ((fd_device_version(ctx->screen->dev) >= FD_VERSION_UNLIMITED_CMDS) &&
       !FD_DBG(NOGROW)) {
      flags |= FD_RINGBUFFER_GROWABLE;
      sz = 0;
   }

   return fd_submit_new_ringbuffer(batch->submit, sz, flags);
}

static void
batch_init(struct fd_batch *batch)
{
   struct fd_context *ctx = batch->ctx;

   batch->submit = fd_submit_new(ctx->pipe);
   if (batch->nondraw) {
      batch->gmem = alloc_ring(batch, 0x1000, FD_RINGBUFFER_PRIMARY);
      batch->draw = alloc_ring(batch, 0x100000, 0);
   } else {
      batch->gmem = alloc_ring(batch, 0x100000, FD_RINGBUFFER_PRIMARY);
      batch->draw = alloc_ring(batch, 0x100000, 0);

      /* a6xx+ re-uses draw rb for both draw and binning pass: */
      if (ctx->screen->gen < 6) {
         batch->binning = alloc_ring(batch, 0x100000, 0);
      }
   }

   batch->in_fence_fd = -1;
   batch->fence = NULL;

   /* Work around problems on earlier gens with submit merging, etc,
    * by always creating a fence to request that the submit is flushed
    * immediately:
    */
   if (ctx->screen->gen < 6)
      batch->fence = fd_fence_create(batch);

   batch->cleared = 0;
   batch->fast_cleared = 0;
   batch->invalidated = 0;
   batch->restore = batch->resolve = 0;
   batch->needs_flush = false;
   batch->flushed = false;
   batch->gmem_reason = 0;
   batch->num_draws = 0;
   batch->num_vertices = 0;
   batch->num_bins_per_pipe = 0;
   batch->prim_strm_bits = 0;
   batch->draw_strm_bits = 0;

   fd_reset_wfi(batch);

   util_dynarray_init(&batch->draw_patches, NULL);
   util_dynarray_init(&batch->fb_read_patches, NULL);

   if (is_a2xx(ctx->screen)) {
      util_dynarray_init(&batch->shader_patches, NULL);
      util_dynarray_init(&batch->gmem_patches, NULL);
   }

   if (is_a3xx(ctx->screen))
      util_dynarray_init(&batch->rbrc_patches, NULL);

   assert(batch->resources->entries == 0);

   util_dynarray_init(&batch->samples, NULL);

   u_trace_init(&batch->trace, &ctx->trace_context);
   batch->last_timestamp_cmd = NULL;
}

struct fd_batch *
fd_batch_create(struct fd_context *ctx, bool nondraw)
{
   struct fd_batch *batch = CALLOC_STRUCT(fd_batch);

   if (!batch)
      return NULL;

   DBG("%p", batch);

   pipe_reference_init(&batch->reference, 1);
   batch->ctx = ctx;
   batch->nondraw = nondraw;

   simple_mtx_init(&batch->submit_lock, mtx_plain);

   batch->resources =
      _mesa_set_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);

   batch_init(batch);

   return batch;
}

static void
cleanup_submit(struct fd_batch *batch)
{
   if (!batch->submit)
      return;

   fd_ringbuffer_del(batch->draw);
   fd_ringbuffer_del(batch->gmem);

   if (batch->binning) {
      fd_ringbuffer_del(batch->binning);
      batch->binning = NULL;
   }

   if (batch->prologue) {
      fd_ringbuffer_del(batch->prologue);
      batch->prologue = NULL;
   }

   if (batch->epilogue) {
      fd_ringbuffer_del(batch->epilogue);
      batch->epilogue = NULL;
   }

   if (batch->tile_setup) {
      fd_ringbuffer_del(batch->tile_setup);
      batch->tile_setup = NULL;
   }

   if (batch->tile_fini) {
      fd_ringbuffer_del(batch->tile_fini);
      batch->tile_fini = NULL;
   }

   if (batch->tessellation) {
      fd_bo_del(batch->tessfactor_bo);
      fd_bo_del(batch->tessparam_bo);
      fd_ringbuffer_del(batch->tess_addrs_constobj);
   }

   fd_submit_del(batch->submit);
   batch->submit = NULL;
}

static void
batch_fini(struct fd_batch *batch)
{
   DBG("%p", batch);

   pipe_resource_reference(&batch->query_buf, NULL);

   if (batch->in_fence_fd != -1)
      close(batch->in_fence_fd);

   /* in case batch wasn't flushed but fence was created: */
   if (batch->fence)
      fd_fence_set_batch(batch->fence, NULL);

   fd_fence_ref(&batch->fence, NULL);

   cleanup_submit(batch);

   util_dynarray_fini(&batch->draw_patches);
   util_dynarray_fini(&batch->fb_read_patches);

   if (is_a2xx(batch->ctx->screen)) {
      util_dynarray_fini(&batch->shader_patches);
      util_dynarray_fini(&batch->gmem_patches);
   }

   if (is_a3xx(batch->ctx->screen))
      util_dynarray_fini(&batch->rbrc_patches);

   while (batch->samples.size > 0) {
      struct fd_hw_sample *samp =
         util_dynarray_pop(&batch->samples, struct fd_hw_sample *);
      fd_hw_sample_reference(batch->ctx, &samp, NULL);
   }
   util_dynarray_fini(&batch->samples);

   u_trace_fini(&batch->trace);
}

static void
batch_flush_dependencies(struct fd_batch *batch) assert_dt
{
   struct fd_batch_cache *cache = &batch->ctx->screen->batch_cache;
   struct fd_batch *dep;

   foreach_batch (dep, cache, batch->dependents_mask) {
      fd_batch_flush(dep);
      fd_batch_reference(&dep, NULL);
   }

   batch->dependents_mask = 0;
}

static void
batch_reset_dependencies(struct fd_batch *batch)
{
   struct fd_batch_cache *cache = &batch->ctx->screen->batch_cache;
   struct fd_batch *dep;

   foreach_batch (dep, cache, batch->dependents_mask) {
      fd_batch_reference(&dep, NULL);
   }

   batch->dependents_mask = 0;
}

static void
batch_reset_resources(struct fd_batch *batch)
{
   fd_screen_assert_locked(batch->ctx->screen);

   set_foreach (batch->resources, entry) {
      struct fd_resource *rsc = (struct fd_resource *)entry->key;
      _mesa_set_remove(batch->resources, entry);
      debug_assert(rsc->track->batch_mask & (1 << batch->idx));
      rsc->track->batch_mask &= ~(1 << batch->idx);
      if (rsc->track->write_batch == batch)
         fd_batch_reference_locked(&rsc->track->write_batch, NULL);
   }
}

static void
batch_reset(struct fd_batch *batch) assert_dt
{
   DBG("%p", batch);

   batch_reset_dependencies(batch);

   fd_screen_lock(batch->ctx->screen);
   batch_reset_resources(batch);
   fd_screen_unlock(batch->ctx->screen);

   batch_fini(batch);
   batch_init(batch);
}

void
fd_batch_reset(struct fd_batch *batch)
{
   if (batch->needs_flush)
      batch_reset(batch);
}

void
__fd_batch_destroy(struct fd_batch *batch)
{
   struct fd_context *ctx = batch->ctx;

   DBG("%p", batch);

   fd_screen_assert_locked(batch->ctx->screen);

   fd_bc_invalidate_batch(batch, true);

   batch_reset_resources(batch);
   debug_assert(batch->resources->entries == 0);
   _mesa_set_destroy(batch->resources, NULL);

   fd_screen_unlock(ctx->screen);
   batch_reset_dependencies(batch);
   debug_assert(batch->dependents_mask == 0);

   util_copy_framebuffer_state(&batch->framebuffer, NULL);
   batch_fini(batch);

   simple_mtx_destroy(&batch->submit_lock);

   free(batch->key);
   free(batch);
   fd_screen_lock(ctx->screen);
}

void
__fd_batch_describe(char *buf, const struct fd_batch *batch)
{
   sprintf(buf, "fd_batch<%u>", batch->seqno);
}

/* Get per-batch prologue */
struct fd_ringbuffer *
fd_batch_get_prologue(struct fd_batch *batch)
{
   if (!batch->prologue)
      batch->prologue = alloc_ring(batch, 0x1000, 0);
   return batch->prologue;
}

/* Only called from fd_batch_flush() */
static void
batch_flush(struct fd_batch *batch) assert_dt
{
   DBG("%p: needs_flush=%d", batch, batch->needs_flush);

   if (!fd_batch_lock_submit(batch))
      return;

   batch->needs_flush = false;

   /* close out the draw cmds by making sure any active queries are
    * paused:
    */
   fd_batch_finish_queries(batch);

   batch_flush_dependencies(batch);

   fd_screen_lock(batch->ctx->screen);
   batch_reset_resources(batch);
   /* NOTE: remove=false removes the batch from the hashtable, so future
    * lookups won't cache-hit a flushed batch, but leaves the weak reference
    * to the batch to avoid having multiple batches with same batch->idx, as
    * that causes all sorts of hilarity.
    */
   fd_bc_invalidate_batch(batch, false);
   batch->flushed = true;

   if (batch == batch->ctx->batch)
      fd_batch_reference_locked(&batch->ctx->batch, NULL);

   fd_screen_unlock(batch->ctx->screen);

   if (batch->fence)
      fd_fence_ref(&batch->ctx->last_fence, batch->fence);

   fd_gmem_render_tiles(batch);

   debug_assert(batch->reference.count > 0);

   cleanup_submit(batch);
   fd_batch_unlock_submit(batch);
}

/* NOTE: could drop the last ref to batch
 */
void
fd_batch_flush(struct fd_batch *batch)
{
   struct fd_batch *tmp = NULL;

   /* NOTE: we need to hold an extra ref across the body of flush,
    * since the last ref to this batch could be dropped when cleaning
    * up used_resources
    */
   fd_batch_reference(&tmp, batch);
   batch_flush(tmp);
   fd_batch_reference(&tmp, NULL);
}

/* find a batches dependents mask, including recursive dependencies: */
static uint32_t
recursive_dependents_mask(struct fd_batch *batch)
{
   struct fd_batch_cache *cache = &batch->ctx->screen->batch_cache;
   struct fd_batch *dep;
   uint32_t dependents_mask = batch->dependents_mask;

   foreach_batch (dep, cache, batch->dependents_mask)
      dependents_mask |= recursive_dependents_mask(dep);

   return dependents_mask;
}

void
fd_batch_add_dep(struct fd_batch *batch, struct fd_batch *dep)
{
   fd_screen_assert_locked(batch->ctx->screen);

   if (batch->dependents_mask & (1 << dep->idx))
      return;

   /* a loop should not be possible */
   debug_assert(!((1 << batch->idx) & recursive_dependents_mask(dep)));

   struct fd_batch *other = NULL;
   fd_batch_reference_locked(&other, dep);
   batch->dependents_mask |= (1 << dep->idx);
   DBG("%p: added dependency on %p", batch, dep);
}

static void
flush_write_batch(struct fd_resource *rsc) assert_dt
{
   struct fd_batch *b = NULL;
   fd_batch_reference_locked(&b, rsc->track->write_batch);

   fd_screen_unlock(b->ctx->screen);
   fd_batch_flush(b);
   fd_screen_lock(b->ctx->screen);

   fd_batch_reference_locked(&b, NULL);
}

static void
fd_batch_add_resource(struct fd_batch *batch, struct fd_resource *rsc)
{

   if (likely(fd_batch_references_resource(batch, rsc))) {
      debug_assert(_mesa_set_search_pre_hashed(batch->resources, rsc->hash, rsc));
      return;
   }

   debug_assert(!_mesa_set_search(batch->resources, rsc));

   _mesa_set_add_pre_hashed(batch->resources, rsc->hash, rsc);
   rsc->track->batch_mask |= (1 << batch->idx);
}

void
fd_batch_resource_write(struct fd_batch *batch, struct fd_resource *rsc)
{
   fd_screen_assert_locked(batch->ctx->screen);

   DBG("%p: write %p", batch, rsc);

   /* Must do this before the early out, so we unset a previous resource
    * invalidate (which may have left the write_batch state in place).
    */
   rsc->valid = true;

   if (rsc->track->write_batch == batch)
      return;

   fd_batch_write_prep(batch, rsc);

   if (rsc->stencil)
      fd_batch_resource_write(batch, rsc->stencil);

   /* note, invalidate write batch, to avoid further writes to rsc
    * resulting in a write-after-read hazard.
    */
   /* if we are pending read or write by any other batch: */
   if (unlikely(rsc->track->batch_mask & ~(1 << batch->idx))) {
      struct fd_batch_cache *cache = &batch->ctx->screen->batch_cache;
      struct fd_batch *dep;

      if (rsc->track->write_batch)
         flush_write_batch(rsc);

      foreach_batch (dep, cache, rsc->track->batch_mask) {
         struct fd_batch *b = NULL;
         if (dep == batch)
            continue;
         /* note that batch_add_dep could flush and unref dep, so
          * we need to hold a reference to keep it live for the
          * fd_bc_invalidate_batch()
          */
         fd_batch_reference(&b, dep);
         fd_batch_add_dep(batch, b);
         fd_bc_invalidate_batch(b, false);
         fd_batch_reference_locked(&b, NULL);
      }
   }
   fd_batch_reference_locked(&rsc->track->write_batch, batch);

   fd_batch_add_resource(batch, rsc);
}

void
fd_batch_resource_read_slowpath(struct fd_batch *batch, struct fd_resource *rsc)
{
   fd_screen_assert_locked(batch->ctx->screen);

   if (rsc->stencil)
      fd_batch_resource_read(batch, rsc->stencil);

   DBG("%p: read %p", batch, rsc);

   /* If reading a resource pending a write, go ahead and flush the
    * writer.  This avoids situations where we end up having to
    * flush the current batch in _resource_used()
    */
   if (unlikely(rsc->track->write_batch && rsc->track->write_batch != batch))
      flush_write_batch(rsc);

   fd_batch_add_resource(batch, rsc);
}

void
fd_batch_check_size(struct fd_batch *batch)
{
   if (FD_DBG(FLUSH)) {
      fd_batch_flush(batch);
      return;
   }

   /* Place a reasonable upper bound on prim/draw stream buffer size: */
   const unsigned limit_bits = 8 * 8 * 1024 * 1024;
   if ((batch->prim_strm_bits > limit_bits) ||
       (batch->draw_strm_bits > limit_bits)) {
      fd_batch_flush(batch);
      return;
   }

   if (!fd_ringbuffer_check_size(batch->draw))
      fd_batch_flush(batch);
}

/* emit a WAIT_FOR_IDLE only if needed, ie. if there has not already
 * been one since last draw:
 */
void
fd_wfi(struct fd_batch *batch, struct fd_ringbuffer *ring)
{
   if (batch->needs_wfi) {
      if (batch->ctx->screen->gen >= 5)
         OUT_WFI5(ring);
      else
         OUT_WFI(ring);
      batch->needs_wfi = false;
   }
}
