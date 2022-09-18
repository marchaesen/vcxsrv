/*
 * Copyright 2022 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef CPU_TRACE_H
#define CPU_TRACE_H

#include "u_perfetto.h"

#include "util/macros.h"

#if defined(HAVE_PERFETTO)

/* note that util_perfetto_is_category_enabled always returns false util
 * util_perfetto_init is called
 */
#define _MESA_TRACE_BEGIN(category, name)                                    \
   do {                                                                      \
      if (unlikely(util_perfetto_is_category_enabled(category)))             \
         util_perfetto_trace_begin(category, name);                          \
   } while (0)

#define _MESA_TRACE_END(category)                                            \
   do {                                                                      \
      if (unlikely(util_perfetto_is_category_enabled(category)))             \
         util_perfetto_trace_end(category);                                  \
   } while (0)

/* NOTE: for now disable atrace for C++ to workaround a ndk bug with ordering
 * between stdatomic.h and atomic.h.  See:
 *
 *   https://github.com/android/ndk/issues/1178
 */
#elif defined(ANDROID) && !defined(__cplusplus)

#include <cutils/trace.h>

#define _MESA_TRACE_BEGIN(category, name)                                    \
   atrace_begin(ATRACE_TAG_GRAPHICS, name)
#define _MESA_TRACE_END(category) atrace_end(ATRACE_TAG_GRAPHICS)

#else

#define _MESA_TRACE_BEGIN(category, name)
#define _MESA_TRACE_END(category)

#endif /* HAVE_PERFETTO */

#if __has_attribute(cleanup) && __has_attribute(unused)

#define _MESA_TRACE_SCOPE_VAR_CONCAT(name, suffix) name##suffix
#define _MESA_TRACE_SCOPE_VAR(suffix)                                        \
   _MESA_TRACE_SCOPE_VAR_CONCAT(_mesa_trace_scope_, suffix)

/* This must expand to a single non-scoped statement for
 *
 *    if (cond)
 *       _MESA_TRACE_SCOPE(...)
 *
 * to work.
 */
#define _MESA_TRACE_SCOPE(category, name)                                    \
   int _MESA_TRACE_SCOPE_VAR(__LINE__)                                       \
      __attribute__((cleanup(_mesa_trace_scope_end), unused)) =              \
         _mesa_trace_scope_begin(category, name)

static inline int
_mesa_trace_scope_begin(enum util_perfetto_category category,
                        const char *name)
{
   _MESA_TRACE_BEGIN(category, name);
   return category;
}

static inline void
_mesa_trace_scope_end(int *scope)
{
   /* we save the category in the scope variable */
   _MESA_TRACE_END(*scope);
}

#else

#define _MESA_TRACE_SCOPE(category, name)

#endif /* __has_attribute(cleanup) && __has_attribute(unused) */

/* These use the default category.  Drivers or subsystems can use these, or
 * define their own categories/macros.
 */
#define MESA_TRACE_BEGIN(name)                                               \
   _MESA_TRACE_BEGIN(UTIL_PERFETTO_CATEGORY_DEFAULT, name)
#define MESA_TRACE_END() _MESA_TRACE_END(UTIL_PERFETTO_CATEGORY_DEFAULT)
#define MESA_TRACE_SCOPE(name)                                               \
   _MESA_TRACE_SCOPE(UTIL_PERFETTO_CATEGORY_DEFAULT, name)
#define MESA_TRACE_FUNC()                                                    \
   _MESA_TRACE_SCOPE(UTIL_PERFETTO_CATEGORY_DEFAULT, __func__)

/* these use the slow category */
#define MESA_TRACE_BEGIN_SLOW(name)                                          \
   _MESA_TRACE_BEGIN(UTIL_PERFETTO_CATEGORY_SLOW, name)
#define MESA_TRACE_END_SLOW() _MESA_TRACE_END(UTIL_PERFETTO_CATEGORY_SLOW)
#define MESA_TRACE_SCOPE_SLOW(name)                                          \
   _MESA_TRACE_SCOPE(UTIL_PERFETTO_CATEGORY_SLOW, name)
#define MESA_TRACE_FUNC_SLOW()                                               \
   _MESA_TRACE_SCOPE(UTIL_PERFETTO_CATEGORY_SLOW, __func__)

#endif /* CPU_TRACE_H */
