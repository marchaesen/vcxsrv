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

#include <stdbool.h>
#include "glheader.h"
#include "api_validate.h"
#include "bufferobj.h"
#include "context.h"
#include "imports.h"
#include "mtypes.h"
#include "enums.h"
#include "vbo/vbo.h"
#include "transformfeedback.h"
#include <stdbool.h>


/**
 * Check if OK to draw arrays/elements.
 */
static bool
check_valid_to_render(struct gl_context *ctx, const char *function)
{
   if (!_mesa_valid_to_render(ctx, function)) {
      return false;
   }

   switch (ctx->API) {
   case API_OPENGLES2:
      /* For ES2, we can draw if we have a vertex program/shader). */
      return ctx->VertexProgram._Current != NULL;

   case API_OPENGLES:
      /* For OpenGL ES, only draw if we have vertex positions
       */
      if (!ctx->Array.VAO->VertexAttrib[VERT_ATTRIB_POS].Enabled)
         return false;
      break;

   case API_OPENGL_CORE:
      /* Section 10.4 (Drawing Commands Using Vertex Arrays) of the OpenGL 4.5
       * Core Profile spec says:
       *
       *     "An INVALID_OPERATION error is generated if no vertex array
       *     object is bound (see section 10.3.1)."
       */
      if (ctx->Array.VAO == ctx->Array.DefaultVAO) {
         _mesa_error(ctx, GL_INVALID_OPERATION, "%s(no VAO bound)", function);
         return false;
      }

      /* The spec argues that this is allowed because a tess ctrl shader
       * without a tess eval shader can be used with transform feedback.
       * However, glBeginTransformFeedback doesn't allow GL_PATCHES and
       * therefore doesn't allow tessellation.
       *
       * Further investigation showed that this is indeed a spec bug and
       * a tess ctrl shader without a tess eval shader shouldn't have been
       * allowed, because there is no API in GL 4.0 that can make use this
       * to produce something useful.
       *
       * Also, all vendors except one don't support a tess ctrl shader without
       * a tess eval shader anyway.
       */
      if (ctx->TessCtrlProgram._Current && !ctx->TessEvalProgram._Current) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "%s(tess eval shader is missing)", function);
         return false;
      }

      /* Section 7.3 (Program Objects) of the OpenGL 4.5 Core Profile spec
       * says:
       *
       *     "If there is no active program for the vertex or fragment shader
       *     stages, the results of vertex and/or fragment processing will be
       *     undefined. However, this is not an error."
       *
       * The fragment shader is not tested here because other state (e.g.,
       * GL_RASTERIZER_DISCARD) affects whether or not we actually care.
       */
      return ctx->VertexProgram._Current != NULL;

   case API_OPENGL_COMPAT:
      if (ctx->VertexProgram._Current != NULL) {
         /* Draw regardless of whether or not we have any vertex arrays.
          * (Ex: could draw a point using a constant vertex pos)
          */
         return true;
      } else {
         /* Draw if we have vertex positions (GL_VERTEX_ARRAY or generic
          * array [0]).
          */
         return (ctx->Array.VAO->VertexAttrib[VERT_ATTRIB_POS].Enabled ||
                 ctx->Array.VAO->VertexAttrib[VERT_ATTRIB_GENERIC0].Enabled);
      }
      break;

   default:
      unreachable("Invalid API value in check_valid_to_render()");
   }

   return true;
}


/**
 * Is 'mode' a valid value for glBegin(), glDrawArrays(), glDrawElements(),
 * etc?  The set of legal values depends on whether geometry shaders/programs
 * are supported.
 * Note: This may be called during display list compilation.
 */
bool
_mesa_is_valid_prim_mode(struct gl_context *ctx, GLenum mode)
{
   /* The overwhelmingly common case is (mode <= GL_TRIANGLE_FAN).  Test that
    * first and exit.  You would think that a switch-statement would be the
    * right approach, but at least GCC 4.7.2 generates some pretty dire code
    * for the common case.
    */
   if (likely(mode <= GL_TRIANGLE_FAN))
      return true;

   if (mode <= GL_POLYGON)
      return (ctx->API == API_OPENGL_COMPAT);

   if (mode <= GL_TRIANGLE_STRIP_ADJACENCY)
      return _mesa_has_geometry_shaders(ctx);

   if (mode == GL_PATCHES)
      return _mesa_has_tessellation(ctx);

   return false;
}


/**
 * Is 'mode' a valid value for glBegin(), glDrawArrays(), glDrawElements(),
 * etc?  Also, do additional checking related to transformation feedback.
 * Note: this function cannot be called during glNewList(GL_COMPILE) because
 * this code depends on current transform feedback state.
 * Also, do additional checking related to tessellation shaders.
 */
