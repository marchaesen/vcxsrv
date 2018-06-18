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

#include "radv_debug.h"
#include "radv_private.h"

#include "nir/nir.h"

/* descriptor index into scratch ring offsets */
#define RING_SCRATCH 0
#define RING_ESGS_VS 1
#define RING_ESGS_GS 2
#define RING_GSVS_VS 3
#define RING_GSVS_GS 4
#define RING_HS_TESS_FACTOR 5
#define RING_HS_TESS_OFFCHIP 6
#define RING_PS_SAMPLE_POSITIONS 7

// Match MAX_SETS from radv_descriptor_set.h
#define RADV_UD_MAX_SETS MAX_SETS

#define RADV_NUM_PHYSICAL_VGPRS 256

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

struct radv_vs_variant_key {
	uint32_t instance_rate_inputs;
	uint32_t instance_rate_divisors[MAX_VERTEX_ATTRIBS];

	/* For 2_10_10_10 formats the alpha is handled as unsigned by pre-vega HW.
	 * so we may need to fix it up. */
	uint64_t alpha_adjust;

	uint32_t as_es:1;
	uint32_t as_ls:1;
	uint32_t export_prim_id:1;
	uint32_t export_layer_id:1;
};

struct radv_tes_variant_key {
	uint32_t as_es:1;
	uint32_t export_prim_id:1;
	uint32_t export_layer_id:1;
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
	uint8_t log2_num_samples;
	uint32_t is_int8;
	uint32_t is_int10;
};

struct radv_shader_variant_key {
	union {
		struct radv_vs_variant_key vs;
		struct radv_fs_variant_key fs;
		struct radv_tes_variant_key tes;
		struct radv_tcs_variant_key tcs;
	};
	bool has_multiview_view_index;
};

struct radv_nir_compiler_options {
	struct radv_pipeline_layout *layout;
	struct radv_shader_variant_key key;
	bool unsafe_math;
	bool supports_spill;
	bool clamp_shadow_reference;
	bool dump_shader;
	bool dump_preoptir;
	bool record_llvm_ir;
	bool check_ir;
	enum radeon_family family;
	enum chip_class chip_class;
	uint32_t tess_offchip_block_dw_size;
	uint32_t address32_hi;
};

enum radv_ud_index {
	AC_UD_SCRATCH_RING_OFFSETS = 0,
	AC_UD_PUSH_CONSTANTS = 1,
	AC_UD_INDIRECT_DESCRIPTOR_SETS = 2,
	AC_UD_VIEW_INDEX = 3,
	AC_UD_SHADER_START = 4,
	AC_UD_VS_VERTEX_BUFFERS = AC_UD_SHADER_START,
	AC_UD_VS_BASE_VERTEX_START_INSTANCE,
	AC_UD_VS_MAX_UD,
	AC_UD_PS_SAMPLE_POS_OFFSET = AC_UD_SHADER_START,
	AC_UD_PS_MAX_UD,
	AC_UD_CS_GRID_SIZE = AC_UD_SHADER_START,
	AC_UD_CS_MAX_UD,
	AC_UD_GS_MAX_UD,
	AC_UD_TCS_MAX_UD,
	AC_UD_TES_MAX_UD,
	AC_UD_MAX_UD = AC_UD_TCS_MAX_UD,
};
struct radv_shader_info {
	bool loads_push_constants;
	uint32_t desc_set_used_mask;
	bool needs_multiview_view_index;
	bool uses_invocation_id;
	bool uses_prim_id;
	struct {
		uint64_t ls_outputs_written;
		uint8_t input_usage_mask[VERT_ATTRIB_MAX];
		uint8_t output_usage_mask[VARYING_SLOT_VAR31 + 1];
		bool has_vertex_buffers; /* needs vertex buffers and base/start */
		bool needs_draw_id;
		bool needs_instance_id;
	} vs;
	struct {
		uint8_t output_usage_mask[VARYING_SLOT_VAR31 + 1];
	} gs;
	struct {
		uint8_t output_usage_mask[VARYING_SLOT_VAR31 + 1];
	} tes;
	struct {
		bool force_persample;
		bool needs_sample_positions;
		bool uses_input_attachments;
		bool writes_memory;
		bool writes_z;
		bool writes_stencil;
		bool writes_sample_mask;
		bool has_pcoord;
		bool prim_id_input;
		bool layer_input;
	} ps;
	struct {
		bool uses_grid_size;
		bool uses_block_id[3];
		bool uses_thread_id[3];
		bool uses_local_invocation_idx;
	} cs;
	struct {
		uint64_t outputs_written;
		uint64_t patch_outputs_written;
	} tcs;
};

struct radv_userdata_info {
	int8_t sgpr_idx;
	uint8_t num_sgprs;
	bool indirect;
	uint32_t indirect_offset;
};

