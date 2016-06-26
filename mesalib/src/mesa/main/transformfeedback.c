/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2010  VMware, Inc.  All Rights Reserved.
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
 * Transform feedback support.
 *
 * Authors:
 *   Brian Paul
 */


#include "buffers.h"
#include "context.h"
#include "hash.h"
#include "macros.h"
#include "mtypes.h"
#include "transformfeedback.h"
#include "shaderapi.h"
#include "shaderobj.h"
#include "main/dispatch.h"

#include "program/prog_parameter.h"

struct using_program_tuple
{
   struct gl_shader_program *shProg;
   bool found;
};

static void
active_xfb_object_references_program(GLuint key, void *data, void *user_data)
{
   struct using_program_tuple *callback_data = user_data;
   struct gl_transform_feedback_object *obj = data;
   if (obj->Active && obj->shader_program == callback_data->shProg)
      callback_data->found = true;
}

/**
 * Return true if any active transform feedback object is using a program.
 */
bool
_mesa_transform_feedback_is_using_program(struct gl_context *ctx,
                                          struct gl_shader_program *shProg)
{
   struct using_program_tuple callback_data;
   callback_data.shProg = shProg;
   callback_data.found = false;

   _mesa_HashWalk(ctx->TransformFeedback.Objects,
                  active_xfb_object_references_program, &callback_data);

   /* Also check DefaultObject, as it's not in the Objects hash table. */
   active_xfb_object_references_program(0, ctx->TransformFeedback.DefaultObject,
                                        &callback_data);

   return callback_data.found;
}

/**
 * Do reference counting of transform feedback buffers.
 */
static void
reference_transform_feedback_object(struct gl_transform_feedback_object **ptr,
                                    struct gl_transform_feedback_object *obj)
{
   if (*ptr == obj)
      return;

   if (*ptr) {
      /* Unreference the old object */
      struct gl_transform_feedback_object *oldObj = *ptr;

      assert(oldObj->RefCount > 0);
      oldObj->RefCount--;

      if (oldObj->RefCount == 0) {
         GET_CURRENT_CONTEXT(ctx);
         if (ctx)
            ctx->Driver.DeleteTransformFeedback(ctx, oldObj);
      }

      *ptr = NULL;
   }
   assert(!*ptr);

   if (obj) {
      /* reference new object */
      if (obj->RefCount == 0) {
         _mesa_problem(NULL, "referencing deleted transform feedback object");
         *ptr = NULL;
      }
      else {
         obj->RefCount++;
         obj->EverBound = GL_TRUE;
         *ptr = obj;
      }
   }
}


/**
 * Check that all the buffer objects currently bound for transform
 * feedback actually exist.  Raise a GL_INVALID_OPERATION error if
 * any buffers are missing.
 * \return GL_TRUE for success, GL_FALSE if error
 */
GLboolean
_mesa_validate_transform_feedback_buffers(struct gl_context *ctx)
{
   /* XXX to do */
   return GL_TRUE;
}



/**
 * Per-context init for transform feedback.
 */
void
_mesa_init_transform_feedback(struct gl_context *ctx)
{
   /* core mesa expects this, even a dummy one, to be available */
   assert(ctx->Driver.NewTransformFeedback);

   ctx->TransformFeedback.DefaultObject =
      ctx->Driver.NewTransformFeedback(ctx, 0);

   assert(ctx->TransformFeedback.DefaultObject->RefCount == 1);

   reference_transform_feedback_object(&ctx->TransformFeedback.CurrentObject,
                                       ctx->TransformFeedback.DefaultObject);

   assert(ctx->TransformFeedback.DefaultObject->RefCount == 2);

   ctx->TransformFeedback.Objects = _mesa_NewHashTable();

   _mesa_reference_buffer_object(ctx,
                                 &ctx->TransformFeedback.CurrentBuffer,
                                 ctx->Shared->NullBufferObj);
}



/**
 * Callback for _mesa_HashDeleteAll().
 */
static void
delete_cb(GLuint key, void *data, void *userData)
{
   struct gl_context *ctx = (struct gl_context *) userData;
   struct gl_transform_feedback_object *obj =
      (struct gl_transform_feedback_object *) data;

   ctx->Driver.DeleteTransformFeedback(ctx, obj);
}


/**
 * Per-context free/clean-up for transform feedback.
 */
void
_mesa_free_transform_feedback(struct gl_context *ctx)
{
   /* core mesa expects this, even a dummy one, to be available */
   assert(ctx->Driver.NewTransformFeedback);

   _mesa_reference_buffer_object(ctx,
                                 &ctx->TransformFeedback.CurrentBuffer,
                                 NULL);

   /* Delete all feedback objects */
   _mesa_HashDeleteAll(ctx->TransformFeedback.Objects, delete_cb, ctx);
   _mesa_DeleteHashTable(ctx->TransformFeedback.Objects);

   /* Delete the default feedback object */
   assert(ctx->Driver.DeleteTransformFeedback);
   ctx->Driver.DeleteTransformFeedback(ctx,
                                       ctx->TransformFeedback.DefaultObject);

   ctx->TransformFeedback.CurrentObject = NULL;
}


