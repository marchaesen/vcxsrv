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

#include <stdio.h>

#include "pipe/p_state.h"
#include "util/format/u_format.h"
#include "util/u_inlines.h"
#include "util/u_memory.h"
#include "util/u_string.h"

#include "freedreno_draw.h"
#include "freedreno_resource.h"
#include "freedreno_state.h"
#include "freedreno_tracepoints.h"

#include "fd6_blitter.h"
#include "fd6_context.h"
#include "fd6_draw.h"
#include "fd6_emit.h"
#include "fd6_format.h"
#include "fd6_gmem.h"
#include "fd6_pack.h"
#include "fd6_program.h"
#include "fd6_resource.h"
#include "fd6_zsa.h"

/**
 * Emits the flags registers, suitable for RB_MRT_FLAG_BUFFER,
 * RB_DEPTH_FLAG_BUFFER, SP_PS_2D_SRC_FLAGS, and RB_BLIT_FLAG_DST.
 */
void
fd6_emit_flag_reference(struct fd_ringbuffer *ring, struct fd_resource *rsc,
                        int level, int layer)
{
   if (fd_resource_ubwc_enabled(rsc, level)) {
      OUT_RELOC(ring, rsc->bo, fd_resource_ubwc_offset(rsc, level, layer), 0,
                0);
      OUT_RING(ring, A6XX_RB_MRT_FLAG_BUFFER_PITCH_PITCH(
                        fdl_ubwc_pitch(&rsc->layout, level)) |
                        A6XX_RB_MRT_FLAG_BUFFER_PITCH_ARRAY_PITCH(
                           rsc->layout.ubwc_layer_size >> 2));
   } else {
      OUT_RING(ring, 0x00000000); /* RB_MRT_FLAG_BUFFER[i].ADDR_LO */
      OUT_RING(ring, 0x00000000); /* RB_MRT_FLAG_BUFFER[i].ADDR_HI */
      OUT_RING(ring, 0x00000000);
   }
}

static void
emit_mrt(struct fd_ringbuffer *ring, struct pipe_framebuffer_state *pfb,
         const struct fd_gmem_stateobj *gmem)
{
   unsigned srgb_cntl = 0;
   unsigned i;

   unsigned max_layer_index = 0;

   for (i = 0; i < pfb->nr_cbufs; i++) {
      enum a6xx_format format = 0;
      enum a3xx_color_swap swap = WZYX;
      bool sint = false, uint = false;
      struct fd_resource *rsc = NULL;
      struct fdl_slice *slice = NULL;
      uint32_t stride = 0;
      uint32_t array_stride = 0;
      uint32_t offset;
      uint32_t tile_mode;

      if (!pfb->cbufs[i])
         continue;

      struct pipe_surface *psurf = pfb->cbufs[i];
      enum pipe_format pformat = psurf->format;
      rsc = fd_resource(psurf->texture);
      if (!rsc->bo)
         continue;

      uint32_t base = gmem ? gmem->cbuf_base[i] : 0;
      slice = fd_resource_slice(rsc, psurf->u.tex.level);
      format = fd6_pipe2color(pformat);
      sint = util_format_is_pure_sint(pformat);
      uint = util_format_is_pure_uint(pformat);

      if (util_format_is_srgb(pformat))
         srgb_cntl |= (1 << i);

      offset =
         fd_resource_offset(rsc, psurf->u.tex.level, psurf->u.tex.first_layer);

      stride = fd_resource_pitch(rsc, psurf->u.tex.level);
      array_stride = fd_resource_layer_stride(rsc, psurf->u.tex.level);
      swap = fd6_resource_swap(rsc, pformat);

      tile_mode = fd_resource_tile_mode(psurf->texture, psurf->u.tex.level);
      max_layer_index = psurf->u.tex.last_layer - psurf->u.tex.first_layer;

      debug_assert((offset + slice->size0) <= fd_bo_size(rsc->bo));

      OUT_REG(
         ring,
         A6XX_RB_MRT_BUF_INFO(i, .color_format = format,
                              .color_tile_mode = tile_mode, .color_swap = swap),
         A6XX_RB_MRT_PITCH(i, .a6xx_rb_mrt_pitch = stride),
         A6XX_RB_MRT_ARRAY_PITCH(i, .a6xx_rb_mrt_array_pitch = array_stride),
         A6XX_RB_MRT_BASE(i, .bo = rsc->bo, .bo_offset = offset),
         A6XX_RB_MRT_BASE_GMEM(i, .unknown = base));

      OUT_REG(ring, A6XX_SP_FS_MRT_REG(i, .color_format = format,
                                       .color_sint = sint, .color_uint = uint));

      OUT_PKT4(ring, REG_A6XX_RB_MRT_FLAG_BUFFER(i), 3);
      fd6_emit_flag_reference(ring, rsc, psurf->u.tex.level,
                              psurf->u.tex.first_layer);
   }

   OUT_REG(ring, A6XX_RB_SRGB_CNTL(.dword = srgb_cntl));
   OUT_REG(ring, A6XX_SP_SRGB_CNTL(.dword = srgb_cntl));

   OUT_REG(ring, A6XX_GRAS_MAX_LAYER_INDEX(max_layer_index));
}

static void
emit_zs(struct fd_ringbuffer *ring, struct pipe_surface *zsbuf,
        const struct fd_gmem_stateobj *gmem)
{
   if (zsbuf) {
      struct fd_resource *rsc = fd_resource(zsbuf->texture);
      enum a6xx_depth_format fmt = fd6_pipe2depth(zsbuf->format);
      uint32_t stride = fd_resource_pitch(rsc, 0);
      uint32_t array_stride = fd_resource_layer_stride(rsc, 0);
      uint32_t base = gmem ? gmem->zsbuf_base[0] : 0;
      uint32_t offset =
         fd_resource_offset(rsc, zsbuf->u.tex.level, zsbuf->u.tex.first_layer);

      OUT_REG(
         ring, A6XX_RB_DEPTH_BUFFER_INFO(.depth_format = fmt),
         A6XX_RB_DEPTH_BUFFER_PITCH(.a6xx_rb_depth_buffer_pitch = stride),
         A6XX_RB_DEPTH_BUFFER_ARRAY_PITCH(.a6xx_rb_depth_buffer_array_pitch =
                                             array_stride),
         A6XX_RB_DEPTH_BUFFER_BASE(.bo = rsc->bo, .bo_offset = offset),
         A6XX_RB_DEPTH_BUFFER_BASE_GMEM(.dword = base));

      OUT_REG(ring, A6XX_GRAS_SU_DEPTH_BUFFER_INFO(.depth_format = fmt));

      OUT_PKT4(ring, REG_A6XX_RB_DEPTH_FLAG_BUFFER_BASE, 3);
      fd6_emit_flag_reference(ring, rsc, zsbuf->u.tex.level,
                              zsbuf->u.tex.first_layer);

      if (rsc->lrz) {
         OUT_REG(ring, A6XX_GRAS_LRZ_BUFFER_BASE(.bo = rsc->lrz),
                 A6XX_GRAS_LRZ_BUFFER_PITCH(.pitch = rsc->lrz_pitch),
                 // XXX a6xx seems to use a different buffer here.. not sure
                 // what for..
                 A6XX_GRAS_LRZ_FAST_CLEAR_BUFFER_BASE());
      } else {
         OUT_PKT4(ring, REG_A6XX_GRAS_LRZ_BUFFER_BASE, 5);
         OUT_RING(ring, 0x00000000);
         OUT_RING(ring, 0x00000000);
         OUT_RING(ring, 0x00000000); /* GRAS_LRZ_BUFFER_PITCH */
         OUT_RING(ring, 0x00000000); /* GRAS_LRZ_FAST_CLEAR_BUFFER_BASE_LO */
         OUT_RING(ring, 0x00000000);
      }

      /* NOTE: blob emits GRAS_LRZ_CNTL plus GRAZ_LRZ_BUFFER_BASE
       * plus this CP_EVENT_WRITE at the end in it's own IB..
       */
      OUT_PKT7(ring, CP_EVENT_WRITE, 1);
      OUT_RING(ring, CP_EVENT_WRITE_0_EVENT(UNK_25));

      if (rsc->stencil) {
         stride = fd_resource_pitch(rsc->stencil, 0);
         array_stride = fd_resource_layer_stride(rsc->stencil, 0);
         uint32_t base = gmem ? gmem->zsbuf_base[1] : 0;

         OUT_REG(ring, A6XX_RB_STENCIL_INFO(.separate_stencil = true),
                 A6XX_RB_STENCIL_BUFFER_PITCH(.a6xx_rb_stencil_buffer_pitch =
                                                 stride),
                 A6XX_RB_STENCIL_BUFFER_ARRAY_PITCH(
                       .a6xx_rb_stencil_buffer_array_pitch = array_stride),
                 A6XX_RB_STENCIL_BUFFER_BASE(.bo = rsc->stencil->bo),
                 A6XX_RB_STENCIL_BUFFER_BASE_GMEM(.dword = base));
      } else {
         OUT_REG(ring, A6XX_RB_STENCIL_INFO(0));
      }
   } else {
      OUT_PKT4(ring, REG_A6XX_RB_DEPTH_BUFFER_INFO, 6);
      OUT_RING(ring, A6XX_RB_DEPTH_BUFFER_INFO_DEPTH_FORMAT(DEPTH6_NONE));
      OUT_RING(ring, 0x00000000); /* RB_DEPTH_BUFFER_PITCH */
      OUT_RING(ring, 0x00000000); /* RB_DEPTH_BUFFER_ARRAY_PITCH */
      OUT_RING(ring, 0x00000000); /* RB_DEPTH_BUFFER_BASE_LO */
      OUT_RING(ring, 0x00000000); /* RB_DEPTH_BUFFER_BASE_HI */
      OUT_RING(ring, 0x00000000); /* RB_DEPTH_BUFFER_BASE_GMEM */

      OUT_REG(ring,
              A6XX_GRAS_SU_DEPTH_BUFFER_INFO(.depth_format = DEPTH6_NONE));

      OUT_PKT4(ring, REG_A6XX_GRAS_LRZ_BUFFER_BASE, 5);
      OUT_RING(ring, 0x00000000); /* RB_DEPTH_FLAG_BUFFER_BASE_LO */
      OUT_RING(ring, 0x00000000); /* RB_DEPTH_FLAG_BUFFER_BASE_HI */
      OUT_RING(ring, 0x00000000); /* GRAS_LRZ_BUFFER_PITCH */
      OUT_RING(ring, 0x00000000); /* GRAS_LRZ_FAST_CLEAR_BUFFER_BASE_LO */
      OUT_RING(ring, 0x00000000); /* GRAS_LRZ_FAST_CLEAR_BUFFER_BASE_HI */

      OUT_REG(ring, A6XX_RB_STENCIL_INFO(0));
   }
}

