/*
 * Copyright © 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

#ifndef AC_SHADOWED_REGS
#define AC_SHADOWED_REGS

#include "ac_gpu_info.h"

struct radeon_cmdbuf;

struct ac_reg_range {
   unsigned offset;
   unsigned size;
};

enum ac_reg_range_type
{
   SI_REG_RANGE_UCONFIG,
   SI_REG_RANGE_CONTEXT,
   SI_REG_RANGE_SH,
   SI_REG_RANGE_CS_SH,
   SI_NUM_SHADOWED_REG_RANGES,

   SI_REG_RANGE_NON_SHADOWED = SI_NUM_SHADOWED_REG_RANGES,
   SI_NUM_ALL_REG_RANGES,
};

typedef void (*set_context_reg_seq_array_fn)(struct radeon_cmdbuf *cs, unsigned reg, unsigned num,
                                             const uint32_t *values);

void ac_get_reg_ranges(enum chip_class chip_class, enum radeon_family family,
                       enum ac_reg_range_type type, unsigned *num_ranges,
                       const struct ac_reg_range **ranges);
void ac_emulate_clear_state(const struct radeon_info *info, struct radeon_cmdbuf *cs,
                            set_context_reg_seq_array_fn set_context_reg_seq_array);
void ac_check_shadowed_regs(enum chip_class chip_class, enum radeon_family family,
                            unsigned reg_offset, unsigned count);
void ac_print_shadowed_regs(const struct radeon_info *info);

#endif
