/*
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_UTRACE_PERFETTO_H
#define PANVK_UTRACE_PERFETTO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* must be at least PANVK_SUBQUEUE_COUNT */
#define PANVK_UTRACE_PERFETTO_QUEUE_COUNT 3
#define PANVK_UTRACE_PERFETTO_STACK_DEPTH 8

struct panvk_device;

enum panvk_utrace_perfetto_stage {
   PANVK_UTRACE_PERFETTO_STAGE_CMDBUF,
   PANVK_UTRACE_PERFETTO_STAGE_COUNT,
};

struct panvk_utrace_perfetto_event {
   enum panvk_utrace_perfetto_stage stage;
   uint64_t begin_ns;
};

struct panvk_utrace_perfetto_queue {
   struct panvk_utrace_perfetto_event stack[PANVK_UTRACE_PERFETTO_STACK_DEPTH];
   uint32_t stack_depth;
};

struct panvk_utrace_perfetto {
   uint32_t gpu_clock_id;
   uint64_t device_id;

   uint64_t queue_iids[PANVK_UTRACE_PERFETTO_QUEUE_COUNT];
   uint64_t stage_iids[PANVK_UTRACE_PERFETTO_STAGE_COUNT];

   uint64_t next_clock_snapshot;
   uint64_t event_id;

   struct panvk_utrace_perfetto_queue queues[PANVK_UTRACE_PERFETTO_QUEUE_COUNT];
};

#ifdef HAVE_PERFETTO

void panvk_utrace_perfetto_init(struct panvk_device *dev, uint32_t queue_count);

#else /* HAVE_PERFETTO */

static inline void
panvk_utrace_perfetto_init(struct panvk_device *dev, uint32_t queue_count)
{
}

#endif /* HAVE_PERFETTO */

#ifdef __cplusplus
}
#endif

#endif /* PANVK_UTRACE_PERFETTO_H */
