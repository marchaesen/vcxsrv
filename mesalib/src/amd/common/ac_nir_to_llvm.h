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

#pragma once

#include <stdbool.h>
#include "llvm-c/Core.h"
#include "llvm-c/TargetMachine.h"
#include "amd_family.h"

struct ac_shader_binary;
struct ac_shader_config;
struct nir_shader;
struct radv_pipeline_layout;


struct ac_vs_variant_key {
	uint32_t instance_rate_inputs;
	uint32_t as_es:1;
};

struct ac_fs_variant_key {
	uint32_t col_format;
	uint32_t is_int8;
};

union ac_shader_variant_key {
	struct ac_vs_variant_key vs;
	struct ac_fs_variant_key fs;
};

struct ac_nir_compiler_options {
	struct radv_pipeline_layout *layout;
	union ac_shader_variant_key key;
	bool unsafe_math;
	bool supports_spill;
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
	AC_UD_SHADER_START = 2,
	AC_UD_VS_VERTEX_BUFFERS = AC_UD_SHADER_START,
	AC_UD_VS_BASE_VERTEX_START_INSTANCE,
	AC_UD_VS_MAX_UD,
	AC_UD_PS_SAMPLE_POS = AC_UD_SHADER_START,
	AC_UD_PS_MAX_UD,
	AC_UD_CS_GRID_SIZE = AC_UD_SHADER_START,
	AC_UD_CS_MAX_UD,
	AC_UD_GS_VS_RING_STRIDE_ENTRIES = AC_UD_SHADER_START,
	AC_UD_GS_MAX_UD,
	AC_UD_MAX_UD = AC_UD_VS_MAX_UD,
};

#define AC_UD_MAX_SETS 4

struct ac_userdata_locations {
	struct ac_userdata_info descriptor_sets[AC_UD_MAX_SETS];
	struct ac_userdata_info shader_data[AC_UD_MAX_UD];
};

struct ac_shader_variant_info {
	struct ac_userdata_locations user_sgprs_locs;
	unsigned num_user_sgprs;
	unsigned num_input_sgprs;
	unsigned num_input_vgprs;
	union {
		struct {
			unsigned param_exports;
			unsigned pos_exports;
			unsigned vgpr_comp_cnt;
			uint32_t export_mask;
			bool writes_pointsize;
			bool writes_layer;
			bool writes_viewport_index;
			bool as_es;
			uint8_t clip_dist_mask;
			uint8_t cull_dist_mask;
			uint32_t esgs_itemsize;
			uint32_t prim_id_output;
			uint32_t layer_output;
		} vs;
		struct {
			unsigned num_interp;
			uint32_t input_mask;
			unsigned output_mask;
			uint32_t flat_shaded_mask;
			bool has_pcoord;
			bool can_discard;
			bool writes_z;
			bool writes_stencil;
			bool early_fragment_test;
			bool writes_memory;
			bool force_persample;
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
		} gs;
	};
};

void ac_compile_nir_shader(LLVMTargetMachineRef tm,
                           struct ac_shader_binary *binary,
                           struct ac_shader_config *config,
                           struct ac_shader_variant_info *shader_info,
                           struct nir_shader *nir,
                           const struct ac_nir_compiler_options *options,
			   bool dump_shader);

void ac_create_gs_copy_shader(LLVMTargetMachineRef tm,
			      struct nir_shader *geom_shader,
			      struct ac_shader_binary *binary,
			      struct ac_shader_config *config,
			      struct ac_shader_variant_info *shader_info,
			      const struct ac_nir_compiler_options *options,
			      bool dump_shader);

