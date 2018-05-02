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
#include <pthread.h>
#include <unistd.h>

#include "util/u_atomic.h"


static void radv_amdgpu_winsys_bo_destroy(struct radeon_winsys_bo *_bo);

static int
radv_amdgpu_bo_va_op(struct radv_amdgpu_winsys *ws,
		     amdgpu_bo_handle bo,
		     uint64_t offset,
		     uint64_t size,
		     uint64_t addr,
		     uint32_t bo_flags,
		     uint32_t ops)
{
	uint64_t flags = AMDGPU_VM_PAGE_READABLE |
			 AMDGPU_VM_PAGE_EXECUTABLE;

	if ((bo_flags & RADEON_FLAG_VA_UNCACHED) && ws->info.chip_class >= GFX9)
		flags |= AMDGPU_VM_MTYPE_UC;

	if (!(bo_flags & RADEON_FLAG_READ_ONLY))
		flags |= AMDGPU_VM_PAGE_WRITEABLE;

	size = ALIGN(size, getpagesize());

	return amdgpu_bo_va_op_raw(ws->dev, bo, offset, size, addr,
				   flags, ops);
}

static void
radv_amdgpu_winsys_virtual_map(struct radv_amdgpu_winsys_bo *bo,
                               const struct radv_amdgpu_map_range *range)
{
	assert(range->size);

	if (!range->bo)
		return; /* TODO: PRT mapping */

	p_atomic_inc(&range->bo->ref_count);
	int r = radv_amdgpu_bo_va_op(bo->ws, range->bo->bo, range->bo_offset,
				     range->size, range->offset + bo->base.va,
				     0, AMDGPU_VA_OP_MAP);
	if (r)
		abort();
}

static void
radv_amdgpu_winsys_virtual_unmap(struct radv_amdgpu_winsys_bo *bo,
                                 const struct radv_amdgpu_map_range *range)
{
	assert(range->size);

	if (!range->bo)
		return; /* TODO: PRT mapping */

	int r = radv_amdgpu_bo_va_op(bo->ws, range->bo->bo, range->bo_offset,
				     range->size, range->offset + bo->base.va,
				     0, AMDGPU_VA_OP_UNMAP);
	if (r)
		abort();
	radv_amdgpu_winsys_bo_destroy((struct radeon_winsys_bo *)range->bo);
}

static int bo_comparator(const void *ap, const void *bp) {
	struct radv_amdgpu_bo *a = *(struct radv_amdgpu_bo *const *)ap;
	struct radv_amdgpu_bo *b = *(struct radv_amdgpu_bo *const *)bp;
	return (a > b) ? 1 : (a < b) ? -1 : 0;
}

static void
radv_amdgpu_winsys_rebuild_bo_list(struct radv_amdgpu_winsys_bo *bo)
{
	if (bo->bo_capacity < bo->range_count) {
		uint32_t new_count = MAX2(bo->bo_capacity * 2, bo->range_count);
		bo->bos = realloc(bo->bos, new_count * sizeof(struct radv_amdgpu_winsys_bo *));
		bo->bo_capacity = new_count;
	}

	uint32_t temp_bo_count = 0;
	for (uint32_t i = 0; i < bo->range_count; ++i)
		if (bo->ranges[i].bo)
			bo->bos[temp_bo_count++] = bo->ranges[i].bo;

	qsort(bo->bos, temp_bo_count, sizeof(struct radv_amdgpu_winsys_bo *), &bo_comparator);

	uint32_t final_bo_count = 1;
	for (uint32_t i = 1; i < temp_bo_count; ++i)
		if (bo->bos[i] != bo->bos[i - 1])
			bo->bos[final_bo_count++] = bo->bos[i];

	bo->bo_count = final_bo_count;
}

static void
radv_amdgpu_winsys_bo_virtual_bind(struct radeon_winsys_bo *_parent,
                                   uint64_t offset, uint64_t size,
                                   struct radeon_winsys_bo *_bo, uint64_t bo_offset)
{
	struct radv_amdgpu_winsys_bo *parent = (struct radv_amdgpu_winsys_bo *)_parent;
	struct radv_amdgpu_winsys_bo *bo = (struct radv_amdgpu_winsys_bo*)_bo;
	int range_count_delta, new_idx;
	int first = 0, last;
	struct radv_amdgpu_map_range new_first, new_last;

	assert(parent->is_virtual);
	assert(!bo || !bo->is_virtual);

	if (!size)
		return;

