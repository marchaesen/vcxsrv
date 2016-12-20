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
#include "amdgpu_id.h"
#include "xf86drm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <amdgpu_drm.h>
#include <assert.h>
#include "radv_amdgpu_cs.h"
#include "radv_amdgpu_bo.h"
#include "radv_amdgpu_surface.h"

#define CIK_TILE_MODE_COLOR_2D			14

#define CIK__GB_TILE_MODE__PIPE_CONFIG(x)        (((x) >> 6) & 0x1f)
#define     CIK__PIPE_CONFIG__ADDR_SURF_P2               0
#define     CIK__PIPE_CONFIG__ADDR_SURF_P4_8x16          4
#define     CIK__PIPE_CONFIG__ADDR_SURF_P4_16x16         5
#define     CIK__PIPE_CONFIG__ADDR_SURF_P4_16x32         6
#define     CIK__PIPE_CONFIG__ADDR_SURF_P4_32x32         7
#define     CIK__PIPE_CONFIG__ADDR_SURF_P8_16x16_8x16    8
#define     CIK__PIPE_CONFIG__ADDR_SURF_P8_16x32_8x16    9
#define     CIK__PIPE_CONFIG__ADDR_SURF_P8_32x32_8x16    10
#define     CIK__PIPE_CONFIG__ADDR_SURF_P8_16x32_16x16   11
#define     CIK__PIPE_CONFIG__ADDR_SURF_P8_32x32_16x16   12
#define     CIK__PIPE_CONFIG__ADDR_SURF_P8_32x32_16x32   13
#define     CIK__PIPE_CONFIG__ADDR_SURF_P8_32x64_32x32   14
#define     CIK__PIPE_CONFIG__ADDR_SURF_P16_32X32_8X16   16
#define     CIK__PIPE_CONFIG__ADDR_SURF_P16_32X32_16X16  17

static unsigned radv_cik_get_num_tile_pipes(struct amdgpu_gpu_info *info)
{
	unsigned mode2d = info->gb_tile_mode[CIK_TILE_MODE_COLOR_2D];

	switch (CIK__GB_TILE_MODE__PIPE_CONFIG(mode2d)) {
	case CIK__PIPE_CONFIG__ADDR_SURF_P2:
		return 2;
	case CIK__PIPE_CONFIG__ADDR_SURF_P4_8x16:
	case CIK__PIPE_CONFIG__ADDR_SURF_P4_16x16:
	case CIK__PIPE_CONFIG__ADDR_SURF_P4_16x32:
	case CIK__PIPE_CONFIG__ADDR_SURF_P4_32x32:
		return 4;
	case CIK__PIPE_CONFIG__ADDR_SURF_P8_16x16_8x16:
	case CIK__PIPE_CONFIG__ADDR_SURF_P8_16x32_8x16:
	case CIK__PIPE_CONFIG__ADDR_SURF_P8_32x32_8x16:
	case CIK__PIPE_CONFIG__ADDR_SURF_P8_16x32_16x16:
	case CIK__PIPE_CONFIG__ADDR_SURF_P8_32x32_16x16:
	case CIK__PIPE_CONFIG__ADDR_SURF_P8_32x32_16x32:
	case CIK__PIPE_CONFIG__ADDR_SURF_P8_32x64_32x32:
		return 8;
	case CIK__PIPE_CONFIG__ADDR_SURF_P16_32X32_8X16:
	case CIK__PIPE_CONFIG__ADDR_SURF_P16_32X32_16X16:
		return 16;
	default:
		fprintf(stderr, "Invalid CIK pipe configuration, assuming P2\n");
		assert(!"this should never occur");
		return 2;
	}
}

static const char *
get_chip_name(enum radeon_family family)
{
	switch (family) {
	case CHIP_TAHITI: return "AMD RADV TAHITI";
	case CHIP_PITCAIRN: return "AMD RADV PITCAIRN";
	case CHIP_VERDE: return "AMD RADV CAPE VERDE";
	case CHIP_OLAND: return "AMD RADV OLAND";
	case CHIP_HAINAN: return "AMD RADV HAINAN";
	case CHIP_BONAIRE: return "AMD RADV BONAIRE";
	case CHIP_KAVERI: return "AMD RADV KAVERI";
	case CHIP_KABINI: return "AMD RADV KABINI";
	case CHIP_HAWAII: return "AMD RADV HAWAII";
	case CHIP_MULLINS: return "AMD RADV MULLINS";
	case CHIP_TONGA: return "AMD RADV TONGA";
	case CHIP_ICELAND: return "AMD RADV ICELAND";
	case CHIP_CARRIZO: return "AMD RADV CARRIZO";
	case CHIP_FIJI: return "AMD RADV FIJI";
	case CHIP_POLARIS10: return "AMD RADV POLARIS10";
	case CHIP_POLARIS11: return "AMD RADV POLARIS11";
	case CHIP_STONEY: return "AMD RADV STONEY";
	default: return "AMD RADV unknown";
	}
}


