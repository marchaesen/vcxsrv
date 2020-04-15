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
 *
 * Authors:
 *    Keith Whitwell <keithw@vmware.com>
 */

#ifndef _TNL_H
#define _TNL_H

#include "main/glheader.h"

struct gl_context;
struct gl_program;
struct gl_buffer_object;
struct gl_transform_feedback_object;
struct dd_function_table;


/* These are the public-access functions exported from tnl.  (A few
 * more are currently hooked into dispatch directly by the module
 * itself.)
 */
extern GLboolean
_tnl_CreateContext( struct gl_context *ctx );

extern void
_tnl_DestroyContext( struct gl_context *ctx );

extern void
_tnl_InvalidateState( struct gl_context *ctx, GLuint new_state );

extern void
_tnl_init_driver_draw_function(struct dd_function_table *functions);

/* Functions to revive the tnl module after being unhooked from
 * dispatch and/or driver callbacks.
 */

extern void
_tnl_wakeup( struct gl_context *ctx );

/* Driver configuration options:
 */
extern void
_tnl_need_projected_coords( struct gl_context *ctx, GLboolean flag );


/**
 * Vertex array information which is derived from gl_array_attributes
 * and gl_vertex_buffer_binding information.  Used by the TNL module and
 * device drivers.
 */
struct tnl_vertex_array
{
   /** Vertex attribute array */
   const struct gl_array_attributes *VertexAttrib;
   /** Vertex buffer binding */
   const struct gl_vertex_buffer_binding *BufferBinding;
};


extern const struct tnl_vertex_array*
_tnl_bind_inputs( struct gl_context *ctx );


/* Control whether T&L does per-vertex fog
 */
extern void
_tnl_allow_vertex_fog( struct gl_context *ctx, GLboolean value );

extern void
_tnl_allow_pixel_fog( struct gl_context *ctx, GLboolean value );

extern GLboolean
_tnl_program_string(struct gl_context *ctx, GLenum target, struct gl_program *program);

struct _mesa_prim;
struct _mesa_index_buffer;

void
_tnl_draw_prims(struct gl_context *ctx,
                const struct tnl_vertex_array *arrays,
		     const struct _mesa_prim *prim,
		     GLuint nr_prims,
		     const struct _mesa_index_buffer *ib,
		     GLboolean index_bounds_valid,
		     GLuint min_index,
		     GLuint max_index,
                     GLuint num_instances,
                     GLuint base_instance);

void
_tnl_draw(struct gl_context *ctx,
          const struct _mesa_prim *prim, GLuint nr_prims,
          const struct _mesa_index_buffer *ib,
          GLboolean index_bounds_valid, GLuint min_index, GLuint max_index,
          GLuint num_instances, GLuint base_instance,
          struct gl_transform_feedback_object *tfb_vertcount, unsigned stream);

extern void
_tnl_RasterPos(struct gl_context *ctx, const GLfloat vObj[4]);

extern void
_tnl_validate_shine_tables( struct gl_context *ctx );



/**
 * For indirect array drawing:
 *
 *    typedef struct {
 *       GLuint count;
 *       GLuint primCount;
 *       GLuint first;
 *       GLuint baseInstance; // in GL 4.2 and later, must be zero otherwise
 *    } DrawArraysIndirectCommand;
 *
 * For indirect indexed drawing:
 *
 *    typedef struct {
 *       GLuint count;
 *       GLuint primCount;
 *       GLuint firstIndex;
 *       GLint  baseVertex;
 *       GLuint baseInstance; // in GL 4.2 and later, must be zero otherwise
 *    } DrawElementsIndirectCommand;
 */


/**
 * Draw a number of primitives.
 * \param prims  array [nr_prims] describing what to draw (prim type,
 *               vertex count, first index, instance count, etc).
 * \param arrays array of vertex arrays for draw
 * \param ib  index buffer for indexed drawing, NULL for array drawing
 * \param index_bounds_valid  are min_index and max_index valid?
 * \param min_index  lowest vertex index used
 * \param max_index  highest vertex index used
 * \param tfb_vertcount  if non-null, indicates which transform feedback
 *                       object has the vertex count.
 * \param tfb_stream  If called via DrawTransformFeedbackStream, specifies the
 *                    vertex stream buffer from which to get the vertex count.
 * \param indirect  If any prims are indirect, this specifies the buffer
 *                  to find the "DrawArrays/ElementsIndirectCommand" data.
 *                  This may be deprecated in the future
 */
typedef void (*tnl_draw_func)(struct gl_context *ctx,
                              const struct tnl_vertex_array* arrays,
                              const struct _mesa_prim *prims,
                              GLuint nr_prims,
                              const struct _mesa_index_buffer *ib,
                              GLboolean index_bounds_valid,
                              GLuint min_index,
                              GLuint max_index,
                              GLuint num_instances,
                              GLuint base_instance);


/* Utility function to cope with various constraints on tnl modules or
 * hardware.  This can be used to split an incoming set of arrays and
 * primitives against the following constraints:
 *    - Maximum number of indices in index buffer.
 *    - Maximum number of vertices referenced by index buffer.
 *    - Maximum hardware vertex buffer size.
 */
struct split_limits
{
   GLuint max_verts;
   GLuint max_indices;
   GLuint max_vb_size;		/* bytes */
};

void
_tnl_split_prims(struct gl_context *ctx,
                 const struct tnl_vertex_array *arrays,
                 const struct _mesa_prim *prim,
                 GLuint nr_prims,
                 const struct _mesa_index_buffer *ib,
                 GLuint min_index,
                 GLuint max_index,
                 GLuint num_instances,
                 GLuint base_instance,
                 tnl_draw_func draw,
                 const struct split_limits *limits);


#endif