/** Initialize the fields of a gl_transform_feedback_object. */
void
_mesa_init_transform_feedback_object(struct gl_transform_feedback_object *obj,
                                     GLuint name)
{
   if (!obj)
      return;

   obj->Name = name;
   obj->RefCount = 1;
   obj->EverBound = GL_FALSE;
}


/** Default fallback for ctx->Driver.NewTransformFeedback() */
static struct gl_transform_feedback_object *
new_transform_feedback(struct gl_context *ctx, GLuint name)
{
   struct gl_transform_feedback_object *obj;
   obj = CALLOC_STRUCT(gl_transform_feedback_object);
   _mesa_init_transform_feedback_object(obj, name);
   return obj;
}

/** Default fallback for ctx->Driver.DeleteTransformFeedback() */
static void
delete_transform_feedback(struct gl_context *ctx,
                          struct gl_transform_feedback_object *obj)
{
   GLuint i;

   for (i = 0; i < ARRAY_SIZE(obj->Buffers); i++) {
      _mesa_reference_buffer_object(ctx, &obj->Buffers[i], NULL);
   }

   free(obj->Label);
   free(obj);
}


/** Default fallback for ctx->Driver.BeginTransformFeedback() */
static void
begin_transform_feedback(struct gl_context *ctx, GLenum mode,
                         struct gl_transform_feedback_object *obj)
{
   /* nop */
}

/** Default fallback for ctx->Driver.EndTransformFeedback() */
static void
end_transform_feedback(struct gl_context *ctx,
                       struct gl_transform_feedback_object *obj)
{
   /* nop */
}

/** Default fallback for ctx->Driver.PauseTransformFeedback() */
static void
pause_transform_feedback(struct gl_context *ctx,
                         struct gl_transform_feedback_object *obj)
{
   /* nop */
}

/** Default fallback for ctx->Driver.ResumeTransformFeedback() */
static void
resume_transform_feedback(struct gl_context *ctx,
                          struct gl_transform_feedback_object *obj)
{
   /* nop */
}


/**
 * Plug in default device driver functions for transform feedback.
 * Most drivers will override some/all of these.
 */
void
_mesa_init_transform_feedback_functions(struct dd_function_table *driver)
{
   driver->NewTransformFeedback = new_transform_feedback;
   driver->DeleteTransformFeedback = delete_transform_feedback;
   driver->BeginTransformFeedback = begin_transform_feedback;
   driver->EndTransformFeedback = end_transform_feedback;
   driver->PauseTransformFeedback = pause_transform_feedback;
   driver->ResumeTransformFeedback = resume_transform_feedback;
}


/**
 * Fill in the correct Size value for each buffer in \c obj.
 *
 * From the GL 4.3 spec, section 6.1.1 ("Binding Buffer Objects to Indexed
 * Targets"):
 *
 *   BindBufferBase binds the entire buffer, even when the size of the buffer
 *   is changed after the binding is established. It is equivalent to calling
 *   BindBufferRange with offset zero, while size is determined by the size of
 *   the bound buffer at the time the binding is used.
 *
 *   Regardless of the size specified with BindBufferRange, or indirectly with
 *   BindBufferBase, the GL will never read or write beyond the end of a bound
 *   buffer. In some cases this constraint may result in visibly different
 *   behavior when a buffer overflow would otherwise result, such as described
 *   for transform feedback operations in section 13.2.2.
 */
static void
compute_transform_feedback_buffer_sizes(
      struct gl_transform_feedback_object *obj)
{
   unsigned i = 0;
   for (i = 0; i < MAX_FEEDBACK_BUFFERS; ++i) {
      GLintptr offset = obj->Offset[i];
      GLsizeiptr buffer_size
         = obj->Buffers[i] == NULL ? 0 : obj->Buffers[i]->Size;
      GLsizeiptr available_space
         = buffer_size <= offset ? 0 : buffer_size - offset;
      GLsizeiptr computed_size;
      if (obj->RequestedSize[i] == 0) {
         /* No size was specified at the time the buffer was bound, so allow
          * writing to all available space in the buffer.
          */
         computed_size = available_space;
      } else {
         /* A size was specified at the time the buffer was bound, however
          * it's possible that the buffer has shrunk since then.  So only
          * allow writing to the minimum of the specified size and the space
          * available.
          */
         computed_size = MIN2(available_space, obj->RequestedSize[i]);
      }

      /* Legal sizes must be multiples of four, so round down if necessary. */
      obj->Size[i] = computed_size & ~0x3;
   }
}


/**
 * Compute the maximum number of vertices that can be written to the currently
 * enabled transform feedback buffers without overflowing any of them.
 */
