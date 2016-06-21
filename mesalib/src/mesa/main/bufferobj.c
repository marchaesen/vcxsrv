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
 * \file bufferobj.c
 * \brief Functions for the GL_ARB_vertex/pixel_buffer_object extensions.
 * \author Brian Paul, Ian Romanick
 */

#include <stdbool.h>
#include <inttypes.h>  /* for PRId64 macro */
#include "util/debug.h"
#include "glheader.h"
#include "enums.h"
#include "hash.h"
#include "imports.h"
#include "context.h"
#include "bufferobj.h"
#include "mtypes.h"
#include "teximage.h"
#include "glformats.h"
#include "texstore.h"
#include "transformfeedback.h"


/* Debug flags */
/*#define VBO_DEBUG*/
/*#define BOUNDS_CHECK*/


/**
 * We count the number of buffer modification calls to check for
 * inefficient buffer use.  This is the number of such calls before we
 * issue a warning.
 */
#define BUFFER_WARNING_CALL_COUNT 4


/**
 * Helper to warn of possible performance issues, such as frequently
 * updating a buffer created with GL_STATIC_DRAW.  Called via the macro
 * below.
 */
static void
buffer_usage_warning(struct gl_context *ctx, GLuint *id, const char *fmt, ...)
{
   va_list args;

   va_start(args, fmt);
   _mesa_gl_vdebug(ctx, id,
                   MESA_DEBUG_SOURCE_API,
                   MESA_DEBUG_TYPE_PERFORMANCE,
                   MESA_DEBUG_SEVERITY_MEDIUM,
                   fmt, args);
   va_end(args);
}

#define BUFFER_USAGE_WARNING(CTX, FMT, ...) \
   do { \
      static GLuint id = 0; \
      buffer_usage_warning(CTX, &id, FMT, ##__VA_ARGS__); \
   } while (0)


/**
 * Used as a placeholder for buffer objects between glGenBuffers() and
 * glBindBuffer() so that glIsBuffer() can work correctly.
 */
static struct gl_buffer_object DummyBufferObject;


/**
 * Return pointer to address of a buffer object target.
 * \param ctx  the GL context
 * \param target  the buffer object target to be retrieved.
 * \return   pointer to pointer to the buffer object bound to \c target in the
 *           specified context or \c NULL if \c target is invalid.
 */
static inline struct gl_buffer_object **
get_buffer_target(struct gl_context *ctx, GLenum target)
{
   /* Other targets are only supported in desktop OpenGL and OpenGL ES 3.0.
    */
   if (!_mesa_is_desktop_gl(ctx) && !_mesa_is_gles3(ctx)
       && target != GL_ARRAY_BUFFER && target != GL_ELEMENT_ARRAY_BUFFER)
      return NULL;

   switch (target) {
   case GL_ARRAY_BUFFER_ARB:
      return &ctx->Array.ArrayBufferObj;
   case GL_ELEMENT_ARRAY_BUFFER_ARB:
      return &ctx->Array.VAO->IndexBufferObj;
   case GL_PIXEL_PACK_BUFFER_EXT:
      return &ctx->Pack.BufferObj;
   case GL_PIXEL_UNPACK_BUFFER_EXT:
      return &ctx->Unpack.BufferObj;
   case GL_COPY_READ_BUFFER:
      return &ctx->CopyReadBuffer;
   case GL_COPY_WRITE_BUFFER:
      return &ctx->CopyWriteBuffer;
   case GL_QUERY_BUFFER:
      if (_mesa_has_ARB_query_buffer_object(ctx))
         return &ctx->QueryBuffer;
      break;
   case GL_DRAW_INDIRECT_BUFFER:
      if ((ctx->API == API_OPENGL_CORE &&
           ctx->Extensions.ARB_draw_indirect) ||
           _mesa_is_gles31(ctx)) {
         return &ctx->DrawIndirectBuffer;
      }
      break;
   case GL_PARAMETER_BUFFER_ARB:
      if (_mesa_has_ARB_indirect_parameters(ctx)) {
         return &ctx->ParameterBuffer;
      }
      break;
   case GL_DISPATCH_INDIRECT_BUFFER:
      if (_mesa_has_compute_shaders(ctx)) {
         return &ctx->DispatchIndirectBuffer;
      }
      break;
   case GL_TRANSFORM_FEEDBACK_BUFFER:
      if (ctx->Extensions.EXT_transform_feedback) {
         return &ctx->TransformFeedback.CurrentBuffer;
      }
      break;
   case GL_TEXTURE_BUFFER:
      if (_mesa_has_ARB_texture_buffer_object(ctx) ||
          _mesa_has_OES_texture_buffer(ctx)) {
         return &ctx->Texture.BufferObject;
      }
      break;
   case GL_UNIFORM_BUFFER:
      if (ctx->Extensions.ARB_uniform_buffer_object) {
         return &ctx->UniformBuffer;
      }
      break;
   case GL_SHADER_STORAGE_BUFFER:
      if (ctx->Extensions.ARB_shader_storage_buffer_object) {
         return &ctx->ShaderStorageBuffer;
      }
      break;
   case GL_ATOMIC_COUNTER_BUFFER:
      if (ctx->Extensions.ARB_shader_atomic_counters) {
         return &ctx->AtomicBuffer;
      }
      break;
   case GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD:
      if (ctx->Extensions.AMD_pinned_memory) {
         return &ctx->ExternalVirtualMemoryBuffer;
      }
      break;
   default:
      return NULL;
   }
   return NULL;
}


/**
 * Get the buffer object bound to the specified target in a GL context.
 * \param ctx  the GL context
 * \param target  the buffer object target to be retrieved.
 * \param error  the GL error to record if target is illegal.
 * \return   pointer to the buffer object bound to \c target in the
 *           specified context or \c NULL if \c target is invalid.
 */
static inline struct gl_buffer_object *
get_buffer(struct gl_context *ctx, const char *func, GLenum target,
           GLenum error)
{
   struct gl_buffer_object **bufObj = get_buffer_target(ctx, target);

   if (!bufObj) {
      _mesa_error(ctx, GL_INVALID_ENUM, "%s(target)", func);
      return NULL;
   }

   if (!_mesa_is_bufferobj(*bufObj)) {
      _mesa_error(ctx, error, "%s(no buffer bound)", func);
      return NULL;
   }

   return *bufObj;
}


/**
 * Convert a GLbitfield describing the mapped buffer access flags
 * into one of GL_READ_WRITE, GL_READ_ONLY, or GL_WRITE_ONLY.
 */
static GLenum
simplified_access_mode(struct gl_context *ctx, GLbitfield access)
{
   const GLbitfield rwFlags = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
   if ((access & rwFlags) == rwFlags)
      return GL_READ_WRITE;
   if ((access & GL_MAP_READ_BIT) == GL_MAP_READ_BIT)
      return GL_READ_ONLY;
   if ((access & GL_MAP_WRITE_BIT) == GL_MAP_WRITE_BIT)
      return GL_WRITE_ONLY;

   /* Otherwise, AccessFlags is zero (the default state).
    *
    * Table 2.6 on page 31 (page 44 of the PDF) of the OpenGL 1.5 spec says:
    *
    * Name           Type  Initial Value  Legal Values
    * ...            ...   ...            ...
    * BUFFER_ACCESS  enum  READ_WRITE     READ_ONLY, WRITE_ONLY
    *                                     READ_WRITE
    *
    * However, table 6.8 in the GL_OES_mapbuffer extension says:
    *
    * Get Value         Type Get Command          Value          Description
    * ---------         ---- -----------          -----          -----------
    * BUFFER_ACCESS_OES Z1   GetBufferParameteriv WRITE_ONLY_OES buffer map flag
    *
    * The difference is because GL_OES_mapbuffer only supports mapping buffers
    * write-only.
    */
   assert(access == 0);

   return _mesa_is_gles(ctx) ? GL_WRITE_ONLY : GL_READ_WRITE;
}


/**
 * Test if the buffer is mapped, and if so, if the mapped range overlaps the
 * given range.
 * The regions do not overlap if and only if the end of the given
 * region is before the mapped region or the start of the given region
 * is after the mapped region.
 *
 * \param obj     Buffer object target on which to operate.
 * \param offset  Offset of the first byte of the subdata range.
 * \param size    Size, in bytes, of the subdata range.
 * \return   true if ranges overlap, false otherwise
 *
 */
static bool
bufferobj_range_mapped(const struct gl_buffer_object *obj,
                       GLintptr offset, GLsizeiptr size)
{
   if (_mesa_bufferobj_mapped(obj, MAP_USER)) {
      const GLintptr end = offset + size;
      const GLintptr mapEnd = obj->Mappings[MAP_USER].Offset +
                              obj->Mappings[MAP_USER].Length;

      if (!(end <= obj->Mappings[MAP_USER].Offset || offset >= mapEnd)) {
         return true;
      }
   }
   return false;
}


/**
 * Tests the subdata range parameters and sets the GL error code for
 * \c glBufferSubDataARB, \c glGetBufferSubDataARB and
 * \c glClearBufferSubData.
 *
 * \param ctx     GL context.
 * \param bufObj  The buffer object.
 * \param offset  Offset of the first byte of the subdata range.
 * \param size    Size, in bytes, of the subdata range.
 * \param mappedRange  If true, checks if an overlapping range is mapped.
 *                     If false, checks if buffer is mapped.
 * \param caller  Name of calling function for recording errors.
 * \return   false if error, true otherwise
 *
 * \sa glBufferSubDataARB, glGetBufferSubDataARB, glClearBufferSubData
 */
static bool
buffer_object_subdata_range_good(struct gl_context *ctx,
                                 const struct gl_buffer_object *bufObj,
                                 GLintptr offset, GLsizeiptr size,
                                 bool mappedRange, const char *caller)
{
   if (size < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(size < 0)", caller);
      return false;
   }

   if (offset < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(offset < 0)", caller);
      return false;
   }

   if (offset + size > bufObj->Size) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(offset %lu + size %lu > buffer size %lu)", caller,
                  (unsigned long) offset,
                  (unsigned long) size,
                  (unsigned long) bufObj->Size);
      return false;
   }

   if (bufObj->Mappings[MAP_USER].AccessFlags & GL_MAP_PERSISTENT_BIT)
      return true;

   if (mappedRange) {
      if (bufferobj_range_mapped(bufObj, offset, size)) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "%s(range is mapped without persistent bit)",
                     caller);
         return false;
      }
   }
   else {
      if (_mesa_bufferobj_mapped(bufObj, MAP_USER)) {
         _mesa_error(ctx, GL_INVALID_OPERATION,
                     "%s(buffer is mapped without persistent bit)",
                     caller);
         return false;
      }
   }

   return true;
}


/**
 * Test the format and type parameters and set the GL error code for
 * \c glClearBufferData and \c glClearBufferSubData.
 *
 * \param ctx             GL context.
 * \param internalformat  Format to which the data is to be converted.
 * \param format          Format of the supplied data.
 * \param type            Type of the supplied data.
 * \param caller          Name of calling function for recording errors.
 * \return   If internalformat, format and type are legal the mesa_format
 *           corresponding to internalformat, otherwise MESA_FORMAT_NONE.
 *
 * \sa glClearBufferData and glClearBufferSubData
 */
static mesa_format
validate_clear_buffer_format(struct gl_context *ctx,
                             GLenum internalformat,
                             GLenum format, GLenum type,
                             const char *caller)
{
   mesa_format mesaFormat;
   GLenum errorFormatType;

   mesaFormat = _mesa_validate_texbuffer_format(ctx, internalformat);
   if (mesaFormat == MESA_FORMAT_NONE) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "%s(invalid internalformat)", caller);
      return MESA_FORMAT_NONE;
   }

   /* NOTE: not mentioned in ARB_clear_buffer_object but according to
    * EXT_texture_integer there is no conversion between integer and
    * non-integer formats
   */
   if (_mesa_is_enum_format_signed_int(format) !=
       _mesa_is_format_integer_color(mesaFormat)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(integer vs non-integer)", caller);
      return MESA_FORMAT_NONE;
   }

   if (!_mesa_is_color_format(format)) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "%s(format is not a color format)", caller);
      return MESA_FORMAT_NONE;
   }

   errorFormatType = _mesa_error_check_format_and_type(ctx, format, type);
   if (errorFormatType != GL_NO_ERROR) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "%s(invalid format or type)", caller);
      return MESA_FORMAT_NONE;
   }

   return mesaFormat;
}


/**
 * Convert user-specified clear value to the specified internal format.
 *
 * \param ctx             GL context.
 * \param internalformat  Format to which the data is converted.
 * \param clearValue      Points to the converted clear value.
 * \param format          Format of the supplied data.
 * \param type            Type of the supplied data.
 * \param data            Data which is to be converted to internalformat.
 * \param caller          Name of calling function for recording errors.
 * \return   true if data could be converted, false otherwise.
 *
 * \sa glClearBufferData, glClearBufferSubData
 */
static bool
convert_clear_buffer_data(struct gl_context *ctx,
                          mesa_format internalformat,
                          GLubyte *clearValue, GLenum format, GLenum type,
                          const GLvoid *data, const char *caller)
{
   GLenum internalformatBase = _mesa_get_format_base_format(internalformat);

   if (_mesa_texstore(ctx, 1, internalformatBase, internalformat,
                      0, &clearValue, 1, 1, 1,
                      format, type, data, &ctx->Unpack)) {
      return true;
   }
   else {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s", caller);
      return false;
   }
}


/**
 * Allocate and initialize a new buffer object.
 *
 * Default callback for the \c dd_function_table::NewBufferObject() hook.
 */
static struct gl_buffer_object *
_mesa_new_buffer_object(struct gl_context *ctx, GLuint name)
{
   struct gl_buffer_object *obj;

   (void) ctx;

   obj = MALLOC_STRUCT(gl_buffer_object);
   _mesa_initialize_buffer_object(ctx, obj, name);
   return obj;
}


/**
 * Delete a buffer object.
 *
 * Default callback for the \c dd_function_table::DeleteBuffer() hook.
 */
void
_mesa_delete_buffer_object(struct gl_context *ctx,
                           struct gl_buffer_object *bufObj)
{
   (void) ctx;

   vbo_delete_minmax_cache(bufObj);
   _mesa_align_free(bufObj->Data);

   /* assign strange values here to help w/ debugging */
   bufObj->RefCount = -1000;
   bufObj->Name = ~0;

   mtx_destroy(&bufObj->Mutex);
   free(bufObj->Label);
   free(bufObj);
}



/**
 * Set ptr to bufObj w/ reference counting.
 * This is normally only called from the _mesa_reference_buffer_object() macro
 * when there's a real pointer change.
 */
void
_mesa_reference_buffer_object_(struct gl_context *ctx,
                               struct gl_buffer_object **ptr,
                               struct gl_buffer_object *bufObj)
{
   if (*ptr) {
      /* Unreference the old buffer */
      GLboolean deleteFlag = GL_FALSE;
      struct gl_buffer_object *oldObj = *ptr;

      mtx_lock(&oldObj->Mutex);
      assert(oldObj->RefCount > 0);
      oldObj->RefCount--;
      deleteFlag = (oldObj->RefCount == 0);
      mtx_unlock(&oldObj->Mutex);

      if (deleteFlag) {
	 assert(ctx->Driver.DeleteBuffer);
         ctx->Driver.DeleteBuffer(ctx, oldObj);
      }

      *ptr = NULL;
   }
   assert(!*ptr);

   if (bufObj) {
      /* reference new buffer */
      mtx_lock(&bufObj->Mutex);
      if (bufObj->RefCount == 0) {
         /* this buffer's being deleted (look just above) */
         /* Not sure this can every really happen.  Warn if it does. */
         _mesa_problem(NULL, "referencing deleted buffer object");
         *ptr = NULL;
      }
      else {
         bufObj->RefCount++;
         *ptr = bufObj;
      }
      mtx_unlock(&bufObj->Mutex);
   }
}


/**
 * Get the value of MESA_NO_MINMAX_CACHE.
 */
static bool
get_no_minmax_cache()
{
   static bool read = false;
   static bool disable = false;

   if (!read) {
      disable = env_var_as_boolean("MESA_NO_MINMAX_CACHE", false);
      read = true;
   }

   return disable;
}


