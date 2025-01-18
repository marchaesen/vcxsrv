/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#define FD_BO_NO_HARDPIN 1

#include "pipe/p_state.h"
#include "util/u_memory.h"
#include "util/u_string.h"

#include "freedreno_state.h"

#include "fd6_context.h"
#include "fd6_pack.h"
#include "fd6_rasterizer.h"

template <chip CHIP>
struct fd_ringbuffer *
__fd6_setup_rasterizer_stateobj(struct fd_context *ctx,
                                const struct pipe_rasterizer_state *cso,
                                bool primitive_restart)
{
   unsigned ndwords = (CHIP >= A7XX) ? 66 : 26;
   struct fd_ringbuffer *ring = fd_ringbuffer_new_object(ctx->pipe, ndwords * 4);
   float psize_min, psize_max;

   if (cso->point_size_per_vertex) {
      psize_min = util_get_min_point_size(cso);
      psize_max = 4092;
   } else {
      /* Force the point size to be as if the vertex output was disabled. */
      psize_min = cso->point_size;
      psize_max = cso->point_size;
   }

   OUT_REG(ring,
           A6XX_GRAS_CL_CNTL(
                 .znear_clip_disable = !cso->depth_clip_near,
                 .zfar_clip_disable = !cso->depth_clip_far,
                 .z_clamp_enable = cso->depth_clamp || CHIP >= A7XX,
                 .zero_gb_scale_z = cso->clip_halfz,
                 .vp_clip_code_ignore = 1,
           ),
   );

   OUT_REG(ring,
           A6XX_GRAS_SU_CNTL(
                 .cull_front = cso->cull_face & PIPE_FACE_FRONT,
                 .cull_back = cso->cull_face & PIPE_FACE_BACK,
                 .front_cw = !cso->front_ccw,
                 .linehalfwidth = cso->line_width / 2.0f,
                 .poly_offset = cso->offset_tri,
                 .line_mode = cso->multisample ? RECTANGULAR : BRESENHAM,
           ),
   );

   OUT_REG(ring,
           A6XX_GRAS_SU_POINT_MINMAX(.min = psize_min, .max = psize_max, ),
           A6XX_GRAS_SU_POINT_SIZE(cso->point_size));

   OUT_REG(ring, A6XX_GRAS_SU_POLY_OFFSET_SCALE(cso->offset_scale),
           A6XX_GRAS_SU_POLY_OFFSET_OFFSET(cso->offset_units),
           A6XX_GRAS_SU_POLY_OFFSET_OFFSET_CLAMP(cso->offset_clamp));

   OUT_REG(ring,
           A6XX_PC_PRIMITIVE_CNTL_0(
                 .primitive_restart = primitive_restart,
                 .provoking_vtx_last = !cso->flatshade_first,
           ),
   );

   if (CHIP >= A7XX) {
      OUT_REG(ring,
              A7XX_VPC_PRIMITIVE_CNTL_0(
                    .primitive_restart = primitive_restart,
                    .provoking_vtx_last = !cso->flatshade_first,
              ),
      );
   }

   enum a6xx_polygon_mode mode = POLYMODE6_TRIANGLES;
   switch (cso->fill_front) {
   case PIPE_POLYGON_MODE_POINT:
      mode = POLYMODE6_POINTS;
      break;
   case PIPE_POLYGON_MODE_LINE:
      mode = POLYMODE6_LINES;
      break;
   default:
      assert(cso->fill_front == PIPE_POLYGON_MODE_FILL);
      break;
   }

   OUT_REG(ring, A6XX_VPC_POLYGON_MODE(mode));
   OUT_REG(ring, PC_POLYGON_MODE(CHIP, mode));

   if (CHIP == A7XX) {
      OUT_REG(ring, A7XX_VPC_POLYGON_MODE2(mode));
   }

   /* With a7xx the hw doesn't do the clamping for us.  When depth clamp
    * is enabled, this gets emitted in fd6_emit_non_ring() due to
    * dependency on viewport state.  But when it is disabled there is
    * no dependency on external state (other than to know the max
    * number of viewports, here we just assume the max) so we can emit
    * this state here:
    */
   if (CHIP >= A7XX && !fd_rast_depth_clamp_enabled(cso)) {
      /* We must assume the max: */
      const unsigned num_viewports = 16;

      OUT_PKT4(ring, REG_A6XX_GRAS_CL_Z_CLAMP(0), num_viewports * 2);
      for (unsigned i = 0; i < num_viewports; i++) {
         OUT_RING(ring, fui(0.0f));
         OUT_RING(ring, fui(1.0f));
      }

      OUT_REG(ring,
         A6XX_RB_Z_CLAMP_MIN(0.0f),
         A6XX_RB_Z_CLAMP_MAX(1.0),
      );
   }

   if (CHIP == A6XX && ctx->screen->info->a6xx.has_legacy_pipeline_shading_rate) {
      OUT_REG(ring, A6XX_RB_UNKNOWN_8A00());
      OUT_REG(ring, A6XX_RB_UNKNOWN_8A10());
      OUT_REG(ring, A6XX_RB_UNKNOWN_8A20());
      OUT_REG(ring, A6XX_RB_UNKNOWN_8A30());
   }

   return ring;
}
FD_GENX(__fd6_setup_rasterizer_stateobj);

void *
fd6_rasterizer_state_create(struct pipe_context *pctx,
                            const struct pipe_rasterizer_state *cso)
{
   struct fd6_rasterizer_stateobj *so;

   so = CALLOC_STRUCT(fd6_rasterizer_stateobj);
   if (!so)
      return NULL;

   so->base = *cso;

   return so;
}

void
fd6_rasterizer_state_delete(struct pipe_context *pctx, void *hwcso)
{
   struct fd6_rasterizer_stateobj *so = (struct fd6_rasterizer_stateobj *)hwcso;

   for (unsigned i = 0; i < ARRAY_SIZE(so->stateobjs); i++)
      if (so->stateobjs[i])
         fd_ringbuffer_del(so->stateobjs[i]);

   FREE(hwcso);
}
