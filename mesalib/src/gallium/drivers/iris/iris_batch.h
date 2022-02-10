/*
 * Copyright © 2017 Intel Corporation
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef IRIS_BATCH_DOT_H
#define IRIS_BATCH_DOT_H

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "util/u_dynarray.h"
#include "util/perf/u_trace.h"

#include "drm-uapi/i915_drm.h"
#include "common/intel_decoder.h"
#include "ds/intel_driver_ds.h"
#include "ds/intel_tracepoints.h"

#include "iris_fence.h"
#include "iris_fine_fence.h"

struct iris_context;

/* The kernel assumes batchbuffers are smaller than 256kB. */
#define MAX_BATCH_SIZE (256 * 1024)

/* Terminating the batch takes either 4 bytes for MI_BATCH_BUFFER_END or 12
 * bytes for MI_BATCH_BUFFER_START (when chaining).  Plus another 24 bytes for
 * the seqno write (using PIPE_CONTROL), and another 24 bytes for the ISP
 * invalidation pipe control.
 */
#define BATCH_RESERVED 60

/* Our target batch size - flush approximately at this point. */
#define BATCH_SZ (64 * 1024 - BATCH_RESERVED)

enum iris_batch_name {
   IRIS_BATCH_RENDER,
   IRIS_BATCH_COMPUTE,
   IRIS_BATCH_BLITTER,
};

struct iris_batch {
   struct iris_context *ice;
   struct iris_screen *screen;
   struct pipe_debug_callback *dbg;
   struct pipe_device_reset_callback *reset;

   /** What batch is this? (e.g. IRIS_BATCH_RENDER/COMPUTE) */
   enum iris_batch_name name;

   /** Current batchbuffer being queued up. */
   struct iris_bo *bo;
   void *map;
   void *map_next;

   /** Size of the primary batch being submitted to execbuf (in bytes). */
   unsigned primary_batch_size;

   /** Total size of all chained batches (in bytes). */
   unsigned total_chained_batch_size;

   /** Last Surface State Base Address set in this hardware context. */
   uint64_t last_surface_base_address;

   uint32_t ctx_id;
   uint32_t exec_flags;
   bool has_engines_context;

   /** A list of all BOs referenced by this batch */
   struct iris_bo **exec_bos;
   int exec_count;
   int exec_array_size;
   /** Bitset of whether this batch writes to BO `i'. */
   BITSET_WORD *bos_written;
   uint32_t max_gem_handle;

   /** Whether INTEL_BLACKHOLE_RENDER is enabled in the batch (aka first
    * instruction is a MI_BATCH_BUFFER_END).
    */
   bool noop_enabled;

   /** Whether the first utrace point has been recorded.
    */
   bool begin_trace_recorded;

   /**
    * A list of iris_syncobjs associated with this batch.
    *
    * The first list entry will always be a signalling sync-point, indicating
    * that this batch has completed.  The others are likely to be sync-points
    * to wait on before executing the batch.
    */
   struct util_dynarray syncobjs;

   /** A list of drm_i915_exec_fences to have execbuf signal or wait on */
   struct util_dynarray exec_fences;

   /** The amount of aperture space (in bytes) used by all exec_bos */
   int aperture_space;

   struct {
      /** Uploader to use for sequence numbers */
      struct u_upload_mgr *uploader;

      /** GPU buffer and CPU map where our seqno's will be written. */
      struct iris_state_ref ref;
      uint32_t *map;

      /** The sequence number to write the next time we add a fence. */
      uint32_t next;
   } fine_fences;

   /** A seqno (and syncobj) for the last batch that was submitted. */
   struct iris_fine_fence *last_fence;

   /** List of other batches which we might need to flush to use a BO */
   struct iris_batch *other_batches[IRIS_BATCH_COUNT - 1];
   unsigned num_other_batches;

   struct {
      /**
       * Set of struct brw_bo * that have been rendered to within this
       * batchbuffer and would need flushing before being used from another
       * cache domain that isn't coherent with it (i.e. the sampler).
       */
      struct hash_table *render;
   } cache;

