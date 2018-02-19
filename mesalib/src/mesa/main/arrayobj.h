/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2004  Brian Paul   All Rights Reserved.
 * (C) Copyright IBM Corporation 2006
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

#ifndef ARRAYOBJ_H
#define ARRAYOBJ_H

#include "glheader.h"
#include "mtypes.h"
#include "glformats.h"

struct gl_context;

/**
 * \file arrayobj.h
 * Functions for the GL_ARB_vertex_array_object extension.
 *
 * \author Ian Romanick <idr@us.ibm.com>
 * \author Brian Paul
 */

/*
 * Internal functions
 */

extern struct gl_vertex_array_object *
_mesa_lookup_vao(struct gl_context *ctx, GLuint id);

extern struct gl_vertex_array_object *
_mesa_lookup_vao_err(struct gl_context *ctx, GLuint id, const char *caller);

extern struct gl_vertex_array_object *
_mesa_new_vao(struct gl_context *ctx, GLuint name);

extern void
_mesa_delete_vao(struct gl_context *ctx, struct gl_vertex_array_object *obj);

extern void
_mesa_reference_vao_(struct gl_context *ctx,
                     struct gl_vertex_array_object **ptr,
                     struct gl_vertex_array_object *vao);

static inline void
_mesa_reference_vao(struct gl_context *ctx,
                    struct gl_vertex_array_object **ptr,
                    struct gl_vertex_array_object *vao)
{
   if (*ptr != vao)
      _mesa_reference_vao_(ctx, ptr, vao);
}


extern void
_mesa_initialize_vao(struct gl_context *ctx,
                     struct gl_vertex_array_object *obj, GLuint name);


extern void
_mesa_update_vao_derived_arrays(struct gl_context *ctx,
                                struct gl_vertex_array_object *vao);

/* Returns true if all varying arrays reside in vbos */
extern bool
_mesa_all_varyings_in_vbos(const struct gl_vertex_array_object *vao);

/* Returns true if all vbos are unmapped */
extern bool
_mesa_all_buffers_are_unmapped(const struct gl_vertex_array_object *vao);


/**
 * Array to apply the position/generic0 aliasing map to
 * an attribute value used in vertex processing inputs to an attribute
 * as they appear in the vao.
 */
extern const GLubyte
_mesa_vao_attribute_map[ATTRIBUTE_MAP_MODE_MAX][VERT_ATTRIB_MAX];


/**
 * Apply the position/generic0 aliasing map to a bitfield from the vao.
 * Use for example to convert gl_vertex_array_object::_Enabled
 * or gl_vertex_buffer_binding::_VertexBinding from the vao numbering to
 * the numbering used with vertex processing inputs.
 */
static inline GLbitfield
_mesa_vao_enable_to_vp_inputs(gl_attribute_map_mode mode, GLbitfield enabled)
{
   switch (mode) {
   case ATTRIBUTE_MAP_MODE_IDENTITY:
      return enabled;
   case ATTRIBUTE_MAP_MODE_POSITION:
      /* Copy VERT_ATTRIB_POS enable bit into GENERIC0 position */
      return (enabled & ~VERT_BIT_GENERIC0)
         | ((enabled & VERT_BIT_POS) << VERT_ATTRIB_GENERIC0);
   case ATTRIBUTE_MAP_MODE_GENERIC0:
      /* Copy VERT_ATTRIB_GENERIC0 enable bit into POS position */
      return (enabled & ~VERT_BIT_POS)
         | ((enabled & VERT_BIT_GENERIC0) >> VERT_ATTRIB_GENERIC0);
   default:
      return 0;
   }
}


/**
 * Return the vp_inputs enabled bitmask after application of
 * the position/generic0 aliasing map.
 */
static inline GLbitfield
_mesa_get_vao_vp_inputs(const struct gl_vertex_array_object *vao)
{
   const gl_attribute_map_mode mode = vao->_AttributeMapMode;
   return _mesa_vao_enable_to_vp_inputs(mode, vao->_Enabled);
}


/*
 * API functions
 */


void GLAPIENTRY
_mesa_BindVertexArray_no_error(GLuint id);

void GLAPIENTRY _mesa_BindVertexArray( GLuint id );

void GLAPIENTRY
_mesa_DeleteVertexArrays_no_error(GLsizei n, const GLuint *ids);

void GLAPIENTRY _mesa_DeleteVertexArrays(GLsizei n, const GLuint *ids);

void GLAPIENTRY
_mesa_GenVertexArrays_no_error(GLsizei n, GLuint *arrays);

void GLAPIENTRY _mesa_GenVertexArrays(GLsizei n, GLuint *arrays);

void GLAPIENTRY
_mesa_CreateVertexArrays_no_error(GLsizei n, GLuint *arrays);

void GLAPIENTRY _mesa_CreateVertexArrays(GLsizei n, GLuint *arrays);

GLboolean GLAPIENTRY _mesa_IsVertexArray( GLuint id );

void GLAPIENTRY
_mesa_VertexArrayElementBuffer_no_error(GLuint vaobj, GLuint buffer);

void GLAPIENTRY _mesa_VertexArrayElementBuffer(GLuint vaobj, GLuint buffer);

void GLAPIENTRY _mesa_GetVertexArrayiv(GLuint vaobj, GLenum pname, GLint *param);

#endif /* ARRAYOBJ_H */
