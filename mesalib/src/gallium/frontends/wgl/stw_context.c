/**************************************************************************
 *
 * Copyright 2008 VMware, Inc.
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

#include <windows.h>

#define WGL_WGLEXT_PROTOTYPES

#include <GL/gl.h>
#include <GL/wglext.h>

#include "pipe/p_compiler.h"
#include "pipe/p_context.h"
#include "pipe/p_state.h"
#include "util/u_memory.h"
#include "util/u_atomic.h"
#include "frontend/api.h"
#include "hud/hud_context.h"

#include "gldrv.h"
#include "stw_device.h"
#include "stw_winsys.h"
#include "stw_framebuffer.h"
#include "stw_pixelformat.h"
#include "stw_context.h"
#include "stw_tls.h"


struct stw_context *
stw_current_context(void)
{
   struct st_context_iface *st;

   st = (stw_dev) ? stw_dev->stapi->get_current(stw_dev->stapi) : NULL;

   return (struct stw_context *) ((st) ? st->st_manager_private : NULL);
}


BOOL APIENTRY
DrvCopyContext(DHGLRC dhrcSource, DHGLRC dhrcDest, UINT fuMask)
{
   struct stw_context *src;
   struct stw_context *dst;
   BOOL ret = FALSE;

   if (!stw_dev)
      return FALSE;

   stw_lock_contexts(stw_dev);

   src = stw_lookup_context_locked( dhrcSource );
   dst = stw_lookup_context_locked( dhrcDest );

   if (src && dst) {
      /* FIXME */
      assert(0);
      (void) src;
      (void) dst;
      (void) fuMask;
   }

   stw_unlock_contexts(stw_dev);

   return ret;
}


BOOL APIENTRY
DrvShareLists(DHGLRC dhglrc1, DHGLRC dhglrc2)
{
   struct stw_context *ctx1;
   struct stw_context *ctx2;
   BOOL ret = FALSE;

   if (!stw_dev)
      return FALSE;

   stw_lock_contexts(stw_dev);

   ctx1 = stw_lookup_context_locked( dhglrc1 );
   ctx2 = stw_lookup_context_locked( dhglrc2 );

   if (ctx1 && ctx2 && ctx2->st->share) {
      ret = ctx2->st->share(ctx2->st, ctx1->st);
      ctx1->shared = TRUE;
      ctx2->shared = TRUE;
   }

   stw_unlock_contexts(stw_dev);

   return ret;
}


DHGLRC APIENTRY
DrvCreateContext(HDC hdc)
{
   return DrvCreateLayerContext( hdc, 0 );
}


DHGLRC APIENTRY
DrvCreateLayerContext(HDC hdc, INT iLayerPlane)
{
   return stw_create_context_attribs(hdc, iLayerPlane, 0, 1, 0, 0,
                                     WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB,
                                     0);
}


/**
 * Return the stw pixel format that most closely matches the pixel format
 * on HDC.
 * Used to get a pixel format when SetPixelFormat() hasn't been called before.
 */
static int
get_matching_pixel_format(HDC hdc)
{
   int iPixelFormat = GetPixelFormat(hdc);
   PIXELFORMATDESCRIPTOR pfd;

   if (!iPixelFormat)
      return 0;
   if (!DescribePixelFormat(hdc, iPixelFormat, sizeof(pfd), &pfd))
      return 0;
   return stw_pixelformat_choose(hdc, &pfd);
}


/**
 * Called via DrvCreateContext(), DrvCreateLayerContext() and
 * wglCreateContextAttribsARB() to actually create a rendering context.
 * \param handle  the desired DHGLRC handle to use for the context, or zero
 *                if a new handle should be allocated.
 * \return the handle for the new context or zero if there was a problem.
 */
