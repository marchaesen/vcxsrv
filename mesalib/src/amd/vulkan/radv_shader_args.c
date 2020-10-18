/*
 * Copyright © 2019 Valve Corporation.
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

#include "radv_private.h"
#include "radv_shader.h"
#include "radv_shader_args.h"

static void
set_loc(struct radv_userdata_info *ud_info, uint8_t *sgpr_idx,
	uint8_t num_sgprs)
{
	ud_info->sgpr_idx = *sgpr_idx;
	ud_info->num_sgprs = num_sgprs;
	*sgpr_idx += num_sgprs;
}

static void
set_loc_shader(struct radv_shader_args *args, int idx, uint8_t *sgpr_idx,
	       uint8_t num_sgprs)
{
	struct radv_userdata_info *ud_info =
		&args->shader_info->user_sgprs_locs.shader_data[idx];
	assert(ud_info);

	set_loc(ud_info, sgpr_idx, num_sgprs);
}

static void
set_loc_shader_ptr(struct radv_shader_args *args, int idx, uint8_t *sgpr_idx)
{
	bool use_32bit_pointers = idx != AC_UD_SCRATCH_RING_OFFSETS;

	set_loc_shader(args, idx, sgpr_idx, use_32bit_pointers ? 1 : 2);
}

static void
set_loc_desc(struct radv_shader_args *args, int idx, uint8_t *sgpr_idx)
{
	struct radv_userdata_locations *locs =
		&args->shader_info->user_sgprs_locs;
	struct radv_userdata_info *ud_info = &locs->descriptor_sets[idx];
	assert(ud_info);

	set_loc(ud_info, sgpr_idx, 1);

	locs->descriptor_sets_enabled |= 1u << idx;
}

struct user_sgpr_info {
	bool indirect_all_descriptor_sets;
	uint8_t remaining_sgprs;
};

static bool needs_view_index_sgpr(struct radv_shader_args *args,
				  gl_shader_stage stage)
{
	switch (stage) {
	case MESA_SHADER_VERTEX:
		if (args->shader_info->needs_multiview_view_index ||
		    (!args->options->key.vs_common_out.as_es && !args->options->key.vs_common_out.as_ls && args->options->key.has_multiview_view_index))
			return true;
		break;
	case MESA_SHADER_TESS_EVAL:
		if (args->shader_info->needs_multiview_view_index || (!args->options->key.vs_common_out.as_es && args->options->key.has_multiview_view_index))
			return true;
		break;
	case MESA_SHADER_GEOMETRY:
	case MESA_SHADER_TESS_CTRL:
		if (args->shader_info->needs_multiview_view_index)
			return true;
		break;
	default:
		break;
	}
	return false;
}

static uint8_t
count_vs_user_sgprs(struct radv_shader_args *args)
{
	uint8_t count = 0;

	if (args->shader_info->vs.has_vertex_buffers)
		count++;
	count += args->shader_info->vs.needs_draw_id ? 3 : 2;

	return count;
}

static void allocate_inline_push_consts(struct radv_shader_args *args,
					struct user_sgpr_info *user_sgpr_info)
{
	uint8_t remaining_sgprs = user_sgpr_info->remaining_sgprs;

	/* Only supported if shaders use push constants. */
	if (args->shader_info->min_push_constant_used == UINT8_MAX)
		return;

	/* Only supported if shaders don't have indirect push constants. */
	if (args->shader_info->has_indirect_push_constants)
		return;

	/* Only supported for 32-bit push constants. */
	if (!args->shader_info->has_only_32bit_push_constants)
		return;

	uint8_t num_push_consts =
		(args->shader_info->max_push_constant_used -
		 args->shader_info->min_push_constant_used) / 4;

	/* Check if the number of user SGPRs is large enough. */
	if (num_push_consts < remaining_sgprs) {
		args->shader_info->num_inline_push_consts = num_push_consts;
	} else {
		args->shader_info->num_inline_push_consts = remaining_sgprs;
	}

	/* Clamp to the maximum number of allowed inlined push constants. */
	if (args->shader_info->num_inline_push_consts > AC_MAX_INLINE_PUSH_CONSTS)
		args->shader_info->num_inline_push_consts = AC_MAX_INLINE_PUSH_CONSTS;

	if (args->shader_info->num_inline_push_consts == num_push_consts &&
	    !args->shader_info->loads_dynamic_offsets) {
		/* Disable the default push constants path if all constants are
		 * inlined and if shaders don't use dynamic descriptors.
		 */
		args->shader_info->loads_push_constants = false;
	}

	args->shader_info->base_inline_push_consts =
		args->shader_info->min_push_constant_used / 4;
}

