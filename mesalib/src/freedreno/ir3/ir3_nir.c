/*
 * Copyright (C) 2015 Rob Clark <robclark@freedesktop.org>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */


#include "util/debug.h"
#include "util/u_math.h"

#include "ir3_nir.h"
#include "ir3_compiler.h"
#include "ir3_shader.h"

static const nir_shader_compiler_options options = {
		.lower_fpow = true,
		.lower_scmp = true,
		.lower_flrp16 = true,
		.lower_flrp32 = true,
		.lower_flrp64 = true,
		.lower_ffract = true,
		.lower_fmod = true,
		.lower_fdiv = true,
		.lower_isign = true,
		.lower_ldexp = true,
		.lower_uadd_carry = true,
		.lower_usub_borrow = true,
		.lower_mul_high = true,
		.lower_mul_2x32_64 = true,
		.fuse_ffma = true,
		.vertex_id_zero_based = true,
		.lower_extract_byte = true,
		.lower_extract_word = true,
		.lower_all_io_to_elements = true,
		.lower_helper_invocation = true,
		.lower_bitfield_insert_to_shifts = true,
		.lower_bitfield_extract_to_shifts = true,
		.lower_pack_half_2x16 = true,
		.lower_pack_snorm_4x8 = true,
		.lower_pack_snorm_2x16 = true,
		.lower_pack_unorm_4x8 = true,
		.lower_pack_unorm_2x16 = true,
		.lower_unpack_half_2x16 = true,
		.lower_unpack_snorm_4x8 = true,
		.lower_unpack_snorm_2x16 = true,
		.lower_unpack_unorm_4x8 = true,
		.lower_unpack_unorm_2x16 = true,
		.lower_pack_split = true,
		.use_interpolated_input_intrinsics = true,
		.lower_rotate = true,
		.lower_to_scalar = true,
		.has_imul24 = true,
		.lower_wpos_pntc = true,
};

/* we don't want to lower vertex_id to _zero_based on newer gpus: */
static const nir_shader_compiler_options options_a6xx = {
		.lower_fpow = true,
		.lower_scmp = true,
		.lower_flrp16 = true,
		.lower_flrp32 = true,
		.lower_flrp64 = true,
		.lower_ffract = true,
		.lower_fmod = true,
		.lower_fdiv = true,
		.lower_isign = true,
		.lower_ldexp = true,
		.lower_uadd_carry = true,
		.lower_usub_borrow = true,
		.lower_mul_high = true,
		.lower_mul_2x32_64 = true,
		.fuse_ffma = true,
		.vertex_id_zero_based = false,
		.lower_extract_byte = true,
		.lower_extract_word = true,
		.lower_all_io_to_elements = true,
		.lower_helper_invocation = true,
		.lower_bitfield_insert_to_shifts = true,
		.lower_bitfield_extract_to_shifts = true,
		.lower_pack_half_2x16 = true,
		.lower_pack_snorm_4x8 = true,
		.lower_pack_snorm_2x16 = true,
		.lower_pack_unorm_4x8 = true,
		.lower_pack_unorm_2x16 = true,
		.lower_unpack_half_2x16 = true,
		.lower_unpack_snorm_4x8 = true,
		.lower_unpack_snorm_2x16 = true,
		.lower_unpack_unorm_4x8 = true,
		.lower_unpack_unorm_2x16 = true,
		.lower_pack_split = true,
		.use_interpolated_input_intrinsics = true,
		.lower_rotate = true,
		.vectorize_io = true,
		.lower_to_scalar = true,
		.has_imul24 = true,
		.max_unroll_iterations = 32,
		.lower_wpos_pntc = true,
};

const nir_shader_compiler_options *
ir3_get_compiler_options(struct ir3_compiler *compiler)
{
	if (compiler->gpu_id >= 600)
		return &options_a6xx;
	return &options;
}

#define OPT(nir, pass, ...) ({                             \
   bool this_progress = false;                             \
   NIR_PASS(this_progress, nir, pass, ##__VA_ARGS__);      \
   this_progress;                                          \
})

#define OPT_V(nir, pass, ...) NIR_PASS_V(nir, pass, ##__VA_ARGS__)

