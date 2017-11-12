/**************************************************************************
 * 
 * Copyright 1999-2006 Brian Paul
 * Copyright 2008 VMware, Inc.
 * All Rights Reserved.
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#ifndef U_THREAD_H_
#define U_THREAD_H_

#include <stdint.h>
#include <stdbool.h>

#include "c11/threads.h"

#ifdef HAVE_PTHREAD
#include <signal.h>
#endif


static inline thrd_t u_thread_create(int (*routine)(void *), void *param)
{
   thrd_t thread;
#ifdef HAVE_PTHREAD
   sigset_t saved_set, new_set;
   int ret;

   sigfillset(&new_set);
   pthread_sigmask(SIG_SETMASK, &new_set, &saved_set);
   ret = thrd_create( &thread, routine, param );
   pthread_sigmask(SIG_SETMASK, &saved_set, NULL);
#else
   int ret;
   ret = thrd_create( &thread, routine, param );
#endif
   if (ret)
      return 0;

   return thread;
}

static inline void u_thread_setname( const char *name )
{
#if defined(HAVE_PTHREAD)
#  if defined(__GNU_LIBRARY__) && defined(__GLIBC__) && defined(__GLIBC_MINOR__) && \
      (__GLIBC__ >= 3 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 12))
   pthread_setname_np(pthread_self(), name);
#  endif
#endif
   (void)name;
}

/*
 * Thread statistics.
 */

/* Return the time of a thread's CPU time clock. */
static inline int64_t
u_thread_get_time_nano(thrd_t thread)
{
#if defined(__linux__) && defined(HAVE_PTHREAD)
   struct timespec ts;
   clockid_t cid;

   pthread_getcpuclockid(thread, &cid);
   clock_gettime(cid, &ts);
   return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
#else
   return 0;
#endif
}

static inline bool u_thread_is_self(thrd_t thread)
{
#if defined(HAVE_PTHREAD)
#  if defined(__GNU_LIBRARY__) && defined(__GLIBC__) && defined(__GLIBC_MINOR__) && \
      (__GLIBC__ >= 3 || (__GLIBC__ == 2 && __GLIBC_MINOR__ >= 12))
   return pthread_equal(pthread_self(), thread);
#  endif
#endif
   return false;
}

/*
 * util_barrier
 */

#if defined(HAVE_PTHREAD) && !defined(__APPLE__)

typedef pthread_barrier_t util_barrier;

static inline void util_barrier_init(util_barrier *barrier, unsigned count)
{
   pthread_barrier_init(barrier, NULL, count);
}

static inline void util_barrier_destroy(util_barrier *barrier)
{
   pthread_barrier_destroy(barrier);
}

static inline void util_barrier_wait(util_barrier *barrier)
{
   pthread_barrier_wait(barrier);
}


#else /* If the OS doesn't have its own, implement barriers using a mutex and a condvar */

typedef struct {
   unsigned count;
   unsigned waiters;
   uint64_t sequence;
   mtx_t mutex;
   cnd_t condvar;
} util_barrier;

static inline void util_barrier_init(util_barrier *barrier, unsigned count)
{
   barrier->count = count;
   barrier->waiters = 0;
   barrier->sequence = 0;
   (void) mtx_init(&barrier->mutex, mtx_plain);
   cnd_init(&barrier->condvar);
}

static inline void util_barrier_destroy(util_barrier *barrier)
{
   assert(barrier->waiters == 0);
   mtx_destroy(&barrier->mutex);
   cnd_destroy(&barrier->condvar);
}

static inline void util_barrier_wait(util_barrier *barrier)
{
   mtx_lock(&barrier->mutex);

   assert(barrier->waiters < barrier->count);
   barrier->waiters++;

   if (barrier->waiters < barrier->count) {
      uint64_t sequence = barrier->sequence;

      do {
         cnd_wait(&barrier->condvar, &barrier->mutex);
      } while (sequence == barrier->sequence);
   } else {
      barrier->waiters = 0;
      barrier->sequence++;
      cnd_broadcast(&barrier->condvar);
   }

   mtx_unlock(&barrier->mutex);
}

#endif

#endif /* U_THREAD_H_ */
