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

#include "util/u_perfetto.h"

#include "freedreno_tracepoints.h"

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

struct FdRenderpassIncrementalState {
   bool was_cleared = true;
};

struct FdRenderpassTraits : public perfetto::DefaultDataSourceTraits {
   using IncrementalStateType = FdRenderpassIncrementalState;
};

class FdRenderpassDataSource : public perfetto::DataSource<FdRenderpassDataSource, FdRenderpassTraits> {
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
   }

   void OnStop(const StopArgs &) override
   {
      PERFETTO_LOG("Tracing stopped");

      // Undo any initialization done in OnStart.
      u_trace_perfetto_stop();
      // TODO we should perhaps block until queued traces are flushed?

      Trace([](FdRenderpassDataSource::TraceContext ctx) {
         auto packet = ctx.NewTracePacket();
         packet->Finalize();
         ctx.Flush();
      });
   }
};

PERFETTO_DECLARE_DATA_SOURCE_STATIC_MEMBERS(FdRenderpassDataSource);
PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(FdRenderpassDataSource);

static void
send_descriptors(FdRenderpassDataSource::TraceContext &ctx, uint64_t ts_ns)
{
   PERFETTO_LOG("Sending renderstage descriptors");

   auto packet = ctx.NewTracePacket();

   packet->set_timestamp(0);
//   packet->set_timestamp(ts_ns);
//   packet->set_timestamp_clock_id(gpu_clock_id);

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
stage_start(struct pipe_context *pctx, uint64_t ts_ns, enum fd_stage_id stage)
{
   struct fd_context *ctx = fd_context(pctx);
   struct fd_perfetto_state *p = &ctx->perfetto;

   p->start_ts[stage] = ts_ns;
}

static void
stage_end(struct pipe_context *pctx, uint64_t ts_ns, enum fd_stage_id stage)
{
   struct fd_context *ctx = fd_context(pctx);
   struct fd_perfetto_state *p = &ctx->perfetto;

   /* If we haven't managed to calibrate the alignment between GPU and CPU
    * timestamps yet, then skip this trace, otherwise perfetto won't know
    * what to do with it.
    */
   if (!sync_gpu_ts)
      return;

   FdRenderpassDataSource::Trace([=](FdRenderpassDataSource::TraceContext tctx) {
      if (auto state = tctx.GetIncrementalState(); state->was_cleared) {
         send_descriptors(tctx, p->start_ts[stage]);
         state->was_cleared = false;
      }

      auto packet = tctx.NewTracePacket();

      packet->set_timestamp(p->start_ts[stage]);
      packet->set_timestamp_clock_id(gpu_clock_id);

      auto event = packet->set_gpu_render_stage_event();
      event->set_event_id(0); // ???
      event->set_hw_queue_id(DEFAULT_HW_QUEUE_ID);
      event->set_duration(ts_ns - p->start_ts[stage]);
      event->set_stage_id(stage);
      event->set_context((uintptr_t)pctx);

      /* The "surface" meta-stage has extra info about render target: */
      if (stage == SURFACE_STAGE_ID) {

         event->set_submission_id(p->submit_id);

         if (p->cbuf0_format) {
            auto data = event->add_extra_data();

            data->set_name("color0 format");
            data->set_value(util_format_short_name(p->cbuf0_format));
         }

         if (p->zs_format) {
            auto data = event->add_extra_data();

            data->set_name("zs format");
            data->set_value(util_format_short_name(p->zs_format));
         }

         {
            auto data = event->add_extra_data();

            data->set_name("width");
            data->set_value(std::to_string(p->width));
         }

         {
            auto data = event->add_extra_data();

            data->set_name("height");
            data->set_value(std::to_string(p->height));
         }

         {
            auto data = event->add_extra_data();

            data->set_name("MSAA");
            data->set_value(std::to_string(p->samples));
         }

         {
            auto data = event->add_extra_data();

            data->set_name("MRTs");
            data->set_value(std::to_string(p->mrts));
         }

         // "renderMode"
         // "surfaceID"

         if (p->nbins) {
            auto data = event->add_extra_data();

            data->set_name("numberOfBins");
            data->set_value(std::to_string(p->nbins));
         }

         if (p->binw) {
            auto data = event->add_extra_data();

            data->set_name("binWidth");
            data->set_value(std::to_string(p->binw));
         }

         if (p->binh) {
            auto data = event->add_extra_data();

            data->set_name("binHeight");
            data->set_value(std::to_string(p->binh));
         }
      }
   });
}

#ifdef __cplusplus
extern "C" {
#endif

void
fd_perfetto_init(void)
{
   util_perfetto_init();

   perfetto::DataSourceDescriptor dsd;
   dsd.set_name("gpu.renderstages.msm");
   FdRenderpassDataSource::Register(dsd);
}

static void
sync_timestamp(struct fd_context *ctx)
{
   uint64_t cpu_ts = perfetto::base::GetBootTimeNs().count();
   uint64_t gpu_ts;

   if (cpu_ts < next_clock_sync_ns)
      return;

   if (fd_pipe_get_param(ctx->pipe, FD_TIMESTAMP, &gpu_ts)) {
      PERFETTO_ELOG("Could not sync CPU and GPU clocks");
      return;
   }

   /* convert GPU ts into ns: */
   gpu_ts = ctx->ts_to_ns(gpu_ts);

   FdRenderpassDataSource::Trace([=](FdRenderpassDataSource::TraceContext tctx) {
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
emit_submit_id(struct fd_context *ctx)
{
   FdRenderpassDataSource::Trace([=](FdRenderpassDataSource::TraceContext tctx) {
      auto packet = tctx.NewTracePacket();

      packet->set_timestamp(perfetto::base::GetBootTimeNs().count());

      auto event = packet->set_vulkan_api_event();
      auto submit = event->set_vk_queue_submit();

      submit->set_submission_id(ctx->submit_count);
   });
}

void
fd_perfetto_submit(struct fd_context *ctx)
{
   sync_timestamp(ctx);
   emit_submit_id(ctx);
}

/*
 * Trace callbacks, called from u_trace once the timestamps from GPU have been
 * collected.
 */

void
fd_start_render_pass(struct pipe_context *pctx, uint64_t ts_ns,
                     const void *flush_data,
                     const struct trace_start_render_pass *payload)
{
   stage_start(pctx, ts_ns, SURFACE_STAGE_ID);

   struct fd_perfetto_state *p = &fd_context(pctx)->perfetto;

   p->submit_id = payload->submit_id;
   p->cbuf0_format = payload->cbuf0_format;
   p->zs_format = payload->zs_format;
   p->width = payload->width;
   p->height = payload->height;
   p->mrts = payload->mrts;
   p->samples = payload->samples;
   p->nbins = payload->nbins;
   p->binw = payload->binw;
   p->binh = payload->binh;
}

void
fd_end_render_pass(struct pipe_context *pctx, uint64_t ts_ns,
                   const void *flush_data,
                   const struct trace_end_render_pass *payload)
{
   stage_end(pctx, ts_ns, SURFACE_STAGE_ID);
}

void
fd_start_binning_ib(struct pipe_context *pctx, uint64_t ts_ns,
                    const void *flush_data,
                    const struct trace_start_binning_ib *payload)
{
   stage_start(pctx, ts_ns, BINNING_STAGE_ID);
}

void
fd_end_binning_ib(struct pipe_context *pctx, uint64_t ts_ns,
                  const void *flush_data,
                  const struct trace_end_binning_ib *payload)
{
   stage_end(pctx, ts_ns, BINNING_STAGE_ID);
}

void
fd_start_draw_ib(struct pipe_context *pctx, uint64_t ts_ns,
                 const void *flush_data,
                 const struct trace_start_draw_ib *payload)
{
   stage_start(
      pctx, ts_ns,
      fd_context(pctx)->perfetto.nbins ? GMEM_STAGE_ID : BYPASS_STAGE_ID);
}

void
fd_end_draw_ib(struct pipe_context *pctx, uint64_t ts_ns,
               const void *flush_data,
               const struct trace_end_draw_ib *payload)
{
   stage_end(
      pctx, ts_ns,
      fd_context(pctx)->perfetto.nbins ? GMEM_STAGE_ID : BYPASS_STAGE_ID);
}

void
fd_start_blit(struct pipe_context *pctx, uint64_t ts_ns,
              const void *flush_data,
              const struct trace_start_blit *payload)
{
   stage_start(pctx, ts_ns, BLIT_STAGE_ID);
}

void
fd_end_blit(struct pipe_context *pctx, uint64_t ts_ns,
            const void *flush_data,
            const struct trace_end_blit *payload)
{
   stage_end(pctx, ts_ns, BLIT_STAGE_ID);
}

void
fd_start_compute(struct pipe_context *pctx, uint64_t ts_ns,
                 const void *flush_data,
                 const struct trace_start_compute *payload)
{
   stage_start(pctx, ts_ns, COMPUTE_STAGE_ID);
}

void
fd_end_compute(struct pipe_context *pctx, uint64_t ts_ns,
               const void *flush_data,
               const struct trace_end_compute *payload)
{
   stage_end(pctx, ts_ns, COMPUTE_STAGE_ID);
}

void
fd_start_clear_restore(struct pipe_context *pctx, uint64_t ts_ns,
                       const void *flush_data,
                       const struct trace_start_clear_restore *payload)
{
   stage_start(pctx, ts_ns, CLEAR_RESTORE_STAGE_ID);
}

void
fd_end_clear_restore(struct pipe_context *pctx, uint64_t ts_ns,
                     const void *flush_data,
                     const struct trace_end_clear_restore *payload)
{
   stage_end(pctx, ts_ns, CLEAR_RESTORE_STAGE_ID);
}

void
fd_start_resolve(struct pipe_context *pctx, uint64_t ts_ns,
                 const void *flush_data,
                 const struct trace_start_resolve *payload)
{
   stage_start(pctx, ts_ns, RESOLVE_STAGE_ID);
}

void
fd_end_resolve(struct pipe_context *pctx, uint64_t ts_ns,
               const void *flush_data,
               const struct trace_end_resolve *payload)
{
   stage_end(pctx, ts_ns, RESOLVE_STAGE_ID);
}

#ifdef __cplusplus
}
#endif