DHGLRC
stw_create_context_attribs(HDC hdc, INT iLayerPlane, DHGLRC hShareContext,
                           int majorVersion, int minorVersion,
                           int contextFlags, int profileMask,
                           DHGLRC handle)
{
   int iPixelFormat;
   struct stw_framebuffer *fb;
   const struct stw_pixelformat_info *pfi;
   struct st_context_attribs attribs;
   struct stw_context *ctx = NULL;
   struct stw_context *shareCtx = NULL;
   enum st_context_error ctx_err = 0;

   if (!stw_dev)
      return 0;

   if (iLayerPlane != 0)
      return 0;

   /*
    * GDI only knows about displayable pixel formats, so determine the pixel
    * format from the framebuffer.
    *
    * This also allows to use a OpenGL DLL / ICD without installing.
    */
   fb = stw_framebuffer_from_hdc( hdc );
   if (fb) {
      iPixelFormat = fb->iPixelFormat;
      stw_framebuffer_unlock(fb);
   } else {
      /* Applications should call SetPixelFormat before creating a context,
       * but not all do, and the opengl32 runtime seems to use a default
       * pixel format in some cases, so use that.
       */
      iPixelFormat = get_matching_pixel_format(hdc);
      if (!iPixelFormat)
         return 0;
   }

   pfi = stw_pixelformat_get_info( iPixelFormat );

   if (hShareContext != 0) {
      stw_lock_contexts(stw_dev);
      shareCtx = stw_lookup_context_locked( hShareContext );
      shareCtx->shared = TRUE;
      stw_unlock_contexts(stw_dev);
   }

   ctx = CALLOC_STRUCT( stw_context );
   if (ctx == NULL)
      goto no_ctx;

   ctx->hDrawDC = hdc;
   ctx->hReadDC = hdc;
   ctx->iPixelFormat = iPixelFormat;
   ctx->shared = shareCtx != NULL;

   memset(&attribs, 0, sizeof(attribs));
   attribs.visual = pfi->stvis;
   attribs.major = majorVersion;
   attribs.minor = minorVersion;
   if (contextFlags & WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB)
      attribs.flags |= ST_CONTEXT_FLAG_FORWARD_COMPATIBLE;
   if (contextFlags & WGL_CONTEXT_DEBUG_BIT_ARB)
      attribs.flags |= ST_CONTEXT_FLAG_DEBUG;

   switch (profileMask) {
   case WGL_CONTEXT_CORE_PROFILE_BIT_ARB:
      /* There are no profiles before OpenGL 3.2.  The
       * WGL_ARB_create_context_profile spec says:
       *
       *     "If the requested OpenGL version is less than 3.2,
       *     WGL_CONTEXT_PROFILE_MASK_ARB is ignored and the functionality
       *     of the context is determined solely by the requested version."
       */
      if (majorVersion > 3 || (majorVersion == 3 && minorVersion >= 2)) {
         attribs.profile = ST_PROFILE_OPENGL_CORE;
         break;
      }
      /* fall-through */
   case WGL_CONTEXT_COMPATIBILITY_PROFILE_BIT_ARB:
      /*
       * The spec also says:
       *
       *     "If version 3.1 is requested, the context returned may implement
       *     any of the following versions:
       *
       *       * Version 3.1. The GL_ARB_compatibility extension may or may not
       *         be implemented, as determined by the implementation.
       *       * The core profile of version 3.2 or greater."
       *
       * But Mesa doesn't support GL_ARB_compatibility, while most prevalent
       * Windows OpenGL implementations do, and unfortunately many Windows
       * applications don't check whether they receive or not a context with
       * GL_ARB_compatibility, so returning a core profile here does more harm
       * than good.
       */
      attribs.profile = ST_PROFILE_DEFAULT;
      break;
   case WGL_CONTEXT_ES_PROFILE_BIT_EXT:
      if (majorVersion >= 2) {
         attribs.profile = ST_PROFILE_OPENGL_ES2;
      } else {
         attribs.profile = ST_PROFILE_OPENGL_ES1;
      }
      break;
   default:
      assert(0);
      goto no_st_ctx;
   }

   ctx->st = stw_dev->stapi->create_context(stw_dev->stapi,
         stw_dev->smapi, &attribs, &ctx_err, shareCtx ? shareCtx->st : NULL);
   if (ctx->st == NULL)
      goto no_st_ctx;

   ctx->st->st_manager_private = (void *) ctx;

   if (ctx->st->cso_context) {
      ctx->hud = hud_create(ctx->st->cso_context, NULL);
   }

   stw_lock_contexts(stw_dev);
   if (handle) {
      /* We're replacing the context data for this handle. See the
       * wglCreateContextAttribsARB() function.
       */
      struct stw_context *old_ctx =
         stw_lookup_context_locked((unsigned) handle);
      if (old_ctx) {
         /* free the old context data associated with this handle */
         if (old_ctx->hud) {
            hud_destroy(old_ctx->hud, NULL);
         }
         ctx->st->destroy(old_ctx->st);
         FREE(old_ctx);
      }

      /* replace table entry */
      handle_table_set(stw_dev->ctx_table, (unsigned) handle, ctx);
   }
   else {
      /* create new table entry */
      handle = (DHGLRC) handle_table_add(stw_dev->ctx_table, ctx);
   }

   ctx->dhglrc = handle;

   stw_unlock_contexts(stw_dev);

   if (!ctx->dhglrc)
      goto no_hglrc;

   return ctx->dhglrc;

no_hglrc:
   if (ctx->hud) {
      hud_destroy(ctx->hud, NULL);
   }
   ctx->st->destroy(ctx->st);
no_st_ctx:
   FREE(ctx);
no_ctx:
   return 0;
}


