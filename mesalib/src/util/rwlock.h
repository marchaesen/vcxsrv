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

#ifndef RWLOCK_H
#define RWLOCK_H

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

struct u_rwlock
{
#ifdef _WIN32
   SRWLOCK rwlock;
#else
   pthread_rwlock_t rwlock;
#endif
};

static inline int u_rwlock_init(struct u_rwlock *rwlock)
{
#ifdef _WIN32
   InitializeSRWLock(&rwlock->rwlock);
   return 0;
#else
   return pthread_rwlock_init(&rwlock->rwlock, NULL);
#endif
}

static inline int u_rwlock_destroy(struct u_rwlock *rwlock)
{
#ifdef _WIN32
   return 0;
#else
   return pthread_rwlock_destroy(&rwlock->rwlock);
#endif
}

static inline int u_rwlock_rdlock(struct u_rwlock *rwlock)
{
#ifdef _WIN32
   AcquireSRWLockShared(&rwlock->rwlock);
   return 0;
#else
   return pthread_rwlock_rdlock(&rwlock->rwlock);
#endif
}

static inline int u_rwlock_rdunlock(struct u_rwlock *rwlock)
{
#ifdef _WIN32
   ReleaseSRWLockShared(&rwlock->rwlock);
   return 0;
#else
   return pthread_rwlock_unlock(&rwlock->rwlock);
#endif
}

static inline int u_rwlock_wrlock(struct u_rwlock *rwlock)
{
#ifdef _WIN32
   AcquireSRWLockExclusive(&rwlock->rwlock);
   return 0;
#else
   return pthread_rwlock_wrlock(&rwlock->rwlock);
#endif
}

static inline int u_rwlock_wrunlock(struct u_rwlock *rwlock)
{
#ifdef _WIN32
   ReleaseSRWLockExclusive(&rwlock->rwlock);
   return 0;
#else
   return pthread_rwlock_unlock(&rwlock->rwlock);
#endif
}

#ifdef __cplusplus
}
#endif

#endif
