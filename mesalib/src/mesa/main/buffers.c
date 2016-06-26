/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2007  Brian Paul   All Rights Reserved.
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


/**
 * \file buffers.c
 * glReadBuffer, DrawBuffer functions.
 */



#include "glheader.h"
#include "buffers.h"
#include "context.h"
#include "enums.h"
#include "fbobject.h"
#include "mtypes.h"
#include "util/bitscan.h"


#define BAD_MASK ~0u


/**
 * Return bitmask of BUFFER_BIT_* flags indicating which color buffers are
 * available to the rendering context (for drawing or reading).
 * This depends on the type of framebuffer.  For window system framebuffers
 * we look at the framebuffer's visual.  But for user-create framebuffers we
 * look at the number of supported color attachments.
 * \param fb  the framebuffer to draw to, or read from
 * \return  bitmask of BUFFER_BIT_* flags
 */
static GLbitfield
supported_buffer_bitmask(const struct gl_context *ctx,
                         const struct gl_framebuffer *fb)
{
   GLbitfield mask = 0x0;

   if (_mesa_is_user_fbo(fb)) {
      /* A user-created renderbuffer */
      mask = ((1 << ctx->Const.MaxColorAttachments) - 1) << BUFFER_COLOR0;
   }
   else {
      /* A window system framebuffer */
      GLint i;
      mask = BUFFER_BIT_FRONT_LEFT; /* always have this */
      if (fb->Visual.stereoMode) {
         mask |= BUFFER_BIT_FRONT_RIGHT;
         if (fb->Visual.doubleBufferMode) {
            mask |= BUFFER_BIT_BACK_LEFT | BUFFER_BIT_BACK_RIGHT;
         }
      }
      else if (fb->Visual.doubleBufferMode) {
         mask |= BUFFER_BIT_BACK_LEFT;
      }

      for (i = 0; i < fb->Visual.numAuxBuffers; i++) {
         mask |= (BUFFER_BIT_AUX0 << i);
      }
   }

   return mask;
}


/**
 * Helper routine used by glDrawBuffer and glDrawBuffersARB.
 * Given a GLenum naming one or more color buffers (such as
 * GL_FRONT_AND_BACK), return the corresponding bitmask of BUFFER_BIT_* flags.
 */
static GLbitfield
draw_buffer_enum_to_bitmask(const struct gl_context *ctx, GLenum buffer)
{
   switch (buffer) {
      case GL_NONE:
         return 0;
      case GL_FRONT:
         return BUFFER_BIT_FRONT_LEFT | BUFFER_BIT_FRONT_RIGHT;
      case GL_BACK:
         if (_mesa_is_gles(ctx)) {
            /* Page 181 (page 192 of the PDF) in section 4.2.1 of the OpenGL
             * ES 3.0.1 specification says:
             *
             *     "When draw buffer zero is BACK, color values are written
             *     into the sole buffer for single-buffered contexts, or into
             *     the back buffer for double-buffered contexts."
             *
             * Since there is no stereo rendering in ES 3.0, only return the
             * LEFT bits.  This also satisfies the "n must be 1" requirement.
             *
             * We also do this for GLES 1 and 2 because those APIs have no
             * concept of selecting the front and back buffer anyway and it's
             * convenient to be able to maintain the magic behaviour of
             * GL_BACK in that case.
             */
            if (ctx->DrawBuffer->Visual.doubleBufferMode)
               return BUFFER_BIT_BACK_LEFT;
            return BUFFER_BIT_FRONT_LEFT;
         }
         return BUFFER_BIT_BACK_LEFT | BUFFER_BIT_BACK_RIGHT;
      case GL_RIGHT:
         return BUFFER_BIT_FRONT_RIGHT | BUFFER_BIT_BACK_RIGHT;
      case GL_FRONT_RIGHT:
         return BUFFER_BIT_FRONT_RIGHT;
      case GL_BACK_RIGHT:
         return BUFFER_BIT_BACK_RIGHT;
      case GL_BACK_LEFT:
         return BUFFER_BIT_BACK_LEFT;
      case GL_FRONT_AND_BACK:
         return BUFFER_BIT_FRONT_LEFT | BUFFER_BIT_BACK_LEFT
              | BUFFER_BIT_FRONT_RIGHT | BUFFER_BIT_BACK_RIGHT;
      case GL_LEFT:
         return BUFFER_BIT_FRONT_LEFT | BUFFER_BIT_BACK_LEFT;
      case GL_FRONT_LEFT:
         return BUFFER_BIT_FRONT_LEFT;
      case GL_AUX0:
         return BUFFER_BIT_AUX0;
      case GL_AUX1:
      case GL_AUX2:
      case GL_AUX3:
         return 1 << BUFFER_COUNT; /* invalid, but not BAD_MASK */
      case GL_COLOR_ATTACHMENT0_EXT:
         return BUFFER_BIT_COLOR0;
      case GL_COLOR_ATTACHMENT1_EXT:
         return BUFFER_BIT_COLOR1;
      case GL_COLOR_ATTACHMENT2_EXT:
         return BUFFER_BIT_COLOR2;
      case GL_COLOR_ATTACHMENT3_EXT:
         return BUFFER_BIT_COLOR3;
      case GL_COLOR_ATTACHMENT4_EXT:
         return BUFFER_BIT_COLOR4;
      case GL_COLOR_ATTACHMENT5_EXT:
         return BUFFER_BIT_COLOR5;
      case GL_COLOR_ATTACHMENT6_EXT:
         return BUFFER_BIT_COLOR6;
      case GL_COLOR_ATTACHMENT7_EXT:
         return BUFFER_BIT_COLOR7;
      default:
         /* not an error, but also not supported */
         if (buffer >= GL_COLOR_ATTACHMENT8 && buffer <= GL_COLOR_ATTACHMENT31)
            return 1 << BUFFER_COUNT;
         /* error */
         return BAD_MASK;
   }
}