   struct intel_batch_decode_ctx decoder;
   struct hash_table_u64 *state_sizes;

   /**
    * Matrix representation of the cache coherency status of the GPU at the
    * current end point of the batch.  For every i and j,
    * coherent_seqnos[i][j] denotes the seqno of the most recent flush of
    * cache domain j visible to cache domain i (which obviously implies that
    * coherent_seqnos[i][i] is the most recent flush of cache domain i).  This
    * can be used to efficiently determine whether synchronization is
    * necessary before accessing data from cache domain i if it was previously
    * accessed from another cache domain j.
    */
   uint64_t coherent_seqnos[NUM_IRIS_DOMAINS][NUM_IRIS_DOMAINS];

   /**
    * Sequence number used to track the completion of any subsequent memory
    * operations in the batch until the next sync boundary.
    */
   uint64_t next_seqno;

   /** Have we emitted any draw calls to this batch? */
   bool contains_draw;

   /** Have we emitted any draw calls with next_seqno? */
   bool contains_draw_with_next_seqno;

   /** Batch contains fence signal operation. */
   bool contains_fence_signal;

   /**
    * Number of times iris_batch_sync_region_start() has been called without a
    * matching iris_batch_sync_region_end() on this batch.
    */
   uint32_t sync_region_depth;

   uint32_t last_aux_map_state;
   struct iris_measure_batch *measure;

   /** Where tracepoints are recorded */
   struct u_trace trace;

   /** Batch wrapper structure for perfetto */
   struct intel_ds_queue *ds;
};

void iris_init_batches(struct iris_context *ice, int priority);
void iris_chain_to_new_batch(struct iris_batch *batch);
void iris_destroy_batches(struct iris_context *ice);
void iris_batch_maybe_flush(struct iris_batch *batch, unsigned estimate);

void _iris_batch_flush(struct iris_batch *batch, const char *file, int line);
#define iris_batch_flush(batch) _iris_batch_flush((batch), __FILE__, __LINE__)

bool iris_batch_references(struct iris_batch *batch, struct iris_bo *bo);

bool iris_batch_prepare_noop(struct iris_batch *batch, bool noop_enable);

#define RELOC_WRITE EXEC_OBJECT_WRITE

void iris_use_pinned_bo(struct iris_batch *batch, struct iris_bo *bo,
                        bool writable, enum iris_domain access);

enum pipe_reset_status iris_batch_check_for_reset(struct iris_batch *batch);

static inline unsigned
iris_batch_bytes_used(struct iris_batch *batch)
{
   return batch->map_next - batch->map;
}

/**
 * Ensure the current command buffer has \param size bytes of space
 * remaining.  If not, this creates a secondary batch buffer and emits
 * a jump from the primary batch to the start of the secondary.
 *
 * Most callers want iris_get_command_space() instead.
 */
static inline void
iris_require_command_space(struct iris_batch *batch, unsigned size)
{
   const unsigned required_bytes = iris_batch_bytes_used(batch) + size;

   if (required_bytes >= BATCH_SZ) {
      iris_chain_to_new_batch(batch);
   }
}

/**
 * Allocate space in the current command buffer, and return a pointer
 * to the mapped area so the caller can write commands there.
 *
 * This should be called whenever emitting commands.
 */
static inline void *
iris_get_command_space(struct iris_batch *batch, unsigned bytes)
{
   if (!batch->begin_trace_recorded) {
      batch->begin_trace_recorded = true;
      trace_intel_begin_batch(&batch->trace, batch);
   }
   iris_require_command_space(batch, bytes);
   void *map = batch->map_next;
   batch->map_next += bytes;
   return map;
}

/**
 * Helper to emit GPU commands - allocates space, copies them there.
 */
static inline void
iris_batch_emit(struct iris_batch *batch, const void *data, unsigned size)
{
   void *map = iris_get_command_space(batch, size);
   memcpy(map, data, size);
}

/**
 * Get a pointer to the batch's signalling syncobj.  Does not refcount.
 */
