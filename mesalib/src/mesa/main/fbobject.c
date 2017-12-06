/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2008  Brian Paul   All Rights Reserved.
 * Copyright (C) 1999-2009  VMware, Inc.  All Rights Reserved.
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


/*
 * GL_EXT/ARB_framebuffer_object extensions
 *
 * Authors:
 *   Brian Paul
 */

#include <stdbool.h>

#include "buffers.h"
#include "context.h"
#include "enums.h"
#include "fbobject.h"
#include "formats.h"
#include "framebuffer.h"
#include "glformats.h"
#include "hash.h"
#include "macros.h"
#include "multisample.h"
#include "mtypes.h"
#include "renderbuffer.h"
#include "state.h"
#include "teximage.h"
#include "texobj.h"


/**
 * Notes:
 *
 * None of the GL_EXT_framebuffer_object functions are compiled into
 * display lists.
 */



/*
 * When glGenRender/FramebuffersEXT() is called we insert pointers to
 * these placeholder objects into the hash table.
 * Later, when the object ID is first bound, we replace the placeholder
 * with the real frame/renderbuffer.
 */
static struct gl_framebuffer DummyFramebuffer;
static struct gl_renderbuffer DummyRenderbuffer;

/* We bind this framebuffer when applications pass a NULL
 * drawable/surface in make current. */
static struct gl_framebuffer IncompleteFramebuffer;


static void
delete_dummy_renderbuffer(struct gl_context *ctx, struct gl_renderbuffer *rb)
{
   /* no op */
}

static void
delete_dummy_framebuffer(struct gl_framebuffer *fb)
{
   /* no op */
}


void
_mesa_init_fbobjects(struct gl_context *ctx)
{
   simple_mtx_init(&DummyFramebuffer.Mutex, mtx_plain);
   simple_mtx_init(&DummyRenderbuffer.Mutex, mtx_plain);
   simple_mtx_init(&IncompleteFramebuffer.Mutex, mtx_plain);
   DummyFramebuffer.Delete = delete_dummy_framebuffer;
   DummyRenderbuffer.Delete = delete_dummy_renderbuffer;
   IncompleteFramebuffer.Delete = delete_dummy_framebuffer;
}

struct gl_framebuffer *
_mesa_get_incomplete_framebuffer(void)
{
   return &IncompleteFramebuffer;
}

/**
 * Helper routine for getting a gl_renderbuffer.
 */
struct gl_renderbuffer *
_mesa_lookup_renderbuffer(struct gl_context *ctx, GLuint id)
{
   struct gl_renderbuffer *rb;

   if (id == 0)
      return NULL;

   rb = (struct gl_renderbuffer *)
      _mesa_HashLookup(ctx->Shared->RenderBuffers, id);
   return rb;
}


/**
 * A convenience function for direct state access that throws
 * GL_INVALID_OPERATION if the renderbuffer doesn't exist.
 */
struct gl_renderbuffer *
_mesa_lookup_renderbuffer_err(struct gl_context *ctx, GLuint id,
                              const char *func)
{
   struct gl_renderbuffer *rb;

   rb = _mesa_lookup_renderbuffer(ctx, id);
   if (!rb || rb == &DummyRenderbuffer) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(non-existent renderbuffer %u)", func, id);
      return NULL;
   }

   return rb;
}


/**
 * Helper routine for getting a gl_framebuffer.
 */
struct gl_framebuffer *
_mesa_lookup_framebuffer(struct gl_context *ctx, GLuint id)
{
   struct gl_framebuffer *fb;

   if (id == 0)
      return NULL;

   fb = (struct gl_framebuffer *)
      _mesa_HashLookup(ctx->Shared->FrameBuffers, id);
   return fb;
}


/**
 * A convenience function for direct state access that throws
 * GL_INVALID_OPERATION if the framebuffer doesn't exist.
 */
struct gl_framebuffer *
_mesa_lookup_framebuffer_err(struct gl_context *ctx, GLuint id,
                             const char *func)
{
   struct gl_framebuffer *fb;

   fb = _mesa_lookup_framebuffer(ctx, id);
   if (!fb || fb == &DummyFramebuffer) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(non-existent framebuffer %u)", func, id);
      return NULL;
   }

   return fb;
}


/**
 * Mark the given framebuffer as invalid.  This will force the
 * test for framebuffer completeness to be done before the framebuffer
 * is used.
 */
static void
invalidate_framebuffer(struct gl_framebuffer *fb)
{
   fb->_Status = 0; /* "indeterminate" */
}


/**
 * Return the gl_framebuffer object which corresponds to the given
 * framebuffer target, such as GL_DRAW_FRAMEBUFFER.
 * Check support for GL_EXT_framebuffer_blit to determine if certain
 * targets are legal.
 * \return gl_framebuffer pointer or NULL if target is illegal
 */
static struct gl_framebuffer *
get_framebuffer_target(struct gl_context *ctx, GLenum target)
{
   bool have_fb_blit = _mesa_is_gles3(ctx) || _mesa_is_desktop_gl(ctx);
   switch (target) {
   case GL_DRAW_FRAMEBUFFER:
      return have_fb_blit ? ctx->DrawBuffer : NULL;
   case GL_READ_FRAMEBUFFER:
      return have_fb_blit ? ctx->ReadBuffer : NULL;
   case GL_FRAMEBUFFER_EXT:
      return ctx->DrawBuffer;
   default:
      return NULL;
   }
}


/**
 * Given a GL_*_ATTACHMENTn token, return a pointer to the corresponding
 * gl_renderbuffer_attachment object.
 * This function is only used for user-created FB objects, not the
 * default / window-system FB object.
 * If \p attachment is GL_DEPTH_STENCIL_ATTACHMENT, return a pointer to
 * the depth buffer attachment point.
 * Returns if the attachment is a GL_COLOR_ATTACHMENTm_EXT on
 * is_color_attachment, because several callers would return different errors
 * if they don't find the attachment.
 */
static struct gl_renderbuffer_attachment *
get_attachment(struct gl_context *ctx, struct gl_framebuffer *fb,
               GLenum attachment, bool *is_color_attachment)
{
   GLuint i;

   assert(_mesa_is_user_fbo(fb));

   if (is_color_attachment)
      *is_color_attachment = false;

   switch (attachment) {
   case GL_COLOR_ATTACHMENT0_EXT:
   case GL_COLOR_ATTACHMENT1_EXT:
   case GL_COLOR_ATTACHMENT2_EXT:
   case GL_COLOR_ATTACHMENT3_EXT:
   case GL_COLOR_ATTACHMENT4_EXT:
   case GL_COLOR_ATTACHMENT5_EXT:
   case GL_COLOR_ATTACHMENT6_EXT:
   case GL_COLOR_ATTACHMENT7_EXT:
   case GL_COLOR_ATTACHMENT8_EXT:
   case GL_COLOR_ATTACHMENT9_EXT:
   case GL_COLOR_ATTACHMENT10_EXT:
   case GL_COLOR_ATTACHMENT11_EXT:
   case GL_COLOR_ATTACHMENT12_EXT:
   case GL_COLOR_ATTACHMENT13_EXT:
   case GL_COLOR_ATTACHMENT14_EXT:
   case GL_COLOR_ATTACHMENT15_EXT:
      if (is_color_attachment)
         *is_color_attachment = true;
      /* Only OpenGL ES 1.x forbids color attachments other than
       * GL_COLOR_ATTACHMENT0.  For all other APIs the limit set by the
       * hardware is used.
       */
      i = attachment - GL_COLOR_ATTACHMENT0_EXT;
      if (i >= ctx->Const.MaxColorAttachments
          || (i > 0 && ctx->API == API_OPENGLES)) {
         return NULL;
      }
      return &fb->Attachment[BUFFER_COLOR0 + i];
   case GL_DEPTH_STENCIL_ATTACHMENT:
      if (!_mesa_is_desktop_gl(ctx) && !_mesa_is_gles3(ctx))
         return NULL;
      /* fall-through */
   case GL_DEPTH_ATTACHMENT_EXT:
      return &fb->Attachment[BUFFER_DEPTH];
   case GL_STENCIL_ATTACHMENT_EXT:
      return &fb->Attachment[BUFFER_STENCIL];
   default:
      return NULL;
   }
}


/**
 * As above, but only used for getting attachments of the default /
 * window-system framebuffer (not user-created framebuffer objects).
 */
static struct gl_renderbuffer_attachment *
get_fb0_attachment(struct gl_context *ctx, struct gl_framebuffer *fb,
                   GLenum attachment)
{
   assert(_mesa_is_winsys_fbo(fb));

   if (_mesa_is_gles3(ctx)) {
      assert(attachment == GL_BACK ||
             attachment == GL_DEPTH ||
             attachment == GL_STENCIL);
      switch (attachment) {
      case GL_BACK:
         /* Since there is no stereo rendering in ES 3.0, only return the
          * LEFT bits.
          */
         if (ctx->DrawBuffer->Visual.doubleBufferMode)
            return &fb->Attachment[BUFFER_BACK_LEFT];
         return &fb->Attachment[BUFFER_FRONT_LEFT];
      case GL_DEPTH:
         return &fb->Attachment[BUFFER_DEPTH];
      case GL_STENCIL:
         return &fb->Attachment[BUFFER_STENCIL];
      }
   }

   switch (attachment) {
   case GL_FRONT_LEFT:
      /* Front buffers can be allocated on the first use, but
       * glGetFramebufferAttachmentParameteriv must work even if that
       * allocation hasn't happened yet. In such case, use the back buffer,
       * which should be the same.
       */
      if (fb->Attachment[BUFFER_FRONT_LEFT].Type == GL_NONE)
         return &fb->Attachment[BUFFER_BACK_LEFT];
      else
         return &fb->Attachment[BUFFER_FRONT_LEFT];
   case GL_FRONT_RIGHT:
      /* Same as above. */
      if (fb->Attachment[BUFFER_FRONT_RIGHT].Type == GL_NONE)
         return &fb->Attachment[BUFFER_BACK_RIGHT];
      else
         return &fb->Attachment[BUFFER_FRONT_RIGHT];
   case GL_BACK_LEFT:
      return &fb->Attachment[BUFFER_BACK_LEFT];
   case GL_BACK_RIGHT:
      return &fb->Attachment[BUFFER_BACK_RIGHT];
   case GL_BACK:
      /* The ARB_ES3_1_compatibility spec says:
       *
       *    "Since this command can only query a single framebuffer
       *     attachment, BACK is equivalent to BACK_LEFT."
       */
      if (ctx->Extensions.ARB_ES3_1_compatibility)
         return &fb->Attachment[BUFFER_BACK_LEFT];
      return NULL;
   case GL_AUX0:
      if (fb->Visual.numAuxBuffers == 1) {
         return &fb->Attachment[BUFFER_AUX0];
      }
      return NULL;

   /* Page 336 (page 352 of the PDF) of the OpenGL 3.0 spec says:
    *
    *     "If the default framebuffer is bound to target, then attachment must
    *     be one of FRONT LEFT, FRONT RIGHT, BACK LEFT, BACK RIGHT, or AUXi,
    *     identifying a color buffer; DEPTH, identifying the depth buffer; or
    *     STENCIL, identifying the stencil buffer."
    *
    * Revision #34 of the ARB_framebuffer_object spec has essentially the same
    * language.  However, revision #33 of the ARB_framebuffer_object spec
    * says:
    *
    *     "If the default framebuffer is bound to <target>, then <attachment>
    *     must be one of FRONT_LEFT, FRONT_RIGHT, BACK_LEFT, BACK_RIGHT, AUXi,
    *     DEPTH_BUFFER, or STENCIL_BUFFER, identifying a color buffer, the
    *     depth buffer, or the stencil buffer, and <pname> may be
    *     FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE or
    *     FRAMEBUFFER_ATTACHMENT_OBJECT_NAME."
    *
    * The enum values for DEPTH_BUFFER and STENCIL_BUFFER have been removed
    * from glext.h, so shipping apps should not use those values.
    *
    * Note that neither EXT_framebuffer_object nor OES_framebuffer_object
    * support queries of the window system FBO.
    */
   case GL_DEPTH:
      return &fb->Attachment[BUFFER_DEPTH];
   case GL_STENCIL:
      return &fb->Attachment[BUFFER_STENCIL];
   default:
      return NULL;
   }
}



/**
 * Remove any texture or renderbuffer attached to the given attachment
 * point.  Update reference counts, etc.
 */
static void
remove_attachment(struct gl_context *ctx,
                  struct gl_renderbuffer_attachment *att)
{
   struct gl_renderbuffer *rb = att->Renderbuffer;

   /* tell driver that we're done rendering to this texture. */
   if (rb && rb->NeedsFinishRenderTexture)
      ctx->Driver.FinishRenderTexture(ctx, rb);

   if (att->Type == GL_TEXTURE) {
      assert(att->Texture);
      _mesa_reference_texobj(&att->Texture, NULL); /* unbind */
      assert(!att->Texture);
   }
   if (att->Type == GL_TEXTURE || att->Type == GL_RENDERBUFFER_EXT) {
      assert(!att->Texture);
      _mesa_reference_renderbuffer(&att->Renderbuffer, NULL); /* unbind */
      assert(!att->Renderbuffer);
   }
   att->Type = GL_NONE;
   att->Complete = GL_TRUE;
}

/**
 * Verify a couple error conditions that will lead to an incomplete FBO and
 * may cause problems for the driver's RenderTexture path.
 */
static bool
driver_RenderTexture_is_safe(const struct gl_renderbuffer_attachment *att)
{
   const struct gl_texture_image *const texImage =
      att->Texture->Image[att->CubeMapFace][att->TextureLevel];

   if (!texImage ||
       texImage->Width == 0 || texImage->Height == 0 || texImage->Depth == 0)
      return false;

   if ((texImage->TexObject->Target == GL_TEXTURE_1D_ARRAY
        && att->Zoffset >= texImage->Height)
       || (texImage->TexObject->Target != GL_TEXTURE_1D_ARRAY
           && att->Zoffset >= texImage->Depth))
      return false;

   return true;
}

/**
 * Create a renderbuffer which will be set up by the driver to wrap the
 * texture image slice.
 *
 * By using a gl_renderbuffer (like user-allocated renderbuffers), drivers get
 * to share most of their framebuffer rendering code between winsys,
 * renderbuffer, and texture attachments.
 *
 * The allocated renderbuffer uses a non-zero Name so that drivers can check
 * it for determining vertical orientation, but we use ~0 to make it fairly
 * unambiguous with actual user (non-texture) renderbuffers.
 */
void
_mesa_update_texture_renderbuffer(struct gl_context *ctx,
                                  struct gl_framebuffer *fb,
                                  struct gl_renderbuffer_attachment *att)
{
   struct gl_texture_image *texImage;
   struct gl_renderbuffer *rb;

   texImage = att->Texture->Image[att->CubeMapFace][att->TextureLevel];

   rb = att->Renderbuffer;
   if (!rb) {
      rb = ctx->Driver.NewRenderbuffer(ctx, ~0);
      if (!rb) {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "glFramebufferTexture()");
         return;
      }
      att->Renderbuffer = rb;

      /* This can't get called on a texture renderbuffer, so set it to NULL
       * for clarity compared to user renderbuffers.
       */
      rb->AllocStorage = NULL;

      rb->NeedsFinishRenderTexture = ctx->Driver.FinishRenderTexture != NULL;
   }

   if (!texImage)
      return;

   rb->_BaseFormat = texImage->_BaseFormat;
   rb->Format = texImage->TexFormat;
   rb->InternalFormat = texImage->InternalFormat;
   rb->Width = texImage->Width2;
   rb->Height = texImage->Height2;
   rb->Depth = texImage->Depth2;
   rb->NumSamples = texImage->NumSamples;
   rb->TexImage = texImage;

   if (driver_RenderTexture_is_safe(att))
      ctx->Driver.RenderTexture(ctx, fb, att);
}

/**
 * Bind a texture object to an attachment point.
 * The previous binding, if any, will be removed first.
 */
static void
set_texture_attachment(struct gl_context *ctx,
                       struct gl_framebuffer *fb,
                       struct gl_renderbuffer_attachment *att,
                       struct gl_texture_object *texObj,
                       GLenum texTarget, GLuint level, GLuint layer,
                       GLboolean layered)
{
   struct gl_renderbuffer *rb = att->Renderbuffer;

   if (rb && rb->NeedsFinishRenderTexture)
      ctx->Driver.FinishRenderTexture(ctx, rb);

   if (att->Texture == texObj) {
      /* re-attaching same texture */
      assert(att->Type == GL_TEXTURE);
   }
   else {
      /* new attachment */
      remove_attachment(ctx, att);
      att->Type = GL_TEXTURE;
      assert(!att->Texture);
      _mesa_reference_texobj(&att->Texture, texObj);
   }
   invalidate_framebuffer(fb);

   /* always update these fields */
   att->TextureLevel = level;
   att->CubeMapFace = _mesa_tex_target_to_face(texTarget);
   att->Zoffset = layer;
   att->Layered = layered;
   att->Complete = GL_FALSE;

   _mesa_update_texture_renderbuffer(ctx, fb, att);
}


/**
 * Bind a renderbuffer to an attachment point.
 * The previous binding, if any, will be removed first.
 */
static void
set_renderbuffer_attachment(struct gl_context *ctx,
                            struct gl_renderbuffer_attachment *att,
                            struct gl_renderbuffer *rb)
{
   /* XXX check if re-doing same attachment, exit early */
   remove_attachment(ctx, att);
   att->Type = GL_RENDERBUFFER_EXT;
   att->Texture = NULL; /* just to be safe */
   att->Layered = GL_FALSE;
   att->Complete = GL_FALSE;
   _mesa_reference_renderbuffer(&att->Renderbuffer, rb);
}


/**
 * Fallback for ctx->Driver.FramebufferRenderbuffer()
 * Attach a renderbuffer object to a framebuffer object.
 */
void
_mesa_FramebufferRenderbuffer_sw(struct gl_context *ctx,
                                 struct gl_framebuffer *fb,
                                 GLenum attachment,
                                 struct gl_renderbuffer *rb)
{
   struct gl_renderbuffer_attachment *att;

   simple_mtx_lock(&fb->Mutex);

   att = get_attachment(ctx, fb, attachment, NULL);
   assert(att);
   if (rb) {
      set_renderbuffer_attachment(ctx, att, rb);
      if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
         /* do stencil attachment here (depth already done above) */
         att = get_attachment(ctx, fb, GL_STENCIL_ATTACHMENT_EXT, NULL);
         assert(att);
         set_renderbuffer_attachment(ctx, att, rb);
      }
      rb->AttachedAnytime = GL_TRUE;
   }
   else {
      remove_attachment(ctx, att);
      if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
         /* detach stencil (depth was detached above) */
         att = get_attachment(ctx, fb, GL_STENCIL_ATTACHMENT_EXT, NULL);
         assert(att);
         remove_attachment(ctx, att);
      }
   }

   invalidate_framebuffer(fb);

   simple_mtx_unlock(&fb->Mutex);
}


/**
 * Fallback for ctx->Driver.ValidateFramebuffer()
 * Check if the renderbuffer's formats are supported by the software
 * renderer.
 * Drivers should probably override this.
 */
