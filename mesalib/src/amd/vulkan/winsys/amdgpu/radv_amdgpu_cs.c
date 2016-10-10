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

#include "amdgpu_id.h"
#include "radv_radeon_winsys.h"
#include "radv_amdgpu_cs.h"
#include "radv_amdgpu_bo.h"
#include "sid.h"

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
};

static inline struct radv_amdgpu_cs *
radv_amdgpu_cs(struct radeon_winsys_cs *base)
{
	return (struct radv_amdgpu_cs*)base;
}


static struct radeon_winsys_fence *radv_amdgpu_create_fence()
{
	struct radv_amdgpu_cs_fence *fence = calloc(1, sizeof(struct amdgpu_cs_fence));
	return (struct radeon_winsys_fence*)fence;
}

static void radv_amdgpu_destroy_fence(struct radeon_winsys_fence *_fence)
{
	struct amdgpu_cs_fence *fence = (struct amdgpu_cs_fence *)_fence;
	free(fence);
}

static bool radv_amdgpu_fence_wait(struct radeon_winsys *_ws,
			      struct radeon_winsys_fence *_fence,
			      bool absolute,
			      uint64_t timeout)
{
	struct amdgpu_cs_fence *fence = (struct amdgpu_cs_fence *)_fence;
	unsigned flags = absolute ? AMDGPU_QUERY_FENCE_TIMEOUT_IS_ABSOLUTE : 0;
	int r;
	uint32_t expired = 0;
	/* Now use the libdrm query. */
	r = amdgpu_cs_query_fence_status(fence,
					 timeout,
					 flags,
					 &expired);

	if (r) {
		fprintf(stderr, "amdgpu: radv_amdgpu_cs_query_fence_status failed.\n");
		return false;
	}

	if (expired) {
		return true;
	}
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
	free(cs->handles);
	free(cs->priorities);
	free(cs);
}

static boolean radv_amdgpu_init_cs(struct radv_amdgpu_cs *cs,
				   enum ring_type ring_type)
{
	for (int i = 0; i < ARRAY_SIZE(cs->buffer_hash_table); ++i) {
		cs->buffer_hash_table[i] = -1;
	}
	return true;
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
	radv_amdgpu_init_cs(cs, RING_GFX);

	if (cs->ws->use_ib_bos) {
		cs->ib_buffer = ws->buffer_create(ws, ib_size, 0,
						RADEON_DOMAIN_GTT,
						RADEON_FLAG_CPU_ACCESS);
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

		cs->ib.ib_mc_address = radv_amdgpu_winsys_bo(cs->ib_buffer)->va;
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
	uint64_t ib_size = MAX2(min_size * 4 + 16, cs->base.max_dw * 4 * 2);

	/* max that fits in the chain size field. */
	ib_size = MIN2(ib_size, 0xfffff);

	if (cs->failed) {
		cs->base.cdw = 0;
		return;
	}

	if (!cs->ws->use_ib_bos) {
		uint32_t *new_buf = realloc(cs->base.buf, ib_size);
		if (new_buf) {
			cs->base.buf = new_buf;
			cs->base.max_dw = ib_size / 4;
		} else {
			cs->failed = true;
			cs->base.cdw = 0;
		}
		return;
	}

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
						   RADEON_FLAG_CPU_ACCESS);

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
	cs->base.buf[cs->base.cdw++] = radv_amdgpu_winsys_bo(cs->ib_buffer)->va;
	cs->base.buf[cs->base.cdw++] = radv_amdgpu_winsys_bo(cs->ib_buffer)->va >> 32;
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

	cs->num_buffers = 0;

