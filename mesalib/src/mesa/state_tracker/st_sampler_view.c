/*
 * Copyright 2016 VMware, Inc.
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
 */

#include "pipe/p_context.h"
#include "util/u_format.h"
#include "util/u_inlines.h"

#include "main/context.h"
#include "main/macros.h"
#include "main/mtypes.h"
#include "main/teximage.h"
#include "main/texobj.h"
#include "program/prog_instruction.h"

#include "st_context.h"
#include "st_sampler_view.h"
#include "st_texture.h"
#include "st_format.h"
#include "st_cb_texture.h"


/**
 * Try to find a matching sampler view for the given context.
 * If none is found an empty slot is initialized with a
 * template and returned instead.
 */
struct pipe_sampler_view **
st_texture_get_sampler_view(struct st_context *st,
                            struct st_texture_object *stObj)
{
   struct pipe_sampler_view **free = NULL;
   GLuint i;

   for (i = 0; i < stObj->num_sampler_views; ++i) {
      struct pipe_sampler_view **sv = &stObj->sampler_views[i];
      /* Is the array entry used ? */
      if (*sv) {
         /* check if the context matches */
         if ((*sv)->context == st->pipe) {
            return sv;
         }
      } else {
         /* Found a free slot, remember that */
         free = sv;
      }
   }

   /* Couldn't find a slot for our context, create a new one */

   if (!free) {
      /* Haven't even found a free one, resize the array */
      unsigned new_size = (stObj->num_sampler_views + 1) *
         sizeof(struct pipe_sampler_view *);
      stObj->sampler_views = realloc(stObj->sampler_views, new_size);
      free = &stObj->sampler_views[stObj->num_sampler_views++];
      *free = NULL;
   }

   assert(*free == NULL);

   return free;
}


/**
 * For the given texture object, release any sampler views which belong
 * to the calling context.
 */
void
st_texture_release_sampler_view(struct st_context *st,
                                struct st_texture_object *stObj)
{
   GLuint i;

   for (i = 0; i < stObj->num_sampler_views; ++i) {
      struct pipe_sampler_view **sv = &stObj->sampler_views[i];

      if (*sv && (*sv)->context == st->pipe) {
         pipe_sampler_view_reference(sv, NULL);
         break;
      }
   }
}


/**
 * Release all sampler views attached to the given texture object, regardless
 * of the context.
 */
void
st_texture_release_all_sampler_views(struct st_context *st,
                                     struct st_texture_object *stObj)
{
   GLuint i;

   /* XXX This should use sampler_views[i]->pipe, not st->pipe */
   for (i = 0; i < stObj->num_sampler_views; ++i)
      pipe_sampler_view_release(st->pipe, &stObj->sampler_views[i]);
}


void
st_texture_free_sampler_views(struct st_texture_object *stObj)
{
   free(stObj->sampler_views);
   stObj->sampler_views = NULL;
   stObj->num_sampler_views = 0;
}


/**
 * Return swizzle1(swizzle2)
 */
static unsigned
swizzle_swizzle(unsigned swizzle1, unsigned swizzle2)
{
   unsigned i, swz[4];

   if (swizzle1 == SWIZZLE_XYZW) {
      /* identity swizzle, no change to swizzle2 */
      return swizzle2;
   }

   for (i = 0; i < 4; i++) {
      unsigned s = GET_SWZ(swizzle1, i);
      switch (s) {
      case SWIZZLE_X:
      case SWIZZLE_Y:
      case SWIZZLE_Z:
      case SWIZZLE_W:
         swz[i] = GET_SWZ(swizzle2, s);
         break;
      case SWIZZLE_ZERO:
         swz[i] = SWIZZLE_ZERO;
         break;
      case SWIZZLE_ONE:
         swz[i] = SWIZZLE_ONE;
         break;
      default:
         assert(!"Bad swizzle term");
         swz[i] = SWIZZLE_X;
      }
   }

   return MAKE_SWIZZLE4(swz[0], swz[1], swz[2], swz[3]);
}


