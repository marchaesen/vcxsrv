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
#include "util/u_prim.h"

#include "freedreno_state.h"
#include "freedreno_resource.h"

#include "fd4_draw.h"
#include "fd4_context.h"
#include "fd4_emit.h"
#include "fd4_program.h"
#include "fd4_format.h"
#include "fd4_zsa.h"


static void
draw_impl(struct fd_context *ctx, struct fd_ringbuffer *ring,
		struct fd4_emit *emit, unsigned index_offset)
{
	const struct pipe_draw_info *info = emit->info;
	enum pc_di_primtype primtype = ctx->primtypes[info->mode];

	fd4_emit_state(ctx, ring, emit);

	if (emit->dirty & (FD_DIRTY_VTXBUF | FD_DIRTY_VTXSTATE))
		fd4_emit_vertex_bufs(ring, emit);

	OUT_PKT0(ring, REG_A4XX_VFD_INDEX_OFFSET, 2);
	OUT_RING(ring, info->index_size ? info->index_bias : emit->draw->start); /* VFD_INDEX_OFFSET */
	OUT_RING(ring, info->start_instance);   /* ??? UNKNOWN_2209 */

	OUT_PKT0(ring, REG_A4XX_PC_RESTART_INDEX, 1);
	OUT_RING(ring, info->primitive_restart ? /* PC_RESTART_INDEX */
			info->restart_index : 0xffffffff);

	/* points + psize -> spritelist: */
	if (ctx->rasterizer->point_size_per_vertex &&
			fd4_emit_get_vp(emit)->writes_psize &&
			(info->mode == PIPE_PRIM_POINTS))
		primtype = DI_PT_POINTLIST_PSIZE;

	fd4_draw_emit(ctx->batch, ring, primtype,
			emit->binning_pass ? IGNORE_VISIBILITY : USE_VISIBILITY,
			info, emit->indirect, emit->draw, index_offset);
}

/* fixup dirty shader state in case some "unrelated" (from the state-
 * tracker's perspective) state change causes us to switch to a
 * different variant.
 */
static void
fixup_shader_state(struct fd_context *ctx, struct ir3_shader_key *key)
{
	struct fd4_context *fd4_ctx = fd4_context(ctx);
	struct ir3_shader_key *last_key = &fd4_ctx->last_key;

	if (!ir3_shader_key_equal(last_key, key)) {
		if (ir3_shader_key_changes_fs(last_key, key)) {
			ctx->dirty_shader[PIPE_SHADER_FRAGMENT] |= FD_DIRTY_SHADER_PROG;
			ctx->dirty |= FD_DIRTY_PROG;
		}

		if (ir3_shader_key_changes_vs(last_key, key)) {
			ctx->dirty_shader[PIPE_SHADER_VERTEX] |= FD_DIRTY_SHADER_PROG;
			ctx->dirty |= FD_DIRTY_PROG;
		}

		fd4_ctx->last_key = *key;
	}
}

static bool
fd4_draw_vbo(struct fd_context *ctx, const struct pipe_draw_info *info,
             const struct pipe_draw_indirect_info *indirect,
             const struct pipe_draw_start_count *draw,
             unsigned index_offset)
{
	struct fd4_context *fd4_ctx = fd4_context(ctx);
	struct fd4_emit emit = {
		.debug = &ctx->debug,
		.vtx  = &ctx->vtx,
		.prog = &ctx->prog,
		.info = info,
                .indirect = indirect,
                .draw = draw,
		.key = {
			.color_two_side = ctx->rasterizer->light_twoside,
			.vclamp_color = ctx->rasterizer->clamp_vertex_color,
			.fclamp_color = ctx->rasterizer->clamp_fragment_color,
			.rasterflat = ctx->rasterizer->flatshade,
			.ucp_enables = ctx->rasterizer->clip_plane_enable,
			.has_per_samp = (fd4_ctx->fsaturate || fd4_ctx->vsaturate ||
					fd4_ctx->fastc_srgb || fd4_ctx->vastc_srgb),
			.vsaturate_s = fd4_ctx->vsaturate_s,
			.vsaturate_t = fd4_ctx->vsaturate_t,
			.vsaturate_r = fd4_ctx->vsaturate_r,
			.fsaturate_s = fd4_ctx->fsaturate_s,
			.fsaturate_t = fd4_ctx->fsaturate_t,
			.fsaturate_r = fd4_ctx->fsaturate_r,
			.vastc_srgb = fd4_ctx->vastc_srgb,
			.fastc_srgb = fd4_ctx->fastc_srgb,
		},
		.rasterflat = ctx->rasterizer->flatshade,
		.sprite_coord_enable = ctx->rasterizer->sprite_coord_enable,
		.sprite_coord_mode = ctx->rasterizer->sprite_coord_mode,
	};

	fixup_shader_state(ctx, &emit.key);

	enum fd_dirty_3d_state dirty = ctx->dirty;
	const struct ir3_shader_variant *vp = fd4_emit_get_vp(&emit);
	const struct ir3_shader_variant *fp = fd4_emit_get_fp(&emit);

	/* do regular pass first, since that is more likely to fail compiling: */

	if (!vp || !fp)
		return false;

	ctx->stats.vs_regs += ir3_shader_halfregs(vp);
	ctx->stats.fs_regs += ir3_shader_halfregs(fp);

	emit.binning_pass = false;
	emit.dirty = dirty;

	struct fd_ringbuffer *ring = ctx->batch->draw;

	if (ctx->rasterizer->rasterizer_discard) {
		fd_wfi(ctx->batch, ring);
		OUT_PKT3(ring, CP_REG_RMW, 3);
		OUT_RING(ring, REG_A4XX_RB_RENDER_CONTROL);
		OUT_RING(ring, ~A4XX_RB_RENDER_CONTROL_DISABLE_COLOR_PIPE);
		OUT_RING(ring, A4XX_RB_RENDER_CONTROL_DISABLE_COLOR_PIPE);
	}

	draw_impl(ctx, ctx->batch->draw, &emit, index_offset);

	if (ctx->rasterizer->rasterizer_discard) {
		fd_wfi(ctx->batch, ring);
		OUT_PKT3(ring, CP_REG_RMW, 3);
		OUT_RING(ring, REG_A4XX_RB_RENDER_CONTROL);
		OUT_RING(ring, ~A4XX_RB_RENDER_CONTROL_DISABLE_COLOR_PIPE);
		OUT_RING(ring, 0);
	}

	/* and now binning pass: */
	emit.binning_pass = true;
	emit.dirty = dirty & ~(FD_DIRTY_BLEND);
	emit.vs = NULL;   /* we changed key so need to refetch vs */
	emit.fs = NULL;
	draw_impl(ctx, ctx->batch->binning, &emit, index_offset);

	fd_context_all_clean(ctx);

	return true;
}

void
fd4_draw_init(struct pipe_context *pctx)
{
	struct fd_context *ctx = fd_context(pctx);
	ctx->draw_vbo = fd4_draw_vbo;
}