static void allocate_user_sgprs(struct radv_shader_args *args,
				gl_shader_stage stage,
				bool has_previous_stage,
				gl_shader_stage previous_stage,
				bool needs_view_index,
				struct user_sgpr_info *user_sgpr_info)
{
	uint8_t user_sgpr_count = 0;

	memset(user_sgpr_info, 0, sizeof(struct user_sgpr_info));

	/* 2 user sgprs will always be allocated for scratch/rings */
	user_sgpr_count += 2;

	switch (stage) {
	case MESA_SHADER_COMPUTE:
		if (args->shader_info->cs.uses_grid_size)
			user_sgpr_count += 3;
		break;
	case MESA_SHADER_FRAGMENT:
		user_sgpr_count += args->shader_info->ps.needs_sample_positions;
		break;
	case MESA_SHADER_VERTEX:
		if (!args->is_gs_copy_shader)
			user_sgpr_count += count_vs_user_sgprs(args);
		break;
	case MESA_SHADER_TESS_CTRL:
		if (has_previous_stage) {
			if (previous_stage == MESA_SHADER_VERTEX)
				user_sgpr_count += count_vs_user_sgprs(args);
		}
		break;
	case MESA_SHADER_TESS_EVAL:
		break;
	case MESA_SHADER_GEOMETRY:
		if (has_previous_stage) {
			if (previous_stage == MESA_SHADER_VERTEX) {
				user_sgpr_count += count_vs_user_sgprs(args);
			}
		}
		break;
	default:
		break;
	}

	if (needs_view_index)
		user_sgpr_count++;

	if (args->shader_info->loads_push_constants)
		user_sgpr_count++;

	if (args->shader_info->so.num_outputs)
		user_sgpr_count++;

	uint32_t available_sgprs = args->options->chip_class >= GFX9 && stage != MESA_SHADER_COMPUTE ? 32 : 16;
	uint32_t remaining_sgprs = available_sgprs - user_sgpr_count;
	uint32_t num_desc_set =
		util_bitcount(args->shader_info->desc_set_used_mask);

	if (remaining_sgprs < num_desc_set) {
		user_sgpr_info->indirect_all_descriptor_sets = true;
		user_sgpr_info->remaining_sgprs = remaining_sgprs - 1;
	} else {
		user_sgpr_info->remaining_sgprs = remaining_sgprs - num_desc_set;
	}

	allocate_inline_push_consts(args, user_sgpr_info);
}

static void
declare_global_input_sgprs(struct radv_shader_args *args,
			   const struct user_sgpr_info *user_sgpr_info)
{
	/* 1 for each descriptor set */
	if (!user_sgpr_info->indirect_all_descriptor_sets) {
		uint32_t mask = args->shader_info->desc_set_used_mask;

		while (mask) {
			int i = u_bit_scan(&mask);

			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_CONST_PTR,
				   &args->descriptor_sets[i]);
		}
	} else {
		ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_CONST_PTR_PTR,
			   &args->descriptor_sets[0]);
	}

	if (args->shader_info->loads_push_constants) {
		/* 1 for push constants and dynamic descriptors */
		ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_CONST_PTR,
			   &args->ac.push_constants);
	}

	for (unsigned i = 0; i < args->shader_info->num_inline_push_consts; i++) {
		ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
			   &args->ac.inline_push_consts[i]);
	}
	args->ac.num_inline_push_consts = args->shader_info->num_inline_push_consts;
	args->ac.base_inline_push_consts = args->shader_info->base_inline_push_consts;

	if (args->shader_info->so.num_outputs) {
		ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_CONST_DESC_PTR,
			   &args->streamout_buffers);
	}
}