/**
 * Helper routine used by glReadBuffer.
 * Given a GLenum naming a color buffer, return the index of the corresponding
 * renderbuffer (a BUFFER_* value).
 * return -1 for an invalid buffer.
 */
static gl_buffer_index
read_buffer_enum_to_index(GLenum buffer)
{
   switch (buffer) {
      case GL_FRONT:
         return BUFFER_FRONT_LEFT;
      case GL_BACK:
         return BUFFER_BACK_LEFT;
      case GL_RIGHT:
         return BUFFER_FRONT_RIGHT;
      case GL_FRONT_RIGHT:
         return BUFFER_FRONT_RIGHT;
      case GL_BACK_RIGHT:
         return BUFFER_BACK_RIGHT;
      case GL_BACK_LEFT:
         return BUFFER_BACK_LEFT;
      case GL_LEFT:
         return BUFFER_FRONT_LEFT;
      case GL_FRONT_LEFT:
         return BUFFER_FRONT_LEFT;
      case GL_AUX0:
         return BUFFER_AUX0;
      case GL_AUX1:
      case GL_AUX2:
      case GL_AUX3:
         return BUFFER_COUNT; /* invalid, but not -1 */
      case GL_COLOR_ATTACHMENT0_EXT:
         return BUFFER_COLOR0;
      case GL_COLOR_ATTACHMENT1_EXT:
         return BUFFER_COLOR1;
      case GL_COLOR_ATTACHMENT2_EXT:
         return BUFFER_COLOR2;
      case GL_COLOR_ATTACHMENT3_EXT:
         return BUFFER_COLOR3;
      case GL_COLOR_ATTACHMENT4_EXT:
         return BUFFER_COLOR4;
      case GL_COLOR_ATTACHMENT5_EXT:
         return BUFFER_COLOR5;
      case GL_COLOR_ATTACHMENT6_EXT:
         return BUFFER_COLOR6;
      case GL_COLOR_ATTACHMENT7_EXT:
         return BUFFER_COLOR7;
      default:
         /* not an error, but also not supported */
         if (buffer >= GL_COLOR_ATTACHMENT8 && buffer <= GL_COLOR_ATTACHMENT31)
            return BUFFER_COUNT;
         /* error */
         return -1;
   }
}

static bool
is_legal_es3_readbuffer_enum(GLenum buf)
{
   return buf == GL_BACK || buf == GL_NONE ||
          (buf >= GL_COLOR_ATTACHMENT0 && buf <= GL_COLOR_ATTACHMENT31);
}