void
_mesa_validate_framebuffer(struct gl_context *ctx, struct gl_framebuffer *fb)
{
   gl_buffer_index buf;
   for (buf = 0; buf < BUFFER_COUNT; buf++) {
      const struct gl_renderbuffer *rb = fb->Attachment[buf].Renderbuffer;
      if (rb) {
         switch (rb->_BaseFormat) {
         case GL_ALPHA:
         case GL_LUMINANCE_ALPHA:
         case GL_LUMINANCE:
         case GL_INTENSITY:
         case GL_RED:
         case GL_RG:
            fb->_Status = GL_FRAMEBUFFER_UNSUPPORTED;
            return;

         default:
            switch (rb->Format) {
            /* XXX This list is likely incomplete. */
            case MESA_FORMAT_R9G9B9E5_FLOAT:
               fb->_Status = GL_FRAMEBUFFER_UNSUPPORTED;
               return;
            default:;
               /* render buffer format is supported by software rendering */
            }
         }
      }
   }
}


/**
 * Return true if the framebuffer has a combined depth/stencil
 * renderbuffer attached.
 */
GLboolean
_mesa_has_depthstencil_combined(const struct gl_framebuffer *fb)
{
   const struct gl_renderbuffer_attachment *depth =
         &fb->Attachment[BUFFER_DEPTH];
   const struct gl_renderbuffer_attachment *stencil =
         &fb->Attachment[BUFFER_STENCIL];

   if (depth->Type == stencil->Type) {
      if (depth->Type == GL_RENDERBUFFER_EXT &&
          depth->Renderbuffer == stencil->Renderbuffer)
         return GL_TRUE;

      if (depth->Type == GL_TEXTURE &&
          depth->Texture == stencil->Texture)
         return GL_TRUE;
   }

   return GL_FALSE;
}


/**
 * For debug only.
 */
static void
att_incomplete(const char *msg)
{
   if (MESA_DEBUG_FLAGS & DEBUG_INCOMPLETE_FBO) {
      _mesa_debug(NULL, "attachment incomplete: %s\n", msg);
   }
}


/**
 * For debug only.
 */
static void
fbo_incomplete(struct gl_context *ctx, const char *msg, int index)
{
   static GLuint msg_id;

   _mesa_gl_debug(ctx, &msg_id,
                  MESA_DEBUG_SOURCE_API,
                  MESA_DEBUG_TYPE_OTHER,
                  MESA_DEBUG_SEVERITY_MEDIUM,
                  "FBO incomplete: %s [%d]\n", msg, index);

   if (MESA_DEBUG_FLAGS & DEBUG_INCOMPLETE_FBO) {
      _mesa_debug(NULL, "FBO Incomplete: %s [%d]\n", msg, index);
   }
}


/**
 * Is the given base format a legal format for a color renderbuffer?
 */
GLboolean
_mesa_is_legal_color_format(const struct gl_context *ctx, GLenum baseFormat)
{
   switch (baseFormat) {
   case GL_RGB:
   case GL_RGBA:
      return GL_TRUE;
   case GL_LUMINANCE:
   case GL_LUMINANCE_ALPHA:
   case GL_INTENSITY:
   case GL_ALPHA:
      return ctx->API == API_OPENGL_COMPAT &&
             ctx->Extensions.ARB_framebuffer_object;
   case GL_RED:
   case GL_RG:
      return ctx->Extensions.ARB_texture_rg;
   default:
      return GL_FALSE;
   }
}


/**
 * Is the given base format a legal format for a color renderbuffer?
 */
static GLboolean
is_format_color_renderable(const struct gl_context *ctx, mesa_format format,
                           GLenum internalFormat)
{
   const GLenum baseFormat =
      _mesa_get_format_base_format(format);
   GLboolean valid;

   valid = _mesa_is_legal_color_format(ctx, baseFormat);
   if (!valid || _mesa_is_desktop_gl(ctx)) {
      return valid;
   }

   /* Reject additional cases for GLES */
   switch (internalFormat) {
   case GL_RGBA8_SNORM:
   case GL_RGB32F:
   case GL_RGB32I:
   case GL_RGB32UI:
   case GL_RGB16F:
   case GL_RGB16I:
   case GL_RGB16UI:
   case GL_RGB8_SNORM:
   case GL_RGB8I:
   case GL_RGB8UI:
   case GL_SRGB8:
   case GL_RGB10:
   case GL_RGB9_E5:
   case GL_RG8_SNORM:
   case GL_R8_SNORM:
      return GL_FALSE;
   default:
      break;
   }

   if (internalFormat != GL_RGB10_A2 &&
       (format == MESA_FORMAT_B10G10R10A2_UNORM ||
        format == MESA_FORMAT_B10G10R10X2_UNORM ||
        format == MESA_FORMAT_R10G10B10A2_UNORM ||
        format == MESA_FORMAT_R10G10B10X2_UNORM)) {
      return GL_FALSE;
   }

   return GL_TRUE;
}


/**
 * Is the given base format a legal format for a depth/stencil renderbuffer?
 */
static GLboolean
is_legal_depth_format(const struct gl_context *ctx, GLenum baseFormat)
{
   switch (baseFormat) {
   case GL_DEPTH_COMPONENT:
   case GL_DEPTH_STENCIL_EXT:
      return GL_TRUE;
   default:
      return GL_FALSE;
   }
}


/**
 * Test if an attachment point is complete and update its Complete field.
 * \param format if GL_COLOR, this is a color attachment point,
 *               if GL_DEPTH, this is a depth component attachment point,
 *               if GL_STENCIL, this is a stencil component attachment point.
 */
static void
test_attachment_completeness(const struct gl_context *ctx, GLenum format,
                             struct gl_renderbuffer_attachment *att)
{
   assert(format == GL_COLOR || format == GL_DEPTH || format == GL_STENCIL);

   /* assume complete */
   att->Complete = GL_TRUE;

   /* Look for reasons why the attachment might be incomplete */
   if (att->Type == GL_TEXTURE) {
      const struct gl_texture_object *texObj = att->Texture;
      const struct gl_texture_image *texImage;
      GLenum baseFormat;

      if (!texObj) {
         att_incomplete("no texobj");
         att->Complete = GL_FALSE;
         return;
      }

      texImage = texObj->Image[att->CubeMapFace][att->TextureLevel];
      if (!texImage) {
         att_incomplete("no teximage");
         att->Complete = GL_FALSE;
         return;
      }
      if (texImage->Width < 1 || texImage->Height < 1) {
         att_incomplete("teximage width/height=0");
         att->Complete = GL_FALSE;
         return;
      }

      switch (texObj->Target) {
      case GL_TEXTURE_3D:
         if (att->Zoffset >= texImage->Depth) {
            att_incomplete("bad z offset");
            att->Complete = GL_FALSE;
            return;
         }
         break;
      case GL_TEXTURE_1D_ARRAY:
         if (att->Zoffset >= texImage->Height) {
            att_incomplete("bad 1D-array layer");
            att->Complete = GL_FALSE;
            return;
         }
         break;
      case GL_TEXTURE_2D_ARRAY:
         if (att->Zoffset >= texImage->Depth) {
            att_incomplete("bad 2D-array layer");
            att->Complete = GL_FALSE;
            return;
         }
         break;
      case GL_TEXTURE_CUBE_MAP_ARRAY:
         if (att->Zoffset >= texImage->Depth) {
            att_incomplete("bad cube-array layer");
            att->Complete = GL_FALSE;
            return;
         }
         break;
      }

      baseFormat = texImage->_BaseFormat;

      if (format == GL_COLOR) {
         if (!_mesa_is_legal_color_format(ctx, baseFormat)) {
            att_incomplete("bad format");
            att->Complete = GL_FALSE;
            return;
         }
         if (_mesa_is_format_compressed(texImage->TexFormat)) {
            att_incomplete("compressed internalformat");
            att->Complete = GL_FALSE;
            return;
         }

         /* OES_texture_float allows creation and use of floating point
          * textures with GL_FLOAT, GL_HALF_FLOAT but it does not allow
          * these textures to be used as a render target, this is done via
          * GL_EXT_color_buffer(_half)_float with set of new sized types.
          */
         if (_mesa_is_gles(ctx) && (texObj->_IsFloat || texObj->_IsHalfFloat)) {
            att_incomplete("bad internal format");
            att->Complete = GL_FALSE;
            return;
         }
      }
      else if (format == GL_DEPTH) {
         if (baseFormat == GL_DEPTH_COMPONENT) {
            /* OK */
         }
         else if (ctx->Extensions.ARB_depth_texture &&
                  baseFormat == GL_DEPTH_STENCIL) {
            /* OK */
         }
         else {
            att->Complete = GL_FALSE;
            att_incomplete("bad depth format");
            return;
         }
      }
      else {
         assert(format == GL_STENCIL);
         if (ctx->Extensions.ARB_depth_texture &&
             baseFormat == GL_DEPTH_STENCIL) {
            /* OK */
         } else if (ctx->Extensions.ARB_texture_stencil8 &&
                    baseFormat == GL_STENCIL_INDEX) {
            /* OK */
         } else {
            /* no such thing as stencil-only textures */
            att_incomplete("illegal stencil texture");
            att->Complete = GL_FALSE;
            return;
         }
      }
   }
   else if (att->Type == GL_RENDERBUFFER_EXT) {
      const GLenum baseFormat = att->Renderbuffer->_BaseFormat;

      assert(att->Renderbuffer);
      if (!att->Renderbuffer->InternalFormat ||
          att->Renderbuffer->Width < 1 ||
          att->Renderbuffer->Height < 1) {
         att_incomplete("0x0 renderbuffer");
         att->Complete = GL_FALSE;
         return;
      }
      if (format == GL_COLOR) {
         if (!_mesa_is_legal_color_format(ctx, baseFormat)) {
            att_incomplete("bad renderbuffer color format");
            att->Complete = GL_FALSE;
            return;
         }
      }
      else if (format == GL_DEPTH) {
         if (baseFormat == GL_DEPTH_COMPONENT) {
            /* OK */
         }
         else if (baseFormat == GL_DEPTH_STENCIL) {
            /* OK */
         }
         else {
            att_incomplete("bad renderbuffer depth format");
            att->Complete = GL_FALSE;
            return;
         }
      }
      else {
         assert(format == GL_STENCIL);
         if (baseFormat == GL_STENCIL_INDEX ||
             baseFormat == GL_DEPTH_STENCIL) {
            /* OK */
         }
         else {
            att->Complete = GL_FALSE;
            att_incomplete("bad renderbuffer stencil format");
            return;
         }
      }
   }
   else {
      assert(att->Type == GL_NONE);
      /* complete */
      return;
   }
}


/**
 * Test if the given framebuffer object is complete and update its
 * Status field with the results.
 * Calls the ctx->Driver.ValidateFramebuffer() function to allow the
 * driver to make hardware-specific validation/completeness checks.
 * Also update the framebuffer's Width and Height fields if the
 * framebuffer is complete.
 */
void
_mesa_test_framebuffer_completeness(struct gl_context *ctx,
                                    struct gl_framebuffer *fb)
{
   GLuint numImages;
   GLenum intFormat = GL_NONE; /* color buffers' internal format */
   GLuint minWidth = ~0, minHeight = ~0, maxWidth = 0, maxHeight = 0;
   GLint numSamples = -1;
   GLint fixedSampleLocations = -1;
   GLint i;
   GLuint j;
   /* Covers max_layer_count, is_layered, and layer_tex_target */
   bool layer_info_valid = false;
   GLuint max_layer_count = 0, att_layer_count;
   bool is_layered = false;
   GLenum layer_tex_target = 0;
   bool has_depth_attachment = false;
   bool has_stencil_attachment = false;

   assert(_mesa_is_user_fbo(fb));

   /* we're changing framebuffer fields here */
   FLUSH_VERTICES(ctx, _NEW_BUFFERS);

   numImages = 0;
   fb->Width = 0;
   fb->Height = 0;
   fb->_AllColorBuffersFixedPoint = GL_TRUE;
   fb->_HasSNormOrFloatColorBuffer = GL_FALSE;
   fb->_HasAttachments = true;
   fb->_IntegerBuffers = 0;

   /* Start at -2 to more easily loop over all attachment points.
    *  -2: depth buffer
    *  -1: stencil buffer
    * >=0: color buffer
    */
   for (i = -2; i < (GLint) ctx->Const.MaxColorAttachments; i++) {
      struct gl_renderbuffer_attachment *att;
      GLenum f;
      mesa_format attFormat;
      GLenum att_tex_target = GL_NONE;

      /*
       * XXX for ARB_fbo, only check color buffers that are named by
       * GL_READ_BUFFER and GL_DRAW_BUFFERi.
       */

      /* check for attachment completeness
       */
      if (i == -2) {
         att = &fb->Attachment[BUFFER_DEPTH];
         test_attachment_completeness(ctx, GL_DEPTH, att);
         if (!att->Complete) {
            fb->_Status = GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT;
            fbo_incomplete(ctx, "depth attachment incomplete", -1);
            return;
         } else if (att->Type != GL_NONE) {
            has_depth_attachment = true;
         }
      }
      else if (i == -1) {
         att = &fb->Attachment[BUFFER_STENCIL];
         test_attachment_completeness(ctx, GL_STENCIL, att);
         if (!att->Complete) {
            fb->_Status = GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT;
            fbo_incomplete(ctx, "stencil attachment incomplete", -1);
            return;
         } else if (att->Type != GL_NONE) {
            has_stencil_attachment = true;
         }
      }
      else {
         att = &fb->Attachment[BUFFER_COLOR0 + i];
         test_attachment_completeness(ctx, GL_COLOR, att);
         if (!att->Complete) {
            fb->_Status = GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT_EXT;
            fbo_incomplete(ctx, "color attachment incomplete", i);
            return;
         }
      }

      /* get width, height, format of the renderbuffer/texture
       */
      if (att->Type == GL_TEXTURE) {
         const struct gl_texture_image *texImg = att->Renderbuffer->TexImage;
         att_tex_target = att->Texture->Target;
         minWidth = MIN2(minWidth, texImg->Width);
         maxWidth = MAX2(maxWidth, texImg->Width);
         minHeight = MIN2(minHeight, texImg->Height);
         maxHeight = MAX2(maxHeight, texImg->Height);
         f = texImg->_BaseFormat;
         attFormat = texImg->TexFormat;
         numImages++;

         if (!is_format_color_renderable(ctx, attFormat,
                                         texImg->InternalFormat) &&
             !is_legal_depth_format(ctx, f) &&
             f != GL_STENCIL_INDEX) {
            fb->_Status = GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT;
            fbo_incomplete(ctx, "texture attachment incomplete", -1);
            return;
         }

         if (numSamples < 0)
            numSamples = texImg->NumSamples;
         else if (numSamples != texImg->NumSamples) {
            fb->_Status = GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
            fbo_incomplete(ctx, "inconsistent sample count", -1);
            return;
         }

         if (fixedSampleLocations < 0)
            fixedSampleLocations = texImg->FixedSampleLocations;
         else if (fixedSampleLocations != texImg->FixedSampleLocations) {
            fb->_Status = GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
            fbo_incomplete(ctx, "inconsistent fixed sample locations", -1);
            return;
         }
      }
      else if (att->Type == GL_RENDERBUFFER_EXT) {
         minWidth = MIN2(minWidth, att->Renderbuffer->Width);
         maxWidth = MAX2(minWidth, att->Renderbuffer->Width);
         minHeight = MIN2(minHeight, att->Renderbuffer->Height);
         maxHeight = MAX2(minHeight, att->Renderbuffer->Height);
         f = att->Renderbuffer->InternalFormat;
         attFormat = att->Renderbuffer->Format;
         numImages++;

         if (numSamples < 0)
            numSamples = att->Renderbuffer->NumSamples;
         else if (numSamples != att->Renderbuffer->NumSamples) {
            fb->_Status = GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
            fbo_incomplete(ctx, "inconsistent sample count", -1);
            return;
         }

         /* RENDERBUFFER has fixedSampleLocations implicitly true */
         if (fixedSampleLocations < 0)
            fixedSampleLocations = GL_TRUE;
         else if (fixedSampleLocations != GL_TRUE) {
            fb->_Status = GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE;
            fbo_incomplete(ctx, "inconsistent fixed sample locations", -1);
            return;
         }
      }
      else {
         assert(att->Type == GL_NONE);
         continue;
      }

      /* Update flags describing color buffer datatypes */
      if (i >= 0) {
         GLenum type = _mesa_get_format_datatype(attFormat);

         /* check if integer color */
         if (_mesa_is_format_integer_color(attFormat))
            fb->_IntegerBuffers |= (1 << i);

         fb->_AllColorBuffersFixedPoint =
            fb->_AllColorBuffersFixedPoint &&
            (type == GL_UNSIGNED_NORMALIZED || type == GL_SIGNED_NORMALIZED);

         fb->_HasSNormOrFloatColorBuffer =
            fb->_HasSNormOrFloatColorBuffer ||
            type == GL_SIGNED_NORMALIZED || type == GL_FLOAT;
      }

      /* Error-check width, height, format */
      if (numImages == 1) {
         /* save format */
         if (i >= 0) {
            intFormat = f;
         }
      }
      else {
         if (!ctx->Extensions.ARB_framebuffer_object) {
            /* check that width, height, format are same */
            if (minWidth != maxWidth || minHeight != maxHeight) {
               fb->_Status = GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS_EXT;
               fbo_incomplete(ctx, "width or height mismatch", -1);
               return;
            }
            /* check that all color buffers are the same format */
            if (intFormat != GL_NONE && f != intFormat) {
               fb->_Status = GL_FRAMEBUFFER_INCOMPLETE_FORMATS_EXT;
               fbo_incomplete(ctx, "format mismatch", -1);
               return;
            }
         }
      }

      /* Check that the format is valid. (MESA_FORMAT_NONE means unsupported)
       */
      if (att->Type == GL_RENDERBUFFER &&
          att->Renderbuffer->Format == MESA_FORMAT_NONE) {
         fb->_Status = GL_FRAMEBUFFER_UNSUPPORTED;
         fbo_incomplete(ctx, "unsupported renderbuffer format", i);
         return;
      }

      /* Check that layered rendering is consistent. */
      if (att->Layered) {
         if (att_tex_target == GL_TEXTURE_CUBE_MAP)
            att_layer_count = 6;
         else if (att_tex_target == GL_TEXTURE_1D_ARRAY)
            att_layer_count = att->Renderbuffer->Height;
         else
            att_layer_count = att->Renderbuffer->Depth;
      } else {
         att_layer_count = 0;
      }
      if (!layer_info_valid) {
         is_layered = att->Layered;
         max_layer_count = att_layer_count;
         layer_tex_target = att_tex_target;
         layer_info_valid = true;
      } else if (max_layer_count > 0 && layer_tex_target != att_tex_target) {
         fb->_Status = GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS;
         fbo_incomplete(ctx, "layered framebuffer has mismatched targets", i);
         return;
      } else if (is_layered != att->Layered) {
         fb->_Status = GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS;
         fbo_incomplete(ctx,
                        "framebuffer attachment layer mode is inconsistent",
                        i);
         return;
      } else if (att_layer_count > max_layer_count) {
         max_layer_count = att_layer_count;
      }

      /*
       * The extension GL_ARB_framebuffer_no_attachments places additional
       * requirement on each attachment. Those additional requirements are
       * tighter that those of previous versions of GL. In interest of better
       * compatibility, we will not enforce these restrictions. For the record
       * those additional restrictions are quoted below:
       *
       * "The width and height of image are greater than zero and less than or
       *  equal to the values of the implementation-dependent limits
       *  MAX_FRAMEBUFFER_WIDTH and MAX_FRAMEBUFFER_HEIGHT, respectively."
       *
       * "If <image> is a three-dimensional texture or a one- or two-dimensional
       *  array texture and the attachment is layered, the depth or layer count
       *  of the texture is less than or equal to the implementation-dependent
       *  limit MAX_FRAMEBUFFER_LAYERS."
       *
       * "If image has multiple samples, its sample count is less than or equal
       *  to the value of the implementation-dependent limit
       *  MAX_FRAMEBUFFER_SAMPLES."
       *
       * The same requirements are also in place for GL 4.5,
       * Section 9.4.1 "Framebuffer Attachment Completeness", pg 310-311
       */
   }

