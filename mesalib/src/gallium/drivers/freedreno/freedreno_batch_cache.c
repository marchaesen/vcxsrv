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
#define XXH_INLINE_ALL
#include "util/xxhash.h"

#include "freedreno_batch.h"
#include "freedreno_batch_cache.h"
#include "freedreno_context.h"
#include "freedreno_resource.h"

/* Overview:
 *
 *   The batch cache provides lookup for mapping pipe_framebuffer_state
 *   to a batch.
 *
 *   It does this via hashtable, with key that roughly matches the
 *   pipe_framebuffer_state, as described below.
 *
 * Batch Cache hashtable key:
 *
 *   To serialize the key, and to avoid dealing with holding a reference to
 *   pipe_surface's (which hold a reference to pipe_resource and complicate
 *   the whole refcnting thing), the key is variable length and inline's the
 *   pertinent details of the pipe_surface.
 *
 * Batch:
 *
 *   Each batch needs to hold a reference to each resource it depends on (ie.
 *   anything that needs a mem2gmem).  And a weak reference to resources it
 *   renders to.  (If both src[n] and dst[n] are not NULL then they are the
 *   same.)
 *
 *   When a resource is destroyed, we need to remove entries in the batch
 *   cache that reference the resource, to avoid dangling pointer issues.
 *   So each resource holds a hashset of batches which have reference them
 *   in their hashtable key.
 *
 *   When a batch has weak reference to no more resources (ie. all the
 *   surfaces it rendered to are destroyed) the batch can be destroyed.
 *   Could happen in an app that renders and never uses the result.  More
 *   common scenario, I think, will be that some, but not all, of the
 *   surfaces are destroyed before the batch is submitted.
 *
 *   If (for example), batch writes to zsbuf but that surface is destroyed
 *   before batch is submitted, we can skip gmem2mem (but still need to
 *   alloc gmem space as before.  If the batch depended on previous contents
 *   of that surface, it would be holding a reference so the surface would
 *   not have been destroyed.
 */

struct fd_batch_key {
   uint32_t width;
   uint32_t height;
   uint16_t layers;
   uint16_t samples;
   uint16_t num_surfs;
   uint16_t ctx_seqno;
   struct {
      struct pipe_resource *texture;
      union pipe_surface_desc u;
      uint8_t pos, samples;
      uint16_t format;
   } surf[0];
};

static struct fd_batch_key *
key_alloc(unsigned num_surfs)
{
   struct fd_batch_key *key = CALLOC_VARIANT_LENGTH_STRUCT(
      fd_batch_key, sizeof(key->surf[0]) * num_surfs);
   return key;
}

uint32_t
fd_batch_key_hash(const void *_key)
{
   const struct fd_batch_key *key = _key;
   uint32_t hash = 0;
   hash = XXH32(key, offsetof(struct fd_batch_key, surf[0]), hash);
   hash = XXH32(key->surf, sizeof(key->surf[0]) * key->num_surfs, hash);
   return hash;
}

bool
fd_batch_key_equals(const void *_a, const void *_b)
{
   const struct fd_batch_key *a = _a;
   const struct fd_batch_key *b = _b;
   return (memcmp(a, b, offsetof(struct fd_batch_key, surf[0])) == 0) &&
          (memcmp(a->surf, b->surf, sizeof(a->surf[0]) * a->num_surfs) == 0);
}

struct fd_batch_key *
fd_batch_key_clone(void *mem_ctx, const struct fd_batch_key *key)
{
   unsigned sz =
      sizeof(struct fd_batch_key) + (sizeof(key->surf[0]) * key->num_surfs);
   struct fd_batch_key *new_key = rzalloc_size(mem_ctx, sz);
   memcpy(new_key, key, sz);
   return new_key;
}

void
fd_bc_init(struct fd_context *ctx)
{
   struct fd_batch_cache *cache = &ctx->batch_cache;

   cache->ht =
      _mesa_hash_table_create(NULL, fd_batch_key_hash, fd_batch_key_equals);
   cache->written_resources = _mesa_pointer_hash_table_create(NULL);
}

void
fd_bc_fini(struct fd_context *ctx) assert_dt
{
   struct fd_batch_cache *cache = &ctx->batch_cache;

   fd_bc_flush(ctx, false);
   _mesa_hash_table_destroy(cache->ht, NULL);
   _mesa_hash_table_destroy(cache->written_resources, NULL);
}