/**
 * Initialize a buffer object to default values.
 */
void
_mesa_initialize_buffer_object(struct gl_context *ctx,
                               struct gl_buffer_object *obj,
                               GLuint name)
{
   memset(obj, 0, sizeof(struct gl_buffer_object));
   mtx_init(&obj->Mutex, mtx_plain);
   obj->RefCount = 1;
   obj->Name = name;
   obj->Usage = GL_STATIC_DRAW_ARB;

   if (get_no_minmax_cache())
      obj->UsageHistory |= USAGE_DISABLE_MINMAX_CACHE;
}



/**
 * Callback called from _mesa_HashWalk()
 */
static void
count_buffer_size(GLuint key, void *data, void *userData)
{
   const struct gl_buffer_object *bufObj =
      (const struct gl_buffer_object *) data;
   GLuint *total = (GLuint *) userData;

   (void) key;
   *total = *total + bufObj->Size;
}


/**
 * Compute total size (in bytes) of all buffer objects for the given context.
 * For debugging purposes.
 */
GLuint
_mesa_total_buffer_object_memory(struct gl_context *ctx)
{
   GLuint total = 0;

   _mesa_HashWalk(ctx->Shared->BufferObjects, count_buffer_size, &total);

   return total;
}


/**
 * Allocate space for and store data in a buffer object.  Any data that was
 * previously stored in the buffer object is lost.  If \c data is \c NULL,
 * memory will be allocated, but no copy will occur.
 *
 * This is the default callback for \c dd_function_table::BufferData()
 * Note that all GL error checking will have been done already.
 *
 * \param ctx     GL context.
 * \param target  Buffer object target on which to operate.
 * \param size    Size, in bytes, of the new data store.
 * \param data    Pointer to the data to store in the buffer object.  This
 *                pointer may be \c NULL.
 * \param usage   Hints about how the data will be used.
 * \param bufObj  Object to be used.
 *
 * \return GL_TRUE for success, GL_FALSE for failure
 * \sa glBufferDataARB, dd_function_table::BufferData.
 */
static GLboolean
buffer_data_fallback(struct gl_context *ctx, GLenum target, GLsizeiptr size,
                     const GLvoid *data, GLenum usage, GLenum storageFlags,
                     struct gl_buffer_object *bufObj)
{
   void * new_data;

   (void) target;

   _mesa_align_free( bufObj->Data );

   new_data = _mesa_align_malloc( size, ctx->Const.MinMapBufferAlignment );
   if (new_data) {
      bufObj->Data = (GLubyte *) new_data;
      bufObj->Size = size;
      bufObj->Usage = usage;
      bufObj->StorageFlags = storageFlags;

      if (data) {
	 memcpy( bufObj->Data, data, size );
      }

      return GL_TRUE;
   }
   else {
      return GL_FALSE;
   }
}


/**
 * Replace data in a subrange of buffer object.  If the data range
 * specified by \c size + \c offset extends beyond the end of the buffer or
 * if \c data is \c NULL, no copy is performed.
 *
 * This is the default callback for \c dd_function_table::BufferSubData()
 * Note that all GL error checking will have been done already.
 *
 * \param ctx     GL context.
 * \param offset  Offset of the first byte to be modified.
 * \param size    Size, in bytes, of the data range.
 * \param data    Pointer to the data to store in the buffer object.
 * \param bufObj  Object to be used.
 *
 * \sa glBufferSubDataARB, dd_function_table::BufferSubData.
 */
static void
buffer_sub_data_fallback(struct gl_context *ctx, GLintptr offset,
                         GLsizeiptr size, const GLvoid *data,
                         struct gl_buffer_object *bufObj)
{
   (void) ctx;

   /* this should have been caught in _mesa_BufferSubData() */
   assert(size + offset <= bufObj->Size);

   if (bufObj->Data) {
      memcpy( (GLubyte *) bufObj->Data + offset, data, size );
   }
}


/**
 * Retrieve data from a subrange of buffer object.  If the data range
 * specified by \c size + \c offset extends beyond the end of the buffer or
 * if \c data is \c NULL, no copy is performed.
 *
 * This is the default callback for \c dd_function_table::GetBufferSubData()
 * Note that all GL error checking will have been done already.
 *
 * \param ctx     GL context.
 * \param target  Buffer object target on which to operate.
 * \param offset  Offset of the first byte to be fetched.
 * \param size    Size, in bytes, of the data range.
 * \param data    Destination for data
 * \param bufObj  Object to be used.
 *
 * \sa glBufferGetSubDataARB, dd_function_table::GetBufferSubData.
 */
static void
_mesa_buffer_get_subdata( struct gl_context *ctx, GLintptrARB offset,
			  GLsizeiptrARB size, GLvoid * data,
			  struct gl_buffer_object * bufObj )
{
   (void) ctx;

   if (bufObj->Data && ((GLsizeiptrARB) (size + offset) <= bufObj->Size)) {
      memcpy( data, (GLubyte *) bufObj->Data + offset, size );
   }
}


/**
 * Clear a subrange of the buffer object with copies of the supplied data.
 * If data is NULL the buffer is filled with zeros.
 *
 * This is the default callback for \c dd_function_table::ClearBufferSubData()
 * Note that all GL error checking will have been done already.
 *
 * \param ctx             GL context.
 * \param offset          Offset of the first byte to be cleared.
 * \param size            Size, in bytes, of the to be cleared range.
 * \param clearValue      Source of the data.
 * \param clearValueSize  Size, in bytes, of the supplied data.
 * \param bufObj          Object to be cleared.
 *
 * \sa glClearBufferSubData, glClearBufferData and
 * dd_function_table::ClearBufferSubData.
 */
void
_mesa_ClearBufferSubData_sw(struct gl_context *ctx,
                            GLintptr offset, GLsizeiptr size,
                            const GLvoid *clearValue,
                            GLsizeiptr clearValueSize,
                            struct gl_buffer_object *bufObj)
{
   GLsizeiptr i;
   GLubyte *dest;

   assert(ctx->Driver.MapBufferRange);
   dest = ctx->Driver.MapBufferRange(ctx, offset, size,
                                     GL_MAP_WRITE_BIT |
                                     GL_MAP_INVALIDATE_RANGE_BIT,
                                     bufObj, MAP_INTERNAL);

   if (!dest) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "glClearBuffer[Sub]Data");
      return;
   }

   if (clearValue == NULL) {
      /* Clear with zeros, per the spec */
      memset(dest, 0, size);
      ctx->Driver.UnmapBuffer(ctx, bufObj, MAP_INTERNAL);
      return;
   }

   for (i = 0; i < size/clearValueSize; ++i) {
      memcpy(dest, clearValue, clearValueSize);
      dest += clearValueSize;
   }

   ctx->Driver.UnmapBuffer(ctx, bufObj, MAP_INTERNAL);
}


/**
 * Default fallback for \c dd_function_table::MapBufferRange().
 * Called via glMapBufferRange().
 */
static void *
map_buffer_range_fallback(struct gl_context *ctx, GLintptr offset,
                          GLsizeiptr length, GLbitfield access,
                          struct gl_buffer_object *bufObj,
                          gl_map_buffer_index index)
{
   (void) ctx;
   assert(!_mesa_bufferobj_mapped(bufObj, index));
   /* Just return a direct pointer to the data */
   bufObj->Mappings[index].Pointer = bufObj->Data + offset;
   bufObj->Mappings[index].Length = length;
   bufObj->Mappings[index].Offset = offset;
   bufObj->Mappings[index].AccessFlags = access;
   return bufObj->Mappings[index].Pointer;
}


/**
 * Default fallback for \c dd_function_table::FlushMappedBufferRange().
 * Called via glFlushMappedBufferRange().
 */
static void
flush_mapped_buffer_range_fallback(struct gl_context *ctx,
                                   GLintptr offset, GLsizeiptr length,
                                   struct gl_buffer_object *obj,
                                   gl_map_buffer_index index)
{
   (void) ctx;
   (void) offset;
   (void) length;
   (void) obj;
   (void) index;
   /* no-op */
}


/**
 * Default callback for \c dd_function_table::UnmapBuffer().
 *
 * The input parameters will have been already tested for errors.
 *
 * \sa glUnmapBufferARB, dd_function_table::UnmapBuffer
 */
static GLboolean
unmap_buffer_fallback(struct gl_context *ctx, struct gl_buffer_object *bufObj,
                      gl_map_buffer_index index)
{
   (void) ctx;
   /* XXX we might assert here that bufObj->Pointer is non-null */
   bufObj->Mappings[index].Pointer = NULL;
   bufObj->Mappings[index].Length = 0;
   bufObj->Mappings[index].Offset = 0;
   bufObj->Mappings[index].AccessFlags = 0x0;
   return GL_TRUE;
}


/**
 * Default fallback for \c dd_function_table::CopyBufferSubData().
 * Called via glCopyBufferSubData().
 */
static void
copy_buffer_sub_data_fallback(struct gl_context *ctx,
                              struct gl_buffer_object *src,
                              struct gl_buffer_object *dst,
                              GLintptr readOffset, GLintptr writeOffset,
                              GLsizeiptr size)
{
   GLubyte *srcPtr, *dstPtr;

   if (src == dst) {
      srcPtr = dstPtr = ctx->Driver.MapBufferRange(ctx, 0, src->Size,
						   GL_MAP_READ_BIT |
						   GL_MAP_WRITE_BIT, src,
                                                   MAP_INTERNAL);

      if (!srcPtr)
	 return;

      srcPtr += readOffset;
      dstPtr += writeOffset;
   } else {
      srcPtr = ctx->Driver.MapBufferRange(ctx, readOffset, size,
					  GL_MAP_READ_BIT, src,
                                          MAP_INTERNAL);
      dstPtr = ctx->Driver.MapBufferRange(ctx, writeOffset, size,
					  (GL_MAP_WRITE_BIT |
					   GL_MAP_INVALIDATE_RANGE_BIT), dst,
                                          MAP_INTERNAL);
   }

   /* Note: the src and dst regions will never overlap.  Trying to do so
    * would generate GL_INVALID_VALUE earlier.
    */
   if (srcPtr && dstPtr)
      memcpy(dstPtr, srcPtr, size);

   ctx->Driver.UnmapBuffer(ctx, src, MAP_INTERNAL);
   if (dst != src)
      ctx->Driver.UnmapBuffer(ctx, dst, MAP_INTERNAL);
}



/**
 * Initialize the state associated with buffer objects
 */
void
_mesa_init_buffer_objects( struct gl_context *ctx )
{
   GLuint i;

   memset(&DummyBufferObject, 0, sizeof(DummyBufferObject));
   mtx_init(&DummyBufferObject.Mutex, mtx_plain);
   DummyBufferObject.RefCount = 1000*1000*1000; /* never delete */

   _mesa_reference_buffer_object(ctx, &ctx->Array.ArrayBufferObj,
                                 ctx->Shared->NullBufferObj);

   _mesa_reference_buffer_object(ctx, &ctx->CopyReadBuffer,
                                 ctx->Shared->NullBufferObj);
   _mesa_reference_buffer_object(ctx, &ctx->CopyWriteBuffer,
                                 ctx->Shared->NullBufferObj);

   _mesa_reference_buffer_object(ctx, &ctx->UniformBuffer,
				 ctx->Shared->NullBufferObj);

   _mesa_reference_buffer_object(ctx, &ctx->ShaderStorageBuffer,
                                 ctx->Shared->NullBufferObj);

   _mesa_reference_buffer_object(ctx, &ctx->AtomicBuffer,
				 ctx->Shared->NullBufferObj);

   _mesa_reference_buffer_object(ctx, &ctx->DrawIndirectBuffer,
				 ctx->Shared->NullBufferObj);

   _mesa_reference_buffer_object(ctx, &ctx->ParameterBuffer,
				 ctx->Shared->NullBufferObj);

   _mesa_reference_buffer_object(ctx, &ctx->DispatchIndirectBuffer,
				 ctx->Shared->NullBufferObj);

   _mesa_reference_buffer_object(ctx, &ctx->QueryBuffer,
                                 ctx->Shared->NullBufferObj);

   for (i = 0; i < MAX_COMBINED_UNIFORM_BUFFERS; i++) {
      _mesa_reference_buffer_object(ctx,
				    &ctx->UniformBufferBindings[i].BufferObject,
				    ctx->Shared->NullBufferObj);
      ctx->UniformBufferBindings[i].Offset = -1;
      ctx->UniformBufferBindings[i].Size = -1;
   }

   for (i = 0; i < MAX_COMBINED_SHADER_STORAGE_BUFFERS; i++) {
      _mesa_reference_buffer_object(ctx,
                                    &ctx->ShaderStorageBufferBindings[i].BufferObject,
                                    ctx->Shared->NullBufferObj);
      ctx->ShaderStorageBufferBindings[i].Offset = -1;
      ctx->ShaderStorageBufferBindings[i].Size = -1;
   }

   for (i = 0; i < MAX_COMBINED_ATOMIC_BUFFERS; i++) {
      _mesa_reference_buffer_object(ctx,
				    &ctx->AtomicBufferBindings[i].BufferObject,
				    ctx->Shared->NullBufferObj);
      ctx->AtomicBufferBindings[i].Offset = 0;
      ctx->AtomicBufferBindings[i].Size = 0;
   }
}


void
_mesa_free_buffer_objects( struct gl_context *ctx )
{
   GLuint i;

   _mesa_reference_buffer_object(ctx, &ctx->Array.ArrayBufferObj, NULL);

   _mesa_reference_buffer_object(ctx, &ctx->CopyReadBuffer, NULL);
   _mesa_reference_buffer_object(ctx, &ctx->CopyWriteBuffer, NULL);

   _mesa_reference_buffer_object(ctx, &ctx->UniformBuffer, NULL);

   _mesa_reference_buffer_object(ctx, &ctx->ShaderStorageBuffer, NULL);

   _mesa_reference_buffer_object(ctx, &ctx->AtomicBuffer, NULL);

   _mesa_reference_buffer_object(ctx, &ctx->DrawIndirectBuffer, NULL);

   _mesa_reference_buffer_object(ctx, &ctx->ParameterBuffer, NULL);

   _mesa_reference_buffer_object(ctx, &ctx->DispatchIndirectBuffer, NULL);

   _mesa_reference_buffer_object(ctx, &ctx->QueryBuffer, NULL);

   for (i = 0; i < MAX_COMBINED_UNIFORM_BUFFERS; i++) {
      _mesa_reference_buffer_object(ctx,
				    &ctx->UniformBufferBindings[i].BufferObject,
				    NULL);
   }

   for (i = 0; i < MAX_COMBINED_SHADER_STORAGE_BUFFERS; i++) {
      _mesa_reference_buffer_object(ctx,
                                    &ctx->ShaderStorageBufferBindings[i].BufferObject,
                                    NULL);
   }

   for (i = 0; i < MAX_COMBINED_ATOMIC_BUFFERS; i++) {
      _mesa_reference_buffer_object(ctx,
				    &ctx->AtomicBufferBindings[i].BufferObject,
				    NULL);
   }

}

bool
_mesa_handle_bind_buffer_gen(struct gl_context *ctx,
                             GLuint buffer,
                             struct gl_buffer_object **buf_handle,
                             const char *caller)
{
   struct gl_buffer_object *buf = *buf_handle;

   if (!buf && (ctx->API == API_OPENGL_CORE)) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(non-gen name)", caller);
      return false;
   }

   if (!buf || buf == &DummyBufferObject) {
      /* If this is a new buffer object id, or one which was generated but
       * never used before, allocate a buffer object now.
       */
      assert(ctx->Driver.NewBufferObject);
      buf = ctx->Driver.NewBufferObject(ctx, buffer);
      if (!buf) {
	 _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s", caller);
	 return false;
      }
      _mesa_HashInsert(ctx->Shared->BufferObjects, buffer, buf);
      *buf_handle = buf;
   }

   return true;
}

/**
 * Bind the specified target to buffer for the specified context.
 * Called by glBindBuffer() and other functions.
 */
