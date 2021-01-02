/*
 * Copyright (C) 2014 Rob Clark <robclark@freedesktop.org>
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

#include "pipe/p_state.h"
#include "pipe/p_screen.h"
#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/format/u_format.h"
#include "tgsi/tgsi_dump.h"
#include "tgsi/tgsi_parse.h"

#include "nir/tgsi_to_nir.h"

#include "freedreno_context.h"
#include "freedreno_util.h"

#include "ir3/ir3_shader.h"
#include "ir3/ir3_gallium.h"
#include "ir3/ir3_compiler.h"
#include "ir3/ir3_nir.h"

static void
dump_shader_info(struct ir3_shader_variant *v, struct pipe_debug_callback *debug)
{
	if (!unlikely(fd_mesa_debug & FD_DBG_SHADERDB))
		return;

	pipe_debug_message(debug, SHADER_INFO,
			"%s shader: %u inst, %u nops, %u non-nops, %u mov, %u cov, "
			"%u dwords, %u last-baryf, %u half, %u full, %u constlen, "
			"%u cat0, %u cat1, %u cat2, %u cat3, %u cat4, %u cat5, %u cat6, %u cat7, "
			"%u sstall, %u (ss), %u (sy), %d max_sun, %d loops\n",
			ir3_shader_stage(v),
			v->info.instrs_count,
			v->info.nops_count,
			v->info.instrs_count - v->info.nops_count,
			v->info.mov_count,
			v->info.cov_count,
			v->info.sizedwords,
			v->info.last_baryf,
			v->info.max_half_reg + 1,
			v->info.max_reg + 1,
			v->constlen,
			v->info.instrs_per_cat[0],
			v->info.instrs_per_cat[1],
			v->info.instrs_per_cat[2],
			v->info.instrs_per_cat[3],
			v->info.instrs_per_cat[4],
			v->info.instrs_per_cat[5],
			v->info.instrs_per_cat[6],
			v->info.instrs_per_cat[7],
			v->info.sstall,
			v->info.ss, v->info.sy,
			v->max_sun, v->loops);
}

static void
upload_shader_variant(struct ir3_shader_variant *v)
{
	struct shader_info *info = &v->shader->nir->info;
	struct ir3_compiler *compiler = v->shader->compiler;

	assert(!v->bo);

	v->bo = fd_bo_new(compiler->dev, v->info.size,
			DRM_FREEDRENO_GEM_CACHE_WCOMBINE |
			DRM_FREEDRENO_GEM_TYPE_KMEM,
			"%s:%s", ir3_shader_stage(v), info->name);

	/* Always include shaders in kernel crash dumps. */
	fd_bo_mark_for_dump(v->bo);

	memcpy(fd_bo_map(v->bo), v->bin, v->info.size);
}

struct ir3_shader_variant *
ir3_shader_variant(struct ir3_shader *shader, struct ir3_shader_key key,
		bool binning_pass, struct pipe_debug_callback *debug)
{
	struct ir3_shader_variant *v;
	bool created = false;

	/* Some shader key values may not be used by a given ir3_shader (for
	 * example, fragment shader saturates in the vertex shader), so clean out
	 * those flags to avoid recompiling.
	 */
	ir3_key_clear_unused(&key, shader);

	v = ir3_shader_get_variant(shader, &key, binning_pass, &created);

	if (created) {
		if (shader->initial_variants_done) {
			pipe_debug_message(debug, SHADER_INFO,
					"%s shader: recompiling at draw time: global 0x%08x, vsats %x/%x/%x, fsats %x/%x/%x, vfsamples %x/%x, astc %x/%x\n",
					ir3_shader_stage(v),
					key.global,
					key.vsaturate_s, key.vsaturate_t, key.vsaturate_r,
					key.fsaturate_s, key.fsaturate_t, key.fsaturate_r,
					key.vsamples, key.fsamples,
					key.vastc_srgb, key.fastc_srgb);

		}

		dump_shader_info(v, debug);
		upload_shader_variant(v);

		if (v->binning) {
			upload_shader_variant(v->binning);
			dump_shader_info(v->binning, debug);
		}
	}

	return v;
}

static void
copy_stream_out(struct ir3_stream_output_info *i,
		const struct pipe_stream_output_info *p)
{
	STATIC_ASSERT(ARRAY_SIZE(i->stride) == ARRAY_SIZE(p->stride));
	STATIC_ASSERT(ARRAY_SIZE(i->output) == ARRAY_SIZE(p->output));

	i->num_outputs = p->num_outputs;
	for (int n = 0; n < ARRAY_SIZE(i->stride); n++)
		i->stride[n] = p->stride[n];

	for (int n = 0; n < ARRAY_SIZE(i->output); n++) {
		i->output[n].register_index  = p->output[n].register_index;
		i->output[n].start_component = p->output[n].start_component;
		i->output[n].num_components  = p->output[n].num_components;
		i->output[n].output_buffer   = p->output[n].output_buffer;
		i->output[n].dst_offset      = p->output[n].dst_offset;
		i->output[n].stream          = p->output[n].stream;
	}
}

struct ir3_shader *
ir3_shader_create(struct ir3_compiler *compiler,
		const struct pipe_shader_state *cso,
		struct pipe_debug_callback *debug,
		struct pipe_screen *screen)
{
	nir_shader *nir;
	if (cso->type == PIPE_SHADER_IR_NIR) {
		/* we take ownership of the reference: */
		nir = cso->ir.nir;
	} else {
		debug_assert(cso->type == PIPE_SHADER_IR_TGSI);
		if (ir3_shader_debug & IR3_DBG_DISASM) {
			tgsi_dump(cso->tokens, 0);
		}
		nir = tgsi_to_nir(cso->tokens, screen, false);
	}

