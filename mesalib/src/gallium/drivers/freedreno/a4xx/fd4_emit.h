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

#ifndef FD4_EMIT_H
#define FD4_EMIT_H

#include "pipe/p_context.h"

#include "freedreno_context.h"
#include "fd4_format.h"
#include "fd4_program.h"
#include "ir3_gallium.h"

struct fd_ringbuffer;

void fd4_emit_gmem_restore_tex(struct fd_ringbuffer *ring,
		unsigned nr_bufs, struct pipe_surface **bufs);

/* grouped together emit-state for prog/vertex/state emit: */
struct fd4_emit {
	struct pipe_debug_callback *debug;
	const struct fd_vertex_state *vtx;
	const struct fd_program_stateobj *prog;
	const struct pipe_draw_info *info;
        const struct pipe_draw_indirect_info *indirect;
        const struct pipe_draw_start_count *draw;
	bool binning_pass;
	struct ir3_shader_key key;
	enum fd_dirty_3d_state dirty;

	uint32_t sprite_coord_enable;  /* bitmask */
	bool sprite_coord_mode;
	bool rasterflat;
	bool no_decode_srgb;

	/* cached to avoid repeated lookups of same variants: */
	const struct ir3_shader_variant *vs, *fs;
	/* TODO: other shader stages.. */
};

static inline enum a4xx_color_fmt fd4_emit_format(struct pipe_surface *surf)
{
	if (!surf)
		return 0;
	return fd4_pipe2color(surf->format);
}

static inline const struct ir3_shader_variant *
fd4_emit_get_vp(struct fd4_emit *emit)
{
	if (!emit->vs) {
		struct ir3_shader *shader = emit->prog->vs;
		emit->vs = ir3_shader_variant(shader, emit->key,
				emit->binning_pass, emit->debug);
	}
	return emit->vs;
}

static inline const struct ir3_shader_variant *
fd4_emit_get_fp(struct fd4_emit *emit)
{
	if (!emit->fs) {
		if (emit->binning_pass) {
			/* use dummy stateobj to simplify binning vs non-binning: */
			static const struct ir3_shader_variant binning_fs = {};
			emit->fs = &binning_fs;
		} else {
			struct ir3_shader *shader = emit->prog->fs;
			emit->fs = ir3_shader_variant(shader, emit->key,
					false, emit->debug);
		}
	}
	return emit->fs;
}

void fd4_emit_vertex_bufs(struct fd_ringbuffer *ring, struct fd4_emit *emit);

void fd4_emit_state(struct fd_context *ctx, struct fd_ringbuffer *ring,
		struct fd4_emit *emit);

void fd4_emit_restore(struct fd_batch *batch, struct fd_ringbuffer *ring);

void fd4_emit_init_screen(struct pipe_screen *pscreen);
void fd4_emit_init(struct pipe_context *pctx);

static inline void
fd4_emit_ib(struct fd_ringbuffer *ring, struct fd_ringbuffer *target)
{
	__OUT_IB(ring, true, target);
}

#endif /* FD4_EMIT_H */
