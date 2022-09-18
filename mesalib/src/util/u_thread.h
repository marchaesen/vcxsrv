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

#include <errno.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "c11/threads.h"
#include "detect_os.h"
#include "macros.h"

#ifdef HAVE_PTHREAD
#include <signal.h>
#ifdef HAVE_PTHREAD_NP_H
#include <pthread_np.h>
#endif
#endif

#ifdef __HAIKU__
#include <OS.h>
#endif

#if DETECT_OS_LINUX && !defined(ANDROID)
#include <sched.h>
#elif defined(_WIN32) && !defined(__CYGWIN__) && _WIN32_WINNT >= 0x0600
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN 1
#endif
#include <windows.h>
#endif

#ifdef __FreeBSD__
/* pthread_np.h -> sys/param.h -> machine/param.h
 * - defines ALIGN which clashes with our ALIGN
 */
#undef ALIGN
#define cpu_set_t cpuset_t
#endif

/* For util_set_thread_affinity to size the mask. */
#define UTIL_MAX_CPUS               1024  /* this should be enough */
#define UTIL_MAX_L3_CACHES          UTIL_MAX_CPUS

/* Some highly performance-sensitive thread-local variables like the current GL
 * context are declared with the initial-exec model on Linux.  glibc allocates a
 * fixed number of extra slots for initial-exec TLS variables at startup, and
 * Mesa relies on (even if it's dlopen()ed after init) being able to fit into
 * those.  This model saves the call to look up the address of the TLS variable.
 *
 * However, if we don't have this TLS model available on the platform, then we
 * still want to use normal TLS (which involves a function call, but not the
 * expensive pthread_getspecific() or its equivalent).
 */
#if DETECT_OS_APPLE
/* Apple Clang emits wrappers when using thread_local that break module linkage,
 * but not with __thread
 */
#define __THREAD_INITIAL_EXEC __thread
#elif defined(__GLIBC__)
#define __THREAD_INITIAL_EXEC thread_local __attribute__((tls_model("initial-exec")))
#define REALLY_INITIAL_EXEC
#else
#define __THREAD_INITIAL_EXEC thread_local
#endif

static inline int
util_get_current_cpu(void)
{
#if DETECT_OS_LINUX && !defined(ANDROID)
   return sched_getcpu();

#elif defined(_WIN32) && !defined(__CYGWIN__) && _WIN32_WINNT >= 0x0600
   return GetCurrentProcessorNumber();

#else
   return -1;
#endif
}

static inline int u_thread_create(thrd_t *thrd, int (*routine)(void *), void *param)
{
   int ret = thrd_error;
#ifdef HAVE_PTHREAD
   sigset_t saved_set, new_set;

   sigfillset(&new_set);
   sigdelset(&new_set, SIGSYS);

   /* SIGSEGV is commonly used by Vulkan API tracing layers in order to track
    * accesses in device memory mapped to user space. Blocking the signal hinders
    * that tracking mechanism.
    */
   sigdelset(&new_set, SIGSEGV);
   pthread_sigmask(SIG_BLOCK, &new_set, &saved_set);
   ret = thrd_create(thrd, routine, param);
   pthread_sigmask(SIG_SETMASK, &saved_set, NULL);
#else
   ret = thrd_create(thrd, routine, param);
#endif

   return ret;
}

static inline void u_thread_setname( const char *name )
{
#if defined(HAVE_PTHREAD)
#if DETECT_OS_LINUX || DETECT_OS_CYGWIN || DETECT_OS_SOLARIS
   int ret = pthread_setname_np(pthread_self(), name);
   if (ret == ERANGE) {
      char buf[16];
      const size_t len = MIN2(strlen(name), ARRAY_SIZE(buf) - 1);
      memcpy(buf, name, len);
      buf[len] = '\0';
      pthread_setname_np(pthread_self(), buf);
   }
#elif DETECT_OS_FREEBSD || DETECT_OS_OPENBSD
   pthread_set_name_np(pthread_self(), name);
#elif DETECT_OS_NETBSD
   pthread_setname_np(pthread_self(), "%s", (void *)name);
#elif DETECT_OS_APPLE
   pthread_setname_np(name);
#elif DETECT_OS_HAIKU
   rename_thread(find_thread(NULL), name);
#else
#warning Not sure how to call pthread_setname_np
#endif
#endif
   (void)name;
}

