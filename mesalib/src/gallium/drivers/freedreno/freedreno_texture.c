/*
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
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

#include "freedreno_texture.h"
#include "freedreno_context.h"
#include "freedreno_resource.h"
#include "freedreno_util.h"

static void
fd_sampler_state_delete(struct pipe_context *pctx, void *hwcso)
{
	FREE(hwcso);
}

static void
fd_sampler_view_destroy(struct pipe_context *pctx,
		struct pipe_sampler_view *view)
{
	pipe_resource_reference(&view->texture, NULL);
	FREE(view);
}

static void bind_sampler_states(struct fd_texture_stateobj *tex,
		unsigned start, unsigned nr, void **hwcso)
{
	unsigned i;

	for (i = 0; i < nr; i++) {
		unsigned p = i + start;
		tex->samplers[p] = hwcso[i];
		if (tex->samplers[p])
			tex->valid_samplers |= (1 << p);
		else
			tex->valid_samplers &= ~(1 << p);
	}

	tex->num_samplers = util_last_bit(tex->valid_samplers);
}

static void set_sampler_views(struct fd_texture_stateobj *tex,
		unsigned start, unsigned nr, struct pipe_sampler_view **views)
{
	unsigned i;
	unsigned samplers = 0;

	for (i = 0; i < nr; i++) {
		struct pipe_sampler_view *view = views ? views[i] : NULL;
		unsigned p = i + start;
		pipe_sampler_view_reference(&tex->textures[p], view);
		if (tex->textures[p]) {
			fd_resource_set_usage(tex->textures[p]->texture, FD_DIRTY_TEX);
			tex->valid_textures |= (1 << p);
		} else {
			tex->valid_textures &= ~(1 << p);
		}
	}

	tex->num_textures = util_last_bit(tex->valid_textures);

	for (i = 0; i < tex->num_textures; i++) {
		uint nr_samples = fd_resource_nr_samples(tex->textures[i]->texture);
		samplers |= (nr_samples >> 1) << (i * 2);
	}

	tex->samples = samplers;
}

void
fd_sampler_states_bind(struct pipe_context *pctx,
		enum pipe_shader_type shader, unsigned start,
		unsigned nr, void **hwcso)
{
	struct fd_context *ctx = fd_context(pctx);

	bind_sampler_states(&ctx->tex[shader], start, nr, hwcso);
	ctx->dirty_shader[shader] |= FD_DIRTY_SHADER_TEX;
	ctx->dirty |= FD_DIRTY_TEX;
}

void
fd_set_sampler_views(struct pipe_context *pctx, enum pipe_shader_type shader,
		unsigned start, unsigned nr,
		struct pipe_sampler_view **views)
{
	struct fd_context *ctx = fd_context(pctx);

	set_sampler_views(&ctx->tex[shader], start, nr, views);
	ctx->dirty_shader[shader] |= FD_DIRTY_SHADER_TEX;
	ctx->dirty |= FD_DIRTY_TEX;
}

void
fd_texture_init(struct pipe_context *pctx)
{
	if (!pctx->delete_sampler_state)
		pctx->delete_sampler_state = fd_sampler_state_delete;
	if (!pctx->sampler_view_destroy)
		pctx->sampler_view_destroy = fd_sampler_view_destroy;
}

/* helper for setting up border-color buffer for a3xx/a4xx: */
void
fd_setup_border_colors(struct fd_texture_stateobj *tex, void *ptr,
		unsigned offset)
{
	unsigned i, j;

	for (i = 0; i < tex->num_samplers; i++) {
		struct pipe_sampler_state *sampler = tex->samplers[i];
		uint16_t *bcolor = (uint16_t *)((uint8_t *)ptr +
				(BORDERCOLOR_SIZE * offset) +
				(BORDERCOLOR_SIZE * i));
		uint32_t *bcolor32 = (uint32_t *)&bcolor[16];

		if (!sampler)
			continue;

		/*
		 * XXX HACK ALERT XXX
		 *
		 * The border colors need to be swizzled in a particular
		 * format-dependent order. Even though samplers don't know about
		 * formats, we can assume that with a GL state tracker, there's a
		 * 1:1 correspondence between sampler and texture. Take advantage
		 * of that knowledge.
		 */
		if (i < tex->num_textures && tex->textures[i]) {
			const struct util_format_description *desc =
					util_format_description(tex->textures[i]->format);
			for (j = 0; j < 4; j++) {
				if (desc->swizzle[j] >= 4)
					continue;

				const struct util_format_channel_description *chan =
					&desc->channel[desc->swizzle[j]];
				if (chan->pure_integer) {
					bcolor32[desc->swizzle[j] + 4] = sampler->border_color.i[j];
					bcolor[desc->swizzle[j] + 8] = sampler->border_color.i[j];
				} else {
					bcolor32[desc->swizzle[j]] = fui(sampler->border_color.f[j]);
					bcolor[desc->swizzle[j]] =
						_mesa_float_to_half(sampler->border_color.f[j]);
				}
			}
		}
	}
}
