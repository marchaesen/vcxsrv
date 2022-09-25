/*
 * Copyright (C) 2018 Rob Clark <robclark@freedesktop.org>
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

#include <assert.h>
#include <inttypes.h>
#include <pthread.h>

#include "util/hash_table.h"
#include "util/os_file.h"
#include "util/slab.h"

#include "freedreno_ringbuffer_sp.h"

/* A "softpin" implementation of submit/ringbuffer, which lowers CPU overhead
 * by avoiding the additional tracking necessary to build cmds/relocs tables
 * (but still builds a bos table)
 */

#define INIT_SIZE 0x1000

#define SUBALLOC_SIZE (32 * 1024)

/* In the pipe->flush() path, we don't have a util_queue_fence we can wait on,
 * instead use a condition-variable.  Note that pipe->flush() is not expected
 * to be a common/hot path.
 */
static pthread_cond_t  flush_cnd = PTHREAD_COND_INITIALIZER;
static pthread_mutex_t flush_mtx = PTHREAD_MUTEX_INITIALIZER;

static void finalize_current_cmd(struct fd_ringbuffer *ring);
static struct fd_ringbuffer *
fd_ringbuffer_sp_init(struct fd_ringbuffer_sp *fd_ring, uint32_t size,
                      enum fd_ringbuffer_flags flags);

/* add (if needed) bo to submit and return index: */
uint32_t
fd_submit_append_bo(struct fd_submit_sp *submit, struct fd_bo *bo)
{
   uint32_t idx;

   /* NOTE: it is legal to use the same bo on different threads for
    * different submits.  But it is not legal to use the same submit
    * from different threads.
    */
   idx = READ_ONCE(bo->idx);

   if (unlikely((idx >= submit->nr_bos) || (submit->bos[idx] != bo))) {
      uint32_t hash = _mesa_hash_pointer(bo);
      struct hash_entry *entry;

      entry = _mesa_hash_table_search_pre_hashed(submit->bo_table, hash, bo);
      if (entry) {
         /* found */
         idx = (uint32_t)(uintptr_t)entry->data;
      } else {
         idx = APPEND(submit, bos, fd_bo_ref(bo));

         _mesa_hash_table_insert_pre_hashed(submit->bo_table, hash, bo,
                                            (void *)(uintptr_t)idx);
      }
      bo->idx = idx;
   }

   return idx;
}

static void
fd_submit_suballoc_ring_bo(struct fd_submit *submit,
                           struct fd_ringbuffer_sp *fd_ring, uint32_t size)
{
   struct fd_submit_sp *fd_submit = to_fd_submit_sp(submit);
   unsigned suballoc_offset = 0;
   struct fd_bo *suballoc_bo = NULL;

   if (fd_submit->suballoc_ring) {
      struct fd_ringbuffer_sp *suballoc_ring =
         to_fd_ringbuffer_sp(fd_submit->suballoc_ring);

      suballoc_bo = suballoc_ring->ring_bo;
      suballoc_offset =
         fd_ringbuffer_size(fd_submit->suballoc_ring) + suballoc_ring->offset;

      suballoc_offset = align(suballoc_offset, 0x10);

      if ((size + suballoc_offset) > suballoc_bo->size) {
         suballoc_bo = NULL;
      }
   }

   if (!suballoc_bo) {
      // TODO possibly larger size for streaming bo?
      fd_ring->ring_bo = fd_bo_new_ring(submit->pipe->dev, SUBALLOC_SIZE);
      fd_ring->offset = 0;
   } else {
      fd_ring->ring_bo = fd_bo_ref(suballoc_bo);
      fd_ring->offset = suballoc_offset;
   }

   struct fd_ringbuffer *old_suballoc_ring = fd_submit->suballoc_ring;

   fd_submit->suballoc_ring = fd_ringbuffer_ref(&fd_ring->base);

   if (old_suballoc_ring)
      fd_ringbuffer_del(old_suballoc_ring);
}

static struct fd_ringbuffer *
fd_submit_sp_new_ringbuffer(struct fd_submit *submit, uint32_t size,
                            enum fd_ringbuffer_flags flags)
{
   struct fd_submit_sp *fd_submit = to_fd_submit_sp(submit);
   struct fd_ringbuffer_sp *fd_ring;

