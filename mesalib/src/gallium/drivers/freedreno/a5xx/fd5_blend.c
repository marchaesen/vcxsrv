/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "pipe/p_state.h"
#include "util/u_blend.h"
#include "util/u_memory.h"
#include "util/u_string.h"

#include "fd5_blend.h"
#include "fd5_context.h"
#include "fd5_format.h"

// XXX move somewhere common.. same across a3xx/a4xx/a5xx..
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
fd5_blend_state_create(struct pipe_context *pctx,
                       const struct pipe_blend_state *cso)
{
   struct fd5_blend_stateobj *so;
   enum a3xx_rop_code rop = ROP_COPY;
   bool reads_dest = false;
   unsigned i, mrt_blend = 0;

   if (cso->logicop_enable) {
      rop = cso->logicop_func; /* maps 1:1 */
      reads_dest = util_logicop_reads_dest(cso->logicop_func);
   }

   so = CALLOC_STRUCT(fd5_blend_stateobj);
   if (!so)
      return NULL;

   so->base = *cso;

   so->lrz_write = true; /* unless blend enabled for any MRT */

   for (i = 0; i < ARRAY_SIZE(so->rb_mrt); i++) {
      const struct pipe_rt_blend_state *rt;

      if (cso->independent_blend_enable)
         rt = &cso->rt[i];
      else
         rt = &cso->rt[0];

      so->rb_mrt[i].blend_control =
         A5XX_RB_MRT_BLEND_CONTROL_RGB_SRC_FACTOR(
            fd_blend_factor(rt->rgb_src_factor)) |
         A5XX_RB_MRT_BLEND_CONTROL_RGB_BLEND_OPCODE(blend_func(rt->rgb_func)) |
         A5XX_RB_MRT_BLEND_CONTROL_RGB_DEST_FACTOR(
            fd_blend_factor(rt->rgb_dst_factor)) |
         A5XX_RB_MRT_BLEND_CONTROL_ALPHA_SRC_FACTOR(
            fd_blend_factor(rt->alpha_src_factor)) |
         A5XX_RB_MRT_BLEND_CONTROL_ALPHA_BLEND_OPCODE(
            blend_func(rt->alpha_func)) |
         A5XX_RB_MRT_BLEND_CONTROL_ALPHA_DEST_FACTOR(
            fd_blend_factor(rt->alpha_dst_factor));

      so->rb_mrt[i].control =
         A5XX_RB_MRT_CONTROL_ROP_CODE(rop) |
         COND(cso->logicop_enable, A5XX_RB_MRT_CONTROL_ROP_ENABLE) |
         A5XX_RB_MRT_CONTROL_COMPONENT_ENABLE(rt->colormask);

      if (rt->blend_enable) {
         so->rb_mrt[i].control |=
//               A5XX_RB_MRT_CONTROL_READ_DEST_ENABLE |
               A5XX_RB_MRT_CONTROL_BLEND | A5XX_RB_MRT_CONTROL_BLEND2;
         mrt_blend |= (1 << i);
         so->lrz_write = false;
      }

      if (reads_dest) {
//         so->rb_mrt[i].control |=
//               A5XX_RB_MRT_CONTROL_READ_DEST_ENABLE;
         mrt_blend |= (1 << i);
      }

//      if (cso->dither)
//         so->rb_mrt[i].buf_info |=
//               A5XX_RB_MRT_BUF_INFO_DITHER_MODE(DITHER_ALWAYS);
   }

   so->rb_blend_cntl =
      A5XX_RB_BLEND_CNTL_ENABLE_BLEND(mrt_blend) |
      COND(cso->alpha_to_coverage, A5XX_RB_BLEND_CNTL_ALPHA_TO_COVERAGE) |
      COND(cso->independent_blend_enable, A5XX_RB_BLEND_CNTL_INDEPENDENT_BLEND);
   so->sp_blend_cntl =
      A5XX_SP_BLEND_CNTL_ENABLE_BLEND(mrt_blend) |
      A5XX_SP_BLEND_CNTL_UNK8 |
      COND(cso->alpha_to_coverage, A5XX_SP_BLEND_CNTL_ALPHA_TO_COVERAGE);

   return so;
}
