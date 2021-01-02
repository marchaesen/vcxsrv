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

#include "pipe/p_state.h"
#include "util/u_string.h"
#include "util/u_memory.h"
#include "util/u_helpers.h"
#include "util/format/u_format.h"
#include "util/u_viewport.h"

#include "freedreno_resource.h"
#include "freedreno_state.h"
#include "freedreno_tracepoints.h"
#include "freedreno_query_hw.h"
#include "common/freedreno_guardband.h"

#include "fd6_emit.h"
#include "fd6_blend.h"
#include "fd6_const.h"
#include "fd6_context.h"
#include "fd6_image.h"
#include "fd6_pack.h"
#include "fd6_program.h"
#include "fd6_rasterizer.h"
#include "fd6_texture.h"
#include "fd6_format.h"
#include "fd6_zsa.h"

/* Border color layout is diff from a4xx/a5xx.. if it turns out to be
 * the same as a6xx then move this somewhere common ;-)
 *
 * Entry layout looks like (total size, 0x60 bytes):
 */

struct PACKED bcolor_entry {
	uint32_t fp32[4];
	uint16_t ui16[4];
	int16_t  si16[4];
	uint16_t fp16[4];
	uint16_t rgb565;
	uint16_t rgb5a1;
	uint16_t rgba4;
	uint8_t __pad0[2];
	uint8_t  ui8[4];
	int8_t   si8[4];
	uint32_t rgb10a2;
	uint32_t z24; /* also s8? */
	uint16_t srgb[4];      /* appears to duplicate fp16[], but clamped, used for srgb */
	uint8_t  __pad1[56];
};

#define FD6_BORDER_COLOR_SIZE        sizeof(struct bcolor_entry)
#define FD6_BORDER_COLOR_UPLOAD_SIZE (2 * PIPE_MAX_SAMPLERS * FD6_BORDER_COLOR_SIZE)

static void
setup_border_colors(struct fd_texture_stateobj *tex, struct bcolor_entry *entries)
{
	unsigned i, j;
	STATIC_ASSERT(sizeof(struct bcolor_entry) == FD6_BORDER_COLOR_SIZE);

	for (i = 0; i < tex->num_samplers; i++) {
		struct bcolor_entry *e = &entries[i];
		struct pipe_sampler_state *sampler = tex->samplers[i];
		union pipe_color_union *bc;

		if (!sampler)
			continue;

		bc = &sampler->border_color;

		/*
		 * XXX HACK ALERT XXX
		 *
		 * The border colors need to be swizzled in a particular
		 * format-dependent order. Even though samplers don't know about
		 * formats, we can assume that with a GL state tracker, there's a
		 * 1:1 correspondence between sampler and texture. Take advantage
		 * of that knowledge.
		 */
		if ((i >= tex->num_textures) || !tex->textures[i])
			continue;

		struct pipe_sampler_view *view = tex->textures[i];
		enum pipe_format format = view->format;
		const struct util_format_description *desc =
				util_format_description(format);

		e->rgb565 = 0;
		e->rgb5a1 = 0;
		e->rgba4 = 0;
		e->rgb10a2 = 0;
		e->z24 = 0;

		unsigned char swiz[4];

		fd6_tex_swiz(format, swiz,
				view->swizzle_r, view->swizzle_g,
				view->swizzle_b, view->swizzle_a);

		for (j = 0; j < 4; j++) {
			int c = swiz[j];
			int cd = c;

			/*
			 * HACK: for PIPE_FORMAT_X24S8_UINT we end up w/ the
			 * stencil border color value in bc->ui[0] but according
			 * to desc->swizzle and desc->channel, the .x/.w component
			 * is NONE and the stencil value is in the y component.
			 * Meanwhile the hardware wants this in the .w component
			 * for x24s8 and the .x component for x32_s8x24.
			 */
			if ((format == PIPE_FORMAT_X24S8_UINT) ||
					(format == PIPE_FORMAT_X32_S8X24_UINT)) {
				if (j == 0) {
					c = 1;
					cd = (format == PIPE_FORMAT_X32_S8X24_UINT) ? 0 : 3;
				} else {
					continue;
				}
			}

			if (c >= 4)
				continue;

			if (desc->channel[c].pure_integer) {
				uint16_t clamped;
				switch (desc->channel[c].size) {
				case 2:
					assert(desc->channel[c].type == UTIL_FORMAT_TYPE_UNSIGNED);
					clamped = CLAMP(bc->ui[j], 0, 0x3);
					break;
				case 8:
					if (desc->channel[c].type == UTIL_FORMAT_TYPE_SIGNED)
						clamped = CLAMP(bc->i[j], -128, 127);
					else
						clamped = CLAMP(bc->ui[j], 0, 255);
					break;
				case 10:
					assert(desc->channel[c].type == UTIL_FORMAT_TYPE_UNSIGNED);
					clamped = CLAMP(bc->ui[j], 0, 0x3ff);
					break;
				case 16:
					if (desc->channel[c].type == UTIL_FORMAT_TYPE_SIGNED)
						clamped = CLAMP(bc->i[j], -32768, 32767);
					else
						clamped = CLAMP(bc->ui[j], 0, 65535);
					break;
				default:
					assert(!"Unexpected bit size");
				case 32:
					clamped = 0;
					break;
				}
				e->fp32[cd] = bc->ui[j];
				e->fp16[cd] = clamped;
			} else {
				float f = bc->f[j];
				float f_u = CLAMP(f, 0, 1);
				float f_s = CLAMP(f, -1, 1);

				e->fp32[c] = fui(f);
				e->fp16[c] = _mesa_float_to_half(f);
				e->srgb[c] = _mesa_float_to_half(f_u);
				e->ui16[c] = f_u * 0xffff;
				e->si16[c] = f_s * 0x7fff;
				e->ui8[c]  = f_u * 0xff;
				e->si8[c]  = f_s * 0x7f;
				if (c == 1)
					e->rgb565 |= (int)(f_u * 0x3f) << 5;
				else if (c < 3)
					e->rgb565 |= (int)(f_u * 0x1f) << (c ? 11 : 0);
				if (c == 3)
					e->rgb5a1 |= (f_u > 0.5) ? 0x8000 : 0;
				else
					e->rgb5a1 |= (int)(f_u * 0x1f) << (c * 5);
				if (c == 3)
					e->rgb10a2 |= (int)(f_u * 0x3) << 30;
				else
					e->rgb10a2 |= (int)(f_u * 0x3ff) << (c * 10);
				e->rgba4 |= (int)(f_u * 0xf) << (c * 4);
				if (c == 0)
					e->z24 = f_u * 0xffffff;
			}
		}

#ifdef DEBUG
		memset(&e->__pad0, 0, sizeof(e->__pad0));
		memset(&e->__pad1, 0, sizeof(e->__pad1));
#endif
	}
}

static void
emit_border_color(struct fd_context *ctx, struct fd_ringbuffer *ring)
{
	struct fd6_context *fd6_ctx = fd6_context(ctx);
	struct bcolor_entry *entries;
	unsigned off;
	void *ptr;

	STATIC_ASSERT(sizeof(struct bcolor_entry) == FD6_BORDER_COLOR_SIZE);

	u_upload_alloc(fd6_ctx->border_color_uploader,
			0, FD6_BORDER_COLOR_UPLOAD_SIZE,
			FD6_BORDER_COLOR_UPLOAD_SIZE, &off,
			&fd6_ctx->border_color_buf,
			&ptr);

	entries = ptr;

	setup_border_colors(&ctx->tex[PIPE_SHADER_VERTEX], &entries[0]);
	setup_border_colors(&ctx->tex[PIPE_SHADER_FRAGMENT],
			&entries[ctx->tex[PIPE_SHADER_VERTEX].num_samplers]);

	OUT_PKT4(ring, REG_A6XX_SP_TP_BORDER_COLOR_BASE_ADDR_LO, 2);
	OUT_RELOC(ring, fd_resource(fd6_ctx->border_color_buf)->bo, off, 0, 0);

	u_upload_unmap(fd6_ctx->border_color_uploader);
}