static void
bind_buffer_object(struct gl_context *ctx, GLenum target, GLuint buffer)
{
   struct gl_buffer_object *oldBufObj;
   struct gl_buffer_object *newBufObj = NULL;
   struct gl_buffer_object **bindTarget = NULL;

   bindTarget = get_buffer_target(ctx, target);
   if (!bindTarget) {
      _mesa_error(ctx, GL_INVALID_ENUM, "glBindBufferARB(target %s)",
                  _mesa_enum_to_string(target));
      return;
   }

   /* Get pointer to old buffer object (to be unbound) */
   oldBufObj = *bindTarget;
   if (oldBufObj && oldBufObj->Name == buffer && !oldBufObj->DeletePending)
      return;   /* rebinding the same buffer object- no change */

   /*
    * Get pointer to new buffer object (newBufObj)
    */
   if (buffer == 0) {
      /* The spec says there's not a buffer object named 0, but we use
       * one internally because it simplifies things.
       */
      newBufObj = ctx->Shared->NullBufferObj;
   }
   else {
      /* non-default buffer object */
      newBufObj = _mesa_lookup_bufferobj(ctx, buffer);
      if (!_mesa_handle_bind_buffer_gen(ctx, buffer,
                                        &newBufObj, "glBindBuffer"))
         return;
   }

   /* record usage history */
   switch (target) {
   case GL_PIXEL_PACK_BUFFER:
      newBufObj->UsageHistory |= USAGE_PIXEL_PACK_BUFFER;
      break;
   default:
      break;
   }

   /* bind new buffer */
   _mesa_reference_buffer_object(ctx, bindTarget, newBufObj);
}


/**
 * Update the default buffer objects in the given context to reference those
 * specified in the shared state and release those referencing the old
 * shared state.
 */
void
_mesa_update_default_objects_buffer_objects(struct gl_context *ctx)
{
   /* Bind the NullBufferObj to remove references to those
    * in the shared context hash table.
    */
   bind_buffer_object( ctx, GL_ARRAY_BUFFER_ARB, 0);
   bind_buffer_object( ctx, GL_ELEMENT_ARRAY_BUFFER_ARB, 0);
   bind_buffer_object( ctx, GL_PIXEL_PACK_BUFFER_ARB, 0);
   bind_buffer_object( ctx, GL_PIXEL_UNPACK_BUFFER_ARB, 0);
}



/**
 * Return the gl_buffer_object for the given ID.
 * Always return NULL for ID 0.
 */
struct gl_buffer_object *
_mesa_lookup_bufferobj(struct gl_context *ctx, GLuint buffer)
{
   if (buffer == 0)
      return NULL;
   else
      return (struct gl_buffer_object *)
         _mesa_HashLookup(ctx->Shared->BufferObjects, buffer);
}


struct gl_buffer_object *
_mesa_lookup_bufferobj_locked(struct gl_context *ctx, GLuint buffer)
{
   if (buffer == 0)
      return NULL;
   else
      return (struct gl_buffer_object *)
         _mesa_HashLookupLocked(ctx->Shared->BufferObjects, buffer);
}

/**
 * A convenience function for direct state access functions that throws
 * GL_INVALID_OPERATION if buffer is not the name of an existing
 * buffer object.
 */
struct gl_buffer_object *
_mesa_lookup_bufferobj_err(struct gl_context *ctx, GLuint buffer,
                           const char *caller)
{
   struct gl_buffer_object *bufObj;

   bufObj = _mesa_lookup_bufferobj(ctx, buffer);
   if (!bufObj || bufObj == &DummyBufferObject) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(non-existent buffer object %u)", caller, buffer);
      return NULL;
   }

   return bufObj;
}


void
_mesa_begin_bufferobj_lookups(struct gl_context *ctx)
{
   _mesa_HashLockMutex(ctx->Shared->BufferObjects);
}


void
_mesa_end_bufferobj_lookups(struct gl_context *ctx)
{
   _mesa_HashUnlockMutex(ctx->Shared->BufferObjects);
}


/**
 * Look up a buffer object for a multi-bind function.
 *
 * Unlike _mesa_lookup_bufferobj(), this function also takes care
 * of generating an error if the buffer ID is not zero or the name
 * of an existing buffer object.
 *
 * If the buffer ID refers to an existing buffer object, a pointer
 * to the buffer object is returned.  If the ID is zero, a pointer
 * to the shared NullBufferObj is returned.  If the ID is not zero
 * and does not refer to a valid buffer object, this function
 * returns NULL.
 *
 * This function assumes that the caller has already locked the
 * hash table mutex by calling _mesa_begin_bufferobj_lookups().
 */
struct gl_buffer_object *
_mesa_multi_bind_lookup_bufferobj(struct gl_context *ctx,
                                  const GLuint *buffers,
                                  GLuint index, const char *caller)
{
   struct gl_buffer_object *bufObj;

   if (buffers[index] != 0) {
      bufObj = _mesa_lookup_bufferobj_locked(ctx, buffers[index]);

      /* The multi-bind functions don't create the buffer objects
         when they don't exist. */
      if (bufObj == &DummyBufferObject)
         bufObj = NULL;
   } else
      bufObj = ctx->Shared->NullBufferObj;

   if (!bufObj) {
      /* The ARB_multi_bind spec says:
       *
       *    "An INVALID_OPERATION error is generated if any value
       *     in <buffers> is not zero or the name of an existing
       *     buffer object (per binding)."
       */
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(buffers[%u]=%u is not zero or the name "
                  "of an existing buffer object)",
                  caller, index, buffers[index]);
   }

   return bufObj;
}


/**
 * If *ptr points to obj, set ptr = the Null/default buffer object.
 * This is a helper for buffer object deletion.
 * The GL spec says that deleting a buffer object causes it to get
 * unbound from all arrays in the current context.
 */
static void
unbind(struct gl_context *ctx,
       struct gl_buffer_object **ptr,
       struct gl_buffer_object *obj)
{
   if (*ptr == obj) {
      _mesa_reference_buffer_object(ctx, ptr, ctx->Shared->NullBufferObj);
   }
}


/**
 * Plug default/fallback buffer object functions into the device
 * driver hooks.
 */
void
_mesa_init_buffer_object_functions(struct dd_function_table *driver)
{
   /* GL_ARB_vertex/pixel_buffer_object */
   driver->NewBufferObject = _mesa_new_buffer_object;
   driver->DeleteBuffer = _mesa_delete_buffer_object;
   driver->BufferData = buffer_data_fallback;
   driver->BufferSubData = buffer_sub_data_fallback;
   driver->GetBufferSubData = _mesa_buffer_get_subdata;
   driver->UnmapBuffer = unmap_buffer_fallback;

   /* GL_ARB_clear_buffer_object */
   driver->ClearBufferSubData = _mesa_ClearBufferSubData_sw;

   /* GL_ARB_map_buffer_range */
   driver->MapBufferRange = map_buffer_range_fallback;
   driver->FlushMappedBufferRange = flush_mapped_buffer_range_fallback;

   /* GL_ARB_copy_buffer */
   driver->CopyBufferSubData = copy_buffer_sub_data_fallback;
}


void
_mesa_buffer_unmap_all_mappings(struct gl_context *ctx,
                                struct gl_buffer_object *bufObj)
{
   int i;

   for (i = 0; i < MAP_COUNT; i++) {
      if (_mesa_bufferobj_mapped(bufObj, i)) {
         ctx->Driver.UnmapBuffer(ctx, bufObj, i);
         assert(bufObj->Mappings[i].Pointer == NULL);
         bufObj->Mappings[i].AccessFlags = 0;
      }
   }
}


/**********************************************************************/
/* API Functions                                                      */
/**********************************************************************/

void GLAPIENTRY
_mesa_BindBuffer(GLenum target, GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);

   if (MESA_VERBOSE & VERBOSE_API) {
      _mesa_debug(ctx, "glBindBuffer(%s, %u)\n",
                  _mesa_enum_to_string(target), buffer);
   }

   bind_buffer_object(ctx, target, buffer);
}


/**
 * Delete a set of buffer objects.
 *
 * \param n      Number of buffer objects to delete.
 * \param ids    Array of \c n buffer object IDs.
 */
void GLAPIENTRY
_mesa_DeleteBuffers(GLsizei n, const GLuint *ids)
{
   GET_CURRENT_CONTEXT(ctx);
   GLsizei i;
   FLUSH_VERTICES(ctx, 0);

   if (n < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glDeleteBuffersARB(n)");
      return;
   }

   _mesa_HashLockMutex(ctx->Shared->BufferObjects);

   for (i = 0; i < n; i++) {
      struct gl_buffer_object *bufObj =
         _mesa_lookup_bufferobj_locked(ctx, ids[i]);
      if (bufObj) {
         struct gl_vertex_array_object *vao = ctx->Array.VAO;
         GLuint j;

         assert(bufObj->Name == ids[i] || bufObj == &DummyBufferObject);

         _mesa_buffer_unmap_all_mappings(ctx, bufObj);

         /* unbind any vertex pointers bound to this buffer */
         for (j = 0; j < ARRAY_SIZE(vao->VertexBinding); j++) {
            unbind(ctx, &vao->VertexBinding[j].BufferObj, bufObj);
         }

         if (ctx->Array.ArrayBufferObj == bufObj) {
            _mesa_BindBuffer( GL_ARRAY_BUFFER_ARB, 0 );
         }
         if (vao->IndexBufferObj == bufObj) {
            _mesa_BindBuffer( GL_ELEMENT_ARRAY_BUFFER_ARB, 0 );
         }

         /* unbind ARB_draw_indirect binding point */
         if (ctx->DrawIndirectBuffer == bufObj) {
            _mesa_BindBuffer( GL_DRAW_INDIRECT_BUFFER, 0 );
         }

         /* unbind ARB_indirect_parameters binding point */
         if (ctx->ParameterBuffer == bufObj) {
            _mesa_BindBuffer(GL_PARAMETER_BUFFER_ARB, 0);
         }

         /* unbind ARB_compute_shader binding point */
         if (ctx->DispatchIndirectBuffer == bufObj) {
            _mesa_BindBuffer(GL_DISPATCH_INDIRECT_BUFFER, 0);
         }

         /* unbind ARB_copy_buffer binding points */
         if (ctx->CopyReadBuffer == bufObj) {
            _mesa_BindBuffer( GL_COPY_READ_BUFFER, 0 );
         }
         if (ctx->CopyWriteBuffer == bufObj) {
            _mesa_BindBuffer( GL_COPY_WRITE_BUFFER, 0 );
         }

         /* unbind transform feedback binding points */
         if (ctx->TransformFeedback.CurrentBuffer == bufObj) {
            _mesa_BindBuffer( GL_TRANSFORM_FEEDBACK_BUFFER, 0 );
         }
         for (j = 0; j < MAX_FEEDBACK_BUFFERS; j++) {
            if (ctx->TransformFeedback.CurrentObject->Buffers[j] == bufObj) {
               _mesa_BindBufferBase( GL_TRANSFORM_FEEDBACK_BUFFER, j, 0 );
            }
         }

         /* unbind UBO binding points */
         for (j = 0; j < ctx->Const.MaxUniformBufferBindings; j++) {
            if (ctx->UniformBufferBindings[j].BufferObject == bufObj) {
               _mesa_BindBufferBase( GL_UNIFORM_BUFFER, j, 0 );
            }
         }

         if (ctx->UniformBuffer == bufObj) {
            _mesa_BindBuffer( GL_UNIFORM_BUFFER, 0 );
         }

         /* unbind SSBO binding points */
         for (j = 0; j < ctx->Const.MaxShaderStorageBufferBindings; j++) {
            if (ctx->ShaderStorageBufferBindings[j].BufferObject == bufObj) {
               _mesa_BindBufferBase(GL_SHADER_STORAGE_BUFFER, j, 0);
            }
         }

         if (ctx->ShaderStorageBuffer == bufObj) {
            _mesa_BindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
         }

         /* unbind Atomci Buffer binding points */
         for (j = 0; j < ctx->Const.MaxAtomicBufferBindings; j++) {
            if (ctx->AtomicBufferBindings[j].BufferObject == bufObj) {
               _mesa_BindBufferBase( GL_ATOMIC_COUNTER_BUFFER, j, 0 );
            }
         }

         if (ctx->AtomicBuffer == bufObj) {
            _mesa_BindBuffer( GL_ATOMIC_COUNTER_BUFFER, 0 );
         }

         /* unbind any pixel pack/unpack pointers bound to this buffer */
         if (ctx->Pack.BufferObj == bufObj) {
            _mesa_BindBuffer( GL_PIXEL_PACK_BUFFER_EXT, 0 );
         }
         if (ctx->Unpack.BufferObj == bufObj) {
            _mesa_BindBuffer( GL_PIXEL_UNPACK_BUFFER_EXT, 0 );
         }

         if (ctx->Texture.BufferObject == bufObj) {
            _mesa_BindBuffer( GL_TEXTURE_BUFFER, 0 );
         }

         if (ctx->ExternalVirtualMemoryBuffer == bufObj) {
            _mesa_BindBuffer(GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD, 0);
         }

         /* unbind query buffer binding point */
         if (ctx->QueryBuffer == bufObj) {
            _mesa_BindBuffer(GL_QUERY_BUFFER, 0);
         }

         /* The ID is immediately freed for re-use */
         _mesa_HashRemoveLocked(ctx->Shared->BufferObjects, ids[i]);
         /* Make sure we do not run into the classic ABA problem on bind.
          * We don't want to allow re-binding a buffer object that's been
          * "deleted" by glDeleteBuffers().
          *
          * The explicit rebinding to the default object in the current context
          * prevents the above in the current context, but another context
          * sharing the same objects might suffer from this problem.
          * The alternative would be to do the hash lookup in any case on bind
          * which would introduce more runtime overhead than this.
          */
         bufObj->DeletePending = GL_TRUE;
         _mesa_reference_buffer_object(ctx, &bufObj, NULL);
      }
   }

   _mesa_HashUnlockMutex(ctx->Shared->BufferObjects);
}


/**
 * This is the implementation for glGenBuffers and glCreateBuffers. It is not
 * exposed to the rest of Mesa to encourage the use of nameless buffers in
 * driver internals.
 */
static void
create_buffers(GLsizei n, GLuint *buffers, bool dsa)
{
   GET_CURRENT_CONTEXT(ctx);
   GLuint first;
   GLint i;
   struct gl_buffer_object *buf;

   const char *func = dsa ? "glCreateBuffers" : "glGenBuffers";

   if (MESA_VERBOSE & VERBOSE_API)
      _mesa_debug(ctx, "%s(%d)\n", func, n);

   if (n < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(n %d < 0)", func, n);
      return;
   }

   if (!buffers) {
      return;
   }

   /*
    * This must be atomic (generation and allocation of buffer object IDs)
    */
   _mesa_HashLockMutex(ctx->Shared->BufferObjects);

   first = _mesa_HashFindFreeKeyBlock(ctx->Shared->BufferObjects, n);

   /* Insert the ID and pointer into the hash table. If non-DSA, insert a
    * DummyBufferObject.  Otherwise, create a new buffer object and insert
    * it.
    */
   for (i = 0; i < n; i++) {
      buffers[i] = first + i;
      if (dsa) {
         assert(ctx->Driver.NewBufferObject);
         buf = ctx->Driver.NewBufferObject(ctx, buffers[i]);
         if (!buf) {
            _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s", func);
            _mesa_HashUnlockMutex(ctx->Shared->BufferObjects);
            return;
         }
      }
      else
         buf = &DummyBufferObject;

      _mesa_HashInsertLocked(ctx->Shared->BufferObjects, buffers[i], buf);
   }

   _mesa_HashUnlockMutex(ctx->Shared->BufferObjects);
}

/**
 * Generate a set of unique buffer object IDs and store them in \c buffers.
 *
 * \param n        Number of IDs to generate.
 * \param buffers  Array of \c n locations to store the IDs.
 */
void GLAPIENTRY
_mesa_GenBuffers(GLsizei n, GLuint *buffers)
{
   create_buffers(n, buffers, false);
}

/**
 * Create a set of buffer objects and store their unique IDs in \c buffers.
 *
 * \param n        Number of IDs to generate.
 * \param buffers  Array of \c n locations to store the IDs.
 */
void GLAPIENTRY
_mesa_CreateBuffers(GLsizei n, GLuint *buffers)
{
   create_buffers(n, buffers, true);
}


