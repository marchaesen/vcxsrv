/*
 * Copyright 2021 Valve Corporation
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

#include "ac_spm.h"

#include "util/bitscan.h"
#include "util/u_memory.h"
#include "ac_perfcounter.h"

static struct ac_spm_block_select *
ac_spm_get_block_select(struct ac_spm_trace_data *spm_trace,
                        const struct ac_pc_block *block)
{
   struct ac_spm_block_select *block_sel, *new_block_sel;
   uint32_t num_block_sel;

   for (uint32_t i = 0; i < spm_trace->num_block_sel; i++) {
      if (spm_trace->block_sel[i].b->b->b->gpu_block == block->b->b->gpu_block)
         return &spm_trace->block_sel[i];
   }

   /* Allocate a new select block if it doesn't already exist. */
   num_block_sel = spm_trace->num_block_sel + 1;
   block_sel = realloc(spm_trace->block_sel, num_block_sel * sizeof(*block_sel));
   if (!block_sel)
      return NULL;

   spm_trace->num_block_sel = num_block_sel;
   spm_trace->block_sel = block_sel;

   /* Initialize the new select block. */
   new_block_sel = &spm_trace->block_sel[spm_trace->num_block_sel - 1];
   memset(new_block_sel, 0, sizeof(*new_block_sel));

   new_block_sel->b = block;
   new_block_sel->num_counters = block->b->b->num_spm_counters;

   /* Broadcast global block writes to SEs and SAs */
   if (!(block->b->b->flags & (AC_PC_BLOCK_SE | AC_PC_BLOCK_SHADER)))
      new_block_sel->grbm_gfx_index = S_030800_SE_BROADCAST_WRITES(1) |
                                      S_030800_SH_BROADCAST_WRITES(1);
   /* Broadcast per SE block writes to SAs */
   else if (block->b->b->flags & AC_PC_BLOCK_SE)
      new_block_sel->grbm_gfx_index = S_030800_SH_BROADCAST_WRITES(1);

   return new_block_sel;
}

static void
ac_spm_init_muxsel(const struct ac_pc_block *block,
                   struct ac_spm_counter_info *counter,
                   uint32_t spm_wire)
{
   struct ac_spm_muxsel *muxsel = &counter->muxsel;

   muxsel->counter = 2 * spm_wire + (counter->is_even ? 0 : 1);
   muxsel->block = block->b->b->spm_block_select;
   muxsel->shader_array = 0;
   muxsel->instance = 0;
}

static bool
ac_spm_map_counter(struct ac_spm_trace_data *spm_trace,
                   struct ac_spm_block_select *block_sel,
                   struct ac_spm_counter_info *counter,
                   uint32_t *spm_wire)
{
   if (block_sel->b->b->b->gpu_block == SQ) {
      for (unsigned i = 0; i < ARRAY_SIZE(spm_trace->sq_block_sel); i++) {
         struct ac_spm_block_select *sq_block_sel = &spm_trace->sq_block_sel[i];
         struct ac_spm_counter_select *cntr_sel = &sq_block_sel->counters[0];
         if (i < spm_trace->num_used_sq_block_sel)
            continue;

         /* SQ doesn't support 16-bit counters. */
         cntr_sel->sel0 |= S_036700_PERF_SEL(counter->event_id) |
                           S_036700_SPM_MODE(3) | /* 32-bit clamp */
                           S_036700_PERF_MODE(0);
         cntr_sel->active |= 0x3;

         /* 32-bits counter are always even. */
         counter->is_even = true;

         /* One wire per SQ module. */
         *spm_wire = i;

         spm_trace->num_used_sq_block_sel++;
         return true;
      }
   } else {
      /* Generic blocks. */
      for (unsigned i = 0; i < block_sel->num_counters; i++) {
         struct ac_spm_counter_select *cntr_sel = &block_sel->counters[i];
         int index = ffs(~cntr_sel->active) - 1;

         switch (index) {
         case 0: /* use S_037004_PERF_SEL */
            cntr_sel->sel0 |= S_037004_PERF_SEL(counter->event_id) |
                              S_037004_CNTR_MODE(1) | /* 16-bit clamp */
                              S_037004_PERF_MODE(0); /* accum */
            break;
         case 1: /* use S_037004_PERF_SEL1 */
            cntr_sel->sel0 |= S_037004_PERF_SEL1(counter->event_id) |
                              S_037004_PERF_MODE1(0);
            break;
         case 2: /* use S_037004_PERF_SEL2 */
            cntr_sel->sel1 |= S_037008_PERF_SEL2(counter->event_id) |
                              S_037008_PERF_MODE2(0);
            break;
         case 3: /* use S_037004_PERF_SEL3 */
            cntr_sel->sel1 |= S_037008_PERF_SEL3(counter->event_id) |
                              S_037008_PERF_MODE3(0);
            break;
         default:
            return false;
         }

         /* Mark this 16-bit counter as used. */
         cntr_sel->active |= 1 << index;

         /* Determine if the counter is even or odd. */
         counter->is_even = !(index % 2);

         /* Determine the SPM wire (one wire holds two 16-bit counters). */
         *spm_wire = !!(index >= 2);

         return true;
      }
   }

   return false;
}

