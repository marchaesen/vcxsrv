/*
 * Copyright © 2021 Google, Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include "fd_pps_driver.h"

#include <cstring>
#include <iostream>
#include <perfetto.h>

#include "pps/pps.h"
#include "pps/pps_algorithm.h"

namespace pps
{

uint64_t
FreedrenoDriver::get_min_sampling_period_ns()
{
   return 100000;
}

/*
TODO this sees like it would be largely the same for a5xx as well
(ie. same countable names)..
 */
void
FreedrenoDriver::setup_a6xx_counters()
{
   /* TODO is there a reason to want more than one group? */
   CounterGroup group = {};
   group.name = "counters";
   groups.clear();
   counters.clear();
   countables.clear();
   enabled_counters.clear();
   groups.emplace_back(std::move(group));

   /*
    * Create the countables that we'll be using.
    */

   auto PERF_CP_ALWAYS_COUNT = countable("PERF_CP_ALWAYS_COUNT");
   auto PERF_CP_BUSY_CYCLES  = countable("PERF_CP_BUSY_CYCLES");
   auto PERF_RB_3D_PIXELS    = countable("PERF_RB_3D_PIXELS");
   auto PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS = countable("PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS");
   auto PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS = countable("PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS");
   auto PERF_TP_L1_CACHELINE_MISSES = countable("PERF_TP_L1_CACHELINE_MISSES");
   auto PERF_SP_BUSY_CYCLES  = countable("PERF_SP_BUSY_CYCLES");

   /*
    * And then setup the derived counters that we are exporting to
    * pps based on the captured countable values
    */

   counter("GPU Frequency", Counter::Units::Hertz, [=]() {
         return PERF_CP_ALWAYS_COUNT / time;
      }
   );

   counter("GPU % Utilization", Counter::Units::Percent, [=]() {
         return 100.0 * (PERF_CP_BUSY_CYCLES / time) / max_freq;
      }
   );

   // This one is a bit of a guess, but seems plausible..
   counter("ALU / Fragment", Counter::Units::None, [=]() {
         return (PERF_SP_FS_STAGE_FULL_ALU_INSTRUCTIONS +
               PERF_SP_FS_STAGE_HALF_ALU_INSTRUCTIONS / 2) / PERF_RB_3D_PIXELS;
      }
   );

   counter("TP L1 Cache Misses", Counter::Units::None, [=]() {
         return PERF_TP_L1_CACHELINE_MISSES / time;
      }
   );

   counter("Shader Core Utilization", Counter::Units::Percent, [=]() {
         return 100.0 * (PERF_SP_BUSY_CYCLES / time) / (max_freq * info->num_sp_cores);
      }
   );

   // TODO add more.. see https://gpuinspector.dev/docs/gpu-counters/qualcomm
   // for what blob exposes
}

/**
 * Generate an submit the cmdstream to configure the counter/countable
 * muxing
 */
void
FreedrenoDriver::configure_counters(bool reset, bool wait)
{
   struct fd_submit *submit = fd_submit_new(pipe);
   enum fd_ringbuffer_flags flags =
      (enum fd_ringbuffer_flags)(FD_RINGBUFFER_PRIMARY | FD_RINGBUFFER_GROWABLE);
   struct fd_ringbuffer *ring = fd_submit_new_ringbuffer(submit, 0x1000, flags);

   for (auto countable : countables)
      countable.configure(ring, reset);

   struct fd_submit_fence fence = {};
   util_queue_fence_init(&fence.ready);

   fd_submit_flush(submit, -1, &fence);

   util_queue_fence_wait(&fence.ready);

   fd_ringbuffer_del(ring);
   fd_submit_del(submit);

   if (wait)
      fd_pipe_wait(pipe, &fence.fence);
}

/**
 * Read the current counter values and record the time.
 */
void
FreedrenoDriver::collect_countables()
{
   last_dump_ts = perfetto::base::GetBootTimeNs().count();

   for (auto countable : countables)
      countable.collect();
}

bool
FreedrenoDriver::init_perfcnt()
{
   uint64_t val;

   dev = fd_device_new(drm_device.fd);
   pipe = fd_pipe_new(dev, FD_PIPE_3D);
   dev_id = fd_pipe_dev_id(pipe);

   if (fd_pipe_get_param(pipe, FD_MAX_FREQ, &val)) {
      PERFETTO_FATAL("Could not get MAX_FREQ");
      return false;
   }
   max_freq = val;

   if (fd_pipe_get_param(pipe, FD_SUSPEND_COUNT, &val)) {
      PERFETTO_ILOG("Could not get SUSPEND_COUNT");
   } else {
      suspend_count = val;
      has_suspend_count = true;
   }

   perfcntrs = fd_perfcntrs(fd_pipe_dev_id(pipe), &num_perfcntrs);
   if (num_perfcntrs == 0) {
      PERFETTO_FATAL("No hw counters available");
      return false;
   }

   assigned_counters.resize(num_perfcntrs);
   assigned_counters.assign(assigned_counters.size(), 0);

   switch (fd_dev_gen(dev_id)) {
   case 6:
      setup_a6xx_counters();
      break;
   default:
      PERFETTO_FATAL("Unsupported GPU: a%03u", fd_dev_gpu_id(dev_id));
      return false;
   }

   state.resize(next_countable_id);

   for (auto countable : countables)
      countable.resolve();

   info = fd_dev_info(dev_id);

   io = fd_dt_find_io();
   if (!io) {
      PERFETTO_FATAL("Could not map GPU I/O space");
      return false;
   }

   configure_counters(true, true);
   collect_countables();

   return true;
}

