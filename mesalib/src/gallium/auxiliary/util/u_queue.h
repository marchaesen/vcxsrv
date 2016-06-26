/*
 * Copyright © 2016 Advanced Micro Devices, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

/* Job queue with execution in a separate thread.
 *
 * Jobs can be added from any thread. After that, the wait call can be used
 * to wait for completion of the job.
 */

#ifndef U_QUEUE_H
#define U_QUEUE_H

#include "os/os_thread.h"

/* Job completion fence.
 * Put this into your job structure.
 */
struct util_queue_fence {
   pipe_mutex mutex;
   pipe_condvar cond;
   int signalled;
};

typedef void (*util_queue_execute_func)(void *job, int thread_index);

struct util_queue_job {
   void *job;
   struct util_queue_fence *fence;
   util_queue_execute_func execute;
};

/* Put this into your context. */
struct util_queue {
   const char *name;
   pipe_mutex lock;
   pipe_condvar has_queued_cond;
   pipe_condvar has_space_cond;
   pipe_thread *threads;
   int num_queued;
   unsigned num_threads;
   int kill_threads;
   int max_jobs;
   int write_idx, read_idx; /* ring buffer pointers */
   struct util_queue_job *jobs;
};

bool util_queue_init(struct util_queue *queue,
                     const char *name,
                     unsigned max_jobs,
                     unsigned num_threads);
void util_queue_destroy(struct util_queue *queue);
void util_queue_fence_init(struct util_queue_fence *fence);
void util_queue_fence_destroy(struct util_queue_fence *fence);

void util_queue_add_job(struct util_queue *queue,
                        void *job,
                        struct util_queue_fence *fence,
                        util_queue_execute_func execute);
void util_queue_job_wait(struct util_queue_fence *fence);

/* util_queue needs to be cleared to zeroes for this to work */
static inline bool
util_queue_is_initialized(struct util_queue *queue)
{
   return queue->threads != NULL;
}

static inline bool
util_queue_fence_is_signalled(struct util_queue_fence *fence)
{
   return fence->signalled != 0;
}

#endif
