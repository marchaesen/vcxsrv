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

/* The size of one batch and the maximum size of one call.
 *
 * This should be as low as possible, so that:
 * - multiple synchronizations within a frame don't slow us down much
 * - a smaller number of calls per frame can still get decent parallelism
 * - the memory footprint of the queue is low, and with that comes a lower
 *   chance of experiencing CPU cache thrashing
 * but it should be high enough so that u_queue overhead remains negligible.
 */
#define MARSHAL_MAX_CMD_SIZE (8 * 1024)

/* The number of batch slots in memory.
 *
 * One batch is being executed, one batch is being filled, the rest are
 * waiting batches. There must be at least 1 slot for a waiting batch,
 * so the minimum number of batches is 3.
 */
#define MARSHAL_MAX_BATCHES 8

#include <inttypes.h>
#include <stdbool.h>
#include "util/u_queue.h"

enum marshal_dispatch_cmd_id;
struct gl_context;

/** A single batch of commands queued up for execution. */
struct glthread_batch
{
   /** Batch fence for waiting for the execution to finish. */
   struct util_queue_fence fence;

   /** The worker thread will access the context with this. */
   struct gl_context *ctx;

   /** Amount of data used by batch commands, in bytes. */
   size_t used;

   /** Data contained in the command buffer. */
   uint8_t buffer[MARSHAL_MAX_CMD_SIZE];
};

struct glthread_state
{
   /** Multithreaded queue. */
   struct util_queue queue;

   /** This is sent to the driver for framebuffer overlay / HUD. */
   struct util_queue_monitoring stats;

   /** The ring of batches in memory. */
   struct glthread_batch batches[MARSHAL_MAX_BATCHES];

   /** Index of the last submitted batch. */
   unsigned last;

   /** Index of the batch being filled and about to be submitted. */
   unsigned next;

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

void _mesa_glthread_init(struct gl_context *ctx);
void _mesa_glthread_destroy(struct gl_context *ctx);

void _mesa_glthread_restore_dispatch(struct gl_context *ctx);
void _mesa_glthread_flush_batch(struct gl_context *ctx);
void _mesa_glthread_finish(struct gl_context *ctx);

#endif /* _GLTHREAD_H*/
