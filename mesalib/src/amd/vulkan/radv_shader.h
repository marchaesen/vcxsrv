/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * based in part on anv driver which is:
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

#ifndef RADV_SHADER_H
#define RADV_SHADER_H

#include "ac_binary.h"
#include "ac_shader_util.h"

#include "amd_family.h"
#include "radv_constants.h"

#include "nir/nir.h"
#include "vulkan/util/vk_object.h"
#include "vulkan/util/vk_shader_module.h"
#include "vulkan/vulkan.h"

#define RADV_VERT_ATTRIB_MAX MAX2(VERT_ATTRIB_MAX, VERT_ATTRIB_GENERIC0 + MAX_VERTEX_ATTRIBS)

struct radv_physical_device;
struct radv_device;
struct radv_pipeline;
struct radv_pipeline_cache;
struct radv_pipeline_key;

struct radv_vs_out_key {
   uint32_t as_es : 1;
   uint32_t as_ls : 1;
   uint32_t as_ngg : 1;
   uint32_t as_ngg_passthrough : 1;
   uint32_t export_prim_id : 1;
   uint32_t export_layer_id : 1;
   uint32_t export_clip_dists : 1;
   uint32_t export_viewport_index : 1;
};

struct radv_vs_variant_key {
   struct radv_vs_out_key out;

   uint32_t instance_rate_inputs;
   uint32_t instance_rate_divisors[MAX_VERTEX_ATTRIBS];
   uint8_t vertex_attribute_formats[MAX_VERTEX_ATTRIBS];
   uint32_t vertex_attribute_bindings[MAX_VERTEX_ATTRIBS];
   uint32_t vertex_attribute_offsets[MAX_VERTEX_ATTRIBS];
   uint32_t vertex_attribute_strides[MAX_VERTEX_ATTRIBS];
   uint8_t vertex_binding_align[MAX_VBS];

   /* For 2_10_10_10 formats the alpha is handled as unsigned by pre-vega HW.
    * so we may need to fix it up. */
   enum ac_fetch_format alpha_adjust[MAX_VERTEX_ATTRIBS];

   /* For some formats the channels have to be shuffled. */
   uint32_t post_shuffle;

   /* Output primitive type. */
   uint8_t outprim;

   /* Provoking vertex mode. */
   bool provoking_vtx_last;
};

struct radv_tes_variant_key {
   struct radv_vs_out_key out;
};

struct radv_tcs_variant_key {
   struct radv_vs_variant_key vs_key;
   unsigned primitive_mode;
   unsigned input_vertices;
};

struct radv_fs_variant_key {
   uint32_t col_format;
   uint8_t log2_ps_iter_samples;
   uint8_t num_samples;
   uint32_t is_int8;
   uint32_t is_int10;
};

struct radv_cs_variant_key {
   uint8_t subgroup_size;
};

struct radv_shader_variant_key {
   union {
      struct radv_vs_variant_key vs;
      struct radv_fs_variant_key fs;
      struct radv_tes_variant_key tes;
      struct radv_tcs_variant_key tcs;
      struct radv_cs_variant_key cs;

      /* A common prefix of the vs and tes keys. */
      struct radv_vs_out_key vs_common_out;
   };
   bool has_multiview_view_index;
};

enum radv_compiler_debug_level {
   RADV_COMPILER_DEBUG_LEVEL_PERFWARN,
   RADV_COMPILER_DEBUG_LEVEL_ERROR,
};

struct radv_nir_compiler_options {
   struct radv_pipeline_layout *layout;
   struct radv_shader_variant_key key;
   bool explicit_scratch_args;
   bool clamp_shadow_reference;
   bool robust_buffer_access;
   bool adjust_frag_coord_z;
   bool dump_shader;
   bool dump_preoptir;
   bool record_ir;
   bool record_stats;
   bool check_ir;
   bool has_ls_vgpr_init_bug;
   bool has_image_load_dcc_bug;
   bool enable_mrt_output_nan_fixup;
   bool disable_optimizations; /* only used by ACO */
   bool wgp_mode;
   enum radeon_family family;
   enum chip_class chip_class;
   const struct radeon_info *info;
   uint32_t tess_offchip_block_dw_size;
   uint32_t address32_hi;
   uint8_t force_vrs_rates;