   fb->MaxNumLayers = max_layer_count;

   if (numImages == 0) {
      fb->_HasAttachments = false;

      if (!ctx->Extensions.ARB_framebuffer_no_attachments) {
         fb->_Status = GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT;
         fbo_incomplete(ctx, "no attachments", -1);
         return;
      }

      if (fb->DefaultGeometry.Width == 0 || fb->DefaultGeometry.Height == 0) {
         fb->_Status = GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT_EXT;
         fbo_incomplete(ctx, "no attachments and default width or height is 0", -1);
         return;
      }
   }

   if (_mesa_is_desktop_gl(ctx) && !ctx->Extensions.ARB_ES2_compatibility) {
      /* Check that all DrawBuffers are present */
      for (j = 0; j < ctx->Const.MaxDrawBuffers; j++) {
         if (fb->ColorDrawBuffer[j] != GL_NONE) {
            const struct gl_renderbuffer_attachment *att
               = get_attachment(ctx, fb, fb->ColorDrawBuffer[j], NULL);
            assert(att);
            if (att->Type == GL_NONE) {
               fb->_Status = GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER_EXT;
               fbo_incomplete(ctx, "missing drawbuffer", j);
               return;
            }
         }
      }

      /* Check that the ReadBuffer is present */
      if (fb->ColorReadBuffer != GL_NONE) {
         const struct gl_renderbuffer_attachment *att
            = get_attachment(ctx, fb, fb->ColorReadBuffer, NULL);
         assert(att);
         if (att->Type == GL_NONE) {
            fb->_Status = GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER_EXT;
            fbo_incomplete(ctx, "missing readbuffer", -1);
            return;
         }
      }
   }

   /* The OpenGL ES3 spec, in chapter 9.4. FRAMEBUFFER COMPLETENESS, says:
    *
    *    "Depth and stencil attachments, if present, are the same image."
    *
    * This restriction is not present in the OpenGL ES2 spec.
    */
   if (_mesa_is_gles3(ctx) &&
       has_stencil_attachment && has_depth_attachment &&
       !_mesa_has_depthstencil_combined(fb)) {
      fb->_Status = GL_FRAMEBUFFER_UNSUPPORTED;
      fbo_incomplete(ctx, "Depth and stencil attachments must be the same image", -1);
      return;
   }

   /* Provisionally set status = COMPLETE ... */
   fb->_Status = GL_FRAMEBUFFER_COMPLETE_EXT;

   /* ... but the driver may say the FB is incomplete.
    * Drivers will most likely set the status to GL_FRAMEBUFFER_UNSUPPORTED
    * if anything.
    */
   if (ctx->Driver.ValidateFramebuffer) {
      ctx->Driver.ValidateFramebuffer(ctx, fb);
      if (fb->_Status != GL_FRAMEBUFFER_COMPLETE_EXT) {
         fbo_incomplete(ctx, "driver marked FBO as incomplete", -1);
         return;
      }
   }

   /*
    * Note that if ARB_framebuffer_object is supported and the attached
    * renderbuffers/textures are different sizes, the framebuffer
    * width/height will be set to the smallest width/height.
    */
   if (numImages != 0) {
      fb->Width = minWidth;
      fb->Height = minHeight;
   }

   /* finally, update the visual info for the framebuffer */
   _mesa_update_framebuffer_visual(ctx, fb);
}


GLboolean GLAPIENTRY
_mesa_IsRenderbuffer(GLuint renderbuffer)
{
   struct gl_renderbuffer *rb;

   GET_CURRENT_CONTEXT(ctx);

   ASSERT_OUTSIDE_BEGIN_END_WITH_RETVAL(ctx, GL_FALSE);

   rb = _mesa_lookup_renderbuffer(ctx, renderbuffer);
   return rb != NULL && rb != &DummyRenderbuffer;
}


static struct gl_renderbuffer *
allocate_renderbuffer_locked(struct gl_context *ctx, GLuint renderbuffer,
                             const char *func)
{
   struct gl_renderbuffer *newRb;

   /* create new renderbuffer object */
   newRb = ctx->Driver.NewRenderbuffer(ctx, renderbuffer);
   if (!newRb) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s", func);
      return NULL;
   }
   assert(newRb->AllocStorage);
   _mesa_HashInsertLocked(ctx->Shared->RenderBuffers, renderbuffer, newRb);

   return newRb;
}


static void
bind_renderbuffer(GLenum target, GLuint renderbuffer, bool allow_user_names)
{
   struct gl_renderbuffer *newRb;
   GET_CURRENT_CONTEXT(ctx);

   if (target != GL_RENDERBUFFER_EXT) {
      _mesa_error(ctx, GL_INVALID_ENUM, "glBindRenderbufferEXT(target)");
      return;
   }

   /* No need to flush here since the render buffer binding has no
    * effect on rendering state.
    */

   if (renderbuffer) {
      newRb = _mesa_lookup_renderbuffer(ctx, renderbuffer);
      if (newRb == &DummyRenderbuffer) {
         /* ID was reserved, but no real renderbuffer object made yet */
         newRb = NULL;
      }
      else if (!newRb && !allow_user_names) {
         /* All RB IDs must be Gen'd */
         _mesa_error(ctx, GL_INVALID_OPERATION, "glBindRenderbuffer(buffer)");
         return;
      }

      if (!newRb) {
         _mesa_HashLockMutex(ctx->Shared->RenderBuffers);
         newRb = allocate_renderbuffer_locked(ctx, renderbuffer,
                                              "glBindRenderbufferEXT");
         _mesa_HashUnlockMutex(ctx->Shared->RenderBuffers);
      }
   }
   else {
      newRb = NULL;
   }

   assert(newRb != &DummyRenderbuffer);

   _mesa_reference_renderbuffer(&ctx->CurrentRenderbuffer, newRb);
}

void GLAPIENTRY
_mesa_BindRenderbuffer(GLenum target, GLuint renderbuffer)
{
   GET_CURRENT_CONTEXT(ctx);

   /* OpenGL ES glBindRenderbuffer and glBindRenderbufferOES use this same
    * entry point, but they allow the use of user-generated names.
    */
   bind_renderbuffer(target, renderbuffer, _mesa_is_gles(ctx));
}

void GLAPIENTRY
_mesa_BindRenderbufferEXT(GLenum target, GLuint renderbuffer)
{
   /* This function should not be in the dispatch table for core profile /
    * OpenGL 3.1, so execution should never get here in those cases -- no
    * need for an explicit test.
    */
   bind_renderbuffer(target, renderbuffer, true);
}

/**
 * ARB_framebuffer_no_attachment - Application passes requested param's
 * here. NOTE: NumSamples requested need not be _NumSamples which is
 * what the hw supports.
 */
static void
framebuffer_parameteri(struct gl_context *ctx, struct gl_framebuffer *fb,
                       GLenum pname, GLint param, const char *func)
{
   switch (pname) {
   case GL_FRAMEBUFFER_DEFAULT_WIDTH:
      if (param < 0 || param > ctx->Const.MaxFramebufferWidth)
        _mesa_error(ctx, GL_INVALID_VALUE, "%s", func);
      else
         fb->DefaultGeometry.Width = param;
      break;
   case GL_FRAMEBUFFER_DEFAULT_HEIGHT:
      if (param < 0 || param > ctx->Const.MaxFramebufferHeight)
        _mesa_error(ctx, GL_INVALID_VALUE, "%s", func);
      else
         fb->DefaultGeometry.Height = param;
      break;
   case GL_FRAMEBUFFER_DEFAULT_LAYERS:
     /*
      * According to the OpenGL ES 3.1 specification section 9.2.1, the
      * GL_FRAMEBUFFER_DEFAULT_LAYERS parameter name is not supported.
      */
      if (_mesa_is_gles31(ctx) && !ctx->Extensions.OES_geometry_shader) {
         _mesa_error(ctx, GL_INVALID_ENUM, "%s(pname=0x%x)", func, pname);
         break;
      }
      if (param < 0 || param > ctx->Const.MaxFramebufferLayers)
         _mesa_error(ctx, GL_INVALID_VALUE, "%s", func);
      else
         fb->DefaultGeometry.Layers = param;
      break;
   case GL_FRAMEBUFFER_DEFAULT_SAMPLES:
      if (param < 0 || param > ctx->Const.MaxFramebufferSamples)
        _mesa_error(ctx, GL_INVALID_VALUE, "%s", func);
      else
        fb->DefaultGeometry.NumSamples = param;
      break;
   case GL_FRAMEBUFFER_DEFAULT_FIXED_SAMPLE_LOCATIONS:
      fb->DefaultGeometry.FixedSampleLocations = param;
      break;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "%s(pname=0x%x)", func, pname);
   }

   invalidate_framebuffer(fb);
   ctx->NewState |= _NEW_BUFFERS;
}

void GLAPIENTRY
_mesa_FramebufferParameteri(GLenum target, GLenum pname, GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_framebuffer *fb;

   if (!ctx->Extensions.ARB_framebuffer_no_attachments) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glFramebufferParameteriv not supported "
                  "(ARB_framebuffer_no_attachments not implemented)");
      return;
   }

   fb = get_framebuffer_target(ctx, target);
   if (!fb) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glFramebufferParameteri(target=0x%x)", target);
      return;
   }

   /* check framebuffer binding */
   if (_mesa_is_winsys_fbo(fb)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glFramebufferParameteri");
      return;
   }

   framebuffer_parameteri(ctx, fb, pname, param, "glFramebufferParameteri");
}

static bool
_pname_valid_for_default_framebuffer(struct gl_context *ctx,
                                     GLenum pname)
{
   if (!_mesa_is_desktop_gl(ctx))
      return false;

   switch (pname) {
   case GL_DOUBLEBUFFER:
   case GL_IMPLEMENTATION_COLOR_READ_FORMAT:
   case GL_IMPLEMENTATION_COLOR_READ_TYPE:
   case GL_SAMPLES:
   case GL_SAMPLE_BUFFERS:
   case GL_STEREO:
      return true;
   default:
      return false;
   }
}

static void
get_framebuffer_parameteriv(struct gl_context *ctx, struct gl_framebuffer *fb,
                            GLenum pname, GLint *params, const char *func)
{
   /* From OpenGL 4.5 spec, section 9.2.3 "Framebuffer Object Queries:
    *
    *    "An INVALID_OPERATION error is generated by GetFramebufferParameteriv
    *     if the default framebuffer is bound to target and pname is not one
    *     of the accepted values from table 23.73, other than
    *     SAMPLE_POSITION."
    *
    * For OpenGL ES, using default framebuffer still raises INVALID_OPERATION
    * for any pname.
    */
   if (_mesa_is_winsys_fbo(fb) &&
       !_pname_valid_for_default_framebuffer(ctx, pname)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(invalid pname=0x%x for default framebuffer)", func, pname);
      return;
   }

   switch (pname) {
   case GL_FRAMEBUFFER_DEFAULT_WIDTH:
      *params = fb->DefaultGeometry.Width;
      break;
   case GL_FRAMEBUFFER_DEFAULT_HEIGHT:
      *params = fb->DefaultGeometry.Height;
      break;
   case GL_FRAMEBUFFER_DEFAULT_LAYERS:
      /*
       * According to the OpenGL ES 3.1 specification section 9.2.3, the
       * GL_FRAMEBUFFER_LAYERS parameter name is not supported.
       */
      if (_mesa_is_gles31(ctx) && !ctx->Extensions.OES_geometry_shader) {
         _mesa_error(ctx, GL_INVALID_ENUM, "%s(pname=0x%x)", func, pname);
         break;
      }
      *params = fb->DefaultGeometry.Layers;
      break;
   case GL_FRAMEBUFFER_DEFAULT_SAMPLES:
      *params = fb->DefaultGeometry.NumSamples;
      break;
   case GL_FRAMEBUFFER_DEFAULT_FIXED_SAMPLE_LOCATIONS:
      *params = fb->DefaultGeometry.FixedSampleLocations;
      break;
   case GL_DOUBLEBUFFER:
      *params = fb->Visual.doubleBufferMode;
      break;
   case GL_IMPLEMENTATION_COLOR_READ_FORMAT:
      *params = _mesa_get_color_read_format(ctx, fb, func);
      break;
   case GL_IMPLEMENTATION_COLOR_READ_TYPE:
      *params = _mesa_get_color_read_type(ctx, fb, func);
      break;
   case GL_SAMPLES:
      *params = _mesa_geometric_samples(fb);
      break;
   case GL_SAMPLE_BUFFERS:
      *params = _mesa_geometric_samples(fb) > 0;
      break;
   case GL_STEREO:
      *params = fb->Visual.stereoMode;
      break;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "%s(pname=0x%x)", func, pname);
   }
}

void GLAPIENTRY
_mesa_GetFramebufferParameteriv(GLenum target, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_framebuffer *fb;

   if (!ctx->Extensions.ARB_framebuffer_no_attachments) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glGetFramebufferParameteriv not supported "
                  "(ARB_framebuffer_no_attachments not implemented)");
      return;
   }

   fb = get_framebuffer_target(ctx, target);
   if (!fb) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glGetFramebufferParameteriv(target=0x%x)", target);
      return;
   }

   get_framebuffer_parameteriv(ctx, fb, pname, params,
                               "glGetFramebufferParameteriv");
}


/**
 * Remove the specified renderbuffer or texture from any attachment point in
 * the framebuffer.
 *
 * \returns
 * \c true if the renderbuffer was detached from an attachment point.  \c
 * false otherwise.
 */
bool
_mesa_detach_renderbuffer(struct gl_context *ctx,
                          struct gl_framebuffer *fb,
                          const void *att)
{
   unsigned i;
   bool progress = false;

   for (i = 0; i < BUFFER_COUNT; i++) {
      if (fb->Attachment[i].Texture == att
          || fb->Attachment[i].Renderbuffer == att) {
         remove_attachment(ctx, &fb->Attachment[i]);
         progress = true;
      }
   }

   /* Section 4.4.4 (Framebuffer Completeness), subsection "Whole Framebuffer
    * Completeness," of the OpenGL 3.1 spec says:
    *
    *     "Performing any of the following actions may change whether the
    *     framebuffer is considered complete or incomplete:
    *
    *     ...
    *
    *        - Deleting, with DeleteTextures or DeleteRenderbuffers, an object
    *          containing an image that is attached to a framebuffer object
    *          that is bound to the framebuffer."
    */
   if (progress)
      invalidate_framebuffer(fb);

   return progress;
}


void GLAPIENTRY
_mesa_DeleteRenderbuffers(GLsizei n, const GLuint *renderbuffers)
{
   GLint i;
   GET_CURRENT_CONTEXT(ctx);

   if (n < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glDeleteRenderbuffers(n < 0)");
      return;
   }

   FLUSH_VERTICES(ctx, _NEW_BUFFERS);

   for (i = 0; i < n; i++) {
      if (renderbuffers[i] > 0) {
         struct gl_renderbuffer *rb;
         rb = _mesa_lookup_renderbuffer(ctx, renderbuffers[i]);
         if (rb) {
            /* check if deleting currently bound renderbuffer object */
            if (rb == ctx->CurrentRenderbuffer) {
               /* bind default */
               assert(rb->RefCount >= 2);
               _mesa_BindRenderbuffer(GL_RENDERBUFFER_EXT, 0);
            }

            /* Section 4.4.2 (Attaching Images to Framebuffer Objects),
             * subsection "Attaching Renderbuffer Images to a Framebuffer,"
             * of the OpenGL 3.1 spec says:
             *
             *     "If a renderbuffer object is deleted while its image is
             *     attached to one or more attachment points in the currently
             *     bound framebuffer, then it is as if FramebufferRenderbuffer
             *     had been called, with a renderbuffer of 0, for each
             *     attachment point to which this image was attached in the
             *     currently bound framebuffer. In other words, this
             *     renderbuffer image is first detached from all attachment
             *     points in the currently bound framebuffer. Note that the
             *     renderbuffer image is specifically not detached from any
             *     non-bound framebuffers. Detaching the image from any
             *     non-bound framebuffers is the responsibility of the
             *     application.
             */
            if (_mesa_is_user_fbo(ctx->DrawBuffer)) {
               _mesa_detach_renderbuffer(ctx, ctx->DrawBuffer, rb);
            }
            if (_mesa_is_user_fbo(ctx->ReadBuffer)
                && ctx->ReadBuffer != ctx->DrawBuffer) {
               _mesa_detach_renderbuffer(ctx, ctx->ReadBuffer, rb);
            }

            /* Remove from hash table immediately, to free the ID.
             * But the object will not be freed until it's no longer
             * referenced anywhere else.
             */
            _mesa_HashRemove(ctx->Shared->RenderBuffers, renderbuffers[i]);

            if (rb != &DummyRenderbuffer) {
               /* no longer referenced by hash table */
               _mesa_reference_renderbuffer(&rb, NULL);
            }
         }
      }
   }
}

static void
create_render_buffers(struct gl_context *ctx, GLsizei n, GLuint *renderbuffers,
                      bool dsa)
{
   const char *func = dsa ? "glCreateRenderbuffers" : "glGenRenderbuffers";
   GLuint first;
   GLint i;

   if (!renderbuffers)
      return;

   _mesa_HashLockMutex(ctx->Shared->RenderBuffers);

   first = _mesa_HashFindFreeKeyBlock(ctx->Shared->RenderBuffers, n);

   for (i = 0; i < n; i++) {
      GLuint name = first + i;
      renderbuffers[i] = name;

      if (dsa) {
         allocate_renderbuffer_locked(ctx, name, func);
      } else {
         /* insert a dummy renderbuffer into the hash table */
         _mesa_HashInsertLocked(ctx->Shared->RenderBuffers, name,
                                &DummyRenderbuffer);
      }
   }

   _mesa_HashUnlockMutex(ctx->Shared->RenderBuffers);
}


static void
create_render_buffers_err(struct gl_context *ctx, GLsizei n,
                          GLuint *renderbuffers, bool dsa)
{
   const char *func = dsa ? "glCreateRenderbuffers" : "glGenRenderbuffers";

   if (n < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(n<0)", func);
      return;
   }

   create_render_buffers(ctx, n, renderbuffers, dsa);
}


void GLAPIENTRY
_mesa_GenRenderbuffers_no_error(GLsizei n, GLuint *renderbuffers)
{
   GET_CURRENT_CONTEXT(ctx);
   create_render_buffers(ctx, n, renderbuffers, false);
}


void GLAPIENTRY
_mesa_GenRenderbuffers(GLsizei n, GLuint *renderbuffers)
{
   GET_CURRENT_CONTEXT(ctx);
   create_render_buffers_err(ctx, n, renderbuffers, false);
}


void GLAPIENTRY
_mesa_CreateRenderbuffers_no_error(GLsizei n, GLuint *renderbuffers)
{
   GET_CURRENT_CONTEXT(ctx);
   create_render_buffers(ctx, n, renderbuffers, true);
}


void GLAPIENTRY
_mesa_CreateRenderbuffers(GLsizei n, GLuint *renderbuffers)
{
   GET_CURRENT_CONTEXT(ctx);
   create_render_buffers_err(ctx, n, renderbuffers, true);
}


