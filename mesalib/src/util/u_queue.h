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

#include <string.h>

#include "util/list.h"
#include "util/u_thread.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UTIL_QUEUE_INIT_USE_MINIMUM_PRIORITY      (1 << 0)
#define UTIL_QUEUE_INIT_RESIZE_IF_FULL            (1 << 1)

/* Job completion fence.
 * Put this into your job structure.
 */
struct util_queue_fence {
   mtx_t mutex;
   cnd_t cond;
   int signalled;
};

typedef void (*util_queue_execute_func)(void *job, int thread_index);

struct util_queue_job {
   void *job;
   struct util_queue_fence *fence;
   util_queue_execute_func execute;
   util_queue_execute_func cleanup;
};

/* Put this into your context. */
struct util_queue {
   const char *name;
   mtx_t lock;
   cnd_t has_queued_cond;
   cnd_t has_space_cond;
   thrd_t *threads;
   unsigned flags;
   int num_queued;
   unsigned num_threads;
   int kill_threads;
   int max_jobs;
   int write_idx, read_idx; /* ring buffer pointers */
   struct util_queue_job *jobs;

   /* for cleanup at exit(), protected by exit_mutex */
   struct list_head head;
};

bool util_queue_init(struct util_queue *queue,
                     const char *name,
                     unsigned max_jobs,
                     unsigned num_threads,
                     unsigned flags);
void util_queue_destroy(struct util_queue *queue);
void util_queue_fence_init(struct util_queue_fence *fence);
void util_queue_fence_destroy(struct util_queue_fence *fence);

/* optional cleanup callback is called after fence is signaled: */
void util_queue_add_job(struct util_queue *queue,
                        void *job,
                        struct util_queue_fence *fence,
                        util_queue_execute_func execute,
                        util_queue_execute_func cleanup);
void util_queue_drop_job(struct util_queue *queue,
                         struct util_queue_fence *fence);

void util_queue_fence_wait(struct util_queue_fence *fence);
int64_t util_queue_get_thread_time_nano(struct util_queue *queue,
                                        unsigned thread_index);

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

/* Convenient structure for monitoring the queue externally and passing
 * the structure between Mesa components. The queue doesn't use it directly.
 */
struct util_queue_monitoring
{
   /* For querying the thread busyness. */
   struct util_queue *queue;

   /* Counters updated by the user of the queue. */
   unsigned num_offloaded_items;
   unsigned num_direct_items;
   unsigned num_syncs;
};

#ifdef __cplusplus
}
#endif

#endif