unsigned
_mesa_compute_max_transform_feedback_vertices(struct gl_context *ctx,
      const struct gl_transform_feedback_object *obj,
      const struct gl_transform_feedback_info *info)
{
   unsigned max_index = 0xffffffff;
   unsigned i;

   for (i = 0; i < ctx->Const.MaxTransformFeedbackBuffers; i++) {
      if ((info->ActiveBuffers >> i) & 1) {
         unsigned stride = info->Buffers[i].Stride;
         unsigned max_for_this_buffer;

         /* Skip any inactive buffers, which have a stride of 0. */
         if (stride == 0)
	    continue;

         max_for_this_buffer = obj->Size[i] / (4 * stride);
         max_index = MIN2(max_index, max_for_this_buffer);
      }
   }

   return max_index;
}


/**
 ** Begin API functions
 **/


/**
 * Figure out which stage of the pipeline is the source of transform feedback
 * data given the current context state, and return its gl_shader_program.
 *
 * If no active program can generate transform feedback data (i.e. no vertex
 * shader is active), returns NULL.
 */
static struct gl_shader_program *
get_xfb_source(struct gl_context *ctx)
{
   int i;
   for (i = MESA_SHADER_GEOMETRY; i >= MESA_SHADER_VERTEX; i--) {
      if (ctx->_Shader->CurrentProgram[i] != NULL)
         return ctx->_Shader->CurrentProgram[i];
   }
   return NULL;
}


void GLAPIENTRY
_mesa_BeginTransformFeedback(GLenum mode)
{
   struct gl_transform_feedback_object *obj;
   struct gl_transform_feedback_info *info = NULL;
   struct gl_shader_program *source;
   GLuint i;
   unsigned vertices_per_prim;
   GET_CURRENT_CONTEXT(ctx);

   obj = ctx->TransformFeedback.CurrentObject;

   /* Figure out what pipeline stage is the source of data for transform
    * feedback.
    */
   source = get_xfb_source(ctx);
   if (source == NULL) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glBeginTransformFeedback(no program active)");
      return;
   }

   info = &source->LinkedTransformFeedback;

   if (info->NumOutputs == 0) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glBeginTransformFeedback(no varyings to record)");
      return;
   }

   switch (mode) {
   case GL_POINTS:
      vertices_per_prim = 1;
      break;
   case GL_LINES:
      vertices_per_prim = 2;
      break;
   case GL_TRIANGLES:
      vertices_per_prim = 3;
      break;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM, "glBeginTransformFeedback(mode)");
      return;
   }

   if (obj->Active) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glBeginTransformFeedback(already active)");
      return;
   }

   for (i = 0; i < ctx->Const.MaxTransformFeedbackBuffers; i++) {
      if ((info->ActiveBuffers >> i) & 1) {
         if (obj->BufferNames[i] == 0) {
            _mesa_error(ctx, GL_INVALID_OPERATION,
                        "glBeginTransformFeedback(binding point %d does not "
                        "have a buffer object bound)", i);
            return;
         }
      }
   }

   FLUSH_VERTICES(ctx, 0);
   ctx->NewDriverState |= ctx->DriverFlags.NewTransformFeedback;

   obj->Active = GL_TRUE;
   ctx->TransformFeedback.Mode = mode;

   compute_transform_feedback_buffer_sizes(obj);

   if (_mesa_is_gles3(ctx)) {
      /* In GLES3, we are required to track the usage of the transform
       * feedback buffer and report INVALID_OPERATION if a draw call tries to
       * exceed it.  So compute the maximum number of vertices that we can
       * write without overflowing any of the buffers currently being used for
       * feedback.
       */
      unsigned max_vertices
         = _mesa_compute_max_transform_feedback_vertices(ctx, obj, info);
      obj->GlesRemainingPrims = max_vertices / vertices_per_prim;
   }

   if (obj->shader_program != source) {
      ctx->NewDriverState |= ctx->DriverFlags.NewTransformFeedbackProg;
      obj->shader_program = source;
   }

   assert(ctx->Driver.BeginTransformFeedback);
   ctx->Driver.BeginTransformFeedback(ctx, mode, obj);
}


void GLAPIENTRY
_mesa_EndTransformFeedback(void)
{
   struct gl_transform_feedback_object *obj;
   GET_CURRENT_CONTEXT(ctx);

   obj = ctx->TransformFeedback.CurrentObject;

   if (!obj->Active) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glEndTransformFeedback(not active)");
      return;
   }

   FLUSH_VERTICES(ctx, 0);
   ctx->NewDriverState |= ctx->DriverFlags.NewTransformFeedback;

   assert(ctx->Driver.EndTransformFeedback);
   ctx->Driver.EndTransformFeedback(ctx, obj);

   ctx->TransformFeedback.CurrentObject->Active = GL_FALSE;
   ctx->TransformFeedback.CurrentObject->Paused = GL_FALSE;
   ctx->TransformFeedback.CurrentObject->EndedAnytime = GL_TRUE;
}


