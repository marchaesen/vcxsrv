/*
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

#include <stdlib.h>
#include <amdgpu.h>
#include <amdgpu_drm.h>
#include <assert.h>
#include <pthread.h>
#include <errno.h>

#include "ac_debug.h"
#include "radv_radeon_winsys.h"
#include "radv_amdgpu_cs.h"
#include "radv_amdgpu_bo.h"
#include "sid.h"


enum {
	VIRTUAL_BUFFER_HASH_TABLE_SIZE = 1024
};

struct radv_amdgpu_cs {
	struct radeon_winsys_cs base;
	struct radv_amdgpu_winsys *ws;

	struct amdgpu_cs_ib_info    ib;

	struct radeon_winsys_bo     *ib_buffer;
	uint8_t                 *ib_mapped;
	unsigned                    max_num_buffers;
	unsigned                    num_buffers;
	amdgpu_bo_handle            *handles;
	uint8_t                     *priorities;

	struct radeon_winsys_bo     **old_ib_buffers;
	unsigned                    num_old_ib_buffers;
	unsigned                    max_num_old_ib_buffers;
	unsigned                    *ib_size_ptr;
	bool                        failed;
	bool                        is_chained;

	int                         buffer_hash_table[1024];
	unsigned                    hw_ip;

	unsigned                    num_virtual_buffers;
	unsigned                    max_num_virtual_buffers;
	struct radeon_winsys_bo     **virtual_buffers;
	uint8_t                     *virtual_buffer_priorities;
	int                         *virtual_buffer_hash_table;
};

static inline struct radv_amdgpu_cs *
radv_amdgpu_cs(struct radeon_winsys_cs *base)
{
	return (struct radv_amdgpu_cs*)base;
}

static int ring_to_hw_ip(enum ring_type ring)
{
	switch (ring) {
	case RING_GFX:
		return AMDGPU_HW_IP_GFX;
	case RING_DMA:
		return AMDGPU_HW_IP_DMA;
	case RING_COMPUTE:
		return AMDGPU_HW_IP_COMPUTE;
	default:
		unreachable("unsupported ring");
	}
}

static int radv_amdgpu_signal_sems(struct radv_amdgpu_ctx *ctx,
				   uint32_t ip_type,
				   uint32_t ring,
				   struct radv_winsys_sem_info *sem_info);
static int radv_amdgpu_cs_submit(struct radv_amdgpu_ctx *ctx,
				 struct amdgpu_cs_request *request,
				 struct radv_winsys_sem_info *sem_info);

static void radv_amdgpu_request_to_fence(struct radv_amdgpu_ctx *ctx,
					 struct radv_amdgpu_fence *fence,
					 struct amdgpu_cs_request *req)
{
	fence->fence.context = ctx->ctx;
	fence->fence.ip_type = req->ip_type;
	fence->fence.ip_instance = req->ip_instance;
	fence->fence.ring = req->ring;
	fence->fence.fence = req->seq_no;
	fence->user_ptr = (volatile uint64_t*)(ctx->fence_map + (req->ip_type * MAX_RINGS_PER_TYPE + req->ring) * sizeof(uint64_t));
}

static struct radeon_winsys_fence *radv_amdgpu_create_fence()
{
	struct radv_amdgpu_fence *fence = calloc(1, sizeof(struct radv_amdgpu_fence));
	return (struct radeon_winsys_fence*)fence;
}

static void radv_amdgpu_destroy_fence(struct radeon_winsys_fence *_fence)
{
	struct radv_amdgpu_fence *fence = (struct radv_amdgpu_fence *)_fence;
	free(fence);
}

static bool radv_amdgpu_fence_wait(struct radeon_winsys *_ws,
			      struct radeon_winsys_fence *_fence,
			      bool absolute,
			      uint64_t timeout)
{
	struct radv_amdgpu_fence *fence = (struct radv_amdgpu_fence *)_fence;
	unsigned flags = absolute ? AMDGPU_QUERY_FENCE_TIMEOUT_IS_ABSOLUTE : 0;
	int r;
	uint32_t expired = 0;

	if (fence->user_ptr) {
		if (*fence->user_ptr >= fence->fence.fence)
			return true;
		if (!absolute && !timeout)
			return false;
	}

	/* Now use the libdrm query. */
	r = amdgpu_cs_query_fence_status(&fence->fence,
	                                 timeout,
	                                 flags,
	                                 &expired);

	if (r) {
		fprintf(stderr, "amdgpu: radv_amdgpu_cs_query_fence_status failed.\n");
		return false;
	}

	if (expired)
		return true;

	return false;
}


static bool radv_amdgpu_fences_wait(struct radeon_winsys *_ws,
			      struct radeon_winsys_fence *const *_fences,
			      uint32_t fence_count,
			      bool wait_all,
			      uint64_t timeout)
{
	struct amdgpu_cs_fence *fences = malloc(sizeof(struct amdgpu_cs_fence) * fence_count);
	int r;
	uint32_t expired = 0, first = 0;

	if (!fences)
		return false;

	for (uint32_t i = 0; i < fence_count; ++i)
		fences[i] = ((struct radv_amdgpu_fence *)_fences[i])->fence;

	/* Now use the libdrm query. */
	r = amdgpu_cs_wait_fences(fences, fence_count, wait_all,
	                          timeout, &expired, &first);

	free(fences);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_cs_wait_fences failed.\n");
		return false;
	}

	if (expired)
		return true;

	return false;
}

static void radv_amdgpu_cs_destroy(struct radeon_winsys_cs *rcs)
{
	struct radv_amdgpu_cs *cs = radv_amdgpu_cs(rcs);

	if (cs->ib_buffer)
		cs->ws->base.buffer_destroy(cs->ib_buffer);
	else
		free(cs->base.buf);

	for (unsigned i = 0; i < cs->num_old_ib_buffers; ++i)
		cs->ws->base.buffer_destroy(cs->old_ib_buffers[i]);

	free(cs->old_ib_buffers);
	free(cs->virtual_buffers);
	free(cs->virtual_buffer_priorities);
	free(cs->virtual_buffer_hash_table);
	free(cs->handles);
	free(cs->priorities);
	free(cs);
}

static void radv_amdgpu_init_cs(struct radv_amdgpu_cs *cs,
				enum ring_type ring_type)
{
	for (int i = 0; i < ARRAY_SIZE(cs->buffer_hash_table); ++i)
		cs->buffer_hash_table[i] = -1;

	cs->hw_ip = ring_to_hw_ip(ring_type);
}

static struct radeon_winsys_cs *
radv_amdgpu_cs_create(struct radeon_winsys *ws,
		      enum ring_type ring_type)
{
	struct radv_amdgpu_cs *cs;
	uint32_t ib_size = 20 * 1024 * 4;
	cs = calloc(1, sizeof(struct radv_amdgpu_cs));
	if (!cs)
		return NULL;

	cs->ws = radv_amdgpu_winsys(ws);
	radv_amdgpu_init_cs(cs, ring_type);

