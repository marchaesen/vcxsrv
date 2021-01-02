/*
 * Copyright (C) 2016 Rob Clark <robclark@freedesktop.org>
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

#include "fd5_context.h"
#include "fd5_blend.h"
#include "fd5_blitter.h"
#include "fd5_compute.h"
#include "fd5_draw.h"
#include "fd5_emit.h"
#include "fd5_gmem.h"
#include "fd5_program.h"
#include "fd5_query.h"
#include "fd5_rasterizer.h"
#include "fd5_texture.h"
#include "fd5_zsa.h"

static void
fd5_context_destroy(struct pipe_context *pctx)
{
	struct fd5_context *fd5_ctx = fd5_context(fd_context(pctx));

	u_upload_destroy(fd5_ctx->border_color_uploader);
	pipe_resource_reference(&fd5_ctx->border_color_buf, NULL);

	fd_context_destroy(pctx);

	fd_bo_del(fd5_ctx->vsc_size_mem);
	fd_bo_del(fd5_ctx->blit_mem);

	fd_context_cleanup_common_vbos(&fd5_ctx->base);

	free(fd5_ctx);
}

static const uint8_t primtypes[] = {
		[PIPE_PRIM_POINTS]         = DI_PT_POINTLIST,
		[PIPE_PRIM_LINES]          = DI_PT_LINELIST,
		[PIPE_PRIM_LINE_STRIP]     = DI_PT_LINESTRIP,
		[PIPE_PRIM_LINE_LOOP]      = DI_PT_LINELOOP,
		[PIPE_PRIM_TRIANGLES]      = DI_PT_TRILIST,
		[PIPE_PRIM_TRIANGLE_STRIP] = DI_PT_TRISTRIP,
		[PIPE_PRIM_TRIANGLE_FAN]   = DI_PT_TRIFAN,
		[PIPE_PRIM_MAX]            = DI_PT_RECTLIST,  /* internal clear blits */
};

struct pipe_context *
fd5_context_create(struct pipe_screen *pscreen, void *priv, unsigned flags)
{
	struct fd_screen *screen = fd_screen(pscreen);
	struct fd5_context *fd5_ctx = CALLOC_STRUCT(fd5_context);
	struct pipe_context *pctx;

	if (!fd5_ctx)
		return NULL;

	pctx = &fd5_ctx->base.base;
	pctx->screen = pscreen;

	fd5_ctx->base.dev = fd_device_ref(screen->dev);
	fd5_ctx->base.screen = fd_screen(pscreen);

	pctx->destroy = fd5_context_destroy;
	pctx->create_blend_state = fd5_blend_state_create;
	pctx->create_rasterizer_state = fd5_rasterizer_state_create;
	pctx->create_depth_stencil_alpha_state = fd5_zsa_state_create;

	fd5_draw_init(pctx);
	fd5_compute_init(pctx);
	fd5_gmem_init(pctx);
	fd5_texture_init(pctx);
	fd5_prog_init(pctx);
	fd5_emit_init(pctx);

	if (!(fd_mesa_debug & FD_DBG_NOBLIT))
		fd5_ctx->base.blit = fd5_blitter_blit;

	pctx = fd_context_init(&fd5_ctx->base, pscreen, primtypes, priv, flags);
	if (!pctx)
		return NULL;

	util_blitter_set_texture_multisample(fd5_ctx->base.blitter, true);

	fd5_ctx->vsc_size_mem = fd_bo_new(screen->dev, 0x1000,
			DRM_FREEDRENO_GEM_TYPE_KMEM, "vsc_size");

	fd5_ctx->blit_mem = fd_bo_new(screen->dev, 0x1000,
			DRM_FREEDRENO_GEM_TYPE_KMEM, "blit");

	fd_context_setup_common_vbos(&fd5_ctx->base);

	fd5_query_context_init(pctx);

	fd5_ctx->border_color_uploader = u_upload_create(pctx, 4096, 0,
                                                         PIPE_USAGE_STREAM, 0);

	return pctx;
}
