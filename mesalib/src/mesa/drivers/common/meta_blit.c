/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2009  VMware, Inc.  All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#include "main/glheader.h"
#include "main/mtypes.h"
#include "main/arbprogram.h"
#include "main/arrayobj.h"
#include "main/blend.h"
#include "main/depth.h"
#include "main/enable.h"
#include "main/enums.h"
#include "main/fbobject.h"
#include "main/image.h"
#include "main/macros.h"
#include "main/matrix.h"
#include "main/readpix.h"
#include "main/scissor.h"
#include "main/shaderapi.h"
#include "main/texobj.h"
#include "main/texenv.h"
#include "main/teximage.h"
#include "main/texparam.h"
#include "main/uniforms.h"
#include "main/varray.h"
#include "main/viewport.h"
#include "swrast/swrast.h"
#include "drivers/common/meta.h"

static struct gl_texture_object *
texture_object_from_renderbuffer(struct gl_context *, struct gl_renderbuffer *);

static struct gl_sampler_object *
setup_sampler(struct gl_context *, struct gl_texture_object *, GLenum target,
              GLenum filter, GLuint srcLevel);

/** Return offset in bytes of the field within a vertex struct */
#define OFFSET(FIELD) ((void *) offsetof(struct vertex, FIELD))

static void
setup_glsl_blit_framebuffer(struct gl_context *ctx,
                            struct blit_state *blit,
                            const struct gl_framebuffer *drawFb,
                            struct gl_renderbuffer *src_rb,
                            GLenum target,
                            bool do_depth)
{
   const unsigned texcoord_size = 2 + (src_rb->Depth > 1 ? 1 : 0);

   /* target = GL_TEXTURE_RECTANGLE is not supported in GLES 3.0 */
   assert(_mesa_is_desktop_gl(ctx) || target == GL_TEXTURE_2D);


   _mesa_meta_setup_vertex_objects(ctx, &blit->VAO, &blit->buf_obj, true,
                                   2, texcoord_size, 0);

   _mesa_meta_setup_blit_shader(ctx, target, do_depth,
                                do_depth ? &blit->shaders_with_depth
                                         : &blit->shaders_without_depth);
}

/**
 * Try to do a color or depth glBlitFramebuffer using texturing.
 *
 * We can do this when the src renderbuffer is actually a texture, or when the
 * driver exposes BindRenderbufferTexImage().
 */
