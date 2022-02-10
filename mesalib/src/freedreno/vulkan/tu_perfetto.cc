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

#include <perfetto.h>

#include "tu_perfetto.h"

#include "util/u_perfetto.h"
#include "util/hash_table.h"

#include "tu_tracepoints.h"
#include "tu_tracepoints_perfetto.h"

static uint32_t gpu_clock_id;
static uint64_t next_clock_sync_ns; /* cpu time of next clk sync */

/**
 * The timestamp at the point where we first emitted the clock_sync..
 * this  will be a *later* timestamp that the first GPU traces (since
 * we capture the first clock_sync from the CPU *after* the first GPU
 * tracepoints happen).  To avoid confusing perfetto we need to drop
 * the GPU traces with timestamps before this.
 */
static uint64_t sync_gpu_ts;

static uint64_t last_suspend_count;

static uint64_t gpu_max_timestamp;
static uint64_t gpu_timestamp_offset;

struct TuRenderpassIncrementalState {
   bool was_cleared = true;
};

struct TuRenderpassTraits : public perfetto::DefaultDataSourceTraits {
   using IncrementalStateType = TuRenderpassIncrementalState;
};

class TuRenderpassDataSource : public perfetto::DataSource<TuRenderpassDataSource, TuRenderpassTraits> {
public:
   void OnSetup(const SetupArgs &) override
   {
      // Use this callback to apply any custom configuration to your data source
      // based on the TraceConfig in SetupArgs.
   }

   void OnStart(const StartArgs &) override
   {
      // This notification can be used to initialize the GPU driver, enable
      // counters, etc. StartArgs will contains the DataSourceDescriptor,
      // which can be extended.
      u_trace_perfetto_start();
      PERFETTO_LOG("Tracing started");

      /* Note: clock_id's below 128 are reserved.. for custom clock sources,
       * using the hash of a namespaced string is the recommended approach.
       * See: https://perfetto.dev/docs/concepts/clock-sync
       */
      gpu_clock_id =
         _mesa_hash_string("org.freedesktop.mesa.freedreno") | 0x80000000;

      gpu_timestamp_offset = 0;
      gpu_max_timestamp = 0;
      last_suspend_count = 0;
   }

   void OnStop(const StopArgs &) override
   {
      PERFETTO_LOG("Tracing stopped");

      // Undo any initialization done in OnStart.
      u_trace_perfetto_stop();
      // TODO we should perhaps block until queued traces are flushed?

      Trace([](TuRenderpassDataSource::TraceContext ctx) {
         auto packet = ctx.NewTracePacket();
         packet->Finalize();
         ctx.Flush();
      });
   }
};

PERFETTO_DECLARE_DATA_SOURCE_STATIC_MEMBERS(TuRenderpassDataSource);
PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(TuRenderpassDataSource);

static void
send_descriptors(TuRenderpassDataSource::TraceContext &ctx, uint64_t ts_ns)
{
   PERFETTO_LOG("Sending renderstage descriptors");

   auto packet = ctx.NewTracePacket();

   packet->set_timestamp(0);

   auto event = packet->set_gpu_render_stage_event();
   event->set_gpu_id(0);

   auto spec = event->set_specifications();

   for (unsigned i = 0; i < ARRAY_SIZE(queues); i++) {
      auto desc = spec->add_hw_queue();

      desc->set_name(queues[i].name);
      desc->set_description(queues[i].desc);
   }

   for (unsigned i = 0; i < ARRAY_SIZE(stages); i++) {
      auto desc = spec->add_stage();

      desc->set_name(stages[i].name);
      if (stages[i].desc)
         desc->set_description(stages[i].desc);
   }
}

static void
stage_start(struct tu_device *dev, uint64_t ts_ns, enum tu_stage_id stage)
{
   struct tu_perfetto_state *p = tu_device_get_perfetto_state(dev);

   p->start_ts[stage] = ts_ns;
}

typedef void (*trace_payload_as_extra_func)(perfetto::protos::pbzero::GpuRenderStageEvent *, const void*);

static void
stage_end(struct tu_device *dev, uint64_t ts_ns, enum tu_stage_id stage,
          uint32_t submission_id, const void* payload = nullptr,
          trace_payload_as_extra_func payload_as_extra = nullptr)
{
   struct tu_perfetto_state *p = tu_device_get_perfetto_state(dev);

   /* If we haven't managed to calibrate the alignment between GPU and CPU
    * timestamps yet, then skip this trace, otherwise perfetto won't know
    * what to do with it.
    */
   if (!sync_gpu_ts)
      return;

   TuRenderpassDataSource::Trace([=](TuRenderpassDataSource::TraceContext tctx) {
      if (auto state = tctx.GetIncrementalState(); state->was_cleared) {
         send_descriptors(tctx, p->start_ts[stage]);
         state->was_cleared = false;
      }

      auto packet = tctx.NewTracePacket();

      gpu_max_timestamp = MAX2(gpu_max_timestamp, ts_ns + gpu_timestamp_offset);

      packet->set_timestamp(p->start_ts[stage] + gpu_timestamp_offset);
      packet->set_timestamp_clock_id(gpu_clock_id);

      auto event = packet->set_gpu_render_stage_event();
      event->set_event_id(0); // ???
      event->set_hw_queue_id(DEFAULT_HW_QUEUE_ID);
      event->set_duration(ts_ns - p->start_ts[stage]);
      event->set_stage_id(stage);
      event->set_context((uintptr_t)dev);
      event->set_submission_id(submission_id);

      if (payload && payload_as_extra) {
         payload_as_extra(event, payload);
      }
   });
}

