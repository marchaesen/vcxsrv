/*
 * Copyrigh 2016 Red Hat Inc.
 * Based on anv:
 * Copyright © 2015 Intel Corporation
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

#include <assert.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "radv_private.h"
#include "radv_cs.h"
#include "sid.h"

static unsigned get_max_db(struct radv_device *device)
{
	unsigned num_db = device->physical_device->rad_info.num_render_backends;
	MAYBE_UNUSED unsigned rb_mask = device->physical_device->rad_info.enabled_rb_mask;

	if (device->physical_device->rad_info.chip_class == SI)
		num_db = 8;
	else
		num_db = MAX2(8, num_db);

	/* Otherwise we need to change the query reset procedure */
	assert(rb_mask == ((1ull << num_db) - 1));

	return num_db;
}

VkResult radv_CreateQueryPool(
	VkDevice                                    _device,
	const VkQueryPoolCreateInfo*                pCreateInfo,
	const VkAllocationCallbacks*                pAllocator,
	VkQueryPool*                                pQueryPool)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	uint64_t size;
	struct radv_query_pool *pool = vk_alloc2(&device->alloc, pAllocator,
					       sizeof(*pool), 8,
					       VK_SYSTEM_ALLOCATION_SCOPE_OBJECT);

	if (!pool)
		return VK_ERROR_OUT_OF_HOST_MEMORY;


	switch(pCreateInfo->queryType) {
	case VK_QUERY_TYPE_OCCLUSION:
		/* 16 bytes tmp. buffer as the compute packet writes 64 bits, but
		 * the app. may have 32 bits of space. */
		pool->stride = 16 * get_max_db(device) + 16;
		break;
	case VK_QUERY_TYPE_PIPELINE_STATISTICS:
		pool->stride = 16 * 11;
		break;
	case VK_QUERY_TYPE_TIMESTAMP:
		pool->stride = 8;
		break;
	default:
		unreachable("creating unhandled query type");
	}

	pool->type = pCreateInfo->queryType;
	pool->availability_offset = pool->stride * pCreateInfo->queryCount;
	size = pool->availability_offset + 4 * pCreateInfo->queryCount;

	pool->bo = device->ws->buffer_create(device->ws, size,
					     64, RADEON_DOMAIN_GTT, 0);

	if (!pool->bo) {
		vk_free2(&device->alloc, pAllocator, pool);
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}

	pool->ptr = device->ws->buffer_map(pool->bo);

	if (!pool->ptr) {
		device->ws->buffer_destroy(pool->bo);
		vk_free2(&device->alloc, pAllocator, pool);
		return VK_ERROR_OUT_OF_DEVICE_MEMORY;
	}
	memset(pool->ptr, 0, size);

	*pQueryPool = radv_query_pool_to_handle(pool);
	return VK_SUCCESS;
}

void radv_DestroyQueryPool(
	VkDevice                                    _device,
	VkQueryPool                                 _pool,
	const VkAllocationCallbacks*                pAllocator)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_query_pool, pool, _pool);

	if (!pool)
		return;

	device->ws->buffer_destroy(pool->bo);
	vk_free2(&device->alloc, pAllocator, pool);
}