BOOL APIENTRY
DrvDeleteContext(DHGLRC dhglrc)
{
   struct stw_context *ctx ;
   BOOL ret = FALSE;

   if (!stw_dev)
      return FALSE;

   stw_lock_contexts(stw_dev);
   ctx = stw_lookup_context_locked(dhglrc);
   handle_table_remove(stw_dev->ctx_table, dhglrc);
   stw_unlock_contexts(stw_dev);

   if (ctx) {
      struct stw_context *curctx = stw_current_context();

      /* Unbind current if deleting current context. */
      if (curctx == ctx)
         stw_dev->stapi->make_current(stw_dev->stapi, NULL, NULL, NULL);

      if (ctx->hud) {
         hud_destroy(ctx->hud, NULL);
      }

      ctx->st->destroy(ctx->st);
      FREE(ctx);

      ret = TRUE;
   }

   return ret;
}


BOOL APIENTRY
DrvReleaseContext(DHGLRC dhglrc)
{
   struct stw_context *ctx;

   if (!stw_dev)
      return FALSE;

   stw_lock_contexts(stw_dev);
   ctx = stw_lookup_context_locked( dhglrc );
   stw_unlock_contexts(stw_dev);

   if (!ctx)
      return FALSE;

   /* The expectation is that ctx is the same context which is
    * current for this thread.  We should check that and return False
    * if not the case.
    */
   if (ctx != stw_current_context())
      return FALSE;

   if (stw_make_current( NULL, NULL, 0 ) == FALSE)
      return FALSE;

   return TRUE;
}


DHGLRC
stw_get_current_context( void )
{
   struct stw_context *ctx;

   ctx = stw_current_context();
   if (!ctx)
      return 0;

   return ctx->dhglrc;
}


HDC
stw_get_current_dc( void )
{
   struct stw_context *ctx;

   ctx = stw_current_context();
   if (!ctx)
      return NULL;

   return ctx->hDrawDC;
}

HDC
stw_get_current_read_dc( void )
{
   struct stw_context *ctx;

   ctx = stw_current_context();
   if (!ctx)
      return NULL;

   return ctx->hReadDC;
}