/**
 * Given a user-specified texture base format, the actual gallium texture
 * format and the current GL_DEPTH_MODE, return a texture swizzle.
 *
 * Consider the case where the user requests a GL_RGB internal texture
 * format the driver actually uses an RGBA format.  The A component should
 * be ignored and sampling from the texture should always return (r,g,b,1).
 * But if we rendered to the texture we might have written A values != 1.
 * By sampling the texture with a ".xyz1" swizzle we'll get the expected A=1.
 * This function computes the texture swizzle needed to get the expected
 * values.
 *
 * In the case of depth textures, the GL_DEPTH_MODE state determines the
 * texture swizzle.
 *
 * This result must be composed with the user-specified swizzle to get
 * the final swizzle.
 */
static unsigned
compute_texture_format_swizzle(GLenum baseFormat, GLenum depthMode,
                               enum pipe_format actualFormat,
                               unsigned glsl_version)
{
   switch (baseFormat) {
   case GL_RGBA:
      return SWIZZLE_XYZW;
   case GL_RGB:
      if (util_format_has_alpha(actualFormat))
         return MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Y, SWIZZLE_Z, SWIZZLE_ONE);
      else
         return SWIZZLE_XYZW;
   case GL_RG:
      if (util_format_get_nr_components(actualFormat) > 2)
         return MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_Y, SWIZZLE_ZERO, SWIZZLE_ONE);
      else
         return SWIZZLE_XYZW;
   case GL_RED:
      if (util_format_get_nr_components(actualFormat) > 1)
         return MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_ZERO,
                              SWIZZLE_ZERO, SWIZZLE_ONE);
      else
         return SWIZZLE_XYZW;
   case GL_ALPHA:
      if (util_format_get_nr_components(actualFormat) > 1)
         return MAKE_SWIZZLE4(SWIZZLE_ZERO, SWIZZLE_ZERO,
                              SWIZZLE_ZERO, SWIZZLE_W);
      else
         return SWIZZLE_XYZW;
   case GL_LUMINANCE:
      if (util_format_get_nr_components(actualFormat) > 1)
         return MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_X, SWIZZLE_X, SWIZZLE_ONE);
      else
         return SWIZZLE_XYZW;
   case GL_LUMINANCE_ALPHA:
      if (util_format_get_nr_components(actualFormat) > 2)
         return MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_X, SWIZZLE_X, SWIZZLE_W);
      else
         return SWIZZLE_XYZW;
   case GL_INTENSITY:
      if (util_format_get_nr_components(actualFormat) > 1)
         return SWIZZLE_XXXX;
      else
         return SWIZZLE_XYZW;
   case GL_STENCIL_INDEX:
   case GL_DEPTH_STENCIL:
   case GL_DEPTH_COMPONENT:
      /* Now examine the depth mode */
      switch (depthMode) {
      case GL_LUMINANCE:
         return MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_X, SWIZZLE_X, SWIZZLE_ONE);
      case GL_INTENSITY:
         return MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_X, SWIZZLE_X, SWIZZLE_X);
      case GL_ALPHA:
         /* The texture(sampler*Shadow) functions from GLSL 1.30 ignore
          * the depth mode and return float, while older shadow* functions
          * and ARB_fp instructions return vec4 according to the depth mode.
          *
          * The problem with the GLSL 1.30 functions is that GL_ALPHA forces
          * them to return 0, breaking them completely.
          *
          * A proper fix would increase code complexity and that's not worth
          * it for a rarely used feature such as the GL_ALPHA depth mode
          * in GL3. Therefore, change GL_ALPHA to GL_INTENSITY for all
          * shaders that use GLSL 1.30 or later.
          *
          * BTW, it's required that sampler views are updated when
          * shaders change (check_sampler_swizzle takes care of that).
          */
         if (glsl_version && glsl_version >= 130)
            return SWIZZLE_XXXX;
         else
            return MAKE_SWIZZLE4(SWIZZLE_ZERO, SWIZZLE_ZERO,
                                 SWIZZLE_ZERO, SWIZZLE_X);
      case GL_RED:
         return MAKE_SWIZZLE4(SWIZZLE_X, SWIZZLE_ZERO,
                              SWIZZLE_ZERO, SWIZZLE_ONE);
      default:
         assert(!"Unexpected depthMode");
         return SWIZZLE_XYZW;
      }
   default:
      assert(!"Unexpected baseFormat");
      return SWIZZLE_XYZW;
   }
}


