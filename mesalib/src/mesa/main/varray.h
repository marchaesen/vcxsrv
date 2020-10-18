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


#ifndef VARRAY_H
#define VARRAY_H

#include "bufferobj.h"

struct gl_interleaved_layout {
   bool tflag, cflag, nflag;      /* enable/disable flags */
   int tcomps, ccomps, vcomps;    /* components per texcoord, color, vertex */
   GLenum ctype;                  /* color type */
   int coffset, noffset, voffset; /* color, normal, vertex offsets */
   int toffset;                   /* always zero */
   int defstride;                 /* default stride */
};

bool
_mesa_get_interleaved_layout(GLenum format,
                             struct gl_interleaved_layout *layout);

void
_mesa_set_vertex_format(struct gl_vertex_format *vertex_format,
                        GLubyte size, GLenum16 type, GLenum16 format,
                        GLboolean normalized, GLboolean integer,
                        GLboolean doubles);


/**
 * Returns a pointer to the vertex attribute data in a client array,
 * or the offset into the vertex buffer for an array that resides in
 * a vertex buffer.
 */
static inline const GLubyte *
_mesa_vertex_attrib_address(const struct gl_array_attributes *array,
                            const struct gl_vertex_buffer_binding *binding)
{
   if (binding->BufferObj)
      return (const GLubyte *) (binding->Offset + array->RelativeOffset);
   else
      return array->Ptr;
}


static inline bool
_mesa_attr_zero_aliases_vertex(const struct gl_context *ctx)
{
   return ctx->_AttribZeroAliasesVertex;
}


extern void
_mesa_update_array_format(struct gl_context *ctx,
                          struct gl_vertex_array_object *vao,
                          gl_vert_attrib attrib, GLint size, GLenum type,
                          GLenum format, GLboolean normalized,
                          GLboolean integer, GLboolean doubles,
                          GLuint relativeOffset);

extern void
_mesa_enable_vertex_array_attribs(struct gl_context *ctx,
                                 struct gl_vertex_array_object *vao,
                                 GLbitfield attrib_bits);

static inline void
_mesa_enable_vertex_array_attrib(struct gl_context *ctx,
                                 struct gl_vertex_array_object *vao,
                                 gl_vert_attrib attrib)
{
   assert(attrib < VERT_ATTRIB_MAX);
   _mesa_enable_vertex_array_attribs(ctx, vao, VERT_BIT(attrib));
}


extern void
_mesa_disable_vertex_array_attribs(struct gl_context *ctx,
                                   struct gl_vertex_array_object *vao,
                                   GLbitfield attrib_bits);

static inline void
_mesa_disable_vertex_array_attrib(struct gl_context *ctx,
                                  struct gl_vertex_array_object *vao,
                                  gl_vert_attrib attrib)
{
   assert(attrib < VERT_ATTRIB_MAX);
   _mesa_disable_vertex_array_attribs(ctx, vao, VERT_BIT(attrib));
}


extern void
_mesa_vertex_attrib_binding(struct gl_context *ctx,
                            struct gl_vertex_array_object *vao,
                            gl_vert_attrib attribIndex,
                            GLuint bindingIndex);


extern void
_mesa_bind_vertex_buffer(struct gl_context *ctx,
                         struct gl_vertex_array_object *vao,
                         GLuint index,
                         struct gl_buffer_object *vbo,
                         GLintptr offset, GLsizei stride,
                         bool offset_is_int32, bool take_vbo_ownership);

extern void GLAPIENTRY
_mesa_VertexPointer_no_error(GLint size, GLenum type, GLsizei stride,
                             const GLvoid *ptr);
extern void GLAPIENTRY
_mesa_VertexPointer(GLint size, GLenum type, GLsizei stride,
                    const GLvoid *ptr);

extern void GLAPIENTRY
_mesa_NormalPointer_no_error(GLenum type, GLsizei stride, const GLvoid *ptr);
extern void GLAPIENTRY
_mesa_NormalPointer(GLenum type, GLsizei stride, const GLvoid *ptr);

extern void GLAPIENTRY
_mesa_ColorPointer_no_error(GLint size, GLenum type, GLsizei stride,
                            const GLvoid *ptr);
extern void GLAPIENTRY
_mesa_ColorPointer(GLint size, GLenum type, GLsizei stride, const GLvoid *ptr);