static bool
do_winsys_init(struct radv_amdgpu_winsys *ws, int fd)
{
	struct amdgpu_buffer_size_alignments alignment_info = {};
	struct amdgpu_heap_info vram, visible_vram, gtt;
	struct drm_amdgpu_info_hw_ip dma = {};
	struct drm_amdgpu_info_hw_ip compute = {};
	drmDevicePtr devinfo;
	int r;
	int i, j;
	/* Get PCI info. */
	r = drmGetDevice(fd, &devinfo);
	if (r) {
		fprintf(stderr, "amdgpu: drmGetDevice failed.\n");
		goto fail;
	}
	ws->info.pci_domain = devinfo->businfo.pci->domain;
	ws->info.pci_bus = devinfo->businfo.pci->bus;
	ws->info.pci_dev = devinfo->businfo.pci->dev;
	ws->info.pci_func = devinfo->businfo.pci->func;
	drmFreeDevice(&devinfo);

	/* Query hardware and driver information. */
	r = amdgpu_query_gpu_info(ws->dev, &ws->amdinfo);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_gpu_info failed.\n");
		goto fail;
	}

	r = amdgpu_query_buffer_size_alignment(ws->dev, &alignment_info);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_buffer_size_alignment failed.\n");
		goto fail;
	}

	r = amdgpu_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_VRAM, 0, &vram);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_heap_info(vram) failed.\n");
		goto fail;
	}

	r = amdgpu_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_VRAM,
	                           AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED, &visible_vram);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_heap_info(visible_vram) failed.\n");
		goto fail;
	}

	r = amdgpu_query_heap_info(ws->dev, AMDGPU_GEM_DOMAIN_GTT, 0, &gtt);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_heap_info(gtt) failed.\n");
		goto fail;
	}

	r = amdgpu_query_hw_ip_info(ws->dev, AMDGPU_HW_IP_DMA, 0, &dma);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(dma) failed.\n");
		goto fail;
	}

	r = amdgpu_query_hw_ip_info(ws->dev, AMDGPU_HW_IP_COMPUTE, 0, &compute);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(compute) failed.\n");
		goto fail;
	}
	ws->info.pci_id = ws->amdinfo.asic_id; /* TODO: is this correct? */
	ws->info.vce_harvest_config = ws->amdinfo.vce_harvest_config;

	switch (ws->info.pci_id) {
#define CHIPSET(pci_id, name, cfamily) case pci_id: ws->info.family = CHIP_##cfamily; break;
#include "pci_ids/radeonsi_pci_ids.h"
#undef CHIPSET
	default:
		fprintf(stderr, "amdgpu: Invalid PCI ID.\n");
		goto fail;
	}

	if (ws->info.family >= CHIP_TONGA)
		ws->info.chip_class = VI;
	else if (ws->info.family >= CHIP_BONAIRE)
		ws->info.chip_class = CIK;
	else if (ws->info.family >= CHIP_TAHITI)
		ws->info.chip_class = SI;
	else {
		fprintf(stderr, "amdgpu: Unknown family.\n");
		goto fail;
	}

	/* family and rev_id are for addrlib */
	switch (ws->info.family) {
	case CHIP_TAHITI:
		ws->family = FAMILY_SI;
		ws->rev_id = SI_TAHITI_P_A0;
		break;
	case CHIP_PITCAIRN:
		ws->family = FAMILY_SI;
		ws->rev_id = SI_PITCAIRN_PM_A0;
	  break;
	case CHIP_VERDE:
		ws->family = FAMILY_SI;
		ws->rev_id = SI_CAPEVERDE_M_A0;
		break;
	case CHIP_OLAND:
		ws->family = FAMILY_SI;
		ws->rev_id = SI_OLAND_M_A0;
		break;
	case CHIP_HAINAN:
		ws->family = FAMILY_SI;
		ws->rev_id = SI_HAINAN_V_A0;
		break;
	case CHIP_BONAIRE:
		ws->family = FAMILY_CI;
		ws->rev_id = CI_BONAIRE_M_A0;
		break;
	case CHIP_KAVERI:
		ws->family = FAMILY_KV;
		ws->rev_id = KV_SPECTRE_A0;
		break;
	case CHIP_KABINI:
		ws->family = FAMILY_KV;
		ws->rev_id = KB_KALINDI_A0;
		break;
	case CHIP_HAWAII:
		ws->family = FAMILY_CI;
		ws->rev_id = CI_HAWAII_P_A0;
		break;
	case CHIP_MULLINS:
		ws->family = FAMILY_KV;
		ws->rev_id = ML_GODAVARI_A0;
		break;
	case CHIP_TONGA:
		ws->family = FAMILY_VI;
		ws->rev_id = VI_TONGA_P_A0;
		break;
	case CHIP_ICELAND:
		ws->family = FAMILY_VI;
		ws->rev_id = VI_ICELAND_M_A0;
		break;
	case CHIP_CARRIZO:
		ws->family = FAMILY_CZ;
		ws->rev_id = CARRIZO_A0;
		break;
	case CHIP_STONEY:
		ws->family = FAMILY_CZ;
		ws->rev_id = STONEY_A0;
		break;
	case CHIP_FIJI:
		ws->family = FAMILY_VI;
		ws->rev_id = VI_FIJI_P_A0;
		break;
	case CHIP_POLARIS10:
		ws->family = FAMILY_VI;
		ws->rev_id = VI_POLARIS10_P_A0;
		break;
	case CHIP_POLARIS11:
		ws->family = FAMILY_VI;
		ws->rev_id = VI_POLARIS11_M_A0;
		break;
	default:
		fprintf(stderr, "amdgpu: Unknown family.\n");
		goto fail;
	}

	ws->addrlib = radv_amdgpu_addr_create(&ws->amdinfo, ws->family, ws->rev_id, ws->info.chip_class);
	if (!ws->addrlib) {
		fprintf(stderr, "amdgpu: Cannot create addrlib.\n");
		goto fail;
	}

	assert(util_is_power_of_two(dma.available_rings + 1));
	assert(util_is_power_of_two(compute.available_rings + 1));

	/* Set hardware information. */
	ws->info.name = get_chip_name(ws->info.family);
	ws->info.gart_size = gtt.heap_size;
	ws->info.vram_size = vram.heap_size;
	ws->info.visible_vram_size = visible_vram.heap_size;
	/* convert the shader clock from KHz to MHz */
	ws->info.max_shader_clock = ws->amdinfo.max_engine_clk / 1000;
	ws->info.max_se = ws->amdinfo.num_shader_engines;
	ws->info.max_sh_per_se = ws->amdinfo.num_shader_arrays_per_engine;
	ws->info.has_uvd = 0;
	ws->info.vce_fw_version = 0;
	ws->info.has_userptr = TRUE;
	ws->info.num_render_backends = ws->amdinfo.rb_pipes;
	ws->info.clock_crystal_freq = ws->amdinfo.gpu_counter_freq;
	ws->info.num_tile_pipes = radv_cik_get_num_tile_pipes(&ws->amdinfo);
	ws->info.pipe_interleave_bytes = 256 << ((ws->amdinfo.gb_addr_cfg >> 4) & 0x7);
	ws->info.has_virtual_memory = TRUE;
	ws->info.sdma_rings = MIN2(util_bitcount(dma.available_rings),
	                           MAX_RINGS_PER_TYPE);
	ws->info.compute_rings = MIN2(util_bitcount(compute.available_rings),
	                              MAX_RINGS_PER_TYPE);

	/* Get the number of good compute units. */
	ws->info.num_good_compute_units = 0;
	for (i = 0; i < ws->info.max_se; i++)
		for (j = 0; j < ws->info.max_sh_per_se; j++)
			ws->info.num_good_compute_units +=
				util_bitcount(ws->amdinfo.cu_bitmap[i][j]);

	memcpy(ws->info.si_tile_mode_array, ws->amdinfo.gb_tile_mode,
	       sizeof(ws->amdinfo.gb_tile_mode));
	ws->info.enabled_rb_mask = ws->amdinfo.enabled_rb_pipes_mask;

	memcpy(ws->info.cik_macrotile_mode_array, ws->amdinfo.gb_macro_tile_mode,
	       sizeof(ws->amdinfo.gb_macro_tile_mode));

	ws->info.gart_page_size = alignment_info.size_remote;

	if (ws->info.chip_class == SI)
		ws->info.gfx_ib_pad_with_type2 = TRUE;

	ws->use_ib_bos = ws->family >= FAMILY_CI;
	return true;
fail:
	return false;
}

static void radv_amdgpu_winsys_query_info(struct radeon_winsys *rws,
                                     struct radeon_info *info)
{
	*info = ((struct radv_amdgpu_winsys *)rws)->info;
}

static void radv_amdgpu_winsys_destroy(struct radeon_winsys *rws)
{
	struct radv_amdgpu_winsys *ws = (struct radv_amdgpu_winsys*)rws;

	AddrDestroy(ws->addrlib);
	amdgpu_device_deinitialize(ws->dev);
	FREE(rws);
}

struct radeon_winsys *
radv_amdgpu_winsys_create(int fd)
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

	ws->debug_all_bos = getenv("RADV_DEBUG_ALL_BOS") ? true : false;
	LIST_INITHEAD(&ws->global_bo_list);
	pthread_mutex_init(&ws->global_bo_list_lock, NULL);
	ws->base.query_info = radv_amdgpu_winsys_query_info;
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