static bool
ac_spm_add_counter(const struct ac_perfcounters *pc,
                   struct ac_spm_trace_data *spm_trace,
                   const struct ac_spm_counter_create_info *info)
{
   struct ac_spm_counter_info *counter;
   struct ac_spm_block_select *block_sel;
   struct ac_pc_block *block;
   uint32_t spm_wire;

   /* Check if the GPU block is valid. */
   block = ac_pc_get_block(pc, info->gpu_block);
   if (!block) {
      fprintf(stderr, "ac/spm: Invalid GPU block.\n");
      return false;
   }

   /* Check if the number of instances is valid. */
   if (info->instance > block->num_instances) {
      fprintf(stderr, "ac/spm: Invalid instance ID.\n");
      return false;
   }

   /* Check if the event ID is valid. */
   if (info->event_id > block->b->selectors) {
      fprintf(stderr, "ac/spm: Invalid event ID.\n");
      return false;
   }

   counter = &spm_trace->counters[spm_trace->num_counters];
   spm_trace->num_counters++;

   counter->gpu_block = info->gpu_block;
   counter->instance = info->instance;
   counter->event_id = info->event_id;

   /* Get the select block used to configure the counter. */
   block_sel = ac_spm_get_block_select(spm_trace, block);
   if (!block_sel)
      return false;

   /* Map the counter to the select block. */
   if (!ac_spm_map_counter(spm_trace, block_sel, counter, &spm_wire)) {
      fprintf(stderr, "ac/spm: No free slots available!\n");
      return false;
   }

   /* Determine the counter segment type. */
   if (block->b->b->flags & AC_PC_BLOCK_SE) {
      counter->segment_type = AC_SPM_SEGMENT_TYPE_SE0; // XXX
   } else {
      counter->segment_type = AC_SPM_SEGMENT_TYPE_GLOBAL;
   }

   /* Configure the muxsel for SPM. */
   ac_spm_init_muxsel(block, counter, spm_wire);

   return true;
}

bool ac_init_spm(const struct radeon_info *info,
                 const struct ac_perfcounters *pc,
                 unsigned num_counters,
                 const struct ac_spm_counter_create_info *counters,
                 struct ac_spm_trace_data *spm_trace)
{
   spm_trace->counters = CALLOC(num_counters, sizeof(*spm_trace->counters));
   if (!spm_trace->counters)
      return false;

   for (unsigned i = 0; i < num_counters; i++) {
      if (!ac_spm_add_counter(pc, spm_trace, &counters[i])) {
         fprintf(stderr, "ac/spm: Failed to add SPM counter (%d).\n", i);
         return false;
      }
   }

   /* Determine the segment size and create a muxsel ram for every segment. */
   for (unsigned s = 0; s < AC_SPM_SEGMENT_TYPE_COUNT; s++) {
      unsigned num_even_counters = 0, num_odd_counters = 0;

      if (s == AC_SPM_SEGMENT_TYPE_GLOBAL) {
         /* The global segment always start with a 64-bit timestamp. */
         num_even_counters += AC_SPM_GLOBAL_TIMESTAMP_COUNTERS;
      }

      /* Count the number of even/odd counters for this segment. */
      for (unsigned c = 0; c < spm_trace->num_counters; c++) {
         struct ac_spm_counter_info *counter = &spm_trace->counters[c];

         if (counter->segment_type != s)
            continue;

         if (counter->is_even) {
            num_even_counters++;
         } else {
            num_odd_counters++;
         }
      }

      /* Compute the number of lines. */
      unsigned even_lines =
         DIV_ROUND_UP(num_even_counters, AC_SPM_NUM_COUNTER_PER_MUXSEL);
      unsigned odd_lines =
         DIV_ROUND_UP(num_odd_counters, AC_SPM_NUM_COUNTER_PER_MUXSEL);
      unsigned num_lines = (even_lines > odd_lines) ? (2 * even_lines - 1) : (2 * odd_lines);

      spm_trace->muxsel_lines[s] = CALLOC(num_lines, sizeof(*spm_trace->muxsel_lines[s]));
      if (!spm_trace->muxsel_lines[s])
         return false;
      spm_trace->num_muxsel_lines[s] = num_lines;
   }

