/*
 * Copyright © 2013 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD3_EMIT_H
#define FD3_EMIT_H

#include "pipe/p_context.h"

#include "fd3_format.h"
#include "fd3_program.h"
#include "freedreno_batch.h"
#include "freedreno_context.h"
#include "ir3_cache.h"
#include "ir3_gallium.h"

struct fd_ringbuffer;

void fd3_emit_gmem_restore_tex(struct fd_ringbuffer *ring,
                               struct pipe_surface **psurf, int bufs);

/* grouped together emit-state for prog/vertex/state emit: */
struct fd3_emit {
   struct util_debug_callback *debug;
   const struct fd_vertex_state *vtx;
   const struct fd3_program_state *prog;
   const struct pipe_draw_info *info;
	unsigned drawid_offset;
   const struct pipe_draw_indirect_info *indirect;
	const struct pipe_draw_start_count_bias *draw;
   bool binning_pass;
   struct ir3_cache_key key;
   enum fd_dirty_3d_state dirty;

   uint32_t sprite_coord_enable;
   bool sprite_coord_mode;
   bool rasterflat;
   bool skip_consts;

   /* cached to avoid repeated lookups of same variants: */
   const struct ir3_shader_variant *vs, *fs;
};

static inline const struct ir3_shader_variant *
fd3_emit_get_vp(struct fd3_emit *emit)
{
   if (!emit->vs) {
      emit->vs = emit->binning_pass ? emit->prog->bs : emit->prog->vs;
   }
   return emit->vs;
}

static inline const struct ir3_shader_variant *
fd3_emit_get_fp(struct fd3_emit *emit)
{
   if (!emit->fs) {
      if (emit->binning_pass) {
         /* use dummy stateobj to simplify binning vs non-binning: */
         static const struct ir3_shader_variant binning_fs = {};
         emit->fs = &binning_fs;
      } else {
         emit->fs = emit->prog->fs;
      }
   }
   return emit->fs;
}

void fd3_emit_vertex_bufs(struct fd_ringbuffer *ring,
                          struct fd3_emit *emit) assert_dt;

void fd3_emit_state(struct fd_context *ctx, struct fd_ringbuffer *ring,
                    struct fd3_emit *emit) assert_dt;

void fd3_emit_restore(struct fd_batch *batch,
                      struct fd_ringbuffer *ring) assert_dt;

void fd3_emit_init_screen(struct pipe_screen *pscreen);
void fd3_emit_init(struct pipe_context *pctx);

static inline void
fd3_emit_ib(struct fd_ringbuffer *ring, struct fd_ringbuffer *target)
{
   __OUT_IB(ring, true, target);
}

static inline void
fd3_emit_cache_flush(struct fd_batch *batch,
                     struct fd_ringbuffer *ring) assert_dt
{
   fd_wfi(batch, ring);
   OUT_PKT0(ring, REG_A3XX_UCHE_CACHE_INVALIDATE0_REG, 2);
   OUT_RING(ring, A3XX_UCHE_CACHE_INVALIDATE0_REG_ADDR(0));
   OUT_RING(ring, A3XX_UCHE_CACHE_INVALIDATE1_REG_ADDR(0) |
                     A3XX_UCHE_CACHE_INVALIDATE1_REG_OPCODE(INVALIDATE) |
                     A3XX_UCHE_CACHE_INVALIDATE1_REG_ENTIRE_CACHE);
}

#endif /* FD3_EMIT_H */