static bool
use_hw_binning(struct fd_batch *batch)
{
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;

   if ((gmem->maxpw * gmem->maxph) > 32)
      return false;

   return fd_binning_enabled && ((gmem->nbins_x * gmem->nbins_y) >= 2) &&
          (batch->num_draws > 0);
}

static void
patch_fb_read_gmem(struct fd_batch *batch)
{
   unsigned num_patches = fd_patch_num_elements(&batch->fb_read_patches);
   if (!num_patches)
      return;

   struct fd_screen *screen = batch->ctx->screen;
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;
   struct pipe_framebuffer_state *pfb = &batch->framebuffer;
   struct pipe_surface *psurf = pfb->cbufs[0];
   uint32_t texconst0 = fd6_tex_const_0(
      psurf->texture, psurf->u.tex.level, psurf->format, PIPE_SWIZZLE_X,
      PIPE_SWIZZLE_Y, PIPE_SWIZZLE_Z, PIPE_SWIZZLE_W);

   /* always TILE6_2 mode in GMEM.. which also means no swap: */
   texconst0 &=
      ~(A6XX_TEX_CONST_0_SWAP__MASK | A6XX_TEX_CONST_0_TILE_MODE__MASK);
   texconst0 |= A6XX_TEX_CONST_0_TILE_MODE(TILE6_2);

   for (unsigned i = 0; i < num_patches; i++) {
      struct fd_cs_patch *patch = fd_patch_element(&batch->fb_read_patches, i);
      patch->cs[0] = texconst0;
      patch->cs[2] = A6XX_TEX_CONST_2_PITCH(gmem->bin_w * gmem->cbuf_cpp[0]) |
                     A6XX_TEX_CONST_2_TYPE(A6XX_TEX_2D);
      patch->cs[4] = A6XX_TEX_CONST_4_BASE_LO(screen->gmem_base);
      patch->cs[5] = A6XX_TEX_CONST_5_BASE_HI(screen->gmem_base >> 32) |
                     A6XX_TEX_CONST_5_DEPTH(1);
   }
   util_dynarray_clear(&batch->fb_read_patches);
}

static void
patch_fb_read_sysmem(struct fd_batch *batch)
{
   unsigned num_patches = fd_patch_num_elements(&batch->fb_read_patches);
   if (!num_patches)
      return;

   struct pipe_framebuffer_state *pfb = &batch->framebuffer;
   struct pipe_surface *psurf = pfb->cbufs[0];
   if (!psurf)
      return;

   struct fd_resource *rsc = fd_resource(psurf->texture);
   unsigned lvl = psurf->u.tex.level;
   unsigned layer = psurf->u.tex.first_layer;
   bool ubwc_enabled = fd_resource_ubwc_enabled(rsc, lvl);
   uint64_t iova = fd_bo_get_iova(rsc->bo) + fd_resource_offset(rsc, lvl, layer);
   uint64_t ubwc_iova = fd_bo_get_iova(rsc->bo) + fd_resource_ubwc_offset(rsc, lvl, layer);
   uint32_t texconst0 = fd6_tex_const_0(
      psurf->texture, psurf->u.tex.level, psurf->format, PIPE_SWIZZLE_X,
      PIPE_SWIZZLE_Y, PIPE_SWIZZLE_Z, PIPE_SWIZZLE_W);
   uint32_t block_width, block_height;
   fdl6_get_ubwc_blockwidth(&rsc->layout, &block_width, &block_height);

   for (unsigned i = 0; i < num_patches; i++) {
      struct fd_cs_patch *patch = fd_patch_element(&batch->fb_read_patches, i);
      patch->cs[0] = texconst0;
      patch->cs[2] = A6XX_TEX_CONST_2_PITCH(fd_resource_pitch(rsc, lvl)) |
                     A6XX_TEX_CONST_2_TYPE(A6XX_TEX_2D);
      /* This is cheating a bit, since we can't use OUT_RELOC() here.. but
       * the render target will already have a reloc emitted for RB_MRT state,
       * so we can get away with manually patching in the address here:
       */
      patch->cs[4] = A6XX_TEX_CONST_4_BASE_LO(iova);
      patch->cs[5] = A6XX_TEX_CONST_5_BASE_HI(iova >> 32) |
                     A6XX_TEX_CONST_5_DEPTH(1);

      if (!ubwc_enabled)
         continue;

      patch->cs[3] |= A6XX_TEX_CONST_3_FLAG;
      patch->cs[7] = A6XX_TEX_CONST_7_FLAG_LO(ubwc_iova);
      patch->cs[8] = A6XX_TEX_CONST_8_FLAG_HI(ubwc_iova >> 32);
      patch->cs[9] = A6XX_TEX_CONST_9_FLAG_BUFFER_ARRAY_PITCH(
            rsc->layout.ubwc_layer_size >> 2);
      patch->cs[10] =
            A6XX_TEX_CONST_10_FLAG_BUFFER_PITCH(
               fdl_ubwc_pitch(&rsc->layout, lvl)) |
            A6XX_TEX_CONST_10_FLAG_BUFFER_LOGW(util_logbase2_ceil(
               DIV_ROUND_UP(u_minify(psurf->texture->width0, lvl), block_width))) |
            A6XX_TEX_CONST_10_FLAG_BUFFER_LOGH(util_logbase2_ceil(
               DIV_ROUND_UP(u_minify(psurf->texture->height0, lvl), block_height)));
   }
   util_dynarray_clear(&batch->fb_read_patches);
}

static void
update_render_cntl(struct fd_batch *batch, struct pipe_framebuffer_state *pfb,
                   bool binning)
{
   struct fd_ringbuffer *ring = batch->gmem;
   struct fd_screen *screen = batch->ctx->screen;
   uint32_t cntl = 0;
   bool depth_ubwc_enable = false;
   uint32_t mrts_ubwc_enable = 0;
   int i;

   if (pfb->zsbuf) {
      struct fd_resource *rsc = fd_resource(pfb->zsbuf->texture);
      depth_ubwc_enable =
         fd_resource_ubwc_enabled(rsc, pfb->zsbuf->u.tex.level);
   }

   for (i = 0; i < pfb->nr_cbufs; i++) {
      if (!pfb->cbufs[i])
         continue;

      struct pipe_surface *psurf = pfb->cbufs[i];
      struct fd_resource *rsc = fd_resource(psurf->texture);
      if (!rsc->bo)
         continue;

      if (fd_resource_ubwc_enabled(rsc, psurf->u.tex.level))
         mrts_ubwc_enable |= 1 << i;
   }

   cntl |= A6XX_RB_RENDER_CNTL_CCUSINGLECACHELINESIZE(2);
   if (binning)
      cntl |= A6XX_RB_RENDER_CNTL_BINNING;

   if (screen->info->a6xx.has_cp_reg_write) {
      OUT_PKT7(ring, CP_REG_WRITE, 3);
      OUT_RING(ring, CP_REG_WRITE_0_TRACKER(TRACK_RENDER_CNTL));
      OUT_RING(ring, REG_A6XX_RB_RENDER_CNTL);
   } else {
      OUT_PKT4(ring, REG_A6XX_RB_RENDER_CNTL, 1);
   }
   OUT_RING(ring, cntl |
                     COND(depth_ubwc_enable, A6XX_RB_RENDER_CNTL_FLAG_DEPTH) |
                     A6XX_RB_RENDER_CNTL_FLAG_MRTS(mrts_ubwc_enable));
}

/* extra size to store VSC_DRAW_STRM_SIZE: */
#define VSC_DRAW_STRM_SIZE(pitch) ((pitch)*32 + 0x100)
#define VSC_PRIM_STRM_SIZE(pitch) ((pitch)*32)