static void
fd6_emit_fb_tex(struct fd_ringbuffer *state, struct fd_context *ctx)
{
	struct pipe_framebuffer_state *pfb = &ctx->batch->framebuffer;
	struct pipe_surface *psurf = pfb->cbufs[0];
	struct fd_resource *rsc = fd_resource(psurf->texture);

	uint32_t texconst0 = fd6_tex_const_0(psurf->texture, psurf->u.tex.level,
			psurf->format, PIPE_SWIZZLE_X, PIPE_SWIZZLE_Y,
			PIPE_SWIZZLE_Z, PIPE_SWIZZLE_W);

	/* always TILE6_2 mode in GMEM.. which also means no swap: */
	texconst0 &= ~(A6XX_TEX_CONST_0_SWAP__MASK | A6XX_TEX_CONST_0_TILE_MODE__MASK);
	texconst0 |= A6XX_TEX_CONST_0_TILE_MODE(TILE6_2);

	OUT_RING(state, texconst0);
	OUT_RING(state, A6XX_TEX_CONST_1_WIDTH(pfb->width) |
			A6XX_TEX_CONST_1_HEIGHT(pfb->height));
	OUT_RINGP(state, A6XX_TEX_CONST_2_TYPE(A6XX_TEX_2D),
			&ctx->batch->fb_read_patches);
	OUT_RING(state, A6XX_TEX_CONST_3_ARRAY_PITCH(rsc->layout.layer_size));

	OUT_RING(state, A6XX_TEX_CONST_4_BASE_LO(ctx->screen->gmem_base));
	OUT_RING(state, A6XX_TEX_CONST_5_BASE_HI(ctx->screen->gmem_base >> 32) |
			A6XX_TEX_CONST_5_DEPTH(1));
	OUT_RING(state, 0);   /* texconst6 */
	OUT_RING(state, 0);   /* texconst7 */
	OUT_RING(state, 0);   /* texconst8 */
	OUT_RING(state, 0);   /* texconst9 */
	OUT_RING(state, 0);   /* texconst10 */
	OUT_RING(state, 0);   /* texconst11 */
	OUT_RING(state, 0);
	OUT_RING(state, 0);
	OUT_RING(state, 0);
	OUT_RING(state, 0);
}

bool
fd6_emit_textures(struct fd_pipe *pipe, struct fd_ringbuffer *ring,
		enum pipe_shader_type type, struct fd_texture_stateobj *tex,
		unsigned bcolor_offset,
		/* can be NULL if no image/SSBO/fb state to merge in: */
		const struct ir3_shader_variant *v, struct fd_context *ctx)
{
	bool needs_border = false;
	unsigned opcode, tex_samp_reg, tex_const_reg, tex_count_reg;
	enum a6xx_state_block sb;

	switch (type) {
	case PIPE_SHADER_VERTEX:
		sb = SB6_VS_TEX;
		opcode = CP_LOAD_STATE6_GEOM;
		tex_samp_reg = REG_A6XX_SP_VS_TEX_SAMP_LO;
		tex_const_reg = REG_A6XX_SP_VS_TEX_CONST_LO;
		tex_count_reg = REG_A6XX_SP_VS_TEX_COUNT;
		break;
	case PIPE_SHADER_TESS_CTRL:
		sb = SB6_HS_TEX;
		opcode = CP_LOAD_STATE6_GEOM;
		tex_samp_reg = REG_A6XX_SP_HS_TEX_SAMP_LO;
		tex_const_reg = REG_A6XX_SP_HS_TEX_CONST_LO;
		tex_count_reg = REG_A6XX_SP_HS_TEX_COUNT;
		break;
	case PIPE_SHADER_TESS_EVAL:
		sb = SB6_DS_TEX;
		opcode = CP_LOAD_STATE6_GEOM;
		tex_samp_reg = REG_A6XX_SP_DS_TEX_SAMP_LO;
		tex_const_reg = REG_A6XX_SP_DS_TEX_CONST_LO;
		tex_count_reg = REG_A6XX_SP_DS_TEX_COUNT;
		break;
	case PIPE_SHADER_GEOMETRY:
		sb = SB6_GS_TEX;
		opcode = CP_LOAD_STATE6_GEOM;
		tex_samp_reg = REG_A6XX_SP_GS_TEX_SAMP_LO;
		tex_const_reg = REG_A6XX_SP_GS_TEX_CONST_LO;
		tex_count_reg = REG_A6XX_SP_GS_TEX_COUNT;
		break;
	case PIPE_SHADER_FRAGMENT:
		sb = SB6_FS_TEX;
		opcode = CP_LOAD_STATE6_FRAG;
		tex_samp_reg = REG_A6XX_SP_FS_TEX_SAMP_LO;
		tex_const_reg = REG_A6XX_SP_FS_TEX_CONST_LO;
		tex_count_reg = REG_A6XX_SP_FS_TEX_COUNT;
		break;
	case PIPE_SHADER_COMPUTE:
		sb = SB6_CS_TEX;
		opcode = CP_LOAD_STATE6_FRAG;
		tex_samp_reg = REG_A6XX_SP_CS_TEX_SAMP_LO;
		tex_const_reg = REG_A6XX_SP_CS_TEX_CONST_LO;
		tex_count_reg = REG_A6XX_SP_CS_TEX_COUNT;
		break;
	default:
		unreachable("bad state block");
	}

	if (tex->num_samplers > 0) {
		struct fd_ringbuffer *state =
			fd_ringbuffer_new_object(pipe, tex->num_samplers * 4 * 4);
		for (unsigned i = 0; i < tex->num_samplers; i++) {
			static const struct fd6_sampler_stateobj dummy_sampler = {};
			const struct fd6_sampler_stateobj *sampler = tex->samplers[i] ?
				fd6_sampler_stateobj(tex->samplers[i]) : &dummy_sampler;
			OUT_RING(state, sampler->texsamp0);
			OUT_RING(state, sampler->texsamp1);
			OUT_RING(state, sampler->texsamp2 |
				A6XX_TEX_SAMP_2_BCOLOR(i + bcolor_offset));
			OUT_RING(state, sampler->texsamp3);
			needs_border |= sampler->needs_border;
		}

		/* output sampler state: */
		OUT_PKT7(ring, opcode, 3);
		OUT_RING(ring, CP_LOAD_STATE6_0_DST_OFF(0) |
			CP_LOAD_STATE6_0_STATE_TYPE(ST6_SHADER) |
			CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
			CP_LOAD_STATE6_0_STATE_BLOCK(sb) |
			CP_LOAD_STATE6_0_NUM_UNIT(tex->num_samplers));
		OUT_RB(ring, state); /* SRC_ADDR_LO/HI */

		OUT_PKT4(ring, tex_samp_reg, 2);
		OUT_RB(ring, state); /* SRC_ADDR_LO/HI */

		fd_ringbuffer_del(state);
	}

	unsigned num_merged_textures = tex->num_textures;
	unsigned num_textures = tex->num_textures;
	if (v) {
		num_merged_textures += v->image_mapping.num_tex;

		if (v->fb_read)
			num_merged_textures++;

		/* There could be more bound textures than what the shader uses.
		 * Which isn't known at shader compile time.  So in the case we
		 * are merging tex state, only emit the textures that the shader
		 * uses (since the image/SSBO related tex state comes immediately
		 * after)
		 */
		num_textures = v->image_mapping.tex_base;
	}

	if (num_merged_textures > 0) {
		struct fd_ringbuffer *state =
			fd_ringbuffer_new_object(pipe, num_merged_textures * 16 * 4);
		for (unsigned i = 0; i < num_textures; i++) {
			static const struct fd6_pipe_sampler_view dummy_view = {};
			const struct fd6_pipe_sampler_view *view = tex->textures[i] ?
				fd6_pipe_sampler_view(tex->textures[i]) : &dummy_view;

			OUT_RING(state, view->texconst0);
			OUT_RING(state, view->texconst1);
			OUT_RING(state, view->texconst2);
			OUT_RING(state, view->texconst3);

			if (view->ptr1) {
				OUT_RELOC(state, view->ptr1->bo, view->offset1,
					(uint64_t)view->texconst5 << 32, 0);
			} else {
				OUT_RING(state, 0x00000000);
				OUT_RING(state, view->texconst5);
			}

			OUT_RING(state, view->texconst6);

			if (view->ptr2) {
				OUT_RELOC(state, view->ptr2->bo, view->offset2, 0, 0);
			} else {
				OUT_RING(state, 0);
				OUT_RING(state, 0);
			}

			OUT_RING(state, view->texconst9);
			OUT_RING(state, view->texconst10);
			OUT_RING(state, view->texconst11);
			OUT_RING(state, 0);
			OUT_RING(state, 0);
			OUT_RING(state, 0);
			OUT_RING(state, 0);
		}

		if (v) {
			const struct ir3_ibo_mapping *mapping = &v->image_mapping;
			struct fd_shaderbuf_stateobj *buf = &ctx->shaderbuf[type];
			struct fd_shaderimg_stateobj *img = &ctx->shaderimg[type];

			for (unsigned i = 0; i < mapping->num_tex; i++) {
				unsigned idx = mapping->tex_to_image[i];
				if (idx & IBO_SSBO) {
					fd6_emit_ssbo_tex(state, &buf->sb[idx & ~IBO_SSBO]);
				} else {
					fd6_emit_image_tex(state, &img->si[idx]);
				}
			}

			if (v->fb_read) {
				fd6_emit_fb_tex(state, ctx);
			}
		}

		/* emit texture state: */
		OUT_PKT7(ring, opcode, 3);
		OUT_RING(ring, CP_LOAD_STATE6_0_DST_OFF(0) |
			CP_LOAD_STATE6_0_STATE_TYPE(ST6_CONSTANTS) |
			CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
			CP_LOAD_STATE6_0_STATE_BLOCK(sb) |
			CP_LOAD_STATE6_0_NUM_UNIT(num_merged_textures));
		OUT_RB(ring, state); /* SRC_ADDR_LO/HI */

		OUT_PKT4(ring, tex_const_reg, 2);
		OUT_RB(ring, state); /* SRC_ADDR_LO/HI */

		fd_ringbuffer_del(state);
	}

	OUT_PKT4(ring, tex_count_reg, 1);
	OUT_RING(ring, num_merged_textures);

	return needs_border;
}

