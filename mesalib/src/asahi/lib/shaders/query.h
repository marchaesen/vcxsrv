/*
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2024 Valve Corporation
 * Copyright 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "libagx.h"

#pragma once

struct libagx_copy_query_push {
   GLOBAL(uint32_t) availability;
   GLOBAL(uint64_t) results;
   GLOBAL(uint16_t) oq_index;
   uint64_t dst_addr;
   uint64_t dst_stride;
   uint32_t first_query;

   /* Flags. Could specialize the shader? */
   uint16_t partial;
   uint16_t _64;
   uint16_t with_availability;
   uint16_t reports_per_query;
};

struct libagx_xfb_counter_copy {
   GLOBAL(uint32_t) dest[4];
   GLOBAL(uint32_t) src[4];
};

struct libagx_increment_params {
   /* Pointer to the invocation statistic */
   GLOBAL(uint32_t) statistic;

   /* Value to increment by */
   uint32_t delta;
};

struct libagx_cs_invocation_params {
   /* Pointer to the indirect dispatch grid */
   GLOBAL(uint32_t) grid;

   /* Pointer to the compute shader invocation statistic */
   GLOBAL(uint32_t) statistic;

   /* Local workgroup size in threads */
   uint32_t local_size_threads;
};

static inline uint32_t
libagx_cs_invocations(uint32_t local_size_threads, uint32_t x, uint32_t y,
                      uint32_t z)
{
   return local_size_threads * x * y * z;
}

struct libagx_increment_ia_counters {
   /* Statistics */
   GLOBAL(uint32_t) ia_vertices;
   GLOBAL(uint32_t) vs_invocations;

   /* Input draw */
   CONSTANT(uint32_t) draw;

   /* Index buffer */
   uint64_t index_buffer;
   uint32_t index_buffer_range_el;
   uint32_t restart_index;
};

struct libagx_imm_write {
   GLOBAL(uint32_t) address;
   uint32_t value;
};
