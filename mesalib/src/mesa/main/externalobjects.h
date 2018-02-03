/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 2017 Red Hat.
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
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors: Dave Airlie <airlied@gmail.com>
 * 	    Andres Rodriguez <andresx7@gmail.com>
 */

/**
 * \file externalobjects.h
 *
 * Declarations of functions related to the API interop extensions.
 */

#ifndef EXTERNALOBJECTS_H
#define EXTERNALOBJECTS_H

#include "glheader.h"
#include "hash.h"

static inline struct gl_memory_object *
_mesa_lookup_memory_object(struct gl_context *ctx, GLuint memory)
{
   if (!memory)
      return NULL;

   return (struct gl_memory_object *)
      _mesa_HashLookup(ctx->Shared->MemoryObjects, memory);
}

static inline struct gl_memory_object *
_mesa_lookup_memory_object_locked(struct gl_context *ctx, GLuint memory)
{
   if (!memory)
      return NULL;

   return (struct gl_memory_object *)
      _mesa_HashLookupLocked(ctx->Shared->MemoryObjects, memory);
}

static inline struct gl_semaphore_object *
_mesa_lookup_semaphore_object(struct gl_context *ctx, GLuint semaphore)
{
   if (!semaphore)
      return NULL;

   return (struct gl_semaphore_object *)
      _mesa_HashLookup(ctx->Shared->SemaphoreObjects, semaphore);
}

static inline struct gl_semaphore_object *
_mesa_lookup_semaphore_object_locked(struct gl_context *ctx, GLuint semaphore)
{
   if (!semaphore)
      return NULL;

   return (struct gl_semaphore_object *)
      _mesa_HashLookupLocked(ctx->Shared->SemaphoreObjects, semaphore);
}

extern void
_mesa_init_memory_object_functions(struct dd_function_table *driver);

extern void
_mesa_initialize_memory_object(struct gl_context *ctx,
                               struct gl_memory_object *obj,
                               GLuint name);
extern void
_mesa_delete_memory_object(struct gl_context *ctx,
                           struct gl_memory_object *semObj);

extern void
_mesa_initialize_semaphore_object(struct gl_context *ctx,
                                  struct gl_semaphore_object *obj,
                                  GLuint name);
extern void
_mesa_delete_semaphore_object(struct gl_context *ctx,
                              struct gl_semaphore_object *semObj);

extern void GLAPIENTRY
_mesa_DeleteMemoryObjectsEXT(GLsizei n, const GLuint *memoryObjects);

extern GLboolean GLAPIENTRY
_mesa_IsMemoryObjectEXT(GLuint memoryObject);

extern void GLAPIENTRY
_mesa_CreateMemoryObjectsEXT(GLsizei n, GLuint *memoryObjects);

extern void GLAPIENTRY
_mesa_MemoryObjectParameterivEXT(GLuint memoryObject,
                                 GLenum pname,
                                 const GLint *params);

extern void GLAPIENTRY
_mesa_GetMemoryObjectParameterivEXT(GLuint memoryObject,
                                    GLenum pname,
                                    GLint *params);

extern void GLAPIENTRY
_mesa_TexStorageMem2DEXT(GLenum target,
                         GLsizei levels,
                         GLenum internalFormat,
                         GLsizei width,
                         GLsizei height,
                         GLuint memory,
                         GLuint64 offset);

extern void GLAPIENTRY
_mesa_TexStorageMem2DMultisampleEXT(GLenum target,
                                    GLsizei samples,
                                    GLenum internalFormat,
                                    GLsizei width,
                                    GLsizei height,
                                    GLboolean fixedSampleLocations,
                                    GLuint memory,
                                    GLuint64 offset);

extern void GLAPIENTRY
_mesa_TexStorageMem3DEXT(GLenum target,
                         GLsizei levels,
                         GLenum internalFormat,
                         GLsizei width,
                         GLsizei height,
                         GLsizei depth,
                         GLuint memory,
                         GLuint64 offset);

extern void GLAPIENTRY
_mesa_TexStorageMem3DMultisampleEXT(GLenum target,
                                    GLsizei samples,
                                    GLenum internalFormat,
                                    GLsizei width,
                                    GLsizei height,
                                    GLsizei depth,
                                    GLboolean fixedSampleLocations,
                                    GLuint memory,
                                    GLuint64 offset);

extern void GLAPIENTRY
_mesa_TextureStorageMem2DEXT(GLuint texture,
                             GLsizei levels,
                             GLenum internalFormat,
                             GLsizei width,
                             GLsizei height,
                             GLuint memory,
                             GLuint64 offset);

extern void GLAPIENTRY
_mesa_TextureStorageMem2DMultisampleEXT(GLuint texture,
                                        GLsizei samples,
                                        GLenum internalFormat,
                                        GLsizei width,
                                        GLsizei height,
                                        GLboolean fixedSampleLocations,
                                        GLuint memory,
                                        GLuint64 offset);

extern void GLAPIENTRY
_mesa_TextureStorageMem3DEXT(GLuint texture,
                             GLsizei levels,
                             GLenum internalFormat,
                             GLsizei width,
                             GLsizei height,
                             GLsizei depth,
                             GLuint memory,
                             GLuint64 offset);

extern void GLAPIENTRY
_mesa_TextureStorageMem3DMultisampleEXT(GLuint texture,
                                        GLsizei samples,
                                        GLenum internalFormat,
                                        GLsizei width,
                                        GLsizei height,
                                        GLsizei depth,
                                        GLboolean fixedSampleLocations,
                                        GLuint memory,
                                        GLuint64 offset);

extern void GLAPIENTRY
_mesa_TexStorageMem1DEXT(GLenum target,
                         GLsizei levels,
                         GLenum internalFormat,
                         GLsizei width,
                         GLuint memory,
                         GLuint64 offset);

extern void GLAPIENTRY
_mesa_TextureStorageMem1DEXT(GLuint texture,
                             GLsizei levels,
                             GLenum internalFormat,
                             GLsizei width,
                             GLuint memory,
                             GLuint64 offset);

extern void GLAPIENTRY
_mesa_GenSemaphoresEXT(GLsizei n, GLuint *semaphores);

extern void GLAPIENTRY
_mesa_DeleteSemaphoresEXT(GLsizei n, const GLuint *semaphores);

extern GLboolean GLAPIENTRY
_mesa_IsSemaphoreEXT(GLuint semaphore);

extern void GLAPIENTRY
_mesa_SemaphoreParameterui64vEXT(GLuint semaphore,
                                 GLenum pname,
                                 const GLuint64 *params);

extern void GLAPIENTRY
_mesa_GetSemaphoreParameterui64vEXT(GLuint semaphore,
                                    GLenum pname,
                                    GLuint64 *params);

extern void GLAPIENTRY
_mesa_WaitSemaphoreEXT(GLuint semaphore,
                       GLuint numBufferBarriers,
                       const GLuint *buffers,
                       GLuint numTextureBarriers,
                       const GLuint *textures,
                       const GLenum *srcLayouts);

extern void GLAPIENTRY
_mesa_SignalSemaphoreEXT(GLuint semaphore,
                         GLuint numBufferBarriers,
                         const GLuint *buffers,
                         GLuint numTextureBarriers,
                         const GLuint *textures,
                         const GLenum *dstLayouts);

extern void GLAPIENTRY
_mesa_ImportMemoryFdEXT(GLuint memory,
                        GLuint64 size,
                        GLenum handleType,
                        GLint fd);

extern void GLAPIENTRY
_mesa_ImportSemaphoreFdEXT(GLuint semaphore,
                           GLenum handleType,
                           GLint fd);

#endif
