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

#include <stdio.h>

#include "radv_amdgpu_bo.h"

#include <amdgpu.h>
#include <amdgpu_drm.h>
#include <inttypes.h>

static void radv_amdgpu_winsys_bo_destroy(struct radeon_winsys_bo *_bo)
{
	struct radv_amdgpu_winsys_bo *bo = radv_amdgpu_winsys_bo(_bo);

	if (bo->ws->debug_all_bos) {
		pthread_mutex_lock(&bo->ws->global_bo_list_lock);
		LIST_DEL(&bo->global_list_item);
		bo->ws->num_buffers--;
		pthread_mutex_unlock(&bo->ws->global_bo_list_lock);
	}
	amdgpu_bo_va_op(bo->bo, 0, bo->size, bo->va, 0, AMDGPU_VA_OP_UNMAP);
	amdgpu_va_range_free(bo->va_handle);
	amdgpu_bo_free(bo->bo);
	FREE(bo);
}

static void radv_amdgpu_add_buffer_to_global_list(struct radv_amdgpu_winsys_bo *bo)
{
	struct radv_amdgpu_winsys *ws = bo->ws;

	if (bo->ws->debug_all_bos) {
		pthread_mutex_lock(&ws->global_bo_list_lock);
		LIST_ADDTAIL(&bo->global_list_item, &ws->global_bo_list);
		ws->num_buffers++;
		pthread_mutex_unlock(&ws->global_bo_list_lock);
	}
}

static struct radeon_winsys_bo *
radv_amdgpu_winsys_bo_create(struct radeon_winsys *_ws,
			     uint64_t size,
			     unsigned alignment,
			     enum radeon_bo_domain initial_domain,
			     unsigned flags)
{
	struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);
	struct radv_amdgpu_winsys_bo *bo;
	struct amdgpu_bo_alloc_request request = {0};
	amdgpu_bo_handle buf_handle;
	uint64_t va = 0;
	amdgpu_va_handle va_handle;
	int r;
	bo = CALLOC_STRUCT(radv_amdgpu_winsys_bo);
	if (!bo) {
		return NULL;
	}

	request.alloc_size = size;
	request.phys_alignment = alignment;

	if (initial_domain & RADEON_DOMAIN_VRAM)
		request.preferred_heap |= AMDGPU_GEM_DOMAIN_VRAM;
	if (initial_domain & RADEON_DOMAIN_GTT)
		request.preferred_heap |= AMDGPU_GEM_DOMAIN_GTT;

	if (flags & RADEON_FLAG_CPU_ACCESS)
		request.flags |= AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED;
	if (flags & RADEON_FLAG_NO_CPU_ACCESS)
		request.flags |= AMDGPU_GEM_CREATE_NO_CPU_ACCESS;
	if (flags & RADEON_FLAG_GTT_WC)
		request.flags |= AMDGPU_GEM_CREATE_CPU_GTT_USWC;

	r = amdgpu_bo_alloc(ws->dev, &request, &buf_handle);
	if (r) {
		fprintf(stderr, "amdgpu: Failed to allocate a buffer:\n");
		fprintf(stderr, "amdgpu:    size      : %"PRIu64" bytes\n", size);
		fprintf(stderr, "amdgpu:    alignment : %u bytes\n", alignment);
		fprintf(stderr, "amdgpu:    domains   : %u\n", initial_domain);
		goto error_bo_alloc;
	}

	r = amdgpu_va_range_alloc(ws->dev, amdgpu_gpu_va_range_general,
				  size, alignment, 0, &va, &va_handle, 0);
	if (r)
		goto error_va_alloc;

	r = amdgpu_bo_va_op(buf_handle, 0, size, va, 0, AMDGPU_VA_OP_MAP);
	if (r)
		goto error_va_map;

	bo->bo = buf_handle;
	bo->va = va;
	bo->va_handle = va_handle;
	bo->initial_domain = initial_domain;
	bo->size = size;
	bo->is_shared = false;
	bo->ws = ws;
	radv_amdgpu_add_buffer_to_global_list(bo);
	return (struct radeon_winsys_bo *)bo;
error_va_map:
	amdgpu_va_range_free(va_handle);

error_va_alloc:
	amdgpu_bo_free(buf_handle);

error_bo_alloc:
	FREE(bo);
	return NULL;
}

static uint64_t radv_amdgpu_winsys_bo_get_va(struct radeon_winsys_bo *_bo)
{
	struct radv_amdgpu_winsys_bo *bo = radv_amdgpu_winsys_bo(_bo);
	return bo->va;
}

static void *
radv_amdgpu_winsys_bo_map(struct radeon_winsys_bo *_bo)
{
	struct radv_amdgpu_winsys_bo *bo = radv_amdgpu_winsys_bo(_bo);
	int ret;
	void *data;
	ret = amdgpu_bo_cpu_map(bo->bo, &data);
	if (ret)
		return NULL;
	return data;
}

static void
radv_amdgpu_winsys_bo_unmap(struct radeon_winsys_bo *_bo)
{
	struct radv_amdgpu_winsys_bo *bo = radv_amdgpu_winsys_bo(_bo);
	amdgpu_bo_cpu_unmap(bo->bo);
}

