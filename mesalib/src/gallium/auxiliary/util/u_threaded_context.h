/**************************************************************************
 *
 * Copyright 2017 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

/* This is a wrapper for pipe_context that executes all pipe_context calls
 * in another thread.
 *
 *
 * Guidelines for adopters and deviations from Gallium
 * ---------------------------------------------------
 *
 * 1) pipe_context is wrapped. pipe_screen isn't wrapped. All pipe_screen
 *    driver functions that take a context (fence_finish, texture_get_handle)
 *    should manually unwrap pipe_context by doing:
 *      pipe = threaded_context_unwrap_sync(pipe);
 *
 *    pipe_context::priv is used to unwrap the context, so drivers and state
 *    trackers shouldn't use it.
 *
 *    No other objects are wrapped.
 *
 * 2) Drivers must subclass and initialize these structures:
 *    - threaded_resource for pipe_resource (use threaded_resource_init/deinit)
 *    - threaded_query for pipe_query (zero memory)
 *    - threaded_transfer for pipe_transfer (zero memory)
 *
 * 3) The threaded context must not be enabled for contexts that can use video
 *    codecs.
 *
 * 4) Changes in driver behavior:
 *    - begin_query and end_query always return true; return values from
 *      the driver are ignored.
 *    - generate_mipmap uses is_format_supported to determine success;
 *      the return value from the driver is ignored.
 *    - resource_commit always returns true; failures are ignored.
 *    - set_debug_callback is skipped if the callback is synchronous.
 *
 *
 * Thread-safety requirements on context functions
 * -----------------------------------------------
 *
 * These pipe_context functions are executed directly, so they shouldn't use
 * pipe_context in an unsafe way. They are de-facto screen functions now:
 * - create_query
 * - create_batch_query
 * - create_*_state (all CSOs and shaders)
 *     - Make sure the shader compiler doesn't use any per-context stuff.
 *       (e.g. LLVM target machine)
 *     - Only pipe_context's debug callback for shader dumps is guaranteed to
 *       be up to date, because set_debug_callback synchronizes execution.
 * - create_surface
 * - surface_destroy
 * - create_sampler_view
 * - sampler_view_destroy
 * - stream_output_target_destroy
 * - transfer_map (only unsychronized buffer mappings)
 * - get_query_result (when threaded_query::flushed == true)
 * - create_stream_output_target
 *
 *
 * Transfer_map rules for buffer mappings
 * --------------------------------------
 *
 * 1) If transfer_map has PIPE_MAP_UNSYNCHRONIZED, the call is made
 *    in the non-driver thread without flushing the queue. The driver will
 *    receive TC_TRANSFER_MAP_THREADED_UNSYNC in addition to PIPE_MAP_-
 *    UNSYNCHRONIZED to indicate this.
 *    Note that transfer_unmap is always enqueued and called from the driver
 *    thread.
 *
 * 2) The driver isn't allowed to infer unsychronized mappings by tracking
 *    the valid buffer range. The threaded context always sends TC_TRANSFER_-
 *    MAP_NO_INFER_UNSYNCHRONIZED to indicate this. Ignoring the flag will lead
 *    to failures.
 *    The threaded context does its own detection of unsynchronized mappings.
 *
 * 3) The driver isn't allowed to do buffer invalidations by itself under any
 *    circumstances. This is necessary for unsychronized maps to map the latest
 *    version of the buffer. (because invalidations can be queued, while
 *    unsychronized maps are not queued and they should return the latest
 *    storage after invalidation). The threaded context always sends
 *    TC_TRANSFER_MAP_NO_INVALIDATE into transfer_map and buffer_subdata to
 *    indicate this. Ignoring the flag will lead to failures.
 *    The threaded context uses its own buffer invalidation mechanism.
 *
 * 4) PIPE_MAP_ONCE can no longer be used to infer that a buffer will not be mapped
 *    a second time before it is unmapped.
 *
 *
 * Rules for fences
 * ----------------
 *
 * Flushes will be executed asynchronously in the driver thread if a
 * create_fence callback is provided. This affects fence semantics as follows.
 *
 * When the threaded context wants to perform an asynchronous flush, it will
 * use the create_fence callback to pre-create the fence from the calling
 * thread. This pre-created fence will be passed to pipe_context::flush
 * together with the TC_FLUSH_ASYNC flag.
 *
 * The callback receives the unwrapped context as a parameter, but must use it
 * in a thread-safe way because it is called from a non-driver thread.
 *
 * If the threaded_context does not immediately flush the current batch, the
 * callback also receives a tc_unflushed_batch_token. If fence_finish is called
 * on the returned fence in the context that created the fence,
 * threaded_context_flush must be called.
 *
 * The driver must implement pipe_context::fence_server_sync properly, since
 * the threaded context handles PIPE_FLUSH_ASYNC.
 *
 *
 * Additional requirements
 * -----------------------
 *
 * get_query_result:
 *    If threaded_query::flushed == true, get_query_result should assume that
 *    it's called from a non-driver thread, in which case the driver shouldn't
 *    use the context in an unsafe way.
 *
 * replace_buffer_storage:
 *    The driver has to implement this callback, which will be called when
 *    the threaded context wants to replace a resource's backing storage with
 *    another resource's backing storage. The threaded context uses it to
 *    implement buffer invalidation. This call is always queued.
 *
 *
 * Performance gotchas
 * -------------------
 *
 * Buffer invalidations are done unconditionally - they don't check whether
 * the buffer is busy. This can cause drivers to have more live allocations
 * and CPU mappings than necessary.
 *
 *
 * How it works (queue architecture)
 * ---------------------------------
 *
 * There is a multithreaded queue consisting of batches, each batch containing
 * 8-byte slots. Calls can occupy 1 or more slots.
 *
 * Once a batch is full and there is no space for the next call, it's flushed,
 * meaning that it's added to the queue for execution in the other thread.
 * The batches are ordered in a ring and reused once they are idle again.
 * The batching is necessary for low queue/mutex overhead.
 */