GLboolean
_mesa_valid_prim_mode(struct gl_context *ctx, GLenum mode, const char *name)
{
   bool valid_enum = _mesa_is_valid_prim_mode(ctx, mode);

   if (!valid_enum) {
      _mesa_error(ctx, GL_INVALID_ENUM, "%s(mode=%x)", name, mode);
      return GL_FALSE;
   }

   /* From the OpenGL 4.5 specification, section 11.3.1:
    *
    * The error INVALID_OPERATION is generated if Begin, or any command that
    * implicitly calls Begin, is called when a geometry shader is active and:
    *
    * * the input primitive type of the current geometry shader is
    *   POINTS and <mode> is not POINTS,
    *
    * * the input primitive type of the current geometry shader is
    *   LINES and <mode> is not LINES, LINE_STRIP, or LINE_LOOP,
    *
    * * the input primitive type of the current geometry shader is
    *   TRIANGLES and <mode> is not TRIANGLES, TRIANGLE_STRIP or
    *   TRIANGLE_FAN,
    *
    * * the input primitive type of the current geometry shader is
    *   LINES_ADJACENCY_ARB and <mode> is not LINES_ADJACENCY_ARB or
    *   LINE_STRIP_ADJACENCY_ARB, or
    *
    * * the input primitive type of the current geometry shader is
    *   TRIANGLES_ADJACENCY_ARB and <mode> is not
    *   TRIANGLES_ADJACENCY_ARB or TRIANGLE_STRIP_ADJACENCY_ARB.
    *
    * The GL spec doesn't mention any interaction with tessellation, which
    * is clearly a spec bug. The same rule should apply, but instead of
    * the draw primitive mode, the tessellation evaluation shader primitive
    * mode should be used for the checking.
   */
   if (ctx->_Shader->CurrentProgram[MESA_SHADER_GEOMETRY]) {
      const GLenum geom_mode =
         ctx->_Shader->CurrentProgram[MESA_SHADER_GEOMETRY]->
            _LinkedShaders[MESA_SHADER_GEOMETRY]->Geom.InputType;
      struct gl_shader_program *tes =
         ctx->_Shader->CurrentProgram[MESA_SHADER_TESS_EVAL];
      GLenum mode_before_gs = mode;

      if (tes) {
         struct gl_shader *tes_sh = tes->_LinkedShaders[MESA_SHADER_TESS_EVAL];
         if (tes_sh->TessEval.PointMode)
            mode_before_gs = GL_POINTS;
         else if (tes_sh->TessEval.PrimitiveMode == GL_ISOLINES)
            mode_before_gs = GL_LINES;
         else
            /* the GL_QUADS mode generates triangles too */
            mode_before_gs = GL_TRIANGLES;
      }

      switch (mode_before_gs) {
      case GL_POINTS:
         valid_enum = (geom_mode == GL_POINTS);
         break;
      case GL_LINES:
      case GL_LINE_LOOP:
      case GL_LINE_STRIP:
         valid_enum = (geom_mode == GL_LINES);
         break;
      case GL_TRIANGLES:
      case GL_TRIANGLE_STRIP:
      case GL_TRIANGLE_FAN:
         valid_enum = (geom_mode == GL_TRIANGLES);
         break;
      case GL_QUADS:
      case GL_QUAD_STRIP:
      case GL_POLYGON:
         valid_enum = false;
         break;
      case GL_LINES_ADJACENCY:
      case GL_LINE_STRIP_ADJACENCY:
         valid_enum = (geom_mode == GL_LINES_ADJACENCY);
         break;
      case GL_TRIANGLES_ADJACENCY:
      case GL_TRIANGLE_STRIP_ADJACENCY:
         valid_enum = (geom_mode == GL_TRIANGLES_ADJACENCY);
         break;
      default:
         valid_enum = false;
         break;
      }
      if (!valid_enum) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "%s(mode=%s vs geometry shader input %s)",
                     name,
                     _mesa_lookup_prim_by_nr(mode_before_gs),
                     _mesa_lookup_prim_by_nr(geom_mode));
         return GL_FALSE;
      }
   }

   /* From the OpenGL 4.0 (Core Profile) spec (section 2.12):
    *
    *     "Tessellation operates only on patch primitives. If tessellation is
    *      active, any command that transfers vertices to the GL will
    *      generate an INVALID_OPERATION error if the primitive mode is not
    *      PATCHES.
    *      Patch primitives are not supported by pipeline stages below the
    *      tessellation evaluation shader. If there is no active program
    *      object or the active program object does not contain a tessellation
    *      evaluation shader, the error INVALID_OPERATION is generated by any
    *      command that transfers vertices to the GL if the primitive mode is
    *      PATCHES."
    *
    */
   if (ctx->_Shader->CurrentProgram[MESA_SHADER_TESS_EVAL] ||
       ctx->_Shader->CurrentProgram[MESA_SHADER_TESS_CTRL]) {
      if (mode != GL_PATCHES) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "only GL_PATCHES valid with tessellation");
         return GL_FALSE;
      }
   }
   else {
      if (mode == GL_PATCHES) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "GL_PATCHES only valid with tessellation");
         return GL_FALSE;
      }
   }

   /* From the GL_EXT_transform_feedback spec:
    *
    *     "The error INVALID_OPERATION is generated if Begin, or any command
    *      that performs an explicit Begin, is called when:
    *
    *      * a geometry shader is not active and <mode> does not match the
    *        allowed begin modes for the current transform feedback state as
    *        given by table X.1.
    *
    *      * a geometry shader is active and the output primitive type of the
    *        geometry shader does not match the allowed begin modes for the
    *        current transform feedback state as given by table X.1.
    *
    */
   if (_mesa_is_xfb_active_and_unpaused(ctx)) {
      GLboolean pass = GL_TRUE;

      if(ctx->_Shader->CurrentProgram[MESA_SHADER_GEOMETRY]) {
         switch (ctx->_Shader->CurrentProgram[MESA_SHADER_GEOMETRY]->
                    _LinkedShaders[MESA_SHADER_GEOMETRY]->Geom.OutputType) {
         case GL_POINTS:
            pass = ctx->TransformFeedback.Mode == GL_POINTS;
            break;
         case GL_LINE_STRIP:
            pass = ctx->TransformFeedback.Mode == GL_LINES;
            break;
         case GL_TRIANGLE_STRIP:
            pass = ctx->TransformFeedback.Mode == GL_TRIANGLES;
            break;
         default:
            pass = GL_FALSE;
         }
      }
      else if (ctx->_Shader->CurrentProgram[MESA_SHADER_TESS_EVAL]) {
         struct gl_shader_program *tes =
            ctx->_Shader->CurrentProgram[MESA_SHADER_TESS_EVAL];
         struct gl_shader *tes_sh = tes->_LinkedShaders[MESA_SHADER_TESS_EVAL];
         if (tes_sh->TessEval.PointMode)
            pass = ctx->TransformFeedback.Mode == GL_POINTS;
         else if (tes_sh->TessEval.PrimitiveMode == GL_ISOLINES)
            pass = ctx->TransformFeedback.Mode == GL_LINES;
         else
            pass = ctx->TransformFeedback.Mode == GL_TRIANGLES;
      }
      else {
         switch (mode) {
         case GL_POINTS:
            pass = ctx->TransformFeedback.Mode == GL_POINTS;
            break;
         case GL_LINES:
         case GL_LINE_STRIP:
         case GL_LINE_LOOP:
            pass = ctx->TransformFeedback.Mode == GL_LINES;
            break;
         default:
            pass = ctx->TransformFeedback.Mode == GL_TRIANGLES;
            break;
         }
      }
      if (!pass) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                         "%s(mode=%s vs transform feedback %s)",
                         name,
                         _mesa_lookup_prim_by_nr(mode),
                         _mesa_lookup_prim_by_nr(ctx->TransformFeedback.Mode));
         return GL_FALSE;
      }
   }

   return GL_TRUE;
}