   fd_ring = slab_alloc(&fd_submit->ring_pool);

   fd_ring->u.submit = submit;

   /* NOTE: needs to be before _suballoc_ring_bo() since it could
    * increment the refcnt of the current ring
    */
   fd_ring->base.refcnt = 1;

   if (flags & FD_RINGBUFFER_STREAMING) {
      fd_submit_suballoc_ring_bo(submit, fd_ring, size);
   } else {
      if (flags & FD_RINGBUFFER_GROWABLE)
         size = INIT_SIZE;

      fd_ring->offset = 0;
      fd_ring->ring_bo = fd_bo_new_ring(submit->pipe->dev, size);
   }

   if (!fd_ringbuffer_sp_init(fd_ring, size, flags))
      return NULL;

   return &fd_ring->base;
}

/**
 * Prepare submit for flush, always done synchronously.
 *
 * 1) Finalize primary ringbuffer, at this point no more cmdstream may
 *    be written into it, since from the PoV of the upper level driver
 *    the submit is flushed, even if deferred
 * 2) Add cmdstream bos to bos table
 * 3) Update bo fences
 */
static bool
fd_submit_sp_flush_prep(struct fd_submit *submit, int in_fence_fd,
                        struct fd_submit_fence *out_fence)
{
   struct fd_submit_sp *fd_submit = to_fd_submit_sp(submit);
   bool has_shared = false;

   finalize_current_cmd(submit->primary);

   struct fd_ringbuffer_sp *primary =
      to_fd_ringbuffer_sp(submit->primary);

   for (unsigned i = 0; i < primary->u.nr_cmds; i++)
      fd_submit_append_bo(fd_submit, primary->u.cmds[i].ring_bo);

   simple_mtx_lock(&table_lock);
   for (unsigned i = 0; i < fd_submit->nr_bos; i++) {
      fd_bo_add_fence(fd_submit->bos[i], submit->pipe, submit->fence);
      has_shared |= fd_submit->bos[i]->shared;
   }
   simple_mtx_unlock(&table_lock);

   fd_submit->out_fence   = out_fence;
   fd_submit->in_fence_fd = (in_fence_fd == -1) ?
         -1 : os_dupfd_cloexec(in_fence_fd);

   return has_shared;
}

static void
fd_submit_sp_flush_execute(void *job, void *gdata, int thread_index)
{
   struct fd_submit *submit = job;
   struct fd_submit_sp *fd_submit = to_fd_submit_sp(submit);
   struct fd_pipe *pipe = submit->pipe;

   fd_submit->flush_submit_list(&fd_submit->submit_list);

   pthread_mutex_lock(&flush_mtx);
   assert(fd_fence_before(pipe->last_submit_fence, fd_submit->base.fence));
   pipe->last_submit_fence = fd_submit->base.fence;
   pthread_cond_broadcast(&flush_cnd);
   pthread_mutex_unlock(&flush_mtx);

   DEBUG_MSG("finish: %u", submit->fence);
}

static void
fd_submit_sp_flush_cleanup(void *job, void *gdata, int thread_index)
{
   struct fd_submit *submit = job;
   fd_submit_del(submit);
}

static int
enqueue_submit_list(struct list_head *submit_list)
{
   struct fd_submit *submit = last_submit(submit_list);
   struct fd_submit_sp *fd_submit = to_fd_submit_sp(submit);

   list_replace(submit_list, &fd_submit->submit_list);
   list_inithead(submit_list);

   struct util_queue_fence *fence;
   if (fd_submit->out_fence) {
      fence = &fd_submit->out_fence->ready;
   } else {
      util_queue_fence_init(&fd_submit->fence);
      fence = &fd_submit->fence;
   }

   DEBUG_MSG("enqueue: %u", submit->fence);

   util_queue_add_job(&submit->pipe->dev->submit_queue,
                      submit, fence,
                      fd_submit_sp_flush_execute,
                      fd_submit_sp_flush_cleanup,
                      0);

   return 0;
}

