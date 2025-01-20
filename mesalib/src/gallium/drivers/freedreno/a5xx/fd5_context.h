/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD5_CONTEXT_H_
#define FD5_CONTEXT_H_

#include "util/u_upload_mgr.h"

#include "freedreno_context.h"

#include "ir3/ir3_shader.h"

struct fd5_context {
   struct fd_context base;

   /* This only needs to be 4 * num_of_pipes bytes (ie. 32 bytes).  We
    * could combine it with another allocation.
    */
   struct fd_bo *vsc_size_mem;

   /* TODO not sure what this is for.. probably similar to
    * CACHE_FLUSH_TS on kernel side, where value gets written
    * to this address synchronized w/ 3d (ie. a way to
    * synchronize when the CP is running far ahead)
    */
   struct fd_bo *blit_mem;

   struct u_upload_mgr *border_color_uploader;
   struct pipe_resource *border_color_buf;

   /* storage for ctx->last.key: */
   struct ir3_shader_key last_key;

   /* cached state about current emitted shader program (3d): */
   unsigned max_loc;
};

static inline struct fd5_context *
fd5_context(struct fd_context *ctx)
{
   return (struct fd5_context *)ctx;
}

struct pipe_context *fd5_context_create(struct pipe_screen *pscreen, void *priv,
                                        unsigned flags);

/* helper for places where we need to stall CP to wait for previous draws: */
static inline void
fd5_emit_flush(struct fd_context *ctx, struct fd_ringbuffer *ring)
{
   OUT_PKT7(ring, CP_EVENT_WRITE, 4);
   OUT_RING(ring, CACHE_FLUSH_TS);
   OUT_RELOC(ring, fd5_context(ctx)->blit_mem, 0, 0, 0); /* ADDR_LO/HI */
   OUT_RING(ring, 0x00000000);

   OUT_WFI5(ring);
}

#endif /* FD5_CONTEXT_H_ */