/**
 * Set thread affinity.
 *
 * \param thread         Thread
 * \param mask           Set this affinity mask
 * \param old_mask       Previous affinity mask returned if not NULL
 * \param num_mask_bits  Number of bits in both masks
 * \return  true on success
 */
static inline bool
util_set_thread_affinity(thrd_t thread,
                         const uint32_t *mask,
                         uint32_t *old_mask,
                         unsigned num_mask_bits)
{
#if defined(HAVE_PTHREAD_SETAFFINITY)
   cpu_set_t cpuset;

   if (old_mask) {
      if (pthread_getaffinity_np(thread, sizeof(cpuset), &cpuset) != 0)
         return false;

      memset(old_mask, 0, num_mask_bits / 8);
      for (unsigned i = 0; i < num_mask_bits && i < CPU_SETSIZE; i++) {
         if (CPU_ISSET(i, &cpuset))
            old_mask[i / 32] |= 1u << (i % 32);
      }
   }

   CPU_ZERO(&cpuset);
   for (unsigned i = 0; i < num_mask_bits && i < CPU_SETSIZE; i++) {
      if (mask[i / 32] & (1u << (i % 32)))
         CPU_SET(i, &cpuset);
   }
   return pthread_setaffinity_np(thread, sizeof(cpuset), &cpuset) == 0;

#elif defined(_WIN32) && !defined(__CYGWIN__)
   DWORD_PTR m = mask[0];

   if (sizeof(m) > 4 && num_mask_bits > 32)
      m |= (uint64_t)mask[1] << 32;

   m = SetThreadAffinityMask(thread.handle, m);
   if (!m)
      return false;

   if (old_mask) {
      memset(old_mask, 0, num_mask_bits / 8);

      old_mask[0] = m;
#ifdef _WIN64
      old_mask[1] = m >> 32;
#endif
   }

   return true;
#else
   return false;
#endif
}

static inline bool
util_set_current_thread_affinity(const uint32_t *mask,
                                 uint32_t *old_mask,
                                 unsigned num_mask_bits)
{
   return util_set_thread_affinity(thrd_current(), mask, old_mask,
                                   num_mask_bits);
}

/*
 * Thread statistics.
 */

/* Return the time of a thread's CPU time clock. */
static inline int64_t
util_thread_get_time_nano(thrd_t thread)
{
#if defined(HAVE_PTHREAD) && !defined(__APPLE__) && !defined(__HAIKU__)
   struct timespec ts;
   clockid_t cid;

   pthread_getcpuclockid(thread, &cid);
   clock_gettime(cid, &ts);
   return (int64_t)ts.tv_sec * 1000000000 + ts.tv_nsec;
#elif defined(_WIN32)
   union {
      FILETIME time;
      ULONGLONG value;
   } kernel_time, user_time;
   GetThreadTimes((HANDLE)thread.handle, NULL, NULL, &kernel_time.time, &user_time.time);
   return (kernel_time.value + user_time.value) * 100;
#else
   (void)thread;
   return 0;
#endif
}

/* Return the time of the current thread's CPU time clock. */
static inline int64_t
util_current_thread_get_time_nano(void)
{
   return util_thread_get_time_nano(thrd_current());
}

static inline bool u_thread_is_self(thrd_t thread)
{
   return thrd_equal(thrd_current(), thread);
}

/*
 * util_barrier
 */

#if defined(HAVE_PTHREAD) && !defined(__APPLE__) && !defined(__HAIKU__)

typedef pthread_barrier_t util_barrier;

static inline void util_barrier_init(util_barrier *barrier, unsigned count)
{
   pthread_barrier_init(barrier, NULL, count);
}

static inline void util_barrier_destroy(util_barrier *barrier)
{
   pthread_barrier_destroy(barrier);
}

static inline bool util_barrier_wait(util_barrier *barrier)
{
   return pthread_barrier_wait(barrier) == PTHREAD_BARRIER_SERIAL_THREAD;
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

static inline bool util_barrier_wait(util_barrier *barrier)
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

   return true;
}

#endif

#endif /* U_THREAD_H_ */
