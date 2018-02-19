/*
 * Copyright © 2016 Red Hat.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include "macros.h"
#include "mtypes.h"
#include "bufferobj.h"
#include "context.h"
#include "externalobjects.h"
#include "teximage.h"
#include "texobj.h"
#include "glformats.h"
#include "texstorage.h"

/**
 * Allocate and initialize a new memory object.  But don't put it into the
 * memory object hash table.
 *
 * Called via ctx->Driver.NewMemoryObject, unless overridden by a device
 * driver.
 *
 * \return pointer to new memory object.
 */
static struct gl_memory_object *
_mesa_new_memory_object(struct gl_context *ctx, GLuint name)
{
   struct gl_memory_object *obj = MALLOC_STRUCT(gl_memory_object);
   if (!obj)
      return NULL;

   _mesa_initialize_memory_object(ctx, obj, name);
   return obj;
}

/**
 * Delete a memory object.  Called via ctx->Driver.DeleteMemory().
 * Not removed from hash table here.
 */
void
_mesa_delete_memory_object(struct gl_context *ctx,
                           struct gl_memory_object *memObj)
{
   free(memObj);
}

void
_mesa_init_memory_object_functions(struct dd_function_table *driver)
{
   driver->NewMemoryObject = _mesa_new_memory_object;
   driver->DeleteMemoryObject = _mesa_delete_memory_object;
}

/**
 * Initialize a buffer object to default values.
 */
void
_mesa_initialize_memory_object(struct gl_context *ctx,
                               struct gl_memory_object *obj,
                               GLuint name)
{
   memset(obj, 0, sizeof(struct gl_memory_object));
   obj->Name = name;
   obj->Dedicated = GL_FALSE;
}

void GLAPIENTRY
_mesa_DeleteMemoryObjectsEXT(GLsizei n, const GLuint *memoryObjects)
{
   GET_CURRENT_CONTEXT(ctx);

   if (MESA_VERBOSE & (VERBOSE_API)) {
      _mesa_debug(ctx, "glDeleteMemoryObjectsEXT(%d, %p)\n", n,
                  memoryObjects);
   }

   if (!ctx->Extensions.EXT_memory_object) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glDeleteMemoryObjectsEXT(unsupported)");
      return;
   }

   if (n < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glDeleteMemoryObjectsEXT(n < 0)");
      return;
   }

   if (!memoryObjects)
      return;

   _mesa_HashLockMutex(ctx->Shared->MemoryObjects);
   for (GLint i = 0; i < n; i++) {
      if (memoryObjects[i] > 0) {
         struct gl_memory_object *delObj
            = _mesa_lookup_memory_object_locked(ctx, memoryObjects[i]);

         if (delObj) {
            _mesa_HashRemoveLocked(ctx->Shared->MemoryObjects,
                                   memoryObjects[i]);
            ctx->Driver.DeleteMemoryObject(ctx, delObj);
         }
      }
   }
   _mesa_HashUnlockMutex(ctx->Shared->MemoryObjects);
}

GLboolean GLAPIENTRY
_mesa_IsMemoryObjectEXT(GLuint memoryObject)
{
   GET_CURRENT_CONTEXT(ctx);

   if (!ctx->Extensions.EXT_memory_object) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glIsMemoryObjectEXT(unsupported)");
      return GL_FALSE;
   }

   struct gl_memory_object *obj =
      _mesa_lookup_memory_object(ctx, memoryObject);

   return obj ? GL_TRUE : GL_FALSE;
}

void GLAPIENTRY
_mesa_CreateMemoryObjectsEXT(GLsizei n, GLuint *memoryObjects)
{
   GET_CURRENT_CONTEXT(ctx);

   const char *func = "glCreateMemoryObjectsEXT";

   if (MESA_VERBOSE & (VERBOSE_API))
      _mesa_debug(ctx, "%s(%d, %p)", func, n, memoryObjects);

   if (!ctx->Extensions.EXT_memory_object) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(unsupported)", func);
      return;
   }

   if (n < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(n < 0)", func);
      return;
   }

   if (!memoryObjects)
      return;

   _mesa_HashLockMutex(ctx->Shared->MemoryObjects);
   GLuint first = _mesa_HashFindFreeKeyBlock(ctx->Shared->MemoryObjects, n);
   if (first) {
      for (GLsizei i = 0; i < n; i++) {
         struct gl_memory_object *memObj;

         memoryObjects[i] = first + i;

         /* allocate memory object */
         memObj = ctx->Driver.NewMemoryObject(ctx, memoryObjects[i]);
         if (!memObj) {
            _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s()", func);
            _mesa_HashUnlockMutex(ctx->Shared->MemoryObjects);
            return;
         }

         /* insert into hash table */
         _mesa_HashInsertLocked(ctx->Shared->MemoryObjects,
                                memoryObjects[i],
                                memObj);
      }
   }

   _mesa_HashUnlockMutex(ctx->Shared->MemoryObjects);
}

