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

#include "drm/freedreno_ringbuffer.h"
#include "msm_priv.h"

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


struct msm_submit_sp {
   struct fd_submit base;

   DECLARE_ARRAY(struct fd_bo *, bos);

   /* maps fd_bo to idx in bos table: */
   struct hash_table *bo_table;

   struct slab_child_pool ring_pool;

   /* Allow for sub-allocation of stateobj ring buffers (ie. sharing
    * the same underlying bo)..
    *
    * We also rely on previous stateobj having been fully constructed
    * so we can reclaim extra space at it's end.
    */
   struct fd_ringbuffer *suballoc_ring;

   /* Flush args, potentially attached to the last submit in the list
    * of submits to merge:
    */
   int in_fence_fd;
   struct fd_submit_fence *out_fence;

   /* State for enqueued submits:
    */
   struct list_head submit_list;   /* includes this submit as last element */

   /* Used in case out_fence==NULL: */
   struct util_queue_fence fence;
};
FD_DEFINE_CAST(fd_submit, msm_submit_sp);

/* for FD_RINGBUFFER_GROWABLE rb's, tracks the 'finalized' cmdstream buffers
 * and sizes.  Ie. a finalized buffer can have no more commands appended to
 * it.
 */
struct msm_cmd_sp {
   struct fd_bo *ring_bo;
   unsigned size;
};

struct msm_ringbuffer_sp {
   struct fd_ringbuffer base;

   /* for FD_RINGBUFFER_STREAMING rb's which are sub-allocated */
   unsigned offset;

   union {
      /* for _FD_RINGBUFFER_OBJECT case, the array of BOs referenced from
       * this one
       */
      struct {
         struct fd_pipe *pipe;
         DECLARE_ARRAY(struct fd_bo *, reloc_bos);
      };
      /* for other cases: */
      struct {
         struct fd_submit *submit;
         DECLARE_ARRAY(struct msm_cmd_sp, cmds);
      };
   } u;

   struct fd_bo *ring_bo;
};
FD_DEFINE_CAST(fd_ringbuffer, msm_ringbuffer_sp);

static void finalize_current_cmd(struct fd_ringbuffer *ring);
static struct fd_ringbuffer *
msm_ringbuffer_sp_init(struct msm_ringbuffer_sp *msm_ring, uint32_t size,
                       enum fd_ringbuffer_flags flags);

/* add (if needed) bo to submit and return index: */
static uint32_t
msm_submit_append_bo(struct msm_submit_sp *submit, struct fd_bo *bo)
{
   struct msm_bo *msm_bo = to_msm_bo(bo);
   uint32_t idx;

   /* NOTE: it is legal to use the same bo on different threads for
    * different submits.  But it is not legal to use the same submit
    * from different threads.
    */
   idx = READ_ONCE(msm_bo->idx);

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
      msm_bo->idx = idx;
   }

   return idx;
}

static void
msm_submit_suballoc_ring_bo(struct fd_submit *submit,
                            struct msm_ringbuffer_sp *msm_ring, uint32_t size)
{
   struct msm_submit_sp *msm_submit = to_msm_submit_sp(submit);
   unsigned suballoc_offset = 0;
   struct fd_bo *suballoc_bo = NULL;

   if (msm_submit->suballoc_ring) {
      struct msm_ringbuffer_sp *suballoc_ring =
         to_msm_ringbuffer_sp(msm_submit->suballoc_ring);

      suballoc_bo = suballoc_ring->ring_bo;
      suballoc_offset =
         fd_ringbuffer_size(msm_submit->suballoc_ring) + suballoc_ring->offset;

      suballoc_offset = align(suballoc_offset, 0x10);

      if ((size + suballoc_offset) > suballoc_bo->size) {
         suballoc_bo = NULL;
      }
   }

   if (!suballoc_bo) {
      // TODO possibly larger size for streaming bo?
      msm_ring->ring_bo = fd_bo_new_ring(submit->pipe->dev, SUBALLOC_SIZE);
      msm_ring->offset = 0;
   } else {
      msm_ring->ring_bo = fd_bo_ref(suballoc_bo);
      msm_ring->offset = suballoc_offset;
   }

   struct fd_ringbuffer *old_suballoc_ring = msm_submit->suballoc_ring;

   msm_submit->suballoc_ring = fd_ringbuffer_ref(&msm_ring->base);

   if (old_suballoc_ring)
      fd_ringbuffer_del(old_suballoc_ring);
}

