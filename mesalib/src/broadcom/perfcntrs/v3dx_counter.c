/*
 * Copyright © 2024 Raspberry Pi Ltd
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

#include "common/v3d_device_info.h"
#include "common/v3d_macros.h"
#include "common/v3d_performance_counters.h"
#include "common/v3d_util.h"
#include "drm-uapi/v3d_drm.h"
#include "util/log.h"
#include "util/ralloc.h"
#include "v3d_perfcntrs.h"
#include "v3dx_counter.h"

unsigned
v3dX(perfcounters_num)(const struct v3d_device_info *devinfo)
{
        return devinfo->max_perfcnt ? devinfo->max_perfcnt
                                    : ARRAY_SIZE(v3d_performance_counters);
}

struct v3d_perfcntr_desc *
v3dX(perfcounters_get)(struct v3d_perfcntrs *perfcounters, unsigned index)
{
        const unsigned max_perfcnt = perfcounters->max_perfcnt;
        struct v3d_perfcntr_desc *counter;

        assert(index < max_perfcnt);
        assert(perfcounters->perfcnt[index] == NULL);

        counter = ralloc(perfcounters, struct v3d_perfcntr_desc);
        if (!counter)
                return NULL;

        if (perfcounters->devinfo->max_perfcnt) {
                struct drm_v3d_perfmon_get_counter req = {
                        .counter = index,
                };
                int ret = v3d_ioctl(perfcounters->fd, DRM_IOCTL_V3D_PERFMON_GET_COUNTER, &req);
                if (ret != 0) {
                        mesa_loge("Failed to get performance counter %d: %s\n", index, strerror(errno));
                        return NULL;
                }

                counter->name = ralloc_strdup(perfcounters->perfcnt, (const char *) req.name);
                counter->category = ralloc_strdup(perfcounters->perfcnt, (const char *) req.category);
                counter->description = ralloc_strdup(perfcounters->perfcnt, (const char *) req.description);
        } else {
                counter->name = v3d_performance_counters[index][V3D_PERFCNT_NAME];
                counter->category = v3d_performance_counters[index][V3D_PERFCNT_CATEGORY];
                counter->description = v3d_performance_counters[index][V3D_PERFCNT_DESCRIPTION];
        }

        counter->index = index;
        perfcounters->perfcnt[index] = counter;

        return counter;
}
