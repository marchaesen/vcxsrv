/*
 * Copyright © 2020 Valve Corporation
 *
 * based on amdgpu winsys.
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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

#include "radv_null_bo.h"
#include "util/u_memory.h"

static struct radeon_winsys_bo *
radv_null_winsys_bo_create(struct radeon_winsys *_ws,
			   uint64_t size,
			   unsigned alignment,
			   enum radeon_bo_domain initial_domain,
			   enum radeon_bo_flag flags,
			   unsigned priority)
{
	struct radv_null_winsys_bo *bo;

	bo = CALLOC_STRUCT(radv_null_winsys_bo);
	if (!bo)
		return NULL;

	bo->ptr = malloc(size);
	if (!bo->ptr)
		goto error_ptr_alloc;

	return (struct radeon_winsys_bo *)bo;
error_ptr_alloc:
	FREE(bo);
	return NULL;
}

static void *
radv_null_winsys_bo_map(struct radeon_winsys_bo *_bo)
{
	struct radv_null_winsys_bo *bo = radv_null_winsys_bo(_bo);
	return bo->ptr;
}

static void
radv_null_winsys_bo_unmap(struct radeon_winsys_bo *_bo)
{
}

static void radv_null_winsys_bo_destroy(struct radeon_winsys *_ws,
					struct radeon_winsys_bo *_bo)
{
	struct radv_null_winsys_bo *bo = radv_null_winsys_bo(_bo);
	FREE(bo->ptr);
	FREE(bo);
}

void radv_null_bo_init_functions(struct radv_null_winsys *ws)
{
	ws->base.buffer_create = radv_null_winsys_bo_create;
	ws->base.buffer_destroy = radv_null_winsys_bo_destroy;
	ws->base.buffer_map = radv_null_winsys_bo_map;
	ws->base.buffer_unmap = radv_null_winsys_bo_unmap;
}
