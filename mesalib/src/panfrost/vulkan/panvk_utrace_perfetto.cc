/*
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include "panvk_utrace_perfetto.h"

#include <functional>
#include <perfetto.h>

#include "c11/threads.h"
#include "util/log.h"
#include "util/perf/u_perfetto.h"
#include "util/perf/u_perfetto_renderpass.h"
#include "util/timespec.h"
#include "util/u_process.h"

#include "panvk_device.h"
#include "panvk_tracepoints.h"
#include "panvk_tracepoints_perfetto.h"
#include "panvk_utrace.h"

struct PanVKRenderpassIncrementalState {
   bool was_cleared = true;
};

struct PanVKRenderpassTraits : public perfetto::DefaultDataSourceTraits {
   using IncrementalStateType = PanVKRenderpassIncrementalState;
};

class PanVKRenderpassDataSource
    : public MesaRenderpassDataSource<PanVKRenderpassDataSource,
                                      PanVKRenderpassTraits> {};

PERFETTO_DECLARE_DATA_SOURCE_STATIC_MEMBERS(PanVKRenderpassDataSource);
PERFETTO_DEFINE_DATA_SOURCE_STATIC_MEMBERS(PanVKRenderpassDataSource);

static const char *
get_stage_name(enum panvk_utrace_perfetto_stage stage)
{
   switch (stage) {
#define CASE(x)                                                                \
   case PANVK_UTRACE_PERFETTO_STAGE_##x:                                       \
      return #x
      CASE(CMDBUF);
#undef CASE
   default:
      unreachable("bad stage");
   }
}

static void
emit_interned_data_packet(struct panvk_device *dev,
                          PanVKRenderpassDataSource::TraceContext &ctx,
                          uint64_t now)
{
   const struct panvk_utrace_perfetto *utp = &dev->utrace.utp;

   auto packet = ctx.NewTracePacket();
   packet->set_timestamp(now);
   packet->set_sequence_flags(
      perfetto::protos::pbzero::TracePacket::SEQ_INCREMENTAL_STATE_CLEARED);

   auto interned_data = packet->set_interned_data();

   for (uint32_t i = 0; i < ARRAY_SIZE(utp->queue_iids); i++) {
      char name[64];
      snprintf(name, sizeof(name), "%s-queue-%d", util_get_process_name(), i);

      auto specs = interned_data->add_gpu_specifications();
      specs->set_iid(utp->queue_iids[i]);
      specs->set_name(name);
   }

   for (uint32_t i = 0; i < ARRAY_SIZE(utp->stage_iids); i++) {
      auto specs = interned_data->add_gpu_specifications();
      specs->set_iid(utp->stage_iids[i]);
      specs->set_name(get_stage_name((enum panvk_utrace_perfetto_stage)i));
   }
}

static uint64_t
get_gpu_time_ns(struct panvk_device *dev)
{
   const struct panvk_physical_device *pdev =
      to_panvk_physical_device(dev->vk.physical);
   const struct pan_kmod_dev_props *props = &pdev->kmod.props;

   const uint64_t ts = pan_kmod_query_timestamp(dev->kmod.dev);
   return ts * NSEC_PER_SEC / props->timestamp_frequency;
}

static void
emit_clock_snapshot_packet(struct panvk_device *dev,
                           PanVKRenderpassDataSource::TraceContext &ctx)
{
   const struct panvk_utrace_perfetto *utp = &dev->utrace.utp;
   const uint64_t gpu_ns = get_gpu_time_ns(dev);
   const uint64_t cpu_ns = perfetto::base::GetBootTimeNs().count();

   MesaRenderpassDataSource<PanVKRenderpassDataSource, PanVKRenderpassTraits>::
      EmitClockSync(ctx, cpu_ns, gpu_ns, utp->gpu_clock_id);
}

static void
emit_setup_packets(struct panvk_device *dev,
                   PanVKRenderpassDataSource::TraceContext &ctx)
{
   struct panvk_utrace_perfetto *utp = &dev->utrace.utp;

   const uint64_t now = perfetto::base::GetBootTimeNs().count();

   /* emit interned data if cleared */
   auto state = ctx.GetIncrementalState();
   if (state->was_cleared) {
      emit_interned_data_packet(dev, ctx, now);

      state->was_cleared = false;
      utp->next_clock_snapshot = 0;
   }

   /* emit clock snapshots periodically */
   if (now >= utp->next_clock_snapshot) {
      emit_clock_snapshot_packet(dev, ctx);

      utp->next_clock_snapshot = now + NSEC_PER_SEC;
   }
}

static struct panvk_utrace_perfetto_event *
begin_event(struct panvk_device *dev,
            const struct panvk_utrace_flush_data *data,
            enum panvk_utrace_perfetto_stage stage)
{
   struct panvk_utrace_perfetto *utp = &dev->utrace.utp;
   struct panvk_utrace_perfetto_queue *queue = &utp->queues[data->subqueue];
   struct panvk_utrace_perfetto_event *ev = &queue->stack[queue->stack_depth++];

   assert(data->subqueue < PANVK_UTRACE_PERFETTO_QUEUE_COUNT);

   if (queue->stack_depth > PANVK_UTRACE_PERFETTO_STACK_DEPTH) {
      PERFETTO_ELOG("queue %d stage %d too deep", data->subqueue, stage);
      return NULL;
   }

   ev->stage = stage;
   return ev;
}

static struct panvk_utrace_perfetto_event *
end_event(struct panvk_device *dev, const struct panvk_utrace_flush_data *data,
          enum panvk_utrace_perfetto_stage stage)
{
   struct panvk_utrace_perfetto *utp = &dev->utrace.utp;
   struct panvk_utrace_perfetto_queue *queue = &utp->queues[data->subqueue];