/* Emits combined texture state, which also includes any Image/SSBO
 * related texture state merged in (because we must have all texture
 * state for a given stage in a single buffer).  In the fast-path, if
 * we don't need to merge in any image/ssbo related texture state, we
 * just use cached texture stateobj.  Otherwise we generate a single-
 * use stateobj.
 *
 * TODO Is there some sane way we can still use cached texture stateobj
 * with image/ssbo in use?
 *
 * returns whether border_color is required:
 */
static bool
fd6_emit_combined_textures(struct fd_ringbuffer *ring, struct fd6_emit *emit,
		enum pipe_shader_type type, const struct ir3_shader_variant *v)
{
	struct fd_context *ctx = emit->ctx;
	bool needs_border = false;

	static const struct {
		enum fd6_state_id state_id;
		unsigned enable_mask;
	} s[PIPE_SHADER_TYPES] = {
		[PIPE_SHADER_VERTEX]    = { FD6_GROUP_VS_TEX, ENABLE_ALL },
		[PIPE_SHADER_TESS_CTRL]  = { FD6_GROUP_HS_TEX, ENABLE_ALL },
		[PIPE_SHADER_TESS_EVAL]  = { FD6_GROUP_DS_TEX, ENABLE_ALL },
		[PIPE_SHADER_GEOMETRY]  = { FD6_GROUP_GS_TEX, ENABLE_ALL },
		[PIPE_SHADER_FRAGMENT]  = { FD6_GROUP_FS_TEX, ENABLE_DRAW },
	};

	debug_assert(s[type].state_id);

	if (!v->image_mapping.num_tex && !v->fb_read) {
		/* in the fast-path, when we don't have to mix in any image/SSBO
		 * related texture state, we can just lookup the stateobj and
		 * re-emit that:
		 *
		 * Also, framebuffer-read is a slow-path because an extra
		 * texture needs to be inserted.
		 *
		 * TODO we can probably simmplify things if we also treated
		 * border_color as a slow-path.. this way the tex state key
		 * wouldn't depend on bcolor_offset.. but fb_read might rather
		 * be *somehow* a fast-path if we eventually used it for PLS.
		 * I suppose there would be no harm in just *always* inserting
		 * an fb_read texture?
		 */
		if ((ctx->dirty_shader[type] & FD_DIRTY_SHADER_TEX) &&
				ctx->tex[type].num_textures > 0) {
			struct fd6_texture_state *tex = fd6_texture_state(ctx,
					type, &ctx->tex[type]);

			needs_border |= tex->needs_border;

			fd6_emit_add_group(emit, tex->stateobj, s[type].state_id,
					s[type].enable_mask);

			fd6_texture_state_reference(&tex, NULL);
		}
	} else {
		/* In the slow-path, create a one-shot texture state object
		 * if either TEX|PROG|SSBO|IMAGE state is dirty:
		 */
		if ((ctx->dirty_shader[type] &
				(FD_DIRTY_SHADER_TEX | FD_DIRTY_SHADER_PROG |
				 FD_DIRTY_SHADER_IMAGE | FD_DIRTY_SHADER_SSBO)) ||
				v->fb_read) {
			struct fd_texture_stateobj *tex = &ctx->tex[type];
			struct fd_ringbuffer *stateobj =
				fd_submit_new_ringbuffer(ctx->batch->submit,
					0x1000, FD_RINGBUFFER_STREAMING);
			unsigned bcolor_offset =
				fd6_border_color_offset(ctx, type, tex);

			needs_border |= fd6_emit_textures(ctx->pipe, stateobj, type, tex,
					bcolor_offset, v, ctx);

			fd6_emit_take_group(emit, stateobj, s[type].state_id,
					s[type].enable_mask);
		}
	}

	return needs_border;
}

static struct fd_ringbuffer *
build_vbo_state(struct fd6_emit *emit)
{
	const struct fd_vertex_state *vtx = emit->vtx;

	struct fd_ringbuffer *ring = fd_submit_new_ringbuffer(emit->ctx->batch->submit,
			4 * (1 + vtx->vertexbuf.count * 4), FD_RINGBUFFER_STREAMING);

	OUT_PKT4(ring, REG_A6XX_VFD_FETCH(0), 4 * vtx->vertexbuf.count);
	for (int32_t j = 0; j < vtx->vertexbuf.count; j++) {
		const struct pipe_vertex_buffer *vb = &vtx->vertexbuf.vb[j];
		struct fd_resource *rsc = fd_resource(vb->buffer.resource);
		if (rsc == NULL) {
			OUT_RING(ring, 0);
			OUT_RING(ring, 0);
			OUT_RING(ring, 0);
			OUT_RING(ring, 0);
		} else {
			uint32_t off = vb->buffer_offset;
			uint32_t size = fd_bo_size(rsc->bo) - off;

			OUT_RELOC(ring, rsc->bo, off, 0, 0);
			OUT_RING(ring, size);           /* VFD_FETCH[j].SIZE */
			OUT_RING(ring, vb->stride);     /* VFD_FETCH[j].STRIDE */
		}
	}

	return ring;
}