/**
 * Called by glDrawBuffer() and glNamedFramebufferDrawBuffer().
 * Specify which renderbuffer(s) to draw into for the first color output.
 * <buffer> can name zero, one, two or four renderbuffers!
 * \sa _mesa_DrawBuffers
 *
 * \param buffer  buffer token such as GL_LEFT or GL_FRONT_AND_BACK, etc.
 *
 * Note that the behaviour of this function depends on whether the
 * current ctx->DrawBuffer is a window-system framebuffer or a user-created
 * framebuffer object.
 *   In the former case, we update the per-context ctx->Color.DrawBuffer
 *   state var _and_ the FB's ColorDrawBuffer state.
 *   In the later case, we update the FB's ColorDrawBuffer state only.
 *
 * Furthermore, upon a MakeCurrent() or BindFramebuffer() call, if the
 * new FB is a window system FB, we need to re-update the FB's
 * ColorDrawBuffer state to match the context.  This is handled in
 * _mesa_update_framebuffer().
 *
 * See the GL_EXT_framebuffer_object spec for more info.
 */
static void
draw_buffer(struct gl_context *ctx, struct gl_framebuffer *fb,
            GLenum buffer, const char *caller)
{
   GLbitfield destMask;

   FLUSH_VERTICES(ctx, 0);

   if (MESA_VERBOSE & VERBOSE_API) {
      _mesa_debug(ctx, "%s %s\n", caller, _mesa_enum_to_string(buffer));
   }

   if (buffer == GL_NONE) {
      destMask = 0x0;
   }
   else {
      const GLbitfield supportedMask
         = supported_buffer_bitmask(ctx, fb);
      destMask = draw_buffer_enum_to_bitmask(ctx, buffer);
      if (destMask == BAD_MASK) {
         /* totally bogus buffer */
         _mesa_error(ctx, GL_INVALID_ENUM, "%s(invalid buffer %s)", caller,
                     _mesa_enum_to_string(buffer));
         return;
      }
      destMask &= supportedMask;
      if (destMask == 0x0) {
         /* none of the named color buffers exist! */
         _mesa_error(ctx, GL_INVALID_OPERATION, "%s(invalid buffer %s)",
                     caller, _mesa_enum_to_string(buffer));
         return;
      }
   }

   /* if we get here, there's no error so set new state */
   _mesa_drawbuffers(ctx, fb, 1, &buffer, &destMask);

   /* Call device driver function only if fb is the bound draw buffer */
   if (fb == ctx->DrawBuffer) {
      if (ctx->Driver.DrawBuffers)
         ctx->Driver.DrawBuffers(ctx, 1, &buffer);
      else if (ctx->Driver.DrawBuffer)
         ctx->Driver.DrawBuffer(ctx, buffer);
   }
}


void GLAPIENTRY
_mesa_DrawBuffer(GLenum buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   draw_buffer(ctx, ctx->DrawBuffer, buffer, "glDrawBuffer");
}


void GLAPIENTRY
_mesa_NamedFramebufferDrawBuffer(GLuint framebuffer, GLenum buf)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_framebuffer *fb;

   if (framebuffer) {
      fb = _mesa_lookup_framebuffer_err(ctx, framebuffer,
                                        "glNamedFramebufferDrawBuffer");
      if (!fb)
         return;
   }
   else
      fb = ctx->WinSysDrawBuffer;

   draw_buffer(ctx, fb, buf, "glNamedFramebufferDrawBuffer");
}


/**
 * Called by glDrawBuffersARB() and glNamedFramebufferDrawBuffers() to specify
 * the destination color renderbuffers for N fragment program color outputs.
 * \sa _mesa_DrawBuffer
 * \param n  number of outputs
 * \param buffers  array [n] of renderbuffer names.  Unlike glDrawBuffer, the
 *                 names cannot specify more than one buffer.  For example,
 *                 GL_FRONT_AND_BACK is illegal.
 */
