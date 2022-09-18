/*
 * Copyright 2020 Advanced Micro Devices, Inc.
 * Copyright 2020 Valve Corporation
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include "ac_sqtt.h"

#include "ac_gpu_info.h"
#include "util/u_math.h"
#include "util/os_time.h"

uint64_t
ac_thread_trace_get_info_offset(unsigned se)
{
   return sizeof(struct ac_thread_trace_info) * se;
}

uint64_t
ac_thread_trace_get_data_offset(const struct radeon_info *rad_info,
                                const struct ac_thread_trace_data *data, unsigned se)
{
   unsigned max_se = rad_info->max_se;
   uint64_t data_offset;

   data_offset = align64(sizeof(struct ac_thread_trace_info) * max_se,
               1 << SQTT_BUFFER_ALIGN_SHIFT);
   data_offset += data->buffer_size * se;

   return data_offset;
}

uint64_t
ac_thread_trace_get_info_va(uint64_t va, unsigned se)
{
   return va + ac_thread_trace_get_info_offset(se);
}

uint64_t
ac_thread_trace_get_data_va(const struct radeon_info *rad_info,
                            const struct ac_thread_trace_data *data, uint64_t va, unsigned se)
{
   return va + ac_thread_trace_get_data_offset(rad_info, data, se);
}

bool
ac_is_thread_trace_complete(struct radeon_info *rad_info,
                            const struct ac_thread_trace_data *data,
                            const struct ac_thread_trace_info *info)
{
   if (rad_info->gfx_level >= GFX10) {
      /* GFX10 doesn't have THREAD_TRACE_CNTR but it reports the number of
       * dropped bytes per SE via THREAD_TRACE_DROPPED_CNTR. Though, this
       * doesn't seem reliable because it might still report non-zero even if
       * the SQTT buffer isn't full.
       *
       * The solution here is to compare the number of bytes written by the hw
       * (in units of 32 bytes) to the SQTT buffer size. If it's equal, that
       * means that the buffer is full and should be resized.
       */
      return !(info->cur_offset * 32 == data->buffer_size - 32);
   }

   /* Otherwise, compare the current thread trace offset with the number
    * of written bytes.
    */
   return info->cur_offset == info->gfx9_write_counter;
}

uint32_t
ac_get_expected_buffer_size(struct radeon_info *rad_info,
                            const struct ac_thread_trace_info *info)
{
   if (rad_info->gfx_level >= GFX10) {
      uint32_t dropped_cntr_per_se = info->gfx10_dropped_cntr / rad_info->max_se;
      return ((info->cur_offset * 32) + dropped_cntr_per_se) / 1024;
   }

   return (info->gfx9_write_counter * 32) / 1024;
}

bool
ac_sqtt_add_pso_correlation(struct ac_thread_trace_data *thread_trace_data,
                            uint64_t pipeline_hash)
{
   struct rgp_pso_correlation *pso_correlation = &thread_trace_data->rgp_pso_correlation;
   struct rgp_pso_correlation_record *record;

   record = malloc(sizeof(struct rgp_pso_correlation_record));
   if (!record)
      return false;

   record->api_pso_hash = pipeline_hash;
   record->pipeline_hash[0] = pipeline_hash;
   record->pipeline_hash[1] = pipeline_hash;
   memset(record->api_level_obj_name, 0, sizeof(record->api_level_obj_name));

   simple_mtx_lock(&pso_correlation->lock);
   list_addtail(&record->list, &pso_correlation->record);
   pso_correlation->record_count++;
   simple_mtx_unlock(&pso_correlation->lock);

   return true;
}

bool
ac_sqtt_add_code_object_loader_event(struct ac_thread_trace_data *thread_trace_data,
                                     uint64_t pipeline_hash,
                                     uint64_t base_address)
{
   struct rgp_loader_events *loader_events = &thread_trace_data->rgp_loader_events;
   struct rgp_loader_events_record *record;

   record = malloc(sizeof(struct rgp_loader_events_record));
   if (!record)
      return false;

   record->loader_event_type = RGP_LOAD_TO_GPU_MEMORY;
   record->reserved = 0;
   record->base_address = base_address & 0xffffffffffff;
   record->code_object_hash[0] = pipeline_hash;
   record->code_object_hash[1] = pipeline_hash;
   record->time_stamp = os_time_get_nano();

   simple_mtx_lock(&loader_events->lock);
   list_addtail(&record->list, &loader_events->record);
   loader_events->record_count++;
   simple_mtx_unlock(&loader_events->lock);

   return true;
}

/* See https://gitlab.freedesktop.org/mesa/mesa/-/issues/5260
 * On some HW SQTT can hang if we're not in one of the profiling pstates. */
bool
ac_check_profile_state(const struct radeon_info *info)
{
   char path[128];
   char data[128];
   int n;

   snprintf(path, sizeof(path),
            "/sys/bus/pci/devices/%04x:%02x:%02x.%x/power_dpm_force_performance_level",
            info->pci_domain, info->pci_bus, info->pci_dev, info->pci_func);

   FILE *f = fopen(path, "r");
   if (!f)
      return false; /* Unknown but optimistic. */
   n = fread(data, 1, sizeof(data) - 1, f);
   fclose(f);
   data[n] = 0;
   return strstr(data, "profile") == NULL;
}