/**
 * Determine if ID is the name of a buffer object.
 *
 * \param id  ID of the potential buffer object.
 * \return  \c GL_TRUE if \c id is the name of a buffer object,
 *          \c GL_FALSE otherwise.
 */
GLboolean GLAPIENTRY
_mesa_IsBuffer(GLuint id)
{
   struct gl_buffer_object *bufObj;
   GET_CURRENT_CONTEXT(ctx);
   ASSERT_OUTSIDE_BEGIN_END_WITH_RETVAL(ctx, GL_FALSE);

   bufObj = _mesa_lookup_bufferobj(ctx, id);

   return bufObj && bufObj != &DummyBufferObject;
}


void
_mesa_buffer_storage(struct gl_context *ctx, struct gl_buffer_object *bufObj,
                     GLenum target, GLsizeiptr size, const GLvoid *data,
                     GLbitfield flags, const char *func)
{
   if (size <= 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(size <= 0)", func);
      return;
   }

   if (flags & ~(GL_MAP_READ_BIT |
                 GL_MAP_WRITE_BIT |
                 GL_MAP_PERSISTENT_BIT |
                 GL_MAP_COHERENT_BIT |
                 GL_DYNAMIC_STORAGE_BIT |
                 GL_CLIENT_STORAGE_BIT)) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(invalid flag bits set)", func);
      return;
   }

   if (flags & GL_MAP_PERSISTENT_BIT &&
       !(flags & (GL_MAP_READ_BIT | GL_MAP_WRITE_BIT))) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(PERSISTENT and flags!=READ/WRITE)", func);
      return;
   }

   if (flags & GL_MAP_COHERENT_BIT && !(flags & GL_MAP_PERSISTENT_BIT)) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(COHERENT and flags!=PERSISTENT)", func);
      return;
   }

   if (bufObj->Immutable) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(immutable)", func);
      return;
   }

   /* Unmap the existing buffer.  We'll replace it now.  Not an error. */
   _mesa_buffer_unmap_all_mappings(ctx, bufObj);

   FLUSH_VERTICES(ctx, _NEW_BUFFER_OBJECT);

   bufObj->Written = GL_TRUE;
   bufObj->Immutable = GL_TRUE;
   bufObj->MinMaxCacheDirty = true;

   assert(ctx->Driver.BufferData);
   if (!ctx->Driver.BufferData(ctx, target, size, data, GL_DYNAMIC_DRAW,
                               flags, bufObj)) {
      if (target == GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD) {
         /* Even though the interaction between AMD_pinned_memory and
          * glBufferStorage is not described in the spec, Graham Sellers
          * said that it should behave the same as glBufferData.
          */
         _mesa_error(ctx, GL_INVALID_OPERATION, "%s", func);
      }
      else {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s", func);
      }
   }
}

void GLAPIENTRY
_mesa_BufferStorage(GLenum target, GLsizeiptr size, const GLvoid *data,
                    GLbitfield flags)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   bufObj = get_buffer(ctx, "glBufferStorage", target, GL_INVALID_OPERATION);
   if (!bufObj)
      return;

   _mesa_buffer_storage(ctx, bufObj, target, size, data, flags,
                        "glBufferStorage");
}

void GLAPIENTRY
_mesa_NamedBufferStorage(GLuint buffer, GLsizeiptr size, const GLvoid *data,
                         GLbitfield flags)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   bufObj = _mesa_lookup_bufferobj_err(ctx, buffer, "glNamedBufferStorage");
   if (!bufObj)
      return;

   /*
    * In direct state access, buffer objects have an unspecified target since
    * they are not required to be bound.
    */
   _mesa_buffer_storage(ctx, bufObj, GL_NONE, size, data, flags,
                        "glNamedBufferStorage");
}


void
_mesa_buffer_data(struct gl_context *ctx, struct gl_buffer_object *bufObj,
                  GLenum target, GLsizeiptr size, const GLvoid *data,
                  GLenum usage, const char *func)
{
   bool valid_usage;

   if (MESA_VERBOSE & VERBOSE_API) {
      _mesa_debug(ctx, "%s(%s, %ld, %p, %s)\n",
                  func,
                  _mesa_enum_to_string(target),
                  (long int) size, data,
                  _mesa_enum_to_string(usage));
   }

   if (size < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(size < 0)", func);
      return;
   }

   switch (usage) {
   case GL_STREAM_DRAW_ARB:
      valid_usage = (ctx->API != API_OPENGLES);
      break;

   case GL_STATIC_DRAW_ARB:
   case GL_DYNAMIC_DRAW_ARB:
      valid_usage = true;
      break;

   case GL_STREAM_READ_ARB:
   case GL_STREAM_COPY_ARB:
   case GL_STATIC_READ_ARB:
   case GL_STATIC_COPY_ARB:
   case GL_DYNAMIC_READ_ARB:
   case GL_DYNAMIC_COPY_ARB:
      valid_usage = _mesa_is_desktop_gl(ctx) || _mesa_is_gles3(ctx);
      break;

   default:
      valid_usage = false;
      break;
   }

   if (!valid_usage) {
      _mesa_error(ctx, GL_INVALID_ENUM, "%s(invalid usage: %s)", func,
                  _mesa_enum_to_string(usage));
      return;
   }

   if (bufObj->Immutable) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(immutable)", func);
      return;
   }

   /* Unmap the existing buffer.  We'll replace it now.  Not an error. */
   _mesa_buffer_unmap_all_mappings(ctx, bufObj);

   FLUSH_VERTICES(ctx, _NEW_BUFFER_OBJECT);

   bufObj->Written = GL_TRUE;
   bufObj->MinMaxCacheDirty = true;

#ifdef VBO_DEBUG
   printf("glBufferDataARB(%u, sz %ld, from %p, usage 0x%x)\n",
                bufObj->Name, size, data, usage);
#endif

#ifdef BOUNDS_CHECK
   size += 100;
#endif

   assert(ctx->Driver.BufferData);
   if (!ctx->Driver.BufferData(ctx, target, size, data, usage,
                               GL_MAP_READ_BIT |
                               GL_MAP_WRITE_BIT |
                               GL_DYNAMIC_STORAGE_BIT,
                               bufObj)) {
      if (target == GL_EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD) {
         /* From GL_AMD_pinned_memory:
          *
          *   INVALID_OPERATION is generated by BufferData if <target> is
          *   EXTERNAL_VIRTUAL_MEMORY_BUFFER_AMD, and the store cannot be
          *   mapped to the GPU address space.
          */
         _mesa_error(ctx, GL_INVALID_OPERATION, "%s", func);
      }
      else {
         _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s", func);
      }
   }
}

void GLAPIENTRY
_mesa_BufferData(GLenum target, GLsizeiptr size,
                 const GLvoid *data, GLenum usage)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   bufObj = get_buffer(ctx, "glBufferData", target, GL_INVALID_OPERATION);
   if (!bufObj)
      return;

   _mesa_buffer_data(ctx, bufObj, target, size, data, usage,
                     "glBufferData");
}

void GLAPIENTRY
_mesa_NamedBufferData(GLuint buffer, GLsizeiptr size, const GLvoid *data,
                      GLenum usage)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   bufObj = _mesa_lookup_bufferobj_err(ctx, buffer, "glNamedBufferData");
   if (!bufObj)
      return;

   /* In direct state access, buffer objects have an unspecified target since
    * they are not required to be bound.
    */
   _mesa_buffer_data(ctx, bufObj, GL_NONE, size, data, usage,
                     "glNamedBufferData");
}


/**
 * Implementation for glBufferSubData and glNamedBufferSubData.
 *
 * \param ctx     GL context.
 * \param bufObj  The buffer object.
 * \param offset  Offset of the first byte of the subdata range.
 * \param size    Size, in bytes, of the subdata range.
 * \param data    The data store.
 * \param func  Name of calling function for recording errors.
 *
 */
void
_mesa_buffer_sub_data(struct gl_context *ctx, struct gl_buffer_object *bufObj,
                      GLintptr offset, GLsizeiptr size, const GLvoid *data,
                      const char *func)
{
   if (!buffer_object_subdata_range_good(ctx, bufObj, offset, size,
                                         true, func)) {
      /* error already recorded */
      return;
   }

   if (bufObj->Immutable &&
       !(bufObj->StorageFlags & GL_DYNAMIC_STORAGE_BIT)) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s", func);
      return;
   }

   if (size == 0)
      return;

   bufObj->NumSubDataCalls++;

   if ((bufObj->Usage == GL_STATIC_DRAW ||
        bufObj->Usage == GL_STATIC_COPY) &&
       bufObj->NumSubDataCalls >= BUFFER_WARNING_CALL_COUNT) {
      /* If the application declared the buffer as static draw/copy or stream
       * draw, it should not be frequently modified with glBufferSubData.
       */
      BUFFER_USAGE_WARNING(ctx,
                           "using %s(buffer %u, offset %u, size %u) to "
                           "update a %s buffer",
                           func, bufObj->Name, offset, size,
                           _mesa_enum_to_string(bufObj->Usage));
   }

   bufObj->Written = GL_TRUE;
   bufObj->MinMaxCacheDirty = true;

   assert(ctx->Driver.BufferSubData);
   ctx->Driver.BufferSubData(ctx, offset, size, data, bufObj);
}

void GLAPIENTRY
_mesa_BufferSubData(GLenum target, GLintptr offset,
                    GLsizeiptr size, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   bufObj = get_buffer(ctx, "glBufferSubData", target, GL_INVALID_OPERATION);
   if (!bufObj)
      return;

   _mesa_buffer_sub_data(ctx, bufObj, offset, size, data, "glBufferSubData");
}

void GLAPIENTRY
_mesa_NamedBufferSubData(GLuint buffer, GLintptr offset,
                         GLsizeiptr size, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   bufObj = _mesa_lookup_bufferobj_err(ctx, buffer, "glNamedBufferSubData");
   if (!bufObj)
      return;

   _mesa_buffer_sub_data(ctx, bufObj, offset, size, data,
                         "glNamedBufferSubData");
}


void GLAPIENTRY
_mesa_GetBufferSubData(GLenum target, GLintptr offset,
                       GLsizeiptr size, GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   bufObj = get_buffer(ctx, "glGetBufferSubData", target,
                       GL_INVALID_OPERATION);
   if (!bufObj)
      return;

   if (!buffer_object_subdata_range_good(ctx, bufObj, offset, size, false,
                                         "glGetBufferSubData")) {
      return;
   }

   assert(ctx->Driver.GetBufferSubData);
   ctx->Driver.GetBufferSubData(ctx, offset, size, data, bufObj);
}

void GLAPIENTRY
_mesa_GetNamedBufferSubData(GLuint buffer, GLintptr offset,
                            GLsizeiptr size, GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   bufObj = _mesa_lookup_bufferobj_err(ctx, buffer,
                                       "glGetNamedBufferSubData");
   if (!bufObj)
      return;

   if (!buffer_object_subdata_range_good(ctx, bufObj, offset, size, false,
                                         "glGetNamedBufferSubData")) {
      return;
   }

   assert(ctx->Driver.GetBufferSubData);
   ctx->Driver.GetBufferSubData(ctx, offset, size, data, bufObj);
}


/**
 * \param subdata   true if caller is *SubData, false if *Data
 */
void
_mesa_clear_buffer_sub_data(struct gl_context *ctx,
                            struct gl_buffer_object *bufObj,
                            GLenum internalformat,
                            GLintptr offset, GLsizeiptr size,
                            GLenum format, GLenum type,
                            const GLvoid *data,
                            const char *func, bool subdata)
{
   mesa_format mesaFormat;
   GLubyte clearValue[MAX_PIXEL_BYTES];
   GLsizeiptr clearValueSize;

   /* This checks for disallowed mappings. */
   if (!buffer_object_subdata_range_good(ctx, bufObj, offset, size,
                                         subdata, func)) {
      return;
   }

   mesaFormat = validate_clear_buffer_format(ctx, internalformat,
                                             format, type, func);

   if (mesaFormat == MESA_FORMAT_NONE) {
      return;
   }

   clearValueSize = _mesa_get_format_bytes(mesaFormat);
   if (offset % clearValueSize != 0 || size % clearValueSize != 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(offset or size is not a multiple of "
                  "internalformat size)", func);
      return;
   }

   /* Bail early. Negative size has already been checked. */
   if (size == 0)
      return;

   bufObj->MinMaxCacheDirty = true;

   if (data == NULL) {
      /* clear to zeros, per the spec */
      ctx->Driver.ClearBufferSubData(ctx, offset, size,
                                     NULL, clearValueSize, bufObj);
      return;
   }

   if (!convert_clear_buffer_data(ctx, mesaFormat, clearValue,
                                  format, type, data, func)) {
      return;
   }

   ctx->Driver.ClearBufferSubData(ctx, offset, size,
                                  clearValue, clearValueSize, bufObj);
}

void GLAPIENTRY
_mesa_ClearBufferData(GLenum target, GLenum internalformat, GLenum format,
                      GLenum type, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   bufObj = get_buffer(ctx, "glClearBufferData", target, GL_INVALID_VALUE);
   if (!bufObj)
      return;

   _mesa_clear_buffer_sub_data(ctx, bufObj, internalformat, 0, bufObj->Size,
                               format, type, data,
                               "glClearBufferData", false);
}

void GLAPIENTRY
_mesa_ClearNamedBufferData(GLuint buffer, GLenum internalformat,
                           GLenum format, GLenum type, const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   bufObj = _mesa_lookup_bufferobj_err(ctx, buffer, "glClearNamedBufferData");
   if (!bufObj)
      return;

   _mesa_clear_buffer_sub_data(ctx, bufObj, internalformat, 0, bufObj->Size,
                               format, type, data,
                               "glClearNamedBufferData", false);
}


void GLAPIENTRY
_mesa_ClearBufferSubData(GLenum target, GLenum internalformat,
                         GLintptr offset, GLsizeiptr size,
                         GLenum format, GLenum type,
                         const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   bufObj = get_buffer(ctx, "glClearBufferSubData", target, GL_INVALID_VALUE);
   if (!bufObj)
      return;

   _mesa_clear_buffer_sub_data(ctx, bufObj, internalformat, offset, size,
                               format, type, data,
                               "glClearBufferSubData", true);
}

void GLAPIENTRY
_mesa_ClearNamedBufferSubData(GLuint buffer, GLenum internalformat,
                              GLintptr offset, GLsizeiptr size,
                              GLenum format, GLenum type,
                              const GLvoid *data)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   bufObj = _mesa_lookup_bufferobj_err(ctx, buffer,
                                       "glClearNamedBufferSubData");
   if (!bufObj)
      return;

   _mesa_clear_buffer_sub_data(ctx, bufObj, internalformat, offset, size,
                               format, type, data,
                               "glClearNamedBufferSubData", true);
}


GLboolean
_mesa_unmap_buffer(struct gl_context *ctx, struct gl_buffer_object *bufObj,
                   const char *func)
{
   GLboolean status = GL_TRUE;
   ASSERT_OUTSIDE_BEGIN_END_WITH_RETVAL(ctx, GL_FALSE);

   if (!_mesa_bufferobj_mapped(bufObj, MAP_USER)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(buffer is not mapped)", func);
      return GL_FALSE;
   }

#ifdef BOUNDS_CHECK
   if (bufObj->Access != GL_READ_ONLY_ARB) {
      GLubyte *buf = (GLubyte *) bufObj->Pointer;
      GLuint i;
      /* check that last 100 bytes are still = magic value */
      for (i = 0; i < 100; i++) {
         GLuint pos = bufObj->Size - i - 1;
         if (buf[pos] != 123) {
            _mesa_warning(ctx, "Out of bounds buffer object write detected"
                          " at position %d (value = %u)\n",
                          pos, buf[pos]);
         }
      }
   }
#endif

#ifdef VBO_DEBUG
   if (bufObj->AccessFlags & GL_MAP_WRITE_BIT) {
      GLuint i, unchanged = 0;
      GLubyte *b = (GLubyte *) bufObj->Pointer;
      GLint pos = -1;
      /* check which bytes changed */
      for (i = 0; i < bufObj->Size - 1; i++) {
         if (b[i] == (i & 0xff) && b[i+1] == ((i+1) & 0xff)) {
            unchanged++;
            if (pos == -1)
               pos = i;
         }
      }
      if (unchanged) {
         printf("glUnmapBufferARB(%u): %u of %ld unchanged, starting at %d\n",
                      bufObj->Name, unchanged, bufObj->Size, pos);
      }
   }
#endif

   status = ctx->Driver.UnmapBuffer(ctx, bufObj, MAP_USER);
   bufObj->Mappings[MAP_USER].AccessFlags = 0;
   assert(bufObj->Mappings[MAP_USER].Pointer == NULL);
   assert(bufObj->Mappings[MAP_USER].Offset == 0);
   assert(bufObj->Mappings[MAP_USER].Length == 0);

   return status;
}