static bool
blitframebuffer_texture(struct gl_context *ctx,
                        const struct gl_framebuffer *readFb,
                        const struct gl_framebuffer *drawFb,
                        GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                        GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                        GLenum filter, GLint flipX, GLint flipY,
                        GLboolean glsl_version, GLboolean do_depth)
{
   int att_index = do_depth ? BUFFER_DEPTH : readFb->_ColorReadBufferIndex;
   const struct gl_renderbuffer_attachment *readAtt =
      &readFb->Attachment[att_index];
   struct blit_state *blit = &ctx->Meta->Blit;
   struct fb_tex_blit_state fb_tex_blit;
   const GLint dstX = MIN2(dstX0, dstX1);
   const GLint dstY = MIN2(dstY0, dstY1);
   const GLint dstW = abs(dstX1 - dstX0);
   const GLint dstH = abs(dstY1 - dstY0);
   const int srcW = abs(srcX1 - srcX0);
   const int srcH = abs(srcY1 - srcY0);
   struct gl_texture_object *texObj;
   GLuint srcLevel;
   GLenum target;
   struct gl_renderbuffer *rb = readAtt->Renderbuffer;
   struct temp_texture *meta_temp_texture;

   assert(rb->NumSamples == 0);

   _mesa_meta_fb_tex_blit_begin(ctx, &fb_tex_blit);

   if (readAtt->Texture &&
       (readAtt->Texture->Target == GL_TEXTURE_2D ||
        readAtt->Texture->Target == GL_TEXTURE_RECTANGLE)) {
      /* If there's a texture attached of a type we can handle, then just use
       * it directly.
       */
      srcLevel = readAtt->TextureLevel;
      texObj = readAtt->Texture;
   } else if (!readAtt->Texture && ctx->Driver.BindRenderbufferTexImage) {
      texObj = texture_object_from_renderbuffer(ctx, rb);
      if (texObj == NULL)
         return false;

      fb_tex_blit.temp_tex_obj = texObj;

      srcLevel = 0;
      if (_mesa_is_winsys_fbo(readFb)) {
         GLint temp = srcY0;
         srcY0 = rb->Height - srcY1;
         srcY1 = rb->Height - temp;
         flipY = -flipY;
      }
   } else {
      GLenum tex_base_format;
      /* Fall back to doing a CopyTexSubImage to get the destination
       * renderbuffer into a texture.
       */
      if (ctx->Meta->Blit.no_ctsi_fallback)
         return false;

      if (do_depth) {
         meta_temp_texture = _mesa_meta_get_temp_depth_texture(ctx);
         tex_base_format = GL_DEPTH_COMPONENT;
      } else {
         meta_temp_texture = _mesa_meta_get_temp_texture(ctx);
         tex_base_format =
            _mesa_base_tex_format(ctx, rb->InternalFormat);
      }

      srcLevel = 0;
      texObj = meta_temp_texture->tex_obj;
      if (texObj == NULL) {
         return false;
      }

      _mesa_meta_setup_copypix_texture(ctx, meta_temp_texture,
                                       srcX0, srcY0,
                                       srcW, srcH,
                                       tex_base_format,
                                       filter);

      assert(texObj->Target == meta_temp_texture->Target);

      srcX0 = 0;
      srcY0 = 0;
      srcX1 = srcW;
      srcY1 = srcH;
   }

   target = texObj->Target;
   fb_tex_blit.tex_obj = texObj;
   fb_tex_blit.baseLevelSave = texObj->BaseLevel;
   fb_tex_blit.maxLevelSave = texObj->MaxLevel;
   fb_tex_blit.stencilSamplingSave = texObj->StencilSampling;

   if (glsl_version) {
      setup_glsl_blit_framebuffer(ctx, blit, drawFb, rb, target, do_depth);
   }
   else {
      _mesa_meta_setup_ff_tnl_for_blit(ctx,
                                       &ctx->Meta->Blit.VAO,
                                       &ctx->Meta->Blit.buf_obj,
                                       2);
   }

   /*
     printf("Blit from texture!\n");
     printf("  srcAtt %p  dstAtt %p\n", readAtt, drawAtt);
     printf("  srcTex %p  dstText %p\n", texObj, drawAtt->Texture);
   */

   fb_tex_blit.samp_obj = setup_sampler(ctx, texObj, target, filter, srcLevel);

   if (ctx->Extensions.EXT_texture_sRGB_decode) {
      /* The GL 4.4 spec, section 18.3.1 ("Blitting Pixel Rectangles") says:
       *
       *    "When values are taken from the read buffer, if FRAMEBUFFER_SRGB
       *     is enabled and the value of FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING
       *     for the framebuffer attachment corresponding to the read buffer
       *     is SRGB (see section 9.2.3), the red, green, and blue components
       *     are converted from the non-linear sRGB color space according to
       *     equation 3.24.
       *
       *     When values are written to the draw buffers, blit operations
       *     bypass most of the fragment pipeline.  The only fragment
       *     operations which affect a blit are the pixel ownership test,
       *     the scissor test, and sRGB conversion (see section 17.3.9)."
       *
       * ES 3.0 contains nearly the exact same text, but omits the part
       * about GL_FRAMEBUFFER_SRGB as that doesn't exist in ES.  Mesa
       * defaults it to on for ES contexts, so we can safely check it.
       */
      const bool decode =
         ctx->Color.sRGBEnabled && _mesa_is_format_srgb(rb->Format);

      _mesa_set_sampler_srgb_decode(ctx, fb_tex_blit.samp_obj,
                                    decode ? GL_DECODE_EXT
                                           : GL_SKIP_DECODE_EXT);
   }

   if (!glsl_version) {
      _mesa_TexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
      _mesa_set_enable(ctx, target, GL_TRUE);
   }

   /* Prepare vertex data (the VBO was previously created and bound) */
   {
      struct vertex verts[4];
      GLfloat s0, t0, s1, t1;

      if (target == GL_TEXTURE_2D) {
         const struct gl_texture_image *texImage
            = _mesa_select_tex_image(texObj, target, srcLevel);
         s0 = srcX0 / (float) texImage->Width;
         s1 = srcX1 / (float) texImage->Width;
         t0 = srcY0 / (float) texImage->Height;
         t1 = srcY1 / (float) texImage->Height;
      }
      else {
         assert(target == GL_TEXTURE_RECTANGLE_ARB);
         s0 = (float) srcX0;
         s1 = (float) srcX1;
         t0 = (float) srcY0;
         t1 = (float) srcY1;
      }

      /* Silence valgrind warnings about reading uninitialized stack. */
      memset(verts, 0, sizeof(verts));

      /* setup vertex positions */
      verts[0].x = -1.0F * flipX;
      verts[0].y = -1.0F * flipY;
      verts[1].x =  1.0F * flipX;
      verts[1].y = -1.0F * flipY;
      verts[2].x =  1.0F * flipX;
      verts[2].y =  1.0F * flipY;
      verts[3].x = -1.0F * flipX;
      verts[3].y =  1.0F * flipY;

      verts[0].tex[0] = s0;
      verts[0].tex[1] = t0;
      verts[0].tex[2] = readAtt->Zoffset;
      verts[1].tex[0] = s1;
      verts[1].tex[1] = t0;
      verts[1].tex[2] = readAtt->Zoffset;
      verts[2].tex[0] = s1;
      verts[2].tex[1] = t1;
      verts[2].tex[2] = readAtt->Zoffset;
      verts[3].tex[0] = s0;
      verts[3].tex[1] = t1;
      verts[3].tex[2] = readAtt->Zoffset;

      _mesa_buffer_sub_data(ctx, blit->buf_obj, 0, sizeof(verts), verts);
   }

   /* setup viewport */
   _mesa_set_viewport(ctx, 0, dstX, dstY, dstW, dstH);
   _mesa_ColorMask(!do_depth, !do_depth, !do_depth, !do_depth);
   _mesa_set_enable(ctx, GL_DEPTH_TEST, do_depth);
   _mesa_DepthMask(do_depth);
   _mesa_DepthFunc(GL_ALWAYS);

   _mesa_DrawArrays(GL_TRIANGLE_FAN, 0, 4);
   _mesa_meta_fb_tex_blit_end(ctx, target, &fb_tex_blit);

   return true;
}

