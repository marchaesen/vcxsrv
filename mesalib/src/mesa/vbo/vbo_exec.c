/*
 * Mesa 3-D graphics library
 *
 * Copyright (C) 1999-2005  Brian Paul   All Rights Reserved.
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


#include "main/glheader.h"
#include "main/arrayobj.h"
#include "main/api_arrayelt.h"
#include "main/vtxfmt.h"
#include "vbo_private.h"

const GLubyte
_vbo_attribute_alias_map[VP_MODE_MAX][VERT_ATTRIB_MAX] = {
   /* VP_MODE_FF: */
   {
      VBO_ATTRIB_POS,                 /* VERT_ATTRIB_POS */
      VBO_ATTRIB_NORMAL,              /* VERT_ATTRIB_NORMAL */
      VBO_ATTRIB_COLOR0,              /* VERT_ATTRIB_COLOR0 */
      VBO_ATTRIB_COLOR1,              /* VERT_ATTRIB_COLOR1 */
      VBO_ATTRIB_FOG,                 /* VERT_ATTRIB_FOG */
      VBO_ATTRIB_COLOR_INDEX,         /* VERT_ATTRIB_COLOR_INDEX */
      VBO_ATTRIB_EDGEFLAG,            /* VERT_ATTRIB_EDGEFLAG */
      VBO_ATTRIB_TEX0,                /* VERT_ATTRIB_TEX0 */
      VBO_ATTRIB_TEX1,                /* VERT_ATTRIB_TEX1 */
      VBO_ATTRIB_TEX2,                /* VERT_ATTRIB_TEX2 */
      VBO_ATTRIB_TEX3,                /* VERT_ATTRIB_TEX3 */
      VBO_ATTRIB_TEX4,                /* VERT_ATTRIB_TEX4 */
      VBO_ATTRIB_TEX5,                /* VERT_ATTRIB_TEX5 */
      VBO_ATTRIB_TEX6,                /* VERT_ATTRIB_TEX6 */
      VBO_ATTRIB_TEX7,                /* VERT_ATTRIB_TEX7 */
      VBO_ATTRIB_POINT_SIZE,          /* VERT_ATTRIB_POINT_SIZE */
      VBO_ATTRIB_GENERIC0,            /* VERT_ATTRIB_GENERIC0 */
      VBO_ATTRIB_GENERIC1,            /* VERT_ATTRIB_GENERIC1 */
      VBO_ATTRIB_GENERIC2,            /* VERT_ATTRIB_GENERIC2 */
      VBO_ATTRIB_GENERIC3,            /* VERT_ATTRIB_GENERIC3 */
      VBO_ATTRIB_MAT_FRONT_AMBIENT,   /* VERT_ATTRIB_GENERIC4 */
      VBO_ATTRIB_MAT_BACK_AMBIENT,    /* VERT_ATTRIB_GENERIC5 */
      VBO_ATTRIB_MAT_FRONT_DIFFUSE,   /* VERT_ATTRIB_GENERIC6 */
      VBO_ATTRIB_MAT_BACK_DIFFUSE,    /* VERT_ATTRIB_GENERIC7 */
      VBO_ATTRIB_MAT_FRONT_SPECULAR,  /* VERT_ATTRIB_GENERIC8 */
      VBO_ATTRIB_MAT_BACK_SPECULAR,   /* VERT_ATTRIB_GENERIC9 */
      VBO_ATTRIB_MAT_FRONT_EMISSION,  /* VERT_ATTRIB_GENERIC10 */
      VBO_ATTRIB_MAT_BACK_EMISSION,   /* VERT_ATTRIB_GENERIC11 */
      VBO_ATTRIB_MAT_FRONT_SHININESS, /* VERT_ATTRIB_GENERIC12 */
      VBO_ATTRIB_MAT_BACK_SHININESS,  /* VERT_ATTRIB_GENERIC13 */
      VBO_ATTRIB_MAT_FRONT_INDEXES,   /* VERT_ATTRIB_GENERIC14 */
      VBO_ATTRIB_MAT_BACK_INDEXES     /* VERT_ATTRIB_GENERIC15 */
   },

   /* VP_MODE_SHADER: */
   {
      VBO_ATTRIB_POS,                 /* VERT_ATTRIB_POS */
      VBO_ATTRIB_NORMAL,              /* VERT_ATTRIB_NORMAL */
      VBO_ATTRIB_COLOR0,              /* VERT_ATTRIB_COLOR0 */
      VBO_ATTRIB_COLOR1,              /* VERT_ATTRIB_COLOR1 */
      VBO_ATTRIB_FOG,                 /* VERT_ATTRIB_FOG */
      VBO_ATTRIB_COLOR_INDEX,         /* VERT_ATTRIB_COLOR_INDEX */
      VBO_ATTRIB_EDGEFLAG,            /* VERT_ATTRIB_EDGEFLAG */
      VBO_ATTRIB_TEX0,                /* VERT_ATTRIB_TEX0 */
      VBO_ATTRIB_TEX1,                /* VERT_ATTRIB_TEX1 */
      VBO_ATTRIB_TEX2,                /* VERT_ATTRIB_TEX2 */
      VBO_ATTRIB_TEX3,                /* VERT_ATTRIB_TEX3 */
      VBO_ATTRIB_TEX4,                /* VERT_ATTRIB_TEX4 */
      VBO_ATTRIB_TEX5,                /* VERT_ATTRIB_TEX5 */
      VBO_ATTRIB_TEX6,                /* VERT_ATTRIB_TEX6 */
      VBO_ATTRIB_TEX7,                /* VERT_ATTRIB_TEX7 */
      VBO_ATTRIB_POINT_SIZE,          /* VERT_ATTRIB_POINT_SIZE */
      VBO_ATTRIB_GENERIC0,            /* VERT_ATTRIB_GENERIC0 */
      VBO_ATTRIB_GENERIC1,            /* VERT_ATTRIB_GENERIC1 */
      VBO_ATTRIB_GENERIC2,            /* VERT_ATTRIB_GENERIC2 */
      VBO_ATTRIB_GENERIC3,            /* VERT_ATTRIB_GENERIC3 */
      VBO_ATTRIB_GENERIC4,            /* VERT_ATTRIB_GENERIC4 */
      VBO_ATTRIB_GENERIC5,            /* VERT_ATTRIB_GENERIC5 */
      VBO_ATTRIB_GENERIC6,            /* VERT_ATTRIB_GENERIC6 */
      VBO_ATTRIB_GENERIC7,            /* VERT_ATTRIB_GENERIC7 */
      VBO_ATTRIB_GENERIC8,            /* VERT_ATTRIB_GENERIC8 */
      VBO_ATTRIB_GENERIC9,            /* VERT_ATTRIB_GENERIC9 */
      VBO_ATTRIB_GENERIC10,           /* VERT_ATTRIB_GENERIC10 */
      VBO_ATTRIB_GENERIC11,           /* VERT_ATTRIB_GENERIC11 */
      VBO_ATTRIB_GENERIC12,           /* VERT_ATTRIB_GENERIC12 */
      VBO_ATTRIB_GENERIC13,           /* VERT_ATTRIB_GENERIC13 */
      VBO_ATTRIB_GENERIC14,           /* VERT_ATTRIB_GENERIC14 */
      VBO_ATTRIB_GENERIC15            /* VERT_ATTRIB_GENERIC15 */
   }
};


