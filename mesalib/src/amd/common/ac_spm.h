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

#ifndef AC_SPM_H
#define AC_SPM_H

#include <stdint.h>

#include "ac_perfcounter.h"

#define AC_SPM_MAX_COUNTER_PER_BLOCK 16
#define AC_SPM_GLOBAL_TIMESTAMP_COUNTERS 4 /* in unit of 16-bit counters*/
#define AC_SPM_NUM_COUNTER_PER_MUXSEL 16 /* 16 16-bit counters per muxsel */
#define AC_SPM_MUXSEL_LINE_SIZE ((AC_SPM_NUM_COUNTER_PER_MUXSEL * 2) / 4) /* in dwords */
#define AC_SPM_NUM_PERF_SEL 4

enum ac_spm_segment_type {
   AC_SPM_SEGMENT_TYPE_SE0,
   AC_SPM_SEGMENT_TYPE_SE1,
   AC_SPM_SEGMENT_TYPE_SE2,
   AC_SPM_SEGMENT_TYPE_SE3,
   AC_SPM_SEGMENT_TYPE_GLOBAL,
   AC_SPM_SEGMENT_TYPE_COUNT,
};

struct ac_spm_counter_create_info {
   enum ac_pc_gpu_block gpu_block;
   uint32_t instance;
   uint32_t event_id;
};

struct ac_spm_muxsel {
   uint16_t counter      : 6;
   uint16_t block        : 4;
   uint16_t shader_array : 1; /* 0: SA0, 1: SA1 */
   uint16_t instance     : 5;
};

struct ac_spm_muxsel_line {
   struct ac_spm_muxsel muxsel[AC_SPM_NUM_COUNTER_PER_MUXSEL];
};

struct ac_spm_counter_info {
   /* General info. */
   enum ac_pc_gpu_block gpu_block;
   uint32_t instance;
   uint32_t event_id;

   /* Muxsel info. */
   enum ac_spm_segment_type segment_type;
   bool is_even;
   struct ac_spm_muxsel muxsel;

   /* Output info. */
   uint64_t offset;
};

struct ac_spm_counter_select {
   uint8_t active; /* mask of used 16-bit counters. */
   uint32_t sel0;
   uint32_t sel1;
};

struct ac_spm_block_select {
   const struct ac_pc_block *b;
   uint32_t grbm_gfx_index;

   uint32_t num_counters;
   struct ac_spm_counter_select counters[AC_SPM_MAX_COUNTER_PER_BLOCK];
};

struct ac_spm_trace_data {
   /* struct radeon_winsys_bo or struct pb_buffer */
   void *bo;
   void *ptr;
   uint32_t buffer_size;
   uint16_t sample_interval;

   /* Enabled counters. */
   unsigned num_counters;
   struct ac_spm_counter_info *counters;

   /* Block/counters selection. */
   uint32_t num_block_sel;
   struct ac_spm_block_select *block_sel;
   uint32_t num_used_sq_block_sel;
   struct ac_spm_block_select sq_block_sel[16];

   /* Muxsel lines. */
   unsigned num_muxsel_lines[AC_SPM_SEGMENT_TYPE_COUNT];
   struct ac_spm_muxsel_line *muxsel_lines[AC_SPM_SEGMENT_TYPE_COUNT];
};

bool ac_init_spm(const struct radeon_info *info,
                 const struct ac_perfcounters *pc,
                 unsigned num_counters,
                 const struct ac_spm_counter_create_info *counters,
                 struct ac_spm_trace_data *spm_trace);
void ac_destroy_spm(struct ac_spm_trace_data *spm_trace);

uint32_t ac_spm_get_sample_size(const struct ac_spm_trace_data *spm_trace);
uint32_t ac_spm_get_num_samples(const struct ac_spm_trace_data *spm_trace);

#endif
