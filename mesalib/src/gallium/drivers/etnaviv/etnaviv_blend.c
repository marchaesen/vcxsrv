/*
 * Copyright (c) 2012-2015 Etnaviv Project
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *    Wladimir J. van der Laan <laanwj@gmail.com>
 */

#include "etnaviv_blend.h"

#include "etnaviv_context.h"
#include "etnaviv_screen.h"
#include "etnaviv_translate.h"
#include "pipe/p_defines.h"
#include "util/u_memory.h"
#include "util/half_float.h"

void *
etna_blend_state_create(struct pipe_context *pctx,
                        const struct pipe_blend_state *so)
{
   struct etna_context *ctx = etna_context(pctx);
   struct etna_blend_state *co = CALLOC_STRUCT(etna_blend_state);
   bool logicop_enable;

   /* pipe_blend_func happens to match the hardware. */
   STATIC_ASSERT(PIPE_BLEND_ADD == BLEND_EQ_ADD);
   STATIC_ASSERT(PIPE_BLEND_SUBTRACT == BLEND_EQ_SUBTRACT);
   STATIC_ASSERT(PIPE_BLEND_REVERSE_SUBTRACT == BLEND_EQ_REVERSE_SUBTRACT);
   STATIC_ASSERT(PIPE_BLEND_MIN == BLEND_EQ_MIN);
   STATIC_ASSERT(PIPE_BLEND_MAX == BLEND_EQ_MAX);

   if (!co)
      return NULL;

   co->base = *so;

   logicop_enable = so->logicop_enable &&
                    VIV_FEATURE(ctx->screen, ETNA_FEATURE_LOGIC_OP);

   for (unsigned int i = 0; i < so->max_rt + 1; i++) {
      const struct pipe_rt_blend_state *rt;

      if (so->independent_blend_enable)
         rt = &so->rt[i];
      else
         rt = &so->rt[0];

      /* Enable blending if
      * - blend enabled in blend state
      * - NOT source factor is ONE and destination factor ZERO and eq is ADD for
      *   both rgb and alpha (which mean that blending is effectively disabled)
      */
      co->rt[i].alpha_enable = rt->blend_enable &&
                    !(rt->rgb_src_factor == PIPE_BLENDFACTOR_ONE &&
                     rt->rgb_dst_factor == PIPE_BLENDFACTOR_ZERO &&
                     rt->rgb_func == PIPE_BLEND_ADD &&
                     rt->alpha_src_factor == PIPE_BLENDFACTOR_ONE &&
                     rt->alpha_dst_factor == PIPE_BLENDFACTOR_ZERO &&
                     rt->alpha_func == PIPE_BLEND_ADD);

      /* Enable separate alpha if
      * - Blending enabled (see above)
      * - NOT source/destination factor and eq is same for both rgb and alpha
      *   (which would effectively that mean alpha is not separate), and
      */
      co->rt[i].separate_alpha = co->rt[i].alpha_enable &&
                           !(rt->rgb_src_factor == rt->alpha_src_factor &&
                             rt->rgb_dst_factor == rt->alpha_dst_factor &&
                             rt->rgb_func == rt->alpha_func);

      co->rt[i].fo_allowed = !co->rt[i].alpha_enable && !logicop_enable;
   }

   co->PE_LOGIC_OP =
         VIVS_PE_LOGIC_OP_OP(logicop_enable ? so->logicop_func : LOGIC_OP_COPY) |
         VIVS_PE_LOGIC_OP_DITHER_MODE(3) | /* TODO: related to dithering, sometimes 2 */
         0x000E4000 /* ??? */;

   /* XXX alpha_to_coverage / alpha_to_one? */
   /* Set dither registers based on dither status. These registers set the
    * dither pattern,
    * for now, set the same values as the blob.
    */
   if (so->dither &&
       (!co->rt[0].alpha_enable ||
        VIV_FEATURE(ctx->screen, ETNA_FEATURE_PE_DITHER_FIX))) {
      co->PE_DITHER[0] = 0x6e4ca280;
      co->PE_DITHER[1] = 0x5d7f91b3;
   } else {
      co->PE_DITHER[0] = 0xffffffff;
      co->PE_DITHER[1] = 0xffffffff;
   }

   return co;
}