/**
 * Verify that the element type is valid.
 *
 * Generates \c GL_INVALID_ENUM and returns \c false if it is not.
 */
static bool
valid_elements_type(struct gl_context *ctx, GLenum type, const char *name)
{
   switch (type) {
   case GL_UNSIGNED_BYTE:
   case GL_UNSIGNED_SHORT:
   case GL_UNSIGNED_INT:
      return true;

   default:
      _mesa_error(ctx, GL_INVALID_ENUM, "%s(type = %s)", name,
                  _mesa_enum_to_string(type));
      return false;
   }
}

static bool
validate_DrawElements_common(struct gl_context *ctx,
                             GLenum mode, GLsizei count, GLenum type,
                             const GLvoid *indices,
                             const char *caller)
{
   /* From the GLES3 specification, section 2.14.2 (Transform Feedback
    * Primitive Capture):
    *
    *   The error INVALID_OPERATION is also generated by DrawElements,
    *   DrawElementsInstanced, and DrawRangeElements while transform feedback
    *   is active and not paused, regardless of mode.
    */
   if (_mesa_is_gles3(ctx) && !ctx->Extensions.OES_geometry_shader &&
       _mesa_is_xfb_active_and_unpaused(ctx)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(transform feedback active)", caller);
      return false;
   }

   if (count < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(count)", caller);
      return false;
   }

   if (!_mesa_valid_prim_mode(ctx, mode, caller)) {
      return false;
   }

   if (!valid_elements_type(ctx, type, caller))
      return false;

   if (!check_valid_to_render(ctx, caller))
      return false;

   /* Not using a VBO for indices, so avoid NULL pointer derefs later.
    */
   if (!_mesa_is_bufferobj(ctx->Array.VAO->IndexBufferObj) && indices == NULL)
      return false;

   if (count == 0)
      return false;

   return true;
}

