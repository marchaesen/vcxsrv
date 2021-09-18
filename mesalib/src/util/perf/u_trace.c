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

#include "util/list.h"
#include "util/ralloc.h"
#include "util/u_debug.h"
#include "util/u_inlines.h"
#include "util/u_fifo.h"

#include "u_trace.h"

#define __NEEDS_TRACE_PRIV
#include "u_trace_priv.h"

#define TIMESTAMP_BUF_SIZE 0x1000
#define TRACES_PER_CHUNK   (TIMESTAMP_BUF_SIZE / sizeof(uint64_t))

#ifdef HAVE_PERFETTO
int ut_perfetto_enabled;

/**
 * Global list of contexts, so we can defer starting the queue until
 * perfetto tracing is started.
 *
 * TODO locking
 */
struct list_head ctx_list = { &ctx_list, &ctx_list };
#endif

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
   void *timestamps;

   /**
    * For trace payload, we sub-allocate from ralloc'd buffers which
    * hang off of the chunk's ralloc context, so they are automatically
    * free'd when the chunk is free'd
    */
   uint8_t *payload_buf, *payload_end;

   struct util_queue_fence fence;

   bool last;          /* this chunk is last in batch */
   bool eof;           /* this chunk is last in frame */

   void *flush_data; /* assigned by u_trace_flush */

   /**
    * Several chunks reference a single flush_data instance thus only
    * one chunk should be designated to free the data.
    */
   bool free_flush_data;
};

static void
free_chunk(void *ptr)
{
   struct u_trace_chunk *chunk = ptr;

   chunk->utctx->delete_timestamp_buffer(chunk->utctx, chunk->timestamps);

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
   chunk->timestamps = ut->utctx->create_timestamp_buffer(ut->utctx, TIMESTAMP_BUF_SIZE);
   chunk->last = true;

   list_addtail(&chunk->node, &ut->trace_chunks);

   return chunk;
}

DEBUG_GET_ONCE_BOOL_OPTION(trace, "GPU_TRACE", false)
DEBUG_GET_ONCE_FILE_OPTION(trace_file, "GPU_TRACEFILE", NULL, "w")

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

static void
queue_init(struct u_trace_context *utctx)
{
   if (utctx->queue.jobs)
      return;

   bool ret = util_queue_init(&utctx->queue, "traceq", 256, 1,
                              UTIL_QUEUE_INIT_USE_MINIMUM_PRIORITY |
                              UTIL_QUEUE_INIT_RESIZE_IF_FULL, NULL);
   assert(ret);

   if (!ret)
      utctx->out = NULL;
}

void
u_trace_context_init(struct u_trace_context *utctx,
      void *pctx,
      u_trace_create_ts_buffer  create_timestamp_buffer,
      u_trace_delete_ts_buffer  delete_timestamp_buffer,
      u_trace_record_ts         record_timestamp,
      u_trace_read_ts           read_timestamp,
      u_trace_delete_flush_data delete_flush_data)
{
   utctx->pctx = pctx;
   utctx->create_timestamp_buffer = create_timestamp_buffer;
   utctx->delete_timestamp_buffer = delete_timestamp_buffer;
   utctx->record_timestamp = record_timestamp;
   utctx->read_timestamp = read_timestamp;
   utctx->delete_flush_data = delete_flush_data;

   utctx->last_time_ns = 0;
   utctx->first_time_ns = 0;
   utctx->frame_nr = 0;

   list_inithead(&utctx->flushed_trace_chunks);

   utctx->out = get_tracefile();

#ifdef HAVE_PERFETTO
   list_add(&utctx->node, &ctx_list);
#endif

   if (!u_trace_context_tracing(utctx))
      return;

   queue_init(utctx);
}

void
u_trace_context_fini(struct u_trace_context *utctx)
{
#ifdef HAVE_PERFETTO
   list_del(&utctx->node);
#endif
   if (!utctx->queue.jobs)
      return;
   util_queue_finish(&utctx->queue);
   util_queue_destroy(&utctx->queue);
   fflush(utctx->out);
   free_chunks(&utctx->flushed_trace_chunks);
}

#ifdef HAVE_PERFETTO
void
u_trace_perfetto_start(void)
{
   list_for_each_entry (struct u_trace_context, utctx, &ctx_list, node)
      queue_init(utctx);
   ut_perfetto_enabled++;
}

void
u_trace_perfetto_stop(void)
{
   assert(ut_perfetto_enabled > 0);
   ut_perfetto_enabled--;
}
#endif

static void
process_chunk(void *job, void *gdata, int thread_index)
{
   struct u_trace_chunk *chunk = job;
   struct u_trace_context *utctx = chunk->utctx;

   /* For first chunk of batch, accumulated times will be zerod: */
   if (utctx->out && !utctx->last_time_ns) {
      fprintf(utctx->out, "+----- NS -----+ +-- Δ --+  +----- MSG -----\n");
   }

   for (unsigned idx = 0; idx < chunk->num_traces; idx++) {
      const struct u_trace_event *evt = &chunk->traces[idx];

      if (!evt->tp)
         continue;

      uint64_t ns = utctx->read_timestamp(utctx, chunk->timestamps, idx, chunk->flush_data);
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

      if (utctx->out) {
         if (evt->tp->print) {
            fprintf(utctx->out, "%016"PRIu64" %+9d: %s: ", ns, delta, evt->tp->name);
            evt->tp->print(utctx->out, evt->payload);
         } else {
            fprintf(utctx->out, "%016"PRIu64" %+9d: %s\n", ns, delta, evt->tp->name);
         }
      }
#ifdef HAVE_PERFETTO
      if (evt->tp->perfetto) {
         evt->tp->perfetto(utctx->pctx, ns, chunk->flush_data, evt->payload);
      }
#endif
   }

   if (chunk->last) {
      if (utctx->out) {
         uint64_t elapsed = utctx->last_time_ns - utctx->first_time_ns;
         fprintf(utctx->out, "ELAPSED: %"PRIu64" ns\n", elapsed);
      }

      utctx->last_time_ns = 0;
      utctx->first_time_ns = 0;
   }

   if (chunk->free_flush_data && utctx->delete_flush_data) {
      utctx->delete_flush_data(utctx, chunk->flush_data);
   }

   if (utctx->out && chunk->eof) {
      fprintf(utctx->out, "END OF FRAME %u\n", utctx->frame_nr++);
   }
}

