/*
 * Copyright © 2023 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#define FD_BO_NO_HARDPIN 1

#include "freedreno_batch.h"

#include "fd6_barrier.h"
#include "fd6_emit.h"

template <chip CHIP>
void
fd6_emit_flushes(struct fd_context *ctx, struct fd_ringbuffer *ring,
                 unsigned flushes)
{
   /* Experiments show that invalidating CCU while it still has data in it
    * doesn't work, so make sure to always flush before invalidating in case
    * any data remains that hasn't yet been made available through a barrier.
    * However it does seem to work for UCHE.
    */
   if (flushes & (FD6_FLUSH_CCU_COLOR | FD6_INVALIDATE_CCU_COLOR))
      fd6_event_write<CHIP>(ctx, ring, FD_CCU_CLEAN_COLOR);

   if (flushes & (FD6_FLUSH_CCU_DEPTH | FD6_INVALIDATE_CCU_DEPTH))
      fd6_event_write<CHIP>(ctx, ring, FD_CCU_CLEAN_DEPTH);

   if (flushes & FD6_INVALIDATE_CCU_COLOR)
      fd6_event_write<CHIP>(ctx, ring, FD_CCU_INVALIDATE_COLOR);

   if (flushes & FD6_INVALIDATE_CCU_DEPTH)
      fd6_event_write<CHIP>(ctx, ring, FD_CCU_INVALIDATE_DEPTH);

   if (flushes & FD6_FLUSH_CACHE)
      fd6_event_write<CHIP>(ctx, ring, FD_CACHE_CLEAN);

   if (flushes & FD6_INVALIDATE_CACHE)
      fd6_event_write<CHIP>(ctx, ring, FD_CACHE_INVALIDATE);

   if (flushes & FD6_WAIT_MEM_WRITES)
      OUT_PKT7(ring, CP_WAIT_MEM_WRITES, 0);

   if (flushes & FD6_WAIT_FOR_IDLE)
      OUT_PKT7(ring, CP_WAIT_FOR_IDLE, 0);

   if (flushes & FD6_WAIT_FOR_ME)
      OUT_PKT7(ring, CP_WAIT_FOR_ME, 0);
}
FD_GENX(fd6_emit_flushes);

template <chip CHIP>
void
fd6_barrier_flush(struct fd_batch *batch)
{
   fd6_emit_flushes<CHIP>(batch->ctx, batch->draw, batch->barrier);
   batch->barrier = 0;
}
FD_GENX(fd6_barrier_flush);

static void
add_flushes(struct pipe_context *pctx, unsigned flushes)
   assert_dt
{
   struct fd_context *ctx = fd_context(pctx);
   struct fd_batch *batch = NULL;

   /* If there is an active compute/nondraw batch, that is the one
    * we want to add the flushes to.  Ie. last op was a launch_grid,
    * if the next one is a launch_grid then the barriers should come
    * between them.  If the next op is a draw_vbo then the batch
    * switch is a sufficient barrier so it doesn't really matter.
    */
   fd_batch_reference(&batch, ctx->batch_nondraw);
   if (!batch)
      fd_batch_reference(&batch, ctx->batch);

   /* A batch flush is already a sufficient barrier: */
   if (!batch)
      return;

   batch->barrier |= flushes;

   fd_batch_reference(&batch, NULL);
}

static void
fd6_texture_barrier(struct pipe_context *pctx, unsigned flags)
   in_dt
{
   unsigned flushes = 0;

   if (flags & PIPE_TEXTURE_BARRIER_SAMPLER) {
      /* If we are sampling from the fb, we could get away with treating
       * this as a PIPE_TEXTURE_BARRIER_FRAMEBUFFER in sysmem mode, but
       * that won't work out in gmem mode because we don't patch the tex
       * state outside of the case that the frag shader tells us it is
       * an fb-read.  And in particular, the fb-read case guarantees us
       * that the read will be from the same texel, but the fb-bound-as-
       * tex case does not.
       *
       * We could try to be clever here and detect if zsbuf/cbuf[n] is
       * bound as a texture, but that doesn't really help if it is bound
       * as a texture after the barrier without a lot of extra book-
       * keeping.  So hopefully no one calls glTextureBarrierNV() just
       * for lolz.
       */
      pctx->flush(pctx, NULL, 0);
      return;
   }

   if (flags & PIPE_TEXTURE_BARRIER_FRAMEBUFFER) {
      flushes |= FD6_WAIT_FOR_IDLE | FD6_WAIT_FOR_ME |
            FD6_FLUSH_CCU_COLOR | FD6_FLUSH_CCU_DEPTH |
            FD6_FLUSH_CACHE | FD6_INVALIDATE_CACHE;
   }

   add_flushes(pctx, flushes);
}

static void
fd6_memory_barrier(struct pipe_context *pctx, unsigned flags)
   in_dt
{
   unsigned flushes = 0;

   if (flags & (PIPE_BARRIER_SHADER_BUFFER |
                PIPE_BARRIER_CONSTANT_BUFFER |
                PIPE_BARRIER_VERTEX_BUFFER |
                PIPE_BARRIER_INDEX_BUFFER |
                PIPE_BARRIER_STREAMOUT_BUFFER)) {
      flushes |= FD6_WAIT_FOR_IDLE;
   }

   if (flags & (PIPE_BARRIER_TEXTURE |
                PIPE_BARRIER_IMAGE |
                PIPE_BARRIER_UPDATE_BUFFER |
                PIPE_BARRIER_UPDATE_TEXTURE)) {
      flushes |= FD6_FLUSH_CACHE | FD6_WAIT_FOR_IDLE;
   }

   if (flags & PIPE_BARRIER_INDIRECT_BUFFER) {
      flushes |= FD6_FLUSH_CACHE | FD6_WAIT_FOR_IDLE;

     /* Various firmware bugs/inconsistencies mean that some indirect draw opcodes
      * do not wait for WFI's to complete before executing. Add a WAIT_FOR_ME if
      * pending for these opcodes. This may result in a few extra WAIT_FOR_ME's
      * with these opcodes, but the alternative would add unnecessary WAIT_FOR_ME's
      * before draw opcodes that don't need it.
      */
      if (fd_context(pctx)->screen->info->a6xx.indirect_draw_wfm_quirk) {
         flushes |= FD6_WAIT_FOR_ME;
      }
   }

   if (flags & PIPE_BARRIER_FRAMEBUFFER) {
      fd6_texture_barrier(pctx, PIPE_TEXTURE_BARRIER_FRAMEBUFFER);
   }

   add_flushes(pctx, flushes);
}

void
fd6_barrier_init(struct pipe_context *pctx)
{
   pctx->texture_barrier = fd6_texture_barrier;
   pctx->memory_barrier = fd6_memory_barrier;
}
