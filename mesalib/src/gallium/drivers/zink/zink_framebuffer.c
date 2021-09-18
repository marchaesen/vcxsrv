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

#include "util/u_framebuffer.h"
#include "util/u_memory.h"
#include "util/u_string.h"

void
zink_destroy_framebuffer(struct zink_screen *screen,
                         struct zink_framebuffer *fb)
{
   hash_table_foreach(&fb->objects, he) {
#if defined(_WIN64) || defined(__x86_64__)
      VKSCR(DestroyFramebuffer)(screen->dev, he->data, NULL);
#else
      VkFramebuffer *ptr = he->data;
      VKSCR(DestroyFramebuffer)(screen->dev, *ptr, NULL);
#endif
   }

   ralloc_free(fb);
}

void
zink_init_framebuffer_imageless(struct zink_screen *screen, struct zink_framebuffer *fb, struct zink_render_pass *rp)
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

   VkFramebufferCreateInfo fci;
   fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
   fci.flags = VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT;
   fci.renderPass = rp->render_pass;
   fci.attachmentCount = fb->state.num_attachments;
   fci.pAttachments = NULL;
   fci.width = fb->state.width;
   fci.height = fb->state.height;
   fci.layers = fb->state.layers + 1;

   VkFramebufferAttachmentsCreateInfo attachments;
   attachments.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENTS_CREATE_INFO;
   attachments.pNext = NULL;
   attachments.attachmentImageInfoCount = fb->state.num_attachments;
   attachments.pAttachmentImageInfos = fb->infos;
   fci.pNext = &attachments;

   if (VKSCR(CreateFramebuffer)(screen->dev, &fci, NULL, &ret) != VK_SUCCESS)
      return;
#if defined(_WIN64) || defined(__x86_64__)
   _mesa_hash_table_insert_pre_hashed(&fb->objects, hash, rp, ret);
#else
   VkFramebuffer *ptr = ralloc(fb, VkFramebuffer);
   if (!ptr) {
      VKSCR(DestroyFramebuffer)(screen->dev, ret, NULL);
      return;
   }
   *ptr = ret;
   _mesa_hash_table_insert_pre_hashed(&fb->objects, hash, rp, ptr);
#endif
out:
   fb->rp = rp;
   fb->fb = ret;
}

static void
populate_attachment_info(VkFramebufferAttachmentImageInfo *att, struct zink_surface_info *info)
{
   att->sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_ATTACHMENT_IMAGE_INFO;
   att->pNext = NULL;
   memcpy(&att->flags, &info->flags, offsetof(struct zink_surface_info, format));
   att->viewFormatCount = 1;
   att->pViewFormats = &info->format;
}

static struct zink_framebuffer *
create_framebuffer_imageless(struct zink_context *ctx, struct zink_framebuffer_state *state)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);
   struct zink_framebuffer *fb = rzalloc(ctx, struct zink_framebuffer);
   if (!fb)
      return NULL;
   pipe_reference_init(&fb->reference, 1);

   if (!_mesa_hash_table_init(&fb->objects, fb, _mesa_hash_pointer, _mesa_key_pointer_equal))
      goto fail;
   memcpy(&fb->state, state, sizeof(struct zink_framebuffer_state));
   for (int i = 0; i < state->num_attachments; i++)
      populate_attachment_info(&fb->infos[i], &fb->state.infos[i]);

   return fb;
fail:
   zink_destroy_framebuffer(screen, fb);
   return NULL;
}