   assert(data->subqueue < PANVK_UTRACE_PERFETTO_QUEUE_COUNT);

   if (!queue->stack_depth)
      return NULL;

   struct panvk_utrace_perfetto_event *ev = &queue->stack[--queue->stack_depth];
   if (queue->stack_depth >= PANVK_UTRACE_PERFETTO_STACK_DEPTH)
      return NULL;

   assert(ev->stage == stage);
   return ev;
}

static void
panvk_utrace_perfetto_begin_event(struct panvk_device *dev,
                                  const struct panvk_utrace_flush_data *data,
                                  enum panvk_utrace_perfetto_stage stage,
                                  uint64_t ts_ns)
{
   struct panvk_utrace_perfetto_event *ev = begin_event(dev, data, stage);
   if (!ev)
      return;

   ev->begin_ns = ts_ns;
}

static void
panvk_utrace_perfetto_end_event(
   struct panvk_device *dev, const struct panvk_utrace_flush_data *data,
   enum panvk_utrace_perfetto_stage stage, uint64_t ts_ns,
   std::function<void(perfetto::protos::pbzero::GpuRenderStageEvent *)>
      emit_event_extra)
{
   const struct panvk_utrace_perfetto_event *ev = end_event(dev, data, stage);
   if (!ev)
      return;

   PanVKRenderpassDataSource::Trace(
      [=](PanVKRenderpassDataSource::TraceContext ctx) {
         struct panvk_utrace_perfetto *utp = &dev->utrace.utp;

         emit_setup_packets(dev, ctx);

         auto packet = ctx.NewTracePacket();
         packet->set_timestamp(ev->begin_ns);
         packet->set_timestamp_clock_id(utp->gpu_clock_id);

         auto event = packet->set_gpu_render_stage_event();
         event->set_event_id(utp->event_id++);
         event->set_duration(ts_ns - ev->begin_ns);
         event->set_hw_queue_iid(utp->queue_iids[data->subqueue]);
         event->set_stage_iid(utp->stage_iids[stage]);
         event->set_context(utp->device_id);

         emit_event_extra(event);
      });
}

#define PANVK_UTRACE_PERFETTO_PROCESS_EVENT(tp, stage)                         \
   void panvk_utrace_perfetto_begin_##tp(                                      \
      struct panvk_device *dev, uint64_t ts_ns, uint16_t tp_idx,               \
      const void *flush_data, const struct trace_begin_##tp *payload,          \
      const void *indirect_data)                                               \
   {                                                                           \
      /* we can ignore them or save them if we choose to */                    \
      assert(!payload && !indirect_data);                                      \
      panvk_utrace_perfetto_begin_event(                                       \
         dev, (const struct panvk_utrace_flush_data *)flush_data,              \
         PANVK_UTRACE_PERFETTO_STAGE_##stage, ts_ns);                          \
   }                                                                           \
                                                                               \
   void panvk_utrace_perfetto_end_##tp(                                        \
      struct panvk_device *dev, uint64_t ts_ns, uint16_t tp_idx,               \
      const void *flush_data, const struct trace_end_##tp *payload,            \
      const void *indirect_data)                                               \
   {                                                                           \
      auto emit_event_extra =                                                  \
         [=](perfetto::protos::pbzero::GpuRenderStageEvent *event) {           \
            trace_payload_as_extra_end_##tp(event, payload, indirect_data);    \
         };                                                                    \
      panvk_utrace_perfetto_end_event(                                         \
         dev, (const struct panvk_utrace_flush_data *)flush_data,              \
         PANVK_UTRACE_PERFETTO_STAGE_##stage, ts_ns, emit_event_extra);        \
   }

/* u_trace_context_process dispatches trace events to a background thread
 * (traceq) for processing.  These callbacks are called from traceq.
 */
PANVK_UTRACE_PERFETTO_PROCESS_EVENT(cmdbuf, CMDBUF)

static uint32_t
get_gpu_clock_id(void)
{
   /* see https://perfetto.dev/docs/concepts/clock-sync */
   return _mesa_hash_string("org.freedesktop.mesa.panfrost") | 0x80000000;
}

static void
register_data_source(void)
{
   perfetto::DataSourceDescriptor dsd;
   dsd.set_name("gpu.renderstages.panfrost");
   PanVKRenderpassDataSource::Register(dsd);
}

void
panvk_utrace_perfetto_init(struct panvk_device *dev, uint32_t queue_count)
{
   const struct panvk_physical_device *pdev =
      to_panvk_physical_device(dev->vk.physical);
   const struct pan_kmod_dev_props *props = &pdev->kmod.props;
   struct panvk_utrace_perfetto *utp = &dev->utrace.utp;

   if (queue_count > PANVK_UTRACE_PERFETTO_QUEUE_COUNT) {
      assert(!"PANVK_UTRACE_PERFETTO_QUEUE_COUNT too small");
      return;
   }

   /* check for timestamp support */
   if (!props->gpu_can_query_timestamp || !props->timestamp_frequency ||
       !get_gpu_time_ns(dev))
      return;

   utp->gpu_clock_id = get_gpu_clock_id();
   utp->device_id = (uintptr_t)dev;

   uint64_t next_iid = 1;
   for (uint32_t i = 0; i < ARRAY_SIZE(utp->queue_iids); i++)
      utp->queue_iids[i] = next_iid++;
   for (uint32_t i = 0; i < ARRAY_SIZE(utp->stage_iids); i++)
      utp->stage_iids[i] = next_iid++;

   util_perfetto_init();

   static once_flag register_ds_once = ONCE_FLAG_INIT;
   call_once(&register_ds_once, register_data_source);
}