static void
ir3_optimize_loop(nir_shader *s)
{
	bool progress;
	unsigned lower_flrp =
		(s->options->lower_flrp16 ? 16 : 0) |
		(s->options->lower_flrp32 ? 32 : 0) |
		(s->options->lower_flrp64 ? 64 : 0);

	do {
		progress = false;

		OPT_V(s, nir_lower_vars_to_ssa);
		progress |= OPT(s, nir_opt_copy_prop_vars);
		progress |= OPT(s, nir_opt_dead_write_vars);
		progress |= OPT(s, nir_lower_alu_to_scalar, NULL, NULL);
		progress |= OPT(s, nir_lower_phis_to_scalar);

		progress |= OPT(s, nir_copy_prop);
		progress |= OPT(s, nir_opt_dce);
		progress |= OPT(s, nir_opt_cse);
		static int gcm = -1;
		if (gcm == -1)
			gcm = env_var_as_unsigned("GCM", 0);
		if (gcm == 1)
			progress |= OPT(s, nir_opt_gcm, true);
		else if (gcm == 2)
			progress |= OPT(s, nir_opt_gcm, false);
		progress |= OPT(s, nir_opt_peephole_select, 16, true, true);
		progress |= OPT(s, nir_opt_intrinsics);
		progress |= OPT(s, nir_opt_algebraic);
		progress |= OPT(s, nir_lower_alu);
		progress |= OPT(s, nir_lower_pack);
		progress |= OPT(s, nir_opt_constant_folding);

		if (lower_flrp != 0) {
			if (OPT(s, nir_lower_flrp,
					lower_flrp,
					false /* always_precise */,
					s->options->lower_ffma)) {
				OPT(s, nir_opt_constant_folding);
				progress = true;
			}

			/* Nothing should rematerialize any flrps, so we only
			 * need to do this lowering once.
			 */
			lower_flrp = 0;
		}

		progress |= OPT(s, nir_opt_dead_cf);
		if (OPT(s, nir_opt_trivial_continues)) {
			progress |= true;
			/* If nir_opt_trivial_continues makes progress, then we need to clean
			 * things up if we want any hope of nir_opt_if or nir_opt_loop_unroll
			 * to make progress.
			 */
			OPT(s, nir_copy_prop);
			OPT(s, nir_opt_dce);
		}
		progress |= OPT(s, nir_opt_if, false);
		progress |= OPT(s, nir_opt_remove_phis);
		progress |= OPT(s, nir_opt_undef);
	} while (progress);
}

static bool
should_split_wrmask(const nir_instr *instr, const void *data)
{
	nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);

	switch (intr->intrinsic) {
	case nir_intrinsic_store_ssbo:
	case nir_intrinsic_store_shared:
	case nir_intrinsic_store_global:
		return true;
	default:
		return false;
	}
}

void
ir3_finalize_nir(struct ir3_compiler *compiler, nir_shader *s)
{
	struct nir_lower_tex_options tex_options = {
			.lower_rect = 0,
			.lower_tg4_offsets = true,
	};

	if (compiler->gpu_id >= 400) {
		/* a4xx seems to have *no* sam.p */
		tex_options.lower_txp = ~0;  /* lower all txp */
	} else {
		/* a3xx just needs to avoid sam.p for 3d tex */
		tex_options.lower_txp = (1 << GLSL_SAMPLER_DIM_3D);
	}

	if (ir3_shader_debug & IR3_DBG_DISASM) {
		debug_printf("----------------------\n");
		nir_print_shader(s, stdout);
		debug_printf("----------------------\n");
	}

	if (s->info.stage == MESA_SHADER_GEOMETRY)
		NIR_PASS_V(s, ir3_nir_lower_gs);

	NIR_PASS_V(s, nir_lower_io_arrays_to_elements_no_indirects, false);

	NIR_PASS_V(s, nir_lower_amul, ir3_glsl_type_size);

	OPT_V(s, nir_lower_regs_to_ssa);
	OPT_V(s, nir_lower_wrmasks, should_split_wrmask, s);

	OPT_V(s, nir_lower_tex, &tex_options);
	OPT_V(s, nir_lower_load_const_to_scalar);
	if (compiler->gpu_id < 500)
		OPT_V(s, ir3_nir_lower_tg4_to_tex);

	ir3_optimize_loop(s);

	/* do idiv lowering after first opt loop to get a chance to propagate
	 * constants for divide by immed power-of-two:
	 */
	const bool idiv_progress = OPT(s, nir_lower_idiv, nir_lower_idiv_fast);

	if (idiv_progress)
		ir3_optimize_loop(s);

	OPT_V(s, nir_remove_dead_variables, nir_var_function_temp, NULL);

	if (ir3_shader_debug & IR3_DBG_DISASM) {
		debug_printf("----------------------\n");
		nir_print_shader(s, stdout);
		debug_printf("----------------------\n");
	}

	nir_sweep(s);
}