/**
 * Given an internal format token for a render buffer, return the
 * corresponding base format (one of GL_RGB, GL_RGBA, GL_STENCIL_INDEX,
 * GL_DEPTH_COMPONENT, GL_DEPTH_STENCIL_EXT, GL_ALPHA, GL_LUMINANCE,
 * GL_LUMINANCE_ALPHA, GL_INTENSITY, etc).
 *
 * This is similar to _mesa_base_tex_format() but the set of valid
 * internal formats is different.
 *
 * Note that even if a format is determined to be legal here, validation
 * of the FBO may fail if the format is not supported by the driver/GPU.
 *
 * \param internalFormat  as passed to glRenderbufferStorage()
 * \return the base internal format, or 0 if internalFormat is illegal
 */
GLenum
_mesa_base_fbo_format(const struct gl_context *ctx, GLenum internalFormat)
{
   /*
    * Notes: some formats such as alpha, luminance, etc. were added
    * with GL_ARB_framebuffer_object.
    */
   switch (internalFormat) {
   case GL_ALPHA:
   case GL_ALPHA4:
   case GL_ALPHA8:
   case GL_ALPHA12:
   case GL_ALPHA16:
      return (ctx->API == API_OPENGL_COMPAT &&
              ctx->Extensions.ARB_framebuffer_object) ? GL_ALPHA : 0;
   case GL_LUMINANCE:
   case GL_LUMINANCE4:
   case GL_LUMINANCE8:
   case GL_LUMINANCE12:
   case GL_LUMINANCE16:
      return (ctx->API == API_OPENGL_COMPAT &&
              ctx->Extensions.ARB_framebuffer_object) ? GL_LUMINANCE : 0;
   case GL_LUMINANCE_ALPHA:
   case GL_LUMINANCE4_ALPHA4:
   case GL_LUMINANCE6_ALPHA2:
   case GL_LUMINANCE8_ALPHA8:
   case GL_LUMINANCE12_ALPHA4:
   case GL_LUMINANCE12_ALPHA12:
   case GL_LUMINANCE16_ALPHA16:
      return (ctx->API == API_OPENGL_COMPAT &&
              ctx->Extensions.ARB_framebuffer_object) ? GL_LUMINANCE_ALPHA : 0;
   case GL_INTENSITY:
   case GL_INTENSITY4:
   case GL_INTENSITY8:
   case GL_INTENSITY12:
   case GL_INTENSITY16:
      return (ctx->API == API_OPENGL_COMPAT &&
              ctx->Extensions.ARB_framebuffer_object) ? GL_INTENSITY : 0;
   case GL_RGB8:
      return GL_RGB;
   case GL_RGB:
   case GL_R3_G3_B2:
   case GL_RGB4:
   case GL_RGB5:
   case GL_RGB10:
   case GL_RGB12:
   case GL_RGB16:
      return _mesa_is_desktop_gl(ctx) ? GL_RGB : 0;
   case GL_SRGB8_EXT:
      return _mesa_is_desktop_gl(ctx) ? GL_RGB : 0;
   case GL_RGBA4:
   case GL_RGB5_A1:
   case GL_RGBA8:
      return GL_RGBA;
   case GL_RGBA:
   case GL_RGBA2:
   case GL_RGBA12:
   case GL_RGBA16:
      return _mesa_is_desktop_gl(ctx) ? GL_RGBA : 0;
   case GL_RGB10_A2:
   case GL_SRGB8_ALPHA8_EXT:
      return _mesa_is_desktop_gl(ctx) || _mesa_is_gles3(ctx) ? GL_RGBA : 0;
   case GL_STENCIL_INDEX:
   case GL_STENCIL_INDEX1_EXT:
   case GL_STENCIL_INDEX4_EXT:
   case GL_STENCIL_INDEX16_EXT:
      /* There are extensions for GL_STENCIL_INDEX1 and GL_STENCIL_INDEX4 in
       * OpenGL ES, but Mesa does not currently support them.
       */
      return _mesa_is_desktop_gl(ctx) ? GL_STENCIL_INDEX : 0;
   case GL_STENCIL_INDEX8_EXT:
      return GL_STENCIL_INDEX;
   case GL_DEPTH_COMPONENT:
   case GL_DEPTH_COMPONENT32:
      return _mesa_is_desktop_gl(ctx) ? GL_DEPTH_COMPONENT : 0;
   case GL_DEPTH_COMPONENT16:
   case GL_DEPTH_COMPONENT24:
      return GL_DEPTH_COMPONENT;
   case GL_DEPTH_STENCIL:
      return _mesa_is_desktop_gl(ctx) ? GL_DEPTH_STENCIL : 0;
   case GL_DEPTH24_STENCIL8:
      return GL_DEPTH_STENCIL;
   case GL_DEPTH_COMPONENT32F:
      return ctx->Version >= 30
         || (ctx->API == API_OPENGL_COMPAT &&
             ctx->Extensions.ARB_depth_buffer_float)
         ? GL_DEPTH_COMPONENT : 0;
   case GL_DEPTH32F_STENCIL8:
      return ctx->Version >= 30
         || (ctx->API == API_OPENGL_COMPAT &&
             ctx->Extensions.ARB_depth_buffer_float)
         ? GL_DEPTH_STENCIL : 0;
   case GL_RED:
   case GL_R16:
      return _mesa_is_desktop_gl(ctx) && ctx->Extensions.ARB_texture_rg
         ? GL_RED : 0;
   case GL_R8:
      return ctx->API != API_OPENGLES && ctx->Extensions.ARB_texture_rg
         ? GL_RED : 0;
   case GL_RG:
   case GL_RG16:
      return _mesa_is_desktop_gl(ctx) && ctx->Extensions.ARB_texture_rg
         ? GL_RG : 0;
   case GL_RG8:
      return ctx->API != API_OPENGLES && ctx->Extensions.ARB_texture_rg
         ? GL_RG : 0;
   /* signed normalized texture formats */
   case GL_RED_SNORM:
   case GL_R8_SNORM:
   case GL_R16_SNORM:
      return _mesa_is_desktop_gl(ctx) && ctx->Extensions.EXT_texture_snorm
         ? GL_RED : 0;
   case GL_RG_SNORM:
   case GL_RG8_SNORM:
   case GL_RG16_SNORM:
      return _mesa_is_desktop_gl(ctx) && ctx->Extensions.EXT_texture_snorm
         ? GL_RG : 0;
   case GL_RGB_SNORM:
   case GL_RGB8_SNORM:
   case GL_RGB16_SNORM:
      return _mesa_is_desktop_gl(ctx) && ctx->Extensions.EXT_texture_snorm
         ? GL_RGB : 0;
   case GL_RGBA_SNORM:
   case GL_RGBA8_SNORM:
   case GL_RGBA16_SNORM:
      return _mesa_is_desktop_gl(ctx) && ctx->Extensions.EXT_texture_snorm
         ? GL_RGBA : 0;
   case GL_ALPHA_SNORM:
   case GL_ALPHA8_SNORM:
   case GL_ALPHA16_SNORM:
      return ctx->API == API_OPENGL_COMPAT &&
             ctx->Extensions.EXT_texture_snorm &&
             ctx->Extensions.ARB_framebuffer_object ? GL_ALPHA : 0;
   case GL_LUMINANCE_SNORM:
   case GL_LUMINANCE8_SNORM:
   case GL_LUMINANCE16_SNORM:
      return _mesa_is_desktop_gl(ctx) && ctx->Extensions.EXT_texture_snorm
         ? GL_LUMINANCE : 0;
   case GL_LUMINANCE_ALPHA_SNORM:
   case GL_LUMINANCE8_ALPHA8_SNORM:
   case GL_LUMINANCE16_ALPHA16_SNORM:
      return _mesa_is_desktop_gl(ctx) && ctx->Extensions.EXT_texture_snorm
         ? GL_LUMINANCE_ALPHA : 0;
   case GL_INTENSITY_SNORM:
   case GL_INTENSITY8_SNORM:
   case GL_INTENSITY16_SNORM:
      return _mesa_is_desktop_gl(ctx) && ctx->Extensions.EXT_texture_snorm
         ? GL_INTENSITY : 0;

   case GL_R16F:
   case GL_R32F:
      return ((_mesa_is_desktop_gl(ctx) &&
               ctx->Extensions.ARB_texture_rg &&
               ctx->Extensions.ARB_texture_float) ||
              _mesa_is_gles3(ctx) /* EXT_color_buffer_float */ )
         ? GL_RED : 0;
   case GL_RG16F:
   case GL_RG32F:
      return ((_mesa_is_desktop_gl(ctx) &&
               ctx->Extensions.ARB_texture_rg &&
               ctx->Extensions.ARB_texture_float) ||
              _mesa_is_gles3(ctx) /* EXT_color_buffer_float */ )
         ? GL_RG : 0;
   case GL_RGB16F:
   case GL_RGB32F:
      return (_mesa_is_desktop_gl(ctx) && ctx->Extensions.ARB_texture_float)
         ? GL_RGB : 0;
   case GL_RGBA16F:
   case GL_RGBA32F:
      return ((_mesa_is_desktop_gl(ctx) &&
               ctx->Extensions.ARB_texture_float) ||
              _mesa_is_gles3(ctx) /* EXT_color_buffer_float */ )
         ? GL_RGBA : 0;
   case GL_ALPHA16F_ARB:
   case GL_ALPHA32F_ARB:
      return ctx->API == API_OPENGL_COMPAT &&
             ctx->Extensions.ARB_texture_float &&
             ctx->Extensions.ARB_framebuffer_object ? GL_ALPHA : 0;
   case GL_LUMINANCE16F_ARB:
   case GL_LUMINANCE32F_ARB:
      return ctx->API == API_OPENGL_COMPAT &&
             ctx->Extensions.ARB_texture_float &&
             ctx->Extensions.ARB_framebuffer_object ? GL_LUMINANCE : 0;
   case GL_LUMINANCE_ALPHA16F_ARB:
   case GL_LUMINANCE_ALPHA32F_ARB:
      return ctx->API == API_OPENGL_COMPAT &&
             ctx->Extensions.ARB_texture_float &&
             ctx->Extensions.ARB_framebuffer_object ? GL_LUMINANCE_ALPHA : 0;
   case GL_INTENSITY16F_ARB:
   case GL_INTENSITY32F_ARB:
      return ctx->API == API_OPENGL_COMPAT &&
             ctx->Extensions.ARB_texture_float &&
             ctx->Extensions.ARB_framebuffer_object ? GL_INTENSITY : 0;
   case GL_R11F_G11F_B10F:
      return ((_mesa_is_desktop_gl(ctx) && ctx->Extensions.EXT_packed_float) ||
              _mesa_is_gles3(ctx) /* EXT_color_buffer_float */ )
         ? GL_RGB : 0;

   case GL_RGBA8UI_EXT:
   case GL_RGBA16UI_EXT:
   case GL_RGBA32UI_EXT:
   case GL_RGBA8I_EXT:
   case GL_RGBA16I_EXT:
   case GL_RGBA32I_EXT:
      return ctx->Version >= 30
         || (_mesa_is_desktop_gl(ctx) &&
             ctx->Extensions.EXT_texture_integer) ? GL_RGBA : 0;

   case GL_RGB8UI_EXT:
   case GL_RGB16UI_EXT:
   case GL_RGB32UI_EXT:
   case GL_RGB8I_EXT:
   case GL_RGB16I_EXT:
   case GL_RGB32I_EXT:
      return _mesa_is_desktop_gl(ctx) && ctx->Extensions.EXT_texture_integer
         ? GL_RGB : 0;
   case GL_R8UI:
   case GL_R8I:
   case GL_R16UI:
   case GL_R16I:
   case GL_R32UI:
   case GL_R32I:
      return ctx->Version >= 30
         || (_mesa_is_desktop_gl(ctx) &&
             ctx->Extensions.ARB_texture_rg &&
             ctx->Extensions.EXT_texture_integer) ? GL_RED : 0;

   case GL_RG8UI:
   case GL_RG8I:
   case GL_RG16UI:
   case GL_RG16I:
   case GL_RG32UI:
   case GL_RG32I:
      return ctx->Version >= 30
         || (_mesa_is_desktop_gl(ctx) &&
             ctx->Extensions.ARB_texture_rg &&
             ctx->Extensions.EXT_texture_integer) ? GL_RG : 0;

   case GL_INTENSITY8I_EXT:
   case GL_INTENSITY8UI_EXT:
   case GL_INTENSITY16I_EXT:
   case GL_INTENSITY16UI_EXT:
   case GL_INTENSITY32I_EXT:
   case GL_INTENSITY32UI_EXT:
      return ctx->API == API_OPENGL_COMPAT &&
             ctx->Extensions.EXT_texture_integer &&
             ctx->Extensions.ARB_framebuffer_object ? GL_INTENSITY : 0;

   case GL_LUMINANCE8I_EXT:
   case GL_LUMINANCE8UI_EXT:
   case GL_LUMINANCE16I_EXT:
   case GL_LUMINANCE16UI_EXT:
   case GL_LUMINANCE32I_EXT:
   case GL_LUMINANCE32UI_EXT:
      return ctx->API == API_OPENGL_COMPAT &&
             ctx->Extensions.EXT_texture_integer &&
             ctx->Extensions.ARB_framebuffer_object ? GL_LUMINANCE : 0;

   case GL_LUMINANCE_ALPHA8I_EXT:
   case GL_LUMINANCE_ALPHA8UI_EXT:
   case GL_LUMINANCE_ALPHA16I_EXT:
   case GL_LUMINANCE_ALPHA16UI_EXT:
   case GL_LUMINANCE_ALPHA32I_EXT:
   case GL_LUMINANCE_ALPHA32UI_EXT:
      return ctx->API == API_OPENGL_COMPAT &&
             ctx->Extensions.EXT_texture_integer &&
             ctx->Extensions.ARB_framebuffer_object ? GL_LUMINANCE_ALPHA : 0;

   case GL_ALPHA8I_EXT:
   case GL_ALPHA8UI_EXT:
   case GL_ALPHA16I_EXT:
   case GL_ALPHA16UI_EXT:
   case GL_ALPHA32I_EXT:
   case GL_ALPHA32UI_EXT:
      return ctx->API == API_OPENGL_COMPAT &&
             ctx->Extensions.EXT_texture_integer &&
             ctx->Extensions.ARB_framebuffer_object ? GL_ALPHA : 0;

   case GL_RGB10_A2UI:
      return (_mesa_is_desktop_gl(ctx) &&
              ctx->Extensions.ARB_texture_rgb10_a2ui)
         || _mesa_is_gles3(ctx) ? GL_RGBA : 0;

   case GL_RGB565:
      return _mesa_is_gles(ctx) || ctx->Extensions.ARB_ES2_compatibility
         ? GL_RGB : 0;
   default:
      return 0;
   }
}


/**
 * Invalidate a renderbuffer attachment.  Called from _mesa_HashWalk().
 */
static void
invalidate_rb(GLuint key, void *data, void *userData)
{
   struct gl_framebuffer *fb = (struct gl_framebuffer *) data;
   struct gl_renderbuffer *rb = (struct gl_renderbuffer *) userData;

   /* If this is a user-created FBO */
   if (_mesa_is_user_fbo(fb)) {
      GLuint i;
      for (i = 0; i < BUFFER_COUNT; i++) {
         struct gl_renderbuffer_attachment *att = fb->Attachment + i;
         if (att->Type == GL_RENDERBUFFER &&
             att->Renderbuffer == rb) {
            /* Mark fb status as indeterminate to force re-validation */
            fb->_Status = 0;
            return;
         }
      }
   }
}


/** sentinal value, see below */
#define NO_SAMPLES 1000

void
_mesa_renderbuffer_storage(struct gl_context *ctx, struct gl_renderbuffer *rb,
                           GLenum internalFormat, GLsizei width,
                           GLsizei height, GLsizei samples)
{
   const GLenum baseFormat = _mesa_base_fbo_format(ctx, internalFormat);

   assert(baseFormat != 0);
   assert(width >= 0 && width <= (GLsizei) ctx->Const.MaxRenderbufferSize);
   assert(height >= 0 && height <= (GLsizei) ctx->Const.MaxRenderbufferSize);
   assert(samples != NO_SAMPLES);
   if (samples != 0) {
      assert(samples > 0);
      assert(_mesa_check_sample_count(ctx, GL_RENDERBUFFER,
                                      internalFormat, samples) == GL_NO_ERROR);
   }

   FLUSH_VERTICES(ctx, _NEW_BUFFERS);

   if (rb->InternalFormat == internalFormat &&
       rb->Width == (GLuint) width &&
       rb->Height == (GLuint) height &&
       rb->NumSamples == samples) {
      /* no change in allocation needed */
      return;
   }

   /* These MUST get set by the AllocStorage func */
   rb->Format = MESA_FORMAT_NONE;
   rb->NumSamples = samples;

   /* Now allocate the storage */
   assert(rb->AllocStorage);
   if (rb->AllocStorage(ctx, rb, internalFormat, width, height)) {
      /* No error - check/set fields now */
      /* If rb->Format == MESA_FORMAT_NONE, the format is unsupported. */
      assert(rb->Width == (GLuint) width);
      assert(rb->Height == (GLuint) height);
      rb->InternalFormat = internalFormat;
      rb->_BaseFormat = baseFormat;
      assert(rb->_BaseFormat != 0);
   }
   else {
      /* Probably ran out of memory - clear the fields */
      rb->Width = 0;
      rb->Height = 0;
      rb->Format = MESA_FORMAT_NONE;
      rb->InternalFormat = GL_NONE;
      rb->_BaseFormat = GL_NONE;
      rb->NumSamples = 0;
   }

   /* Invalidate the framebuffers the renderbuffer is attached in. */
   if (rb->AttachedAnytime) {
      _mesa_HashWalk(ctx->Shared->FrameBuffers, invalidate_rb, rb);
   }
}

/**
 * Helper function used by renderbuffer_storage_direct() and
 * renderbuffer_storage_target().
 * samples will be NO_SAMPLES if called by a non-multisample function.
 */
static void
renderbuffer_storage(struct gl_context *ctx, struct gl_renderbuffer *rb,
                     GLenum internalFormat, GLsizei width,
                     GLsizei height, GLsizei samples, const char *func)
{
   GLenum baseFormat;
   GLenum sample_count_error;

   baseFormat = _mesa_base_fbo_format(ctx, internalFormat);
   if (baseFormat == 0) {
      _mesa_error(ctx, GL_INVALID_ENUM, "%s(internalFormat=%s)",
                  func, _mesa_enum_to_string(internalFormat));
      return;
   }

   if (width < 0 || width > (GLsizei) ctx->Const.MaxRenderbufferSize) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(invalid width %d)", func,
                  width);
      return;
   }

   if (height < 0 || height > (GLsizei) ctx->Const.MaxRenderbufferSize) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(invalid height %d)", func,
                  height);
      return;
   }

   if (samples == NO_SAMPLES) {
      /* NumSamples == 0 indicates non-multisampling */
      samples = 0;
   }
   else {
      /* check the sample count;
       * note: driver may choose to use more samples than what's requested
       */
      sample_count_error = _mesa_check_sample_count(ctx, GL_RENDERBUFFER,
            internalFormat, samples);

      /* Section 2.5 (GL Errors) of OpenGL 3.0 specification, page 16:
       *
       * "If a negative number is provided where an argument of type sizei or
       * sizeiptr is specified, the error INVALID VALUE is generated."
       */
      if (samples < 0) {
         sample_count_error = GL_INVALID_VALUE;
      }

      if (sample_count_error != GL_NO_ERROR) {
         _mesa_error(ctx, sample_count_error, "%s(samples=%d)", func, samples);
         return;
      }
   }

   _mesa_renderbuffer_storage(ctx, rb, internalFormat, width, height, samples);
}