void
vbo_exec_init(struct gl_context *ctx, bool use_buffer_objects)
{
   struct vbo_exec_context *exec = &vbo_context(ctx)->exec;

   exec->ctx = ctx;

   vbo_exec_vtx_init(exec, use_buffer_objects);

   ctx->Driver.NeedFlush = 0;
   ctx->Driver.CurrentExecPrimitive = PRIM_OUTSIDE_BEGIN_END;

   exec->eval.recalculate_maps = GL_TRUE;
}


void vbo_exec_destroy( struct gl_context *ctx )
{
   struct vbo_exec_context *exec = &vbo_context(ctx)->exec;

   vbo_exec_vtx_destroy( exec );
}


/**
 * In some degenarate cases we can improve our ability to merge
 * consecutive primitives.  For example:
 * glBegin(GL_LINE_STRIP);
 * glVertex(1);
 * glVertex(1);
 * glEnd();
 * glBegin(GL_LINE_STRIP);
 * glVertex(1);
 * glVertex(1);
 * glEnd();
 * Can be merged as a GL_LINES prim with four vertices.
 *
 * This function converts 2-vertex line strips/loops into GL_LINES, etc.
 */
void
vbo_try_prim_conversion(struct _mesa_prim *p)
{
   if (p->mode == GL_LINE_STRIP && p->count == 2) {
      /* convert 2-vertex line strip to a separate line */
      p->mode = GL_LINES;
   }
   else if ((p->mode == GL_TRIANGLE_STRIP || p->mode == GL_TRIANGLE_FAN)
       && p->count == 3) {
      /* convert 3-vertex tri strip or fan to a separate triangle */
      p->mode = GL_TRIANGLES;
   }

   /* Note: we can't convert a 4-vertex quad strip to a separate quad
    * because the vertex ordering is different.  We'd have to muck
    * around in the vertex data to make it work.
    */
}


/**
 * Function for merging two subsequent glBegin/glEnd draws.
 * Return true if p1 was concatenated onto p0 (to discard p1 in the caller).
 */
bool
vbo_merge_draws(struct gl_context *ctx, bool in_dlist,
                struct _mesa_prim *p0, const struct _mesa_prim *p1)
{
   /* The prim mode must match (ex: both GL_TRIANGLES) */
   if (p0->mode != p1->mode)
      return false;

   /* p1's vertices must come right after p0 */
   if (p0->start + p0->count != p1->start)
      return false;