/**
 * Error checking for glDrawElements().  Includes parameter checking
 * and VBO bounds checking.
 * \return GL_TRUE if OK to render, GL_FALSE if error found
 */
GLboolean
_mesa_validate_DrawElements(struct gl_context *ctx,
                            GLenum mode, GLsizei count, GLenum type,
                            const GLvoid *indices)
{
   FLUSH_CURRENT(ctx, 0);

   return validate_DrawElements_common(ctx, mode, count, type, indices,
                                       "glDrawElements");
}


/**
 * Error checking for glMultiDrawElements().  Includes parameter checking
 * and VBO bounds checking.
 * \return GL_TRUE if OK to render, GL_FALSE if error found
 */
GLboolean
_mesa_validate_MultiDrawElements(struct gl_context *ctx,
                                 GLenum mode, const GLsizei *count,
                                 GLenum type, const GLvoid * const *indices,
                                 GLuint primcount)
{
   unsigned i;

   FLUSH_CURRENT(ctx, 0);

   for (i = 0; i < primcount; i++) {
      if (count[i] < 0) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glMultiDrawElements(count)" );
         return GL_FALSE;
      }
   }

   if (!_mesa_valid_prim_mode(ctx, mode, "glMultiDrawElements")) {
      return GL_FALSE;
   }

   if (!valid_elements_type(ctx, type, "glMultiDrawElements"))
      return GL_FALSE;

   if (!check_valid_to_render(ctx, "glMultiDrawElements"))
      return GL_FALSE;

   /* Not using a VBO for indices, so avoid NULL pointer derefs later.
    */
   if (!_mesa_is_bufferobj(ctx->Array.VAO->IndexBufferObj)) {
      for (i = 0; i < primcount; i++) {
         if (!indices[i])
            return GL_FALSE;
      }
   }

   return GL_TRUE;
}


/**
 * Error checking for glDrawRangeElements().  Includes parameter checking
 * and VBO bounds checking.
 * \return GL_TRUE if OK to render, GL_FALSE if error found
 */
GLboolean
_mesa_validate_DrawRangeElements(struct gl_context *ctx, GLenum mode,
                                 GLuint start, GLuint end,
                                 GLsizei count, GLenum type,
                                 const GLvoid *indices)
{
   FLUSH_CURRENT(ctx, 0);

   if (end < start) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glDrawRangeElements(end<start)");
      return GL_FALSE;
   }

   return validate_DrawElements_common(ctx, mode, count, type, indices,
                                       "glDrawRangeElements");
}


/**
 * Called from the tnl module to error check the function parameters and
 * verify that we really can draw something.
 * \return GL_TRUE if OK to render, GL_FALSE if error found
 */
GLboolean
_mesa_validate_DrawArrays(struct gl_context *ctx, GLenum mode, GLsizei count)
{
   struct gl_transform_feedback_object *xfb_obj
      = ctx->TransformFeedback.CurrentObject;
   FLUSH_CURRENT(ctx, 0);

   if (count < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glDrawArrays(count)" );
      return GL_FALSE;
   }

   if (!_mesa_valid_prim_mode(ctx, mode, "glDrawArrays")) {
      return GL_FALSE;
   }

   if (!check_valid_to_render(ctx, "glDrawArrays"))
      return GL_FALSE;

   /* From the GLES3 specification, section 2.14.2 (Transform Feedback
    * Primitive Capture):
    *
    *   The error INVALID_OPERATION is generated by DrawArrays and
    *   DrawArraysInstanced if recording the vertices of a primitive to the
    *   buffer objects being used for transform feedback purposes would result
    *   in either exceeding the limits of any buffer object’s size, or in
    *   exceeding the end position offset + size − 1, as set by
    *   BindBufferRange.
    *
    * This is in contrast to the behaviour of desktop GL, where the extra
    * primitives are silently dropped from the transform feedback buffer.
    */
   if (_mesa_is_gles3(ctx) && _mesa_is_xfb_active_and_unpaused(ctx)) {
      size_t prim_count = vbo_count_tessellated_primitives(mode, count, 1);
      if (xfb_obj->GlesRemainingPrims < prim_count) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "glDrawArrays(exceeds transform feedback size)");
         return GL_FALSE;
      }
      xfb_obj->GlesRemainingPrims -= prim_count;
   }

   if (count == 0)
      return GL_FALSE;

   return GL_TRUE;
}