static void
update_vsc_pipe(struct fd_batch *batch)
{
   struct fd_context *ctx = batch->ctx;
   struct fd6_context *fd6_ctx = fd6_context(ctx);
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;
   struct fd_ringbuffer *ring = batch->gmem;
   int i;

   if (batch->draw_strm_bits / 8 > fd6_ctx->vsc_draw_strm_pitch) {
      if (fd6_ctx->vsc_draw_strm)
         fd_bo_del(fd6_ctx->vsc_draw_strm);
      fd6_ctx->vsc_draw_strm = NULL;
      /* Note: probably only need to align to 0x40, but aligning stronger
       * reduces the odds that we will have to realloc again on the next
       * frame:
       */
      fd6_ctx->vsc_draw_strm_pitch = align(batch->draw_strm_bits / 8, 0x4000);
      mesa_logd("pre-resize VSC_DRAW_STRM_PITCH to: 0x%x",
                fd6_ctx->vsc_draw_strm_pitch);
   }

   if (batch->prim_strm_bits / 8 > fd6_ctx->vsc_prim_strm_pitch) {
      if (fd6_ctx->vsc_prim_strm)
         fd_bo_del(fd6_ctx->vsc_prim_strm);
      fd6_ctx->vsc_prim_strm = NULL;
      fd6_ctx->vsc_prim_strm_pitch = align(batch->prim_strm_bits / 8, 0x4000);
      mesa_logd("pre-resize VSC_PRIM_STRM_PITCH to: 0x%x",
                fd6_ctx->vsc_prim_strm_pitch);
   }

   if (!fd6_ctx->vsc_draw_strm) {
      fd6_ctx->vsc_draw_strm = fd_bo_new(
         ctx->screen->dev, VSC_DRAW_STRM_SIZE(fd6_ctx->vsc_draw_strm_pitch),
         0, "vsc_draw_strm");
   }

   if (!fd6_ctx->vsc_prim_strm) {
      fd6_ctx->vsc_prim_strm = fd_bo_new(
         ctx->screen->dev, VSC_PRIM_STRM_SIZE(fd6_ctx->vsc_prim_strm_pitch),
         0, "vsc_prim_strm");
   }

   OUT_REG(
      ring, A6XX_VSC_BIN_SIZE(.width = gmem->bin_w, .height = gmem->bin_h),
      A6XX_VSC_DRAW_STRM_SIZE_ADDRESS(.bo = fd6_ctx->vsc_draw_strm,
                                      .bo_offset =
                                         32 * fd6_ctx->vsc_draw_strm_pitch));

   OUT_REG(ring, A6XX_VSC_BIN_COUNT(.nx = gmem->nbins_x, .ny = gmem->nbins_y));

   OUT_PKT4(ring, REG_A6XX_VSC_PIPE_CONFIG_REG(0), 32);
   for (i = 0; i < 32; i++) {
      const struct fd_vsc_pipe *pipe = &gmem->vsc_pipe[i];
      OUT_RING(ring, A6XX_VSC_PIPE_CONFIG_REG_X(pipe->x) |
                        A6XX_VSC_PIPE_CONFIG_REG_Y(pipe->y) |
                        A6XX_VSC_PIPE_CONFIG_REG_W(pipe->w) |
                        A6XX_VSC_PIPE_CONFIG_REG_H(pipe->h));
   }

   OUT_REG(
      ring, A6XX_VSC_PRIM_STRM_ADDRESS(.bo = fd6_ctx->vsc_prim_strm),
      A6XX_VSC_PRIM_STRM_PITCH(.dword = fd6_ctx->vsc_prim_strm_pitch),
      A6XX_VSC_PRIM_STRM_LIMIT(.dword = fd6_ctx->vsc_prim_strm_pitch - 64));

   OUT_REG(
      ring, A6XX_VSC_DRAW_STRM_ADDRESS(.bo = fd6_ctx->vsc_draw_strm),
      A6XX_VSC_DRAW_STRM_PITCH(.dword = fd6_ctx->vsc_draw_strm_pitch),
      A6XX_VSC_DRAW_STRM_LIMIT(.dword = fd6_ctx->vsc_draw_strm_pitch - 64));
}

/*
 * If overflow is detected, either 0x1 (VSC_DRAW_STRM overflow) or 0x3
 * (VSC_PRIM_STRM overflow) plus the size of the overflowed buffer is
 * written to control->vsc_overflow.  This allows the CPU to
 * detect which buffer overflowed (and, since the current size is
 * encoded as well, this protects against already-submitted but
 * not executed batches from fooling the CPU into increasing the
 * size again unnecessarily).
 */
static void
emit_vsc_overflow_test(struct fd_batch *batch)
{
   struct fd_ringbuffer *ring = batch->gmem;
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;
   struct fd6_context *fd6_ctx = fd6_context(batch->ctx);

   debug_assert((fd6_ctx->vsc_draw_strm_pitch & 0x3) == 0);
   debug_assert((fd6_ctx->vsc_prim_strm_pitch & 0x3) == 0);

   /* Check for overflow, write vsc_scratch if detected: */
   for (int i = 0; i < gmem->num_vsc_pipes; i++) {
      OUT_PKT7(ring, CP_COND_WRITE5, 8);
      OUT_RING(ring, CP_COND_WRITE5_0_FUNCTION(WRITE_GE) |
                        CP_COND_WRITE5_0_WRITE_MEMORY);
      OUT_RING(ring, CP_COND_WRITE5_1_POLL_ADDR_LO(
                        REG_A6XX_VSC_DRAW_STRM_SIZE_REG(i)));
      OUT_RING(ring, CP_COND_WRITE5_2_POLL_ADDR_HI(0));
      OUT_RING(ring, CP_COND_WRITE5_3_REF(fd6_ctx->vsc_draw_strm_pitch - 64));
      OUT_RING(ring, CP_COND_WRITE5_4_MASK(~0));
      OUT_RELOC(ring,
                control_ptr(fd6_ctx, vsc_overflow)); /* WRITE_ADDR_LO/HI */
      OUT_RING(ring,
               CP_COND_WRITE5_7_WRITE_DATA(1 + fd6_ctx->vsc_draw_strm_pitch));

      OUT_PKT7(ring, CP_COND_WRITE5, 8);
      OUT_RING(ring, CP_COND_WRITE5_0_FUNCTION(WRITE_GE) |
                        CP_COND_WRITE5_0_WRITE_MEMORY);
      OUT_RING(ring, CP_COND_WRITE5_1_POLL_ADDR_LO(
                        REG_A6XX_VSC_PRIM_STRM_SIZE_REG(i)));
      OUT_RING(ring, CP_COND_WRITE5_2_POLL_ADDR_HI(0));
      OUT_RING(ring, CP_COND_WRITE5_3_REF(fd6_ctx->vsc_prim_strm_pitch - 64));
      OUT_RING(ring, CP_COND_WRITE5_4_MASK(~0));
      OUT_RELOC(ring,
                control_ptr(fd6_ctx, vsc_overflow)); /* WRITE_ADDR_LO/HI */
      OUT_RING(ring,
               CP_COND_WRITE5_7_WRITE_DATA(3 + fd6_ctx->vsc_prim_strm_pitch));
   }

   OUT_PKT7(ring, CP_WAIT_MEM_WRITES, 0);
}

static void
check_vsc_overflow(struct fd_context *ctx)
{
   struct fd6_context *fd6_ctx = fd6_context(ctx);
   struct fd6_control *control = fd_bo_map(fd6_ctx->control_mem);
   uint32_t vsc_overflow = control->vsc_overflow;

   if (!vsc_overflow)
      return;

   /* clear overflow flag: */
   control->vsc_overflow = 0;

   unsigned buffer = vsc_overflow & 0x3;
   unsigned size = vsc_overflow & ~0x3;

   if (buffer == 0x1) {
      /* VSC_DRAW_STRM overflow: */

      if (size < fd6_ctx->vsc_draw_strm_pitch) {
         /* we've already increased the size, this overflow is
          * from a batch submitted before resize, but executed
          * after
          */
         return;
      }

      fd_bo_del(fd6_ctx->vsc_draw_strm);
      fd6_ctx->vsc_draw_strm = NULL;
      fd6_ctx->vsc_draw_strm_pitch *= 2;

      mesa_logd("resized VSC_DRAW_STRM_PITCH to: 0x%x",
                fd6_ctx->vsc_draw_strm_pitch);

   } else if (buffer == 0x3) {
      /* VSC_PRIM_STRM overflow: */

      if (size < fd6_ctx->vsc_prim_strm_pitch) {
         /* we've already increased the size */
         return;
      }

      fd_bo_del(fd6_ctx->vsc_prim_strm);
      fd6_ctx->vsc_prim_strm = NULL;
      fd6_ctx->vsc_prim_strm_pitch *= 2;

      mesa_logd("resized VSC_PRIM_STRM_PITCH to: 0x%x",
                fd6_ctx->vsc_prim_strm_pitch);

   } else {
      /* NOTE: it's possible, for example, for overflow to corrupt the
       * control page.  I mostly just see this hit if I set initial VSC
       * buffer size extremely small.  Things still seem to recover,
       * but maybe we should pre-emptively realloc vsc_data/vsc_data2
       * and hope for different memory placement?
       */
      mesa_loge("invalid vsc_overflow value: 0x%08x", vsc_overflow);
   }
}

