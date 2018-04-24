/*
 * Copyright © 2017 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NON-INFRINGEMENT. IN NO EVENT SHALL THE COPYRIGHT HOLDERS, AUTHORS
 * AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 */

#include "ac_gpu_info.h"
#include "sid.h"
#include "gfx9d.h"

#include "util/u_math.h"

#include <stdio.h>

#include <xf86drm.h>
#include <amdgpu_drm.h>

#include <amdgpu.h>

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

static unsigned cik_get_num_tile_pipes(struct amdgpu_gpu_info *info)
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

static bool has_syncobj(int fd)
{
	uint64_t value;
	if (drmGetCap(fd, DRM_CAP_SYNCOBJ, &value))
		return false;
	return value ? true : false;
}

bool ac_query_gpu_info(int fd, amdgpu_device_handle dev,
		       struct radeon_info *info,
		       struct amdgpu_gpu_info *amdinfo)
{
	struct amdgpu_buffer_size_alignments alignment_info = {};
	struct amdgpu_heap_info vram, vram_vis, gtt;
	struct drm_amdgpu_info_hw_ip dma = {}, compute = {}, uvd = {};
	struct drm_amdgpu_info_hw_ip uvd_enc = {}, vce = {}, vcn_dec = {};
	struct drm_amdgpu_info_hw_ip vcn_enc = {}, gfx = {};
	struct amdgpu_gds_resource_info gds = {};
	uint32_t vce_version = 0, vce_feature = 0, uvd_version = 0, uvd_feature = 0;
	int r, i, j;
	drmDevicePtr devinfo;

	/* Get PCI info. */
	r = drmGetDevice2(fd, 0, &devinfo);
	if (r) {
		fprintf(stderr, "amdgpu: drmGetDevice2 failed.\n");
		return false;
	}
	info->pci_domain = devinfo->businfo.pci->domain;
	info->pci_bus = devinfo->businfo.pci->bus;
	info->pci_dev = devinfo->businfo.pci->dev;
	info->pci_func = devinfo->businfo.pci->func;
	drmFreeDevice(&devinfo);