   struct {
      void (*func)(void *private_data, enum radv_compiler_debug_level level, const char *message);
      void *private_data;
   } debug;
};

enum radv_ud_index {
   AC_UD_SCRATCH_RING_OFFSETS = 0,
   AC_UD_PUSH_CONSTANTS = 1,
   AC_UD_INLINE_PUSH_CONSTANTS = 2,
   AC_UD_INDIRECT_DESCRIPTOR_SETS = 3,
   AC_UD_VIEW_INDEX = 4,
   AC_UD_STREAMOUT_BUFFERS = 5,
   AC_UD_NGG_GS_STATE = 6,
   AC_UD_NGG_CULLING_SETTINGS = 7,
   AC_UD_NGG_VIEWPORT = 8,
   AC_UD_SHADER_START = 9,
   AC_UD_VS_VERTEX_BUFFERS = AC_UD_SHADER_START,
   AC_UD_VS_BASE_VERTEX_START_INSTANCE,
   AC_UD_VS_MAX_UD,
   AC_UD_PS_MAX_UD,
   AC_UD_CS_GRID_SIZE = AC_UD_SHADER_START,
   AC_UD_CS_SBT_DESCRIPTORS,
   AC_UD_CS_MAX_UD,
   AC_UD_GS_MAX_UD,
   AC_UD_TCS_MAX_UD,
   AC_UD_TES_MAX_UD,
   AC_UD_MAX_UD = AC_UD_TCS_MAX_UD,
};

struct radv_stream_output {
   uint8_t location;
   uint8_t buffer;
   uint16_t offset;
   uint8_t component_mask;
   uint8_t stream;
};

struct radv_streamout_info {
   uint16_t num_outputs;
   struct radv_stream_output outputs[MAX_SO_OUTPUTS];
   uint16_t strides[MAX_SO_BUFFERS];
   uint32_t enabled_stream_buffers_mask;
};

struct radv_userdata_info {
   int8_t sgpr_idx;
   uint8_t num_sgprs;
};

struct radv_userdata_locations {
   struct radv_userdata_info descriptor_sets[MAX_SETS];
   struct radv_userdata_info shader_data[AC_UD_MAX_UD];
   uint32_t descriptor_sets_enabled;
};

struct radv_vs_output_info {
   uint8_t vs_output_param_offset[VARYING_SLOT_MAX];
   uint8_t clip_dist_mask;
   uint8_t cull_dist_mask;
   uint8_t param_exports;
   bool writes_pointsize;
   bool writes_layer;
   bool writes_viewport_index;
   bool writes_primitive_shading_rate;
   bool export_prim_id;
   unsigned pos_exports;
};

struct radv_es_output_info {
   uint32_t esgs_itemsize;
};

struct gfx9_gs_info {
   uint32_t vgt_gs_onchip_cntl;
   uint32_t vgt_gs_max_prims_per_subgroup;
   uint32_t vgt_esgs_ring_itemsize;
   uint32_t lds_size;
};

struct gfx10_ngg_info {
   uint16_t ngg_emit_size; /* in dwords */
   uint32_t hw_max_esverts;
   uint32_t max_gsprims;
   uint32_t max_out_verts;
   uint32_t prim_amp_factor;
   uint32_t vgt_esgs_ring_itemsize;
   uint32_t esgs_ring_size;
   bool max_vert_out_per_gs_instance;
   bool enable_vertex_grouping;
};