static void
declare_vs_specific_input_sgprs(struct radv_shader_args *args,
				gl_shader_stage stage,
				bool has_previous_stage,
				gl_shader_stage previous_stage)
{
	if (!args->is_gs_copy_shader &&
	    (stage == MESA_SHADER_VERTEX ||
	     (has_previous_stage && previous_stage == MESA_SHADER_VERTEX))) {
		if (args->shader_info->vs.has_vertex_buffers) {
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_CONST_DESC_PTR,
				   &args->vertex_buffers);
		}
		ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.base_vertex);
		ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.start_instance);
		if (args->shader_info->vs.needs_draw_id) {
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.draw_id);
		}
	}
}

static void
declare_vs_input_vgprs(struct radv_shader_args *args)
{
	ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.vertex_id);
	if (!args->is_gs_copy_shader) {
		if (args->options->key.vs_common_out.as_ls) {
			ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->rel_auto_id);
			if (args->options->chip_class >= GFX10) {
				ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user vgpr */
				ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.instance_id);
			} else {
				ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.instance_id);
				ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* unused */
			}
		} else {
			if (args->options->chip_class >= GFX10) {
				if (args->options->key.vs_common_out.as_ngg) {
					ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user vgpr */
					ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* user vgpr */
					ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.instance_id);
				} else {
					ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* unused */
					ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->vs_prim_id);
					ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.instance_id);
				}
			} else {
				ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.instance_id);
				ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->vs_prim_id);
				ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL); /* unused */
			}
		}
	}
}

static void
declare_streamout_sgprs(struct radv_shader_args *args, gl_shader_stage stage)
{
	int i;

	if (args->options->use_ngg_streamout) {
		if (stage == MESA_SHADER_TESS_EVAL)
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
		return;
	}

	/* Streamout SGPRs. */
	if (args->shader_info->so.num_outputs) {
		assert(stage == MESA_SHADER_VERTEX ||
		       stage == MESA_SHADER_TESS_EVAL);

		ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->streamout_config);
		ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->streamout_write_idx);
	} else if (stage == MESA_SHADER_TESS_EVAL) {
		ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
	}

	/* A streamout buffer offset is loaded if the stride is non-zero. */
	for (i = 0; i < 4; i++) {
		if (!args->shader_info->so.strides[i])
			continue;

		ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->streamout_offset[i]);
	}
}

static void
declare_tes_input_vgprs(struct radv_shader_args *args)
{
	ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &args->tes_u);
	ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &args->tes_v);
	ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->tes_rel_patch_id);
	ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.tes_patch_id);
}

static void
set_global_input_locs(struct radv_shader_args *args,
		      const struct user_sgpr_info *user_sgpr_info,
		      uint8_t *user_sgpr_idx)
{
	uint32_t mask = args->shader_info->desc_set_used_mask;

	if (!user_sgpr_info->indirect_all_descriptor_sets) {
		while (mask) {
			int i = u_bit_scan(&mask);

			set_loc_desc(args, i, user_sgpr_idx);
		}
	} else {
		set_loc_shader_ptr(args, AC_UD_INDIRECT_DESCRIPTOR_SETS,
				   user_sgpr_idx);

		args->shader_info->need_indirect_descriptor_sets = true;
	}

	if (args->shader_info->loads_push_constants) {
		set_loc_shader_ptr(args, AC_UD_PUSH_CONSTANTS, user_sgpr_idx);
	}

	if (args->shader_info->num_inline_push_consts) {
		set_loc_shader(args, AC_UD_INLINE_PUSH_CONSTANTS, user_sgpr_idx,
			       args->shader_info->num_inline_push_consts);
	}

	if (args->streamout_buffers.used) {
		set_loc_shader_ptr(args, AC_UD_STREAMOUT_BUFFERS,
				   user_sgpr_idx);
	}
}