static void
emit_common_init(struct fd_batch *batch)
{
   struct fd_ringbuffer *ring = batch->gmem;
   struct fd_autotune *at = &batch->ctx->autotune;
   struct fd_batch_result *result = batch->autotune_result;

   if (!result)
      return;

   OUT_PKT4(ring, REG_A6XX_RB_SAMPLE_COUNT_CONTROL, 1);
   OUT_RING(ring, A6XX_RB_SAMPLE_COUNT_CONTROL_COPY);

   OUT_PKT4(ring, REG_A6XX_RB_SAMPLE_COUNT_ADDR, 2);
   OUT_RELOC(ring, results_ptr(at, result[result->idx].samples_start));

   fd6_event_write(batch, ring, ZPASS_DONE, false);
}

static void
emit_common_fini(struct fd_batch *batch)
{
   struct fd_ringbuffer *ring = batch->gmem;
   struct fd_autotune *at = &batch->ctx->autotune;
   struct fd_batch_result *result = batch->autotune_result;

   if (!result)
      return;

   OUT_PKT4(ring, REG_A6XX_RB_SAMPLE_COUNT_CONTROL, 1);
   OUT_RING(ring, A6XX_RB_SAMPLE_COUNT_CONTROL_COPY);

   OUT_PKT4(ring, REG_A6XX_RB_SAMPLE_COUNT_ADDR, 2);
   OUT_RELOC(ring, results_ptr(at, result[result->idx].samples_end));

   fd6_event_write(batch, ring, ZPASS_DONE, false);

   // TODO is there a better event to use.. a single ZPASS_DONE_TS would be nice
   OUT_PKT7(ring, CP_EVENT_WRITE, 4);
   OUT_RING(ring, CP_EVENT_WRITE_0_EVENT(CACHE_FLUSH_TS));
   OUT_RELOC(ring, results_ptr(at, fence));
   OUT_RING(ring, result->fence);
}

/*
 * Emit conditional CP_INDIRECT_BRANCH based on VSC_STATE[p], ie. the IB
 * is skipped for tiles that have no visible geometry.
 */
static void
emit_conditional_ib(struct fd_batch *batch, const struct fd_tile *tile,
                    struct fd_ringbuffer *target)
{
   struct fd_ringbuffer *ring = batch->gmem;

   if (target->cur == target->start)
      return;

   emit_marker6(ring, 6);

   unsigned count = fd_ringbuffer_cmd_count(target);

   BEGIN_RING(ring, 5 + 4 * count); /* ensure conditional doesn't get split */

   OUT_PKT7(ring, CP_REG_TEST, 1);
   OUT_RING(ring, A6XX_CP_REG_TEST_0_REG(REG_A6XX_VSC_STATE_REG(tile->p)) |
                     A6XX_CP_REG_TEST_0_BIT(tile->n) |
                     A6XX_CP_REG_TEST_0_WAIT_FOR_ME);

   OUT_PKT7(ring, CP_COND_REG_EXEC, 2);
   OUT_RING(ring, CP_COND_REG_EXEC_0_MODE(PRED_TEST));
   OUT_RING(ring, CP_COND_REG_EXEC_1_DWORDS(4 * count));

   for (unsigned i = 0; i < count; i++) {
      uint32_t dwords;
      OUT_PKT7(ring, CP_INDIRECT_BUFFER, 3);
      dwords = fd_ringbuffer_emit_reloc_ring_full(ring, target, i) / 4;
      assert(dwords > 0);
      OUT_RING(ring, dwords);
   }

   emit_marker6(ring, 6);
}

static void
set_scissor(struct fd_ringbuffer *ring, uint32_t x1, uint32_t y1, uint32_t x2,
            uint32_t y2)
{
   OUT_REG(ring, A6XX_GRAS_SC_WINDOW_SCISSOR_TL(.x = x1, .y = y1),
           A6XX_GRAS_SC_WINDOW_SCISSOR_BR(.x = x2, .y = y2));

   OUT_REG(ring, A6XX_GRAS_2D_RESOLVE_CNTL_1(.x = x1, .y = y1),
           A6XX_GRAS_2D_RESOLVE_CNTL_2(.x = x2, .y = y2));
}

static void
set_bin_size(struct fd_ringbuffer *ring, uint32_t w, uint32_t h, uint32_t flag)
{
   OUT_REG(ring, A6XX_GRAS_BIN_CONTROL(.binw = w, .binh = h, .dword = flag));
   OUT_REG(ring, A6XX_RB_BIN_CONTROL(.binw = w, .binh = h, .dword = flag));
   /* no flag for RB_BIN_CONTROL2... */
   OUT_REG(ring, A6XX_RB_BIN_CONTROL2(.binw = w, .binh = h));
}

static void
emit_binning_pass(struct fd_batch *batch) assert_dt
{
   struct fd_ringbuffer *ring = batch->gmem;
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;
   struct fd_screen *screen = batch->ctx->screen;

   debug_assert(!batch->tessellation);

   set_scissor(ring, 0, 0, gmem->width - 1, gmem->height - 1);

   emit_marker6(ring, 7);
   OUT_PKT7(ring, CP_SET_MARKER, 1);
   OUT_RING(ring, A6XX_CP_SET_MARKER_0_MODE(RM6_BINNING));
   emit_marker6(ring, 7);

   OUT_PKT7(ring, CP_SET_VISIBILITY_OVERRIDE, 1);
   OUT_RING(ring, 0x1);

   OUT_PKT7(ring, CP_SET_MODE, 1);
   OUT_RING(ring, 0x1);

   OUT_WFI5(ring);

   OUT_REG(ring, A6XX_VFD_MODE_CNTL(.render_mode = BINNING_PASS));

   update_vsc_pipe(batch);

   OUT_PKT4(ring, REG_A6XX_PC_POWER_CNTL, 1);
   OUT_RING(ring, screen->info->a6xx.magic.PC_POWER_CNTL);

   OUT_PKT4(ring, REG_A6XX_VFD_POWER_CNTL, 1);
   OUT_RING(ring, screen->info->a6xx.magic.PC_POWER_CNTL);

   OUT_PKT7(ring, CP_EVENT_WRITE, 1);
   OUT_RING(ring, UNK_2C);

   OUT_PKT4(ring, REG_A6XX_RB_WINDOW_OFFSET, 1);
   OUT_RING(ring, A6XX_RB_WINDOW_OFFSET_X(0) | A6XX_RB_WINDOW_OFFSET_Y(0));

   OUT_PKT4(ring, REG_A6XX_SP_TP_WINDOW_OFFSET, 1);
   OUT_RING(ring,
            A6XX_SP_TP_WINDOW_OFFSET_X(0) | A6XX_SP_TP_WINDOW_OFFSET_Y(0));

   /* emit IB to binning drawcmds: */
   trace_start_binning_ib(&batch->trace, ring);
   fd6_emit_ib(ring, batch->draw);
   trace_end_binning_ib(&batch->trace, ring);

   fd_reset_wfi(batch);

   OUT_PKT7(ring, CP_SET_DRAW_STATE, 3);
   OUT_RING(ring, CP_SET_DRAW_STATE__0_COUNT(0) |
                     CP_SET_DRAW_STATE__0_DISABLE_ALL_GROUPS |
                     CP_SET_DRAW_STATE__0_GROUP_ID(0));
   OUT_RING(ring, CP_SET_DRAW_STATE__1_ADDR_LO(0));
   OUT_RING(ring, CP_SET_DRAW_STATE__2_ADDR_HI(0));

   OUT_PKT7(ring, CP_EVENT_WRITE, 1);
   OUT_RING(ring, UNK_2D);

   fd6_cache_inv(batch, ring);
   fd6_cache_flush(batch, ring);
   fd_wfi(batch, ring);

   OUT_PKT7(ring, CP_WAIT_FOR_ME, 0);

   trace_start_vsc_overflow_test(&batch->trace, batch->gmem);
   emit_vsc_overflow_test(batch);
   trace_end_vsc_overflow_test(&batch->trace, batch->gmem);

   OUT_PKT7(ring, CP_SET_VISIBILITY_OVERRIDE, 1);
   OUT_RING(ring, 0x0);

   OUT_PKT7(ring, CP_SET_MODE, 1);
   OUT_RING(ring, 0x0);

   OUT_WFI5(ring);

   OUT_REG(ring,
           A6XX_RB_CCU_CNTL(.color_offset = screen->ccu_offset_gmem,
                            .gmem = true,
                            .unk2 = screen->info->a6xx.ccu_cntl_gmem_unk2));
}