struct radv_shader_info {
   bool loads_push_constants;
   bool loads_dynamic_offsets;
   uint8_t min_push_constant_used;
   uint8_t max_push_constant_used;
   bool has_only_32bit_push_constants;
   bool has_indirect_push_constants;
   uint8_t num_inline_push_consts;
   uint8_t base_inline_push_consts;
   uint32_t desc_set_used_mask;
   bool needs_multiview_view_index;
   bool uses_invocation_id;
   bool uses_prim_id;
   uint8_t wave_size;
   uint8_t ballot_bit_size;
   struct radv_userdata_locations user_sgprs_locs;
   unsigned num_user_sgprs;
   unsigned num_input_sgprs;
   unsigned num_input_vgprs;
   unsigned private_mem_vgprs;
   bool need_indirect_descriptor_sets;
   bool is_ngg;
   bool is_ngg_passthrough;
   bool has_ngg_culling;
   bool has_ngg_early_prim_export;
   uint32_t num_lds_blocks_when_not_culling;
   uint32_t num_tess_patches;
   unsigned workgroup_size;
   struct {
      uint8_t input_usage_mask[RADV_VERT_ATTRIB_MAX];
      uint8_t output_usage_mask[VARYING_SLOT_VAR31 + 1];
      bool needs_draw_id;
      bool needs_instance_id;
      struct radv_vs_output_info outinfo;
      struct radv_es_output_info es_info;
      bool as_es;
      bool as_ls;
      bool export_prim_id;
      bool tcs_in_out_eq;
      uint64_t tcs_temp_only_input_mask;
      uint8_t num_linked_outputs;
      bool needs_base_instance;
      bool use_per_attribute_vb_descs;
      uint32_t vb_desc_usage_mask;
   } vs;
   struct {
      uint8_t output_usage_mask[VARYING_SLOT_VAR31 + 1];
      uint8_t num_stream_output_components[4];
      uint8_t output_streams[VARYING_SLOT_VAR31 + 1];
      uint8_t max_stream;
      unsigned gsvs_vertex_size;
      unsigned max_gsvs_emit_size;
      unsigned vertices_in;
      unsigned vertices_out;
      unsigned output_prim;
      unsigned invocations;
      unsigned es_type; /* GFX9: VS or TES */
      uint8_t num_linked_inputs;
   } gs;
   struct {
      uint8_t output_usage_mask[VARYING_SLOT_VAR31 + 1];
      struct radv_vs_output_info outinfo;
      struct radv_es_output_info es_info;
      bool as_es;
      unsigned primitive_mode;
      enum gl_tess_spacing spacing;
      bool ccw;
      bool point_mode;
      bool export_prim_id;
      uint8_t num_linked_inputs;
      uint8_t num_linked_patch_inputs;
      uint8_t num_linked_outputs;
   } tes;
   struct {
      bool uses_sample_shading;
      bool needs_sample_positions;
      bool writes_memory;
      bool writes_z;
      bool writes_stencil;
      bool writes_sample_mask;
      bool has_pcoord;
      bool prim_id_input;
      bool layer_input;
      bool viewport_index_input;
      uint8_t num_input_clips_culls;
      uint32_t input_mask;
      uint32_t flat_shaded_mask;
      uint32_t explicit_shaded_mask;
      uint32_t float16_shaded_mask;
      uint32_t num_interp;
      bool can_discard;
      bool early_fragment_test;
      bool post_depth_coverage;
      bool reads_sample_mask_in;
      uint8_t depth_layout;
      bool uses_persp_or_linear_interp;
      bool allow_flat_shading;
   } ps;
   struct {
      bool uses_grid_size;
      bool uses_block_id[3];
      bool uses_thread_id[3];
      bool uses_local_invocation_idx;
      unsigned block_size[3];

      bool uses_sbt;
   } cs;
   struct {
      uint64_t tes_inputs_read;
      uint64_t tes_patch_inputs_read;
      unsigned tcs_vertices_out;
      uint32_t num_lds_blocks;
      uint8_t num_linked_inputs;
      uint8_t num_linked_outputs;
      uint8_t num_linked_patch_outputs;
      bool tes_reads_tess_factors : 1;
   } tcs;

   struct radv_streamout_info so;

   struct gfx9_gs_info gs_ring_info;
   struct gfx10_ngg_info ngg_info;

   unsigned float_controls_mode;
};

enum radv_shader_binary_type { RADV_BINARY_TYPE_LEGACY, RADV_BINARY_TYPE_RTLD };

struct radv_shader_binary {
   enum radv_shader_binary_type type;
   gl_shader_stage stage;
   bool is_gs_copy_shader;

   struct radv_shader_info info;

   /* Self-referential size so we avoid consistency issues. */
   uint32_t total_size;
};