extern void GLAPIENTRY
_mesa_IndexPointer_no_error(GLenum type, GLsizei stride, const GLvoid *ptr);
extern void GLAPIENTRY
_mesa_IndexPointer(GLenum type, GLsizei stride, const GLvoid *ptr);


extern void GLAPIENTRY
_mesa_TexCoordPointer_no_error(GLint size, GLenum type, GLsizei stride,
                               const GLvoid *ptr);
extern void GLAPIENTRY
_mesa_TexCoordPointer(GLint size, GLenum type, GLsizei stride,
                      const GLvoid *ptr);
extern void GLAPIENTRY
_mesa_MultiTexCoordPointerEXT(GLenum texunit, GLint size, GLenum type,
                              GLsizei stride, const GLvoid *ptr);

extern void GLAPIENTRY
_mesa_EdgeFlagPointer_no_error(GLsizei stride, const GLvoid *ptr);
extern void GLAPIENTRY
_mesa_EdgeFlagPointer(GLsizei stride, const GLvoid *ptr);


extern void GLAPIENTRY
_mesa_VertexPointerEXT(GLint size, GLenum type, GLsizei stride,
                       GLsizei count, const GLvoid *ptr);


extern void GLAPIENTRY
_mesa_NormalPointerEXT(GLenum type, GLsizei stride, GLsizei count,
                       const GLvoid *ptr);


extern void GLAPIENTRY
_mesa_ColorPointerEXT(GLint size, GLenum type, GLsizei stride, GLsizei count,
                      const GLvoid *ptr);


extern void GLAPIENTRY
_mesa_IndexPointerEXT(GLenum type, GLsizei stride, GLsizei count,
                      const GLvoid *ptr);


extern void GLAPIENTRY
_mesa_TexCoordPointerEXT(GLint size, GLenum type, GLsizei stride,
                         GLsizei count, const GLvoid *ptr);


extern void GLAPIENTRY
_mesa_EdgeFlagPointerEXT(GLsizei stride, GLsizei count, const GLboolean *ptr);

extern void GLAPIENTRY
_mesa_FogCoordPointer_no_error(GLenum type, GLsizei stride,
                               const GLvoid *ptr);
extern void GLAPIENTRY
_mesa_FogCoordPointer(GLenum type, GLsizei stride, const GLvoid *ptr);


extern void GLAPIENTRY
_mesa_SecondaryColorPointer_no_error(GLint size, GLenum type,
                                     GLsizei stride, const GLvoid *ptr);
extern void GLAPIENTRY
_mesa_SecondaryColorPointer(GLint size, GLenum type,
                            GLsizei stride, const GLvoid *ptr);


extern void GLAPIENTRY
_mesa_PointSizePointerOES_no_error(GLenum type, GLsizei stride,
                                   const GLvoid *ptr);
extern void GLAPIENTRY
_mesa_PointSizePointerOES(GLenum type, GLsizei stride, const GLvoid *ptr);


extern void GLAPIENTRY
_mesa_VertexAttribPointer_no_error(GLuint index, GLint size, GLenum type,
                                   GLboolean normalized, GLsizei stride,
                                   const GLvoid *pointer);
extern void GLAPIENTRY
_mesa_VertexAttribPointer(GLuint index, GLint size, GLenum type,
                          GLboolean normalized, GLsizei stride,
                          const GLvoid *pointer);

void GLAPIENTRY
_mesa_VertexAttribIPointer_no_error(GLuint index, GLint size, GLenum type,
                                    GLsizei stride, const GLvoid *ptr);
void GLAPIENTRY
_mesa_VertexAttribIPointer(GLuint index, GLint size, GLenum type,
                           GLsizei stride, const GLvoid *ptr);

extern void GLAPIENTRY
_mesa_VertexAttribLPointer_no_error(GLuint index, GLint size, GLenum type,
                                    GLsizei stride, const GLvoid *pointer);
extern void GLAPIENTRY
_mesa_VertexAttribLPointer(GLuint index, GLint size, GLenum type,
                           GLsizei stride, const GLvoid *pointer);

extern void GLAPIENTRY
_mesa_EnableVertexAttribArray(GLuint index);

extern void GLAPIENTRY
_mesa_EnableVertexAttribArray_no_error(GLuint index);


extern void GLAPIENTRY
_mesa_EnableVertexArrayAttrib(GLuint vaobj, GLuint index);

extern void GLAPIENTRY
_mesa_EnableVertexArrayAttrib_no_error(GLuint vaobj, GLuint index);

