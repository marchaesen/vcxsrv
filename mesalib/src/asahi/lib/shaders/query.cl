/*
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2024 Valve Corporation
 * Copyright 2022 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#include "libagx.h"
#include "query.h"

static inline void
write_query_result(uintptr_t dst_addr, int32_t idx, bool is_64, uint64_t result)
{
   /* TODO: do we want real 64-bit stats? sync with CPU impl */
   result &= 0xffffffff;

   if (is_64) {
      global uint64_t *out = (global uint64_t *)dst_addr;
      out[idx] = result;
   } else {
      global uint32_t *out = (global uint32_t *)dst_addr;
      out[idx] = result;
   }
}

void
libagx_copy_query(constant struct libagx_copy_query_push *push, unsigned i)
{
   uint64_t dst = push->dst_addr + (((uint64_t)i) * push->dst_stride);
   uint32_t query = push->first_query + i;
   bool available = push->availability[query];

   if (available || push->partial) {
      /* For occlusion queries, results[] points to the device global heap. We
       * need to remap indices according to the query pool's allocation.
       */
      uint result_index = push->oq_index ? push->oq_index[query] : query;
      uint idx = result_index * push->reports_per_query;

      for (unsigned i = 0; i < push->reports_per_query; ++i) {
         write_query_result(dst, i, push->_64, push->results[idx + i]);
      }
   }

   if (push->with_availability) {
      write_query_result(dst, push->reports_per_query, push->_64, available);
   }
}

void
libagx_copy_xfb_counters(constant struct libagx_xfb_counter_copy *push)
{
   unsigned i = get_local_id(0);

   *(push->dest[i]) = push->src[i] ? *(push->src[i]) : 0;
}

void
libagx_increment_statistic(constant struct libagx_increment_params *p)
{
   *(p->statistic) += p->delta;
}

void
libagx_increment_cs_invocations(constant struct libagx_cs_invocation_params *p)
{
   *(p->statistic) += libagx_cs_invocations(p->local_size_threads, p->grid[0],
                                            p->grid[1], p->grid[2]);
}

kernel void
libagx_increment_ia_counters(constant struct libagx_increment_ia_counters *p,
                             uint index_size_B, uint tid)
{
   unsigned count = p->draw[0];
   local uint scratch;

   if (index_size_B /* implies primitive restart */) {
      uint start = p->draw[2];
      uint partial = 0;

      /* Count non-restart indices */
      for (uint i = tid; i < count; i += 1024) {
         uint index = libagx_load_index_buffer_internal(
            p->index_buffer, p->index_buffer_range_el, start + i, index_size_B);

         if (index != p->restart_index)
            partial++;
      }

      /* Accumulate the partials across the workgroup */
      scratch = 0;
      barrier(CLK_LOCAL_MEM_FENCE);
      atomic_add(&scratch, partial);
      barrier(CLK_LOCAL_MEM_FENCE);
      count = scratch;

      /* Elect a single thread from the workgroup to increment the counters */
      if (tid != 0)
         return;
   }

   count *= p->draw[1];

   if (p->ia_vertices) {
      *(p->ia_vertices) += count;
   }

   if (p->vs_invocations) {
      *(p->vs_invocations) += count;
   }
}

void
libagx_write_u32s(constant struct libagx_imm_write *p, uint id)
{
   *(p[id].address) = p[id].value;
}