static struct fd_ringbuffer *
msm_submit_sp_new_ringbuffer(struct fd_submit *submit, uint32_t size,
                             enum fd_ringbuffer_flags flags)
{
   struct msm_submit_sp *msm_submit = to_msm_submit_sp(submit);
   struct msm_ringbuffer_sp *msm_ring;

   msm_ring = slab_alloc(&msm_submit->ring_pool);

   msm_ring->u.submit = submit;

   /* NOTE: needs to be before _suballoc_ring_bo() since it could
    * increment the refcnt of the current ring
    */
   msm_ring->base.refcnt = 1;

   if (flags & FD_RINGBUFFER_STREAMING) {
      msm_submit_suballoc_ring_bo(submit, msm_ring, size);
   } else {
      if (flags & FD_RINGBUFFER_GROWABLE)
         size = INIT_SIZE;

      msm_ring->offset = 0;
      msm_ring->ring_bo = fd_bo_new_ring(submit->pipe->dev, size);
   }

   if (!msm_ringbuffer_sp_init(msm_ring, size, flags))
      return NULL;

   return &msm_ring->base;
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
msm_submit_sp_flush_prep(struct fd_submit *submit, int in_fence_fd,
                         struct fd_submit_fence *out_fence)
{
   struct msm_submit_sp *msm_submit = to_msm_submit_sp(submit);
   bool has_shared = false;

   finalize_current_cmd(submit->primary);

   struct msm_ringbuffer_sp *primary =
      to_msm_ringbuffer_sp(submit->primary);

   for (unsigned i = 0; i < primary->u.nr_cmds; i++)
      msm_submit_append_bo(msm_submit, primary->u.cmds[i].ring_bo);

   simple_mtx_lock(&table_lock);
   for (unsigned i = 0; i < msm_submit->nr_bos; i++) {
      fd_bo_add_fence(msm_submit->bos[i], submit->pipe, submit->fence);
      has_shared |= msm_submit->bos[i]->shared;
   }
   simple_mtx_unlock(&table_lock);

   msm_submit->out_fence   = out_fence;
   msm_submit->in_fence_fd = (in_fence_fd == -1) ?
         -1 : os_dupfd_cloexec(in_fence_fd);