static unsigned
get_texture_format_swizzle(const struct st_context *st,
                           const struct st_texture_object *stObj,
                           unsigned glsl_version)
{
   GLenum baseFormat = _mesa_texture_base_format(&stObj->base);
   unsigned tex_swizzle;

   if (baseFormat != GL_NONE) {
      GLenum depth_mode = stObj->base.DepthMode;
      /* In ES 3.0, DEPTH_TEXTURE_MODE is expected to be GL_RED for textures
       * with depth component data specified with a sized internal format.
       */
      if (_mesa_is_gles3(st->ctx) &&
          util_format_is_depth_or_stencil(stObj->pt->format)) {
         const struct gl_texture_image *firstImage =
            _mesa_base_tex_image(&stObj->base);
         if (firstImage->InternalFormat != GL_DEPTH_COMPONENT &&
             firstImage->InternalFormat != GL_DEPTH_STENCIL &&
             firstImage->InternalFormat != GL_STENCIL_INDEX)
            depth_mode = GL_RED;
      }
      tex_swizzle = compute_texture_format_swizzle(baseFormat,
                                                   depth_mode,
                                                   stObj->pt->format,
                                                   glsl_version);
   }
   else {
      tex_swizzle = SWIZZLE_XYZW;
   }

   /* Combine the texture format swizzle with user's swizzle */
   return swizzle_swizzle(stObj->base._Swizzle, tex_swizzle);
}


/**
 * Return TRUE if the texture's sampler view swizzle is not equal to
 * the texture's swizzle.
 *
 * \param stObj  the st texture object,
 */
MAYBE_UNUSED static boolean
check_sampler_swizzle(const struct st_context *st,
                      const struct st_texture_object *stObj,
		      const struct pipe_sampler_view *sv, unsigned glsl_version)
{
   unsigned swizzle = get_texture_format_swizzle(st, stObj, glsl_version);

   return ((sv->swizzle_r != GET_SWZ(swizzle, 0)) ||
           (sv->swizzle_g != GET_SWZ(swizzle, 1)) ||
           (sv->swizzle_b != GET_SWZ(swizzle, 2)) ||
           (sv->swizzle_a != GET_SWZ(swizzle, 3)));
}


static unsigned
last_level(const struct st_texture_object *stObj)
{
   unsigned ret = MIN2(stObj->base.MinLevel + stObj->base._MaxLevel,
                       stObj->pt->last_level);
   if (stObj->base.Immutable)
      ret = MIN2(ret, stObj->base.MinLevel + stObj->base.NumLevels - 1);
   return ret;
}


static unsigned
last_layer(const struct st_texture_object *stObj)
{
   if (stObj->base.Immutable && stObj->pt->array_size > 1)
      return MIN2(stObj->base.MinLayer + stObj->base.NumLayers - 1,
                  stObj->pt->array_size - 1);
   return stObj->pt->array_size - 1;
}


/**
 * Determine the format for the texture sampler view.
 */
static enum pipe_format
get_sampler_view_format(struct st_context *st,
                        const struct st_texture_object *stObj,
                        const struct gl_sampler_object *samp)
{
   enum pipe_format format;

   if (stObj->base.Target == GL_TEXTURE_BUFFER) {
      format =
         st_mesa_format_to_pipe_format(st, stObj->base._BufferObjectFormat);
   }
   else {
      format =
         stObj->surface_based ? stObj->surface_format : stObj->pt->format;

      if (util_format_is_depth_and_stencil(format)) {
         if (stObj->base.StencilSampling) {
            format = util_format_stencil_only(format);
         }
         else {
            GLenum baseFormat = _mesa_texture_base_format(&stObj->base);
            if (baseFormat == GL_STENCIL_INDEX) {
               format = util_format_stencil_only(format);
            }
         }
      }
      else {
         /* If sRGB decoding is off, use the linear format */
         if (samp->sRGBDecode == GL_SKIP_DECODE_EXT) {
            format = util_format_linear(format);
         }

         /* Use R8_UNORM for video formats */
         switch (format) {
         case PIPE_FORMAT_NV12:
         case PIPE_FORMAT_IYUV:
            format = PIPE_FORMAT_R8_UNORM;
            break;
         default:
            break;
         }
      }
   }

   return format;
}


