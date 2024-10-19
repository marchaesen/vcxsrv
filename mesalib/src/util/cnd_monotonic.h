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

#ifndef CND_MONOTONIC_H
#define CND_MONOTONIC_H

#include "c11/threads.h"

#include "util/os_time.h"

#include <assert.h>

#ifdef __cplusplus
extern "C" {
#endif

struct u_cnd_monotonic
{
#ifdef _WIN32
   void *Ptr;
#else
   pthread_cond_t cond;
#endif
};

int u_cnd_monotonic_init(struct u_cnd_monotonic *cond);
void u_cnd_monotonic_destroy(struct u_cnd_monotonic *cond);
int u_cnd_monotonic_broadcast(struct u_cnd_monotonic *cond);
int u_cnd_monotonic_signal(struct u_cnd_monotonic *cond);
int u_cnd_monotonic_timedwait(struct u_cnd_monotonic *cond, mtx_t *mtx,
                              const struct timespec *abs_time);
int u_cnd_monotonic_wait(struct u_cnd_monotonic *cond, mtx_t *mtx);

#ifdef __cplusplus
}
#endif

#endif