/**
 * Late passes that need to be done after pscreen->finalize_nir()
 */
void
ir3_nir_post_finalize(struct ir3_compiler *compiler, nir_shader *s)
{
	NIR_PASS_V(s, nir_lower_io, nir_var_all, ir3_glsl_type_size,
			   (nir_lower_io_options)0);

	if (s->info.stage == MESA_SHADER_FRAGMENT) {
		/* NOTE: lower load_barycentric_at_sample first, since it
		 * produces load_barycentric_at_offset:
		 */
		NIR_PASS_V(s, ir3_nir_lower_load_barycentric_at_sample);
		NIR_PASS_V(s, ir3_nir_lower_load_barycentric_at_offset);
		NIR_PASS_V(s, ir3_nir_move_varying_inputs);
		NIR_PASS_V(s, nir_lower_fb_read);
	}

	if (compiler->gpu_id >= 600 &&
			s->info.stage == MESA_SHADER_FRAGMENT &&
			!(ir3_shader_debug & IR3_DBG_NOFP16)) {
		NIR_PASS_V(s, nir_lower_mediump_outputs);
	}

	/* we cannot ensure that ir3_finalize_nir() is only called once, so
	 * we also need to do trig workarounds here:
	 */
	OPT_V(s, ir3_nir_apply_trig_workarounds);

	ir3_optimize_loop(s);
}

void
ir3_nir_lower_variant(struct ir3_shader_variant *so, nir_shader *s)
{
	if (ir3_shader_debug & IR3_DBG_DISASM) {
		debug_printf("----------------------\n");
		nir_print_shader(s, stdout);
		debug_printf("----------------------\n");
	}

	bool progress = false;

	if (so->key.has_gs || so->key.tessellation) {
		switch (so->shader->type) {
		case MESA_SHADER_VERTEX:
			NIR_PASS_V(s, ir3_nir_lower_to_explicit_output, so, so->key.tessellation);
			progress = true;
			break;
		case MESA_SHADER_TESS_CTRL:
			NIR_PASS_V(s, ir3_nir_lower_tess_ctrl, so, so->key.tessellation);
			NIR_PASS_V(s, ir3_nir_lower_to_explicit_input);
			progress = true;
			break;
		case MESA_SHADER_TESS_EVAL:
			NIR_PASS_V(s, ir3_nir_lower_tess_eval, so->key.tessellation);
			if (so->key.has_gs)
				NIR_PASS_V(s, ir3_nir_lower_to_explicit_output, so, so->key.tessellation);
			progress = true;
			break;
		case MESA_SHADER_GEOMETRY:
			NIR_PASS_V(s, ir3_nir_lower_to_explicit_input);
			progress = true;
			break;
		default:
			break;
		}
	}

	if (s->info.stage == MESA_SHADER_VERTEX) {
		if (so->key.ucp_enables)
			progress |= OPT(s, nir_lower_clip_vs, so->key.ucp_enables, false, false, NULL);
		if (so->key.vclamp_color)
			progress |= OPT(s, nir_lower_clamp_color_outputs);
	} else if (s->info.stage == MESA_SHADER_FRAGMENT) {
		if (so->key.ucp_enables)
			progress |= OPT(s, nir_lower_clip_fs, so->key.ucp_enables, false);
		if (so->key.fclamp_color)
			progress |= OPT(s, nir_lower_clamp_color_outputs);
	}
	if (so->key.color_two_side) {
		OPT_V(s, nir_lower_two_sided_color);
		progress = true;
	}

	struct nir_lower_tex_options tex_options = { };

	switch (so->shader->type) {
	case MESA_SHADER_FRAGMENT:
		tex_options.saturate_s = so->key.fsaturate_s;
		tex_options.saturate_t = so->key.fsaturate_t;
		tex_options.saturate_r = so->key.fsaturate_r;
		break;
	case MESA_SHADER_VERTEX:
		tex_options.saturate_s = so->key.vsaturate_s;
		tex_options.saturate_t = so->key.vsaturate_t;
		tex_options.saturate_r = so->key.vsaturate_r;
		break;
	default:
		/* TODO */
		break;
	}

	if (tex_options.saturate_s || tex_options.saturate_t ||
		tex_options.saturate_r) {
		progress |= OPT(s, nir_lower_tex, &tex_options);
	}

	if (!so->binning_pass)
		OPT_V(s, ir3_nir_analyze_ubo_ranges, so);

	progress |= OPT(s, ir3_nir_lower_ubo_loads, so);

	/* UBO offset lowering has to come after we've decided what will
	 * be left as load_ubo
	 */
	OPT_V(s, ir3_nir_lower_io_offsets, so->shader->compiler->gpu_id);

	if (progress)
		ir3_optimize_loop(s);

	/* Do late algebraic optimization to turn add(a, neg(b)) back into
	* subs, then the mandatory cleanup after algebraic.  Note that it may
	* produce fnegs, and if so then we need to keep running to squash
	* fneg(fneg(a)).
	*/
	bool more_late_algebraic = true;
	while (more_late_algebraic) {
		more_late_algebraic = OPT(s, nir_opt_algebraic_late);
		OPT_V(s, nir_opt_constant_folding);
		OPT_V(s, nir_copy_prop);
		OPT_V(s, nir_opt_dce);
		OPT_V(s, nir_opt_cse);
	}

	OPT_V(s, nir_opt_sink, nir_move_const_undef);

	if (ir3_shader_debug & IR3_DBG_DISASM) {
		debug_printf("----------------------\n");
		nir_print_shader(s, stdout);
		debug_printf("----------------------\n");
	}

	nir_sweep(s);

	/* Binning pass variants re-use  the const_state of the corresponding
	 * draw pass shader, so that same const emit can be re-used for both
	 * passes:
	 */
	if (!so->binning_pass)
		ir3_setup_const_state(s, so, ir3_const_state(so));
}