	struct ir3_stream_output_info stream_output = {};
	copy_stream_out(&stream_output, &cso->stream_output);

	struct ir3_shader *shader = ir3_shader_from_nir(compiler, nir, 0, &stream_output);

	/* Compile standard variants immediately to try to avoid draw-time stalls
	 * to run the compiler.
	 */
	struct ir3_shader_key key = {
		.tessellation = IR3_TESS_NONE,
		.ucp_enables = MASK(nir->info.clip_distance_array_size),
		.msaa = true,
	};

	switch (nir->info.stage) {
	case MESA_SHADER_TESS_EVAL:
		key.tessellation = ir3_tess_mode(nir->info.tess.primitive_mode);
		break;

	case MESA_SHADER_TESS_CTRL:
		/* The primitive_mode field, while it exists for TCS, is not
		 * populated (since separable shaders between TCS/TES are legal,
		 * so TCS wouldn't have access to TES's declaration).  Make a
		 * guess so that we shader-db something plausible for TCS.
		 */
		if (nir->info.outputs_written & VARYING_BIT_TESS_LEVEL_INNER)
			key.tessellation = IR3_TESS_TRIANGLES;
		else
			key.tessellation = IR3_TESS_ISOLINES;
		break;

	case MESA_SHADER_GEOMETRY:
		key.has_gs = true;
		break;

	default:
		break;
	}

	key.safe_constlen = false;
	struct ir3_shader_variant *v = ir3_shader_variant(shader, key, false, debug);
	if (!v)
		return NULL;

	if (v->constlen > compiler->max_const_safe) {
		key.safe_constlen = true;
		ir3_shader_variant(shader, key, false, debug);
	}

	if (nir->info.stage == MESA_SHADER_VERTEX) {
		key.safe_constlen = false;
		v = ir3_shader_variant(shader, key, true, debug);
		if (!v)
			return NULL;

		if (v->constlen > compiler->max_const_safe) {
			key.safe_constlen = true;
			ir3_shader_variant(shader, key, true, debug);
		}
	}

	shader->initial_variants_done = true;

	return shader;
}

/* a bit annoying that compute-shader and normal shader state objects
 * aren't a bit more aligned.
 */
struct ir3_shader *
ir3_shader_create_compute(struct ir3_compiler *compiler,
		const struct pipe_compute_state *cso,
		struct pipe_debug_callback *debug,
		struct pipe_screen *screen)
{
	nir_shader *nir;
	if (cso->ir_type == PIPE_SHADER_IR_NIR) {
		/* we take ownership of the reference: */
		nir = (nir_shader *)cso->prog;
	} else {
		debug_assert(cso->ir_type == PIPE_SHADER_IR_TGSI);
		if (ir3_shader_debug & IR3_DBG_DISASM) {
			tgsi_dump(cso->prog, 0);
		}
		nir = tgsi_to_nir(cso->prog, screen, false);
	}

	struct ir3_shader *shader = ir3_shader_from_nir(compiler, nir, 0, NULL);

	/* Immediately compile a standard variant.  We have so few variants in our
	 * shaders, that doing so almost eliminates draw-time recompiles.  (This
	 * is also how we get data from shader-db's ./run)
	 */
	static struct ir3_shader_key key; /* static is implicitly zeroed */
	ir3_shader_variant(shader, key, false, debug);

	shader->initial_variants_done = true;

	return shader;
}

void *
ir3_shader_state_create(struct pipe_context *pctx, const struct pipe_shader_state *cso)
{
	struct fd_context *ctx = fd_context(pctx);
	struct ir3_compiler *compiler = ctx->screen->compiler;
	return ir3_shader_create(compiler, cso, &ctx->debug, pctx->screen);
}

void
ir3_shader_state_delete(struct pipe_context *pctx, void *hwcso)
{
	struct ir3_shader *so = hwcso;

	/* free the uploaded shaders, since this is handled outside of the
	 * shared ir3 code (ie. not used by turnip):
	 */
	for (struct ir3_shader_variant *v = so->variants; v; v = v->next) {
		fd_bo_del(v->bo);
		v->bo = NULL;

		if (v->binning && v->binning->bo) {
			fd_bo_del(v->binning->bo);
			v->binning->bo = NULL;
		}
	}

	ir3_shader_destroy(so);
}

static void
ir3_screen_finalize_nir(struct pipe_screen *pscreen, void *nir, bool optimize)
{
	struct fd_screen *screen = fd_screen(pscreen);

	ir3_finalize_nir(screen->compiler, nir);
}

void
ir3_prog_init(struct pipe_context *pctx)
{
	pctx->create_vs_state = ir3_shader_state_create;
	pctx->delete_vs_state = ir3_shader_state_delete;

	pctx->create_tcs_state = ir3_shader_state_create;
	pctx->delete_tcs_state = ir3_shader_state_delete;

	pctx->create_tes_state = ir3_shader_state_create;
	pctx->delete_tes_state = ir3_shader_state_delete;

	pctx->create_gs_state = ir3_shader_state_create;
	pctx->delete_gs_state = ir3_shader_state_delete;

	pctx->create_fs_state = ir3_shader_state_create;
	pctx->delete_fs_state = ir3_shader_state_delete;
}

void
ir3_screen_init(struct pipe_screen *pscreen)
{
	pscreen->finalize_nir = ir3_screen_finalize_nir;
}