/* Flushes all batches in the batch cache.  Used at glFlush() and similar times. */
void
fd_bc_flush(struct fd_context *ctx, bool deferred) assert_dt
{
   struct fd_batch_cache *cache = &ctx->batch_cache;

   /* deferred flush doesn't actually flush, but it marks every other
    * batch associated with the context as dependent on the current
    * batch.  So when the current batch gets flushed, all other batches
    * that came before also get flushed.
    */
   if (deferred) {
      struct fd_batch *current_batch = fd_context_batch(ctx);

      foreach_batch (batch, cache) {
         if (batch != current_batch)
            fd_batch_add_dep(current_batch, batch);
      }
      fd_batch_reference(&current_batch, NULL);
   } else {
      foreach_batch (batch, cache) {
         fd_batch_flush(batch);
      }
   }
}

/**
 * Flushes the batch (if any) writing this resource.
 */
void
fd_bc_flush_writer(struct fd_context *ctx, struct fd_resource *rsc) assert_dt
{
   struct fd_batch_cache *cache = &ctx->batch_cache;
   struct hash_entry *entry =
      _mesa_hash_table_search_pre_hashed(cache->written_resources, rsc->hash, rsc);
   if (entry) {
      fd_batch_flush(entry->data);
   }
}

/**
 * Flushes any batches reading this resource.
 */
void
fd_bc_flush_readers(struct fd_context *ctx, struct fd_resource *rsc) assert_dt
{
   foreach_batch (batch, &ctx->batch_cache) {
      if (fd_batch_references(batch, rsc))
         fd_batch_flush(batch);
   }
}

/**
 * Flushes any batches accessing this resource as part of the gmem key.
 *
 * Used in resource shadowing.
 */
void
fd_bc_flush_gmem_users(struct fd_context *ctx, struct fd_resource *rsc)
{
   foreach_batch (batch, &ctx->batch_cache) {
      if (!batch->key)
         continue;
      for (int i = 0; i < batch->key->num_surfs; i++) {
         if (batch->key->surf[i].texture == &rsc->b.b) {
            struct fd_batch *tmp = NULL;
            fd_batch_reference(&tmp, batch);
            fd_batch_flush(batch);
            fd_batch_reference(&tmp, NULL);
            break;
         }
      }
   }
}

void
fd_bc_dump(struct fd_context *ctx, const char *fmt, ...)
{
   struct fd_batch_cache *cache = &ctx->batch_cache;

   if (!FD_DBG(MSGS))
      return;

   va_list ap;
   va_start(ap, fmt);
   vprintf(fmt, ap);
   va_end(ap);

   foreach_batch (batch, cache) {
      printf("  %p<%u>%s\n", batch, batch->seqno,
             batch->needs_flush ? ", NEEDS FLUSH" : "");
   }

   printf("----\n");
}

/* Removes a batch from the batch cache (typically after a flush) */
void
fd_bc_free_key(struct fd_batch *batch)
{
   if (!batch)
      return;

   struct fd_context *ctx = batch->ctx;
   struct fd_batch_cache *cache = &ctx->batch_cache;

   if (batch->key) {
      _mesa_hash_table_remove_key(cache->ht, batch->key);
      free((void *)batch->key);
      batch->key = NULL;
   }
}

/* Called when the resource is has had its underlying storage replaced, so
 * previous batch references to it are no longer relevant for flushing access to
 * that storage.
 */
void
fd_bc_invalidate_resource(struct fd_context *ctx, struct fd_resource *rsc)
{
   foreach_batch (batch, &ctx->batch_cache) {
      struct set_entry *entry = _mesa_set_search_pre_hashed(batch->resources, rsc->hash, rsc);
      if (entry) {
         struct pipe_resource *table_ref = &rsc->b.b;
         pipe_resource_reference(&table_ref, NULL);

         ASSERTED int32_t count = p_atomic_dec_return(&rsc->batch_references);
         assert(count >= 0);

         _mesa_set_remove(batch->resources, entry);
      }
   }

   struct hash_entry *entry =
      _mesa_hash_table_search(ctx->batch_cache.written_resources, rsc);
   if (entry) {
      struct fd_batch *batch = entry->data;
      struct pipe_resource *table_ref = &rsc->b.b;
      pipe_resource_reference(&table_ref, NULL);
      _mesa_hash_table_remove(ctx->batch_cache.written_resources, entry);
      fd_batch_reference(&batch, NULL);
   }
}

