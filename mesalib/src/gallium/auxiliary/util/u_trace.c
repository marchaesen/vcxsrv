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

#include <inttypes.h>

#include "pipe/p_context.h"
#include "pipe/p_state.h"

#include "util/list.h"
#include "util/ralloc.h"
#include "util/u_debug.h"
#include "util/u_inlines.h"

#include "u_fifo.h"
#include "u_trace.h"

#define __NEEDS_TRACE_PRIV
#include "u_trace_priv.h"

#define TIMESTAMP_BUF_SIZE 0x1000
#define TRACES_PER_CHUNK   (TIMESTAMP_BUF_SIZE / sizeof(uint64_t))

struct u_trace_event {
   const struct u_tracepoint *tp;
   const void *payload;
};

/**
 * A "chunk" of trace-events and corresponding timestamp buffer.  As
 * trace events are emitted, additional trace chucks will be allocated
 * as needed.  When u_trace_flush() is called, they are transferred
 * from the u_trace to the u_trace_context queue.
 */
struct u_trace_chunk {
   struct list_head node;

   struct u_trace_context *utctx;

   /* The number of traces this chunk contains so far: */
   unsigned num_traces;

   /* table of trace events: */
   struct u_trace_event traces[TRACES_PER_CHUNK];

   /* table of driver recorded 64b timestamps, index matches index
    * into traces table
    */
   struct pipe_resource *timestamps;

   /**
    * For trace payload, we sub-allocate from ralloc'd buffers which
    * hang off of the chunk's ralloc context, so they are automatically
    * free'd when the chunk is free'd
    */
   uint8_t *payload_buf, *payload_end;

   struct util_queue_fence fence;

   bool last;          /* this chunk is last in batch */
   bool eof;           /* this chunk is last in frame */
};

static void
free_chunk(void *ptr)
{
   struct u_trace_chunk *chunk = ptr;

   pipe_resource_reference(&chunk->timestamps, NULL);

   list_del(&chunk->node);
}

static void
free_chunks(struct list_head *chunks)
{
   while (!list_is_empty(chunks)) {
      struct u_trace_chunk *chunk = list_first_entry(chunks,
            struct u_trace_chunk, node);
      ralloc_free(chunk);
   }
}

static struct u_trace_chunk *
get_chunk(struct u_trace *ut)
{
   struct u_trace_chunk *chunk;

   /* do we currently have a non-full chunk to append msgs to? */
   if (!list_is_empty(&ut->trace_chunks)) {
           chunk = list_last_entry(&ut->trace_chunks,
                           struct u_trace_chunk, node);
           if (chunk->num_traces < TRACES_PER_CHUNK)
                   return chunk;
           /* we need to expand to add another chunk to the batch, so
            * the current one is no longer the last one of the batch:
            */
           chunk->last = false;
   }

   /* .. if not, then create a new one: */
   chunk = rzalloc_size(NULL, sizeof(*chunk));
   ralloc_set_destructor(chunk, free_chunk);

   chunk->utctx = ut->utctx;

   struct pipe_resource tmpl = {
         .target     = PIPE_BUFFER,
         .format     = PIPE_FORMAT_R8_UNORM,
         .bind       = PIPE_BIND_QUERY_BUFFER | PIPE_BIND_LINEAR,
         .width0     = TIMESTAMP_BUF_SIZE,
         .height0    = 1,
         .depth0     = 1,
         .array_size = 1,
   };

   struct pipe_screen *pscreen = ut->utctx->pctx->screen;
   chunk->timestamps = pscreen->resource_create(pscreen, &tmpl);

   chunk->last = true;

   list_addtail(&chunk->node, &ut->trace_chunks);

   return chunk;
}

DEBUG_GET_ONCE_BOOL_OPTION(trace, "GALLIUM_GPU_TRACE", false)
DEBUG_GET_ONCE_FILE_OPTION(trace_file, "GALLIUM_GPU_TRACEFILE", NULL, "w")

static FILE *
get_tracefile(void)
{
   static FILE *tracefile = NULL;
   static bool firsttime = true;

   if (firsttime) {
      tracefile = debug_get_option_trace_file();
      if (!tracefile && debug_get_option_trace()) {
         tracefile = stdout;
      }

      firsttime = false;
   }

   return tracefile;
}

void
u_trace_context_init(struct u_trace_context *utctx,
      struct pipe_context *pctx,
      u_trace_record_ts record_timestamp,
      u_trace_read_ts   read_timestamp)
{
   utctx->pctx = pctx;
   utctx->record_timestamp = record_timestamp;
   utctx->read_timestamp   = read_timestamp;

   utctx->last_time_ns = 0;
   utctx->first_time_ns = 0;
   utctx->frame_nr = 0;

   list_inithead(&utctx->flushed_trace_chunks);

   utctx->out = get_tracefile();

   if (!utctx->out)
      return;

   bool ret = util_queue_init(&utctx->queue, "traceq", 256, 1,
         UTIL_QUEUE_INIT_USE_MINIMUM_PRIORITY |
         UTIL_QUEUE_INIT_RESIZE_IF_FULL);
   assert(ret);

   if (!ret)
      utctx->out = NULL;
}