void GLAPIENTRY
_mesa_MemoryObjectParameterivEXT(GLuint memoryObject,
                                 GLenum pname,
                                 const GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_memory_object *memObj;

   const char *func = "glMemoryObjectParameterivEXT";

   if (!ctx->Extensions.EXT_memory_object) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(unsupported)", func);
      return;
   }

   memObj = _mesa_lookup_memory_object(ctx, memoryObject);
   if (!memObj)
      return;

   if (memObj->Immutable) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(memoryObject is immutable", func);
      return;
   }

   switch (pname) {
   case GL_DEDICATED_MEMORY_OBJECT_EXT:
      memObj->Dedicated = (GLboolean) params[0];
      break;
   case GL_PROTECTED_MEMORY_OBJECT_EXT:
      /* EXT_protected_textures not supported */
      goto invalid_pname;
   default:
      goto invalid_pname;
   }
   return;

invalid_pname:
   _mesa_error(ctx, GL_INVALID_ENUM, "%s(pname=0x%x)", func, pname);
}

void GLAPIENTRY
_mesa_GetMemoryObjectParameterivEXT(GLuint memoryObject,
                                    GLenum pname,
                                    GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_memory_object *memObj;

   const char *func = "glMemoryObjectParameterivEXT";

   if (!ctx->Extensions.EXT_memory_object) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(unsupported)", func);
      return;
   }

   memObj = _mesa_lookup_memory_object(ctx, memoryObject);
   if (!memObj)
      return;

   switch (pname) {
      case GL_DEDICATED_MEMORY_OBJECT_EXT:
         *params = (GLint) memObj->Dedicated;
         break;
      case GL_PROTECTED_MEMORY_OBJECT_EXT:
         /* EXT_protected_textures not supported */
         goto invalid_pname;
      default:
         goto invalid_pname;
   }
   return;

invalid_pname:
   _mesa_error(ctx, GL_INVALID_ENUM, "%s(pname=0x%x)", func, pname);
}

static struct gl_memory_object *
lookup_memory_object_err(struct gl_context *ctx, unsigned memory,
                         const char* func)
{
   if (memory == 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(memory=0)", func);
      return NULL;
   }

   struct gl_memory_object *memObj = _mesa_lookup_memory_object(ctx, memory);
   if (!memObj)
      return NULL;

   if (!memObj->Immutable) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(no associated memory)",
                  func);
      return NULL;
   }

   return memObj;
}

/**
 * Helper used by _mesa_TexStorageMem1/2/3DEXT().
 */
static void
texstorage_memory(GLuint dims, GLenum target, GLsizei levels,
                  GLenum internalFormat, GLsizei width, GLsizei height,
                  GLsizei depth, GLuint memory, GLuint64 offset,
                  const char *func)
{
   struct gl_texture_object *texObj;
   struct gl_memory_object *memObj;

   GET_CURRENT_CONTEXT(ctx);

   if (!ctx->Extensions.EXT_memory_object) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(unsupported)", func);
      return;
   }

   texObj = _mesa_get_current_tex_object(ctx, target);
   if (!texObj)
      return;

   memObj = lookup_memory_object_err(ctx, memory, func);
   if (!memObj)
      return;

   _mesa_texture_storage_memory(ctx, dims, texObj, memObj, target,
                                levels, internalFormat,
                                width, height, depth, offset, false);
}

static void
texstorage_memory_ms(GLuint dims, GLenum target, GLsizei samples,
                     GLenum internalFormat, GLsizei width, GLsizei height,
                     GLsizei depth, GLboolean fixedSampleLocations,
                     GLuint memory, GLuint64 offset, const char* func)
{
   struct gl_texture_object *texObj;
   struct gl_memory_object *memObj;

   GET_CURRENT_CONTEXT(ctx);

   if (!ctx->Extensions.EXT_memory_object) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(unsupported)", func);
      return;
   }

   texObj = _mesa_get_current_tex_object(ctx, target);
   if (!texObj)
      return;

   memObj = lookup_memory_object_err(ctx, memory, func);
   if (!memObj)
      return;

   _mesa_texture_storage_ms_memory(ctx, dims, texObj, memObj, target, samples,
                                   internalFormat, width, height, depth,
                                   fixedSampleLocations, offset, func);
}

