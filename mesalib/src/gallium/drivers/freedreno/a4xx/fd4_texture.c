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
#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/u_inlines.h"
#include "util/format/u_format.h"

#include "fd4_texture.h"
#include "fd4_format.h"

static enum a4xx_tex_clamp
tex_clamp(unsigned wrap, bool clamp_to_edge, bool *needs_border)
{
	/* Hardware does not support _CLAMP, but we emulate it: */
	if (wrap == PIPE_TEX_WRAP_CLAMP) {
		wrap = (clamp_to_edge) ?
			PIPE_TEX_WRAP_CLAMP_TO_EDGE : PIPE_TEX_WRAP_CLAMP_TO_BORDER;
	}

	switch (wrap) {
	case PIPE_TEX_WRAP_REPEAT:
		return A4XX_TEX_REPEAT;
	case PIPE_TEX_WRAP_CLAMP_TO_EDGE:
		return A4XX_TEX_CLAMP_TO_EDGE;
	case PIPE_TEX_WRAP_CLAMP_TO_BORDER:
		*needs_border = true;
		return A4XX_TEX_CLAMP_TO_BORDER;
	case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_EDGE:
		/* only works for PoT.. need to emulate otherwise! */
		return A4XX_TEX_MIRROR_CLAMP;
	case PIPE_TEX_WRAP_MIRROR_REPEAT:
		return A4XX_TEX_MIRROR_REPEAT;
	case PIPE_TEX_WRAP_MIRROR_CLAMP:
	case PIPE_TEX_WRAP_MIRROR_CLAMP_TO_BORDER:
		/* these two we could perhaps emulate, but we currently
		 * just don't advertise PIPE_CAP_TEXTURE_MIRROR_CLAMP
		 */
	default:
		DBG("invalid wrap: %u", wrap);
		return 0;
	}
}

static enum a4xx_tex_filter
tex_filter(unsigned filter, bool aniso)
{
	switch (filter) {
	case PIPE_TEX_FILTER_NEAREST:
		return A4XX_TEX_NEAREST;
	case PIPE_TEX_FILTER_LINEAR:
		return aniso ? A4XX_TEX_ANISO : A4XX_TEX_LINEAR;
	default:
		DBG("invalid filter: %u", filter);
		return 0;
	}
}

static void *
fd4_sampler_state_create(struct pipe_context *pctx,
		const struct pipe_sampler_state *cso)
{
	struct fd4_sampler_stateobj *so = CALLOC_STRUCT(fd4_sampler_stateobj);
	unsigned aniso = util_last_bit(MIN2(cso->max_anisotropy >> 1, 8));
	bool miplinear = false;
	bool clamp_to_edge;

	if (!so)
		return NULL;

	if (cso->min_mip_filter == PIPE_TEX_MIPFILTER_LINEAR)
		miplinear = true;

	so->base = *cso;

	/*
	 * For nearest filtering, _CLAMP means _CLAMP_TO_EDGE;  for linear
	 * filtering, _CLAMP means _CLAMP_TO_BORDER while additionally
	 * clamping the texture coordinates to [0.0, 1.0].
	 *
	 * The clamping will be taken care of in the shaders.  There are two
	 * filters here, but let the minification one has a say.
	 */
	clamp_to_edge = (cso->min_img_filter == PIPE_TEX_FILTER_NEAREST);
	if (!clamp_to_edge) {
		so->saturate_s = (cso->wrap_s == PIPE_TEX_WRAP_CLAMP);
		so->saturate_t = (cso->wrap_t == PIPE_TEX_WRAP_CLAMP);
		so->saturate_r = (cso->wrap_r == PIPE_TEX_WRAP_CLAMP);
	}

	so->needs_border = false;
	so->texsamp0 =
		COND(miplinear, A4XX_TEX_SAMP_0_MIPFILTER_LINEAR_NEAR) |
		A4XX_TEX_SAMP_0_XY_MAG(tex_filter(cso->mag_img_filter, aniso)) |
		A4XX_TEX_SAMP_0_XY_MIN(tex_filter(cso->min_img_filter, aniso)) |
		A4XX_TEX_SAMP_0_ANISO(aniso) |
		A4XX_TEX_SAMP_0_WRAP_S(tex_clamp(cso->wrap_s, clamp_to_edge, &so->needs_border)) |
		A4XX_TEX_SAMP_0_WRAP_T(tex_clamp(cso->wrap_t, clamp_to_edge, &so->needs_border)) |
		A4XX_TEX_SAMP_0_WRAP_R(tex_clamp(cso->wrap_r, clamp_to_edge, &so->needs_border));

	so->texsamp1 =
//		COND(miplinear, A4XX_TEX_SAMP_1_MIPFILTER_LINEAR_FAR) |
		COND(!cso->seamless_cube_map, A4XX_TEX_SAMP_1_CUBEMAPSEAMLESSFILTOFF) |
		COND(!cso->normalized_coords, A4XX_TEX_SAMP_1_UNNORM_COORDS);

	if (cso->min_mip_filter != PIPE_TEX_MIPFILTER_NONE) {
		so->texsamp0 |= A4XX_TEX_SAMP_0_LOD_BIAS(cso->lod_bias);
		so->texsamp1 |=
			A4XX_TEX_SAMP_1_MIN_LOD(cso->min_lod) |
			A4XX_TEX_SAMP_1_MAX_LOD(cso->max_lod);
	}

	if (cso->compare_mode)
		so->texsamp1 |= A4XX_TEX_SAMP_1_COMPARE_FUNC(cso->compare_func); /* maps 1:1 */

	return so;
}

