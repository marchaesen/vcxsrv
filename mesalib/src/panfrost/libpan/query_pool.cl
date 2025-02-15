/*
 * Copyright 2024 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */
#include "compiler/libcl/libcl.h"
#include "compiler/libcl/libcl_vk.h"
#include "genxml/gen_macros.h"

#if (PAN_ARCH >= 6 && PAN_ARCH < 10)
static inline void
write_occlusion_query_result(uintptr_t dst_addr, int32_t idx, uint32_t flags,
                             global uint64_t *report_addr,
                             uint32_t report_count)
{
   uint64_t value = 0;

   for (uint32_t i = 0; i < report_count; i++)
      value += report_addr[i];

   vk_write_query(dst_addr, idx, flags, value);
}

KERNEL(1)
panlib_copy_query_result(uint64_t pool_addr, global uint32_t *available_addr,
                         uint32_t query_stride, uint32_t first_query,
                         uint32_t query_count, uint64_t dst_addr,
                         uint64_t dst_stride, uint32_t query_type,
                         uint32_t flags, uint32_t report_count)
{
   uint32_t i = cl_global_id.x;

   if (i >= query_count)
      return;

   uint32_t query = first_query + i;
   uintptr_t dst = dst_addr + ((uint64_t)i * dst_stride);
   global uint64_t *report_addr =
      (global uint64_t *)(pool_addr + ((uint64_t)query * query_stride));

   bool available = available_addr[query];

   if ((flags & VK_QUERY_RESULT_PARTIAL_BIT) || available) {
      switch (query_type) {
      case VK_QUERY_TYPE_OCCLUSION:
         write_occlusion_query_result(dst, 0, flags, report_addr, report_count);
         break;
      default:
         unreachable("Unsupported query type");
         break;
      }
   }

   if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
      vk_write_query(dst, 1, flags, available);
   }
}

KERNEL(1)
panlib_clear_query_result(uint64_t pool_addr, global uint32_t *available_addr,
                          uint32_t query_stride, uint32_t first_query,
                          uint32_t query_count, uint32_t report_count,
                          uint32_t availaible_value)
{
   uint32_t i = cl_global_id.x;

   if (i >= query_count)
      return;

   uint32_t query = first_query + i;
   global uint64_t *report_addr =
      (global uint64_t *)(pool_addr + ((uint64_t)query * query_stride));

   available_addr[query] = availaible_value;

   for (uint32_t i = 0; i < report_count; i++)
      report_addr[i] = 0;
}

#endif