static enum a6xx_ztest_mode
compute_ztest_mode(struct fd6_emit *emit, bool lrz_valid)
{
	struct fd_context *ctx =  emit->ctx;
	struct pipe_framebuffer_state *pfb = &ctx->batch->framebuffer;
	struct fd6_zsa_stateobj *zsa = fd6_zsa_stateobj(ctx->zsa);
	const struct ir3_shader_variant *fs = emit->fs;

	if (fs->shader->nir->info.fs.early_fragment_tests)
		return A6XX_EARLY_Z;

	if (fs->no_earlyz || fs->writes_pos || !zsa->base.depth_enabled ||
			fs->writes_stencilref) {
		return A6XX_LATE_Z;
	} else if ((fs->has_kill || zsa->alpha_test) &&
			(zsa->base.depth_writemask || !pfb->zsbuf)) {
		/* Slightly odd, but seems like the hw wants us to select
		 * LATE_Z mode if there is no depth buffer + discard.  Either
		 * that, or when occlusion query is enabled.  See:
		 *
		 * dEQP-GLES31.functional.fbo.no_attachments.*
		 */
		return lrz_valid ? A6XX_EARLY_LRZ_LATE_Z : A6XX_LATE_Z;
	} else {
		return A6XX_EARLY_Z;
	}
}

/**
 * Calculate normalized LRZ state based on zsa/prog/blend state, updating
 * the zsbuf's lrz state as necessary to detect the cases where we need
 * to invalidate lrz.
 */
static struct fd6_lrz_state
compute_lrz_state(struct fd6_emit *emit, bool binning_pass)
{
	struct fd_context *ctx = emit->ctx;
	struct pipe_framebuffer_state *pfb = &ctx->batch->framebuffer;
	const struct ir3_shader_variant *fs = emit->fs;
	struct fd6_lrz_state lrz;

	if (!pfb->zsbuf) {
		memset(&lrz, 0, sizeof(lrz));
		if (!binning_pass) {
			lrz.z_mode = compute_ztest_mode(emit, false);
		}
		return lrz;
	}

	struct fd6_blend_stateobj *blend = fd6_blend_stateobj(ctx->blend);
	struct fd6_zsa_stateobj *zsa = fd6_zsa_stateobj(ctx->zsa);
	struct fd_resource *rsc = fd_resource(pfb->zsbuf->texture);

	lrz = zsa->lrz;

	/* normalize lrz state: */
	if (blend->reads_dest || fs->writes_pos || fs->no_earlyz || fs->has_kill) {
		lrz.write = false;
		if (binning_pass)
			lrz.enable = false;
	}

	/* if we change depthfunc direction, bail out on using LRZ.  The
	 * LRZ buffer encodes a min/max depth value per block, but if
	 * we switch from GT/GE <-> LT/LE, those values cannot be
	 * interpreted properly.
	 */
	if (zsa->base.depth_enabled &&
			(rsc->lrz_direction != FD_LRZ_UNKNOWN) &&
			(rsc->lrz_direction != lrz.direction)) {
		rsc->lrz_valid = false;
	}

	if (zsa->invalidate_lrz || !rsc->lrz_valid) {
		rsc->lrz_valid = false;
		memset(&lrz, 0, sizeof(lrz));
	}

	if (fs->no_earlyz || fs->writes_pos) {
		lrz.enable = false;
		lrz.write = false;
		lrz.test = false;
	}

	if (!binning_pass) {
		lrz.z_mode = compute_ztest_mode(emit, rsc->lrz_valid);
	}

	/* Once we start writing to the real depth buffer, we lock in the
	 * direction for LRZ.. if we have to skip a LRZ write for any
	 * reason, it is still safe to have LRZ until there is a direction
	 * reversal.  Prior to the reversal, since we disabled LRZ writes
	 * in the "unsafe" cases, this just means that the LRZ test may
	 * not early-discard some things that end up not passing a later
	 * test (ie. be overly concervative).  But once you have a reversal
	 * of direction, it is possible to increase/decrease the z value
	 * to the point where the overly-conservative test is incorrect.
	 */
	if (zsa->base.depth_writemask) {
		rsc->lrz_direction = lrz.direction;
	}

	return lrz;
}

static struct fd_ringbuffer *
build_lrz(struct fd6_emit *emit, bool binning_pass)
{
	struct fd_context *ctx = emit->ctx;
	struct fd6_context *fd6_ctx = fd6_context(ctx);
	struct fd6_lrz_state lrz =
			compute_lrz_state(emit, binning_pass);

	/* If the LRZ state has not changed, we can skip the emit: */
	if (!ctx->last.dirty &&
			!memcmp(&fd6_ctx->last.lrz[binning_pass], &lrz, sizeof(lrz)))
		return NULL;

	fd6_ctx->last.lrz[binning_pass] = lrz;

	struct fd_ringbuffer *ring = fd_submit_new_ringbuffer(ctx->batch->submit,
			8*4, FD_RINGBUFFER_STREAMING);

	OUT_REG(ring, A6XX_GRAS_LRZ_CNTL(
			.enable        = lrz.enable,
			.lrz_write     = lrz.write,
			.greater       = lrz.direction == FD_LRZ_GREATER,
			.z_test_enable = lrz.test,
		));
	OUT_REG(ring, A6XX_RB_LRZ_CNTL(
			.enable = lrz.enable,
		));

	OUT_REG(ring, A6XX_RB_DEPTH_PLANE_CNTL(
			.z_mode = lrz.z_mode,
		));

	OUT_REG(ring, A6XX_GRAS_SU_DEPTH_PLANE_CNTL(
			.z_mode = lrz.z_mode,
		));

	return ring;
}

