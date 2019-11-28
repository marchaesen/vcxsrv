/*
 * Copyright © 2017 Google.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#ifndef RADV_DEBUG_H
#define RADV_DEBUG_H

#include "radv_private.h"

/* Please keep docs/envvars.html up-to-date when you add/remove options. */
enum {
	RADV_DEBUG_NO_FAST_CLEARS    =   0x1,
	RADV_DEBUG_NO_DCC            =   0x2,
	RADV_DEBUG_DUMP_SHADERS      =   0x4,
	RADV_DEBUG_NO_CACHE          =   0x8,
	RADV_DEBUG_DUMP_SHADER_STATS =  0x10,
	RADV_DEBUG_NO_HIZ            =  0x20,
	RADV_DEBUG_NO_COMPUTE_QUEUE  =  0x40,
	RADV_DEBUG_ALL_BOS           =  0x80,
	RADV_DEBUG_NO_IBS            = 0x100,
	RADV_DEBUG_DUMP_SPIRV        = 0x200,
	RADV_DEBUG_VM_FAULTS         = 0x400,
	RADV_DEBUG_ZERO_VRAM         = 0x800,
	RADV_DEBUG_SYNC_SHADERS      = 0x1000,
	RADV_DEBUG_NO_SISCHED        = 0x2000,
	RADV_DEBUG_PREOPTIR          = 0x4000,
	RADV_DEBUG_NO_DYNAMIC_BOUNDS = 0x8000,
	RADV_DEBUG_NO_OUT_OF_ORDER   = 0x10000,
	RADV_DEBUG_INFO              = 0x20000,
	RADV_DEBUG_ERRORS            = 0x40000,
	RADV_DEBUG_STARTUP           = 0x80000,
	RADV_DEBUG_CHECKIR           = 0x100000,
	RADV_DEBUG_NOTHREADLLVM      = 0x200000,
	RADV_DEBUG_NOBINNING         = 0x400000,
	RADV_DEBUG_NO_LOAD_STORE_OPT = 0x800000,
	RADV_DEBUG_NO_NGG            = 0x1000000,
	RADV_DEBUG_NO_SHADER_BALLOT  = 0x2000000,
	RADV_DEBUG_ALL_ENTRYPOINTS   = 0x4000000,
	RADV_DEBUG_DUMP_META_SHADERS = 0x8000000,
	RADV_DEBUG_NO_MEMORY_CACHE   = 0x10000000,
};

enum {
	RADV_PERFTEST_NO_BATCHCHAIN   =    0x1,
	RADV_PERFTEST_SISCHED         =    0x2,
	RADV_PERFTEST_LOCAL_BOS       =    0x4,
	RADV_PERFTEST_DCC_MSAA        =    0x8,
	RADV_PERFTEST_BO_LIST         =   0x10,
	RADV_PERFTEST_SHADER_BALLOT   =   0x20,
	RADV_PERFTEST_TC_COMPAT_CMASK =   0x40,
	RADV_PERFTEST_CS_WAVE_32      =   0x80,
	RADV_PERFTEST_PS_WAVE_32      =  0x100,
	RADV_PERFTEST_GE_WAVE_32      =  0x200,
	RADV_PERFTEST_DFSM            =  0x400,
	RADV_PERFTEST_ACO             =  0x800,
};

bool
radv_init_trace(struct radv_device *device);

void
radv_check_gpu_hangs(struct radv_queue *queue, struct radeon_cmdbuf *cs);

void
radv_print_spirv(const char *data, uint32_t size, FILE *fp);

void
radv_dump_enabled_options(struct radv_device *device, FILE *f);

#endif