static struct fd_batch *
alloc_batch(struct fd_batch_cache *cache, struct fd_context *ctx, bool nondraw) assert_dt
{
   struct fd_batch *batch;

   if (cache->ht->entries >= 32) {
      /* TODO: is LRU the better policy?  Or perhaps the batch that
       * depends on the fewest other batches?
       */
      struct fd_batch *flush_batch = NULL;
      foreach_batch (batch, cache) {
         if (!flush_batch || (batch->seqno < flush_batch->seqno))
            fd_batch_reference(&flush_batch, batch);
      }

      DBG("%p: too many batches!  flush forced!", flush_batch);
      fd_batch_flush(flush_batch);

      fd_batch_reference(&flush_batch, NULL);
   }

   batch = fd_batch_create(ctx, nondraw);
   if (!batch)
      return NULL;

   batch->seqno = cache->cnt++;

   return batch;
}

struct fd_batch *
fd_bc_alloc_batch(struct fd_context *ctx, bool nondraw) assert_dt
{
   struct fd_batch_cache *cache = &ctx->batch_cache;
   struct fd_batch *batch;

   /* For normal draw batches, pctx->set_framebuffer_state() handles
    * this, but for nondraw batches, this is a nice central location
    * to handle them all.
    */
   if (nondraw)
      fd_context_switch_from(ctx);

   batch = alloc_batch(cache, ctx, nondraw);

   if (batch && nondraw)
      fd_context_switch_to(ctx, batch);

   return batch;
}

static struct fd_batch *
batch_from_key(struct fd_context *ctx, struct fd_batch_key *key) assert_dt
{
   struct fd_batch_cache *cache = &ctx->batch_cache;
   struct fd_batch *batch = NULL;
   uint32_t hash = fd_batch_key_hash(key);
   struct hash_entry *entry =
      _mesa_hash_table_search_pre_hashed(cache->ht, hash, key);

   if (entry) {
      free(key);
      fd_batch_reference(&batch, (struct fd_batch *)entry->data);
      assert(!batch->flushed);
      return batch;
   }

   batch = alloc_batch(cache, ctx, false);
#ifdef DEBUG
   DBG("%p: hash=0x%08x, %ux%u, %u layers, %u samples", batch, hash, key->width,
       key->height, key->layers, key->samples);
   for (unsigned idx = 0; idx < key->num_surfs; idx++) {
      DBG("%p:  surf[%u]: %p (%s) (%u,%u / %u,%u,%u)", batch,
          key->surf[idx].pos, key->surf[idx].texture,
          util_format_name(key->surf[idx].format),
          key->surf[idx].u.buf.first_element, key->surf[idx].u.buf.last_element,
          key->surf[idx].u.tex.first_layer, key->surf[idx].u.tex.last_layer,
          key->surf[idx].u.tex.level);
   }
#endif
   if (!batch)
      return NULL;

   /* reset max_scissor, which will be adjusted on draws
    * according to the actual scissor.
    */
   batch->max_scissor.minx = ~0;
   batch->max_scissor.miny = ~0;
   batch->max_scissor.maxx = 0;
   batch->max_scissor.maxy = 0;

   _mesa_hash_table_insert_pre_hashed(cache->ht, hash, key, batch);
   batch->key = key;
   batch->hash = hash;

   return batch;
}

static void
key_surf(struct fd_batch_key *key, unsigned idx, unsigned pos,
         struct pipe_surface *psurf)
{
   key->surf[idx].texture = psurf->texture;
   key->surf[idx].u = psurf->u;
   key->surf[idx].pos = pos;
   key->surf[idx].samples = MAX2(1, psurf->nr_samples);
   key->surf[idx].format = psurf->format;
}

struct fd_batch *
fd_batch_from_fb(struct fd_context *ctx,
                 const struct pipe_framebuffer_state *pfb)
{
   unsigned idx = 0, n = pfb->nr_cbufs + (pfb->zsbuf ? 1 : 0);
   struct fd_batch_key *key = key_alloc(n);

   key->width = pfb->width;
   key->height = pfb->height;
   key->layers = pfb->layers;
   key->samples = util_framebuffer_get_num_samples(pfb);
   key->ctx_seqno = ctx->seqno;

   if (pfb->zsbuf)
      key_surf(key, idx++, 0, pfb->zsbuf);

   for (unsigned i = 0; i < pfb->nr_cbufs; i++)
      if (pfb->cbufs[i])
         key_surf(key, idx++, i + 1, pfb->cbufs[i]);

   key->num_surfs = idx;

   return batch_from_key(ctx, key);
}