extern void GLAPIENTRY
_mesa_EnableVertexArrayAttribEXT( GLuint vaobj, GLuint index );


extern void GLAPIENTRY
_mesa_DisableVertexAttribArray(GLuint index);

extern void GLAPIENTRY
_mesa_DisableVertexAttribArray_no_error(GLuint index);


extern void GLAPIENTRY
_mesa_DisableVertexArrayAttrib(GLuint vaobj, GLuint index);

extern void GLAPIENTRY
_mesa_DisableVertexArrayAttrib_no_error(GLuint vaobj, GLuint index);

extern void GLAPIENTRY
_mesa_DisableVertexArrayAttribEXT( GLuint vaobj, GLuint index );

extern void GLAPIENTRY
_mesa_GetVertexAttribdv(GLuint index, GLenum pname, GLdouble *params);

extern void GLAPIENTRY
_mesa_GetVertexAttribfv(GLuint index, GLenum pname, GLfloat *params);

extern void GLAPIENTRY
_mesa_GetVertexAttribLdv(GLuint index, GLenum pname, GLdouble *params);

extern void GLAPIENTRY
_mesa_GetVertexAttribiv(GLuint index, GLenum pname, GLint *params);

extern void GLAPIENTRY
_mesa_GetVertexAttribLui64vARB(GLuint index, GLenum pname, GLuint64EXT *params);


extern void GLAPIENTRY
_mesa_GetVertexAttribIiv(GLuint index, GLenum pname, GLint *params);


extern void GLAPIENTRY
_mesa_GetVertexAttribIuiv(GLuint index, GLenum pname, GLuint *params);


extern void GLAPIENTRY
_mesa_GetVertexAttribPointerv(GLuint index, GLenum pname, GLvoid **pointer);


void GLAPIENTRY
_mesa_GetVertexArrayIndexediv(GLuint vaobj, GLuint index,
                              GLenum pname, GLint *param);


void GLAPIENTRY
_mesa_GetVertexArrayIndexed64iv(GLuint vaobj, GLuint index,
                                GLenum pname, GLint64 *param);


extern void GLAPIENTRY
_mesa_InterleavedArrays(GLenum format, GLsizei stride, const GLvoid *pointer);


extern void GLAPIENTRY
_mesa_LockArraysEXT(GLint first, GLsizei count);

extern void GLAPIENTRY
_mesa_UnlockArraysEXT(void);


void GLAPIENTRY
_mesa_PrimitiveRestartIndex_no_error(GLuint index);

extern void GLAPIENTRY
_mesa_PrimitiveRestartIndex(GLuint index);

extern void GLAPIENTRY
_mesa_VertexAttribDivisor_no_error(GLuint index, GLuint divisor);
extern void GLAPIENTRY
_mesa_VertexAttribDivisor(GLuint index, GLuint divisor);
extern void GLAPIENTRY
_mesa_VertexArrayVertexAttribDivisorEXT(GLuint vaobj, GLuint index, GLuint divisor);

static inline unsigned
_mesa_get_prim_restart_index(bool fixed_index, unsigned restart_index,
                             unsigned index_size)
{
   /* The index_size parameter is meant to be in bytes. */
   assert(index_size == 1 || index_size == 2 || index_size == 4);

   /* From the OpenGL 4.3 core specification, page 302:
    * "If both PRIMITIVE_RESTART and PRIMITIVE_RESTART_FIXED_INDEX are
    *  enabled, the index value determined by PRIMITIVE_RESTART_FIXED_INDEX
    *  is used."
    */
   if (fixed_index) {
      /* 1 -> 0xff, 2 -> 0xffff, 4 -> 0xffffffff */
      return 0xffffffffu >> 8 * (4 - index_size);
   }

   return restart_index;
}

static inline unsigned
_mesa_primitive_restart_index(const struct gl_context *ctx,
                              unsigned index_size)
{
   return _mesa_get_prim_restart_index(ctx->Array.PrimitiveRestartFixedIndex,
                                       ctx->Array.RestartIndex, index_size);
}

extern void GLAPIENTRY
_mesa_BindVertexBuffer_no_error(GLuint bindingIndex, GLuint buffer,
                                GLintptr offset, GLsizei stride);
extern void GLAPIENTRY
_mesa_BindVertexBuffer(GLuint bindingIndex, GLuint buffer, GLintptr offset,
                       GLsizei stride);