/**
 * Helper function used by _mesa_NamedRenderbufferStorage*().
 * samples will be NO_SAMPLES if called by a non-multisample function.
 */
static void
renderbuffer_storage_named(GLuint renderbuffer, GLenum internalFormat,
                           GLsizei width, GLsizei height, GLsizei samples,
                           const char *func)
{
   GET_CURRENT_CONTEXT(ctx);

   if (MESA_VERBOSE & VERBOSE_API) {
      if (samples == NO_SAMPLES)
         _mesa_debug(ctx, "%s(%u, %s, %d, %d)\n",
                     func, renderbuffer,
                     _mesa_enum_to_string(internalFormat),
                     width, height);
      else
         _mesa_debug(ctx, "%s(%u, %s, %d, %d, %d)\n",
                     func, renderbuffer,
                     _mesa_enum_to_string(internalFormat),
                     width, height, samples);
   }

   struct gl_renderbuffer *rb = _mesa_lookup_renderbuffer(ctx, renderbuffer);
   if (!rb || rb == &DummyRenderbuffer) {
      /* ID was reserved, but no real renderbuffer object made yet */
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(invalid renderbuffer %u)",
                  func, renderbuffer);
      return;
   }

   renderbuffer_storage(ctx, rb, internalFormat, width, height, samples, func);
}

/**
 * Helper function used by _mesa_RenderbufferStorage() and
 * _mesa_RenderbufferStorageMultisample().
 * samples will be NO_SAMPLES if called by _mesa_RenderbufferStorage().
 */
static void
renderbuffer_storage_target(GLenum target, GLenum internalFormat,
                            GLsizei width, GLsizei height, GLsizei samples,
                            const char *func)
{
   GET_CURRENT_CONTEXT(ctx);

   if (MESA_VERBOSE & VERBOSE_API) {
      if (samples == NO_SAMPLES)
         _mesa_debug(ctx, "%s(%s, %s, %d, %d)\n",
                     func,
                     _mesa_enum_to_string(target),
                     _mesa_enum_to_string(internalFormat),
                     width, height);
      else
         _mesa_debug(ctx, "%s(%s, %s, %d, %d, %d)\n",
                     func,
                     _mesa_enum_to_string(target),
                     _mesa_enum_to_string(internalFormat),
                     width, height, samples);
   }

   if (target != GL_RENDERBUFFER_EXT) {
      _mesa_error(ctx, GL_INVALID_ENUM, "%s(target)", func);
      return;
   }

   if (!ctx->CurrentRenderbuffer) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(no renderbuffer bound)",
                  func);
      return;
   }

   renderbuffer_storage(ctx, ctx->CurrentRenderbuffer, internalFormat, width,
                        height, samples, func);
}


void GLAPIENTRY
_mesa_EGLImageTargetRenderbufferStorageOES(GLenum target, GLeglImageOES image)
{
   struct gl_renderbuffer *rb;
   GET_CURRENT_CONTEXT(ctx);

   if (!ctx->Extensions.OES_EGL_image) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glEGLImageTargetRenderbufferStorageOES(unsupported)");
      return;
   }

   if (target != GL_RENDERBUFFER) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "EGLImageTargetRenderbufferStorageOES");
      return;
   }

   rb = ctx->CurrentRenderbuffer;
   if (!rb) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "EGLImageTargetRenderbufferStorageOES");
      return;
   }

   FLUSH_VERTICES(ctx, _NEW_BUFFERS);

   ctx->Driver.EGLImageTargetRenderbufferStorage(ctx, rb, image);
}


/**
 * Helper function for _mesa_GetRenderbufferParameteriv() and
 * _mesa_GetFramebufferAttachmentParameteriv()
 * We have to be careful to respect the base format.  For example, if a
 * renderbuffer/texture was created with internalFormat=GL_RGB but the
 * driver actually chose a GL_RGBA format, when the user queries ALPHA_SIZE
 * we need to return zero.
 */
static GLint
get_component_bits(GLenum pname, GLenum baseFormat, mesa_format format)
{
   if (_mesa_base_format_has_channel(baseFormat, pname))
      return _mesa_get_format_bits(format, pname);
   else
      return 0;
}



void GLAPIENTRY
_mesa_RenderbufferStorage(GLenum target, GLenum internalFormat,
                             GLsizei width, GLsizei height)
{
   /* GL_ARB_fbo says calling this function is equivalent to calling
    * glRenderbufferStorageMultisample() with samples=0.  We pass in
    * a token value here just for error reporting purposes.
    */
   renderbuffer_storage_target(target, internalFormat, width, height,
                               NO_SAMPLES, "glRenderbufferStorage");
}


void GLAPIENTRY
_mesa_RenderbufferStorageMultisample(GLenum target, GLsizei samples,
                                     GLenum internalFormat,
                                     GLsizei width, GLsizei height)
{
   renderbuffer_storage_target(target, internalFormat, width, height,
                               samples, "glRenderbufferStorageMultisample");
}


/**
 * OpenGL ES version of glRenderBufferStorage.
 */
void GLAPIENTRY
_es_RenderbufferStorageEXT(GLenum target, GLenum internalFormat,
                           GLsizei width, GLsizei height)
{
   switch (internalFormat) {
   case GL_RGB565:
      /* XXX this confuses GL_RENDERBUFFER_INTERNAL_FORMAT_OES */
      /* choose a closest format */
      internalFormat = GL_RGB5;
      break;
   default:
      break;
   }

   renderbuffer_storage_target(target, internalFormat, width, height, 0,
                               "glRenderbufferStorageEXT");
}

void GLAPIENTRY
_mesa_NamedRenderbufferStorage(GLuint renderbuffer, GLenum internalformat,
                               GLsizei width, GLsizei height)
{
   /* GL_ARB_fbo says calling this function is equivalent to calling
    * glRenderbufferStorageMultisample() with samples=0.  We pass in
    * a token value here just for error reporting purposes.
    */
   renderbuffer_storage_named(renderbuffer, internalformat, width, height,
                              NO_SAMPLES, "glNamedRenderbufferStorage");
}

void GLAPIENTRY
_mesa_NamedRenderbufferStorageMultisample(GLuint renderbuffer, GLsizei samples,
                                          GLenum internalformat,
                                          GLsizei width, GLsizei height)
{
   renderbuffer_storage_named(renderbuffer, internalformat, width, height,
                              samples,
                              "glNamedRenderbufferStorageMultisample");
}


static void
get_render_buffer_parameteriv(struct gl_context *ctx,
                              struct gl_renderbuffer *rb, GLenum pname,
                              GLint *params, const char *func)
{
   /* No need to flush here since we're just quering state which is
    * not effected by rendering.
    */

   switch (pname) {
   case GL_RENDERBUFFER_WIDTH_EXT:
      *params = rb->Width;
      return;
   case GL_RENDERBUFFER_HEIGHT_EXT:
      *params = rb->Height;
      return;
   case GL_RENDERBUFFER_INTERNAL_FORMAT_EXT:
      *params = rb->InternalFormat;
      return;
   case GL_RENDERBUFFER_RED_SIZE_EXT:
   case GL_RENDERBUFFER_GREEN_SIZE_EXT:
   case GL_RENDERBUFFER_BLUE_SIZE_EXT:
   case GL_RENDERBUFFER_ALPHA_SIZE_EXT:
   case GL_RENDERBUFFER_DEPTH_SIZE_EXT:
   case GL_RENDERBUFFER_STENCIL_SIZE_EXT:
      *params = get_component_bits(pname, rb->_BaseFormat, rb->Format);
      break;
   case GL_RENDERBUFFER_SAMPLES:
      if ((_mesa_is_desktop_gl(ctx) && ctx->Extensions.ARB_framebuffer_object)
          || _mesa_is_gles3(ctx)) {
         *params = rb->NumSamples;
         break;
      }
      /* fallthrough */
   default:
      _mesa_error(ctx, GL_INVALID_ENUM, "%s(invalid pname=%s)", func,
                  _mesa_enum_to_string(pname));
      return;
   }
}


void GLAPIENTRY
_mesa_GetRenderbufferParameteriv(GLenum target, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);

   if (target != GL_RENDERBUFFER_EXT) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glGetRenderbufferParameterivEXT(target)");
      return;
   }

   if (!ctx->CurrentRenderbuffer) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "glGetRenderbufferParameterivEXT"
                  "(no renderbuffer bound)");
      return;
   }

   get_render_buffer_parameteriv(ctx, ctx->CurrentRenderbuffer, pname,
                                 params, "glGetRenderbufferParameteriv");
}


void GLAPIENTRY
_mesa_GetNamedRenderbufferParameteriv(GLuint renderbuffer, GLenum pname,
                                      GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);

   struct gl_renderbuffer *rb = _mesa_lookup_renderbuffer(ctx, renderbuffer);
   if (!rb || rb == &DummyRenderbuffer) {
      /* ID was reserved, but no real renderbuffer object made yet */
      _mesa_error(ctx, GL_INVALID_OPERATION, "glGetNamedRenderbufferParameteriv"
                  "(invalid renderbuffer %i)", renderbuffer);
      return;
   }

   get_render_buffer_parameteriv(ctx, rb, pname, params,
                                 "glGetNamedRenderbufferParameteriv");
}


GLboolean GLAPIENTRY
_mesa_IsFramebuffer(GLuint framebuffer)
{
   GET_CURRENT_CONTEXT(ctx);
   ASSERT_OUTSIDE_BEGIN_END_WITH_RETVAL(ctx, GL_FALSE);
   if (framebuffer) {
      struct gl_framebuffer *rb = _mesa_lookup_framebuffer(ctx, framebuffer);
      if (rb != NULL && rb != &DummyFramebuffer)
         return GL_TRUE;
   }
   return GL_FALSE;
}


/**
 * Check if any of the attachments of the given framebuffer are textures
 * (render to texture).  Call ctx->Driver.RenderTexture() for such
 * attachments.
 */
static void
check_begin_texture_render(struct gl_context *ctx, struct gl_framebuffer *fb)
{
   GLuint i;
   assert(ctx->Driver.RenderTexture);

   if (_mesa_is_winsys_fbo(fb))
      return; /* can't render to texture with winsys framebuffers */

   for (i = 0; i < BUFFER_COUNT; i++) {
      struct gl_renderbuffer_attachment *att = fb->Attachment + i;
      if (att->Texture && att->Renderbuffer->TexImage
          && driver_RenderTexture_is_safe(att)) {
         ctx->Driver.RenderTexture(ctx, fb, att);
      }
   }
}


/**
 * Examine all the framebuffer's attachments to see if any are textures.
 * If so, call ctx->Driver.FinishRenderTexture() for each texture to
 * notify the device driver that the texture image may have changed.
 */
static void
check_end_texture_render(struct gl_context *ctx, struct gl_framebuffer *fb)
{
   /* Skip if we know NeedsFinishRenderTexture won't be set. */
   if (_mesa_is_winsys_fbo(fb) && !ctx->Driver.BindRenderbufferTexImage)
      return;

   if (ctx->Driver.FinishRenderTexture) {
      GLuint i;
      for (i = 0; i < BUFFER_COUNT; i++) {
         struct gl_renderbuffer_attachment *att = fb->Attachment + i;
         struct gl_renderbuffer *rb = att->Renderbuffer;
         if (rb && rb->NeedsFinishRenderTexture) {
            ctx->Driver.FinishRenderTexture(ctx, rb);
         }
      }
   }
}


static void
bind_framebuffer(GLenum target, GLuint framebuffer, bool allow_user_names)
{
   struct gl_framebuffer *newDrawFb, *newReadFb;
   GLboolean bindReadBuf, bindDrawBuf;
   GET_CURRENT_CONTEXT(ctx);

   switch (target) {
   case GL_DRAW_FRAMEBUFFER_EXT:
      bindDrawBuf = GL_TRUE;
      bindReadBuf = GL_FALSE;
      break;
   case GL_READ_FRAMEBUFFER_EXT:
      bindDrawBuf = GL_FALSE;
      bindReadBuf = GL_TRUE;
      break;
   case GL_FRAMEBUFFER_EXT:
      bindDrawBuf = GL_TRUE;
      bindReadBuf = GL_TRUE;
      break;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM, "glBindFramebufferEXT(target)");
      return;
   }

   if (framebuffer) {
      /* Binding a user-created framebuffer object */
      newDrawFb = _mesa_lookup_framebuffer(ctx, framebuffer);
      if (newDrawFb == &DummyFramebuffer) {
         /* ID was reserved, but no real framebuffer object made yet */
         newDrawFb = NULL;
      }
      else if (!newDrawFb && !allow_user_names) {
         /* All FBO IDs must be Gen'd */
         _mesa_error(ctx, GL_INVALID_OPERATION, "glBindFramebuffer(buffer)");
         return;
      }

      if (!newDrawFb) {
         /* create new framebuffer object */
         newDrawFb = ctx->Driver.NewFramebuffer(ctx, framebuffer);
         if (!newDrawFb) {
            _mesa_error(ctx, GL_OUT_OF_MEMORY, "glBindFramebufferEXT");
            return;
         }
         _mesa_HashInsert(ctx->Shared->FrameBuffers, framebuffer, newDrawFb);
      }
      newReadFb = newDrawFb;
   }
   else {
      /* Binding the window system framebuffer (which was originally set
       * with MakeCurrent).
       */
      newDrawFb = ctx->WinSysDrawBuffer;
      newReadFb = ctx->WinSysReadBuffer;
   }

   _mesa_bind_framebuffers(ctx,
                           bindDrawBuf ? newDrawFb : ctx->DrawBuffer,
                           bindReadBuf ? newReadFb : ctx->ReadBuffer);
}

void
_mesa_bind_framebuffers(struct gl_context *ctx,
                        struct gl_framebuffer *newDrawFb,
                        struct gl_framebuffer *newReadFb)
{
   struct gl_framebuffer *const oldDrawFb = ctx->DrawBuffer;
   struct gl_framebuffer *const oldReadFb = ctx->ReadBuffer;
   const bool bindDrawBuf = oldDrawFb != newDrawFb;
   const bool bindReadBuf = oldReadFb != newReadFb;

   assert(newDrawFb);
   assert(newDrawFb != &DummyFramebuffer);

   /*
    * OK, now bind the new Draw/Read framebuffers, if they're changing.
    *
    * We also check if we're beginning and/or ending render-to-texture.
    * When a framebuffer with texture attachments is unbound, call
    * ctx->Driver.FinishRenderTexture().
    * When a framebuffer with texture attachments is bound, call
    * ctx->Driver.RenderTexture().
    *
    * Note that if the ReadBuffer has texture attachments we don't consider
    * that a render-to-texture case.
    */
   if (bindReadBuf) {
      FLUSH_VERTICES(ctx, _NEW_BUFFERS);

      /* check if old readbuffer was render-to-texture */
      check_end_texture_render(ctx, oldReadFb);

      _mesa_reference_framebuffer(&ctx->ReadBuffer, newReadFb);
   }

   if (bindDrawBuf) {
      FLUSH_VERTICES(ctx, _NEW_BUFFERS);

      /* check if old framebuffer had any texture attachments */
      if (oldDrawFb)
         check_end_texture_render(ctx, oldDrawFb);

      /* check if newly bound framebuffer has any texture attachments */
      check_begin_texture_render(ctx, newDrawFb);

      _mesa_reference_framebuffer(&ctx->DrawBuffer, newDrawFb);
   }

   if ((bindDrawBuf || bindReadBuf) && ctx->Driver.BindFramebuffer) {
      /* The few classic drivers that actually hook this function really only
       * want to know if the draw framebuffer changed.
       */
      ctx->Driver.BindFramebuffer(ctx,
                                  bindDrawBuf ? GL_FRAMEBUFFER : GL_READ_FRAMEBUFFER,
                                  newDrawFb, newReadFb);
   }
}

void GLAPIENTRY
_mesa_BindFramebuffer(GLenum target, GLuint framebuffer)
{
   GET_CURRENT_CONTEXT(ctx);

   /* OpenGL ES glBindFramebuffer and glBindFramebufferOES use this same entry
    * point, but they allow the use of user-generated names.
    */
   bind_framebuffer(target, framebuffer, _mesa_is_gles(ctx));
}


void GLAPIENTRY
_mesa_BindFramebufferEXT(GLenum target, GLuint framebuffer)
{
   /* This function should not be in the dispatch table for core profile /
    * OpenGL 3.1, so execution should never get here in those cases -- no
    * need for an explicit test.
    */
   bind_framebuffer(target, framebuffer, true);
}


void GLAPIENTRY
_mesa_DeleteFramebuffers(GLsizei n, const GLuint *framebuffers)
{
   GLint i;
   GET_CURRENT_CONTEXT(ctx);

   if (n < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glDeleteFramebuffers(n < 0)");
      return;
   }

   FLUSH_VERTICES(ctx, _NEW_BUFFERS);

   for (i = 0; i < n; i++) {
      if (framebuffers[i] > 0) {
         struct gl_framebuffer *fb;
         fb = _mesa_lookup_framebuffer(ctx, framebuffers[i]);
         if (fb) {
            assert(fb == &DummyFramebuffer || fb->Name == framebuffers[i]);

            /* check if deleting currently bound framebuffer object */
            if (fb == ctx->DrawBuffer) {
               /* bind default */
               assert(fb->RefCount >= 2);
               _mesa_BindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
            }
            if (fb == ctx->ReadBuffer) {
               /* bind default */
               assert(fb->RefCount >= 2);
               _mesa_BindFramebuffer(GL_READ_FRAMEBUFFER, 0);
            }

            /* remove from hash table immediately, to free the ID */
            _mesa_HashRemove(ctx->Shared->FrameBuffers, framebuffers[i]);

            if (fb != &DummyFramebuffer) {
               /* But the object will not be freed until it's no longer
                * bound in any context.
                */
               _mesa_reference_framebuffer(&fb, NULL);
            }
         }
      }
   }
}


/**
 * This is the implementation for glGenFramebuffers and glCreateFramebuffers.
 * It is not exposed to the rest of Mesa to encourage the use of
 * nameless buffers in driver internals.
 */
static void
create_framebuffers(GLsizei n, GLuint *framebuffers, bool dsa)
{
   GET_CURRENT_CONTEXT(ctx);
   GLuint first;
   GLint i;
   struct gl_framebuffer *fb;

   const char *func = dsa ? "glCreateFramebuffers" : "glGenFramebuffers";

   if (n < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(n < 0)", func);
      return;
   }

   if (!framebuffers)
      return;

   _mesa_HashLockMutex(ctx->Shared->FrameBuffers);

   first = _mesa_HashFindFreeKeyBlock(ctx->Shared->FrameBuffers, n);

   for (i = 0; i < n; i++) {
      GLuint name = first + i;
      framebuffers[i] = name;

      if (dsa) {
         fb = ctx->Driver.NewFramebuffer(ctx, framebuffers[i]);
         if (!fb) {
            _mesa_HashUnlockMutex(ctx->Shared->FrameBuffers);
            _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s", func);
            return;
         }
      }
      else
         fb = &DummyFramebuffer;

      _mesa_HashInsertLocked(ctx->Shared->FrameBuffers, name, fb);
   }

   _mesa_HashUnlockMutex(ctx->Shared->FrameBuffers);
}


