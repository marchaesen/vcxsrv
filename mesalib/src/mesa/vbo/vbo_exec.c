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
#include "main/mtypes.h"
#include "main/api_arrayelt.h"
#include "main/vtxfmt.h"
#include "vbo_private.h"

const GLubyte
_vbo_attribute_alias_map[VP_MODE_MAX][VERT_ATTRIB_MAX] = {
   /* VP_FF: */
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

   /* VP_SHADER: */
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
vbo_exec_init(struct gl_context *ctx)
{
   struct vbo_exec_context *exec = &vbo_context(ctx)->exec;

   exec->ctx = ctx;

   /* aelt_context should have been created by the caller */
   assert(ctx->aelt_context);

   vbo_exec_vtx_init(exec);

   ctx->Driver.NeedFlush = 0;
   ctx->Driver.CurrentExecPrimitive = PRIM_OUTSIDE_BEGIN_END;

   /* The aelt_context state should still be dirty from its creation */
   assert(_ae_is_state_dirty(ctx));

   exec->array.recalculate_inputs = GL_TRUE;
   exec->eval.recalculate_maps = GL_TRUE;
}


void vbo_exec_destroy( struct gl_context *ctx )
{
   struct vbo_exec_context *exec = &vbo_context(ctx)->exec;

   if (ctx->aelt_context) {
      _ae_destroy_context( ctx );
      ctx->aelt_context = NULL;
   }

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
 * Helper function for determining if two subsequent glBegin/glEnd
 * primitives can be combined.  This is only possible for GL_POINTS,
 * GL_LINES, GL_TRIANGLES and GL_QUADS.
 * If we return true, it means that we can concatenate p1 onto p0 (and
 * discard p1).
 */
bool
vbo_can_merge_prims(const struct _mesa_prim *p0, const struct _mesa_prim *p1)
{
   if (!p0->begin ||
       !p1->begin ||
       !p0->end ||
       !p1->end)
      return false;

   /* The prim mode must match (ex: both GL_TRIANGLES) */
   if (p0->mode != p1->mode)
      return false;

   /* p1's vertices must come right after p0 */
   if (p0->start + p0->count != p1->start)
      return false;

   if (p0->basevertex != p1->basevertex ||
       p0->num_instances != p1->num_instances ||
       p0->base_instance != p1->base_instance)
      return false;

   /* can always merge subsequent GL_POINTS primitives */
   if (p0->mode == GL_POINTS)
      return true;

   /* independent lines with no extra vertices */
   if (p0->mode == GL_LINES && p0->count % 2 == 0 && p1->count % 2 == 0)
      return true;

   /* independent tris */
   if (p0->mode == GL_TRIANGLES && p0->count % 3 == 0 && p1->count % 3 == 0)
      return true;

   /* independent quads */
   if (p0->mode == GL_QUADS && p0->count % 4 == 0 && p1->count % 4 == 0)
      return true;

   return false;
}


/**
 * If we've determined that p0 and p1 can be merged, this function
 * concatenates p1 onto p0.
 */
void
vbo_merge_prims(struct _mesa_prim *p0, const struct _mesa_prim *p1)
{
   assert(vbo_can_merge_prims(p0, p1));

   p0->count += p1->count;
   p0->end = p1->end;
}