	/* Query hardware and driver information. */
	r = amdgpu_query_gpu_info(dev, amdinfo);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_gpu_info failed.\n");
		return false;
	}

	r = amdgpu_query_buffer_size_alignment(dev, &alignment_info);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_buffer_size_alignment failed.\n");
		return false;
	}

	r = amdgpu_query_heap_info(dev, AMDGPU_GEM_DOMAIN_VRAM, 0, &vram);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_heap_info(vram) failed.\n");
		return false;
	}

	r = amdgpu_query_heap_info(dev, AMDGPU_GEM_DOMAIN_VRAM,
				AMDGPU_GEM_CREATE_CPU_ACCESS_REQUIRED,
				&vram_vis);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_heap_info(vram_vis) failed.\n");
		return false;
	}

	r = amdgpu_query_heap_info(dev, AMDGPU_GEM_DOMAIN_GTT, 0, &gtt);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_heap_info(gtt) failed.\n");
		return false;
	}

	r = amdgpu_query_hw_ip_info(dev, AMDGPU_HW_IP_DMA, 0, &dma);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(dma) failed.\n");
		return false;
	}

	r = amdgpu_query_hw_ip_info(dev, AMDGPU_HW_IP_GFX, 0, &gfx);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(gfx) failed.\n");
		return false;
	}

	r = amdgpu_query_hw_ip_info(dev, AMDGPU_HW_IP_COMPUTE, 0, &compute);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(compute) failed.\n");
		return false;
	}

	r = amdgpu_query_hw_ip_info(dev, AMDGPU_HW_IP_UVD, 0, &uvd);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(uvd) failed.\n");
		return false;
	}

	if (info->drm_major == 3 && info->drm_minor >= 17) {
		r = amdgpu_query_hw_ip_info(dev, AMDGPU_HW_IP_UVD_ENC, 0, &uvd_enc);
		if (r) {
			fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(uvd_enc) failed.\n");
			return false;
		}
	}

	if (info->drm_major == 3 && info->drm_minor >= 17) {
		r = amdgpu_query_hw_ip_info(dev, AMDGPU_HW_IP_VCN_DEC, 0, &vcn_dec);
		if (r) {
			fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(vcn_dec) failed.\n");
			return false;
		}
	}

	if (info->drm_major == 3 && info->drm_minor >= 17) {
		r = amdgpu_query_hw_ip_info(dev, AMDGPU_HW_IP_VCN_ENC, 0, &vcn_enc);
		if (r) {
			fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(vcn_enc) failed.\n");
			return false;
		}
	}

	r = amdgpu_query_firmware_version(dev, AMDGPU_INFO_FW_GFX_ME, 0, 0,
					&info->me_fw_version,
					&info->me_fw_feature);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_firmware_version(me) failed.\n");
		return false;
	}

	r = amdgpu_query_firmware_version(dev, AMDGPU_INFO_FW_GFX_PFP, 0, 0,
					&info->pfp_fw_version,
					&info->pfp_fw_feature);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_firmware_version(pfp) failed.\n");
		return false;
	}

	r = amdgpu_query_firmware_version(dev, AMDGPU_INFO_FW_GFX_CE, 0, 0,
					&info->ce_fw_version,
					&info->ce_fw_feature);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_firmware_version(ce) failed.\n");
		return false;
	}

	r = amdgpu_query_firmware_version(dev, AMDGPU_INFO_FW_UVD, 0, 0,
					&uvd_version, &uvd_feature);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_firmware_version(uvd) failed.\n");
		return false;
	}

	r = amdgpu_query_hw_ip_info(dev, AMDGPU_HW_IP_VCE, 0, &vce);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_hw_ip_info(vce) failed.\n");
		return false;
	}

	r = amdgpu_query_firmware_version(dev, AMDGPU_INFO_FW_VCE, 0, 0,
					&vce_version, &vce_feature);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_firmware_version(vce) failed.\n");
		return false;
	}

	r = amdgpu_query_sw_info(dev, amdgpu_sw_info_address32_hi, &info->address32_hi);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_sw_info(address32_hi) failed.\n");
		return false;
	}

	r = amdgpu_query_gds_info(dev, &gds);
	if (r) {
		fprintf(stderr, "amdgpu: amdgpu_query_gds_info failed.\n");
		return false;
	}

	/* Set chip identification. */
	info->pci_id = amdinfo->asic_id; /* TODO: is this correct? */
	info->vce_harvest_config = amdinfo->vce_harvest_config;

	switch (info->pci_id) {
#define CHIPSET(pci_id, cfamily) case pci_id: info->family = CHIP_##cfamily; break;
#include "pci_ids/radeonsi_pci_ids.h"
#undef CHIPSET

	default:
		fprintf(stderr, "amdgpu: Invalid PCI ID.\n");
		return false;
	}

	if (info->family >= CHIP_VEGA10)
		info->chip_class = GFX9;
	else if (info->family >= CHIP_TONGA)
		info->chip_class = VI;
	else if (info->family >= CHIP_BONAIRE)
		info->chip_class = CIK;
	else if (info->family >= CHIP_TAHITI)
		info->chip_class = SI;
	else {
		fprintf(stderr, "amdgpu: Unknown family.\n");
		return false;
	}

	/* Set which chips have dedicated VRAM. */
	info->has_dedicated_vram =
		!(amdinfo->ids_flags & AMDGPU_IDS_FLAGS_FUSION);

	/* Set hardware information. */
	info->gart_size = gtt.heap_size;
	info->vram_size = vram.heap_size;
	info->vram_vis_size = vram_vis.heap_size;
	info->gds_size = gds.gds_total_size;
	info->gds_gfx_partition_size = gds.gds_gfx_partition_size;
	/* The kernel can split large buffers in VRAM but not in GTT, so large
	 * allocations can fail or cause buffer movement failures in the kernel.
	 */
	info->max_alloc_size = MIN2(info->vram_size * 0.9, info->gart_size * 0.7);
	/* convert the shader clock from KHz to MHz */
	info->max_shader_clock = amdinfo->max_engine_clk / 1000;
	info->max_se = amdinfo->num_shader_engines;
	info->max_sh_per_se = amdinfo->num_shader_arrays_per_engine;
	info->has_hw_decode =
		(uvd.available_rings != 0) || (vcn_dec.available_rings != 0);
	info->uvd_fw_version =
		uvd.available_rings ? uvd_version : 0;
	info->vce_fw_version =
		vce.available_rings ? vce_version : 0;
	info->uvd_enc_supported =
		uvd_enc.available_rings ? true : false;
	info->has_userptr = true;
	info->has_syncobj = has_syncobj(fd);
	info->has_syncobj_wait_for_submit = info->has_syncobj && info->drm_minor >= 20;
	info->has_fence_to_handle = info->has_syncobj && info->drm_minor >= 21;
	info->has_ctx_priority = info->drm_minor >= 22;
	/* TODO: Enable this once the kernel handles it efficiently. */
	info->has_local_buffers = info->drm_minor >= 20 &&
				  !info->has_dedicated_vram;
	info->num_render_backends = amdinfo->rb_pipes;
	info->clock_crystal_freq = amdinfo->gpu_counter_freq;
	if (!info->clock_crystal_freq) {
		fprintf(stderr, "amdgpu: clock crystal frequency is 0, timestamps will be wrong\n");
		info->clock_crystal_freq = 1;
	}
	info->tcc_cache_line_size = 64; /* TC L2 line size on GCN */
	info->gb_addr_config = amdinfo->gb_addr_cfg;
	if (info->chip_class == GFX9) {
		info->num_tile_pipes = 1 << G_0098F8_NUM_PIPES(amdinfo->gb_addr_cfg);
		info->pipe_interleave_bytes =
			256 << G_0098F8_PIPE_INTERLEAVE_SIZE_GFX9(amdinfo->gb_addr_cfg);
	} else {
		info->num_tile_pipes = cik_get_num_tile_pipes(amdinfo);
		info->pipe_interleave_bytes =
			256 << G_0098F8_PIPE_INTERLEAVE_SIZE_GFX6(amdinfo->gb_addr_cfg);
	}
	info->r600_has_virtual_memory = true;

	assert(util_is_power_of_two_or_zero(dma.available_rings + 1));
	assert(util_is_power_of_two_or_zero(compute.available_rings + 1));

	info->num_sdma_rings = util_bitcount(dma.available_rings);
	info->num_compute_rings = util_bitcount(compute.available_rings);

	/* Get the number of good compute units. */
	info->num_good_compute_units = 0;
	for (i = 0; i < info->max_se; i++)
		for (j = 0; j < info->max_sh_per_se; j++)
			info->num_good_compute_units +=
				util_bitcount(amdinfo->cu_bitmap[i][j]);

	memcpy(info->si_tile_mode_array, amdinfo->gb_tile_mode,
		sizeof(amdinfo->gb_tile_mode));
	info->enabled_rb_mask = amdinfo->enabled_rb_pipes_mask;

	memcpy(info->cik_macrotile_mode_array, amdinfo->gb_macro_tile_mode,
		sizeof(amdinfo->gb_macro_tile_mode));

	info->pte_fragment_size = alignment_info.size_local;
	info->gart_page_size = alignment_info.size_remote;

	if (info->chip_class == SI)
		info->gfx_ib_pad_with_type2 = TRUE;

	unsigned ib_align = 0;
	ib_align = MAX2(ib_align, gfx.ib_start_alignment);
	ib_align = MAX2(ib_align, compute.ib_start_alignment);
	ib_align = MAX2(ib_align, dma.ib_start_alignment);
	ib_align = MAX2(ib_align, uvd.ib_start_alignment);
	ib_align = MAX2(ib_align, uvd_enc.ib_start_alignment);
	ib_align = MAX2(ib_align, vce.ib_start_alignment);
	ib_align = MAX2(ib_align, vcn_dec.ib_start_alignment);
	ib_align = MAX2(ib_align, vcn_enc.ib_start_alignment);
       assert(ib_align);
	info->ib_start_alignment = ib_align;

	return true;
}

