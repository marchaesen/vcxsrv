/**************************************************************************
 *
 * Copyright 2009, VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/
/*
 * Author: Keith Whitwell <keithw@vmware.com>
 * Author: Jakob Bornecrantz <wallbraker@gmail.com>
 */

#include "utils.h"

#include "dri_screen.h"
#include "dri_drawable.h"
#include "dri_context.h"
#include "frontend/drm_driver.h"

#include "pipe/p_context.h"
#include "pipe-loader/pipe_loader.h"
#include "state_tracker/st_context.h"

#include "util/u_memory.h"

GLboolean
dri_create_context(gl_api api, const struct gl_config * visual,
                   __DRIcontext * cPriv,
                   const struct __DriverContextConfig *ctx_config,
                   unsigned *error,
                   void *sharedContextPrivate)
{
   __DRIscreen *sPriv = cPriv->driScreenPriv;
   struct dri_screen *screen = dri_screen(sPriv);
   struct st_api *stapi = screen->st_api;
   struct dri_context *ctx = NULL;
   struct st_context_iface *st_share = NULL;
   struct st_context_attribs attribs;
   enum st_context_error ctx_err = 0;
   unsigned allowed_flags = __DRI_CTX_FLAG_DEBUG |
                            __DRI_CTX_FLAG_FORWARD_COMPATIBLE |
                            __DRI_CTX_FLAG_NO_ERROR;
   unsigned allowed_attribs =
      __DRIVER_CONTEXT_ATTRIB_PRIORITY |
      __DRIVER_CONTEXT_ATTRIB_RELEASE_BEHAVIOR;
   const __DRIbackgroundCallableExtension *backgroundCallable =
      screen->sPriv->dri2.backgroundCallable;
   const struct driOptionCache *optionCache = &screen->dev->option_cache;

   if (screen->has_reset_status_query) {
      allowed_flags |= __DRI_CTX_FLAG_ROBUST_BUFFER_ACCESS;
      allowed_attribs |= __DRIVER_CONTEXT_ATTRIB_RESET_STRATEGY;
   }

   if (ctx_config->flags & ~allowed_flags) {
      *error = __DRI_CTX_ERROR_UNKNOWN_FLAG;
      goto fail;
   }

   if (ctx_config->attribute_mask & ~allowed_attribs) {
      *error = __DRI_CTX_ERROR_UNKNOWN_ATTRIBUTE;
      goto fail;
   }

   memset(&attribs, 0, sizeof(attribs));
   switch (api) {
   case API_OPENGLES:
      attribs.profile = ST_PROFILE_OPENGL_ES1;
      break;
   case API_OPENGLES2:
      attribs.profile = ST_PROFILE_OPENGL_ES2;
      break;
   case API_OPENGL_COMPAT:
   case API_OPENGL_CORE:
      if (driQueryOptionb(optionCache, "force_compat_profile")) {
         attribs.profile = ST_PROFILE_DEFAULT;
      } else {
         attribs.profile = api == API_OPENGL_COMPAT ? ST_PROFILE_DEFAULT
                                                    : ST_PROFILE_OPENGL_CORE;
      }

      attribs.major = ctx_config->major_version;
      attribs.minor = ctx_config->minor_version;

      if ((ctx_config->flags & __DRI_CTX_FLAG_FORWARD_COMPATIBLE) != 0)
	 attribs.flags |= ST_CONTEXT_FLAG_FORWARD_COMPATIBLE;
      break;
   default:
      *error = __DRI_CTX_ERROR_BAD_API;
      goto fail;
   }

   if ((ctx_config->flags & __DRI_CTX_FLAG_DEBUG) != 0)
      attribs.flags |= ST_CONTEXT_FLAG_DEBUG;

   if (ctx_config->flags & __DRI_CTX_FLAG_ROBUST_BUFFER_ACCESS)
      attribs.flags |= ST_CONTEXT_FLAG_ROBUST_ACCESS;

   if (ctx_config->attribute_mask & __DRIVER_CONTEXT_ATTRIB_RESET_STRATEGY)
      if (ctx_config->reset_strategy != __DRI_CTX_RESET_NO_NOTIFICATION)
         attribs.flags |= ST_CONTEXT_FLAG_RESET_NOTIFICATION_ENABLED;

   if (ctx_config->flags & __DRI_CTX_FLAG_NO_ERROR)
      attribs.flags |= ST_CONTEXT_FLAG_NO_ERROR;

   if (ctx_config->attribute_mask & __DRIVER_CONTEXT_ATTRIB_PRIORITY) {
      switch (ctx_config->priority) {
      case __DRI_CTX_PRIORITY_LOW:
         attribs.flags |= ST_CONTEXT_FLAG_LOW_PRIORITY;
         break;
      case __DRI_CTX_PRIORITY_HIGH:
         attribs.flags |= ST_CONTEXT_FLAG_HIGH_PRIORITY;
         break;
      default:
         break;
      }
   }

   if ((ctx_config->attribute_mask & __DRIVER_CONTEXT_ATTRIB_RELEASE_BEHAVIOR)
       && (ctx_config->release_behavior == __DRI_CTX_RELEASE_BEHAVIOR_NONE))
      attribs.flags |= ST_CONTEXT_FLAG_RELEASE_NONE;

   struct dri_context *share_ctx = NULL;
   if (sharedContextPrivate) {
      share_ctx = (struct dri_context *)sharedContextPrivate;
      st_share = share_ctx->st;
   }

   ctx = CALLOC_STRUCT(dri_context);
   if (ctx == NULL) {
      *error = __DRI_CTX_ERROR_NO_MEMORY;
      goto fail;
   }

   cPriv->driverPrivate = ctx;
   ctx->cPriv = cPriv;
   ctx->sPriv = sPriv;

   if (driQueryOptionb(&screen->dev->option_cache, "mesa_no_error"))
      attribs.flags |= ST_CONTEXT_FLAG_NO_ERROR;

   attribs.options = screen->options;
   dri_fill_st_visual(&attribs.visual, screen, visual);
   ctx->st = stapi->create_context(stapi, &screen->base, &attribs, &ctx_err,
				   st_share);
   if (ctx->st == NULL) {
      switch (ctx_err) {
      case ST_CONTEXT_SUCCESS:
	 *error = __DRI_CTX_ERROR_SUCCESS;
	 break;
      case ST_CONTEXT_ERROR_NO_MEMORY:
	 *error = __DRI_CTX_ERROR_NO_MEMORY;
	 break;
      case ST_CONTEXT_ERROR_BAD_API:
	 *error = __DRI_CTX_ERROR_BAD_API;
	 break;
      case ST_CONTEXT_ERROR_BAD_VERSION:
	 *error = __DRI_CTX_ERROR_BAD_VERSION;
	 break;
      case ST_CONTEXT_ERROR_BAD_FLAG:
	 *error = __DRI_CTX_ERROR_BAD_FLAG;
	 break;
      case ST_CONTEXT_ERROR_UNKNOWN_ATTRIBUTE:
	 *error = __DRI_CTX_ERROR_UNKNOWN_ATTRIBUTE;
	 break;
      case ST_CONTEXT_ERROR_UNKNOWN_FLAG:
	 *error = __DRI_CTX_ERROR_UNKNOWN_FLAG;
	 break;
      }
      goto fail;
   }
   ctx->st->st_manager_private = (void *) ctx;
   ctx->stapi = stapi;

   if (ctx->st->cso_context) {
      ctx->pp = pp_init(ctx->st->pipe, screen->pp_enabled, ctx->st->cso_context);
      ctx->hud = hud_create(ctx->st->cso_context,
                            share_ctx ? share_ctx->hud : NULL);
   }

   /* Do this last. */
   if (ctx->st->start_thread &&
         driQueryOptionb(&screen->dev->option_cache, "mesa_glthread")) {

      if (backgroundCallable && backgroundCallable->base.version >= 2 &&
            backgroundCallable->isThreadSafe) {

         if (backgroundCallable->isThreadSafe(cPriv->loaderPrivate))
            ctx->st->start_thread(ctx->st);
         else
            fprintf(stderr, "dri_create_context: glthread isn't thread safe "
                  "- missing call XInitThreads\n");
      } else {
         fprintf(stderr, "dri_create_context: requested glthread but driver "
               "is missing backgroundCallable V2 extension\n");
      }
   }

   *error = __DRI_CTX_ERROR_SUCCESS;
   return GL_TRUE;

 fail:
   if (ctx && ctx->st)
      ctx->st->destroy(ctx->st);

   free(ctx);
   return GL_FALSE;
}

