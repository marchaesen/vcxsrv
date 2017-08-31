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
 *
 * Create calls causing a sync that can't be async due to driver limitations:
 * - create_stream_output_target
 *
 *
 * Transfer_map rules for buffer mappings
 * --------------------------------------
 *
 * 1) If transfer_map has PIPE_TRANSFER_UNSYNCHRONIZED, the call is made
 *    in the non-driver thread without flushing the queue. The driver will
 *    receive TC_TRANSFER_MAP_THREADED_UNSYNC in addition to PIPE_TRANSFER_-
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
 * There is a multithreaded queue consisting of batches, each batch consisting
 * of call slots. Each call slot consists of an 8-byte header (call ID +
 * call size + constant 32-bit marker for integrity checking) and an 8-byte
 * body for per-call data. That is 16 bytes per call slot.
 *
 * Simple calls such as bind_xx_state(CSO) occupy only one call slot. Bigger
 * calls occupy multiple call slots depending on the size needed by call
 * parameters. That means that calls can have a variable size in the batch.
 * For example, set_vertex_buffers(count = any, buffers = NULL) occupies only
 * 1 call slot, but set_vertex_buffers(count = 5) occupies 6 call slots.
 * Even though the first call slot can use only 8 bytes for data, additional
 * call slots used by the same call can use all 16 bytes for data.
 * For example, a call using 2 call slots has 24 bytes of space for data.
 *
 * Once a batch is full and there is no space for the next call, it's flushed,
 * meaning that it's added to the queue for execution in the other thread.
 * The batches are ordered in a ring and reused once they are idle again.
 * The batching is necessary for low queue/mutex overhead.
 *
 */

#ifndef U_THREADED_CONTEXT_H
#define U_THREADED_CONTEXT_H

#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/u_queue.h"
#include "util/u_range.h"
#include "util/slab.h"

/* These are transfer flags sent to drivers. */
/* Never infer whether it's safe to use unsychronized mappings: */
#define TC_TRANSFER_MAP_NO_INFER_UNSYNCHRONIZED (1u << 29)
/* Don't invalidate buffers: */
#define TC_TRANSFER_MAP_NO_INVALIDATE        (1u << 30)
/* transfer_map is called from a non-driver thread: */
#define TC_TRANSFER_MAP_THREADED_UNSYNC      (1u << 31)

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
#define TC_CALLS_PER_BATCH    192

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

   /* If "this" is not the base instance of the buffer, but it's one of its
    * reallocations (set in "latest" of the base instance), this points to
    * the valid range of the base instance. It's used for transfers after
    * a buffer invalidation, because such transfers operate on "latest", not
    * the base instance. Initially it's set to &valid_buffer_range.
    */
   struct util_range *base_valid_buffer_range;

   /* Drivers are required to update this for shared resources and user
    * pointers. */
   bool	is_shared;
   bool is_user_ptr;
};

struct threaded_transfer {
   struct pipe_transfer b;

   /* Staging buffer for DISCARD_RANGE transfers. */
   struct pipe_resource *staging;

   /* Offset into the staging buffer, because the backing buffer is
    * sub-allocated. */
   unsigned offset;
};

struct threaded_query {
   /* The query is added to the list in end_query and removed in flush. */
   struct list_head head_unflushed;

   /* Whether pipe->flush has been called after end_query. */
   bool flushed;
};

/* This is the second half of tc_call containing call data.
 * Most calls will typecast this to the type they need, typically larger
 * than 8 bytes.
 */
union tc_payload {
   struct pipe_query *query;
   struct pipe_resource *resource;
   struct pipe_transfer *transfer;
   uint64_t handle;
};

#ifdef _MSC_VER
#define ALIGN16 __declspec(align(16))
#else
#define ALIGN16 __attribute__((aligned(16)))
#endif

/* Each call slot should be aligned to its own size for optimal cache usage. */
struct ALIGN16 tc_call {
   unsigned sentinel;
   ushort num_call_slots;
   ushort call_id;
   union tc_payload payload;
};

struct tc_batch {
   struct pipe_context *pipe;
   unsigned sentinel;
   unsigned num_total_call_slots;
   struct util_queue_fence fence;
   struct tc_call call[TC_CALLS_PER_BATCH];
};

struct threaded_context {
   struct pipe_context base;
   struct pipe_context *pipe;
   struct slab_child_pool pool_transfers;
   tc_replace_buffer_storage_func replace_buffer_storage;
   unsigned map_buffer_alignment;

   struct list_head unflushed_queries;

   /* Counters for the HUD. */
   unsigned num_offloaded_slots;
   unsigned num_direct_slots;
   unsigned num_syncs;

   struct util_queue queue;
   struct util_queue_fence *fence;

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
                        struct threaded_context **out);

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

#endif