/**
 * Helper used by _mesa_TextureStorageMem1/2/3DEXT().
 */
static void
texturestorage_memory(GLuint dims, GLuint texture, GLsizei levels,
                      GLenum internalFormat, GLsizei width, GLsizei height,
                      GLsizei depth, GLuint memory, GLuint64 offset,
                      const char *func)
{
   struct gl_texture_object *texObj;
   struct gl_memory_object *memObj;

   GET_CURRENT_CONTEXT(ctx);

   if (!ctx->Extensions.EXT_memory_object) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(unsupported)", func);
      return;
   }

   texObj = _mesa_lookup_texture(ctx, texture);
   if (!texObj)
      return;

   memObj = lookup_memory_object_err(ctx, memory, func);
   if (!memObj)
      return;

   _mesa_texture_storage_memory(ctx, dims, texObj, memObj, texObj->Target,
                                levels, internalFormat,
                                width, height, depth, offset, true);
}

static void
texturestorage_memory_ms(GLuint dims, GLuint texture, GLsizei samples,
                         GLenum internalFormat, GLsizei width, GLsizei height,
                         GLsizei depth, GLboolean fixedSampleLocations,
                         GLuint memory, GLuint64 offset, const char* func)
{
   struct gl_texture_object *texObj;
   struct gl_memory_object *memObj;

   GET_CURRENT_CONTEXT(ctx);

   if (!ctx->Extensions.EXT_memory_object) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(unsupported)", func);
      return;
   }

   texObj = _mesa_lookup_texture(ctx, texture);
   if (!texObj)
      return;

   memObj = lookup_memory_object_err(ctx, memory, func);
   if (!memObj)
      return;

   _mesa_texture_storage_ms_memory(ctx, dims, texObj, memObj, texObj->Target,
                                   samples, internalFormat, width, height,
                                   depth, fixedSampleLocations, offset, func);
}

void GLAPIENTRY
_mesa_TexStorageMem2DEXT(GLenum target,
                         GLsizei levels,
                         GLenum internalFormat,
                         GLsizei width,
                         GLsizei height,
                         GLuint memory,
                         GLuint64 offset)
{
   texstorage_memory(2, target, levels, internalFormat, width, height, 1,
                     memory, offset, "glTexStorageMem2DEXT");
}

void GLAPIENTRY
_mesa_TexStorageMem2DMultisampleEXT(GLenum target,
                                    GLsizei samples,
                                    GLenum internalFormat,
                                    GLsizei width,
                                    GLsizei height,
                                    GLboolean fixedSampleLocations,
                                    GLuint memory,
                                    GLuint64 offset)
{
   texstorage_memory_ms(2, target, samples, internalFormat, width, height, 1,
                        fixedSampleLocations, memory, offset,
                        "glTexStorageMem2DMultisampleEXT");
}

void GLAPIENTRY
_mesa_TexStorageMem3DEXT(GLenum target,
                         GLsizei levels,
                         GLenum internalFormat,
                         GLsizei width,
                         GLsizei height,
                         GLsizei depth,
                         GLuint memory,
                         GLuint64 offset)
{
   texstorage_memory(3, target, levels, internalFormat, width, height, depth,
                     memory, offset, "glTexStorageMem3DEXT");
}

void GLAPIENTRY
_mesa_TexStorageMem3DMultisampleEXT(GLenum target,
                                    GLsizei samples,
                                    GLenum internalFormat,
                                    GLsizei width,
                                    GLsizei height,
                                    GLsizei depth,
                                    GLboolean fixedSampleLocations,
                                    GLuint memory,
                                    GLuint64 offset)
{
   texstorage_memory_ms(3, target, samples, internalFormat, width, height,
                        depth, fixedSampleLocations, memory, offset,
                        "glTexStorageMem3DMultisampleEXT");
}

void GLAPIENTRY
_mesa_TextureStorageMem2DEXT(GLuint texture,
                             GLsizei levels,
                             GLenum internalFormat,
                             GLsizei width,
                             GLsizei height,
                             GLuint memory,
                             GLuint64 offset)
{
   texturestorage_memory(2, texture, levels, internalFormat, width, height, 1,
                         memory, offset, "glTexureStorageMem2DEXT");
}

