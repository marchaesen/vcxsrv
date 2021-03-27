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
                         struct zink_framebuffer *fb)
{
   hash_table_foreach(&fb->objects, he) {
#if defined(_WIN64) || defined(__x86_64__)
      vkDestroyFramebuffer(screen->dev, he->data, NULL);
#else
      VkFramebuffer *ptr = he->data;
      vkDestroyFramebuffer(screen->dev, *ptr, NULL);
#endif
   }

   zink_surface_reference(screen, (struct zink_surface**)&fb->null_surface, NULL);

   ralloc_free(fb);
}

void
zink_init_framebuffer(struct zink_screen *screen, struct zink_framebuffer *fb, struct zink_render_pass *rp)
{
   VkFramebuffer ret;

   if (fb->rp == rp)
      return;

   uint32_t hash = _mesa_hash_pointer(rp);

   struct hash_entry *he = _mesa_hash_table_search_pre_hashed(&fb->objects, hash, rp);
   if (he) {
#if defined(_WIN64) || defined(__x86_64__)
      ret = (VkFramebuffer)he->data;
#else
      VkFramebuffer *ptr = he->data;
      ret = *ptr;
#endif
      goto out;
   }

   VkFramebufferCreateInfo fci = {};
   fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
   fci.renderPass = rp->render_pass;
   fci.attachmentCount = fb->state.num_attachments;
   fci.pAttachments = fb->state.attachments;
   fci.width = fb->state.width;
   fci.height = fb->state.height;
   fci.layers = fb->state.layers;

   if (vkCreateFramebuffer(screen->dev, &fci, NULL, &ret) != VK_SUCCESS)
      return;
#if defined(_WIN64) || defined(__x86_64__)
   _mesa_hash_table_insert_pre_hashed(&fb->objects, hash, rp, ret);
#else
   VkFramebuffer *ptr = ralloc(fb, VkFramebuffer);
   if (!ptr) {
      vkDestroyFramebuffer(screen->dev, ret, NULL);
      return;
   }
   *ptr = ret;
   _mesa_hash_table_insert_pre_hashed(&fb->objects, hash, rp, ptr);
#endif
out:
   fb->rp = rp;
   fb->fb = ret;
}

struct zink_framebuffer *
zink_create_framebuffer(struct zink_context *ctx,
                        struct zink_framebuffer_state *state,
                        struct pipe_surface **attachments)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_framebuffer *fb = rzalloc(NULL, struct zink_framebuffer);
   if (!fb)
      return NULL;

   unsigned num_attachments = 0;
   for (int i = 0; i < state->num_attachments; i++) {
      struct zink_surface *surf;
      if (state->attachments[i]) {
         surf = zink_surface(attachments[i]);
         /* no ref! */
         fb->surfaces[i] = attachments[i];
         num_attachments++;
      } else {
         if (!fb->null_surface)
            fb->null_surface = framebuffer_null_surface_init(ctx, state);
         surf = zink_surface(fb->null_surface);
         state->attachments[i] = zink_surface(fb->null_surface)->image_view;
      }
      util_dynarray_append(&surf->framebuffer_refs, struct zink_framebuffer*, fb);
   }
   pipe_reference_init(&fb->reference, 1 + num_attachments);

   if (!_mesa_hash_table_init(&fb->objects, fb, _mesa_hash_pointer, _mesa_key_pointer_equal))
      goto fail;
   memcpy(&fb->state, state, sizeof(struct zink_framebuffer_state));

   return fb;
fail:
   zink_destroy_framebuffer(screen, fb);
   return NULL;
}

void
debug_describe_zink_framebuffer(char* buf, const struct zink_framebuffer *ptr)
{
   sprintf(buf, "zink_framebuffer");
}
