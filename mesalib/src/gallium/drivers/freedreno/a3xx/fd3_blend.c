/*
 * Copyright © 2013 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "pipe/p_state.h"
#include "util/u_blend.h"
#include "util/u_dual_blend.h"
#include "util/u_memory.h"
#include "util/u_string.h"

#include "fd3_blend.h"
#include "fd3_context.h"
#include "fd3_format.h"

static enum a3xx_rb_blend_opcode
blend_func(unsigned func)
{
   switch (func) {
   case PIPE_BLEND_ADD:
      return BLEND_DST_PLUS_SRC;
   case PIPE_BLEND_MIN:
      return BLEND_MIN_DST_SRC;
   case PIPE_BLEND_MAX:
      return BLEND_MAX_DST_SRC;
   case PIPE_BLEND_SUBTRACT:
      return BLEND_SRC_MINUS_DST;
   case PIPE_BLEND_REVERSE_SUBTRACT:
      return BLEND_DST_MINUS_SRC;
   default:
      DBG("invalid blend func: %x", func);
      return 0;
   }
}

void *
fd3_blend_state_create(struct pipe_context *pctx,
                       const struct pipe_blend_state *cso)
{
   struct fd3_blend_stateobj *so;
   enum a3xx_rop_code rop = ROP_COPY;
   bool reads_dest = false;
   int i;

   if (cso->logicop_enable) {
      rop = cso->logicop_func; /* maps 1:1 */
      reads_dest = util_logicop_reads_dest(cso->logicop_func);
   }

   so = CALLOC_STRUCT(fd3_blend_stateobj);
   if (!so)
      return NULL;

   so->base = *cso;

   for (i = 0; i < ARRAY_SIZE(so->rb_mrt); i++) {
      const struct pipe_rt_blend_state *rt;
      if (cso->independent_blend_enable)
         rt = &cso->rt[i];
      else
         rt = &cso->rt[0];

      so->rb_mrt[i].blend_control =
         A3XX_RB_MRT_BLEND_CONTROL_RGB_SRC_FACTOR(
            fd_blend_factor(rt->rgb_src_factor)) |
         A3XX_RB_MRT_BLEND_CONTROL_RGB_BLEND_OPCODE(blend_func(rt->rgb_func)) |
         A3XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(
            fd_blend_factor(rt->rgb_dst_factor)) |
         A3XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(
            fd_blend_factor(rt->alpha_src_factor)) |
         A3XX_RB_MRT_BLEND_CONTROL_ALPHA_BLEND_OPCODE(
            blend_func(rt->alpha_func)) |
         A3XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(
            fd_blend_factor(rt->alpha_dst_factor));

      so->rb_mrt[i].control =
         A3XX_RB_MRT_CONTROL_ROP_CODE(rop) |
         A3XX_RB_MRT_CONTROL_COMPONENT_ENABLE(rt->colormask);

      if (rt->blend_enable)
         so->rb_mrt[i].control |= A3XX_RB_MRT_CONTROL_READ_DEST_ENABLE |
                                  A3XX_RB_MRT_CONTROL_BLEND |
                                  A3XX_RB_MRT_CONTROL_BLEND2;

      if (reads_dest)
         so->rb_mrt[i].control |= A3XX_RB_MRT_CONTROL_READ_DEST_ENABLE;

      if (cso->dither)
         so->rb_mrt[i].control |=
            A3XX_RB_MRT_CONTROL_DITHER_MODE(DITHER_ALWAYS);
   }

   if (cso->rt[0].blend_enable && util_blend_state_is_dual(cso, 0))
      so->rb_render_control = A3XX_RB_RENDER_CONTROL_DUAL_COLOR_IN_ENABLE;

   return so;
}