GLboolean GLAPIENTRY
_mesa_UnmapBuffer(GLenum target)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   bufObj = get_buffer(ctx, "glUnmapBuffer", target, GL_INVALID_OPERATION);
   if (!bufObj)
      return GL_FALSE;

   return _mesa_unmap_buffer(ctx, bufObj, "glUnmapBuffer");
}

GLboolean GLAPIENTRY
_mesa_UnmapNamedBuffer(GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   bufObj = _mesa_lookup_bufferobj_err(ctx, buffer, "glUnmapNamedBuffer");
   if (!bufObj)
      return GL_FALSE;

   return _mesa_unmap_buffer(ctx, bufObj, "glUnmapNamedBuffer");
}


static bool
get_buffer_parameter(struct gl_context *ctx,
                     struct gl_buffer_object *bufObj, GLenum pname,
                     GLint64 *params, const char *func)
{
   switch (pname) {
   case GL_BUFFER_SIZE_ARB:
      *params = bufObj->Size;
      break;
   case GL_BUFFER_USAGE_ARB:
      *params = bufObj->Usage;
      break;
   case GL_BUFFER_ACCESS_ARB:
      *params = simplified_access_mode(ctx,
                            bufObj->Mappings[MAP_USER].AccessFlags);
      break;
   case GL_BUFFER_MAPPED_ARB:
      *params = _mesa_bufferobj_mapped(bufObj, MAP_USER);
      break;
   case GL_BUFFER_ACCESS_FLAGS:
      if (!ctx->Extensions.ARB_map_buffer_range)
         goto invalid_pname;
      *params = bufObj->Mappings[MAP_USER].AccessFlags;
      break;
   case GL_BUFFER_MAP_OFFSET:
      if (!ctx->Extensions.ARB_map_buffer_range)
         goto invalid_pname;
      *params = bufObj->Mappings[MAP_USER].Offset;
      break;
   case GL_BUFFER_MAP_LENGTH:
      if (!ctx->Extensions.ARB_map_buffer_range)
         goto invalid_pname;
      *params = bufObj->Mappings[MAP_USER].Length;
      break;
   case GL_BUFFER_IMMUTABLE_STORAGE:
      if (!ctx->Extensions.ARB_buffer_storage)
         goto invalid_pname;
      *params = bufObj->Immutable;
      break;
   case GL_BUFFER_STORAGE_FLAGS:
      if (!ctx->Extensions.ARB_buffer_storage)
         goto invalid_pname;
      *params = bufObj->StorageFlags;
      break;
   default:
      goto invalid_pname;
   }

   return true;

invalid_pname:
   _mesa_error(ctx, GL_INVALID_ENUM, "%s(invalid pname: %s)", func,
               _mesa_enum_to_string(pname));
   return false;
}

void GLAPIENTRY
_mesa_GetBufferParameteriv(GLenum target, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;
   GLint64 parameter;

   bufObj = get_buffer(ctx, "glGetBufferParameteriv", target,
                       GL_INVALID_OPERATION);
   if (!bufObj)
      return;

   if (!get_buffer_parameter(ctx, bufObj, pname, &parameter,
                             "glGetBufferParameteriv"))
      return; /* Error already recorded. */

   *params = (GLint) parameter;
}

void GLAPIENTRY
_mesa_GetBufferParameteri64v(GLenum target, GLenum pname, GLint64 *params)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;
   GLint64 parameter;

   bufObj = get_buffer(ctx, "glGetBufferParameteri64v", target,
                       GL_INVALID_OPERATION);
   if (!bufObj)
      return;

   if (!get_buffer_parameter(ctx, bufObj, pname, &parameter,
                             "glGetBufferParameteri64v"))
      return; /* Error already recorded. */

   *params = parameter;
}

void GLAPIENTRY
_mesa_GetNamedBufferParameteriv(GLuint buffer, GLenum pname, GLint *params)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;
   GLint64 parameter;

   bufObj = _mesa_lookup_bufferobj_err(ctx, buffer,
                                       "glGetNamedBufferParameteriv");
   if (!bufObj)
      return;

   if (!get_buffer_parameter(ctx, bufObj, pname, &parameter,
                             "glGetNamedBufferParameteriv"))
      return; /* Error already recorded. */

   *params = (GLint) parameter;
}

void GLAPIENTRY
_mesa_GetNamedBufferParameteri64v(GLuint buffer, GLenum pname,
                                  GLint64 *params)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;
   GLint64 parameter;

   bufObj = _mesa_lookup_bufferobj_err(ctx, buffer,
                                       "glGetNamedBufferParameteri64v");
   if (!bufObj)
      return;

   if (!get_buffer_parameter(ctx, bufObj, pname, &parameter,
                             "glGetNamedBufferParameteri64v"))
      return; /* Error already recorded. */

   *params = parameter;
}


void GLAPIENTRY
_mesa_GetBufferPointerv(GLenum target, GLenum pname, GLvoid **params)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   if (pname != GL_BUFFER_MAP_POINTER) {
      _mesa_error(ctx, GL_INVALID_ENUM, "glGetBufferPointerv(pname != "
                  "GL_BUFFER_MAP_POINTER)");
      return;
   }

   bufObj = get_buffer(ctx, "glGetBufferPointerv", target,
                       GL_INVALID_OPERATION);
   if (!bufObj)
      return;

   *params = bufObj->Mappings[MAP_USER].Pointer;
}

void GLAPIENTRY
_mesa_GetNamedBufferPointerv(GLuint buffer, GLenum pname, GLvoid **params)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   if (pname != GL_BUFFER_MAP_POINTER) {
      _mesa_error(ctx, GL_INVALID_ENUM, "glGetNamedBufferPointerv(pname != "
                  "GL_BUFFER_MAP_POINTER)");
      return;
   }

   bufObj = _mesa_lookup_bufferobj_err(ctx, buffer,
                                       "glGetNamedBufferPointerv");
   if (!bufObj)
      return;

   *params = bufObj->Mappings[MAP_USER].Pointer;
}


void
_mesa_copy_buffer_sub_data(struct gl_context *ctx,
                           struct gl_buffer_object *src,
                           struct gl_buffer_object *dst,
                           GLintptr readOffset, GLintptr writeOffset,
                           GLsizeiptr size, const char *func)
{
   if (_mesa_check_disallowed_mapping(src)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(readBuffer is mapped)", func);
      return;
   }

   if (_mesa_check_disallowed_mapping(dst)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(writeBuffer is mapped)", func);
      return;
   }

   if (readOffset < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(readOffset %d < 0)", func, (int) readOffset);
      return;
   }

   if (writeOffset < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(writeOffset %d < 0)", func, (int) writeOffset);
      return;
   }

   if (size < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(size %d < 0)", func, (int) size);
      return;
   }

   if (readOffset + size > src->Size) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(readOffset %d + size %d > src_buffer_size %d)", func,
                  (int) readOffset, (int) size, (int) src->Size);
      return;
   }

   if (writeOffset + size > dst->Size) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(writeOffset %d + size %d > dst_buffer_size %d)", func,
                  (int) writeOffset, (int) size, (int) dst->Size);
      return;
   }

   if (src == dst) {
      if (readOffset + size <= writeOffset) {
         /* OK */
      }
      else if (writeOffset + size <= readOffset) {
         /* OK */
      }
      else {
         /* overlapping src/dst is illegal */
         _mesa_error(ctx, GL_INVALID_VALUE,
                     "%s(overlapping src/dst)", func);
         return;
      }
   }

   dst->MinMaxCacheDirty = true;

   ctx->Driver.CopyBufferSubData(ctx, src, dst, readOffset, writeOffset, size);
}

void GLAPIENTRY
_mesa_CopyBufferSubData(GLenum readTarget, GLenum writeTarget,
                        GLintptr readOffset, GLintptr writeOffset,
                        GLsizeiptr size)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *src, *dst;

   src = get_buffer(ctx, "glCopyBufferSubData", readTarget,
                    GL_INVALID_OPERATION);
   if (!src)
      return;

   dst = get_buffer(ctx, "glCopyBufferSubData", writeTarget,
                    GL_INVALID_OPERATION);
   if (!dst)
      return;

   _mesa_copy_buffer_sub_data(ctx, src, dst, readOffset, writeOffset, size,
                              "glCopyBufferSubData");
}

void GLAPIENTRY
_mesa_CopyNamedBufferSubData(GLuint readBuffer, GLuint writeBuffer,
                             GLintptr readOffset, GLintptr writeOffset,
                             GLsizeiptr size)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *src, *dst;

   src = _mesa_lookup_bufferobj_err(ctx, readBuffer,
                                    "glCopyNamedBufferSubData");
   if (!src)
      return;

   dst = _mesa_lookup_bufferobj_err(ctx, writeBuffer,
                                    "glCopyNamedBufferSubData");
   if (!dst)
      return;

   _mesa_copy_buffer_sub_data(ctx, src, dst, readOffset, writeOffset, size,
                              "glCopyNamedBufferSubData");
}


void *
_mesa_map_buffer_range(struct gl_context *ctx,
                       struct gl_buffer_object *bufObj,
                       GLintptr offset, GLsizeiptr length,
                       GLbitfield access, const char *func)
{
   void *map;
   GLbitfield allowed_access;

   ASSERT_OUTSIDE_BEGIN_END_WITH_RETVAL(ctx, NULL);

   if (offset < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(offset %ld < 0)", func, (long) offset);
      return NULL;
   }

   if (length < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(length %ld < 0)", func, (long) length);
      return NULL;
   }

   /* Page 38 of the PDF of the OpenGL ES 3.0 spec says:
    *
    *     "An INVALID_OPERATION error is generated for any of the following
    *     conditions:
    *
    *     * <length> is zero."
    *
    * Additionally, page 94 of the PDF of the OpenGL 4.5 core spec
    * (30.10.2014) also says this, so it's no longer allowed for desktop GL,
    * either.
    */
   if (length == 0) {
      _mesa_error(ctx, GL_INVALID_OPERATION, "%s(length = 0)", func);
      return NULL;
   }

   allowed_access = GL_MAP_READ_BIT |
                    GL_MAP_WRITE_BIT |
                    GL_MAP_INVALIDATE_RANGE_BIT |
                    GL_MAP_INVALIDATE_BUFFER_BIT |
                    GL_MAP_FLUSH_EXPLICIT_BIT |
                    GL_MAP_UNSYNCHRONIZED_BIT;

   if (ctx->Extensions.ARB_buffer_storage) {
         allowed_access |= GL_MAP_PERSISTENT_BIT |
                           GL_MAP_COHERENT_BIT;
   }

   if (access & ~allowed_access) {
      /* generate an error if any bits other than those allowed are set */
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(access has undefined bits set)", func);
      return NULL;
   }

   if ((access & (GL_MAP_READ_BIT | GL_MAP_WRITE_BIT)) == 0) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(access indicates neither read or write)", func);
      return NULL;
   }

   if ((access & GL_MAP_READ_BIT) &&
       (access & (GL_MAP_INVALIDATE_RANGE_BIT |
                  GL_MAP_INVALIDATE_BUFFER_BIT |
                  GL_MAP_UNSYNCHRONIZED_BIT))) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(read access with disallowed bits)", func);
      return NULL;
   }

   if ((access & GL_MAP_FLUSH_EXPLICIT_BIT) &&
       ((access & GL_MAP_WRITE_BIT) == 0)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(access has flush explicit without write)", func);
      return NULL;
   }

   if (access & GL_MAP_READ_BIT &&
       !(bufObj->StorageFlags & GL_MAP_READ_BIT)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(buffer does not allow read access)", func);
      return NULL;
   }

   if (access & GL_MAP_WRITE_BIT &&
       !(bufObj->StorageFlags & GL_MAP_WRITE_BIT)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(buffer does not allow write access)", func);
      return NULL;
   }

   if (access & GL_MAP_COHERENT_BIT &&
       !(bufObj->StorageFlags & GL_MAP_COHERENT_BIT)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(buffer does not allow coherent access)", func);
      return NULL;
   }

   if (access & GL_MAP_PERSISTENT_BIT &&
       !(bufObj->StorageFlags & GL_MAP_PERSISTENT_BIT)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(buffer does not allow persistent access)", func);
      return NULL;
   }

   if (offset + length > bufObj->Size) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(offset %lu + length %lu > buffer_size %lu)", func,
                  (unsigned long) offset, (unsigned long) length,
                  (unsigned long) bufObj->Size);
      return NULL;
   }

   if (_mesa_bufferobj_mapped(bufObj, MAP_USER)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(buffer already mapped)", func);
      return NULL;
   }

   if (!bufObj->Size) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s(buffer size = 0)", func);
      return NULL;
   }

   if (access & GL_MAP_WRITE_BIT) {
      bufObj->NumMapBufferWriteCalls++;
      if ((bufObj->Usage == GL_STATIC_DRAW ||
           bufObj->Usage == GL_STATIC_COPY) &&
          bufObj->NumMapBufferWriteCalls >= BUFFER_WARNING_CALL_COUNT) {
         BUFFER_USAGE_WARNING(ctx,
                              "using %s(buffer %u, offset %u, length %u) to "
                              "update a %s buffer",
                              func, bufObj->Name, offset, length,
                              _mesa_enum_to_string(bufObj->Usage));
      }
   }

   assert(ctx->Driver.MapBufferRange);
   map = ctx->Driver.MapBufferRange(ctx, offset, length, access, bufObj,
                                    MAP_USER);
   if (!map) {
      _mesa_error(ctx, GL_OUT_OF_MEMORY, "%s(map failed)", func);
   }
   else {
      /* The driver callback should have set all these fields.
       * This is important because other modules (like VBO) might call
       * the driver function directly.
       */
      assert(bufObj->Mappings[MAP_USER].Pointer == map);
      assert(bufObj->Mappings[MAP_USER].Length == length);
      assert(bufObj->Mappings[MAP_USER].Offset == offset);
      assert(bufObj->Mappings[MAP_USER].AccessFlags == access);
   }

   if (access & GL_MAP_WRITE_BIT) {
      bufObj->Written = GL_TRUE;
      bufObj->MinMaxCacheDirty = true;
   }

#ifdef VBO_DEBUG
   if (strstr(func, "Range") == NULL) { /* If not MapRange */
      printf("glMapBuffer(%u, sz %ld, access 0x%x)\n",
            bufObj->Name, bufObj->Size, access);
      /* Access must be write only */
      if ((access & GL_MAP_WRITE_BIT) && (!(access & ~GL_MAP_WRITE_BIT))) {
         GLuint i;
         GLubyte *b = (GLubyte *) bufObj->Pointer;
         for (i = 0; i < bufObj->Size; i++)
            b[i] = i & 0xff;
      }
   }
#endif

#ifdef BOUNDS_CHECK
   if (strstr(func, "Range") == NULL) { /* If not MapRange */
      GLubyte *buf = (GLubyte *) bufObj->Pointer;
      GLuint i;
      /* buffer is 100 bytes larger than requested, fill with magic value */
      for (i = 0; i < 100; i++) {
         buf[bufObj->Size - i - 1] = 123;
      }
   }
#endif

   return map;
}

void * GLAPIENTRY
_mesa_MapBufferRange(GLenum target, GLintptr offset, GLsizeiptr length,
                     GLbitfield access)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   if (!ctx->Extensions.ARB_map_buffer_range) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glMapBufferRange(ARB_map_buffer_range not supported)");
      return NULL;
   }

   bufObj = get_buffer(ctx, "glMapBufferRange", target, GL_INVALID_OPERATION);
   if (!bufObj)
      return NULL;

   return _mesa_map_buffer_range(ctx, bufObj, offset, length, access,
                                 "glMapBufferRange");
}

