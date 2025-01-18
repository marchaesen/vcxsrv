/*
 * Copyright 2024 Google LLC
 * SPDX-License-Identifier: MIT
 */

#ifndef PANVK_UTRACE_H
#define PANVK_UTRACE_H

#include "util/perf/u_trace.h"

#include "panvk_macros.h"
#include "panvk_mempool.h"

struct panvk_device;
struct vk_sync;

struct panvk_utrace_flush_data {
   uint32_t subqueue;

   struct vk_sync *sync;
   uint64_t wait_value;

   struct panvk_pool clone_pool;
};

void *panvk_utrace_create_buffer(struct u_trace_context *utctx,
                                 uint64_t size_B);

void panvk_utrace_delete_buffer(struct u_trace_context *utctx, void *buffer);

uint64_t panvk_utrace_read_ts(struct u_trace_context *utctx, void *timestamps,
                              uint64_t offset_B, void *flush_data);

void panvk_utrace_delete_flush_data(struct u_trace_context *utctx,
                                    void *flush_data);

#ifdef PAN_ARCH

#if PAN_ARCH >= 10

void panvk_per_arch(utrace_context_init)(struct panvk_device *dev);
void panvk_per_arch(utrace_context_fini)(struct panvk_device *dev);

void panvk_per_arch(utrace_copy_buffer)(struct u_trace_context *utctx,
                                        void *cmdstream, void *ts_from,
                                        uint64_t from_offset, void *ts_to,
                                        uint64_t to_offset, uint64_t size_B);

struct cs_builder;

void panvk_per_arch(utrace_clone_init_pool)(struct panvk_pool *pool,
                                            struct panvk_device *dev);
void panvk_per_arch(utrace_clone_init_builder)(struct cs_builder *b,
                                               struct panvk_pool *pool);
void panvk_per_arch(utrace_clone_finish_builder)(struct cs_builder *b);

#else /* PAN_ARCH >= 10 */

static inline void
panvk_per_arch(utrace_context_init)(struct panvk_device *dev)
{
}

static inline void
panvk_per_arch(utrace_context_fini)(struct panvk_device *dev)
{
}

#endif /* PAN_ARCH >= 10 */

#endif /* PAN_ARCH */

#endif /* PANVK_UTRACE_H */