static bool
should_defer(struct fd_submit *submit)
{
   struct fd_submit_sp *fd_submit = to_fd_submit_sp(submit);

   /* if too many bo's, it may not be worth the CPU cost of submit merging: */
   if (fd_submit->nr_bos > 30)
      return false;

   /* On the kernel side, with 32K ringbuffer, we have an upper limit of 2k
    * cmds before we exceed the size of the ringbuffer, which results in
    * deadlock writing into the RB (ie. kernel doesn't finish writing into
    * the RB so it doesn't kick the GPU to start consuming from the RB)
    */
   if (submit->pipe->dev->deferred_cmds > 128)
      return false;

   return true;
}

static int
fd_submit_sp_flush(struct fd_submit *submit, int in_fence_fd,
                   struct fd_submit_fence *out_fence)
{
   struct fd_device *dev = submit->pipe->dev;
   struct fd_pipe *pipe = submit->pipe;

   /* Acquire lock before flush_prep() because it is possible to race between
    * this and pipe->flush():
    */
   simple_mtx_lock(&dev->submit_lock);

   /* If there are deferred submits from another fd_pipe, flush them now,
    * since we can't merge submits from different submitqueue's (ie. they
    * could have different priority, etc)
    */
   if (!list_is_empty(&dev->deferred_submits) &&
       (last_submit(&dev->deferred_submits)->pipe != submit->pipe)) {
      struct list_head submit_list;

      list_replace(&dev->deferred_submits, &submit_list);
      list_inithead(&dev->deferred_submits);
      dev->deferred_cmds = 0;

      enqueue_submit_list(&submit_list);
   }

   list_addtail(&fd_submit_ref(submit)->node, &dev->deferred_submits);

   bool has_shared = fd_submit_sp_flush_prep(submit, in_fence_fd, out_fence);

   assert(fd_fence_before(pipe->last_enqueue_fence, submit->fence));
   pipe->last_enqueue_fence = submit->fence;

   /* If we don't need an out-fence, we can defer the submit.
    *
    * TODO we could defer submits with in-fence as well.. if we took our own
    * reference to the fd, and merged all the in-fence-fd's when we flush the
    * deferred submits
    */
   if ((in_fence_fd == -1) && !out_fence && !has_shared && should_defer(submit)) {
      DEBUG_MSG("defer: %u", submit->fence);
      dev->deferred_cmds += fd_ringbuffer_cmd_count(submit->primary);
      assert(dev->deferred_cmds == fd_dev_count_deferred_cmds(dev));
      simple_mtx_unlock(&dev->submit_lock);

      return 0;
   }

   struct list_head submit_list;

   list_replace(&dev->deferred_submits, &submit_list);
   list_inithead(&dev->deferred_submits);
   dev->deferred_cmds = 0;

   simple_mtx_unlock(&dev->submit_lock);

   return enqueue_submit_list(&submit_list);
}

void
fd_pipe_sp_flush(struct fd_pipe *pipe, uint32_t fence)
{
   struct fd_device *dev = pipe->dev;
   struct list_head submit_list;

   DEBUG_MSG("flush: %u", fence);

   list_inithead(&submit_list);

   simple_mtx_lock(&dev->submit_lock);

   assert(!fd_fence_after(fence, pipe->last_enqueue_fence));

   foreach_submit_safe (deferred_submit, &dev->deferred_submits) {
      /* We should never have submits from multiple pipes in the deferred
       * list.  If we did, we couldn't compare their fence to our fence,
       * since each fd_pipe is an independent timeline.
       */
      if (deferred_submit->pipe != pipe)
         break;

      if (fd_fence_after(deferred_submit->fence, fence))
         break;

      list_del(&deferred_submit->node);
      list_addtail(&deferred_submit->node, &submit_list);
      dev->deferred_cmds -= fd_ringbuffer_cmd_count(deferred_submit->primary);
   }

   assert(dev->deferred_cmds == fd_dev_count_deferred_cmds(dev));

   simple_mtx_unlock(&dev->submit_lock);

   if (list_is_empty(&submit_list))
      goto flush_sync;

   enqueue_submit_list(&submit_list);

flush_sync:
   /* Once we are sure that we've enqueued at least up to the requested
    * submit, we need to be sure that submitq has caught up and flushed
    * them to the kernel
    */
   pthread_mutex_lock(&flush_mtx);
   while (fd_fence_before(pipe->last_submit_fence, fence)) {
      pthread_cond_wait(&flush_cnd, &flush_mtx);
   }
   pthread_mutex_unlock(&flush_mtx);
}