static void
emit_msaa(struct fd_ringbuffer *ring, unsigned nr)
{
   enum a3xx_msaa_samples samples = fd_msaa_samples(nr);

   OUT_PKT4(ring, REG_A6XX_SP_TP_RAS_MSAA_CNTL, 2);
   OUT_RING(ring, A6XX_SP_TP_RAS_MSAA_CNTL_SAMPLES(samples));
   OUT_RING(ring, A6XX_SP_TP_DEST_MSAA_CNTL_SAMPLES(samples) |
                     COND(samples == MSAA_ONE,
                          A6XX_SP_TP_DEST_MSAA_CNTL_MSAA_DISABLE));

   OUT_PKT4(ring, REG_A6XX_GRAS_RAS_MSAA_CNTL, 2);
   OUT_RING(ring, A6XX_GRAS_RAS_MSAA_CNTL_SAMPLES(samples));
   OUT_RING(ring, A6XX_GRAS_DEST_MSAA_CNTL_SAMPLES(samples) |
                     COND(samples == MSAA_ONE,
                          A6XX_GRAS_DEST_MSAA_CNTL_MSAA_DISABLE));

   OUT_PKT4(ring, REG_A6XX_RB_RAS_MSAA_CNTL, 2);
   OUT_RING(ring, A6XX_RB_RAS_MSAA_CNTL_SAMPLES(samples));
   OUT_RING(ring,
            A6XX_RB_DEST_MSAA_CNTL_SAMPLES(samples) |
               COND(samples == MSAA_ONE, A6XX_RB_DEST_MSAA_CNTL_MSAA_DISABLE));

   OUT_PKT4(ring, REG_A6XX_RB_MSAA_CNTL, 1);
   OUT_RING(ring, A6XX_RB_MSAA_CNTL_SAMPLES(samples));
}

static void prepare_tile_setup_ib(struct fd_batch *batch);
static void prepare_tile_fini_ib(struct fd_batch *batch);