#ifndef U_THREADED_CONTEXT_H
#define U_THREADED_CONTEXT_H

#include "c11/threads.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/u_inlines.h"
#include "util/u_queue.h"
#include "util/u_range.h"
#include "util/u_thread.h"
#include "util/slab.h"

struct threaded_context;
struct tc_unflushed_batch_token;

/* 0 = disabled, 1 = assertions, 2 = printfs */
#define TC_DEBUG 0

/* These are map flags sent to drivers. */
/* Never infer whether it's safe to use unsychronized mappings: */
#define TC_TRANSFER_MAP_NO_INFER_UNSYNCHRONIZED (1u << 29)
/* Don't invalidate buffers: */
#define TC_TRANSFER_MAP_NO_INVALIDATE        (1u << 30)
/* transfer_map is called from a non-driver thread: */
#define TC_TRANSFER_MAP_THREADED_UNSYNC      (1u << 31)

/* Custom flush flags sent to drivers. */
/* fence is pre-populated with a fence created by the create_fence callback */
#define TC_FLUSH_ASYNC        (1u << 31)

/* Size of the queue = number of batch slots in memory.
 * - 1 batch is always idle and records new commands
 * - 1 batch is being executed
 * so the queue size is TC_MAX_BATCHES - 2 = number of waiting batches.
 *
 * Use a size as small as possible for low CPU L2 cache usage but large enough
 * so that the queue isn't stalled too often for not having enough idle batch
 * slots.
 */
#define TC_MAX_BATCHES        10

/* The size of one batch. Non-trivial calls (i.e. not setting a CSO pointer)
 * can occupy multiple call slots.
 *
 * The idea is to have batches as small as possible but large enough so that
 * the queuing and mutex overhead is negligible.
 */
#define TC_SLOTS_PER_BATCH    1536

/* Threshold for when to use the queue or sync. */
#define TC_MAX_STRING_MARKER_BYTES  512

/* Threshold for when to enqueue buffer/texture_subdata as-is.
 * If the upload size is greater than this, it will do instead:
 * - for buffers: DISCARD_RANGE is done by the threaded context
 * - for textures: sync and call the driver directly
 */
#define TC_MAX_SUBDATA_BYTES        320

typedef void (*tc_replace_buffer_storage_func)(struct pipe_context *ctx,
                                               struct pipe_resource *dst,
                                               struct pipe_resource *src);
typedef struct pipe_fence_handle *(*tc_create_fence_func)(struct pipe_context *ctx,
                                                          struct tc_unflushed_batch_token *token);

struct threaded_resource {
   struct pipe_resource b;
   const struct u_resource_vtbl *vtbl;

   /* Since buffer invalidations are queued, we can't use the base resource
    * for unsychronized mappings. This points to the latest version of
    * the buffer after the latest invalidation. It's only used for unsychro-
    * nized mappings in the non-driver thread. Initially it's set to &b.
    */
   struct pipe_resource *latest;

   /* The buffer range which is initialized (with a write transfer, streamout,
    * or writable shader resources). The remainder of the buffer is considered
    * invalid and can be mapped unsynchronized.
    *
    * This allows unsychronized mapping of a buffer range which hasn't been
    * used yet. It's for applications which forget to use the unsynchronized
    * map flag and expect the driver to figure it out.
    *
    * Drivers should set this to the full range for buffers backed by user
    * memory.
    */
   struct util_range valid_buffer_range;

   /* Drivers are required to update this for shared resources and user
    * pointers. */
   bool	is_shared;
   bool is_user_ptr;

   /* If positive, prefer DISCARD_RANGE with a staging buffer over any other
    * method of CPU access when map flags allow it. Useful for buffers that
    * are too large for the visible VRAM window.
    */
   int max_forced_staging_uploads;

   /* If positive, then a staging transfer is in progress.
    */
   int pending_staging_uploads;

