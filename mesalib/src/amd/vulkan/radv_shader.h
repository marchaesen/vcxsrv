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
#include "amd_family.h"
#include "radv_constants.h"

#include "nir/nir.h"
#include "vulkan/vulkan.h"

struct radv_device;

struct radv_shader_module {
	struct nir_shader *nir;
	unsigned char sha1[20];
	uint32_t size;
	char data[0];
};

enum {
	RADV_ALPHA_ADJUST_NONE = 0,
	RADV_ALPHA_ADJUST_SNORM = 1,
	RADV_ALPHA_ADJUST_SINT = 2,
	RADV_ALPHA_ADJUST_SSCALED = 3,
};

struct radv_vs_out_key {
	uint32_t as_es:1;
	uint32_t as_ls:1;
	uint32_t as_ngg:1;
	uint32_t export_prim_id:1;
	uint32_t export_layer_id:1;
	uint32_t export_clip_dists:1;
};

struct radv_vs_variant_key {
	struct radv_vs_out_key out;

	uint32_t instance_rate_inputs;
	uint32_t instance_rate_divisors[MAX_VERTEX_ATTRIBS];
	uint8_t vertex_attribute_formats[MAX_VERTEX_ATTRIBS];
	uint32_t vertex_attribute_bindings[MAX_VERTEX_ATTRIBS];
	uint32_t vertex_attribute_offsets[MAX_VERTEX_ATTRIBS];
	uint32_t vertex_attribute_strides[MAX_VERTEX_ATTRIBS];

	/* For 2_10_10_10 formats the alpha is handled as unsigned by pre-vega HW.
	 * so we may need to fix it up. */
	uint64_t alpha_adjust;

	/* For some formats the channels have to be shuffled. */
	uint32_t post_shuffle;

	/* Output primitive type. */
	uint8_t outprim;
};

struct radv_tes_variant_key {
	struct radv_vs_out_key out;

	uint8_t num_patches;
	uint8_t tcs_num_outputs;
};

struct radv_tcs_variant_key {
	struct radv_vs_variant_key vs_key;
	unsigned primitive_mode;
	unsigned input_vertices;
	unsigned num_inputs;
	uint32_t tes_reads_tess_factors:1;
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

struct radv_nir_compiler_options {
	struct radv_pipeline_layout *layout;
	struct radv_shader_variant_key key;
	bool unsafe_math;
	bool supports_spill;
	bool clamp_shadow_reference;
	bool robust_buffer_access;
	bool dump_shader;
	bool dump_preoptir;
	bool record_ir;
	bool check_ir;
	bool has_ls_vgpr_init_bug;
	bool use_ngg_streamout;
	enum radeon_family family;
	enum chip_class chip_class;
	uint32_t tess_offchip_block_dw_size;
	uint32_t address32_hi;
};

enum radv_ud_index {
	AC_UD_SCRATCH_RING_OFFSETS = 0,
	AC_UD_PUSH_CONSTANTS = 1,
	AC_UD_INLINE_PUSH_CONSTANTS = 2,
	AC_UD_INDIRECT_DESCRIPTOR_SETS = 3,
	AC_UD_VIEW_INDEX = 4,
	AC_UD_STREAMOUT_BUFFERS = 5,
	AC_UD_SHADER_START = 6,
	AC_UD_VS_VERTEX_BUFFERS = AC_UD_SHADER_START,
	AC_UD_VS_BASE_VERTEX_START_INSTANCE,
	AC_UD_VS_MAX_UD,
	AC_UD_PS_MAX_UD,
	AC_UD_CS_GRID_SIZE = AC_UD_SHADER_START,
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
	uint8_t	vs_output_param_offset[VARYING_SLOT_MAX];
	uint8_t clip_dist_mask;
	uint8_t cull_dist_mask;
	uint8_t param_exports;
	bool writes_pointsize;
	bool writes_layer;
	bool writes_viewport_index;
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
	struct radv_userdata_locations user_sgprs_locs;
	unsigned num_user_sgprs;
	unsigned num_input_sgprs;
	unsigned num_input_vgprs;
	unsigned private_mem_vgprs;
	bool need_indirect_descriptor_sets;
	bool is_ngg;
	struct {
		uint64_t ls_outputs_written;
		uint8_t input_usage_mask[VERT_ATTRIB_MAX];
		uint8_t output_usage_mask[VARYING_SLOT_VAR31 + 1];
		bool has_vertex_buffers; /* needs vertex buffers and base/start */
		bool needs_draw_id;
		bool needs_instance_id;
		struct radv_vs_output_info outinfo;
		struct radv_es_output_info es_info;
		bool as_es;
		bool as_ls;
		bool export_prim_id;
	} vs;
	struct {
		uint8_t output_usage_mask[VARYING_SLOT_VAR31 + 1];
		uint8_t num_stream_output_components[4];
		uint8_t output_streams[VARYING_SLOT_VAR31 + 1];
		uint8_t max_stream;
		bool writes_memory;
		unsigned gsvs_vertex_size;
		unsigned max_gsvs_emit_size;
		unsigned vertices_in;
		unsigned vertices_out;
		unsigned output_prim;
		unsigned invocations;
		unsigned es_type; /* GFX9: VS or TES */
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
	} tes;
	struct {
		bool force_persample;
		bool needs_sample_positions;
		bool writes_memory;
		bool writes_z;
		bool writes_stencil;
		bool writes_sample_mask;
		bool has_pcoord;
		bool prim_id_input;
		bool layer_input;
		uint8_t num_input_clips_culls;
		uint32_t input_mask;
		uint32_t flat_shaded_mask;
		uint32_t float16_shaded_mask;
		uint32_t num_interp;
		bool can_discard;
		bool early_fragment_test;
		bool post_depth_coverage;
	} ps;
	struct {
		bool uses_grid_size;
		bool uses_block_id[3];
		bool uses_thread_id[3];
		bool uses_local_invocation_idx;
		unsigned block_size[3];
	} cs;
	struct {
		uint64_t outputs_written;
		uint64_t patch_outputs_written;
		unsigned tcs_vertices_out;
		uint32_t num_patches;
		uint32_t lds_size;
	} tcs;