void
u_trace_context_fini(struct u_trace_context *utctx)
{
   if (!utctx->out)
      return;
   util_queue_finish(&utctx->queue);
   util_queue_destroy(&utctx->queue);
   fflush(utctx->out);
   free_chunks(&utctx->flushed_trace_chunks);
}

static void
process_chunk(void *job, int thread_index)
{
   struct u_trace_chunk *chunk = job;
   struct u_trace_context *utctx = chunk->utctx;

   /* For first chunk of batch, accumulated times will be zerod: */
   if (!utctx->last_time_ns) {
      fprintf(utctx->out, "+----- NS -----+ +-- Δ --+  +----- MSG -----\n");
   }

   for (unsigned idx = 0; idx < chunk->num_traces; idx++) {
      const struct u_trace_event *evt = &chunk->traces[idx];

      uint64_t ns = utctx->read_timestamp(utctx, chunk->timestamps, idx);
      int32_t delta;

      if (!utctx->first_time_ns)
         utctx->first_time_ns = ns;

      if (ns != U_TRACE_NO_TIMESTAMP) {
         delta = utctx->last_time_ns ? ns - utctx->last_time_ns : 0;
         utctx->last_time_ns = ns;
      } else {
         /* we skipped recording the timestamp, so it should be
          * the same as last msg:
          */
         ns = utctx->last_time_ns;
         delta = 0;
      }

      if (evt->tp->print) {
         fprintf(utctx->out, "%016"PRIu64" %+9d: %s: ", ns, delta, evt->tp->name);
         evt->tp->print(utctx->out, evt->payload);
      } else {
         fprintf(utctx->out, "%016"PRIu64" %+9d: %s\n", ns, delta, evt->tp->name);
      }
   }

   if (chunk->last) {
      uint64_t elapsed = utctx->last_time_ns - utctx->first_time_ns;
      fprintf(utctx->out, "ELAPSED: %"PRIu64" ns\n", elapsed);

      utctx->last_time_ns = 0;
      utctx->first_time_ns = 0;
   }

   if (chunk->eof) {
      fprintf(utctx->out, "END OF FRAME %u\n", utctx->frame_nr++);
   }
}

static void
cleanup_chunk(void *job, int thread_index)
{
   ralloc_free(job);
}

void
u_trace_context_process(struct u_trace_context *utctx, bool eof)
{
   struct list_head *chunks = &utctx->flushed_trace_chunks;

   if (list_is_empty(chunks))
      return;

   struct u_trace_chunk *last_chunk = list_last_entry(chunks,
            struct u_trace_chunk, node);
   last_chunk->eof = eof;

   while (!list_is_empty(chunks)) {
      struct u_trace_chunk *chunk = list_first_entry(chunks,
            struct u_trace_chunk, node);

      /* remove from list before enqueuing, because chunk is freed
       * once it is processed by the queue:
       */
      list_delinit(&chunk->node);

      util_queue_add_job(&utctx->queue, chunk, &chunk->fence,
            process_chunk, cleanup_chunk,
            TIMESTAMP_BUF_SIZE);
   }
}


void
u_trace_init(struct u_trace *ut, struct u_trace_context *utctx)
{
   ut->utctx = utctx;
   list_inithead(&ut->trace_chunks);
   ut->enabled = !!utctx->out;
}

void
u_trace_fini(struct u_trace *ut)
{
   /* Normally the list of trace-chunks would be empty, if they
    * have been flushed to the trace-context.
    */
   free_chunks(&ut->trace_chunks);
}

/**
 * Append a trace event, returning pointer to buffer of tp->payload_sz
 * to be filled in with trace payload.  Called by generated tracepoint
 * functions.
 */
void *
u_trace_append(struct u_trace *ut, const struct u_tracepoint *tp)
{
   struct u_trace_chunk *chunk = get_chunk(ut);

   assert(tp->payload_sz == ALIGN_NPOT(tp->payload_sz, 8));

   if (unlikely((chunk->payload_buf + tp->payload_sz) > chunk->payload_end)) {
      const unsigned payload_chunk_sz = 0x100;  /* TODO arbitrary size? */

      assert(tp->payload_sz < payload_chunk_sz);

      chunk->payload_buf = ralloc_size(chunk, payload_chunk_sz);
      chunk->payload_end = chunk->payload_buf + payload_chunk_sz;
   }

   /* sub-allocate storage for trace payload: */
   void *payload = chunk->payload_buf;
   chunk->payload_buf += tp->payload_sz;

   /* record a timestamp for the trace: */
   ut->utctx->record_timestamp(ut, chunk->timestamps, chunk->num_traces);

   chunk->traces[chunk->num_traces] = (struct u_trace_event) {
         .tp = tp,
         .payload = payload,
   };

   chunk->num_traces++;

   return payload;
}

void
u_trace_flush(struct u_trace *ut)
{
   /* transfer batch's log chunks to context: */
   list_splicetail(&ut->trace_chunks, &ut->utctx->flushed_trace_chunks);
   list_inithead(&ut->trace_chunks);
}