/* before first tile */
static void
fd6_emit_tile_init(struct fd_batch *batch) assert_dt
{
   struct fd_ringbuffer *ring = batch->gmem;
   struct pipe_framebuffer_state *pfb = &batch->framebuffer;
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;
   struct fd_screen *screen = batch->ctx->screen;

   fd6_emit_restore(batch, ring);

   fd6_emit_lrz_flush(ring);

   if (batch->prologue) {
      trace_start_prologue(&batch->trace, ring);
      fd6_emit_ib(ring, batch->prologue);
      trace_end_prologue(&batch->trace, ring);
   }

   fd6_cache_inv(batch, ring);

   prepare_tile_setup_ib(batch);
   prepare_tile_fini_ib(batch);

   OUT_PKT7(ring, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   OUT_RING(ring, 0x0);

   /* blob controls "local" in IB2, but I think that is not required */
   OUT_PKT7(ring, CP_SKIP_IB2_ENABLE_LOCAL, 1);
   OUT_RING(ring, 0x1);

   fd_wfi(batch, ring);
   OUT_REG(ring,
           A6XX_RB_CCU_CNTL(.color_offset = screen->ccu_offset_gmem,
                            .gmem = true,
                            .unk2 = screen->info->a6xx.ccu_cntl_gmem_unk2));

   emit_zs(ring, pfb->zsbuf, batch->gmem_state);
   emit_mrt(ring, pfb, batch->gmem_state);
   emit_msaa(ring, pfb->samples);
   patch_fb_read_gmem(batch);

   if (use_hw_binning(batch)) {
      /* enable stream-out during binning pass: */
      OUT_REG(ring, A6XX_VPC_SO_DISABLE(false));

      set_bin_size(ring, gmem->bin_w, gmem->bin_h,
                   A6XX_RB_BIN_CONTROL_RENDER_MODE(BINNING_PASS) |
                   A6XX_RB_BIN_CONTROL_LRZ_FEEDBACK_ZMODE_MASK(0x6));
      update_render_cntl(batch, pfb, true);
      emit_binning_pass(batch);

      /* and disable stream-out for draw pass: */
      OUT_REG(ring, A6XX_VPC_SO_DISABLE(true));

      /*
       * NOTE: even if we detect VSC overflow and disable use of
       * visibility stream in draw pass, it is still safe to execute
       * the reset of these cmds:
       */

      // NOTE a618 not setting .FORCE_LRZ_WRITE_DIS .. 
      set_bin_size(ring, gmem->bin_w, gmem->bin_h,
                   A6XX_RB_BIN_CONTROL_FORCE_LRZ_WRITE_DIS |
                   A6XX_RB_BIN_CONTROL_LRZ_FEEDBACK_ZMODE_MASK(0x6));

      OUT_PKT4(ring, REG_A6XX_VFD_MODE_CNTL, 1);
      OUT_RING(ring, 0x0);

      OUT_PKT4(ring, REG_A6XX_PC_POWER_CNTL, 1);
      OUT_RING(ring, screen->info->a6xx.magic.PC_POWER_CNTL);

      OUT_PKT4(ring, REG_A6XX_VFD_POWER_CNTL, 1);
      OUT_RING(ring, screen->info->a6xx.magic.PC_POWER_CNTL);

      OUT_PKT7(ring, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
      OUT_RING(ring, 0x1);
   } else {
      /* no binning pass, so enable stream-out for draw pass:: */
      OUT_REG(ring, A6XX_VPC_SO_DISABLE(false));

      set_bin_size(ring, gmem->bin_w, gmem->bin_h, 0x6000000);
   }

   update_render_cntl(batch, pfb, false);

   emit_common_init(batch);
}

static void
set_window_offset(struct fd_ringbuffer *ring, uint32_t x1, uint32_t y1)
{
   OUT_PKT4(ring, REG_A6XX_RB_WINDOW_OFFSET, 1);
   OUT_RING(ring, A6XX_RB_WINDOW_OFFSET_X(x1) | A6XX_RB_WINDOW_OFFSET_Y(y1));

   OUT_PKT4(ring, REG_A6XX_RB_WINDOW_OFFSET2, 1);
   OUT_RING(ring, A6XX_RB_WINDOW_OFFSET2_X(x1) | A6XX_RB_WINDOW_OFFSET2_Y(y1));

   OUT_PKT4(ring, REG_A6XX_SP_WINDOW_OFFSET, 1);
   OUT_RING(ring, A6XX_SP_WINDOW_OFFSET_X(x1) | A6XX_SP_WINDOW_OFFSET_Y(y1));

   OUT_PKT4(ring, REG_A6XX_SP_TP_WINDOW_OFFSET, 1);
   OUT_RING(ring,
            A6XX_SP_TP_WINDOW_OFFSET_X(x1) | A6XX_SP_TP_WINDOW_OFFSET_Y(y1));
}

/* before mem2gmem */
static void
fd6_emit_tile_prep(struct fd_batch *batch, const struct fd_tile *tile)
{
   struct fd_context *ctx = batch->ctx;
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;
   struct fd6_context *fd6_ctx = fd6_context(ctx);
   struct fd_ringbuffer *ring = batch->gmem;

   emit_marker6(ring, 7);
   OUT_PKT7(ring, CP_SET_MARKER, 1);
   OUT_RING(ring, A6XX_CP_SET_MARKER_0_MODE(RM6_GMEM));
   emit_marker6(ring, 7);

   uint32_t x1 = tile->xoff;
   uint32_t y1 = tile->yoff;
   uint32_t x2 = tile->xoff + tile->bin_w - 1;
   uint32_t y2 = tile->yoff + tile->bin_h - 1;

   set_scissor(ring, x1, y1, x2, y2);

   if (use_hw_binning(batch)) {
      const struct fd_vsc_pipe *pipe = &gmem->vsc_pipe[tile->p];

      OUT_PKT7(ring, CP_WAIT_FOR_ME, 0);

      OUT_PKT7(ring, CP_SET_MODE, 1);
      OUT_RING(ring, 0x0);

      OUT_PKT7(ring, CP_SET_BIN_DATA5, 7);
      OUT_RING(ring, CP_SET_BIN_DATA5_0_VSC_SIZE(pipe->w * pipe->h) |
                        CP_SET_BIN_DATA5_0_VSC_N(tile->n));
      OUT_RELOC(ring, fd6_ctx->vsc_draw_strm, /* per-pipe draw-stream address */
                (tile->p * fd6_ctx->vsc_draw_strm_pitch), 0, 0);
      OUT_RELOC(ring,
                fd6_ctx->vsc_draw_strm, /* VSC_DRAW_STRM_ADDRESS + (p * 4) */
                (tile->p * 4) + (32 * fd6_ctx->vsc_draw_strm_pitch), 0, 0);
      OUT_RELOC(ring, fd6_ctx->vsc_prim_strm,
                (tile->p * fd6_ctx->vsc_prim_strm_pitch), 0, 0);

      OUT_PKT7(ring, CP_SET_VISIBILITY_OVERRIDE, 1);
      OUT_RING(ring, 0x0);

      set_window_offset(ring, x1, y1);

      const struct fd_gmem_stateobj *gmem = batch->gmem_state;
      set_bin_size(ring, gmem->bin_w, gmem->bin_h, 0x6000000);

      OUT_PKT7(ring, CP_SET_MODE, 1);
      OUT_RING(ring, 0x0);
   } else {
      set_window_offset(ring, x1, y1);

      OUT_PKT7(ring, CP_SET_VISIBILITY_OVERRIDE, 1);
      OUT_RING(ring, 0x1);

      OUT_PKT7(ring, CP_SET_MODE, 1);
      OUT_RING(ring, 0x0);
   }
}

static void
set_blit_scissor(struct fd_batch *batch, struct fd_ringbuffer *ring)
{
   struct pipe_scissor_state blit_scissor = batch->max_scissor;

   blit_scissor.minx = ROUND_DOWN_TO(blit_scissor.minx, 16);
   blit_scissor.miny = ROUND_DOWN_TO(blit_scissor.miny, 4);
   blit_scissor.maxx = ALIGN(blit_scissor.maxx, 16);
   blit_scissor.maxy = ALIGN(blit_scissor.maxy, 4);

   OUT_PKT4(ring, REG_A6XX_RB_BLIT_SCISSOR_TL, 2);
   OUT_RING(ring, A6XX_RB_BLIT_SCISSOR_TL_X(blit_scissor.minx) |
                     A6XX_RB_BLIT_SCISSOR_TL_Y(blit_scissor.miny));
   OUT_RING(ring, A6XX_RB_BLIT_SCISSOR_BR_X(blit_scissor.maxx - 1) |
                     A6XX_RB_BLIT_SCISSOR_BR_Y(blit_scissor.maxy - 1));
}

static void
emit_blit(struct fd_batch *batch, struct fd_ringbuffer *ring, uint32_t base,
          struct pipe_surface *psurf, bool stencil)
{
   struct fd_resource *rsc = fd_resource(psurf->texture);
   enum pipe_format pfmt = psurf->format;
   uint32_t offset;
   bool ubwc_enabled;

   debug_assert(psurf->u.tex.first_layer == psurf->u.tex.last_layer);

   /* separate stencil case: */
   if (stencil) {
      rsc = rsc->stencil;
      pfmt = rsc->b.b.format;
   }

   offset =
      fd_resource_offset(rsc, psurf->u.tex.level, psurf->u.tex.first_layer);
   ubwc_enabled = fd_resource_ubwc_enabled(rsc, psurf->u.tex.level);

   debug_assert(psurf->u.tex.first_layer == psurf->u.tex.last_layer);

   enum a6xx_format format = fd6_pipe2color(pfmt);
   uint32_t stride = fd_resource_pitch(rsc, psurf->u.tex.level);
   uint32_t size = fd_resource_slice(rsc, psurf->u.tex.level)->size0;
   enum a3xx_color_swap swap = fd6_resource_swap(rsc, pfmt);
   enum a3xx_msaa_samples samples = fd_msaa_samples(rsc->b.b.nr_samples);
   uint32_t tile_mode = fd_resource_tile_mode(&rsc->b.b, psurf->u.tex.level);

   OUT_REG(ring,
           A6XX_RB_BLIT_DST_INFO(.tile_mode = tile_mode, .samples = samples,
                                 .color_format = format, .color_swap = swap,
                                 .flags = ubwc_enabled),
           A6XX_RB_BLIT_DST(.bo = rsc->bo, .bo_offset = offset),
           A6XX_RB_BLIT_DST_PITCH(.a6xx_rb_blit_dst_pitch = stride),
           A6XX_RB_BLIT_DST_ARRAY_PITCH(.a6xx_rb_blit_dst_array_pitch = size));

   OUT_REG(ring, A6XX_RB_BLIT_BASE_GMEM(.dword = base));

   if (ubwc_enabled) {
      OUT_PKT4(ring, REG_A6XX_RB_BLIT_FLAG_DST, 3);
      fd6_emit_flag_reference(ring, rsc, psurf->u.tex.level,
                              psurf->u.tex.first_layer);
   }

   fd6_emit_blit(batch, ring);
}

static void
emit_restore_blit(struct fd_batch *batch, struct fd_ringbuffer *ring,
                  uint32_t base, struct pipe_surface *psurf, unsigned buffer)
{
   bool stencil = (buffer == FD_BUFFER_STENCIL);

   OUT_REG(ring, A6XX_RB_BLIT_INFO(.gmem = true, .unk0 = true,
                                   .depth = (buffer == FD_BUFFER_DEPTH),
                                   .sample_0 = util_format_is_pure_integer(
                                      psurf->format)));

   emit_blit(batch, ring, base, psurf, stencil);
}

static void
emit_clears(struct fd_batch *batch, struct fd_ringbuffer *ring)
{
   struct pipe_framebuffer_state *pfb = &batch->framebuffer;
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;
   enum a3xx_msaa_samples samples = fd_msaa_samples(pfb->samples);

   uint32_t buffers = batch->fast_cleared;

   if (buffers & PIPE_CLEAR_COLOR) {

      for (int i = 0; i < pfb->nr_cbufs; i++) {
         union pipe_color_union *color = &batch->clear_color[i];
         union util_color uc = {0};

         if (!pfb->cbufs[i])
            continue;

         if (!(buffers & (PIPE_CLEAR_COLOR0 << i)))
            continue;

         enum pipe_format pfmt = pfb->cbufs[i]->format;

         // XXX I think RB_CLEAR_COLOR_DWn wants to take into account SWAP??
         union pipe_color_union swapped;
         switch (fd6_pipe2swap(pfmt)) {
         case WZYX:
            swapped.ui[0] = color->ui[0];
            swapped.ui[1] = color->ui[1];
            swapped.ui[2] = color->ui[2];
            swapped.ui[3] = color->ui[3];
            break;
         case WXYZ:
            swapped.ui[2] = color->ui[0];
            swapped.ui[1] = color->ui[1];
            swapped.ui[0] = color->ui[2];
            swapped.ui[3] = color->ui[3];
            break;
         case ZYXW:
            swapped.ui[3] = color->ui[0];
            swapped.ui[0] = color->ui[1];
            swapped.ui[1] = color->ui[2];
            swapped.ui[2] = color->ui[3];
            break;
         case XYZW:
            swapped.ui[3] = color->ui[0];
            swapped.ui[2] = color->ui[1];
            swapped.ui[1] = color->ui[2];
            swapped.ui[0] = color->ui[3];
            break;
         }

         util_pack_color_union(pfmt, &uc, &swapped);

         OUT_PKT4(ring, REG_A6XX_RB_BLIT_DST_INFO, 1);
         OUT_RING(ring,
                  A6XX_RB_BLIT_DST_INFO_TILE_MODE(TILE6_LINEAR) |
                     A6XX_RB_BLIT_DST_INFO_SAMPLES(samples) |
                     A6XX_RB_BLIT_DST_INFO_COLOR_FORMAT(fd6_pipe2color(pfmt)));

         OUT_PKT4(ring, REG_A6XX_RB_BLIT_INFO, 1);
         OUT_RING(ring,
                  A6XX_RB_BLIT_INFO_GMEM | A6XX_RB_BLIT_INFO_CLEAR_MASK(0xf));

         OUT_PKT4(ring, REG_A6XX_RB_BLIT_BASE_GMEM, 1);
         OUT_RING(ring, gmem->cbuf_base[i]);

         OUT_PKT4(ring, REG_A6XX_RB_UNKNOWN_88D0, 1);
         OUT_RING(ring, 0);

         OUT_PKT4(ring, REG_A6XX_RB_BLIT_CLEAR_COLOR_DW0, 4);
         OUT_RING(ring, uc.ui[0]);
         OUT_RING(ring, uc.ui[1]);
         OUT_RING(ring, uc.ui[2]);
         OUT_RING(ring, uc.ui[3]);

         fd6_emit_blit(batch, ring);
      }
   }

   const bool has_depth = pfb->zsbuf;
   const bool has_separate_stencil =
      has_depth && fd_resource(pfb->zsbuf->texture)->stencil;

   /* First clear depth or combined depth/stencil. */
   if ((has_depth && (buffers & PIPE_CLEAR_DEPTH)) ||
       (!has_separate_stencil && (buffers & PIPE_CLEAR_STENCIL))) {
      enum pipe_format pfmt = pfb->zsbuf->format;
      uint32_t clear_value;
      uint32_t mask = 0;

      if (has_separate_stencil) {
         pfmt = util_format_get_depth_only(pfb->zsbuf->format);
         clear_value = util_pack_z(pfmt, batch->clear_depth);
      } else {
         pfmt = pfb->zsbuf->format;
         clear_value =
            util_pack_z_stencil(pfmt, batch->clear_depth, batch->clear_stencil);
      }

      if (buffers & PIPE_CLEAR_DEPTH)
         mask |= 0x1;

      if (!has_separate_stencil && (buffers & PIPE_CLEAR_STENCIL))
         mask |= 0x2;

      OUT_PKT4(ring, REG_A6XX_RB_BLIT_DST_INFO, 1);
      OUT_RING(ring,
               A6XX_RB_BLIT_DST_INFO_TILE_MODE(TILE6_LINEAR) |
                  A6XX_RB_BLIT_DST_INFO_SAMPLES(samples) |
                  A6XX_RB_BLIT_DST_INFO_COLOR_FORMAT(fd6_pipe2color(pfmt)));

      OUT_PKT4(ring, REG_A6XX_RB_BLIT_INFO, 1);
      OUT_RING(ring, A6XX_RB_BLIT_INFO_GMEM |
                        // XXX UNK0 for separate stencil ??
                        A6XX_RB_BLIT_INFO_DEPTH |
                        A6XX_RB_BLIT_INFO_CLEAR_MASK(mask));

      OUT_PKT4(ring, REG_A6XX_RB_BLIT_BASE_GMEM, 1);
      OUT_RING(ring, gmem->zsbuf_base[0]);

      OUT_PKT4(ring, REG_A6XX_RB_UNKNOWN_88D0, 1);
      OUT_RING(ring, 0);

      OUT_PKT4(ring, REG_A6XX_RB_BLIT_CLEAR_COLOR_DW0, 1);
      OUT_RING(ring, clear_value);

      fd6_emit_blit(batch, ring);
   }

   /* Then clear the separate stencil buffer in case of 32 bit depth
    * formats with separate stencil. */
   if (has_separate_stencil && (buffers & PIPE_CLEAR_STENCIL)) {
      OUT_PKT4(ring, REG_A6XX_RB_BLIT_DST_INFO, 1);
      OUT_RING(ring, A6XX_RB_BLIT_DST_INFO_TILE_MODE(TILE6_LINEAR) |
                        A6XX_RB_BLIT_DST_INFO_SAMPLES(samples) |
                        A6XX_RB_BLIT_DST_INFO_COLOR_FORMAT(FMT6_8_UINT));

      OUT_PKT4(ring, REG_A6XX_RB_BLIT_INFO, 1);
      OUT_RING(ring, A6XX_RB_BLIT_INFO_GMEM |
                        // A6XX_RB_BLIT_INFO_UNK0 |
                        A6XX_RB_BLIT_INFO_DEPTH |
                        A6XX_RB_BLIT_INFO_CLEAR_MASK(0x1));

      OUT_PKT4(ring, REG_A6XX_RB_BLIT_BASE_GMEM, 1);
      OUT_RING(ring, gmem->zsbuf_base[1]);

      OUT_PKT4(ring, REG_A6XX_RB_UNKNOWN_88D0, 1);
      OUT_RING(ring, 0);

      OUT_PKT4(ring, REG_A6XX_RB_BLIT_CLEAR_COLOR_DW0, 1);
      OUT_RING(ring, batch->clear_stencil & 0xff);

      fd6_emit_blit(batch, ring);
   }
}

/*
 * transfer from system memory to gmem
 */
static void
emit_restore_blits(struct fd_batch *batch, struct fd_ringbuffer *ring)
{
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;
   struct pipe_framebuffer_state *pfb = &batch->framebuffer;

   if (batch->restore & FD_BUFFER_COLOR) {
      unsigned i;
      for (i = 0; i < pfb->nr_cbufs; i++) {
         if (!pfb->cbufs[i])
            continue;
         if (!(batch->restore & (PIPE_CLEAR_COLOR0 << i)))
            continue;
         emit_restore_blit(batch, ring, gmem->cbuf_base[i], pfb->cbufs[i],
                           FD_BUFFER_COLOR);
      }
   }

   if (batch->restore & (FD_BUFFER_DEPTH | FD_BUFFER_STENCIL)) {
      struct fd_resource *rsc = fd_resource(pfb->zsbuf->texture);

      if (!rsc->stencil || (batch->restore & FD_BUFFER_DEPTH)) {
         emit_restore_blit(batch, ring, gmem->zsbuf_base[0], pfb->zsbuf,
                           FD_BUFFER_DEPTH);
      }
      if (rsc->stencil && (batch->restore & FD_BUFFER_STENCIL)) {
         emit_restore_blit(batch, ring, gmem->zsbuf_base[1], pfb->zsbuf,
                           FD_BUFFER_STENCIL);
      }
   }
}

static void
prepare_tile_setup_ib(struct fd_batch *batch)
{
   if (!(batch->restore || batch->fast_cleared))
      return;

   batch->tile_setup =
      fd_submit_new_ringbuffer(batch->submit, 0x1000, FD_RINGBUFFER_STREAMING);

   set_blit_scissor(batch, batch->tile_setup);

   emit_restore_blits(batch, batch->tile_setup);
   emit_clears(batch, batch->tile_setup);
}

/*
 * transfer from system memory to gmem
 */
static void
fd6_emit_tile_mem2gmem(struct fd_batch *batch, const struct fd_tile *tile)
{
}

/* before IB to rendering cmds: */
static void
fd6_emit_tile_renderprep(struct fd_batch *batch, const struct fd_tile *tile)
{
   if (!batch->tile_setup)
      return;

   trace_start_clear_restore(&batch->trace, batch->gmem, batch->fast_cleared);
   if (batch->fast_cleared || !use_hw_binning(batch)) {
      fd6_emit_ib(batch->gmem, batch->tile_setup);
   } else {
      emit_conditional_ib(batch, tile, batch->tile_setup);
   }
   trace_end_clear_restore(&batch->trace, batch->gmem);
}

static bool
blit_can_resolve(enum pipe_format format)
{
   const struct util_format_description *desc = util_format_description(format);

   /* blit event can only do resolve for simple cases:
    * averaging samples as unsigned integers or choosing only one sample
    */
   if (util_format_is_snorm(format) || util_format_is_srgb(format))
      return false;

   /* can't do formats with larger channel sizes
    * note: this includes all float formats
    * note2: single channel integer formats seem OK
    */
   if (desc->channel[0].size > 10)
      return false;

   switch (format) {
   /* for unknown reasons blit event can't msaa resolve these formats when tiled
    * likely related to these formats having different layout from other cpp=2
    * formats
    */
   case PIPE_FORMAT_R8G8_UNORM:
   case PIPE_FORMAT_R8G8_UINT:
   case PIPE_FORMAT_R8G8_SINT:
   /* TODO: this one should be able to work? */
   case PIPE_FORMAT_Z24_UNORM_S8_UINT:
      return false;
   default:
      break;
   }

   return true;
}

static bool
needs_resolve(struct pipe_surface *psurf)
{
   return psurf->nr_samples &&
          (psurf->nr_samples != psurf->texture->nr_samples);
}

static void
emit_resolve_blit(struct fd_batch *batch, struct fd_ringbuffer *ring,
                  uint32_t base, struct pipe_surface *psurf,
                  unsigned buffer) assert_dt
{
   uint32_t info = 0;
   bool stencil = false;

   if (!fd_resource(psurf->texture)->valid)
      return;

   /* if we need to resolve, but cannot with BLIT event, we instead need
    * to generate per-tile CP_BLIT (r2d) commands:
    *
    * The separate-stencil is a special case, we might need to use CP_BLIT
    * for depth, but we can still resolve stencil with a BLIT event
    */
   if (needs_resolve(psurf) && !blit_can_resolve(psurf->format) &&
       (buffer != FD_BUFFER_STENCIL)) {
      fd6_resolve_tile(batch, ring, base, psurf);
      return;
   }

   switch (buffer) {
   case FD_BUFFER_COLOR:
      break;
   case FD_BUFFER_STENCIL:
      info |= A6XX_RB_BLIT_INFO_UNK0;
      stencil = true;
      break;
   case FD_BUFFER_DEPTH:
      info |= A6XX_RB_BLIT_INFO_DEPTH;
      break;
   }

   if (util_format_is_pure_integer(psurf->format) ||
       util_format_is_depth_or_stencil(psurf->format))
      info |= A6XX_RB_BLIT_INFO_SAMPLE_0;

   OUT_PKT4(ring, REG_A6XX_RB_BLIT_INFO, 1);
   OUT_RING(ring, info);

   emit_blit(batch, ring, base, psurf, stencil);
}

/*
 * transfer from gmem to system memory (ie. normal RAM)
 */

static void
prepare_tile_fini_ib(struct fd_batch *batch) assert_dt
{
   const struct fd_gmem_stateobj *gmem = batch->gmem_state;
   struct pipe_framebuffer_state *pfb = &batch->framebuffer;
   struct fd_ringbuffer *ring;

   batch->tile_fini =
      fd_submit_new_ringbuffer(batch->submit, 0x1000, FD_RINGBUFFER_STREAMING);
   ring = batch->tile_fini;

   set_blit_scissor(batch, ring);

   if (batch->resolve & (FD_BUFFER_DEPTH | FD_BUFFER_STENCIL)) {
      struct fd_resource *rsc = fd_resource(pfb->zsbuf->texture);

      if (!rsc->stencil || (batch->resolve & FD_BUFFER_DEPTH)) {
         emit_resolve_blit(batch, ring, gmem->zsbuf_base[0], pfb->zsbuf,
                           FD_BUFFER_DEPTH);
      }
      if (rsc->stencil && (batch->resolve & FD_BUFFER_STENCIL)) {
         emit_resolve_blit(batch, ring, gmem->zsbuf_base[1], pfb->zsbuf,
                           FD_BUFFER_STENCIL);
      }
   }

   if (batch->resolve & FD_BUFFER_COLOR) {
      unsigned i;
      for (i = 0; i < pfb->nr_cbufs; i++) {
         if (!pfb->cbufs[i])
            continue;
         if (!(batch->resolve & (PIPE_CLEAR_COLOR0 << i)))
            continue;
         emit_resolve_blit(batch, ring, gmem->cbuf_base[i], pfb->cbufs[i],
                           FD_BUFFER_COLOR);
      }
   }
}

static void
fd6_emit_tile(struct fd_batch *batch, const struct fd_tile *tile)
{
   if (!use_hw_binning(batch)) {
      fd6_emit_ib(batch->gmem, batch->draw);
   } else {
      emit_conditional_ib(batch, tile, batch->draw);
   }

   if (batch->epilogue)
      fd6_emit_ib(batch->gmem, batch->epilogue);
}

static void
fd6_emit_tile_gmem2mem(struct fd_batch *batch, const struct fd_tile *tile)
{
   struct fd_ringbuffer *ring = batch->gmem;

   if (use_hw_binning(batch)) {
      OUT_PKT7(ring, CP_SET_MARKER, 1);
      OUT_RING(ring, A6XX_CP_SET_MARKER_0_MODE(RM6_ENDVIS));
   }

   OUT_PKT7(ring, CP_SET_DRAW_STATE, 3);
   OUT_RING(ring, CP_SET_DRAW_STATE__0_COUNT(0) |
                     CP_SET_DRAW_STATE__0_DISABLE_ALL_GROUPS |
                     CP_SET_DRAW_STATE__0_GROUP_ID(0));
   OUT_RING(ring, CP_SET_DRAW_STATE__1_ADDR_LO(0));
   OUT_RING(ring, CP_SET_DRAW_STATE__2_ADDR_HI(0));

   OUT_PKT7(ring, CP_SKIP_IB2_ENABLE_LOCAL, 1);
   OUT_RING(ring, 0x0);

   emit_marker6(ring, 7);
   OUT_PKT7(ring, CP_SET_MARKER, 1);
   OUT_RING(ring, A6XX_CP_SET_MARKER_0_MODE(RM6_RESOLVE));
   emit_marker6(ring, 7);

   trace_start_resolve(&batch->trace, batch->gmem);
   if (batch->fast_cleared || !use_hw_binning(batch)) {
      fd6_emit_ib(batch->gmem, batch->tile_fini);
   } else {
      emit_conditional_ib(batch, tile, batch->tile_fini);
   }
   trace_end_resolve(&batch->trace, batch->gmem);
}

static void
fd6_emit_tile_fini(struct fd_batch *batch)
{
   struct fd_ringbuffer *ring = batch->gmem;

   emit_common_fini(batch);

   OUT_PKT4(ring, REG_A6XX_GRAS_LRZ_CNTL, 1);
   OUT_RING(ring, A6XX_GRAS_LRZ_CNTL_ENABLE);

   fd6_emit_lrz_flush(ring);

   fd6_event_write(batch, ring, PC_CCU_RESOLVE_TS, true);

   if (use_hw_binning(batch)) {
      check_vsc_overflow(batch->ctx);
   }
}

static void
emit_sysmem_clears(struct fd_batch *batch, struct fd_ringbuffer *ring) assert_dt
{
   struct fd_context *ctx = batch->ctx;
   struct pipe_framebuffer_state *pfb = &batch->framebuffer;

   uint32_t buffers = batch->fast_cleared;

   if (!buffers)
      return;

   trace_start_clear_restore(&batch->trace, ring, buffers);

   if (buffers & PIPE_CLEAR_COLOR) {
      for (int i = 0; i < pfb->nr_cbufs; i++) {
         union pipe_color_union color = batch->clear_color[i];

         if (!pfb->cbufs[i])
            continue;

         if (!(buffers & (PIPE_CLEAR_COLOR0 << i)))
            continue;

         fd6_clear_surface(ctx, ring, pfb->cbufs[i], pfb->width, pfb->height,
                           &color);
      }
   }
   if (buffers & (PIPE_CLEAR_DEPTH | PIPE_CLEAR_STENCIL)) {
      union pipe_color_union value = {};

      const bool has_depth = pfb->zsbuf;
      struct pipe_resource *separate_stencil =
         has_depth && fd_resource(pfb->zsbuf->texture)->stencil
            ? &fd_resource(pfb->zsbuf->texture)->stencil->b.b
            : NULL;

      if ((has_depth && (buffers & PIPE_CLEAR_DEPTH)) ||
          (!separate_stencil && (buffers & PIPE_CLEAR_STENCIL))) {
         value.f[0] = batch->clear_depth;
         value.ui[1] = batch->clear_stencil;
         fd6_clear_surface(ctx, ring, pfb->zsbuf, pfb->width, pfb->height,
                           &value);
      }

      if (separate_stencil && (buffers & PIPE_CLEAR_STENCIL)) {
         value.ui[0] = batch->clear_stencil;

         struct pipe_surface stencil_surf = *pfb->zsbuf;
         stencil_surf.format = PIPE_FORMAT_S8_UINT;
         stencil_surf.texture = separate_stencil;

         fd6_clear_surface(ctx, ring, &stencil_surf, pfb->width, pfb->height,
                           &value);
      }
   }

   fd6_event_write(batch, ring, PC_CCU_FLUSH_COLOR_TS, true);
   fd_wfi(batch, ring);

   trace_end_clear_restore(&batch->trace, ring);
}

static void
setup_tess_buffers(struct fd_batch *batch, struct fd_ringbuffer *ring)
{
   struct fd_context *ctx = batch->ctx;

   batch->tessfactor_bo = fd_bo_new(ctx->screen->dev, batch->tessfactor_size,
                                    0, "tessfactor");

   batch->tessparam_bo = fd_bo_new(ctx->screen->dev, batch->tessparam_size,
                                   0, "tessparam");

   OUT_PKT4(ring, REG_A6XX_PC_TESSFACTOR_ADDR, 2);
   OUT_RELOC(ring, batch->tessfactor_bo, 0, 0, 0);

   batch->tess_addrs_constobj->cur = batch->tess_addrs_constobj->start;
   OUT_RELOC(batch->tess_addrs_constobj, batch->tessparam_bo, 0, 0, 0);
   OUT_RELOC(batch->tess_addrs_constobj, batch->tessfactor_bo, 0, 0, 0);
}

static void
fd6_emit_sysmem_prep(struct fd_batch *batch) assert_dt
{
   struct fd_ringbuffer *ring = batch->gmem;
   struct fd_screen *screen = batch->ctx->screen;

   fd6_emit_restore(batch, ring);
   fd6_emit_lrz_flush(ring);

   if (batch->prologue) {
      if (!batch->nondraw) {
         trace_start_prologue(&batch->trace, ring);
      }
      fd6_emit_ib(ring, batch->prologue);
      if (!batch->nondraw) {
         trace_end_prologue(&batch->trace, ring);
      }
   }

   /* remaining setup below here does not apply to blit/compute: */
   if (batch->nondraw)
      return;

   struct pipe_framebuffer_state *pfb = &batch->framebuffer;

   if (pfb->width > 0 && pfb->height > 0)
      set_scissor(ring, 0, 0, pfb->width - 1, pfb->height - 1);
   else
      set_scissor(ring, 0, 0, 0, 0);

   set_window_offset(ring, 0, 0);

   set_bin_size(ring, 0, 0, 0xc00000); /* 0xc00000 = BYPASS? */

   emit_sysmem_clears(batch, ring);

   emit_marker6(ring, 7);
   OUT_PKT7(ring, CP_SET_MARKER, 1);
   OUT_RING(ring, A6XX_CP_SET_MARKER_0_MODE(RM6_BYPASS));
   emit_marker6(ring, 7);

   if (batch->tessellation)
      setup_tess_buffers(batch, ring);

   OUT_PKT7(ring, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   OUT_RING(ring, 0x0);

   /* blob controls "local" in IB2, but I think that is not required */
   OUT_PKT7(ring, CP_SKIP_IB2_ENABLE_LOCAL, 1);
   OUT_RING(ring, 0x1);

   fd6_event_write(batch, ring, PC_CCU_INVALIDATE_COLOR, false);
   fd6_cache_inv(batch, ring);

   fd_wfi(batch, ring);
   OUT_REG(ring, A6XX_RB_CCU_CNTL(.color_offset = screen->ccu_offset_bypass));

   /* enable stream-out, with sysmem there is only one pass: */
   OUT_REG(ring, A6XX_VPC_SO_DISABLE(false));

   OUT_PKT7(ring, CP_SET_VISIBILITY_OVERRIDE, 1);
   OUT_RING(ring, 0x1);

   emit_zs(ring, pfb->zsbuf, NULL);
   emit_mrt(ring, pfb, NULL);
   emit_msaa(ring, pfb->samples);
   patch_fb_read_sysmem(batch);

   update_render_cntl(batch, pfb, false);

   emit_common_init(batch);
}

static void
fd6_emit_sysmem_fini(struct fd_batch *batch) assert_dt
{
   struct fd_ringbuffer *ring = batch->gmem;

   emit_common_fini(batch);

   if (batch->epilogue)
      fd6_emit_ib(batch->gmem, batch->epilogue);

   OUT_PKT7(ring, CP_SKIP_IB2_ENABLE_GLOBAL, 1);
   OUT_RING(ring, 0x0);

   fd6_emit_lrz_flush(ring);

   fd6_event_write(batch, ring, PC_CCU_FLUSH_COLOR_TS, true);
   fd6_event_write(batch, ring, PC_CCU_FLUSH_DEPTH_TS, true);
   fd_wfi(batch, ring);
}

void
fd6_gmem_init(struct pipe_context *pctx) disable_thread_safety_analysis
{
   struct fd_context *ctx = fd_context(pctx);

   ctx->emit_tile_init = fd6_emit_tile_init;
   ctx->emit_tile_prep = fd6_emit_tile_prep;
   ctx->emit_tile_mem2gmem = fd6_emit_tile_mem2gmem;
   ctx->emit_tile_renderprep = fd6_emit_tile_renderprep;
   ctx->emit_tile = fd6_emit_tile;
   ctx->emit_tile_gmem2mem = fd6_emit_tile_gmem2mem;
   ctx->emit_tile_fini = fd6_emit_tile_fini;
   ctx->emit_sysmem_prep = fd6_emit_sysmem_prep;
   ctx->emit_sysmem_fini = fd6_emit_sysmem_fini;
}