void ac_compute_driver_uuid(char *uuid, size_t size)
{
	char amd_uuid[] = "AMD-MESA-DRV";

	assert(size >= sizeof(amd_uuid));

	memset(uuid, 0, size);
	strncpy(uuid, amd_uuid, size);
}

void ac_compute_device_uuid(struct radeon_info *info, char *uuid, size_t size)
{
	uint32_t *uint_uuid = (uint32_t*)uuid;

	assert(size >= sizeof(uint32_t)*4);

	/**
	 * Use the device info directly instead of using a sha1. GL/VK UUIDs
	 * are 16 byte vs 20 byte for sha1, and the truncation that would be
	 * required would get rid of part of the little entropy we have.
	 * */
	memset(uuid, 0, size);
	uint_uuid[0] = info->pci_domain;
	uint_uuid[1] = info->pci_bus;
	uint_uuid[2] = info->pci_dev;
	uint_uuid[3] = info->pci_func;
}

void ac_print_gpu_info(struct radeon_info *info)
{
	printf("Device info:\n");
	printf("    pci (domain:bus:dev.func): %04x:%02x:%02x.%x\n",
	       info->pci_domain, info->pci_bus,
	       info->pci_dev, info->pci_func);
	printf("    pci_id = 0x%x\n", info->pci_id);
	printf("    family = %i\n", info->family);
	printf("    chip_class = %i\n", info->chip_class);
	printf("    num_compute_rings = %u\n", info->num_compute_rings);
	printf("    num_sdma_rings = %i\n", info->num_sdma_rings);
	printf("    clock_crystal_freq = %i\n", info->clock_crystal_freq);
	printf("    tcc_cache_line_size = %u\n", info->tcc_cache_line_size);

	printf("Memory info:\n");
	printf("    pte_fragment_size = %u\n", info->pte_fragment_size);
	printf("    gart_page_size = %u\n", info->gart_page_size);
	printf("    gart_size = %i MB\n", (int)DIV_ROUND_UP(info->gart_size, 1024*1024));
	printf("    vram_size = %i MB\n", (int)DIV_ROUND_UP(info->vram_size, 1024*1024));
	printf("    vram_vis_size = %i MB\n", (int)DIV_ROUND_UP(info->vram_vis_size, 1024*1024));
	printf("    gds_size = %u kB\n", info->gds_size / 1024);
	printf("    gds_gfx_partition_size = %u kB\n", info->gds_gfx_partition_size / 1024);
	printf("    max_alloc_size = %i MB\n",
	       (int)DIV_ROUND_UP(info->max_alloc_size, 1024*1024));
	printf("    min_alloc_size = %u\n", info->min_alloc_size);
	printf("    address32_hi = %u\n", info->address32_hi);
	printf("    has_dedicated_vram = %u\n", info->has_dedicated_vram);

	printf("CP info:\n");
	printf("    gfx_ib_pad_with_type2 = %i\n", info->gfx_ib_pad_with_type2);
	printf("    ib_start_alignment = %u\n", info->ib_start_alignment);
	printf("    me_fw_version = %i\n", info->me_fw_version);
	printf("    me_fw_feature = %i\n", info->me_fw_feature);
	printf("    pfp_fw_version = %i\n", info->pfp_fw_version);
	printf("    pfp_fw_feature = %i\n", info->pfp_fw_feature);
	printf("    ce_fw_version = %i\n", info->ce_fw_version);
	printf("    ce_fw_feature = %i\n", info->ce_fw_feature);

	printf("Multimedia info:\n");
	printf("    has_hw_decode = %u\n", info->has_hw_decode);
	printf("    uvd_enc_supported = %u\n", info->uvd_enc_supported);
	printf("    uvd_fw_version = %u\n", info->uvd_fw_version);
	printf("    vce_fw_version = %u\n", info->vce_fw_version);
	printf("    vce_harvest_config = %i\n", info->vce_harvest_config);

	printf("Kernel info:\n");
	printf("    drm = %i.%i.%i\n", info->drm_major,
	       info->drm_minor, info->drm_patchlevel);
	printf("    has_userptr = %i\n", info->has_userptr);
	printf("    has_syncobj = %u\n", info->has_syncobj);
	printf("    has_syncobj_wait_for_submit = %u\n", info->has_syncobj_wait_for_submit);
	printf("    has_fence_to_handle = %u\n", info->has_fence_to_handle);
	printf("    has_ctx_priority = %u\n", info->has_ctx_priority);
	printf("    has_local_buffers = %u\n", info->has_local_buffers);

	printf("Shader core info:\n");
	printf("    max_shader_clock = %i\n", info->max_shader_clock);
	printf("    num_good_compute_units = %i\n", info->num_good_compute_units);
	printf("    max_se = %i\n", info->max_se);
	printf("    max_sh_per_se = %i\n", info->max_sh_per_se);

	printf("Render backend info:\n");
	printf("    num_render_backends = %i\n", info->num_render_backends);
	printf("    num_tile_pipes = %i\n", info->num_tile_pipes);
	printf("    pipe_interleave_bytes = %i\n", info->pipe_interleave_bytes);
	printf("    enabled_rb_mask = 0x%x\n", info->enabled_rb_mask);
	printf("    max_alignment = %u\n", (unsigned)info->max_alignment);

	printf("GB_ADDR_CONFIG:\n");
	if (info->chip_class >= GFX9) {
		printf("    num_pipes = %u\n",
		       1 << G_0098F8_NUM_PIPES(info->gb_addr_config));
		printf("    pipe_interleave_size = %u\n",
		       256 << G_0098F8_PIPE_INTERLEAVE_SIZE_GFX9(info->gb_addr_config));
		printf("    max_compressed_frags = %u\n",
		       1 << G_0098F8_MAX_COMPRESSED_FRAGS(info->gb_addr_config));
		printf("    bank_interleave_size = %u\n",
		       1 << G_0098F8_BANK_INTERLEAVE_SIZE(info->gb_addr_config));
		printf("    num_banks = %u\n",
		       1 << G_0098F8_NUM_BANKS(info->gb_addr_config));
		printf("    shader_engine_tile_size = %u\n",
		       16 << G_0098F8_SHADER_ENGINE_TILE_SIZE(info->gb_addr_config));
		printf("    num_shader_engines = %u\n",
		       1 << G_0098F8_NUM_SHADER_ENGINES_GFX9(info->gb_addr_config));
		printf("    num_gpus = %u (raw)\n",
		       G_0098F8_NUM_GPUS_GFX9(info->gb_addr_config));
		printf("    multi_gpu_tile_size = %u (raw)\n",
		       G_0098F8_MULTI_GPU_TILE_SIZE(info->gb_addr_config));
		printf("    num_rb_per_se = %u\n",
		       1 << G_0098F8_NUM_RB_PER_SE(info->gb_addr_config));
		printf("    row_size = %u\n",
		       1024 << G_0098F8_ROW_SIZE(info->gb_addr_config));
		printf("    num_lower_pipes = %u (raw)\n",
		       G_0098F8_NUM_LOWER_PIPES(info->gb_addr_config));
		printf("    se_enable = %u (raw)\n",
		       G_0098F8_SE_ENABLE(info->gb_addr_config));
	} else {
		printf("    num_pipes = %u\n",
		       1 << G_0098F8_NUM_PIPES(info->gb_addr_config));
		printf("    pipe_interleave_size = %u\n",
		       256 << G_0098F8_PIPE_INTERLEAVE_SIZE_GFX6(info->gb_addr_config));
		printf("    bank_interleave_size = %u\n",
		       1 << G_0098F8_BANK_INTERLEAVE_SIZE(info->gb_addr_config));
		printf("    num_shader_engines = %u\n",
		       1 << G_0098F8_NUM_SHADER_ENGINES_GFX6(info->gb_addr_config));
		printf("    shader_engine_tile_size = %u\n",
		       16 << G_0098F8_SHADER_ENGINE_TILE_SIZE(info->gb_addr_config));
		printf("    num_gpus = %u (raw)\n",
		       G_0098F8_NUM_GPUS_GFX6(info->gb_addr_config));
		printf("    multi_gpu_tile_size = %u (raw)\n",
		       G_0098F8_MULTI_GPU_TILE_SIZE(info->gb_addr_config));
		printf("    row_size = %u\n",
		       1024 << G_0098F8_ROW_SIZE(info->gb_addr_config));
		printf("    num_lower_pipes = %u (raw)\n",
		       G_0098F8_NUM_LOWER_PIPES(info->gb_addr_config));
	}
}