void
_mesa_meta_fb_tex_blit_begin(struct gl_context *ctx,
                             struct fb_tex_blit_state *blit)
{
   /* None of the existing callers preinitialize fb_tex_blit_state to zeros,
    * and both use stack variables.  If samp_obj_save is not NULL,
    * _mesa_reference_sampler_object will try to dereference it.  Leaving
    * random garbage in samp_obj_save can only lead to crashes.
    *
    * Since the state isn't persistent across calls, we won't catch ref
    * counting problems.
    */
   blit->samp_obj_save = NULL;
   _mesa_reference_sampler_object(ctx, &blit->samp_obj_save,
                                  ctx->Texture.Unit[ctx->Texture.CurrentUnit].Sampler);
   blit->temp_tex_obj = NULL;
}

void
_mesa_meta_fb_tex_blit_end(struct gl_context *ctx, GLenum target,
                           struct fb_tex_blit_state *blit)
{
   struct gl_texture_object *const texObj =
      _mesa_get_current_tex_object(ctx, target);

   /* Either there is no temporary texture or the temporary texture is bound. */
   assert(blit->temp_tex_obj == NULL || blit->temp_tex_obj == texObj);

   /* Restore texture object state, the texture binding will be restored by
    * _mesa_meta_end().  If the texture is the temporary texture that is about
    * to be destroyed, don't bother restoring its state.
    */
   if (blit->temp_tex_obj == NULL) {
      /* If the target restricts values for base level or max level, we assume
       * that the original values were valid.
       */
      if (blit->baseLevelSave != texObj->BaseLevel)
         _mesa_texture_parameteriv(ctx, texObj, GL_TEXTURE_BASE_LEVEL,
                                   &blit->baseLevelSave, false);

      if (blit->maxLevelSave != texObj->MaxLevel)
         _mesa_texture_parameteriv(ctx, texObj, GL_TEXTURE_MAX_LEVEL,
                                   &blit->maxLevelSave, false);

      /* If ARB_stencil_texturing is not supported, the mode won't have changed. */
      if (texObj->StencilSampling != blit->stencilSamplingSave) {
         /* GLint so the compiler won't complain about type signedness mismatch
          * in the call to _mesa_texture_parameteriv below.
          */
         const GLint param = blit->stencilSamplingSave ?
            GL_STENCIL_INDEX : GL_DEPTH_COMPONENT;

         _mesa_texture_parameteriv(ctx, texObj, GL_DEPTH_STENCIL_TEXTURE_MODE,
                                   &param, false);
      }
   }