GLboolean
_mesa_validate_DrawArraysInstanced(struct gl_context *ctx, GLenum mode, GLint first,
                                   GLsizei count, GLsizei numInstances)
{
   struct gl_transform_feedback_object *xfb_obj
      = ctx->TransformFeedback.CurrentObject;
   FLUSH_CURRENT(ctx, 0);

   if (count < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glDrawArraysInstanced(count=%d)", count);
      return GL_FALSE;
   }

   if (first < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glDrawArraysInstanced(start=%d)", first);
      return GL_FALSE;
   }

   if (!_mesa_valid_prim_mode(ctx, mode, "glDrawArraysInstanced")) {
      return GL_FALSE;
   }

   if (numInstances <= 0) {
      if (numInstances < 0)
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glDrawArraysInstanced(numInstances=%d)", numInstances);
      return GL_FALSE;
   }

   if (!check_valid_to_render(ctx, "glDrawArraysInstanced(invalid to render)"))
      return GL_FALSE;

   /* From the GLES3 specification, section 2.14.2 (Transform Feedback
    * Primitive Capture):
    *
    *   The error INVALID_OPERATION is generated by DrawArrays and
    *   DrawArraysInstanced if recording the vertices of a primitive to the
    *   buffer objects being used for transform feedback purposes would result
    *   in either exceeding the limits of any buffer object’s size, or in
    *   exceeding the end position offset + size − 1, as set by
    *   BindBufferRange.
    *
    * This is in contrast to the behaviour of desktop GL, where the extra
    * primitives are silently dropped from the transform feedback buffer.
    */
   if (_mesa_is_gles3(ctx) && _mesa_is_xfb_active_and_unpaused(ctx)) {
      size_t prim_count
         = vbo_count_tessellated_primitives(mode, count, numInstances);
      if (xfb_obj->GlesRemainingPrims < prim_count) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "glDrawArraysInstanced(exceeds transform feedback size)");
         return GL_FALSE;
      }
      xfb_obj->GlesRemainingPrims -= prim_count;
   }

   if (count == 0)
      return GL_FALSE;

   return GL_TRUE;
}


GLboolean
_mesa_validate_DrawElementsInstanced(struct gl_context *ctx,
                                     GLenum mode, GLsizei count, GLenum type,
                                     const GLvoid *indices, GLsizei numInstances)
{
   FLUSH_CURRENT(ctx, 0);

   if (numInstances < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glDrawElementsInstanced(numInstances=%d)", numInstances);
      return GL_FALSE;
   }

   return validate_DrawElements_common(ctx, mode, count, type, indices,
                                       "glDrawElementsInstanced")
      && (numInstances > 0);
}


GLboolean
_mesa_validate_DrawTransformFeedback(struct gl_context *ctx,
                                     GLenum mode,
                                     struct gl_transform_feedback_object *obj,
                                     GLuint stream,
                                     GLsizei numInstances)
{
   FLUSH_CURRENT(ctx, 0);

   if (!_mesa_valid_prim_mode(ctx, mode, "glDrawTransformFeedback*(mode)")) {
      return GL_FALSE;
   }

   if (!obj) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glDrawTransformFeedback*(name)");
      return GL_FALSE;
   }

   /* From the GL 4.5 specification, page 429:
    * "An INVALID_VALUE error is generated if id is not the name of a
    *  transform feedback object."
    */
   if (!obj->EverBound) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glDrawTransformFeedback*(name)");
      return GL_FALSE;
   }

   if (stream >= ctx->Const.MaxVertexStreams) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glDrawTransformFeedbackStream*(index>=MaxVertexStream)");
      return GL_FALSE;
   }

   if (!obj->EndedAnytime) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "glDrawTransformFeedback*");
      return GL_FALSE;
   }

   if (numInstances <= 0) {
      if (numInstances < 0)
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glDrawTransformFeedback*Instanced(numInstances=%d)",
                     numInstances);
      return GL_FALSE;
   }

   if (!check_valid_to_render(ctx, "glDrawTransformFeedback*")) {
      return GL_FALSE;
   }

   return GL_TRUE;
}