bool
etna_update_blend(struct etna_context *ctx)
{
   struct pipe_framebuffer_state *pfb = &ctx->framebuffer_s;
   struct pipe_blend_state *pblend = ctx->blend;
   struct etna_blend_state *blend = etna_blend_state(pblend);
   unsigned current_rt = 0;

   for (unsigned i = 0; i < pfb->nr_cbufs; i++) {
      if (!pfb->cbufs[i])
         continue;

      const struct pipe_rt_blend_state *rt;
      uint32_t colormask;

      if (pblend->independent_blend_enable)
         rt = &pblend->rt[i];
      else
         rt = &pblend->rt[0];

      if (translate_pe_format_rb_swap(pfb->cbufs[i]->format)) {
         colormask = rt->colormask & (PIPE_MASK_A | PIPE_MASK_G);
         if (rt->colormask & PIPE_MASK_R)
            colormask |= PIPE_MASK_B;
         if (rt->colormask & PIPE_MASK_B)
            colormask |= PIPE_MASK_R;
      } else {
         colormask = rt->colormask;
      }

      /* If the complete render target is written, set full_overwrite:
      * - The color mask covers all channels of the render target
      * - No blending or logicop is used
      */
      const struct util_format_description *desc = util_format_description(pfb->cbufs[i]->format);
      bool full_overwrite = (blend->rt[i].fo_allowed &&
                            util_format_colormask_full(desc, colormask));

      if (current_rt == 0) {
         blend->rt[0].PE_COLOR_FORMAT =
                  VIVS_PE_COLOR_FORMAT_COMPONENTS(colormask) |
                  COND(full_overwrite, VIVS_PE_COLOR_FORMAT_OVERWRITE);
      } else {
         blend->rt[current_rt].PE_HALTI5_COLORMASK =
                  VIVS_PE_HALTI5_RT_COLORMASK_COMPONENTS(colormask) |
                  COND(full_overwrite, VIVS_PE_HALTI5_RT_COLORMASK_OVERWRITE);
      }

      if (blend->rt[i].alpha_enable) {
         blend->rt[current_rt].PE_ALPHA_CONFIG =
            VIVS_PE_ALPHA_CONFIG_BLEND_ENABLE_COLOR |
            COND(blend->rt[i].separate_alpha, VIVS_PE_ALPHA_CONFIG_BLEND_SEPARATE_ALPHA) |
            VIVS_PE_ALPHA_CONFIG_SRC_FUNC_COLOR(translate_blend_factor(rt->rgb_src_factor)) |
            VIVS_PE_ALPHA_CONFIG_SRC_FUNC_ALPHA(translate_blend_factor(rt->alpha_src_factor)) |
            VIVS_PE_ALPHA_CONFIG_DST_FUNC_COLOR(translate_blend_factor(rt->rgb_dst_factor)) |
            VIVS_PE_ALPHA_CONFIG_DST_FUNC_ALPHA(translate_blend_factor(rt->alpha_dst_factor)) |
            VIVS_PE_ALPHA_CONFIG_EQ_COLOR(rt->rgb_func) |
            VIVS_PE_ALPHA_CONFIG_EQ_ALPHA(rt->alpha_func);
      } else {
         blend->rt[current_rt].PE_ALPHA_CONFIG = 0;
      }

      current_rt++;
   }

   if (current_rt == 0)
      blend->rt[0].PE_COLOR_FORMAT = VIVS_PE_COLOR_FORMAT_OVERWRITE;

   return true;
}

void
etna_set_blend_color(struct pipe_context *pctx, const struct pipe_blend_color *bc)
{
   struct etna_context *ctx = etna_context(pctx);
   struct compiled_blend_color *cs = &ctx->blend_color;

   memcpy(cs->color, bc->color, sizeof(float) * 4);

   ctx->dirty |= ETNA_DIRTY_BLEND_COLOR;
}

bool
etna_update_blend_color(struct etna_context *ctx)
{
   struct pipe_framebuffer_state *fb = &ctx->framebuffer_s;
   struct compiled_blend_color *cs = &ctx->blend_color;
   unsigned rt = 0;

   for (unsigned i = 0; i < fb->nr_cbufs; i++) {
      if (!fb->cbufs[i])
         continue;

      bool rb_swap = translate_pe_format_rb_swap(fb->cbufs[i]->format);

      if (rt == 0) {
         cs->PE_ALPHA_BLEND_COLOR =
            VIVS_PE_ALPHA_BLEND_COLOR_R(float_to_ubyte(cs->color[rb_swap ? 2 : 0])) |
            VIVS_PE_ALPHA_BLEND_COLOR_G(float_to_ubyte(cs->color[1])) |
            VIVS_PE_ALPHA_BLEND_COLOR_B(float_to_ubyte(cs->color[rb_swap ? 0 : 2])) |
            VIVS_PE_ALPHA_BLEND_COLOR_A(float_to_ubyte(cs->color[3]));
      }

      cs->rt[rt].PE_ALPHA_COLOR_EXT0 =
         VIVS_PE_ALPHA_COLOR_EXT0_B(_mesa_float_to_half(cs->color[rb_swap ? 2 : 0])) |
         VIVS_PE_ALPHA_COLOR_EXT0_G(_mesa_float_to_half(cs->color[1]));
      cs->rt[rt].PE_ALPHA_COLOR_EXT1 =
         VIVS_PE_ALPHA_COLOR_EXT1_R(_mesa_float_to_half(cs->color[rb_swap ? 0 : 2])) |
         VIVS_PE_ALPHA_COLOR_EXT1_A(_mesa_float_to_half(cs->color[3]));

      rt++;
   }

   return true;
}