struct radv_userdata_locations {
	struct radv_userdata_info descriptor_sets[RADV_UD_MAX_SETS];
	struct radv_userdata_info shader_data[AC_UD_MAX_UD];
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

struct radv_shader_variant_info {
	struct radv_userdata_locations user_sgprs_locs;
	struct radv_shader_info info;
	unsigned num_user_sgprs;
	unsigned num_input_sgprs;
	unsigned num_input_vgprs;
	unsigned private_mem_vgprs;
	bool need_indirect_descriptor_sets;
	struct {
		struct {
			struct radv_vs_output_info outinfo;
			struct radv_es_output_info es_info;
			unsigned vgpr_comp_cnt;
			bool as_es;
			bool as_ls;
		} vs;
		struct {
			unsigned num_interp;
			uint32_t input_mask;
			uint32_t flat_shaded_mask;
			bool can_discard;
			bool early_fragment_test;
		} fs;
		struct {
			unsigned block_size[3];
		} cs;
		struct {
			unsigned vertices_in;
			unsigned vertices_out;
			unsigned output_prim;
			unsigned invocations;
			unsigned gsvs_vertex_size;
			unsigned max_gsvs_emit_size;
			unsigned es_type; /* GFX9: VS or TES */
		} gs;
		struct {
			unsigned tcs_vertices_out;
			uint32_t num_patches;
			uint32_t lds_size;
		} tcs;
		struct {
			struct radv_vs_output_info outinfo;
			struct radv_es_output_info es_info;
			bool as_es;
			unsigned primitive_mode;
			enum gl_tess_spacing spacing;
			bool ccw;
			bool point_mode;
		} tes;
	};
};

struct radv_shader_variant {
	uint32_t ref_count;

	struct radeon_winsys_bo *bo;
	uint64_t bo_offset;
	struct ac_shader_config config;
	uint32_t code_size;
	struct radv_shader_variant_info info;
	unsigned rsrc1;
	unsigned rsrc2;

	/* debug only */
	uint32_t *spirv;
	uint32_t spirv_size;
	struct nir_shader *nir;
	char *disasm_string;
	char *llvm_ir_string;

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
radv_optimize_nir(struct nir_shader *shader, bool optimize_conservatively);

nir_shader *
radv_shader_compile_to_nir(struct radv_device *device,
			   struct radv_shader_module *module,
			   const char *entrypoint_name,
			   gl_shader_stage stage,
			   const VkSpecializationInfo *spec_info,
			   const VkPipelineCreateFlags flags);

void *
radv_alloc_shader_memory(struct radv_device *device,
			  struct radv_shader_variant *shader);

void
radv_destroy_shader_slabs(struct radv_device *device);

struct radv_shader_variant *
radv_shader_variant_create(struct radv_device *device,
			   struct radv_shader_module *module,
			   struct nir_shader *const *shaders,
			   int shader_count,
			   struct radv_pipeline_layout *layout,
			   const struct radv_shader_variant_key *key,
			   void **code_out,
			   unsigned *code_size_out);

struct radv_shader_variant *
radv_create_gs_copy_shader(struct radv_device *device, struct nir_shader *nir,
			   void **code_out, unsigned *code_size_out,
			   bool multiview);

void
radv_shader_variant_destroy(struct radv_device *device,
			    struct radv_shader_variant *variant);

const char *
radv_get_shader_name(struct radv_shader_variant *var, gl_shader_stage stage);

void
radv_shader_dump_stats(struct radv_device *device,
		       struct radv_shader_variant *variant,
		       gl_shader_stage stage,
		       FILE *file);

static inline bool
radv_can_dump_shader(struct radv_device *device,
		     struct radv_shader_module *module,
		     bool is_gs_copy_shader)
{
	if (!(device->instance->debug_flags & RADV_DEBUG_DUMP_SHADERS))
		return false;

	/* Only dump non-meta shaders, useful for debugging purposes. */
	return (module && !module->nir) || is_gs_copy_shader;
}

static inline bool
radv_can_dump_shader_stats(struct radv_device *device,
			   struct radv_shader_module *module)
{
	/* Only dump non-meta shader stats. */
	return device->instance->debug_flags & RADV_DEBUG_DUMP_SHADER_STATS &&
	       module && !module->nir;
}

static inline unsigned shader_io_get_unique_index(gl_varying_slot slot)
{
	/* handle patch indices separate */
	if (slot == VARYING_SLOT_TESS_LEVEL_OUTER)
		return 0;
	if (slot == VARYING_SLOT_TESS_LEVEL_INNER)
		return 1;
	if (slot >= VARYING_SLOT_PATCH0 && slot <= VARYING_SLOT_TESS_MAX)
		return 2 + (slot - VARYING_SLOT_PATCH0);
	if (slot == VARYING_SLOT_POS)
		return 0;
	if (slot == VARYING_SLOT_PSIZ)
		return 1;
	if (slot == VARYING_SLOT_CLIP_DIST0)
		return 2;
	/* 3 is reserved for clip dist as well */
	if (slot >= VARYING_SLOT_VAR0 && slot <= VARYING_SLOT_VAR31)
		return 4 + (slot - VARYING_SLOT_VAR0);
	unreachable("illegal slot in get unique index\n");
}

static inline uint32_t
radv_get_num_physical_sgprs(struct radv_physical_device *physical_device)
{
	return physical_device->rad_info.chip_class >= VI ? 800 : 512;
}

#endif
