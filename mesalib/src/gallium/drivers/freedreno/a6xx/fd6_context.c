/*
 * Copyright (C) 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
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

#include "freedreno_query_acc.h"

#include "fd6_context.h"
#include "fd6_compute.h"
#include "fd6_blend.h"
#include "fd6_blitter.h"
#include "fd6_draw.h"
#include "fd6_emit.h"
#include "fd6_gmem.h"
#include "fd6_image.h"
#include "fd6_program.h"
#include "fd6_query.h"
#include "fd6_rasterizer.h"
#include "fd6_texture.h"
#include "fd6_zsa.h"

static void
fd6_context_destroy(struct pipe_context *pctx)
{
	struct fd6_context *fd6_ctx = fd6_context(fd_context(pctx));

	u_upload_destroy(fd6_ctx->border_color_uploader);
	pipe_resource_reference(&fd6_ctx->border_color_buf, NULL);

	fd_context_destroy(pctx);

	if (fd6_ctx->vsc_draw_strm)
		fd_bo_del(fd6_ctx->vsc_draw_strm);
	if (fd6_ctx->vsc_prim_strm)
		fd_bo_del(fd6_ctx->vsc_prim_strm);
	fd_bo_del(fd6_ctx->control_mem);

	fd_context_cleanup_common_vbos(&fd6_ctx->base);

	ir3_cache_destroy(fd6_ctx->shader_cache);

	fd6_texture_fini(pctx);

	free(fd6_ctx);
}

static const uint8_t primtypes[] = {
		[PIPE_PRIM_POINTS]                      = DI_PT_POINTLIST,
		[PIPE_PRIM_LINES]                       = DI_PT_LINELIST,
		[PIPE_PRIM_LINE_STRIP]                  = DI_PT_LINESTRIP,
		[PIPE_PRIM_LINE_LOOP]                   = DI_PT_LINELOOP,
		[PIPE_PRIM_TRIANGLES]                   = DI_PT_TRILIST,
		[PIPE_PRIM_TRIANGLE_STRIP]              = DI_PT_TRISTRIP,
		[PIPE_PRIM_TRIANGLE_FAN]                = DI_PT_TRIFAN,
		[PIPE_PRIM_LINES_ADJACENCY]             = DI_PT_LINE_ADJ,
		[PIPE_PRIM_LINE_STRIP_ADJACENCY]        = DI_PT_LINESTRIP_ADJ,
		[PIPE_PRIM_TRIANGLES_ADJACENCY]         = DI_PT_TRI_ADJ,
		[PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY]    = DI_PT_TRISTRIP_ADJ,
		[PIPE_PRIM_PATCHES]                     = DI_PT_PATCHES0,
		[PIPE_PRIM_MAX]                         = DI_PT_RECTLIST,  /* internal clear blits */
};

static void *
fd6_vertex_state_create(struct pipe_context *pctx, unsigned num_elements,
		const struct pipe_vertex_element *elements)
{
	struct fd_context *ctx = fd_context(pctx);

	struct fd6_vertex_stateobj *state = CALLOC_STRUCT(fd6_vertex_stateobj);
	memcpy(state->base.pipe, elements, sizeof(*elements) * num_elements);
	state->base.num_elements = num_elements;
	state->stateobj =
		fd_ringbuffer_new_object(ctx->pipe, 4 * (num_elements * 2 + 1));
	struct fd_ringbuffer *ring = state->stateobj;

	OUT_PKT4(ring, REG_A6XX_VFD_DECODE(0), 2 * num_elements);
	for (int32_t i = 0; i < num_elements; i++) {
		const struct pipe_vertex_element *elem = &elements[i];
		enum pipe_format pfmt = elem->src_format;
		enum a6xx_format fmt = fd6_pipe2vtx(pfmt);
		bool isint = util_format_is_pure_integer(pfmt);
		debug_assert(fmt != FMT6_NONE);

		OUT_RING(ring, A6XX_VFD_DECODE_INSTR_IDX(elem->vertex_buffer_index) |
				A6XX_VFD_DECODE_INSTR_OFFSET(elem->src_offset) |
				A6XX_VFD_DECODE_INSTR_FORMAT(fmt) |
				COND(elem->instance_divisor, A6XX_VFD_DECODE_INSTR_INSTANCED) |
				A6XX_VFD_DECODE_INSTR_SWAP(fd6_pipe2swap(pfmt)) |
				A6XX_VFD_DECODE_INSTR_UNK30 |
				COND(!isint, A6XX_VFD_DECODE_INSTR_FLOAT));
		OUT_RING(ring, MAX2(1, elem->instance_divisor)); /* VFD_DECODE[j].STEP_RATE */
	}

	return state;
}

static void
fd6_vertex_state_delete(struct pipe_context *pctx, void *hwcso)
{
	struct fd6_vertex_stateobj *so = hwcso;

	fd_ringbuffer_del(so->stateobj);
	FREE(hwcso);
}

struct pipe_context *
fd6_context_create(struct pipe_screen *pscreen, void *priv, unsigned flags)
{
	struct fd_screen *screen = fd_screen(pscreen);
	struct fd6_context *fd6_ctx = CALLOC_STRUCT(fd6_context);
	struct pipe_context *pctx;

	if (!fd6_ctx)
		return NULL;

	pctx = &fd6_ctx->base.base;
	pctx->screen = pscreen;

	fd6_ctx->base.dev = fd_device_ref(screen->dev);
	fd6_ctx->base.screen = fd_screen(pscreen);

	pctx->destroy = fd6_context_destroy;
	pctx->create_blend_state = fd6_blend_state_create;
	pctx->create_rasterizer_state = fd6_rasterizer_state_create;
	pctx->create_depth_stencil_alpha_state = fd6_zsa_state_create;
	pctx->create_vertex_elements_state = fd6_vertex_state_create;

	fd6_draw_init(pctx);
	fd6_compute_init(pctx);
	fd6_gmem_init(pctx);
	fd6_texture_init(pctx);
	fd6_prog_init(pctx);
	fd6_emit_init(pctx);
	fd6_query_context_init(pctx);

	pctx = fd_context_init(&fd6_ctx->base, pscreen, primtypes, priv, flags);
	if (!pctx)
		return NULL;

	/* after fd_context_init() to override set_shader_images() */
	fd6_image_init(pctx);

	util_blitter_set_texture_multisample(fd6_ctx->base.blitter, true);

	pctx->delete_vertex_elements_state = fd6_vertex_state_delete;

	/* fd_context_init overwrites delete_rasterizer_state, so set this
	 * here. */
	pctx->delete_rasterizer_state = fd6_rasterizer_state_delete;
	pctx->delete_blend_state = fd6_blend_state_delete;
	pctx->delete_depth_stencil_alpha_state = fd6_zsa_state_delete;

	/* initial sizes for VSC buffers (or rather the per-pipe sizes
	 * which is used to derive entire buffer size:
	 */
	fd6_ctx->vsc_draw_strm_pitch = 0x440;
	fd6_ctx->vsc_prim_strm_pitch = 0x1040;

	fd6_ctx->control_mem = fd_bo_new(screen->dev, 0x1000,
			DRM_FREEDRENO_GEM_TYPE_KMEM, "control");

	memset(fd_bo_map(fd6_ctx->control_mem), 0,
			sizeof(struct fd6_control));

	fd_context_setup_common_vbos(&fd6_ctx->base);

	fd6_blitter_init(pctx);

	fd6_ctx->border_color_uploader = u_upload_create(pctx, 4096, 0,
                                                         PIPE_USAGE_STREAM, 0);

	return pctx;
}