void GLAPIENTRY
_mesa_GenFramebuffers(GLsizei n, GLuint *framebuffers)
{
   create_framebuffers(n, framebuffers, false);
}


void GLAPIENTRY
_mesa_CreateFramebuffers(GLsizei n, GLuint *framebuffers)
{
   create_framebuffers(n, framebuffers, true);
}


GLenum
_mesa_check_framebuffer_status(struct gl_context *ctx,
                               struct gl_framebuffer *buffer)
{
   ASSERT_OUTSIDE_BEGIN_END_WITH_RETVAL(ctx, 0);

   if (_mesa_is_winsys_fbo(buffer)) {
      /* EGL_KHR_surfaceless_context allows the winsys FBO to be incomplete. */
      if (buffer != &IncompleteFramebuffer) {
         return GL_FRAMEBUFFER_COMPLETE_EXT;
      } else {
         return GL_FRAMEBUFFER_UNDEFINED;
      }
   }

   /* No need to flush here */

   if (buffer->_Status != GL_FRAMEBUFFER_COMPLETE) {
      _mesa_test_framebuffer_completeness(ctx, buffer);
   }

   return buffer->_Status;
}


GLenum GLAPIENTRY
_mesa_CheckFramebufferStatus_no_error(GLenum target)
{
   GET_CURRENT_CONTEXT(ctx);

   struct gl_framebuffer *fb = get_framebuffer_target(ctx, target);
   return _mesa_check_framebuffer_status(ctx, fb);
}


GLenum GLAPIENTRY
_mesa_CheckFramebufferStatus(GLenum target)
{
   struct gl_framebuffer *fb;
   GET_CURRENT_CONTEXT(ctx);

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx, "glCheckFramebufferStatus(%s)\n",
                  _mesa_enum_to_string(target));

   fb = get_framebuffer_target(ctx, target);
   if (!fb) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glCheckFramebufferStatus(invalid target %s)",
                  _mesa_enum_to_string(target));
      return 0;
   }

   return _mesa_check_framebuffer_status(ctx, fb);
}


GLenum GLAPIENTRY
_mesa_CheckNamedFramebufferStatus(GLuint framebuffer, GLenum target)
{
   struct gl_framebuffer *fb;
   GET_CURRENT_CONTEXT(ctx);

   /* Validate the target (for conformance's sake) and grab a reference to the
    * default framebuffer in case framebuffer = 0.
    * Section 9.4 Framebuffer Completeness of the OpenGL 4.5 core spec
    * (30.10.2014, PDF page 336) says:
    *    "If framebuffer is zero, then the status of the default read or
    *    draw framebuffer (as determined by target) is returned."
    */
   switch (target) {
      case GL_DRAW_FRAMEBUFFER:
      case GL_FRAMEBUFFER:
         fb = ctx->WinSysDrawBuffer;
         break;
      case GL_READ_FRAMEBUFFER:
         fb = ctx->WinSysReadBuffer;
         break;
      default:
         _mesa_error(ctx, GL_INVALID_ENUM,
                     "glCheckNamedFramebufferStatus(invalid target %s)",
                     _mesa_enum_to_string(target));
         return 0;
   }

   if (framebuffer) {
      fb = _mesa_lookup_framebuffer_err(ctx, framebuffer,
                                        "glCheckNamedFramebufferStatus");
      if (!fb)
         return 0;
   }

   return _mesa_check_framebuffer_status(ctx, fb);
}


/**
 * Replicate the src attachment point. Used by framebuffer_texture() when
 * the same texture is attached at GL_DEPTH_ATTACHMENT and
 * GL_STENCIL_ATTACHMENT.
 */
static void
reuse_framebuffer_texture_attachment(struct gl_framebuffer *fb,
                                     gl_buffer_index dst,
                                     gl_buffer_index src)
{
   struct gl_renderbuffer_attachment *dst_att = &fb->Attachment[dst];
   struct gl_renderbuffer_attachment *src_att = &fb->Attachment[src];

   assert(src_att->Texture != NULL);
   assert(src_att->Renderbuffer != NULL);

   _mesa_reference_texobj(&dst_att->Texture, src_att->Texture);
   _mesa_reference_renderbuffer(&dst_att->Renderbuffer, src_att->Renderbuffer);
   dst_att->Type = src_att->Type;
   dst_att->Complete = src_att->Complete;
   dst_att->TextureLevel = src_att->TextureLevel;
   dst_att->CubeMapFace = src_att->CubeMapFace;
   dst_att->Zoffset = src_att->Zoffset;
   dst_att->Layered = src_att->Layered;
}


static struct gl_texture_object *
get_texture_for_framebuffer(struct gl_context *ctx, GLuint texture)
{
   if (!texture)
      return NULL;

   return _mesa_lookup_texture(ctx, texture);
}


/**
 * Common code called by gl*FramebufferTexture*() to retrieve the correct
 * texture object pointer.
 *
 * \param texObj where the pointer to the texture object is returned.  Note
 * that a successful call may return texObj = NULL.
 *
 * \return true if no errors, false if errors
 */
static bool
get_texture_for_framebuffer_err(struct gl_context *ctx, GLuint texture,
                                bool layered, const char *caller,
                                struct gl_texture_object **texObj)
{
   *texObj = NULL; /* This will get returned if texture = 0. */

   if (!texture)
      return true;

   *texObj = _mesa_lookup_texture(ctx, texture);
   if (*texObj == NULL || (*texObj)->Target == 0) {
      /* Can't render to a non-existent texture object.
       *
       * The OpenGL 4.5 core spec (02.02.2015) in Section 9.2 Binding and
       * Managing Framebuffer Objects specifies a different error
       * depending upon the calling function (PDF pages 325-328).
       * *FramebufferTexture (where layered = GL_TRUE) throws invalid
       * value, while the other commands throw invalid operation (where
       * layered = GL_FALSE).
       */
      const GLenum error = layered ? GL_INVALID_VALUE :
                           GL_INVALID_OPERATION;
      _mesa_error(ctx, error,
                  "%s(non-existent texture %u)", caller, texture);
      return false;
   }

   return true;
}


/**
 * Common code called by gl*FramebufferTexture() to verify the texture target
 * and decide whether or not the attachment should truly be considered
 * layered.
 *
 * \param layered true if attachment should be considered layered, false if
 * not
 *
 * \return true if no errors, false if errors
 */
static bool
check_layered_texture_target(struct gl_context *ctx, GLenum target,
                             const char *caller, GLboolean *layered)
{
   *layered = GL_TRUE;

   switch (target) {
   case GL_TEXTURE_3D:
   case GL_TEXTURE_1D_ARRAY_EXT:
   case GL_TEXTURE_2D_ARRAY_EXT:
   case GL_TEXTURE_CUBE_MAP:
   case GL_TEXTURE_CUBE_MAP_ARRAY:
   case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
      return true;
   case GL_TEXTURE_1D:
   case GL_TEXTURE_2D:
   case GL_TEXTURE_RECTANGLE:
   case GL_TEXTURE_2D_MULTISAMPLE:
      /* These texture types are valid to pass to
       * glFramebufferTexture(), but since they aren't layered, it
       * is equivalent to calling glFramebufferTexture{1D,2D}().
       */
      *layered = GL_FALSE;
      return true;
   }

   _mesa_error(ctx, GL_INVALID_OPERATION,
               "%s(invalid texture target %s)", caller,
               _mesa_enum_to_string(target));
   return false;
}


/**
 * Common code called by gl*FramebufferTextureLayer() to verify the texture
 * target.
 *
 * \return true if no errors, false if errors
 */
static bool
check_texture_target(struct gl_context *ctx, GLenum target,
                     const char *caller)
{
   /* We're being called by glFramebufferTextureLayer().
    * The only legal texture types for that function are 3D,
    * cube-map, and 1D/2D/cube-map array textures.
    *
    * We don't need to check for GL_ARB_texture_cube_map_array because the
    * application wouldn't have been able to create a texture with a
    * GL_TEXTURE_CUBE_MAP_ARRAY target if the extension were not enabled.
    */
   switch (target) {
   case GL_TEXTURE_3D:
   case GL_TEXTURE_1D_ARRAY:
   case GL_TEXTURE_2D_ARRAY:
   case GL_TEXTURE_CUBE_MAP_ARRAY:
   case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
      return true;
   case GL_TEXTURE_CUBE_MAP:
      /* We don't need to check the extension (GL_ARB_direct_state_access) or
       * GL version (4.5) for GL_TEXTURE_CUBE_MAP because DSA is always
       * enabled in core profile.  This can be called from
       * _mesa_FramebufferTextureLayer in compatibility profile (OpenGL 3.0),
       * so we do have to check the profile.
       */
      return ctx->API == API_OPENGL_CORE;
   }

   _mesa_error(ctx, GL_INVALID_OPERATION,
               "%s(invalid texture target %s)", caller,
               _mesa_enum_to_string(target));
   return false;
}


/**
 * Common code called by glFramebufferTexture*D() to verify the texture
 * target.
 *
 * \return true if no errors, false if errors
 */
static bool
check_textarget(struct gl_context *ctx, int dims, GLenum target,
                GLenum textarget, const char *caller)
{
   bool err = false;

   switch (textarget) {
   case GL_TEXTURE_1D:
      err = dims != 1;
      break;
   case GL_TEXTURE_1D_ARRAY:
      err = dims != 1 || !ctx->Extensions.EXT_texture_array;
      break;
   case GL_TEXTURE_2D:
      err = dims != 2;
      break;
   case GL_TEXTURE_2D_ARRAY:
      err = dims != 2 || !ctx->Extensions.EXT_texture_array ||
            (_mesa_is_gles(ctx) && ctx->Version < 30);
      break;
   case GL_TEXTURE_2D_MULTISAMPLE:
   case GL_TEXTURE_2D_MULTISAMPLE_ARRAY:
      err = dims != 2 ||
            !ctx->Extensions.ARB_texture_multisample ||
            (_mesa_is_gles(ctx) && ctx->Version < 31);
      break;
   case GL_TEXTURE_RECTANGLE:
      err = dims != 2 || _mesa_is_gles(ctx) ||
            !ctx->Extensions.NV_texture_rectangle;
      break;
   case GL_TEXTURE_CUBE_MAP:
   case GL_TEXTURE_CUBE_MAP_ARRAY:
      err = true;
      break;
   case GL_TEXTURE_CUBE_MAP_POSITIVE_X:
   case GL_TEXTURE_CUBE_MAP_NEGATIVE_X:
   case GL_TEXTURE_CUBE_MAP_POSITIVE_Y:
   case GL_TEXTURE_CUBE_MAP_NEGATIVE_Y:
   case GL_TEXTURE_CUBE_MAP_POSITIVE_Z:
   case GL_TEXTURE_CUBE_MAP_NEGATIVE_Z:
      err = dims != 2 || !ctx->Extensions.ARB_texture_cube_map;
      break;
   case GL_TEXTURE_3D:
      err = dims != 3;
      break;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "%s(unknown textarget 0x%x)", caller, textarget);
      return false;
   }

   if (err) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(invalid textarget %s)",
                  caller, _mesa_enum_to_string(textarget));
      return false;
   }

   /* Make sure textarget is consistent with the texture's type */
   err = (target == GL_TEXTURE_CUBE_MAP) ?
          !_mesa_is_cube_face(textarget): (target != textarget);

   if (err) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(mismatched texture target)", caller);
      return false;
   }

   return true;
}


/**
 * Common code called by gl*FramebufferTextureLayer() and
 * glFramebufferTexture3D() to validate the layer.
 *
 * \return true if no errors, false if errors
 */
static bool
check_layer(struct gl_context *ctx, GLenum target, GLint layer,
            const char *caller)
{
   /* Page 306 (page 328 of the PDF) of the OpenGL 4.5 (Core Profile)
    * spec says:
    *
    *    "An INVALID_VALUE error is generated if texture is non-zero
    *     and layer is negative."
    */
   if (layer < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(layer %d < 0)", caller, layer);
      return false;
   }

   if (target == GL_TEXTURE_3D) {
      const GLuint maxSize = 1 << (ctx->Const.Max3DTextureLevels - 1);
      if (layer >= maxSize) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "%s(invalid layer %u)", caller, layer);
         return false;
      }
   }
   else if ((target == GL_TEXTURE_1D_ARRAY) ||
            (target == GL_TEXTURE_2D_ARRAY) ||
            (target == GL_TEXTURE_CUBE_MAP_ARRAY) ||
            (target == GL_TEXTURE_2D_MULTISAMPLE_ARRAY)) {
      if (layer >= ctx->Const.MaxArrayTextureLayers) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "%s(layer %u >= GL_MAX_ARRAY_TEXTURE_LAYERS)",
                     caller, layer);
         return false;
      }
   }
   else if (target == GL_TEXTURE_CUBE_MAP) {
      if (layer >= 6) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "%s(layer %u >= 6)", caller, layer);
         return false;
      }
   }

   return true;
}


/**
 * Common code called by all gl*FramebufferTexture*() entry points to verify
 * the level.
 *
 * \return true if no errors, false if errors
 */
static bool
check_level(struct gl_context *ctx, struct gl_texture_object *texObj,
            GLenum target, GLint level, const char *caller)
{
   /* Section 9.2.8 of the OpenGL 4.6 specification says:
    *
    *    "If texture refers to an immutable-format texture, level must be
    *     greater than or equal to zero and smaller than the value of
    *     TEXTURE_VIEW_NUM_LEVELS for texture."
    */
   const int max_levels = texObj->Immutable ? texObj->ImmutableLevels :
                          _mesa_max_texture_levels(ctx, target);

   if (level < 0 || level >= max_levels) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(invalid level %d)", caller, level);
      return false;
   }

   return true;
}


struct gl_renderbuffer_attachment *
_mesa_get_and_validate_attachment(struct gl_context *ctx,
                                  struct gl_framebuffer *fb,
                                  GLenum attachment, const char *caller)
{
   /* The window-system framebuffer object is immutable */
   if (_mesa_is_winsys_fbo(fb)) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(window-system framebuffer)",
                  caller);
      return NULL;
   }

   /* Not a hash lookup, so we can afford to get the attachment here. */
   bool is_color_attachment;
   struct gl_renderbuffer_attachment *att =
      get_attachment(ctx, fb, attachment, &is_color_attachment);
   if (att == NULL) {
      if (is_color_attachment) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "%s(invalid color attachment %s)", caller,
                     _mesa_enum_to_string(attachment));
      } else {
         _mesa_error(ctx, GL_INVALID_ENUM,
                     "%s(invalid attachment %s)", caller,
                     _mesa_enum_to_string(attachment));
      }
      return NULL;
   }

   return att;
}


void
_mesa_framebuffer_texture(struct gl_context *ctx, struct gl_framebuffer *fb,
                          GLenum attachment,
                          struct gl_renderbuffer_attachment *att,
                          struct gl_texture_object *texObj, GLenum textarget,
                          GLint level, GLuint layer, GLboolean layered)
{
   FLUSH_VERTICES(ctx, _NEW_BUFFERS);

   simple_mtx_lock(&fb->Mutex);
   if (texObj) {
      if (attachment == GL_DEPTH_ATTACHMENT &&
          texObj == fb->Attachment[BUFFER_STENCIL].Texture &&
          level == fb->Attachment[BUFFER_STENCIL].TextureLevel &&
          _mesa_tex_target_to_face(textarget) ==
          fb->Attachment[BUFFER_STENCIL].CubeMapFace &&
          layer == fb->Attachment[BUFFER_STENCIL].Zoffset) {
         /* The texture object is already attached to the stencil attachment
          * point. Don't create a new renderbuffer; just reuse the stencil
          * attachment's. This is required to prevent a GL error in
          * glGetFramebufferAttachmentParameteriv(GL_DEPTH_STENCIL).
          */
         reuse_framebuffer_texture_attachment(fb, BUFFER_DEPTH,
                                              BUFFER_STENCIL);
      } else if (attachment == GL_STENCIL_ATTACHMENT &&
                 texObj == fb->Attachment[BUFFER_DEPTH].Texture &&
                 level == fb->Attachment[BUFFER_DEPTH].TextureLevel &&
                 _mesa_tex_target_to_face(textarget) ==
                 fb->Attachment[BUFFER_DEPTH].CubeMapFace &&
                 layer == fb->Attachment[BUFFER_DEPTH].Zoffset) {
         /* As above, but with depth and stencil transposed. */
         reuse_framebuffer_texture_attachment(fb, BUFFER_STENCIL,
                                              BUFFER_DEPTH);
      } else {
         set_texture_attachment(ctx, fb, att, texObj, textarget,
                                level, layer, layered);

         if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
            /* Above we created a new renderbuffer and attached it to the
             * depth attachment point. Now attach it to the stencil attachment
             * point too.
             */
            assert(att == &fb->Attachment[BUFFER_DEPTH]);
            reuse_framebuffer_texture_attachment(fb,BUFFER_STENCIL,
                                                 BUFFER_DEPTH);
         }
      }

      /* Set the render-to-texture flag.  We'll check this flag in
       * glTexImage() and friends to determine if we need to revalidate
       * any FBOs that might be rendering into this texture.
       * This flag never gets cleared since it's non-trivial to determine
       * when all FBOs might be done rendering to this texture.  That's OK
       * though since it's uncommon to render to a texture then repeatedly
       * call glTexImage() to change images in the texture.
       */
      texObj->_RenderToTexture = GL_TRUE;
   }
   else {
      remove_attachment(ctx, att);
      if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
         assert(att == &fb->Attachment[BUFFER_DEPTH]);
         remove_attachment(ctx, &fb->Attachment[BUFFER_STENCIL]);
      }
   }

   invalidate_framebuffer(fb);

   simple_mtx_unlock(&fb->Mutex);
}


static void
framebuffer_texture_with_dims_no_error(GLenum target, GLenum attachment,
                                       GLenum textarget, GLuint texture,
                                       GLint level, GLint layer)
{
   GET_CURRENT_CONTEXT(ctx);

   /* Get the framebuffer object */
   struct gl_framebuffer *fb = get_framebuffer_target(ctx, target);

   /* Get the texture object */
   struct gl_texture_object *texObj =
      get_texture_for_framebuffer(ctx, texture);

   struct gl_renderbuffer_attachment *att =
      get_attachment(ctx, fb, attachment, NULL);

   _mesa_framebuffer_texture(ctx, fb, attachment, att, texObj, textarget,
                             level, layer, GL_FALSE);
}


static void
framebuffer_texture_with_dims(int dims, GLenum target,
                              GLenum attachment, GLenum textarget,
                              GLuint texture, GLint level, GLint layer,
                              const char *caller)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_framebuffer *fb;
   struct gl_texture_object *texObj;

   /* Get the framebuffer object */
   fb = get_framebuffer_target(ctx, target);
   if (!fb) {
      _mesa_error(ctx, GL_INVALID_ENUM, "%s(invalid target %s)", caller,
                  _mesa_enum_to_string(target));
      return;
   }

   /* Get the texture object */
   if (!get_texture_for_framebuffer_err(ctx, texture, false, caller, &texObj))
      return;

   if (texObj) {
      if (!check_textarget(ctx, dims, texObj->Target, textarget, caller))
         return;

      if ((dims == 3) && !check_layer(ctx, texObj->Target, layer, caller))
         return;

      if (!check_level(ctx, texObj, textarget, level, caller))
         return;
   }

   struct gl_renderbuffer_attachment *att =
      _mesa_get_and_validate_attachment(ctx, fb, attachment, caller);
   if (!att)
      return;

   _mesa_framebuffer_texture(ctx, fb, attachment, att, texObj, textarget,
                             level, layer, GL_FALSE);
}