	if (cs->ws->use_ib_bos) {
		cs->ws->base.cs_add_buffer(&cs->base, cs->ib_buffer, 8);

		for (unsigned i = 0; i < cs->num_old_ib_buffers; ++i)
			cs->ws->base.buffer_destroy(cs->old_ib_buffers[i]);

		cs->num_old_ib_buffers = 0;
		cs->ib.ib_mc_address = radv_amdgpu_winsys_bo(cs->ib_buffer)->va;
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

	if(cs->handles[index] == bo)
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

static void radv_amdgpu_cs_add_buffer(struct radeon_winsys_cs *_cs,
				 struct radeon_winsys_bo *_bo,
				 uint8_t priority)
{
	struct radv_amdgpu_cs *cs = radv_amdgpu_cs(_cs);
	struct radv_amdgpu_winsys_bo *bo = radv_amdgpu_winsys_bo(_bo);

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
				      amdgpu_bo_list_handle *bo_list)
{
	int r;
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
	} else if (count == 1 && !extra_bo) {
		struct radv_amdgpu_cs *cs = (struct radv_amdgpu_cs*)cs_array[0];
		r = amdgpu_bo_list_create(ws->dev, cs->num_buffers, cs->handles,
					  cs->priorities, bo_list);
	} else {
		unsigned total_buffer_count = !!extra_bo;
		unsigned unique_bo_count = !!extra_bo;
		for (unsigned i = 0; i < count; ++i) {
			struct radv_amdgpu_cs *cs = (struct radv_amdgpu_cs*)cs_array[i];
			total_buffer_count += cs->num_buffers;
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

		for (unsigned i = 0; i < count; ++i) {
			struct radv_amdgpu_cs *cs = (struct radv_amdgpu_cs*)cs_array[i];
			for (unsigned j = 0; j < cs->num_buffers; ++j) {
				bool found = false;
				for (unsigned k = 0; k < unique_bo_count; ++k) {
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
		}
		r = amdgpu_bo_list_create(ws->dev, unique_bo_count, handles,
					  priorities, bo_list);

		free(handles);
		free(priorities);
	}
	return r;
}

static int radv_amdgpu_winsys_cs_submit_chained(struct radeon_winsys_ctx *_ctx,
						struct radeon_winsys_cs **cs_array,
						unsigned cs_count,
						struct radeon_winsys_fence *_fence)
{
	int r;
	struct radv_amdgpu_ctx *ctx = radv_amdgpu_ctx(_ctx);
	struct amdgpu_cs_fence *fence = (struct amdgpu_cs_fence *)_fence;
	struct radv_amdgpu_cs *cs0 = radv_amdgpu_cs(cs_array[0]);
	amdgpu_bo_list_handle bo_list;
	struct amdgpu_cs_request request = {0};

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

	r = radv_amdgpu_create_bo_list(cs0->ws, cs_array, cs_count, NULL, &bo_list);
	if (r) {
		fprintf(stderr, "amdgpu: Failed to created the BO list for submission\n");
		return r;
	}

	request.ip_type = AMDGPU_HW_IP_GFX;
	request.number_of_ibs = 1;
	request.ibs = &cs0->ib;
	request.resources = bo_list;

	r = amdgpu_cs_submit(ctx->ctx, 0, &request, 1);
	if (r) {
		if (r == -ENOMEM)
			fprintf(stderr, "amdgpu: Not enough memory for command submission.\n");
		else
			fprintf(stderr, "amdgpu: The CS has been rejected, "
					"see dmesg for more information.\n");
	}

	amdgpu_bo_list_destroy(bo_list);

	if (fence) {
		fence->context = ctx->ctx;
		fence->ip_type = request.ip_type;
		fence->ip_instance = request.ip_instance;
		fence->ring = request.ring;
		fence->fence = request.seq_no;
	}
	ctx->last_seq_no = request.seq_no;

	return r;
}

static int radv_amdgpu_winsys_cs_submit_fallback(struct radeon_winsys_ctx *_ctx,
						 struct radeon_winsys_cs **cs_array,
						 unsigned cs_count,
						 struct radeon_winsys_fence *_fence)
{
	int r;
	struct radv_amdgpu_ctx *ctx = radv_amdgpu_ctx(_ctx);
	struct amdgpu_cs_fence *fence = (struct amdgpu_cs_fence *)_fence;
	amdgpu_bo_list_handle bo_list;
	struct amdgpu_cs_request request;

	assert(cs_count);

	for (unsigned i = 0; i < cs_count;) {
		struct radv_amdgpu_cs *cs0 = radv_amdgpu_cs(cs_array[i]);
		struct amdgpu_cs_ib_info ibs[AMDGPU_CS_MAX_IBS_PER_SUBMIT];
		unsigned cnt = MIN2(AMDGPU_CS_MAX_IBS_PER_SUBMIT, cs_count - i);

		memset(&request, 0, sizeof(request));

		r = radv_amdgpu_create_bo_list(cs0->ws, &cs_array[i], cnt, NULL, &bo_list);
		if (r) {
			fprintf(stderr, "amdgpu: Failed to created the BO list for submission\n");
			return r;
		}

		request.ip_type = AMDGPU_HW_IP_GFX;
		request.resources = bo_list;
		request.number_of_ibs = cnt;
		request.ibs = ibs;

		for (unsigned j = 0; j < cnt; ++j) {
			struct radv_amdgpu_cs *cs = radv_amdgpu_cs(cs_array[i + j]);
			ibs[j] = cs->ib;

			if (cs->is_chained) {
				*cs->ib_size_ptr -= 4;
				cs->is_chained = false;
			}
		}

		r = amdgpu_cs_submit(ctx->ctx, 0, &request, 1);
		if (r) {
			if (r == -ENOMEM)
				fprintf(stderr, "amdgpu: Not enough memory for command submission.\n");
			else
				fprintf(stderr, "amdgpu: The CS has been rejected, "
						"see dmesg for more information.\n");
		}

		amdgpu_bo_list_destroy(bo_list);

		if (r)
			return r;

		i += cnt;
	}
	if (fence) {
		fence->context = ctx->ctx;
		fence->ip_type = request.ip_type;
		fence->ip_instance = request.ip_instance;
		fence->ring = request.ring;
		fence->fence = request.seq_no;
	}
	ctx->last_seq_no = request.seq_no;

	return 0;
}

static int radv_amdgpu_winsys_cs_submit_sysmem(struct radeon_winsys_ctx *_ctx,
					       struct radeon_winsys_cs **cs_array,
					       unsigned cs_count,
					       struct radeon_winsys_fence *_fence)
{
	int r;
	struct radv_amdgpu_ctx *ctx = radv_amdgpu_ctx(_ctx);
	struct amdgpu_cs_fence *fence = (struct amdgpu_cs_fence *)_fence;
	struct radv_amdgpu_cs *cs0 = radv_amdgpu_cs(cs_array[0]);
	struct radeon_winsys *ws = (struct radeon_winsys*)cs0->ws;
	amdgpu_bo_list_handle bo_list;
	struct amdgpu_cs_request request;
	uint32_t pad_word = 0xffff1000U;

	if (radv_amdgpu_winsys(ws)->family == FAMILY_SI)
		pad_word = 0x80000000;

	assert(cs_count);

	for (unsigned i = 0; i < cs_count;) {
		struct amdgpu_cs_ib_info ib = {0};
		struct radeon_winsys_bo *bo = NULL;
		uint32_t *ptr;
		unsigned cnt = 0;
		unsigned size = 0;

		while (i + cnt < cs_count && 0xffff8 - size >= radv_amdgpu_cs(cs_array[i + cnt])->base.cdw) {
			size += radv_amdgpu_cs(cs_array[i + cnt])->base.cdw;
			++cnt;
		}

		assert(cnt);

		bo = ws->buffer_create(ws, 4 * size, 4096, RADEON_DOMAIN_GTT, RADEON_FLAG_CPU_ACCESS);
		ptr = ws->buffer_map(bo);

		for (unsigned j = 0; j < cnt; ++j) {
			struct radv_amdgpu_cs *cs = radv_amdgpu_cs(cs_array[i + j]);
			memcpy(ptr, cs->base.buf, 4 * cs->base.cdw);
			ptr += cs->base.cdw;

		}

		while(!size || (size & 7)) {
			*ptr++ = pad_word;
			++size;
		}

		memset(&request, 0, sizeof(request));


		r = radv_amdgpu_create_bo_list(cs0->ws, &cs_array[i], cnt,
		                               (struct radv_amdgpu_winsys_bo*)bo, &bo_list);
		if (r) {
			fprintf(stderr, "amdgpu: Failed to created the BO list for submission\n");
			return r;
		}

		ib.size = size;
		ib.ib_mc_address = ws->buffer_get_va(bo);

		request.ip_type = AMDGPU_HW_IP_GFX;
		request.resources = bo_list;
		request.number_of_ibs = 1;
		request.ibs = &ib;

		r = amdgpu_cs_submit(ctx->ctx, 0, &request, 1);
		if (r) {
			if (r == -ENOMEM)
				fprintf(stderr, "amdgpu: Not enough memory for command submission.\n");
			else
				fprintf(stderr, "amdgpu: The CS has been rejected, "
						"see dmesg for more information.\n");
		}

		amdgpu_bo_list_destroy(bo_list);

		ws->buffer_destroy(bo);
		if (r)
			return r;

		i += cnt;
	}
	if (fence) {
		fence->context = ctx->ctx;
		fence->ip_type = request.ip_type;
		fence->ip_instance = request.ip_instance;
		fence->ring = request.ring;
		fence->fence = request.seq_no;
	}
	ctx->last_seq_no = request.seq_no;

	return 0;
}

static int radv_amdgpu_winsys_cs_submit(struct radeon_winsys_ctx *_ctx,
					struct radeon_winsys_cs **cs_array,
					unsigned cs_count,
					bool can_patch,
					struct radeon_winsys_fence *_fence)
{
	struct radv_amdgpu_cs *cs = radv_amdgpu_cs(cs_array[0]);
	if (!cs->ws->use_ib_bos) {
		return radv_amdgpu_winsys_cs_submit_sysmem(_ctx, cs_array,
							   cs_count, _fence);
	} else if (can_patch && cs_count > AMDGPU_CS_MAX_IBS_PER_SUBMIT && false) {
		return radv_amdgpu_winsys_cs_submit_chained(_ctx, cs_array,
							    cs_count, _fence);
	} else {
		return radv_amdgpu_winsys_cs_submit_fallback(_ctx, cs_array,
							     cs_count, _fence);
	}
}

static struct radeon_winsys_ctx *radv_amdgpu_ctx_create(struct radeon_winsys *_ws)
{
	struct radv_amdgpu_winsys *ws = radv_amdgpu_winsys(_ws);
	struct radv_amdgpu_ctx *ctx = CALLOC_STRUCT(radv_amdgpu_ctx);
	int r;

	if (!ctx)
		return NULL;
	r = amdgpu_cs_ctx_create(ws->dev, &ctx->ctx);
	if (r) {
		fprintf(stderr, "amdgpu: radv_amdgpu_cs_ctx_create failed. (%i)\n", r);
		goto error_create;
	}
	ctx->ws = ws;
	return (struct radeon_winsys_ctx *)ctx;
error_create:
	return NULL;
}

static void radv_amdgpu_ctx_destroy(struct radeon_winsys_ctx *rwctx)
{
	struct radv_amdgpu_ctx *ctx = (struct radv_amdgpu_ctx *)rwctx;
	amdgpu_cs_ctx_free(ctx->ctx);
	FREE(ctx);
}

static bool radv_amdgpu_ctx_wait_idle(struct radeon_winsys_ctx *rwctx)
{
	struct radv_amdgpu_ctx *ctx = (struct radv_amdgpu_ctx *)rwctx;

	if (ctx->last_seq_no) {
		uint32_t expired;
		struct amdgpu_cs_fence fence;

		fence.context = ctx->ctx;
		fence.ip_type = RING_GFX;
		fence.ip_instance = 0;
		fence.ring = 0;
		fence.fence = ctx->last_seq_no;

		int ret = amdgpu_cs_query_fence_status(&fence, 1000000000ull, 0,
						       &expired);

		if (ret || !expired)
			return false;
	}

	return true;
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
	ws->base.create_fence = radv_amdgpu_create_fence;
	ws->base.destroy_fence = radv_amdgpu_destroy_fence;
	ws->base.fence_wait = radv_amdgpu_fence_wait;
}