	if (cs->ws->use_ib_bos) {
		cs->ib_buffer = ws->buffer_create(ws, ib_size, 0,
						  RADEON_DOMAIN_GTT,
						  RADEON_FLAG_CPU_ACCESS |
						  RADEON_FLAG_NO_INTERPROCESS_SHARING |
						  RADEON_FLAG_READ_ONLY);
		if (!cs->ib_buffer) {
			free(cs);
			return NULL;
		}

		cs->ib_mapped = ws->buffer_map(cs->ib_buffer);
		if (!cs->ib_mapped) {
			ws->buffer_destroy(cs->ib_buffer);
			free(cs);
			return NULL;
		}

		cs->ib.ib_mc_address = radv_amdgpu_winsys_bo(cs->ib_buffer)->base.va;
		cs->base.buf = (uint32_t *)cs->ib_mapped;
		cs->base.max_dw = ib_size / 4 - 4;
		cs->ib_size_ptr = &cs->ib.size;
		cs->ib.size = 0;

		ws->cs_add_buffer(&cs->base, cs->ib_buffer, 8);
	} else {
		cs->base.buf = malloc(16384);
		cs->base.max_dw = 4096;
		if (!cs->base.buf) {
			free(cs);
			return NULL;
		}
	}

	return &cs->base;
}

static void radv_amdgpu_cs_grow(struct radeon_winsys_cs *_cs, size_t min_size)
{
	struct radv_amdgpu_cs *cs = radv_amdgpu_cs(_cs);

	if (cs->failed) {
		cs->base.cdw = 0;
		return;
	}

	if (!cs->ws->use_ib_bos) {
		const uint64_t limit_dws = 0xffff8;
		uint64_t ib_dws = MAX2(cs->base.cdw + min_size,
				       MIN2(cs->base.max_dw * 2, limit_dws));

		/* The total ib size cannot exceed limit_dws dwords. */
		if (ib_dws > limit_dws)
		{
			cs->failed = true;
			cs->base.cdw = 0;
			return;
		}

		uint32_t *new_buf = realloc(cs->base.buf, ib_dws * 4);
		if (new_buf) {
			cs->base.buf = new_buf;
			cs->base.max_dw = ib_dws;
		} else {
			cs->failed = true;
			cs->base.cdw = 0;
		}
		return;
	}

	uint64_t ib_size = MAX2(min_size * 4 + 16, cs->base.max_dw * 4 * 2);

	/* max that fits in the chain size field. */
	ib_size = MIN2(ib_size, 0xfffff);

	while (!cs->base.cdw || (cs->base.cdw & 7) != 4)
		cs->base.buf[cs->base.cdw++] = 0xffff1000;

	*cs->ib_size_ptr |= cs->base.cdw + 4;

	if (cs->num_old_ib_buffers == cs->max_num_old_ib_buffers) {
		cs->max_num_old_ib_buffers = MAX2(1, cs->max_num_old_ib_buffers * 2);
		cs->old_ib_buffers = realloc(cs->old_ib_buffers,
					     cs->max_num_old_ib_buffers * sizeof(void*));
	}

	cs->old_ib_buffers[cs->num_old_ib_buffers++] = cs->ib_buffer;

	cs->ib_buffer = cs->ws->base.buffer_create(&cs->ws->base, ib_size, 0,
						   RADEON_DOMAIN_GTT,
						   RADEON_FLAG_CPU_ACCESS |
						   RADEON_FLAG_NO_INTERPROCESS_SHARING |
						   RADEON_FLAG_READ_ONLY);

	if (!cs->ib_buffer) {
		cs->base.cdw = 0;
		cs->failed = true;
		cs->ib_buffer = cs->old_ib_buffers[--cs->num_old_ib_buffers];
	}

	cs->ib_mapped = cs->ws->base.buffer_map(cs->ib_buffer);
	if (!cs->ib_mapped) {
		cs->ws->base.buffer_destroy(cs->ib_buffer);
		cs->base.cdw = 0;
		cs->failed = true;
		cs->ib_buffer = cs->old_ib_buffers[--cs->num_old_ib_buffers];
	}

	cs->ws->base.cs_add_buffer(&cs->base, cs->ib_buffer, 8);

	cs->base.buf[cs->base.cdw++] = PKT3(PKT3_INDIRECT_BUFFER_CIK, 2, 0);
	cs->base.buf[cs->base.cdw++] = radv_amdgpu_winsys_bo(cs->ib_buffer)->base.va;
	cs->base.buf[cs->base.cdw++] = radv_amdgpu_winsys_bo(cs->ib_buffer)->base.va >> 32;
	cs->ib_size_ptr = cs->base.buf + cs->base.cdw;
	cs->base.buf[cs->base.cdw++] = S_3F2_CHAIN(1) | S_3F2_VALID(1);

	cs->base.buf = (uint32_t *)cs->ib_mapped;
	cs->base.cdw = 0;
	cs->base.max_dw = ib_size / 4 - 4;

}

static bool radv_amdgpu_cs_finalize(struct radeon_winsys_cs *_cs)
{
	struct radv_amdgpu_cs *cs = radv_amdgpu_cs(_cs);

	if (cs->ws->use_ib_bos) {
		while (!cs->base.cdw || (cs->base.cdw & 7) != 0)
			cs->base.buf[cs->base.cdw++] = 0xffff1000;

		*cs->ib_size_ptr |= cs->base.cdw;

		cs->is_chained = false;
	}

	return !cs->failed;
}

static void radv_amdgpu_cs_reset(struct radeon_winsys_cs *_cs)
{
	struct radv_amdgpu_cs *cs = radv_amdgpu_cs(_cs);
	cs->base.cdw = 0;
	cs->failed = false;

	for (unsigned i = 0; i < cs->num_buffers; ++i) {
		unsigned hash = ((uintptr_t)cs->handles[i] >> 6) &
		                 (ARRAY_SIZE(cs->buffer_hash_table) - 1);
		cs->buffer_hash_table[hash] = -1;
	}

	for (unsigned i = 0; i < cs->num_virtual_buffers; ++i) {
		unsigned hash = ((uintptr_t)cs->virtual_buffers[i] >> 6) & (VIRTUAL_BUFFER_HASH_TABLE_SIZE - 1);
		cs->virtual_buffer_hash_table[hash] = -1;
	}

	cs->num_buffers = 0;
	cs->num_virtual_buffers = 0;

	if (cs->ws->use_ib_bos) {
		cs->ws->base.cs_add_buffer(&cs->base, cs->ib_buffer, 8);

		for (unsigned i = 0; i < cs->num_old_ib_buffers; ++i)
			cs->ws->base.buffer_destroy(cs->old_ib_buffers[i]);

		cs->num_old_ib_buffers = 0;
		cs->ib.ib_mc_address = radv_amdgpu_winsys_bo(cs->ib_buffer)->base.va;
		cs->ib_size_ptr = &cs->ib.size;
		cs->ib.size = 0;
	}
}