void GLAPIENTRY
_mesa_FramebufferTexture1D_no_error(GLenum target, GLenum attachment,
                                    GLenum textarget, GLuint texture,
                                    GLint level)
{
   framebuffer_texture_with_dims_no_error(target, attachment, textarget,
                                          texture, level, 0);
}


void GLAPIENTRY
_mesa_FramebufferTexture1D(GLenum target, GLenum attachment,
                           GLenum textarget, GLuint texture, GLint level)
{
   framebuffer_texture_with_dims(1, target, attachment, textarget, texture,
                                 level, 0, "glFramebufferTexture1D");
}


void GLAPIENTRY
_mesa_FramebufferTexture2D_no_error(GLenum target, GLenum attachment,
                                    GLenum textarget, GLuint texture,
                                    GLint level)
{
   framebuffer_texture_with_dims_no_error(target, attachment, textarget,
                                          texture, level, 0);
}


void GLAPIENTRY
_mesa_FramebufferTexture2D(GLenum target, GLenum attachment,
                           GLenum textarget, GLuint texture, GLint level)
{
   framebuffer_texture_with_dims(2, target, attachment, textarget, texture,
                                 level, 0, "glFramebufferTexture2D");
}


void GLAPIENTRY
_mesa_FramebufferTexture3D_no_error(GLenum target, GLenum attachment,
                                    GLenum textarget, GLuint texture,
                                    GLint level, GLint layer)
{
   framebuffer_texture_with_dims_no_error(target, attachment, textarget,
                                          texture, level, layer);
}


void GLAPIENTRY
_mesa_FramebufferTexture3D(GLenum target, GLenum attachment,
                           GLenum textarget, GLuint texture,
                           GLint level, GLint layer)
{
   framebuffer_texture_with_dims(3, target, attachment, textarget, texture,
                                 level, layer, "glFramebufferTexture3D");
}


static ALWAYS_INLINE void
frame_buffer_texture(GLuint framebuffer, GLenum target,
                     GLenum attachment, GLuint texture,
                     GLint level, GLint layer, const char *func,
                     bool dsa, bool no_error, bool check_layered)
{
   GET_CURRENT_CONTEXT(ctx);
   GLboolean layered = GL_FALSE;

   if (!no_error && check_layered) {
      if (!_mesa_has_geometry_shaders(ctx)) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "unsupported function (%s) called", func);
         return;
      }
   }

   /* Get the framebuffer object */
   struct gl_framebuffer *fb;
   if (no_error) {
      if (dsa) {
         fb = _mesa_lookup_framebuffer(ctx, framebuffer);
      } else {
         fb = get_framebuffer_target(ctx, target);
      }
   } else {
      if (dsa) {
         fb = _mesa_lookup_framebuffer_err(ctx, framebuffer, func);
         if (!fb)
            return;
      } else {
         fb = get_framebuffer_target(ctx, target);
         if (!fb) {
            _mesa_error(ctx, GL_INVALID_ENUM, "%s(invalid target %s)",
                        func, _mesa_enum_to_string(target));
            return;
         }
      }
   }

   /* Get the texture object and framebuffer attachment*/
   struct gl_renderbuffer_attachment *att;
   struct gl_texture_object *texObj;
   if (no_error) {
      texObj = get_texture_for_framebuffer(ctx, texture);
      att = get_attachment(ctx, fb, attachment, NULL);
   } else {
      if (!get_texture_for_framebuffer_err(ctx, texture, check_layered, func,
                                           &texObj))
         return;

      att = _mesa_get_and_validate_attachment(ctx, fb, attachment, func);
      if (!att)
         return;
   }

   GLenum textarget = 0;
   if (texObj) {
      if (check_layered) {
         /* We do this regardless of no_error because this sets layered */
         if (!check_layered_texture_target(ctx, texObj->Target, func,
                                           &layered))
            return;
      }

      if (!no_error) {
         if (!check_layered) {
            if (!check_texture_target(ctx, texObj->Target, func))
               return;

            if (!check_layer(ctx, texObj->Target, layer, func))
               return;
         }

         if (!check_level(ctx, texObj, texObj->Target, level, func))
            return;
      }

      if (!check_layered && texObj->Target == GL_TEXTURE_CUBE_MAP) {
         assert(layer >= 0 && layer < 6);
         textarget = GL_TEXTURE_CUBE_MAP_POSITIVE_X + layer;
         layer = 0;
      }
   }

   _mesa_framebuffer_texture(ctx, fb, attachment, att, texObj, textarget,
                             level, layer, layered);
}

void GLAPIENTRY
_mesa_FramebufferTextureLayer_no_error(GLenum target, GLenum attachment,
                                       GLuint texture, GLint level,
                                       GLint layer)
{
   frame_buffer_texture(0, target, attachment, texture, level, layer,
                        "glFramebufferTextureLayer", false, true, false);
}


void GLAPIENTRY
_mesa_FramebufferTextureLayer(GLenum target, GLenum attachment,
                              GLuint texture, GLint level, GLint layer)
{
   frame_buffer_texture(0, target, attachment, texture, level, layer,
                        "glFramebufferTextureLayer", false, false, false);
}


void GLAPIENTRY
_mesa_NamedFramebufferTextureLayer_no_error(GLuint framebuffer,
                                            GLenum attachment,
                                            GLuint texture, GLint level,
                                            GLint layer)
{
   frame_buffer_texture(framebuffer, 0, attachment, texture, level, layer,
                        "glNamedFramebufferTextureLayer", true, true, false);
}


void GLAPIENTRY
_mesa_NamedFramebufferTextureLayer(GLuint framebuffer, GLenum attachment,
                                   GLuint texture, GLint level, GLint layer)
{
   frame_buffer_texture(framebuffer, 0, attachment, texture, level, layer,
                        "glNamedFramebufferTextureLayer", true, false, false);
}


void GLAPIENTRY
_mesa_FramebufferTexture_no_error(GLenum target, GLenum attachment,
                                  GLuint texture, GLint level)
{
   frame_buffer_texture(0, target, attachment, texture, level, 0,
                        "glFramebufferTexture", false, true, true);
}


void GLAPIENTRY
_mesa_FramebufferTexture(GLenum target, GLenum attachment,
                         GLuint texture, GLint level)
{
   frame_buffer_texture(0, target, attachment, texture, level, 0,
                        "glFramebufferTexture", false, false, true);
}

void GLAPIENTRY
_mesa_NamedFramebufferTexture_no_error(GLuint framebuffer, GLenum attachment,
                                       GLuint texture, GLint level)
{
   frame_buffer_texture(framebuffer, 0, attachment, texture, level, 0,
                        "glNamedFramebufferTexture", true, true, true);
}


void GLAPIENTRY
_mesa_NamedFramebufferTexture(GLuint framebuffer, GLenum attachment,
                              GLuint texture, GLint level)
{
   frame_buffer_texture(framebuffer, 0, attachment, texture, level, 0,
                        "glNamedFramebufferTexture", true, false, true);
}


void
_mesa_framebuffer_renderbuffer(struct gl_context *ctx,
                               struct gl_framebuffer *fb,
                               GLenum attachment,
                               struct gl_renderbuffer *rb)
{
   assert(!_mesa_is_winsys_fbo(fb));

   FLUSH_VERTICES(ctx, _NEW_BUFFERS);

   assert(ctx->Driver.FramebufferRenderbuffer);
   ctx->Driver.FramebufferRenderbuffer(ctx, fb, attachment, rb);

   /* Some subsequent GL commands may depend on the framebuffer's visual
    * after the binding is updated.  Update visual info now.
    */
   _mesa_update_framebuffer_visual(ctx, fb);
}

static ALWAYS_INLINE void
framebuffer_renderbuffer(struct gl_context *ctx, struct gl_framebuffer *fb,
                         GLenum attachment, GLenum renderbuffertarget,
                         GLuint renderbuffer, const char *func, bool no_error)
{
   struct gl_renderbuffer_attachment *att;
   struct gl_renderbuffer *rb;
   bool is_color_attachment;

   if (!no_error && renderbuffertarget != GL_RENDERBUFFER) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "%s(renderbuffertarget is not GL_RENDERBUFFER)", func);
      return;
   }

   if (renderbuffer) {
      if (!no_error) {
         rb = _mesa_lookup_renderbuffer_err(ctx, renderbuffer, func);
         if (!rb)
            return;
      } else {
         rb = _mesa_lookup_renderbuffer(ctx, renderbuffer);
      }
   } else {
      /* remove renderbuffer attachment */
      rb = NULL;
   }

   if (!no_error) {
      if (_mesa_is_winsys_fbo(fb)) {
         /* Can't attach new renderbuffers to a window system framebuffer */
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "%s(window-system framebuffer)", func);
         return;
      }

      att = get_attachment(ctx, fb, attachment, &is_color_attachment);
      if (att == NULL) {
         /*
          * From OpenGL 4.5 spec, section 9.2.7 "Attaching Renderbuffer Images
          * to a Framebuffer":
          *
          *    "An INVALID_OPERATION error is generated if attachment is
          *    COLOR_- ATTACHMENTm where m is greater than or equal to the
          *    value of MAX_COLOR_- ATTACHMENTS ."
          *
          * If we are at this point, is because the attachment is not valid, so
          * if is_color_attachment is true, is because of the previous reason.
          */
         if (is_color_attachment) {
            _mesa_error(ctx, GL_INVALID_OPERATION,
                        "%s(invalid color attachment %s)", func,
                        _mesa_enum_to_string(attachment));
         } else {
            _mesa_error(ctx, GL_INVALID_ENUM,
                        "%s(invalid attachment %s)", func,
                        _mesa_enum_to_string(attachment));
         }

         return;
      }

      if (attachment == GL_DEPTH_STENCIL_ATTACHMENT &&
          rb && rb->Format != MESA_FORMAT_NONE) {
         /* make sure the renderbuffer is a depth/stencil format */
         const GLenum baseFormat = _mesa_get_format_base_format(rb->Format);
         if (baseFormat != GL_DEPTH_STENCIL) {
            _mesa_error(ctx, GL_INVALID_OPERATION,
                        "%s(renderbuffer is not DEPTH_STENCIL format)", func);
            return;
         }
      }
   }

   _mesa_framebuffer_renderbuffer(ctx, fb, attachment, rb);
}

static void
framebuffer_renderbuffer_error(struct gl_context *ctx,
                               struct gl_framebuffer *fb, GLenum attachment,
                               GLenum renderbuffertarget,
                               GLuint renderbuffer, const char *func)
{
   framebuffer_renderbuffer(ctx, fb, attachment, renderbuffertarget,
                            renderbuffer, func, false);
}

static void
framebuffer_renderbuffer_no_error(struct gl_context *ctx,
                                  struct gl_framebuffer *fb, GLenum attachment,
                                  GLenum renderbuffertarget,
                                  GLuint renderbuffer, const char *func)
{
   framebuffer_renderbuffer(ctx, fb, attachment, renderbuffertarget,
                            renderbuffer, func, true);
}

void GLAPIENTRY
_mesa_FramebufferRenderbuffer_no_error(GLenum target, GLenum attachment,
                                       GLenum renderbuffertarget,
                                       GLuint renderbuffer)
{
   GET_CURRENT_CONTEXT(ctx);

   struct gl_framebuffer *fb = get_framebuffer_target(ctx, target);
   framebuffer_renderbuffer_no_error(ctx, fb, attachment, renderbuffertarget,
                                     renderbuffer, "glFramebufferRenderbuffer");
}

void GLAPIENTRY
_mesa_FramebufferRenderbuffer(GLenum target, GLenum attachment,
                              GLenum renderbuffertarget,
                              GLuint renderbuffer)
{
   struct gl_framebuffer *fb;
   GET_CURRENT_CONTEXT(ctx);

   fb = get_framebuffer_target(ctx, target);
   if (!fb) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glFramebufferRenderbuffer(invalid target %s)",
                  _mesa_enum_to_string(target));
      return;
   }

   framebuffer_renderbuffer_error(ctx, fb, attachment, renderbuffertarget,
                                  renderbuffer, "glFramebufferRenderbuffer");
}

void GLAPIENTRY
_mesa_NamedFramebufferRenderbuffer_no_error(GLuint framebuffer,
                                            GLenum attachment,
                                            GLenum renderbuffertarget,
                                            GLuint renderbuffer)
{
   GET_CURRENT_CONTEXT(ctx);

   struct gl_framebuffer *fb = _mesa_lookup_framebuffer(ctx, framebuffer);
   framebuffer_renderbuffer_no_error(ctx, fb, attachment, renderbuffertarget,
                                     renderbuffer,
                                     "glNamedFramebufferRenderbuffer");
}

void GLAPIENTRY
_mesa_NamedFramebufferRenderbuffer(GLuint framebuffer, GLenum attachment,
                                   GLenum renderbuffertarget,
                                   GLuint renderbuffer)
{
   struct gl_framebuffer *fb;
   GET_CURRENT_CONTEXT(ctx);

   fb = _mesa_lookup_framebuffer_err(ctx, framebuffer,
                                     "glNamedFramebufferRenderbuffer");
   if (!fb)
      return;

   framebuffer_renderbuffer_error(ctx, fb, attachment, renderbuffertarget,
                                  renderbuffer,
                                  "glNamedFramebufferRenderbuffer");
}


static void
get_framebuffer_attachment_parameter(struct gl_context *ctx,
                                     struct gl_framebuffer *buffer,
                                     GLenum attachment, GLenum pname,
                                     GLint *params, const char *caller)
{
   const struct gl_renderbuffer_attachment *att;
   bool is_color_attachment = false;
   GLenum err;

   /* The error code for an attachment type of GL_NONE differs between APIs.
    *
    * From the ES 2.0.25 specification, page 127:
    * "If the value of FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE is NONE, then
    *  querying any other pname will generate INVALID_ENUM."
    *
    * From the OpenGL 3.0 specification, page 337, or identically,
    * the OpenGL ES 3.0.4 specification, page 240:
    *
    * "If the value of FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE is NONE, no
    *  framebuffer is bound to target.  In this case querying pname
    *  FRAMEBUFFER_ATTACHMENT_OBJECT_NAME will return zero, and all other
    *  queries will generate an INVALID_OPERATION error."
    */
   err = ctx->API == API_OPENGLES2 && ctx->Version < 30 ?
      GL_INVALID_ENUM : GL_INVALID_OPERATION;

   if (_mesa_is_winsys_fbo(buffer)) {
      /* Page 126 (page 136 of the PDF) of the OpenGL ES 2.0.25 spec
       * says:
       *
       *     "If the framebuffer currently bound to target is zero, then
       *     INVALID_OPERATION is generated."
       *
       * The EXT_framebuffer_object spec has the same wording, and the
       * OES_framebuffer_object spec refers to the EXT_framebuffer_object
       * spec.
       */
      if ((!_mesa_is_desktop_gl(ctx) ||
           !ctx->Extensions.ARB_framebuffer_object)
          && !_mesa_is_gles3(ctx)) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "%s(window-system framebuffer)", caller);
         return;
      }

      if (_mesa_is_gles3(ctx) && attachment != GL_BACK &&
          attachment != GL_DEPTH && attachment != GL_STENCIL) {
         _mesa_error(ctx, GL_INVALID_ENUM,
                     "%s(invalid attachment %s)", caller,
                     _mesa_enum_to_string(attachment));
         return;
      }

      /* The specs are not clear about how to handle
       * GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME with the default framebuffer,
       * but dEQP-GLES3 expects an INVALID_ENUM error. This has also been
       * discussed in:
       *
       * https://cvs.khronos.org/bugzilla/show_bug.cgi?id=12928#c1
       * and https://bugs.freedesktop.org/show_bug.cgi?id=31947
       */
      if (pname == GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME) {
         _mesa_error(ctx, GL_INVALID_ENUM,
                     "%s(requesting GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME "
                     "when GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE is "
                     "GL_FRAMEBUFFER_DEFAULT is not allowed)", caller);
         return;
      }

      /* the default / window-system FBO */
      att = get_fb0_attachment(ctx, buffer, attachment);
   }
   else {
      /* user-created framebuffer FBO */
      att = get_attachment(ctx, buffer, attachment, &is_color_attachment);
   }

   if (att == NULL) {
      /*
       * From OpenGL 4.5 spec, section 9.2.3 "Framebuffer Object Queries":
       *
       *    "An INVALID_OPERATION error is generated if a framebuffer object
       *     is bound to target and attachment is COLOR_ATTACHMENTm where m is
       *     greater than or equal to the value of MAX_COLOR_ATTACHMENTS."
       *
       * If we are at this point, is because the attachment is not valid, so
       * if is_color_attachment is true, is because of the previous reason.
       */
      if (is_color_attachment) {
         _mesa_error(ctx, GL_INVALID_OPERATION, "%s(invalid color attachment %s)",
                     caller, _mesa_enum_to_string(attachment));
      } else {
         _mesa_error(ctx, GL_INVALID_ENUM, "%s(invalid attachment %s)", caller,
                     _mesa_enum_to_string(attachment));
      }
      return;
   }

   if (attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
      const struct gl_renderbuffer_attachment *depthAtt, *stencilAtt;
      if (pname == GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE) {
         /* This behavior is first specified in OpenGL 4.4 specification.
          *
          * From the OpenGL 4.4 spec page 275:
          *   "This query cannot be performed for a combined depth+stencil
          *    attachment, since it does not have a single format."
          */
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "%s(GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE"
                     " is invalid for depth+stencil attachment)", caller);
         return;
      }
      /* the depth and stencil attachments must point to the same buffer */
      depthAtt = get_attachment(ctx, buffer, GL_DEPTH_ATTACHMENT, NULL);
      stencilAtt = get_attachment(ctx, buffer, GL_STENCIL_ATTACHMENT, NULL);
      if (depthAtt->Renderbuffer != stencilAtt->Renderbuffer) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "%s(DEPTH/STENCIL attachments differ)", caller);
         return;
      }
   }

   /* No need to flush here */

   switch (pname) {
   case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE_EXT:
      /* From the OpenGL spec, 9.2. Binding and Managing Framebuffer Objects:
       *
       * "If the value of FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE is NONE, then
       *  either no framebuffer is bound to target; or the default framebuffer
       *  is bound, attachment is DEPTH or STENCIL, and the number of depth or
       *  stencil bits, respectively, is zero."
       *
       * Note that we don't need explicit checks on DEPTH and STENCIL, because
       * on the case the spec is pointing, att->Type is already NONE, so we
       * just need to check att->Type.
       */
      *params = (_mesa_is_winsys_fbo(buffer) && att->Type != GL_NONE) ?
         GL_FRAMEBUFFER_DEFAULT : att->Type;
      return;
   case GL_FRAMEBUFFER_ATTACHMENT_OBJECT_NAME_EXT:
      if (att->Type == GL_RENDERBUFFER_EXT) {
         *params = att->Renderbuffer->Name;
      }
      else if (att->Type == GL_TEXTURE) {
         *params = att->Texture->Name;
      }
      else {
         assert(att->Type == GL_NONE);
         if (_mesa_is_desktop_gl(ctx) || _mesa_is_gles3(ctx)) {
            *params = 0;
         } else {
            goto invalid_pname_enum;
         }
      }
      return;
   case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_LEVEL_EXT:
      if (att->Type == GL_TEXTURE) {
         *params = att->TextureLevel;
      }
      else if (att->Type == GL_NONE) {
         _mesa_error(ctx, err, "%s(invalid pname %s)", caller,
                     _mesa_enum_to_string(pname));
      }
      else {
         goto invalid_pname_enum;
      }
      return;
   case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_CUBE_MAP_FACE_EXT:
      if (att->Type == GL_TEXTURE) {
         if (att->Texture && att->Texture->Target == GL_TEXTURE_CUBE_MAP) {
            *params = GL_TEXTURE_CUBE_MAP_POSITIVE_X + att->CubeMapFace;
         }
         else {
            *params = 0;
         }
      }
      else if (att->Type == GL_NONE) {
         _mesa_error(ctx, err, "%s(invalid pname %s)", caller,
                     _mesa_enum_to_string(pname));
      }
      else {
         goto invalid_pname_enum;
      }
      return;
   case GL_FRAMEBUFFER_ATTACHMENT_TEXTURE_3D_ZOFFSET_EXT:
      if (ctx->API == API_OPENGLES) {
         goto invalid_pname_enum;
      } else if (att->Type == GL_NONE) {
         _mesa_error(ctx, err, "%s(invalid pname %s)", caller,
                     _mesa_enum_to_string(pname));
      } else if (att->Type == GL_TEXTURE) {
         if (att->Texture && (att->Texture->Target == GL_TEXTURE_3D ||
             att->Texture->Target == GL_TEXTURE_2D_ARRAY)) {
            *params = att->Zoffset;
         }
         else {
            *params = 0;
         }
      }
      else {
         goto invalid_pname_enum;
      }
      return;
   case GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING:
      if ((!_mesa_is_desktop_gl(ctx) ||
           !ctx->Extensions.ARB_framebuffer_object)
          && !_mesa_is_gles3(ctx)) {
         goto invalid_pname_enum;
      }
      else if (att->Type == GL_NONE) {
         if (_mesa_is_winsys_fbo(buffer) &&
             (attachment == GL_DEPTH || attachment == GL_STENCIL)) {
            *params = GL_LINEAR;
         } else {
            _mesa_error(ctx, err, "%s(invalid pname %s)", caller,
                        _mesa_enum_to_string(pname));
         }
      }
      else {
         if (ctx->Extensions.EXT_framebuffer_sRGB) {
            *params =
               _mesa_get_format_color_encoding(att->Renderbuffer->Format);
         }
         else {
            /* According to ARB_framebuffer_sRGB, we should return LINEAR
             * if the sRGB conversion is unsupported. */
            *params = GL_LINEAR;
         }
      }
      return;
   case GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE:
      if ((ctx->API != API_OPENGL_COMPAT ||
           !ctx->Extensions.ARB_framebuffer_object)
          && ctx->API != API_OPENGL_CORE
          && !_mesa_is_gles3(ctx)) {
         goto invalid_pname_enum;
      }
      else if (att->Type == GL_NONE) {
         _mesa_error(ctx, err, "%s(invalid pname %s)", caller,
                     _mesa_enum_to_string(pname));
      }
      else {
         mesa_format format = att->Renderbuffer->Format;

         /* Page 235 (page 247 of the PDF) in section 6.1.13 of the OpenGL ES
          * 3.0.1 spec says:
          *
          *     "If pname is FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE.... If
          *     attachment is DEPTH_STENCIL_ATTACHMENT the query will fail and
          *     generate an INVALID_OPERATION error.
          */
         if (_mesa_is_gles3(ctx) &&
             attachment == GL_DEPTH_STENCIL_ATTACHMENT) {
            _mesa_error(ctx, GL_INVALID_OPERATION,
                        "%s(cannot query "
                        "GL_FRAMEBUFFER_ATTACHMENT_COMPONENT_TYPE of "
                        "GL_DEPTH_STENCIL_ATTACHMENT)", caller);
            return;
         }

         if (format == MESA_FORMAT_S_UINT8) {
            /* special cases */
            *params = GL_INDEX;
         }
         else if (format == MESA_FORMAT_Z32_FLOAT_S8X24_UINT) {
            /* depends on the attachment parameter */
            if (attachment == GL_STENCIL_ATTACHMENT) {
               *params = GL_INDEX;
            }
            else {
               *params = GL_FLOAT;
            }
         }
         else {
            *params = _mesa_get_format_datatype(format);
         }
      }
      return;
   case GL_FRAMEBUFFER_ATTACHMENT_RED_SIZE:
   case GL_FRAMEBUFFER_ATTACHMENT_GREEN_SIZE:
   case GL_FRAMEBUFFER_ATTACHMENT_BLUE_SIZE:
   case GL_FRAMEBUFFER_ATTACHMENT_ALPHA_SIZE:
   case GL_FRAMEBUFFER_ATTACHMENT_DEPTH_SIZE:
   case GL_FRAMEBUFFER_ATTACHMENT_STENCIL_SIZE:
      if ((!_mesa_is_desktop_gl(ctx) ||
           !ctx->Extensions.ARB_framebuffer_object)
          && !_mesa_is_gles3(ctx)) {
         goto invalid_pname_enum;
      }
      else if (att->Texture) {
         const struct gl_texture_image *texImage =
            _mesa_select_tex_image(att->Texture, att->Texture->Target,
                                   att->TextureLevel);
         if (texImage) {
            *params = get_component_bits(pname, texImage->_BaseFormat,
                                         texImage->TexFormat);
         }
         else {
            *params = 0;
         }
      }
      else if (att->Renderbuffer) {
         *params = get_component_bits(pname, att->Renderbuffer->_BaseFormat,
                                      att->Renderbuffer->Format);
      }
      else {
         assert(att->Type == GL_NONE);
         _mesa_error(ctx, err, "%s(invalid pname %s)", caller,
                     _mesa_enum_to_string(pname));
      }
      return;
   case GL_FRAMEBUFFER_ATTACHMENT_LAYERED:
      if (!_mesa_has_geometry_shaders(ctx)) {
         goto invalid_pname_enum;
      } else if (att->Type == GL_TEXTURE) {
         *params = att->Layered;
      } else if (att->Type == GL_NONE) {
         _mesa_error(ctx, err, "%s(invalid pname %s)", caller,
                     _mesa_enum_to_string(pname));
      } else {
         goto invalid_pname_enum;
      }
      return;
   default:
      goto invalid_pname_enum;
   }

   return;