static void
ir3_nir_scan_driver_consts(nir_shader *shader,
		struct ir3_const_state *layout)
{
	nir_foreach_function (function, shader) {
		if (!function->impl)
			continue;

		nir_foreach_block (block, function->impl) {
			nir_foreach_instr (instr, block) {
				if (instr->type != nir_instr_type_intrinsic)
					continue;

				nir_intrinsic_instr *intr =
					nir_instr_as_intrinsic(instr);
				unsigned idx;

				switch (intr->intrinsic) {
				case nir_intrinsic_get_buffer_size:
					idx = nir_src_as_uint(intr->src[0]);
					if (layout->ssbo_size.mask & (1 << idx))
						break;
					layout->ssbo_size.mask |= (1 << idx);
					layout->ssbo_size.off[idx] =
						layout->ssbo_size.count;
					layout->ssbo_size.count += 1; /* one const per */
					break;
				case nir_intrinsic_image_atomic_add:
				case nir_intrinsic_image_atomic_imin:
				case nir_intrinsic_image_atomic_umin:
				case nir_intrinsic_image_atomic_imax:
				case nir_intrinsic_image_atomic_umax:
				case nir_intrinsic_image_atomic_and:
				case nir_intrinsic_image_atomic_or:
				case nir_intrinsic_image_atomic_xor:
				case nir_intrinsic_image_atomic_exchange:
				case nir_intrinsic_image_atomic_comp_swap:
				case nir_intrinsic_image_store:
				case nir_intrinsic_image_size:
					idx = nir_src_as_uint(intr->src[0]);
					if (layout->image_dims.mask & (1 << idx))
						break;
					layout->image_dims.mask |= (1 << idx);
					layout->image_dims.off[idx] =
						layout->image_dims.count;
					layout->image_dims.count += 3; /* three const per */
					break;
				case nir_intrinsic_load_base_vertex:
				case nir_intrinsic_load_first_vertex:
					layout->num_driver_params =
						MAX2(layout->num_driver_params, IR3_DP_VTXID_BASE + 1);
					break;
				case nir_intrinsic_load_base_instance:
					layout->num_driver_params =
						MAX2(layout->num_driver_params, IR3_DP_INSTID_BASE + 1);
					break;
				case nir_intrinsic_load_user_clip_plane:
					layout->num_driver_params =
						MAX2(layout->num_driver_params, IR3_DP_UCP7_W + 1);
					break;
				case nir_intrinsic_load_num_work_groups:
					layout->num_driver_params =
						MAX2(layout->num_driver_params, IR3_DP_NUM_WORK_GROUPS_Z + 1);
					break;
				case nir_intrinsic_load_local_group_size:
					layout->num_driver_params =
						MAX2(layout->num_driver_params, IR3_DP_LOCAL_GROUP_SIZE_Z + 1);
					break;
				default:
					break;
				}
			}
		}
	}
}