void * GLAPIENTRY
_mesa_MapNamedBufferRange(GLuint buffer, GLintptr offset, GLsizeiptr length,
                          GLbitfield access)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   if (!ctx->Extensions.ARB_map_buffer_range) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glMapNamedBufferRange("
                  "ARB_map_buffer_range not supported)");
      return NULL;
   }

   bufObj = _mesa_lookup_bufferobj_err(ctx, buffer, "glMapNamedBufferRange");
   if (!bufObj)
      return NULL;

   return _mesa_map_buffer_range(ctx, bufObj, offset, length, access,
                                 "glMapNamedBufferRange");
}

/**
 * Converts GLenum access from MapBuffer and MapNamedBuffer into
 * flags for input to _mesa_map_buffer_range.
 *
 * \return true if the type of requested access is permissible.
 */
static bool
get_map_buffer_access_flags(struct gl_context *ctx, GLenum access,
                            GLbitfield *flags)
{
   switch (access) {
   case GL_READ_ONLY_ARB:
      *flags = GL_MAP_READ_BIT;
      return _mesa_is_desktop_gl(ctx);
   case GL_WRITE_ONLY_ARB:
      *flags = GL_MAP_WRITE_BIT;
      return true;
   case GL_READ_WRITE_ARB:
      *flags = GL_MAP_READ_BIT | GL_MAP_WRITE_BIT;
      return _mesa_is_desktop_gl(ctx);
   default:
      return false;
   }
}

void * GLAPIENTRY
_mesa_MapBuffer(GLenum target, GLenum access)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;
   GLbitfield accessFlags;

   if (!get_map_buffer_access_flags(ctx, access, &accessFlags)) {
      _mesa_error(ctx, GL_INVALID_ENUM, "glMapBuffer(invalid access)");
      return NULL;
   }

   bufObj = get_buffer(ctx, "glMapBuffer", target, GL_INVALID_OPERATION);
   if (!bufObj)
      return NULL;

   return _mesa_map_buffer_range(ctx, bufObj, 0, bufObj->Size, accessFlags,
                                 "glMapBuffer");
}

void * GLAPIENTRY
_mesa_MapNamedBuffer(GLuint buffer, GLenum access)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;
   GLbitfield accessFlags;

   if (!get_map_buffer_access_flags(ctx, access, &accessFlags)) {
      _mesa_error(ctx, GL_INVALID_ENUM, "glMapNamedBuffer(invalid access)");
      return NULL;
   }

   bufObj = _mesa_lookup_bufferobj_err(ctx, buffer, "glMapNamedBuffer");
   if (!bufObj)
      return NULL;

   return _mesa_map_buffer_range(ctx, bufObj, 0, bufObj->Size, accessFlags,
                                 "glMapNamedBuffer");
}


void
_mesa_flush_mapped_buffer_range(struct gl_context *ctx,
                                struct gl_buffer_object *bufObj,
                                GLintptr offset, GLsizeiptr length,
                                const char *func)
{
   if (!ctx->Extensions.ARB_map_buffer_range) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(ARB_map_buffer_range not supported)", func);
      return;
   }

   if (offset < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(offset %ld < 0)", func, (long) offset);
      return;
   }

   if (length < 0) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(length %ld < 0)", func, (long) length);
      return;
   }

   if (!_mesa_bufferobj_mapped(bufObj, MAP_USER)) {
      /* buffer is not mapped */
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(buffer is not mapped)", func);
      return;
   }

   if ((bufObj->Mappings[MAP_USER].AccessFlags &
        GL_MAP_FLUSH_EXPLICIT_BIT) == 0) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(GL_MAP_FLUSH_EXPLICIT_BIT not set)", func);
      return;
   }

   if (offset + length > bufObj->Mappings[MAP_USER].Length) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(offset %ld + length %ld > mapped length %ld)", func,
                  (long) offset, (long) length,
                  (long) bufObj->Mappings[MAP_USER].Length);
      return;
   }

   assert(bufObj->Mappings[MAP_USER].AccessFlags & GL_MAP_WRITE_BIT);

   if (ctx->Driver.FlushMappedBufferRange)
      ctx->Driver.FlushMappedBufferRange(ctx, offset, length, bufObj,
                                         MAP_USER);
}

void GLAPIENTRY
_mesa_FlushMappedBufferRange(GLenum target, GLintptr offset,
                             GLsizeiptr length)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   bufObj = get_buffer(ctx, "glFlushMappedBufferRange", target,
                       GL_INVALID_OPERATION);
   if (!bufObj)
      return;

   _mesa_flush_mapped_buffer_range(ctx, bufObj, offset, length,
                                   "glFlushMappedBufferRange");
}

void GLAPIENTRY
_mesa_FlushMappedNamedBufferRange(GLuint buffer, GLintptr offset,
                                  GLsizeiptr length)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   bufObj = _mesa_lookup_bufferobj_err(ctx, buffer,
                                       "glFlushMappedNamedBufferRange");
   if (!bufObj)
      return;

   _mesa_flush_mapped_buffer_range(ctx, bufObj, offset, length,
                                   "glFlushMappedNamedBufferRange");
}


/**
 * Binds a buffer object to a uniform buffer binding point.
 *
 * The caller is responsible for flushing vertices and updating
 * NewDriverState.
 */
static void
set_ubo_binding(struct gl_context *ctx,
                struct gl_uniform_buffer_binding *binding,
                struct gl_buffer_object *bufObj,
                GLintptr offset,
                GLsizeiptr size,
                GLboolean autoSize)
{
   _mesa_reference_buffer_object(ctx, &binding->BufferObject, bufObj);

   binding->Offset = offset;
   binding->Size = size;
   binding->AutomaticSize = autoSize;

   /* If this is a real buffer object, mark it has having been used
    * at some point as a UBO.
    */
   if (size >= 0)
      bufObj->UsageHistory |= USAGE_UNIFORM_BUFFER;
}

/**
 * Binds a buffer object to a shader storage buffer binding point.
 *
 * The caller is responsible for flushing vertices and updating
 * NewDriverState.
 */
static void
set_ssbo_binding(struct gl_context *ctx,
                 struct gl_shader_storage_buffer_binding *binding,
                 struct gl_buffer_object *bufObj,
                 GLintptr offset,
                 GLsizeiptr size,
                 GLboolean autoSize)
{
   _mesa_reference_buffer_object(ctx, &binding->BufferObject, bufObj);

   binding->Offset = offset;
   binding->Size = size;
   binding->AutomaticSize = autoSize;

   /* If this is a real buffer object, mark it has having been used
    * at some point as a SSBO.
    */
   if (size >= 0)
      bufObj->UsageHistory |= USAGE_SHADER_STORAGE_BUFFER;
}

/**
 * Binds a buffer object to a uniform buffer binding point.
 *
 * Unlike set_ubo_binding(), this function also flushes vertices
 * and updates NewDriverState.  It also checks if the binding
 * has actually changed before updating it.
 */
static void
bind_uniform_buffer(struct gl_context *ctx,
                    GLuint index,
                    struct gl_buffer_object *bufObj,
                    GLintptr offset,
                    GLsizeiptr size,
                    GLboolean autoSize)
{
   struct gl_uniform_buffer_binding *binding =
      &ctx->UniformBufferBindings[index];

   if (binding->BufferObject == bufObj &&
       binding->Offset == offset &&
       binding->Size == size &&
       binding->AutomaticSize == autoSize) {
      return;
   }

   FLUSH_VERTICES(ctx, 0);
   ctx->NewDriverState |= ctx->DriverFlags.NewUniformBuffer;

   set_ubo_binding(ctx, binding, bufObj, offset, size, autoSize);
}

/**
 * Binds a buffer object to a shader storage buffer binding point.
 *
 * Unlike set_ssbo_binding(), this function also flushes vertices
 * and updates NewDriverState.  It also checks if the binding
 * has actually changed before updating it.
 */
static void
bind_shader_storage_buffer(struct gl_context *ctx,
                           GLuint index,
                           struct gl_buffer_object *bufObj,
                           GLintptr offset,
                           GLsizeiptr size,
                           GLboolean autoSize)
{
   struct gl_shader_storage_buffer_binding *binding =
      &ctx->ShaderStorageBufferBindings[index];

   if (binding->BufferObject == bufObj &&
       binding->Offset == offset &&
       binding->Size == size &&
       binding->AutomaticSize == autoSize) {
      return;
   }

   FLUSH_VERTICES(ctx, 0);
   ctx->NewDriverState |= ctx->DriverFlags.NewShaderStorageBuffer;

   set_ssbo_binding(ctx, binding, bufObj, offset, size, autoSize);
}

/**
 * Bind a region of a buffer object to a uniform block binding point.
 * \param index  the uniform buffer binding point index
 * \param bufObj  the buffer object
 * \param offset  offset to the start of buffer object region
 * \param size  size of the buffer object region
 */
static void
bind_buffer_range_uniform_buffer(struct gl_context *ctx,
				 GLuint index,
				 struct gl_buffer_object *bufObj,
				 GLintptr offset,
				 GLsizeiptr size)
{
   if (index >= ctx->Const.MaxUniformBufferBindings) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glBindBufferRange(index=%d)", index);
      return;
   }

   if (offset & (ctx->Const.UniformBufferOffsetAlignment - 1)) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glBindBufferRange(offset misaligned %d/%d)", (int) offset,
		  ctx->Const.UniformBufferOffsetAlignment);
      return;
   }

   if (bufObj == ctx->Shared->NullBufferObj) {
      offset = -1;
      size = -1;
   }

   _mesa_reference_buffer_object(ctx, &ctx->UniformBuffer, bufObj);
   bind_uniform_buffer(ctx, index, bufObj, offset, size, GL_FALSE);
}

/**
 * Bind a region of a buffer object to a shader storage block binding point.
 * \param index  the shader storage buffer binding point index
 * \param bufObj  the buffer object
 * \param offset  offset to the start of buffer object region
 * \param size  size of the buffer object region
 */
static void
bind_buffer_range_shader_storage_buffer(struct gl_context *ctx,
                                        GLuint index,
                                        struct gl_buffer_object *bufObj,
                                        GLintptr offset,
                                        GLsizeiptr size)
{
   if (index >= ctx->Const.MaxShaderStorageBufferBindings) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glBindBufferRange(index=%d)", index);
      return;
   }

   if (offset & (ctx->Const.ShaderStorageBufferOffsetAlignment - 1)) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glBindBufferRange(offset misaligned %d/%d)", (int) offset,
                  ctx->Const.ShaderStorageBufferOffsetAlignment);
      return;
   }

   if (bufObj == ctx->Shared->NullBufferObj) {
      offset = -1;
      size = -1;
   }

   _mesa_reference_buffer_object(ctx, &ctx->ShaderStorageBuffer, bufObj);
   bind_shader_storage_buffer(ctx, index, bufObj, offset, size, GL_FALSE);
}

/**
 * Bind a buffer object to a uniform block binding point.
 * As above, but offset = 0.
 */
static void
bind_buffer_base_uniform_buffer(struct gl_context *ctx,
				GLuint index,
				struct gl_buffer_object *bufObj)
{
   if (index >= ctx->Const.MaxUniformBufferBindings) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glBindBufferBase(index=%d)", index);
      return;
   }

   _mesa_reference_buffer_object(ctx, &ctx->UniformBuffer, bufObj);

   if (bufObj == ctx->Shared->NullBufferObj)
      bind_uniform_buffer(ctx, index, bufObj, -1, -1, GL_TRUE);
   else
      bind_uniform_buffer(ctx, index, bufObj, 0, 0, GL_TRUE);
}

/**
 * Bind a buffer object to a shader storage block binding point.
 * As above, but offset = 0.
 */
static void
bind_buffer_base_shader_storage_buffer(struct gl_context *ctx,
                                       GLuint index,
                                       struct gl_buffer_object *bufObj)
{
   if (index >= ctx->Const.MaxShaderStorageBufferBindings) {
      _mesa_error(ctx, GL_INVALID_VALUE, "glBindBufferBase(index=%d)", index);
      return;
   }

   _mesa_reference_buffer_object(ctx, &ctx->ShaderStorageBuffer, bufObj);

   if (bufObj == ctx->Shared->NullBufferObj)
      bind_shader_storage_buffer(ctx, index, bufObj, -1, -1, GL_TRUE);
   else
      bind_shader_storage_buffer(ctx, index, bufObj, 0, 0, GL_TRUE);
}

/**
 * Binds a buffer object to an atomic buffer binding point.
 *
 * The caller is responsible for validating the offset,
 * flushing the vertices and updating NewDriverState.
 */
static void
set_atomic_buffer_binding(struct gl_context *ctx,
                          struct gl_atomic_buffer_binding *binding,
                          struct gl_buffer_object *bufObj,
                          GLintptr offset,
                          GLsizeiptr size)
{
   _mesa_reference_buffer_object(ctx, &binding->BufferObject, bufObj);

   if (bufObj == ctx->Shared->NullBufferObj) {
      binding->Offset = 0;
      binding->Size = 0;
   } else {
      binding->Offset = offset;
      binding->Size = size;
      bufObj->UsageHistory |= USAGE_ATOMIC_COUNTER_BUFFER;
   }
}

/**
 * Binds a buffer object to an atomic buffer binding point.
 *
 * Unlike set_atomic_buffer_binding(), this function also validates the
 * index and offset, flushes vertices, and updates NewDriverState.
 * It also checks if the binding has actually changing before
 * updating it.
 */
static void
bind_atomic_buffer(struct gl_context *ctx,
                   unsigned index,
                   struct gl_buffer_object *bufObj,
                   GLintptr offset,
                   GLsizeiptr size,
                   const char *name)
{
   struct gl_atomic_buffer_binding *binding;

   if (index >= ctx->Const.MaxAtomicBufferBindings) {
      _mesa_error(ctx, GL_INVALID_VALUE, "%s(index=%d)", name, index);
      return;
   }

   if (offset & (ATOMIC_COUNTER_SIZE - 1)) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "%s(offset misaligned %d/%d)", name, (int) offset,
                  ATOMIC_COUNTER_SIZE);
      return;
   }

   _mesa_reference_buffer_object(ctx, &ctx->AtomicBuffer, bufObj);

   binding = &ctx->AtomicBufferBindings[index];
   if (binding->BufferObject == bufObj &&
       binding->Offset == offset &&
       binding->Size == size) {
      return;
   }

   FLUSH_VERTICES(ctx, 0);
   ctx->NewDriverState |= ctx->DriverFlags.NewAtomicBuffer;

   set_atomic_buffer_binding(ctx, binding, bufObj, offset, size);
}

static inline bool
bind_buffers_check_offset_and_size(struct gl_context *ctx,
                                   GLuint index,
                                   const GLintptr *offsets,
                                   const GLsizeiptr *sizes)
{
   if (offsets[index] < 0) {
     /* The ARB_multi_bind spec says:
      *
      *    "An INVALID_VALUE error is generated by BindBuffersRange if any
      *     value in <offsets> is less than zero (per binding)."
      */
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glBindBuffersRange(offsets[%u]=%" PRId64 " < 0)",
                  index, (int64_t) offsets[index]);
      return false;
   }

   if (sizes[index] <= 0) {
     /* The ARB_multi_bind spec says:
      *
      *     "An INVALID_VALUE error is generated by BindBuffersRange if any
      *      value in <sizes> is less than or equal to zero (per binding)."
      */
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glBindBuffersRange(sizes[%u]=%" PRId64 " <= 0)",
                  index, (int64_t) sizes[index]);
      return false;
   }

   return true;
}

static bool
error_check_bind_uniform_buffers(struct gl_context *ctx,
                                 GLuint first, GLsizei count,
                                 const char *caller)
{
   if (!ctx->Extensions.ARB_uniform_buffer_object) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "%s(target=GL_UNIFORM_BUFFER)", caller);
      return false;
   }

   /* The ARB_multi_bind_spec says:
    *
    *     "An INVALID_OPERATION error is generated if <first> + <count> is
    *      greater than the number of target-specific indexed binding points,
    *      as described in section 6.7.1."
    */
   if (first + count > ctx->Const.MaxUniformBufferBindings) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(first=%u + count=%d > the value of "
                  "GL_MAX_UNIFORM_BUFFER_BINDINGS=%u)",
                  caller, first, count,
                  ctx->Const.MaxUniformBufferBindings);
      return false;
   }

   return true;
}

