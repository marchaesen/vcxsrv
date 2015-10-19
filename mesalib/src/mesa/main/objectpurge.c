/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2008  Brian Paul   All Rights Reserved.
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

/**
 * Code related to the GL_APPLE_object_purgeable extension.
 */


#include "glheader.h"
#include "enums.h"
#include "hash.h"
#include "imports.h"
#include "context.h"
#include "bufferobj.h"
#include "fbobject.h"
#include "mtypes.h"
#include "objectpurge.h"
#include "texobj.h"
#include "teximage.h"


static GLenum
buffer_object_purgeable(struct gl_context *ctx, GLuint name, GLenum option)
{
   struct gl_buffer_object *bufObj;
   GLenum retval;

   bufObj = _mesa_lookup_bufferobj(ctx, name);
   if (!bufObj) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glObjectPurgeable(name = 0x%x)", name);
      return 0;
   }
   if (!_mesa_is_bufferobj(bufObj)) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "glObjectPurgeable(buffer 0)" );
      return 0;
   }

   if (bufObj->Purgeable) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glObjectPurgeable(name = 0x%x) is already purgeable", name);
      return GL_VOLATILE_APPLE;
   }

   bufObj->Purgeable = GL_TRUE;

   retval = GL_VOLATILE_APPLE;
   if (ctx->Driver.BufferObjectPurgeable)
      retval = ctx->Driver.BufferObjectPurgeable(ctx, bufObj, option);

   return retval;
}


static GLenum
renderbuffer_purgeable(struct gl_context *ctx, GLuint name, GLenum option)
{
   struct gl_renderbuffer *bufObj;
   GLenum retval;

   bufObj = _mesa_lookup_renderbuffer(ctx, name);
   if (!bufObj) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glObjectUnpurgeable(name = 0x%x)", name);
      return 0;
   }

   if (bufObj->Purgeable) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glObjectPurgeable(name = 0x%x) is already purgeable", name);
      return GL_VOLATILE_APPLE;
   }

   bufObj->Purgeable = GL_TRUE;

   retval = GL_VOLATILE_APPLE;
   if (ctx->Driver.RenderObjectPurgeable)
      retval = ctx->Driver.RenderObjectPurgeable(ctx, bufObj, option);

   return retval;
}


static GLenum
texture_object_purgeable(struct gl_context *ctx, GLuint name, GLenum option)
{
   struct gl_texture_object *bufObj;
   GLenum retval;

   bufObj = _mesa_lookup_texture(ctx, name);
   if (!bufObj) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glObjectPurgeable(name = 0x%x)", name);
      return 0;
   }

   if (bufObj->Purgeable) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glObjectPurgeable(name = 0x%x) is already purgeable", name);
      return GL_VOLATILE_APPLE;
   }

   bufObj->Purgeable = GL_TRUE;

   retval = GL_VOLATILE_APPLE;
   if (ctx->Driver.TextureObjectPurgeable)
      retval = ctx->Driver.TextureObjectPurgeable(ctx, bufObj, option);

   return retval;
}


GLenum GLAPIENTRY
_mesa_ObjectPurgeableAPPLE(GLenum objectType, GLuint name, GLenum option)
{
   GLenum retval;

   GET_CURRENT_CONTEXT(ctx);
   ASSERT_OUTSIDE_BEGIN_END_WITH_RETVAL(ctx, 0);

   if (name == 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glObjectPurgeable(name = 0x%x)", name);
      return 0;
   }

   switch (option) {
   case GL_VOLATILE_APPLE:
   case GL_RELEASED_APPLE:
      /* legal */
      break;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glObjectPurgeable(name = 0x%x) invalid option: %d",
                  name, option);
      return 0;
   }

   switch (objectType) {
   case GL_TEXTURE:
      retval = texture_object_purgeable(ctx, name, option);
      break;
   case GL_RENDERBUFFER_EXT:
      retval = renderbuffer_purgeable(ctx, name, option);
      break;
   case GL_BUFFER_OBJECT_APPLE:
      retval = buffer_object_purgeable(ctx, name, option);
      break;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glObjectPurgeable(name = 0x%x) invalid type: %d",
                  name, objectType);
      return 0;
   }

   /* In strict conformance to the spec, we must only return VOLATILE when
    * when passed the VOLATILE option. Madness.
    *
    * XXX First fix the spec, then fix me.
    */
   return option == GL_VOLATILE_APPLE ? GL_VOLATILE_APPLE : retval;
}


static GLenum
buffer_object_unpurgeable(struct gl_context *ctx, GLuint name, GLenum option)
{
   struct gl_buffer_object *bufObj;
   GLenum retval;

   bufObj = _mesa_lookup_bufferobj(ctx, name);
   if (!bufObj) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glObjectUnpurgeable(name = 0x%x)", name);
      return 0;
   }

   if (! bufObj->Purgeable) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glObjectUnpurgeable(name = 0x%x) object is "
                  " already \"unpurged\"", name);
      return 0;
   }

   bufObj->Purgeable = GL_FALSE;

   retval = option;
   if (ctx->Driver.BufferObjectUnpurgeable)
      retval = ctx->Driver.BufferObjectUnpurgeable(ctx, bufObj, option);

   return retval;
}