static void
fd4_sampler_states_bind(struct pipe_context *pctx,
		enum pipe_shader_type shader, unsigned start,
		unsigned nr, void **hwcso)
{
	struct fd_context *ctx = fd_context(pctx);
	struct fd4_context *fd4_ctx = fd4_context(ctx);
	uint16_t saturate_s = 0, saturate_t = 0, saturate_r = 0;
	unsigned i;

	if (!hwcso)
		nr = 0;

	for (i = 0; i < nr; i++) {
		if (hwcso[i]) {
			struct fd4_sampler_stateobj *sampler =
					fd4_sampler_stateobj(hwcso[i]);
			if (sampler->saturate_s)
				saturate_s |= (1 << i);
			if (sampler->saturate_t)
				saturate_t |= (1 << i);
			if (sampler->saturate_r)
				saturate_r |= (1 << i);
		}
	}

	fd_sampler_states_bind(pctx, shader, start, nr, hwcso);

	if (shader == PIPE_SHADER_FRAGMENT) {
		fd4_ctx->fsaturate =
			(saturate_s != 0) ||
			(saturate_t != 0) ||
			(saturate_r != 0);
		fd4_ctx->fsaturate_s = saturate_s;
		fd4_ctx->fsaturate_t = saturate_t;
		fd4_ctx->fsaturate_r = saturate_r;
	} else if (shader == PIPE_SHADER_VERTEX) {
		fd4_ctx->vsaturate =
			(saturate_s != 0) ||
			(saturate_t != 0) ||
			(saturate_r != 0);
		fd4_ctx->vsaturate_s = saturate_s;
		fd4_ctx->vsaturate_t = saturate_t;
		fd4_ctx->vsaturate_r = saturate_r;
	}
}

static enum a4xx_tex_type
tex_type(unsigned target)
{
	switch (target) {
	default:
		assert(0);
	case PIPE_BUFFER:
	case PIPE_TEXTURE_1D:
	case PIPE_TEXTURE_1D_ARRAY:
		return A4XX_TEX_1D;
	case PIPE_TEXTURE_RECT:
	case PIPE_TEXTURE_2D:
	case PIPE_TEXTURE_2D_ARRAY:
		return A4XX_TEX_2D;
	case PIPE_TEXTURE_3D:
		return A4XX_TEX_3D;
	case PIPE_TEXTURE_CUBE:
	case PIPE_TEXTURE_CUBE_ARRAY:
		return A4XX_TEX_CUBE;
	}
}

static bool
use_astc_srgb_workaround(struct pipe_context *pctx, enum pipe_format format)
{
	return (fd_screen(pctx->screen)->gpu_id == 420) &&
		(util_format_description(format)->layout == UTIL_FORMAT_LAYOUT_ASTC);
}