#ifdef __cplusplus
extern "C" {
#endif

void
tu_perfetto_init(void)
{
   util_perfetto_init();

   perfetto::DataSourceDescriptor dsd;
   dsd.set_name("gpu.renderstages.msm");
   TuRenderpassDataSource::Register(dsd);
}

static void
sync_timestamp(struct tu_device *dev)
{
   uint64_t cpu_ts = perfetto::base::GetBootTimeNs().count();
   uint64_t gpu_ts = 0;

   if (cpu_ts < next_clock_sync_ns)
      return;

    if (tu_device_get_gpu_timestamp(dev, &gpu_ts)) {
      PERFETTO_ELOG("Could not sync CPU and GPU clocks");
      return;
    }

   uint64_t current_suspend_count = 0;
   /* If we fail to get it we will use a fallback */
   tu_device_get_suspend_count(dev, &current_suspend_count);

   /* convert GPU ts into ns: */
   gpu_ts = tu_device_ticks_to_ns(dev, gpu_ts);

   /* GPU timestamp is being reset after suspend-resume cycle.
    * Perfetto requires clock snapshots to be monotonic,
    * so we have to fix-up the time.
    */
   if (current_suspend_count != last_suspend_count) {
      gpu_timestamp_offset = gpu_max_timestamp;
      last_suspend_count = current_suspend_count;
   }

   gpu_ts += gpu_timestamp_offset;

   /* Fallback check, detect non-monotonic cases which would happen
    * if we cannot retrieve suspend count.
    */
   if (sync_gpu_ts > gpu_ts) {
      gpu_ts += (gpu_max_timestamp - gpu_timestamp_offset);
      gpu_timestamp_offset = gpu_max_timestamp;
   }

   if (sync_gpu_ts > gpu_ts) {
      PERFETTO_ELOG("Non-monotonic gpu timestamp detected, bailing out");
      return;
   }

   gpu_max_timestamp = gpu_ts;

   TuRenderpassDataSource::Trace([=](TuRenderpassDataSource::TraceContext tctx) {
      auto packet = tctx.NewTracePacket();

      packet->set_timestamp(cpu_ts);

      auto event = packet->set_clock_snapshot();

      {
         auto clock = event->add_clocks();

         clock->set_clock_id(perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME);
         clock->set_timestamp(cpu_ts);
      }

      {
         auto clock = event->add_clocks();

         clock->set_clock_id(gpu_clock_id);
         clock->set_timestamp(gpu_ts);
      }

      sync_gpu_ts = gpu_ts;
      next_clock_sync_ns = cpu_ts + 30000000;
   });
}

static void
emit_submit_id(uint32_t submission_id)
{
   TuRenderpassDataSource::Trace([=](TuRenderpassDataSource::TraceContext tctx) {
      auto packet = tctx.NewTracePacket();

      packet->set_timestamp(perfetto::base::GetBootTimeNs().count());

      auto event = packet->set_vulkan_api_event();
      auto submit = event->set_vk_queue_submit();

      submit->set_submission_id(submission_id);
   });
}

void
tu_perfetto_submit(struct tu_device *dev, uint32_t submission_id)
{
   /* sync_timestamp isn't free */
   if (!ut_perfetto_enabled)
      return;

   sync_timestamp(dev);
   emit_submit_id(submission_id);
}

/*
 * Trace callbacks, called from u_trace once the timestamps from GPU have been
 * collected.
 */

#define CREATE_EVENT_CALLBACK(event_name, stage)                              \
void                                                                          \
tu_start_##event_name(struct tu_device *dev, uint64_t ts_ns,                  \
                   const void *flush_data,                                    \
                   const struct trace_start_##event_name *payload)            \
{                                                                             \
   stage_start(dev, ts_ns, stage);                                            \
}                                                                             \
                                                                              \
void                                                                          \
tu_end_##event_name(struct tu_device *dev, uint64_t ts_ns,                    \
                   const void *flush_data,                                    \
                   const struct trace_end_##event_name *payload)              \
{                                                                             \
   auto trace_flush_data = (const struct tu_u_trace_submission_data *) flush_data; \
   uint32_t submission_id =                                                        \
      tu_u_trace_submission_data_get_submit_id(trace_flush_data);                  \
   stage_end(dev, ts_ns, stage, submission_id, payload,                            \
      (trace_payload_as_extra_func) &trace_payload_as_extra_end_##event_name);     \
}

CREATE_EVENT_CALLBACK(render_pass, SURFACE_STAGE_ID)
CREATE_EVENT_CALLBACK(binning_ib, BINNING_STAGE_ID)
CREATE_EVENT_CALLBACK(draw_ib_gmem, GMEM_STAGE_ID)
CREATE_EVENT_CALLBACK(draw_ib_sysmem, BYPASS_STAGE_ID)
CREATE_EVENT_CALLBACK(blit, BLIT_STAGE_ID)
CREATE_EVENT_CALLBACK(compute, COMPUTE_STAGE_ID)
CREATE_EVENT_CALLBACK(gmem_clear, CLEAR_GMEM_STAGE_ID)
CREATE_EVENT_CALLBACK(sysmem_clear, CLEAR_SYSMEM_STAGE_ID)
CREATE_EVENT_CALLBACK(sysmem_clear_all, CLEAR_SYSMEM_STAGE_ID)
CREATE_EVENT_CALLBACK(gmem_load, GMEM_LOAD_STAGE_ID)
CREATE_EVENT_CALLBACK(gmem_store, GMEM_STORE_STAGE_ID)
CREATE_EVENT_CALLBACK(sysmem_resolve, SYSMEM_RESOLVE_STAGE_ID)

#ifdef __cplusplus
}
#endif
