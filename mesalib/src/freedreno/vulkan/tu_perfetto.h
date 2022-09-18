/*
 * Copyright © 2021 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#ifndef TU_PERFETTO_H_
#define TU_PERFETTO_H_

#ifdef HAVE_PERFETTO

/* we can't include tu_common.h because ir3 headers are not C++-compatible */
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TU_PERFETTO_MAX_STACK_DEPTH 8

struct tu_device;
struct tu_u_trace_submission_data;

struct tu_perfetto_stage {
   int stage_id;
   uint64_t start_ts;
};

struct tu_perfetto_state {
   struct tu_perfetto_stage stages[TU_PERFETTO_MAX_STACK_DEPTH];
   unsigned stage_depth;
   unsigned skipped_depth;
};

void tu_perfetto_init(void);

void tu_perfetto_submit(struct tu_device *dev, uint32_t submission_id);

/* Helpers */

struct tu_perfetto_state *
tu_device_get_perfetto_state(struct tu_device *dev);

uint32_t
tu_u_trace_submission_data_get_submit_id(const struct tu_u_trace_submission_data *data);

#ifdef __cplusplus
}
#endif

#endif /* HAVE_PERFETTO */

#endif /* TU_PERFETTO_H_ */
