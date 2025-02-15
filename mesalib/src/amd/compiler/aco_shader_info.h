/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef ACO_SHADER_INFO_H
#define ACO_SHADER_INFO_H

#include "ac_hw_stage.h"
#include "ac_shader_args.h"
#include "amd_family.h"
#include "shader_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

#define ACO_MAX_SO_OUTPUTS     128
#define ACO_MAX_SO_BUFFERS     4
#define ACO_MAX_VERTEX_ATTRIBS 32
#define ACO_MAX_VBS            32

struct aco_vs_prolog_info {
   struct ac_arg inputs;

   uint32_t instance_rate_inputs;
   uint32_t nontrivial_divisors;
   uint32_t zero_divisors;
   uint32_t post_shuffle;
   /* Having two separate fields instead of a single uint64_t makes it easier to remove attributes
    * using bitwise arithmetic.
    */
   uint32_t alpha_adjust_lo;
   uint32_t alpha_adjust_hi;

   uint8_t formats[ACO_MAX_VERTEX_ATTRIBS];

   unsigned num_attributes;
   uint32_t misaligned_mask;
   uint32_t unaligned_mask;
   bool is_ngg;
   gl_shader_stage next_stage;
};

struct aco_ps_epilog_info {
   struct ac_arg colors[MAX_DRAW_BUFFERS];
   uint8_t color_map[MAX_DRAW_BUFFERS];

   uint32_t spi_shader_col_format;

   /* Bitmasks, each bit represents one of the 8 MRTs. */
   uint8_t color_is_int8;
   uint8_t color_is_int10;

   bool mrt0_is_dual_src;

   bool alpha_to_coverage_via_mrtz;
   bool alpha_to_one;

   /* OpenGL only */
   uint16_t color_types;
   bool clamp_color;
   bool skip_null_export;
   bool writes_all_cbufs;
   enum compare_func alpha_func;
   /* Depth/stencil/samplemask are always passed via VGPRs, and the epilog key can choose
    * not to export them using these flags, which can be dynamic states.
    */
   bool kill_depth;
   bool kill_stencil;
   bool kill_samplemask;

   struct ac_arg alpha_reference;
   struct ac_arg depth;
   struct ac_arg stencil;
   struct ac_arg samplemask;
};

struct aco_ps_prolog_info {
   bool poly_stipple;
   unsigned poly_stipple_buf_offset;

   bool bc_optimize_for_persp;
   bool bc_optimize_for_linear;
   bool force_persp_sample_interp;
   bool force_linear_sample_interp;
   bool force_persp_center_interp;
   bool force_linear_center_interp;

   unsigned samplemask_log_ps_iter;
   bool get_frag_coord_from_pixel_coord;
   bool pixel_center_integer;
   bool force_samplemask_to_helper_invocation;
   unsigned num_interp_inputs;
   unsigned colors_read;
   int color_interp_vgpr_index[2];
   int color_attr_index[2];
   bool color_two_side;
   bool needs_wqm;

   struct ac_arg internal_bindings;
};

struct aco_shader_info {
   enum ac_hw_stage hw_stage;
   uint8_t wave_size;
   bool schedule_ngg_pos_exports; /* Whether we should schedule position exports up or not. */
   bool image_2d_view_of_3d;
   unsigned workgroup_size;
   bool merged_shader_compiled_separately; /* GFX9+ */
   struct ac_arg next_stage_pc;
   struct ac_arg epilog_pc; /* Vulkan only */
   struct {
      bool tcs_in_out_eq;
      bool any_tcs_inputs_via_lds;
      bool has_prolog;
   } vs;
   struct {
      struct ac_arg tcs_offchip_layout;

      /* Vulkan only */
      uint32_t num_lds_blocks;
   } tcs;
   struct {
      uint32_t num_inputs;
      unsigned spi_ps_input_ena;
      unsigned spi_ps_input_addr;
      bool has_prolog;
      bool has_epilog;

      /* OpenGL only */
      struct ac_arg alpha_reference;
   } ps;
   struct {
      bool uses_full_subgroups;
   } cs;

   uint32_t gfx9_gs_ring_lds_size;
};

enum aco_compiler_debug_level {
   ACO_COMPILER_DEBUG_LEVEL_ERROR,
};

struct aco_compiler_options {
   bool dump_ir;
   bool dump_preoptir;
   bool record_asm;
   bool record_ir;
   bool record_stats;
   bool has_ls_vgpr_init_bug;
   bool load_grid_size_from_user_sgpr;
   bool optimisations_disabled;
   uint8_t enable_mrt_output_nan_fixup;
   bool wgp_mode;
   bool is_opengl;
   enum radeon_family family;
   enum amd_gfx_level gfx_level;
   uint32_t address32_hi;
   struct {
      void (*func)(void* private_data, enum aco_compiler_debug_level level, const char* message);
      void* private_data;
   } debug;
};

enum aco_statistic {
   aco_statistic_hash,
   aco_statistic_instructions,
   aco_statistic_copies,
   aco_statistic_branches,
   aco_statistic_latency,
   aco_statistic_inv_throughput,
   aco_statistic_vmem_clauses,
   aco_statistic_smem_clauses,
   aco_statistic_sgpr_presched,
   aco_statistic_vgpr_presched,
   aco_statistic_valu,
   aco_statistic_salu,
   aco_statistic_vmem,
   aco_statistic_smem,
   aco_statistic_vopd,
   aco_num_statistics
};

enum aco_symbol_id {
   aco_symbol_invalid,
   aco_symbol_scratch_addr_lo,
   aco_symbol_scratch_addr_hi,
   aco_symbol_lds_ngg_scratch_base,
   aco_symbol_lds_ngg_gs_out_vertex_base,
   aco_symbol_const_data_addr,
};

struct aco_symbol {
   enum aco_symbol_id id;
   unsigned offset;
};

#define MAX_SGPRS 108
#define MAX_VGPRS       256
#define MAX_LDS_SIZE    65536 /* 64 KiB */
#define NUM_SAVED_VGPRS 2

struct aco_trap_handler_layout {
   uint32_t saved_vgprs[NUM_SAVED_VGPRS * 64];

   uint32_t ttmp0;
   uint32_t ttmp1;

   struct {
      uint32_t status;
      uint32_t mode;
      uint32_t trap_sts;
      uint32_t hw_id1;
      uint32_t gpr_alloc;
      uint32_t lds_alloc;
      uint32_t ib_sts;
   } sq_wave_regs;

   uint32_t m0;
   uint32_t exec_lo;
   uint32_t exec_hi;
   uint32_t sgprs[MAX_SGPRS];
   uint32_t vgprs[MAX_VGPRS * 64];
   uint32_t lds[MAX_LDS_SIZE / 4];
};

#ifdef __cplusplus
}
#endif
#endif
