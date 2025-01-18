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
 *    Christian Gmeiner <christian.gmeiner@gmail.com>
 */

#ifndef ETNAVIV_STATE_H_
#define ETNAVIV_STATE_H_

#include "etnaviv_context.h"
#include "etnaviv_screen.h"
#include "pipe/p_context.h"

static inline bool
etna_depth_enabled(struct etna_context *ctx)
{
   return ctx->zsa && ctx->zsa->depth_enabled;
}

static inline bool
etna_stencil_enabled(struct etna_context *ctx)
{
   return ctx->zsa && ctx->zsa->stencil[0].enabled;
}

static inline bool
etna_use_ts_for_mrt(const struct etna_screen *screen, const struct pipe_framebuffer_state *fb)
{
   if (screen->info->halti >= 2)
      return true;

   unsigned count = 0;

   for (unsigned i = 0; i < fb->nr_cbufs; i++) {
      if (!fb->cbufs[i])
         continue;

      count++;
   }

   return count <= 1;
}

bool
etna_state_update(struct etna_context *ctx);

void
etna_state_init(struct pipe_context *pctx);

#endif /* ETNAVIV_STATE_H_ */