   /* RLC uses the following order: Global, SE0, SE1, SE2, SE3. */
   const enum ac_spm_segment_type ordered_segment[AC_SPM_SEGMENT_TYPE_COUNT] =
   {
      AC_SPM_SEGMENT_TYPE_GLOBAL,
      AC_SPM_SEGMENT_TYPE_SE0,
      AC_SPM_SEGMENT_TYPE_SE1,
      AC_SPM_SEGMENT_TYPE_SE2,
      AC_SPM_SEGMENT_TYPE_SE3,
   };

   for (unsigned s = 0; s < AC_SPM_SEGMENT_TYPE_COUNT; s++) {
      if (!spm_trace->muxsel_lines[s])
         continue;

      uint32_t segment_offset = 0;
      for (unsigned i = 0; s != ordered_segment[i]; i++) {
         segment_offset += spm_trace->num_muxsel_lines[ordered_segment[i]] *
                           AC_SPM_NUM_COUNTER_PER_MUXSEL;
      }

      uint32_t even_counter_idx = 0, even_line_idx = 0;
      uint32_t odd_counter_idx = 0, odd_line_idx = 1;

      /* Add the global timestamps first. */
      if (s == AC_SPM_SEGMENT_TYPE_GLOBAL) {
         struct ac_spm_muxsel global_timestamp_muxsel = {
            .counter = 0x30,
            .block = 0x3,
            .shader_array = 0,
            .instance = 0x1e,
         };

         for (unsigned i = 0; i < 4; i++) {
            spm_trace->muxsel_lines[s][even_line_idx].muxsel[even_counter_idx++] = global_timestamp_muxsel;
         }
      }

      for (unsigned i = 0; i < spm_trace->num_counters; i++) {
         struct ac_spm_counter_info *counter = &spm_trace->counters[i];

         if (counter->segment_type != s)
            continue;

         if (counter->is_even) {
            counter->offset = segment_offset + even_line_idx *
                              AC_SPM_NUM_COUNTER_PER_MUXSEL + even_counter_idx;

            spm_trace->muxsel_lines[s][even_line_idx].muxsel[even_counter_idx] = spm_trace->counters[i].muxsel;
            if (++even_counter_idx == AC_SPM_NUM_COUNTER_PER_MUXSEL) {
               even_counter_idx = 0;
               even_line_idx += 2;
            }
         } else {
            counter->offset = segment_offset + odd_line_idx *
                              AC_SPM_NUM_COUNTER_PER_MUXSEL + odd_counter_idx;

            spm_trace->muxsel_lines[s][odd_line_idx].muxsel[odd_counter_idx] = spm_trace->counters[i].muxsel;
            if (++odd_counter_idx == AC_SPM_NUM_COUNTER_PER_MUXSEL) {
               odd_counter_idx = 0;
               odd_line_idx += 2;
            }
         }
      }
   }

   return true;
}

void ac_destroy_spm(struct ac_spm_trace_data *spm_trace)
{
   for (unsigned s = 0; s < AC_SPM_SEGMENT_TYPE_COUNT; s++) {
      FREE(spm_trace->muxsel_lines[s]);
   }
   FREE(spm_trace->block_sel);
   FREE(spm_trace->counters);
}

uint32_t ac_spm_get_sample_size(const struct ac_spm_trace_data *spm_trace)
{
   uint32_t sample_size = 0; /* in bytes */

   for (unsigned s = 0; s < AC_SPM_SEGMENT_TYPE_COUNT; s++) {
      sample_size += spm_trace->num_muxsel_lines[s] * AC_SPM_MUXSEL_LINE_SIZE * 4;
   }

   return sample_size;
}

uint32_t ac_spm_get_num_samples(const struct ac_spm_trace_data *spm_trace)
{
   uint32_t sample_size = ac_spm_get_sample_size(spm_trace);
   uint32_t *ptr = (uint32_t *)spm_trace->ptr;
   uint32_t data_size, num_lines_written;
   uint32_t num_samples = 0;

   /* Get the data size (in bytes) written by the hw to the ring buffer. */
   data_size = ptr[0];

   /* Compute the number of 256 bits (16 * 16-bits counters) lines written. */
   num_lines_written = data_size / (2 * AC_SPM_NUM_COUNTER_PER_MUXSEL);

   /* Check for overflow. */
   if (num_lines_written % (sample_size / 32)) {
      abort();
   } else {
      num_samples = num_lines_written / (sample_size / 32);
   }

   return num_samples;
}