/* Sets up the variant-dependent constant state for the ir3_shader.  Note
 * that it is also used from ir3_nir_analyze_ubo_ranges() to figure out the
 * maximum number of driver params that would eventually be used, to leave
 * space for this function to allocate the driver params.
 */
void
ir3_setup_const_state(nir_shader *nir, struct ir3_shader_variant *v,
		struct ir3_const_state *const_state)
{
	struct ir3_compiler *compiler = v->shader->compiler;

	memset(&const_state->offsets, ~0, sizeof(const_state->offsets));

	ir3_nir_scan_driver_consts(nir, const_state);

	if ((compiler->gpu_id < 500) &&
			(v->shader->stream_output.num_outputs > 0)) {
		const_state->num_driver_params =
			MAX2(const_state->num_driver_params, IR3_DP_VTXCNT_MAX + 1);
	}

	const_state->num_ubos = nir->info.num_ubos;

	/* num_driver_params is scalar, align to vec4: */
	const_state->num_driver_params = align(const_state->num_driver_params, 4);

	debug_assert((const_state->ubo_state.size % 16) == 0);
	unsigned constoff = const_state->ubo_state.size / 16;
	unsigned ptrsz = ir3_pointer_size(compiler);

	if (const_state->num_ubos > 0) {
		const_state->offsets.ubo = constoff;
		constoff += align(const_state->num_ubos * ptrsz, 4) / 4;
	}

	if (const_state->ssbo_size.count > 0) {
		unsigned cnt = const_state->ssbo_size.count;
		const_state->offsets.ssbo_sizes = constoff;
		constoff += align(cnt, 4) / 4;
	}

	if (const_state->image_dims.count > 0) {
		unsigned cnt = const_state->image_dims.count;
		const_state->offsets.image_dims = constoff;
		constoff += align(cnt, 4) / 4;
	}

	if (const_state->num_driver_params > 0) {
		/* offset cannot be 0 for vs params loaded by CP_DRAW_INDIRECT_MULTI */
		if (v->type == MESA_SHADER_VERTEX && compiler->gpu_id >= 600)
			constoff = MAX2(constoff, 1);
		const_state->offsets.driver_param = constoff;
	}
	constoff += const_state->num_driver_params / 4;

	if ((v->type == MESA_SHADER_VERTEX) &&
			(compiler->gpu_id < 500) &&
			v->shader->stream_output.num_outputs > 0) {
		const_state->offsets.tfbo = constoff;
		constoff += align(IR3_MAX_SO_BUFFERS * ptrsz, 4) / 4;
	}

	switch (v->type) {
	case MESA_SHADER_VERTEX:
		const_state->offsets.primitive_param = constoff;
		constoff += 1;
		break;
	case MESA_SHADER_TESS_CTRL:
	case MESA_SHADER_TESS_EVAL:
		constoff = align(constoff - 1, 4) + 3;
		const_state->offsets.primitive_param = constoff;
		const_state->offsets.primitive_map = constoff + 5;
		constoff += 5 + DIV_ROUND_UP(nir->num_inputs, 4);
		break;
	case MESA_SHADER_GEOMETRY:
		const_state->offsets.primitive_param = constoff;
		const_state->offsets.primitive_map = constoff + 1;
		constoff += 1 + DIV_ROUND_UP(nir->num_inputs, 4);
		break;
	default:
		break;
	}

	const_state->offsets.immediate = constoff;

	assert(constoff <= ir3_max_const(v));
}