void GLAPIENTRY
_mesa_TextureStorageMem2DMultisampleEXT(GLuint texture,
                                        GLsizei samples,
                                        GLenum internalFormat,
                                        GLsizei width,
                                        GLsizei height,
                                        GLboolean fixedSampleLocations,
                                        GLuint memory,
                                        GLuint64 offset)
{
   texturestorage_memory_ms(2, texture, samples, internalFormat, width, height,
                            1, fixedSampleLocations, memory, offset,
                            "glTextureStorageMem2DMultisampleEXT");
}

void GLAPIENTRY
_mesa_TextureStorageMem3DEXT(GLuint texture,
                             GLsizei levels,
                             GLenum internalFormat,
                             GLsizei width,
                             GLsizei height,
                             GLsizei depth,
                             GLuint memory,
                             GLuint64 offset)
{
   texturestorage_memory(3, texture, levels, internalFormat, width, height,
                         depth, memory, offset, "glTextureStorageMem3DEXT");
}

void GLAPIENTRY
_mesa_TextureStorageMem3DMultisampleEXT(GLuint texture,
                                        GLsizei samples,
                                        GLenum internalFormat,
                                        GLsizei width,
                                        GLsizei height,
                                        GLsizei depth,
                                        GLboolean fixedSampleLocations,
                                        GLuint memory,
                                        GLuint64 offset)
{
   texturestorage_memory_ms(3, texture, samples, internalFormat, width, height,
                            depth, fixedSampleLocations, memory, offset,
                            "glTextureStorageMem3DMultisampleEXT");
}

void GLAPIENTRY
_mesa_TexStorageMem1DEXT(GLenum target,
                         GLsizei levels,
                         GLenum internalFormat,
                         GLsizei width,
                         GLuint memory,
                         GLuint64 offset)
{
   texstorage_memory(1, target, levels, internalFormat, width, 1, 1, memory,
                     offset, "glTexStorageMem1DEXT");
}

void GLAPIENTRY
_mesa_TextureStorageMem1DEXT(GLuint texture,
                             GLsizei levels,
                             GLenum internalFormat,
                             GLsizei width,
                             GLuint memory,
                             GLuint64 offset)
{
   texturestorage_memory(1, texture, levels, internalFormat, width, 1, 1,
                         memory, offset, "glTextureStorageMem1DEXT");
}

/**
 * Used as a placeholder for semaphore objects between glGenSemaphoresEXT()
 * and glImportSemaphoreFdEXT(), so that glIsSemaphoreEXT() can work correctly.
 */
static struct gl_semaphore_object DummySemaphoreObject;

/**
 * Delete a semaphore object.  Called via ctx->Driver.DeleteSemaphore().
 * Not removed from hash table here.
 */
void
_mesa_delete_semaphore_object(struct gl_context *ctx,
                              struct gl_semaphore_object *semObj)
{
   if (semObj != &DummySemaphoreObject)
      free(semObj);
}

/**
 * Initialize a semaphore object to default values.
 */
void
_mesa_initialize_semaphore_object(struct gl_context *ctx,
                                  struct gl_semaphore_object *obj,
                                  GLuint name)
{
   memset(obj, 0, sizeof(struct gl_semaphore_object));
   obj->Name = name;
}

void GLAPIENTRY
_mesa_GenSemaphoresEXT(GLsizei n, GLuint *semaphores)
{
   GET_CURRENT_CONTEXT(ctx);

   const char *func = "glGenSemaphoresEXT";

   if (MESA_VERBOSE & (VERBOSE_API))
      _mesa_debug(ctx, "%s(%d, %p)", func, n, semaphores);

   if (!ctx->Extensions.EXT_semaphore) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(unsupported)", func);
      return;
   }

   if (n < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(n < 0)", func);
      return;
   }

   if (!semaphores)
      return;

   _mesa_HashLockMutex(ctx->Shared->SemaphoreObjects);
   GLuint first = _mesa_HashFindFreeKeyBlock(ctx->Shared->SemaphoreObjects, n);
   if (first) {
      for (GLsizei i = 0; i < n; i++) {
         semaphores[i] = first + i;
         _mesa_HashInsertLocked(ctx->Shared->SemaphoreObjects,
                                semaphores[i], &DummySemaphoreObject);
      }
   }

   _mesa_HashUnlockMutex(ctx->Shared->SemaphoreObjects);
}

