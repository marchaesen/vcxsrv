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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "u_perfetto.h"

#include <perfetto.h>

#include "c11/threads.h"
#include "util/macros.h"

/* perfetto requires string literals */
#define UTIL_PERFETTO_CATEGORY_DEFAULT_STR "mesa.default"
#define UTIL_PERFETTO_CATEGORY_SLOW_STR "mesa.slow"

PERFETTO_DEFINE_CATEGORIES(
   perfetto::Category(UTIL_PERFETTO_CATEGORY_DEFAULT_STR)
      .SetDescription("Mesa default events"),
   perfetto::Category(UTIL_PERFETTO_CATEGORY_SLOW_STR)
      .SetDescription("Mesa slow events")
      .SetTags("slow"));

PERFETTO_TRACK_EVENT_STATIC_STORAGE();

int util_perfetto_category_states[UTIL_PERFETTO_CATEGORY_COUNT];

static void
util_perfetto_update_category_states(void)
{
#define UPDATE_CATEGORY(cat)                                                 \
   p_atomic_set(                                                             \
      &util_perfetto_category_states[UTIL_PERFETTO_CATEGORY_##cat],          \
      TRACE_EVENT_CATEGORY_ENABLED(UTIL_PERFETTO_CATEGORY_##cat##_STR))
   UPDATE_CATEGORY(DEFAULT);
   UPDATE_CATEGORY(SLOW);
#undef UPDATE_CATEGORY
}

void
util_perfetto_trace_begin(enum util_perfetto_category category,
                          const char *name)
{
#define TRACE_BEGIN(cat, name)                                               \
   TRACE_EVENT_BEGIN(                                                        \
      UTIL_PERFETTO_CATEGORY_##cat##_STR, nullptr,                           \
      [&](perfetto::EventContext ctx) { ctx.event()->set_name(name); })
   switch (category) {
   case UTIL_PERFETTO_CATEGORY_DEFAULT:
      TRACE_BEGIN(DEFAULT, name);
      break;
   case UTIL_PERFETTO_CATEGORY_SLOW:
      TRACE_BEGIN(SLOW, name);
      break;
   default:
      unreachable("bad perfetto category");
   }
#undef TRACE_BEGIN
}

void
util_perfetto_trace_end(enum util_perfetto_category category)
{
#define TRACE_END(cat) TRACE_EVENT_END(UTIL_PERFETTO_CATEGORY_##cat##_STR)
   switch (category) {
   case UTIL_PERFETTO_CATEGORY_DEFAULT:
      TRACE_END(DEFAULT);
      break;
   case UTIL_PERFETTO_CATEGORY_SLOW:
      TRACE_END(SLOW);
      break;
   default:
      unreachable("bad perfetto category");
   }
#undef TRACE_END

   util_perfetto_update_category_states();
}

class UtilPerfettoObserver : public perfetto::TrackEventSessionObserver {
 public:
   UtilPerfettoObserver() { perfetto::TrackEvent::AddSessionObserver(this); }

   void OnStart(const perfetto::DataSourceBase::StartArgs &) override
   {
      util_perfetto_update_category_states();
   }

   /* XXX There is no PostStop callback.  We have to call
    * util_perfetto_update_category_states occasionally to poll.
    */
};

static void
util_perfetto_fini(void)
{
   perfetto::Tracing::Shutdown();
}

static void
util_perfetto_init_once(void)
{
   // Connects to the system tracing service
   perfetto::TracingInitArgs args;
   args.backends = perfetto::kSystemBackend;
   perfetto::Tracing::Initialize(args);

   static UtilPerfettoObserver observer;
   perfetto::TrackEvent::Register();

   atexit(&util_perfetto_fini);
}

static once_flag perfetto_once_flag = ONCE_FLAG_INIT;

void
util_perfetto_init(void)
{
   call_once(&perfetto_once_flag, util_perfetto_init_once);
}