static void
draw_buffers(struct gl_context *ctx, struct gl_framebuffer *fb,
             GLsizei n, const GLenum *buffers, const char *caller)
{
   GLuint output;
   GLbitfield usedBufferMask, supportedMask;
   GLbitfield destMask[MAX_DRAW_BUFFERS];

   FLUSH_VERTICES(ctx, 0);

   /* Turns out n==0 is a valid input that should not produce an error.
    * The remaining code below correctly handles the n==0 case.
    *
    * From the OpenGL 3.0 specification, page 258:
    * "An INVALID_VALUE error is generated if n is greater than
    *  MAX_DRAW_BUFFERS."
    */
   if (n < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(n < 0)", caller);
      return;
   }

   if (n > (GLsizei) ctx->Const.MaxDrawBuffers) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(n > maximum number of draw buffers)", caller);
      return;
   }

   supportedMask = supported_buffer_bitmask(ctx, fb);
   usedBufferMask = 0x0;

   /* From the ES 3.0 specification, page 180:
    * "If the GL is bound to the default framebuffer, then n must be 1
    *  and the constant must be BACK or NONE."
    * (same restriction applies with GL_EXT_draw_buffers specification)
    */
   if (ctx->API == API_OPENGLES2 && _mesa_is_winsys_fbo(fb) &&
       (n != 1 || (buffers[0] != GL_NONE && buffers[0] != GL_BACK))) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(invalid buffers)", caller);
      return;
   }

   /* complicated error checking... */
   for (output = 0; output < n; output++) {
      /* Section 4.2 (Whole Framebuffer Operations) of the OpenGL 3.0
       * specification says:
       *
       *     "Each buffer listed in bufs must be BACK, NONE, or one of the values
       *      from table 4.3 (NONE, COLOR_ATTACHMENTi)"
       */
      if (_mesa_is_gles3(ctx) && buffers[output] != GL_NONE &&
          buffers[output] != GL_BACK &&
          (buffers[output] < GL_COLOR_ATTACHMENT0 ||
           buffers[output] >= GL_COLOR_ATTACHMENT0 + ctx->Const.MaxColorAttachments)) {
         _mesa_error(ctx, GL_INVALID_ENUM, "glDrawBuffers(buffer)");
         return;
      }

      if (buffers[output] == GL_NONE) {
         destMask[output] = 0x0;
      }
      else {
         /* Page 259 (page 275 of the PDF) in section 4.2.1 of the OpenGL 3.0
          * spec (20080923) says:
          *
          *     "If the GL is bound to a framebuffer object and DrawBuffers is
          *     supplied with [...] COLOR_ATTACHMENTm where m is greater than
          *     or equal to the value of MAX_COLOR_ATTACHMENTS, then the error
          *     INVALID_OPERATION results."
          */
         if (_mesa_is_user_fbo(fb) && buffers[output] >=
             GL_COLOR_ATTACHMENT0 + ctx->Const.MaxDrawBuffers) {
            _mesa_error(ctx, GL_INVALID_OPERATION,
                        "%s(buffers[%d] >= maximum number of draw buffers)",
                        caller, output);
            return;
         }

         destMask[output] = draw_buffer_enum_to_bitmask(ctx, buffers[output]);

         /* From the OpenGL 3.0 specification, page 258:
          * "Each buffer listed in bufs must be one of the values from tables
          *  4.5 or 4.6.  Otherwise, an INVALID_ENUM error is generated.
          */
         if (destMask[output] == BAD_MASK) {
            _mesa_error(ctx, GL_INVALID_ENUM, "%s(invalid buffer %s)",
                        caller, _mesa_enum_to_string(buffers[output]));
            return;
         }

         /* From the OpenGL 4.0 specification, page 256:
          * "For both the default framebuffer and framebuffer objects, the
          *  constants FRONT, BACK, LEFT, RIGHT, and FRONT_AND_BACK are not
          *  valid in the bufs array passed to DrawBuffers, and will result in
          *  the error INVALID_ENUM. This restriction is because these
          *  constants may themselves refer to multiple buffers, as shown in
          *  table 4.4."
          *  Previous versions of the OpenGL specification say INVALID_OPERATION,
          *  but the Khronos conformance tests expect INVALID_ENUM.
          */
         if (_mesa_bitcount(destMask[output]) > 1) {
            _mesa_error(ctx, GL_INVALID_ENUM, "%s(invalid buffer %s)",
                        caller, _mesa_enum_to_string(buffers[output]));
            return;
         }

         /* From the OpenGL 3.0 specification, page 259:
          * "If the GL is bound to the default framebuffer and DrawBuffers is
          *  supplied with a constant (other than NONE) that does not indicate
          *  any of the color buffers allocated to the GL context by the window
          *  system, the error INVALID_OPERATION will be generated.
          *
          *  If the GL is bound to a framebuffer object and DrawBuffers is
          *  supplied with a constant from table 4.6 [...] then the error
          *  INVALID_OPERATION results."
          */
         destMask[output] &= supportedMask;
         if (destMask[output] == 0) {
            _mesa_error(ctx, GL_INVALID_OPERATION,
                        "%s(unsupported buffer %s)",
                        caller, _mesa_enum_to_string(buffers[output]));
            return;
         }

         /* ES 3.0 is even more restrictive.  From the ES 3.0 spec, page 180:
          * "If the GL is bound to a framebuffer object, the ith buffer listed
          *  in bufs must be COLOR_ATTACHMENTi or NONE. [...] INVALID_OPERATION."
          * (same restriction applies with GL_EXT_draw_buffers specification)
          */
         if (ctx->API == API_OPENGLES2 && _mesa_is_user_fbo(fb) &&
             buffers[output] != GL_NONE &&
             buffers[output] != GL_COLOR_ATTACHMENT0 + output) {
            _mesa_error(ctx, GL_INVALID_OPERATION,
                        "%s(unsupported buffer %s)",
                        caller, _mesa_enum_to_string(buffers[output]));
            return;
         }

         /* From the OpenGL 3.0 specification, page 258:
          * "Except for NONE, a buffer may not appear more than once in the
          *  array pointed to by bufs.  Specifying a buffer more then once will
          *  result in the error INVALID_OPERATION."
          */
         if (destMask[output] & usedBufferMask) {
            _mesa_error(ctx, GL_INVALID_OPERATION,
                        "%s(duplicated buffer %s)",
                        caller, _mesa_enum_to_string(buffers[output]));
            return;
         }

         /* update bitmask */
         usedBufferMask |= destMask[output];
      }
   }

   /* OK, if we get here, there were no errors so set the new state */
   _mesa_drawbuffers(ctx, fb, n, buffers, destMask);

   /*
    * Call device driver function if fb is the bound draw buffer.
    * Note that n can be equal to 0,
    * in which case we don't want to reference buffers[0], which
    * may not be valid.
    */
   if (fb == ctx->DrawBuffer) {
      if (ctx->Driver.DrawBuffers)
         ctx->Driver.DrawBuffers(ctx, n, buffers);
      else if (ctx->Driver.DrawBuffer)
         ctx->Driver.DrawBuffer(ctx, n > 0 ? buffers[0] : GL_NONE);
   }
}