static bool
error_check_bind_shader_storage_buffers(struct gl_context *ctx,
                                        GLuint first, GLsizei count,
                                        const char *caller)
{
   if (!ctx->Extensions.ARB_shader_storage_buffer_object) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "%s(target=GL_SHADER_STORAGE_BUFFER)", caller);
      return false;
   }

   /* The ARB_multi_bind_spec says:
    *
    *     "An INVALID_OPERATION error is generated if <first> + <count> is
    *      greater than the number of target-specific indexed binding points,
    *      as described in section 6.7.1."
    */
   if (first + count > ctx->Const.MaxShaderStorageBufferBindings) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(first=%u + count=%d > the value of "
                  "GL_MAX_SHADER_STORAGE_BUFFER_BINDINGS=%u)",
                  caller, first, count,
                  ctx->Const.MaxShaderStorageBufferBindings);
      return false;
   }

   return true;
}

/**
 * Unbind all uniform buffers in the range
 * <first> through <first>+<count>-1
 */
static void
unbind_uniform_buffers(struct gl_context *ctx, GLuint first, GLsizei count)
{
   struct gl_buffer_object *bufObj = ctx->Shared->NullBufferObj;
   GLint i;

   for (i = 0; i < count; i++)
      set_ubo_binding(ctx, &ctx->UniformBufferBindings[first + i],
                      bufObj, -1, -1, GL_TRUE);
}

/**
 * Unbind all shader storage buffers in the range
 * <first> through <first>+<count>-1
 */
static void
unbind_shader_storage_buffers(struct gl_context *ctx, GLuint first,
                              GLsizei count)
{
   struct gl_buffer_object *bufObj = ctx->Shared->NullBufferObj;
   GLint i;

   for (i = 0; i < count; i++)
      set_ssbo_binding(ctx, &ctx->ShaderStorageBufferBindings[first + i],
                       bufObj, -1, -1, GL_TRUE);
}

static void
bind_uniform_buffers(struct gl_context *ctx, GLuint first, GLsizei count,
                     const GLuint *buffers,
                     bool range,
                     const GLintptr *offsets, const GLsizeiptr *sizes,
                     const char *caller)
{
   GLint i;

   if (!error_check_bind_uniform_buffers(ctx, first, count, caller))
      return;

   /* Assume that at least one binding will be changed */
   FLUSH_VERTICES(ctx, 0);
   ctx->NewDriverState |= ctx->DriverFlags.NewUniformBuffer;

   if (!buffers) {
      /* The ARB_multi_bind spec says:
       *
       *    "If <buffers> is NULL, all bindings from <first> through
       *     <first>+<count>-1 are reset to their unbound (zero) state.
       *     In this case, the offsets and sizes associated with the
       *     binding points are set to default values, ignoring
       *     <offsets> and <sizes>."
       */
      unbind_uniform_buffers(ctx, first, count);
      return;
   }

   /* Note that the error semantics for multi-bind commands differ from
    * those of other GL commands.
    *
    * The Issues section in the ARB_multi_bind spec says:
    *
    *    "(11) Typically, OpenGL specifies that if an error is generated by a
    *          command, that command has no effect.  This is somewhat
    *          unfortunate for multi-bind commands, because it would require a
    *          first pass to scan the entire list of bound objects for errors
    *          and then a second pass to actually perform the bindings.
    *          Should we have different error semantics?
    *
    *       RESOLVED:  Yes.  In this specification, when the parameters for
    *       one of the <count> binding points are invalid, that binding point
    *       is not updated and an error will be generated.  However, other
    *       binding points in the same command will be updated if their
    *       parameters are valid and no other error occurs."
    */

   _mesa_begin_bufferobj_lookups(ctx);

   for (i = 0; i < count; i++) {
      struct gl_uniform_buffer_binding *binding =
         &ctx->UniformBufferBindings[first + i];
      struct gl_buffer_object *bufObj;
      GLintptr offset = 0;
      GLsizeiptr size = 0;

      if (range) {
         if (!bind_buffers_check_offset_and_size(ctx, i, offsets, sizes))
            continue;

         /* The ARB_multi_bind spec says:
          *
          *     "An INVALID_VALUE error is generated by BindBuffersRange if any
          *      pair of values in <offsets> and <sizes> does not respectively
          *      satisfy the constraints described for those parameters for the
          *      specified target, as described in section 6.7.1 (per binding)."
          *
          * Section 6.7.1 refers to table 6.5, which says:
          *
          *     "┌───────────────────────────────────────────────────────────────┐
          *      │ Uniform buffer array bindings (see sec. 7.6)                  │
          *      ├─────────────────────┬─────────────────────────────────────────┤
          *      │  ...                │  ...                                    │
          *      │  offset restriction │  multiple of value of UNIFORM_BUFFER_-  │
          *      │                     │  OFFSET_ALIGNMENT                       │
          *      │  ...                │  ...                                    │
          *      │  size restriction   │  none                                   │
          *      └─────────────────────┴─────────────────────────────────────────┘"
          */
         if (offsets[i] & (ctx->Const.UniformBufferOffsetAlignment - 1)) {
            _mesa_error(ctx, GL_INVALID_VALUE,
                        "glBindBuffersRange(offsets[%u]=%" PRId64
                        " is misaligned; it must be a multiple of the value of "
                        "GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT=%u when "
                        "target=GL_UNIFORM_BUFFER)",
                        i, (int64_t) offsets[i],
                        ctx->Const.UniformBufferOffsetAlignment);
            continue;
         }

         offset = offsets[i];
         size = sizes[i];
      }

      if (binding->BufferObject && binding->BufferObject->Name == buffers[i])
         bufObj = binding->BufferObject;
      else
         bufObj = _mesa_multi_bind_lookup_bufferobj(ctx, buffers, i, caller);

      if (bufObj) {
         if (bufObj == ctx->Shared->NullBufferObj)
            set_ubo_binding(ctx, binding, bufObj, -1, -1, !range);
         else
            set_ubo_binding(ctx, binding, bufObj, offset, size, !range);
      }
   }

   _mesa_end_bufferobj_lookups(ctx);
}

static void
bind_shader_storage_buffers(struct gl_context *ctx, GLuint first,
                            GLsizei count, const GLuint *buffers,
                            bool range,
                            const GLintptr *offsets,
                            const GLsizeiptr *sizes,
                            const char *caller)
{
   GLint i;

   if (!error_check_bind_shader_storage_buffers(ctx, first, count, caller))
      return;

   /* Assume that at least one binding will be changed */
   FLUSH_VERTICES(ctx, 0);
   ctx->NewDriverState |= ctx->DriverFlags.NewShaderStorageBuffer;

   if (!buffers) {
      /* The ARB_multi_bind spec says:
       *
       *    "If <buffers> is NULL, all bindings from <first> through
       *     <first>+<count>-1 are reset to their unbound (zero) state.
       *     In this case, the offsets and sizes associated with the
       *     binding points are set to default values, ignoring
       *     <offsets> and <sizes>."
       */
      unbind_shader_storage_buffers(ctx, first, count);
      return;
   }

   /* Note that the error semantics for multi-bind commands differ from
    * those of other GL commands.
    *
    * The Issues section in the ARB_multi_bind spec says:
    *
    *    "(11) Typically, OpenGL specifies that if an error is generated by a
    *          command, that command has no effect.  This is somewhat
    *          unfortunate for multi-bind commands, because it would require a
    *          first pass to scan the entire list of bound objects for errors
    *          and then a second pass to actually perform the bindings.
    *          Should we have different error semantics?
    *
    *       RESOLVED:  Yes.  In this specification, when the parameters for
    *       one of the <count> binding points are invalid, that binding point
    *       is not updated and an error will be generated.  However, other
    *       binding points in the same command will be updated if their
    *       parameters are valid and no other error occurs."
    */

   _mesa_begin_bufferobj_lookups(ctx);

   for (i = 0; i < count; i++) {
      struct gl_shader_storage_buffer_binding *binding =
         &ctx->ShaderStorageBufferBindings[first + i];
      struct gl_buffer_object *bufObj;
      GLintptr offset = 0;
      GLsizeiptr size = 0;

      if (range) {
         if (!bind_buffers_check_offset_and_size(ctx, i, offsets, sizes))
            continue;

         /* The ARB_multi_bind spec says:
         *
         *     "An INVALID_VALUE error is generated by BindBuffersRange if any
         *      pair of values in <offsets> and <sizes> does not respectively
         *      satisfy the constraints described for those parameters for the
         *      specified target, as described in section 6.7.1 (per binding)."
         *
         * Section 6.7.1 refers to table 6.5, which says:
         *
         *     "┌───────────────────────────────────────────────────────────────┐
         *      │ Shader storage buffer array bindings (see sec. 7.8)           │
         *      ├─────────────────────┬─────────────────────────────────────────┤
         *      │  ...                │  ...                                    │
         *      │  offset restriction │  multiple of value of SHADER_STORAGE_-  │
         *      │                     │  BUFFER_OFFSET_ALIGNMENT                │
         *      │  ...                │  ...                                    │
         *      │  size restriction   │  none                                   │
         *      └─────────────────────┴─────────────────────────────────────────┘"
         */
         if (offsets[i] & (ctx->Const.ShaderStorageBufferOffsetAlignment - 1)) {
            _mesa_error(ctx, GL_INVALID_VALUE,
                        "glBindBuffersRange(offsets[%u]=%" PRId64
                        " is misaligned; it must be a multiple of the value of "
                        "GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT=%u when "
                        "target=GL_SHADER_STORAGE_BUFFER)",
                        i, (int64_t) offsets[i],
                        ctx->Const.ShaderStorageBufferOffsetAlignment);
            continue;
         }

         offset = offsets[i];
         size = sizes[i];
      }

      if (binding->BufferObject && binding->BufferObject->Name == buffers[i])
         bufObj = binding->BufferObject;
      else
         bufObj = _mesa_multi_bind_lookup_bufferobj(ctx, buffers, i, caller);

      if (bufObj) {
         if (bufObj == ctx->Shared->NullBufferObj)
            set_ssbo_binding(ctx, binding, bufObj, -1, -1, !range);
         else
            set_ssbo_binding(ctx, binding, bufObj, offset, size, !range);
      }
   }

   _mesa_end_bufferobj_lookups(ctx);
}

static bool
error_check_bind_xfb_buffers(struct gl_context *ctx,
                             struct gl_transform_feedback_object *tfObj,
                             GLuint first, GLsizei count, const char *caller)
{
   if (!ctx->Extensions.EXT_transform_feedback) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "%s(target=GL_TRANSFORM_FEEDBACK_BUFFER)", caller);
      return false;
   }

   /* Page 398 of the PDF of the OpenGL 4.4 (Core Profile) spec says:
    *
    *     "An INVALID_OPERATION error is generated :
    *
    *     ...
    *     • by BindBufferRange or BindBufferBase if target is TRANSFORM_-
    *       FEEDBACK_BUFFER and transform feedback is currently active."
    *
    * We assume that this is also meant to apply to BindBuffersRange
    * and BindBuffersBase.
    */
   if (tfObj->Active) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(Changing transform feedback buffers while "
                  "transform feedback is active)", caller);
      return false;
   }

   /* The ARB_multi_bind_spec says:
    *
    *     "An INVALID_OPERATION error is generated if <first> + <count> is
    *      greater than the number of target-specific indexed binding points,
    *      as described in section 6.7.1."
    */
   if (first + count > ctx->Const.MaxTransformFeedbackBuffers) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(first=%u + count=%d > the value of "
                  "GL_MAX_TRANSFORM_FEEDBACK_BUFFERS=%u)",
                  caller, first, count,
                  ctx->Const.MaxTransformFeedbackBuffers);
      return false;
   }

   return true;
}

/**
 * Unbind all transform feedback buffers in the range
 * <first> through <first>+<count>-1
 */
static void
unbind_xfb_buffers(struct gl_context *ctx,
                   struct gl_transform_feedback_object *tfObj,
                   GLuint first, GLsizei count)
{
   struct gl_buffer_object * const bufObj = ctx->Shared->NullBufferObj;
   GLint i;

   for (i = 0; i < count; i++)
      _mesa_set_transform_feedback_binding(ctx, tfObj, first + i,
                                           bufObj, 0, 0);
}

static void
bind_xfb_buffers(struct gl_context *ctx,
                 GLuint first, GLsizei count,
                 const GLuint *buffers,
                 bool range,
                 const GLintptr *offsets,
                 const GLsizeiptr *sizes,
                 const char *caller)
{
   struct gl_transform_feedback_object *tfObj =
       ctx->TransformFeedback.CurrentObject;
   GLint i;

   if (!error_check_bind_xfb_buffers(ctx, tfObj, first, count, caller))
      return;

   /* Assume that at least one binding will be changed */
   FLUSH_VERTICES(ctx, 0);
   ctx->NewDriverState |= ctx->DriverFlags.NewTransformFeedback;

   if (!buffers) {
      /* The ARB_multi_bind spec says:
       *
       *    "If <buffers> is NULL, all bindings from <first> through
       *     <first>+<count>-1 are reset to their unbound (zero) state.
       *     In this case, the offsets and sizes associated with the
       *     binding points are set to default values, ignoring
       *     <offsets> and <sizes>."
       */
      unbind_xfb_buffers(ctx, tfObj, first, count);
      return;
   }

   /* Note that the error semantics for multi-bind commands differ from
    * those of other GL commands.
    *
    * The Issues section in the ARB_multi_bind spec says:
    *
    *    "(11) Typically, OpenGL specifies that if an error is generated by a
    *          command, that command has no effect.  This is somewhat
    *          unfortunate for multi-bind commands, because it would require a
    *          first pass to scan the entire list of bound objects for errors
    *          and then a second pass to actually perform the bindings.
    *          Should we have different error semantics?
    *
    *       RESOLVED:  Yes.  In this specification, when the parameters for
    *       one of the <count> binding points are invalid, that binding point
    *       is not updated and an error will be generated.  However, other
    *       binding points in the same command will be updated if their
    *       parameters are valid and no other error occurs."
    */

   _mesa_begin_bufferobj_lookups(ctx);

   for (i = 0; i < count; i++) {
      const GLuint index = first + i;
      struct gl_buffer_object * const boundBufObj = tfObj->Buffers[index];
      struct gl_buffer_object *bufObj;
      GLintptr offset = 0;
      GLsizeiptr size = 0;

      if (range) {
         offset = offsets[i];
         size = sizes[i];

         if (!bind_buffers_check_offset_and_size(ctx, i, offsets, sizes))
            continue;

         /* The ARB_multi_bind spec says:
          *
          *     "An INVALID_VALUE error is generated by BindBuffersRange if any
          *      pair of values in <offsets> and <sizes> does not respectively
          *      satisfy the constraints described for those parameters for the
          *      specified target, as described in section 6.7.1 (per binding)."
          *
          * Section 6.7.1 refers to table 6.5, which says:
          *
          *     "┌───────────────────────────────────────────────────────────────┐
          *      │ Transform feedback array bindings (see sec. 13.2.2)           │
          *      ├───────────────────────┬───────────────────────────────────────┤
          *      │    ...                │    ...                                │
          *      │    offset restriction │    multiple of 4                      │
          *      │    ...                │    ...                                │
          *      │    size restriction   │    multiple of 4                      │
          *      └───────────────────────┴───────────────────────────────────────┘"
          */
         if (offsets[i] & 0x3) {
            _mesa_error(ctx, GL_INVALID_VALUE,
                        "glBindBuffersRange(offsets[%u]=%" PRId64
                        " is misaligned; it must be a multiple of 4 when "
                        "target=GL_TRANSFORM_FEEDBACK_BUFFER)",
                        i, (int64_t) offsets[i]);
            continue;
         }

         if (sizes[i] & 0x3) {
            _mesa_error(ctx, GL_INVALID_VALUE,
                        "glBindBuffersRange(sizes[%u]=%" PRId64
                        " is misaligned; it must be a multiple of 4 when "
                        "target=GL_TRANSFORM_FEEDBACK_BUFFER)",
                        i, (int64_t) sizes[i]);
            continue;
         }

         offset = offsets[i];
         size = sizes[i];
      }

      if (boundBufObj && boundBufObj->Name == buffers[i])
         bufObj = boundBufObj;
      else
         bufObj = _mesa_multi_bind_lookup_bufferobj(ctx, buffers, i, caller);

      if (bufObj)
         _mesa_set_transform_feedback_binding(ctx, tfObj, index, bufObj,
                                              offset, size);
   }

   _mesa_end_bufferobj_lookups(ctx);
}