void
FreedrenoDriver::enable_counter(const uint32_t counter_id)
{
   enabled_counters.push_back(counters[counter_id]);
}

void
FreedrenoDriver::enable_all_counters()
{
   enabled_counters.reserve(counters.size());
   for (auto &counter : counters) {
      enabled_counters.push_back(counter);
   }
}

void
FreedrenoDriver::enable_perfcnt(const uint64_t /* sampling_period_ns */)
{
}

bool
FreedrenoDriver::dump_perfcnt()
{
   if (has_suspend_count) {
      uint64_t val;

      fd_pipe_get_param(pipe, FD_SUSPEND_COUNT, &val);

      if (suspend_count != val) {
         PERFETTO_ILOG("Device had suspended!");

         suspend_count = val;

         configure_counters(true, true);
         collect_countables();

         /* We aren't going to have anything sensible by comparing
          * current values to values from prior to the suspend, so
          * just skip this sampling period.
          */
         return false;
      }
   }

   auto last_ts = last_dump_ts;

   /* Capture the timestamp from the *start* of the sampling period: */
   last_capture_ts = last_dump_ts;

   collect_countables();

   auto elapsed_time_ns = last_dump_ts - last_ts;

   time = (float)elapsed_time_ns / 1000000000.0;

   /* On older kernels that dont' support querying the suspend-
    * count, just send configuration cmdstream regularly to keep
    * the GPU alive and correctly configured for the countables
    * we want
    */
   if (!has_suspend_count) {
      configure_counters(false, false);
   }

   return true;
}

uint64_t FreedrenoDriver::next()
{
   auto ret = last_capture_ts;
   last_capture_ts = 0;
   return ret;
}

void FreedrenoDriver::disable_perfcnt()
{
   /* There isn't really any disable, only reconfiguring which countables
    * get muxed to which counters
    */
}

/*
 * Countable
 */

FreedrenoDriver::Countable
FreedrenoDriver::countable(std::string name)
{
   auto countable = Countable(this, name);
   countables.emplace_back(countable);
   return countable;
}

FreedrenoDriver::Countable::Countable(FreedrenoDriver *d, std::string name)
   : id {d->next_countable_id++}, d {d}, name {name}
{
}

/* Emit register writes on ring to configure counter/countable muxing: */
void
FreedrenoDriver::Countable::configure(struct fd_ringbuffer *ring, bool reset)
{
   const struct fd_perfcntr_countable *countable = d->state[id].countable;
   const struct fd_perfcntr_counter   *counter   = d->state[id].counter;

   OUT_PKT7(ring, CP_WAIT_FOR_IDLE, 0);

   if (counter->enable && reset) {
      OUT_PKT4(ring, counter->enable, 1);
      OUT_RING(ring, 0);
   }

   if (counter->clear && reset) {
      OUT_PKT4(ring, counter->clear, 1);
      OUT_RING(ring, 1);

      OUT_PKT4(ring, counter->clear, 1);
      OUT_RING(ring, 0);
   }

   OUT_PKT4(ring, counter->select_reg, 1);
   OUT_RING(ring, countable->selector);

   if (counter->enable && reset) {
      OUT_PKT4(ring, counter->enable, 1);
      OUT_RING(ring, 1);
   }
}

/* Collect current counter value and calculate delta since last sample: */
void
FreedrenoDriver::Countable::collect()
{
   const struct fd_perfcntr_counter *counter = d->state[id].counter;

   d->state[id].last_value = d->state[id].value;

   uint32_t *reg_lo = (uint32_t *)d->io + counter->counter_reg_lo;
   uint32_t *reg_hi = (uint32_t *)d->io + counter->counter_reg_hi;

   uint32_t lo = *reg_lo;
   uint32_t hi = *reg_hi;

   d->state[id].value = lo | ((uint64_t)hi << 32);
}

/* Resolve the countable and assign next counter from it's group: */
void
FreedrenoDriver::Countable::resolve()
{
   for (unsigned i = 0; i < d->num_perfcntrs; i++) {
      const struct fd_perfcntr_group *g = &d->perfcntrs[i];
      for (unsigned j = 0; j < g->num_countables; j++) {
         const struct fd_perfcntr_countable *c = &g->countables[j];
         if (name == c->name) {
            d->state[id].countable = c;

            /* Assign a counter from the same group: */
            assert(d->assigned_counters[i] < g->num_counters);
            d->state[id].counter = &g->counters[d->assigned_counters[i]++];

            std::cout << "Countable: " << name << ", group=" << g->name <<
                  ", counter=" << d->assigned_counters[i] - 1 << "\n";

            return;
         }
      }
   }
   unreachable("no such countable!");
}

uint64_t
FreedrenoDriver::Countable::get_value() const
{
   return d->state[id].value - d->state[id].last_value;
}

/*
 * DerivedCounter
 */

FreedrenoDriver::DerivedCounter::DerivedCounter(FreedrenoDriver *d, std::string name,
                                                Counter::Units units,
                                                std::function<int64_t()> derive)
   : Counter(d->next_counter_id++, name, 0)
{
   std::cout << "DerivedCounter: " << name << ", id=" << id << "\n";
   this->units = units;
   set_getter([=](const Counter &c, const Driver &d) {
         return derive();
      }
   );
}

FreedrenoDriver::DerivedCounter
FreedrenoDriver::counter(std::string name, Counter::Units units,
                         std::function<int64_t()> derive)
{
   auto counter = DerivedCounter(this, name, units, derive);
   counters.emplace_back(counter);
   return counter;
}

} // namespace pps