static struct radeon_winsys_bo *
radv_amdgpu_winsys_bo_from_fd(struct radeon_winsys *_ws,
			      int fd, unsigned *stride,
			      unsigned *offset)
{
	struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);
	struct radv_amdgpu_winsys_bo *bo;
	uint64_t va;
	amdgpu_va_handle va_handle;
	enum amdgpu_bo_handle_type type = amdgpu_bo_handle_type_dma_buf_fd;
	struct amdgpu_bo_import_result result = {0};
	struct amdgpu_bo_info info = {0};
	enum radeon_bo_domain initial = 0;
	int r;
	bo = CALLOC_STRUCT(radv_amdgpu_winsys_bo);
	if (!bo)
		return NULL;

	r = amdgpu_bo_import(ws->dev, type, fd, &result);
	if (r)
		goto error;

	r = amdgpu_bo_query_info(result.buf_handle, &info);
	if (r)
		goto error_query;

	r = amdgpu_va_range_alloc(ws->dev, amdgpu_gpu_va_range_general,
				  result.alloc_size, 1 << 20, 0, &va, &va_handle, 0);
	if (r)
		goto error_query;

	r = amdgpu_bo_va_op(result.buf_handle, 0, result.alloc_size, va, 0, AMDGPU_VA_OP_MAP);
	if (r)
		goto error_va_map;

	if (info.preferred_heap & AMDGPU_GEM_DOMAIN_VRAM)
		initial |= RADEON_DOMAIN_VRAM;
	if (info.preferred_heap & AMDGPU_GEM_DOMAIN_GTT)
		initial |= RADEON_DOMAIN_GTT;

	bo->bo = result.buf_handle;
	bo->va = va;
	bo->va_handle = va_handle;
	bo->initial_domain = initial;
	bo->size = result.alloc_size;
	bo->is_shared = true;
	return (struct radeon_winsys_bo *)bo;
error_va_map:
	amdgpu_va_range_free(va_handle);

error_query:
	amdgpu_bo_free(result.buf_handle);

error:
	FREE(bo);
	return NULL;
}

static bool
radv_amdgpu_winsys_get_fd(struct radeon_winsys *_ws,
			  struct radeon_winsys_bo *_bo,
			  int *fd)
{
	struct radv_amdgpu_winsys_bo *bo = radv_amdgpu_winsys_bo(_bo);
	enum amdgpu_bo_handle_type type = amdgpu_bo_handle_type_dma_buf_fd;
	int r;
	unsigned handle;
	r = amdgpu_bo_export(bo->bo, type, &handle);
	if (r)
		return false;

	*fd = (int)handle;
	bo->is_shared = true;
	return true;
}

static unsigned radv_eg_tile_split_rev(unsigned eg_tile_split)
{
	switch (eg_tile_split) {
	case 64:    return 0;
	case 128:   return 1;
	case 256:   return 2;
	case 512:   return 3;
	default:
	case 1024:  return 4;
	case 2048:  return 5;
	case 4096:  return 6;
	}
}

static void
radv_amdgpu_winsys_bo_set_metadata(struct radeon_winsys_bo *_bo,
				   struct radeon_bo_metadata *md)
{
	struct radv_amdgpu_winsys_bo *bo = radv_amdgpu_winsys_bo(_bo);
	struct amdgpu_bo_metadata metadata = {0};
	uint32_t tiling_flags = 0;

	if (md->macrotile == RADEON_LAYOUT_TILED)
		tiling_flags |= AMDGPU_TILING_SET(ARRAY_MODE, 4); /* 2D_TILED_THIN1 */
	else if (md->microtile == RADEON_LAYOUT_TILED)
		tiling_flags |= AMDGPU_TILING_SET(ARRAY_MODE, 2); /* 1D_TILED_THIN1 */
	else
		tiling_flags |= AMDGPU_TILING_SET(ARRAY_MODE, 1); /* LINEAR_ALIGNED */

	tiling_flags |= AMDGPU_TILING_SET(PIPE_CONFIG, md->pipe_config);
	tiling_flags |= AMDGPU_TILING_SET(BANK_WIDTH, util_logbase2(md->bankw));
	tiling_flags |= AMDGPU_TILING_SET(BANK_HEIGHT, util_logbase2(md->bankh));
	if (md->tile_split)
		tiling_flags |= AMDGPU_TILING_SET(TILE_SPLIT, radv_eg_tile_split_rev(md->tile_split));
	tiling_flags |= AMDGPU_TILING_SET(MACRO_TILE_ASPECT, util_logbase2(md->mtilea));
	tiling_flags |= AMDGPU_TILING_SET(NUM_BANKS, util_logbase2(md->num_banks)-1);

	if (md->scanout)
		tiling_flags |= AMDGPU_TILING_SET(MICRO_TILE_MODE, 0); /* DISPLAY_MICRO_TILING */
	else
		tiling_flags |= AMDGPU_TILING_SET(MICRO_TILE_MODE, 1); /* THIN_MICRO_TILING */

	metadata.tiling_info = tiling_flags;
	metadata.size_metadata = md->size_metadata;
	memcpy(metadata.umd_metadata, md->metadata, sizeof(md->metadata));

	amdgpu_bo_set_metadata(bo->bo, &metadata);
}

void radv_amdgpu_bo_init_functions(struct radv_amdgpu_winsys *ws)
{
	ws->base.buffer_create = radv_amdgpu_winsys_bo_create;
	ws->base.buffer_destroy = radv_amdgpu_winsys_bo_destroy;
	ws->base.buffer_get_va = radv_amdgpu_winsys_bo_get_va;
	ws->base.buffer_map = radv_amdgpu_winsys_bo_map;
	ws->base.buffer_unmap = radv_amdgpu_winsys_bo_unmap;
	ws->base.buffer_from_fd = radv_amdgpu_winsys_bo_from_fd;
	ws->base.buffer_get_fd = radv_amdgpu_winsys_get_fd;
	ws->base.buffer_set_metadata = radv_amdgpu_winsys_bo_set_metadata;
}