/**
 * Helper used by BindBufferRange() and BindBufferBase().
 */
static void
bind_buffer_range(struct gl_context *ctx,
                  struct gl_transform_feedback_object *obj,
                  GLuint index,
                  struct gl_buffer_object *bufObj,
                  GLintptr offset, GLsizeiptr size,
                  bool dsa)
{
   /* Note: no need to FLUSH_VERTICES or flag NewTransformFeedback, because
    * transform feedback buffers can't be changed while transform feedback is
    * active.
    */

   if (!dsa) {
      /* The general binding point */
      _mesa_reference_buffer_object(ctx,
                                    &ctx->TransformFeedback.CurrentBuffer,
                                    bufObj);
   }

   /* The per-attribute binding point */
   _mesa_set_transform_feedback_binding(ctx, obj, index, bufObj, offset, size);
}


/**
 * Specify a buffer object to receive transform feedback results.  Plus,
 * specify the starting offset to place the results, and max size.
 * Called from the glBindBufferRange() and glTransformFeedbackBufferRange
 * functions.
 */
void
_mesa_bind_buffer_range_transform_feedback(struct gl_context *ctx,
                                           struct gl_transform_feedback_object *obj,
                                           GLuint index,
                                           struct gl_buffer_object *bufObj,
                                           GLintptr offset,
                                           GLsizeiptr size,
                                           bool dsa)
{
   const char *gl_methd_name;
   if (dsa)
      gl_methd_name = "glTransformFeedbackBufferRange";
   else
      gl_methd_name = "glBindBufferRange";


   if (obj->Active) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(transform feedback active)",
                  gl_methd_name);
      return;
   }

   if (index >= ctx->Const.MaxTransformFeedbackBuffers) {
      /* OpenGL 4.5 core profile, 6.1, pdf page 82: "An INVALID_VALUE error is
       * generated if index is greater than or equal to the number of binding
       * points for transform feedback, as described in section 6.7.1."
       */
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(index=%d out of bounds)",
                  gl_methd_name, index);
      return;
   }

   if (size & 0x3) {
      /* OpenGL 4.5 core profile, 6.7, pdf page 103: multiple of 4 */
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(size=%d must be a multiple of "
                  "four)", gl_methd_name, (int) size);
      return;
   }  

   if (offset & 0x3) {
      /* OpenGL 4.5 core profile, 6.7, pdf page 103: multiple of 4 */
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(offset=%d must be a multiple "
                  "of four)", gl_methd_name, (int) offset);
      return;
   }

   if (offset < 0) {
      /* OpenGL 4.5 core profile, 6.1, pdf page 82: "An INVALID_VALUE error is
       * generated by BindBufferRange if offset is negative."
       *
       * OpenGL 4.5 core profile, 13.2, pdf page 445: "An INVALID_VALUE error
       * is generated by TransformFeedbackBufferRange if offset is negative."
       */
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(offset=%d must be >= 0)",
                  gl_methd_name,
                  (int) offset);
      return;
   }

   if (size <= 0 && (dsa || bufObj != ctx->Shared->NullBufferObj)) {
      /* OpenGL 4.5 core profile, 6.1, pdf page 82: "An INVALID_VALUE error is
       * generated by BindBufferRange if buffer is non-zero and size is less
       * than or equal to zero."
       *
       * OpenGL 4.5 core profile, 13.2, pdf page 445: "An INVALID_VALUE error
       * is generated by TransformFeedbackBufferRange if size is less than or
       * equal to zero."
       */
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(size=%d must be > 0)",
                  gl_methd_name, (int) size);
      return;
   }

   bind_buffer_range(ctx, obj, index, bufObj, offset, size, dsa);
}


/**
 * Specify a buffer object to receive transform feedback results.
 * As above, but start at offset = 0.
 * Called from the glBindBufferBase() and glTransformFeedbackBufferBase()
 * functions.
 */
void
_mesa_bind_buffer_base_transform_feedback(struct gl_context *ctx,
                                          struct gl_transform_feedback_object *obj,
                                          GLuint index,
                                          struct gl_buffer_object *bufObj,
                                          bool dsa)
{
   if (obj->Active) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(transform feedback active)",
                  dsa ? "glTransformFeedbackBufferBase" : "glBindBufferBase");
      return;
   }

   if (index >= ctx->Const.MaxTransformFeedbackBuffers) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(index=%d out of bounds)",
                  dsa ? "glTransformFeedbackBufferBase" : "glBindBufferBase",
                  index);
      return;
   }

   bind_buffer_range(ctx, obj, index, bufObj, 0, 0, dsa);
}

/**
 * Wrapper around lookup_transform_feedback_object that throws
 * GL_INVALID_OPERATION if id is not in the hash table. After calling
 * _mesa_error, it returns NULL.
 */