struct zink_framebuffer *
zink_get_framebuffer_imageless(struct zink_context *ctx)
{
   assert(zink_screen(ctx->base.screen)->info.have_KHR_imageless_framebuffer);

   struct zink_framebuffer_state state;
   for (int i = 0; i < ctx->fb_state.nr_cbufs; i++) {
      struct pipe_surface *psurf = ctx->fb_state.cbufs[i];
      if (!psurf)
         psurf = ctx->dummy_surface[util_logbase2_ceil(ctx->gfx_pipeline_state.rast_samples+1)];
      struct zink_surface *surface = zink_csurface(psurf);
      memcpy(&state.infos[i], &surface->info, sizeof(surface->info));
   }

   state.num_attachments = ctx->fb_state.nr_cbufs;
   if (ctx->fb_state.zsbuf) {
      struct pipe_surface *psurf = ctx->fb_state.zsbuf;
      struct zink_surface *surface = zink_csurface(psurf);
      memcpy(&state.infos[state.num_attachments], &surface->info, sizeof(surface->info));
      state.num_attachments++;
   }

   state.width = MAX2(ctx->fb_state.width, 1);
   state.height = MAX2(ctx->fb_state.height, 1);
   state.layers = MAX2(util_framebuffer_get_num_layers(&ctx->fb_state), 1) - 1;
   state.samples = ctx->fb_state.samples - 1;

   struct zink_framebuffer *fb;
   struct hash_entry *entry = _mesa_hash_table_search(&ctx->framebuffer_cache, &state);
   if (entry)
      return entry->data;

   fb = create_framebuffer_imageless(ctx, &state);
   _mesa_hash_table_insert(&ctx->framebuffer_cache, &fb->state, fb);

   return fb;
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

   VkFramebufferCreateInfo fci = {0};
   fci.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
   fci.renderPass = rp->render_pass;
   fci.attachmentCount = fb->state.num_attachments;
   fci.pAttachments = fb->state.attachments;
   fci.width = fb->state.width;
   fci.height = fb->state.height;
   fci.layers = fb->state.layers + 1;

   if (VKSCR(CreateFramebuffer)(screen->dev, &fci, NULL, &ret) != VK_SUCCESS)
      return;
#if defined(_WIN64) || defined(__x86_64__)
   _mesa_hash_table_insert_pre_hashed(&fb->objects, hash, rp, ret);
#else
   VkFramebuffer *ptr = ralloc(fb, VkFramebuffer);
   if (!ptr) {
      VKSCR(DestroyFramebuffer)(screen->dev, ret, NULL);
      return;
   }
   *ptr = ret;
   _mesa_hash_table_insert_pre_hashed(&fb->objects, hash, rp, ptr);
#endif
out:
   fb->rp = rp;
   fb->fb = ret;
}

static struct zink_framebuffer *
create_framebuffer(struct zink_context *ctx,
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
         surf = zink_csurface(attachments[i]);
         /* no ref! */
         fb->surfaces[i] = attachments[i];
         num_attachments++;
         util_dynarray_append(&surf->framebuffer_refs, struct zink_framebuffer*, fb);
      } else {
         surf = zink_csurface(ctx->dummy_surface[util_logbase2_ceil(state->samples+1)]);
         state->attachments[i] = surf->image_view;
      }
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

struct zink_framebuffer *
zink_get_framebuffer(struct zink_context *ctx)
{
   struct zink_screen *screen = zink_screen(ctx->base.screen);

   assert(!screen->info.have_KHR_imageless_framebuffer);

   struct pipe_surface *attachments[PIPE_MAX_COLOR_BUFS + 1] = {0};

   struct zink_framebuffer_state state = {0};
   for (int i = 0; i < ctx->fb_state.nr_cbufs; i++) {
      struct pipe_surface *psurf = ctx->fb_state.cbufs[i];
      state.attachments[i] = psurf ? zink_csurface(psurf)->image_view : VK_NULL_HANDLE;
      attachments[i] = psurf;
   }

   state.num_attachments = ctx->fb_state.nr_cbufs;
   if (ctx->fb_state.zsbuf) {
      struct pipe_surface *psurf = ctx->fb_state.zsbuf;
      state.attachments[state.num_attachments] = psurf ? zink_csurface(psurf)->image_view : VK_NULL_HANDLE;
      attachments[state.num_attachments++] = psurf;
   }

   state.width = MAX2(ctx->fb_state.width, 1);
   state.height = MAX2(ctx->fb_state.height, 1);
   state.layers = MAX2(util_framebuffer_get_num_layers(&ctx->fb_state), 1) - 1;
   state.samples = ctx->fb_state.samples - 1;

   struct zink_framebuffer *fb;
   simple_mtx_lock(&screen->framebuffer_mtx);
   struct hash_entry *entry = _mesa_hash_table_search(&screen->framebuffer_cache, &state);
   if (entry) {
      fb = (void*)entry->data;
      struct zink_framebuffer *fb_ref = NULL;
      /* this gains 1 ref every time we reuse it */
      zink_framebuffer_reference(screen, &fb_ref, fb);
   } else {
      /* this adds 1 extra ref on creation because all newly-created framebuffers are
       * going to be bound; necessary to handle framebuffers which have no "real" attachments
       * and are only using null surfaces since the only ref they get is the extra one here
       */
      fb = create_framebuffer(ctx, &state, attachments);
      _mesa_hash_table_insert(&screen->framebuffer_cache, &fb->state, fb);
   }
   simple_mtx_unlock(&screen->framebuffer_mtx);

   return fb;
}