int
ac_get_gs_table_depth(enum chip_class chip_class, enum radeon_family family)
{
	if (chip_class >= GFX9)
		return -1;

	switch (family) {
	case CHIP_OLAND:
	case CHIP_HAINAN:
	case CHIP_KAVERI:
	case CHIP_KABINI:
	case CHIP_MULLINS:
	case CHIP_ICELAND:
	case CHIP_CARRIZO:
	case CHIP_STONEY:
		return 16;
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
	case CHIP_VERDE:
	case CHIP_BONAIRE:
	case CHIP_HAWAII:
	case CHIP_TONGA:
	case CHIP_FIJI:
	case CHIP_POLARIS10:
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
	case CHIP_VEGAM:
		return 32;
	default:
		unreachable("Unknown GPU");
	}
}

void
ac_get_raster_config(struct radeon_info *info,
		     uint32_t *raster_config_p,
		     uint32_t *raster_config_1_p)
{
	unsigned num_rb = MIN2(info->num_render_backends, 16);
	unsigned raster_config, raster_config_1;
	switch (info->family) {
	case CHIP_TAHITI:
	case CHIP_PITCAIRN:
		raster_config = 0x2a00126a;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_VERDE:
		raster_config = 0x0000124a;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_OLAND:
		raster_config = 0x00000082;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_HAINAN:
		raster_config = 0x00000000;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_BONAIRE:
		raster_config = 0x16000012;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_HAWAII:
		raster_config = 0x3a00161a;
		raster_config_1 = 0x0000002e;
		break;
	case CHIP_FIJI:
		if (info->cik_macrotile_mode_array[0] == 0x000000e8) {
			/* old kernels with old tiling config */
			raster_config = 0x16000012;
			raster_config_1 = 0x0000002a;
		} else {
			raster_config = 0x3a00161a;
			raster_config_1 = 0x0000002e;
		}
		break;
	case CHIP_POLARIS10:
		raster_config = 0x16000012;
		raster_config_1 = 0x0000002a;
		break;
	case CHIP_POLARIS11:
	case CHIP_POLARIS12:
		raster_config = 0x16000012;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_VEGAM:
		raster_config = 0x3a00161a;
		raster_config_1 = 0x0000002e;
		break;
	case CHIP_TONGA:
		raster_config = 0x16000012;
		raster_config_1 = 0x0000002a;
		break;
	case CHIP_ICELAND:
		if (num_rb == 1)
			raster_config = 0x00000000;
		else
			raster_config = 0x00000002;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_CARRIZO:
		raster_config = 0x00000002;
		raster_config_1 = 0x00000000;
		break;
	case CHIP_KAVERI:
		/* KV should be 0x00000002, but that causes problems with radeon */
		raster_config = 0x00000000; /* 0x00000002 */
		raster_config_1 = 0x00000000;
		break;
	case CHIP_KABINI:
	case CHIP_MULLINS:
	case CHIP_STONEY:
		raster_config = 0x00000000;
		raster_config_1 = 0x00000000;
		break;
	default:
		fprintf(stderr,
			"ac: Unknown GPU, using 0 for raster_config\n");
		raster_config = 0x00000000;
		raster_config_1 = 0x00000000;
		break;
	}
	*raster_config_p = raster_config;
	*raster_config_1_p = raster_config_1;
}