struct radv_shader_binary_legacy {
   struct radv_shader_binary base;
   struct ac_shader_config config;
   unsigned code_size;
   unsigned exec_size;
   unsigned ir_size;
   unsigned disasm_size;
   unsigned stats_size;

   /* data has size of stats_size + code_size + ir_size + disasm_size + 2,
    * where the +2 is for 0 of the ir strings. */
   uint8_t data[0];
};

struct radv_shader_binary_rtld {
   struct radv_shader_binary base;
   unsigned elf_size;
   unsigned llvm_ir_size;
   uint8_t data[0];
};

struct radv_shader_variant {
   uint32_t ref_count;

   struct radeon_winsys_bo *bo;
   uint64_t bo_offset;
   struct ac_shader_config config;
   uint8_t *code_ptr;
   uint32_t code_size;
   uint32_t exec_size;
   struct radv_shader_info info;

   /* debug only */
   char *spirv;
   uint32_t spirv_size;
   char *nir_string;
   char *disasm_string;
   char *ir_string;
   uint32_t *statistics;

   struct list_head slab_list;
};

struct radv_shader_slab {
   struct list_head slabs;
   struct list_head shaders;
   struct radeon_winsys_bo *bo;
   uint64_t size;
   char *ptr;
};

void radv_optimize_nir(const struct radv_device *device, struct nir_shader *shader,
                       bool optimize_conservatively, bool allow_copies);
void radv_optimize_nir_algebraic(nir_shader *shader, bool opt_offsets);
bool radv_nir_lower_ycbcr_textures(nir_shader *shader, const struct radv_pipeline_layout *layout);

nir_shader *radv_shader_compile_to_nir(struct radv_device *device, struct vk_shader_module *module,
                                       const char *entrypoint_name, gl_shader_stage stage,
                                       const VkSpecializationInfo *spec_info,
                                       const VkPipelineCreateFlags flags,
                                       const struct radv_pipeline_layout *layout,
                                       const struct radv_pipeline_key *key);

void radv_destroy_shader_slabs(struct radv_device *device);

VkResult radv_create_shaders(struct radv_pipeline *pipeline, struct radv_device *device,
                             struct radv_pipeline_cache *cache, const struct radv_pipeline_key *key,
                             const VkPipelineShaderStageCreateInfo **pStages,
                             const VkPipelineCreateFlags flags,
                             VkPipelineCreationFeedbackEXT *pipeline_feedback,
                             VkPipelineCreationFeedbackEXT **stage_feedbacks);

struct radv_shader_variant *radv_shader_variant_create(struct radv_device *device,
                                                       const struct radv_shader_binary *binary,
                                                       bool keep_shader_info);
struct radv_shader_variant *radv_shader_variant_compile(
   struct radv_device *device, struct vk_shader_module *module, struct nir_shader *const *shaders,
   int shader_count, struct radv_pipeline_layout *layout, const struct radv_shader_variant_key *key,
   struct radv_shader_info *info, bool keep_shader_info, bool keep_statistic_info,
   bool disable_optimizations, struct radv_shader_binary **binary_out);

struct radv_shader_variant *
radv_create_gs_copy_shader(struct radv_device *device, struct nir_shader *nir,
                           struct radv_shader_info *info, struct radv_shader_binary **binary_out,
                           bool multiview, bool keep_shader_info, bool keep_statistic_info,
                           bool disable_optimizations);

struct radv_shader_variant *radv_create_trap_handler_shader(struct radv_device *device);

void radv_shader_variant_destroy(struct radv_device *device, struct radv_shader_variant *variant);

unsigned radv_get_max_waves(const struct radv_device *device, struct radv_shader_variant *variant,
                            gl_shader_stage stage);

const char *radv_get_shader_name(struct radv_shader_info *info, gl_shader_stage stage);

bool radv_can_dump_shader(struct radv_device *device, struct vk_shader_module *module,
                          bool meta_shader);

bool radv_can_dump_shader_stats(struct radv_device *device, struct vk_shader_module *module);

VkResult radv_dump_shader_stats(struct radv_device *device, struct radv_pipeline *pipeline,
                                gl_shader_stage stage, FILE *output);

