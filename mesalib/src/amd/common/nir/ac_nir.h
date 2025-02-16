/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */


#ifndef AC_NIR_H
#define AC_NIR_H

#include "ac_hw_stage.h"
#include "ac_shader_args.h"
#include "ac_shader_util.h"
#include "nir_defines.h"

#ifdef __cplusplus
extern "C" {
#endif

enum
{
   /* SPI_PS_INPUT_CNTL_i.OFFSET[0:4] */
   AC_EXP_PARAM_OFFSET_0 = 0,
   AC_EXP_PARAM_OFFSET_31 = 31,
   /* SPI_PS_INPUT_CNTL_i.DEFAULT_VAL[0:1] */
   AC_EXP_PARAM_DEFAULT_VAL_0000 = 64,
   AC_EXP_PARAM_DEFAULT_VAL_0001,
   AC_EXP_PARAM_DEFAULT_VAL_1110,
   AC_EXP_PARAM_DEFAULT_VAL_1111,
   AC_EXP_PARAM_UNDEFINED = 255, /* deprecated, use AC_EXP_PARAM_DEFAULT_VAL_0000 instead */
};

enum {
   AC_EXP_FLAG_COMPRESSED = (1 << 0),
   AC_EXP_FLAG_DONE       = (1 << 1),
   AC_EXP_FLAG_VALID_MASK = (1 << 2),
};

struct ac_nir_config {
   enum amd_gfx_level gfx_level;
   bool uses_aco;
};

/* Maps I/O semantics to the actual location used by the lowering pass. */
typedef unsigned (*ac_nir_map_io_driver_location)(unsigned semantic);

/* Forward declaration of nir_builder so we don't have to include nir_builder.h here */
struct nir_builder;
typedef struct nir_builder nir_builder;

struct nir_xfb_info;
typedef struct nir_xfb_info nir_xfb_info;

/* Executed by ac_nir_cull when the current primitive is accepted. */
typedef void (*ac_nir_cull_accepted)(nir_builder *b, void *state);

void
ac_nir_set_options(struct radeon_info *info, bool use_llvm,
                   nir_shader_compiler_options *options);

nir_def *
ac_nir_load_arg_at_offset(nir_builder *b, const struct ac_shader_args *ac_args,
                          struct ac_arg arg, unsigned relative_index);

nir_def *
ac_nir_load_arg(nir_builder *b, const struct ac_shader_args *ac_args, struct ac_arg arg);

nir_def *
ac_nir_load_arg_upper_bound(nir_builder *b, const struct ac_shader_args *ac_args, struct ac_arg arg,
                            unsigned upper_bound);

void ac_nir_store_arg(nir_builder *b, const struct ac_shader_args *ac_args, struct ac_arg arg,
                      nir_def *val);

nir_def *
ac_nir_unpack_arg(nir_builder *b, const struct ac_shader_args *ac_args, struct ac_arg arg,
                  unsigned rshift, unsigned bitwidth);

bool ac_nir_lower_sin_cos(nir_shader *shader);

bool ac_nir_lower_intrinsics_to_args(nir_shader *shader, const enum amd_gfx_level gfx_level,
                                     bool has_ls_vgpr_init_bug, const enum ac_hw_stage hw_stage,
                                     unsigned wave_size, unsigned workgroup_size,
                                     const struct ac_shader_args *ac_args);

nir_xfb_info *ac_nir_get_sorted_xfb_info(const nir_shader *nir);

bool ac_nir_optimize_outputs(nir_shader *nir, bool sprite_tex_disallowed,
                             int8_t slot_remap[NUM_TOTAL_VARYING_SLOTS],
                             uint8_t param_export_index[NUM_TOTAL_VARYING_SLOTS]);

void
ac_nir_lower_ls_outputs_to_mem(nir_shader *ls,
                               ac_nir_map_io_driver_location map,
                               enum amd_gfx_level gfx_level,
                               bool tcs_in_out_eq,
                               uint64_t tcs_inputs_via_temp,
                               uint64_t tcs_inputs_via_lds);

void
ac_nir_lower_hs_inputs_to_mem(nir_shader *shader,
                              ac_nir_map_io_driver_location map,
                              enum amd_gfx_level gfx_level,
                              bool tcs_in_out_eq,
                              uint64_t tcs_inputs_via_temp,
                              uint64_t tcs_inputs_via_lds);

void
ac_nir_lower_hs_outputs_to_mem(nir_shader *shader, const nir_tcs_info *info,
                               ac_nir_map_io_driver_location map,
                               enum amd_gfx_level gfx_level,
                               uint64_t tes_inputs_read,
                               uint32_t tes_patch_inputs_read,
                               unsigned wave_size);

void
ac_nir_lower_tes_inputs_to_mem(nir_shader *shader,
                               ac_nir_map_io_driver_location map);

void
ac_nir_compute_tess_wg_info(const struct radeon_info *info, const struct shader_info *tcs_info,
                            unsigned wave_size, bool tess_uses_primid, bool all_invocations_define_tess_levels,
                            unsigned num_tcs_input_cp, unsigned lds_input_vertex_size,
                            unsigned num_mem_tcs_outputs, unsigned num_mem_tcs_patch_outputs,
                            unsigned *num_patches_per_wg, unsigned *hw_lds_size);

void
ac_nir_lower_es_outputs_to_mem(nir_shader *shader,
                               ac_nir_map_io_driver_location map,
                               enum amd_gfx_level gfx_level,
                               unsigned esgs_itemsize,
                               uint64_t gs_inputs_read);

void
ac_nir_lower_gs_inputs_to_mem(nir_shader *shader,
                              ac_nir_map_io_driver_location map,
                              enum amd_gfx_level gfx_level,
                              bool triangle_strip_adjacency_fix);

bool
ac_nir_lower_indirect_derefs(nir_shader *shader,
                             enum amd_gfx_level gfx_level);

typedef struct {
   const struct radeon_info *hw_info;

   unsigned max_workgroup_size;
   unsigned wave_size;
   uint8_t clip_cull_dist_mask;
   const uint8_t *vs_output_param_offset; /* GFX11+ */
   bool has_param_exports;
   bool can_cull;
   bool disable_streamout;
   bool has_gen_prim_query;
   bool has_xfb_prim_query;
   bool use_gfx12_xfb_intrinsic;
   bool has_gs_invocations_query;
   bool has_gs_primitives_query;
   bool kill_pointsize;
   bool kill_layer;
   bool force_vrs;
   bool compact_primitives;

   /* VS */
   unsigned num_vertices_per_primitive;
   bool early_prim_export;
   bool passthrough;
   bool use_edgeflags;
   bool export_primitive_id;
   bool export_primitive_id_per_prim;
   uint32_t instance_rate_inputs;
   uint32_t user_clip_plane_enable_mask;

   /* GS */
   unsigned gs_out_vtx_bytes;
} ac_nir_lower_ngg_options;

void
ac_nir_lower_ngg_nogs(nir_shader *shader, const ac_nir_lower_ngg_options *options);

void
ac_nir_lower_ngg_gs(nir_shader *shader, const ac_nir_lower_ngg_options *options);

void
ac_nir_lower_ngg_mesh(nir_shader *shader,
                      const struct radeon_info *hw_info,
                      uint32_t clipdist_enable_mask,
                      const uint8_t *vs_output_param_offset,
                      bool has_param_exports,
                      bool *out_needs_scratch_ring,
                      unsigned wave_size,
                      unsigned workgroup_size,
                      bool multiview,
                      bool has_query,
                      bool fast_launch_2);

void
ac_nir_lower_task_outputs_to_mem(nir_shader *shader,
                                 unsigned task_payload_entry_bytes,
                                 unsigned task_num_entries,
                                 bool has_query);

void
ac_nir_lower_mesh_inputs_to_mem(nir_shader *shader,
                                unsigned task_payload_entry_bytes,
                                unsigned task_num_entries);

bool
ac_nir_lower_global_access(nir_shader *shader);

bool ac_nir_lower_resinfo(nir_shader *nir, enum amd_gfx_level gfx_level);
bool ac_nir_lower_image_opcodes(nir_shader *nir);

typedef struct ac_nir_gs_output_info {
   const uint8_t *streams;
   const uint8_t *streams_16bit_lo;
   const uint8_t *streams_16bit_hi;

   const uint8_t *varying_mask;
   const uint8_t *varying_mask_16bit_lo;
   const uint8_t *varying_mask_16bit_hi;

   const uint8_t *sysval_mask;

   /* type for each 16bit slot component */
   nir_alu_type (*types_16bit_lo)[4];
   nir_alu_type (*types_16bit_hi)[4];
} ac_nir_gs_output_info;

nir_shader *
ac_nir_create_gs_copy_shader(const nir_shader *gs_nir,
                             enum amd_gfx_level gfx_level,
                             uint32_t clip_cull_mask,
                             const uint8_t *param_offsets,
                             bool has_param_exports,
                             bool disable_streamout,
                             bool kill_pointsize,
                             bool kill_layer,
                             bool force_vrs,
                             ac_nir_gs_output_info *output_info);

void
ac_nir_lower_legacy_vs(nir_shader *nir,
                       enum amd_gfx_level gfx_level,
                       uint32_t clip_cull_mask,
                       const uint8_t *param_offsets,
                       bool has_param_exports,
                       bool export_primitive_id,
                       bool disable_streamout,
                       bool kill_pointsize,
                       bool kill_layer,
                       bool force_vrs);

void
ac_nir_lower_legacy_gs(nir_shader *nir,
                       bool has_gen_prim_query,
                       bool has_pipeline_stats_query,
                       ac_nir_gs_output_info *output_info);

/* This is a pre-link pass. It should only eliminate code and do lowering that mostly doesn't
 * generate AMD-specific intrinsics.
 */
typedef struct {
   /* System values. */
   bool force_center_interp_no_msaa; /* true if MSAA is disabled, false may mean that the state is unknown */
   bool uses_vrs_coarse_shading;
   bool load_sample_positions_always_loads_current_ones;
   bool dynamic_rasterization_samples;
   int force_front_face; /* 0 -> keep, 1 -> set to true, -1 -> set to false */
   bool optimize_frag_coord; /* TODO: remove this after RADV can handle it */
   bool frag_coord_is_center; /* GL requirement for sample shading */

   /* frag_coord/pixel_coord:
    *    allow_pixel_coord && (frag_coord_is_center || ps_iter_samples == 1 ||
    *                          force_center_interp_no_msaa ||
    *                          the fractional part of frag_coord.xy isn't used):
    *       * frag_coord.xy is replaced by u2f(pixel_coord) + 0.5.
    *    else:
    *       * pixel_coord is replaced by f2u16(frag_coord.xy)
    *       * ps_iter_samples == 0 means the state is unknown.
    *
    * barycentrics:
    *    force_center_interp_no_msaa:
    *       * All barycentrics including at_sample but excluding at_offset are changed to
    *         barycentric_pixel
    *    ps_iter_samples >= 2:
    *       * All barycentrics are changed to per-sample interpolation except at_offset/at_sample.
    *       * barycentric_at_sample(sample_id) is replaced by barycentric_sample.
    *
    * sample_mask_in:
    *    force_center_interp_no_msaa && !uses_vrs_coarse_shading:
    *       * sample_mask_in is replaced by b2i32(!helper_invocation)
    *    ps_iter_samples == 2, 4:
    *       * sample_mask_in is changed to (sample_mask_in & (ps_iter_mask << sample_id))
    *    ps_iter_samples == 8:
    *       * sample_mask_in is replaced by 1 << sample_id.
    *
    * When ps_iter_samples is equal to rasterization samples, set ps_iter_samples = 8 for this pass.
    */
   unsigned ps_iter_samples;

   /* fbfetch_output */
   bool fbfetch_is_1D;
   bool fbfetch_layered;
   bool fbfetch_msaa;
   bool fbfetch_apply_fmask;

   /* Outputs. */
   bool clamp_color;                /* GL only */
   bool alpha_test_alpha_to_one;    /* GL only, this only affects alpha test */
   enum compare_func alpha_func;    /* GL only */
   bool keep_alpha_for_mrtz;        /* this prevents killing alpha based on spi_shader_col_format_hint */
   unsigned spi_shader_col_format_hint; /* this only shrinks and eliminates output stores */
   bool kill_z;
   bool kill_stencil;
   bool kill_samplemask;
} ac_nir_lower_ps_early_options;

bool
ac_nir_lower_ps_early(nir_shader *nir, const ac_nir_lower_ps_early_options *options);

/* This is a post-link pass. It shouldn't eliminate any code and it shouldn't affect shader_info
 * (those should be done in the early pass).
 */
typedef struct {
   enum amd_gfx_level gfx_level;
   enum radeon_family family;
   bool use_aco;

   /* System values. */
   bool bc_optimize_for_persp;
   bool bc_optimize_for_linear;

   /* Exports. */
   bool uses_discard;
   bool alpha_to_coverage_via_mrtz;
   bool dual_src_blend_swizzle;
   unsigned spi_shader_col_format;
   unsigned color_is_int8;
   unsigned color_is_int10;
   bool alpha_to_one;

   /* Vulkan only */
   unsigned enable_mrt_output_nan_fixup;
   bool no_color_export;
   bool no_depth_export;
} ac_nir_lower_ps_late_options;

bool
ac_nir_lower_ps_late(nir_shader *nir, const ac_nir_lower_ps_late_options *options);

typedef struct {
   enum amd_gfx_level gfx_level;

   /* If true, round the layer component of the coordinates source to the nearest
    * integer for all array ops. This is always done for cube array ops.
    */
   bool lower_array_layer_round_even;

   /* Fix derivatives of constants and FS inputs in control flow.
    *
    * Ignores interpolateAtSample()/interpolateAtOffset(), dynamically indexed input loads,
    * pervertexEXT input loads, textureGather() with implicit LOD and 16-bit derivatives and
    * texture samples with nir_tex_src_min_lod.
    *
    * The layer must also be a constant or FS input.
    */
   bool fix_derivs_in_divergent_cf;
   unsigned max_wqm_vgprs;
} ac_nir_lower_tex_options;

bool
ac_nir_lower_tex(nir_shader *nir, const ac_nir_lower_tex_options *options);

void
ac_nir_store_debug_log_amd(nir_builder *b, nir_def *uvec4);

bool
ac_nir_opt_pack_half(nir_shader *shader, enum amd_gfx_level gfx_level);

unsigned
ac_nir_varying_expression_max_cost(nir_shader *producer, nir_shader *consumer);

bool
ac_nir_opt_shared_append(nir_shader *shader);

bool
ac_nir_flag_smem_for_loads(nir_shader *shader, enum amd_gfx_level gfx_level, bool use_llvm, bool after_lowering);

bool
ac_nir_lower_mem_access_bit_sizes(nir_shader *shader, enum amd_gfx_level gfx_level, bool use_llvm);

bool
ac_nir_optimize_uniform_atomics(nir_shader *nir);

unsigned
ac_nir_lower_bit_size_callback(const nir_instr *instr, void *data);

bool
ac_nir_might_lower_bit_size(const nir_shader *shader);

bool
ac_nir_mem_vectorize_callback(unsigned align_mul, unsigned align_offset, unsigned bit_size,
                              unsigned num_components, int64_t hole_size,
                              nir_intrinsic_instr *low, nir_intrinsic_instr *high, void *data);

bool
ac_nir_scalarize_overfetching_loads_callback(const nir_instr *instr, const void *data);

enum gl_access_qualifier
ac_nir_get_mem_access_flags(const nir_intrinsic_instr *instr);

#ifdef __cplusplus
}
#endif

#endif /* AC_NIR_H */
