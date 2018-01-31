/*
 * Copyright © 2016 Bas Nieuwenhuizen
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

#ifndef AC_NIR_TO_LLVM_H
#define AC_NIR_TO_LLVM_H

#include <stdbool.h>
#include "llvm-c/Core.h"
#include "llvm-c/TargetMachine.h"
#include "amd_family.h"
#include "../vulkan/radv_descriptor_set.h"
#include "ac_shader_info.h"
#include "compiler/shader_enums.h"
struct ac_shader_binary;
struct ac_shader_config;
struct nir_shader;
struct radv_pipeline_layout;

struct ac_llvm_context;
struct ac_shader_abi;

struct ac_vs_variant_key {
	uint32_t instance_rate_inputs;
	uint32_t as_es:1;
	uint32_t as_ls:1;
	uint32_t export_prim_id:1;
};

struct ac_tes_variant_key {
	uint32_t as_es:1;
	uint32_t export_prim_id:1;
};

struct ac_tcs_variant_key {
	struct ac_vs_variant_key vs_key;
	unsigned primitive_mode;
	unsigned input_vertices;
	uint32_t tes_reads_tess_factors:1;
};

struct ac_fs_variant_key {
	uint32_t col_format;
	uint8_t log2_ps_iter_samples;
	uint8_t log2_num_samples;
	uint32_t is_int8;
	uint32_t is_int10;
	uint32_t multisample : 1;
};

struct ac_shader_variant_key {
	union {
		struct ac_vs_variant_key vs;
		struct ac_fs_variant_key fs;
		struct ac_tes_variant_key tes;
		struct ac_tcs_variant_key tcs;
	};
	bool has_multiview_view_index;
};

struct ac_nir_compiler_options {
	struct radv_pipeline_layout *layout;
	struct ac_shader_variant_key key;
	bool unsafe_math;
	bool supports_spill;
	bool clamp_shadow_reference;
	bool dump_preoptir;
	enum radeon_family family;
	enum chip_class chip_class;
};

struct ac_userdata_info {
	int8_t sgpr_idx;
	uint8_t num_sgprs;
	bool indirect;
	uint32_t indirect_offset;
};

enum ac_ud_index {
	AC_UD_SCRATCH_RING_OFFSETS = 0,
	AC_UD_PUSH_CONSTANTS = 1,
	AC_UD_INDIRECT_DESCRIPTOR_SETS = 2,
	AC_UD_VIEW_INDEX = 3,
	AC_UD_SHADER_START = 4,
	AC_UD_VS_VERTEX_BUFFERS = AC_UD_SHADER_START,
	AC_UD_VS_BASE_VERTEX_START_INSTANCE,
	AC_UD_VS_LS_TCS_IN_LAYOUT,
	AC_UD_VS_MAX_UD,
	AC_UD_PS_SAMPLE_POS_OFFSET = AC_UD_SHADER_START,
	AC_UD_PS_MAX_UD,
	AC_UD_CS_GRID_SIZE = AC_UD_SHADER_START,
	AC_UD_CS_MAX_UD,
	AC_UD_GS_VS_RING_STRIDE_ENTRIES = AC_UD_VS_MAX_UD,
	AC_UD_GS_MAX_UD,
	AC_UD_TCS_OFFCHIP_LAYOUT = AC_UD_VS_MAX_UD,
	AC_UD_TCS_MAX_UD,
	AC_UD_TES_OFFCHIP_LAYOUT = AC_UD_SHADER_START,
	AC_UD_TES_MAX_UD,
	AC_UD_MAX_UD = AC_UD_TCS_MAX_UD,
};

/* Interpolation locations */
#define INTERP_CENTER 0
#define INTERP_CENTROID 1
#define INTERP_SAMPLE 2

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
#define AC_UD_MAX_SETS MAX_SETS

struct ac_userdata_locations {
	struct ac_userdata_info descriptor_sets[AC_UD_MAX_SETS];
	struct ac_userdata_info shader_data[AC_UD_MAX_UD];
};

struct ac_vs_output_info {
	uint8_t	vs_output_param_offset[VARYING_SLOT_MAX];
	uint8_t clip_dist_mask;
	uint8_t cull_dist_mask;
	uint8_t param_exports;
	bool writes_pointsize;
	bool writes_layer;
	bool writes_viewport_index;
	bool export_prim_id;
	uint32_t export_mask;
	unsigned pos_exports;
};

struct ac_es_output_info {
	uint32_t esgs_itemsize;
};

struct ac_shader_variant_info {
	struct ac_userdata_locations user_sgprs_locs;
	struct ac_shader_info info;
	unsigned num_user_sgprs;
	unsigned num_input_sgprs;
	unsigned num_input_vgprs;
	bool need_indirect_descriptor_sets;
	struct {
		struct {
			struct ac_vs_output_info outinfo;
			struct ac_es_output_info es_info;
			unsigned vgpr_comp_cnt;
			bool as_es;
			bool as_ls;
			uint64_t outputs_written;
		} vs;
		struct {
			unsigned num_interp;
			uint32_t input_mask;
			uint32_t flat_shaded_mask;
			bool has_pcoord;
			bool can_discard;
			bool writes_z;
			bool writes_stencil;
			bool writes_sample_mask;
			bool early_fragment_test;
			bool prim_id_input;
			bool layer_input;
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
			/* Which outputs are actually written */
			uint64_t outputs_written;
			/* Which patch outputs are actually written */
			uint32_t patch_outputs_written;

		} tcs;
		struct {
			struct ac_vs_output_info outinfo;
			struct ac_es_output_info es_info;
			bool as_es;
			unsigned primitive_mode;
			enum gl_tess_spacing spacing;
			bool ccw;
			bool point_mode;
		} tes;
	};
};

void ac_compile_nir_shader(LLVMTargetMachineRef tm,
                           struct ac_shader_binary *binary,
                           struct ac_shader_config *config,
                           struct ac_shader_variant_info *shader_info,
                           struct nir_shader *const *nir,
                           int nir_count,
                           const struct ac_nir_compiler_options *options,
			   bool dump_shader);

void ac_create_gs_copy_shader(LLVMTargetMachineRef tm,
			      struct nir_shader *geom_shader,
			      struct ac_shader_binary *binary,
			      struct ac_shader_config *config,
			      struct ac_shader_variant_info *shader_info,
			      const struct ac_nir_compiler_options *options,
			      bool dump_shader);

struct nir_to_llvm_context;
void ac_nir_translate(struct ac_llvm_context *ac, struct ac_shader_abi *abi,
		      struct nir_shader *nir, struct nir_to_llvm_context *nctx);

#endif /* AC_NIR_TO_LLVM_H */
