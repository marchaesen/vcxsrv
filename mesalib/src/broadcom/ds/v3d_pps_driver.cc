/*
 * Copyright © 2024 Raspberry Pi Ltd
 * SPDX-License-Identifier: MIT
 */

#include "v3d_pps_driver.h"

#include <perfetto.h>
#include <string.h>
#include <string>

#include "perfcntrs/v3d_perfcntrs.h"
#include <xf86drm.h>

namespace pps
{

uint64_t
V3DDriver::get_min_sampling_period_ns()
{
   return 100000;
}

bool
V3DDriver::init_perfcnt()
{
   const char *v3d_ds_counter_env = getenv("V3D_DS_COUNTER");
   if (!v3d_ds_counter_env || v3d_ds_counter_env[0] == '\0')
      return false;

   bool success = v3d_get_device_info(drm_device.fd, &devinfo, &drmIoctl);
   if (!success)
      return false;

   perfcntrs = v3d_perfcntrs_init(&devinfo, drm_device.fd);
   if (!perfcntrs)
      return false;

   groups.clear();
   counters.clear();
   enabled_counters.clear();

   struct drm_v3d_perfmon_create createreq = { 0 };
   CounterGroup group = {};

   char *dup = strdup(v3d_ds_counter_env);
   char *name = strtok(dup, ",");
   while (name != NULL && createreq.ncounters < DRM_V3D_MAX_PERF_COUNTERS) {
      const struct v3d_perfcntr_desc *desc = v3d_perfcntrs_get_by_name(perfcntrs, name);
      if (desc) {
         Counter counter_desc = {};

         counter_desc.units = Counter::Units::None;
         counter_desc.id = createreq.ncounters;
         counter_desc.name = desc->name;
         counter_desc.group = group.id;
         counter_desc.getter = [this](
            const Counter &c, const Driver &dri) -> Counter::Value {
            return static_cast<int64_t>(values[c.id]);
         };

         counters.emplace_back(std::move(counter_desc));
         createreq.counters[createreq.ncounters++] = desc->index;
      } else {
         PERFETTO_ELOG("Unkown performance counter name: %s", name);
      }

      name = strtok(NULL, ",");
   }

   free(dup);

   if (createreq.ncounters == 0)
      return false;

   int ret = drmIoctl(drm_device.fd, DRM_IOCTL_V3D_PERFMON_CREATE, &createreq);
   if (ret != 0)
      PERFETTO_FATAL("Failed to create perfmon %s", strerror(errno));

   perfmon_id = createreq.id;

   return true;
}

void
V3DDriver::enable_counter(uint32_t counter_id)
{
   enabled_counters.push_back(counters[counter_id]);
}

void
V3DDriver::enable_all_counters()
{
   enabled_counters.reserve(counters.size());
   enabled_counters.insert(enabled_counters.end(), counters.begin(), counters.end());
}

void
V3DDriver::enable_perfcnt(uint64_t sampling_period_ns)
{
   struct drm_v3d_perfmon_set_global globalreq = {
      .id = perfmon_id,
   };

   int ret = drmIoctl(drm_device.fd, DRM_IOCTL_V3D_PERFMON_SET_GLOBAL, &globalreq);
   if (ret != 0) {
      if (errno == ENOTTY)
         PERFETTO_FATAL("Failed to set global perfmon. Feature not available - update your kernel");
      else
         PERFETTO_FATAL("Failed to set global perfmon %s", strerror(errno));
   }
}

void
V3DDriver::disable_perfcnt()
{
   struct drm_v3d_perfmon_set_global globalreq = {
      .flags = DRM_V3D_PERFMON_CLEAR_GLOBAL,
      .id = perfmon_id,
   };

   int ret = drmIoctl(drm_device.fd, DRM_IOCTL_V3D_PERFMON_SET_GLOBAL, &globalreq);
   if (ret != 0)
      PERFETTO_FATAL("Failed to clear global perfmon %s", strerror(errno));

   struct drm_v3d_perfmon_destroy destroyreq = {
      .id = perfmon_id,
   };

   ret = drmIoctl(drm_device.fd, DRM_IOCTL_V3D_PERFMON_DESTROY, &destroyreq);
   if (ret != 0)
      PERFETTO_FATAL("Failed to destroy perfmon %s", strerror(errno));
}

bool
V3DDriver::dump_perfcnt()
{
   last_dump_ts = perfetto::base::GetBootTimeNs().count();

   struct drm_v3d_perfmon_get_values req = {
      .id = perfmon_id,
      .values_ptr = (uintptr_t)values,
   };

   int ret = drmIoctl(drm_device.fd, DRM_IOCTL_V3D_PERFMON_GET_VALUES, &req);
   if (ret != 0) {
         PERFETTO_FATAL("Can't request perfmon counters values\n");
         return false;
   }

   return true;
}

uint64_t
V3DDriver::next()
{
   auto ret = last_dump_ts;
   last_dump_ts = 0;
   return ret;
}

uint32_t
V3DDriver::gpu_clock_id() const
{
   return perfetto::protos::pbzero::BUILTIN_CLOCK_BOOTTIME;
}

uint64_t
V3DDriver::gpu_timestamp() const
{
   return perfetto::base::GetBootTimeNs().count();
}

bool
V3DDriver::cpu_gpu_timestamp(uint64_t &, uint64_t &) const
{
   /* Not supported */
   return false;
}

} // namespace pps
