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
#include "radv_amdgpu_winsys.h"
#include "radv_amdgpu_winsys_public.h"
#include "radv_amdgpu_surface.h"
#include "radv_debug.h"
#include "ac_surface.h"
#include "xf86drm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <amdgpu_drm.h>
#include <assert.h>
#include "radv_amdgpu_cs.h"
#include "radv_amdgpu_bo.h"
#include "radv_amdgpu_surface.h"

static bool
do_winsys_init(struct radv_amdgpu_winsys *ws, int fd)
{
	if (!ac_query_gpu_info(fd, ws->dev, &ws->info, &ws->amdinfo))
		return false;

	ws->addrlib = amdgpu_addr_create(&ws->info, &ws->amdinfo, &ws->info.max_alignment);
	if (!ws->addrlib) {
		fprintf(stderr, "amdgpu: Cannot create addrlib.\n");
		return false;
	}

	ws->info.num_sdma_rings = MIN2(ws->info.num_sdma_rings, MAX_RINGS_PER_TYPE);
	ws->info.num_compute_rings = MIN2(ws->info.num_compute_rings, MAX_RINGS_PER_TYPE);

	ws->use_ib_bos = ws->info.chip_class >= CIK;
	return true;
}

static void radv_amdgpu_winsys_query_info(struct radeon_winsys *rws,
                                     struct radeon_info *info)
{
	*info = ((struct radv_amdgpu_winsys *)rws)->info;
}

static uint64_t radv_amdgpu_winsys_query_value(struct radeon_winsys *rws,
					       enum radeon_value_id value)
{
	struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys *)rws;
	struct amdgpu_heap_info heap;
	uint64_t retval = 0;

	switch (value) {
	case RADEON_TIMESTAMP:
		amdgpu_query_info(ws->dev, AMDGPU_INFO_TIMESTAMP, 8, &retval);
		return retval;
	case RADEON_NUM_BYTES_MOVED:
		amdgpu_query_info(ws->dev, AMDGPU_INFO_NUM_BYTES_MOVED,
				  8, &retval);
		return retval;
	case RADEON_NUM_EVICTIONS:
		amdgpu_query_info(ws->dev, AMDGPU_INFO_NUM_EVICTIONS,
				  8, &retval);
		return retval;
	case RADEON_NUM_VRAM_CPU_PAGE_FAULTS:
		amdgpu_query_info(ws->dev, AMDGPU_INFO_NUM_VRAM_CPU_PAGE_FAULTS,
				  8, &retval);
		return retval;
	case RADEON_VRAM_USAGE:
		amdgpu_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_VRAM,
				       0, &heap);
		return heap.heap_usage;
	case RADEON_VRAM_VIS_USAGE:
		amdgpu_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_VRAM,
				       AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED,
				       &heap);
		return heap.heap_usage;
	case RADEON_GTT_USAGE:
		amdgpu_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_GTT,
				       0, &heap);
		return heap.heap_usage;
	case RADEON_GPU_TEMPERATURE:
		amdgpu_query_sensor_info(ws->dev, AMDGPU_INFO_SENSOR_GPU_TEMP,
					 4, &retval);
		return retval;
	case RADEON_CURRENT_SCLK:
		amdgpu_query_sensor_info(ws->dev, AMDGPU_INFO_SENSOR_GFX_SCLK,
					 4, &retval);
		return retval;
	case RADEON_CURRENT_MCLK:
		amdgpu_query_sensor_info(ws->dev, AMDGPU_INFO_SENSOR_GFX_MCLK,
					 4, &retval);
		return retval;
	default:
		unreachable("invalid query value");
	}

	return 0;
}

static bool radv_amdgpu_winsys_read_registers(struct radeon_winsys *rws,
					      unsigned reg_offset,
					      unsigned num_registers, uint32_t *out)
{
	struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys*)rws;

	return amdgpu_read_mm_registers(ws->dev, reg_offset / 4, num_registers,
					0xffffffff, 0, out) == 0;
}

static const char *radv_amdgpu_winsys_get_chip_name(struct radeon_winsys *rws)
{
	amdgpu_device_handle dev = ((struct radv_amdgpu_winsys *)rws)->dev;

	return amdgpu_get_marketing_name(dev);
}

static void radv_amdgpu_winsys_destroy(struct radeon_winsys *rws)
{
	struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys*)rws;

	AddrDestroy(ws->addrlib);
	amdgpu_device_deinitialize(ws->dev);
	FREE(rws);
}

struct radeon_winsys *
radv_amdgpu_winsys_create(int fd, uint64_t debug_flags, uint64_t perftest_flags)
{
	uint32_t drm_major, drm_minor, r;
	amdgpu_device_handle dev;
	struct radv_amdgpu_winsys *ws;

	r = amdgpu_device_initialize(fd, &drm_major, &drm_minor, &dev);
	if (r)
		return NULL;

	ws = calloc(1, sizeof(struct radv_amdgpu_winsys));
	if (!ws)
		goto fail;

	ws->dev = dev;
	ws->info.drm_major = drm_major;
	ws->info.drm_minor = drm_minor;
	if (!do_winsys_init(ws, fd))
		goto winsys_fail;

	ws->debug_all_bos = !!(debug_flags & RADV_DEBUG_ALL_BOS);
	if (debug_flags & RADV_DEBUG_NO_IBS)
		ws->use_ib_bos = false;

	ws->use_local_bos = perftest_flags & RADV_PERFTEST_LOCAL_BOS;
	ws->zero_all_vram_allocs = debug_flags & RADV_DEBUG_ZERO_VRAM;
	ws->batchchain = !(perftest_flags & RADV_PERFTEST_NO_BATCHCHAIN);
	LIST_INITHEAD(&ws->global_bo_list);
	pthread_mutex_init(&ws->global_bo_list_lock, NULL);
	ws->base.query_info = radv_amdgpu_winsys_query_info;
	ws->base.query_value = radv_amdgpu_winsys_query_value;
	ws->base.read_registers = radv_amdgpu_winsys_read_registers;
	ws->base.get_chip_name = radv_amdgpu_winsys_get_chip_name;
	ws->base.destroy = radv_amdgpu_winsys_destroy;
	radv_amdgpu_bo_init_functions(ws);
	radv_amdgpu_cs_init_functions(ws);
	radv_amdgpu_surface_init_functions(ws);

	return &ws->base;

winsys_fail:
	free(ws);
fail:
	amdgpu_device_deinitialize(dev);
	return NULL;
}