static bool
error_check_bind_atomic_buffers(struct gl_context *ctx,
                                GLuint first, GLsizei count,
                                const char *caller)
{
   if (!ctx->Extensions.ARB_shader_atomic_counters) {
      _mesa_error(ctx, GL_INVALID_ENUM,
                  "%s(target=GL_ATOMIC_COUNTER_BUFFER)", caller);
      return false;
   }

   /* The ARB_multi_bind_spec says:
    *
    *     "An INVALID_OPERATION error is generated if <first> + <count> is
    *      greater than the number of target-specific indexed binding points,
    *      as described in section 6.7.1."
    */
   if (first + count > ctx->Const.MaxAtomicBufferBindings) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "%s(first=%u + count=%d > the value of "
                  "GL_MAX_ATOMIC_BUFFER_BINDINGS=%u)",
                  caller, first, count, ctx->Const.MaxAtomicBufferBindings);
      return false;
   }

   return true;
}

/**
 * Unbind all atomic counter buffers in the range
 * <first> through <first>+<count>-1
 */
static void
unbind_atomic_buffers(struct gl_context *ctx, GLuint first, GLsizei count)
{
   struct gl_buffer_object * const bufObj = ctx->Shared->NullBufferObj;
   GLint i;

   for (i = 0; i < count; i++)
      set_atomic_buffer_binding(ctx, &ctx->AtomicBufferBindings[first + i],
                                bufObj, -1, -1);
}

static void
bind_atomic_buffers(struct gl_context *ctx,
                    GLuint first,
                    GLsizei count,
                    const GLuint *buffers,
                    bool range,
                    const GLintptr *offsets,
                    const GLsizeiptr *sizes,
                    const char *caller)
{
   GLint i;

   if (!error_check_bind_atomic_buffers(ctx, first, count, caller))
     return;

   /* Assume that at least one binding will be changed */
   FLUSH_VERTICES(ctx, 0);
   ctx->NewDriverState |= ctx->DriverFlags.NewAtomicBuffer;

   if (!buffers) {
      /* The ARB_multi_bind spec says:
       *
       *    "If <buffers> is NULL, all bindings from <first> through
       *     <first>+<count>-1 are reset to their unbound (zero) state.
       *     In this case, the offsets and sizes associated with the
       *     binding points are set to default values, ignoring
       *     <offsets> and <sizes>."
       */
      unbind_atomic_buffers(ctx, first, count);
      return;
   }

   /* Note that the error semantics for multi-bind commands differ from
    * those of other GL commands.
    *
    * The Issues section in the ARB_multi_bind spec says:
    *
    *    "(11) Typically, OpenGL specifies that if an error is generated by a
    *          command, that command has no effect.  This is somewhat
    *          unfortunate for multi-bind commands, because it would require a
    *          first pass to scan the entire list of bound objects for errors
    *          and then a second pass to actually perform the bindings.
    *          Should we have different error semantics?
    *
    *       RESOLVED:  Yes.  In this specification, when the parameters for
    *       one of the <count> binding points are invalid, that binding point
    *       is not updated and an error will be generated.  However, other
    *       binding points in the same command will be updated if their
    *       parameters are valid and no other error occurs."
    */

   _mesa_begin_bufferobj_lookups(ctx);

   for (i = 0; i < count; i++) {
      struct gl_atomic_buffer_binding *binding =
         &ctx->AtomicBufferBindings[first + i];
      struct gl_buffer_object *bufObj;
      GLintptr offset = 0;
      GLsizeiptr size = 0;

      if (range) {
         if (!bind_buffers_check_offset_and_size(ctx, i, offsets, sizes))
            continue;

         /* The ARB_multi_bind spec says:
          *
          *     "An INVALID_VALUE error is generated by BindBuffersRange if any
          *      pair of values in <offsets> and <sizes> does not respectively
          *      satisfy the constraints described for those parameters for the
          *      specified target, as described in section 6.7.1 (per binding)."
          *
          * Section 6.7.1 refers to table 6.5, which says:
          *
          *     "┌───────────────────────────────────────────────────────────────┐
          *      │ Atomic counter array bindings (see sec. 7.7.2)                │
          *      ├───────────────────────┬───────────────────────────────────────┤
          *      │    ...                │    ...                                │
          *      │    offset restriction │    multiple of 4                      │
          *      │    ...                │    ...                                │
          *      │    size restriction   │    none                               │
          *      └───────────────────────┴───────────────────────────────────────┘"
          */
         if (offsets[i] & (ATOMIC_COUNTER_SIZE - 1)) {
            _mesa_error(ctx, GL_INVALID_VALUE,
                        "glBindBuffersRange(offsets[%u]=%" PRId64
                        " is misaligned; it must be a multiple of %d when "
                        "target=GL_ATOMIC_COUNTER_BUFFER)",
                        i, (int64_t) offsets[i], ATOMIC_COUNTER_SIZE);
            continue;
         }

         offset = offsets[i];
         size = sizes[i];
      }

      if (binding->BufferObject && binding->BufferObject->Name == buffers[i])
         bufObj = binding->BufferObject;
      else
         bufObj = _mesa_multi_bind_lookup_bufferobj(ctx, buffers, i, caller);

      if (bufObj)
         set_atomic_buffer_binding(ctx, binding, bufObj, offset, size);
   }

   _mesa_end_bufferobj_lookups(ctx);
}

void GLAPIENTRY
_mesa_BindBufferRange(GLenum target, GLuint index,
                      GLuint buffer, GLintptr offset, GLsizeiptr size)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   if (MESA_VERBOSE & VERBOSE_API) {
      _mesa_debug(ctx, "glBindBufferRange(%s, %u, %u, %lu, %lu)\n",
                  _mesa_enum_to_string(target), index, buffer,
                  (unsigned long) offset, (unsigned long) size);
   }

   if (buffer == 0) {
      bufObj = ctx->Shared->NullBufferObj;
   } else {
      bufObj = _mesa_lookup_bufferobj(ctx, buffer);
   }
   if (!_mesa_handle_bind_buffer_gen(ctx, buffer,
                                     &bufObj, "glBindBufferRange"))
      return;

   if (!bufObj) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glBindBufferRange(invalid buffer=%u)", buffer);
      return;
   }

   if (buffer != 0) {
      if (size <= 0) {
         _mesa_error(ctx, GL_INVALID_VALUE, "glBindBufferRange(size=%d)",
                     (int) size);
         return;
      }
   }

   switch (target) {
   case GL_TRANSFORM_FEEDBACK_BUFFER:
      _mesa_bind_buffer_range_transform_feedback(ctx,
                                                 ctx->TransformFeedback.CurrentObject,
                                                 index, bufObj, offset, size,
                                                 false);
      return;
   case GL_UNIFORM_BUFFER:
      bind_buffer_range_uniform_buffer(ctx, index, bufObj, offset, size);
      return;
   case GL_SHADER_STORAGE_BUFFER:
      bind_buffer_range_shader_storage_buffer(ctx, index, bufObj, offset, size);
      return;
   case GL_ATOMIC_COUNTER_BUFFER:
      bind_atomic_buffer(ctx, index, bufObj, offset, size,
                         "glBindBufferRange");
      return;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM, "glBindBufferRange(target)");
      return;
   }
}

void GLAPIENTRY
_mesa_BindBufferBase(GLenum target, GLuint index, GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   if (MESA_VERBOSE & VERBOSE_API) {
      _mesa_debug(ctx, "glBindBufferBase(%s, %u, %u)\n",
                  _mesa_enum_to_string(target), index, buffer);
   }

   if (buffer == 0) {
      bufObj = ctx->Shared->NullBufferObj;
   } else {
      bufObj = _mesa_lookup_bufferobj(ctx, buffer);
   }
   if (!_mesa_handle_bind_buffer_gen(ctx, buffer,
                                     &bufObj, "glBindBufferBase"))
      return;

   if (!bufObj) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glBindBufferBase(invalid buffer=%u)", buffer);
      return;
   }

   /* Note that there's some oddness in the GL 3.1-GL 3.3 specifications with
    * regards to BindBufferBase.  It says (GL 3.1 core spec, page 63):
    *
    *     "BindBufferBase is equivalent to calling BindBufferRange with offset
    *      zero and size equal to the size of buffer."
    *
    * but it says for glGetIntegeri_v (GL 3.1 core spec, page 230):
    *
    *     "If the parameter (starting offset or size) was not specified when the
    *      buffer object was bound, zero is returned."
    *
    * What happens if the size of the buffer changes?  Does the size of the
    * buffer at the moment glBindBufferBase was called still play a role, like
    * the first quote would imply, or is the size meaningless in the
    * glBindBufferBase case like the second quote would suggest?  The GL 4.1
    * core spec page 45 says:
    *
    *     "It is equivalent to calling BindBufferRange with offset zero, while
    *      size is determined by the size of the bound buffer at the time the
    *      binding is used."
    *
    * My interpretation is that the GL 4.1 spec was a clarification of the
    * behavior, not a change.  In particular, this choice will only make
    * rendering work in cases where it would have had undefined results.
    */

   switch (target) {
   case GL_TRANSFORM_FEEDBACK_BUFFER:
      _mesa_bind_buffer_base_transform_feedback(ctx,
                                                ctx->TransformFeedback.CurrentObject,
                                                index, bufObj, false);
      return;
   case GL_UNIFORM_BUFFER:
      bind_buffer_base_uniform_buffer(ctx, index, bufObj);
      return;
   case GL_SHADER_STORAGE_BUFFER:
      bind_buffer_base_shader_storage_buffer(ctx, index, bufObj);
      return;
   case GL_ATOMIC_COUNTER_BUFFER:
      bind_atomic_buffer(ctx, index, bufObj, 0, 0,
                         "glBindBufferBase");
      return;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM, "glBindBufferBase(target)");
      return;
   }
}

void GLAPIENTRY
_mesa_BindBuffersRange(GLenum target, GLuint first, GLsizei count,
                       const GLuint *buffers,
                       const GLintptr *offsets, const GLsizeiptr *sizes)
{
   GET_CURRENT_CONTEXT(ctx);

   if (MESA_VERBOSE & VERBOSE_API) {
      _mesa_debug(ctx, "glBindBuffersRange(%s, %u, %d, %p, %p, %p)\n",
                  _mesa_enum_to_string(target), first, count,
                  buffers, offsets, sizes);
   }

   switch (target) {
   case GL_TRANSFORM_FEEDBACK_BUFFER:
      bind_xfb_buffers(ctx, first, count, buffers, true, offsets, sizes,
                       "glBindBuffersRange");
      return;
   case GL_UNIFORM_BUFFER:
      bind_uniform_buffers(ctx, first, count, buffers, true, offsets, sizes,
                           "glBindBuffersRange");
      return;
   case GL_SHADER_STORAGE_BUFFER:
      bind_shader_storage_buffers(ctx, first, count, buffers, true, offsets, sizes,
                                  "glBindBuffersRange");
      return;
   case GL_ATOMIC_COUNTER_BUFFER:
      bind_atomic_buffers(ctx, first, count, buffers, true, offsets, sizes,
                          "glBindBuffersRange");
      return;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM, "glBindBuffersRange(target=%s)",
                  _mesa_enum_to_string(target));
      break;
   }
}

void GLAPIENTRY
_mesa_BindBuffersBase(GLenum target, GLuint first, GLsizei count,
                      const GLuint *buffers)
{
   GET_CURRENT_CONTEXT(ctx);

   if (MESA_VERBOSE & VERBOSE_API) {
      _mesa_debug(ctx, "glBindBuffersBase(%s, %u, %d, %p)\n",
                  _mesa_enum_to_string(target), first, count, buffers);
   }

   switch (target) {
   case GL_TRANSFORM_FEEDBACK_BUFFER:
      bind_xfb_buffers(ctx, first, count, buffers, false, NULL, NULL,
                       "glBindBuffersBase");
      return;
   case GL_UNIFORM_BUFFER:
      bind_uniform_buffers(ctx, first, count, buffers, false, NULL, NULL,
                           "glBindBuffersBase");
      return;
   case GL_SHADER_STORAGE_BUFFER:
      bind_shader_storage_buffers(ctx, first, count, buffers, false, NULL, NULL,
                                  "glBindBuffersBase");
      return;
   case GL_ATOMIC_COUNTER_BUFFER:
      bind_atomic_buffers(ctx, first, count, buffers, false, NULL, NULL,
                          "glBindBuffersBase");
      return;
   default:
      _mesa_error(ctx, GL_INVALID_ENUM, "glBindBuffersBase(target=%s)",
                  _mesa_enum_to_string(target));
      break;
   }
}

void GLAPIENTRY
_mesa_InvalidateBufferSubData(GLuint buffer, GLintptr offset,
                              GLsizeiptr length)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;
   const GLintptr end = offset + length;

   /* Section 6.5 (Invalidating Buffer Data) of the OpenGL 4.5 (Compatibility
    * Profile) spec says:
    *
    *     "An INVALID_VALUE error is generated if buffer is zero or is not the
    *     name of an existing buffer object."
    */
   bufObj = _mesa_lookup_bufferobj(ctx, buffer);
   if (!bufObj || bufObj == &DummyBufferObject) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glInvalidateBufferSubData(name = %u) invalid object",
                  buffer);
      return;
   }

   /* The GL_ARB_invalidate_subdata spec says:
    *
    *     "An INVALID_VALUE error is generated if <offset> or <length> is
    *     negative, or if <offset> + <length> is greater than the value of
    *     BUFFER_SIZE."
    */
   if (offset < 0 || length < 0 || end > bufObj->Size) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glInvalidateBufferSubData(invalid offset or length)");
      return;
   }

   /* The OpenGL 4.4 (Core Profile) spec says:
    *
    *     "An INVALID_OPERATION error is generated if buffer is currently
    *     mapped by MapBuffer or if the invalidate range intersects the range
    *     currently mapped by MapBufferRange, unless it was mapped
    *     with MAP_PERSISTENT_BIT set in the MapBufferRange access flags."
    */
   if (!(bufObj->Mappings[MAP_USER].AccessFlags & GL_MAP_PERSISTENT_BIT) &&
       bufferobj_range_mapped(bufObj, offset, length)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glInvalidateBufferSubData(intersection with mapped "
                  "range)");
      return;
   }

   if (ctx->Driver.InvalidateBufferSubData)
      ctx->Driver.InvalidateBufferSubData(ctx, bufObj, offset, length);
}

void GLAPIENTRY
_mesa_InvalidateBufferData(GLuint buffer)
{
   GET_CURRENT_CONTEXT(ctx);
   struct gl_buffer_object *bufObj;

   /* Section 6.5 (Invalidating Buffer Data) of the OpenGL 4.5 (Compatibility
    * Profile) spec says:
    *
    *     "An INVALID_VALUE error is generated if buffer is zero or is not the
    *     name of an existing buffer object."
    */
   bufObj = _mesa_lookup_bufferobj(ctx, buffer);
   if (!bufObj || bufObj == &DummyBufferObject) {
      _mesa_error(ctx, GL_INVALID_VALUE,
                  "glInvalidateBufferData(name = %u) invalid object",
                  buffer);
      return;
   }

   /* The OpenGL 4.4 (Core Profile) spec says:
    *
    *     "An INVALID_OPERATION error is generated if buffer is currently
    *     mapped by MapBuffer or if the invalidate range intersects the range
    *     currently mapped by MapBufferRange, unless it was mapped
    *     with MAP_PERSISTENT_BIT set in the MapBufferRange access flags."
    */
   if (_mesa_check_disallowed_mapping(bufObj)) {
      _mesa_error(ctx, GL_INVALID_OPERATION,
                  "glInvalidateBufferData(intersection with mapped "
                  "range)");
      return;
   }

   if (ctx->Driver.InvalidateBufferSubData)
      ctx->Driver.InvalidateBufferSubData(ctx, bufObj, 0, bufObj->Size);
}
