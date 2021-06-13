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

#ifndef AC_RGP_H
#define AC_RGP_H

#include <stdint.h>
#include "compiler/shader_enums.h"
#include "util/list.h"
#include "util/simple_mtx.h"

struct radeon_info;
struct ac_thread_trace;

enum rgp_hardware_stages {
   RGP_HW_STAGE_VS = 0,
   RGP_HW_STAGE_LS,
   RGP_HW_STAGE_HS,
   RGP_HW_STAGE_ES,
   RGP_HW_STAGE_GS,
   RGP_HW_STAGE_PS,
   RGP_HW_STAGE_CS,
   RGP_HW_STAGE_MAX,
};

struct rgp_shader_data {
   uint64_t hash[2];
   uint32_t code_size;
   uint8_t *code;
   uint32_t vgpr_count;
   uint32_t sgpr_count;
   uint64_t base_address;
   uint32_t elf_symbol_offset;
   uint32_t hw_stage;
   uint32_t is_combined;
};

struct rgp_code_object_record {
   uint32_t shader_stages_mask;
   struct rgp_shader_data shader_data[MESA_SHADER_STAGES];
   uint32_t num_shaders_combined; /* count combined shaders as one count */
   uint64_t pipeline_hash[2];
   struct list_head list;
};

struct rgp_code_object {
   uint32_t record_count;
   struct list_head record;
   simple_mtx_t lock;
};

enum rgp_loader_event_type
{
   RGP_LOAD_TO_GPU_MEMORY = 0,
   RGP_UNLOAD_FROM_GPU_MEMORY,
};

struct rgp_loader_events_record {
   uint32_t loader_event_type;
   uint32_t reserved;
   uint64_t base_address;
   uint64_t code_object_hash[2];
   uint64_t time_stamp;
   struct list_head list;
};

struct rgp_loader_events {
   uint32_t record_count;
   struct list_head record;
   simple_mtx_t lock;
};

struct rgp_pso_correlation_record {
   uint64_t api_pso_hash;
   uint64_t pipeline_hash[2];
   char api_level_obj_name[64];
   struct list_head list;
};

struct rgp_pso_correlation {
   uint32_t record_count;
   struct list_head record;
   simple_mtx_t lock;
};

void
ac_rgp_file_write_elf_object(FILE *output, size_t file_elf_start,
                             struct rgp_code_object_record *record,
                             uint32_t *written_size, uint flags);

#endif