static struct pipe_sampler_view *
st_create_texture_sampler_view_from_stobj(struct st_context *st,
					  struct st_texture_object *stObj,
					  enum pipe_format format,
                                          unsigned glsl_version)
{
   struct pipe_sampler_view templ;
   unsigned swizzle = get_texture_format_swizzle(st, stObj, glsl_version);

   u_sampler_view_default_template(&templ, stObj->pt, format);

   if (stObj->pt->target == PIPE_BUFFER) {
      unsigned base, size;

      base = stObj->base.BufferOffset;
      if (base >= stObj->pt->width0)
         return NULL;
      size = MIN2(stObj->pt->width0 - base, (unsigned)stObj->base.BufferSize);
      if (!size)
         return NULL;

      templ.u.buf.offset = base;
      templ.u.buf.size = size;
   } else {
      templ.u.tex.first_level = stObj->base.MinLevel + stObj->base.BaseLevel;
      templ.u.tex.last_level = last_level(stObj);
      assert(templ.u.tex.first_level <= templ.u.tex.last_level);
      if (stObj->layer_override) {
         templ.u.tex.first_layer = templ.u.tex.last_layer = stObj->layer_override;
      } else {
         templ.u.tex.first_layer = stObj->base.MinLayer;
         templ.u.tex.last_layer = last_layer(stObj);
      }
      assert(templ.u.tex.first_layer <= templ.u.tex.last_layer);
      templ.target = gl_target_to_pipe(stObj->base.Target);
   }

   templ.swizzle_r = GET_SWZ(swizzle, 0);
   templ.swizzle_g = GET_SWZ(swizzle, 1);
   templ.swizzle_b = GET_SWZ(swizzle, 2);
   templ.swizzle_a = GET_SWZ(swizzle, 3);

   return st->pipe->create_sampler_view(st->pipe, stObj->pt, &templ);
}


struct pipe_sampler_view *
st_get_texture_sampler_view_from_stobj(struct st_context *st,
                                       struct st_texture_object *stObj,
                                       const struct gl_sampler_object *samp,
                                       unsigned glsl_version)
{
   struct pipe_sampler_view **sv;

   if (!stObj || !stObj->pt) {
      return NULL;
   }

   sv = st_texture_get_sampler_view(st, stObj);

   if (*sv) {
      /* Debug check: make sure that the sampler view's parameters are
       * what they're supposed to be.
       */
      MAYBE_UNUSED struct pipe_sampler_view *view = *sv;
      assert(!check_sampler_swizzle(st, stObj, view, glsl_version));
      assert(get_sampler_view_format(st, stObj, samp) == view->format);
      assert(gl_target_to_pipe(stObj->base.Target) == view->target);
      if (stObj->base.Target == GL_TEXTURE_BUFFER) {
         unsigned base = stObj->base.BufferOffset;
         MAYBE_UNUSED unsigned size = MIN2(stObj->pt->width0 - base,
                              (unsigned) stObj->base.BufferSize);
         assert(view->u.buf.offset == base);
         assert(view->u.buf.size == size);
      }
      else {
         assert(stObj->base.MinLevel + stObj->base.BaseLevel ==
                view->u.tex.first_level);
         assert(last_level(stObj) == view->u.tex.last_level);
         assert(stObj->layer_override || stObj->base.MinLayer == view->u.tex.first_layer);
         assert(stObj->layer_override || last_layer(stObj) == view->u.tex.last_layer);
         assert(!stObj->layer_override ||
                (stObj->layer_override == view->u.tex.first_layer &&
                 stObj->layer_override == view->u.tex.last_layer));
      }
   }
   else {
      /* create new sampler view */
      enum pipe_format format = get_sampler_view_format(st, stObj, samp);

      *sv = st_create_texture_sampler_view_from_stobj(st, stObj,
                                                      format, glsl_version);

   }

   return *sv;
}
