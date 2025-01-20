/*
 * Copyright © 2024 Raspberry Pi Ltd
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "util/hash_table.h"

struct v3d_device_info;

struct v3d_perfcntr_desc {
        unsigned index;
        const char *name;
        const char *category;
        const char *description;
};

struct v3d_perfcntrs {
        int fd;
        unsigned max_perfcnt;
        const struct v3d_device_info *devinfo;
        struct v3d_perfcntr_desc **perfcnt;
        struct hash_table *name_table;
};

#ifdef __cplusplus
extern "C" {
#endif

struct v3d_perfcntrs *
v3d_perfcntrs_init(const struct v3d_device_info *devinfo, int fd);

void
v3d_perfcntrs_fini(struct v3d_perfcntrs *perfcounters);

static inline struct v3d_perfcntr_desc *
v3d_perfcntrs_get_by_index(struct v3d_perfcntrs *perfcounters, unsigned index)
{
        if (index >= perfcounters->max_perfcnt)
                return NULL;

        return perfcounters->perfcnt[index];
}

static inline struct v3d_perfcntr_desc *
v3d_perfcntrs_get_by_name(struct v3d_perfcntrs *perfcounters, const char *name)
{
        struct hash_entry *entry = _mesa_hash_table_search(perfcounters->name_table, name);

        return entry ? (struct v3d_perfcntr_desc *)entry->data : NULL;
}

#ifdef __cplusplus
}
#endif
