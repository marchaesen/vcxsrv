/*
 * Copyright 2018 Collabora Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * on the rights to use, copy, modify, merge, publish, distribute, sub
 * license, and/or sell copies of the Software, and to permit persons to whom
 * the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHOR(S) AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
 * DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
 * USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#ifndef ZINK_FRAMEBUFFER_H
#define ZINK_FRAMEBUFFER_H

#include "pipe/p_state.h"
#include <vulkan/vulkan.h>

#include "util/u_inlines.h"

struct zink_context;
struct zink_screen;
struct zink_render_pass;

struct zink_framebuffer_state {
   struct zink_render_pass *rp;
   uint32_t width;
   uint16_t height, layers;
   uint8_t samples;
   uint8_t num_attachments;
   struct zink_surface *attachments[PIPE_MAX_COLOR_BUFS + 1];
   bool has_null_attachments;
};

struct zink_framebuffer {
   struct pipe_reference reference;
   VkFramebuffer fb;

   struct pipe_surface *surfaces[PIPE_MAX_COLOR_BUFS + 1];
   struct zink_render_pass *rp;
   struct pipe_surface *null_surface; /* for use with unbound attachments */
};

struct zink_framebuffer *
zink_create_framebuffer(struct zink_context *ctx, struct zink_screen *screen,
                        struct zink_framebuffer_state *fb);

void
zink_destroy_framebuffer(struct zink_screen *screen,
                         struct zink_framebuffer *fbuf);

void
debug_describe_zink_framebuffer(char* buf, const struct zink_framebuffer *ptr);

static inline void
zink_framebuffer_reference(struct zink_screen *screen,
                           struct zink_framebuffer **dst,
                           struct zink_framebuffer *src)
{
   struct zink_framebuffer *old_dst = *dst;

   if (pipe_reference_described(&old_dst->reference, &src->reference,
                                (debug_reference_descriptor)debug_describe_zink_framebuffer))
      zink_destroy_framebuffer(screen, old_dst);
   *dst = src;
}

#endif
