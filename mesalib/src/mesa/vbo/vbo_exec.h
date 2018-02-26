/**************************************************************************

Copyright 2002 VMware, Inc.

All Rights Reserved.

Permission is hereby granted, free of charge, to any person obtaining a
copy of this software and associated documentation files (the "Software"),
to deal in the Software without restriction, including without limitation
on the rights to use, copy, modify, merge, publish, distribute, sub
license, and/or sell copies of the Software, and to permit persons to whom
the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice (including the next
paragraph) shall be included in all copies or substantial portions of the
Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
VMWARE AND/OR THEIR SUPPLIERS BE LIABLE FOR ANY CLAIM,
DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
USE OR OTHER DEALINGS IN THE SOFTWARE.

**************************************************************************/

/*
 * Authors:
 *   Keith Whitwell <keithw@vmware.com>
 *
 */

#ifndef VBO_EXEC_H
#define VBO_EXEC_H


#include "main/mtypes.h"
#include "main/imports.h"
#include "vbo.h"
#include "vbo_attrib.h"


/**
 * Max number of primitives (number of glBegin/End pairs) per VBO.
 */
#define VBO_MAX_PRIM 64


/**
 * Size (in bytes) of the VBO to use for glBegin/glVertex/glEnd-style rendering.
 */
#define VBO_VERT_BUFFER_SIZE (1024 * 64)


struct vbo_exec_eval1_map {
   struct gl_1d_map *map;
   GLuint sz;
};

struct vbo_exec_eval2_map {
   struct gl_2d_map *map;
   GLuint sz;
};



struct vbo_exec_copied_vtx {
   fi_type buffer[VBO_ATTRIB_MAX * 4 * VBO_MAX_COPIED_VERTS];
   GLuint nr;
};


struct vbo_exec_context
{
   struct gl_context *ctx;
   GLvertexformat vtxfmt;
   GLvertexformat vtxfmt_noop;

   struct {
      struct gl_buffer_object *bufferobj;

      GLuint vertex_size;       /* in dwords */

      struct _mesa_prim prim[VBO_MAX_PRIM];
      GLuint prim_count;

      fi_type *buffer_map;
      fi_type *buffer_ptr;              /* cursor, points into buffer */
      GLuint   buffer_used;             /* in bytes */
      fi_type vertex[VBO_ATTRIB_MAX*4]; /* current vertex */

      GLuint vert_count;   /**< Number of vertices currently in buffer */
      GLuint max_vert;     /**< Max number of vertices allowed in buffer */
      struct vbo_exec_copied_vtx copied;

      GLbitfield64 enabled;             /**< mask of enabled vbo arrays. */
      GLubyte attrsz[VBO_ATTRIB_MAX];   /**< nr. of attrib components (1..4) */
      GLenum16 attrtype[VBO_ATTRIB_MAX];  /**< GL_FLOAT, GL_DOUBLE, GL_INT, etc */
      GLubyte active_sz[VBO_ATTRIB_MAX];  /**< attrib size (nr. 32-bit words) */

      /** pointers into the current 'vertex' array, declared above */
      fi_type *attrptr[VBO_ATTRIB_MAX];
   } vtx;

   struct {
      GLboolean recalculate_maps;
      struct vbo_exec_eval1_map map1[VERT_ATTRIB_MAX];
      struct vbo_exec_eval2_map map2[VERT_ATTRIB_MAX];
   } eval;

   struct {
      GLboolean recalculate_inputs;
   } array;

   /* Which flags to set in vbo_exec_begin_vertices() */
   GLbitfield begin_vertices_flags;

#ifdef DEBUG
   GLint flush_call_depth;
#endif
};



void
vbo_exec_init(struct gl_context *ctx);

void
vbo_exec_destroy(struct gl_context *ctx);

void
vbo_exec_vtx_init(struct vbo_exec_context *exec);

void
vbo_exec_vtx_destroy(struct vbo_exec_context *exec);

void
vbo_exec_vtx_flush(struct vbo_exec_context *exec, GLboolean unmap);

void
vbo_exec_vtx_map(struct vbo_exec_context *exec);

void
vbo_exec_eval_update(struct vbo_exec_context *exec);

void
vbo_exec_do_EvalCoord2f(struct vbo_exec_context *exec, GLfloat u, GLfloat v);

void
vbo_exec_do_EvalCoord1f(struct vbo_exec_context *exec, GLfloat u);

#endif