static void
fd_submit_sp_destroy(struct fd_submit *submit)
{
   struct fd_submit_sp *fd_submit = to_fd_submit_sp(submit);

   if (fd_submit->suballoc_ring)
      fd_ringbuffer_del(fd_submit->suballoc_ring);

   _mesa_hash_table_destroy(fd_submit->bo_table, NULL);

   // TODO it would be nice to have a way to assert() if all
   // rb's haven't been free'd back to the slab, because that is
   // an indication that we are leaking bo's
   slab_destroy_child(&fd_submit->ring_pool);

   for (unsigned i = 0; i < fd_submit->nr_bos; i++)
      fd_bo_del(fd_submit->bos[i]);

   free(fd_submit->bos);
   free(fd_submit);
}

static const struct fd_submit_funcs submit_funcs = {
   .new_ringbuffer = fd_submit_sp_new_ringbuffer,
   .flush = fd_submit_sp_flush,
   .destroy = fd_submit_sp_destroy,
};

struct fd_submit *
fd_submit_sp_new(struct fd_pipe *pipe, flush_submit_list_fn flush_submit_list)
{
   struct fd_submit_sp *fd_submit = calloc(1, sizeof(*fd_submit));
   struct fd_submit *submit;

   fd_submit->bo_table = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                                 _mesa_key_pointer_equal);

   slab_create_child(&fd_submit->ring_pool, &pipe->ring_pool);

   fd_submit->flush_submit_list = flush_submit_list;

   submit = &fd_submit->base;
   submit->funcs = &submit_funcs;

   return submit;
}

void
fd_pipe_sp_ringpool_init(struct fd_pipe *pipe)
{
   // TODO tune size:
   slab_create_parent(&pipe->ring_pool, sizeof(struct fd_ringbuffer_sp), 16);
}

void
fd_pipe_sp_ringpool_fini(struct fd_pipe *pipe)
{
   if (pipe->ring_pool.num_elements)
      slab_destroy_parent(&pipe->ring_pool);
}

static void
finalize_current_cmd(struct fd_ringbuffer *ring)
{
   assert(!(ring->flags & _FD_RINGBUFFER_OBJECT));

   struct fd_ringbuffer_sp *fd_ring = to_fd_ringbuffer_sp(ring);
   APPEND(&fd_ring->u, cmds,
          (struct fd_cmd_sp){
             .ring_bo = fd_bo_ref(fd_ring->ring_bo),
             .size = offset_bytes(ring->cur, ring->start),
          });
}

static void
fd_ringbuffer_sp_grow(struct fd_ringbuffer *ring, uint32_t size)
{
   struct fd_ringbuffer_sp *fd_ring = to_fd_ringbuffer_sp(ring);
   struct fd_pipe *pipe = fd_ring->u.submit->pipe;

   assert(ring->flags & FD_RINGBUFFER_GROWABLE);

   finalize_current_cmd(ring);

   fd_bo_del(fd_ring->ring_bo);
   fd_ring->ring_bo = fd_bo_new_ring(pipe->dev, size);

   ring->start = fd_bo_map(fd_ring->ring_bo);
   ring->end = &(ring->start[size / 4]);
   ring->cur = ring->start;
   ring->size = size;
}

static inline bool
fd_ringbuffer_references_bo(struct fd_ringbuffer *ring, struct fd_bo *bo)
{
   struct fd_ringbuffer_sp *fd_ring = to_fd_ringbuffer_sp(ring);

   for (int i = 0; i < fd_ring->u.nr_reloc_bos; i++) {
      if (fd_ring->u.reloc_bos[i] == bo)
         return true;
   }
   return false;
}

static void
fd_ringbuffer_sp_emit_bo_nonobj(struct fd_ringbuffer *ring, struct fd_bo *bo)
{
   assert(!(ring->flags & _FD_RINGBUFFER_OBJECT));

   struct fd_ringbuffer_sp *fd_ring = to_fd_ringbuffer_sp(ring);
   struct fd_submit_sp *fd_submit = to_fd_submit_sp(fd_ring->u.submit);

   fd_submit_append_bo(fd_submit, bo);
}