static int radv_amdgpu_cs_find_buffer(struct radv_amdgpu_cs *cs,
				      amdgpu_bo_handle bo)
{
	unsigned hash = ((uintptr_t)bo >> 6) & (ARRAY_SIZE(cs->buffer_hash_table) - 1);
	int index = cs->buffer_hash_table[hash];

	if (index == -1)
		return -1;

	if (cs->handles[index] == bo)
		return index;

	for (unsigned i = 0; i < cs->num_buffers; ++i) {
		if (cs->handles[i] == bo) {
			cs->buffer_hash_table[hash] = i;
			return i;
		}
	}

	return -1;
}

static void radv_amdgpu_cs_add_buffer_internal(struct radv_amdgpu_cs *cs,
					       amdgpu_bo_handle bo,
					       uint8_t priority)
{
	unsigned hash;
	int index = radv_amdgpu_cs_find_buffer(cs, bo);

	if (index != -1) {
		cs->priorities[index] = MAX2(cs->priorities[index], priority);
		return;
	}

	if (cs->num_buffers == cs->max_num_buffers) {
		unsigned new_count = MAX2(1, cs->max_num_buffers * 2);
		cs->handles = realloc(cs->handles, new_count * sizeof(amdgpu_bo_handle));
		cs->priorities = realloc(cs->priorities, new_count * sizeof(uint8_t));
		cs->max_num_buffers = new_count;
	}

	cs->handles[cs->num_buffers] = bo;
	cs->priorities[cs->num_buffers] = priority;

	hash = ((uintptr_t)bo >> 6) & (ARRAY_SIZE(cs->buffer_hash_table) - 1);
	cs->buffer_hash_table[hash] = cs->num_buffers;

	++cs->num_buffers;
}

static void radv_amdgpu_cs_add_virtual_buffer(struct radeon_winsys_cs *_cs,
                                              struct radeon_winsys_bo *bo,
                                              uint8_t priority)
{
	struct radv_amdgpu_cs *cs = radv_amdgpu_cs(_cs);
	unsigned hash = ((uintptr_t)bo >> 6) & (VIRTUAL_BUFFER_HASH_TABLE_SIZE - 1);


	if (!cs->virtual_buffer_hash_table) {
		cs->virtual_buffer_hash_table = malloc(VIRTUAL_BUFFER_HASH_TABLE_SIZE * sizeof(int));
		for (int i = 0; i < VIRTUAL_BUFFER_HASH_TABLE_SIZE; ++i)
			cs->virtual_buffer_hash_table[i] = -1;
	}

	if (cs->virtual_buffer_hash_table[hash] >= 0) {
		int idx = cs->virtual_buffer_hash_table[hash];
		if (cs->virtual_buffers[idx] == bo) {
			cs->virtual_buffer_priorities[idx] = MAX2(cs->virtual_buffer_priorities[idx], priority);
			return;
		}
		for (unsigned i = 0; i < cs->num_virtual_buffers; ++i) {
			if (cs->virtual_buffers[i] == bo) {
				cs->virtual_buffer_priorities[i] = MAX2(cs->virtual_buffer_priorities[i], priority);
				cs->virtual_buffer_hash_table[hash] = i;
				return;
			}
		}
	}

	if(cs->max_num_virtual_buffers <= cs->num_virtual_buffers) {
		cs->max_num_virtual_buffers = MAX2(2, cs->max_num_virtual_buffers * 2);
		cs->virtual_buffers = realloc(cs->virtual_buffers, sizeof(struct radv_amdgpu_virtual_virtual_buffer*) * cs->max_num_virtual_buffers);
		cs->virtual_buffer_priorities = realloc(cs->virtual_buffer_priorities, sizeof(uint8_t) * cs->max_num_virtual_buffers);
	}

	cs->virtual_buffers[cs->num_virtual_buffers] = bo;
	cs->virtual_buffer_priorities[cs->num_virtual_buffers] = priority;

	cs->virtual_buffer_hash_table[hash] = cs->num_virtual_buffers;
	++cs->num_virtual_buffers;

}

static void radv_amdgpu_cs_add_buffer(struct radeon_winsys_cs *_cs,
				 struct radeon_winsys_bo *_bo,
				 uint8_t priority)
{
	struct radv_amdgpu_cs *cs = radv_amdgpu_cs(_cs);
	struct radv_amdgpu_winsys_bo *bo = radv_amdgpu_winsys_bo(_bo);

	if (bo->is_virtual)  {
		radv_amdgpu_cs_add_virtual_buffer(_cs, _bo, priority);
		return;
	}

	if (bo->base.is_local)
		return;

	radv_amdgpu_cs_add_buffer_internal(cs, bo->bo, priority);
}

static void radv_amdgpu_cs_execute_secondary(struct radeon_winsys_cs *_parent,
					     struct radeon_winsys_cs *_child)
{
	struct radv_amdgpu_cs *parent = radv_amdgpu_cs(_parent);
	struct radv_amdgpu_cs *child = radv_amdgpu_cs(_child);

	for (unsigned i = 0; i < child->num_buffers; ++i) {
		radv_amdgpu_cs_add_buffer_internal(parent, child->handles[i],
						   child->priorities[i]);
	}

	for (unsigned i = 0; i < child->num_virtual_buffers; ++i) {
		radv_amdgpu_cs_add_buffer(&parent->base, child->virtual_buffers[i],
		                          child->virtual_buffer_priorities[i]);
	}

	if (parent->ws->use_ib_bos) {
		if (parent->base.cdw + 4 > parent->base.max_dw)
			radv_amdgpu_cs_grow(&parent->base, 4);

		parent->base.buf[parent->base.cdw++] = PKT3(PKT3_INDIRECT_BUFFER_CIK, 2, 0);
		parent->base.buf[parent->base.cdw++] = child->ib.ib_mc_address;
		parent->base.buf[parent->base.cdw++] = child->ib.ib_mc_address >> 32;
		parent->base.buf[parent->base.cdw++] = child->ib.size;
	} else {
		if (parent->base.cdw + child->base.cdw > parent->base.max_dw)
			radv_amdgpu_cs_grow(&parent->base, child->base.cdw);

		memcpy(parent->base.buf + parent->base.cdw, child->base.buf, 4 * child->base.cdw);
		parent->base.cdw += child->base.cdw;
	}
}