BOOL
stw_make_current(HDC hDrawDC, HDC hReadDC, DHGLRC dhglrc)
{
   struct stw_context *old_ctx = NULL;
   struct stw_context *ctx = NULL;
   BOOL ret = FALSE;

   if (!stw_dev)
      return FALSE;

   old_ctx = stw_current_context();
   if (old_ctx != NULL) {
      if (old_ctx->dhglrc == dhglrc) {
         if (old_ctx->hDrawDC == hDrawDC && old_ctx->hReadDC == hReadDC) {
            /* Return if already current. */
            return TRUE;
         }
      } else {
         if (old_ctx->shared) {
            struct pipe_fence_handle *fence = NULL;
            old_ctx->st->flush(old_ctx->st,
                               ST_FLUSH_FRONT | ST_FLUSH_WAIT, &fence,
                               NULL, NULL);
         }
         else {
            old_ctx->st->flush(old_ctx->st, ST_FLUSH_FRONT, NULL, NULL, NULL);
         }
      }
   }

   if (dhglrc) {
      struct stw_framebuffer *fb = NULL;
      struct stw_framebuffer *fbRead = NULL;
      stw_lock_contexts(stw_dev);
      ctx = stw_lookup_context_locked( dhglrc );
      stw_unlock_contexts(stw_dev);
      if (!ctx) {
         goto fail;
      }

      /* This call locks fb's mutex */
      fb = stw_framebuffer_from_hdc( hDrawDC );
      if (fb) {
         stw_framebuffer_update(fb);
      }
      else {
         /* Applications should call SetPixelFormat before creating a context,
          * but not all do, and the opengl32 runtime seems to use a default
          * pixel format in some cases, so we must create a framebuffer for
          * those here.
          */
         int iPixelFormat = get_matching_pixel_format(hDrawDC);
         if (iPixelFormat)
            fb = stw_framebuffer_create( hDrawDC, iPixelFormat );
         if (!fb)
            goto fail;
      }

      if (fb->iPixelFormat != ctx->iPixelFormat) {
         stw_framebuffer_unlock(fb);
         SetLastError(ERROR_INVALID_PIXEL_FORMAT);
         goto fail;
      }

      /* Bind the new framebuffer */
      ctx->hDrawDC = hDrawDC;
      ctx->hReadDC = hReadDC;

      struct stw_framebuffer *old_fb = ctx->current_framebuffer;
      if (old_fb != fb) {
         stw_framebuffer_reference_locked(fb);
         ctx->current_framebuffer = fb;
      }
      stw_framebuffer_unlock(fb);

      if (hReadDC) {
         if (hReadDC == hDrawDC) {
            fbRead = fb;
         }
         else {
            fbRead = stw_framebuffer_from_hdc( hReadDC );

            if (fbRead) {
               stw_framebuffer_update(fbRead);
            }
            else {
               /* Applications should call SetPixelFormat before creating a
                * context, but not all do, and the opengl32 runtime seems to
                * use a default pixel format in some cases, so we must create
                * a framebuffer for those here.
                */
               int iPixelFormat = GetPixelFormat(hReadDC);
               if (iPixelFormat)
                  fbRead = stw_framebuffer_create( hReadDC, iPixelFormat );
               if (!fbRead)
                  goto fail;
            }

            if (fbRead->iPixelFormat != ctx->iPixelFormat) {
               stw_framebuffer_unlock(fbRead);
               SetLastError(ERROR_INVALID_PIXEL_FORMAT);
               goto fail;
            }
            stw_framebuffer_unlock(fbRead);
         }
         ret = stw_dev->stapi->make_current(stw_dev->stapi, ctx->st,
                                            fb->stfb, fbRead->stfb);
      }
      else {
         /* Note: when we call this function we will wind up in the
          * stw_st_framebuffer_validate_locked() function which will incur
          * a recursive fb->mutex lock.
          */
         ret = stw_dev->stapi->make_current(stw_dev->stapi, ctx->st,
                                            fb->stfb, fb->stfb);
      }

      if (old_fb && old_fb != fb) {
         stw_lock_framebuffers(stw_dev);
         stw_framebuffer_lock(old_fb);
         stw_framebuffer_release_locked(old_fb);
         stw_unlock_framebuffers(stw_dev);
      }

fail:
      if (fb) {
         /* fb must be unlocked at this point. */
         assert(!stw_own_mutex(&fb->mutex));
      }

      /* On failure, make the thread's current rendering context not current
       * before returning.
       */
      if (!ret) {
         stw_make_current(NULL, NULL, 0);
      }
   } else {
      ret = stw_dev->stapi->make_current(stw_dev->stapi, NULL, NULL, NULL);
   }

   /* Unreference the previous framebuffer if any. It must be done after
    * make_current, as it can be referenced inside.
    */
   if (old_ctx && old_ctx != ctx) {
      struct stw_framebuffer *old_fb = old_ctx->current_framebuffer;
      if (old_fb) {
         old_ctx->current_framebuffer = NULL;
         stw_lock_framebuffers(stw_dev);
         stw_framebuffer_lock(old_fb);
         stw_framebuffer_release_locked(old_fb);
         stw_unlock_framebuffers(stw_dev);
      }
   }

   return ret;
}


/**
 * Notify the current context that the framebuffer has become invalid.
 */
void
stw_notify_current_locked( struct stw_framebuffer *fb )
{
   p_atomic_inc(&fb->stfb->stamp);
}


/**
 * Although WGL allows different dispatch entrypoints per context
 */
