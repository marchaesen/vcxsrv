/*
 * Copyright © 2024 Raspberry Pi Ltd
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>
#include "common/v3d_device_info.h"
#include "common/v3d_util.h"
#include "util/ralloc.h"
#include "v3d_perfcntrs.h"

#define v3dX(x) v3d42_##x
#include "v3dx_counter.h"
#undef v3dX

#define v3dX(x) v3d71_##x
#include "v3dx_counter.h"
#undef v3dX

struct v3d_perfcntrs *
v3d_perfcntrs_init(const struct v3d_device_info *devinfo, int fd)
{
        struct v3d_perfcntrs *perfcounters;

        if (!devinfo)
                return NULL;

        perfcounters = rzalloc(NULL, struct v3d_perfcntrs);
        if (!perfcounters)
                return NULL;

        perfcounters->name_table = _mesa_hash_table_create(NULL, _mesa_hash_string, _mesa_key_string_equal);
        if (!perfcounters->name_table) {
                v3d_perfcntrs_fini(perfcounters);
                return NULL;
        }

        perfcounters->fd = fd;
        perfcounters->devinfo = devinfo;

        perfcounters->max_perfcnt = v3d_X(perfcounters->devinfo, perfcounters_num)(perfcounters->devinfo);
        assert(perfcounters->max_perfcnt);

        perfcounters->perfcnt = rzalloc_array(perfcounters, struct v3d_perfcntr_desc *, perfcounters->max_perfcnt);
        if (!perfcounters->perfcnt) {
                fprintf(stderr, "Error allocating performance counters names");
                v3d_perfcntrs_fini(perfcounters);
                return NULL;
        }

        /* pre-fill our array and hash_table */
        for (unsigned i = 0; i < perfcounters->max_perfcnt; i++) {
                struct v3d_perfcntr_desc *desc = v3d_X(perfcounters->devinfo, perfcounters_get)(perfcounters, i);

                _mesa_hash_table_insert(perfcounters->name_table, desc->name, desc);
        }

        return perfcounters;
}

void
v3d_perfcntrs_fini(struct v3d_perfcntrs *perfcounters)
{
        if (!perfcounters)
                return;

        _mesa_hash_table_destroy(perfcounters->name_table, NULL);
        ralloc_free(perfcounters);
}
