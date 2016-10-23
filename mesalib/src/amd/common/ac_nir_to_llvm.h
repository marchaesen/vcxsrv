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
	enum radeon_family family;
	enum chip_class chip_class;
};

struct ac_shader_variant_info {
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
			uint8_t clip_dist_mask;
			uint8_t cull_dist_mask;
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
		} fs;
		struct {
			unsigned block_size[3];
		} cs;
	};
};

void ac_compile_nir_shader(LLVMTargetMachineRef tm,
                           struct ac_shader_binary *binary,
                           struct ac_shader_config *config,
                           struct ac_shader_variant_info *shader_info,
                           struct nir_shader *nir,
                           const struct ac_nir_compiler_options *options,
			   bool dump_shader);

/* SHADER ABI defines */

/* offset in dwords */
#define AC_USERDATA_DESCRIPTOR_SET_0 0
#define AC_USERDATA_DESCRIPTOR_SET_1 2
#define AC_USERDATA_DESCRIPTOR_SET_2 4
#define AC_USERDATA_DESCRIPTOR_SET_3 6
#define AC_USERDATA_PUSH_CONST_DYN 8

#define AC_USERDATA_VS_VERTEX_BUFFERS 10
#define AC_USERDATA_VS_BASE_VERTEX 12
#define AC_USERDATA_VS_START_INSTANCE 13

#define AC_USERDATA_PS_SAMPLE_POS 10

#define AC_USERDATA_CS_GRID_SIZE 10

#ifdef __cplusplus
extern "C"
#endif
void ac_add_attr_dereferenceable(LLVMValueRef val, uint64_t bytes);