static void
fd_ringbuffer_sp_emit_bo_obj(struct fd_ringbuffer *ring, struct fd_bo *bo)
{
   assert(ring->flags & _FD_RINGBUFFER_OBJECT);

   struct fd_ringbuffer_sp *fd_ring = to_fd_ringbuffer_sp(ring);

   /* Avoid emitting duplicate BO references into the list.  Ringbuffer
    * objects are long-lived, so this saves ongoing work at draw time in
    * exchange for a bit at context setup/first draw.  And the number of
    * relocs per ringbuffer object is fairly small, so the O(n^2) doesn't
    * hurt much.
    */
   if (!fd_ringbuffer_references_bo(ring, bo)) {
      APPEND(&fd_ring->u, reloc_bos, fd_bo_ref(bo));
   }
}

#define PTRSZ 64
#include "freedreno_ringbuffer_sp_reloc.h"
#undef PTRSZ
#define PTRSZ 32
#include "freedreno_ringbuffer_sp_reloc.h"
#undef PTRSZ

static uint32_t
fd_ringbuffer_sp_cmd_count(struct fd_ringbuffer *ring)
{
   if (ring->flags & FD_RINGBUFFER_GROWABLE)
      return to_fd_ringbuffer_sp(ring)->u.nr_cmds + 1;
   return 1;
}

static bool
fd_ringbuffer_sp_check_size(struct fd_ringbuffer *ring)
{
   assert(!(ring->flags & _FD_RINGBUFFER_OBJECT));
   struct fd_ringbuffer_sp *fd_ring = to_fd_ringbuffer_sp(ring);
   struct fd_submit *submit = fd_ring->u.submit;

   if (to_fd_submit_sp(submit)->nr_bos > MAX_ARRAY_SIZE/2) {
      return false;
   }

   return true;
}

static void
fd_ringbuffer_sp_destroy(struct fd_ringbuffer *ring)
{
   struct fd_ringbuffer_sp *fd_ring = to_fd_ringbuffer_sp(ring);

   fd_bo_del(fd_ring->ring_bo);

   if (ring->flags & _FD_RINGBUFFER_OBJECT) {
      for (unsigned i = 0; i < fd_ring->u.nr_reloc_bos; i++) {
         fd_bo_del(fd_ring->u.reloc_bos[i]);
      }
      free(fd_ring->u.reloc_bos);

      free(fd_ring);
   } else {
      struct fd_submit *submit = fd_ring->u.submit;

      for (unsigned i = 0; i < fd_ring->u.nr_cmds; i++) {
         fd_bo_del(fd_ring->u.cmds[i].ring_bo);
      }
      free(fd_ring->u.cmds);

      slab_free(&to_fd_submit_sp(submit)->ring_pool, fd_ring);
   }
}

static const struct fd_ringbuffer_funcs ring_funcs_nonobj_32 = {
   .grow = fd_ringbuffer_sp_grow,
   .emit_bo = fd_ringbuffer_sp_emit_bo_nonobj,
   .emit_reloc = fd_ringbuffer_sp_emit_reloc_nonobj_32,
   .emit_reloc_ring = fd_ringbuffer_sp_emit_reloc_ring_32,
   .cmd_count = fd_ringbuffer_sp_cmd_count,
   .check_size = fd_ringbuffer_sp_check_size,
   .destroy = fd_ringbuffer_sp_destroy,
};

static const struct fd_ringbuffer_funcs ring_funcs_obj_32 = {
   .grow = fd_ringbuffer_sp_grow,
   .emit_bo = fd_ringbuffer_sp_emit_bo_obj,
   .emit_reloc = fd_ringbuffer_sp_emit_reloc_obj_32,
   .emit_reloc_ring = fd_ringbuffer_sp_emit_reloc_ring_32,
   .cmd_count = fd_ringbuffer_sp_cmd_count,
   .destroy = fd_ringbuffer_sp_destroy,
};

