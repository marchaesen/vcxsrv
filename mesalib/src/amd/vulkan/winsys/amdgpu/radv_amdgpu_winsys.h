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
#include "addrlib/addrinterface.h"
#include <amdgpu.h>
#include "util/list.h"

struct radv_amdgpu_winsys {
	struct radeon_winsys base;
	amdgpu_device_handle dev;

	struct radeon_info info;
	struct amdgpu_gpu_info amdinfo;
	ADDR_HANDLE addrlib;

	uint32_t rev_id;
	unsigned family;

	bool debug_all_bos;
	pthread_mutex_t global_bo_list_lock;
	struct list_head global_bo_list;
	unsigned num_buffers;

	bool use_ib_bos;
};

static inline struct radv_amdgpu_winsys *
radv_amdgpu_winsys(struct radeon_winsys *base)
{
	return (struct radv_amdgpu_winsys*)base;
}

#endif /* RADV_AMDGPU_WINSYS_H */