static void
fd6_emit_streamout(struct fd_ringbuffer *ring, struct fd6_emit *emit, struct ir3_stream_output_info *info)
{
	struct fd_context *ctx = emit->ctx;
	const struct fd6_program_state *prog = fd6_emit_get_prog(emit);
	struct fd_streamout_stateobj *so = &ctx->streamout;

	emit->streamout_mask = 0;

	for (unsigned i = 0; i < so->num_targets; i++) {
		struct pipe_stream_output_target *target = so->targets[i];

		if (!target)
			continue;

		OUT_PKT4(ring, REG_A6XX_VPC_SO_BUFFER_BASE_LO(i), 3);
		/* VPC_SO[i].BUFFER_BASE_LO: */
		OUT_RELOC(ring, fd_resource(target->buffer)->bo, target->buffer_offset, 0, 0);
		OUT_RING(ring, target->buffer_size - target->buffer_offset);

		if (so->reset & (1 << i)) {
			unsigned offset = (so->offsets[i] * info->stride[i] * 4);
			OUT_PKT4(ring, REG_A6XX_VPC_SO_BUFFER_OFFSET(i), 1);
			OUT_RING(ring, offset);
		} else {
			OUT_PKT7(ring, CP_MEM_TO_REG, 3);
			OUT_RING(ring, CP_MEM_TO_REG_0_REG(REG_A6XX_VPC_SO_BUFFER_OFFSET(i)) |
					CP_MEM_TO_REG_0_SHIFT_BY_2 | CP_MEM_TO_REG_0_UNK31 |
					CP_MEM_TO_REG_0_CNT(0));
			OUT_RELOC(ring, control_ptr(fd6_context(ctx), flush_base[i].offset));
		}

		OUT_PKT4(ring, REG_A6XX_VPC_SO_FLUSH_BASE_LO(i), 2);
		OUT_RELOC(ring, control_ptr(fd6_context(ctx), flush_base[i]));

		so->reset &= ~(1 << i);

		emit->streamout_mask |= (1 << i);
	}

	if (emit->streamout_mask) {
		fd6_emit_add_group(emit, prog->streamout_stateobj, FD6_GROUP_SO, ENABLE_ALL);
	} else {
		/* If we transition from a draw with streamout to one without, turn
		 * off streamout.
		 */
		if (ctx->last.streamout_mask != 0) {
			struct fd_ringbuffer *obj = fd_submit_new_ringbuffer(emit->ctx->batch->submit,
					5 * 4, FD_RINGBUFFER_STREAMING);

			OUT_PKT7(obj, CP_CONTEXT_REG_BUNCH, 4);
			OUT_RING(obj, REG_A6XX_VPC_SO_CNTL);
			OUT_RING(obj, 0);
			OUT_RING(obj, REG_A6XX_VPC_SO_STREAM_CNTL);
			OUT_RING(obj, 0);

			fd6_emit_take_group(emit, obj, FD6_GROUP_SO, ENABLE_ALL);
		}
	}

	ctx->last.streamout_mask = emit->streamout_mask;
}