static struct gl_transform_feedback_object *
lookup_transform_feedback_object_err(struct gl_context *ctx,
                                     GLuint xfb, const char* func)
{
   struct gl_transform_feedback_object *obj;

   obj = _mesa_lookup_transform_feedback_object(ctx, xfb);
   if (!obj) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(xfb=%u: non-generated object name)", func, xfb);
   }

   return obj;
}

/**
 * Wrapper around _mesa_lookup_bufferobj that throws GL_INVALID_VALUE if id
 * is not in the hash table. Specialised version for the
 * transform-feedback-related functions. After calling _mesa_error, it
 * returns NULL.
 */
static struct gl_buffer_object *
lookup_transform_feedback_bufferobj_err(struct gl_context *ctx,
                                        GLuint buffer, const char* func)
{
   struct gl_buffer_object *bufObj;

   /* OpenGL 4.5 core profile, 13.2, pdf page 444: buffer must be zero or the
    * name of an existing buffer object.
    */
   if (buffer == 0) {
      bufObj = ctx->Shared->NullBufferObj;
   } else {
      bufObj = _mesa_lookup_bufferobj(ctx, buffer);
      if (!bufObj) {
         _mesa_error(ctx, GL_INVALID_VALUE, "%s(invalid buffer=%u)", func,
                     buffer);
      }
   }

   return bufObj;
}

void GLAPIENTRY
_mesa_TransformFeedbackBufferBase(GLuint xfb, GLuint index, GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_transform_feedback_object *obj;
   struct gl_buffer_object *bufObj;

   obj = lookup_transform_feedback_object_err(ctx, xfb,
                                              "glTransformFeedbackBufferBase");
   if(!obj) {
      return;
   }

   bufObj = lookup_transform_feedback_bufferobj_err(ctx, buffer,
                                              "glTransformFeedbackBufferBase");
   if(!bufObj) {
      return;
   }

   _mesa_bind_buffer_base_transform_feedback(ctx, obj, index, bufObj, true);
}

void GLAPIENTRY
_mesa_TransformFeedbackBufferRange(GLuint xfb, GLuint index, GLuint buffer,
                                   GLintptr offset, GLsizeiptr size)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_transform_feedback_object *obj;
   struct gl_buffer_object *bufObj;

   obj = lookup_transform_feedback_object_err(ctx, xfb,
                                              "glTransformFeedbackBufferRange");
   if(!obj) {
      return;
   }

   bufObj = lookup_transform_feedback_bufferobj_err(ctx, buffer,
                                              "glTransformFeedbackBufferRange");
   if(!bufObj) {
      return;
   }

   _mesa_bind_buffer_range_transform_feedback(ctx, obj, index, bufObj, offset,
                                              size, true);
}

/**
 * Specify a buffer object to receive transform feedback results, plus the
 * offset in the buffer to start placing results.
 * This function is part of GL_EXT_transform_feedback, but not GL3.
 */
void GLAPIENTRY
_mesa_BindBufferOffsetEXT(GLenum target, GLuint index, GLuint buffer,
                          GLintptr offset)
{
   struct gl_transform_feedback_object *obj;
   struct gl_buffer_object *bufObj;
   GET_CURRENT_CONTEXT(ctx);

   if (target != GL_TRANSFORM_FEEDBACK_BUFFER) {
      _mesa_error(ctx, GL_INVALID_ENUM, "glBindBufferOffsetEXT(target)");
      return;
   }

   obj = ctx->TransformFeedback.CurrentObject;

   if (obj->Active) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glBindBufferOffsetEXT(transform feedback active)");
      return;
   }

   if (index >= ctx->Const.MaxTransformFeedbackBuffers) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glBindBufferOffsetEXT(index=%d)", index);
      return;
   }

   if (offset & 0x3) {
      /* must be multiple of four */
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glBindBufferOffsetEXT(offset=%d)", (int) offset);
      return;
   }

   if (buffer == 0) {
      bufObj = ctx->Shared->NullBufferObj;
   } else {
      bufObj = _mesa_lookup_bufferobj(ctx, buffer);
   }

   if (!bufObj) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glBindBufferOffsetEXT(invalid buffer=%u)", buffer);
      return;
   }

   bind_buffer_range(ctx, obj, index, bufObj, offset, 0, false);
}


/**
 * This function specifies the transform feedback outputs to be written
 * to the feedback buffer(s), and in what order.
 */