void GLAPIENTRY
_mesa_DrawBuffers(GLsizei n, const GLenum *buffers)
{
   GET_CURRENT_CONTEXT(ctx);
   draw_buffers(ctx, ctx->DrawBuffer, n, buffers, "glDrawBuffers");
}


void GLAPIENTRY
_mesa_NamedFramebufferDrawBuffers(GLuint framebuffer, GLsizei n,
                                  const GLenum *bufs)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_framebuffer *fb;

   if (framebuffer) {
      fb = _mesa_lookup_framebuffer_err(ctx, framebuffer,
                                        "glNamedFramebufferDrawBuffers");
      if (!fb)
         return;
   }
   else
      fb = ctx->WinSysDrawBuffer;

   draw_buffers(ctx, fb, n, bufs, "glNamedFramebufferDrawBuffers");
}


/**
 * Performs necessary state updates when _mesa_drawbuffers makes an
 * actual change.
 */
static void
updated_drawbuffers(struct gl_context *ctx, struct gl_framebuffer *fb)
{
   FLUSH_VERTICES(ctx, _NEW_BUFFERS);

   if (ctx->API == API_OPENGL_COMPAT && !ctx->Extensions.ARB_ES2_compatibility) {
      /* Flag the FBO as requiring validation. */
      if (_mesa_is_user_fbo(fb)) {
	 fb->_Status = 0;
      }
   }
}