static const GLCLTPROCTABLE cpt =
{
   OPENGL_VERSION_110_ENTRIES,
   {
      &glNewList,
      &glEndList,
      &glCallList,
      &glCallLists,
      &glDeleteLists,
      &glGenLists,
      &glListBase,
      &glBegin,
      &glBitmap,
      &glColor3b,
      &glColor3bv,
      &glColor3d,
      &glColor3dv,
      &glColor3f,
      &glColor3fv,
      &glColor3i,
      &glColor3iv,
      &glColor3s,
      &glColor3sv,
      &glColor3ub,
      &glColor3ubv,
      &glColor3ui,
      &glColor3uiv,
      &glColor3us,
      &glColor3usv,
      &glColor4b,
      &glColor4bv,
      &glColor4d,
      &glColor4dv,
      &glColor4f,
      &glColor4fv,
      &glColor4i,
      &glColor4iv,
      &glColor4s,
      &glColor4sv,
      &glColor4ub,
      &glColor4ubv,
      &glColor4ui,
      &glColor4uiv,
      &glColor4us,
      &glColor4usv,
      &glEdgeFlag,
      &glEdgeFlagv,
      &glEnd,
      &glIndexd,
      &glIndexdv,
      &glIndexf,
      &glIndexfv,
      &glIndexi,
      &glIndexiv,
      &glIndexs,
      &glIndexsv,
      &glNormal3b,
      &glNormal3bv,
      &glNormal3d,
      &glNormal3dv,
      &glNormal3f,
      &glNormal3fv,
      &glNormal3i,
      &glNormal3iv,
      &glNormal3s,
      &glNormal3sv,
      &glRasterPos2d,
      &glRasterPos2dv,
      &glRasterPos2f,
      &glRasterPos2fv,
      &glRasterPos2i,
      &glRasterPos2iv,
      &glRasterPos2s,
      &glRasterPos2sv,
      &glRasterPos3d,
      &glRasterPos3dv,
      &glRasterPos3f,
      &glRasterPos3fv,
      &glRasterPos3i,
      &glRasterPos3iv,
      &glRasterPos3s,
      &glRasterPos3sv,
      &glRasterPos4d,
      &glRasterPos4dv,
      &glRasterPos4f,
      &glRasterPos4fv,
      &glRasterPos4i,
      &glRasterPos4iv,
      &glRasterPos4s,
      &glRasterPos4sv,
      &glRectd,
      &glRectdv,
      &glRectf,
      &glRectfv,
      &glRecti,
      &glRectiv,
      &glRects,
      &glRectsv,
      &glTexCoord1d,
      &glTexCoord1dv,
      &glTexCoord1f,
      &glTexCoord1fv,
      &glTexCoord1i,
      &glTexCoord1iv,
      &glTexCoord1s,
      &glTexCoord1sv,
      &glTexCoord2d,
      &glTexCoord2dv,
      &glTexCoord2f,
      &glTexCoord2fv,
      &glTexCoord2i,
      &glTexCoord2iv,
      &glTexCoord2s,
      &glTexCoord2sv,
      &glTexCoord3d,
      &glTexCoord3dv,
      &glTexCoord3f,
      &glTexCoord3fv,
      &glTexCoord3i,
      &glTexCoord3iv,
      &glTexCoord3s,
      &glTexCoord3sv,
      &glTexCoord4d,
      &glTexCoord4dv,
      &glTexCoord4f,
      &glTexCoord4fv,
      &glTexCoord4i,
      &glTexCoord4iv,
      &glTexCoord4s,
      &glTexCoord4sv,
      &glVertex2d,
      &glVertex2dv,
      &glVertex2f,
      &glVertex2fv,
      &glVertex2i,
      &glVertex2iv,
      &glVertex2s,
      &glVertex2sv,
      &glVertex3d,
      &glVertex3dv,
      &glVertex3f,
      &glVertex3fv,
      &glVertex3i,
      &glVertex3iv,
      &glVertex3s,
      &glVertex3sv,
      &glVertex4d,
      &glVertex4dv,
      &glVertex4f,
      &glVertex4fv,
      &glVertex4i,
      &glVertex4iv,
      &glVertex4s,
      &glVertex4sv,
      &glClipPlane,
      &glColorMaterial,
      &glCullFace,
      &glFogf,
      &glFogfv,
      &glFogi,
      &glFogiv,
      &glFrontFace,
      &glHint,
      &glLightf,
      &glLightfv,
      &glLighti,
      &glLightiv,
      &glLightModelf,
      &glLightModelfv,
      &glLightModeli,
      &glLightModeliv,
      &glLineStipple,
      &glLineWidth,
      &glMaterialf,
      &glMaterialfv,
      &glMateriali,
      &glMaterialiv,
      &glPointSize,
      &glPolygonMode,
      &glPolygonStipple,
      &glScissor,
      &glShadeModel,
      &glTexParameterf,
      &glTexParameterfv,
      &glTexParameteri,
      &glTexParameteriv,
      &glTexImage1D,
      &glTexImage2D,
      &glTexEnvf,
      &glTexEnvfv,
      &glTexEnvi,
      &glTexEnviv,
      &glTexGend,
      &glTexGendv,
      &glTexGenf,
      &glTexGenfv,
      &glTexGeni,
      &glTexGeniv,
      &glFeedbackBuffer,
      &glSelectBuffer,
      &glRenderMode,
      &glInitNames,
      &glLoadName,
      &glPassThrough,
      &glPopName,
      &glPushName,
      &glDrawBuffer,
      &glClear,
      &glClearAccum,
      &glClearIndex,
      &glClearColor,
      &glClearStencil,
      &glClearDepth,
      &glStencilMask,
      &glColorMask,
      &glDepthMask,
      &glIndexMask,
      &glAccum,
      &glDisable,
      &glEnable,
      &glFinish,
      &glFlush,
      &glPopAttrib,
      &glPushAttrib,
      &glMap1d,
      &glMap1f,
      &glMap2d,
      &glMap2f,
      &glMapGrid1d,
      &glMapGrid1f,
      &glMapGrid2d,
      &glMapGrid2f,
      &glEvalCoord1d,
      &glEvalCoord1dv,
      &glEvalCoord1f,
      &glEvalCoord1fv,
      &glEvalCoord2d,
      &glEvalCoord2dv,
      &glEvalCoord2f,
      &glEvalCoord2fv,
      &glEvalMesh1,
      &glEvalPoint1,
      &glEvalMesh2,
      &glEvalPoint2,
      &glAlphaFunc,
      &glBlendFunc,
      &glLogicOp,
      &glStencilFunc,
      &glStencilOp,
      &glDepthFunc,
      &glPixelZoom,
      &glPixelTransferf,
      &glPixelTransferi,
      &glPixelStoref,
      &glPixelStorei,
      &glPixelMapfv,
      &glPixelMapuiv,
      &glPixelMapusv,
      &glReadBuffer,
      &glCopyPixels,
      &glReadPixels,
      &glDrawPixels,
      &glGetBooleanv,
      &glGetClipPlane,
      &glGetDoublev,
      &glGetError,
      &glGetFloatv,
      &glGetIntegerv,
      &glGetLightfv,
      &glGetLightiv,
      &glGetMapdv,
      &glGetMapfv,
      &glGetMapiv,
      &glGetMaterialfv,
      &glGetMaterialiv,
      &glGetPixelMapfv,
      &glGetPixelMapuiv,
      &glGetPixelMapusv,
      &glGetPolygonStipple,
      &glGetString,
      &glGetTexEnvfv,
      &glGetTexEnviv,
      &glGetTexGendv,
      &glGetTexGenfv,
      &glGetTexGeniv,
      &glGetTexImage,
      &glGetTexParameterfv,
      &glGetTexParameteriv,
      &glGetTexLevelParameterfv,
      &glGetTexLevelParameteriv,
      &glIsEnabled,
      &glIsList,
      &glDepthRange,
      &glFrustum,
      &glLoadIdentity,
      &glLoadMatrixf,
      &glLoadMatrixd,
      &glMatrixMode,
      &glMultMatrixf,
      &glMultMatrixd,
      &glOrtho,
      &glPopMatrix,
      &glPushMatrix,
      &glRotated,
      &glRotatef,
      &glScaled,
      &glScalef,
      &glTranslated,
      &glTranslatef,
      &glViewport,
      &glArrayElement,
      &glBindTexture,
      &glColorPointer,
      &glDisableClientState,
      &glDrawArrays,
      &glDrawElements,
      &glEdgeFlagPointer,
      &glEnableClientState,
      &glIndexPointer,
      &glIndexub,
      &glIndexubv,
      &glInterleavedArrays,
      &glNormalPointer,
      &glPolygonOffset,
      &glTexCoordPointer,
      &glVertexPointer,
      &glAreTexturesResident,
      &glCopyTexImage1D,
      &glCopyTexImage2D,
      &glCopyTexSubImage1D,
      &glCopyTexSubImage2D,
      &glDeleteTextures,
      &glGenTextures,
      &glGetPointerv,
      &glIsTexture,
      &glPrioritizeTextures,
      &glTexSubImage1D,
      &glTexSubImage2D,
      &glPopClientAttrib,
      &glPushClientAttrib
   }
};


PGLCLTPROCTABLE APIENTRY
DrvSetContext(HDC hdc, DHGLRC dhglrc, PFN_SETPROCTABLE pfnSetProcTable)
{
   PGLCLTPROCTABLE r = (PGLCLTPROCTABLE)&cpt;

   if (!stw_make_current(hdc, hdc, dhglrc))
      r = NULL;

   return r;
}
