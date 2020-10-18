/*
 * Copyright © 2019 Valve Corporation.
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

#include "ac_shader_args.h"
#include "radv_constants.h"
#include "util/list.h"
#include "compiler/shader_enums.h"
#include "amd_family.h"

struct radv_shader_args {
	struct ac_shader_args ac;
	struct radv_shader_info *shader_info;
	const struct radv_nir_compiler_options *options;

	struct ac_arg descriptor_sets[MAX_SETS];
	struct ac_arg ring_offsets;
	struct ac_arg scratch_offset;

	struct ac_arg vertex_buffers;
	struct ac_arg rel_auto_id;
	struct ac_arg vs_prim_id;
	struct ac_arg es2gs_offset;

	struct ac_arg oc_lds;
	struct ac_arg merged_wave_info;
	struct ac_arg tess_factor_offset;
	struct ac_arg tes_rel_patch_id;
	struct ac_arg tes_u;
	struct ac_arg tes_v;

	/* HW GS */
	/* On gfx10:
	 *  - bits 0..11: ordered_wave_id
	 *  - bits 12..20: number of vertices in group
	 *  - bits 22..30: number of primitives in group
	 */
	struct ac_arg gs_tg_info;
	struct ac_arg gs2vs_offset;
	struct ac_arg gs_wave_id;
	struct ac_arg gs_vtx_offset[6];

	/* Streamout */
	struct ac_arg streamout_buffers;
	struct ac_arg streamout_write_idx;
	struct ac_arg streamout_config;
	struct ac_arg streamout_offset[4];

	/* NGG GS */
	struct ac_arg ngg_gs_state;

	bool is_gs_copy_shader;
	bool is_trap_handler_shader;
};

static inline struct radv_shader_args *
radv_shader_args_from_ac(struct ac_shader_args *args)
{
	struct radv_shader_args *radv_args = NULL;
	return (struct radv_shader_args *) container_of(args, radv_args, ac);
}

void radv_declare_shader_args(struct radv_shader_args *args,
			      gl_shader_stage stage,
			      bool has_previous_stage,
			      gl_shader_stage previous_stage);

