/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
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

#ifndef RADV_AMDGPU_BO_H
#define RADV_AMDGPU_BO_H

#include "radv_amdgpu_winsys.h"

struct radv_amdgpu_winsys_bo {
	amdgpu_bo_handle bo;
	amdgpu_va_handle va_handle;

	uint64_t va;
	enum radeon_bo_domain initial_domain;
	uint64_t size;
	bool is_shared;

	struct radv_amdgpu_winsys *ws;
	struct list_head global_list_item;
};

static inline
struct radv_amdgpu_winsys_bo *radv_amdgpu_winsys_bo(struct radeon_winsys_bo *bo)
{
	return (struct radv_amdgpu_winsys_bo *)bo;
}

void radv_amdgpu_bo_init_functions(struct radv_amdgpu_winsys *ws);

#endif /* RADV_AMDGPU_BO_H */