static GLboolean
valid_draw_indirect(struct gl_context *ctx,
                    GLenum mode, const GLvoid *indirect,
                    GLsizei size, const char *name)
{
   const uint64_t end = (uint64_t) (uintptr_t) indirect + size;

   /* OpenGL ES 3.1 spec. section 10.5:
    *
    *      "DrawArraysIndirect requires that all data sourced for the
    *      command, including the DrawArraysIndirectCommand
    *      structure,  be in buffer objects,  and may not be called when
    *      the default vertex array object is bound."
    */
   if (ctx->Array.VAO == ctx->Array.DefaultVAO) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "(no VAO bound)");
      return GL_FALSE;
   }

   /* From OpenGL ES 3.1 spec. section 10.5:
    *     "An INVALID_OPERATION error is generated if zero is bound to
    *     VERTEX_ARRAY_BINDING, DRAW_INDIRECT_BUFFER or to any enabled
    *     vertex array."
    *
    * Here we check that for each enabled vertex array we have a vertex
    * buffer bound.
    */
   if (_mesa_is_gles31(ctx) &&
       ctx->Array.VAO->_Enabled != ctx->Array.VAO->VertexAttribBufferMask) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(No VBO bound)", name);
      return GL_FALSE;
   }

   if (!_mesa_valid_prim_mode(ctx, mode, name))
      return GL_FALSE;

   /* OpenGL ES 3.1 specification, section 10.5:
    *
    *      "An INVALID_OPERATION error is generated if
    *      transform feedback is active and not paused."
    */
   if (_mesa_is_gles31(ctx) && !ctx->Extensions.OES_geometry_shader &&
       _mesa_is_xfb_active_and_unpaused(ctx)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(TransformFeedback is active and not paused)", name);
   }

   /* From OpenGL version 4.4. section 10.5
    * and OpenGL ES 3.1, section 10.6:
    *
    *      "An INVALID_VALUE error is generated if indirect is not a
    *       multiple of the size, in basic machine units, of uint."
    */
   if ((GLsizeiptr)indirect & (sizeof(GLuint) - 1)) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(indirect is not aligned)", name);
      return GL_FALSE;
   }

   if (!_mesa_is_bufferobj(ctx->DrawIndirectBuffer)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s: no buffer bound to DRAW_INDIRECT_BUFFER", name);
      return GL_FALSE;
   }

   if (_mesa_check_disallowed_mapping(ctx->DrawIndirectBuffer)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(DRAW_INDIRECT_BUFFER is mapped)", name);
      return GL_FALSE;
   }

   /* From the ARB_draw_indirect specification:
    * "An INVALID_OPERATION error is generated if the commands source data
    *  beyond the end of the buffer object [...]"
    */
   if (ctx->DrawIndirectBuffer->Size < end) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(DRAW_INDIRECT_BUFFER too small)", name);
      return GL_FALSE;
   }

   if (!check_valid_to_render(ctx, name))
      return GL_FALSE;

   return GL_TRUE;
}

static inline GLboolean
valid_draw_indirect_elements(struct gl_context *ctx,
                             GLenum mode, GLenum type, const GLvoid *indirect,
                             GLsizeiptr size, const char *name)
{
   if (!valid_elements_type(ctx, type, name))
      return GL_FALSE;

   /*
    * Unlike regular DrawElementsInstancedBaseVertex commands, the indices
    * may not come from a client array and must come from an index buffer.
    * If no element array buffer is bound, an INVALID_OPERATION error is
    * generated.
    */
   if (!_mesa_is_bufferobj(ctx->Array.VAO->IndexBufferObj)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(no buffer bound to GL_ELEMENT_ARRAY_BUFFER)", name);
      return GL_FALSE;
   }

   return valid_draw_indirect(ctx, mode, indirect, size, name);
}

static inline GLboolean
valid_draw_indirect_multi(struct gl_context *ctx,
                          GLsizei primcount, GLsizei stride,
                          const char *name)
{

   /* From the ARB_multi_draw_indirect specification:
    * "INVALID_VALUE is generated by MultiDrawArraysIndirect or
    *  MultiDrawElementsIndirect if <primcount> is negative."
    *
    * "<primcount> must be positive, otherwise an INVALID_VALUE error will
    *  be generated."
    */
   if (primcount < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(primcount < 0)", name);
      return GL_FALSE;
   }


   /* From the ARB_multi_draw_indirect specification:
    * "<stride> must be a multiple of four, otherwise an INVALID_VALUE
    *  error is generated."
    */
   if (stride % 4) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(stride %% 4)", name);
      return GL_FALSE;
   }

   return GL_TRUE;
}

GLboolean
_mesa_validate_DrawArraysIndirect(struct gl_context *ctx,
                                  GLenum mode,
                                  const GLvoid *indirect)
{
   const unsigned drawArraysNumParams = 4;

   FLUSH_CURRENT(ctx, 0);

   return valid_draw_indirect(ctx, mode,
                              indirect, drawArraysNumParams * sizeof(GLuint),
                              "glDrawArraysIndirect");
}

GLboolean
_mesa_validate_DrawElementsIndirect(struct gl_context *ctx,
                                    GLenum mode, GLenum type,
                                    const GLvoid *indirect)
{
   const unsigned drawElementsNumParams = 5;

   FLUSH_CURRENT(ctx, 0);

   return valid_draw_indirect_elements(ctx, mode, type,
                                       indirect, drawElementsNumParams * sizeof(GLuint),
                                       "glDrawElementsIndirect");
}

GLboolean
_mesa_validate_MultiDrawArraysIndirect(struct gl_context *ctx,
                                       GLenum mode,
                                       const GLvoid *indirect,
                                       GLsizei primcount, GLsizei stride)
{
   GLsizeiptr size = 0;
   const unsigned drawArraysNumParams = 4;

   FLUSH_CURRENT(ctx, 0);

   /* caller has converted stride==0 to drawArraysNumParams * sizeof(GLuint) */
   assert(stride != 0);

   if (!valid_draw_indirect_multi(ctx, primcount, stride,
                                  "glMultiDrawArraysIndirect"))
      return GL_FALSE;

   /* number of bytes of the indirect buffer which will be read */
   size = primcount
      ? (primcount - 1) * stride + drawArraysNumParams * sizeof(GLuint)
      : 0;

   if (!valid_draw_indirect(ctx, mode, indirect, size,
                            "glMultiDrawArraysIndirect"))
      return GL_FALSE;

   return GL_TRUE;
}

