/**************************************************************************
 *
 * Copyright 2019 VMware, Inc.
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
 * Miscellantous state tracker utility functions, macros.
 */


#ifndef ST_UTIL
#define ST_UTIL


#include "state_tracker/st_context.h"


#ifdef __cplusplus
extern "C" {
#endif


/** For drawing quads for glClear, glDraw/CopyPixels, glBitmap, etc. */
struct st_util_vertex
{
   float x, y, z;
   float r, g, b, a;
   float s, t;
};



/* Invalidate the readpixels cache to ensure we don't read stale data.
 */
static inline void
st_invalidate_readpix_cache(struct st_context *st)
{
   if (unlikely(st->readpix_cache.src)) {
      pipe_resource_reference(&st->readpix_cache.src, NULL);
      pipe_resource_reference(&st->readpix_cache.cache, NULL);
   }
}


#define Y_0_TOP 1
#define Y_0_BOTTOM 2

static inline GLuint
st_fb_orientation(const struct gl_framebuffer *fb)
{
   if (fb && fb->FlipY) {
      /* Drawing into a window (on-screen buffer).
       *
       * Negate Y scale to flip image vertically.
       * The NDC Y coords prior to viewport transformation are in the range
       * [y=-1=bottom, y=1=top]
       * Hardware window coords are in the range [y=0=top, y=H-1=bottom] where
       * H is the window height.
       * Use the viewport transformation to invert Y.
       */
      return Y_0_TOP;
   }
   else {
      /* Drawing into user-created FBO (very likely a texture).
       *
       * For textures, T=0=Bottom, so by extension Y=0=Bottom for rendering.
       */
      return Y_0_BOTTOM;
   }
}


static inline bool
st_user_clip_planes_enabled(struct gl_context *ctx)
{
   return (ctx->API == API_OPENGL_COMPAT ||
           ctx->API == API_OPENGLES) && /* only ES 1.x */
          ctx->Transform.ClipPlanesEnabled;
}

static inline bool
st_point_size_per_vertex(struct gl_context *ctx)
{
   const struct gl_program *vertProg = ctx->VertexProgram._Current;
   if (vertProg) {
      if (vertProg->Id == 0) {
         if (vertProg->info.outputs_written &
             BITFIELD64_BIT(VARYING_SLOT_PSIZ)) {
            /* generated program which emits point size */
            return true;
         }
      }
      else if (ctx->API != API_OPENGLES2) {
         /* PointSizeEnabled is always set in ES2 contexts */
         return ctx->VertexProgram.PointSizeEnabled;
      }
      else {
         /* ST_NEW_TESSEVAL_PROGRAM | ST_NEW_GEOMETRY_PROGRAM */
         /* We have to check the last bound stage and see if it writes psize */
         struct gl_program *last = NULL;
         if (ctx->GeometryProgram._Current)
            last = ctx->GeometryProgram._Current;
         else if (ctx->TessEvalProgram._Current)
            last = ctx->TessEvalProgram._Current;
         else if (ctx->VertexProgram._Current)
            last = ctx->VertexProgram._Current;
         if (last)
            return !!(last->info.outputs_written &
                      BITFIELD64_BIT(VARYING_SLOT_PSIZ));
      }
   }
   return false;
}

/** clear-alloc a struct-sized object, with casting */
#define ST_CALLOC_STRUCT(T)   (struct T *) calloc(1, sizeof(struct T))


#ifdef __cplusplus
}
#endif


#endif /* ST_UTIL */