static void
set_vs_specific_input_locs(struct radv_shader_args *args,
			   gl_shader_stage stage, bool has_previous_stage,
			   gl_shader_stage previous_stage,
			   uint8_t *user_sgpr_idx)
{
	if (!args->is_gs_copy_shader &&
	    (stage == MESA_SHADER_VERTEX ||
	     (has_previous_stage && previous_stage == MESA_SHADER_VERTEX))) {
		if (args->shader_info->vs.has_vertex_buffers) {
			set_loc_shader_ptr(args, AC_UD_VS_VERTEX_BUFFERS,
					   user_sgpr_idx);
		}

		unsigned vs_num = 2;
		if (args->shader_info->vs.needs_draw_id)
			vs_num++;

		set_loc_shader(args, AC_UD_VS_BASE_VERTEX_START_INSTANCE,
			       user_sgpr_idx, vs_num);
	}
}

/* Returns whether the stage is a stage that can be directly before the GS */
static bool is_pre_gs_stage(gl_shader_stage stage)
{
	return stage == MESA_SHADER_VERTEX || stage == MESA_SHADER_TESS_EVAL;
}

void
radv_declare_shader_args(struct radv_shader_args *args,
			 gl_shader_stage stage,
			 bool has_previous_stage,
			 gl_shader_stage previous_stage)
{
	struct user_sgpr_info user_sgpr_info;
	bool needs_view_index = needs_view_index_sgpr(args, stage);

	if (args->options->chip_class >= GFX10) {
		if (is_pre_gs_stage(stage) && args->options->key.vs_common_out.as_ngg) {
			/* On GFX10, VS is merged into GS for NGG. */
			previous_stage = stage;
			stage = MESA_SHADER_GEOMETRY;
			has_previous_stage = true;
		}
	}

	for (int i = 0; i < MAX_SETS; i++)
		args->shader_info->user_sgprs_locs.descriptor_sets[i].sgpr_idx = -1;
	for (int i = 0; i < AC_UD_MAX_UD; i++)
		args->shader_info->user_sgprs_locs.shader_data[i].sgpr_idx = -1;


	allocate_user_sgprs(args, stage, has_previous_stage,
			    previous_stage, needs_view_index, &user_sgpr_info);

	if (args->options->explicit_scratch_args) {
		ac_add_arg(&args->ac, AC_ARG_SGPR, 2, AC_ARG_CONST_DESC_PTR,
			   &args->ring_offsets);
	}

	switch (stage) {
	case MESA_SHADER_COMPUTE:
		declare_global_input_sgprs(args, &user_sgpr_info);

		if (args->shader_info->cs.uses_grid_size) {
			ac_add_arg(&args->ac, AC_ARG_SGPR, 3, AC_ARG_INT,
				   &args->ac.num_work_groups);
		}

		for (int i = 0; i < 3; i++) {
			if (args->shader_info->cs.uses_block_id[i]) {
				ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
					   &args->ac.workgroup_ids[i]);
			}
		}

		if (args->shader_info->cs.uses_local_invocation_idx) {
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
				   &args->ac.tg_size);
		}

		if (args->options->explicit_scratch_args) {
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
				   &args->scratch_offset);
		}

		ac_add_arg(&args->ac, AC_ARG_VGPR, 3, AC_ARG_INT,
			   &args->ac.local_invocation_ids);
		break;
	case MESA_SHADER_VERTEX:
		declare_global_input_sgprs(args, &user_sgpr_info);

		declare_vs_specific_input_sgprs(args, stage, has_previous_stage,
						previous_stage);

		if (needs_view_index) {
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
				   &args->ac.view_index);
		}

		if (args->options->key.vs_common_out.as_es) {
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
				&args->es2gs_offset);
		} else if (args->options->key.vs_common_out.as_ls) {
			/* no extra parameters */
		} else {
			declare_streamout_sgprs(args, stage);
		}

		if (args->options->explicit_scratch_args) {
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
				   &args->scratch_offset);
		}

		declare_vs_input_vgprs(args);
		break;
	case MESA_SHADER_TESS_CTRL:
		if (has_previous_stage) {
			// First 6 system regs
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->oc_lds);
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
				   &args->merged_wave_info);
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
				   &args->tess_factor_offset);

			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->scratch_offset);
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL); // unknown
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL); // unknown

			declare_global_input_sgprs(args, &user_sgpr_info);

			declare_vs_specific_input_sgprs(args, stage,
							has_previous_stage,
							previous_stage);

			if (needs_view_index) {
				ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
					   &args->ac.view_index);
			}

			ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT,
				  &args->ac.tcs_patch_id);
			ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT,
				   &args->ac.tcs_rel_ids);

			declare_vs_input_vgprs(args);
		} else {
			declare_global_input_sgprs(args, &user_sgpr_info);

			if (needs_view_index) {
				ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
					   &args->ac.view_index);
			}

			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->oc_lds);
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
				   &args->tess_factor_offset);
			if (args->options->explicit_scratch_args) {
				ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
					   &args->scratch_offset);
			}
			ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT,
				   &args->ac.tcs_patch_id);
			ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT,
				   &args->ac.tcs_rel_ids);
		}
		break;
	case MESA_SHADER_TESS_EVAL:
		declare_global_input_sgprs(args, &user_sgpr_info);

		if (needs_view_index)
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
				&args->ac.view_index);

		if (args->options->key.vs_common_out.as_es) {
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->oc_lds);
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL);
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
				&args->es2gs_offset);
		} else {
			declare_streamout_sgprs(args, stage);
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->oc_lds);
		}
		if (args->options->explicit_scratch_args) {
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
				   &args->scratch_offset);
		}
		declare_tes_input_vgprs(args);
		break;
	case MESA_SHADER_GEOMETRY:
		if (has_previous_stage) {
			// First 6 system regs
			if (args->options->key.vs_common_out.as_ngg) {
				ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
					&args->gs_tg_info);
			} else {
				ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
					&args->gs2vs_offset);
			}

			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
				   &args->merged_wave_info);
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->oc_lds);

			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->scratch_offset);
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL); // unknown
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, NULL); // unknown

			declare_global_input_sgprs(args, &user_sgpr_info);

			if (previous_stage != MESA_SHADER_TESS_EVAL) {
				declare_vs_specific_input_sgprs(args, stage,
								has_previous_stage,
								previous_stage);
			}

			if (needs_view_index) {
				ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
					   &args->ac.view_index);
			}

			if (args->options->key.vs_common_out.as_ngg) {
				ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
					   &args->ngg_gs_state);
			}

			ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT,
				   &args->gs_vtx_offset[0]);
			ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT,
				   &args->gs_vtx_offset[2]);
			ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT,
				   &args->ac.gs_prim_id);
			ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT,
				   &args->ac.gs_invocation_id);
			ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT,
				   &args->gs_vtx_offset[4]);

			if (previous_stage == MESA_SHADER_VERTEX) {
				declare_vs_input_vgprs(args);
			} else {
				declare_tes_input_vgprs(args);
			}
		} else {
			declare_global_input_sgprs(args, &user_sgpr_info);

			if (needs_view_index) {
				ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
					   &args->ac.view_index);
			}

			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->gs2vs_offset);
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->gs_wave_id);
			if (args->options->explicit_scratch_args) {
				ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
					   &args->scratch_offset);
			}
			ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT,
				   &args->gs_vtx_offset[0]);
			ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT,
				   &args->gs_vtx_offset[1]);
			ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT,
				   &args->ac.gs_prim_id);
			ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT,
				   &args->gs_vtx_offset[2]);
			ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT,
				   &args->gs_vtx_offset[3]);
			ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT,
				   &args->gs_vtx_offset[4]);
			ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT,
				   &args->gs_vtx_offset[5]);
			ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT,
				   &args->ac.gs_invocation_id);
		}
		break;
	case MESA_SHADER_FRAGMENT:
		declare_global_input_sgprs(args, &user_sgpr_info);

		ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT, &args->ac.prim_mask);
		if (args->options->explicit_scratch_args) {
			ac_add_arg(&args->ac, AC_ARG_SGPR, 1, AC_ARG_INT,
				   &args->scratch_offset);
		}
		ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_INT, &args->ac.persp_sample);
		ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_INT, &args->ac.persp_center);
		ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_INT, &args->ac.persp_centroid);
		ac_add_arg(&args->ac, AC_ARG_VGPR, 3, AC_ARG_INT, &args->ac.pull_model);
		ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_INT, &args->ac.linear_sample);
		ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_INT, &args->ac.linear_center);
		ac_add_arg(&args->ac, AC_ARG_VGPR, 2, AC_ARG_INT, &args->ac.linear_centroid);
		ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, NULL);  /* line stipple tex */
		ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &args->ac.frag_pos[0]);
		ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &args->ac.frag_pos[1]);
		ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &args->ac.frag_pos[2]);
		ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_FLOAT, &args->ac.frag_pos[3]);
		ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.front_face);
		ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.ancillary);
		ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, &args->ac.sample_coverage);
		ac_add_arg(&args->ac, AC_ARG_VGPR, 1, AC_ARG_INT, NULL);  /* fixed pt */
		break;
	default:
		unreachable("Shader stage not implemented");
	}

	args->shader_info->num_input_vgprs = 0;
	args->shader_info->num_input_sgprs = 2;
	args->shader_info->num_input_sgprs += args->ac.num_sgprs_used;
	args->shader_info->num_input_vgprs = args->ac.num_vgprs_used;

	uint8_t user_sgpr_idx = 0;

	set_loc_shader_ptr(args, AC_UD_SCRATCH_RING_OFFSETS,
			   &user_sgpr_idx);

	/* For merged shaders the user SGPRs start at 8, with 8 system SGPRs in front (including
	 * the rw_buffers at s0/s1. With user SGPR0 = s8, lets restart the count from 0 */
	if (has_previous_stage)
		user_sgpr_idx = 0;

	set_global_input_locs(args, &user_sgpr_info, &user_sgpr_idx);

	switch (stage) {
	case MESA_SHADER_COMPUTE:
		if (args->shader_info->cs.uses_grid_size) {
			set_loc_shader(args, AC_UD_CS_GRID_SIZE,
				       &user_sgpr_idx, 3);
		}
		break;
	case MESA_SHADER_VERTEX:
		set_vs_specific_input_locs(args, stage, has_previous_stage,
					   previous_stage, &user_sgpr_idx);
		if (args->ac.view_index.used)
			set_loc_shader(args, AC_UD_VIEW_INDEX, &user_sgpr_idx, 1);
		break;
	case MESA_SHADER_TESS_CTRL:
		set_vs_specific_input_locs(args, stage, has_previous_stage,
					   previous_stage, &user_sgpr_idx);
		if (args->ac.view_index.used)
			set_loc_shader(args, AC_UD_VIEW_INDEX, &user_sgpr_idx, 1);
		break;
	case MESA_SHADER_TESS_EVAL:
		if (args->ac.view_index.used)
			set_loc_shader(args, AC_UD_VIEW_INDEX, &user_sgpr_idx, 1);
		break;
	case MESA_SHADER_GEOMETRY:
		if (has_previous_stage) {
			if (previous_stage == MESA_SHADER_VERTEX)
				set_vs_specific_input_locs(args, stage,
							   has_previous_stage,
							   previous_stage,
							   &user_sgpr_idx);
		}
		if (args->ac.view_index.used)
			set_loc_shader(args, AC_UD_VIEW_INDEX, &user_sgpr_idx, 1);

		if (args->ngg_gs_state.used)
			set_loc_shader(args, AC_UD_NGG_GS_STATE, &user_sgpr_idx, 1);
		break;
	case MESA_SHADER_FRAGMENT:
		break;
	default:
		unreachable("Shader stage not implemented");
	}

	args->shader_info->num_user_sgprs = user_sgpr_idx;
}