static int radv_amdgpu_create_bo_list(struct radv_amdgpu_winsys *ws,
				      struct radeon_winsys_cs **cs_array,
				      unsigned count,
				      struct radv_amdgpu_winsys_bo *extra_bo,
				      struct radeon_winsys_cs *extra_cs,
				      amdgpu_bo_list_handle *bo_list)
{
	int r = 0;

	if (ws->debug_all_bos) {
		struct radv_amdgpu_winsys_bo *bo;
		amdgpu_bo_handle *handles;
		unsigned num = 0;

		pthread_mutex_lock(&ws->global_bo_list_lock);

		handles = malloc(sizeof(handles[0]) * ws->num_buffers);
		if (!handles) {
			pthread_mutex_unlock(&ws->global_bo_list_lock);
			return -ENOMEM;
		}

		LIST_FOR_EACH_ENTRY(bo, &ws->global_bo_list, global_list_item) {
			assert(num < ws->num_buffers);
			handles[num++] = bo->bo;
		}

		r = amdgpu_bo_list_create(ws->dev, ws->num_buffers,
					  handles, NULL,
					  bo_list);
		free(handles);
		pthread_mutex_unlock(&ws->global_bo_list_lock);
	} else if (count == 1 && !extra_bo && !extra_cs &&
	           !radv_amdgpu_cs(cs_array[0])->num_virtual_buffers) {
		struct radv_amdgpu_cs *cs = (struct radv_amdgpu_cs*)cs_array[0];
		if (cs->num_buffers == 0) {
			*bo_list = 0;
			return 0;
		}
		r = amdgpu_bo_list_create(ws->dev, cs->num_buffers, cs->handles,
					  cs->priorities, bo_list);
	} else {
		unsigned total_buffer_count = !!extra_bo;
		unsigned unique_bo_count = !!extra_bo;
		for (unsigned i = 0; i < count; ++i) {
			struct radv_amdgpu_cs *cs = (struct radv_amdgpu_cs*)cs_array[i];
			total_buffer_count += cs->num_buffers;
			for (unsigned j = 0; j < cs->num_virtual_buffers; ++j)
				total_buffer_count += radv_amdgpu_winsys_bo(cs->virtual_buffers[j])->bo_count;
		}

		if (extra_cs) {
			total_buffer_count += ((struct radv_amdgpu_cs*)extra_cs)->num_buffers;
		}
		if (total_buffer_count == 0) {
			*bo_list = 0;
			return 0;
		}
		amdgpu_bo_handle *handles = malloc(sizeof(amdgpu_bo_handle) * total_buffer_count);
		uint8_t *priorities = malloc(sizeof(uint8_t) * total_buffer_count);
		if (!handles || !priorities) {
			free(handles);
			free(priorities);
			return -ENOMEM;
		}

		if (extra_bo) {
			handles[0] = extra_bo->bo;
			priorities[0] = 8;
		}

		for (unsigned i = 0; i < count + !!extra_cs; ++i) {
			struct radv_amdgpu_cs *cs;

			if (i == count)
				cs = (struct radv_amdgpu_cs*)extra_cs;
			else
				cs = (struct radv_amdgpu_cs*)cs_array[i];

			if (!cs->num_buffers)
				continue;

			if (unique_bo_count == 0) {
				memcpy(handles, cs->handles, cs->num_buffers * sizeof(amdgpu_bo_handle));
				memcpy(priorities, cs->priorities, cs->num_buffers * sizeof(uint8_t));
				unique_bo_count = cs->num_buffers;
				continue;
			}
			int unique_bo_so_far = unique_bo_count;
			for (unsigned j = 0; j < cs->num_buffers; ++j) {
				bool found = false;
				for (unsigned k = 0; k < unique_bo_so_far; ++k) {
					if (handles[k] == cs->handles[j]) {
						found = true;
						priorities[k] = MAX2(priorities[k],
								     cs->priorities[j]);
						break;
					}
				}
				if (!found) {
					handles[unique_bo_count] = cs->handles[j];
					priorities[unique_bo_count] = cs->priorities[j];
					++unique_bo_count;
				}
			}
			for (unsigned j = 0; j < cs->num_virtual_buffers; ++j) {
				struct radv_amdgpu_winsys_bo *virtual_bo = radv_amdgpu_winsys_bo(cs->virtual_buffers[j]);
				for(unsigned k = 0; k < virtual_bo->bo_count; ++k) {
					struct radv_amdgpu_winsys_bo *bo = virtual_bo->bos[k];
					bool found = false;
					for (unsigned m = 0; m < unique_bo_count; ++m) {
						if (handles[m] == bo->bo) {
							found = true;
							priorities[m] = MAX2(priorities[m],
									cs->virtual_buffer_priorities[j]);
							break;
						}
					}
					if (!found) {
						handles[unique_bo_count] = bo->bo;
						priorities[unique_bo_count] = cs->virtual_buffer_priorities[j];
						++unique_bo_count;
					}
				}
			}
		}

		if (unique_bo_count > 0) {
			r = amdgpu_bo_list_create(ws->dev, unique_bo_count, handles,
						  priorities, bo_list);
		} else {
			*bo_list = 0;
		}

		free(handles);
		free(priorities);
	}

	return r;
}

static struct amdgpu_cs_fence_info radv_set_cs_fence(struct radv_amdgpu_ctx *ctx, int ip_type, int ring)
{
	struct amdgpu_cs_fence_info ret = {0};
	if (ctx->fence_map) {
		ret.handle = radv_amdgpu_winsys_bo(ctx->fence_bo)->bo;
		ret.offset = (ip_type * MAX_RINGS_PER_TYPE + ring) * sizeof(uint64_t);
	}
	return ret;
}

static void radv_assign_last_submit(struct radv_amdgpu_ctx *ctx,
				    struct amdgpu_cs_request *request)
{
	radv_amdgpu_request_to_fence(ctx,
	                             &ctx->last_submission[request->ip_type][request->ring],
	                             request);
}

static int radv_amdgpu_winsys_cs_submit_chained(struct radeon_winsys_ctx *_ctx,
						int queue_idx,
						struct radv_winsys_sem_info *sem_info,
						struct radeon_winsys_cs **cs_array,
						unsigned cs_count,
						struct radeon_winsys_cs *initial_preamble_cs,
						struct radeon_winsys_cs *continue_preamble_cs,
						struct radeon_winsys_fence *_fence)
{
	int r;
	struct radv_amdgpu_ctx *ctx = radv_amdgpu_ctx(_ctx);
	struct radv_amdgpu_fence *fence = (struct radv_amdgpu_fence *)_fence;
	struct radv_amdgpu_cs *cs0 = radv_amdgpu_cs(cs_array[0]);
	amdgpu_bo_list_handle bo_list;
	struct amdgpu_cs_request request = {0};
	struct amdgpu_cs_ib_info ibs[2];

	for (unsigned i = cs_count; i--;) {
		struct radv_amdgpu_cs *cs = radv_amdgpu_cs(cs_array[i]);

		if (cs->is_chained) {
			*cs->ib_size_ptr -= 4;
			cs->is_chained = false;
		}

		if (i + 1 < cs_count) {
			struct radv_amdgpu_cs *next = radv_amdgpu_cs(cs_array[i + 1]);
			assert(cs->base.cdw + 4 <= cs->base.max_dw);

			cs->is_chained = true;
			*cs->ib_size_ptr += 4;

			cs->base.buf[cs->base.cdw + 0] = PKT3(PKT3_INDIRECT_BUFFER_CIK, 2, 0);
			cs->base.buf[cs->base.cdw + 1] = next->ib.ib_mc_address;
			cs->base.buf[cs->base.cdw + 2] = next->ib.ib_mc_address >> 32;
			cs->base.buf[cs->base.cdw + 3] = S_3F2_CHAIN(1) | S_3F2_VALID(1) | next->ib.size;
		}
	}