   /* If staging uploads are pending, this will hold the union of the mapped
    * ranges.
    */
   struct util_range pending_staging_uploads_range;
};

struct threaded_transfer {
   struct pipe_transfer b;

   /* Staging buffer for DISCARD_RANGE transfers. */
   struct pipe_resource *staging;

   /* If b.resource is not the base instance of the buffer, but it's one of its
    * reallocations (set in "latest" of the base instance), this points to
    * the valid range of the base instance. It's used for transfers after
    * a buffer invalidation, because such transfers operate on "latest", not
    * the base instance. Initially it's set to &b.resource->valid_buffer_range.
    */
   struct util_range *valid_buffer_range;
};

struct threaded_query {
   /* The query is added to the list in end_query and removed in flush. */
   struct list_head head_unflushed;

   /* Whether pipe->flush has been called in non-deferred mode after end_query. */
   bool flushed;
};

struct tc_call_base {
#if !defined(NDEBUG) && TC_DEBUG >= 1
   uint32_t sentinel;
#endif
   ushort num_slots;
   ushort call_id;
};

/**
 * A token representing an unflushed batch.
 *
 * See the general rules for fences for an explanation.
 */
struct tc_unflushed_batch_token {
   struct pipe_reference ref;
   struct threaded_context *tc;
};

struct tc_batch {
   struct threaded_context *tc;
#if !defined(NDEBUG) && TC_DEBUG >= 1
   unsigned sentinel;
#endif
   unsigned num_total_slots;
   struct util_queue_fence fence;
   struct tc_unflushed_batch_token *token;
   uint64_t slots[TC_SLOTS_PER_BATCH];
};

struct threaded_context {
   struct pipe_context base;
   struct pipe_context *pipe;
   struct slab_child_pool pool_transfers;
   tc_replace_buffer_storage_func replace_buffer_storage;
   tc_create_fence_func create_fence;
   unsigned map_buffer_alignment;
   unsigned ubo_alignment;

   struct list_head unflushed_queries;

   /* Counters for the HUD. */
   unsigned num_offloaded_slots;
   unsigned num_direct_slots;
   unsigned num_syncs;

   bool use_forced_staging_uploads;

   /* Estimation of how much vram/gtt bytes are mmap'd in
    * the current tc_batch.
    */
   uint64_t bytes_mapped_estimate;
   uint64_t bytes_mapped_limit;

   struct util_queue queue;
   struct util_queue_fence *fence;

#ifndef NDEBUG
   /**
    * The driver thread is normally the queue thread, but
    * there are cases where the queue is flushed directly
    * from the frontend thread
    */
   thread_id driver_thread;
#endif

   unsigned last, next;
   struct tc_batch batch_slots[TC_MAX_BATCHES];
};

void threaded_resource_init(struct pipe_resource *res);
void threaded_resource_deinit(struct pipe_resource *res);
struct pipe_context *threaded_context_unwrap_sync(struct pipe_context *pipe);

struct pipe_context *
threaded_context_create(struct pipe_context *pipe,
                        struct slab_parent_pool *parent_transfer_pool,
                        tc_replace_buffer_storage_func replace_buffer,
                        tc_create_fence_func create_fence,
                        struct threaded_context **out);

void
threaded_context_flush(struct pipe_context *_pipe,
                       struct tc_unflushed_batch_token *token,
                       bool prefer_async);

void
tc_draw_vbo(struct pipe_context *_pipe, const struct pipe_draw_info *info,
            unsigned drawid_offset,
            const struct pipe_draw_indirect_info *indirect,
            const struct pipe_draw_start_count_bias *draws,
            unsigned num_draws);

static inline struct threaded_context *
threaded_context(struct pipe_context *pipe)
{
   return (struct threaded_context*)pipe;
}

static inline struct threaded_resource *
threaded_resource(struct pipe_resource *res)
{
   return (struct threaded_resource*)res;
}

static inline struct threaded_query *
threaded_query(struct pipe_query *q)
{
   return (struct threaded_query*)q;
}

static inline struct threaded_transfer *
threaded_transfer(struct pipe_transfer *transfer)
{
   return (struct threaded_transfer*)transfer;
}

static inline void
tc_unflushed_batch_token_reference(struct tc_unflushed_batch_token **dst,
                                   struct tc_unflushed_batch_token *src)
{
   if (pipe_reference((struct pipe_reference *)*dst, (struct pipe_reference *)src))
      free(*dst);
   *dst = src;
}

/**
 * Helper for !NDEBUG builds to assert that it is called from driver
 * thread.  This is to help drivers ensure that various code-paths
 * are not hit indirectly from pipe entry points that are called from
 * front-end/state-tracker thread.
 */
static inline void
tc_assert_driver_thread(struct threaded_context *tc)
{
   if (!tc)
      return;
#ifndef NDEBUG
   assert(util_thread_id_equal(tc->driver_thread, util_get_thread_id()));
#endif
}

#endif