   /* This checks whether mode is equal to any line primitive type, taking
    * advantage of the fact that primitives types go from 0 to 14.
    */
   if ((1 << p0->mode) &
       ((1 << GL_LINES) |
        (1 << GL_LINE_LOOP) |
        (1 << GL_LINE_STRIP) |
        (1 << GL_LINES_ADJACENCY) |
        (1 << GL_LINE_STRIP_ADJACENCY))) {
      /* "begin" resets the line stipple pattern during line stipple emulation
       * in tnl.
       *
       * StippleFlag can be unknown when compiling a display list.
       *
       * Other uses of "begin" are internal to the vbo module, and in those
       * cases, "begin" is not used after merging draws.
       */
      if (p1->begin == 1 && (in_dlist || ctx->Line.StippleFlag))
         return false;

      /* _mesa_prim::end is irrelevant at this point and is only used
       * before this function is called.
       */
   }

   assert(p0->basevertex == p1->basevertex);

   switch (p0->mode) {
   case GL_POINTS:
      /* can always merge subsequent GL_POINTS primitives */
      break;
   /* check independent primitives with no extra vertices */
   case GL_LINES:
      if (p0->count % 2)
         return false;
      break;
   case GL_TRIANGLES:
      if (p0->count % 3)
         return false;
      break;
   case GL_QUADS:
   case GL_LINES_ADJACENCY:
      if (p0->count % 4)
         return false;
      break;
   case GL_TRIANGLES_ADJACENCY:
      if (p0->count % 6)
         return false;
      break;
   case GL_PATCHES:
      /* "patch_vertices" can be unknown when compiling a display list. */
      if (in_dlist ||
          p0->count % ctx->TessCtrlProgram.patch_vertices)
         return false;
      break;
   default:
      return false;
   }

   /* Merge draws. */
   p0->count += p1->count;
   p0->end = p1->end;
   return true;
}

/**
 * Copy zero, one or two vertices from the current vertex buffer into
 * the temporary "copy" buffer.
 * This is used when a single primitive overflows a vertex buffer and
 * we need to continue the primitive in a new vertex buffer.
 * The temporary "copy" buffer holds the vertices which need to get
 * copied from the old buffer to the new one.
 */
unsigned
vbo_copy_vertices(struct gl_context *ctx,
                  GLenum mode,
                  struct _mesa_prim *last_prim,
                  unsigned vertex_size,
                  bool in_dlist,
                  fi_type *dst,
                  const fi_type *src)
{
   const unsigned count = last_prim->count;
   unsigned copy = 0;

   switch (mode) {
   case GL_POINTS:
      return 0;
   case GL_LINES:
      copy = count % 2;
      break;
   case GL_TRIANGLES:
      copy = count % 3;
      break;
   case GL_QUADS:
   case GL_LINES_ADJACENCY:
      copy = count % 4;
      break;
   case GL_TRIANGLES_ADJACENCY:
      copy = count % 6;
      break;
   case GL_LINE_STRIP:
      copy = MIN2(1, count);
      break;
   case GL_LINE_STRIP_ADJACENCY:
      /* We need to copy 3 vertices, because:
       *    Last strip:  ---o---o---x     (last line)
       *    Next strip:     x---o---o---  (next line)
       */
      copy = MIN2(3, count);
      break;
   case GL_PATCHES:
      if (in_dlist) {
         /* We don't know the value of GL_PATCH_VERTICES when compiling
          * a display list.
          *
          * Fail an assertion in debug builds and use the value of 3
          * in release builds, which is more likely than any other value.
          */
         assert(!"patch_vertices is unknown");
         copy = count % 3;
      } else {
         copy = count % ctx->TessCtrlProgram.patch_vertices;
      }
      break;
   case GL_LINE_LOOP:
      if (!in_dlist && last_prim->begin == 0) {
         /* We're dealing with the second or later section of a split/wrapped
          * GL_LINE_LOOP.  Since we're converting line loops to line strips,
          * we've already incremented the last_prim->start counter by one to
          * skip the 0th vertex in the loop.  We need to undo that (effectively
          * subtract one from last_prim->start) so that we copy the 0th vertex
          * to the next vertex buffer.
          */
         assert(last_prim->start > 0);
         src -= vertex_size;
      }
      /* fall-through */
   case GL_TRIANGLE_FAN:
   case GL_POLYGON:
      if (count == 0) {
         return 0;
      } else if (count == 1) {
         memcpy(dst, src + 0, vertex_size * sizeof(GLfloat));
         return 1;
      } else {
         memcpy(dst, src + 0, vertex_size * sizeof(GLfloat));
         memcpy(dst + vertex_size, src + (count - 1) * vertex_size,
                vertex_size * sizeof(GLfloat));
         return 2;
      }
   case GL_TRIANGLE_STRIP:
      /* Draw an even number of triangles to keep front/back facing the same. */
      last_prim->count -= count % 2;
      /* fallthrough */
   case GL_QUAD_STRIP:
      if (count <= 1)
         copy = count;
      else
         copy = 2 + (count % 2);
      break;
   case PRIM_OUTSIDE_BEGIN_END:
      return 0;
   case GL_TRIANGLE_STRIP_ADJACENCY:
      /* TODO: Splitting tri strips with adjacency is too complicated. */
   default:
      unreachable("Unexpected primitive type");
      return 0;
   }

   memcpy(dst, src + (count - copy) * vertex_size,
          copy * vertex_size * sizeof(GLfloat));
   return copy;
}