/**
 * Helper function to set the GL_DRAW_BUFFER state for the given context and
 * FBO.  Called via glDrawBuffer(), glDrawBuffersARB()
 *
 * All error checking will have been done prior to calling this function
 * so nothing should go wrong at this point.
 *
 * \param ctx  current context
 * \param fb   the desired draw buffer
 * \param n    number of color outputs to set
 * \param buffers  array[n] of colorbuffer names, like GL_LEFT.
 * \param destMask  array[n] of BUFFER_BIT_* bitmasks which correspond to the
 *                  colorbuffer names.  (i.e. GL_FRONT_AND_BACK =>
 *                  BUFFER_BIT_FRONT_LEFT | BUFFER_BIT_BACK_LEFT).
 */
void
_mesa_drawbuffers(struct gl_context *ctx, struct gl_framebuffer *fb,
                  GLuint n, const GLenum *buffers, const GLbitfield *destMask)
{
   GLbitfield mask[MAX_DRAW_BUFFERS];
   GLuint buf;

   if (!destMask) {
      /* compute destMask values now */
      const GLbitfield supportedMask = supported_buffer_bitmask(ctx, fb);
      GLuint output;
      for (output = 0; output < n; output++) {
         mask[output] = draw_buffer_enum_to_bitmask(ctx, buffers[output]);
         assert(mask[output] != BAD_MASK);
         mask[output] &= supportedMask;
      }
      destMask = mask;
   }

   /*
    * destMask[0] may have up to four bits set
    * (ex: glDrawBuffer(GL_FRONT_AND_BACK)).
    * Otherwise, destMask[x] can only have one bit set.
    */
   if (n > 0 && _mesa_bitcount(destMask[0]) > 1) {
      GLuint count = 0, destMask0 = destMask[0];
      while (destMask0) {
         const int bufIndex = u_bit_scan(&destMask0);
         if (fb->_ColorDrawBufferIndexes[count] != bufIndex) {
            updated_drawbuffers(ctx, fb);
            fb->_ColorDrawBufferIndexes[count] = bufIndex;
         }
         count++;
      }
      fb->ColorDrawBuffer[0] = buffers[0];
      fb->_NumColorDrawBuffers = count;
   }
   else {
      GLuint count = 0;
      for (buf = 0; buf < n; buf++ ) {
         if (destMask[buf]) {
            GLint bufIndex = ffs(destMask[buf]) - 1;
            /* only one bit should be set in the destMask[buf] field */
            assert(_mesa_bitcount(destMask[buf]) == 1);
            if (fb->_ColorDrawBufferIndexes[buf] != bufIndex) {
	       updated_drawbuffers(ctx, fb);
               fb->_ColorDrawBufferIndexes[buf] = bufIndex;
            }
            count = buf + 1;
         }
         else {
            if (fb->_ColorDrawBufferIndexes[buf] != -1) {
	       updated_drawbuffers(ctx, fb);
               fb->_ColorDrawBufferIndexes[buf] = -1;
            }
         }
         fb->ColorDrawBuffer[buf] = buffers[buf];
      }
      fb->_NumColorDrawBuffers = count;
   }

   /* set remaining outputs to -1 (GL_NONE) */
   for (buf = fb->_NumColorDrawBuffers; buf < ctx->Const.MaxDrawBuffers; buf++) {
      if (fb->_ColorDrawBufferIndexes[buf] != -1) {
         updated_drawbuffers(ctx, fb);
         fb->_ColorDrawBufferIndexes[buf] = -1;
      }
   }
   for (buf = n; buf < ctx->Const.MaxDrawBuffers; buf++) {
      fb->ColorDrawBuffer[buf] = GL_NONE;
   }

   if (_mesa_is_winsys_fbo(fb)) {
      /* also set context drawbuffer state */
      for (buf = 0; buf < ctx->Const.MaxDrawBuffers; buf++) {
         if (ctx->Color.DrawBuffer[buf] != fb->ColorDrawBuffer[buf]) {
	    updated_drawbuffers(ctx, fb);
            ctx->Color.DrawBuffer[buf] = fb->ColorDrawBuffer[buf];
         }
      }
   }
}