invalid_pname_enum:
   _mesa_error(ctx, GL_INVALID_ENUM, "%s(invalid pname %s)", caller,
               _mesa_enum_to_string(pname));
   return;
}


void GLAPIENTRY
_mesa_GetFramebufferAttachmentParameteriv(GLenum target, GLenum attachment,
                                          GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_framebuffer *buffer;

   buffer = get_framebuffer_target(ctx, target);
   if (!buffer) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glGetFramebufferAttachmentParameteriv(invalid target %s)",
                  _mesa_enum_to_string(target));
      return;
   }

   get_framebuffer_attachment_parameter(ctx, buffer, attachment, pname,
                                        params,
                                    "glGetFramebufferAttachmentParameteriv");
}


void GLAPIENTRY
_mesa_GetNamedFramebufferAttachmentParameteriv(GLuint framebuffer,
                                               GLenum attachment,
                                               GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_framebuffer *buffer;

   if (framebuffer) {
      buffer = _mesa_lookup_framebuffer_err(ctx, framebuffer,
                              "glGetNamedFramebufferAttachmentParameteriv");
      if (!buffer)
         return;
   }
   else {
      /*
       * Section 9.2 Binding and Managing Framebuffer Objects of the OpenGL
       * 4.5 core spec (30.10.2014, PDF page 314):
       *    "If framebuffer is zero, then the default draw framebuffer is
       *    queried."
       */
      buffer = ctx->WinSysDrawBuffer;
   }

   get_framebuffer_attachment_parameter(ctx, buffer, attachment, pname,
                                        params,
                              "glGetNamedFramebufferAttachmentParameteriv");
}


void GLAPIENTRY
_mesa_NamedFramebufferParameteri(GLuint framebuffer, GLenum pname,
                                 GLint param)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_framebuffer *fb = NULL;

   if (!ctx->Extensions.ARB_framebuffer_no_attachments) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glNamedFramebufferParameteri("
                  "ARB_framebuffer_no_attachments not implemented)");
      return;
   }

   fb = _mesa_lookup_framebuffer_err(ctx, framebuffer,
                                     "glNamedFramebufferParameteri");

   if (fb) {
      framebuffer_parameteri(ctx, fb, pname, param,
                             "glNamedFramebufferParameteriv");
   }
}


void GLAPIENTRY
_mesa_GetNamedFramebufferParameteriv(GLuint framebuffer, GLenum pname,
                                     GLint *param)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_framebuffer *fb;

   if (!ctx->Extensions.ARB_framebuffer_no_attachments) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glNamedFramebufferParameteriv("
                  "ARB_framebuffer_no_attachments not implemented)");
      return;
   }

   if (framebuffer) {
      fb = _mesa_lookup_framebuffer_err(ctx, framebuffer,
                                        "glGetNamedFramebufferParameteriv");
   } else {
      fb = ctx->WinSysDrawBuffer;
   }

   if (fb) {
      get_framebuffer_parameteriv(ctx, fb, pname, param,
                                  "glGetNamedFramebufferParameteriv");
   }
}


static void
invalidate_framebuffer_storage(struct gl_context *ctx,
                               struct gl_framebuffer *fb,
                               GLsizei numAttachments,
                               const GLenum *attachments, GLint x, GLint y,
                               GLsizei width, GLsizei height, const char *name)
{
   int i;

   /* Section 17.4 Whole Framebuffer Operations of the OpenGL 4.5 Core
    * Spec (2.2.2015, PDF page 522) says:
    *    "An INVALID_VALUE error is generated if numAttachments, width, or
    *    height is negative."
    */
   if (numAttachments < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(numAttachments < 0)", name);
      return;
   }

   if (width < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(width < 0)", name);
      return;
   }

   if (height < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(height < 0)", name);
      return;
   }

   /* The GL_ARB_invalidate_subdata spec says:
    *
    *     "If an attachment is specified that does not exist in the
    *     framebuffer bound to <target>, it is ignored."
    *
    * It also says:
    *
    *     "If <attachments> contains COLOR_ATTACHMENTm and m is greater than
    *     or equal to the value of MAX_COLOR_ATTACHMENTS, then the error
    *     INVALID_OPERATION is generated."
    *
    * No mention is made of GL_AUXi being out of range.  Therefore, we allow
    * any enum that can be allowed by the API (OpenGL ES 3.0 has a different
    * set of retrictions).
    */
   for (i = 0; i < numAttachments; i++) {
      if (_mesa_is_winsys_fbo(fb)) {
         switch (attachments[i]) {
         case GL_ACCUM:
         case GL_AUX0:
         case GL_AUX1:
         case GL_AUX2:
         case GL_AUX3:
            /* Accumulation buffers and auxilary buffers were removed in
             * OpenGL 3.1, and they never existed in OpenGL ES.
             */
            if (ctx->API != API_OPENGL_COMPAT)
               goto invalid_enum;
            break;
         case GL_COLOR:
         case GL_DEPTH:
         case GL_STENCIL:
            break;
         case GL_BACK_LEFT:
         case GL_BACK_RIGHT:
         case GL_FRONT_LEFT:
         case GL_FRONT_RIGHT:
            if (!_mesa_is_desktop_gl(ctx))
               goto invalid_enum;
            break;
         default:
            goto invalid_enum;
         }
      } else {
         switch (attachments[i]) {
         case GL_DEPTH_ATTACHMENT:
         case GL_STENCIL_ATTACHMENT:
            break;
         case GL_DEPTH_STENCIL_ATTACHMENT:
            /* GL_DEPTH_STENCIL_ATTACHMENT is a valid attachment point only
             * in desktop and ES 3.0 profiles. Note that OES_packed_depth_stencil
             * extension does not make this attachment point valid on ES 2.0.
             */
            if (_mesa_is_desktop_gl(ctx) || _mesa_is_gles3(ctx))
               break;
            /* fallthrough */
         case GL_COLOR_ATTACHMENT0:
         case GL_COLOR_ATTACHMENT1:
         case GL_COLOR_ATTACHMENT2:
         case GL_COLOR_ATTACHMENT3:
         case GL_COLOR_ATTACHMENT4:
         case GL_COLOR_ATTACHMENT5:
         case GL_COLOR_ATTACHMENT6:
         case GL_COLOR_ATTACHMENT7:
         case GL_COLOR_ATTACHMENT8:
         case GL_COLOR_ATTACHMENT9:
         case GL_COLOR_ATTACHMENT10:
         case GL_COLOR_ATTACHMENT11:
         case GL_COLOR_ATTACHMENT12:
         case GL_COLOR_ATTACHMENT13:
         case GL_COLOR_ATTACHMENT14:
         case GL_COLOR_ATTACHMENT15: {
            unsigned k = attachments[i] - GL_COLOR_ATTACHMENT0;
            if (k >= ctx->Const.MaxColorAttachments) {
               _mesa_error(ctx, GL_INVALID_OPERATION,
                           "%s(attachment >= max. color attachments)", name);
               return;
            }
            break;
         }
         default:
            goto invalid_enum;
         }
      }
   }

   /* We don't actually do anything for this yet.  Just return after
    * validating the parameters and generating the required errors.
    */
   return;

invalid_enum:
   _mesa_error(ctx, GL_INVALID_ENUM, "%s(invalid attachment %s)", name,
               _mesa_enum_to_string(attachments[i]));
   return;
}


void GLAPIENTRY
_mesa_InvalidateSubFramebuffer_no_error(GLenum target, GLsizei numAttachments,
                                        const GLenum *attachments, GLint x,
                                        GLint y, GLsizei width, GLsizei height)
{
   /* no-op */
}


void GLAPIENTRY
_mesa_InvalidateSubFramebuffer(GLenum target, GLsizei numAttachments,
                               const GLenum *attachments, GLint x, GLint y,
                               GLsizei width, GLsizei height)
{
   struct gl_framebuffer *fb;
   GET_CURRENT_CONTEXT(ctx);

   fb = get_framebuffer_target(ctx, target);
   if (!fb) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glInvalidateSubFramebuffer(invalid target %s)",
                  _mesa_enum_to_string(target));
      return;
   }

   invalidate_framebuffer_storage(ctx, fb, numAttachments, attachments,
                                  x, y, width, height,
                                  "glInvalidateSubFramebuffer");
}


void GLAPIENTRY
_mesa_InvalidateNamedFramebufferSubData(GLuint framebuffer,
                                        GLsizei numAttachments,
                                        const GLenum *attachments,
                                        GLint x, GLint y,
                                        GLsizei width, GLsizei height)
{
   struct gl_framebuffer *fb;
   GET_CURRENT_CONTEXT(ctx);

   /* The OpenGL 4.5 core spec (02.02.2015) says (in Section 17.4 Whole
    * Framebuffer Operations, PDF page 522): "If framebuffer is zero, the
    * default draw framebuffer is affected."
    */
   if (framebuffer) {
      fb = _mesa_lookup_framebuffer_err(ctx, framebuffer,
                                        "glInvalidateNamedFramebufferSubData");
      if (!fb)
         return;
   }
   else
      fb = ctx->WinSysDrawBuffer;

   invalidate_framebuffer_storage(ctx, fb, numAttachments, attachments,
                                  x, y, width, height,
                                  "glInvalidateNamedFramebufferSubData");
}


void GLAPIENTRY
_mesa_InvalidateFramebuffer_no_error(GLenum target, GLsizei numAttachments,
                                     const GLenum *attachments)
{
   /* no-op */
}


void GLAPIENTRY
_mesa_InvalidateFramebuffer(GLenum target, GLsizei numAttachments,
                            const GLenum *attachments)
{
   struct gl_framebuffer *fb;
   GET_CURRENT_CONTEXT(ctx);

   fb = get_framebuffer_target(ctx, target);
   if (!fb) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glInvalidateFramebuffer(invalid target %s)",
                  _mesa_enum_to_string(target));
      return;
   }

   /* The GL_ARB_invalidate_subdata spec says:
    *
    *     "The command
    *
    *        void InvalidateFramebuffer(enum target,
    *                                   sizei numAttachments,
    *                                   const enum *attachments);
    *
    *     is equivalent to the command InvalidateSubFramebuffer with <x>, <y>,
    *     <width>, <height> equal to 0, 0, <MAX_VIEWPORT_DIMS[0]>,
    *     <MAX_VIEWPORT_DIMS[1]> respectively."
    */
   invalidate_framebuffer_storage(ctx, fb, numAttachments, attachments,
                                  0, 0,
                                  ctx->Const.MaxViewportWidth,
                                  ctx->Const.MaxViewportHeight,
                                  "glInvalidateFramebuffer");
}


void GLAPIENTRY
_mesa_InvalidateNamedFramebufferData(GLuint framebuffer,
                                     GLsizei numAttachments,
                                     const GLenum *attachments)
{
   struct gl_framebuffer *fb;
   GET_CURRENT_CONTEXT(ctx);

   /* The OpenGL 4.5 core spec (02.02.2015) says (in Section 17.4 Whole
    * Framebuffer Operations, PDF page 522): "If framebuffer is zero, the
    * default draw framebuffer is affected."
    */
   if (framebuffer) {
      fb = _mesa_lookup_framebuffer_err(ctx, framebuffer,
                                        "glInvalidateNamedFramebufferData");
      if (!fb)
         return;
   }
   else
      fb = ctx->WinSysDrawBuffer;

   /* The GL_ARB_invalidate_subdata spec says:
    *
    *     "The command
    *
    *        void InvalidateFramebuffer(enum target,
    *                                   sizei numAttachments,
    *                                   const enum *attachments);
    *
    *     is equivalent to the command InvalidateSubFramebuffer with <x>, <y>,
    *     <width>, <height> equal to 0, 0, <MAX_VIEWPORT_DIMS[0]>,
    *     <MAX_VIEWPORT_DIMS[1]> respectively."
    */
   invalidate_framebuffer_storage(ctx, fb, numAttachments, attachments,
                                  0, 0,
                                  ctx->Const.MaxViewportWidth,
                                  ctx->Const.MaxViewportHeight,
                                  "glInvalidateNamedFramebufferData");
}


void GLAPIENTRY
_mesa_DiscardFramebufferEXT(GLenum target, GLsizei numAttachments,
                            const GLenum *attachments)
{
   struct gl_framebuffer *fb;
   GLint i;

   GET_CURRENT_CONTEXT(ctx);

   fb = get_framebuffer_target(ctx, target);
   if (!fb) {
      _mesa_error(ctx, GL_INVALID_ENUM,
         "glDiscardFramebufferEXT(target %s)",
         _mesa_enum_to_string(target));
      return;
   }

   if (numAttachments < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glDiscardFramebufferEXT(numAttachments < 0)");
      return;
   }

   for (i = 0; i < numAttachments; i++) {
      switch (attachments[i]) {
      case GL_COLOR:
      case GL_DEPTH:
      case GL_STENCIL:
         if (_mesa_is_user_fbo(fb))
            goto invalid_enum;
         break;
      case GL_COLOR_ATTACHMENT0:
      case GL_DEPTH_ATTACHMENT:
      case GL_STENCIL_ATTACHMENT:
         if (_mesa_is_winsys_fbo(fb))
            goto invalid_enum;
         break;
      default:
         goto invalid_enum;
      }
   }

   if (ctx->Driver.DiscardFramebuffer)
      ctx->Driver.DiscardFramebuffer(ctx, target, numAttachments, attachments);

   return;

invalid_enum:
   _mesa_error(ctx, GL_INVALID_ENUM,
               "glDiscardFramebufferEXT(attachment %s)",
              _mesa_enum_to_string(attachments[i]));
}