   return has_shared;
}

static int
flush_submit_list(struct list_head *submit_list)
{
   struct msm_submit_sp *msm_submit = to_msm_submit_sp(last_submit(submit_list));
   struct msm_pipe *msm_pipe = to_msm_pipe(msm_submit->base.pipe);
   struct drm_msm_gem_submit req = {
      .flags = msm_pipe->pipe,
      .queueid = msm_pipe->queue_id,
   };
   int ret;

   unsigned nr_cmds = 0;

   /* Determine the number of extra cmds's from deferred submits that
    * we will be merging in:
    */
   foreach_submit (submit, submit_list) {
      assert(submit->pipe == &msm_pipe->base);
      nr_cmds += to_msm_ringbuffer_sp(submit->primary)->u.nr_cmds;
   }

   struct drm_msm_gem_submit_cmd cmds[nr_cmds];

   unsigned cmd_idx = 0;

   /* Build up the table of cmds, and for all but the last submit in the
    * list, merge their bo tables into the last submit.
    */
   foreach_submit_safe (submit, submit_list) {
      struct msm_ringbuffer_sp *deferred_primary =
         to_msm_ringbuffer_sp(submit->primary);

      for (unsigned i = 0; i < deferred_primary->u.nr_cmds; i++) {
         cmds[cmd_idx].type = MSM_SUBMIT_CMD_BUF;
         cmds[cmd_idx].submit_idx =
               msm_submit_append_bo(msm_submit, deferred_primary->u.cmds[i].ring_bo);
         cmds[cmd_idx].submit_offset = deferred_primary->offset;
         cmds[cmd_idx].size = deferred_primary->u.cmds[i].size;
         cmds[cmd_idx].pad = 0;
         cmds[cmd_idx].nr_relocs = 0;

         cmd_idx++;
      }

      /* We are merging all the submits in the list into the last submit,
       * so the remainder of the loop body doesn't apply to the last submit
       */
      if (submit == last_submit(submit_list)) {
         DEBUG_MSG("merged %u submits", cmd_idx);
         break;
      }

      struct msm_submit_sp *msm_deferred_submit = to_msm_submit_sp(submit);
      for (unsigned i = 0; i < msm_deferred_submit->nr_bos; i++) {
         /* Note: if bo is used in both the current submit and the deferred
          * submit being merged, we expect to hit the fast-path as we add it
          * to the current submit:
          */
         msm_submit_append_bo(msm_submit, msm_deferred_submit->bos[i]);
      }

      /* Now that the cmds/bos have been transfered over to the current submit,
       * we can remove the deferred submit from the list and drop it's reference
       */
      list_del(&submit->node);
      fd_submit_del(submit);
   }

   if (msm_submit->in_fence_fd != -1) {
      req.flags |= MSM_SUBMIT_FENCE_FD_IN;
      req.fence_fd = msm_submit->in_fence_fd;
      msm_pipe->no_implicit_sync = true;
   }

   if (msm_pipe->no_implicit_sync) {
      req.flags |= MSM_SUBMIT_NO_IMPLICIT;
   }

   if (msm_submit->out_fence && msm_submit->out_fence->use_fence_fd) {
      req.flags |= MSM_SUBMIT_FENCE_FD_OUT;
   }

   /* Needs to be after get_cmd() as that could create bos/cmds table:
    *
    * NOTE allocate on-stack in the common case, but with an upper-
    * bound to limit on-stack allocation to 4k:
    */
   const unsigned bo_limit = sizeof(struct drm_msm_gem_submit_bo) / 4096;
   bool bos_on_stack = msm_submit->nr_bos < bo_limit;
   struct drm_msm_gem_submit_bo
      _submit_bos[bos_on_stack ? msm_submit->nr_bos : 0];
   struct drm_msm_gem_submit_bo *submit_bos;
   if (bos_on_stack) {
      submit_bos = _submit_bos;
   } else {
      submit_bos = malloc(msm_submit->nr_bos * sizeof(submit_bos[0]));
   }

   for (unsigned i = 0; i < msm_submit->nr_bos; i++) {
      submit_bos[i].flags = msm_submit->bos[i]->reloc_flags;
      submit_bos[i].handle = msm_submit->bos[i]->handle;
      submit_bos[i].presumed = 0;
   }

   req.bos = VOID2U64(submit_bos);
   req.nr_bos = msm_submit->nr_bos;
   req.cmds = VOID2U64(cmds);
   req.nr_cmds = nr_cmds;

   DEBUG_MSG("nr_cmds=%u, nr_bos=%u", req.nr_cmds, req.nr_bos);

   ret = drmCommandWriteRead(msm_pipe->base.dev->fd, DRM_MSM_GEM_SUBMIT, &req,
                             sizeof(req));
   if (ret) {
      ERROR_MSG("submit failed: %d (%s)", ret, strerror(errno));
      msm_dump_submit(&req);
   } else if (!ret && msm_submit->out_fence) {
      msm_submit->out_fence->fence.kfence = req.fence;
      msm_submit->out_fence->fence.ufence = msm_submit->base.fence;
      msm_submit->out_fence->fence_fd = req.fence_fd;
   }

   if (!bos_on_stack)
      free(submit_bos);

   pthread_mutex_lock(&flush_mtx);
   assert(fd_fence_before(msm_pipe->last_submit_fence, msm_submit->base.fence));
   msm_pipe->last_submit_fence = msm_submit->base.fence;
   pthread_cond_broadcast(&flush_cnd);
   pthread_mutex_unlock(&flush_mtx);

   if (msm_submit->in_fence_fd != -1)
      close(msm_submit->in_fence_fd);

   return ret;
}

static void
msm_submit_sp_flush_execute(void *job, void *gdata, int thread_index)
{
   struct fd_submit *submit = job;
   struct msm_submit_sp *msm_submit = to_msm_submit_sp(submit);

   flush_submit_list(&msm_submit->submit_list);

   DEBUG_MSG("finish: %u", submit->fence);
}

static void
msm_submit_sp_flush_cleanup(void *job, void *gdata, int thread_index)
{
   struct fd_submit *submit = job;
   fd_submit_del(submit);
}

static int
enqueue_submit_list(struct list_head *submit_list)
{
   struct fd_submit *submit = last_submit(submit_list);
   struct msm_submit_sp *msm_submit = to_msm_submit_sp(submit);
   struct msm_device *msm_dev = to_msm_device(submit->pipe->dev);

   list_replace(submit_list, &msm_submit->submit_list);
   list_inithead(submit_list);

   struct util_queue_fence *fence;
   if (msm_submit->out_fence) {
      fence = &msm_submit->out_fence->ready;
   } else {
      util_queue_fence_init(&msm_submit->fence);
      fence = &msm_submit->fence;
   }

   DEBUG_MSG("enqueue: %u", submit->fence);

   util_queue_add_job(&msm_dev->submit_queue,
                      submit, fence,
                      msm_submit_sp_flush_execute,
                      msm_submit_sp_flush_cleanup,
                      0);

   return 0;
}

static bool
should_defer(struct fd_submit *submit)
{
   struct msm_submit_sp *msm_submit = to_msm_submit_sp(submit);

   /* if too many bo's, it may not be worth the CPU cost of submit merging: */
   if (msm_submit->nr_bos > 30)
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
msm_submit_sp_flush(struct fd_submit *submit, int in_fence_fd,
                    struct fd_submit_fence *out_fence)
{
   struct fd_device *dev = submit->pipe->dev;
   struct msm_pipe *msm_pipe = to_msm_pipe(submit->pipe);

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

   bool has_shared = msm_submit_sp_flush_prep(submit, in_fence_fd, out_fence);

   assert(fd_fence_before(msm_pipe->last_enqueue_fence, submit->fence));
   msm_pipe->last_enqueue_fence = submit->fence;

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
msm_pipe_sp_flush(struct fd_pipe *pipe, uint32_t fence)
{
   struct msm_pipe *msm_pipe = to_msm_pipe(pipe);
   struct fd_device *dev = pipe->dev;
   struct list_head submit_list;

   DEBUG_MSG("flush: %u", fence);

   list_inithead(&submit_list);

   simple_mtx_lock(&dev->submit_lock);

   assert(!fd_fence_after(fence, msm_pipe->last_enqueue_fence));

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
   while (fd_fence_before(msm_pipe->last_submit_fence, fence)) {
      pthread_cond_wait(&flush_cnd, &flush_mtx);
   }
   pthread_mutex_unlock(&flush_mtx);
}

static void
msm_submit_sp_destroy(struct fd_submit *submit)
{
   struct msm_submit_sp *msm_submit = to_msm_submit_sp(submit);

   if (msm_submit->suballoc_ring)
      fd_ringbuffer_del(msm_submit->suballoc_ring);

   _mesa_hash_table_destroy(msm_submit->bo_table, NULL);

   // TODO it would be nice to have a way to debug_assert() if all
   // rb's haven't been free'd back to the slab, because that is
   // an indication that we are leaking bo's
   slab_destroy_child(&msm_submit->ring_pool);

   for (unsigned i = 0; i < msm_submit->nr_bos; i++)
      fd_bo_del(msm_submit->bos[i]);

   free(msm_submit->bos);
   free(msm_submit);
}

static const struct fd_submit_funcs submit_funcs = {
   .new_ringbuffer = msm_submit_sp_new_ringbuffer,
   .flush = msm_submit_sp_flush,
   .destroy = msm_submit_sp_destroy,
};

struct fd_submit *
msm_submit_sp_new(struct fd_pipe *pipe)
{
   struct msm_submit_sp *msm_submit = calloc(1, sizeof(*msm_submit));
   struct fd_submit *submit;

   msm_submit->bo_table = _mesa_hash_table_create(NULL, _mesa_hash_pointer,
                                                  _mesa_key_pointer_equal);

   slab_create_child(&msm_submit->ring_pool, &to_msm_pipe(pipe)->ring_pool);

   submit = &msm_submit->base;
   submit->funcs = &submit_funcs;

   return submit;
}

void
msm_pipe_sp_ringpool_init(struct msm_pipe *msm_pipe)
{
   // TODO tune size:
   slab_create_parent(&msm_pipe->ring_pool, sizeof(struct msm_ringbuffer_sp),
                      16);
}

void
msm_pipe_sp_ringpool_fini(struct msm_pipe *msm_pipe)
{
   if (msm_pipe->ring_pool.num_elements)
      slab_destroy_parent(&msm_pipe->ring_pool);
}

static void
finalize_current_cmd(struct fd_ringbuffer *ring)
{
   debug_assert(!(ring->flags & _FD_RINGBUFFER_OBJECT));

   struct msm_ringbuffer_sp *msm_ring = to_msm_ringbuffer_sp(ring);
   APPEND(&msm_ring->u, cmds,
          (struct msm_cmd_sp){
             .ring_bo = fd_bo_ref(msm_ring->ring_bo),
             .size = offset_bytes(ring->cur, ring->start),
          });
}

static void
msm_ringbuffer_sp_grow(struct fd_ringbuffer *ring, uint32_t size)
{
   struct msm_ringbuffer_sp *msm_ring = to_msm_ringbuffer_sp(ring);
   struct fd_pipe *pipe = msm_ring->u.submit->pipe;

   debug_assert(ring->flags & FD_RINGBUFFER_GROWABLE);

   finalize_current_cmd(ring);

   fd_bo_del(msm_ring->ring_bo);
   msm_ring->ring_bo = fd_bo_new_ring(pipe->dev, size);

   ring->start = fd_bo_map(msm_ring->ring_bo);
   ring->end = &(ring->start[size / 4]);
   ring->cur = ring->start;
   ring->size = size;
}

static inline bool
msm_ringbuffer_references_bo(struct fd_ringbuffer *ring, struct fd_bo *bo)
{
   struct msm_ringbuffer_sp *msm_ring = to_msm_ringbuffer_sp(ring);

   for (int i = 0; i < msm_ring->u.nr_reloc_bos; i++) {
      if (msm_ring->u.reloc_bos[i] == bo)
         return true;
   }
   return false;
}

#define PTRSZ 64
#include "msm_ringbuffer_sp.h"
#undef PTRSZ
#define PTRSZ 32
#include "msm_ringbuffer_sp.h"
#undef PTRSZ

static uint32_t
msm_ringbuffer_sp_cmd_count(struct fd_ringbuffer *ring)
{
   if (ring->flags & FD_RINGBUFFER_GROWABLE)
      return to_msm_ringbuffer_sp(ring)->u.nr_cmds + 1;
   return 1;
}

static bool
msm_ringbuffer_sp_check_size(struct fd_ringbuffer *ring)
{
   assert(!(ring->flags & _FD_RINGBUFFER_OBJECT));
   struct msm_ringbuffer_sp *msm_ring = to_msm_ringbuffer_sp(ring);
   struct fd_submit *submit = msm_ring->u.submit;

   if (to_msm_submit_sp(submit)->nr_bos > MAX_ARRAY_SIZE/2) {
      return false;
   }

   return true;
}

static void
msm_ringbuffer_sp_destroy(struct fd_ringbuffer *ring)
{
   struct msm_ringbuffer_sp *msm_ring = to_msm_ringbuffer_sp(ring);

   fd_bo_del(msm_ring->ring_bo);

   if (ring->flags & _FD_RINGBUFFER_OBJECT) {
      for (unsigned i = 0; i < msm_ring->u.nr_reloc_bos; i++) {
         fd_bo_del(msm_ring->u.reloc_bos[i]);
      }
      free(msm_ring->u.reloc_bos);

      free(msm_ring);
   } else {
      struct fd_submit *submit = msm_ring->u.submit;

      for (unsigned i = 0; i < msm_ring->u.nr_cmds; i++) {
         fd_bo_del(msm_ring->u.cmds[i].ring_bo);
      }
      free(msm_ring->u.cmds);

      slab_free(&to_msm_submit_sp(submit)->ring_pool, msm_ring);
   }
}

static const struct fd_ringbuffer_funcs ring_funcs_nonobj_32 = {
   .grow = msm_ringbuffer_sp_grow,
   .emit_reloc = msm_ringbuffer_sp_emit_reloc_nonobj_32,
   .emit_reloc_ring = msm_ringbuffer_sp_emit_reloc_ring_32,
   .cmd_count = msm_ringbuffer_sp_cmd_count,
   .check_size = msm_ringbuffer_sp_check_size,
   .destroy = msm_ringbuffer_sp_destroy,
};

static const struct fd_ringbuffer_funcs ring_funcs_obj_32 = {
   .grow = msm_ringbuffer_sp_grow,
   .emit_reloc = msm_ringbuffer_sp_emit_reloc_obj_32,
   .emit_reloc_ring = msm_ringbuffer_sp_emit_reloc_ring_32,
   .cmd_count = msm_ringbuffer_sp_cmd_count,
   .destroy = msm_ringbuffer_sp_destroy,
};

static const struct fd_ringbuffer_funcs ring_funcs_nonobj_64 = {
   .grow = msm_ringbuffer_sp_grow,
   .emit_reloc = msm_ringbuffer_sp_emit_reloc_nonobj_64,
   .emit_reloc_ring = msm_ringbuffer_sp_emit_reloc_ring_64,
   .cmd_count = msm_ringbuffer_sp_cmd_count,
   .check_size = msm_ringbuffer_sp_check_size,
   .destroy = msm_ringbuffer_sp_destroy,
};

static const struct fd_ringbuffer_funcs ring_funcs_obj_64 = {
   .grow = msm_ringbuffer_sp_grow,
   .emit_reloc = msm_ringbuffer_sp_emit_reloc_obj_64,
   .emit_reloc_ring = msm_ringbuffer_sp_emit_reloc_ring_64,
   .cmd_count = msm_ringbuffer_sp_cmd_count,
   .destroy = msm_ringbuffer_sp_destroy,
};

static inline struct fd_ringbuffer *
msm_ringbuffer_sp_init(struct msm_ringbuffer_sp *msm_ring, uint32_t size,
                       enum fd_ringbuffer_flags flags)
{
   struct fd_ringbuffer *ring = &msm_ring->base;

   /* We don't do any translation from internal FD_RELOC flags to MSM flags. */
   STATIC_ASSERT(FD_RELOC_READ == MSM_SUBMIT_BO_READ);
   STATIC_ASSERT(FD_RELOC_WRITE == MSM_SUBMIT_BO_WRITE);
   STATIC_ASSERT(FD_RELOC_DUMP == MSM_SUBMIT_BO_DUMP);

   debug_assert(msm_ring->ring_bo);

   uint8_t *base = fd_bo_map(msm_ring->ring_bo);
   ring->start = (void *)(base + msm_ring->offset);
   ring->end = &(ring->start[size / 4]);
   ring->cur = ring->start;

   ring->size = size;
   ring->flags = flags;

   if (flags & _FD_RINGBUFFER_OBJECT) {
      if (fd_dev_64b(&msm_ring->u.pipe->dev_id)) {
         ring->funcs = &ring_funcs_obj_64;
      } else {
         ring->funcs = &ring_funcs_obj_32;
      }
   } else {
      if (fd_dev_64b(&msm_ring->u.submit->pipe->dev_id)) {
         ring->funcs = &ring_funcs_nonobj_64;
      } else {
         ring->funcs = &ring_funcs_nonobj_32;
      }
   }

   // TODO initializing these could probably be conditional on flags
   // since unneed for FD_RINGBUFFER_STAGING case..
   msm_ring->u.cmds = NULL;
   msm_ring->u.nr_cmds = msm_ring->u.max_cmds = 0;

   msm_ring->u.reloc_bos = NULL;
   msm_ring->u.nr_reloc_bos = msm_ring->u.max_reloc_bos = 0;

   return ring;
}

struct fd_ringbuffer *
msm_ringbuffer_sp_new_object(struct fd_pipe *pipe, uint32_t size)
{
   struct msm_pipe *msm_pipe = to_msm_pipe(pipe);
   struct msm_ringbuffer_sp *msm_ring = malloc(sizeof(*msm_ring));

   /* Lock access to the msm_pipe->suballoc_* since ringbuffer object allocation
    * can happen both on the frontend (most CSOs) and the driver thread (a6xx
    * cached tex state, for example)
    */
   static simple_mtx_t suballoc_lock = _SIMPLE_MTX_INITIALIZER_NP;
   simple_mtx_lock(&suballoc_lock);

   /* Maximum known alignment requirement is a6xx's TEX_CONST at 16 dwords */
   msm_ring->offset = align(msm_pipe->suballoc_offset, 64);
   if (!msm_pipe->suballoc_bo ||
       msm_ring->offset + size > fd_bo_size(msm_pipe->suballoc_bo)) {
      if (msm_pipe->suballoc_bo)
         fd_bo_del(msm_pipe->suballoc_bo);
      msm_pipe->suballoc_bo =
         fd_bo_new_ring(pipe->dev, MAX2(SUBALLOC_SIZE, align(size, 4096)));
      msm_ring->offset = 0;
   }

   msm_ring->u.pipe = pipe;
   msm_ring->ring_bo = fd_bo_ref(msm_pipe->suballoc_bo);
   msm_ring->base.refcnt = 1;

   msm_pipe->suballoc_offset = msm_ring->offset + size;

   simple_mtx_unlock(&suballoc_lock);

   return msm_ringbuffer_sp_init(msm_ring, size, _FD_RINGBUFFER_OBJECT);
}
