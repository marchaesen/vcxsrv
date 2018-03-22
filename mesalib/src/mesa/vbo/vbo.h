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
 * \brief Public interface to the VBO module
 * \author Keith Whitwell
 */


#ifndef _VBO_H
#define _VBO_H

#include <stdbool.h>
#include "main/glheader.h"

#ifdef __cplusplus
extern "C" {
#endif

struct gl_vertex_array;
struct gl_context;
struct gl_transform_feedback_object;

struct _mesa_prim
{
   GLuint mode:8;    /**< GL_POINTS, GL_LINES, GL_QUAD_STRIP, etc */
   GLuint indexed:1;
   GLuint begin:1;
   GLuint end:1;
   GLuint weak:1;
   GLuint no_current_update:1;
   GLuint is_indirect:1;
   GLuint pad:18;

   GLuint start;
   GLuint count;
   GLint basevertex;
   GLuint num_instances;
   GLuint base_instance;
   GLuint draw_id;

   GLsizeiptr indirect_offset;
};

/* Would like to call this a "vbo_index_buffer", but this would be
 * confusing as the indices are not neccessarily yet in a non-null
 * buffer object.
 */
struct _mesa_index_buffer
{
   GLuint count;
   unsigned index_size;
   struct gl_buffer_object *obj;
   const void *ptr;
};



GLboolean
_vbo_CreateContext(struct gl_context *ctx);

void
_vbo_DestroyContext(struct gl_context *ctx);

void
vbo_exec_invalidate_state(struct gl_context *ctx);

void
_vbo_install_exec_vtxfmt(struct gl_context *ctx);

void
vbo_initialize_exec_dispatch(const struct gl_context *ctx,
                             struct _glapi_table *exec);

void
vbo_initialize_save_dispatch(const struct gl_context *ctx,
                             struct _glapi_table *exec);

void
vbo_exec_FlushVertices(struct gl_context *ctx, GLuint flags);

void
vbo_save_SaveFlushVertices(struct gl_context *ctx);

void
vbo_save_NotifyBegin(struct gl_context *ctx, GLenum mode);

void
vbo_save_NewList(struct gl_context *ctx, GLuint list, GLenum mode);

void
vbo_save_EndList(struct gl_context *ctx);

void
vbo_save_BeginCallList(struct gl_context *ctx, struct gl_display_list *list);

void
vbo_save_EndCallList(struct gl_context *ctx);


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
typedef void (*vbo_draw_func)(struct gl_context *ctx,
                              const struct _mesa_prim *prims,
                              GLuint nr_prims,
                              const struct _mesa_index_buffer *ib,
                              GLboolean index_bounds_valid,
                              GLuint min_index,
                              GLuint max_index,
                              struct gl_transform_feedback_object *tfb_vertcount,
                              unsigned tfb_stream,
                              struct gl_buffer_object *indirect);


/**
 * Draw a primitive, getting the vertex count, instance count, start
 * vertex, etc. from a buffer object.
 * \param mode  GL_POINTS, GL_LINES, GL_TRIANGLE_STRIP, etc.
 * \param indirect_data  buffer to get "DrawArrays/ElementsIndirectCommand" data
 * \param indirect_offset  offset of first primitive in indrect_data buffer
 * \param draw_count  number of primitives to draw
 * \param stride  stride, in bytes, between "DrawArrays/ElementsIndirectCommand"
 *                objects
 * \param indirect_draw_count_buffer  if non-NULL specifies a buffer to get the
 *                                    real draw_count value.  Used for
 *                                    GL_ARB_indirect_parameters.
 * \param indirect_draw_count_offset  offset to the draw_count value in
 *                                    indirect_draw_count_buffer
 * \param ib  index buffer for indexed drawing, NULL otherwise.
 */
typedef void (*vbo_indirect_draw_func)(
   struct gl_context *ctx,
   GLuint mode,
   struct gl_buffer_object *indirect_data,
   GLsizeiptr indirect_offset,
   unsigned draw_count,
   unsigned stride,
   struct gl_buffer_object *indirect_draw_count_buffer,
   GLsizeiptr indirect_draw_count_offset,
   const struct _mesa_index_buffer *ib);




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
_vbo_draw(struct gl_context *ctx, const struct _mesa_prim *prims,
               GLuint nr_prims, const struct _mesa_index_buffer *ib,
               GLboolean index_bounds_valid, GLuint min_index, GLuint max_index,
               struct gl_transform_feedback_object *tfb_vertcount,
               unsigned tfb_stream, struct gl_buffer_object *indirect);


void
_vbo_draw_indirect(struct gl_context *ctx, GLuint mode,
                        struct gl_buffer_object *indirect_data,
                        GLsizeiptr indirect_offset, unsigned draw_count,
                        unsigned stride,
                        struct gl_buffer_object *indirect_draw_count_buffer,
                        GLsizeiptr indirect_draw_count_offset,
                        const struct _mesa_index_buffer *ib);


void
vbo_split_prims(struct gl_context *ctx,
                const struct gl_vertex_array *arrays,
                const struct _mesa_prim *prim,
                GLuint nr_prims,
                const struct _mesa_index_buffer *ib,
                GLuint min_index,
                GLuint max_index,
                vbo_draw_func draw,
                const struct split_limits *limits);


void
vbo_delete_minmax_cache(struct gl_buffer_object *bufferObj);

void
vbo_get_minmax_indices(struct gl_context *ctx, const struct _mesa_prim *prim,
                       const struct _mesa_index_buffer *ib,
                       GLuint *min_index, GLuint *max_index, GLuint nr_prims);

void
vbo_use_buffer_objects(struct gl_context *ctx);

void
vbo_always_unmap_buffers(struct gl_context *ctx);

void
vbo_set_draw_func(struct gl_context *ctx, vbo_draw_func func);

void
vbo_set_indirect_draw_func(struct gl_context *ctx,
                           vbo_indirect_draw_func func);

void
vbo_sw_primitive_restart(struct gl_context *ctx,
                         const struct _mesa_prim *prim,
                         GLuint nr_prims,
                         const struct _mesa_index_buffer *ib,
                         struct gl_buffer_object *indirect);


/**
 * Utility that tracks and updates the current array entries.
 */
struct vbo_inputs
{
   /**
    * Array of inputs to be set to the _DrawArrays pointer.
    * The array contains pointers into the _DrawVAO and to the vbo modules
    * current values. The array of pointers is updated incrementally
    * based on the current and vertex_processing_mode values below.
    */
   struct gl_vertex_array inputs[VERT_ATTRIB_MAX];
   /** Those VERT_BIT_'s where the inputs array point to current values. */
   GLbitfield current;
   /** Store which aliasing current values - generics or materials - are set. */
   gl_vertex_processing_mode vertex_processing_mode;
};


/**
 * Set the recalculate_inputs flag.
 * The method should in the longer run be replaced with listening for the
 * DriverFlags.NewArray flag in NewDriverState. But for now ...
 */
void
_vbo_set_recalculate_inputs(struct gl_context *ctx);


/**
 * Initialize inputs.
 */
void
_vbo_init_inputs(struct vbo_inputs *inputs);


/**
 * Update the gl_vertex_array array inside the vbo_inputs structure
 * provided the current _VPMode, the provided vao and
 * the vao's enabled arrays filtered by the filter bitmask.
 */
void
_vbo_update_inputs(struct gl_context *ctx, struct vbo_inputs *inputs);


void GLAPIENTRY
_es_Color4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a);