	r = radv_amdgpu_create_bo_list(cs0->ws, cs_array, cs_count, NULL, initial_preamble_cs, &bo_list);
	if (r) {
		fprintf(stderr, "amdgpu: buffer list creation failed for the "
				"chained submission(%d)\n", r);
		return r;
	}

	request.ip_type = cs0->hw_ip;
	request.ring = queue_idx;
	request.number_of_ibs = 1;
	request.ibs = &cs0->ib;
	request.resources = bo_list;
	request.fence_info = radv_set_cs_fence(ctx, cs0->hw_ip, queue_idx);

	if (initial_preamble_cs) {
		request.ibs = ibs;
		request.number_of_ibs = 2;
		ibs[1] = cs0->ib;
		ibs[0] = ((struct radv_amdgpu_cs*)initial_preamble_cs)->ib;
	}

	r = radv_amdgpu_cs_submit(ctx, &request, sem_info);
	if (r) {
		if (r == -ENOMEM)
			fprintf(stderr, "amdgpu: Not enough memory for command submission.\n");
		else
			fprintf(stderr, "amdgpu: The CS has been rejected, "
					"see dmesg for more information.\n");
	}

	if (bo_list)
		amdgpu_bo_list_destroy(bo_list);

	if (fence)
		radv_amdgpu_request_to_fence(ctx, fence, &request);

	radv_assign_last_submit(ctx, &request);

	return r;
}

static int radv_amdgpu_winsys_cs_submit_fallback(struct radeon_winsys_ctx *_ctx,
						 int queue_idx,
						 struct radv_winsys_sem_info *sem_info,
						 struct radeon_winsys_cs **cs_array,
						 unsigned cs_count,
						 struct radeon_winsys_cs *initial_preamble_cs,
						 struct radeon_winsys_cs *continue_preamble_cs,
						 struct radeon_winsys_fence *_fence)
{
	int r;
	struct radv_amdgpu_ctx *ctx = radv_amdgpu_ctx(_ctx);
	struct radv_amdgpu_fence *fence = (struct radv_amdgpu_fence *)_fence;
	amdgpu_bo_list_handle bo_list;
	struct amdgpu_cs_request request;
	bool emit_signal_sem = sem_info->cs_emit_signal;
	assert(cs_count);

	for (unsigned i = 0; i < cs_count;) {
		struct radv_amdgpu_cs *cs0 = radv_amdgpu_cs(cs_array[i]);
		struct amdgpu_cs_ib_info ibs[AMDGPU_CS_MAX_IBS_PER_SUBMIT];
		struct radeon_winsys_cs *preamble_cs = i ? continue_preamble_cs : initial_preamble_cs;
		unsigned cnt = MIN2(AMDGPU_CS_MAX_IBS_PER_SUBMIT - !!preamble_cs,
		                    cs_count - i);

		memset(&request, 0, sizeof(request));

		r = radv_amdgpu_create_bo_list(cs0->ws, &cs_array[i], cnt, NULL,
		                               preamble_cs, &bo_list);
		if (r) {
			fprintf(stderr, "amdgpu: buffer list creation failed "
					"for the fallback submission (%d)\n", r);
			return r;
		}

		request.ip_type = cs0->hw_ip;
		request.ring = queue_idx;
		request.resources = bo_list;
		request.number_of_ibs = cnt + !!preamble_cs;
		request.ibs = ibs;
		request.fence_info = radv_set_cs_fence(ctx, cs0->hw_ip, queue_idx);

		if (preamble_cs) {
			ibs[0] = radv_amdgpu_cs(preamble_cs)->ib;
		}

		for (unsigned j = 0; j < cnt; ++j) {
			struct radv_amdgpu_cs *cs = radv_amdgpu_cs(cs_array[i + j]);
			ibs[j + !!preamble_cs] = cs->ib;

			if (cs->is_chained) {
				*cs->ib_size_ptr -= 4;
				cs->is_chained = false;
			}
		}

		sem_info->cs_emit_signal = (i == cs_count - cnt) ? emit_signal_sem : false;
		r = radv_amdgpu_cs_submit(ctx, &request, sem_info);
		if (r) {
			if (r == -ENOMEM)
				fprintf(stderr, "amdgpu: Not enough memory for command submission.\n");
			else
				fprintf(stderr, "amdgpu: The CS has been rejected, "
						"see dmesg for more information.\n");
		}

		if (bo_list)
			amdgpu_bo_list_destroy(bo_list);

		if (r)
			return r;

		i += cnt;
	}
	if (fence)
		radv_amdgpu_request_to_fence(ctx, fence, &request);

	radv_assign_last_submit(ctx, &request);

	return 0;
}

static int radv_amdgpu_winsys_cs_submit_sysmem(struct radeon_winsys_ctx *_ctx,
					       int queue_idx,
					       struct radv_winsys_sem_info *sem_info,
					       struct radeon_winsys_cs **cs_array,
					       unsigned cs_count,
					       struct radeon_winsys_cs *initial_preamble_cs,
					       struct radeon_winsys_cs *continue_preamble_cs,
					       struct radeon_winsys_fence *_fence)
{
	int r;
	struct radv_amdgpu_ctx *ctx = radv_amdgpu_ctx(_ctx);
	struct radv_amdgpu_fence *fence = (struct radv_amdgpu_fence *)_fence;
	struct radv_amdgpu_cs *cs0 = radv_amdgpu_cs(cs_array[0]);
	struct radeon_winsys *ws = (struct radeon_winsys*)cs0->ws;
	amdgpu_bo_list_handle bo_list;
	struct amdgpu_cs_request request;
	uint32_t pad_word = 0xffff1000U;
	bool emit_signal_sem = sem_info->cs_emit_signal;

	if (radv_amdgpu_winsys(ws)->info.chip_class == SI)
		pad_word = 0x80000000;

	assert(cs_count);