void
dri_destroy_context(__DRIcontext * cPriv)
{
   struct dri_context *ctx = dri_context(cPriv);

   if (ctx->hud) {
      hud_destroy(ctx->hud, ctx->st->cso_context);
   }

   if (ctx->pp)
      pp_free(ctx->pp);

   /* No particular reason to wait for command completion before
    * destroying a context, but we flush the context here
    * to avoid having to add code elsewhere to cope with flushing a
    * partially destroyed context.
    */
   ctx->st->flush(ctx->st, 0, NULL, NULL, NULL);
   ctx->st->destroy(ctx->st);
   free(ctx);
}

/* This is called inside MakeCurrent to unbind the context. */
GLboolean
dri_unbind_context(__DRIcontext * cPriv)
{
   /* dri_util.c ensures cPriv is not null */
   struct dri_screen *screen = dri_screen(cPriv->driScreenPriv);
   struct dri_context *ctx = dri_context(cPriv);
   struct st_context_iface *st = ctx->st;
   struct st_api *stapi = screen->st_api;

   if (--ctx->bind_count == 0) {
      if (st == stapi->get_current(stapi)) {
         if (st->thread_finish)
            st->thread_finish(st);

         /* Record HUD queries for the duration the context was "current". */
         if (ctx->hud)
            hud_record_only(ctx->hud, st->pipe);

         stapi->make_current(stapi, NULL, NULL, NULL);
      }
   }

   return GL_TRUE;
}

