/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * based on amdgpu winsys.
 * Copyright © 2011 Marek Olšák <maraeo@gmail.com>
 * Copyright © 2015 Advanced Micro Devices, Inc.
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

#ifndef RADV_AMDGPU_WINSYS_H
#define RADV_AMDGPU_WINSYS_H

#include "radv_radeon_winsys.h"
#include "ac_gpu_info.h"
#include <amdgpu.h>
#include "util/list.h"
#include "util/rwlock.h"
#include <pthread.h>

struct radv_amdgpu_winsys {
	struct radeon_winsys base;
	amdgpu_device_handle dev;

	struct radeon_info info;
	struct amdgpu_gpu_info amdinfo;
	struct ac_addrlib *addrlib;

	bool debug_all_bos;
	bool use_ib_bos;
	bool zero_all_vram_allocs;
	bool use_local_bos;
	bool use_llvm;
	unsigned num_buffers;

	struct u_rwlock global_bo_list_lock;
	struct list_head global_bo_list;

	uint64_t allocated_vram;
	uint64_t allocated_vram_vis;
	uint64_t allocated_gtt;

	/* syncobj cache */
	pthread_mutex_t syncobj_lock;
	uint32_t *syncobj;
	uint32_t syncobj_count, syncobj_capacity;
};

static inline struct radv_amdgpu_winsys *
radv_amdgpu_winsys(struct radeon_winsys *base)
{
	return (struct radv_amdgpu_winsys*)base;
}

#endif /* RADV_AMDGPU_WINSYS_H */
