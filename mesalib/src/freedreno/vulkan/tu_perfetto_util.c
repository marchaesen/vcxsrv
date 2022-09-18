/*
 * Copyright © 2021 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "tu_device.h"
#include "tu_perfetto.h"

/* Including tu_device.h in tu_perfetto.cc doesn't work, so
 * we need some helper methods to access tu_device.
 */

struct tu_perfetto_state *
tu_device_get_perfetto_state(struct tu_device *dev)
{
    return &dev->perfetto;
}

uint32_t
tu_u_trace_submission_data_get_submit_id(const struct tu_u_trace_submission_data *data)
{
    return data->submission_id;
}