VkResult radv_GetQueryPoolResults(
	VkDevice                                    _device,
	VkQueryPool                                 queryPool,
	uint32_t                                    firstQuery,
	uint32_t                                    queryCount,
	size_t                                      dataSize,
	void*                                       pData,
	VkDeviceSize                                stride,
	VkQueryResultFlags                          flags)
{
	RADV_FROM_HANDLE(radv_device, device, _device);
	RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);
	char *data = pData;
	VkResult result = VK_SUCCESS;

	for(unsigned i = 0; i < queryCount; ++i, data += stride) {
		char *dest = data;
		unsigned query = firstQuery + i;
		char *src = pool->ptr + query * pool->stride;
		uint32_t available;

		switch (pool->type) {
		case VK_QUERY_TYPE_TIMESTAMP: {
			if (flags & VK_QUERY_RESULT_WAIT_BIT) {
				while(!*(volatile uint32_t*)(pool->ptr + pool->availability_offset + 4 * query))
					;
			}

			available = *(uint32_t*)(pool->ptr + pool->availability_offset + 4 * query);
			if (!available && !(flags & VK_QUERY_RESULT_PARTIAL_BIT)) {
				result = VK_NOT_READY;
				break;

			}

			if (flags & VK_QUERY_RESULT_64_BIT) {
				*(uint64_t*)dest = *(uint64_t*)src;
				dest += 8;
			} else {
				*(uint32_t*)dest = *(uint32_t*)src;
				dest += 4;
			}
			break;
		}
		case VK_QUERY_TYPE_OCCLUSION: {
			volatile uint64_t const *src64 = (volatile uint64_t const *)src;
			uint64_t result = 0;
			int db_count = get_max_db(device);
			available = 1;

			for (int i = 0; i < db_count; ++i) {
				uint64_t start, end;
				do {
					start = src64[2 * i];
					end = src64[2 * i + 1];
				} while ((!(start & (1ull << 63)) || !(end & (1ull << 63))) && (flags & VK_QUERY_RESULT_WAIT_BIT));

				if (!(start & (1ull << 63)) || !(end & (1ull << 63)))
					available = 0;
				else {
					result += end - start;
				}
			}

			if (!available && !(flags & VK_QUERY_RESULT_PARTIAL_BIT)) {
				result = VK_NOT_READY;
				break;

			}

			if (flags & VK_QUERY_RESULT_64_BIT) {
				*(uint64_t*)dest = result;
				dest += 8;
			} else {
				*(uint32_t*)dest = result;
				dest += 4;
			}
			break;
		default:
			unreachable("trying to get results of unhandled query type");
		}
		}

		if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
			if (flags & VK_QUERY_RESULT_64_BIT) {
				*(uint64_t*)dest = available;
			} else {
				*(uint32_t*)dest = available;
			}
		}
	}

	return result;
}

void radv_CmdCopyQueryPoolResults(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    firstQuery,
    uint32_t                                    queryCount,
    VkBuffer                                    dstBuffer,
    VkDeviceSize                                dstOffset,
    VkDeviceSize                                stride,
    VkQueryResultFlags                          flags)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);
	RADV_FROM_HANDLE(radv_buffer, dst_buffer, dstBuffer);
	struct radeon_winsys_cs *cs = cmd_buffer->cs;
	uint64_t va = cmd_buffer->device->ws->buffer_get_va(pool->bo);
	uint64_t dest_va = cmd_buffer->device->ws->buffer_get_va(dst_buffer->bo);
	dest_va += dst_buffer->offset + dstOffset;

	cmd_buffer->device->ws->cs_add_buffer(cmd_buffer->cs, pool->bo, 8);
	cmd_buffer->device->ws->cs_add_buffer(cmd_buffer->cs, dst_buffer->bo, 8);

	for(unsigned i = 0; i < queryCount; ++i, dest_va += stride) {
		unsigned query = firstQuery + i;
		uint64_t local_src_va = va  + query * pool->stride;
		unsigned elem_size = (flags & VK_QUERY_RESULT_64_BIT) ? 8 : 4;

		MAYBE_UNUSED unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cs, 26);

		if (flags & VK_QUERY_RESULT_WAIT_BIT) {
			/* TODO, not sure if there is any case where we won't always be ready yet */
			uint64_t avail_va = va + pool->availability_offset + 4 * query;


			/* This waits on the ME. All copies below are done on the ME */
			radeon_emit(cs, PKT3(PKT3_WAIT_REG_MEM, 5, 0));
			radeon_emit(cs, WAIT_REG_MEM_EQUAL | WAIT_REG_MEM_MEM_SPACE(1));
			radeon_emit(cs, avail_va);
			radeon_emit(cs, avail_va >> 32);
			radeon_emit(cs, 1); /* reference value */
			radeon_emit(cs, 0xffffffff); /* mask */
			radeon_emit(cs, 4); /* poll interval */
		}

		switch (pool->type) {
		case VK_QUERY_TYPE_OCCLUSION:
			local_src_va += pool->stride - 16;

		case VK_QUERY_TYPE_TIMESTAMP:
			radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
			radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_MEM) |
					COPY_DATA_DST_SEL(COPY_DATA_MEM) |
					((flags & VK_QUERY_RESULT_64_BIT) ? COPY_DATA_COUNT_SEL : 0));
			radeon_emit(cs, local_src_va);
			radeon_emit(cs, local_src_va >> 32);
			radeon_emit(cs, dest_va);
			radeon_emit(cs, dest_va >> 32);
			break;
		default:
			unreachable("trying to get results of unhandled query type");
		}

		/* The flag could be still changed while the data copy is busy and we
		 * then might have invalid data, but a ready flag. However, the availability
		 * writes happen on the ME too, so they should be synchronized. Might need to
		 * revisit this with multiple queues.
		 */
		if (flags & VK_QUERY_RESULT_WITH_AVAILABILITY_BIT) {
			uint64_t avail_va = va + pool->availability_offset + 4 * query;
			uint64_t avail_dest_va = dest_va;
			if (pool->type != VK_QUERY_TYPE_PIPELINE_STATISTICS)
				avail_dest_va += elem_size;
			else
				abort();

			radeon_emit(cs, PKT3(PKT3_COPY_DATA, 4, 0));
			radeon_emit(cs, COPY_DATA_SRC_SEL(COPY_DATA_MEM) |
					COPY_DATA_DST_SEL(COPY_DATA_MEM));
			radeon_emit(cs, avail_va);
			radeon_emit(cs, avail_va >> 32);
			radeon_emit(cs, avail_dest_va);
			radeon_emit(cs, avail_dest_va >> 32);
		}

		assert(cs->cdw <= cdw_max);
	}

}