static void
cleanup_chunk(void *job, void *gdata, int thread_index)
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
   ut->enabled = u_trace_context_tracing(utctx);
}

void
u_trace_fini(struct u_trace *ut)
{
   /* Normally the list of trace-chunks would be empty, if they
    * have been flushed to the trace-context.
    */
   free_chunks(&ut->trace_chunks);
}

bool
u_trace_has_points(struct u_trace *ut)
{
   return !list_is_empty(&ut->trace_chunks);
}

struct u_trace_iterator
u_trace_begin_iterator(struct u_trace *ut)
{
   if (!ut->enabled)
      return (struct u_trace_iterator) {NULL, NULL, 0};

   struct u_trace_chunk *first_chunk =
      list_first_entry(&ut->trace_chunks, struct u_trace_chunk, node);

   return (struct u_trace_iterator) { ut, first_chunk, 0};
}

struct u_trace_iterator
u_trace_end_iterator(struct u_trace *ut)
{
   if (!ut->enabled)
      return (struct u_trace_iterator) {NULL, NULL, 0};

   struct u_trace_chunk *last_chunk =
      list_last_entry(&ut->trace_chunks, struct u_trace_chunk, node);

   return (struct u_trace_iterator) { ut, last_chunk, last_chunk->num_traces};
}

bool
u_trace_iterator_equal(struct u_trace_iterator a,
                       struct u_trace_iterator b)
{
   return a.ut == b.ut &&
          a.chunk == b.chunk &&
          a.event_idx == b.event_idx;
}

void
u_trace_clone_append(struct u_trace_iterator begin_it,
                     struct u_trace_iterator end_it,
                     struct u_trace *into,
                     void *cmdstream,
                     u_trace_copy_ts_buffer copy_ts_buffer)
{
   struct u_trace_chunk *from_chunk = begin_it.chunk;
   uint32_t from_idx = begin_it.event_idx;

   while (from_chunk != end_it.chunk || from_idx != end_it.event_idx) {
      struct u_trace_chunk *to_chunk = get_chunk(into);

      unsigned to_copy = MIN2(TRACES_PER_CHUNK - to_chunk->num_traces,
                              from_chunk->num_traces - from_idx);
      if (from_chunk == end_it.chunk)
         to_copy = MIN2(to_copy, end_it.event_idx - from_idx);

      copy_ts_buffer(begin_it.ut->utctx, cmdstream,
                     from_chunk->timestamps, from_idx,
                     to_chunk->timestamps, to_chunk->num_traces,
                     to_copy);

      memcpy(&to_chunk->traces[to_chunk->num_traces],
             &from_chunk->traces[from_idx],
             to_copy * sizeof(struct u_trace_event));

      to_chunk->num_traces += to_copy;
      from_idx += to_copy;

      assert(from_idx <= from_chunk->num_traces);
      if (from_idx == from_chunk->num_traces) {
         if (from_chunk == end_it.chunk)
            break;

         from_idx = 0;
         from_chunk = LIST_ENTRY(struct u_trace_chunk, from_chunk->node.next, node);
      }
   }
}

void
u_trace_disable_event_range(struct u_trace_iterator begin_it,
                            struct u_trace_iterator end_it)
{
   struct u_trace_chunk *current_chunk = begin_it.chunk;
   uint32_t start_idx = begin_it.event_idx;

   while(current_chunk != end_it.chunk) {
      memset(&current_chunk->traces[start_idx], 0,
             (current_chunk->num_traces - start_idx) * sizeof(struct u_trace_event));
      start_idx = 0;
      current_chunk = LIST_ENTRY(struct u_trace_chunk, current_chunk->node.next, node);
   }

   memset(&current_chunk->traces[start_idx], 0,
          (end_it.event_idx - start_idx) * sizeof(struct u_trace_event));
}

/**
 * Append a trace event, returning pointer to buffer of tp->payload_sz
 * to be filled in with trace payload.  Called by generated tracepoint
 * functions.
 */
void *
u_trace_append(struct u_trace *ut, void *cs, const struct u_tracepoint *tp)
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
   ut->utctx->record_timestamp(ut, cs, chunk->timestamps, chunk->num_traces);

   chunk->traces[chunk->num_traces] = (struct u_trace_event) {
         .tp = tp,
         .payload = payload,
   };

   chunk->num_traces++;

   return payload;
}

void
u_trace_flush(struct u_trace *ut, void *flush_data, bool free_data)
{
   list_for_each_entry(struct u_trace_chunk, chunk, &ut->trace_chunks, node) {
      chunk->flush_data = flush_data;
      chunk->free_flush_data = false;
   }

   if (free_data && !list_is_empty(&ut->trace_chunks)) {
      struct u_trace_chunk *last_chunk =
         list_last_entry(&ut->trace_chunks, struct u_trace_chunk, node);
      last_chunk->free_flush_data = true;
   }

   /* transfer batch's log chunks to context: */
   list_splicetail(&ut->trace_chunks, &ut->utctx->flushed_trace_chunks);
   list_inithead(&ut->trace_chunks);
}