void GLAPIENTRY
_mesa_TransformFeedbackVaryings(GLuint program, GLsizei count,
                                const GLchar * const *varyings,
                                GLenum bufferMode)
{
   struct gl_shader_program *shProg;
   GLint i;
   GET_CURRENT_CONTEXT(ctx);

   /* From the ARB_transform_feedback2 specification:
    * "The error INVALID_OPERATION is generated by TransformFeedbackVaryings
    *  if the current transform feedback object is active, even if paused."
    */
   if (ctx->TransformFeedback.CurrentObject->Active) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
               "glTransformFeedbackVaryings(current object is active)");
      return;
   }

   switch (bufferMode) {
   case GL_INTERLEAVED_ATTRIBS:
      break;
   case GL_SEPARATE_ATTRIBS:
      break;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glTransformFeedbackVaryings(bufferMode)");
      return;
   }

   if (count < 0 ||
       (bufferMode == GL_SEPARATE_ATTRIBS &&
        (GLuint) count > ctx->Const.MaxTransformFeedbackBuffers)) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glTransformFeedbackVaryings(count=%d)", count);
      return;
   }

   shProg = _mesa_lookup_shader_program(ctx, program);
   if (!shProg) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glTransformFeedbackVaryings(program=%u)", program);
      return;
   }

   if (ctx->Extensions.ARB_transform_feedback3) {
      if (bufferMode == GL_INTERLEAVED_ATTRIBS) {
         unsigned buffers = 1;

         for (i = 0; i < count; i++) {
            if (strcmp(varyings[i], "gl_NextBuffer") == 0)
               buffers++;
         }

         if (buffers > ctx->Const.MaxTransformFeedbackBuffers) {
            _mesa_error(ctx, GL_INVALID_OPERATION,
                        "glTransformFeedbackVaryings(too many gl_NextBuffer "
                        "occurrences)");
            return;
         }
      } else {
         for (i = 0; i < count; i++) {
            if (strcmp(varyings[i], "gl_NextBuffer") == 0 ||
                strcmp(varyings[i], "gl_SkipComponents1") == 0 ||
                strcmp(varyings[i], "gl_SkipComponents2") == 0 ||
                strcmp(varyings[i], "gl_SkipComponents3") == 0 ||
                strcmp(varyings[i], "gl_SkipComponents4") == 0) {
               _mesa_error(ctx, GL_INVALID_OPERATION,
                           "glTransformFeedbackVaryings(SEPARATE_ATTRIBS,"
                           "varying=%s)",
                           varyings[i]);
               return;
            }
         }
      }
   }

   /* free existing varyings, if any */
   for (i = 0; i < (GLint) shProg->TransformFeedback.NumVarying; i++) {
      free(shProg->TransformFeedback.VaryingNames[i]);
   }
   free(shProg->TransformFeedback.VaryingNames);

   /* allocate new memory for varying names */
   shProg->TransformFeedback.VaryingNames =
      malloc(count * sizeof(GLchar *));

   if (!shProg->TransformFeedback.VaryingNames) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "glTransformFeedbackVaryings()");
      return;
   }

   /* Save the new names and the count */
   for (i = 0; i < count; i++) {
      shProg->TransformFeedback.VaryingNames[i] = strdup(varyings[i]);
   }
   shProg->TransformFeedback.NumVarying = count;

   shProg->TransformFeedback.BufferMode = bufferMode;

   /* No need to invoke FLUSH_VERTICES or flag NewTransformFeedback since
    * the varyings won't be used until shader link time.
    */
}


/**
 * Get info about the transform feedback outputs which are to be written
 * to the feedback buffer(s).
 */
void GLAPIENTRY
_mesa_GetTransformFeedbackVarying(GLuint program, GLuint index,
                                  GLsizei bufSize, GLsizei *length,
                                  GLsizei *size, GLenum *type, GLchar *name)
{
   const struct gl_shader_program *shProg;
   struct gl_program_resource *res;
   GET_CURRENT_CONTEXT(ctx);

   shProg = _mesa_lookup_shader_program(ctx, program);
   if (!shProg) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glGetTransformFeedbackVarying(program=%u)", program);
      return;
   }

   res = _mesa_program_resource_find_index((struct gl_shader_program *) shProg,
                                           GL_TRANSFORM_FEEDBACK_VARYING,
                                           index);
   if (!res) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glGetTransformFeedbackVarying(index=%u)", index);
      return;
   }

   /* return the varying's name and length */
   _mesa_copy_string(name, bufSize, length, _mesa_program_resource_name(res));

   /* return the datatype and value's size (in datatype units) */
   if (type)
      _mesa_program_resource_prop((struct gl_shader_program *) shProg,
                                  res, index, GL_TYPE, (GLint*) type,
                                  "glGetTransformFeedbackVarying");
   if (size)
      _mesa_program_resource_prop((struct gl_shader_program *) shProg,
                                  res, index, GL_ARRAY_SIZE, (GLint*) size,
                                  "glGetTransformFeedbackVarying");
}



struct gl_transform_feedback_object *
_mesa_lookup_transform_feedback_object(struct gl_context *ctx, GLuint name)
{
   /* OpenGL 4.5 core profile, 13.2 pdf page 444: "xfb must be zero, indicating
    * the default transform feedback object, or the name of an existing
    * transform feedback object."
    */
   if (name == 0) {
      return ctx->TransformFeedback.DefaultObject;
   }
   else
      return (struct gl_transform_feedback_object *)
         _mesa_HashLookup(ctx->TransformFeedback.Objects, name);
}