static GLenum
renderbuffer_unpurgeable(struct gl_context *ctx, GLuint name, GLenum option)
{
   struct gl_renderbuffer *bufObj;
   GLenum retval;

   bufObj = _mesa_lookup_renderbuffer(ctx, name);
   if (!bufObj) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glObjectUnpurgeable(name = 0x%x)", name);
      return 0;
   }

   if (! bufObj->Purgeable) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glObjectUnpurgeable(name = 0x%x) object is "
                  " already \"unpurged\"", name);
      return 0;
   }

   bufObj->Purgeable = GL_FALSE;

   retval = option;
   if (ctx->Driver.RenderObjectUnpurgeable)
      retval = ctx->Driver.RenderObjectUnpurgeable(ctx, bufObj, option);

   return retval;
}


static GLenum
texture_object_unpurgeable(struct gl_context *ctx, GLuint name, GLenum option)
{
   struct gl_texture_object *bufObj;
   GLenum retval;

   bufObj = _mesa_lookup_texture(ctx, name);
   if (!bufObj) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glObjectUnpurgeable(name = 0x%x)", name);
      return 0;
   }

   if (! bufObj->Purgeable) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glObjectUnpurgeable(name = 0x%x) object is"
                  " already \"unpurged\"", name);
      return 0;
   }

   bufObj->Purgeable = GL_FALSE;

   retval = option;
   if (ctx->Driver.TextureObjectUnpurgeable)
      retval = ctx->Driver.TextureObjectUnpurgeable(ctx, bufObj, option);

   return retval;
}


GLenum GLAPIENTRY
_mesa_ObjectUnpurgeableAPPLE(GLenum objectType, GLuint name, GLenum option)
{
   GET_CURRENT_CONTEXT(ctx);
   ASSERT_OUTSIDE_BEGIN_END_WITH_RETVAL(ctx, 0);

   if (name == 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glObjectUnpurgeable(name = 0x%x)", name);
      return 0;
   }

   switch (option) {
   case GL_RETAINED_APPLE:
   case GL_UNDEFINED_APPLE:
      /* legal */
      break;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glObjectUnpurgeable(name = 0x%x) invalid option: %d",
                  name, option);
      return 0;
   }

   switch (objectType) {
   case GL_BUFFER_OBJECT_APPLE:
      return buffer_object_unpurgeable(ctx, name, option);
   case GL_TEXTURE:
      return texture_object_unpurgeable(ctx, name, option);
   case GL_RENDERBUFFER_EXT:
      return renderbuffer_unpurgeable(ctx, name, option);
   default:
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glObjectUnpurgeable(name = 0x%x) invalid type: %d",
                  name, objectType);
      return 0;
   }
}


static void
get_buffer_object_parameteriv(struct gl_context *ctx, GLuint name,
                              GLenum pname, GLint *params)
{
   struct gl_buffer_object *bufObj = _mesa_lookup_bufferobj(ctx, name);
   if (!bufObj) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glGetObjectParameteriv(name = 0x%x) invalid object", name);
      return;
   }

   switch (pname) {
   case GL_PURGEABLE_APPLE:
      *params = bufObj->Purgeable;
      break;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glGetObjectParameteriv(name = 0x%x) invalid enum: %d",
                  name, pname);
      break;
   }
}


static void
get_renderbuffer_parameteriv(struct gl_context *ctx, GLuint name,
                             GLenum pname, GLint *params)
{
   struct gl_renderbuffer *rb = _mesa_lookup_renderbuffer(ctx, name);
   if (!rb) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glObjectUnpurgeable(name = 0x%x)", name);
      return;
   }

   switch (pname) {
   case GL_PURGEABLE_APPLE:
      *params = rb->Purgeable;
      break;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glGetObjectParameteriv(name = 0x%x) invalid enum: %d",
                  name, pname);
      break;
   }
}


static void
get_texture_object_parameteriv(struct gl_context *ctx, GLuint name,
                               GLenum pname, GLint *params)
{
   struct gl_texture_object *texObj = _mesa_lookup_texture(ctx, name);
   if (!texObj) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glObjectUnpurgeable(name = 0x%x)", name);
      return;
   }

   switch (pname) {
   case GL_PURGEABLE_APPLE:
      *params = texObj->Purgeable;
      break;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glGetObjectParameteriv(name = 0x%x) invalid enum: %d",
                  name, pname);
      break;
   }
}


void GLAPIENTRY
_mesa_GetObjectParameterivAPPLE(GLenum objectType, GLuint name, GLenum pname,
                                GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);

   if (name == 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glGetObjectParameteriv(name = 0x%x)", name);
      return;
   }

   switch (objectType) {
   case GL_TEXTURE:
      get_texture_object_parameteriv(ctx, name, pname, params);
      break;
   case GL_BUFFER_OBJECT_APPLE:
      get_buffer_object_parameteriv(ctx, name, pname, params);
      break;
   case GL_RENDERBUFFER_EXT:
      get_renderbuffer_parameteriv(ctx, name, pname, params);
      break;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "glGetObjectParameteriv(name = 0x%x) invalid type: %d",
                  name, objectType);
   }
}
