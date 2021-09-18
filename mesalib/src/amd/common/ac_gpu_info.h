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

#ifdef __cplusplus
extern "C" {
#endif

struct amdgpu_gpu_info;

struct radeon_info {
   /* PCI info: domain:bus:dev:func */
   uint32_t pci_domain;
   uint32_t pci_bus;
   uint32_t pci_dev;
   uint32_t pci_func;

   /* Device info. */
   const char *name;
   const char *marketing_name;
   bool is_pro_graphics;
   uint32_t pci_id;
   uint32_t pci_rev_id;
   enum radeon_family family;
   enum chip_class chip_class;
   uint32_t family_id;
   uint32_t chip_external_rev;
   uint32_t clock_crystal_freq;

   /* Features. */
   bool has_graphics; /* false if the chip is compute-only */
   uint32_t num_rings[NUM_RING_TYPES];
   uint32_t ib_pad_dw_mask[NUM_RING_TYPES];
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
   uint64_t gart_size;
   uint64_t vram_size;
   uint64_t vram_vis_size;
   uint32_t vram_bit_width;
   uint32_t vram_type;
   unsigned gds_size;
   unsigned gds_gfx_partition_size;
   uint64_t max_alloc_size;
   uint32_t min_alloc_size;
   uint32_t address32_hi;
   bool has_dedicated_vram;
   bool all_vram_visible;
   bool smart_access_memory;
   bool has_l2_uncached;
   bool r600_has_virtual_memory;
   uint32_t max_tcc_blocks;
   uint32_t num_tcc_blocks;
   uint32_t tcc_cache_line_size;
   bool tcc_rb_non_coherent; /* whether L2 inv is needed for render->texture transitions */
   unsigned pc_lines;
   uint32_t lds_size_per_workgroup;
   uint32_t lds_alloc_granularity;
   uint32_t lds_encode_granularity;
   uint32_t max_memory_clock;
   uint32_t ce_ram_size;
   uint32_t l1_cache_size;
   uint32_t l2_cache_size;

   /* CP info. */
   bool gfx_ib_pad_with_type2;
   unsigned ib_alignment; /* both start and size alignment */
   uint32_t me_fw_version;
   uint32_t me_fw_feature;
   uint32_t pfp_fw_version;
   uint32_t pfp_fw_feature;
   uint32_t ce_fw_version;
   uint32_t ce_fw_feature;

   /* Multimedia info. */
   struct {
      bool uvd_decode;
      bool vcn_decode;
      bool jpeg_decode;
      bool vce_encode;
      bool uvd_encode;
      bool vcn_encode;
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
   bool kernel_flushes_hdp_before_ib;
   bool htile_cmask_support_1d_tiling;
   bool si_TA_CS_BC_BASE_ADDR_allowed;
   bool has_bo_metadata;
   bool has_gpu_reset_status_query;
   bool has_eqaa_surface_allocator;
   bool has_format_bc1_through_bc7;
   bool kernel_flushes_tc_l2_after_ib;
   bool has_indirect_compute_dispatch;
   bool has_unaligned_shader_loads;
   bool has_sparse_vm_mappings;
   bool has_2d_tiling;
   bool has_read_registers_query;
   bool has_gds_ordered_append;
   bool has_scheduled_fence_dependency;
   /* Whether SR-IOV is enabled or amdgpu.mcbp=1 was set on the kernel command line. */
   bool mid_command_buffer_preemption_enabled;
   bool has_tmz_support;
   bool kernel_has_modifiers;

   /* Shader cores. */
   uint32_t cu_mask[4][2];
   uint32_t r600_max_quad_pipes; /* wave size / 16 */
   uint32_t max_shader_clock;
   uint32_t num_good_compute_units;
   uint32_t max_good_cu_per_sa;
   uint32_t min_good_cu_per_sa; /* min != max if SAs have different # of CUs */
   uint32_t max_se;             /* number of shader engines incl. disabled ones */
   uint32_t num_se;             /* number of enabled shader engines */
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
};

bool ac_query_gpu_info(int fd, void *dev_p, struct radeon_info *info,
                       struct amdgpu_gpu_info *amdinfo);

void ac_compute_driver_uuid(char *uuid, size_t size);

void ac_compute_device_uuid(struct radeon_info *info, char *uuid, size_t size);
void ac_print_gpu_info(struct radeon_info *info, FILE *f);
int ac_get_gs_table_depth(enum chip_class chip_class, enum radeon_family family);
void ac_get_raster_config(struct radeon_info *info, uint32_t *raster_config_p,
                          uint32_t *raster_config_1_p, uint32_t *se_tile_repeat_p);
void ac_get_harvested_configs(struct radeon_info *info, unsigned raster_config,
                              unsigned *cik_raster_config_1_p, unsigned *raster_config_se);
unsigned ac_get_compute_resource_limits(struct radeon_info *info, unsigned waves_per_threadgroup,
                                        unsigned max_waves_per_sh, unsigned threadgroups_per_cu);

#ifdef __cplusplus
}
#endif

#endif /* AC_GPU_INFO_H */
