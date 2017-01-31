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
 *
 * Authors:
 *      Marek Olšák <maraeo@gmail.com>
 */
#ifndef AC_DEBUG_H
#define AC_DEBUG_H

#include <stdint.h>
#include <stdio.h>

#include "amd_family.h"

#define AC_ENCODE_TRACE_POINT(id)       (0xcafe0000 | ((id) & 0xffff))
#define AC_IS_TRACE_POINT(x)            (((x) & 0xcafe0000) == 0xcafe0000)
#define AC_GET_TRACE_POINT_ID(x)        ((x) & 0xffff)

typedef void *(*ac_debug_addr_callback)(void *data, uint64_t addr);

void ac_dump_reg(FILE *file, unsigned offset, uint32_t value,
		 uint32_t field_mask);
void ac_parse_ib(FILE *f, uint32_t *ib, int num_dw, int trace_id,
		 const char *name, enum chip_class chip_class,
		 ac_debug_addr_callback addr_callback, void *addr_callback_data);

#endif