static const struct fd_ringbuffer_funcs ring_funcs_nonobj_64 = {
   .grow = fd_ringbuffer_sp_grow,
   .emit_bo = fd_ringbuffer_sp_emit_bo_nonobj,
   .emit_reloc = fd_ringbuffer_sp_emit_reloc_nonobj_64,
   .emit_reloc_ring = fd_ringbuffer_sp_emit_reloc_ring_64,
   .cmd_count = fd_ringbuffer_sp_cmd_count,
   .check_size = fd_ringbuffer_sp_check_size,
   .destroy = fd_ringbuffer_sp_destroy,
};

static const struct fd_ringbuffer_funcs ring_funcs_obj_64 = {
   .grow = fd_ringbuffer_sp_grow,
   .emit_bo = fd_ringbuffer_sp_emit_bo_obj,
   .emit_reloc = fd_ringbuffer_sp_emit_reloc_obj_64,
   .emit_reloc_ring = fd_ringbuffer_sp_emit_reloc_ring_64,
   .cmd_count = fd_ringbuffer_sp_cmd_count,
   .destroy = fd_ringbuffer_sp_destroy,
};

static inline struct fd_ringbuffer *
fd_ringbuffer_sp_init(struct fd_ringbuffer_sp *fd_ring, uint32_t size,
                      enum fd_ringbuffer_flags flags)
{
   struct fd_ringbuffer *ring = &fd_ring->base;

   assert(fd_ring->ring_bo);

   uint8_t *base = fd_bo_map(fd_ring->ring_bo);
   ring->start = (void *)(base + fd_ring->offset);
   ring->end = &(ring->start[size / 4]);
   ring->cur = ring->start;

   ring->size = size;
   ring->flags = flags;

   if (flags & _FD_RINGBUFFER_OBJECT) {
      if (fd_dev_64b(&fd_ring->u.pipe->dev_id)) {
         ring->funcs = &ring_funcs_obj_64;
      } else {
         ring->funcs = &ring_funcs_obj_32;
      }
   } else {
      if (fd_dev_64b(&fd_ring->u.submit->pipe->dev_id)) {
         ring->funcs = &ring_funcs_nonobj_64;
      } else {
         ring->funcs = &ring_funcs_nonobj_32;
      }
   }

   // TODO initializing these could probably be conditional on flags
   // since unneed for FD_RINGBUFFER_STAGING case..
   fd_ring->u.cmds = NULL;
   fd_ring->u.nr_cmds = fd_ring->u.max_cmds = 0;

   fd_ring->u.reloc_bos = NULL;
   fd_ring->u.nr_reloc_bos = fd_ring->u.max_reloc_bos = 0;

   return ring;
}

struct fd_ringbuffer *
fd_ringbuffer_sp_new_object(struct fd_pipe *pipe, uint32_t size)
{
   struct fd_device *dev = pipe->dev;
   struct fd_ringbuffer_sp *fd_ring = malloc(sizeof(*fd_ring));

   /* Lock access to the fd_pipe->suballoc_* since ringbuffer object allocation
    * can happen both on the frontend (most CSOs) and the driver thread (a6xx
    * cached tex state, for example)
    */
   simple_mtx_lock(&dev->suballoc_lock);

   /* Maximum known alignment requirement is a6xx's TEX_CONST at 16 dwords */
   fd_ring->offset = align(dev->suballoc_offset, 64);
   if (!dev->suballoc_bo ||
       fd_ring->offset + size > fd_bo_size(dev->suballoc_bo)) {
      if (dev->suballoc_bo)
         fd_bo_del(dev->suballoc_bo);
      dev->suballoc_bo =
         fd_bo_new_ring(dev, MAX2(SUBALLOC_SIZE, align(size, 4096)));
      fd_ring->offset = 0;
   }

   fd_ring->u.pipe = pipe;
   fd_ring->ring_bo = fd_bo_ref(dev->suballoc_bo);
   fd_ring->base.refcnt = 1;

   dev->suballoc_offset = fd_ring->offset + size;

   simple_mtx_unlock(&dev->suballoc_lock);

   return fd_ringbuffer_sp_init(fd_ring, size, _FD_RINGBUFFER_OBJECT);
}