void
ac_get_harvested_configs(struct radeon_info *info,
			 unsigned raster_config,
			 unsigned *cik_raster_config_1_p,
			 unsigned *raster_config_se)
{
	unsigned sh_per_se = MAX2(info->max_sh_per_se, 1);
	unsigned num_se = MAX2(info->max_se, 1);
	unsigned rb_mask = info->enabled_rb_mask;
	unsigned num_rb = MIN2(info->num_render_backends, 16);
	unsigned rb_per_pkr = MIN2(num_rb / num_se / sh_per_se, 2);
	unsigned rb_per_se = num_rb / num_se;
	unsigned se_mask[4];
	unsigned se;

	se_mask[0] = ((1 << rb_per_se) - 1) & rb_mask;
	se_mask[1] = (se_mask[0] << rb_per_se) & rb_mask;
	se_mask[2] = (se_mask[1] << rb_per_se) & rb_mask;
	se_mask[3] = (se_mask[2] << rb_per_se) & rb_mask;

	assert(num_se == 1 || num_se == 2 || num_se == 4);
	assert(sh_per_se == 1 || sh_per_se == 2);
	assert(rb_per_pkr == 1 || rb_per_pkr == 2);


	if (info->chip_class >= CIK) {
		unsigned raster_config_1 = *cik_raster_config_1_p;
		if ((num_se > 2) && ((!se_mask[0] && !se_mask[1]) ||
				     (!se_mask[2] && !se_mask[3]))) {
			raster_config_1 &= C_028354_SE_PAIR_MAP;

			if (!se_mask[0] && !se_mask[1]) {
				raster_config_1 |=
					S_028354_SE_PAIR_MAP(V_028354_RASTER_CONFIG_SE_PAIR_MAP_3);
			} else {
				raster_config_1 |=
					S_028354_SE_PAIR_MAP(V_028354_RASTER_CONFIG_SE_PAIR_MAP_0);
			}
			*cik_raster_config_1_p = raster_config_1;
		}
	}

	for (se = 0; se < num_se; se++) {
		unsigned pkr0_mask = ((1 << rb_per_pkr) - 1) << (se * rb_per_se);
		unsigned pkr1_mask = pkr0_mask << rb_per_pkr;
		int idx = (se / 2) * 2;

		raster_config_se[se] = raster_config;
		if ((num_se > 1) && (!se_mask[idx] || !se_mask[idx + 1])) {
			raster_config_se[se] &= C_028350_SE_MAP;

			if (!se_mask[idx]) {
				raster_config_se[se] |=
					S_028350_SE_MAP(V_028350_RASTER_CONFIG_SE_MAP_3);
			} else {
				raster_config_se[se] |=
					S_028350_SE_MAP(V_028350_RASTER_CONFIG_SE_MAP_0);
			}
		}

		pkr0_mask &= rb_mask;
		pkr1_mask &= rb_mask;
		if (rb_per_se > 2 && (!pkr0_mask || !pkr1_mask)) {
			raster_config_se[se] &= C_028350_PKR_MAP;

			if (!pkr0_mask) {
				raster_config_se[se] |=
					S_028350_PKR_MAP(V_028350_RASTER_CONFIG_PKR_MAP_3);
			} else {
				raster_config_se[se] |=
					S_028350_PKR_MAP(V_028350_RASTER_CONFIG_PKR_MAP_0);
			}
		}

		if (rb_per_se >= 2) {
			unsigned rb0_mask = 1 << (se * rb_per_se);
			unsigned rb1_mask = rb0_mask << 1;

			rb0_mask &= rb_mask;
			rb1_mask &= rb_mask;
			if (!rb0_mask || !rb1_mask) {
				raster_config_se[se] &= C_028350_RB_MAP_PKR0;

				if (!rb0_mask) {
					raster_config_se[se] |=
						S_028350_RB_MAP_PKR0(V_028350_RASTER_CONFIG_RB_MAP_3);
				} else {
					raster_config_se[se] |=
						S_028350_RB_MAP_PKR0(V_028350_RASTER_CONFIG_RB_MAP_0);
				}
			}

			if (rb_per_se > 2) {
				rb0_mask = 1 << (se * rb_per_se + rb_per_pkr);
				rb1_mask = rb0_mask << 1;
				rb0_mask &= rb_mask;
				rb1_mask &= rb_mask;
				if (!rb0_mask || !rb1_mask) {
					raster_config_se[se] &= C_028350_RB_MAP_PKR1;

					if (!rb0_mask) {
						raster_config_se[se] |=
							S_028350_RB_MAP_PKR1(V_028350_RASTER_CONFIG_RB_MAP_3);
					} else {
						raster_config_se[se] |=
							S_028350_RB_MAP_PKR1(V_028350_RASTER_CONFIG_RB_MAP_0);
					}
				}
			}
		}
	}
}