void radv_CmdResetQueryPool(
	VkCommandBuffer                             commandBuffer,
	VkQueryPool                                 queryPool,
	uint32_t                                    firstQuery,
	uint32_t                                    queryCount)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);
	uint64_t va = cmd_buffer->device->ws->buffer_get_va(pool->bo);

	cmd_buffer->device->ws->cs_add_buffer(cmd_buffer->cs, pool->bo, 8);

	si_cp_dma_clear_buffer(cmd_buffer, va + firstQuery * pool->stride,
			       queryCount * pool->stride, 0);
	si_cp_dma_clear_buffer(cmd_buffer, va + pool->availability_offset + firstQuery * 4,
			       queryCount * 4, 0);
}

void radv_CmdBeginQuery(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query,
    VkQueryControlFlags                         flags)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);
	struct radeon_winsys_cs *cs = cmd_buffer->cs;
	uint64_t va = cmd_buffer->device->ws->buffer_get_va(pool->bo);
	va += pool->stride * query;

	cmd_buffer->device->ws->cs_add_buffer(cs, pool->bo, 8);

	switch (pool->type) {
	case VK_QUERY_TYPE_OCCLUSION:
		radeon_check_space(cmd_buffer->device->ws, cs, 7);

		++cmd_buffer->state.active_occlusion_queries;
		if (cmd_buffer->state.active_occlusion_queries == 1)
			radv_set_db_count_control(cmd_buffer);

		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_ZPASS_DONE) | EVENT_INDEX(1));
		radeon_emit(cs, va);
		radeon_emit(cs, va >> 32);
		break;
	default:
		unreachable("beginning unhandled query type");
	}
}