void GLAPIENTRY
_mesa_DeleteSemaphoresEXT(GLsizei n, const GLuint *semaphores)
{
   GET_CURRENT_CONTEXT(ctx);

   const char *func = "glDeleteSemaphoresEXT";

   if (MESA_VERBOSE & (VERBOSE_API)) {
      _mesa_debug(ctx, "%s(%d, %p)\n", func, n, semaphores);
   }

   if (!ctx->Extensions.EXT_semaphore) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(unsupported)", func);
      return;
   }

   if (n < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(n < 0)", func);
      return;
   }

   if (!semaphores)
      return;

   _mesa_HashLockMutex(ctx->Shared->SemaphoreObjects);
   for (GLint i = 0; i < n; i++) {
      if (semaphores[i] > 0) {
         struct gl_semaphore_object *delObj
            = _mesa_lookup_semaphore_object_locked(ctx, semaphores[i]);

         if (delObj) {
            _mesa_HashRemoveLocked(ctx->Shared->SemaphoreObjects,
                                   semaphores[i]);
            ctx->Driver.DeleteSemaphoreObject(ctx, delObj);
         }
      }
   }
   _mesa_HashUnlockMutex(ctx->Shared->SemaphoreObjects);
}

GLboolean GLAPIENTRY
_mesa_IsSemaphoreEXT(GLuint semaphore)
{
   GET_CURRENT_CONTEXT(ctx);

   if (!ctx->Extensions.EXT_semaphore) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "glIsSemaphoreEXT(unsupported)");
      return GL_FALSE;
   }

   struct gl_semaphore_object *obj =
      _mesa_lookup_semaphore_object(ctx, semaphore);

   return obj ? GL_TRUE : GL_FALSE;
}

/**
 * Helper that outputs the correct error status for parameter
 * calls where no pnames are defined
 */
static void
semaphore_parameter_stub(const char* func, GLenum pname)
{
   GET_CURRENT_CONTEXT(ctx);

   if (!ctx->Extensions.EXT_semaphore) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(unsupported)", func);
      return;
   }

   /* EXT_semaphore and EXT_semaphore_fd define no parameters */
   _mesa_error(ctx, GL_INVALID_ENUM, "%s(pname=0x%x)", func, pname);
}

void GLAPIENTRY
_mesa_SemaphoreParameterui64vEXT(GLuint semaphore,
                                 GLenum pname,
                                 const GLuint64 *params)
{
   const char *func = "glSemaphoreParameterui64vEXT";

   semaphore_parameter_stub(func, pname);
}

void GLAPIENTRY
_mesa_GetSemaphoreParameterui64vEXT(GLuint semaphore,
                                    GLenum pname,
                                    GLuint64 *params)
{
   const char *func = "glGetSemaphoreParameterui64vEXT";

   semaphore_parameter_stub(func, pname);
}

void GLAPIENTRY
_mesa_WaitSemaphoreEXT(GLuint semaphore,
                       GLuint numBufferBarriers,
                       const GLuint *buffers,
                       GLuint numTextureBarriers,
                       const GLuint *textures,
                       const GLenum *srcLayouts)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_semaphore_object *semObj = NULL;
   struct gl_buffer_object **bufObjs = NULL;
   struct gl_texture_object **texObjs = NULL;

   const char *func = "glWaitSemaphoreEXT";

   if (!ctx->Extensions.EXT_semaphore) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(unsupported)", func);
      return;
   }

   ASSERT_OUTSIDE_BEGIN_END(ctx);

   semObj = _mesa_lookup_semaphore_object(ctx, semaphore);
   if (!semObj)
      return;

   FLUSH_VERTICES(ctx, 0);
   FLUSH_CURRENT(ctx, 0);

   bufObjs = malloc(sizeof(struct gl_buffer_object *) * numBufferBarriers);
   if (!bufObjs) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s(numBufferBarriers=%u)",
                  func, numBufferBarriers);
      goto end;
   }

   for (unsigned i = 0; i < numBufferBarriers; i++) {
      bufObjs[i] = _mesa_lookup_bufferobj(ctx, buffers[i]);
   }

   texObjs = malloc(sizeof(struct gl_texture_object *) * numTextureBarriers);
   if (!texObjs) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s(numTextureBarriers=%u)",
                  func, numTextureBarriers);
      goto end;
   }

   for (unsigned i = 0; i < numTextureBarriers; i++) {
      texObjs[i] = _mesa_lookup_texture(ctx, textures[i]);
   }

   ctx->Driver.ServerWaitSemaphoreObject(ctx, semObj,
                                         numBufferBarriers, bufObjs,
                                         numTextureBarriers, texObjs,
                                         srcLayouts);