	for (unsigned i = 0; i < cs_count;) {
		struct amdgpu_cs_ib_info ib = {0};
		struct radeon_winsys_bo *bo = NULL;
		struct radeon_winsys_cs *preamble_cs = i ? continue_preamble_cs : initial_preamble_cs;
		uint32_t *ptr;
		unsigned cnt = 0;
		unsigned size = 0;
		unsigned pad_words = 0;
		if (preamble_cs)
			size += preamble_cs->cdw;

		while (i + cnt < cs_count && 0xffff8 - size >= radv_amdgpu_cs(cs_array[i + cnt])->base.cdw) {
			size += radv_amdgpu_cs(cs_array[i + cnt])->base.cdw;
			++cnt;
		}

		while(!size || (size & 7)) {
			size++;
			pad_words++;
		}
		assert(cnt);

		bo = ws->buffer_create(ws, 4 * size, 4096, RADEON_DOMAIN_GTT,
				       RADEON_FLAG_CPU_ACCESS |
				       RADEON_FLAG_NO_INTERPROCESS_SHARING |
				       RADEON_FLAG_READ_ONLY);
		ptr = ws->buffer_map(bo);

		if (preamble_cs) {
			memcpy(ptr, preamble_cs->buf, preamble_cs->cdw * 4);
			ptr += preamble_cs->cdw;
		}

		for (unsigned j = 0; j < cnt; ++j) {
			struct radv_amdgpu_cs *cs = radv_amdgpu_cs(cs_array[i + j]);
			memcpy(ptr, cs->base.buf, 4 * cs->base.cdw);
			ptr += cs->base.cdw;

		}

		for (unsigned j = 0; j < pad_words; ++j)
			*ptr++ = pad_word;

		memset(&request, 0, sizeof(request));


		r = radv_amdgpu_create_bo_list(cs0->ws, &cs_array[i], cnt,
		                               (struct radv_amdgpu_winsys_bo*)bo,
		                               preamble_cs, &bo_list);
		if (r) {
			fprintf(stderr, "amdgpu: buffer list creation failed "
					"for the sysmem submission (%d)\n", r);
			return r;
		}

		ib.size = size;
		ib.ib_mc_address = radv_buffer_get_va(bo);

		request.ip_type = cs0->hw_ip;
		request.ring = queue_idx;
		request.resources = bo_list;
		request.number_of_ibs = 1;
		request.ibs = &ib;
		request.fence_info = radv_set_cs_fence(ctx, cs0->hw_ip, queue_idx);

		sem_info->cs_emit_signal = (i == cs_count - cnt) ? emit_signal_sem : false;
		r = radv_amdgpu_cs_submit(ctx, &request, sem_info);
		if (r) {
			if (r == -ENOMEM)
				fprintf(stderr, "amdgpu: Not enough memory for command submission.\n");
			else
				fprintf(stderr, "amdgpu: The CS has been rejected, "
						"see dmesg for more information.\n");
		}

		if (bo_list)
			amdgpu_bo_list_destroy(bo_list);

		ws->buffer_destroy(bo);
		if (r)
			return r;

		i += cnt;
	}
	if (fence)
		radv_amdgpu_request_to_fence(ctx, fence, &request);

	radv_assign_last_submit(ctx, &request);

	return 0;
}

static int radv_amdgpu_winsys_cs_submit(struct radeon_winsys_ctx *_ctx,
					int queue_idx,
					struct radeon_winsys_cs **cs_array,
					unsigned cs_count,
					struct radeon_winsys_cs *initial_preamble_cs,
					struct radeon_winsys_cs *continue_preamble_cs,
					struct radv_winsys_sem_info *sem_info,
					bool can_patch,
					struct radeon_winsys_fence *_fence)
{
	struct radv_amdgpu_cs *cs = radv_amdgpu_cs(cs_array[0]);
	struct radv_amdgpu_ctx *ctx = radv_amdgpu_ctx(_ctx);
	int ret;

	assert(sem_info);
	if (!cs->ws->use_ib_bos) {
		ret = radv_amdgpu_winsys_cs_submit_sysmem(_ctx, queue_idx, sem_info, cs_array,
							   cs_count, initial_preamble_cs, continue_preamble_cs, _fence);
	} else if (can_patch && cs_count > AMDGPU_CS_MAX_IBS_PER_SUBMIT && cs->ws->batchchain) {
		ret = radv_amdgpu_winsys_cs_submit_chained(_ctx, queue_idx, sem_info, cs_array,
							    cs_count, initial_preamble_cs, continue_preamble_cs, _fence);
	} else {
		ret = radv_amdgpu_winsys_cs_submit_fallback(_ctx, queue_idx, sem_info, cs_array,
							     cs_count, initial_preamble_cs, continue_preamble_cs, _fence);
	}

	radv_amdgpu_signal_sems(ctx, cs->hw_ip, queue_idx, sem_info);
	return ret;
}

static void *radv_amdgpu_winsys_get_cpu_addr(void *_cs, uint64_t addr)
{
	struct radv_amdgpu_cs *cs = (struct radv_amdgpu_cs *)_cs;
	void *ret = NULL;

	if (!cs->ib_buffer)
		return NULL;
	for (unsigned i = 0; i <= cs->num_old_ib_buffers; ++i) {
		struct radv_amdgpu_winsys_bo *bo;

		bo = (struct radv_amdgpu_winsys_bo*)
		       (i == cs->num_old_ib_buffers ? cs->ib_buffer : cs->old_ib_buffers[i]);
		if (addr >= bo->base.va && addr - bo->base.va < bo->size) {
			if (amdgpu_bo_cpu_map(bo->bo, &ret) == 0)
				return (char *)ret + (addr - bo->base.va);
		}
	}
	if(cs->ws->debug_all_bos) {
		pthread_mutex_lock(&cs->ws->global_bo_list_lock);
		list_for_each_entry(struct radv_amdgpu_winsys_bo, bo,
		                    &cs->ws->global_bo_list, global_list_item) {
			if (addr >= bo->base.va && addr - bo->base.va < bo->size) {
				if (amdgpu_bo_cpu_map(bo->bo, &ret) == 0) {
					pthread_mutex_unlock(&cs->ws->global_bo_list_lock);
					return (char *)ret + (addr - bo->base.va);
				}
			}
		}
		pthread_mutex_unlock(&cs->ws->global_bo_list_lock);
	}
	return ret;
}

static void radv_amdgpu_winsys_cs_dump(struct radeon_winsys_cs *_cs,
                                       FILE* file,
                                       const int *trace_ids, int trace_id_count)
{
	struct radv_amdgpu_cs *cs = (struct radv_amdgpu_cs *)_cs;
	void *ib = cs->base.buf;
	int num_dw = cs->base.cdw;

	if (cs->ws->use_ib_bos) {
		ib = radv_amdgpu_winsys_get_cpu_addr(cs, cs->ib.ib_mc_address);
		num_dw = cs->ib.size;
	}
	assert(ib);
	ac_parse_ib(file, ib, num_dw, trace_ids, trace_id_count,  "main IB",
		    cs->ws->info.chip_class, radv_amdgpu_winsys_get_cpu_addr, cs);
}

static uint32_t radv_to_amdgpu_priority(enum radeon_ctx_priority radv_priority)
{
	switch (radv_priority) {
		case RADEON_CTX_PRIORITY_REALTIME:
			return AMDGPU_CTX_PRIORITY_VERY_HIGH;
		case RADEON_CTX_PRIORITY_HIGH:
			return AMDGPU_CTX_PRIORITY_HIGH;
		case RADEON_CTX_PRIORITY_MEDIUM:
			return AMDGPU_CTX_PRIORITY_NORMAL;
		case RADEON_CTX_PRIORITY_LOW:
			return AMDGPU_CTX_PRIORITY_LOW;
		default:
			unreachable("Invalid context priority");
	}
}

