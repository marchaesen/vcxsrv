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
#include "freedreno_batch_cache.h"
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

   batch->resources =
      _mesa_set_create(NULL, _mesa_hash_pointer, _mesa_key_pointer_equal);
   batch->dependents =
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

   util_copy_framebuffer_state(&batch->framebuffer, NULL);
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

/* Flushes any batches that this batch depends on, recursively. */
static void
batch_flush_dependencies(struct fd_batch *batch) assert_dt
{
   set_foreach (batch->dependents, entry) {
      struct fd_batch *dep = (void *)entry->key;
      fd_batch_flush(dep);
      fd_batch_reference(&dep, NULL);
   }
   _mesa_set_clear(batch->dependents, NULL);
}

static void
batch_reset_dependencies(struct fd_batch *batch)
{
   set_foreach (batch->dependents, entry) {
      struct fd_batch *dep = (void *)entry->key;
      fd_batch_reference(&dep, NULL);
   }
   _mesa_set_clear(batch->dependents, NULL);
}

static void
batch_reset_resources(struct fd_batch *batch)
{
   struct fd_batch_cache *cache = &batch->ctx->batch_cache;

   set_foreach (batch->resources, entry) {
      struct fd_resource *rsc = (struct fd_resource *)entry->key;
      struct hash_entry *entry =
         _mesa_hash_table_search_pre_hashed(cache->written_resources, rsc->hash, rsc);
      if (entry) {
         struct fd_batch *write_batch = entry->data;
         assert(write_batch == batch);

         struct pipe_resource *table_ref = &rsc->b.b;
         pipe_resource_reference(&table_ref, NULL);

         fd_batch_reference(&write_batch, NULL);
         _mesa_hash_table_remove(cache->written_resources, entry);
      }

      ASSERTED int32_t count = p_atomic_dec_return(&rsc->batch_references);
      assert(count >= 0);

      struct pipe_resource *table_ref = &rsc->b.b;
      pipe_resource_reference(&table_ref, NULL);
   }
   /* Clear at the end so if the batch is reused we get a fully empty set
    * rather than having any deleted keys.
    */
   _mesa_set_clear(batch->resources, NULL);

   free(batch->bos);
   batch->bos = NULL;
}

void
__fd_batch_destroy(struct fd_batch *batch)
{
   DBG("%p", batch);

   fd_bc_free_key(batch);

   batch_reset_resources(batch);
   assert(batch->resources->entries == 0);
   _mesa_set_destroy(batch->resources, NULL);

   batch_reset_dependencies(batch);
   assert(batch->dependents->entries == 0);
   _mesa_set_destroy(batch->dependents, NULL);

   batch_fini(batch);

   free(batch);
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

   batch_reset_resources(batch);
   fd_bc_free_key(batch);
   batch->flushed = true;

   if (batch == batch->ctx->batch)
      fd_batch_reference(&batch->ctx->batch, NULL);

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

   /* NOTE: Many callers pass in a ctx->batch or fd_bc_writer() batches without
    * refcounting, which batch_flush will reset, so we need to hold a ref across
    * the body of the flush.
    */
   fd_batch_reference(&tmp, batch);
   batch_flush(tmp);
   fd_batch_reference(&tmp, NULL);
}

static uint32_t ASSERTED
dependents_contains(const struct fd_batch *haystack, const struct fd_batch *needle)
{
   if (haystack == needle)
      return true;

   set_foreach (haystack->dependents, entry) {
      if (dependents_contains(entry->key, needle))
         return true;
   }

   return false;
}

void
fd_batch_add_dep(struct fd_batch *batch, struct fd_batch *dep)
{
   if (_mesa_set_search(batch->dependents, dep))
      return;

   /* a loop should not be possible */
   assert(!dependents_contains(dep, batch));

   struct fd_batch *table_ref = NULL;
   fd_batch_reference(&table_ref, dep);
   _mesa_set_add(batch->dependents, dep);
   DBG("%p: added dependency on %p", batch, dep);
}

static void
fd_batch_add_resource(struct fd_batch *batch, struct fd_resource *rsc)
{
   if (fd_batch_references(batch, rsc))
      return;

   ASSERTED bool found = false;
   _mesa_set_search_or_add_pre_hashed(batch->resources, rsc->hash, rsc, &found);
   assert(!found);

   struct pipe_resource *table_ref = NULL;
   pipe_resource_reference(&table_ref, &rsc->b.b);
   p_atomic_inc(&rsc->batch_references);

   uint32_t handle = fd_bo_id(rsc->bo);
   if (batch->bos_size <= handle) {
      uint32_t new_size = MAX2(BITSET_WORDBITS,
                               util_next_power_of_two(handle + 1));
      batch->bos = realloc(batch->bos, new_size / 8);
      memset(&batch->bos[batch->bos_size / BITSET_WORDBITS], 0,
             (new_size - batch->bos_size) / 8);
      batch->bos_size = new_size;
   }
   BITSET_SET(batch->bos, handle);
}

void
fd_batch_resource_write(struct fd_batch *batch, struct fd_resource *rsc)
{
   struct fd_context *ctx = batch->ctx;
   struct fd_batch_cache *cache = &ctx->batch_cache;

   DBG("%p: write %p", batch, rsc);

   /* Must do this before the early out, so we unset a previous resource
    * invalidate (which may have left the write_batch state in place).
    */
   rsc->valid = true;

   /* This has to happen before the early out, because
    * fd_bc_invalidate_resource() may not have been called on our context to
    * clear our writer when reallocating the BO, and otherwise we could end up
    * with our batch writing the BO but returning !fd_batch_references(rsc).
    */
   fd_batch_add_resource(batch, rsc);

   struct fd_batch *prev_writer = fd_bc_writer(ctx, rsc);
   if (prev_writer == batch)
      return;

   fd_batch_write_prep(batch, rsc);

   if (rsc->stencil)
      fd_batch_resource_write(batch, rsc->stencil);

   /* Flush any other batches accessing our resource.  Similar to
    * fd_bc_flush_readers().
    */
   foreach_batch (reader, cache) {
      if (reader == batch || !fd_batch_references(reader, rsc))
         continue;
      fd_batch_flush(reader);
   }

   struct fd_batch *table_ref = NULL;
   fd_batch_reference(&table_ref, batch);
   struct pipe_resource *table_rsc_ref = NULL;
   pipe_resource_reference(&table_rsc_ref, &rsc->b.b);
   _mesa_hash_table_insert_pre_hashed(cache->written_resources, rsc->hash, rsc,
                                      batch);
}

void
fd_batch_resource_read_slowpath(struct fd_batch *batch, struct fd_resource *rsc)
{
   if (rsc->stencil)
      fd_batch_resource_read(batch, rsc->stencil);

   DBG("%p: read %p", batch, rsc);

   struct fd_context *ctx = batch->ctx;

   struct fd_batch *writer = fd_bc_writer(ctx, rsc);
   if (writer && writer != batch)
      fd_batch_flush(writer);

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