GLboolean
_mesa_validate_MultiDrawElementsIndirect(struct gl_context *ctx,
                                         GLenum mode, GLenum type,
                                         const GLvoid *indirect,
                                         GLsizei primcount, GLsizei stride)
{
   GLsizeiptr size = 0;
   const unsigned drawElementsNumParams = 5;

   FLUSH_CURRENT(ctx, 0);

   /* caller has converted stride==0 to drawElementsNumParams * sizeof(GLuint) */
   assert(stride != 0);

   if (!valid_draw_indirect_multi(ctx, primcount, stride,
                                  "glMultiDrawElementsIndirect"))
      return GL_FALSE;

   /* number of bytes of the indirect buffer which will be read */
   size = primcount
      ? (primcount - 1) * stride + drawElementsNumParams * sizeof(GLuint)
      : 0;

   if (!valid_draw_indirect_elements(ctx, mode, type,
                                     indirect, size,
                                     "glMultiDrawElementsIndirect"))
      return GL_FALSE;

   return GL_TRUE;
}

static GLboolean
valid_draw_indirect_parameters(struct gl_context *ctx,
                               const char *name,
                               GLintptr drawcount)
{
   /* From the ARB_indirect_parameters specification:
    * "INVALID_VALUE is generated by MultiDrawArraysIndirectCountARB or
    *  MultiDrawElementsIndirectCountARB if <drawcount> is not a multiple of
    *  four."
    */
   if (drawcount & 3) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(drawcount is not a multiple of 4)", name);
      return GL_FALSE;
   }

   /* From the ARB_indirect_parameters specification:
    * "INVALID_OPERATION is generated by MultiDrawArraysIndirectCountARB or
    *  MultiDrawElementsIndirectCountARB if no buffer is bound to the
    *  PARAMETER_BUFFER_ARB binding point."
    */
   if (!_mesa_is_bufferobj(ctx->ParameterBuffer)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s: no buffer bound to PARAMETER_BUFFER", name);
      return GL_FALSE;
   }

   if (_mesa_check_disallowed_mapping(ctx->ParameterBuffer)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(PARAMETER_BUFFER is mapped)", name);
      return GL_FALSE;
   }

   /* From the ARB_indirect_parameters specification:
    * "INVALID_OPERATION is generated by MultiDrawArraysIndirectCountARB or
    *  MultiDrawElementsIndirectCountARB if reading a <sizei> typed value
    *  from the buffer bound to the PARAMETER_BUFFER_ARB target at the offset
    *  specified by <drawcount> would result in an out-of-bounds access."
    */
   if (ctx->ParameterBuffer->Size < drawcount + sizeof(GLsizei)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(PARAMETER_BUFFER too small)", name);
      return GL_FALSE;
   }

   return GL_TRUE;
}

GLboolean
_mesa_validate_MultiDrawArraysIndirectCount(struct gl_context *ctx,
                                            GLenum mode,
                                            GLintptr indirect,
                                            GLintptr drawcount,
                                            GLsizei maxdrawcount,
                                            GLsizei stride)
{
   GLsizeiptr size = 0;
   const unsigned drawArraysNumParams = 4;

   FLUSH_CURRENT(ctx, 0);

   /* caller has converted stride==0 to drawArraysNumParams * sizeof(GLuint) */
   assert(stride != 0);

   if (!valid_draw_indirect_multi(ctx, maxdrawcount, stride,
                                  "glMultiDrawArraysIndirectCountARB"))
      return GL_FALSE;

   /* number of bytes of the indirect buffer which will be read */
   size = maxdrawcount
      ? (maxdrawcount - 1) * stride + drawArraysNumParams * sizeof(GLuint)
      : 0;

   if (!valid_draw_indirect(ctx, mode, (void *)indirect, size,
                            "glMultiDrawArraysIndirectCountARB"))
      return GL_FALSE;

   return valid_draw_indirect_parameters(
         ctx, "glMultiDrawArraysIndirectCountARB", drawcount);
}

