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

#include "freedreno_query_hw.h"

#include "fd4_context.h"
#include "fd4_blend.h"
#include "fd4_draw.h"
#include "fd4_emit.h"
#include "fd4_gmem.h"
#include "fd4_program.h"
#include "fd4_query.h"
#include "fd4_rasterizer.h"
#include "fd4_texture.h"
#include "fd4_zsa.h"

static void
fd4_context_destroy(struct pipe_context *pctx)
{
	struct fd4_context *fd4_ctx = fd4_context(fd_context(pctx));

	u_upload_destroy(fd4_ctx->border_color_uploader);
	pipe_resource_reference(&fd4_ctx->border_color_buf, NULL);

	fd_context_destroy(pctx);

	fd_bo_del(fd4_ctx->vs_pvt_mem);
	fd_bo_del(fd4_ctx->fs_pvt_mem);
	fd_bo_del(fd4_ctx->vsc_size_mem);

	fd_context_cleanup_common_vbos(&fd4_ctx->base);

	fd_hw_query_fini(pctx);

	free(fd4_ctx);
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
fd4_context_create(struct pipe_screen *pscreen, void *priv, unsigned flags)
{
	struct fd_screen *screen = fd_screen(pscreen);
	struct fd4_context *fd4_ctx = CALLOC_STRUCT(fd4_context);
	struct pipe_context *pctx;

	if (!fd4_ctx)
		return NULL;

	pctx = &fd4_ctx->base.base;
	pctx->screen = pscreen;

	fd4_ctx->base.dev = fd_device_ref(screen->dev);
	fd4_ctx->base.screen = fd_screen(pscreen);

	pctx->destroy = fd4_context_destroy;
	pctx->create_blend_state = fd4_blend_state_create;
	pctx->create_rasterizer_state = fd4_rasterizer_state_create;
	pctx->create_depth_stencil_alpha_state = fd4_zsa_state_create;

	fd4_draw_init(pctx);
	fd4_gmem_init(pctx);
	fd4_texture_init(pctx);
	fd4_prog_init(pctx);
	fd4_emit_init(pctx);

	pctx = fd_context_init(&fd4_ctx->base, pscreen, primtypes, priv, flags);
	if (!pctx)
		return NULL;

	fd_hw_query_init(pctx);

	fd4_ctx->vs_pvt_mem = fd_bo_new(screen->dev, 0x2000,
			DRM_FREEDRENO_GEM_TYPE_KMEM, "vs_pvt");

	fd4_ctx->fs_pvt_mem = fd_bo_new(screen->dev, 0x2000,
			DRM_FREEDRENO_GEM_TYPE_KMEM, "fs_pvt");

	fd4_ctx->vsc_size_mem = fd_bo_new(screen->dev, 0x1000,
			DRM_FREEDRENO_GEM_TYPE_KMEM, "vsc_size");

	fd_context_setup_common_vbos(&fd4_ctx->base);

	fd4_query_context_init(pctx);

	fd4_ctx->border_color_uploader = u_upload_create(pctx, 4096, 0,
                                                         PIPE_USAGE_STREAM, 0);

	return pctx;
}
