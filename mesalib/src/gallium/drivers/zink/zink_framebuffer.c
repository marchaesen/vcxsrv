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

#include "zink_context.h"
#include "zink_framebuffer.h"

#include "zink_render_pass.h"
#include "zink_screen.h"
#include "zink_surface.h"

#include "util/u_memory.h"
#include "util/u_string.h"

static struct pipe_surface *
framebuffer_null_surface_init(struct zink_context *ctx, struct zink_framebuffer_state *state)
{
   struct pipe_surface surf_templ = {};
   unsigned idx = util_logbase2_ceil(MAX2(state->samples, 1));

   if (!ctx->null_buffers[idx]) {
      struct pipe_resource *pres;
      struct pipe_resource templ = {};
      templ.width0 = state->width;
      templ.height0 = state->height;
      templ.depth0 = 1;
      templ.format = PIPE_FORMAT_R8_UINT;
      templ.target = PIPE_TEXTURE_2D;
      templ.bind = PIPE_BIND_RENDER_TARGET;
      templ.nr_samples = state->samples;

      pres = ctx->base.screen->resource_create(ctx->base.screen, &templ);
      if (!pres)
         return NULL;

      ctx->null_buffers[idx] = pres;
   }
   surf_templ.format = PIPE_FORMAT_R8_UINT;
   surf_templ.nr_samples = state->samples;
   return ctx->base.create_surface(&ctx->base, ctx->null_buffers[idx], &surf_templ);
}

void
zink_destroy_framebuffer(struct zink_screen *screen,
                         struct zink_framebuffer *fbuf)
{
   vkDestroyFramebuffer(screen->dev, fbuf->fb, NULL);
   for (int i = 0; i < ARRAY_SIZE(fbuf->surfaces); ++i)
      pipe_surface_reference(fbuf->surfaces + i, NULL);

   pipe_surface_reference(&fbuf->null_surface, NULL);

   zink_render_pass_reference(screen, &fbuf->rp, NULL);

   FREE(fbuf);
}

struct zink_framebuffer *
zink_create_framebuffer(struct zink_context *ctx, struct zink_screen *screen,
                        struct zink_framebuffer_state *fb)
{
   struct zink_framebuffer *fbuf = CALLOC_STRUCT(zink_framebuffer);
   if (!fbuf)
      return NULL;

   pipe_reference_init(&fbuf->reference, 1);

   if (fb->has_null_attachments)
      fbuf->null_surface = framebuffer_null_surface_init(ctx, fb);

   VkImageView attachments[ARRAY_SIZE(fb->attachments)] = {};
   for (int i = 0; i < fb->num_attachments; i++) {
      struct zink_surface *surf = fb->attachments[i];
      if (!surf)
         surf = zink_surface(fbuf->null_surface);
      pipe_surface_reference(fbuf->surfaces + i, &surf->base);
      attachments[i] = surf->image_view;
   }

   zink_render_pass_reference(screen, &fbuf->rp, fb->rp);

   VkFramebufferCreateInfo fci = {};
   fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
   fci.renderPass = fbuf->rp->render_pass;
   fci.attachmentCount = fb->num_attachments;
   fci.pAttachments = attachments;
   fci.width = fb->width;
   fci.height = fb->height;
   fci.layers = fb->layers;

   if (vkCreateFramebuffer(screen->dev, &fci, NULL, &fbuf->fb) != VK_SUCCESS) {
      zink_destroy_framebuffer(screen, fbuf);
      return NULL;
   }

   return fbuf;
}

void
debug_describe_zink_framebuffer(char* buf, const struct zink_framebuffer *ptr)
{
   sprintf(buf, "zink_framebuffer");
}