void
fd6_emit_state(struct fd_ringbuffer *ring, struct fd6_emit *emit)
{
	struct fd_context *ctx = emit->ctx;
	struct pipe_framebuffer_state *pfb = &ctx->batch->framebuffer;
	const struct fd6_program_state *prog = fd6_emit_get_prog(emit);
	const struct ir3_shader_variant *vs = emit->vs;
	const struct ir3_shader_variant *hs = emit->hs;
	const struct ir3_shader_variant *ds = emit->ds;
	const struct ir3_shader_variant *gs = emit->gs;
	const struct ir3_shader_variant *fs = emit->fs;
	const enum fd_dirty_3d_state dirty = emit->dirty;
	bool needs_border = false;

	emit_marker6(ring, 5);

	/* NOTE: we track fb_read differently than _BLEND_ENABLED since
	 * we might at some point decide to do sysmem in some cases when
	 * blend is enabled:
	 */
	if (fs->fb_read)
		ctx->batch->gmem_reason |= FD_GMEM_FB_READ;

	if (emit->dirty & FD_DIRTY_VTXSTATE) {
		struct fd6_vertex_stateobj *vtx = fd6_vertex_stateobj(ctx->vtx.vtx);

		fd6_emit_add_group(emit, vtx->stateobj, FD6_GROUP_VTXSTATE, ENABLE_ALL);
	}

	if (emit->dirty & FD_DIRTY_VTXBUF) {
		struct fd_ringbuffer *state;

		state = build_vbo_state(emit);
		fd6_emit_take_group(emit, state, FD6_GROUP_VBO, ENABLE_ALL);
	}

	if (dirty & (FD_DIRTY_ZSA | FD_DIRTY_RASTERIZER)) {
		struct fd_ringbuffer *state =
			fd6_zsa_state(ctx,
					util_format_is_pure_integer(pipe_surface_format(pfb->cbufs[0])),
					fd_depth_clamp_enabled(ctx));

		fd6_emit_add_group(emit, state, FD6_GROUP_ZSA, ENABLE_ALL);
	}

	if (dirty & (FD_DIRTY_ZSA | FD_DIRTY_BLEND | FD_DIRTY_PROG)) {
		struct fd_ringbuffer *state;

		state = build_lrz(emit, false);
		if (state) {
			fd6_emit_take_group(emit, state, FD6_GROUP_LRZ, ENABLE_DRAW);
		}

		state = build_lrz(emit, true);
		if (state) {
			fd6_emit_take_group(emit, state,
					FD6_GROUP_LRZ_BINNING, CP_SET_DRAW_STATE__0_BINNING);
		}
	}

	if (dirty & FD_DIRTY_STENCIL_REF) {
		struct pipe_stencil_ref *sr = &ctx->stencil_ref;

		OUT_PKT4(ring, REG_A6XX_RB_STENCILREF, 1);
		OUT_RING(ring, A6XX_RB_STENCILREF_REF(sr->ref_value[0]) |
				A6XX_RB_STENCILREF_BFREF(sr->ref_value[1]));
	}

	/* NOTE: scissor enabled bit is part of rasterizer state, but
	 * fd_rasterizer_state_bind() will mark scissor dirty if needed:
	 */
	if (dirty & FD_DIRTY_SCISSOR) {
		struct fd_ringbuffer *ring = fd_submit_new_ringbuffer(
				emit->ctx->batch->submit, 3*4, FD_RINGBUFFER_STREAMING);
		struct pipe_scissor_state *scissor = fd_context_get_scissor(ctx);

		OUT_REG(ring,
				A6XX_GRAS_SC_SCREEN_SCISSOR_TL(0,
					.x = scissor->minx,
					.y = scissor->miny
				),
				A6XX_GRAS_SC_SCREEN_SCISSOR_BR(0,
					.x = MAX2(scissor->maxx, 1) - 1,
					.y = MAX2(scissor->maxy, 1) - 1
				)
			);

		fd6_emit_take_group(emit, ring, FD6_GROUP_SCISSOR, ENABLE_ALL);

		ctx->batch->max_scissor.minx = MIN2(ctx->batch->max_scissor.minx, scissor->minx);
		ctx->batch->max_scissor.miny = MIN2(ctx->batch->max_scissor.miny, scissor->miny);
		ctx->batch->max_scissor.maxx = MAX2(ctx->batch->max_scissor.maxx, scissor->maxx);
		ctx->batch->max_scissor.maxy = MAX2(ctx->batch->max_scissor.maxy, scissor->maxy);
	}

	if (dirty & FD_DIRTY_VIEWPORT) {
		struct pipe_scissor_state *scissor = &ctx->viewport_scissor;

		OUT_REG(ring,
				A6XX_GRAS_CL_VPORT_XOFFSET(0, ctx->viewport.translate[0]),
				A6XX_GRAS_CL_VPORT_XSCALE(0, ctx->viewport.scale[0]),
				A6XX_GRAS_CL_VPORT_YOFFSET(0, ctx->viewport.translate[1]),
				A6XX_GRAS_CL_VPORT_YSCALE(0, ctx->viewport.scale[1]),
				A6XX_GRAS_CL_VPORT_ZOFFSET(0, ctx->viewport.translate[2]),
				A6XX_GRAS_CL_VPORT_ZSCALE(0, ctx->viewport.scale[2])
			);

		OUT_REG(ring,
				A6XX_GRAS_SC_VIEWPORT_SCISSOR_TL(0,
					.x = scissor->minx,
					.y = scissor->miny
				),
				A6XX_GRAS_SC_VIEWPORT_SCISSOR_BR(0,
					.x = MAX2(scissor->maxx, 1) - 1,
					.y = MAX2(scissor->maxy, 1) - 1
				)
			);

		unsigned guardband_x =
			fd_calc_guardband(ctx->viewport.translate[0], ctx->viewport.scale[0],
							  false);
		unsigned guardband_y =
			fd_calc_guardband(ctx->viewport.translate[1], ctx->viewport.scale[1],
							  false);

		OUT_REG(ring, A6XX_GRAS_CL_GUARDBAND_CLIP_ADJ(
					.horz = guardband_x,
					.vert = guardband_y
				)
			);
	}

	/* The clamp ranges are only used when the rasterizer wants depth
	 * clamping.
	 */
	if ((dirty & (FD_DIRTY_VIEWPORT | FD_DIRTY_RASTERIZER)) &&
			fd_depth_clamp_enabled(ctx)) {
		float zmin, zmax;
		util_viewport_zmin_zmax(&ctx->viewport, ctx->rasterizer->clip_halfz,
				&zmin, &zmax);

		OUT_REG(ring,
				A6XX_GRAS_CL_Z_CLAMP_MIN(0, zmin),
				A6XX_GRAS_CL_Z_CLAMP_MAX(0, zmax));

		OUT_REG(ring,
				A6XX_RB_Z_CLAMP_MIN(zmin),
				A6XX_RB_Z_CLAMP_MAX(zmax));
	}

	if (dirty & FD_DIRTY_PROG) {
		fd6_emit_add_group(emit, prog->config_stateobj, FD6_GROUP_PROG_CONFIG, ENABLE_ALL);
		fd6_emit_add_group(emit, prog->stateobj, FD6_GROUP_PROG, ENABLE_DRAW);
		fd6_emit_add_group(emit, prog->binning_stateobj,
				FD6_GROUP_PROG_BINNING, CP_SET_DRAW_STATE__0_BINNING);

		/* emit remaining streaming program state, ie. what depends on
		 * other emit state, so cannot be pre-baked.
		 */
		struct fd_ringbuffer *streaming = fd6_program_interp_state(emit);

		fd6_emit_take_group(emit, streaming, FD6_GROUP_PROG_INTERP, ENABLE_DRAW);
	}

	if (dirty & FD_DIRTY_RASTERIZER) {
		struct fd_ringbuffer *stateobj =
			fd6_rasterizer_state(ctx, emit->primitive_restart);
		fd6_emit_add_group(emit, stateobj,
						   FD6_GROUP_RASTERIZER, ENABLE_ALL);
	}

	if (dirty & (FD_DIRTY_FRAMEBUFFER | FD_DIRTY_RASTERIZER_DISCARD |
				 FD_DIRTY_PROG | FD_DIRTY_BLEND_DUAL)) {
		struct fd_ringbuffer *ring = fd_submit_new_ringbuffer(
				emit->ctx->batch->submit, 9 * 4, FD_RINGBUFFER_STREAMING);

		unsigned nr = pfb->nr_cbufs;

		if (ctx->rasterizer->rasterizer_discard)
			nr = 0;

		struct fd6_blend_stateobj *blend = fd6_blend_stateobj(ctx->blend);

		if (blend->use_dual_src_blend)
			nr++;

		OUT_PKT4(ring, REG_A6XX_RB_FS_OUTPUT_CNTL0, 2);
		OUT_RING(ring, COND(fs->writes_pos, A6XX_RB_FS_OUTPUT_CNTL0_FRAG_WRITES_Z) |
				COND(fs->writes_smask && pfb->samples > 1,
						A6XX_RB_FS_OUTPUT_CNTL0_FRAG_WRITES_SAMPMASK) |
				COND(fs->writes_stencilref,
						A6XX_RB_FS_OUTPUT_CNTL0_FRAG_WRITES_STENCILREF) |
				COND(blend->use_dual_src_blend,
						A6XX_RB_FS_OUTPUT_CNTL0_DUAL_COLOR_IN_ENABLE));
		OUT_RING(ring, A6XX_RB_FS_OUTPUT_CNTL1_MRT(nr));

		OUT_PKT4(ring, REG_A6XX_SP_FS_OUTPUT_CNTL1, 1);
		OUT_RING(ring, A6XX_SP_FS_OUTPUT_CNTL1_MRT(nr));

		unsigned mrt_components = 0;
		for (unsigned i = 0; i < pfb->nr_cbufs; i++) {
			if (!pfb->cbufs[i])
				continue;
			mrt_components |= 0xf << (i * 4);
		}

		/* dual source blending has an extra fs output in the 2nd slot */
		if (blend->use_dual_src_blend)
			mrt_components |= 0xf << 4;

		OUT_REG(ring, A6XX_SP_FS_RENDER_COMPONENTS(.dword = mrt_components));
		OUT_REG(ring, A6XX_RB_RENDER_COMPONENTS(.dword = mrt_components));

		fd6_emit_take_group(emit, ring, FD6_GROUP_PROG_FB_RAST, ENABLE_DRAW);
	}

	fd6_emit_consts(emit);

	struct ir3_stream_output_info *info = &fd6_last_shader(prog)->shader->stream_output;
	if (info->num_outputs)
		fd6_emit_streamout(ring, emit, info);

	if (dirty & (FD_DIRTY_BLEND | FD_DIRTY_SAMPLE_MASK)) {
		struct fd6_blend_variant *blend = fd6_blend_variant(ctx->blend,
				pfb->samples, ctx->sample_mask);
		fd6_emit_add_group(emit, blend->stateobj, FD6_GROUP_BLEND, ENABLE_DRAW);
	}

	if (dirty & FD_DIRTY_BLEND_COLOR) {
		struct pipe_blend_color *bcolor = &ctx->blend_color;
		struct fd_ringbuffer *ring = fd_submit_new_ringbuffer(
				emit->ctx->batch->submit, 5*4, FD_RINGBUFFER_STREAMING);

		OUT_REG(ring,
				A6XX_RB_BLEND_RED_F32(bcolor->color[0]),
				A6XX_RB_BLEND_GREEN_F32(bcolor->color[1]),
				A6XX_RB_BLEND_BLUE_F32(bcolor->color[2]),
				A6XX_RB_BLEND_ALPHA_F32(bcolor->color[3])
			);

		fd6_emit_take_group(emit, ring, FD6_GROUP_BLEND_COLOR, ENABLE_DRAW);
	}

	needs_border |= fd6_emit_combined_textures(ring, emit, PIPE_SHADER_VERTEX, vs);
	if (hs) {
		needs_border |= fd6_emit_combined_textures(ring, emit, PIPE_SHADER_TESS_CTRL, hs);
		needs_border |= fd6_emit_combined_textures(ring, emit, PIPE_SHADER_TESS_EVAL, ds);
	}
	if (gs) {
		needs_border |= fd6_emit_combined_textures(ring, emit, PIPE_SHADER_GEOMETRY, gs);
	}
	needs_border |= fd6_emit_combined_textures(ring, emit, PIPE_SHADER_FRAGMENT, fs);

	if (needs_border)
		emit_border_color(ctx, ring);

	if (hs) {
		debug_assert(ir3_shader_nibo(hs) == 0);
		debug_assert(ir3_shader_nibo(ds) == 0);
	}
	if (gs) {
		debug_assert(ir3_shader_nibo(gs) == 0);
	}

#define DIRTY_IBO (FD_DIRTY_SHADER_SSBO | FD_DIRTY_SHADER_IMAGE | \
				   FD_DIRTY_SHADER_PROG)
	if (ctx->dirty_shader[PIPE_SHADER_FRAGMENT] & DIRTY_IBO) {
		struct fd_ringbuffer *state =
			fd6_build_ibo_state(ctx, fs, PIPE_SHADER_FRAGMENT);
		struct fd_ringbuffer *obj = fd_submit_new_ringbuffer(
			ctx->batch->submit, 0x100, FD_RINGBUFFER_STREAMING);

		OUT_PKT7(obj, CP_LOAD_STATE6, 3);
		OUT_RING(obj, CP_LOAD_STATE6_0_DST_OFF(0) |
			CP_LOAD_STATE6_0_STATE_TYPE(ST6_SHADER) |
			CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
			CP_LOAD_STATE6_0_STATE_BLOCK(SB6_IBO) |
			CP_LOAD_STATE6_0_NUM_UNIT(ir3_shader_nibo(fs)));
		OUT_RB(obj, state);

		OUT_PKT4(obj, REG_A6XX_SP_IBO_LO, 2);
		OUT_RB(obj, state);

		/* TODO if we used CP_SET_DRAW_STATE for compute shaders, we could
		 * de-duplicate this from program->config_stateobj
		 */
		OUT_PKT4(obj, REG_A6XX_SP_IBO_COUNT, 1);
		OUT_RING(obj, ir3_shader_nibo(fs));

		fd6_emit_ibo_consts(emit, fs, PIPE_SHADER_FRAGMENT, ring);

		fd6_emit_take_group(emit, obj, FD6_GROUP_IBO, ENABLE_DRAW);
		fd_ringbuffer_del(state);
	}

	if (emit->num_groups > 0) {
		OUT_PKT7(ring, CP_SET_DRAW_STATE, 3 * emit->num_groups);
		for (unsigned i = 0; i < emit->num_groups; i++) {
			struct fd6_state_group *g = &emit->groups[i];
			unsigned n = g->stateobj ?
				fd_ringbuffer_size(g->stateobj) / 4 : 0;

			debug_assert((g->enable_mask & ~ENABLE_ALL) == 0);

			if (n == 0) {
				OUT_RING(ring, CP_SET_DRAW_STATE__0_COUNT(0) |
						CP_SET_DRAW_STATE__0_DISABLE |
						g->enable_mask |
						CP_SET_DRAW_STATE__0_GROUP_ID(g->group_id));
				OUT_RING(ring, 0x00000000);
				OUT_RING(ring, 0x00000000);
			} else {
				OUT_RING(ring, CP_SET_DRAW_STATE__0_COUNT(n) |
						g->enable_mask |
						CP_SET_DRAW_STATE__0_GROUP_ID(g->group_id));
				OUT_RB(ring, g->stateobj);
			}

			if (g->stateobj)
				fd_ringbuffer_del(g->stateobj);
		}
		emit->num_groups = 0;
	}
}

