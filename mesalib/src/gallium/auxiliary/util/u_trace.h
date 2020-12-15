/*
 * Copyright © 2020 Google, Inc.
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
 */

#ifndef _U_TRACE_H
#define _U_TRACE_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "util/u_queue.h"

/* A trace mechanism (very) loosely inspired by the linux kernel tracepoint
 * mechanism, in that it allows for defining driver specific (or common)
 * tracepoints, which generate 'trace_$name()' functions that can be
 * called at various points in commandstream emit.
 *
 * Currently a printf backend is implemented, but the expectation is to
 * also implement a perfetto backend for shipping out traces to a tool like
 * AGI.
 *
 * Notable differences:
 *
 *  - GPU timestamps!  A driver provided callback is used to emit timestamps
 *    to a buffer.  At a later point in time (when stalling to wait for the
 *    GPU is not required), the timestamps are re-united with the trace
 *    payload.  This makes the trace mechanism suitable for profiling.
 *
 *  - Instead of a systemwide trace ringbuffer, buffering of un-retired
 *    tracepoints is split into two stages.  Traces are emitted to a
 *    'u_trace' instance, and at a later time flushed to a 'u_trace_context'
 *    instance.  This avoids the requirement that commandstream containing
 *    tracepoints is emitted in the same order as it is generated.
 *
 *    If the hw has multiple parallel "engines" (for example, 3d/blit/compute)
 *    then a `u_trace_context` per-engine should be used.
 *
 *  - Unlike kernel tracepoints, u_trace tracepoints are defined in py
 *    from which header and src files are generated.  Since we already have
 *    a build dependency on python+mako, this gives more flexibility than
 *    clunky preprocessor macro magic.
 *
 */

struct u_trace_context;
struct u_trace;
struct u_trace_chunk;

struct pipe_resource;

/**
 * Special reserved value to indicate that no timestamp was captured,
 * and that the timestamp of the previous trace should be reused.
 */
#define U_TRACE_NO_TIMESTAMP ((uint64_t)0)

/**
 * Driver provided callback to emit commands to capture a 64b timestamp
 * into the specified timestamps buffer, at the specified index.
 *
 * The hw counter that the driver records should be something that runs at
 * a fixed rate, even as the GPU freq changes.  The same source used for
 * GL_TIMESTAMP queries should be appropriate.
 */
typedef void (*u_trace_record_ts)(struct u_trace *ut,
      struct pipe_resource *timestamps, unsigned idx);

/**
 * Driver provided callback to read back a previously recorded timestamp.
 * If necessary, this should block until the GPU has finished writing back
 * the timestamps.  (The timestamps will be read back in order, so it is
 * safe to only synchronize on idx==0.)
 *
 * The returned timestamp should be in units of nanoseconds.  The same
 * timebase as GL_TIMESTAMP queries should be used.
 *
 * The driver can return the special U_TRACE_NO_TIMESTAMP value to indicate
 * that no timestamp was captured and the timestamp from the previous trace
 * will be re-used.  (The first trace in the u_trace buf may not do this.)
 * This allows the driver to detect cases where multiple tracepoints are
 * emitted with no other intervening cmdstream, to avoid pointlessly
 * capturing the same timestamp multiple times in a row.
 */
typedef uint64_t (*u_trace_read_ts)(struct u_trace_context *utctx,
      struct pipe_resource *timestamps, unsigned idx);

/**
 * The trace context provides tracking for "in-flight" traces, once the
 * cmdstream that records timestamps has been flushed.
 */
struct u_trace_context {
   struct pipe_context      *pctx;
   u_trace_record_ts         record_timestamp;
   u_trace_read_ts           read_timestamp;

   FILE *out;

   /* Once u_trace_flush() is called u_trace_chunk's are queued up to
    * render tracepoints on a queue.  The per-chunk queue jobs block until
    * timestamps are available.
    */
   struct util_queue queue;

   /* State to accumulate time across N chunks associated with a single
    * batch (u_trace).
    */
   uint64_t last_time_ns;
   uint64_t first_time_ns;

   uint32_t frame_nr;

   /* list of unprocessed trace chunks in fifo order: */
   struct list_head flushed_trace_chunks;
};

/**
 * The u_trace ptr is passed as the first arg to generated tracepoints.
 * It provides buffering for tracepoint payload until the corresponding
 * driver cmdstream containing the emitted commands to capture is
 * flushed.
 *
 * Individual tracepoints emitted to u_trace are expected to be "executed"
 * (ie. timestamp captured) in FIFO order with respect to other tracepoints
 * emitted to the same u_trace.  But the order WRT other u_trace instances
 * is undefined util u_trace_flush().
 */
struct u_trace {
   struct u_trace_context *utctx;

   struct list_head trace_chunks;  /* list of unflushed trace chunks in fifo order */

   bool enabled;
};

void u_trace_context_init(struct u_trace_context *utctx,
      struct pipe_context *pctx,
      u_trace_record_ts record_timestamp,
      u_trace_read_ts   read_timestamp);
void u_trace_context_fini(struct u_trace_context *utctx);

/**
 * Flush (trigger processing) of traces previously flushed to the trace-context
 * by u_trace_flush().
 *
 * This should typically be called in the driver's pctx->flush().
 */
void u_trace_context_process(struct u_trace_context *utctx, bool eof);

void u_trace_init(struct u_trace *ut, struct u_trace_context *utctx);
void u_trace_fini(struct u_trace *ut);

/**
 * Flush traces to the parent trace-context.  At this point, the expectation
 * is that all the tracepoints are "executed" by the GPU following any previously
 * flushed u_trace batch.
 *
 * This should typically be called when the corresponding cmdstream (containing
 * the timestamp reads) is flushed to the kernel.
 */
void u_trace_flush(struct u_trace *ut);

/*
 * TODO in some cases it is useful to have composite tracepoints like this,
 * to log more complex data structures.. but this is probably not where they
 * should live:
 */

void __trace_surface(struct u_trace *ut, const struct pipe_surface *psurf);
void __trace_framebuffer(struct u_trace *ut, const struct pipe_framebuffer_state *pfb);

static inline void
trace_framebuffer_state(struct u_trace *ut, const struct pipe_framebuffer_state *pfb)
{
   if (likely(!ut->enabled))
      return;

   __trace_framebuffer(ut, pfb);
   for (unsigned i = 0; i < pfb->nr_cbufs; i++) {
      if (pfb->cbufs[i]) {
         __trace_surface(ut, pfb->cbufs[i]);
      }
   }
   if (pfb->zsbuf) {
      __trace_surface(ut, pfb->zsbuf);
   }
}

#endif  /* _U_TRACE_H */