static struct radeon_winsys_ctx *radv_amdgpu_ctx_create(struct radeon_winsys *_ws,
							enum radeon_ctx_priority priority)
{
	struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);
	struct radv_amdgpu_ctx *ctx = CALLOC_STRUCT(radv_amdgpu_ctx);
	uint32_t amdgpu_priority = radv_to_amdgpu_priority(priority);
	int r;

	if (!ctx)
		return NULL;

	r = amdgpu_cs_ctx_create2(ws->dev, amdgpu_priority, &ctx->ctx);
	if (r) {
		fprintf(stderr, "amdgpu: radv_amdgpu_cs_ctx_create2 failed. (%i)\n", r);
		goto error_create;
	}
	ctx->ws = ws;

	assert(AMDGPU_HW_IP_NUM * MAX_RINGS_PER_TYPE * sizeof(uint64_t) <= 4096);
	ctx->fence_bo = ws->base.buffer_create(&ws->base, 4096, 8,
	                                      RADEON_DOMAIN_GTT,
	                                      RADEON_FLAG_CPU_ACCESS|
					       RADEON_FLAG_NO_INTERPROCESS_SHARING);
	if (ctx->fence_bo)
		ctx->fence_map = (uint64_t*)ws->base.buffer_map(ctx->fence_bo);
	if (ctx->fence_map)
		memset(ctx->fence_map, 0, 4096);
	return (struct radeon_winsys_ctx *)ctx;
error_create:
	FREE(ctx);
	return NULL;
}

static void radv_amdgpu_ctx_destroy(struct radeon_winsys_ctx *rwctx)
{
	struct radv_amdgpu_ctx *ctx = (struct radv_amdgpu_ctx *)rwctx;
	ctx->ws->base.buffer_destroy(ctx->fence_bo);
	amdgpu_cs_ctx_free(ctx->ctx);
	FREE(ctx);
}

static bool radv_amdgpu_ctx_wait_idle(struct radeon_winsys_ctx *rwctx,
                                      enum ring_type ring_type, int ring_index)
{
	struct radv_amdgpu_ctx *ctx = (struct radv_amdgpu_ctx *)rwctx;
	int ip_type = ring_to_hw_ip(ring_type);

	if (ctx->last_submission[ip_type][ring_index].fence.fence) {
		uint32_t expired;
		int ret = amdgpu_cs_query_fence_status(&ctx->last_submission[ip_type][ring_index].fence,
		                                       1000000000ull, 0, &expired);

		if (ret || !expired)
			return false;
	}

	return true;
}

static struct radeon_winsys_sem *radv_amdgpu_create_sem(struct radeon_winsys *_ws)
{
	struct amdgpu_cs_fence *sem = CALLOC_STRUCT(amdgpu_cs_fence);
	if (!sem)
		return NULL;

	return (struct radeon_winsys_sem *)sem;
}

static void radv_amdgpu_destroy_sem(struct radeon_winsys_sem *_sem)
{
	struct amdgpu_cs_fence *sem = (struct amdgpu_cs_fence *)_sem;
	FREE(sem);
}

static int radv_amdgpu_signal_sems(struct radv_amdgpu_ctx *ctx,
				   uint32_t ip_type,
				   uint32_t ring,
				   struct radv_winsys_sem_info *sem_info)
{
	for (unsigned i = 0; i < sem_info->signal.sem_count; i++) {
		struct amdgpu_cs_fence *sem = (struct amdgpu_cs_fence *)(sem_info->signal.sem)[i];

		if (sem->context)
			return -EINVAL;

		*sem = ctx->last_submission[ip_type][ring].fence;
	}
	return 0;
}

static struct drm_amdgpu_cs_chunk_sem *radv_amdgpu_cs_alloc_syncobj_chunk(struct radv_winsys_sem_counts *counts,
									  struct drm_amdgpu_cs_chunk *chunk, int chunk_id)
{
	struct drm_amdgpu_cs_chunk_sem *syncobj = malloc(sizeof(struct drm_amdgpu_cs_chunk_sem) * counts->syncobj_count);
	if (!syncobj)
		return NULL;

	for (unsigned i = 0; i < counts->syncobj_count; i++) {
		struct drm_amdgpu_cs_chunk_sem *sem = &syncobj[i];
		sem->handle = counts->syncobj[i];
	}

	chunk->chunk_id = chunk_id;
	chunk->length_dw = sizeof(struct drm_amdgpu_cs_chunk_sem) / 4 * counts->syncobj_count;
	chunk->chunk_data = (uint64_t)(uintptr_t)syncobj;
	return syncobj;
}

static int radv_amdgpu_cs_submit(struct radv_amdgpu_ctx *ctx,
				 struct amdgpu_cs_request *request,
				 struct radv_winsys_sem_info *sem_info)
{
	int r;
	int num_chunks;
	int size;
	bool user_fence;
	struct drm_amdgpu_cs_chunk *chunks;
	struct drm_amdgpu_cs_chunk_data *chunk_data;
	struct drm_amdgpu_cs_chunk_dep *sem_dependencies = NULL;
	struct drm_amdgpu_cs_chunk_sem *wait_syncobj = NULL, *signal_syncobj = NULL;
	int i;
	struct amdgpu_cs_fence *sem;

	user_fence = (request->fence_info.handle != NULL);
	size = request->number_of_ibs + (user_fence ? 2 : 1) + 3;

	chunks = alloca(sizeof(struct drm_amdgpu_cs_chunk) * size);

	size = request->number_of_ibs + (user_fence ? 1 : 0);

	chunk_data = alloca(sizeof(struct drm_amdgpu_cs_chunk_data) * size);

	num_chunks = request->number_of_ibs;
	for (i = 0; i < request->number_of_ibs; i++) {
		struct amdgpu_cs_ib_info *ib;
		chunks[i].chunk_id = AMDGPU_CHUNK_ID_IB;
		chunks[i].length_dw = sizeof(struct drm_amdgpu_cs_chunk_ib) / 4;
		chunks[i].chunk_data = (uint64_t)(uintptr_t)&chunk_data[i];

		ib = &request->ibs[i];

		chunk_data[i].ib_data._pad = 0;
		chunk_data[i].ib_data.va_start = ib->ib_mc_address;
		chunk_data[i].ib_data.ib_bytes = ib->size * 4;
		chunk_data[i].ib_data.ip_type = request->ip_type;
		chunk_data[i].ib_data.ip_instance = request->ip_instance;
		chunk_data[i].ib_data.ring = request->ring;
		chunk_data[i].ib_data.flags = ib->flags;
	}

	if (user_fence) {
		i = num_chunks++;

		chunks[i].chunk_id = AMDGPU_CHUNK_ID_FENCE;
		chunks[i].length_dw = sizeof(struct drm_amdgpu_cs_chunk_fence) / 4;
		chunks[i].chunk_data = (uint64_t)(uintptr_t)&chunk_data[i];

		amdgpu_cs_chunk_fence_info_to_data(&request->fence_info,
						   &chunk_data[i]);
	}

	if (sem_info->wait.syncobj_count && sem_info->cs_emit_wait) {
		wait_syncobj = radv_amdgpu_cs_alloc_syncobj_chunk(&sem_info->wait,
								  &chunks[num_chunks],
								  AMDGPU_CHUNK_ID_SYNCOBJ_IN);
		if (!wait_syncobj) {
			r = -ENOMEM;
			goto error_out;
		}
		num_chunks++;

		if (sem_info->wait.sem_count == 0)
			sem_info->cs_emit_wait = false;

	}