void
fd6_emit_cs_state(struct fd_context *ctx, struct fd_ringbuffer *ring,
		struct ir3_shader_variant *cp)
{
	enum fd_dirty_shader_state dirty = ctx->dirty_shader[PIPE_SHADER_COMPUTE];

	if (dirty & (FD_DIRTY_SHADER_TEX | FD_DIRTY_SHADER_PROG |
			 FD_DIRTY_SHADER_IMAGE | FD_DIRTY_SHADER_SSBO)) {
		struct fd_texture_stateobj *tex = &ctx->tex[PIPE_SHADER_COMPUTE];
		unsigned bcolor_offset = fd6_border_color_offset(ctx, PIPE_SHADER_COMPUTE, tex);

		bool needs_border = fd6_emit_textures(ctx->pipe, ring, PIPE_SHADER_COMPUTE, tex,
				bcolor_offset, cp, ctx);

		if (needs_border)
			emit_border_color(ctx, ring);

		OUT_PKT4(ring, REG_A6XX_SP_VS_TEX_COUNT, 1);
		OUT_RING(ring, 0);

		OUT_PKT4(ring, REG_A6XX_SP_HS_TEX_COUNT, 1);
		OUT_RING(ring, 0);

		OUT_PKT4(ring, REG_A6XX_SP_DS_TEX_COUNT, 1);
		OUT_RING(ring, 0);

		OUT_PKT4(ring, REG_A6XX_SP_GS_TEX_COUNT, 1);
		OUT_RING(ring, 0);

		OUT_PKT4(ring, REG_A6XX_SP_FS_TEX_COUNT, 1);
		OUT_RING(ring, 0);
	}

	if (dirty & (FD_DIRTY_SHADER_SSBO | FD_DIRTY_SHADER_IMAGE)) {
		struct fd_ringbuffer *state =
			fd6_build_ibo_state(ctx, cp, PIPE_SHADER_COMPUTE);

		OUT_PKT7(ring, CP_LOAD_STATE6_FRAG, 3);
		OUT_RING(ring, CP_LOAD_STATE6_0_DST_OFF(0) |
			CP_LOAD_STATE6_0_STATE_TYPE(ST6_IBO) |
			CP_LOAD_STATE6_0_STATE_SRC(SS6_INDIRECT) |
			CP_LOAD_STATE6_0_STATE_BLOCK(SB6_CS_SHADER) |
			CP_LOAD_STATE6_0_NUM_UNIT(ir3_shader_nibo(cp)));
		OUT_RB(ring, state);

		OUT_PKT4(ring, REG_A6XX_SP_CS_IBO_LO, 2);
		OUT_RB(ring, state);

		OUT_PKT4(ring, REG_A6XX_SP_CS_IBO_COUNT, 1);
		OUT_RING(ring, ir3_shader_nibo(cp));

		fd_ringbuffer_del(state);
	}
}


/* emit setup at begin of new cmdstream buffer (don't rely on previous
 * state, there could have been a context switch between ioctls):
 */