	/* We have at most 2 new ranges (1 by the bind, and another one by splitting a range that contains the newly bound range). */
	if (parent->range_capacity - parent->range_count < 2) {
		parent->range_capacity += 2;
		parent->ranges = realloc(parent->ranges,
		                         parent->range_capacity * sizeof(struct radv_amdgpu_map_range));
	}

	/*
	 * [first, last] is exactly the range of ranges that either overlap the
	 * new parent, or are adjacent to it. This corresponds to the bind ranges
	 * that may change.
	 */
	while(first + 1 < parent->range_count && parent->ranges[first].offset + parent->ranges[first].size < offset)
		++first;

	last = first;
	while(last + 1 < parent->range_count && parent->ranges[last].offset <= offset + size)
		++last;

	/* Whether the first or last range are going to be totally removed or just
	 * resized/left alone. Note that in the case of first == last, we will split
	 * this into a part before and after the new range. The remove flag is then
	 * whether to not create the corresponding split part. */
	bool remove_first = parent->ranges[first].offset == offset;
	bool remove_last = parent->ranges[last].offset + parent->ranges[last].size == offset + size;
	bool unmapped_first = false;

	assert(parent->ranges[first].offset <= offset);
	assert(parent->ranges[last].offset + parent->ranges[last].size >= offset + size);

	/* Try to merge the new range with the first range. */
	if (parent->ranges[first].bo == bo && (!bo || offset - bo_offset == parent->ranges[first].offset - parent->ranges[first].bo_offset)) {
		size += offset - parent->ranges[first].offset;
		offset = parent->ranges[first].offset;
		bo_offset = parent->ranges[first].bo_offset;
		remove_first = true;
	}

	/* Try to merge the new range with the last range. */
	if (parent->ranges[last].bo == bo && (!bo || offset - bo_offset == parent->ranges[last].offset - parent->ranges[last].bo_offset)) {
		size = parent->ranges[last].offset + parent->ranges[last].size - offset;
		remove_last = true;
	}

	range_count_delta = 1 - (last - first + 1) + !remove_first + !remove_last;
	new_idx = first + !remove_first;

	/* Any range between first and last is going to be entirely covered by the new range so just unmap them. */
	for (int i = first + 1; i < last; ++i)
		radv_amdgpu_winsys_virtual_unmap(parent, parent->ranges + i);

	/* If the first/last range are not left alone we unmap then and optionally map
	 * them again after modifications. Not that this implicitly can do the splitting
	 * if first == last. */
	new_first = parent->ranges[first];
	new_last = parent->ranges[last];

	if (parent->ranges[first].offset + parent->ranges[first].size > offset || remove_first) {
		radv_amdgpu_winsys_virtual_unmap(parent, parent->ranges + first);
		unmapped_first = true;

		if (!remove_first) {
			new_first.size = offset - new_first.offset;
			radv_amdgpu_winsys_virtual_map(parent, &new_first);
		}
	}

	if (parent->ranges[last].offset < offset + size || remove_last) {
		if (first != last || !unmapped_first)
			radv_amdgpu_winsys_virtual_unmap(parent, parent->ranges + last);

		if (!remove_last) {
			new_last.size -= offset + size - new_last.offset;
			new_last.offset = offset + size;
			radv_amdgpu_winsys_virtual_map(parent, &new_last);
		}
	}

	/* Moves the range list after last to account for the changed number of ranges. */
	memmove(parent->ranges + last + 1 + range_count_delta, parent->ranges + last + 1,
	        sizeof(struct radv_amdgpu_map_range) * (parent->range_count - last - 1));

	if (!remove_first)
		parent->ranges[first] = new_first;

	if (!remove_last)
		parent->ranges[new_idx + 1] = new_last;

	/* Actually set up the new range. */
	parent->ranges[new_idx].offset = offset;
	parent->ranges[new_idx].size = size;
	parent->ranges[new_idx].bo = bo;
	parent->ranges[new_idx].bo_offset = bo_offset;

	radv_amdgpu_winsys_virtual_map(parent, parent->ranges + new_idx);

	parent->range_count += range_count_delta;

	radv_amdgpu_winsys_rebuild_bo_list(parent);
}

