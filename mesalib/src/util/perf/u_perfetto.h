/*
 * Copyright © 2021 Google, Inc.
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
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#ifndef _UTIL_PERFETTO_H
#define _UTIL_PERFETTO_H

#include "util/u_atomic.h"
#include "util/detect_os.h"

// On Unix, pass a clockid_t to designate which clock was used to gather the timestamp
// On Windows, this paramter is ignored, and it's expected that `timestamp` comes from QueryPerformanceCounter
#if DETECT_OS_POSIX
#include <time.h>
typedef clockid_t perfetto_clock_id;
#else
typedef int32_t perfetto_clock_id;
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifdef HAVE_PERFETTO

extern int util_perfetto_tracing_state;

void util_perfetto_init(void);

static inline bool
util_perfetto_is_tracing_enabled(void)
{
   return p_atomic_read_relaxed(&util_perfetto_tracing_state);
}

void util_perfetto_trace_begin(const char *name);

void util_perfetto_trace_end(void);

void util_perfetto_trace_begin_flow(const char *fname, uint64_t id);

void util_perfetto_counter_set(const char *name, double value);

void util_perfetto_trace_full_begin(const char *name, uint64_t track_id, uint64_t id, perfetto_clock_id clock, uint64_t timestamp);

void util_perfetto_trace_full_end(const char *name, uint64_t track_id, perfetto_clock_id clock, uint64_t timestamp);

uint64_t util_perfetto_next_id(void);

uint64_t util_perfetto_new_track(const char *name);

#else /* HAVE_PERFETTO */

static inline void
util_perfetto_init(void)
{
}

static inline bool
util_perfetto_is_tracing_enabled(void)
{
   return false;
}

static inline void
util_perfetto_trace_begin(const char *name)
{
}

static inline void
util_perfetto_trace_end(void)
{
}

static inline void util_perfetto_trace_begin_flow(const char *fname, uint64_t id)
{
}

static inline void
util_perfetto_trace_full_begin(const char *name, uint64_t track_id, uint64_t id, perfetto_clock_id clock, uint64_t timestamp)
{
}

static inline void
util_perfetto_trace_full_end(const char *name, uint64_t track_id, perfetto_clock_id clock, uint64_t timestamp)
{
}

static inline void util_perfetto_counter_set(const char *name, double value)
{
}

static inline uint64_t util_perfetto_next_id(void)
{
   return 0;
}

static inline uint64_t util_perfetto_new_track(const char *name)
{
   return 0;
}

#endif /* HAVE_PERFETTO */

#ifdef __cplusplus
}
#endif

#endif /* _UTIL_PERFETTO_H */