void GLAPIENTRY
_mesa_VertexArrayVertexBuffer_no_error(GLuint vaobj, GLuint bindingIndex,
                                       GLuint buffer, GLintptr offset,
                                       GLsizei stride);
extern void GLAPIENTRY
_mesa_VertexArrayVertexBuffer(GLuint vaobj, GLuint bindingIndex, GLuint buffer,
                              GLintptr offset, GLsizei stride);

extern void GLAPIENTRY
_mesa_VertexArrayBindVertexBufferEXT(GLuint vaobj, GLuint bindingIndex, GLuint buffer,
                                     GLintptr offset, GLsizei stride);

void GLAPIENTRY
_mesa_BindVertexBuffers_no_error(GLuint first, GLsizei count,
                                 const GLuint *buffers, const GLintptr *offsets,
                                 const GLsizei *strides);

extern void GLAPIENTRY
_mesa_BindVertexBuffers(GLuint first, GLsizei count, const GLuint *buffers,
                        const GLintptr *offsets, const GLsizei *strides);

void
_mesa_InternalBindVertexBuffers(struct gl_context *ctx,
                                const struct glthread_attrib_binding *buffers,
                                GLbitfield buffer_mask,
                                GLboolean restore_pointers);

void GLAPIENTRY
_mesa_VertexArrayVertexBuffers_no_error(GLuint vaobj, GLuint first,
                                        GLsizei count, const GLuint *buffers,
                                        const GLintptr *offsets,
                                        const GLsizei *strides);

extern void GLAPIENTRY
_mesa_VertexArrayVertexBuffers(GLuint vaobj, GLuint first, GLsizei count,
                               const GLuint *buffers,
                               const GLintptr *offsets, const GLsizei *strides);

extern void GLAPIENTRY
_mesa_VertexAttribFormat(GLuint attribIndex, GLint size, GLenum type,
                         GLboolean normalized, GLuint relativeOffset);

extern void GLAPIENTRY
_mesa_VertexArrayAttribFormat(GLuint vaobj, GLuint attribIndex, GLint size,
                              GLenum type, GLboolean normalized,
                              GLuint relativeOffset);

extern void GLAPIENTRY
_mesa_VertexArrayVertexAttribFormatEXT(GLuint vaobj, GLuint attribIndex, GLint size,
                                       GLenum type, GLboolean normalized,
                                       GLuint relativeOffset);

extern void GLAPIENTRY
_mesa_VertexAttribIFormat(GLuint attribIndex, GLint size, GLenum type,
                          GLuint relativeOffset);

extern void GLAPIENTRY
_mesa_VertexArrayAttribIFormat(GLuint vaobj, GLuint attribIndex,
                               GLint size, GLenum type,
                               GLuint relativeOffset);

extern void GLAPIENTRY
_mesa_VertexArrayVertexAttribIFormatEXT(GLuint vaobj, GLuint attribIndex,
                                        GLint size, GLenum type,
                                        GLuint relativeOffset);

extern void GLAPIENTRY
_mesa_VertexAttribLFormat(GLuint attribIndex, GLint size, GLenum type,
                          GLuint relativeOffset);

extern void GLAPIENTRY
_mesa_VertexArrayAttribLFormat(GLuint vaobj, GLuint attribIndex,
                               GLint size, GLenum type,
                               GLuint relativeOffset);

extern void GLAPIENTRY
_mesa_VertexArrayVertexAttribLFormatEXT(GLuint vaobj, GLuint attribIndex,
                                        GLint size, GLenum type,
                                        GLuint relativeOffset);

void GLAPIENTRY
_mesa_VertexAttribBinding_no_error(GLuint attribIndex, GLuint bindingIndex);

extern void GLAPIENTRY
_mesa_VertexAttribBinding(GLuint attribIndex, GLuint bindingIndex);

void GLAPIENTRY
_mesa_VertexArrayAttribBinding_no_error(GLuint vaobj, GLuint attribIndex,
                                        GLuint bindingIndex);

extern void GLAPIENTRY
_mesa_VertexArrayAttribBinding(GLuint vaobj, GLuint attribIndex,
                               GLuint bindingIndex);

extern void GLAPIENTRY
_mesa_VertexArrayVertexAttribBindingEXT(GLuint vaobj, GLuint attribIndex,
                                        GLuint bindingIndex);

void GLAPIENTRY
_mesa_VertexBindingDivisor_no_error(GLuint bindingIndex, GLuint divisor);