end:
   free(bufObjs);
   free(texObjs);
}

void GLAPIENTRY
_mesa_SignalSemaphoreEXT(GLuint semaphore,
                         GLuint numBufferBarriers,
                         const GLuint *buffers,
                         GLuint numTextureBarriers,
                         const GLuint *textures,
                         const GLenum *dstLayouts)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_semaphore_object *semObj = NULL;
   struct gl_buffer_object **bufObjs = NULL;
   struct gl_texture_object **texObjs = NULL;

   const char *func = "glSignalSemaphoreEXT";

   if (!ctx->Extensions.EXT_semaphore) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(unsupported)", func);
      return;
   }

   ASSERT_OUTSIDE_BEGIN_END(ctx);

   semObj = _mesa_lookup_semaphore_object(ctx, semaphore);
   if (!semObj)
      return;

   FLUSH_VERTICES(ctx, 0);
   FLUSH_CURRENT(ctx, 0);

   bufObjs = malloc(sizeof(struct gl_buffer_object *) * numBufferBarriers);
   if (!bufObjs) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s(numBufferBarriers=%u)",
                  func, numBufferBarriers);
      goto end;
   }

   for (unsigned i = 0; i < numBufferBarriers; i++) {
      bufObjs[i] = _mesa_lookup_bufferobj(ctx, buffers[i]);
   }

   texObjs = malloc(sizeof(struct gl_texture_object *) * numTextureBarriers);
   if (!texObjs) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s(numTextureBarriers=%u)",
                  func, numTextureBarriers);
      goto end;
   }

   for (unsigned i = 0; i < numTextureBarriers; i++) {
      texObjs[i] = _mesa_lookup_texture(ctx, textures[i]);
   }

   ctx->Driver.ServerSignalSemaphoreObject(ctx, semObj,
                                           numBufferBarriers, bufObjs,
                                           numTextureBarriers, texObjs,
                                           dstLayouts);

end:
   free(bufObjs);
   free(texObjs);
}

void GLAPIENTRY
_mesa_ImportMemoryFdEXT(GLuint memory,
                        GLuint64 size,
                        GLenum handleType,
                        GLint fd)
{
   GET_CURRENT_CONTEXT(ctx);

   const char *func = "glImportMemoryFdEXT";

   if (!ctx->Extensions.EXT_memory_object_fd) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(unsupported)", func);
      return;
   }

   if (handleType != GL_HANDLE_TYPE_OPAQUE_FD_EXT) {
      _mesa_error(ctx, GL_INVALID_ENUM, "%s(handleType=%u)", func, handleType);
      return;
   }

   struct gl_memory_object *memObj = _mesa_lookup_memory_object(ctx, memory);
   if (!memObj)
      return;

   ctx->Driver.ImportMemoryObjectFd(ctx, memObj, size, fd);
   memObj->Immutable = GL_TRUE;
}

void GLAPIENTRY
_mesa_ImportSemaphoreFdEXT(GLuint semaphore,
                           GLenum handleType,
                           GLint fd)
{
   GET_CURRENT_CONTEXT(ctx);

   const char *func = "glImportSemaphoreFdEXT";

   if (!ctx->Extensions.EXT_semaphore_fd) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(unsupported)", func);
      return;
   }

   if (handleType != GL_HANDLE_TYPE_OPAQUE_FD_EXT) {
      _mesa_error(ctx, GL_INVALID_ENUM, "%s(handleType=%u)", func, handleType);
      return;
   }

   struct gl_semaphore_object *semObj = _mesa_lookup_semaphore_object(ctx,
                                                                      semaphore);
   if (!semObj)
      return;

   if (semObj == &DummySemaphoreObject) {
      semObj = ctx->Driver.NewSemaphoreObject(ctx, semaphore);
      if (!semObj) {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s", func);
         return;
      }
      _mesa_HashInsert(ctx->Shared->SemaphoreObjects, semaphore, semObj);
   }

   ctx->Driver.ImportSemaphoreFd(ctx, semObj, fd);
}