GLboolean
dri_make_current(__DRIcontext * cPriv,
		 __DRIdrawable * driDrawPriv,
		 __DRIdrawable * driReadPriv)
{
   /* dri_util.c ensures cPriv is not null */
   struct dri_context *ctx = dri_context(cPriv);
   struct dri_drawable *draw = dri_drawable(driDrawPriv);
   struct dri_drawable *read = dri_drawable(driReadPriv);

   ++ctx->bind_count;

   if (!draw && !read)
      return ctx->stapi->make_current(ctx->stapi, ctx->st, NULL, NULL);
   else if (!draw || !read)
      return GL_FALSE;

   if (ctx->dPriv != driDrawPriv) {
      ctx->dPriv = driDrawPriv;
      draw->texture_stamp = driDrawPriv->lastStamp - 1;
   }
   if (ctx->rPriv != driReadPriv) {
      ctx->rPriv = driReadPriv;
      read->texture_stamp = driReadPriv->lastStamp - 1;
   }

   ctx->stapi->make_current(ctx->stapi, ctx->st, &draw->base, &read->base);

   /* This is ok to call here. If they are already init, it's a no-op. */
   if (ctx->pp && draw->textures[ST_ATTACHMENT_BACK_LEFT])
      pp_init_fbos(ctx->pp, draw->textures[ST_ATTACHMENT_BACK_LEFT]->width0,
                   draw->textures[ST_ATTACHMENT_BACK_LEFT]->height0);

   return GL_TRUE;
}

struct dri_context *
dri_get_current(__DRIscreen *sPriv)
{
   struct dri_screen *screen = dri_screen(sPriv);
   struct st_api *stapi = screen->st_api;
   struct st_context_iface *st;

   st = stapi->get_current(stapi);

   return (struct dri_context *) st ? st->st_manager_private : NULL;
}

/* vim: set sw=3 ts=8 sts=3 expandtab: */