static void radv_amdgpu_winsys_bo_destroy(struct radeon_winsys_bo *_bo)
{
	struct radv_amdgpu_winsys_bo *bo = radv_amdgpu_winsys_bo(_bo);

	if (p_atomic_dec_return(&bo->ref_count))
		return;
	if (bo->is_virtual) {
		for (uint32_t i = 0; i < bo->range_count; ++i) {
			radv_amdgpu_winsys_virtual_unmap(bo, bo->ranges + i);
		}
		free(bo->bos);
		free(bo->ranges);
	} else {
		if (bo->ws->debug_all_bos) {
			pthread_mutex_lock(&bo->ws->global_bo_list_lock);
			LIST_DEL(&bo->global_list_item);
			bo->ws->num_buffers--;
			pthread_mutex_unlock(&bo->ws->global_bo_list_lock);
		}
		radv_amdgpu_bo_va_op(bo->ws, bo->bo, 0, bo->size, bo->base.va,
				     0, AMDGPU_VA_OP_UNMAP);
		amdgpu_bo_free(bo->bo);
	}
	amdgpu_va_range_free(bo->va_handle);
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

	r = amdgpu_va_range_alloc(ws->dev, amdgpu_gpu_va_range_general,
				  size, alignment, 0, &va, &va_handle, 0);
	if (r)
		goto error_va_alloc;

	bo->base.va = va;
	bo->va_handle = va_handle;
	bo->size = size;
	bo->ws = ws;
	bo->is_virtual = !!(flags & RADEON_FLAG_VIRTUAL);
	bo->ref_count = 1;

	if (flags & RADEON_FLAG_VIRTUAL) {
		bo->ranges = realloc(NULL, sizeof(struct radv_amdgpu_map_range));
		bo->range_count = 1;
		bo->range_capacity = 1;

		bo->ranges[0].offset = 0;
		bo->ranges[0].size = size;
		bo->ranges[0].bo = NULL;
		bo->ranges[0].bo_offset = 0;

		radv_amdgpu_winsys_virtual_map(bo, bo->ranges);
		return (struct radeon_winsys_bo *)bo;
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
	if (!(flags & RADEON_FLAG_IMPLICIT_SYNC) && ws->info.drm_minor >= 22)
		request.flags |= AMDGPU_GEM_CREATE_EXPLICIT_SYNC;
	if (flags & RADEON_FLAG_NO_INTERPROCESS_SHARING &&
	    ws->info.has_local_buffers && ws->use_local_bos) {
		bo->base.is_local = true;
		request.flags |= AMDGPU_GEM_CREATE_VM_ALWAYS_VALID;
	}

	/* this won't do anything on pre 4.9 kernels */
	if (ws->zero_all_vram_allocs && (initial_domain & RADEON_DOMAIN_VRAM))
		request.flags |= AMDGPU_GEM_CREATE_VRAM_CLEARED;
	r = amdgpu_bo_alloc(ws->dev, &request, &buf_handle);
	if (r) {
		fprintf(stderr, "amdgpu: Failed to allocate a buffer:\n");
		fprintf(stderr, "amdgpu:    size      : %"PRIu64" bytes\n", size);
		fprintf(stderr, "amdgpu:    alignment : %u bytes\n", alignment);
		fprintf(stderr, "amdgpu:    domains   : %u\n", initial_domain);
		goto error_bo_alloc;
	}

	r = radv_amdgpu_bo_va_op(ws, buf_handle, 0, size, va, flags,
				 AMDGPU_VA_OP_MAP);
	if (r)
		goto error_va_map;

	bo->bo = buf_handle;
	bo->initial_domain = initial_domain;
	bo->is_shared = false;
	radv_amdgpu_add_buffer_to_global_list(bo);
	return (struct radeon_winsys_bo *)bo;
error_va_map:
	amdgpu_bo_free(buf_handle);

error_bo_alloc:
	amdgpu_va_range_free(va_handle);

error_va_alloc:
	FREE(bo);
	return NULL;
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
radv_amdgpu_winsys_bo_from_ptr(struct radeon_winsys *_ws,
                               void *pointer,
                               uint64_t size)
{
	struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);
	amdgpu_bo_handle buf_handle;
	struct radv_amdgpu_winsys_bo *bo;
	uint64_t va;
	amdgpu_va_handle va_handle;

	bo = CALLOC_STRUCT(radv_amdgpu_winsys_bo);
	if (!bo)
		return NULL;

	if (amdgpu_create_bo_from_user_mem(ws->dev, pointer, size, &buf_handle))
		goto error;

	if (amdgpu_va_range_alloc(ws->dev, amdgpu_gpu_va_range_general,
	                          size, 1 << 12, 0, &va, &va_handle, 0))
		goto error_va_alloc;

	if (amdgpu_bo_va_op(buf_handle, 0, size, va, 0, AMDGPU_VA_OP_MAP))
		goto error_va_map;

	/* Initialize it */
	bo->base.va = va;
	bo->va_handle = va_handle;
	bo->size = size;
	bo->ref_count = 1;
	bo->ws = ws;
	bo->bo = buf_handle;
	bo->initial_domain = RADEON_DOMAIN_GTT;

	radv_amdgpu_add_buffer_to_global_list(bo);
	return (struct radeon_winsys_bo *)bo;

error_va_map:
	amdgpu_va_range_free(va_handle);

error_va_alloc:
	amdgpu_bo_free(buf_handle);

error:
	FREE(bo);
	return NULL;
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

	r = radv_amdgpu_bo_va_op(ws, result.buf_handle, 0, result.alloc_size,
				 va, 0, AMDGPU_VA_OP_MAP);
	if (r)
		goto error_va_map;

	if (info.preferred_heap & AMDGPU_GEM_DOMAIN_VRAM)
		initial |= RADEON_DOMAIN_VRAM;
	if (info.preferred_heap & AMDGPU_GEM_DOMAIN_GTT)
		initial |= RADEON_DOMAIN_GTT;

	bo->bo = result.buf_handle;
	bo->base.va = va;
	bo->va_handle = va_handle;
	bo->initial_domain = initial;
	bo->size = result.alloc_size;
	bo->is_shared = true;
	bo->ws = ws;
	bo->ref_count = 1;
	radv_amdgpu_add_buffer_to_global_list(bo);
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

	if (bo->ws->info.chip_class >= GFX9) {
		tiling_flags |= AMDGPU_TILING_SET(SWIZZLE_MODE, md->u.gfx9.swizzle_mode);
	} else {
		if (md->u.legacy.macrotile == RADEON_LAYOUT_TILED)
			tiling_flags |= AMDGPU_TILING_SET(ARRAY_MODE, 4); /* 2D_TILED_THIN1 */
		else if (md->u.legacy.microtile == RADEON_LAYOUT_TILED)
			tiling_flags |= AMDGPU_TILING_SET(ARRAY_MODE, 2); /* 1D_TILED_THIN1 */
		else
			tiling_flags |= AMDGPU_TILING_SET(ARRAY_MODE, 1); /* LINEAR_ALIGNED */

		tiling_flags |= AMDGPU_TILING_SET(PIPE_CONFIG, md->u.legacy.pipe_config);
		tiling_flags |= AMDGPU_TILING_SET(BANK_WIDTH, util_logbase2(md->u.legacy.bankw));
		tiling_flags |= AMDGPU_TILING_SET(BANK_HEIGHT, util_logbase2(md->u.legacy.bankh));
		if (md->u.legacy.tile_split)
			tiling_flags |= AMDGPU_TILING_SET(TILE_SPLIT, radv_eg_tile_split_rev(md->u.legacy.tile_split));
		tiling_flags |= AMDGPU_TILING_SET(MACRO_TILE_ASPECT, util_logbase2(md->u.legacy.mtilea));
		tiling_flags |= AMDGPU_TILING_SET(NUM_BANKS, util_logbase2(md->u.legacy.num_banks)-1);

		if (md->u.legacy.scanout)
			tiling_flags |= AMDGPU_TILING_SET(MICRO_TILE_MODE, 0); /* DISPLAY_MICRO_TILING */
		else
			tiling_flags |= AMDGPU_TILING_SET(MICRO_TILE_MODE, 1); /* THIN_MICRO_TILING */
	}

	metadata.tiling_info = tiling_flags;
	metadata.size_metadata = md->size_metadata;
	memcpy(metadata.umd_metadata, md->metadata, sizeof(md->metadata));

	amdgpu_bo_set_metadata(bo->bo, &metadata);
}

void radv_amdgpu_bo_init_functions(struct radv_amdgpu_winsys *ws)
{
	ws->base.buffer_create = radv_amdgpu_winsys_bo_create;
	ws->base.buffer_destroy = radv_amdgpu_winsys_bo_destroy;
	ws->base.buffer_map = radv_amdgpu_winsys_bo_map;
	ws->base.buffer_unmap = radv_amdgpu_winsys_bo_unmap;
	ws->base.buffer_from_ptr = radv_amdgpu_winsys_bo_from_ptr;
	ws->base.buffer_from_fd = radv_amdgpu_winsys_bo_from_fd;
	ws->base.buffer_get_fd = radv_amdgpu_winsys_get_fd;
	ws->base.buffer_set_metadata = radv_amdgpu_winsys_bo_set_metadata;
	ws->base.buffer_virtual_bind = radv_amdgpu_winsys_bo_virtual_bind;
}