void radv_CmdEndQuery(
    VkCommandBuffer                             commandBuffer,
    VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);
	struct radeon_winsys_cs *cs = cmd_buffer->cs;
	uint64_t va = cmd_buffer->device->ws->buffer_get_va(pool->bo);
	uint64_t avail_va = va + pool->availability_offset + 4 * query;
	va += pool->stride * query;

	cmd_buffer->device->ws->cs_add_buffer(cs, pool->bo, 8);

	switch (pool->type) {
	case VK_QUERY_TYPE_OCCLUSION:
		radeon_check_space(cmd_buffer->device->ws, cs, 14);

		cmd_buffer->state.active_occlusion_queries--;
		if (cmd_buffer->state.active_occlusion_queries == 0)
			radv_set_db_count_control(cmd_buffer);

		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE, 2, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_ZPASS_DONE) | EVENT_INDEX(1));
		radeon_emit(cs, va + 8);
		radeon_emit(cs, (va + 8) >> 32);

		/* hangs for VK_COMMAND_BUFFER_LEVEL_SECONDARY. */
		if (cmd_buffer->level == VK_COMMAND_BUFFER_LEVEL_PRIMARY) {
			radeon_emit(cs, PKT3(PKT3_OCCLUSION_QUERY, 3, 0));
			radeon_emit(cs, va);
			radeon_emit(cs, va >> 32);
			radeon_emit(cs, va + pool->stride - 16);
			radeon_emit(cs, (va + pool->stride - 16) >> 32);
		}

		break;
	default:
		unreachable("ending unhandled query type");
	}

	radeon_check_space(cmd_buffer->device->ws, cs, 5);

	radeon_emit(cs, PKT3(PKT3_WRITE_DATA, 3, 0));
	radeon_emit(cs, S_370_DST_SEL(V_370_MEMORY_SYNC) |
		    S_370_WR_CONFIRM(1) |
		    S_370_ENGINE_SEL(V_370_ME));
	radeon_emit(cs, avail_va);
	radeon_emit(cs, avail_va >> 32);
	radeon_emit(cs, 1);
}

void radv_CmdWriteTimestamp(
    VkCommandBuffer                             commandBuffer,
    VkPipelineStageFlagBits                     pipelineStage,
    VkQueryPool                                 queryPool,
    uint32_t                                    query)
{
	RADV_FROM_HANDLE(radv_cmd_buffer, cmd_buffer, commandBuffer);
	RADV_FROM_HANDLE(radv_query_pool, pool, queryPool);
	bool mec = radv_cmd_buffer_uses_mec(cmd_buffer);
	struct radeon_winsys_cs *cs = cmd_buffer->cs;
	uint64_t va = cmd_buffer->device->ws->buffer_get_va(pool->bo);
	uint64_t avail_va = va + pool->availability_offset + 4 * query;
	uint64_t query_va = va + pool->stride * query;

	cmd_buffer->device->ws->cs_add_buffer(cs, pool->bo, 5);

	MAYBE_UNUSED unsigned cdw_max = radeon_check_space(cmd_buffer->device->ws, cs, 12);

	if (mec) {
		radeon_emit(cs, PKT3(PKT3_RELEASE_MEM, 5, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_BOTTOM_OF_PIPE_TS) | EVENT_INDEX(5));
		radeon_emit(cs, 3 << 29);
		radeon_emit(cs, query_va);
		radeon_emit(cs, query_va >> 32);
		radeon_emit(cs, 0);
		radeon_emit(cs, 0);
	} else {
		radeon_emit(cs, PKT3(PKT3_EVENT_WRITE_EOP, 4, 0));
		radeon_emit(cs, EVENT_TYPE(V_028A90_BOTTOM_OF_PIPE_TS) | EVENT_INDEX(5));
		radeon_emit(cs, query_va);
		radeon_emit(cs, (3 << 29) | ((query_va >> 32) & 0xFFFF));
		radeon_emit(cs, 0);
		radeon_emit(cs, 0);
	}

	radeon_emit(cs, PKT3(PKT3_WRITE_DATA, 3, 0));
	radeon_emit(cs, S_370_DST_SEL(mec ? V_370_MEM_ASYNC : V_370_MEMORY_SYNC) |
		    S_370_WR_CONFIRM(1) |
		    S_370_ENGINE_SEL(V_370_ME));
	radeon_emit(cs, avail_va);
	radeon_emit(cs, avail_va >> 32);
	radeon_emit(cs, 1);

	assert(cmd_buffer->cs->cdw <= cdw_max);
}
