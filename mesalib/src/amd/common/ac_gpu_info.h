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

#include "amd_family.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "util/macros.h"

#ifdef __cplusplus
extern "C" {
#endif

#define AMD_MAX_SE         8
#define AMD_MAX_SA_PER_SE  2

struct amdgpu_gpu_info;

struct amd_ip_info {
   uint8_t ver_major;
   uint8_t ver_minor;
   uint8_t num_queues;
};

struct radeon_info {
   /* Device info. */
   const char *name;
   char lowercase_name[32];
   const char *marketing_name;
   uint32_t num_se;           /* only enabled SEs */
   uint32_t num_rb;           /* only enabled RBs */
   uint32_t num_cu;           /* only enabled CUs */
   uint32_t max_gpu_freq_mhz; /* also known as the shader clock */
   uint32_t max_gflops;
   uint32_t l1_cache_size;
   uint32_t l2_cache_size;
   uint32_t l3_cache_size_mb;
   uint32_t num_tcc_blocks; /* also the number of memory channels */
   uint32_t memory_freq_mhz;
   uint32_t memory_freq_mhz_effective;
   uint32_t memory_bus_width;
   uint32_t memory_bandwidth_gbps;
   uint32_t clock_crystal_freq;
   struct amd_ip_info ip[AMD_NUM_IP_TYPES];

   /* Identification. */
   /* PCI info: domain:bus:dev:func */
   uint32_t pci_domain;
   uint32_t pci_bus;
   uint32_t pci_dev;
   uint32_t pci_func;

   uint32_t pci_id;
   uint32_t pci_rev_id;
   enum radeon_family family;
   enum amd_gfx_level gfx_level;
   uint32_t family_id;
   uint32_t chip_external_rev;
   uint32_t chip_rev; /* 0 = A0, 1 = A1, etc. */

   /* Flags. */
   bool is_pro_graphics;
   bool has_graphics; /* false if the chip is compute-only */
   uint32_t ib_pad_dw_mask[AMD_NUM_IP_TYPES];
   bool has_clear_state;
   bool has_distributed_tess;
   bool has_dcc_constant_encode;
   bool has_rbplus;     /* if RB+ registers exist */
   bool rbplus_allowed; /* if RB+ is allowed */
   bool has_load_ctx_reg_pkt;
   bool has_out_of_order_rast;
   bool has_packed_math_16bit;
   bool has_accelerated_dot_product;
   bool cpdma_prefetch_writes_memory;
   bool has_gfx9_scissor_bug;
   bool has_tc_compat_zrange_bug;
   bool has_msaa_sample_loc_bug;
   bool has_ls_vgpr_init_bug;
   bool has_zero_index_buffer_bug;
   bool has_image_load_dcc_bug;
   bool has_two_planes_iterate256_bug;
   bool has_vgt_flush_ngg_legacy_bug;
   bool has_cs_regalloc_hang_bug;
   bool has_32bit_predication;
   bool has_3d_cube_border_color_mipmap;
   bool never_stop_sq_perf_counters;
   bool has_sqtt_rb_harvest_bug;
   bool has_sqtt_auto_flush_mode_bug;
   bool never_send_perfcounter_stop;
   bool discardable_allows_big_page;
   bool has_export_conflict_bug;

   /* Display features. */
   /* There are 2 display DCC codepaths, because display expects unaligned DCC. */
   /* Disable RB and pipe alignment to skip the retile blit. (1 RB chips only) */
   bool use_display_dcc_unaligned;
   /* Allocate both aligned and unaligned DCC and use the retile blit. */
   bool use_display_dcc_with_retile_blit;

   /* Memory info. */
   uint32_t pte_fragment_size;
   uint32_t gart_page_size;
   uint32_t gart_size_kb;
   uint32_t vram_size_kb;
   uint64_t vram_vis_size_kb;
   uint32_t vram_type;
   uint32_t max_heap_size_kb;
   uint32_t min_alloc_size;
   uint32_t address32_hi;
   bool has_dedicated_vram;
   bool all_vram_visible;
   bool smart_access_memory;
   bool has_l2_uncached;
   bool r600_has_virtual_memory;
   uint32_t max_tcc_blocks;
   uint32_t tcc_cache_line_size;
   bool tcc_rb_non_coherent; /* whether L2 inv is needed for render->texture transitions */
   unsigned pc_lines;
   uint32_t lds_size_per_workgroup;
   uint32_t lds_alloc_granularity;
   uint32_t lds_encode_granularity;

   /* CP info. */
   bool gfx_ib_pad_with_type2;
   unsigned ib_alignment; /* both start and size alignment */
   uint32_t me_fw_version;
   uint32_t me_fw_feature;
   uint32_t mec_fw_version;
   uint32_t mec_fw_feature;
   uint32_t pfp_fw_version;
   uint32_t pfp_fw_feature;

   /* Multimedia info. */
   struct {
      bool vcn_decode; /* TODO: remove */
   } has_video_hw;

   uint32_t uvd_fw_version;
   uint32_t vce_fw_version;
   uint32_t vce_harvest_config;
   struct video_caps_info {
      struct {
         uint32_t valid;
         uint32_t max_width;
         uint32_t max_height;
         uint32_t max_pixels_per_frame;
         uint32_t max_level;
         uint32_t pad;
      } codec_info[8]; /* the number of available codecs */
   } dec_caps, enc_caps;

   /* Kernel & winsys capabilities. */
   uint32_t drm_major; /* version */
   uint32_t drm_minor;
   uint32_t drm_patchlevel;
   bool is_amdgpu;
   bool has_userptr;
   bool has_syncobj;
   bool has_timeline_syncobj;
   bool has_fence_to_handle;
   bool has_local_buffers;
   bool has_bo_metadata;
   bool has_eqaa_surface_allocator;
   bool has_sparse_vm_mappings;
   bool has_scheduled_fence_dependency;
   bool has_stable_pstate;
   /* Whether SR-IOV is enabled or amdgpu.mcbp=1 was set on the kernel command line. */
   bool mid_command_buffer_preemption_enabled;
   bool has_tmz_support;
   bool kernel_has_modifiers;

   /* Shader cores. */
   uint32_t cu_mask[AMD_MAX_SE][AMD_MAX_SA_PER_SE];
   uint32_t r600_max_quad_pipes; /* wave size / 16 */
   uint32_t max_good_cu_per_sa;
   uint32_t min_good_cu_per_sa; /* min != max if SAs have different # of CUs */
   uint32_t max_se;             /* number of shader engines incl. disabled ones */
   uint32_t max_sa_per_se;      /* shader arrays per shader engine */
   uint32_t max_wave64_per_simd;
   uint32_t num_physical_sgprs_per_simd;
   uint32_t num_physical_wave64_vgprs_per_simd;
   uint32_t num_simd_per_compute_unit;
   uint32_t min_sgpr_alloc;
   uint32_t max_sgpr_alloc;
   uint32_t sgpr_alloc_granularity;
   uint32_t min_wave64_vgpr_alloc;
   uint32_t max_vgpr_alloc;
   uint32_t wave64_vgpr_alloc_granularity;
   uint32_t max_scratch_waves;

   /* Render backends (color + depth blocks). */
   uint32_t r300_num_gb_pipes;
   uint32_t r300_num_z_pipes;
   uint32_t r600_gb_backend_map; /* R600 harvest config */
   bool r600_gb_backend_map_valid;
   uint32_t r600_num_banks;
   uint32_t mc_arb_ramcfg;
   uint32_t gb_addr_config;
   uint32_t pa_sc_tile_steering_override; /* CLEAR_STATE also sets this */
   uint32_t max_render_backends;  /* number of render backends incl. disabled ones */
   uint32_t num_tile_pipes; /* pipe count from PIPE_CONFIG */
   uint32_t pipe_interleave_bytes;
   uint32_t enabled_rb_mask; /* GCN harvest config */
   uint64_t max_alignment;   /* from addrlib */
   uint32_t pbb_max_alloc_count;

   /* Tile modes. */
   uint32_t si_tile_mode_array[32];
   uint32_t cik_macrotile_mode_array[16];

   /* AMD_CU_MASK environment variable or ~0. */
   bool spi_cu_en_has_effect;
   uint32_t spi_cu_en;
};

bool ac_query_gpu_info(int fd, void *dev_p, struct radeon_info *info);

void ac_compute_driver_uuid(char *uuid, size_t size);

void ac_compute_device_uuid(struct radeon_info *info, char *uuid, size_t size);
void ac_print_gpu_info(struct radeon_info *info, FILE *f);
int ac_get_gs_table_depth(enum amd_gfx_level gfx_level, enum radeon_family family);
void ac_get_raster_config(struct radeon_info *info, uint32_t *raster_config_p,
                          uint32_t *raster_config_1_p, uint32_t *se_tile_repeat_p);
void ac_get_harvested_configs(struct radeon_info *info, unsigned raster_config,
                              unsigned *cik_raster_config_1_p, unsigned *raster_config_se);
unsigned ac_get_compute_resource_limits(const struct radeon_info *info,
                                        unsigned waves_per_threadgroup, unsigned max_waves_per_sh,
                                        unsigned threadgroups_per_cu);

struct ac_hs_info {
   uint32_t tess_offchip_block_dw_size;
   uint32_t max_offchip_buffers;
   uint32_t hs_offchip_param;
   uint32_t tess_factor_ring_size;
   uint32_t tess_offchip_ring_offset;
   uint32_t tess_offchip_ring_size;
};

void ac_get_hs_info(struct radeon_info *info,
                    struct ac_hs_info *hs);

/* Task rings BO layout information.
 * This BO is shared between GFX and ACE queues so that the ACE and GFX
 * firmware can cooperate on task->mesh dispatches and is also used to
 * store the task payload which is passed to mesh shaders.
 *
 * The driver only needs to create this BO once,
 * and it will always be able to accomodate the maximum needed
 * task payload size.
 *
 * The following memory layout is used:
 * 1. Control buffer: 9 DWORDs, 256 byte aligned
 *    Used by the firmware to maintain the current state.
 * (padding)
 * 2. Draw ring: 4 DWORDs per entry, 256 byte aligned
 *    Task shaders store the mesh dispatch size here.
 * (padding)
 * 3. Payload ring: 16K bytes per entry, 256 byte aligned.
 *    This is where task payload is stored by task shaders and
 *    read by mesh shaders.
 *
 */
struct ac_task_info {
   uint32_t draw_ring_offset;
   uint32_t payload_ring_offset;
   uint32_t bo_size_bytes;
   uint16_t num_entries;
};

/* Size of each payload entry in the task payload ring.
 * Spec requires minimum 16K bytes.
 */
#define AC_TASK_PAYLOAD_ENTRY_BYTES 16384

/* Size of each draw entry in the task draw ring.
 * 4 DWORDs per entry.
 */
#define AC_TASK_DRAW_ENTRY_BYTES 16

/* Size of the task control buffer. 9 DWORDs. */
#define AC_TASK_CTRLBUF_BYTES 36

void ac_get_task_info(struct radeon_info *info,
                      struct ac_task_info *task_info);

uint32_t ac_memory_ops_per_clock(uint32_t vram_type);

#ifdef __cplusplus
}
#endif

#endif /* AC_GPU_INFO_H */
