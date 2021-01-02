/*
 * Copyright (C) 2013 Rob Clark <robclark@freedesktop.org>
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
#include "util/format/u_format.h"

#include "freedreno_state.h"
#include "freedreno_resource.h"

#include "fd3_draw.h"
#include "fd3_context.h"
#include "fd3_emit.h"
#include "fd3_program.h"
#include "fd3_format.h"
#include "fd3_zsa.h"

static inline uint32_t
add_sat(uint32_t a, int32_t b)
{
	int64_t ret = (uint64_t)a + (int64_t)b;
	if (ret > ~0U)
		return ~0U;
	if (ret < 0)
		return 0;
	return (uint32_t)ret;
}

static void
draw_impl(struct fd_context *ctx, struct fd_ringbuffer *ring,
		struct fd3_emit *emit, unsigned index_offset)
{
	const struct pipe_draw_info *info = emit->info;
	enum pc_di_primtype primtype = ctx->primtypes[info->mode];

	fd3_emit_state(ctx, ring, emit);

	if (emit->dirty & (FD_DIRTY_VTXBUF | FD_DIRTY_VTXSTATE))
		fd3_emit_vertex_bufs(ring, emit);

	OUT_PKT0(ring, REG_A3XX_PC_VERTEX_REUSE_BLOCK_CNTL, 1);
	OUT_RING(ring, 0x0000000b);             /* PC_VERTEX_REUSE_BLOCK_CNTL */

	OUT_PKT0(ring, REG_A3XX_VFD_INDEX_MIN, 4);
	OUT_RING(ring, add_sat(info->min_index, info->index_bias)); /* VFD_INDEX_MIN */
	OUT_RING(ring, add_sat(info->max_index, info->index_bias)); /* VFD_INDEX_MAX */
	OUT_RING(ring, info->start_instance);   /* VFD_INSTANCEID_OFFSET */
	OUT_RING(ring, info->index_size ? info->index_bias : emit->draw->start); /* VFD_INDEX_OFFSET */

	OUT_PKT0(ring, REG_A3XX_PC_RESTART_INDEX, 1);
	OUT_RING(ring, info->primitive_restart ? /* PC_RESTART_INDEX */
			info->restart_index : 0xffffffff);

	/* points + psize -> spritelist: */
	if (ctx->rasterizer->point_size_per_vertex &&
			fd3_emit_get_vp(emit)->writes_psize &&
			(info->mode == PIPE_PRIM_POINTS))
		primtype = DI_PT_POINTLIST_PSIZE;

	fd_draw_emit(ctx->batch, ring, primtype,
			emit->binning_pass ? IGNORE_VISIBILITY : USE_VISIBILITY,
			info, emit->draw, index_offset);
}

/* fixup dirty shader state in case some "unrelated" (from the state-
 * tracker's perspective) state change causes us to switch to a
 * different variant.
 */
static void
fixup_shader_state(struct fd_context *ctx, struct ir3_shader_key *key)
{
	struct fd3_context *fd3_ctx = fd3_context(ctx);
	struct ir3_shader_key *last_key = &fd3_ctx->last_key;

	if (!ir3_shader_key_equal(last_key, key)) {
		if (ir3_shader_key_changes_fs(last_key, key)) {
			ctx->dirty_shader[PIPE_SHADER_FRAGMENT] |= FD_DIRTY_SHADER_PROG;
			ctx->dirty |= FD_DIRTY_PROG;
		}

		if (ir3_shader_key_changes_vs(last_key, key)) {
			ctx->dirty_shader[PIPE_SHADER_VERTEX] |= FD_DIRTY_SHADER_PROG;
			ctx->dirty |= FD_DIRTY_PROG;
		}

		fd3_ctx->last_key = *key;
	}
}

static bool
fd3_draw_vbo(struct fd_context *ctx, const struct pipe_draw_info *info,
             const struct pipe_draw_indirect_info *indirect,
             const struct pipe_draw_start_count *draw,
             unsigned index_offset)
{
	struct fd3_context *fd3_ctx = fd3_context(ctx);
	struct fd3_emit emit = {
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
			.has_per_samp = (fd3_ctx->fsaturate || fd3_ctx->vsaturate),
			.vsaturate_s = fd3_ctx->vsaturate_s,
			.vsaturate_t = fd3_ctx->vsaturate_t,
			.vsaturate_r = fd3_ctx->vsaturate_r,
			.fsaturate_s = fd3_ctx->fsaturate_s,
			.fsaturate_t = fd3_ctx->fsaturate_t,
			.fsaturate_r = fd3_ctx->fsaturate_r,
		},
		.rasterflat = ctx->rasterizer->flatshade,
		.sprite_coord_enable = ctx->rasterizer->sprite_coord_enable,
		.sprite_coord_mode = ctx->rasterizer->sprite_coord_mode,
	};

	if (fd3_needs_manual_clipping(ctx->prog.vs, ctx->rasterizer))
		emit.key.ucp_enables = ctx->rasterizer->clip_plane_enable;

	fixup_shader_state(ctx, &emit.key);

	unsigned dirty = ctx->dirty;
	const struct ir3_shader_variant *vp = fd3_emit_get_vp(&emit);
	const struct ir3_shader_variant *fp = fd3_emit_get_fp(&emit);

	/* do regular pass first, since that is more likely to fail compiling: */

	if (!vp || !fp)
		return false;

	ctx->stats.vs_regs += ir3_shader_halfregs(vp);
	ctx->stats.fs_regs += ir3_shader_halfregs(fp);

	emit.binning_pass = false;
	emit.dirty = dirty;
	draw_impl(ctx, ctx->batch->draw, &emit, index_offset);

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
fd3_draw_init(struct pipe_context *pctx)
{
	struct fd_context *ctx = fd_context(pctx);
	ctx->draw_vbo = fd3_draw_vbo;
}
