/**************************************************************************
 *
 * Copyright 2020 Lag Free Games, LLC
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/

#include "cnd_monotonic.h"
#include "util/os_time.h"
#include "util/timespec.h"

#include <assert.h>

#ifdef _WIN32
#include <windows.h>

static_assert(sizeof(struct u_cnd_monotonic) == sizeof(CONDITION_VARIABLE),
              "The size of u_cnd_monotonic must equal to CONDITION_VARIABLE");
static_assert(sizeof(mtx_t) == sizeof(CRITICAL_SECTION),
              "The size of mtx_t must equal to CRITICAL_SECTION");
#endif

int
u_cnd_monotonic_init(struct u_cnd_monotonic *cond)
{
   assert(cond != NULL);

#ifdef _WIN32
   InitializeConditionVariable((PCONDITION_VARIABLE)cond);
   return thrd_success;
#else
   int ret = thrd_error;
   pthread_condattr_t condattr;
   if (pthread_condattr_init(&condattr) == 0) {
      if (
         // pthread_condattr_setclock is not supported on Apple platforms.
         // Instead, they use a relative deadline. See u_cnd_monotonic_timedwait.
#ifndef __APPLE__
         (pthread_condattr_setclock(&condattr, CLOCK_MONOTONIC) == 0) &&
#endif
         (pthread_cond_init(&cond->cond, &condattr) == 0)) {
         ret = thrd_success;
      }

      pthread_condattr_destroy(&condattr);
   }

   return ret;
#endif
}

void
u_cnd_monotonic_destroy(struct u_cnd_monotonic *cond)
{
   assert(cond != NULL);

#ifdef _WIN32
   // Do nothing
#else
   pthread_cond_destroy(&cond->cond);
#endif
}

int
u_cnd_monotonic_broadcast(struct u_cnd_monotonic *cond)
{
   assert(cond != NULL);

#ifdef _WIN32
   WakeAllConditionVariable((PCONDITION_VARIABLE)cond);
   return thrd_success;
#else
   return (pthread_cond_broadcast(&cond->cond) == 0) ? thrd_success : thrd_error;
#endif
}

int
u_cnd_monotonic_signal(struct u_cnd_monotonic *cond)
{
   assert(cond != NULL);

#ifdef _WIN32
   WakeConditionVariable((PCONDITION_VARIABLE)cond);
   return thrd_success;
#else
   return (pthread_cond_signal(&cond->cond) == 0) ? thrd_success : thrd_error;
#endif
}

int
u_cnd_monotonic_timedwait(struct u_cnd_monotonic *cond, mtx_t *mtx,
                          const struct timespec *abs_time)
{
   assert(cond != NULL);
   assert(mtx != NULL);
   assert(abs_time != NULL);

#ifdef _WIN32
   const uint64_t future = (abs_time->tv_sec * 1000) + (abs_time->tv_nsec / 1000000);
   const uint64_t now = os_time_get_nano() / 1000000;
   const DWORD timeout = (future > now) ? (DWORD)(future - now) : 0;
   if (SleepConditionVariableCS((PCONDITION_VARIABLE)cond,
                                (PCRITICAL_SECTION)mtx, timeout))
      return thrd_success;
   return (GetLastError() == ERROR_TIMEOUT) ? thrd_timedout : thrd_error;
#else
#ifdef __APPLE__
   // Convert to a relative wait as we can't use CLOCK_MONOTONIC deadlines on macOS.
   struct timespec now_time;
   timespec_get(&now_time, TIME_MONOTONIC);
   struct timespec rel_time;
   timespec_sub_saturate(&rel_time, abs_time, &now_time);
   int rt = pthread_cond_timedwait_relative_np(&cond->cond, mtx, &rel_time);
#else
   int rt = pthread_cond_timedwait(&cond->cond, mtx, abs_time);
#endif
   if (rt == ETIMEDOUT)
      return thrd_timedout;
   return (rt == 0) ? thrd_success : thrd_error;
#endif
}

int
u_cnd_monotonic_wait(struct u_cnd_monotonic *cond, mtx_t *mtx)
{
   assert(cond != NULL);
   assert(mtx != NULL);

#ifdef _WIN32
   SleepConditionVariableCS((PCONDITION_VARIABLE)cond,
                            (PCRITICAL_SECTION)mtx, INFINITE);
   return thrd_success;
#else
   return (pthread_cond_wait(&cond->cond, mtx) == 0) ? thrd_success : thrd_error;
#endif
}
