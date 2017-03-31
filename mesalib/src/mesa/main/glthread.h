/*
 * Copyright © 2012 Intel Corporation
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

#ifndef _GLTHREAD_H
#define _GLTHREAD_H

#include "main/mtypes.h"

/* Command size is a number of bytes stored in a short. */
#define MARSHAL_MAX_CMD_SIZE 65535

#ifdef HAVE_PTHREAD

#include <inttypes.h>
#include <stdbool.h>
#include <pthread.h>

enum marshal_dispatch_cmd_id;

struct glthread_state
{
   /** The worker thread that asynchronously processes our GL commands. */
   pthread_t thread;

   /**
    * Mutex used for synchronizing between the main thread and the worker
    * thread.
    */
   pthread_mutex_t mutex;

   /** Condvar used for waking the worker thread. */
   pthread_cond_t new_work;

   /** Condvar used for waking the main thread. */
   pthread_cond_t work_done;

   /** Used to tell the worker thread to quit */
   bool shutdown;

   /** Indicates that the worker thread is currently processing a batch */
   bool busy;

   /**
    * Singly-linked list of command batches that are awaiting execution by
    * a thread pool task.  NULL if empty.
    */
   struct glthread_batch *batch_queue;

   /**
    * Tail pointer for appending batches to the end of batch_queue.  If the
    * queue is empty, this points to batch_queue.
    */
   struct glthread_batch **batch_queue_tail;

   /**
    * Batch containing commands that are being prepared for insertion into
    * batch_queue.  NULL if there are no such commands.
    *
    * Since this is only used by the main thread, it doesn't need the mutex to
    * be accessed.
    */
   struct glthread_batch *batch;

   /**
    * Tracks on the main thread side whether the current vertex array binding
    * is in a VBO.
    */
   bool vertex_array_is_vbo;

   /**
    * Tracks on the main thread side whether the current element array (index
    * buffer) binding is in a VBO.
    */
   bool element_array_is_vbo;
};

/**
 * A single batch of commands queued up for later execution by a thread pool
 * task.
 */
struct glthread_batch
{
   /**
    * Next batch of commands to execute after this batch, or NULL if this is
    * the last set of commands queued.  Protected by ctx->Marshal.Mutex.
    */
   struct glthread_batch *next;

   /**
    * Amount of data used by batch commands, in bytes.
    */
   size_t used;

   /**
    * Data contained in the command buffer.
    */
   uint8_t buffer[MARSHAL_MAX_CMD_SIZE];
};

void _mesa_glthread_init(struct gl_context *ctx);
void _mesa_glthread_destroy(struct gl_context *ctx);

void _mesa_glthread_restore_dispatch(struct gl_context *ctx);
void _mesa_glthread_flush_batch(struct gl_context *ctx);
void _mesa_glthread_finish(struct gl_context *ctx);

#else /* HAVE_PTHREAD */

static inline void
_mesa_glthread_init(struct gl_context *ctx)
{
}

static inline void
_mesa_glthread_destroy(struct gl_context *ctx)
{
}

static inline void
_mesa_glthread_finish(struct gl_context *ctx)
{
}

static inline void
_mesa_glthread_restore_dispatch(struct gl_context *ctx)
{
}

static inline void
_mesa_glthread_flush_batch(struct gl_context *ctx)
{
}

#endif /* !HAVE_PTHREAD */
#endif /* _GLTHREAD_H*/
