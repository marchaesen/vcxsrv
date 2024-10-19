/*
 * Copyright © 2014 Rob Clark <robclark@freedesktop.org>
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#ifndef FD4_CONTEXT_H_
#define FD4_CONTEXT_H_

#include "util/u_upload_mgr.h"

#include "freedreno_context.h"

#include "ir3/ir3_shader.h"

struct fd4_context {
   struct fd_context base;

   struct fd_bo *vs_pvt_mem, *fs_pvt_mem;

   /* This only needs to be 4 * num_of_pipes bytes (ie. 32 bytes).  We
    * could combine it with another allocation.
    *
    * (upper area used as scratch bo.. see fd4_query)
    */
   struct fd_bo *vsc_size_mem;

   struct u_upload_mgr *border_color_uploader;
   struct pipe_resource *border_color_buf;

   /* bitmask of samplers which need astc srgb workaround: */
   uint16_t vastc_srgb, fastc_srgb, castc_srgb;

   /* samplers swizzles, needed for tg4 workaround: */
   uint16_t vsampler_swizzles[16], fsampler_swizzles[16], csampler_swizzles[16];

   /* storage for ctx->last.key: */
   struct ir3_shader_key last_key;
};

static inline struct fd4_context *
fd4_context(struct fd_context *ctx)
{
   return (struct fd4_context *)ctx;
}

struct pipe_context *fd4_context_create(struct pipe_screen *pscreen, void *priv,
                                        unsigned flags);

#endif /* FD4_CONTEXT_H_ */
