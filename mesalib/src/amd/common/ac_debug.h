/*
 * Copyright 2015 Advanced Micro Devices, Inc.
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

#ifndef AC_DEBUG_H
#define AC_DEBUG_H

#include "amd_family.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#define AC_ENCODE_TRACE_POINT(id) (0xcafe0000 | ((id)&0xffff))
#define AC_IS_TRACE_POINT(x)      (((x)&0xcafe0000) == 0xcafe0000)
#define AC_GET_TRACE_POINT_ID(x)  ((x)&0xffff)

#define AC_MAX_WAVES_PER_CHIP (64 * 40)

#ifdef __cplusplus
extern "C" {
#endif

struct ac_wave_info {
   unsigned se; /* shader engine */
   unsigned sh; /* shader array */
   unsigned cu; /* compute unit */
   unsigned simd;
   unsigned wave;
   uint32_t status;
   uint64_t pc; /* program counter */
   uint32_t inst_dw0;
   uint32_t inst_dw1;
   uint64_t exec;
   bool matched; /* whether the wave is used by a currently-bound shader */
};

typedef void *(*ac_debug_addr_callback)(void *data, uint64_t addr);

const char *ac_get_register_name(enum chip_class chip_class, unsigned offset);
void ac_dump_reg(FILE *file, enum chip_class chip_class, unsigned offset, uint32_t value,
                 uint32_t field_mask);
void ac_parse_ib_chunk(FILE *f, uint32_t *ib, int num_dw, const int *trace_ids,
                       unsigned trace_id_count, enum chip_class chip_class,
                       ac_debug_addr_callback addr_callback, void *addr_callback_data);
void ac_parse_ib(FILE *f, uint32_t *ib, int num_dw, const int *trace_ids, unsigned trace_id_count,
                 const char *name, enum chip_class chip_class, ac_debug_addr_callback addr_callback,
                 void *addr_callback_data);

bool ac_vm_fault_occured(enum chip_class chip_class, uint64_t *old_dmesg_timestamp,
                         uint64_t *out_addr);

unsigned ac_get_wave_info(enum chip_class chip_class,
                          struct ac_wave_info waves[AC_MAX_WAVES_PER_CHIP]);

#ifdef __cplusplus
}
#endif

#endif