void
fd6_emit_restore(struct fd_batch *batch, struct fd_ringbuffer *ring)
{
	//struct fd_context *ctx = batch->ctx;

	if (!batch->nondraw) {
		trace_start_state_restore(&batch->trace);
	}

	fd6_cache_inv(batch, ring);

	OUT_REG(ring, A6XX_HLSQ_INVALIDATE_CMD(
			.vs_state = true,
			.hs_state = true,
			.ds_state = true,
			.gs_state = true,
			.fs_state = true,
			.cs_state = true,
			.gfx_ibo = true,
			.cs_ibo = true,
			.gfx_shared_const = true,
			.cs_shared_const = true,
			.gfx_bindless = 0x1f,
			.cs_bindless = 0x1f
		));

	OUT_WFI5(ring);

	WRITE(REG_A6XX_RB_UNKNOWN_8E04, 0x0);
	WRITE(REG_A6XX_SP_UNKNOWN_AE04, 0x8);
	WRITE(REG_A6XX_SP_UNKNOWN_AE00, 0);
	WRITE(REG_A6XX_SP_UNKNOWN_AE0F, 0x3f);
	WRITE(REG_A6XX_SP_UNKNOWN_B605, 0x44);
	WRITE(REG_A6XX_SP_UNKNOWN_B600, 0x100000);
	WRITE(REG_A6XX_HLSQ_UNKNOWN_BE00, 0x80);
	WRITE(REG_A6XX_HLSQ_UNKNOWN_BE01, 0);

	WRITE(REG_A6XX_VPC_UNKNOWN_9600, 0);
	WRITE(REG_A6XX_GRAS_UNKNOWN_8600, 0x880);
	WRITE(REG_A6XX_HLSQ_UNKNOWN_BE04, 0x80000);
	WRITE(REG_A6XX_SP_UNKNOWN_AE03, 0x1430);
	WRITE(REG_A6XX_SP_IBO_COUNT, 0);
	WRITE(REG_A6XX_SP_UNKNOWN_B182, 0);
	WRITE(REG_A6XX_HLSQ_SHARED_CONSTS, 0);
	WRITE(REG_A6XX_UCHE_UNKNOWN_0E12, 0x3200000);
	WRITE(REG_A6XX_UCHE_CLIENT_PF, 4);
	WRITE(REG_A6XX_RB_UNKNOWN_8E01, 0x1);
	WRITE(REG_A6XX_SP_MODE_CONTROL, A6XX_SP_MODE_CONTROL_CONSTANT_DEMOTION_ENABLE | 4);
	WRITE(REG_A6XX_VFD_ADD_OFFSET, A6XX_VFD_ADD_OFFSET_VERTEX);
	WRITE(REG_A6XX_RB_UNKNOWN_8811, 0x00000010);
	WRITE(REG_A6XX_PC_MODE_CNTL, 0x1f);

	WRITE(REG_A6XX_GRAS_UNKNOWN_8101, 0);
	WRITE(REG_A6XX_GRAS_SAMPLE_CNTL, 0);
	WRITE(REG_A6XX_GRAS_UNKNOWN_8110, 0x2);

	WRITE(REG_A6XX_RB_UNKNOWN_8818, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_8819, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_881A, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_881B, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_881C, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_881D, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_881E, 0);
	WRITE(REG_A6XX_RB_UNKNOWN_88F0, 0);

	WRITE(REG_A6XX_VPC_POINT_COORD_INVERT,
		  A6XX_VPC_POINT_COORD_INVERT(0).value);
	WRITE(REG_A6XX_VPC_UNKNOWN_9300, 0);

	WRITE(REG_A6XX_VPC_SO_DISABLE, A6XX_VPC_SO_DISABLE(true).value);

	WRITE(REG_A6XX_PC_RASTER_CNTL, 0);

	WRITE(REG_A6XX_PC_MULTIVIEW_CNTL, 0);

	WRITE(REG_A6XX_SP_UNKNOWN_B183, 0);

	WRITE(REG_A6XX_GRAS_UNKNOWN_8099, 0);
	WRITE(REG_A6XX_GRAS_VS_LAYER_CNTL, 0);
	WRITE(REG_A6XX_GRAS_UNKNOWN_80A0, 2);
	WRITE(REG_A6XX_GRAS_UNKNOWN_80AF, 0);
	WRITE(REG_A6XX_VPC_UNKNOWN_9210, 0);
	WRITE(REG_A6XX_VPC_UNKNOWN_9211, 0);
	WRITE(REG_A6XX_VPC_UNKNOWN_9602, 0);
	WRITE(REG_A6XX_PC_UNKNOWN_9E72, 0);
	WRITE(REG_A6XX_SP_TP_SAMPLE_CONFIG, 0);
	/* NOTE blob seems to (mostly?) use 0xb2 for SP_TP_UNKNOWN_B309
	 * but this seems to kill texture gather offsets.
	 */
	WRITE(REG_A6XX_SP_TP_UNKNOWN_B309, 0xa2);
	WRITE(REG_A6XX_RB_SAMPLE_CONFIG, 0);
	WRITE(REG_A6XX_GRAS_SAMPLE_CONFIG, 0);
	WRITE(REG_A6XX_RB_Z_BOUNDS_MIN, 0);
	WRITE(REG_A6XX_RB_Z_BOUNDS_MAX, 0);
	WRITE(REG_A6XX_HLSQ_CONTROL_5_REG, 0xfc);

	emit_marker6(ring, 7);

	OUT_PKT4(ring, REG_A6XX_VFD_MODE_CNTL, 1);
	OUT_RING(ring, 0x00000000);   /* VFD_MODE_CNTL */

	WRITE(REG_A6XX_VFD_MULTIVIEW_CNTL, 0);

	OUT_PKT4(ring, REG_A6XX_PC_MODE_CNTL, 1);
	OUT_RING(ring, 0x0000001f);   /* PC_MODE_CNTL */

	/* we don't use this yet.. probably best to disable.. */
	OUT_PKT7(ring, CP_SET_DRAW_STATE, 3);
	OUT_RING(ring, CP_SET_DRAW_STATE__0_COUNT(0) |
			CP_SET_DRAW_STATE__0_DISABLE_ALL_GROUPS |
			CP_SET_DRAW_STATE__0_GROUP_ID(0));
	OUT_RING(ring, CP_SET_DRAW_STATE__1_ADDR_LO(0));
	OUT_RING(ring, CP_SET_DRAW_STATE__2_ADDR_HI(0));

	OUT_PKT4(ring, REG_A6XX_VPC_SO_STREAM_CNTL, 1);
	OUT_RING(ring, 0x00000000);   /* VPC_SO_STREAM_CNTL */

	OUT_PKT4(ring, REG_A6XX_GRAS_LRZ_CNTL, 1);
	OUT_RING(ring, 0x00000000);

	OUT_PKT4(ring, REG_A6XX_RB_LRZ_CNTL, 1);
	OUT_RING(ring, 0x00000000);

	if (!batch->nondraw) {
		trace_end_state_restore(&batch->trace);
	}
}

static void
fd6_mem_to_mem(struct fd_ringbuffer *ring, struct pipe_resource *dst,
		unsigned dst_off, struct pipe_resource *src, unsigned src_off,
		unsigned sizedwords)
{
	struct fd_bo *src_bo = fd_resource(src)->bo;
	struct fd_bo *dst_bo = fd_resource(dst)->bo;
	unsigned i;

	for (i = 0; i < sizedwords; i++) {
		OUT_PKT7(ring, CP_MEM_TO_MEM, 5);
		OUT_RING(ring, 0x00000000);
		OUT_RELOC(ring, dst_bo, dst_off, 0, 0);
		OUT_RELOC(ring, src_bo, src_off, 0, 0);

		dst_off += 4;
		src_off += 4;
	}
}

/* this is *almost* the same as fd6_cache_flush().. which I guess
 * could be re-worked to be something a bit more generic w/ param
 * indicating what needs to be flushed..  although that would mean
 * figuring out which events trigger what state to flush..
 */
static void
fd6_framebuffer_barrier(struct fd_context *ctx)
{
	struct fd6_context *fd6_ctx = fd6_context(ctx);
	struct fd_batch *batch = ctx->batch;
	struct fd_ringbuffer *ring = batch->draw;
	unsigned seqno;

	seqno = fd6_event_write(batch, ring, RB_DONE_TS, true);

	OUT_PKT7(ring, CP_WAIT_REG_MEM, 6);
	OUT_RING(ring, CP_WAIT_REG_MEM_0_FUNCTION(WRITE_EQ) |
		       CP_WAIT_REG_MEM_0_POLL_MEMORY);
	OUT_RELOC(ring, control_ptr(fd6_ctx, seqno));
	OUT_RING(ring, CP_WAIT_REG_MEM_3_REF(seqno));
	OUT_RING(ring, CP_WAIT_REG_MEM_4_MASK(~0));
	OUT_RING(ring, CP_WAIT_REG_MEM_5_DELAY_LOOP_CYCLES(16));

	fd6_event_write(batch, ring, PC_CCU_FLUSH_COLOR_TS, true);
	fd6_event_write(batch, ring, PC_CCU_FLUSH_DEPTH_TS, true);

	seqno = fd6_event_write(batch, ring, CACHE_FLUSH_TS, true);

	fd6_event_write(batch, ring, 0x31, false);

	OUT_PKT7(ring, CP_WAIT_MEM_GTE, 4);
	OUT_RING(ring, CP_WAIT_MEM_GTE_0_RESERVED(0));
	OUT_RELOC(ring, control_ptr(fd6_ctx, seqno));
	OUT_RING(ring, CP_WAIT_MEM_GTE_3_REF(seqno));
}

void
fd6_emit_init_screen(struct pipe_screen *pscreen)
{
	struct fd_screen *screen = fd_screen(pscreen);
	screen->emit_ib = fd6_emit_ib;
	screen->mem_to_mem = fd6_mem_to_mem;
}

void
fd6_emit_init(struct pipe_context *pctx)
{
	struct fd_context *ctx = fd_context(pctx);
	ctx->framebuffer_barrier = fd6_framebuffer_barrier;
}