	if (sem_info->wait.sem_count && sem_info->cs_emit_wait) {
		sem_dependencies = malloc(sizeof(struct drm_amdgpu_cs_chunk_dep) * sem_info->wait.sem_count);
		if (!sem_dependencies) {
			r = -ENOMEM;
			goto error_out;
		}
		int sem_count = 0;
		for (unsigned j = 0; j < sem_info->wait.sem_count; j++) {
			sem = (struct amdgpu_cs_fence *)sem_info->wait.sem[j];
			if (!sem->context)
				continue;
			struct drm_amdgpu_cs_chunk_dep *dep = &sem_dependencies[sem_count++];

			amdgpu_cs_chunk_fence_to_dep(sem, dep);

			sem->context = NULL;
		}
		i = num_chunks++;

		/* dependencies chunk */
		chunks[i].chunk_id = AMDGPU_CHUNK_ID_DEPENDENCIES;
		chunks[i].length_dw = sizeof(struct drm_amdgpu_cs_chunk_dep) / 4 * sem_count;
		chunks[i].chunk_data = (uint64_t)(uintptr_t)sem_dependencies;

		sem_info->cs_emit_wait = false;
	}

	if (sem_info->signal.syncobj_count && sem_info->cs_emit_signal) {
		signal_syncobj = radv_amdgpu_cs_alloc_syncobj_chunk(&sem_info->signal,
								    &chunks[num_chunks],
								    AMDGPU_CHUNK_ID_SYNCOBJ_OUT);
		if (!signal_syncobj) {
			r = -ENOMEM;
			goto error_out;
		}
		num_chunks++;
	}

	r = amdgpu_cs_submit_raw(ctx->ws->dev,
				 ctx->ctx,
				 request->resources,
				 num_chunks,
				 chunks,
				 &request->seq_no);
error_out:
	free(sem_dependencies);
	free(wait_syncobj);
	free(signal_syncobj);
	return r;
}

static int radv_amdgpu_create_syncobj(struct radeon_winsys *_ws,
				      uint32_t *handle)
{
	struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);
	return amdgpu_cs_create_syncobj(ws->dev, handle);
}

static void radv_amdgpu_destroy_syncobj(struct radeon_winsys *_ws,
				    uint32_t handle)
{
	struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);
	amdgpu_cs_destroy_syncobj(ws->dev, handle);
}

static void radv_amdgpu_reset_syncobj(struct radeon_winsys *_ws,
				    uint32_t handle)
{
	struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);
	amdgpu_cs_syncobj_reset(ws->dev, &handle, 1);
}

static void radv_amdgpu_signal_syncobj(struct radeon_winsys *_ws,
				    uint32_t handle)
{
	struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);
	amdgpu_cs_syncobj_signal(ws->dev, &handle, 1);
}

static bool radv_amdgpu_wait_syncobj(struct radeon_winsys *_ws, const uint32_t *handles,
                                     uint32_t handle_count, bool wait_all, uint64_t timeout)
{
	struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);
	uint32_t tmp;

	/* The timeouts are signed, while vulkan timeouts are unsigned. */
	timeout = MIN2(timeout, INT64_MAX);

	int ret = amdgpu_cs_syncobj_wait(ws->dev, (uint32_t*)handles, handle_count, timeout,
					 DRM_SYNCOBJ_WAIT_FLAGS_WAIT_FOR_SUBMIT |
					 (wait_all ? DRM_SYNCOBJ_WAIT_FLAGS_WAIT_ALL : 0),
					 &tmp);
	if (ret == 0) {
		return true;
	} else if (ret == -1 && errno == ETIME) {
		return false;
	} else {
		fprintf(stderr, "amdgpu: radv_amdgpu_wait_syncobj failed!\nerrno: %d\n", errno);
		return false;
	}
}

static int radv_amdgpu_export_syncobj(struct radeon_winsys *_ws,
				      uint32_t syncobj,
				      int *fd)
{
	struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);

	return amdgpu_cs_export_syncobj(ws->dev, syncobj, fd);
}

static int radv_amdgpu_import_syncobj(struct radeon_winsys *_ws,
				      int fd,
				      uint32_t *syncobj)
{
	struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);

	return amdgpu_cs_import_syncobj(ws->dev, fd, syncobj);
}


static int radv_amdgpu_export_syncobj_to_sync_file(struct radeon_winsys *_ws,
                                                   uint32_t syncobj,
                                                   int *fd)
{
	struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);

	return amdgpu_cs_syncobj_export_sync_file(ws->dev, syncobj, fd);
}

static int radv_amdgpu_import_syncobj_from_sync_file(struct radeon_winsys *_ws,
                                                     uint32_t syncobj,
                                                     int fd)
{
	struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);

	return amdgpu_cs_syncobj_import_sync_file(ws->dev, syncobj, fd);
}

void radv_amdgpu_cs_init_functions(struct radv_amdgpu_winsys *ws)
{
	ws->base.ctx_create = radv_amdgpu_ctx_create;
	ws->base.ctx_destroy = radv_amdgpu_ctx_destroy;
	ws->base.ctx_wait_idle = radv_amdgpu_ctx_wait_idle;
	ws->base.cs_create = radv_amdgpu_cs_create;
	ws->base.cs_destroy = radv_amdgpu_cs_destroy;
	ws->base.cs_grow = radv_amdgpu_cs_grow;
	ws->base.cs_finalize = radv_amdgpu_cs_finalize;
	ws->base.cs_reset = radv_amdgpu_cs_reset;
	ws->base.cs_add_buffer = radv_amdgpu_cs_add_buffer;
	ws->base.cs_execute_secondary = radv_amdgpu_cs_execute_secondary;
	ws->base.cs_submit = radv_amdgpu_winsys_cs_submit;
	ws->base.cs_dump = radv_amdgpu_winsys_cs_dump;
	ws->base.create_fence = radv_amdgpu_create_fence;
	ws->base.destroy_fence = radv_amdgpu_destroy_fence;
	ws->base.create_sem = radv_amdgpu_create_sem;
	ws->base.destroy_sem = radv_amdgpu_destroy_sem;
	ws->base.create_syncobj = radv_amdgpu_create_syncobj;
	ws->base.destroy_syncobj = radv_amdgpu_destroy_syncobj;
	ws->base.reset_syncobj = radv_amdgpu_reset_syncobj;
	ws->base.signal_syncobj = radv_amdgpu_signal_syncobj;
	ws->base.wait_syncobj = radv_amdgpu_wait_syncobj;
	ws->base.export_syncobj = radv_amdgpu_export_syncobj;
	ws->base.import_syncobj = radv_amdgpu_import_syncobj;
	ws->base.export_syncobj_to_sync_file = radv_amdgpu_export_syncobj_to_sync_file;
	ws->base.import_syncobj_from_sync_file = radv_amdgpu_import_syncobj_from_sync_file;
	ws->base.fence_wait = radv_amdgpu_fence_wait;
	ws->base.fences_wait = radv_amdgpu_fences_wait;
}