static inline unsigned
calculate_tess_lds_size(enum chip_class chip_class, unsigned tcs_num_input_vertices,
                        unsigned tcs_num_output_vertices, unsigned tcs_num_inputs,
                        unsigned tcs_num_patches, unsigned tcs_num_outputs,
                        unsigned tcs_num_patch_outputs)
{
   unsigned input_vertex_size = tcs_num_inputs * 16;
   unsigned output_vertex_size = tcs_num_outputs * 16;

   unsigned input_patch_size = tcs_num_input_vertices * input_vertex_size;

   unsigned pervertex_output_patch_size = tcs_num_output_vertices * output_vertex_size;
   unsigned output_patch_size = pervertex_output_patch_size + tcs_num_patch_outputs * 16;

   unsigned output_patch0_offset = input_patch_size * tcs_num_patches;

   unsigned lds_size = output_patch0_offset + output_patch_size * tcs_num_patches;

   if (chip_class >= GFX7) {
      assert(lds_size <= 65536);
      lds_size = align(lds_size, 512) / 512;
   } else {
      assert(lds_size <= 32768);
      lds_size = align(lds_size, 256) / 256;
   }

   return lds_size;
}

static inline unsigned
get_tcs_num_patches(unsigned tcs_num_input_vertices, unsigned tcs_num_output_vertices,
                    unsigned tcs_num_inputs, unsigned tcs_num_outputs,
                    unsigned tcs_num_patch_outputs, unsigned tess_offchip_block_dw_size,
                    enum chip_class chip_class, enum radeon_family family)
{
   uint32_t input_vertex_size = tcs_num_inputs * 16;
   uint32_t input_patch_size = tcs_num_input_vertices * input_vertex_size;
   uint32_t output_vertex_size = tcs_num_outputs * 16;
   uint32_t pervertex_output_patch_size = tcs_num_output_vertices * output_vertex_size;
   uint32_t output_patch_size = pervertex_output_patch_size + tcs_num_patch_outputs * 16;

   /* Ensure that we only need one wave per SIMD so we don't need to check
    * resource usage. Also ensures that the number of tcs in and out
    * vertices per threadgroup are at most 256.
    */
   unsigned num_patches = 64 / MAX2(tcs_num_input_vertices, tcs_num_output_vertices) * 4;
   /* Make sure that the data fits in LDS. This assumes the shaders only
    * use LDS for the inputs and outputs.
    */
   unsigned hardware_lds_size = 32768;

   /* Looks like STONEY hangs if we use more than 32 KiB LDS in a single
    * threadgroup, even though there is more than 32 KiB LDS.
    *
    * Test: dEQP-VK.tessellation.shader_input_output.barrier
    */
   if (chip_class >= GFX7 && family != CHIP_STONEY)
      hardware_lds_size = 65536;

   if (input_patch_size + output_patch_size)
      num_patches = MIN2(num_patches, hardware_lds_size / (input_patch_size + output_patch_size));
   /* Make sure the output data fits in the offchip buffer */
   if (output_patch_size)
      num_patches = MIN2(num_patches, (tess_offchip_block_dw_size * 4) / output_patch_size);
   /* Not necessary for correctness, but improves performance. The
    * specific value is taken from the proprietary driver.
    */
   num_patches = MIN2(num_patches, 40);

   /* GFX6 bug workaround - limit LS-HS threadgroups to only one wave. */
   if (chip_class == GFX6) {
      unsigned one_wave = 64 / MAX2(tcs_num_input_vertices, tcs_num_output_vertices);
      num_patches = MIN2(num_patches, one_wave);
   }
   return num_patches;
}

void radv_lower_io(struct radv_device *device, nir_shader *nir);

bool radv_lower_io_to_mem(struct radv_device *device, struct nir_shader *nir,
                          struct radv_shader_info *info, const struct radv_pipeline_key *pl_key);

void radv_lower_ngg(struct radv_device *device, struct nir_shader *nir,
                    struct radv_shader_info *info,
                    const struct radv_pipeline_key *pl_key,
                    struct radv_shader_variant_key *key,
                    bool consider_culling);

bool radv_consider_culling(struct radv_device *device, struct nir_shader *nir,
                           uint64_t ps_inputs_read);

void radv_get_nir_options(struct radv_physical_device *device);

#endif
