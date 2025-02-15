/*
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2024 Valve Corporation
 * Copyright 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "compiler/libcl/libcl.h"
#include "query.h"

static inline void
write_query_result(uintptr_t dst_addr, int32_t idx, bool is_64, uint64_t result)
{
   if (is_64) {
      global uint64_t *out = (global uint64_t *)dst_addr;
      out[idx] = result;
   } else {
      global uint32_t *out = (global uint32_t *)dst_addr;
      out[idx] = result;
   }
}

KERNEL(32)
libagx_copy_query(global uint32_t *availability, global uint64_t *results,
                  global uint16_t *oq_index, uint64_t dst_addr,
                  uint64_t dst_stride, uint32_t first_query, uint16_t partial,
                  uint16_t _64, uint16_t with_availability,
                  uint16_t reports_per_query)
{
   uint i = cl_global_id.x;
   uint64_t dst = dst_addr + (((uint64_t)i) * dst_stride);
   uint32_t query = first_query + i;

   bool available;
   if (availability)
      available = availability[query];
   else
      available = (results[query] != LIBAGX_QUERY_UNAVAILABLE);

   if (available || partial) {
      /* For occlusion queries, results[] points to the device global heap. We
       * need to remap indices according to the query pool's allocation.
       */
      uint result_index = oq_index ? oq_index[query] : query;
      uint idx = result_index * reports_per_query;

      for (unsigned i = 0; i < reports_per_query; ++i) {
         write_query_result(dst, i, _64, results[idx + i]);
      }
   }

   if (with_availability) {
      write_query_result(dst, reports_per_query, _64, available);
   }
}

/* TODO: Share with Gallium... */
enum pipe_query_value_type {
   PIPE_QUERY_TYPE_I32,
   PIPE_QUERY_TYPE_U32,
   PIPE_QUERY_TYPE_I64,
   PIPE_QUERY_TYPE_U64,
};

KERNEL(1)
libagx_copy_query_gl(global uint64_t *query, global uint64_t *dest,
                     ushort value_type, ushort bool_size)
{
   uint64_t value = *query;

   if (bool_size == 4) {
      value = (uint32_t)value;
   }

   if (bool_size) {
      value = value != 0;
   }

   if (value_type <= PIPE_QUERY_TYPE_U32) {
      global uint32_t *dest32 = (global uint32_t *)dest;
      bool u32 = (value_type == PIPE_QUERY_TYPE_U32);

      *dest32 = u32 ? convert_uint_sat(value) : convert_int_sat((int64_t)value);
   } else {
      *dest = value;
   }
}

KERNEL(4)
libagx_copy_xfb_counters(constant struct libagx_xfb_counter_copy *push)
{
   unsigned i = cl_local_id.x;

   *(push->dest[i]) = push->src[i] ? *(push->src[i]) : 0;
}

KERNEL(1)
libagx_increment_statistic(global uint32_t *statistic, uint32_t delta)
{
   *statistic += delta;
}

KERNEL(1)
libagx_increment_cs_invocations(global uint *grid, global uint32_t *statistic,
                                uint32_t local_size_threads)
{
   *statistic += local_size_threads * grid[0] * grid[1] * grid[2];
}

KERNEL(32)
libagx_write_u32s(constant struct libagx_imm_write *p)
{
   uint id = cl_global_id.x;
   *(p[id].address) = p[id].value;
}

/*
 * We set the source as volatile since the caching situation around timestamps
 * is a bit unclear. It might not be necessary but - absent hardware/firmware
 * documentation - this gives me peace of mind.
 */
KERNEL(1)
libagx_copy_timestamp(global uint64_t *dest, volatile global uint64_t *src)
{
   *dest = *src;
}

KERNEL(1)
libagx_write_u32(global uint32_t *address, uint32_t value)
{
   *address = value;
}