static void
create_transform_feedbacks(struct gl_context *ctx, GLsizei n, GLuint *ids,
                           bool dsa)
{
   GLuint first;
   const char* func;

   if (dsa)
      func = "glCreateTransformFeedbacks";
   else
      func = "glGenTransformFeedbacks";

   if (n < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(n < 0)", func);
      return;
   }

   if (!ids)
      return;

   /* we don't need contiguous IDs, but this might be faster */
   first = _mesa_HashFindFreeKeyBlock(ctx->TransformFeedback.Objects, n);
   if (first) {
      GLsizei i;
      for (i = 0; i < n; i++) {
         struct gl_transform_feedback_object *obj
            = ctx->Driver.NewTransformFeedback(ctx, first + i);
         if (!obj) {
            _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s", func);
            return;
         }
         ids[i] = first + i;
         _mesa_HashInsert(ctx->TransformFeedback.Objects, first + i, obj);
         if (dsa) {
            /* this is normally done at bind time in the non-dsa case */
            obj->EverBound = GL_TRUE;
         }
      }
   }
   else {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s", func);
   }
}

/**
 * Create new transform feedback objects.   Transform feedback objects
 * encapsulate the state related to transform feedback to allow quickly
 * switching state (and drawing the results, below).
 * Part of GL_ARB_transform_feedback2.
 */
void GLAPIENTRY
_mesa_GenTransformFeedbacks(GLsizei n, GLuint *names)
{
   GET_CURRENT_CONTEXT(ctx);

   /* GenTransformFeedbacks should just reserve the object names that a
    * subsequent call to BindTransformFeedback should actively create. For
    * the sake of simplicity, we reserve the names and create the objects
    * straight away.
    */

   create_transform_feedbacks(ctx, n, names, false);
}

/**
 * Create new transform feedback objects.   Transform feedback objects
 * encapsulate the state related to transform feedback to allow quickly
 * switching state (and drawing the results, below).
 * Part of GL_ARB_direct_state_access.
 */
void GLAPIENTRY
_mesa_CreateTransformFeedbacks(GLsizei n, GLuint *names)
{
   GET_CURRENT_CONTEXT(ctx);

   create_transform_feedbacks(ctx, n, names, true);
}


/**
 * Is the given ID a transform feedback object?
 * Part of GL_ARB_transform_feedback2.
 */
GLboolean GLAPIENTRY
_mesa_IsTransformFeedback(GLuint name)
{
   struct gl_transform_feedback_object *obj;
   GET_CURRENT_CONTEXT(ctx);

   ASSERT_OUTSIDE_BEGIN_END_WITH_RETVAL(ctx, GL_FALSE);

   if (name == 0)
      return GL_FALSE;

   obj = _mesa_lookup_transform_feedback_object(ctx, name);
   if (obj == NULL)
      return GL_FALSE;

   return obj->EverBound;
}


/**
 * Bind the given transform feedback object.
 * Part of GL_ARB_transform_feedback2.
 */
void GLAPIENTRY
_mesa_BindTransformFeedback(GLenum target, GLuint name)
{
   struct gl_transform_feedback_object *obj;
   GET_CURRENT_CONTEXT(ctx);

   if (target != GL_TRANSFORM_FEEDBACK) {
      _mesa_error(ctx, GL_INVALID_ENUM, "glBindTransformFeedback(target)");
      return;
   }

   if (_mesa_is_xfb_active_and_unpaused(ctx)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
              "glBindTransformFeedback(transform is active, or not paused)");
      return;
   }

   obj = _mesa_lookup_transform_feedback_object(ctx, name);
   if (!obj) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glBindTransformFeedback(name=%u)", name);
      return;
   }

   reference_transform_feedback_object(&ctx->TransformFeedback.CurrentObject,
                                       obj);
}


/**
 * Delete the given transform feedback objects.
 * Part of GL_ARB_transform_feedback2.
 */
void GLAPIENTRY
_mesa_DeleteTransformFeedbacks(GLsizei n, const GLuint *names)
{
   GLint i;
   GET_CURRENT_CONTEXT(ctx);

   if (n < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glDeleteTransformFeedbacks(n < 0)");
      return;
   }

   if (!names)
      return;

   for (i = 0; i < n; i++) {
      if (names[i] > 0) {
         struct gl_transform_feedback_object *obj
            = _mesa_lookup_transform_feedback_object(ctx, names[i]);
         if (obj) {
            if (obj->Active) {
               _mesa_error(ctx, GL_INVALID_OPERATION,
                           "glDeleteTransformFeedbacks(object %u is active)",
                           names[i]);
               return;
            }
            _mesa_HashRemove(ctx->TransformFeedback.Objects, names[i]);
            /* unref, but object may not be deleted until later */
            if (obj == ctx->TransformFeedback.CurrentObject) {
               reference_transform_feedback_object(
                     &ctx->TransformFeedback.CurrentObject,
                     ctx->TransformFeedback.DefaultObject);
            }
            reference_transform_feedback_object(&obj, NULL);
         }
      }
   }
}


