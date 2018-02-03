/*
 * mesa 3-D graphics library
 *
 * Copyright (C) 1999-2006  Brian Paul   All Rights Reserved.
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
 * Types, functions, etc which are private to the VBO module.
 */


#ifndef VBO_PRIVATE_H
#define VBO_PRIVATE_H


#include "vbo/vbo_attrib.h"
#include "vbo/vbo_exec.h"
#include "vbo/vbo_save.h"
#include "main/mtypes.h"


struct _glapi_table;
struct _mesa_prim;


struct vbo_context {
   struct gl_vertex_array currval[VBO_ATTRIB_MAX];

   struct vbo_exec_context exec;
   struct vbo_save_context save;

   /* Callback into the driver.  This must always succeed, the driver
    * is responsible for initiating any fallback actions required:
    */
   vbo_draw_func draw_prims;

   /* Optional callback for indirect draws. This allows multidraws to not be
    * broken up, as well as for the actual count to be passed in as a separate
    * indirect parameter.
    */
   vbo_indirect_draw_func draw_indirect_prims;
};


static inline struct vbo_context *
vbo_context(struct gl_context *ctx)
{
   return ctx->vbo_context;
}


/**
 * Current vertex processing mode: fixed function vs. shader.
 * In reality, fixed function is probably implemented by a shader but that's
 * not what we care about here.
 */
enum vp_mode {
   VP_FF,    /**< legacy / fixed function */
   VP_SHADER, /**< ARB vertex program or GLSL vertex shader */
   VP_MODE_MAX /**< for sizing arrays */
};


/**
 * Get current vertex processing mode (fixed function vs. shader).
 */
static inline enum vp_mode
get_vp_mode( struct gl_context *ctx )
{
   if (!ctx->VertexProgram._Current)
      return VP_FF;
   else if (ctx->VertexProgram._Current == ctx->VertexProgram._TnlProgram)
      return VP_FF;
   else
      return VP_SHADER;
}


/**
 * Array to apply the fixed function material aliasing map to
 * an attribute value used in vbo processing inputs to an attribute
 * as they appear in the vao.
 */
extern const GLubyte
_vbo_attribute_alias_map[VP_MODE_MAX][VERT_ATTRIB_MAX];


/**
 * Return if format is integer. The immediate mode commands only emit floats
 * for non-integer types, thus everything else is integer.
 */
static inline GLboolean
vbo_attrtype_to_integer_flag(GLenum format)
{
   switch (format) {
   case GL_FLOAT:
   case GL_DOUBLE:
      return GL_FALSE;
   case GL_INT:
   case GL_UNSIGNED_INT:
   case GL_UNSIGNED_INT64_ARB:
      return GL_TRUE;
   default:
      unreachable("Bad vertex attribute type");
      return GL_FALSE;
   }
}

static inline GLboolean
vbo_attrtype_to_double_flag(GLenum format)
{
   switch (format) {
   case GL_FLOAT:
   case GL_INT:
   case GL_UNSIGNED_INT:
   case GL_UNSIGNED_INT64_ARB:
      return GL_FALSE;
   case GL_DOUBLE:
      return GL_TRUE;
   default:
      unreachable("Bad vertex attribute type");
      return GL_FALSE;
   }
}


/**
 * Return default component values for the given format.
 * The return type is an array of fi_types, because that's how we declare
 * the vertex storage : floats , integers or unsigned integers.
 */
static inline const fi_type *
vbo_get_default_vals_as_union(GLenum format)
{
   static const GLfloat default_float[4] = { 0, 0, 0, 1 };
   static const GLint default_int[4] = { 0, 0, 0, 1 };

   switch (format) {
   case GL_FLOAT:
      return (fi_type *)default_float;
   case GL_INT:
   case GL_UNSIGNED_INT:
      return (fi_type *)default_int;
   default:
      unreachable("Bad vertex format");
      return NULL;
   }
}


/**
 * Compute the max number of vertices which can be stored in
 * a vertex buffer, given the current vertex size, and the amount
 * of space already used.
 */
static inline unsigned
vbo_compute_max_verts(const struct vbo_exec_context *exec)
{
   unsigned n = (VBO_VERT_BUFFER_SIZE - exec->vtx.buffer_used) /
      (exec->vtx.vertex_size * sizeof(GLfloat));
   if (n == 0)
      return 0;
   /* Subtract one so we're always sure to have room for an extra
    * vertex for GL_LINE_LOOP -> GL_LINE_STRIP conversion.
    */
   n--;
   return n;
}


void
vbo_try_prim_conversion(struct _mesa_prim *p);

bool
vbo_can_merge_prims(const struct _mesa_prim *p0, const struct _mesa_prim *p1);

void
vbo_merge_prims(struct _mesa_prim *p0, const struct _mesa_prim *p1);


#endif /* VBO_PRIVATE_H */
