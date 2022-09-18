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

#ifndef FD6_TEXTURE_H_
#define FD6_TEXTURE_H_

#include "pipe/p_context.h"

#include "freedreno_resource.h"
#include "freedreno_texture.h"

#include "fd6_context.h"
#include "fdl/fd6_format_table.h"

struct fd6_sampler_stateobj {
   struct pipe_sampler_state base;
   uint32_t texsamp0, texsamp1, texsamp2, texsamp3;
   bool needs_border;
   uint16_t seqno;
};

static inline struct fd6_sampler_stateobj *
fd6_sampler_stateobj(struct pipe_sampler_state *samp)
{
   return (struct fd6_sampler_stateobj *)samp;
}

struct fd6_pipe_sampler_view {
   struct pipe_sampler_view base;
   struct fd_resource *ptr1, *ptr2;
   uint16_t seqno;

   /* TEX_CONST descriptor, with just offsets from the BOs in the iova dwords. */
   uint32_t descriptor[FDL6_TEX_CONST_DWORDS];

   /* For detecting when a resource has transitioned from UBWC compressed
    * to uncompressed, which means the sampler state needs to be updated
    */
   uint16_t rsc_seqno;

   bool needs_validate;
};

static inline struct fd6_pipe_sampler_view *
fd6_pipe_sampler_view(struct pipe_sampler_view *pview)
{
   return (struct fd6_pipe_sampler_view *)pview;
}

void fd6_sampler_view_update(struct fd_context *ctx,
                             struct fd6_pipe_sampler_view *so) assert_dt;

void fd6_texture_init(struct pipe_context *pctx);
void fd6_texture_fini(struct pipe_context *pctx);

static inline unsigned
fd6_border_color_offset(struct fd_context *ctx, enum pipe_shader_type type,
                        struct fd_texture_stateobj *tex) assert_dt
{
   /* Currently we put the FS border-color state after VS.  Possibly
    * we could swap the order.
    *
    * This will need update for HS/DS/GS
    */
   if (type != PIPE_SHADER_FRAGMENT)
      return 0;

   unsigned needs_border = false;

   for (unsigned i = 0; i < tex->num_samplers; i++) {
      if (!tex->samplers[i])
         continue;

      struct fd6_sampler_stateobj *sampler =
         fd6_sampler_stateobj(tex->samplers[i]);

      needs_border |= sampler->needs_border;
   }

   if (!needs_border)
      return 0;

   return ctx->tex[PIPE_SHADER_VERTEX].num_samplers;
}

/*
 * Texture stateobj:
 *
 * The sampler and sampler-view state is mapped to a single hardware
 * stateobj which can be emit'd as a pointer in a CP_SET_DRAW_STATE
 * packet, to avoid the overhead of re-generating the entire cmdstream
 * when application toggles thru multiple different texture states.
 */

struct fd6_texture_key {
   struct {
      /* We need to track the seqno of the rsc as well as of the
       * sampler view, because resource shadowing/etc can result
       * that the underlying bo changes (which means the previous
       * state was no longer valid.
       */
      uint16_t rsc_seqno;
      uint16_t seqno;
   } view[16];
   struct {
      uint16_t seqno;
   } samp[16];
   uint8_t type;
   uint8_t bcolor_offset;
};

struct fd6_texture_state {
   struct pipe_reference reference;
   struct fd6_texture_key key;
   struct fd_ringbuffer *stateobj;
   bool needs_border;
};

struct fd6_texture_state *
fd6_texture_state(struct fd_context *ctx, enum pipe_shader_type type,
                  struct fd_texture_stateobj *tex) assert_dt;

/* not called directly: */
void __fd6_texture_state_describe(char *buf,
                                  const struct fd6_texture_state *tex);
void __fd6_texture_state_destroy(struct fd6_texture_state *tex);

static inline void
fd6_texture_state_reference(struct fd6_texture_state **ptr,
                            struct fd6_texture_state *tex)
{
   struct fd6_texture_state *old_tex = *ptr;

   if (pipe_reference_described(
          &(*ptr)->reference, &tex->reference,
          (debug_reference_descriptor)__fd6_texture_state_describe))
      __fd6_texture_state_destroy(old_tex);

   *ptr = tex;
}

#endif /* FD6_TEXTURE_H_ */