void GLAPIENTRY
_es_Normal3f(GLfloat x, GLfloat y, GLfloat z);

void GLAPIENTRY
_es_MultiTexCoord4f(GLenum target, GLfloat s, GLfloat t, GLfloat r, GLfloat q);

void GLAPIENTRY
_es_Materialfv(GLenum face, GLenum pname, const GLfloat *params);

void GLAPIENTRY
_es_Materialf(GLenum face, GLenum pname, GLfloat param);

void GLAPIENTRY
_es_VertexAttrib4f(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);

void GLAPIENTRY
_es_VertexAttrib1f(GLuint indx, GLfloat x);

void GLAPIENTRY
_es_VertexAttrib1fv(GLuint indx, const GLfloat* values);

void GLAPIENTRY
_es_VertexAttrib2f(GLuint indx, GLfloat x, GLfloat y);

void GLAPIENTRY
_es_VertexAttrib2fv(GLuint indx, const GLfloat* values);

void GLAPIENTRY
_es_VertexAttrib3f(GLuint indx, GLfloat x, GLfloat y, GLfloat z);

void GLAPIENTRY
_es_VertexAttrib3fv(GLuint indx, const GLfloat* values);

void GLAPIENTRY
_es_VertexAttrib4fv(GLuint indx, const GLfloat* values);

#ifdef __cplusplus
} // extern "C"
#endif

#endif
