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

#ifndef FREEDRENO_BATCH_H_
#define FREEDRENO_BATCH_H_

#include "util/list.h"
#include "util/set.h"
#include "util/simple_mtx.h"
#include "util/u_inlines.h"
#include "util/u_queue.h"
#include "util/perf/u_trace.h"

#include "freedreno_context.h"
#include "freedreno_fence.h"
#include "freedreno_util.h"

#ifdef __cplusplus
extern "C" {
#endif

struct fd_resource;
struct fd_batch_key;
struct fd_batch_result;

/* A batch tracks everything about a cmdstream batch/submit, including the
 * ringbuffers used for binning, draw, and gmem cmds, list of associated
 * fd_resource-s, etc.
 */
struct fd_batch {
   struct pipe_reference reference;
   unsigned seqno;
   unsigned idx; /* index into cache->batches[] */

   struct u_trace trace;

   /* To detect cases where we can skip cmdstream to record timestamp: */
   uint32_t *last_timestamp_cmd;

   int in_fence_fd;
   struct pipe_fence_handle *fence;

   struct fd_context *ctx;

   /* do we need to mem2gmem before rendering.  We don't, if for example,
    * there was a glClear() that invalidated the entire previous buffer
    * contents.  Keep track of which buffer(s) are cleared, or needs
    * restore.  Masks of PIPE_CLEAR_*
    *
    * The 'cleared' bits will be set for buffers which are *entirely*
    * cleared, and 'partial_cleared' bits will be set if you must
    * check cleared_scissor.
    *
    * The 'invalidated' bits are set for cleared buffers, and buffers
    * where the contents are undefined, ie. what we don't need to restore
    * to gmem.
    */
   enum {
      /* align bitmask values w/ PIPE_CLEAR_*.. since that is convenient.. */
      FD_BUFFER_COLOR = PIPE_CLEAR_COLOR,
      FD_BUFFER_DEPTH = PIPE_CLEAR_DEPTH,
      FD_BUFFER_STENCIL = PIPE_CLEAR_STENCIL,
      FD_BUFFER_ALL = FD_BUFFER_COLOR | FD_BUFFER_DEPTH | FD_BUFFER_STENCIL,
   } invalidated, cleared, fast_cleared, restore, resolve;

   /* is this a non-draw batch (ie compute/blit which has no pfb state)? */
   bool nondraw : 1;
   bool needs_flush : 1;
   bool flushed : 1;
   bool tessellation : 1; /* tessellation used in batch */

   /* Keep track if WAIT_FOR_IDLE is needed for registers we need
    * to update via RMW:
    */
   bool needs_wfi : 1;

   /* To decide whether to render to system memory, keep track of the
    * number of draws, and whether any of them require multisample,
    * depth_test (or depth write), stencil_test, blending, and
    * color_logic_Op (since those functions are disabled when by-
    * passing GMEM.
    */
   enum fd_gmem_reason gmem_reason;

   /* At submit time, once we've decided that this batch will use GMEM
    * rendering, the appropriate gmem state is looked up:
    */
   const struct fd_gmem_stateobj *gmem_state;

   /* A calculated "draw cost" value for the batch, which tries to
    * estimate the bandwidth-per-sample of all the draws according
    * to:
    *
    *    foreach_draw (...) {
    *      cost += num_mrt;
    *      if (blend_enabled)
    *        cost += num_mrt;
    *      if (depth_test_enabled)
    *        cost++;
    *      if (depth_write_enabled)
    *        cost++;
    *    }
    *
    * The idea is that each sample-passed minimally does one write
    * per MRT.  If blend is enabled, the hw will additionally do
    * a framebuffer read per sample-passed (for each MRT with blend
    * enabled).  If depth-test is enabled, the hw will additionally
    * a depth buffer read.  If depth-write is enable, the hw will
    * additionally do a depth buffer write.
    *
    * This does ignore depth buffer traffic for samples which do not
    * pass do to depth-test fail, and some other details.  But it is
    * just intended to be a rough estimate that is easy to calculate.
    */
   unsigned cost;

   /* Tells the gen specific backend where to write stats used for
    * the autotune module.
    *
    * Pointer only valid during gmem emit code.
    */
   struct fd_batch_result *autotune_result;

   unsigned num_draws;    /* number of draws in current batch */
   unsigned num_vertices; /* number of vertices in current batch */

   /* Currently only used on a6xx, to calculate vsc prim/draw stream
    * sizes:
    */
   unsigned num_bins_per_pipe;
   unsigned prim_strm_bits;
   unsigned draw_strm_bits;

   /* Track the maximal bounds of the scissor of all the draws within a
    * batch.  Used at the tile rendering step (fd_gmem_render_tiles(),
    * mem2gmem/gmem2mem) to avoid needlessly moving data in/out of gmem.
    */
   struct pipe_scissor_state max_scissor;

   /* Keep track of DRAW initiators that need to be patched up depending
    * on whether we using binning or not:
    */
   struct util_dynarray draw_patches;

   /* texture state that needs patching for fb_read: */
   struct util_dynarray fb_read_patches;

   /* Keep track of writes to RB_RENDER_CONTROL which need to be patched
    * once we know whether or not to use GMEM, and GMEM tile pitch.
    *
    * (only for a3xx.. but having gen specific subclasses of fd_batch
    * seemed overkill for now)
    */
   struct util_dynarray rbrc_patches;

   /* Keep track of GMEM related values that need to be patched up once we
    * know the gmem layout:
    */
   struct util_dynarray gmem_patches;

   /* Keep track of pointer to start of MEM exports for a20x binning shaders
    *
    * this is so the end of the shader can be cut off at the right point
    * depending on the GMEM configuration
    */
   struct util_dynarray shader_patches;

   struct pipe_framebuffer_state framebuffer;

   struct fd_submit *submit;

   /** draw pass cmdstream: */
   struct fd_ringbuffer *draw;
   /** binning pass cmdstream: */
   struct fd_ringbuffer *binning;
   /** tiling/gmem (IB0) cmdstream: */
   struct fd_ringbuffer *gmem;

   /** preemble cmdstream (executed once before first tile): */
   struct fd_ringbuffer *prologue;

   /** epilogue cmdstream (executed after each tile): */
   struct fd_ringbuffer *epilogue;

   struct fd_ringbuffer *tile_setup;
   struct fd_ringbuffer *tile_fini;

   union pipe_color_union clear_color[MAX_RENDER_TARGETS];
   double clear_depth;
   unsigned clear_stencil;

   /**
    * hw query related state:
    */
   /*@{*/
   /* next sample offset.. incremented for each sample in the batch/
    * submit, reset to zero on next submit.
    */
   uint32_t next_sample_offset;

   /* cached samples (in case multiple queries need to reference
    * the same sample snapshot)
    */
   struct fd_hw_sample *sample_cache[MAX_HW_SAMPLE_PROVIDERS];

   /* which sample providers were used in the current batch: */
   uint32_t query_providers_used;

   /* which sample providers are currently enabled in the batch: */
   uint32_t query_providers_active;

   /* list of samples in current batch: */
   struct util_dynarray samples;

   /* current query result bo and tile stride: */
   struct pipe_resource *query_buf;
   uint32_t query_tile_stride;
   /*@}*/

   /* Set of references to resources used by currently-unsubmitted batch (read
    * or write).  Note that this set may not include all BOs referenced by the
    * batch due to fd_bc_resource_invalidate().
    */
   struct set *resources;

   BITSET_WORD *bos;
   uint32_t bos_size;

   /** key in batch-cache (if not null): */
   struct fd_batch_key *key;
   uint32_t hash;

   /** set of dependent batches.. holds refs to dependent batches: */
   struct set *dependents;

   /* Buffer for tessellation engine input
    */
   struct fd_bo *tessfactor_bo;
   uint32_t tessfactor_size;

   /* Buffer for passing parameters between TCS and TES
    */
   struct fd_bo *tessparam_bo;
   uint32_t tessparam_size;

   struct fd_ringbuffer *tess_addrs_constobj;
};

struct fd_batch *fd_batch_create(struct fd_context *ctx, bool nondraw);

void fd_batch_reset(struct fd_batch *batch) assert_dt;
void fd_batch_flush(struct fd_batch *batch) assert_dt;
void fd_batch_add_dep(struct fd_batch *batch, struct fd_batch *dep) assert_dt;
void fd_batch_resource_write(struct fd_batch *batch,
                             struct fd_resource *rsc) assert_dt;
void fd_batch_resource_read_slowpath(struct fd_batch *batch,
                                     struct fd_resource *rsc) assert_dt;
void fd_batch_check_size(struct fd_batch *batch) assert_dt;

uint32_t fd_batch_key_hash(const void *_key);
bool fd_batch_key_equals(const void *_a, const void *_b);
struct fd_batch_key *fd_batch_key_clone(void *mem_ctx,
                                        const struct fd_batch_key *key);

/* not called directly: */
void __fd_batch_describe(char *buf, const struct fd_batch *batch) assert_dt;
void __fd_batch_destroy(struct fd_batch *batch);

static inline void
fd_batch_reference(struct fd_batch **ptr, struct fd_batch *batch)
{
   struct fd_batch *old_batch = *ptr;

   if (pipe_reference_described(
          &(*ptr)->reference, &batch->reference,
          (debug_reference_descriptor)__fd_batch_describe))
      __fd_batch_destroy(old_batch);

   *ptr = batch;
}

static inline void
fd_batch_unlock_submit(struct fd_batch *batch)
{
}

/**
 * Returns true if emit-lock was acquired, false if failed to acquire lock,
 * ie. batch already flushed.
 */
static inline bool MUST_CHECK
fd_batch_lock_submit(struct fd_batch *batch)
{
   return !batch->flushed;
}

/**
 * Mark the batch as having something worth flushing (rendering, blit, query,
 * etc)
 */
static inline void
fd_batch_needs_flush(struct fd_batch *batch)
{
   batch->needs_flush = true;
   fd_fence_ref(&batch->ctx->last_fence, NULL);
}

/* Since we reorder batches and can pause/resume queries (notably for disabling
 * queries dueing some meta operations), we update the current query state for
 * the batch before each draw.
 */
static inline void
fd_batch_update_queries(struct fd_batch *batch) assert_dt
{
   struct fd_context *ctx = batch->ctx;

   if (ctx->query_update_batch)
      ctx->query_update_batch(batch, false);
}

static inline void
fd_batch_finish_queries(struct fd_batch *batch) assert_dt
{
   struct fd_context *ctx = batch->ctx;

   if (ctx->query_update_batch)
      ctx->query_update_batch(batch, true);
}

static inline void
fd_reset_wfi(struct fd_batch *batch)
{
   batch->needs_wfi = true;
}

void fd_wfi(struct fd_batch *batch, struct fd_ringbuffer *ring) assert_dt;

/* emit a CP_EVENT_WRITE:
 */
static inline void
fd_event_write(struct fd_batch *batch, struct fd_ringbuffer *ring,
               enum vgt_event_type evt)
{
   OUT_PKT3(ring, CP_EVENT_WRITE, 1);
   OUT_RING(ring, evt);
   fd_reset_wfi(batch);
}

/* Get per-tile epilogue */
static inline struct fd_ringbuffer *
fd_batch_get_epilogue(struct fd_batch *batch)
{
   if (batch->epilogue == NULL) {
      batch->epilogue = fd_submit_new_ringbuffer(batch->submit, 0x1000,
                                                 (enum fd_ringbuffer_flags)0);
   }

   return batch->epilogue;
}

struct fd_ringbuffer *fd_batch_get_prologue(struct fd_batch *batch);

#ifdef __cplusplus
}
#endif

#endif /* FREEDRENO_BATCH_H_ */