   _mesa_bind_sampler(ctx, ctx->Texture.CurrentUnit, blit->samp_obj_save);
   _mesa_reference_sampler_object(ctx, &blit->samp_obj_save, NULL);
   _mesa_reference_sampler_object(ctx, &blit->samp_obj, NULL);
   _mesa_delete_nameless_texture(ctx, blit->temp_tex_obj);
}

static struct gl_texture_object *
texture_object_from_renderbuffer(struct gl_context *ctx,
                                 struct gl_renderbuffer *rb)
{
   struct gl_texture_image *texImage;
   struct gl_texture_object *texObj;
   const GLenum target = GL_TEXTURE_2D;

   texObj = ctx->Driver.NewTextureObject(ctx, 0xDEADBEEF, target);
   texImage = _mesa_get_tex_image(ctx, texObj, target, 0);

   if (!ctx->Driver.BindRenderbufferTexImage(ctx, rb, texImage)) {
      _mesa_delete_nameless_texture(ctx, texObj);
      return NULL;
   }

   if (ctx->Driver.FinishRenderTexture && !rb->NeedsFinishRenderTexture) {
      rb->NeedsFinishRenderTexture = true;
      ctx->Driver.FinishRenderTexture(ctx, rb);
   }

   return texObj;
}

static struct gl_sampler_object *
setup_sampler(struct gl_context *ctx, struct gl_texture_object *texObj,
              GLenum target, GLenum filter, GLuint srcLevel)
{
   struct gl_sampler_object *samp_obj =
      ctx->Driver.NewSamplerObject(ctx, 0xDEADBEEF);

   if (samp_obj == NULL)
      return NULL;

   _mesa_bind_sampler(ctx, ctx->Texture.CurrentUnit, samp_obj);
   _mesa_set_sampler_filters(ctx, samp_obj, filter, filter);
   _mesa_set_sampler_wrap(ctx, samp_obj, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE,
                          samp_obj->WrapR);

   /* Prepare src texture state */
   _mesa_bind_texture(ctx, target, texObj);
   if (target != GL_TEXTURE_RECTANGLE_ARB) {
      _mesa_texture_parameteriv(ctx, texObj, GL_TEXTURE_BASE_LEVEL,
                                (GLint *) &srcLevel, false);
      _mesa_texture_parameteriv(ctx, texObj, GL_TEXTURE_MAX_LEVEL,
                                (GLint *) &srcLevel, false);
   }

   return samp_obj;
}

/**
 * Meta implementation of ctx->Driver.BlitFramebuffer() in terms
 * of texture mapping and polygon rendering.
 */
