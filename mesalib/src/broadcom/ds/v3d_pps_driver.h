/*
 * Copyright © 2024 Raspberry Pi Ltd
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "common/v3d_device_info.h"
#include "drm-uapi/v3d_drm.h"
#include "perfcntrs/v3d_perfcntrs.h"
#include "pps/pps_driver.h"

namespace pps
{

class V3DDriver : public Driver
{
public:
   uint64_t get_min_sampling_period_ns() override;
   bool init_perfcnt() override;
   void enable_counter(uint32_t counter_id) override;
   void enable_all_counters() override;
   void enable_perfcnt(uint64_t sampling_period_ns) override;
   void disable_perfcnt() override;
   bool dump_perfcnt() override;
   uint64_t next() override;
   uint32_t gpu_clock_id() const override;
   uint64_t gpu_timestamp() const override;
   bool cpu_gpu_timestamp(uint64_t &cpu_timestamp,
                          uint64_t &gpu_timestamp) const override;

private:
   struct v3d_device_info devinfo;
   struct v3d_perfcntrs *perfcntrs;
   uint64_t last_dump_ts = 0;

   unsigned int perfmon_id;
   uint64_t values[DRM_V3D_MAX_PERF_COUNTERS];
};

} // namespace pps