extern void GLAPIENTRY
_mesa_VertexBindingDivisor(GLuint bindingIndex, GLuint divisor);

void GLAPIENTRY
_mesa_VertexArrayBindingDivisor_no_error(GLuint vaobj, GLuint bindingIndex,
                                         GLuint divisor);

extern void GLAPIENTRY
_mesa_VertexArrayBindingDivisor(GLuint vaobj, GLuint bindingIndex,
                                GLuint divisor);

extern void GLAPIENTRY
_mesa_VertexArrayVertexBindingDivisorEXT(GLuint vaobj, GLuint bindingIndex,
                                         GLuint divisor);


extern void
_mesa_copy_vertex_attrib_array(struct gl_context *ctx,
                               struct gl_array_attributes *dst,
                               const struct gl_array_attributes *src);

extern void
_mesa_copy_vertex_buffer_binding(struct gl_context *ctx,
                                 struct gl_vertex_buffer_binding *dst,
                                 const struct gl_vertex_buffer_binding *src);

extern void
_mesa_print_arrays(struct gl_context *ctx);

extern void
_mesa_init_varray(struct gl_context *ctx);

extern void
_mesa_free_varray_data(struct gl_context *ctx);

extern void GLAPIENTRY
_mesa_VertexArrayVertexOffsetEXT(GLuint vaobj, GLuint buffer, GLint size,
                                 GLenum type, GLsizei stride, GLintptr offset);

extern void GLAPIENTRY
_mesa_VertexArrayColorOffsetEXT(GLuint vaobj, GLuint buffer, GLint size,
                                GLenum type, GLsizei stride, GLintptr offset);

extern void GLAPIENTRY
_mesa_VertexArrayEdgeFlagOffsetEXT(GLuint vaobj, GLuint buffer, GLsizei stride,
                                   GLintptr offset);

extern void GLAPIENTRY
_mesa_VertexArrayIndexOffsetEXT(GLuint vaobj, GLuint buffer, GLenum type,
                                GLsizei stride, GLintptr offset);

extern void GLAPIENTRY
_mesa_VertexArrayNormalOffsetEXT(GLuint vaobj, GLuint buffer, GLenum type,
                                 GLsizei stride, GLintptr offset);

extern void GLAPIENTRY
_mesa_VertexArrayTexCoordOffsetEXT(GLuint vaobj, GLuint buffer, GLint size,
                                   GLenum type, GLsizei stride, GLintptr offset);

extern void GLAPIENTRY
_mesa_VertexArrayMultiTexCoordOffsetEXT(GLuint vaobj, GLuint buffer, GLenum texunit,
                                        GLint size, GLenum type, GLsizei stride,
                                        GLintptr offset);

extern void GLAPIENTRY
_mesa_VertexArrayFogCoordOffsetEXT(GLuint vaobj, GLuint buffer, GLenum type,
                                   GLsizei stride, GLintptr offset);

extern void GLAPIENTRY
_mesa_VertexArraySecondaryColorOffsetEXT(GLuint vaobj, GLuint buffer, GLint size,
                                         GLenum type, GLsizei stride, GLintptr offset);

extern void GLAPIENTRY
_mesa_VertexArrayVertexAttribOffsetEXT(GLuint vaobj, GLuint buffer, GLuint index, GLint size,
                                       GLenum type, GLboolean normalized,
                                       GLsizei stride, GLintptr offset);

extern void GLAPIENTRY
_mesa_VertexArrayVertexAttribIOffsetEXT(GLuint vaobj, GLuint buffer, GLuint index, GLint size,
                                        GLenum type, GLsizei stride, GLintptr offset);

extern void GLAPIENTRY
_mesa_VertexArrayVertexAttribLOffsetEXT(GLuint vaobj, GLuint buffer, GLuint index, GLint size,
                                        GLenum type, GLsizei stride, GLintptr offset);

extern void GLAPIENTRY
_mesa_GetVertexArrayIntegervEXT(GLuint vaobj, GLenum pname, GLint *param);

extern void GLAPIENTRY
_mesa_GetVertexArrayPointervEXT(GLuint vaobj, GLenum pname, GLvoid** param);

extern void GLAPIENTRY
_mesa_GetVertexArrayIntegeri_vEXT(GLuint vaobj, GLuint index, GLenum pname, GLint *param);

extern void GLAPIENTRY
_mesa_GetVertexArrayPointeri_vEXT(GLuint vaobj, GLuint index, GLenum pname, GLvoid** param);

#endif