static inline struct iris_syncobj *
iris_batch_get_signal_syncobj(struct iris_batch *batch)
{
   /* The signalling syncobj is the first one in the list. */
   struct iris_syncobj *syncobj =
      ((struct iris_syncobj **) util_dynarray_begin(&batch->syncobjs))[0];
   return syncobj;
}


/**
 * Take a reference to the batch's signalling syncobj.
 *
 * Callers can use this to wait for the the current batch under construction
 * to complete (after flushing it).
 */
static inline void
iris_batch_reference_signal_syncobj(struct iris_batch *batch,
                                   struct iris_syncobj **out_syncobj)
{
   struct iris_syncobj *syncobj = iris_batch_get_signal_syncobj(batch);
   iris_syncobj_reference(batch->screen->bufmgr, out_syncobj, syncobj);
}

/**
 * Record the size of a piece of state for use in INTEL_DEBUG=bat printing.
 */
static inline void
iris_record_state_size(struct hash_table_u64 *ht,
                       uint32_t offset_from_base,
                       uint32_t size)
{
   if (ht) {
      _mesa_hash_table_u64_insert(ht, offset_from_base,
                                  (void *)(uintptr_t) size);
   }
}

/**
 * Mark the start of a region in the batch with stable synchronization
 * sequence number.  Any buffer object accessed by the batch buffer only needs
 * to be marked once (e.g. via iris_bo_bump_seqno()) within a region delimited
 * by iris_batch_sync_region_start() and iris_batch_sync_region_end().
 */
static inline void
iris_batch_sync_region_start(struct iris_batch *batch)
{
   batch->sync_region_depth++;
}

/**
 * Mark the end of a region in the batch with stable synchronization sequence
 * number.  Should be called once after each call to
 * iris_batch_sync_region_start().
 */
static inline void
iris_batch_sync_region_end(struct iris_batch *batch)
{
   assert(batch->sync_region_depth);
   batch->sync_region_depth--;
}

/**
 * Start a new synchronization section at the current point of the batch,
 * unless disallowed by a previous iris_batch_sync_region_start().
 */
static inline void
iris_batch_sync_boundary(struct iris_batch *batch)
{
   if (!batch->sync_region_depth) {
      batch->contains_draw_with_next_seqno = false;
      batch->next_seqno = p_atomic_inc_return(&batch->screen->last_seqno);
      assert(batch->next_seqno > 0);
   }
}

/**
 * Update the cache coherency status of the batch to reflect a flush of the
 * specified caching domain.
 */
static inline void
iris_batch_mark_flush_sync(struct iris_batch *batch,
                           enum iris_domain access)
{
   batch->coherent_seqnos[access][access] = batch->next_seqno - 1;
}

/**
 * Update the cache coherency status of the batch to reflect an invalidation
 * of the specified caching domain.  All prior flushes of other caches will be
 * considered visible to the specified caching domain.
 */
static inline void
iris_batch_mark_invalidate_sync(struct iris_batch *batch,
                                enum iris_domain access)
{
   for (unsigned i = 0; i < NUM_IRIS_DOMAINS; i++)
      batch->coherent_seqnos[access][i] = batch->coherent_seqnos[i][i];
}

/**
 * Update the cache coherency status of the batch to reflect a reset.  All
 * previously accessed data can be considered visible to every caching domain
 * thanks to the kernel's heavyweight flushing at batch buffer boundaries.
 */
static inline void
iris_batch_mark_reset_sync(struct iris_batch *batch)
{
   for (unsigned i = 0; i < NUM_IRIS_DOMAINS; i++)
      for (unsigned j = 0; j < NUM_IRIS_DOMAINS; j++)
         batch->coherent_seqnos[i][j] = batch->next_seqno - 1;
}

const char *
iris_batch_name_to_string(enum iris_batch_name name);

#define iris_foreach_batch(ice, batch)                \
   for (struct iris_batch *batch = &ice->batches[0];  \
        batch <= &ice->batches[((struct iris_screen *)ice->ctx.screen)->devinfo.ver >= 12 ? IRIS_BATCH_BLITTER : IRIS_BATCH_COMPUTE]; \
        ++batch)

#endif