/**
 * Pause transform feedback.
 * Part of GL_ARB_transform_feedback2.
 */
void GLAPIENTRY
_mesa_PauseTransformFeedback(void)
{
   struct gl_transform_feedback_object *obj;
   GET_CURRENT_CONTEXT(ctx);

   obj = ctx->TransformFeedback.CurrentObject;

   if (!_mesa_is_xfb_active_and_unpaused(ctx)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
           "glPauseTransformFeedback(feedback not active or already paused)");
      return;
   }

   FLUSH_VERTICES(ctx, 0);
   ctx->NewDriverState |= ctx->DriverFlags.NewTransformFeedback;

   assert(ctx->Driver.PauseTransformFeedback);
   ctx->Driver.PauseTransformFeedback(ctx, obj);

   obj->Paused = GL_TRUE;
}


/**
 * Resume transform feedback.
 * Part of GL_ARB_transform_feedback2.
 */
void GLAPIENTRY
_mesa_ResumeTransformFeedback(void)
{
   struct gl_transform_feedback_object *obj;
   GET_CURRENT_CONTEXT(ctx);

   obj = ctx->TransformFeedback.CurrentObject;

   if (!obj->Active || !obj->Paused) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
               "glResumeTransformFeedback(feedback not active or not paused)");
      return;
   }

   /* From the ARB_transform_feedback2 specification:
    * "The error INVALID_OPERATION is generated by ResumeTransformFeedback if
    *  the program object being used by the current transform feedback object
    *  is not active."
    */
   if (obj->shader_program != get_xfb_source(ctx)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glResumeTransformFeedback(wrong program bound)");
      return;
   }

   FLUSH_VERTICES(ctx, 0);
   ctx->NewDriverState |= ctx->DriverFlags.NewTransformFeedback;

   obj->Paused = GL_FALSE;

   assert(ctx->Driver.ResumeTransformFeedback);
   ctx->Driver.ResumeTransformFeedback(ctx, obj);
}

extern void GLAPIENTRY
_mesa_GetTransformFeedbackiv(GLuint xfb, GLenum pname, GLint *param)
{
    struct gl_transform_feedback_object *obj;
    GET_CURRENT_CONTEXT(ctx);

    obj = lookup_transform_feedback_object_err(ctx, xfb,
                                               "glGetTransformFeedbackiv");
    if(!obj) {
       return;
    }

    switch(pname) {
    case GL_TRANSFORM_FEEDBACK_PAUSED:
       *param = obj->Paused;
       break;
    case GL_TRANSFORM_FEEDBACK_ACTIVE:
       *param = obj->Active;
       break;
    default:
       _mesa_error(ctx, GL_INVALID_ENUM,
                   "glGetTransformFeedbackiv(pname=%i)", pname);
    }
}

extern void GLAPIENTRY
_mesa_GetTransformFeedbacki_v(GLuint xfb, GLenum pname, GLuint index,
                              GLint *param)
{
   struct gl_transform_feedback_object *obj;
   GET_CURRENT_CONTEXT(ctx);

   obj = lookup_transform_feedback_object_err(ctx, xfb,
                                              "glGetTransformFeedbacki_v");
   if(!obj) {
      return;
   }

   if (index >= ctx->Const.MaxTransformFeedbackBuffers) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glGetTransformFeedbacki_v(index=%i)", index);
      return;
   }

   switch(pname) {
   case GL_TRANSFORM_FEEDBACK_BUFFER_BINDING:
      *param = obj->BufferNames[index];
      break;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glGetTransformFeedbacki_v(pname=%i)", pname);
   }
}

extern void GLAPIENTRY
_mesa_GetTransformFeedbacki64_v(GLuint xfb, GLenum pname, GLuint index,
                                GLint64 *param)
{
   struct gl_transform_feedback_object *obj;
   GET_CURRENT_CONTEXT(ctx);

   obj = lookup_transform_feedback_object_err(ctx, xfb,
                                              "glGetTransformFeedbacki64_v");
   if(!obj) {
      return;
   }

   if (index >= ctx->Const.MaxTransformFeedbackBuffers) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glGetTransformFeedbacki64_v(index=%i)", index);
      return;
   }

   compute_transform_feedback_buffer_sizes(obj);
   switch(pname) {
   case GL_TRANSFORM_FEEDBACK_BUFFER_START:
      *param = obj->Offset[index];
      break;
   case GL_TRANSFORM_FEEDBACK_BUFFER_SIZE:
      *param = obj->Size[index];
      break;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glGetTransformFeedbacki64_v(pname=%i)", pname);
   }
}
