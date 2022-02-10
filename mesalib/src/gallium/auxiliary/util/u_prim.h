/**************************************************************************
 *
 * Copyright 2008 VMware, Inc.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sub license, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT.
 * IN NO EVENT SHALL VMWARE AND/OR ITS SUPPLIERS BE LIABLE FOR
 * ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 **************************************************************************/


#ifndef U_PRIM_H
#define U_PRIM_H


#include "pipe/p_defines.h"
#include "util/compiler.h"
#include "util/u_debug.h"
#include "compiler/shader_enums.h"

#ifdef __cplusplus
extern "C" {
#endif

struct u_prim_vertex_count {
   unsigned min;
   unsigned incr;
};

/**
 * Decompose a primitive that is a loop, a strip, or a fan.  Return the
 * original primitive if it is already decomposed.
 */
static inline enum pipe_prim_type
u_decomposed_prim(enum pipe_prim_type prim)
{
   switch (prim) {
   case PIPE_PRIM_LINE_LOOP:
   case PIPE_PRIM_LINE_STRIP:
      return PIPE_PRIM_LINES;
   case PIPE_PRIM_TRIANGLE_STRIP:
   case PIPE_PRIM_TRIANGLE_FAN:
      return PIPE_PRIM_TRIANGLES;
   case PIPE_PRIM_QUAD_STRIP:
      return PIPE_PRIM_QUADS;
   case PIPE_PRIM_LINE_STRIP_ADJACENCY:
      return PIPE_PRIM_LINES_ADJACENCY;
   case PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return PIPE_PRIM_TRIANGLES_ADJACENCY;
   default:
      return prim;
   }
}

/**
 * Reduce a primitive to one of PIPE_PRIM_POINTS, PIPE_PRIM_LINES, and
 * PIPE_PRIM_TRIANGLES.
 */
static inline enum pipe_prim_type
u_reduced_prim(enum pipe_prim_type prim)
{
   switch (prim) {
   case PIPE_PRIM_POINTS:
      return PIPE_PRIM_POINTS;
   case PIPE_PRIM_LINES:
   case PIPE_PRIM_LINE_LOOP:
   case PIPE_PRIM_LINE_STRIP:
   case PIPE_PRIM_LINES_ADJACENCY:
   case PIPE_PRIM_LINE_STRIP_ADJACENCY:
      return PIPE_PRIM_LINES;
   default:
      return PIPE_PRIM_TRIANGLES;
   }
}

/**
 * Re-assemble a primitive to remove its adjacency.
 */
static inline enum pipe_prim_type
u_assembled_prim(enum pipe_prim_type prim)
{
   switch (prim) {
   case PIPE_PRIM_LINES_ADJACENCY:
   case PIPE_PRIM_LINE_STRIP_ADJACENCY:
      return PIPE_PRIM_LINES;
   case PIPE_PRIM_TRIANGLES_ADJACENCY:
   case PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return PIPE_PRIM_TRIANGLES;
   default:
      return prim;
   }
}

/**
 * Return the vertex count information for a primitive.
 *
 * Note that if this function is called directly or indirectly anywhere in a
 * source file, it will increase the size of the binary slightly more than
 * expected because of the use of a table.
 */
static inline const struct u_prim_vertex_count *
u_prim_vertex_count(enum pipe_prim_type prim)
{
   static const struct u_prim_vertex_count prim_table[PIPE_PRIM_MAX] = {
      { 1, 1 }, /* PIPE_PRIM_POINTS */
      { 2, 2 }, /* PIPE_PRIM_LINES */
      { 2, 1 }, /* PIPE_PRIM_LINE_LOOP */
      { 2, 1 }, /* PIPE_PRIM_LINE_STRIP */
      { 3, 3 }, /* PIPE_PRIM_TRIANGLES */
      { 3, 1 }, /* PIPE_PRIM_TRIANGLE_STRIP */
      { 3, 1 }, /* PIPE_PRIM_TRIANGLE_FAN */
      { 4, 4 }, /* PIPE_PRIM_QUADS */
      { 4, 2 }, /* PIPE_PRIM_QUAD_STRIP */
      { 3, 1 }, /* PIPE_PRIM_POLYGON */
      { 4, 4 }, /* PIPE_PRIM_LINES_ADJACENCY */
      { 4, 1 }, /* PIPE_PRIM_LINE_STRIP_ADJACENCY */
      { 6, 6 }, /* PIPE_PRIM_TRIANGLES_ADJACENCY */
      { 6, 2 }, /* PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY */
   };

   return (likely(prim < PIPE_PRIM_MAX)) ? &prim_table[prim] : NULL;
}

/**
 * Given a vertex count, return the number of primitives.
 * For polygons, return the number of triangles.
 */
static inline unsigned
u_prims_for_vertices(enum pipe_prim_type prim, unsigned num)
{
   const struct u_prim_vertex_count *info = u_prim_vertex_count(prim);

   assert(info);
   assert(info->incr != 0);

   if (num < info->min)
      return 0;

   return 1 + ((num - info->min) / info->incr);
}

static inline boolean
u_validate_pipe_prim(enum pipe_prim_type pipe_prim, unsigned nr)
{
   const struct u_prim_vertex_count *count = u_prim_vertex_count(pipe_prim);

   return (count && nr >= count->min);
}


static inline boolean
u_trim_pipe_prim(enum pipe_prim_type pipe_prim, unsigned *nr)
{
   const struct u_prim_vertex_count *count = u_prim_vertex_count(pipe_prim);

   if (count && *nr >= count->min) {
      if (count->incr > 1)
         *nr -= (*nr % count->incr);
      return TRUE;
   }
   else {
      *nr = 0;
      return FALSE;
   }
}

static inline unsigned
u_vertices_per_prim(enum pipe_prim_type primitive)
{
   switch(primitive) {
   case PIPE_PRIM_POINTS:
      return 1;
   case PIPE_PRIM_LINES:
   case PIPE_PRIM_LINE_LOOP:
   case PIPE_PRIM_LINE_STRIP:
      return 2;
   case PIPE_PRIM_TRIANGLES:
   case PIPE_PRIM_TRIANGLE_STRIP:
   case PIPE_PRIM_TRIANGLE_FAN:
      return 3;
   case PIPE_PRIM_LINES_ADJACENCY:
   case PIPE_PRIM_LINE_STRIP_ADJACENCY:
      return 4;
   case PIPE_PRIM_TRIANGLES_ADJACENCY:
   case PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return 6;

   case PIPE_PRIM_QUADS:
   case PIPE_PRIM_QUAD_STRIP:
      /* these won't be seen from geometry shaders
         but prim assembly might for prim id. */
      return 4;

   /* following primitives should never be used
    * with geometry shaders abd their size is
    * undefined */
   case PIPE_PRIM_POLYGON:
   default:
      debug_printf("Unrecognized geometry shader primitive");
      return 3;
   }
}

/**
 * Returns the number of decomposed primitives for the given
 * vertex count.
 * Parts of the pipline are invoked once for each triangle in
 * triangle strip, triangle fans and triangles and once
 * for each line in line strip, line loop, lines. Also 
 * statistics depend on knowing the exact number of decomposed
 * primitives for a set of vertices.
 */
static inline unsigned
u_decomposed_prims_for_vertices(enum pipe_prim_type primitive, int vertices)
{
   switch (primitive) {
   case PIPE_PRIM_POINTS:
      return vertices;
   case PIPE_PRIM_LINES:
      return vertices / 2;
   case PIPE_PRIM_LINE_LOOP:
      return (vertices >= 2) ? vertices : 0;
   case PIPE_PRIM_LINE_STRIP:
      return (vertices >= 2) ? vertices - 1 : 0;
   case PIPE_PRIM_TRIANGLES:
      return vertices / 3;
   case PIPE_PRIM_TRIANGLE_STRIP:
      return (vertices >= 3) ? vertices - 2 : 0;
   case PIPE_PRIM_TRIANGLE_FAN:
      return (vertices >= 3) ? vertices - 2 : 0;
   case PIPE_PRIM_LINES_ADJACENCY:
      return vertices / 4;
   case PIPE_PRIM_LINE_STRIP_ADJACENCY:
      return (vertices >= 4) ? vertices - 3 : 0;
   case PIPE_PRIM_TRIANGLES_ADJACENCY:
      return vertices / 6;
   case PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY:
      return (vertices >= 6) ? 1 + (vertices - 6) / 2 : 0;
   case PIPE_PRIM_QUADS:
      return vertices / 4;
   case PIPE_PRIM_QUAD_STRIP:
      return (vertices >= 4) ? (vertices - 2) / 2 : 0;
   /* Polygons can't be decomposed
    * because the number of their vertices isn't known so
    * for them and whatever else we don't recognize just
    * return 1 if the number of vertices is greater than
    * or equal to 3 and zero otherwise */
   case PIPE_PRIM_POLYGON:
   default:
      debug_printf("Invalid decomposition primitive!\n");
      return (vertices >= 3) ? 1 : 0;
   }
}

/**
 * Returns the number of reduced/tessellated primitives for the given vertex
 * count.  Each quad is treated as two triangles.  Polygons are treated as
 * triangle fans.
 */
static inline unsigned
u_reduced_prims_for_vertices(enum pipe_prim_type primitive, int vertices)
{
   switch (primitive) {
   case PIPE_PRIM_QUADS:
   case PIPE_PRIM_QUAD_STRIP:
      return u_decomposed_prims_for_vertices(primitive, vertices) * 2;
   case PIPE_PRIM_POLYGON:
      primitive = PIPE_PRIM_TRIANGLE_FAN;
      FALLTHROUGH;
   default:
      return u_decomposed_prims_for_vertices(primitive, vertices);
   }
}

static inline enum pipe_prim_type
u_base_prim_type(enum pipe_prim_type prim_type)
{
   switch(prim_type) {
      case PIPE_PRIM_POINTS:
         return PIPE_PRIM_POINTS;
      case PIPE_PRIM_LINES:
      case PIPE_PRIM_LINE_LOOP:
      case PIPE_PRIM_LINE_STRIP:
      case PIPE_PRIM_LINES_ADJACENCY:
      case PIPE_PRIM_LINE_STRIP_ADJACENCY:
         return PIPE_PRIM_LINES;
      case PIPE_PRIM_TRIANGLES:
      case PIPE_PRIM_TRIANGLE_STRIP:
      case PIPE_PRIM_TRIANGLE_FAN:
      case PIPE_PRIM_TRIANGLES_ADJACENCY:
      case PIPE_PRIM_TRIANGLE_STRIP_ADJACENCY:
         return PIPE_PRIM_TRIANGLES;
      case PIPE_PRIM_QUADS:
      case PIPE_PRIM_QUAD_STRIP:
         return PIPE_PRIM_QUADS;
      default:
         return prim_type;
   }
}

static inline enum pipe_prim_type
u_tess_prim_from_shader(enum tess_primitive_mode shader_mode)
{
   switch (shader_mode) {
   case TESS_PRIMITIVE_TRIANGLES:
      return PIPE_PRIM_TRIANGLES;
   case TESS_PRIMITIVE_QUADS:
      return PIPE_PRIM_QUADS;
   case TESS_PRIMITIVE_ISOLINES:
      return PIPE_PRIM_LINES;
   default:
      return PIPE_PRIM_POINTS;
   }
}

static inline unsigned
u_vertices_for_prims(enum pipe_prim_type prim_type, int count)
{
   if (count <= 0)
      return 0;

   /* We can only figure out the number of vertices from a number of primitives
    * if we are using basic primitives (so no loops, strips, fans, etc).
    */
   assert(prim_type == u_base_prim_type(prim_type) &&
          prim_type != PIPE_PRIM_PATCHES && prim_type != PIPE_PRIM_POLYGON);

   const struct u_prim_vertex_count *info = u_prim_vertex_count(prim_type);
   assert(info);

   return info->min + (count - 1) * info->incr;
}

/**
 * Returns the number of stream out outputs for a given number of vertices and
 * primitive type.
 */

static inline unsigned
u_stream_outputs_for_vertices(enum pipe_prim_type primitive, unsigned nr)
{
   /* Extraneous vertices don't contribute to stream outputs */
   u_trim_pipe_prim(primitive, &nr);

   /* Polygons are special, since they are a single primitive with many
    * vertices. In this case, we just have an output for each vertex (after
    * trimming) */

   if (primitive == PIPE_PRIM_POLYGON)
      return nr;

   /* Normally, consider how many primitives are actually generated */
   unsigned prims = u_decomposed_prims_for_vertices(primitive, nr);

   /* One output per vertex after decomposition */
   enum pipe_prim_type base = u_base_prim_type(primitive);
   return u_vertices_for_prims(base, prims);
}

const char *u_prim_name(enum pipe_prim_type pipe_prim);


#ifdef __cplusplus
}
#endif


#endif