GLbitfield
_mesa_meta_BlitFramebuffer(struct gl_context *ctx,
                           const struct gl_framebuffer *readFb,
                           const struct gl_framebuffer *drawFb,
                           GLint srcX0, GLint srcY0, GLint srcX1, GLint srcY1,
                           GLint dstX0, GLint dstY0, GLint dstX1, GLint dstY1,
                           GLbitfield mask, GLenum filter)
{
   const GLint dstW = abs(dstX1 - dstX0);
   const GLint dstH = abs(dstY1 - dstY0);
   const GLint dstFlipX = (dstX1 - dstX0) / dstW;
   const GLint dstFlipY = (dstY1 - dstY0) / dstH;

   struct {
      GLint srcX0, srcY0, srcX1, srcY1;
      GLint dstX0, dstY0, dstX1, dstY1;
   } clip = {
      srcX0, srcY0, srcX1, srcY1,
      dstX0, dstY0, dstX1, dstY1
   };

   const GLboolean use_glsl_version = ctx->Extensions.ARB_vertex_shader &&
                                      ctx->Extensions.ARB_fragment_shader;

   /* Multisample blit is not supported. */
   if (readFb->Visual.samples > 0)
      return mask;

   /* Clip a copy of the blit coordinates. If these differ from the input
    * coordinates, then we'll set the scissor.
    */
   if (!_mesa_clip_blit(ctx, readFb, drawFb,
                        &clip.srcX0, &clip.srcY0, &clip.srcX1, &clip.srcY1,
                        &clip.dstX0, &clip.dstY0, &clip.dstX1, &clip.dstY1)) {
      /* clipped/scissored everything away */
      return 0;
   }

   /* Only scissor and FRAMEBUFFER_SRGB affect blit.  Leave sRGB alone, but
    * save restore scissor as we'll set a custom scissor if necessary.
    */
   _mesa_meta_begin(ctx, MESA_META_ALL &
                         ~(MESA_META_DRAW_BUFFERS |
                           MESA_META_FRAMEBUFFER_SRGB));

   /* Dithering shouldn't be performed for glBlitFramebuffer */
   _mesa_set_enable(ctx, GL_DITHER, GL_FALSE);

   /* If the clipping earlier changed the destination rect at all, then
    * enable the scissor to clip to it.
    */
   if (clip.dstX0 != dstX0 || clip.dstY0 != dstY0 ||
       clip.dstX1 != dstX1 || clip.dstY1 != dstY1) {
      _mesa_set_enable(ctx, GL_SCISSOR_TEST, GL_TRUE);
      _mesa_Scissor(MIN2(clip.dstX0, clip.dstX1),
                    MIN2(clip.dstY0, clip.dstY1),
                    abs(clip.dstX0 - clip.dstX1),
                    abs(clip.dstY0 - clip.dstY1));
   }

   /* Try faster, direct texture approach first */
   if (mask & GL_COLOR_BUFFER_BIT) {
      if (blitframebuffer_texture(ctx, readFb, drawFb,
                                  srcX0, srcY0, srcX1, srcY1,
                                  dstX0, dstY0, dstX1, dstY1,
                                  filter, dstFlipX, dstFlipY,
                                  use_glsl_version, false)) {
         mask &= ~GL_COLOR_BUFFER_BIT;
      }
   }

   if (mask & GL_DEPTH_BUFFER_BIT && use_glsl_version) {
      if (blitframebuffer_texture(ctx, readFb, drawFb,
                                  srcX0, srcY0, srcX1, srcY1,
                                  dstX0, dstY0, dstX1, dstY1,
                                  filter, dstFlipX, dstFlipY,
                                  use_glsl_version, true)) {
         mask &= ~GL_DEPTH_BUFFER_BIT;
      }
   }

   if (mask & GL_STENCIL_BUFFER_BIT) {
      /* XXX can't easily do stencil */
   }

   _mesa_meta_end(ctx);

   return mask;
}

void
_mesa_meta_glsl_blit_cleanup(struct gl_context *ctx, struct blit_state *blit)
{
   if (blit->VAO) {
      _mesa_DeleteVertexArrays(1, &blit->VAO);
      blit->VAO = 0;
      _mesa_reference_buffer_object(ctx, &blit->buf_obj, NULL);
   }

   _mesa_meta_blit_shader_table_cleanup(ctx, &blit->shaders_with_depth);
   _mesa_meta_blit_shader_table_cleanup(ctx, &blit->shaders_without_depth);

   if (blit->depthTex.tex_obj != NULL) {
      _mesa_delete_nameless_texture(ctx, blit->depthTex.tex_obj);
      blit->depthTex.tex_obj = NULL;
   }
}

void
_mesa_meta_and_swrast_BlitFramebuffer(struct gl_context *ctx,
                                      struct gl_framebuffer *readFb,
                                      struct gl_framebuffer *drawFb,
                                      GLint srcX0, GLint srcY0,
                                      GLint srcX1, GLint srcY1,
                                      GLint dstX0, GLint dstY0,
                                      GLint dstX1, GLint dstY1,
                                      GLbitfield mask, GLenum filter)
{
   mask = _mesa_meta_BlitFramebuffer(ctx, readFb, drawFb,
                                     srcX0, srcY0, srcX1, srcY1,
                                     dstX0, dstY0, dstX1, dstY1,
                                     mask, filter);
   if (mask == 0x0)
      return;

   _swrast_BlitFramebuffer(ctx, readFb, drawFb,
                           srcX0, srcY0, srcX1, srcY1,
                           dstX0, dstY0, dstX1, dstY1,
                           mask, filter);
}