GLboolean
_mesa_validate_MultiDrawElementsIndirectCount(struct gl_context *ctx,
                                              GLenum mode, GLenum type,
                                              GLintptr indirect,
                                              GLintptr drawcount,
                                              GLsizei maxdrawcount,
                                              GLsizei stride)
{
   GLsizeiptr size = 0;
   const unsigned drawElementsNumParams = 5;

   FLUSH_CURRENT(ctx, 0);

   /* caller has converted stride==0 to drawElementsNumParams * sizeof(GLuint) */
   assert(stride != 0);

   if (!valid_draw_indirect_multi(ctx, maxdrawcount, stride,
                                  "glMultiDrawElementsIndirectCountARB"))
      return GL_FALSE;

   /* number of bytes of the indirect buffer which will be read */
   size = maxdrawcount
      ? (maxdrawcount - 1) * stride + drawElementsNumParams * sizeof(GLuint)
      : 0;

   if (!valid_draw_indirect_elements(ctx, mode, type,
                                     (void *)indirect, size,
                                     "glMultiDrawElementsIndirectCountARB"))
      return GL_FALSE;

   return valid_draw_indirect_parameters(
         ctx, "glMultiDrawElementsIndirectCountARB", drawcount);
}

static bool
check_valid_to_compute(struct gl_context *ctx, const char *function)
{
   struct gl_shader_program *prog;

   if (!_mesa_has_compute_shaders(ctx)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "unsupported function (%s) called",
                  function);
      return false;
   }

   /* From the OpenGL 4.3 Core Specification, Chapter 19, Compute Shaders:
    *
    * "An INVALID_OPERATION error is generated if there is no active program
    *  for the compute shader stage."
    */
   prog = ctx->_Shader->CurrentProgram[MESA_SHADER_COMPUTE];
   if (prog == NULL || prog->_LinkedShaders[MESA_SHADER_COMPUTE] == NULL) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(no active compute shader)",
                  function);
      return false;
   }

   return true;
}

GLboolean
_mesa_validate_DispatchCompute(struct gl_context *ctx,
                               const GLuint *num_groups)
{
   int i;
   FLUSH_CURRENT(ctx, 0);

   if (!check_valid_to_compute(ctx, "glDispatchCompute"))
      return GL_FALSE;

   for (i = 0; i < 3; i++) {
      /* From the OpenGL 4.3 Core Specification, Chapter 19, Compute Shaders:
       *
       * "An INVALID_VALUE error is generated if any of num_groups_x,
       *  num_groups_y and num_groups_z are greater than or equal to the
       *  maximum work group count for the corresponding dimension."
       *
       * However, the "or equal to" portions appears to be a specification
       * bug. In all other areas, the specification appears to indicate that
       * the number of workgroups can match the MAX_COMPUTE_WORK_GROUP_COUNT
       * value. For example, under DispatchComputeIndirect:
       *
       * "If any of num_groups_x, num_groups_y or num_groups_z is greater than
       *  the value of MAX_COMPUTE_WORK_GROUP_COUNT for the corresponding
       *  dimension then the results are undefined."
       *
       * Additionally, the OpenGLES 3.1 specification does not contain "or
       * equal to" as an error condition.
       */
      if (num_groups[i] > ctx->Const.MaxComputeWorkGroupCount[i]) {
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "glDispatchCompute(num_groups_%c)", 'x' + i);
         return GL_FALSE;
      }
   }

   return GL_TRUE;
}

static GLboolean
valid_dispatch_indirect(struct gl_context *ctx,
                        GLintptr indirect,
                        GLsizei size, const char *name)
{
   const uint64_t end = (uint64_t) indirect + size;

   if (!check_valid_to_compute(ctx, name))
      return GL_FALSE;

   /* From the OpenGL 4.3 Core Specification, Chapter 19, Compute Shaders:
    *
    * "An INVALID_VALUE error is generated if indirect is negative or is not a
    *  multiple of four."
    */
   if (indirect & (sizeof(GLuint) - 1)) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(indirect is not aligned)", name);
      return GL_FALSE;
   }

   if (indirect < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(indirect is less than zero)", name);
      return GL_FALSE;
   }

   /* From the OpenGL 4.3 Core Specification, Chapter 19, Compute Shaders:
    *
    * "An INVALID_OPERATION error is generated if no buffer is bound to the
    *  DRAW_INDIRECT_BUFFER binding, or if the command would source data
    *  beyond the end of the buffer object."
    */
   if (!_mesa_is_bufferobj(ctx->DispatchIndirectBuffer)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s: no buffer bound to DISPATCH_INDIRECT_BUFFER", name);
      return GL_FALSE;
   }

   if (_mesa_check_disallowed_mapping(ctx->DispatchIndirectBuffer)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(DISPATCH_INDIRECT_BUFFER is mapped)", name);
      return GL_FALSE;
   }

   if (ctx->DispatchIndirectBuffer->Size < end) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(DISPATCH_INDIRECT_BUFFER too small)", name);
      return GL_FALSE;
   }

   return GL_TRUE;
}

GLboolean
_mesa_validate_DispatchComputeIndirect(struct gl_context *ctx,
                                       GLintptr indirect)
{
   FLUSH_CURRENT(ctx, 0);

   return valid_dispatch_indirect(ctx, indirect, 3 * sizeof(GLuint),
                                  "glDispatchComputeIndirect");
}