	struct radv_streamout_info so;

	struct gfx9_gs_info gs_ring_info;
	struct gfx10_ngg_info ngg_info;

	unsigned float_controls_mode;
};

enum radv_shader_binary_type {
	RADV_BINARY_TYPE_LEGACY,
	RADV_BINARY_TYPE_RTLD
};

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
	
	/* data has size of code_size + ir_size + disasm_size + 2, where
	 * the +2 is for 0 of the ir strings. */
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
	uint32_t code_size;
	uint32_t exec_size;
	struct radv_shader_info info;

	/* debug only */
	bool aco_used;
	char *spirv;
	uint32_t spirv_size;
	char *nir_string;
	char *disasm_string;
	char *ir_string;

	struct list_head slab_list;
};

struct radv_shader_slab {
	struct list_head slabs;
	struct list_head shaders;
	struct radeon_winsys_bo *bo;
	uint64_t size;
	char *ptr;
};

void
radv_optimize_nir(struct nir_shader *shader, bool optimize_conservatively,
		  bool allow_copies);
bool
radv_nir_lower_ycbcr_textures(nir_shader *shader,
                             const struct radv_pipeline_layout *layout);

nir_shader *
radv_shader_compile_to_nir(struct radv_device *device,
			   struct radv_shader_module *module,
			   const char *entrypoint_name,
			   gl_shader_stage stage,
			   const VkSpecializationInfo *spec_info,
			   const VkPipelineCreateFlags flags,
			   const struct radv_pipeline_layout *layout,
			   bool use_aco);

void *
radv_alloc_shader_memory(struct radv_device *device,
			  struct radv_shader_variant *shader);

void
radv_destroy_shader_slabs(struct radv_device *device);

void
radv_create_shaders(struct radv_pipeline *pipeline,
		    struct radv_device *device,
		    struct radv_pipeline_cache *cache,
		    const struct radv_pipeline_key *key,
		    const VkPipelineShaderStageCreateInfo **pStages,
		    const VkPipelineCreateFlags flags,
		    VkPipelineCreationFeedbackEXT *pipeline_feedback,
		    VkPipelineCreationFeedbackEXT **stage_feedbacks);

struct radv_shader_variant *
radv_shader_variant_create(struct radv_device *device,
			   const struct radv_shader_binary *binary,
			   bool keep_shader_info);
struct radv_shader_variant *
radv_shader_variant_compile(struct radv_device *device,
			    struct radv_shader_module *module,
			    struct nir_shader *const *shaders,
			    int shader_count,
			    struct radv_pipeline_layout *layout,
			    const struct radv_shader_variant_key *key,
			    struct radv_shader_info *info,
			    bool keep_shader_info,
			    bool use_aco,
			    struct radv_shader_binary **binary_out);

struct radv_shader_variant *
radv_create_gs_copy_shader(struct radv_device *device, struct nir_shader *nir,
			   struct radv_shader_info *info,
			   struct radv_shader_binary **binary_out,
			   bool multiview,  bool keep_shader_info);

void
radv_shader_variant_destroy(struct radv_device *device,
			    struct radv_shader_variant *variant);


unsigned
radv_get_max_waves(struct radv_device *device,
                   struct radv_shader_variant *variant,
                   gl_shader_stage stage);

unsigned
radv_get_max_workgroup_size(enum chip_class chip_class,
                            gl_shader_stage stage,
                            const unsigned *sizes);

const char *
radv_get_shader_name(struct radv_shader_info *info,
		     gl_shader_stage stage);

void
radv_shader_dump_stats(struct radv_device *device,
		       struct radv_shader_variant *variant,
		       gl_shader_stage stage,
		       FILE *file);

bool
radv_can_dump_shader(struct radv_device *device,
		     struct radv_shader_module *module,
		     bool is_gs_copy_shader);

bool
radv_can_dump_shader_stats(struct radv_device *device,
			   struct radv_shader_module *module);

unsigned
shader_io_get_unique_index(gl_varying_slot slot);

void
radv_lower_fs_io(nir_shader *nir);

#endif