/**
 * Update the current drawbuffer's _ColorDrawBufferIndex[] list, etc.
 * from the context's Color.DrawBuffer[] state.
 * Use when changing contexts.
 */
void
_mesa_update_draw_buffers(struct gl_context *ctx)
{
   /* should be a window system FBO */
   assert(_mesa_is_winsys_fbo(ctx->DrawBuffer));

   _mesa_drawbuffers(ctx, ctx->DrawBuffer, ctx->Const.MaxDrawBuffers,
                     ctx->Color.DrawBuffer, NULL);
}


/**
 * Like \sa _mesa_drawbuffers(), this is a helper function for setting
 * GL_READ_BUFFER state for the given context and FBO.
 * Note that all error checking should have been done before calling
 * this function.
 * \param ctx  the rendering context
 * \param fb  the framebuffer object to update
 * \param buffer  GL_FRONT, GL_BACK, GL_COLOR_ATTACHMENT0, etc.
 * \param bufferIndex  the numerical index corresponding to 'buffer'
 */
void
_mesa_readbuffer(struct gl_context *ctx, struct gl_framebuffer *fb,
                 GLenum buffer, gl_buffer_index bufferIndex)
{
   if ((fb == ctx->ReadBuffer) && _mesa_is_winsys_fbo(fb)) {
      /* Only update the per-context READ_BUFFER state if we're bound to
       * a window-system framebuffer.
       */
      ctx->Pixel.ReadBuffer = buffer;
   }

   fb->ColorReadBuffer = buffer;
   fb->_ColorReadBufferIndex = bufferIndex;

   ctx->NewState |= _NEW_BUFFERS;
}



/**
 * Called by glReadBuffer and glNamedFramebufferReadBuffer to set the source
 * renderbuffer for reading pixels.
 * \param mode color buffer such as GL_FRONT, GL_BACK, etc.
 */
static void
read_buffer(struct gl_context *ctx, struct gl_framebuffer *fb,
            GLenum buffer, const char *caller)
{
   GLbitfield supportedMask;
   gl_buffer_index srcBuffer;

   FLUSH_VERTICES(ctx, 0);

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx, "%s %s\n", caller, _mesa_enum_to_string(buffer));

   if (buffer == GL_NONE) {
      /* This is legal--it means that no buffer should be bound for reading. */
      srcBuffer = -1;
   }
   else {
      /* general case / window-system framebuffer */
      if (_mesa_is_gles3(ctx) && !is_legal_es3_readbuffer_enum(buffer))
         srcBuffer = -1;
      else
         srcBuffer = read_buffer_enum_to_index(buffer);

      if (srcBuffer == -1) {
         _mesa_error(ctx, GL_INVALID_ENUM,
                     "%s(invalid buffer %s)", caller,
                     _mesa_enum_to_string(buffer));
         return;
      }
      supportedMask = supported_buffer_bitmask(ctx, fb);
      if (((1 << srcBuffer) & supportedMask) == 0) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "%s(invalid buffer %s)", caller,
                     _mesa_enum_to_string(buffer));
         return;
      }
   }

   /* OK, all error checking has been completed now */

   _mesa_readbuffer(ctx, fb, buffer, srcBuffer);

   /* Call the device driver function only if fb is the bound read buffer */
   if (fb == ctx->ReadBuffer) {
      if (ctx->Driver.ReadBuffer)
         ctx->Driver.ReadBuffer(ctx, buffer);
   }
}


void GLAPIENTRY
_mesa_ReadBuffer(GLenum buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   read_buffer(ctx, ctx->ReadBuffer, buffer, "glReadBuffer");
}


void GLAPIENTRY
_mesa_NamedFramebufferReadBuffer(GLuint framebuffer, GLenum src)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_framebuffer *fb;

   if (framebuffer) {
      fb = _mesa_lookup_framebuffer_err(ctx, framebuffer,
                                        "glNamedFramebufferReadBuffer");
      if (!fb)
         return;
   }
   else
      fb = ctx->WinSysReadBuffer;

   read_buffer(ctx, fb, src, "glNamedFramebufferReadBuffer");
}
