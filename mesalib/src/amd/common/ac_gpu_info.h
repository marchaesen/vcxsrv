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

#ifndef AC_GPU_INFO_H
#define AC_GPU_INFO_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "amd_family.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Prior to C11 the following may trigger a typedef redeclaration warning */
typedef struct amdgpu_device *amdgpu_device_handle;
struct amdgpu_gpu_info;

struct radeon_info {
	/* PCI info: domain:bus:dev:func */
	uint32_t                    pci_domain;
	uint32_t                    pci_bus;
	uint32_t                    pci_dev;
	uint32_t                    pci_func;

	/* Device info. */
	uint32_t                    pci_id;
	enum radeon_family          family;
	enum chip_class             chip_class;
	uint32_t                    num_compute_rings;
	uint32_t                    num_sdma_rings;
	uint32_t                    clock_crystal_freq;
	uint32_t                    tcc_cache_line_size;

	/* Memory info. */
	uint32_t                    pte_fragment_size;
	uint32_t                    gart_page_size;
	uint64_t                    gart_size;
	uint64_t                    vram_size;
	uint64_t                    vram_vis_size;
	unsigned                    gds_size;
	unsigned                    gds_gfx_partition_size;
	uint64_t                    max_alloc_size;
	uint32_t                    min_alloc_size;
	uint32_t                    address32_hi;
	bool                        has_dedicated_vram;
	bool                        r600_has_virtual_memory;

	/* CP info. */
	bool                        gfx_ib_pad_with_type2;
	unsigned                    ib_start_alignment;
	uint32_t                    me_fw_version;
	uint32_t                    me_fw_feature;
	uint32_t                    pfp_fw_version;
	uint32_t                    pfp_fw_feature;
	uint32_t                    ce_fw_version;
	uint32_t                    ce_fw_feature;

	/* Multimedia info. */
	bool                        has_hw_decode;
	bool                        uvd_enc_supported;
	uint32_t                    uvd_fw_version;
	uint32_t                    vce_fw_version;
	uint32_t                    vce_harvest_config;

	/* Kernel info. */
	uint32_t                    drm_major; /* version */
	uint32_t                    drm_minor;
	uint32_t                    drm_patchlevel;
	bool                        has_userptr;
	bool                        has_syncobj;
	bool                        has_syncobj_wait_for_submit;
	bool                        has_fence_to_handle;
	bool                        has_ctx_priority;
	bool                        has_local_buffers;

	/* Shader cores. */
	uint32_t                    r600_max_quad_pipes; /* wave size / 16 */
	uint32_t                    max_shader_clock;
	uint32_t                    num_good_compute_units;
	uint32_t                    max_se; /* shader engines */
	uint32_t                    max_sh_per_se; /* shader arrays per shader engine */

	/* Render backends (color + depth blocks). */
	uint32_t                    r300_num_gb_pipes;
	uint32_t                    r300_num_z_pipes;
	uint32_t                    r600_gb_backend_map; /* R600 harvest config */
	bool                        r600_gb_backend_map_valid;
	uint32_t                    r600_num_banks;
	uint32_t                    gb_addr_config;
	uint32_t                    num_render_backends;
	uint32_t                    num_tile_pipes; /* pipe count from PIPE_CONFIG */
	uint32_t                    pipe_interleave_bytes;
	uint32_t                    enabled_rb_mask; /* GCN harvest config */
	uint64_t                    max_alignment; /* from addrlib */

	/* Tile modes. */
	uint32_t                    si_tile_mode_array[32];
	uint32_t                    cik_macrotile_mode_array[16];
};

bool ac_query_gpu_info(int fd, amdgpu_device_handle dev,
		       struct radeon_info *info,
		       struct amdgpu_gpu_info *amdinfo);

void ac_compute_driver_uuid(char *uuid, size_t size);

void ac_compute_device_uuid(struct radeon_info *info, char *uuid, size_t size);
void ac_print_gpu_info(struct radeon_info *info);

#ifdef __cplusplus
}
#endif

#endif /* AC_GPU_INFO_H */
