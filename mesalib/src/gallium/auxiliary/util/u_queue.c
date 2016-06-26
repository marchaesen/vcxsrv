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

#include "u_queue.h"
#include "u_memory.h"
#include "u_string.h"
#include "os/os_time.h"

static void
util_queue_fence_signal(struct util_queue_fence *fence)
{
   pipe_mutex_lock(fence->mutex);
   fence->signalled = true;
   pipe_condvar_broadcast(fence->cond);
   pipe_mutex_unlock(fence->mutex);
}

void
util_queue_job_wait(struct util_queue_fence *fence)
{
   if (fence->signalled)
      return;

   pipe_mutex_lock(fence->mutex);
   while (!fence->signalled)
      pipe_condvar_wait(fence->cond, fence->mutex);
   pipe_mutex_unlock(fence->mutex);
}

struct thread_input {
   struct util_queue *queue;
   int thread_index;
};

static PIPE_THREAD_ROUTINE(util_queue_thread_func, input)
{
   struct util_queue *queue = ((struct thread_input*)input)->queue;
   int thread_index = ((struct thread_input*)input)->thread_index;

   FREE(input);

   if (queue->name) {
      char name[16];
      util_snprintf(name, sizeof(name), "%s:%i", queue->name, thread_index);
      pipe_thread_setname(name);
   }

   while (1) {
      struct util_queue_job job;

      pipe_mutex_lock(queue->lock);
      assert(queue->num_queued >= 0 && queue->num_queued <= queue->max_jobs);

      /* wait if the queue is empty */
      while (!queue->kill_threads && queue->num_queued == 0)
         pipe_condvar_wait(queue->has_queued_cond, queue->lock);

      if (queue->kill_threads) {
         pipe_mutex_unlock(queue->lock);
         break;
      }

      job = queue->jobs[queue->read_idx];
      memset(&queue->jobs[queue->read_idx], 0, sizeof(struct util_queue_job));
      queue->read_idx = (queue->read_idx + 1) % queue->max_jobs;

      queue->num_queued--;
      pipe_condvar_signal(queue->has_space_cond);
      pipe_mutex_unlock(queue->lock);

      if (job.job) {
         job.execute(job.job, thread_index);
         util_queue_fence_signal(job.fence);
      }
   }

   /* signal remaining jobs before terminating */
   pipe_mutex_lock(queue->lock);
   while (queue->jobs[queue->read_idx].job) {
      util_queue_fence_signal(queue->jobs[queue->read_idx].fence);

      queue->jobs[queue->read_idx].job = NULL;
      queue->read_idx = (queue->read_idx + 1) % queue->max_jobs;
   }
   pipe_mutex_unlock(queue->lock);
   return 0;
}

bool
util_queue_init(struct util_queue *queue,
                const char *name,
                unsigned max_jobs,
                unsigned num_threads)
{
   unsigned i;

   memset(queue, 0, sizeof(*queue));
   queue->name = name;
   queue->num_threads = num_threads;
   queue->max_jobs = max_jobs;

   queue->jobs = (struct util_queue_job*)
                 CALLOC(max_jobs, sizeof(struct util_queue_job));
   if (!queue->jobs)
      goto fail;

   pipe_mutex_init(queue->lock);

   queue->num_queued = 0;
   pipe_condvar_init(queue->has_queued_cond);
   pipe_condvar_init(queue->has_space_cond);

   queue->threads = (pipe_thread*)CALLOC(num_threads, sizeof(pipe_thread));
   if (!queue->threads)
      goto fail;

   /* start threads */
   for (i = 0; i < num_threads; i++) {
      struct thread_input *input = MALLOC_STRUCT(thread_input);
      input->queue = queue;
      input->thread_index = i;

      queue->threads[i] = pipe_thread_create(util_queue_thread_func, input);

      if (!queue->threads[i]) {
         FREE(input);

         if (i == 0) {
            /* no threads created, fail */
            goto fail;
         } else {
            /* at least one thread created, so use it */
            queue->num_threads = i+1;
            break;
         }
      }
   }
   return true;

fail:
   FREE(queue->threads);

   if (queue->jobs) {
      pipe_condvar_destroy(queue->has_space_cond);
      pipe_condvar_destroy(queue->has_queued_cond);
      pipe_mutex_destroy(queue->lock);
      FREE(queue->jobs);
   }
   /* also util_queue_is_initialized can be used to check for success */
   memset(queue, 0, sizeof(*queue));
   return false;
}

void
util_queue_destroy(struct util_queue *queue)
{
   unsigned i;

   /* Signal all threads to terminate. */
   pipe_mutex_lock(queue->lock);
   queue->kill_threads = 1;
   pipe_condvar_broadcast(queue->has_queued_cond);
   pipe_mutex_unlock(queue->lock);

   for (i = 0; i < queue->num_threads; i++)
      pipe_thread_wait(queue->threads[i]);

   pipe_condvar_destroy(queue->has_space_cond);
   pipe_condvar_destroy(queue->has_queued_cond);
   pipe_mutex_destroy(queue->lock);
   FREE(queue->jobs);
   FREE(queue->threads);
}

void
util_queue_fence_init(struct util_queue_fence *fence)
{
   memset(fence, 0, sizeof(*fence));
   pipe_mutex_init(fence->mutex);
   pipe_condvar_init(fence->cond);
   fence->signalled = true;
}

void
util_queue_fence_destroy(struct util_queue_fence *fence)
{
   pipe_condvar_destroy(fence->cond);
   pipe_mutex_destroy(fence->mutex);
}

void
util_queue_add_job(struct util_queue *queue,
                   void *job,
                   struct util_queue_fence *fence,
                   util_queue_execute_func execute)
{
   struct util_queue_job *ptr;

   assert(fence->signalled);
   fence->signalled = false;

   pipe_mutex_lock(queue->lock);
   assert(queue->num_queued >= 0 && queue->num_queued <= queue->max_jobs);

   /* if the queue is full, wait until there is space */
   while (queue->num_queued == queue->max_jobs)
      pipe_condvar_wait(queue->has_space_cond, queue->lock);

   ptr = &queue->jobs[queue->write_idx];
   assert(ptr->job == NULL);
   ptr->job = job;
   ptr->fence = fence;
   ptr->execute = execute;
   queue->write_idx = (queue->write_idx + 1) % queue->max_jobs;

   queue->num_queued++;
   pipe_condvar_signal(queue->has_queued_cond);
   pipe_mutex_unlock(queue->lock);
}
