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

#include "ir3_nir.h"
#include "ir3_compiler.h"
#include "ir3_shader.h"

static const nir_shader_compiler_options options = {
		.lower_fpow = true,
		.lower_scmp = true,
		.lower_flrp32 = true,
		.lower_flrp64 = true,
		.lower_ffract = true,
		.lower_fmod32 = true,
		.lower_fmod64 = true,
		.lower_fdiv = true,
		.lower_isign = true,
		.lower_ldexp = true,
		.fuse_ffma = true,
		.native_integers = true,
		.vertex_id_zero_based = true,
		.lower_extract_byte = true,
		.lower_extract_word = true,
		.lower_all_io_to_temps = true,
		.lower_helper_invocation = true,
};

const nir_shader_compiler_options *
ir3_get_compiler_options(struct ir3_compiler *compiler)
{
	return &options;
}

/* for given shader key, are any steps handled in nir? */
bool
ir3_key_lowers_nir(const struct ir3_shader_key *key)
{
	return key->fsaturate_s | key->fsaturate_t | key->fsaturate_r |
			key->vsaturate_s | key->vsaturate_t | key->vsaturate_r |
			key->ucp_enables | key->color_two_side |
			key->fclamp_color | key->vclamp_color;
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
	do {
		progress = false;

		OPT_V(s, nir_lower_vars_to_ssa);
		progress |= OPT(s, nir_opt_copy_prop_vars);
		progress |= OPT(s, nir_opt_dead_write_vars);
		progress |= OPT(s, nir_lower_alu_to_scalar);
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
		progress |= OPT(s, nir_opt_constant_folding);
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
		progress |= OPT(s, nir_opt_if);
		progress |= OPT(s, nir_opt_remove_phis);
		progress |= OPT(s, nir_opt_undef);

	} while (progress);
}

struct nir_shader *
ir3_optimize_nir(struct ir3_shader *shader, nir_shader *s,
		const struct ir3_shader_key *key)
{
	struct nir_lower_tex_options tex_options = {
			.lower_rect = 0,
	};

	if (key) {
		switch (shader->type) {
		case MESA_SHADER_FRAGMENT:
			tex_options.saturate_s = key->fsaturate_s;
			tex_options.saturate_t = key->fsaturate_t;
			tex_options.saturate_r = key->fsaturate_r;
			break;
		case MESA_SHADER_VERTEX:
			tex_options.saturate_s = key->vsaturate_s;
			tex_options.saturate_t = key->vsaturate_t;
			tex_options.saturate_r = key->vsaturate_r;
			break;
		default:
			/* TODO */
			break;
		}
	}

	if (shader->compiler->gpu_id >= 400) {
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

	OPT_V(s, nir_opt_global_to_local);
	OPT_V(s, nir_lower_regs_to_ssa);

	if (key) {
		if (s->info.stage == MESA_SHADER_VERTEX) {
			OPT_V(s, nir_lower_clip_vs, key->ucp_enables, false);
			if (key->vclamp_color)
				OPT_V(s, nir_lower_clamp_color_outputs);
		} else if (s->info.stage == MESA_SHADER_FRAGMENT) {
			OPT_V(s, nir_lower_clip_fs, key->ucp_enables);
			if (key->fclamp_color)
				OPT_V(s, nir_lower_clamp_color_outputs);
		}
		if (key->color_two_side) {
			OPT_V(s, nir_lower_two_sided_color);
		}
	} else {
		/* only want to do this the first time (when key is null)
		 * and not again on any potential 2nd variant lowering pass:
		 */
		OPT_V(s, ir3_nir_apply_trig_workarounds);
	}

	OPT_V(s, nir_lower_tex, &tex_options);
	OPT_V(s, nir_lower_load_const_to_scalar);
	if (shader->compiler->gpu_id < 500)
		OPT_V(s, ir3_nir_lower_tg4_to_tex);

	ir3_optimize_loop(s);

	/* do idiv lowering after first opt loop to give a chance for
	 * divide by immed power-of-two to be caught first:
	 */
	if (OPT(s, nir_lower_idiv))
		ir3_optimize_loop(s);

	OPT_V(s, nir_remove_dead_variables, nir_var_function_temp);

	OPT_V(s, nir_move_load_const);

	if (ir3_shader_debug & IR3_DBG_DISASM) {
		debug_printf("----------------------\n");
		nir_print_shader(s, stdout);
		debug_printf("----------------------\n");
	}

	nir_sweep(s);

	return s;
}

void
ir3_nir_scan_driver_consts(nir_shader *shader,
		struct ir3_driver_const_layout *layout)
{
	nir_foreach_function(function, shader) {
		if (!function->impl)
			continue;

		nir_foreach_block(block, function->impl) {
			nir_foreach_instr(instr, block) {
				if (instr->type != nir_instr_type_intrinsic)
					continue;

				nir_intrinsic_instr *intr =
					nir_instr_as_intrinsic(instr);
				unsigned idx;

				switch (intr->intrinsic) {
				case nir_intrinsic_get_buffer_size:
					idx = nir_src_as_const_value(intr->src[0])->u32[0];
					if (layout->ssbo_size.mask & (1 << idx))
						break;
					layout->ssbo_size.mask |= (1 << idx);
					layout->ssbo_size.off[idx] =
						layout->ssbo_size.count;
					layout->ssbo_size.count += 1; /* one const per */
					break;
				case nir_intrinsic_image_deref_atomic_add:
				case nir_intrinsic_image_deref_atomic_min:
				case nir_intrinsic_image_deref_atomic_max:
				case nir_intrinsic_image_deref_atomic_and:
				case nir_intrinsic_image_deref_atomic_or:
				case nir_intrinsic_image_deref_atomic_xor:
				case nir_intrinsic_image_deref_atomic_exchange:
				case nir_intrinsic_image_deref_atomic_comp_swap:
				case nir_intrinsic_image_deref_store:
				case nir_intrinsic_image_deref_size:
					idx = nir_intrinsic_get_var(intr, 0)->data.driver_location;
					if (layout->image_dims.mask & (1 << idx))
						break;
					layout->image_dims.mask |= (1 << idx);
					layout->image_dims.off[idx] =
						layout->image_dims.count;
					layout->image_dims.count += 3; /* three const per */
					break;
				default:
					break;
				}
			}
		}
	}
}