static struct pipe_sampler_view *
fd4_sampler_view_create(struct pipe_context *pctx, struct pipe_resource *prsc,
		const struct pipe_sampler_view *cso)
{
	struct fd4_pipe_sampler_view *so = CALLOC_STRUCT(fd4_pipe_sampler_view);
	struct fd_resource *rsc = fd_resource(prsc);
	enum pipe_format format = cso->format;
	unsigned lvl, layers = 0;

	if (!so)
		return NULL;

	if (format == PIPE_FORMAT_X32_S8X24_UINT) {
		rsc = rsc->stencil;
		format = rsc->base.format;
	}

	so->base = *cso;
	pipe_reference(NULL, &prsc->reference);
	so->base.texture = prsc;
	so->base.reference.count = 1;
	so->base.context = pctx;

	so->texconst0 =
		A4XX_TEX_CONST_0_TYPE(tex_type(cso->target)) |
		A4XX_TEX_CONST_0_FMT(fd4_pipe2tex(format)) |
		fd4_tex_swiz(format, cso->swizzle_r, cso->swizzle_g,
				cso->swizzle_b, cso->swizzle_a);

	if (util_format_is_srgb(format)) {
		if (use_astc_srgb_workaround(pctx, format))
			so->astc_srgb = true;
		so->texconst0 |= A4XX_TEX_CONST_0_SRGB;
	}

	if (cso->target == PIPE_BUFFER) {
		unsigned elements = cso->u.buf.size / util_format_get_blocksize(format);

		lvl = 0;
		so->texconst1 =
			A4XX_TEX_CONST_1_WIDTH(elements) |
			A4XX_TEX_CONST_1_HEIGHT(1);
		so->texconst2 =
			A4XX_TEX_CONST_2_PITCH(elements * rsc->layout.cpp);
		so->offset = cso->u.buf.offset;
	} else {
		unsigned miplevels;

		lvl = fd_sampler_first_level(cso);
		miplevels = fd_sampler_last_level(cso) - lvl;
		layers = cso->u.tex.last_layer - cso->u.tex.first_layer + 1;

		so->texconst0 |= A4XX_TEX_CONST_0_MIPLVLS(miplevels);
		so->texconst1 =
			A4XX_TEX_CONST_1_WIDTH(u_minify(prsc->width0, lvl)) |
			A4XX_TEX_CONST_1_HEIGHT(u_minify(prsc->height0, lvl));
		so->texconst2 =
			A4XX_TEX_CONST_2_PITCHALIGN(rsc->layout.pitchalign - 5) |
			A4XX_TEX_CONST_2_PITCH(fd_resource_pitch(rsc, lvl));
		so->offset = fd_resource_offset(rsc, lvl, cso->u.tex.first_layer);
	}

	/* NOTE: since we sample z24s8 using 8888_UINT format, the swizzle
	 * we get isn't quite right.  Use SWAP(XYZW) as a cheap and cheerful
	 * way to re-arrange things so stencil component is where the swiz
	 * expects.
	 *
	 * Note that gallium expects stencil sampler to return (s,s,s,s)
	 * which isn't quite true.  To make that happen we'd have to massage
	 * the swizzle.  But in practice only the .x component is used.
	 */
	if (format == PIPE_FORMAT_X24S8_UINT)
		so->texconst2 |= A4XX_TEX_CONST_2_SWAP(XYZW);

	switch (cso->target) {
	case PIPE_TEXTURE_1D_ARRAY:
	case PIPE_TEXTURE_2D_ARRAY:
		so->texconst3 =
			A4XX_TEX_CONST_3_DEPTH(layers) |
			A4XX_TEX_CONST_3_LAYERSZ(rsc->layout.layer_size);
		break;
	case PIPE_TEXTURE_CUBE:
	case PIPE_TEXTURE_CUBE_ARRAY:
		so->texconst3 =
			A4XX_TEX_CONST_3_DEPTH(layers / 6) |
			A4XX_TEX_CONST_3_LAYERSZ(rsc->layout.layer_size);
		break;
	case PIPE_TEXTURE_3D:
		so->texconst3 =
			A4XX_TEX_CONST_3_DEPTH(u_minify(prsc->depth0, lvl)) |
			A4XX_TEX_CONST_3_LAYERSZ(fd_resource_slice(rsc, lvl)->size0);
		so->texconst4 = A4XX_TEX_CONST_4_LAYERSZ(
			fd_resource_slice(rsc, prsc->last_level)->size0);
		break;
	default:
		so->texconst3 = 0x00000000;
		break;
	}

	return &so->base;
}

static void
fd4_set_sampler_views(struct pipe_context *pctx, enum pipe_shader_type shader,
		unsigned start, unsigned nr,
		struct pipe_sampler_view **views)
{
	struct fd_context *ctx = fd_context(pctx);
	struct fd4_context *fd4_ctx = fd4_context(ctx);
	uint16_t astc_srgb = 0;
	unsigned i;

	for (i = 0; i < nr; i++) {
		if (views[i]) {
			struct fd4_pipe_sampler_view *view =
					fd4_pipe_sampler_view(views[i]);
			if (view->astc_srgb)
				astc_srgb |= (1 << i);
		}
	}

	fd_set_sampler_views(pctx, shader, start, nr, views);

	if (shader == PIPE_SHADER_FRAGMENT) {
		fd4_ctx->fastc_srgb = astc_srgb;
	} else if (shader == PIPE_SHADER_VERTEX) {
		fd4_ctx->vastc_srgb = astc_srgb;
	}
}

void
fd4_texture_init(struct pipe_context *pctx)
{
	pctx->create_sampler_state = fd4_sampler_state_create;
	pctx->bind_sampler_states = fd4_sampler_states_bind;
	pctx->create_sampler_view = fd4_sampler_view_create;
	pctx->set_sampler_views = fd4_set_sampler_views;
}
